#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "xtils/scripting/binding.h"
#include "xtils/scripting/engine.h"

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
