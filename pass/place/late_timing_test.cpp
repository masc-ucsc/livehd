// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "late_timing.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <tuple>

#include "gtest/gtest.h"
#include "late_timing_dump_fixture.hpp"
#include "timing_circuit_fixture.hpp"

namespace late = livehd::placement::late;
namespace nldm = livehd::liberty::timing;
namespace {
const std::string models = R"(
library(test) {time_unit:"1ps";capacitive_load_unit(1,ff);default_input_pin_cap:1;
  cell(INV) {pin(A){direction:input;rise_capacitance:2;fall_capacitance:3;}
    pin(Y){direction:output;function:"!A";timing(){related_pin:A;timing_sense:negative_unate;
      cell_rise(scalar){values("10");}cell_fall(scalar){values("20");}
      rise_transition(scalar){values("3");}fall_transition(scalar){values("4");}
    }}
  }
  cell(BUF) {pin(A){direction:input;}pin(Y){direction:output;function:A;
    timing(){related_pin:A;timing_sense:positive_unate;
      cell_rise(scalar){values("5");}cell_fall(scalar){values("7");}
      rise_transition(scalar){values("1");}fall_transition(scalar){values("2");}
    }}
  }
  cell(XOR) {pin(A){direction:input;}pin(B){direction:input;}pin(Y){direction:output;function:"A ^ B";
    timing(){related_pin:"A B";timing_sense:non_unate;
      cell_rise(scalar){values("5");}cell_fall(scalar){values("7");}
      rise_transition(scalar){values("1");}fall_transition(scalar){values("2");}
    }}
  }
  cell(FF) {ff(IQ,IQN){clocked_on:CLK;next_state:D;}
    pin(CLK){direction:input;clock:true;}
    pin(D){direction:input;timing(){related_pin:CLK;timing_type:setup_rising;
      rise_constraint(scalar){values("5");}fall_constraint(scalar){values("8");}
    }}
    pin(Q){direction:output;timing(){related_pin:CLK;timing_type:rising_edge;timing_sense:non_unate;
      cell_rise(scalar){values("10");}cell_fall(scalar){values("20");}
      rise_transition(scalar){values("2");}fall_transition(scalar){values("3");}
    }}
  }
})";
struct Circuit : livehd::placement::test_fixture::Timing_circuit {
  Circuit(std::initializer_list<std::string> inputs, std::initializer_list<std::string> outputs)
      : Timing_circuit(models, "late_timing", inputs, outputs) {}
};
const late::Pin_result& pin(const late::Result& result, std::string_view name) {
  const auto found = std::find_if(result.pins.begin(), result.pins.end(), [&](const auto& value) { return value.name == name; });
  if (found == result.pins.end()) {
    throw std::runtime_error("test pin missing: " + std::string(name));
  }
  return *found;
}
bool issue(const late::Result& result, const std::string& text) {
  return std::any_of(result.unsupported.begin(), result.unsupported.end(), [&](const auto& value) {
    return value.find(text) != value.npos;
  });
}
// Provisional cells: a renamed FF and P, a register whose data pin R0 has a
// 3 ps setup; neither is in the Liberty library.
std::shared_ptr<late::Provisional_cells> provisional(const nldm::Library&                liberty,
                                                     std::optional<late::Capture_budget> budget = {}) {
  auto cells   = std::make_shared<late::Provisional_cells>();
  cells->label = "test";
  auto copy    = liberty.cells.at("FF");
  copy.name    = "PFF";
  cells->library.cells.emplace("PFF", copy);
  auto half = liberty.cells.at("FF");
  half.name = "P";
  auto rail = half.pins.at("D");
  half.pins.erase("D");
  rail.name = "R0";
  for (auto& table : rail.arcs.at(0).constraint) {
    table->values = {3};
  }
  half.pins.emplace("R0", rail);
  cells->library.cells.emplace("P", half);
  if (budget) {
    cells->capture["P"] = *budget;
  }
  return cells;
}
hhds::Node_class instance(Circuit& circuit, const late::Provisional_cells& cells, const std::string& name,
                          const std::string& master) {
  return circuit.cell(name, master, &cells.library);
}
}  // namespace

TEST(LateTiming, ProvisionalCellBindsLaunchesAndCutsLikeItsRegister) {
  Circuit    circuit({"clk"}, {});
  const auto cells = provisional(*circuit.library);
  auto       flop = instance(circuit, *cells, "ff", "PFF"), inv = circuit.cell("inv", "INV");
  flop.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("clk"));
  inv.create_sink_pin("A").connect_driver(flop.create_driver_pin("Q"));
  flop.create_sink_pin("D").connect_driver(inv.create_driver_pin("Y"));
  late::Constraints constraints;
  constraints.clocks["main"] = {.period_ps = 100, .roots = {"clk"}};
  constraints.complete       = true;
  const auto unknown         = late::Proxy(*circuit.graph, circuit.library, constraints).evaluate();
  EXPECT_FALSE(unknown.certified);
  EXPECT_TRUE(issue(unknown, "uncharacterized cell: ff (PFF)"));
  const auto result = late::Proxy(*circuit.graph, circuit.library, constraints, 0, 0, cells).evaluate();
  ASSERT_TRUE(result.certified) << (result.unsupported.empty() ? "" : result.unsupported.front());
  EXPECT_EQ(pin(result, "ff:CLK").clock_name, "main");
  EXPECT_FALSE(pin(result, "ff:D").clock_name);
  EXPECT_EQ(pin(result, "ff:D").transitions[0].arrival_ps, 30);
  EXPECT_EQ(pin(result, "ff:D").transitions[1].required_ps, 92);
  EXPECT_EQ(result.wns_ps, 62);
  EXPECT_EQ(result.achievable_period_ps, 38);
  EXPECT_EQ(result.fractional_endpoints, 0);
}

TEST(LateTiming, HalfCycleCaptureScalesRequirementAndPeriod) {
  // ff -> BUF -> p.R0 captured at T/2 with its own 1 ps uncertainty (the
  // clock's 5 ps is replaced): required 0.5*100 - 1 - 3 = 46 against
  // arrivals 15 (rise) and 27 (fall), slack 19, period 2*(27+3+1) = 62.
  Circuit    circuit({"clk"}, {});
  const auto cells = provisional(*circuit.library, late::Capture_budget{.fraction = .5, .uncertainty_ps = 1});
  auto       flop = circuit.cell("ff", "FF"), buffer = circuit.cell("b", "BUF");
  auto       half = instance(circuit, *cells, "p", "P");
  flop.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("clk"));
  half.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("clk"));
  buffer.create_sink_pin("A").connect_driver(flop.create_driver_pin("Q"));
  half.create_sink_pin("R0").connect_driver(buffer.create_driver_pin("Y"));
  flop.create_sink_pin("D").connect_driver(half.create_driver_pin("Q"));
  late::Constraints constraints;
  constraints.clocks["main"] = {.period_ps = 100, .uncertainty_ps = 5, .roots = {"clk"}};
  constraints.complete       = true;
  const auto result          = late::Proxy(*circuit.graph, circuit.library, constraints, 0, 0, cells).evaluate();
  ASSERT_TRUE(result.certified) << (result.unsupported.empty() ? "" : result.unsupported.front());
  EXPECT_EQ(pin(result, "p:R0").transitions[0].arrival_ps, 15);
  EXPECT_EQ(pin(result, "p:R0").transitions[1].arrival_ps, 27);
  EXPECT_EQ(pin(result, "p:R0").transitions[0].required_ps, 46);
  EXPECT_EQ(pin(result, "p:R0").transitions[1].required_ps, 46);
  // The full-cycle D check keeps the clock uncertainty: 100 - 5 - 8 = 87.
  EXPECT_EQ(pin(result, "ff:D").transitions[1].required_ps, 87);
  EXPECT_EQ(result.wns_ps, 19);
  EXPECT_EQ(result.clocks.at("main").wns_ps, 19);
  EXPECT_EQ(result.clocks.at("main").achievable_period_ps, 62);
  EXPECT_EQ(result.achievable_period_ps, 62);
  EXPECT_EQ(result.feedback().delay, 62);
  EXPECT_EQ(result.fractional_endpoints, 1);
  // The capture filter keeps one budget's checks: the other endpoint keeps
  // no requirement, and arrivals are unchanged.
  const late::Proxy proxy(*circuit.graph, circuit.library, constraints, 0, 0, cells);
  const auto        halves = proxy.evaluate(nullptr, nullptr, {.capture_filter = [](double f) { return f == .5; }});
  const auto        fulls  = proxy.evaluate(nullptr, nullptr, {.capture_filter = [](double f) { return f == 1; }});
  EXPECT_EQ(pin(halves, "p:R0").transitions[1].required_ps, 46);
  EXPECT_FALSE(pin(halves, "ff:D").transitions[1].required_ps);
  EXPECT_EQ(pin(halves, "ff:D").transitions[1].arrival_ps, pin(result, "ff:D").transitions[1].arrival_ps);
  EXPECT_EQ(pin(fulls, "ff:D").transitions[1].required_ps, 87);
  EXPECT_FALSE(pin(fulls, "p:R0").transitions[1].required_ps);
  EXPECT_EQ(halves.fractional_endpoints, 1);
  EXPECT_EQ(fulls.fractional_endpoints, 0);
}

TEST(LateTiming, MixedFullAndHalfEndpointsTakeTheLargerPeriod) {
  // Half: ff.Q -> p.R0, slack 50-1-3-20 = 26, period 100-26/0.5 = 48.
  // Full: p.Q -> three INV -> ff.D at 60, slack 92-60 = 32, period 68. WNS is
  // the half-cycle slack, but the full-cycle path limits the period.
  Circuit    circuit({"clk"}, {});
  const auto cells = provisional(*circuit.library, late::Capture_budget{.fraction = .5, .uncertainty_ps = 1});
  auto       flop  = circuit.cell("ff", "FF");
  auto       half  = instance(circuit, *cells, "p", "P");
  flop.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("clk"));
  half.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("clk"));
  half.create_sink_pin("R0").connect_driver(flop.create_driver_pin("Q"));
  auto driver = half.create_driver_pin("Q");
  for (const auto* name : {"i1", "i2", "i3"}) {
    auto inv = circuit.cell(name, "INV");
    inv.create_sink_pin("A").connect_driver(driver);
    driver = inv.create_driver_pin("Y");
  }
  flop.create_sink_pin("D").connect_driver(driver);
  late::Constraints constraints;
  constraints.clocks["main"] = {.period_ps = 100, .roots = {"clk"}};
  constraints.complete       = true;
  const auto result          = late::Proxy(*circuit.graph, circuit.library, constraints, 0, 0, cells).evaluate();
  ASSERT_TRUE(result.certified) << (result.unsupported.empty() ? "" : result.unsupported.front());
  EXPECT_EQ(pin(result, "ff:D").transitions[0].arrival_ps, 60);
  EXPECT_EQ(pin(result, "ff:D").transitions[1].arrival_ps, 60);
  EXPECT_EQ(result.wns_ps, 26);
  EXPECT_EQ(result.tns_ps, 0);
  EXPECT_EQ(result.achievable_period_ps, 68);
  EXPECT_EQ(result.fractional_endpoints, 1);
}

