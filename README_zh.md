# xtils

[English](README.md)

> **实用至上的 C++17 工具库 —— 把 Go/Python/Rust 标准库的开发体验带给 C++。**

[![Build](https://github.com/lingzolabs/xtils/actions/workflows/ci.yml/badge.svg)](https://github.com/lingzolabs/xtils/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-orange.svg)](https://en.cppreference.com/w/cpp/17)

10 行代码启动 HTTP 服务，日志自带滚转开箱即用，状态机即插即用 —— 同时保持 C++ 本身的性能优势。

## 为什么选 xtils？

| | xtils | Boost | Abseil | POCO |
|---|---|---|---|---|
| **定位** | 实用优先，开箱即用 | 大而全但笨重 | 底层构建模块 | 框架导向 |
| **接入成本** | 单个静态库，CMake 一行搞定 | 模块系统复杂 | 以 Bazel 为中心 | 重量级框架 |
| **HTTP 服务** | ✅ 内置路由 | ❌ Beast 过于底层 | ❌ 不提供 | ✅ 但 API 陈旧 |
| **日志** | ✅ 异步、滚转、分级 | ❌ Boost.Log 啰嗦 | ✅ 但功能简陋 | ✅ |
| **状态机** | ✅ 带历史记录 + 行为树 | ❌ Boost.MSM 模板地狱 | ❌ 不提供 | ❌ |
| **定时任务** | ✅ Cron 表达式、事件循环 | ❌ | ❌ | ❌ |
| **上手难度** | 低 | 高 | 中 | 中 |

**xtils** 的目标不是极致性能，而是在保持 C++ 性能优势的前提下，提供现代主流语言标准库级别的易用性。

## 快速上手

### HTTP 服务

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
                            HttpResponse& resp) {
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

### 日志

```cpp
#include "xtils/logging/logger.h"
// App::Init() 之后日志即可使用，无需额外配置
LogI("Server started on port %d", 8080);
LogE("Connection failed: %s", error.c_str());
```

### 定时任务

```cpp
xtils::CronScheduler scheduler;
scheduler.AddTask("*/5 * * * * *", []() {  // 每 5 秒执行
  LogI("Heartbeat check");
});
scheduler.Start();
```

### 行为树（JSON 驱动）

用 JSON 定义行为树，注册自定义动作节点，即可运行：

```cpp
// 注册自定义动作节点
xtils::BtFactory factory;
factory.Register<PatrolAction>("Patrol");
factory.Register<AttackAction>("Attack");

// 从 JSON 文件加载行为树
factory.LoadTreeFile("trees/main.json");
auto tree = factory.buildFromRegisteredTree("main");

// 在主循环中 tick（如游戏循环、机器人控制循环）
while (running) {
  tree->tick();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
```

树的 JSON 描述示例：
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

内置节点：Sequence、Selector、Inverter、Repeater、Delay、SubTree、WaitForEvent、EventGuard 等。

### 轻量级 JSON

零依赖的 JSON 解析/序列化，无需引入第三方头文件：

```cpp
#include "xtils/utils/json.h"

// 解析
auto json = xtils::Json::parse(R"({"name": "xtils", "version": 2})");
std::string name = json->get_string("name").value();  // "xtils"
int64_t ver = json->get_integer("version").value();   // 2

// 构建
xtils::Json obj;
obj["status"] = "ok";
obj["items"] = xtils::Json::array_t{1, 2, 3};
std::string output = obj.dump(2);  // 2 空格缩进美化输出
```

## 模块一览

| 模块 | 说明 |
|------|------|
| **App** | 应用生命周期管理、服务编排、配置集成 |
| **Config** | 命令行参数 + JSON 配置文件，支持类型安全访问 |
| **Logging** | 异步日志，控制台 + 文件输出，按大小滚转，看门狗（内存/CPU 守护） |
| **Net / HTTP** | HTTP Server（路由、CORS、WebSocket 升级、文件流），HTTP Client（同步/异步、multipart、重定向、Cookie、SSL） |
| **Net / WebSocket** | WebSocket Client，自动重连，ping/pong 心跳 |
| **Net / TCP & UDP** | TCP Client/Server，UDP Client/Server，组播支持 |
| **Net / TLS** | 支持 OpenSSL 与 mbedTLS 双后端 |
| **FSM** | 有限状态机，支持状态历史记录 |
| **Tasks** | TaskRunner（事件循环）、ThreadTaskRunner、CronScheduler（Cron 表达式）、Timer、TaskGroup、Event |
| **System** | 信号处理、PagedMemory（mmap + guard pages）、Unix Socket、EventFd、平台抽象 |
| **行为树** | JSON 驱动的行为树引擎：Sequence、Selector、Decorator、SubTree、事件系统、黑板通信、JSONL 日志记录 |
| **Utils** | 轻量级 JSON（零依赖解析/序列化）、Base64、SHA1、文件工具、字符串工具、字节读写器、时间工具、线程安全容器、ScopedGuard |

## 构建

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

带测试和示例：

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
cmake --build build
cd build && ctest --output-on-failure
```

详细安装与集成说明见 [INSTALL.md](INSTALL.md)。

### CMake 集成

```cmake
find_package(xtils REQUIRED)
target_link_libraries(your_target PRIVATE xtils::xtils)
```

### 依赖

- C++17 编译器（GCC 7+、Clang 5+、MSVC 2017+）
- CMake ≥ 3.10
- OpenSSL 或 mbedTLS（TLS 功能需要）

## 文档

- [架构设计](docs/architecture.md)
- [API 参考](docs/api-reference.md)
- [更新日志](docs/CHANGELOG.md)

## 许可证

[MIT](LICENSE) © Albert Lv
