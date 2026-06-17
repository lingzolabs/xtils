#pragma once

#include <sys/types.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "xtils/config/config.h"
#include "xtils/tasks/event.h"
#include "xtils/tasks/task_group.h"
#include "xtils/tasks/timer.h"

namespace xtils {

class IService;
class App {
 public:
  // Constructible directly for testing. Use Ins() for the global instance.
  App();
  ~App();
  static App* Ins();

  void Register(std::list<std::shared_ptr<IService>> services);
  void Register(std::shared_ptr<IService> p);

 public:
  // until shutdown
  void Run();
  // run in backgroud
  void RunDaemon();
  void Init(const std::vector<std::string>& args);
  bool IsRunning();

  // Post a task to run on the main thread (synchronously, in order).
  // WARNING: Long-running tasks will block other Spawn() tasks, SpawnAsync()
  // callbacks, timers, and event handlers. Use SpawnAsync() for I/O or
  // CPU-intensive work.
  void Spawn(Task task);

  // Post a task to run on a worker thread (asynchronously, in parallel).
  // The optional `main` callback runs on the main thread after `task`
  // completes. Use this for I/O operations, network calls, or CPU-intensive
  // work.
  void SpawnAsync(Task task, Task main = nullptr);

  void Every(uint32_t ms, TimerCallback cb);

  void Delay(uint32_t ms, TimerCallback cb);

  // event
  template <typename Event>
  void Emit(const Event& e) {
    em_->Emit<Event>(e);
  }

  template <typename Event, typename TypedCallback>
  Subscription Connect(Event id, TypedCallback cb) {
    return em_->Connect<Event>(id, cb);
  }

  template <typename Event, typename TypedCallback>
  Subscription Connect(TypedCallback cb) {
    return em_->Connect<Event>(cb);
  }

  const Config& Conf() { return config_; }

  // For testing: returns service_ topologically ordered by Dependencies().
  // Aborts the process on cycles or unknown dependencies. Public so
  // dependency-graph behaviour can be unit-tested without spinning up the
  // full Run() loop.
  std::list<std::shared_ptr<IService>> TopoSortServices();

 private:
  void deinit();

  void parse_args(const std::vector<std::string>& args,
                  bool allow_exit = false);

  void pre_run();

  // init
  void default_config();
  void init_log();
  void init_inspect();
  void print_banner();


 private:
  Config config_;
  std::unique_ptr<EventManager> em_;
  std::shared_ptr<TaskGroup> async_tg_;
  std::unique_ptr<SteadyTimer> timer_;
  std::list<std::shared_ptr<IService>> service_;
  std::atomic<bool> running_{false};
  bool initialized_ = false;
  std::vector<std::string> args_;
  std::thread main_;

  uint32_t major_;
  uint32_t minor_;
  uint32_t patch_;
  std::string build_time_;
};

}  // namespace xtils
