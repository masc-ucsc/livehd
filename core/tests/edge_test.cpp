//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgedgeiter.hpp"
#include "lgraph.hpp"

bool test0() {
  LGraph *g = LGraph::create("lgdb_core_test", "test0", "test");

  auto n1 = g->create_node_sub("n1");
  auto n2 = g->create_node_sub("n2");

  auto driver_pin20  = n1.setup_driver_pin(20);
  auto sink_pin120 = n2.setup_sink_pin(120);

  for(auto &out : n1.out_edges()) {
    I(false);
    (void)out; // just to silence the warning
  }
  for(auto &out : n2.out_edges()) {
    I(false);
    (void)out; // just to silence the warning
  }

  g->add_edge(driver_pin20,sink_pin120);
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
    auto s = n1.setup_driver_pin(i);
    g->add_edge(s,sink_pin120);
  }
  conta = 0;
  for(auto &out : n1.out_edges()) {
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

  return true;
}

bool test1() {
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

int main() {
  test0();
  test1();
  test20();
  test21();
  test2();
  test22();
  test3();

  return 0;
}
