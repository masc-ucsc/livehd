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
#include <vector>

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

TEST(LibertyDff, BufferedFilesAndCommentsPreserveCellBoundaries) {
  const auto first = write_lib("comment-first.lib", std::string(65535, ' ')
      + "/*" + std::string(65536, 'x') + "*/\n"
      + lib(q_cell("FIRST", 2)) + "// no final newline");
  const auto second = write_lib("comment-second.lib",
      "// ignored cell(BAD) {}\n" + lib(q_cell("SECOND", 3))
      + "/* unterminated cell(ALSO_BAD) {}");
  const auto missing = first + ".missing";
  const auto cells = livehd::liberty::scan_dff_cells(first + " " + missing + " " + second);
  ASSERT_EQ(cells.size(), 2U);
  EXPECT_EQ(cells[0].name, "FIRST");
  EXPECT_EQ(cells[1].name, "SECOND");
  EXPECT_DOUBLE_EQ(cells[0].area, 2);
  EXPECT_DOUBLE_EQ(cells[1].area, 3);
  // A slash that does not begin a comment must remain part of the quoted name.
  const auto slash = write_lib("slash.lib", lib(q_cell("\"path/name\"", 4)) + "/");
  const auto named = livehd::liberty::scan_dff_cells(slash);
  ASSERT_EQ(named.size(), 1U);
  EXPECT_EQ(named.front().name, "path/name");
}

