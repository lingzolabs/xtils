#include "xtils/tasks/task_group.h"

#include <exception>
#include <memory>
#include <string>

#include "xtils/logging/logger.h"
#include "xtils/system/platform.h"
#include "xtils/tasks/task_runner.h"
#include "xtils/tasks/thread_task_runner.h"
#include "xtils/utils/string_utils.h"

namespace xtils {

std::atomic_int TaskGroup::id_counter_{0};

void TaskGroup::runLoop(int id) {
  StackString<10> name("T%02d-%02d", group_id_, id);
  xtils::MaybeSetThreadName(name.c_str());
  Task task;
  while (!quit_) {
    if (tasks_.PopWait(task)) {
      try {
        task();
      } catch (const std::exception& e) {
        LogW("task exception: %s", e.what());
      } catch (...) {
        LogW("task exception: unknown");
      }
      pending_tasks_.fetch_sub(1);
    }
    if (exit_id_.load() == id) {
      loopExited(id);
    }
  }
}

void TaskGroup::loopExited(int id) {
  LogI("thread %d exit", id);
  exit_id_.store(-1);
}

TaskGroup::TaskGroup(int size, std::shared_ptr<TaskRunner> runner)
    : weak_factory_(this) {
  group_id_ = id_counter_.fetch_add(1);
  if (runner) {
    main_runner_ = runner;
  } else {
    StackString<32> main_name("mainLoop-%02d", group_id_);
    main_runner_ = ThreadTaskRunner::CreateAndStartShared(main_name.c_str());
  }
  if (size <= 0) {
    size = std::thread::hardware_concurrency();
  }
  for (int i = 0; i < size; i++) {
    threads_.emplace_back(&TaskGroup::runLoop, this, i);
  }
}

TaskGroup::~TaskGroup() {
  if (!quit_) {
    Stop();
  }
}

void TaskGroup::PostTask(Task task) { main_runner_->PostTask(std::move(task)); }

void TaskGroup::PostAsyncTask(Task task, uint32_t ms) {
  if (ms == 0) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (quit_.load()) return;
    pending_tasks_.fetch_add(1);
    tasks_.Push(std::move(task));
  } else {
    auto weak = weak_factory_.GetWeakPtr();
    main_runner_->PostDelayedTask(
        [task = std::move(task), weak]() mutable {
          if (auto ptr = weak.get()) {
            std::lock_guard<std::mutex> lock(ptr->state_mutex_);
            if (ptr->quit_.load()) return;
            ptr->pending_tasks_.fetch_add(1);
            ptr->tasks_.Push(std::move(task));
          }
        },
        ms);
  }
}

std::shared_ptr<TaskRunner> TaskGroup::MainRunner() { return main_runner_; }

bool TaskGroup::IsBusy() { return pending_tasks_.load() > 0; }
int TaskGroup::Size() { return threads_.size(); }
void TaskGroup::Stop() {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    quit_ = true;
    tasks_.Quit();
  }
  for (auto& t : threads_) {
    if (t.joinable()) t.join();
  }
}

bool TaskGroup::StopWaitAll(std::chrono::seconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (IsBusy()) {
    if (std::chrono::steady_clock::now() >= deadline) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  Stop();
  return true;
}
}  // namespace xtils
