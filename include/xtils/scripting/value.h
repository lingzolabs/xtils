#pragma once

#ifdef XTILS_HAS_SCRIPTING

#include <cstdint>
#include <string>

#include "quickjs.h"

namespace xtils {

class ScriptContext;

class ScriptValue {
 public:
  ScriptValue();
  explicit ScriptValue(JSValue val, JSContext* ctx);
  ~ScriptValue();

  ScriptValue(const ScriptValue&) = delete;
  ScriptValue& operator=(const ScriptValue&) = delete;
  ScriptValue(ScriptValue&& other) noexcept;
  ScriptValue& operator=(ScriptValue&& other) noexcept;

  bool IsString() const;
  bool IsNumber() const;
  bool IsBool() const;
  bool IsNull() const;
  bool IsUndefined() const;
  bool IsObject() const;
  bool IsArray() const;
  bool IsException() const;

  std::string ToString() const;
  int64_t ToInt() const;
  double ToDouble() const;
  bool ToBool() const;

  JSValue Raw() const { return val_; }
  JSContext* Ctx() const { return ctx_; }

  void Swap(ScriptValue& other) noexcept;

 private:
  void Free();

  JSContext* ctx_;
  JSValue val_;
};

}  // namespace xtils

#endif  // XTILS_HAS_SCRIPTING
