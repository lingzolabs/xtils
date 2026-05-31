#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "xtils/tasks/thread_task_runner.h"
#include "xtils/utils/json.h"

namespace xtils {

/**
 * Inspect - Simple HTTP/WebSocket server for online debugging
 *
 * Provides easy access to program state and control through web interface.
 * - HTTP routes for status queries and control commands
 * - WebSocket for real-time data push and bidirectional communication
 */
class Inspect {
 public:
  // Request information (unified for both HTTP and WebSocket)
  struct Request {
    std::string path;
    std::map<std::string, std::string> query;  // URL parameters
    std::string body;                          // HTTP body or WebSocket data
    bool is_websocket = false;
    bool is_text = true;         // For WebSocket messages
    void *connection = nullptr;  // Internal connection handle

    Request() = default;
    Request(const std::string &path, const std::string &body = "")
        : path(path), body(body) {}
  };

  // Response information
  struct Response {
    std::string content;
    std::string content_type;
    std::string status;
    bool is_text = true;  // For WebSocket responses

    Response(const std::string &content = "",
             const std::string &content_type = "application/json",
             const std::string &status = "200 OK")
        : content(content), content_type(content_type), status(status) {}
  };

  // WebSocket publish result
  struct PublishResult {
    size_t sent_count = 0;
    size_t failed_count = 0;
    bool has_subscribers = false;
    std::string error;

    bool IsSuccess() const { return sent_count > 0 && failed_count == 0; }
    bool HasFailures() const { return failed_count > 0; }
  };

  // Handler function type (handles both HTTP and WebSocket)
  using Handler = std::function<void(const Request &, Response &)>;

  // Singleton access
  static Inspect &Get();

  /**
   * @brief Initialize the inspect server
   * @param ip Server IP address (default: 127.0.0.1)
   * @param port Server port (default: 8080)
   */
  void Init(const std::string &ip = "127.0.0.1", int port = 8080);

  /**
   * @brief Stop the inspect server
   */
  void Stop();

  /**
   * @brief Check if server is running
   */
  bool IsRunning() const;

  /**
   * @brief Register a route handler (handles both GET and POST automatically)
   * @param path URL path
   * @param handler Request handler function
   */
  void Route(const std::string &path, Handler handler);

  /**
   * @brief Register a route with description for documentation
   * @param path URL path
   * @param description Description for web UI
   * @param handler Request handler function
   */
  void Route(const std::string &path, const std::string &description,
             Handler handler);

  /**
   * @brief Register a WebSocket route (supports both HTTP and WebSocket)
   * @param path URL path
   * @param handler Request handler function
   */
  void WebSocket(const std::string &path, Handler handler);

  /**
   * @brief Register a WebSocket route with description
   */
  void WebSocket(const std::string &path, const std::string &description,
                 Handler handler);

  /**
   * @brief Register static content
   */
  void Static(const std::string &path, const std::string &content,
              const std::string &content_type = "text/html");

  /**
   * @brief Remove a route
   */
  void Unregister(const std::string &path);

  /**
   * @brief Check if route exists
   */
  bool HasRoute(const std::string &path) const;

  // WebSocket publishing (broadcast to all connected clients)

  /**
   * @brief Publish message to WebSocket clients
   * @param url WebSocket URL path
   * @param message Message content
   * @param is_text true for text, false for binary
   * @return Number of clients that received the message
   */
  size_t Publish(const std::string &url, const std::string &message,
                 bool is_text = true);

  /**
   * @brief Publish JSON data to WebSocket clients
   */
  size_t Publish(const std::string &url, const xtils::Json &json);

  /**
   * @brief Publish with detailed result
   */
  PublishResult PublishWithResult(const std::string &url,
                                  const std::string &message,
                                  bool is_text = true);

  /**
   * @brief Check if URL has WebSocket subscribers
   */
  bool HasSubscribers(const std::string &url) const;

  /**
   * @brief Get subscriber count for URL
   */
  size_t GetSubscriberCount(const std::string &url) const;

  // Response helpers
  static Response Json(const xtils::Json &json);
  static Response Text(const std::string &text);
  static Response Html(const std::string &html);
  static Response Error(const std::string &message);
  static Response Success(const std::string &message = "OK");

  // Utilities
  xtils::Json GetServerInfo() const;
  void SetCORS(const std::string &allow_origin = "*");
  std::vector<std::string> GetRoutes() const;
  std::vector<std::string> GetWebSocketRoutes() const;

  // Non-copyable, non-movable
  Inspect(const Inspect &) = delete;
  Inspect &operator=(const Inspect &) = delete;
  Inspect(Inspect &&) = delete;
  Inspect &operator=(Inspect &&) = delete;

 private:
  Inspect();
  ThreadTaskRunner task_runner_;

 public:
  ~Inspect();
};

}  // namespace xtils

#ifdef INSPECT_DISABLE

#define INSPECT(path, desc, body)
#define INSPECT_WS(path, desc, body)
#define INSPECT_VAR(path, expr)
#define INSPECT_STATIC(path, content, content_type)
#define INSPECT_PUBLISH(url, message)
#define INSPECT_PUBLISH_BIN(url, bin)

#else

// ---------------------------------------------------------------------------
// Modern macros - concise registration
// ---------------------------------------------------------------------------

/**
 * Register HTTP route.
 * - Inline body: INSPECT("/path", "desc", { resp = Inspect::Json(j); });
 * - Named handler: INSPECT("/path", "desc", HandleFoo(req, resp));
 *
 * `req` and `resp` are available in the body.
 */
#define INSPECT(path, desc, body)                                          \
  xtils::Inspect::Get().Route(path, desc,                                  \
      [&](const xtils::Inspect::Request &req [[maybe_unused]],             \
          xtils::Inspect::Response &resp [[maybe_unused]]) { body; })

/**
 * Register WebSocket route. `req.body` = received message.
 *
 *   INSPECT_WS("/ws/echo", "echo", { resp = Inspect::Text(req.body); });
 */
#define INSPECT_WS(path, desc, body)                                       \
  xtils::Inspect::Get().WebSocket(path, desc,                              \
      [&](const xtils::Inspect::Request &req [[maybe_unused]],             \
          xtils::Inspect::Response &resp [[maybe_unused]]) { body; })

/**
 * Expose a variable/expression as JSON: {"value": expr}
 *
 *   INSPECT_VAR("/api/counter", counter.load());
 *   INSPECT_VAR("/api/name", config.name);
 */
#define INSPECT_VAR(path, expr)                                            \
  xtils::Inspect::Get().Route(path, #expr,                                 \
      [&](const xtils::Inspect::Request &, xtils::Inspect::Response &resp) { \
        xtils::Json _j_; _j_["value"] = (expr);                           \
        resp = xtils::Inspect::Json(_j_);                                  \
      })

/**
 * Register static content.
 */
#define INSPECT_STATIC(path, content, content_type) \
  xtils::Inspect::Get().Static(path, content, content_type)

/**
 * Publish to WebSocket subscribers.
 */
#define INSPECT_PUBLISH(url, message) \
  xtils::Inspect::Get().Publish(url, message, true)

#define INSPECT_PUBLISH_BIN(url, bin) \
  xtils::Inspect::Get().Publish(url, bin, false)

#endif
