#pragma once

#include <cstddef>

namespace xtils {

// TrySend accepts a whole chunk or none. Accepted means copied/queued, not
// delivered to the peer. WouldBlock must be retried after a capacity callback.
// Chunks larger than kMaxSendChunkSize are an input error, not WouldBlock.
enum class SendStatus { kAccepted, kWouldBlock, kClosed, kError };
inline constexpr size_t kMaxSendChunkSize = 16 * 1024;

}  // namespace xtils
