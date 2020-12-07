//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <vector>
#include <string>

#include "lbench.hpp"
#include "lrand.hpp"

#include "graph_core.hpp"

using testing::HasSubstr;
using namespace std;

class Setup_graph_core : public ::testing::Test {
protected:

  void SetUp() override {
  }

  void TearDown() override {
    //Graph_library::sync_all();
  }
};

TEST_F(Setup_graph_core, shallow_tree) {
  fmt::print("\n\n");

  Graph_core c1("lgdb_gc","shallow_tree");

  for (int i = 0; i < 100; i++)
  {
    Index_ID id = c1.create_master_root(0);
  }

  for (int i = 3; i < 30; i++) {
    c1.add_edge(0, i);
  }

  for (int i = 0; i < 40; i++){
    c1.add_edge(i,i + 1);
    c1.add_edge(i,i + 2);
  }

  for (int i = 0; i < c1.table.size(); i++) {
    cout << "BIG BOLD LETTERS --- " << i << endl;
    std::vector<Index_ID> edges = c1.get_edges(i);
    for (Index_ID e : edges) {
      cout << i << ": " << e << endl;
    }
  }


  c1.del(0);
  c1.del(1);
  cout << "DEL\n" << endl;



  for (int i = 0; i < c1.table.size(); i++){
    cout << "BIG BOLD LETTERS --- " << i << endl;
    std::vector<Index_ID> edges = c1.get_edges(i);
    for (Index_ID e : edges) {
      cout << i << ": " << e << endl;
    }
  }


  /*
  //c1.table[0].edge_storage_or_pid_bits = 10;

  //cout << c1.table[0].edge_storage_or_pid_bits << endl;
  /*
  c1.table[1].edge_storage_or_pid_bits = 11;
  c1.table[2].edge_storage_or_pid_bits = 12;
  c1.table[3].edge_storage_or_pid_bits = 13;
  */

  fmt::print("\n\n");
}

