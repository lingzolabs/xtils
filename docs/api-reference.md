# API Reference

Complete public API by module. Headers are under `include/xtils/`.

---

## 1. App Framework (`app/`)

### `xtils::App` — Singleton application context

```cpp
#include "xtils/app/app.h"

App* App::Ins();                                          // Get singleton
void Register(std::shared_ptr<IService> service);         // Register service
void Register(std::list<std::shared_ptr<IService>> svcs); // Register multiple
void Init(const std::vector<std::string>& args);          // Initialize with CLI args
void Run();                                               // Block until shutdown
void RunDaemon();                                         // Run in background thread
bool IsRunning();                                         // Check running state

// Task scheduling
void Spawn(Task task);                            // Post task to main thread (blocking)
void SpawnAsync(Task task, Task main = nullptr);  // Post task to worker thread, optional main-thread callback

// Timers
void Every(uint32_t ms, TimerCallback cb);  // Repeating timer
void Delay(uint32_t ms, TimerCallback cb);  // One-shot timer

// Events
void Emit<Event>(const Event& e);
void Connect<Event>(TypedCallback cb);           // Connect by type
void Connect<Event>(Event id, TypedCallback cb); // Connect enum event by id

const Config& Conf();  // Access configuration
```

### `xtils::IService` / `xtils::Service<T>` — Service lifecycle

```cpp
#include "xtils/app/service.h"

class IService {
  explicit IService(const char* name);
  virtual void Init() = 0;   // Called during app init
  virtual void Deinit() = 0; // Called during app shutdown
protected:
  App* ctx;      // App context (set by framework)
  Config config; // Service-specific config
};

template <typename T>
class Service : public IService {
  auto GetWeakPtr();          // Get weak pointer to this service
  void Emit<T>(const T& e);  // Emit event through app context
};
```

### Global Functions

```cpp
void xtils::Init(const std::vector<std::string>& args);
void xtils::Init(int argc, const char* const argv[]);
bool xtils::IsOk();
void xtils::RunForever();
void xtils::RunDaemon();
void xtils::Shutdown();
```

---

## 2. Config (`config/`)

### `xtils::Config`

```cpp
#include "xtils/config/config.h"

// Define options
Config& Define(name, description, default_value, required=false);
Config& Define<T>(name, description, T default_value, required=false);

// Loading
bool ParseArgs(int argc, const char** argv, allow_exit=false);
bool ParseArgs(const std::vector<std::string>& args, allow_exit=false);
bool LoadFile(const std::string& filename);       // JSON config file
bool ParseJson(const Json& json);
bool Parse(const std::string& json_content);

// Access (dot-notation paths: "server.port")
std::optional<T> Get<T>(const std::string& path) const;
std::optional<std::string> GetString(path) const;
std::optional<int64_t> GetInt(path) const;
std::optional<double> GetDouble(path) const;
std::optional<bool> GetBool(path) const;
std::optional<Json> Get(path) const;
bool Has(path) const;

// Mutation
void Set(path, const Json& value);
void Set<T>(path, const T& value);

// Validation & output
bool Validate() const;
std::string Help() const;
std::vector<std::string> MissingRequired() const;
std::string ToString() const;
Json ToJson() const;
bool Save(const std::string& filename) const;
void Print() const;
```

**Supported types**: `string`, `int64_t`, `double`, `bool`, `vector<int64_t>`, `vector<double>`, `vector<string>`, `vector<int>`, `vector<float>`, `Json`.

**CLI parsing**: Supports `--config-file <path>` to load JSON first, then CLI args override.

**Required options**: `Define(..., required=true)` does **not** write the default value into `data_`. The default value is used as the type hint for CLI parsing and for help text; the value must be supplied by JSON, CLI, or `Set()` before `Validate()` passes.

```cpp
Config c;
c.Define("token", "API token", "", true);
CHECK_FALSE(c.Has("token"));
CHECK_FALSE(c.Validate());
c.Set("token", "secret");
CHECK(c.Validate());
```

---

## 3. FSM (`fsm/`)

### `xtils::fsm::FSM`

