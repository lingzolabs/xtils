#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <atomic>
#include <stdexcept>

#include "xtils/utils/exception.h"

using namespace xtils;

TEST_CASE("Try: returns true when callback completes") {
  std::atomic<int> ran{0};
  bool ok = Try([&] { ran++; });
  CHECK(ok);
  CHECK(ran == 1);
}

TEST_CASE("Try: catches std::exception") {
  bool ok =
      Try([] { throw std::runtime_error("boom"); }, /*log=*/false);
  CHECK_FALSE(ok);
}

TEST_CASE("Try: catches non-std exceptions") {
  bool ok = Try([] { throw 42; }, /*log=*/false);
  CHECK_FALSE(ok);
}

TEST_CASE("xtils::runtime_error is throwable and carries message") {
  try {
    throw xtils::runtime_error("oops");
  } catch (const std::exception& e) {
    CHECK(std::string(e.what()) == "oops");
  }
}
