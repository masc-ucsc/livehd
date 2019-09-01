//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <vector>
#include <string>

#include "rng.hpp"
#include "tree.hpp"

#include "attribute.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

#include "tree_lgdb_setup.hpp"

using testing::HasSubstr;

class Setup_hierarchy : public Tree_lgdb_setup {
protected:
  void check_lgraph_fwd(int rseed) {
    I(lg_root);

    int pos = 1;
    std::vector<std::string> full_fwd_order;
    full_fwd_order.resize(absl_fwd_pos.size());

    for (auto &node : node_order) {
      if (node.is_type_io())
        continue;
      const auto &fwd_pos = Fwd_pos_attr::ref(lg_root)->get(node.get_compact());
      I(fwd_pos<=full_fwd_order.size());
      I(full_fwd_order[fwd_pos-1].empty());
      I(fwd_pos>0);

      std::string txt = fmt::format("name:{} lg:{}",node.get_name(), node.get_class_lgraph()->get_name());
      full_fwd_order[fwd_pos-1] = txt;
      pos++;
    }
    // FIXME: add test so that a node is not visited twice

    tree.dump();

    std::vector<std::string> fwd_order;
    for(size_t i=0;i<full_fwd_order.size();++i) {
      fmt::print("full_fwd_order: {}\n",full_fwd_order[i]);
      if (full_fwd_order[i].rfind("name:leaf")!=std::string::npos)
        fwd_order.emplace_back(full_fwd_order[i]);
    }

    pos = 1;
    std::vector<std::string> iterator_order;
    for(auto node:lg_root->forward(true)) {
      if (node.is_type_io()) {
        continue;
      }
#if 0
      const auto &fwd_pos = Fwd_pos_attr::ref(lg_root)->get(node.get_compact());
      const auto &bwd_pos = Bwd_pos_attr::ref(lg_root)->get(node.get_compact());

      EXPECT_NE(absl_fwd_pos.find(node.get_compact()), absl_fwd_pos.end());
      EXPECT_NE(absl_bwd_pos.find(node.get_compact()), absl_bwd_pos.end());

      EXPECT_EQ(fwd_pos,absl_fwd_pos[node.get_compact()]);
      EXPECT_EQ(bwd_pos,absl_bwd_pos[node.get_compact()]);

      fmt::print("pos:{} fwd_pos:{}\n",pos, fwd_pos);
#else
#endif
      std::string txt = fmt::format("name:{} lg:{}", node.get_name(), node.get_class_lgraph()->get_name());
      fmt::print("iterator_order: {}\n",txt);
      iterator_order.push_back(txt);

      //EXPECT_EQ(fwd_pos,pos);

      pos++;
    }

    EXPECT_EQ(iterator_order.size(), fwd_order.size());

    for(size_t i=0;i<fwd_order.size();++i) {
      //fmt::print("iter:{}  vs expected:{}\n",iterator_order[i], fwd_order[i]);
      EXPECT_EQ(iterator_order[i], fwd_order[i]);
    }
  }

  void SetUp() override {
  }

  void TearDown() override {
    //Graph_library::sync_all();
  }
};

TEST_F(Setup_hierarchy, check_attributes) {

  int rseed=123;
  populate_tree(rseed, 10, 100, 0.5, true);
  map_tree_to_lgraph(rseed);

  I(lg_root);

  for (auto &node : node_order) {
    const auto &fwd_pos = Fwd_pos_attr::ref(lg_root)->get(node.get_compact());
    const auto &bwd_pos = Bwd_pos_attr::ref(lg_root)->get(node.get_compact());

    EXPECT_NE(absl_fwd_pos.find(node.get_compact()), absl_fwd_pos.end());
    EXPECT_NE(absl_bwd_pos.find(node.get_compact()), absl_bwd_pos.end());

    EXPECT_EQ(fwd_pos,absl_fwd_pos[node.get_compact()]);
    EXPECT_EQ(bwd_pos,absl_bwd_pos[node.get_compact()]);
  }
}

TEST_F(Setup_hierarchy, simple_check_fwd) {

  int rseed=30;
  Rng rint(rseed);
  RandomBool rbool;

  populate_tree(rseed, 3, 12, 0.5, true);
  map_tree_to_lgraph(rseed);
  Graph_library::sync_all();

  check_lgraph_fwd(rseed);
}


TEST_F(Setup_hierarchy, check_fwd) {

  return;

  int rseed=30;
  Rng rint(rseed);
  RandomBool rbool;

  for(int nlevels=3;nlevels<12;nlevels+=rint.uniform<int>(4)+1) {
    for(int i=0;i<3;i++) {
      rseed++;
      populate_tree(rseed, nlevels, 8+rint.uniform<int>(nlevels*30*(1+i)), 0.5, true);
      map_tree_to_lgraph(rseed);

      check_lgraph_fwd(rseed);
    }
  }
}

