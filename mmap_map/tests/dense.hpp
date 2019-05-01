//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef DENSE_H
#define DENSE_H

// TODO:
//
//  Have a Ptr Map table for objects that can be created
//
//  Have a "adjust_pointers(void *offset) called everytime that a mmap is loaded (called only in dense)
//
//  Say that no pointers in pointer objects (only base objects from dense/sparse table)
//
//  Force to call constructors inside object
//
//  Foo* foo = new (your_memory_address_here) Foo ();
//
//  Do not allow pointers outside dense objects (or to bring outside pointers inside)
//
// Create buckets for memory allocation size.  __builtin_clz(size>>2-1)
//
// 31: 8
// 30: 16
// 29: 32
// 28: 64

#include "mmap_allocator.hpp"
#include <unistd.h>

#ifndef likely
#define likely(x) __builtin_expect((x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect((x), 0)
#endif

template <typename T> class Dense {
private:
  explicit Dense() = delete;

  void reserve(size_t sz) {
    if(__buffer) {
      __buffer = __allocator.reserve(sz);
    } else {
      __buffer = __allocator.reallocate(sz);
    }
  }

public:
  typedef T                 value_type;
  typedef mmap_allocator<T> allocator_type;
  typedef value_type *      iterator;
  typedef const value_type *const_iterator;

  explicit Dense(std::string_view filename)
      : __allocator(filename) {
    __buffer = 0;
    __size   = 0;
  }

  ~Dense() {
    __size = 0; // No need but nice
  }

  void emplace_back() {
    if(__buffer) {
      reserve(__size + 1);
    }
    __size = __size + 1;
  }

  void resize(size_t sz) {
    assert(sz >= __size); // Shrink still not implemented
    if(sz > __size && __buffer) {
      reserve(sz);
    }
    __size = sz;
  }

  void sync() {
    if(__buffer) {
      __allocator.deallocate(__buffer, __size);
      __buffer = 0;
    }
    // WARNING: do not reset __size, lazy open later, must preserve __size
  }

  void reload(uint64_t sz) {
    if(sz == 0) {
      assert(__size == 0);
      return; // Nothing to do. Do not create mmap for empty lgraphs or alredy loaded lgraphs
    }
    if(__buffer) {
      __buffer = __allocator.reallocate(sz);
    }
    __size = sz;
  }

  void clear() noexcept {
    __allocator.clear();
    __size   = 0;
    __buffer = 0;
  }

  size_t capacity() const {
    return __allocator.capacity();
  }

  void push_back(const value_type &x) {
    if(unlikely(__buffer == 0)) {
      __buffer = __allocator.reallocate(__size + 1);
    } else if(unlikely(__size >= capacity())) {
      reserve(__size + 1);
    }
    __buffer[__size] = x;
    __size++;
  }

  template <class... Args> void emplace_back(Args &&... args) {
    if(unlikely(__buffer == 0)) {
      __buffer = __allocator.reallocate(__size + 1);
    } else if(unlikely(__size >= capacity())) {
      reserve(__size + 1);
    }
    __buffer[__size] = value_type(std::forward<Args>(args)...);
    __size++;
  }

  size_t size() const {
    return __size;
  }
  bool empty() const {
    return __size == 0;
  }

  value_type &operator[](const size_t idx) {
    assert(idx < __size);
    if(unlikely(__buffer == 0))
      __buffer = __allocator.reallocate(__size);
    return __buffer[idx];
  }

  const value_type &operator[](const size_t idx) const {
    assert(idx < __size);
    if(unlikely(__buffer == 0))
      __buffer = __allocator.reallocate(__size);
    return __buffer[idx];
  }

  value_type &back() {
    assert(__size);
    if(unlikely(__buffer == 0))
      __buffer = __allocator.reallocate(__size);
    return __buffer[__size - 1];
  }
  const value_type &back() const {
    assert(__size);
    if(unlikely(__buffer == 0))
      __buffer = __allocator.reallocate(__size);
    return __buffer[__size - 1];
  }

  value_type *data() noexcept {
    assert(__size);
    if(unlikely(__buffer == 0))
      __buffer = __allocator.reallocate(__size);
    return __buffer;
  }
  const value_type *data() const noexcept {
    assert(__size);
    if(unlikely(__buffer == 0))
      __buffer = __allocator.reallocate(__size);
    return __buffer;
  }

  iterator begin() {
    if(unlikely(__buffer == 0))
      __buffer = __allocator.reallocate(__size);
    return __buffer;
  }
  iterator end() {
    if(unlikely(__buffer == 0))
      __buffer = __allocator.reallocate(__size);
    return __buffer + __size;
  }

  const_iterator cbegin() {
    if(unlikely(__buffer == 0))
      __buffer = __allocator.reallocate(__size);
    return __buffer;
  }
  const_iterator cend() {
    if(unlikely(__buffer == 0))
      __buffer = __allocator.reallocate(__size);
    return __buffer + __size;
  }

  const_iterator begin() const {
    if(unlikely(__buffer == 0))
      __buffer = __allocator.reallocate(__size);
    return __buffer;
  }
  const_iterator end() const {
    if(unlikely(__buffer == 0))
      __buffer = __allocator.reallocate(__size);
    return __buffer + __size;
  }

  const_iterator cbegin() const {
    if(unlikely(__buffer == 0))
      __buffer = __allocator.reallocate(__size);
    return __buffer;
  }
  const_iterator cend() const {
    if(unlikely(__buffer == 0))
      __buffer = __allocator.reallocate(__size);
    return __buffer + __size;
  }

private:
  allocator_type __allocator;
  size_t         __size;

  mutable value_type *__restrict__ __buffer; // mutable needed to handle the lazy load/unload
};

#endif
