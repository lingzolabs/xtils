#include <atomic>
#include <fstream>
#include <future>

#include "net_test_server.h"
#include "xtils/net/http_client.h"
#include "xtils/net/http_server.h"
#include "xtils/tasks/thread_task_runner.h"
#include "xtils/utils/scoped.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

using namespace xtils;
using namespace std::chrono_literals;

namespace {
struct Listener : HttpClient::Listener {
  void OnHttpResponse(HttpClient*,
                      const HttpClient::Response& response) override {
    if (++terminals == 1) result.set_value(response);
  }
  void OnHttpError(HttpClient*, const std::string& error) override {
    HttpClient::Response response;
    response.status_message = error;
    if (++terminals == 1) result.set_value(response);
  }
  std::atomic<int> terminals{0};
  std::promise<HttpClient::Response> result;
};
void Drain(int fd) {
  char buffer[65536];
  while (recv(fd, buffer, sizeof(buffer), 0) > 0) {
  }
}
}  // namespace

TEST_CASE(
    "HTTP lifecycle: synchronous calls on the owner runner fail promptly") {
  auto runner = ThreadTaskRunner::CreateAndStart("http_sync_owner");
  HttpClient client(&runner);
  std::promise<HttpClient::Response> response;
  runner.PostTask(
      [&] { response.set_value(client.Get("http://127.0.0.1:1/")); });
  auto result = response.get_future();
  REQUIRE(result.wait_for(1s) == std::future_status::ready);
  CHECK(result.get().status_message.find("owner runner") != std::string::npos);
}

TEST_CASE("HTTP lifecycle: synchronous timeout works while runner is stalled") {
  auto runner = ThreadTaskRunner::CreateAndStart("http_stalled_runner");
  std::promise<void> entered, release;
  auto resume = release.get_future().share();
  runner.PostTask([&] {
    entered.set_value();
    resume.wait();
  });
  entered.get_future().wait();
  HttpClient client(&runner);
  client.SetTimeout(50);
  auto result = std::async(std::launch::async,
                           [&] { return client.Get("http://127.0.0.1:1/"); });
  bool ready = result.wait_for(500ms) == std::future_status::ready;
  release.set_value();  // Never strand the runner, even on RED.
  CHECK(ready);
  auto response = result.get();
  if (ready) CHECK(response.status_message == "Request timeout");
}

TEST_CASE(
    "HTTP server: oversized in-memory response closes instead of queuing") {
  auto [raw, peer] =
      UnixSocketRaw::CreatePairPosix(SockFamily::kUnix, SockType::kStream);
  auto runner = ThreadTaskRunner::CreateAndStart("http_output_limit");
  struct Events : UnixSocket::EventListener {
    void OnDisconnect(UnixSocket*) override { disconnected.set_value(); }
    std::promise<void> disconnected;
  } events;
  std::unique_ptr<HttpServerConnection> conn;
  std::promise<void> started;
  runner.PostTask([&] {
    auto sock =
        UnixSocket::AdoptConnected(raw.ReleaseFd(), &events, &runner,
                                   SockFamily::kUnix, SockType::kStream);
    conn = std::make_unique<HttpServerConnection>(std::move(sock), 4096);
    conn->SendResponse("200 OK", {}, std::string(20 * 1024 * 1024, 'x'));
    started.set_value();
  });
  started.get_future().wait();
  auto disconnected = events.disconnected.get_future();
  bool ready = disconnected.wait_for(1s) == std::future_status::ready;
  CHECK(ready);
  std::promise<void> cleaned;
  runner.PostTask([&] {
    conn.reset();
    cleaned.set_value();
  });
  cleaned.get_future().wait();
}

TEST_CASE("HTTP lifecycle: cancellation before queued startup is not lost") {
  auto runner = ThreadTaskRunner::CreateAndStart("http_cancel_start");
  Listener listener;
  HttpClient client(&runner);
  runner.PostTask([&] {
    CHECK(client.GetAsync("http://127.0.0.1:1/", &listener));
    client.Cancel();
    client.Cancel();
  });
  auto result = listener.result.get_future();
  REQUIRE(result.wait_for(1s) == std::future_status::ready);
  CHECK(result.get().status_message == "Request cancelled");
  CHECK(listener.terminals == 1);
}

