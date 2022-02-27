#pragma once

#include <assert.h>

#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <sstream>
#include <string>

namespace is {

// This is the official STL implementation as is in e.g. GCC 6.2. There is no std::align in GCC 4.8 for some reason.
// The code is presented here to provide support for older compilers.
inline void* align(size_t __align, size_t __size, void*& __ptr, size_t& __space) noexcept {
  const auto __intptr  = reinterpret_cast<uintptr_t>(__ptr);
  const auto __aligned = (__intptr - 1u + __align) & -__align;
  const auto __diff    = __aligned - __intptr;
  if ((__size + __diff) > __space)
    return nullptr;
  else {
    __space -= __diff;
    return __ptr = reinterpret_cast<void*>(__aligned);
  }
}

/**
 * Template parameters: T class type, A alignment size in bytes.
 */
template <typename T, size_t A>
class aligned_vector {
public:
  explicit aligned_vector() noexcept : data_(NULL), raw_data_(NULL), size_(0), capacity_(0) {}

  explicit aligned_vector(size_t n) noexcept : data_(NULL), raw_data_(NULL), size_(0), capacity_(0) { resize(n); }

  explicit aligned_vector(size_t n, const T& val) noexcept : data_(NULL), raw_data_(NULL), size_(0), capacity_(0) {
    reserve(n);
    for (size_t i = 0; i < n; i++) {
      push_back(val);
    }
  }

  explicit aligned_vector(const aligned_vector& p) noexcept { copy_from_(p); }

  ~aligned_vector() noexcept { free_data_(); }

  inline size_t size() const { return size_; }

  inline bool empty() { return (size_ == 0); }

  inline T* data() const { return data_; }

  T& at(size_t pos) const {
    assert(pos >= 0 && pos < this->size_);
    return data_[pos];
  }

  aligned_vector<T, A>& operator=(const aligned_vector& p) {
    copy_from_(p);
    return *this;
  }

  inline T& operator[](size_t pos) {
    assert(pos < this->size_);
    return data_[pos];
  }

  inline const T& operator[](size_t pos) const {
    assert(pos < this->size_);
    return data_[pos];
  }

  void clear() { free_data_(); }

  inline size_t capacity() const { return capacity_; }

  void push_back(T& t) {
    if (size_ >= capacity_) {
      reserve((capacity_ == 0) ? (1) : (capacity_ * 2));
    }
    data_[size_] = t;
    size_ += 1;
  }

  void push_back(const T& t) {
    if (size_ >= capacity_) {
      reserve((capacity_ == 0) ? (1) : (capacity_ * 2));
    }
    data_[size_] = t;
    size_ += 1;
  }

  void emplace_back(T& t) { push_back(t); }

  void emplace_back(const T& t) { push_back(t); }

  void resize(size_t size) {
    reserve(size);
    size_ = size;
  }

  inline T& front() {
    assert(size_ > 0);
    return raw_data_[0];
  }

  inline T& back() {
    assert(size_ > 0);
    return raw_data_[size_ - 1];
  }

  void reserve(size_t capacity) {
    // Allocate the aligned memory and perform sanity checks.
    T* new_raw_data     = NULL;
    T* new_aligned_data = alloc_aligned_(&new_raw_data, capacity, A);
    assert(new_raw_data);
    assert(new_aligned_data);

    // Move the existing data.
    size_t min_size = (size_ < capacity) ? (size_) : (capacity);
    std::memmove(new_aligned_data, data_, sizeof(T) * min_size);

    // Reset the memory info.
    if (raw_data_) {
      delete[] raw_data_;
    }
    raw_data_ = new_raw_data;
    data_     = new_aligned_data;
    capacity_ = capacity;
    size_     = min_size;
  }

#ifdef DEBUG_VERBOSE_
  std::string verbose() {
    std::stringstream ss;
    char              x[100];
    sprintf(x, "%lX", (size_t)this->data());

    char raw_x[100];
    sprintf(raw_x, "%lX", (size_t)(this->raw_data_));

    ss << "size() = " << this->size() << " capacity() = " << this->capacity() << " aligned address = " << raw_x << " (% " << A
       << " = " << (((size_t)this->raw_data_)) % ((size_t)A) << ") ";
    ss << " hex address = " << x << " (% " << A << " = " << (((size_t)this->data())) % ((size_t)A) << ")";
    if (this->data() == this->raw_data_) {
      ss << "\tSAME address (no alignment was performed)";
    } else {
      ss << "\tDIFFERENT address (alignment was performed)";
    }

    return ss.str();
  }
#endif

  // begin()
  // end()
  // assign
  // insert

private:
  T*     data_;
  T*     raw_data_;
  size_t size_;
  size_t capacity_;

  void free_data_() {
    if (raw_data_) {
      delete[] raw_data_;
      raw_data_ = NULL;
      data_     = NULL;
      size_     = 0;
      capacity_ = 0;
    }
  }

  T* alloc_aligned_(T** pt, size_t size, size_t alignment_size) {
    *pt                 = new T[size + alignment_size - 1];
    void*  ptr          = (void*)*pt;
    size_t storage_size = (size + alignment_size - 1) * sizeof(T);
    return (T*)is::align(alignment_size, size * sizeof(T), ptr, storage_size);
  }

  void copy_from_(const aligned_vector& p) {
    if (&p == this) {
      return;
    }
    this->free_data_();
    this->reserve(p.capacity());
    this->size_ = p.size();
    memmove(data_, p.data(), p.size());
  }
};

}  // namespace is
