#include "xtils/tasks/thread_task_runner.h"

#include <sys/prctl.h>

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

#include "xtils/logging/logger.h"
#include "xtils/tasks/unix_task_runner.h"

namespace xtils {

ThreadTaskRunner::ThreadTaskRunner(ThreadTaskRunner&& other) noexcept
    : thread_(std::move(other.thread_)), task_runner_(other.task_runner_) {
  other.task_runner_ = nullptr;
}

ThreadTaskRunner& ThreadTaskRunner::operator=(ThreadTaskRunner&& other) {
  this->~ThreadTaskRunner();
  new (this) ThreadTaskRunner(std::move(other));
  return *this;
}

ThreadTaskRunner::~ThreadTaskRunner() { Stop(); }

void ThreadTaskRunner::Stop() {
  std::lock_guard<std::mutex> stopping(stop_mutex_);
  {
    std::lock_guard<std::mutex> lock(runner_mutex_);
    if (task_runner_) {
      XTILS_CHECK(!task_runner_->RunsTasksOnCurrentThread());
      task_runner_->Quit();
    }
  }
  if (thread_.joinable()) thread_.join();
}

ThreadTaskRunner::ThreadTaskRunner(const std::string& name) : name_(name) {
  std::mutex init_lock;
  std::condition_variable init_cv;

  std::function<void(UnixTaskRunner*)> initializer =
      [this, &init_lock, &init_cv](UnixTaskRunner* task_runner) {
        std::lock_guard<std::mutex> lock(init_lock);
        task_runner_ = task_runner;
        // Notify while still holding the lock, as init_cv ceases to exist as
        // soon as the main thread observes a non-null task_runner_, and it can
        // wake up spuriously (i.e. before the notify if we had unlocked before
        // notifying).
        init_cv.notify_one();
      };

  thread_ = std::thread(&ThreadTaskRunner::RunTaskThread, this,
                        std::move(initializer));

  std::unique_lock<std::mutex> lock(init_lock);
  init_cv.wait(lock, [this] { return !!task_runner_; });
}

void ThreadTaskRunner::RunTaskThread(
    std::function<void(UnixTaskRunner*)> initializer) {
  if (!name_.empty()) {
    xtils::MaybeSetThreadName(name_);
  }

  UnixTaskRunner task_runner;
  task_runner.PostTask(std::bind(std::move(initializer), &task_runner));
  task_runner.Run();
  // Publish rejection before the stack-allocated runner is destroyed. A
  // concurrent TryPostTask holds this lock while it accesses the pointer.
  std::lock_guard<std::mutex> lock(runner_mutex_);
  task_runner_ = nullptr;
}

void ThreadTaskRunner::PostTask(std::function<void()> task) {
  TryPostTask(std::move(task));
}

bool ThreadTaskRunner::TryPostTask(std::function<void()> task) {
  std::lock_guard<std::mutex> lock(runner_mutex_);
  return task_runner_ && task_runner_->TryPostTask(std::move(task));
}

bool ThreadTaskRunner::AcceptsNewWork() const {
  std::lock_guard<std::mutex> lock(runner_mutex_);
  return task_runner_ && task_runner_->AcceptsNewWork();
}

TaskRunner::ShutdownTaskHandle ThreadTaskRunner::RegisterShutdownTask(
    std::function<void()> task) {
  std::lock_guard<std::mutex> lock(runner_mutex_);
  return task_runner_ ? task_runner_->RegisterShutdownTask(std::move(task))
                      : kInvalidShutdownTaskHandle;
}

bool ThreadTaskRunner::UnregisterShutdownTask(ShutdownTaskHandle handle) {
  std::lock_guard<std::mutex> lock(runner_mutex_);
  return task_runner_ && task_runner_->UnregisterShutdownTask(handle);
}

void ThreadTaskRunner::PostDelayedTask(std::function<void()> task,
                                       uint32_t delay_ms) {
  task_runner_->PostDelayedTask(std::move(task), delay_ms);
}

ThreadTaskRunner::DelayedTaskHandle ThreadTaskRunner::PostDelayedTaskWithHandle(
    std::function<void()> task, uint32_t delay_ms) {
  return task_runner_->PostDelayedTaskWithHandle(std::move(task), delay_ms);
}

bool ThreadTaskRunner::CancelDelayedTask(DelayedTaskHandle handle) {
  return task_runner_->CancelDelayedTask(handle);
}

void ThreadTaskRunner::AddFileDescriptorWatch(
    PlatformHandle handle, std::function<void()> watch_task) {
  task_runner_->AddFileDescriptorWatch(handle, std::move(watch_task));
}

bool ThreadTaskRunner::SetFileDescriptorWatchWritable(PlatformHandle handle,
                                                      bool enabled) {
  return task_runner_->SetFileDescriptorWatchWritable(handle, enabled);
}

bool ThreadTaskRunner::SetFileDescriptorWatchReadable(PlatformHandle handle,
                                                      bool enabled) {
  return task_runner_->SetFileDescriptorWatchReadable(handle, enabled);
}

void ThreadTaskRunner::RemoveFileDescriptorWatch(PlatformHandle handle) {
  task_runner_->RemoveFileDescriptorWatch(handle);
}

bool ThreadTaskRunner::RunsTasksOnCurrentThread() const {
  std::lock_guard<std::mutex> lock(runner_mutex_);
  return task_runner_ && task_runner_->RunsTasksOnCurrentThread();
}

}  // namespace xtils
