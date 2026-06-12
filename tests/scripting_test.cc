#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "xtils/scripting/binding.h"
#include "xtils/scripting/engine.h"
#include "xtils/scripting/json_interop.h"

using namespace xtils;

TEST_CASE("ScriptEngine: create and destroy") {
  ScriptEngine engine;
  CHECK(true);
}

TEST_CASE("ScriptContext: eval arithmetic") {
  ScriptEngine engine;
  auto ctx = engine.CreateContext();
  auto result = ctx->Eval("1 + 2");
  CHECK(result.IsNumber());
  CHECK(result.ToInt() == 3);
  CHECK(result.ToDouble() == 3.0);
}

TEST_CASE("ScriptContext: eval string") {
  ScriptEngine engine;
  auto ctx = engine.CreateContext();
  auto result = ctx->Eval("'hello'");
  CHECK(result.IsString());
  CHECK(result.ToString() == "hello");
}

TEST_CASE("ScriptContext: eval variables") {
  ScriptEngine engine;
  auto ctx = engine.CreateContext();
  auto result = ctx->Eval("var x = 42; x * 2");
  CHECK(result.IsNumber());
  CHECK(result.ToInt() == 84);
}

TEST_CASE("ScriptContext: eval syntax error") {
  ScriptEngine engine;
  auto ctx = engine.CreateContext();
  auto result = ctx->Eval("!!! invalid !!!");
  CHECK(result.IsException());
}

TEST_CASE("ScriptContext: RegisterFunction") {
  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  ctx->RegisterFunction(
      "add", [](ScriptContext& c, const std::vector<ScriptValue>& args) {
        int64_t a = args[0].ToInt();
        int64_t b = args[1].ToInt();
        return ToScriptValue(c, a + b);
      });

  auto result = ctx->Eval("add(10, 32)");
  CHECK(result.IsNumber());
  CHECK(result.ToInt() == 42);
}

TEST_CASE("ScriptContext: RegisterFunction returning string") {
  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  ctx->RegisterFunction(
      "greet", [](ScriptContext& c, const std::vector<ScriptValue>& args) {
        std::string name = args[0].ToString();
        return ToScriptValue(c, std::string("Hello, " + name));
      });

  auto result = ctx->Eval("greet('World')");
  CHECK(result.IsString());
  CHECK(result.ToString() == "Hello, World");
}

TEST_CASE("ScriptValue: type checks") {
  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  auto num = ctx->Eval("42");
  CHECK(num.IsNumber());
  CHECK_FALSE(num.IsString());
  CHECK_FALSE(num.IsBool());
  CHECK_FALSE(num.IsNull());
  CHECK_FALSE(num.IsUndefined());

  auto str = ctx->Eval("'test'");
  CHECK(str.IsString());

  auto b = ctx->Eval("true");
  CHECK(b.IsBool());
  CHECK(b.ToBool() == true);

  auto n = ctx->Eval("null");
  CHECK(n.IsNull());

  auto u = ctx->Eval("undefined");
  CHECK(u.IsUndefined());

  auto obj = ctx->Eval("({a: 1})");
  CHECK(obj.IsObject());
}

TEST_CASE("ScriptValue: move semantics") {
  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  ScriptValue a = ctx->Eval("100");
  CHECK(a.ToInt() == 100);

  ScriptValue b(std::move(a));
  CHECK(b.ToInt() == 100);

  ScriptValue c = ctx->Eval("200");
  b = std::move(c);
  CHECK(b.ToInt() == 200);
}

TEST_CASE("ScriptEngine: memory limit") {
  ScriptEngine engine;
  engine.SetMemoryLimit(1024 * 1024);
  auto ctx = engine.CreateContext();
  auto result = ctx->Eval("1 + 1");
  CHECK(result.IsNumber());
  CHECK(result.ToInt() == 2);
}

TEST_CASE("ScriptContext: eval empty string") {
  ScriptEngine engine;
  auto ctx = engine.CreateContext();
  auto result = ctx->Eval("");
  CHECK(result.IsUndefined());
}

