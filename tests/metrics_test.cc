#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <atomic>
#include <thread>
#include <vector>

#include "xtils/metrics/metrics.h"

using namespace xtils::metrics;

TEST_CASE("Counter: Inc and Value") {
  Counter c;
  CHECK(c.Value() == 0);
  c.Inc();
  c.Inc(4);
  CHECK(c.Value() == 5);
}

TEST_CASE("Gauge: Set, Inc, Dec") {
  Gauge g;
  g.Set(10);
  CHECK(g.Value() == 10);
  g.Inc(5);
  CHECK(g.Value() == 15);
  g.Dec(3);
  CHECK(g.Value() == 12);
  g.Set(-7);
  CHECK(g.Value() == -7);
}

TEST_CASE("Counter: concurrent Inc is correct") {
  Counter c;
  constexpr int kThreads = 4;
  constexpr int kIters = 10000;
  std::vector<std::thread> ts;
  for (int i = 0; i < kThreads; ++i) {
    ts.emplace_back([&]() {
      for (int j = 0; j < kIters; ++j) c.Inc();
    });
  }
  for (auto& t : ts) t.join();
  CHECK(c.Value() == static_cast<uint64_t>(kThreads * kIters));
}

TEST_CASE("Histogram: Observe places into right bucket") {
  Histogram h({1.0, 5.0, 10.0});
  h.Observe(0.5);    // bucket 0 (<=1.0)
  h.Observe(3.0);    // bucket 1 (<=5.0)
  h.Observe(7.0);    // bucket 2 (<=10.0)
  h.Observe(100.0);  // bucket 3 (+Inf)
  auto s = h.Snap();
  CHECK(s.count == 4);
  CHECK(s.sum == 110.5);
  REQUIRE(s.buckets.size() == 4);
  // Cumulative counts: 1, 2, 3, 4 (with le=+Inf).
  CHECK(s.buckets[0].second == 1);
  CHECK(s.buckets[1].second == 2);
  CHECK(s.buckets[2].second == 3);
  CHECK(s.buckets[3].second == 4);
}

TEST_CASE("Family: Labels returns same Cell for same labels") {
  CounterFamily fam("reqs", "request count", {"method"});
  fam.Labels({"GET"}).Inc();
  fam.Labels({"GET"}).Inc(2);
  fam.Labels({"POST"}).Inc();
  auto all = fam.All();
  REQUIRE(all.size() == 2);
}

TEST_CASE("MetricRegistry: get-or-create returns same family") {
  MetricRegistry r;
  auto& a = r.Counter("foo");
  auto& b = r.Counter("foo", "different help");
  CHECK(&a == &b);
}

TEST_CASE("PrometheusExporter: counter family output") {
  MetricRegistry r;
  auto& fam = r.Counter("http_requests_total", "Number of HTTP requests",
                        {"method", "status"});
  fam.Labels({"GET", "200"}).Inc(3);
  fam.Labels({"POST", "500"}).Inc(1);
  auto out = PrometheusExporter::Render(r);

  CHECK(out.find("# HELP http_requests_total Number of HTTP requests") !=
        std::string::npos);
  CHECK(out.find("# TYPE http_requests_total counter") != std::string::npos);
  CHECK(out.find("http_requests_total{method=\"GET\",status=\"200\"} 3") !=
        std::string::npos);
  CHECK(out.find("http_requests_total{method=\"POST\",status=\"500\"} 1") !=
        std::string::npos);
}

TEST_CASE("PrometheusExporter: gauge and histogram output") {
  MetricRegistry r;
  r.Gauge("queue_size", "current queue size").Labels({}).Set(42);
  auto& h = r.Histogram("rpc_latency_s", "rpc latency in seconds",
                        {"endpoint"}, {0.01, 0.1, 1.0});
  h.Labels({"echo"}).Observe(0.05);
  h.Labels({"echo"}).Observe(0.5);
  h.Labels({"echo"}).Observe(2.0);

  auto out = PrometheusExporter::Render(r);
  CHECK(out.find("# TYPE queue_size gauge") != std::string::npos);
  CHECK(out.find("queue_size 42") != std::string::npos);

  CHECK(out.find("# TYPE rpc_latency_s histogram") != std::string::npos);
  CHECK(out.find("rpc_latency_s_bucket{endpoint=\"echo\",le=\"0.01\"} 0") !=
        std::string::npos);
  CHECK(out.find("rpc_latency_s_bucket{endpoint=\"echo\",le=\"0.1\"} 1") !=
        std::string::npos);
  CHECK(out.find("rpc_latency_s_bucket{endpoint=\"echo\",le=\"1\"} 2") !=
        std::string::npos);
  CHECK(out.find("rpc_latency_s_bucket{endpoint=\"echo\",le=\"+Inf\"} 3") !=
        std::string::npos);
  CHECK(out.find("rpc_latency_s_count{endpoint=\"echo\"} 3") !=
        std::string::npos);
}
