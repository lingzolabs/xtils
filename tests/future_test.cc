#include "xtils/tasks/future.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

using namespace xtils;

TEST_CASE("Future: basic resolve") {
  Promise<int> p;
  auto f = p.GetFuture();

  int result = 0;
  f.Then([&](int v) { result = v; });

  p.SetValue(42);
  CHECK(result == 42);
}

TEST_CASE("Future: chain Then") {
  Promise<int> p;
  auto f = p.GetFuture();

  std::string result;
  f.Then([](int v) { return v * 2; })
      .Then([](int v) { return std::to_string(v); })
      .Then([&](std::string s) { result = s; });

  p.SetValue(21);
  CHECK(result == "42");
}

TEST_CASE("Future: error propagation") {
  Promise<int> p;
  auto f = p.GetFuture();

  bool then_called = false;
  std::string err_msg;

  f.Then([&](int v) {
      then_called = true;
      return v;
    })
    .OnError([&](const Error& e) { err_msg = e.message; });

  p.SetError(Error("something went wrong"));
  CHECK(!then_called);
  CHECK(err_msg == "something went wrong");
}

TEST_CASE("Future: exception in Then becomes error") {
  Promise<int> p;
  auto f = p.GetFuture();

  std::string err_msg;
  f.Then([](int v) -> int {
      (void)v;
      throw std::runtime_error("oops");
      return 0;
    })
    .OnError([&](const Error& e) { err_msg = e.message; });

  p.SetValue(1);
  CHECK(err_msg == "oops");
}

TEST_CASE("Future: resolve before Then (already ready)") {
  Promise<int> p;
  auto f = p.GetFuture();

  p.SetValue(99);  // resolve first

  int result = 0;
  f.Then([&](int v) { result = v; });
  CHECK(result == 99);
}

TEST_CASE("Future: void future") {
  Promise<void> p;
  auto f = p.GetFuture();

  bool called = false;
  f.Then([&]() { called = true; });

  p.SetValue();
  CHECK(called);
}

TEST_CASE("Future: MakeReadyFuture") {
  int result = 0;
  MakeReadyFuture(42).Then([&](int v) { result = v; });
  CHECK(result == 42);
}

TEST_CASE("Future: MakeErrorFuture") {
  std::string msg;
  MakeErrorFuture<int>(Error("fail"))
      .OnError([&](const Error& e) { msg = e.message; });
  CHECK(msg == "fail");
}

TEST_CASE("Future: async resolve from thread") {
  Promise<int> p;
  auto f = p.GetFuture();

  std::atomic<int> result{0};
  f.Then([&](int v) { result.store(v); });

  std::thread t([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    p.SetValue(77);
  });
  t.join();

  // Give time for callback
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  CHECK(result.load() == 77);
}
