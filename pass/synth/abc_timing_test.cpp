// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "abc_timing.hpp"

#include <limits>

#include "abc_boundary.hpp"
#include "gtest/gtest.h"
// clang-format off
extern "C" {
#include "base/abc/abc.h"
#include "base/main/main.h"
#include "map/scl/sclLib.h"
}
// clang-format on

using livehd::synth::timing_better;

TEST(AbcTiming, BudgetAndParetoPoliciesKeepTimingPartOfSelection) {
  // With a budget, meeting timing dominates area; area decides among meets.
  EXPECT_TRUE(timing_better({true, 20, 90}, {true, 10, 110}, 100));
  EXPECT_FALSE(timing_better({true, 5, 110}, {true, 10, 90}, 100));
  EXPECT_TRUE(timing_better({true, 5, 95}, {true, 10, 90}, 100));
  // When both miss, improve delay even if it costs area.
  EXPECT_TRUE(timing_better({true, 20, 110}, {true, 10, 120}, 100));
  EXPECT_FALSE(timing_better({true, 5, 130}, {true, 10, 120}, 100));
  // Without a budget, neither area nor timing may regress.
  EXPECT_FALSE(timing_better({true, 5, 95}, {true, 10, 90}, -1));
  EXPECT_FALSE(timing_better({true, 20, 80}, {true, 10, 90}, -1));
  EXPECT_TRUE(timing_better({true, 5, 80}, {true, 10, 90}, -1));
  EXPECT_FALSE(timing_better({true, 10, 90}, {true, 10, 90}, -1));
  EXPECT_TRUE(timing_better({true, 10, 90}, {true, 10, 90}, -1, true));
  EXPECT_FALSE(timing_better({false, 0, 0}, {true, 10, 90}, 100));
  EXPECT_FALSE(timing_better({true, 1, std::numeric_limits<double>::quiet_NaN()}, {true, 10, 90}, 100));
}

TEST(AbcTiming, DelayTieBandLetsAreaDecideOnlyInsideTheBand) {
  // The br_tracker_reorder_buffer_ctrl_1r1w case: both miss a 400 ps budget,
  // the candidate is 1.6% smaller and 0.026 ps (0.004%) slower. Strictly that
  // is a delay loss; inside a 1% band it is a tie and the smaller one wins.
  EXPECT_FALSE(timing_better({true, 80.95, 673.276}, {true, 82.27, 673.25}, 400));
  EXPECT_TRUE(timing_better({true, 80.95, 673.276}, {true, 82.27, 673.25}, 400, false, 0.01));
  // The br_arb_weighted_rr case: 1% faster at +2.7% area is a tie inside the
  // band, so the LARGER candidate no longer wins on a noise-level delay gain.
  EXPECT_TRUE(timing_better({true, 185.08, 576.37}, {true, 180.25, 582.02}, 400));
  EXPECT_FALSE(timing_better({true, 185.08, 576.37}, {true, 180.25, 582.02}, 400, false, 0.01));
  // A real delay gain outside the band still wins over area (br_tracker_reorder:
  // 3.8% faster at +2.3% area), and a real loss still loses.
  EXPECT_TRUE(timing_better({true, 27.53, 643.11}, {true, 26.91, 668.75}, 400, false, 0.01));
  EXPECT_FALSE(timing_better({true, 20, 700}, {true, 30, 668}, 400, false, 0.01));
  // Meeting vs missing the budget stays strict, even inside the band.
  EXPECT_FALSE(timing_better({true, 5, 100.5}, {true, 10, 100}, 100.2, false, 0.01));
  EXPECT_TRUE(timing_better({true, 20, 100}, {true, 10, 100.5}, 100.2, false, 0.01));
  // Without a budget the band admits an area win at a noise-level delay cost.
  EXPECT_FALSE(timing_better({true, 5, 90.5}, {true, 10, 90}, -1));
  EXPECT_TRUE(timing_better({true, 5, 90.5}, {true, 10, 90}, -1, false, 0.01));
  // A zero band is exactly the strict order; a negative or NaN band refuses.
  EXPECT_FALSE(timing_better({true, 5, 90.5}, {true, 10, 90}, -1, false, 0));
  EXPECT_FALSE(timing_better({true, 5, 80}, {true, 10, 90}, -1, false, -0.01));
  EXPECT_FALSE(timing_better({true, 5, 80}, {true, 10, 90}, -1, false, std::numeric_limits<double>::quiet_NaN()));
}

TEST(AbcTiming, CapturesFrameDefaultsAndPerPortOverrides) {
  auto* frame = Abc_FrameCreate();
  ASSERT_NE(frame, nullptr);
  auto* previous         = Abc_FrameEnter(frame);
  char  default_driver[] = "BUFx1";
  Abc_FrameSetDrivingCell(Abc_UtilStrsav(default_driver));
  Abc_FrameSetMaxLoad(7);
  auto* network = Abc_NtkAlloc(ABC_NTK_LOGIC, ABC_FUNC_SOP, 1);
  auto* a       = Abc_NtkCreatePi(network);
  auto* b       = Abc_NtkCreatePi(network);
  char  aname[] = "a", bname[] = "b";
  Abc_ObjAssignName(a, aname, nullptr);
  Abc_ObjAssignName(b, bname, nullptr);
  Abc_ObjAddFanin(Abc_NtkCreatePo(network), a);
  auto environment = livehd::synth::abc_boundary_environment(network);
  EXPECT_EQ(environment.inputs[0].driving_cell, "BUFx1");
  EXPECT_EQ(environment.inputs[1].driving_cell, "BUFx1");
  EXPECT_DOUBLE_EQ(environment.outputs[0].load_ff, 7);
  {
    livehd::abc::Boundary_table table(2, 1);
    SC_Cell                     driver{};
    char                        driver_name[] = "NAND2x1";
    driver.pName                              = driver_name;
    table.set_pi(0, &driver, 23);
    table.install();
    environment = livehd::synth::abc_boundary_environment(network);
    EXPECT_EQ(environment.inputs[0].driving_cell, "NAND2x1");
    EXPECT_DOUBLE_EQ(environment.inputs[0].arrival_ps, 23);
    EXPECT_EQ(environment.inputs[1].driving_cell, "BUFx1");
    EXPECT_DOUBLE_EQ(environment.outputs[0].load_ff, 7);
    table.set_po(0, 11, 31);
    environment = livehd::synth::abc_boundary_environment(network);
    EXPECT_DOUBLE_EQ(environment.outputs[0].load_ff, 11);
    EXPECT_DOUBLE_EQ(environment.outputs[0].downstream_ps, 31);
  }
  Abc_NtkDelete(network);
  Abc_FrameLeave(previous);
  Abc_FrameDestroy(frame);
}
