// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "physical_rank.hpp"

#include <cmath>
#include <format>
#include <map>
#include <string>

#include "cell.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "node_util.hpp"
#include "test_library.hpp"

namespace fast = livehd::timing::fast;
namespace gu   = livehd::graph_util;
namespace lt   = livehd::liberty::timing;
namespace ph   = livehd::physical;

namespace {
// d -> ua(FF) -> inv -> ub(FF) -> q, both flops on clk.
struct Design {
  hhds::GraphLibrary           library;
  std::shared_ptr<hhds::Graph> graph;
  Design() {
    auto ff = library.create_io("FF");
    ff->add_input("CLK", 1);
    ff->add_input("D", 2);
    ff->add_output("Q", 3);
    auto inv = library.create_io("INV");
    inv->add_input("A", 1);
    inv->add_output("Y", 2);
    auto top = library.create_io("top");
    top->add_input("clk", 1);
    top->add_input("d", 2);
    top->add_output("q", 3);
    graph      = top->create_graph();
    auto& g    = *graph;
    const auto make = [&](const std::shared_ptr<hhds::GraphIO>& io, std::string_view name) {
      auto node = gu::create_typed_node(g, Ntype_op::Sub);
      node.set_subnode(io);
      node.set_name(name);
      return node;
    };
    const auto ua = make(ff, "ua"), ub = make(ff, "ub"), ui = make(inv, "inv");
    g.get_input_pin("d").connect_sink(ua.create_sink_pin("D"));
    g.get_input_pin("clk").connect_sink(ua.create_sink_pin("CLK"));
    g.get_input_pin("clk").connect_sink(ub.create_sink_pin("CLK"));
    ua.create_driver_pin("Q").connect_sink(ui.create_sink_pin("A"));
    ui.create_driver_pin("Y").connect_sink(ub.create_sink_pin("D"));
    ub.create_driver_pin("Q").connect_sink(g.get_output_pin("q"));
  }
};

// The RC snapshot of the nets the placer sees; `wire` scales the wire of the inverter's input net.
std::shared_ptr<const ph::Parasitics> rc(double cap_ff, double res_ohm) {
  auto p = std::make_shared<ph::Parasitics>();
  ph::Rc_net n1;
  n1.driver = "ua:Q";
  n1.nodes  = {{"ua:Q", 0}, {"inv:A", cap_ff}};
  n1.edges  = {{0, 1, res_ohm}};
  ph::Rc_net n2;
  n2.driver = "inv:Y";
  n2.nodes  = {{"inv:Y", 0}, {"ub:D", 0}};
  n2.edges  = {{0, 1, 0}};
  p->nets   = {n1, n2};
  return p;
}

fast::Physical_ranker::Options options() {
  fast::Physical_ranker::Options o;
  o.liberty_texts.push_back(fast::test::library_text(1.0));
  const auto unit = lt::parse(o.liberty_texts[0]).time_unit_ps;
  o.sdc_text      = std::format("create_clock -name clk -period {} [get_ports clk]\n", 1000.0 / unit);
  return o;
}
}  // namespace

TEST(PhysicalRank, WireDelayLowersTheSlackOfTheEndpointItFeeds) {
  Design                  design;
  fast::Physical_ranker   ranker(options());
  const std::set<std::string> no_cuts;
  const auto              bare = ranker.rank(design.graph, rc(0, 0), no_cuts);
  const auto              wire = ranker.rank(design.graph, rc(5, 2000), no_cuts);  // 2 kΩ into 5 fF + the 2 fF pin
  ASSERT_TRUE(bare.ranking && wire.ranking);
  for (const auto& u : ranker.unsupported()) {
    ADD_FAILURE() << u;
  }
  EXPECT_EQ(ranker.statistics().unresolved_labels, 0u);
  EXPECT_EQ(ranker.engine()->report().endpoints, 2u);
  EXPECT_TRUE(bare.ranking_only);
  EXPECT_EQ(ranker.statistics().binds, 1u);   // the same graph is bound once
  EXPECT_EQ(ranker.statistics().remaps, 1u);  // and the same labels are mapped once
  const auto* a = bare.ranking->find("ub:D");
  const auto* b = wire.ranking->find("ub:D");
  ASSERT_TRUE(a && b);
  ASSERT_TRUE(std::isfinite(a->slack));
  // The wire adds its Elmore delay 2 * (5 + 2) = 14 ps, and the heavier load slows the flop's Q on top of it.
  EXPECT_GT(a->slack - b->slack, 14.0);
  EXPECT_LT(a->slack - b->slack, 40.0);
  // The clock pins are marked; the labels are sorted and unique; each net lists its labelled pins.
  ASSERT_TRUE(wire.ranking->find("inv:A"));
  const auto& labels = wire.ranking->layout->labels;
  EXPECT_TRUE(std::is_sorted(labels.begin(), labels.end()));
  EXPECT_EQ(wire.ranking->layout->net_offsets.size(), 3u);
  EXPECT_EQ(wire.ranking->layout->net_pins.size(), 4u);
}

