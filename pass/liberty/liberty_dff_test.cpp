// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// The Liberty DFF-cell picker's parse + rank contract (liberty_dff.hpp) on
// synthetic library text shaped like the two PDKs pass.abc maps against.
// ASAP7's only sub-x4 plain flops are the QN family (`next_state : "!D"`, a
// lone QN output), sky130 exposes Q-only dfxtp and Q/Q_N dfxbp; the pick must
// be the smallest-area plain POSEDGE flop, a negedge or reset-bearing cell must
// never qualify, and the drive ladder must hold only same-shaped cells.

#include "liberty_dff.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "gtest/gtest.h"

namespace {

// Writes `text` to a fresh file under TEST_TMPDIR and returns its path.
std::string write_lib(const std::string& name, const std::string& text) {
  const char* tmp  = std::getenv("TEST_TMPDIR");
  auto        dir  = std::filesystem::path(tmp != nullptr ? tmp : std::filesystem::temp_directory_path().string());
  auto        path = dir / name;
  std::ofstream(path) << text;
  return path.string();
}

// One posedge flop with a Q output (a dfxtp / DFFx1 shape).
std::string q_cell(const std::string& name, double area, const std::string& next = "D", const std::string& clk = "CLK") {
  return "  cell(" + name + ") {\n    area : " + std::to_string(area) + ";\n    ff(IQ, IQN) { next_state : \"" + next
         + "\"; clocked_on : \"" + clk
         + "\"; }\n    pin(CLK) { direction : input; capacitance : 1; clock : true; }\n    pin(D)   { direction : input; "
           "capacitance : 1; }\n    pin(Q)   { direction : output; function : \"IQ\"; }\n  }\n";
}

// ASAP7 shape: the state var is the COMPLEMENT, and the sole output shows it.
std::string qn_cell(const std::string& name, double area, const std::string& next = "!D", const std::string& clk = "CLK") {
  return "  cell (" + name + ") {\n    area : " + std::to_string(area)
         + ";\n    pin (QN) {\n      direction : output;\n      function : \"IQN\";\n      max_capacitance : 46.08;\n    }\n"
           "    pin (CLK) {\n      direction : input;\n      clock : true;\n    }\n    pin (D) {\n      direction : input;\n"
           "    }\n    ff (IQN,IQNN) {\n      clocked_on : \""
         + clk + "\";\n      next_state : \"" + next + "\";\n    }\n  }\n";
}

std::string lib(const std::string& cells) {
  return "library(t) {\n  cell(INVx1) { area : 1; pin(A) { direction : input; } pin(Y) { direction : output; function : "
         "\"A'\"; } }\n"
         + cells + "}\n";
}

}  // namespace

TEST(LibertyDff, Asap7ShapedQnFamilyIsPickedByArea) {
  // DFFHQx4 (Q, 0.3645) vs DFFHQNx1/x2/x3 (QN, 0.2916/0.30618/0.32076) plus the
  // negedge DFFLQNx1 (cheapest of all, clocked_on "!CLK") and an async-reset cell.
  const auto path = write_lib("asap7.lib",
                              lib(q_cell("DFFHQx4", 0.3645) + qn_cell("DFFHQNx3", 0.32076) + qn_cell("DFFHQNx1", 0.2916)
                                  + qn_cell("DFFHQNx2", 0.30618) + qn_cell("DFFLQNx1", 0.2916, "!D", "!CLK")
                                  + "  cell(DFFASRHQNx1) { area : 0.2; ff(IQN, IQNN) { next_state : \"!D\"; clocked_on : "
                                    "\"CLK\"; clear : \"!RESETN\"; preset : \"!SETN\"; }\n    pin(CLK) { direction : input; "
                                    "}\n    pin(D) { direction : input; }\n    pin(RESETN) { direction : input; }\n    "
                                    "pin(SETN) { direction : input; }\n    pin(QN) { direction : output; function : "
                                    "\"IQN\"; }\n  }\n"));
  auto sel = livehd::liberty::resolve_dff_cells(path);
  ASSERT_TRUE(sel.base.has_value());
  EXPECT_EQ(sel.base->name, "DFFHQNx1");
  EXPECT_EQ(sel.base->d_pin, "D");
  EXPECT_EQ(sel.base->clk_pin, "CLK");
  EXPECT_EQ(sel.base->q_pin, "QN");
  EXPECT_TRUE(sel.base->q_inverted);
  EXPECT_DOUBLE_EQ(sel.base->area, 0.2916);
  EXPECT_EQ(sel.base->n_out, 1);
  // Ladder: same pins + polarity, area ascending, base first; DFFHQx4 (Q) and
  // the negedge/reset cells are out.
  ASSERT_EQ(sel.ladder.size(), 3U);
  EXPECT_EQ(sel.ladder[0].name, "DFFHQNx1");
  EXPECT_EQ(sel.ladder[1].name, "DFFHQNx2");
  EXPECT_EQ(sel.ladder[2].name, "DFFHQNx3");
  EXPECT_EQ(livehd::liberty::dff_descriptor(*sel.base), "DFFHQNx1:D:CLK:QN:1");

  // The scan still lists the Q cell (it is a plain posedge flop, just larger).
  auto all = livehd::liberty::scan_dff_cells(path);
  ASSERT_EQ(all.size(), 4U);
  for (const auto& c : all) {
    EXPECT_NE(c.name, "DFFLQNx1") << "negedge flop must not qualify";
    EXPECT_NE(c.name, "DFFASRHQNx1") << "clear/preset flop must not qualify";
  }
}