TEST_CASE(
    "HTTP lifecycle: cancellation from body or send progress completes once") {
  bool from_body = false;
  SUBCASE("send progress") {}
  SUBCASE("body callback") { from_body = true; }
  NetTestServer server([&](int fd) {
    NetTestServer::ReadHeaders(fd);
    if (from_body)
      NetTestServer::Reply(fd,
                           "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc");
    Drain(fd);
  });
  struct Canceller : Listener {
    void OnProgress(HttpClient* client, size_t, int64_t) override {
      if (!from_body) client->Cancel();
    }
    bool OnBodyData(HttpClient* client, const void*, size_t) override {
      if (from_body) client->Cancel();
      return true;
    }
    bool from_body = false;
  } listener;
  listener.from_body = from_body;
  auto runner = ThreadTaskRunner::CreateAndStart("http_cancel_callback");
  HttpClient client(&runner);
  HttpClient::Request request;
  request.url = HttpUrl(server.Url());
  if (!from_body) request.SetBody(std::string(12 * 1024 * 1024, 'x'));
  REQUIRE(client.SendAsync(request, &listener));
  auto result = listener.result.get_future();
  REQUIRE(result.wait_for(2s) == std::future_status::ready);
  CHECK(result.get().status_message == "Request cancelled");
  std::promise<void> drained;
  runner.PostTask([&] { drained.set_value(); });
  drained.get_future().wait();
  CHECK(listener.terminals == 1);
}

TEST_CASE("HTTP lifecycle: early response stops an unfinished upload") {
  std::promise<size_t> received;
  NetTestServer server([&](int fd) {
    NetTestServer::ReadHeaders(fd);
    NetTestServer::Reply(
        fd, "HTTP/1.1 413 Payload Too Large\r\nContent-Length: 0\r\n\r\n");
    size_t count = 0;
    char buffer[65536];
    ssize_t n;
    while ((n = recv(fd, buffer, sizeof(buffer), 0)) > 0) count += n;
    received.set_value(count);
  });
  auto runner = ThreadTaskRunner::CreateAndStart("http_early_response");
  HttpClient client(&runner);
  auto response = client.Post(server.Url(), std::string(32 * 1024 * 1024, 'x'));
  CHECK(response.status_code == 413);
  CHECK(received.get_future().get() < 32 * 1024 * 1024);
}

