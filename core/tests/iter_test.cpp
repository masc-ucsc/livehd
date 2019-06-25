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
      auto pin = g->add_graph_input("i" + std::to_string(j), j, 1);
      dpins.push_back(pin.get_compact());
    }

    int outs = 10 + rand_r(&rseed) % 100;
    for(int j = 0; j < outs; j++) {
      auto pin = g->add_graph_output("o" + std::to_string(j), inps+j,1);
      spins.push_back(pin.get_compact());
      dpins.push_back(g->get_graph_output_driver(("o" + std::to_string(j))).get_compact());
    }

    int nnodes = 100 + rand_r(&rseed) % 1000;
    for(int j = 0; j < nnodes; j++) { // Simple output nodes
      auto node = g->create_node();
      Node_Type_Op op  = (Node_Type_Op)(1 + rand_r(&rseed) % 22); // regular node types range
      node.set_type(op);
      dpins.push_back(node.setup_driver_pin(0).get_compact());
      spins.push_back(node.setup_sink_pin(0).get_compact());
    }

    int const_nodes = 10 + rand_r(&rseed) % 100;
    for(int j = 0; j < const_nodes; j++) { // Simple output nodes
      auto node = g->create_node();
      node.set_type(U32Const_Op);
      dpins.push_back(node.setup_driver_pin().get_compact());
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

    int nedges = 1000 + rand_r(&rseed) % 8000;
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

      Node_pin dpin(g,src);
      Node_pin spin(g,dst);
      if (dpin.get_node() == spin.get_node())
        continue; // No self-loops

      edges.insert(std::make_pair(src, dst));
      g->add_edge(dpin, spin);
    }

  }
}

