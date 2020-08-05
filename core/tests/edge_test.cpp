//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "lbench.hpp"
#include "lrand.hpp"
#include "mmap_map.hpp"

using testing::HasSubstr;

class Edge_test : public ::testing::Test {
protected:
  LGraph *g;
  Node  n1;
  Node  n2;
  Sub_node *n1_sub;
  Sub_node *n2_sub;

  Lrand<bool>  rbool;
  Lrand<int>   rint;

  mmap_lib::map<std::string_view, Node_pin> track_n1_inp_setup_pins;
  mmap_lib::map<std::string_view, Node_pin> track_n2_inp_setup_pins;

  mmap_lib::map<std::string_view, Node_pin> track_n1_out_setup_pins;
  mmap_lib::map<std::string_view, Node_pin> track_n2_out_setup_pins;

  mmap_lib::map<XEdge::Compact, int> track_edge_count;

  mmap_lib::map<int,bool> n1_graph_pos_created;
  mmap_lib::map<int,bool> n2_graph_pos_created;

  void SetUp() override {
    g = LGraph::create("lgdb_edge_test", "test0", "test");

    n1 = g->create_node_sub("n1");
    n2 = g->create_node_sub("n2");

    n1_sub = g->ref_library()->ref_sub("n1");
    n2_sub = g->ref_library()->ref_sub("n2");

    n1_sub->reset_pins();
    n2_sub->reset_pins();

    n1_graph_pos_created.clear();
    n2_graph_pos_created.clear();
    g->sync();
  }

  void TearDown() override {
  }

  Port_ID get_free_n1_graph_pos() {
    Port_ID pos;
    do {
      pos = rint.max((1<<Port_bits)-1);
    }while(n1_graph_pos_created.find(pos) != n1_graph_pos_created.end());

    n1_graph_pos_created.set(pos, true);

    return pos;
  }

  Port_ID get_free_n2_graph_pos() {
    Port_ID pos;
    do {
      pos = rint.max((1<<Port_bits)-1);
    }while(n2_graph_pos_created.find(pos) != n2_graph_pos_created.end());

    n2_graph_pos_created.set(pos, true);

    return pos;
  }

  void check_setup_pins() {
    Graph_library::sync_all(); // To ease debug

    int n_hits;

    n_hits=0;
    for(auto &pin : n1.out_setup_pins()) {
      EXPECT_TRUE(!pin.is_invalid());
      EXPECT_TRUE(track_n1_out_setup_pins.has(pin.get_type_sub_pin_name()));
      n_hits++;
    }
    EXPECT_EQ(track_n1_out_setup_pins.size(), n_hits);

    n_hits=0;
    for(auto &pin : n2.out_setup_pins()) {
      EXPECT_TRUE(!pin.is_invalid());
      EXPECT_TRUE(track_n2_out_setup_pins.has(pin.get_type_sub_pin_name()));
      n_hits++;
    }
    EXPECT_EQ(track_n2_out_setup_pins.size(), n_hits);

    n_hits=0;
    for(auto &pin : n1.inp_setup_pins()) {
      EXPECT_TRUE(!pin.is_invalid());
      EXPECT_TRUE(track_n1_inp_setup_pins.has(pin.get_type_sub_pin_name()));
      n_hits++;
    }
    EXPECT_EQ(track_n1_inp_setup_pins.size(), n_hits);

    n_hits=0;
    for(auto &pin : n2.inp_setup_pins()) {
      EXPECT_TRUE(!pin.is_invalid());
      EXPECT_TRUE(track_n2_inp_setup_pins.has(pin.get_type_sub_pin_name()));
      n_hits++;
    }
    EXPECT_EQ(track_n2_inp_setup_pins.size(), n_hits);

  }

