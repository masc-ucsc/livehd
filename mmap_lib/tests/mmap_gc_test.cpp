//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "rng.hpp"

#include "mmap_gc.hpp"

using testing::HasSubstr;

unsigned int rseed = 123;

class Setup_mmap_gc_test : public ::testing::Test {
protected:

  void SetUp() override {
    struct rlimit rval;

    rval.rlim_cur = 16;
    rval.rlim_max = 18;

    int err = setrlimit(RLIMIT_NOFILE, &rval);
    ASSERT_EQ(err,0);
  }

  void TearDown() override {
  }
};

static void trigger_clean(void *) {}

TEST_F(Setup_mmap_gc_test, fd_limit) {

  int nfailed = 0;
  std::vector<int> open_fd;
  for (int i = 0; i < 17; ++i) {
    std::string name("mmap_gc_test_file");
    name   = name + std::to_string(i) + ".data";
    int fd = mmap_lib::mmap_gc::open(name);
    if (fd < 0)
      nfailed++;
    else
      open_fd.emplace_back(fd);
  }

  EXPECT_GE(nfailed, 0);
  EXPECT_GE(open_fd.size(),12);

  for(auto fd:open_fd)
    close(fd);
}

TEST_F(Setup_mmap_gc_test, mmap_limit) {
  Rng rng(15);
  struct track_entry {
    std::string name;
    void *base;
    int  fd;
  };
  std::vector<track_entry> open_tracks;

  for (int i = 0; i < 17; ++i) {
    track_entry entry;
    std::string name("mmap_gc_test_file");

    entry.name = name + std::to_string(i) + ".data";
    if (rng.uniform<char>(0x80) >= 0x60)
      entry.fd  = mmap_lib::mmap_gc::open(name);
    else
      entry.fd  = -1;

    void *base;
    size_t size;

    std::tie(base, size) = mmap_lib::mmap_gc::mmap(entry.name, entry.fd, 8192, trigger_clean);

    open_tracks.emplace_back(entry);
  }
}

