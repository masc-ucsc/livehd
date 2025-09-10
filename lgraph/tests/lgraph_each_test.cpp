//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <format>
#include <iostream>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lgraph.hpp"
#include "lrand.hpp"
#include "node.hpp"
#include "node_pin.hpp"

unsigned int rseed = 123;

class Setup_graphs_test : public ::testing::Test {
protected:
  Lgraph *top  = 0;
  Lgraph *c1   = 0;  // Child 1
  Lgraph *c3   = 0;  // Child 2
  Lgraph *c2   = 0;  // Child 3
  Lgraph *gc11 = 0;  // Grand Child from 1, 1st
  Lgraph *gc31 = 0;  // Grand child from 3, 1st
  Lgraph *gc32 = 0;  // Grand child from 3, 2nd
  Lgraph *top2 = 0;

  absl::flat_hash_map<std::string, int> children;

  void add_child(Lgraph *parent, Lgraph *child, std::string_view iname, bool randomize) {
    Node node;

    if (child) {
      auto str = absl::StrCat(parent->get_name(), ":", child->get_name());
      children[str]++;

      if (rand_r(&rseed) & 1) {  // Should be the same because the lgraph is already created
        node = parent->create_node_sub(child->get_lgid());
      } else {
        node = parent->create_node_sub(child->get_name());
      }

    } else {
      children[absl::StrCat(parent->get_name(), ":", iname)]++;

      node = parent->create_node_sub(iname);

      auto       *sub        = node.ref_type_sub_node();
      const auto &parent_sub = parent->get_self_sub_node();

      // Match parent names in tmap
      for (const auto &io_pin : parent_sub.get_io_pins()) {
        if (io_pin.dir == Sub_node::Direction::Input) {
          if (!sub->has_pin(io_pin.name)) {
            sub->add_input_pin(io_pin.name);
            auto dpin = parent->get_graph_input(io_pin.name);
            dpin.connect_sink(node.setup_sink_pin(io_pin.name));
          }

        } else if (io_pin.dir == Sub_node::Direction::Output) {
          if (!sub->has_pin(io_pin.name)) {
            auto spin = parent->get_graph_output(io_pin.name);
            if (!spin.get_node().has_inputs()) {
              sub->add_output_pin(io_pin.name);
              node.setup_driver_pin(io_pin.name).connect_sink(spin);
            }
          }
        } else if (io_pin.dir == Sub_node::Direction::Invalid) {
        } else {
          I(false);                                // For Lgraph sub there should be no undefined iopins
          I(io_pin.graph_io_pos != Port_invalid);  // graph_io_pos must be defined too
        }
      }
      node.set_type_sub(sub->get_lgid());
    }

    if (rand_r(&rseed) & 1 || !randomize) {
      node.set_name(iname);
    }
  }

  void add_io(Lgraph *g) {
    int inps = rand_r(&rseed) % 4;  // 0..3 inputs
    int pos  = 0;
    for (int j = 0; j < inps; j++) {
      std::string io_name("i" + std::to_string(j));

      g->add_graph_input(io_name, pos++, rand_r(&rseed) & 15);
    }
    inps = rand_r(&rseed) % 5;  // 0..4 outputs
    for (int j = 0; j < inps; j++) {
      std::string io_name("o" + std::to_string(j));
      g->add_graph_output(io_name, pos++, rand_r(&rseed) & 15);
    }
  }

  std::vector<Lgraph *> lgs;

  void SetUp() override {
    auto *lib = Graph_library::instance("lgdb_lg_each");
    top       = lib->create_lgraph("top", "nosource");
    ASSERT_NE(top, nullptr);
    c1 = lib->create_lgraph("c1", "nosource");
    ASSERT_NE(c1, nullptr);
    c2 = lib->create_lgraph("c2", "nosource");
    ASSERT_NE(c2, nullptr);
    c3 = lib->create_lgraph("c3", "nosource");
    ASSERT_NE(c3, nullptr);
    gc11 = lib->create_lgraph("gc11", "nosource");
    ASSERT_NE(gc11, nullptr);
    gc31 = lib->create_lgraph("gc31", "nosource");
    ASSERT_NE(gc31, nullptr);
    gc32 = lib->create_lgraph("gc32", "nosource");
    ASSERT_NE(gc32, nullptr);
    top2 = lib->create_lgraph("top2", "nosource");
    ASSERT_NE(top2, nullptr);

    lgs.push_back(top);
    lgs.push_back(c1);
    lgs.push_back(c2);
    lgs.push_back(c3);
    lgs.push_back(gc11);
    lgs.push_back(gc31);
    lgs.push_back(gc32);
    lgs.push_back(top2);

    for (auto &lg : lgs) {
      add_io(lg);
    }

    static bool randomize = false;

    std::print("lgraph_each random instance name {}\n", randomize ? "set" : "not set");

    add_child(top, c1, "ti1", randomize);
    add_child(top, c2, "ti2a", randomize);
    add_child(top, c2, "ti2b", randomize);  // 2 instances of c2 in top

    add_child(top, nullptr, "tmap1", randomize);

    add_child(c1, gc11, "ci1_11a", randomize);
    add_child(c1, nullptr, "tmap1", randomize);
    add_child(c1, nullptr, "tmap2", randomize);
    add_child(c1, gc11, "ci1_11b", randomize);

    add_child(c3, gc31, "ci3_31", randomize);
    add_child(c3, gc32, "ci3_32a", randomize);
    add_child(c3, gc32, "ci3_32b", randomize);

    add_child(top2, c1, "xi1", randomize);
    add_child(top2, c2, "xi2", randomize);
    add_child(top2, c3, "xi3", randomize);

    Lrand_range<size_t> rnd(0, lgs.size());
    Lrand_range<size_t> rnd_cells(0, 20);

    for (int i = 0; i < 30; ++i) {
      std::string lg_name{"lg_name"};
      lg_name += std::to_string(i);

      auto *lg = lib->create_lgraph(lg_name, "nosource");
      add_io(lg);

      for (int j = rnd_cells.any(); j > 0; --j) {
        std::string i_name(lg_name + "_cell_" + std::to_string(j));
        add_child(lg, nullptr, i_name, randomize);
      }

      for (int j = 0; j < 2; ++j) {
        std::string i_name(lg_name + "_" + std::to_string(j));
        auto       *parent_lg = lgs[rnd.any()];
        add_child(parent_lg, lg, i_name, randomize);
      }
    }

    randomize = !randomize;
  }

