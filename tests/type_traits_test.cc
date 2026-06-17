#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <string_view>

#include "xtils/utils/type_traits.h"

using namespace xtils;

namespace foo {
struct Bar {};
}  // namespace foo

TEST_CASE("type_name: built-in types") {
  // Compiler-dependent exact text; just sanity-check non-empty + contains
  // something recognisable.
  auto name = type_name<int>();
  CHECK(!name.empty());
  CHECK(name.find("int") != std::string_view::npos);
}

TEST_CASE("type_name: user-defined struct includes the type name") {
  auto name = type_name<foo::Bar>();
  CHECK(name.find("Bar") != std::string_view::npos);
}

TEST_CASE("pretty_name: strips cv-ref qualifiers") {
  auto a = pretty_name<const int&>();
  auto b = pretty_name<int>();
  CHECK(a == b);
}

TEST_CASE("type_name_cstr: returns null-terminated C string") {
  const char* s = type_name_cstr<int>();
  CHECK(s != nullptr);
  CHECK(s[0] != '\0');
}
