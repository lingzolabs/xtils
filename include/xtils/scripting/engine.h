#pragma once

#ifdef XTILS_HAS_SCRIPTING

#include <cstddef>
#include <memory>

#include "quickjs.h"

#include "xtils/scripting/context.h"

namespace xtils {

class ScriptEngine {
 public:
  ScriptEngine();
  ~ScriptEngine();

  ScriptEngine(const ScriptEngine&) = delete;
  ScriptEngine& operator=(const ScriptEngine&) = delete;
  ScriptEngine(ScriptEngine&& other) noexcept;
  ScriptEngine& operator=(ScriptEngine&& other) noexcept;

  std::unique_ptr<ScriptContext> CreateContext();

  void SetMemoryLimit(size_t limit);
  void SetMaxStackSize(size_t stack_size);
  void RunGC();

 private:
  JSRuntime* rt_;
};

}  // namespace xtils

#endif  // XTILS_HAS_SCRIPTING
