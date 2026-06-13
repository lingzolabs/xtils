#include "xtils/net/ipc_channel.h"

#include <chrono>
#include <string>
#include <thread>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

using namespace xtils;

static const std::string kTestSocket = "/tmp/xtils_ipc_test.sock";

class IpcFixture {
 public:
  IpcFixture() : server_(kTestSocket), client_(kTestSocket) {}
  ~IpcFixture() {
    client_.Disconnect();
    server_.Stop();
  }

  void StartAndConnect() {
    REQUIRE(server_.Start());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    REQUIRE(client_.Connect());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  IpcServer server_;
  IpcClient client_;
};

TEST_CASE_FIXTURE(IpcFixture, "IPC: server start and stop") {
  CHECK(server_.Start());
  server_.Stop();
}

TEST_CASE_FIXTURE(IpcFixture, "IPC: client connect and disconnect") {
  REQUIRE(server_.Start());
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  CHECK(client_.Connect());
  CHECK(client_.IsConnected());

  client_.Disconnect();
  CHECK(!client_.IsConnected());
}

TEST_CASE_FIXTURE(IpcFixture, "IPC: JSON-RPC 2.0 method call") {
  server_.Register("add", [](const Json& params) -> Result<Json> {
    auto a = params.get_integer("a");
    auto b = params.get_integer("b");
    if (!a || !b) return Err(jsonrpc::kInvalidParams, "missing a or b");
    Json result = Json::object();
    result["sum"] = Json(*a + *b);
    return result;
  });

  StartAndConnect();

  Json params = Json::object();
  params["a"] = Json(static_cast<int64_t>(3));
  params["b"] = Json(static_cast<int64_t>(4));

  auto result = client_.Call("add", params);
  REQUIRE(result.ok());
  CHECK(result->get_integer("sum").value_or(0) == 7);
}

TEST_CASE_FIXTURE(IpcFixture, "IPC: JSON-RPC 2.0 error response") {
  server_.Register("fail", [](const Json& params) -> Result<Json> {
    (void)params;
    return Err(-1, "intentional failure");
  });

  StartAndConnect();

  auto result = client_.Call("fail");
  CHECK(!result.ok());
  CHECK(result.error().code == -1);
  CHECK(result.error().message == "intentional failure");
}

TEST_CASE_FIXTURE(IpcFixture, "IPC: method not found") {
  StartAndConnect();

  auto result = client_.Call("nonexistent");
  CHECK(!result.ok());
  CHECK(result.error().code == jsonrpc::kMethodNotFound);
}

TEST_CASE_FIXTURE(IpcFixture, "IPC: client notification to server") {
  std::atomic<bool> received{false};
  std::string got_value;

  server_.OnNotify("ping", [&](const Json& params) {
    received = true;
    got_value = params.get_string("msg").value_or("");
  });

  StartAndConnect();

  Json params = Json::object();
  params["msg"] = Json(std::string("hello"));
  CHECK(client_.Notify("ping", params));

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(received);
  CHECK(got_value == "hello");
}

TEST_CASE_FIXTURE(IpcFixture, "IPC: server notification to client") {
  StartAndConnect();

  std::atomic<bool> received{false};
  std::string got_method;
  std::string got_data;

  auto sub = client_.OnNotify("status_update",
      [&](const std::string& method, const Json& params) {
        received = true;
        got_method = method;
        got_data = params.get_string("status").value_or("");
      });

  Json params = Json::object();
  params["status"] = Json(std::string("ready"));
  server_.Notify("status_update", params);

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(received);
  CHECK(got_method == "status_update");
  CHECK(got_data == "ready");
}

TEST_CASE_FIXTURE(IpcFixture, "IPC: global notification handler") {
  StartAndConnect();

  std::atomic<int> count{0};
  auto sub = client_.OnNotify(
      [&](const std::string& method, const Json& params) {
        (void)params;
        (void)method;
        ++count;
      });

  server_.Notify("event_a");
  server_.Notify("event_b");

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(count == 2);
}

TEST_CASE_FIXTURE(IpcFixture, "IPC: multiple clients") {
  server_.Register("echo", [](const Json& params) -> Result<Json> {
    return params;
  });

  REQUIRE(server_.Start());
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  IpcClient client2(kTestSocket);
  REQUIRE(client_.Connect());
  REQUIRE(client2.Connect());
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  CHECK(server_.ClientCount() == 2);

  Json p1 = Json::object();
  p1["id"] = Json(std::string("c1"));
  auto r1 = client_.Call("echo", p1);
  REQUIRE(r1.ok());
  CHECK(r1->get_string("id").value_or("") == "c1");

  Json p2 = Json::object();
  p2["id"] = Json(std::string("c2"));
  auto r2 = client2.Call("echo", p2);
  REQUIRE(r2.ok());
  CHECK(r2->get_string("id").value_or("") == "c2");

  client2.Disconnect();
}

TEST_CASE_FIXTURE(IpcFixture, "IPC: async call") {
  server_.Register("slow", [](const Json& params) -> Result<Json> {
    (void)params;
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    Json r = Json::object();
    r["done"] = Json(true);
    return r;
  });

  StartAndConnect();

  std::atomic<bool> got_result{false};
  client_.CallAsync("slow", Json::object(), [&](Result<Json> r) {
    CHECK(r.ok());
    CHECK(r->get_bool("done").value_or(false) == true);
    got_result = true;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  CHECK(got_result);
}

TEST_CASE_FIXTURE(IpcFixture, "IPC: call timeout") {
  server_.Register("hang", [](const Json& params) -> Result<Json> {
    (void)params;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    return Json(nullptr);
  });

  StartAndConnect();

  auto result = client_.Call("hang", Json::object(), 50);
  CHECK(!result.ok());
  CHECK(result.error().message == "timeout");
}
