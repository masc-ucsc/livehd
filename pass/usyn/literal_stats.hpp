// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// EXPERIMENT (pass.usyn.literal_stats): how much simpler would each selected
// DOMINO gate be if a small mux ("literal") network, settled in the first half
// of the cycle by early control signals, chose the gate's input literals?
//
// For every selected cell (<= 8 inputs) and literal-network depth n = 1..3, the
// cell function F is Shannon-cofactored over n eligible controls. A literal
// network can then feed one DOMINO template T whose inputs it selects, per
// control value, among the remaining inputs, their complements and constants.
//   * lower bound: max over cofactors of the re-synthesized DOMINO cost (any
//     shared template must implement every cofactor);
//   * template: a cofactor's own gate that realizes EVERY cofactor by literal
//     substitution (bounded exact search), plus the literal network's muxes.
// Statistics only: the selection, emission and netlist are unchanged.
//
// Control eligibility scenarios (cumulative):
//   reg     flop Q (this region's state or a region input driven by a flop) and
//           design/port inputs (treated as registered at the boundary)
//   reg_lat reg + latches closed during the first half (transparent-low)
//   stat    reg + interior signals whose whole support is `reg` (static logic
//           computed from registered signals during the first half)
//   stat_lat stat over reg_lat

#include <span>
#include <string>

#include "endpoint.hpp"
#include "logical_region.hpp"
#include "pass_partition.hpp"
#include "region_blast.hpp"
#include "tmap.hpp"

namespace livehd::usyn {

// A verified literal-network rewrite of one state's DominoLatch cell: the
// next state equals `tmpl` whose variable slots[s] reads, per control value v
// (bit q of v = inputs[ctrl[q]]), the literal per[v][s] -- 0..2r-1: remaining
// input (choice/2, in input order without the controls), complemented when
// choice&1; 2r / 2r+1: constant 0 / 1. inputs are native XAG signals.
struct Literal_rewrite {
  uint32_t                           state = 0;
  std::vector<Xsignal>               inputs;
  std::vector<uint32_t>              ctrl;
  std::vector<uint32_t>              slots;
  Gate_formula                       tmpl;
  std::vector<std::vector<uint32_t>> per;
};

// One JSON object (single line) for the region; empty on refusal.
// With `tmap`, the improved cells of the reg/stat scenarios are also emitted
// twice as standalone combinational networks (selected cells vs literal network
// + template) and technology-mapped; extra "kind":"tmap" JSON lines follow.
std::string literal_stats_region(const partition::Region_body& rb, const synth::Region_blast& blast,
                                 const Logical_region& selected, const Endpoint_options& options,
                                 const synth::Tmap_options* tmap = nullptr, std::string_view provider = {},
                                 std::vector<Literal_rewrite>* plan = nullptr, uint32_t plan_depth = 0);

// Rebuild each planned state's next state in `graph` as literal network +
// template (balanced 2:1 mux trees over the controls; identical trees share by
// structural hashing). Returns the number of rewritten states.
uint32_t apply_literal_rewrites(Xag& graph, std::vector<Xsignal>& state_d, std::span<const Literal_rewrite> plan);

}  // namespace livehd::usyn
