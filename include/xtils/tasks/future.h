#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <variant>

#include "xtils/tasks/task_group.h"
#include "xtils/utils/result.h"

namespace xtils {

template <typename T>
class Future;

template <typename T>
class Promise;

namespace detail {

template <typename T>
struct SharedState {
  std::mutex mutex;
  std::variant<std::monostate, T, Error> value;
  std::function<void()> continuation;
  TaskGroup* executor = nullptr;
  bool ready = false;

  void SetValue(T&& val) {
    std::function<void()> cont;
    {
      std::lock_guard<std::mutex> lock(mutex);
      value = std::move(val);
      ready = true;
      cont = std::move(continuation);
    }
    if (cont) {
      if (executor) {
        executor->PostAsyncTask(std::move(cont));
      } else {
        cont();
      }
    }
  }

  void SetError(Error&& err) {
    std::function<void()> cont;
    {
      std::lock_guard<std::mutex> lock(mutex);
      value = std::move(err);
      ready = true;
      cont = std::move(continuation);
    }
    if (cont) {
      if (executor) {
        executor->PostAsyncTask(std::move(cont));
      } else {
        cont();
      }
    }
  }

  void SetContinuation(std::function<void()> cont, TaskGroup* exec) {
    std::lock_guard<std::mutex> lock(mutex);
    executor = exec;
    if (ready) {
      if (exec) {
        exec->PostAsyncTask(std::move(cont));
      } else {
        cont();
      }
    } else {
      continuation = std::move(cont);
    }
  }

  bool IsReady() const { return ready; }

  bool HasValue() const {
    return value.index() == 1;
  }

  bool HasError() const {
    return value.index() == 2;
  }

  T& GetValue() { return std::get<1>(value); }
  Error& GetError() { return std::get<2>(value); }
};

// Void specialization
template <>
struct SharedState<void> {
  std::mutex mutex;
  std::variant<std::monostate, std::monostate, Error> value;
  std::function<void()> continuation;
  TaskGroup* executor = nullptr;
  bool ready = false;

  void SetValue() {
    std::function<void()> cont;
    {
      std::lock_guard<std::mutex> lock(mutex);
      value.template emplace<1>(std::monostate{});
      ready = true;
      cont = std::move(continuation);
    }
    if (cont) {
      if (executor) {
        executor->PostAsyncTask(std::move(cont));
      } else {
        cont();
      }
    }
  }

  void SetError(Error&& err) {
    std::function<void()> cont;
    {
      std::lock_guard<std::mutex> lock(mutex);
      value = std::move(err);
      ready = true;
      cont = std::move(continuation);
    }
    if (cont) {
      if (executor) {
        executor->PostAsyncTask(std::move(cont));
      } else {
        cont();
      }
    }
  }

  void SetContinuation(std::function<void()> cont, TaskGroup* exec) {
    std::lock_guard<std::mutex> lock(mutex);
    executor = exec;
    if (ready) {
      if (exec) {
        exec->PostAsyncTask(std::move(cont));
      } else {
        cont();
      }
    } else {
      continuation = std::move(cont);
    }
  }

  bool IsReady() const { return ready; }
  bool HasValue() const { return value.index() == 1; }
  bool HasError() const { return value.index() == 2; }
  Error& GetError() { return std::get<2>(value); }
};

}  // namespace detail

// Promise<T> — the write-end of a Future.
template <typename T>
class Promise {
 public:
  Promise() : state_(std::make_shared<detail::SharedState<T>>()) {}

  Future<T> GetFuture();

  void SetValue(T&& value) { state_->SetValue(std::move(value)); }
  void SetValue(const T& value) {
    T copy = value;
    state_->SetValue(std::move(copy));
  }
  void SetError(Error err) { state_->SetError(std::move(err)); }

 private:
  std::shared_ptr<detail::SharedState<T>> state_;
};

template <>
class Promise<void> {
 public:
  Promise() : state_(std::make_shared<detail::SharedState<void>>()) {}

  Future<void> GetFuture();

  void SetValue() { state_->SetValue(); }
  void SetError(Error err) { state_->SetError(std::move(err)); }

