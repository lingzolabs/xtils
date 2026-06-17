#include "xtils/metrics/metrics.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace xtils {
namespace metrics {

// ─── Histogram ──────────────────────────────────────────────────────────

Histogram::Histogram(std::vector<double> upper_bounds)
    : bounds_(std::move(upper_bounds)),
      bucket_counts_(bounds_.size() + 1, 0) {}

void Histogram::Observe(double value) {
  std::lock_guard<std::mutex> lock(mu_);
  count_++;
  sum_ += value;
  // Find the first bound >= value; bump its bucket and all subsequent.
  // Using the cumulative representation directly: increment the bucket
  // index where value would fall, plus all higher buckets.
  size_t i = 0;
  while (i < bounds_.size() && value > bounds_[i]) ++i;
  for (; i < bucket_counts_.size(); ++i) {
    bucket_counts_[i]++;
  }
}

Histogram::Snapshot Histogram::Snap() const {
  std::lock_guard<std::mutex> lock(mu_);
  Snapshot s;
  s.count = count_;
  s.sum = sum_;
  s.buckets.reserve(bucket_counts_.size());
  for (size_t i = 0; i < bounds_.size(); ++i) {
    s.buckets.emplace_back(bounds_[i], bucket_counts_[i]);
  }
  s.buckets.emplace_back(std::numeric_limits<double>::infinity(),
                         bucket_counts_.back());
  return s;
}

// ─── MetricRegistry ─────────────────────────────────────────────────────

CounterFamily& MetricRegistry::Counter(const std::string& name,
                                       const std::string& help,
                                       std::vector<std::string> labels) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = counters_.find(name);
  if (it == counters_.end()) {
    it = counters_
             .emplace(name,
                      std::make_unique<CounterFamily>(name, help,
                                                       std::move(labels)))
             .first;
  }
  return *it->second;
}

GaugeFamily& MetricRegistry::Gauge(const std::string& name,
                                   const std::string& help,
                                   std::vector<std::string> labels) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = gauges_.find(name);
  if (it == gauges_.end()) {
    it = gauges_
             .emplace(name, std::make_unique<GaugeFamily>(name, help,
                                                           std::move(labels)))
             .first;
  }
  return *it->second;
}

HistogramFamily& MetricRegistry::Histogram(
    const std::string& name, const std::string& help,
    std::vector<std::string> labels, std::vector<double> bounds) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = histograms_.find(name);
  if (it == histograms_.end()) {
    if (bounds.empty()) {
      // Sensible default for latency-in-seconds: Prometheus default.
      bounds = {0.005, 0.01, 0.025, 0.05, 0.1,
                0.25,  0.5,  1.0,   2.5,  5.0, 10.0};
    }
    it = histograms_
             .emplace(name,
                      std::make_unique<HistogramFamily>(
                          name, help, std::move(labels), std::move(bounds)))
             .first;
  }
  return *it->second;
}

std::vector<const CounterFamily*> MetricRegistry::Counters() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<const CounterFamily*> out;
  out.reserve(counters_.size());
  for (const auto& kv : counters_) out.push_back(kv.second.get());
  return out;
}

std::vector<const GaugeFamily*> MetricRegistry::Gauges() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<const GaugeFamily*> out;
  out.reserve(gauges_.size());
  for (const auto& kv : gauges_) out.push_back(kv.second.get());
  return out;
}

std::vector<const HistogramFamily*> MetricRegistry::Histograms() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<const HistogramFamily*> out;
  out.reserve(histograms_.size());
  for (const auto& kv : histograms_) out.push_back(kv.second.get());
  return out;
}

// ─── PrometheusExporter ─────────────────────────────────────────────────

namespace {

void EscapeLabelValue(std::ostream& os, const std::string& v) {
  for (char c : v) {
    switch (c) {
      case '\\':
        os << "\\\\";
        break;
      case '\n':
        os << "\\n";
        break;
      case '"':
        os << "\\\"";
        break;
      default:
        os << c;
    }
  }
}

void WriteLabels(std::ostream& os, const std::vector<std::string>& names,
                 const std::vector<std::string>& values, bool with_brace,
                 const std::string& extra_name = "",
                 const std::string& extra_value = "") {
  if (names.empty() && extra_name.empty()) return;
  os << '{';
  bool first = true;
  size_t n = std::min(names.size(), values.size());
  for (size_t i = 0; i < n; ++i) {
    if (!first) os << ',';
    os << names[i] << "=\"";
    EscapeLabelValue(os, values[i]);
    os << '"';
    first = false;
  }
  if (!extra_name.empty()) {
    if (!first) os << ',';
    os << extra_name << "=\"";
    EscapeLabelValue(os, extra_value);
    os << '"';
  }
  os << '}';
  (void)with_brace;
}

std::string FormatDouble(double d) {
  if (std::isinf(d)) return d > 0 ? "+Inf" : "-Inf";
  if (std::isnan(d)) return "NaN";
  std::ostringstream os;
  os << d;
  return os.str();
}

}  // namespace

std::string PrometheusExporter::Render(const MetricRegistry& registry) {
  std::ostringstream os;

  for (const auto* fam : registry.Counters()) {
    if (!fam->Help().empty()) {
      os << "# HELP " << fam->Name() << ' ' << fam->Help() << '\n';
    }
    os << "# TYPE " << fam->Name() << " counter\n";
    auto cells = fam->All();
    if (cells.empty() && fam->LabelNames().empty()) {
      // No cells yet — nothing to emit beyond HELP/TYPE.
    }
    for (const auto& kv : cells) {
      os << fam->Name();
      WriteLabels(os, fam->LabelNames(), kv.first, true);
      os << ' ' << kv.second->Value() << '\n';
    }
  }

  for (const auto* fam : registry.Gauges()) {
    if (!fam->Help().empty()) {
      os << "# HELP " << fam->Name() << ' ' << fam->Help() << '\n';
    }
    os << "# TYPE " << fam->Name() << " gauge\n";
    for (const auto& kv : fam->All()) {
      os << fam->Name();
      WriteLabels(os, fam->LabelNames(), kv.first, true);
      os << ' ' << kv.second->Value() << '\n';
    }
  }

  for (const auto* fam : registry.Histograms()) {
    if (!fam->Help().empty()) {
      os << "# HELP " << fam->Name() << ' ' << fam->Help() << '\n';
    }
    os << "# TYPE " << fam->Name() << " histogram\n";
    for (const auto& kv : fam->All()) {
      auto snap = kv.second->Snap();
      // bucket lines
      for (const auto& b : snap.buckets) {
        os << fam->Name() << "_bucket";
        WriteLabels(os, fam->LabelNames(), kv.first, true, "le",
                    FormatDouble(b.first));
        os << ' ' << b.second << '\n';
      }
      // _sum and _count
      os << fam->Name() << "_sum";
      WriteLabels(os, fam->LabelNames(), kv.first, true);
      os << ' ' << snap.sum << '\n';
      os << fam->Name() << "_count";
      WriteLabels(os, fam->LabelNames(), kv.first, true);
      os << ' ' << snap.count << '\n';
    }
  }

  return os.str();
}

}  // namespace metrics
}  // namespace xtils
