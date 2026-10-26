// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "candidate.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <stdexcept>

#include "gtest/gtest.h"

namespace b = livehd::placement::buffering;
namespace m = livehd::liberty::timing;
namespace {
m::Table table(double base, double slew, double cap) {
  return {
      m::Variable::input_slew,
      m::Variable::output_load,
      {0, 100},
      {0, 100},
      {base, base + 100 * cap, base + 100 * slew, base + 100 * (cap + slew)}
  };
}
struct Fixture {
  m::Library lib;
  b::Context context;
  Fixture(size_t fanout = 1) {
    const auto cell = [&](std::string name, double cin, double delay, double ds, double dc, double slew, double ss, double sc) {
      m::Cell c;
      c.name                 = name;
      auto& in               = c.pins["A"];
      in.name                = "A";
      in.direction           = "input";
      in.capacitance_ff      = {cin, cin + 1};
      in.max_transition_ps   = 1000;
      auto& out              = c.pins["Y"];
      out.name               = "Y";
      out.direction          = "output";
      out.function           = "A";
      out.max_transition_ps  = 1000;
      out.max_capacitance_ff = 100;
      m::Arc arc;
      arc.related_pin = "A";
      arc.type        = "combinational";
      arc.sense       = m::Sense::positive;
      for (unsigned t = 0; t < 2; ++t) {
        arc.delay[t] = table(delay + t, ds, dc);
        arc.slew[t]  = table(slew + t, ss, sc);
      }
      out.arcs.push_back(arc);
      lib.cells.emplace(name, std::move(c));
    };
    cell("D", 1, 5, .5, 1, 2, .2, 2);
    cell("B1", 1, 2, .3, .2, 1, .1, .5);
    cell("B2", 2, 1, .1, .1, .5, .05, .2);
    cell("S", 4, 0, 0, 0, 0, 0, 0);
    context.core   = {0, 0, 100, 10};
    context.driver = {
        "d",
        {"D", "A", "Y"},
        {0, 0},
        {10, 20},
        {3, 7}
    };
    context.capacity["gap"]         = 10;
    context.normalization_period_ps = 100;
    for (size_t i = 0; i < fanout; ++i) {
      context.sinks.push_back({
          "s" + std::to_string(i),
          "S",
          "A",
          { 80 + double(i) * 4,                   2},
          {                100,                 120},
          {.5 / double(fanout), .5 / double(fanout)}
      });
    }
  }
  b::Candidate candidate(const std::vector<int>& groups) const {
    b::Candidate          c;
    std::map<int, size_t> used;
    for (int g : groups) {
      if (g >= 0) {
        used.emplace(g, 0);
      }
    }
    for (auto& [g, index] : used) {
      index          = c.buffers.size();
      const double x = 20 + 20 * double(index);
      c.buffers.push_back({
          "b" + std::to_string(g),
          {g ? "B2" : "B1", "A", "Y"},
          {x + .1, .5},
          {x + .8, .5},
          {x, 0, x + 1, 1},
          true,
          {{"gap", 1}}
      });
    }
    c.trees.resize(c.buffers.size() + 1);
    for (size_t i = 0; i < c.trees.size(); ++i) {
      auto& tree        = c.trees[i];
      tree.driver_stage = i;
      tree.rc.driver    = i ? c.buffers[i - 1].name + ":Y" : "d:Y";
      tree.rc.nodes.push_back({tree.rc.driver, 0});
      tree.positions.push_back(i ? c.buffers[i - 1].output : context.driver.output);
    }
    const auto connect = [&](size_t tree, bool buffer, size_t index, b::Point p) {
      auto&        net  = c.trees[tree];
      const size_t node = net.rc.nodes.size();
      net.rc.nodes.push_back({"p" + std::to_string(node), 0});
      net.positions.push_back(p);
      net.rc.edges.push_back({0, unsigned(node), 0});
      net.terminals.push_back({node, buffer, index});
      net.rc.wire_length_um += std::abs(p.x - net.positions[0].x) + std::abs(p.y - net.positions[0].y);
    };
    for (const auto& [g, index] : used) {
      connect(0, true, index, c.buffers[index].input);
    }
    for (size_t i = 0; i < groups.size(); ++i) {
      connect(groups[i] < 0 ? 0 : used.at(groups[i]) + 1, false, i, context.sinks[i].position);
    }
    return c;
  }
};
}  // namespace
TEST(BufferCandidate, ExhaustiveFanoutOneThroughFourAndTwoBufferTypesMatchIndependentStageOracle) {
  size_t evaluated = 0;
  for (size_t n = 1; n <= 4; ++n) {
    Fixture                     f(n);
    std::vector<int>            groups(n);
    std::function<void(size_t)> enumerate = [&](size_t i) {
      if (i < n) {
        for (int g : {-1, 0, 1}) {
          groups[i] = g;
          enumerate(i + 1);
        }
        return;
      }
      const auto candidate = f.candidate(groups);
      const auto actual    = b::evaluate(f.lib, f.context, candidate);
      ASSERT_EQ(actual.status, b::Status::evaluated);
      ASSERT_TRUE(actual.feasible());
      ASSERT_TRUE(actual.meets_required.value());
      std::array<double, 2> required{1e9, 1e9};
      double                cost = 0;
      for (unsigned t = 0; t < 2; ++t) {
        double        load = 0;
        std::set<int> buffers;
        for (int g : groups) {
          if (g < 0) {
            load += 4 + t;
          } else {
            buffers.insert(g);
          }
        }
        for (int g : buffers) {
          load += (g ? 2 : 1) + t;
        }
        const double source_delay = 5 + t + .5 * f.context.driver.input_slew_ps[t] + load;
        const double source_slew  = 2 + t + .2 * f.context.driver.input_slew_ps[t] + 2 * load;
        for (size_t k = 0; k < n; ++k) {
          double    delay = source_delay, slew = source_slew;
          const int g = groups[k];
          if (g >= 0) {
            const double downstream  = double(std::count(groups.begin(), groups.end(), g)) * (4 + t);
            delay                   += (g ? 1 : 2) + t + (g ? .1 : .3) * source_slew + (g ? .1 : .2) * downstream;
            slew                     = (g ? .5 : 1) + t + (g ? .05 : .1) * source_slew + (g ? .2 : .5) * downstream;
          }
          const auto& pin = actual.pins.at(f.context.sinks[k].name);
          EXPECT_NEAR(pin.arrival_ps[t], f.context.driver.input_arrival_ps[t] + delay, 1e-8);
          EXPECT_NEAR(pin.slew_ps[t], slew, 1e-8);
          required[t]  = std::min(required[t], *f.context.sinks[k].required_ps[t] - delay);
          cost        += f.context.sinks[k].multiplier[t] * delay / f.context.normalization_period_ps;
        }
        EXPECT_NEAR(*actual.root_required_ps[t], required[t], 1e-8);
      }
      EXPECT_NEAR(actual.multiplier_cost, cost, 1e-9);
      EXPECT_EQ(actual.waveform_fits, 2 * (candidate.buffers.size() + 1));
      EXPECT_DOUBLE_EQ(actual.area_um2, double(candidate.buffers.size()));
      ++evaluated;
    };
    enumerate(0);
  }
  EXPECT_EQ(evaluated, 120);
}
TEST(BufferCandidate, PhysicalRequirementsRemainSeparateFromMultiplierCostAndUnconstrainedState) {
  Fixture    f;
  const auto candidate          = f.candidate({-1});
  const auto original           = b::evaluate(f.lib, f.context, candidate);
  f.context.sinks[0].multiplier = {0, 0};
  const auto no_cost            = b::evaluate(f.lib, f.context, candidate);
  EXPECT_EQ(original.root_required_ps, no_cost.root_required_ps);
  EXPECT_DOUBLE_EQ(no_cost.multiplier_cost, 0);
  f.context.sinks[0].required_ps = {0, 0};
  const auto impossible          = b::evaluate(f.lib, f.context, candidate);
  EXPECT_TRUE(impossible.feasible());
  EXPECT_FALSE(impossible.meets_required.value());
  EXPECT_LT(*impossible.root_required_ps[0], 0);
  f.context.sinks[0].required_ps = {};
  const auto unconstrained       = b::evaluate(f.lib, f.context, candidate);
  EXPECT_FALSE(unconstrained.meets_required);
  EXPECT_FALSE(unconstrained.root_required_ps[0]);
  f.context.sinks[0].multiplier[0] = .1;
  EXPECT_THROW(b::evaluate(f.lib, f.context, candidate), std::invalid_argument);
}
TEST(BufferCandidate, CriticalIsolationRecomputesDriverBufferAndEveryNoncriticalSink) {
  Fixture    f(4);
  const auto direct   = b::evaluate(f.lib, f.context, f.candidate({-1, -1, -1, -1}));
  auto       isolated = f.candidate({-1, 0, 0, 0});
  const auto split    = b::evaluate(f.lib, f.context, isolated);
  EXPECT_LT(split.pins.at("s0").arrival_ps[0], direct.pins.at("s0").arrival_ps[0]);
  for (size_t i = 1; i < 4; ++i) {
    EXPECT_TRUE(split.pins.contains("s" + std::to_string(i)));
  }
  f.lib.cells.at("B1").pins.at("Y").max_capacitance_ff = 10;
  const auto rejected                                  = b::evaluate(f.lib, f.context, isolated);
  EXPECT_FALSE(rejected.electrically_feasible);
  EXPECT_TRUE(std::any_of(rejected.violations.begin(), rejected.violations.end(), [](const auto& v) {
    return v.object == "b0:Y" && v.kind == "capacitance";
  }));
}
TEST(BufferCandidate, ElectricalLimitsOverrideShortWireAndEveryReceiverSlewIsChecked) {
  Fixture    f;
  const auto candidate                               = f.candidate({-1});
  const auto baseline                                = b::evaluate(f.lib, f.context, candidate);
  f.lib.cells.at("S").pins.at("A").max_transition_ps = baseline.pins.at("s0").slew_ps[1] - 1e-3;
  auto bad                                           = b::evaluate(f.lib, f.context, candidate);
  EXPECT_FALSE(bad.feasible());
  f.lib.cells.at("S").pins.at("A").max_transition_ps = baseline.pins.at("s0").slew_ps[1] + 1e-3;
  EXPECT_TRUE(b::evaluate(f.lib, f.context, candidate).feasible());
  f.lib.cells.at("D").pins.at("Y").max_capacitance_ff = 3;
  bad                                                 = b::evaluate(f.lib, f.context, candidate);
  EXPECT_FALSE(bad.electrically_feasible);
}
TEST(BufferCandidate, NativeLumpedWireAndCrossLibraryThresholdConversionMatchAnalyticOracle) {
  Fixture f;
  auto    candidate = f.candidate({-1});
  auto&   arc       = f.lib.cells.at("D").pins.at("Y").arcs[0];
  for (unsigned t = 0; t < 2; ++t) {
    arc.delay[t] = table(5 + t, .5, 0);
    arc.slew[t]  = table(3 + t, .2, 0);
  }
  auto& sink                      = f.lib.cells.at("S");
  sink.thresholds.input           = {.4, .6};
  sink.thresholds.lower           = {.1, .1};
  sink.thresholds.upper           = {.9, .9};
  auto& tree                      = candidate.trees[0];
  tree.rc.nodes[0].capacitance_ff = 2;
  tree.rc.nodes[1].capacitance_ff = 3;
  tree.rc.edges[0].resistance_ohm = 1000;
  const auto actual               = b::evaluate(f.lib, f.context, candidate);
  ASSERT_TRUE(actual.feasible());
  for (unsigned t = 0; t < 2; ++t) {
    const double                                 driver_slew = 3 + t + .2 * f.context.driver.input_slew_ps[t];
    // A flat load projection keeps the proxy's characterized lumped
    // boundary: Elmore delay plus linear cross-library threshold conversion.
    const double                                 full_slew   = driver_slew / .6;
    const livehd::placement::late::Wire_waveform wire{3 + 4 + t + (t == 0 ? 1 : -1) * (sink.thresholds.input[t] - .5) * full_slew,
                                                      full_slew * .8};
    EXPECT_NEAR(actual.pins.at("s0").arrival_ps[t],
                f.context.driver.input_arrival_ps[t] + 5 + t + .5 * f.context.driver.input_slew_ps[t] + wire.delay_ps,
                1e-8);
    EXPECT_NEAR(actual.pins.at("s0").slew_ps[t], wire.slew_ps, 1e-8);
  }
}
TEST(BufferCandidate, OriginalNegativeUnateDriverMapsInputTransitionsBeforePositiveBuffer) {
  Fixture f;
  f.lib.cells.at("D").pins.at("Y").arcs[0].sense = m::Sense::negative;
  const auto actual                              = b::evaluate(f.lib, f.context, f.candidate({0}));
  ASSERT_TRUE(actual.feasible());
  for (unsigned t = 0; t < 2; ++t) {
    const double sd = 5 + t + .5 * f.context.driver.input_slew_ps[1 - t] + 1 + t;
    const double ss = 2 + t + .2 * f.context.driver.input_slew_ps[1 - t] + 2 * (1 + t);
    const double bd = 2 + t + .3 * ss + .2 * (4 + t);
    EXPECT_NEAR(actual.pins.at("s0").arrival_ps[t], f.context.driver.input_arrival_ps[1 - t] + sd + bd, 1e-8);
    EXPECT_NEAR(*actual.root_required_ps[1 - t], *f.context.sinks[0].required_ps[t] - sd - bd, 1e-8);
  }
}
TEST(BufferCandidate, SharedCapacityZeroSlotsBodyExclusionsAndRoutingBlocksAreHardLimits) {
  Fixture f(2);
  auto    candidate         = f.candidate({0, 1});
  f.context.capacity["gap"] = 1;
  auto actual               = b::evaluate(f.lib, f.context, candidate);
  EXPECT_FALSE(actual.resources_feasible);
  EXPECT_EQ(actual.demand.at("gap"), 2);
  f.context.capacity["gap"] = 0;
  EXPECT_FALSE(b::evaluate(f.lib, f.context, candidate).feasible());
  f.context.capacity["gap"] = 2;
  EXPECT_TRUE(b::evaluate(f.lib, f.context, candidate).feasible());
  f.context.placement_exclusions.push_back(candidate.buffers[0].body);
  EXPECT_FALSE(b::evaluate(f.lib, f.context, candidate).resources_feasible);
  f.context.placement_exclusions.clear();
  candidate.trees[0].routing_allowed = false;
  EXPECT_FALSE(b::evaluate(f.lib, f.context, candidate).resources_feasible);
}
TEST(BufferCandidate, UnsupportedModelsBudgetsAndMalformedTopologyCannotBecomeFeasible) {
  Fixture f(2);
  auto    candidate           = f.candidate({0, 1});
  f.context.max_waveform_fits = 1;
  auto result                 = b::evaluate(f.lib, f.context, candidate);
  EXPECT_EQ(result.status, b::Status::budget_exhausted);
  EXPECT_FALSE(result.feasible());
  EXPECT_EQ(result.waveform_fits, 0);
  f.context.max_waveform_fits = 100;
  f.context.max_rc_nodes      = 1;
  EXPECT_EQ(b::evaluate(f.lib, f.context, candidate).status, b::Status::budget_exhausted);
  f.context.max_rc_nodes                     = 100;
  f.lib.cells.at("B1").pins.at("Y").function = "!A";
  EXPECT_EQ(b::evaluate(f.lib, f.context, candidate).status, b::Status::unsupported);
  f.lib.cells.at("B1").pins.at("Y").function = "A";
  candidate.trees[0].terminals.push_back(candidate.trees[0].terminals[0]);
  EXPECT_THROW(b::evaluate(f.lib, f.context, candidate), std::invalid_argument);
}

