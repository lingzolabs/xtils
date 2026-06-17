#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "xtils/tasks/thread_task_runner.h"
#include "xtils/tasks/unix_task_runner.h"

using namespace xtils;

TEST_CASE("TaskRunner: PostDelayedTaskWithHandle returns nonzero handle") {
  auto runner = ThreadTaskRunner::CreateAndStart("ext-test");
  std::atomic<int> ran{0};
  auto h = runner.PostDelayedTaskWithHandle([&] { ran++; }, 50);
  CHECK(h != TaskRunner::kInvalidDelayedTaskHandle);
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  CHECK(ran == 1);
}

TEST_CASE("TaskRunner: CancelDelayedTask prevents execution") {
  auto runner = ThreadTaskRunner::CreateAndStart("ext-test-2");
  std::atomic<int> ran{0};
  auto h = runner.PostDelayedTaskWithHandle([&] { ran++; }, 200);
  CHECK(h != TaskRunner::kInvalidDelayedTaskHandle);

  bool cancelled = runner.CancelDelayedTask(h);
  CHECK(cancelled);

  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  CHECK(ran == 0);
}

TEST_CASE("TaskRunner: CancelDelayedTask on unknown handle returns false") {
  auto runner = ThreadTaskRunner::CreateAndStart("ext-test-3");
  CHECK_FALSE(runner.CancelDelayedTask(TaskRunner::kInvalidDelayedTaskHandle));
  CHECK_FALSE(runner.CancelDelayedTask(99999));
}

TEST_CASE("TaskRunner: CancelDelayedTask after task ran returns false") {
  auto runner = ThreadTaskRunner::CreateAndStart("ext-test-4");
  std::atomic<int> ran{0};
  auto h = runner.PostDelayedTaskWithHandle([&] { ran++; }, 10);
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  CHECK(ran == 1);
  CHECK_FALSE(runner.CancelDelayedTask(h));
}

TEST_CASE("TaskRunner: PostTaskAt schedules at absolute time") {
  auto runner = ThreadTaskRunner::CreateAndStart("ext-test-5");
  std::atomic<int> ran{0};
  auto target = runner.Now() + std::chrono::milliseconds(80);
  runner.PostTaskAt(target, [&] { ran++; });
  std::this_thread::sleep_for(std::chrono::milliseconds(40));
  CHECK(ran == 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  CHECK(ran == 1);
}

TEST_CASE("TaskRunner: PostTaskAt with past time runs immediately") {
  auto runner = ThreadTaskRunner::CreateAndStart("ext-test-6");
  std::atomic<int> ran{0};
  auto past = runner.Now() - std::chrono::seconds(1);
  runner.PostTaskAt(past, [&] { ran++; });
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  CHECK(ran == 1);
}

TEST_CASE("TaskRunner: PostDelayedTask still works (handle not returned)") {
  auto runner = ThreadTaskRunner::CreateAndStart("ext-test-7");
  std::atomic<int> ran{0};
  runner.PostDelayedTask([&] { ran++; }, 30);
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  CHECK(ran == 1);
}
