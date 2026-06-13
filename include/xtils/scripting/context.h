#pragma once

#ifdef XTILS_HAS_SCRIPTING

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "quickjs.h"

#include "xtils/scripting/value.h"

namespace xtils {

class ScriptEngine;

using NativeFunc =
    std::function<ScriptValue(ScriptContext& ctx,
                              const std::vector<ScriptValue>& args)>;

class ScriptContext {
 public:
  ~ScriptContext();

  ScriptContext(const ScriptContext&) = delete;
  ScriptContext& operator=(const ScriptContext&) = delete;
  ScriptContext(ScriptContext&& other) noexcept;
  ScriptContext& operator=(ScriptContext&& other) noexcept;

  ScriptValue Eval(const std::string& code,
                   const std::string& filename = "<eval>");
  ScriptValue EvalFile(const std::string& path);

  void RegisterFunction(const std::string& name, NativeFunc func);

  JSContext* Raw() { return ctx_; }

 private:
  friend class ScriptEngine;
  // Owned context (from CreateContext)
  explicit ScriptContext(JSContext* ctx);
  // Borrowed context (does NOT free on destruction)
  ScriptContext(JSContext* ctx, bool owned);

  JSContext* ctx_;
  bool owned_;
  std::vector<void*> registered_func_ptrs_;
};

}  // namespace xtils

#endif  // XTILS_HAS_SCRIPTING
