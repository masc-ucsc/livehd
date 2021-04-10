//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <set>

#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "mmap_tree.hpp"
#include "spmc.hpp"
#include "thread_pool.hpp"

bool failed = false;

//#define VERBOSE
//#define VERBOSE2
//#define VERBOSE3

absl::flat_hash_map<Node::Compact, int> test_order;
int                                     test_order_sequence;
void                                    setup_test_order() {
  test_order.clear();
  test_order_sequence = 1;
}

void check_test_order(Lgraph *top) {
  for (auto node : top->fast(true)) {
    if (node.is_type_sub_present())
      continue;

    if (node.is_type_loop_last())
      continue;

    auto it_node = test_order.find(node.get_compact());
    if (it_node == test_order.end()) {
      fmt::print("ERROR: missing node:{}\n", node.debug_name());
      I(false);
    }

    int      max_input = 0;
    Node_pin max_input_pin;
    for (auto edge : node.inp_edges()) {
      auto it = test_order.find(edge.driver.get_node().get_compact());
      if (it == test_order.end())
        continue;
      if (it->second < max_input)
        continue;

      max_input     = it->second;
      max_input_pin = edge.driver;
    }
    if (max_input > it_node->second) {
      fmt::print("ERROR: wrong order node:{} l:{} p:{} is earlier than pin:{} of node:{} l:{} p:{}\n",
                 node.debug_name(),
                 node.get_hidx().level,
                 node.get_hidx().pos,
                 max_input_pin.debug_name(),
                 max_input_pin.get_node().debug_name(),
                 max_input_pin.get_node().get_hidx().level,
                 max_input_pin.get_node().get_hidx().pos);
      I(false);
    }
  }

  top->sync();
  delete top;
}

// performs Topological Sort on a given DAG
void do_fwd_traversal(Lgraph *lg, const std::string &name) {
  {
    Lbench b("core.ITER_" + name + "_fast");

    setup_test_order();
    for (auto node : lg->fast(true)) {
      I(!node.is_graph_io());
      // fmt::print("fast visiting {} l:{} p:{}\n", node.debug_name(),node.get_hidx().level,node.get_hidx().pos);
      // node.dump();
      I(test_order.find(node.get_compact()) == test_order.end());
      test_order[node.get_compact()] = test_order_sequence++;
    }
  }
  {
    Lbench b("core.ITER_" + name + "_fwd");

    setup_test_order();
    for (auto node : lg->forward(true)) {
      I(!node.is_graph_io());
      // fmt::print("fwd  visiting {} l:{} p:{}\n", node.debug_name(),node.get_hidx().level,node.get_hidx().pos);
      I(test_order.find(node.get_compact()) == test_order.end());
      test_order[node.get_compact()] = test_order_sequence++;
    }
  }
}

#define SIZE_BASE 1000