  Node_pin add_n1_setup_driver_pin(const std::string pname) {

    const auto &it = track_n1_out_setup_pins.find(pname);
    if (it == track_n1_out_setup_pins.end()) {
      EXPECT_FALSE(n1_sub->has_pin(pname));

      auto instance_pid = n1_sub->add_output_pin(pname, get_free_n1_graph_pos());

      auto dpin = n1.setup_driver_pin(pname);
      I(dpin.get_pid() == instance_pid);
      track_n1_out_setup_pins.set(pname, dpin);
      return dpin;
    }

    Node_pin dpin;
    if (rbool.any())
      dpin = n1.setup_driver_pin(pname);
    else
      dpin = n1.get_driver_pin(pname);
    EXPECT_TRUE( dpin.is_driver());
    EXPECT_TRUE(!dpin.is_sink());

    if (rbool.any())
      EXPECT_EQ(dpin.get_compact(), it->second.get_compact());
    else
      EXPECT_EQ(dpin, it->second);

    return dpin;
  }

  Node_pin add_n2_setup_sink_pin(const std::string pname) {

    const auto &it = track_n2_inp_setup_pins.find(pname);
    if (it == track_n2_inp_setup_pins.end()) {
      EXPECT_FALSE(n2_sub->has_pin(pname));
      auto instance_pid = n2_sub->add_input_pin(pname, get_free_n2_graph_pos());

      auto spin = n2.setup_sink_pin(pname);
      I(spin.get_pid() == instance_pid);
      track_n2_inp_setup_pins.set(pname, spin);
      return spin;
    }

    Node_pin spin;
    if (rbool.any())
      spin = n2.setup_sink_pin(pname);
    else
      spin = n2.setup_sink_pin(pname);
    EXPECT_TRUE(!spin.is_driver());
    EXPECT_TRUE( spin.is_sink());

    if (rbool.any())
      EXPECT_EQ(spin.get_compact(), it->second.get_compact());
    else
      EXPECT_EQ(spin, it->second);

    return spin;
  }

  void check_edges() {
    g->sync(); // For debug in graph_library.yaml

    for(auto e:n1.inp_edges()) {
      (void)e;
      EXPECT_TRUE(false);
    }
    for(auto e:n2.out_edges()) {
      (void)e;
      EXPECT_TRUE(false);
    }

    for(auto e:n1.out_edges()) {
      auto it = track_edge_count.find(e.get_compact());
      EXPECT_TRUE(it!=track_edge_count.end());
    }

    for(auto e:n2.inp_edges()) {
      auto it = track_edge_count.find(e.get_compact());
      EXPECT_TRUE(it!=track_edge_count.end());
    }

  }

  void add_edge(Node_pin dpin, Node_pin spin) {
    XEdge edge(dpin, spin);
    auto it = track_edge_count.find(edge.get_compact());
    if (spin.is_connected(dpin)) {
      EXPECT_TRUE(it != track_edge_count.end());
      return;
    }
    EXPECT_TRUE(it == track_edge_count.end());
    g->add_edge(dpin,spin);

    track_edge_count.set(edge.get_compact(),1);
  }

};

TEST_F(Edge_test, random_insert) {

  check_setup_pins();
  check_edges();

  auto dpin = add_n1_setup_driver_pin("\\random very long @ string with spaces and complicated stuff %");
  auto spin = add_n2_setup_sink_pin("\\   foo  \nbar");
  check_edges();

  check_setup_pins();

  add_edge(dpin,spin);

  check_setup_pins();
  check_edges();

  for(int i=0;i<6000;++i) {
    Node_pin d;
    Node_pin s;
    if (rbool.any())
      d = add_n1_setup_driver_pin("driver_pin" + std::to_string(i));
    if (rbool.any())
      s = add_n2_setup_sink_pin("sink_pin" + std::to_string(i));
    if (rbool.any() && !d.is_invalid() && !s.is_invalid()) {
      add_edge(d, s);
    }
  }

  int conta=0;
  for(auto &out : n1.out_edges()) {
    conta++;
    (void)out;
  }
  EXPECT_EQ(conta, track_edge_count.size());
  for(auto &out : n2.out_edges()) {
    I(false);
    (void)out; // just to silence the warning
  }
}

