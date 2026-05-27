/*
 * Description: Lightweight logger header with macro definitions
 *
 * Copyright (c) 2018 - 2024 Albert Lv <altair.albert@gmail.com>
 *
 * Licensed under the MIT License. See LICENSE file in the project root for full
 * license information.
 *
 * Author: Albert Lv <altair.albert@gmail.com>
 * Version: 2.0.0
 *
 * Changelog:
 * - Atomic log level (no mutex overhead for level checks)
 * - LogEntry uses const char* for literals (zero-copy)
 * - Renamed CHECK/DCHECK/FATAL to XTILS_ prefix (opt-in short names)
 * - Removed deprecated wrappers
 * - Removed global namespace alias
 * - Per-sink formatting via Formatter interface
 */

#pragma once

#include <ctime>
#include <memory>
#include <string>

#include "xtils/logging/sink.h"

#ifndef LOG_TAG_STRING
#define LOG_TAG_STRING "default"
#endif

namespace xtils {
namespace logger {

struct source_loc {
  const char* file_name;
  int line;
  const char* function_name;

  constexpr source_loc() : file_name(""), line(0), function_name("") {}
  constexpr source_loc(const char* f, int l, const char* func)
      : file_name(f), line(l), function_name(func) {}
};

constexpr const char* level_name[] = {"T", "D", "I", "W", "E"};
static_assert(log_level::max == sizeof(level_name) / sizeof(level_name[0]),
              "log_level::max need equal sizeof(level_name)");

constexpr const char* to_string(log_level level) { return level_name[level]; }

// Log entry — minimal allocations, const char* for compile-time literals
struct LogEntry {
  struct timespec timestamp;
  log_level level;
  const char* tag;            // from LOG_TAG_STRING (string literal)
  const char* file_name;      // from __FILE__ (string literal)
  const char* function_name;  // from __FUNCTION__ (string literal)
  int line;
  std::string message;        // formatted printf result (1 allocation)
};

// Logger class with async processing
class Logger {
 public:
  Logger();
  ~Logger();

  // Non-copyable, non-movable
  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  void SetLevel(log_level level);
  log_level Level() const;

  // Core logging functions
  void WriteLogAsync(LogEntry&& entry);
  void WriteLogSync(LogEntry&& entry);
  void WriteRaw(std::string_view message);

  // Add a sink with optional formatter (defaults to PlainFormatter)
  void AddSink(std::unique_ptr<Sink> sink,
               std::unique_ptr<Formatter> formatter = nullptr);

  void Flush();
  void Shutdown();

  // Statistics
  size_t GetDroppedCount() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

// Simple interface functions
Logger* DefaultLogger();
void SetLevel(Logger* logger, log_level level);

}  // namespace logger
}  // namespace xtils

// Compile-time filename extraction
constexpr const char* xtils_get_filename(const char* path) {
  const char* last_slash = path;
  for (const char* p = path; *p; ++p) {
    if (*p == '/' || *p == '\\') {
      last_slash = p + 1;
    }
  }
  return last_slash;
}

namespace xtils {

// Core logging function - implemented in logger.cc
void _write_log(logger::Logger* log, const char* tag,
                const logger::source_loc& loc, logger::log_level level,
                const char* fmt, ...);

}  // namespace xtils

// Internal macros
#define __XTILS_SOURCE_NAME__ (xtils_get_filename(__FILE__))
#define __XTILS_SOURCE_LOC__ \
  (xtils::logger::source_loc{__XTILS_SOURCE_NAME__, __LINE__, __FUNCTION__})
#define __XTILS_LOG(logger, level, ...) \
  xtils::_write_log(logger, LOG_TAG_STRING, __XTILS_SOURCE_LOC__, level, \
                    __VA_ARGS__)

// ============================================================================
// Instance-level macros (XTILS_ prefixed)
// ============================================================================

#ifdef ENABLE_TRACE_LOGGING
#define XTILS_LOG_T(logger, ...) \
  __XTILS_LOG(logger, xtils::logger::trace, __VA_ARGS__)
#else
#define XTILS_LOG_T(logger, ...)
#endif

#define XTILS_LOG_D(logger, ...) \
  __XTILS_LOG(logger, xtils::logger::debug, __VA_ARGS__)
#define XTILS_LOG_I(logger, ...) \
  __XTILS_LOG(logger, xtils::logger::info, __VA_ARGS__)
#define XTILS_LOG_W(logger, ...) \
  __XTILS_LOG(logger, xtils::logger::warn, __VA_ARGS__)
#define XTILS_LOG_E(logger, ...) \
  __XTILS_LOG(logger, xtils::logger::error, __VA_ARGS__)

// ============================================================================
// Convenience macros using default logger (kept as-is for ergonomics)
// ============================================================================

#ifdef ENABLE_TRACE_LOGGING
#define LogT(...) \
  __XTILS_LOG(xtils::logger::DefaultLogger(), xtils::logger::trace, __VA_ARGS__)
#else
#define LogT(...)
#endif

#define LogD(...) \
  __XTILS_LOG(xtils::logger::DefaultLogger(), xtils::logger::debug, __VA_ARGS__)
#define LogI(...) \
  __XTILS_LOG(xtils::logger::DefaultLogger(), xtils::logger::info, __VA_ARGS__)
#define LogW(...) \
  __XTILS_LOG(xtils::logger::DefaultLogger(), xtils::logger::warn, __VA_ARGS__)
#define LogE(...) \
  __XTILS_LOG(xtils::logger::DefaultLogger(), xtils::logger::error, __VA_ARGS__)

// Special purpose macros
#define LogTodo() LogW("======>> TODO <<=====")
#define LogThis() LogI("======>> THIS <<=====")

// ============================================================================
// Assertion and fatal error macros (XTILS_ prefixed)
// ============================================================================

#define XTILS_CHECK(expr)                    \
  do {                                       \
    if (!(expr)) {                           \
      LogE("Assert -- " #expr " -- ");       \
      abort();                               \
    }                                        \
  } while (0)

#define XTILS_DCHECK(expr) XTILS_CHECK(expr)

#define XTILS_FATAL(x, ...)       \
  do {                            \
    LogE(x, ##__VA_ARGS__);       \
    abort();                      \
  } while (0)

// ============================================================================
// Opt-in short macro names (define XTILS_LOG_SHORT_MACROS before including)
// ============================================================================

#ifdef XTILS_LOG_SHORT_MACROS

#ifdef ENABLE_TRACE_LOGGING
#define TRACE(logger, ...) XTILS_LOG_T(logger, __VA_ARGS__)
#else
#define TRACE(logger, ...)
#endif

#define DEBUG(logger, ...) XTILS_LOG_D(logger, __VA_ARGS__)
#define INFO(logger, ...) XTILS_LOG_I(logger, __VA_ARGS__)
#define WARN(logger, ...) XTILS_LOG_W(logger, __VA_ARGS__)
#define ERROR(logger, ...) XTILS_LOG_E(logger, __VA_ARGS__)

#define CHECK(expr) XTILS_CHECK(expr)
#define DCHECK(expr) XTILS_DCHECK(expr)
#define FATAL(x, ...) XTILS_FATAL(x, ##__VA_ARGS__)

#endif  // XTILS_LOG_SHORT_MACROS
