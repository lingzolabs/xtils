#include "xtils/utils/clock.h"

#include <thread>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

TEST_CASE("RealClock returns increasing values") {
  auto* clock = xtils::RealClock::Instance();

  uint64_t steady1 = clock->SteadyNowMs();
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  uint64_t steady2 = clock->SteadyNowMs();
  CHECK(steady2 >= steady1);

  uint64_t system1 = clock->SystemNowMs();
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  uint64_t system2 = clock->SystemNowMs();
  CHECK(system2 >= system1);
}

TEST_CASE("FakeClock starts at configured values") {
  xtils::FakeClock fake;
  CHECK(fake.SteadyNowMs() == 0);
  CHECK(fake.SystemNowMs() == 1000000000000ULL);
}

TEST_CASE("FakeClock::Advance works") {
  xtils::FakeClock fake;

  fake.Advance(100);
  CHECK(fake.SteadyNowMs() == 100);
  CHECK(fake.SystemNowMs() == 1000000000100ULL);

  fake.Advance(50);
  CHECK(fake.SteadyNowMs() == 150);
  CHECK(fake.SystemNowMs() == 1000000000150ULL);
}

TEST_CASE("FakeClock::SetSteady and SetSystem work") {
  xtils::FakeClock fake;

  fake.SetSteady(5000);
  CHECK(fake.SteadyNowMs() == 5000);
  CHECK(fake.SystemNowMs() == 1000000000000ULL);  // unchanged

  fake.SetSystem(2000000000000ULL);
  CHECK(fake.SystemNowMs() == 2000000000000ULL);
  CHECK(fake.SteadyNowMs() == 5000);  // unchanged
}

TEST_CASE("Multiple reads between Advance return same value") {
  xtils::FakeClock fake;
  fake.Advance(42);

  uint64_t a = fake.SteadyNowMs();
  uint64_t b = fake.SteadyNowMs();
  uint64_t c = fake.SteadyNowMs();
  CHECK(a == 42);
  CHECK(b == 42);
  CHECK(c == 42);

  uint64_t sa = fake.SystemNowMs();
  uint64_t sb = fake.SystemNowMs();
  CHECK(sa == sb);
}
