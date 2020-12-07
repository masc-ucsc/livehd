#include "absl/container/flat_hash_map.h"

#include "lgedgeiter.hpp"
#include "lbench.hpp"
#include "lgraph.hpp"
#include "Adjacency_list.hpp"

LGraph *create_some_random_lgraph() {

  LGraph *lg=LGraph::create("lgdb_bench", "random", "-");

  // 1. create a new LGraph


  return lg;
}

graph::Bi_adjacency_list populate_graph(LGraph *lg) {
  printf("%s\n","populating");

  graph::Bi_adjacency_list g2;
  absl::flat_hash_map<Node::Compact, graph::Bi_adjacency_list::Vert> map2g2vertex;

  printf("%s\n","after hash");
  map2g2vertex[lg->get_graph_input_node().get_compact()] = g2.insert_vert();
  map2g2vertex[lg->get_graph_output_node().get_compact()] = g2.insert_vert();

  printf("%s\n","After hash filling");

  for(auto node:lg->fast()) {
    auto i = g2.insert_vert();
    map2g2vertex[node.get_compact()] = i;
  }

  printf("%s\n","Got past first for loop");

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

  printf("%s\n","populated");
  return g2;
}

// Traversal Algos
//nodes+input_edges+output_edges
void lgraph_traverse(LGraph *lg){
  printf("%s\n","lgraph_traverse over edges+nodes 1000 times.");
  Lbench b("lgraph_traverse_nodes+edges");
  int count = 0;
  for (size_t i = 0; i < 1000; i++) {
    for(auto node:lg->fast()) {
      for(auto e:node.out_edges()) {
        count++;

      }
      for(auto e:node.inp_edges()) {
        count++;
      }
      printf("%d\n",count );
    }
  }

}
//nodes+input_edges+output_edges
void ngraph_traverse(const graph::Bi_adjacency_list &ng){
  printf("%s\n","ngraph_traverse over edges+nodes 1000 times");
  Lbench b("ngraph_traverse_nodes+edges");
  int count = 0;
  for (size_t i = 0; i < 1000; i++) {
    for (auto e : ng.verts()){
      for(auto f: ng.out_edges(e)){
        count++;
      }
      for(auto f: ng.in_edges(e)){
        count++;
      }
      printf("%d\n",count );
    }

  }

}

//nodes+input_edges
void lgraph_node_input_traverse(LGraph *lg){
  printf("%s\n","lgraph_traverse over input edges+nodes 1000 times.");
  Lbench b("lgraph_node_input_traverse");
  int count=0;
  for (size_t i = 0; i < 1000; i++) {
    for(auto node:lg->fast()) {
      for(auto e:node.inp_edges()) {
        count++;
      }
    }
    printf("%d\n",count );
  }

}
//nodes+input_edges
void ngraph_node_input_traverse(const graph::Bi_adjacency_list &ng){
  printf("%s\n","ngraph_traverse over input edges+nodes 1000 times");
  Lbench b("ngraph_node_input_traverse");
  int count = 0;
  for (size_t i = 0; i < 1000; i++) {
    for (auto e : ng.verts()){
      for (auto f : ng.in_edges(e)){
        count++;
      }
      printf("%d\n",count );
    }

  }

}

//nodes
void lgraph_node_traverse(LGraph *lg){
  printf("%s\n","lgraph_traverse over nodes 1000 times.");
  Lbench b("lgraph_node_traverse");
  int count = 0;
  for (size_t i = 0; i < 1000; i++) {
    for(auto node:lg->fast()) {
      count++;
      printf("%d\n",count );
    }

  }

}
//nodes
void ngraph_node_traverse(const graph::Bi_adjacency_list &ng){
  printf("%s\n","ngraph_traverse over nodes 1000 times");
  Lbench b("ngraph_node_traverse");
  int count = 0;
  for (size_t i = 0; i < 1000; i++) {
    for (auto e : ng.verts()){
      count++;
      printf("%d\n",count );
    }

  }

}

int main(int argc, char **argv) {

  fmt::print("benchmark the graph\n");

  LGraph *lg;

  if (argc==1) {
    lg = create_some_random_lgraph();
    fmt::print("benchmark the random graph\n");
  }else if (argc==3) {
    fmt::print("benchmark the graph lgdb:{} name:{}\n", argv[1], argv[2]);
    lg = LGraph::open(argv[1], argv[2]);
  }else{
    fmt::print("usage:\n\t{} <lgdb> <lg_name>\n", argv[0]);
    exit(-2);
  }

  // // 2. create a copy of ngraph/boost::graph/....
  char test[] = "graph will be populated";
  printf("%s\n",test);
  const graph::Bi_adjacency_list& ng = populate_graph(lg);

  // 3. benchmark same traverse the graph in all the graphs
  lgraph_traverse(lg);
  ngraph_traverse(ng);

  lgraph_node_input_traverse(lg);
  ngraph_node_input_traverse(ng);

  lgraph_node_traverse(lg);
  ngraph_node_traverse(ng);


}
