

#include <iostream>

#include "gtest/gtest.h"

#include "lbench.hpp"
#include "spmc.hpp"
#include "mpmc.hpp"
#include "thread_pool.hpp"
#include "concurrentqueue.hpp"

int control = 1023;

struct Test1 {
  int  a;
  void inc_a(int v) {
    a += v;
    std::cout << "Test1.inc_a:" << a << std::endl;
  }
};

std::atomic<int> total;
void             mywork(int a) {
  total += a;
}

class GTest1 : public ::testing::Test {
protected:
  void SetUp() override {
  }
};

TEST_F(GTest1, interface) {

  total = 0;

  Thread_pool pool;
  const int JOB_COUNT = 2000000;

  Test1 t1;
  t1.a = 0;

  pool.add(&Test1::inc_a, t1, 3);

  for(int i = 0; i < JOB_COUNT; ++i) {
    pool.add(mywork, 1);
  }

  pool.wait_all();
  std::cout << "finished total:" << total << std::endl;
  std::cout << "test1.a:" << t1.a << std::endl;

  EXPECT_EQ(t1.a, 3);
  EXPECT_EQ(total, JOB_COUNT);
}

TEST_F(GTest1, bench) {
  {
    Lbench bb("mpmc");

    mpmc<int> queue(256);

    for (int i = 0; i < 10000000; ++i) {
      queue.enqueue(i);
      int a;
      queue.dequeue(a);
      assert(a == i);
    }
  }
  {
    Lbench bb("spmc");

    spmc256<int> queue;

    for (int i = 0; i < 10000000; ++i) {
      queue.enqueue(i);
      int a;
      queue.dequeue(a);
      assert(a == i);
    }
  }
  {
    Lbench bb("concurrentqueue");
    moodycamel::ConcurrentQueue<int> queue(256);

    for (int i = 0; i < 10000000; ++i) {
      queue.enqueue(i);
      int a;
      queue.try_dequeue(a);
      assert(a == i);
    }
  }
}

