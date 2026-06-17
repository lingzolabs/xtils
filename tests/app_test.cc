#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "xtils/app/app.h"
#include "xtils/app/service.h"

using namespace xtils;

// A minimal test service
class TestService : public IService {
 public:
  TestService() : IService("test_service") {}
  void Init() override { init_called = true; }
  void Deinit() override { deinit_called = true; }

  bool init_called = false;
  bool deinit_called = false;
};

TEST_CASE("App can be constructed directly") {
  App app;
  CHECK_FALSE(app.IsRunning());
}

TEST_CASE("Multiple App instances don't crash") {
  App app1;
  App app2;
  CHECK_FALSE(app1.IsRunning());
  CHECK_FALSE(app2.IsRunning());
}

TEST_CASE("App Init and basic configuration") {
  App app;
  std::vector<std::string> args = {"test_app"};
  app.Init(args);
  CHECK(app.Conf().GetOr<int>("xtils.threads") == 4);
  CHECK_FALSE(app.IsRunning());
}

TEST_CASE("App service registration and lifecycle") {
  App app;
  auto svc = std::make_shared<TestService>();
  app.Register(svc);

  std::vector<std::string> args = {"test_app"};
  app.Init(args);

  // Services are initialized during Run/pre_run, not at Init time.
  // We can't call Run() in a test (it blocks), so just verify registration
  // didn't crash and the service is accessible.
  CHECK_FALSE(svc->init_called);
}

TEST_CASE("Ins() returns a valid pointer") {
  App* ins = App::Ins();
  CHECK(ins != nullptr);
}

TEST_CASE("App accepts threads=1 (single-threaded mode)") {
  App app;
  std::vector<std::string> args = {"test_app", "--xtils.threads=1"};
  // Should not abort/exit.
  app.Init(args);
  CHECK(app.Conf().GetOr<int>("xtils.threads") == 1);
}

namespace {
struct DepSvc : public IService {
  DepSvc(const char* n, std::vector<std::string> deps,
         std::vector<std::string>* trace)
      : IService(n), deps_(std::move(deps)), trace_(trace) {}
  void Init() override { trace_->push_back("init:" + std::string(name)); }
  void Deinit() override { trace_->push_back("deinit:" + std::string(name)); }
  std::vector<std::string> Dependencies() const override { return deps_; }

  std::vector<std::string> deps_;
  std::vector<std::string>* trace_;
};
}  // namespace

TEST_CASE("App: TopoSort linear chain — A depends on B depends on C") {
  std::vector<std::string> trace;
  App app;
  // Register in arbitrary order.
  app.Register(std::make_shared<DepSvc>("A", std::vector<std::string>{"B"},
                                         &trace));
  app.Register(std::make_shared<DepSvc>("B", std::vector<std::string>{"C"},
                                         &trace));
  app.Register(std::make_shared<DepSvc>("C", std::vector<std::string>{}, &trace));
  auto sorted = app.TopoSortServices();
  std::vector<std::string> names;
  for (const auto& s : sorted) names.push_back(s->Name());
  CHECK(names == std::vector<std::string>{"C", "B", "A"});
}

TEST_CASE("App: TopoSort diamond — D depends on A,B; A,B depend on C") {
  std::vector<std::string> trace;
  App app;
  app.Register(std::make_shared<DepSvc>("D", std::vector<std::string>{"A", "B"},
                                         &trace));
  app.Register(std::make_shared<DepSvc>("A", std::vector<std::string>{"C"},
                                         &trace));
  app.Register(std::make_shared<DepSvc>("B", std::vector<std::string>{"C"},
                                         &trace));
  app.Register(std::make_shared<DepSvc>("C", std::vector<std::string>{}, &trace));
  auto sorted = app.TopoSortServices();
  std::vector<std::string> names;
  for (const auto& s : sorted) names.push_back(s->Name());
  REQUIRE(names.size() == 4);
  // C must come first; D must come last; A and B in the middle.
  CHECK(names[0] == "C");
  CHECK(names[3] == "D");
  CHECK((names[1] == "A" || names[1] == "B"));
  CHECK((names[2] == "A" || names[2] == "B"));
  CHECK(names[1] != names[2]);
}

TEST_CASE("App: TopoSort with no Dependencies preserves registration order") {
  std::vector<std::string> trace;
  App app;
  app.Register(std::make_shared<DepSvc>("first", std::vector<std::string>{},
                                         &trace));
  app.Register(std::make_shared<DepSvc>("second", std::vector<std::string>{},
                                         &trace));
  app.Register(std::make_shared<DepSvc>("third", std::vector<std::string>{},
                                         &trace));
  auto sorted = app.TopoSortServices();
  std::vector<std::string> names;
  for (const auto& s : sorted) names.push_back(s->Name());
  CHECK(names == std::vector<std::string>{"first", "second", "third"});
}
