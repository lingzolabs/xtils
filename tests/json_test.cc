#include "xtils/utils/json.h"

#include <string>
#include <system_error>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

using namespace xtils;

// Test data
namespace {

// Valid JSON test cases
std::vector<std::string> valid_json_test_cases = {
    // Basic structures
    R"({})",                // Empty object
    R"([])",                // Empty array
    R"({"key": "value"})",  // Simple key-value pair
    R"([1, 2, 3])",         // Number array

    // Data types
    R"({"int": 42, "float": 3.14})",                // Number types
    R"({"bool_true": true, "bool_false": false})",  // Boolean values
    R"({"null_value": null})",                      // null value

    // Nested structures
    R"({"person": {"name": "Alice", "age": 30}})",  // Nested object
    R"({"matrix": [[1,2], [3,4]]})",                // Nested array

    // Special characters
    R"({"escaped": "Quote: \" Slash: \\ \/ \b \f \n \r \t"})",  // Escape
                                                                // sequences
    R"({"unicode": "\u4E2D\u6587"})",  // Unicode Chinese

    // Number edge cases
    R"({"negative": -42})",                     // Negative number
    R"({"zero": 0})",                           // Zero
    R"({"decimal": 0.123})",                    // Decimal
    R"({"exp": 1.23e10})",                      // Scientific notation
    R"({"large": 999999999999999})",            // Large number
    R"({"max_int64": 9223372036854775807})",    // Max int64_t
    R"({"min_int64": -9223372036854775808})",   // Min int64_t
    R"({"precision_test": 9007199254740993})",  // Beyond JS safe integer
    R"({"zero_float": 0.0})",                   // Float zero
    R"({"negative_float": -3.14})",             // Negative float
    R"({"scientific_int": 5e2})",  // Scientific notation integer form

    // String tests
    R"({"empty": ""})",                                  // Empty string
    R"({"spaces": "   "})",                              // Space string
    R"({"special": "!@#$%^&*()_+-=[]{}|;':\"<>?,./"})",  // Special characters

    // Root value types (valid JSON)
    R"("string_root")",  // String root value
    R"(42)",             // Number root value
    R"(true)",           // Boolean root value
    R"(false)",          // Boolean root value
    R"(null)"            // null root value
};

// Invalid JSON test cases
std::vector<std::string> invalid_json_test_cases = {
    // Structure errors
    R"({)",                // Missing closing bracket
    R"({"key": "value")",  // Missing closing brace
    R"([1, 2, 3)",         // Missing closing bracket

    // Key-value errors
    R"({key: "value"})",  // Unquoted key
    R"({"key": value})",  // Unquoted string value

    // Separator errors
    R"({"a": "1", "b": "2",})",  // Trailing comma
    R"({"a":: "value"})",        // Double colon
    R"([1, 2, 3,])",             // Array trailing comma
    R"({"a": "1",, "b": "2"})",  // Double comma

    // Data type errors
    R"({"number": 12.3.4})",  // Invalid number format
    R"({"bool": tru})",       // Invalid boolean
    R"({"bool": True})",      // Uppercase boolean
    R"({"null": NULL})",      // Uppercase null
    R"({"number": 01})",      // Leading zero
    R"({"number": .5})",      // Missing integer part
    R"({"number": 5.})",      // Missing decimal part
    R"({"number": -})",       // Lone minus sign

    // String errors
    R"({"str": "unclosed})",     // Unclosed string
    R"({"escape": "\x"})",       // Invalid escape
    R"({"unicode": "\u123"})",   // Incomplete Unicode
    R"({"unicode": "\uGHIJ"})",  // Invalid Unicode hex
    R"({"newline": "
    "})",                        // Unescaped newline
    R"({"tab": "	"})",    // Unescaped tab

    // Empty content
    "",     // Completely empty
    "   ",  // Only spaces

    // Extra content
    R"({ }extra)",                  // Extra characters
    R"({"key": "value"} garbage)",  // Trailing garbage

    // Array specific errors
    R"([,])",     // Array starts with comma
    R"([1,,2])",  // Double comma in array
    R"([1 2])",   // Missing comma separator

    // Object specific errors
    R"({,})",                                // Object starts with comma
    R"({"key" "value"})",                    // Missing colon
    R"({"key":})",                           // Missing value
    R"({:"value"})",                         // Missing key
    R"({"key": "value" "key2": "value2"})",  // Missing comma separator

    // Number format errors
    R"({"number": +42})",       // Plus sign prefix
    R"({"number": 0x123})",     // Hexadecimal
    R"({"number": .123})",      // Missing leading zero
    R"({"number": 123.})",      // Missing decimal part
    R"({"number": 1e})",        // Incomplete exponent
    R"({"number": Infinity})",  // Infinity literal
    R"({"number": NaN})",       // NaN literal

    // String format errors
    R"({"str": 'single_quotes'})",  // Single quotes
    R"({"str": "multi
    line"})",                       // Unescaped multiline
    R"({"str": "\"})",              // Just backslash
    R"({"str": "\a"})",             // Invalid escape character

    // Boolean errors
    R"({"bool": TRUE})",   // Uppercase TRUE
    R"({"bool": FALSE})",  // Uppercase FALSE
    R"({"bool": yes})",    // yes/no format

    // null errors
    R"({"null": NULL})",       // Uppercase NULL
    R"({"null": nil})",        // Ruby/Go style nil
    R"({"null": undefined})",  // JavaScript undefined
};

}  // namespace

