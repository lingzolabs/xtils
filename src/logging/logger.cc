/*
 * Description: Logger implementation with async processing
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
 * - Atomic log level (lock-free level check)
 * - LogEntry uses const char* for tag/file/function (no copies)
 * - Timestamp stored as raw timespec, formatted by Formatter on worker thread
 * - Per-sink formatting via Formatter interface
 * - Single allocation per log message in hot path
 */

#include "xtils/logging/logger.h"

#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <queue>
#include <string_view>
#include <thread>
#include <vector>

#include "xtils/logging/sink.h"

#define MAX_LINE_LOG_SIZE (1024 * 8)

namespace xtils {
namespace logger {

// Logger implementation class
class Logger::Impl {
 public:
  Impl() : level_(info), shutdown_requested_(false), dropped_messages_(0) {
    worker_thread_ = std::thread(&Impl::WorkerThread, this);
  }

  ~Impl() { Shutdown(); }

  void SetLevel(log_level level) {
    level_.store(level, std::memory_order_release);
  }

  log_level Level() const {
    return level_.load(std::memory_order_acquire);
  }

  void WriteLogAsync(LogEntry&& entry) {
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      if (log_queue_.size() >= kMaxQueueSize) {
        dropped_messages_.fetch_add(1, std::memory_order_relaxed);
        return;
      }
      log_queue_.push(std::move(entry));
    }
    cv_.notify_one();
  }

  void WriteLogSync(LogEntry&& entry) {
    ProcessLogEntry(entry);
  }

  void WriteRaw(std::string_view message) {
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    for (auto& se : sinks_) {
      if (se.sink) {
        se.sink->write(message);
      }
    }
  }

  void AddSink(std::unique_ptr<Sink> sink,
               std::unique_ptr<Formatter> formatter) {
    if (!formatter) {
      formatter = std::make_unique<PlainFormatter>();
    }
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    sinks_.push_back({std::move(sink), std::move(formatter)});
  }

  void Flush() {
    // Drain the queue
    std::queue<LogEntry> temp_queue;
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      std::swap(temp_queue, log_queue_);
    }

    while (!temp_queue.empty()) {
      ProcessLogEntry(temp_queue.front());
      temp_queue.pop();
    }

    // Flush all sinks
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    for (auto& se : sinks_) {
      if (se.sink) {
        se.sink->flush();
      }
    }
  }

  void Shutdown() {
    if (!shutdown_requested_.exchange(true)) {
      cv_.notify_all();
      if (worker_thread_.joinable()) {
        worker_thread_.join();
      }
    }
  }

  size_t GetDroppedCount() const {
    return dropped_messages_.load(std::memory_order_relaxed);
  }

 private:
  static constexpr size_t kMaxQueueSize = 4096;

  struct SinkEntry {
    std::unique_ptr<Sink> sink;
    std::unique_ptr<Formatter> formatter;
  };

  void WorkerThread() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    while (!shutdown_requested_.load() || !log_queue_.empty()) {
      cv_.wait(lock, [this] {
        return shutdown_requested_.load() || !log_queue_.empty();
      });
      while (!log_queue_.empty()) {
        LogEntry entry = std::move(log_queue_.front());
        log_queue_.pop();
        lock.unlock();
        ProcessLogEntry(entry);
        lock.lock();
      }
    }
  }

  void ProcessLogEntry(const LogEntry& entry) {
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    if (sinks_.empty()) {
      // Fallback: write plain to stdout
      PlainFormatter fmt;
      std::string msg = fmt.Format(entry);
      auto ret = ::write(STDOUT_FILENO, msg.data(), msg.size());
      (void)ret;
    } else {
      for (auto& se : sinks_) {
        if (se.sink && se.formatter) {
          std::string msg = se.formatter->Format(entry);
          se.sink->write(msg);
        }
      }
    }
  }

  std::atomic<log_level> level_;

  std::mutex queue_mutex_;
  std::queue<LogEntry> log_queue_;
  std::condition_variable cv_;

  std::mutex sinks_mutex_;
  std::vector<SinkEntry> sinks_;

  std::atomic<bool> shutdown_requested_;
  std::thread worker_thread_;
  std::atomic<size_t> dropped_messages_;
};

// Logger public interface delegation
Logger::Logger() : impl_(std::make_unique<Impl>()) {}
Logger::~Logger() = default;

void Logger::SetLevel(log_level level) { impl_->SetLevel(level); }
log_level Logger::Level() const { return impl_->Level(); }

void Logger::WriteLogAsync(LogEntry&& entry) {
  impl_->WriteLogAsync(std::move(entry));
}

void Logger::WriteLogSync(LogEntry&& entry) {
  impl_->WriteLogSync(std::move(entry));
}

void Logger::WriteRaw(std::string_view message) {
  if (impl_) impl_->WriteRaw(message);
}

void Logger::AddSink(std::unique_ptr<Sink> sink,
                     std::unique_ptr<Formatter> formatter) {
  impl_->AddSink(std::move(sink), std::move(formatter));
}

void Logger::Flush() { impl_->Flush(); }
void Logger::Shutdown() { impl_->Shutdown(); }
size_t Logger::GetDroppedCount() const { return impl_->GetDroppedCount(); }

// Global logger instance
Logger* DefaultLogger() {
  static Logger logger;
  return &logger;
}

void SetLevel(Logger* logger, log_level level) {
  if (logger) {
    logger->SetLevel(level);
  }
}

}  // namespace logger
}  // namespace xtils

namespace xtils {

// Core log writing function — single entry point for all logging macros
void _write_log(logger::Logger* log, const char* tag,
                const logger::source_loc& loc, logger::log_level level,
                const char* fmt, ...) {
  if (!log || log->Level() > level) return;

  constexpr size_t kBufSize = MAX_LINE_LOG_SIZE;
  constexpr const char* kSuffix = "...[truncated]";
  constexpr size_t kSuffixLen = 14;

  char buf[kBufSize];
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(buf, kBufSize, fmt, args);
  va_end(args);

  if (n >= static_cast<int>(kBufSize)) {
    std::memcpy(buf + kBufSize - (kSuffixLen + 1), kSuffix, kSuffixLen + 1);
    n = kBufSize - 1;
    level = logger::warn;
  } else if (n < 0) {
    std::memcpy(buf, "[format error]", 15);
    n = 14;
    level = logger::warn;
  }

  // Build LogEntry with minimal allocations:
  // - timestamp: raw timespec (no formatting, no allocation)
  // - tag/file/function: const char* from literals (no allocation)
  // - message: single std::string allocation
  logger::LogEntry entry;
  clock_gettime(CLOCK_REALTIME, &entry.timestamp);
  entry.level = level;
  entry.tag = tag;
  entry.file_name = loc.file_name;
  entry.function_name = loc.function_name;
  entry.line = loc.line;
  entry.message.assign(buf, static_cast<size_t>(n));

  // Sync path for warn/error (ensures critical messages are written before
  // potential crash); async path for trace/debug/info (better throughput)
  if (level >= logger::warn) {
    log->WriteLogSync(std::move(entry));
  } else {
    log->WriteLogAsync(std::move(entry));
  }
}

}  // namespace xtils
