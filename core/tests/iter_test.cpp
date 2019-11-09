//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgedgeiter.hpp"
#include "lgraph.hpp"

#include <set>

bool failed = false;

//#define VERBOSE
//#define VERBOSE2
//#define VERBOSE3


absl::flat_hash_map<Node::Compact,int> test_order;
int test_order_sequence;
void setup_test_order() {
  test_order.clear();
  test_order_sequence = 1;
}

void check_test_order(LGraph *top) {
  for(auto node:top->fast(true)) {
    if (node.is_type_sub_present())
      continue;

    if (node.is_type_loop_breaker())
      continue;

    auto it_node = test_order.find(node.get_compact());
    if (it_node == test_order.end()) {
      fmt::print("ERROR: missing node:{}\n",node.debug_name());
      I(false);
    }

    int max_input = 0;
    Node_pin max_input_pin;
    for(auto edge:node.inp_edges()) {
      auto it = test_order.find(edge.driver.get_node().get_compact());
      if (it==test_order.end())
       continue;
      if (it->second<max_input)
        continue;

      max_input = it->second;
      max_input_pin = edge.driver;
    }
    if (max_input>it_node->second) {
      fmt::print("ERROR: wrong order node:{} is earlier than pin:{}\n",node.debug_name(), max_input_pin.debug_name());
      I(false);
    }
  }
}

// performs Topological Sort on a given DAG
void do_fwd_traversal(LGraph *lg) {

  for (auto node : lg->forward(true)) {
    I(!node.is_graph_io());
    //fmt::print("visiting {}\n", node.debug_name());
    I(test_order.find(node.get_compact()) == test_order.end());
    test_order[node.get_compact()] = test_order_sequence++;
  }
}


void generate_graphs(int n) {

  unsigned int rseed = 123;

  for(int i = 0; i < n; i++) {
    std::string           gname = "test_" + std::to_string(i);
    LGraph *              g     = LGraph::create("lgdb_iter_test", gname, "test");
    std::vector<Node_pin::Compact> spins;
    std::vector<Node_pin::Compact> dpins;

    int inps = 10 + rand_r(&rseed) % 100;
    for(int j = 0; j < inps; j++) {
      auto pin = g->add_graph_input("i" + std::to_string(j), 1+j, 1);
      dpins.push_back(pin.get_compact());
    }

    int outs = 10 + rand_r(&rseed) % 100;
    for(int j = 0; j < outs; j++) {
      auto pin = g->add_graph_output("o" + std::to_string(j), 1+inps+j,1);
      spins.push_back(pin.get_compact());
      dpins.push_back(g->get_graph_output_driver(("o" + std::to_string(j))).get_compact());
    }

    int nnodes = 100 + rand_r(&rseed) % 1000;
    for(int j = 0; j < nnodes; j++) { // Simple output nodes
      auto node = g->create_node();
      Node_Type_Op op  = (Node_Type_Op)(1+(rand_r(&rseed) % ShiftLeft_Op)); // regular node types range
      node.set_type(op);
      dpins.push_back(node.setup_driver_pin(0).get_compact());
      spins.push_back(node.setup_sink_pin(0).get_compact());
    }

    int const_nodes = 10 + rand_r(&rseed) % 100;
    for(int j = 0; j < const_nodes; j++) { // Simple output nodes
      auto node = g->create_node_const(rand_r(&rseed) & 0xFF);
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
        do{
          // Loop to always link forward (avoid loops)
          src = dpins[rand_r(&rseed) % (dpins.size())];
          dst = spins[rand_r(&rseed) % (spins.size())];

          Node_pin dpin(g,src);
          Node_pin spin(g,dst);
          if (dpin.is_graph_output())
            continue;
          if (spin.is_graph_input())
            continue;

          if (i&1) {
            if (spin.get_node().get_compact().get_nid() > dpin.get_node().get_compact().get_nid())
              break;
          }else{
            if (spin.get_node().get_compact().get_nid() < dpin.get_node().get_compact().get_nid())
              break;
          }
        }while (true);

        counter++;
      } while(edges.find(std::make_pair(src, dst)) != edges.end() && counter < 1000);

      if (counter>=1000)
        break;

      if (edges.find(std::make_pair(src, dst)) != edges.end())
        break;

      Node_pin dpin(g,src);
      Node_pin spin(g,dst);
      if (dpin.get_node() == spin.get_node())
        continue; // No self-loops

      edges.insert(std::make_pair(src, dst));
      I(!g->has_edge(dpin, spin));
      g->add_edge(dpin, spin);
      I(g->has_edge(dpin, spin));
    }

    g->sync();
  }
}

