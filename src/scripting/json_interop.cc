#include "xtils/scripting/json_interop.h"

#ifdef XTILS_HAS_SCRIPTING

#include <cstring>
#include <string>

#include "quickjs.h"

namespace xtils {

// ─────────────────────────────────────────────────────────────────────────────
// Json → ScriptValue
// ─────────────────────────────────────────────────────────────────────────────

ScriptValue JsonToScriptValue(ScriptContext& ctx, const Json& json) {
  JSContext* js_ctx = ctx.Raw();

  if (json.is_null()) {
    return ScriptValue(JS_NULL, js_ctx);
  }
  if (json.is_bool()) {
    return ScriptValue(JS_NewBool(js_ctx, json.as_bool()), js_ctx);
  }
  if (json.is_integer()) {
    return ScriptValue(JS_NewInt64(js_ctx, json.as_integer()), js_ctx);
  }
  if (json.is_float()) {
    return ScriptValue(JS_NewFloat64(js_ctx, json.as_float()), js_ctx);
  }
  if (json.is_string()) {
    return ScriptValue(
        JS_NewStringLen(js_ctx, json.as_string().c_str(),
                        json.as_string().size()),
        js_ctx);
  }
  if (json.is_array()) {
    JSValue arr = JS_NewArray(js_ctx);
    const auto& json_arr = json.as_array();
    for (size_t i = 0; i < json_arr.size(); ++i) {
      ScriptValue elem = JsonToScriptValue(ctx, json_arr[i]);
      JS_SetPropertyUint32(js_ctx, arr, static_cast<uint32_t>(i),
                           JS_DupValue(js_ctx, elem.Raw()));
    }
    return ScriptValue(arr, js_ctx);
  }
  if (json.is_object()) {
    JSValue obj = JS_NewObject(js_ctx);
    const auto& json_obj = json.as_object();
    for (const auto& [key, value] : json_obj) {
      ScriptValue val = JsonToScriptValue(ctx, value);
      JS_SetPropertyStr(js_ctx, obj, key.c_str(),
                        JS_DupValue(js_ctx, val.Raw()));
    }
    return ScriptValue(obj, js_ctx);
  }

  return ScriptValue(JS_UNDEFINED, js_ctx);
}

// ─────────────────────────────────────────────────────────────────────────────
// ScriptValue → Json
// ─────────────────────────────────────────────────────────────────────────────

Json ScriptValueToJson(const ScriptValue& value) {
  if (value.IsNull() || value.IsUndefined()) {
    return Json(nullptr);
  }

  if (value.IsBool()) {
    return Json(value.ToBool());
  }

  if (value.IsNumber()) {
    // Determine if integer or float
    double d = value.ToDouble();
    int64_t i = value.ToInt();
    if (static_cast<double>(i) == d && d >= -9007199254740992.0 &&
        d <= 9007199254740992.0) {
      return Json(i);
    }
    return Json(d);
  }

  if (value.IsString()) {
    return Json(value.ToString());
  }

  JSContext* ctx = value.Ctx();
  JSValue raw = value.Raw();

  if (value.IsArray()) {
    Json::array_t arr;
    JSValue len_val = JS_GetPropertyStr(ctx, raw, "length");
    int64_t len = 0;
    JS_ToInt64(ctx, &len, len_val);
    JS_FreeValue(ctx, len_val);

    arr.reserve(static_cast<size_t>(len));
    for (int64_t i = 0; i < len; ++i) {
      JSValue elem = JS_GetPropertyUint32(ctx, raw, static_cast<uint32_t>(i));
      ScriptValue sv(elem, ctx);
      arr.push_back(ScriptValueToJson(sv));
      // Note: sv destructor will free elem via JS_FreeValue
    }
    return Json(arr);
  }

  if (value.IsObject()) {
    Json::object_t obj;

    JSPropertyEnum* props = nullptr;
    uint32_t prop_count = 0;
    if (JS_GetOwnPropertyNames(ctx, &props, &prop_count, raw,
                                JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
      for (uint32_t i = 0; i < prop_count; ++i) {
        const char* key = JS_AtomToCString(ctx, props[i].atom);
        if (key) {
          JSValue prop_val = JS_GetProperty(ctx, raw, props[i].atom);
          ScriptValue sv(prop_val, ctx);
          obj[key] = ScriptValueToJson(sv);
          JS_FreeCString(ctx, key);
        }
      }
      // Free property list
      for (uint32_t i = 0; i < prop_count; ++i) {
        JS_FreeAtom(ctx, props[i].atom);
      }
      js_free(ctx, props);
    }
    return Json(obj);
  }

  // Functions, symbols, etc. → null
  return Json(nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// EvalWithJson / EvalToJson
// ─────────────────────────────────────────────────────────────────────────────

ScriptValue EvalWithJson(ScriptContext& ctx, const std::string& var_name,
                         const Json& json, const std::string& code,
                         const std::string& filename) {
  // Inject the Json value as a global variable
  ScriptValue js_val = JsonToScriptValue(ctx, json);
  JSValue global = JS_GetGlobalObject(ctx.Raw());
  JS_SetPropertyStr(ctx.Raw(), global, var_name.c_str(),
                    JS_DupValue(ctx.Raw(), js_val.Raw()));
  JS_FreeValue(ctx.Raw(), global);

  // Evaluate the code
  return ctx.Eval(code, filename);
}

Json EvalToJson(ScriptContext& ctx, const std::string& code,
                const std::string& filename) {
  ScriptValue result = ctx.Eval(code, filename);
  if (result.IsException()) {
    return Json(nullptr);
  }
  return ScriptValueToJson(result);
}

// ─────────────────────────────────────────────────────────────────────────────
// JsonParseViaJs / JsonStringifyViaJs
// ─────────────────────────────────────────────────────────────────────────────

Json JsonParseViaJs(ScriptContext& ctx, const std::string& json_str) {
  // Use JS JSON.parse for faster parsing
  JSValue global = JS_GetGlobalObject(ctx.Raw());
  JSValue json_obj = JS_GetPropertyStr(ctx.Raw(), global, "JSON");
  JSValue parse_fn = JS_GetPropertyStr(ctx.Raw(), json_obj, "parse");

  JSValue str_val =
      JS_NewStringLen(ctx.Raw(), json_str.c_str(), json_str.size());
  JSValue result = JS_Call(ctx.Raw(), parse_fn, json_obj, 1, &str_val);

  JS_FreeValue(ctx.Raw(), str_val);
  JS_FreeValue(ctx.Raw(), parse_fn);
  JS_FreeValue(ctx.Raw(), json_obj);
  JS_FreeValue(ctx.Raw(), global);

  if (JS_IsException(result)) {
    JS_FreeValue(ctx.Raw(), result);
    return Json(nullptr);
  }

  ScriptValue sv(result, ctx.Raw());
  return ScriptValueToJson(sv);
}

std::string JsonStringifyViaJs(ScriptContext& ctx, const Json& json,
                               int indent) {
  // Convert Json to JS value first
  ScriptValue js_val = JsonToScriptValue(ctx, json);

  // Call JSON.stringify(value, null, indent)
  JSValue global = JS_GetGlobalObject(ctx.Raw());
  JSValue json_obj = JS_GetPropertyStr(ctx.Raw(), global, "JSON");
  JSValue stringify_fn = JS_GetPropertyStr(ctx.Raw(), json_obj, "stringify");

  JSValue args[3];
  args[0] = js_val.Raw();
  args[1] = JS_NULL;
  args[2] = (indent > 0) ? JS_NewInt32(ctx.Raw(), indent) : JS_UNDEFINED;

  JSValue result = JS_Call(ctx.Raw(), stringify_fn, json_obj, 3, args);

  if (indent > 0) JS_FreeValue(ctx.Raw(), args[2]);
  JS_FreeValue(ctx.Raw(), stringify_fn);
  JS_FreeValue(ctx.Raw(), json_obj);
  JS_FreeValue(ctx.Raw(), global);

  if (JS_IsException(result)) {
    JS_FreeValue(ctx.Raw(), result);
    return "null";
  }

  const char* cstr = JS_ToCString(ctx.Raw(), result);
  std::string str_result(cstr ? cstr : "null");
  JS_FreeCString(ctx.Raw(), cstr);
  JS_FreeValue(ctx.Raw(), result);

  return str_result;
}

}  // namespace xtils

#endif  // XTILS_HAS_SCRIPTING