TEST(LateTiming, HalfCycleAcrossClocksIsUnsupported) {
  Circuit    circuit({"ca", "cb"}, {});
  const auto cells = provisional(*circuit.library, late::Capture_budget{.fraction = .5});
  auto       flop  = circuit.cell("ff", "FF");
  auto       half  = instance(circuit, *cells, "p", "P");
  flop.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("ca"));
  half.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("cb"));
  half.create_sink_pin("R0").connect_driver(flop.create_driver_pin("Q"));
  late::Constraints constraints;
  constraints.clocks["a"] = {.period_ps = 100, .roots = {"ca"}};
  constraints.clocks["b"] = {.period_ps = 100, .roots = {"cb"}};
  constraints.complete    = true;
  const auto across       = late::Proxy(*circuit.graph, circuit.library, constraints, 0, 0, cells).evaluate();
  EXPECT_FALSE(across.certified);
  EXPECT_TRUE(issue(across, "half-cycle capture across launch domains is unsupported: p:R0"));
  EXPECT_EQ(across.fractional_endpoints, 0);
  // The legacy single clock has no named capture identity for a fraction.
  late::Constraints legacy;
  legacy.clock = late::Ideal_clock{
      .period_ps = 100,
      .roots     = {"ca", "cb"}
  };
  legacy.complete = true;
  const auto old  = late::Proxy(*circuit.graph, circuit.library, legacy, 0, 0, cells).evaluate();
  EXPECT_FALSE(old.certified);
  EXPECT_TRUE(issue(old, "half-cycle capture requires a named clock: p:R0"));
}

TEST(LateTiming, FractionalEndpointsRefuseTheFeasibleGraph) {
  Circuit    circuit({"clk"}, {});
  const auto cells = provisional(*circuit.library, late::Capture_budget{.fraction = .5});
  auto       flop  = circuit.cell("ff", "FF");
  auto       half  = instance(circuit, *cells, "p", "P");
  flop.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("clk"));
  half.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("clk"));
  half.create_sink_pin("R0").connect_driver(flop.create_driver_pin("Q"));
  flop.create_sink_pin("D").connect_driver(half.create_driver_pin("Q"));
  late::Constraints constraints;
  constraints.clocks["main"] = {.period_ps = 100, .roots = {"clk"}};
  constraints.complete       = true;
  const late::Proxy proxy(*circuit.graph, circuit.library, constraints, 0, 0, cells);
  EXPECT_TRUE(proxy.evaluate().certified);
  const auto captured = proxy.evaluate(nullptr, nullptr, {.capture_feasible_graph = true});
  EXPECT_FALSE(captured.certified);
  EXPECT_FALSE(captured.achievable_period_ps);
  EXPECT_TRUE(issue(captured, "feasible graph objective does not support fractional capture"));
}

TEST(LateTiming, ProvisionalNameCollisionThrows) {
  Circuit circuit({"clk"}, {});
  auto    cells = provisional(*circuit.library);
  auto    clash = *cells;
  clash.library.cells.emplace("FF", circuit.library->cells.at("FF"));
  const auto build = [&](const late::Provisional_cells& set) {
    return late::Proxy(*circuit.graph, circuit.library, {}, 0, 0, std::make_shared<late::Provisional_cells>(set));
  };
  EXPECT_THROW(build(clash), std::invalid_argument);
  for (const double fraction : {0., 1.5, -.5, std::nan("")}) {
    auto bad         = *cells;
    bad.capture["P"] = {.fraction = fraction};
    EXPECT_THROW(build(bad), std::invalid_argument) << fraction;
  }
  auto negative         = *cells;
  negative.capture["P"] = {.fraction = .5, .uncertainty_ps = -1};
  EXPECT_THROW(build(negative), std::invalid_argument);
  auto unknown            = *cells;
  unknown.capture["NOPE"] = {.fraction = .5};
  EXPECT_THROW(build(unknown), std::invalid_argument);
  EXPECT_NO_THROW(build(*cells));
}

TEST(LateTiming, LaunchFilterSeparatesLaunchClasses) {
  // ff.Q and p.Q (through a BUF) meet at an XOR into ff.D; input a feeds p.R0.
  Circuit    circuit({"clk", "a"}, {});
  const auto cells = provisional(*circuit.library, late::Capture_budget{.fraction = .5});
  auto       flop = circuit.cell("ff", "FF"), buffer = circuit.cell("b", "BUF"), gate = circuit.cell("x", "XOR");
  auto       half = instance(circuit, *cells, "p", "P");
  flop.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("clk"));
  half.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("clk"));
  half.create_sink_pin("R0").connect_driver(circuit.graph->get_input_pin("a"));
  buffer.create_sink_pin("A").connect_driver(half.create_driver_pin("Q"));
  gate.create_sink_pin("A").connect_driver(flop.create_driver_pin("Q"));
  gate.create_sink_pin("B").connect_driver(buffer.create_driver_pin("Y"));
  flop.create_sink_pin("D").connect_driver(gate.create_driver_pin("Y"));
  late::Constraints constraints;
  constraints.clocks["main"]         = {.period_ps = 100, .roots = {"clk"}};
  constraints.inputs["a"].arrival_ps = {3, 4};
  constraints.inputs["a"].clock      = "main";
  constraints.complete               = true;
  const late::Proxy proxy(*circuit.graph, circuit.library, constraints, 0, 0, cells);
  const auto        all = proxy.evaluate();
  ASSERT_TRUE(all.certified) << (all.unsupported.empty() ? "" : all.unsupported.front());
  const auto flops  = proxy.evaluate(nullptr,
                                    nullptr,
                                     {.launch_filter = [](std::string_view cell) { return cell == "FF"; }, .seed_inputs = false});
  const auto halves = proxy.evaluate(nullptr,
                                     nullptr,
                                     {.launch_filter = [](std::string_view cell) { return cell == "P"; }, .seed_inputs = false});
  const auto inputs = proxy.evaluate(nullptr, nullptr, {.launch_filter = [](std::string_view) { return false; }});
  // XOR (5 rise, 7 fall) after Q at 10/20, or after BUF(Q) at 15/27.
  EXPECT_EQ(pin(flops, "ff:D").transitions[0].arrival_ps, 25);
  EXPECT_EQ(pin(flops, "ff:D").transitions[1].arrival_ps, 27);
  EXPECT_EQ(pin(halves, "ff:D").transitions[0].arrival_ps, 32);
  EXPECT_EQ(pin(halves, "ff:D").transitions[1].arrival_ps, 34);
  EXPECT_EQ(pin(all, "ff:D").transitions[1].arrival_ps, 34);
  EXPECT_FALSE(pin(flops, "p:R0").transitions[0].arrival_ps);
  EXPECT_TRUE(issue(flops, "uncovered endpoint transition: p:R0"));
  EXPECT_EQ(pin(inputs, "p:R0").transitions[1].arrival_ps, 4);
  EXPECT_FALSE(pin(inputs, "ff:D").transitions[0].arrival_ps);
  // Requirements are those of the unfiltered evaluation.
  EXPECT_EQ(*pin(flops, "ff:D").transitions[1].required_ps - *pin(flops, "ff:D").transitions[1].arrival_ps, 92 - 27);
  EXPECT_EQ(pin(inputs, "p:R0").transitions[1].required_ps, pin(all, "p:R0").transitions[1].required_ps);
}

TEST(LateTiming, EmptyProvisionalIsBitExact) {
  const auto compare = [](const Circuit& circuit, const late::Constraints& constraints) {
    const auto empty = std::make_shared<late::Provisional_cells>();
    for (const bool capture : {false, true}) {
      const auto plain
          = livehd::placement::test_fixture::late_timing_dump(late::Proxy(*circuit.graph, circuit.library, constraints)
                                                                  .evaluate(nullptr, nullptr, {.capture_feasible_graph = capture}));
      const auto extended
          = livehd::placement::test_fixture::late_timing_dump(late::Proxy(*circuit.graph, circuit.library, constraints, 0, 0, empty)
                                                                  .evaluate(nullptr, nullptr, {.capture_feasible_graph = capture}));
      EXPECT_EQ(plain, extended) << capture;
    }
  };
  Circuit loop({"clk"}, {});
  auto    flop = loop.cell("ff", "FF"), inv = loop.cell("inv", "INV");
  flop.create_sink_pin("CLK").connect_driver(loop.graph->get_input_pin("clk"));
  inv.create_sink_pin("A").connect_driver(flop.create_driver_pin("Q"));
  flop.create_sink_pin("D").connect_driver(inv.create_driver_pin("Y"));
  late::Constraints legacy;
  legacy.clock = late::Ideal_clock{
      .period_ps      = 100,
      .uncertainty_ps = 3,
      .slew_ps        = {2, 4},
      .roots          = {"clk"}
  };
  legacy.complete = true;
  compare(loop, legacy);
  late::Constraints named;
  named.clocks["main"] = *legacy.clock;
  named.complete       = true;
  compare(loop, named);
}

TEST(LateTiming, NamedClockMatchesLegacyTimingOnTheSameRegisterLoop) {
  Circuit circuit({"clk"}, {});
  auto    flop = circuit.cell("ff", "FF"), inv = circuit.cell("inv", "INV");
  flop.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("clk"));
  inv.create_sink_pin("A").connect_driver(flop.create_driver_pin("Q"));
  flop.create_sink_pin("D").connect_driver(inv.create_driver_pin("Y"));
  late::Constraints legacy;
  legacy.clock    = late::Ideal_clock{.period_ps = 100, .roots = {"clk"}};
  legacy.complete = true;
  auto named      = legacy;
  named.clock.reset();
  named.clocks["main"] = *legacy.clock;
  const auto old       = late::Proxy(*circuit.graph, circuit.library, legacy).evaluate();
  const auto now       = late::Proxy(*circuit.graph, circuit.library, named).evaluate();
  ASSERT_TRUE(now.certified) << (now.unsupported.empty() ? "" : now.unsupported.front());
  EXPECT_EQ(now.wns_ps, old.wns_ps);
  EXPECT_EQ(now.tns_ps, old.tns_ps);
  EXPECT_EQ(now.achievable_period_ps, old.achievable_period_ps);
  EXPECT_EQ(now.domains.size(), 1);
  for (const auto& previous : old.pins) {
    const auto& current = pin(now, previous.name);
    for (unsigned t = 0; t < 2; ++t) {
      EXPECT_EQ(current.transitions[t].arrival_ps, previous.transitions[t].arrival_ps);
      EXPECT_EQ(current.transitions[t].required_ps, previous.transitions[t].required_ps);
      EXPECT_EQ(current.transitions[t].slew_ps, previous.transitions[t].slew_ps);
    }
  }
}

