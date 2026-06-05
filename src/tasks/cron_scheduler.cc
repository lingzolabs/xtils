#include "xtils/tasks/cron_scheduler.h"

#include <ctime>
#include <vector>

namespace xtils {

CronScheduler::CronScheduler(int tzOffsetMinutes, bool testMode)
    : tzOffsetMinutes_(tzOffsetMinutes),
      running_(false),
      nextId_(1),
      testMode_(testMode) {}

CronScheduler::~CronScheduler() { stop(); }

CronScheduler::TaskID CronScheduler::every(Seconds interval,
                                           std::function<void()> fn) {
  return addTask(TaskType::Interval, interval, {}, {}, {}, {}, {}, {},
                 std::move(fn));
}

CronScheduler::TaskID CronScheduler::cron(
    std::set<int> seconds, std::set<int> minutes, std::set<int> hours,
    std::set<int> days, std::set<int> months, std::set<int> weekdays,
    std::function<void()> fn) {
  return addTask(TaskType::Cron, {}, seconds, minutes, hours, days, months,
                 weekdays, std::move(fn));
}

void CronScheduler::start() {
  std::lock_guard<std::mutex> lk(mutex_);
  if (running_) return;
  running_ = true;
  if (!testMode_) worker_ = std::thread([this] { schedulerThread(); });
}

void CronScheduler::stop() {
  {
    std::lock_guard<std::mutex> lk(mutex_);
    running_ = false;
  }
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

bool CronScheduler::cancel(TaskID id) {
  std::lock_guard<std::mutex> lk(mutex_);
  if (auto it = tasks_.find(id); it != tasks_.end()) {
    it->second.active = false;
    rebuildQueue();
    cv_.notify_all();
    return true;
  }
  return false;
}

std::optional<CronScheduler::TaskInfo> CronScheduler::getTaskInfo(TaskID id) {
  std::lock_guard<std::mutex> lk(mutex_);
  if (auto it = tasks_.find(id); it != tasks_.end()) {
    const auto& t = it->second;
    TaskInfo info;
    info.id = t.id;
    info.active = t.active;
    info.type = (t.type == TaskType::Interval ? "Interval" : "Cron");
    info.lastRun = t.lastRun.time_since_epoch().count()
                       ? Clock::to_time_t(t.lastRun)
                       : 0;
    info.schedule = describeTask(t);
    return info;
  }
  return std::nullopt;
}

void CronScheduler::triggerCheck(TimePoint now) {
  if (!testMode_) return;
  runOnce(now);
}

std::tm CronScheduler::advanceSec(std::tm tm, int sec) {
  std::time_t tt = timegm(&tm) + sec;
  std::tm newtm{};
  gmtime_r(&tt, &newtm);
  return newtm;
}

std::tm CronScheduler::toLocalTm(TimePoint tp, int tzOffsetMinutes) {
  tp += std::chrono::minutes(tzOffsetMinutes);
  std::time_t tt = Clock::to_time_t(tp);
  std::tm tm{};
  gmtime_r(&tt, &tm);
  return tm;
}

CronScheduler::TimePoint CronScheduler::fromLocalTm(std::tm tm,
                                                     int tzOffsetMinutes) {
  std::time_t tt = timegm(&tm) - tzOffsetMinutes * 60;
  return Clock::from_time_t(tt);
}

CronScheduler::TaskID CronScheduler::addTask(
    TaskType type, std::chrono::seconds interval, std::set<int> seconds,
    std::set<int> minutes, std::set<int> hours, std::set<int> days,
    std::set<int> months, std::set<int> weekdays, std::function<void()> fn) {
  TaskID id = nextId_++;
  Task t{id,    type, interval, seconds,  minutes,
         hours, days, months,   weekdays, fn};
  t.nextRun = calcNextRunTime(t, Clock::now());
  std::lock_guard<std::mutex> lk(mutex_);
  tasks_[id] = t;
  rebuildQueue();
  cv_.notify_all();
  return id;
}

void CronScheduler::rebuildQueue() {
  std::priority_queue<Task*, std::vector<Task*>, TaskCompare> q;
  for (auto& [_, t] : tasks_)
    if (t.active) q.push(&t);
  taskQueue_ = std::move(q);
}

void CronScheduler::schedulerThread() {
  std::unique_lock<std::mutex> lk(mutex_);
  while (running_) {
    cv_.wait(lk, [&] { return !taskQueue_.empty() || !running_; });
    if (!running_) break;

    Task* t = taskQueue_.top();
    if (!t->active) {
      taskQueue_.pop();
      continue;
    }

    auto now = Clock::now();
    if (t->nextRun <= now) {
      taskQueue_.pop();
      TimePoint prevNext = t->nextRun;
      lk.unlock();
      try {
        t->fn();
      } catch (...) {
      }
      lk.lock();
      t->lastRun = prevNext;
      t->nextRun = calcNextRunTime(*t, now);
      taskQueue_.push(t);
    } else {
      auto dur = t->nextRun - Clock::now();
      cv_.wait_for(lk, dur, [&] { return !running_; });
    }
  }
}

CronScheduler::TimePoint CronScheduler::calcNextRunTime(const Task& t,
                                                         TimePoint from) {
  if (t.type == TaskType::Interval) return from + t.interval;

  std::tm tm = toLocalTm(from + Seconds(1), tzOffsetMinutes_);

  // 最多跳一年（防止死循环）
  for (int i = 0; i < 400; ++i) {
    // 检查各字段是否匹配
    if ((!t.seconds.empty() && !t.seconds.count(tm.tm_sec)) ||
        (!t.minutes.empty() && !t.minutes.count(tm.tm_min)) ||
        (!t.hours.empty() && !t.hours.count(tm.tm_hour)) ||
        (!t.days.empty() && !t.days.count(tm.tm_mday)) ||
        (!t.months.empty() && !t.months.count(tm.tm_mon + 1)) ||
        (!t.weekdays.empty() && !t.weekdays.count(tm.tm_wday))) {
      // 按字段跳转（优先级：秒→分→时→日→月→年）
      if (!t.seconds.empty() && !t.seconds.count(tm.tm_sec)) {
        auto it = t.seconds.lower_bound(tm.tm_sec + 1);
        if (it == t.seconds.end()) {
          tm.tm_min++;
          tm.tm_sec = *t.seconds.begin();
        } else {
          tm.tm_sec = *it;
        }
        timegm(&tm);
        continue;
      }

      if (!t.minutes.empty() && !t.minutes.count(tm.tm_min)) {
        auto it = t.minutes.lower_bound(tm.tm_min + 1);
        if (it == t.minutes.end()) {
          tm.tm_hour++;
          tm.tm_min = *t.minutes.begin();
        } else {
          tm.tm_min = *it;
        }
        tm.tm_sec = t.seconds.empty() ? 0 : *t.seconds.begin();
        timegm(&tm);
        continue;
      }

      if (!t.hours.empty() && !t.hours.count(tm.tm_hour)) {
        auto it = t.hours.lower_bound(tm.tm_hour + 1);
        if (it == t.hours.end()) {
          tm.tm_mday++;
          tm.tm_hour = *t.hours.begin();
        } else {
          tm.tm_hour = *it;
        }
        tm.tm_min = t.minutes.empty() ? 0 : *t.minutes.begin();
        tm.tm_sec = t.seconds.empty() ? 0 : *t.seconds.begin();
        timegm(&tm);
        continue;
      }

      if (!t.days.empty() && !t.days.count(tm.tm_mday)) {
        tm.tm_mday++;
        tm.tm_hour = t.hours.empty() ? 0 : *t.hours.begin();
        tm.tm_min = t.minutes.empty() ? 0 : *t.minutes.begin();
        tm.tm_sec = t.seconds.empty() ? 0 : *t.seconds.begin();
        timegm(&tm);
        continue;
      }

      if (!t.months.empty() && !t.months.count(tm.tm_mon + 1)) {
        auto it = t.months.lower_bound(tm.tm_mon + 2);
        if (it == t.months.end()) {
          tm.tm_year++;
          tm.tm_mon = *t.months.begin() - 1;
        } else {
          tm.tm_mon = *it - 1;
        }
        tm.tm_mday = t.days.empty() ? 1 : *t.days.begin();
        tm.tm_hour = t.hours.empty() ? 0 : *t.hours.begin();
        tm.tm_min = t.minutes.empty() ? 0 : *t.minutes.begin();
        tm.tm_sec = t.seconds.empty() ? 0 : *t.seconds.begin();
        timegm(&tm);
        continue;
      }

      // 星期跳转
      if (!t.weekdays.empty() && !t.weekdays.count(tm.tm_wday)) {
        tm.tm_mday++;
        tm.tm_hour = t.hours.empty() ? 0 : *t.hours.begin();
        tm.tm_min = t.minutes.empty() ? 0 : *t.minutes.begin();
        tm.tm_sec = t.seconds.empty() ? 0 : *t.seconds.begin();
        timegm(&tm);
        continue;
      }
    }

    return fromLocalTm(tm, tzOffsetMinutes_);
  }

  // fallback（超过一年）
  return from + Hours(24 * 365);
}

std::string CronScheduler::describeTask(const Task& t) {
  if (t.type == TaskType::Interval)
    return "every " + std::to_string(t.interval.count()) + "s";
  auto setToStr = [](const std::set<int>& s) {
    if (s.empty()) return std::string("*");
    std::string r;
    for (int v : s) r += std::to_string(v) + ",";
    if (!r.empty()) r.pop_back();
    return r;
  };
  return "cron " + setToStr(t.seconds) + " " + setToStr(t.minutes) + " " +
         setToStr(t.hours) + " " + setToStr(t.days) + " " +
         setToStr(t.months) + " " + setToStr(t.weekdays);
}

void CronScheduler::runOnce(TimePoint now) {
  std::vector<std::function<void()>> toRun;
  for (auto& [_, t] : tasks_) {
    if (!t.active) continue;
    if (t.nextRun <= now) {
      t.lastRun = t.nextRun;
      t.nextRun = calcNextRunTime(t, now);
      toRun.push_back(t.fn);
    }
  }
  for (auto& fn : toRun) {
    try {
      fn();
    } catch (...) {
    }
  }
}

}  // namespace xtils