TEST(BufferCandidate, CascadedDifferentBuffersUseActualInputSlewAndBackwardRequirements) {
  Fixture f;
  auto    c      = f.candidate({0});
  auto    second = f.candidate({1}).buffers[0];
  second.body    = {40, 0, 41, 1};
  second.input   = {40.1, .5};
  second.output  = {40.8, .5};
  c.buffers.push_back(second);
  c.trees[1].rc.nodes[1].pin = "b1:A";
  c.trees[1].positions[1]    = second.input;
  c.trees[1].terminals[0]    = {1, true, 1};
  auto last                  = c.trees[1];
  last.driver_stage          = 2;
  last.rc.driver             = "b1:Y";
  last.rc.nodes[0].pin       = "b1:Y";
  last.rc.nodes[1].pin       = "s0";
  last.positions             = {second.output, f.context.sinks[0].position};
  last.terminals[0]          = {1, false, 0};
  c.trees.push_back(last);
  for (auto& tree : c.trees) {
    const auto a = tree.positions[0], b = tree.positions[1];
    tree.rc.wire_length_um = std::abs(a.x - b.x) + std::abs(a.y - b.y);
  }
  const auto actual = b::evaluate(f.lib, f.context, c);
  ASSERT_TRUE(actual.feasible());
  EXPECT_EQ(actual.waveform_fits, 6);
  for (unsigned t = 0; t < 2; ++t) {
    const double d0 = 5 + t + .5 * f.context.driver.input_slew_ps[t] + 1 + t;
    const double s0 = 2 + t + .2 * f.context.driver.input_slew_ps[t] + 2 * (1 + t);
    const double d1 = 2 + t + .3 * s0 + .2 * (2 + t), s1 = 1 + t + .1 * s0 + .5 * (2 + t);
    const double d2 = 1 + t + .1 * s1 + .1 * (4 + t);
    EXPECT_NEAR(actual.pins.at("s0").arrival_ps[t], f.context.driver.input_arrival_ps[t] + d0 + d1 + d2, 1e-8);
    EXPECT_NEAR(*actual.pins.at("b1:A").required_ps[t], *f.context.sinks[0].required_ps[t] - d2, 1e-8);
    EXPECT_NEAR(*actual.root_required_ps[t], *f.context.sinks[0].required_ps[t] - d0 - d1 - d2, 1e-8);
  }
}
TEST(BufferCandidate, InfeasibleDistributedDriverFitRemainsUnsupportedRatherThanAFeasibleFallback) {
  Fixture f;
  auto    candidate = f.candidate({-1});
  auto&   arc       = f.lib.cells.at("D").pins.at("Y").arcs[0];
  for (unsigned t = 0; t < 2; ++t) {
    arc.delay[t] = table(5, 0, 34);
    arc.slew[t]  = table(1, 0, 0);
  }
  candidate.trees[0].rc.nodes[0].capacitance_ff = 500;
  candidate.trees[0].rc.nodes[1].capacitance_ff = 2500;
  candidate.trees[0].rc.edges[0].resistance_ohm = 5000;
  const auto result                             = b::evaluate(f.lib, f.context, candidate);
  EXPECT_EQ(result.status, b::Status::unsupported);
  EXPECT_FALSE(result.feasible());
  EXPECT_NE(result.reason.find("waveform fit failed"), std::string::npos);
}
TEST(BufferCandidate, CharacterizedBoundaryUsesTheExplicitSharedPolicyAndRetainsItsCause) {
  Fixture f;
  auto    candidate = f.candidate({-1});
  auto&   arc       = f.lib.cells.at("D").pins.at("Y").arcs[0];
  for (unsigned t = 0; t < 2; ++t) {
    arc.delay[t] = table(10, 0, 2);
    arc.slew[t]  = table(0, 0, 0);
  }
  f.lib.cells.at("S").pins.at("A").capacitance_ff = {3, 3};
  candidate.trees[0].rc.nodes[0].capacitance_ff   = 2;
  candidate.trees[0].rc.edges[0].resistance_ohm   = 10000;
  const auto strict                               = b::evaluate(f.lib, f.context, candidate);
  ASSERT_EQ(strict.status, b::Status::unsupported);
  EXPECT_FALSE(strict.characterized_boundary_policy);
  EXPECT_FALSE(strict.bounded_boundary_policy);
  EXPECT_EQ(strict.driver_modes.at(livehd::placement::late::Driver_mode::failed), 1);
  f.context.driver_options.characterized_boundary = true;
  const auto admitted                             = b::evaluate(f.lib, f.context, candidate);
  ASSERT_EQ(admitted.status, b::Status::evaluated);
  EXPECT_TRUE(admitted.feasible());
  EXPECT_TRUE(admitted.characterized_boundary_policy);
  EXPECT_FALSE(admitted.bounded_boundary_policy);
  EXPECT_EQ(admitted.driver_modes.at(livehd::placement::late::Driver_mode::table_boundary), 2);
  EXPECT_EQ(admitted.driver_failures.at(livehd::placement::late::Driver_failure::no_positive_ramp), 2);
  f.context.driver_options.characterized_boundary = false;
  const auto restored                             = b::evaluate(f.lib, f.context, candidate);
  EXPECT_EQ(restored.status, strict.status);
  EXPECT_EQ(restored.reason, strict.reason);
  EXPECT_EQ(restored.driver_modes, strict.driver_modes);
  EXPECT_EQ(restored.driver_failures, strict.driver_failures);
}

