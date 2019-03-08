//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgedgeiter.hpp"
#include "lgraph.hpp"

bool test0() {
  LGraph *g = LGraph::create("lgdb_core_test", "test0", "test");

  auto n1 = g->create_node(SubGraph_Op);
  auto n2 = g->create_node(SubGraph_Op);

  auto driver_pin20  = n1.setup_driver_pin(20);
  auto sink_pin120 = n2.setup_sink_pin(120);

  for(auto &out : g->out_edges(n1.get_nid())) {
    I(false);
    (void)out; // just to silence the warning
  }
  for(auto &out : g->out_edges(n2.get_nid())) {
    I(false);
    (void)out; // just to silence the warning
  }

  g->add_edge(driver_pin20,sink_pin120);
  int conta=0;
  for(auto &out : g->out_edges(n1.get_nid())) {
    conta++;
    (void)out;
  }
  I(conta==1);
  for(auto &out : g->out_edges(n2.get_nid())) {
    I(false);
    (void)out; // just to silence the warning
  }

  for(int i=30;i<330;i++) {
    auto s = n1.setup_driver_pin(i);
    g->add_edge(s,sink_pin120);
  }
  conta = 0;
  for(auto &out : g->out_edges(n1.get_nid())) {
    conta++;
    (void)out;
  }
  I(conta == (1+300));

  for(int i=200;i<1200;i++) {
    auto s = n1.setup_driver_pin((i&31) + 20);
    auto p = n2.setup_sink_pin(i);
    g->add_edge(s,p);
  }
  conta = 0;
  for(auto &out : g->out_edges(n1.get_nid())) {
    conta++;
    (void)out;
  }
  I(conta == (1+300+1000));
  conta = 0;
  for(auto &out : g->inp_edges(n2.get_nid())) {
    conta++;
    (void)out;
  }
  I(conta == (1+300+1000));

  g->close();

  return true;
}

bool test1() {
  LGraph *g = LGraph::create("lgdb_core_test", "test", "test");

  Index_ID idx1 = g->create_node(SubGraph_Op).get_nid();
  Index_ID idx2 = g->create_node(SubGraph_Op).get_nid();

  auto dpin = g->get_node(idx1).setup_driver_pin(20);
  auto spin = g->get_node(idx2).setup_sink_pin(25);

  g->add_edge(dpin, spin);

  for(auto &out : g->out_edges(idx1)) {
    assert(out.get_inp_pin().get_idx() == spin.get_idx());
    assert(out.get_out_pin().get_idx() == dpin.get_idx());

    assert(out.get_inp_pin().get_pid() == 25);
    assert(out.get_out_pin().get_pid() == 20);

    assert(out.get_out_pin().is_input() == false);
    assert(out.get_inp_pin().is_input() == true);
  }

  for(auto &inp : g->inp_edges(idx2)) {
    assert(inp.get_inp_pin().get_idx() == spin.get_idx());
    assert(inp.get_out_pin().get_idx() == dpin.get_idx());

    assert(inp.get_inp_pin().get_pid() == 25);
    assert(inp.get_out_pin().get_pid() == 20);

    assert(inp.get_out_pin().is_input() == false);
    assert(inp.get_inp_pin().is_input() == true);
  }

  g->close();

  return true;
}

bool test20() {
  LGraph *g = LGraph::create("lgdb_core_test", "test20", "test");

  Index_ID idx1 = g->create_node(SubGraph_Op).get_nid();
  Index_ID idx2 = g->create_node(SubGraph_Op).get_nid();

  auto dpin = g->get_node(idx1).setup_driver_pin(0);
  auto spin = g->get_node(idx2).setup_sink_pin(3);

  g->add_edge(dpin, spin, 33);

  for(auto &inp : g->inp_edges(idx2)) {
    g->del_edge(inp);
  }

  for(auto &inp : g->inp_edges(idx2)) {
    assert(false);
    (void)inp; // just to silence the warning
  }

  for(auto &out : g->out_edges(idx1)) {
    assert(false);
    (void)out; // just to silence the warning
  }

  g->close();

  return true;
}