TEST(LateTiming, AsynchronousLaunchesKeepTheirOwnRequiredTimesAndClockMetrics) {
  Circuit circuit({"ca", "cb", "d"}, {"za", "zb"});
  auto    a = circuit.cell("a", "FF"), b = circuit.cell("b", "FF");
  a.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("ca"));
  b.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("cb"));
  b.create_sink_pin("D").connect_driver(circuit.graph->get_input_pin("d"));
  a.create_sink_pin("D").connect_driver(b.create_driver_pin("Q"));
  circuit.graph->get_output_pin("za").connect_driver(a.create_driver_pin("Q"));
  circuit.graph->get_output_pin("zb").connect_driver(b.create_driver_pin("Q"));
  late::Constraints constraints;
  constraints.clocks["a"] = {.period_ps = 100, .roots = {"ca"}};
  constraints.clocks["b"] = {.period_ps = 200, .roots = {"cb"}};
  constraints.asynchronous.emplace("a", "b");
  constraints.inputs["d"].arrival_ps = {10, 10};
  constraints.inputs["d"].slew_ps    = {1, 1};
  constraints.inputs["d"].clock      = "b";
  constraints.outputs["za"]          = {
      {100, 100},
      {},
      "a"
  };
  constraints.outputs["zb"] = {
      {200, 200},
      {},
      "b"
  };
  constraints.complete = true;
  const auto result    = late::Proxy(*circuit.graph, circuit.library, constraints).evaluate();
  ASSERT_TRUE(result.certified) << (result.unsupported.empty() ? "" : result.unsupported.front());
  EXPECT_EQ(result.wns_ps, 80);
  EXPECT_EQ(result.clocks.at("a").wns_ps, 80);
  EXPECT_EQ(result.clocks.at("b").wns_ps, 180);
  EXPECT_EQ(result.clocks.at("a").achievable_period_ps, 20);
  EXPECT_EQ(result.clocks.at("b").achievable_period_ps, 20);
  EXPECT_FALSE(result.achievable_period_ps);
  EXPECT_THROW(result.feedback(), std::invalid_argument);
  EXPECT_EQ(pin(result, "a:D").transitions[1].arrival_ps, 20);
  EXPECT_FALSE(pin(result, "a:D").transitions[1].required_ps);
}

TEST(LateTiming, RelatedClockOffsetsUseLaunchCaptureEdgePairs) {
  Circuit circuit({"ca", "cb", "d"}, {"z"});
  auto    a = circuit.cell("a", "FF"), b = circuit.cell("b", "FF");
  a.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("ca"));
  b.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("cb"));
  b.create_sink_pin("D").connect_driver(circuit.graph->get_input_pin("d"));
  a.create_sink_pin("D").connect_driver(b.create_driver_pin("Q"));
  circuit.graph->get_output_pin("z").connect_driver(a.create_driver_pin("Q"));
  late::Constraints constraints;
  constraints.clocks["a"]            = {.period_ps = 100, .roots = {"ca"}, .phase_ps = 25};
  constraints.clocks["b"]            = {.period_ps = 200, .roots = {"cb"}, .phase_ps = 10};
  constraints.inputs["d"].arrival_ps = {10, 10};
  constraints.inputs["d"].clock      = "b";
  constraints.outputs["z"]           = {
      {100, 100},
      {},
      "a"
  };
  constraints.complete = true;
  const auto result    = late::Proxy(*circuit.graph, circuit.library, constraints).evaluate();
  ASSERT_TRUE(result.certified) << (result.unsupported.empty() ? "" : result.unsupported.front());
  EXPECT_EQ(pin(result, "a:D").transitions[1].arrival_ps, 30);
  EXPECT_EQ(pin(result, "a:D").transitions[1].required_ps, 17);
  EXPECT_EQ(result.wns_ps, -13);
  EXPECT_EQ(result.tns_ps, -13);
  EXPECT_EQ(result.clocks.at("a").tns_ps, -13);
  EXPECT_FALSE(result.clocks.at("a").independent_period);
  EXPECT_FALSE(result.clocks.at("a").achievable_period_ps);
  EXPECT_FALSE(result.achievable_period_ps);
}

TEST(LateTiming, ASlowerAsynchronousArrivalCannotPoisonAnEligibleSetupCheck) {
  Circuit circuit({"ca", "cb", "da", "db"}, {"z"});
  auto    flop = circuit.cell("ff", "FF"), gate = circuit.cell("x", "XOR");
  flop.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("ca"));
  gate.create_sink_pin("A").connect_driver(circuit.graph->get_input_pin("da"));
  gate.create_sink_pin("B").connect_driver(circuit.graph->get_input_pin("db"));
  flop.create_sink_pin("D").connect_driver(gate.create_driver_pin("Y"));
  circuit.graph->get_output_pin("z").connect_driver(flop.create_driver_pin("Q"));
  late::Constraints constraints;
  constraints.clocks["a"] = {.period_ps = 100, .roots = {"ca"}};
  constraints.clocks["b"] = {.period_ps = 200, .roots = {"cb"}};
  constraints.asynchronous.emplace("a", "b");
  constraints.inputs["da"].arrival_ps = {10, 10};
  constraints.inputs["da"].clock      = "a";
  constraints.inputs["db"].arrival_ps = {500, 500};
  constraints.inputs["db"].clock      = "b";
  constraints.outputs["z"]            = {
      {100, 100},
      {},
      "a"
  };
  constraints.complete = true;
  const auto result    = late::Proxy(*circuit.graph, circuit.library, constraints).evaluate();
  ASSERT_TRUE(result.certified) << (result.unsupported.empty() ? "" : result.unsupported.front());
  EXPECT_EQ(result.wns_ps, 75);
  EXPECT_EQ(result.achievable_period_ps, 25);
  EXPECT_EQ(result.feedback().delay, 25);
  EXPECT_EQ(pin(result, "ff:D").transitions[1].arrival_ps, 507);
  EXPECT_EQ(pin(result, "ff:D").transitions[1].required_ps, 582);
  EXPECT_FALSE(result.clocks.at("b").wns_ps);
}

TEST(LateTiming, ForwardedClockAliasesRequireAValidatedPositiveClockPath) {
  Circuit circuit({"clk", "d"}, {"clk_out", "q"});
  auto    flop = circuit.cell("ff", "FF"), buffer = circuit.cell("buf", "BUF");
  flop.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("clk"));
  flop.create_sink_pin("D").connect_driver(circuit.graph->get_input_pin("d"));
  buffer.create_sink_pin("A").connect_driver(circuit.graph->get_input_pin("clk"));
  circuit.graph->get_output_pin("clk_out").connect_driver(buffer.create_driver_pin("Y"));
  circuit.graph->get_output_pin("q").connect_driver(flop.create_driver_pin("Q"));
  late::Constraints constraints;
  constraints.clocks["main"]         = {.period_ps = 100, .roots = {"clk"}};
  constraints.clock_aliases["fwd"]   = {"main", {"clk_out"}};
  constraints.inputs["d"].arrival_ps = {10, 10};
  constraints.inputs["d"].clock      = "fwd";
  constraints.outputs["q"]           = {
      {100, 100},
      {},
      "main"
  };
  constraints.outputs["clk_out"] = {
      {100, 100},
      {},
      "fwd"
  };
  constraints.complete = true;
  const auto result    = late::Proxy(*circuit.graph, circuit.library, constraints).evaluate();
  ASSERT_TRUE(result.certified) << (result.unsupported.empty() ? "" : result.unsupported.front());
  EXPECT_TRUE(pin(result, "clk_out").clock);
  EXPECT_FALSE(pin(result, "clk_out").endpoint);
  EXPECT_EQ(result.achievable_period_ps, 20);
  constraints.clock_aliases["fwd"].roots = {"d"};
  const auto unsupported                 = late::Proxy(*circuit.graph, circuit.library, constraints).evaluate();
  EXPECT_FALSE(unsupported.certified);
  EXPECT_TRUE(issue(unsupported, "forwarded-clock root is not reached"));
}

TEST(LateTiming, AnUnconnectedGateInputDoesNotMakeAClockBuffer) {
  // The library AND2 has two inputs; leaving B unconnected must not let the
  // ideal clock pass it as if it were a single-input buffer.
  Circuit circuit({"clk", "d"}, {"q"});
  circuit.library = std::make_shared<nldm::Library>(nldm::parse(models + R"(
    library(gates){time_unit:"1ps";capacitive_load_unit(1,ff);default_input_pin_cap:1;
      cell(AND2){pin(A){direction:input;}pin(B){direction:input;}pin(Y){direction:output;function:"A&B";
        timing(){related_pin:A;timing_sense:positive_unate;cell_rise(scalar){values("5");}cell_fall(scalar){values("5");}
          rise_transition(scalar){values("1");}fall_transition(scalar){values("1");}}
        timing(){related_pin:B;timing_sense:positive_unate;cell_rise(scalar){values("5");}cell_fall(scalar){values("5");}
          rise_transition(scalar){values("1");}fall_transition(scalar){values("1");}}}}
    })"));
  auto gate = circuit.cell("g", "AND2"), flop = circuit.cell("ff", "FF");
  gate.create_sink_pin("A").connect_driver(circuit.graph->get_input_pin("clk"));
  flop.create_sink_pin("CLK").connect_driver(gate.create_driver_pin("Y"));
  flop.create_sink_pin("D").connect_driver(circuit.graph->get_input_pin("d"));
  circuit.graph->get_output_pin("q").connect_driver(flop.create_driver_pin("Q"));
  late::Constraints constraints;
  constraints.clock                  = late::Ideal_clock{.period_ps = 100, .roots = {"clk"}};
  constraints.inputs["d"].arrival_ps = {10, 10};
  constraints.outputs["q"]           = {
      {100, 100},
      {}
  };
  constraints.complete = true;
  const auto result    = late::Proxy(*circuit.graph, circuit.library, constraints).evaluate();
  EXPECT_FALSE(result.certified);
  EXPECT_TRUE(issue(result, "ideal clock through unsupported inversion/gating: g:Y"));
  EXPECT_FALSE(pin(result, "ff:CLK").clock);
}

const std::string gates = R"(
  cell(AND2){pin(A){direction:input;}pin(B){direction:input;}pin(Y){direction:output;function:"A&B";
    timing(){related_pin:A;timing_sense:positive_unate;cell_rise(scalar){values("5");}cell_fall(scalar){values("5");}
      rise_transition(scalar){values("1");}fall_transition(scalar){values("1");}}
    timing(){related_pin:B;timing_sense:positive_unate;cell_rise(scalar){values("5");}cell_fall(scalar){values("5");}
      rise_transition(scalar){values("1");}fall_transition(scalar){values("1");}}}}
  cell(ICG){clock_gating_integrated_cell:"latch_posedge";latch(IQ,IQN){enable:"!CLK";data_in:GATE;}
    pin(CLK){direction:input;clock:true;clock_gate_clock_pin:true;}
    pin(GATE){direction:input;clock_gate_enable_pin:true;timing(){related_pin:CLK;timing_type:setup_rising;
      rise_constraint(scalar){values("4");}fall_constraint(scalar){values("4");}}}
    pin(GCLK){direction:output;clock_gate_out_pin:true;state_function:"CLK&IQ";
      timing(){related_pin:CLK;timing_sense:positive_unate;cell_rise(scalar){values("6");}cell_fall(scalar){values("6");}
        rise_transition(scalar){values("1");}fall_transition(scalar){values("1");}}}}
})";

