#pragma once

#include <arpa/inet.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <functional>
#include <stdexcept>
#include <string>
#include <thread>

// One local connection on an ephemeral port. Deadlines bound failed tests.
class NetTestServer {
 public:
  explicit NetTestServer(std::function<void(int)> serve) {
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (fd_ < 0 ||
        bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) ||
        listen(fd_, 4)) {
      if (fd_ >= 0) close(fd_);
      throw std::runtime_error("test listener setup failed");
    }
    socklen_t size = sizeof(addr);
    getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &size);
    port = ntohs(addr.sin_port);
    thread_ = std::thread([this, serve] {
      // Raw OpenSSL fixture servers use write(); shield only this helper thread
      // from peers intentionally rejecting/closing their handshakes.
      sigset_t blocked;
      sigemptyset(&blocked);
      sigaddset(&blocked, SIGPIPE);
      pthread_sigmask(SIG_BLOCK, &blocked, nullptr);
      pollfd pfd{fd_, POLLIN, 0};
      if (poll(&pfd, 1, 5000) <= 0) return;
      int client = accept(fd_, nullptr, nullptr);
      if (client < 0) return;
      timeval timeout{5, 0};
      setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
      setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
      serve(client);
      close(client);
    });
  }
  ~NetTestServer() {
    shutdown(fd_, SHUT_RDWR);
    thread_.join();
    close(fd_);
  }
  std::string Url() const {
    return "http://127.0.0.1:" + std::to_string(port) + "/";
  }
  static std::string ReadHeaders(int fd) {
    std::string result;
    char ch;
    while (result.size() < 65536 && recv(fd, &ch, 1, 0) == 1) {
      result += ch;
      if (result.size() >= 4 &&
          result.compare(result.size() - 4, 4, "\r\n\r\n") == 0)
        break;
    }
    return result;
  }
  static void Reply(int fd, const std::string& response) {
    size_t sent = 0;
    while (sent < response.size()) {
      auto n = send(fd, response.data() + sent, response.size() - sent,
                    MSG_NOSIGNAL);
      if (n <= 0) break;
      sent += static_cast<size_t>(n);
    }
  }
  uint16_t port = 0;

 private:
  int fd_ = -1;
  std::thread thread_;
};
