/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "xtils/tasks/unix_task_runner.h"

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include <algorithm>

#include "xtils/debug/tracer.h"
#include "xtils/logging/logger.h"
#include "xtils/logging/watchdog.h"

namespace xtils {

UnixTaskRunner::UnixTaskRunner() {
  AddFileDescriptorWatch(event_.fd(), [] {
    // Not reached -- see PostFileDescriptorWatches().
    XTILS_FATAL("Should be unreachable.");
  });
}

UnixTaskRunner::~UnixTaskRunner() {
  // Pending closures can own sockets whose destructors remove watches or post
  // further cleanup. Destroy them while all runner members are still alive,
  // outside the lock and outside the containers being mutated by cleanup.
  for (;;) {
    decltype(immediate_tasks_) immediate;
    decltype(delayed_tasks_) delayed;
    decltype(watch_tasks_) watches;
    {
      std::lock_guard<std::mutex> lock(lock_);
      if (immediate_tasks_.empty() && delayed_tasks_.empty() &&
          watch_tasks_.empty())
        break;
      immediate.swap(immediate_tasks_);
      delayed.swap(delayed_tasks_);
      watches.swap(watch_tasks_);
    }
  }
}

void UnixTaskRunner::WakeUp() { event_.Notify(); }

void UnixTaskRunner::Run() {
  created_thread_id_.store(GetThreadId(), std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(lock_);
    quit_ = false;
    running_loop_ = true;
  }
  bool stopping = false;
  for (;;) {
    int poll_timeout_ms = 0;
    decltype(shutdown_tasks_) callbacks;
    {
      std::lock_guard<std::mutex> lock(lock_);
      if (quit_) {
        if (!stopping) callbacks.swap(shutdown_tasks_);
        stopping = true;
        if (callbacks.empty() && immediate_tasks_.empty()) {
          running_loop_ = false;
          return;
        }
      } else {
        poll_timeout_ms = GetDelayMsToNextTaskLocked();
        UpdateWatchTasksLocked();
      }
    }
    if (stopping) {
      for (auto& entry : callbacks) entry.second();
      // Shutdown callbacks may schedule completion tasks. Drain those before
      // leaving, but do not execute delayed tasks or poll watches.
      std::function<void()> immediate;
      {
        std::lock_guard<std::mutex> lock(lock_);
        if (!immediate_tasks_.empty()) {
          immediate = std::move(immediate_tasks_.front());
          immediate_tasks_.pop_front();
        }
      }
      if (immediate) RunTaskWithWatchdogGuard(immediate);
      continue;
    }

    int ret = poll(&poll_fds_[0], static_cast<nfds_t>(poll_fds_.size()),
                   poll_timeout_ms);
    XTILS_CHECK(ret >= 0);
    PostFileDescriptorWatches(0 /*ignored*/);

    // To avoid starvation we always interleave all types of tasks -- immediate,
    // delayed and file descriptor watches.
    RunImmediateAndDelayedTask();
  }
}

void UnixTaskRunner::Quit() {
  std::lock_guard<std::mutex> lock(lock_);
  quit_ = true;
  WakeUp();
}

bool UnixTaskRunner::QuitCalled() {
  std::lock_guard<std::mutex> lock(lock_);
  return quit_;
}

bool UnixTaskRunner::IsIdleForTesting() {
  std::lock_guard<std::mutex> lock(lock_);
  return immediate_tasks_.empty();
}

void UnixTaskRunner::AdvanceTimeForTesting(uint32_t ms) {
  std::lock_guard<std::mutex> lock(lock_);
  advanced_time_for_testing_ += TimeMillis(ms);
}

void UnixTaskRunner::UpdateWatchTasksLocked() {
  if (!watch_tasks_changed_) return;
  watch_tasks_changed_ = false;
  poll_fds_.clear();
  for (auto& it : watch_tasks_) {
    PlatformHandle handle = it.first;
    WatchTask& watch_task = it.second;
    watch_task.poll_fd_index = poll_fds_.size();
    short events = watch_task.readable ? POLLIN : 0;
    if (watch_task.writable) events |= POLLOUT;
    poll_fds_.push_back({handle, events, 0});
  }
}

void UnixTaskRunner::RunImmediateAndDelayedTask() {
  TRACE_SCOPE("UnixTaskRunner:RunImmediateAndDelayedTask");
  // If locking overhead becomes an issue, add a separate work queue.
  std::function<void()> immediate_task;
  std::function<void()> delayed_task;
  TimeMillis now = GetWallTimeMs();
  {
    std::lock_guard<std::mutex> lock(lock_);
    if (!immediate_tasks_.empty()) {
      immediate_task = std::move(immediate_tasks_.front());
      immediate_tasks_.pop_front();
    }
    if (!delayed_tasks_.empty()) {
      auto it = delayed_tasks_.begin();
      if (now + advanced_time_for_testing_ >= it->first) {
        delayed_task = std::move(it->second.task);
        delayed_tasks_.erase(it);
      }
    }
  }

  errno = 0;
  if (immediate_task) RunTaskWithWatchdogGuard(immediate_task);
  errno = 0;
  if (delayed_task) RunTaskWithWatchdogGuard(delayed_task);
}

void UnixTaskRunner::PostFileDescriptorWatches(uint64_t windows_wait_result) {
  TRACE_SCOPE("UnixTaskRunner::PostFileDescriptorWatches");
  for (size_t i = 0; i < poll_fds_.size(); i++) {
    const PlatformHandle handle = poll_fds_[i].fd;
    if (!(poll_fds_[i].revents &
          (POLLIN | POLLOUT | POLLHUP | POLLERR | POLLNVAL)))
      continue;
    poll_fds_[i].revents = 0;

    // The wake-up event is handled inline to avoid an infinite recursion of
    // posted tasks.
    if (handle == event_.fd()) {
      event_.Clear();
      continue;
    }

    // Binding to |this| is safe since we are the only object executing the
    // task.
    PostTask(std::bind(&UnixTaskRunner::RunFileDescriptorWatch, this, handle));

    // On UNIX systems instead, we just make the fd negative while its task is
    // pending. This makes poll(2) ignore the fd.
    // XTILS_DCHECK(poll_fds_[i].fd >= 0);
    poll_fds_[i].fd = -poll_fds_[i].fd;
  }
}

void UnixTaskRunner::RunFileDescriptorWatch(PlatformHandle fd) {
  std::function<void()> task;
  {
    std::lock_guard<std::mutex> lock(lock_);
    auto it = watch_tasks_.find(fd);
    if (it == watch_tasks_.end()) return;
    WatchTask& watch_task = it->second;

    // Make poll(2) pay attention to the fd again. Since another thread may have
    // updated this watch we need to refresh the set first.
    UpdateWatchTasksLocked();

    size_t fd_index = watch_task.poll_fd_index;
    XTILS_DCHECK(fd_index < poll_fds_.size());
    XTILS_DCHECK(::abs(poll_fds_[fd_index].fd) == fd);
    poll_fds_[fd_index].fd = fd;
    task = watch_task.callback;
  }
  errno = 0;
  RunTaskWithWatchdogGuard(task);
}

int UnixTaskRunner::GetDelayMsToNextTaskLocked() const {
  if (!immediate_tasks_.empty()) return 0;
  if (!delayed_tasks_.empty()) {
    TimeMillis diff = delayed_tasks_.begin()->first - GetWallTimeMs() -
                      advanced_time_for_testing_;
    return std::max(0, static_cast<int>(diff.count()));
  }
  return -1;
}

void UnixTaskRunner::PostTask(std::function<void()> task) {
  TryPostTask(std::move(task));
}

bool UnixTaskRunner::AcceptsNewWork() const {
  std::lock_guard<std::mutex> lock(lock_);
  return !quit_;
}

TaskRunner::ShutdownTaskHandle UnixTaskRunner::RegisterShutdownTask(
    std::function<void()> task) {
  std::lock_guard<std::mutex> lock(lock_);
  if (quit_) return kInvalidShutdownTaskHandle;
  const auto handle = next_shutdown_handle_++;
  shutdown_tasks_.emplace(handle, std::move(task));
  return handle;
}

bool UnixTaskRunner::UnregisterShutdownTask(ShutdownTaskHandle handle) {
  std::lock_guard<std::mutex> lock(lock_);
  return shutdown_tasks_.erase(handle) != 0;
}

bool UnixTaskRunner::TryPostTask(std::function<void()> task) {
  TRACE_SCOPE("UnixTaskRunner:PostTask");
  bool was_empty;
  {
    std::lock_guard<std::mutex> lock(lock_);
    if (quit_ && !(running_loop_ && RunsTasksOnCurrentThread())) return false;
    was_empty = immediate_tasks_.empty();
    immediate_tasks_.push_back(std::move(task));
  }
  if (was_empty) WakeUp();
  return true;
}

void UnixTaskRunner::PostDelayedTask(std::function<void()> task,
                                     uint32_t delay_ms) {
  TimeMillis runtime = GetWallTimeMs() + TimeMillis(delay_ms);
  {
    std::lock_guard<std::mutex> lock(lock_);
    DelayedSlot slot;
    slot.task = std::move(task);
    delayed_tasks_.insert(
        std::make_pair(runtime + advanced_time_for_testing_, std::move(slot)));
  }
  WakeUp();
}

UnixTaskRunner::DelayedTaskHandle UnixTaskRunner::PostDelayedTaskWithHandle(
    std::function<void()> task, uint32_t delay_ms) {
  TimeMillis runtime = GetWallTimeMs() + TimeMillis(delay_ms);
  DelayedTaskHandle handle =
      next_delayed_handle_.fetch_add(1, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(lock_);
    DelayedSlot slot;
    slot.task = std::move(task);
    slot.handle = handle;
    delayed_tasks_.insert(
        std::make_pair(runtime + advanced_time_for_testing_, std::move(slot)));
  }
  WakeUp();
  return handle;
}

bool UnixTaskRunner::CancelDelayedTask(DelayedTaskHandle handle) {
  if (handle == kInvalidDelayedTaskHandle) return false;
  std::lock_guard<std::mutex> lock(lock_);
  for (auto it = delayed_tasks_.begin(); it != delayed_tasks_.end(); ++it) {
    if (it->second.handle == handle) {
      delayed_tasks_.erase(it);
      return true;
    }
  }
  return false;
}

void UnixTaskRunner::AddFileDescriptorWatch(PlatformHandle fd,
                                            std::function<void()> task) {
  {
    std::lock_guard<std::mutex> lock(lock_);
    XTILS_DCHECK(!watch_tasks_.count(fd));
    WatchTask& watch_task = watch_tasks_[fd];
    watch_task.callback = std::move(task);
    watch_task.poll_fd_index = SIZE_MAX;
    watch_tasks_changed_ = true;
  }
  WakeUp();
}

bool UnixTaskRunner::SetFileDescriptorWatchWritable(PlatformHandle fd,
                                                    bool enabled) {
  {
    std::lock_guard<std::mutex> lock(lock_);
    auto it = watch_tasks_.find(fd);
    if (it == watch_tasks_.end()) return false;
    if (it->second.writable == enabled) return true;
    it->second.writable = enabled;
    watch_tasks_changed_ = true;
  }
  WakeUp();
  return true;
}

bool UnixTaskRunner::SetFileDescriptorWatchReadable(PlatformHandle fd,
                                                    bool enabled) {
  {
    std::lock_guard<std::mutex> lock(lock_);
    auto it = watch_tasks_.find(fd);
    if (it == watch_tasks_.end()) return false;
    if (it->second.readable == enabled) return true;
    it->second.readable = enabled;
    watch_tasks_changed_ = true;
  }
  WakeUp();
  return true;
}

void UnixTaskRunner::RemoveFileDescriptorWatch(PlatformHandle fd) {
  {
    std::lock_guard<std::mutex> lock(lock_);
    watch_tasks_.erase(fd);
    watch_tasks_changed_ = true;
  }
  // No need to schedule a wake-up for this.
}

bool UnixTaskRunner::RunsTasksOnCurrentThread() const {
  return GetThreadId() == created_thread_id_.load(std::memory_order_relaxed);
}

}  // namespace xtils