TEST(BufferCandidate, BoundedBoundaryUsesTheSameLowResistancePolicyWithoutBecomingAnUncountedFallback) {
  Fixture f;
  auto    candidate     = f.candidate({-1});
  auto&   cell          = f.lib.cells.at("D");
  cell.thresholds.lower = {.1, .1};
  cell.thresholds.upper = {.9, .9};
  auto& arc             = cell.pins.at("Y").arcs[0];
  for (unsigned t = 0; t < 2; ++t) {
    arc.delay[t] = table(10, 0, 20);
    arc.slew[t]  = table(30, 0, 3);
  }
  f.lib.cells.at("S").pins.at("A").capacitance_ff = {3, 3};
  candidate.trees[0].rc.nodes[0].capacitance_ff   = 2;
  candidate.trees[0].rc.edges[0].resistance_ohm   = 100;
  const auto strict                               = b::evaluate(f.lib, f.context, candidate);
  EXPECT_FALSE(strict.bounded_boundary_policy);
  EXPECT_FALSE(strict.driver_modes.contains(livehd::placement::late::Driver_mode::full_load_boundary));
  f.context.driver_options.bounded_boundary = true;
  const auto admitted                       = b::evaluate(f.lib, f.context, candidate);
  ASSERT_EQ(admitted.status, b::Status::evaluated);
  EXPECT_TRUE(admitted.feasible());
  EXPECT_TRUE(admitted.bounded_boundary_policy);
  EXPECT_FALSE(admitted.characterized_boundary_policy);
  EXPECT_EQ(admitted.driver_modes.at(livehd::placement::late::Driver_mode::full_load_boundary), 2);
  EXPECT_EQ(admitted.driver_failures.at(livehd::placement::late::Driver_failure::full_load_ramp), 2);
  EXPECT_NEAR(admitted.pins.at("d:Y").arrival_ps[0] - f.context.driver.input_arrival_ps[0], 110, 1e-9);
}

