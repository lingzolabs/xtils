# Scripting Module

> Embedded JavaScript engine (QuickJS-NG) for dynamic scripting capabilities in C++.

## Overview

The scripting module integrates [QuickJS-NG](https://github.com/quickjs-ng/quickjs) v0.15.1 into xtils, providing:

- **ES2025 JavaScript execution** with full UTF-8/Unicode support
- **C++ ↔ JS bidirectional binding** — register native functions callable from JS
- **xtils::Json interop** — seamless conversion between `Json` and JS objects
- **Sandboxed execution** — memory limits, stack limits, context isolation
- **RAII safety** — all JS resources automatically freed via C++ destructors

## Build

The module is **opt-in** (disabled by default):

```bash
cmake -B build -DSCRIPTING_ENABLE=ON
cmake --build build
```

When disabled (`SCRIPTING_ENABLE=OFF`), zero scripting code is compiled — no QuickJS dependency, no binary size impact.

## Quick Start

```cpp
#include "xtils/scripting/engine.h"
#include "xtils/scripting/binding.h"
#include "xtils/scripting/json_interop.h"

using namespace xtils;

// 1. Create engine and context
ScriptEngine engine;
engine.SetMemoryLimit(32 * 1024 * 1024);  // 32MB sandbox
auto ctx = engine.CreateContext();

// 2. Evaluate JavaScript
auto result = ctx->Eval("1 + 2");
int64_t sum = result.ToInt();  // 3

// 3. Register C++ functions for JS to call
ctx->RegisterFunction("add",
    [](ScriptContext& c, const std::vector<ScriptValue>& args) {
      return ToScriptValue(c, args[0].ToInt() + args[1].ToInt());
    });
auto r = ctx->Eval("add(100, 200)");  // 300

// 4. Pass xtils::Json to JS, get results back as Json
Json data;
data["name"] = Json("World");
data["count"] = Json(5);
auto greeting = EvalWithJson(*ctx, "input", data,
    "'Hello, ' + input.name + '! x' + input.count");
// "Hello, World! x5"

// 5. Evaluate JS and get result as Json
Json result_json = EvalToJson(*ctx, "({x: 1, y: [2, 3]})");
result_json["x"].as_integer();  // 1
```

## API Reference

### ScriptEngine

Manages the JS runtime (memory, GC). One engine can have multiple isolated contexts.

```cpp
#include "xtils/scripting/engine.h"

ScriptEngine engine;
engine.SetMemoryLimit(64 * 1024 * 1024);  // Memory cap
engine.SetMaxStackSize(1024 * 1024);       // Stack cap
auto ctx = engine.CreateContext();          // New isolated context
engine.RunGC();                             // Force garbage collection
```

### ScriptContext

JS execution environment. Each context has its own global object (isolated).

```cpp
#include "xtils/scripting/context.h"

// Evaluate code
ScriptValue result = ctx->Eval("expression");
ScriptValue file_result = ctx->EvalFile("/path/to/script.js");

// Register native functions
ctx->RegisterFunction("name", [](ScriptContext& ctx,
    const std::vector<ScriptValue>& args) -> ScriptValue {
  // Process args, return result
  return ToScriptValue(ctx, 42);
});
```

### ScriptValue

Move-only RAII wrapper for JS values. Automatically freed on destruction.

```cpp
#include "xtils/scripting/value.h"

ScriptValue val = ctx->Eval("42");

// Type checks
val.IsNumber();     val.IsString();     val.IsBool();
val.IsNull();       val.IsUndefined();  val.IsObject();
val.IsArray();      val.IsException();

// Conversions
int64_t i = val.ToInt();
double d  = val.ToDouble();
bool b    = val.ToBool();
std::string s = val.ToString();
```

### Binding Helpers

```cpp
#include "xtils/scripting/binding.h"

// C++ → JS value creation
ToScriptValue(ctx, 42);            // int
ToScriptValue(ctx, 3.14);          // double
ToScriptValue(ctx, true);          // bool
ToScriptValue(ctx, "hello");       // string
ToScriptValue(ctx, std::string()); // string
MakeUndefined(ctx);                // undefined
MakeNull(ctx);                     // null
```

### ClassBinding<T> — register C++ classes into JS

Allows JS code to construct, hold, and call methods on real C++ objects.
The binding wires up constructors, methods (mutable and `const`),
properties (with getter / optional setter), and read-only properties via
a fluent builder.

```cpp
#include "xtils/scripting/class_binding.h"

struct Counter {
  Counter() = default;
  explicit Counter(int initial) : value_(initial) {}
  int Get() const { return value_; }
  void Set(int v) { value_ = v; }
  void Inc() { ++value_; }
  int  value_ = 0;
};

ClassBinding<Counter>::Define(*ctx, "Counter")
    .DefaultConstructor()
    .Constructor<int>()                          // ctor(int)
    .Method("inc", &Counter::Inc)
    .Method("get", &Counter::Get)                // const method
    .Property("value", &Counter::Get, &Counter::Set)
    .PropertyReadonly("snapshot", &Counter::Get) // getter only
    .Register();

ctx->Eval(R"JS(
  const c = new Counter(10);
  c.inc();
  c.value = c.value + 1;       // setter
  console.log(c.snapshot);     // 12
)JS");
```

Builder API (after `ClassBinding<T>::Define(ctx, jsName)`):

| Method | Purpose |
|--------|---------|
| `.Constructor<Args...>()` | Register a constructor `T(Args...)` |
| `.DefaultConstructor()` | Register `T()` |
| `.Method(name, &T::Foo)` | Register a non-const member function |
| `.Method(name, &T::Bar /* const */)` | Register a `const` member function |
| `.Method(name, arg_count, fn)` | Register a custom JS-callable lambda |
| `.Property(name, getter, setter)` | Read/write property |
| `.PropertyReadonly(name, getter)` | Read-only property |
| `.Register()` | Finalize and install the class on the context |

Unwrapping a JS instance back to a C++ pointer is supported via the
helpers in `class_binding.h` (used internally by method dispatch). The
class must already have been registered before that helper is called.

### Json Interop

Seamless conversion between `xtils::Json` and JS objects.

```cpp
#include "xtils/scripting/json_interop.h"

// Json → ScriptValue (inject C++ data into JS)
Json data = ...;
ScriptValue js_val = JsonToScriptValue(*ctx, data);

// ScriptValue → Json (extract JS results to C++)
ScriptValue js_result = ctx->Eval("({key: 'value'})");
Json json_result = ScriptValueToJson(js_result);

// Convenience: inject Json as global variable and eval
auto result = EvalWithJson(*ctx, "data", my_json, "data.name.toUpperCase()");

// Convenience: eval and get result as Json
Json j = EvalToJson(*ctx, "({x: 1, y: [2, 3]})");

// Fast JSON parsing via QuickJS (2.5x faster than Json::parse)
Json parsed = JsonParseViaJs(*ctx, raw_json_string);

// Stringify via JS (with optional pretty-printing)
std::string str = JsonStringifyViaJs(*ctx, json_obj, 2);  // indent=2
```

## Use Cases

### 1. Hot-Reloadable Business Rules

```cpp
ScriptEngine engine;
auto ctx = engine.CreateContext();

// Load rules from file (can be updated at runtime)
ctx->EvalFile("rules/discount.js");

// Apply rules to C++ data
Json order;
order["amount"] = Json(1500);
order["tier"] = Json("gold");
auto discount = EvalWithJson(*ctx, "order", order,
    "calculateDiscount(order)");
```

### 2. Plugin System

```cpp
// Expose C++ API to JS plugins
ctx->RegisterFunction("httpGet",
    [](ScriptContext& c, const std::vector<ScriptValue>& args) {
      std::string url = args[0].ToString();
      // ... perform HTTP request ...
      return ToScriptValue(c, response_body);
    });

// Load user plugin
ctx->EvalFile("plugins/my_plugin.js");
```

### 3. Config-as-Code

```cpp
// Instead of static JSON config, use JS for dynamic configuration
auto config = EvalToJson(*ctx, R"JS(
  const env = 'production';
  ({
    port: env === 'production' ? 443 : 8080,
    workers: require_os_cpus(),
    features: ['auth', 'cache', env === 'production' && 'monitoring'].filter(Boolean),
  })
)JS");
```

### 4. Data Transformation Pipeline

```cpp
// Inject raw data, transform via JS, get structured result
Json raw_data = LoadFromDatabase();
Json result = EvalWithJson(*ctx, "raw", raw_data, R"JS(
  raw.filter(r => r.active)
     .map(r => ({id: r.id, score: r.value * r.weight}))
     .sort((a, b) => b.score - a.score)
     .slice(0, 10)
)JS");
// result is xtils::Json ready for C++ consumption
```

## Performance Characteristics

| Operation | Latency | Notes |
|-----------|---------|-------|
| Engine+Context startup | ~137 µs | Fast enough for per-request creation |
| Simple Eval (`1+1`) | ~1.2 µs | Negligible overhead |
| Native function call from JS | ~72 ns | 36M calls/sec throughput |
| JSON.parse (via JS) | 2.5x faster than C++ `Json::parse` | QuickJS's optimized parser |
| Json::dump (C++) | 6x faster than JS `JSON.stringify` | Direct memory serialization |
| Object field access | C++ 1.8x faster | Already-parsed data stays in C++ |
| Regex (JS vs std::regex) | JS 1.5-1.8x faster | `std::regex` is notoriously slow |

### When to Use JS vs C++

| Scenario | Recommendation |
|----------|---------------|
| Parse incoming JSON | **JS** `JsonParseViaJs()` — faster |
| Serialize to JSON | **C++** `Json::dump()` — 6x faster |
| Iterate parsed data | **C++** — direct memory access |
| Regex matching/replace | **JS** — faster than `std::regex` |
| Dynamic rules/logic | **JS** — hot-reloadable |
| Compute-intensive loops | **C++** — 50x faster for pure math |
| Data transformation | **JS** — expressive, one-liners |

## Version

- **QuickJS-NG**: v0.15.1 (ES2025, MIT License)
- **CMake target**: `qjs` (static library, fetched via FetchContent)
- **Compile guard**: `XTILS_HAS_SCRIPTING=1` (automatically defined when enabled)

## Headers

| Header | Description |
|--------|-------------|
| `xtils/scripting/engine.h` | `ScriptEngine` — runtime management |
| `xtils/scripting/context.h` | `ScriptContext` — eval, function registration |
| `xtils/scripting/value.h` | `ScriptValue` — RAII value wrapper |
| `xtils/scripting/binding.h` | `ToScriptValue`, `MakeUndefined`, `MakeNull` |
| `xtils/scripting/class_binding.h` | `ClassBinding<T>` — register C++ classes (constructors, methods, properties) |
| `xtils/scripting/json_interop.h` | `JsonToScriptValue`, `ScriptValueToJson`, `EvalWithJson`, etc. |
