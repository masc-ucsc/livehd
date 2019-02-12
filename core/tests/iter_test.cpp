//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgedgeiter.hpp"
#include "lgraph.hpp"

#include <set>

void generate_graphs(int n) {

  unsigned int rseed = 123;

  for(int i = 0; i < n; i++) {
    std::string           gname = "test_" + std::to_string(i);
    LGraph *              g     = LGraph::create("core_test_lgdb", gname, "test");
    std::vector<Index_ID> nodes;

    int inps = 10 + rand_r(&rseed) % 100;
    for(int j = 0; j < inps; j++) {
      // max 110 inputs, min 10
      Index_ID inp_id = g->add_graph_input(("i" + std::to_string(j)).c_str(), 0, 1, 0);
      nodes.push_back(inp_id);
    }
    int outs = 10 + rand_r(&rseed) % 100;
    for(int j = 0; j < outs; j++) {
      // max 110 outs, min 10
      Index_ID out_id = g->add_graph_output(("o" + std::to_string(j)).c_str(), 0, 1, 0);
      nodes.push_back(out_id);
    }

    int nnodes = 100 + rand_r(&rseed) % 1000;
    for(int j = 0; j < nnodes; j++) {
      Index_ID     nid = g->create_node().get_nid();
      Node_Type_Op op  = (Node_Type_Op)(1 + rand_r(&rseed) % 22); // regular node types range
      g->node_type_set(nid, op);
      nodes.push_back(nid);
    }

    int                                     nedges = 2000 + rand_r(&rseed) % 5000;
    std::set<std::pair<Index_ID, Index_ID>> edges;
    for(int j = 0; j < nedges; j++) {
      int      counter = 0;
      Index_ID src;
      ;
      Index_ID dst;
      do {
        do {
          src = nodes[rand_r(&rseed) % (nodes.size())];
        } while(!g->is_graph_output(src));
        do {
          dst = nodes[rand_r(&rseed) % (nodes.size())];
        } while(!g->is_graph_input(dst));
        counter++;
      } while(!src && !dst && edges.find(std::make_pair(src, dst)) != edges.end() && counter < 1000 &&
              (g->is_graph_output(dst) || dst >= src)); // guarantees no loops

      if(!src || !dst || edges.find(std::make_pair(src, dst)) != edges.end())
        continue;

      if(!g->is_graph_output(dst) && dst >= src)
        continue;

      edges.insert(std::make_pair(src, dst));
      g->add_edge(Node_pin(src, 0, false), Node_pin(dst, 0, true));
    }

    g->close();
  }
}

bool fwd(int n) {
  for(int i = 0; i < n; i++) {
    std::string gname = "test_" + std::to_string(i);
    LGraph *    g     = LGraph::open("core_test_lgdb", gname);
    if(g == 0)
      return false;

    std::set<Index_ID> visited;
    for(auto &idx : g->forward()) {

      // check if all incoming edges were visited
      for(auto &inp : g->inp_edges(idx)) {
        if(visited.find(inp.get_out_pin().get_idx()) == visited.end()) {
          printf("fwd failed for lgraph %d\n", i);
          I(false);
          return false;
        }
      }

      visited.insert(idx);
    }

    g->close();
  }

  return true;
}

bool bwd(int n) {
  for(int i = 0; i < n; i++) {
    std::string gname = "test_" + std::to_string(i);
    LGraph *    g     = LGraph::open("core_test_lgdb", gname);
    if(g == 0)
      return false;

    std::set<Index_ID> visited;
    for(auto &idx : g->backward()) {

      // check if all incoming edges were visited
      for(auto &out : g->out_edges(idx)) {
        if(visited.find(out.get_inp_pin().get_idx()) == visited.end()) {
          printf("bwd failed for lgraph %d\n", i);
          I(false);
          return false;
        }
      }

      visited.insert(idx);
    }

    g->close();
  }
  return true;
}

