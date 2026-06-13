#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>

namespace xtils {

// Abstract clock interface for dependency injection
class IClock {
 public:
  virtual ~IClock() = default;

  // Steady (monotonic) clock — milliseconds since unspecified epoch
  virtual uint64_t SteadyNowMs() const = 0;

  // System (wall) clock — milliseconds since Unix epoch
  virtual uint64_t SystemNowMs() const = 0;
};

// Real clock implementation (default)
class RealClock : public IClock {
 public:
  uint64_t SteadyNowMs() const override {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               now.time_since_epoch())
        .count();
  }

  uint64_t SystemNowMs() const override {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               now.time_since_epoch())
        .count();
  }

  static RealClock* Instance() {
    static RealClock clock;
    return &clock;
  }
};

// Fake clock for testing — time only advances when you call Advance()
class FakeClock : public IClock {
 public:
  FakeClock() : steady_ms_(0), system_ms_(1000000000000ULL) {}  // ~2001

  uint64_t SteadyNowMs() const override {
    std::lock_guard<std::mutex> lock(mu_);
    return steady_ms_;
  }

  uint64_t SystemNowMs() const override {
    std::lock_guard<std::mutex> lock(mu_);
    return system_ms_;
  }

  // Advance both clocks by the given milliseconds
  void Advance(uint64_t ms) {
    std::lock_guard<std::mutex> lock(mu_);
    steady_ms_ += ms;
    system_ms_ += ms;
  }

  // Set absolute values
  void SetSteady(uint64_t ms) {
    std::lock_guard<std::mutex> lock(mu_);
    steady_ms_ = ms;
  }

  void SetSystem(uint64_t ms) {
    std::lock_guard<std::mutex> lock(mu_);
    system_ms_ = ms;
  }

 private:
  mutable std::mutex mu_;
  uint64_t steady_ms_;
  uint64_t system_ms_;
};

}  // namespace xtils
