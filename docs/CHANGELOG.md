# CHANGELOG

All notable changes to xtils are documented here (reverse chronological order).
Format: `type(scope): description` — types: feat, fix, refactor, chore, tidy.

---

## Unreleased

### 2026-06 — Net API Cleanup & Examples

- **refactor(net)!**: move HTTP client public types under `HttpClient` (`HttpClient::Request`, `HttpClient::Response`, `HttpClient::Listener`, `HttpClient::MultipartField`, `HttpClient::MultipartFile`) and rename the generic synchronous request entry point to `HttpClient::Send()`.
- **refactor(net)!**: expose server/router scoped names (`HttpServer::Request`, `HttpServer::Connection`, `HttpRouter::Context`, `HttpRouter::Response`) and remove the `HttpRequest`/`HttpResponse` public header collision between client, server, and router APIs.
- **fix(http)**: make `HttpClient` single-flight start/cancel semantics explicit and atomic enough to avoid resetting an in-flight request before a busy check.
- **tidy(websocket)**: remove `WebSocketClient`'s unused dependency on `HttpClientEventListener`/`http_client.h`; the client owns its HTTP upgrade handshake directly.
- **examples(net)**: add advanced HTTP client/router examples, WebSocket server example, and JSON-RPC IPC example.

### 2026-06 — Review Defects & Documentation

- **fix(http)**: reset per-request CORS state on keep-alive connections and avoid dangling `Origin` string_view
- **fix(http)**: prevent `HttpClient` timeout callbacks from accessing destroyed clients; preserve listener on rejected concurrent async requests
- **fix(http)**: avoid appending chunked transfer final trailers to response body
- **fix(tasks)**: make `TaskGroup::Parallel(size)` honor requested worker count and clarify delayed-task busy semantics
- **fix(tasks)**: make `CronScheduler` test-mode checks deterministic and lock task map during `triggerCheck()`
- **fix(cmake)**: export `xtils-autogen` as an installed target instead of regenerating it in downstream packages
- **docs**: correct Cron examples and expand AI-facing API coverage for Config, Tasks, IPC, Result, Signal, Serialize, Clock, and install behavior

### 2025-06 — JSON Ergonomics & Test Stability

- **feat(json)**: add `Json::object()` and `Json::array()` static factory methods for convenient construction
- **fix(net)**: resolve heap-use-after-free in TcpClient under AddressSanitizer (disconnect race condition)
- **fix(tasks)**: fix flaky `test_cron_scheduler` by deriving test time from internal nextRun
- **feat(tasks)**: add `nextRun` field to `CronScheduler::TaskInfo`

### 2025-06 — Scripting Module (QuickJS-NG)

- **feat(scripting)**: add embedded JavaScript engine via QuickJS-NG v0.15.1
- **feat(scripting)**: `ScriptEngine` — RAII runtime with memory/stack limits and GC control
- **feat(scripting)**: `ScriptContext` — JS eval, file loading, native function registration
- **feat(scripting)**: `ScriptValue` — move-only RAII wrapper with type checks and conversions
- **feat(scripting)**: `ToScriptValue`/`MakeUndefined`/`MakeNull` binding helpers
- **feat(scripting)**: `json_interop.h` — bidirectional `Json` ↔ `ScriptValue` conversion
- **feat(scripting)**: `EvalWithJson` — inject `Json` as global variable and eval code
- **feat(scripting)**: `EvalToJson` — eval JS and return result as `Json`
- **feat(scripting)**: `JsonParseViaJs` — fast JSON parsing via QuickJS (2.5x faster than `Json::parse`)
- **feat(scripting)**: `JsonStringifyViaJs` — stringify with optional pretty-print
- **build(scripting)**: opt-in via `SCRIPTING_ENABLE=ON`, zero impact when disabled
- **build(scripting)**: QuickJS-NG fetched via CMake FetchContent (no submodules needed)

### 2025-05 — Inspect Overhaul

- **refactor(inspect)**: rewrite Impl class — reduce from ~880 to ~330 lines, cleaner architecture
- **feat(inspect)**: add built-in web console with HTTP (GET/POST) and WebSocket panels
- **refactor(inspect)**: extract HTML/CSS/JS to standalone `src/debug/inspect_page.html`
- **build(inspect)**: add `cmake/embed_file.cmake` to embed files as C++ `const char*` at configure time
- **refactor(inspect)**: replace legacy macros with unified `INSPECT`/`INSPECT_WS`/`INSPECT_VAR`
- **feat(inspect)**: web UI renders routes and server info from embedded JSON (JS-driven DOM)
- **refactor(inspect)**: system info collection simplified to compact `SysSnapshot` struct

### 2025-05 — FSM Improvements, Multipart & mbedTLS

- **fix(app)**: `Service::Deinit()` now called before stopping infrastructure — services can safely perform network cleanup (e.g. WebSocket close handshake) in `Deinit()`
- **feat(fsm)**: enrich `HistoryEntry` with human-readable names (`from_name`, `to_name`, `event_name`) + `DumpHistory()` for formatted output
- **fix(fsm)**: resolve thread-safety issues — `recursive_mutex`, `GetHistory()` returns by value, add `SetRecordFailedEvents(bool)`
- **refactor(fsm)**: optimize FSM — `RegisterEvent`/`GetEventName` API, deque-based history, improved `ToDotGraph`, deprecated wrappers moved to `fsm_compat.h`
- **fix(bt)**: Retry decorator now correctly propagates Running status without consuming attempts
- **feat(net)**: add `MultipartParser` for parsing multipart/form-data bodies + lazy `GetMultipartFields()`/`GetMultipartFiles()` on `HttpRequestContext`
- **feat(net)**: add `HttpServerConnection::SendFileStreaming()` for chunked file delivery
- **feat(net)**: add mbedTLS transport backend with `TLS_BACKEND` cmake option (replaces `USE_OPENSSL`/`USE_MBEDTLS` dual options)

