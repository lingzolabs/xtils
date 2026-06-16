#include "xtils/net/ipc_channel.h"

#include <unistd.h>

#include <chrono>
#include <utility>

namespace xtils {

static const char* kJsonRpcVersion = "2.0";
static constexpr uint32_t kIpcIoTimeoutMs = 5000;

namespace {

bool ShouldUnlinkAddress(SockFamily family, const std::string& address) {
  return family == SockFamily::kUnix && !address.empty() && address[0] != '@';
}

}  // namespace

// =============================================================================
// IpcServer
// =============================================================================

IpcServer::IpcServer(const std::string& address, TaskRunner* runner)
    : address_(address),
      runner_(runner),
      family_(GetSockFamily(address.c_str())) {}

IpcServer::~IpcServer() { Stop(); }

void IpcServer::Register(const std::string& method, MethodHandler handler) {
  std::lock_guard<std::mutex> lock(handlers_mu_);
  handlers_[method] = std::move(handler);
}

void IpcServer::OnNotify(const std::string& method, NotifyHandler handler) {
  std::lock_guard<std::mutex> lock(handlers_mu_);
  notify_handlers_[method] = std::move(handler);
}

bool IpcServer::Start() {
  if (running_) return true;
  if (family_ == SockFamily::kUnspec) return false;

  if (ShouldUnlinkAddress(family_, address_)) ::unlink(address_.c_str());

  listen_socket_ = UnixSocketRaw::CreateMayFail(family_, SockType::kStream);
  if (!listen_socket_) return false;

  if (!listen_socket_.Bind(address_) || !listen_socket_.Listen()) {
    listen_socket_.Shutdown();
    if (ShouldUnlinkAddress(family_, address_)) ::unlink(address_.c_str());
    return false;
  }

  running_ = true;
  accept_thread_ = std::thread(&IpcServer::AcceptLoop, this);
  return true;
}

void IpcServer::Stop() {
  if (!running_.exchange(false)) return;

  if (listen_socket_) listen_socket_.Shutdown();

  if (accept_thread_.joinable()) accept_thread_.join();

  std::vector<std::shared_ptr<ClientConn>> conns;
  {
    std::lock_guard<std::mutex> lock(clients_mu_);
    conns = std::move(clients_);
  }
  for (auto& c : conns) {
    {
      std::lock_guard<std::mutex> lock(c->write_mu);
      if (c->socket) c->socket.Shutdown();
    }
    if (c->read_thread.joinable()) c->read_thread.join();
  }

  if (ShouldUnlinkAddress(family_, address_)) ::unlink(address_.c_str());
}

size_t IpcServer::ClientCount() const {
  std::lock_guard<std::mutex> lock(clients_mu_);
  size_t count = 0;
  for (auto& c : clients_) {
    if (c->socket) ++count;
  }
  return count;
}

void IpcServer::Notify(const std::string& method, const Json& params) {
  Json msg = Json::object();
  msg["jsonrpc"] = Json(std::string(kJsonRpcVersion));
  msg["method"] = Json(method);
  msg["params"] = params;
  std::vector<std::shared_ptr<ClientConn>> conns;
  {
    std::lock_guard<std::mutex> lock(clients_mu_);
    conns = clients_;
  }
  for (auto& c : conns) {
    SendJsonTo(c, msg);
  }
}

void IpcServer::AcceptLoop() {
  while (running_) {
    UnixSocketRaw client_socket = listen_socket_.Accept();
    if (!client_socket) {
      if (running_) break;
      continue;
    }

    auto conn = std::make_shared<ClientConn>();
    conn->socket = std::move(client_socket);
    conn->socket.SetTxTimeout(kIpcIoTimeoutMs);

    {
      std::lock_guard<std::mutex> lock(clients_mu_);
      clients_.push_back(conn);
    }

    conn->read_thread = std::thread(&IpcServer::ClientReadLoop, this, conn);
  }
}

void IpcServer::ClientReadLoop(std::shared_ptr<ClientConn> conn) {
  char buf[4096];
  while (running_ && conn->socket) {
    ssize_t n = conn->socket.Receive(buf, sizeof(buf));
    if (n <= 0) break;

    conn->read_buf.append(buf, static_cast<size_t>(n));

    size_t pos;
    while ((pos = conn->read_buf.find('\n')) != std::string::npos) {
      std::string line = conn->read_buf.substr(0, pos);
      conn->read_buf.erase(0, pos + 1);
      if (!line.empty()) {
        HandleMessage(conn, line);
      }
    }
  }

  {
    std::lock_guard<std::mutex> lock(conn->write_mu);
    if (conn->socket) conn->socket.Shutdown();
  }
}

void IpcServer::HandleMessage(std::shared_ptr<ClientConn> conn,
                              const std::string& line) {
  auto msg = Json::parse(line);
  if (!msg || !msg->is_object()) {
    // JSON-RPC 2.0: Parse error
    Json response = Json::object();
    response["jsonrpc"] = Json(std::string(kJsonRpcVersion));
    response["id"] = Json(nullptr);
    Json err_obj = Json::object();
    err_obj["code"] = Json(static_cast<int64_t>(jsonrpc::kParseError));
    err_obj["message"] = Json(std::string("Parse error"));
    response["error"] = std::move(err_obj);
    SendJsonTo(conn, response);
    return;
  }

  // Validate jsonrpc version
  auto ver_ptr = msg->find("jsonrpc");
  if (!ver_ptr || !ver_ptr->is_string() || ver_ptr->as_string() != "2.0") {
    // Be lenient: still process if method exists
  }

  auto method_ptr = msg->find("method");
  if (!method_ptr || !method_ptr->is_string()) {
    // Invalid Request
    Json response = Json::object();
    response["jsonrpc"] = Json(std::string(kJsonRpcVersion));
    response["id"] = msg->get("id").value_or(Json(nullptr));
    Json err_obj = Json::object();
    err_obj["code"] = Json(static_cast<int64_t>(jsonrpc::kInvalidRequest));
    err_obj["message"] = Json(std::string("Invalid Request"));
    response["error"] = std::move(err_obj);
    SendJsonTo(conn, response);
    return;
  }

  std::string method = method_ptr->as_string();
  Json params = msg->get("params").value_or(Json::object());

  // Check if it's a notification (no "id" field)
  auto id_ptr = msg->find("id");
  bool is_notification = (id_ptr == nullptr);

  if (is_notification) {
    // Notification — call handler but don't respond
    std::lock_guard<std::mutex> lock(handlers_mu_);
    auto it = notify_handlers_.find(method);
    if (it != notify_handlers_.end()) {
      it->second(params);
    }
    return;
  }

  // Request — find handler and respond
  MethodHandler handler;
  {
    std::lock_guard<std::mutex> lock(handlers_mu_);
    auto it = handlers_.find(method);
    if (it != handlers_.end()) {
      handler = it->second;
    }
  }

  Json response = Json::object();
  response["jsonrpc"] = Json(std::string(kJsonRpcVersion));
  response["id"] = *id_ptr;

  if (handler) {
    auto result = handler(params);
    if (result.ok()) {
      response["result"] = *result;
    } else {
      Json err_obj = Json::object();
      err_obj["code"] = Json(static_cast<int64_t>(result.error().code));
      err_obj["message"] = Json(result.error().message);
      response["error"] = std::move(err_obj);
    }
  } else {
    Json err_obj = Json::object();
    err_obj["code"] = Json(static_cast<int64_t>(jsonrpc::kMethodNotFound));
    err_obj["message"] = Json("Method not found: " + method);
    response["error"] = std::move(err_obj);
  }

  SendJsonTo(conn, response);
}

void IpcServer::SendJsonTo(const std::shared_ptr<ClientConn>& conn,
                           const Json& msg) {
  std::string line = msg.dump();
  line.push_back('\n');
  SendTo(conn, line);
}

void IpcServer::SendTo(const std::shared_ptr<ClientConn>& conn,
                       const std::string& msg) {
  if (!conn) return;

  std::lock_guard<std::mutex> lock(conn->write_mu);
  if (!conn->socket) return;

  ssize_t n = conn->socket.Send(msg.data(), msg.size());
  if (n != static_cast<ssize_t>(msg.size())) return;
}

// =============================================================================
// IpcClient
// =============================================================================

IpcClient::IpcClient(const std::string& address, TaskRunner* runner)
    : address_(address),
      runner_(runner),
      family_(GetSockFamily(address.c_str())) {}

IpcClient::~IpcClient() { Disconnect(); }

bool IpcClient::Connect() {
  if (connected_) return true;
  if (family_ == SockFamily::kUnspec) return false;

  socket_ = UnixSocketRaw::CreateMayFail(family_, SockType::kStream);
  if (!socket_) return false;
  socket_.SetTxTimeout(kIpcIoTimeoutMs);

  if (!socket_.Connect(address_)) {
    socket_.Shutdown();
    return false;
  }

  connected_ = true;
  read_thread_ = std::thread(&IpcClient::ReadLoop, this);
  return true;
}

void IpcClient::Disconnect() {
  connected_ = false;

  {
    std::lock_guard<std::mutex> lock(send_mu_);
    if (socket_) socket_.Shutdown();
  }

  if (read_thread_.joinable()) read_thread_.join();

  std::lock_guard<std::mutex> lock(pending_mu_);
  for (auto& [id, call] : pending_calls_) {
    (void)id;
    std::lock_guard<std::mutex> call_lock(call->mu);
    if (!call->done) {
      call->result = Err("disconnected");
      call->done = true;
      call->cv.notify_all();
    }
  }
  pending_calls_.clear();
}

bool IpcClient::IsConnected() const { return connected_; }

Result<Json> IpcClient::Call(const std::string& method, const Json& params,
                             uint32_t timeout_ms) {
  if (!connected_) return Err("not connected");

  uint64_t id = next_id_++;
  Json msg = Json::object();
  msg["jsonrpc"] = Json(std::string(kJsonRpcVersion));
  msg["id"] = Json(static_cast<int64_t>(id));
  msg["method"] = Json(method);
  msg["params"] = params;

  auto pending = std::make_shared<PendingCall>();
  {
    std::lock_guard<std::mutex> lock(pending_mu_);
    pending_calls_[id] = pending;
  }

  if (!SendJson(msg)) {
    std::lock_guard<std::mutex> lock(pending_mu_);
    pending_calls_.erase(id);
    return Err("send failed");
  }

  std::unique_lock<std::mutex> lock(pending->mu);
  if (!pending->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                            [&] { return pending->done; })) {
    lock.unlock();
    std::lock_guard<std::mutex> plock(pending_mu_);
    pending_calls_.erase(id);
    return Err("timeout");
  }