```cpp
#include "xtils/fsm/fsm.h"

// State management
StateId AddState(std::unique_ptr<State> state);
StateId AddState(const std::string& name);
StateId AddState(name, StateCallback on_enter);
StateId AddState(name, StateCallback on_enter, StateCallback on_exit);

// Transitions
void AddTransition(from_name, to_name, EventType event, condition=nullptr);
void AddTransition(from_id, to_id, EventType event, condition=nullptr);
void AddTransition(from_name, to_name, std::vector<EventType> events, condition=nullptr);

// Control
void Start(const std::string& initial_state);
void Start(StateId initial_state_id);
void Reset(state_name_or_id);
void ProcessEvent(EventType event);

// Queries
bool IsInState(name_or_id) const;
std::optional<std::string> GetCurrentStateName() const;
std::optional<StateId> GetCurrentStateId() const;
std::optional<StateId> GetStateId(name) const;
std::string ToDotGraph() const;  // Graphviz DOT export

// History
std::deque<HistoryEntry> GetHistory() const;
void ClearHistory();
void SetMaxHistorySize(size_t);
void SetRecordFailedEvents(bool record);
std::string DumpHistory() const;

// Event registration
void RegisterEvent(EventType event, const std::string& name);
std::string GetEventName(EventType event) const;

// Thread safety
void EnableThreadSafety(bool enable = true);
```

### `HistoryEntry`

```cpp
struct HistoryEntry {
  std::int64_t timestamp;    // Wall clock (ms since Unix epoch)
  StateId from_state;
  StateId to_state;
  EventType event;
  bool transition_occurred;
  std::string from_name;     // Human-readable state name
  std::string to_name;       // Human-readable state name
  std::string event_name;    // Human-readable event name
  std::string description;
  std::string toString() const;
};
```

### Transition Conditions

```cpp
// Guard: blocks transition if returns false
std::shared_ptr<TransitionCondition> MakeGuard(name, TransitionGuard);
// Action: executes on transition
std::shared_ptr<TransitionCondition> MakeAction(name, TransitionAction);
// Combined
std::shared_ptr<TransitionCondition> MakeCondition(name, guard, action);
```

### `xtils::fsm::State`

```cpp
State(name);
State(name, on_enter);
State(name, on_enter, on_exit);
State(name, on_enter, on_exit, on_update);

const std::string& name() const;
StateId id() const;
virtual void onEnter(EventType);
virtual void onExit(EventType);
virtual void onUpdate(EventType);
```

---

## 4. Behavior Tree (`fsm/behavior_tree.h`)

### Core Types

```cpp
enum class Status { Success, Failure, Running, Idle };
enum class Type { Composite, Action, Decorator };
```

### `xtils::Node` — Base node

```cpp
Status tick();                                   // Execute node
virtual Status OnTick() = 0;                     // Override: main logic
virtual Status OnStart() { return Running; }     // Override: first tick setup
virtual void OnStop() {}                         // Override: cleanup

template<T> std::optional<T> getInput(name);     // Read from port/blackboard
template<T> void setOutput(name, value);          // Write to port/blackboard

static Ports getPorts();    // Override: declare ports
static std::string desc();  // Override: description
```

### Built-in Nodes

| Node | Type | Description |
|------|------|-------------|
| `Sequence` | Composite | Runs children in order, fails on first failure |
| `Selector` | Composite | Runs children in order, succeeds on first success |
| `Inverter` | Decorator | Inverts child result |
| `Delay` | Decorator | Delays child execution by `delay_ms` port |
| `SubTree` | Decorator | Executes registered subtree by `tree_name` port |
| `SimpleAction` | Action | Wraps `std::function<Status()>` |
| `AlwaysSuccess` | Action | Always returns Success |
| `AlwaysFailure` | Action | Always returns Failure |
| `WaitForEvent` | Action | Blocks until event (`event_type` port, `timeout_ms` port) |
| `EventGuard` | Decorator | Interrupts child on event |

### `xtils::BtTree`

```cpp
BtTree(Node::Ptr root, name="", blackboard=nullptr, logger=nullptr);

Status tick();
void reset();
void shutdown();
std::string dump();     // Text dump
Json dumpTree();        // JSON dump

AnyMap& blackboard();   // Access blackboard

// Pause/Resume
void pause();
void resume();
bool isPaused() const;

// Event system
void sendEvent(EventType type, const AnyData& data = {});
std::optional<BtEvent> peekEvent(EventType type) const;
std::optional<BtEvent> consumeEvent(EventType type);
bool hasEvent(EventType type) const;
void clearEvents();
```

### `xtils::BtFactory`

```cpp
BtFactory();

// Register node types
template<T> void Register(const std::string& name);
void RegisterSimpleAction(std::function<Status()> func, name);

// Register subtree templates
void RegisterTree(name, const Json& tree_json);
void LoadTreeFile(const std::string& path);
size_t LoadTreesFromDirectory(const std::string& directory);

// Build trees
BtTree::Ptr buildFromJson(const Json& j, blackboard=nullptr, logger=nullptr);
BtTree::Ptr buildFromRegisteredTree(name, blackboard=nullptr, logger=nullptr);
```

