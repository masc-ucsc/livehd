//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cassert>
#include <climits>
#include <functional>
#include <map>

#include "absl/container/flat_hash_map.h"

#ifndef MMAP_LIB_LIKELY
#define MMAP_LIB_LIKELY(x) __builtin_expect((x), 1)
#endif
#ifndef MMAP_LIB_UNLIKELY
#define MMAP_LIB_UNLIKELY(x) __builtin_expect((x), 0)
#endif

namespace mmap_lib {
struct mmap_gc_entry {
  static inline int global_age = 1;
  int               age;  // signed (to do quadrants in cleanup)
  mmap_gc_entry() {
    age  = global_age++;
    size = 0;
    fd   = -1;
  }
  std::string                       name;  // Mostly for debugging
  int                               fd;
  size_t                            size;
  void *                            base;
  std::function<bool(void *, bool)> gc_function;
};

class mmap_gc {
protected:
  using gc_pool_type = absl::flat_hash_map<void *, mmap_gc_entry>;  // pointer stability for delete
  static inline gc_pool_type mmap_gc_pool;

  static inline int n_open_mmaps = 0;
  static inline int n_open_fds   = 0;

  static inline int n_max_mmaps = 512;
  static inline int n_max_fds   = 512;

  static void recycle_older() {
    // Recycle around 1/2 of the newer open fds with mmap

    int may_recycle_fds   = 0;
    int may_recycle_mmaps = 0;

    std::vector<mmap_gc_entry> sorted;
    for (auto it : mmap_gc_pool) {
      if (it.second.fd < 0) continue;
      may_recycle_fds++;
      if (it.second.base) may_recycle_mmaps++;

      assert(it.first == it.second.base);
      sorted.emplace_back(it.second);
    }

    int n_recycle_fds   = may_recycle_fds == 1 ? 1 : may_recycle_fds / 2;
    int n_recycle_mmaps = may_recycle_mmaps == 1 ? 1 : may_recycle_mmaps / 2;

    if (n_open_fds < n_max_fds && may_recycle_fds > 4) n_recycle_fds = may_recycle_fds / 4;
    if (n_open_mmaps < n_max_mmaps && may_recycle_mmaps > 4) n_recycle_mmaps = may_recycle_mmaps / 4;

#if 0
    std::cerr << "trying:"
      << " may_recycle_fds:" << may_recycle_fds << " may_recycle_mmaps:" << may_recycle_mmaps
      << " n_recycle_fds:" << n_recycle_fds << " n_recycle_mmaps:" << n_recycle_mmaps
      << " n_open_mmaps:" << n_open_mmaps << " n_max_mmaps:" << n_max_mmaps
      << " n_open_fds:" << n_open_fds << " n_max_fds:" << n_max_fds << "\n";
#endif

    std::sort(sorted.begin(), sorted.end(), [](const mmap_gc_entry &a, const mmap_gc_entry &b) { return a.age < b.age; });

    if (MMAP_LIB_UNLIKELY(mmap_gc_entry::global_age > 32768)) {  // infrequent but enough for coverage/testing
      mmap_gc_entry::global_age = sorted.size();
      int age                   = 1;
      for (const auto e : sorted) {
        auto it        = mmap_gc_pool.find(e.base);
        it->second.age = age++;
      }
    }

    if (n_recycle_fds > sorted.size()/2) {
      n_recycle_fds = 1+sorted.size() / 2;
    }
    if (n_recycle_mmaps > sorted.size()/2) {
      n_recycle_mmaps = 1+sorted.size() / 2;
    }
#ifndef NDEBUG
    if (sorted.size() > 2) {
      assert(sorted[0].age < sorted[1].age);
      assert(sorted[0].age);
    }
#endif

    int n_gc = 0;
    for (const auto e : sorted) {
      if (n_recycle_fds == 0 && n_recycle_mmaps == 0) break;
      auto it = mmap_gc_pool.find(e.base);
      assert(it != mmap_gc_pool.end());
      bool done = mmap_gc::recycle_int(it, false);
      if (done) {
        if (e.base) n_recycle_mmaps--;
        n_recycle_fds--;
        mmap_gc_pool.erase(it);
        n_gc++;
      }
    }
#if 0
    std::cerr << "gc:" << n_gc
      << " n_open_mmaps:" << n_open_mmaps << " n_max_mmaps:" << n_max_mmaps
      << " n_open_fds:" << n_open_fds << " n_max_fds:" << n_max_fds << "\n";
#endif
    assert(n_gc);
  }