 private:
  std::shared_ptr<detail::SharedState<void>> state_;
  friend class Future<void>;
};

// Future<T> — a chainable async result.
//
// Usage:
//   Promise<int> p;
//   auto future = p.GetFuture();
//
//   future
//     .Then([](int v) { return v * 2; })
//     .Then([](int v) { printf("result: %d\n", v); })
//     .OnError([](const Error& e) { printf("err: %s\n", e.message.c_str()); });
//
//   p.SetValue(21);  // prints "result: 42"
//
template <typename T>
class Future {
 public:
  Future() = default;

  // Then: transform the value, returns a new Future
  template <typename F>
  auto Then(F&& func, TaskGroup* exec = nullptr)
      -> Future<decltype(func(std::declval<T>()))> {
    using U = decltype(func(std::declval<T>()));
    auto next = std::make_shared<detail::SharedState<U>>();
    auto prev = state_;

    state_->SetContinuation(
        [prev, next, f = std::forward<F>(func)]() mutable {
          if (prev->HasValue()) {
            try {
              if constexpr (std::is_void_v<U>) {
                f(std::move(prev->GetValue()));
                next->SetValue();
              } else {
                next->SetValue(f(std::move(prev->GetValue())));
              }
            } catch (const std::exception& e) {
              next->SetError(Error(e.what()));
            }
          } else {
            next->SetError(std::move(prev->GetError()));
          }
        },
        exec);

    Future<U> result;
    result.state_ = next;
    return result;
  }

  // OnError: handle errors
  Future<T>& OnError(std::function<void(const Error&)> handler,
                     TaskGroup* exec = nullptr) {
    auto prev = state_;
    state_->SetContinuation(
        [prev, h = std::move(handler)]() {
          if (prev->HasError()) {
            h(prev->GetError());
          }
        },
        exec);
    return *this;
  }

  // Check state
  bool IsReady() const { return state_ && state_->IsReady(); }
  bool HasValue() const { return state_ && state_->HasValue(); }
  bool HasError() const { return state_ && state_->HasError(); }

 private:
  template <typename U>
  friend class Future;
  template <typename U>
  friend class Promise;

  std::shared_ptr<detail::SharedState<T>> state_;
};

template <>
class Future<void> {
 public:
  Future() = default;

  template <typename F>
  auto Then(F&& func, TaskGroup* exec = nullptr) -> Future<decltype(func())> {
    using U = decltype(func());
    auto next = std::make_shared<detail::SharedState<U>>();
    auto prev = state_;

    state_->SetContinuation(
        [prev, next, f = std::forward<F>(func)]() mutable {
          if (prev->HasValue()) {
            try {
              if constexpr (std::is_void_v<U>) {
                f();
                next->SetValue();
              } else {
                next->SetValue(f());
              }
            } catch (const std::exception& e) {
              next->SetError(Error(e.what()));
            }
          } else {
            next->SetError(std::move(prev->GetError()));
          }
        },
        exec);

    Future<U> result;
    result.state_ = next;
    return result;
  }

  Future<void>& OnError(std::function<void(const Error&)> handler,
                        TaskGroup* exec = nullptr) {
    auto prev = state_;
    state_->SetContinuation(
        [prev, h = std::move(handler)]() {
          if (prev->HasError()) {
            h(prev->GetError());
          }
        },
        exec);
    return *this;
  }

  bool IsReady() const { return state_ && state_->IsReady(); }
  bool HasValue() const { return state_ && state_->HasValue(); }
  bool HasError() const { return state_ && state_->HasError(); }

 private:
  template <typename U>
  friend class Future;
  template <typename U>
  friend class Promise;

  std::shared_ptr<detail::SharedState<void>> state_;
};

// Implementation of Promise::GetFuture
template <typename T>
Future<T> Promise<T>::GetFuture() {
  Future<T> f;
  f.state_ = state_;
  return f;
}

inline Future<void> Promise<void>::GetFuture() {
  Future<void> f;
  f.state_ = state_;
  return f;
}

// Helper to create a ready future
template <typename T>
Future<T> MakeReadyFuture(T&& value) {
  Promise<T> p;
  auto f = p.GetFuture();
  p.SetValue(std::forward<T>(value));
  return f;
}

inline Future<void> MakeReadyFuture() {
  Promise<void> p;
  auto f = p.GetFuture();
  p.SetValue();
  return f;
}

template <typename T>
Future<T> MakeErrorFuture(Error err) {
  Promise<T> p;
  auto f = p.GetFuture();
  p.SetError(std::move(err));
  return f;
}

}  // namespace xtils
