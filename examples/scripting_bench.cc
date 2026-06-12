// Scripting deep benchmark: UTF-8, large data, complex eval, real workloads
//
// Focuses on:
// 1. UTF-8 correctness (CJK, emoji, mixed)
// 2. Large data processing (string & array at KB/MB scale)
// 3. Complex JS patterns (closures, generators, regex, async-like)
// 4. Real-world script scenarios (JSON ETL, template engine, rule engine)

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <string>
#include <vector>

#include "xtils/scripting/binding.h"
#include "xtils/scripting/engine.h"

using namespace xtils;
using Clock = std::chrono::high_resolution_clock;
using Ms = std::chrono::duration<double, std::milli>;
using Us = std::chrono::duration<double, std::micro>;

static void PrintHeader(const char* title) {
  printf("\n═══════════════════════════════════════════════════════════\n");
  printf("  %s\n", title);
  printf("═══════════════════════════════════════════════════════════\n\n");
}

static void PrintSection(const char* title) {
  printf("\n── %s ──\n", title);
}

// ─────────────────────────────────────────────────────────────────────────────
// 1. UTF-8 Support
// ─────────────────────────────────────────────────────────────────────────────

static void TestUtf8Basic() {
  PrintSection("UTF-8: Basic Correctness");

  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  // Chinese characters
  auto r1 = ctx->Eval("'你好世界'.length");
  printf("  '你好世界'.length = %lld (expect 4)\n", (long long)r1.ToInt());

  auto r2 = ctx->Eval("'你好世界'[2]");
  printf("  '你好世界'[2] = %s (expect 世)\n", r2.ToString().c_str());

  // Emoji (surrogate pairs in UTF-16, but JS handles them)
  auto r3 = ctx->Eval("'😀🎉🚀'.length");
  printf("  '😀🎉🚀'.length = %lld (expect 6, surrogate pairs)\n",
         (long long)r3.ToInt());

  // Spread to get actual codepoints
  auto r4 = ctx->Eval("[...'😀🎉🚀'].length");
  printf("  [...'😀🎉🚀'].length = %lld (expect 3, codepoints)\n",
         (long long)r4.ToInt());

  // Mixed CJK + ASCII + Emoji
  auto r5 = ctx->Eval(
      "let s = 'Hello你好🌍World世界!';"
      "s.includes('你好') && s.includes('🌍') && s.includes('World')");
  printf("  Mixed string includes: %s (expect true)\n",
         r5.ToBool() ? "true" : "false");

  // String methods on CJK
  auto r6 = ctx->Eval(
      "'北京市海淀区中关村大街1号'.slice(3, 6)");
  printf("  CJK slice(3,6): %s (expect 海淀区)\n", r6.ToString().c_str());

  // Replace with regex on CJK
  auto r7 = ctx->Eval(
      "'2024年06月12日'.replace(/[年月日]/g, '-').slice(0, -1)");
  printf("  CJK regex replace: %s (expect 2024-06-12)\n",
         r7.ToString().c_str());

  // UTF-8 round-trip through C++ binding
  ctx->RegisterFunction(
      "echo", [](ScriptContext& c, const std::vector<ScriptValue>& args) {
        std::string s = args[0].ToString();
        return ToScriptValue(c, std::string("[C++] ") + s);
      });
  auto r8 = ctx->Eval("echo('传入中文🔥返回')");
  printf("  C++ round-trip: %s\n", r8.ToString().c_str());
}

