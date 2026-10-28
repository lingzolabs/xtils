#include "xtils/system/unix_socket.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <future>
#include <mutex>
#include <string>
#include <thread>

#include "xtils/tasks/thread_task_runner.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

using namespace xtils;

template <typename T>
bool WaitFor(std::future<T>& f, int ms = 2000) {
  return f.wait_for(std::chrono::milliseconds(ms)) == std::future_status::ready;
}

TEST_CASE(
    "UnixSocket: backpressure does not block runner and queued close drains") {
  auto [raw, peer] =
      UnixSocketRaw::CreatePairPosix(SockFamily::kUnix, SockType::kStream);
  int small = 4096;
  setsockopt(raw.fd(), SOL_SOCKET, SO_SNDBUF, &small, sizeof(small));
  peer.SetRxTimeout(3000);
  auto runner = ThreadTaskRunner::CreateAndStart("socket_backpressure");
  UnixSocket::EventListener listener;
  std::unique_ptr<UnixSocket> socket;
  const std::string payload(1024 * 1024, 'q');
  std::promise<bool> sent;
  runner.PostTask([&] {
    socket = UnixSocket::AdoptConnected(raw.ReleaseFd(), &listener, &runner,
                                        SockFamily::kUnix, SockType::kStream);
    bool accepted = socket->Send(payload.data(), payload.size());
    socket->ShutdownAfterFlush(false);
    sent.set_value(accepted);
  });
  auto sent_future = sent.get_future();
  CHECK(WaitFor(sent_future, 500));
  std::string received;
  char buffer[16384];
  while (received.size() < payload.size()) {
    auto n = peer.Receive(buffer, sizeof(buffer));
    if (n <= 0) break;
    received.append(buffer, static_cast<size_t>(n));
  }
  CHECK(sent_future.get());
  CHECK(received == payload);
  std::promise<void> closed;
  runner.PostTask([&] {
    socket.reset();
    closed.set_value();
  });
  closed.get_future().wait();
}

TEST_CASE(
    "UnixSocket: TrySend resumes through deferred capacity notification") {
  auto [raw, peer] =
      UnixSocketRaw::CreatePairPosix(SockFamily::kUnix, SockType::kStream);
  int small = 4096;
  setsockopt(raw.fd(), SOL_SOCKET, SO_SNDBUF, &small, sizeof(small));
  peer.SetRxTimeout(3000);
  auto runner = ThreadTaskRunner::CreateAndStart("socket_capacity");
  const std::string payload(12 * 1024 * 1024, 'c');
  struct Producer : UnixSocket::EventListener {
    void OnWriteCapacityAvailable(UnixSocket* socket) override {
      ++resumes;
      Pump(socket);
    }
    void Pump(UnixSocket* socket) {
      while (offset < payload->size()) {
        size_t len = std::min(kMaxSendChunkSize, payload->size() - offset);
        auto status = socket->TrySend(payload->data() + offset, len);
        if (status == SendStatus::kWouldBlock) {
          if (!reported) {
            reported = true;
            blocked.set_value();
          }
          return;
        }
        if (status != SendStatus::kAccepted) return;
        offset += len;
      }
      socket->ShutdownAfterFlush(false);
    }
    const std::string* payload = nullptr;
    size_t offset = 0;
    int resumes = 0;
    bool reported = false;
    std::promise<void> blocked;
  } producer;
  producer.payload = &payload;
  std::unique_ptr<UnixSocket> socket;
  runner.PostTask([&] {
    socket = UnixSocket::AdoptConnected(raw.ReleaseFd(), &producer, &runner,
                                        SockFamily::kUnix, SockType::kStream);
    producer.Pump(socket.get());
  });
  auto blocked = producer.blocked.get_future();
  CHECK(WaitFor(blocked));
  std::promise<void> marker;
  runner.PostTask([&] { marker.set_value(); });
  auto ready = marker.get_future();
  CHECK(WaitFor(ready));
  std::string received;
  char buffer[65536];
  while (received.size() < payload.size()) {
    auto n = peer.Receive(buffer, sizeof(buffer));
    if (n <= 0) break;
    received.append(buffer, static_cast<size_t>(n));
  }
  CHECK(received == payload);
  std::promise<int> closed;
  runner.PostTask([&] {
    socket.reset();
    closed.set_value(producer.resumes);
  });
  CHECK(closed.get_future().get() > 0);
}

