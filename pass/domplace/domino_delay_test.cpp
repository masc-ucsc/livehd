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
  // U26: every pull-down device is width x = 1.5 (half a reference-inverter
  // input), so the output inverter (3) over one device input is h = 2 and the
  // drains on the dynamic node are 1.5 wide; the precharge drain is w = 2.
  const double out  = 1.681 / 2.39 + 1;  // output inverter into dfxtp_1's D, plus its parasitic
  // AND2 (s=2, b=1, n=2): g h = 3/3 * 2; p = 3 (1 + 0.5*1 + 2/1.5) / 3 = 17/6;
  // precharge (3 + 1.5*1 + 2) / 3 = 13/6.
  const auto   and2 = estimate_stage_delay(model, pdn(2, 1, 2), 1.681, &why);
  ASSERT_TRUE(and2) << why;
  EXPECT_DOUBLE_EQ(and2->g_dynamic, 1);
  EXPECT_DOUBLE_EQ(and2->h_dynamic, 2);
  EXPECT_NEAR(and2->p_dynamic, 17.0 / 6, 1e-12);
  EXPECT_NEAR(and2->h_output, 1.681 / 2.39, 1e-12);
  EXPECT_NEAR(and2->eval_tau, 2 + 17.0 / 6 + out, 1e-12);
  EXPECT_NEAR(and2->eval_ps, (2 + 17.0 / 6 + out) * tau, 1e-6);
  EXPECT_NEAR(and2->precharge_tau, 13.0 / 6, 1e-12);
  EXPECT_NEAR(and2->precharge_ps, 13.0 / 6 * tau, 1e-6);
  // AND3 and AND4: deeper footed stacks. g h = (s+1)/3 * 2,
  // p = (s+1) (1 + 0.5 (s-1) + 4/3) / 3; the precharge is AND2's.
  EXPECT_NEAR(eval_ps(model, pdn(3, 1, 3)), (8.0 / 3 + 40.0 / 9 + out) * tau, 1e-6);
  EXPECT_NEAR(eval_ps(model, pdn(4, 1, 4)), (10.0 / 3 + 115.0 / 18 + out) * tau, 1e-6);
  EXPECT_NEAR(precharge_ps(model, pdn(4, 1, 4)), 13.0 / 6 * tau, 1e-6);
  // ab + cd (s=2, b=2, n=4): p = 3 (2 + 0.5*2 + 4/3) / 3 = 13/3, precharge
  // (3 + 3 + 2) / 3; OR4 (s=1, b=4, n=4): g h = 4/3, p = 2 (4 + 4/3) / 3 =
  // 32/9, precharge (3 + 6 + 2) / 3.
  EXPECT_NEAR(eval_ps(model, pdn(2, 2, 4)), (2 + 13.0 / 3 + out) * tau, 1e-6);
  EXPECT_NEAR(precharge_ps(model, pdn(2, 2, 4)), 8.0 / 3 * tau, 1e-6);
  EXPECT_NEAR(eval_ps(model, pdn(1, 4, 4)), (4.0 / 3 + 32.0 / 9 + out) * tau, 1e-6);
  EXPECT_NEAR(precharge_ps(model, pdn(1, 4, 4)), 11.0 / 3 * tau, 1e-6);
  // The width-3 devices of the earlier model (pulldown_width = 3) give back
  // its AND2: h = 1, p = 13/6, precharge 8/3.
  Delay_parameters wide;
  wide.pulldown_width = 3;
  auto old            = model;
  old.parameters      = wide;
  EXPECT_NEAR(eval_ps(old, pdn(2, 1, 2)), 80.75685685749998, 1e-6);
  EXPECT_NEAR(precharge_ps(old, pdn(2, 1, 2)), 8.0 / 3 * tau, 1e-6);
}

TEST(DominoDelay, RailCapacitanceCountsThePdnDevicesItGates) {
  // a&b | a&c | a&d: rail a gates three devices (1.5 C_in), b, c and d one
  // each (0.5 C_in); a complemented rail is its own pin.
  using Kind = usyn::Gate_formula::Kind;
  usyn::Gate_formula f;
  const auto         lit = [&](uint32_t v, bool negative = false) {
    f.nodes.push_back({Kind::literal, 0, 0, v, negative});
    return static_cast<uint32_t>(f.nodes.size() - 1);
  };
  const auto op = [&](Kind kind, uint32_t l, uint32_t r) {
    f.nodes.push_back({kind, l, r, 0, false});
    return static_cast<uint32_t>(f.nodes.size() - 1);
  };
  const auto ab = op(Kind::series, lit(0), lit(1));
  const auto ac = op(Kind::series, lit(0), lit(2));
  const auto ad = op(Kind::series, lit(0), lit(3, true));
  op(Kind::parallel, op(Kind::parallel, ab, ac), ad);
  const auto metrics = f.metrics();
  ASSERT_TRUE(metrics);
  const auto rails   = usyn::gate_rails(*metrics, 4);
  const auto devices = rail_devices(f, rails);
  ASSERT_EQ(rails.size(), 4U);
  EXPECT_EQ(devices, (std::vector<uint32_t>{3, 1, 1, 1}));
  EXPECT_TRUE(rails[3].negative);  // !d
  uint32_t total = 0;
  for (const auto count : devices) {
    total += count;
  }
  EXPECT_EQ(total, metrics->transistors);
  const double c_in = 2.39;
  EXPECT_DOUBLE_EQ(devices[0] * kRailDeviceCinFraction * c_in, 1.5 * c_in);
  EXPECT_DOUBLE_EQ(devices[1] * kRailDeviceCinFraction * c_in, 0.5 * c_in);
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
