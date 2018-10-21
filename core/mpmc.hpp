// This is free and unencumbered software released into the public domain.

// Anyone is free to copy, modify, publish, use, compile, sell, or
// distribute this software, either in source code form or as a compiled
// binary, for any purpose, commercial or non-commercial, and by any
// means.

// In jurisdictions that recognize copyright laws, the author or authors
// of this software dedicate any and all copyright interest in the
// software to the public domain. We make this dedication for the benefit
// of the public at large and to the detriment of our heirs and
// successors. We intend this dedication to be an overt act of
// relinquishment in perpetuity of all present and future rights to this
// software under copyright law.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

// For more information, please refer to <http://unlicense.org/>

// Implementation of Dmitry Vyukov's MPMC algorithm
// http://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue


#ifndef __MPMC_BOUNDED_QUEUE_INCLUDED__
#define __MPMC_BOUNDED_QUEUE_INCLUDED__

#include <assert.h>
#include <strings.h>

#include <cstddef>
#include <cstdlib>

#include <atomic>

template<typename T>
class mpmc {
public:

  mpmc(size_t size) :
    _size(size),
    //_buffer(reinterpret_cast<node_t *>(aligned_alloc(128,sizeof(node_t)*(_size + 1)))), // page align needed for muslc (alpine)
    _buffer(reinterpret_cast<node_t *>(std::aligned_alloc(8*alignof(std::max_align_t),sizeof(node_t)*(_size + 1)))), // page align needed for muslc (alpine)
    _mask(size - 1),
    _head_seq(0),
    _tail_seq(0)
  {
    //bzero(_buffer,sizeof(node_t)*(_size + 1));

    // make sure it's a power of 2
    assert((_size != 0) && ((_size & (~_size + 1)) == _size));

    for (size_t i = 0; i < _size; ++i) {
      _buffer[i].seq.store(i, std::memory_order_relaxed);
    }
  }

  ~mpmc() {
    std::free(_buffer);
  }

  bool enqueue( const T& data) {
    // _head_seq only wraps at MAX(_head_seq) instead we use a mask to convert the sequence to an array index
    // this is why the ring buffer must be a size which is a power of 2. this also allows the sequence to double as a ticket/lock.
    size_t  head_seq = _head_seq.load(std::memory_order_relaxed);

    for (;;) {
      node_t*  node     = &_buffer[head_seq & _mask];
      size_t   node_seq = node->seq.load(std::memory_order_acquire);
      intptr_t dif      = (intptr_t) node_seq - (intptr_t) head_seq;

      if (dif == 0) {
        if (_head_seq.compare_exchange_weak(head_seq, head_seq + 1, std::memory_order_relaxed)) {
          node->data = data;
          node->seq.store(head_seq + 1, std::memory_order_release);
          return true;
        }
      }else if (dif < 0) {
        return false;
      }else {
        // under normal circumstances this branch should never be taken
        head_seq = _head_seq.load(std::memory_order_relaxed);
      }
    }

    // never taken
    return false;
  }

  bool dequeue( T& data) {
    size_t       tail_seq = _tail_seq.load(std::memory_order_relaxed);

    for (;;) {
      node_t*  node     = &_buffer[tail_seq & _mask];
      size_t   node_seq = node->seq.load(std::memory_order_acquire);
      intptr_t dif      = (intptr_t) node_seq - (intptr_t)(tail_seq + 1);

      if (dif == 0) {
        if (_tail_seq.compare_exchange_weak(tail_seq, tail_seq + 1, std::memory_order_relaxed)) {
          data = node->data;
          node->seq.store(tail_seq + _mask + 1, std::memory_order_release);
          return true;
        }
      }else if (dif < 0) {
        return false;
      }else {
        // under normal circumstances this branch should never be taken
        tail_seq = _tail_seq.load(std::memory_order_relaxed);
      }
    }

    // never taken
    return false;
  }

  bool empty() const { return _head_seq.load() == _tail_seq.load(); }

private:

  struct node_t {
    T                     data;
    std::atomic<size_t>   seq;
  };

  typedef char cache_line_pad_t[128]; // Some CPUs could have 128 cache line in LLC

  // Mostly read only data
  const size_t        _size;
  node_t* const         _buffer;
  const size_t        _mask;
  cache_line_pad_t      _pad1;
  // Mostly enqueue thread call
  std::atomic<size_t> _head_seq;
  cache_line_pad_t      _pad2;
  // Mostly dequeue thread call
  std::atomic<size_t> _tail_seq;
  cache_line_pad_t      _pad3;

  mpmc(const mpmc&) {}
  void operator=(const mpmc&) {}
};

#endif
