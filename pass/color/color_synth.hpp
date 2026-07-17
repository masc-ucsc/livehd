// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Synthesis-boundary coloring (ported from pass/label/label_synth, 2c-color).
// Ids propagate forward along combinational fan-out, bounded by CUT nodes: the
// loop breaks (flop/mem/latch/stateful sub) always, plus -- in `synth` mode --
// large arithmetic (Mult/Div, wide Sum).
//
// A region is one combinational cone plus, at its END, the register that cone
// feeds ("stage N = the logic plus the register it writes"). A cut therefore
// joins AT MOST ONE neighbour -- the driver of its `din` -- and hands its id to
// nobody.
//
// Both halves of that rule are load-bearing, and each was measured on a
// flattened dino CPU (9613 comb nodes):
//   * inherit from EVERY fan-in and a register welds its data cone to the
//     enable/stall cone that gates it => 99.4% of nodes in one region.
//   * propagate to fan-out and the cones a register drives re-merge wherever
//     they reconverge (forwarding logic reads several stage registers) => 100%
//     of nodes in one region, and per-def dino fell from 45 regions to 30.
// Joining `din` alone does neither, and keeps registers out of regions of their
// own (17 of dino's 45 regions before; ~39k one-flop regions on XSCore).
//
// NOTE: none of this manufactures pipeline stages on a FLAT netlist. A pipeline
// register is one multi-bit flop node, so flat dino has 17 state nodes for 9613
// comb nodes and its comb-only subgraph is a single connected component -- there
// is nothing there to cut. The regions live in the module hierarchy; this is a
// per-def coloring.

#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "color_common.hpp"
#include "color_size.hpp"
#include "hhds/graph.hpp"

namespace livehd::color {

class Color_synth {
public:
  Color_synth(Color_opts opts, std::string_view alg);
  void label(hhds::Graph* g);

private:
  Color_opts opts;
  bool       synth = true;
  // Does this graph carry source-seeded (block-attribute) regions? Re-read per
  // def in label(), since the driver reuses one Color_synth across defs.
  bool seeded = false;

  int last_free_id = 1;

  absl::flat_hash_map<hhds::Node_class, int> flat_node2id;
  Int_union_find                             uf;

  int  get_free_id() { return last_free_id++; }
  void set_id(const hhds::Node_class& node, int id);
  void force_id(const hhds::Node_class& node, int id);
  [[nodiscard]] bool is_cut(const hhds::Node_class& node) const;
  [[nodiscard]] bool is_seeded(const hhds::Node_class& node) const;
  [[nodiscard]] int  data_cone_id(const hhds::Node_class& node);
  void mark_ids(hhds::Graph* g);
  void merge_ids();
};

}  // namespace livehd::color
