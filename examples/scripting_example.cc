// Scripting module example: functionality demo + performance benchmark
//
// Demonstrates:
// 1. Basic JS evaluation
// 2. C++ <-> JS function binding
// 3. Multiple isolated contexts
// 4. EvalFile from disk
// 5. Performance benchmarks (startup, eval, function calls, GC)

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <numeric>
#include <string>
#include <vector>

#include "xtils/scripting/binding.h"
#include "xtils/scripting/engine.h"

using namespace xtils;
using Clock = std::chrono::high_resolution_clock;
using Ms = std::chrono::duration<double, std::milli>;
using Us = std::chrono::duration<double, std::micro>;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static void PrintHeader(const char* title) {
  printf("\n═══════════════════════════════════════════════════════════\n");
  printf("  %s\n", title);
  printf("═══════════════════════════════════════════════════════════\n\n");
}

static void PrintSection(const char* title) {
  printf("\n── %s ──\n", title);
}

// ─────────────────────────────────────────────────────────────────────────────
// 1. Functionality Demo
// ─────────────────────────────────────────────────────────────────────────────

static void DemoBasicEval() {
  PrintSection("Basic Eval");

  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  // Arithmetic
  auto r1 = ctx->Eval("2 ** 10");
  printf("  2^10 = %lld\n", (long long)r1.ToInt());

  // String operations
  auto r2 = ctx->Eval("'Hello' + ' ' + 'QuickJS!'");
  printf("  String concat: %s\n", r2.ToString().c_str());

  // Array operations
  auto r3 = ctx->Eval("[1,2,3,4,5].reduce((a,b) => a+b, 0)");
  printf("  Array reduce [1..5] sum = %lld\n", (long long)r3.ToInt());

  // Object / JSON
  auto r4 = ctx->Eval(
      "JSON.stringify({name: 'xtils', version: '1.0', scripting: true})");
  printf("  JSON: %s\n", r4.ToString().c_str());

  // ES2020+ features
  auto r5 = ctx->Eval("const x = null; x?.foo?.bar ?? 'default'");
  printf("  Optional chaining: %s\n", r5.ToString().c_str());

  // Template literals
  auto r6 = ctx->Eval("`PI ≈ ${Math.PI.toFixed(6)}`");
  printf("  Template literal: %s\n", r6.ToString().c_str());
}

static void DemoCppBinding() {
  PrintSection("C++ <-> JS Binding");

  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  // Register a native fibonacci function
  ctx->RegisterFunction(
      "nativeFib", [](ScriptContext& c, const std::vector<ScriptValue>& args) {
        int64_t n = args[0].ToInt();
        int64_t a = 0, b = 1;
        for (int64_t i = 0; i < n; ++i) {
          int64_t t = a + b;
          a = b;
          b = t;
        }
        return ToScriptValue(c, a);
      });

  // Register a native print function
  ctx->RegisterFunction(
      "print", [](ScriptContext& c, const std::vector<ScriptValue>& args) {
        for (size_t i = 0; i < args.size(); ++i) {
          if (i > 0) printf(" ");
          printf("%s", args[i].ToString().c_str());
        }
        printf("\n");
        return MakeUndefined(c);
      });

  // Register a function that returns multiple values via object
  ctx->RegisterFunction(
      "getSystemInfo",
      [](ScriptContext& c, const std::vector<ScriptValue>& args) {
        (void)args;
        return c.Eval(
            "({platform: 'linux', arch: 'x86_64', "
            "cpus: 8, memory_mb: 16384})");
      });

  // Call native fibonacci from JS
  auto r1 = ctx->Eval("nativeFib(50)");
  printf("  nativeFib(50) = %lld\n", (long long)r1.ToInt());

  // JS calling native print
  ctx->Eval("print('  [JS] Hello from JavaScript via native print!')");

  // JS calling getSystemInfo
  auto r2 = ctx->Eval("JSON.stringify(getSystemInfo())");
  printf("  System info: %s\n", r2.ToString().c_str());

  // JS function calling native function in a loop
  auto r3 = ctx->Eval(
      "let results = [];"
      "for (let i = 1; i <= 10; i++) results.push(nativeFib(i));"
      "results.join(', ')");
  printf("  Fib 1..10: %s\n", r3.ToString().c_str());
}

static void DemoIsolation() {
  PrintSection("Context Isolation");

  ScriptEngine engine;
  auto ctx1 = engine.CreateContext();
  auto ctx2 = engine.CreateContext();

  ctx1->Eval("var secret = 'context1_secret'");
  ctx2->Eval("var secret = 'context2_secret'");

  auto r1 = ctx1->Eval("secret");
  auto r2 = ctx2->Eval("secret");

  printf("  Context 1 secret: %s\n", r1.ToString().c_str());
  printf("  Context 2 secret: %s\n", r2.ToString().c_str());
  printf("  Isolation verified: %s\n",
         (r1.ToString() != r2.ToString()) ? "YES ✓" : "NO ✗");
}

