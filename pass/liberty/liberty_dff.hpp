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
  // Asynchronous clear/preset, stated in terms of the OUTPUT pin the netlist
  // uses as Q (q_pin), never in the Liberty's clear/preset vocabulary: when
  // asserted, `reset0_pin` forces q_pin to 0 and `reset1_pin` forces it to 1.
  // The two differ exactly when q_pin shows the complement state var. ASAP7's
  // DFFASRHQNx1 (`ff(IQN,IQNN) {clear:"!SETN"; preset:"!RESETN"}`, QN=IQN) is
  // therefore reset0=SETN, reset1=RESETN, both active low; sky130 dfrtp_1
  // (`clear:"!RESET_B"`, Q=IQ) is reset0=RESET_B. `*_low` = asserted at 0.
  // Empty on a plain flop (scan_dff_cells / resolve_dff_cells base+ladder).
  std::string reset0_pin;
  bool        reset0_low = false;
  std::string reset1_pin;
  bool        reset1_low = false;
  // q_pin while BOTH pins are asserted (clear_preset_var1/2 of the var q_pin
  // shows: L=0, H=1), -1 when the library leaves it unstated or N/T/X.
  // pass.abc never asserts both; the model (emit_dff_model) needs a value.
  int         both_value = -1;
  // A transparent LATCH cell (a Liberty `latch(IQ,IQN) {data_in; enable}`
  // group) rather than a flop: `clk_pin` is then its ENABLE pin, `d_pin` its
  // data_in, and the cell is transparent while clk_pin is 1 (`en_low` false:
  // ASAP7 DHLx1, sky130 dlxtp GATE) or 0 (`en_low` true: `enable : "!CLK"`,
  // ASAP7 DLLx1, sky130 dlxtn GATE_N). q_inverted / reset0 / reset1 keep their
  // flop meaning (q_pin shows !D while transparent; a reset pin forces q_pin
  // to 0 / 1 with priority over the enable). The same IO port convention
  // (create_dff_io: d=1, enable=2, q=3, reset0=4, reset1=5) applies.
  bool        latch  = false;
  bool        en_low = false;
  [[nodiscard]] bool is_async() const { return !reset0_pin.empty() || !reset1_pin.empty(); }
  [[nodiscard]] const std::string& reset_pin(bool value) const { return value ? reset1_pin : reset0_pin; }
  [[nodiscard]] bool               reset_low(bool value) const { return value ? reset1_low : reset0_low; }
};

// An INTEGRATED CLOCK GATE cell: `clock_gating_integrated_cell :
// latch_posedge[_precontrol|_postcontrol]` -- the enable (OR the test pin) is
// held by a latch transparent while CLK is LOW, and the gated clock is
// `CLK & latch`. So its output rises only on a CLK rise whose enable was
// sampled high, which is exactly the latch-based gate RTL spells as
// `always_latch if (!clk) en_l = en; assign gclk = clk & en_l;`. ASAP7:
// ICGx1_ASAP7_75t_R (CLK, ENA, SE -> GCLK); sky130: dlclkp_1 (CLK, GATE ->
// GCLK) and sdlclkp_1 (+ SCE). Pins are identified by the Liberty's own
// clock_gate_{clock,enable,test,out}_pin attributes, never by spelling.
struct Icg_cell {
  std::string name;
  std::string clk_pin;   // clock_gate_clock_pin
  std::string en_pin;    // clock_gate_enable_pin
  std::string test_pin;  // clock_gate_test_pin (active high; tied 0), empty when the cell has none
  std::string out_pin;   // clock_gate_out_pin
  double      area = 0;
};

// Every non-dont_use `latch_posedge*` ICG cell whose pins are exactly one
// clock, one enable, at most one test input and one gated-clock output whose
// (state_)function is `CLK & <state>`. File order; unranked.
std::vector<Icg_cell> scan_icg_cells(const std::string& lib_files);

