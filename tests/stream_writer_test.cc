#include "xtils/net/stream_writer.h"

#include <fcntl.h>
#include <unistd.h>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

using namespace xtils;

TEST_CASE("StreamWriter preserves a rejected chunk and yields at its budget") {
  StreamWriter writer;
  std::string payload(1024 * 1024, 'x');
  writer.Append(payload);
  std::string blocked;
  auto result = writer.Pump([&](const void* data, size_t len) {
    blocked.assign(static_cast<const char*>(data), len);
    return SendStatus::kWouldBlock;
  });
  CHECK(result == StreamWriter::PumpResult::kWouldBlock);
  CHECK(writer.accepted_bytes() == 0);
  std::string received;
  bool first = true;
  auto sink = [&](const void* data, size_t len) {
    CHECK(len <= kMaxSendChunkSize);
    if (first) {
      CHECK(std::string(static_cast<const char*>(data), len) == blocked);
      first = false;
    }
    received.append(static_cast<const char*>(data), len);
    return SendStatus::kAccepted;
  };
  CHECK(writer.Pump(sink) == StreamWriter::PumpResult::kYield);
  CHECK(received.size() == 256 * 1024);
  while (!writer.empty()) writer.Pump(sink);
  CHECK(received == payload);
}

TEST_CASE("StreamWriter streams files in order and detects truncation") {
  char path[] = "/tmp/xtils-stream-writer-XXXXXX";
  ScopedFile file(mkstemp(path));
  REQUIRE(file);
  Scoped remove([&] { unlink(path); });
  std::string payload(2 * kMaxSendChunkSize, 'f');
  REQUIRE(write(file.get(), payload.data(), payload.size()) ==
          static_cast<ssize_t>(payload.size()));
  StreamWriter writer;
  writer.Append("header");
  REQUIRE(writer.AppendFile(path));
  writer.Append("trailer");
  SUBCASE("backpressure keeps the same file bytes") {
    std::string received;
    bool block = false;
    auto sink = [&](const void* data, size_t len) {
      block = !block;
      if (block) return SendStatus::kWouldBlock;
      received.append(static_cast<const char*>(data), len);
      return SendStatus::kAccepted;
    };
    while (!writer.empty()) writer.Pump(sink);
    CHECK(received == "header" + payload + "trailer");
    CHECK(writer.accepted_bytes() == writer.total_bytes());
  }
  SUBCASE("truncated source is an error, not successful EOF") {
    REQUIRE(ftruncate(file.get(), 3) == 0);
    CHECK(writer.Pump([](const void*, size_t) {
      return SendStatus::kAccepted;
    }) == StreamWriter::PumpResult::kError);
    CHECK_FALSE(writer.error().empty());
  }
}
