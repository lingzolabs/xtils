#pragma once

#ifdef XTILS_HAS_SCRIPTING

#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "quickjs.h"

#include "xtils/scripting/context.h"
#include "xtils/scripting/value.h"

namespace xtils {

// ─────────────────────────────────────────────────────────────────────────────
// ClassBinding<T> — Register a C++ class into JavaScript
//
// Usage:
//   ClassBinding<MyClass>::Define(ctx, "MyClass")
//       .Constructor<int, std::string>()  // ctor(int, string)
//       .Method("doSomething", &MyClass::DoSomething)
//       .Method("getValue", &MyClass::GetValue)
//       .Property("name", &MyClass::GetName, &MyClass::SetName)
//       .PropertyReadonly("id", &MyClass::GetId)
//       .Register();
//
// Then in JS:
//   let obj = new MyClass(42, "hello");
//   obj.doSomething();
//   let v = obj.getValue();
//   obj.name = "new name";
//   console.log(obj.id);
// ─────────────────────────────────────────────────────────────────────────────

/// Type-erased method wrapper
using MethodFunc = std::function<JSValue(JSContext*, void*, int, JSValueConst*)>;
/// Getter/Setter pair
using GetterFunc = std::function<JSValue(JSContext*, void*)>;
using SetterFunc = std::function<JSValue(JSContext*, void*, JSValue)>;

namespace detail {

// ─── Argument conversion helpers ───

template <typename T>
T FromJsValue(JSContext* ctx, JSValueConst val);

template <>
inline int32_t FromJsValue<int32_t>(JSContext* ctx, JSValueConst val) {
  int32_t r = 0;
  JS_ToInt32(ctx, &r, val);
  return r;
}

template <>
inline int64_t FromJsValue<int64_t>(JSContext* ctx, JSValueConst val) {
  int64_t r = 0;
  JS_ToInt64(ctx, &r, val);
  return r;
}

template <>
inline double FromJsValue<double>(JSContext* ctx, JSValueConst val) {
  double r = 0;
  JS_ToFloat64(ctx, &r, val);
  return r;
}

template <>
inline float FromJsValue<float>(JSContext* ctx, JSValueConst val) {
  return static_cast<float>(FromJsValue<double>(ctx, val));
}

template <>
inline bool FromJsValue<bool>(JSContext* ctx, JSValueConst val) {
  return JS_ToBool(ctx, val) != 0;
}

template <>
inline std::string FromJsValue<std::string>(JSContext* ctx, JSValueConst val) {
  const char* s = JS_ToCString(ctx, val);
  std::string result(s ? s : "");
  JS_FreeCString(ctx, s);
  return result;
}

// ─── Return value conversion helpers ───

template <typename T>
JSValue ToJsValue(JSContext* ctx, const T& val);

template <>
inline JSValue ToJsValue<int32_t>(JSContext* ctx, const int32_t& val) {
  return JS_NewInt32(ctx, val);
}

template <>
inline JSValue ToJsValue<int64_t>(JSContext* ctx, const int64_t& val) {
  return JS_NewInt64(ctx, val);
}

template <>
inline JSValue ToJsValue<double>(JSContext* ctx, const double& val) {
  return JS_NewFloat64(ctx, val);
}

template <>
inline JSValue ToJsValue<float>(JSContext* ctx, const float& val) {
  return JS_NewFloat64(ctx, static_cast<double>(val));
}

template <>
inline JSValue ToJsValue<bool>(JSContext* ctx, const bool& val) {
  return JS_NewBool(ctx, val);
}

template <>
inline JSValue ToJsValue<std::string>(JSContext* ctx, const std::string& val) {
  return JS_NewStringLen(ctx, val.c_str(), val.size());
}

// ─── Method wrapper: member function → MethodFunc ───

template <typename T, typename Ret, typename... Args, size_t... I>
JSValue CallMethod(JSContext* ctx, void* opaque, int argc, JSValueConst* argv,
                   Ret (T::*method)(Args...), std::index_sequence<I...>) {
  (void)argc;
  T* obj = static_cast<T*>(opaque);
  if constexpr (std::is_void_v<Ret>) {
    (obj->*method)(FromJsValue<std::decay_t<Args>>(ctx, argv[I])...);
    return JS_UNDEFINED;
  } else {
    auto result = (obj->*method)(
        FromJsValue<std::decay_t<Args>>(ctx, argv[I])...);
    return ToJsValue(ctx, result);
  }
}

template <typename T, typename Ret, typename... Args, size_t... I>
JSValue CallConstMethod(JSContext* ctx, void* opaque, int argc,
                        JSValueConst* argv, Ret (T::*method)(Args...) const,
                        std::index_sequence<I...>) {
  (void)argc;
  T* obj = static_cast<T*>(opaque);
  if constexpr (std::is_void_v<Ret>) {
    (obj->*method)(FromJsValue<std::decay_t<Args>>(ctx, argv[I])...);
    return JS_UNDEFINED;
  } else {
    auto result = (obj->*method)(
        FromJsValue<std::decay_t<Args>>(ctx, argv[I])...);
    return ToJsValue(ctx, result);
  }
}

// Template to store the class_id for type T
template <typename T>
struct ClassIdHolder {
  static JSClassID id;
};

template <typename T>
JSClassID ClassIdHolder<T>::id = 0;

}  // namespace detail

// ─────────────────────────────────────────────────────────────────────────────
// ClassDef: Stores all metadata for one class binding
// ─────────────────────────────────────────────────────────────────────────────

struct ClassMethodDef {
  std::string name;
  int arg_count;
  MethodFunc func;
};

struct ClassPropertyDef {
  std::string name;
  GetterFunc getter;
  SetterFunc setter;  // nullptr for readonly
};

struct ClassDefData {
  std::string class_name;
  JSClassID class_id = 0;
  std::function<void*(JSContext*, int, JSValueConst*)> constructor;
  std::function<void(void*)> destructor;
  std::vector<ClassMethodDef> methods;
  std::vector<ClassPropertyDef> properties;
};

// Free functions (implemented in class_binding.cc)
void RegisterClassImpl(ScriptContext& ctx, std::shared_ptr<ClassDefData> def);
JSValue WrapObjectImpl(ScriptContext& ctx, void* ptr, JSClassID class_id);
void* UnwrapObjectImpl(JSContext* ctx, JSValue val, JSClassID class_id);

// ─────────────────────────────────────────────────────────────────────────────
// ClassBinding<T> — Fluent builder for registering C++ classes in JS
// ─────────────────────────────────────────────────────────────────────────────

template <typename T>
class ClassBinding {
 public:
  /// Start defining a class binding
  static ClassBinding Define(ScriptContext& ctx, const std::string& name) {
    return ClassBinding(ctx, name);
  }

