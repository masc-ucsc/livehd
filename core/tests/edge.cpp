
#include "lgraph.hpp"
#include "lgedgeiter.hpp"

int main() {


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



  return 0;
}