  static bool recycle_int(gc_pool_type::iterator it, bool force_recycle) {
#ifndef NDEBUG
    static bool recursion_mode = false;
    assert(!recursion_mode);  // do not call recycle inside the gc_function
    recursion_mode = true;
#endif

    bool aborted = it->second.gc_function(it->first, force_recycle);
#ifndef NDEBUG
    recursion_mode = false;
#endif
    if (aborted) {
      assert(!force_recycle);
#ifndef NDEBUG
      std::cerr << "ABORT GC for " << it->second.name << std::endl;  // OK
#endif
      return false;
    }

    if (it->second.fd >= 0) {
      ::close(it->second.fd);
      n_open_fds--;
    }

    ::munmap(it->first, it->second.size);
    n_open_mmaps--;

    // std::cerr << "mmap_gc_pool del name:" << it->second.name << " fd:" << it->second.fd << " base:" << it->first << std::endl;

    return true;
  }

  static void try_collect_mmap() {
    // std::cerr << "try_collect_mmap\n";
    if (n_open_mmaps < n_max_mmaps) {  // readjust max
      n_max_mmaps = 1 + 3 * n_open_mmaps / 4;
    } else {
      n_max_mmaps = n_open_mmaps / 2;
    }
    recycle_older();
  }

  static std::tuple<void *, size_t> mmap_step(std::string_view name, int fd, size_t size) {
    if (size & 0xFFF) {
      size >>= 12;
      size++;
      size <<= 12;
    }
    assert((size & 0xFFF) == 0);

    if (fd < 0) {
      void *base = ::mmap(0, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, fd, 0);  // superpages
      return {base, size};
    }

    struct stat s;
    int         status = ::fstat(fd, &s);
    /* LCOV_EXCL_START */
    if (status < 0) {
      std::cerr << "mmap_map::reload ERROR Could not check file status " << name << std::endl;
      exit(-3);
    }
    /* LCOV_EXCL_STOP */
    if (s.st_size <= size) {
      int ret = ::ftruncate(fd, size);
      /* LCOV_EXCL_START */
      if (ret < 0) {
        std::cerr << "mmap_map::reload ERROR ftruncate could not resize " << name << " to " << size << "\n";
        exit(-1);
      }
      /* LCOV_EXCL_STOP */
    } else {
      size = s.st_size;
    }

    void *base = ::mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);  // no superpages

    return std::make_tuple(base, size);
  }

public:
  /* LCOV_EXCL_START */
  static void dump() {
    for (auto it : mmap_gc_pool) {
      std::cerr << "name:" << it.second.name << " base:" << it.first << " age:" << it.second.age << " fd:" << it.second.fd
                << std::endl;
    }
  }
  /* LCOV_EXCL_STOP */

  static void delete_file(void *base) {
    auto it = mmap_gc_pool.find(base);
    assert(it != mmap_gc_pool.end());
    assert(it->second.fd >= 0);
    ::close(it->second.fd);
    n_open_fds--;
    unlink(it->second.name.c_str());
    it->second.fd = -1;
  }

  // mmap_map.hpp:    mmap_fd     = mmap_gc::open(mmap_name);
  //
  // mmap_map.hpp:    mmap_txt_fd = mmap_gc::open(mmap_name + "txt");
  // mmap_vector.hpp: mmap_fd     = mmap_gc::open(mmap_name);
  static int open(const std::string &name) {
#if 0
    std::cerr << "mmap_gc_pool open filename:" << name 
      << " n_open_fds=" << n_open_fds
      << " n_open_mmaps=" << n_open_mmaps
      << "\n";
#endif
#ifndef NDEBUG
    for (const auto &e : mmap_gc_pool) {
      if (e.second.fd < 0) continue;
      assert(e.second.name != name);  // No name duplicate (may be OK for multithreaded access)
    }
#endif

    if (n_open_fds > 500) {
      recycle_older();
    }

    int fd = ::open(name.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd >= 0) {
      n_open_fds++;
      return fd;
    }
    try_collect_fd();
    fd = ::open(name.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd >= 0) {
      n_open_fds++;
      return fd;
    }

    /* LCOV_EXCL_STOP */
    dump();  // We were not able to find fds to recycle
    assert(false);
    /* LCOV_EXCL_START */

    return -1;
  }

  // mmap_map.hpp:    mmap_gc::recycle(mmap_base);
  // mmap_vector.hpp: mmap_gc::recycle(mmap_base);
  static void recycle(void *base) {
    // Remove from gc
    auto it = mmap_gc_pool.find(base);
    assert(it != mmap_gc_pool.end());

    bool done = recycle_int(it, true);
    // auto entry = it->second;
    // std::cerr << "mmap_gc_pool del name:" << entry.name << " fd:" << entry.fd << std::endl;
    assert(done);  // Do not call recycle and then deny it!!!!
    mmap_gc_pool.erase(it);
  }

