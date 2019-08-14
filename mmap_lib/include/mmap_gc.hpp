//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <cassert>
#include "absl/container/flat_hash_map.h"

#ifndef MMAP_LIB_LIKELY
#define MMAP_LIB_LIKELY(x) __builtin_expect((x), 1)
#endif
#ifndef MMAP_LIB_UNLIKELY
#define MMAP_LIB_UNLIKELY(x) __builtin_expect((x), 0)
#endif

namespace mmap_lib {
struct mmap_gc_entry {
  std::string name; // Mostly for debugging
  int         fd;
  size_t      size;
  std::function<void(void *)> gc_function;
};

static inline absl::flat_hash_map<void *, mmap_gc_entry> mmap_gc_pool;
static inline void *next_mmap=nullptr;

class mmap_gc {
protected:
  static inline int n_open_mmaps = 0;
  static inline int n_open_fds   = 0;

  static inline int n_max_mmaps = 256;
  static inline int n_max_fds   = 256;

  static void try_collect_step() {
    auto it = mmap_gc_pool.find(next_mmap);
    if (it == mmap_gc_pool.end())
      it = mmap_gc_pool.begin();
    if (it == mmap_gc_pool.end())
      return;
    if (it->second.fd >= 0)
      n_open_fds--;

    n_open_mmaps--;
    it->second.gc_function(it->first);

    auto it2 = it;
    ++it2;
    next_mmap = it2->first;

    mmap_gc_pool.erase(it);
  }

  static void try_collect_fd() {
    int n = 3 * n_open_fds / 4;
    if (n>n_max_fds)
      n_max_fds = n;
    if (n_max_fds<24)
      n_max_fds = 24;
    while(n_open_fds >= n_max_fds)
      try_collect_step();
  }

  static void try_collect_mmap() {
    int n = 3 * n_open_mmaps / 4;
    if (n>n_max_mmaps)
      n_max_mmaps = n;
    if (n_max_mmaps<24)
      n_max_mmaps = 24;
    while(n_open_mmaps >= n_max_mmaps)
      try_collect_step();
  }

public:
  // mmap_map.hpp:    mmap_fd     = mmap_gc::open(mmap_name);
  // mmap_map.hpp:    mmap_txt_fd = mmap_gc::open(mmap_name + "txt");
  // mmap_vector.hpp: mmap_fd     = mmap_gc::open(mmap_name);
  static int open(const std::string &name) {
#ifndef NDEBUG
    for(const auto &e:mmap_gc_pool) {
      assert(e.second.fd >=0 && e.second.name != name); // No name duplicate
    }
#endif

    int fd = ::open(name.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd>=0)
      return fd;

    if (fd == EACCES)
      return -1; // No access, no need to recycle

    try_collect_fd();
    fd = ::open(name.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd>=0)
      return fd;

    return -1;
  }
  // mmap_map.hpp:    mmap_gc::garbage_collect(mmap_base, mmap_size);
  // mmap_map.hpp:    mmap_gc::garbage_collect(old_mmap_base, old_mmap_size);
  // mmap_map.hpp:    mmap_gc::garbage_collect(mmap_base, mmap_size);
  // mmap_vector.hpp: mmap_gc::garbage_collect(mmap_base, mmap_size);
  static void garbage_collect(void *base, size_t size) {

  }

  // mmap_vector.hpp: std::tie(base, mmap_size) = mmap_gc::mmap(mmap_name, mmap_fd, mmap_size, std::bind(&vector<T>::gc_function, this, std::placeholders::_1));
  // mmap_map.hpp:    std::tie(base, size)      = mmap_gc::mmap(mmap_name, fd, size, std::bind(&map<MaxLoadFactor100, Key, T, Hash>::gc_function, this, std::placeholders::_1));
  static std::pair<void *, size_t> mmap(std::string_view name, int fd, size_t size, std::function<void(void *)> gc_function) {

    HERE
    return std::pair(nullptr,0);
  }
  // mmap_vector.hpp: mmap_base     = reinterpret_cast<uint8_t *>(mmap_gc::remap(mmap_name, mmap_base, old_mmap_size, mmap_size));
  // mmap_map.hpp:    mmap_txt_base = reinterpret_cast<uint64_t *>(mmap_gc::remap(mmap_name, mmap_txt_base, mmap_txt_size, size));
  static void *remap(std::string_view name, void *mmap_txt_base, size_t old_size, size_t new_size) {

    return nullptr;
  };
};


};
