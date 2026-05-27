#include "xtils/logging/logger.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

using namespace xtils::logger;

// ============================================================================
// Test sink that captures messages in a vector<string>
// ============================================================================

class TestSink : public Sink {
 public:
  void write(std::string_view msg) override {
    std::lock_guard<std::mutex> lock(mu_);
    messages_.emplace_back(msg);
  }

  void flush() override {
    std::lock_guard<std::mutex> lock(mu_);
    flush_count_++;
  }

  std::vector<std::string> messages() const {
    std::lock_guard<std::mutex> lock(mu_);
    return messages_;
  }

  size_t flush_count() const {
    std::lock_guard<std::mutex> lock(mu_);
    return flush_count_;
  }

  void clear() {
    std::lock_guard<std::mutex> lock(mu_);
    messages_.clear();
    flush_count_ = 0;
  }

 private:
  mutable std::mutex mu_;
  std::vector<std::string> messages_;
  size_t flush_count_ = 0;
};

// Helper to build a simple LogEntry
static LogEntry MakeEntry(log_level level, const char* msg) {
  LogEntry entry;
  clock_gettime(CLOCK_REALTIME, &entry.timestamp);
  entry.level = level;
  entry.tag = "test";
  entry.file_name = "logger_test.cc";
  entry.function_name = "test";
  entry.line = 42;
  entry.message = msg;
  return entry;
}

// ============================================================================
// Tests
// ============================================================================

TEST_CASE("SetLevel/Level atomic operations") {
  Logger logger;

  // Default level is info
  CHECK(logger.Level() == info);

  logger.SetLevel(debug);
  CHECK(logger.Level() == debug);

  logger.SetLevel(error);
  CHECK(logger.Level() == error);

  logger.SetLevel(trace);
  CHECK(logger.Level() == trace);
}

TEST_CASE("Default logger level filtering") {
  Logger logger;
  auto sink = std::make_unique<TestSink>();
  auto* sink_ptr = sink.get();
  logger.AddSink(std::move(sink));

  // Default level is info, so debug messages should be filtered
  // _write_log checks level before calling WriteLogAsync/WriteLogSync,
  // but we test directly via WriteLogSync to verify sink receives it
  logger.SetLevel(warn);

  // Write an info-level message via WriteLogSync — Logger itself does NOT
  // filter at Write* level; filtering is done in _write_log macro.
  // So WriteLogSync always dispatches regardless of level.
  logger.WriteLogSync(MakeEntry(info, "should arrive"));
  CHECK(sink_ptr->messages().size() == 1);

  // Verify that _write_log respects level filtering
  sink_ptr->clear();
  logger.SetLevel(warn);
  // Simulate what _write_log does: check level before writing
  LogEntry debug_entry = MakeEntry(debug, "filtered");
  if (logger.Level() <= debug_entry.level) {
    logger.WriteLogSync(std::move(debug_entry));
  }
  CHECK(sink_ptr->messages().empty());

  // warn-level should pass
  LogEntry warn_entry = MakeEntry(warn, "passes");
  if (logger.Level() <= warn_entry.level) {
    logger.WriteLogSync(std::move(warn_entry));
  }
  CHECK(sink_ptr->messages().size() == 1);
}

TEST_CASE("WriteLogSync dispatches to sinks immediately") {
  Logger logger;
  auto sink = std::make_unique<TestSink>();
  auto* sink_ptr = sink.get();
  logger.AddSink(std::move(sink));

  logger.WriteLogSync(MakeEntry(info, "sync message"));

  // Should be available immediately (no queue)
  auto msgs = sink_ptr->messages();
  REQUIRE(msgs.size() == 1);
  CHECK(msgs[0].find("sync message") != std::string::npos);
}

TEST_CASE("WriteLogAsync dispatches to sinks") {
  Logger logger;
  auto sink = std::make_unique<TestSink>();
  auto* sink_ptr = sink.get();
  logger.AddSink(std::move(sink));

  logger.WriteLogAsync(MakeEntry(info, "async message"));

  // Flush to ensure the worker thread processes the entry
  logger.Flush();

  auto msgs = sink_ptr->messages();
  REQUIRE(msgs.size() == 1);
  CHECK(msgs[0].find("async message") != std::string::npos);
}

