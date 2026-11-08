// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "fp_repetition.hpp"

#include <format>

#include "gtest/gtest.h"

namespace pl = livehd::placement;
namespace {
// `flops` flops of area 1 named `<prefix>r_reg[i]`, plus `memories` macros of `memory_area`.
void fill(pl::Fp_rep_module& m, const std::string& prefix, int flops, int memories = 0, double memory_area = 10) {
  for (int i = 0; i < flops; ++i) {
    m.leaves.push_back({std::format("{}r_reg[{}]", prefix, i), 1, false});
  }
  for (int i = 0; i < memories; ++i) {
    m.leaves.push_back({std::format("{}u_mem{}", prefix, i), memory_area, true});
  }
}
// The detector alone: every repeated group counts, macros or not.
pl::Fp_rep_options generic() {
  pl::Fp_rep_options o;
  o.candidates = false;
  return o;
}
pl::Fp_rep_result run(const pl::Fp_rep_design& d, pl::Fp_rep_options o = generic()) { return pl::detect_fp_repetition(d, o); }
}  // namespace

TEST(FpRepetition, SplitAndArrayBase) {
  EXPECT_EQ(pl::fp_rep_split("\\a.b/gen[0].c_reg[3][2]$_DFF_P_ "),
            (std::vector<std::string>{"a", "b", "gen[0]", "c_reg[3][2]$_DFF_P_"}));
  EXPECT_EQ(pl::fp_rep_split("x[1.2].y"), (std::vector<std::string>{"x[1.2]", "y"}));
  EXPECT_EQ(pl::fp_rep_array_base("mem_reg[12][3]$_DFFE_PP_"), "mem_reg");
  EXPECT_EQ(pl::fp_rep_array_base("q"), "q");
  EXPECT_EQ(pl::fp_rep_array_base("storage_8_3_"), "storage");
  EXPECT_EQ(pl::fp_rep_array_base("data_q"), "data_q");
  EXPECT_EQ(pl::fp_rep_array_base("_123_"), "_123_");
  EXPECT_EQ(pl::fp_rep_array_base("r1"), "r1");
}

// Eyeriss-like: four copies of one module hold 55% of the flop+memory area.
TEST(FpRepetition, FourHierarchicalCopiesEnable) {
  pl::Fp_rep_design d;
  d.modules.resize(2);
  d.modules[0].name = "top";
  d.modules[1].name = "Cluster";
  fill(d.modules[1], "pe.", 35, 2, 10);  // 55 per copy, 220 in four copies
  fill(d.modules[0], "glue.", 180);
  for (int i = 0; i < 4; ++i) {
    d.modules[0].children.push_back({std::format("cg_{}", i), 1});
  }
  const auto r = run(d);
  EXPECT_TRUE(r.hierarchy);
  ASSERT_FALSE(r.groups.empty());
  EXPECT_EQ(r.groups[0].copies.size(), 4u);
  EXPECT_EQ(r.groups[0].module, "Cluster");
  EXPECT_NEAR(r.share, .55, 1e-9);
  EXPECT_TRUE(r.enabled);
  EXPECT_EQ(r.trigger, "repetition");
  EXPECT_EQ(r.groups[0].memories, 2u);
  EXPECT_NE(pl::fp_rep_json(r, generic()).find("\"trigger\":\"repetition\""), std::string::npos);
}

TEST(FpRepetition, TwoSmallCopiesStayOff) {
  pl::Fp_rep_design d;
  d.modules.resize(2);
  d.modules[0].name = "top";
  d.modules[1].name = "Core";
  fill(d.modules[1], "", 5);
  fill(d.modules[0], "big.", 90);
  d.modules[0].children = {
      {"core_0", 1},
      {"core_1", 1}
  };
  const auto r = run(d);
  ASSERT_FALSE(r.groups.empty());
  EXPECT_NEAR(r.share, .10, 1e-9);
  EXPECT_FALSE(r.enabled);
  EXPECT_EQ(r.trigger, "none");
  auto lower  = generic();
  lower.share = .1;
  EXPECT_TRUE(run(d, lower).enabled);
}

