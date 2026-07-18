// This file is distributed under the BSD 3-Clause License. See LICENSE for
// details.
#pragma once

// Shared helpers for the pass/color algorithms (the revived pass/label, 2c).
//
// The algorithms (acyclic, cgen, synth, path, mincut, flat) each compute a
// per-node color id over a single graph def and then hand the result to
// apply_coloring, which performs the optional continuous (per-region) split and
// writes either the flat per-def color (default, "compact") or the per-instance
// hier color.

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

using Node = hhds::Node_class;
using NodeSet = absl::flat_hash_set<hhds::Node_class>;
using Node2Id = absl::flat_hash_map<hhds::Node_class, int>;

// Union-find over node identities (a region = connected component of same-color
// nodes). Shared by the continuous per-region split (apply_coloring) and the
// partition pass; keep the single definition here so the two never drift.
//
// find() is ITERATIVE (path halving). It walks a chain as long as the design is
// wide -- a whole-design walk is millions of nodes -- so recursion here is a
// stack overflow, not a style preference.
class Union_find {
public:
  hhds::Node_class find(const hhds::Node_class &n) {
    auto cur = n;
    parent_.try_emplace(cur, cur);
    while (true) {
      auto p = parent_[cur];
      if (p == cur) {
        return cur;
      }
      auto gp      = parent_[p];
      parent_[cur] = gp;  // path halving
      cur          = gp;
    }
  }
  void merge(const hhds::Node_class &a, const hhds::Node_class &b) {
    auto ra = find(a);
    auto rb = find(b);
    if (ra != rb) {
      parent_[ra] = rb;
    }
  }

private:
  // Invariant every value stored is also a key -- that is what lets find()
  // index with operator[] without inserting a bogus default-constructed root.
  absl::flat_hash_map<hhds::Node_class, hhds::Node_class> parent_;
};

// Union-find over COLOR IDs (ints). Same iterative contract as Union_find.
// Union-by-min keeps the representative the smallest id in the class, so the
// resulting color ids are a deterministic function of the id allocation order
// rather than of hash iteration order.
class Int_union_find {
public:
  int find(int x) {
    parent_.try_emplace(x, x);
    while (true) {
      const int p = parent_[x];
      if (p == x) {
        return x;
      }
      const int gp = parent_[p];
      parent_[x]   = gp;  // path halving
      x            = gp;
    }
  }
  void merge(int a, int b) {
    const int ra = find(a);
    const int rb = find(b);
    if (ra == rb) {
      return;
    }
    if (ra < rb) {
      parent_[rb] = ra;
    } else {
      parent_[ra] = rb;
    }
  }

private:
  absl::flat_hash_map<int, int> parent_;
};

// Per-def coloring outcome, filled by apply_coloring from the walk it already
// makes (so `--stats` costs no extra traversal). `color_nodes` is keyed by the
// color id AS WRITTEN (post seeded-base shift), which is the id pass.partition
// turns into the `<def>__c<id>` module name.
struct Def_color_sizes {
  absl::flat_hash_map<int, uint64_t> color_nodes;
  // Gate equivalents per color, same keys as `color_nodes`. This -- not the node
  // count -- is what the size window bounds and what predicts ABC's memory: a
  // 200k-node region of wide datapath passes any node gate and still OOMs
  // (todo/livehd/2c-color-size.html R1).
  absl::flat_hash_map<int, uint64_t> color_ge;
  uint64_t partitionable = 0;  // nodes the algorithm was allowed to color
  uint64_t uncolored = 0;      // ... that it left without a color
};

// Per-pass options shared by every algorithm. `hier` selects whether the pass
// driver colors the whole instance hierarchy (every unique def) or only the
// given graph. `compact` writes the flat per-def color (the default — these
// structural algorithms color identically per instance, so one write per def is
// both correct and far cheaper); when false the per-instance hier color is
// written instead. `continuous` splits each color into one id per maximal
// connected same-color region. `keep_colored` preserves pre-existing colors on
// nodes the algorithm leaves uncolored (the 2p iterative flow).
struct Color_opts {
  bool hier = true;
  bool verbose = false;
  bool compact = true;
  bool continuous = false;
  bool keep_colored = false;

