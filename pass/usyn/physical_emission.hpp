// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "semantic_region.hpp"

namespace livehd::usyn {

// The post-freeze physical choice of every frozen state (2d-domplace). Data
// only: the selection lives in pass/domplace, outside usyn_salt, and is never
// part of the logical cache. The frozen netlist stays the logical authority.
enum class Physical_decision : uint8_t { domino, retained_cmos };

// Body-less declaration of a provisional domino cell, netlist side, without PG
// pins. Port ids: rails 1..k in physical rail order, the clock k+1 and the
// output k+2, which is a loop break.
struct Physical_cell_interface {
  std::string              name;   // __lhd_domino_<h16>_v<k>_t<t>
  std::vector<std::string> rails;  // "R0".."R{k-1}"
  std::string              clock  = "CLK";
  std::string              output = "Q";
};

// One pin of a converted state: the formula variable, the literal polarity, and
// the frozen native signal on the pin. An inverted producer is a real NOT cell.
struct Physical_rail {
  uint32_t input    = 0;
  bool     negative = false;
  Xsignal  producer;
};

struct Physical_state {
  uint32_t                    state    = 0;  // frozen.state index == state_bits index
  Physical_decision           decision = Physical_decision::retained_cmos;
  bool                        eligible = false;  // passed physical eligibility (then a selection decided)
  std::string                 reason;            // a pass/domplace reason code
  std::optional<Gate_formula> formula;           // positive output, rechecked; absent for early refusals
  Gate_formula::Metrics       metrics{};
  std::vector<Physical_rail>  rails;         // pin order: gate_rails(metrics) over the cofactored inputs
  uint32_t                    cell     = 0;  // index into Physical_emission::cells (domino only)
  // Liberty-basis area estimates for the report, not placement footprints.
  // a_cmos is priced only under select=area; absent means not computed.
  double                      a_domino = 0, a_rail_not = 0;
  std::optional<double>       a_cmos;
};

struct Physical_emission {
  uint32_t                             version = 1;
  std::vector<Physical_state>          states;  // one per frozen state, in frozen order
  std::vector<Physical_cell_interface> cells;   // unique by name
};

// Runs after freeze on every region, a logical cache hit included. Input names
// are the design input bits behind frozen.inputs, indexed like them ("" for an
// input that is not a design input bit); a region other than the design top
// gets none (an empty span). nullopt refuses the design.
using Physical_selector
    = std::function<std::optional<Physical_emission>(const Stateful_region&, std::span<const std::string> input_names, Budget&)>;

}  // namespace livehd::usyn
