#include "xtils/utils/result.h"

#include <string>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

using namespace xtils;

TEST_CASE("Result: success value") {
  Result<int> r = 42;
  CHECK(r.ok());
  CHECK(static_cast<bool>(r));
  CHECK(*r == 42);
  CHECK(r.value() == 42);
}

TEST_CASE("Result: error value") {
  Result<int> r = Err("something failed");
  CHECK(!r.ok());
  CHECK(!static_cast<bool>(r));
  CHECK(r.error().message == "something failed");
}

TEST_CASE("Result: error with code") {
  Result<int> r = Err(404, "not found");
  CHECK(!r.ok());
  CHECK(r.error().code == 404);
  CHECK(r.error().message == "not found");
}

TEST_CASE("Result: value_or") {
  Result<int> ok = 10;
  Result<int> err = Err("fail");

  CHECK(ok.value_or(99) == 10);
  CHECK(err.value_or(99) == 99);
}

TEST_CASE("Result: map") {
  Result<int> r = 21;
  auto doubled = r.map([](int v) { return v * 2; });
  CHECK(doubled.ok());
  CHECK(*doubled == 42);
}

TEST_CASE("Result: map propagates error") {
  Result<int> r = Err("fail");
  auto doubled = r.map([](int v) { return v * 2; });
  CHECK(!doubled.ok());
  CHECK(doubled.error().message == "fail");
}

TEST_CASE("Result: and_then") {
  auto parse = [](int v) -> Result<std::string> {
    if (v > 0) return std::to_string(v);
    return Err("non-positive");
  };

  Result<int> good = 42;
  auto r1 = good.and_then(parse);
  CHECK(r1.ok());
  CHECK(*r1 == "42");

  Result<int> bad = Err("upstream");
  auto r2 = bad.and_then(parse);
  CHECK(!r2.ok());
  CHECK(r2.error().message == "upstream");
}

TEST_CASE("Result<void>: success") {
  Result<void> r = Ok();
  CHECK(r.ok());
}

TEST_CASE("Result<void>: error") {
  Result<void> r = Err("void error");
  CHECK(!r.ok());
  CHECK(r.error().message == "void error");
}

TEST_CASE("Result: Ok helper") {
  auto r = Ok(std::string("hello"));
  CHECK(r.ok());
  CHECK(*r == "hello");
}

TEST_CASE("Result: with string type") {
  auto fn = [](bool succeed) -> Result<std::string> {
    if (succeed) return std::string("data");
    return Err("failed to load");
  };

  auto r1 = fn(true);
  CHECK(r1.ok());
  CHECK(*r1 == "data");

  auto r2 = fn(false);
  CHECK(!r2.ok());
  CHECK(r2.error().message == "failed to load");
}

TEST_CASE("Result: with vector type") {
  auto fn = []() -> Result<std::vector<int>> {
    return std::vector<int>{1, 2, 3};
  };

  auto r = fn();
  CHECK(r.ok());
  CHECK(r->size() == 3);
  CHECK((*r)[0] == 1);
}

TEST_CASE("Result: is_err() symmetry") {
  Result<int> ok = Ok(1);
  CHECK(ok.ok());
  CHECK_FALSE(ok.is_err());

  Result<int> bad = Err("nope");
  CHECK_FALSE(bad.ok());
  CHECK(bad.is_err());
}

TEST_CASE("Result: unwrap_or_else") {
  Result<int> ok = Ok(7);
  CHECK(ok.unwrap_or_else([](const Error&) { return 99; }) == 7);

  Result<int> bad = Err(42, "fail");
  int recovered = bad.unwrap_or_else(
      [](const Error& e) { return e.code * 10; });
  CHECK(recovered == 420);
}

TEST_CASE("Result: expect on success returns value") {
  Result<int> ok = Ok(123);
  CHECK(ok.expect("must be ok") == 123);
}

TEST_CASE("Result<void>: is_err()") {
  Result<void> ok;
  CHECK(ok.ok());
  CHECK_FALSE(ok.is_err());

  Result<void> bad = Err("nope");
  CHECK_FALSE(bad.ok());
  CHECK(bad.is_err());
}
