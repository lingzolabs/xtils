// Head-to-head comparison: xtils C++ JSON/Regex vs QuickJS JS JSON/Regex
//
// Tests:
// 1. JSON parse performance (C++ Json::parse vs JS JSON.parse)
// 2. JSON serialize performance (C++ Json::dump vs JS JSON.stringify)
// 3. JSON manipulation (access, modify, nested ops)
// 4. Regex matching (C++ std::regex vs JS RegExp)
// 5. Regex replace (C++ std::regex_replace vs JS String.replace)

#include <chrono>
#include <cstdio>
#include <numeric>
#include <regex>
#include <string>
#include <vector>

#include "xtils/scripting/binding.h"
#include "xtils/scripting/engine.h"
#include "xtils/utils/json.h"

using namespace xtils;
using Clock = std::chrono::high_resolution_clock;
using Ms = std::chrono::duration<double, std::milli>;
using Us = std::chrono::duration<double, std::micro>;

static void PrintHeader(const char* title) {
  printf("\n═══════════════════════════════════════════════════════════════\n");
  printf("  %s\n", title);
  printf("═══════════════════════════════════════════════════════════════\n\n");
}

static void PrintSection(const char* title) {
  printf("\n── %s ──\n", title);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Data Generators
// ─────────────────────────────────────────────────────────────────────────────

// Generate a complex JSON string with nested objects/arrays
static std::string GenerateJsonData(int records) {
  std::string json = "[";
  for (int i = 0; i < records; ++i) {
    if (i > 0) json += ",";
    json += "{\"id\":" + std::to_string(i);
    json += ",\"name\":\"user_" + std::to_string(i) + "\"";
    json += ",\"email\":\"user" + std::to_string(i) + "@example.com\"";
    json += ",\"age\":" + std::to_string(18 + i % 50);
    json += ",\"score\":" + std::to_string(i * 1.5);
    json += ",\"active\":" + std::string(i % 3 != 0 ? "true" : "false");
    json += ",\"tags\":[\"tag" + std::to_string(i % 5) + "\",\"tag" +
            std::to_string(i % 3) + "\"]";
    json += ",\"address\":{\"city\":\"City" + std::to_string(i % 10) + "\"";
    json += ",\"zip\":\"" + std::to_string(10000 + i) + "\"}";
    json += "}";
  }
  json += "]";
  return json;
}

// Generate text for regex testing
static std::string GenerateLogData(int lines) {
  std::string log;
  for (int i = 0; i < lines; ++i) {
    log += "2024-06-12 10:" + std::to_string(10 + i % 50) + ":" +
           std::to_string(10 + i % 50) + "." + std::to_string(i % 1000) +
           " [" + (i % 3 == 0 ? "ERROR" : i % 3 == 1 ? "WARN" : "INFO") +
           "] ";
    log += "Request from 192.168." + std::to_string(i % 256) + "." +
           std::to_string(i % 256);
    log += " path=/api/v" + std::to_string(i % 3 + 1) + "/users/" +
           std::to_string(i);
    log += " status=" + std::to_string(200 + (i % 5) * 100);
    log += " latency=" + std::to_string(i % 500) + "ms";
    log += " bytes=" + std::to_string(1024 + i * 10) + "\n";
  }
  return log;
}

// ─────────────────────────────────────────────────────────────────────────────
// 1. JSON Parse
// ─────────────────────────────────────────────────────────────────────────────

static void CompareJsonParse() {
  PrintSection("JSON Parse: C++ vs JS");

  for (int records : {100, 1000, 5000}) {
    std::string json_str = GenerateJsonData(records);
    printf("\n  --- %d records (%zu KB) ---\n", records,
           json_str.size() / 1024);

    // C++ xtils::Json::parse
    double cpp_total = 0;
    int cpp_iters = (records <= 100) ? 100 : (records <= 1000) ? 20 : 5;
    for (int i = 0; i < cpp_iters; ++i) {
      auto t0 = Clock::now();
      auto result = Json::parse(json_str);
      auto t1 = Clock::now();
      cpp_total += Ms(t1 - t0).count();
      if (!result) {
        printf("  C++ parse FAILED!\n");
        return;
      }
    }
    double cpp_avg = cpp_total / cpp_iters;

    // JS JSON.parse
    ScriptEngine engine;
    engine.SetMemoryLimit(256 * 1024 * 1024);
    auto ctx = engine.CreateContext();

    // Pass the JSON string to JS
    ctx->RegisterFunction(
        "getJsonStr",
        [&json_str](ScriptContext& c, const std::vector<ScriptValue>& args) {
          (void)args;
          return ToScriptValue(c, json_str);
        });

    double js_total = 0;
    for (int i = 0; i < cpp_iters; ++i) {
      auto t0 = Clock::now();
      auto r = ctx->Eval("JSON.parse(getJsonStr())");
      auto t1 = Clock::now();
      js_total += Ms(t1 - t0).count();
      if (r.IsException()) {
        printf("  JS parse FAILED!\n");
        return;
      }
    }
    double js_avg = js_total / cpp_iters;

    printf("  C++ Json::parse:  %.2f ms/iter\n", cpp_avg);
    printf("  JS  JSON.parse:   %.2f ms/iter\n", js_avg);
    printf("  Winner: %s (%.1fx faster)\n",
           cpp_avg < js_avg ? "C++" : "JS",
           cpp_avg < js_avg ? js_avg / cpp_avg : cpp_avg / js_avg);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. JSON Serialize (dump/stringify)
// ─────────────────────────────────────────────────────────────────────────────

static void CompareJsonSerialize() {
  PrintSection("JSON Serialize: C++ vs JS");

  for (int records : {100, 1000, 5000}) {
    std::string json_str = GenerateJsonData(records);
    printf("\n  --- %d records ---\n", records);

    // Parse first (C++)
    auto json_obj = Json::parse(json_str);
    if (!json_obj) {
      printf("  C++ parse failed, skip\n");
      continue;
    }

    int iters = (records <= 100) ? 200 : (records <= 1000) ? 50 : 10;

    // C++ Json::dump
    double cpp_total = 0;
    size_t cpp_len = 0;
    for (int i = 0; i < iters; ++i) {
      auto t0 = Clock::now();
      std::string out = json_obj->dump();
      auto t1 = Clock::now();
      cpp_total += Ms(t1 - t0).count();
      cpp_len = out.size();
    }
    double cpp_avg = cpp_total / iters;

    // JS JSON.stringify (parse once, stringify N times)
    ScriptEngine engine;
    engine.SetMemoryLimit(256 * 1024 * 1024);
    auto ctx = engine.CreateContext();
    ctx->RegisterFunction(
        "getJsonStr",
        [&json_str](ScriptContext& c, const std::vector<ScriptValue>& args) {
          (void)args;
          return ToScriptValue(c, json_str);
        });
    ctx->Eval("var obj = JSON.parse(getJsonStr())");

    double js_total = 0;
    int64_t js_len = 0;
    for (int i = 0; i < iters; ++i) {
      auto t0 = Clock::now();
      auto r = ctx->Eval("JSON.stringify(obj)");
      auto t1 = Clock::now();
      js_total += Ms(t1 - t0).count();
      if (i == 0) js_len = r.ToString().size();
    }
    double js_avg = js_total / iters;

    printf("  C++ Json::dump:     %.2f ms/iter (%zu bytes)\n", cpp_avg,
           cpp_len);
    printf("  JS  JSON.stringify: %.2f ms/iter (%lld bytes)\n", js_avg,
           (long long)js_len);
    printf("  Winner: %s (%.1fx faster)\n",
           cpp_avg < js_avg ? "C++" : "JS",
           cpp_avg < js_avg ? js_avg / cpp_avg : cpp_avg / js_avg);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. JSON Manipulation (access + modify)
// ─────────────────────────────────────────────────────────────────────────────

static void CompareJsonManipulation() {
  PrintSection("JSON Manipulation: C++ vs JS");

  const int records = 1000;
  std::string json_str = GenerateJsonData(records);
  printf("  %d records, iterate + access fields + compute\n\n", records);

  // C++: parse then iterate
  auto json_obj = Json::parse(json_str);
  int iters = 50;

  double cpp_total = 0;
  int64_t cpp_result = 0;
  for (int it = 0; it < iters; ++it) {
    auto t0 = Clock::now();
    const auto& arr = json_obj->as_array();
    int64_t total_age = 0;
    int active_count = 0;
    for (const auto& item : arr) {
      if (item["active"].as_bool()) {
        total_age += item["age"].as_integer();
        active_count++;
      }
    }
    cpp_result = total_age / (active_count > 0 ? active_count : 1);
    auto t1 = Clock::now();
    cpp_total += Ms(t1 - t0).count();
  }
  double cpp_avg = cpp_total / iters;

  // JS: parse then iterate
  ScriptEngine engine;
  engine.SetMemoryLimit(256 * 1024 * 1024);
  auto ctx = engine.CreateContext();
  ctx->RegisterFunction(
      "getJsonStr",
      [&json_str](ScriptContext& c, const std::vector<ScriptValue>& args) {
        (void)args;
        return ToScriptValue(c, json_str);
      });
  ctx->Eval("var data = JSON.parse(getJsonStr())");

  double js_total = 0;
  int64_t js_result = 0;
  for (int it = 0; it < iters; ++it) {
    auto t0 = Clock::now();
    auto r = ctx->Eval(
        "var totalAge = 0, activeCount = 0;"
        "for (var item of data) {"
        "  if (item.active) { totalAge += item.age; activeCount++; }"
        "}"
        "Math.floor(totalAge / activeCount)");
    auto t1 = Clock::now();
    js_total += Ms(t1 - t0).count();
    if (it == 0) js_result = r.ToInt();
  }
  double js_avg = js_total / iters;

  printf("  C++ iterate+access: %.3f ms/iter (avg_age=%lld)\n", cpp_avg,
         (long long)cpp_result);
  printf("  JS  iterate+access: %.3f ms/iter (avg_age=%lld)\n", js_avg,
         (long long)js_result);
  printf("  Winner: %s (%.1fx faster)\n",
         cpp_avg < js_avg ? "C++" : "JS",
         cpp_avg < js_avg ? js_avg / cpp_avg : cpp_avg / js_avg);
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. Regex Match
// ─────────────────────────────────────────────────────────────────────────────

static void CompareRegexMatch() {
  PrintSection("Regex Match: C++ std::regex vs JS RegExp");

  for (int lines : {100, 1000, 5000}) {
    std::string log_data = GenerateLogData(lines);
    printf("\n  --- %d lines (%zu KB) ---\n", lines, log_data.size() / 1024);

    // Pattern: extract IP addresses
    int iters = (lines <= 100) ? 50 : (lines <= 1000) ? 10 : 3;

    // C++ std::regex
    double cpp_total = 0;
    int cpp_matches = 0;
    {
      std::regex pattern(R"((\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}))");
      for (int it = 0; it < iters; ++it) {
        auto t0 = Clock::now();
        auto begin =
            std::sregex_iterator(log_data.begin(), log_data.end(), pattern);
        auto end = std::sregex_iterator();
        cpp_matches = std::distance(begin, end);
        auto t1 = Clock::now();
        cpp_total += Ms(t1 - t0).count();
      }
    }
    double cpp_avg = cpp_total / iters;

    // JS RegExp
    ScriptEngine engine;
    engine.SetMemoryLimit(128 * 1024 * 1024);
    auto ctx = engine.CreateContext();
    ctx->RegisterFunction(
        "getLogData",
        [&log_data](ScriptContext& c, const std::vector<ScriptValue>& args) {
          (void)args;
          return ToScriptValue(c, log_data);
        });
    ctx->Eval("var logData = getLogData()");

    double js_total = 0;
    int64_t js_matches = 0;
    for (int it = 0; it < iters; ++it) {
      auto t0 = Clock::now();
      auto r = ctx->Eval(
          "logData.match(/(\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3})/g)"
          ".length");
      auto t1 = Clock::now();
      js_total += Ms(t1 - t0).count();
      if (it == 0) js_matches = r.ToInt();
    }
    double js_avg = js_total / iters;

    printf("  C++ std::regex:  %.2f ms/iter (%d matches)\n", cpp_avg,
           cpp_matches);
    printf("  JS  RegExp:      %.2f ms/iter (%lld matches)\n", js_avg,
           (long long)js_matches);
    printf("  Winner: %s (%.1fx faster)\n",
           cpp_avg < js_avg ? "C++" : "JS",
           cpp_avg < js_avg ? js_avg / cpp_avg : cpp_avg / js_avg);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. Regex Replace
// ─────────────────────────────────────────────────────────────────────────────

static void CompareRegexReplace() {
  PrintSection("Regex Replace: C++ std::regex vs JS RegExp");

  for (int lines : {100, 1000, 5000}) {
    std::string log_data = GenerateLogData(lines);
    printf("\n  --- %d lines (%zu KB) ---\n", lines, log_data.size() / 1024);

    int iters = (lines <= 100) ? 50 : (lines <= 1000) ? 10 : 3;

    // Pattern: mask IP addresses → 192.168.x.x → ***.***.x.x
    // C++ std::regex_replace
    double cpp_total = 0;
    size_t cpp_len = 0;
    {
      std::regex pattern(R"((\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3}))");
      for (int it = 0; it < iters; ++it) {
        auto t0 = Clock::now();
        std::string result =
            std::regex_replace(log_data, pattern, "***.$2.$3.$4");
        auto t1 = Clock::now();
        cpp_total += Ms(t1 - t0).count();
        cpp_len = result.size();
      }
    }
    double cpp_avg = cpp_total / iters;

    // JS String.replace
    ScriptEngine engine;
    engine.SetMemoryLimit(128 * 1024 * 1024);
    auto ctx = engine.CreateContext();
    ctx->RegisterFunction(
        "getLogData",
        [&log_data](ScriptContext& c, const std::vector<ScriptValue>& args) {
          (void)args;
          return ToScriptValue(c, log_data);
        });
    ctx->Eval("var logData = getLogData()");

    double js_total = 0;
    int64_t js_len = 0;
    for (int it = 0; it < iters; ++it) {
      auto t0 = Clock::now();
      auto r = ctx->Eval(
          "logData.replace(/(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})"
          "/g, '***.$2.$3.$4').length");
      auto t1 = Clock::now();
      js_total += Ms(t1 - t0).count();
      if (it == 0) js_len = r.ToInt();
    }
    double js_avg = js_total / iters;

    printf("  C++ regex_replace: %.2f ms/iter (%zu chars)\n", cpp_avg,
           cpp_len);
    printf("  JS  String.replace: %.2f ms/iter (%lld chars)\n", js_avg,
           (long long)js_len);
    printf("  Winner: %s (%.1fx faster)\n",
           cpp_avg < js_avg ? "C++" : "JS",
           cpp_avg < js_avg ? js_avg / cpp_avg : cpp_avg / js_avg);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// 6. Complex Regex Patterns
// ─────────────────────────────────────────────────────────────────────────────

static void CompareComplexRegex() {
  PrintSection("Complex Regex: Email/URL Extraction");

  // Generate text with emails and URLs mixed in
  std::string text;
  for (int i = 0; i < 2000; ++i) {
    text += "Contact user" + std::to_string(i) + "@company.com or visit ";
    text += "https://api.example.com/v" + std::to_string(i % 3 + 1);
    text += "/resource/" + std::to_string(i) + " for details. ";
    text += "Also try admin_" + std::to_string(i) + "@sub.domain.org ";
    text += "or ftp://files.host.net/path/file" + std::to_string(i) + ".txt\n";
  }
  printf("  Text size: %zu KB\n\n", text.size() / 1024);

  int iters = 5;

  // --- Email extraction ---
  printf("  [Email extraction]\n");
  {
    // C++
    std::regex email_re(R"([a-zA-Z0-9_.+-]+@[a-zA-Z0-9-]+\.[a-zA-Z0-9-.]+)");
    double cpp_total = 0;
    int cpp_count = 0;
    for (int it = 0; it < iters; ++it) {
      auto t0 = Clock::now();
      auto begin =
          std::sregex_iterator(text.begin(), text.end(), email_re);
      auto end = std::sregex_iterator();
      cpp_count = std::distance(begin, end);
      auto t1 = Clock::now();
      cpp_total += Ms(t1 - t0).count();
    }
    double cpp_avg = cpp_total / iters;

    // JS
    ScriptEngine engine;
    engine.SetMemoryLimit(128 * 1024 * 1024);
    auto ctx = engine.CreateContext();
    ctx->RegisterFunction(
        "getText",
        [&text](ScriptContext& c, const std::vector<ScriptValue>& args) {
          (void)args;
          return ToScriptValue(c, text);
        });
    ctx->Eval("var text = getText()");

    double js_total = 0;
    int64_t js_count = 0;
    for (int it = 0; it < iters; ++it) {
      auto t0 = Clock::now();
      auto r = ctx->Eval(
          "text.match(/[a-zA-Z0-9_.+-]+@[a-zA-Z0-9-]+\\.[a-zA-Z0-9-.]+/g)"
          ".length");
      auto t1 = Clock::now();
      js_total += Ms(t1 - t0).count();
      if (it == 0) js_count = r.ToInt();
    }
    double js_avg = js_total / iters;

    printf("    C++: %.2f ms (%d emails)\n", cpp_avg, cpp_count);
    printf("    JS:  %.2f ms (%lld emails)\n", js_avg, (long long)js_count);
    printf("    Winner: %s (%.1fx)\n",
           cpp_avg < js_avg ? "C++" : "JS",
           cpp_avg < js_avg ? js_avg / cpp_avg : cpp_avg / js_avg);
  }

  // --- URL extraction ---
  printf("\n  [URL extraction]\n");
  {
    std::regex url_re(
        R"(https?://[a-zA-Z0-9._~:/?#\[\]@!$&'()*+,;=-]+)");
    double cpp_total = 0;
    int cpp_count = 0;
    for (int it = 0; it < iters; ++it) {
      auto t0 = Clock::now();
      auto begin =
          std::sregex_iterator(text.begin(), text.end(), url_re);
      auto end = std::sregex_iterator();
      cpp_count = std::distance(begin, end);
      auto t1 = Clock::now();
      cpp_total += Ms(t1 - t0).count();
    }
    double cpp_avg = cpp_total / iters;

    ScriptEngine engine;
    engine.SetMemoryLimit(128 * 1024 * 1024);
    auto ctx = engine.CreateContext();
    ctx->RegisterFunction(
        "getText",
        [&text](ScriptContext& c, const std::vector<ScriptValue>& args) {
          (void)args;
          return ToScriptValue(c, text);
        });
    ctx->Eval("var text = getText()");

    double js_total = 0;
    int64_t js_count = 0;
    for (int it = 0; it < iters; ++it) {
      auto t0 = Clock::now();
      auto r = ctx->Eval(
          "text.match(/https?:\\/\\/[a-zA-Z0-9._~:/?#\\[\\]@!$&'()*+,;=-]+/g)"
          ".length");
      auto t1 = Clock::now();
      js_total += Ms(t1 - t0).count();
      if (it == 0) js_count = r.ToInt();
    }
    double js_avg = js_total / iters;

    printf("    C++: %.2f ms (%d URLs)\n", cpp_avg, cpp_count);
    printf("    JS:  %.2f ms (%lld URLs)\n", js_avg, (long long)js_count);
    printf("    Winner: %s (%.1fx)\n",
           cpp_avg < js_avg ? "C++" : "JS",
           cpp_avg < js_avg ? js_avg / cpp_avg : cpp_avg / js_avg);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// 7. Capability Comparison (features C++ doesn't easily do)
// ─────────────────────────────────────────────────────────────────────────────

static void CompareCapabilities() {
  PrintSection("Capability: What JS does better");

  ScriptEngine engine;
  engine.SetMemoryLimit(128 * 1024 * 1024);
  auto ctx = engine.CreateContext();

  printf("  1. Dynamic JSON schema validation (no C++ equivalent):\n");
  auto t0 = Clock::now();
  auto r1 = ctx->Eval(R"JS(
    function validate(obj, schema) {
      for (const [key, rule] of Object.entries(schema)) {
        if (rule.required && !(key in obj)) return `missing: ${key}`;
        if (key in obj) {
          if (rule.type && typeof obj[key] !== rule.type)
            return `${key}: expected ${rule.type}, got ${typeof obj[key]}`;
          if (rule.min !== undefined && obj[key] < rule.min)
            return `${key}: below min ${rule.min}`;
          if (rule.max !== undefined && obj[key] > rule.max)
            return `${key}: above max ${rule.max}`;
          if (rule.pattern && !new RegExp(rule.pattern).test(obj[key]))
            return `${key}: pattern mismatch`;
        }
      }
      return 'valid';
    }

    const schema = {
      name: { required: true, type: 'string', pattern: '^[A-Za-z ]+$' },
      age: { required: true, type: 'number', min: 0, max: 150 },
      email: { required: true, type: 'string', pattern: '.+@.+\\..+' },
    };

    let validCount = 0;
    const testData = [
      { name: 'Alice', age: 30, email: 'alice@test.com' },
      { name: 'Bob123', age: 25, email: 'bob@test.com' },
      { name: 'Charlie', age: -5, email: 'c@t.co' },
      { name: 'Diana', age: 28, email: 'not-an-email' },
      { name: 'Eve', age: 35, email: 'eve@example.org' },
    ];

    // Validate 10000 objects
    let results = [];
    for (let i = 0; i < 10000; i++) {
      const obj = testData[i % testData.length];
      const r = validate(obj, schema);
      if (r === 'valid') validCount++;
      if (i < 5) results.push(r);
    }
    `${validCount}/10000 valid. Samples: ${results.join('; ')}`
  )JS");
  auto t1 = Clock::now();
  printf("    10K validations: %.2f ms\n", Ms(t1 - t0).count());
  printf("    Result: %s\n", r1.ToString().c_str());

  printf("\n  2. Dynamic regex construction from runtime data:\n");
  auto t2 = Clock::now();
  auto r2 = ctx->Eval(R"JS(
    // Build regex patterns dynamically from a blocklist
    const blocklist = ['spam', 'phish', 'malware', 'trojan', 'worm',
                       'ransom', 'exploit', 'botnet', 'rootkit', 'keylog'];
    const pattern = new RegExp('\\b(' + blocklist.join('|') + ')\\w*\\b', 'gi');

    const emails = [];
    for (let i = 0; i < 5000; i++) {
      const bad = blocklist[i % blocklist.length];
      emails.push(i % 4 === 0
        ? `Subject: Get free ${bad}_offer_${i}! Act now!`
        : `Subject: Meeting notes from ${['Monday','Tuesday','Wednesday'][i%3]}`);
    }

    let flagged = 0;
    for (const email of emails) {
      if (pattern.test(email)) flagged++;
    }
    `${flagged}/5000 flagged`
  )JS");
  auto t3 = Clock::now();
  printf("    5K emails scanned: %.2f ms\n", Ms(t3 - t2).count());
  printf("    Result: %s\n", r2.ToString().c_str());

  printf("\n  3. JSON path query (dot notation, dynamic):\n");
  auto ctx3 = engine.CreateContext();
  auto t4 = Clock::now();
  auto r3 = ctx3->Eval(R"JS(
    function jsonPath(obj, path) {
      return path.split('.').reduce((o, k) => {
        if (o === undefined || o === null) return undefined;
        const idx = parseInt(k);
        return isNaN(idx) ? o[k] : o[idx];
      }, obj);
    }

    const data = {
      users: [
        { name: 'Alice', roles: ['admin', 'user'], profile: { city: 'Beijing' } },
        { name: 'Bob', roles: ['user'], profile: { city: 'Shanghai' } },
        { name: 'Charlie', roles: ['moderator'], profile: { city: 'Shenzhen' } },
      ]
    };

    // Query 100K paths
    const paths = [
      'users.0.name', 'users.1.profile.city', 'users.2.roles.0',
      'users.0.profile.city', 'users.1.name', 'users.2.profile.city',
    ];

    let results = [];
    for (let i = 0; i < 100000; i++) {
      results.push(jsonPath(data, paths[i % paths.length]));
    }
    `100K queries done. Samples: ${results.slice(0,6).join(', ')}`
  )JS");
  auto t5 = Clock::now();
  printf("    100K path queries: %.2f ms\n", Ms(t5 - t4).count());
  printf("    Result: %s\n", r3.ToString().c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
  PrintHeader("JSON: C++ xtils::Json vs JS JSON");
  CompareJsonParse();
  CompareJsonSerialize();
  CompareJsonManipulation();

  PrintHeader("REGEX: C++ std::regex vs JS RegExp");
  CompareRegexMatch();
  CompareRegexReplace();
  CompareComplexRegex();

  PrintHeader("CAPABILITY: JS Unique Strengths");
  CompareCapabilities();

  PrintHeader("CONCLUSION");
  printf("  Key Takeaways:\n");
  printf("  • JSON parse/stringify: compare raw throughput\n");
  printf("  • std::regex vs JS RegExp: real-world pattern matching\n");
  printf("  • JS advantages: dynamic patterns, schema validation,\n");
  printf("    JSON path queries — all without recompilation\n");
  printf("\n");

  return 0;
}
