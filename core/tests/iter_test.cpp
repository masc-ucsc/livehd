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
      auto pin = g->add_graph_input("i" + std::to_string(j), j);
      pin.set_bits(1);
      dpins.push_back(pin.get_compact());
    }

    int outs = 10 + rand_r(&rseed) % 100;
    for(int j = 0; j < outs; j++) {
      auto pin = g->add_graph_output("o" + std::to_string(j), inps+j);
      pin.set_bits(1);
      spins.push_back(pin.get_compact());
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
    absl::flat_hash_set<std::pair<Node_pin::Compact, Node_pin::Compact>> edges;
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
      Node_pin dpin(g,src);
      Node_pin spin(g,dst);
      g->add_edge(dpin, spin);
    }

  }
}

bool fwd(int n) {
  for(int i = 0; i < n; i++) {
    std::string gname = "test_" + std::to_string(i);
    LGraph *    g     = LGraph::open("lgdb_iter_test", gname);
    if(g == 0)
      return false;

    absl::flat_hash_set<Node::Compact> visited;
    for(auto node : g->forward()) {

      // check if all incoming edges were visited
      for(auto &inp : node.inp_edges()) {
        if(visited.find(inp.driver.get_node().get_compact()) == visited.end()) {
          printf("fwd failed for lgraph %d\n", i);
          I(false);
          return false;
        }
      }

      visited.insert(node.get_compact());
    }

  }

  return true;
}

bool bwd(int n) {
  for(int i = 0; i < n; i++) {
    std::string gname = "test_" + std::to_string(i);
    LGraph *    g     = LGraph::open("lgdb_iter_test", gname);
    if(g == 0)
      return false;

    absl::flat_hash_set<Node::Compact> visited;
    for(auto node : g->backward()) {
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

  }
  return true;
}

void simple() {
  std::string gname = "simple_iter";
  LGraph *    g     = LGraph::create("lgdb_iter_test", gname, "test");

  int pos = 0;
  auto i1 = g->add_graph_input("i0", pos++); // 1
  i1.set_bits(1);
  auto i2 = g->add_graph_input("i1", pos++); // 2
  i2.set_bits(1);
  auto i3 = g->add_graph_input("i2", pos++); // 3
  i3.set_bits(1);
  auto i4 = g->add_graph_input("i3", pos++); // 4
  i4.set_bits(1);

  auto o5 = g->add_graph_output("o0", pos++); // 5
  auto o6 = g->add_graph_output("o1", pos++); // 6
  auto o7 = g->add_graph_output("o2", pos++); // 7
  auto o8 = g->add_graph_output("o3", pos++); // 8

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

  std::vector<std::string> fwd;
  int conta=0;
  for(const auto node : g->forward()) {
    fwd.emplace_back(node.debug_name(true));
    conta++;
  }
  if (conta!=22)
    failed = true;

  std::vector<std::string> bwd;
  conta =0;
  for(const auto node : g->backward()) {
    bwd.emplace_back(node.debug_name(true));
    conta++;
  }
  if (conta!=22)
    failed = true;

  std::vector<std::string> fast;
  conta =0;
  for(const auto node : g->fast()) {
    fast.emplace_back(node.debug_name(true));
    conta++;
  }
  if (conta!=22)
    failed = true;

  for(const auto &node : g->fast()) {

    fmt::print("node:{}\n", node.debug_name());
    fmt::print("  inp_edges");
    for(const auto &edge : node.inp_edges()) {
      fmt::print("  {}", edge.driver.debug_name());
    }
    fmt::print("\n");
    fmt::print("  out_edges");
    for(const auto &edge : node.out_edges()) {
      fmt::print("  {}", edge.sink.debug_name());
    }
    fmt::print("\n");
  }
  fmt::print("fwd :");
  for(auto txt:fwd)
    fmt::print(" {}",txt);
  fmt::print("\n");

  fmt::print("fwd :");
  for(auto txt:bwd)
    fmt::print(" {}",txt);
  fmt::print("\n");

  fmt::print("fast:");
  for(auto txt:fast)
    fmt::print(" {}",txt);
  fmt::print("\n");

  if (fwd.size() != bwd.size())
    failed = true;
  if (fast.size() != bwd.size())
    failed = true;

  std::sort(fwd.begin() , fwd.end());
  std::sort(bwd.begin() , bwd.end());
  std::sort(fast.begin(), fast.end());

  auto fast_it = fast.begin();
  auto fwd_it  = fwd.begin();
  auto bwd_it  = bwd.begin();

  while(fast_it != fast.end()) {
    if (*fast_it != *fwd_it) {
      fmt::print("missmatch fast {} vs fwd {}\n",*fast_it, *fwd_it);
      failed = true;
    }
    if (*fast_it != *bwd_it) {
      fmt::print("missmatch fast {} vs bwd {}\n",*fast_it, *bwd_it);
      failed = true;
    }
    fast_it++;
    fwd_it++;
    bwd_it++;
    if (fwd_it == fwd.end() && bwd_it == bwd.end() && fast_it == fast.end())
      break;

    if (fwd_it == fwd.end()) {
      fmt::print("fwd is shorter\n");
      failed = true;
      break;
    }
    if (bwd_it == bwd.end()) {
      fmt::print("bwd is shorter\n");
      failed = true;
      break;
    }
    if (fast_it == fast.end()) {
      fmt::print("fast is shorter\n");
      failed = true;
      break;
    }
  }
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
