#include "xtils/net/http_client.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "xtils/net/http_server.h"
#include "xtils/tasks/thread_task_runner.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

using namespace xtils;
using namespace std::chrono_literals;

// ============================================================================
// HttpUrl tests
// ============================================================================

TEST_CASE("HttpUrl: parse HTTP URL") {
  HttpUrl url("http://example.com:8080/api/test?q=hello#section");

  CHECK(url.scheme == "http");
  CHECK(url.host == "example.com");
  CHECK(url.port == 8080);
  CHECK(url.path == "/api/test");
  CHECK(url.query == "q=hello");
  CHECK(url.fragment == "section");
  CHECK(url.IsValid());
  CHECK_FALSE(url.IsHttps());
}

TEST_CASE("HttpUrl: parse HTTPS URL with default port") {
  HttpUrl url("https://secure.example.com/login");

  CHECK(url.scheme == "https");
  CHECK(url.host == "secure.example.com");
  CHECK(url.path == "/login");
  CHECK(url.IsHttps());
  CHECK(url.IsValid());
  CHECK(url.GetDefaultPort() == 443);
}

TEST_CASE("HttpUrl: parse HTTP URL default port") {
  HttpUrl url("http://localhost/");

  CHECK(url.scheme == "http");
  CHECK(url.host == "localhost");
  CHECK(url.GetDefaultPort() == 80);
  CHECK_FALSE(url.IsHttps());
}

TEST_CASE("HttpUrl: parse WebSocket URL") {
  HttpUrl url("wss://ws.example.com/socket");

  CHECK(url.scheme == "wss");
  CHECK(url.host == "ws.example.com");
  CHECK(url.path == "/socket");
  CHECK(url.IsHttps());  // wss is secure
}

TEST_CASE("HttpUrl: empty/invalid URL") {
  HttpUrl url("");
  CHECK_FALSE(url.IsValid());

  HttpUrl url2("not-a-url");
  CHECK_FALSE(url2.IsValid());

  HttpUrl url3("http://localhost:notaport/");
  CHECK_FALSE(url3.IsValid());

  HttpUrl url4("http://localhost:70000/");
  CHECK_FALSE(url4.IsValid());
}

TEST_CASE("HttpUrl: base() strips path and query") {
  HttpUrl url("http://example.com:9090/api/data?key=val");
  HttpUrl base = url.base();

  CHECK(base.host == "example.com");
  CHECK(base.port == 9090);
  CHECK(base.path == "/");
  CHECK(base.query.empty());
  CHECK(base.fragment.empty());
}

TEST_CASE("HttpUrl: isSameHost") {
  HttpUrl url1("http://example.com:8080/a");
  HttpUrl url2("http://example.com:8080/b");
  HttpUrl url3("http://example.com:9090/a");

  CHECK(url1.isSameHost(url2));
  CHECK_FALSE(url1.isSameHost(url3));
}

TEST_CASE("HttpUrl: ToString") {
  HttpUrl url("http://example.com:8080/api?q=1");
  std::string s = url.ToString();
  CHECK(s.find("example.com") != std::string::npos);
  CHECK(s.find("8080") != std::string::npos);
}

// ============================================================================
// HttpClient::Request struct tests
// ============================================================================

TEST_CASE("HttpClient::Request: set headers") {
  xtils::HttpClient::Request req;
  req.AddHeader("X-Custom", "value1");
  req.SetContentType("application/json");
  req.SetUserAgent("test-agent/1.0");
  req.SetAuthorization("Bearer token123");

  bool found_custom = false;
  bool found_ct = false;
  bool found_ua = false;
  bool found_auth = false;
  for (const auto& h : req.headers) {
    if (h.name == "X-Custom" && h.value == "value1") found_custom = true;
    if (h.name == "Content-Type" && h.value == "application/json")
      found_ct = true;
    if (h.name == "User-Agent" && h.value == "test-agent/1.0") found_ua = true;
    if (h.name == "Authorization" && h.value == "Bearer token123")
      found_auth = true;
  }
  CHECK(found_custom);
  CHECK(found_ct);
  CHECK(found_ua);
  CHECK(found_auth);
}

TEST_CASE("HttpClient::Request: set JSON body") {
  xtils::HttpClient::Request req;
  req.SetJsonBody(R"({"key":"value"})");

  CHECK(req.body == R"({"key":"value"})");
  bool found_ct = false;
  for (const auto& h : req.headers) {
    if (h.name == "Content-Type" && h.value == "application/json")
      found_ct = true;
  }
  CHECK(found_ct);
}

