#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace xtils {

// A subscription handle returned by Signal::Connect().
// Disconnects automatically on destruction (RAII), or manually via Disconnect().
class Subscription {
 public:
  Subscription() = default;
  ~Subscription() { Disconnect(); }

  // Move only
  Subscription(Subscription&& other) noexcept : impl_(std::move(other.impl_)) {}
  Subscription& operator=(Subscription&& other) noexcept {
    if (this != &other) {
      Disconnect();
      impl_ = std::move(other.impl_);
    }
    return *this;
  }
  Subscription(const Subscription&) = delete;
  Subscription& operator=(const Subscription&) = delete;

  void Disconnect() {
    if (impl_) {
      impl_->disconnect();
      impl_.reset();
    }
  }

  bool Connected() const { return impl_ && impl_->connected(); }

  // Prevent automatic disconnect (caller takes manual responsibility)
  void Detach() { impl_.reset(); }

  // IImpl interface for external implementors (e.g. EventManager)
  struct IImpl {
    virtual ~IImpl() = default;
    virtual void disconnect() = 0;
    virtual bool connected() const = 0;
  };

  // Construct from implementation (used by Signal and EventManager)
  explicit Subscription(std::shared_ptr<IImpl> impl) : impl_(std::move(impl)) {}

 private:
  template <typename... Args>
  friend class Signal;
  std::shared_ptr<IImpl> impl_;
};

// A collection of subscriptions that disconnect together.
class ScopedSubscriptions {
 public:
  ScopedSubscriptions() = default;
  ~ScopedSubscriptions() = default;

  ScopedSubscriptions(ScopedSubscriptions&&) = default;
  ScopedSubscriptions& operator=(ScopedSubscriptions&&) = default;
  ScopedSubscriptions(const ScopedSubscriptions&) = delete;
  ScopedSubscriptions& operator=(const ScopedSubscriptions&) = delete;

  void Add(Subscription&& sub) { subs_.push_back(std::move(sub)); }
  Subscription& operator+=(Subscription&& sub) {
    subs_.push_back(std::move(sub));
    return subs_.back();
  }

  void DisconnectAll() { subs_.clear(); }
  size_t Count() const { return subs_.size(); }

 private:
  std::vector<Subscription> subs_;
};

// Signal<Args...> — an object-level signal that can be connected to callbacks.
//
// Usage:
//   class Sensor {
//    public:
//     Signal<float> on_value_changed;
//     void Update(float v) { on_value_changed.Emit(v); }
//   };
//
//   Sensor s;
//   auto sub = s.on_value_changed.Connect([](float v) { printf("%f\n", v); });
//   s.Update(3.14f);   // prints 3.14
//   sub.Disconnect();  // or let sub go out of scope
//   s.Update(2.0f);    // no output
//
template <typename... Args>
class Signal {
 public:
  using Callback = std::function<void(Args...)>;

  Signal() = default;
  ~Signal() = default;

  // Non-copyable, movable
  Signal(const Signal&) = delete;
  Signal& operator=(const Signal&) = delete;
  Signal(Signal&&) = default;
  Signal& operator=(Signal&&) = default;

  // Connect a callback; returns a Subscription handle.
  Subscription Connect(Callback cb) {
    auto slot = std::make_shared<Slot>();
    slot->callback = std::move(cb);
    slot->id = next_id_++;

    std::lock_guard<std::mutex> lock(mutex_);
    slots_.push_back(slot);

    auto weak = std::weak_ptr<Slot>(slot);
    auto self = this;
    auto impl = std::make_shared<SubImpl>(self, slot->id, weak);
    return Subscription(impl);
  }

  // Emit the signal — invoke all connected callbacks.
  void Emit(Args... args) {
    std::vector<std::shared_ptr<Slot>> snapshot;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      snapshot = slots_;
    }
    for (auto& slot : snapshot) {
      if (slot && slot->callback) {
        slot->callback(args...);
      }
    }
  }

  // Call operator for convenience
  void operator()(Args... args) { Emit(std::forward<Args>(args)...); }

  // Number of connected slots
  size_t SlotCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return slots_.size();
  }

  // Disconnect all
  void DisconnectAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    slots_.clear();
  }

 private:
  struct Slot {
    Callback callback;
    uint64_t id = 0;
  };

  void RemoveSlot(uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    slots_.erase(
        std::remove_if(slots_.begin(), slots_.end(),
                       [id](const std::shared_ptr<Slot>& s) {
                         return s && s->id == id;
                       }),
        slots_.end());
  }

  struct SubImpl : Subscription::IImpl {
    Signal* signal;
    uint64_t slot_id;
    std::weak_ptr<Slot> weak_slot;

    SubImpl(Signal* sig, uint64_t id, std::weak_ptr<Slot> w)
        : signal(sig), slot_id(id), weak_slot(std::move(w)) {}

    void disconnect() override {
      if (signal && !weak_slot.expired()) {
        signal->RemoveSlot(slot_id);
      }
    }

    bool connected() const override { return !weak_slot.expired(); }
  };

  mutable std::mutex mutex_;
  std::vector<std::shared_ptr<Slot>> slots_;
  uint64_t next_id_ = 1;
};

}  // namespace xtils
