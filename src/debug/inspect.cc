#include "xtils/debug/inspect.h"

#include <algorithm>
#include <ctime>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "xtils/logging/logger.h"
#include "xtils/net/http_common.h"
#include "xtils/net/http_server.h"
#include "xtils/tasks/task_runner.h"
#include "xtils/tasks/thread_task_runner.h"
#include "xtils/utils/exception.h"
#include "xtils/utils/file_utils.h"
#include "xtils/utils/json.h"
#include "inspect_page.h"  // generated: kInspectPageHtml
#include "xtils/utils/string_utils.h"

namespace xtils {

// ---------------------------------------------------------------------------
// System info helpers
// ---------------------------------------------------------------------------

namespace {

std::string FormatDuration(uint64_t sec) {
  std::string s;
  if (sec >= 86400) { s += std::to_string(sec / 86400) + "d "; sec %= 86400; }
  if (sec >= 3600) { s += std::to_string(sec / 3600) + "h "; sec %= 3600; }
  if (sec >= 60) { s += std::to_string(sec / 60) + "m "; sec %= 60; }
  s += std::to_string(sec) + "s";
  return s;
}

struct SysSnapshot {
  std::string uptime;       // process uptime
  std::string rss;          // e.g. "12345 KB"
  std::string threads;
  std::string fds;
  std::string load;         // e.g. "0.5/0.3/0.2"
  std::string mem;          // e.g. "available/total KB"
  std::string localtime;

  static SysSnapshot Collect() {
    SysSnapshot s;
    long clk = sysconf(_SC_CLK_TCK);

    // Process info from /proc/self/stat
    double sys_uptime = 0;
    Try([&] {
      std::string line;
      file_utils::read("/proc/uptime", &line);
      std::istringstream(line) >> sys_uptime;
    });
    Try([&] {
      std::string line;
      file_utils::read("/proc/self/stat", &line);
      std::istringstream ss(line);
      std::vector<std::string> f;
      std::string tok;
      while (ss >> tok) f.push_back(tok);
      if (f.size() > 23) {
        double start = sys_uptime - std::stol(f[21]) / double(clk);
        s.uptime = FormatDuration(uint64_t(start));
        s.rss = std::to_string(std::stol(f[23]) * sysconf(_SC_PAGESIZE) / 1024)
                + " KB";
        s.threads = f[19];
      }
    });
    Try([&] {
      s.fds = std::to_string(
          file_utils::list_directory("/proc/self/fd").size());
    });

    // Load average
    Try([&] {
      std::string line;
      file_utils::read("/proc/loadavg", &line);
      double l1, l5, l15;
      std::istringstream(line) >> l1 >> l5 >> l15;
      char buf[64];
      snprintf(buf, sizeof(buf), "%.2f/%.2f/%.2f", l1, l5, l15);
      s.load = buf;
    });

    // Memory
    Try([&] {
      std::vector<std::string> lines;
      file_utils::read_lines("/proc/meminfo", &lines);
      size_t total = 0, avail = 0;
      for (auto& l : lines) {
        if (l.find("MemTotal:") == 0)
          std::istringstream(l.substr(9)) >> total;
        else if (l.find("MemAvailable:") == 0)
          std::istringstream(l.substr(13)) >> avail;
      }
      s.mem = std::to_string(avail) + "/" + std::to_string(total) + " KB";
    });

    // Time
    {
      auto t = std::time(nullptr);
      char buf[32];
      std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
      s.localtime = buf;
    }
    return s;
  }
};

}  // namespace

// ---------------------------------------------------------------------------
// Route info
// ---------------------------------------------------------------------------

struct RouteInfo {
  std::string description;
  Inspect::Handler handler;
  bool is_websocket = false;
};

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

class Impl : public HttpRequestHandler {
 public:
  void Init(TaskRunner* runner, const std::string& ip, int port) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    if (running_) return;
    port_ = port;
    ip_ = ip;
    server_ = std::make_unique<HttpServer>(runner, this);
    running_ = server_->Start(ip, port);
    RegisterIndexRoute();
  }

  void Stop() {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    if (!running_) return;
    ws_conns_.clear();
    conn_url_.clear();
    running_ = false;
    server_.reset();
  }

