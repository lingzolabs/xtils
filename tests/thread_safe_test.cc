#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <chrono>
#include <list>
#include <string>
#include <thread>

#include "xtils/utils/thread_safe.h"

using namespace xtils;

TEST_CASE("ThreadSafe: Push and TryPop") {
  ThreadSafe<std::list<int>> q;
  CHECK(q.Size() == 0);
  q.Push(1);
  q.Push(2);
  CHECK(q.Size() == 2);

  int v = 0;
  CHECK(q.TryPop(v));
  CHECK(v == 1);
  CHECK(q.TryPop(v));
  CHECK(v == 2);
  CHECK_FALSE(q.TryPop(v));
}

TEST_CASE("ThreadSafe: PopWait blocks until Push") {
  ThreadSafe<std::list<std::string>> q;
  std::thread producer([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    q.Push(std::string("hello"));
  });
  std::string out;
  CHECK(q.PopWait(out, std::chrono::seconds(2)));
  CHECK(out == "hello");
  producer.join();
}

TEST_CASE("ThreadSafe: PopWait timeout returns false") {
  ThreadSafe<std::list<int>> q;
  int v = 0;
  CHECK_FALSE(q.PopWait(v, std::chrono::seconds(1)));
}

TEST_CASE("ThreadSafe: Quit unblocks waiters") {
  ThreadSafe<std::list<int>> q;
  std::thread waiter([&]() {
    int v = 0;
    bool got = q.PopWait(v, std::chrono::seconds(5));
    CHECK_FALSE(got);  // Quit should make PopWait return false
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(40));
  q.Quit();
  waiter.join();
}

TEST_CASE("ThreadSafe: Clear empties the queue") {
  ThreadSafe<std::list<int>> q;
  q.Push(1);
  q.Push(2);
  q.Push(3);
  CHECK(q.Size() == 3);
  q.Clear();
  CHECK(q.Size() == 0);
}

TEST_CASE("ThreadSafe: move-only types via push(rvalue)") {
  ThreadSafe<std::list<std::unique_ptr<int>>> q;
  q.Push(std::make_unique<int>(42));
  std::unique_ptr<int> out;
  CHECK(q.TryPop(out));
  REQUIRE(out);
  CHECK(*out == 42);
}
