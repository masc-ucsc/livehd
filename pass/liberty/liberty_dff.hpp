// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "hhds/graph.hpp"

// Liberty DFF-cell support shared by pass.abc (which maps flops to DFF-cell Subs
// when register=true) and pass.liberty gensim (which emits a behavioral model for
// that cell). ABC's read_lib DROPS sequential cells before they reach the Mio
// library, so the only way to learn a flop cell's D/CLK/Q pins is to scan the
// Liberty text directly — that is what find_dff_cell does.
namespace livehd::liberty {

struct Dff_cell {
  std::string name;     // Liberty cell name (e.g. sky130_fd_sc_hd__dfxtp_1)
  std::string d_pin;    // data input pin  (Liberty ff `next_state`, sign stripped)
  std::string clk_pin;  // clock input pin (Liberty ff `clocked_on`, posedge)
  std::string q_pin;    // output pin used as Q (see q_inverted)
  // Cell semantics: q_pin(t+1) = q_inverted ? !d_pin(t) : d_pin(t). ASAP7's only
  // plain posedge flops below the x4 drive are the QN family (`ff (IQN,IQNN)
  // {next_state : "!D"}` + `pin (QN) {function : "IQN"}`): DFFHQNx1 is 0.2916
  // um^2 against DFFHQx4's 0.3645, -20% per flop suite-wide, so the inverted
  // output has to be a first-class pick and the read-back owns the inversion.
  bool        q_inverted = false;
  double      area       = 0;  // Liberty `area` (0 when the cell body has none)
  int         n_out      = 0;  // output-pin count (1 for a Q-only dfxtp, 2 for a Q/Q_N dfxbp)
  // The register overhead a mapped region's combinational delay has to leave
  // room for, in PICOSECONDS (scaled from the library's `time_unit`), read off
  // the cell's own `timing()` groups: `clk_to_q_ps` = the larger of the Q pin's
  // CLK `rising_edge` `cell_rise`/`cell_fall` tables at (zero clock slew, the
  // middle load), `setup_ps` = the larger of the D pin's CLK `setup_rising`
  // `rise_constraint`/`fall_constraint` tables at the table center. Those
  // entries are calibrated against OpenSTA's full-path numbers on the lhdtrack
  // netlists (see liberty_dff.cpp Table_pick): ASAP7 DFFHQNx1 73.0 + 10.9 =
  // 83.9 ps against 75-87 measured, sky130 dfxtp_1 328 + 200 = 528 ps against
  // 250-440. Zero when the cell has no such tables (the hermetic test
  // Liberties): no margin is then subtracted. pass.abc's `reg_margin=auto` =
  // clk_to_q_ps + setup_ps.
  double      clk_to_q_ps = 0;
  double      setup_ps    = 0;
};

// Every plain POSEDGE D-flop in the whitespace-separated Liberty file list: a
// cell with an `ff(){}` group whose `clocked_on` is a bare pin (posedge, not
// `!CLK` -- that is what keeps ASAP7's negedge DFFLQ* / sky130's dfrtn out),
// `next_state` a bare pin or its complement (`!D`, `D'`, `(!D)`; no scan/enable
// logic), no async clear/preset, exactly one data + one clock input, and a Q
// (preferred) or QN output. File order; unranked.
std::vector<Dff_cell> scan_dff_cells(const std::string& lib_files);

// The register-mapping pick. With `prefer` empty the candidates are ranked
// (area asc, n_out asc, non-inverted Q first, name asc): area is the one
// number a technology library states about a flop, so it is the primary key
// (ASAP7 DFFHQNx1 over DFFHQx4; sky130 dfxtp_1 over dfxbp_1 and the x2/x4
// drives) and the rest only break exact ties deterministically. When `prefer`
// names a cell only that cell is considered. Returns nullopt when nothing
// qualifies.
std::optional<Dff_cell> find_dff_cell(const std::string& lib_files, std::string_view prefer = "");

// The drive ladder of `base`: every plain posedge flop with the SAME d/clk/q
// pin names and the same output polarity (ASAP7: DFFHQNx1 0.2916 < DFFHQNx2
// 0.30618 < DFFHQNx3 0.32076), sorted by area ascending, `base` included.
// pass.abc picks a rung by the Q net's mapped fanout: ABC's `buffer -N` tail
// never buffers a latch output (a CI), so a high-fanout register would
// otherwise sit on the weakest drive (measured br_amba_axi2axil 542 -> 637
// ps on x1 alone; x3 574).
std::vector<Dff_cell> find_dff_ladder(const std::string& lib_files, const Dff_cell& base);

// One scan, both answers. With an explicit `prefer` the ladder is just that
// cell: a user naming a drive strength gets exactly it.
struct Dff_selection {
  std::optional<Dff_cell> base;
  std::vector<Dff_cell>   ladder;
};
Dff_selection resolve_dff_cells(const std::string& lib_files, std::string_view prefer = "");

// `name:d:clk:q:inverted` -- the resolved pick as one string, for the pass.abc
// incremental-cache salt (a cached mapped body names its DFF Sub decl, so the
// pick has to be part of the key, not just the raw `dff_cell` option).
std::string dff_descriptor(const Dff_cell& dff);

// Create-or-find the 1-bit blackbox IO decl (inputs d_pin, clk_pin; output q_pin)
// for `dff` in `outlib`. Port ids: d=1, clk=2, q=3 (a fixed convention so the
// pass.abc netlist Sub and the gensim model agree). Idempotent (find-or-create).
std::shared_ptr<hhds::GraphIO> create_dff_io(hhds::GraphLibrary& outlib, const Dff_cell& dff);

// Emit a behavioral model graph for `dff` into `outlib`: `q = Flop(clock_pin=clk,
// din=d)`, wrapped in a Not when the cell's output is QN (q_inverted). Mirrors
// pass.liberty gensim's combinational cell models so a mapped DFF Sub resolves
// for LEC/sim. No-op when a model of that name already exists.
void emit_dff_model(hhds::GraphLibrary& outlib, const Dff_cell& dff);

}  // namespace livehd::liberty