// A flattened netlist: only escaped hierarchical names remain, both separators.
TEST(FpRepetition, FlattenedNamesWithTolerance) {
  pl::Fp_rep_design d;
  d.modules.resize(1);
  for (int c = 0; c < 4; ++c) {
    // Synthesis removed one flop from the last copy: within the 5% tolerance.
    fill(d.modules[0], std::format("\\array.cg_{}/pe.", c), c == 3 ? 99 : 100, 1, 50);
  }
  fill(d.modules[0], "ctrl.", 400);
  const auto r = run(d);
  EXPECT_FALSE(r.hierarchy);
  ASSERT_FALSE(r.groups.empty());
  EXPECT_EQ(r.groups[0].parent, "array");
  EXPECT_EQ(r.groups[0].copies.size(), 4u);
  EXPECT_TRUE(r.groups[0].module.empty());
  EXPECT_NEAR(r.share, 599. / 999., 1e-9);
  EXPECT_TRUE(r.enabled);
  auto strict      = generic();
  strict.tolerance = 0;
  const auto exact = run(d, strict);
  ASSERT_FALSE(exact.groups.empty());
  EXPECT_EQ(exact.groups[0].copies.size(), 3u);  // the 99-flop copy no longer matches
  // Different memory makeup never matches.
  d.modules[0].leaves.push_back({"array.cg_0/pe.extra_mem", 50, true});
  EXPECT_EQ(run(d).groups[0].copies.size(), 3u);
}

// One big register file in one scope: area, but no copies.
TEST(FpRepetition, RegisterFileIsNotRepetition) {
  pl::Fp_rep_design d;
  d.modules.resize(1);
  for (int i = 0; i < 64; ++i) {
    for (int b = 0; b < 32; ++b) {
      d.modules[0].leaves.push_back({std::format("core.rf.mem_reg[{}][{}]", i, b), 1, false});
    }
  }
  fill(d.modules[0], "core.alu.", 500);
  const auto r = run(d);
  EXPECT_TRUE(r.groups.empty());
  EXPECT_FALSE(r.enabled);
  ASSERT_FALSE(r.arrays.empty());
  EXPECT_EQ(r.arrays[0].scope, "core/rf");
  EXPECT_EQ(r.arrays[0].name, "mem_reg");
  EXPECT_EQ(r.arrays[0].flops, 2048u);
  EXPECT_NEAR(r.array_share, 2048. / 2548., 1e-9);
  EXPECT_EQ(r.trigger, "none");
}

// A generate loop of processing elements: indexed intermediate components.
TEST(FpRepetition, GenerateLoopArray) {
  pl::Fp_rep_design d;
  d.modules.resize(1);
  for (int i = 0; i < 8; ++i) {
    fill(d.modules[0], std::format("\\gen_pe[{}].u_pe.", i), 40);
  }
  fill(d.modules[0], "ctl.", 100);
  const auto r = run(d);
  ASSERT_FALSE(r.groups.empty());
  EXPECT_EQ(r.groups[0].copies.size(), 8u);
  EXPECT_EQ(r.groups[0].parent, "");
  EXPECT_NEAR(r.share, 320. / 420., 1e-9);
  EXPECT_TRUE(r.enabled);
}

// Many tiny repeated FIFOs add up to 40%, but no copy reaches 2% on its own.
TEST(FpRepetition, TinyCopiesRejected) {
  pl::Fp_rep_design d;
  d.modules.resize(2);
  d.modules[0].name = "top";
  d.modules[1].name = "fifo";
  fill(d.modules[1], "", 4);
  for (int i = 0; i < 100; ++i) {
    d.modules[0].children.push_back({std::format("fifo_{}", i), 1});
  }
  fill(d.modules[0], "core.", 600);
  const auto r = run(d);
  EXPECT_TRUE(r.groups.empty());
  EXPECT_FALSE(r.enabled);
  auto loose           = generic();
  loose.min_copy_share = 0;
  EXPECT_NEAR(run(d, loose).share, .4, 1e-9);
}

TEST(FpRepetition, MinCopiesAndValidation) {
  pl::Fp_rep_design d;
  d.modules.resize(1);
  for (int i = 0; i < 3; ++i) {
    fill(d.modules[0], std::format("u{}.", i), 30);
  }
  auto four       = generic();
  four.min_copies = 4;
  EXPECT_TRUE(run(d).enabled);
  EXPECT_FALSE(run(d, four).enabled);
  auto bad       = generic();
  bad.min_copies = 1;
  EXPECT_THROW(run(d, bad), std::invalid_argument);
  EXPECT_FALSE(run(pl::Fp_rep_design{{{"empty", {}, {}}}, 0}).enabled);
}

