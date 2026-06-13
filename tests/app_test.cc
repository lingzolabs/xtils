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
