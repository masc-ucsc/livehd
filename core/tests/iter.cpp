
#include "lgraph.hpp"
#include "lgedgeiter.hpp"

#include <set>


void generate_graphs(int n) {

  for(int i = 0; i < n; i++) {
    std::string gname = "test_" + std::to_string(i);
    LGraph *g = new LGraph("lgdb", gname, true);
    std::vector<Index_ID> nodes;

    int inps = 10+rand()%100;
    for(int j = 0; j < inps; j++) {
      //max 110 inputs, min 10
      Index_ID inp_id = g->add_graph_input(("i" + std::to_string(j)).c_str(), 0, 1);
      nodes.push_back(inp_id);
    }
    int outs = 10+rand()%100;
    for(int j = 0; j < outs; j++) {
      //max 110 outs, min 10
      Index_ID out_id = g->add_graph_output(("o" + std::to_string(j)).c_str(), 0, 1);
      nodes.push_back(out_id);
    }

    int nnodes = 100+rand()%1000;
    for(int j = 0; j < nnodes; j++) {
      Index_ID nid    = g->create_node().get_nid();
      Node_Type_Op op = (Node_Type_Op)(1+rand()%22); // regular node types range
      g->node_type_set(nid, op);
      nodes.push_back(nid);
    }

    int nedges = 2000+rand()%5000;
    std::set<std::pair<Index_ID, Index_ID> > edges;
    for(int j = 0; j < nedges; j++) {
      int counter  = 0;
      Index_ID src;;
      Index_ID dst;
      do{
        do{
          src = nodes[rand()%(nodes.size())];
        } while(!g->is_graph_output(src));
        do {
          dst = nodes[rand()%(nodes.size())];
        } while(!g->is_graph_input(dst));
        counter++;
      } while(!src && !dst &&
          edges.find(std::make_pair(src,dst)) != edges.end() && counter < 1000 &&
          (g->is_graph_output(dst) || dst >= src)); // guarantees no loops

      if(!src || !dst || edges.find(std::make_pair(src,dst)) != edges.end())
        continue;

      if(!g->is_graph_output(dst) && dst >= src)
        continue;

      edges.insert(std::make_pair(src,dst));
      g->add_edge(Node_Pin(src, 0, false), Node_Pin(dst, 0, true));

    }

    g->sync();

  }
}

bool fwd(int n) {
  for(int i = 0; i < n; i++) {
    std::string gname = "test_" + std::to_string(i);
    LGraph *g = new LGraph("lgdb", gname, false);

    std::set<Index_ID> visited;
    for(auto &idx : g->forward()) {

      //check if all incoming edges were visited
      for(auto& inp : g->inp_edges(idx)) {
        if(visited.find(inp.get_out_pin().get_nid()) == visited.end()) {
          printf("fwd failed for lgraph %d\n", i);
          return false;
        }
      }

      visited.insert(idx);
    }
  }
  return true;
}

bool bwd(int n){
  for(int i = 0; i < n; i++) {
    std::string gname = "test_" + std::to_string(i);
    LGraph *g = new LGraph("lgdb", gname, false);

    std::set<Index_ID> visited;
    for(auto &idx : g->backward()) {

      //check if all incoming edges were visited
      for(auto& inp : g->out_edges(idx)) {
        if(visited.find(inp.get_out_pin().get_nid()) == visited.end()) {
          printf("bwd failed for lgraph %d\n", i);
          return false;
        }
      }

      visited.insert(idx);
    }
  }
  return true;
}

int main() {
  bool failed = false;

  int n = 100;
  generate_graphs(n);

  if(!fwd(n)) {
    failed = true;
  }
  if(!bwd(n)) {
    failed = true;
  }

  return failed ? 1 : 0;
}