TEST_CASE("HttpClient::Request: set form body") {
  xtils::HttpClient::Request req;
  req.SetFormBody({{"name", "alice"}, {"age", "30"}});

  CHECK_FALSE(req.body.empty());
  // Form body should contain the key=value pairs
  CHECK(req.body.find("name=alice") != std::string::npos);
  CHECK(req.body.find("age=30") != std::string::npos);
}

TEST_CASE("HttpClient::Request: multipart body") {
  xtils::HttpClient::Request req;
  std::vector<HttpClient::MultipartField> fields = {{"field1", "value1"}};
  std::vector<HttpClient::MultipartFile> files;  // No files for this test

  req.SetMultipartBody(fields, files);
  CHECK(req.is_multipart());
  CHECK(req.multipart_fields.size() == 1);
  CHECK(req.multipart_fields[0].name == "field1");
  CHECK(req.multipart_fields[0].value == "value1");
}

TEST_CASE("HttpClient: sync invalid URL returns without deadlock") {
  auto task_runner = ThreadTaskRunner::CreateAndStart("http_client_invalid");
  HttpClient client(&task_runner);

  xtils::HttpClient::Request req;
  auto resp = client.Send(req);

  CHECK(resp.status_code == 0);
  CHECK(resp.status_message == "Invalid URL");
}

TEST_CASE("HttpClient: busy Send does not reset in-flight async request") {
  class SlowHandler : public HttpRequestHandler {
   public:
    void OnHttpRequest(const HttpServer::Request& req) override {
      std::this_thread::sleep_for(100ms);
      req.conn->SendResponse("200 OK", {{"Content-Type", "text/plain"}},
                             "done");
    }
  };

  class Listener : public HttpClient::Listener {
   public:
    void OnHttpResponse(HttpClient*,
                        const HttpClient::Response& response) override {
      status = response.status_code;
      body = response.body;
      done = true;
    }
    void OnHttpError(HttpClient*, const std::string& error) override {
      status = 0;
      body = error;
      done = true;
    }

    std::atomic<bool> done{false};
    int status = -1;
    std::string body;
  };

  auto server_runner = ThreadTaskRunner::CreateAndStart("http_client_busy_srv");
  SlowHandler handler;
  HttpServer server(&server_runner, &handler);
  REQUIRE(server.Start("127.0.0.1", 19090));

  auto client_runner = ThreadTaskRunner::CreateAndStart("http_client_busy_cli");
  HttpClient client(&client_runner);
  Listener listener;

  REQUIRE(client.GetAsync("http://127.0.0.1:19090/slow", &listener));

  HttpClient::Request second;
  second.url = HttpUrl("http://127.0.0.1:19090/second");
  auto busy = client.Send(second);
  CHECK(busy.status_code == 0);
  CHECK(busy.status_message == "Client is busy");

  for (int i = 0; i < 50 && !listener.done.load(); ++i) {
    std::this_thread::sleep_for(20ms);
  }

  CHECK(listener.done.load());
  CHECK(listener.status == 200);
  CHECK(listener.body == "done");

  server_runner.PostTask([&server]() { server.Stop(); });
  std::this_thread::sleep_for(50ms);
}

// ============================================================================
// HttpClient::Response struct tests
// ============================================================================

TEST_CASE("HttpClient::Response: status helpers") {
  xtils::HttpClient::Response resp;

  resp.status_code = 200;
  CHECK(resp.IsSuccessful());
  CHECK_FALSE(resp.IsRedirect());
  CHECK_FALSE(resp.IsError());

  resp.status_code = 301;
  CHECK_FALSE(resp.IsSuccessful());
  CHECK(resp.IsRedirect());
  CHECK_FALSE(resp.IsError());

  resp.status_code = 404;
  CHECK_FALSE(resp.IsSuccessful());
  CHECK_FALSE(resp.IsRedirect());
  CHECK(resp.IsError());

  resp.status_code = 500;
  CHECK(resp.IsError());
}

TEST_CASE("HttpClient::Response: header access") {
  xtils::HttpClient::Response resp;
  resp.headers.push_back({"Content-Type", "application/json"});
  resp.headers.push_back({"X-Request-Id", "abc123"});

  CHECK(resp.GetHeader("Content-Type") == "application/json");
  CHECK(resp.GetHeader("X-Request-Id") == "abc123");
  CHECK(resp.HasHeader("Content-Type"));
  CHECK_FALSE(resp.HasHeader("Missing-Header"));
}

// ============================================================================
// HttpUtils tests
// ============================================================================

