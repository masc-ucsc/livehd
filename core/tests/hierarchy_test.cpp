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
    bool leaf;
  };

  Tree<Node_data> tree;

  void populate_tree(int rseed, const int max_depth, const int size, const double leaf_ratio_goal) {

    tree.clear();

    Node_data root_data;
    root_data.create_pos = 0;

    tree.set_root(root_data);

    I(max_depth>1);

    Rng rint(rseed);
    RandomBool rbool;


    int max_level=1;
    int n_leafs = 0;
    for(int i=0;i<size;++i) {
      int level = 0;
      level = rint.uniform<int>(max_level);
      I(level<max_depth);

      Tree_index index(level, rint.uniform<int>(tree.get_tree_width(level)));

      Node_data data;
      data.create_pos = i+1;
      data.fwd_pos = -1;
      data.bwd_pos = -1;

      double leaf_ratio = (double)n_leafs/(1.0 + i);

      //fmt::print("leaf_ratio:{} {} {}\n", leaf_ratio,n_leafs, i);

      if (leaf_ratio < leaf_ratio_goal && index.level) { // Not to root
        tree.add_younger_sibling(index, data);
        n_leafs++;
      }else{
        //index.pos = tree.get_tree_width(index.level)-1; // Add child at the end
        if (!tree.is_leaf(index))
          n_leafs++;

        tree.add_child(index, data);
        if ((index.level+1) == max_level && max_level<max_depth)
          max_level++;
        I(max_level<=max_depth);
      }
    }

    int pos = 0;
    n_leafs = 0;
    for (auto index : tree.depth_preorder()) {
      auto &data = tree.get_data(index);
      data.fwd_pos = pos;
      data.bwd_pos = size-pos;
      data.leaf    = tree.is_leaf(index);
      if (data.leaf)
        n_leafs++;

      ++pos;
    }

    fmt::print("Tree with {} nodes {} leafs and {} depth\n", size, n_leafs, max_level);

    EXPECT_TRUE(pos == (size+1)); // Missing nodes??? (tree.hpp bug)
  }

  void SetUp() override {
    int rseed=123;
    populate_tree(rseed, 10, 100, 0.5);
  }

  void TearDown() override {
  }
};

TEST_F(Setup_hierarchy, setup_tree) {

  for(auto index:tree.depth_preorder()) {
    const auto &data = tree.get_data(index);
    fmt::print(" level:{} pos:{} create_pos:{} fwd:{} bwd:{} leaf:{}\n", index.level, index.pos, data.create_pos, data.fwd_pos, data.bwd_pos, data.leaf);
  }

  EXPECT_TRUE(true);
}

