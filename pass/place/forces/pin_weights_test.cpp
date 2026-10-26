// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pin_weights.hpp"

#include <cmath>
#include <stdexcept>

#include "gtest/gtest.h"
#include "node_util.hpp"

namespace tc   = livehd::placement::timeclosure;
namespace late = livehd::placement::late;
namespace {
late::Result fanout(double a = 6000, double b = 5800) {
  late::Result r;
  r.certified            = true;
  r.wns_ps               = 1000 - std::max(a, b);
  r.achievable_period_ps = std::max(a, b);
  r.pins.resize(3);
  r.pins[0].name     = "driver";
  r.pins[1].name     = "critical";
  r.pins[2].name     = "side";
  r.pins[1].endpoint = r.pins[2].endpoint = true;
  r.pins[0].transitions[0].arrival_ps     = 0;
  for (size_t i = 1; i < 3; ++i) {
    r.pins[i].transitions[0].arrival_ps  = i == 1 ? a : b;
    r.pins[i].transitions[0].required_ps = 1000;
    r.pins[i].transitions[0].predecessor = late::Predecessor{0, 0};
  }
  r.feasible_graph.emplace();
  auto& g               = *r.feasible_graph;
  g.reference_period_ps = 1000;
  g.launch_domains      = {std::nullopt};
  g.vertices            = {
      {0, 0, 0,            0, std::nullopt},
      {1, 0, 0, std::nullopt,         1000},
      {2, 0, 0, std::nullopt,         1000}
  };
  g.arcs = {
      {0, 1, 0, a, true},
      {0, 2, 1, b, true}
  };
  return r;
}
late::Result cell_path(double scale = 1) {
  late::Result r;
  r.certified            = true;
  r.wns_ps               = 38 * scale;
  r.achievable_period_ps = 62 * scale;
  r.pins.resize(4);
  const std::vector<std::string> names{"source", "u:A", "u:Y", "out"};
  const std::vector<double>      arrivals{0, 10, 60, 62};
  for (size_t i = 0; i < 4; ++i) {
    r.pins[i].name                      = names[i];
    r.pins[i].transitions[0].arrival_ps = arrivals[i] * scale;
    if (i) {
      r.pins[i].transitions[0].predecessor = late::Predecessor{i - 1, 0};
    }
  }
  r.pins[3].endpoint                   = true;
  r.pins[3].transitions[0].required_ps = 100 * scale;
  r.feasible_graph.emplace();
  auto& g               = *r.feasible_graph;
  g.reference_period_ps = 100 * scale;
  g.launch_domains      = {std::nullopt};
  g.vertices            = {
      {0, 0, 0,            0, std::nullopt},
      {1, 0, 0, std::nullopt, std::nullopt},
      {2, 0, 0, std::nullopt, std::nullopt},
      {3, 0, 0, std::nullopt,  100 * scale}
  };
  g.arcs = {
      {0, 1, 0, 10 * scale,  true},
      {1, 2, 1, 50 * scale, false},
      {2, 3, 2,  2 * scale,  true}
  };
  return r;
}
struct Output_chain {
  hhds::GraphLibrary           owner;
  std::shared_ptr<hhds::Graph> graph;
  hhds::Node_class             cell;
  Output_chain() {
    auto top = owner.create_io("output_chain");
    graph    = top->create_graph();
    auto io  = owner.create_io("MULTI");
    io->add_input("A", 0);
    for (const auto& name : {"CON", "MID", "SN"}) {
      io->add_output(name, io->get_output_pin_decls().size());
      io->set_bits(name, 1);
    }
    io->set_bits("A", 1);
    cell = livehd::graph_util::create_typed_node(*graph, Ntype_op::Sub);
    cell.set_name("u");
    cell.set_subnode(io);
  }
  late::Result observation(double scale = 1) const {
    late::Result r;
    r.certified            = true;
    r.wns_ps               = 23 * scale;
    r.achievable_period_ps = 77 * scale;
    const std::vector<std::string> names{"source", "u:A", "u:CON", "u:MID", "u:SN", "out", "side"};
    const std::vector<double>      arrivals{0, 10, 50, 45, 75, 77, 75};
    r.pins.resize(names.size());
    r.feasible_graph.emplace();
    auto& g               = *r.feasible_graph;
    g.reference_period_ps = 100 * scale;
    g.launch_domains      = {std::nullopt};
    for (size_t i = 0; i < names.size(); ++i) {
      r.pins[i].name                      = names[i];
      r.pins[i].transitions[0].arrival_ps = arrivals[i] * scale;
      if (i >= 1 && i <= 4) {
        r.pins[i].graph_node       = cell;
        r.pins[i].declaration_port = i == 1 ? 0 : i - 2;
      }
      if (i) {
        r.pins[i].transitions[0].predecessor = late::Predecessor{i == 6 ? 0 : i - 1, 0};
      }
      if (i >= 5) {
        r.pins[i].endpoint                   = true;
        r.pins[i].transitions[0].required_ps = 100 * scale;
      }
      g.vertices.push_back(
          {i, 0, 0, i == 0 ? std::optional(0.) : std::nullopt, i >= 5 ? std::optional(100 * scale) : std::nullopt});
    }
    g.arcs = {
        {0, 1, 0, 10 * scale,  true},
        {1, 2, 1, 40 * scale, false},
        {2, 3, 2, -5 * scale, false},
        {3, 4, 3, 30 * scale, false},
        {4, 5, 4,  2 * scale,  true},
        {0, 6, 5, 75 * scale,  true}
    };
    return r;
  }
};
}  // namespace
TEST(PinWeights, RankedDelayAndGraphMassProduceSelectivePhysicalSinkWeights) {
  tc::Pin_weight_bank bank({.blend_base = 1});
  const auto          r = bank.update(fanout());
  ASSERT_EQ(r.pins.size(), 2);
  const auto& critical = r.pins.at("critical");
  const auto& side     = r.pins.at("side");
  EXPECT_EQ(critical.driver, "driver");
  EXPECT_EQ(critical.graph, 1);
  EXPECT_EQ(critical.path, 1);
  EXPECT_EQ(critical.weight, 1.5);
  EXPECT_NEAR(side.path, 5800. / 4 / 6000, 1e-12);
  EXPECT_NEAR(side.graph, std::exp(-200. / 120), 1e-12);
  EXPECT_NEAR(side.weight, .5 * side.graph + side.path, 1e-12);
  EXPECT_EQ(r.critical_endpoints, 2);
  const auto swapped = bank.update(fanout(5800, 6000));
  EXPECT_GT(swapped.pins.at("side").weight, swapped.pins.at("critical").weight);
}
TEST(PinWeights, CellArcDelayIsAttributedToInfluencingInputRatherThanOutput) {
  tc::Pin_weight_bank bank({.blend_base = 1});
  const auto          r = bank.update(cell_path());
  ASSERT_EQ(r.pins.size(), 2);
  EXPECT_EQ(r.pins.at("u:A").driver, "source");
  EXPECT_EQ(r.pins.at("out").driver, "u:Y");
  EXPECT_EQ(r.pins.at("u:A").path, 1);
  EXPECT_NEAR(r.pins.at("out").path, 2. / 60, 1e-12);
  EXPECT_FALSE(r.pins.contains("u:Y"));
  EXPECT_FALSE(r.pins.contains("source"));
}
TEST(PinWeights, RelatedOutputChainPreservesEveryPositiveArcChargeAtThePhysicalInput) {
  Output_chain        c;
  tc::Pin_weight_bank ps({.blend_base = 1}), ns({.blend_base = 1});
  const auto          result = ps.update(c.observation());
  const auto          scaled = ns.update(c.observation(.001));
  ASSERT_EQ(result.pins.size(), 3);
  EXPECT_EQ(result.related_output_path_arcs, 1);
  EXPECT_EQ(result.pins.at("u:A").driver, "source");
  // A receives wire 10 + cell 40 + max(-5,0) + cell 30 = 80.
  // The second endpoint contributes its independent 75 / 2^2 = 18.75.
  EXPECT_EQ(result.pins.at("u:A").path, 1);
  EXPECT_NEAR(result.pins.at("out").path, 2. / 80, 1e-12);
  EXPECT_NEAR(result.pins.at("side").path, 18.75 / 80, 1e-12);
  EXPECT_EQ(result.pins.at("out").driver, "u:SN");
  for (const auto& [name, weight] : result.pins) {
    EXPECT_NEAR(scaled.pins.at(name).path, weight.path, 1e-12);
    EXPECT_NEAR(scaled.pins.at(name).graph, weight.graph, 1e-12);
    EXPECT_NEAR(scaled.pins.at(name).weight, weight.weight, 1e-12);
  }
  EXPECT_FALSE(result.pins.contains("u:CON"));
  EXPECT_FALSE(result.pins.contains("u:MID"));
  EXPECT_FALSE(result.pins.contains("u:SN"));
}
TEST(PinWeights, UnresolvedOrMisboundOutputContributionsRejectWithoutChangingHistory) {
  Output_chain        c, other;
  tc::Pin_weight_bank bank;
  bank.update(fanout());
  auto control               = bank;
  auto invalid               = c.observation();
  invalid.pins[3].graph_node = other.cell;
  EXPECT_THROW(bank.update(invalid), std::invalid_argument);
  invalid                    = c.observation();
  invalid.pins[3].graph_node = {};
  EXPECT_THROW(bank.update(invalid), std::invalid_argument);
  invalid              = c.observation();
  // Output/input port numbers may overlap: a port id alone is insufficient.
  invalid.pins[2].name = "u:A";
  EXPECT_THROW(bank.update(invalid), std::invalid_argument);
  invalid               = c.observation();
  invalid.pins[2].clock = true;
  EXPECT_THROW(bank.update(invalid), std::invalid_argument);
  invalid                              = c.observation();
  invalid.feasible_graph->arcs[1].wire = true;
  EXPECT_THROW(bank.update(invalid), std::invalid_argument);
  invalid = c.observation();
  invalid.pins[2].transitions[0].predecessor.reset();
  invalid.feasible_graph->arcs.erase(invalid.feasible_graph->arcs.begin() + 1);
  invalid.feasible_graph->vertices[2].boundary_arrival_ps = 50;
  EXPECT_THROW(bank.update(invalid), std::invalid_argument);
  const auto after = bank.update(fanout()), expected = control.update(fanout());
  EXPECT_EQ(after.pins.at("side").weight, expected.pins.at("side").weight);
}
TEST(PinWeights, NativeUnconnectedRelatedOutputUsesItsDeclaredOwnerWithoutCreatingGraphPins) {
  hhds::GraphLibrary owner;
  auto               io = owner.create_io("native_output_chain");
  io->add_input("a", 0);
  io->add_input("clk", 1);
  io->add_output("z", 0);
  for (const auto& name : {"a", "clk", "z"}) {
    io->set_bits(name, 1);
  }
  auto graph  = io->create_graph();
  auto master = owner.create_io("MULTI");
  master->add_input("A", 1);
  master->add_output("CON", 2);
  master->add_output("SN", 3);
  for (const auto& name : {"A", "CON", "SN"}) {
    master->set_bits(name, 1);
  }
  auto cell = livehd::graph_util::create_typed_node(*graph, Ntype_op::Sub);
  cell.set_name("u");
  cell.set_subnode(master);
  cell.create_sink_pin("A").connect_driver(graph->get_input_pin("a"));
  graph->get_output_pin("z").connect_driver(cell.create_driver_pin("SN"));
  auto              models = std::make_shared<livehd::liberty::timing::Library>(livehd::liberty::timing::parse(R"(
library(test){time_unit:"1ps";capacitive_load_unit(1,ff);default_input_pin_cap:1;
cell(MULTI){pin(A){direction:input;}
pin(CON){direction:output;function:"!A";timing(){related_pin:A;timing_sense:negative_unate;
cell_rise(scalar){values("40");}cell_fall(scalar){values("50");}
rise_transition(scalar){values("1");}fall_transition(scalar){values("1");}}}
pin(SN){direction:output;function:A;timing(){related_pin:CON;timing_sense:negative_unate;
cell_rise(scalar){values("30");}cell_fall(scalar){values("20");}
rise_transition(scalar){values("1");}fall_transition(scalar){values("1");}}}}})"));
  late::Constraints constraints;
  constraints.complete       = true;
  constraints.clocks["main"] = {.period_ps = 100, .roots = {"clk"}};
  constraints.inputs["a"]    = {
      {10, 12},
      { 1,  1},
      std::nullopt,
      "main"
  };
  constraints.outputs["z"] = {
      {100, 100},
      {  0,   0},
      "main"
  };
  ASSERT_TRUE(cell.try_get_driver_pin(2).is_invalid());
  const auto timing = late::Proxy(*graph, models, constraints).evaluate(nullptr, nullptr, {.capture_feasible_graph = true});
  ASSERT_TRUE(timing.certified);
  ASSERT_EQ(timing.achievable_period_ps, 90);
  tc::Pin_weight_bank bank({.blend_base = 1});
  const auto          weights = bank.update(timing);
  ASSERT_EQ(weights.pins.size(), 2);
  EXPECT_EQ(weights.related_output_path_arcs, 1);
  EXPECT_EQ(weights.pins.at("u:A").driver, "a");
  EXPECT_EQ(weights.pins.at("u:A").path, 1);
  EXPECT_EQ(weights.pins.at("z").driver, "u:SN");
  EXPECT_EQ(weights.pins.at("z").path, 0);
  EXPECT_FALSE(weights.pins.contains("u:CON"));
  EXPECT_TRUE(cell.try_get_driver_pin(2).is_invalid());
}
TEST(PinWeights, TimeUnitsDoNotChangeNormalizedWeights) {
  tc::Pin_weight_bank ps, ns;
  const auto          a = ps.update(cell_path()), b = ns.update(cell_path(.001));
  ASSERT_EQ(a.pins.size(), b.pins.size());
  for (const auto& [name, w] : a.pins) {
    const auto& n = b.pins.at(name);
    EXPECT_NEAR(w.graph, n.graph, 1e-12);
    EXPECT_NEAR(w.path, n.path, 1e-12);
    EXPECT_NEAR(w.weight, n.weight, 1e-12);
    EXPECT_NEAR(w.blend, n.blend, 1e-12);
  }
}
TEST(PinWeights, LeavingWindowClearsImmediatelyAndEpochChangesResetBlendHistory) {
  tc::Pin_weight_bank bank;
  const auto          first = bank.update(fanout());
  const auto          saved = bank;
  const auto          next  = bank.update(fanout(6000, 100));
  EXPECT_FALSE(next.pins.contains("side"));
  EXPECT_EQ(next.removed, 1);
  bank                = saved;
  const auto repeated = bank.update(fanout());
  EXPECT_GT(repeated.pins.at("side").weight, first.pins.at("side").weight);
  auto changed = fanout();
  ++changed.topology_generation;
  const auto reset = bank.update(changed);
  EXPECT_TRUE(reset.epoch_reset);
  EXPECT_EQ(reset.removed, 2);
  EXPECT_DOUBLE_EQ(reset.pins.at("side").weight, first.pins.at("side").weight);
  bank.update(changed);
  changed.characterized_boundary_policy = true;
  const auto policy_reset               = bank.update(changed);
  EXPECT_TRUE(policy_reset.epoch_reset);
  EXPECT_EQ(policy_reset.removed, 2);
  EXPECT_DOUBLE_EQ(policy_reset.pins.at("side").weight, first.pins.at("side").weight);
  changed.bounded_boundary_policy = true;
  const auto bounded_reset        = bank.update(changed);
  EXPECT_TRUE(bounded_reset.epoch_reset);
  EXPECT_EQ(bounded_reset.removed, 2);
  EXPECT_DOUBLE_EQ(bounded_reset.pins.at("side").weight, first.pins.at("side").weight);
  bank.clear();
  EXPECT_FALSE(bank.update(changed).epoch_reset);
}
TEST(PinWeights, DriverIdentityChangesCannotReuseOldSmoothedWeights) {
  tc::Pin_weight_bank bank;
  const auto          first = bank.update(fanout());
  bank.update(fanout());
  auto changed         = fanout();
  changed.pins[0].name = "replacement";
  const auto next      = bank.update(changed);
  EXPECT_EQ(next.pins.at("side").driver, "replacement");
  EXPECT_EQ(next.removed, 2);
  EXPECT_DOUBLE_EQ(next.pins.at("side").weight, first.pins.at("side").weight);
}
TEST(PinWeights, InvalidOrUnsupportedPathsLeaveTheBankUnchanged) {
  tc::Pin_weight_bank bank;
  bank.update(fanout());
  auto control      = bank;
  auto invalid      = fanout();
  invalid.certified = false;
  EXPECT_THROW(bank.update(invalid), std::invalid_argument);
  invalid                                    = fanout();
  invalid.pins[1].transitions[0].predecessor = late::Predecessor{2, 0};
  EXPECT_THROW(bank.update(invalid), std::invalid_argument);
  invalid = fanout();
  invalid.pins[1].transitions[0].predecessor.reset();
  EXPECT_THROW(bank.update(invalid), std::invalid_argument);
  const auto after = bank.update(fanout()), expected = control.update(fanout());
  EXPECT_EQ(after.pins.at("side").weight, expected.pins.at("side").weight);
}
TEST(PinWeights, NamedDomainPredecessorsRemainAuthoritativeOverAggregateReports) {
  auto                 named = fanout();
  late::Result::Domain domain;
  domain.launch_clock = "main";
  domain.pins.resize(named.pins.size());
  for (size_t i = 0; i < named.pins.size(); ++i) {
    for (unsigned t = 0; t < 2; ++t) {
      const auto& src   = named.pins[i].transitions[t];
      domain.pins[i][t] = {src.arrival_ps, src.required_ps, src.predecessor};
    }
  }
  named.domains.push_back(domain);
  named.feasible_graph->launch_domains     = {"main"};
  named.pins[1].transitions[0].predecessor = late::Predecessor{2, 0};
  tc::Pin_weight_bank bank({.blend_base = 1}), legacy({.blend_base = 1});
  const auto          a = bank.update(named), b = legacy.update(fanout());
  EXPECT_EQ(a.pins.at("critical").weight, b.pins.at("critical").weight);
  EXPECT_EQ(a.pins.at("side").weight, b.pins.at("side").weight);
}
TEST(PinWeights, ZeroDelayAndExtremeAdaptivePoliciesStayFiniteAndBounded) {
  auto                zero = fanout(0, 0);
  tc::Pin_weight_bank normal;
  const auto          r = normal.update(zero);
  for (const auto& [name, w] : r.pins) {
    EXPECT_TRUE(std::isfinite(w.weight));
    EXPECT_GE(w.blend, 0);
    EXPECT_LE(w.blend, 1);
  }
  tc::Pin_weight_bank fast({.rising_base = 1e300, .rising_exponent = 1e300});
  const auto          f = fast.update(fanout());
  EXPECT_EQ(f.pins.at("critical").blend, 1);
  tc::Pin_weight_bank frozen({.blend_base = 0});
  EXPECT_EQ(frozen.update(fanout()).pins.at("critical").weight, 0);
  EXPECT_THROW(tc::Pin_weight_bank({.graph_fraction = -1}), std::invalid_argument);
  EXPECT_THROW(tc::Pin_weight_bank({.rising_base = .5}), std::invalid_argument);
}
