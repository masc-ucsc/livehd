// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "proxy_bridge.hpp"

#include <algorithm>
#include <numeric>
#include <stdexcept>

#include "gtest/gtest.h"
#include "node_util.hpp"

namespace tc    = livehd::placement::timeclosure;
namespace late  = livehd::placement::late;
namespace model = livehd::liberty::timing;
namespace {
const std::string models = R"(
library(test){time_unit:"1ps";capacitive_load_unit(1,ff);default_input_pin_cap:1;
 cell(BUF){pin(A){direction:input;}pin(Y){direction:output;function:A;
 timing(){related_pin:A;timing_sense:positive_unate;
 cell_rise(scalar){values("5");}cell_fall(scalar){values("7");}
 rise_transition(scalar){values("1");}fall_transition(scalar){values("2");}}}}
 cell(XOR){pin(A){direction:input;}pin(B){direction:input;}pin(Y){direction:output;function:"A ^ B";
 timing(){related_pin:"A B";timing_sense:non_unate;
 cell_rise(scalar){values("5");}cell_fall(scalar){values("7");}
 rise_transition(scalar){values("1");}fall_transition(scalar){values("2");}}}}
 cell(FF){ff(IQ,IQN){clocked_on:CLK;next_state:D;}
 pin(CLK){direction:input;clock:true;}
 pin(D){direction:input;timing(){related_pin:CLK;timing_type:setup_rising;
 rise_constraint(scalar){values("5");}fall_constraint(scalar){values("8");}}}
 pin(Q){direction:output;timing(){related_pin:CLK;timing_type:rising_edge;timing_sense:non_unate;
 cell_rise(scalar){values("10");}cell_fall(scalar){values("20");}
 rise_transition(scalar){values("2");}fall_transition(scalar){values("3");}}}}
})";
struct Circuit {
  hhds::GraphLibrary                    owner;
  std::shared_ptr<const model::Library> library = std::make_shared<model::Library>(model::parse(models));
  std::shared_ptr<hhds::Graph>          graph;
  Circuit(std::initializer_list<std::string> inputs, std::initializer_list<std::string> outputs) {
    auto io = owner.create_io("objective_bridge");
    for (const auto& name : inputs) {
      io->add_input(name, io->get_input_pin_decls().size());
      io->set_bits(name, 1);
    }
    for (const auto& name : outputs) {
      io->add_output(name, io->get_output_pin_decls().size());
      io->set_bits(name, 1);
    }
    graph = io->create_graph();
  }
  hhds::Node_class cell(const std::string& name, const std::string& master) {
    auto io = owner.find_io(master);
    if (!io) {
      io = owner.create_io(master);
      for (const auto& [pin, decl] : library->cells.at(master).pins) {
        if (decl.direction == "input") {
          io->add_input(pin, io->get_input_pin_decls().size());
        } else {
          io->add_output(pin, io->get_output_pin_decls().size());
        }
        io->set_bits(pin, 1);
      }
    }
    auto node = livehd::graph_util::create_typed_node(*graph, Ntype_op::Sub);
    node.set_name(name);
    node.set_subnode(io);
    return node;
  }
};
struct Reconvergent : Circuit {
  Reconvergent() : Circuit({"clk"}, {"z"}) {
    auto ff = cell("ff", "FF"), b = cell("b", "BUF"), x = cell("x", "XOR");
    ff.create_sink_pin("CLK").connect_driver(graph->get_input_pin("clk"));
    b.create_sink_pin("A").connect_driver(ff.create_driver_pin("Q"));
    x.create_sink_pin("A").connect_driver(ff.create_driver_pin("Q"));
    x.create_sink_pin("B").connect_driver(b.create_driver_pin("Y"));
    ff.create_sink_pin("D").connect_driver(x.create_driver_pin("Y"));
    graph->get_output_pin("z").connect_driver(x.create_driver_pin("Y"));
  }
  late::Constraints constraints(bool named = false) {
    late::Constraints c;
    late::Ideal_clock clock{.period_ps = 100, .roots = {"clk"}};
    if (named) {
      c.clocks["main"] = clock;
    } else {
      c.clock = clock;
    }
    c.outputs["z"] = {
        {100, 100},
        {},
        named ? "main" : ""
    };
    c.complete = true;
    return c;
  }
};
void same_observation(const late::Result& a, const late::Result& b) {
  ASSERT_EQ(a.pins.size(), b.pins.size());
  EXPECT_EQ(a.wns_ps, b.wns_ps);
  EXPECT_EQ(a.tns_ps, b.tns_ps);
  EXPECT_EQ(a.achievable_period_ps, b.achievable_period_ps);
  EXPECT_EQ(a.certified, b.certified);
  EXPECT_EQ(a.unsupported, b.unsupported);
  EXPECT_EQ(a.visited_pins, b.visited_pins);
  EXPECT_EQ(a.evaluated_arcs, b.evaluated_arcs);
  for (size_t i = 0; i < a.pins.size(); ++i) {
    for (unsigned t = 0; t < 2; ++t) {
      const auto& x = a.pins[i].transitions[t];
      const auto& y = b.pins[i].transitions[t];
      EXPECT_EQ(x.arrival_ps, y.arrival_ps);
      EXPECT_EQ(x.required_ps, y.required_ps);
      EXPECT_EQ(x.slew_ps, y.slew_ps);
      EXPECT_EQ(x.load_ff, y.load_ff);
      EXPECT_EQ(bool(x.predecessor), bool(y.predecessor));
      if (x.predecessor && y.predecessor) {
        EXPECT_EQ(x.predecessor->pin, y.predecessor->pin);
        EXPECT_EQ(x.predecessor->transition, y.predecessor->transition);
      }
    }
  }
}
}  // namespace

