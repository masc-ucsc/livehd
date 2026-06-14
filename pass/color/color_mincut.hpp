// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Min-cut partitioning via the external VieCut library (ported from
// pass/label/label_mincut, 2c-color). Gathers the non-const/non-IO nodes, emits
// a METIS connectivity file, runs VieCut, and colors from the partition.

#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "color_common.hpp"
#include "hhds/graph.hpp"

namespace livehd::color {

class Color_mincut {
public:
  Color_mincut(Color_opts opts, int iters, int seed, std::string_view alg);
  void label(hhds::Graph* g);

private:
  using IntSet = absl::flat_hash_set<int>;

  Color_opts  opts;
  int         iters;
  int         seed;
  std::string alg;

  int num_nodes = 0;
  int num_edges = 0;

  absl::flat_hash_map<hhds::Node_class, int> node2id;     // node -> dense VieCut id
  absl::flat_hash_map<int, hhds::Node_class> id2node;     // dense VieCut id -> node
  absl::flat_hash_map<int, IntSet>           id2neighs;   // id -> neighbor ids
  absl::flat_hash_map<hhds::Node_class, int> node2color;  // node -> color

  void gather_ids(hhds::Graph* g);
  void gather_neighs(hhds::Graph* g);
  void lg_to_metis(hhds::Graph* g, const std::string& metis_path);
  void viecut_cut(const std::string& metis_path, const std::string& out_path);
  void viecut_label(const std::string& result_path);
};

}  // namespace livehd::color