TEST(LibertyDff, Sky130ShapedPrefersQOnlyDfxtp) {
  // dfxtp_1 (Q, 20.02) < dfxtp_2 (21.27) < dfxbp_1 (Q + Q_N, 23.77); quoted
  // names/directions the way the sky130 text spells them.
  const std::string dfxbp =
      "  cell (\"sky130_fd_sc_hd__dfxbp_1\") {\n    area : 23.772800;\n    ff (\"IQ\",\"IQ_N\") {\n      clocked_on : "
      "\"CLK\";\n      next_state : \"D\";\n    }\n    pin (\"CLK\") { direction : \"input\"; }\n    pin (\"D\") { "
      "direction : \"input\"; }\n    pin (\"Q\") { direction : \"output\"; function : \"IQ\"; }\n    pin (\"Q_N\") { "
      "direction : \"output\"; function : \"IQ_N\"; }\n  }\n";
  const std::string dfxtp1 =
      "  cell (\"sky130_fd_sc_hd__dfxtp_1\") {\n    area : 20.019200;\n    ff (\"IQ\",\"IQ_N\") {\n      clocked_on : "
      "\"CLK\";\n      next_state : \"D\";\n    }\n    pin (\"CLK\") { direction : \"input\"; }\n    pin (\"D\") { "
      "direction : \"input\"; }\n    pin (\"Q\") { direction : \"output\"; function : \"IQ\"; }\n  }\n";
  const std::string dfxtp2 =
      "  cell (\"sky130_fd_sc_hd__dfxtp_2\") {\n    area : 21.270400;\n    ff (\"IQ\",\"IQ_N\") {\n      clocked_on : "
      "\"CLK\";\n      next_state : \"D\";\n    }\n    pin (\"CLK\") { direction : \"input\"; }\n    pin (\"D\") { "
      "direction : \"input\"; }\n    pin (\"Q\") { direction : \"output\"; function : \"IQ\"; }\n  }\n";
  // dfrtp: async reset (`clear`), must be rejected; dfrtn: negedge + reset.
  const std::string dfrtp =
      "  cell (\"sky130_fd_sc_hd__dfrtp_1\") {\n    area : 25.0;\n    ff (\"IQ\",\"IQ_N\") {\n      clear : "
      "\"!RESET_B\";\n      clocked_on : \"CLK\";\n      next_state : \"D\";\n    }\n    pin (\"CLK\") { direction : "
      "\"input\"; }\n    pin (\"D\") { direction : \"input\"; }\n    pin (\"RESET_B\") { direction : \"input\"; }\n    pin "
      "(\"Q\") { direction : \"output\"; function : \"IQ\"; }\n  }\n";
  const auto path = write_lib("sky130.lib", lib(dfxbp + dfxtp2 + dfrtp + dfxtp1));
  auto       sel  = livehd::liberty::resolve_dff_cells(path);
  ASSERT_TRUE(sel.base.has_value());
  EXPECT_EQ(sel.base->name, "sky130_fd_sc_hd__dfxtp_1");
  EXPECT_EQ(sel.base->q_pin, "Q");
  EXPECT_FALSE(sel.base->q_inverted);
  EXPECT_DOUBLE_EQ(sel.base->area, 20.0192);
  // dfxbp shares the pins/polarity (Q chosen), so it is a ladder rung too.
  ASSERT_EQ(sel.ladder.size(), 3U);
  EXPECT_EQ(sel.ladder[0].name, "sky130_fd_sc_hd__dfxtp_1");
  EXPECT_EQ(sel.ladder[1].name, "sky130_fd_sc_hd__dfxtp_2");
  EXPECT_EQ(sel.ladder[2].name, "sky130_fd_sc_hd__dfxbp_1");
}