  /// Register a constructor with typed arguments.
  template <typename... Args>
  ClassBinding& Constructor() {
    def_.constructor = [](JSContext* ctx, int argc,
                          JSValueConst* argv) -> void* {
      (void)argc;
      return ConstructHelper<Args...>(ctx, argv,
                                      std::index_sequence_for<Args...>{});
    };
    return *this;
  }

  /// Register a default constructor (no arguments)
  ClassBinding& DefaultConstructor() {
    def_.constructor = [](JSContext*, int, JSValueConst*) -> void* {
      return new T();
    };
    return *this;
  }

  /// Register a member method (non-const)
  template <typename Ret, typename... Args>
  ClassBinding& Method(const std::string& name, Ret (T::*method)(Args...)) {
    auto m = method;
    def_.methods.push_back(
        {name, static_cast<int>(sizeof...(Args)),
         [m](JSContext* ctx, void* opaque, int argc,
             JSValueConst* argv) -> JSValue {
           return detail::CallMethod(ctx, opaque, argc, argv, m,
                                     std::index_sequence_for<Args...>{});
         }});
    return *this;
  }

  /// Register a const member method
  template <typename Ret, typename... Args>
  ClassBinding& Method(const std::string& name,
                       Ret (T::*method)(Args...) const) {
    auto m = method;
    def_.methods.push_back(
        {name, static_cast<int>(sizeof...(Args)),
         [m](JSContext* ctx, void* opaque, int argc,
             JSValueConst* argv) -> JSValue {
           return detail::CallConstMethod(ctx, opaque, argc, argv, m,
                                          std::index_sequence_for<Args...>{});
         }});
    return *this;
  }

