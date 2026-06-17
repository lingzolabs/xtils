# Error Model Convention

> Status: **Accepted** — applies to all new code in xtils. Existing legacy
> APIs are kept for backward compatibility and migrated opportunistically.

## TL;DR

| Use case | Choose |
|----------|--------|
| Operation can fail with a reason | `Result<T>` |
| Value may be absent (no error) | `std::optional<T>` |
| Boolean predicate / cheap state | `bool` |
| Catastrophic library boundary error | exception (only documented entry points) |

## When to use what

### `Result<T, Error>` — preferred for fallible operations

Use `Result<T>` whenever an operation can fail with an *informative reason*
the caller might want to act on or log. Returning `Result<T>` makes the
failure mode part of the type signature.

```cpp
xtils::Result<std::string> ReadConfig(const std::string& path);

auto r = ReadConfig("/etc/app.json");
if (!r) {
  LogE("config load failed: %s", r.error().message.c_str());
  return;
}
Use(*r);
```

`Result<T>` supports:
- `bool ok()`, `bool is_err()`, `explicit operator bool()`
- `T& value()`, `const T& value() const`, `T* operator->()`, `T& operator*()`
- `E& error()`, `const E& error() const`
- `T value_or(fallback)`
- `T unwrap_or_else(F)` — lazily compute a fallback
- `T expect(const char* msg)` — abort with `msg` if not ok (debug aid)
- `Result<U> map(F)` — transform value
- `Result<U> and_then(F)` — chain another fallible op
- `Result<void>` specialization for fire-and-check operations

Helpers:
- `Ok(v)` — make a success Result
- `Ok()` — make a `Result<void>` success
- `Err("msg")`, `Err(code, "msg")` — make a failure
- `MakeReadyFuture(v)` / `MakeErrorFuture<T>(err)` for `Future<T>` interop

### `std::optional<T>` — *value not present*, **not** *failed*

Use `optional` when the operation cannot fail, only the value may be absent.

```cpp
std::optional<int64_t> Config::GetInt(const std::string& path) const;
//                     ^ "no value at this path" — not an error
```

Don't use `optional` to signal an error. The caller has no way to learn the
reason. Use `Result<T>` instead.

### `bool` + output parameter — legacy, do not add new ones

Older xtils APIs use `bool foo(In, Out*)`:

```cpp
bool file_utils::read(const std::string& path, std::string* out);
```

This pattern predates `Result<T>` in the codebase. Keep it for binary
compatibility; do not introduce new APIs in this style. New `file_utils`
helpers should return `Result<std::string>` etc.

### Exceptions — only at well-documented library entry points

A few subsystems still use exceptions because they parse user-provided
content and the failure paths are highly varied:

- `Json::as_*()` throws `std::runtime_error` on type mismatch.
- `Json::parse(text)` overload without `error_code` returns `optional`;
  the `parse(text, ec)` overload reports failures via `std::error_code`.
- `xtils::fsm::*` and `BehaviorTree::*` throw on construction-time
  malformed input (unknown node type, missing children, etc.).

These are the only public APIs that throw. Wrap them at integration
boundaries. We will provide non-throwing `*Result` variants for FSM/BT
in a follow-up.

## How to migrate a `bool foo(in, Out*)` to `Result<Out>`

1. Add a new function `Result<Out> Foo(in)` next to the existing one.
2. Implement it in terms of the old one (or vice versa).
3. Update internal callers; leave the legacy API for external callers.
4. Mark the legacy API `[[deprecated("Use Foo() instead")]]` once the
   internal migration is complete.

## Don't

- Don't combine `Result<T>` with throwing helpers in the same code path.
- Don't catch `Error` from `Result` to convert to exceptions; let the
  caller decide how to handle it.
- Don't put `Result<bool>` where a plain `bool` (true=success) suffices.
- Don't return `Result<std::optional<T>>` — collapse the layers, prefer
  `Result<T>` with a "not found" code.