TEST(LibertyDff, DfxbpPrefersQOverQn) {
  // A Q/Q_N cell alone: Q wins (no inversion to carry), n_out = 2.
  const auto path = write_lib("dfxbp.lib",
                              lib("  cell(dfxbp) { area : 3; ff(IQ, IQ_N) { next_state : \"D\"; clocked_on : \"CLK\"; }\n"
                                  "    pin(CLK) { direction : input; } pin(D) { direction : input; }\n"
                                  "    pin(Q_N) { direction : output; function : \"IQ_N\"; }\n"
                                  "    pin(Q) { direction : output; function : \"IQ\"; }\n  }\n"));
  auto       dff  = livehd::liberty::find_dff_cell(path);
  ASSERT_TRUE(dff.has_value());
  EXPECT_EQ(dff->q_pin, "Q");
  EXPECT_FALSE(dff->q_inverted);
  EXPECT_EQ(dff->n_out, 2);
}

TEST(LibertyDff, QnOnlyWithPlainNextStateIsInvertedOnce) {
  // `ff(IQ,IQN){next_state:"D"}` whose only output reads IQN: QN(t+1) = !D(t).
  const auto path = write_lib("qn_plain.lib",
                              lib("  cell(dffqn) { area : 3; ff(IQ, IQN) { next_state : \"D\"; clocked_on : \"CLK\"; }\n"
                                  "    pin(CLK) { direction : input; } pin(D) { direction : input; }\n"
                                  "    pin(QN) { direction : output; function : \"IQN\"; }\n  }\n"));
  auto       dff  = livehd::liberty::find_dff_cell(path);
  ASSERT_TRUE(dff.has_value());
  EXPECT_EQ(dff->q_pin, "QN");
  EXPECT_TRUE(dff->q_inverted);
}

TEST(LibertyDff, NextStateSpellings) {
  // `D'`, `(!D)`, `!(D)` and `( D )` all parse; scan-mux / enable forms do not.
  for (const auto& [next, inv] : {std::pair{"D'", true}, {"(!D)", true}, {"!(D)", true}, {"( D )", false}, {"(D)'", true}}) {
    const auto path = write_lib("next.lib", lib(qn_cell("X", 1, next)));
    auto       dff  = livehd::liberty::find_dff_cell(path);
    ASSERT_TRUE(dff.has_value()) << next;
    EXPECT_EQ(dff->d_pin, "D") << next;
    // qn_cell's output is the state var IQN (out_inv = false): q_inverted == next_inv.
    EXPECT_EQ(dff->q_inverted, inv) << next;
  }
  for (const char* next : {"(SE*SI)+(!SE*D)", "(!D * !SE) + (!D * !SI) + (SE * !SI)", "D*E+IQ*!E", ""}) {
    const auto path = write_lib("next_bad.lib", lib(qn_cell("X", 1, next)));
    EXPECT_FALSE(livehd::liberty::find_dff_cell(path).has_value()) << next;
  }
}

TEST(LibertyDff, NegedgeIsRejectedEvenWhenCheapest) {
  const auto path = write_lib("negedge.lib", lib(qn_cell("DFFNLx1", 4, "!D", "!CLK") + q_cell("DFFx1", 6)));
  auto       dff  = livehd::liberty::find_dff_cell(path);
  ASSERT_TRUE(dff.has_value());
  EXPECT_EQ(dff->name, "DFFx1");
  // And explicitly asking for the negedge cell yields nothing rather than a miscompile.
  EXPECT_FALSE(livehd::liberty::find_dff_cell(path, "DFFNLx1").has_value());
  // A negedge-only library has no pick.
  const auto only = write_lib("negedge_only.lib", lib(qn_cell("DFFNLx1", 4, "!D", "!CLK")));
  EXPECT_FALSE(livehd::liberty::find_dff_cell(only).has_value());
}

