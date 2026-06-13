#include "xtils/net/ipc_channel.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

namespace xtils {

static const char* kJsonRpcVersion = "2.0";

// =============================================================================
// IpcServer
// =============================================================================

IpcServer::IpcServer(const std::string& socket_path, TaskRunner* runner)
    : socket_path_(socket_path), runner_(runner) {}

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
  ::unlink(socket_path_.c_str());

  listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (listen_fd_ < 0) return false;

  struct sockaddr_un addr {};
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

  if (::bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr),
             sizeof(addr)) < 0) {
    ::close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  if (::listen(listen_fd_, 8) < 0) {
    ::close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  running_ = true;
  accept_thread_ = std::thread(&IpcServer::AcceptLoop, this);
  return true;
}

void IpcServer::Stop() {
  if (!running_.exchange(false)) return;

  if (listen_fd_ >= 0) {
    ::shutdown(listen_fd_, SHUT_RDWR);
    ::close(listen_fd_);
    listen_fd_ = -1;
  }

  if (accept_thread_.joinable()) accept_thread_.join();

  std::vector<std::shared_ptr<ClientConn>> conns;
  {
    std::lock_guard<std::mutex> lock(clients_mu_);
    conns = std::move(clients_);
  }
  for (auto& c : conns) {
    if (c->fd >= 0) {
      ::shutdown(c->fd, SHUT_RDWR);
      ::close(c->fd);
      c->fd = -1;
    }
    if (c->read_thread.joinable()) c->read_thread.join();
  }

  ::unlink(socket_path_.c_str());
}

size_t IpcServer::ClientCount() const {
  std::lock_guard<std::mutex> lock(clients_mu_);
  size_t count = 0;
  for (auto& c : clients_) {
    if (c->fd >= 0) ++count;
  }
  return count;
}

void IpcServer::Notify(const std::string& method, const Json& params) {
  Json msg = Json::object();
  msg["jsonrpc"] = Json(std::string(kJsonRpcVersion));
  msg["method"] = Json(method);
  msg["params"] = params;
  std::string line = msg.dump() + "\n";

  std::lock_guard<std::mutex> lock(clients_mu_);
  for (auto& c : clients_) {
    SendTo(c->fd, line);
  }
}

void IpcServer::AcceptLoop() {
  while (running_) {
    int client_fd = ::accept(listen_fd_, nullptr, nullptr);
    if (client_fd < 0) break;

    auto conn = std::make_shared<ClientConn>();
    conn->fd = client_fd;

    {
      std::lock_guard<std::mutex> lock(clients_mu_);
      clients_.push_back(conn);
    }

    conn->read_thread =
        std::thread(&IpcServer::ClientReadLoop, this, conn);
  }
}

void IpcServer::ClientReadLoop(std::shared_ptr<ClientConn> conn) {
  char buf[4096];
  while (running_ && conn->fd >= 0) {
    ssize_t n = ::recv(conn->fd, buf, sizeof(buf), 0);
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
  // Mark as disconnected; cleanup in Stop()
  if (conn->fd >= 0) {
    ::close(conn->fd);
    conn->fd = -1;
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
    SendTo(conn->fd, response.dump() + "\n");
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
    SendTo(conn->fd, response.dump() + "\n");
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

  SendTo(conn->fd, response.dump() + "\n");
}

void IpcServer::SendTo(int fd, const std::string& msg) {
  size_t total = 0;
  while (total < msg.size()) {
    ssize_t n =
        ::send(fd, msg.data() + total, msg.size() - total, MSG_NOSIGNAL);
    if (n <= 0) break;
    total += static_cast<size_t>(n);
  }
}

// =============================================================================
// IpcClient
// =============================================================================

IpcClient::IpcClient(const std::string& socket_path, TaskRunner* runner)
    : socket_path_(socket_path), runner_(runner) {}

IpcClient::~IpcClient() { Disconnect(); }

bool IpcClient::Connect() {
  fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd_ < 0) return false;

  struct sockaddr_un addr {};
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

  if (::connect(fd_, reinterpret_cast<struct sockaddr*>(&addr),
                sizeof(addr)) < 0) {
    ::close(fd_);
    fd_ = -1;
    return false;
  }

  connected_ = true;
  read_thread_ = std::thread(&IpcClient::ReadLoop, this);
  return true;
}

void IpcClient::Disconnect() {
  if (!connected_.exchange(false)) return;

  if (fd_ >= 0) {
    ::shutdown(fd_, SHUT_RDWR);
    ::close(fd_);
    fd_ = -1;
  }

  if (read_thread_.joinable()) read_thread_.join();

  std::lock_guard<std::mutex> lock(pending_mu_);
  for (auto& [id, call] : pending_calls_) {
    std::lock_guard<std::mutex> cl(call->mu);
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

  if (!SendRaw(msg.dump() + "\n")) {
    std::lock_guard<std::mutex> lock(pending_mu_);
    pending_calls_.erase(id);
    return Err("send failed");
  }

  std::unique_lock<std::mutex> lock(pending->mu);
  if (!pending->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                            [&] { return pending->done; })) {
    std::lock_guard<std::mutex> plock(pending_mu_);
    pending_calls_.erase(id);
    return Err("timeout");
  }

  return std::move(pending->result);
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

  if (!SendRaw(msg.dump() + "\n")) {
    std::lock_guard<std::mutex> lock(pending_mu_);
    pending_calls_.erase(id);
    if (callback) callback(Err("send failed"));
    return;
  }

  std::thread([pending, callback, id, this]() {
    std::unique_lock<std::mutex> lock(pending->mu);
    pending->cv.wait(lock, [&] { return pending->done; });
    lock.unlock();
    {
      std::lock_guard<std::mutex> plock(pending_mu_);
      pending_calls_.erase(id);
    }
    if (callback) callback(std::move(pending->result));
  }).detach();
}

bool IpcClient::Notify(const std::string& method, const Json& params) {
  if (!connected_) return false;

  Json msg = Json::object();
  msg["jsonrpc"] = Json(std::string(kJsonRpcVersion));
  msg["method"] = Json(method);
  msg["params"] = params;
  // No "id" field — this is a notification per JSON-RPC 2.0

  return SendRaw(msg.dump() + "\n");
}

Subscription IpcClient::OnNotify(const std::string& method,
                                 NotifyCallback cb) {
  std::lock_guard<std::mutex> lock(notify_signals_mu_);
  return named_notify_signals_[method].Connect(std::move(cb));
}

Subscription IpcClient::OnNotify(NotifyCallback cb) {
  return notify_signal_.Connect(std::move(cb));
}

bool IpcClient::SendRaw(const std::string& data) {
  size_t total = 0;
  while (total < data.size()) {
    ssize_t n =
        ::send(fd_, data.data() + total, data.size() - total, MSG_NOSIGNAL);
    if (n <= 0) return false;
    total += static_cast<size_t>(n);
  }
  return true;
}

void IpcClient::ReadLoop() {
  std::string read_buf;
  char buf[4096];
  while (connected_ && fd_ >= 0) {
    ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
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
    std::lock_guard<std::mutex> cl(call->mu);
    if (!call->done) {
      call->result = Err("disconnected");
      call->done = true;
      call->cv.notify_all();
    }
  }
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
  }

  std::lock_guard<std::mutex> lock(pending->mu);
  auto err_ptr = msg->find("error");
  if (err_ptr && err_ptr->is_object()) {
    int code =
        static_cast<int>(err_ptr->get_integer("code").value_or(-1));
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