### Ports & Blackboard

```cpp
// Port declaration in custom nodes
static Ports getPorts() {
  return { InputPort<int>("count"), OutputPort<std::string>("result") };
}

// AnyMap (blackboard)
anymap.set<T>(name, value);
std::optional<T> anymap.get<T>(name) const;
bool anymap.has(name) const;
std::vector<std::string> anymap.keys() const;
```

---

## 5. Logging (`logging/`)

### Macros (primary interface)

```cpp
#include "xtils/logging/logger.h"

// Default logger (printf-style format)
LogT(fmt, ...)   // Trace (only when ENABLE_TRACE_LOGGING defined)
LogD(fmt, ...)   // Debug
LogI(fmt, ...)   // Info
LogW(fmt, ...)   // Warning
LogE(fmt, ...)   // Error

// Custom logger instance
TRACE(logger, fmt, ...)
DEBUG(logger, fmt, ...)
INFO(logger, fmt, ...)
WARN(logger, fmt, ...)
ERROR(logger, fmt, ...)

// Assertions
CHECK(expr)       // Assert + abort
DCHECK(expr)      // Debug assert
FATAL(fmt, ...)   // Log + abort
```

Set the log tag per translation unit before including logger.h:
```cpp
#define LOG_TAG_STRING "my_module"
#include "xtils/logging/logger.h"
```

### `xtils::logger::Logger`

```cpp
Logger* DefaultLogger();                    // Global logger
void SetLevel(Logger* logger, log_level);   // Set level

// Logger methods
void SetLevel(log_level level);
log_level Level() const;
void AddSink(std::unique_ptr<Sink> s);
void Flush();
void Shutdown();
size_t GetDroppedCount() const;
```

### Log Levels

`trace` (0) → `debug` (1) → `info` (2) → `warn` (3) → `error` (4)

### Sinks

```cpp
#include "xtils/logging/sink.h"

struct Sink { virtual void write(buf, start, len) = 0; virtual void flush() = 0; };
class ConsoleSink : public Sink;  // stdout
class FileSink : public Sink;     // Rotating file (path, max_bytes, max_items)
```

### Watchdog

```cpp
#include "xtils/logging/watchdog.h"

Watchdog* Watchdog::GetInstance();
void Start();
void SetMemoryLimit(uint64_t bytes, uint32_t window_ms);
void SetCpuLimit(uint32_t percentage, uint32_t window_ms);
Watchdog::Timer CreateFatalTimer(uint32_t ms, WatchdogCrashReason);

// Convenience
void RunTaskWithWatchdogGuard(const std::function<void()>& task);
```

---

## 6. Networking (`net/`)

### TCP Client

```cpp
#include "xtils/net/tcp_client.h"

class TcpClientEventListener {
  virtual void OnConnected(bool success) = 0;
  virtual void OnDataReceived(const void* data, size_t len) = 0;
  virtual void OnDisconnected() = 0;
};

TcpClient(TaskRunner*, TcpClientEventListener*);
bool Connect(address, port);
bool ConnectToHost(hostname, port);  // DNS resolution
void Disconnect();
bool Send(const void* data, size_t len);
bool SendString(const std::string& data);
bool IsConnected() const;
void SetKeepAlive(bool);
void SetNoDelay(bool);
```

### TCP Server

```cpp
#include "xtils/net/tcp_server.h"

class TcpServerEventListener {
  virtual void OnClientConnected(TcpServerConnection* conn) = 0;
  virtual void OnDataReceived(TcpServerConnection* conn, const void* data, size_t len) = 0;
  virtual void OnClientDisconnected(TcpServerConnection* conn) = 0;
};

TcpServer(TaskRunner*, TcpServerEventListener*);
bool Start(address, port);
bool StartDualStack(port);  // IPv4 + IPv6
void Stop();
void Broadcast(data, len);
size_t GetConnectionCount() const;
```

### UDP Client

```cpp
#include "xtils/net/udp_client.h"

UdpClient(TaskRunner*, UdpClientEventListener*);
bool Open(local_address="", local_port=0);
bool SendTo(server_addr, data, len);  // "ip:port"
bool Send(data, len);
void Close();
void SetBroadcast(bool);
bool JoinMulticastGroup(group_addr, interface_addr="");
```

### UDP Server

```cpp
#include "xtils/net/udp_server.h"

UdpServer(TaskRunner*, UdpServerEventListener*);
bool Start(address, port);
bool StartDualStack(port);
bool SendTo(client_addr, data, len);
void Broadcast(data, len);
void SetClientTimeout(uint32_t timeout_ms);
```

