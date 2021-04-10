//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "graph_core.hpp"

#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "graph_core_compress.hpp"
#include "gtest/gtest.h"
#include "lbench.hpp"
#include "lrand.hpp"

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

  // test functions create master root
  // do set s and gets
  // TEST now

  // create_master_root
}
