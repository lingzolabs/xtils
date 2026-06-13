#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "xtils/utils/json.h"

namespace xtils {
namespace serialize {

// Traits for custom serialization
template <typename T, typename = void>
struct has_to_json : std::false_type {};

template <typename T>
struct has_to_json<T, std::void_t<decltype(std::declval<T>().ToJson())>>
    : std::true_type {};

template <typename T, typename = void>
struct has_from_json : std::false_type {};

template <typename T>
struct has_from_json<
    T, std::void_t<decltype(T::FromJson(std::declval<const Json&>()))>>
    : std::true_type {};

// to_json helpers
inline Json to_json(bool v) { return Json(v); }
inline Json to_json(int v) { return Json(static_cast<int64_t>(v)); }
inline Json to_json(int64_t v) { return Json(v); }
inline Json to_json(uint32_t v) { return Json(static_cast<int64_t>(v)); }
inline Json to_json(uint64_t v) { return Json(static_cast<int64_t>(v)); }
inline Json to_json(float v) { return Json(static_cast<double>(v)); }
inline Json to_json(double v) { return Json(v); }
inline Json to_json(const std::string& v) { return Json(v); }
inline Json to_json(const char* v) { return Json(std::string(v)); }
inline Json to_json(const Json& v) { return v; }

template <typename T>
auto to_json(const T& v)
    -> std::enable_if_t<has_to_json<T>::value, Json> {
  return v.ToJson();
}

template <typename T>
Json to_json(const std::vector<T>& vec) {
  Json::array_t arr;
  for (const auto& item : vec) {
    arr.push_back(to_json(item));
  }
  return Json(arr);
}

// from_json helpers
inline bool from_json(const Json& j, bool& v) {
  if (!j.is_bool()) return false;
  v = j.as_bool();
  return true;
}

inline bool from_json(const Json& j, int& v) {
  if (!j.is_integer()) return false;
  v = static_cast<int>(j.as_integer());
  return true;
}

inline bool from_json(const Json& j, int64_t& v) {
  if (!j.is_integer()) return false;
  v = j.as_integer();
  return true;
}

inline bool from_json(const Json& j, uint32_t& v) {
  if (!j.is_integer()) return false;
  v = static_cast<uint32_t>(j.as_integer());
  return true;
}

inline bool from_json(const Json& j, uint64_t& v) {
  if (!j.is_integer()) return false;
  v = static_cast<uint64_t>(j.as_integer());
  return true;
}

inline bool from_json(const Json& j, float& v) {
  if (j.is_float()) { v = static_cast<float>(j.as_float()); return true; }
  if (j.is_integer()) { v = static_cast<float>(j.as_integer()); return true; }
  return false;
}

inline bool from_json(const Json& j, double& v) {
  if (j.is_float()) { v = j.as_float(); return true; }
  if (j.is_integer()) { v = static_cast<double>(j.as_integer()); return true; }
  return false;
}

inline bool from_json(const Json& j, std::string& v) {
  if (!j.is_string()) return false;
  v = j.as_string();
  return true;
}

template <typename T>
auto from_json(const Json& j, T& v)
    -> std::enable_if_t<has_from_json<T>::value, bool> {
  auto opt = T::FromJson(j);
  if (!opt) return false;
  v = std::move(*opt);
  return true;
}

template <typename T>
bool from_json(const Json& j, std::vector<T>& vec) {
  if (!j.is_array()) return false;
  vec.clear();
  for (const auto& item : j.as_array()) {
    T val;
    if (from_json(item, val)) {
      vec.push_back(std::move(val));
    }
  }
  return true;
}

}  // namespace serialize
}  // namespace xtils

// ============================================================================
// Serialization macros
// ============================================================================

