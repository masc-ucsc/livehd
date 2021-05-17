//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "graph_core.hpp"

#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "graph_core_compress.hpp"
#include "gtest/gtest.h"
#include "lbench.hpp"
#include "lrand.hpp"

#include <stdlib.h>
#include <time.h>
#include <unordered_map>
#include <iostream>


using namespace std;

using testing::HasSubstr;

class Setup_graph_core : public ::testing::Test {
protected:
  void SetUp() override {}

  void TearDown() override {
    // Graph_library::sync_all();
  }
};

TEST_F(Setup_graph_core, bench_compress) {
  uint32_t dest[8];

  Lrand<uint32_t> r;

  uint32_t *mem = (uint32_t *)alloca(8 * 32 * 10000);

  for (int i = 0; i < 32 * 10000; i = i + 31) {
    mem[i] = r.max(300);
  }

  {
    Lbench   b("core.GRAPH_CORE_bench_compress");
    uint32_t carry = 0;
    for (int j = 0; j < 1000; ++j) {
      for (int i = 0; i < 32 * 10000; i = i + 4) {
        __uint128_t *v = (__uint128_t *)(&mem[i]);
        expand_master_root_0(dest, *v, carry);
        carry += dest[7];
      }
    }

    fmt::print("carry {} for 80M calls\n", carry);
  }
}

TEST_F(Setup_graph_core, shallow_tree) {
  Lbench b("core.GRAPH_CORE_shallow_tree");

  Graph_core c1("lgdb_gc", "shallow_tree");

  unordered_map<int, int> testing_root;

  // I am testing master_root in this case
  // the uncommented line is the one that is testing is_master_root()
  // I am comparing is_master_root and 1 because we know it is a master root node
  // I have master root bit set to 1 indicating it is a master root
  // There is an inconsistency in the output of the test however

  // I wrote the second test for that reason which is testing set_master_root()
  // This function is also producing inconsistent output as well and I am not sure why
  // any help is much appreciated


///*
  for(int i = 0; i < 5; i++){
    auto instruction_type = (rand() % 100) + 1;
    auto root_id = c1.create_master_root(instruction_type);
    testing_root[root_id] = instruction_type;

    //EXPECT_EQ(c1.test_master_root(root_id), 1); // check if set to master_root properly
    EXPECT_EQ(c1.is_master_root(root_id), 1); // is it a master_root


    // need to get first two working before worrying about these
    //EXPECT_EQ(c1.get_pid(root_id), 0); // what is the pid of the node
    //EXPECT_EQ(instruction_type, c1.get_type(root_id)); //check if Index_id's match
    //EXPECT_EQ(root_id << 2, root_id * 4);
  }
//*/


/*
  unordered_map<int, int> testing_master;

  for(int i = 0; i < 5; i++){
    auto pid = (rand() % 300) + 1;
    auto instruction_type_master = (rand() % 100) + 1001;
    auto master_id = c1.create_master(instruction_type_master, pid);
    testing_master[master_id] = instruction_type_master;

    EXPECT_EQ(instruction_type_master, c1.get_type(master_id)); // check if Index_id's match up
    //EXPECT_EQ(c1.test_master_root(master_id), 0); // check if set to master properly
    //EXPECT_EQ(c1.is_master_root(master_id), false); // is it a master_root
    //EXPECT_EQ(c1.get_pid(master_id), pid); // what is the pid of the node
  }
//*/
}
