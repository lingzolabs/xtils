// IPC JSON-RPC 2.0 example.
//
// Starts an IpcServer and IpcClient in the same process, registers methods,
// performs sync/async calls, and sends notifications in both directions.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "xtils/net/ipc_channel.h"
#include "xtils/utils/json.h"

using namespace xtils;
using namespace std::chrono_literals;

int main() {
  const std::string socket_path = "/tmp/xtils_ipc_example.sock";

  IpcServer server(socket_path);
  server.Register("add", [](const Json& params) -> Result<Json> {
    auto a = params.get_integer("a");
    auto b = params.get_integer("b");
    if (!a || !b) return Err(jsonrpc::kInvalidParams, "missing a or b");

    Json result = Json::object();
    result["sum"] = Json(*a + *b);
    return result;
  });
  server.OnNotify("client.hello", [](const Json& params) {
    printf("server received notification: %s\n",
           params.get_string("msg").value_or("").c_str());
  });

  if (!server.Start()) {
    printf("failed to start IPC server\n");
    return 1;
  }

  IpcClient client(socket_path);
  if (!client.Connect()) {
    printf("failed to connect IPC client\n");
    return 1;
  }

  auto sub = client.OnNotify(
      "server.ready", [](const std::string& method, const Json& params) {
        printf("client got %s: %s\n", method.c_str(), params.dump().c_str());
      });

  Json params = Json::object();
  params["a"] = Json(static_cast<int64_t>(7));
  params["b"] = Json(static_cast<int64_t>(35));
  auto result = client.Call("add", params);
  if (result.ok()) {
    printf("7 + 35 = %lld\n",
           static_cast<long long>(result->get_integer("sum").value_or(0)));
  }

  Json hello = Json::object();
  hello["msg"] = Json(std::string("hello from client"));
  client.Notify("client.hello", hello);

  std::atomic<bool> async_done{false};
  client.CallAsync("add", params, [&](Result<Json> r) {
    printf("async add ok=%d\n", r.ok() ? 1 : 0);
    async_done = true;
  });

  Json ready = Json::object();
  ready["status"] = Json(std::string("ready"));
  server.Notify("server.ready", ready);

  for (int i = 0; i < 20 && !async_done.load(); ++i) {
    std::this_thread::sleep_for(10ms);
  }

  client.Disconnect();
  server.Stop();
  return 0;
}