TEST(LibertyDff, CombinationalInventoryKeepsPinOrderAndExcludesUnsupportedCells) {
  const auto cell = [](const std::string& name, const std::string& extra, const std::string& output) {
    return "cell(" + name + ") { area: 2; " + extra
           + " pin(B) {direction:input;} pin(A) {direction:input;} pin(Y) {direction:output; function:\"A*!B\"; " + output + "} }";
  };
  auto path
      = write_lib("comb.lib",
                  lib(cell("ANDN", "", "") + cell("DISABLED", "dont_use:true;", "") + cell("TRISTATE", "", "three_state:\"A\";")
                      + cell("MULTI", "pin(Z) {direction:output; function:\"B\";}", "") + q_cell("STATE", 0.1)));
  const auto cells = livehd::liberty::scan_comb_cells(path);
  ASSERT_EQ(cells.size(), 2U);
  EXPECT_EQ(cells[0].name, "INVx1");
  EXPECT_EQ(cells[1].name, "ANDN");
  EXPECT_EQ(cells[1].inputs, (std::vector<std::string>{"B", "A"}));
  EXPECT_EQ(cells[1].output, "Y");
  EXPECT_EQ(cells[1].function, "A*!B");
  EXPECT_DOUBLE_EQ(cells[1].area, 2);
}

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
  auto       sel  = livehd::liberty::resolve_dff_cells(path);
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
  const std::string dfxbp
      = "  cell (\"sky130_fd_sc_hd__dfxbp_1\") {\n    area : 23.772800;\n    ff (\"IQ\",\"IQ_N\") {\n      clocked_on : "
        "\"CLK\";\n      next_state : \"D\";\n    }\n    pin (\"CLK\") { direction : \"input\"; }\n    pin (\"D\") { "
        "direction : \"input\"; }\n    pin (\"Q\") { direction : \"output\"; function : \"IQ\"; }\n    pin (\"Q_N\") { "
        "direction : \"output\"; function : \"IQ_N\"; }\n  }\n";
  const std::string dfxtp1
      = "  cell (\"sky130_fd_sc_hd__dfxtp_1\") {\n    area : 20.019200;\n    ff (\"IQ\",\"IQ_N\") {\n      clocked_on : "
        "\"CLK\";\n      next_state : \"D\";\n    }\n    pin (\"CLK\") { direction : \"input\"; }\n    pin (\"D\") { "
        "direction : \"input\"; }\n    pin (\"Q\") { direction : \"output\"; function : \"IQ\"; }\n  }\n";
  const std::string dfxtp2
      = "  cell (\"sky130_fd_sc_hd__dfxtp_2\") {\n    area : 21.270400;\n    ff (\"IQ\",\"IQ_N\") {\n      clocked_on : "
        "\"CLK\";\n      next_state : \"D\";\n    }\n    pin (\"CLK\") { direction : \"input\"; }\n    pin (\"D\") { "
        "direction : \"input\"; }\n    pin (\"Q\") { direction : \"output\"; function : \"IQ\"; }\n  }\n";
  // dfrtp: async reset (`clear`), must be rejected; dfrtn: negedge + reset.
  const std::string dfrtp
      = "  cell (\"sky130_fd_sc_hd__dfrtp_1\") {\n    area : 25.0;\n    ff (\"IQ\",\"IQ_N\") {\n      clear : "
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
  for (const auto& [next, inv] : {
           std::pair{   "D'",  true},
           { "(!D)",  true},
           { "!(D)",  true},
           {"( D )", false},
           { "(D)'",  true}
  }) {
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
  const auto path
      = write_lib("prefer.lib", lib(qn_cell("DFFHQNx1", 0.2916) + qn_cell("DFFHQNx2", 0.30618) + q_cell("DFFHQx4", 0.3645)));
  auto sel = livehd::liberty::resolve_dff_cells(path, "DFFHQNx2");
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
  const auto tq
      = write_lib("test_qn.lib",
                  lib(q_cell("DFFx1", 6) + qn_cell("DFFNx1", 5) + qn_cell("DFFNx2", 5.5) + qn_cell("DFFNLx1", 4, "!D", "!CLK")));
  auto sel = livehd::liberty::resolve_dff_cells(tq);
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

// --- asynchronous clear/preset register cells (Dff_selection::areset_ladder) --

namespace {

// A flop with the given ff head/attributes and extra input pins, whose one
// output `out` shows state var `fn`.
std::string async_cell(const std::string& name, double area, const std::string& head, const std::string& ff_attrs,
                       const std::vector<std::string>& pins, const std::string& out, const std::string& fn,
                       const std::string& next = "D", bool dont_use = false, const std::string& clk = "CLK") {
  std::string s = "  cell (" + name + ") {\n    area : " + std::to_string(area) + ";\n";
  if (dont_use) {
    s += "    dont_use : true;\n";
  }
  s += "    ff (" + head + ") {\n      clocked_on : \"" + clk + "\";\n      next_state : \"" + next + "\";\n" + ff_attrs
       + "    }\n";
  s += "    pin (CLK) { direction : input; clock : true; }\n    pin (D) { direction : input; }\n";
  for (const auto& p : pins) {
    s += "    pin (" + p + ") { direction : input; }\n";
  }
  s += "    pin (" + out + ") { direction : output; function : \"" + fn + "\"; }\n  }\n";
  return s;
}

}  // namespace

TEST(LibertyDff, Asap7AsyncCellIsStatedInQnTerms) {
  // DFFASRHQNx1 exactly as ASAP7 spells it: the state var is IQN (QN = IQN),
  // `clear : "!SETN"` drives IQN -- hence QN -- to 0 and `preset : "!RESETN"`
  // to 1. So the register bit resetting to 0 takes SETN, the one resetting to 1
  // RESETN, both active low; clear_preset_var1 L = QN 0 while both assert.
  const auto path = write_lib("asap7_async.lib",
                              lib(qn_cell("DFFHQNx1", 0.2916)
                                  + async_cell("DFFASRHQNx1",
                                               0.37908,
                                               "IQN,IQNN",
                                               "      clear : \"!SETN\";\n      clear_preset_var1 : L;\n      "
                                               "clear_preset_var2 : L;\n      preset : \"!RESETN\";\n",
                                               {"RESETN", "SETN"},
                                               "QN",
                                               "IQN",
                                               "!D")));
  auto       sel  = livehd::liberty::resolve_dff_cells(path);
  ASSERT_TRUE(sel.base.has_value());
  EXPECT_EQ(sel.base->name, "DFFHQNx1");  // the async cell never becomes the plain pick
  EXPECT_FALSE(sel.base->is_async());
  for (int v = 0; v < 2; ++v) {
    ASSERT_EQ(sel.areset_ladder[v].size(), 1U) << v;
    const auto& c = sel.areset_ladder[v].front();
    EXPECT_EQ(c.name, "DFFASRHQNx1");
    EXPECT_TRUE(c.q_inverted);
    EXPECT_EQ(c.q_pin, "QN");
    EXPECT_EQ(c.reset0_pin, "SETN");
    EXPECT_TRUE(c.reset0_low);
    EXPECT_EQ(c.reset1_pin, "RESETN");
    EXPECT_TRUE(c.reset1_low);
    EXPECT_EQ(c.both_value, 0);
  }
  EXPECT_EQ(livehd::liberty::scan_async_dff_cells(path).size(), 1U);
  const auto cells = livehd::liberty::selection_cells(sel);
  ASSERT_EQ(cells.size(), 2U);  // DFFHQNx1 + the async cell, once for both values
  EXPECT_EQ(cells[1].name, "DFFASRHQNx1");
  EXPECT_NE(livehd::liberty::dff_selection_descriptor(sel).find("areset0=DFFASRHQNx1"), std::string::npos);
}

TEST(LibertyDff, Sky130AsyncPicksClearAndPresetCellsByArea) {
  // dfrtp_1/_2 (clear) and dfstp_1 (preset) are the single-purpose picks; the
  // dual dfbbp_1 is dearer. Never qualifying although cheaper: a clear cell
  // marked dont_use, a negedge clear cell and a gated clear (`!RESET_B & EN`).
  // dfrbn_1 exposes only Q_N (the complement var), so its CLEAR forces Q_N to
  // 1: it is the cheapest PRESET cell, inverted.
  const std::string ff_clear  = "      clear : \"!RESET_B\";\n";
  const std::string ff_preset = "      preset : \"!SET_B\";\n";
  const std::string head      = "\"IQ\",\"IQ_N\"";
  const auto        path
      = write_lib("sky130_async.lib",
                  lib(q_cell("dfxtp_1", 20.02) + async_cell("dfrtp_1", 25.02, head, ff_clear, {"RESET_B"}, "Q", "IQ")
                      + async_cell("dfrtp_2", 26.0, head, ff_clear, {"RESET_B"}, "Q", "IQ")
                      + async_cell("dfstp_1", 26.28, head, ff_preset, {"SET_B"}, "Q", "IQ")
                      + async_cell("dfbbp_1",
                                   32.5,
                                   head,
                                   ff_clear + ff_preset + "      clear_preset_var1 : \"H\";\n      clear_preset_var2 : \"L\";\n",
                                   {"RESET_B", "SET_B"},
                                   "Q",
                                   "IQ")
                      + async_cell("dfrtp_cheap", 10.0, head, ff_clear, {"RESET_B"}, "Q", "IQ", "D", /*dont_use=*/true)
                      + async_cell("dfrtn_1", 9.0, head, ff_clear, {"RESET_B"}, "Q", "IQ", "D", false, "!CLK")
                      + async_cell("dfgated_1", 8.0, head, "      clear : \"!RESET_B & EN\";\n", {"RESET_B", "EN"}, "Q", "IQ")
                      + async_cell("dfrbn_1", 25.5, head, ff_clear, {"RESET_B"}, "Q_N", "IQ_N")));
  auto sel = livehd::liberty::resolve_dff_cells(path);
  ASSERT_TRUE(sel.base.has_value());
  EXPECT_EQ(sel.base->name, "dfxtp_1");
  ASSERT_EQ(sel.areset_ladder[0].size(), 2U);  // dfrtp_1, dfrtp_2: same pins/polarity
  EXPECT_EQ(sel.areset_ladder[0][0].name, "dfrtp_1");
  EXPECT_EQ(sel.areset_ladder[0][1].name, "dfrtp_2");
  EXPECT_EQ(sel.areset_ladder[0][0].reset0_pin, "RESET_B");
  EXPECT_TRUE(sel.areset_ladder[0][0].reset0_low);
  EXPECT_TRUE(sel.areset_ladder[0][0].reset1_pin.empty());
  EXPECT_FALSE(sel.areset_ladder[0][0].q_inverted);
  ASSERT_EQ(sel.areset_ladder[1].size(), 1U);
  const auto& pre = sel.areset_ladder[1].front();
  EXPECT_EQ(pre.name, "dfrbn_1");  // 25.5 < dfstp_1's 26.28
  EXPECT_EQ(pre.q_pin, "Q_N");
  EXPECT_TRUE(pre.q_inverted);
  EXPECT_EQ(pre.reset1_pin, "RESET_B");
  EXPECT_TRUE(pre.reset1_low);
  EXPECT_TRUE(pre.reset0_pin.empty());
  for (const auto& c : livehd::liberty::scan_async_dff_cells(path)) {
    EXPECT_NE(c.name, "dfrtp_cheap") << "dont_use";
    EXPECT_NE(c.name, "dfrtn_1") << "negedge";
    EXPECT_NE(c.name, "dfgated_1") << "a gated clear is not a reset pin";
  }
}

TEST(LibertyDff, AsyncDualCellServesBothValues) {
  // Only the dual cell: it serves both values; `RN'` is an active-low clear,
  // `S` an active-high preset, and both_value comes from clear_preset_var1
  // (Q shows IQ: H = 1).
  const auto path = write_lib("dual.lib",
                              lib(q_cell("DFFx1", 6)
                                  + async_cell("DFFRSx1",
                                               9,
                                               "IQ,IQN",
                                               "      clear : \"RN'\";\n      preset : \"S\";\n      clear_preset_var1 : H;\n",
                                               {"RN", "S"},
                                               "Q",
                                               "IQ")));
  auto       sel  = livehd::liberty::resolve_dff_cells(path);
  for (int v = 0; v < 2; ++v) {
    ASSERT_EQ(sel.areset_ladder[v].size(), 1U) << v;
    const auto& c = sel.areset_ladder[v].front();
    EXPECT_EQ(c.name, "DFFRSx1");
    EXPECT_EQ(c.reset0_pin, "RN");
    EXPECT_TRUE(c.reset0_low);
    EXPECT_EQ(c.reset1_pin, "S");
    EXPECT_FALSE(c.reset1_low);
    EXPECT_EQ(c.both_value, 1);
  }
}

TEST(LibertyDff, PlainOnlyLibraryHasNoAsyncPick) {
  // test.lib / test_qn.lib carry no clear/preset cell: an async-reset register
  // stays a native flop there (lhd_abc_seq_test's abc_async_reset contract),
  // and the cache descriptor is exactly the plain one.
  const auto t   = write_lib("plain_only.lib", lib(q_cell("DFFx1", 6) + qn_cell("DFFNx1", 5)));
  auto       sel = livehd::liberty::resolve_dff_cells(t);
  EXPECT_TRUE(sel.areset_ladder[0].empty());
  EXPECT_TRUE(sel.areset_ladder[1].empty());
  ASSERT_TRUE(sel.base.has_value());
  EXPECT_EQ(livehd::liberty::dff_selection_descriptor(sel), livehd::liberty::dff_descriptor(*sel.base));
}

// --- integrated clock-gate cells (Dff_selection::icg_ladder) -----------------

namespace {

// A clock-gate cell in either PDK's spelling: pins named by the Liberty's
// clock_gate_*_pin attributes, the gated clock as a state_function.
std::string icg_cell(const std::string& name, double area, const std::string& kind, const std::string& en,
                     const std::string& test = "", const std::string& fn = "CLK & IQ", bool dont_use = false,
                     const std::string& extra_pin = "") {
  std::string s = "  cell (" + name + ") {\n    area : " + std::to_string(area) + ";\n";
  if (dont_use) {
    s += "    dont_use : true;\n";
  }
  s += "    clock_gating_integrated_cell : " + kind + ";\n";
  s += "    statetable (\"CLK " + en + "\", \"IQ\") { table : \"L L : - : L, L H : - : H, H - : - : N\"; }\n";
  s += "    pin (IQ) { direction : internal; internal_node : \"IQ\"; }\n";
  s += "    pin (GCLK) { clock_gate_out_pin : true; direction : output; state_function : \"" + fn + "\"; }\n";
  s += "    pin (CLK) { clock : true; clock_gate_clock_pin : true; direction : input; }\n";
  s += "    pin (" + en + ") { clock_gate_enable_pin : true; direction : input; }\n";
  if (!test.empty()) {
    s += "    pin (" + test + ") { clock_gate_test_pin : true; direction : input; }\n";
  }
  if (!extra_pin.empty()) {
    s += "    pin (" + extra_pin + ") { direction : input; }\n";
  }
  return s + "  }\n";
}

}  // namespace

TEST(LibertyDff, Asap7IcgLadderByArea) {
  // ASAP7: ICGx1..x5 plus equal-area `*DC` siblings; the ladder stops at the
  // first area tie. The test pin SE is recorded (the mapper ties it 0).
  std::string  cells;
  const double areas[] = {0.26244, 0.27702, 0.2916, 0.30618, 0.32076};
  for (int i = 0; i < 5; ++i) {
    cells += icg_cell("ICGx" + std::to_string(i + 1) + "_ASAP7_75t_R", areas[i], "latch_posedge_precontrol", "ENA", "SE");
  }
  cells           += icg_cell("ICGx4DC_ASAP7_75t_R", 0.69984, "latch_posedge_precontrol", "ENA", "SE");
  cells           += icg_cell("ICGx8DC_ASAP7_75t_R", 0.69984, "latch_posedge_precontrol", "ENA", "SE");
  const auto path  = write_lib("asap7_icg.lib", lib(qn_cell("DFFHQNx1", 0.2916) + cells));
  auto       sel   = livehd::liberty::resolve_dff_cells(path);
  ASSERT_EQ(sel.icg_ladder.size(), 5U);
  const auto& c = sel.icg_ladder.front();
  EXPECT_EQ(c.name, "ICGx1_ASAP7_75t_R");
  EXPECT_EQ(c.clk_pin, "CLK");
  EXPECT_EQ(c.en_pin, "ENA");
  EXPECT_EQ(c.test_pin, "SE");
  EXPECT_EQ(c.out_pin, "GCLK");
  EXPECT_EQ(sel.icg_ladder[4].name, "ICGx5_ASAP7_75t_R");
  EXPECT_EQ(livehd::liberty::scan_icg_cells(path).size(), 7U);
  EXPECT_NE(livehd::liberty::dff_selection_descriptor(sel).find("|icg=ICGx1_ASAP7_75t_R:CLK:ENA:SE:GCLK"), std::string::npos);
}

TEST(LibertyDff, Sky130IcgPickSkipsDontUseNegedgeAndOddShapes) {
  // dlclkp_1 (no test pin) is the pick over the dearer sdlclkp_1; a cheaper
  // dont_use cell, a latch_negedge cell, an OR-gated output, an `_obs` cell and
  // a cell with an unknown extra input never qualify.
  const auto path = write_lib(
      "sky130_icg.lib",
      lib(q_cell("dfxtp_1", 20.02) + icg_cell("sky130_fd_sc_hd__dlclkp_1", 17.5168, "\"latch_posedge\"", "GATE", "", "(CLK*M0)")
          + icg_cell("sky130_fd_sc_hd__dlclkp_2", 18.768, "\"latch_posedge\"", "GATE", "", "(CLK*M0)")
          + icg_cell("sky130_fd_sc_hd__sdlclkp_1", 18.768, "\"latch_posedge_precontrol\"", "GATE", "SCE", "(CLK*M0)")
          + icg_cell("cheap_dont_use", 5, "latch_posedge", "GATE", "", "CLK & IQ", /*dont_use=*/true)
          + icg_cell("negedge", 4, "latch_negedge", "GATE", "", "CLK | IQ")
          + icg_cell("or_gated", 3, "latch_posedge", "GATE", "", "CLK + IQ")
          + icg_cell("observed", 3, "latch_posedge_precontrol_obs", "GATE", "SCE")
          + icg_cell("extra_input", 2, "latch_posedge", "GATE", "", "CLK & IQ", false, "RN")));
  auto sel = livehd::liberty::resolve_dff_cells(path);
  ASSERT_EQ(sel.icg_ladder.size(), 2U);
  EXPECT_EQ(sel.icg_ladder[0].name, "sky130_fd_sc_hd__dlclkp_1");
  EXPECT_TRUE(sel.icg_ladder[0].test_pin.empty());
  EXPECT_EQ(sel.icg_ladder[0].en_pin, "GATE");
  EXPECT_EQ(sel.icg_ladder[1].name, "sky130_fd_sc_hd__dlclkp_2");
  for (const auto& c : livehd::liberty::scan_icg_cells(path)) {
    EXPECT_TRUE(c.name.starts_with("sky130_fd_sc_hd__")) << c.name;
  }
}

TEST(LibertyDff, NoIcgCellLeavesTheLadderEmpty) {
  const auto path = write_lib("no_icg.lib", lib(q_cell("DFFx1", 6)));
  auto       sel  = livehd::liberty::resolve_dff_cells(path);
  EXPECT_TRUE(sel.icg_ladder.empty());
  ASSERT_TRUE(sel.base.has_value());
  EXPECT_EQ(livehd::liberty::dff_selection_descriptor(sel), livehd::liberty::dff_descriptor(*sel.base));
}

namespace {

// A transparent data latch in either PDK's spelling: `latch(state, nstate)`
// with data_in / enable (and optional clear / preset), outputs by function
// (`outs` is a comma list of PIN:FUNCTION).
std::string latch_cell(const std::string& name, double area, const std::string& en, const std::string& extra_group = "",
                       const std::vector<std::string>& extra_in = {}, const std::string& outs = "Q:IQ",
                       const std::string& head = "", const std::string& state = "IQ, IQN", const std::string& data_in = "D") {
  const std::string enpin = en.front() == '!' ? en.substr(1) : en;
  std::string       s     = "  cell (" + name + ") {\n    area : " + std::to_string(area) + ";\n" + head;
  s += "    latch (" + state + ") { data_in : \"" + data_in + "\"; enable : \"" + en + "\";" + extra_group + " }\n";
  s += "    pin (" + enpin + ") { direction : input; clock : true; }\n    pin (D) { direction : input; }\n";
  for (const auto& p : extra_in) {
    s += "    pin (" + p + ") { direction : input; }\n";
  }
  size_t b = 0;
  while (b < outs.size()) {
    size_t e = outs.find(',', b);
    if (e == std::string::npos) {
      e = outs.size();
    }
    const std::string o     = outs.substr(b, e - b);
    const auto        colon = o.find(':');
    s += "    pin (" + o.substr(0, colon) + ") { direction : output; function : \"" + o.substr(colon + 1) + "\"; }\n";
    b  = e + 1;
  }
  return s + "  }\n";
}

}  // namespace

TEST(LibertyDff, Asap7LatchLaddersPerEnablePolarity) {
  // ASAP7: DHLx1..x3 (enable CLK) and DLLx1..x3 (enable !CLK), pins D/CLK/Q,
  // no reset latch. They are never flops, and the flop pick is unchanged.
  std::string  cells;
  const double hl[] = {0.2187, 0.23328, 0.24786};
  const double ll[] = {0.2187, 0.23328, 0.26244};
  for (int i = 0; i < 3; ++i) {
    cells += latch_cell("DHLx" + std::to_string(i + 1) + "_ASAP7_75t_R", hl[i], "CLK");
    cells += latch_cell("DLLx" + std::to_string(i + 1) + "_ASAP7_75t_R", ll[i], "!CLK");
  }
  const auto path = write_lib("asap7_latch.lib", lib(qn_cell("DFFHQNx1", 0.2916) + cells));
  auto       sel  = livehd::liberty::resolve_dff_cells(path);
  ASSERT_TRUE(sel.base.has_value());
  EXPECT_EQ(sel.base->name, "DFFHQNx1");
  EXPECT_TRUE(sel.has_latch_cells());
  const auto& hi = sel.latch_ladder[0][0];
  const auto& lo = sel.latch_ladder[1][0];
  ASSERT_EQ(hi.size(), 3U);
  ASSERT_EQ(lo.size(), 3U);
  EXPECT_EQ(hi[0].name, "DHLx1_ASAP7_75t_R");
  EXPECT_EQ(hi[2].name, "DHLx3_ASAP7_75t_R");
  EXPECT_EQ(lo[0].name, "DLLx1_ASAP7_75t_R");
  EXPECT_TRUE(hi[0].latch);
  EXPECT_FALSE(hi[0].en_low);
  EXPECT_TRUE(lo[0].en_low);
  EXPECT_EQ(hi[0].d_pin, "D");
  EXPECT_EQ(hi[0].clk_pin, "CLK");
  EXPECT_EQ(hi[0].q_pin, "Q");
  EXPECT_FALSE(hi[0].q_inverted);
  EXPECT_FALSE(hi[0].is_async());
  for (int low = 0; low < 2; ++low) {
    EXPECT_TRUE(sel.latch_ladder[low][1].empty());
    EXPECT_TRUE(sel.latch_ladder[low][2].empty());
  }
  EXPECT_EQ(livehd::liberty::scan_latch_cells(path).size(), 6U);
  EXPECT_EQ(livehd::liberty::scan_dff_cells(path).size(), 1U);
  EXPECT_EQ(livehd::liberty::selection_cells(sel).size(), 7U);
  EXPECT_NE(livehd::liberty::dff_selection_descriptor(sel).find("|latch10=DLLx1_ASAP7_75t_R:D:!CLK:Q:0:latch"), std::string::npos);
}

TEST(LibertyDff, Sky130LatchPicksSkipDontUseIsolationScanAndClockGates) {
  // dlxtp_1 (GATE) / dlxtn_1 (!GATE_N) are the plain picks, the dearer
  // two-output dlxbp_1 only a drive rung; dlrtp_1 / dlrtn_1 (clear !RESET_B) serve reset-to-0.
  // A cheaper dont_use latch, an isolation latch, a scan latch (an extra
  // input), an ICG built on a latch group and a latch whose only output is
  // gated never qualify.
  const std::string icg_latch
      = "  cell (dlclkp_1) {\n    area : 1;\n    clock_gating_integrated_cell : \"latch_posedge\";\n    latch (IQ, IQN) { "
        "data_in : \"GATE\"; enable : \"!CLK\"; }\n    pin (CLK) { direction : input; clock_gate_clock_pin : true; }\n    pin "
        "(GATE) { direction : input; clock_gate_enable_pin : true; }\n    pin (GCLK) { direction : output; clock_gate_out_pin : "
        "true; function : \"IQ & CLK\"; }\n  }\n";
  const auto path = write_lib(
      "sky130_latch.lib",
      lib(q_cell("dfxtp_1", 20.02) + latch_cell("sky130_fd_sc_hd__dlxbp_1", 18.77, "GATE", "", {}, "Q:IQ,Q_N:IQN")
          + latch_cell("sky130_fd_sc_hd__dlxtp_1", 15.01, "GATE") + latch_cell("sky130_fd_sc_hd__dlxtn_1", 15.01, "!GATE_N")
          + latch_cell("sky130_fd_sc_hd__dlrtp_1", 16.27, "GATE", " clear : \"!RESET_B\";", {"RESET_B"})
          + latch_cell("sky130_fd_sc_hd__dlrtn_1", 16.27, "!GATE_N", " clear : \"!RESET_B\";", {"RESET_B"})
          + latch_cell("cheap_dont_use", 1, "GATE", "", {}, "Q:IQ", "    dont_use : true;\n")
          + latch_cell("iso_latch", 1, "GATE", "", {}, "Q:IQ", "    is_isolation_cell : true;\n")
          + latch_cell("scan_latch", 1, "GATE", "", {"SCE"}) + latch_cell("gated_out", 1, "GATE", "", {}, "Q:IQ & GATE")
          + icg_latch));
  auto sel = livehd::liberty::resolve_dff_cells(path);
  // The two-output dlxbp_1 has the same pins/polarity: the dearer ladder rung.
  ASSERT_EQ(sel.latch_ladder[0][0].size(), 2U);
  EXPECT_EQ(sel.latch_ladder[0][0][0].name, "sky130_fd_sc_hd__dlxtp_1");
  EXPECT_EQ(sel.latch_ladder[0][0][1].name, "sky130_fd_sc_hd__dlxbp_1");
  ASSERT_EQ(sel.latch_ladder[1][0].size(), 1U);
  EXPECT_EQ(sel.latch_ladder[1][0][0].name, "sky130_fd_sc_hd__dlxtn_1");
  EXPECT_EQ(sel.latch_ladder[1][0][0].clk_pin, "GATE_N");
  ASSERT_EQ(sel.latch_ladder[0][1].size(), 1U);
  const auto& r = sel.latch_ladder[0][1][0];
  EXPECT_EQ(r.name, "sky130_fd_sc_hd__dlrtp_1");
  EXPECT_EQ(r.reset0_pin, "RESET_B");
  EXPECT_TRUE(r.reset0_low);
  EXPECT_TRUE(r.reset1_pin.empty());
  ASSERT_EQ(sel.latch_ladder[1][1].size(), 1U);
  EXPECT_EQ(sel.latch_ladder[1][1][0].name, "sky130_fd_sc_hd__dlrtn_1");
  EXPECT_TRUE(sel.latch_ladder[0][2].empty());  // no preset latch
  EXPECT_TRUE(sel.latch_ladder[1][2].empty());
  const auto all = livehd::liberty::scan_latch_cells(path);
  EXPECT_EQ(all.size(), 5U);  // dlxbp, dlxtp, dlxtn, dlrtp, dlrtn
  for (const auto& c : all) {
    EXPECT_TRUE(c.name.starts_with("sky130_fd_sc_hd__")) << c.name << " must not qualify";
  }
}

TEST(LibertyDff, LatchQnOnlyCellIsInvertedWithResetsInPinTerms) {
  // `latch(IQN, IQNN) { data_in : "!D" }` with only QN = IQN: the pin shows !D
  // while transparent (q_inverted). A clear drives the stored var -- the pin
  // -- to 0 (reset0); a preset on the same shape would be reset1.
  const auto path = write_lib("qn_latch.lib",
                              lib(latch_cell("LQN", 3, "!CLK", " clear : \"!RN\";", {"RN"}, "QN:IQN", "", "IQN, IQNN", "!D")
                                  + latch_cell("LQNP", 3, "CLK", " preset : \"S\";", {"S"}, "QN:IQN", "", "IQN, IQNN", "!D")));
  auto       sel  = livehd::liberty::resolve_dff_cells(path);
  ASSERT_EQ(sel.latch_ladder[1][1].size(), 1U);
  const auto& c = sel.latch_ladder[1][1][0];
  EXPECT_EQ(c.name, "LQN");
  EXPECT_EQ(c.q_pin, "QN");
  EXPECT_TRUE(c.q_inverted);
  EXPECT_TRUE(c.en_low);
  EXPECT_EQ(c.reset0_pin, "RN");
  EXPECT_TRUE(c.reset0_low);
  ASSERT_EQ(sel.latch_ladder[0][2].size(), 1U);
  EXPECT_EQ(sel.latch_ladder[0][2][0].reset1_pin, "S");
  EXPECT_FALSE(sel.latch_ladder[0][2][0].reset1_low);
  EXPECT_TRUE(sel.latch_ladder[0][0].empty());  // a reset cell is not a plain pick
}

TEST(LibertyDff, MemoryAreaUsesEitherClockEdgeWithoutChangingRegisterSelection) {
  auto disabled = q_cell("DONT_USE", 0.01);
  disabled.insert(disabled.find('{') + 1, "dont_use:true;");
  const auto path = write_lib("memory_area.lib",
                              lib(q_cell("POS", 2) + q_cell("NEG", 1, "D", "!CLK") + disabled
                                  + "cell(LATCH) { area:0.001; latch(IQ,IQN) { data_in:D; enable:G; } }"));
  const auto area = livehd::liberty::smallest_flop_area(path);
  ASSERT_TRUE(area);
  EXPECT_DOUBLE_EQ(*area, 1);
  const auto registers = livehd::liberty::scan_dff_cells(path);
  ASSERT_EQ(registers.size(), 1);
  EXPECT_EQ(registers.front().name, "POS");
}
TEST(LibertyDff, DualOutputTieInventoryPreservesBothConstants) {
  const auto path = write_lib(
      "tie_cells.lib",
      lib("cell(TIE) { area:1; pin(HI) {direction:output; function:\"1\";} pin(LO) {direction:output; function:\"0\";} }"
          "cell(NOT_A_TIE) {area:1; pin(A) {direction:input;} pin(Y) {direction:output; function:\"1\";} }"));
  const auto cells = livehd::liberty::scan_constant_cells(path);
  ASSERT_EQ(cells.size(), 1);
  EXPECT_EQ(cells.front().name, "TIE");
  EXPECT_EQ(cells.front().outputs,
            (std::vector<std::pair<std::string, bool>>{
                {"HI",  true},
                {"LO", false}
  }));
}
TEST(LibertyDff, ConstantInventoryExcludesDisabledStateAndThreeStateCells) {
  const auto path  = write_lib("constant_inventory.lib", lib(R"(
    cell(arbitrary_high) { pg_pin(VP) {pg_type:primary_power;}
      pin(unusual) {direction:output; function:"1";} }
    cell(arbitrary_low) { pin(different) {direction:output; function:"0";} }
    cell(disabled) {dont_use:true; pin(Y) {direction:output; function:"1";} }
    cell(state) {ff(IQ,IQN) {next_state:"0";} pin(Y) {direction:output; function:"0";} }
    cell(high_impedance) {pin(Y) {direction:output; function:"0"; three_state:"1";} }
    cell(bussed) {bus(Y) {pin(Y[0]) {direction:output; function:"0";} } }
    cell(extra_input) {pin(A) {direction:input;} pin(Y) {direction:output; function:"0";} }
  )"));
  const auto cells = livehd::liberty::scan_constant_cells(path);
  ASSERT_EQ(cells.size(), 2);
  EXPECT_EQ(cells[0].name, "arbitrary_high");
  EXPECT_EQ(cells[0].outputs,
            (std::vector<std::pair<std::string, bool>>{
                {"unusual", true}
  }));
  EXPECT_EQ(cells[1].name, "arbitrary_low");
  EXPECT_EQ(cells[1].outputs,
            (std::vector<std::pair<std::string, bool>>{
                {"different", false}
  }));
}

TEST(LibertyFunction, ExactTruthAndUnsupportedSyntax) {
  livehd::liberty::Comb_cell c;
  c.inputs   = {"A", "B", "C"};
  c.function = "A ^ B * C";
  EXPECT_EQ(livehd::liberty::cell_truth(c), uint64_t{0x6060606060606060});
  c.function = "A B' + !C";
  EXPECT_EQ(livehd::liberty::cell_truth(c), uint64_t{0x2f2f2f2f2f2f2f2f});
  c.function = "A & absent";
  EXPECT_FALSE(livehd::liberty::cell_truth(c));
  c.function = "(A & B";
  EXPECT_FALSE(livehd::liberty::cell_truth(c));
  c.function = "A ? B : C";
  EXPECT_FALSE(livehd::liberty::cell_truth(c));
  c.inputs   = {"A", "B", "C", "D", "E", "F", "G"};
  c.function = "A";
  EXPECT_FALSE(livehd::liberty::cell_truth(c));
}

TEST(LibertyFunction, RepairElectricalUnitsAndNldmEstimate) {
  const auto cells = livehd::liberty::scan_comb_cells_text(R"(
    library (picofarads) { time_unit : "1ns"; capacitive_load_unit (1, "pf"); default_max_transition : 2;
      cell (BUF) { area : 1; pin(A) { direction : input; capacitance : 0.002; }
        pin (Y) { direction : output; function : "A"; max_capacitance : 0.1; max_fanout : 8;
          timing () { cell_rise(t) { index_2("0.01, 0.1"); values("0.03, 0.12"); } } } } }
    library (femtofarads) { time_unit : "1ps"; capacitive_load_unit (1, "ff");
      cell (BUF2) { area : 1; pin(A) { direction : input; capacitance : 3; }
        pin(Y) { direction : output; function : "A"; max_capacitance : 15; } } }
  )");
  ASSERT_EQ(cells.size(), 2);
  EXPECT_DOUBLE_EQ(cells[0].input_capacitance_ff.front(), 2);
  EXPECT_DOUBLE_EQ(cells[0].max_capacitance_ff.value(), 100);
  EXPECT_DOUBLE_EQ(cells[0].max_transition_ps.value(), 2000);
  EXPECT_DOUBLE_EQ(cells[0].max_fanout.value(), 8);
  EXPECT_DOUBLE_EQ(cells[0].intrinsic_ps, 30);
  EXPECT_NEAR(cells[0].resistance_ohm, 1000, 1e-8);
  EXPECT_DOUBLE_EQ(cells[1].input_capacitance_ff.front(), 3);
  EXPECT_DOUBLE_EQ(cells[1].max_capacitance_ff.value(), 15);
  EXPECT_FALSE(cells[1].max_transition_ps);
}

TEST(LibertyFunction, ExistingSequentialOutputDriveUsesItsOwnTablesAndUnits) {
  const std::string text   = R"(
    library (ns) { time_unit : "1ns"; capacitive_load_unit (1, "pf"); default_max_transition : 0.5;
      cell (FF) { area : 1; dont_use : true;
        ff(IQ, IQN) { next_state : "D"; clocked_on : "CLK"; }
        pin(CLK) { direction : input; clock : true; }
        pin(D) { direction : input; }
        pin(Q) { direction : output; function : "IQ"; max_capacitance : 0.2; max_fanout : 20;
          timing() { related_pin : "CLK"; timing_type : rising_edge;
            cell_rise(t) { index_2("0.01,0.11"); values("0.02,0.12"); } } }
        pin(QN) { direction : output; function : "IQN"; max_capacitance : 0.1;
          timing() { related_pin : "CLK"; timing_type : rising_edge;
            cell_fall(t) { index_2("0.01,0.11"); values("0.03,0.23"); } } }
        pin(Z) { direction : output; three_state : "D"; }
      }
    }
    library (ps) { time_unit : "1ps"; capacitive_load_unit (1, "ff");
      cell (OTHER) { pin(Y) { direction : output; max_capacitance : 8;
        timing() { cell_rise(t) { index_2("1,3"); values("2,6"); } } } }
    }
  )";
  const auto        drives = livehd::liberty::scan_output_drives_text(text);
  ASSERT_EQ(drives.size(), 3);
  EXPECT_EQ(drives[0].name, "FF");
  EXPECT_TRUE(drives[0].sequential);
  EXPECT_EQ(drives[0].output, "Q");
  EXPECT_DOUBLE_EQ(drives[0].max_capacitance_ff.value(), 200);
  EXPECT_DOUBLE_EQ(drives[0].max_transition_ps.value(), 500);
  EXPECT_DOUBLE_EQ(drives[0].max_fanout.value(), 20);
  EXPECT_DOUBLE_EQ(drives[0].intrinsic_ps, 20);
  EXPECT_NEAR(drives[0].resistance_ohm, 1000, 1e-8);
  EXPECT_EQ(drives[1].output, "QN");
  EXPECT_NEAR(drives[1].resistance_ohm, 2000, 1e-8);
  EXPECT_FALSE(drives[1].max_fanout);
  EXPECT_EQ(drives[2].name, "OTHER");
  EXPECT_FALSE(drives[2].sequential);
  EXPECT_DOUBLE_EQ(drives[2].max_capacitance_ff.value(), 8);
  EXPECT_DOUBLE_EQ(drives[2].intrinsic_ps, 2);
  EXPECT_NEAR(drives[2].resistance_ohm, 2000, 1e-8);
  EXPECT_TRUE(livehd::liberty::scan_comb_cells_text(text).empty());
}

TEST(LibertyFunction, RepairNldmLoadAxisFollowsTemplate) {
  // osu018 shape: variable_1 is the load, so index_2 holds slews and the load
  // sweep is column 0. A template with no load axis gives no estimate.
  const auto cells = livehd::liberty::scan_comb_cells_text(R"(
    library (osu) { time_unit : "1ns"; capacitive_load_unit (1, "pf");
      lu_table_template(load_first) { variable_1 : total_output_net_capacitance; variable_2 : input_net_transition;
        index_1 ("1, 2"); index_2 ("1, 2, 3"); }
      lu_table_template(slew_only) { variable_1 : input_net_transition; index_1 ("1, 2"); }
      cell (BUFX2) { area : 1; pin(A) { direction : input; capacitance : 0.002; }
        pin (Y) { direction : output; function : "A";
          timing () { cell_rise(load_first) { index_1 ("0.005, 0.105"); index_2 ("0.06, 0.6, 1.2");
            values ("0.065, 0.07, 0.09", "0.235, 0.24, 0.26"); } } } }
      cell (BUFX4) { area : 1; pin(A) { direction : input; capacitance : 0.002; }
        pin (Y) { direction : output; function : "A";
          timing () { cell_rise(slew_only) { index_1 ("0.06, 1.2"); values ("0.04, 0.09"); } } } } }
  )");
  ASSERT_EQ(cells.size(), 2);
  EXPECT_DOUBLE_EQ(cells[0].intrinsic_ps, 65);
  EXPECT_NEAR(cells[0].resistance_ohm, 1700, 1e-6);  // (0.235-0.065) ns / 0.1 pF
  EXPECT_DOUBLE_EQ(cells[1].intrinsic_ps, 40);
  EXPECT_DOUBLE_EQ(cells[1].resistance_ohm, 0);
}

TEST(LibertyMemory, OpaqueTypedInterfaceAndScopedBusTypes) {
  const auto file  = write_lib("memory_interface.lib", R"(
 library(one) {
   type(DATA) { bit_width: 5; bit_from: 0; bit_to: 4; }
   cell(ram5) {
     dont_use: true; area: 123;
     memory() { type: ram; address_width: 3; word_width: 5; }
     pin(CK) { direction: input; clock: true; }
     pin(A) { direction: input; }
     bus(D) { direction: input; bus_type: DATA; }
     bus(Q) { direction: output; bus_type: DATA;
       timing() { related_pin: "CK"; timing_type: rising_edge; }
       pin(Q[0]) { direction: output; }
     }
   }
 }
 library(two) {
   type(DATA) { bit_width: 7; bit_from: 6; bit_to: 0; }
   cell(ram7) {
     memory() { type: ram; address_width: 2; word_width: 7; }
     bus(D) { direction: input; bus_type: DATA; }
     bus(Q) { direction: output; bus_type: DATA; }
   }
 })");
  const auto cells = livehd::liberty::scan_memory_cells(file);
  ASSERT_EQ(cells.size(), 2U);
  EXPECT_EQ(cells[0].name, "ram5");
  EXPECT_EQ(cells[0].area, 123);
  EXPECT_EQ(cells[0].address_bits, 3);
  ASSERT_EQ(cells[0].pins.size(), 4U);  // nested per-bit pins do not duplicate the bus
  EXPECT_TRUE(cells[0].pins[3].clocked);
  EXPECT_FALSE(cells[1].pins[1].clocked);
  EXPECT_EQ(cells[0].pins[2].msb, 0);
  EXPECT_EQ(cells[0].pins[2].lsb, 4);
  EXPECT_EQ(cells[1].pins[0].msb, 6);
  EXPECT_EQ(cells[1].pins[0].lsb, 0);
  const auto decl = livehd::liberty::memory_cell_declarations(cells);
  EXPECT_NE(decl.find("input [0:4] \\D "), std::string::npos);
  EXPECT_NE(decl.find("output [6:0] \\Q "), std::string::npos);
  EXPECT_EQ(decl.find("always"), std::string::npos);  // no invented functional/collision model
  hhds::GraphLibrary lib;
  livehd::liberty::declare_memory_cells(lib, cells);
  EXPECT_NO_THROW(livehd::liberty::declare_memory_cells(lib, cells));
  // Ask without materializing: get_graph() asserts on a body-less gid.
  EXPECT_FALSE(lib.find_io("ram5")->has_graph());
  EXPECT_FALSE(lib.find_io("ram7")->has_graph());
  EXPECT_TRUE(lib.find_io("ram5")->is_loop_break("Q"));
  EXPECT_FALSE(lib.find_io("ram7")->is_loop_break("Q"));
  // A reused lg: dir keeps the body-less declaration of an older .lib: the
  // current Liberty replaces it (same gid) instead of failing every rerun.
  const auto gid               = lib.find_io("ram5")->get_gid();
  auto       changed           = cells;
  changed[0].pins[0].direction = "output";
  EXPECT_NO_THROW(livehd::liberty::declare_memory_cells(lib, changed));
  ASSERT_TRUE(lib.find_io("ram5"));
  EXPECT_TRUE(lib.find_io("ram5")->has_output("CK"));
  EXPECT_FALSE(lib.find_io("ram5")->has_input("CK"));
  EXPECT_EQ(lib.find_io("ram5")->get_gid(), gid);
  EXPECT_FALSE(lib.find_io("ram5")->has_graph());
  auto renamed = cells;  // a pin the saved declaration never had (output, so is_loop_break is probed)
  renamed[0].pins.push_back(livehd::liberty::Memory_pin{.name = "BUSY", .direction = "output"});
  EXPECT_NO_THROW(livehd::liberty::declare_memory_cells(lib, renamed));
  EXPECT_TRUE(lib.find_io("ram5")->has_output("BUSY"));
  // A same-named module with a real body is never silently turned into the macro.
  hhds::GraphLibrary design;
  ASSERT_TRUE(design.create_io("ram7")->create_graph());
  EXPECT_THROW(livehd::liberty::declare_memory_cells(design, cells), std::runtime_error);
}

TEST(LibertyMemory, AddressDataAndClockRolesComeFromMetadata) {
  const auto file  = write_lib("memory_roles.lib", R"(
 library(one) {
   cell(arbitrary_ram_name) {
     memory() { type: ram; address_width: 1; word_width: 1; }
     pin(P) { direction: input; clock: true; }
     pin(X) { direction: input; }
     pin(Y) { direction: input; memory_write() { address: X; } }
     pin(E) { direction: input; }
     pin(Z) { direction: output; memory_read() { address: X; } }
   }
 })");
  const auto cells = livehd::liberty::scan_memory_cells(file);
  ASSERT_EQ(cells.size(), 1U);
  ASSERT_EQ(cells[0].pins.size(), 5U);
  EXPECT_TRUE(cells[0].pins[0].clock_input);
  EXPECT_FALSE(cells[0].pins[0].address_data_input);
  EXPECT_TRUE(cells[0].pins[1].address_data_input);
  EXPECT_TRUE(cells[0].pins[2].address_data_input);
  EXPECT_FALSE(cells[0].pins[3].address_data_input);
  EXPECT_FALSE(cells[0].pins[4].address_data_input);
}

TEST(LibertyMemory, UnsupportedMemoryCellsAreSkippedNotFatal) {
  // A ROM, an inout-pin RAM, an untyped bus and a malformed standard-cell type
  // group must not fail a design that never instantiates them; a bus typed at
  // cell scope (legal Liberty) resolves, and an internal pin is not a port.
  const auto                                file = write_lib("memory_tolerant.lib", R"(
 library(mixed) {
   type(BAD) { bit_width: 4; }
   type(DATA) { bit_width: 8; bit_from: 7; bit_to: 0; }
   cell(INVX1) { area: 1; pin(A) { direction: input; } pin(Y) { direction: output; function: "!A"; } }
   cell(rom8) { memory() { type: rom; address_width: 2; word_width: 8; }
     bus(A) { direction: input; bus_type: DATA; } bus(Q) { direction: output; bus_type: DATA; } }
   cell(ram_inout) { memory() { type: ram; address_width: 2; word_width: 1; }
     pin(A) { direction: input; } pin(DQ) { direction: inout; } }
   cell(ram_untyped) { memory() { type: ram; address_width: 2; word_width: 3; }
     bus(D) { direction: input; bus_type: NOPE; } }
   cell(ram_local) {
     type(WORD) { bit_width: 3; bit_from: 2; bit_to: 0; }
     type(DATA) { bit_width: 3; bit_from: 0; bit_to: 2; }
     memory() { type: ram; address_width: 2; word_width: 3; }
     pin(CK) { direction: input; } pin(NODE) { direction: internal; }
     bus(D) { direction: input; bus_type: WORD; } bus(Q) { direction: output; bus_type: DATA; } }
 })");
  std::vector<livehd::liberty::Memory_skip> skipped;
  std::vector<livehd::liberty::Memory_cell> cells;
  ASSERT_NO_THROW(cells = livehd::liberty::scan_memory_cells(file, &skipped));
  ASSERT_EQ(cells.size(), 1U);
  EXPECT_EQ(cells[0].name, "ram_local");
  ASSERT_EQ(cells[0].pins.size(), 3U);  // CK, D, Q: the internal node is dropped
  EXPECT_EQ(cells[0].pins[1].msb, 2);
  EXPECT_EQ(cells[0].pins[1].lsb, 0);
  EXPECT_EQ(cells[0].pins[2].msb, 0);  // the cell-scope DATA shadows the library's [7:0]
  EXPECT_EQ(cells[0].pins[2].lsb, 2);
  ASSERT_EQ(skipped.size(), 3U);
  EXPECT_EQ(skipped[0].name, "rom8");
  EXPECT_NE(skipped[0].reason.find("rom"), std::string::npos);
  EXPECT_EQ(skipped[1].name, "ram_inout");
  EXPECT_NE(skipped[1].reason.find("inout"), std::string::npos);
  EXPECT_EQ(skipped[2].name, "ram_untyped");
  EXPECT_NE(skipped[2].reason.find("NOPE"), std::string::npos);
  EXPECT_EQ(livehd::liberty::scan_memory_cells(file).size(), 1U);  // the reason list is optional
}

TEST(LibertyMemory, InvalidAndConflictingInterfacesAreRefused) {
  const std::string cell = R"(cell(ram) { memory() { type: ram; address_width: 3; word_width: 1; }
    pin(D) { direction: input; } pin(Q) { direction: output; } })";
  const auto        same = write_lib("memory_corners.lib", "library(a){" + cell + "} library(b){" + cell + "}");
  EXPECT_EQ(livehd::liberty::scan_memory_cells(same).size(), 1U);
  auto bad = cell;
  bad.replace(bad.find("word_width: 1"), std::string("word_width: 1").size(), "word_width: 2");
  EXPECT_THROW(
      livehd::liberty::scan_memory_cells(write_lib("memory_conflict.lib", "library(a){" + cell + "} library(b){" + bad + "}")),
      std::runtime_error);
  const auto invalid = write_lib("memory_bus_bad.lib", R"(library(a){
    type(DATA) { bit_width: 7; bit_from: 0; bit_to: 3; }
    cell(ram) { memory() { type: ram; address_width: 2; word_width: 7; }
      bus(D) { direction: input; bus_type: DATA; } }
  })");
  EXPECT_THROW(livehd::liberty::scan_memory_cells(invalid), std::runtime_error);
}

