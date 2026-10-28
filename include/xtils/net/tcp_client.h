#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "xtils/system/unix_socket.h"
#include "xtils/tasks/task_runner.h"

namespace xtils {

class TcpClient;

class TcpClientEventListener {
 public:
  virtual ~TcpClientEventListener() = default;

  // Called when connection is established or fails
  virtual void OnConnected(bool success) = 0;

  // Called when data is received from server
  virtual void OnDataReceived(const void* data, size_t len) = 0;

  // Called when connection is lost or closed
  virtual void OnDisconnected() = 0;

  virtual bool OnReadable() { return false; }
  virtual void OnWriteCapacityAvailable() {}

  // Called when client encounters an error
  virtual void OnError(const std::string& error) {}
};

class TcpClient : public UnixSocket::EventListener {
 public:
  enum class State { kDisconnected = 0, kConnecting, kConnected, kError };

  explicit TcpClient(TaskRunner* task_runner, TcpClientEventListener* listener);
  ~TcpClient() override;

  // Connect to server at the specified address and port
  // address can be IPv4 address like "192.168.1.1" or IPv6 like "::1"
  // Returns whether the request was accepted, never waits for another runner.
  // Completion is reported by OnConnected. Other I/O stays on the owner runner.
  bool Connect(const std::string& address, uint16_t port);

  // Connect using hostname resolution (supports both IPv4 and IPv6)
  bool ConnectToHost(const std::string& hostname, uint16_t port);

  // Disconnect from server
  void Disconnect();

  // Send data to server
  SendStatus TrySend(const void* data, size_t len);
  bool Send(const void* data, size_t len);
  bool SendString(const std::string& data);

  // Receive data from server (non-blocking)
  size_t Receive(void* buffer, size_t buffer_size);

  // Get current connection state
  State GetState() const { return state_; }

  // Check if client is connected
  bool IsConnected() const { return state_ == State::kConnected; }

  // Get server address information
  std::string GetServerAddress() const;

  // Get the underlying socket file descriptor
  SocketHandle GetSocketFd() const;

  // TLS adapters use OnReadable() for both read and requested write readiness.
  bool SetWritableInterest(bool enabled);

  // Set connection timeout in milliseconds
  void SetConnectTimeout(uint32_t timeout_ms);

  // Set send/receive timeouts
  void SetSendTimeout(uint32_t timeout_ms);
  void SetReceiveTimeout(uint32_t timeout_ms);

  // Enable/disable keepalive
  void SetKeepAlive(bool enable);

  // Set TCP_NODELAY option
  void SetNoDelay(bool enable);

 private:
  // UnixSocket::EventListener implementation
  void OnConnect(UnixSocket* self, bool connected) override;
  void OnDisconnect(UnixSocket* self) override;
  void OnDataAvailable(UnixSocket* self) override;
  void OnWriteCapacityAvailable(UnixSocket*) override;
  void OnNewIncomingConnection(
      UnixSocket* self, std::unique_ptr<UnixSocket> new_connection) override {}

  void ConnectOnRunner(const std::string& address, uint16_t port,
                       uint64_t generation);
  void SetState(State new_state);
  void HandleError(const std::string& error);

  TaskRunner* task_runner_;
  TcpClientEventListener* listener_;
  std::unique_ptr<UnixSocket> socket_;
  std::atomic<State> state_;
  std::atomic<uint64_t> connect_generation_{0};
  struct ConnectLifetime {
    std::recursive_mutex mutex;
    TcpClient* owner = nullptr;
  };
  std::shared_ptr<ConnectLifetime> connect_lifetime_ =
      std::make_shared<ConnectLifetime>();
  std::string server_address_;
  uint16_t server_port_;
  uint32_t connect_timeout_ms_;
};

}  // namespace xtils
