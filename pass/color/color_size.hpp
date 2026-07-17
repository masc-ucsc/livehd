// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The SIZE WINDOW: reshape a per-def coloring so that every region's weight
// lands inside [min_ge, max_ge] GATE EQUIVALENTS (todo/livehd/2c-color-size.html).
//
// Why a window at all. `pass.color synth` cuts at state and at large arithmetic,
// which is the right SHAPE for synthesis and the wrong SIZE for it: on XSCore it
// produced 42,176 regions whose median was ONE node, alongside a single
// 3.48M-node monster. ABC pays a fixed cost per region (35k singleton runs are
// nearly all overhead) and its memory scales with the biggest one (a flat XSCore
// reached 221 GB). Neither end is fixable by cutting differently -- the shape is
// right -- so the window merges the small end and splits the large end
// afterwards.
//
// Why GATE EQUIVALENTS and not node counts: see graph_util::ge_weight. A node is
// not a unit of work, and a node-count window would pass a wide datapath region
// straight into an OOM.
//
// The engine is deliberately independent of any one algorithm: it consumes a
// Node2Id and returns a Node2Id. `synth` is wired to it today; the others can opt
// in by setting Color_opts::min_ge/max_ge.

#include <cstdint>

#include "color_common.hpp"
#include "hhds/graph.hpp"

namespace livehd::color {

// What the window did -- for `--stats` and for the tests that pin the invariant.
struct Size_window_stats {
  uint64_t regions_in   = 0;  // regions before the window (connected components)
  uint64_t regions_out  = 0;  // regions after
  uint64_t merges       = 0;  // merge-small steps taken
  uint64_t splits       = 0;  // regions that were split
  uint64_t left_under   = 0;  // regions still under min: no neighbour to merge into
  uint64_t left_over    = 0;  // regions still over max: indivisible (one huge node)
};

// Reshape `node2id` so each region (= connected component of equal-id nodes)
// weighs between min_ge and max_ge gate equivalents. 0 disables that half of the
// window; 0/0 returns the input's component split unchanged.
//
// The returned ids are 1..k, minted in `forward_class()` first-encounter order --
// a deterministic function of the graph, not of hash iteration. Every id is
// exactly one connected region, so a caller does NOT need Color_opts::continuous
// on top (it would only relabel what is already split).
//
// Two invariants the caller may rely on, and one it may not:
//   * no region is over max_ge   -- UNLESS a single node weighs more than max on
//     its own (a wide Mult); such a node is indivisible and is counted in
//     `left_over` rather than silently dropped.
//   * no region is under min_ge  -- UNLESS it has no neighbour at all (a def
//     whose entire body is smaller than min: `left_under`). That case is what the
//     cross-hierarchy absorb pass exists to fix; nothing here can.
//   * NOT maintained: an acyclic region quotient graph. Merging freely can put a
//     formerly cross-region comb cycle inside one region, or create a cycle
//     between two regions. Nothing downstream of a coloring requires a quotient
//     DAG (verified across pass.partition / pass.abc / pass.lec / cgen-verilog).
[[nodiscard]] Node2Id apply_size_window(hhds::Graph* g, const Node2Id& node2id, uint64_t min_ge, uint64_t max_ge,
                                        Size_window_stats* st = nullptr);

}  // namespace livehd::color