  void TearDown() override {
    for (auto *lg : lgs) {
      delete lg;
    }
    lgs.clear();
  }
};

TEST_F(Setup_graphs_test, each_local_sub) {
  absl::flat_hash_map<std::string, int> children2;

  for (auto &parent : lgs) {
    std::print("checking parent:{}\n", parent->get_name());
    parent->each_local_sub_fast([parent, &children2, this](Node &node, Lg_type_id lgid) {
      (void)lgid;
      Lgraph *child = parent->ref_library()->open_lgraph(node.get_type_sub());
      if (child == nullptr) {
        return;
      }

      ASSERT_NE(child, nullptr);

      std::string iname("NONAME");
      if (node.has_name()) {
        iname = node.get_name();
      }

      std::print("parent:{} child:{} iname:{}\n", parent->get_name(), child->get_name(), iname);

      EXPECT_TRUE(children.find(absl::StrCat(parent->get_name(), ":", child->get_name())) != children.end());

      auto id = absl::StrCat(parent->get_name(), ":", child->get_name());
      if (children2.find(id) == children2.end()) {
        children2[id] = 1;
      } else {
        children2[id]++;
      }
    });
  }

  for (const auto &c : children) {
    if (str_tools::contains(c.first, "tmap")) {
      EXPECT_EQ(c.second, 1);
    } else if (str_tools::contains(c.first, "cell")) {
      EXPECT_NE(c.second, children2[c.first]);
    } else {
      EXPECT_EQ(c.second, children2[c.first]);
    }
  }
  for (const auto &c : children2) {
    if (str_tools::contains(c.first, "tmap")) {
      EXPECT_EQ(c.second, 1);
    } else if (str_tools::contains(c.first, "cell")) {
      EXPECT_NE(c.second, children[c.first]);
    } else {
      EXPECT_EQ(c.second, children[c.first]);
    }
  }
}

TEST_F(Setup_graphs_test, each_local_sub_twice) {
  absl::flat_hash_map<std::string, int> children2;

  for (auto &parent : lgs) {
    std::print("checking parent:{}\n", parent->get_name());
    parent->each_local_sub_fast([parent, &children2, this](Node &node, Lg_type_id lgid) {
      (void)lgid;
      Lgraph *child = parent->ref_library()->open_lgraph(node.get_type_sub());
      if (child == nullptr) {
        return;
      }

      ASSERT_NE(child, nullptr);

      EXPECT_TRUE(children.find(absl::StrCat(parent->get_name(), ":", child->get_name())) != children.end());

      auto id = absl::StrCat(parent->get_name(), ":", child->get_name());

      std::string iname("NONAME");
      if (node.has_name()) {
        iname = node.get_name();
      }
      std::print("parent:{} child:{} iname:{} id:{}\n", parent->get_name(), child->get_name(), iname, id);

      if (children2.find(id) == children2.end()) {
        children2[id] = 1;
      } else {
        children2[id]++;
      }
    });
  }

  for (auto &c : children) {
    if (str_tools::contains(c.first, "tmap")) {
      EXPECT_EQ(c.second, 1);
    } else if (str_tools::contains(c.first, "cell")) {
      EXPECT_NE(c.second, children2[c.first]);
    } else {
      EXPECT_EQ(c.second, children2[c.first]);
    }
  }
  for (auto &c : children2) {
    if (str_tools::contains(c.first, "tmap")) {
      EXPECT_EQ(c.second, 1);
    } else if (str_tools::contains(c.first, "cell")) {
      EXPECT_NE(c.second, children[c.first]);
    } else {
      EXPECT_EQ(c.second, children[c.first]);
    }
  }
}

