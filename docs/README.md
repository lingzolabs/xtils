# xtils Documentation

> AI-friendly reference for the xtils C++17 static utility library.

## Quick Navigation

| Document | Description |
|----------|-------------|
| [Architecture](architecture.md) | Project structure, build system, module dependencies |
| [API Reference](api-reference.md) | Complete public API by module |
| [Scripting](scripting.md) | QuickJS scripting module — API, usage, performance |
| [CHANGELOG](CHANGELOG.md) | Version history and breaking changes |

## Project Summary

**xtils** is a C++17 static library providing:

- **App framework** — singleton `App`, `IService` lifecycle, config, events, timers
- **Config** — JSON-backed config with CLI parsing, dot-notation access, type-safe getters
- **FSM** — finite state machine with guards, actions, history, Graphviz export
- **Behavior Tree** — JSON-driven BT with blackboard, ports, subtrees, events
- **Logging** — async/sync logger with console & rotating file sinks, watchdog
- **Networking** — TCP/UDP client & server, HTTP client & server, WebSocket client/server, TLS support, routing, JSON-RPC IPC
- **System** — eventfd, paged memory, signal handler, Unix sockets, platform abstraction
- **Tasks** — TaskRunner, ThreadTaskRunner, UnixTaskRunner, TaskGroup, timers, cron scheduler, events, Future/Promise
- **Utils** — JSON, Result, Signal, Serialize, Clock, string, file, base64, SHA1, byte reader/writer, thread-safe queue, weak pointer, scoped RAII
- **Scripting** — Embedded QuickJS-NG JavaScript engine with C++ binding and Json interop (opt-in)
- **Debug** — Inspect (HTTP/WS debug server), Tracer (Chrome trace format)

## Conventions

- Namespace: `xtils::` (sub-namespaces: `xtils::fsm::`, `xtils::logger::`, `xtils::debug::`)
- API style: **PascalCase** methods (e.g. `AddState()`, `GetString()`)
- Legacy **snake_case** methods exist with `[[deprecated]]` wrappers — use PascalCase
- Known historical exceptions: `CronScheduler` uses lowercase methods (`every()`, `cron()`, `start()`, `stop()`, `cancel()`, `getTaskInfo()`)
- HTTP names are scoped to their owner to avoid collisions: `HttpClient::Request/Response`, `HttpServer::Request`, `HttpRouter::Response`
- Headers: `#pragma once`, path `xtils/<module>/<file>.h`
- Source files: `.cc`; headers: `.h`
