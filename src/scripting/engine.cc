#include "xtils/scripting/engine.h"

#ifdef XTILS_HAS_SCRIPTING

#include <stdexcept>

namespace xtils {

ScriptEngine::ScriptEngine() : rt_(JS_NewRuntime()) {
  if (!rt_) {
    throw std::runtime_error("ScriptEngine: failed to create runtime");
  }
}

ScriptEngine::~ScriptEngine() {
  if (rt_) JS_FreeRuntime(rt_);
}

ScriptEngine::ScriptEngine(ScriptEngine&& other) noexcept : rt_(other.rt_) {
  other.rt_ = nullptr;
}

ScriptEngine& ScriptEngine::operator=(ScriptEngine&& other) noexcept {
  if (this != &other) {
    if (rt_) JS_FreeRuntime(rt_);
    rt_ = other.rt_;
    other.rt_ = nullptr;
  }
  return *this;
}

std::unique_ptr<ScriptContext> ScriptEngine::CreateContext() {
  JSContext* ctx = JS_NewContext(rt_);
  if (!ctx) {
    throw std::runtime_error("ScriptEngine: failed to create context");
  }
  return std::unique_ptr<ScriptContext>(new ScriptContext(ctx));
}

void ScriptEngine::SetMemoryLimit(size_t limit) {
  JS_SetMemoryLimit(rt_, limit);
}

void ScriptEngine::SetMaxStackSize(size_t stack_size) {
  JS_SetMaxStackSize(rt_, stack_size);
}

void ScriptEngine::RunGC() { JS_RunGC(rt_); }

}  // namespace xtils

#endif  // XTILS_HAS_SCRIPTING