### HTTP Client

```cpp
#include "xtils/net/http_client.h"

HttpClient(TaskRunner*);

// Scoped public types avoid collisions with server/router APIs.
using Request = HttpClient::Request;
using Response = HttpClient::Response;
using Listener = HttpClient::Listener;
using MultipartField = HttpClient::MultipartField;
using MultipartFile = HttpClient::MultipartFile;

// Synchronous
HttpClient::Response Send(const HttpClient::Request& request);
HttpClient::Response Get(url);
HttpClient::Response Post(url, body, content_type="");
HttpClient::Response PostJson(url, json);
HttpClient::Response PostForm(url, map<string,string>);
HttpClient::Response PostMultipart(url, fields, files);

// Asynchronous (single-flight: returns false if this client is busy).
// Keep listener alive until OnHttpResponse/OnHttpError is called; IsBusy()
// tracks transport/request state and is not a listener-lifetime barrier.
bool SendAsync(request, listener);
bool GetAsync(url, listener);
bool PostAsync(url, body, content_type, listener);
bool PostJsonAsync(url, json, listener);
bool PostMultipartAsync(url, fields, files, listener);

// Configuration
void SetTimeout(uint32_t timeout_ms);
void SetFollowRedirects(bool, max_redirects=5);
void SetKeepAlive(bool);  // sends the header; no connection pool/reuse yet
void SetVerifySSL(bool);
void SetSSLCertificate(cert_path);
void SetCookie(name, value, domain="");
```

### HTTP Server (low-level)

```cpp
#include "xtils/net/http_server.h"

class HttpRequestHandler {
  virtual void OnHttpRequest(const HttpServer::Request&) = 0;
  virtual void OnWebsocketMessage(const WebsocketMessage&) {}
  virtual void OnHttpConnectionClosed(HttpServer::Connection*) {}
};

HttpServer(TaskRunner*, HttpRequestHandler*);
bool Start(ip, port);
void Stop();
void AddAllowedOrigin(const std::string&);

// Connection methods
conn->SendResponse(http_code, headers, content, force_close);
conn->UpgradeToWebsocket(request);
conn->SendWebsocketMessage(data, len);

// Streaming file response (chunked, memory-efficient)
bool conn->SendFileStreaming(file_path, http_code, headers);
```

### HTTP Router (Express-style)

```cpp
#include "xtils/net/http_router.h"

// Handler-friendly scoped aliases:
//   HttpRouter::Context  == HttpRequestContext
//   HttpRouter::Response == response builder

HttpRouter router;
router.Get("/api/users", handler);
router.Post("/api/users", handler);
router.Put("/api/users/:id", handler);
router.Delete("/api/users/:id", handler);
router.Any("/api/*", handler);

// Middleware
router.Use([](const HttpRequestContext& ctx, HttpRouter::Response& res) -> bool { ... });
router.Use("/api", middleware);

// Static files
router.Static("/static", "/var/www");

// Route groups
auto api = router.Group("/api/v1");
api.Get("/users", handler);

// CORS
router.EnableCors("*", "GET,POST,PUT,DELETE,OPTIONS");

// Handler signature
void handler(const HttpRequestContext& ctx, HttpRouter::Response& res) {
  auto id = ctx.GetParam("id");     // URL parameter
  auto q = ctx.GetQuery("search");  // Query parameter
  auto body = ctx.GetBody();
  res.Status(200).Json("{\"ok\":true}");
}
```

### WebSocket Client

```cpp
#include "xtils/net/websocket_client.h"

class WebSocketClientEventListener {
  virtual void OnWebSocketConnected(WebSocketClient*) = 0;
  virtual void OnWebSocketMessage(WebSocketClient*, const WebSocketMessage&) = 0;
  virtual void OnWebSocketClosed(WebSocketClient*, uint16_t code, reason) = 0;
  virtual void OnWebSocketError(WebSocketClient*, error) = 0;
};

WebSocketClient(TaskRunner*, listener=nullptr);
bool Connect(url);
bool Connect(url, headers);
bool Connect(url, headers, protocols);
bool SendText(text);
bool SendBinary(data, len);
bool SendPing(data="");
void Close(code=1000, reason="");

// Config
void SetAutoReconnect(bool, delay_ms=5000);
void SetPingInterval(uint32_t interval_ms);
void SetMaxMessageSize(size_t);
void SetVerifySSL(bool);
```

### JSON-RPC IPC over Stream Sockets

