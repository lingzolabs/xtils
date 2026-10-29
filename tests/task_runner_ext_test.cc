#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

#include "doctest/doctest.h"
#include "xtils/tasks/thread_task_runner.h"
#include "xtils/tasks/unix_task_runner.h"
#include "xtils/utils/scoped.h"

using namespace xtils;

TEST_CASE("TaskRunner: queued owners release watches before runner teardown") {
  bool released = false;
  {
    UnixTaskRunner runner;
    runner.AddFileDescriptorWatch(0, [] {});
    auto owner = std::make_shared<Scoped>([&] {
      runner.RemoveFileDescriptorWatch(0);
      released = true;
    });
    runner.PostTask([owner] {});
  }
  CHECK(released);
}

TEST_CASE("TaskRunner: stop runs owner callbacks then drains accepted work") {
  auto runner = ThreadTaskRunner::CreateAndStart("ext-orderly-stop");
  std::promise<void> blocked, release;
  auto resume = release.get_future().share();
  std::mutex mutex;
  std::vector<int> order;
  runner.PostTask([&] {
    blocked.set_value();
    resume.wait();
  });
  blocked.get_future().wait();
  REQUIRE(runner.TryPostTask([&] {
    std::lock_guard<std::mutex> lock(mutex);
    order.push_back(2);
  }));
  REQUIRE(runner.RegisterShutdownTask([&] {
    {
      std::lock_guard<std::mutex> lock(mutex);
      order.push_back(1);
    }
    CHECK(runner.TryPostTask([&] {
      std::lock_guard<std::mutex> lock(mutex);
      order.push_back(3);
    }));
  }));
  auto stop = std::async(std::launch::async, [&] { runner.Stop(); });
  while (!runner.get()->QuitCalled()) std::this_thread::yield();
  CHECK_FALSE(runner.TryPostTask([] {}));
  release.set_value();
  REQUIRE(stop.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
  stop.get();
  CHECK(order == std::vector<int>{1, 2, 3});
  CHECK_FALSE(runner.TryPostTask([] {}));
}

TEST_CASE("TaskRunner: concurrent posting cannot access a destroyed runner") {
  auto runner = ThreadTaskRunner::CreateAndStart("ext-concurrent-stop");
  std::atomic<bool> finished{false};
  std::thread poster([&] {
    while (!finished.load()) {
      runner.TryPostTask([] {});
      std::this_thread::yield();
    }
  });
  runner.Stop();
  finished = true;
  poster.join();
  CHECK_FALSE(runner.TryPostTask([] {}));
}

TEST_CASE("TaskRunner: removing shutdown hooks releases captured owners") {
  auto runner = ThreadTaskRunner::CreateAndStart("ext-shutdown-owner");
  auto owner = std::make_shared<int>(42);
  std::weak_ptr<int> weak = owner;
  auto handle = runner.RegisterShutdownTask([owner] {});
  REQUIRE(handle != TaskRunner::kInvalidShutdownTaskHandle);
  owner.reset();
  CHECK_FALSE(weak.expired());
  CHECK(runner.UnregisterShutdownTask(handle));
  CHECK(weak.expired());
  runner.Stop();
}

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