  Result<Json> result = std::move(pending->result);
  lock.unlock();
  return result;
}

void IpcClient::CallAsync(const std::string& method, const Json& params,
                          std::function<void(Result<Json>)> callback) {
  if (!connected_) {
    if (callback) callback(Err("not connected"));
    return;
  }

  uint64_t id = next_id_++;
  Json msg = Json::object();
  msg["jsonrpc"] = Json(std::string(kJsonRpcVersion));
  msg["id"] = Json(static_cast<int64_t>(id));
  msg["method"] = Json(method);
  msg["params"] = params;

  auto pending = std::make_shared<PendingCall>();
  {
    std::lock_guard<std::mutex> lock(pending_mu_);
    pending_calls_[id] = pending;
  }

  if (!SendJson(msg)) {
    std::lock_guard<std::mutex> lock(pending_mu_);
    pending_calls_.erase(id);
    if (callback) callback(Err("send failed"));
    return;
  }

  std::thread([pending, callback]() {
    std::unique_lock<std::mutex> lock(pending->mu);
    pending->cv.wait(lock, [&] { return pending->done; });
    Result<Json> result = std::move(pending->result);
    lock.unlock();
    if (callback) callback(std::move(result));
  }).detach();
}

bool IpcClient::Notify(const std::string& method, const Json& params) {
  if (!connected_) return false;

  Json msg = Json::object();
  msg["jsonrpc"] = Json(std::string(kJsonRpcVersion));
  msg["method"] = Json(method);
  msg["params"] = params;
  // No "id" field — this is a notification per JSON-RPC 2.0

  return SendJson(msg);
}

