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

#include <unistd.h>
#include "mmap_allocator.hpp"

template <typename T>
class Dense {
private:
  explicit Dense() = delete;

public:
  typedef T                   value_type;
  typedef mmap_allocator<T>   allocator_type;
  typedef value_type*         iterator;
  typedef const value_type*   const_iterator;

  explicit Dense(const std::string& filename) : __allocator(filename) {
    size_t sz = get_saved_size(filename);
    if(sz > 0) {
      __buffer = __allocator.allocate(sz);
      __size   = sz;
    } else {
      __buffer = 0;
      __size = 0;
    }
  }

  virtual ~Dense() {
    __allocator.save_size(this->size());
  }

  void sync() {
    __allocator.save_size(this->size());
  }

  void reload() {
  }

  void resize(size_t sz) {
    if(sz > __size) {
      reserve(sz);
    }
    __size = sz;
  }

  void clear() noexcept {
    __size = 0;
  }

  void push_back(const value_type& x) {
    if(__size >= capacity()) {
      reserve(__size+1);
    }
    __buffer[__size] = x;
    __size++;
  }

  template <class... Args> void emplace_back(Args&&... args) {
    if(__size >= capacity()) {
      reserve(__size+1);
    }
    __buffer[__size] = value_type(std::forward<Args>(args)...);
    __size++;
  }

  size_t size() const { return __size; }
  size_t capacity() const { return __allocator.capacity(); }
  bool empty() const { return __size == 0; }

  void reserve(size_t sz) {
    value_type* new_buffer = __allocator.allocate(sz);
    //since we are using mmap with memremap, we do not need to worry about the
    //old buffer pointer, since the OS will move it and thus deallocate the old
    //memory
    __buffer = new_buffer;
  }

  value_type& operator[](const size_t idx) {
    assert(idx < __size);
    return __buffer[idx];
  }

  const value_type& operator[](const size_t idx) const {
    assert(idx < __size);
    return __buffer[idx];
  }

  value_type& back() { return __buffer[__size-1]; }
  const value_type& back() const { return __buffer[__size-1]; }

  value_type* data() noexcept { return __buffer; }
  const value_type* data() const noexcept  { return __buffer; }

	iterator begin() { return __buffer; }
	iterator end()   { return __buffer+__size; }

	const_iterator cbegin() { return __buffer; }
	const_iterator cend()   { return __buffer+__size; }

	const_iterator begin() const { return __buffer; }
	const_iterator end()   const { return __buffer+__size; }

	const_iterator cbegin() const { return __buffer; }
  const_iterator cend() const { return __buffer+__size; }


private:

  allocator_type  __allocator;
  size_t          __size;

  value_type*     __buffer;


  size_t get_saved_size(const std::string & filename) {
    int fd = open(filename.c_str(), O_RDONLY);
    if(fd < 0)
      return 0;

    size_t size;
    int ret = read(fd, &size, sizeof(uint64_t));
    if(ret <= 0)
      return 0;

    close(fd);
    return size;
  }
};

#endif