TEST(PhysicalRank, StructuralChangeRebindsAndCutsRemoveTheStart) {
  Design                design;
  fast::Physical_ranker ranker(options());
  const auto            before = ranker.rank(design.graph, rc(5, 2000), {});
  const auto*           d0     = before.ranking->find("ub:D");
  ASSERT_TRUE(d0 && std::isfinite(d0->slack));
  // Cutting the driver of the first net removes the path through it: ub:D has no arrival any more.
  const auto cut = ranker.rank(design.graph, rc(5, 2000), {"ua:Q"});
  const auto* d1 = cut.ranking->find("ub:D");
  ASSERT_TRUE(d1);
  EXPECT_FALSE(std::isfinite(d1->slack));
  EXPECT_EQ(ranker.statistics().binds, 1u);
  // A structural edit advances the body epoch: the next call binds again.
  auto extra = gu::create_typed_node(*design.graph, Ntype_op::Sub);
  extra.set_subnode(design.library.create_io("INV"));
  const auto after = ranker.rank(design.graph, rc(5, 2000), {});
  EXPECT_EQ(ranker.statistics().binds, 2u);
  EXPECT_TRUE(std::isfinite(after.ranking->find("ub:D")->slack));
  // A resize (the same node with another cell) is a structural change too, and the slack follows the new cell.
  auto buf = design.library.create_io("BUF");
  buf->add_input("A", 1);
  buf->add_output("Y", 2);
  for (auto node : design.graph->body().nodes()) {
    if (node.get_name() == "inv") {
      node.set_subnode(buf);
    }
  }
  const auto resized = ranker.rank(design.graph, rc(5, 2000), {});
  EXPECT_EQ(ranker.statistics().binds, 3u);
  EXPECT_NE(resized.ranking->find("ub:D")->slack, after.ranking->find("ub:D")->slack);
}

TEST(PhysicalRank, DetailedEvaluationFillsTheFeedback) {
  Design                design;
  fast::Physical_ranker ranker(options());
  const auto            fb = ranker.evaluate(design.graph, rc(5, 2000), {});
  ASSERT_TRUE(fb.wns.has_value());
  ASSERT_TRUE(fb.hold_wns.has_value());
  EXPECT_GT(*fb.wns, 0);  // a 1 ns clock against a flop, an inverter and a flop
  EXPECT_GT(fb.area, 0);
  EXPECT_GT(fb.delay, 0);
  EXPECT_FALSE(fb.ranking_only);
  ASSERT_TRUE(fb.dense.has_value());
  const auto* d = fb.find_label("ub:D");
  ASSERT_TRUE(d);
  ASSERT_TRUE(d->slack && d->required && d->arrival && d->slew);
  EXPECT_GE(*d->slack, *d->required - *d->arrival - 1e-3);  // the extremes are taken per field, over both transitions
  EXPECT_NEAR(*d->slack, *fb.wns, 1e-3);  // the only endpoint on the worst path
  EXPECT_TRUE(d->hold_slack.has_value());
  const auto* q = fb.find_label("ua:Q");
  ASSERT_TRUE(q && q->load_ff);
  EXPECT_NEAR(*q->load_ff, 5 + 2, 1e-3);  // the wire plus the inverter's input pin
  EXPECT_FALSE(q->clock);
  ASSERT_FALSE(fb.path.empty());
  EXPECT_EQ(fb.path.back(), "ub:D");
}

