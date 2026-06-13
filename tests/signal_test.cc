#include "xtils/utils/signal.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

using namespace xtils;

TEST_CASE("Signal: basic connect and emit") {
  Signal<int> sig;
  int received = 0;

  auto sub = sig.Connect([&](int v) { received = v; });
  sig.Emit(42);
  CHECK(received == 42);
}

TEST_CASE("Signal: multiple subscribers") {
  Signal<int> sig;
  int a = 0, b = 0;

  auto s1 = sig.Connect([&](int v) { a = v; });
  auto s2 = sig.Connect([&](int v) { b = v * 2; });

  sig.Emit(5);
  CHECK(a == 5);
  CHECK(b == 10);
}

TEST_CASE("Signal: disconnect") {
  Signal<int> sig;
  int count = 0;

  auto sub = sig.Connect([&](int v) { (void)v; ++count; });
  sig.Emit(1);
  CHECK(count == 1);

  sub.Disconnect();
  sig.Emit(2);
  CHECK(count == 1);  // should not change
}

TEST_CASE("Signal: RAII auto-disconnect") {
  Signal<int> sig;
  int count = 0;

  {
    auto sub = sig.Connect([&](int v) { (void)v; ++count; });
    sig.Emit(1);
    CHECK(count == 1);
  }

  sig.Emit(2);
  CHECK(count == 1);  // sub destroyed, should not fire
}

TEST_CASE("Signal: ScopedSubscriptions") {
  Signal<int> sig;
  int count = 0;

  {
    ScopedSubscriptions subs;
    subs += sig.Connect([&](int v) { (void)v; ++count; });
    subs += sig.Connect([&](int v) { (void)v; ++count; });

    sig.Emit(1);
    CHECK(count == 2);
    CHECK(subs.Count() == 2);
  }

  sig.Emit(2);
  CHECK(count == 2);  // both disconnected
}

TEST_CASE("Signal: void signal (no args)") {
  Signal<> sig;
  int count = 0;

  auto sub = sig.Connect([&]() { ++count; });
  sig.Emit();
  sig.Emit();
  CHECK(count == 2);
}

TEST_CASE("Signal: multiple arguments") {
  Signal<int, std::string> sig;
  int got_int = 0;
  std::string got_str;

  auto sub = sig.Connect([&](int i, std::string s) {
    got_int = i;
    got_str = s;
  });

  sig.Emit(42, "hello");
  CHECK(got_int == 42);
  CHECK(got_str == "hello");
}

TEST_CASE("Signal: Detach prevents auto-disconnect") {
  Signal<int> sig;
  int count = 0;

  {
    auto sub = sig.Connect([&](int v) { (void)v; ++count; });
    sub.Detach();  // won't disconnect on destruction
  }

  sig.Emit(1);
  CHECK(count == 1);  // still connected after sub destroyed
}

TEST_CASE("Signal: DisconnectAll") {
  Signal<int> sig;
  int count = 0;

  auto s1 = sig.Connect([&](int v) { (void)v; ++count; });
  auto s2 = sig.Connect([&](int v) { (void)v; ++count; });
  s1.Detach();
  s2.Detach();

  sig.Emit(1);
  CHECK(count == 2);

  sig.DisconnectAll();
  sig.Emit(2);
  CHECK(count == 2);  // no more callbacks
}

TEST_CASE("Signal: SlotCount") {
  Signal<int> sig;
  CHECK(sig.SlotCount() == 0);

  auto s1 = sig.Connect([](int) {});
  CHECK(sig.SlotCount() == 1);

  auto s2 = sig.Connect([](int) {});
  CHECK(sig.SlotCount() == 2);

  s1.Disconnect();
  CHECK(sig.SlotCount() == 1);
}
