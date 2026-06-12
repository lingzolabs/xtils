#pragma once

#ifdef XTILS_HAS_SCRIPTING

#include <string>
#include <vector>

#include "quickjs.h"

#include "xtils/scripting/context.h"
#include "xtils/scripting/value.h"

namespace xtils {

inline ScriptValue ToScriptValue(ScriptContext& ctx, int32_t val) {
  return ScriptValue(JS_NewInt32(ctx.Raw(), val), ctx.Raw());
}

inline ScriptValue ToScriptValue(ScriptContext& ctx, int64_t val) {
  return ScriptValue(JS_NewInt64(ctx.Raw(), val), ctx.Raw());
}

inline ScriptValue ToScriptValue(ScriptContext& ctx, double val) {
  return ScriptValue(JS_NewFloat64(ctx.Raw(), val), ctx.Raw());
}

inline ScriptValue ToScriptValue(ScriptContext& ctx, bool val) {
  return ScriptValue(JS_NewBool(ctx.Raw(), val), ctx.Raw());
}

inline ScriptValue ToScriptValue(ScriptContext& ctx, const std::string& val) {
  return ScriptValue(JS_NewString(ctx.Raw(), val.c_str()), ctx.Raw());
}

inline ScriptValue ToScriptValue(ScriptContext& ctx, const char* val) {
  return ScriptValue(JS_NewString(ctx.Raw(), val), ctx.Raw());
}

inline ScriptValue MakeUndefined(ScriptContext& ctx) {
  return ScriptValue(JS_UNDEFINED, ctx.Raw());
}

inline ScriptValue MakeNull(ScriptContext& ctx) {
  return ScriptValue(JS_NULL, ctx.Raw());
}

}  // namespace xtils

#endif  // XTILS_HAS_SCRIPTING