TEST(LateTiming, IdealClockPassesARecognizedClockGate) {
  // A negative-edge enable flop (a flop on the inverted clock) ANDed with the
  // clock, as a mapped behavioral clock gate: the gated register is timed, the
  // enable storage launches on the falling edge, and the gate's enable gets a
  // clock-gating setup check against the rising edge.
  Circuit circuit({"clk", "en", "d"}, {"q"});
  circuit.library = std::make_shared<nldm::Library>(nldm::parse(models.substr(0, models.rfind('}')) + gates));
  auto inv = circuit.cell("i", "INV"), storage = circuit.cell("s", "FF"), gate = circuit.cell("g", "AND2"),
       flop = circuit.cell("r", "FF");
  inv.create_sink_pin("A").connect_driver(circuit.graph->get_input_pin("clk"));
  storage.create_sink_pin("CLK").connect_driver(inv.create_driver_pin("Y"));
  storage.create_sink_pin("D").connect_driver(circuit.graph->get_input_pin("en"));
  gate.create_sink_pin("A").connect_driver(circuit.graph->get_input_pin("clk"));
  gate.create_sink_pin("B").connect_driver(storage.create_driver_pin("Q"));
  flop.create_sink_pin("CLK").connect_driver(gate.create_driver_pin("Y"));
  flop.create_sink_pin("D").connect_driver(circuit.graph->get_input_pin("d"));
  circuit.graph->get_output_pin("q").connect_driver(flop.create_driver_pin("Q"));
  late::Constraints constraints;
  constraints.clocks["main"]          = {.period_ps = 100, .roots = {"clk"}};
  constraints.inputs["en"].arrival_ps = {10, 10};
  constraints.inputs["en"].clock      = "main";
  constraints.inputs["d"].arrival_ps  = {10, 10};
  constraints.inputs["d"].clock       = "main";
  constraints.outputs["q"]            = {
      {100, 100},
      {},
      "main"
  };
  constraints.complete = true;
  const auto result    = late::Proxy(*circuit.graph, circuit.library, constraints).evaluate();
  ASSERT_TRUE(result.certified) << (result.unsupported.empty() ? "" : result.unsupported.front());
  EXPECT_EQ(pin(result, "r:CLK").clock_name, "main");
  EXPECT_FALSE(pin(result, "r:CLK").clock_inverted);
  EXPECT_EQ(pin(result, "s:CLK").clock_name, "main");
  EXPECT_TRUE(pin(result, "s:CLK").clock_inverted);
  // The enable launches at the falling edge (50 + 10/20 clock-to-Q) and must
  // settle before the next rising edge (100): slack 30, a half-cycle pair.
  const auto& enable = pin(result, "g:B");
  EXPECT_TRUE(enable.endpoint && enable.clock_gate);
  EXPECT_EQ(enable.transitions[1].arrival_ps, 70);
  EXPECT_EQ(enable.transitions[1].required_ps, 100);
  // en (10) into the falling-edge storage (setup 5/8): slack 32 of 50.
  EXPECT_EQ(pin(result, "s:D").transitions[1].required_ps, 42);
  EXPECT_EQ(result.wns_ps, 30);
  ASSERT_EQ(result.clocks.size(), 1);
  EXPECT_EQ(result.achievable_period_ps, 40);  // 100 - 30 / 0.5
}

TEST(LateTiming, IdealClockPassesAnIntegratedClockGate) {
  Circuit circuit({"clk", "en", "d"}, {"q"});
  circuit.library = std::make_shared<nldm::Library>(nldm::parse(models.substr(0, models.rfind('}')) + gates));
  auto icg = circuit.cell("g", "ICG"), flop = circuit.cell("r", "FF");
  icg.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("clk"));
  icg.create_sink_pin("GATE").connect_driver(circuit.graph->get_input_pin("en"));
  flop.create_sink_pin("CLK").connect_driver(icg.create_driver_pin("GCLK"));
  flop.create_sink_pin("D").connect_driver(circuit.graph->get_input_pin("d"));
  circuit.graph->get_output_pin("q").connect_driver(flop.create_driver_pin("Q"));
  late::Constraints constraints;
  constraints.clocks["main"]          = {.period_ps = 100, .roots = {"clk"}};
  constraints.inputs["en"].arrival_ps = {30, 30};
  constraints.inputs["en"].clock      = "main";
  constraints.inputs["d"].arrival_ps  = {10, 10};
  constraints.inputs["d"].clock       = "main";
  constraints.outputs["q"]            = {
      {100, 100},
      {},
      "main"
  };
  constraints.complete = true;
  const auto result    = late::Proxy(*circuit.graph, circuit.library, constraints).evaluate();
  ASSERT_TRUE(result.certified) << (result.unsupported.empty() ? "" : result.unsupported.front());
  EXPECT_EQ(pin(result, "r:CLK").clock_name, "main");
  EXPECT_TRUE(pin(result, "g:GCLK").clock);
  // The cell's own clock-gating setup (4 ps) times the enable.
  EXPECT_TRUE(pin(result, "g:GATE").endpoint);
  EXPECT_EQ(pin(result, "g:GATE").transitions[0].required_ps, 96);
  EXPECT_EQ(result.wns_ps, 66);
}

TEST(LateTiming, InversionTracksTransitionsAndBackwardRequirements) {
  Circuit circuit({"a"}, {"z"});
  auto    inv = circuit.cell("u", "INV");
  inv.create_sink_pin("A").connect_driver(circuit.graph->get_input_pin("a"));
  circuit.graph->get_output_pin("z").connect_driver(inv.create_driver_pin("Y"));
  late::Constraints constraints;
  constraints.inputs["a"] = {
      {1, 2},
      {3, 4}
  };
  constraints.outputs["z"] = {
      {100, 90},
      {  5,  6}
  };
  constraints.complete = true;
  late::Proxy proxy(*circuit.graph, circuit.library, constraints, 17, 23);
  const auto  result = proxy.evaluate();
  ASSERT_TRUE(result.unsupported.empty()) << result.unsupported.front();
  EXPECT_TRUE(result.certified);
  EXPECT_EQ(result.topology_generation, 17);
  EXPECT_EQ(result.model_generation, 23);
  const auto& output = pin(result, "z");
  EXPECT_EQ(output.transitions[0].arrival_ps, 12);
  EXPECT_EQ(output.transitions[1].arrival_ps, 21);
  EXPECT_EQ(pin(result, "a").transitions[0].required_ps, 70);
  EXPECT_EQ(pin(result, "a").transitions[1].required_ps, 90);
  EXPECT_EQ(pin(result, "u:Y").transitions[0].load_ff, 5);
  EXPECT_EQ(pin(result, "u:Y").transitions[1].load_ff, 6);
  EXPECT_EQ(result.wns_ps, 69);
  EXPECT_EQ(result.tns_ps, 0);
  const auto predecessor = pin(result, "u:Y").transitions[0].predecessor;
  ASSERT_TRUE(predecessor);
  EXPECT_EQ(result.pins[predecessor->pin].name, "u:A");
  EXPECT_EQ(predecessor->transition, 1);
  EXPECT_FALSE(result.achievable_period_ps);
}
TEST(LateTiming, NonUnateAndReconvergenceKeepTheWorstPredecessor) {
  Circuit circuit({"a", "b"}, {"z"});
  auto    gate = circuit.cell("x", "XOR");
  gate.create_sink_pin("A").connect_driver(circuit.graph->get_input_pin("a"));
  gate.create_sink_pin("B").connect_driver(circuit.graph->get_input_pin("b"));
  circuit.graph->get_output_pin("z").connect_driver(gate.create_driver_pin("Y"));
  late::Constraints constraints;
  constraints.inputs["a"] = {
      {1, 20},
      {1,  1}
  };
  constraints.inputs["b"] = {
      {30, 2},
      { 1, 1}
  };
  constraints.outputs["z"] = {
      {25, 25},
      { 0,  0}
  };
  constraints.complete = true;
  const auto result    = late::Proxy(*circuit.graph, circuit.library, constraints).evaluate();
  EXPECT_EQ(pin(result, "z").transitions[0].arrival_ps, 35);
  EXPECT_EQ(pin(result, "z").transitions[1].arrival_ps, 37);
  EXPECT_EQ(result.wns_ps, -12);
  EXPECT_EQ(result.tns_ps, -12);  // once per endpoint, not once per transition
  const auto predecessor = pin(result, "x:Y").transitions[1].predecessor;
  ASSERT_TRUE(predecessor);
  EXPECT_EQ(result.pins[predecessor->pin].name, "x:B");
  EXPECT_EQ(predecessor->transition, 0);
}
TEST(LateTiming, ClockToQAndSetupCutARegisterFeedbackCycle) {
  Circuit circuit({"clk"}, {});
  auto    flop = circuit.cell("ff", "FF"), inv = circuit.cell("inv", "INV");
  flop.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("clk"));
  inv.create_sink_pin("A").connect_driver(flop.create_driver_pin("Q"));
  flop.create_sink_pin("D").connect_driver(inv.create_driver_pin("Y"));
  late::Constraints constraints;
  constraints.clock    = late::Ideal_clock{.period_ps = 100, .roots = {"clk"}};
  constraints.complete = true;
  const auto result    = late::Proxy(*circuit.graph, circuit.library, constraints).evaluate();
  ASSERT_TRUE(result.unsupported.empty()) << result.unsupported.front();
  EXPECT_TRUE(result.certified);
  EXPECT_TRUE(pin(result, "ff:CLK").clock);
  EXPECT_EQ(pin(result, "ff:D").transitions[0].arrival_ps, 30);
  EXPECT_EQ(pin(result, "ff:D").transitions[1].arrival_ps, 30);
  EXPECT_EQ(pin(result, "ff:D").transitions[0].required_ps, 95);
  EXPECT_EQ(pin(result, "ff:D").transitions[1].required_ps, 92);
  EXPECT_EQ(result.wns_ps, 62);
  EXPECT_EQ(result.achievable_period_ps, 38);
  EXPECT_EQ(result.feedback().time_unit_ps, 1);
  EXPECT_EQ(result.feedback().delay, 38);
}
TEST(LateTiming, UnresolvedCombinationalCycleIsRejected) {
  Circuit circuit({}, {"z"});
  auto    a = circuit.cell("a", "INV"), b = circuit.cell("b", "INV");
  a.create_sink_pin("A").connect_driver(b.create_driver_pin("Y"));
  b.create_sink_pin("A").connect_driver(a.create_driver_pin("Y"));
  circuit.graph->get_output_pin("z").connect_driver(a.create_driver_pin("Y"));
  EXPECT_THROW((late::Proxy(*circuit.graph, circuit.library, {})), std::invalid_argument);
}
TEST(LateTiming, IdenticalRCTreeAddsPinLoadOnceAndPreservesTransitionLoads) {
  Circuit circuit({"a"}, {"z"});
  auto    inv = circuit.cell("u", "INV");
  inv.create_sink_pin("A").connect_driver(circuit.graph->get_input_pin("a"));
  circuit.graph->get_output_pin("z").connect_driver(inv.create_driver_pin("Y"));
  late::Constraints constraints;
  constraints.inputs["a"]  = {};
  constraints.outputs["z"] = {
      {100, 100},
      {  5,   7}
  };
  constraints.complete = true;
  livehd::physical::Parasitics rc;
  rc.nets = {
      {  "a", {{"a", .2}, {"u:A", .4}}, {{0, 1, 1000}}},
      {"u:Y",   {{"u:Y", 0}, {"z", 0}},    {{0, 1, 0}}}
  };
  late::Proxy proxy(*circuit.graph, circuit.library, constraints);
  const auto  result = proxy.evaluate(&rc);
  ASSERT_TRUE(result.unsupported.empty()) << result.unsupported.front();
  EXPECT_NEAR(*pin(result, "a").transitions[0].load_ff, 2.6, 1e-12);
  EXPECT_NEAR(*pin(result, "a").transitions[1].load_ff, 3.6, 1e-12);
  EXPECT_NEAR(*pin(result, "z").transitions[0].arrival_ps, 10 + 3.4 * std::log(2), 1e-12);
  EXPECT_NEAR(*pin(result, "z").transitions[1].arrival_ps, 20 + 2.4 * std::log(2), 1e-12);
  EXPECT_EQ(pin(result, "u:Y").transitions[0].load_ff, 5);
  EXPECT_EQ(pin(result, "u:Y").transitions[1].load_ff, 7);
  rc.nets[0].nodes[1].pin = "z";
  EXPECT_THROW(proxy.evaluate(&rc), std::invalid_argument);
}
TEST(LateTiming, MissingConstraintsAndRCDoNotCertifyZeroDelay) {
  Circuit circuit({"a"}, {"z"});
  auto    buffer = circuit.cell("u", "BUF");
  buffer.create_sink_pin("A").connect_driver(circuit.graph->get_input_pin("a"));
  circuit.graph->get_output_pin("z").connect_driver(buffer.create_driver_pin("Y"));
  late::Constraints constraints;
  constraints.outputs["z"] = {
      {100, 100},
      {  0,   0}
  };
  constraints.complete = true;
  const auto result    = late::Proxy(*circuit.graph, circuit.library, constraints).evaluate();
  EXPECT_FALSE(result.certified);
  EXPECT_FALSE(result.wns_ps);
  EXPECT_FALSE(pin(result, "z").transitions[0].arrival_ps);
  EXPECT_THROW(result.feedback(), std::invalid_argument);
  EXPECT_TRUE(issue(result, "unconstrained input"));
  constraints.inputs["a"] = {};
  livehd::physical::Parasitics empty;
  const auto                   missing_rc = late::Proxy(*circuit.graph, circuit.library, constraints).evaluate(&empty);
  EXPECT_FALSE(missing_rc.certified);
  EXPECT_TRUE(issue(missing_rc, "missing physical RC"));
}
TEST(LateTiming, RepeatedEvaluationDoesNotRetainOldRCArrivals) {
  Circuit circuit({"a"}, {"z"});
  auto    buffer = circuit.cell("u", "BUF");
  buffer.create_sink_pin("A").connect_driver(circuit.graph->get_input_pin("a"));
  circuit.graph->get_output_pin("z").connect_driver(buffer.create_driver_pin("Y"));
  late::Constraints constraints;
  constraints.inputs["a"]  = {};
  constraints.outputs["z"] = {
      {100, 100},
      {  0,   0}
  };
  late::Proxy                  proxy(*circuit.graph, circuit.library, constraints);
  const auto                   before = proxy.evaluate();
  livehd::physical::Parasitics rc;
  rc.nets = {
      {  "a", {{"a", 0}, {"u:A", 10}}, {{0, 1, 1000}}},
      {"u:Y", {{"u:Y", 0}, {"z", 10}}, {{0, 1, 1000}}}
  };
  const auto wired = proxy.evaluate(&rc), after = proxy.evaluate();
  EXPECT_GT(*pin(wired, "z").transitions[0].arrival_ps, *pin(before, "z").transitions[0].arrival_ps);
  EXPECT_EQ(pin(before, "z").transitions[0].arrival_ps, pin(after, "z").transitions[0].arrival_ps);
  EXPECT_FALSE(after.certified);  // caller has not affirmed full SDC coverage
}

