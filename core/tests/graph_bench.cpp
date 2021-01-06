#include <chrono>
#include <iostream>

#include "Adjacency_list.hpp"
#include "absl/container/flat_hash_map.h"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

using namespace std::chrono;
using namespace std;

LGraph* create_some_random_lgraph() {
  LGraph* lg = LGraph::create("lgdb_bench", "random", "-");

  // 1. create a new LGraph

  return lg;
}

graph::Bi_adjacency_list g2;

void populate_graph(LGraph* lg) {
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
}

void lgraph_counts(LGraph* lg) {
  int nodes = 0, edges = 0;
  for (auto node : lg->fast()) {
    nodes++;
    for (auto e : node.out_edges()) {
      edges++;
    }
  }
  fmt::print("Lgraph, nodes: {}, edges: {}\n", nodes, edges);
}

void boosted_graph_counts(graph::Bi_adjacency_list* g2) {
  int nodes = 0, edges = 0;
  for (auto vert : g2->verts()) {
    nodes++;
    for (auto edge : g2->out_edges(vert)) {
      edges++;
    }
  }
  fmt::print("Boosted graph, nodes: {}, edges: {}\n", nodes, edges);
}

int traverse_lgraph_nodes(LGraph* lg) {
  int i = 0;
  for (const auto& node : lg->fast()) i++;
  return i;
}

int traverse_boosted_graph_nodes(graph::Bi_adjacency_list* g) {
  int i = 0;
  for (const auto& vert : g->verts()) i++;
  return i;
}

int traverse_lgraph_in(LGraph* lg) {
  int i = 0;
  for (const auto& node : lg->fast()) {
    for (const auto& e : node.inp_edges()) {
      i++;
    }
  }
  return i;
}

int traverse_boosted_graph_in(graph::Bi_adjacency_list* g) {
  int i = 0;
  for (const auto& vert : g->verts()) {
    for (const auto& edge : g->in_edges(vert)) {
      i++;
    }
  }
  return i;
}

int traverse_lgraph_out(LGraph* lg) {
  int i = 0;
  for (const auto& node : lg->fast()) {
    for (const auto& e : node.out_edges()) {
      i++;
    }
  }
  return i;
}

int traverse_boosted_graph_out(graph::Bi_adjacency_list* g) {
  int i = 0;
  for (const auto& vert : g->verts()) {
    for (const auto& edge : g->out_edges(vert)) {
      i++;
    }
  }
  return i;
}

int traverse_lgraph_in_out(LGraph* lg) {
  int i = 0;
  for (const auto& node : lg->fast()) {
    for (const auto& e : node.inp_edges()) i++;
    for (const auto& e : node.out_edges()) i++;
  }
  return i;
}

int traverse_boosted_graph_in_out(graph::Bi_adjacency_list* g) {
  int i = 0;
  for (const auto& vert : g->verts()) {
    for (const auto& edge : g->in_edges(vert)) i++;
    for (const auto& edge : g->out_edges(vert)) i++;
  }
  return i;
}

int main(int argc, char** argv) {
  fmt::print("benchmark the graph\n");

  LGraph* lg;
  if (argc == 1) {
    lg = create_some_random_lgraph();
  } else if (argc == 3) {
    fmt::print("benchmark the graph lgdb:{} name:{}\n", argv[1], argv[2]);
    lg = LGraph::open(argv[1], argv[2]);
  } else {
    fmt::print("usage:\n\t{} <lgdb> <lg_name>\n", argv[0]);
    exit(-2);
  }

  populate_graph(lg);

  lgraph_counts(lg);
  boosted_graph_counts(&g2);

  // 3. benchmark same traverse the graph in all the graphs
  fmt::print("Benchmark LGraph and boosted graph\n");
  int  iterations = 10000;
  auto start      = high_resolution_clock::now();
  auto stop       = high_resolution_clock::now();
  auto duration   = duration_cast<microseconds>(stop - start);
  int  x;
  int  micros = 1000000;

  fmt::print("--------------------------Nodes--------------------\n");
  start = high_resolution_clock::now();
  for (int i = 0; i < iterations; i++) {
    x = traverse_lgraph_nodes(lg);
  }
  stop     = high_resolution_clock::now();
  duration = duration_cast<microseconds>(stop - start);
  fmt::print("Traverse LGraph {} times took {}s\n", iterations, duration.count() / micros);

  start = high_resolution_clock::now();
  for (int i = 0; i < iterations; i++) {
    x = traverse_boosted_graph_nodes(&g2);
  }
  stop     = high_resolution_clock::now();
  duration = duration_cast<microseconds>(stop - start);
  fmt::print("Traverse boosted graph {} times took {}s\n", iterations, duration.count() / micros);

  fmt::print("--------------------------Nodes+in--------------------\n");
  start = high_resolution_clock::now();
  for (int i = 0; i < iterations; i++) {
    x = traverse_lgraph_in(lg);
  }
  stop     = high_resolution_clock::now();
  duration = duration_cast<microseconds>(stop - start);
  fmt::print("Traverse LGraph {} times took {}s\n", iterations, duration.count() / micros);

  start = high_resolution_clock::now();
  for (int i = 0; i < iterations; i++) {
    x = traverse_boosted_graph_in(&g2);
  }
  stop     = high_resolution_clock::now();
  duration = duration_cast<microseconds>(stop - start);
  fmt::print("Traverse boosted graph {} times took {}s\n", iterations, duration.count() / micros);

  fmt::print("--------------------------Nodes+out--------------------\n");
  start = high_resolution_clock::now();
  for (int i = 0; i < iterations; i++) {
    x = traverse_lgraph_out(lg);
  }
  stop     = high_resolution_clock::now();
  duration = duration_cast<microseconds>(stop - start);
  fmt::print("Traverse LGraph {} times took {}s\n", iterations, duration.count() / micros);

  start = high_resolution_clock::now();
  for (int i = 0; i < iterations; i++) {
    x = traverse_boosted_graph_out(&g2);
  }
  stop     = high_resolution_clock::now();
  duration = duration_cast<microseconds>(stop - start);
  fmt::print("Traverse boosted graph {} times took {}s\n", iterations, duration.count() / micros);

  fmt::print("--------------------------Nodes+in+out--------------------\n");
  start = high_resolution_clock::now();
  for (int i = 0; i < iterations; i++) {
    x = traverse_lgraph_in_out(lg);
  }
  stop     = high_resolution_clock::now();
  duration = duration_cast<microseconds>(stop - start);
  fmt::print("Traverse LGraph {} times took {}s\n", iterations, duration.count() / micros);

  start = high_resolution_clock::now();
  for (int i = 0; i < iterations; i++) {
    x = traverse_boosted_graph_in_out(&g2);
  }
  stop     = high_resolution_clock::now();
  duration = duration_cast<microseconds>(stop - start);
  fmt::print("Traverse boosted graph {} times took {}s\n", iterations, duration.count() / micros);

  return 0;
}
