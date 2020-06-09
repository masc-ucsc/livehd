//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <functional>
#include <iostream>

#include "mmap_gc.hpp"

namespace mmap_lib {
#define MMAPA_MIN_ENTRIES (1ULL << 10)
#define MMAPA_INCR_ENTRIES (1ULL << 14)
#define MMAPA_MAX_ENTRIES (1ULL << 31)

template <typename T>
class vector {
protected:
  size_t set_mmap_file() const {
    assert(!mmap_name.empty());

    if (mmap_fd < 0) {
      mmap_fd = mmap_gc::open(mmap_name);
    }

    // Get the size of the file
    struct stat s;
    int         status = fstat(mmap_fd, &s);
    /* LCOV_EXCL_STOP */
    if (status < 0) {
      std::cerr << "ERROR: Could not check file status " << mmap_name << std::endl;
      exit(-3);
    }
    /* LCOV_EXCL_START */

    return s.st_size;
  }

  void truncate_file_adjust_mmap_size(size_t file_size) const {
    if (file_size > mmap_size && (file_size < (4 * mmap_size))) {
      // If the mmap was there, reuse as long as it was not huge
      mmap_size = file_size;
      assert(file_size > 4096);
    }
    if (mmap_size != file_size) {
      int ret = ftruncate(mmap_fd, mmap_size);
      /* LCOV_EXCL_STOP */
      if (ret < 0) {
        std::cerr << "ERROR: ftruncate could not resize  " << mmap_name << " to " << mmap_size << "\n";
        mmap_base = 0;
        exit(-1);
      }
      /* LCOV_EXCL_START */
      file_size = mmap_size;
    }
  }

  void grow_mmap_size(size_t n) const {
    if (n < MMAPA_MIN_ENTRIES) n = MMAPA_MIN_ENTRIES;

    size_t req_size = sizeof(T) * n + 4096;
    if (mmap_size == 0) {
      mmap_size = req_size;
    } else if (mmap_size <= req_size) {
      mmap_size += mmap_size / 2;  // 1.5 every time
      while (mmap_size <= req_size) {
        mmap_size += MMAPA_INCR_ENTRIES;
      }
      assert(mmap_size <= MMAPA_MAX_ENTRIES * sizeof(T));
    }
  }

  void adjust_mmap(size_t old_mmap_size) const {
    assert(mmap_base != nullptr);
    assert(mmap_size != old_mmap_size); // waste of time. Who call it?

    void *base;
    std::tie(base, mmap_size) = mmap_gc::remap(mmap_name, mmap_base, old_mmap_size, mmap_size);
    mmap_base                 = reinterpret_cast<uint8_t *>(base);
    entries_capacity          = (mmap_size - 4096) / sizeof(T);
    entries_size              = (uint64_t *)mmap_base;  // First word in mmap
  }

  void setup_mmap() const {
    assert(mmap_base == nullptr);
    assert(mmap_size >= calc_min_mmap_size());

    void *base;
    std::tie(base, mmap_size) = mmap_gc::mmap(mmap_name, mmap_fd, mmap_size,
                                              std::bind(&vector<T>::gc_done, this, std::placeholders::_1, std::placeholders::_2));

    entries_capacity = (mmap_size - 4096) / sizeof(T);
    mmap_base        = reinterpret_cast<uint8_t *>(base);

    entries_size = (uint64_t *)mmap_base;  // First word in mmap
  }

  __attribute__((noinline)) T *reserve_int(size_t n) const {
    if (mmap_base == nullptr) {
      assert(mmap_fd < 0);
      assert(mmap_size == 0);

      grow_mmap_size(n);

      if (!mmap_name.empty()) {
        size_t file_size = set_mmap_file();
        truncate_file_adjust_mmap_size(file_size);
      }

      setup_mmap();

      assert(mmap_base);
      assert(mmap_size >= (sizeof(T) * n + 4096));
      assert(entries_capacity >= n);
      return (T *)(&mmap_base[4096]);
    }

    if (mmap_size > (sizeof(T) * n + 4096)) {
      assert(entries_capacity > n);
      return (T *)(&mmap_base[4096]);
    }

    assert(mmap_base != nullptr);
    assert(mmap_size);

    auto old_mmap_size = mmap_size;
    grow_mmap_size(n);
    if (!mmap_name.empty()) {
      truncate_file_adjust_mmap_size(old_mmap_size);
    }

    adjust_mmap(old_mmap_size);

    return (T *)(&mmap_base[4096]);
  }

  mutable uint8_t *__restrict__ mmap_base;
  mutable uint64_t *entries_size;
  mutable size_t    entries_capacity;  // size/sizeof - space_control
  mutable size_t    mmap_size;
  mutable int       mmap_fd;
  const std::string mmap_path;
  const std::string mmap_name;

  bool gc_done(void *base, bool force_recycle) const {
    assert(base == mmap_base);

    if (mmap_fd >= 0 && *entries_size == 0) {
      unlink(mmap_name.c_str());
    }

    mmap_base        = nullptr;
    entries_size     = nullptr;
    mmap_fd          = -1;
    entries_capacity = 0;

    return false;
  }

