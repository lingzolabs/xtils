#include "xtils/utils/serialize.h"

#include <optional>
#include <string>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

using namespace xtils;

struct Point {
  int x = 0;
  int y = 0;
  XTILS_SERIALIZABLE(Point, x, y)
};

struct Person {
  std::string name;
  int age = 0;
  double score = 0.0;
  bool active = false;
  XTILS_SERIALIZABLE(Person, name, age, score, active)
};

struct Nested {
  std::string label;
  Point point;
  XTILS_SERIALIZABLE(Nested, label, point)
};

struct WithVector {
  std::string name;
  std::vector<int> values;
  XTILS_SERIALIZABLE(WithVector, name, values)
};

TEST_CASE("Serialize: Point to/from JSON") {
  Point p{10, 20};
  Json j = p.ToJson();

  CHECK(j["x"].as_integer() == 10);
  CHECK(j["y"].as_integer() == 20);

  auto p2 = Point::FromJson(j);
  REQUIRE(p2.has_value());
  CHECK(p2->x == 10);
  CHECK(p2->y == 20);
}

TEST_CASE("Serialize: Person to/from JSON") {
  Person person{"Alice", 30, 95.5, true};
  Json j = person.ToJson();

  CHECK(j["name"].as_string() == "Alice");
  CHECK(j["age"].as_integer() == 30);
  CHECK(j["score"].as_float() == doctest::Approx(95.5));
  CHECK(j["active"].as_bool() == true);

  auto p2 = Person::FromJson(j);
  REQUIRE(p2.has_value());
  CHECK(p2->name == "Alice");
  CHECK(p2->age == 30);
  CHECK(p2->score == doctest::Approx(95.5));
  CHECK(p2->active == true);
}

TEST_CASE("Serialize: Nested struct") {
  Nested n{"origin", {0, 0}};
  Json j = n.ToJson();

  CHECK(j["label"].as_string() == "origin");
  CHECK(j["point"]["x"].as_integer() == 0);
  CHECK(j["point"]["y"].as_integer() == 0);

  auto n2 = Nested::FromJson(j);
  REQUIRE(n2.has_value());
  CHECK(n2->label == "origin");
  CHECK(n2->point.x == 0);
  CHECK(n2->point.y == 0);
}

TEST_CASE("Serialize: Vector field") {
  WithVector w{"test", {1, 2, 3}};
  Json j = w.ToJson();

  CHECK(j["name"].as_string() == "test");
  CHECK(j["values"].as_array().size() == 3);

  auto w2 = WithVector::FromJson(j);
  REQUIRE(w2.has_value());
  CHECK(w2->name == "test");
  CHECK(w2->values.size() == 3);
  CHECK(w2->values[0] == 1);
  CHECK(w2->values[2] == 3);
}

TEST_CASE("Serialize: FromJson with invalid input") {
  auto p = Point::FromJson(Json("not an object"));
  CHECK(!p.has_value());
}

TEST_CASE("Serialize: FromJson with missing fields uses defaults") {
  Json j = Json::object();
  j["x"] = Json(static_cast<int64_t>(5));
  // y is missing — should remain default (0)

  auto p = Point::FromJson(j);
  REQUIRE(p.has_value());
  CHECK(p->x == 5);
  CHECK(p->y == 0);
}