Subscription IpcClient::OnNotify(const std::string& method, NotifyCallback cb) {
  std::lock_guard<std::mutex> lock(notify_signals_mu_);
  return named_notify_signals_[method].Connect(std::move(cb));
}

Subscription IpcClient::OnNotify(NotifyCallback cb) {
  return notify_signal_.Connect(std::move(cb));
}

bool IpcClient::SendJson(const Json& msg) {
  std::string line = msg.dump();
  line.push_back('\n');
  return SendRaw(line);
}

bool IpcClient::SendRaw(const std::string& data) {
  std::lock_guard<std::mutex> lock(send_mu_);
  if (!socket_) return false;

  ssize_t n = socket_.Send(data.data(), data.size());
  return n == static_cast<ssize_t>(data.size());
}

void IpcClient::ReadLoop() {
  std::string read_buf;
  char buf[4096];
  while (connected_ && socket_) {
    ssize_t n = socket_.Receive(buf, sizeof(buf));
    if (n <= 0) {
      connected_ = false;
      break;
    }

    read_buf.append(buf, static_cast<size_t>(n));

    size_t pos;
    while ((pos = read_buf.find('\n')) != std::string::npos) {
      std::string line = read_buf.substr(0, pos);
      read_buf.erase(0, pos + 1);
      if (!line.empty()) {
        HandleMessage(line);
      }
    }
  }

  // Wake all pending calls on disconnect
  std::lock_guard<std::mutex> lock(pending_mu_);
  for (auto& [id, call] : pending_calls_) {
    (void)id;
    std::lock_guard<std::mutex> call_lock(call->mu);
    if (!call->done) {
      call->result = Err("disconnected");
      call->done = true;
      call->cv.notify_all();
    }
  }
  pending_calls_.clear();
}

