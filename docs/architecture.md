# Architecture

> **Platform:** Linux only. The implementation uses pthreads, `__NR_gettid`,
> `<poll.h>`, `<dirent.h>`, abstract Unix sockets, and other Linux-specific
> APIs. Windows is not supported. macOS is currently untested.

## Directory Layout

```
xtils/
├── include/xtils/          # Public headers
│   ├── app/                # App framework (app.h, service.h, auto-gen.h)
│   ├── config/             # Configuration (config.h, config_watcher.h)
│   ├── debug/              # Debug tools (inspect.h, tracer.h)
│   ├── fsm/                # State machines (fsm.h, behavior_tree.h, bt_*logger.h)
│   ├── logging/            # Logging (logger.h, log_builder.h, mdc.h, sink.h, watchdog.h)
│   ├── metrics/            # Metrics (metrics.h — Counter/Gauge/Histogram + Prometheus exporter)
│   ├── net/                # Networking
│   │   ├── transport/      # Transport layer (transport.h, tls_transport.h, mbedtls_transport.h, tls_factory.h, plain_tcp_transport.h)
│   │   ├── http_client.h   # HTTP client (sync & async; HttpClient::Request/Response)
│   │   ├── http_server.h   # HTTP server (low-level; HttpServer::Request/Connection)
│   │   ├── http_router.h   # HTTP router (Express-style; HttpRouter::Response)
│   │   ├── http_multipart.h # Multipart form-data parser
│   │   ├── http_common.h   # HTTP types (method, url, headers, status codes)
│   │   ├── tcp_client.h / tcp_server.h
│   │   ├── udp_client.h / udp_server.h
│   │   ├── websocket_client.h
│   │   ├── websocket_common.h
│   │   └── ipc_channel.h       # JSON-RPC 2.0 over stream sockets
│   ├── scripting/          # Embedded JS engine (opt-in, QuickJS-NG)
│   │   ├── engine.h        # ScriptEngine — runtime management
│   │   ├── context.h       # ScriptContext — eval, function registration
│   │   ├── value.h         # ScriptValue — RAII value wrapper
│   │   ├── binding.h       # C++ → JS value helpers (free functions)
│   │   ├── class_binding.h # ClassBinding<T> — register C++ classes into JS
│   │   └── json_interop.h  # Json ↔ ScriptValue conversion
│   ├── system/             # OS abstractions (event_fd, paged_memory, platform, signal_handler, unix_socket)
│   ├── tasks/              # Async & scheduling
│   │   ├── task_runner.h          # Abstract TaskRunner interface
│   │   ├── unix_task_runner.h     # epoll/poll-based event loop
│   │   ├── thread_task_runner.h   # TaskRunner on a dedicated thread
│   │   ├── task_group.h           # Sequential/parallel task groups
│   │   ├── timer.h                # Steady & system clock timers
│   │   ├── cron_scheduler.h       # Cron-style job scheduler
│   │   ├── event.h                # Typed event manager
│   │   └── future.h               # Future/Promise with continuations
│   └── utils/              # General utilities
│       ├── json.h           # Custom JSON implementation
│       ├── result.h         # Result<T>/Error expected-style return values
│       ├── signal.h         # Object-level signals and RAII subscriptions
│       ├── serialize.h      # Json serialization helpers/macros
│       ├── clock.h          # IClock, RealClock, FakeClock
│       ├── string_utils.h   # String operations
│       ├── file_utils.h     # File I/O & path operations
│       ├── base64.h / sha1.h
│       ├── byte_reader.h / byte_writer.h
│       ├── thread_safe.h    # Thread-safe queue
│       ├── weak_ptr.h       # Single-threaded WeakPtr
│       ├── scoped.h         # RAII wrappers (ScopedFile, ScopedDir, Scoped)
│       ├── time_utils.h     # Time utilities (steady/system clock)
│       ├── type_traits.h    # Compile-time type name
│       ├── endianness.h     # Byte order conversion
│       ├── crypto.h         # SHA256, HMAC-SHA1/SHA256, secure RNG, UUID v4
│       └── exception.h      # Exception utilities
├── src/                    # Implementation (.cc files, mirrors include layout)
├── tests/                  # Unit tests (*_test.cc, uses doctest)
├── examples/               # Usage examples
├── cmake/                  # CMake helpers (autogen, embed_file, config template)
├── CMakeLists.txt          # Root build file
└── docs/                   # This documentation
```

## Module Dependency Graph

```
utils (json, string, file, time, thread_safe, weak_ptr, scoped, ...)
  ↑
system (event_fd, paged_memory, signal_handler, unix_socket, platform)
  ↑
tasks (task_runner, unix_task_runner, thread_task_runner, task_group, timer, event, cron_scheduler)
  ↑
config (config.h, config_watcher.h — depends on json, tasks)
  ↑
logging (logger, log_builder, mdc, sink, watchdog)
  ↑
metrics (metrics, registry, Prometheus exporter — depends on utils)
  ↑
net (tcp, udp, http, websocket, ipc — depends on tasks, system, utils)
  ↑
fsm (fsm, behavior_tree — depends on json, type_traits)
  ↑
debug (inspect — depends on net, tasks; tracer — standalone)
  ↑
scripting (opt-in, depends on utils/json; links QuickJS-NG)
  ↑
app (app, service — orchestrates all modules)
```

