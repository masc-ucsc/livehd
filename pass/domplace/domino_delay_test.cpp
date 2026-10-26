// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "domino_delay.hpp"

#include <filesystem>
#include <fstream>

#include "gtest/gtest.h"

namespace livehd::domplace {
namespace {
constexpr const char* kLib = "inou/lefdef/tests/data/sky130hd.lib";

usyn::Gate_formula::Metrics pdn(uint32_t stack, uint32_t branches, uint32_t transistors) {
  usyn::Gate_formula::Metrics metrics;
  metrics.stack       = stack;
  metrics.branches    = branches;
  metrics.transistors = transistors;
  return metrics;
}
const Domino_reference& sky130() {
  static const auto reference = [] {
    std::string why;
    auto        r = resolve_domino_reference(kLib, "", "", 4, 1, &why);
    EXPECT_TRUE(r) << why;
    return r.value_or(Domino_reference{});
  }();
  return reference;
}
double eval_ps(const Delay_model& model, const usyn::Gate_formula::Metrics& metrics, double d_ff = 1.681) {
  std::string why;
  const auto  d = estimate_stage_delay(model, metrics, d_ff, &why);
  EXPECT_TRUE(d) << why;
  return d ? d->eval_ps : 0;
}
double precharge_ps(const Delay_model& model, const usyn::Gate_formula::Metrics& metrics) {
  std::string why;
  const auto  d = estimate_stage_delay(model, metrics, 1.681, &why);
  EXPECT_TRUE(d) << why;
  return d ? d->precharge_ps : 0;
}
}  // namespace

// Hand-computed (independent bilinear NLDM interpolation) on the sky130 fixture:
// inv_1 has C_in = max(2.302, 2.214, 2.390) = 2.39 fF and dfxtp_1 a D input
// of max(1.678, 1.681, 1.674) = 1.681 fF.
TEST(DominoDelay, Fo4OfTheSky130InverterIsTheSelfConsistentNldmPoint) {
  const auto& reference = sky130();
  EXPECT_EQ(reference.inverter.name, "sky130_fd_sc_hd__inv_1");
  ASSERT_TRUE(reference.registers.front().d_ff);
  EXPECT_DOUBLE_EQ(*reference.registers.front().d_ff, 1.681);
  const auto model = domino_delay_model(reference);
  ASSERT_TRUE(model.fo4) << model.issue;
  const auto& fo4 = *model.fo4;
  EXPECT_EQ(fo4.input_pin, "A");
  EXPECT_EQ(fo4.output_pin, "Y");
  EXPECT_DOUBLE_EQ(fo4.c_in_ff, 2.39);
  EXPECT_DOUBLE_EQ(fo4.load_ff, 9.56);
  EXPECT_NEAR(fo4.slew_rise_ps, 88.23625179560294, 1e-6);
  EXPECT_NEAR(fo4.slew_fall_ps, 54.424010928945656, 1e-6);
  EXPECT_NEAR(fo4.delay_rise_ps, 91.1134501005379, 1e-6);
  EXPECT_NEAR(fo4.delay_fall_ps, 74.71124308586306, 1e-6);
  EXPECT_NEAR(fo4.fo4_ps, 82.91234659320048, 1e-6);
  ASSERT_TRUE(model.tau_ps);
  EXPECT_NEAR(*model.tau_ps, 82.91234659320048 / 5, 1e-6);  // p_inv = 1
}

TEST(DominoDelay, HandComputedEstimatesOnSky130) {
  const auto model = domino_delay_model(sky130());
  ASSERT_TRUE(model.tau_ps);
  const double tau = 82.91234659320048 / 5;
  std::string  why;
  // AND2 (s=2, b=1, n=2): g h = 3/3 * 1; p = 3 (1 + 0.5*1 + 2/3) / 3 = 13/6;
  // output 1.681/2.39 + 1; precharge (3 + 3 + 2) / 3 = 8/3.
  const auto   and2 = estimate_stage_delay(model, pdn(2, 1, 2), 1.681, &why);
  ASSERT_TRUE(and2) << why;
  EXPECT_DOUBLE_EQ(and2->g_dynamic, 1);
  EXPECT_DOUBLE_EQ(and2->h_dynamic, 1);
  EXPECT_NEAR(and2->p_dynamic, 13.0 / 6, 1e-12);
  EXPECT_NEAR(and2->h_output, 1.681 / 2.39, 1e-12);
  EXPECT_NEAR(and2->eval_tau, 1 + 13.0 / 6 + 1.681 / 2.39 + 1, 1e-12);
  EXPECT_NEAR(and2->eval_ps, 80.75685685749998, 1e-6);
  EXPECT_NEAR(and2->precharge_tau, 8.0 / 3, 1e-12);
  EXPECT_NEAR(and2->precharge_ps, 8.0 / 3 * tau, 1e-6);
  // AND3 and AND4: deeper footed stacks.
  EXPECT_NEAR(eval_ps(model, pdn(3, 1, 3)), 109.31555401738014, 1e-6);
  EXPECT_NEAR(eval_ps(model, pdn(4, 1, 4)), 143.40174095014035, 1e-6);
  // ab + cd (s=2, b=2, n=4) and OR4 (s=1, b=4, n=4).
  EXPECT_NEAR(eval_ps(model, pdn(2, 2, 4)), 105.63056083546012, 1e-6);
  EXPECT_NEAR(precharge_ps(model, pdn(2, 2, 4)), 11.0 / 3 * tau, 1e-6);
  EXPECT_NEAR(eval_ps(model, pdn(1, 4, 4)), 90.89058810778005, 1e-6);
  EXPECT_NEAR(precharge_ps(model, pdn(1, 4, 4)), 17.0 / 3 * tau, 1e-6);
}

TEST(DominoDelay, DeeperStacksAndMoreBranchesAreSlower) {
  const auto model = domino_delay_model(sky130());
  ASSERT_TRUE(model.tau_ps);
  for (uint32_t s = 1; s < 4; ++s) {
    // One series chain, and the same chain with a parallel twin.
    EXPECT_LT(eval_ps(model, pdn(s, 1, s)), eval_ps(model, pdn(s + 1, 1, s + 1))) << s;
    EXPECT_LT(eval_ps(model, pdn(s, 2, 2 * s)), eval_ps(model, pdn(s + 1, 2, 2 * s + 2))) << s;
    // Same devices, more of them in series.
    EXPECT_LT(eval_ps(model, pdn(s, 2, 4)), eval_ps(model, pdn(s + 1, 2, 4))) << s;
    EXPECT_EQ(precharge_ps(model, pdn(s, 1, s)), precharge_ps(model, pdn(s + 1, 1, s + 1))) << s;
  }
  for (uint32_t b = 1; b < 4; ++b) {
    for (uint32_t s = 1; s <= 2; ++s) {
      // An added branch, and the same devices regrouped into more branches.
      EXPECT_LT(eval_ps(model, pdn(s, b, b * s)), eval_ps(model, pdn(s, b + 1, (b + 1) * s))) << b << " " << s;
      EXPECT_LT(eval_ps(model, pdn(s, b, 4)), eval_ps(model, pdn(s, b + 1, 4))) << b << " " << s;
      EXPECT_LT(precharge_ps(model, pdn(s, b, b * s)), precharge_ps(model, pdn(s, b + 1, (b + 1) * s))) << b << " " << s;
    }
  }
  // More inputs on the same stack and branches: more internal diffusion.
  EXPECT_LT(eval_ps(model, pdn(2, 2, 3)), eval_ps(model, pdn(2, 2, 4)));
  // A larger register D input is a larger electrical fanout.
  EXPECT_LT(eval_ps(model, pdn(2, 1, 2), 1.681), eval_ps(model, pdn(2, 1, 2), 3.0));
}

TEST(DominoDelay, MissingInverterDataIsADiagnosticNeverAZero) {
  // An inverter without NLDM tables.
  const auto path = std::filesystem::path(testing::TempDir()) / "no_nldm.lib";
  std::ofstream(path) << "library(bare) {\n"
                         "  cell(INV) { area : 1; pin(A) {direction : input; capacitance : 0.002;}\n"
                         "    pin(Y) {direction : output; function : \"!A\";} }\n"
                         "  cell(DFF) { area : 4; ff(IQ, IQN) {clocked_on : \"CLK\"; next_state : \"D\";}\n"
                         "    pin(CLK) {direction : input; clock : true;} pin(D) {direction : input;}\n"
                         "    pin(Q) {direction : output; function : \"IQ\";} }\n"
                         "}\n";
  std::string why;
  const auto  reference = resolve_domino_reference(path.string(), "", "", 4, 1, &why);
  ASSERT_TRUE(reference) << why;
  EXPECT_FALSE(reference->registers.front().d_ff);  // no capacitance: absent, not zero
  const auto model = domino_delay_model(*reference);
  EXPECT_FALSE(model.fo4);
  EXPECT_FALSE(model.tau_ps);
  EXPECT_NE(model.issue.find("INV"), std::string::npos) << model.issue;
  EXPECT_NE(model.issue.find("NLDM"), std::string::npos) << model.issue;
  EXPECT_FALSE(estimate_stage_delay(model, pdn(2, 1, 2), 1.0, &why));
  EXPECT_NE(why.find("no FO4"), std::string::npos) << why;
  // FO4 known but the register D load unknown.
  const auto sky = domino_delay_model(sky130());
  EXPECT_FALSE(estimate_stage_delay(sky, pdn(2, 1, 2), std::nullopt, &why));
  EXPECT_NE(why.find("D input"), std::string::npos) << why;
  EXPECT_FALSE(estimate_stage_delay(sky, pdn(0, 0, 0), 1.0, &why));
  // derive_fo4 refuses a non-positive input capacitance by name.
  EXPECT_FALSE(derive_fo4(*sky130().inverter_timing, 0, &why));
  EXPECT_NE(why.find("input capacitance"), std::string::npos) << why;
}
}  // namespace livehd::domplace