TEST(PhysicalRank, WhatifEditsAgreeWithAFullEvaluationOfTheEditedDesign) {
  const std::set<std::string> no_cuts;
  Design                      design;
  fast::Physical_ranker       ranker(options());
  const auto                  base = ranker.evaluate(design.graph, rc(5, 2000), no_cuts);
  auto&                       w    = ranker.whatif();
  const double                wns0 = w.wns();
  EXPECT_NEAR(wns0, *base.wns, 1e-3);
  EXPECT_NEAR(*w.pin("ub:D").slack, *base.find_label("ub:D")->slack, 1e-3);
  EXPECT_NEAR(*w.load("ua:Q"), 7.0, 1e-3);
  EXPECT_NEAR(w.clock_period(), 1000.0, 1e-3);
  const auto analysis = w.setup_analysis();
  EXPECT_EQ(analysis.engine, "fast");
  EXPECT_TRUE(analysis.full_cycle);
  EXPECT_NEAR(analysis.periods_ps.at("clk"), 1000.0, 1e-3);
  EXPECT_NEAR(*w.pin_capacitance("INV", "A"), 2.0, 1e-3);
  ASSERT_TRUE(w.arc("INV", "A", "Y", 10, 5).has_value());
  EXPECT_FALSE(w.arc("INV", "A", "Nope", 10, 5).has_value());

  // A resize against the full evaluation of a design that has the other cell.
  const auto m = w.mark();
  w.resize("inv", "INV2");
  const double resized = w.wns();
  EXPECT_NE(resized, wns0);
  {
    Design other;
    auto   inv2 = other.library.create_io("INV2");
    inv2->add_input("A", 1);
    inv2->add_output("Y", 2);
    for (auto node : other.graph->body().nodes()) {
      if (node.get_name() == "inv") {
        node.set_subnode(inv2);
      }
    }
    fast::Physical_ranker fresh(options());
    const auto            fb = fresh.evaluate(other.graph, rc(5, 2000), no_cuts);
    EXPECT_NEAR(resized, *fb.wns, 1e-3);
    EXPECT_NEAR(*w.pin("ub:D").slack, *fb.find_label("ub:D")->slack, 1e-3);
  }
  w.rollback(m);
  EXPECT_NEAR(w.wns(), wns0, 1e-4);

  // A buffer in front of the inverter: it slows the path, and rolling back removes the cell and its name.
  w.add_cell("buf1", "BUF");
  auto net1   = rc(5, 2000);
  auto edited = std::make_shared<ph::Parasitics>(*net1);
  edited->nets[0].nodes = {{"ua:Q", 0}, {"buf1:A", 1}};
  edited->nets[0].edges = {{0, 1, 500}};
  ph::Rc_net out;
  out.driver = "buf1:Y";
  out.nodes  = {{"buf1:Y", 0}, {"inv:A", 2}};
  out.edges  = {{0, 1, 500}};
  w.set_net(edited->nets[0]);
  w.set_net(out);
  const double buffered = w.wns();
  EXPECT_LT(buffered, wns0);
  const auto failing = w.failing_endpoints(1e9);
  ASSERT_FALSE(failing.empty());
  EXPECT_EQ(failing.front().first, "ub:D");
  EXPECT_NEAR(failing.front().second, buffered, 1e-3);
  EXPECT_GT(w.pin("buf1:Y").arrival.value_or(0), 0);
  w.rollback(m);
  EXPECT_NEAR(w.wns(), wns0, 1e-4);
  EXPECT_FALSE(w.pin("buf1:Y").arrival.has_value());
  // A full evaluation after edits starts from the base again.
  const auto again = ranker.evaluate(design.graph, rc(5, 2000), no_cuts);
  EXPECT_NEAR(*again.wns, wns0, 1e-4);
  EXPECT_THROW(w.resize("nothing", "INV2"), std::invalid_argument);
}

TEST(PhysicalRank, FixedSetupDelayCannotPredictClockFrequencyEvenWhenItEqualsThePeriod) {
  Design design;
  auto config = options();
  const double unit = lt::parse(config.liberty_texts.front()).time_unit_ps;
  config.sdc_text += std::format("set_max_delay {} -from [get_clocks clk] -to [get_clocks clk]\n", 1000.0 / unit);
  fast::Physical_ranker ranker(config);
  ASSERT_TRUE(ranker.evaluate(design.graph, rc(5, 2000), {}).wns);
  EXPECT_TRUE(ranker.unsupported().empty());
  EXPECT_FALSE(ranker.whatif().setup_analysis().full_cycle);
  auto frozen = fast::Physical_ranker::from_frozen_checkpoint(ranker.whatif().frozen_checkpoint());
  EXPECT_FALSE(frozen->whatif().setup_analysis().full_cycle);
}

