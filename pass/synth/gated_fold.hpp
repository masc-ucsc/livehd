// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "hhds/graph.hpp"

namespace livehd::synth {

// A Liberty integrated clock-gate cell, by its clock_gate_*_pin attributes
// (pass/liberty scan_icg_cells), for an RTL that instantiates it by name.
struct Icg_cell_pins {
  std::string name, clk, en, test, out;
};

struct Gated_fold_options {
  std::string                domain_clock;  // the design input whose gated registers fold
  std::vector<Icg_cell_pins> icg_cells;
  uint32_t                   max_enable_nodes = 256;  // the largest enable cone cloned per gate
  // Fold the gates whose enable storage samples at the falling edge
  // (negedge-and, latch-or) too: cycle-equivalent only under the half-cycle
  // enable contract. Otherwise they retain with gated-fall-sample.
  bool                       fall_sample      = false;
};

// One register's clock-gate outcome. `code` is "fold" for a folded register,
// else a pass/domplace reason code that retains it. root/gclk are the design
// input and the original gated clock its new clock AND reads (folded only);
// `gate` is what region emission may drop once nothing reads it.
struct Gated_fold_hint {
  std::string      code;
  hhds::Pin_class  root, gclk, enable;  // enable: the gate's cycle-level enable net in the flat top
  hhds::Node_class gate;                // the gate's top-level instance or enable latch, when it has one
};

struct Gated_fold_result {
  absl::flat_hash_map<hhds::Node_class, Gated_fold_hint> registers;  // every register on a non-input clock
  std::map<std::string, uint64_t>                        gates;      // folded registers by recognized gate kind
  std::map<std::string, uint64_t>                        refused;    // gated registers left alone, by reason code
};

// The 2d-domplace gated-clock fold (U21), on the flat source of the design top
// right before partitioning (Pass_partition's prepare_src seam), so a gate in
// a preserved definition (`slcg` -> `NV_CLK_gate_power` -> `CKLNQD12`) is seen
// with its consumers. A positive-edge register whose clock is a recognized,
// glitch-safe clock gate on `domain_clock` keeps its gated clock for the CMOS
// implementation and gains the gate's cycle-level enable:
//
//   clock_pin := root & gclk      (== gclk: the gate ANDs root with its enable)
//   enable    := enable & en      (redundant on the gated clock)
//
// so the register is a plain enable register on the root clock for the
// domino profile (next = en ? f : Q) while gclk stays reachable for one that
// remains CMOS. The two new ANDs take the register's color, so they partition
// with it; the gate's enable cone is cloned once into a color of its own, so
// it reaches every consumer region as one input (the stage's enable rail).
// Recognized gates:
//   latch-and    clk & L, L a latch transparent while clk is low (en = its D)
//   icg-cell     a Liberty integrated clock gate (en = enable | test enable)
// and, only with options.fall_sample (their enable is sampled at the falling
// edge, so they equal the cycle enable only while it holds until the rise):
//   negedge-and  clk & F, F a negative-edge flop on clk (en = its D)
//   latch-or     clk | ~L, L a latch transparent while clk is high (en = its D)
// through 1-bit identities and the outputs of instantiated definitions, whose
// combinational enable cone (test enables included) is cloned across the
// boundaries. Anything else is reported, never folded: an inverted clock, a
// gate on a gated clock (a chain), a clock that is not a design input or not
// the domain, state inside the enable cone, an unsafe AND/OR.
// Returns empty registers when `flat` is not the design top.
Gated_fold_result fold_gated_clocks(hhds::Graph* flat, const Gated_fold_options& options);

}  // namespace livehd::synth