TEST_CASE("JSON Valid Parsing") {
  SUBCASE("All valid JSON should parse successfully") {
    for (const auto& json_str : valid_json_test_cases) {
      INFO("Testing JSON: " << json_str);

      auto json_opt = Json::parse(json_str);
      CHECK(json_opt.has_value());

      if (!json_opt.has_value()) {
        FAIL("Failed to parse valid JSON: " << json_str);
      }
    }
  }
}

TEST_CASE("JSON Invalid Parsing") {
  SUBCASE("All invalid JSON should fail to parse") {
    for (const auto& json_str : invalid_json_test_cases) {
      INFO("Testing invalid JSON: " << json_str);

      auto json_opt = Json::parse(json_str);
      CHECK_FALSE(json_opt.has_value());

      if (json_opt.has_value()) {
        FAIL("Should have failed to parse invalid JSON: " << json_str);
      }
    }
  }
}

TEST_CASE("JSON Data Type Access") {
  SUBCASE("Object access") {
    std::string json_str = R"({"name": "Alice", "age": 30, "active": true})";
    auto json_opt = Json::parse(json_str);

    REQUIRE(json_opt.has_value());
    auto json = json_opt.value();

    CHECK(json.is_object());
    CHECK(json["name"].as_string() == "Alice");
    CHECK(json["age"].as_integer() == 30);
    CHECK(json["active"].as_bool() == true);
  }

  SUBCASE("Array access") {
    std::string json_str = R"([1, "two", 3.14, true, null])";
    auto json_opt = Json::parse(json_str);

    REQUIRE(json_opt.has_value());
    auto json = json_opt.value();

    CHECK(json.is_array());
    CHECK(json[0].as_integer() == 1);
    CHECK(json[1].as_string() == "two");
    CHECK(json[2].as_float() == doctest::Approx(3.14));
    CHECK(json[3].as_bool() == true);
    CHECK(json[4].is_null());
  }

  SUBCASE("Nested structure access") {
    std::string json_str = R"({
            "user": {
                "name": "Bob",
                "roles": ["admin", "user"]
            }
        })";
    auto json_opt = Json::parse(json_str);

    REQUIRE(json_opt.has_value());
    auto json = json_opt.value();

    CHECK(json["user"]["name"].as_string() == "Bob");
    CHECK(json["user"]["roles"][0].as_string() == "admin");
    CHECK(json["user"]["roles"][1].as_string() == "user");
  }
}