TEST_CASE("ScriptContext: multiple contexts isolated") {
  ScriptEngine engine;
  auto ctx1 = engine.CreateContext();
  auto ctx2 = engine.CreateContext();

  ctx1->Eval("var x = 10");
  ctx2->Eval("var x = 20");

  auto r1 = ctx1->Eval("x");
  auto r2 = ctx2->Eval("x");
  CHECK(r1.ToInt() == 10);
  CHECK(r2.ToInt() == 20);
}

// ─────────────────────────────────────────────────────────────────────────────
// Json <-> ScriptValue Interop Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("JsonToScriptValue: primitives") {
  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  auto sv_null = JsonToScriptValue(*ctx, Json(nullptr));
  CHECK(sv_null.IsNull());

  auto sv_bool = JsonToScriptValue(*ctx, Json(true));
  CHECK(sv_bool.IsBool());
  CHECK(sv_bool.ToBool() == true);

  auto sv_int = JsonToScriptValue(*ctx, Json(42));
  CHECK(sv_int.IsNumber());
  CHECK(sv_int.ToInt() == 42);

  auto sv_float = JsonToScriptValue(*ctx, Json(3.14));
  CHECK(sv_float.IsNumber());
  CHECK(sv_float.ToDouble() == doctest::Approx(3.14));

  auto sv_str = JsonToScriptValue(*ctx, Json("hello"));
  CHECK(sv_str.IsString());
  CHECK(sv_str.ToString() == "hello");
}

TEST_CASE("JsonToScriptValue: array") {
  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  Json::array_t arr = {Json(1), Json(2), Json(3)};
  auto sv = JsonToScriptValue(*ctx, Json(arr));
  CHECK(sv.IsArray());

  // Verify via JS
  JSValue global = JS_GetGlobalObject(ctx->Raw());
  JS_SetPropertyStr(ctx->Raw(), global, "testArr",
                    JS_DupValue(ctx->Raw(), sv.Raw()));
  JS_FreeValue(ctx->Raw(), global);

  auto r = ctx->Eval("testArr.length");
  CHECK(r.ToInt() == 3);
  auto r2 = ctx->Eval("testArr[0] + testArr[1] + testArr[2]");
  CHECK(r2.ToInt() == 6);
}

TEST_CASE("JsonToScriptValue: nested object") {
  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  Json::object_t obj;
  obj["name"] = Json("Alice");
  obj["age"] = Json(30);
  obj["active"] = Json(true);
  Json::array_t tags = {Json("admin"), Json("user")};
  obj["tags"] = Json(tags);

  auto sv = JsonToScriptValue(*ctx, Json(obj));
  CHECK(sv.IsObject());

  JSValue global = JS_GetGlobalObject(ctx->Raw());
  JS_SetPropertyStr(ctx->Raw(), global, "user",
                    JS_DupValue(ctx->Raw(), sv.Raw()));
  JS_FreeValue(ctx->Raw(), global);

  auto r1 = ctx->Eval("user.name");
  CHECK(r1.ToString() == "Alice");
  auto r2 = ctx->Eval("user.age");
  CHECK(r2.ToInt() == 30);
  auto r3 = ctx->Eval("user.active");
  CHECK(r3.ToBool() == true);
  auto r4 = ctx->Eval("user.tags[1]");
  CHECK(r4.ToString() == "user");
}

TEST_CASE("ScriptValueToJson: primitives") {
  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  auto j_null = ScriptValueToJson(ctx->Eval("null"));
  CHECK(j_null.is_null());

  auto j_bool = ScriptValueToJson(ctx->Eval("true"));
  CHECK(j_bool.is_bool());
  CHECK(j_bool.as_bool() == true);

  auto j_int = ScriptValueToJson(ctx->Eval("42"));
  CHECK(j_int.is_integer());
  CHECK(j_int.as_integer() == 42);

  auto j_float = ScriptValueToJson(ctx->Eval("3.14"));
  CHECK(j_float.is_float());
  CHECK(j_float.as_float() == doctest::Approx(3.14));

  auto j_str = ScriptValueToJson(ctx->Eval("'hello'"));
  CHECK(j_str.is_string());
  CHECK(j_str.as_string() == "hello");
}