void generate_graphs(int n) {
  unsigned int rseed = 123;

  for (int i = 0; i < n; i++) {
    std::string                    gname = "test_" + std::to_string(i);
    Lgraph *                       g     = Lgraph::create("lgdb_iter_test", gname, "test");
    std::vector<Node_pin::Compact> spins;
    std::vector<Node_pin::Compact> dpins;

    int inps = 10 + rand_r(&rseed) % SIZE_BASE;
    for (int j = 0; j < inps; j++) {
      auto pin = g->add_graph_input("i" + std::to_string(j), 1 + j, 1);
      dpins.push_back(pin.get_compact());
    }

    int outs = 10 + rand_r(&rseed) % SIZE_BASE;
    for (int j = 0; j < outs; j++) {
      auto pin = g->add_graph_output("o" + std::to_string(j), 1 + inps + j, 1);
      spins.push_back(pin.get_compact());
      dpins.push_back(g->get_graph_output_driver_pin(("o" + std::to_string(j))).get_compact());
    }

    int nnodes = SIZE_BASE + rand_r(&rseed) % (SIZE_BASE * 10);
    for (int j = 0; j < nnodes; j++) {  // Simple output nodes
      auto     node = g->create_node();
      Ntype_op op   = (Ntype_op)(1 + (rand_r(&rseed) % (int)Ntype_op::Mux));  // regular node types range
      node.set_type(op);
      dpins.push_back(node.setup_driver_pin().get_compact());
      spins.push_back(node.setup_sink_pin_raw(0).get_compact());
    }

    int const_nodes = SIZE_BASE / 10 + rand_r(&rseed) % SIZE_BASE;
    for (int j = 0; j < const_nodes; j++) {  // Simple output nodes
      auto node = g->create_node_const(Lconst(rand_r(&rseed) & 0xFF));
      dpins.push_back(node.setup_driver_pin().get_compact());
    }

    int cnodes = SIZE_BASE / 10 + rand_r(&rseed) % (SIZE_BASE * 10);
    for (int j = 0; j < cnodes; j++) {  // complex nodes
      auto node = g->create_node(Ntype_op::CompileErr);
      auto d1   = rand_r(&rseed) % 3;
      auto s1   = rand_r(&rseed) % 6;
      dpins.push_back(node.setup_driver_pin_raw(d1).get_compact());
      spins.push_back(node.setup_sink_pin_raw(s1).get_compact());
      if (rand_r(&rseed) & 1) {
        auto d2 = rand_r(&rseed) % 3;
        auto s2 = rand_r(&rseed) % 6;
        if (d1 != d2)
          dpins.push_back(node.setup_driver_pin_raw(d2).get_compact());
        if (s1 != s2)
          spins.push_back(node.setup_sink_pin_raw(s2).get_compact());
      }
    }

    int                                                                  nedges = SIZE_BASE * 4 + rand_r(&rseed) % (SIZE_BASE * 8);
    absl::flat_hash_set<std::pair<Node_pin::Compact, Node_pin::Compact>> edges;
    for (int j = 0; j < nedges; j++) {
      int               counter = 0;
      Node_pin::Compact src;
      Node_pin::Compact dst;
      do {
        do {
          // Loop to always link forward (avoid loops)
          src = dpins[rand_r(&rseed) % (dpins.size())];
          dst = spins[rand_r(&rseed) % (spins.size())];

          Node_pin dpin(g, src);
          Node_pin spin(g, dst);
          if (dpin.is_graph_output())
            continue;
          if (spin.is_graph_input())
            continue;

          bool allow_comb_looop = false;
          if (allow_comb_looop) {
            break;
          }

          if (i & 1) {
            if (spin.get_node().get_compact().get_nid() > dpin.get_node().get_compact().get_nid())
              break;
          } else {
            if (spin.get_node().get_compact().get_nid() < dpin.get_node().get_compact().get_nid())
              break;
          }
        } while (true);

        counter++;
      } while (edges.find(std::make_pair(src, dst)) != edges.end() && counter < (SIZE_BASE * 10));

      if (counter >= (SIZE_BASE * 10))
        break;

      if (edges.find(std::make_pair(src, dst)) != edges.end())
        break;

      Node_pin dpin(g, src);
      Node_pin spin(g, dst);
      if (dpin.get_node() == spin.get_node())
        continue;  // No self-loops

      edges.insert(std::make_pair(src, dst));
      I(!spin.is_connected(dpin));  // no edge
      g->add_edge(dpin, spin);
      I(spin.is_connected(dpin));  //    edge
    }
  }
}

bool fwd(int n) {
  for (int i = 0; i < n; i++) {
    std::string gname = "test_" + std::to_string(i);
    Lgraph *    g     = Lgraph::open("lgdb_iter_test", gname);
    if (g == nullptr)
      return false;

    setup_test_order();

    fmt::print("FWD {}\n", gname);
    do_fwd_traversal(g, "fwd" + std::to_string(i + 1));

    check_test_order(g);
  }

  return true;
}