TEST(LateTiming, LoadIndependentDelayTablesExposeTheDelayOnlyWireApproximation) {
  Circuit circuit({"a"}, {"z"});
  auto    buffer = circuit.cell("u", "BUF");
  buffer.create_sink_pin("A").connect_driver(circuit.graph->get_input_pin("a"));
  circuit.graph->get_output_pin("z").connect_driver(buffer.create_driver_pin("Y"));
  late::Constraints constraints;
  constraints.inputs["a"]  = {};
  constraints.outputs["z"] = {
      {100, 100},
      {  0,   0}
  };
  livehd::physical::Parasitics rc;
  rc.nets = {
      {  "a",  {{"a", 0}, {"u:A", 0}},    {{0, 1, 0}}},
      {"u:Y", {{"u:Y", 0}, {"z", 10}}, {{0, 1, 1000}}}
  };
  const auto result = late::Proxy(*circuit.graph, circuit.library, constraints).evaluate(&rc);
  EXPECT_EQ(result.delay_only_wire_drivers, 1);
  EXPECT_EQ(pin(result, "z").transitions[0].arrival_ps, 15);
  EXPECT_EQ(pin(result, "z").transitions[1].arrival_ps, 17);
  EXPECT_EQ(pin(result, "z").transitions[0].slew_ps, 1);
  EXPECT_EQ(pin(result, "z").transitions[1].slew_ps, 2);
  EXPECT_FALSE(result.warnings.empty());
}

TEST(LateTiming, CalibratedStarUsesRealPinLoadsAndRespondsToMotionWithoutRefresh) {
  Circuit circuit({"a"}, {"z"});
  auto    buffer = circuit.cell("u", "BUF");
  buffer.create_sink_pin("A").connect_driver(circuit.graph->get_input_pin("a"));
  circuit.graph->get_output_pin("z").connect_driver(buffer.create_driver_pin("Y"));
  late::Constraints constraints;
  constraints.inputs["a"]  = {};
  constraints.outputs["z"] = {
      {100, 100},
      {  2,   3}
  };
  constraints.complete = true;
  livehd::physical::Parasitics rc;
  rc.nets = {
      {  "a",  {{"a", 0}, {"u:A", 0}},    {{0, 1, 0}}},
      {"u:Y", {{"u:Y", 0}, {"z", 10}}, {{0, 1, 1000}}}
  };
  late::Star_context star;
  star.policy = {
      {100, 1, 100, 1},
      1,
      "test"
  };
  star.locations = {
      {  "a",  {0, 0}},
      {"u:A",  {1, 0}},
      {"u:Y",  {0, 0}},
      {  "z", {10, 0}}
  };
  late::Proxy proxy(*circuit.graph, circuit.library, constraints);
  const auto  control = proxy.evaluate(&rc), anchor = proxy.evaluate(&rc, &star);
  EXPECT_TRUE(anchor.certified);
  EXPECT_EQ(star.stats.calibrated, 2);
  for (unsigned t = 0; t < 2; ++t) {
    EXPECT_EQ(pin(anchor, "u:Y").transitions[t].load_ff, pin(control, "u:Y").transitions[t].load_ff);
    EXPECT_NEAR(*pin(anchor, "z").transitions[t].arrival_ps, *pin(control, "z").transitions[t].arrival_ps, 1e-12);
  }
  star.locations["z"].first = 12;
  const auto moved          = proxy.evaluate(&rc, &star);
  EXPECT_EQ(star.stats.reused, 2);
  EXPECT_EQ(star.stats.calibrated, 0);
  EXPECT_EQ(pin(moved, "u:Y").transitions[0].load_ff, 14);
  EXPECT_GT(*pin(moved, "z").transitions[0].arrival_ps, *pin(anchor, "z").transitions[0].arrival_ps);
  EXPECT_EQ(proxy.evaluate(&rc).wns_ps, control.wns_ps);
  EXPECT_THROW(proxy.evaluate(nullptr, &star), std::invalid_argument);
}

TEST(LateTiming, DistributedDriverFittingPreservesPhysicalLoadsAndExposesNumericalFailures) {
  Circuit circuit({"a"}, {"z"});
  auto    library = std::make_shared<nldm::Library>(*circuit.library);
  auto&   arc     = library->cells.at("BUF").pins.at("Y").arcs[0];
  for (unsigned t = 0; t < 2; ++t) {
    arc.delay[t] = nldm::Table{
        nldm::Variable::output_load,
        nldm::Variable::none,
        {0, 10},
        {},
        {5, 25}
    };
    arc.slew[t] = nldm::Table{
        nldm::Variable::output_load,
        nldm::Variable::none,
        {0, 10},
        {},
        {20, 40}
    };
  }
  circuit.library = library;
  auto buffer     = circuit.cell("u", "BUF");
  buffer.create_sink_pin("A").connect_driver(circuit.graph->get_input_pin("a"));
  circuit.graph->get_output_pin("z").connect_driver(buffer.create_driver_pin("Y"));
  late::Constraints constraints;
  constraints.inputs["a"]  = {};
  constraints.outputs["z"] = {
      {100, 100},
      {  0,   0}
  };
  constraints.complete = true;
  livehd::physical::Parasitics rc;
  rc.nets = {
      {  "a", {{"a", 0}, {"u:A", 0}},     {{0, 1, 0}}},
      {"u:Y", {{"u:Y", 2}, {"z", 3}}, {{0, 1, 10000}}}
  };
  const auto fitted = late::Proxy(*circuit.graph, library, constraints).evaluate(&rc);
  EXPECT_TRUE(fitted.certified);
  EXPECT_EQ(fitted.driver_fit_failures, 0);
  EXPECT_EQ(fitted.distributed_driver_fits, 2);
  EXPECT_EQ(pin(fitted, "u:Y").transitions[0].load_ff, 5);
  EXPECT_LT(*pin(fitted, "z").transitions[0].arrival_ps, 15 + 30);
  EXPECT_LT(*pin(fitted, "z").transitions[1].arrival_ps, 15 + 30);
  late::Star_context star;
  star.options.calibrate_admittance = true;
  star.policy                       = {
      {1000, .5, 1000, .5},
      1,
      "test"
  };
  star.locations = {
      {  "a",  {0, 0}},
      {"u:A",  {0, 0}},
      {"u:Y",  {0, 0}},
      {  "z", {10, 0}}
  };
  const auto corrected = late::Proxy(*circuit.graph, library, constraints).evaluate(&rc, &star);
  EXPECT_TRUE(corrected.certified);
  EXPECT_EQ(corrected.driver_fit_failures, fitted.driver_fit_failures);
  for (const auto& original : fitted.pins) {
    const auto& current = pin(corrected, original.name);
    for (unsigned t = 0; t < 2; ++t) {
      const auto& a = original.transitions[t];
      const auto& b = current.transitions[t];
      EXPECT_EQ(a.arrival_ps.has_value(), b.arrival_ps.has_value());
      EXPECT_EQ(a.slew_ps.has_value(), b.slew_ps.has_value());
      if (a.arrival_ps && b.arrival_ps) {
        EXPECT_NEAR(*a.arrival_ps, *b.arrival_ps, 1e-8);
      }
      if (a.slew_ps && b.slew_ps) {
        EXPECT_NEAR(*a.slew_ps, *b.slew_ps, 1e-8);
      }
    }
  }
  for (unsigned t = 0; t < 2; ++t) {
    arc.slew[t]->values = {0, 0};
  }
  const auto failed = late::Proxy(*circuit.graph, library, constraints).evaluate(&rc);
  EXPECT_FALSE(failed.certified);
  EXPECT_GT(failed.driver_fit_failures, 0);
  EXPECT_TRUE(issue(failed, "distributed driver fit failed"));
  EXPECT_TRUE(pin(failed, "z").transitions[0].arrival_ps);
  EXPECT_THROW(failed.feedback(), std::invalid_argument);
  const auto boundary
      = late::Proxy(*circuit.graph, library, constraints).evaluate(&rc, nullptr, {.driver = {.characterized_boundary = true}});
  EXPECT_TRUE(boundary.certified);
  EXPECT_TRUE(boundary.characterized_boundary_policy);
  EXPECT_FALSE(failed.characterized_boundary_policy);
  EXPECT_EQ(boundary.driver_fit_failures, 0);
  EXPECT_EQ(boundary.table_boundary_driver_fits, 2);
  EXPECT_EQ(boundary.distributed_driver_fits, 0);
  EXPECT_FALSE(boundary.warnings.empty());
  EXPECT_EQ(boundary.wns_ps, failed.wns_ps);
  EXPECT_EQ(pin(boundary, "z").transitions[0].slew_ps, pin(failed, "z").transitions[0].slew_ps);
  const auto continuous_boundary
      = late::Proxy(*circuit.graph, library, constraints).evaluate(&rc, &star, {.driver = {.characterized_boundary = true}});
  EXPECT_TRUE(continuous_boundary.certified);
  EXPECT_EQ(continuous_boundary.table_boundary_driver_fits, 2);
  EXPECT_EQ(continuous_boundary.wns_ps, boundary.wns_ps);
  constraints.complete = false;
  EXPECT_FALSE(late::Proxy(*circuit.graph, library, constraints)
                   .evaluate(&rc, nullptr, {.driver = {.characterized_boundary = true}})
                   .certified);
}

