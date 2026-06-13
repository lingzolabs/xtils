// WebSocket server example built on HttpServer.
//
// Connect with:
//   websocat ws://127.0.0.1:8090/ws

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "xtils/net/http_server.h"
#include "xtils/system/signal_handler.h"
#include "xtils/tasks/thread_task_runner.h"

using namespace xtils;

class WebSocketEchoHandler : public HttpRequestHandler {
 public:
  void OnHttpRequest(const HttpServer::Request& req) override {
    if (req.is_websocket_handshake && std::string(req.uri) == "/ws") {
      req.conn->UpgradeToWebsocket(req);
      clients_.push_back(req.conn);
      return;
    }

    HttpHeaders headers{{"Content-Type", "text/plain; charset=utf-8"}};
    req.conn->SendResponse("200 OK", headers,
                           "xtils websocket server: connect to /ws\n");
  }

  void OnWebsocketMessage(const WebsocketMessage& msg) override {
    std::string text(msg.data.data(), msg.data.size());
    std::string echo = "echo: " + text;
    msg.conn->SendWebsocketMessageText(echo.data(), echo.size());

    std::string broadcast = "broadcast: " + text;
    for (auto* client : clients_) {
      if (client && client->is_websocket() && client != msg.conn) {
        client->SendWebsocketMessageText(broadcast.data(), broadcast.size());
      }
    }
  }

  void OnHttpConnectionClosed(HttpServerConnection* conn) override {
    clients_.erase(std::remove(clients_.begin(), clients_.end(), conn),
                   clients_.end());
  }

 private:
  std::vector<HttpServerConnection*> clients_;
};

int main() {
  system::SignalHandler::Initialize();

  auto runner = ThreadTaskRunner::CreateAndStart("websocket_server");
  WebSocketEchoHandler handler;
  HttpServer server(&runner, &handler);
  server.AddAllowedOrigin("*");

  constexpr int kPort = 8090;
  if (!server.Start("0.0.0.0", kPort)) {
    printf("failed to start websocket server\n");
    return 1;
  }

  printf("WebSocket server listening on ws://127.0.0.1:%d/ws\n", kPort);
  printf("Press Ctrl+C to stop.\n");
  while (!system::SignalHandler::IsShutdownRequested()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  runner.PostTask([&server]() { server.Stop(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  return 0;
}
