// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Synthesis-boundary coloring (ported from pass/label/label_synth, 2c-color).
// Forward-propagates ids along combinational fan-out; in `synth` mode large
// arithmetic (Mult/Div, wide Sum) opens a fresh synthesis boundary.

#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "color_common.hpp"
#include "hhds/graph.hpp"

namespace livehd::color {

class Color_synth {
public:
  Color_synth(Color_opts opts, std::string_view alg);
  void label(hhds::Graph* g);

private:
  Color_opts opts;
  bool       synth = true;

  int last_free_id     = 1;
  int collapse_set_min = 0;

  absl::flat_hash_set<int>                    collapse_set;
  absl::flat_hash_map<hhds::Node_class, int>  flat_node2id;
  absl::flat_hash_map<int, int>               flat_merges;

  int  get_free_id() { return last_free_id++; }
  void set_id(const hhds::Node_class& node, int id);
  void collapse_merge(int dst);
  void mark_ids(hhds::Graph* g);
  void merge_ids();
};

}  // namespace livehd::color
