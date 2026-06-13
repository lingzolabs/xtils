// Advanced HTTP router example.
//
// Demonstrates middleware, query params, custom errors, CORS, file downloads,
// and multipart/form-data parsing.

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "xtils/logging/logger.h"
#include "xtils/net/http_router.h"
#include "xtils/net/http_server.h"
#include "xtils/system/signal_handler.h"
#include "xtils/tasks/thread_task_runner.h"

using namespace xtils;

int main() {
  system::SignalHandler::Initialize();
  auto runner = ThreadTaskRunner::CreateAndStart("http_router_advanced");

  auto router = std::make_unique<HttpRouter>();
  router->EnableCors("*", "GET,POST,PUT,PATCH,DELETE,OPTIONS");

  router->Use([](const HttpRequestContext& ctx,
                 HttpRouter::Response& res) -> bool {
    LogI("%.*s %.*s", static_cast<int>(ctx.request->method.size()),
         ctx.request->method.data(), static_cast<int>(ctx.request->uri.size()),
         ctx.request->uri.data());
    return true;
  });

  router->Use(
      "/api/private",
      [](const HttpRequestContext& ctx, HttpRouter::Response& res) -> bool {
        if (ctx.GetHeader("Authorization") != "Bearer secret") {
          res.Status(401).Json(R"({"error":"missing or invalid token"})");
          return false;
        }
        return true;
      });

  router->SetNotFoundHandler(
      [](const HttpRequestContext&, HttpRouter::Response& res) {
        res.Status(404).Json(R"({"error":"not found"})");
      });

  router->SetErrorHandler([](const HttpRequestContext&,
                             HttpRouter::Response& res,
                             const std::string& error) {
    res.Status(500).Json(std::string(R"({"error":")") + error + R"("})");
  });

  router->Get("/api/search",
              [](const HttpRequestContext& ctx, HttpRouter::Response& res) {
                std::string q = ctx.query.Get("q", "");
                res.Json(std::string(R"({"query":")") + q + R"("})");
              });

  router->Get("/api/private/profile",
              [](const HttpRequestContext&, HttpRouter::Response& res) {
                res.Json(R"({"user":"demo"})");
              });

  router->Post("/api/upload", [](const HttpRequestContext& ctx,
                                 HttpRouter::Response& res) {
    if (!ctx.IsMultipart()) {
      res.Status(400).Json(R"({"error":"expected multipart/form-data"})");
      return;
    }

    const auto& fields = ctx.GetMultipartFields();
    const auto& files = ctx.GetMultipartFiles();
    res.Json(std::string(R"({"fields":)") + std::to_string(fields.size()) +
             R"(,"files":)" + std::to_string(files.size()) + "}");
  });

  router->Get("/download/{name}",
              [](const HttpRequestContext& ctx, HttpRouter::Response& res) {
                std::string name = ctx.GetParam("name");
                res.Download("./public/" + name, name);
              });

  auto handler = std::make_unique<RouterHttpRequestHandler>(std::move(router));
  HttpServer server(&runner, handler.get());
  server.AddAllowedOrigin("*");

  constexpr int kPort = 8081;
  if (!server.Start("0.0.0.0", kPort)) {
    printf("failed to start advanced HTTP server\n");
    return 1;
  }

  printf("Advanced HTTP router listening on http://127.0.0.1:%d\n", kPort);
  printf("Try: curl 'http://127.0.0.1:%d/api/search?q=xtils'\n", kPort);
  printf("Press Ctrl+C to stop.\n");
  while (!system::SignalHandler::IsShutdownRequested()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  runner.PostTask([&server]() { server.Stop(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  return 0;
}
