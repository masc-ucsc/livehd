
#include "lgraph.hpp"
#include "lgedgeiter.hpp"

bool test1() {
  LGraph* g = new LGraph("lgdb","test", true);

  Index_ID idx1 = g->create_node().get_nid();
  Index_ID idx2 = g->create_node().get_nid();

  g->add_edge(Node_Pin(idx1, 20, false),
              Node_Pin(idx2, 25, true));

  for(auto & out : g->out_edges(idx1)) {
    assert(out.get_inp_pin().get_nid() == idx2);
    assert(out.get_out_pin().get_nid() == idx1);

    assert(out.get_inp_pin().get_pid() == 25);
    assert(out.get_out_pin().get_pid() == 20);

    //assert(out.get_inp_pid() == 25);
    //assert(out.get_out_pid() == 20);

    assert(out.get_out_pin().is_input() == false);
    assert(out.get_inp_pin().is_input() == true);
  }


  for(auto & inp : g->inp_edges(idx2)) {
    assert(inp.get_inp_pin().get_nid() == idx2);
    assert(inp.get_out_pin().get_nid() == idx1);

    assert(inp.get_inp_pin().get_pid() == 25);
    assert(inp.get_out_pin().get_pid() == 20);

    //assert(inp.get_inp_pid() == 25);
    //assert(inp.get_out_pid() == 20);

    assert(inp.get_out_pin().is_input() == false);
    assert(inp.get_inp_pin().is_input() == true);
  }

  return true;
}


bool test2() {

  LGraph* g = new LGraph("lgdb","test2", true);

  Index_ID idx1 = g->create_node().get_nid();
  Index_ID idx2 = g->create_node().get_nid();

  g->add_edge(Node_Pin(idx1, 20, false),
              Node_Pin(idx2, 25, true));



  for(auto & inp : g->inp_edges(idx2)) {
    g->del_edge(inp);
  }

  for(auto& inp : g->inp_edges(idx2)) {
    assert(false);
    (void) inp; //just to silence the warning
  }

  return true;

}

bool test3() {

  LGraph* g = new LGraph("lgdb","test3", true);

  Index_ID idx1 = g->create_node().get_nid();
  Index_ID idx2 = g->create_node().get_nid();

  g->add_edge(Node_Pin(idx1, 20, false),
              Node_Pin(idx2, 25, true));



  for(auto & inp : g->inp_edges(idx2)) {
    g->del_edge(inp);
  }

  for(auto& inp : g->inp_edges(idx2)) {
    assert(false);
    (void) inp; //just to silence the warning
  }

  g->del_node(idx2);

  for(auto nid : g->fast()) {
    assert(nid != idx2);
  }

  return true;
}

bool test4() {

  LGraph* g = new LGraph("lgdb","test4", true);

  Index_ID idx1 = g->create_node().get_nid();
  Index_ID idx2 = g->create_node().get_nid();

  g->add_edge(Node_Pin(idx1, 20, false),
              Node_Pin(idx2, 25, true));


  Index_ID idx_child = g->get_idx_from_pid(idx2, 25);

  g->del_node(idx2);

  for(auto nid : g->fast()) {
    assert(nid != idx2);
  }

  return true;
}

int main() {
  test1();
  test2();
  test3();
  test4();

  return 0;
}


