#pragma once

#include <exception>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "xtils/utils/json.h"

namespace xtils {

namespace detail {
// Trait to detect std::vector<T>
template <typename T>
struct is_vector : std::false_type {};
template <typename T, typename A>
struct is_vector<std::vector<T, A>> : std::true_type {};
template <typename T>
inline constexpr bool is_vector_v = is_vector<T>::value;
}  // namespace detail

class Config {
 public:
  // Option definition for command line parsing
  struct Option {
    std::string name;
    std::string description;
    Json default_value;
    bool required = false;

    Option() = default;
    Option(const std::string& name, const std::string& description,
           const Json& default_value, bool required = false)
        : name(name),
          description(description),
          default_value(default_value),
          required(required) {}
    Option& operator=(const Option& o) {
      this->name = o.name;
      this->description = o.description;
      this->default_value = o.default_value;
      this->required = o.required;
      return *this;
    }
  };

  Config() : data_(Json::object()) {}

  // Configuration definition
  Config& Define(const std::string& name, const std::string& description,
                 const Json& default_value, bool required = false);

  // Template version for C++ native types
  template <typename T>
  Config& Define(const std::string& name, const std::string& description,
                 const T& default_value, bool required = false);

  // Loading methods
  // ParseArgs supports --config-file parameter to load configuration file
  // first, then command line arguments can override file settings
  bool ParseArgs(int argc, const char** argv, bool allow_exit = false);
  bool ParseArgs(const std::vector<std::string>& args,
                 bool allow_exit = false);
  bool LoadFile(const std::string& filename);
  bool ParseJson(const Json& json);
  bool Parse(const std::string& json_content);

  // Primary access method with dot notation support (e.g., "server.port")
  template <typename T>
  std::optional<T> Get(const std::string& path) const;

  // Specialized getters for common types
  std::optional<std::string> GetString(const std::string& path) const;
  std::optional<int64_t> GetInt(const std::string& path) const;
  std::optional<double> GetDouble(const std::string& path) const;
  std::optional<bool> GetBool(const std::string& path) const;
  std::optional<Json> Get(const std::string& path) const;

  // Convenience: returns value or the Define()'d default.
  // Throws if path has no value AND no defined default.
  template <typename T>
  T GetOr(const std::string& path) const;

  // Convenience: returns value or explicit fallback
  template <typename T>
  T GetOr(const std::string& path, const T& fallback) const;

  // Utility methods
  bool Has(const std::string& path) const;
  void Set(const std::string& path, const Json& value);

  // Template version for C++ native types
  template <typename T>
  void Set(const std::string& path, const T& value);

  // Validation
  bool Validate() const;
  std::string Help() const;
  std::vector<std::string> MissingRequired() const;
  std::vector<std::string> NoParsed() const;

  // Serialization
  std::string ToString() const;
  Json ToJson() const;
  bool Save(const std::string& filename) const;
  void Print() const;
  auto Options() { return options_; }

  // Deprecated wrappers (will be removed in a future version)
#include "xtils/config/config_compat.h"

 private:
  std::map<std::string, Option> options_;
  Json data_;
  bool config_loaded_ = false;

  // Helper methods
  std::optional<Json> parse_value(const std::string& value_str,
                                  const Json& default_value) const;
  void apply_defaults();
  std::vector<std::string> split_path(const std::string& path) const;
  Json merge_objects(const Json& xtils, const Json& overlay) const;
  std::vector<std::string> no_parsed_;

