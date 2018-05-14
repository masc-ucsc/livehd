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
#include <vector>

template <typename T>
class Dense : public std::vector<T, mmap_allocator<T>> {

public:
  typedef mmap_allocator<T>              allocator_type;
  typedef std::vector<T, allocator_type> b_vector;

  Dense(const std::string & filename)
      : b_vector(get_saved_size(filename), allocator_type(filename)) {

    b_vector::reserve(this->get_allocator().capacity());
  }

  virtual ~Dense() {
    this->get_allocator().save_size(this->size());
  }

  void sync() {
    this->get_allocator().save_size(this->size());
  }
  void reload() {
  }

  void resize(size_t sz) {
    this->get_allocator().save_size(sz);
    if(sz <= this->size()) {
      b_vector::resize(sz);
    } else {
      if(sz > this->capacity())
        this->reserve(sz);
      b_vector::_M_impl._M_finish = b_vector::_M_impl._M_start + sz;
    }
  }

  void clear() {
    b_vector::clear();
  }

private:
  long get_saved_size(const std::string & filename) {
    int fd = open(filename.c_str(), O_RDONLY);
    if(fd < 0)
      return 0;

    uint64_t size;
    read(fd, &size, sizeof(uint64_t));

    close(fd);

    return size;
  }
};

#endif
