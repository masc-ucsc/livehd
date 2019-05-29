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
    std::vector<Node_pin::Compact> spins;
    std::vector<Node_pin::Compact> dpins;

    int inps = 10 + rand_r(&rseed) % 100;
    for(int j = 0; j < inps; j++) {
      dpins.push_back(g->add_graph_input(("i" + std::to_string(j)), 1, 0).get_compact());
    }

    int outs = 10 + rand_r(&rseed) % 100;
    for(int j = 0; j < outs; j++) {
      spins.push_back(g->add_graph_output(("o" + std::to_string(j)), 1, 0).get_compact());
      dpins.push_back(g->get_graph_output_driver(("o" + std::to_string(j))).get_compact());
    }

    int nnodes = 100 + rand_r(&rseed) % 1000;
    for(int j = 0; j < nnodes; j++) { // Simple output nodes
      auto node = g->create_node();
      Node_Type_Op op  = (Node_Type_Op)(1 + rand_r(&rseed) % 22); // regular node types range
      node.set_type(op);
      dpins.push_back(node.setup_driver_pin().get_compact());
      spins.push_back(node.setup_sink_pin().get_compact());
    }

    int cnodes = 100 + rand_r(&rseed) % 1000;
    for(int j = 0; j < cnodes; j++) { // complex nodes
      auto node = g->create_node(FFlop_Op);
      auto d1 = rand_r(&rseed)%3;
      auto s1 = rand_r(&rseed)%6;
      dpins.push_back(node.setup_driver_pin(d1).get_compact());
      spins.push_back(node.setup_sink_pin(s1).get_compact());
      if (rand_r(&rseed)&1) {
        auto d2 = rand_r(&rseed)%3;
        auto s2 = rand_r(&rseed)%6;
        if (d1!=d2)
          dpins.push_back(node.setup_driver_pin(d2).get_compact());
        if (s1!=s2)
          spins.push_back(node.setup_sink_pin(s2).get_compact());
      }
    }

    int nedges = 2000 + rand_r(&rseed) % 5000;
    std::set<std::pair<Node_pin::Compact, Node_pin::Compact>> edges;
    for(int j = 0; j < nedges; j++) {
      int      counter = 0;
      Node_pin::Compact src;
      Node_pin::Compact dst;
      do {
        src = dpins[rand_r(&rseed) % (dpins.size())];
        dst = spins[rand_r(&rseed) % (spins.size())];
        counter++;
      } while(edges.find(std::make_pair(src, dst)) != edges.end() && counter < 1000);

      if (edges.find(std::make_pair(src, dst)) != edges.end())
        break;

      edges.insert(std::make_pair(src, dst));
      g->add_edge(Node_pin(g,0,src), Node_pin(g,0,dst));
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
      auto node = Node(g,0,Node::Compact(idx));

      // check if all incoming edges were visited
      for(auto &inp : node.inp_edges()) {
        if(visited.find(inp.driver.get_node().get_compact().nid) == visited.end()) {
          printf("fwd failed for lgraph %d\n", i);
          I(false);
          return false;
        }
      }

      visited.insert(node.get_compact().nid);
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

    std::set<Node::Compact> visited;
    for(auto &idx : g->backward()) {
      auto node = Node(g,0,Node::Compact(idx));
      visited.insert(node.get_compact());

      // check if all incoming edges were visited
      for(auto &out : node.out_edges()) {
        if(visited.find(out.sink.get_node().get_compact()) == visited.end()) {
          printf("bwd failed for lgraph %d\n", i);
          I(false);
          return false;
        }
      }

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

  auto c9 = g->create_node_const(1,3); //  9
  auto c10 = g->create_node_const(21,4); //  10
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
  auto t23 = g->create_node(SubGraph_Op); // 23

  /*
  // nodes:
  //     1i    2i    3i    4i    9c 23g 10c   11c   12c
  //       \  /       \   /  \    \  | /      |   /   \
  //        13g        14g    15g  16g        17g    18g  22g
  //        | \       / \           \        /  \   /   \
  //        |  \     /   \           \      20   19g    21g
  //        |   \   /     \           \   /
  //        5o    6o       7o           8o
  //
  // IDX:
  //
  //     1i    3i    5i    7i   17c 31g 18c   19c   20c
  //       \  /       \   /  \    \  | /      |   /   \
  //        21g        22g    23g  24g        25g    26g  30g
  //        | \       / \           \        /  \   /   \
  //        |  \     /   \           \      28   27g    29g
  //        |   \   /     \           \   /
  //        9o   11o      13o          15o
  */

  g->add_edge(i1, t13.setup_sink_pin(random()&0xFF));
  g->add_edge(i2, t13.setup_sink_pin(random()&0xFF));

  g->add_edge(i3, t14.setup_sink_pin(random()&0xFF));
  g->add_edge(i4, t14.setup_sink_pin(random()&0xFF));
  g->add_edge(i4, t15.setup_sink_pin(random()&0xFF));

  g->add_edge(c9.setup_driver_pin(), t16.setup_sink_pin(random()&0xFF));
  g->add_edge(c10.setup_driver_pin(), t16.setup_sink_pin(random()&0xFF));
  g->add_edge(t23.setup_driver_pin(random()&0xFF), t16.setup_sink_pin(random()&0xFF));

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
  for(const auto &nid : g->fast()) {
    auto node = Node(g,0,Node::Compact(nid)); // NOTE: To remove once new iterators are finished

    fmt::print("idx:{}\n", nid);
    fmt::print("  inp_edges");
    for(const auto &edge : node.inp_edges()) {
      fmt::print("  {}", edge.driver.get_node().get_compact().nid);
    }
    fmt::print("\n");
    fmt::print("  out_edges");
    for(const auto &edge : node.out_edges()) {
      fmt::print("  {}", edge.sink.get_node().get_compact().nid);
    }
    fmt::print("\n");
  }

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
