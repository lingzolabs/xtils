/*
 * Description: Lightweight metrics primitives + registry.
 *
 * Counter (monotonic), Gauge (up/down), Histogram (preset buckets).
 * Labelled families let one metric record multiple labelled cells, e.g.:
 *
 *   auto& reqs = registry.Counter("http_requests_total", {"method","status"});
 *   reqs.Labels({"GET","200"}).Inc();
 *   reqs.Labels({"POST","500"}).Inc();
 *
 * Render to Prometheus text format with PrometheusExporter::Render().
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace xtils {
namespace metrics {

// ─── Counter ─────────────────────────────────────────────────────────────

class Counter {
 public:
  Counter() = default;

  void Inc(uint64_t v = 1) { value_.fetch_add(v, std::memory_order_relaxed); }
  uint64_t Value() const {
    return value_.load(std::memory_order_relaxed);
  }

 private:
  std::atomic<uint64_t> value_{0};
};

// ─── Gauge ───────────────────────────────────────────────────────────────

class Gauge {
 public:
  Gauge() = default;

  void Set(int64_t v) { value_.store(v, std::memory_order_relaxed); }
  void Inc(int64_t v = 1) { value_.fetch_add(v, std::memory_order_relaxed); }
  void Dec(int64_t v = 1) { value_.fetch_sub(v, std::memory_order_relaxed); }
  int64_t Value() const { return value_.load(std::memory_order_relaxed); }

 private:
  std::atomic<int64_t> value_{0};
};

// ─── Histogram ───────────────────────────────────────────────────────────

class Histogram {
 public:
  // upper_bounds must be strictly ascending; +Inf bucket is implicit.
  explicit Histogram(std::vector<double> upper_bounds);

  void Observe(double value);

  // Snapshot of (cumulative) bucket counts paired with their upper bound.
  // The final entry has bound = +Inf and count = total observations.
  struct Snapshot {
    std::vector<std::pair<double, uint64_t>> buckets;  // cumulative counts
    uint64_t count = 0;
    double sum = 0.0;
  };
  Snapshot Snap() const;

 private:
  std::vector<double> bounds_;
  // bucket_counts_[i] is observations with value <= bounds_[i].
  // Last slot is +Inf bucket (== total count).
  mutable std::mutex mu_;
  std::vector<uint64_t> bucket_counts_;
  uint64_t count_ = 0;
  double sum_ = 0.0;
};

// ─── Labelled families ───────────────────────────────────────────────────

template <typename Cell>
class Family {
 public:
  Family(std::string name, std::string help,
         std::vector<std::string> label_names)
      : name_(std::move(name)),
        help_(std::move(help)),
        label_names_(std::move(label_names)) {}

  // Default ctor for unlabelled cells; Labels({}) gives the singleton cell.
  Cell& Labels(const std::vector<std::string>& values) {
    if (values.size() != label_names_.size()) {
      // Misuse: caller passed wrong arity. We tolerate it by using the
      // values verbatim as the key — exporter will just label them with
      // the names we know about and ignore extras.
    }
    std::lock_guard<std::mutex> lock(mu_);
    auto it = cells_.find(values);
    if (it == cells_.end()) {
      it = cells_.emplace(values, std::make_unique<Cell>()).first;
    }
    return *it->second;
  }

  const std::string& Name() const { return name_; }
  const std::string& Help() const { return help_; }
  const std::vector<std::string>& LabelNames() const { return label_names_; }

  // Snapshot all (label_values, cell*) pairs.
  std::vector<std::pair<std::vector<std::string>, const Cell*>> All() const {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<std::pair<std::vector<std::string>, const Cell*>> out;
    out.reserve(cells_.size());
    for (const auto& kv : cells_) {
      out.emplace_back(kv.first, kv.second.get());
    }
    return out;
  }

 private:
  std::string name_;
  std::string help_;
  std::vector<std::string> label_names_;
  mutable std::mutex mu_;
  std::map<std::vector<std::string>, std::unique_ptr<Cell>> cells_;
};

using CounterFamily = Family<Counter>;
using GaugeFamily = Family<Gauge>;

// HistogramFamily needs its own bound list per family.
class HistogramFamily {
 public:
  HistogramFamily(std::string name, std::string help,
                  std::vector<std::string> label_names,
                  std::vector<double> upper_bounds)
      : name_(std::move(name)),
        help_(std::move(help)),
        label_names_(std::move(label_names)),
        bounds_(std::move(upper_bounds)) {}

  Histogram& Labels(const std::vector<std::string>& values) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = cells_.find(values);
    if (it == cells_.end()) {
      it = cells_.emplace(values, std::make_unique<Histogram>(bounds_)).first;
    }
    return *it->second;
  }

  const std::string& Name() const { return name_; }
  const std::string& Help() const { return help_; }
  const std::vector<std::string>& LabelNames() const { return label_names_; }

  std::vector<std::pair<std::vector<std::string>, const Histogram*>> All()
      const {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<std::pair<std::vector<std::string>, const Histogram*>> out;
    for (const auto& kv : cells_) {
      out.emplace_back(kv.first, kv.second.get());
    }
    return out;
  }

 private:
  std::string name_;
  std::string help_;
  std::vector<std::string> label_names_;
  std::vector<double> bounds_;
  mutable std::mutex mu_;
  std::map<std::vector<std::string>, std::unique_ptr<Histogram>> cells_;
};

// ─── Registry ────────────────────────────────────────────────────────────

class MetricRegistry {
 public:
  // Get-or-create families. Calling with the same name returns the same
  // family — help/label_names of the first call are kept.
  CounterFamily& Counter(const std::string& name,
                         const std::string& help = "",
                         std::vector<std::string> label_names = {});
  GaugeFamily& Gauge(const std::string& name,
                     const std::string& help = "",
                     std::vector<std::string> label_names = {});
  HistogramFamily& Histogram(const std::string& name,
                             const std::string& help = "",
                             std::vector<std::string> label_names = {},
                             std::vector<double> upper_bounds = {});

  // Snapshots for exporters (alphabetical by name).
  std::vector<const CounterFamily*> Counters() const;
  std::vector<const GaugeFamily*> Gauges() const;
  std::vector<const HistogramFamily*> Histograms() const;

 private:
  mutable std::mutex mu_;
  std::map<std::string, std::unique_ptr<CounterFamily>> counters_;
  std::map<std::string, std::unique_ptr<GaugeFamily>> gauges_;
  std::map<std::string, std::unique_ptr<HistogramFamily>> histograms_;
};

// ─── Prometheus text exporter ────────────────────────────────────────────

class PrometheusExporter {
 public:
  // Returns a payload suitable for HTTP `Content-Type: text/plain; version=0.0.4`.
  static std::string Render(const MetricRegistry& registry);
};

}  // namespace metrics
}  // namespace xtils
