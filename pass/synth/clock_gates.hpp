// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "pass_partition.hpp"

namespace livehd::synth {

// A recognized latch-based clock gate, `gclk = clk & L` with `L` a latch
// transparent while `clk` is low holding `en` -- mapped onto the Liberty's
// integrated clock-gate cell. Its latch and AND (and the 1-bit identities
// between them) are absorbed: neither bit-blasted nor kept native. `en`
// crosses ABC as an extra PO (`en_po`, after the async-reset POs) so the
// mapped logic drives the cell's enable pin; the gated clock is a PI of the
// mapped logic (Pi_kind::icg_output, index = this gate) read back from the
// cell's output; the cell's clock pin is the region input `clk_src`, or the
// `parent` gate's cell output for a gate chain.
struct Icg_gate {
  hhds::Node_class latch;        // source enable latch, independent of technology mapping
  hhds::Pin_class  gclk;         // source-side AND output: the gated clock the registers' clk_drv resolve to
  hhds::Pin_class  clk_src;      // source-side reference clock: a region-input driver, or the parent gate's gclk
  int32_t          parent = -1;  // Region_blast::icgs index of the gate driving clk_src, -1: a region input
  hhds::Pin_class  en_drv;       // source-side driver of the latched enable (bit 0 is latched)
  int32_t          en_po = -1;
  std::string      name;        // the enable latch's name, for the cell instance
  int              fanout = 0;  // crossed register bits it clocks (the drive-ladder pick)
};

// Structural recognition only: independent of Liberty, DFF selection and
// whether state crosses a mapper. Source nodes and boundary pins remain owned
// by the caller's region. No graph mutation occurs.
struct Clock_gates {
  std::vector<Icg_gate>                             gates;
  absl::flat_hash_set<hhds::Node_class>             absorbed;
  absl::flat_hash_map<hhds::Pin_class, int32_t>     by_output;
  absl::flat_hash_map<hhds::Pin_class, std::string> rejected;
};

hhds::Pin_class clock_root(hhds::Pin_class pin, const absl::flat_hash_set<hhds::Pin_class>& boundaries);
Clock_gates     analyze_clock_gates(const livehd::partition::Region_body& region);

}  // namespace livehd::synth