bool fwd(int n) {

  for(int i = 0; i < n; i++) {
    std::string gname = "test_" + std::to_string(i);
    LGraph *    g     = LGraph::open("lgdb_iter_test", gname);
    if(g == nullptr)
      return false;

    setup_test_order();

    fmt::print("FWD {}\n", gname);
    do_fwd_traversal(g);

    check_test_order(g);
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
    visited.clear();
    for(auto node : g->backward(true)) {
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

void simple_line() {


  std::string gname = "top_0";
  LGraph *g0 = LGraph::create("lgdb_iter_test", "g0", "test");
  auto &sfuture = g0->ref_library()->setup_sub("future", "test");
  if (!sfuture.has_pin("fut_i"))
    sfuture.add_input_pin("fut_i",10);
  if (!sfuture.has_pin("fut_o"))
    sfuture.add_output_pin("fut_o",11);
  g0->ref_library()->sync();

  LGraph *s0 = LGraph::create("lgdb_iter_test", "s0", "test");
  LGraph *s1 = LGraph::create("lgdb_iter_test", "s1", "test");
  LGraph *s2 = LGraph::create("lgdb_iter_test", "s2", "test");

  auto g0_i_pin = g0->add_graph_input("g0_i", 1, 0);
  auto g0_o_pin = g0->add_graph_output("g0_o", 2, 0);

  auto s0_i_pin = s0->add_graph_input("s0_i", 3, 0);
  auto s0_o_pin = s0->add_graph_output("s0_o", 4, 0);

  auto s1_i_pin = s1->add_graph_input("s1_i", 5, 0);
  auto s1_o_pin = s1->add_graph_output("s1_o", 6, 0);

  auto s2_i_pin = s2->add_graph_input("s2_i", 7, 0);
  auto s2_o_pin = s2->add_graph_output("s2_o", 8, 0);
  (void)s2_o_pin; // disconnected


  auto g0_node0 = g0->create_node(Or_Op);
  g0_node0.set_name("g0_node0");
  auto g0_node1 = g0->create_node_sub(s0->get_lgid());
  g0_node1.set_name("g0_node1");
  auto g0_node2 = g0->create_node_sub(s1->get_lgid());
  g0_node2.set_name("g0_node2");
  auto g0_node3 = g0->create_node_sub("future");
  g0_node3.set_name("g0_future");
  auto g0_node4 = g0->create_node_sub(s2->get_lgid());
  g0_node4.set_name("g0_disc0");
  auto g0_node5 = g0->create_node_sub(s2->get_lgid());
  g0_node5.set_name("g0_disc1");

  auto s0_node = s0->create_node(Or_Op);
  auto s1_node = s1->create_node(Or_Op);
  auto s2_node = s2->create_node(Or_Op);

  // g0
  g0->add_edge(g0_i_pin, g0_node0.setup_sink_pin(0));
  g0->add_edge(g0_node0.setup_driver_pin(0), g0_node1.setup_sink_pin("s0_i"));
  g0->add_edge(g0_node1.setup_driver_pin("s0_o"), g0_node2.setup_sink_pin("s1_i"));
  g0->add_edge(g0_node2.setup_driver_pin("s1_o"), g0_o_pin);
  g0->add_edge(g0_node2.setup_driver_pin("s1_o"), g0_node3.setup_sink_pin("fut_i"));
  g0->add_edge(g0_node3.setup_driver_pin("fut_o"), g0_node4.setup_sink_pin("s2_i"));
  g0->add_edge(g0_node4.setup_driver_pin("s2_o"), g0_node5.setup_sink_pin("s2_i"));

  // s0
  s0->add_edge(s0_i_pin, s0_node.setup_sink_pin(0));
  s0->add_edge(s0_node.setup_driver_pin(0), s0_o_pin);

  // s1
  s1->add_edge(s1_i_pin, s1_node.setup_sink_pin(0));
  s1->add_edge(s1_node.setup_driver_pin(0), s1_o_pin);

  // s2
  s2->add_edge(s2_i_pin, s2_node.setup_sink_pin(0));

  setup_test_order();

  do_fwd_traversal(g0);

  check_test_order(g0);
}

void simple() {
  std::string gname = "simple_iter";
  LGraph *g     = LGraph::create("lgdb_iter_test", gname, "test");
  LGraph *sub_g = LGraph::create("lgdb_iter_test", "sub", "test");

  for (int i = 0; i < 256; i++) {
    // Disconnected IOs from 1000-1512
    sub_g->add_graph_input("di" + std::to_string(i), 1000+i+1, 0);
    sub_g->add_graph_output("do" + std::to_string(i), 1000+256+i+1, 0);

    // Connected IOs from 1-512
    auto ipin = sub_g->add_graph_input("i" + std::to_string(i), i+1, 0);
    auto opin = sub_g->add_graph_output("o" + std::to_string(i), 256+i+1, 0);
    auto node = sub_g->create_node(Or_Op);
    sub_g->add_edge(ipin, node.setup_sink_pin(0));
    sub_g->add_edge(node.setup_driver_pin(rand()&1), opin);
  }
#ifndef NDEBUG
  g->ref_library()->sync(); // Not needed, but nice to debug/read the Graph_library
#endif

  int pos = 1; // Start with pos 1
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

  auto c9 = g->create_node_const(1); //  9
  auto c10 = g->create_node_const(21); //  10
  auto c11 = g->create_node_const("xxx",3); //  11
  auto c12 = g->create_node_const("yyyy",4); // 12

  auto t13 = g->create_node_sub(sub_g->get_lgid()); // 13
  t13.set_name("13g");
  auto t14 = g->create_node_sub(sub_g->get_lgid()); // 14
  t14.set_name("14g");
  auto t15 = g->create_node_sub(sub_g->get_lgid()); // 15
  t15.set_name("15g");
  auto t16 = g->create_node_sub(sub_g->get_lgid()); // 16
  t16.set_name("16g");
  auto t17 = g->create_node_sub(sub_g->get_lgid()); // 17
  t17.set_name("17g");
  auto t18 = g->create_node_sub(sub_g->get_lgid()); // 18
  t18.set_name("18g");
  auto t19 = g->create_node_sub(sub_g->get_lgid()); // 19
  t19.set_name("19g");
  auto t20 = g->create_node_sub(sub_g->get_lgid()); // 20
  t20.set_name("20g");
  auto t21 = g->create_node_sub(sub_g->get_lgid()); // 21
  t21.set_name("21g");
  auto t22 = g->create_node_sub(sub_g->get_lgid()); // 22
  t22.set_name("22g");
  auto t23 = g->create_node_sub(sub_g->get_lgid()); // 23
  t23.set_name("23g");

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

  g->add_edge(i1, t13.setup_sink_pin(fmt::format("i{}",(random()&0xFF))));
  g->add_edge(i2, t13.setup_sink_pin(fmt::format("i{}",(random()&0xFF))));

  g->add_edge(i3, t14.setup_sink_pin(fmt::format("i{}",(random()&0xFF))));
  g->add_edge(i4, t14.setup_sink_pin(fmt::format("i{}",(random()&0xFF))));
  g->add_edge(i4, t15.setup_sink_pin(fmt::format("i{}",(random()&0xFF))));

  g->add_edge(c9.setup_driver_pin() , t16.setup_sink_pin(fmt::format("i{}",(random()&0xFF))));
  g->add_edge(c10.setup_driver_pin(), t16.setup_sink_pin(fmt::format("i{}",(random()&0xFF))));
  g->add_edge(t23.setup_driver_pin(fmt::format("o{}",(random()&0xFF))), t16.setup_sink_pin(fmt::format("i{}",(random()&0xFF))));

  g->add_edge(c11.setup_driver_pin(), t17.setup_sink_pin(fmt::format("i{}",(random()&0xFF))));
  if (rand()&1)
    g->add_edge(c12.setup_driver_pin(), t17.setup_sink_pin(fmt::format("di{}",(random()&0xFF))));
  g->add_edge(c12.setup_driver_pin(), t17.setup_sink_pin(fmt::format("i{}",(random()&0xFF))));
  g->add_edge(c12.setup_driver_pin(), t18.setup_sink_pin(fmt::format("i{}",(random()&0xFF))));

  g->add_edge(t13.setup_driver_pin(fmt::format("o{}",+(random()&0xFF))), o5);
  g->add_edge(t13.setup_driver_pin(fmt::format("o{}",(random()&0xFF))), o6);
  if (rand()&1)
    g->add_edge(t14.setup_driver_pin(fmt::format("do{}",(random()&0xFF))), o6);
  g->add_edge(t14.setup_driver_pin(fmt::format("o{}",(random()&0xFF))), o6);
  g->add_edge(t14.setup_driver_pin(fmt::format("o{}",(random()&0xFF))), o7);

  g->add_edge(t17.setup_driver_pin(fmt::format("o{}",(random()&0xFF))), t20.setup_sink_pin(fmt::format("i{}",(random()&0xFF))));
  if (rand()&1)
    g->add_edge(t17.setup_driver_pin(fmt::format("do{}",(random()&0xFF))), t20.setup_sink_pin(fmt::format("i{}",(random()&0xFF))));
  g->add_edge(t17.setup_driver_pin(fmt::format("o{}",(random()&0xFF))), t19.setup_sink_pin(fmt::format("i{}",(random()&0xFF))));
  g->add_edge(t18.setup_driver_pin(fmt::format("o{}",(random()&0xFF))), t19.setup_sink_pin(fmt::format("i{}",(random()&0xFF))));
  g->add_edge(t18.setup_driver_pin(fmt::format("o{}",(random()&0xFF))), t21.setup_sink_pin(fmt::format("i{}",(random()&0xFF))));

  g->add_edge(t16.setup_driver_pin(fmt::format("o{}",(random()&0xFF))), o8);
  g->add_edge(t20.setup_driver_pin(fmt::format("o{}",(random()&0xFF))), o8);

#ifdef VERBOSE
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
#endif

  setup_test_order();

  do_fwd_traversal(g);

  check_test_order(g);
}

int main() {

#if 1
  simple_line();

  simple();

  for(int i=0;i<40;i++) {
    simple();
    if (failed)
      return -3;
  }
#endif

  int n = 20;
  generate_graphs(n);

  if(!fwd(n)) {
    failed = true;
  }

#if 0
  if(!bwd(n)) {
    failed = true;
  }
#endif

  return failed ? 1 : 0;
}