TEST_CASE("ScriptValueToJson: array and object") {
  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  auto j_arr = ScriptValueToJson(ctx->Eval("[1, 'two', true, null]"));
  CHECK(j_arr.is_array());
  CHECK(j_arr.as_array().size() == 4);
  CHECK(j_arr[0].as_integer() == 1);
  CHECK(j_arr[1].as_string() == "two");
  CHECK(j_arr[2].as_bool() == true);
  CHECK(j_arr[3].is_null());

  auto j_obj = ScriptValueToJson(
      ctx->Eval("({name: 'Bob', score: 95.5, items: [1,2,3]})"));
  CHECK(j_obj.is_object());
  CHECK(j_obj["name"].as_string() == "Bob");
  CHECK(j_obj["score"].as_float() == doctest::Approx(95.5));
  CHECK(j_obj["items"].is_array());
  CHECK(j_obj["items"].as_array().size() == 3);
}

TEST_CASE("EvalWithJson: inject and process") {
  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  Json::object_t data;
  data["name"] = Json("World");
  data["count"] = Json(3);

  auto result = EvalWithJson(*ctx, "data", Json(data),
                             "'Hello, ' + data.name + '! x' + data.count");
  CHECK(result.IsString());
  CHECK(result.ToString() == "Hello, World! x3");
}

TEST_CASE("EvalToJson: evaluate and convert") {
  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  auto j = EvalToJson(*ctx, "({x: 1, y: [2, 3], z: 'test'})");
  CHECK(j.is_object());
  CHECK(j["x"].as_integer() == 1);
  CHECK(j["y"].is_array());
  CHECK(j["y"][0].as_integer() == 2);
  CHECK(j["z"].as_string() == "test");
}

TEST_CASE("JsonParseViaJs: parse JSON string") {
  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  std::string json_str = R"({"name":"Alice","age":30,"scores":[85,92,78]})";
  auto j = JsonParseViaJs(*ctx, json_str);
  CHECK(j.is_object());
  CHECK(j["name"].as_string() == "Alice");
  CHECK(j["age"].as_integer() == 30);
  CHECK(j["scores"].is_array());
  CHECK(j["scores"][1].as_integer() == 92);
}

TEST_CASE("JsonStringifyViaJs: serialize to string") {
  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  Json::object_t obj;
  obj["a"] = Json(1);
  obj["b"] = Json("two");
  Json::array_t arr = {Json(true), Json(false)};
  obj["c"] = Json(arr);

  std::string result = JsonStringifyViaJs(*ctx, Json(obj));
  // Parse back to verify round-trip
  auto parsed = Json::parse(result);
  CHECK(parsed.has_value());
  CHECK(parsed->operator[]("a").as_integer() == 1);
  CHECK(parsed->operator[]("b").as_string() == "two");
  CHECK(parsed->operator[]("c").is_array());
}

TEST_CASE("Json <-> ScriptValue: round-trip") {
  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  // Build complex Json
  Json::object_t inner;
  inner["city"] = Json("Beijing");
  inner["zip"] = Json("100000");

  Json::object_t obj;
  obj["name"] = Json("test");
  obj["value"] = Json(42);
  obj["pi"] = Json(3.14159);
  obj["active"] = Json(true);
  obj["nothing"] = Json(nullptr);
  Json::array_t tags = {Json("a"), Json("b"), Json("c")};
  obj["tags"] = Json(tags);
  obj["address"] = Json(inner);

  Json original(obj);

  // Json → ScriptValue → Json
  ScriptValue sv = JsonToScriptValue(*ctx, original);
  Json converted = ScriptValueToJson(sv);

  CHECK(converted["name"].as_string() == "test");
  CHECK(converted["value"].as_integer() == 42);
  CHECK(converted["pi"].as_float() == doctest::Approx(3.14159));
  CHECK(converted["active"].as_bool() == true);
  CHECK(converted["nothing"].is_null());
  CHECK(converted["tags"].as_array().size() == 3);
  CHECK(converted["tags"][0].as_string() == "a");
  CHECK(converted["address"]["city"].as_string() == "Beijing");
}