  /// Register a method via lambda/function (custom logic)
  ClassBinding& Method(const std::string& name, int arg_count,
                       MethodFunc func) {
    def_.methods.push_back({name, arg_count, std::move(func)});
    return *this;
  }

  /// Register a read-write property (getter + setter methods)
  template <typename PropT>
  ClassBinding& Property(const std::string& name, PropT (T::*getter)() const,
                         void (T::*setter)(PropT)) {
    auto g = getter;
    auto s = setter;
    def_.properties.push_back(
        {name,
         [g](JSContext* ctx, void* opaque) -> JSValue {
           T* obj = static_cast<T*>(opaque);
           return detail::ToJsValue(ctx, (obj->*g)());
         },
         [s](JSContext* ctx, void* opaque, JSValue val) -> JSValue {
           T* obj = static_cast<T*>(opaque);
           (obj->*s)(detail::FromJsValue<PropT>(ctx, val));
           return JS_UNDEFINED;
         }});
    return *this;
  }

  /// Register a read-only property (getter only)
  template <typename PropT>
  ClassBinding& PropertyReadonly(const std::string& name,
                                 PropT (T::*getter)() const) {
    auto g = getter;
    def_.properties.push_back(
        {name,
         [g](JSContext* ctx, void* opaque) -> JSValue {
           T* obj = static_cast<T*>(opaque);
           return detail::ToJsValue(ctx, (obj->*g)());
         },
         nullptr});
    return *this;
  }

  /// Finalize and register the class in the JS context.
  void Register() {
    auto def = std::make_shared<ClassDefData>(std::move(def_));
    RegisterClassImpl(ctx_, def);
    detail::ClassIdHolder<T>::id = def->class_id;
  }

 private:
  ClassBinding(ScriptContext& ctx, const std::string& name) : ctx_(ctx) {
    def_.class_name = name;
    def_.destructor = [](void* ptr) { delete static_cast<T*>(ptr); };
  }

  template <typename... Args, size_t... I>
  static void* ConstructHelper(JSContext* ctx, JSValueConst* argv,
                                std::index_sequence<I...>) {
    return new T(detail::FromJsValue<std::decay_t<Args>>(ctx, argv[I])...);
  }

  ScriptContext& ctx_;
  ClassDefData def_;
};

// ─────────────────────────────────────────────────────────────────────────────
// WrapObject / UnwrapObject
// ─────────────────────────────────────────────────────────────────────────────

/// Wrap an existing C++ pointer as a JS object (JS does NOT own the pointer).
/// The class must have been previously registered via ClassBinding<T>::Register().
template <typename T>
ScriptValue WrapObject(ScriptContext& ctx, T* ptr) {
  JSClassID cid = detail::ClassIdHolder<T>::id;
  if (cid == 0) return ScriptValue(JS_EXCEPTION, ctx.Raw());
  JSValue val = WrapObjectImpl(ctx, static_cast<void*>(ptr), cid);
  return ScriptValue(val, ctx.Raw());
}

/// Extract the C++ pointer from a JS object created via ClassBinding.
/// Returns nullptr if the value is not the correct class.
template <typename T>
T* UnwrapObject(const ScriptValue& value) {
  JSClassID cid = detail::ClassIdHolder<T>::id;
  if (cid == 0) return nullptr;
  return static_cast<T*>(UnwrapObjectImpl(value.Ctx(), value.Raw(), cid));
}

}  // namespace xtils

#endif  // XTILS_HAS_SCRIPTING
