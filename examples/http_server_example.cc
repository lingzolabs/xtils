// HTTP Server Example
// Demonstrates xtils HTTP router features: routing, path params, static files,
// CORS, route groups, and middleware.

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

#include "xtils/logging/logger.h"
#include "xtils/net/http_router.h"
#include "xtils/net/http_server.h"
#include "xtils/system/signal_handler.h"
#include "xtils/tasks/thread_task_runner.h"

using namespace xtils;

int main() {
  // Initialize signal handler for graceful shutdown (Ctrl+C).
  system::SignalHandler::Initialize();

  // Create a task runner (drives the server's I/O loop).
  auto task_runner = ThreadTaskRunner::CreateAndStart("http_server");

  // Build the router.
  auto router = std::make_unique<HttpRouter>();

  // --- Basic GET route (JSON response) ---
  router->Get("/api/hello",
              [](const HttpRequestContext& ctx, HttpRouter::Response& resp) {
                resp.Json(R"({"message":"Hello, World!"})");
              });

  // --- POST route (read request body) ---
  router->Post("/api/echo",
               [](const HttpRequestContext& ctx, HttpRouter::Response& resp) {
                 std::string body = ctx.GetBody();
                 resp.Json(R"({"echo":")" + body + R"("})");
               });

  // --- Path parameter: /users/{id} ---
  router->Get("/api/users/{id}", [](const HttpRequestContext& ctx,
                                    HttpRouter::Response& resp) {
    std::string id = ctx.GetParam("id");
    resp.Json(R"({"id":")" + id + R"(","name":"User )" + id + R"("})");
  });

  // --- Route group: /api/v1 ---
  auto v1 = router->Group("/api/v1");

  v1.Get("/status",
         [](const HttpRequestContext& ctx, HttpRouter::Response& resp) {
           resp.Json(R"({"status":"ok","version":"1.0.0"})");
         });

  v1.Post("/items",
          [](const HttpRequestContext& ctx, HttpRouter::Response& resp) {
            std::string body = ctx.GetBody();
            LogI("Create item: %s", body.c_str());
            resp.Status(201).Json(R"({"created":true})");
          });

  v1.Get("/items/{id}",
         [](const HttpRequestContext& ctx, HttpRouter::Response& resp) {
           std::string id = ctx.GetParam("id");
           resp.Json(R"({"id":")" + id + R"(","name":"Item )" + id + R"("})");
         });

  v1.Delete("/items/{id}",
            [](const HttpRequestContext& ctx, HttpRouter::Response& resp) {
              std::string id = ctx.GetParam("id");
              LogI("Delete item: %s", id.c_str());
              resp.Json(R"({"deleted":")" + id + R"("})");
            });

  // --- Static file serving: /static -> ./public ---
  router->Static("/static", "./public");

  // Create the request handler from the router.
  auto handler = std::make_unique<RouterHttpRequestHandler>(std::move(router));

  // Create and start the HTTP server.
  HttpServer server(&task_runner, handler.get());

  // --- CORS: allow all origins ---
  server.AddAllowedOrigin("*");

  constexpr uint16_t kPort = 8080;
  if (!server.Start("0.0.0.0", kPort)) {
    LogE("Failed to start HTTP server on port %d", kPort);
    return 1;
  }

  LogI("HTTP server listening on http://0.0.0.0:%d", kPort);
  LogI("Routes:");
  LogI("  GET  /api/hello          - JSON greeting");
  LogI("  POST /api/echo           - Echo request body");
  LogI("  GET  /api/users/{id}     - User by ID");
  LogI("  GET  /api/v1/status      - API status");
  LogI("  POST /api/v1/items       - Create item");
  LogI("  GET  /api/v1/items/{id}  - Get item");
  LogI("  DELETE /api/v1/items/{id} - Delete item");
  LogI("  GET  /static/*           - Static files from ./public");
  LogI("Press Ctrl+C to stop.");

  // Keep running until shutdown signal.
  while (!system::SignalHandler::IsShutdownRequested()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  LogI("Shutting down...");
  task_runner.PostTask([&server]() { server.Stop(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  return 0;
}
