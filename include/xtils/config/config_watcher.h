/*
 * Description: inotify-based hot-reload watcher for Config.
 *
 * Usage:
 *   xtils::Config cfg;
 *   cfg.LoadFile("/etc/app.json");
 *   xtils::ConfigWatcher w(&cfg, &task_runner);
 *   w.Watch("/etc/app.json", [](Config& cfg) {
 *     LogI("config reloaded; new log level: %lld",
 *          cfg.GetInt("log.level").value_or(0));
 *   });
 *   // The watcher stops when its destructor runs (RAII).
 */
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "xtils/config/config.h"
#include "xtils/tasks/task_runner.h"

namespace xtils {

class ConfigWatcher {
 public:
  using OnReload = std::function<void(Config&)>;

  ConfigWatcher(Config* config, TaskRunner* task_runner);
  ~ConfigWatcher();

  ConfigWatcher(const ConfigWatcher&) = delete;
  ConfigWatcher& operator=(const ConfigWatcher&) = delete;

  // Begin watching `filename`. On every IN_CLOSE_WRITE / IN_MODIFY event
  // the file is re-loaded into `config` and the callback is invoked on
  // the watcher's TaskRunner thread. Returns true on success, false if
  // inotify_init1 / inotify_add_watch failed.
  //
  // Calling Watch() a second time replaces the previous watch.
  bool Watch(const std::string& filename, OnReload on_reload);

  // Stop watching. Idempotent; also runs in the destructor.
  void Stop();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace xtils