TEST(LibertyFunction, ClockedMemoryBusDriveKeepsConstantDelayAndElectricalLimits) {
  const std::string contents = R"(
    library (ram) { time_unit : "1ns"; capacitive_load_unit (1, "pf");
      type(data) { base_type : array; data_type : bit; bit_width : 2; bit_from : 3; bit_to : 2; }
      type(addr) { base_type : array; data_type : bit; bit_width : 4; bit_from : 3; bit_to : 0; }
      cell(RAM) { memory() { type : ram; address_width : 4; word_width : 2; }
        pin(CLK) { direction : input; clock : true; }
        bus(A) { bus_type : addr; direction : input; }
        bus(Q) { bus_type : data; direction : output; max_capacitance : 0.5;
          memory_read() { address : A; }
          timing() { related_pin : "CLK"; timing_type : rising_edge;
            cell_rise(t) { index_2("0.005,0.5"); values("0.218,0.218"); } }
        }
      }
    }
  )";
  const auto        drives   = livehd::liberty::scan_output_drives_text(contents);
  ASSERT_EQ(drives.size(), 2);
  for (size_t i = 0; i < drives.size(); ++i) {
    EXPECT_EQ(drives[i].name, "RAM");
    EXPECT_EQ(drives[i].output, "Q[" + std::to_string(i + 2) + "]");
    EXPECT_TRUE(drives[i].sequential);
    EXPECT_DOUBLE_EQ(drives[i].max_capacitance_ff.value(), 500);
    EXPECT_DOUBLE_EQ(drives[i].intrinsic_ps, 218);
    EXPECT_DOUBLE_EQ(drives[i].resistance_ohm, 0);
    EXPECT_FALSE(drives[i].max_fanout);
  }
  auto              async_contents = contents;
  const std::string edge           = "related_pin : \"CLK\"; timing_type : rising_edge;";
  const auto        at             = async_contents.find(edge);
  ASSERT_NE(at, std::string::npos);
  async_contents.replace(at, edge.size(), "related_pin : \"A\"; timing_type : combinational;");
  const auto async_drives = livehd::liberty::scan_output_drives_text(async_contents);
  ASSERT_EQ(async_drives.size(), 2);
  EXPECT_TRUE(async_drives[0].sequential);  // storage cell, even with an asynchronous output
  EXPECT_DOUBLE_EQ(async_drives[0].intrinsic_ps, 218);
  EXPECT_FALSE(livehd::liberty::scan_memory_cells_text(async_contents)[0].pins.back().clocked);
  EXPECT_TRUE(livehd::liberty::scan_comb_cells_text(R"(
    library(ram) { cell(RAM) { memory() { type : ram; } } }
  )")
                  .empty());
}
