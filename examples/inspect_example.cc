#include <atomic>
#include <ctime>
#include <thread>

#include "xtils/debug/inspect.h"
#include "xtils/logging/logger.h"
#include "xtils/utils/json.h"

using namespace xtils;

// Global counter for demonstration
static std::atomic<int> global_counter{0};

// Example handler functions
void HandleHello(const Inspect::Request &req, Inspect::Response &resp) {
  xtils::Json response;
  response["message"] = "Hello, World!";
  response["path"] = req.path;

  // Include query parameters if any
  if (!req.query.empty()) {
    xtils::Json query_json;
    for (const auto &[key, value] : req.query) {
      query_json[key] = value;
    }
    response["query"] = query_json;
  }

  resp = Inspect::Json(response);
}

void HandleUserInfo(const Inspect::Request &req, Inspect::Response &resp) {
  // Extract user ID from query parameters
  auto it = req.query.find("id");
  if (it == req.query.end()) {
    resp = Inspect::Error("Missing 'id' parameter");
    return;
  }

  std::string user_id = it->second;

  xtils::Json user_info;
  user_info["id"] = user_id;
  user_info["name"] = "User " + user_id;
  user_info["email"] = "user" + user_id + "@example.com";
  user_info["active"] = true;

  resp = Inspect::Json(user_info);
}

void HandleStatus(const Inspect::Request &req, Inspect::Response &resp) {
  xtils::Json status;
  status["server"] = "running";
  status["timestamp"] = std::time(nullptr);
  status["version"] = "1.0.0";
  status["global_counter"] = global_counter.load();

  // Get server info from Inspect
  status["inspect_info"] = Inspect::Get().GetServerInfo();

  resp = Inspect::Json(status);
}

void HandleEcho(const Inspect::Request &req, Inspect::Response &resp) {
  if (!req.body.empty()) {
    xtils::Json echo_response;
    echo_response["echo"] = req.body;
    echo_response["content_length"] = req.body.length();
    resp = Inspect::Json(echo_response);
  } else {
    resp = Inspect::Error("Only POST method supported");
  }
}

int main() {
  // Create and configure Inspect server
  Inspect::Get().Init("127.0.0.1", 9090);
  auto &inspect = Inspect::Get();

  // Register routes - named handlers
  INSPECT("/api/hello", "Hello world message", HandleHello(req, resp));
  INSPECT("/api/user", "Get user info (?id=)", HandleUserInfo(req, resp));
  INSPECT("/api/status", "Server status", HandleStatus(req, resp));
  INSPECT("/api/echo", "Echo request body (POST)", HandleEcho(req, resp));

  // Inline routes with new INSPECT macro
  INSPECT("/api/time", "Current timestamp", {
    xtils::Json j;
    j["timestamp"] = std::time(nullptr);
    resp = Inspect::Json(j);
  });

  INSPECT("/api/counter", "Get or increment counter", {
    if (req.body.empty()) {
      xtils::Json j;
      j["counter"] = global_counter.load();
      resp = Inspect::Json(j);
    } else {
      int val = ++global_counter;
      xtils::Json j;
      j["counter"] = val;
      j["message"] = "incremented";
      resp = Inspect::Json(j);
    }
  });

  INSPECT_WS("/ping", "echo pong", {
    LogI("/ping %s", req.body.c_str());
    resp = Inspect::Text("pong");
  });

  // Add a simple demo page
  std::string demo_html = R"(<!DOCTYPE html>
<html>
<head>
    <title>Inspect Demo</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; }
        .endpoint { background: #f5f5f5; padding: 10px; margin: 10px 0; border-radius: 5px; }
        .method { font-weight: bold; color: #007acc; }
        button { padding: 8px 16px; margin: 5px; background: #007acc; color: white; border: none; border-radius: 4px; }
    </style>
</head>
<body>
    <h1>Inspect Server Demo</h1>
    <p>Optimized Inspect server with enhanced features.</p>

    <h2>Available Endpoints:</h2>
    <div class="endpoint">
        <span class="method">GET</span> /api/hello - Hello world with request info
    </div>
    <div class="endpoint">
        <span class="method">GET</span> /api/user?id=123 - Get user information
    </div>
    <div class="endpoint">
        <span class="method">GET</span> /api/status - Server status and metrics
    </div>
    <div class="endpoint">
        <span class="method">GET/POST</span> /api/counter - Counter operations
    </div>
    <div class="endpoint">
        <span class="method">POST</span> /api/echo - Echo request body
    </div>
    <div class="endpoint">
        <span class="method">GET</span> /api/time - Current timestamp
    </div>
    <div class="endpoint">
        <span class="method">GET</span> /ping - Simple ping/pong
    </div>

    <h2>Features Demonstrated:</h2>
    <ul>
        <li>Thread-safe singleton pattern</li>
        <li>INSPECT macros for easy registration</li>
        <li>Real-time WebSocket publishing</li>
        <li>Enhanced error handling and logging</li>
        <li>Auto-generated API documentation</li>
    </ul>
</body>
</html>)";

  INSPECT_STATIC("/demo", demo_html, "text/html");

  // Enable CORS for API endpoints
  inspect.SetCORS("*");
  // Start the server
  LogI("Starting optimized Inspect server on port 9090...");
  LogI("Visit http://localhost:9090/ for API documentation");
  LogI("Visit http://localhost:9090/demo for interactive demo");

  // Background thread for periodic WebSocket messages and stats
  std::thread background_thread([&inspect]() {
    int heartbeat_count = 0;

    while (inspect.IsRunning()) {
      std::this_thread::sleep_for(std::chrono::seconds(30));

      // Send heartbeat to any connected WebSocket clients
      if (inspect.HasSubscribers("/ping")) {
        xtils::Json heartbeat;
        heartbeat["type"] = "heartbeat";
        heartbeat["timestamp"] = std::time(nullptr);
        heartbeat["counter"] = global_counter.load();
        heartbeat["heartbeat_count"] = ++heartbeat_count;

        auto result = inspect.PublishWithResult("/ping", heartbeat.dump());
        if (result.sent_count > 0) {
          LogI("Heartbeat sent to %zu WebSocket subscribers",
               result.sent_count);
        }
      }

      // Log server statistics
      auto server_info = inspect.GetServerInfo();
      LogI("Server stats - Routes: %ld, WebSocket connections: %ld",
           server_info["handlers_count"].size(),
           server_info["total_websocket_connections"].size());
    }
  });

  // Keep the server running
  LogI("=== Optimized Inspect Server Running ===");
  LogI("Features demonstrated:");
  LogI("  - Thread-safe singleton pattern");
  LogI("  - INSPECT macros for easy registration");
  LogI("  - Real-time WebSocket publishing");
  LogI("  - Enhanced error handling and logging");
  LogI("  - Auto-generated API documentation");
  LogI("Press Ctrl+C to stop the server");

  // Simple event loop with graceful shutdown handling
  try {
    while (inspect.IsRunning()) {
      std::this_thread::sleep_for(std::chrono::seconds(1));

      // Simulate some activity every 60 seconds
      static int activity_counter = 0;
      if (++activity_counter % 60 == 0) {
        LogI("Server uptime: %d minutes, global counter: %d",
             activity_counter / 60, global_counter.load());
      }
    }
  } catch (const std::exception &e) {
    LogE("Server error: %s", e.what());
  }

  LogI("Shutting down background thread...");
  background_thread.join();

  LogI("Inspect server example completed");
  return 0;
}
