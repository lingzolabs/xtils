#include "xtils/config/config.h"

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

#include "xtils/logging/logger.h"

namespace xtils {

// Helper function for C++17 compatibility (starts_with is C++20)
static bool starts_with(const std::string& str, const std::string& prefix) {
  return str.length() >= prefix.length() &&
         str.substr(0, prefix.length()) == prefix;
}

// A flag starts with "--" or "-" followed by a non-digit
static bool is_flag(const std::string& arg) {
  if (arg.size() >= 2 && arg[0] == '-' && arg[1] == '-') return true;
  if (arg.size() >= 2 && arg[0] == '-' && !std::isdigit(arg[1])) return true;
  return false;
}

Config& Config::Define(const std::string& name, const std::string& description,
                       const Json& default_value, bool required) {
  options_[name] = Option(name, description, default_value, required);

  // Required options intentionally do not get their placeholder default value.
  // They must be provided by file, CLI, or Set().
  if (!required && !Has(name)) {
    Set(name, default_value);
  }

  return *this;
}

bool Config::ParseArgs(int argc, const char** argv, bool allow_exit) {
  return ParseArgs(std::vector<std::string>(argv, argv + argc), allow_exit);
}

bool Config::ParseArgs(const std::vector<std::string>& args, bool allow_exit) {
  if (args.empty()) return true;

  // First, apply default values
  apply_defaults();
  no_parsed_.clear();
  no_parsed_.push_back(args[0]);

  // First pass: look for --config-file parameter
  std::string config_file;
  for (size_t i = 1; i < args.size(); ++i) {  // suport mutil config file
    std::string arg = args[i];

    if (arg == "--config-file" && i + 1 < args.size()) {
      config_file = args[++i];  // skip the value
    } else if (starts_with(arg, "--config-file=")) {
      config_file = arg.substr(14);  // length of "--config-file="
    }
    // Load config file if specified
    if (!config_file.empty()) {
      if (!config_loaded_) {
        if (!LoadFile(config_file)) {
          LogE("Failed to load config file: %s", config_file.c_str());
          return false;
        }
        config_loaded_ = true;
      }
    }
    config_file.clear();
  }

  // Second pass: process all command line arguments
  for (size_t i = 1; i < args.size(); ++i) {
    std::string arg = args[i];

    // Skip help flags
    if ((arg == "-h" || arg == "--help") && allow_exit) {
      std::cout << Help() << std::endl;
      std::cout.flush();
      _Exit(0);
    }
    if (arg == "--dump" && allow_exit) {
      Print();
      std::cout.flush();
      _Exit(0);
    }

    // Skip config-file argument as it's already processed
    if (arg == "--config-file") {
      ++i;  // Skip the next argument (the file path)
      continue;
    }
    if (starts_with(arg, "--config-file=")) {
      continue;
    }

    std::string key;
    std::string value_str;
    bool has_value = false;

    if (arg.length() >= 2 && arg.substr(0, 2) == "--") {
      // Long form: --key=value or --key value
      std::string long_arg = arg.substr(2);
      size_t eq_pos = long_arg.find('=');

      if (eq_pos != std::string::npos) {
        key = long_arg.substr(0, eq_pos);
        value_str = long_arg.substr(eq_pos + 1);
        has_value = true;
      } else {
        key = long_arg;
        // Check if next argument is the value
        if (i + 1 < args.size() && !is_flag(args[i + 1])) {
          value_str = args[++i];
          has_value = true;
        }
      }
    }

    if (key.empty()) {
      no_parsed_.push_back(arg);
      continue;
    }

    auto option_it = options_.find(key);
    if (option_it == options_.end()) {
      no_parsed_.push_back("--" + key);
      if (has_value) no_parsed_.push_back(value_str);
      continue;
    }

    // For boolean options, if no value is provided or empty value, treat as
    // true
    if (has_value && value_str.empty() &&
        option_it->second.default_value.is_bool()) {
      Set(key, Json(true));
    } else if (!has_value && option_it->second.default_value.is_bool()) {
      Set(key, Json(true));
    } else if (!has_value) {
      LogE("Option %s requires a value", key.c_str());
      return false;
    } else {
      auto parsed_value =
          parse_value(value_str, option_it->second.default_value);
      if (!parsed_value) {
        LogE("Invalid value for option %s : %s", key.c_str(),
             value_str.c_str());
        return false;
      }
      Set(key, parsed_value.value());
    }
  }

  return Validate();
}

bool Config::LoadFile(const std::string& filename) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    LogE("Failed to open config file: %s", filename.c_str());
    return false;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  file.close();

  return Parse(buffer.str());
}

bool Config::Parse(const std::string& json_content) {
  auto json = Json::parse(json_content);
  if (!json) {
    LogE("Failed to parse JSON configuration");
    return false;
  }
  return ParseJson(*json);
}
bool Config::ParseJson(const Json& json) {
  // Apply defaults first
  apply_defaults();

  // Merge JSON values
  if (json.is_object()) {
    data_ = merge_objects(data_, json);
  }

  return Validate();
}

std::optional<std::string> Config::GetString(const std::string& path) const {
  return Get<std::string>(path);
}

std::optional<int64_t> Config::GetInt(const std::string& path) const {
  return Get<int64_t>(path);
}