TEST_F(Edge_test, trivial_delete) {

  auto dpin = add_n1_setup_driver_pin("driver_pin" + std::to_string(1));
  auto spin = add_n2_setup_sink_pin("sink_pin" + std::to_string(3));

  g->add_edge(dpin, spin, 33);

  EXPECT_EQ(n1.out_edges().size(), 1);
  EXPECT_EQ(n1.inp_edges().size(), 0);

  EXPECT_EQ(n2.out_edges().size(), 0);
  EXPECT_EQ(n2.inp_edges().size(), 1);

  for(auto &inp : n2.inp_edges()) {
    inp.del_edge();
  }

  EXPECT_EQ(n1.out_edges().size(), 0);
  EXPECT_EQ(n1.inp_edges().size(), 0);

  EXPECT_EQ(n2.out_edges().size(), 0);
  EXPECT_EQ(n2.inp_edges().size(), 0);

  g->add_edge(dpin, spin, 33);

  EXPECT_EQ(n1.out_edges().size(), 1);
  EXPECT_EQ(n1.inp_edges().size(), 0);

  EXPECT_EQ(n2.out_edges().size(), 0);
  EXPECT_EQ(n2.inp_edges().size(), 1);

#if 0
  n2.del_node();

  EXPECT_EQ(n1.out_edges().size(), 0);
  EXPECT_EQ(n1.inp_edges().size(), 0);
#endif
}

TEST_F(Edge_test, overflow_delete) {

  auto s1 = g->create_node(Sum_Op);

  track_edge_count.clear();
  check_setup_pins();
  check_edges();

  for(int i=0;i<6000;++i) {
    Node_pin d;
    Node_pin s;
    if (rbool.any())
      d = add_n1_setup_driver_pin("driver_pin" + std::to_string(i));
    if (rbool.any())
      s = add_n2_setup_sink_pin("sink_pin" + std::to_string(i));
    if (rbool.any() && !d.is_invalid() && !s.is_invalid()) {
      add_edge(d, s1.setup_sink_pin(0));
      add_edge(s1.setup_driver_pin(), s);
    }
  }

  std::vector<XEdge::Compact> all_edges;
  for (auto &b : track_edge_count) {
    all_edges.push_back(b.first);
  }

  std::random_shuffle(all_edges.begin(), all_edges.end());

  for (auto &e : all_edges) {
    XEdge edge(g, e);

    edge.del_edge();
    track_edge_count.erase(e);
    check_edges();
  }

  EXPECT_EQ(n1.out_edges().size(),0);
  EXPECT_EQ(n2.inp_edges().size(),0);

  EXPECT_EQ(s1.out_edges().size(),0);
  EXPECT_EQ(s1.inp_edges().size(),0);
}

TEST_F(Edge_test, overflow_delete_node) {

  auto s1 = g->create_node(Sum_Op);

  track_edge_count.clear();
  check_setup_pins();
  check_edges();

  for(int i=0;i<6000;++i) {
    Node_pin d;
    Node_pin s;
    if (rbool.any())
      d = add_n1_setup_driver_pin("driver_pin" + std::to_string(i));
    if (rbool.any())
      s = add_n2_setup_sink_pin("sink_pin" + std::to_string(i));
    if (rbool.any() && !d.is_invalid() && !s.is_invalid()) {
      add_edge(d, s1.setup_sink_pin(0));
      add_edge(s1.setup_driver_pin(), s);
    }
  }

  s1.del_node(); // Nuke the middle node with all the connections

  EXPECT_EQ(n1.out_edges().size(),0);
  EXPECT_EQ(n1.inp_edges().size(),0);

  EXPECT_EQ(n1.out_edges().size(),0);
  EXPECT_EQ(n2.inp_edges().size(),0);

  EXPECT_TRUE(s1.is_invalid());
}