bool bwd(int n) {
  for (int i = 0; i < n; i++) {
    std::string gname = "test_" + std::to_string(i);
    Lgraph *    g     = Lgraph::open("lgdb_iter_test", gname);
    if (g == 0)
      return false;

    // g->dump();
    // fmt::print("----------------------\n");
    absl::flat_hash_set<Node::Compact> visited;
    for (auto node : g->backward()) {
      // fmt::print(" bwd {}\n", node.debug_name());

      visited.insert(node.get_compact());

      if (!node.is_type_loop_last() && node.get_type_op() != Ntype_op::IO) {
        // check if all incoming edges were visited
        for (auto &out : node.out_edges()) {
          if (!out.sink.get_node().is_type_loop_last() && out.sink.get_node().get_type_op() != Ntype_op::IO) {
            if (visited.find(out.sink.get_node().get_compact()) == visited.end()) {
              fmt::print("bwd failed for lgraph node:{} bwd:{}\n", node.debug_name(), out.sink.get_node().debug_name());
              I(false);
              return false;
            }
          }
        }
      }
    }
    visited.clear();
    for (auto node : g->backward(true)) {
      // fmt::print(" bwd {}\n", node.debug_name());

      visited.insert(node.get_compact());

      if (!node.is_type_loop_last() && node.get_type_op() != Ntype_op::IO) {
        // check if all incoming edges were visited
        for (auto &out : node.out_edges()) {
          if (!out.sink.get_node().is_type_loop_last() && out.sink.get_node().get_type_op() != Ntype_op::IO) {
            if (visited.find(out.sink.get_node().get_compact()) == visited.end()) {
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
  std::string gname   = "top_0";
  Lgraph *    g0      = Lgraph::create("lgdb_iter_test", "g0", "test");
  auto &      sfuture = g0->ref_library()->setup_sub("future", "test");
  if (!sfuture.has_pin("fut_i"))
    sfuture.add_input_pin("fut_i", 10);
  if (!sfuture.has_pin("fut_o"))
    sfuture.add_output_pin("fut_o", 11);
  g0->ref_library()->sync();

  Lgraph *s0 = Lgraph::create("lgdb_iter_test", "s0", "test");
  Lgraph *s1 = Lgraph::create("lgdb_iter_test", "s1", "test");
  Lgraph *s2 = Lgraph::create("lgdb_iter_test", "s2", "test");

  auto g0_i_pin = g0->add_graph_input("g0_i", 1, 0);
  auto g0_o_pin = g0->add_graph_output("g0_o", 2, 0);

  auto s0_i_pin = s0->add_graph_input("s0_i", 3, 0);
  auto s0_o_pin = s0->add_graph_output("s0_o", 4, 0);

  auto s1_i_pin = s1->add_graph_input("s1_i", 5, 0);
  auto s1_o_pin = s1->add_graph_output("s1_o", 6, 0);

  auto s2_i_pin = s2->add_graph_input("s2_i", 7, 0);
  auto s2_o_pin = s2->add_graph_output("s2_o", 8, 0);
  (void)s2_o_pin;  // disconnected

  auto g0_node0 = g0->create_node(Ntype_op::Or);
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

  auto s0_node = s0->create_node(Ntype_op::Or);
  auto s1_node = s1->create_node(Ntype_op::Or);
  auto s2_node = s2->create_node(Ntype_op::Or);

  // g0
  g0->add_edge(g0_i_pin, g0_node0.setup_sink_pin());
  g0->add_edge(g0_node0.setup_driver_pin(), g0_node1.setup_sink_pin("s0_i"));
  g0->add_edge(g0_node1.setup_driver_pin("s0_o"), g0_node2.setup_sink_pin("s1_i"));
  g0->add_edge(g0_node2.setup_driver_pin("s1_o"), g0_o_pin);
  g0->add_edge(g0_node2.setup_driver_pin("s1_o"), g0_node3.setup_sink_pin("fut_i"));
  g0->add_edge(g0_node3.setup_driver_pin("fut_o"), g0_node4.setup_sink_pin("s2_i"));
  g0->add_edge(g0_node4.setup_driver_pin("s2_o"), g0_node5.setup_sink_pin("s2_i"));

  // s0
  s0->add_edge(s0_i_pin, s0_node.setup_sink_pin());
  s0->add_edge(s0_node.setup_driver_pin(), s0_o_pin);

  // s1
  s1->add_edge(s1_i_pin, s1_node.setup_sink_pin());
  s1->add_edge(s1_node.setup_driver_pin(), s1_o_pin);

  // s2
  s2->add_edge(s2_i_pin, s2_node.setup_sink_pin());

  setup_test_order();

  do_fwd_traversal(g0, "simple_line");

  check_test_order(g0);

  delete s0;
  delete s1;
  delete s2;
}

void simple(int num) {
  std::string gname = "simple_iter";
  Lgraph *    g     = Lgraph::create("lgdb_iter_test", gname, "test");
  Lgraph *    sub_g = Lgraph::create("lgdb_iter_test", "sub", "test");

  for (int i = 0; i < 256; i++) {
    // Disconnected IOs from 1000-1512
    sub_g->add_graph_input("di" + std::to_string(i), 1000 + i + 1, 0);
    sub_g->add_graph_output("do" + std::to_string(i), 1000 + 256 + i + 1, 0);

    // Connected IOs from 1-512
    auto ipin = sub_g->add_graph_input("i" + std::to_string(i), i + 1, 0);
    auto opin = sub_g->add_graph_output("o" + std::to_string(i), 256 + i + 1, 0);
    auto node = sub_g->create_node(Ntype_op::Or);
    sub_g->add_edge(ipin, node.setup_sink_pin());
    sub_g->add_edge(node.setup_driver_pin(), opin);
  }
#ifndef NDEBUG
  g->ref_library()->sync();  // Not needed, but nice to debug/read the Graph_library
#endif

  int  pos = 1;                                   // Start with pos 1
  auto i1  = g->add_graph_input("i0", pos++, 0);  // 1
  i1.set_bits(1);
  auto i2 = g->add_graph_input("i1", pos++, rand() & 0xF);  // 2
  i2.set_bits(1);
  auto i3 = g->add_graph_input("i2", pos++, rand() & 0xF);  // 3
  i3.set_bits(1);
  auto i4 = g->add_graph_input("i3", pos++, rand() & 0xF);  // 4
  i4.set_bits(1);

  auto o5 = g->add_graph_output("o0", pos++, rand() & 0xF);  // 5
  auto o6 = g->add_graph_output("o1", pos++, rand() & 0xF);  // 6
  auto o7 = g->add_graph_output("o2", pos++, rand() & 0xF);  // 7
  auto o8 = g->add_graph_output("o3", pos++, rand() & 0xF);  // 8

  auto c9  = g->create_node_const(Lconst(1));         //  9
  auto c10 = g->create_node_const(Lconst(21));        //  10
  auto c11 = g->create_node_const(Lconst("0bxxx"));   //  11
  auto c12 = g->create_node_const(Lconst("0b????"));  // 12

  auto t13 = g->create_node_sub(sub_g->get_lgid());  // 13
  t13.set_name("13g");
  auto t14 = g->create_node_sub(sub_g->get_lgid());  // 14
  t14.set_name("14g");
  auto t15 = g->create_node_sub(sub_g->get_lgid());  // 15
  t15.set_name("15g");
  auto t16 = g->create_node_sub(sub_g->get_lgid());  // 16
  t16.set_name("16g");
  auto t17 = g->create_node_sub(sub_g->get_lgid());  // 17
  t17.set_name("17g");
  auto t18 = g->create_node_sub(sub_g->get_lgid());  // 18
  t18.set_name("18g");
  auto t19 = g->create_node_sub(sub_g->get_lgid());  // 19
  t19.set_name("19g");
  auto t20 = g->create_node_sub(sub_g->get_lgid());  // 20
  t20.set_name("20g");
  auto t21 = g->create_node_sub(sub_g->get_lgid());  // 21
  t21.set_name("21g");
  auto t22 = g->create_node_sub(sub_g->get_lgid());  // 22
  t22.set_name("22g");
  auto t23 = g->create_node_sub(sub_g->get_lgid());  // 23
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

  g->add_edge(i1, t13.setup_sink_pin(fmt::format("i{}", (random() & 0xFF))));
  g->add_edge(i2, t13.setup_sink_pin(fmt::format("i{}", (random() & 0xFF))));

  g->add_edge(i3, t14.setup_sink_pin(fmt::format("i{}", (random() & 0xFF))));
  g->add_edge(i4, t14.setup_sink_pin(fmt::format("i{}", (random() & 0xFF))));
  g->add_edge(i4, t15.setup_sink_pin(fmt::format("i{}", (random() & 0xFF))));

  g->add_edge(c9.setup_driver_pin(), t16.setup_sink_pin(fmt::format("i{}", (random() & 0xFF))));
  g->add_edge(c10.setup_driver_pin(), t16.setup_sink_pin(fmt::format("i{}", (random() & 0xFF))));
  g->add_edge(t23.setup_driver_pin(fmt::format("o{}", (random() & 0xFF))),
              t16.setup_sink_pin(fmt::format("i{}", (random() & 0xFF))));

  g->add_edge(c11.setup_driver_pin(), t17.setup_sink_pin(fmt::format("i{}", (random() & 0xFF))));
  if (rand() & 1)
    g->add_edge(c12.setup_driver_pin(), t17.setup_sink_pin(fmt::format("di{}", (random() & 0xFF))));
  g->add_edge(c12.setup_driver_pin(), t17.setup_sink_pin(fmt::format("i{}", (random() & 0xFF))));
  g->add_edge(c12.setup_driver_pin(), t18.setup_sink_pin(fmt::format("i{}", (random() & 0xFF))));

  g->add_edge(t13.setup_driver_pin(fmt::format("o{}", +(random() & 0xFF))), o5);
  g->add_edge(t13.setup_driver_pin(fmt::format("o{}", (random() & 0xFF))), o6);
  if (rand() & 1)
    g->add_edge(t14.setup_driver_pin(fmt::format("do{}", (random() & 0xFF))), o6);
  g->add_edge(t14.setup_driver_pin(fmt::format("o{}", (random() & 0xFF))), o6);
  g->add_edge(t14.setup_driver_pin(fmt::format("o{}", (random() & 0xFF))), o7);

  g->add_edge(t17.setup_driver_pin(fmt::format("o{}", (random() & 0xFF))),
              t20.setup_sink_pin(fmt::format("i{}", (random() & 0xFF))));
  if (rand() & 1)
    g->add_edge(t17.setup_driver_pin(fmt::format("do{}", (random() & 0xFF))),
                t20.setup_sink_pin(fmt::format("i{}", (random() & 0xFF))));
  g->add_edge(t17.setup_driver_pin(fmt::format("o{}", (random() & 0xFF))),
              t19.setup_sink_pin(fmt::format("i{}", (random() & 0xFF))));
  g->add_edge(t18.setup_driver_pin(fmt::format("o{}", (random() & 0xFF))),
              t19.setup_sink_pin(fmt::format("i{}", (random() & 0xFF))));
  g->add_edge(t18.setup_driver_pin(fmt::format("o{}", (random() & 0xFF))),
              t21.setup_sink_pin(fmt::format("i{}", (random() & 0xFF))));

  g->add_edge(t16.setup_driver_pin(fmt::format("o{}", (random() & 0xFF))), o8);
  g->add_edge(t20.setup_driver_pin(fmt::format("o{}", (random() & 0xFF))), o8);

#ifdef VERBOSE
  for (const auto &node : g->fast()) {
    fmt::print("node:{}\n", node.debug_name());
    fmt::print("  inp_edges");
    for (const auto &edge : node.inp_edges()) {
      fmt::print("  {}", edge.driver.debug_name());
    }
    fmt::print("\n");
    fmt::print("  out_edges");
    for (const auto &edge : node.out_edges()) {
      fmt::print("  {}", edge.sink.debug_name());
    }
    fmt::print("\n");
  }
#endif

  setup_test_order();

  do_fwd_traversal(g, "simple" + std::to_string(num + 1));

  check_test_order(g);

  sub_g->sync();
  delete sub_g;
}

#if 0
class Decouple_forward_iterator {
protected:
  spmc256<Node> queue;

  std::thread  pth;
  bool running;

  void worker(Lgraph *lg, bool visit_sub) {
    for(auto node:lg->forward(visit_sub)) {
      while(true) {
        auto done = queue.enqueue(node);
        if (done)
          break;
      }
    }
    while(true) {
      Node inv;
      auto done = queue.enqueue(inv);
      if (done)
        break;
    }
    running = false;
  }

public:
  class Decl_iter {
  private:
    Decouple_forward_iterator *it;
    Node current_node;

  public:
    constexpr Decl_iter(Decouple_forward_iterator *_it) : it(_it) {}

    constexpr Decl_iter(const Decl_iter &i) :it        (i.it) { }

    constexpr Decl_iter &operator=(const Decl_iter &i) {
      it = i.it;

      return *this;
    }

    Decl_iter &operator++() {
      current_node = it->get_next();
    }

    bool operator!=(const Decl_iter &other) const {
      I(it == other.it);
      return current_node != other.current_node;
    }

    bool operator==(const Decl_iter &other) const {
      I(it == other.it);
      return current_node == other.current_node;
    }

    Node operator*() const { return current_node; }

    bool is_invalid() const { return current_node.is_invalid(); }
  };

  Decouple_forward_iterator(Lgraph *lg, bool visit_sub) {
    running = true;
    pth = std::thread([this, lg, visit_sub] { this->worker(lg, visit_sub); });
  }

  ~Decouple_forward_iterator() {
    if (running) {
      pthread_cancel(pth.native_handle());
      running = false;
    }
    if (pth.joinable())
      pth.join();
  }

  void get_next() {
    while(!queue.dequeue(current_node))
      ;
  }

  Node operator*() const { return current_node; }

  Decl_iter begin() const {
    Decl_iter dcit(this);
    dcit.get_next();
    return dcit;
  }

  Decl_iter end() const {
    Decl_iter dcit(this);
    return dcit;
  }
};
#endif

int main(int argc, char **argv) {
  if (argc == 3 || argc == 4) {
    int niters = 30;
    if (argc == 4) {
      niters = atoi(argv[3]);
    }

    fmt::print("benchmarking path:{} name:{} niters:{}\n", argv[1], argv[2], niters);
    auto *lg = Lgraph::open(argv[1], argv[2]);

    Lbench bench("fwd.custom");
    int    total = 0;
//#define ITER_MMAP 1
#define ITER_DIRECT 1
    //#define ITER_THREAD 1
    //#define ITER_VECTOR 1
    //#define ITER_VECTOR_CHECK_ORDER 1
    //#define ITER_TREE 1

#ifdef ITER_VECTOR

#ifdef BUCKET_TOPO_SORT
    std::vector<size_t> min_level;
    min_level.resize(lg->size());

    int n_loop_breakers = 0;
    int n_loop_others   = 0;

    size_t max_level = 0;
    for (auto node : lg->fast()) {
      if (node.is_type_loop_last())
        continue;

      size_t level              = 0u;
      bool   all_input_breakers = true;
      for (const auto &dpin : node.inp_drivers()) {
        auto d_node = dpin.get_node();

        if (d_node.is_type_loop_last() || dpin.is_graph_input()) {
          if (min_level[d_node.get_nid()] == 0) {
            // FIXME: visit the node first
          }
        } else {
          all_input_breakers = false;
          level              = std::max(min_level[d_node.get_nid()], level);
        }
      }
      if (!all_input_breakers) {
        ++level;
        for (const auto &e : node.out_edges()) {
          auto pos       = e.sink.get_node_nid();
          auto l         = std::max(min_level[pos], level + 1);
          min_level[pos] = l;
          max_level      = std::max(max_level, l);
        }
      }

      if (level == 0) {
        // FIXME: const or loop breaker. visit now
        ++n_loop_breakers;
      } else {
        min_level[node.get_nid()] = level;
        max_level                 = std::max(max_level, level);
        ++n_loop_others;
      }
    }
    fmt::print("loop breakers:{} others:{} max:{}\n", n_loop_breakers, n_loop_others, max_level);
    std::vector<size_t> histogram;
    for (auto v : min_level) {
      if (v >= histogram.size())
        histogram.resize(v + 1);

      histogram[v]++;
    }
    for (auto i = 0u; i < histogram.size(); ++i) {
      fmt::print("  {} has {}\n", i, histogram[i]);
    }
#endif

    std::vector<Node::Compact_class> fwd_order;
    for (auto node : lg->forward()) {
      fwd_order.emplace_back(node.get_compact_class());
    }
#endif
#ifdef ITER_REBUILD
    auto *                                                        tlg = Lgraph::create(argv[1], "topo_sorted", "-");
    absl::flat_hash_map<Node::Compact_class, Node::Compact_class> lg2tlg;
    for (auto node : lg->forward()) {
      auto tnode = tlg->create_node(node);

      lg2tlg.emplace(node.get_compact_class(), tnode.get_compact_class());

      for (auto &e : node.inp_edges()) {
        auto it = lg2tlg.find(e.driver.get_node().get_compact_class());
        if (it == lg2tlg.end())
          continue;
        auto tdpin = it->second.get_node(tlg).setup_driver_pin_raw(e.driver.get_pid());
        tnode.setup_sink_pin_raw(e.sink.get_pid()).connect_driver(tdpin);
      }
      for (auto &e : node.out_edges()) {
        auto it = lg2tlg.find(e.sink.get_node().get_compact_class());
        if (it == lg2tlg.end())
          continue;
        auto tspin = it->second.get_node(tlg).setup_sink_pin_raw(e.sink.get_pid());
        tnode.setup_driver_pin_raw(e.driver.get_pid()).connect_sink(tspin);
      }
    }
#endif
#ifdef ITER_TREE
    mmap_lib::tree<Node::Compact_class> fwd_order;
    Node                                invalid;
    fwd_order.set_root(invalid.get_compact_class());

    const auto root = fwd_order.get_root();
    for (auto node : lg->forward()) {
      fwd_order.add_child(root, node.get_compact_class());
    }
#endif
#ifdef ITER_MMAP
    mmap_lib::map<Node::Compact_class, Node::Compact_class> fwd_order;
    Node                                                    invalid;
    Node::Compact_class                                     first_cnode = invalid.get_compact_class();
    Node::Compact_class                                     last_cnode  = invalid.get_compact_class();
    for (auto node : lg->forward()) {
      if (last_cnode.is_invalid()) {
        first_cnode = node.get_compact_class();
        last_cnode  = node.get_compact_class();
      } else {
        auto n = node.get_compact_class();
        fwd_order.set(last_cnode, n);
        last_cnode = n;
      }
    }
#endif
    for (int i = 0; i < niters; ++i) {
#ifdef ITER_DIRECT
      for (auto node : lg->forward()) {
        auto op = node.get_type_op();
        if (Ntype::is_multi_driver(op))
          total += 1;
      }
#endif
#ifdef ITER_THREAD
      Decouple_forward_iterator it;
      it.restart(lg);

      while (true) {
        auto nid = it.get_next();
        if (nid == 0)
          break;
        auto node = Node::Compact_class(nid).get_node(lg);
        auto op   = node.get_type_op();
        if (Ntype::is_multi_driver(op))
          total += 1;
      }
#endif
#ifdef ITER_REBUILD
      for (auto node : tlg->fast()) {
        auto op = node.get_type_op();
        if (Ntype::is_multi_driver(op))
          total += 1;
      }
#endif
#ifdef ITER_TREE
      for (const auto &it : fwd_order.depth_preorder(fwd_order.get_root())) {
        auto node = fwd_order.get_data(it).get_node(lg);
        auto op   = node.get_type_op();
        if (Ntype::is_multi_driver(op))
          total += 1;
      }
#endif
#ifdef ITER_MMAP
      auto cnode = first_cnode;
      while (!cnode.is_invalid()) {
        auto node = cnode.get_node(lg);
        auto op   = node.get_type_op();
        if (Ntype::is_multi_driver(op))
          total += 1;

        const auto &it = fwd_order.find(cnode);
        if (it == fwd_order.end())
          break;
        cnode = it->second;
      }
#endif
#ifdef ITER_VECTOR
#ifdef ITER_VECTOR_CHECK_ORDER
      absl::flat_hash_set<Node::Compact_class> visited;
#endif

      for (const auto &cnode : fwd_order) {
        auto node = cnode.get_node(lg);
        if (node.is_invalid())
          continue;

#ifdef ITER_VECTOR_CHECK_ORDER
        visited.insert(cnode);
        for (const auto &e : node.inp_edges()) {
          if (visited.contains(e.driver.get_node().get_compact_class())) {
            continue;
          }
          if (e.driver.is_graph_io())
            continue;
          if (!node.is_type_sub() && !e.driver.get_node().is_type_sub()) {
            fmt::print("delayed\n");
            node.dump();
            e.driver.get_node().dump();
            break;
          }
        }
#endif

        auto op = node.get_type_op();
        if (Ntype::is_multi_driver(op))
          total += 1;
      }
#endif
    }

    fmt::print("total {}\n", total);
    return 0;
  }

  simple_line();

  simple(1);

  for (int i = 0; i < 40; i++) {
    simple(2);
    if (failed)
      return -3;
  }

  int n = 20;
  generate_graphs(n);

  failed |= fwd(n);

#if 0
  if(!bwd(n)) {
    failed = true;
  }
#endif

  return failed ? 1 : 0;
}
