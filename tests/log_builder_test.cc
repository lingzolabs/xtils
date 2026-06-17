#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "xtils/logging/log_builder.h"
#include "xtils/logging/logger.h"
#include "xtils/logging/mdc.h"
#include "xtils/logging/sink.h"

using namespace xtils::logger;

// In-memory sink that captures messages for assertion.
class CapturingSink : public Sink {
 public:
  void write(std::string_view msg) override {
    std::lock_guard<std::mutex> lock(m_);
    out_.emplace_back(msg);
  }
  void flush() override {}

  std::vector<std::string> Take() {
    std::lock_guard<std::mutex> lock(m_);
    return std::move(out_);
  }

 private:
  std::mutex m_;
  std::vector<std::string> out_;
};

// ─── Mdc tests ──────────────────────────────────────────────────────────

TEST_CASE("Mdc: Put/Get/Erase basic") {
  Mdc::Clear();
  CHECK(Mdc::Get("k").empty());
  Mdc::Put("k", "v");
  CHECK(Mdc::Get("k") == "v");
  Mdc::Erase("k");
  CHECK(Mdc::Get("k").empty());
}

TEST_CASE("Mdc::Scope restores prior value") {
  Mdc::Clear();
  Mdc::Put("k", "outer");
  {
    Mdc::Scope s("k", "inner");
    CHECK(Mdc::Get("k") == "inner");
  }
  CHECK(Mdc::Get("k") == "outer");
  Mdc::Clear();
}

TEST_CASE("Mdc::Scope removes key when not previously set") {
  Mdc::Clear();
  CHECK(Mdc::Get("k").empty());
  {
    Mdc::Scope s("k", "v");
    CHECK(Mdc::Get("k") == "v");
  }
  CHECK(Mdc::Get("k").empty());
}

TEST_CASE("Mdc is thread-local") {
  Mdc::Clear();
  Mdc::Put("k", "main");
  std::string in_thread;
  std::thread t([&]() {
    in_thread = Mdc::Get("k");  // should be empty on a fresh thread
    Mdc::Put("k", "worker");
    CHECK(Mdc::Get("k") == "worker");
  });
  t.join();
  CHECK(in_thread.empty());
  CHECK(Mdc::Get("k") == "main");
  Mdc::Clear();
}

// ─── LogBuilder tests ───────────────────────────────────────────────────

TEST_CASE("LogBuilder: Field chains and Msg dispatches") {
  Mdc::Clear();
  Logger logger;
  auto sink = std::make_unique<CapturingSink>();
  CapturingSink* sink_ptr = sink.get();
  logger.AddSink(std::move(sink), std::make_unique<PlainFormatter>());
  logger.SetLevel(trace);

  LogBuilder(&logger, "test", source_loc("f.cc", 1, "fn"), info)
      .Field("req_id", "abc")
      .Field("status", 200)
      .Msg("done");

  // Sync because sync only runs at warn+. Info is async; flush.
  logger.Flush();
  auto out = sink_ptr->Take();
  REQUIRE(out.size() == 1);
  std::string s = out[0];
  CHECK(s.find("done") != std::string::npos);
  CHECK(s.find("req_id=abc") != std::string::npos);
  CHECK(s.find("status=200") != std::string::npos);
}

TEST_CASE("LogBuilder pulls MDC into output") {
  Mdc::Clear();
  Mdc::Put("trace_id", "T-42");
  Logger logger;
  auto sink = std::make_unique<CapturingSink>();
  CapturingSink* sink_ptr = sink.get();
  logger.AddSink(std::move(sink), std::make_unique<PlainFormatter>());
  logger.SetLevel(trace);

  LogBuilder(&logger, "test", source_loc("f.cc", 2, "fn"), warn)
      .Field("k", "v")
      .Msg("hi");
  // warn is sync; no flush needed.
  auto out = sink_ptr->Take();
  REQUIRE(out.size() == 1);
  CHECK(out[0].find("trace_id=T-42") != std::string::npos);
  CHECK(out[0].find("k=v") != std::string::npos);
  CHECK(out[0].find("hi") != std::string::npos);
  Mdc::Clear();
}

TEST_CASE("LogBuilder: level disabled is no-op") {
  Logger logger;
  logger.SetLevel(error);
  auto sink = std::make_unique<CapturingSink>();
  CapturingSink* sink_ptr = sink.get();
  logger.AddSink(std::move(sink));

  LogBuilder(&logger, "test", source_loc("f.cc", 3, "fn"), info)
      .Field("k", "v")
      .Msg("filtered");

  logger.Flush();
  auto out = sink_ptr->Take();
  CHECK(out.empty());
}

// ─── JsonFormatter tests ────────────────────────────────────────────────

TEST_CASE("JsonFormatter: emits valid JSON line with expected fields") {
  Logger logger;
  auto sink = std::make_unique<CapturingSink>();
  CapturingSink* sink_ptr = sink.get();
  logger.AddSink(std::move(sink), std::make_unique<JsonFormatter>());
  logger.SetLevel(trace);

  LogBuilder(&logger, "tag1", source_loc("foo.cc", 7, "fn"), warn)
      .Field("x", 1)
      .Msg("hello \"world\"");
  auto out = sink_ptr->Take();
  REQUIRE(out.size() == 1);
  std::string s = out[0];
  // Starts with '{', ends with "}\n", contains expected keys.
  CHECK(s.front() == '{');
  CHECK(s.back() == '\n');
  CHECK(s.find("\"level\":\"W\"") != std::string::npos);
  CHECK(s.find("\"tag\":\"tag1\"") != std::string::npos);
  CHECK(s.find("\"file\":\"foo.cc\"") != std::string::npos);
  CHECK(s.find("\"line\":7") != std::string::npos);
  // Quotes inside the message must be escaped in JSON.
  CHECK(s.find("hello \\\"world\\\"") != std::string::npos);
}