TEST_CASE("UnixSocket: queued descriptors survive caller close and send once") {
  auto [raw, peer] =
      UnixSocketRaw::CreatePairPosix(SockFamily::kUnix, SockType::kStream);
  int small = 4096;
  setsockopt(raw.fd(), SOL_SOCKET, SO_SNDBUF, &small, sizeof(small));
  peer.SetRxTimeout(3000);
  auto runner = ThreadTaskRunner::CreateAndStart("socket_queued_fd");
  UnixSocket::EventListener listener;
  std::unique_ptr<UnixSocket> socket;
  const std::string payload(256 * 1024, 'p');
  std::promise<bool> sent;
  runner.PostTask([&] {
    socket = UnixSocket::AdoptConnected(raw.ReleaseFd(), &listener, &runner,
                                        SockFamily::kUnix, SockType::kStream);
    ScopedFile fd(open("/dev/null", O_RDONLY));
    bool ok = socket->Send(payload.data(), payload.size());
    ok = socket->Send("f", 1, fd.get()) && ok;
    sent.set_value(ok);
  });
  auto ready = sent.get_future();
  REQUIRE(WaitFor(ready));
  CHECK(ready.get());
  size_t received = 0;
  int descriptors = 0;
  while (received < payload.size() + 1) {
    char buffer[16384];
    ScopedFile fd;
    auto n = peer.Receive(buffer, sizeof(buffer), &fd, 1);
    if (n <= 0) break;
    received += static_cast<size_t>(n);
    if (fd) {
      ++descriptors;
      CHECK(fcntl(fd.get(), F_GETFD) >= 0);
    }
  }
  CHECK(received == payload.size() + 1);
  CHECK(descriptors == 1);
  std::promise<void> closed;
  runner.PostTask([&] {
    socket.reset();
    closed.set_value();
  });
  closed.get_future().wait();
}

TEST_CASE("UnixSocket: queued sends have bounded capacity and a deadline") {
  auto [raw, peer] =
      UnixSocketRaw::CreatePairPosix(SockFamily::kUnix, SockType::kStream);
  int small = 4096;
  setsockopt(raw.fd(), SOL_SOCKET, SO_SNDBUF, &small, sizeof(small));
  auto runner = ThreadTaskRunner::CreateAndStart("socket_send_timeout");
  struct Listener : UnixSocket::EventListener {
    void OnDisconnect(UnixSocket*) override { disconnected.set_value(); }
    std::promise<void> disconnected;
  } listener;
  std::unique_ptr<UnixSocket> socket;
  std::promise<bool> sent;
  runner.PostTask([&] {
    socket = UnixSocket::AdoptConnected(raw.ReleaseFd(), &listener, &runner,
                                        SockFamily::kUnix, SockType::kStream);
    socket->SetTxTimeout(20);
    const std::string payload(1024 * 1024, 'x');
    bool rejected = !socket->Send(payload.data(), 8 * 1024 * 1024 + 1);
    sent.set_value(rejected && socket->Send(payload.data(), payload.size()));
  });
  auto ready = sent.get_future();
  CHECK(WaitFor(ready));
  CHECK(ready.get());
  auto disconnected = listener.disconnected.get_future();
  CHECK(WaitFor(disconnected));
  std::promise<void> closed;
  runner.PostTask([&] {
    socket.reset();
    closed.set_value();
  });
  closed.get_future().wait();
}

