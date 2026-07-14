// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "hhds/graph.hpp"

// Liberty DFF-cell support shared by pass.abc (which maps flops to DFF-cell Subs
// when register=true) and pass.liberty gensim (which emits a behavioral model for
// that cell). ABC's read_lib DROPS sequential cells before they reach the Mio
// library, so the only way to learn a flop cell's D/CLK/Q pins is to scan the
// Liberty text directly — that is what find_dff_cell does.
namespace livehd::liberty {

struct Dff_cell {
  std::string name;     // Liberty cell name (e.g. sky130_fd_sc_hd__dfxtp_1)
  std::string d_pin;    // data input pin  (Liberty ff `next_state`)
  std::string clk_pin;  // clock input pin (Liberty ff `clocked_on`, posedge)
  std::string q_pin;    // Q output pin    (output whose `function` == the ff state var)
};

// Scan the whitespace-separated Liberty file list for a plain POSEDGE D-flop: a
// cell with an `ff(){}` group whose `clocked_on` is a bare pin (posedge, not
// `!CLK`) and `next_state` a bare pin (no scan/enable logic), exactly one data +
// one clock input, and one non-inverting Q output. When `prefer` is non-empty
// only that cell name is considered. Returns nullopt when nothing qualifies.
std::optional<Dff_cell> find_dff_cell(const std::string& lib_files, std::string_view prefer = "");

// Create-or-find the 1-bit blackbox IO decl (inputs d_pin, clk_pin; output q_pin)
// for `dff` in `outlib`. Port ids: d=1, clk=2, q=3 (a fixed convention so the
// pass.abc netlist Sub and the gensim model agree). Idempotent (find-or-create).
std::shared_ptr<hhds::GraphIO> create_dff_io(hhds::GraphLibrary& outlib, const Dff_cell& dff);

// Emit a behavioral model graph for `dff` into `outlib`: `q = Flop(clock_pin=clk,
// din=d)`. Mirrors pass.liberty gensim's combinational cell models so a mapped
// DFF Sub resolves for LEC/sim. No-op when a model of that name already exists.
void emit_dff_model(hhds::GraphLibrary& outlib, const Dff_cell& dff);

}  // namespace livehd::liberty
