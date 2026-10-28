// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "attrs.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "pass/place/prects_networks.hpp"
#include "pass/place/preroute.hpp"
#include "pass/place/preroute_repair.hpp"
#include "pass/place/preroute_timing.hpp"
#include "pass/place/repair.hpp"
#include "pass/place/repair_internal.hpp"
#include "pass_opentimer.hpp"
#include "rapidjson/document.h"
#include "rc_batch.hpp"

namespace {
namespace gu   = livehd::graph_util;
using Feedback = livehd::timing::Feedback;

TEST(RcBatch, LargeUpdatesMaterializeInChunksAndKeepEveryNetForPropagation) {
  ot::Timer timer;
  timer.set_num_threads(1);
  timer.set_time_unit(ot::second_t(1e-12))
      .set_capacitance_unit(ot::farad_t(1e-15))
      .set_resistance_unit(ot::ohm_t(1000))
      .set_rc_model(ot::RcModel::elmore);
  constexpr size_t count = 22000;
  for (size_t i = 0; i < count; ++i) {
    const auto a = "input_" + std::to_string(i), y = "output_" + std::to_string(i);
    timer.insert_primary_input(a).insert_primary_output(y).disconnect_pin(y).connect_pin(y, a);
    timer.set_at(a, ot::MAX, ot::RISE, 0).set_slew(a, ot::MAX, ot::RISE, 20);
  }
  timer.update_netlist();
  livehd::opentimer::Rc_batch batch(timer);
  for (size_t i = 0; i < count; ++i) {
    const auto a = "input_" + std::to_string(i), y = "output_" + std::to_string(i);
    spef::Net  net(a, 10);
    net.caps.emplace_back(a, "", 2);
    net.caps.emplace_back(y, "", 8);
    net.ress.emplace_back(a, y, 1000);
    batch.add(std::move(net));
  }
  EXPECT_EQ(batch.size(), count);
  // Earlier chunks have replaced their trees; a final partial chunk remains.
  EXPECT_NE(timer.pins().at("input_0").net()->rct(), nullptr);
  EXPECT_EQ(timer.pins().at("input_21999").net()->rct(), nullptr);
  EXPECT_FALSE(timer.pins().at("output_0").at(ot::MAX, ot::RISE));
  batch.flush();
  batch.flush();  // Empty flush must not clear the accumulated frontier.
  timer.update_timing();
  for (size_t i = 0; i < count; ++i) {
    EXPECT_EQ(timer.pins().at("output_" + std::to_string(i)).at(ot::MAX, ot::RISE), 8);
  }
}
// Fault injection after an insertion has already changed the real timer journal.
class Insertion_probe : public livehd::timing::Whatif {
  livehd::timing::Whatif& inner;

public:
  size_t      additions = 0, fail_at = 0, net_updates = 0;
  std::string failing_setup_pin;
  bool        fail_setup_after_update = false;
  explicit Insertion_probe(livehd::timing::Whatif& timer) : inner(timer) {}
  size_t mark() const override { return inner.mark(); }
  void   rollback(size_t m) override { inner.rollback(m); }
  void   resize(const std::string& n, const std::string& m) override { inner.resize(n, m); }
  void   add_cell(const std::string& n, const std::string& m) override {
    inner.add_cell(n, m);
    if (++additions == fail_at) {
      throw std::runtime_error("injected repeater installation failure");
    }
  }
  void set_net(const livehd::physical::Rc_net& n) override {
    ++net_updates;
    inner.set_net(n);
  }
  double     wns() override { return inner.wns(); }
  double     tns() override { return inner.tns(); }
  Pin_timing pin(const std::string& p) override {
    auto result = inner.pin(p);
    if (p == failing_setup_pin && (!fail_setup_after_update || net_updates)) {
      result.slack = -1;
    }
    return result;
  }
  std::optional<double>                       load(const std::string& p) override { return inner.load(p); }
  std::vector<std::pair<std::string, double>> failing_endpoints(double below) override { return inner.failing_endpoints(below); }
  std::pair<unsigned, double>                 electrical(const std::vector<std::string>& p) override { return inner.electrical(p); }
  std::optional<std::pair<double, double>>    arc(const std::string& m, const std::string& f, const std::string& t, double slew,
                                                  double load) override {
    return inner.arc(m, f, t, slew, load);
  }
  std::optional<double> pin_capacitance(const std::string& m, const std::string& p) override { return inner.pin_capacitance(m, p); }
  double                clock_period() override { return inner.clock_period(); }
};
struct Circuit {
  hhds::GraphLibrary                            library;
  std::shared_ptr<hhds::Graph>                  graph;
  std::shared_ptr<livehd::physical::Parasitics> rc = std::make_shared<livehd::physical::Parasitics>();
  Circuit(bool strong, bool extra, double resistance = 100, bool shift_indices = false) {
    auto io = library.create_io("top");
    io->add_input("a", 0);
    io->set_bits("a", 1);
    io->add_output("y", 0);
    io->set_bits("y", 1);
    graph = io->create_graph();
    gu::set_bits(graph->get_input_pin("a"), 1);
    if (shift_indices) {
      const auto marker = gu::create_const(*graph, *Dlop::create_integer(42));
      EXPECT_FALSE(marker.is_invalid());
    }
    const auto gate = [&](std::string master, std::string name, const hhds::Pin_class& input, const std::string& physical_input) {
      const auto output = master.find("__buf_") != std::string::npos ? "X" : "Y";
      auto       cell   = library.find_io(master);
      if (!cell) {
        cell = library.create_io(master);
        cell->add_input("A", 0);
        cell->set_bits("A", 1);
        cell->add_output(output, 0);
        cell->set_bits(output, 1);
      }
      auto node = gu::create_typed_node(*graph, Ntype_op::Sub);
      node.set_subnode(cell);
      node.set_name(name);
      node.create_sink_pin("A").connect_driver(input);
      auto pin = node.create_driver_pin(output);
      gu::set_bits(pin, 1);
      node.attr(livehd::attrs::place).set(Ann_place(10, 10, 10, 10));
      rc->cells.push_back({name, 10, 10, 0, master, 10, 10});
      rc->nets.push_back({
          physical_input,
          {{physical_input, .1}, {name + ":A", .2}},
          {{0, 1, resistance}}
      });
      return pin;
    };
    auto        u      = gate(strong ? "sky130_fd_sc_hd__buf_2" : "sky130_fd_sc_hd__buf_1", "u", graph->get_input_pin("a"), "a");
    std::string driver = "u:X";
    if (extra) {
      u      = gate("sky130_fd_sc_hd__buf_1", "b", u, driver);
      driver = "b:X";
    }
    auto v = gate("sky130_fd_sc_hd__inv_1", "v", u, driver);
    graph->get_output_pin("y").connect_driver(v);
    rc->nets.push_back({
        "v:Y",
        {{"v:Y", .1}, {"y", .2}},
        {{0, 1, resistance}}
    });
  }
};
class PhysicalSession : public ::testing::Test {
protected:
  std::filesystem::path directory;
  Eprp_var              options;
  void                  SetUp() override {
    directory = std::filesystem::path(std::getenv("TEST_TMPDIR")) / ::testing::UnitTest::GetInstance()->current_test_info()->name();
    std::filesystem::create_directories(directory);
    std::ofstream(directory / "timing.sdc") << "create_clock -name virtual -period 10\nset_input_delay -clock virtual 0 "
                                               "[all_inputs]\nset_output_delay -clock virtual 0 [all_outputs]\n";
    std::ifstream source("inou/lefdef/tests/data/sky130hd.lib");
    std::string   liberty{std::istreambuf_iterator<char>(source), {}};
    const auto    begin = liberty.find("cell (\"sky130_fd_sc_hd__buf_1\")");
    ASSERT_NE(begin, std::string::npos);
    auto     end   = liberty.find('{', begin);
    unsigned depth = 1;
    while (depth && ++end < liberty.size()) {
      depth += liberty[end] == '{';
      depth -= liberty[end] == '}';
    }
    ASSERT_EQ(depth, 0u);
    auto variant = liberty.substr(begin, end - begin + 1);
    variant.replace(variant.find("sky130_fd_sc_hd__buf_1"), std::string("sky130_fd_sc_hd__buf_1").size(), "sky130_fd_sc_hd__buf_2");
    auto cap = variant.find("capacitance :", variant.find("pin (\"A\")"));
    variant.replace(cap, variant.find(';', cap) - cap + 1, "capacitance : 0.004206;");
    liberty.insert(liberty.rfind('}'), variant + "\n");
    std::ofstream(directory / "cells.lib") << liberty;
    options.dict = {
        {"files", (directory / "cells.lib").string() + "," + (directory / "timing.sdc").string()},
        {  "top",                                                                          "top"},
        { "hier",                                                                        "false"},
        {  "qor",                                   (directory / "must_not_exist.json").string()}
    };
    options.set_stage_labels(options.dict);
  }
  void        TearDown() override { std::filesystem::remove_all(directory); }
  static void compare(const Feedback& actual, const Feedback& expected) {
    const auto number = [](double a, double b) { EXPECT_NEAR(a, b, 1e-5 * std::max({1., std::abs(a), std::abs(b)})); };
    number(actual.delay, expected.delay);
    number(actual.area, expected.area);
    number(actual.electrical_excess, expected.electrical_excess);
    EXPECT_EQ(actual.violations, expected.violations);
    number(actual.time_unit_ps, expected.time_unit_ps);
    const auto optional = [&](auto a, auto b) {
      ASSERT_EQ(bool(a), bool(b));
      if (a && b) {
        number(*a, *b);
      }
    };
    optional(actual.wns, expected.wns);
    optional(actual.tns, expected.tns);
    optional(actual.hold_wns, expected.hold_wns);
    ASSERT_EQ(actual.pin_count(), expected.pin_count());
    expected.for_each_pin([&](const auto& name, const auto& pin) {
      ASSERT_NE(actual.find_label(name), nullptr) << name;
      const auto& value = actual.at_pin(name);
      optional(value.arrival, pin.arrival);
      optional(value.required, pin.required);
      optional(value.slack, pin.slack);
      optional(value.hold_slack, pin.hold_slack);
      optional(value.slew, pin.slew);
      optional(value.load_ff, pin.load_ff);
      optional(value.capacitance_ff, pin.capacitance_ff);
      EXPECT_EQ(value.clock, pin.clock);
      EXPECT_EQ(value.violations, pin.violations);
      number(value.electrical_excess, pin.electrical_excess);
    });
    EXPECT_EQ(actual.path, expected.path);
  }
};
TEST_F(PhysicalSession, RegisteredResetCutChangesDownstreamSlackWithoutChangingLoadsOrClocks) {
  hhds::GraphLibrary library;
  auto               io = library.create_io("top");
  for (const auto& [name, port] : std::vector<std::pair<std::string, unsigned>>{
           {    "clk", 1},
           {"trigger", 2},
           {   "data", 3}
  }) {
    io->add_input(name, port);
    io->set_bits(name, 1);
  }
  io->add_output("y", 0);
  io->set_bits("y", 1);
  auto graph = io->create_graph();
  for (const auto* name : {"clk", "trigger", "data"}) {
    gu::set_ubits(graph->get_input_pin(name), 1);
  }
  auto       rc = std::make_shared<livehd::physical::Parasitics>();
  const auto cell
      = [&](const std::string& name, const std::string& master, const std::vector<std::string>& inputs, const std::string& output) {
          auto type = library.create_io(master);
          for (size_t i = 0; i < inputs.size(); ++i) {
            type->add_input(inputs[i], i + 1);
            type->set_bits(inputs[i], 1);
          }
          type->add_output(output, 0);
          type->set_bits(output, 1);
          auto node = gu::create_typed_node(*graph, Ntype_op::Sub);
          node.set_subnode(type);
          node.set_name(name);
          gu::set_ubits(node.create_driver_pin(output), 1);
          node.attr(livehd::attrs::place).set(Ann_place(10, 10, 10, 10));
          rc->cells.push_back({name, 10, 10, 0, master, 10, 10});
          return node;
        };
  auto       reset = cell("reset_stage", "sky130_fd_sc_hd__dfxtp_1", {"CLK", "D"}, "Q");
  auto       mix   = cell("mix", "sky130_fd_sc_hd__nor2_1", {"A", "B"}, "Y");
  const auto wire  = [&](const hhds::Pin_class& from,
                         const hhds::Pin_class& to,
                         const std::string&     driver,
                         const std::string&     sink,
                         double                 resistance) {
    from.connect_sink(to);
    rc->nets.push_back({
        driver,
        {{driver, .1}, {sink, .2}},
        {{0, 1, resistance}}
    });
  };
  wire(graph->get_input_pin("clk"), reset.create_sink_pin("CLK"), "clk", "reset_stage:CLK", 100);
  wire(graph->get_input_pin("trigger"), reset.create_sink_pin("D"), "trigger", "reset_stage:D", 100);
  wire(graph->get_input_pin("data"), mix.create_sink_pin("A"), "data", "mix:A", 100);
  wire(reset.get_driver_pin("Q"), mix.create_sink_pin("B"), "reset_stage:Q", "mix:B", 5000000);
  wire(mix.get_driver_pin("Y"), graph->get_output_pin("y"), "mix:Y", "y", 100);
  std::ofstream(directory / "timing.sdc")
      << "create_clock -name clk -period 10 [get_ports clk]\n"
         "set_input_delay -clock clk 0 [all_inputs]\nset_output_delay -clock clk 0 [all_outputs]\n";
  const auto                          before = *rc;
  livehd::opentimer::Physical_session session(options);
  const auto                          full = session.evaluate(graph, rc);
  const std::set<std::string>         cuts{"reset_stage:Q", "mix:B"};
  const auto                          data = session.evaluate(graph, rc, cuts);
  ASSERT_TRUE(full.dense);
  ASSERT_TRUE(data.dense);
  EXPECT_TRUE(full.pins.empty());
  EXPECT_TRUE(data.pins.empty());
  EXPECT_EQ(full.dense->index, data.dense->index);
  EXPECT_NE(full.dense->values.data(), data.dense->values.data());
  ASSERT_TRUE(full.at_pin("y").slack);
  ASSERT_TRUE(data.at_pin("y").slack);
  EXPECT_GT(*data.at_pin("y").slack, *full.at_pin("y").slack + 1);
  EXPECT_TRUE(data.at_pin("reset_stage:CLK").clock);
  EXPECT_FALSE(data.at_pin("mix:B").arrival);
  EXPECT_TRUE(data.at_pin("mix:A").arrival);
  EXPECT_EQ(data.at_pin("reset_stage:Q").load_ff, full.at_pin("reset_stage:Q").load_ff);
  EXPECT_EQ(data.at_pin("mix:B").capacitance_ff, full.at_pin("mix:B").capacitance_ff);
  EXPECT_EQ(rc->nets, before.nets);
  livehd::opentimer::Physical_session fresh(options);
  compare(data, fresh.evaluate(graph, rc, cuts));
  compare(data, session.evaluate(graph, rc, cuts));
  // Reset is also a false path in the approximate GP model. Changing model
  // must preserve the cut while keeping the functional data input timed.
  using Purpose          = livehd::opentimer::Physical_session::Purpose;
  const auto ranked_full = session.evaluate(graph, rc, {}, Purpose::ranking);
  const auto ranked_data = session.evaluate(graph, rc, cuts, Purpose::ranking);
  ASSERT_TRUE(std::isfinite(ranked_full.pin_rank("PIN", "y").slack));
  ASSERT_TRUE(std::isfinite(ranked_data.pin_rank("PIN", "y").slack));
  EXPECT_TRUE(ranked_data.ranking_only);
  EXPECT_GT(ranked_data.pin_rank("PIN", "y").slack, ranked_full.pin_rank("PIN", "y").slack + 1);
  EXPECT_FALSE(std::isfinite(ranked_data.pin_rank("mix", "B").slack));
  EXPECT_TRUE(std::isfinite(ranked_data.pin_rank("mix", "A").slack));
  compare(data, session.evaluate(graph, rc, cuts));
  auto       moved          = std::make_shared<livehd::physical::Parasitics>(*rc);
  const auto original_pose  = reset.attr(livehd::attrs::place).get();
  auto       moved_pose     = original_pose;
  moved_pose.x             += 100;
  reset.attr(livehd::attrs::place).set(moved_pose);
  moved->cells.front().x = moved_pose.x;
  for (auto& net : moved->nets) {
    if (net.driver == "reset_stage:Q") {
      net.nodes.back().capacitance_ff  *= 2;
      net.edges.front().resistance_ohm *= 2;
    }
  }
  livehd::opentimer::Physical_session fresh_moved(options);
  const auto                          moved_data = session.evaluate(graph, moved, cuts);
  ASSERT_TRUE(moved_data.dense);
  EXPECT_EQ(moved_data.dense->index, full.dense->index);
  EXPECT_FALSE(moved_data.at_pin("mix:B").arrival);
  compare(moved_data, fresh_moved.evaluate(graph, moved, cuts));
  reset.attr(livehd::attrs::place).set(original_pose);
  compare(full, session.evaluate(graph, rc));
  EXPECT_EQ(session.statistics().builds, 1u);

  auto report_options              = options;
  report_options.graphs            = {graph};
  report_options.physical_rc       = rc;
  report_options.dict["qor"]       = (directory / "reset_report.json").string();
  report_options.dict["cache_dir"] = (directory / "reset_cache").string();
  report_options.set_stage_labels(report_options.dict);
  // Final STA must receive the same cuts, and a cached cut analysis must not
  // be replayed after those cuts are removed from the same physical design.
  for (bool cut : {true, true, false, true}) {
    report_options.physical_timing_cuts = cut ? cuts : std::set<std::string>{};
    Pass_opentimer::time_work(report_options);
    std::ifstream     input(directory / "reset_report.json");
    const std::string json{std::istreambuf_iterator<char>(input), {}};
    compare(livehd::placement::timing_feedback(json), cut ? data : full);
  }
  livehd::opentimer::Physical_session supplied_cuts(report_options);
  compare(data, supplied_cuts.evaluate(graph, rc));
  // Persist the distribution driver with its RC, then reload the library as a
  // standalone STA invocation would. The EPRP handoff is deliberately absent.
  auto persisted         = std::make_shared<livehd::physical::Parasitics>(*rc);
  persisted->timing_cuts = {"reset_stage:Q"};
  graph->get_input_node().attr(livehd::attrs::placement_rc).set(livehd::physical::serialize_parasitics(*persisted));
  const auto saved = (directory / "reset_library").string();
  library.save(saved);
  hhds::GraphLibrary restored;
  restored.load(saved);
  const auto reloaded   = restored.find_io("top")->get_graph();
  report_options.graphs = {reloaded};
  report_options.physical_rc.reset();
  report_options.physical_timing_cuts.clear();
  for (bool cut : {true, true, false, true}) {
    persisted->timing_cuts = cut ? std::set<std::string>{"reset_stage:Q"} : std::set<std::string>{};
    reloaded->get_input_node().attr(livehd::attrs::placement_rc).set(livehd::physical::serialize_parasitics(*persisted));
    Pass_opentimer::time_work(report_options);
    std::ifstream     input(directory / "reset_report.json");
    const std::string json{std::istreambuf_iterator<char>(input), {}};
    compare(livehd::placement::timing_feedback(json), cut ? data : full);
    // The physical session also applies persisted cuts and restores the old
    // masks when a later physical graph has none, without rebuilding its timer.
    compare(cut ? data : full, session.evaluate(graph, persisted));
  }
}

TEST_F(PhysicalSession, RankingTracksRcAndGateChangesWithoutPublishingDetailedTimes) {
  using Purpose = livehd::opentimer::Physical_session::Purpose;
  livehd::opentimer::Physical_session session(options);
  Circuit                             initial(false, false, 100);
  const auto                          first = session.evaluate(initial.graph, initial.rc, {}, Purpose::ranking);
  ASSERT_TRUE(first.ranking_only);
  ASSERT_TRUE(first.ranking);
  ASSERT_TRUE(std::isfinite(first.pin_rank("PIN", "y").slack));
  EXPECT_FALSE(first.has_pins());
  EXPECT_FALSE(first.wns);
  EXPECT_FALSE(first.tns);
  EXPECT_FALSE(first.electrical_pin_feedback);
  EXPECT_TRUE(first.path.empty());
  Circuit    longer(false, false, 10000);
  const auto moved = session.evaluate(longer.graph, longer.rc, {}, Purpose::ranking);
  ASSERT_TRUE(std::isfinite(moved.pin_rank("PIN", "y").slack));
  EXPECT_EQ(first.ranking->layout, moved.ranking->layout);
  EXPECT_LT(moved.pin_rank("PIN", "y").slack, first.pin_rank("PIN", "y").slack);
  Circuit                             changed(true, false, 10000);
  const auto                          repowered = session.evaluate(changed.graph, changed.rc, {}, Purpose::ranking);
  livehd::opentimer::Physical_session fresh_rank(options);
  const auto                          expected_rank = fresh_rank.evaluate(changed.graph, changed.rc, {}, Purpose::ranking);
  EXPECT_EQ(repowered.pin_rank("PIN", "y").slack, expected_rank.pin_rank("PIN", "y").slack);
  const auto detailed = session.evaluate(changed.graph, changed.rc);
  EXPECT_FALSE(detailed.ranking_only);
  livehd::opentimer::Physical_session fresh_detail(options);
  compare(detailed, fresh_detail.evaluate(changed.graph, changed.rc));
  const auto restored = session.evaluate(initial.graph, initial.rc, {}, Purpose::ranking);
  EXPECT_EQ(restored.pin_rank("PIN", "y").slack, first.pin_rank("PIN", "y").slack);
  EXPECT_EQ(session.statistics().builds, 1U);
  EXPECT_EQ(session.statistics().rankings, 4U);
  EXPECT_EQ(session.statistics().evaluations, 5U);
}

TEST_F(PhysicalSession, IndexedRankingPreservesWeightsAcrossTopologyAndFiltering) {
  using Purpose = livehd::opentimer::Physical_session::Purpose;
  livehd::opentimer::Physical_session session(options);
  std::vector<Feedback>               retained;
  for (const auto& [strong, extra] : {
           std::pair{false, false},
           { true,  true},
           {false, false}
  }) {
    Circuit    circuit(strong, extra, 5000, true);
    const auto rank = session.evaluate(circuit.graph, circuit.rc, {}, Purpose::ranking);
    ASSERT_TRUE(rank.ranking);
    EXPECT_FALSE(rank.has_pins());
    EXPECT_EQ(rank.ranking->parasitics, circuit.rc);
    livehd::opentimer::Physical_session fresh(options);
    const auto                          expected = fresh.evaluate(circuit.graph, circuit.rc, {}, Purpose::ranking);
    EXPECT_EQ(rank.ranking->layout->labels, expected.ranking->layout->labels);
    for (const auto& label : rank.ranking->layout->labels) {
      EXPECT_EQ(rank.ranking->find(label)->slack, expected.ranking->find(label)->slack) << label;
      EXPECT_EQ(rank.ranking->find(label)->clock, expected.ranking->find(label)->clock) << label;
    }
    Feedback legacy;
    legacy.ranking_only = true;
    for (const auto& label : rank.ranking->layout->labels) {
      const auto& value = *rank.ranking->find(label);
      if (std::isfinite(value.slack)) {
        legacy.pins[label].slack = value.slack;
      }
      if (value.clock) {
        legacy.pins[label].clock = true;
      }
    }
    auto reordered = *circuit.rc;
    std::reverse(reordered.nets.begin(), reordered.nets.end());
    for (bool filter : {false, true}) {
      livehd::placement::Prects_networks controls;
      if (filter) {
        controls.pins = {"u:A", "y"};  // Exercise partial-net filtering too.
      }
      const auto indexed = livehd::placement::prects_data_timing(rank, controls);
      const auto named   = livehd::placement::prects_data_timing(legacy, controls);
      for (double fraction : {.1, .2, 1.}) {
        const auto weights = livehd::placement::timing_weights(*circuit.rc, named, fraction);
        EXPECT_EQ(livehd::placement::timing_weights(*circuit.rc, indexed, fraction), weights);
        EXPECT_EQ(livehd::placement::timing_weights(reordered, indexed, fraction), weights);
      }
      EXPECT_EQ(indexed.cell_slack("u"), named.cell_slack("u"));
      EXPECT_EQ(indexed.cell_slack("missing"), named.cell_slack("missing"));
    }
    session.whatif().add_cell("trial_only", "sky130_fd_sc_hd__buf_1");
    const auto restored = session.evaluate(circuit.graph, circuit.rc, {}, Purpose::ranking);
    EXPECT_EQ(restored.pin_rank("PIN", "y").slack, rank.pin_rank("PIN", "y").slack);
    retained.push_back(rank);
  }
  // Old feedback owns its snapshot and layout after scratch graphs, timer
  // bindings and later topology generations have gone away.
  for (const auto& rank : retained) {
    EXPECT_FALSE(livehd::placement::timing_weights(*rank.ranking->parasitics, rank, .2).empty());
  }
}
TEST_F(PhysicalSession, DataCutsSurviveTopologyChangesAndCanBeRemoved) {
  livehd::opentimer::Physical_session session(options);
  for (bool extra : {false, true, false}) {
    Circuit                     circuit(false, extra);
    const std::set<std::string> cuts{"u:X"};
    const auto                  cut = session.evaluate(circuit.graph, circuit.rc, cuts);
    EXPECT_FALSE(cut.at_pin("y").arrival);
    livehd::opentimer::Physical_session fresh(options);
    compare(cut, fresh.evaluate(circuit.graph, circuit.rc, cuts));
    const auto restored = session.evaluate(circuit.graph, circuit.rc);
    EXPECT_TRUE(restored.at_pin("y").arrival);
    compare(restored, fresh.evaluate(circuit.graph, circuit.rc));
    // Leave the same mask active across the next topology change.
    compare(cut, session.evaluate(circuit.graph, circuit.rc, cuts));
  }
  EXPECT_EQ(session.statistics().builds, 1u);
}

TEST_F(PhysicalSession, ResizeRcTopologyAndRejectedTrialRollbackMatchFreshTiming) {
  livehd::opentimer::Physical_session session(options);
  const auto                          check = [&](Circuit& circuit) {
    const auto                          actual = session.evaluate(circuit.graph, circuit.rc);
    livehd::opentimer::Physical_session fresh(options);
    compare(actual, fresh.evaluate(circuit.graph, circuit.rc));
    EXPECT_FALSE(std::filesystem::exists(directory / "must_not_exist.json"));
    EXPECT_TRUE(actual.source_report.empty());
  };
  Circuit initial(false, false);
  check(initial);
  const auto nets = session.statistics().updated_nets;
  check(initial);
  EXPECT_EQ(nets, session.statistics().updated_nets);
  auto reordered = std::make_shared<livehd::physical::Parasitics>(*initial.rc);
  std::reverse(reordered->cells.begin(), reordered->cells.end());
  livehd::opentimer::Physical_session fresh_reordered(options);
  compare(session.evaluate(initial.graph, reordered), fresh_reordered.evaluate(initial.graph, reordered));
  check(initial);
  EXPECT_EQ(session.statistics().repowered_gates, 0u);
  Circuit resized(true, false, 100, true);
  check(resized);
  EXPECT_EQ(nets, session.statistics().updated_nets);  // Repower does not reload unchanged wire RC.
  Circuit moved(true, false, 500, true);
  check(moved);
  EXPECT_GT(session.statistics().updated_nets, nets);
  Circuit inserted(true, true, 500, true);
  check(inserted);
  check(initial);  // Rejected insertion/resize/RC trial: undo all three kinds of delta.
  EXPECT_EQ(session.statistics().builds, 1u);
  EXPECT_EQ(session.statistics().evaluations, 8u);
  EXPECT_GE(session.statistics().repowered_gates, 2u);
  EXPECT_EQ(session.statistics().inserted_gates, 1u);
  EXPECT_EQ(session.statistics().removed_gates, 1u);
}
TEST_F(PhysicalSession, PhysicalBindingsOutliveScratchRcAndRebindAfterTopologyChanges) {
  using Purpose = livehd::opentimer::Physical_session::Purpose;
  std::vector<std::pair<Feedback, Feedback>>        retained;
  std::weak_ptr<const livehd::physical::Parasitics> discarded;
  {
    livehd::opentimer::Physical_session session(options);
    for (unsigned step = 0; step < 6; ++step) {
      // Alternating topology removes and recreates b's pins. Fresh scratch
      // graphs/RC die each iteration, while detailed feedback stays alive.
      Circuit    circuit(step % 2, step % 3 != 1, 100 + 500 * step, true);
      const auto rank   = session.evaluate(circuit.graph, circuit.rc, {}, Purpose::ranking);
      const auto actual = session.evaluate(circuit.graph, circuit.rc);
      ASSERT_TRUE(actual.dense);
      ASSERT_TRUE(rank.ranking);
      EXPECT_EQ(actual.dense->index->labels, rank.ranking->layout->labels);
      livehd::opentimer::Physical_session fresh(options);
      retained.emplace_back(actual, fresh.evaluate(circuit.graph, circuit.rc));
      compare(retained.back().first, retained.back().second);
      EXPECT_TRUE(discarded.expired());
      discarded         = circuit.rc;
      auto&      whatif = session.whatif();
      const auto mark   = whatif.mark();
      whatif.add_cell("temporary_with_a_long_name", "sky130_fd_sc_hd__buf_1");
      (void)whatif.wns();
      whatif.rollback(mark);
      compare(session.evaluate(circuit.graph, circuit.rc), actual);
    }
  }
  EXPECT_TRUE(discarded.expired());
  for (const auto& [actual, expected] : retained) {
    compare(actual, expected);
  }
}

TEST_F(PhysicalSession, RejectsCyclicAndDisconnectedRcBeforeTiming) {
  Circuit circuit(false, false);
  auto&   net = circuit.rc->nets.front();
  net.nodes.push_back({{}, .1});
  net.edges.push_back({1, 2, 1});
  net.edges.push_back({2, 0, 1});
  livehd::opentimer::Physical_session session(options);
  EXPECT_THROW(session.evaluate(circuit.graph, circuit.rc), std::invalid_argument);
  net.edges.resize(1);
  livehd::opentimer::Physical_session fresh(options);
  EXPECT_THROW(fresh.evaluate(circuit.graph, circuit.rc), std::invalid_argument);
  net.edges.push_back({1, 2, 1});
  livehd::opentimer::Physical_session valid(options);
  EXPECT_NO_THROW(valid.evaluate(circuit.graph, circuit.rc));
}

TEST_F(PhysicalSession, HierarchicalNetNamesSurviveTopologyReplacementAndWhatifRollback) {
  Circuit           initial(false, false), buffered(false, true);
  const std::string prefix = "top/repeated_tile/compute_lane/register_bank/";
  for (auto* circuit : {&initial, &buffered}) {
    for (auto node : circuit->graph->body().nodes()) {
      if (node.get_subnode_io()) {
        node.set_name(prefix + std::string(node.get_name()));
      }
    }
    for (auto& cell : circuit->rc->cells) {
      cell.name = prefix + cell.name;
    }
    const auto rename = [&](std::string& pin) {
      if (pin.find(':') != std::string::npos) {
        pin = prefix + pin;
      }
    };
    for (auto& net : circuit->rc->nets) {
      rename(net.driver);
      for (auto& node : net.nodes) {
        rename(node.pin);
      }
    }
  }
  livehd::opentimer::Physical_session session(options);
  for (auto* circuit : {&initial, &buffered, &initial, &buffered}) {
    const auto baseline                            = session.evaluate(circuit->graph, circuit->rc);
    auto       edited                              = std::make_shared<livehd::physical::Parasitics>(*circuit->rc);
    edited->nets.at(1).edges.at(0).resistance_ohm *= 4;
    livehd::opentimer::Physical_session fresh(options);
    const auto                          expected = fresh.evaluate(circuit->graph, edited);
    auto&                               whatif   = session.whatif();
    const auto                          mark     = whatif.mark();
    whatif.set_net(edited->nets.at(1));
    ASSERT_TRUE(expected.wns);
    EXPECT_NEAR(whatif.wns(), *expected.wns * expected.time_unit_ps, 1e-3);
    whatif.rollback(mark);
    ASSERT_TRUE(baseline.wns);
    EXPECT_NEAR(whatif.wns(), *baseline.wns * baseline.time_unit_ps, 1e-3);
    compare(session.evaluate(circuit->graph, circuit->rc), baseline);
  }
  EXPECT_EQ(session.statistics().builds, 1u);
}
TEST_F(PhysicalSession, ShieldedLoadsRepowerAndRollbackMatchFreshTiming) {
  livehd::opentimer::Physical_session session(options);
  for (const auto& [strong, extra, resistance] : {
           std::tuple{false, false, 250000.},
           std::tuple{ true, false, 250000.},
           std::tuple{ true,  true, 100000.},
           std::tuple{false, false, 250000.},
  }) {
    Circuit                             circuit(strong, extra, resistance, true);
    const auto                          incremental = session.evaluate(circuit.graph, circuit.rc);
    livehd::opentimer::Physical_session fresh(options);
    compare(incremental, fresh.evaluate(circuit.graph, circuit.rc));
  }
  EXPECT_EQ(session.statistics().builds, 1u);
  EXPECT_GE(session.statistics().repowered_gates, 2u);
  EXPECT_EQ(session.statistics().inserted_gates, 1u);
  EXPECT_EQ(session.statistics().removed_gates, 1u);
}
TEST_F(PhysicalSession, FinalReportReconcilesTrialsAndSeedsStandaloneCache) {
  const auto read_report = [&](const char* name) {
    std::ifstream       input(directory / name);
    const std::string   text{std::istreambuf_iterator<char>(input), {}};
    rapidjson::Document json;
    json.Parse(text.c_str());
    EXPECT_FALSE(json.HasParseError());
    return json;
  };
  for (bool cut : {false, true}) {
    Circuit initial(false, false), trial(true, true, 500, true), final(false, false, 25000, true);
    auto    session = std::make_shared<livehd::opentimer::Physical_session>(options);
    session->evaluate(initial.graph, initial.rc);
    session->evaluate(trial.graph, trial.rc, {}, livehd::opentimer::Physical_session::Purpose::ranking);
    // The report must discard an unevaluated what-if edit before reconciling
    // the accepted graph, which also removes the trial's inserted cell.
    session->whatif().resize("u", "sky130_fd_sc_hd__buf_1");
    final.graph->get_input_node().attr(livehd::attrs::synth_region).set("report_color");
    final.graph->get_input_node().attr(livehd::attrs::synth_region_id).set(7);
    final.graph->get_input_node().attr(livehd::attrs::color).set(2);
    auto report                     = options;
    report.graphs                   = {final.graph};
    report.physical_rc              = final.rc;
    report.physical_timing_cuts     = cut ? std::set<std::string>{"u:X"} : std::set<std::string>{};
    report.physical_timing_session  = session;
    report.dict["qor"]              = (directory / "session.json").string();
    report.dict["cache_dir"]        = (directory / (cut ? "cut_cache" : "full_cache")).string();
    report.dict["stats"]            = "true";
    report.dict["critical_percent"] = "7";
    report.set_stage_labels(report.dict);
    Pass_opentimer::time_work(report);
    EXPECT_FALSE(report.physical_timing_session);
    EXPECT_EQ(session->statistics().builds, 1U);
    EXPECT_EQ(session->statistics().removed_gates, 1U);
    const auto actual = read_report("session.json");
    ASSERT_TRUE(actual.IsObject());
    EXPECT_TRUE(actual["physical_session_reused"].GetBool());
    EXPECT_EQ(actual["incremental"]["misses"].GetUint(), 1U);
    const auto& colors = actual["designs"][0]["colors"];
    ASSERT_EQ(colors.Size(), 1U);
    EXPECT_STREQ(colors[0]["module"].GetString(), "report_color");
    EXPECT_EQ(colors[0]["cells"].GetUint(), 2U);
    report.dict["qor"] = (directory / "cached.json").string();
    report.set_stage_labels(report.dict);
    Pass_opentimer::time_work(report);
    const auto cached = read_report("cached.json");
    EXPECT_EQ(cached["incremental"]["hits"].GetUint(), 1U);
    EXPECT_FALSE(cached["physical_session_reused"].GetBool());
    EXPECT_EQ(cached["designs"], actual["designs"]);
    report.dict["cache_dir"] = "";
    report.dict["qor"]       = (directory / "fresh.json").string();
    report.set_stage_labels(report.dict);
    Pass_opentimer::time_work(report);
    const auto fresh = read_report("fresh.json");
    for (const char* field :
         {"wns", "tns", "area", "cells", "max_delay", "constraints_complete", "timing_cells_complete", "path", "endpoints"}) {
      ASSERT_EQ(actual["designs"][0].HasMember(field), fresh["designs"][0].HasMember(field)) << field;
      if (fresh["designs"][0].HasMember(field)) {
        EXPECT_EQ(actual["designs"][0][field], fresh["designs"][0][field]) << field;
      }
    }
    std::ifstream                       input(directory / "session.json");
    const std::string                   text{std::istreambuf_iterator<char>(input), {}};
    livehd::opentimer::Physical_session expected(options);
    compare(livehd::placement::timing_feedback(text), expected.evaluate(final.graph, final.rc, report.physical_timing_cuts));
  }
}
TEST_F(PhysicalSession, FinalReportReusesUnchangedRcAndFallsBackForChangedOptions) {
  Circuit circuit(false, false);
  for (bool change_load : {false, true}) {
    auto session = std::make_shared<livehd::opentimer::Physical_session>(options, 2);
    session->evaluate(circuit.graph, circuit.rc);
    const auto nets                = session->statistics().updated_nets;
    auto       report              = options;
    report.graphs                  = {circuit.graph};
    report.physical_rc             = circuit.rc;
    report.physical_timing_session = session;
    report.dict["qor"]             = (directory / "final.json").string();
    if (change_load) {
      report.dict["io_load"] = "42";
    }
    report.set_stage_labels(report.dict);
    Pass_opentimer::time_work(report);
    std::ifstream       input(directory / "final.json");
    const std::string   text{std::istreambuf_iterator<char>(input), {}};
    rapidjson::Document json;
    json.Parse(text.c_str());
    ASSERT_FALSE(json.HasParseError());
    EXPECT_EQ(json["physical_session_reused"].GetBool(), !change_load);
    EXPECT_EQ(session->statistics().builds, 1U);
    EXPECT_EQ(session->statistics().updated_nets, nets);
    EXPECT_EQ(session->statistics().evaluations, change_load ? 1U : 2U);
    EXPECT_EQ(session->statistics().worker_threads, 2U);
    livehd::opentimer::Physical_session fresh(report);
    compare(livehd::placement::timing_feedback(text), fresh.evaluate(circuit.graph, circuit.rc));
  }
}
TEST_F(PhysicalSession, FinalReportReloadsChangedTimingFileContentsBeforeCaching) {
  Circuit circuit(true, false);
  for (const char* file : {"timing.sdc", "cells.lib"}) {
    auto session = std::make_shared<livehd::opentimer::Physical_session>(options);
    session->evaluate(circuit.graph, circuit.rc);
    const auto    path  = directory / file;
    const auto    stamp = std::filesystem::last_write_time(path);
    std::ifstream input(path);
    std::string   contents{std::istreambuf_iterator<char>(input), {}};
    input.close();
    const bool        sdc      = std::string_view(file) == "timing.sdc";
    const std::string before   = sdc ? "-period 10" : "0.004206";
    const std::string after    = sdc ? "-period 20" : "0.008206";
    const auto        position = contents.find(before);
    ASSERT_NE(position, std::string::npos);
    contents.replace(position, before.size(), after);
    std::ofstream(path) << contents;
    // Same paths, sizes and timestamps: compatibility must use file contents.
    std::filesystem::last_write_time(path, stamp);
    auto report                    = options;
    report.graphs                  = {circuit.graph};
    report.physical_rc             = circuit.rc;
    report.physical_timing_session = session;
    report.dict["qor"]             = (directory / "changed.json").string();
    report.dict["cache_dir"]       = (directory / "changed_cache").string();
    report.set_stage_labels(report.dict);
    Pass_opentimer::time_work(report);
    std::ifstream       output(directory / "changed.json");
    const std::string   text{std::istreambuf_iterator<char>(output), {}};
    rapidjson::Document json;
    json.Parse(text.c_str());
    ASSERT_FALSE(json.HasParseError());
    EXPECT_FALSE(json["physical_session_reused"].GetBool());
    EXPECT_EQ(json["incremental"]["misses"].GetUint(), 1U);
    EXPECT_EQ(session->statistics().evaluations, 1U);
    livehd::opentimer::Physical_session fresh(report);
    compare(livehd::placement::timing_feedback(text), fresh.evaluate(circuit.graph, circuit.rc));
    Pass_opentimer::time_work(report);
    std::ifstream       cached_output(directory / "changed.json");
    const std::string   cached_text{std::istreambuf_iterator<char>(cached_output), {}};
    rapidjson::Document cached;
    cached.Parse(cached_text.c_str());
    ASSERT_FALSE(cached.HasParseError());
    EXPECT_EQ(cached["incremental"]["hits"].GetUint(), 1U);
    EXPECT_EQ(cached["designs"], json["designs"]);
  }
}
TEST_F(PhysicalSession, DirectFeedbackMatchesExplicitReportAndDoesNotReloadFiles) {
  Circuit circuit(false, false);
  auto    report_options     = options;
  report_options.graphs      = {circuit.graph};
  report_options.physical_rc = circuit.rc;
  report_options.dict["qor"] = (directory / "explicit_report.json").string();
  report_options.set_stage_labels(report_options.dict);
  Pass_opentimer::time_work(report_options);
  std::ifstream                       input(directory / "explicit_report.json");
  const std::string                   json{std::istreambuf_iterator<char>(input), {}};
  const auto                          expected = livehd::placement::timing_feedback(json);
  livehd::opentimer::Physical_session session(options);
  compare(session.evaluate(circuit.graph, circuit.rc), expected);
  std::filesystem::remove(directory / "timing.sdc");
  std::filesystem::remove(directory / "cells.lib");
  // Repeated timing must not reread SDC or touch the configured report path.
  compare(session.evaluate(circuit.graph, circuit.rc), expected);
  EXPECT_FALSE(std::filesystem::exists(directory / "must_not_exist.json"));
}
TEST_F(PhysicalSession, InvalidCandidateDoesNotPoisonTheRetainedNetlist) {
  Circuit                             initial(false, false), candidate(true, false);
  livehd::opentimer::Physical_session session(options, 1);
  const auto                          before  = session.evaluate(initial.graph, initial.rc);
  auto                                unknown = candidate.library.create_io("missing_timing_cell");
  unknown->add_input("A", 0);
  unknown->set_bits("A", 1);
  unknown->add_output("Y", 0);
  unknown->set_bits("Y", 1);
  for (auto n : candidate.graph->body().nodes()) {
    if (n.get_name() == "v") {
      n.set_subnode(unknown);
    }
  }
  for (auto& c : candidate.rc->cells) {
    if (c.name == "v") {
      c.master = "missing_timing_cell";
    }
  }
  EXPECT_THROW(session.evaluate(candidate.graph, candidate.rc), std::runtime_error);
  compare(session.evaluate(initial.graph, initial.rc), before);
  EXPECT_EQ(session.statistics().worker_threads, 1U);
}
}  // namespace
namespace {
TEST_F(PhysicalSession, EvaluationRestoresAppliedWhatifRcBeforeReusingUnchangedNets) {
  for (bool explicit_rollback : {false, true}) {
    Circuit                             circuit(false, false);
    livehd::opentimer::Physical_session session(options);
    const auto                          baseline = session.evaluate(circuit.graph, circuit.rc);
    auto&                               whatif   = session.whatif();
    const auto                          mark     = whatif.mark();
    auto                                changed  = circuit.rc->nets.at(1);
    for (auto& node : changed.nodes) {
      node.capacitance_ff *= 20;
    }
    for (auto& edge : changed.edges) {
      edge.resistance_ohm *= 1000;
    }
    whatif.set_net(changed);
    ASSERT_TRUE(baseline.wns);
    EXPECT_GT(std::abs(whatif.wns() - *baseline.wns * baseline.time_unit_ps), 1e-3);
    if (explicit_rollback) {
      whatif.rollback(mark);  // Restoration is queued, not propagated yet.
    }
    // The incoming snapshot matches the original: ordinary incremental RC
    // reconciliation skips its nets, so the what-if journal must restore them.
    const auto restored = session.evaluate(circuit.graph, circuit.rc);
    compare(restored, baseline);
    EXPECT_EQ(whatif.mark(), 0U);
    EXPECT_NEAR(whatif.wns(), *baseline.wns * baseline.time_unit_ps, 1e-3);
  }
}

TEST_F(PhysicalSession, WhatifIndexReleasesItsRcSnapshotAtTheNextEvaluation) {
  Circuit                             circuit(false, false);
  livehd::opentimer::Physical_session session(options);
  session.evaluate(circuit.graph, circuit.rc);
  auto&      whatif = session.whatif();
  const auto mark   = whatif.mark();
  whatif.set_net(circuit.rc->nets.front());
  whatif.rollback(mark);
  std::weak_ptr<const livehd::physical::Parasitics> indexed = circuit.rc;
  circuit.rc                                                = std::make_shared<livehd::physical::Parasitics>(*circuit.rc);
  session.evaluate(circuit.graph, circuit.rc);
  // The old index is discarded together with its owning snapshot, even if no
  // further what-if work occurs. Reusing the public proxy must rebuild it.
  EXPECT_TRUE(indexed.expired());
  whatif.set_net(circuit.rc->nets.front());
  EXPECT_TRUE(indexed.expired());
  whatif.rollback(mark);
  livehd::opentimer::Physical_session fresh(options);
  const auto                          expected = fresh.evaluate(circuit.graph, circuit.rc);
  ASSERT_TRUE(expected.wns);
  EXPECT_NEAR(whatif.wns(), *expected.wns * expected.time_unit_ps, 1e-3);
}

TEST_F(PhysicalSession, ReconciliationMaterializesRollbackBeforeCommittingTrialConnections) {
  Circuit                             circuit(false, false);
  livehd::opentimer::Physical_session session(options);
  session.evaluate(circuit.graph, circuit.rc);
  auto candidate = std::make_shared<livehd::physical::Parasitics>(*circuit.rc);
  // Bypass u for v's input without adding or removing any timing gates.
  candidate->nets[0].nodes.push_back(candidate->nets[1].nodes.back());
  candidate->nets[0].edges.push_back({0, 2, 100});
  candidate->nets[1].nodes.pop_back();
  candidate->nets[1].edges.clear();
  for (auto node : circuit.graph->body().nodes()) {
    if (node.get_name() == "v") {
      node.get_sink_pin("A").del_driver();
      node.get_sink_pin("A").connect_driver(circuit.graph->get_input_pin("a"));
    }
  }
  auto& whatif = session.whatif();
  for (const auto& net : candidate->nets) {
    whatif.set_net(net);
  }
  const auto expected_wns = whatif.wns();  // Apply the trial connections.
  const auto expected_tns = whatif.tns();
  ASSERT_TRUE(std::isfinite(expected_wns));
  ASSERT_TRUE(std::isfinite(expected_tns));
  // evaluate() queues rollback, then commits this same topology. It must not
  // mistake the still-materialized trial connections for the baseline.
  const auto committed = session.evaluate(circuit.graph, candidate);
  ASSERT_TRUE(committed.wns);
  ASSERT_TRUE(committed.tns);
  EXPECT_NEAR(*committed.wns * committed.time_unit_ps, expected_wns, 1e-3);
  EXPECT_NEAR(*committed.tns * committed.time_unit_ps, expected_tns, 1e-3);
}

TEST_F(PhysicalSession, EvaluatedConnectionsSurviveRecycledPinsAndWhatifRollback) {
  Circuit                             first(false, true), plain(false, false);
  livehd::opentimer::Physical_session session(options);
  session.evaluate(first.graph, first.rc);
  for (const auto& name : {std::string("replacement"), std::string("b"), std::string("last")}) {
    // Removing a cell initialized between u and v leaves reusable pin indices
    // inside the evaluated index range, not just beyond its end.
    const auto before = session.evaluate(plain.graph, plain.rc);
    Circuit    next(false, true);
    for (auto node : next.graph->body().nodes()) {
      if (node.get_name() == "b") {
        node.set_name(name);
      }
    }
    for (auto& cell : next.rc->cells) {
      if (cell.name == "b") {
        cell.name = name;
      }
    }
    const auto rename = [&](std::string& pin) {
      if (pin.starts_with("b:")) {
        pin = name + pin.substr(1);
      }
    };
    for (auto& net : next.rc->nets) {
      rename(net.driver);
      for (auto& point : net.nodes) {
        rename(point.pin);
      }
    }
    // Connectivity must not depend on the incoming net order.
    std::reverse(next.rc->nets.begin(), next.rc->nets.end());
    livehd::opentimer::Physical_session fresh(options);
    const auto                          expected = fresh.evaluate(next.graph, next.rc);
    ASSERT_TRUE(before.wns);
    ASSERT_TRUE(expected.wns);
    auto&      whatif = session.whatif();
    const auto mark   = whatif.mark();
    whatif.add_cell(name, "sky130_fd_sc_hd__buf_1");
    for (const auto& net : next.rc->nets) {
      whatif.set_net(net);
    }
    EXPECT_NEAR(whatif.wns(), *expected.wns * expected.time_unit_ps, 1e-3);
    whatif.rollback(mark);
    EXPECT_NEAR(whatif.wns(), *before.wns * before.time_unit_ps, 1e-3);
    EXPECT_THROW(whatif.pin(name + ":X"), std::runtime_error);
    compare(session.evaluate(plain.graph, plain.rc), before);
    compare(session.evaluate(next.graph, next.rc), expected);
  }
}

TEST_F(PhysicalSession, DuplicatePhysicalConnectionFailsAndSessionCanRecover) {
  Circuit                             circuit(false, false);
  livehd::opentimer::Physical_session session(options);
  const auto                          before    = session.evaluate(circuit.graph, circuit.rc);
  auto                                duplicate = std::make_shared<livehd::physical::Parasitics>(*circuit.rc);
  auto&                               net       = duplicate->nets.front();
  const auto                          index     = net.nodes.size();
  net.nodes.push_back({"y", .2});
  net.edges.push_back({0, static_cast<unsigned>(index), 100});
  EXPECT_THROW(session.evaluate(circuit.graph, duplicate), std::runtime_error);
  compare(session.evaluate(circuit.graph, circuit.rc), before);
}

TEST_F(PhysicalSession, WhatifResizeBufferAndRollbackMatchFreshTiming) {
  Circuit                             initial(false, false), strong(true, false), buffered(false, true);
  livehd::opentimer::Physical_session session(options);
  const auto                          before = session.evaluate(initial.graph, initial.rc);
  auto&                               w      = session.whatif();
  const auto                          ps     = [&](const Feedback& f, double v) { return v * f.time_unit_ps; };
  const double                        wns0 = w.wns(), tns0 = w.tns();
  ASSERT_TRUE(before.wns);
  EXPECT_NEAR(wns0, ps(before, *before.wns), 1e-3);
  EXPECT_NEAR(tns0, ps(before, before.tns.value_or(0)), 1e-3);

  // Resize: equal to a fresh timer on the resized netlist.
  const auto start = w.mark();
  w.resize("u", "sky130_fd_sc_hd__buf_2");
  livehd::opentimer::Physical_session fresh_strong(options);
  const auto                          sized = fresh_strong.evaluate(strong.graph, strong.rc);
  EXPECT_NEAR(w.wns(), ps(sized, *sized.wns), 1e-3);
  EXPECT_NEAR(*w.pin("y").slack, ps(sized, *sized.at_pin("y").slack), 1e-3);
  ASSERT_TRUE(sized.at_pin("y").hold_slack);
  ASSERT_TRUE(w.pin("y").hold_slack);
  EXPECT_NEAR(*w.pin("y").hold_slack, ps(sized, *sized.at_pin("y").hold_slack), 1e-3);
  w.rollback(start);
  EXPECT_NEAR(w.wns(), wns0, 1e-6);

  // Buffer insertion through set_net on both pieces: equal to the explicit netlist.
  w.add_cell("b", "sky130_fd_sc_hd__buf_1");
  w.set_net({
      "u:X",
      {{"u:X", .1}, {"b:A", .2}},
      {{0, 1, 100}}
  });
  w.set_net({
      "b:X",
      {{"b:X", .1}, {"v:A", .2}},
      {{0, 1, 100}}
  });
  livehd::opentimer::Physical_session fresh_buffered(options);
  const auto                          inserted = fresh_buffered.evaluate(buffered.graph, buffered.rc);
  EXPECT_NEAR(w.wns(), ps(inserted, *inserted.wns), 1e-3);
  EXPECT_NEAR(*w.pin("v:A").arrival, ps(inserted, *inserted.at_pin("v:A").arrival), 1e-3);
  ASSERT_TRUE(w.load("b:X"));
  EXPECT_GT(*w.load("b:X"), .29);
  ASSERT_TRUE(w.pin("b:X").arrival);  // Populate the transient identity cache.
  const auto failing = w.failing_endpoints(1e9);
  ASSERT_EQ(failing.size(), 1u);
  EXPECT_EQ(failing.front().first, "y");
  w.rollback(start);
  EXPECT_NEAR(w.wns(), wns0, 1e-6);
  EXPECT_NEAR(w.tns(), tns0, 1e-6);
  EXPECT_THROW(w.pin("b:X"), std::runtime_error);
  EXPECT_NEAR(*w.pin("v:A").arrival, ps(before, *before.at_pin("v:A").arrival), 1e-3);

  // Pending edits never leak into the next full evaluation.
  w.resize("u", "sky130_fd_sc_hd__buf_2");
  compare(session.evaluate(initial.graph, initial.rc), before);
  EXPECT_EQ(w.mark(), 0u);
  EXPECT_EQ(session.statistics().builds, 1u);
  const auto arc = w.arc("sky130_fd_sc_hd__buf_1", "A", "X", 20, 1);
  ASSERT_TRUE(arc);
  EXPECT_GT(arc->first, 0);
  EXPECT_DOUBLE_EQ(livehd::placement::preroute_hold_margin(w, "sky130_fd_sc_hd__buf_1", "A", "X", 20, 1), 2 * arc->first);
  EXPECT_THROW(livehd::placement::preroute_hold_margin(w, "sky130_fd_sc_hd__buf_1", "A", "X", -1, 1), std::invalid_argument);
  EXPECT_ANY_THROW(livehd::placement::preroute_hold_margin(w, "missing_buffer", "A", "X", 20, 1));
  EXPECT_EQ(w.mark(), 0u);
  EXPECT_NEAR(w.wns(), wns0, 1e-6);
  EXPECT_GT(*w.pin_capacitance("sky130_fd_sc_hd__buf_1", "A"), 0);
  // A checkpoint may reuse a physical name with a different timer identity.
  const auto refreshed = session.evaluate(buffered.graph, buffered.rc);
  ASSERT_TRUE(before.dense);
  ASSERT_TRUE(refreshed.dense);
  EXPECT_NE(before.dense->index, refreshed.dense->index);
  EXPECT_EQ(before.find_label("b:X"), nullptr);
  EXPECT_NE(refreshed.find_label("b:X"), nullptr);
  ASSERT_TRUE(w.pin("b:X").arrival);
  ASSERT_TRUE(refreshed.at_pin("b:X").arrival);
  EXPECT_NEAR(*w.pin("b:X").arrival, ps(refreshed, *refreshed.at_pin("b:X").arrival), 1e-3);
  session.evaluate(initial.graph, initial.rc);
  EXPECT_THROW(w.pin("b:X"), std::runtime_error);
  const auto reinserted = session.evaluate(buffered.graph, buffered.rc);
  ASSERT_TRUE(w.pin("b:X").arrival);
  EXPECT_NEAR(*w.pin("b:X").arrival, ps(reinserted, *reinserted.at_pin("b:X").arrival), 1e-3);
}
}  // namespace

