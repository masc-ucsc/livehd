
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
      void *raw_ptr;
      bool  something = _pointer_queue.dequeue(raw_ptr);
      I(something);
      free(raw_ptr);
    }
  }

  void *get_ptr() {
    void *raw_retval;
    bool  recycle_success = _pointer_queue.dequeue(raw_retval);
    if (!recycle_success) {
      raw_retval = malloc(size);
    }
    return raw_retval;
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