  bool IsRunning() const {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    return running_;
  }

  // Route management
  void AddRoute(const std::string& path, const std::string& desc,
                Inspect::Handler h, bool ws) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    routes_[path] = {desc, std::move(h), ws};
  }

  void RemoveRoute(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    routes_.erase(path);
  }

  bool HasRoute(const std::string& path) const {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    return routes_.count(path) > 0;
  }

  std::vector<std::string> GetPaths() const {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    std::vector<std::string> v;
    for (auto& [p, _] : routes_) v.push_back(p);
    std::sort(v.begin(), v.end());
    return v;
  }

  // Publish
  Inspect::PublishResult Publish(const std::string& url,
                                 const std::string& msg, bool text) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    Inspect::PublishResult r;
    auto it = ws_conns_.find(url);
    if (it == ws_conns_.end()) return r;
    r.has_subscribers = true;
    auto& conns = it->second;
    conns.erase(std::remove(conns.begin(), conns.end(), nullptr), conns.end());
    for (auto* c : conns) {
      try {
        text ? c->SendWebsocketMessageText(msg.data(), msg.size())
             : c->SendWebsocketMessage(msg.data(), msg.size());
        r.sent_count++;
      } catch (const std::exception& e) {
        r.failed_count++;
        if (r.error.empty()) r.error = e.what();
      }
    }
    return r;
  }

  bool HasSubscribers(const std::string& url) const {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    auto it = ws_conns_.find(url);
    if (it == ws_conns_.end()) return false;
    for (auto* c : it->second)
      if (c) return true;
    return false;
  }

  size_t SubscriberCount(const std::string& url) const {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    auto it = ws_conns_.find(url);
    if (it == ws_conns_.end()) return 0;
    size_t n = 0;
    for (auto* c : it->second)
      if (c) n++;
    return n;
  }

  void SetCORS(const std::string& origin) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    cors_ = origin;
    if (server_) server_->AddAllowedOrigin(origin);
  }

  xtils::Json GetServerInfo() const {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    xtils::Json info;
    info["port"] = port_;
    info["ip"] = ip_;
    info["running"] = running_;
    xtils::Json::array_t ra;
    for (auto& [p, ri] : routes_) {
      xtils::Json r;
      r["path"] = p;
      if (!ri.description.empty()) r["description"] = ri.description;
      r["websocket"] = ri.is_websocket;
      ra.push_back(r);
    }
    info["routes"] = ra;
    return info;
  }

  // HttpRequestHandler
  void OnHttpRequest(const HttpRequest& req) override {
    if (req.is_websocket_handshake) {
      HandleWsUpgrade(req);
      return;
    }
    std::string uri(req.uri);
    auto path = PathOf(uri);
    auto query = QueryOf(uri);

    Inspect::Request ireq;
    ireq.path = path;
    ireq.query = std::move(query);
    ireq.body = std::string(req.body);

    Inspect::Response resp;
    {
      std::lock_guard<std::recursive_mutex> lock(mu_);
      auto it = routes_.find(path);
      if (it != routes_.end()) {
        try { it->second.handler(ireq, resp); }
        catch (const std::exception& e) {
          resp = Inspect::Error(std::string("Handler error: ") + e.what());
        }
      } else {
        resp = Inspect::Error("Not found: " + path);
        resp.status = "404 Not Found";
      }
    }
    SendHttpResponse(req.conn, resp);
  }

  void OnWebsocketMessage(const WebsocketMessage& msg) override {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    auto cit = conn_url_.find(msg.conn);
    if (cit == conn_url_.end()) return;
    auto rit = routes_.find(cit->second);
    if (rit == routes_.end() || !rit->second.is_websocket) return;

    Inspect::Request req;
    req.path = cit->second;
    req.body = std::string(msg.data);
    req.is_websocket = true;
    req.is_text = msg.is_text;
    req.connection = msg.conn;

    Inspect::Response resp;
    try { rit->second.handler(req, resp); }
    catch (const std::exception& e) {
      resp = Inspect::Error(std::string("WS error: ") + e.what());
    }
    if (!resp.content.empty()) {
      resp.is_text
          ? msg.conn->SendWebsocketMessageText(resp.content.data(),
                                               resp.content.size())
          : msg.conn->SendWebsocketMessage(resp.content.data(),
                                           resp.content.size());
    }
  }

  void OnHttpConnectionClosed(HttpServerConnection* conn) override {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    auto it = conn_url_.find(conn);
    if (it == conn_url_.end()) return;
    auto& v = ws_conns_[it->second];
    v.erase(std::remove(v.begin(), v.end(), conn), v.end());
    if (v.empty()) ws_conns_.erase(it->second);
    conn_url_.erase(it);
  }

 private:
  // --- Helpers ---
  static std::string PathOf(const std::string& uri) {
    auto q = uri.find('?');
    return q == std::string::npos ? uri : uri.substr(0, q);
  }

  static std::map<std::string, std::string> QueryOf(const std::string& uri) {
    std::map<std::string, std::string> m;
    auto q = uri.find('?');
    if (q == std::string::npos) return m;
    std::istringstream ss(uri.substr(q + 1));
    std::string kv;
    while (std::getline(ss, kv, '&')) {
      auto eq = kv.find('=');
      if (eq != std::string::npos)
        m[kv.substr(0, eq)] = kv.substr(eq + 1);
    }
    return m;
  }

  void HandleWsUpgrade(const HttpRequest& req) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    auto path = PathOf(std::string(req.uri));
    auto it = routes_.find(path);
    if (it == routes_.end() || !it->second.is_websocket) {
      HttpHeaders h{{"Content-Type", "application/json"}};
      req.conn->SendResponseAndClose("400 Bad Request", h,
                                     R"({"error":"WS not supported"})");
      return;
    }
    ws_conns_[path].push_back(req.conn);
    conn_url_[req.conn] = path;
    req.conn->UpgradeToWebsocket(req);
  }

  void SendHttpResponse(HttpServerConnection* conn,
                        const Inspect::Response& resp) {
    HttpHeaders h{{"Content-Type", resp.content_type}};
    if (!cors_.empty()) {
      h.push_back({"Access-Control-Allow-Origin", cors_});
      h.push_back({"Access-Control-Allow-Methods", "GET, POST, OPTIONS"});
      h.push_back({"Access-Control-Allow-Headers", "Content-Type"});
    }
    conn->SendResponse(resp.status.c_str(), h, resp.content);
  }

  // --- Index page ---
  void RegisterIndexRoute() {
    routes_["/"] = {"Index", [this](const Inspect::Request& req,
                                    Inspect::Response& resp) {
      if (req.query.count("json"))
        resp = Inspect::Json(GetServerInfo());
      else
        resp = Inspect::Html(BuildIndexHtml());
    }, false};
  }

  std::string BuildIndexHtml() const {
    // Serialize route & info data as JSON
    xtils::Json routes_json;
    for (auto& [path, ri] : routes_) {
      xtils::Json r;
      r["path"] = path;
      r["ws"] = ri.is_websocket;
      if (!ri.description.empty()) r["desc"] = ri.description;
      routes_json.push_back(r);
    }

    auto s = SysSnapshot::Collect();
    xtils::Json info_json;
    if (!s.uptime.empty()) info_json["uptime"] = s.uptime;
    if (!s.rss.empty()) info_json["rss"] = s.rss;
    if (!s.threads.empty()) info_json["threads"] = s.threads;
    if (!s.fds.empty()) info_json["fds"] = s.fds;
    if (!s.load.empty()) info_json["load"] = s.load;
    if (!s.mem.empty()) info_json["mem"] = s.mem;
    if (!s.localtime.empty()) info_json["time"] = s.localtime;

    std::string html = kInspectPageHtml;
    ReplaceAll(html, "{{ROUTES_JSON}}", routes_json.dump());
    ReplaceAll(html, "{{INFO_JSON}}", info_json.dump());
    return html;
  }

  mutable std::recursive_mutex mu_;
  std::unique_ptr<HttpServer> server_;
  int port_ = 0;
  std::string ip_;
  bool running_ = false;
  std::string cors_;
  std::map<std::string, RouteInfo> routes_;
  std::map<std::string, std::vector<HttpServerConnection*>> ws_conns_;
  std::map<HttpServerConnection*, std::string> conn_url_;
};