TEST_F(PhysicalSession, PrerouteTimingChecksMarginsAndRestoresAcceptedAndRejectedTrials) {
  Circuit                             circuit(false, false);
  livehd::opentimer::Physical_session session(options);
  session.evaluate(circuit.graph, circuit.rc);
  auto&                                     timer    = session.whatif();
  const auto                                original = circuit.rc->nets.at(1);
  livehd::placement::Preroute_timing_limits limits;
  limits.hold_margin_ps = 0;
  const auto      mark  = timer.mark();
  const auto      wns   = timer.wns();
  const auto      load  = timer.load(original.driver);
  Insertion_probe probe(timer);
  auto            trial = livehd::placement::check_preroute_timing(probe, original, original, {"y"}, limits);
  ASSERT_TRUE(trial.legal) << trial.reason << " " << trial.pin;
  ASSERT_TRUE(trial.net_hold_slack_ps);
  EXPECT_NEAR(*trial.net_hold_slack_ps, std::min(*timer.pin("u:X").hold_slack, *timer.pin("v:A").hold_slack), 1e-6);
  ASSERT_TRUE(trial.hold_slack_ps);
  EXPECT_NEAR(*trial.hold_slack_ps, std::min(*trial.net_hold_slack_ps, *timer.pin("y").hold_slack), 1e-6);
  EXPECT_EQ(probe.net_updates, 0);
  EXPECT_EQ(timer.mark(), mark);
  EXPECT_DOUBLE_EQ(timer.wns(), wns);
  EXPECT_EQ(timer.load(original.driver), load);
  auto slow                    = original;
  slow.edges[0].resistance_ohm = 1e9;
  trial                        = livehd::placement::check_preroute_timing(probe, original, slow, {"y"}, limits);
  EXPECT_EQ(probe.net_updates, 1);
  EXPECT_FALSE(trial.legal);
  EXPECT_EQ(trial.reason, "routed setup margin");
  EXPECT_EQ(timer.mark(), mark);
  EXPECT_NEAR(timer.wns(), wns, 1e-6);
  limits.hold_margin_ps = 1e9;
  trial                 = livehd::placement::check_preroute_timing(timer, original, original, {}, limits);
  EXPECT_FALSE(trial.legal);
  EXPECT_EQ(trial.reason, "original hold margin");
  limits.hold_margin_ps = 0;
  EXPECT_THROW(livehd::placement::check_preroute_timing(timer, original, original, {"unknown:A"}, limits), std::runtime_error);
  EXPECT_EQ(timer.mark(), mark);
  auto fast = original;
  for (auto& node : fast.nodes) {
    node.capacitance_ff = 0;
  }
  for (auto& edge : fast.edges) {
    edge.resistance_ohm = 0;
  }
  const auto hold
      = [&] { return std::min({*timer.pin("u:X").hold_slack, *timer.pin("v:A").hold_slack, *timer.pin("y").hold_slack}); };
  const auto before_hold = hold();
  timer.set_net(fast);
  const auto after_hold = hold();
  timer.rollback(mark);
  ASSERT_LT(after_hold, before_hold);
  limits.hold_margin_ps = (before_hold + after_hold) / 2;
  trial                 = livehd::placement::check_preroute_timing(timer, original, fast, {"y"}, limits);
  EXPECT_FALSE(trial.legal);
  EXPECT_EQ(trial.reason, "routed hold margin");
  EXPECT_NEAR(hold(), before_hold, 1e-6);
  // A nested trial must preserve prior caller-owned what-if changes too.
  timer.resize("u", "sky130_fd_sc_hd__buf_2");
  const auto nested_mark = timer.mark();
  const auto nested_wns  = timer.wns();
  limits.hold_margin_ps  = 0;
  trial                  = livehd::placement::check_preroute_timing(timer, original, original, {"y"}, limits);
  EXPECT_TRUE(trial.legal) << trial.reason;
  EXPECT_EQ(timer.mark(), nested_mark);
  EXPECT_NEAR(timer.wns(), nested_wns, 1e-6);
  timer.rollback(mark);
  limits.deadline = std::chrono::steady_clock::now();
  EXPECT_TRUE(livehd::placement::check_preroute_timing(timer, original, original, {}, limits).timed_out);
  auto bad             = original;
  bad.nodes.back().pin = "other:A";
  EXPECT_THROW(livehd::placement::check_preroute_timing(timer, original, bad, {}, limits), std::invalid_argument);
  EXPECT_EQ(timer.mark(), mark);
}

