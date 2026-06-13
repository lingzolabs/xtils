#include "xtils/scripting/class_binding.h"

#ifdef XTILS_HAS_SCRIPTING

#include <cstring>
#include <unordered_map>

namespace xtils {

// ─────────────────────────────────────────────────────────────────────────────
// Global registry: maps class_id → ClassDefData for trampoline access
// ─────────────────────────────────────────────────────────────────────────────

namespace {

struct ClassRegistry {
  std::unordered_map<uint32_t, std::shared_ptr<ClassDefData>> defs;

  static ClassRegistry& Instance() {
    static ClassRegistry instance;
    return instance;
  }
};

// Metadata attached to each JS object: pointer + ownership flag
struct OpaqueData {
  void* ptr;
  bool owned;
  uint32_t class_id;
  ClassDefData* def;
};

void ClassFinalizer(JSRuntime* rt, JSValue val) {
  JSClassID cid = JS_GetClassID(val);
  OpaqueData* data = static_cast<OpaqueData*>(JS_GetOpaque(val, cid));
  if (!data) return;

  if (data->owned && data->ptr && data->def && data->def->destructor) {
    data->def->destructor(data->ptr);
  }
  js_free_rt(rt, data);
}

JSValue MethodTrampoline(JSContext* ctx, JSValueConst this_val, int argc,
                         JSValueConst* argv, int magic) {
  JSClassID cid = JS_GetClassID(this_val);
  OpaqueData* data =
      static_cast<OpaqueData*>(JS_GetOpaque2(ctx, this_val, cid));
  if (!data || !data->ptr || !data->def) return JS_EXCEPTION;

  auto& methods = data->def->methods;
  if (magic < 0 || magic >= static_cast<int>(methods.size()))
    return JS_EXCEPTION;

  if (argc < methods[magic].arg_count) return JS_EXCEPTION;

  return methods[magic].func(ctx, data->ptr, argc, argv);
}

JSValue PropertyGetTrampoline(JSContext* ctx, JSValueConst this_val,
                              int magic) {
  JSClassID cid = JS_GetClassID(this_val);
  OpaqueData* data =
      static_cast<OpaqueData*>(JS_GetOpaque2(ctx, this_val, cid));
  if (!data || !data->ptr || !data->def) return JS_EXCEPTION;

  auto& props = data->def->properties;
  if (magic < 0 || magic >= static_cast<int>(props.size()))
    return JS_EXCEPTION;

  return props[magic].getter(ctx, data->ptr);
}

JSValue PropertySetTrampoline(JSContext* ctx, JSValueConst this_val,
                              JSValue val, int magic) {
  JSClassID cid = JS_GetClassID(this_val);
  OpaqueData* data =
      static_cast<OpaqueData*>(JS_GetOpaque2(ctx, this_val, cid));
  if (!data || !data->ptr || !data->def) return JS_EXCEPTION;

  auto& props = data->def->properties;
  if (magic < 0 || magic >= static_cast<int>(props.size()))
    return JS_EXCEPTION;
  if (!props[magic].setter) return JS_EXCEPTION;

  return props[magic].setter(ctx, data->ptr, val);
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// RegisterClassImpl
// ─────────────────────────────────────────────────────────────────────────────

void RegisterClassImpl(ScriptContext& ctx, std::shared_ptr<ClassDefData> def) {
  JSContext* js_ctx = ctx.Raw();
  JSRuntime* rt = JS_GetRuntime(js_ctx);

  // Allocate a new class ID
  JSClassID class_id = 0;
  JS_NewClassID(rt, &class_id);
  def->class_id = class_id;

  // Store in registry
  ClassRegistry::Instance().defs[class_id] = def;

  // Define the JS class with our finalizer
  JSClassDef class_def = {};
  class_def.class_name = def->class_name.c_str();
  class_def.finalizer = ClassFinalizer;
  JS_NewClass(rt, class_id, &class_def);

  // Create prototype object with methods and properties
  JSValue proto = JS_NewObject(js_ctx);

  // Register methods on prototype using JS_NewCFunctionMagic
  for (size_t i = 0; i < def->methods.size(); ++i) {
    JSValue fn = JS_NewCFunctionMagic(
        js_ctx, MethodTrampoline, def->methods[i].name.c_str(),
        def->methods[i].arg_count, JS_CFUNC_generic_magic,
        static_cast<int>(i));
    JS_SetPropertyStr(js_ctx, proto, def->methods[i].name.c_str(), fn);
  }

  // Register properties using JS_DefinePropertyGetSet
  for (size_t i = 0; i < def->properties.size(); ++i) {
    // Create getter function
    JSCFunctionType gt;
    gt.getter_magic = PropertyGetTrampoline;
    JSValue getter = JS_NewCFunction2(
        js_ctx, gt.generic, def->properties[i].name.c_str(), 0,
        JS_CFUNC_getter_magic, static_cast<int>(i));

    // Create setter function (or undefined if readonly)
    JSValue setter = JS_UNDEFINED;
    if (def->properties[i].setter) {
      JSCFunctionType st;
      st.setter_magic = PropertySetTrampoline;
      setter = JS_NewCFunction2(
          js_ctx, st.generic, def->properties[i].name.c_str(), 1,
          JS_CFUNC_setter_magic, static_cast<int>(i));
    }

    JSAtom atom = JS_NewAtom(js_ctx, def->properties[i].name.c_str());
    JS_DefinePropertyGetSet(js_ctx, proto, atom, getter, setter,
                            JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
    JS_FreeAtom(js_ctx, atom);
  }

  // Create constructor function
  JSValue ctor = JS_NewCFunction2(
      js_ctx,
      [](JSContext* c, JSValueConst new_target, int argc,
         JSValueConst* argv) -> JSValue {
        // Retrieve class_id stored on the constructor
        JSValue cid_val =
            JS_GetPropertyStr(c, new_target, "__xtils_class_id__");
        int64_t cid64 = 0;
        JS_ToInt64(c, &cid64, cid_val);
        JS_FreeValue(c, cid_val);
        JSClassID cid = static_cast<JSClassID>(cid64);

        auto& registry = ClassRegistry::Instance();
        auto it = registry.defs.find(cid);
        if (it == registry.defs.end()) return JS_EXCEPTION;

        // Call C++ constructor
        void* ptr = it->second->constructor(c, argc, argv);
        if (!ptr) return JS_EXCEPTION;

        // Create JS object with proper prototype
        JSValue proto = JS_GetPropertyStr(c, new_target, "prototype");
        if (JS_IsException(proto)) {
          it->second->destructor(ptr);
          return JS_EXCEPTION;
        }
        JSValue obj = JS_NewObjectProtoClass(c, proto, cid);
        JS_FreeValue(c, proto);
        if (JS_IsException(obj)) {
          it->second->destructor(ptr);
          return JS_EXCEPTION;
        }

        // Attach C++ pointer as opaque data
        OpaqueData* opaque =
            static_cast<OpaqueData*>(js_mallocz(c, sizeof(OpaqueData)));
        opaque->ptr = ptr;
        opaque->owned = true;
        opaque->class_id = cid;
        opaque->def = it->second.get();
        JS_SetOpaque(obj, opaque);

        return obj;
      },
      def->class_name.c_str(), 0, JS_CFUNC_constructor, 0);

  // Store class_id on constructor for retrieval in the ctor trampoline
  JS_DefinePropertyValueStr(js_ctx, ctor, "__xtils_class_id__",
                            JS_NewInt64(js_ctx, class_id), 0);

  // Link constructor ↔ prototype
  JS_SetConstructor(js_ctx, ctor, proto);
  JS_SetClassProto(js_ctx, class_id, proto);

  // Register constructor as global
  JSValue global = JS_GetGlobalObject(js_ctx);
  JS_SetPropertyStr(js_ctx, global, def->class_name.c_str(), ctor);
  JS_FreeValue(js_ctx, global);
}

// ─────────────────────────────────────────────────────────────────────────────
// WrapObjectImpl / UnwrapObjectImpl
// ─────────────────────────────────────────────────────────────────────────────

JSValue WrapObjectImpl(ScriptContext& ctx, void* ptr, JSClassID class_id) {
  JSContext* js_ctx = ctx.Raw();
  JSValue proto = JS_GetClassProto(js_ctx, class_id);
  JSValue obj = JS_NewObjectProtoClass(js_ctx, proto, class_id);
  JS_FreeValue(js_ctx, proto);

  if (JS_IsException(obj)) return JS_EXCEPTION;

  auto& registry = ClassRegistry::Instance();
  auto it = registry.defs.find(class_id);
  ClassDefData* def_ptr = (it != registry.defs.end()) ? it->second.get() : nullptr;

  OpaqueData* opaque =
      static_cast<OpaqueData*>(js_mallocz(js_ctx, sizeof(OpaqueData)));
  opaque->ptr = ptr;
  opaque->owned = false;  // Non-owning wrap
  opaque->class_id = class_id;
  opaque->def = def_ptr;
  JS_SetOpaque(obj, opaque);

  return obj;
}

void* UnwrapObjectImpl(JSContext* ctx, JSValue val, JSClassID class_id) {
  OpaqueData* data =
      static_cast<OpaqueData*>(JS_GetOpaque2(ctx, val, class_id));
  if (!data) return nullptr;
  return data->ptr;
}

}  // namespace xtils

#endif  // XTILS_HAS_SCRIPTING