bool fwd(int n) { for(int i = 0; i < n; i++) {
    std::string gname = "test_" + std::to_string(i);
    LGraph *    g     = LGraph::open("lgdb_iter_test", gname);
    if(g == 0)
      return false;

    //g->dump();
    //fmt::print("----------------------\n");
    absl::flat_hash_set<Node::Compact> visited;
    for(auto node : g->forward()) {

      if (!node.get_type().is_pipelined() && node.get_type().op != GraphIO_Op) {
        // check if all incoming edges were visited
        for(auto &inp : node.inp_edges()) {
          if (!inp.driver.get_node().get_type().is_pipelined() && inp.driver.get_node().get_type().op != GraphIO_Op) {
            if(visited.find(inp.driver.get_node().get_compact()) == visited.end()) {
              fmt::print("fwd failed for lgraph node:{} fwd:{}\n", node.debug_name(), inp.driver.get_node().debug_name());
              I(false);
              return false;
            }
          }
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

    //g->dump();
    //fmt::print("----------------------\n");
    absl::flat_hash_set<Node::Compact> visited;
    for(auto node : g->backward()) {
      //fmt::print(" bwd {}\n", node.debug_name());

      visited.insert(node.get_compact());

      if (!node.get_type().is_pipelined() && node.get_type().op != GraphIO_Op) {
        // check if all incoming edges were visited
        for(auto &out : node.out_edges()) {
          if (!out.sink.get_node().get_type().is_pipelined() && out.sink.get_node().get_type().op != GraphIO_Op) {
            if(visited.find(out.sink.get_node().get_compact()) == visited.end()) {
              fmt::print("bwd failed for lgraph node:{} bwd:{}\n", node.debug_name(), out.sink.get_node().debug_name());
              I(false);
              return false;
            }
          }
        }
      }

    }

  }
  return true;
}

void simple() {
  std::string gname = "simple_iter";
  LGraph *g     = LGraph::create("lgdb_iter_test", gname, "test");
  LGraph *sub_g = LGraph::create("lgdb_iter_test", "sub", "test");

  int pos = 0;
  auto i1 = g->add_graph_input("i0", pos++, 0); // 1
  i1.set_bits(1);
  auto i2 = g->add_graph_input("i1", pos++, rand()&0xF); // 2
  i2.set_bits(1);
  auto i3 = g->add_graph_input("i2", pos++, rand()&0xF); // 3
  i3.set_bits(1);
  auto i4 = g->add_graph_input("i3", pos++, rand()&0xF); // 4
  i4.set_bits(1);

  auto o5 = g->add_graph_output("o0", pos++, rand()&0xF); // 5
  auto o6 = g->add_graph_output("o1", pos++, rand()&0xF); // 6
  auto o7 = g->add_graph_output("o2", pos++, rand()&0xF); // 7
  auto o8 = g->add_graph_output("o3", pos++, rand()&0xF); // 8

  auto c9 = g->create_node_const(1,3); //  9
  auto c10 = g->create_node_const(21,4); //  10
  auto c11 = g->create_node_const("xxx",3); //  11
  auto c12 = g->create_node_const("yyyy",4); // 12

  auto t13 = g->create_node_sub(sub_g->get_lgid()); // 13
  auto t14 = g->create_node_sub(sub_g->get_lgid()); // 14
  auto t15 = g->create_node_sub(sub_g->get_lgid()); // 15
  auto t16 = g->create_node_sub(sub_g->get_lgid()); // 16
  auto t17 = g->create_node_sub(sub_g->get_lgid()); // 17
  auto t18 = g->create_node_sub(sub_g->get_lgid()); // 18
  auto t19 = g->create_node_sub(sub_g->get_lgid()); // 19
  auto t20 = g->create_node_sub(sub_g->get_lgid()); // 20
  auto t21 = g->create_node_sub(sub_g->get_lgid()); // 21
  auto t22 = g->create_node_sub(sub_g->get_lgid()); // 22
  (void)t22; // Disconnected node. Source of bug when fully disconnected
  auto t23 = g->create_node_sub(sub_g->get_lgid()); // 23

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
  // node_debug_name:
  //
  //     1i    1i    1i    1i   11c 25g 12c  13c   14c
  //       \  /       \   /  \    \  | /     |   /   \
  //        15g        16g    17g  18g       19g    20g  24g
  //        | \       / \           \       /  \   /   \
  //        |  \     /   \           \     22   21g    23g
  //        |   \   /     \           \  /
  //        2o   2o       2o           2o
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

  std::vector<std::string> fwd;
  int conta=0;
  //for(const auto node : g->forward()) {
  auto iter = g->forward();
  for(auto it = iter.begin() ; it!= iter.end() ; ++it) {
    auto node = *it;
    //fmt::print(" fwd:{}\n", node.debug_name());
    fwd.emplace_back(node.debug_name());
    conta++;
  }
  if (conta!=16) {
    fmt::print("ERROR. expected 16 nodes in forward traversal. Found {}\n",conta);
    failed = true;
  }

  std::vector<std::string> bwd;
  conta =0;
  for(const auto node : g->backward()) {
    //fmt::print(" bwd:{}\n", node.debug_name());
    bwd.emplace_back(node.debug_name());
    conta++;
  }
  if (conta!=16) {
    fmt::print("ERROR. expected 16 nodes in backward traversal. Found {}\n",conta);
    failed = true;
  }

  std::vector<std::string> fast;
  conta =0;
  for(const auto node : g->fast()) {
    fast.emplace_back(node.debug_name());
    conta++;
  }

  fmt::print("fwd :");
  for(auto txt:fwd)
    fmt::print(" {}",txt);
  fmt::print("\n");

  fmt::print("bwd :");
  for(auto txt:bwd)
    fmt::print(" {}",txt);
  fmt::print("\n");

  fmt::print("fast:");
  for(auto txt:fast)
    fmt::print(" {}",txt);
  fmt::print("\n");

  std::sort(fwd.begin() , fwd.end());
  std::sort(bwd.begin() , bwd.end());
  std::sort(fast.begin(), fast.end());

  auto fwd_it  = fwd.begin();
  auto bwd_it  = bwd.begin();

  while(fwd_it != fwd.end()) {
    if (*bwd_it != *fwd_it) {
      fmt::print("missmatch bwd {} vs fwd {}\n",*bwd_it, *fwd_it);
      failed = true;
    }
    fwd_it++;
    bwd_it++;
    if (fwd_it == fwd.end() && bwd_it == bwd.end())
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
  }
}

int main() {

#if 1
  for(int i=0;i<20;i++) {
    simple();
    if (failed)
      return -3;
  }
#endif

  int n = 20;
  generate_graphs(n);

#if 1
  if(!fwd(n)) {
    failed = true;
  }
#endif

  if(!bwd(n)) {
    failed = true;
  }

  return failed ? 1 : 0;
}