TEST_F(PhysicalSession, RepeaterProposalUsesRoutingRcAndOnlyAddsLegallyFixedCells) {
  namespace pl = livehd::placement;
  namespace ph = livehd::physical;
  Circuit     circuit(false, false);
  auto        tech = ph::read_lef({"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"});
  pl::Options config;
  config.liberty = (directory / "cells.lib").string();
  pl::Result state;
  state.graph        = circuit.graph;
  state.parasitics   = circuit.rc;
  auto& frame        = state.floorplan;
  frame.dbu          = tech.dbu;
  const auto& buffer = tech.macros.at("sky130_fd_sc_hd__buf_1");
  const auto& site   = tech.sites.at(buffer.site);
  const auto  sw = std::llround(site.width * tech.dbu), rh = std::llround(site.height * tech.dbu);
  const auto  width = (200 * tech.dbu / sw) * sw;
  frame.die         = {0, 0, double(width), double(4 * rh)};
  for (int row = 0; row < 4; ++row) {
    frame.rows.push_back(
        {"r" + std::to_string(row), buffer.site, 0, row * rh, width / sw, sw, row % 2 ? ph::Orientation::FS : ph::Orientation::N});
  }
  for (auto node : state.graph->body().nodes()) {
    if (node.get_name() != "u" && node.get_name() != "v") {
      continue;
    }
    const auto& master = tech.macros.at(std::string(node.get_subnode_io()->get_name()));
    const auto  x      = node.get_name() == "u" ? 10 * sw : width - 20 * sw;
    Ann_place   place(x, 0, std::llround(master.width * tech.dbu), std::llround(master.height * tech.dbu));
    place.status = ph::Status::PLACED;
    node.attr(livehd::attrs::place).set(place);
    frame.components.push_back({std::string(node.get_name()), std::string(node.get_subnode_io()->get_name()), place});
    state.pin_locations[node.get_name() == "u" ? "u:X" : "v:A"] = {double(x) + sw, double(rh) / 2};
  }
  state.parasitics->cells.clear();
  for (const auto& cell : frame.components) {
    state.parasitics->cells.push_back(
        {cell.name, cell.place.x, cell.place.y, unsigned(cell.place.orientation), cell.macro, cell.place.width, cell.place.height});
  }
  frame.nets = {
      {"signal", {{"u", "X"}, {"v", "A"}}, {}, "SIGNAL"}
  };
  auto snapshot        = std::make_shared<pl::Preroute_design>();
  snapshot->pin_labels = {"u:X", "v:A"};
  auto& design         = snapshot->design;
  design.layout        = {frame.die.xl, frame.die.yl, frame.die.xh, frame.die.yh, double(sw), double(rh)};
  design.num_movable   = int(frame.components.size());
  for (const auto& cell : frame.components) {
    design.node_x.push_back(cell.place.x);
    design.node_y.push_back(cell.place.y);
    design.node_size_x.push_back(cell.place.width);
    design.node_size_y.push_back(cell.place.height);
    design.node_macro.push_back(0);
  }
  for (const auto& label : snapshot->pin_labels) {
    const auto name = label.substr(0, label.find(':'));
    const auto cell = std::find_if(frame.components.begin(), frame.components.end(), [&](const auto& c) { return c.name == name; });
    design.pin2node.push_back(int(cell - frame.components.begin()));
    const auto xy = state.pin_locations.at(label);
    design.pin_offset_x.push_back(xy.first - cell->place.x);
    design.pin_offset_y.push_back(xy.second - cell->place.y);
  }
  design.flat_net2pin       = {0, 1};
  design.flat_net2pin_start = {0, 2};
  design.net_weights        = {1};
  design.build_maps();
  state.preroute_design                             = snapshot;
  const auto                    original_components = frame.components;
  pl::detail::Repeater_proposer proposer(config, tech);
  const auto                    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  auto                          proposal = proposer.propose(state, {}, "u:X", {100, 10}, deadline);
  ASSERT_TRUE(proposal);
  EXPECT_FALSE(proposal->cells.empty());
  EXPECT_EQ(proposal->sinks, (std::vector<std::string>{"v:A"}));
  EXPECT_EQ(proposal->cells.size(), proposal->tree.buffers.size());
  livehd::opentimer::Physical_session session(options);
  const auto                          feedback = session.evaluate(state.graph, state.parasitics);
  const auto                          timed    = proposer.propose(state, feedback, "u:X", {100, 10}, deadline);
  ASSERT_TRUE(timed);
  EXPECT_FALSE(timed->cells.empty());
  const auto materialized = pl::detail::repeater_snapshot(state, *timed, {0, 0, 1}, tech, {100, 10});
  EXPECT_EQ(materialized.branches.size(), timed->cells.size() + 1);
  EXPECT_EQ(materialized.state.floorplan.components.size(), frame.components.size() + timed->cells.size());
  EXPECT_EQ(materialized.state.floorplan.nets.size(), timed->cells.size() + 1);
  EXPECT_EQ(materialized.state.parasitics->nets.front(), state.parasitics->nets.front());
  EXPECT_EQ(materialized.state.parasitics->nets[2], state.parasitics->nets[2]);
  EXPECT_EQ(materialized.state.preroute_design->design.node_macro.size(), materialized.state.floorplan.components.size());
  EXPECT_EQ(
      pl::bind_preroute(materialized.state.floorplan, *materialized.state.parasitics, *materialized.state.preroute_design).size(),
      materialized.branches.size());
  // The reconstructed physical/timing view must describe the proposed graph,
  // including every new buffer and its exact legalized geometry.
  EXPECT_NO_THROW(session.evaluate(materialized.state.graph, materialized.state.parasitics));
  snapshot->dont_touch_pins.insert("v:A");
  EXPECT_THROW(pl::detail::repeater_snapshot(state, *timed, {0, 0, 1}, tech, {100, 10}), std::invalid_argument);
  snapshot->dont_touch_pins.clear();
  auto powered                   = state;
  powered.floorplan.special_nets = {
      {"VDD", {}, {},  "POWER"},
      {"VSS", {}, {}, "GROUND"}
  };
  const auto powered_trial = pl::detail::repeater_snapshot(powered, *timed, {0, 0, 1}, tech, {100, 10});
  for (const auto& cell : timed->cells) {
    for (const auto& [name, pin] : tech.macros.at(cell.macro).pins) {
      if (pin.use != "POWER" && pin.use != "GROUND") {
        continue;
      }
      const auto& net = powered_trial.state.floorplan.special_nets.at(pin.use == "POWER" ? 0 : 1);
      EXPECT_NE(std::find(net.connections.begin(), net.connections.end(), ph::Connection{cell.name, name}), net.connections.end());
    }
  }
  auto           obstructed = state;
  const auto&    inserted   = timed->cells.front();
  const auto&    layer      = tech.macros.at(inserted.macro).pins.at(timed->ports.front().first).shapes.front().layer;
  ph::Route_path crossing;
  crossing.layer = layer;
  crossing.steps = {
      ph::Route_point{                       inserted.place.x, inserted.place.y + inserted.place.height / 2, 0},
      ph::Route_point{inserted.place.x + inserted.place.width, inserted.place.y + inserted.place.height / 2, 0}
  };
  obstructed.floorplan.nets.push_back({"already_fixed", {}, {{"FIXED", {crossing}}}, "SIGNAL"});
  EXPECT_THROW(pl::detail::repeater_snapshot(obstructed, *timed, {0, 0, 1}, tech, {100, 10}), std::invalid_argument);

  EXPECT_THROW(pl::detail::repeater_snapshot(state, *timed, {0, 0, 1, false, true}, tech, {100, 10}), std::invalid_argument);
  auto non_signal                       = state;
  non_signal.floorplan.nets.front().use = "CLOCK";
  EXPECT_THROW(pl::detail::repeater_snapshot(non_signal, *timed, {0, 0, 1}, tech, {100, 10}), std::invalid_argument);

  EXPECT_EQ(frame.nets.front().connections,
            (std::vector<ph::Connection>{
                {"u", "X"},
                {"v", "A"}
  }));
  EXPECT_EQ(frame.components, original_components);
  for (const auto& cell : proposal->cells) {
    EXPECT_EQ(cell.place.status, ph::Status::FIXED);
    EXPECT_EQ(cell.place.x % sw, 0);
    EXPECT_EQ(cell.place.y % rh, 0);
    for (const auto& existing : original_components) {
      EXPECT_FALSE(cell.place.x < existing.place.x + existing.place.width && existing.place.x < cell.place.x + cell.place.width
                   && cell.place.y < existing.place.y + existing.place.height
                   && existing.place.y < cell.place.y + cell.place.height);
    }
  }
  // Proposal graph rewiring leaves the source graph's direct connection intact.
  for (auto node : state.graph->body().nodes()) {
    if (node.get_name() == "v") {
      EXPECT_EQ(node.get_sink_pin("A").get_driver_pin().get_master_node().get_name(), "u");
    }
  }
  for (auto node : proposal->graph->body().nodes()) {
    for (const auto& existing : original_components) {
      if (node.get_name() == existing.name) {
        EXPECT_EQ(node.attr(livehd::attrs::place).get(), existing.place);
      }
    }
  }
  auto    tight = state;
  // A prior short route locks the cell, not its unrelated unrouted output.
  // Proposal planning must preserve that cell and the already-routed input.
  Circuit fixed_circuit(false, false);
  auto    fixed_endpoint = state;
  fixed_endpoint.graph   = fixed_circuit.graph;
  for (auto& cell : fixed_endpoint.floorplan.components) {
    if (cell.name == "u") {
      cell.place.status = ph::Status::FIXED;
    }
    for (auto node : fixed_endpoint.graph->body().nodes()) {
      if (node.get_name() == cell.name) {
        node.attr(livehd::attrs::place).set(cell.place);
      }
    }
  }
  EXPECT_FALSE(proposer.propose(fixed_endpoint, {}, "u:X", {100, 10}, deadline));
  ph::Route_path prior_path;
  prior_path.layer = layer;
  prior_path.steps = {
      ph::Route_point{ 0, 0, 0},
      ph::Route_point{sw, 0, 0}
  };
  fixed_endpoint.floorplan.nets.push_back({
      "prior_input",
      {{"PIN", "a"}, {"u", "A"}},
      {{"FIXED", {prior_path}}},
      "SIGNAL"
  });
  const auto fixed_cells    = fixed_endpoint.floorplan.components;
  const auto fixed_nets     = fixed_endpoint.floorplan.nets;
  const auto fixed_proposal = proposer.propose(fixed_endpoint, {}, "u:X", {100, 10}, deadline);
  ASSERT_TRUE(fixed_proposal);
  EXPECT_EQ(fixed_endpoint.floorplan.components, fixed_cells);
  EXPECT_EQ(fixed_endpoint.floorplan.nets, fixed_nets);
  for (auto node : fixed_proposal->graph->body().nodes()) {
    for (const auto& cell : fixed_cells) {
      if (node.get_name() == cell.name) {
        EXPECT_EQ(node.attr(livehd::attrs::place).get(), cell.place);
        EXPECT_EQ(node.get_subnode_io()->get_name(), cell.macro);
      }
    }
    if (node.get_name() == "u") {
      EXPECT_TRUE(node.get_sink_pin("A").get_driver_pin().get_master_node().is_input_node());
    }
  }
  fixed_endpoint.floorplan.nets.back().connections.push_back({"u", "X"});
  EXPECT_FALSE(proposer.propose(fixed_endpoint, {}, "u:X", {100, 10}, deadline));
  fixed_endpoint.floorplan.nets.back().connections.pop_back();
  fixed_endpoint.floorplan.nets.back().wires.front().type = "ROUTED";
  EXPECT_FALSE(proposer.propose(fixed_endpoint, {}, "u:X", {100, 10}, deadline));
  tight.floorplan.rows.clear();
  EXPECT_FALSE(proposer.propose(tight, {}, "u:X", {100, 10}, deadline));
  // The scheduler must abandon a long net atomically when no legal repeater
  // site exists, leaving its original graph, geometry and timer available to
  // the downstream router.
  tight.parasitics                         = std::make_shared<ph::Parasitics>(*state.parasitics);
  tight.parasitics->nets[1].wire_length_um = 1e6;
  pl::Preroute_options stage_options;
  stage_options.tech_profile           = "asap7";
  stage_options.capacity.layers        = {layer};
  stage_options.per_net_budget         = std::chrono::milliseconds(2000);
  stage_options.timing.deadline        = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  stage_options.timing.setup_margin_ps = stage_options.timing.hold_margin_ps = 0;
  pl::Route_technology_rc stage_rc;
  stage_rc.layers[layer]    = {100, 10};
  const auto stage_feedback = session.evaluate(tight.graph, tight.parasitics);
  auto&      stage_timer    = session.whatif();
  const auto stage_mark     = stage_timer.mark();
  const auto stage_wns      = stage_timer.wns();
  const auto rejected_stage = pl::detail::preroute_stage(tight,
                                                         tech,
                                                         stage_rc,
                                                         {
                                                             {0, 0, 1}
  },
                                                         stage_timer,
                                                         config,
                                                         stage_options,
                                                         stage_feedback);
  ASSERT_EQ(rejected_stage.records.size(), 1);
  EXPECT_TRUE(rejected_stage.records.front().attempted);
  EXPECT_EQ(rejected_stage.records.front().reason, "no legal repeater proposal");
  EXPECT_EQ(rejected_stage.repeater_cells, 0);
  EXPECT_EQ(rejected_stage.placed.graph, tight.graph);
  EXPECT_EQ(rejected_stage.placed.floorplan.components, tight.floorplan.components);
  EXPECT_EQ(rejected_stage.placed.floorplan.nets, tight.floorplan.nets);
  EXPECT_EQ(stage_timer.mark(), stage_mark);
  EXPECT_NEAR(stage_timer.wns(), stage_wns, 1e-6);

  EXPECT_FALSE(proposer.propose(state, {}, "u:X", {100, 10}, std::chrono::steady_clock::now()));
  state.floorplan.components.front().place.status = ph::Status::FIXED;
  EXPECT_FALSE(proposer.propose(state, {}, "u:X", {100, 10}, deadline));
}