TEST(LibertyDff, AreaRankingAndTieBreaks) {
  // Same area: fewer outputs first, then a non-inverted Q, then the name.
  const auto path = write_lib("rank.lib",
                              lib(qn_cell("Z_qn", 2) + q_cell("B_q", 2) + q_cell("A_q", 2)
                                  + "  cell(A_bp) { area : 2; ff(IQ, IQN) { next_state : \"D\"; clocked_on : \"CLK\"; }\n"
                                    "    pin(CLK) { direction : input; } pin(D) { direction : input; }\n"
                                    "    pin(Q) { direction : output; function : \"IQ\"; }\n"
                                    "    pin(QN) { direction : output; function : \"IQN\"; }\n  }\n"
                                  + q_cell("Big", 9)));
  auto       dff  = livehd::liberty::find_dff_cell(path);
  ASSERT_TRUE(dff.has_value());
  EXPECT_EQ(dff->name, "A_q");
  // Area beats file order: the first cell in the file is not the pick.
  const auto path2 = write_lib("rank2.lib", lib(q_cell("First", 9) + q_cell("Cheap", 1)));
  EXPECT_EQ(livehd::liberty::find_dff_cell(path2)->name, "Cheap");
  // A cell without an area attribute ranks last.
  const auto path3 = write_lib("rank3.lib",
                               lib("  cell(NoArea) { ff(IQ, IQN) { next_state : \"D\"; clocked_on : \"CLK\"; }\n"
                                   "    pin(CLK) { direction : input; } pin(D) { direction : input; }\n"
                                   "    pin(Q) { direction : output; function : \"IQ\"; }\n  }\n"
                                   + q_cell("Sized", 50)));
  EXPECT_EQ(livehd::liberty::find_dff_cell(path3)->name, "Sized");
}

TEST(LibertyDff, AreaReadsTheCellNotAPinGroup) {
  // `area` after the pin groups, with a pin carrying an area-like key: the
  // cell-level value must be the one parsed.
  const auto path = write_lib("area_late.lib",
                              lib("  cell(Late) { ff(IQ, IQN) { next_state : \"D\"; clocked_on : \"CLK\"; }\n"
                                  "    pin(CLK) { direction : input; }\n    pin(D) { direction : input; }\n"
                                  "    pin(Q) { direction : output; function : \"IQ\"; area : 99; }\n"
                                  "    area : 7.25;\n  }\n"));
  auto       dff  = livehd::liberty::find_dff_cell(path);
  ASSERT_TRUE(dff.has_value());
  EXPECT_DOUBLE_EQ(dff->area, 7.25);
}

TEST(LibertyDff, PreferNamesOneCellAndLadderIsThatCell) {
  const auto path = write_lib("prefer.lib", lib(qn_cell("DFFHQNx1", 0.2916) + qn_cell("DFFHQNx2", 0.30618) + q_cell("DFFHQx4", 0.3645)));
  auto       sel  = livehd::liberty::resolve_dff_cells(path, "DFFHQNx2");
  ASSERT_TRUE(sel.base.has_value());
  EXPECT_EQ(sel.base->name, "DFFHQNx2");
  ASSERT_EQ(sel.ladder.size(), 1U);
  EXPECT_EQ(sel.ladder[0].name, "DFFHQNx2");
  // Explicit Q cell: honored as-is.
  EXPECT_EQ(livehd::liberty::find_dff_cell(path, "DFFHQx4")->q_pin, "Q");
  // Unknown / non-flop name: nothing.
  EXPECT_FALSE(livehd::liberty::find_dff_cell(path, "INVx1").has_value());
  EXPECT_FALSE(livehd::liberty::find_dff_cell(path, "nope").has_value());
  // find_dff_ladder from an explicit base still lists every same-shaped cell.
  auto ladder = livehd::liberty::find_dff_ladder(path, *sel.base);
  ASSERT_EQ(ladder.size(), 2U);
  EXPECT_EQ(ladder[0].name, "DFFHQNx1");
  EXPECT_EQ(ladder[1].name, "DFFHQNx2");
}

