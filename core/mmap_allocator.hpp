//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef MMAP_ALLOCATOR_H
#define MMAP_ALLOCATOR_H

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <iostream>

#define MMAPA_INIT_ENTRIES (1ULL << 14)
#define MMAPA_INCR_ENTRIES (1ULL << 20)
#define MMAPA_MAX_ENTRIES (1ULL << 34)

template <typename T> class mmap_allocator {
public:
  typedef T value_type;

  explicit mmap_allocator(const std::string &filename)
      : mmap_base(0)
      , file_size(0)
      , mmap_size(0)
      , mmap_capacity(0)
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

  T *allocate(size_t n) {
    alloc++;

    if(n < 1024)
      n = 1024;

    if(mmap_fd < 0) {

      mmap_fd = open(mmap_name.c_str(), O_RDWR | O_CREAT, 0644);
      if(mmap_fd < 0) {
        std::cerr << "ERROR: Could not mmap file " << mmap_name << std::endl;
        exit(-4);
      }

      /* Get the size of the file. */
      struct stat s;
      int         status = fstat(mmap_fd, &s);
      if(status < 0) {
        std::cerr << "ERROR: Could not check file status " << mmap_name << std::endl;
        exit(-3);
      }
      file_size = s.st_size;

      mmap_size = MMAPA_INIT_ENTRIES * sizeof(T);
      if(file_size > mmap_size)
        mmap_size = file_size;
      mmap_base = reinterpret_cast<uint64_t *>(mmap(0, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, mmap_fd, 0));
      if(mmap_base == MAP_FAILED) {
        std::cerr << "ERROR: mmap could not allocate\n";
        mmap_base = 0;
        exit(-1);
      }
    }

    if(file_size < sizeof(T) * n) {
      file_size = sizeof(T) * n;

      if(mmap_size < file_size) {
        size_t old_size = mmap_size;
        while(mmap_size < file_size) {
          mmap_size += MMAPA_INCR_ENTRIES;
        }

        assert(mmap_size <= MMAPA_MAX_ENTRIES * sizeof(T));

        mmap_base = reinterpret_cast<uint64_t *>(mremap(mmap_base, old_size, mmap_size, MREMAP_MAYMOVE));
        if(mmap_base == MAP_FAILED) {
          std::cerr << "ERROR: mmap could not allocate\n";
          mmap_base = 0;
          exit(-1);
        }
      }

      assert(file_size < MMAPA_MAX_ENTRIES * sizeof(T));
      ftruncate(mmap_fd, file_size);
    }

    mmap_capacity = n;

    return (T *)(mmap_base);
  }

  virtual void clear() {
    assert(false);
  }

  void deallocate(T *p, size_t) {
    alloc--;
    // std::cout << "mmap_allocator::deallocate\n";
    if(alloc != 0)
      return;

    p = nullptr;
    if(mmap_base)
      munmap(mmap_base, file_size);
    if(mmap_fd >= 0)
      close(mmap_fd);
    mmap_fd       = -1;
    mmap_base     = 0;
    mmap_capacity = 0;
  }

  virtual ~mmap_allocator() {
    // std::cout << "destroy mmap_allocator\n";
  }

  void save_size(uint64_t size) {
    if(mmap_fd < 0)
      return;
    if(mmap_base == 0)
      return;

    file_size  = sizeof(T) * size;

    msync(mmap_base, file_size, MS_SYNC);
  }

  size_t capacity() const {
    return mmap_capacity;
  }

  const std::string &get_filename() const {
    return mmap_name;
  }

protected:
  uint64_t *  mmap_base;
  size_t      file_size;
  size_t      mmap_size;
  size_t      mmap_capacity; // size/sizeof - space_control
  int         mmap_fd;
  int         alloc;
  std::string mmap_name;
};
#endif
