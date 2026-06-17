#pragma once

#include <stdint.h>

#include <chrono>
#include <functional>

#include "xtils/system/platform.h"

namespace xtils {
using Task = std::function<void()>;
class TaskRunner {
 public:
  // Opaque handle returned by PostDelayedTaskWithHandle(). 0 means "invalid".
  using DelayedTaskHandle = uint64_t;
  static constexpr DelayedTaskHandle kInvalidDelayedTaskHandle = 0;

  virtual ~TaskRunner() = default;

  virtual void PostTask(std::function<void()>) = 0;

  virtual void PostDelayedTask(std::function<void()>, uint32_t delay_ms) = 0;

  // Like PostDelayedTask but returns a handle that can be passed to
  // CancelDelayedTask(). Default implementation calls PostDelayedTask and
  // returns kInvalidDelayedTaskHandle, meaning cancellation is not supported.
  virtual DelayedTaskHandle PostDelayedTaskWithHandle(std::function<void()> t,
                                                     uint32_t delay_ms) {
    PostDelayedTask(std::move(t), delay_ms);
    return kInvalidDelayedTaskHandle;
  }

  // Try to cancel a previously scheduled delayed task. Returns true if the
  // task was cancelled before it ran. Returns false if the task already ran,
  // already started running, or the handle is unknown / invalid. Default
  // implementation always returns false.
  virtual bool CancelDelayedTask(DelayedTaskHandle /*handle*/) {
    return false;
  }

  // Steady-clock "now" for this runner. Default uses std::steady_clock so
  // tests can override with a fake. Callers wanting absolute scheduling
  // should compute (target - Now()) and call PostDelayedTask.
  virtual std::chrono::steady_clock::time_point Now() const {
    return std::chrono::steady_clock::now();
  }

  // Convenience: schedule task to run at an absolute steady-clock time.
  // Implemented in terms of Now() + PostDelayedTask, so subclasses don't
  // need to override unless they want a more efficient implementation.
  void PostTaskAt(std::chrono::steady_clock::time_point at,
                  std::function<void()> task) {
    auto now = Now();
    uint32_t delay_ms = 0;
    if (at > now) {
      auto d = std::chrono::duration_cast<std::chrono::milliseconds>(at - now)
                   .count();
      // Clamp to uint32_t range.
      if (d > 0) {
        delay_ms = static_cast<uint32_t>(
            d > static_cast<int64_t>(UINT32_MAX) ? UINT32_MAX : d);
      }
    }
    if (delay_ms == 0) {
      PostTask(std::move(task));
    } else {
      PostDelayedTask(std::move(task), delay_ms);
    }
  }

  virtual void AddFileDescriptorWatch(PlatformHandle,
                                      std::function<void()>) = 0;

  virtual void RemoveFileDescriptorWatch(PlatformHandle) = 0;

  virtual bool RunsTasksOnCurrentThread() const = 0;
};

}  // namespace xtils
