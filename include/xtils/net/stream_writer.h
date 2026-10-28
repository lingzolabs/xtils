#pragma once

#include <deque>
#include <functional>
#include <string>

#include "xtils/system/send_status.h"
#include "xtils/utils/scoped.h"

namespace xtils {

// Runner-confined producer for ordered memory/file segments. The sink must not
// destroy/reset this writer from inside TrySend. File data is buffered a chunk
// at a time, and a blocked chunk is preserved unchanged until it is accepted.
class StreamWriter {
 public:
  enum class PumpResult { kDone, kWouldBlock, kYield, kError };
  using Sink = std::function<SendStatus(const void*, size_t)>;

  void Append(std::string bytes);
  void Prepend(std::string bytes);
  bool AppendFile(const std::string& path);
  PumpResult Pump(const Sink& sink, size_t budget = 256 * 1024);
  void Reset();

  bool empty() const { return segments_.empty(); }
  size_t total_bytes() const { return total_bytes_; }
  size_t accepted_bytes() const { return accepted_bytes_; }
  const std::string& error() const { return error_; }

 private:
  struct Segment {
    std::string data;
    ScopedFile file;
    off_t file_offset = 0;
    size_t offset = 0;
    size_t remaining = 0;
    bool is_file = false;
  };
  bool AddSize(size_t size);
  std::deque<Segment> segments_;
  size_t total_bytes_ = 0;
  size_t accepted_bytes_ = 0;
  std::string error_;
};

}  // namespace xtils
