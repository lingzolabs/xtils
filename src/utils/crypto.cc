#include "xtils/utils/crypto.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(USE_OPENSSL)
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#elif defined(USE_MBEDTLS)
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/md.h>
#include <mbedtls/sha1.h>
#include <mbedtls/sha256.h>
#include <mutex>
#else
#error "No TLS backend defined: define USE_OPENSSL or USE_MBEDTLS"
#endif

namespace xtils {
namespace crypto {

namespace {

const char kHexLower[] = "0123456789abcdef";

std::string ToHex(const std::string& bin) {
  std::string out;
  out.resize(bin.size() * 2);
  for (size_t i = 0; i < bin.size(); ++i) {
    auto b = static_cast<unsigned char>(bin[i]);
    out[2 * i] = kHexLower[b >> 4];
    out[2 * i + 1] = kHexLower[b & 0x0F];
  }
  return out;
}

}  // namespace

// ─── SHA-256 ────────────────────────────────────────────────────────────

#if defined(USE_OPENSSL)

std::string Sha256(std::string_view data) {
  unsigned char digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(),
         digest);
  return std::string(reinterpret_cast<const char*>(digest),
                     SHA256_DIGEST_LENGTH);
}

std::string HmacSha1(std::string_view key, std::string_view msg) {
  unsigned char out[20];
  unsigned int len = 0;
  HMAC(EVP_sha1(), key.data(), static_cast<int>(key.size()),
       reinterpret_cast<const unsigned char*>(msg.data()), msg.size(), out,
       &len);
  return std::string(reinterpret_cast<const char*>(out), len);
}

std::string HmacSha256(std::string_view key, std::string_view msg) {
  unsigned char out[32];
  unsigned int len = 0;
  HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
       reinterpret_cast<const unsigned char*>(msg.data()), msg.size(), out,
       &len);
  return std::string(reinterpret_cast<const char*>(out), len);
}

bool SecureRandom(void* buf, size_t len) {
  return RAND_bytes(static_cast<unsigned char*>(buf),
                    static_cast<int>(len)) == 1;
}

#elif defined(USE_MBEDTLS)

std::string Sha256(std::string_view data) {
  unsigned char digest[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, /*is224=*/0);
  mbedtls_sha256_update(&ctx,
                        reinterpret_cast<const unsigned char*>(data.data()),
                        data.size());
  mbedtls_sha256_finish(&ctx, digest);
  mbedtls_sha256_free(&ctx);
  return std::string(reinterpret_cast<const char*>(digest), 32);
}

std::string HmacSha1(std::string_view key, std::string_view msg) {
  unsigned char out[20];
  const mbedtls_md_info_t* info =
      mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
  mbedtls_md_hmac(info, reinterpret_cast<const unsigned char*>(key.data()),
                  key.size(),
                  reinterpret_cast<const unsigned char*>(msg.data()),
                  msg.size(), out);
  return std::string(reinterpret_cast<const char*>(out), 20);
}

std::string HmacSha256(std::string_view key, std::string_view msg) {
  unsigned char out[32];
  const mbedtls_md_info_t* info =
      mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_hmac(info, reinterpret_cast<const unsigned char*>(key.data()),
                  key.size(),
                  reinterpret_cast<const unsigned char*>(msg.data()),
                  msg.size(), out);
  return std::string(reinterpret_cast<const char*>(out), 32);
}

namespace {
struct DrbgState {
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context ctr_drbg;
  bool ready = false;
};

DrbgState& GlobalDrbg() {
  static DrbgState s;
  static std::mutex m;
  std::lock_guard<std::mutex> lock(m);
  if (!s.ready) {
    mbedtls_entropy_init(&s.entropy);
    mbedtls_ctr_drbg_init(&s.ctr_drbg);
    static const char* pers = "xtils-crypto";
    int rc = mbedtls_ctr_drbg_seed(&s.ctr_drbg, mbedtls_entropy_func,
                                   &s.entropy,
                                   reinterpret_cast<const unsigned char*>(pers),
                                   std::strlen(pers));
    s.ready = (rc == 0);
  }
  return s;
}
}  // namespace

bool SecureRandom(void* buf, size_t len) {
  auto& s = GlobalDrbg();
  if (!s.ready) return false;
  return mbedtls_ctr_drbg_random(&s.ctr_drbg,
                                 static_cast<unsigned char*>(buf), len) == 0;
}

#endif

std::string Sha256Hex(std::string_view data) { return ToHex(Sha256(data)); }
std::string HmacSha1Hex(std::string_view key, std::string_view msg) {
  return ToHex(HmacSha1(key, msg));
}
std::string HmacSha256Hex(std::string_view key, std::string_view msg) {
  return ToHex(HmacSha256(key, msg));
}

std::string SecureRandomHex(size_t n_bytes) {
  std::string raw;
  raw.resize(n_bytes);
  if (n_bytes == 0) return std::string();
  if (!SecureRandom(raw.data(), n_bytes)) {
    std::fputs("xtils::crypto::SecureRandom backend failed\n", stderr);
    std::abort();
  }
  return ToHex(raw);
}

// ─── UUID v4 ────────────────────────────────────────────────────────────

std::string Uuid::V4() {
  unsigned char b[16];
  if (!SecureRandom(b, sizeof(b))) {
    std::fputs("xtils::crypto::Uuid::V4 RNG failed\n", stderr);
    std::abort();
  }
  // RFC 4122: set version=4 (top 4 bits of byte 6), variant=10xx (top 2
  // bits of byte 8).
  b[6] = (b[6] & 0x0F) | 0x40;
  b[8] = (b[8] & 0x3F) | 0x80;

  char out[37];  // 8+1+4+1+4+1+4+1+12 + '\0'
  std::snprintf(out, sizeof(out),
                "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
                "%02x%02x%02x%02x%02x%02x",
                b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9],
                b[10], b[11], b[12], b[13], b[14], b[15]);
  return std::string(out, 36);
}

}  // namespace crypto
}  // namespace xtils
