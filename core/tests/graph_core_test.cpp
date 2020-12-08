//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <vector>
#include <string>

#include "lbench.hpp"
#include "lrand.hpp"


#include "graph_core.hpp"
#include <chrono>

using testing::HasSubstr;
using namespace std;
using namespace std::chrono;

class Setup_graph_core : public ::testing::Test {
protected:

  void SetUp() override {
  }

  void TearDown() override {
    //Graph_library::sync_all();
  }
};

Graph_core* build_random_graph(int nodes) {
  Graph_core* c1 = new Graph_core("lgdb_gc","shallow_tree");

  for (int i = 0; i < nodes; i++)
  {
    Index_ID id = c1->create_master_root(0);
  }

  // add edges
  for (int i = 0; i < nodes; i++) {
    vector<uint8_t> edge_set;

    for (int j = -128; j < 128; j++) {
      if (i + j >= 0 && i + j < nodes && j != 0) {
        edge_set.push_back((uint8_t)j);
      }
    }

    int num_edges = rand() % 50 + 1;
    for (int j = 0; j < num_edges; j++) {
      int rand_offset = rand() % num_edges;
      int other = i + (int8_t) (edge_set.at(rand_offset));
      c1->add_edge(i, other);
    }
  }

  return c1;
}

bool subset_of (vector<Index_ID>* a, vector<Index_ID>* b){
  for (auto a_elt: *a) {
    if (std::find(b->begin(), b->end(), a_elt) == b->end()) {
        return false;
    }
  }
  return true;
}

bool dfs(Graph_core* gc, Index_ID start, Index_ID end, vector<Index_ID>* visited) {
  visited->push_back(start);
  vector<Index_ID> edges = gc->get_edges(start);

  if (subset_of(&edges, visited)) {
    return false;
  } else if (std::find(edges.begin(), edges.end(), end) != edges.end()) {
    return true;
  }
  else {
    for (Index_ID s: edges) {
      if(std::find(visited->begin(), visited->end(), s) == visited->end() && dfs(gc, s, end, visited))
        return true;
    }
    return false;
  }
}

TEST_F(Setup_graph_core, shallow_tree) {
  fmt::print("\n\n");

  for (int i = 500; i <= 5000; i+= 500) {
    auto start = high_resolution_clock::now();
    Graph_core* gc = build_random_graph(i);

    auto mid = high_resolution_clock::now();
    vector<Index_ID>* visited = new vector<Index_ID>();
    bool path_exist = dfs(gc, 0, i - 1, visited);

    auto end = high_resolution_clock::now();

    auto create_time = duration_cast<milliseconds>(mid- start);
    auto trav_time = duration_cast<milliseconds>(end - mid);
    fmt::print("{}, {}, {}\n", i, create_time.count(), trav_time.count());
  }

  /*
  Graph_core* c1 = new Graph_core("lgdb_gc","shallow_tree");

  for (int i = 0; i < 500; i++)
    c1->create_master_root(0);

  for (int i = 3; i < 30; i++) {

    c1->add_edge(0, i);
  }

  for (int i = 0; i < 400; i++){
    c1->add_edge(i,i + 1);
    c1->add_edge(i,i + 2);
  }

  vector<Index_ID>* visited = new vector<Index_ID>();
  fmt::print("--{}--", dfs(c1, 0, 450, visited));
  */

  fmt::print("\n\n");
}