TEST_F(Setup_graphs_test, hierarchy) {
  for (auto &parent : lgs) {
    std::print("hierarchy for name:{} lgid:{}\n", parent->get_name(), parent->get_lgid());
    parent->each_local_sub_fast([](Node &node, Lg_type_id lgid) { std::print("  {} {}\n", node.get_name(), lgid); });
  }

  EXPECT_TRUE(true);
}

TEST_F(Setup_graphs_test, No_each_input) {
  for (auto &parent : lgs) {
    parent->each_graph_input([](const Node_pin &pin) {
      EXPECT_TRUE(pin.is_graph_input());
      EXPECT_TRUE(!pin.is_graph_output());
      EXPECT_FALSE(pin.get_node().has_inputs());
    });
  }

  EXPECT_TRUE(true);
}

TEST_F(Setup_graphs_test, No_each_output) {
  for (auto &parent : lgs) {
    parent->each_graph_output([](const Node_pin &pin) {
      EXPECT_TRUE(!pin.is_graph_input());
      EXPECT_TRUE(pin.is_graph_output());
      EXPECT_FALSE(pin.get_node().has_outputs());
    });
  }

  EXPECT_TRUE(true);
}

TEST_F(Setup_graphs_test, each_unique_hier_sub_parallel) {
  std::map<Lg_type_id, int> to_pos;

  std::vector<Lgraph *> all_lgs;

  // to_pos[top->get_lgid()] = 0;
  top->each_hier_unique_sub_bottom_up([&to_pos, &all_lgs](Lgraph *lg) -> bool {
    // std::print("adding name:{} lgid:{}\n", lg->get_name(), lg->get_lgid());

    EXPECT_TRUE(to_pos.find(lg->get_lgid()) == to_pos.end());

    to_pos[lg->get_lgid()] = all_lgs.size();
    all_lgs.emplace_back(lg);

    return true;
  });

  std::vector<int> all_visited;  // Only monotonic st and ld operator, so it is atomic
  all_visited.resize(all_lgs.size());

  // top->get_htree().dump();

  top->each_hier_unique_sub_bottom_up([&to_pos, &all_visited](Lgraph *lg) -> bool {
    // std::print("1a.checking name:{} lgid:{}\n", lg->get_name(), lg->get_lgid());
    bool sure_leaf = lg->get_down_class_map().empty();

    I(to_pos.find(lg->get_lgid()) != to_pos.end());
    auto pos = to_pos[lg->get_lgid()];

    EXPECT_EQ(all_visited[pos], 0);  // no double insertions
    all_visited[pos] = 1;
    EXPECT_EQ(all_visited[pos], 1);  // no double insertions

    lg->each_local_unique_sub_fast([&all_visited, &to_pos, &sure_leaf](Lgraph *sub_lg) -> bool {
      // std::print("1b.checking name:{} lgid:{}\n", sub_lg->get_name(), sub_lg->get_lgid());

      EXPECT_FALSE(sure_leaf);  // A leaf should not have sub nodes
      I(to_pos.find(sub_lg->get_lgid()) != to_pos.end());
      auto pos = to_pos[sub_lg->get_lgid()];

      EXPECT_EQ(all_visited[pos], 1);  // already visited

      return true;  // continue
    });

    return true;  // continue
  });

  all_visited.clear();
  all_visited.resize(all_lgs.size());
  top->each_hier_unique_sub_bottom_up_parallel2([&to_pos, &all_visited](Lgraph *lg) -> bool {
    // std::print("2a.checking name:{} lgid:{}\n", lg->get_name(), lg->get_lgid());
    bool sure_leaf = lg->get_down_class_map().empty();

    I(to_pos.find(lg->get_lgid()) != to_pos.end());
    auto pos = to_pos[lg->get_lgid()];

    EXPECT_EQ(all_visited[pos], 0);  // no double insertions
    all_visited[pos] = 1;
    EXPECT_EQ(all_visited[pos], 1);  // no double insertions

    lg->each_local_unique_sub_fast([&all_visited, &to_pos, &sure_leaf](Lgraph *sub_lg) -> bool {
      // std::print("2b.checking name:{} lgid:{}\n", sub_lg->get_name(), sub_lg->get_lgid());

      EXPECT_FALSE(sure_leaf);  // A leaf should not have sub nodes
      I(to_pos.find(sub_lg->get_lgid()) != to_pos.end());
      auto pos = to_pos[sub_lg->get_lgid()];

      if (sub_lg->is_empty()) {
        if (all_visited[pos] != 0) {
          std::print("OOPS: checking name:{} lgid:{}\n", sub_lg->get_name(), sub_lg->get_lgid());
          sub_lg->dump();
        }

        EXPECT_EQ(all_visited[pos], 0);  // not visited
      } else {
        EXPECT_EQ(all_visited[pos], 1);  // already visited
      }

      return true;  // continue
    });

    return true;  // continue
  });
}
