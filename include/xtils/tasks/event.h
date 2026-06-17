/*
 * Description: Type-safe event system with subscription management
 *
 * Copyright (c) 2018 - 2024 Albert Lv <altair.albert@gmail.com>
 *
 * Licensed under the MIT License. See LICENSE file in the project root for full
 * license information.
 *
 * Author: Albert Lv <altair.albert@gmail.com>
 * Version: 2.0.0
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <typeindex>
#include <unordered_map>

#include "xtils/tasks/task_group.h"
#include "xtils/utils/signal.h"
#include "xtils/utils/type_traits.h"

namespace xtils {

using EventId = std::uint32_t;

struct IEvent {
  virtual std::string_view name() = 0;
};

template <typename T>
struct Event : public IEvent {
  std::string_view name() override { return xtils::type_name<T>(); }
};

using OnEvent = std::function<void(const void*)>;

class EventManager {
 public:
  // Create an EventManager with a borrowed executor. Lifecycle of `tg`
  // remains the caller's responsibility — Stop() will NOT shut it down.
  explicit EventManager(std::shared_ptr<TaskGroup> tg)
      : tg_(std::move(tg)), owns_executor_(false) {}

  // Convenience constructor: own a private sequential TaskGroup that is
  // shut down together with this EventManager.
  EventManager() : tg_(TaskGroup::Sequential()), owns_executor_(true) {}

  ~EventManager() { Stop(); }

  template <typename EventT>
  using TypedCallback = std::function<void(const EventT&)>;

  // Connect with subscription handle (new API)
  template <typename T, std::enable_if_t<!std::is_enum_v<T>, int> = 0>
  Subscription Connect(TypedCallback<T> cb) {
    std::type_index type = std::type_index(typeid(T));
    auto slot = std::make_shared<SlotEntry>();
    slot->id = next_id_++;
    slot->callback = [cb](const void* e) { cb(*static_cast<const T*>(e)); };

    std::lock_guard<std::mutex> lock(map_mutex_);
    maps_[type].push_back(slot);

    auto weak = std::weak_ptr<SlotEntry>(slot);
    auto impl = std::make_shared<EventSubImpl>(this, type, slot->id, weak);
    return MakeSubscription(std::move(impl));
  }

  template <typename T, std::enable_if_t<std::is_enum_v<T>, int> = 0>
  Subscription Connect(T id, TypedCallback<T> cb) {
    std::uint32_t uid = static_cast<std::uint32_t>(id);
    auto slot = std::make_shared<SlotEntry>();
    slot->id = next_id_++;
    slot->callback = [cb](const void* e) { cb(*static_cast<const T*>(e)); };

    std::lock_guard<std::mutex> lock(enum_map_mutex_);
    enum_maps_[uid].push_back(slot);

    auto weak = std::weak_ptr<SlotEntry>(slot);
    auto impl =
        std::make_shared<EnumEventSubImpl>(this, uid, slot->id, weak);
    return MakeSubscription(std::move(impl));
  }

  template <typename T, std::enable_if_t<!std::is_enum_v<T>, int> = 0>
  void Emit(const T& e) {
    if (stop_) return;
    std::type_index type = std::type_index(typeid(T));
    std::vector<OnEvent> cbs;
    {
      std::lock_guard<std::mutex> lock(map_mutex_);
      auto it = maps_.find(type);
      if (it != maps_.end()) {
        for (auto& slot : it->second) {
          if (slot) cbs.push_back(slot->callback);
        }
      }
    }
    for (auto& cb : cbs) {
      tg_->PostAsyncTask([e, cb] { cb(&e); });
    }
  }

  template <typename T, std::enable_if_t<std::is_enum_v<T>, int> = 0>
  void Emit(const T& e) {
    if (stop_) return;
    std::uint32_t uid = static_cast<std::uint32_t>(e);
    std::vector<OnEvent> cbs;
    {
      std::lock_guard<std::mutex> lock(enum_map_mutex_);
      auto it = enum_maps_.find(uid);
      if (it != enum_maps_.end()) {
        for (auto& slot : it->second) {
          if (slot) cbs.push_back(slot->callback);
        }
      }
    }
    for (auto& cb : cbs) {
      tg_->PostAsyncTask([e, cb] { cb(&e); });
    }
  }

  // Disconnect all subscribers and prevent further dispatch.
  // Does NOT shut down a borrowed executor — that is the caller's
  // responsibility. If this EventManager owns a private executor
  // (default-constructed), the executor is stopped here.
  void Stop() {
    if (stop_.exchange(true)) return;
    {
      std::lock_guard<std::mutex> lock(map_mutex_);
      maps_.clear();
    }
    {
      std::lock_guard<std::mutex> lock(enum_map_mutex_);
      enum_maps_.clear();
    }
    if (owns_executor_ && tg_) {
      tg_->Stop();
    }
  }

 private:
  struct SlotEntry {
    uint64_t id = 0;
    OnEvent callback;
  };

  using SlotList = std::list<std::shared_ptr<SlotEntry>>;

  void RemoveTypedSlot(std::type_index type, uint64_t id) {
    std::lock_guard<std::mutex> lock(map_mutex_);
    auto it = maps_.find(type);
    if (it != maps_.end()) {
      it->second.remove_if(
          [id](const std::shared_ptr<SlotEntry>& s) { return s && s->id == id; });
    }
  }

  void RemoveEnumSlot(uint32_t uid, uint64_t id) {
    std::lock_guard<std::mutex> lock(enum_map_mutex_);
    auto it = enum_maps_.find(uid);
    if (it != enum_maps_.end()) {
      it->second.remove_if(
          [id](const std::shared_ptr<SlotEntry>& s) { return s && s->id == id; });
    }
  }

  struct EventSubImpl : Subscription::IImpl {
    EventManager* mgr;
    std::type_index type;
    uint64_t slot_id;
    std::weak_ptr<SlotEntry> weak;

    EventSubImpl(EventManager* m, std::type_index t, uint64_t id,
                 std::weak_ptr<SlotEntry> w)
        : mgr(m), type(t), slot_id(id), weak(std::move(w)) {}

    void disconnect() override {
      if (mgr && !weak.expired()) mgr->RemoveTypedSlot(type, slot_id);
    }
    bool connected() const override { return !weak.expired(); }
  };

  struct EnumEventSubImpl : Subscription::IImpl {
    EventManager* mgr;
    uint32_t uid;
    uint64_t slot_id;
    std::weak_ptr<SlotEntry> weak;

    EnumEventSubImpl(EventManager* m, uint32_t u, uint64_t id,
                     std::weak_ptr<SlotEntry> w)
        : mgr(m), uid(u), slot_id(id), weak(std::move(w)) {}

    void disconnect() override {
      if (mgr && !weak.expired()) mgr->RemoveEnumSlot(uid, slot_id);
    }
    bool connected() const override { return !weak.expired(); }
  };

  static Subscription MakeSubscription(std::shared_ptr<Subscription::IImpl> impl) {
    return Subscription(std::move(impl));
  }

 private:
  std::atomic_bool stop_{false};
  std::mutex map_mutex_;
  std::unordered_map<std::type_index, SlotList> maps_;
  std::mutex enum_map_mutex_;
  std::map<std::uint32_t, SlotList> enum_maps_;
  std::shared_ptr<TaskGroup> tg_;
  bool owns_executor_;
  std::atomic<uint64_t> next_id_{1};
};
}  // namespace xtils