TEST(PhysicalRank, CrossingNetJournalMatchesFullTimingWithReconvergenceAndAClockException) {
  hhds::GraphLibrary lib;
  const auto ff = lib.create_io("FF"), inv = lib.create_io("INV"), xor2 = lib.create_io("XOR2");
  ff->add_input("CLK", 1);
  ff->add_input("D", 2);
  ff->add_output("Q", 3);
  inv->add_input("A", 1);
  inv->add_output("Y", 2);
  xor2->add_input("A", 1);
  xor2->add_input("B", 2);
  xor2->add_output("Y", 3);
  const auto top = lib.create_io("top");
  top->add_input("clkA", 1);
  top->add_input("clkB", 2);
  top->add_input("data", 3);
  const auto graph = top->create_graph();
  auto instance = [&](const auto& master, const char* name) {
    auto n = gu::create_typed_node(*graph, Ntype_op::Sub);
    n.set_subnode(master);
    n.set_name(name);
    return n;
  };
  auto launch = instance(ff, "launch"), local = instance(inv, "local"), outside = instance(inv, "outside");
  auto join = instance(xor2, "join"), capture = instance(ff, "capture"), other = instance(ff, "other");
  for (auto reg : {launch, capture}) {
    graph->get_input_pin("clkA").connect_sink(reg.create_sink_pin("CLK"));
  }
  graph->get_input_pin("clkB").connect_sink(other.create_sink_pin("CLK"));
  graph->get_input_pin("data").connect_sink(launch.create_sink_pin("D"));
  auto source = launch.create_driver_pin("Q");
  source.connect_sink(local.create_sink_pin("A"));
  source.connect_sink(outside.create_sink_pin("A"));
  local.create_driver_pin("Y").connect_sink(join.create_sink_pin("A"));
  outside.create_driver_pin("Y").connect_sink(join.create_sink_pin("B"));
  auto combined = join.create_driver_pin("Y");
  combined.connect_sink(capture.create_sink_pin("D"));
  combined.connect_sink(other.create_sink_pin("D"));

  auto constraints = options();
  constraints.sdc_text = "create_clock -name A -period 100 [get_ports clkA]\n"
                         "create_clock -name B -period 80 [get_ports clkB]\n"
                         "set_input_delay 0 -clock A [get_ports data]\n";
  auto baseline = std::make_shared<ph::Parasitics>();
  baseline->nets = {{"launch:Q", {{"launch:Q", 0}, {"local:A", 1}, {"outside:A", 1}}, {{0, 1, 100}, {0, 2, 100}}},
                    {"local:Y", {{"local:Y", 0}, {"join:A", 1}}, {{0, 1, 100}}},
                    {"outside:Y", {{"outside:Y", 0}, {"join:B", 1}}, {{0, 1, 100}}},
                    {"join:Y", {{"join:Y", 0}, {"capture:D", 1}, {"other:D", 1}}, {{0, 1, 100}, {0, 2, 100}}}};
  // Moving local changes an incident tree with an exterior branch, and the load seen at local:Y.
  auto moved = std::make_shared<ph::Parasitics>(*baseline);
  moved->nets[0].nodes[1].capacitance_ff = 16;
  moved->nets[0].nodes[2].capacitance_ff = 9;
  moved->nets[0].edges[1].resistance_ohm = 2500;
  moved->nets[1].nodes[1].capacitance_ff = 20;

  using Endpoints = std::map<std::string, std::pair<double, std::string>>;
  auto endpoints = [](livehd::timing::Whatif& timer) {
    Endpoints measured;
    EXPECT_TRUE(timer.visit_setup_paths([&](const auto& pin, double slack, const auto& clock, const auto&) {
      EXPECT_TRUE(measured.emplace(pin, std::pair{slack, clock}).second);
    }));
    return measured;
  };
  for (bool except_crossing : {false, true}) {
    if (except_crossing) {
      constraints.sdc_text += "set_false_path -from [get_clocks A] -to [get_clocks B]\n";
    }
    fast::Physical_ranker incremental(constraints), full(constraints);
    ASSERT_TRUE(incremental.evaluate(graph, baseline, {}).wns.has_value());
    auto& trial = incremental.whatif();
    EXPECT_FALSE(trial.setup_analysis().full_cycle);
    EXPECT_EQ(trial.setup_analysis().periods_ps.size(), 2u);
    const auto start = endpoints(trial);
    ASSERT_TRUE(start.contains("capture:D"));
    ASSERT_EQ(start.contains("other:D"), !except_crossing);
    EXPECT_EQ(start.at("capture:D").second, "A");
    if (!except_crossing) {
      EXPECT_EQ(start.at("other:D").second, "B");
    }
    const auto mark = trial.mark();
    trial.set_net(moved->nets[0]);
    trial.set_net(moved->nets[1]);
    ASSERT_TRUE(full.evaluate(graph, moved, {}).wns.has_value());
    ASSERT_TRUE(incremental.unsupported().empty());
    ASSERT_TRUE(full.unsupported().empty());
    auto& reference = full.whatif();
    const auto after = endpoints(trial);
    EXPECT_LT(after.at("capture:D").first, start.at("capture:D").first);
    EXPECT_LT(trial.tns(), 0);
    EXPECT_EQ(after, endpoints(reference));
    EXPECT_DOUBLE_EQ(trial.wns(), reference.wns());
    EXPECT_DOUBLE_EQ(trial.tns(), reference.tns());
    EXPECT_EQ(trial.load("launch:Q"), reference.load("launch:Q"));
    EXPECT_EQ(trial.pin("outside:A").slew, reference.pin("outside:A").slew);
    trial.rollback(mark);
    EXPECT_EQ(endpoints(trial), start);
  }
}

