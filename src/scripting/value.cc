#include "xtils/scripting/value.h"

#ifdef XTILS_HAS_SCRIPTING

#include <utility>

namespace xtils {

ScriptValue::ScriptValue() : ctx_(nullptr), val_(JS_UNDEFINED) {}

ScriptValue::ScriptValue(JSValue val, JSContext* ctx) : ctx_(ctx), val_(val) {}

ScriptValue::~ScriptValue() { Free(); }

ScriptValue::ScriptValue(ScriptValue&& other) noexcept
    : ctx_(other.ctx_), val_(other.val_) {
  other.ctx_ = nullptr;
  other.val_ = JS_UNDEFINED;
}

ScriptValue& ScriptValue::operator=(ScriptValue&& other) noexcept {
  if (this != &other) {
    Free();
    ctx_ = other.ctx_;
    val_ = other.val_;
    other.ctx_ = nullptr;
    other.val_ = JS_UNDEFINED;
  }
  return *this;
}

void ScriptValue::Free() {
  if (ctx_) {
    JS_FreeValue(ctx_, val_);
  }
  ctx_ = nullptr;
  val_ = JS_UNDEFINED;
}

void ScriptValue::Swap(ScriptValue& other) noexcept {
  std::swap(ctx_, other.ctx_);
  std::swap(val_, other.val_);
}

bool ScriptValue::IsString() const { return JS_IsString(val_); }
bool ScriptValue::IsNumber() const { return JS_IsNumber(val_); }
bool ScriptValue::IsBool() const { return JS_IsBool(val_); }
bool ScriptValue::IsNull() const { return JS_IsNull(val_); }
bool ScriptValue::IsUndefined() const { return JS_IsUndefined(val_); }
bool ScriptValue::IsObject() const { return JS_IsObject(val_); }
bool ScriptValue::IsException() const { return JS_IsException(val_); }

bool ScriptValue::IsArray() const {
  if (!ctx_) return false;
  return JS_IsArray(val_);
}

std::string ScriptValue::ToString() const {
  if (!ctx_) return "";
  const char* cstr = JS_ToCString(ctx_, val_);
  if (!cstr) return "";
  std::string result(cstr);
  JS_FreeCString(ctx_, cstr);
  return result;
}

int64_t ScriptValue::ToInt() const {
  int64_t result = 0;
  JS_ToInt64(ctx_, &result, val_);
  return result;
}

double ScriptValue::ToDouble() const {
  double result = 0.0;
  JS_ToFloat64(ctx_, &result, val_);
  return result;
}

bool ScriptValue::ToBool() const { return JS_ToBool(ctx_, val_) != 0; }

}  // namespace xtils

#endif  // XTILS_HAS_SCRIPTING
