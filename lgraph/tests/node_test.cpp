//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <format>
#include <iostream>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

unsigned int rseed = 123;

class Setup_graphs_test : public ::testing::Test {
protected:
  Lgraph *top = 0;
  Lgraph *c1  = 0;
  Lgraph *c2  = 0;

  absl::flat_hash_map<std::string, int> children;

  std::vector<Lgraph *> lgs;

  void SetUp() override {
    auto *lib = Graph_library::instance("lgdb_node_test");
    I(lib);

    top = lib->create_lgraph("top", "nosource");
    ASSERT_NE(top, nullptr);
    c1 = lib->create_lgraph("c1", "nosource");
    ASSERT_NE(c1, nullptr);
    c2 = lib->create_lgraph("c2", "nosource");
    ASSERT_NE(c2, nullptr);

    //---------------------------------------------------
    // Create graphs input/outputs
    auto top_a = top->add_graph_input("a", 1, 10);
    auto top_b = top->add_graph_input("b", 3, 10);
    top_b.set_offset(3);
    auto top_z      = top->add_graph_output("z", 5, 1);
    auto top_y      = top->add_graph_output("Y", 7, 10);
    auto top_s2_out = top->add_graph_output("s2_out", 11, 1);

    auto c1_aaa = c1->add_graph_input("an_input", 13, 10);
    auto c1_sss = c1->add_graph_output("s1_output", 17, 1);

    auto c2_aaa = c2->add_graph_input("a1", 19, 10);
    auto c2_bbb = c2->add_graph_input("anotherinput", 29, 10);
    auto c2_sss = c2->add_graph_output("Y", 30001, 1);

    //---------------------------------------------------
    // populate top graph with cells and instances

    auto s1  = top->create_node_sub(c1->get_lgid());
    auto s2  = top->create_node_sub("c2");
    auto sum = top->create_node(Ntype_op::Sum);
    auto mux = top->create_node(Ntype_op::Mux);
    auto mor = top->create_node(Ntype_op::Xor);  // cell called mor to avoid xor reserved keyword

    auto s1_aaa = s1.setup_sink_pin("an_input");
    auto s1_sss = s1.setup_driver_pin("s1_output");

    I(s1_aaa.get_pid() == c1_aaa.get_pid());
    I(s1_sss.get_pid() == c1_sss.get_pid());

    auto s2_aaa = s2.setup_sink_pin("a1");
    auto s2_bbb = s2.setup_sink_pin("anotherinput");
    auto s2_sss = s2.setup_driver_pin("Y");
    I(s2_aaa.get_pid() == c2_aaa.get_pid());
    I(s2_bbb.get_pid() == c2_bbb.get_pid());
    I(s2_sss.get_pid() == c2_sss.get_pid());

    auto sum_a = sum.setup_sink_pin("A");
    auto sum_b = sum.setup_sink_pin("B");
    auto sum_y = sum.setup_driver_pin("Y");

    auto mor_a = mor.setup_sink_pin("A");
    auto mor_y = mor.setup_driver_pin("Y");

    auto mux_a = mux.setup_sink_pin("1");
    auto mux_b = mux.setup_sink_pin("2");
    auto mux_s = mux.setup_sink_pin("0");
    auto mux_y = mux.setup_driver_pin("Y");

    top_a.connect_sink(sum_a);
    top_a.set_bits(10);
    // top->add_edge(top_a, sum_a, 10);
    sum_b.connect_driver(top_b);
    top_b.set_bits(10);
    // top->add_edge(top_b, sum_b, 10);

    top->add_edge(top_a, mor_a, 10);
    top->add_edge(top_b, mor_a, 10);

    top->add_edge(top_a, s1_aaa, 10);

    top->add_edge(top_a, s2_aaa, 10);
    top->add_edge(s1_sss, s2_bbb, 1);
    top->add_edge(s1_sss, top_z, 1);

    top->add_edge(sum_y, mux_b, 10);
    top->add_edge(s2_sss, mux_s, 1);
    top->add_edge(mor_y, mux_a, 10);

    top->add_edge(mux_y, top_y, 10);
    top->add_edge(s2_sss, top_s2_out, 1);
  }
};