TEST_CASE("HTTP lifecycle: client can be destroyed from body callback") {
  NetTestServer server([](int fd) {
    NetTestServer::ReadHeaders(fd);
    NetTestServer::Reply(fd, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc");
  });
  auto runner = ThreadTaskRunner::CreateAndStart("http_body_destroy");
  std::unique_ptr<HttpClient> client = std::make_unique<HttpClient>(&runner);
  struct Destroyer : Listener {
    bool OnBodyData(HttpClient*, const void*, size_t) override {
      client->reset();
      destroyed.set_value();
      return false;
    }
    std::unique_ptr<HttpClient>* client = nullptr;
    std::promise<void> destroyed;
  } listener;
  listener.client = &client;
  REQUIRE(client->GetAsync(server.Url(), &listener));
  REQUIRE(listener.destroyed.get_future().wait_for(1s) ==
          std::future_status::ready);
  std::promise<void> drained;
  runner.PostTask([&] { drained.set_value(); });
  drained.get_future().wait();
  CHECK(listener.terminals == 0);
}

TEST_CASE(
    "HTTP lifecycle: completion can start a new request without stale "
    "timeouts") {
  NetTestServer first([](int fd) {
    NetTestServer::ReadHeaders(fd);
    NetTestServer::Reply(fd, "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\na");
  });
  std::promise<void> release;
  auto released = release.get_future();
  NetTestServer second([&](int fd) {
    NetTestServer::ReadHeaders(fd);
    released.wait();
    NetTestServer::Reply(fd, "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nb");
  });
  struct Chain : Listener {
    void OnHttpResponse(HttpClient* client,
                        const HttpClient::Response& response) override {
      if (++responses == 1) {
        HttpClient::Request request;
        request.url = HttpUrl(next);
        request.timeout_ms = 1000;
        CHECK(client->SendAsync(request, this));
      } else {
        Listener::OnHttpResponse(client, response);
      }
    }
    int responses = 0;
    std::string next;
  } listener;
  listener.next = second.Url();
  auto runner = ThreadTaskRunner::CreateAndStart("http_reentrant_completion");
  HttpClient client(&runner);
  HttpClient::Request request;
  request.url = HttpUrl(first.Url());
  request.timeout_ms = 100;
  REQUIRE(client.SendAsync(request, &listener));
  runner.PostDelayedTask([&] { release.set_value(); }, 200);
  auto result = listener.result.get_future();
  REQUIRE(result.wait_for(2s) == std::future_status::ready);
  CHECK(result.get().body == "b");
  CHECK(listener.responses == 2);
  CHECK(listener.terminals == 1);
}

TEST_CASE("HTTP lifecycle: no-body responses do not wait for EOF") {
  HttpClient::Request request;
  std::string headers;
  int expected = 200;
  SUBCASE("HEAD") {
    request.method = HttpMethod::kHead;
    headers = "HTTP/1.1 200 OK\r\nContent-Length: 999\r\n\r\n";
  }
  SUBCASE("204") {
    expected = 204;
    headers = "HTTP/1.1 204 No Content\r\n\r\n";
  }
  SUBCASE("304") {
    expected = 304;
    headers = "HTTP/1.1 304 Not Modified\r\n\r\n";
  }
  std::promise<void> release;
  auto released = release.get_future();
  NetTestServer server([&](int fd) {
    NetTestServer::ReadHeaders(fd);
    NetTestServer::Reply(fd, headers);
    released.wait();
  });
  auto runner = ThreadTaskRunner::CreateAndStart("http_no_body");
  Listener listener;
  HttpClient client(&runner);
  request.url = HttpUrl(server.Url());
  REQUIRE(client.SendAsync(request, &listener));
  auto result = listener.result.get_future();
  bool ready = result.wait_for(1s) == std::future_status::ready;
  release.set_value();
  REQUIRE(ready);
  CHECK(result.get().status_code == expected);
}

TEST_CASE("HTTP multipart: streamed files use the exact advertised length") {
  char path[] = "/tmp/xtils-multipart-XXXXXX";
  int fd = mkstemp(path);
  REQUIRE(fd >= 0);
  close(fd);
  Scoped remove([&] { unlink(path); });
  std::string payload;
  SUBCASE("large file") { payload.assign(12 * 1024 * 1024, 'f'); }
  SUBCASE("empty file"){} {
    std::ofstream file(path, std::ios::binary);
    file << payload;
  }
  std::promise<std::string> received;
  NetTestServer server([&](int socket) {
    auto headers = NetTestServer::ReadHeaders(socket);
    auto pos = headers.find("Content-Length: ");
    size_t length =
        pos == std::string::npos ? 0 : std::stoull(headers.substr(pos + 16));
    std::string body;
    char buffer[65536];
    while (body.size() < length) {
      auto n = recv(socket, buffer,
                    std::min(sizeof(buffer), length - body.size()), 0);
      if (n <= 0) break;
      body.append(buffer, n);
    }
    received.set_value(std::move(body));
    NetTestServer::Reply(socket,
                         "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
  });
  auto runner = ThreadTaskRunner::CreateAndStart("http_multipart");
  HttpClient client(&runner);
  HttpClient::Request request;
  request.method = HttpMethod::kPost;
  request.url = HttpUrl(server.Url());
  request.boundary = "test-boundary";
  request.SetMultipartBody(
      {{"field", "value"}},
      {{"file", "file.bin", "application/octet-stream", path}});
  auto response = client.Send(request);
  CHECK(response.status_code == 200);
  std::string expected =
      "--test-boundary\r\nContent-Disposition: form-data; "
      "name=\"field\"\r\n\r\nvalue\r\n";
  expected +=
      "--test-boundary\r\nContent-Disposition: form-data; name=\"file\"; "
      "filename=\"file.bin\"\r\nContent-Type: application/octet-stream\r\n\r\n";
  expected += payload + "\r\n--test-boundary--\r\n";
  CHECK(received.get_future().get() == expected);
}
