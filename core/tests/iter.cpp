
#include "lgraph.hpp"
#include "lgedgeiter.hpp"

#include <set>


void generate_graphs(int n) {

  for(int i = 0; i < n; i++) {
    std::string gname = "test_" + std::to_string(n);
    LGraph *g = new LGraph("lgdb", gname, false);
    Index_ID idx1 = g->create_node().get_nid();
  }
}

#if 0
bool fwd() {
  std::set<Index_ID> visited;
  for(auto &idx : g->forward()) {

    //check if all incoming edges were visited
    for(auto& inp : g->inp_edges(idx)) {
      if(visited.find(inp.get_out_pin().get_nid()) == visited.end())
        return false;
    }

    visited.insert(idx);
  }
  return true;
}

bool bwd(){

  return true;
}
#endif

int main() {
  bool failed = false;

#if 0
  if(!fwd()) {
    fmt::print("fwd traversal failed\n");
    failed = true;
  }
  if(!bwd()) {
    fmt::print("bwd traversal failed\n");
    failed = true;
  }
#endif

  return failed ? 1 : 0;
}