TEST(ProxyObjective, CapturePreservesNativeObservationsAndAllReconvergentTransitionArcs) {
  Reconvergent c;
  late::Proxy  proxy(*c.graph, c.library, c.constraints());
  const auto   normal   = proxy.evaluate();
  const auto   captured = proxy.evaluate(nullptr, nullptr, {.capture_feasible_graph = true});
  ASSERT_TRUE(captured.certified);
  ASSERT_TRUE(captured.feasible_graph);
  EXPECT_FALSE(normal.feasible_graph);
  same_observation(normal, captured);
  const auto objective = tc::proxy_objective(captured);
  EXPECT_DOUBLE_EQ(objective.achievable_period, *normal.achievable_period_ps);
  EXPECT_NEAR(std::accumulate(objective.endpoint_mass.begin(), objective.endpoint_mass.end(), 0.), 1, 1e-12);
  bool        direct = false, detour = false;
  const auto& g = *captured.feasible_graph;
  for (size_t i = 0; i < g.arcs.size(); ++i) {
    const auto& arc  = g.arcs[i];
    const auto& from = g.vertices[arc.from];
    const auto& to   = g.vertices[arc.to];
    if (captured.pins[to.pin].name == "x:Y" && objective.arc_mass[i] > 0) {
      direct |= captured.pins[from.pin].name == "x:A";
      detour |= captured.pins[from.pin].name == "x:B";
      EXPECT_FALSE(arc.wire);
    }
  }
  EXPECT_TRUE(direct);
  EXPECT_TRUE(detour);
  for (const auto& v : g.vertices) {
    if (v.endpoint_required_ps) {
      EXPECT_TRUE(captured.pins[v.pin].endpoint);
    }
  }
}
TEST(ProxyObjective, NamedAndLegacyClockCaptureGiveTheSamePeriodAndKeepEndpointSeeds) {
  Reconvergent c;
  const auto   legacy
      = late::Proxy(*c.graph, c.library, c.constraints()).evaluate(nullptr, nullptr, {.capture_feasible_graph = true});
  const auto named
      = late::Proxy(*c.graph, c.library, c.constraints(true)).evaluate(nullptr, nullptr, {.capture_feasible_graph = true});
  ASSERT_TRUE(named.certified);
  ASSERT_TRUE(named.feasible_graph);
  EXPECT_EQ(tc::proxy_objective(legacy).achievable_period, tc::proxy_objective(named).achievable_period);
  ASSERT_EQ(named.feasible_graph->launch_domains.size(), 1);
  EXPECT_EQ(named.feasible_graph->launch_domains[0], "main");
  bool checked = false;
  for (const auto& vertex : named.feasible_graph->vertices) {
    if (named.pins[vertex.pin].name == "b:A") {
      EXPECT_FALSE(vertex.endpoint_required_ps);
      EXPECT_TRUE(named.domains[vertex.domain].pins[vertex.pin][vertex.transition].required_ps);
      checked = true;
    }
  }
  EXPECT_TRUE(checked);
}
TEST(ProxyObjective, AsynchronousSlowDomainDoesNotReceiveEligibleEndpointMass) {
  Circuit c({"ca", "cb", "a", "b"}, {"z"});
  auto    ff = c.cell("ff", "FF"), x = c.cell("x", "XOR");
  ff.create_sink_pin("CLK").connect_driver(c.graph->get_input_pin("ca"));
  x.create_sink_pin("A").connect_driver(c.graph->get_input_pin("a"));
  x.create_sink_pin("B").connect_driver(c.graph->get_input_pin("b"));
  ff.create_sink_pin("D").connect_driver(x.create_driver_pin("Y"));
  c.graph->get_output_pin("z").connect_driver(ff.create_driver_pin("Q"));
  late::Constraints limits;
  limits.clocks["a"] = {.period_ps = 100, .roots = {"ca"}};
  limits.clocks["b"] = {.period_ps = 200, .roots = {"cb"}};
  limits.asynchronous.emplace("a", "b");
  limits.inputs["a"] = {
      {10, 10},
      {},
      std::nullopt,
      "a"
  };
  limits.inputs["b"] = {
      {500, 500},
      {},
      std::nullopt,
      "b"
  };
  limits.outputs["z"] = {
      {100, 100},
      {},
      "a"
  };
  limits.complete = true;
  const auto r    = late::Proxy(*c.graph, c.library, limits).evaluate(nullptr, nullptr, {.capture_feasible_graph = true});
  ASSERT_TRUE(r.certified);
  ASSERT_TRUE(r.feasible_graph);
  ASSERT_TRUE(r.achievable_period_ps);
  const auto objective = tc::proxy_objective(r);
  EXPECT_DOUBLE_EQ(objective.achievable_period, *r.achievable_period_ps);
  bool slow_domain = false;
  for (size_t i = 0; i < r.feasible_graph->vertices.size(); ++i) {
    const auto& v = r.feasible_graph->vertices[i];
    if (r.feasible_graph->launch_domains[v.domain] == "b") {
      EXPECT_FALSE(v.endpoint_required_ps);
      EXPECT_EQ(objective.vertex_mass[i], 0);
      slow_domain = true;
    }
  }
  EXPECT_TRUE(slow_domain);
  limits.asynchronous.clear();
  const auto related = late::Proxy(*c.graph, c.library, limits).evaluate(nullptr, nullptr, {.capture_feasible_graph = true});
  ASSERT_TRUE(related.certified);
  EXPECT_FALSE(related.achievable_period_ps);
  EXPECT_THROW(tc::proxy_objective(related), std::invalid_argument);
}
TEST(ProxyObjective, ConstantSensitizationKeepsOnlyFeasibleNonUnateTransitions) {
  Circuit c({"clk", "a", "enable"}, {"z"});
  auto    x = c.cell("x", "XOR");
  x.create_sink_pin("A").connect_driver(c.graph->get_input_pin("a"));
  x.create_sink_pin("B").connect_driver(c.graph->get_input_pin("enable"));
  c.graph->get_output_pin("z").connect_driver(x.create_driver_pin("Y"));
  late::Constraints limits;
  limits.clocks["main"] = {.period_ps = 100, .roots = {"clk"}};
  limits.inputs["a"]    = {
      {3, 7},
      {},
      std::nullopt,
      "main"
  };
  limits.constants["enable"] = true;
  limits.outputs["z"]        = {
      {100, 100},
      {},
      "main"
  };
  limits.complete   = true;
  const auto result = late::Proxy(*c.graph, c.library, limits).evaluate(nullptr, nullptr, {.capture_feasible_graph = true});
  ASSERT_TRUE(result.certified);
  ASSERT_TRUE(result.feasible_graph);
  const auto objective = tc::proxy_objective(result);
  EXPECT_EQ(objective.achievable_period, 12);
  bool checked = false;
  for (const auto& arc : result.feasible_graph->arcs) {
    const auto& from = result.feasible_graph->vertices[arc.from];
    const auto& to   = result.feasible_graph->vertices[arc.to];
    if (result.pins[to.pin].name == "x:Y") {
      EXPECT_EQ(result.pins[from.pin].name, "x:A");
      EXPECT_NE(from.transition, to.transition);
      checked = true;
    }
  }
  EXPECT_TRUE(checked);
}
TEST(ProxyObjective, UnsupportedUncapturedOrInconsistentTimingCannotBecomeWeights) {
  Reconvergent c;
  auto         r = late::Proxy(*c.graph, c.library, c.constraints()).evaluate();
  EXPECT_THROW(tc::proxy_objective(r), std::invalid_argument);
  r                = late::Proxy(*c.graph, c.library, c.constraints()).evaluate(nullptr, nullptr, {.capture_feasible_graph = true});
  const auto saved = r;
  r.certified      = false;
  EXPECT_THROW(tc::proxy_objective(r), std::invalid_argument);
  r = saved;
  r.feasible_graph->reference_period_ps.reset();
  EXPECT_THROW(tc::proxy_objective(r), std::invalid_argument);
  r                                 = saved;
  r.feasible_graph->vertices[0].pin = r.pins.size();
  EXPECT_THROW(tc::proxy_objective(r), std::invalid_argument);
  r                                   = saved;
  r.feasible_graph->arcs[0].delay_ps += 10;
  EXPECT_THROW(tc::proxy_objective(r), std::invalid_argument);
  r                                    = saved;
  r.feasible_graph->vertices[0].domain = 99;
  EXPECT_THROW(tc::proxy_objective(r), std::invalid_argument);
}
TEST(ProxyObjective, SerializedWireDelaysAndPinLoadsRemainInTheObjectiveGraph) {
  Circuit c({"clk", "a"}, {"z"});
  auto    u = c.cell("u", "BUF");
  u.create_sink_pin("A").connect_driver(c.graph->get_input_pin("a"));
  c.graph->get_output_pin("z").connect_driver(u.create_driver_pin("Y"));
  late::Constraints limits;
  limits.clocks["main"] = {.period_ps = 100, .roots = {"clk"}};
  limits.inputs["a"]    = {
      {0, 0},
      {},
      std::nullopt,
      "main"
  };
  limits.outputs["z"] = {
      {100, 100},
      {  2,   3},
      "main"
  };
  limits.complete = true;
  livehd::physical::Parasitics rc;
  rc.nets = {
      {  "a", {{"a", 0}, {"u:A", 10}}, {{0, 1, 1000}}},
      {"u:Y", {{"u:Y", 0}, {"z", 10}}, {{0, 1, 1000}}}
  };
  late::Proxy proxy(*c.graph, c.library, limits);
  const auto  zero  = proxy.evaluate(nullptr, nullptr, {.capture_feasible_graph = true});
  const auto  wired = proxy.evaluate(&rc, nullptr, {.capture_feasible_graph = true});
  ASSERT_TRUE(wired.certified);
  const auto objective = tc::proxy_objective(wired);
  EXPECT_GT(objective.achievable_period, tc::proxy_objective(zero).achievable_period);
  bool positive_wire = false;
  for (size_t i = 0; i < wired.feasible_graph->arcs.size(); ++i) {
    const auto& arc = wired.feasible_graph->arcs[i];
    if (arc.wire && arc.delay_ps > 0 && objective.arc_mass[i] > 0) {
      positive_wire = true;
    }
  }
  EXPECT_TRUE(positive_wire);
}
