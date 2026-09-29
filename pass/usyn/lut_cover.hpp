// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "lnet.hpp"
#include "unate.hpp"

namespace livehd::usyn {

// The cost of one LUT function (at most 8 inputs, every input in its support).
// Its inputs and output are free in either polarity, so a function and its
// complement cost the same. A function one domino gate builds (recipe support,
// series and factored-literal limits) is always a domino gate; any other
// function is a static (non-unate) LUT.
struct Function_cost {
  bool     constant     = false;  // no inputs
  bool     alias        = false;  // one input: a wire or an inverter, free
  bool     domino       = false;  // one domino gate
  bool     complemented = false;  // `form` computes the complement
  uint32_t cost         = 0;      // transistor proxy (Cover_cost)
  Form     form;                  // over the inputs in order
};
Function_cost function_cost(const Truth_table& table, const Recipe& recipe, const Cover_cost& cost, Budget& budget);

// One LUT of the cover: `root` of the source network (a node id) as a
// function of `leaves` (sources or other LUT roots), in ascending leaf order.
struct Cover_lut {
  Id              root = 0;
  std::vector<Id> leaves;
  Truth_table     table;
  Function_cost   fn;
  uint8_t         level = 0;  // chained domino gates up to here; 255 = not a pure domino chain
};
struct Cover_result {
  Status                 status = Status::search_exhausted;
  std::string            reason;
  std::vector<Cover_lut> luts;  // topological
  uint64_t               domino = 0, nonunate = 0, aliases = 0, constants = 0;
  uint64_t               cost = 0, domino_cost = 0, nonunate_cost = 0, domino_literals = 0;
  uint64_t               flow_cost = 0;  // the cover before exact-area recovery
  std::array<uint64_t, 9> domino_inputs{};  // domino gates by input count
  std::array<uint64_t, 7> domino_series{};  // domino gates by longest product (6 = 6 or more)
  // Outputs (not wires or constants) by the domino levels that build them.
  uint64_t               outputs_shallow = 0, outputs_deep = 0, outputs_wire = 0;
  // Outputs (not wires or constants) by the domino gates in their whole cone
  // inside the region: 1, 2, 3, 4 or more (a non-unate LUT counts as a gate).
  std::array<uint64_t, 5> cone_gates{};
  uint64_t               cuts = 0, functions = 0;
  // Source-network nodes inside some LUT, and extra copies of those inside
  // more than one LUT (logic replication).
  uint64_t               covered_nodes = 0, replicated_nodes = 0;
  // Gate inputs that read a region input (source) in true / complemented
  // polarity: a complemented one is free in dual rail but a static inverter.
  uint64_t               source_literals_pos = 0, source_literals_neg = 0;
};

// Cut-based cover of a whole region (a STRASH Lnet, livehd::synth::strash):
// priority cuts with composed truth tables, a depth-optimal round, then
// area-flow and exact-area rounds that minimize the summed transistor proxy
// under the domino_levels required times. The cover is checked by simulation
// against the source network. Every LUT reads at most `support` nodes; the
// combinational outputs are the source's outputs, then its latch inputs.
Cover_result lut_cover(const livehd::synth::Lnet& source, const Search_options& options);

// The cover as a coarse network over the source's boundary (its inputs and
// latches in CI order, its outputs): one LUT per gate with its minimum SOP as
// side data (Lnet::sop), a wire as its leaf, an inverter as a 1-input LUT, a
// constant as a constant. nullopt when a LUT reads a leaf the cover never
// built.
std::optional<livehd::synth::Lnet> cover_network(const livehd::synth::Lnet& source, const Cover_result& cover);

}  // namespace livehd::usyn