TEST_CASE("UnixSocket: zero-length datagrams are transmitted") {
  auto [raw, peer] =
      UnixSocketRaw::CreatePairPosix(SockFamily::kUnix, SockType::kDgram);
  auto runner = ThreadTaskRunner::CreateAndStart("empty_datagram");
  UnixSocket::EventListener listener;
  std::promise<bool> sent;
  runner.PostTask([&] {
    auto socket =
        UnixSocket::AdoptConnected(raw.ReleaseFd(), &listener, &runner,
                                   SockFamily::kUnix, SockType::kDgram);
    sent.set_value(socket->Send(nullptr, 0));
  });
  CHECK(sent.get_future().get());
  pollfd ready{peer.fd(), POLLIN, 0};
  CHECK(poll(&ready, 1, 500) == 1);
}

// --- GetSockFamily ---

TEST_CASE("GetSockFamily: Unix paths") {
  CHECK(GetSockFamily("/tmp/my.sock") == SockFamily::kUnix);
  CHECK(GetSockFamily("relative.sock") == SockFamily::kUnix);
}

TEST_CASE("GetSockFamily: abstract Unix") {
  CHECK(GetSockFamily("@abstract_name") == SockFamily::kUnix);
}

TEST_CASE("GetSockFamily: IPv4") {
  CHECK(GetSockFamily("1.2.3.4:8080") == SockFamily::kInet);
  CHECK(GetSockFamily("127.0.0.1:0") == SockFamily::kInet);
}

TEST_CASE("GetSockFamily: IPv6") {
  CHECK(GetSockFamily("[::1]:8080") == SockFamily::kInet6);
}

TEST_CASE("GetSockFamily: empty string") {
  CHECK(GetSockFamily("") == SockFamily::kUnspec);
}

// --- UnixSocketRaw: CreatePairPosix ---

TEST_CASE("UnixSocketRaw: CreatePairPosix stream send/receive") {
  auto [a, b] =
      UnixSocketRaw::CreatePairPosix(SockFamily::kUnix, SockType::kStream);
  CHECK(static_cast<bool>(a));
  CHECK(static_cast<bool>(b));

  const std::string msg = "hello from pair";
  ssize_t sent = a.Send(msg.data(), msg.size());
  CHECK(sent == static_cast<ssize_t>(msg.size()));

  char buf[64] = {};
  ssize_t received = b.Receive(buf, sizeof(buf));
  CHECK(received == static_cast<ssize_t>(msg.size()));
  CHECK(std::string(buf, received) == msg);
}

TEST_CASE("UnixSocketRaw: CreatePairPosix dgram") {
  auto [a, b] =
      UnixSocketRaw::CreatePairPosix(SockFamily::kUnix, SockType::kDgram);
  CHECK(static_cast<bool>(a));
  CHECK(static_cast<bool>(b));

  const std::string msg = "datagram test";
  a.Send(msg.data(), msg.size());

  char buf[64] = {};
  ssize_t received = b.Receive(buf, sizeof(buf));
  CHECK(received == static_cast<ssize_t>(msg.size()));
  CHECK(std::string(buf, received) == msg);
}

// --- UnixSocketRaw: CreateMayFail + Bind + Listen + Connect ---

TEST_CASE("UnixSocketRaw: IPv4 bind, listen, connect, send/receive") {
  // Create server socket
  auto server =
      UnixSocketRaw::CreateMayFail(SockFamily::kInet, SockType::kStream);
  CHECK(static_cast<bool>(server));

  // Try binding on port 0 for ephemeral port
  CHECK(server.Bind("127.0.0.1:0"));
  CHECK(server.Listen());

  // Get the actual bound address (e.g., "127.0.0.1:NNNNN")
  std::string server_addr = server.GetSockAddr();
  CHECK_FALSE(server_addr.empty());

  // Create client socket and connect
  auto client =
      UnixSocketRaw::CreateMayFail(SockFamily::kInet, SockType::kStream);
  CHECK(static_cast<bool>(client));
  CHECK(client.Connect(server_addr));

  // Accept the connection on the server side
  struct sockaddr_storage addr {};
  socklen_t addr_len = sizeof(addr);
  int client_fd =
      accept(server.fd(), reinterpret_cast<struct sockaddr*>(&addr), &addr_len);
  CHECK(client_fd >= 0);

  // Send data from client, receive on accepted connection
  const std::string msg = "raw socket test";
  client.Send(msg.data(), msg.size());

  char buf[64] = {};
  ssize_t n = recv(client_fd, buf, sizeof(buf), 0);
  CHECK(n == static_cast<ssize_t>(msg.size()));
  CHECK(std::string(buf, n) == msg);

  close(client_fd);
}

