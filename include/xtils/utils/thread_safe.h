#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <utility>
namespace xtils {

template <typename T>
class ThreadSafe {
 public:
  using value_type = typename T::value_type;
  ThreadSafe() = default;
  ~ThreadSafe() { Quit(); }

  bool PopWait(value_type& e,
               std::chrono::seconds timeout = std::chrono::seconds::max()) {
    std::unique_lock<std::mutex> lock(mtx_);
    if (timeout != std::chrono::seconds::max()) {
      auto ret = cv_.wait_for(lock, timeout,
                              [&]() { return !data_.empty() || quit_; });
      if (quit_ || !ret) return false;

    } else {
      cv_.wait(lock, [&]() { return !data_.empty() || quit_; });
      if (quit_) return false;
    }
    e = std::move(data_.front());
    data_.pop_front();
    return true;
  }

  bool TryPop(value_type& e) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (data_.empty()) return false;
    e = std::move(data_.front());
    data_.pop_front();
    return true;
  }

  void Push(const value_type& e) {
    std::lock_guard<std::mutex> lock(mtx_);
    data_.push_back(e);
    cv_.notify_all();
  }

  void Push(value_type&& e) {
    std::lock_guard<std::mutex> lock(mtx_);
    data_.push_back(std::move(e));
    cv_.notify_all();
  }

  void Clear() {
    std::lock_guard<std::mutex> lock(mtx_);
    data_.clear();
    cv_.notify_all();
  }

  std::size_t Size() {
    std::lock_guard<std::mutex> lock(mtx_);
    return data_.size();
  }

  void Quit() {
    std::lock_guard<std::mutex> lock(mtx_);
    quit_ = true;
    cv_.notify_all();
  }

 private:
  T data_;
  bool quit_{false};
  std::condition_variable cv_;
  std::mutex mtx_;
};

}  // namespace xtils
