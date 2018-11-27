//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgedgeiter.hpp"
#include "lgraph.hpp"

bool test0() {
  LGraph *g = LGraph::create("core_test_lgdb", "test0");

  Index_ID idx1 = g->create_node().get_nid();
  g->create_node().get_nid();

  g->get_idx_from_pid(idx1, 20);

  for(auto &out : g->out_edges(idx1)) {
    assert(false);
    (void)out; // just to silence the warning
  }

  g->close();

  return true;
}

bool test1() {
  LGraph *g = LGraph::create("core_test_lgdb", "test");

  Index_ID idx1 = g->create_node().get_nid();
  Index_ID idx2 = g->create_node().get_nid();

  g->add_edge(Node_Pin(idx1, 20, false), Node_Pin(idx2, 25, true));

  for(auto &out : g->out_edges(idx1)) {
    assert(out.get_inp_pin().get_nid() == idx2);
    assert(out.get_out_pin().get_nid() == idx1);

    assert(out.get_inp_pin().get_pid() == 25);
    assert(out.get_out_pin().get_pid() == 20);

    assert(out.get_out_pin().is_input() == false);
    assert(out.get_inp_pin().is_input() == true);
  }

  for(auto &inp : g->inp_edges(idx2)) {
    assert(inp.get_inp_pin().get_nid() == idx2);
    assert(inp.get_out_pin().get_nid() == idx1);

    assert(inp.get_inp_pin().get_pid() == 25);
    assert(inp.get_out_pin().get_pid() == 20);

    assert(inp.get_out_pin().is_input() == false);
    assert(inp.get_inp_pin().is_input() == true);
  }

  g->close();

  return true;
}

bool test20() {
  LGraph *g = LGraph::create("core_test_lgdb", "test20");

  Index_ID idx1 = g->create_node().get_nid();
  Index_ID idx2 = g->create_node().get_nid();

  g->add_edge(Node_Pin(idx1, 0, false), Node_Pin(idx2, 0, true));

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

  LGraph *g = LGraph::create("core_test_lgdb", "test21");

  Index_ID idx1 = g->create_node().get_nid();
  Index_ID idx2 = g->create_node().get_nid();

  g->add_edge(Node_Pin(idx1, 0, false), Node_Pin(idx2, 0, true));

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

  LGraph *g = LGraph::create("core_test_lgdb", "test2");

  Index_ID idx1 = g->create_node().get_nid();
  Index_ID idx2 = g->create_node().get_nid();

  g->add_edge(Node_Pin(idx1, 20, false), Node_Pin(idx2, 25, true));

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

  LGraph *g = LGraph::create("core_test_lgdb", "test22");

  Index_ID idx1 = g->create_node().get_nid();
  Index_ID idx2 = g->create_node().get_nid();

  g->add_edge(Node_Pin(idx1, 20, false), Node_Pin(idx2, 25, true));

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

  LGraph *g = LGraph::create("core_test_lgdb", "test3");

  Index_ID idx1 = g->create_node().get_nid();
  Index_ID idx2 = g->create_node().get_nid();

  g->add_edge(Node_Pin(idx1, 20, false), Node_Pin(idx2, 25, true));

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

  LGraph *g = LGraph::create("core_test_lgdb", "test4");

  Index_ID idx1 = g->create_node().get_nid();
  Index_ID idx2 = g->create_node().get_nid();

  g->add_edge(Node_Pin(idx1, 20, false), Node_Pin(idx2, 25, true));

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
