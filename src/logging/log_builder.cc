#include "xtils/logging/log_builder.h"

#include <ctime>
#include <sstream>

namespace xtils {
namespace logger {

void LogBuilder::Send(const std::string& body) {
  if (!enabled_ || sent_) return;
  sent_ = true;

  // Render message: body + " | k=v ..." (fields then MDC)
  std::ostringstream os;
  os << body;
  bool need_sep = !body.empty();

  auto emit = [&](const std::string& k, const std::string& v) {
    if (need_sep) os << ' ';
    os << k << '=';
    // Quote values containing spaces or special chars to preserve key/value
    // boundaries when humans grep through plain text logs.
    bool needs_quote = false;
    for (char c : v) {
      if (c == ' ' || c == '\t' || c == '"' || c == '|') {
        needs_quote = true;
        break;
      }
    }
    if (needs_quote) {
      os << '"';
      for (char c : v) {
        if (c == '"' || c == '\\') os << '\\';
        os << c;
      }
      os << '"';
    } else {
      os << v;
    }
    need_sep = true;
  };

  for (const auto& kv : fields_) emit(kv.first, kv.second);
  for (const auto& kv : Mdc::Snapshot()) emit(kv.first, kv.second);

  LogEntry entry;
  clock_gettime(CLOCK_REALTIME, &entry.timestamp);
  entry.level = level_;
  entry.tag = tag_;
  entry.file_name = loc_.file_name;
  entry.function_name = loc_.function_name;
  entry.line = loc_.line;
  entry.message = os.str();

  if (level_ >= warn) {
    log_->WriteLogSync(std::move(entry));
  } else {
    log_->WriteLogAsync(std::move(entry));
  }
}

}  // namespace logger
}  // namespace xtils