TEST(LateTiming, ConstantCaseDisablesFalseConditionalArcsButUnknownCasesRemainConservative) {
  Circuit   circuit({"a", "en"}, {"z"});
  auto      library = std::make_shared<nldm::Library>(*circuit.library);
  auto&     cell    = library->cells.at("INV");
  nldm::Pin enable;
  enable.name           = "EN";
  enable.direction      = "input";
  enable.capacitance_ff = {1, 1};
  cell.pins.emplace("EN", enable);
  auto slow                      = cell.pins.at("Y").arcs[0];
  slow.when                      = "EN";
  slow.delay[0]->values          = {100};
  slow.delay[1]->values          = {200};
  cell.pins.at("Y").arcs[0].when = "!EN";
  cell.pins.at("Y").arcs.push_back(slow);
  circuit.library = library;
  auto inv        = circuit.cell("u", "INV");
  inv.create_sink_pin("A").connect_driver(circuit.graph->get_input_pin("a"));
  inv.create_sink_pin("EN").connect_driver(circuit.graph->get_input_pin("en"));
  circuit.graph->get_output_pin("z").connect_driver(inv.create_driver_pin("Y"));
  late::Constraints constraints;
  constraints.inputs["a"]     = {};
  constraints.constants["en"] = false;
  constraints.outputs["z"]    = {
      {1000, 1000},
      {   0,    0}
  };
  constraints.complete = true;
  const auto fixed     = late::Proxy(*circuit.graph, library, constraints).evaluate();
  ASSERT_TRUE(fixed.unsupported.empty()) << fixed.unsupported.front();
  EXPECT_EQ(pin(fixed, "z").transitions[0].arrival_ps, 10);
  EXPECT_EQ(pin(fixed, "z").transitions[1].arrival_ps, 20);
  EXPECT_EQ(pin(fixed, "u:EN").constant, false);
  EXPECT_TRUE(fixed.certified);
  constraints.constants.clear();
  constraints.inputs["en"] = {};
  const auto unknown       = late::Proxy(*circuit.graph, library, constraints).evaluate();
  EXPECT_EQ(pin(unknown, "z").transitions[0].arrival_ps, 100);
  EXPECT_EQ(pin(unknown, "z").transitions[1].arrival_ps, 200);
}
TEST(LateTiming, TieConstantsPropagateWithoutInventingATransitionArrival) {
  Circuit    circuit({}, {"z"});
  auto       library = std::make_shared<nldm::Library>(*circuit.library);
  nldm::Cell tie;
  tie.name = "TIE";
  nldm::Pin output;
  output.name      = "Y";
  output.direction = "output";
  output.function  = "0";
  tie.pins.emplace("Y", output);
  library->cells.emplace("TIE", tie);
  circuit.library = library;
  auto source = circuit.cell("t", "TIE"), inv = circuit.cell("u", "INV");
  inv.create_sink_pin("A").connect_driver(source.create_driver_pin("Y"));
  circuit.graph->get_output_pin("z").connect_driver(inv.create_driver_pin("Y"));
  late::Constraints constraints;
  constraints.outputs["z"] = {
      {100, 100},
      {  0,   0}
  };
  constraints.complete = true;
  const auto result    = late::Proxy(*circuit.graph, library, constraints).evaluate();
  EXPECT_TRUE(result.unsupported.empty());
  EXPECT_EQ(pin(result, "t:Y").constant, false);
  EXPECT_EQ(pin(result, "u:A").constant, false);
  EXPECT_EQ(pin(result, "u:Y").constant, true);
  EXPECT_EQ(pin(result, "z").constant, true);
  EXPECT_FALSE(pin(result, "z").transitions[0].arrival_ps);
  EXPECT_FALSE(result.wns_ps);
  EXPECT_FALSE(result.achievable_period_ps);
}

TEST(LateTiming, OutputRelatedGroupsRetainCharacterizedSerialTiming) {
  Circuit circuit({"a"}, {"z", "carry"});
  auto    library                = std::make_shared<nldm::Library>(*circuit.library);
  auto&   cell                   = library->cells.at("INV");
  auto    carry                  = cell.pins.at("Y");
  carry.name                     = "CON";
  carry.function                 = "A";
  carry.arcs[0].sense            = nldm::Sense::positive;
  carry.arcs[0].delay[0]->values = {100};
  carry.arcs[0].delay[1]->values = {100};
  cell.pins.emplace("CON", carry);
  auto invalid             = cell.pins.at("Y").arcs[0];
  invalid.related_pin      = "CON";
  invalid.delay[0]->values = {200};
  invalid.delay[1]->values = {200};
  cell.pins.at("Y").arcs.push_back(invalid);
  circuit.library = library;
  auto gate       = circuit.cell("adder", "INV");
  gate.create_sink_pin("A").connect_driver(circuit.graph->get_input_pin("a"));
  circuit.graph->get_output_pin("z").connect_driver(gate.create_driver_pin("Y"));
  circuit.graph->get_output_pin("carry").connect_driver(gate.create_driver_pin("CON"));
  late::Constraints constraints;
  constraints.inputs["a"]  = {};
  constraints.outputs["z"] = {
      {1000, 1000},
      {   0,    0}
  };
  constraints.outputs["carry"] = {
      {1000, 1000},
      {   0,    0}
  };
  constraints.complete = true;
  const auto result    = late::Proxy(*circuit.graph, library, constraints).evaluate();
  ASSERT_TRUE(result.unsupported.empty()) << result.unsupported.front();
  EXPECT_TRUE(result.certified);
  ASSERT_EQ(result.warnings.size(), 1);
  EXPECT_EQ(pin(result, "z").transitions[0].arrival_ps, 300);
  EXPECT_EQ(pin(result, "carry").transitions[0].arrival_ps, 100);
}

TEST(LateTiming, UnconnectedRelatedOutputUsesItsDeclarationWithoutChangingTheGraph) {
  Circuit circuit({"a"}, {"z"});
  auto    library                = std::make_shared<nldm::Library>(*circuit.library);
  auto&   cell                   = library->cells.at("INV");
  auto    carry                  = cell.pins.at("Y");
  carry.name                     = "ZCON";
  carry.function                 = "A";
  carry.arcs[0].sense            = nldm::Sense::positive;
  carry.arcs[0].delay[0]->values = {100};
  carry.arcs[0].delay[1]->values = {100};
  cell.pins.emplace("ZCON", carry);
  auto internal             = cell.pins.at("Y").arcs[0];
  internal.related_pin      = "ZCON";
  internal.delay[0]->values = {200};
  internal.delay[1]->values = {200};
  cell.pins.at("Y").arcs.push_back(internal);
  circuit.library = library;
  auto gate       = circuit.cell("adder", "INV");
  gate.create_sink_pin("A").connect_driver(circuit.graph->get_input_pin("a"));
  circuit.graph->get_output_pin("z").connect_driver(gate.create_driver_pin("Y"));
  const auto port = gate.get_subnode_io()->get_output_port_id("ZCON");
  ASSERT_TRUE(gate.try_get_driver_pin(port).is_invalid());
  late::Constraints constraints;
  constraints.inputs["a"]  = {};
  constraints.outputs["z"] = {
      {1000, 1000},
      {   0,    0}
  };
  constraints.complete = true;
  const auto result    = late::Proxy(*circuit.graph, library, constraints).evaluate();
  ASSERT_TRUE(result.unsupported.empty()) << result.unsupported.front();
  EXPECT_EQ(pin(result, "z").transitions[0].arrival_ps, 300);
  EXPECT_TRUE(pin(result, "adder:ZCON").graph_pin.is_invalid());
  EXPECT_EQ(pin(result, "adder:ZCON").graph_node, gate);
  EXPECT_EQ(pin(result, "adder:ZCON").declaration_port, port);
  EXPECT_TRUE(gate.try_get_driver_pin(port).is_invalid());
}

TEST(LateTiming, CaseAnalysisRestrictsNonUnateArcsToTheirActualPolarity) {
  Circuit circuit({"a", "b"}, {"z"});
  auto    gate = circuit.cell("x", "XOR");
  gate.create_sink_pin("A").connect_driver(circuit.graph->get_input_pin("a"));
  gate.create_sink_pin("B").connect_driver(circuit.graph->get_input_pin("b"));
  circuit.graph->get_output_pin("z").connect_driver(gate.create_driver_pin("Y"));
  late::Constraints constraints;
  constraints.inputs["a"] = {
      {1, 20},
      {1,  1}
  };
  constraints.constants["b"] = false;
  constraints.outputs["z"]   = {
      {100, 100},
      {  0,   0}
  };
  constraints.complete = true;
  const auto result    = late::Proxy(*circuit.graph, circuit.library, constraints).evaluate();
  ASSERT_TRUE(result.unsupported.empty()) << result.unsupported.front();
  EXPECT_EQ(pin(result, "z").transitions[0].arrival_ps, 6);
  EXPECT_EQ(pin(result, "z").transitions[1].arrival_ps, 27);
}

TEST(LateTiming, SensitizedConditionOverridesTheUnconditionalDefault) {
  Circuit   circuit({"a", "en"}, {"z"});
  auto      library = std::make_shared<nldm::Library>(*circuit.library);
  auto&     cell    = library->cells.at("INV");
  nldm::Pin enable;
  enable.name           = "EN";
  enable.direction      = "input";
  enable.capacitance_ff = {1, 1};
  cell.pins.emplace("EN", enable);
  auto conditional                           = cell.pins.at("Y").arcs[0];
  conditional.when                           = "EN";
  cell.pins.at("Y").arcs[0].delay[0]->values = {1000};
  cell.pins.at("Y").arcs[0].delay[1]->values = {2000};
  cell.pins.at("Y").arcs.push_back(conditional);
  circuit.library = library;
  auto inv        = circuit.cell("u", "INV");
  inv.create_sink_pin("A").connect_driver(circuit.graph->get_input_pin("a"));
  inv.create_sink_pin("EN").connect_driver(circuit.graph->get_input_pin("en"));
  circuit.graph->get_output_pin("z").connect_driver(inv.create_driver_pin("Y"));
  late::Constraints constraints;
  constraints.inputs["a"]     = {};
  constraints.constants["en"] = true;
  constraints.outputs["z"]    = {
      {100, 100},
      {  0,   0}
  };
  const auto result = late::Proxy(*circuit.graph, library, constraints).evaluate();
  EXPECT_EQ(pin(result, "z").transitions[0].arrival_ps, 10);
  EXPECT_EQ(pin(result, "z").transitions[1].arrival_ps, 20);
}