```cpp
#include "xtils/net/ipc_channel.h"

// Address can be a Unix socket path, abstract Unix socket, TCP IPv4, or TCP IPv6:
//   "/tmp/app.sock", "@app", "127.0.0.1:9000", "[::1]:9000"

// Server side
IpcServer server("/tmp/app.sock");
server.Register("echo", [](const Json& params) -> Result<Json> {
  return params;
});
server.OnNotify("event", [](const Json& params) { /* no response */ });
server.Start();
server.Notify("broadcast", Json::object());
server.Stop();

// Client side
IpcClient client("/tmp/app.sock");
client.Connect();
Result<Json> r = client.Call("echo", Json::object(), 5000);
client.CallAsync("echo", Json::object(), [](Result<Json> r) {});
client.Notify("event", Json::object());
Subscription sub = client.OnNotify("broadcast", [](auto method, auto params) {});
client.Disconnect();
```

Wire format is newline-delimited JSON-RPC 2.0. Requests include `jsonrpc`, `id`, `method`, and optional `params`; notifications omit `id` and do not receive a response. The same API works over filesystem Unix sockets, Linux abstract Unix sockets, TCP IPv4, and TCP IPv6 addresses.

### Multipart Parser

```cpp
#include "xtils/net/http_multipart.h"

struct MultipartFormField {
  std::string name;
  std::string value;
};

struct MultipartFormFile {
  std::string field_name;     // Form field name
  std::string filename;       // Original filename
  std::string content_type;   // MIME type
  std::string content;        // File content (binary-safe)
};

class MultipartParser {
  MultipartParser(std::string_view body, std::string_view boundary);
  bool Parse();
  const std::vector<MultipartFormField>& GetFields() const;
  const std::vector<MultipartFormFile>& GetFiles() const;
  static std::string ExtractBoundary(std::string_view content_type);
};

// Lazy access via HttpRequestContext:
ctx.GetMultipartFields();  // Returns const vector<MultipartFormField>&
ctx.GetMultipartFiles();   // Returns const vector<MultipartFormFile>&
```

### TLS Factory

```cpp
#include "xtils/net/transport/tls_factory.h"

// Backend-agnostic TLS object creation
TlsContextPtr CreateTlsContext(const TlsCertConfig& cfg);
std::unique_ptr<Transport> CreateTlsTransport(TaskRunner* runner, TransportEventListener* listener);
```

### HTTP Common Types

```cpp
#include "xtils/net/http_common.h"

enum class HttpMethod { kGet, kPost, kPut, kDelete, kHead, kOptions, kPatch, kTrace, kConnect, kAny };

struct HttpUrl {
  std::string scheme, host, path, query, fragment;
  uint16_t port;
  bool IsHttps() const;
  bool IsValid() const;
};

namespace HttpStatus { /* OK=200, NOT_FOUND=404, ... */ }
namespace HttpUtils {
  std::string UrlEncode/UrlDecode(str);
  std::string FormDataEncode(map);
  std::string GetMimeType(extension);
  // ... more utilities
}
```

---

## 7. Tasks & Scheduling (`tasks/`)

### `TaskRunner` — Abstract interface

```cpp
#include "xtils/tasks/task_runner.h"

using Task = std::function<void()>;

class TaskRunner {
  virtual void PostTask(std::function<void()>) = 0;
  virtual void PostDelayedTask(std::function<void()>, uint32_t delay_ms) = 0;
  virtual void AddFileDescriptorWatch(PlatformHandle, std::function<void()>) = 0;
  virtual void RemoveFileDescriptorWatch(PlatformHandle) = 0;
  virtual bool RunsTasksOnCurrentThread() const = 0;
};
```

### `UnixTaskRunner` — epoll/poll event loop

```cpp
#include "xtils/tasks/unix_task_runner.h"

UnixTaskRunner();
void Run();   // Block, process events
void Quit();  // Stop the loop
// Implements TaskRunner interface
```

### `ThreadTaskRunner` — Dedicated thread

```cpp
#include "xtils/tasks/thread_task_runner.h"

// Factory methods
static ThreadTaskRunner CreateAndStart(name="");
static std::shared_ptr<ThreadTaskRunner> CreateAndStartShared(name="");

// Access underlying runner
UnixTaskRunner* get() const;
// Implements TaskRunner interface (proxied to internal runner)
```

### `TaskGroup` — Parallel/Sequential execution