TEST_F(Setup_graphs_test, iterate_sub_graph) {
  for (const auto &node : top->forward()) {
    for (const auto &out_edge : node.out_edges()) {
      auto dpin = out_edge.driver;
      auto spin = out_edge.sink;
      std::print("name:{} pid:{} -> name:{} pid:{}\n", dpin.debug_name(), dpin.get_pid(), spin.debug_name(), spin.get_pid());
    }
  }

  EXPECT_TRUE(true);
}

TEST_F(Setup_graphs_test, annotate1a) {
  for (const auto &node : top->forward()) {
    EXPECT_FALSE(node.has_place());
    // EXPECT_DEATH({ node.get_place().get_x(); }, "Assertion.*failed");  // get_place for something not set, triggers failure
  }
}

TEST_F(Setup_graphs_test, annotate1b) {
  for (auto node : top->forward()) {
    EXPECT_TRUE(!node.has_place());
    Ann_place p;
    node.set_place(p);
    EXPECT_EQ(node.get_place().get_x(), 0);
    EXPECT_TRUE(node.has_place());
  }
  for (const auto &node : top->forward()) {
    EXPECT_EQ(node.get_place().get_x(), 0);  // Now, OK, ref passes referene or allocates
  }
}

TEST_F(Setup_graphs_test, annotated) {
  for (const auto &node : top->forward()) {
    for (const auto &out_edge : node.out_edges()) {
      auto dpin = out_edge.driver;
      if (!dpin.get_node().has_place()) {
        Ann_place p;
        dpin.get_node().set_place(p);
      }

      EXPECT_EQ(dpin.get_node().get_place().get_x(), 0);
      EXPECT_EQ(dpin.get_node().get_place().get_y(), 0);
      if (dpin.has_name() && dpin.get_name() == "b") {
        EXPECT_EQ(dpin.get_offset(), 3);
      } else {
        EXPECT_EQ(dpin.get_offset(), 0);
      }
    }
  }

  int x_val = 0;
  int y_val = 0;
  for (auto node : top->forward()) {
    x_val++;
    y_val += 3;

    auto place1 = node.get_place();
    place1.replace(x_val, y_val, 0, 0);
    EXPECT_EQ(place1.get_x(), x_val);
    node.set_place(place1);

    auto place2 = node.get_place();
    EXPECT_EQ(place2.get_x(), x_val);
    node.set_place(place2);

    auto place3 = node.get_place();
    EXPECT_EQ(place3.get_x(), x_val);

    auto place4 = node.get_place();
    EXPECT_EQ(place4.get_x(), x_val);
    EXPECT_EQ(place4, place2);
  }

  for (auto node : top->backward()) {
    EXPECT_TRUE(node.has_place());
  }

  EXPECT_TRUE(true);
}

TEST_F(Setup_graphs_test, annotate2) {
  absl::flat_hash_map<Node_pin::Compact_class, int> my_map2;

  int total = 0;
  for (const auto &node : top->forward()) {
    for (const auto &e : node.out_edges()) {
      my_map2[e.driver.get_compact_class()] = total;
      total++;
    }
  }

  std::vector<bool> used(total);
  used.clear();
  used.resize(total);

  for (const auto &it : my_map2) {
    auto dpin = Node_pin(top, it.first);
    EXPECT_TRUE(dpin.is_driver());
    EXPECT_FALSE(used[it.second]);
    used[it.second] = true;
  }
}

TEST_F(Setup_graphs_test, annotate2_hier) {
  absl::flat_hash_map<Node_pin::Compact, int> my_map2;

  int total = 0;
  for (const auto &node : top->forward(true)) {
    for (const auto &e : node.out_edges()) {
      my_map2[e.driver.get_compact()] = total;
      total++;
    }
  }

  std::vector<bool> used(total);
  used.clear();
  used.resize(total);

  for (const auto &it : my_map2) {
    auto dpin = Node_pin(top, it.first);
    EXPECT_TRUE(dpin.is_driver());
    EXPECT_FALSE(used[it.second]);
    used[it.second] = true;
  }
}

TEST_F(Setup_graphs_test, cell_trivials) {
#ifdef NDEBUG
  // costexpr propagates only when OPT is enabled
  constexpr auto pid = Ntype::get_sink_pid(Ntype_op::Sum, "B");
  static_assert(pid == 1);
#endif

  auto pid2 = Ntype::get_sink_pid(Ntype_op::Mux, "321");
  EXPECT_EQ(pid2, 321);
}
