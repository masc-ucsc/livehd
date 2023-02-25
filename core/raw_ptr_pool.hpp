
#pragma once

#include <memory>

#include "iassert.hpp"
#include "spsc.hpp"

class raw_ptr_pool {
public:
  const size_t size;
  explicit raw_ptr_pool(size_t sz) : size(sz) {}

  ~raw_ptr_pool() {
    while (!_pointer_queue.empty()) {
      auto raw_ptr = _pointer_queue.dequeue();
      I(raw_ptr);
      I(*raw_ptr);
      free(*raw_ptr);
    }
  }

  void *get_ptr() {
    auto raw_retval = _pointer_queue.dequeue();
    if (!raw_retval) {
      return malloc(size);
    }
    return *raw_retval;
  }

  void release_ptr(void *to_release) {
    bool fits = _pointer_queue.enqueue(to_release);
    if (fits) {
      return;
    }
    free(to_release);
  }

private:
  spsc256<void *> _pointer_queue;
};