TEST_CASE("JSON Number Type Handling") {
  SUBCASE("Integer values") {
    // Positive integer
    auto json_opt = Json::parse("42");
    REQUIRE(json_opt.has_value());
    auto json = json_opt.value();
    CHECK(json.as_integer() == 42);
    CHECK(json.as_number() == doctest::Approx(42.0));

    // Negative integer
    json_opt = Json::parse("-123");
    REQUIRE(json_opt.has_value());
    json = json_opt.value();
    CHECK(json.as_integer() == -123);

    // Zero
    json_opt = Json::parse("0");
    REQUIRE(json_opt.has_value());
    json = json_opt.value();
    CHECK(json.as_integer() == 0);
  }

  SUBCASE("Floating point values") {
    // Simple decimal
    auto json_opt = Json::parse("3.14");
    REQUIRE(json_opt.has_value());
    auto json = json_opt.value();
    CHECK(json.as_float() == doctest::Approx(3.14));

    // Scientific notation
    json_opt = Json::parse("1.23e10");
    REQUIRE(json_opt.has_value());
    json = json_opt.value();
    CHECK(json.as_float() == doctest::Approx(1.23e10));

    // Negative scientific notation
    json_opt = Json::parse("-2.5e-3");
    REQUIRE(json_opt.has_value());
    json = json_opt.value();
    CHECK(json.as_float() == doctest::Approx(-2.5e-3));
  }

  SUBCASE("Large number handling") {
    // Large positive integer
    auto json_opt = Json::parse("9223372036854775807");  // Max int64_t
    REQUIRE(json_opt.has_value());
    auto json = json_opt.value();
    CHECK(json.as_integer() == 9223372036854775807LL);

    // Large negative integer
    json_opt = Json::parse("-9223372036854775808");  // Min int64_t
    REQUIRE(json_opt.has_value());
    json = json_opt.value();
    CHECK(json.as_integer() == std::numeric_limits<int64_t>::min());
  }
}

TEST_CASE("JSON Optional Access") {
  SUBCASE("Valid key access") {
    std::string json_str = R"({"existing": "value", "number": 42})";
    auto json_opt = Json::parse(json_str);

    REQUIRE(json_opt.has_value());
    auto json = json_opt.value();

    auto opt_str = json.get_string("existing");
    CHECK(opt_str.has_value());
    CHECK(opt_str.value() == "value");

    auto opt_int = json.get_integer("number");
    CHECK(opt_int.has_value());
    CHECK(opt_int.value() == 42);
  }

  SUBCASE("Non-existent key access") {
    std::string json_str = R"({"existing": "value"})";
    auto json_opt = Json::parse(json_str);

    REQUIRE(json_opt.has_value());
    auto json = json_opt.value();

    auto opt_str = json.get_string("non_existent");
    CHECK_FALSE(opt_str.has_value());

    auto opt_int = json.get_integer("missing");
    CHECK_FALSE(opt_int.has_value());
  }

  SUBCASE("Type mismatch access") {
    std::string json_str = R"({"string_value": "hello", "int_value": 42})";
    auto json_opt = Json::parse(json_str);

    REQUIRE(json_opt.has_value());
    auto json = json_opt.value();

    // Try to get string as int
    auto opt_int = json.get_integer("string_value");
    CHECK_FALSE(opt_int.has_value());

    // Try to get int as string
    auto opt_str = json.get_string("int_value");
    CHECK_FALSE(opt_str.has_value());
  }

  SUBCASE("has_key functionality") {
    std::string json_str = R"({"key1": "value1", "key2": 42})";
    auto json_opt = Json::parse(json_str);

    REQUIRE(json_opt.has_value());
    auto json = json_opt.value();

    CHECK(json.has_key("key1"));
    CHECK(json.has_key("key2"));
    CHECK_FALSE(json.has_key("nonexistent"));
  }
}

