#pragma once

#ifdef XTILS_HAS_SCRIPTING

#include <string>

#include "xtils/scripting/context.h"
#include "xtils/scripting/value.h"
#include "xtils/utils/json.h"

namespace xtils {

// ─────────────────────────────────────────────────────────────────────────────
// Json → ScriptValue: Convert xtils::Json to a JS value in the given context.
// ─────────────────────────────────────────────────────────────────────────────

/// Convert a Json object to a ScriptValue (JS object/array/primitive).
/// Handles all Json types: null, bool, integer, float, string, array, object.
ScriptValue JsonToScriptValue(ScriptContext& ctx, const Json& json);

// ─────────────────────────────────────────────────────────────────────────────
// ScriptValue → Json: Convert a JS value back to xtils::Json.
// ─────────────────────────────────────────────────────────────────────────────

/// Convert a ScriptValue to a Json object.
/// Handles: undefined/null → null, bool, number (int or float), string,
/// array, object. Functions and other exotic types become null.
Json ScriptValueToJson(const ScriptValue& value);

// ─────────────────────────────────────────────────────────────────────────────
// Convenience: Eval with Json input/output
// ─────────────────────────────────────────────────────────────────────────────

/// Eval JS code with a Json value injected as a global variable.
/// Example: EvalWithJson(ctx, "data", myJson, "data.name + ' is ' + data.age")
ScriptValue EvalWithJson(ScriptContext& ctx, const std::string& var_name,
                         const Json& json, const std::string& code,
                         const std::string& filename = "<eval>");

/// Eval JS code and parse the result as Json.
/// The JS expression should return a JSON-serializable value.
Json EvalToJson(ScriptContext& ctx, const std::string& code,
                const std::string& filename = "<eval>");

// ─────────────────────────────────────────────────────────────────────────────
// Convenience: Parse/Stringify via JS engine (faster parse than C++ Json)
// ─────────────────────────────────────────────────────────────────────────────

/// Parse a JSON string using JS JSON.parse, return as xtils::Json.
/// Leverages QuickJS's fast JSON parser (2.5x faster than Json::parse).
Json JsonParseViaJs(ScriptContext& ctx, const std::string& json_str);

/// Stringify a Json object using JS JSON.stringify.
/// Optionally pretty-print with indent.
std::string JsonStringifyViaJs(ScriptContext& ctx, const Json& json,
                               int indent = 0);

}  // namespace xtils

#endif  // XTILS_HAS_SCRIPTING