void IpcClient::HandleMessage(const std::string& line) {
  auto msg = Json::parse(line);
  if (!msg) return;

  // Check if it's a notification (has "method" but no "id")
  auto method_ptr = msg->find("method");
  auto id_ptr = msg->find("id");

  if (method_ptr && method_ptr->is_string() && !id_ptr) {
    // Server notification
    std::string method = method_ptr->as_string();
    Json params = msg->get("params").value_or(Json::object());
    notify_signal_.Emit(method, params);
    {
      std::lock_guard<std::mutex> lock(notify_signals_mu_);
      auto it = named_notify_signals_.find(method);
      if (it != named_notify_signals_.end()) {
        it->second.Emit(method, params);
      }
    }
    return;
  }

  // It's a response — match by ID
  if (!id_ptr || !id_ptr->is_integer()) return;

  uint64_t id = static_cast<uint64_t>(id_ptr->as_integer());

  std::shared_ptr<PendingCall> pending;
  {
    std::lock_guard<std::mutex> lock(pending_mu_);
    auto it = pending_calls_.find(id);
    if (it == pending_calls_.end()) return;
    pending = it->second;
    pending_calls_.erase(it);
  }

  std::lock_guard<std::mutex> lock(pending->mu);
  auto err_ptr = msg->find("error");
  if (err_ptr && err_ptr->is_object()) {
    int code = static_cast<int>(err_ptr->get_integer("code").value_or(-1));
    std::string message =
        err_ptr->get_string("message").value_or("unknown error");
    pending->result = Err(code, message);
  } else {
    auto result_ptr = msg->find("result");
    if (result_ptr) {
      pending->result = *result_ptr;
    } else {
      pending->result = Json(nullptr);
    }
  }
  pending->done = true;
  pending->cv.notify_all();
}

}  // namespace xtils
