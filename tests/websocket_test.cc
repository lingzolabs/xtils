// WebSocket client unit test.
// Uses a raw TCP socket to act as a minimal WebSocket server (echo).

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "xtils/net/websocket_client.h"
#include "xtils/net/websocket_common.h"
#include "xtils/tasks/thread_task_runner.h"
#include "xtils/utils/base64.h"
#include "xtils/utils/sha1.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

using namespace xtils;

// --- Helpers ---

static uint16_t FindPort(uint16_t start) {
  for (uint16_t p = start; p < start + 100; ++p) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) continue;
    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(p);
    int ret = bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    close(fd);
    if (ret == 0) return p;
  }
  return 0;
}

// Minimal WebSocket server: accept one connection, perform handshake,
// echo text messages back, handle close frames.
class MiniWsServer {
 public:
  MiniWsServer() = default;
  ~MiniWsServer() { Stop(); }

  bool Start(uint16_t port) {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) return false;

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr),
             sizeof(addr)) < 0) {
      close(listen_fd_);
      listen_fd_ = -1;
      return false;
    }
    if (listen(listen_fd_, 1) < 0) {
      close(listen_fd_);
      listen_fd_ = -1;
      return false;
    }

    running_ = true;
    thread_ = std::thread([this]() { Run(); });
    return true;
  }

  void Stop() {
    running_ = false;
    if (listen_fd_ >= 0) {
      shutdown(listen_fd_, SHUT_RDWR);
      close(listen_fd_);
      listen_fd_ = -1;
    }
    if (thread_.joinable()) thread_.join();
  }

 private:
  void Run() {
    while (running_) {
      struct sockaddr_in client_addr {};
      socklen_t len = sizeof(client_addr);
      int client_fd =
          accept(listen_fd_, reinterpret_cast<struct sockaddr*>(&client_addr),
                 &len);
      if (client_fd < 0) break;
      HandleClient(client_fd);
      close(client_fd);
    }
  }

  void HandleClient(int fd) {
    // Read HTTP upgrade request.
    char buf[4096];
    std::string request;
    while (true) {
      ssize_t n = recv(fd, buf, sizeof(buf), 0);
      if (n <= 0) return;
      request.append(buf, n);
      if (request.find("\r\n\r\n") != std::string::npos) break;
    }

    // Extract Sec-WebSocket-Key.
    std::string key;
    auto pos = request.find("Sec-WebSocket-Key: ");
    if (pos == std::string::npos) return;
    auto start = pos + 19;
    auto end = request.find("\r\n", start);
    key = request.substr(start, end - start);

    // Compute accept value.
    std::string accept_key =
        WebSocketUtils::ComputeWebSocketAccept(key);

    // Send handshake response.
    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " +
        accept_key + "\r\n\r\n";
    send(fd, response.data(), response.size(), 0);

    // Echo loop: read frames and echo text back.
    std::vector<uint8_t> recv_buf;
    while (running_) {
      char frame_buf[4096];
      ssize_t n = recv(fd, frame_buf, sizeof(frame_buf), 0);
      if (n <= 0) break;
      recv_buf.insert(recv_buf.end(), frame_buf, frame_buf + n);

      // Parse frames.
      while (recv_buf.size() >= 2) {
        WebSocketFrame frame;
        size_t consumed =
            WebSocketUtils::ParseFrame(recv_buf.data(), recv_buf.size(), frame);
        if (consumed == 0) break;  // Need more data.
        recv_buf.erase(recv_buf.begin(), recv_buf.begin() + consumed);

        // ParseFrame already unmasked the payload.

        if (frame.opcode == WebSocketOpcode::kText ||
            frame.opcode == WebSocketOpcode::kBinary) {
          // Echo back (unmasked, server→client).
          auto reply = WebSocketUtils::BuildFrame(
              frame.opcode, frame.payload.data(), frame.payload.size());
          send(fd, reply.data(), reply.size(), 0);
        } else if (frame.opcode == WebSocketOpcode::kPing) {
          // Respond with pong.
          auto pong = WebSocketUtils::BuildFrame(
              WebSocketOpcode::kPong, frame.payload.data(),
              frame.payload.size());
          send(fd, pong.data(), pong.size(), 0);
        } else if (frame.opcode == WebSocketOpcode::kClose) {
          // Send close back and exit.
          auto close_frame = WebSocketUtils::BuildFrame(
              WebSocketOpcode::kClose, frame.payload.data(),
              frame.payload.size());
          send(fd, close_frame.data(), close_frame.size(), 0);
          return;
        }
      }
    }
  }

  int listen_fd_ = -1;
  std::atomic<bool> running_{false};
  std::thread thread_;
};

