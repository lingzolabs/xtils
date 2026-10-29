#include "xtils/net/stream_writer.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <limits>

namespace xtils {

bool StreamWriter::AddSize(size_t size) {
  if (size > std::numeric_limits<size_t>::max() - total_bytes_) {
    error_ = "Output length overflow";
    return false;
  }
  total_bytes_ += size;
  return true;
}

void StreamWriter::Append(std::string bytes) {
  if (bytes.empty() || !AddSize(bytes.size())) return;
  segments_.emplace_back();
  segments_.back().data = std::move(bytes);
  buffered_memory_bytes_ += segments_.back().data.size();
}

void StreamWriter::Prepend(std::string bytes) {
  if (bytes.empty() || !AddSize(bytes.size())) return;
  segments_.emplace_front();
  segments_.front().data = std::move(bytes);
  buffered_memory_bytes_ += segments_.front().data.size();
}

bool StreamWriter::AppendFile(const std::string& path) {
  Segment part;
  part.is_file = true;
  part.file.reset(open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK));
  struct stat info {};
  if (!part.file || fstat(part.file.get(), &info) != 0 ||
      !S_ISREG(info.st_mode) || info.st_size < 0 ||
      static_cast<uint64_t>(info.st_size) >
          std::numeric_limits<size_t>::max()) {
    error_ = "Failed to open regular file: " + path;
    return false;
  }
  part.remaining = static_cast<size_t>(info.st_size);
  if (!AddSize(part.remaining)) return false;
  segments_.push_back(std::move(part));
  return true;
}

StreamWriter::PumpResult StreamWriter::Pump(const Sink& sink, size_t budget) {
  if (!error_.empty()) return PumpResult::kError;
  while (!segments_.empty() && budget > 0) {
    auto& part = segments_.front();
    if (part.is_file && part.data.empty() && part.remaining > 0) {
      part.data.resize(std::min(part.remaining, kMaxSendChunkSize));
      ssize_t count;
      do {
        count = pread(part.file.get(), part.data.data(), part.data.size(),
                      part.file_offset);
      } while (count < 0 && errno == EINTR);
      if (count <= 0) {
        error_ = "File was truncated or could not be read";
        return PumpResult::kError;
      }
      part.data.resize(static_cast<size_t>(count));
      part.file_offset += count;
    }
    if (part.offset == part.data.size()) {
      segments_.pop_front();
      continue;
    }
    size_t count =
        std::min({kMaxSendChunkSize, budget, part.data.size() - part.offset});
    auto status = sink(part.data.data() + part.offset, count);
    if (status == SendStatus::kWouldBlock) return PumpResult::kWouldBlock;
    if (status != SendStatus::kAccepted) {
      error_ = status == SendStatus::kClosed ? "Transport closed during send"
                                             : "Failed to send data";
      return PumpResult::kError;
    }
    part.offset += count;
    accepted_bytes_ += count;
    if (!part.is_file) buffered_memory_bytes_ -= count;
    budget -= count;
    if (part.is_file) {
      part.remaining -= count;
      if (part.offset == part.data.size()) {
        part.data.clear();
        part.offset = 0;
        if (part.remaining == 0) segments_.pop_front();
      }
    } else if (part.offset == part.data.size()) {
      segments_.pop_front();
    }
  }
  return segments_.empty() ? PumpResult::kDone : PumpResult::kYield;
}

void StreamWriter::Reset() {
  segments_.clear();
  total_bytes_ = 0;
  accepted_bytes_ = 0;
  buffered_memory_bytes_ = 0;
  error_.clear();
}

}  // namespace xtils