// ---------------------------------------------------------------------------
// Inspect public API
// ---------------------------------------------------------------------------

static Impl* g_impl = nullptr;

Inspect::Inspect() : task_runner_(ThreadTaskRunner::CreateAndStart("inspect")) {
  g_impl = new Impl();
}

Inspect::~Inspect() {
  delete g_impl;
  g_impl = nullptr;
}

Inspect& Inspect::Get() {
  static Inspect ins;
  return ins;
}

void Inspect::Init(const std::string& ip, int port) {
  if (g_impl) g_impl->Init(&task_runner_, ip, port);
}

void Inspect::Stop() {
  if (g_impl) g_impl->Stop();
}

bool Inspect::IsRunning() const {
  XTILS_CHECK(g_impl);
  return g_impl->IsRunning();
}

void Inspect::Route(const std::string& path, Handler handler) {
  XTILS_CHECK(g_impl);
  g_impl->AddRoute(path, "", std::move(handler), false);
}

void Inspect::Route(const std::string& path, const std::string& desc,
                    Handler handler) {
  XTILS_CHECK(g_impl);
  g_impl->AddRoute(path, desc, std::move(handler), false);
}

void Inspect::WebSocket(const std::string& path, Handler handler) {
  XTILS_CHECK(g_impl);
  g_impl->AddRoute(path, "", std::move(handler), true);
}