// --- UnixSocket with ThreadTaskRunner ---

namespace {

class TestListener : public UnixSocket::EventListener {
 public:
  std::promise<bool> connect_promise;
  std::promise<std::string> data_promise;
  std::promise<void> disconnect_promise;
  std::promise<std::unique_ptr<UnixSocket>> accept_promise;
  std::atomic<bool> connect_done{false};
  std::atomic<bool> data_done{false};

  void OnConnect(UnixSocket* self, bool connected) override {
    if (!connect_done.exchange(true)) {
      connect_promise.set_value(connected);
    }
  }

  void OnDataAvailable(UnixSocket* self) override {
    std::string s = self->ReceiveString(4096);
    if (!s.empty() && !data_done.exchange(true)) {
      data_promise.set_value(s);
    }
  }

  void OnDisconnect(UnixSocket* self) override {
    try {
      disconnect_promise.set_value();
    } catch (...) {
    }
  }

  void OnNewIncomingConnection(UnixSocket* self,
                               std::unique_ptr<UnixSocket> new_conn) override {
    try {
      accept_promise.set_value(std::move(new_conn));
    } catch (...) {
    }
  }
};

}  // namespace

TEST_CASE("UnixSocket: Listen + Connect + Data with ThreadTaskRunner") {
  auto task_runner = ThreadTaskRunner::CreateAndStart("unix_sock_test");

  TestListener server_listener;
  TestListener client_listener;

  // First bind a raw socket to get ephemeral port
  auto raw = UnixSocketRaw::CreateMayFail(SockFamily::kInet, SockType::kStream);
  CHECK(static_cast<bool>(raw));
  CHECK(raw.Bind("127.0.0.1:0"));
  std::string addr = raw.GetSockAddr();
  CHECK_FALSE(addr.empty());

  // Listen using the pre-bound raw socket
  auto server =
      UnixSocket::Listen(raw.ReleaseFd(), &server_listener, &task_runner,
                         SockFamily::kInet, SockType::kStream);
  CHECK(server);
  CHECK(server->is_listening());

  // Connect client
  auto client = UnixSocket::Connect(addr, &client_listener, &task_runner,
                                    SockFamily::kInet, SockType::kStream);
  CHECK(client);

  // Wait for client connection
  auto connect_fut = client_listener.connect_promise.get_future();
  CHECK(WaitFor(connect_fut));
  CHECK(connect_fut.get() == true);

  // Wait for server to accept
  auto accept_fut = server_listener.accept_promise.get_future();
  CHECK(WaitFor(accept_fut));
  auto accepted = accept_fut.get();
  CHECK(accepted);

  // Client sends data, accepted socket should receive
  // We need a listener for the accepted connection
  TestListener accepted_listener;
  // The accepted connection inherits the server_listener, so data arrives
  // there. Actually, the accepted socket uses the server_listener's
  // OnDataAvailable. Let's send from client and receive from the accepted
  // socket directly.
  client->SendStr("hello from client");

  // Read from accepted socket - give it a moment
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  std::string received = accepted->ReceiveString(4096);
  // The data might already have been consumed by the server_listener's
  // OnDataAvailable. Let's check the server_listener data_promise instead.
  auto data_fut = server_listener.data_promise.get_future();
  if (received.empty()) {
    // Data was consumed by the event listener
    CHECK(WaitFor(data_fut));
    received = data_fut.get();
  }
  CHECK(received == "hello from client");

  // Echo back
  accepted->SendStr("echo: hello from client");

  auto client_data_fut = client_listener.data_promise.get_future();
  CHECK(WaitFor(client_data_fut));
  CHECK(client_data_fut.get() == "echo: hello from client");

  // Cleanup
  client->Shutdown(false);
  accepted->Shutdown(false);
  server->Shutdown(false);
}