TEST(LibertyDff, TestLibsKeepTheirPicks) {
  // The hermetic test libraries pass.abc's shell tests grep for: test.lib's
  // sole DFFx1; test_qn.lib's DFFNx1 (5) over DFFx1 (6), never the negedge
  // DFFNLx1 (4), with DFFNx2 as the second rung.
  const auto t = write_lib("test.lib", lib(q_cell("DFFx1", 6)));
  EXPECT_EQ(livehd::liberty::find_dff_cell(t)->name, "DFFx1");
  const auto tq = write_lib("test_qn.lib",
                            lib(q_cell("DFFx1", 6) + qn_cell("DFFNx1", 5) + qn_cell("DFFNx2", 5.5) + qn_cell("DFFNLx1", 4, "!D", "!CLK")));
  auto       sel = livehd::liberty::resolve_dff_cells(tq);
  ASSERT_TRUE(sel.base.has_value());
  EXPECT_EQ(sel.base->name, "DFFNx1");
  EXPECT_TRUE(sel.base->q_inverted);
  ASSERT_EQ(sel.ladder.size(), 2U);
  EXPECT_EQ(sel.ladder[1].name, "DFFNx2");
}

// --- register overhead (clk->Q + setup) for pass.abc's reg_margin=auto --------

namespace {

// ASAP7-shaped QN flop with real timing groups: a rising_edge clk->Q arc on
// QN (3x3 rise/fall tables, plus a min_pulse_width group that must be
// ignored), a setup_rising arc on D (plus a hold_rising group that must be
// ignored) and `\` line continuations between the rows like the vendor file.
std::string timed_qn_cell(const std::string& name, double area) {
  return "  cell (" + name + ") {\n    area : " + std::to_string(area)
         + ";\n    pin (QN) {\n      direction : output;\n      function : \"IQN\";\n"
           "      timing () {\n        related_pin : \"CLK\";\n        timing_sense : non_unate;\n        timing_type : rising_edge;\n"
           "        cell_rise (delay_template_3x3) {\n          index_1 (\"5, 10, 20\");\n          index_2 (\"0.72, 1.44, 2.88\");\n"
           "          values ( \\\n            \"1, 2, 3\", \\\n            \"4, 5, 6\", \\\n            \"7, 8, 9\" \\\n          );\n        }\n"
           "        cell_fall (delay_template_3x3) {\n          index_1 (\"5, 10, 20\");\n          index_2 (\"0.72, 1.44, 2.88\");\n"
           "          values ( \"2, 3.5, 4\", \"5, 6, 7\", \"8, 9, 10\" );\n        }\n      }\n"
           "      timing () {\n        related_pin : \"CLK\";\n        timing_type : min_pulse_width;\n"
           "        rise_constraint (mpw) { values ( \"500, 500, 500\" ); }\n      }\n    }\n"
           "    pin (CLK) {\n      direction : input;\n      clock : true;\n    }\n"
           "    pin (D) {\n      direction : input;\n"
           "      timing () {\n        related_pin : \"CLK\";\n        timing_type : hold_rising;\n"
           "        rise_constraint (c) { values ( \"99, 99, 99\", \"99, 99, 99\", \"99, 99, 99\" ); }\n      }\n"
           "      timing () {\n        related_pin : \"CLK\";\n        timing_type : setup_rising;\n"
           "        rise_constraint (c) {\n          index_1 (\"5, 10, 20\");\n          index_2 (\"5, 10, 20\");\n"
           "          values ( \\\n            \"1, 2, 3\", \\\n            \"4, 5, 6\", \\\n            \"7, 8, 9\" \\\n          );\n        }\n"
           "        fall_constraint (c) { values ( \"0, 0, 1\", \"0, -0.5, 0\", \"0, 0, 0\" ); }\n      }\n    }\n"
           "    ff (IQN,IQNN) {\n      clocked_on : \"CLK\";\n      next_state : \"!D\";\n    }\n  }\n";
}

std::string lib_with_unit(const std::string& unit, const std::string& cells) {
  return "library(t) {\n  time_unit : \"" + unit
         + "\";\n  cell(INVx1) { area : 1; pin(A) { direction : input; } pin(Y) { direction : output; function : \"A'\"; } }\n"
         + cells + "}\n";
}

}  // namespace

