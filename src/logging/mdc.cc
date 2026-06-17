#include "xtils/logging/mdc.h"

namespace xtils {
namespace logger {

namespace {

using MdcMap = std::unordered_map<std::string, std::string>;

MdcMap& Tls() {
  thread_local MdcMap m;
  return m;
}

const std::string& EmptyString() {
  static const std::string s;
  return s;
}

}  // namespace

void Mdc::Put(std::string key, std::string value) {
  Tls()[std::move(key)] = std::move(value);
}

void Mdc::Erase(const std::string& key) { Tls().erase(key); }

const std::string& Mdc::Get(const std::string& key) {
  auto& m = Tls();
  auto it = m.find(key);
  return it == m.end() ? EmptyString() : it->second;
}

void Mdc::Clear() { Tls().clear(); }

std::vector<std::pair<std::string, std::string>> Mdc::Snapshot() {
  std::vector<std::pair<std::string, std::string>> out;
  auto& m = Tls();
  out.reserve(m.size());
  for (const auto& kv : m) out.emplace_back(kv.first, kv.second);
  return out;
}

Mdc::Scope::Scope(std::string key, std::string value)
    : key_(std::move(key)), had_prior_(false) {
  auto& m = Tls();
  auto it = m.find(key_);
  if (it != m.end()) {
    had_prior_ = true;
    prior_ = it->second;
  }
  m[key_] = std::move(value);
}

Mdc::Scope::~Scope() {
  auto& m = Tls();
  if (had_prior_) {
    m[key_] = std::move(prior_);
  } else {
    m.erase(key_);
  }
}

}  // namespace logger
}  // namespace xtils
