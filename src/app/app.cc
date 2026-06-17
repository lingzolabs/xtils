#include "xtils/app/app.h"

#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "xtils/app/auto-gen.h"
#include "xtils/app/service.h"
#include "xtils/debug/inspect.h"
#include "xtils/debug/tracer.h"
#include "xtils/logging/logger.h"
#include "xtils/logging/sink.h"
#include "xtils/system/signal_handler.h"
#include "xtils/tasks/event.h"
#include "xtils/tasks/task_group.h"
#include "xtils/tasks/timer.h"
#include "xtils/utils/file_utils.h"
#include "xtils/utils/json.h"
#include "xtils/utils/string_utils.h"
#include "xtils/utils/time_utils.h"

namespace xtils {

App::App() { default_config(); }
App::~App() {
  if (main_.joinable()) {
    main_.join();
  }
}

App *App::Ins() {
  static App app;
  return &app;
}

void App::default_config() {
  config_.Define("xtils.threads", "threds numbers", 4)
      .Define("xtils.inspect.enable", "enable inspect or not", true)
      .Define("xtils.inspect.port", "inspect prot", 9090)
      .Define("xtils.inspect.addr", "inspect address", "0.0.0.0")
      .Define("xtils.inspect.cors", "inspect cross addr", "*")
      .Define("xtils.log.file.name", "log file name,default in current dir",
              "./log/app.log")
      .Define("xtils.log.file.max_bytes", "log file size, default 4M",
              4 * 1024 * 1024)
      .Define("xtils.log.file.max_items", "max file number, app.max.log", 5)
      .Define("xtils.log.file.enable", "log to file or not", false)
      .Define("xtils.log.level",
              "log level: 0 trace, 1 debug, 2 info, 3 warn, 4 error", 1)
      .Define("xtils.log.console.enable", "log to console or not", true);
}
void App::parse_args(const std::vector<std::string> &args, bool allow_exit) {
  if (!config_.ParseArgs(args, allow_exit)) {
    std::cerr << "Failed to parse command line arguments" << std::endl;
    std::cerr << config_.Help() << std::endl;
    exit(1);
  }

  // Validate configuration
  if (!config_.Validate()) {
    std::cerr << "Configuration validation failed:" << std::endl;
    auto missing = config_.MissingRequired();
    for (const auto &key : missing) {
      std::cerr << "  Missing required parameter: " << key << std::endl;
    }
    std::cerr << config_.Help() << std::endl;
    exit(1);
  }
}
void App::Init(const std::vector<std::string> &args) {
  TRACE_SCOPE("App:init");
  // Setup signal handlers for graceful shutdown
  system::SignalHandler::Initialize();

  args_ = args;  // cache command line arguments
  // Parse command line arguments and load configuration
  parse_args(args, false);  // don't exit on help

  init_log();

  // init thread pool
  int threads_size = Conf().GetOr<int>("xtils.threads");
  if (threads_size < 1) {
    LogE("xtils.threads must be >= 1, got %d", threads_size);
    std::cerr << "xtils.threads must be >= 1, got " << threads_size
              << std::endl;
    exit(1);
  }
  async_tg_ = std::make_unique<TaskGroup>(threads_size);
  // init event manager
  em_ = std::make_unique<EventManager>(
      TaskGroup::Sequential(async_tg_->MainRunner()));
  timer_ = std::make_unique<SteadyTimer>(async_tg_.get());
  initialized_ = true;
}

void App::init_log() {
  TRACE_SCOPE("App:init_log");
  if (Conf().GetOr<bool>("xtils.log.console.enable")) {
    logger::DefaultLogger()->AddSink(std::make_unique<logger::ConsoleSink>(),
                                      std::make_unique<logger::ColorFormatter>());
  }
  if (Conf().GetOr<bool>("xtils.log.file.enable")) {
    std::string file = Conf().GetOr<std::string>("xtils.log.file.name");
    int max_bytes = Conf().GetOr<int>("xtils.log.file.max_bytes");
    int max_items = Conf().GetOr<int>("xtils.log.file.max_items");
    if (!file_utils::exists(file)) {
      bool create_dir = file_utils::mkdir(file_utils::dirname(file));
      if (!create_dir) {
        LogE("can't open log file, %s", file.c_str());
      } else {
        logger::DefaultLogger()->AddSink(
            std::make_unique<logger::FileSink>(file, max_bytes, max_items));
      }
    } else {
      logger::DefaultLogger()->AddSink(
          std::make_unique<logger::FileSink>(file, max_bytes, max_items));
    }
  }
  int log_level = Conf().GetOr<int>("xtils.log.level");
  XTILS_CHECK(log_level < logger::max);
  logger::SetLevel(logger::DefaultLogger(), (logger::log_level)log_level);
}

void App::init_inspect() {
#ifndef INSPECT_DISABLE
  TRACE_SCOPE("App:init_inspect");
  if (Conf().GetOr<bool>("xtils.inspect.enable")) {
    std::string addr = Conf().GetOr<std::string>("xtils.inspect.addr");
    int port = Conf().GetOr<int>("xtils.inspect.port");
    std::string cors = Conf().GetOr<std::string>("xtils.inspect.cors");

    Inspect::Get().Init(addr, port);
    Inspect::Get().SetCORS(cors);
    LogI("inspect url http://%s:%d, cors: %s", addr.c_str(), port,
         cors.c_str());
  }

  if (Conf().GetOr<bool>("xtils.inspect.enable")) {
    INSPECT("/api/config", "config in process", {
      resp = Inspect::Json(config_.ToJson());
    });
    INSPECT("/api/tracer", "get tracer info", {
      std::string tracer;
      TRACE_DATA(&tracer);
      resp = Inspect::Text(tracer);
    });
    INSPECT("/api/version", "get version info", {
      Json version;
      version["major"] = major_;
      version["minor"] = minor_;
      version["patch"] = patch_;
      version["build_time"] = build_time_;
      resp = Inspect::Json(version);
    });
  }
#endif
}

void App::pre_run() {
  // all config
  for (const auto &s : service_) {
    for (const auto &e : s->config.Options()) {
      const auto &opt = e.second;
      config_.Define(s->name + "." + opt.name, opt.description,
                     opt.default_value, opt.required);
    }
  }

  parse_args(args_, true);  // again with sub services

  // sub config
  for (auto &s : service_) {
    auto sub = config_.Get(s->name);
    if (sub) {
      s->config.ParseJson(*sub);
    }
  }

  print_banner();
  init_inspect();

  // Topologically sort services by Dependencies(). Falls back to
  // registration order when no service declares any dependency.
  std::list<std::shared_ptr<IService>> ordered = TopoSortServices();
  service_ = std::move(ordered);

  for (auto &p : service_) {
    p->ctx = this;
    p->Init();
    LogI("Init %s service successed!!", p->name.c_str());
  }
}

void App::RunDaemon() {
  if (running_) {
    LogW("App is already running");
    return;
  }
  main_ = std::thread(std::bind(&App::Run, this));
  // Block until the daemon thread has finished bringing services up.
  // RunUntilCompleted posts a single task to the worker pool and waits.
  async_tg_->RunUntilCompleted([]() { return true; });
}

void App::Run() {
  XTILS_CHECK(initialized_);
  if (running_) {
    LogW("App is already running");
    return;
  }
  running_ = true;
  // process service
  pre_run();
  LogI("App starting main run loop...");

  // Health monitoring: post a heartbeat task to the main runner every
  // kHeartbeatMs and observe the actual delivery interval. If it drifts
  // significantly beyond the schedule, the main runner is blocked.
  constexpr uint32_t kHeartbeatMs = 1000;
  constexpr int64_t kSlowThresholdMs = 2000;
  constexpr int64_t kBlockedThresholdMs = 5000;
  auto last_beat = std::make_shared<std::atomic<int64_t>>(
      steady::GetCurrentMs());
  timer_->SetRepeatingTimer(kHeartbeatMs, [this, last_beat]() {
    Spawn([last_beat, this]() {
      int64_t now = steady::GetCurrentMs();
      int64_t prev = last_beat->exchange(now, std::memory_order_relaxed);
      int64_t delta = now - prev;
      if (delta > kBlockedThresholdMs) {
        LogW("main runner blocked: heartbeat delayed %lldms",
             static_cast<long long>(delta));
      } else if (delta > kSlowThresholdMs) {
        LogW("main runner slow: heartbeat delayed %lldms",
             static_cast<long long>(delta));
      }
      if (async_tg_->IsBusy()) {
        LogD("task group busy, workers=%d", async_tg_->Size());
      }
    });
  });

  // Idle wait until shutdown is requested. Sleep is fine here because the
  // actual work runs on the main runner thread and the worker pool.
  while (IsOk()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  LogI("App shutting down...");
  deinit();
  LogI("Exit main");
  logger::DefaultLogger()->Shutdown();  // flush all log
  running_ = false;
}

void App::deinit() {
  TRACE_SCOPE("App::deinit");
#ifndef INSPECT_DISABLE
  if (Conf().GetOr<bool>("xtils.inspect.enable")) {
    Inspect::Get().Stop();
  }
#endif

  // Deinit services FIRST while infrastructure is still running,
  // so services can still use event loop / thread pool for cleanup
  // (e.g. WebSocket close handshake, flush pending I/O).
  // Reverse topological order: leaves before their dependencies.
  for (auto it = service_.rbegin(); it != service_.rend(); ++it) {
    (*it)->Deinit();
  }

  async_tg_->Stop();  // stop task group
  em_->Stop();        // stop event manager
  em_.reset();
  timer_.reset();
  async_tg_.reset();
}

void App::Spawn(Task task) {
  async_tg_->MainRunner()->PostTask(std::move(task));
}

void App::SpawnAsync(Task task, Task main) {
  auto main_runner = async_tg_->MainRunner();
  async_tg_->PostAsyncTask(
      [task = std::move(task), main = std::move(main), main_runner]() {
        {
          TRACE_SCOPE("AsyncTask");
          task();
        }
        if (main) {
          main_runner->PostTask(std::move(main));
        }
      });
}

void App::Every(uint32_t ms, TimerCallback cb) {
  timer_->SetRepeatingTimer(ms, cb);
}

void App::Delay(uint32_t ms, TimerCallback cb) {
  timer_->SetRelativeTimer(ms, cb, TimerType::kOneShot);
}

void App::print_banner() {
  build_time_ = XTILS_BUILD_TIME;
  app_version(major_, minor_, patch_);
  const std::string fmt = R"(
================ XTILS =================
  Version : v%d.%d.%d
  Build   : %s
  Service : %s
========================================
)";
  std::stringstream ss;
  for (auto &s : service_) {
    ss << s->name << " ";
  }
  std::string names = ss.str();
  StackString<1024> banner(fmt.c_str(), major_, minor_, patch_,
                           build_time_.c_str(),
                           names.empty() ? "None" : names.c_str());
  logger::DefaultLogger()->WriteRaw(banner.c_str());
}

void App::Register(std::list<std::shared_ptr<IService>> services) {
  XTILS_CHECK(!running_);
  for (auto &p : services) {
    Register(p);
  }
}

void App::Register(std::shared_ptr<IService> p) {
  XTILS_CHECK(!running_);
  service_.push_back(p);
}

bool App::IsRunning() { return running_; }

// Topologically sort services_ by IService::Dependencies(). Uses Kahn's
// algorithm: start with services that have no in-edges and peel off.
// On a cycle, logs the remaining services and exit(1). Unknown deps log
// + exit(1) too. When no service declares any dependency this returns
// the original registration order untouched.
std::list<std::shared_ptr<IService>> App::TopoSortServices() {
  // Map name -> service ptr; name conflicts are reported then resolved
  // first-wins to keep the algorithm stable.
  std::map<std::string, std::shared_ptr<IService>> by_name;
  for (const auto &s : service_) {
    if (by_name.count(s->name)) {
      LogE("App: duplicate service name '%s' — dependency resolution may "
           "misbehave",
           s->name.c_str());
      continue;
    }
    by_name[s->name] = s;
  }

  // Validate dependencies and check whether any service declares one.
  bool any_dep = false;
  for (const auto &s : service_) {
    for (const auto &dep : s->Dependencies()) {
      any_dep = true;
      if (!by_name.count(dep)) {
        LogE("App: service '%s' depends on unknown service '%s'",
             s->name.c_str(), dep.c_str());
        std::cerr << "App: service '" << s->name
                  << "' depends on unknown service '" << dep << "'"
                  << std::endl;
        exit(1);
      }
    }
  }
  if (!any_dep) return service_;  // preserve registration order

  // Kahn's algorithm.
  std::map<std::string, int> indegree;
  std::map<std::string, std::vector<std::string>> reverse_deps;
  for (const auto &s : service_) indegree[s->name] = 0;
  for (const auto &s : service_) {
    for (const auto &dep : s->Dependencies()) {
      indegree[s->name]++;
      reverse_deps[dep].push_back(s->name);
    }
  }

  std::list<std::shared_ptr<IService>> ordered;
  std::list<std::string> ready;
  // Iterate in registration order so the result is stable across runs.
  for (const auto &s : service_) {
    if (indegree[s->name] == 0) ready.push_back(s->name);
  }
  while (!ready.empty()) {
    std::string name = ready.front();
    ready.pop_front();
    ordered.push_back(by_name[name]);
    for (const auto &child : reverse_deps[name]) {
      if (--indegree[child] == 0) ready.push_back(child);
    }
  }

  if (ordered.size() != service_.size()) {
    LogE("App: dependency cycle detected. Services involved:");
    std::cerr << "App: dependency cycle detected. Services involved: ";
    bool first = true;
    for (const auto &kv : indegree) {
      if (kv.second > 0) {
        if (!first) std::cerr << ", ";
        std::cerr << kv.first;
        first = false;
      }
    }
    std::cerr << std::endl;
    exit(1);
  }
  return ordered;
}
}  // namespace xtils