// Helper macros for field enumeration
#define _XTILS_FIELD_TO_JSON_1(f) j[#f] = xtils::serialize::to_json(f)
#define _XTILS_FIELD_TO_JSON_2(f, ...) _XTILS_FIELD_TO_JSON_1(f); _XTILS_FIELD_TO_JSON_1(__VA_ARGS__)
#define _XTILS_FIELD_TO_JSON_3(f, ...) _XTILS_FIELD_TO_JSON_1(f); _XTILS_FIELD_TO_JSON_2(__VA_ARGS__)
#define _XTILS_FIELD_TO_JSON_4(f, ...) _XTILS_FIELD_TO_JSON_1(f); _XTILS_FIELD_TO_JSON_3(__VA_ARGS__)
#define _XTILS_FIELD_TO_JSON_5(f, ...) _XTILS_FIELD_TO_JSON_1(f); _XTILS_FIELD_TO_JSON_4(__VA_ARGS__)
#define _XTILS_FIELD_TO_JSON_6(f, ...) _XTILS_FIELD_TO_JSON_1(f); _XTILS_FIELD_TO_JSON_5(__VA_ARGS__)
#define _XTILS_FIELD_TO_JSON_7(f, ...) _XTILS_FIELD_TO_JSON_1(f); _XTILS_FIELD_TO_JSON_6(__VA_ARGS__)
#define _XTILS_FIELD_TO_JSON_8(f, ...) _XTILS_FIELD_TO_JSON_1(f); _XTILS_FIELD_TO_JSON_7(__VA_ARGS__)
#define _XTILS_FIELD_TO_JSON_9(f, ...) _XTILS_FIELD_TO_JSON_1(f); _XTILS_FIELD_TO_JSON_8(__VA_ARGS__)
#define _XTILS_FIELD_TO_JSON_10(f, ...) _XTILS_FIELD_TO_JSON_1(f); _XTILS_FIELD_TO_JSON_9(__VA_ARGS__)
#define _XTILS_FIELD_TO_JSON_11(f, ...) _XTILS_FIELD_TO_JSON_1(f); _XTILS_FIELD_TO_JSON_10(__VA_ARGS__)
#define _XTILS_FIELD_TO_JSON_12(f, ...) _XTILS_FIELD_TO_JSON_1(f); _XTILS_FIELD_TO_JSON_11(__VA_ARGS__)
#define _XTILS_FIELD_TO_JSON_13(f, ...) _XTILS_FIELD_TO_JSON_1(f); _XTILS_FIELD_TO_JSON_12(__VA_ARGS__)
#define _XTILS_FIELD_TO_JSON_14(f, ...) _XTILS_FIELD_TO_JSON_1(f); _XTILS_FIELD_TO_JSON_13(__VA_ARGS__)
#define _XTILS_FIELD_TO_JSON_15(f, ...) _XTILS_FIELD_TO_JSON_1(f); _XTILS_FIELD_TO_JSON_14(__VA_ARGS__)
#define _XTILS_FIELD_TO_JSON_16(f, ...) _XTILS_FIELD_TO_JSON_1(f); _XTILS_FIELD_TO_JSON_15(__VA_ARGS__)

