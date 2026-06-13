# xtils

[中文](README_zh.md)

> **A practical-first C++17 utility library — the standard library experience that C++ never gave you.**

[![Build](https://github.com/lingzolabs/xtils/actions/workflows/ci.yml/badge.svg)](https://github.com/lingzolabs/xtils/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-orange.svg)](https://en.cppreference.com/w/cpp/17)

Start an HTTP server in 10 lines. Logging with built-in rotation out of the box. A plug-and-play finite state machine. All with the performance C++ inherently provides.

## Why xtils?

| | xtils | Boost | Abseil | POCO |
|---|---|---|---|---|
| **Philosophy** | Practical-first, batteries-included | Comprehensive but heavy | Low-level building blocks | Framework-oriented |
| **Setup** | Single static lib, CMake one-liner | Complex module system | Bazel-centric | Heavyweight framework |
| **HTTP Server** | ✅ Built-in with router | ❌ Beast is low-level | ❌ Not provided | ✅ But dated API |
| **Logging** | ✅ Async, rotation, levels | ❌ Boost.Log is verbose | ✅ But minimal | ✅ |
| **FSM** | ✅ With history & behavior trees | ❌ Boost.MSM is template-heavy | ❌ Not provided | ❌ |
| **Cron/Tasks** | ✅ Cron expressions, event loop | ❌ | ❌ | ❌ |
| **Learning curve** | Low | High | Medium | Medium |

**xtils** bridges the gap between C++'s raw performance and the developer experience of Go, Python, or Rust standard libraries.

## Quick Start

### HTTP Server

```cpp
#include "xtils/net/http_server.h"
#include "xtils/net/http_router.h"
#include "xtils/tasks/thread_task_runner.h"
#include "xtils/system/signal_handler.h"

using namespace xtils;

int main() {
  system::SignalHandler::Initialize();
  auto task_runner = ThreadTaskRunner::CreateAndStart("http");

  auto router = std::make_unique<HttpRouter>();
  router->Get("/hello", [](const HttpRequestContext& ctx,
                            HttpRouter::Response& resp) {
    resp.Json(R"({"message": "Hello, World!"})");
  });

  auto handler = std::make_unique<RouterHttpRequestHandler>(std::move(router));
  HttpServer server(&task_runner, handler.get());
  server.Start("0.0.0.0", 8080);

  while (!system::SignalHandler::IsShutdownRequested()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
}
```

### Logging

```cpp
#include "xtils/logging/logger.h"
// That's it. Logging is ready after App::Init().
LogI("Server started on port %d", 8080);
LogE("Connection failed: %s", error.c_str());
```

### Cron Task

```cpp
#include "xtils/tasks/cron_scheduler.h"

xtils::CronScheduler scheduler;
scheduler.every(xtils::CronScheduler::Seconds(5), []() {
  LogI("Heartbeat check");
});
scheduler.start();
```

### Behavior Tree (JSON-driven)

Define behavior trees in JSON, register custom actions, and run:

```cpp
// Register custom action nodes
xtils::BtFactory factory;
factory.Register<PatrolAction>("Patrol");
factory.Register<AttackAction>("Attack");

// Load tree from JSON file
factory.LoadTreeFile("trees/main.json");
auto tree = factory.buildFromRegisteredTree("main");

// Tick the tree (e.g. in a game loop)
while (running) {
  tree->tick();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
```

Tree JSON example:
```json
{
  "name": "main",
  "root": {
    "name": "Selector",
    "children": [
      { "name": "Patrol" },
      { "name": "SubTree", "ports": { "tree_name": "recovery" } }
    ]
  }
}
```

Built-in nodes: Sequence, Selector, Inverter, Repeater, Delay, SubTree, WaitForEvent, EventGuard, and more.

### Lightweight JSON

Zero-dependency JSON parser/serializer, no third-party headers needed:

```cpp
#include "xtils/utils/json.h"

// Parse
auto json = xtils::Json::parse(R"({"name": "xtils", "version": 2})");
std::string name = json->get_string("name").value();  // "xtils"
int64_t ver = json->get_integer("version").value();   // 2

// Build
auto obj = xtils::Json::object();
obj["status"] = "ok";
obj["items"] = xtils::Json::array_t{1, 2, 3};
obj["meta"] = xtils::Json::object();  // nested empty object
std::string output = obj.dump(2);  // Pretty-print with 2-space indent
```

## Modules

| Module | Description |
|--------|-------------|
| **App** | Application lifecycle, service management, config integration |
| **Config** | Command-line args + JSON config file with typed access |
| **Logging** | Async logging, console + file sinks, size-based rotation, watchdog (memory/CPU guards) |
| **Net / HTTP** | HTTP Server (router, CORS, WebSocket upgrade, file streaming), HTTP Client (sync/async, multipart, redirect, cookies, SSL) |
| **Net / WebSocket** | WebSocket Client with auto-reconnect and ping/pong |
| **Net / TCP & UDP** | TCP Client/Server, UDP Client/Server with multicast support |
| **Net / IPC** | Unix domain socket IPC channel with length-prefixed framing |
| **Net / TLS** | OpenSSL and mbedTLS backends |
| **FSM** | Finite State Machine with history tracking |
| **Tasks** | TaskRunner (event loop), ThreadTaskRunner, CronScheduler, Timer, TaskGroup, Event |
| **System** | Signal handling, PagedMemory (mmap with guard pages), Unix sockets, EventFd, platform abstractions |
| **Behavior Tree** | JSON-driven behavior tree engine: Sequence, Selector, Decorator, SubTree, event system, blackboard, JSONL logging |
| **Debug** | Inspect (HTTP/WebSocket debug server with route registration), Tracer (Chrome `chrome://tracing` format) |
| **Scripting** | Embedded QuickJS-NG JavaScript engine with C++ binding and JSON interop (opt-in via `SCRIPTING_ENABLE`) |
| **Utils** | Lightweight JSON (zero-dep parser/serializer), Base64, SHA1, file utils, string utils, byte reader/writer, time utils, thread-safe wrappers, scoped guard |

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

With tests and examples:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
cmake --build build
cd build && ctest --output-on-failure
```

See [INSTALL.md](INSTALL.md) for detailed installation and integration instructions.

### CMake Integration

```cmake
find_package(xtils REQUIRED)
target_link_libraries(your_target PRIVATE xtils::xtils)
```

### Requirements

- C++17 compiler (GCC 7+, Clang 5+, MSVC 2017+)
- CMake ≥ 3.10
- OpenSSL or mbedTLS (for TLS features)

## Documentation

- [Architecture & Design](docs/architecture.md)
- [API Reference](docs/api-reference.md)
- [Changelog](docs/CHANGELOG.md)

## License

[MIT](LICENSE) © Albert Lv