  size_t calc_min_mmap_size() const { return sizeof(T) * MMAPA_MIN_ENTRIES + 4096; }

  __attribute__((inline)) T *ref_base() const {
    if (MMAP_LIB_LIKELY(mmap_base != nullptr)) {
      return (T *)(mmap_base + 4096);
    }
    if (mmap_name.empty()) {
      mmap_size = calc_min_mmap_size();
    } else {
      size_t file_sz = set_mmap_file();
      if (file_sz == 0) {
        mmap_size = calc_min_mmap_size();
      } else {
        mmap_size = file_sz;
      }
    }
    setup_mmap();
    assert(mmap_base);

    return (T *)(&mmap_base[4096]);
  }

public:
  explicit vector(std::string_view _path, std::string_view _map_name)
      : mmap_base(0)
      , entries_size(nullptr)
      , entries_capacity(0)
      , mmap_size(0)
      , mmap_fd(-1)
      , mmap_path(_path.empty() ? "." : _path)
      , mmap_name{std::string(_path) + std::string("/") + std::string(_map_name)} {
    if (mmap_path != ".") {
      struct stat sb;
      if (stat(mmap_path.c_str(), &sb) != 0 || !S_ISDIR(sb.st_mode)) {
        int e = mkdir(mmap_path.c_str(), 0755);
        assert(e >= 0);
      }
    }
  }

  explicit vector() : mmap_base(0), entries_size(nullptr), entries_capacity(0), mmap_size(0), mmap_fd(-1) {}

  ~vector() {
    if (mmap_base) {
      mmap_gc::recycle(mmap_base);
      assert(mmap_base == nullptr);
    }
  }

  // Allocates space, but it does not touch contents
  void reserve(size_t n) const { reserve_int(n); }

  void emplace_back() {
    ref_base();
    if (MMAP_LIB_UNLIKELY(capacity() <= *entries_size)) {
      reserve_int(size() + 1);
    }
    (*entries_size)++;
  }

  template <class... Args>
  void emplace_back(Args &&... args) {
    auto *base = ref_base();
    assert(entries_size);
    if (MMAP_LIB_UNLIKELY(capacity() <= *entries_size)) {
      base = reserve_int(size() + 1);
    }

    base[*entries_size] = T(std::forward<Args>(args)...);
    (*entries_size)++;
  }

#if 0
  template <typename Data>
    T *doCreate(const size_t idx, Data&& val) {
      const auto *base = ref_base();
      assert(idx < capacity());
      base[idx] = std::forward<Data>(val);
      return &base[idx];
    }

	T *set(const size_t idx, T &&val) {
		return doCreate(idx, std::move(val));
	}
	T *set(const size_t idx, const T &val) {
		return doCreate(idx, val);
	}
#endif
  template <class... Args>
  T *set(const size_t idx, Args &&... args) {
    auto *base = ref_base();
    assert(base);
    assert(idx < capacity());

    base[idx] = T(std::forward<Args>(args)...);

    return &base[idx];
  }

  [[nodiscard]] const T *ref(size_t const &idx) const {
    const auto *base = ref_base();
    assert(idx < size());
    return &base[idx];
  }

  [[nodiscard]] T *ref(size_t const &idx) {
    auto *base = ref_base();
    assert(idx < size());
    return &base[idx];
  }

  [[nodiscard]] const T &operator[](const size_t idx) const {
    const auto *base = ref_base();
    assert(idx < size());
    return base[idx];
  }

  T *      begin() { return ref_base(); }
  const T *cbegin() const { return ref_base(); }
  const T *begin() const { return ref_base(); }

  T *end() {
    auto *base = ref_base();
    return &base[*entries_size];
  }

  const T *cend() const {
    const auto *base = ref_base();
    return &base[*entries_size];
  }

  const T *end() const {
    const auto *base = ref_base();
    return &base[*entries_size];
  }

  void clear() {
    if (mmap_base == nullptr) {
      assert(mmap_base == nullptr);
      assert(mmap_fd < 0);
      if (!mmap_name.empty()) unlink(mmap_name.c_str());
      return;
    }
    if (*entries_size != 0) {
      *entries_size = 0;
    }

    *entries_size = 0;  // Setting zero, triggers an unlink when calling gc_done

    mmap_gc::recycle(mmap_base);
    assert(entries_capacity == 0);
    assert(entries_size == nullptr);
  }

  [[nodiscard]] inline std::string_view get_name() const { return mmap_name; }
  [[nodiscard]] inline std::string_view get_path() const { return mmap_path; }

  [[nodiscard]] inline size_t capacity() const { return entries_capacity; }

  uint64_t *ref_config_data(int offset) const {
    assert(offset < 4096 / 8);
    assert(offset > 0);

    ref_base();  // Force to get mmap_base
    assert(mmap_base);

    return (uint64_t *)&mmap_base[offset];
  }

  [[nodiscard]] size_t size() const {
    if (MMAP_LIB_LIKELY(entries_size != nullptr)) {
      return *entries_size;
    }
    ref_base();  // Force to get entries_size
    return *entries_size;
  }

  bool empty() const { return size() == 0; }
};

}  // namespace mmap_lib
