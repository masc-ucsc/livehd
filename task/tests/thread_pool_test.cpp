//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "thread_pool.hpp"

#include <iostream>
#include <atomic>

#include "fmt/format.h"

#include "concurrentqueue.hpp"
#include "gtest/gtest.h"
#include "lbench.hpp"
#include "mpmc.hpp"
#include "spmc.hpp"

int control = 1023;

struct Test1 {
  int  a;
  void inc_a(int v) {
    a += v;
    std::cout << "Test1.inc_a:" << a << std::endl;
  }
};

std::atomic<int> total;
void             mywork(int a) { total.fetch_add(a, std::memory_order_relaxed); }

class GTest1 : public ::testing::Test {
protected:
  void SetUp() override {}
};

Thread_pool global_pool(8);
struct Test2 {
  static inline std::mutex lgs_mutex;
  static inline std::atomic<int> total;
  void inc_a(int v) {
#if 0
    {
      std::lock_guard<std::mutex> guard(lgs_mutex);
      fmt::print("1.inc:{}\n", v);
    }
#endif
    total += v;
  }
  static void inc_b(int v) {
#if 0
    {
      std::lock_guard<std::mutex> guard(lgs_mutex);
      fmt::print("1.inc:{}\n", v);
    }
#endif
    total += v;
  }
  void spawner(int v) {
#if 0
    {
      std::lock_guard<std::mutex> guard(lgs_mutex);
      fmt::print("1.spawner:{}\n", v);
    }
#endif
    for(auto i=0u;i<3;i++) {
      global_pool.add(&Test2::inc_a, this, v);
      global_pool.add(&Test2::inc_b, v);
    }
  }
};

TEST_F(GTest1, mpmc_thread) {

  int prime_numbers[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43,
    47, 53, 59, 61, 67, 71, 73, 79, 83, 89, 97, 101, 103, 107, 109, 113, 127,
    131, 137, 139, 149, 151, 157, 163, 167, 173, 179, 181, 191, 193, 197, 199,
    211, 223, 227, 229, 233, 239, 241, 251, 257, 263, 269, 271, 277, 281, 283,
    293, 307, 311, 313, 317, 331, 337, 347, 349, 353, 359, 367, 373, 379, 383,
    389, 397, 401, 409, 419, 421, 431, 433, 439, 443, 449, 457, 461, 463, 467,
    479, 487, 491, 499, 503, 509, 521, 523, 541, 547, 557, 563, 569, 571, 577,
    587, 593, 599, 601, 607, 613, 617, 619, 631, 641, 643, 647, 653, 659, 661,
    673, 677, 683, 691, 701, 709, 719, 727, 733, 739, 743, 751, 757, 761, 769,
    773, 787, 797, 809, 811, 821, 823, 827, 829, 839, 853, 857, 859, 863, 877,
    881, 883, 887, 907, 911, 919, 929, 937, 941, 947, 953, 967, 971, 977, 983,
    991, 997};

  for (int n = 1; n < 5; n = n + 2) {
    total = 0;

    Test2 t2;
    t2.total = 0;

    Lbench      bb("task.THREAD_POOL_pool" + std::to_string(n));
    const int   JOB_COUNT = 20;

    for (int i = 0; i < JOB_COUNT; ++i) {
      for (auto p = 0; p < 160; ++p) {
        global_pool.add(&Test2::spawner, &t2, prime_numbers[p]);
      }
    }

    global_pool.wait_all();

    std::atomic<int> total=0;
    for (int i = 0; i < JOB_COUNT; ++i) {
      for (auto p = 0; p < 160; ++p) {
        auto v = prime_numbers[p];
        //fmt::print("2.spawner:{}\n", v);
        for (auto j = 0; j < 3; ++j) {
          //fmt::print("2.inc:{}\n", v);
          total += v;
          total += v;
        }
      }
    }

    std::cout << "test2.total:" << t2.total << std::endl;
    std::cout << "      total:" <<    total << std::endl;

    EXPECT_EQ(t2.total, total);
  }
}

TEST_F(GTest1, interface) {
  for (int n = 1; n < 5; n = n + 2) {
    total = 0;

    Lbench      bb("task.THREAD_POOL_pool" + std::to_string(n));
    Thread_pool pool(n);
    const int   JOB_COUNT = 2000000;

    Test1 t1;
    t1.a = 0;

    pool.add(&Test1::inc_a, &t1, 3);

    for (int i = 0; i < JOB_COUNT; ++i) {
      pool.add(mywork, 1);
    }

    pool.wait_all();
    std::cout << "n:" << n << " finished total:" << total << std::endl;
    std::cout << "test1.a:" << t1.a << std::endl;

    EXPECT_EQ(t1.a, 3);
    EXPECT_EQ(total, JOB_COUNT);
  }
}

TEST_F(GTest1, bench) {
  {
    Lbench bb("task.THREAD_POOL_mpmc");

    mpmc<int> queue(256);

    for (int i = 0; i < 10000000; ++i) {
      queue.enqueue(i);
      int a;
      queue.dequeue(a);
      assert(a == i);
    }
  }
  {
    Lbench bb("task.THREAD_POOL_spmc");

    spmc256<int> queue;

    for (int i = 0; i < 10000000; ++i) {
      queue.enqueue(i);
      int a;
      queue.dequeue(a);
      assert(a == i);
    }
  }
  {
    Lbench                           bb("task.THREAD_POOL_concurrentqueue");
    moodycamel::ConcurrentQueue<int> queue(256);

    for (int i = 0; i < 10000000; ++i) {
      queue.enqueue(i);
      int a;
      queue.try_dequeue(a);
      assert(a == i);
    }
  }
}
