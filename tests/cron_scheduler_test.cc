#include <ctime>
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <chrono>
#include <set>
#include <thread>
#include <vector>

#include "doctest/doctest.h"
#include "xtils/tasks/cron_scheduler.h"

// Helper function to get the current time in system_clock
xtils::CronScheduler::TimePoint getCurrentTime() {
  return xtils::CronScheduler::Clock::now();
}

// Helper to advance time in test mode and trigger checks
void advanceTimeAndTrigger(xtils::CronScheduler& scheduler,
                           xtils::CronScheduler::TimePoint& now,
                           xtils::CronScheduler::Seconds duration,
                           std::atomic<int>& run_count) {
  for (int i = 0; i < duration.count(); ++i) {
    now += xtils::CronScheduler::Seconds(1);
    scheduler.triggerCheck(now);
    std::this_thread::sleep_for(
        std::chrono::milliseconds(1));  // Allow tasks to process
  }
}

TEST_CASE("CronScheduler: Every interval tasks in test mode") {
  xtils::CronScheduler scheduler(0, true);  // testMode = true
  std::atomic<int> counter = 0;
  std::vector<xtils::CronScheduler::TimePoint> run_times;

  auto task_id = scheduler.every(xtils::CronScheduler::Seconds(2), [&]() {
    counter++;
    run_times.push_back(getCurrentTime());
  });

  CHECK(task_id > 0);

  xtils::CronScheduler::TimePoint now = getCurrentTime();
  scheduler.triggerCheck(now);  // Initial check should not run it immediately

  CHECK(counter == 0);

  // Advance time by 1 second, should not run
  advanceTimeAndTrigger(scheduler, now, xtils::CronScheduler::Seconds(1),
                        counter);
  CHECK(counter == 0);

  // Advance time by another 1 second (total 2s), should run
  advanceTimeAndTrigger(scheduler, now, xtils::CronScheduler::Seconds(1),
                        counter);
  CHECK(counter == 1);
  CHECK(run_times.size() == 1);

  // Advance time by 2 seconds, should run again
  advanceTimeAndTrigger(scheduler, now, xtils::CronScheduler::Seconds(2),
                        counter);
  CHECK(counter == 2);
  CHECK(run_times.size() == 2);

  // Check task info
  auto info = scheduler.getTaskInfo(task_id);
  REQUIRE(info);
  CHECK(info->id == task_id);
  CHECK(info->type == "Interval");
  CHECK(info->active == true);
  CHECK(info->schedule == "every 2s");
  CHECK(info->lastRun != 0);  // Should have run at least once

  scheduler.stop();
}

TEST_CASE("CronScheduler: Cron tasks in test mode") {
  xtils::CronScheduler scheduler(0, true);  // testMode = true
  std::atomic<int> counter = 0;
  std::vector<xtils::CronScheduler::TimePoint> run_times;

  // Schedule to run at 5 and 10 seconds past the minute
  std::set<int> seconds_to_run = {5, 10};
  auto task_id = scheduler.cron(seconds_to_run, {}, {}, {}, {}, {}, [&]() {
    counter++;
    run_times.push_back(getCurrentTime());
  });

  CHECK(task_id > 0);

  auto advanceToNextRun = [&](int expected_count) {
    auto info = scheduler.getTaskInfo(task_id);
    REQUIRE(info);
    auto next_run_tp = xtils::CronScheduler::Clock::from_time_t(info->nextRun);
    auto now = next_run_tp - xtils::CronScheduler::Seconds(1);

    scheduler.triggerCheck(now);
    CHECK(counter == expected_count - 1);

    advanceTimeAndTrigger(scheduler, now, xtils::CronScheduler::Seconds(1),
                          counter);
    CHECK(counter == expected_count);
    CHECK(run_times.size() == static_cast<size_t>(expected_count));
  };

  advanceToNextRun(1);
  advanceToNextRun(2);
  advanceToNextRun(3);

  // Check task info
  auto info2 = scheduler.getTaskInfo(task_id);
  REQUIRE(info2);
  CHECK(info2->id == task_id);
  CHECK(info2->type == "Cron");
  CHECK(info2->active == true);
  CHECK(info2->schedule == "cron 5,10 * * * * *");
  CHECK(info2->lastRun != 0);

  scheduler.stop();
}

