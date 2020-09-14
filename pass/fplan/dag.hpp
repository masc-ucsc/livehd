#pragma once

#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <utility>  // for std::pair
#include <vector>

#include "pattern.hpp"
#include "graph_info.hpp"
#include "i_resolve_header.hpp"
#include "lgraph_base_core.hpp"

// encapsulating dag class because we don't really need the actual nodes

class Dag {
public:
  using d_type = graph::Atomic_out_adjacency_list;

  Dag();

  // initialize a dag from a vector of patterns with all leaves being unique,
  // and all patterns either containing leaves or other patterns.
  // TODO: this should be a constructor, but Graph_info has serious restrictions on moving.
  void init(std::vector<Pattern> hier_patterns, std::unordered_map<Lg_type_id::type, Dim> leaf_dims,
            const Graph_info<g_type>& ginfo);

  void dump() {
    using namespace graph::attributes;
    std::cout << g.dot_format("label"_of_vert = labels, "dim"_of_vert = dims) << std::endl;
  }

private:
  // TODO: I'll probably be using this in the future...
  using label_map_t = graph::Vert_map<d_type, Lg_type_id::type>;
  using dim_map_t   = graph::Vert_map<d_type, Dim>;

  d_type       g;
  dim_map_t    dims;  // area of a node
  d_type::Vert root;
  label_map_t  labels;  // used to ensure dag doesn't contain duplicates
};