/*
 * Description: Lightweight crypto utilities — SHA256, HMAC, secure RNG, UUID.
 *
 * Backend is the same TLS engine selected via TLS_BACKEND (OpenSSL or
 * mbedTLS), so no new dependency is added.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace xtils {
namespace crypto {

// ─── Hash digests ───────────────────────────────────────────────────────

// SHA-256 of `data`. Returns the 32-byte raw binary digest.
std::string Sha256(std::string_view data);

// Hex (lowercase) representation of Sha256(data) — 64 chars.
std::string Sha256Hex(std::string_view data);

// ─── HMAC ───────────────────────────────────────────────────────────────

// HMAC-SHA1: 20-byte raw binary digest.
std::string HmacSha1(std::string_view key, std::string_view msg);
std::string HmacSha1Hex(std::string_view key, std::string_view msg);

// HMAC-SHA256: 32-byte raw binary digest.
std::string HmacSha256(std::string_view key, std::string_view msg);
std::string HmacSha256Hex(std::string_view key, std::string_view msg);

// ─── Secure random ──────────────────────────────────────────────────────

// Fill `buf` with `len` cryptographically secure random bytes.
// Returns true on success. Aborts on backend failure (RNG starvation is
// not a recoverable condition for security-sensitive uses).
bool SecureRandom(void* buf, size_t len);

// Returns 2n lowercase hex chars of secure random.
std::string SecureRandomHex(size_t n_bytes);

// ─── UUID ───────────────────────────────────────────────────────────────

class Uuid {
 public:
  // Generate a random UUID v4 (RFC 4122). Returns 36-char canonical
  // 8-4-4-4-12 form, e.g. "f81d4fae-7dec-11d0-a765-00a0c91e6bf6".
  static std::string V4();
};

}  // namespace crypto
}  // namespace xtils
