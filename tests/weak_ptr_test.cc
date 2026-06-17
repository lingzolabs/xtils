#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "xtils/utils/weak_ptr.h"

using namespace xtils;

namespace {
struct Owner {
  Owner() : weak_factory(this) {}
  WeakPtr<Owner> GetWeak() { return weak_factory.GetWeakPtr(); }
  int payload = 7;
  WeakPtrFactory<Owner> weak_factory;
};
}  // namespace

TEST_CASE("WeakPtr: valid while owner alive") {
  auto owner = std::make_unique<Owner>();
  WeakPtr<Owner> w = owner->GetWeak();
  CHECK(w);
  CHECK(w.get() == owner.get());
  CHECK(w->payload == 7);
}

TEST_CASE("WeakPtr: invalidates after owner destruction") {
  WeakPtr<Owner> w;
  {
    auto owner = std::make_unique<Owner>();
    w = owner->GetWeak();
    CHECK(w);
  }
  CHECK_FALSE(w);
  CHECK(w.get() == nullptr);
}

TEST_CASE("WeakPtr: copy is independent") {
  auto owner = std::make_unique<Owner>();
  WeakPtr<Owner> w1 = owner->GetWeak();
  WeakPtr<Owner> w2 = w1;
  CHECK(w1);
  CHECK(w2);
  owner.reset();
  CHECK_FALSE(w1);
  CHECK_FALSE(w2);
}

TEST_CASE("WeakPtrFactory::Reset rebinds to a new owner") {
  Owner a;
  Owner b;
  a.weak_factory.Reset(&b);
  WeakPtr<Owner> w = a.weak_factory.GetWeakPtr();
  CHECK(w.get() == &b);
}
