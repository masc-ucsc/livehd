// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Shared helpers for the pass/color algorithms (the revived pass/label, 2c).
//
// The four algorithms (acyclic, synth, path, mincut) each compute a per-node
// color id over a single graph def and then hand the result to apply_coloring,
// which performs the optional continuous (per-region) split and writes either
// the flat per-def color (default, "compact") or the per-instance hier color.

#include <cstdint>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "hhds/graph.hpp"
#include "node_util.hpp"

namespace livehd::color {

constexpr int32_t NO_COLOR = 0;

using Node    = hhds::Node_class;
using NodeSet = absl::flat_hash_set<hhds::Node_class>;
using Node2Id = absl::flat_hash_map<hhds::Node_class, int>;

// Per-pass options shared by every algorithm. `hier` selects whether the pass
// driver colors the whole instance hierarchy (every unique def) or only the
// given graph. `compact` writes the flat per-def color (the default — these
// structural algorithms color identically per instance, so one write per def is
// both correct and far cheaper); when false the per-instance hier color is
// written instead. `continuous` splits each color into one id per maximal
// connected same-color region. `keep_colored` preserves pre-existing colors on
// nodes the algorithm leaves uncolored (the 2p iterative flow).
struct Color_opts {
  bool hier         = true;
  bool verbose      = false;
  bool compact      = true;
  bool continuous   = false;
  bool keep_colored = false;
};

// A node participates in partitioning iff it is a regular (non-builtin) node
// that is neither a constant nor graph IO. INPUT/OUTPUT/CONST live on the HHDS
// singleton nodes (nid < 4<<2, caught by is_builtin_node); legacy LiveHD
// constants are Ntype_op::Nconst regular nodes.
[[nodiscard]] inline bool is_partitionable(const hhds::Node_class& n) {
  if (n.is_invalid() || livehd::graph_util::is_builtin_node(n)) {
    return false;
  }
  auto op = livehd::graph_util::type_op_of(n);
  return op != Ntype_op::Nconst && op != Ntype_op::IO && op != Ntype_op::Invalid;
}

// True if `pin`'s driver/sink crosses a graph boundary (IO singleton) or a
// constant source.
[[nodiscard]] inline bool is_io_or_const_node(const hhds::Node_class& n) {
  return livehd::graph_util::is_builtin_node(n) || livehd::graph_util::type_op_of(n) == Ntype_op::Nconst;
}

// Write `node2id` onto `g`'s regular nodes. Applies the continuous split when
// requested, then writes flat (compact) or per-instance hier color. Nodes
// absent from node2id are cleared (unless keep_colored leaves an existing
// color in place). Returns the number of distinct color ids written.
int apply_coloring(hhds::Graph* g, const Node2Id& node2id, const Color_opts& opts);

// Drop the active coloring on `g` (flat + hier color attrs on every node).
void clear_coloring(hhds::Graph* g);

// Active-coloring descriptor (ColoringInfo) persistence. Stored as a JSON blob
// on the graph's INPUT_NODE (a stable builtin carrier that persists with the
// body). One record per top graph.
void                      set_coloring_info(hhds::Graph* g, const std::string& json);
[[nodiscard]] std::string get_coloring_info(const hhds::Graph* g);
void                      del_coloring_info(hhds::Graph* g);

// Build the serialized ColoringInfo for `g` from its active (flat) coloring:
// schema_version, top, algorithm, params (verbatim JSON object string), and a
// per-color {region_cnt, instance_cnt} map. `params_json` is inlined as-is.
[[nodiscard]] std::string build_coloring_info_json(hhds::Graph* g, std::string_view top, std::string_view algorithm,
                                                   std::string_view params_json);

}  // namespace livehd::color