TEST_CASE("JSON String Handling") {
  SUBCASE("Basic strings") {
    // Empty string
    auto json_opt = Json::parse(R"("")");
    REQUIRE(json_opt.has_value());
    auto json = json_opt.value();
    CHECK(json.as_string() == "");

    // Simple string
    json_opt = Json::parse(R"("hello")");
    REQUIRE(json_opt.has_value());
    json = json_opt.value();
    CHECK(json.as_string() == "hello");

    // String with spaces
    json_opt = Json::parse(R"("hello world")");
    REQUIRE(json_opt.has_value());
    json = json_opt.value();
    CHECK(json.as_string() == "hello world");
  }

  SUBCASE("Escaped characters") {
    // Quote escape
    auto json_opt = Json::parse(R"("He said \"hello\"")");
    REQUIRE(json_opt.has_value());
    auto json = json_opt.value();
    CHECK(json.as_string() == "He said \"hello\"");

    // Backslash escape
    json_opt = Json::parse(R"("C:\\path\\to\\file")");
    REQUIRE(json_opt.has_value());
    json = json_opt.value();
    CHECK(json.as_string() == "C:\\path\\to\\file");

    // Newline escape
    json_opt = Json::parse(R"("line1\nline2")");
    REQUIRE(json_opt.has_value());
    json = json_opt.value();
    CHECK(json.as_string() == "line1\nline2");

    // Tab escape
    json_opt = Json::parse(R"("col1\tcol2")");
    REQUIRE(json_opt.has_value());
    json = json_opt.value();
    CHECK(json.as_string() == "col1\tcol2");
  }

  SUBCASE("Unicode strings") {
    // Basic Unicode
    auto json_opt = Json::parse(R"("\u4E2D\u6587")");
    REQUIRE(json_opt.has_value());
    auto json = json_opt.value();
    CHECK(json.as_string() == "中文");

    // Mixed ASCII and Unicode
    json_opt = Json::parse(R"("Hello \u4E16\u754C!")");
    REQUIRE(json_opt.has_value());
    json = json_opt.value();
    CHECK(json.as_string() == "Hello 世界!");
  }
}

TEST_CASE("JSON Boolean and Null") {
  SUBCASE("Boolean values") {
    // true
    auto json_opt = Json::parse("true");
    REQUIRE(json_opt.has_value());
    auto json = json_opt.value();
    CHECK(json.is_bool());
    CHECK(json.as_bool() == true);

    // false
    json_opt = Json::parse("false");
    REQUIRE(json_opt.has_value());
    json = json_opt.value();
    CHECK(json.is_bool());
    CHECK(json.as_bool() == false);
  }

  SUBCASE("Null values") {
    auto json_opt = Json::parse("null");
    REQUIRE(json_opt.has_value());
    auto json = json_opt.value();
    CHECK(json.is_null());
  }
}

TEST_CASE("JSON Array Operations") {
  SUBCASE("Array size and indexing") {
    std::string json_str = R"([1, 2, 3, 4, 5])";
    auto json_opt = Json::parse(json_str);

    REQUIRE(json_opt.has_value());
    auto json = json_opt.value();

    CHECK(json.is_array());
    CHECK(json.size() == 5);
    CHECK_FALSE(json.empty());

    for (size_t i = 0; i < 5; ++i) {
      CHECK(json[i].as_integer() == static_cast<int64_t>(i + 1));
    }
  }

  SUBCASE("Array bounds checking") {
    std::string json_str = R"([1, 2, 3])";
    auto json_opt = Json::parse(json_str);

    REQUIRE(json_opt.has_value());
    auto json = json_opt.value();

    CHECK(json.has_index(0));
    CHECK(json.has_index(1));
    CHECK(json.has_index(2));
    CHECK_FALSE(json.has_index(3));
  }

  SUBCASE("Mixed type array") {
    std::string json_str = R"([42, "hello", true, null, 3.14])";
    auto json_opt = Json::parse(json_str);

    REQUIRE(json_opt.has_value());
    auto json = json_opt.value();

    CHECK(json[0].is_integer());
    CHECK(json[1].is_string());
    CHECK(json[2].is_bool());
    CHECK(json[3].is_null());
    CHECK(json[4].is_float());
  }
}

