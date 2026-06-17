#include "xtils/net/http_client_pool.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "xtils/net/http_server.h"
#include "xtils/tasks/thread_task_runner.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

using namespace xtils;
using namespace std::chrono_literals;

namespace {

class EchoHandler : public HttpRequestHandler {
 public:
  void OnHttpRequest(const HttpServer::Request& req) override {
    counter.fetch_add(1, std::memory_order_relaxed);
    req.conn->SendResponse("200 OK", {{"Content-Type", "text/plain"}}, "ok");
  }
  std::atomic<int> counter{0};
};

class SlowHandler : public HttpRequestHandler {
 public:
  void OnHttpRequest(const HttpServer::Request& req) override {
    std::this_thread::sleep_for(120ms);
    req.conn->SendResponse("200 OK", {{"Content-Type", "text/plain"}}, "ok");
  }
};

}  // namespace

TEST_CASE("HttpClientPool: sequential Send with size 1") {
  auto srv_runner = ThreadTaskRunner::CreateAndStart("pool-test-srv-1");
  EchoHandler handler;
  HttpServer server(&srv_runner, &handler);
  REQUIRE(server.Start("127.0.0.1", 19200));

  auto cli_runner = ThreadTaskRunner::CreateAndStart("pool-test-cli-1");
  HttpClientPool pool(&cli_runner, 1);
  CHECK(pool.Size() == 1);

  for (int i = 0; i < 5; ++i) {
    HttpClient::Request req;
    req.url = HttpUrl("http://127.0.0.1:19200/echo");
    auto resp = pool.Send(req);
    CHECK(resp.status_code == 200);
  }
  CHECK(handler.counter.load() == 5);
}

TEST_CASE("HttpClientPool: concurrent Send with size N") {
  auto srv_runner = ThreadTaskRunner::CreateAndStart("pool-test-srv-2");
  EchoHandler handler;
  HttpServer server(&srv_runner, &handler);
  REQUIRE(server.Start("127.0.0.1", 19201));

  auto cli_runner = ThreadTaskRunner::CreateAndStart("pool-test-cli-2");
  constexpr int kPoolSize = 4;
  constexpr int kRequests = 10;
  HttpClientPool pool(&cli_runner, kPoolSize);
  CHECK(pool.Size() == kPoolSize);

  std::vector<std::thread> threads;
  std::atomic<int> ok{0};
  for (int i = 0; i < kRequests; ++i) {
    threads.emplace_back([&]() {
      HttpClient::Request req;
      req.url = HttpUrl("http://127.0.0.1:19201/echo");
      auto resp = pool.Send(req);
      if (resp.status_code == 200) ok.fetch_add(1);
    });
  }
  for (auto& t : threads) t.join();
  CHECK(ok.load() == kRequests);
  CHECK(handler.counter.load() == kRequests);
  // After all requests complete, the pool should be fully replenished.
  CHECK(pool.AvailableForTesting() == kPoolSize);
}

TEST_CASE("HttpClientPool: Acquire times out when fully saturated") {
  auto srv_runner = ThreadTaskRunner::CreateAndStart("pool-test-srv-3");
  SlowHandler handler;
  HttpServer server(&srv_runner, &handler);
  REQUIRE(server.Start("127.0.0.1", 19202));

  auto cli_runner = ThreadTaskRunner::CreateAndStart("pool-test-cli-3");
  HttpClientPool pool(&cli_runner, 1);

  // Saturate the only client with a slow request running on a background
  // thread, then try to acquire from this thread with a short timeout.
  std::atomic<bool> done{false};
  std::thread bg([&]() {
    HttpClient::Request req;
    req.url = HttpUrl("http://127.0.0.1:19202/slow");
    pool.Send(req);
    done = true;
  });

  // Give the bg thread time to start its request.
  std::this_thread::sleep_for(20ms);

  HttpClient::Request req2;
  req2.url = HttpUrl("http://127.0.0.1:19202/slow");
  auto resp = pool.Send(req2, 30ms);  // very short acquire timeout
  CHECK(resp.status_code == 0);
  CHECK(resp.status_message.find("timed out") != std::string::npos);

  bg.join();
  CHECK(done.load());
}

TEST_CASE("HttpClientPool: Acquire returns RAII handle that auto-releases") {
  auto cli_runner = ThreadTaskRunner::CreateAndStart("pool-test-cli-4");
  HttpClientPool pool(&cli_runner, 2);
  CHECK(pool.AvailableForTesting() == 2);
  {
    auto h1 = pool.Acquire(100ms);
    CHECK(h1);
    CHECK(pool.AvailableForTesting() == 1);
    {
      auto h2 = pool.Acquire(100ms);
      CHECK(h2);
      CHECK(pool.AvailableForTesting() == 0);
    }
    CHECK(pool.AvailableForTesting() == 1);
  }
  CHECK(pool.AvailableForTesting() == 2);
}