#define _XTILS_FIELD_FROM_JSON_1(f) \
  do { if (j.contains(#f)) xtils::serialize::from_json(j[#f], obj.f); } while(0)
#define _XTILS_FIELD_FROM_JSON_2(f, ...) _XTILS_FIELD_FROM_JSON_1(f); _XTILS_FIELD_FROM_JSON_1(__VA_ARGS__)
#define _XTILS_FIELD_FROM_JSON_3(f, ...) _XTILS_FIELD_FROM_JSON_1(f); _XTILS_FIELD_FROM_JSON_2(__VA_ARGS__)
#define _XTILS_FIELD_FROM_JSON_4(f, ...) _XTILS_FIELD_FROM_JSON_1(f); _XTILS_FIELD_FROM_JSON_3(__VA_ARGS__)
#define _XTILS_FIELD_FROM_JSON_5(f, ...) _XTILS_FIELD_FROM_JSON_1(f); _XTILS_FIELD_FROM_JSON_4(__VA_ARGS__)
#define _XTILS_FIELD_FROM_JSON_6(f, ...) _XTILS_FIELD_FROM_JSON_1(f); _XTILS_FIELD_FROM_JSON_5(__VA_ARGS__)
#define _XTILS_FIELD_FROM_JSON_7(f, ...) _XTILS_FIELD_FROM_JSON_1(f); _XTILS_FIELD_FROM_JSON_6(__VA_ARGS__)
#define _XTILS_FIELD_FROM_JSON_8(f, ...) _XTILS_FIELD_FROM_JSON_1(f); _XTILS_FIELD_FROM_JSON_7(__VA_ARGS__)
#define _XTILS_FIELD_FROM_JSON_9(f, ...) _XTILS_FIELD_FROM_JSON_1(f); _XTILS_FIELD_FROM_JSON_8(__VA_ARGS__)
#define _XTILS_FIELD_FROM_JSON_10(f, ...) _XTILS_FIELD_FROM_JSON_1(f); _XTILS_FIELD_FROM_JSON_9(__VA_ARGS__)
#define _XTILS_FIELD_FROM_JSON_11(f, ...) _XTILS_FIELD_FROM_JSON_1(f); _XTILS_FIELD_FROM_JSON_10(__VA_ARGS__)
#define _XTILS_FIELD_FROM_JSON_12(f, ...) _XTILS_FIELD_FROM_JSON_1(f); _XTILS_FIELD_FROM_JSON_11(__VA_ARGS__)
#define _XTILS_FIELD_FROM_JSON_13(f, ...) _XTILS_FIELD_FROM_JSON_1(f); _XTILS_FIELD_FROM_JSON_12(__VA_ARGS__)
#define _XTILS_FIELD_FROM_JSON_14(f, ...) _XTILS_FIELD_FROM_JSON_1(f); _XTILS_FIELD_FROM_JSON_13(__VA_ARGS__)
#define _XTILS_FIELD_FROM_JSON_15(f, ...) _XTILS_FIELD_FROM_JSON_1(f); _XTILS_FIELD_FROM_JSON_14(__VA_ARGS__)
#define _XTILS_FIELD_FROM_JSON_16(f, ...) _XTILS_FIELD_FROM_JSON_1(f); _XTILS_FIELD_FROM_JSON_15(__VA_ARGS__)

// Count variadic arguments (up to 16)
#define _XTILS_NARG(...) _XTILS_NARG_(__VA_ARGS__, _XTILS_RSEQ_N())
#define _XTILS_NARG_(...) _XTILS_ARG_N(__VA_ARGS__)
#define _XTILS_ARG_N(_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,_15,_16,N,...) N
#define _XTILS_RSEQ_N() 16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1

// Dispatcher
#define _XTILS_CONCAT(a, b) a##b
#define _XTILS_DISPATCH(macro, n) _XTILS_CONCAT(macro, n)

// Main macro: generates ToJson() and FromJson() methods
// Usage:
//   struct Point {
//     int x = 0;
//     int y = 0;
//     std::string label;
//     XTILS_SERIALIZABLE(Point, x, y, label)
//   };
//
//   Point p{1, 2, "origin"};
//   Json j = p.ToJson();           // {"x":1,"y":2,"label":"origin"}
//   auto p2 = Point::FromJson(j);  // optional<Point>
//
#define XTILS_SERIALIZABLE(Type, ...)                                       \
  xtils::Json ToJson() const {                                             \
    xtils::Json j = xtils::Json::object_t{};                               \
    _XTILS_DISPATCH(_XTILS_FIELD_TO_JSON_, _XTILS_NARG(__VA_ARGS__))       \
    (__VA_ARGS__);                                                          \
    return j;                                                              \
  }                                                                        \
  static std::optional<Type> FromJson(const xtils::Json& j) {              \
    if (!j.is_object()) return std::nullopt;                               \
    Type obj;                                                              \
    _XTILS_DISPATCH(_XTILS_FIELD_FROM_JSON_, _XTILS_NARG(__VA_ARGS__))     \
    (__VA_ARGS__);                                                          \
    return obj;                                                            \
  }
