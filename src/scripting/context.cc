#include "xtils/scripting/context.h"

#ifdef XTILS_HAS_SCRIPTING

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace xtils {

ScriptContext::ScriptContext(JSContext* ctx) : ctx_(ctx), owned_(true) {}

ScriptContext::ScriptContext(JSContext* ctx, bool owned)
    : ctx_(ctx), owned_(owned) {}

ScriptContext::~ScriptContext() {
  if (ctx_ && owned_) {
    for (auto* p : registered_func_ptrs_) {
      delete static_cast<NativeFunc*>(p);
    }
    JS_FreeContext(ctx_);
  }
}

ScriptContext::ScriptContext(ScriptContext&& other) noexcept
    : ctx_(other.ctx_),
      owned_(other.owned_),
      registered_func_ptrs_(std::move(other.registered_func_ptrs_)) {
  other.ctx_ = nullptr;
  other.owned_ = false;
}

ScriptContext& ScriptContext::operator=(ScriptContext&& other) noexcept {
  if (this != &other) {
    if (ctx_ && owned_) {
      for (auto* p : registered_func_ptrs_) {
        delete static_cast<NativeFunc*>(p);
      }
      JS_FreeContext(ctx_);
    }
    ctx_ = other.ctx_;
    owned_ = other.owned_;
    registered_func_ptrs_ = std::move(other.registered_func_ptrs_);
    other.ctx_ = nullptr;
    other.owned_ = false;
  }
  return *this;
}

ScriptValue ScriptContext::Eval(const std::string& code,
                                const std::string& filename) {
  JSValue result = JS_Eval(ctx_, code.c_str(), code.size(), filename.c_str(),
                           JS_EVAL_TYPE_GLOBAL);
  return ScriptValue(result, ctx_);
}

ScriptValue ScriptContext::EvalFile(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    throw std::runtime_error("ScriptContext::EvalFile: cannot open " + path);
  }
  std::ostringstream oss;
  oss << file.rdbuf();
  return Eval(oss.str(), path);
}

void ScriptContext::RegisterFunction(const std::string& name,
                                     NativeFunc func) {
  // Store the NativeFunc on the heap. The pointer is encoded as an int64
  // inside a JSValue passed as closure data to JS_NewCFunctionData.
  // The pointer lives until runtime destruction.
  auto* func_ptr = new NativeFunc(std::move(func));
  registered_func_ptrs_.push_back(func_ptr);
  JSValue data_val = JS_NewInt64(ctx_, reinterpret_cast<int64_t>(func_ptr));

  JSValue fn = JS_NewCFunctionData(
      ctx_,
      [](JSContext* ctx, JSValueConst /*this_val*/, int argc,
         JSValueConst* argv, int /*magic*/,
         JSValueConst* func_data) -> JSValue {
        int64_t ptr_val = 0;
        JS_ToInt64(ctx, &ptr_val, func_data[0]);
        auto* native_func = reinterpret_cast<NativeFunc*>(ptr_val);

        std::vector<ScriptValue> args;
        args.reserve(argc);
        for (int i = 0; i < argc; ++i) {
          args.emplace_back(JS_DupValue(ctx, argv[i]), ctx);
        }

        ScriptContext tmp_ctx(ctx, false);
        ScriptValue result = (*native_func)(tmp_ctx, args);
        JSValue ret = JS_DupValue(ctx, result.Raw());
        return ret;
      },
      0, 0, 1, &data_val);

  JS_FreeValue(ctx_, data_val);

  JSValue global = JS_GetGlobalObject(ctx_);
  JS_SetPropertyStr(ctx_, global, name.c_str(), fn);
  JS_FreeValue(ctx_, global);
}

}  // namespace xtils

#endif  // XTILS_HAS_SCRIPTING