TEST(BufferCandidate, TimeUnitScalingPreservesNormalizedMultiplierCostAndPhysicalFeasibility) {
  Fixture    f(2);
  const auto c         = f.candidate({0, 1});
  const auto reference = b::evaluate(f.lib, f.context, c);
  for (auto& [name, cell] : f.lib.cells) {
    for (auto& [pin_name, pin] : cell.pins) {
      if (pin.max_transition_ps) {
        *pin.max_transition_ps *= 1000;
      }
      for (auto& arc : pin.arcs) {
        for (auto* tables : {&arc.delay, &arc.slew}) {
          for (auto& table : *tables) {
            if (table) {
              for (double& x : table->x) {
                x *= 1000;
              }
              for (double& value : table->values) {
                value *= 1000;
              }
            }
          }
        }
      }
    }
  }
  for (unsigned t = 0; t < 2; ++t) {
    f.context.driver.input_arrival_ps[t] *= 1000;
    f.context.driver.input_slew_ps[t]    *= 1000;
  }
  for (auto& sink : f.context.sinks) {
    for (auto& r : sink.required_ps) {
      if (r) {
        *r *= 1000;
      }
    }
  }
  f.context.normalization_period_ps *= 1000;
  const auto scaled                  = b::evaluate(f.lib, f.context, c);
  EXPECT_EQ(reference.feasible(), scaled.feasible());
  EXPECT_NEAR(reference.multiplier_cost, scaled.multiplier_cost, 1e-9);
  for (unsigned t = 0; t < 2; ++t) {
    EXPECT_NEAR(*scaled.root_required_ps[t], 1000 * *reference.root_required_ps[t], 1e-6);
  }
}

