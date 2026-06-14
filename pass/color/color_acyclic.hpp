// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Acyclic combinational partitioning (ported from the deleted
// pass/label/label_acyclic on the current hhds::Graph API; see 2c-color).
// Grows acyclic regions backward from roots (graph outputs + fan-out>1 nodes),
// optionally merges small same-parent / single-parent partitions, then writes a
// color id per node.

#include <cstdint>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "color_common.hpp"
#include "hhds/graph.hpp"

namespace livehd::color {

class Color_acyclic {
public:
  Color_acyclic(Color_opts opts, int cutoff, bool merge_en);
  void label(hhds::Graph* g);

private:
  using IntSet = absl::flat_hash_set<int>;

  Color_opts opts;
  int        cutoff;
  bool       merge_en;
  int        part_id = 1;

  std::vector<hhds::Node_class> node_preds;  // worklist of predecessors
  NodeSet                       roots;       // potential partition roots

  absl::flat_hash_map<hhds::Node_class, int> node2id;      // node -> partition id
  absl::flat_hash_map<int, NodeSet>          id2nodes;     // partition id -> nodes
  absl::flat_hash_map<int, NodeSet>          id2inc;       // partition id -> incoming neighbor nodes
  absl::flat_hash_map<int, NodeSet>          id2out;       // partition id -> outgoing neighbor nodes
  absl::flat_hash_map<int, IntSet>           id2incparts;  // partition id -> incoming partition ids
  absl::flat_hash_map<int, IntSet>           id2outparts;  // partition id -> outgoing partition ids

  [[nodiscard]] static bool node_set_cmp(const NodeSet& a, const NodeSet& b);
  [[nodiscard]] static bool int_set_cmp(const IntSet& a, const IntSet& b);
  static void               node_set_write(NodeSet& tgt, const NodeSet& ref);
  static void               int_set_write(IntSet& tgt, const IntSet& ref);

  void gather_roots(hhds::Graph* g);
  void grow_partitions(hhds::Graph* g);
  void gather_inou(hhds::Graph* g);

  void merge_op(int merge_from, int merge_into);
  void merge_partitions_same_parents();
  void merge_partitions_one_parent();
};

}  // namespace livehd::color