```cpp
#include "xtils/tasks/task_group.h"

static std::unique_ptr<TaskGroup> Sequential(runner=nullptr);
static std::unique_ptr<TaskGroup> Parallel(size=hw_concurrency, runner=nullptr);

void PostTask(Task task);               // Blocking queue
void PostAsyncTask(Task task, ms=0);    // Async with optional delay

template<F> auto RunUntilCompleted(F task);  // Block until future completes

bool IsBusy();
int Size();
void Stop();
bool StopWaitAll(timeout=5s);
std::shared_ptr<TaskRunner> MainRunner();
```

Semantics:
- `Parallel(size)` creates exactly `size` worker threads; `size <= 0` falls back to hardware concurrency.
- `PostAsyncTask(task, 0)` counts as busy until the worker task finishes.
- `PostAsyncTask(task, delay_ms)` is scheduled on the main runner and becomes busy only when it is queued to workers after the delay. This avoids treating future timers as current work.
- `Stop()` / `StopWaitAll()` prevent not-yet-due delayed worker tasks from being queued later.
- `StopWaitAll()` waits for currently queued/running worker tasks, then stops worker threads. It does not wait for arbitrary `PostTask()` work on `MainRunner()`.

### Timer

```cpp
#include "xtils/tasks/timer.h"

// SteadyTimer (monotonic clock, best for relative timers)
SteadyTimer(TaskGroup*);
TimerId SetRelativeTimer(delay_ms, callback, type=OneShot);
TimerId SetAbsoluteTimer(SteadyTimePoint, callback, type=OneShot);
TimerId SetRepeatingTimer(interval_ms, callback);
bool CancelTimer(TimerId);
void CancelAllTimers();
static uint64_t GetCurrentTimestampMs();

// SystemTimer (wall clock, best for UTC/absolute times)
SystemTimer(TaskGroup*);
TimerId SetAbsoluteUtcTimer(utc_timestamp_ms, callback, type=OneShot);
static uint64_t GetCurrentUtcTimestampMs();

// Aliases
using MonotonicTimer = SteadyTimer;
using UtcTimer = SystemTimer;
```

### CronScheduler

```cpp
#include "xtils/tasks/cron_scheduler.h"

CronScheduler(tzOffsetMinutes=0, testMode=false);

// Interval task
TaskID every(Seconds interval, std::function<void()> fn);

// Cron-style task (set<int> for each field, empty = wildcard)
TaskID cron(seconds, minutes, hours, days, months, weekdays, fn);

void start();
void stop();
bool cancel(TaskID);
std::optional<TaskInfo> getTaskInfo(TaskID);
void triggerCheck(TimePoint now);  // testMode only
// TaskInfo fields: id, type, active, schedule, lastRun, nextRun
```

Usage notes:
- `every()` schedules fixed intervals from creation time.
- `cron()` accepts one `std::set<int>` per field: seconds, minutes, hours, days, months, weekdays; an empty set means wildcard.
- In `testMode=true`, no worker thread is started; call `triggerCheck(now)` with a synthetic `TimePoint` to deterministically execute due jobs.
- Public method names are lowercase for historical compatibility.

### EventManager

```cpp
#include "xtils/tasks/event.h"

EventManager(std::shared_ptr<TaskGroup> tg);

// By type (struct events)
void Connect<T>(TypedCallback<T> cb);
void Emit<T>(const T& e);

// By enum (enum events)
void Connect<T>(T id, TypedCallback<T> cb);
void Emit<T>(const T& e);

void Stop();
```

### Future / Promise

```cpp
#include "xtils/tasks/future.h"

Promise<int> p;
Future<int> f = p.GetFuture();
Future<int> next = f.Then([](int v) { return v + 1; });
f.OnError([](const Error& e) {});
p.SetValue(42);

Promise<void> pv;
Future<void> fv = pv.GetFuture();
pv.SetError(Error("failed"));
```

`Future<T>` is chainable rather than blocking: use `Then()` for value continuations, `OnError()` for error handling, and `IsReady()`/`HasValue()`/`HasError()` for state checks. Continuations can run inline or through an optional `TaskGroup` executor.

---

## 8. Debug (`debug/`)

### Inspect — HTTP/WebSocket debug server

```cpp
#include "xtils/debug/inspect.h"

Inspect& Inspect::Get();  // Singleton
void Init(ip="127.0.0.1", port=8080);
void Stop();
bool IsRunning() const;

// Routes
void Route(path, handler);
void Route(path, description, handler);
void WebSocket(path, handler);
void WebSocket(path, description, handler);
void Static(path, content, content_type="text/html");
void Unregister(path);

// WebSocket publish
size_t Publish(url, message, is_text=true);
size_t Publish(url, const Json&);
bool HasSubscribers(url) const;

// Response helpers
static Response Json(json);
static Response Text(text);
static Response Html(html);
static Response Error(message);
static Response Success(message="OK");
```

