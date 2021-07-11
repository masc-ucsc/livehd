//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "mmap_gc.hpp"

#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
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
  track_entry() {
    base = 0;
    fd = -1;
  }
  std::string name;
  void *      base;
  int         fd;
};

std::vector<track_entry> open_tracks;

#define MAX_RLIMIT_FDS 32
int n_open_fds = 0;

static bool trigger_clean1(void *base, bool force_recycle) {
  return true;
}

static bool trigger_clean2(void *base, bool force_recycle) {

  Lrand<uint8_t> rnd;

  if (!force_recycle && n_open_fds< rnd.between(1,MAX_RLIMIT_FDS-1))
    return false;

  int n_found = 0;
  for (auto &e : open_tracks) {
    if (e.base != base)
      continue;
    EXPECT_NE(e.fd, -1);  // only mmap with fd can be recycled
    e.fd   = -1;
    e.base = 0;
    ++n_found;
  }
  EXPECT_EQ(n_found,1);

  clean_called++;

  --n_open_fds;

  return true;
}

TEST_F(Setup_mmap_gc_test, fd_limit) {
  // Constrain even more the file max fds
  struct rlimit rval;

  rval.rlim_cur = MAX_RLIMIT_FDS;
  rval.rlim_max = MAX_RLIMIT_FDS;

  int err = setrlimit(RLIMIT_NOFILE, &rval);
  ASSERT_EQ(err, 0);

  clean_called              = 0;
  int max_allowd_without_fd = 8;  // Not possible to garbage collect

  Lrand<uint8_t> rnd;

  mkdir("lgdb_gc_test", 0755);

  open_tracks.resize(2048);
  std::vector<int> fd_id_created;

  for (int i = 0; i < 256; ++i) {
    track_entry &entry = open_tracks[i];

    std::string name("lgdb_gc_test/mmap_gc_test_file_fd");

    entry.name = name + std::to_string(i) + ".data";
    if (rnd.any() >= 128 || max_allowd_without_fd == 0) {
      entry.fd = mmap_lib::mmap_gc::open(entry.name);
      ++n_open_fds;
      fd_id_created.emplace_back(i);
    } else {
      max_allowd_without_fd--;
      entry.fd = -1;
    }

    size_t size;
    std::tie(entry.base, size) = mmap_lib::mmap_gc::mmap(entry.name, entry.fd, rnd.between(2,16)*1024, trigger_clean2);
    (void)size;
    uint32_t *ptr = (uint32_t *)entry.base;
    ptr[i] = i;
  }

  EXPECT_GT(clean_called,1);

  // Now, reopen FDS to check contents
  for(auto i:fd_id_created) {
    std::string name("lgdb_gc_test/mmap_gc_test_file_fd");
    auto ename = name + std::to_string(i) + ".data";

    auto efd = mmap_lib::mmap_gc::open(ename);

    fd_id_created.emplace_back(i);

    size_t size;
    void *base;
    std::tie(base, size) = mmap_lib::mmap_gc::mmap(ename, efd, rnd.between(2,16)*1024, trigger_clean1);
    (void)size;
    uint32_t *ptr = (uint32_t *)base;

    EXPECT_EQ(ptr[i],i);
  }

}
