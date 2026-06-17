#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <cstdint>

#include "xtils/utils/endianness.h"

using namespace xtils;

TEST_CASE("Endianness: kSystemIsLittleEndian matches IsSystemLittleEndian") {
  CHECK(kSystemIsLittleEndian == IsSystemLittleEndian());
}

TEST_CASE("SwapBytes: 1-byte is identity") {
  uint8_t a = 0x7F;
  CHECK(SwapBytes(a) == 0x7F);
}

TEST_CASE("SwapBytes: 2-byte") {
  uint16_t a = 0x1234;
  CHECK(SwapBytes(a) == 0x3412);
}

TEST_CASE("SwapBytes: 4-byte") {
  uint32_t a = 0x12345678u;
  CHECK(SwapBytes(a) == 0x78563412u);
}

TEST_CASE("SwapBytes: 8-byte") {
  uint64_t a = 0x0123456789ABCDEFull;
  CHECK(SwapBytes(a) == 0xEFCDAB8967452301ull);
}

TEST_CASE("SwapBytes: signed types") {
  int32_t a = static_cast<int32_t>(0x01020304);
  int32_t s = SwapBytes(a);
  CHECK(static_cast<uint32_t>(s) == 0x04030201u);
}

TEST_CASE("SwapBytes: round-trip identity") {
  uint64_t a = 0xDEADBEEFCAFEBABEull;
  CHECK(SwapBytes(SwapBytes(a)) == a);
}