TEST(LateTiming, UncompiledSetupConditionCannotCertifyEitherClockModel) {
  // An unknown `when` variable cannot be evaluated; the check stays
  // pessimistically active, but neither timing path may certify it.
  for (const bool named : {false, true}) {
    Circuit circuit({"clk"}, {});
    auto    library                                   = std::make_shared<nldm::Library>(*circuit.library);
    library->cells.at("FF").pins.at("D").arcs[0].when = "SE";
    circuit.library                                   = library;
    auto flop = circuit.cell("ff", "FF"), inv = circuit.cell("inv", "INV");
    flop.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("clk"));
    inv.create_sink_pin("A").connect_driver(flop.create_driver_pin("Q"));
    flop.create_sink_pin("D").connect_driver(inv.create_driver_pin("Y"));
    late::Constraints       constraints;
    const late::Ideal_clock clock{.period_ps = 100, .roots = {"clk"}};
    if (named) {
      constraints.clocks["main"] = clock;
    } else {
      constraints.clock = clock;
    }
    constraints.complete = true;
    const auto result    = late::Proxy(*circuit.graph, library, constraints).evaluate();
    EXPECT_FALSE(result.certified) << "named clocks: " << named;
    EXPECT_TRUE(issue(result, "unsupported conditional setup expression: ff:D")) << "named clocks: " << named;
  }
}

