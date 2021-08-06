//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "thread_pool.hpp"

#include <iostream>

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

TEST_F(GTest1, interface) {
  for (int n = 1; n < 5; n = n + 2) {
    total = 0;

    Lbench      bb("task.THREAD_POOL_pool" + std::to_string(n));
    Thread_pool pool(n);
    const int   JOB_COUNT = 2000000;

    Test1 t1;
    t1.a = 0;

    pool.add(&Test1::inc_a, t1, 3);

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
