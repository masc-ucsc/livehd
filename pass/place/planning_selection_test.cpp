// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "planning_selection.hpp"

#include <sstream>

#include "gtest/gtest.h"
#include "handoff_pins.hpp"
#include "place.hpp"
#include "placement_session.hpp"
#include "rapidjson/document.h"

namespace livehd::placement {
namespace {
timing::Feedback feedback(double slack_ps, bool ranking = false, double unit = 1) {
  timing::Feedback out;
  out.time_unit_ps            = unit;
  out.ranking_only            = ranking;
  out.pins["capture:D"].slack = slack_ps / unit;
  out.pins["clock:CLK"].slack = -100000 / unit;
  out.pins["clock:CLK"].clock = true;
  if (ranking) {
    auto  layout = std::make_shared<timing::Feedback::Ranking::Layout>();
    auto& r      = out.ranking.emplace();
    r.layout     = layout;
    for (const auto& [name, pin] : out.pins) {
      layout->labels.push_back(name);
      r.pins.push_back({*pin.slack, pin.clock});
    }
    out.pins.clear();
  }
  return out;
}
}  // namespace

TEST(PlanningSelection, LowerCongestionCannotSpendTimingBeyondTheInitialReference) {
  // The production policy sees legal candidates together with their retained
  // optimizer snapshots. A rejected candidate must not replace either pose or
  // timing. Exercise that ownership with a real continued native optimizer.
  namespace dp = DreamPlace::native;
  dp::Design d;
  d.layout       = {0, 0, 80, 40, 1, 1};
  d.num_movable  = 2;
  d.node_x       = {10, 40};
  d.node_y       = {10, 20};
  d.node_size_x  = {2, 2};
  d.node_size_y  = {1, 1};
  d.pin2node     = {0, 1};
  d.pin_offset_x = d.pin_offset_y = {0, 0};
  d.flat_net2pin                  = {0, 1};
  d.flat_net2pin_start            = {0, 2};
  d.build_maps();
  dp::Options controls;
  controls.max_iterations = 2;
  controls.min_iterations = 0;
  controls.num_threads    = 1;
  controls.bins_x = controls.bins_y = 8;
  controls.filler_max_count         = 16;
  controls.legalize = controls.detailed_place = false;
  Placement_session              session;
  const std::vector<std::string> names{"cell:a", "cell:b"};
  Planning_selection             selection(25);
  struct Candidate {
    dp::Design                  pose;
    timing::Feedback            timing;
    Placement_session::Snapshot optimizer;
  };
  std::optional<Candidate> chosen;
  const double             slacks[] = {-100, -120, -140};
  for (unsigned round = 0; round < 3; ++round) {
    session.advance(d, controls, names, "candidate", 2);
    Candidate candidate{d, feedback(slacks[round], true), session.snapshot()};
    if (!selection.consider(round, 100 - 20 * round, candidate.timing)) {
      break;
    }
    chosen = std::move(candidate);
  }
  ASSERT_TRUE(chosen);
  EXPECT_EQ(selection.selected_round(), 1u);
  EXPECT_EQ(selection.last().reason, "timing_degraded");
  d = chosen->pose;
  session.restore(chosen->optimizer);
  EXPECT_EQ(d.node_x, chosen->optimizer.physical.node_x);
  EXPECT_EQ(session.iteration(), 4);
  EXPECT_EQ(session.spent_iterations(), 6u);
  EXPECT_EQ(session.cold_starts(), 1u);
  EXPECT_EQ(chosen->timing.pin_rank("capture", "D").slack, -120);
  std::ostringstream text;
  selection.write(text);
  rapidjson::Document report;
  report.Parse(text.str().c_str());
  ASSERT_FALSE(report.HasParseError());
  EXPECT_EQ(report["reference_worst_data_slack_ps"].GetDouble(), -100);
  EXPECT_EQ(report["selected_round"].GetUint(), 1u);
  ASSERT_EQ(report["candidates"].Size(), 3u);
  EXPECT_EQ(report["candidates"][2]["worst_data_slack_ps"].GetDouble(), -140);
  EXPECT_FALSE(report["candidates"][2]["accepted"].GetBool());
}

TEST(PlanningSelection, ConvertsUnitsAndIgnoresClockPinsWithoutUsingReportedWns) {
  for (bool ranking : {false, true}) {
    Planning_selection selection(25);
    auto               first = feedback(-100, ranking, 1000);
    first.wns                = -999;  // includes another scope; the policy compares data pins
    EXPECT_TRUE(selection.consider(0, 10, first));
    EXPECT_TRUE(selection.consider(1, 9, feedback(-125, ranking, 1)));
    EXPECT_FALSE(selection.consider(2, 8, feedback(-125.01, ranking, .001)));
    EXPECT_EQ(selection.last().reason, "timing_degraded");
    EXPECT_EQ(selection.last().data_pins, 1u);
    EXPECT_NEAR(*selection.last().worst_data_slack_ps, -125.01, 1e-9);
  }
}

TEST(PlanningSelection, MissingOrIncompatibleCandidateTimingDoesNotReplaceTheIncumbent) {
  Planning_selection selection(25);
  ASSERT_TRUE(selection.consider(0, 10, feedback(-100)));
  EXPECT_FALSE(selection.consider(1, 9, {}));
  EXPECT_EQ(selection.last().reason, "timing_missing");
  auto clock_only                    = feedback(-100);
  clock_only.pins["capture:D"].clock = true;
  EXPECT_FALSE(selection.consider(2, 8, clock_only));
  EXPECT_EQ(selection.last().reason, "timing_missing");
  auto changed            = feedback(-50);
  changed.pins["other:D"] = changed.pins.extract("capture:D").mapped();
  EXPECT_FALSE(selection.consider(3, 7, changed));
  EXPECT_EQ(selection.last().reason, "timing_view_changed");
  EXPECT_FALSE(selection.consider(4, 6, feedback(-50, true)));
  EXPECT_EQ(selection.last().reason, "timing_view_changed");
  EXPECT_EQ(selection.selected_round(), 0u);
  for (auto missing : {timing::Feedback{}, clock_only}) {
    Planning_selection initial(25);
    EXPECT_THROW(initial.consider(0, 1, missing), std::runtime_error);
    EXPECT_EQ(initial.last().reason, "reference_timing_missing");
    EXPECT_FALSE(initial.selected_round());
  }
}

TEST(PlanningSelection, DisabledGuardRetainsRoutingSelectionAndRejectsInvalidLimits) {
  Planning_selection selection;
  EXPECT_TRUE(selection.consider(0, 10, {}));
  EXPECT_TRUE(selection.consider(1, 9, feedback(-100000)));
  EXPECT_FALSE(selection.consider(2, 11, feedback(10)));
  EXPECT_EQ(selection.last().reason, "routing_worse");
  EXPECT_TRUE(selection.consider(3, 9, {}));
  EXPECT_EQ(selection.selected_round(), 3u);
  for (double limit : {-2., -.5, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
    EXPECT_THROW(Planning_selection{limit}, std::invalid_argument);
  }
  auto invalid         = feedback(-100);
  invalid.time_unit_ps = 0;
  Planning_selection guard(0);
  EXPECT_THROW(guard.consider(0, 1, invalid), std::runtime_error);
}

TEST(PlanningSelection, CriticalRecoveryContinuesRejectedTrialsAndRetainsBestMeasuredSnapshot) {
  namespace dp = DreamPlace::native;
  dp::Design d;
  d.layout       = {0, 0, 80, 40, 1, 1};
  d.num_movable  = 2;
  d.node_x       = {10, 40};
  d.node_y       = {10, 20};
  d.node_size_x  = {2, 2};
  d.node_size_y  = {1, 1};
  d.pin2node     = {0, 1};
  d.pin_offset_x = d.pin_offset_y = {0, 0};
  d.flat_net2pin                  = {0, 1};
  d.flat_net2pin_start            = {0, 2};
  d.build_maps();
  dp::Options ctl;
  ctl.max_iterations = 2;
  ctl.min_iterations = 0;
  ctl.num_threads    = 1;
  ctl.bins_x = ctl.bins_y = 8;
  ctl.filler_max_count    = 16;
  ctl.legalize = ctl.detailed_place = false;
  Placement_session              session;
  const std::vector<std::string> names{"cell:a", "cell:b"};
  session.advance(d, ctl, names, "initial", 2);
  const auto                                 real_widths = d.node_size_x;
  Planning_selection                         selection(25);
  std::optional<Placement_session::Snapshot> chosen;
  const size_t                               scores[] = {5, 9, 3, 0};
  unsigned                                   measured = 0;
  for (unsigned round = 0; round <= 3; ++round) {
    ++measured;
    if (selection.consider(round, scores[round], feedback(-100))) {
      chosen = session.snapshot();
    }
    const auto stop = selection.recovery_stop_reason(true, 3, scores[round] == 0);
    if (round == 1) {
      EXPECT_FALSE(selection.last().accepted);
      EXPECT_EQ(selection.last().reason, "routing_worse");
      EXPECT_EQ(selection.selected_round(), 0u);
      EXPECT_TRUE(stop.empty());
      EXPECT_EQ(selection.recovery_stop_reason(false, 3, false), "candidate_rejected_routing_worse");
    }
    if (!stop.empty()) {
      EXPECT_EQ(stop, "plan_pass");
      break;
    }
    auto pressured                   = session.snapshot();
    pressured.optimizer.inflation[0] = 1.5;
    session.restore(pressured);
    session.advance(d, ctl, names, "plan_rebalance", 2, {}, false, true);
    EXPECT_TRUE(session.history().back().routing_inflation_reset);
    EXPECT_TRUE(session.history().back().density_reset);
    EXPECT_DOUBLE_EQ(session.snapshot().optimizer.inflation[0], 1);
    EXPECT_EQ(d.node_size_x, real_widths);
  }
  ASSERT_TRUE(chosen);
  EXPECT_EQ(measured, 4u);
  EXPECT_EQ(selection.selected_round(), 3u);
  d = chosen->physical;
  session.restore(*chosen);
  EXPECT_EQ(d.node_x, session.snapshot().physical.node_x);
  EXPECT_EQ(session.spent_iterations(), 8u);
}

TEST(PlanningSelection, CriticalRetryBudgetAndTimingGuardHaveSeparateOwnership) {
  Planning_selection selection(25);
  ASSERT_TRUE(selection.consider(0, 5, feedback(-100)));
  for (unsigned round = 1; round <= 3; ++round) {
    EXPECT_FALSE(selection.consider(round, 5 + round, feedback(-100)));
    EXPECT_EQ(selection.selected_round(), 0u);
    EXPECT_EQ(selection.recovery_stop_reason(true, 3, false), round < 3 ? "" : "retry_budget_exhausted");
  }
  Planning_selection timing(25);
  ASSERT_TRUE(timing.consider(0, 5, feedback(-100)));
  EXPECT_FALSE(timing.consider(1, 0, feedback(-200)));
  EXPECT_EQ(timing.last().reason, "timing_degraded");
  EXPECT_TRUE(timing.recovery_stop_reason(true, 3, true).empty());
  EXPECT_TRUE(timing.consider(2, 0, feedback(-110)));
  EXPECT_EQ(timing.recovery_stop_reason(true, 3, true), "plan_pass");
}

TEST(PlanningSelection, RawCriticalPlanDefersOnlyMovableCoreLegalization) {
  namespace ph = physical;
  ph::Technology tech;
  tech.dbu   = 1000;
  auto& site = tech.sites["site"];
  site.width = site.height = .010;
  auto& macro              = tech.macros["cell"];
  macro.klass              = "CORE";
  macro.width = macro.height = .010;
  macro.symmetry_x = macro.symmetry_y = true;
  ph::Floorplan frame;
  frame.dbu = 1000;
  frame.die = {0, 0, 100, 40};
  for (int y = 0; y < 40; y += 10) {
    frame.rows.push_back({"r" + std::to_string(y), "site", 0, y, 10, 10, ph::Orientation::N});
  }
  Ann_place fixed(0, 0, 10, 10);
  fixed.status = ph::Status::FIXED;
  Ann_place raw(15, 3, 10, 10);
  raw.status       = ph::Status::PLACED;
  raw.orientation  = ph::Orientation::FS;
  frame.components = {
      { "fixed", "cell", fixed},
      {"moving", "cell",   raw}
  };
  Options options;
  options.legroute_plan.enabled               = true;
  options.legroute_options.critical_cellboxes = true;
  EXPECT_THROW(check_legality(tech, frame, options), std::runtime_error);
  EXPECT_NO_THROW(check_critical_plan_frame(tech, frame, options));
  auto bad                  = frame;
  bad.components[0].place.x = -1;
  EXPECT_THROW(check_critical_plan_frame(tech, bad, options), std::runtime_error);
  bad                       = frame;
  bad.components[1].place.x = 95;
  EXPECT_THROW(check_critical_plan_frame(tech, bad, options), std::runtime_error);
  bad                           = frame;
  bad.components[1].place.width = 11;
  EXPECT_THROW(check_critical_plan_frame(tech, bad, options), std::runtime_error);
  bad = frame;
  bad.components.push_back({"fixed_overlap", "cell", fixed});
  EXPECT_THROW(check_critical_plan_frame(tech, bad, options), std::runtime_error);
  options.legroute_options.critical_cellboxes = false;
  EXPECT_THROW(check_critical_plan_frame(tech, frame, options), std::invalid_argument);
}

TEST(PlanningSelection, PowerAwareIoPoseDrivesTimingAndOptimizerRollback) {
  namespace dp = DreamPlace::native;
  namespace ph = physical;
  ph::Technology tech;
  tech.manufacturing_grid = .001;
  for (const auto& name : {"M4", "M5"}) {
    auto& layer         = tech.layers[name];
    layer.width         = .020;
    layer.direction     = name == std::string("M4") ? "HORIZONTAL" : "VERTICAL";
    layer.rules.loaded  = true;
    layer.rules.area    = .002;
    layer.rules.spacing = {
        {.020, {}, {}}
    };
  }
  ph::Floorplan frame;
  frame.die    = {0, 0, 1000, 1000};
  frame.tracks = {
      {"Y", "M4", 10, 10, 100},
      {"X", "M5", 10, 10, 100}
  };
  frame.terminals = {
      {.name = "a", .net = "logical_a", .x = 105, .y = 0, .status = ph::Status::PLACED},
      {.name = "z", .net = "logical_z", .x = 805, .y = 0, .status = ph::Status::PLACED}
  };
  auto signal_pins = frame.terminals;
  // A real supply obstruction makes legal handoff IO differ from the GP IO.
  frame.terminals.push_back(
      {.name = "VDD", .net = "VDD", .status = ph::Status::FIXED, .shapes = {{"M5", {100, 0, 120, 200}}}, .use = "POWER"});
  const auto measured = handoff_pins(tech, frame, "M4", "M5");
  ASSERT_NE(measured[0].x, signal_pins[0].x);

  dp::Design d;
  d.layout      = {0, 0, 1000, 1000, 1, 1};
  d.num_movable = 1;
  // A fixed placement-only object sits between cells and signal IOs. Supply
  // terminals must not become new nodes or overwrite that object.
  d.node_x      = {200, 500, 105, 805};
  d.node_y      = {200, 500, 0, 0};
  d.node_size_x = d.node_size_y = {2, 20, 0, 0};
  d.pin2node                    = {2, 0, 0, 3};
  d.pin_offset_x = d.pin_offset_y = {0, 0, 0, 0};
  d.flat_net2pin                  = {0, 1, 2, 3};
  d.flat_net2pin_start            = {0, 2, 4};
  d.build_maps();
  dp::Options controls;
  controls.max_iterations = 2;
  controls.min_iterations = 0;
  controls.num_threads    = 1;
  controls.bins_x = controls.bins_y = 8;
  controls.filler_max_count         = 16;
  controls.legalize = controls.detailed_place = false;
  Placement_session              session;
  const std::vector<std::string> names{"cell:a", "mask:0", "io:a", "io:z"};
  session.advance(d, controls, names, "initial", 2);
  const auto old_timing   = feedback(-std::abs(d.node_x[0] - d.node_x[2]));
  auto       routed_names = measured;
  routed_names[0].net     = "sat_net_0";
  detail::install_handoff_io(d, signal_pins, 2, routed_names);
  EXPECT_EQ(d.node_x.size(), 4u);
  EXPECT_EQ(d.node_x[1], 500);
  EXPECT_EQ(d.node_x[2], measured[0].x);
  EXPECT_EQ(d.node_y[2], measured[0].y);
  EXPECT_EQ(signal_pins.size(), 2u);
  EXPECT_EQ(signal_pins[0].net, "logical_a");
  EXPECT_EQ(signal_pins[0].shapes, measured[0].shapes);
  const auto refreshed = feedback(-std::abs(d.node_x[0] - d.node_x[2]));
  EXPECT_NE(refreshed.pins.at("capture:D").slack, old_timing.pins.at("capture:D").slack);
  session.synchronize(d, controls, names, "plan_measure");
  const auto accepted = session.snapshot();
  EXPECT_EQ(accepted.physical.node_x[2], measured[0].x);
  EXPECT_EQ(accepted.physical.node_y[2], measured[0].y);
  Planning_selection selection(25);
  ASSERT_TRUE(selection.consider(0, 100, refreshed));
  const auto accepted_pins = signal_pins;
  auto       losing        = routed_names;
  losing[0].x              = 999;
  detail::install_handoff_io(d, signal_pins, 2, losing);
  session.synchronize(d, controls, names, "candidate");
  ASSERT_FALSE(selection.consider(1, 90, feedback(*refreshed.pins.at("capture:D").slack - 26)));
  d = accepted.physical;
  detail::install_handoff_io(d, signal_pins, 2, accepted_pins);
  session.restore(accepted);
  EXPECT_EQ(signal_pins, accepted_pins);
  EXPECT_EQ(d.node_x, session.snapshot().physical.node_x);
  EXPECT_EQ(d.node_y, session.snapshot().physical.node_y);
  EXPECT_EQ(session.cold_starts(), 1u);
  EXPECT_EQ(selection.selected_round(), 0u);
  auto wrong_order = routed_names;
  std::swap(wrong_order[0], wrong_order[1]);
  EXPECT_THROW(detail::install_handoff_io(d, signal_pins, 2, wrong_order), std::logic_error);
  EXPECT_EQ(signal_pins, accepted_pins);
  EXPECT_EQ(d.node_x, accepted.physical.node_x);
}
}  // namespace livehd::placement
