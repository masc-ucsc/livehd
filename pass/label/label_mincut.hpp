// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <vector>

#include "pass.hpp"
#include "cell.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "lgraphbase.hpp"
#include "lnast.hpp"

class Label_mincut {
private:
  const bool verbose;
  const bool hier;
  int iters;
  int seed;
  std::string alg;

  uint8_t node_id; 
  uint8_t num_nodes;
  uint8_t num_edges;

  std::string base_path;
  std::string metis_name;
  std::string result_name;

  using IntSet = absl::flat_hash_set<int>;
  
  absl::flat_hash_map<Node::Compact, int> node2id;    // <Node, VieCut id>
  absl::flat_hash_map<int, Node::Compact> id2node;    // <VieCut id, Node>
  absl::flat_hash_map<int, IntSet>        id2neighs;  // <VieCut id, VieCut id of Neighbors>
  absl::flat_hash_map<Node::Compact, int> node2color; // <Node, corresponding color>

  void gather_ids(Lgraph *g);
  void gather_neighs(Lgraph *g);
  
  void lg_to_metis(Lgraph *g);
  void viecut_cut(std::string inp_metis_path, std::string out_path);
  void viecut_label(std::string result_path);

public:
  void label(Lgraph *g);
  Label_mincut(bool _v, bool _h, int _i, int _s, std::string_view _a);
  void dump(Lgraph *g);
};
