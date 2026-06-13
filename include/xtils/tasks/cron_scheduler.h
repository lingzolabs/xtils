#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>

namespace xtils {
class CronScheduler {
 public:
  using TaskID = uint64_t;
  using Clock = std::chrono::system_clock;
  using TimePoint = Clock::time_point;
  using Seconds = std::chrono::seconds;
  using Minutes = std::chrono::minutes;
  using Hours = std::chrono::hours;

  struct TaskInfo {
    TaskID id;
    std::string type;
    bool active;
    std::string schedule;
    std::time_t lastRun;
    std::time_t nextRun;
  };

  explicit CronScheduler(int tzOffsetMinutes = 0, bool testMode = false);
  ~CronScheduler();

  TaskID every(Seconds interval, std::function<void()> fn);

  TaskID cron(std::set<int> seconds, std::set<int> minutes, std::set<int> hours,
              std::set<int> days, std::set<int> months, std::set<int> weekdays,
              std::function<void()> fn);

  void start();
  void stop();

  bool cancel(TaskID id);
  std::optional<TaskInfo> getTaskInfo(TaskID id);

  // for testing
  void triggerCheck(TimePoint now);

  static std::tm advanceSec(std::tm tm, int sec);
  static std::tm toLocalTm(TimePoint tp, int tzOffsetMinutes);
  static TimePoint fromLocalTm(std::tm tm, int tzOffsetMinutes);

 private:
  enum class TaskType { Interval, Cron };

  struct Task {
    TaskID id;
    TaskType type;
    std::chrono::seconds interval;
    std::set<int> seconds, minutes, hours, days, months, weekdays;
    std::function<void()> fn;
    TimePoint lastRun{};
    TimePoint nextRun{};
    bool active = true;
  };

  struct TaskCompare {
    bool operator()(Task* a, Task* b) const { return a->nextRun > b->nextRun; }
  };

  TaskID addTask(TaskType type, std::chrono::seconds interval,
                 std::set<int> seconds, std::set<int> minutes,
                 std::set<int> hours, std::set<int> days, std::set<int> months,
                 std::set<int> weekdays, std::function<void()> fn);
  void rebuildQueue();
  void schedulerThread();
  TimePoint calcNextRunTime(const Task& t, TimePoint from);
  std::string describeTask(const Task& t);
  void runOnce(TimePoint now);

  std::unordered_map<TaskID, Task> tasks_;
  std::atomic<bool> running_;
  std::thread worker_;
  std::mutex mutex_;
  std::condition_variable cv_;
  int tzOffsetMinutes_;
  std::atomic<TaskID> nextId_;
  bool testMode_;
  std::priority_queue<Task*, std::vector<Task*>, TaskCompare> taskQueue_;
};

}  // namespace xtils