  // mmap_vector.hpp: std::tie(base, mmap_size) = mmap_gc::mmap(mmap_name, mmap_fd, mmap_size, std::bind(&vector<T>::gc_function,
  // this, std::placeholders::_1)); mmap_map.hpp:    std::tie(base, size)      = mmap_gc::mmap(mmap_name, fd, size,
  // std::bind(&map<MaxLoadFactor100, Key, T, Hash>::gc_function, this, std::placeholders::_1));
  static std::tuple<void *, size_t> mmap(std::string_view name, int fd, size_t size,
                                         std::function<bool(void *, bool)> gc_function) {
    auto [base, final_size] = mmap_step(name, fd, size);
    if (base == MAP_FAILED) {
      try_collect_mmap();
      std::tie(base, final_size) = mmap_step(name, fd, size);
      /* LCOV_EXCL_START */
      if (base == MAP_FAILED) {
        std::cerr << "ERROR mmap_lib::mmap could not check allocate " << size / 1024 << "KBs for " << name << std::endl;
        exit(-3);
      }
      /* LCOV_EXCL_STOP */
    }
    n_open_mmaps++;

    mmap_gc_entry entry;
    entry.name        = name;
    entry.fd          = fd;
    entry.size        = final_size;
    entry.gc_function = gc_function;
    entry.base        = base;

    assert(mmap_gc_pool.find(base) == mmap_gc_pool.end());
    // std::cerr << "mmap_gc_pool add name:" << name << " fd:" << fd << " base:" << base << std::endl;
    mmap_gc_pool[base] = entry;

    return {base, final_size};
  }

  // mmap_vector.hpp: mmap_base     = reinterpret_cast<uint8_t *>(mmap_gc::remap(mmap_name, mmap_base, old_mmap_size, mmap_size));
  // mmap_map.hpp:    mmap_txt_base = reinterpret_cast<uint64_t *>(mmap_gc::remap(mmap_name, mmap_txt_base, mmap_txt_size, size));
  static std::tuple<void *, size_t> remap(std::string_view mmap_name, void *mmap_old_base, size_t old_size, size_t new_size) {
    if (new_size & 0xFFF) {
      new_size >>= 12;
      new_size++;
      new_size <<= 12;
    }
    assert((new_size & 0xFFF) == 0);

    auto it = mmap_gc_pool.find(mmap_old_base);
    assert(it != mmap_gc_pool.end());

    assert(old_size == it->second.size);
    assert(old_size != new_size);

    if (it->second.fd >= 0) {
      int ret = ftruncate(it->second.fd, new_size);
      /* LCOV_EXCL_START */
      if (ret < 0) {
        std::cerr << "ERROR: ftruncate could not allocate " << mmap_name << " to " << new_size << "\n";
        exit(-1);
      }
      /* LCOV_EXCL_STOP */
    }

    void *base;
#ifdef __APPLE__
    // No remap in OS X
    if (it->second.fd > 0) {
      munmap(mmap_old_base, old_size);
      base = ::mmap(0, new_size, PROT_READ | PROT_WRITE, MAP_SHARED, it->second.fd, 0);  // no superpages
      /* LCOV_EXCL_START */
      if (base == MAP_FAILED) {
        std::cerr << "ERROR: OS X could not allocate" << mmap_name << "txt with " << new_size << "bytes\n";
        exit(-1);
      }
      /* LCOV_EXCL_STOP */
    } else {
      // Painful new allocation, and then copy
      base = ::mmap(0, new_size, PROT_READ | PROT_WRITE, MAP_SHARED, it->second.fd, 0);  // no superpages
      memcpy(base, mmap_old_base, old_size);
      munmap(mmap_old_base, old_size);
    }
#else
    base = mremap(mmap_old_base, old_size, new_size, MREMAP_MAYMOVE);
    if (base == MAP_FAILED) {
      try_collect_mmap();
      base = mremap(mmap_old_base, old_size, new_size, MREMAP_MAYMOVE);
      /* LCOV_EXCL_START */
      if (base == MAP_FAILED) {
        std::cerr << "ERROR: remmap could not allocate" << mmap_name << "txt with " << new_size << "bytes\n";
        exit(-1);
      }
      /* LCOV_EXCL_STOP */
    }
#endif
    auto entry = it->second;
    entry.size = new_size;

    // std::cerr << "mmap_gc_pool del name:" << entry.name << " fd:" << entry.fd << " base:" << mmap_old_base << std::endl;
    mmap_gc_pool.erase(it);  // old mmap_old_base

    // std::cerr << "mmap_gc_pool add name:" << entry.name << " fd:" << entry.fd << " base:" << base << std::endl;
    entry.base         = base;
    mmap_gc_pool[base] = entry;

    return std::make_tuple(base, new_size);
  }

  static void try_collect_fd() {
    // std::cerr << "try_collect_fd\n";
    if (n_open_fds < n_max_fds) {  // readjust max
      n_max_fds = 1 + 3 * n_open_fds / 4;
    } else {
      n_max_fds = n_open_fds / 2;
    }
    recycle_older();
  }

};

}  // namespace mmap_lib