### 2025-05 (early) — Code Quality & Type Traits

- **fix(type_traits)**: add `type_name_cstr` for printf-safe usage
- **feat(bt)**: add structured `BtLogger` for offline and online analysis (CompositeLogger, FileLogger, InspectLogger)
- **fix**: resolve review findings from code quality overhaul
- **refactor**: comprehensive code quality overhaul — PascalCase API across all modules, `[[deprecated]]` wrappers for snake_case legacy APIs, test coverage expansion

### 2025-04 — Behavior Tree Events & Subtrees

- **feat(bt)**: load trees from JSON directories (`LoadTreesFromDirectory`)
- **feat(bt)**: add event-driven subtree controls (`SubTree`, `WaitForEvent`, `EventGuard` nodes)
- **feat(bt)**: add tree events system (`sendEvent`, `consumeEvent`, `peekEvent` on BtTree)
- **feat(bt)**: add pause/resume support for BtTree

### 2025-03 — API Naming & HTTP Improvements

- **refactor(app)**: rename `PostTask`/`PostAsyncTask` to `Spawn`/`SpawnAsync`
- **feat(http)**: update HttpClient API (unified sync/async interface)
- **fix(http)**: POST form/multipart with file handling
- **fix(http)**: chunked transfer encoding error
- **chore**: update inner TCP API for Transport abstraction

### 2025-02 — TLS & WebSocket

- **feat(net)**: support WSS (WebSocket Secure) client
- **feat(net)**: HttpClient supports TLS (via Transport layer)
- **tidy**: add backward-compatible API wrappers

### 2025-01 — Behavior Tree Foundation

- **feat(bt)**: add tree name in tree file format
- **feat(json)**: keep one-line for `dump(0)`
- **feat(bt)**: add `BtLogger` interface and implementations
- **feat(bt)**: update tree JSON format
- **feat(http)**: support `multipart/form-data` and large file uploads
- **fix(http)**: HTTP client redirect handling
- **feat(bt)**: update node interface (OnTick/OnStart/OnStop)
- **feat(bt)**: add common-use nodes (Sequence, Selector, Inverter, Delay, AlwaysSuccess, AlwaysFailure, SimpleAction)
- **feat(bt)**: add Blackboard (`AnyMap`)
- **feat(bt)**: use `AnyData` to replace `std::any`
- **feat(bt)**: add `dump()` / `dumpTree()` for tree visualization
- **feat(app)**: update service API
- **feat(bt)**: add input/output ports for nodes
- **feat(bt)**: initial behavior tree implementation with `BtFactory` JSON builder

### 2024 — Foundation

- **feat(tasks)**: add `EventManager` with typed/enum event dispatch
- **feat**: add `BUILD_WITH_SANITIZERS` CMake option (ASan + UBSan)
- **feat(json)**: custom JSON implementation (replaces nlohmann_json dependency)
- **tidy**: export `cxx_std_17` compile feature to consumers
- **feat(tasks)**: update `TaskGroup` API (`Sequential`/`Parallel` factory methods, `RunUntilCompleted`)
- **fix(json)**: `dump()` indent error
- **tidy**: update log formatter
- **feat(logging)**: flush log on exit
- **feat(logging)**: disable log file by default
- **feat(cmake)**: export autogen for downstream projects
- **feat**: disable build examples by default
- **feat(net)**: add WebSocket client
- **fix**: build error for CI
- **feat**: add README
- **tidy**: update Inspect web page
- **chore**: add GitHub Actions CI (ubuntu-latest, Release)
- **feat(tasks)**: add `CronScheduler` (interval + cron-expression tasks)
- **feat(app)**: update App API for better usability
- **feat(net)**: add HttpClient, TcpServer, TcpClient, UdpServer, UdpClient
- **feat(fsm)**: better debugging (history, Graphviz export)
- **feat(debug)**: Tracer uses `forward_list` to reduce memory usage

---

## Breaking Changes Summary

| When | What | Migration |
|------|------|-----------|
| 2025-05 | `USE_OPENSSL`/`USE_MBEDTLS` → `TLS_BACKEND` | Use `TLS_BACKEND=openssl` or `TLS_BACKEND=mbedtls`; legacy `USE_MBEDTLS=ON` still works |
| 2025-05 | `FSM::GetHistory()` returns by value | Update code that held const reference to history |
| 2025-05 | All public APIs renamed to PascalCase | Use new names; old snake_case still works but emits deprecation warnings |
| 2025-03 | `PostTask`/`PostAsyncTask` → `Spawn`/`SpawnAsync` on App | Use new names |
| 2025-01 | BT node interface changed to `OnTick`/`OnStart`/`OnStop` | Override new virtual methods |
| 2024 | Custom JSON replaces nlohmann_json | Use `xtils::Json` API (similar but not identical) |