TEST_CASE("JSON Object Operations") {
  SUBCASE("Object size and key iteration") {
    std::string json_str = R"({"a": 1, "b": 2, "c": 3})";
    auto json_opt = Json::parse(json_str);

    REQUIRE(json_opt.has_value());
    auto json = json_opt.value();

    CHECK(json.is_object());
    CHECK(json.size() == 3);
    CHECK_FALSE(json.empty());

    CHECK(json.has_key("a"));
    CHECK(json.has_key("b"));
    CHECK(json.has_key("c"));
    CHECK_FALSE(json.has_key("d"));
  }

  SUBCASE("Empty object") {
    std::string json_str = R"({})";
    auto json_opt = Json::parse(json_str);

    REQUIRE(json_opt.has_value());
    auto json = json_opt.value();

    CHECK(json.is_object());
    CHECK(json.size() == 0);
    CHECK(json.empty());
  }
}

TEST_CASE("JSON Boundary Cases") {
  SUBCASE("Empty structures") {
    // Empty object
    auto json_opt = Json::parse("{}");
    REQUIRE(json_opt.has_value());
    auto json = json_opt.value();
    CHECK(json.is_object());
    CHECK(json.size() == 0);

    // Empty array
    json_opt = Json::parse("[]");
    REQUIRE(json_opt.has_value());
    json = json_opt.value();
    CHECK(json.is_array());
    CHECK(json.size() == 0);
  }

  SUBCASE("Deeply nested structures") {
    // Deep object nesting
    std::string deep_object = R"({"a":{"b":{"c":{"d":{"e":"deep"}}}}})";
    auto json_opt = Json::parse(deep_object);
    REQUIRE(json_opt.has_value());
    auto json = json_opt.value();
    CHECK(json["a"]["b"]["c"]["d"]["e"].as_string() == "deep");

    // Deep array nesting
    std::string deep_array = R"([[[[[5]]]]])";
    json_opt = Json::parse(deep_array);
    REQUIRE(json_opt.has_value());
    json = json_opt.value();
    CHECK(json[0][0][0][0][0].as_integer() == 5);
  }

  SUBCASE("Large structures") {
    // Large array
    std::string large_array = "[";
    for (int i = 0; i < 100; ++i) {  // Reduced size for faster testing
      if (i > 0) large_array += ",";
      large_array += std::to_string(i);
    }
    large_array += "]";

    auto json_opt = Json::parse(large_array);
    REQUIRE(json_opt.has_value());
    auto json = json_opt.value();
    CHECK(json.is_array());
    CHECK(json.size() == 100);
    CHECK(json[0].as_integer() == 0);
    CHECK(json[99].as_integer() == 99);
  }
}

TEST_CASE("JSON Type Checking") {
  SUBCASE("Type identification") {
    auto tests = std::vector<std::pair<std::string, std::string>>{
        {"null", "null"},  {"true", "bool"},  {"false", "bool"},
        {"42", "integer"}, {"3.14", "float"}, {R"("hello")", "string"},
        {"[]", "array"},   {"{}", "object"}};

    for (const auto& [json_str, expected_type] : tests) {
      INFO("Testing type for: " << json_str);
      auto json_opt = Json::parse(json_str);
      REQUIRE(json_opt.has_value());
      auto json = json_opt.value();

      if (expected_type == "null")
        CHECK(json.is_null());
      else if (expected_type == "bool")
        CHECK(json.is_bool());
      else if (expected_type == "integer")
        CHECK(json.is_integer());
      else if (expected_type == "float")
        CHECK(json.is_float());
      else if (expected_type == "string")
        CHECK(json.is_string());
      else if (expected_type == "array")
        CHECK(json.is_array());
      else if (expected_type == "object")
        CHECK(json.is_object());
    }
  }
}

