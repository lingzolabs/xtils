#pragma once

#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace xtils {

// A lightweight error type for Result
struct Error {
  int code = 0;
  std::string message;

  Error() = default;
  explicit Error(const std::string& msg) : code(-1), message(msg) {}
  Error(int c, const std::string& msg) : code(c), message(msg) {}

  explicit operator bool() const { return code != 0; }
};

// Result<T, E> — a type-safe union of success value or error.
// Inspired by Rust's Result and C++23 std::expected.
//
// Usage:
//   Result<int> parse(const std::string& s) {
//     int val;
//     if (try_parse(s, val)) return val;
//     return Err("parse failed");
//   }
//
//   auto r = parse("123");
//   if (r) { use(*r); }
//   else { LogE("%s", r.error().message.c_str()); }
//
template <typename T, typename E = Error>
class Result {
 public:
  // Success constructors
  Result(const T& value) : data_(value) {}
  Result(T&& value) : data_(std::move(value)) {}

  // Error constructors — use tag type to disambiguate
  struct ErrorTag {};
  Result(ErrorTag, const E& err) : data_(err) {}
  Result(ErrorTag, E&& err) : data_(std::move(err)) {}

  // Copy/move
  Result(const Result&) = default;
  Result(Result&&) = default;
  Result& operator=(const Result&) = default;
  Result& operator=(Result&&) = default;

  // Check success
  bool ok() const { return data_.index() == 0; }
  explicit operator bool() const { return ok(); }

  // Access value (undefined behavior if !ok())
  T& value() & { return std::get<0>(data_); }
  const T& value() const& { return std::get<0>(data_); }
  T&& value() && { return std::get<0>(std::move(data_)); }

  T& operator*() & { return value(); }
  const T& operator*() const& { return value(); }
  T&& operator*() && { return std::move(*this).value(); }

  T* operator->() { return &value(); }
  const T* operator->() const { return &value(); }

  // Access error (undefined behavior if ok())
  E& error() & { return std::get<1>(data_); }
  const E& error() const& { return std::get<1>(data_); }
  E&& error() && { return std::get<1>(std::move(data_)); }

  // Get value or fallback
  T value_or(const T& fallback) const& { return ok() ? value() : fallback; }
  T value_or(T&& fallback) && {
    return ok() ? std::move(*this).value() : std::move(fallback);
  }

  // Monadic operations
  template <typename F>
  auto map(F&& f) const -> Result<decltype(f(value())), E> {
    using U = decltype(f(value()));
    if (ok()) return Result<U, E>(f(value()));
    return Result<U, E>(typename Result<U, E>::ErrorTag{}, error());
  }

  template <typename F>
  auto and_then(F&& f) const -> decltype(f(value())) {
    if (ok()) return f(value());
    using RetType = decltype(f(value()));
    return RetType(typename RetType::ErrorTag{}, error());
  }

 private:
  std::variant<T, E> data_;
};

// Specialization for void success type
template <typename E>
class Result<void, E> {
 public:
  Result() : err_(), has_error_(false) {}

  struct ErrorTag {};
  Result(ErrorTag, const E& err) : err_(err), has_error_(true) {}
  Result(ErrorTag, E&& err) : err_(std::move(err)), has_error_(true) {}

  Result(const Result&) = default;
  Result(Result&&) = default;
  Result& operator=(const Result&) = default;
  Result& operator=(Result&&) = default;

  bool ok() const { return !has_error_; }
  explicit operator bool() const { return ok(); }

  E& error() & { return err_; }
  const E& error() const& { return err_; }

 private:
  E err_;
  bool has_error_;
};

// Helper to create error results
template <typename E = Error>
struct ErrWrapper {
  E err;
  explicit ErrWrapper(E e) : err(std::move(e)) {}

  template <typename T>
  operator Result<T, E>() const {
    return Result<T, E>(typename Result<T, E>::ErrorTag{}, err);
  }
};

// Convenience function to create errors
inline ErrWrapper<Error> Err(const std::string& msg) {
  return ErrWrapper<Error>(Error(msg));
}

inline ErrWrapper<Error> Err(const char* msg) {
  return ErrWrapper<Error>(Error(std::string(msg)));
}

inline ErrWrapper<Error> Err(int code, const std::string& msg) {
  return ErrWrapper<Error>(Error(code, msg));
}

template <typename E,
          std::enable_if_t<!std::is_convertible_v<E, std::string> &&
                               !std::is_same_v<std::decay_t<E>, Error>,
                           int> = 0>
ErrWrapper<std::decay_t<E>> Err(E&& e) {
  return ErrWrapper<std::decay_t<E>>(std::forward<E>(e));
}

// Convenience function to create success results
template <typename T>
Result<std::decay_t<T>> Ok(T&& value) {
  return Result<std::decay_t<T>>(std::forward<T>(value));
}

inline Result<void> Ok() { return Result<void>(); }

}  // namespace xtils
