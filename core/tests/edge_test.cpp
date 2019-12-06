//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "lgedgeiter.hpp"
#include "lgraph.hpp"

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
  }

  void TearDown() override {
    g->sync();
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

      n1_sub->add_output_pin(pname, get_free_n1_graph_pos());

      auto dpin = n1.setup_driver_pin(pname);
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
      n2_sub->add_input_pin(pname, get_free_n2_graph_pos());

      auto spin = n2.setup_sink_pin(pname);
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

    for(auto e:n1.inp_edges()) {
      (void)e;
      EXPECT_TRUE(false);
    }
    for(auto e:n2.out_edges()) {
      (void)e;
      EXPECT_TRUE(false);
    }

    int conta=0;
    for(auto e:n1.out_edges()) {
      auto it = track_edge_count.find(e.get_compact());
      EXPECT_TRUE(it!=track_edge_count.end());
      conta++;
    }
    EXPECT_EQ(track_edge_count.size(), conta);

    conta=0;
    for(auto e:n2.inp_edges()) {
      auto it = track_edge_count.find(e.get_compact());
      EXPECT_TRUE(it!=track_edge_count.end());
      conta++;
    }
    EXPECT_EQ(track_edge_count.size(), conta);

  }

  void add_edge(Node_pin dpin, Node_pin spin) {
    XEdge edge(dpin, spin);
    auto it = track_edge_count.find(edge.get_compact());
    if (g->has_edge(dpin,spin)) {
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
    if (rbool.any() && !d.is_invalid() && !s.is_invalid())
      add_edge(d, s);
  }
}

#if 0
  int conta=0;
  for(auto &out : n1.out_edges()) {
    conta++;
    (void)out;
  }
  I(conta==1);
  for(auto &out : n2.out_edges()) {
    I(false);
    (void)out; // just to silence the warning
  }

  for(int i=30;i<330;i++) {
    auto s = n1.setup_driver_pin("o" + std::to_string(i));
    g->add_edge(s,sink_pin120);
  }
  conta = 0;
  for(auto &out : n1.out_edges()) {
    conta++;
    (void)out;
  }
  I(conta == (1+300));

  for(int i=200;i<1200;i++) {
    auto s = n1.setup_driver_pin("o" + std::to_string((i&31) + 20));
    auto p = n2.setup_sink_pin("i" + std::to_string(i));
    g->add_edge(s,p);
  }
  conta = 0;
  Port_ID last_pid=0;
  for(auto &out : n1.out_edges_ordered()) {
    I(last_pid<=out.driver.get_pid());
    last_pid = out.driver.get_pid();
    conta++;
    (void)out;
  }
  I(conta == (1+300+1000));
  conta = 0;
  last_pid=0;
  for(auto &out : n2.inp_edges_ordered()) {
    I(last_pid<=out.sink.get_pid());
    last_pid = out.sink.get_pid();
    conta++;
    (void)out;
  }
  I(conta == (1+300+1000));


  LGraph *g = LGraph::create("lgdb_core_test", "test", "test");

  auto n1 = g->create_node_sub("n1");
  auto n2 = g->create_node_sub("n2");

  auto dpin = n1.setup_driver_pin(20);
  auto spin = n2.setup_sink_pin(25);

  g->add_edge(dpin, spin);

  for(auto &out : n1.out_edges()) {
    I(out.sink == spin);
    I(out.driver == dpin);

    I(out.sink.get_pid() == 25);
    I(out.driver.get_pid() == 20);

    I(out.driver.is_input() == false);
    I(out.sink.is_input() == true);
  }

  for(auto &inp : n2.inp_edges()) {
    I(inp.sink == spin);
    I(inp.driver == dpin);

    I(inp.sink.get_pid() == 25);
    I(inp.driver.get_pid() == 20);

    I(inp.driver.is_input() == false);
    I(inp.sink.is_input() == true);
  }

  return true;
}

bool test20() {
  LGraph *g = LGraph::create("lgdb_core_test", "test20", "test");

  auto n1 = g->create_node_sub("n1");
  auto n2 = g->create_node_sub("n2");

  auto dpin = n1.setup_driver_pin(0);
  auto spin = n2.setup_sink_pin(3);

  g->add_edge(dpin, spin, 33);

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

  return true;
}

bool test21() {

  LGraph *g = LGraph::create("lgdb_core_test", "test21", "test");

  auto n1 = g->create_node_sub("n1");
  auto n2 = g->create_node_sub("n2");

  auto dpin = n1.setup_driver_pin(0);
  auto spin = n2.setup_sink_pin(0);

  g->add_edge(dpin, spin, 33);

  for(auto &inp : n2.inp_edges()) {
    I(inp.get_bits() == 33);
    I(inp.driver.get_bits() == 33);
  }

  for(auto &out : n1.out_edges()) {
    I(out.get_bits() == 33);
    I(out.driver.get_bits() == 33);
    out.del_edge();
  }

  for(auto &inp : n2.inp_edges()) {
    I(false);
    (void)inp; // just to silence the warning
  }

  for(auto &out : n1.out_edges()) {
    I(false);
    (void)out; // just to silence the warning
  }

  return true;
}

bool test2() {

  LGraph *g = LGraph::create("lgdb_core_test", "test2", "test");

  auto n1 = g->create_node_sub("n1");
  auto n2 = g->create_node_sub("n2");

  auto dpin = n1.setup_driver_pin(20);
  auto spin = n2.setup_sink_pin(30);

  g->add_edge(dpin, spin, 33);

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

  return true;
}

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
