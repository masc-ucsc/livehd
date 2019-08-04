//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <functional>
#include <cassert>
#include <iostream>

namespace mmap_lib {
#define MMAPA_MIN_ENTRIES  (1ULL << 10)
#define MMAPA_INCR_ENTRIES (1ULL << 14)
#define MMAPA_MAX_ENTRIES  (1ULL << 31)

template<typename T> class vector {

protected:
  T *allocate(size_t n) const {

    allocate_int(n);

    return reserve(n);
  }

  T *reallocate(size_t n) const {
    return allocate(n);
  }

  T *reserve(size_t n) const {
    if(mmap_base == 0) {
      allocate_int(n);
      assert(mmap_base);
      assert(mmap_size > (sizeof(T) * n+4096));
      return (T *)(&mmap_base[4096]);
    }

    if(mmap_size > (sizeof(T) * n + 4096)) {
      return (T *)(&mmap_base[4096]);
    }

    if(n < MMAPA_MIN_ENTRIES)
      n = MMAPA_MIN_ENTRIES;

    size_t req_size = sizeof(T) * n + 4096;

    if(mmap_size <= req_size) {
      size_t old_size = mmap_size;
      mmap_size += mmap_size / 2; // 1.5 every time
      while(mmap_size <= req_size) {
        mmap_size += MMAPA_INCR_ENTRIES;
      }
      entries_capacity = (mmap_size-4096) / sizeof(T);

      assert(mmap_size <= MMAPA_MAX_ENTRIES * sizeof(T));

      mmap_base = reinterpret_cast<uint64_t *>(mmap_gc::remap(mmap_name, mmap_base, old_size, mmap_size));
    }

    return (T *)(&mmap_base[4096]);
  }

  size_t calc_min_mmap_size() const { return sizeof(T) * MMAPA_MIN_ENTRIES + 4096; }

  T *ref_base() const {
    if (unlikely(mmap_base == nullptr)) {
      if (mmap_name.empty()) {
        mmap_size = calc_min_mmap_size();
      }else{
        size_t file_sz = set_mmap_file();
        if (file_sz==0) {
          mmap_size = calc_min_mmap_size();
        } else {
          mmap_size = file_sz;
        }
      }
      setup_mmap();
      assert(mmap_base);
    }

    return (T *)(&mmap_base[4096]);
  }

public:
  static void global_garbage_collect(std::function<void(void)> gc) {
    static std::function<void(void)> g_collect;
    if (gc)
      g_collect = gc;
    else
      g_collect();
  }

  explicit mmap_allocator(std::string_view filename)
      : mmap_base(0)
      , entries_size(nullptr)
      , entries_capacity(0)
      , mmap_size(0)
      , mmap_fd(-1)
      , alloc(0)
      , mmap_name(filename) {
  }

  explicit mmap_allocator()
      : mmap_base(0)
      , entries_size(nullptr)
      , entries_capacity(0)
      , mmap_size(0)
      , mmap_fd(-1)
      , alloc(0) {
  }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
  void construct(T *ptr) {
#pragma clang diagnostic pop
      // Do nothing, do not allocate/reallocate new elements on resize (destroys old data in mmap)
  }

  void emplace_back() {
    ref_base();
    (*entries_size)++;
  }

  template <class... Args> void emplace_back(Args &&... args) {
    auto *base = ref_base();
    assert(base);
    assert(*entries_size);
    if (unlikely(capacity() <= size())) {
      reserve(size()+1);
    }

    base[*entries_size] = T(std::forward<Args>(args)...);
    (*entries_size)++;
  }

  T &operator[](const size_t idx) {
    auto *base = ref_base();
    assert(idx < size());
    return base[idx];
  }

  const T &operator[](const size_t idx) const {
    const auto *base = ref_base();
    assert(idx < size());
    return base[idx];
  }

  T &back() {
    auto *base = ref_base();
    assert(!empty());
    return base[size() - 1];
  }

  const T &back() const {
    const auto *base = ref_base();
    assert(!empty());
    return base[size() - 1];
  }

  T *begin() { return ref_base(); }
  const T *cbegin() const { return ref_base(); }
  const T *begin() const { return ref_base(); }

  T *end() {
    auto *base = ref_base();
    return &base[size()];
  }

  const T *cend() const {
    const auto *base = ref_base();
    return &base[size()];
  }

  const T *end() const {
    const auto *base = ref_base();
    return &base[size()];
  }

  void clear() {
    if(mmap_fd < 0 && mmap_base == nullptr) {
      assert(mmap_base == nullptr);
      assert(alloc == 0);
      return;
    }

    assert(alloc == 1);
    alloc = 0;
    if(mmap_base) {
      munmap(mmap_base, mmap_size);
    }
    if (mmap_fd >= 0) {
      assert(!mmap_name.empty());
      close(mmap_fd);
      unlink(mmap_name.c_str());
      mmap_fd = -1;
    }
    mmap_base        = nullptr;
    entries_size     = nullptr;
    entries_capacity = 0;
    mmap_size        = 0;
  }

  void deallocate() {
    alloc--;
    if(alloc != 0)
      return;

    bool can_delete_when_all_zeroes=(*entries_size==0);
    if(mmap_base) {
      munmap(mmap_base, mmap_size);
      mmap_base    = nullptr;
      entries_size = nullptr;
    }
    if(mmap_fd >= 0)
      close(mmap_fd);

    if(can_delete_when_all_zeroes)
      unlink(mmap_name.c_str());

    mmap_fd       = -1;
    mmap_base     = 0;
    entries_capacity = 0;
  }

  inline std::string_view get_filename() const { return mmap_name; }
  inline size_t capacity() const { return entries_capacity; }

  uint64_t *ref_config_data(int offset) const {
    assert(offset<4096/8);
    assert(offset>0);

    ref_base(); // Force to get mmap_base
    assert(mmap_base);

    return (uint64_t *)&mmap_base[offset];
  }

  size_t size() const {
    ref_base(); // Force to get entries_size
    return *entries_size;
  }

  bool empty() const {
    return size() == 0;
  }

protected:
  size_t set_mmap_file() {
    assert(!mmap_name.empty());

    if (mmap_fd<0) {

      mmap_fd = open(mmap_name.c_str(), O_RDWR | O_CREAT, 0644);
      if(mmap_fd < 0) {
        std::function<void(void)> fn_void;
        global_garbage_collect(fn_void);

        mmap_fd = open(mmap_name.c_str(), O_RDWR | O_CREAT, 0644);
        if (mmap_fd < 0) {
          std::cerr << "ERROR: Could not mmap file " << mmap_name << std::endl;
          assert(false);
          exit(-4);
        }
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

    return file_size;
  }

  void setup_mmap() {
    assert(mmap_base==nullptr);
    assert(mmap_size>=calc_min_mmap_size());

    void *base;
    std::tie(base, mmap_size) = mmap_gc::mmap(mmap_name, fd, mmap_size, XXX);

    mmap_base = reinterpret_cast<uint64_t *>(base);

    entries_size = mmap_base; // First word in mmap
  }

  T *allocate_int(size_t n) const {
    alloc++;

    if(mmap_fd < 0) {
      size_t file_size = set_mmap_file();
      if(n < MMAPA_MIN_ENTRIES)
        n = MMAPA_MIN_ENTRIES;

      mmap_size        = n * sizeof(T) + 4096;
      entries_capacity = n;
      if(file_size > mmap_size && (file_size < (4*mmap_size))) {
        // If the mmap was there, reuse as long as it was not huge
        mmap_size     = file_size;
        assert(file_size>4096);
        entries_capacity = (file_size-4096) / sizeof(T);
      }
      if (mmap_size != file_size) {
        int ret = ftruncate(mmap_fd, mmap_size);
        if(ret<0) {
          std::cerr << "ERROR: ftruncate could not resize  " << mmap_name << " to " << mmap_size << "\n";
          mmap_base = 0;
          exit(-1);
        }
        file_size = mmap_size;
      }
      setup_mmap();
      return (T *)(&mmap_base[4096]);
    }

    return reserve(n);
  }

  mutable uint64_t *__restrict__ mmap_base;
  mutable uint64_t *__restrict__ entries_size;
  mutable size_t    entries_capacity; // size/sizeof - space_control
  mutable size_t    mmap_size;
  mutable int       mmap_fd;
  mutable int       alloc;
  const std::string mmap_name;
};

} // namespace mmap_lib