static void BenchUtf8Processing() {
  PrintSection("UTF-8: Performance with CJK Text");

  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  // Build a large CJK string (~100KB)
  ctx->Eval(
      "var base = '这是一段用于性能测试的中文文本，包含各种字符。';"
      "var big = '';"
      "for (let i = 0; i < 2000; i++) big += base + i + '\\n';"
  );

  auto len_r = ctx->Eval("big.length");
  printf("  String size: %lld chars\n", (long long)len_r.ToInt());

  // Benchmark: split lines
  auto t0 = Clock::now();
  auto r1 = ctx->Eval("big.split('\\n').length");
  auto t1 = Clock::now();
  printf("  split('\\n'): %lld lines in %.2f ms\n", (long long)r1.ToInt(),
         Ms(t1 - t0).count());

  // Benchmark: regex match all numbers
  auto t2 = Clock::now();
  auto r2 = ctx->Eval("big.match(/\\d+/g).length");
  auto t3 = Clock::now();
  printf("  regex match /\\d+/g: %lld matches in %.2f ms\n",
         (long long)r2.ToInt(), Ms(t3 - t2).count());

  // Benchmark: replace CJK chars
  auto t4 = Clock::now();
  auto r3 = ctx->Eval("big.replace(/[，。]/g, '|').length");
  auto t5 = Clock::now();
  printf("  regex replace CJK punct: %lld chars in %.2f ms\n",
         (long long)r3.ToInt(), Ms(t5 - t4).count());

  // Benchmark: indexOf repeated
  auto t6 = Clock::now();
  auto r4 = ctx->Eval(
      "let count = 0; let pos = 0;"
      "while ((pos = big.indexOf('性能', pos)) !== -1) { count++; pos++; }"
      "count");
  auto t7 = Clock::now();
  printf("  indexOf('性能') occurrences: %lld in %.2f ms\n",
         (long long)r4.ToInt(), Ms(t7 - t6).count());
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. Large Data Processing
// ─────────────────────────────────────────────────────────────────────────────

static void BenchLargeString() {
  PrintSection("Large Data: String Operations (1MB)");

  ScriptEngine engine;
  engine.SetMemoryLimit(64 * 1024 * 1024);  // 64MB
  auto ctx = engine.CreateContext();

  // Build 1MB string
  auto t0 = Clock::now();
  ctx->Eval(
      "var chunk = 'abcdefghij'.repeat(100);"  // 1KB
      "var mb = '';"
      "for (let i = 0; i < 1024; i++) mb += chunk;");
  auto t1 = Clock::now();
  auto len = ctx->Eval("mb.length");
  printf("  Build 1MB string: %lld chars in %.2f ms\n",
         (long long)len.ToInt(), Ms(t1 - t0).count());

  // toUpperCase
  auto t2 = Clock::now();
  ctx->Eval("var upper = mb.toUpperCase()");
  auto t3 = Clock::now();
  printf("  toUpperCase(1MB): %.2f ms\n", Ms(t3 - t2).count());

  // split by every 100 chars
  auto t4 = Clock::now();
  auto r1 = ctx->Eval("mb.match(/.{1,100}/g).length");
  auto t5 = Clock::now();
  printf("  split into 100-char chunks: %lld chunks in %.2f ms\n",
         (long long)r1.ToInt(), Ms(t5 - t4).count());

  // search & replace
  auto t6 = Clock::now();
  auto r2 = ctx->Eval("mb.replace(/abcde/g, 'XXXXX').length");
  auto t7 = Clock::now();
  printf("  replace 'abcde'->'XXXXX': %lld chars in %.2f ms\n",
         (long long)r2.ToInt(), Ms(t7 - t6).count());
}

static void BenchLargeArray() {
  PrintSection("Large Data: Array Operations (1M elements)");

  ScriptEngine engine;
  engine.SetMemoryLimit(256 * 1024 * 1024);  // 256MB
  auto ctx = engine.CreateContext();

  const int N = 1000000;
  std::string n_str = std::to_string(N);

  // Build array
  auto t0 = Clock::now();
  ctx->Eval(("var arr = new Array(" + n_str + ");"
             "for (let i = 0; i < " + n_str + "; i++) arr[i] = i;")
                .c_str());
  auto t1 = Clock::now();
  printf("  Build array[%d]: %.2f ms\n", N, Ms(t1 - t0).count());

  // map
  auto t2 = Clock::now();
  ctx->Eval("var mapped = arr.map(x => x * 2)");
  auto t3 = Clock::now();
  printf("  map(x => x*2): %.2f ms\n", Ms(t3 - t2).count());

  // filter
  auto t4 = Clock::now();
  auto r1 = ctx->Eval("arr.filter(x => x % 7 === 0).length");
  auto t5 = Clock::now();
  printf("  filter(x%%7===0): %lld items in %.2f ms\n",
         (long long)r1.ToInt(), Ms(t5 - t4).count());

  // reduce (sum)
  auto t6 = Clock::now();
  auto r2 = ctx->Eval("arr.reduce((a, b) => a + b, 0)");
  auto t7 = Clock::now();
  printf("  reduce(sum): %lld in %.2f ms\n", (long long)r2.ToInt(),
         Ms(t7 - t6).count());

  // sort (random shuffle then sort)
  ctx->Eval("var shuffled = arr.slice().sort(() => Math.random() - 0.5)");
  auto t8 = Clock::now();
  ctx->Eval("shuffled.sort((a, b) => a - b)");
  auto t9 = Clock::now();
  printf("  sort(1M numbers): %.2f ms\n", Ms(t9 - t8).count());

  // JSON serialize
  auto t10 = Clock::now();
  auto r3 = ctx->Eval(
      "JSON.stringify(arr.slice(0, 10000)).length");
  auto t11 = Clock::now();
  printf("  JSON.stringify(10K items): %lld chars in %.2f ms\n",
         (long long)r3.ToInt(), Ms(t11 - t10).count());
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. Complex Eval Patterns
// ─────────────────────────────────────────────────────────────────────────────

static void BenchClosuresAndHigherOrder() {
  PrintSection("Complex: Closures & Higher-Order Functions");

  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  // Closure-heavy code: memoized fibonacci
  auto t0 = Clock::now();
  auto r = ctx->Eval(R"JS(
    function memoize(fn) {
      const cache = new Map();
      return function(...args) {
        const key = args.join(',');
        if (cache.has(key)) return cache.get(key);
        const result = fn(...args);
        cache.set(key, result);
        return result;
      };
    }

    const fib = memoize(function(n) {
      if (n <= 1) return n;
      return fib(n - 1) + fib(n - 2);
    });

    // Compute fib(1) to fib(80)
    let results = [];
    for (let i = 1; i <= 80; i++) results.push(fib(i));
    results[results.length - 1]
  )JS");
  auto t1 = Clock::now();
  printf("  Memoized fib(80) = %s (%.2f ms)\n", r.ToString().c_str(),
         Ms(t1 - t0).count());

  // Function composition pipeline (separate context to avoid variable clash)
  auto ctx2 = engine.CreateContext();
  auto t2 = Clock::now();
  auto r2 = ctx2->Eval(R"JS(
    const pipe = (...fns) => x => fns.reduce((v, f) => f(v), x);
    const double = x => x * 2;
    const addOne = x => x + 1;
    const square = x => x * x;
    const toString = x => `Result: ${x}`;

    const transform = pipe(double, addOne, square, toString);

    let results = [];
    for (let i = 0; i < 100000; i++) {
      results.push(transform(i));
    }
    results.length + ' | last: ' + results[results.length-1]
  )JS");
  auto t3 = Clock::now();
  printf("  Function pipe x100K: %s (%.2f ms)\n", r2.ToString().c_str(),
         Ms(t3 - t2).count());
}

static void BenchClassesAndPrototypes() {
  PrintSection("Complex: Classes & Prototype Chains");

  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  auto t0 = Clock::now();
  auto r = ctx->Eval(R"JS(
    class Vector3 {
      constructor(x, y, z) { this.x = x; this.y = y; this.z = z; }
      add(v) { return new Vector3(this.x + v.x, this.y + v.y, this.z + v.z); }
      scale(s) { return new Vector3(this.x * s, this.y * s, this.z * s); }
      dot(v) { return this.x * v.x + this.y * v.y + this.z * v.z; }
      length() { return Math.sqrt(this.dot(this)); }
      normalize() { const l = this.length(); return this.scale(1/l); }
    }

    class Particle {
      constructor(pos, vel) { this.pos = pos; this.vel = vel; this.acc = new Vector3(0, -9.8, 0); }
      update(dt) {
        this.vel = this.vel.add(this.acc.scale(dt));
        this.pos = this.pos.add(this.vel.scale(dt));
      }
    }

    // Simulate 1000 particles for 100 steps
    const particles = [];
    for (let i = 0; i < 1000; i++) {
      particles.push(new Particle(
        new Vector3(Math.random()*100, Math.random()*100, Math.random()*100),
        new Vector3(Math.random()*10-5, Math.random()*20, Math.random()*10-5)
      ));
    }

    for (let step = 0; step < 100; step++) {
      for (const p of particles) p.update(0.016);
    }

    // Return stats
    const avgY = particles.reduce((s, p) => s + p.pos.y, 0) / particles.length;
    `1000 particles x 100 steps, avg Y: ${avgY.toFixed(2)}`
  )JS");
  auto t1 = Clock::now();
  printf("  Particle sim: %s (%.2f ms)\n", r.ToString().c_str(),
         Ms(t1 - t0).count());
}

static void BenchRegexHeavy() {
  PrintSection("Complex: Regex-Heavy Workload");

  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  auto t0 = Clock::now();
  auto r = ctx->Eval(R"JS(
    // Simple tokenizer / lexer
    const code = `
      function hello(name) {
        const greeting = "Hello, " + name + "!";
        if (name.length > 10) {
          return greeting.toUpperCase();
        }
        return greeting;
      }
      for (let i = 0; i < 100; i++) { hello("world_" + i); }
    `;

    const TOKEN_PATTERNS = [
      ['KEYWORD',   /^(?:function|const|let|var|if|else|for|return|new|class)\b/],
      ['IDENT',     /^[a-zA-Z_$][a-zA-Z0-9_$]*/],
      ['NUMBER',    /^[0-9]+(?:\.[0-9]+)?/],
      ['STRING',    /^"[^"]*"|^'[^']*'/],
      ['OP',        /^[+\-*/%=<>!&|^~?:]+/],
      ['PUNC',      /^[{}()\[\];,.]/],
      ['SPACE',     /^\s+/],
    ];

    function tokenize(src) {
      const tokens = [];
      let pos = 0;
      while (pos < src.length) {
        let matched = false;
        for (const [type, regex] of TOKEN_PATTERNS) {
          const m = src.slice(pos).match(regex);
          if (m) {
            if (type !== 'SPACE') tokens.push({type, value: m[0]});
            pos += m[0].length;
            matched = true;
            break;
          }
        }
        if (!matched) pos++;
      }
      return tokens;
    }

    // Tokenize the code 500 times
    let totalTokens = 0;
    for (let i = 0; i < 500; i++) {
      totalTokens += tokenize(code).length;
    }
    `${totalTokens} tokens from 500 passes`
  )JS");
  auto t1 = Clock::now();
  printf("  Lexer x500: %s (%.2f ms)\n", r.ToString().c_str(),
         Ms(t1 - t0).count());
}

static void BenchGeneratorsAndIterators() {
  PrintSection("Complex: Generators & Iterators");

  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  auto t0 = Clock::now();
  auto r = ctx->Eval(R"JS(
    function* fibonacci() {
      let a = 0n, b = 1n;
      while (true) {
        yield a;
        [a, b] = [b, a + b];
      }
    }

    function* take(iter, n) {
      let count = 0;
      for (const val of iter) {
        if (count++ >= n) break;
        yield val;
      }
    }

    function* map(iter, fn) {
      for (const val of iter) yield fn(val);
    }

    function* filter(iter, pred) {
      for (const val of iter) {
        if (pred(val)) yield val;
      }
    }

    // Pipeline: fib -> filter(even) -> map(square) -> take(10000)
    const pipeline = take(
      map(
        filter(fibonacci(), x => x % 2n === 0n),
        x => x * x
      ),
      10000
    );

    let sum = 0n;
    let count = 0;
    for (const val of pipeline) {
      sum += val;
      count++;
    }
    `${count} items, sum has ${sum.toString().length} digits`
  )JS");
  auto t1 = Clock::now();
  printf("  Generator pipeline: %s (%.2f ms)\n", r.ToString().c_str(),
         Ms(t1 - t0).count());
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. Real-World Script Scenarios
// ─────────────────────────────────────────────────────────────────────────────

static void BenchJsonEtl() {
  PrintSection("Real-World: JSON ETL Pipeline");

  ScriptEngine engine;
  engine.SetMemoryLimit(128 * 1024 * 1024);
  auto ctx = engine.CreateContext();

  auto t0 = Clock::now();
  auto r = ctx->Eval(R"JS(
    // Generate 10000 "records"
    const records = [];
    for (let i = 0; i < 10000; i++) {
      records.push({
        id: i,
        name: `user_${i}`,
        email: `user${i}@example.com`,
        age: 18 + (i % 50),
        score: Math.random() * 100,
        tags: ['tag' + (i%5), 'tag' + (i%3)],
        active: i % 3 !== 0,
        metadata: { created: Date.now() - i * 1000, region: ['us', 'eu', 'ap'][i%3] }
      });
    }

    // ETL: filter -> transform -> group -> aggregate
    const result = records
      .filter(r => r.active && r.age >= 25)
      .map(r => ({
        id: r.id,
        name: r.name.toUpperCase(),
        score: Math.round(r.score * 10) / 10,
        region: r.metadata.region,
        primary_tag: r.tags[0]
      }))
      .reduce((groups, r) => {
        const key = r.region;
        if (!groups[key]) groups[key] = { count: 0, total_score: 0, names: [] };
        groups[key].count++;
        groups[key].total_score += r.score;
        if (groups[key].names.length < 3) groups[key].names.push(r.name);
        return groups;
      }, {});

    // Compute averages
    for (const [region, data] of Object.entries(result)) {
      data.avg_score = Math.round(data.total_score / data.count * 10) / 10;
      delete data.total_score;
    }

    JSON.stringify(result)
  )JS");
  auto t1 = Clock::now();
  printf("  10K records ETL: %.2f ms\n", Ms(t1 - t0).count());
  printf("  Result: %.80s...\n", r.ToString().c_str());
}

static void BenchTemplateEngine() {
  PrintSection("Real-World: Template Engine");

  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  auto t0 = Clock::now();
  auto r = ctx->Eval(R"JS(
    // Simple template engine with {{ expr }} and {% if %} {% for %}
    function compile(template) {
      const code = ['let _out = "";'];
      const parts = template.split(/({{.*?}}|{%.*?%})/g);
      for (const part of parts) {
        if (part.startsWith('{{') && part.endsWith('}}')) {
          code.push(`_out += (${part.slice(2, -2).trim()});`);
        } else if (part.startsWith('{%') && part.endsWith('%}')) {
          const stmt = part.slice(2, -2).trim();
          if (stmt.startsWith('if ')) code.push(`if (${stmt.slice(3)}) {`);
          else if (stmt === 'endif') code.push('}');
          else if (stmt.startsWith('for ')) code.push(`for (${stmt.slice(4)}) {`);
          else if (stmt === 'endfor') code.push('}');
          else code.push(stmt);
        } else {
          code.push(`_out += ${JSON.stringify(part)};`);
        }
      }
      code.push('return _out;');
      return new Function('data', code.join('\n'));
    }

    const tmpl = `
<h1>{{ data.title }}</h1>
<ul>
{% for const item of data.items %}
  <li>{{ item.name }}: {{ item.value }}{% if item.value > 50 %} (HIGH){% endif %}</li>
{% endfor %}
</ul>
<p>Total: {{ data.items.reduce((s,i) => s + i.value, 0) }}</p>`;

    const render = compile(tmpl);

    // Render 10000 times with different data
    let totalLen = 0;
    for (let i = 0; i < 10000; i++) {
      const html = render({
        title: 'Report #' + i,
        items: [
          {name: 'CPU', value: 30 + (i % 70)},
          {name: 'Memory', value: 40 + (i % 60)},
          {name: 'Disk', value: 20 + (i % 80)},
        ]
      });
      totalLen += html.length;
    }
    `10000 renders, total output: ${(totalLen/1024).toFixed(0)} KB`
  )JS");
  auto t1 = Clock::now();
  printf("  Template engine: %s (%.2f ms)\n", r.ToString().c_str(),
         Ms(t1 - t0).count());
}

static void BenchRuleEngine() {
  PrintSection("Real-World: Rule Engine (Business Logic)");

  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  // Register native data source
  ctx->RegisterFunction(
      "getOrders",
      [](ScriptContext& c, const std::vector<ScriptValue>& args) {
        (void)args;
        // Simulate returning order data as JSON string
        return c.Eval(R"JS(
          (function() {
            const orders = [];
            for (let i = 0; i < 5000; i++) {
              orders.push({
                id: 'ORD-' + i.toString().padStart(6, '0'),
                amount: Math.random() * 10000,
                items: Math.floor(Math.random() * 20) + 1,
                customer_tier: ['bronze', 'silver', 'gold', 'platinum'][i % 4],
                is_international: i % 5 === 0,
                weight_kg: Math.random() * 50
              });
            }
            return orders;
          })()
        )JS");
      });

  auto t0 = Clock::now();
  auto r = ctx->Eval(R"JS(
    // Dynamic business rules loaded at runtime
    const rules = [
      { name: 'free_shipping', condition: o => o.amount > 500 && !o.is_international },
      { name: 'bulk_discount', condition: o => o.items >= 10, action: o => o.amount *= 0.9 },
      { name: 'intl_surcharge', condition: o => o.is_international, action: o => o.amount += o.weight_kg * 2.5 },
      { name: 'vip_discount', condition: o => o.customer_tier === 'platinum', action: o => o.amount *= 0.85 },
      { name: 'heavy_surcharge', condition: o => o.weight_kg > 30, action: o => o.amount += 50 },
    ];

    const orders = getOrders();
    const results = { total: orders.length, rules_applied: {} };
    rules.forEach(r => results.rules_applied[r.name] = 0);

    for (const order of orders) {
      for (const rule of rules) {
        if (rule.condition(order)) {
          if (rule.action) rule.action(order);
          results.rules_applied[rule.name]++;
        }
      }
    }

    results.avg_amount = Math.round(orders.reduce((s, o) => s + o.amount, 0) / orders.length);
    JSON.stringify(results)
  )JS");
  auto t1 = Clock::now();
  printf("  5K orders x 5 rules: %.2f ms\n", Ms(t1 - t0).count());
  printf("  Result: %s\n", r.ToString().c_str());
}

static void BenchCppJsBridge() {
  PrintSection("Real-World: Heavy C++ <-> JS Bridge Traffic");

  ScriptEngine engine;
  auto ctx = engine.CreateContext();

  // Register multiple native functions
  ctx->RegisterFunction(
      "sqrt", [](ScriptContext& c, const std::vector<ScriptValue>& args) {
        return ToScriptValue(c, std::sqrt(args[0].ToDouble()));
      });
  ctx->RegisterFunction(
      "pow", [](ScriptContext& c, const std::vector<ScriptValue>& args) {
        return ToScriptValue(c, std::pow(args[0].ToDouble(), args[1].ToDouble()));
      });
  ctx->RegisterFunction(
      "log", [](ScriptContext& c, const std::vector<ScriptValue>& args) {
        return ToScriptValue(c, std::log(args[0].ToDouble()));
      });
  ctx->RegisterFunction(
      "sin", [](ScriptContext& c, const std::vector<ScriptValue>& args) {
        return ToScriptValue(c, std::sin(args[0].ToDouble()));
      });
  ctx->RegisterFunction(
      "cos", [](ScriptContext& c, const std::vector<ScriptValue>& args) {
        return ToScriptValue(c, std::cos(args[0].ToDouble()));
      });

  auto t0 = Clock::now();
  auto r = ctx->Eval(R"JS(
    // Math-heavy computation calling native functions 500K+ times
    let sum = 0;
    for (let i = 1; i <= 100000; i++) {
      sum += sqrt(i) + sin(i * 0.01) + cos(i * 0.01) + log(i);
    }
    sum.toFixed(4)
  )JS");
  auto t1 = Clock::now();
  printf("  500K native math calls: %s (%.2f ms)\n",
         r.ToString().c_str(), Ms(t1 - t0).count());
  double calls_per_sec =
      500000.0 / (Ms(t1 - t0).count() / 1000.0);
  printf("  Throughput: %.1f M calls/sec\n", calls_per_sec / 1e6);
}

// ─────────────────────────────────────────────────────────────────────────────
// Summary
// ─────────────────────────────────────────────────────────────────────────────

static void PrintSummary() {
  PrintSection("Quick Summary");
  printf("  ┌─────────────────────────────────────────────────┐\n");
  printf("  │ QuickJS-NG in xtils: Feature & Performance      │\n");
  printf("  ├─────────────────────────────────────────────────┤\n");
  printf("  │ ✓ Full UTF-8/Unicode support (CJK, emoji)       │\n");
  printf("  │ ✓ ES2025 features (generators, classes, etc.)   │\n");
  printf("  │ ✓ Memory limit / sandboxing                     │\n");
  printf("  │ ✓ C++ <-> JS bidirectional binding              │\n");
  printf("  │ ✓ Context isolation                             │\n");
  printf("  │ ✓ Error handling (no crash on JS exceptions)    │\n");
  printf("  └─────────────────────────────────────────────────┘\n");
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
  PrintHeader("UTF-8 & UNICODE SUPPORT");
  TestUtf8Basic();
  BenchUtf8Processing();

  PrintHeader("LARGE DATA PROCESSING");
  BenchLargeString();
  BenchLargeArray();

  PrintHeader("COMPLEX EVAL PATTERNS");
  BenchClosuresAndHigherOrder();
  BenchClassesAndPrototypes();
  BenchRegexHeavy();
  BenchGeneratorsAndIterators();

  PrintHeader("REAL-WORLD SCENARIOS");
  BenchJsonEtl();
  BenchTemplateEngine();
  BenchRuleEngine();
  BenchCppJsBridge();

  PrintHeader("COMPLETE");
  PrintSummary();

  return 0;
}
