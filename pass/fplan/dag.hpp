#pragma once

#include <unordered_map>
#include <vector>

#include "graph_info.hpp"

#include "i_resolve_header.hpp"
#include "lgraph_base_core.hpp"

// encapsulating dag class because we don't really need the actual nodes

// TODO: this isn't cheap, but I'm not exactly sure what we need for alg 5.
// TODO: is 0 an illegal lgid?

// NOTE: all patterns should be put in before any leaves are put in.

class Dag {
public:
  using pattern_t     = std::unordered_map<Lg_type_id::type, unsigned int>;
  using pattern_vec_t = std::vector<pattern_t>;

  using dag_t = graph::Stable_out_adjacency_list;

  Dag();

  void make(pattern_vec_t pv, const Graph_info<g_type>& ginfo);

  void dump();

private:
  using label_map_t = graph::Vert_map<dag_t, Lg_type_id::type>;

  dag_t       g;
  label_map_t labels;

  dag_t::Vert root;
};