TEST(LateTiming, InputDrivingCellAddsOnlyLoadDependentDelayToTheWire) {
  Circuit circuit({"a"}, {"z"});
  circuit.library = std::make_shared<nldm::Library>(nldm::parse(models + R"(
    library(drive){time_unit:"1ps";capacitive_load_unit(1,ff);
      lu_table_template(load){variable_1:total_output_net_capacitance;index_1("0,10");}
      cell(EXTERNAL){pin(A){direction:input;capacitance:1;}
        pin(Y){direction:output;function:A;timing(){related_pin:A;timing_sense:positive_unate;
          cell_rise(load){values("10,30");}cell_fall(load){values("20,50");}
          rise_transition(load){values("1,11");}fall_transition(load){values("2,22");}
        }}
      }
    })"));
  auto buffer     = circuit.cell("u", "BUF");
  buffer.create_sink_pin("A").connect_driver(circuit.graph->get_input_pin("a"));
  circuit.graph->get_output_pin("z").connect_driver(buffer.create_driver_pin("Y"));
  late::Constraints constraints;
  constraints.inputs["a"] = {
      {5, 6},
      {0, 0},
      late::Driving_cell{"EXTERNAL", "Y", "A", {0, 0}}
  };
  constraints.outputs["z"] = {
      {100, 100},
      {  0,   0}
  };
  constraints.complete = true;
  const auto result    = late::Proxy(*circuit.graph, circuit.library, constraints).evaluate();
  ASSERT_TRUE(result.unsupported.empty()) << result.unsupported.front();
  EXPECT_EQ(pin(result, "a").transitions[0].arrival_ps, 5);
  EXPECT_EQ(pin(result, "a").transitions[1].arrival_ps, 6);
  EXPECT_EQ(pin(result, "a").transitions[0].slew_ps, 2);
  EXPECT_EQ(pin(result, "a").transitions[1].slew_ps, 4);
  EXPECT_EQ(pin(result, "u:A").transitions[0].arrival_ps, 7);
  EXPECT_EQ(pin(result, "u:A").transitions[1].arrival_ps, 9);
  EXPECT_EQ(pin(result, "z").transitions[0].arrival_ps, 12);
  EXPECT_EQ(pin(result, "z").transitions[1].arrival_ps, 16);
  constraints.inputs["a"].driving_cell->cell = "MISSING";
  const auto missing                         = late::Proxy(*circuit.graph, circuit.library, constraints).evaluate();
  EXPECT_FALSE(missing.certified);
  EXPECT_TRUE(issue(missing, "uncharacterized input driving cell"));
  EXPECT_FALSE(pin(missing, "z").transitions[0].arrival_ps);
}

TEST(LateTiming, BoundedDriverPolicyCountsItsModelsAndPreservesDefaultObservation) {
  Circuit circuit({"a"}, {"z"});
  auto    library       = std::make_shared<nldm::Library>(*circuit.library);
  auto&   cell          = library->cells.at("BUF");
  cell.thresholds.lower = {.1, .1};
  cell.thresholds.upper = {.9, .9};
  auto& arc             = cell.pins.at("Y").arcs[0];
  for (unsigned t = 0; t < 2; ++t) {
    arc.delay[t] = nldm::Table{
        nldm::Variable::output_load,
        nldm::Variable::none,
        {0, 10},
        {},
        {10, 60}
    };
    arc.slew[t] = nldm::Table{nldm::Variable::none, nldm::Variable::none, {}, {}, {20.6}};
  }
  circuit.library = library;
  auto buffer     = circuit.cell("u", "BUF");
  buffer.create_sink_pin("A").connect_driver(circuit.graph->get_input_pin("a"));
  circuit.graph->get_output_pin("z").connect_driver(buffer.create_driver_pin("Y"));
  late::Constraints constraints;
  constraints.inputs["a"]  = {};
  constraints.outputs["z"] = {
      {100, 100},
      {  0,   0}
  };
  constraints.complete = true;
  livehd::physical::Parasitics rc;
  rc.nets = {
      {  "a",    {{"a", 0}, {"u:A", 0}},  {{0, 1, 0}}},
      {"u:Y", {{"u:Y", .8}, {"z", 4.2}}, {{0, 1, 20}}}
  };
  late::Proxy proxy(*circuit.graph, library, constraints);
  const auto  original = proxy.evaluate(&rc);
  const auto  bounded  = proxy.evaluate(&rc, nullptr, {.driver = {.bounded_boundary = true}});
  EXPECT_FALSE(original.bounded_boundary_policy);
  EXPECT_TRUE(bounded.bounded_boundary_policy);
  EXPECT_FALSE(bounded.characterized_boundary_policy);
  EXPECT_TRUE(bounded.certified);
  EXPECT_EQ(bounded.crossing_boundary_driver_fits, 2);
  EXPECT_EQ(bounded.full_load_boundary_driver_fits, 0);
  EXPECT_EQ(bounded.driver_fit_failures, 0);
  EXPECT_EQ(bounded.distributed_driver_fits, 0);
  EXPECT_EQ(pin(bounded, "u:Y").transitions[0].slew_ps, 20.6);
  // The top-level receiver retains the library's 20%-80% thresholds.
  EXPECT_NEAR(*pin(bounded, "z").transitions[0].slew_ps, 20.6 * .6 / .8, 1e-12);
  EXPECT_GT(*pin(original, "z").transitions[0].slew_ps, 20.6);
  EXPECT_EQ(proxy.evaluate(&rc).wns_ps, original.wns_ps);
  EXPECT_EQ(proxy.evaluate(&rc).crossing_boundary_driver_fits, 0);
}

namespace {
// Bit-exact characterization of both clock models: every scenario runs with
// the legacy single clock and with the same clock named, so a shared timing
// implementation cannot silently change either model's observable Result.
// Graph capture must only append the graph section, so the golden image keeps
// the capture run. After an intended timing change, regenerate it with:
//   bazel test -c opt //pass/place:late_timing_test; cp
//   bazel-testlogs/pass/place/late_timing_test/test.outputs/late_timing_characterization.txt pass/place/tests/
struct Characterization {
  std::string text;
  void        add(const std::string& name, const hhds::Graph& graph, std::shared_ptr<const nldm::Library> library,
                  const late::Constraints& legacy, const livehd::physical::Parasitics* rc = nullptr,
                  const late::Star_context* star = nullptr) {
    auto named = legacy;
    if (legacy.clock) {
      named.clocks["main"] = *legacy.clock;
      named.clock.reset();
      for (auto& [port, input] : named.inputs) {
        (void)port;
        if (input.clock == "default") {
          input.clock = "main";
        }
      }
      for (auto& [port, output] : named.outputs) {
        (void)port;
        if (output.clock == "default") {
          output.clock = "main";
        }
      }
    }
    for (const late::Constraints* constraints : std::array<const late::Constraints*, 2>{&legacy, &named}) {
      const auto run = [&](bool capture) -> std::string {
        try {
          auto copy = star ? std::optional<late::Star_context>(*star) : std::nullopt;
          return livehd::placement::test_fixture::late_timing_dump(
              late::Proxy(graph, library, *constraints).evaluate(rc, copy ? &*copy : nullptr, {.capture_feasible_graph = capture}));
        } catch (const std::exception& error) {
          return std::string("throws ") + error.what() + "\n";
        }
      };
      const auto label    = name + (constraints == &legacy ? " legacy" : " named");
      const auto captured = run(true);
      const auto graph_at = captured.find("\ngraph reference ");
      EXPECT_EQ(graph_at == std::string::npos ? captured : captured.substr(0, graph_at + 1), run(false))
          << label << ": graph capture changed more than the graph section";
      text += "### " + label + "\n" + captured;
      if (!legacy.clock) {
        break;
      }
    }
  }
};
late::Constraints clocked() {
  late::Constraints c;
  c.clock = late::Ideal_clock{
      .period_ps      = 100,
      .uncertainty_ps = 3,
      .slew_ps        = {2, 4},
      .roots          = {"clk"}
  };
  c.complete = true;
  return c;
}
// Two cones through a HALF cell characterized for rising outputs only. Cone
// a -> u -> z is timed; input b is cut, so cone b -> v -> w is never reached.
// With reached=false, u is a complete BUF and the cut cone also ends at a
// cut output, so only the unreached HALF lacks a fall delay.
struct Half_circuit {
  Circuit circuit{
      {"clk", "a", "b"},
      {"z", "w"}
  };
  late::Constraints constraints = clocked();
  explicit Half_circuit(bool reached) {
    circuit.library = std::make_shared<nldm::Library>(nldm::parse(models + R"(
      library(half){time_unit:"1ps";capacitive_load_unit(1,ff);default_input_pin_cap:1;
        cell(HALF){pin(A){direction:input;}pin(Y){direction:output;function:A;timing(){related_pin:A;timing_sense:positive_unate;
          cell_rise(scalar){values("5");}rise_transition(scalar){values("1");}}}}
      })"));
    auto u = circuit.cell("u", reached ? "HALF" : "BUF"), v = circuit.cell("v", "HALF");
    u.create_sink_pin("A").connect_driver(circuit.graph->get_input_pin("a"));
    v.create_sink_pin("A").connect_driver(circuit.graph->get_input_pin("b"));
    circuit.graph->get_output_pin("z").connect_driver(u.create_driver_pin("Y"));
    circuit.graph->get_output_pin("w").connect_driver(v.create_driver_pin("Y"));
    constraints.inputs["a"] = {
        {3, 4},
        {1, 1},
        std::nullopt,
        "default"
    };
    constraints.outputs["z"] = {
        {50, 50},
        {},
        "default"
    };
    constraints.cuts.insert("b");
    if (reached) {
      constraints.outputs["w"] = {
          {50, 50},
          {},
          "default"
      };
    } else {
      constraints.cuts.insert("w");
    }
  }
};
}  // namespace
TEST(LateTiming, MissingCellDelaysFollowEachClockModel) {
  const auto evaluate = [](const Half_circuit& half, bool named) {
    auto constraints = half.constraints;
    if (named) {
      constraints.clocks["main"] = *constraints.clock;
      constraints.clock.reset();
      constraints.inputs["a"].clock  = "main";
      constraints.outputs["z"].clock = "main";
      if (constraints.outputs.contains("w")) {
        constraints.outputs["w"].clock = "main";
      }
    }
    return late::Proxy(*half.circuit.graph, half.circuit.library, constraints).evaluate();
  };
  // Legacy reports a missing delay only where an arrival reaches it; named
  // clocks report every sensitized arc that lacks one.
  const Half_circuit both(true);
  const auto         legacy = evaluate(both, false), named = evaluate(both, true);
  EXPECT_TRUE(issue(legacy, "missing cell delay/transition table: u:Y"));
  EXPECT_FALSE(issue(legacy, "v:Y"));
  EXPECT_TRUE(issue(named, "missing cell delay/transition table or input waveform: u:Y"));
  EXPECT_TRUE(issue(named, "missing cell delay/transition table or input waveform: v:Y"));
  // So an unreached incomplete cell keeps only the legacy result certified.
  const Half_circuit cut_only(false);
  const auto         legacy_cut = evaluate(cut_only, false), named_cut = evaluate(cut_only, true);
  EXPECT_TRUE(legacy_cut.certified) << (legacy_cut.unsupported.empty() ? "" : legacy_cut.unsupported.front());
  EXPECT_FALSE(named_cut.certified);
  EXPECT_TRUE(issue(named_cut, "missing cell delay/transition table or input waveform: v:Y"));
}
TEST(LateTiming, ACutInputConeDoesNotUncertifyEitherClockModel) {
  // A cut input feeds complete cells into a cut output: no waveform or
  // arrival reaches them, so the register loop is the whole measured subset.
  for (const bool named : {false, true}) {
    Circuit circuit({"clk", "a"}, {"w"});
    auto    flop = circuit.cell("ff", "FF"), inv = circuit.cell("inv", "INV");
    auto    u = circuit.cell("u", "BUF"), v = circuit.cell("v", "INV");
    flop.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("clk"));
    inv.create_sink_pin("A").connect_driver(flop.create_driver_pin("Q"));
    flop.create_sink_pin("D").connect_driver(inv.create_driver_pin("Y"));
    u.create_sink_pin("A").connect_driver(circuit.graph->get_input_pin("a"));
    v.create_sink_pin("A").connect_driver(u.create_driver_pin("Y"));
    circuit.graph->get_output_pin("w").connect_driver(v.create_driver_pin("Y"));
    late::Constraints       constraints;
    const late::Ideal_clock clock{.period_ps = 100, .roots = {"clk"}};
    if (named) {
      constraints.clocks["main"] = clock;
    } else {
      constraints.clock = clock;
    }
    constraints.cuts     = {"a", "w"};
    constraints.complete = true;
    const auto result    = late::Proxy(*circuit.graph, circuit.library, constraints).evaluate();
    ASSERT_TRUE(result.certified) << "named clocks: " << named << ": "
                                  << (result.unsupported.empty() ? "" : result.unsupported.front());
    EXPECT_EQ(result.achievable_period_ps, 38) << "named clocks: " << named;
    EXPECT_FALSE(pin(result, "v:Y").transitions[0].arrival_ps);
  }
}
TEST(LateTiming, CharacterizationOfBothClockModelsIsBitExact) {
  Characterization image;
  {
    // Register feedback loop.
    Circuit circuit({"clk"}, {});
    auto    flop = circuit.cell("ff", "FF"), inv = circuit.cell("inv", "INV");
    flop.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("clk"));
    inv.create_sink_pin("A").connect_driver(flop.create_driver_pin("Q"));
    flop.create_sink_pin("D").connect_driver(inv.create_driver_pin("Y"));
    image.add("register_loop", *circuit.graph, circuit.library, clocked());
  }
  for (const auto& [label, input_clock, z_clock, w_clock] :
       std::vector<std::tuple<std::string, std::string, std::string, std::optional<std::string>>>{
           {            "pipeline_absolute",        "",        "",           ""},
           {             "pipeline_clocked", "default", "default",    "default"},
           {               "pipeline_mixed", "default", "default",           ""},
           {"pipeline_unconstrained_output", "default", "default", std::nullopt}
  }) {
    // Input -> flop -> output with absolute, clocked and missing requirements.
    Circuit circuit({"clk", "a"}, {"z", "w"});
    auto    inv = circuit.cell("inv", "INV"), flop = circuit.cell("ff", "FF"), buf = circuit.cell("buf", "BUF");
    inv.create_sink_pin("A").connect_driver(circuit.graph->get_input_pin("a"));
    flop.create_sink_pin("D").connect_driver(inv.create_driver_pin("Y"));
    flop.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("clk"));
    buf.create_sink_pin("A").connect_driver(flop.create_driver_pin("Q"));
    circuit.graph->get_output_pin("z").connect_driver(buf.create_driver_pin("Y"));
    circuit.graph->get_output_pin("w").connect_driver(inv.create_driver_pin("Y"));
    auto c                   = clocked();
    c.inputs["a"].arrival_ps = {7, 9};
    c.inputs["a"].slew_ps    = {1, 2};
    c.inputs["a"].clock      = input_clock;
    c.outputs["z"]           = {
        {25, 27},
        { 1,  2},
        z_clock
    };
    if (w_clock) {
      c.outputs["w"] = {
          {60, 61},
          { 3,  3},
          *w_clock
      };
    }
    image.add(label, *circuit.graph, circuit.library, c);
  }
  {
    // Reconvergent non-unate paths around a register (objective fixture).
    Circuit circuit({"clk"}, {"z"});
    auto    ff = circuit.cell("ff", "FF"), b = circuit.cell("b", "BUF"), x = circuit.cell("x", "XOR");
    ff.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("clk"));
    b.create_sink_pin("A").connect_driver(ff.create_driver_pin("Q"));
    x.create_sink_pin("A").connect_driver(ff.create_driver_pin("Q"));
    x.create_sink_pin("B").connect_driver(b.create_driver_pin("Y"));
    ff.create_sink_pin("D").connect_driver(x.create_driver_pin("Y"));
    circuit.graph->get_output_pin("z").connect_driver(x.create_driver_pin("Y"));
    auto c         = clocked();
    c.outputs["z"] = {
        {100, 100},
        {},
        "default"
    };
    image.add("reconvergent", *circuit.graph, circuit.library, c);
  }
  {
    // Inverter chain with RC parasitics, a calibrated star and an unused clock root.
    Circuit circuit({"clk", "a"}, {"z"});
    auto    u = circuit.cell("u", "INV"), v = circuit.cell("v", "INV");
    u.create_sink_pin("A").connect_driver(circuit.graph->get_input_pin("a"));
    v.create_sink_pin("A").connect_driver(u.create_driver_pin("Y"));
    circuit.graph->get_output_pin("z").connect_driver(v.create_driver_pin("Y"));
    auto c        = clocked();
    c.inputs["a"] = {
        {3, 4},
        {5, 6},
        std::nullopt,
        "default"
    };
    c.outputs["z"] = {
        {90, 95},
        { 5,  7},
        "default"
    };
    livehd::physical::Parasitics rc;
    rc.nets = {
        {  "a",    {{"a", .2}, {"u:A", .4}}, {{0, 1, 1000}}},
        {"u:Y", {{"u:Y", .5}, {"v:A", 1.5}}, {{0, 1, 2000}}},
        {"v:Y",      {{"v:Y", 0}, {"z", 3}},  {{0, 1, 500}}}
    };
    image.add("rc_chain", *circuit.graph, circuit.library, c, &rc);
    late::Star_context star;
    star.policy = {
        {100, 1, 100, 1},
        1,
        "test"
    };
    star.locations = {
        {  "a",  {0, 0}},
        {"u:A",  {1, 0}},
        {"u:Y",  {2, 0}},
        {"v:A",  {6, 0}},
        {"v:Y",  {7, 0}},
        {  "z", {10, 0}}
    };
    image.add("rc_chain_star", *circuit.graph, circuit.library, c, &rc, &star);
  }
  {
    // Case analysis, sensitization and an uncompiled setup condition.
    Circuit circuit({"clk", "a", "en"}, {"z"});
    auto    library                                   = std::make_shared<nldm::Library>(*circuit.library);
    library->cells.at("FF").pins.at("D").arcs[0].when = "SE";
    circuit.library                                   = library;
    auto x = circuit.cell("x", "XOR"), flop = circuit.cell("ff", "FF");
    x.create_sink_pin("A").connect_driver(circuit.graph->get_input_pin("a"));
    x.create_sink_pin("B").connect_driver(circuit.graph->get_input_pin("en"));
    flop.create_sink_pin("D").connect_driver(x.create_driver_pin("Y"));
    flop.create_sink_pin("CLK").connect_driver(circuit.graph->get_input_pin("clk"));
    circuit.graph->get_output_pin("z").connect_driver(flop.create_driver_pin("Q"));
    auto c            = clocked();
    c.inputs["a"]     = {};
    c.constants["en"] = true;
    c.outputs["z"]    = {
        {50, 50},
        {},
        "default"
    };
    image.add("constant_xor_condition", *circuit.graph, library, c);
  }
  {
    // Input driving cell in an unclocked environment.
    Circuit circuit({"a"}, {"z"});
    circuit.library = std::make_shared<nldm::Library>(nldm::parse(models + R"(
      library(drive){time_unit:"1ps";capacitive_load_unit(1,ff);
        lu_table_template(load){variable_1:total_output_net_capacitance;index_1("0,10");}
        cell(EXTERNAL){pin(A){direction:input;capacitance:1;}
          pin(Y){direction:output;function:A;timing(){related_pin:A;timing_sense:positive_unate;
            cell_rise(load){values("10,30");}cell_fall(load){values("20,50");}
            rise_transition(load){values("1,11");}fall_transition(load){values("2,22");}
          }}
        }
      })"));
    auto buffer     = circuit.cell("u", "BUF");
    buffer.create_sink_pin("A").connect_driver(circuit.graph->get_input_pin("a"));
    circuit.graph->get_output_pin("z").connect_driver(buffer.create_driver_pin("Y"));
    late::Constraints c;
    c.inputs["a"] = {
        {5, 6},
        {0, 0},
        late::Driving_cell{"EXTERNAL", "Y", "A", {0, 0}}
    };
    c.outputs["z"] = {
        {100, 100},
        {  0,   0}
    };
    c.complete = true;
    image.add("driving_cell_unclocked", *circuit.graph, circuit.library, c);
  }
  for (const bool reached : {true, false}) {
    Half_circuit half(reached);
    image.add(reached ? "missing_delay_reached_and_cut" : "missing_delay_cut_cone_only",
              *half.circuit.graph,
              half.circuit.library,
              half.constraints);
  }
  const std::string golden_path = "pass/place/tests/late_timing_characterization.txt";
  std::ifstream     golden(golden_path);
  ASSERT_TRUE(golden) << "missing characterization golden " << golden_path;
  const std::string expected((std::istreambuf_iterator<char>(golden)), {});
  if (expected != image.text) {
    if (const char* dir = std::getenv("TEST_UNDECLARED_OUTPUTS_DIR")) {
      std::ofstream(std::string(dir) + "/late_timing_characterization.txt") << image.text;
    }
    size_t line = 1, at = 0;
    while (at < expected.size() && at < image.text.size() && expected[at] == image.text[at]) {
      line += expected[at++] == '\n';
    }
    FAIL() << "late timing characterization differs from " << golden_path << " at line " << line;
  }
}
