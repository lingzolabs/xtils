#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <regex>
#include <string>
#include <unordered_set>

#include "xtils/utils/crypto.h"

using namespace xtils::crypto;

// ─── SHA-256 ────────────────────────────────────────────────────────────
// NIST FIPS 180-2 test vector.

TEST_CASE("Sha256: empty input") {
  CHECK(Sha256Hex("") ==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("Sha256: 'abc'") {
  CHECK(Sha256Hex("abc") ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

// ─── HMAC ───────────────────────────────────────────────────────────────
// Test vectors from RFC 2202 (HMAC-SHA1) and RFC 4231 (HMAC-SHA256).

TEST_CASE("HmacSha1: RFC 2202 case 1") {
  std::string key(20, '\x0b');
  CHECK(HmacSha1Hex(key, "Hi There") ==
        "b617318655057264e28bc0b6fb378c8ef146be00");
}

TEST_CASE("HmacSha256: RFC 4231 case 1") {
  std::string key(20, '\x0b');
  CHECK(HmacSha256Hex(key, "Hi There") ==
        "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
}

TEST_CASE("HmacSha256: RFC 4231 case 2 - 'Jefe'") {
  CHECK(HmacSha256Hex("Jefe", "what do ya want for nothing?") ==
        "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

// ─── SecureRandom ───────────────────────────────────────────────────────

TEST_CASE("SecureRandom: fills buffer with non-uniform data") {
  unsigned char buf[64] = {};
  CHECK(SecureRandom(buf, sizeof(buf)));
  // Probability that 64 bytes are all zero is ~0; check at least one nonzero.
  bool any_nonzero = false;
  for (auto b : buf) {
    if (b != 0) {
      any_nonzero = true;
      break;
    }
  }
  CHECK(any_nonzero);
}

TEST_CASE("SecureRandom: distinct successive calls produce distinct output") {
  unsigned char a[32], b[32];
  CHECK(SecureRandom(a, sizeof(a)));
  CHECK(SecureRandom(b, sizeof(b)));
  CHECK(std::memcmp(a, b, sizeof(a)) != 0);
}

TEST_CASE("SecureRandomHex: 32-byte hex is 64 chars") {
  auto s = SecureRandomHex(32);
  CHECK(s.size() == 64);
  for (char c : s) {
    bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    CHECK(ok);
  }
}

// ─── UUID v4 ────────────────────────────────────────────────────────────

TEST_CASE("Uuid::V4: format 8-4-4-4-12 with version=4 and variant=8/9/a/b") {
  static const std::regex kPattern(
      "^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$");
  for (int i = 0; i < 200; ++i) {
    auto u = Uuid::V4();
    REQUIRE(u.size() == 36);
    CHECK(std::regex_match(u, kPattern));
  }
}

TEST_CASE("Uuid::V4: distinct calls produce distinct values") {
  std::unordered_set<std::string> seen;
  for (int i = 0; i < 1000; ++i) seen.insert(Uuid::V4());
  CHECK(seen.size() == 1000);
}
