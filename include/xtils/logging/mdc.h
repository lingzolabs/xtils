/*
 * Description: Mapped Diagnostic Context (MDC) — thread-local key/value
 * context that automatically attaches to LogBuilder output.
 */
#pragma once

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace xtils {
namespace logger {

// Thread-local key/value context. Values set on this thread propagate
// into every LogBuilder rendered on the same thread.
class Mdc {
 public:
  // Set or update a context entry.
  static void Put(std::string key, std::string value);

  // Remove an entry. No-op if absent.
  static void Erase(const std::string& key);

  // Get an entry; empty string if absent.
  static const std::string& Get(const std::string& key);

  // Wipe the entire context for this thread.
  static void Clear();

  // Snapshot the current MDC as ordered key/value pairs.
  static std::vector<std::pair<std::string, std::string>> Snapshot();

  // RAII helper. Sets `key` on construction and restores the prior value
  // (or removes it) on destruction.
  class Scope {
   public:
    Scope(std::string key, std::string value);
    ~Scope();

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

   private:
    std::string key_;
    bool had_prior_;
    std::string prior_;
  };
};

}  // namespace logger
}  // namespace xtils