// Client-side listener that records events.
class TestWsListener : public WebSocketClientEventListener {
 public:
  std::mutex mu;
  std::condition_variable cv;
  std::atomic<bool> connected{false};
  std::atomic<bool> closed{false};
  std::string last_message;
  std::string last_error;
  std::atomic<int> message_count{0};

  void OnWebSocketConnected(WebSocketClient*) override {
    std::lock_guard<std::mutex> lk(mu);
    connected = true;
    cv.notify_all();
  }

  void OnWebSocketMessage(WebSocketClient*,
                          const WebSocketMessage& msg) override {
    std::lock_guard<std::mutex> lk(mu);
    last_message = msg.data;
    message_count++;
    cv.notify_all();
  }

  void OnWebSocketClosed(WebSocketClient*, uint16_t,
                         const std::string&) override {
    std::lock_guard<std::mutex> lk(mu);
    closed = true;
    cv.notify_all();
  }

  void OnWebSocketError(WebSocketClient*, const std::string& err) override {
    std::lock_guard<std::mutex> lk(mu);
    last_error = err;
    cv.notify_all();
  }

  bool WaitConnected(int timeout_ms = 3000) {
    std::unique_lock<std::mutex> lk(mu);
    return cv.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                       [this] { return connected.load(); });
  }

  bool WaitMessage(const std::string& expected, int timeout_ms = 3000) {
    std::unique_lock<std::mutex> lk(mu);
    return cv.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                       [this, &expected] { return last_message == expected; });
  }

  bool WaitMessageCount(int count, int timeout_ms = 3000) {
    std::unique_lock<std::mutex> lk(mu);
    return cv.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                       [this, count] { return message_count >= count; });
  }

  bool WaitClosed(int timeout_ms = 3000) {
    std::unique_lock<std::mutex> lk(mu);
    return cv.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                       [this] { return closed.load(); });
  }

  void ClearMessage() {
    std::lock_guard<std::mutex> lk(mu);
    last_message.clear();
  }
};

// --- Tests ---

TEST_CASE("WebSocket: connect and echo text message") {
  uint16_t port = FindPort(19800);
  REQUIRE(port != 0);

  MiniWsServer server;
  REQUIRE(server.Start(port));

  auto task_runner = ThreadTaskRunner::CreateAndStart("ws_client");
  TestWsListener listener;
  WebSocketClient client(&task_runner, &listener);

  std::string url = "ws://127.0.0.1:" + std::to_string(port) + "/ws";
  REQUIRE(client.Connect(url));
  REQUIRE(listener.WaitConnected());
  CHECK(client.IsConnected());

  // Send a text message and wait for echo.
  client.SendText("hello xtils");
  REQUIRE(listener.WaitMessage("hello xtils"));
  CHECK(listener.last_message == "hello xtils");

  // Clean up.
  client.Close();
  listener.WaitClosed(2000);
}

TEST_CASE("WebSocket: multiple messages") {
  uint16_t port = FindPort(19810);
  REQUIRE(port != 0);

  MiniWsServer server;
  REQUIRE(server.Start(port));

  auto task_runner = ThreadTaskRunner::CreateAndStart("ws_client2");
  TestWsListener listener;
  WebSocketClient client(&task_runner, &listener);

  std::string url = "ws://127.0.0.1:" + std::to_string(port) + "/ws";
  REQUIRE(client.Connect(url));
  REQUIRE(listener.WaitConnected());

  for (int i = 0; i < 5; ++i) {
    listener.ClearMessage();
    std::string msg = "msg_" + std::to_string(i);
    client.SendText(msg);
    REQUIRE(listener.WaitMessage(msg));
    CHECK(listener.last_message == msg);
  }

  client.Close();
  listener.WaitClosed(2000);
}

TEST_CASE("WebSocket: ping/pong keeps connection alive") {
  uint16_t port = FindPort(19820);
  REQUIRE(port != 0);

  MiniWsServer server;
  REQUIRE(server.Start(port));

  auto task_runner = ThreadTaskRunner::CreateAndStart("ws_client3");
  TestWsListener listener;
  WebSocketClient client(&task_runner, &listener);

  std::string url = "ws://127.0.0.1:" + std::to_string(port) + "/ws";
  REQUIRE(client.Connect(url));
  REQUIRE(listener.WaitConnected());

  // Send ping — server responds with pong (handled internally by client).
  CHECK(client.SendPing("test_ping"));

  // Give some time for the pong round trip.
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Connection should still be alive.
  CHECK(client.IsConnected());

  // Verify we can still send messages after ping/pong.
  client.SendText("after_ping");
  REQUIRE(listener.WaitMessage("after_ping"));

  client.Close();
  listener.WaitClosed(2000);
}