  // Private helpers for template deduplication
  template <typename T>
  static Json to_json_value(const T& value);
  template <typename T>
  static std::optional<T> from_json_value(const Json& json_val);
};

// Private helper: convert C++ value to Json
template <typename T>
Json Config::to_json_value(const T& value) {
  if constexpr (std::is_same_v<T, std::string>) {
    return Json(value);
  } else if constexpr (std::is_same_v<T, const char*> || std::is_array_v<T>) {
    return Json(std::string(value));
  } else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
    return Json(static_cast<int64_t>(value));
  } else if constexpr (std::is_floating_point_v<T>) {
    return Json(static_cast<double>(value));
  } else if constexpr (std::is_same_v<T, bool>) {
    return Json(value);
  } else if constexpr (detail::is_vector_v<T>) {
    Json::array_t arr;
    for (const auto& val : value) {
      arr.push_back(to_json_value(val));
    }
    return Json(arr);
  } else {
    static_assert(std::is_same_v<T, std::string> ||
                      std::is_same_v<T, const char*> || std::is_array_v<T> ||
                      std::is_integral_v<T> || std::is_floating_point_v<T> ||
                      std::is_same_v<T, bool> || detail::is_vector_v<T>,
                  "Unsupported type for Config");
    return Json{};
  }
}

// Private helper: extract C++ value from Json
template <typename T>
std::optional<T> Config::from_json_value(const Json& json_val) {
  if constexpr (std::is_same_v<T, std::string>) {
    if (!json_val.is_string()) return std::nullopt;
    return json_val.as_string();
  } else if constexpr (std::is_same_v<T, int64_t>) {
    if (!json_val.is_integer()) return std::nullopt;
    return json_val.as_integer();
  } else if constexpr (std::is_same_v<T, double>) {
    if (json_val.is_float()) return json_val.as_float();
    if (json_val.is_integer()) return static_cast<double>(json_val.as_integer());
    return std::nullopt;
  } else if constexpr (std::is_same_v<T, bool>) {
    if (!json_val.is_bool()) return std::nullopt;
    return json_val.as_bool();
  } else if constexpr (std::is_integral_v<T>) {
    if (!json_val.is_integer()) return std::nullopt;
    return static_cast<T>(json_val.as_integer());
  } else if constexpr (std::is_floating_point_v<T>) {
    if (json_val.is_float()) return static_cast<T>(json_val.as_float());
    if (json_val.is_integer()) return static_cast<T>(json_val.as_integer());
    return std::nullopt;
  } else if constexpr (detail::is_vector_v<T>) {
    if (!json_val.is_array()) return std::nullopt;
    using ElemType = typename T::value_type;
    T result;
    for (const auto& val : json_val.as_array()) {
      auto elem = from_json_value<ElemType>(val);
      if (elem) result.push_back(*elem);
    }
    return result;
  } else if constexpr (std::is_same_v<T, Json>) {
    return json_val;
  } else {
    static_assert(
        std::is_same_v<T, std::string> || std::is_same_v<T, int64_t> ||
            std::is_same_v<T, double> || std::is_same_v<T, bool> ||
            std::is_integral_v<T> || std::is_floating_point_v<T> ||
            detail::is_vector_v<T> || std::is_same_v<T, Json>,
        "Unsupported type for Config::get");
    return std::nullopt;
  }
}

// Template implementation
template <typename T>
std::optional<T> Config::Get(const std::string& path) const {
  auto json_val = Get(path);
  if (!json_val) return std::nullopt;
  return from_json_value<T>(*json_val);
}

// Template implementation for Define with native types
template <typename T>
Config& Config::Define(const std::string& name, const std::string& description,
                       const T& default_value, bool required) {
  return Define(name, description, to_json_value(default_value), required);
}

// Template implementation for Set with native types
template <typename T>
void Config::Set(const std::string& path, const T& value) {
  Set(path, to_json_value(value));
}

// GetOr with defined default
template <typename T>
T Config::GetOr(const std::string& path) const {
  auto val = Get<T>(path);
  if (val) return *val;
  // Look up the option's default value
  auto it = options_.find(path);
  if (it != options_.end()) {
    auto def = from_json_value<T>(it->second.default_value);
    if (def) return *def;
  }
  throw std::runtime_error("Config::GetOr: no value and no default for '" + path + "'");
}

// GetOr with explicit fallback
template <typename T>
T Config::GetOr(const std::string& path, const T& fallback) const {
  auto val = Get<T>(path);
  return val ? *val : fallback;
}

}  // namespace xtils
