#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "xtils/system/unix_socket.h"
#include "xtils/utils/json.h"
#include "xtils/utils/result.h"
#include "xtils/utils/signal.h"

namespace xtils {

class TaskGroup;

// JSON-RPC 2.0 error codes
namespace jsonrpc {
constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
constexpr int kInternalError = -32603;
// Server-defined errors: -32000 to -32099
}  // namespace jsonrpc

// JSON-RPC 2.0 compliant IPC over stream sockets.
//
// Address format follows UnixSocketRaw/GetSockFamily:
//   /path/to/socket : filesystem Unix domain socket
//   @abstract_name  : abstract Unix domain socket
//   127.0.0.1:9000  : TCP/IPv4 socket
//   [::1]:9000      : TCP/IPv6 socket
//
// Wire format: newline-delimited JSON-RPC 2.0 messages.
//
// Request:
//   {"jsonrpc":"2.0","id":1,"method":"getStatus","params":{"key":"val"}}
//
// Response (success):
//   {"jsonrpc":"2.0","id":1,"result":{"status":"ok"}}
//
// Response (error):
//   {"jsonrpc":"2.0","id":1,"error":{"code":-32601,"message":"Method not
//   found"}}
//
// Notification (no id, no response expected):
//   {"jsonrpc":"2.0","method":"notify","params":{"event":"data"}}
//

// Server side — accepts connections and dispatches JSON-RPC 2.0 requests
class IpcServer {
 public:
  using MethodHandler = std::function<Result<Json>(const Json& params)>;
  // Notification handler receives params but returns nothing
  using NotifyHandler = std::function<void(const Json& params)>;

  explicit IpcServer(const std::string& address);
  explicit IpcServer(const std::string& address, TaskGroup& handler_group);
  ~IpcServer();

  // Register a method handler (responds to client)
  void Register(const std::string& method, MethodHandler handler);

  // Register a notification handler (no response sent)
  void OnNotify(const std::string& method, NotifyHandler handler);

  // Send a JSON-RPC 2.0 notification to all connected clients
  void Notify(const std::string& method, const Json& params = Json::object());

  // Start listening
  bool Start();
  void Stop();

  // Number of connected clients
  size_t ClientCount() const;

 private:
  struct ClientConn {
    UnixSocketRaw socket;
    std::mutex write_mu;
    std::string read_buf;
    std::thread read_thread;
  };

  void AcceptLoop();
  void ClientReadLoop(std::shared_ptr<ClientConn> conn);
  void HandleMessage(std::shared_ptr<ClientConn> conn, const std::string& line);
  void DispatchHandler(std::function<void()> task);
  void SendJsonTo(const std::shared_ptr<ClientConn>& conn, const Json& msg);
  void SendTo(const std::shared_ptr<ClientConn>& conn, const std::string& msg);
  static void SendJsonToConn(const std::shared_ptr<ClientConn>& conn,
                             const Json& msg);
  static void SendToConn(const std::shared_ptr<ClientConn>& conn,
                         const std::string& msg);

  std::string address_;
  TaskGroup* handler_group_ = nullptr;
  UnixSocketRaw listen_socket_;
  SockFamily family_ = SockFamily::kUnspec;
  std::atomic<bool> running_{false};
  std::thread accept_thread_;

  std::mutex handlers_mu_;
  std::map<std::string, MethodHandler> handlers_;
  std::map<std::string, NotifyHandler> notify_handlers_;

  mutable std::mutex clients_mu_;
  std::vector<std::shared_ptr<ClientConn>> clients_;
};

// Client side — connects to server and makes JSON-RPC 2.0 calls
class IpcClient {
 public:
  using NotifyCallback =
      std::function<void(const std::string& method, const Json& params)>;

  explicit IpcClient(const std::string& address);
  explicit IpcClient(const std::string& address, TaskGroup& callback_group);
  ~IpcClient();

  bool Connect();
  void Disconnect();
  bool IsConnected() const;

  // Synchronous JSON-RPC 2.0 call with timeout
  Result<Json> Call(const std::string& method,
                    const Json& params = Json::object(),
                    uint32_t timeout_ms = 5000);

  // Async JSON-RPC 2.0 call
  void CallAsync(const std::string& method, const Json& params,
                 std::function<void(Result<Json>)> callback);

  // Send a notification (no response expected)
  bool Notify(const std::string& method, const Json& params = Json::object());

  // Subscribe to incoming notifications from server
  Subscription OnNotify(const std::string& method, NotifyCallback cb);
  Subscription OnNotify(NotifyCallback cb);  // all notifications

 private:
  struct PendingCall {
    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
    Result<Json> result = Err("timeout");
    std::function<void(Result<Json>)> callback;
  };

  void ReadLoop();
  void HandleMessage(const std::string& line);
  void CompletePending(std::shared_ptr<PendingCall> pending,
                       Result<Json> result);
  void FailPendingCalls(const char* message);
  void DispatchCallback(std::function<void(Result<Json>)> callback,
                        Result<Json> result);
  bool SendJson(const Json& msg);
  bool SendRaw(const std::string& data);

  std::string address_;
  TaskGroup* callback_group_ = nullptr;
  UnixSocketRaw socket_;
  SockFamily family_ = SockFamily::kUnspec;
  std::mutex send_mu_;
  std::atomic<bool> connected_{false};
  std::thread read_thread_;

  std::atomic<uint64_t> next_id_{1};

  std::mutex pending_mu_;
  std::map<uint64_t, std::shared_ptr<PendingCall>> pending_calls_;

  Signal<std::string, Json> notify_signal_;  // all notifications
  std::mutex notify_signals_mu_;
  std::map<std::string, Signal<std::string, Json>> named_notify_signals_;
};

}  // namespace xtils
