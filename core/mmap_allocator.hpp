//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <iostream>

#include "graph_library.hpp"

#define MMAPA_MIN_SIZE (1ULL << 10)
#define MMAPA_INCR_SIZE (1ULL << 14)
#define MMAPA_MAX_ENTRIES (1ULL << 34)

template <typename T> class mmap_allocator {
public:
  typedef T value_type;

  explicit mmap_allocator(const std::string &filename)
      : mmap_base(0)
      , mmap_capacity(0)
      , mmap_size(0)
      , mmap_fd(-1)
      , alloc(0)
      , mmap_name(filename) {
  }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
  void construct(T *ptr){
#pragma clang diagnostic pop
      // Do nothing, do not allocate/reallocate new elements on resize (destroys old data in mmap)
  }

  T *reserve(size_t n) const {
    if(mmap_base == 0) {
      allocate_int(n);
      assert(mmap_base);
      assert(mmap_size > (sizeof(T) * n));
      return (T *)(mmap_base);
    }

    if(mmap_size > (sizeof(T) * n)) {
      return (T *)(mmap_base);
    }

    if(n < MMAPA_MIN_SIZE)
      n = MMAPA_MIN_SIZE;

    size_t req_size = sizeof(T) * n;

    if(mmap_size <= req_size) {
      size_t old_size = mmap_size;
      mmap_size += mmap_size / 2; // 1.5 every time
      while(mmap_size <= req_size) {
        mmap_size += MMAPA_INCR_SIZE;
      }
      mmap_capacity = mmap_size / sizeof(T);

      assert(mmap_size <= MMAPA_MAX_ENTRIES * sizeof(T));

      mmap_base = reinterpret_cast<uint64_t *>(mremap(mmap_base, old_size, mmap_size, MREMAP_MAYMOVE));
      if(mmap_base == MAP_FAILED) {
        std::cerr << "ERROR: mmap could not allocate" << mmap_name << "\n";
        mmap_base = 0;
        exit(-1);
      }
      int ret = ftruncate(mmap_fd, mmap_size);
      if(ret<0) {
        std::cerr << "ERROR: ftruncate could not allocate " << mmap_name << " to " << mmap_size << "\n";
        mmap_base = 0;
        exit(-1);
      }
    }

    return (T *)(mmap_base);
  }

  T *allocate(size_t n) const {

    allocate_int(n);

    return reserve(n);
  }

  T *reallocate(size_t n) const {
    return allocate(n);
  }

  void clear() {
    if(mmap_fd < 0) {
      unlink(mmap_name.c_str());
      assert(mmap_base == 0);
      assert(alloc == 0);
      return;
    }

    assert(alloc == 1);
    alloc = 0;
    if(mmap_base) {
      munmap(mmap_base, mmap_size);
    }
    close(mmap_fd);
    unlink(mmap_name.c_str());
    mmap_fd       = -1;
    mmap_base     = 0;
    mmap_capacity = 0;
    mmap_size     = 0;
  }

  void deallocate(T *p, size_t sz) {
    alloc--;
    if(alloc != 0)
      return;

    bool can_delete_when_all_zeroes=false;
    if(mmap_base) {
      if (sz<=16) { // Common in char_arrays
        assert(mmap_size>16);
        uint64_t *t=static_cast<uint64_t *>(mmap_base);
        can_delete_when_all_zeroes = t[0] == 0 && t[1] == 0;
      }
      munmap(mmap_base, mmap_size);
    }
    if(mmap_fd >= 0)
      close(mmap_fd);

    if(can_delete_when_all_zeroes)
      unlink(mmap_name.c_str());

    mmap_fd       = -1;
    mmap_base     = 0;
    mmap_capacity = 0;
  }

  inline size_t capacity() const {
    return mmap_capacity;
  }

  size_t size() const {
    return mmap_size / sizeof(T);
  }

  const std::string &get_filename() const {
    return mmap_name;
  }

protected:
  T *allocate_int(size_t n) const {
    alloc++;

    if(n < MMAPA_MIN_SIZE)
      n = MMAPA_MIN_SIZE;

    if(mmap_fd < 0) {

      mmap_fd = open(mmap_name.c_str(), O_RDWR | O_CREAT, 0644);
      if(mmap_fd < 0) {
        Graph_library::sync_all(); // Garbage collect all the lgraph mmaps

        mmap_fd = open(mmap_name.c_str(), O_RDWR | O_CREAT, 0644);
        if (mmap_fd<0) {
          std::cerr << "ERROR: Could not mmap file " << mmap_name << std::endl;
          assert(false);
          exit(-4);
        }
      }

      /* Get the size of the file. */
      struct stat s;
      int         status = fstat(mmap_fd, &s);
      if(status < 0) {
        std::cerr << "ERROR: Could not check file status " << mmap_name << std::endl;
        exit(-3);
      }
      size_t file_size = s.st_size;

      mmap_size     = n * sizeof(T);
      mmap_capacity = n;
      if(file_size > mmap_size) {
        mmap_size     = file_size;
        mmap_capacity = file_size / sizeof(T);
      }
      mmap_base = reinterpret_cast<uint64_t *>(mmap(0, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, mmap_fd, 0));
      if(mmap_base == MAP_FAILED) {
        std::cerr << "ERROR: mmap could not adjust\n";
        mmap_base = 0;
        exit(-1);
      }

      if (mmap_size != file_size) {
        int ret = ftruncate(mmap_fd, mmap_size);
        if(ret<0) {
          std::cerr << "ERROR: ftruncate could not adjust " << mmap_name << " to " << mmap_size << "\n";
          mmap_base = 0;
          exit(-1);
        }
      }

      return (T *)(mmap_base);
    }

    return reserve(n);
  }

  mutable uint64_t *__restrict__ mmap_base;
  mutable size_t    mmap_capacity; // size/sizeof - space_control
  mutable size_t    mmap_size;
  mutable int       mmap_fd;
  mutable int       alloc;
  std::string       mmap_name;
};

