#include <chrono>
#include <iostream>

#include "absl/container/flat_hash_map.h"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

Lgraph* create_some_random_lgraph() {
  Lgraph* lg = Lgraph::create("lgdb_bench", "random", "-");

  I(false);

  return lg;
}

void populate_graph(Lgraph* lg) {
  (void)lg;
#if 0
  absl::flat_hash_map<Node::Compact, graph::Bi_adjacency_list::Vert> map2g2vertex;

  map2g2vertex[lg->get_graph_input_node().get_compact()]  = g2.insert_vert();
  map2g2vertex[lg->get_graph_output_node().get_compact()] = g2.insert_vert();

  for (auto node : lg->fast()) {
    auto i                           = g2.insert_vert();
    map2g2vertex[node.get_compact()] = i;
  }

  for (auto node : lg->fast()) {
    auto src_it = map2g2vertex.find(node.get_compact());
    assert(src_it != map2g2vertex.end());

    for (auto e : node.out_edges()) {
      // insert in graph
      auto dst_it = map2g2vertex.find(e.sink.get_node().get_compact());
      assert(dst_it != map2g2vertex.end());

      g2.insert_edge(src_it->second, dst_it->second);
    }
  }
#endif
}

void lgraph_counts(Lgraph* lg) {
  int nodes = 0;
  int edges = 0;
  for (auto node : lg->fast()) {
    (void)node;
    nodes++;
    for (auto e : node.out_edges()) {
      (void)e;
      edges++;
    }
  }
  fmt::print("Lgraph, nodes: {}, edges: {}\n", nodes, edges);
}

int traverse_lgraph_nodes(Lgraph* lg) {
  int i = 0;
  for (const auto& node : lg->fast()) {
    (void)node;
    i++;
  }
  return i;
}

int traverse_lgraph_in(Lgraph* lg) {
  int i = 0;
  for (const auto& node : lg->fast()) {
    (void)node;
    for (const auto& e : node.inp_edges()) {
      (void)e;
      i++;
    }
  }
  return i;
}

int traverse_lgraph_out(Lgraph* lg) {
  int i = 0;
  for (const auto& node : lg->fast()) {
    (void)node;
    for (const auto& e : node.out_edges()) {
      (void)e;
      i++;
    }
  }
  return i;
}

int main(int argc, char** argv) {
  fmt::print("benchmark the graph\n");

  Lgraph* lg;
  if (argc == 1) {
    lg = create_some_random_lgraph();
  } else if (argc == 3) {
    fmt::print("benchmark the graph lgdb:{} name:{}\n", argv[1], argv[2]);
    lg = Lgraph::open(argv[1], argv[2]);
  } else {
    fmt::print("usage:\n\t{} <lgdb> <lg_name>\n", argv[0]);
    exit(-2);
  }

  populate_graph(lg);

  lgraph_counts(lg);

  // 3. benchmark same traverse the graph in all the graphs
  fmt::print("Benchmark LiveHD graph\n");
  int  iterations = 10000;
  auto start      = std::chrono::high_resolution_clock::now();
  auto stop       = std::chrono::high_resolution_clock::now();
  auto duration   = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);
  int  x          = 0;
  int  micros     = 1000000;

  fmt::print("--------------------------Nodes--------------------\n");
  start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < iterations; i++) {
    x += traverse_lgraph_nodes(lg);
  }
  stop     = std::chrono::high_resolution_clock::now();
  duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);
  fmt::print("Traverse Lgraph {} times took {}s\n", iterations, duration.count() / micros);

  fmt::print("--------------------------Nodes+in--------------------\n");
  start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < iterations; i++) {
    x += traverse_lgraph_in(lg);
  }
  stop     = std::chrono::high_resolution_clock::now();
  duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);
  fmt::print("Traverse Lgraph {} times took {}s\n", iterations, duration.count() / micros);

  fmt::print("--------------------------Nodes+out--------------------\n");
  start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < iterations; i++) {
    x += traverse_lgraph_out(lg);
  }
  stop     = std::chrono::high_resolution_clock::now();
  duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);
  fmt::print("Traverse Lgraph {} times took {}s\n", iterations, duration.count() / micros);

  fmt::print("x:{} opt/check\n", x);

  return 0;
}