TEST_F(Edge_test, overflow_delete_del_edge_bench) {


  auto s1 = g->create_node(Sum_Op);

  for(int i=0;i<6000;++i) {
    Node_pin d;
    Node_pin s;
    if (rbool.any())
      d = add_n1_setup_driver_pin("driver_pin" + std::to_string(i));
    if (rbool.any())
      s = add_n2_setup_sink_pin("sink_pin" + std::to_string(i));
    if (rbool.any() && !d.is_invalid() && !s.is_invalid()) {
      add_edge(d, s1.setup_sink_pin(0));
      add_edge(s1.setup_driver_pin(), s);
    }
  }


  std::vector<XEdge::Compact> all_edges;
  for (auto &b : track_edge_count) {
    all_edges.push_back(b.first);
  }

  std::random_shuffle(all_edges.begin(), all_edges.end());

  {
    Lbench bench("overflow_delete del_node");

    for (auto &e : all_edges) {
      XEdge edge(g, e);
      edge.del_edge();
    }

    double secs = bench.get_secs();
    fmt::print("del_edge size:{} {}Kdels/sec\n", all_edges.size(), (all_edges.size() / 1000.0) / secs);
  }

  EXPECT_EQ(n1.out_edges().size(),0);
  EXPECT_EQ(n2.inp_edges().size(),0);

  EXPECT_EQ(s1.out_edges().size(),0);
  EXPECT_EQ(s1.inp_edges().size(),0);
}

TEST_F(Edge_test, overflow_delete_del_node_bench) {

  auto s1 = g->create_node(Sum_Op);

  int all_edges=0;
  for(int i=0;i<6000;++i) {
    Node_pin d;
    Node_pin s;
    if (rbool.any())
      d = add_n1_setup_driver_pin("driver_pin" + std::to_string(i));
    if (rbool.any())
      s = add_n2_setup_sink_pin("sink_pin" + std::to_string(i));
    if (rbool.any() && !d.is_invalid() && !s.is_invalid()) {
      add_edge(d, s1.setup_sink_pin(0));
      add_edge(s1.setup_driver_pin(), s);
      all_edges+=2;
    }
  }

  {
    Lbench bench("overflow_delete del_node");

    s1.del_node();

    double secs = bench.get_secs();
    fmt::print("del_edge size:{} {}Kdels/sec\n", all_edges, (all_edges / 1000.0) / secs);
  }

  EXPECT_EQ(n1.out_edges().size(),0);
  EXPECT_EQ(n2.inp_edges().size(),0);

  EXPECT_TRUE(s1.is_invalid());
}

#if 0
bool test22() {

  LGraph *g = LGraph::create("lgdb_core_test", "test22", "test");

  auto n1 = g->create_node_sub("n1");
  auto n2 = g->create_node_sub("n2");

  auto dpin = n1.setup_driver_pin(20);
  auto spin = n2.setup_sink_pin(0);

  g->add_edge(dpin, spin, 33);

  for(auto &out : n1.out_edges()) {
    out.del_edge();
  }

  for(auto &inp : n2.inp_edges()) {
    assert(false);
    (void)inp; // just to silence the warning
  }

  for(auto &out : n1.out_edges()) {
    assert(false);
    (void)out; // just to silence the warning
  }

  return true;
}

bool test3() {

  LGraph *g = LGraph::create("lgdb_core_test", "test3", "test");

  auto n1 = g->create_node_sub("n1");
  auto n2 = g->create_node_sub("n2");

  g->add_edge(n1.setup_driver_pin(20), n2.setup_sink_pin(25));

  for(auto &inp : n2.inp_edges()) {
    inp.del_edge();
  }

  for(auto &inp : n2.inp_edges()) {
    assert(false);
    (void)inp; // just to silence the warning
  }

  for(auto &out : n1.out_edges()) {
    assert(false);
    (void)out; // just to silence the warning
  }

  n2.del_node();

  for(auto node:g->fast()) {
    I(node != n2);
  }

  return true;
}

#endif
