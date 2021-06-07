//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "mmap_gc.hpp"

#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lrand.hpp"
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define DOING_ASAN_TEST 1
#endif
#endif

class Setup_mmap_gc_test : public ::testing::Test {
protected:
  void SetUp() override {}

  void TearDown() override {}
};

int clean_called;
struct track_entry {
  std::string name;
  void *      base;
  int         fd;
  bool        has_fd;
};

std::vector<track_entry> open_tracks;

int global_fd = -1;

static bool trigger_clean(void *base, bool force_recycle) {
  // printf("1.trigger_clean %p\n",base);
  bool found       = false;
  bool ignore_call = ((rand() & 0x3) == 0) & !force_recycle;
  for (auto &e : open_tracks) {
    if (e.base != base)
      continue;

    // printf("2.trigger_clean %p %d %s\n",e.base,e.fd, ignore_call?"abort":"cont");

    EXPECT_FALSE(found);  // only one
    found = true;
    EXPECT_TRUE(e.fd >= 0);  // Can not recycle without mmap
    assert(e.fd != -1);      // only mmap with fd can be recycled
    if (!ignore_call) {
      e.base = 0;
      e.fd   = -1;
    }

    if (global_fd >= 0) {
      EXPECT_NE(global_fd, e.fd);
    }
  }
  EXPECT_TRUE(found);

  clean_called++;
  if (ignore_call)
    return true;  // abort GC
  else
    return false;
}

static bool trigger_clean2(void *base, bool force_recycle) {
  for (auto e : open_tracks) {
    if (e.base != base)
      continue;
    assert(e.fd != -1);  // only mmap with fd can be recycled
    e.base = 0;
  }
  clean_called++;
  return false;
}

#ifdef __linux__
TEST_F(Setup_mmap_gc_test, mmap_limit) {
  struct rlimit rval;

  struct rusage rusage;
  getrusage(RUSAGE_SELF, &rusage);
#if defined(__APPLE__) && defined(__MACH__)
  size_t sz = rusage.ru_maxrss;
#else
  size_t sz = rusage.ru_maxrss * 1024L;
#endif

  rval.rlim_cur = sz + 16 * 1024 * 4096;
  rval.rlim_max = sz + 16 * 1024 * 4096;

#ifdef DOING_ASAN_TEST
  std::cout << "ASAN needs mmaps, and it fails when it should not in this case\n";
#else
  std::cout << "Current memory usage " << sz / 1024 << "KB limit to " << rval.rlim_max / 1024 << "KB\n";
  int err = setrlimit(RLIMIT_AS, &rval);
  ASSERT_EQ(err, 0);
#endif

  Lrand<uint8_t> rng;

  clean_called = 0;
  open_tracks.clear();
  int max_allowd_without_fd = 8;  // Not possible to garbage collect

  for (int i = 0; i < 256; ++i) {
    track_entry entry;
    std::string name("mmap_gc_test_file");

    entry.name = name + std::to_string(i) + ".data";
    if (rng.max(0xF) >= 0xE || max_allowd_without_fd == 0) {
      entry.fd     = mmap_lib::mmap_gc::open(entry.name);
      entry.has_fd = true;
    } else {
      max_allowd_without_fd--;
      entry.fd     = -1;
      entry.has_fd = false;
    }

    void * base;
    size_t size;

    global_fd  = entry.fd;
    entry.base = 0;
    // printf("1.alloc %p %d [%s]\n",entry.base, entry.fd, entry.name.c_str());

    std::tie(base, size) = mmap_lib::mmap_gc::mmap(entry.name, entry.fd, 4096 * 1024, trigger_clean);
    int *value           = (int *)base;
    value[0]             = i;
    for (int j = 1; j < 256 * 1024; j = j + 2048) {
      value[j] = j;  // Touch pages to force memory allocation
    }
    entry.base = base;

    // printf("2.alloc %p %d\n",entry.base, entry.fd);
    open_tracks.emplace_back(entry);

    global_fd = -1;
  }
#if 1
  std::vector<int> opened(256);
  for (auto &e : open_tracks) {
    // Check that it is persistent
    void * base;
    size_t size;
    if (e.base == 0) {  // Got recycled
      assert(e.has_fd);
      if (e.fd < 0)
        e.fd = mmap_lib::mmap_gc::open(e.name);
      std::tie(base, size) = mmap_lib::mmap_gc::mmap(e.name, e.fd, 4096 * 1024, trigger_clean);
      e.base               = base;
    } else {
      base = e.base;  // without fd, the pointer should be stable
    }
    int *value = (int *)base;
    int  pos   = *value;
    assert(pos >= 0 && pos < 256);
    opened[pos]++;
    EXPECT_EQ(opened[pos], 1);
    for (int j = 1; j < 256 * 1024; j = j + 2048) {
      EXPECT_EQ(value[j], j);
    }
  }

  for (int i = 0; i < 256; ++i) {
    EXPECT_EQ(opened[i], 1);
  }
#endif

#ifndef DOING_ASAN_TEST
  EXPECT_GE(clean_called, 10);  // ulimit was set to 18
#endif
}
#endif

TEST_F(Setup_mmap_gc_test, fd_limit) {
  // Constrain even more the file max fds
  struct rlimit rval;

  rval.rlim_cur = 32;
  rval.rlim_max = 32;

  int err = setrlimit(RLIMIT_NOFILE, &rval);
  ASSERT_EQ(err, 0);

  clean_called              = 0;
  int max_allowd_without_fd = 8;  // Not possible to garbage collect

  Lrand<uint8_t> rng;
  struct track_entry {
    std::string name;
    void *      base;
    int         fd;
  };

  for (int i = 0; i < 256; ++i) {
    track_entry entry;
    std::string name("mmap_gc_test_file_fd");

    entry.name = name + std::to_string(i) + ".data";
    if (rng.max(0xF) >= 0xE || max_allowd_without_fd == 0) {
      entry.fd = mmap_lib::mmap_gc::open(name);
    } else {
      max_allowd_without_fd--;
      entry.fd = -1;
    }

    void * base;
    size_t size;

    std::tie(base, size) = mmap_lib::mmap_gc::mmap(entry.name, entry.fd, 1024, trigger_clean2);
  }
}
