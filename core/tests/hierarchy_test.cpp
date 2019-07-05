//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <vector>
#include <string>

#include "rng.hpp"
#include "tree.hpp"

#include "lgraph.hpp"

using testing::HasSubstr;

class Setup_hierarchy : public ::testing::Test {
protected:
  struct Node_data {
    int create_pos;
    int fwd_pos;
    int bwd_pos;
  };

  Tree<Node_data> tree;

  void populate_tree(int rseed, int max_depth, int size) {

    tree.clear();


    Node_data root_data;
    root_data.create_pos = 0;


    tree.set_root(root_data);

    I(max_depth>1);

    Rng rint(rseed);
    RandomBool rbool;

    int max_level=1;
    for(int i=0;i<size;++i) {
      int level = 0;
      level = rint.uniform<int>(max_level);
      I(level<max_depth);

      Tree_index index(level, rint.uniform<int>(tree.get_tree_width(level)));

      Node_data data;
      data.create_pos = i+1;
      data.fwd_pos = -1;
      data.bwd_pos = -1;


      if (rbool(rint) && index.pos) {
        tree.add_sibling(index, data);
      }else{
        //index.pos = tree.get_tree_width(index.level)-1; // Add child at the end
        tree.add_child(index, data);
        if ((index.level+1) == max_level && max_level<max_depth)
          max_level++;
        I(max_level<=max_depth);
      }
    }
  }

  void SetUp() override {
    int rseed=123;
    populate_tree(rseed, 100, 10000);
  }

  void TearDown() override {
  }
};

TEST_F(Setup_hierarchy, setup_tree) {

  for(auto index:tree.depth_preorder()) {
    const auto &data = tree.get_data(index);
    fmt::print(" level:{} pos:{} create_pos:{}\n", index.level, index.pos, data.create_pos);
  }

  EXPECT_TRUE(true);
}