static void DemoEvalFile() {
  PrintSection("EvalFile");

  // Write a temp JS file
  const char* path = "/tmp/xtils_scripting_example.js";
  {
    std::ofstream f(path);
    f << "// Example script loaded from file\n"
      << "function factorial(n) {\n"
      << "  if (n <= 1) return 1;\n"
      << "  return n * factorial(n - 1);\n"
      << "}\n"
      << "\n"
      << "function range(start, end) {\n"
      << "  let arr = [];\n"
      << "  for (let i = start; i <= end; i++) arr.push(i);\n"
      << "  return arr;\n"
      << "}\n"
      << "\n"
      << "// Return result\n"
      << "range(1, 10).map(factorial).join(', ');\n";
  }

  ScriptEngine engine;
  auto ctx = engine.CreateContext();
  auto result = ctx->EvalFile(path);
  printf("  Loaded: %s\n", path);
  printf("  factorial(1..10): %s\n", result.ToString().c_str());
}

static void DemoErrorHandling() {
  PrintSection("Error Handling");

  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  // Syntax error
  auto r1 = ctx->Eval("function( {{{");
  printf("  Syntax error caught: %s\n",
         r1.IsException() ? "YES ✓" : "NO ✗");

  // Runtime error
  auto r2 = ctx->Eval("undefinedVariable.property");
  printf("  Runtime error caught: %s\n",
         r2.IsException() ? "YES ✓" : "NO ✗");

  // Type error
  auto r3 = ctx->Eval("null.toString()");
  printf("  Type error caught: %s\n",
         r3.IsException() ? "YES ✓" : "NO ✗");
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. Performance Benchmark
// ─────────────────────────────────────────────────────────────────────────────

static void BenchEngineStartup(int iterations) {
  PrintSection("Benchmark: Engine + Context Startup");

  std::vector<double> times;
  times.reserve(iterations);

  for (int i = 0; i < iterations; ++i) {
    auto t0 = Clock::now();
    ScriptEngine engine;
    auto ctx = engine.CreateContext();
    // Ensure context is usable
    ctx->Eval("1");
    auto t1 = Clock::now();
    times.push_back(Us(t1 - t0).count());
  }

  double avg = std::accumulate(times.begin(), times.end(), 0.0) / times.size();
  double min_t = *std::min_element(times.begin(), times.end());
  double max_t = *std::max_element(times.begin(), times.end());

  printf("  Iterations: %d\n", iterations);
  printf("  Avg: %.1f µs | Min: %.1f µs | Max: %.1f µs\n", avg, min_t, max_t);
}

static void BenchEvalSimple(int iterations) {
  PrintSection("Benchmark: Simple Eval (1+1)");

  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  // Warmup
  for (int i = 0; i < 100; ++i) ctx->Eval("1+1");

  auto t0 = Clock::now();
  for (int i = 0; i < iterations; ++i) {
    auto r = ctx->Eval("1+1");
    (void)r;
  }
  auto t1 = Clock::now();

  double total_us = Us(t1 - t0).count();
  printf("  Iterations: %d\n", iterations);
  printf("  Total: %.2f ms | Per-eval: %.2f µs\n", total_us / 1000.0,
         total_us / iterations);
}

static void BenchNativeFunctionCall(int iterations) {
  PrintSection("Benchmark: Native Function Call from JS");

  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  int64_t call_count = 0;
  ctx->RegisterFunction(
      "inc", [&call_count](ScriptContext& c,
                           const std::vector<ScriptValue>& args) {
        (void)args;
        ++call_count;
        return ToScriptValue(c, call_count);
      });

  // Warmup
  ctx->Eval("for(let i=0;i<100;i++) inc()");
  call_count = 0;

  std::string code = "for(let i=0;i<" + std::to_string(iterations) +
                     ";i++) inc(); inc()";

  auto t0 = Clock::now();
  auto r = ctx->Eval(code);
  auto t1 = Clock::now();

  double total_us = Us(t1 - t0).count();
  printf("  Iterations: %d (verified: %lld)\n", iterations,
         (long long)r.ToInt());
  printf("  Total: %.2f ms | Per-call: %.3f µs\n", total_us / 1000.0,
         total_us / iterations);
}

static void BenchFibonacciJS(int n, int iterations) {
  PrintSection("Benchmark: JS Fibonacci (recursive)");

  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  ctx->Eval(
      "function fib(n) {"
      "  if (n <= 1) return n;"
      "  return fib(n-1) + fib(n-2);"
      "}");

  // Warmup
  ctx->Eval("fib(20)");

  std::string code = "fib(" + std::to_string(n) + ")";

  std::vector<double> times;
  times.reserve(iterations);

  int64_t result = 0;
  for (int i = 0; i < iterations; ++i) {
    auto t0 = Clock::now();
    auto r = ctx->Eval(code);
    auto t1 = Clock::now();
    result = r.ToInt();
    times.push_back(Ms(t1 - t0).count());
  }

  double avg = std::accumulate(times.begin(), times.end(), 0.0) / times.size();
  double min_t = *std::min_element(times.begin(), times.end());

  printf("  fib(%d) = %lld\n", n, (long long)result);
  printf("  Iterations: %d\n", iterations);
  printf("  Avg: %.3f ms | Best: %.3f ms\n", avg, min_t);
}

static void BenchStringProcessing(int iterations) {
  PrintSection("Benchmark: String Processing");

  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  std::string code =
      "let s = 'hello world';"
      "for (let i = 0; i < " +
      std::to_string(iterations) +
      "; i++) {"
      "  s = s.split('').reverse().join('');"
      "}"
      "s";

  auto t0 = Clock::now();
  auto r = ctx->Eval(code);
  auto t1 = Clock::now();

  double total_ms = Ms(t1 - t0).count();
  printf("  Iterations: %d (reverse string)\n", iterations);
  printf("  Total: %.2f ms | Per-op: %.3f µs\n", total_ms,
         (total_ms * 1000.0) / iterations);
  printf("  Result: %s\n", r.ToString().c_str());
}

static void BenchObjectCreation(int iterations) {
  PrintSection("Benchmark: Object Creation & GC");

  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  std::string code =
      "let arr = [];"
      "for (let i = 0; i < " +
      std::to_string(iterations) +
      "; i++) {"
      "  arr.push({x: i, y: i*2, name: 'obj_'+i});"
      "}"
      "arr.length";

  auto t0 = Clock::now();
  auto r = ctx->Eval(code);
  auto t1 = Clock::now();

  double total_ms = Ms(t1 - t0).count();
  printf("  Objects created: %lld\n", (long long)r.ToInt());
  printf("  Total: %.2f ms | Per-object: %.3f µs\n", total_ms,
         (total_ms * 1000.0) / iterations);

  // GC benchmark
  auto t2 = Clock::now();
  engine.RunGC();
  auto t3 = Clock::now();
  printf("  GC time: %.3f ms\n", Ms(t3 - t2).count());
}

static void BenchMemoryLimit() {
  PrintSection("Benchmark: Memory Limit Enforcement");

  ScriptEngine engine;
  engine.SetMemoryLimit(2 * 1024 * 1024);  // 2MB
  auto ctx = engine.CreateContext();

  // Try to allocate a lot of memory
  auto r = ctx->Eval(
      "let arr = [];"
      "let count = 0;"
      "try {"
      "  while(true) { arr.push(new Array(1024).fill(0)); count++; }"
      "} catch(e) {"
      "  'OOM after ' + count + ' allocations: ' + e.message;"
      "}");

  printf("  Memory limit: 2 MB\n");
  printf("  Result: %s\n", r.ToString().c_str());
}

static void BenchCompareNativeVsJS() {
  PrintSection("Benchmark: Native C++ vs JS (fibonacci)");

  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  const int N = 35;

  // JS implementation
  ctx->Eval(
      "function jsFib(n) {"
      "  if (n <= 1) return n;"
      "  return jsFib(n-1) + jsFib(n-2);"
      "}");

  // Native C++ implementation registered as JS function
  ctx->RegisterFunction(
      "nativeFib", [](ScriptContext& c, const std::vector<ScriptValue>& args) {
        std::function<int64_t(int64_t)> fib = [&](int64_t n) -> int64_t {
          if (n <= 1) return n;
          return fib(n - 1) + fib(n - 2);
        };
        return ToScriptValue(c, fib(args[0].ToInt()));
      });

  // Benchmark JS fib
  std::string js_code = "jsFib(" + std::to_string(N) + ")";
  auto t0 = Clock::now();
  auto r1 = ctx->Eval(js_code);
  auto t1 = Clock::now();
  double js_ms = Ms(t1 - t0).count();

  // Benchmark native fib called from JS
  std::string native_code = "nativeFib(" + std::to_string(N) + ")";
  auto t2 = Clock::now();
  auto r2 = ctx->Eval(native_code);
  auto t3 = Clock::now();
  double native_ms = Ms(t3 - t2).count();

  printf("  fib(%d):\n", N);
  printf("    JS result:     %lld (%.2f ms)\n", (long long)r1.ToInt(), js_ms);
  printf("    Native result: %lld (%.2f ms)\n", (long long)r2.ToInt(),
         native_ms);
  printf("    Speedup:       %.1fx (native/JS)\n", js_ms / native_ms);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
  // ─── Functionality Demo ───
  PrintHeader("FUNCTIONALITY DEMO");
  DemoBasicEval();
  DemoCppBinding();
  DemoIsolation();
  DemoEvalFile();
  DemoErrorHandling();

  // ─── Performance Benchmark ───
  PrintHeader("PERFORMANCE BENCHMARK");
  BenchEngineStartup(1000);
  BenchEvalSimple(100000);
  BenchNativeFunctionCall(100000);
  BenchFibonacciJS(30, 10);
  BenchStringProcessing(100000);
  BenchObjectCreation(100000);
  BenchMemoryLimit();
  BenchCompareNativeVsJS();

  PrintHeader("DONE");
  return 0;
}
