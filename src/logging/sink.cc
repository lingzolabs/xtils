/*
 * Description: Sink and Formatter implementations
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
 * - ConsoleSink: cached isatty, loop write for partial writes
 * - FileSink: fixed fseek→ftell bug, fixed rotate path generation
 * - PlainFormatter & ColorFormatter implementations
 * - Simplified Sink interface (string_view)
 */

#include "xtils/logging/sink.h"

#include <errno.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "xtils/logging/logger.h"
#include "xtils/utils/file_utils.h"

namespace xtils {
namespace logger {

// ============================================================================
// Formatter implementations
// ============================================================================

namespace {

// Format timespec into "YYYY-MM-DD HH:MM:SS.uuuuuu"
inline void FormatTimestamp(const struct timespec& ts, char* buf,
                            size_t buf_size) {
  struct tm t;
  localtime_r(&ts.tv_sec, &t);
  int written = strftime(buf, buf_size, "%Y-%m-%d %H:%M:%S", &t);
  snprintf(buf + written, buf_size - written, ".%06ld",
           ts.tv_nsec / 1000);
}

}  // namespace

std::string PlainFormatter::Format(const LogEntry& entry) const {
  char time_buf[32];
  FormatTimestamp(entry.timestamp, time_buf, sizeof(time_buf));

  std::string out;
  out.reserve(entry.message.size() + 80);
  out.append(to_string(entry.level));
  out.push_back(' ');
  out.append(time_buf);
  out.push_back(' ');
  out.append(entry.tag ? entry.tag : "");
  out.push_back(' ');
  out.append(entry.file_name ? entry.file_name : "");
  out.push_back(':');
  out.append(std::to_string(entry.line));
  out.push_back(' ');
  out.append(entry.message);
  out.push_back('\n');
  return out;
}

std::string ColorFormatter::Format(const LogEntry& entry) const {
  static constexpr const char* kColors[] = {
      "\033[37m",  // white  - TRACE
      "\033[36m",  // cyan   - DEBUG
      "\033[32m",  // green  - INFO
      "\033[33m",  // yellow - WARN
      "\033[31m",  // red    - ERROR
  };
  static constexpr const char* kReset = "\033[0m";

  char time_buf[32];
  FormatTimestamp(entry.timestamp, time_buf, sizeof(time_buf));

  std::string out;
  out.reserve(entry.message.size() + 100);
  out.append(kColors[entry.level]);
  out.append(to_string(entry.level));
  out.push_back(' ');
  out.append(time_buf);
  out.push_back(' ');
  out.append(entry.tag ? entry.tag : "");
  out.push_back(' ');
  out.append(entry.file_name ? entry.file_name : "");
  out.push_back(':');
  out.append(std::to_string(entry.line));
  out.push_back(' ');
  out.append(entry.message);
  out.append(kReset);
  out.push_back('\n');
  return out;
}

// ============================================================================
// ConsoleSink
// ============================================================================

ConsoleSink::ConsoleSink() : use_color_(isatty(STDOUT_FILENO)) {}

void ConsoleSink::write(std::string_view msg) {
  const char* data = msg.data();
  size_t remaining = msg.size();
  while (remaining > 0) {
    ssize_t n = ::write(STDOUT_FILENO, data, remaining);
    if (n < 0) {
      if (errno == EINTR) continue;
      break;  // unrecoverable write error
    }
    data += n;
    remaining -= static_cast<size_t>(n);
  }
}

void ConsoleSink::flush() { fsync(STDOUT_FILENO); }

// ============================================================================
// FileSink
// ============================================================================

class FileSink::Impl {
 public:
  explicit Impl(const std::string& path, std::size_t max_bytes,
                std::size_t max_items)
      : name_(path), max_bytes_(max_bytes), max_items_(max_items) {
    logger_file_ = fopen(path.c_str(), "a");
    if (logger_file_) {
      fseek(logger_file_, 0, SEEK_END);
      byte_counts_ = static_cast<std::size_t>(ftell(logger_file_));
    } else {
      byte_counts_ = 0;
    }
  }

  ~Impl() {
    if (logger_file_) {
      fflush(logger_file_);
      int fd = fileno(logger_file_);
      fsync(fd);
      fclose(logger_file_);
    }
  }

  void write(std::string_view msg) {
    if (!logger_file_) return;

    if (byte_counts_ > max_bytes_) {
      Rotate();
    }

    size_t n = fwrite(msg.data(), 1, msg.size(), logger_file_);
    byte_counts_ += n;
  }

  void Rotate() {
    if (logger_file_) {
      fflush(logger_file_);
      int fd = fileno(logger_file_);
      fsync(fd);
      fclose(logger_file_);
      logger_file_ = nullptr;
    }

    namespace fs = file_utils;
    const std::string dir = fs::absolute_path(name_);
    const std::string stem = fs::stem(name_);
    const std::string ext = fs::extension(name_);
    const std::string prefix = fs::join_path(dir, stem);
    const std::string suffix = ext.empty() ? "" : ("." + ext);

    // Remove oldest backup if it exists
    if (max_items_ > 0) {
      auto oldest = prefix + "." + std::to_string(max_items_) + suffix;
      if (fs::exists(oldest)) fs::remove(oldest);
    }

    // Shift existing backups: N-1→N, N-2→N-1, ..., 1→2
    for (int i = static_cast<int>(max_items_) - 1; i >= 1; --i) {
      auto old_file = prefix + "." + std::to_string(i) + suffix;
      auto new_file = prefix + "." + std::to_string(i + 1) + suffix;
      if (fs::exists(old_file)) {
        fs::rename(old_file, new_file);
      }
    }

    // Rename current log file to .1
    auto first_backup = prefix + ".1" + suffix;
    if (fs::exists(name_)) {
      fs::rename(name_, first_backup);
    }

    // Re-open
    logger_file_ = fopen(name_.c_str(), "a");
    if (logger_file_) {
      fseek(logger_file_, 0, SEEK_END);
      byte_counts_ = static_cast<std::size_t>(ftell(logger_file_));
    } else {
      byte_counts_ = 0;
    }
  }

  void flush() {
    if (logger_file_) fflush(logger_file_);
  }

 private:
  std::string name_;
  std::size_t max_bytes_;
  std::size_t max_items_;
  std::size_t byte_counts_ = 0;
  FILE* logger_file_ = nullptr;
};

FileSink::FileSink(const std::string& path, std::size_t max_bytes,
                   std::size_t max_items)
    : impl_(std::make_unique<Impl>(path, max_bytes, max_items)) {}

FileSink::~FileSink() = default;

void FileSink::write(std::string_view msg) { impl_->write(msg); }

void FileSink::flush() { impl_->flush(); }

}  // namespace logger
}  // namespace xtils