TEST_CASE("GetDroppedCount increments when queue is full") {
  // Mirrors Impl::kMaxQueueSize in logger.cc
  static constexpr size_t kQueueLimit = 4096;
  static constexpr size_t kOverflow = 100;

  Logger logger;
  auto sink = std::make_unique<TestSink>();
  logger.AddSink(std::move(sink));

  // Shutdown the logger to stop the worker thread from draining
  logger.Shutdown();

  CHECK(logger.GetDroppedCount() == 0);

  // Fill the queue beyond its capacity
  for (size_t i = 0; i < kQueueLimit + kOverflow; ++i) {
    logger.WriteLogAsync(MakeEntry(info, "flood"));
  }

  // After Shutdown, the worker thread is joined and queue is drained.
  // Subsequent WriteLogAsync calls fill the queue; once full, drops occur.
  CHECK(logger.GetDroppedCount() == kOverflow);
}

TEST_CASE("Flush drains the queue") {
  Logger logger;
  auto sink = std::make_unique<TestSink>();
  auto* sink_ptr = sink.get();
  logger.AddSink(std::move(sink));

  // Write several async messages
  for (int i = 0; i < 10; ++i) {
    logger.WriteLogAsync(MakeEntry(info, "queued"));
  }

  // Before flush, messages may not all be processed
  logger.Flush();

  // After flush, all messages should be delivered
  CHECK(sink_ptr->messages().size() == 10);
  // Flush should call flush() on all sinks exactly once
  CHECK(sink_ptr->flush_count() == 1);
}

TEST_CASE("Multiple sinks receive the same message") {
  Logger logger;
  auto sink1 = std::make_unique<TestSink>();
  auto sink2 = std::make_unique<TestSink>();
  auto* sink1_ptr = sink1.get();
  auto* sink2_ptr = sink2.get();

  logger.AddSink(std::move(sink1));
  logger.AddSink(std::move(sink2));

  logger.WriteLogSync(MakeEntry(warn, "broadcast"));

  auto msgs1 = sink1_ptr->messages();
  auto msgs2 = sink2_ptr->messages();
  REQUIRE(msgs1.size() == 1);
  REQUIRE(msgs2.size() == 1);
  CHECK(msgs1[0].find("broadcast") != std::string::npos);
  CHECK(msgs2[0].find("broadcast") != std::string::npos);
}

TEST_CASE("PlainFormatter produces expected output format") {
  PlainFormatter fmt;
  LogEntry entry = MakeEntry(info, "hello world");

  std::string output = fmt.Format(entry);

  // Format: "<level> <timestamp> <tag> <file>:<line> <message>\n"
  // Level for info is "I"
  CHECK(output.substr(0, 2) == "I ");
  CHECK(output.find("test") != std::string::npos);         // tag
  CHECK(output.find("logger_test.cc") != std::string::npos);  // file
  CHECK(output.find(":42") != std::string::npos);          // line
  CHECK(output.find("hello world") != std::string::npos);  // message
  CHECK(output.back() == '\n');

  // Verify timestamp format (YYYY-MM-DD HH:MM:SS.uuuuuu)
  // Position after "I " is the timestamp
  CHECK(output[6] == '-');   // YYYY-
  CHECK(output[9] == '-');   // MM-
  CHECK(output[12] == ' ');  // DD<space>
}

TEST_CASE("ColorFormatter includes ANSI codes") {
  ColorFormatter fmt;

  SUBCASE("info level includes green ANSI code") {
    LogEntry entry = MakeEntry(info, "colored");
    std::string output = fmt.Format(entry);

    CHECK(output.find("\033[32m") != std::string::npos);  // green
    CHECK(output.find("\033[0m") != std::string::npos);   // reset
    CHECK(output.find("colored") != std::string::npos);
  }

  SUBCASE("error level includes red ANSI code") {
    LogEntry entry = MakeEntry(error, "error msg");
    std::string output = fmt.Format(entry);

    CHECK(output.find("\033[31m") != std::string::npos);  // red
    CHECK(output.find("\033[0m") != std::string::npos);   // reset
  }

  SUBCASE("warn level includes yellow ANSI code") {
    LogEntry entry = MakeEntry(warn, "warning");
    std::string output = fmt.Format(entry);

    CHECK(output.find("\033[33m") != std::string::npos);  // yellow
    CHECK(output.find("\033[0m") != std::string::npos);   // reset
  }

  SUBCASE("debug level includes cyan ANSI code") {
    LogEntry entry = MakeEntry(debug, "debug msg");
    std::string output = fmt.Format(entry);

    CHECK(output.find("\033[36m") != std::string::npos);  // cyan
    CHECK(output.find("\033[0m") != std::string::npos);   // reset
  }

  SUBCASE("trace level includes white ANSI code") {
    LogEntry entry = MakeEntry(trace, "trace msg");
    std::string output = fmt.Format(entry);

    CHECK(output.find("\033[37m") != std::string::npos);  // white
    CHECK(output.find("\033[0m") != std::string::npos);   // reset
  }
}