TEST(PhysicalRank, RankingFromNumericTreesEqualsRankingFromParasitics) {
  const std::set<std::string> no_cuts;
  Design                      design;
  fast::Physical_ranker       ranker(options());
  const auto                  from_rc = ranker.rank(design.graph, rc(5, 2000), no_cuts);

  auto topology           = std::make_shared<fast::Tree_topology>();
  topology->net_pin_start = {0, 2, 4};
  topology->pin_label     = {livehd::Pin_label("ua:Q"), livehd::Pin_label("inv:A"), livehd::Pin_label("inv:Y"), livehd::Pin_label("ub:D")};
  topology->driver_index  = {0, 0};
  fast::Wire_trees trees;
  trees.topology    = topology;
  trees.node_start  = {0, 2, 4};
  trees.edge_start  = {0, 1, 2};
  trees.node_cap_ff = {0, 5, 0, 0};
  trees.edges       = {{0, 1, 2000}, {0, 1, 0}};
  auto skeleton     = std::make_shared<ph::Parasitics>();
  skeleton->nets.resize(2);
  skeleton->nets[0].driver = "ua:Q";
  skeleton->nets[1].driver = "inv:Y";
  fast::Physical_ranker numeric(options());
  const auto            from_trees = numeric.rank_trees(design.graph, trees, skeleton, no_cuts);
  ASSERT_TRUE(from_trees.ranking);
  EXPECT_EQ(from_trees.ranking->parasitics, skeleton);
  const auto& a = *from_rc.ranking;
  const auto& b = *from_trees.ranking;
  ASSERT_EQ(a.layout->labels, b.layout->labels);
  ASSERT_EQ(a.pins.size(), b.pins.size());
  for (size_t i = 0; i < a.pins.size(); ++i) {
    EXPECT_EQ(a.pins[i].slack, b.pins[i].slack) << a.layout->labels[i];
    EXPECT_EQ(a.pins[i].clock, b.pins[i].clock);
  }
  EXPECT_EQ(a.layout->net_offsets, b.layout->net_offsets);
  EXPECT_EQ(a.layout->net_pins, b.layout->net_pins);
  EXPECT_EQ(numeric.statistics().remaps, 1u);
  (void)numeric.rank_trees(design.graph, trees, skeleton, no_cuts);
  EXPECT_EQ(numeric.statistics().remaps, 1u);  // the same topology is mapped once
  EXPECT_TRUE(trees.bytes() > 0);
}