// fp=auto counts by default only groups that candidate discovery can center:
// more than fp.min_macros memories per copy and fp.min_copies copies.
TEST(FpRepetition, CandidateGate) {
  pl::Fp_rep_design d;
  d.modules.resize(2);
  d.modules[0].name = "top";
  d.modules[1].name = "Cluster";
  fill(d.modules[1], "pe.", 30, 3, 10);
  fill(d.modules[0], "glue.", 100);
  for (int i = 0; i < 4; ++i) {
    d.modules[0].children.push_back({std::format("cg_{}", i), 1});
  }
  const pl::Fp_rep_options production;
  auto                     r = pl::detect_fp_repetition(d, production);
  ASSERT_FALSE(r.groups.empty());
  EXPECT_TRUE(r.groups[0].fp_candidate);
  EXPECT_NEAR(r.candidate_share, 240. / 340., 1e-9);
  EXPECT_TRUE(r.enabled);
  // Two macros per copy: repeated, but no floorplan centroid family.
  auto two          = production;
  two.fp_min_macros = 3;
  r                 = pl::detect_fp_repetition(d, two);
  EXPECT_FALSE(r.groups[0].fp_candidate);
  EXPECT_NEAR(r.share, 240. / 340., 1e-9);
  EXPECT_EQ(r.candidate_share, 0);
  EXPECT_FALSE(r.enabled);
  // Fewer copies than fp.min_copies.
  d.modules[0].children.pop_back();
  r = pl::detect_fp_repetition(d, production);
  EXPECT_FALSE(r.enabled);
  EXPECT_TRUE(run(d).enabled);
  // Memory-less repetition never has candidates.
  pl::Fp_rep_design logic;
  logic.modules.resize(1);
  for (int i = 0; i < 4; ++i) {
    fill(logic.modules[0], std::format("u{}.", i), 50);
  }
  EXPECT_FALSE(pl::detect_fp_repetition(logic, production).enabled);
  EXPECT_TRUE(run(logic).enabled);
}

// Copies must match on flops in both directions even when memory areas
// differ within tolerance: A and C are copies, B (5x the flops) is not.
TEST(FpRepetition, FlopToleranceIsTwoSided) {
  pl::Fp_rep_design d;
  d.modules.resize(4);
  d.modules[0].name = "top";
  d.modules[1].name = "A";
  d.modules[2].name = "B";
  d.modules[3].name = "C";
  fill(d.modules[1], "", 100, 1, 100);
  fill(d.modules[2], "", 500, 1, 101);
  fill(d.modules[3], "", 100, 1, 102);
  d.modules[0].children = {
      {"a", 1},
      {"b", 2},
      {"c", 3}
  };
  const auto r = run(d);
  ASSERT_EQ(r.groups.size(), 1u);
  EXPECT_EQ(r.groups[0].copies, (std::vector<std::string>{"a", "c"}));
  EXPECT_TRUE(r.groups[0].module.empty());
}

// Soft blocks (CLASS BLOCK SOFT) are standard-cell regions: candidate discovery never sees them as macros, so the
// detector must not count them as memories (they would inflate the share and set fp_candidate).
TEST(FpRepetition, SoftBlocksAreNotMemories) {
  EXPECT_TRUE(pl::fp_rep_memory_class("BLOCK"));
  EXPECT_TRUE(pl::fp_rep_memory_class("BLOCK BLACKBOX"));
  EXPECT_FALSE(pl::fp_rep_memory_class("BLOCK SOFT"));
  EXPECT_FALSE(pl::fp_rep_memory_class("CORE"));
  EXPECT_FALSE(pl::fp_rep_memory_class("PAD"));
}

// With auto_candidates, only a floorplan candidate family turns fp on, even at auto_share 0.
TEST(FpRepetition, ZeroShareStillNeedsACandidate) {
  pl::Fp_rep_design logic;
  logic.modules.resize(1);
  for (int i = 0; i < 4; ++i) {
    fill(logic.modules[0], std::format("u{}.", i), 50);
  }
  pl::Fp_rep_options zero;
  zero.share   = 0;
  const auto r = pl::detect_fp_repetition(logic, zero);
  ASSERT_FALSE(r.groups.empty());
  EXPECT_EQ(r.candidate_share, 0);
  EXPECT_FALSE(r.enabled);
  zero.candidates = false;
  EXPECT_TRUE(pl::detect_fp_repetition(logic, zero).enabled);
}

// Two families under one parent with equal share and copy count are ordered by their first copy, not by the
// order the grouping produced them (memory-less families group first).
TEST(FpRepetition, EqualGroupsOrderByFirstCopy) {
  pl::Fp_rep_design d;
  d.modules.resize(1);
  for (int i = 0; i < 4; ++i) {
    fill(d.modules[0], std::format("z{}.", i), 10);        // 10 flops, area 10
    fill(d.modules[0], std::format("a{}.", i), 0, 1, 10);  // one memory, area 10
  }
  const auto r = run(d);
  ASSERT_EQ(r.groups.size(), 2u);
  EXPECT_EQ(r.groups[0].share, r.groups[1].share);
  EXPECT_EQ(r.groups[0].copies.front(), "a0");
  EXPECT_EQ(r.groups[1].copies.front(), "z0");
}
