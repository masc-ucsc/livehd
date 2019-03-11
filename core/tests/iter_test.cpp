//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgedgeiter.hpp"
#include "lgraph.hpp"

#include <set>

bool failed = false;

void generate_graphs(int n) {

  unsigned int rseed = 123;

  for(int i = 0; i < n; i++) {
    std::string           gname = "test_" + std::to_string(i);
    LGraph *              g     = LGraph::create("lgdb_iter_test", gname, "test");
    std::vector<Index_ID> nodes;

    int inps = 10 + rand_r(&rseed) % 100;
    for(int j = 0; j < inps; j++) {
      // max 110 inputs, min 10
      Index_ID inp_id = g->add_graph_input(("i" + std::to_string(j)).c_str(), 1, 0).get_idx();
      nodes.push_back(inp_id);
    }
    int outs = 10 + rand_r(&rseed) % 100;
    for(int j = 0; j < outs; j++) {
      // max 110 outs, min 10
      Index_ID out_id = g->add_graph_output(("o" + std::to_string(j)).c_str(), 1, 0).get_idx();
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
      g->add_edge(g->get_node(src).setup_driver_pin(), g->get_node(dst).setup_sink_pin());
    }

    g->close();
  }
}

bool fwd(int n) {
  for(int i = 0; i < n; i++) {
    std::string gname = "test_" + std::to_string(i);
    LGraph *    g     = LGraph::open("lgdb_iter_test", gname);
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
    LGraph *    g     = LGraph::open("lgdb_iter_test", gname);
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
  LGraph *    g     = LGraph::create("lgdb_iter_test", gname, "test");

  auto i1 = g->add_graph_input("i0", 1, 0); // 1
  auto i2 = g->add_graph_input("i1", 1, 0); // 2
  auto i3 = g->add_graph_input("i2", 1, 0); // 3
  auto i4 = g->add_graph_input("i3", 1, 0); // 4

  auto o5 = g->add_graph_output("o0", 1, 0); // 5
  auto o6 = g->add_graph_output("o1", 1, 0); // 6
  auto o7 = g->add_graph_output("o2", 1, 0); // 7
  auto o8 = g->add_graph_output("o3", 1, 0); // 8

  auto c9 = g->create_node_u32(1,3); //  9
  auto c10 = g->create_node_u32(21,4); //  10
  auto c11 = g->create_node_const("xxx",3); //  11
  auto c12 = g->create_node_const("yyyy",4); // 12

  auto t13 = g->create_node(SubGraph_Op); // 13
  auto t14 = g->create_node(SubGraph_Op); // 14
  auto t15 = g->create_node(SubGraph_Op); // 15
  auto t16 = g->create_node(SubGraph_Op); // 16
  auto t17 = g->create_node(SubGraph_Op); // 17
  auto t18 = g->create_node(SubGraph_Op); // 18
  auto t19 = g->create_node(SubGraph_Op); // 19
  auto t20 = g->create_node(SubGraph_Op); // 20
  auto t21 = g->create_node(SubGraph_Op); // 21
  auto t22 = g->create_node(SubGraph_Op); // 22
  (void)t22; // Disconnected node. Source of bug when fully disconnected

  /*     1     2     3     4     9     10     11    12
  //       \  /       \   /  \    \   /       |   /   \
  //        13         14     15   16          17     18   22
  //        | \       / \           \        /  \   /   \
  //        |  \     /   \           \      20   19     21
  //        |   \   /     \           \   /
  //        5     6        7            8
  */

  g->add_edge(i1, t13.setup_sink_pin(random()&0xFF));
  g->add_edge(i2, t13.setup_sink_pin(random()&0xFF));

  g->add_edge(i3, t14.setup_sink_pin(random()&0xFF));
  g->add_edge(i4, t14.setup_sink_pin(random()&0xFF));
  g->add_edge(i4, t15.setup_sink_pin(random()&0xFF));

  g->add_edge(c9.setup_driver_pin(), t16.setup_sink_pin(random()&0xFF));
  g->add_edge(c10.setup_driver_pin(), t16.setup_sink_pin(random()&0xFF));

  g->add_edge(c11.setup_driver_pin(), t17.setup_sink_pin(random()&0xFF));
  if (rand()&1)
    g->add_edge(c12.setup_driver_pin(), t17.setup_sink_pin(1000+(random()&0xFF)));
  g->add_edge(c12.setup_driver_pin(), t17.setup_sink_pin(random()&0xFF));
  g->add_edge(c12.setup_driver_pin(), t18.setup_sink_pin(random()&0xFF));

  g->add_edge(t13.setup_driver_pin(random()&0xFF), o5);
  g->add_edge(t13.setup_driver_pin(random()&0xFF), o6);
  if (rand()&1)
    g->add_edge(t14.setup_driver_pin(1000+(random()&0xFF)), o6);
  g->add_edge(t14.setup_driver_pin(random()&0xFF), o6);
  g->add_edge(t14.setup_driver_pin(random()&0xFF), o7);

  g->add_edge(t17.setup_driver_pin(random()&0xFF), t20.setup_sink_pin(random()&0xFF));
  if (rand()&1)
    g->add_edge(t17.setup_driver_pin(1000+(random()&0xFF)), t20.setup_sink_pin(random()&0xFF));
  g->add_edge(t17.setup_driver_pin(random()&0xFF), t19.setup_sink_pin(random()&0xFF));
  g->add_edge(t18.setup_driver_pin(random()&0xFF), t19.setup_sink_pin(random()&0xFF));
  g->add_edge(t18.setup_driver_pin(random()&0xFF), t21.setup_sink_pin(random()&0xFF));

  g->add_edge(t16.setup_driver_pin(random()&0xFF), o8);
  g->add_edge(t20.setup_driver_pin(random()&0xFF), o8);

  std::string fwd;
  int conta=0;
  for(const auto &idx : g->forward()) {
    fwd += std::to_string(idx) + " ";
    conta++;
  }
  if (conta!=22)
    failed = true;

  std::string bwd;
  conta =0;
  for(const auto &idx : g->backward()) {
    bwd += std::to_string(idx) + " ";
    conta++;
  }
  if (conta!=22)
    failed = true;

  std::string fast;
  conta =0;
  for(const auto &idx : g->fast()) {
    fast += std::to_string(idx) + " ";
    conta++;
  }
  if (conta!=22)
    failed = true;

  fmt::print("fwd :{}\n",fwd);
  fmt::print("back:{}\n",bwd);
  fmt::print("fast:{}\n",fast);

  if (fwd.size() != bwd.size())
    failed = true;
  if (fast.size() != bwd.size())
    failed = true;

  g->close();

  return true;
}

int main() {

  for(int i=0;i<20;i++) {
    simple();
    if (failed)
      return -3;
  }

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
