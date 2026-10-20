// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "resize_engine.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>

#include "attrs.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "pass/opentimer/pass_opentimer.hpp"
#include "rapidjson/document.h"

namespace livehd::placement {
namespace {
namespace gu = graph_util;
// Small sky130 netlists with a controlled frame; every test runs the new engine
// on the persistent what-if timer of one Physical_session.
class Resize_engine : public ::testing::Test {
protected:
  hhds::GraphLibrary                                   source, flat, stretched;
  std::shared_ptr<hhds::Graph>                         graph;
  Options                                              options;
  std::unique_ptr<livehd::opentimer::Physical_session> session;
  double                                               period_ns = 10;
  void                                                 SetUp() override {
    options.fp_place = options.final_cells = options.macro_snap = false;
    options.scale_macro_density = options.exact_macro_density = options.adaptive_density = options.routability = false;
    options.macro_overlap_weight = options.macro_jitter = 0;
    options.io_iterations = options.schedule_iterations = 0;
    options.halo_x = options.halo_y = 0;
    options.utilization             = .5;
    options.density                 = .8;
    options.lefs = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
    options.liberty               = "inou/lefdef/tests/data/sky130hd.lib";
    options.iterations            = 10;
    options.macro_timing_interval = 0;
    options.bins                  = 8;
    options.resize_engine         = "new";
    options.output_dir
        = std::string(std::getenv("TEST_TMPDIR")) + "/" + ::testing::UnitTest::GetInstance()->current_test_info()->name();
    std::filesystem::create_directories(options.output_dir);
    const auto io = source.create_io("top");
    io->add_input("a", 0);
    io->set_bits("a", 1);
    io->add_output("y", 0);
    io->set_bits("y", 1);
    graph = io->create_graph();
    gu::set_bits(graph->get_input_pin("a"), 1);
  }
  hhds::Pin_class gate(const std::string& master, const std::string& name, hhds::Pin_class driver) {
    const auto out_name = master.find("__buf_") != std::string::npos ? "X" : "Y";
    auto       io       = source.find_io(master);
    if (!io) {
      io = source.create_io(master);
      io->add_input("A", 0);
      io->set_bits("A", 1);
      io->add_output(out_name, 0);
      io->set_bits(out_name, 1);
    }
    auto n = gu::create_typed_node(*graph, Ntype_op::Sub);
    n.set_subnode(io);
    n.set_name(name);
    n.create_sink_pin("A").connect_driver(driver);
    auto out = n.create_driver_pin(out_name);
    gu::set_bits(out, 1);
    return out;
  }
  void output(const std::string& name, unsigned position, hhds::Pin_class driver) {
    if (name != "y") {
      graph->get_io()->add_output(name, position);
      graph->get_io()->set_bits(name, 1);
    }
    graph->get_output_pin(name).connect_driver(driver);
  }
  Timing_feedback timing(const Result& state) {
    start_session();
    return session->evaluate(state.graph, state.parasitics);
  }
  void start_session() {
    if (!session) {
      const auto sdc = options.output_dir + "/clock.sdc";
      std::ofstream(sdc) << std::format(
          "create_clock -name virtual -period {}\nset_input_delay -clock virtual 0 [all_inputs]\nset_output_delay -clock virtual 0 "
          "[all_outputs]\n",
          period_ns);
      Eprp_var var;
      var.dict = {
          {"files", options.liberty + "," + sdc},
          {  "top",                       "top"},
          { "hier",                     "false"}
      };
      var.set_stage_labels(var.dict);
      session        = std::make_unique<livehd::opentimer::Physical_session>(var);
      options.whatif = &session->whatif();
    }
  }
  // root at the left edge, `leaves` inverters at the right edge (x_right DBU).
  Result long_wires(unsigned leaves, int64_t x_right) {
    auto root = gate("sky130_fd_sc_hd__buf_1", "root", graph->get_input_pin("a"));
    for (unsigned i = 0; i < leaves; ++i) {
      output(i == 0 ? std::string("y") : "y" + std::to_string(i),
             i,
             gate("sky130_fd_sc_hd__inv_1", "leaf" + std::to_string(i), root));
    }
    auto       initial = place(*graph, flat, options);
    auto       frame   = initial.floorplan;
    const auto site    = frame.rows.front().site;
    frame.rows.clear();
    frame.die = {0, 0, static_cast<double>(x_right + 8740), 27200};
    for (unsigned i = 0; i < 10; ++i) {
      frame.rows.push_back({"ROW_" + std::to_string(i),
                            site,
                            0,
                            i * 2720,
                            static_cast<unsigned>((x_right + 8740) / 460),
                            460,
                            Ann_place::Orientation::N});
    }
    unsigned row = 0;
    for (auto& c : frame.components) {
      c.place.status      = Ann_place::Status::PLACED;
      c.place.orientation = Ann_place::Orientation::N;
      c.place.x           = c.name == "root" ? 460 : x_right;
      c.place.y           = c.name == "root" ? 0 : (row++ % 10) * 2720;
    }
    for (auto& t : frame.terminals) {
      t.status = Ann_place::Status::FIXED;
      t.x      = t.name == "a" ? 0 : x_right + 8740;
      t.y      = 0;
    }
    options.previous = std::make_shared<physical::Floorplan>(frame);
    auto placed      = place(*graph, stretched, options);
    options.previous.reset();
    return placed;
  }
  // A same-function drive variant `to` of `from` with its own area and LEF width
  // (timing copied), so the ladder has a larger and a smaller member.
  void add_variant(const std::string& from, const std::string& to, double area, double width_um) {
    const auto read = [](const std::string& path) {
      std::ifstream input(path);
      return std::string{std::istreambuf_iterator<char>(input), {}};
    };
    auto       lib   = read(options.liberty);
    const auto start = lib.find("cell (\"" + from + "\")");
    ASSERT_NE(start, std::string::npos);
    auto end   = lib.find('{', start);
    int  depth = 1;
    for (++end; depth && end < lib.size(); ++end) {
      depth += lib[end] == '{';
      depth -= lib[end] == '}';
    }
    auto cell = lib.substr(start, end - start);
    cell.replace(cell.find(from), from.size(), to);
    cell = std::regex_replace(cell, std::regex(R"(area\s*:\s*[0-9.]+)"), std::format("area : {}", area));
    lib.insert(lib.rfind('}'), cell);
    options.liberty = options.output_dir + "/variants.lib";
    std::ofstream(options.liberty) << lib;
    auto       lef    = read(options.lefs[1]);
    const auto begin  = lef.find("MACRO " + from);
    const auto finish = lef.find("END " + from, begin) + 4 + from.size();
    auto       macro  = lef.substr(begin, finish - begin);
    for (size_t pos = macro.find(from); pos != std::string::npos; pos = macro.find(from, pos + to.size())) {
      macro.replace(pos, from.size(), to);
    }
    macro = std::regex_replace(macro, std::regex(R"(SIZE\s+[0-9.]+)"), std::format("SIZE {:.3f}", width_um));
    options.lefs.push_back(options.output_dir + "/variants.lef");
    std::ofstream(options.lefs.back()) << "VERSION 5.8 ;\n" << macro << "\nEND LIBRARY\n";
  }
  static unsigned count(const Result& r, std::string_view prefix) {
    unsigned value = 0;
    for (auto n : r.graph->body().nodes()) {
      value += n.get_name().starts_with(prefix);
    }
    return value;
  }
  static double ps(const Timing_feedback& f, std::optional<double> v) { return v.value_or(0) * f.time_unit_ps; }
  // Every output must still be a single inversion of a through identity cells.
  static void   equivalent(const Result& state) {
    for (const auto& decl : state.graph->get_io()->get_output_pin_decls()) {
      auto               driver     = state.graph->get_output_pin(decl.name).get_driver_pin();
      unsigned           inversions = 0;
      std::set<uint64_t> visited;
      while (!driver.get_master_node().is_input_node()) {
        auto n = driver.get_master_node();
        ASSERT_TRUE(visited.insert(n.get_debug_nid()).second);
        const auto io = n.get_subnode_io();
        ASSERT_TRUE(io);
        const std::string name(io->get_name());
        ASSERT_TRUE(name.starts_with("sky130_fd_sc_hd__buf_") || name.starts_with("sky130_fd_sc_hd__inv_")) << name;
        inversions   += name.starts_with("sky130_fd_sc_hd__inv_");
        const auto p  = n.get_sink_pin("A").get_driver_pins();
        ASSERT_EQ(p.size(), 1);
        driver = p.front();
      }
      EXPECT_EQ(inversions % 2, 1u);
      EXPECT_EQ(driver.get_pin_name(), "a");
    }
  }
};

TEST_F(Resize_engine, MetTimingAddsNoCellsDownsizesAndNeverWorsensSetup) {
  // A met critical inverter chain plus an oversized driver on a short path:
  // only the slack-rich driver is down-sized and no cell is added.
  add_variant("sky130_fd_sc_hd__buf_1", "sky130_fd_sc_hd__buf_8", 11.26, 2.760);
  auto p = graph->get_input_pin("a");
  for (unsigned i = 0; i < 12; ++i) {
    p = gate("sky130_fd_sc_hd__inv_1", "c" + std::to_string(i), p);
  }
  output("y", 0, gate("sky130_fd_sc_hd__inv_1", "chain_end", p));
  auto big = gate("sky130_fd_sc_hd__buf_8", "big", graph->get_input_pin("a"));
  output("y1", 1, gate("sky130_fd_sc_hd__inv_1", "leaf", big));
  auto         placed = place(*graph, flat, options);
  const auto   probe  = timing(placed);
  // Re-time with a period just above the chain delay: the chain is critical.
  const double chain  = probe.pins.at("y").arrival.value_or(0) * probe.time_unit_ps;
  ASSERT_GT(chain, 50);
  session.reset();
  period_ns         = (chain + 120) / 1000;
  const auto before = timing(placed);
  ASSERT_GT(ps(before, before.wns), 0);
  auto       result = resize_placed(std::move(placed), options, [&](const Result& r) { return timing(r); });
  const auto after  = timing(result.placed);
  SCOPED_TRACE(result.report);
  EXPECT_EQ(count(result.placed, "__place_rebuffer"), 0u);
  EXPECT_GE(ps(after, after.wns), ps(before, before.wns) - 1e-3);
  EXPECT_LT(after.area, before.area);
  for (auto n : result.placed.graph->body().nodes()) {
    if (n.get_name() == "big") {
      EXPECT_NE(n.get_subnode_io()->get_name(), "sky130_fd_sc_hd__buf_8");
    }
  }
  equivalent(result.placed);
  rapidjson::Document report;
  report.Parse(result.report.c_str());
  ASSERT_TRUE(report.IsObject());
  EXPECT_STREQ(report["engine"].GetString(), "new");
  EXPECT_EQ(report["rounds"].Size(), 1u);  // met timing: one round, then stop
}

TEST_F(Resize_engine, LongFailingWireGetsRepeatersSeededOnTheCutEdge) {
  options.resistance_ohm_um = 20;
  options.capacitance_ff_um = 1;
  period_ns                 = 1.2;
  auto       placed         = long_wires(1, 230000);
  const auto before         = timing(placed);
  ASSERT_LT(ps(before, before.wns), 0);
  auto       result = resize_placed(std::move(placed), options, [&](const Result& r) { return timing(r); });
  const auto after  = timing(result.placed);
  SCOPED_TRACE(result.report);
  EXPECT_GT(count(result.placed, "__place_rebuffer"), 0u);
  EXPECT_GT(ps(after, after.wns), ps(before, before.wns));
  EXPECT_GE(ps(after, after.tns), ps(before, before.tns));
  // Seeds land between the endpoints of the wire they cut (legalized nearby).
  for (const auto& c : result.placed.floorplan.components) {
    if (c.name.starts_with("__place_rebuffer")) {
      EXPECT_GT(c.place.x, 460 + 2000);
      EXPECT_LT(c.place.x, 230000 - 2000);
    }
  }
  equivalent(result.placed);
}

TEST_F(Resize_engine, ProvablyInfeasiblePathGetsNoBuffers) {
  // A short chain whose gate delays alone exceed the period: even ideal wires
  // and the strongest drives cannot meet it, so no cell may be spent on it.
  period_ns = .05;
  auto p    = graph->get_input_pin("a");
  for (unsigned i = 0; i < 6; ++i) {
    p = gate("sky130_fd_sc_hd__inv_1", "g" + std::to_string(i), p);
  }
  output("y", 0, gate("sky130_fd_sc_hd__inv_1", "last", p));
  auto       placed = place(*graph, flat, options);
  const auto before = timing(placed);
  ASSERT_LT(ps(before, before.wns), 0);
  ASSERT_EQ(before.violations, 0u);
  const auto plan = plan_resize(placed, options, before, *options.whatif, Resize_stage::placed);
  SCOPED_TRACE(plan.report);
  EXPECT_EQ(plan.buffers, 0u);
  rapidjson::Document report;
  report.Parse(plan.report.c_str());
  ASSERT_TRUE(report.IsObject());
  EXPECT_GT(report["timing"]["infeasible_paths"].GetUint(), 0u);
  EXPECT_EQ(report["timing"]["feasible_paths"].GetUint(), 0u);
  EXPECT_GE(plan.wns_after, plan.wns_before - 1e-3);  // what-if never accepts a worse state
  EXPECT_GE(plan.tns_after, plan.tns_before - 1e-2);
  EXPECT_EQ(options.whatif->mark(), 0u);  // plan leaves no pending what-if edit
}

TEST_F(Resize_engine, ElectricalFanoutTreeUsesVariableBranching) {
  // One weak driver, 24 far-away loads: a real max-capacitance violation that
  // only a fanout tree can fix; each level branches 2..8 ways.
  options.resistance_ohm_um = 10;
  options.capacitance_ff_um = 1;
  period_ns                 = 100;
  auto       placed         = long_wires(24, 150000);
  const auto before         = timing(placed);
  ASSERT_GT(before.violations, 0u);
  auto       result = resize_placed(std::move(placed), options, [&](const Result& r) { return timing(r); });
  const auto after  = timing(result.placed);
  SCOPED_TRACE(result.report);
  EXPECT_LT(after.violations, before.violations);
  EXPECT_GE(ps(after, after.wns), ps(before, before.wns) - 1e-3);
  std::map<std::string, unsigned> children;  // driver instance -> repeater loads
  for (auto n : result.placed.graph->body().nodes()) {
    if (!n.get_name().starts_with("__place_rebuffer")) {
      continue;
    }
    auto driver = n.get_sink_pin("A").get_driver_pin();
    while (gu::type_op_of(driver.get_master_node()) == Ntype_op::Get_mask) {
      driver = driver.get_master_node().get_sink_pin(0).get_driver_pin();
    }
    ++children[std::string(driver.get_master_node().get_name())];
  }
  ASSERT_FALSE(children.empty());
  for (const auto& [driver, n] : children) {
    EXPECT_LE(n, 8u) << driver;
  }
  EXPECT_GE(children["root"], 2u);
  equivalent(result.placed);
}

TEST_F(Resize_engine, FloorplanCheckpointsUseTheEngineOnRealTiming) {
  // FP plan with resize: the floorplan-stage checkpoints run the new engine on
  // the real incremental timer at overlapping GP coordinates.
  auto root = gate("sky130_fd_sc_hd__buf_1", "root", graph->get_input_pin("a"));
  for (unsigned i = 0; i < 6; ++i) {
    output(i ? "y" + std::to_string(i) : std::string("y"), i, gate("sky130_fd_sc_hd__inv_1", "macro" + std::to_string(i), root));
  }
  std::ifstream input(options.lefs[1]);
  std::string   lef{std::istreambuf_iterator<char>(input), {}};
  const auto    start = lef.find("MACRO sky130_fd_sc_hd__inv_1\n");
  ASSERT_NE(start, std::string::npos);
  lef.replace(lef.find("CLASS CORE", start), std::string("CLASS CORE").size(), "CLASS BLOCK");
  options.lefs[1] = options.output_dir + "/macro.lef";
  std::ofstream(options.lefs[1]) << lef;
  options.fp_place = options.final_cells = options.fp_colors = options.fp_resize = true;
  options.fp_minimum                                                             = 0;
  options.fp_iterations                                                          = 120;
  options.iterations                                                             = 40;
  options.fp_timing_interval                                                     = 50;
  options.halo_x = options.halo_y = .46;
  options.utilization             = .1;
  options.resistance_ohm_um       = 200;
  options.capacitance_ff_um       = 20;
  period_ns                       = .3;
  for (auto n : graph->body().nodes()) {
    n.attr(attrs::synth_color_group).set(std::string("bank:color:1"));
  }
  unsigned checkpoints = 0;
  options.fp_evaluate  = [&](const Result& state) {
    ++checkpoints;
    for (auto n : state.graph->body().nodes()) {
      if (n.get_name().starts_with("__place_rebuffer")) {
        EXPECT_TRUE(n.attr(attrs::place).has());
        EXPECT_TRUE(n.attr(attrs::synth_color_group).has());
      }
    }
    return timing(state);
  };
  start_session();
  hhds::GraphLibrary owner;
  const auto         placed = place(*graph, owner, options);
  EXPECT_GT(checkpoints, 1u);
  equivalent(placed);
  check_legality(physical::read_lef(options.lefs), placed.floorplan, options.halo_x, options.halo_y);
  bool saw_engine = false;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(options.output_dir)) {
    if (!entry.path().filename().string().starts_with("fp_repair_")) {
      continue;
    }
    std::ifstream       f(entry.path());
    const std::string   text{std::istreambuf_iterator<char>(f), {}};
    rapidjson::Document report;
    report.Parse(text.c_str());
    ASSERT_TRUE(report.IsObject()) << entry.path();
    EXPECT_STREQ(report["engine"].GetString(), "new");
    saw_engine = true;
  }
  EXPECT_TRUE(saw_engine);
}

TEST_F(Resize_engine, SynthesisBufferChainIsBypassedForTheCriticalSink) {
  // A fanout "tree" built as a chain (each buffer drives a leaf and the next
  // buffer): the failing last leaf is re-driven from an earlier net of the
  // chain, which carries the same value. Rewiring only, no cell added.
  auto p = graph->get_input_pin("a");
  for (unsigned i = 0; i < 8; ++i) {
    p = gate("sky130_fd_sc_hd__buf_1", "b" + std::to_string(i), p);
    output(i ? "y" + std::to_string(i) : std::string("y"), i, gate("sky130_fd_sc_hd__inv_1", "leaf" + std::to_string(i), p));
  }
  output("y8", 8, gate("sky130_fd_sc_hd__inv_1", "last", p));
  auto         placed = place(*graph, flat, options);
  const auto   probe  = timing(placed);
  const double last   = probe.pins.at("y8").arrival.value_or(0) * probe.time_unit_ps;
  const double first  = probe.pins.at("y1").arrival.value_or(0) * probe.time_unit_ps;
  ASSERT_GT(last - first, 300);
  session.reset();
  period_ns         = (last - 150) / 1000;  // only the chain's end fails
  const auto before = timing(placed);
  ASSERT_LT(ps(before, before.wns), 0);
  ASSERT_EQ(before.violations, 0u);
  auto       result = resize_placed(std::move(placed), options, [&](const Result& r) { return timing(r); });
  const auto after  = timing(result.placed);
  SCOPED_TRACE(result.report);
  EXPECT_EQ(count(result.placed, "__place_rebuffer"), 0u);
  EXPECT_GT(ps(after, after.wns), ps(before, before.wns) + 100);
  EXPECT_GE(ps(after, after.tns), ps(before, before.tns));
  rapidjson::Document report;
  report.Parse(result.report.c_str());
  ASSERT_TRUE(report.IsObject());
  EXPECT_GT(report["rounds"][0]["plan"]["timing"]["bypass"]["accepted"].GetUint(), 0u);
  // Every buffer still drives something; every output keeps its function.
  for (auto n : result.placed.graph->body().nodes()) {
    if (n.get_name().starts_with("b")) {
      EXPECT_FALSE(n.get_driver_pin("X").out_edges().empty()) << n.get_name();
    }
  }
  equivalent(result.placed);
}

TEST_F(Resize_engine, SlackRichLongWireOnlyGetsTheRepeatersItsLimitsNeed) {
  // A 600 um wire with a real Liberty violation and ample slack: only the
  // limits matter, so the repair uses the longest legal repeater spacing. The
  // delay-optimal spacing for buf_1 at this RC (~140 um) would spend four.
  options.resistance_ohm_um = 10;
  options.capacitance_ff_um = 1;
  period_ns                 = 1000;
  auto       placed         = long_wires(1, 600000);
  const auto before         = timing(placed);
  ASSERT_GT(before.violations, 0u);
  auto       result = resize_placed(std::move(placed), options, [&](const Result& r) { return timing(r); });
  const auto after  = timing(result.placed);
  SCOPED_TRACE(result.report);
  EXPECT_LT(after.violations, before.violations);
  EXPECT_GE(ps(after, after.wns), ps(before, before.wns) - 1e-3);
  const auto repeaters = count(result.placed, "__place_rebuffer");
  EXPECT_GT(repeaters, 0u);
  EXPECT_LE(repeaters, 2u);
  equivalent(result.placed);
}

TEST_F(Resize_engine, LegacyEngineStaysSelectable) {
  options.resize_engine = "legacy";
  add_variant("sky130_fd_sc_hd__buf_1", "sky130_fd_sc_hd__buf_8", 11.26, 2.760);
  auto big = gate("sky130_fd_sc_hd__buf_8", "big", graph->get_input_pin("a"));
  output("y", 0, gate("sky130_fd_sc_hd__inv_1", "leaf", big));
  auto       placed = place(*graph, flat, options);
  const auto before = timing(placed);
  auto       result
      = repair_buffers(std::move(placed), options, [&](const Result& r) { return timing(r); }, Repair_scope::sizing, before);
  rapidjson::Document report;
  report.Parse(result.report.c_str());
  ASSERT_TRUE(report.IsObject());
  EXPECT_FALSE(report.HasMember("engine"));
  EXPECT_TRUE(report.HasMember("trials"));
}
}  // namespace
}  // namespace livehd::placement