std::optional<double> Config::GetDouble(const std::string& path) const {
  return Get<double>(path);
}

std::optional<bool> Config::GetBool(const std::string& path) const {
  return Get<bool>(path);
}

std::vector<std::string> Config::NoParsed() const { return no_parsed_; }

bool Config::Has(const std::string& path) const {
  return Get(path).has_value();
}

void Config::Set(const std::string& path, const Json& value) {
  auto parts = split_path(path);
  if (parts.empty()) return;

  // Ensure data_ is an object
  if (!data_.is_object()) {
    data_ = Json::object();
  }

  Json* current = &data_;

  // Navigate to the parent of the target key
  for (size_t i = 0; i < parts.size() - 1; ++i) {
    if (!current->is_object()) {
      *current = Json::object();
    }

    // Get or create the nested object
    if (!current->has_key(parts[i])) {
      (*current)[parts[i]] = Json::object();
    }
    current = &(*current)[parts[i]];
  }

  // Ensure the current level is an object
  if (!current->is_object()) {
    *current = Json::object();
  }

  // Set the final value
  (*current)[parts.back()] = value;
}

bool Config::Validate() const {
  auto missing = MissingRequired();
  if (!missing.empty()) {
    std::stringstream ss;
    ss << "Missing required options: ";
    for (size_t i = 0; i < missing.size(); ++i) {
      ss << missing[i];
      if (i < missing.size() - 1) ss << ", ";
    }
    LogE("%s", ss.str().c_str());
    return false;
  }
  return true;
}

std::string Config::Help() const {
  std::ostringstream oss;
  oss << "Configuration Options:\n";
  oss << "  --config-file <file>\n";
  oss << "      Load configuration from JSON file. Command line arguments\n";
  oss << "      can override values from the config file.\n";
  oss << "\n";

  for (const auto& [name, option] : options_) {
    oss << "  --" << option.name;

    if (option.required) {
      oss << " (required)";
    } else {
      oss << " (default: ";
      if (option.default_value.is_string()) {
        oss << option.default_value.as_string();
      } else {
        oss << option.default_value.dump();
      }
      oss << ")";
    }

    oss << "\n      " << option.description << "\n";
  }

  return oss.str();
}

std::vector<std::string> Config::MissingRequired() const {
  std::vector<std::string> missing;

  for (const auto& [name, option] : options_) {
    if (option.required && !Has(name)) {
      missing.push_back(name);
    }
  }

  return missing;
}

void Config::Print() const { std::cout << data_.dump(2) << std::endl; }

Json Config::ToJson() const { return data_; }

bool Config::Save(const std::string& filename) const {
  std::ofstream file(filename);
  if (!file.is_open()) {
    LogE("Failed to create config file: %s", filename.c_str());
    return false;
  }

  file << ToString();
  file.close();
  return true;
}

std::string Config::ToString() const { return data_.dump(2); }

std::optional<Json> Config::parse_value(const std::string& value_str,
                                        const Json& default_value) const {
  if (default_value.is_string()) {
    return Json(value_str);
  } else if (default_value.is_integer()) {
    try {
      return Json(std::stoll(value_str));
    } catch (...) {
      return std::nullopt;
    }
  } else if (default_value.is_float()) {
    try {
      return Json(std::stod(value_str));
    } catch (...) {
      return std::nullopt;
    }
  } else if (default_value.is_bool()) {
    std::string lower = value_str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "true" || lower == "1" || lower == "yes" || lower == "on") {
      return Json(true);
    } else if (lower == "false" || lower == "0" || lower == "no" ||
               lower == "off") {
      return Json(false);
    } else {
      return std::nullopt;
    }
  }

  // Fallback: try parsing as JSON (for arrays/objects)
  if (default_value.is_array() || default_value.is_object()) {
    auto parsed = Json::parse(value_str);
    if (parsed) return *parsed;
  }
  return std::nullopt;
}

void Config::apply_defaults() {
  for (const auto& [name, option] : options_) {
    if (!option.required && !Has(name)) {
      Set(name, option.default_value);
    }
  }
}

std::vector<std::string> Config::split_path(const std::string& path) const {
  std::vector<std::string> parts;
  std::stringstream ss(path);
  std::string part;

  while (std::getline(ss, part, '.')) {
    if (!part.empty()) {
      parts.push_back(part);
    }
  }

  return parts;
}

std::optional<Json> Config::Get(const std::string& path) const {
  auto parts = split_path(path);
  if (parts.empty()) return std::nullopt;

  const Json* current = &data_;

  for (const auto& part : parts) {
    if (!current->is_object()) {
      return std::nullopt;
    }

    if (!current->has_key(part)) {
      return std::nullopt;
    }

    current = &(*current)[part];
  }

  return *current;
}

Json Config::merge_objects(const Json& xtils, const Json& overlay) const {
  if (!xtils.is_object() || !overlay.is_object()) {
    return overlay;
  }

  Json::object_t result = xtils.as_object();
  const auto& overlay_obj = overlay.as_object();

  for (const auto& [key, value] : overlay_obj) {
    if (result.find(key) != result.end() && result.at(key).is_object() &&
        value.is_object()) {
      result[key] = merge_objects(result.at(key), value);
    } else {
      result[key] = value;
    }
  }

  return Json(result);
}

}  // namespace xtils