bool test21() {

  LGraph *g = LGraph::create("lgdb_core_test", "test21", "test");

  Index_ID idx1 = g->create_node(SubGraph_Op).get_nid();
  Index_ID idx2 = g->create_node(SubGraph_Op).get_nid();

  auto dpin = g->get_node(idx1).setup_driver_pin(0);
  auto spin = g->get_node(idx2).setup_sink_pin(0);

  g->add_edge(dpin, spin, 33);

  for(auto &out : g->out_edges(idx1)) {
    g->del_edge(out);
  }

  for(auto &inp : g->inp_edges(idx2)) {
    assert(false);
    (void)inp; // just to silence the warning
  }

  for(auto &out : g->out_edges(idx1)) {
    assert(false);
    (void)out; // just to silence the warning
  }

  g->close();

  return true;
}

bool test2() {

  LGraph *g = LGraph::create("lgdb_core_test", "test2", "test");

  Index_ID idx1 = g->create_node(SubGraph_Op).get_nid();
  Index_ID idx2 = g->create_node(SubGraph_Op).get_nid();

  auto dpin = g->get_node(idx1).setup_driver_pin(20);
  auto spin = g->get_node(idx2).setup_sink_pin(30);

  g->add_edge(dpin, spin, 33);

  for(auto &inp : g->inp_edges(idx2)) {
    g->del_edge(inp);
  }

  for(auto &inp : g->inp_edges(idx2)) {
    assert(false);
    (void)inp; // just to silence the warning
  }

  for(auto &out : g->out_edges(idx1)) {
    assert(false);
    (void)out; // just to silence the warning
  }

  g->close();

  return true;
}

bool test22() {

  LGraph *g = LGraph::create("lgdb_core_test", "test22", "test");

  Index_ID idx1 = g->create_node(SubGraph_Op).get_nid();
  Index_ID idx2 = g->create_node(SubGraph_Op).get_nid();

  auto dpin = g->get_node(idx1).setup_driver_pin(20);
  auto spin = g->get_node(idx2).setup_sink_pin(0);

  g->add_edge(dpin, spin, 33);

  for(auto &out : g->out_edges(idx1)) {
    g->del_edge(out);
  }

  for(auto &inp : g->inp_edges(idx2)) {
    assert(false);
    (void)inp; // just to silence the warning
  }

  for(auto &out : g->out_edges(idx1)) {
    assert(false);
    (void)out; // just to silence the warning
  }

  g->close();

  return true;
}

bool test3() {

  LGraph *g = LGraph::create("lgdb_core_test", "test3", "test");

  Index_ID idx1 = g->create_node(SubGraph_Op).get_nid();
  Index_ID idx2 = g->create_node(SubGraph_Op).get_nid();

  g->add_edge(g->get_node(idx1).setup_driver_pin(20), g->get_node(idx2).setup_sink_pin(25));

  for(auto &inp : g->inp_edges(idx2)) {
    g->del_edge(inp);
  }

  for(auto &inp : g->inp_edges(idx2)) {
    assert(false);
    (void)inp; // just to silence the warning
  }

  for(auto &out : g->out_edges(idx1)) {
    assert(false);
    (void)out; // just to silence the warning
  }

  g->del_node(idx2);

  for(auto nid : g->fast()) {
    assert(nid != idx2);
  }

  g->close();

  return true;
}

bool test4() {

  LGraph *g = LGraph::create("lgdb_core_test", "test4", "test");

  Index_ID idx1  = g->create_node(SubGraph_Op).get_nid();
  auto idx2_node = g->create_node(SubGraph_Op);
  Index_ID idx2  = idx2_node.get_nid();

  g->add_edge(g->get_node(idx1).setup_driver_pin(20), idx2_node.setup_sink_pin(25));

  g->del_node(idx2);

  for(auto nid : g->fast()) {
    assert(nid != idx2);
  }

  g->close();

  return true;
}

int main() {
  test0();
  test1();
  test20();
  test21();
  test2();
  test22();
  test3();
  test4();

  return 0;
}