void Inspect::WebSocket(const std::string& path, const std::string& desc,
                        Handler handler) {
  XTILS_CHECK(g_impl);
  g_impl->AddRoute(path, desc, std::move(handler), true);
}

void Inspect::Static(const std::string& path, const std::string& content,
                     const std::string& content_type) {
  XTILS_CHECK(g_impl);
  g_impl->AddRoute(path, "Static",
                   [content, content_type](const Request&, Response& r) {
                     r = Response(content, content_type);
                   }, false);
}

void Inspect::Unregister(const std::string& path) {
  XTILS_CHECK(g_impl);
  g_impl->RemoveRoute(path);
}

bool Inspect::HasRoute(const std::string& path) const {
  XTILS_CHECK(g_impl);
  return g_impl->HasRoute(path);
}

size_t Inspect::Publish(const std::string& url, const std::string& msg,
                        bool is_text) {
  XTILS_CHECK(g_impl);
  return g_impl->Publish(url, msg, is_text).sent_count;
}

size_t Inspect::Publish(const std::string& url, const xtils::Json& json) {
  return Publish(url, json.dump(), true);
}

Inspect::PublishResult Inspect::PublishWithResult(const std::string& url,
                                                  const std::string& msg,
                                                  bool is_text) {
  XTILS_CHECK(g_impl);
  return g_impl->Publish(url, msg, is_text);
}

bool Inspect::HasSubscribers(const std::string& url) const {
  XTILS_CHECK(g_impl);
  return g_impl->HasSubscribers(url);
}

size_t Inspect::GetSubscriberCount(const std::string& url) const {
  XTILS_CHECK(g_impl);
  return g_impl->SubscriberCount(url);
}

void Inspect::SetCORS(const std::string& origin) {
  XTILS_CHECK(g_impl);
  g_impl->SetCORS(origin);
}

xtils::Json Inspect::GetServerInfo() const {
  XTILS_CHECK(g_impl);
  return g_impl->GetServerInfo();
}

std::vector<std::string> Inspect::GetRoutes() const {
  XTILS_CHECK(g_impl);
  return g_impl->GetPaths();
}

// Response helpers
Inspect::Response Inspect::Json(const xtils::Json& j) {
  return Response(j.dump(), "application/json", "200 OK");
}
Inspect::Response Inspect::Text(const std::string& t) {
  return Response(t, "text/plain", "200 OK");
}
Inspect::Response Inspect::Html(const std::string& h) {
  return Response(h, "text/html", "200 OK");
}
Inspect::Response Inspect::Error(const std::string& msg) {
  xtils::Json e;
  e["error"] = msg;
  return Response(e.dump(), "application/json", "500 Internal Server Error");
}
Inspect::Response Inspect::Success(const std::string& msg) {
  xtils::Json j;
  j["success"] = true;
  j["message"] = msg;
  return Response(j.dump(), "application/json", "200 OK");
}

}  // namespace xtils
