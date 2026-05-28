#pragma once
// Deprecated API compatibility layer — included inside Config class body.
// These wrappers will be removed in a future major version.

 public:
  [[deprecated("Use Define() instead")]]
  Config& define(const std::string& name, const std::string& description,
                 const Json& default_value, bool required = false) {
    return Define(name, description, default_value, required);
  }
  template <typename T>
  [[deprecated("Use Define() instead")]]
  Config& define(const std::string& name, const std::string& description,
                 const T& default_value, bool required = false) {
    return Define(name, description, default_value, required);
  }
  [[deprecated("Use ParseArgs() instead")]]
  bool parse_args(int argc, const char** argv, bool allow_exit = false) {
    return ParseArgs(argc, argv, allow_exit);
  }
  [[deprecated("Use ParseArgs() instead")]]
  bool parse_args(const std::vector<std::string>& args,
                  bool allow_exit = false) {
    return ParseArgs(args, allow_exit);
  }
  [[deprecated("Use LoadFile() instead")]]
  bool load_file(const std::string& filename) { return LoadFile(filename); }
  [[deprecated("Use ParseJson() instead")]]
  bool parse_json(const Json& json) { return ParseJson(json); }
  [[deprecated("Use Parse() instead")]]
  bool parse(const std::string& json_content) { return Parse(json_content); }
  template <typename T>
  [[deprecated("Use Get() instead")]]
  std::optional<T> get(const std::string& path) const { return Get<T>(path); }
  [[deprecated("Use GetString() instead")]]
  std::optional<std::string> get_string(const std::string& path) const {
    return GetString(path);
  }
  [[deprecated("Use GetInt() instead")]]
  std::optional<int64_t> get_int(const std::string& path) const {
    return GetInt(path);
  }
  [[deprecated("Use GetDouble() instead")]]
  std::optional<double> get_double(const std::string& path) const {
    return GetDouble(path);
  }
  [[deprecated("Use GetBool() instead")]]
  std::optional<bool> get_bool(const std::string& path) const {
    return GetBool(path);
  }
  [[deprecated("Use Get() instead")]]
  std::optional<Json> get(const std::string& path) const { return Get(path); }
  [[deprecated("Use Has() instead")]]
  bool has(const std::string& path) const { return Has(path); }
  [[deprecated("Use Set() instead")]]
  void set(const std::string& path, const Json& value) { Set(path, value); }
  template <typename T>
  [[deprecated("Use Set() instead")]]
  void set(const std::string& path, const T& value) { Set(path, value); }
  [[deprecated("Use Validate() instead")]]
  bool validate() const { return Validate(); }
  [[deprecated("Use Help() instead")]]
  std::string help() const { return Help(); }
  [[deprecated("Use MissingRequired() instead")]]
  std::vector<std::string> missing_required() const { return MissingRequired(); }
  [[deprecated("Use NoParsed() instead")]]
  std::vector<std::string> no_parsed() const { return NoParsed(); }
  [[deprecated("Use ToString() instead")]]
  std::string to_string() const { return ToString(); }
  [[deprecated("Use ToJson() instead")]]
  Json to_json() const { return ToJson(); }
  [[deprecated("Use Save() instead")]]
  bool save(const std::string& filename) const { return Save(filename); }
  [[deprecated("Use Print() instead")]]
  void print() const { Print(); }
  [[deprecated("Use Options() instead")]]
  auto options() { return Options(); }
  template <typename T>
  [[deprecated("Use GetOr() instead")]]
  T get_or(const std::string& path) const { return GetOr<T>(path); }
  template <typename T>
  [[deprecated("Use GetOr() instead")]]
  T get_or(const std::string& path, const T& fallback) const { return GetOr<T>(path, fallback); }
