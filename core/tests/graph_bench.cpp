
#include "absl/container/flat_hash_map.h"

#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "Adjacency_list.hpp"

LGraph *create_some_random_lgraph() {

  LGraph *lg=LGraph::create("lgdb_bench", "random", "-");

  // 1. create a new LGraph


  return lg;
}

graph::Bi_adjacency_list g2;

void populate_graph(LGraph *lg) {

  absl::flat_hash_map<Node::Compact, graph::Bi_adjacency_list::Vert> map2g2vertex;

  map2g2vertex[lg->get_graph_input_node().get_compact()] = g2.insert_vert();
  map2g2vertex[lg->get_graph_output_node().get_compact()] = g2.insert_vert();

  for(auto node:lg->fast()) {
    auto i = g2.insert_vert();
    map2g2vertex[node.get_compact()] = i;
  }

  for(auto node:lg->fast()) {
    auto src_it = map2g2vertex.find(node.get_compact());
    assert(src_it!=map2g2vertex.end());

    for(auto e:node.out_edges()) {
      // insert in graph
      auto dst_it = map2g2vertex.find(e.sink.get_node().get_compact());
      assert(dst_it!=map2g2vertex.end());

      g2.insert_edge(src_it->second, dst_it->second);
    }
  }
}

int main(int argc, char **argv) {

  fmt::print("benchmark the graph\n");

  LGraph *lg;
  if (argc==1) {
    lg = create_some_random_lgraph();
  }else if (argc==3) {
    fmt::print("benchmark the graph lgdb:{} name:{}\n", argv[1], argv[2]);
    lg = LGraph::open(argv[1], argv[2]);
  }else{
    fmt::print("usage:\n\t{} <lgdb> <lg_name>\n", argv[0]);
    exit(-2);
  }

  // 2. create a copy of ngraph/boost::graph/....

  populate_graph(lg);

  // 3. benchmark same traverse the graph in all the graphs

}