TEST_CASE("HttpUtils: method conversion") {
  CHECK(HttpUtils::HttpMethodToString(HttpMethod::kGet) == "GET");
  CHECK(HttpUtils::HttpMethodToString(HttpMethod::kPost) == "POST");
  CHECK(HttpUtils::HttpMethodToString(HttpMethod::kPut) == "PUT");
  CHECK(HttpUtils::HttpMethodToString(HttpMethod::kDelete) == "DELETE");
  CHECK(HttpUtils::HttpMethodToString(HttpMethod::kPatch) == "PATCH");

  CHECK(HttpUtils::StringToHttpMethod("GET") == HttpMethod::kGet);
  CHECK(HttpUtils::StringToHttpMethod("POST") == HttpMethod::kPost);
  CHECK(HttpUtils::StringToHttpMethod("PUT") == HttpMethod::kPut);
  CHECK(HttpUtils::StringToHttpMethod("DELETE") == HttpMethod::kDelete);
}

TEST_CASE("HttpUtils: URL encode/decode") {
  std::string encoded = HttpUtils::UrlEncode("hello world&foo=bar");
  CHECK(encoded.find(' ') == std::string::npos);
  CHECK(encoded.find("hello") != std::string::npos);

  std::string decoded = HttpUtils::UrlDecode(encoded);
  CHECK(decoded == "hello world&foo=bar");
}

TEST_CASE("HttpUtils: URL encode/decode roundtrip") {
  std::string original = "special chars: <>&\"'";
  std::string encoded = HttpUtils::UrlEncode(original);
  std::string decoded = HttpUtils::UrlDecode(encoded);
  CHECK(decoded == original);
}

TEST_CASE("HttpUtils: form data encode/parse") {
  std::map<std::string, std::string> data = {{"name", "alice"},
                                             {"city", "new york"}};

  std::string encoded = HttpUtils::FormDataEncode(data);
  CHECK_FALSE(encoded.empty());

  auto parsed = HttpUtils::ParseFormData(encoded);
  CHECK(parsed["name"] == "alice");
  CHECK(parsed["city"] == "new york");
}

TEST_CASE("HttpUtils: HTML escape") {
  std::string text = "<script>alert('xss')</script>";
  std::string escaped = HttpUtils::EscapeHtml(text);
  CHECK(escaped.find('<') == std::string::npos);
  CHECK(escaped.find('>') == std::string::npos);
  CHECK(escaped.find("&lt;") != std::string::npos);
  CHECK(escaped.find("&gt;") != std::string::npos);
}

TEST_CASE("HttpUtils: status helpers") {
  CHECK(HttpUtils::IsSuccessStatus(200));
  CHECK(HttpUtils::IsSuccessStatus(201));
  CHECK_FALSE(HttpUtils::IsSuccessStatus(400));

  CHECK(HttpUtils::IsRedirectStatus(301));
  CHECK(HttpUtils::IsRedirectStatus(302));
  CHECK_FALSE(HttpUtils::IsRedirectStatus(200));

  CHECK(HttpUtils::IsErrorStatus(400));
  CHECK(HttpUtils::IsErrorStatus(500));
  CHECK_FALSE(HttpUtils::IsErrorStatus(200));
}

TEST_CASE("HttpUtils: MIME type detection") {
  CHECK(HttpUtils::GetMimeType(".html").find("text/html") != std::string::npos);
  CHECK(HttpUtils::GetMimeType(".json").find("json") != std::string::npos);
  CHECK(HttpUtils::GetMimeType(".css").find("css") != std::string::npos);
  CHECK(HttpUtils::GetMimeType(".js").find("javascript") != std::string::npos);
  CHECK(HttpUtils::GetMimeType(".png").find("image") != std::string::npos);
}

TEST_CASE("HttpUtils: method validation") {
  CHECK(HttpUtils::IsValidHttpMethod("GET"));
  CHECK(HttpUtils::IsValidHttpMethod("POST"));
  CHECK(HttpUtils::IsValidHttpMethod("PUT"));
  CHECK(HttpUtils::IsValidHttpMethod("DELETE"));
  CHECK_FALSE(HttpUtils::IsValidHttpMethod("INVALID"));
  CHECK_FALSE(HttpUtils::IsValidHttpMethod(""));
}

TEST_CASE("HttpUtils: status message") {
  CHECK(HttpUtils::GetStatusMessage(200).find("OK") != std::string::npos);
  CHECK(HttpUtils::GetStatusMessage(404).find("Not Found") !=
        std::string::npos);
  CHECK(HttpUtils::GetStatusMessage(500).find("Internal") != std::string::npos);
}
