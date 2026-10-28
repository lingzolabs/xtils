#include <sys/wait.h>
#include <unistd.h>

#include <future>
#include <string>

#include "net_test_server.h"
#include "xtils/net/http_client.h"
#include "xtils/net/http_server.h"
#include "xtils/net/tcp_client.h"
#include "xtils/tasks/thread_task_runner.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

using namespace xtils;
using namespace std::chrono_literals;

TEST_CASE("TCP: cross-runner connects never wait on the destination runner") {
  // Isolate the old deadlock so a regression cannot strand the test runner.
  pid_t child = fork();
  REQUIRE(child >= 0);
  if (child == 0) {
    struct Listener : TcpClientEventListener {
      void OnConnected(bool) override {}
      void OnDataReceived(const void*, size_t) override {}
      void OnDisconnected() override {}
    } la, lb;
    auto a = ThreadTaskRunner::CreateAndStart("cross_a");
    auto b = ThreadTaskRunner::CreateAndStart("cross_b");
    TcpClient ca(&a, &la), cb(&b, &lb);
    std::promise<void> entered_a, entered_b, done_a, done_b;
    auto ready_a = entered_a.get_future().share();
    auto ready_b = entered_b.get_future().share();
    a.PostTask([&] {
      entered_a.set_value();
      ready_b.wait();
      cb.Connect("127.0.0.1", 1);
      done_a.set_value();
    });
    b.PostTask([&] {
      entered_b.set_value();
      ready_a.wait();
      ca.Connect("127.0.0.1", 1);
      done_b.set_value();
    });
    bool ok = done_a.get_future().wait_for(1s) == std::future_status::ready;
    ok = (done_b.get_future().wait_for(1s) == std::future_status::ready) && ok;
    _exit(ok ? 0 : 1);
  }
  int status = 0;
  REQUIRE(waitpid(child, &status, 0) == child);
  CHECK(WIFEXITED(status));
  CHECK(WEXITSTATUS(status) == 0);
}