TEST(BufferCandidate, ExpandedGraphRejectsDisconnectedCyclesAndImpossibleWireGeometry) {
  Fixture f(2);
  auto    candidate = f.candidate({0, 1});
  auto&   root      = candidate.trees[0];
  root.terminals    = {
      {1, false, 0},
      {2, false, 1}
  };
  root.positions[1] = f.context.sinks[0].position;
  root.positions[2] = f.context.sinks[1].position;
  for (size_t i = 1; i < 3; ++i) {
    candidate.trees[i].terminals[0] = {1, true, 2 - i};
    candidate.trees[i].positions[1] = candidate.buffers[2 - i].input;
  }
  for (auto& tree : candidate.trees) {
    tree.rc.wire_length_um = 0;
    for (const auto& edge : tree.rc.edges) {
      tree.rc.wire_length_um += std::abs(tree.positions[edge.from].x - tree.positions[edge.to].x)
                                + std::abs(tree.positions[edge.from].y - tree.positions[edge.to].y);
    }
  }
  EXPECT_THROW(b::evaluate(f.lib, f.context, candidate), std::invalid_argument);
  candidate                            = f.candidate({-1, -1});
  candidate.trees[0].rc.wire_length_um = 0;
  EXPECT_THROW(b::evaluate(f.lib, f.context, candidate), std::invalid_argument);
}
