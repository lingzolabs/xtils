/*
 * Description: Structured log builder with chained field API.
 *
 * Usage:
 *   LOGI().Field("req_id", id).Field("status", code).Msg("done");
 *
 * Output (PlainFormatter):
 *   2026-06-17 10:30:00 I default: done req_id=abc123 status=200
 *
 * MDC entries on the current thread are appended after fields.
 */
#pragma once

#include <cstdarg>
#include <cstdio>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "xtils/logging/logger.h"
#include "xtils/logging/mdc.h"

namespace xtils {
namespace logger {

class LogBuilder {
 public:
  LogBuilder(Logger* log, const char* tag, const source_loc& loc,
             log_level level)
      : log_(log),
        tag_(tag),
        loc_(loc),
        level_(level),
        enabled_(log && log->Level() <= level) {}

  LogBuilder(const LogBuilder&) = delete;
  LogBuilder& operator=(const LogBuilder&) = delete;
  LogBuilder(LogBuilder&&) = default;

  // Auto-flush with empty body if Msg/Send was never called.
  ~LogBuilder() {
    if (enabled_ && !sent_) Send("");
  }

  // ─── Field append helpers (chainable) ─────────────────────────────────
  LogBuilder& Field(std::string key, std::string value) & {
    if (enabled_) fields_.emplace_back(std::move(key), std::move(value));
    return *this;
  }
  LogBuilder& Field(std::string key, const char* value) & {
    return Field(std::move(key), std::string(value ? value : ""));
  }
  LogBuilder& Field(std::string key, int64_t v) & {
    return Field(std::move(key), std::to_string(v));
  }
  LogBuilder& Field(std::string key, int v) & {
    return Field(std::move(key), std::to_string(v));
  }
  LogBuilder& Field(std::string key, unsigned v) & {
    return Field(std::move(key), std::to_string(v));
  }
  LogBuilder& Field(std::string key, double v) & {
    return Field(std::move(key), std::to_string(v));
  }
  LogBuilder& Field(std::string key, bool v) & {
    return Field(std::move(key), std::string(v ? "true" : "false"));
  }

  // Rvalue overloads to allow chaining off a temporary returned by LOGI().
  template <typename V>
  LogBuilder&& Field(std::string key, V&& value) && {
    Field(std::move(key), std::forward<V>(value));
    return std::move(*this);
  }

  // ─── Terminal: render and dispatch ────────────────────────────────────
  void Msg(const std::string& body) { Send(body); }
  void Msg() { Send(""); }

  void Msg(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    if (!enabled_) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) n = 0;
    if (static_cast<size_t>(n) >= sizeof(buf)) n = sizeof(buf) - 1;
    Send(std::string(buf, static_cast<size_t>(n)));
  }

  // For tests: peek at the rendered body+fields without dispatching.
  std::string RenderForTesting() const {
    std::ostringstream os;
    bool need_sep = false;
    auto emit = [&](const std::string& k, const std::string& v) {
      if (need_sep) os << ' ';
      os << k << '=' << v;
      need_sep = true;
    };
    for (const auto& kv : fields_) emit(kv.first, kv.second);
    for (const auto& kv : Mdc::Snapshot()) emit(kv.first, kv.second);
    return os.str();
  }

 private:
  void Send(const std::string& body);

  Logger* log_;
  const char* tag_;
  source_loc loc_;
  log_level level_;
  bool enabled_;
  bool sent_ = false;
  std::vector<std::pair<std::string, std::string>> fields_;
};

}  // namespace logger
}  // namespace xtils

// Builder-style macros (capital). Coexist with classic LogI/LogD/... macros.
#define __XTILS_BUILDER(level)                                              \
  ::xtils::logger::LogBuilder(::xtils::logger::DefaultLogger(),             \
                              LOG_TAG_STRING, __XTILS_SOURCE_LOC__, level)

#define LOGI() __XTILS_BUILDER(::xtils::logger::info)
#define LOGW() __XTILS_BUILDER(::xtils::logger::warn)
#define LOGE() __XTILS_BUILDER(::xtils::logger::error)
#define LOGD() __XTILS_BUILDER(::xtils::logger::debug)

#ifdef ENABLE_TRACE_LOGGING
#define LOGT() __XTILS_BUILDER(::xtils::logger::trace)
#else
// When trace is disabled, the builder evaluates to a level-disabled no-op.
#define LOGT() __XTILS_BUILDER(::xtils::logger::trace)
#endif
