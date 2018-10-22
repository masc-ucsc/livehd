
#pragma once

#include <cstddef>
#include <cstdlib>

#include <atomic>

template <class Type> class spmc256 {
private:
  typedef uint8_t    IndexType;
  typedef char cache_line_pad_t[128]; // Some CPUs could have 128 cache line in LLC

  IndexType               tail; // Not atomic, it is a spmc
  cache_line_pad_t        _pad1;
  std::atomic<IndexType>  head;
  cache_line_pad_t        _pad2;
  Type                    array[256];

public:
  spmc256()
      : tail(0)
      , head(0) {
  }
  virtual ~spmc256() {
  }

  Type *getTailRef() {
    return &array[tail];
  }

  int size() const { // WARNING: NOT ATOMIC. Can give WEIRD RESULTS
    if (tail>head)
      return tail - head;
    else
      return 256 - (head-tail);
  }

  bool enqueue(const Type &item_) {
    // Not thread safe to insert (sp)
    if (full())
      return false;

    array[tail] = item_;
    tail++;

    return true;
  };

  bool full() const { return (tail+1) == head; }
  bool empty() const { return tail == head; }

  bool dequeue(Type &data) {
    for(;;) {
      IndexType head_copy = head.load(std::memory_order_acquire);
      if (head_copy == tail)
        return false;
      data = array[head_copy];
      if (head.compare_exchange_weak(head_copy, (IndexType)(head_copy + 1), std::memory_order_release))
        return true;
    }
  };
};

#if 0
#include <iostream>
#include <pthread.h>
//#define MPMC 1
//#define SPSC 1

#ifdef MPMC
#include "mpmc.hpp"

mpmc<uint32_t> cf(256);
#else
#ifdef SPSC
#include "spsc.hpp"

spsc<uint32_t> cf(256);
#else
spmc256<uint32_t> cf;
#endif
#endif

#define NUM_ITERATIONS 100000000

int total=0;
extern "C" void *consumer(void *threadargs) {

  for(uint32_t i=0;i<(NUM_ITERATIONS);i++) {
    while(cf.empty()) {
      ;
    }
    uint32_t j;
    while(!cf.dequeue(j))
      ;
    total += j;
  }

  return 0;
}


int main() {

  pthread_t c1;
  pthread_create(&c1,0,&consumer,(void *) 0);

#if 0
  pthread_t c2;
  pthread_t c3;
  pthread_t c4;

  pthread_create(&c2,0,&consumer,(void *) 0);
  pthread_create(&c3,0,&consumer,(void *) 0);
  pthread_create(&c4,0,&consumer,(void *) 0);
#endif

  // Producer
  for(uint32_t i=0;i<NUM_ITERATIONS;i++) {
    while(!cf.enqueue(i))
      ;
#if 0
    if ((i&4095)==0) {
      std::cout << "sz:" << cf.size() << std::endl;
    }
#endif
  }

  std::cout << "total:" << total << std::endl;

  return 0;
}

#endif