## Networking API Ownership

HTTP public types are intentionally scoped to their owning API to prevent header
collisions and make examples unambiguous:

- `HttpClient::Request`, `HttpClient::Response`, `HttpClient::Listener`
- `HttpServer::Request`, `HttpServer::Connection`, `HttpServer::Handler`
- `HttpRouter::Context`, `HttpRouter::Response`

`HttpClient` is single-flight: one client instance owns one in-progress request.
Use multiple `HttpClient` instances for parallel requests.

## Build System

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_TESTS` | OFF | Build unit tests |
| `BUILD_EXAMPLES` | OFF | Build examples |
| `BUILD_WITH_SANITIZERS` | OFF | Enable ASan + UBSan |
| `TLS_BACKEND` | openssl | TLS backend: `openssl` or `mbedtls` |
| `INSPECT_DISABLE` | OFF | Disable inspect module (strips all INSPECT_* macros) |
| `SCRIPTING_ENABLE` | OFF | Enable QuickJS-NG scripting module (fetched via FetchContent) |

### Build Commands

```bash
# Debug with tests & examples
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
cmake --build build

# Run tests
cd build && ctest --output-on-failure

# Release
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Networking Examples

When `BUILD_EXAMPLES=ON`, the networking examples cover the main public surface:

- `tcp_example`, `udp_example`, `udp_multicast_example`
- `http_client`, `http_client_advanced`
- `http_server`, `http_router_advanced`
- `websocket_client`, `websocket_server`
- `ipc_channel`

### Linking

```cmake
find_package(xtils REQUIRED)
target_link_libraries(myapp xtils::xtils)
```

The library exports `cxx_std_17` as a public compile feature — consumers automatically get C++17. The generated version-symbol archive (`xtils-autogen`) is exported as part of `xtils::xtils`, so C++-only downstream projects do not need to include xtils CMake helper scripts.

## HTTP and transport execution

HTTP request lifecycle uses `xtils::fsm::FSM`:
`Idle → Connecting → Active → Finishing → Idle`. Failure, cancellation and
timeout enter the same Finishing state. Network mutations stay on one runner;
nested lifecycle events are queued, and cleanup/user callbacks run after the
transport and parser stacks unwind. Sending is independent from parsing while
Active, so an early rejection can stop an unfinished upload.

`StreamWriter` owns ordered memory/file sources, retries unchanged chunks after
backpressure, and yields after a 256 KiB production budget. Memory sources retain
their caller-provided content; file sources buffer only a chunk. Socket/TLS
queues remain capped at 8 MiB. `TrySend` accepts at most 16 KiB atomically and
returns Accepted/WouldBlock/Closed/Error. Accepted means queued, not peer
delivery. WouldBlock arms a deferred, one-shot capacity callback.

HTTP servers pause readable interest until their response source is consumed,
then resume buffered pipelined requests. Custom networking TaskRunners need both
`SetFileDescriptorWatchReadable` and `SetFileDescriptorWatchWritable`.
Runner teardown drops queued resource owners while watch containers are valid.

The response parser, not disconnect alone, decides completion: fixed-length
bodies require all bytes, chunked bodies require the terminal chunk and complete
trailers, and only close-delimited bodies finish at clean EOF. HTTPS additionally
requires a TLS close_notify for close-delimited completion.

## TLS Backend

The library requires exactly one TLS backend, selected via `TLS_BACKEND`:

- **OpenSSL** (default, `TLS_BACKEND=openssl`): links `OpenSSL::SSL`
- **mbedTLS 3.x** (`TLS_BACKEND=mbedtls`, verified with 3.6.7): links `MbedTLS::mbedtls`, `MbedTLS::mbedx509`, `MbedTLS::mbedcrypto`. Use `MbedTLS_DIR` to select a separate 3.x installation when the system supplies 4.x; the 4.x PSA API is not supported by this adapter.

Compile definition `USE_OPENSSL` or `USE_MBEDTLS` is propagated to consumers.

Backend-agnostic code should use the factory in `net/transport/tls_factory.h`:
```cpp
#include "xtils/net/transport/tls_factory.h"
auto ctx = CreateTlsContext(cfg);
auto transport = CreateTlsTransport(runner, listener);
```

## Code Conventions

- Google C++ style (`.clang-format`)
- 2-space indent, 80-column limit
- `#pragma once` for header guards
- PascalCase for public API methods
- `[[deprecated("Use Xxx() instead")]]` on legacy snake_case wrappers
