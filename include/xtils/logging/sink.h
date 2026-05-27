/*
 * Description: Logging sink interface and built-in sinks
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
 * - Simplified Sink interface: write(string_view) instead of (buf, start, len)
 * - Added Formatter abstraction for per-sink formatting
 * - ConsoleSink caches isatty() at construction
 * - Fixed FileSink bugs (ftell, rotate path)
 */

#pragma once

#include <ctime>
#include <memory>
#include <string>
#include <string_view>

namespace xtils {
namespace logger {

enum log_level { trace = 0, debug = 1, info = 2, warn = 3, error = 4, max };

// Forward declaration
struct LogEntry;

// Formatter interface — each sink can format log entries independently
class Formatter {
 public:
  virtual ~Formatter() = default;
  virtual std::string Format(const LogEntry& entry) const = 0;
};

// Plain text formatter (no color)
class PlainFormatter : public Formatter {
 public:
  std::string Format(const LogEntry& entry) const override;
};

// ANSI color formatter for terminal output
class ColorFormatter : public Formatter {
 public:
  std::string Format(const LogEntry& entry) const override;
};

// Sink interface — minimal and clean
struct Sink {
  virtual ~Sink() = default;
  virtual void write(std::string_view msg) = 0;
  virtual void flush() = 0;
};

// Console sink with cached isatty check
class ConsoleSink : public Sink {
 public:
  ConsoleSink();
  void write(std::string_view msg) override;
  void flush() override;

 private:
  bool use_color_;
};

// File sink with rotation support
class FileSink : public Sink {
 public:
  FileSink(const std::string& path, std::size_t max_bytes,
           std::size_t max_items);
  ~FileSink();
  void write(std::string_view msg) override;
  void flush() override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace logger
}  // namespace xtils