  // Size window, in MAPPABLE gate equivalents (graph_util::mappable_ge_weight:
  // a Sub instance counts ~1, everything else ge_weight), honored by the
  // algorithms that opt in (today: synth). Regions below `min_ge` are merged
  // into a neighbour; regions above `max_ge` are split. 0 disables that half of
  // the window.
  //
  // Both default to 0 -- INERT -- on purpose: this struct is the algorithm's
  // contract, and an algorithm asked for the raw coloring must give the raw
  // coloring. The shipped policy (1000 / 30000000) lives on the pass.color
  // labels, so it applies to the CLI without silently rewriting what a direct
  // caller or a unit test asked for.
  uint64_t min_ge = 0;
  uint64_t max_ge = 0;

  // `--stats` sink for the def currently being colored, or nullptr. It rides on
  // the options so every algorithm reports without each one growing a
  // parameter; the driver re-points it per def and owns the cross-def
  // aggregation (an algorithm only ever sees one def).
  Def_color_sizes *sizes = nullptr;
};

// A node participates in partitioning iff it is a regular (non-builtin) node
// that is neither a constant nor graph IO. INPUT/OUTPUT/CONST live on the HHDS
// singleton nodes (nid < 4<<2, caught by is_builtin_node); legacy LiveHD
// constants are Ntype_op::Nconst regular nodes.
[[nodiscard]] inline bool is_partitionable(const hhds::Node_class &n) {
  if (n.is_invalid() || livehd::graph_util::is_builtin_node(n)) {
    return false;
  }
  auto op = livehd::graph_util::type_op_of(n);
  return op != Ntype_op::Nconst && op != Ntype_op::IO &&
         op != Ntype_op::Invalid;
}

// Write `node2id` onto `g`'s regular nodes. Applies the continuous split when
// requested, then writes flat (compact) or per-instance hier color. Nodes
// absent from node2id are cleared (unless keep_colored leaves an existing
// color in place). Returns the number of distinct color ids written; fills
// `sizes` when non-null.
// Source-SEEDED colors (2opt-freq B block attributes: coloring_info
// algorithm=="block-attr", or "seeded":true carried by a later rebuild) win
// over the algorithm: seeded nodes keep their color and algorithm ids shift
// above the max seeded id.
int apply_coloring(hhds::Graph *g, const Node2Id &node2id,
                   const Color_opts &opts, Def_color_sizes *sizes = nullptr);

// True when g's active coloring carries source-seeded block regions.
[[nodiscard]] bool has_seeded_coloring(hhds::Graph *g);

// Drop the active coloring on `g` (flat + hier color attrs on every node).
void clear_coloring(hhds::Graph *g);

// Active-coloring descriptor (ColoringInfo) persistence. Stored as a JSON blob
// on the graph's INPUT_NODE (a stable builtin carrier that persists with the
// body). One record per top graph.
void set_coloring_info(hhds::Graph *g, const std::string &json);
void del_coloring_info(hhds::Graph *g);

// Splice the source-seeded members ("seeded" + the block-attribute
// "region_opts" pass.abc consumes) from g's CURRENT coloring_info into a
// freshly built one, so a pass.color rebuild never drops the user's block
// annotations. Identity when g carries no seeded coloring.
[[nodiscard]] std::string preserve_seeded_info(hhds::Graph *g,
                                               std::string fresh_json);

// Build the serialized ColoringInfo for `g` from its active (flat) coloring:
// schema_version, top, algorithm, params (verbatim JSON object string), and a
// per-color {region_cnt, instance_cnt} map. `params_json` is inlined as-is.
[[nodiscard]] std::string
build_coloring_info_json(hhds::Graph *g, std::string_view top,
                         std::string_view algorithm,
                         std::string_view params_json);

} // namespace livehd::color
