// Advanced HTTP client example.
//
// Demonstrates the scoped API names (`HttpClient::Request/Response/Listener`),
// default headers, cookies, redirects, SSL verification configuration, and the
// single-flight busy contract.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "xtils/net/http_client.h"
#include "xtils/tasks/thread_task_runner.h"

using namespace xtils;

class PrintListener : public HttpClient::Listener {
 public:
  void OnHttpResponse(HttpClient*,
                      const HttpClient::Response& response) override {
    printf("async response: %d %s (%zu bytes)\n", response.status_code,
           response.status_message.c_str(), response.body.size());
    done = true;
  }

  void OnHttpError(HttpClient*, const std::string& error) override {
    printf("async error: %s\n", error.c_str());
    done = true;
  }

  bool OnBodyData(HttpClient*, const void* data, size_t len) override {
    printf("streamed %zu bytes\n", len);
    return true;  // also accumulate in Response::body
  }

  std::atomic<bool> done{false};
};

int main(int argc, char** argv) {
  std::string url = argc > 1 ? argv[1] : "http://httpbin.org/anything";

  auto runner = ThreadTaskRunner::CreateAndStart("http_client_advanced");
  HttpClient client(&runner);
  client.SetUserAgent("xtils-http-client-advanced/1.0");
  client.AddDefaultHeader("X-Example", "advanced-client");
  client.SetFollowRedirects(true, 3);
  client.SetCookie("session", "demo", "httpbin.org");
  client.SetVerifySSL(true);

  HttpClient::Request req;
  req.method = HttpMethod::kPost;
  req.url = HttpUrl(url);
  req.SetJsonBody(R"({"hello":"xtils"})");

  HttpClient::Response response = client.Send(req);
  printf("sync response: %d %s\n", response.status_code,
         response.status_message.c_str());

  PrintListener listener;
  if (!client.GetAsync(url, &listener)) {
    printf("failed to start async request\n");
    return 1;
  }

  // A single HttpClient is single-flight; use another instance for parallelism.
  if (!client.GetAsync(url, &listener)) {
    printf("second async request rejected while first is busy (expected)\n");
  }

  for (int i = 0; i < 200 && !listener.done.load(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  if (!listener.done.load()) {
    printf("async request did not finish in time; cancelling\n");
    client.Cancel();
  }

  return 0;
}
