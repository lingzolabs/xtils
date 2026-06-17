#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <cstdlib>
#include <cstdio>
#include <atomic>
#include <chrono>
#include <fstream>
#include <string>
#include <thread>

#include "xtils/config/config.h"
#include "xtils/config/config_watcher.h"
#include "xtils/tasks/thread_task_runner.h"

using namespace xtils;
using namespace std::chrono_literals;

// ─── LoadEnv ─────────────────────────────────────────────────────────────

TEST_CASE("Config::LoadEnv: prefixed env imports keys") {
  setenv("XTILS_TEST_NUM", "42", 1);
  setenv("XTILS_TEST_NAME", "alice", 1);
  setenv("XTILS_TEST_FLAG", "true", 1);
  setenv("OTHER_VAR", "ignored", 1);

  Config c;
  size_t n = c.LoadEnv("XTILS");
  CHECK(n >= 3);

  CHECK(c.GetInt("xtils.test.num").value_or(-1) == 42);
  CHECK(c.GetString("xtils.test.name").value_or("") == "alice");
  CHECK(c.GetBool("xtils.test.flag").value_or(false) == true);
  CHECK_FALSE(c.Has("other.var"));

  unsetenv("XTILS_TEST_NUM");
  unsetenv("XTILS_TEST_NAME");
  unsetenv("XTILS_TEST_FLAG");
  unsetenv("OTHER_VAR");
}

TEST_CASE("Config::LoadEnv: type coercion (int / double / bool / string)") {
  setenv("APP_INT", "100", 1);
  setenv("APP_DOUBLE", "3.14", 1);
  setenv("APP_BOOL", "yes", 1);
  setenv("APP_STR", "hello world", 1);

  Config c;
  c.LoadEnv("APP");
  CHECK(c.GetInt("app.int").value_or(-1) == 100);
  CHECK(c.GetDouble("app.double").value_or(-1.0) == 3.14);
  CHECK(c.GetBool("app.bool").value_or(false) == true);
  CHECK(c.GetString("app.str").value_or("") == "hello world");

  unsetenv("APP_INT");
  unsetenv("APP_DOUBLE");
  unsetenv("APP_BOOL");
  unsetenv("APP_STR");
}

// ─── ConfigWatcher ───────────────────────────────────────────────────────

TEST_CASE("ConfigWatcher: file modification triggers reload") {
  // Write initial JSON.
  char tmpl[] = "/tmp/xtils_cfg_watch_XXXXXX";
  int fd = mkstemp(tmpl);
  REQUIRE(fd >= 0);
  ::close(fd);
  std::string path = tmpl;

  {
    std::ofstream f(path);
    f << R"({"app": {"v": 1}})";
  }

  Config cfg;
  REQUIRE(cfg.LoadFile(path));
  CHECK(cfg.GetInt("app.v").value_or(0) == 1);

  auto runner = ThreadTaskRunner::CreateAndStart("cfg-watch");
  ConfigWatcher watcher(&cfg, &runner);
  std::atomic<int> reloaded{0};
  REQUIRE(watcher.Watch(path, [&](Config& c) {
    reloaded += static_cast<int>(c.GetInt("app.v").value_or(0));
  }));

  // Modify the file.
  std::this_thread::sleep_for(50ms);
  {
    std::ofstream f(path);
    f << R"({"app": {"v": 7}})";
  }

  // Wait for inotify event to be delivered.
  for (int i = 0; i < 100 && reloaded == 0; ++i) {
    std::this_thread::sleep_for(20ms);
  }
  CHECK(reloaded.load() >= 1);
  CHECK(cfg.GetInt("app.v").value_or(0) == 7);

  watcher.Stop();
  std::remove(path.c_str());
}

TEST_CASE("ConfigWatcher: Stop is idempotent and clean") {
  Config cfg;
  auto runner = ThreadTaskRunner::CreateAndStart("cfg-watch-stop");
  ConfigWatcher w(&cfg, &runner);
  w.Stop();  // never started; should not crash
  w.Stop();  // double stop
}
