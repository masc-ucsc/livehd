// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// `--lib` cell instances the LEC resolves before the read-only phase schedule
// classifies state, so that a mapped netlist's commit classes depend less on
// how the netlist was read.
//
// Two representations of one mapped netlist -- the `lg:` graph `lhd synth`
// writes, and the same netlist re-read from its Verilog emission -- should get
// the same verdict. For a latch enabled by a latch-based clock gate
// (lhd/tests/abc_latch_mix.v `abc_latch_gated`) they did not, for two
// independent reasons:
//
//  1. PORT NUMBERING (fixed at the source, not here). A Verilog side elaborates
//     the `--lib` models itself, so its instances bind to its own copy of each
//     cell, whose port ids follow the Verilog declaration order; the `--lib`
//     graph of the same cell (same gid) may number them differently (gensim's
//     DLCLKPx1 has GCLK at port 4, its Verilog re-read at port 3). The time
//     base splices stateful and clock-cone cells from the `--lib` model, and
//     that splice bound ports BY ID: GCLK's readers matched no output and every
//     latch the gate enabled read as always-transparent. The time base now
//     splices an instance whose def the design holds from THAT body (by id,
//     through its own IO), and only a body-less instance from the `--lib`
//     model, bound by port NAME (graph/inline_sub.hpp); it refuses the run
//     when a model does not declare the instance's ports by name and width
//     (pass/single_edge/proof_prep.cpp). The splice below reaches body-less
//     cells only, through the same inliner, and refuses the same way.
//
//  2. OPAQUE GATES ON A LATCH ENABLE (this file). The phase schedule classifies
//     a latch structurally: `clk & en_l` makes it a CLOCK-role latch (it closes
//     at the fall, gated by the live `en_l`), anything it cannot see through
//     makes it a DATA latch (it commits at the rise with the clock read as a
//     free data input). USYN maps that gate onto an `AND2x1` cell; the Verilog
//     re-read inlines the cell (comb inlining), the `lg:` graph keeps it
//     opaque. The same hardware was scheduled two different ways, and the latch
//     that never opens under a solver-chosen `clk = 0` refuted a latch that the
//     reference commits every period. `inline_clock_latch_enable_cells`
//     exposes, on every latch enable, the single-product combinational cells
//     (an And of literals, maybe inverted) whose output depends on a clock, so
//     that gate reaches the classifier as the same native `clk & en_l` on every
//     representation. It runs AFTER edge normalization (which keeps its own,
//     deliberately narrower view of a latch enable -- see
//     pass/single_edge/proof_prep.cpp inline_clock_lib_cells) and only matters
//     where the phase schedule decides.
//
// KNOWN GAPS -- the `lg:` form and the Verilog form can still reach the
// classifier in different shapes (observed only as a false REFUTED; a splice
// here is the cell's own model, so it changes how the schedule classifies a
// latch, never what the cell computes):
//   * A single-product cell on a gate's NON-clock operand stays opaque in the
//     `lg:` form, because only cells whose output depends on a clock are
//     spliced, while the Verilog re-read dissolves it. An active-low gate whose
//     enable is `NOR2x1(clk, INVx1(held_en))` exposes the NOR2 but keeps the
//     INVx1, and that `lg:` netlist refutes its own Verilog emission.
//   * A cell with an Or in its model (OR2, XOR2, AOI...) stays opaque and keeps
//     the data-latch reading. The schedule's Or-form gate model is itself
//     incomplete: natively, `gclk_n = clk | ~held_en` (held_en latched while
//     clk is high) consumed as `always_latch if (!gclk_n)` refutes against the
//     equivalent `always_latch if (!clk && held_en)`, with no cell involved.
//     Exposing Or cells would feed it more shapes whose guard polarity it does
//     not model.
// In both repaired cases the two sides' latch state had no pairing (the
// reference's one 4-bit `g` against four 1-bit cells), so the checker fell back
// to a pair-free BMC under synthetic `?` initialization; that fallback only
// exposed the time-base mismatch, it did not cause it.

#include <cstddef>
#include <functional>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "hhds/graph.hpp"

namespace livehd::lec {

using Lib_cell_models = absl::flat_hash_map<hhds::Gid, hhds::Graph*>;

struct Lib_cell_prep_result {
  size_t      spliced = 0;
  std::string error;  // non-empty: a splice was refused; the caller must not query
};

// Splice every single-product combinational `--lib` cell instance (BUF, INV,
// AND, NAND, NOR, ...; no Or in its model -- see lib_cell_prep.cpp for why)
// that sits in a latch's enable cone, anywhere in `top`'s instance tree, and
// whose output depends on a clock input or a Clock_cell without crossing state
// (see 2. above). `is_boxed` names defs that stay opaque (collapsed/trusted):
// the walk does not descend into them. A model that does not bind to the
// instance by port name stops the walk with `error` set (the splices already
// done are complete and semantics-preserving).
[[nodiscard]] Lib_cell_prep_result inline_clock_latch_enable_cells(const Lib_cell_models& sub_lib, hhds::Graph* top,
                                                                   const std::function<bool(const hhds::Graph*)>& is_boxed = {});

}  // namespace livehd::lec
