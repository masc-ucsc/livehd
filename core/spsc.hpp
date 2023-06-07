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

// Note:
// A combination of the algorithms described by the circular buffers
// documentation found in the Linux kernel, and the bounded MPMC queue
// by Dmitry Vyukov[1]. Implemented in pure C++11. Should work across
// most CPU architectures.
//
// [1] http://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue

#pragma once

#include <atomic>
#include <cassert>
#include <climits>  // for CHAR_BIT
#include <cstdlib>
#include <iostream>
#include <optional>

#if 0
template <typename T>
class spsc {
public:
  spsc() = delete;

  spsc(size_t size)
      : _size(size)
      , _mask(size - 1)
      , _buffer(reinterpret_cast<T *>(aligned_malloc(size + 1)))
      ,  // need one extra element for a guard
      _head(0)
      , _tail(0) {
    // make sure it's a power of 2
    assert((_size != 0) && ((_size & (~_size + 1)) == _size));
  }

  ~spsc() { free(_buffer); }

  bool empty() const { return _head == _tail; }

  bool enqueue(T &input) {
    const size_t head = _head.load(std::memory_order_relaxed);

    if (((_tail.load(std::memory_order_acquire) - (head + 1)) & _mask) >= 1) {
      _buffer[head & _mask] = input;
      _head.store(head + 1, std::memory_order_release);
      return true;
    }
    return false;
  }

  bool dequeue(T &output) {
    const size_t tail = _tail.load(std::memory_order_relaxed);

    if (((_head.load(std::memory_order_acquire) - tail) & _mask) >= 1) {
      output = _buffer[_tail & _mask];
      _tail.store(tail + 1, std::memory_order_release);
      return true;
    }
    return false;
  }

private:
  typedef char cache_line_pad_t[64];

  cache_line_pad_t _pad0;
  const size_t     _size;
  const size_t     _mask;
  T *const         _buffer;

  cache_line_pad_t    _pad1;
  std::atomic<size_t> _head;

  cache_line_pad_t    _pad2;
  std::atomic<size_t> _tail;

  spsc(const spsc &) {}
  void operator=(const spsc &) {}
};
#endif

template <typename T>
class spsc256 {
protected:
  static inline char* align_for(char* ptr) {
    const std::size_t alignment = 256;
    return ptr + (alignment - (reinterpret_cast<std::uintptr_t>(ptr) % alignment)) % alignment;
  }

  static inline void* aligned_malloc(size_t size) {
    if (std::alignment_of<T>::value >= 256) {
      return std::malloc(size);
    }
    size_t alignment = 256;
    void*  raw       = std::malloc(size + alignment - 1 + sizeof(void*));
    if (!raw) {
      return nullptr;
    }
    char* ptr = align_for(reinterpret_cast<char*>(raw) + sizeof(void*));
    assert(ptr > raw);
    *(reinterpret_cast<void**>(ptr) - 1) = raw;
    return ptr;
  }

  static inline void aligned_free(void* ptr) {
    if (ptr == nullptr) {
      return;
    }

    if (std::alignment_of<T>::value >= 256) {
      return std::free(ptr);
    }

    std::free(*(reinterpret_cast<void**>(ptr) - 1));
  }

public:
  spsc256() : _buffer(reinterpret_cast<T*>(aligned_malloc(sizeof(void*) * (256 + 1)))), _head(0), _tail(0) {}

  ~spsc256() { aligned_free(_buffer); }

  bool empty() const { return _head == _tail; }

  bool enqueue(T& input) {
    const size_t head = _head.load(std::memory_order_relaxed);

    if (((_tail.load(std::memory_order_acquire) - (head + 1)) & 255) >= 1) {
      _buffer[head & 255] = input;
      _head.store(head + 1, std::memory_order_release);
      return true;
    }
    return false;
  }

  std::optional<T> dequeue() {
    const size_t tail = _tail.load(std::memory_order_relaxed);

    if (((_head.load(std::memory_order_acquire) - tail) & 255) >= 1) {
      auto output = _buffer[_tail & 255];
      _tail.store(tail + 1, std::memory_order_release);
      return output;
    }
    return {};
  }

private:
  typedef char cache_line_pad_t[64];

  cache_line_pad_t _pad0;
  T*               _buffer;

  cache_line_pad_t    _pad1;
  std::atomic<size_t> _head;

  cache_line_pad_t    _pad2;
  std::atomic<size_t> _tail;

  spsc256(const spsc256&) {}
  void operator=(const spsc256&) {}
};