TEST(LibertyDff, RegisterOverheadReadsZeroSlewMidLoadAndTableCenter) {
  // clk->Q = max(cell_rise[0][mid] = 2, cell_fall[0][mid] = 3.5) = 3.5; the
  // min_pulse_width group on the same pin and the hold_rising group on D are
  // skipped by their timing_type; setup = max(rise[mid][mid] = 5,
  // fall[mid][mid] = -0.5) = 5. ASAP7 says "1ps".
  const auto ps = write_lib("timed_ps.lib", lib_with_unit("1ps", timed_qn_cell("DFFHQNx1", 0.2916)));
  auto       c  = livehd::liberty::find_dff_cell(ps);
  ASSERT_TRUE(c.has_value());
  EXPECT_TRUE(c->q_inverted);
  EXPECT_DOUBLE_EQ(c->clk_to_q_ps, 3.5);
  EXPECT_DOUBLE_EQ(c->setup_ps, 5.0);
}

TEST(LibertyDff, RegisterOverheadScalesByTimeUnit) {
  // sky130 says "1ns": the same tables mean 1000x the picoseconds. A "10ps"
  // unit scales by 10, and a library without a time_unit takes the Liberty
  // default (1ns).
  const auto ns = write_lib("timed_ns.lib", lib_with_unit("1ns", timed_qn_cell("dfxtp_1", 20.0192)));
  auto       c  = livehd::liberty::find_dff_cell(ns);
  ASSERT_TRUE(c.has_value());
  EXPECT_DOUBLE_EQ(c->clk_to_q_ps, 3500.0);
  EXPECT_DOUBLE_EQ(c->setup_ps, 5000.0);
  const auto tens = write_lib("timed_10ps.lib", lib_with_unit("10ps", timed_qn_cell("X", 1)));
  EXPECT_DOUBLE_EQ(livehd::liberty::find_dff_cell(tens)->clk_to_q_ps, 35.0);
  const auto none = write_lib("timed_nounit.lib", lib(timed_qn_cell("X", 1)));
  EXPECT_DOUBLE_EQ(livehd::liberty::find_dff_cell(none)->clk_to_q_ps, 3500.0);
}

TEST(LibertyDff, RegisterOverheadIsZeroWithoutTimingTables) {
  // The hermetic test Liberties (test.lib, test_qn.lib) carry no timing on
  // their flops: no margin may be invented for them.
  const auto t = write_lib("untimed.lib", lib(q_cell("DFFx1", 6)));
  auto       c = livehd::liberty::find_dff_cell(t);
  ASSERT_TRUE(c.has_value());
  EXPECT_DOUBLE_EQ(c->clk_to_q_ps, 0.0);
  EXPECT_DOUBLE_EQ(c->setup_ps, 0.0);
}

TEST(LibertyDff, RegisterOverheadFollowsTheChosenOutputPin) {
  // A dfxbp exposes Q and Q_N; the pick wires Q, so its clk->Q is Q's table
  // (rise 7 at [0][mid]), not Q_N's (rise 70). A 1-D table is its one row.
  const std::string dfxbp
      = "  cell(dfxbp_1) {\n    area : 25;\n    ff(IQ, IQ_N) { next_state : \"D\"; clocked_on : \"CLK\"; }\n"
        "    pin(CLK) { direction : input; }\n    pin(D) { direction : input; }\n"
        "    pin(Q) { direction : output; function : \"IQ\";\n"
        "      timing() { related_pin : \"CLK\"; timing_type : rising_edge; cell_rise(t) { values(\"6, 7, 8\"); } } }\n"
        "    pin(Q_N) { direction : output; function : \"IQ_N\";\n"
        "      timing() { related_pin : \"CLK\"; timing_type : rising_edge; cell_rise(t) { values(\"60, 70, 80\"); } } }\n  }\n";
  const auto path = write_lib("dfxbp_timed.lib", lib_with_unit("1ps", dfxbp));
  auto       c    = livehd::liberty::find_dff_cell(path);
  ASSERT_TRUE(c.has_value());
  EXPECT_EQ(c->q_pin, "Q");
  EXPECT_DOUBLE_EQ(c->clk_to_q_ps, 7.0);
  EXPECT_DOUBLE_EQ(c->setup_ps, 0.0);
}