TEST_CASE("JSON Real-world Examples") {
  SUBCASE("API response format") {
    std::string api_response = R"({
            "status": "success",
            "data": {
                "users": [
                    {
                        "id": 1,
                        "name": "Alice",
                        "email": "alice@example.com",
                        "active": true
                    },
                    {
                        "id": 2,
                        "name": "Bob",
                        "email": "bob@example.com",
                        "active": false
                    }
                ],
                "total": 2
            }
        })";

    auto json_opt = Json::parse(api_response);
    REQUIRE(json_opt.has_value());
    auto json = json_opt.value();

    CHECK(json["status"].as_string() == "success");
    CHECK(json["data"]["total"].as_integer() == 2);
    CHECK(json["data"]["users"][0]["name"].as_string() == "Alice");
    CHECK(json["data"]["users"][1]["active"].as_bool() == false);
  }

  SUBCASE("Configuration file format") {
    std::string config = R"({
            "server": {
                "host": "localhost",
                "port": 8080,
                "ssl": false
            },
            "database": {
                "url": "postgresql://localhost/mydb",
                "pool_size": 10,
                "timeout": 30.5
            },
            "features": ["auth", "logging", "metrics"]
        })";

    auto json_opt = Json::parse(config);
    REQUIRE(json_opt.has_value());
    auto json = json_opt.value();

    CHECK(json["server"]["host"].as_string() == "localhost");
    CHECK(json["server"]["port"].as_integer() == 8080);
    CHECK(json["server"]["ssl"].as_bool() == false);
    CHECK(json["database"]["pool_size"].as_integer() == 10);
    CHECK(json["database"]["timeout"].as_float() == doctest::Approx(30.5));
    CHECK(json["features"][0].as_string() == "auth");
    CHECK(json["features"].size() == 3);
  }
}

TEST_CASE("JSON Error Handling") {
  SUBCASE("Parse error handling") {
    // Test various invalid JSON formats
    std::vector<std::string> error_cases = {R"({invalid})",
                                            R"({"key": })",
                                            R"([1, 2, 3,])",
                                            R"({"key": "value",})",
                                            "invalid json",
                                            "",
                                            "   "};

    for (const auto& error_case : error_cases) {
      INFO("Testing error case: " << error_case);
      auto json_opt = Json::parse(error_case);
      CHECK_FALSE(json_opt.has_value());
    }
  }

  SUBCASE("Access errors") {
    std::string json_str = R"({"string": "hello", "number": 42, "bool": true})";
    auto json_opt = Json::parse(json_str);
    REQUIRE(json_opt.has_value());
    const auto json =
        json_opt.value();  // Use const to trigger throwing behavior

    // Test accessing non-existent key on const object
    CHECK_THROWS_AS(json["nonexistent"], std::runtime_error);

    // Test array access on non-array
    CHECK_THROWS_AS(json[0], std::runtime_error);

    // Test object access on array
    std::string array_str = R"([1, 2, 3])";
    json_opt = Json::parse(array_str);
    REQUIRE(json_opt.has_value());
    const auto array_json =
        json_opt.value();  // Use const to trigger throwing behavior
    CHECK_THROWS_AS(array_json["key"], std::runtime_error);
  }
}

TEST_CASE("JSON Self-Assignment Safety") {
  // Copy self-assign for every variant type — must not double-free.
  SUBCASE("null self-copy-assign") {
    Json j;
    j = j;
    CHECK(j.is_null());
  }
  SUBCASE("bool self-copy-assign") {
    Json j(true);
    j = j;
    CHECK(j.as_bool() == true);
  }
  SUBCASE("integer self-copy-assign") {
    Json j(42);
    j = j;
    CHECK(j.as_integer() == 42);
  }
  SUBCASE("float self-copy-assign") {
    Json j(3.14);
    j = j;
    CHECK(j.as_float() == 3.14);
  }
  SUBCASE("string self-copy-assign") {
    Json j(std::string("hello"));
    j = j;
    CHECK(j.as_string() == "hello");
  }
  SUBCASE("array self-copy-assign") {
    Json j(Json::array_t{Json(1), Json(2), Json(3)});
    j = j;
    CHECK(j.size() == 3);
    CHECK(j[0].as_integer() == 1);
  }
  SUBCASE("object self-copy-assign") {
    Json::object_t obj{{"k", Json("v")}};
    Json j(obj);
    j = j;
    CHECK(j["k"].as_string() == "v");
  }
  // Move self-assign — implementation guards with this != &other.
  SUBCASE("string self-move-assign") {
    Json j(std::string("abc"));
    j = std::move(j);
    // After self-move, the value must remain accessible (no double-free).
    CHECK((j.is_string() || j.is_null()));
  }
}

