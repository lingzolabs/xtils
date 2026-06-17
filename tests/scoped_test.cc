#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <atomic>
#include <string>

#include "xtils/utils/scoped.h"

using namespace xtils;

TEST_CASE("ScopedFile: closes fd on destruction") {
  int fd = ::open("/dev/null", O_RDONLY);
  REQUIRE(fd >= 0);
  {
    ScopedFile sf(fd);
    CHECK(sf);
    CHECK(sf.get() == fd);
  }
  // After scope, fd should be closed. Best-effort test: fcntl on a closed
  // fd returns -1 with errno EBADF.
  CHECK(::fcntl(fd, F_GETFD) == -1);
}

TEST_CASE("ScopedFile: release leaves fd open") {
  int fd = ::open("/dev/null", O_RDONLY);
  REQUIRE(fd >= 0);
  {
    ScopedFile sf(fd);
    int released = sf.release();
    CHECK(released == fd);
    CHECK_FALSE(sf);  // sf no longer owns
  }
  // fd must still be open.
  CHECK(::fcntl(fd, F_GETFD) != -1);
  ::close(fd);
}

TEST_CASE("ScopedFile: default-constructed is invalid") {
  ScopedFile sf;
  CHECK_FALSE(sf);
  CHECK(sf.get() == ScopedFile::kInvalid);
}

TEST_CASE("Scoped: runs deferred function on destruction") {
  std::atomic<int> ran{0};
  {
    Scoped guard([&] { ran++; });
    CHECK(ran == 0);
  }
  CHECK(ran == 1);
}

TEST_CASE("Scoped: empty function is a no-op") {
  Scoped guard(std::function<void()>{});
  // No assertion: just must not crash.
}

TEST_CASE("ScopedFstream: closes FILE* on destruction") {
  FILE* f = std::fopen("/dev/null", "r");
  REQUIRE(f != nullptr);
  ScopedFstream sf(f);
  CHECK(sf);
  // Destructor calls fclose; best-effort smoke test only.
}
