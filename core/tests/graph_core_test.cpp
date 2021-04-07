//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "graph_core_compress.hpp"
#include "gtest/gtest.h"
#include "lbench.hpp"
#include "lrand.hpp"

#include "graph_core.hpp"
#include "graph_core_compress.hpp"

#include <time.h>
#include <unordered_map>

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

   /* Use for loop to generate 200 master root
    * give each master root a type between 1 and 200
    * store the random number type in a map
    */

   unordered_map<int, int> testingMap;
   // uint8_t receiveRootType;

   // Port_ID testPID = 103;
   // Port_ID receiveMasterPID;

   for(int i = 0; i < 20; i++){
     auto instruction_type = rand() % 200 + 1;
     auto root_ID = c1.create_master_root(instruction_type);
     testingMap[root_ID] = instruction_type;
     EXPECT_EQ(testingMap[root_ID], root_ID);
   }// check if returning repeat master_root ID
   //receiveRootType = c1.get_type(masterRootID);



   //EXPECT_EQUAL
   // use testing map find to verify whether
   //if( testingMap.find(masterRootID) == c1.get_type(masterRootID)){
     //they are equal
   //}else{
     //there is a problem
   //}


   // masterID = c1.create_master(masterRootID,testPID);
   // receiveMasterPID = c1.getPID(masterRootID);

   // EXPECT_EQ to verify corectness
}