TEST_F(PhysicalSession, RepeaterTimingValidatesTopologyAndRestoresCellsAndWires) {
  namespace pl = livehd::placement;
  Circuit                             original(false, false), repeated(false, true);
  livehd::opentimer::Physical_session session(options);
  session.evaluate(original.graph, original.rc);
  auto&                                    timer = session.whatif();
  const auto                               net   = original.rc->nets.at(1);
  const std::vector<pl::Preroute_repeater> cells{
      {"b", "sky130_fd_sc_hd__buf_1", "A", "X"}
  };
  std::vector<livehd::physical::Rc_net> branches{repeated.rc->nets.at(1), repeated.rc->nets.at(2)};
  pl::Preroute_timing_limits            limits;
  limits.hold_margin_ps = 0;
  limits.deadline       = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  timer.resize("u", "sky130_fd_sc_hd__buf_2");
  const auto mark  = timer.mark();
  const auto wns   = timer.wns();
  const auto load  = timer.load(net.driver);
  auto       trial = pl::check_preroute_repeater_timing(timer, net, branches, cells, {"y"}, limits);
  ASSERT_TRUE(trial.legal) << trial.reason << " " << trial.pin;
  EXPECT_EQ(timer.mark(), mark);
  EXPECT_EQ(timer.load(net.driver), load);
  EXPECT_NEAR(timer.wns(), wns, 1e-6);
  // A repeated successful trial proves the temporary instance was removed.
  EXPECT_TRUE(pl::check_preroute_repeater_timing(timer, net, branches, cells, {"y"}, limits).legal);
  auto slow                                = branches;
  slow.back().edges.front().resistance_ohm = 1e9;
  trial                                    = pl::check_preroute_repeater_timing(timer, net, slow, cells, {"y"}, limits);
  EXPECT_FALSE(trial.legal);
  EXPECT_EQ(timer.mark(), mark);
  EXPECT_EQ(timer.load(net.driver), load);
  EXPECT_NEAR(timer.wns(), wns, 1e-6);
  auto bad                    = branches;
  bad.back().nodes.back().pin = "other:A";
  EXPECT_THROW(pl::check_preroute_repeater_timing(timer, net, bad, cells, {}, limits), std::invalid_argument);
  bad                          = branches;
  // All pins still occur exactly once, but the buffer drives its own input.
  bad.front().nodes.back().pin = "v:A";
  bad.back().nodes.back().pin  = "b:A";
  EXPECT_THROW(pl::check_preroute_repeater_timing(timer, net, bad, cells, {}, limits), std::invalid_argument);
  auto missing           = cells;
  missing.front().master = "missing_buffer";
  EXPECT_ANY_THROW(pl::check_preroute_repeater_timing(timer, net, branches, missing, {}, limits));
  EXPECT_EQ(timer.mark(), mark);
  EXPECT_NEAR(timer.wns(), wns, 1e-6);
  limits.deadline = std::chrono::steady_clock::now();
  EXPECT_TRUE(pl::check_preroute_repeater_timing(timer, net, branches, cells, {}, limits).timed_out);
  EXPECT_EQ(timer.mark(), mark);
  timer.rollback(0);
}