TEST_CASE("JSON Moved-From State") {
  // After std::move, source becomes null and is reusable.
  Json src(std::string("hello"));
  Json dst(std::move(src));
  CHECK(dst.as_string() == "hello");
  CHECK(src.is_null());
  // Reuse the moved-from object.
  src = Json(123);
  CHECK(src.as_integer() == 123);
}

TEST_CASE("JSON UTF-16 Surrogate Pairs") {
  SUBCASE("emoji round-trip via \\uD83D\\uDE00") {
    // Grinning face 😀 = U+1F600, encoded as surrogate pair D83D DE00.
    std::string text = R"("\uD83D\uDE00")";
    auto j = Json::parse(text);
    REQUIRE(j);
    REQUIRE(j->is_string());
    // The decoded string must be valid UTF-8 for U+1F600 (4 bytes).
    const std::string& s = j->as_string();
    CHECK(s.size() == 4);
    CHECK(static_cast<unsigned char>(s[0]) == 0xF0);
    CHECK(static_cast<unsigned char>(s[1]) == 0x9F);
    CHECK(static_cast<unsigned char>(s[2]) == 0x98);
    CHECK(static_cast<unsigned char>(s[3]) == 0x80);
  }
  SUBCASE("lone high surrogate is rejected") {
    std::error_code ec;
    Json::parse(R"("\uD83D")", ec);
    CHECK(ec);  // parse must fail
  }
  SUBCASE("lone low surrogate is rejected") {
    std::error_code ec;
    Json::parse(R"("\uDE00")", ec);
    CHECK(ec);
  }
  SUBCASE("BMP code point round-trip via \\u00E9") {
    // é = U+00E9 = 0xC3 0xA9 in UTF-8.
    auto j = Json::parse(R"("\u00E9")");
    REQUIRE(j);
    const std::string& s = j->as_string();
    CHECK(s.size() == 2);
    CHECK(static_cast<unsigned char>(s[0]) == 0xC3);
    CHECK(static_cast<unsigned char>(s[1]) == 0xA9);
  }
}

TEST_CASE("JSON Deep Recursion Limit") {
  // MAX_PARSE_DEPTH is 100 in the implementation. Build deeper input and
  // verify it is rejected gracefully (no crash, parse returns error).
  std::string text;
  text.reserve(300);
  for (int i = 0; i < 150; ++i) text += '[';
  for (int i = 0; i < 150; ++i) text += ']';

  std::error_code ec;
  Json::parse(text, ec);
  CHECK(ec);  // depth exceeded must error, not crash
}

TEST_CASE("JSON Mixed Round-Trip") {
  Json::object_t obj;
  obj["name"] = Json("alice");
  obj["age"] = Json(30);
  obj["active"] = Json(true);
  obj["score"] = Json(98.5);
  obj["tags"] = Json(Json::array_t{Json("a"), Json("b")});
  obj["profile"] =
      Json(Json::object_t{{"city", Json("sh")}, {"id", Json(7)}});
  Json src(obj);

  std::string serialized = src.dump();
  auto reparsed = Json::parse(serialized);
  REQUIRE(reparsed);
  CHECK((*reparsed)["name"].as_string() == "alice");
  CHECK((*reparsed)["age"].as_integer() == 30);
  CHECK((*reparsed)["active"].as_bool() == true);
  CHECK((*reparsed)["score"].as_float() == 98.5);
  CHECK((*reparsed)["tags"].size() == 2);
  CHECK((*reparsed)["profile"]["city"].as_string() == "sh");
}

// Test runner
int main() {
  doctest::Context context;

  // Configure test output
  context.setOption("order-by", "name");
  context.setOption("no-breaks", true);

  // Run tests
  int result = context.run();

  if (result == 0) {
    std::cout << "\n✅ All JSON tests passed!" << std::endl;
  } else {
    std::cout << "\n❌ Some JSON tests failed!" << std::endl;
  }

  return result;
}