#### Macros (disabled when `INSPECT_DISABLE` defined)

```cpp
// Register HTTP route — use `req` and `resp` in body
INSPECT(path, desc, {
  resp = Inspect::Json(myJson);
});

// Register WebSocket route — req.body = received message
INSPECT_WS(path, desc, {
  resp = Inspect::Text(req.body);
});

// Expose a variable as JSON {"value": expr}
INSPECT_VAR(path, expr);

// Register static content
INSPECT_STATIC(path, content, content_type);

// Publish to WebSocket subscribers
INSPECT_PUBLISH(url, message);
INSPECT_PUBLISH_BIN(url, binary_data);
```

#### Web Console

The built-in index page (`/`) provides:
- **Left sidebar**: clickable route list (click HTTP routes to send GET, click WS routes to fill path)
- **Info bar**: process uptime, RSS, threads, fds, load average, memory, time
- **HTTP panel**: GET/POST with JSON pretty-printing
- **WebSocket panel**: connect/disconnect, color-coded message log (recv=green, send=blue, sys=gray, err=red)

The HTML source is maintained separately in `src/debug/inspect_page.html` and embedded at CMake configure time via `cmake/embed_file.cmake`.

### Tracer — Chrome trace format

```cpp
#include "xtils/debug/tracer.h"

// Macros (active only when ENABLE_TRACE_RECORDING defined)
TRACE_SCOPE(name)     // RAII scope event
TRACE_INSTANT(name)   // Instant event
TRACE_DATA(p_str)     // Get trace data as string*
TRACE_SAVE(filename)  // Save to file (Chrome trace JSON)
```

---

## 9. System (`system/`)

### EventFd

```cpp
#include "xtils/system/event_fd.h"
// Linux eventfd wrapper for thread signaling
```

### PagedMemory

```cpp
#include "xtils/system/paged_memory.h"
// Page-aligned memory allocation (used by HTTP server receive buffers)
```

### SignalHandler

```cpp
#include "xtils/system/signal_handler.h"
// POSIX signal handler registration
```

### UnixSocket

```cpp
#include "xtils/system/unix_socket.h"
// Low-level socket abstraction (TCP/UDP, IPv4/IPv6, listening/connecting)
```

### Platform

```cpp
#include "xtils/system/platform.h"
// Platform typedefs: PlatformHandle, PlatformThreadId, ThreadID, SocketHandle, SockFamily, TimeMillis
```

---

## 10. Utilities (`utils/`)

### JSON

```cpp
#include "xtils/utils/json.h"

// Construction
Json();                    // null
Json(nullptr);             // null
Json(bool);                // boolean
Json(int/int64_t/...);    // integer
Json(double);              // float
Json(string/const char*);  // string
Json(array_t);             // array
Json(object_t);            // object

// Static factory methods
static Json object();      // empty object {}
static Json array();       // empty array []

// Type checks
is_null(), is_bool(), is_integer(), is_float(), is_number(), is_string(), is_array(), is_object()

// Value access (throws on type mismatch)
as_bool(), as_integer(), as_float(), as_number(), as_string(), as_array(), as_object()
as<T>()  // Template access

// Subscript
json["key"]       // Object access (creates if non-const)
json[index]       // Array access
json.push_back(v) // Append to array

// Safe access
std::optional<Json> get(key_or_index) const;
get_bool(key), get_integer(key), get_string(key), ...
has_key(key), contains(key), has_index(index)

// Serialization
std::string dump(indent=0) const;                      // To string
static Json parse(text, error_code&);                  // From string
static std::optional<Json> parse(text);                // From string (optional)

size_t size() const;
bool empty() const;
void clear();
void erase(key_or_index);
```

### Result

```cpp
#include "xtils/utils/result.h"

Result<int> ParseInt(std::string_view s) {
  if (s.empty()) return Err("empty input");
  return Ok(42);
}

auto r = ParseInt("42");
if (r) {
  int value = *r;
} else {
  LogE("%s", r.error().message.c_str());
}

Result<void> ok = Ok();
Result<Json> bad = Err(1001, "bad params");
```

### Signal

```cpp
#include "xtils/utils/signal.h"

Signal<int> changed;
Subscription sub = changed.Connect([](int v) {});
changed.Emit(1);
sub.Disconnect();

ScopedSubscriptions subs;
subs += changed.Connect([](int v) {});
subs.DisconnectAll();
```