TEST_CASE("CronScheduler: Cancel task") {
  xtils::CronScheduler scheduler(0, true);  // testMode = true
  std::atomic<int> counter = 0;

  auto task_id =
      scheduler.every(xtils::CronScheduler::Seconds(1), [&]() { counter++; });

  CHECK(task_id > 0);

  xtils::CronScheduler::TimePoint now = getCurrentTime();
  scheduler.triggerCheck(now);
  CHECK(counter == 0);

  // Let it run once
  advanceTimeAndTrigger(scheduler, now, xtils::CronScheduler::Seconds(1),
                        counter);
  CHECK(counter == 1);

  // Cancel the task
  bool cancelled = scheduler.cancel(task_id);
  CHECK(cancelled == true);

  // Verify task info shows inactive
  auto info = scheduler.getTaskInfo(task_id);
  REQUIRE(info);
  CHECK(info->active == false);

  // Advance time, it should not run again
  advanceTimeAndTrigger(scheduler, now, xtils::CronScheduler::Seconds(5),
                        counter);
  CHECK(counter == 1);  // Counter should still be 1

  scheduler.stop();
}

TEST_CASE("CronScheduler: GetTaskInfo for non-existent task") {
  xtils::CronScheduler scheduler(0, true);
  auto info = scheduler.getTaskInfo(9999);  // Non-existent ID
  CHECK(!info);
  scheduler.stop();
}

TEST_CASE("CronScheduler: Start and Stop with real threads (basic check)") {
  // Real-mode timing is wall-clock based and sensitive to CI load. We only
  // assert that the scheduler eventually fires the task and stops cleanly.
  // Precise tick counts are covered by the deterministic test-mode cases
  // above. Do NOT add tight upper bounds here — they have proved flaky.
  xtils::CronScheduler scheduler;  // Real mode
  std::atomic<int> counter = 0;

  auto task_id = scheduler.every(xtils::CronScheduler::Seconds(1),
                                 [&]() { counter++; });

  scheduler.start();
  std::this_thread::sleep_for(std::chrono::seconds(3));
  scheduler.stop();

  CHECK(counter >= 1);  // smoke check: scheduler thread fired at least once

  auto info = scheduler.getTaskInfo(task_id);
  REQUIRE(info);
  CHECK(info->active == true);
  CHECK(info->lastRun != 0);
}

TEST_CASE("CronScheduler: Multiple tasks and cancellation in real mode") {
  // See note in previous test: real-mode timing is intentionally only smoke-
  // tested. Deterministic counts live in the test-mode cases above.
  xtils::CronScheduler scheduler;
  std::atomic<int> every_counter = 0;
  std::atomic<int> cron_counter = 0;

  auto every_task_id = scheduler.every(xtils::CronScheduler::Seconds(1),
                                       [&]() { every_counter++; });
  auto currentTime = getCurrentTime();

  std::tm tm_now = xtils::CronScheduler::toLocalTm(currentTime, 0);
  std::set<int> seconds_for_cron = {2, 7};
  if (tm_now.tm_sec < 53) {
    seconds_for_cron.clear();
    seconds_for_cron.insert(tm_now.tm_sec + 2);
    seconds_for_cron.insert(tm_now.tm_sec + 7);
  } else {
    std::this_thread::sleep_for(std::chrono::seconds(60 - tm_now.tm_sec));
  }
  auto cron_task_id =
      scheduler.cron(seconds_for_cron, {}, {}, {}, {}, {}, [&]() {
        cron_counter++;
      });

  scheduler.start();
  std::this_thread::sleep_for(std::chrono::seconds(4));

  // Cancel the every task and let it idle through the rest of the window.
  CHECK(scheduler.cancel(every_task_id));
  int every_count_at_cancel = every_counter.load();

  std::this_thread::sleep_for(std::chrono::seconds(4));
  scheduler.stop();

  // Smoke checks only:
  // - every task fired at least once before cancellation
  // - cancellation actually stopped further increments
  // - cron task fired at least once for the configured seconds
  CHECK(every_counter >= 1);
  CHECK(every_counter == every_count_at_cancel);
  CHECK(cron_counter >= 1);

  auto every_info = scheduler.getTaskInfo(every_task_id);
  REQUIRE(every_info);
  CHECK(every_info->active == false);

  auto cron_info = scheduler.getTaskInfo(cron_task_id);
  REQUIRE(cron_info);
  CHECK(cron_info->active == true);
}

TEST_CASE("CronScheduler: Scheduler clean shutdown with active tasks") {
  xtils::CronScheduler scheduler;
  std::atomic<int> counter = 0;

  scheduler.every(xtils::CronScheduler::Seconds(1), [&]() { counter++; });

  scheduler.start();
  std::this_thread::sleep_for(std::chrono::seconds(2));  // Let it run a bit
  scheduler.stop();  // Should clean up gracefully

  // No specific CHECK for counter, just ensuring no crash and stop works.
  // The important part is that stop() completes without issues.
  CHECK(true);  // Placeholder check for successful execution
}