TEST_F(PhysicalSession, IntegratedPreroutePublishesOnlyAcceptedGeometryAndRestoresTimer) {
  namespace ph = livehd::physical;
  namespace pl = livehd::placement;
  namespace dp = DreamPlace::native;
  Circuit                             circuit(false, false);
  livehd::opentimer::Physical_session session(options);
  session.evaluate(circuit.graph, circuit.rc);
  auto&          timer = session.whatif();
  const auto     wns   = timer.wns();
  ph::Technology tech;
  tech.manufacturing_grid = .001;
  tech.routing_layers     = {"M1", "M2", "M3"};
  for (const auto& name : tech.routing_layers) {
    auto& l         = tech.layers[name];
    l.width         = .018;
    l.direction     = name == "M2" ? "HORIZONTAL" : "VERTICAL";
    l.rules.loaded  = true;
    l.rules.area    = .000666;
    l.rules.spacing = {
        {.018, {}, {}}
    };
    l.rules.properties["LEF58_RIGHTWAYONGRIDONLY"] = {"RIGHTWAYONGRIDONLY ;"};
  }
  tech.cut_layers["V1"].width   = .018;
  tech.cut_layers["V1"].spacing = {.018};
  tech.vias["VIA12"].shapes     = {
      {"M1", {-.009, -.011, .009, .011}},
      {"M2", {-.014, -.009, .014, .009}},
      {"V1", {-.009, -.009, .009, .009}}
  };
  auto& master       = tech.macros["cell"];
  master.klass       = "CORE";
  master.width       = .162;
  master.height      = .270;
  master.pins["A"]   = {"INPUT", "SIGNAL", {{"M1", {.018, .027, .036, .243}}}};
  master.pins["X"]   = {"OUTPUT", "SIGNAL", {{"M1", {.018, .027, .036, .243}}}};
  // Distinct masters prevent unconnected duplicate pin shapes from blocking
  // the physical frame of this isolated internal-net integration fixture.
  tech.macros["buf"] = master;
  tech.macros["buf"].pins.erase("A");
  tech.macros["inv"] = master;
  tech.macros["inv"].pins.erase("X");
  ph::Floorplan fp;
  fp.die = {0, 0, 4000, 4000};
  Ann_place u(1000, 1000, 162, 270), v(2000, 1000, 162, 270);
  u.status = v.status = ph::Status::PLACED;
  fp.components       = {
      {"u", "buf", u},
      {"v", "inv", v}
  };
  fp.nets.push_back({
      "wire",
      {{"u", "X"}, {"v", "A"}},
      {},
      "SIGNAL"
  });
  fp.tracks = {
      {"Y", "M2", 1, 110, 36},
      {"X", "M3", 9, 110, 36}
  };
  // 1045 is one of the M2 tracks (1 + 29*36).
  pl::Preroute_design snapshot;
  auto&               design = snapshot.design;
  snapshot.pin_labels        = {"u:X", "v:A"};
  design.layout              = {0, 0, 4000, 4000, 1, 1};
  design.node_x              = {1000, 2000};
  design.node_y              = {1000, 1000};
  design.node_size_x         = {162, 162};
  design.node_size_y         = {270, 270};
  design.pin_offset_x        = {27, 27};
  design.pin_offset_y        = {45, 45};
  design.pin2node            = {0, 1};
  design.flat_net2pin        = {0, 1};
  design.flat_net2pin_start  = {0, 2};
  design.net_weights         = {1};
  design.build_maps();
  pl::Preroute_options opt;
  opt.tech_profile   = "asap7";  // The synthetic M1/M2/V1 fixture follows its routing layers.
  opt.per_net_budget = std::chrono::milliseconds(2000);
  opt.capacity       = {
      fp.die,
      1,
      1,
      {"M2", "M3"},
      .3
  };
  opt.demand.bins_x = opt.demand.bins_y = 1;
  opt.demand.horizontal_capacity = opt.demand.vertical_capacity = 1. / 36;
  opt.demand.model                                              = dp::RudyModel::rsmt;
  opt.demand.macro_blockages = opt.demand.pin_access = true;
  opt.timing.deadline                                = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  opt.timing.hold_margin_ps                          = 0;
  pl::Route_technology_rc wire;
  wire.layers["M2"]             = {1, .001};
  wire.cut_resistance_ohm["V1"] = 1;
  const auto bindings           = pl::bind_preroute(fp, *circuit.rc, snapshot);
  ASSERT_EQ(bindings.size(), 1);
  EXPECT_EQ(bindings[0].physical_net, 0);
  EXPECT_EQ(bindings[0].demand_net, 0);
  EXPECT_EQ(bindings[0].rc_net, 1);
  EXPECT_FALSE(bindings[0].tie);
  EXPECT_FALSE(bindings[0].dont_touch);
  snapshot.tie_pins.insert("u:X");
  EXPECT_TRUE(pl::bind_preroute(fp, *circuit.rc, snapshot)[0].tie);
  snapshot.tie_pins.clear();
  EXPECT_TRUE(pl::bind_preroute(fp, *circuit.rc, snapshot, {"v:A"})[0].dont_touch);
  EXPECT_THROW(pl::bind_preroute(fp, *circuit.rc, snapshot, {"v:A", "removed:A"}), std::invalid_argument);
  EXPECT_THROW(pl::bind_preroute(fp, *circuit.rc, snapshot, {""}), std::invalid_argument);
  snapshot.dont_touch_pins.insert("v:A");
  EXPECT_TRUE(pl::bind_preroute(fp, *circuit.rc, snapshot)[0].dont_touch);
  EXPECT_TRUE(pl::bind_preroute(fp, *circuit.rc, snapshot, {"u:X"})[0].dont_touch);
  snapshot.dont_touch_pins.insert("removed:A");
  EXPECT_THROW(pl::bind_preroute(fp, *circuit.rc, snapshot), std::invalid_argument);
  snapshot.dont_touch_pins.clear();
  auto renamed         = fp;
  renamed.nets[0].name = "unrelated_name";
  std::reverse(renamed.nets[0].connections.begin(), renamed.nets[0].connections.end());
  EXPECT_EQ(pl::bind_preroute(renamed, *circuit.rc, snapshot)[0].rc_net, 1);
  renamed.nets[0].connections[0].pin = "stale_pin";
  EXPECT_THROW(pl::bind_preroute(renamed, *circuit.rc, snapshot), std::invalid_argument);
  auto duplicate_frame = fp;
  duplicate_frame.nets[0].connections.push_back(duplicate_frame.nets[0].connections.front());
  EXPECT_THROW(pl::bind_preroute(duplicate_frame, *circuit.rc, snapshot), std::invalid_argument);
  auto duplicate_rc = *circuit.rc;
  duplicate_rc.nets[1].nodes.push_back(duplicate_rc.nets[1].nodes.front());
  EXPECT_THROW(pl::bind_preroute(fp, duplicate_rc, snapshot), std::invalid_argument);
  auto duplicate_demand          = snapshot;
  duplicate_demand.pin_labels[1] = duplicate_demand.pin_labels[0];
  EXPECT_THROW(pl::bind_preroute(fp, *circuit.rc, duplicate_demand), std::invalid_argument);
  // The host scheduler must preserve the same short-net behavior, and restore
  // pre-existing caller edits after publishing accepted RC in its own journal.
  pl::Result stage_input;
  stage_input.graph           = circuit.graph;
  stage_input.floorplan       = fp;
  stage_input.parasitics      = circuit.rc;
  stage_input.preroute_design = std::make_shared<pl::Preroute_design>(snapshot);
  pl::Options stage_config;
  stage_config.liberty      = (directory / "cells.lib").string();
  const auto stage_feedback = session.evaluate(circuit.graph, circuit.rc);
  timer.set_net(circuit.rc->nets[1]);
  const auto caller_mark = timer.mark();
  const auto staged      = pl::detail::preroute_stage(stage_input, tech, wire, bindings, timer, stage_config, opt, stage_feedback);
  ASSERT_EQ(staged.records.size(), 1);
  ASSERT_TRUE(staged.records.front().accepted) << staged.records.front().reason;
  EXPECT_EQ(staged.repeater_cells, 0);
  // v:Y belongs to the same instance as the routed v:A, but to another net.
  // Locking v must not prevent repair of that unrelated failing data path.
  Insertion_probe unsafe_pin(timer);
  unsafe_pin.failing_setup_pin = "v:Y";
  const auto unsafe            = pl::preroute(tech, fp, *circuit.rc, snapshot, bindings, wire, unsafe_pin, opt);
  ASSERT_EQ(unsafe.records.size(), 1);
  EXPECT_FALSE(unsafe.records.front().accepted);
  EXPECT_EQ(unsafe.records.front().reason, "initial setup/hold margin");
  EXPECT_TRUE(unsafe.frame.nets.front().wires.empty());
  EXPECT_EQ(unsafe_pin.net_updates, 0);
  EXPECT_EQ(timer.mark(), caller_mark);
  Insertion_probe changed_pin(timer);
  changed_pin.failing_setup_pin       = "v:Y";
  changed_pin.fail_setup_after_update = true;
  const auto changed                  = pl::preroute(tech, fp, *circuit.rc, snapshot, bindings, wire, changed_pin, opt);
  ASSERT_EQ(changed.records.size(), 1);
  EXPECT_FALSE(changed.records.front().accepted);
  EXPECT_EQ(changed.records.front().reason, "routed setup margin");
  EXPECT_TRUE(changed.frame.nets.front().wires.empty());
  EXPECT_GT(changed_pin.net_updates, 0);
  EXPECT_EQ(timer.mark(), caller_mark);
  // The host's explicit control-net mask remains authoritative for other pins.
  auto control_snapshot = snapshot;
  control_snapshot.pin_labels.push_back("v:Y");
  auto& control_design = control_snapshot.design;
  control_design.pin2node.push_back(1);
  control_design.pin_offset_x.push_back(27);
  control_design.pin_offset_y.push_back(45);
  control_design.flat_net2pin.push_back(2);
  control_design.flat_net2pin_start.push_back(3);
  control_design.net_weights.push_back(1);
  control_design.net_mask = {1, 0};
  control_design.build_maps();
  const auto masked = pl::preroute(tech, fp, *circuit.rc, control_snapshot, bindings, wire, unsafe_pin, opt);
  ASSERT_EQ(masked.records.size(), 1);
  EXPECT_TRUE(masked.records.front().accepted) << masked.records.front().reason;
  EXPECT_EQ(timer.mark(), caller_mark);
  EXPECT_FALSE(staged.placed.floorplan.nets.front().wires.empty());
  EXPECT_TRUE(stage_input.floorplan.nets.front().wires.empty());
  EXPECT_EQ(timer.mark(), caller_mark);
  EXPECT_NEAR(timer.wns(), wns, 1e-6);
  auto protected_input    = stage_input;
  auto protected_snapshot = std::make_shared<pl::Preroute_design>(snapshot);
  protected_snapshot->dont_touch_pins.insert("v:A");
  protected_input.preroute_design = protected_snapshot;
  const auto protected_stage
      = pl::detail::preroute_stage(protected_input, tech, wire, bindings, timer, stage_config, opt, stage_feedback);
  ASSERT_EQ(protected_stage.records.size(), 1);
  EXPECT_FALSE(protected_stage.records.front().accepted);
  EXPECT_EQ(protected_stage.records.front().reason, "logical exclusion");
  EXPECT_TRUE(protected_stage.placed.floorplan.nets.front().wires.empty());
  EXPECT_EQ(protected_stage.repeater_cells, 0);
  EXPECT_EQ(timer.mark(), caller_mark);
  EXPECT_NEAR(timer.wns(), wns, 1e-6);
  auto straight_wire                   = wire;
  straight_wire.layers["M1"]           = {1, .001};
  auto straight_input                  = stage_input;
  auto straight_tech                   = tech;
  straight_tech.layers["M1"].direction = "HORIZONTAL";
  straight_input.floorplan.tracks.push_back({"Y", "M1", 19, 110, 36});
  auto straight_options                              = opt;
  straight_options.straight_only                     = true;
  straight_options.capacity.count_only_usable_tracks = true;
  straight_options.capacity.layers                   = {"M1", "M2", "M3"};
  straight_options.straight_routes.emplace("wire",
                                           ph::Shape{
                                               "M1",
                                               {1018, 1126, 2036, 1144}
  });
  const auto direct = pl::detail::preroute_stage(straight_input,
                                                 straight_tech,
                                                 straight_wire,
                                                 bindings,
                                                 timer,
                                                 stage_config,
                                                 straight_options,
                                                 stage_feedback);
  ASSERT_EQ(direct.records.size(), 1);
  ASSERT_TRUE(direct.records.front().accepted) << direct.records.front().reason;
  EXPECT_EQ(direct.records.front().vias, 0);
  EXPECT_EQ(direct.repeater_cells, 0);
  ASSERT_EQ(direct.placed.floorplan.nets.front().wires.size(), 1);
  EXPECT_EQ(direct.placed.floorplan.nets.front().wires.front().paths.size(), 1);
  EXPECT_EQ(timer.mark(), caller_mark);
  EXPECT_NEAR(timer.wns(), wns, 1e-6);
  auto via_options            = straight_options;
  via_options.capacity.layers = opt.capacity.layers;
  via_options.straight_routes.clear();
  via_options.straight_routes.emplace("wire",
                                      ph::Shape{
                                          "M2",
                                          {1000, 1036, 2050, 1054}
  });
  const auto with_vias
      = pl::detail::preroute_stage(stage_input, tech, straight_wire, bindings, timer, stage_config, via_options, stage_feedback);
  ASSERT_EQ(with_vias.records.size(), 1);
  ASSERT_TRUE(with_vias.records.front().accepted) << with_vias.records.front().reason;
  EXPECT_EQ(with_vias.records.front().vias, 2);
  EXPECT_EQ(with_vias.repeater_cells, 0);
  EXPECT_FALSE(with_vias.placed.floorplan.nets.front().wires.empty());
  EXPECT_TRUE(stage_input.floorplan.nets.front().wires.empty());
  EXPECT_EQ(timer.mark(), caller_mark);
  // Same aligned centers, different characterized metal: choose M4 only when
  // its driver-aware timing beats M2, including the extra endpoint vias.
  auto lift_tech  = tech;
  auto lift_input = stage_input;
  lift_tech.routing_layers.push_back("M4");
  lift_tech.layers["M4"] = lift_tech.layers.at("M2");
  for (int n = 2; n <= 3; ++n) {
    const auto cut                                                           = "V" + std::to_string(n);
    lift_tech.cut_layers[cut]                                                = lift_tech.cut_layers.at("V1");
    lift_tech.vias["VIA" + std::to_string(n) + std::to_string(n + 1)].shapes = {
        {    "M" + std::to_string(n), {-.009, -.009, .009, .009}},
        {"M" + std::to_string(n + 1), {-.009, -.009, .009, .009}},
        {                        cut, {-.009, -.009, .009, .009}}
    };
  }
  lift_input.floorplan.tracks.push_back({"X", "M3", 1000, 2000, 1});
  lift_input.floorplan.tracks.push_back({"Y", "M4", 1, 110, 36});
  auto lift_options = via_options;
  lift_options.capacity.layers.push_back("M4");
  lift_options.critical_fraction   = 1;
  auto lift_rc                     = wire;
  lift_rc.layers["M2"]             = {1000, .001};
  lift_rc.layers["M3"]             = {1, .001};
  lift_rc.layers["M4"]             = {1, .001};
  lift_rc.cut_resistance_ohm["V2"] = lift_rc.cut_resistance_ohm["V3"] = 1;
  const auto lift                                                     = [&](const auto& rc_model, const auto& config) {
    return pl::preroute(lift_tech, lift_input.floorplan, *circuit.rc, snapshot, bindings, rc_model, timer, config);
  };
  const auto fast = lift(lift_rc, lift_options);
  ASSERT_EQ(fast.records.size(), 1);
  ASSERT_TRUE(fast.records.front().accepted) << fast.records.front().reason;
  EXPECT_TRUE(fast.records.front().layer_wire_length_um.contains("M4"));
  EXPECT_EQ(fast.records.front().vias, 6);
  EXPECT_TRUE(fast.records.front().critical_layer_search);
  EXPECT_EQ(fast.records.front().layer_candidates, 2);
  EXPECT_EQ(timer.mark(), caller_mark);
  EXPECT_NEAR(timer.wns(), wns, 1e-6);
  auto missing_rc = lift_rc;
  missing_rc.layers.erase("M4");
  const auto uncharacterized = lift(missing_rc, lift_options);
  ASSERT_TRUE(uncharacterized.records.front().accepted) << uncharacterized.records.front().reason;
  EXPECT_FALSE(uncharacterized.records.front().layer_wire_length_um.contains("M4"));
  auto unavailable = lift_options;
  unavailable.capacity.layers.pop_back();
  const auto no_capacity = lift(lift_rc, unavailable);
  ASSERT_TRUE(no_capacity.records.front().accepted) << no_capacity.records.front().reason;
  EXPECT_FALSE(no_capacity.records.front().layer_wire_length_um.contains("M4"));
  lift_options.critical_fraction = 0;
  const auto disabled            = lift(lift_rc, lift_options);
  ASSERT_TRUE(disabled.records.front().accepted) << disabled.records.front().reason;
  EXPECT_FALSE(disabled.records.front().layer_wire_length_um.contains("M4"));
  lift_options.critical_fraction   = 1;
  lift_rc.cut_resistance_ohm["V2"] = lift_rc.cut_resistance_ohm["V3"] = 10000;
  const auto slow                                                     = lift(lift_rc, lift_options);
  ASSERT_TRUE(slow.records.front().accepted) << slow.records.front().reason;
  EXPECT_FALSE(slow.records.front().layer_wire_length_um.contains("M4"));
  EXPECT_EQ(timer.mark(), caller_mark);
  EXPECT_NEAR(timer.wns(), wns, 1e-6);
  auto pg_input         = straight_input;
  auto pg_tech          = straight_tech;
  pg_tech.sites["site"] = {.054, .270, true};
  for (const auto* name : {"buf", "inv"}) {
    auto& cell       = pg_tech.macros.at(name);
    cell.site        = "site";
    cell.pins["VDD"] = {"INOUT", "POWER", {{"M1", {0, .261, .162, .279}}}};
    cell.pins["VSS"] = {"INOUT", "GROUND", {{"M1", {0, -.009, .162, .009}}}};
  }
  pg_tech.via_rules["M2_M1"] = {
      true,
      true,
      {{"M1", "", std::array{0., 0.}},
        {"M2", "", std::array{.002, 0.}},
        {"V1", "", {}, {}, std::array{.036, .036}, ph::Rect{-.009, -.009, .009, .009}}}
  };
  pg_input.floorplan.rows.push_back({"r", "site", 972, 1000, 40, 54, ph::Orientation::N});
  auto deferred_options        = straight_options;
  deferred_options.deferred_pg = pl::power_grid_profile("pg_asap7", pg_tech);
  deferred_options.deferred_pg->stripes.clear();
  deferred_options.deferred_pg->connect = {{{"M1", "M2"}}};
  deferred_options.deferred_pg->macro_connect.clear();
  deferred_options.deferred_pg->pin_layer = "M2";
  const auto with_pg                      = pl::detail::preroute_stage(pg_input,
                                                                       pg_tech,
                                                                       straight_wire,
                                                                       bindings,
                                                                       timer,
                                                                       stage_config,
                                                                       deferred_options,
                                                                       stage_feedback);
  ASSERT_EQ(with_pg.records.size(), 1);
  ASSERT_TRUE(with_pg.records.front().accepted) << with_pg.records.front().reason;
  EXPECT_EQ(with_pg.placed.floorplan.special_nets.size(), 2);
  EXPECT_FALSE(with_pg.placed.floorplan.vias.empty());
  EXPECT_EQ(with_pg.placed.floorplan.nets, direct.placed.floorplan.nets);
  EXPECT_EQ(with_pg.placed.floorplan.components.front().place.status, ph::Status::FIXED);
  EXPECT_TRUE(pg_input.floorplan.special_nets.empty());
  EXPECT_TRUE(pg_input.floorplan.nets.front().wires.empty());
  auto wired_grid_frame    = with_pg.placed.floorplan;
  wired_grid_frame.nets    = pg_input.floorplan.nets;
  auto cached_grid_options = straight_options;
  cached_grid_options.power_grid_occupancy
      = std::make_shared<const ph::Routing_power_grid>(pg_tech, wired_grid_frame, wired_grid_frame.dbu);
  const auto rebuilt_grid_route = pl::preroute(pg_tech,
                                               wired_grid_frame,
                                               *pg_input.parasitics,
                                               *pg_input.preroute_design,
                                               bindings,
                                               straight_wire,
                                               timer,
                                               straight_options);
  const auto cached_grid_route  = pl::preroute(pg_tech,
                                               wired_grid_frame,
                                               *pg_input.parasitics,
                                               *pg_input.preroute_design,
                                               bindings,
                                               straight_wire,
                                               timer,
                                               cached_grid_options);
  ASSERT_TRUE(rebuilt_grid_route.records.front().accepted);
  ASSERT_TRUE(cached_grid_route.records.front().accepted);
  EXPECT_EQ(cached_grid_route.frame.nets, rebuilt_grid_route.frame.nets);
  EXPECT_EQ(cached_grid_route.parasitics.nets, rebuilt_grid_route.parasitics.nets);
  EXPECT_EQ(cached_grid_route.frame.special_nets, wired_grid_frame.special_nets);
  EXPECT_EQ(timer.mark(), caller_mark);
  EXPECT_NEAR(timer.wns(), wns, 1e-6);
  auto no_pg_capacity                           = deferred_options;
  no_pg_capacity.capacity.max_preroute_fraction = 0;
  no_pg_capacity.capacity.bins_x = no_pg_capacity.demand.bins_x = 8;
  const auto pg_rejected
      = pl::detail::preroute_stage(pg_input, pg_tech, straight_wire, bindings, timer, stage_config, no_pg_capacity, stage_feedback);
  ASSERT_EQ(pg_rejected.records.size(), 1);
  EXPECT_FALSE(pg_rejected.records.front().accepted);
  EXPECT_EQ(pg_rejected.placed.floorplan.nets, pg_input.floorplan.nets);
  EXPECT_EQ(pg_rejected.placed.floorplan.components, pg_input.floorplan.components);
  EXPECT_EQ(pg_rejected.placed.floorplan.special_nets.size(), 2);
  EXPECT_EQ(timer.mark(), caller_mark);
  EXPECT_NEAR(timer.wns(), wns, 1e-6);
  straight_options.straight_routes.clear();
  const auto unselected = pl::detail::preroute_stage(straight_input,
                                                     straight_tech,
                                                     straight_wire,
                                                     bindings,
                                                     timer,
                                                     stage_config,
                                                     straight_options,
                                                     stage_feedback);
  ASSERT_EQ(unselected.records.size(), 1);
  EXPECT_EQ(unselected.records.front().reason, "no retained two-pin SAT alignment");
  EXPECT_EQ(unselected.placed.floorplan.nets, straight_input.floorplan.nets);
  EXPECT_EQ(timer.mark(), caller_mark);
  auto expired_options            = opt;
  expired_options.timing.deadline = std::chrono::steady_clock::now();
  const auto expired_stage
      = pl::detail::preroute_stage(stage_input, tech, wire, bindings, timer, stage_config, expired_options, stage_feedback);
  ASSERT_EQ(expired_stage.records.size(), 1);
  EXPECT_EQ(expired_stage.records.front().reason, "stage deadline");
  EXPECT_EQ(expired_stage.placed.floorplan.nets, fp.nets);
  EXPECT_EQ(timer.mark(), caller_mark);
  auto broken_options            = opt;
  broken_options.capacity.layers = {"uncharacterized"};
  EXPECT_THROW(pl::detail::preroute_stage(stage_input, tech, wire, bindings, timer, stage_config, broken_options, stage_feedback),
               std::invalid_argument);
  EXPECT_EQ(timer.mark(), caller_mark);
  timer.rollback(0);
  auto setup_expired            = opt;
  setup_expired.timing.deadline = std::chrono::steady_clock::now();
  const auto expired_setup      = pl::preroute(tech, fp, *circuit.rc, snapshot, bindings, wire, timer, setup_expired);
  ASSERT_EQ(expired_setup.records.size(), 1);
  EXPECT_EQ(expired_setup.records.front().reason, "stage deadline");
  EXPECT_TRUE(expired_setup.timings_ms.contains("setup_deadline"));
  EXPECT_EQ(expired_setup.frame.nets, fp.nets);
  EXPECT_EQ(expired_setup.frame.components, fp.components);
  EXPECT_EQ(timer.mark(), 0);
  EXPECT_NEAR(timer.wns(), wns, 1e-6);
  auto capped_options      = opt;
  capped_options.max_sites = 999;
  const auto capped        = pl::preroute(tech, fp, *circuit.rc, snapshot, bindings, wire, timer, capped_options);
  ASSERT_EQ(capped.records.size(), 1);
  EXPECT_EQ(capped.records.front().reason, "maximum site span");
  EXPECT_FALSE(capped.records.front().attempted);
  EXPECT_EQ(capped.frame.nets, fp.nets);
  EXPECT_EQ(timer.mark(), 0);
  EXPECT_NEAR(timer.wns(), wns, 1e-6);
  capped_options.max_sites = 1000;
  EXPECT_TRUE(pl::preroute_span_exclusion(design, 0, capped_options).empty());
  auto shifted_design             = design;
  shifted_design.pin_offset_y[1] += 10;
  capped_options.max_rows         = 9;
  EXPECT_EQ(pl::preroute_span_exclusion(shifted_design, 0, capped_options), "maximum row span");
  capped_options.max_rows = 10;
  EXPECT_TRUE(pl::preroute_span_exclusion(shifted_design, 0, capped_options).empty());
  capped_options.max_rows = -1;
  EXPECT_THROW(pl::preroute_span_exclusion(design, 0, capped_options), std::invalid_argument);
  const auto result = pl::preroute(tech, fp, *circuit.rc, snapshot, bindings, wire, timer, opt);
  ASSERT_EQ(result.records.size(), 1);
  ASSERT_TRUE(result.records[0].accepted) << result.records[0].reason;
  EXPECT_TRUE(result.records[0].attempted);
  EXPECT_EQ(result.records[0].pin_count, 2);
  EXPECT_DOUBLE_EQ(result.records[0].estimated_length_um, circuit.rc->nets[1].wire_length_um);
  EXPECT_DOUBLE_EQ(result.records[0].layer_wire_length_um.at("M2"), result.records[0].wire_length_um);
  ASSERT_EQ(result.frame.nets[0].wires.size(), 1);
  EXPECT_EQ(result.frame.nets[0].wires[0].type, "FIXED");
  EXPECT_EQ(result.frame.components[0].place.status, ph::Status::FIXED);
  EXPECT_EQ(result.frame.components[1].place.status, ph::Status::FIXED);
  EXPECT_TRUE(fp.nets[0].wires.empty());
  EXPECT_EQ(fp.components[0].place.status, ph::Status::PLACED);
  EXPECT_NE(result.parasitics.nets[1], circuit.rc->nets[1]);
  const auto resumed = pl::preroute(tech, result.frame, result.parasitics, snapshot, bindings, wire, timer, opt);
  EXPECT_EQ(resumed.frame.nets, result.frame.nets);
  EXPECT_EQ(resumed.frame.components, result.frame.components);
  ASSERT_EQ(resumed.records.size(), 1);
  EXPECT_EQ(resumed.records.front().reason, "not an unwired signal net");
  const auto expired_fixed = pl::preroute(tech, result.frame, result.parasitics, snapshot, bindings, wire, timer, setup_expired);
  ASSERT_EQ(expired_fixed.records.size(), 1);
  EXPECT_EQ(expired_fixed.records.front().reason, "stage deadline");
  EXPECT_EQ(expired_fixed.frame.nets, result.frame.nets);
  EXPECT_EQ(expired_fixed.frame.components, result.frame.components);
  EXPECT_EQ(expired_fixed.frame.vias, result.frame.vias);
  EXPECT_EQ(expired_fixed.parasitics.nets, result.parasitics.nets);
  EXPECT_EQ(timer.mark(), 0);
  EXPECT_NEAR(timer.wns(), wns, 1e-6);
  auto stale_fixed = result.frame;
  stale_fixed.nets.front().connections.pop_back();
  EXPECT_THROW(pl::preroute(tech, stale_fixed, result.parasitics, snapshot, bindings, wire, timer, opt), std::invalid_argument);

  EXPECT_EQ(timer.mark(), 0);
  EXPECT_NEAR(timer.wns(), wns, 1e-6);
  {
    // A two-branch repeater tree can publish only if both branches route.
    Circuit                             tree_circuit(false, true);
    livehd::opentimer::Physical_session tree_session(options);
    tree_session.evaluate(tree_circuit.graph, tree_circuit.rc);
    auto&      tree_timer                       = tree_session.whatif();
    const auto tree_wns                         = tree_timer.wns();
    auto       tree_tech                        = tech;
    tree_tech.macros["middle"]                  = master;
    tree_tech.macros["middle"].pins["X"].shapes = {
        {"M1", {.126, .027, .144, .243}}
    };
    auto      tree_frame = fp;
    Ann_place middle(1500, 1000, 162, 270);
    middle.status = ph::Status::PLACED;
    tree_frame.components.push_back({"b", "middle", middle});
    tree_frame.nets = {
        { "first", {{"u", "X"}, {"b", "A"}}, {}, "SIGNAL"},
        {"second", {{"b", "X"}, {"v", "A"}}, {}, "SIGNAL"}
    };
    pl::Preroute_design tree_snapshot;
    auto&               td   = tree_snapshot.design;
    tree_snapshot.pin_labels = {"u:X", "b:A", "b:X", "v:A"};
    td.layout                = design.layout;
    td.node_x                = {1000, 2000, 1500};
    td.node_y                = {1000, 1000, 1000};
    td.node_size_x           = {162, 162, 162};
    td.node_size_y           = {270, 270, 270};
    td.pin_offset_x          = {27, 27, 135, 27};
    td.pin_offset_y          = {45, 45, 45, 45};
    td.pin2node              = {0, 2, 2, 1};
    td.flat_net2pin          = {0, 1, 2, 3};
    td.flat_net2pin_start    = {0, 2, 4};
    td.net_weights           = {1, 1};
    td.build_maps();
    const auto tree_bindings = pl::bind_preroute(tree_frame, *tree_circuit.rc, tree_snapshot);
    auto trial = pl::preroute_tree(tree_tech, tree_frame, *tree_circuit.rc, tree_snapshot, tree_bindings, wire, tree_timer, opt);
    ASSERT_TRUE(trial.result) << trial.reason;
    ASSERT_EQ(trial.result->records.size(), 2);
    EXPECT_EQ(trial.timings_ms, trial.result->timings_ms);
    EXPECT_TRUE(trial.result->records[0].accepted);
    EXPECT_TRUE(trial.result->records[1].accepted);
    auto mixed_bindings                    = tree_bindings;
    mixed_bindings.front().dont_touch      = true;
    auto limited                           = opt;
    limited.capacity.max_preroute_fraction = 0;
    limited.timing.deadline                = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    const auto mixed
        = pl::preroute_tree(tree_tech, tree_frame, *tree_circuit.rc, tree_snapshot, mixed_bindings, wire, tree_timer, limited);
    EXPECT_FALSE(mixed.result);
    EXPECT_EQ(mixed.reason, "tree branch first: logical exclusion");
    EXPECT_TRUE(mixed.capacity_detail.empty());
    EXPECT_TRUE(mixed.timings_ms.contains("occupancy"));
    EXPECT_TRUE(mixed.timings_ms.contains("capacity"));
    EXPECT_TRUE(mixed.timings_ms.contains("search_and_timing"));
    auto expired_tree_options            = opt;
    expired_tree_options.timing.deadline = std::chrono::steady_clock::now();
    const auto expired_tree              = pl::preroute_tree(tree_tech,
                                                             tree_frame,
                                                             *tree_circuit.rc,
                                                             tree_snapshot,
                                                             tree_bindings,
                                                             wire,
                                                             tree_timer,
                                                             expired_tree_options);
    EXPECT_FALSE(expired_tree.result);
    EXPECT_TRUE(expired_tree.timings_ms.contains("setup_deadline"));
    EXPECT_EQ(tree_timer.mark(), 0);
    ASSERT_EQ(mixed.rejected_records.size(), 2);
    // Atomic preflight rejects the tree without routing an eligible sibling.
    EXPECT_FALSE(mixed.timings_ms.contains("pin_escape_setup"));
    EXPECT_TRUE(mixed.rejected_records.back().capacity_detail.empty());
    EXPECT_EQ(mixed.rejected_records.back().reason, "tree rollback: ineligible sibling");
    for (const auto& record : mixed.rejected_records) {
      EXPECT_FALSE(record.attempted);
      EXPECT_FALSE(record.accepted);
    }
    auto repeated_frame                           = tree_frame;
    repeated_frame.components.back().macro        = "sky130_fd_sc_hd__buf_1";
    repeated_frame.components.back().place.status = ph::Status::FIXED;
    tree_tech.macros["sky130_fd_sc_hd__buf_1"]    = tree_tech.macros.at("middle");
    const std::vector<pl::Preroute_repeater> repeaters{
        {"b", "sky130_fd_sc_hd__buf_1", "A", "X"}
    };
    auto repeated_trial = pl::preroute_repeated_tree(tree_tech,
                                                     repeated_frame,
                                                     *tree_circuit.rc,
                                                     tree_snapshot,
                                                     tree_bindings,
                                                     wire,
                                                     timer,
                                                     opt,
                                                     circuit.rc->nets.at(1),
                                                     repeaters,
                                                     {"y"});
    ASSERT_TRUE(repeated_trial.result) << repeated_trial.reason;
    EXPECT_EQ(timer.mark(), 0);
    EXPECT_NEAR(timer.wns(), wns, 1e-6);
    EXPECT_EQ(repeated_trial.result->records.size(), 2);
    // Separate the downstream access rows. This fixture has no M2/M3 via,
    // so that branch cannot route, while both pins still have legal accesses
    // and the upstream branch can route on M2.
    tree_tech.macros["middle"].pins["X"].shapes = {
        {"M1", {.126, .180, .144, .243}}
    };
    tree_tech.macros["inv"].pins["A"].shapes = {
        {"M1", {.018, .027, .036, .090}}
    };
    td.pin_offset_y[2] = 225;
    trial = pl::preroute_tree(tree_tech, tree_frame, *tree_circuit.rc, tree_snapshot, tree_bindings, wire, tree_timer, opt);
    EXPECT_FALSE(trial.result);
    tree_tech.macros["sky130_fd_sc_hd__buf_1"] = tree_tech.macros.at("middle");
    repeated_trial                             = pl::preroute_repeated_tree(tree_tech,
                                                                            repeated_frame,
                                                                            *tree_circuit.rc,
                                                                            tree_snapshot,
                                                                            tree_bindings,
                                                                            wire,
                                                                            timer,
                                                                            opt,
                                                                            circuit.rc->nets.at(1),
                                                                            repeaters,
                                                                            {"y"});
    EXPECT_FALSE(repeated_trial.result);
    EXPECT_EQ(timer.mark(), 0);
    EXPECT_NEAR(timer.wns(), wns, 1e-6);
    ASSERT_EQ(trial.rejected_records.size(), 2);
    EXPECT_TRUE(std::any_of(trial.rejected_records.begin(), trial.rejected_records.end(), [](const auto& record) {
      return record.reason == "tree rollback";
    }));
    for (const auto& record : trial.rejected_records) {
      EXPECT_FALSE(record.accepted);
    }
    for (const auto& net : tree_frame.nets) {
      EXPECT_TRUE(net.wires.empty());
    }
    for (const auto& cell : tree_frame.components) {
      EXPECT_EQ(cell.place.status, ph::Status::PLACED);
    }
    EXPECT_EQ(tree_timer.mark(), 0);
    EXPECT_NEAR(tree_timer.wns(), tree_wns, 1e-6);
    EXPECT_THROW(pl::preroute_tree(tree_tech, tree_frame, *tree_circuit.rc, tree_snapshot, {}, wire, tree_timer, opt),
                 std::invalid_argument);
  }
  {
    // Exercise automatic planning through successful whole-tree publication,
    // with real Liberty timing and the synthetic routing grid above.
    Circuit buffered(false, false);
    auto    buffered_tech       = tech;
    buffered_tech.sites["site"] = {.027, .270, true};
    for (const auto* name : {"sky130_fd_sc_hd__buf_1", "sky130_fd_sc_hd__buf_2"}) {
      auto cell                          = master;
      cell.site                          = "site";
      cell.symmetry_x                    = true;
      cell.pins["X"].shapes.front().rect = {.126, .027, .144, .243};
      buffered_tech.macros[name]         = cell;
    }
    buffered_tech.macros["sky130_fd_sc_hd__inv_1"] = tech.macros.at("inv");
    pl::Result input                               = stage_input;
    input.graph                                    = buffered.graph;
    input.parasitics                               = buffered.rc;
    auto& bf                                       = input.floorplan;
    bf.die                                         = {0, 0, 24000, 4000};
    bf.components[0].macro                         = "sky130_fd_sc_hd__buf_1";
    bf.components[1].macro                         = "sky130_fd_sc_hd__inv_1";
    bf.components[1].place.x                       = 21000;
    bf.rows                                        = {
        {"r", "site", 0, 1000, 888, 27, ph::Orientation::N}
    };
    bf.tracks[1].count = 666;
    input.parasitics->cells.clear();
    for (const auto& cell : bf.components) {
      input.parasitics->cells.push_back({cell.name,
                                         cell.place.x,
                                         cell.place.y,
                                         unsigned(cell.place.orientation),
                                         cell.macro,
                                         cell.place.width,
                                         cell.place.height});
      for (auto node : input.graph->body().nodes()) {
        if (node.get_name() == cell.name) {
          node.attr(livehd::attrs::place).set(cell.place);
        }
      }
    }
    input.parasitics->nets[1].wire_length_um = 20;
    auto demand                              = std::make_shared<pl::Preroute_design>(snapshot);
    demand->design.layout.xh                 = 24000;
    demand->design.node_x[1]                 = 21000;
    demand->design.pin_offset_x[0]           = 135;
    demand->design.pin_offset_y              = {135, 135};
    input.pin_locations                      = {
        {"u:X",  {1135, 1135}},
        {"v:A", {21027, 1135}}
    };
    input.preroute_design = demand;
    livehd::opentimer::Physical_session buffered_session(options);
    const auto                          before = buffered_session.evaluate(input.graph, input.parasitics);
    auto&                               bt     = buffered_session.whatif();
    auto                                bo     = opt;
    bo.capacity.bounds                         = bf.die;
    bo.timing.setup_margin_ps = bo.timing.hold_margin_ps = 0;
    bo.per_net_budget                                    = std::chrono::milliseconds(2000);
    bo.timing.deadline                                   = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    auto brc                                             = wire;
    brc.layers["M2"]                                     = {100, 10};
    Insertion_probe capped_probe(bt);
    auto            capped_tree_options = bo;
    capped_tree_options.max_sites       = 1000;
    const auto capped_tree              = pl::detail::preroute_stage(input,
                                                                     buffered_tech,
                                                                     brc,
                                                                     {
                                                                         {0, 0, 1}
    },
                                                                     capped_probe,
                                                                     stage_config,
                                                                     capped_tree_options,
                                                                     before);
    EXPECT_EQ(capped_probe.additions, 0);
    EXPECT_EQ(capped_tree.repeater_cells, 0);
    ASSERT_EQ(capped_tree.records.size(), 1);
    EXPECT_EQ(capped_tree.records.front().reason, "maximum site span");
    EXPECT_EQ(capped_tree.placed.floorplan.nets, input.floorplan.nets);
    EXPECT_EQ(capped_tree.placed.floorplan.components, input.floorplan.components);
    EXPECT_EQ(bt.mark(), 0);
    auto capacity_limited                           = bo;
    capacity_limited.capacity.max_preroute_fraction = 0;
    capacity_limited.timing.deadline                = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    const auto capacity_rejected                    = pl::detail::preroute_stage(input,
                                                                                 buffered_tech,
                                                                                 brc,
                                                                                 {
                                                                                     {0, 0, 1}
    },
                                                                                 bt,
                                                                                 stage_config,
                                                                                 capacity_limited,
                                                                                 before);
    ASSERT_EQ(capacity_rejected.records.size(), 1);
    EXPECT_FALSE(capacity_rejected.records.front().accepted);
    EXPECT_NE(capacity_rejected.records.front().capacity_detail.find("tree branch "), std::string::npos);
    EXPECT_EQ(capacity_rejected.repeater_cells, 0);
    for (const auto* phase : {"repeater_initial", "repeater_proposal", "repeater_snapshot", "repeater_tree"}) {
      ASSERT_TRUE(capacity_rejected.timings_ms.contains(phase));
      EXPECT_EQ(capacity_rejected.timings_ms.at(phase), capacity_rejected.records.front().timings_ms.at(phase));
      EXPECT_GE(capacity_rejected.timings_ms.at(phase), 0);
    }
    EXPECT_GE(capacity_rejected.timings_ms.at("repeater_trials"),
              capacity_rejected.timings_ms.at("repeater_initial") + capacity_rejected.timings_ms.at("repeater_proposal")
                  + capacity_rejected.timings_ms.at("repeater_snapshot") + capacity_rejected.timings_ms.at("repeater_tree"));
    ASSERT_TRUE(capacity_rejected.timings_ms.contains("repeater_route_occupancy"));
    ASSERT_TRUE(capacity_rejected.records.front().timings_ms.contains("route_occupancy"));
    EXPECT_EQ(capacity_rejected.timings_ms.at("repeater_route_occupancy"),
              capacity_rejected.records.front().timings_ms.at("route_occupancy"));
    EXPECT_EQ(capacity_rejected.placed.floorplan.nets, input.floorplan.nets);
    EXPECT_EQ(bt.mark(), 0);
    Insertion_probe probe(bt);
    const auto      original_wns  = bt.wns();
    const auto      accepted_tree = pl::detail::preroute_stage(input,
                                                               buffered_tech,
                                                               brc,
                                                               {
                                                                   {0, 0, 1}
    },
                                                               probe,
                                                               stage_config,
                                                               bo,
                                                               before);
    std::string     reasons;
    for (const auto& record : accepted_tree.records) {
      reasons += record.reason + "; ";
    }
    ASSERT_EQ(accepted_tree.repeater_trees, 1) << reasons;
    EXPECT_TRUE(accepted_tree.timings_ms.contains("repeater_capacity_seed"));
    EXPECT_TRUE(accepted_tree.timings_ms.contains("repeater_route_occupancy"));
    auto pg_tree_input = input;
    pg_tree_input.floorplan.special_nets.push_back(
        {"VDD", {}, {{"FIXED", {{"M2", 18, {}, {ph::Route_point{0, 3000, 0}, ph::Route_point{24000, 3000, 0}}}}}}, "POWER"});
    auto pg_tree_options            = bo;
    pg_tree_options.timing.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    const auto pg_tree              = pl::detail::preroute_stage(pg_tree_input,
                                                                 buffered_tech,
                                                                 brc,
                                                                 {
                                                                     {0, 0, 1}
    },
                                                                 bt,
                                                                 stage_config,
                                                                 pg_tree_options,
                                                                 before);
    ASSERT_EQ(pg_tree.repeater_trees, 1);
    EXPECT_TRUE(pg_tree.timings_ms.contains("repeater_capacity_seed"));
    EXPECT_TRUE(pg_tree.timings_ms.contains("power_grid_occupancy"));
    EXPECT_EQ(pg_tree.placed.floorplan.special_nets, pg_tree_input.floorplan.special_nets);
    EXPECT_TRUE(input.floorplan.special_nets.empty());
    EXPECT_EQ(bt.mark(), 0);
    ASSERT_TRUE(accepted_tree.repeater_length_limit_um);
    EXPECT_LT(*accepted_tree.repeater_length_limit_um, input.parasitics->nets[1].wire_length_um);
    EXPECT_TRUE(accepted_tree.repeater_classification_complete);
    EXPECT_EQ(accepted_tree.repeater_candidates, 1);
    EXPECT_EQ(accepted_tree.repeater_margin_eligible, 1);
    EXPECT_EQ(accepted_tree.repeater_attempted, 1);
    EXPECT_GT(accepted_tree.repeater_cells, 0);
    EXPECT_EQ(accepted_tree.placed.floorplan.components.size(), input.floorplan.components.size() + accepted_tree.repeater_cells);
    EXPECT_NE(accepted_tree.placed.graph, input.graph);
    for (const auto& record : accepted_tree.records) {
      EXPECT_TRUE(record.accepted) << record.reason;
    }
    EXPECT_TRUE(input.floorplan.nets.front().wires.empty());
    EXPECT_EQ(bt.mark(), 0);
    EXPECT_NEAR(bt.wns(), original_wns, 1e-6);
    // The final additions install the accepted tree in the stage journal.
    // Fail after its first cell, rather than during a proposal check.
    Insertion_probe broken(bt);
    ASSERT_GE(probe.additions, accepted_tree.repeater_cells);
    broken.fail_at     = probe.additions - accepted_tree.repeater_cells + 1;
    bo.timing.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    EXPECT_THROW(pl::detail::preroute_stage(input,
                                            buffered_tech,
                                            brc,
                                            {
                                                {0, 0, 1}
    },
                                            broken,
                                            stage_config,
                                            bo,
                                            before),
                 std::runtime_error);
    EXPECT_EQ(broken.additions, broken.fail_at);
    EXPECT_EQ(bt.mark(), 0);
    EXPECT_NEAR(bt.wns(), original_wns, 1e-6);
    // Physical net use remains authoritative even if a caller omitted the
    // corresponding logical binding flag. Do not turn a clock/power tree into
    // SIGNAL branches during repeater materialization.
    for (const auto* use : {"CLOCK", "POWER"}) {
      auto non_signal                       = input;
      non_signal.floorplan.nets.front().use = use;
      bo.timing.deadline                    = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      const auto excluded_tree              = pl::detail::preroute_stage(non_signal,
                                                                         buffered_tech,
                                                                         brc,
                                                                         {
                                                                             {0, 0, 1}
      },
                                                                         bt,
                                                                         stage_config,
                                                                         bo,
                                                                         before);
      ASSERT_EQ(excluded_tree.records.size(), 1);
      EXPECT_EQ(excluded_tree.records.front().reason, "not an unwired signal net");
      EXPECT_EQ(excluded_tree.repeater_cells, 0);
      EXPECT_EQ(excluded_tree.placed.floorplan.nets, non_signal.floorplan.nets);
      EXPECT_EQ(bt.mark(), 0);
    }
  }
  opt.capacity.max_preroute_fraction = 0;
  const auto rejected                = pl::preroute(tech,
                                                    fp,
                                                    *circuit.rc,
                                                    snapshot,
                                                    {
                                                        {0, 0, 1}
  },
                                                    wire,
                                                    timer,
                                                    opt);
  EXPECT_FALSE(rejected.records[0].accepted);
  EXPECT_EQ(rejected.records[0].reason, "pre-route capacity fraction");
  EXPECT_FALSE(rejected.records[0].capacity_detail.empty());
  EXPECT_TRUE(rejected.records[0].timings_ms.contains("capacity_precheck"));
  EXPECT_FALSE(rejected.records[0].timings_ms.contains("rc"));
  EXPECT_TRUE(rejected.records[0].timings_ms.contains("remaining_demand"));
  EXPECT_TRUE(rejected.records[0].timings_ms.contains("demand_precheck"));
  EXPECT_TRUE(rejected.frame.nets[0].wires.empty());
  EXPECT_EQ(rejected.frame.components[0].place.status, ph::Status::PLACED);
  EXPECT_EQ(rejected.parasitics.nets, circuit.rc->nets);
  EXPECT_NEAR(timer.wns(), wns, 1e-6);
  const auto excluded = pl::preroute(tech,
                                     fp,
                                     *circuit.rc,
                                     snapshot,
                                     {
                                         {0, 0, 1, false, true, false, false}
  },
                                     wire,
                                     timer,
                                     opt);
  EXPECT_FALSE(excluded.records[0].accepted);
  EXPECT_EQ(excluded.records[0].reason, "logical exclusion");
  EXPECT_FALSE(excluded.records[0].attempted);
  EXPECT_EQ(excluded.records[0].pin_count, 2);
  EXPECT_TRUE(excluded.records[0].layer_wire_length_um.empty());
  EXPECT_TRUE(excluded.frame.nets[0].wires.empty());
  EXPECT_THROW(pl::preroute(tech,
                            fp,
                            *circuit.rc,
                            snapshot,
                            {
                                {0, 0, 0}
  },
                            wire,
                            timer,
                            opt),
               std::invalid_argument);
  snapshot.pin_labels[1] = "wrong:A";
  EXPECT_THROW(pl::preroute(tech,
                            fp,
                            *circuit.rc,
                            snapshot,
                            {
                                {0, 0, 1}
  },
                            wire,
                            timer,
                            opt),
               std::invalid_argument);
  snapshot.pin_labels[1] = "v:A";
  // Pin labels follow pin2node, even when the net CSR traverses pins backwards.
  design.flat_net2pin    = {1, 0};
  design.build_maps();
  const auto permuted = pl::preroute(tech,
                                     fp,
                                     *circuit.rc,
                                     snapshot,
                                     {
                                         {0, 0, 1}
  },
                                     wire,
                                     timer,
                                     opt);
  ASSERT_EQ(permuted.records.size(), 1);
  EXPECT_EQ(permuted.records[0].reason, "pre-route capacity fraction");
  EXPECT_EQ(timer.mark(), 0);
  // Force a maze transition whose cut enclosure needs a wider path. Width
  // definitions must follow the same acceptance transaction as wires and RC.
  fp.components[1].place.y                            += 540;
  design.node_y[1]                                    += 540;
  tech.cut_layers["V2"]                                = tech.cut_layers.at("V1");
  tech.cut_layers["V2"].properties["LEF58_CUTCLASS"]   = {"CUTCLASS square WIDTH .018 ;"};
  tech.cut_layers["V2"].properties["LEF58_ENCLOSURE"]  = {"ENCLOSURE CUTCLASS square EOL .01825 .005 .001 ;"};
  tech.vias["VIA23"].shapes                            = {
      {"M2", {-.014, -.009, .014, .009}},
      {"M3", {-.009, -.014, .009, .014}},
      {"V2", {-.009, -.009, .009, .009}}
  };
  for (const auto* layer : {"M2", "M3"}) {
    tech.layers[layer].rules.properties["LEF58_WIDTHTABLE"] = {"WIDTHTABLE .018 .090 ;"};
  }
  wire.layers["M3"]             = wire.layers.at("M2");
  wire.cut_resistance_ohm["V2"] = 1;
  opt.patterns                  = 100000;
  opt.timing.deadline           = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  const auto wide_rejected      = pl::preroute(tech, fp, *circuit.rc, snapshot, bindings, wire, timer, opt);
  ASSERT_EQ(wide_rejected.records.size(), 1);
  EXPECT_EQ(wide_rejected.records[0].reason, "pre-route capacity fraction");
  EXPECT_TRUE(wide_rejected.frame.nondefault_rules.empty());
  EXPECT_TRUE(wide_rejected.frame.nets[0].wires.empty());
  EXPECT_EQ(wide_rejected.parasitics.nets, circuit.rc->nets);
  opt.capacity.max_preroute_fraction = .3;
  const auto wide                    = pl::preroute(tech, fp, *circuit.rc, snapshot, bindings, wire, timer, opt);
  ASSERT_EQ(wide.records.size(), 1);
  ASSERT_TRUE(wide.records[0].accepted) << wide.records[0].reason;
  EXPECT_FALSE(wide.frame.nondefault_rules.empty());
  EXPECT_TRUE(fp.nondefault_rules.empty());
  EXPECT_TRUE(fp.nets[0].wires.empty());
  EXPECT_EQ(timer.mark(), 0);
  EXPECT_NEAR(timer.wns(), wns, 1e-6);
}