`Subscription` is move-only and disconnects automatically on destruction unless `Detach()` is called.

### Serialize

```cpp
#include "xtils/utils/serialize.h"

struct User {
  int id = 0;
  std::string name;
  XTILS_SERIALIZABLE(User, id, name)
};

User u{1, "alice"};
Json j = u.ToJson();
auto parsed = User::FromJson(j);
```

Also provides `xtils::serialize::to_json()` / `from_json()` overloads for primitive types, strings, vectors, `Json`, and custom types with `ToJson()` / `FromJson()`.

### Clock

```cpp
#include "xtils/utils/clock.h"

IClock* clock = RealClock::Instance();
uint64_t now_ms = clock->SteadyNowMs();

FakeClock fake;
fake.Advance(1000);
fake.SetSystem(1700000000000ULL);
```

`IClock` is a small dependency-injection interface for code that needs testable time. `RealClock` uses `std::chrono`; `FakeClock` advances only when explicitly told. Current `Timer` and `CronScheduler` APIs still use their existing clock/timepoint types directly.

### String Utils

```cpp
#include "xtils/utils/string_utils.h"

// Conversion
std::optional<uint32_t> StringToUInt32(str, base=10);
std::optional<int64_t> StringToInt64(str, base=10);
std::optional<double> StringToDouble(str);
// Also: CStringToUInt32, CStringToInt32, CStringToInt64, CStringToUInt64, CStringToDouble
// Also: StringViewToInt32, StringViewToUInt64, etc.

// String operations
bool StartsWith(str, prefix);
bool EndsWith(str, suffix);
bool Contains(haystack, needle);
bool CaseInsensitiveEqual(a, b);
std::string Join(parts, delim);
std::vector<std::string> SplitString(text, delimiter);
std::string StripPrefix(str, prefix);
std::string StripSuffix(str, suffix);
std::string TrimWhitespace(str);
std::string ToLower(str);
std::string ToUpper(str);
std::string ToHex(data, size);
std::string ReplaceAll(str, to_replace, replacement);

// Low-level
void StringCopy(dst, src, dst_size);  // Safe strlcpy
size_t SprintfTrunc(dst, dst_size, fmt, ...);
StackString<N>(fmt, ...);  // Stack-allocated formatted string
```

### File Utils

```cpp
#include "xtils/utils/file_utils.h"
// Namespace: file_utils::

bool readable/writeable/exists/is_file/is_directory(path);
bool read(path, out_string);
bool read(path, out_string, max_size);
bool read_lines(path, out_vec);
bool write(path, content);
bool append(path, content);
bool mkdir(path);
std::vector<std::string> list_directory/list_files/list_directories(path);
bool copy/move/rename/remove/remove_all(src, dst);
size_t file_size(path);
std::string dirname/bsname/extension/stem/join_path/absolute_path/canonical_path(path);
std::string current_path();
bool change_directory(path);
```

### Thread-Safe Queue

```cpp
#include "xtils/utils/thread_safe.h"

ThreadSafe<std::list<T>> queue;
queue.Push(value);
bool queue.PopWait(value, timeout=max);  // Blocking
bool queue.TryPop(value);               // Non-blocking
std::size_t queue.Size();
void queue.Clear();
void queue.Quit();  // Unblock all waiters
```

### WeakPtr

```cpp
#include "xtils/utils/weak_ptr.h"

// Single-threaded weak pointer (invalidates on factory destruction)
WeakPtrFactory<MyClass> factory_(this);
WeakPtr<MyClass> ptr = factory_.GetWeakPtr();
ptr.get();       // Returns nullptr after owner destroyed
explicit operator bool();
```

### Scoped RAII

```cpp
#include "xtils/utils/scoped.h"

ScopedFile fd(open(...));    // Auto-closes fd
ScopedFstream fp(fopen(...));
ScopedDir dir(opendir(...));
Scoped defer([] { cleanup(); });  // Generic deferred cleanup
```

### Other Utils

| Header | Description |
|--------|-------------|
| `base64.h` | Base64 encode/decode |
| `sha1.h` | SHA1 hash |
| `byte_reader.h` | Binary data reader (endian-aware) |
| `byte_writer.h` | Binary data writer (endian-aware) |
| `endianness.h` | Byte order detection & conversion |
| `time_utils.h` | `steady::Now()`, `system::GetCurrentUtcMs()`, `common::TimeDiffMs()`, etc. |
| `type_traits.h` | Compile-time `type_name<T>()` |
| `exception.h` | Exception utilities |
| `string_view.h` | string_view helpers |