// Every plain POSEDGE D-flop in the whitespace-separated Liberty file list: a
// cell with an `ff(){}` group whose `clocked_on` is a bare pin (posedge, not
// `!CLK` -- that is what keeps ASAP7's negedge DFFLQ* / sky130's dfrtn out),
// `next_state` a bare pin or its complement (`!D`, `D'`, `(!D)`; no scan/enable
// logic), no async clear/preset, exactly one data + one clock input, and a Q
// (preferred) or QN output. File order; unranked.
std::vector<Dff_cell> scan_dff_cells(const std::string& lib_files);
// Every posedge D-flop that is plain except for an asynchronous clear and/or
// preset -- each a bare input pin or its complement (`!RESET_B`, `RN'`),
// distinct from D and CLK -- with a Q/QN output whose function names a state
// var (so which pin forces which output value is known). File order; unranked;
// dont_use cells excluded.
std::vector<Dff_cell> scan_async_dff_cells(const std::string& lib_files);
// The names of every cell marked `dont_use : true` (a separate full read;
// resolve_dff_cells fills Dff_selection::dont_use from the same pass).
std::vector<std::string> scan_dont_use_cells(const std::string& lib_files);

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
  // Every cell marked `dont_use : true`, in file order: the set ABC's reader
  // skips (so no rung above names one); pass.abc reports it once per run.
  std::vector<std::string> dont_use;
  // The asynchronous-reset register cells: areset_ladder[v] maps a register
  // whose asynchronous reset loads bit value v (0: a clear to 0, 1: a preset to
  // 1). Ranked like the plain pick (area, outputs, fewest async pins, Q over
  // QN, name); front() is the pick, the rest its same-shaped drive ladder.
  // Empty = the library has no such cell, and those register bits stay native
  // flops. Independent of `prefer` (which names the plain register cell).
  std::vector<Dff_cell> areset_ladder[2];
  // The integrated clock-gate cells a latch+AND clock gate maps onto: the
  // smallest-area cell (a test-pin-less one on an area tie), then its
  // same-pinned drive ladder by strictly increasing area (at most five rungs;
  // the read-back picks one per doubling of the clocked bits past 8). Empty =
  // the library has none, and gated
  // registers stay native flops. Independent of `prefer`.
  std::vector<Icg_cell> icg_ladder;
  // The transparent data-latch cells (Dff_cell::latch) a level-sensitive LATCH
  // maps onto: latch_ladder[en_low][kind], kind 0 = a plain latch, 1 = one
  // whose reset forces q_pin to 0 (reset0_pin), 2 = to 1 (reset1_pin). Each is
  // ranked like the flop picks (area, then fewer outputs / reset pins, Q over
  // QN, name) -- front() is the pick, the rest its same-shaped drive ladder by
  // area. dont_use, isolation / level-shifter / clock-gate cells, and any cell
  // with an input beyond data, enable and bare clear/preset pins never
  // qualify. Empty = that latch shape stays native. Independent of `prefer`.
  std::vector<Dff_cell> latch_ladder[2][3];
  [[nodiscard]] bool    has_latch_cells() const {
    for (const auto& pol : latch_ladder) {
      for (const auto& l : pol) {
        if (!l.empty()) {
          return true;
        }
      }
    }
    return false;
  }
};
Dff_selection resolve_dff_cells(const std::string& lib_files, std::string_view prefer = "");

// Every transparent data-latch cell (Dff_cell::latch) in the Liberty that
// qualifies for latch_ladder (see there), in file order; unranked.
std::vector<Dff_cell> scan_latch_cells(const std::string& lib_files);

// Every distinct cell a register may be mapped to under `sel`: the plain
// ladder, then each asynchronous-reset ladder (a cell serving both reset values
// once), then the data-latch ladders. What gensim models and what a netlist
// reader treats as a register.
std::vector<Dff_cell> selection_cells(const Dff_selection& sel);

// `name:d:clk:q:inverted` -- the resolved pick as one string, for the pass.abc
// incremental-cache salt (a cached mapped body names its DFF Sub decl, so the
// pick has to be part of the key, not just the raw `dff_cell` option).
std::string dff_descriptor(const Dff_cell& dff);
// dff_descriptor of the base pick plus both asynchronous-reset picks (an
// `areset` suffix per non-empty ladder, with its reset pins and polarities):
// the incremental-cache salt, since a cached mapped body names those cells too.
std::string dff_selection_descriptor(const Dff_selection& sel, std::string_view fallback = "");

// Create-or-find the 1-bit blackbox IO decl (inputs d_pin, clk_pin; output q_pin)
// for `dff` in `outlib`. Port ids: d=1, clk=2, q=3, and for an asynchronous
// cell reset0_pin=4 / reset1_pin=5 when present (a fixed convention so the
// pass.abc netlist Sub and the gensim model agree). Idempotent (find-or-create).
std::shared_ptr<hhds::GraphIO> create_dff_io(hhds::GraphLibrary& outlib, const Dff_cell& dff);

// Emit a behavioral model graph for `dff` into `outlib`: `q = Flop(clock_pin=clk,
// din=d)`, wrapped in a Not when the cell's output is QN (q_inverted). An
// asynchronous cell's Flop also carries `async` + `reset_pin` + `initial` in
// q_pin terms: one pin => reset_pin=that pin (`negreset` when active low),
// initial=its forced value; both pins => reset_pin = act0|act1 and initial =
// act1 (both_value 1) or act1&!act0 (otherwise). Mirrors
// pass.liberty gensim's combinational cell models so a mapped DFF Sub resolves
// for LEC/sim. A latch cell (Dff_cell::latch) is `q = Latch(din=d, enable=clk)`
// instead -- `enable = Not(clk)` for an active-low cell, `din = Not(d)` for a QN
// one (the state IS the pin, as for the flop), and a reset as the Latch's
// reset_pin/negreset/initial (the same q_pin-terms mapping). No-op when a model
// of that name already exists.
void emit_dff_model(hhds::GraphLibrary& outlib, const Dff_cell& dff);

// `name:clk:en:test:out` for the incremental-cache salt (dff_selection_descriptor
// appends it as `|icg=...`).
std::string icg_descriptor(const Icg_cell& icg);

// Create-or-find the 1-bit blackbox IO decl of an ICG cell. Port ids: clk=1,
// en=2, test=3 (when present), out=4.
std::shared_ptr<hhds::GraphIO> create_icg_io(hhds::GraphLibrary& outlib, const Icg_cell& icg);

// Emit the ICG's behavioral model into `outlib`: a Latch transparent while CLK
// is low (enable = !CLK) holding `en | test`, and `out = CLK & latch` -- the
// same body shape a reader produces for the RTL clock gate, so
// latch_contract::match_icg_def recognizes a mapped ICG Sub exactly like the
// source gate. No-op when a model of that name already exists.
void emit_icg_model(hhds::GraphLibrary& outlib, const Icg_cell& icg);

}  // namespace livehd::liberty