bool simple() {
  std::string gname = "simple_iter";
  LGraph *    g     = LGraph::create("core_test_lgdb", gname, "test");

  g->add_graph_input("i0", 0, 1, 0); // 1
  g->add_graph_input("i1", 0, 1, 0); // 2
  g->add_graph_input("i2", 0, 1, 0); // 3
  g->add_graph_input("i3", 0, 1, 0); // 4

  g->add_graph_output("o0", 0, 1, 0); // 5
  g->add_graph_output("o1", 0, 1, 0); // 6
  g->add_graph_output("o2", 0, 1, 0); // 7
  g->add_graph_output("o3", 0, 1, 0); // 8

  Index_ID const0 = g->create_node().get_nid(); //  9
  Index_ID const1 = g->create_node().get_nid(); //  10
  Index_ID const2 = g->create_node().get_nid(); //  11
  Index_ID const3 = g->create_node().get_nid(); //  12

  g->node_u32type_set(const0, 1);
  g->node_u32type_set(const1, 21);
  g->node_const_type_set(const2, "xxx");
  g->node_const_type_set_string(const3, "yyy");

  g->create_node().get_nid(); // 13
  g->create_node().get_nid(); // 14
  g->create_node().get_nid(); // 15
  g->create_node().get_nid(); // 16
  g->create_node().get_nid(); // 17
  g->create_node().get_nid(); // 18
  g->create_node().get_nid(); // 19
  g->create_node().get_nid(); // 20
  g->create_node().get_nid(); // 20

  /*     1     2     3     4     9     10     11    12
  //       \  /       \   /  \    \   /       |   /   \
  //        13         14     15   16          17     18
  //        | \       / \           \        /  \   /   \
  //        |  \     /   \           \      20   19     21
  //        |   \   /     \           \   /
  //        5     6        7            8
  */

  g->add_edge(Node_pin(1, 0, false), Node_pin(13, 0, true));
  g->add_edge(Node_pin(2, 0, false), Node_pin(13, 0, true));

  g->add_edge(Node_pin(3, 0, false), Node_pin(14, 0, true));
  g->add_edge(Node_pin(4, 0, false), Node_pin(14, 0, true));
  g->add_edge(Node_pin(4, 0, false), Node_pin(15, 0, true));

  g->add_edge(Node_pin(9, 0, false), Node_pin(16, 0, true));
  g->add_edge(Node_pin(10, 0, false), Node_pin(16, 0, true));

  g->add_edge(Node_pin(11, 0, false), Node_pin(17, 0, true));
  g->add_edge(Node_pin(12, 0, false), Node_pin(17, 0, true));
  g->add_edge(Node_pin(12, 0, false), Node_pin(18, 0, true));

  g->add_edge(Node_pin(13, 0, false), Node_pin(5, 0, true));
  g->add_edge(Node_pin(13, 0, false), Node_pin(6, 0, true));
  g->add_edge(Node_pin(14, 0, false), Node_pin(6, 0, true));
  g->add_edge(Node_pin(14, 0, false), Node_pin(7, 0, true));

  g->add_edge(Node_pin(17, 0, false), Node_pin(20, 0, true));
  g->add_edge(Node_pin(17, 0, false), Node_pin(19, 0, true));
  g->add_edge(Node_pin(18, 0, false), Node_pin(19, 0, true));
  g->add_edge(Node_pin(18, 0, false), Node_pin(21, 0, true));

  g->add_edge(Node_pin(16, 0, false), Node_pin(8, 0, true));
  g->add_edge(Node_pin(20, 0, false), Node_pin(8, 0, true));

  std::string fwd = "fwd: ";
  for(const auto &idx : g->forward()) {
    fwd += std::to_string(idx) + " ";
  }

  std::string bwd = "bwd: ";
  for(const auto &idx : g->backward()) {
    bwd += std::to_string(idx) + " ";
  }

  printf("\n\n%s\n\n%s\n\n", fwd.c_str(), bwd.c_str());

  g->close();

  return true;
}

int main() {
  bool failed = false;

  simple();

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