TEST_CASE("HTTP: POST body can exceed the transport queue capacity") {
  const std::string body(12 * 1024 * 1024, 'p');
  std::promise<std::string> received;
  NetTestServer server([&](int fd) {
    auto headers = NetTestServer::ReadHeaders(fd);
    std::string bytes;
    char buffer[65536];
    while (!headers.empty() && bytes.size() < body.size()) {
      auto n = recv(fd, buffer, sizeof(buffer), 0);
      if (n <= 0) break;
      bytes.append(buffer, static_cast<size_t>(n));
    }
    received.set_value(std::move(bytes));
    NetTestServer::Reply(fd, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
  });
  auto runner = ThreadTaskRunner::CreateAndStart("large_post");
  HttpClient client(&runner);
  client.SetTimeout(6000);
  auto response = client.Post(server.Url(), body);
  CHECK(response.status_code == 200);
  CHECK(received.get_future().get().compare(body) == 0);
}

TEST_CASE("HTTP: in-memory response can exceed the transport queue capacity") {
  bool pipeline = false;
  SUBCASE("single response") {}
  SUBCASE("pipelined responses preserve ordering") { pipeline = true; }
  struct Handler : HttpRequestHandler {
    void OnHttpRequest(const HttpServerRequest& request) override {
      request.conn->SendResponse("200 OK", {},
                                 std::string(12 * 1024 * 1024, 'r'));
    }
  } handler;
  auto server_runner = ThreadTaskRunner::CreateAndStart("large_response");
  HttpServer server(&server_runner, &handler);
  std::promise<int> started;
  server_runner.PostTask([&] {
    for (int port = 19880; port < 19900; ++port) {
      if (server.Start("127.0.0.1", port)) {
        started.set_value(port);
        return;
      }
    }
    started.set_value(0);
  });
  int port = started.get_future().get();
  REQUIRE(port != 0);
  auto runner = ThreadTaskRunner::CreateAndStart("response_client");
  HttpClient client(&runner);
  if (!pipeline) {
    auto response = client.Get("http://127.0.0.1:" + std::to_string(port));
    CHECK(response.status_code == 200);
    CHECK(response.body.size() == 12 * 1024 * 1024);
    CHECK(response.body.find_first_not_of('r') == std::string::npos);
  } else {
    ScopedFile fd(socket(AF_INET, SOCK_STREAM, 0));
    REQUIRE(fd);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    REQUIRE(connect(fd.get(), reinterpret_cast<sockaddr*>(&address),
                    sizeof(address)) == 0);
    timeval timeout{5, 0};
    setsockopt(fd.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    NetTestServer::Reply(
        fd.get(),
        "GET /first HTTP/1.1\r\nHost: localhost\r\n\r\n"
        "GET /second HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
    std::string bytes;
    char buffer[65536];
    ssize_t n;
    while ((n = recv(fd.get(), buffer, sizeof(buffer), 0)) > 0)
      bytes.append(buffer, n);
    for (int i = 0; i < 2; ++i) {
      auto end = bytes.find("\r\n\r\n");
      CHECK(bytes.find("HTTP/1.1 200 OK") == 0);
      if (end == std::string::npos ||
          bytes.size() < end + 4 + 12 * 1024 * 1024) {
        FAIL_CHECK("missing pipelined response body");
        break;
      }
      CHECK(bytes.substr(end + 4, 12 * 1024 * 1024) ==
            std::string(12 * 1024 * 1024, 'r'));
      bytes.erase(0, end + 4 + 12 * 1024 * 1024);
    }
    CHECK(bytes.empty());
  }
  std::promise<void> stopped;
  server_runner.PostTask([&] {
    server.Stop();
    stopped.set_value();
  });
  stopped.get_future().wait();
}

TEST_CASE("HTTP: EOF is not completion of an incomplete response") {
  std::string reply;
  SUBCASE("fixed length") {
    reply = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nabc";
  }
  SUBCASE("chunked missing terminator") {
    reply = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n";
  }
  SUBCASE("chunked incomplete trailers") {
    reply =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nX: y\r\n";
  }
  SUBCASE("incomplete headers") { reply = "HTTP/1.1 200 OK\r\nContent-Len"; }
  SUBCASE("no response") { reply = ""; }
  NetTestServer server([&](int fd) {
    NetTestServer::ReadHeaders(fd);
    NetTestServer::Reply(fd, reply);
  });
  auto runner = ThreadTaskRunner::CreateAndStart("incomplete_response");
  HttpClient client(&runner);
  client.SetTimeout(500);
  auto response = client.Get(server.Url());
  CHECK(response.status_code == 0);
  CHECK(response.status_message.find("EOF") != std::string::npos);
}

TEST_CASE("HTTP: response framing is incremental and bounded") {
  std::string reply;
  std::string expected = "abc";
  bool valid = true;
  size_t limit = 1024;
  SUBCASE("informational response") {
    reply =
        "HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: "
        "3\r\n\r\nabc";
  }
  SUBCASE("fixed length excludes trailing bytes") {
    reply = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabcEXTRA";
  }
  SUBCASE("chunk extensions and trailers") {
    reply =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: "
        "Chunked\r\n\r\n3;x=y\r\nabc\r\n0\r\nX-Test: yes\r\n\r\n";
  }
  SUBCASE("bad chunk terminator") {
    valid = false;
    reply =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: "
        "chunked\r\n\r\n3\r\nabcXX0\r\n\r\n";
  }
  SUBCASE("chunk size cannot narrow to zero") {
    valid = false;
    reply =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: "
        "chunked\r\n\r\n100000000\r\n\r\n";
  }
  SUBCASE("close-delimited body respects limit") {
    valid = false;
    limit = 2;
    reply = "HTTP/1.1 200 OK\r\n\r\nabc";
  }
  SUBCASE("chunked body respects cumulative limit") {
    valid = false;
    limit = 2;
    reply =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: "
        "chunked\r\n\r\n2\r\nab\r\n1\r\nc\r\n0\r\n\r\n";
  }
  NetTestServer server([&](int fd) {
    NetTestServer::ReadHeaders(fd);
    NetTestServer::Reply(fd, reply);
  });
  auto runner = ThreadTaskRunner::CreateAndStart("response_framing");
  HttpClient client(&runner);
  client.SetMaxReceiveBufferSize(limit);
  auto response = client.Get(server.Url());
  CHECK(response.status_code == (valid ? 200 : 0));
  if (valid) CHECK(response.body == expected);
}
