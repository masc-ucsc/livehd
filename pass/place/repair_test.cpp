// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "repair.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <regex>
#include <set>

#include "attrs.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "pass/liberty/liberty_dff.hpp"
#include "pass/opentimer/pass_opentimer.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "steiner_repair.hpp"
namespace livehd::placement {
namespace {
namespace gu = graph_util;
class Buffer_repair : public ::testing::Test {
protected:
  hhds::GraphLibrary                                   source, flat;
  std::shared_ptr<hhds::Graph>                         graph;
  Options                                              options;
  std::unique_ptr<livehd::opentimer::Physical_session> timing_session;
  void                                                 SetUp() override {
    // Repair fixtures use a controlled plain frame, independent of the
    // production floorplan/macro pipeline and its density policy.
    options.optimization_pending = true;  // Exercise the post-placement repair API.
    options.fp_place = options.final_cells = options.macro_snap = false;
    options.scale_macro_density = options.exact_macro_density = options.adaptive_density = options.routability = false;
    options.macro_overlap_weight = options.macro_jitter = 0;
    options.io_iterations = options.schedule_iterations = 0;
    options.halo_x = options.halo_y = 0;
    options.utilization             = .5;
    options.density                 = .8;
    options.lefs                    = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
    options.liberty                 = "inou/lefdef/tests/data/sky130hd.lib";
    options.iterations              = 10;
    options.macro_timing_interval   = 0;  // Existing repair fixtures isolate FP or post-placement repair.
    options.final_timing_start      = 0;  // Short fixture: exercise in-loop timing.
    options.bins                    = 8;
    options.output_dir
        = std::string(std::getenv("TEST_TMPDIR")) + "/" + ::testing::UnitTest::GetInstance()->current_test_info()->name();
    const auto io = source.create_io("top");
    io->add_input("a", 0);
    io->set_bits("a", 1);
    io->add_output("y", 0);
    io->set_bits("y", 1);
    graph = io->create_graph();
    gu::set_bits(graph->get_input_pin("a"), 1);
  }
  hhds::Pin_class gate(std::string master, std::string name, hhds::Pin_class driver) {
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
  void add_allowed_buffer_variant() {
    // The small checked-in library contains only buf_1. Add a distinct,
    // stronger identity with the same pin/footprint and legal physical shape.
    std::filesystem::create_directories(options.output_dir);
    std::ifstream lib_input(options.liberty);
    std::string   lib{std::istreambuf_iterator<char>(lib_input), {}};
    const auto    end = lib.rfind('}');
    ASSERT_NE(end, std::string::npos);
    lib.insert(end,
               R"(cell(sky130_fd_sc_hd__buf_2) { area: 1; cell_footprint: "sky130_fd_sc_hd__buf";
                  pin(A) {direction: input; capacitance: 0.1;}
                  pin(X) {direction: output; function: "A"; max_capacitance: 1000;
                    timing() {related_pin: "A"; cell_rise(t) {index_1("0.001"); index_2("0.001,0.021"); values("0.001,0.003");}}}})");
    options.liberty = options.output_dir + "/allowed.lib";
    std::ofstream(options.liberty) << lib;
    std::ifstream     lef_input(options.lefs.back());
    std::string       lef{std::istreambuf_iterator<char>(lef_input), {}};
    const std::string from = "sky130_fd_sc_hd__buf_1", to = "sky130_fd_sc_hd__buf_2";
    const auto        begin = lef.find("MACRO " + from), finish = lef.find("END " + from, begin);
    ASSERT_NE(begin, std::string::npos);
    ASSERT_NE(finish, std::string::npos);
    auto macro = lef.substr(begin, finish + 4 + from.size() - begin);
    for (auto at = macro.find(from); at != std::string::npos; at = macro.find(from, at + to.size())) {
      macro.replace(at, from.size(), to);
    }
    options.lefs.push_back(options.output_dir + "/allowed.lef");
    std::ofstream(options.lefs.back()) << "VERSION 5.8 ;\n" << macro << "\nEND LIBRARY\n";
  }
  void enable_floorplan() {
    std::filesystem::create_directories(options.output_dir);
    std::ifstream input(options.lefs[1]);
    std::string   lef{std::istreambuf_iterator<char>(input), {}};
    const auto    start = lef.find("MACRO sky130_fd_sc_hd__inv_1\n");
    ASSERT_NE(start, std::string::npos);
    const auto klass = lef.find("CLASS CORE", start);
    ASSERT_NE(klass, std::string::npos);
    lef.replace(klass, std::string("CLASS CORE").size(), "CLASS BLOCK");
    options.lefs[1] = options.output_dir + "/macro.lef";
    std::ofstream(options.lefs[1]) << lef;
    options.fp_place = options.final_cells = options.fp_colors = options.fp_resize = true;
    options.fp_minimum                                                             = 0;
    options.fp_iterations                                                          = 120;
    options.iterations                                                             = 40;
    options.fp_timing_interval                                                     = 50;
    options.halo_x = options.halo_y = .46;
    options.utilization             = .1;
    for (auto n : graph->body().nodes()) {
      n.attr(attrs::synth_color_group).set(std::string("bank:color:1"));
    }
  }
  unsigned count(const Result& r, std::string_view prefix) {
    unsigned value = 0;
    for (auto n : r.graph->body().nodes()) {
      if (n.get_name().starts_with(prefix)) {
        ++value;
      }
    }
    return value;
  }
  Timing_feedback timing(const Result& state) {
    if (!timing_session) {
      const auto sdc = options.output_dir + "/clock.sdc";
      std::ofstream(sdc) << "create_clock -name virtual -period 10\nset_input_delay -clock virtual 0 "
                            "[all_inputs]\nset_output_delay -clock virtual 0 [all_outputs]\n";
      Eprp_var var;
      var.dict = {
          {"files", options.liberty + "," + sdc},
          {  "top",                       "top"},
          { "hier",                     "false"}
      };
      var.set_stage_labels(var.dict);
      timing_session = std::make_unique<livehd::opentimer::Physical_session>(var);
    }
    return timing_session->evaluate(state.graph, state.parasitics);
  }
  // Independent identity/inversion interpretation of the repaired netlist.
  // Every primary output must still be an inverter of a, through any number
  // of non-inverting buffers; catches disconnected branches and cycles.
  void equivalent(const Result& state, unsigned expected_inversions = 1) {
    for (const auto& decl : state.graph->get_io()->get_output_pin_decls()) {
      auto               driver     = state.graph->get_output_pin(decl.name).get_driver_pin();
      unsigned           inversions = 0;
      std::set<uint64_t> visited;
      while (!driver.get_master_node().is_input_node()) {
        auto n = driver.get_master_node();
        ASSERT_TRUE(visited.insert(n.get_debug_nid()).second);
        const auto io = n.get_subnode_io();
        ASSERT_TRUE(io);
        const auto name = io->get_name();
        ASSERT_TRUE(name == "sky130_fd_sc_hd__buf_1" || name == "sky130_fd_sc_hd__buf_2" || name == "sky130_fd_sc_hd__inv_1");
        inversions   += name == "sky130_fd_sc_hd__inv_1";
        const auto p  = n.get_sink_pin("A").get_driver_pins();
        ASSERT_EQ(p.size(), 1);
        driver = p.front();
      }
      EXPECT_EQ(inversions % 2, expected_inversions);
      EXPECT_EQ(driver.get_pin_name(), "a");
    }
  }
};
TEST_F(Buffer_repair, FinalLegalizationPreservesOptimizerProvenanceAndRefreshesGeometry) {
  graph->get_output_pin("y").connect_driver(gate("sky130_fd_sc_hd__inv_1", "sink", graph->get_input_pin("a")));
  options.density = .5;
  auto initial = place(*graph, flat, options);
  // Exercise the report boundary without loading a learned model: inference
  // and support selection are tested independently of final legalization.
  for (bool learned : {false, true}) {
    rapidjson::Document prior;
    prior.Parse(initial.qor.c_str());
    ASSERT_TRUE(prior.IsObject());
    auto& allocator = prior.GetAllocator();
    prior["route_model"].SetString(learned ? "uroute" : "rsmt", allocator);
    prior["uroute_skipped"].SetString(learned ? "" : "insufficient-support", allocator);
    prior["uroute_sample_ids"].SetArray();
    if (learned) {
      prior["uroute_sample_ids"].PushBack(rapidjson::Value("earlier-placement", allocator), allocator);
    }
    prior["incremental"]["enabled"].SetBool(true);
    prior["incremental"]["hit"].SetBool(true);
    prior["incremental"]["reused_blocks"].SetUint(1);
    prior["incremental"]["changed_blocks"].SetUint(0);
    prior["gp_converged"].SetBool(true);
    prior["gp_overflow"].SetDouble(.125);
    prior["initial_hpwl_um"].SetDouble(123);
    prior["virtual_filler_count"].SetUint(7);
    prior["virtual_filler_area_um2"].SetDouble(8);
    prior["virtual_filler_size_x_um"].SetDouble(2);
    prior["virtual_filler_size_y_um"].SetDouble(4);
    prior["virtual_filler_count_limited"].SetBool(true);
    prior["hpwl_um"].SetDouble(-1);
    prior["rc_identity"].SetString("stale", allocator);
    rapidjson::StringBuffer                    buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    prior.Accept(writer);
    initial.qor                = buffer.GetString();
    const auto          result = finalize_placement(initial, options, [](const Result&) { return Timing_feedback{}; });
    rapidjson::Document final;
    final.Parse(result.placed.qor.c_str());
    ASSERT_TRUE(final.IsObject());
    for (const auto* key : {"incremental",
                            "gp_converged",
                            "gp_overflow",
                            "initial_hpwl_um",
                            "virtual_filler_count",
                            "virtual_filler_area_um2",
                            "virtual_filler_size_x_um",
                            "virtual_filler_size_y_um",
                            "virtual_filler_count_limited",
                            "route_model",
                            "uroute_skipped",
                            "uroute_sample_ids"}) {
      EXPECT_EQ(final[key], prior[key]) << key;
      EXPECT_EQ(final["before_final_legalization"][key], prior[key]) << key;
    }
    EXPECT_GE(final["hpwl_um"].GetDouble(), 0);
    EXPECT_STRNE(final["rc_identity"].GetString(), "stale");
    EXPECT_EQ(final["before_final_legalization"]["hpwl_um"].GetDouble(), -1);
    check_legality(physical::read_lef(options.lefs), result.placed.floorplan);
  }
}
TEST_F(Buffer_repair, LateTimingScheduleKeepsWarmupAndIncreasesFrequencyNearEnd) {
  auto root = gate("sky130_fd_sc_hd__buf_1", "root", graph->get_input_pin("a"));
  graph->get_output_pin("y").connect_driver(gate("sky130_fd_sc_hd__inv_1", "sink", root));
  options.virtual_fillers = false;
  options.iterations = options.min_iterations = 12;
  options.post_density_factor                 = 0;
  options.final_timing_start                  = 2;
  options.macro_timing_interval               = 1;
  options.final_timing_schedule               = "late";
  options.fp_evaluate                         = [&](const Result& state) { return timing(state); };
  const auto    placed                        = place(*graph, flat, options);
  std::ifstream trace(options.output_dir + "/final_timing.jsonl");
  std::set<int> iterations;
  for (std::string line; std::getline(trace, line);) {
    rapidjson::Document d;
    d.Parse(line.c_str());
    ASSERT_FALSE(d.HasParseError());
    iterations.insert(d["iteration"].GetInt());
  }
  EXPECT_EQ(iterations, (std::set<int>{2, 6, 8, 10, 11, 12}));
  EXPECT_NO_THROW(timing(placed));
  equivalent(placed);
}
TEST_F(Buffer_repair, FinalGpBuffersAfterFirstCheckpointAndPreservesExistingChains) {
  auto root = gate("sky130_fd_sc_hd__buf_1", "root", graph->get_input_pin("a"));
  for (unsigned i = 0; i < 6; ++i) {
    const auto name = i ? "y" + std::to_string(i) : std::string("y");
    if (i) {
      graph->get_io()->add_output(name, i);
      graph->get_io()->set_bits(name, 1);
    }
    graph->get_output_pin(name).connect_driver(gate("sky130_fd_sc_hd__inv_1", "sink" + std::to_string(i), root));
  }
  options.gp_resize       = true;
  options.virtual_fillers = false;
  // Depth two runs at iteration one. Timing starts at two, so depths three
  // and four must not inspect the subsequently repaired topology.
  options.cell_jump       = {.depth = 4, .settle = 1, .interval = 2, .fix_period = 1};
  options.core = options.die    = {0, 0, 500, 50};
  options.resistance_ohm_um     = 1000;
  options.capacitance_ff_um     = 10;
  options.macro_timing_interval = 2;
  options.final_timing_start    = 0;  // Short fixture: exercise in-loop timing.
  options.iterations = options.min_iterations = 12;
  options.post_density_factor                 = 0;
  options.fp_evaluate                         = [&](const Result& state) {
    auto score          = timing(state);  // Check real RC provenance on every trial.
    options.whatif      = &timing_session->whatif();
    const auto inserted = count(state, "__place_rebuffer");
    score.delay         = inserted ? 1 : 2;
    score.area          = inserted ? 0 : 100;
    score.wns = score.tns   = -1;
    score.hold_wns          = 0;
    score.violations        = 0;
    score.electrical_excess = 0;
    for (auto& [name, pin] : score.pins) {
      (void)name;
      pin.slack = -.1 * options.whatif->clock_period() / score.time_unit_ps;
    }
    return score;
  };
  const auto    placed = place(*graph, flat, options);
  std::ifstream jumps(options.output_dir + "/cell_jump.jsonl");
  std::string   jump_line;
  ASSERT_TRUE(std::getline(jumps, jump_line));
  rapidjson::Document jump;
  jump.Parse(jump_line.c_str());
  ASSERT_FALSE(jump.HasParseError());
  EXPECT_EQ(jump["iteration"].GetInt(), 1);
  EXPECT_EQ(jump["depth"].GetUint(), 2U);
  EXPECT_FALSE(std::getline(jumps, jump_line));
  EXPECT_GT(count(placed, "__place_rebuffer"), 0U);
  EXPECT_EQ(count(placed, "root"), 1U);
  equivalent(placed);
  check_legality(physical::read_lef(options.lefs), placed.floorplan);
  EXPECT_EQ(count(Result{.graph = graph}, "__place_rebuffer"), 0U);
  std::ifstream       first(options.output_dir + "/gp_buffer_repair_2.json");
  const std::string   report{std::istreambuf_iterator<char>(first), {}};
  rapidjson::Document d;
  d.Parse(report.c_str());
  ASSERT_FALSE(d.HasParseError()) << report;
  EXPECT_TRUE(d["measurement_only"].GetBool());
  std::ifstream     second(options.output_dir + "/gp_buffer_repair_4.json");
  const std::string changes{std::istreambuf_iterator<char>(second), {}};
  d.Parse(changes.c_str());
  ASSERT_FALSE(d.HasParseError());
  EXPECT_EQ(d["checkpoint"].GetUint(), 2U);
  EXPECT_STREQ(d["scale"].GetString(), "shortest_clock_period");
  EXPECT_DOUBLE_EQ(d["margin_ps"].GetDouble(), .01 * d["scale_ps"].GetDouble());
  ASSERT_FALSE(d["trials"].Empty());
  EXPECT_TRUE(d["trials"][0]["accepted"].GetBool()) << changes;
  // The edit cannot immediately be undone/rebuilt at the next checkpoint.
  std::ifstream     third(options.output_dir + "/gp_buffer_repair_6.json");
  const std::string cooldown{std::istreambuf_iterator<char>(third), {}};
  d.Parse(cooldown.c_str());
  ASSERT_FALSE(d.HasParseError());
  for (const auto& trial : d["trials"].GetArray()) {
    EXPECT_FALSE(trial["accepted"].GetBool());
  }
}
TEST_F(Buffer_repair, FinalGpRemovalRequiresMarginAndTimingAcceptanceAndCanBeDisabled) {
  auto b = gate("sky130_fd_sc_hd__buf_1", "buffer", graph->get_input_pin("a"));
  graph->get_output_pin("y").connect_driver(gate("sky130_fd_sc_hd__inv_1", "sink", b));
  options.macro_timing_interval = 2;
  options.final_timing_start    = 0;  // Short fixture: exercise in-loop timing.
  options.iterations = options.min_iterations = 8;
  options.post_density_factor                 = 0;
  const auto directory                        = options.output_dir;
  for (unsigned mode = 0; mode < 4; ++mode) {
    options.output_dir       = directory + "/" + std::to_string(mode);
    options.gp_resize        = mode != 0;
    options.gp_buffer_trials = mode == 2 ? 1 : 4;
    // Timing precedes the default jump schedule, even with GP repair disabled.
    // The short run need not budget for depths that timing will cancel.
    options.cell_jump.depth  = 4;
    options.fp_evaluate      = [&](const Result& state) {
      auto score           = timing(state);
      options.whatif       = &timing_session->whatif();
      const double period  = options.whatif->clock_period();
      const bool   present = count(state, "buffer");
      score.delay          = present ? 2 : mode == 2 ? 3 : 1;
      score.area           = present ? 2 : 1;
      score.wns = score.tns   = 0;
      score.hold_wns          = 0;
      score.violations        = 0;
      score.electrical_excess = 0;
      for (auto& [name, pin] : score.pins) {
        (void)name;
        pin.slack   = (mode == 1 ? .005 : .05) * period / score.time_unit_ps;
        pin.load_ff = 0;
        pin.slew    = 0;
      }
      return score;
    };
    hhds::GraphLibrary owner;
    const auto         placed = place(*graph, owner, options);
    EXPECT_FALSE(std::filesystem::exists(options.output_dir + "/cell_jump.jsonl"));
    EXPECT_EQ(count(placed, "buffer"), mode == 3 ? 0U : 1U) << mode;
    if (mode == 2) {
      // A rejected removal must preserve the accepted circuit even when the
      // budget ends immediately, with no later trial to rebuild its RC.
      std::ifstream       checkpoint(options.output_dir + "/gp_buffer_repair_4.json");
      const std::string   report{std::istreambuf_iterator<char>(checkpoint), {}};
      rapidjson::Document d;
      d.Parse(report.c_str());
      ASSERT_FALSE(d.HasParseError()) << report;
      unsigned removals = 0;
      for (const auto& trial : d["trials"].GetArray()) {
        if (std::string_view(trial["kind"].GetString()) == "remove") {
          ++removals;
          EXPECT_FALSE(trial["accepted"].GetBool());
        }
      }
      EXPECT_EQ(removals, 1U);
      EXPECT_NO_THROW(timing(placed));  // Validate restored RC/graph provenance.
    }
    equivalent(placed);
    check_legality(physical::read_lef(options.lefs), placed.floorplan);
  }
}

TEST_F(Buffer_repair, SingleCellLegalizationKeepsEveryExistingNeighborFixed) {
  // The production stage selection must also preserve an explicit ECO frame.
  options.fp_place = options.final_cells = true;
  options.utilization                    = 0.25;
  auto b                                 = gate("sky130_fd_sc_hd__buf_1", "b", graph->get_input_pin("a"));
  auto p                                 = gate("sky130_fd_sc_hd__inv_1", "invert", b);
  graph->get_output_pin("y").connect_driver(p);
  const auto before = place(*graph, flat, options);
  auto       added  = gate("sky130_fd_sc_hd__buf_1", "added", p);
  gu::drop_drivers(graph->get_output_pin("y"));
  graph->get_output_pin("y").connect_driver(added);
  auto seed   = before.floorplan.components.front().place;
  seed.status = Ann_place::Status::PLACED;
  added.get_master_node().attr(attrs::place).set(seed);
  options.previous       = std::make_shared<physical::Floorplan>(before.floorplan);
  options.changed_cells  = {"added"};
  options.legalize_cells = true;
  hhds::GraphLibrary owner;
  const auto         after = place(*graph, owner, options);
  for (const auto& old : before.floorplan.components) {
    const auto next = std::find_if(after.floorplan.components.begin(), after.floorplan.components.end(), [&](const auto& c) {
      return c.name == old.name;
    });
    ASSERT_NE(next, after.floorplan.components.end());
    EXPECT_EQ(next->place, old.place);
  }
  rapidjson::Document qor;
  qor.Parse(after.qor.c_str());
  EXPECT_TRUE(qor["eco"]["single_cell"].GetBool());
  equivalent(after);
}
TEST(SteinerRepair, SplitsLongEdgesAndVisitsEverySinkExactlyOnce) {
  const std::vector<Point> locations{
      {10000,    0},
      {10000, 5000},
      { 5000, 5000},
      {    0, 5000}
  };
  const auto plan = plan_buffers({0, 0}, locations, {1, 1, 1, 1}, 0.5, 10, 2, 2000, 0.0001);
  ASSERT_GT(plan.buffers.size(), 4);
  std::vector<unsigned> seen(locations.size());
  std::set<unsigned>    visited;
  const auto            walk = [&](auto&& self, Point origin, const std::vector<Buffer_tree::Load>& loads) -> void {
    EXPECT_LE(loads.size(), 2);
    for (const auto& load : loads) {
      if (load.buffer) {
        ASSERT_TRUE(visited.insert(load.index).second);
        const auto& b = plan.buffers.at(load.index);
        EXPECT_LE(std::abs(origin.first - b.position.first) + std::abs(origin.second - b.position.second), 2000 + 1e-6);
        self(self, b.position, b.loads);
      } else {
        ++seen.at(load.index);
        const auto xy = locations.at(load.index);
        EXPECT_LE(std::abs(origin.first - xy.first) + std::abs(origin.second - xy.second), 2000 + 1e-6);
      }
    }
  };
  walk(walk, Point{0, 0}, plan.root);
  for (const auto n : seen) {
    EXPECT_EQ(n, 1);
  }
  EXPECT_EQ(visited.size(), plan.buffers.size());
}
TEST(SteinerRepair, CoincidentPinsAndAlreadySmallNets) {
  const std::vector<Point> pins(20, {100, 100});
  const auto               plan = plan_buffers({100, 100}, pins, std::vector<double>(20, 2), 1, 8, 4, 100, 0.01);
  ASSERT_FALSE(plan.buffers.empty());
  unsigned leaves = 0;
  for (const auto& b : plan.buffers) {
    EXPECT_EQ(b.position, Point(100, 100));
    EXPECT_LE(b.loads.size(), 4);
    for (const auto& l : b.loads) {
      leaves += !l.buffer;
    }
  }
  for (const auto& l : plan.root) {
    leaves += !l.buffer;
  }
  EXPECT_EQ(leaves, 20);
  const auto small = plan_buffers(
      {
          0,
          0
  },
      {{1, 0}, {0, 1}},
      {1, 1},
      1,
      8,
      4,
      100,
      0.01);
  EXPECT_TRUE(small.buffers.empty());
  EXPECT_EQ(small.root.size(), 2);
}
TEST(SteinerRepair, LargeSpatialNetExceedsOldTerminalAndBufferLimits) {
  std::vector<Point> pins;
  for (unsigned i = 0; i < 5000; ++i) {
    pins.emplace_back(10 * (i % 100), 10 * (i / 100));
  }
  const auto plan = plan_buffers({-10, -10}, pins, std::vector<double>(pins.size(), 1), .25, 4, 4, 100, 0);
  EXPECT_GT(plan.buffers.size(), 256);
  std::vector<unsigned> seen(pins.size()), parents(plan.buffers.size());
  const auto            check = [&](Point xy, const std::vector<Buffer_tree::Load>& loads) {
    EXPECT_LE(loads.size(), 4);
    double cap = 0;
    for (const auto& load : loads) {
      const auto target = load.buffer ? plan.buffers.at(load.index).position : pins.at(load.index);
      EXPECT_LE(std::abs(xy.first - target.first) + std::abs(xy.second - target.second), 100 + 1e-6);
      if (load.buffer) {
        ++parents.at(load.index);
        cap += .25;
      } else {
        ++seen.at(load.index);
        cap += 1;
      }
    }
    EXPECT_LE(cap, 4 + 1e-6);
  };
  for (size_t i = 0; i < plan.buffers.size(); ++i) {
    for (const auto& load : plan.buffers[i].loads) {
      if (load.buffer) {
        EXPECT_LT(load.index, i);
      }
    }
    check(plan.buffers[i].position, plan.buffers[i].loads);
  }
  check({-10, -10}, plan.root);
  for (const auto n : seen) {
    EXPECT_EQ(n, 1);
  }
  for (const auto n : parents) {
    EXPECT_EQ(n, 1);
  }
}
TEST(SteinerRepair, StrongSourceCanKeepDirectConnectionBeyondBufferSpacing) {
  const Buffer_drive repeater{1, 20, 20, 4, 20, 1000, 1};
  const Buffer_drive source{1, 100, 1000, 32, 1, 10, 1};
  const auto         plan = plan_buffer_choices(
      {
          0,
          0
  },
      {{100, 0}, {0, 100}},
      {1, 1},
      {100, 100},
      {repeater},
      source,
      .01,
      .001);
  EXPECT_TRUE(plan.buffers.empty());
  ASSERT_EQ(plan.root.size(), 2);
  EXPECT_FALSE(plan.root[0].buffer);
  EXPECT_FALSE(plan.root[1].buffer);
}
TEST(SteinerRepair, DriveChoiceRespectsRootAndLargeSinkCapacitance) {
  const Buffer_drive weak{.1, 4, 1000, 4, 1, 1000, 1};
  const Buffer_drive strong{.5, 64, 1000, 32, 2, 50, 2};
  auto               driver = weak;
  driver.max_cap_ff         = .6;
  const auto plan           = plan_buffer_choices(
      {
          0,
          0
  },
      {{100, 0}, {0, 100}},
      {20, 1},
      {10, 100},
      {weak, strong},
      driver,
      .1,
      .001);
  ASSERT_FALSE(plan.buffers.empty());
  EXPECT_EQ(plan.root.size(), 1);
  EXPECT_TRUE(plan.root.front().buffer);
  bool drives_heavy = false;
  for (const auto& branch : plan.buffers) {
    for (const auto& load : branch.loads) {
      if (!load.buffer && load.index == 0) {
        EXPECT_EQ(branch.drive, 1);
        drives_heavy = true;
      }
    }
  }
  EXPECT_TRUE(drives_heavy);
  EXPECT_THROW(plan_buffer_choices(
                   {
                       0,
                       0
  },
                   {{1, 0}},
                   {100},
                   {0},
                   {weak, strong},
                   driver,
                   .1,
                   .001),
               std::runtime_error);
}
TEST_F(Buffer_repair, FloorplanSizesAllStressedDriversIncludingEqualAreaDrives) {
  std::filesystem::create_directories(options.output_dir);
  const std::string from = "sky130_fd_sc_hd__buf_1", to = "sky130_fd_sc_hd__buf_2", overloaded = "sky130_fd_sc_hd__buf_4";
  options.liberty = options.output_dir + "/drives.lib";
  std::ofstream lib(options.liberty);
  lib << "library(test) { time_unit : \"1ps\"; capacitive_load_unit(1,ff);\n";
  for (const auto& name : {from, to, overloaded}) {
    lib << "cell(" << name
        << ") { area: 1; cell_footprint: buf; pin(A) {direction: input; capacitance: " << (name == overloaded ? 10 : .1) << ";} "
        << "pin(X) {direction: output; function: \"A\"; max_capacitance: 100; timing() { related_pin: \"A\"; "
        << "cell_rise(t) { index_1(\"1\"); index_2(\"1,21\"); values(\"1,"
        << (name == from ? 21
            : name == to ? 3
                         : 1)
        << "\"); } } } }\n";
  }
  lib << "}\n";
  lib.close();
  std::ifstream lef_input(options.lefs.back());
  std::string   lef{std::istreambuf_iterator<char>(lef_input), {}};
  const auto    begin = lef.find("MACRO " + from), end = lef.find("END " + from, begin) + 4 + from.size();
  options.lefs.push_back(options.output_dir + "/drives.lef");
  std::ofstream added_lef(options.lefs.back());
  added_lef << "VERSION 5.8 ;\n";
  for (const auto& name : {to, overloaded}) {
    auto macro = lef.substr(begin, end - begin);
    for (auto pos = macro.find(from); pos != std::string::npos; pos = macro.find(from, pos + name.size())) {
      macro.replace(pos, from.size(), name);
    }
    added_lef << macro << "\n";
  }
  added_lef << "END LIBRARY\n";
  added_lef.close();
  for (unsigned i = 0; i < 32; ++i) {
    const auto name = "b" + std::to_string(i);
    auto       out  = gate(from, name, graph->get_input_pin("a"));
    const auto port = i ? "y" + std::to_string(i) : "y";
    if (i) {
      graph->get_io()->add_output(port, i);
      graph->get_io()->set_bits(port, 1);
    }
    graph->get_output_pin(port).connect_driver(out);
  }
  auto state = place(*graph, flat, options);
  for (auto& net : state.parasitics->nets) {
    for (auto& edge : net.edges) {
      edge.resistance_ohm = 10000;
    }
  }
  Timing_feedback before;
  before.delay = 20;
  before.area  = 32;
  for (unsigned i = 0; i < 32; ++i) {
    auto& pin   = before.pins["b" + std::to_string(i) + ":X"];
    pin.load_ff = 20;
    pin.slack   = -1;
  }
  unsigned   evaluations = 0;
  const auto result      = resize_floorplan(
      state,
      options,
      before,
      [&](const Result& trial) {
        ++evaluations;
        for (const auto& cell : trial.floorplan.components) {
          EXPECT_EQ(cell.macro, to);
        }
        auto after  = before;
        after.delay = 5;
        return after;
      },
      [](Result&) {});
  EXPECT_EQ(result.proposed, 32);
  EXPECT_EQ(result.accepted, 32);
  EXPECT_EQ(evaluations, 1);
  // A global batch containing b0 must roll back, while disjoint useful edits
  // survive bisection. This catches both the old two-driver starvation and
  // accidental publication of partially accepted graph/geometry changes.
  hhds::GraphLibrary owner;
  auto               legal = place(*graph, owner, options);
  for (auto& net : legal.parasitics->nets) {
    for (auto& edge : net.edges) {
      edge.resistance_ohm = 10000;
    }
  }
  const auto evaluate = [&](const Result& trial) {
    auto     feedback = before;
    unsigned weak = 0, present = 0;
    bool     bad = false;
    for (const auto& cell : trial.floorplan.components) {
      ++present;
      weak += cell.macro == from;
      bad  |= cell.name == "b0" && cell.macro == to;
    }
    feedback.delay             = present != 32 ? 40 : bad ? 30 : weak == 32 ? 20 : 5;
    feedback.violations        = weak;
    feedback.electrical_excess = weak;
    return feedback;
  };
  const auto          repaired = repair_buffers(std::move(legal), options, evaluate, Repair_scope::sizing);
  rapidjson::Document trace;
  trace.Parse(repaired.report.c_str());
  unsigned visited_batches         = 0;
  bool     covered_disjoint_subset = false;
  for (const auto& trial : trace["trials"].GetArray()) {
    if (std::string_view(trial["action"].GetString()) != "sizing_sweep") {
      continue;
    }
    if (++visited_batches > 3) {
      break;
    }
    covered_disjoint_subset |= trial["accepted"].GetBool();
  }
  EXPECT_TRUE(covered_disjoint_subset);  // one bad subset cannot starve the rest
  unsigned stronger = 0;
  for (const auto& cell : repaired.placed.floorplan.components) {
    stronger += cell.macro == to;
    if (cell.name == "b0") {
      EXPECT_EQ(cell.macro, from);
    }
  }
  EXPECT_EQ(stronger, 31);
  equivalent(repaired.placed, 0);
  // Scattered bad changes defeat a bounded binary subdivision. Pin feedback
  // must keep the independent useful changes in one retry, without weakening
  // the electrical acceptance gate. Reuse the already measured initial state.
  hhds::GraphLibrary guided_owner;
  auto               guided = place(*graph, guided_owner, options);
  for (auto& net : guided.parasitics->nets) {
    for (auto& edge : net.edges) {
      edge.resistance_ohm = 10000;
    }
  }
  auto guided_before                    = before;
  guided_before.electrical_pin_feedback = true;
  guided_before.wns                     = -1;
  guided_before.tns                     = -32;
  unsigned   guided_calls               = 0;
  const auto guided_evaluate            = [&](const Result& trial) {
    ++guided_calls;
    auto feedback  = guided_before;
    feedback.delay = 5;
    for (const auto& cell : trial.floorplan.components) {
      const auto i = std::stoul(cell.name.substr(1));
      if (cell.macro == to && i % 4 == 0) {
        auto& pin = feedback.pins[cell.name + ":X"];
        if (i % 8 == 0) {
          pin.violations        = 1;
          pin.electrical_excess = .5;
          ++feedback.violations;
          feedback.electrical_excess += .5;
        } else {
          pin.slack      = -2;
          feedback.wns   = -2;
          *feedback.tns -= 1;
        }
      }
    }
    return feedback;
  };
  const auto pruned = repair_buffers(std::move(guided), options, guided_evaluate, Repair_scope::sizing, guided_before);
  EXPECT_EQ(guided_calls, 2);
  ASSERT_TRUE(pruned.timing);
  EXPECT_EQ(pruned.timing->violations, 0);
  EXPECT_EQ(pruned.timing->delay, 5);
  EXPECT_EQ(pruned.timing->wns, guided_before.wns);
  EXPECT_EQ(pruned.timing->tns, guided_before.tns);
  unsigned useful = 0;
  for (const auto& cell : pruned.placed.floorplan.components) {
    useful += cell.macro == to;
    if (std::stoul(cell.name.substr(1)) % 4 == 0) {
      EXPECT_EQ(cell.macro, from);
    }
  }
  EXPECT_EQ(useful, 24);
  equivalent(pruned.placed, 0);
  for (auto node : graph->body().nodes()) {
    if (node.get_subnode_io()) {
      EXPECT_EQ(node.get_subnode_io()->get_name(), from);
    }
  }
}
TEST_F(Buffer_repair, RealTimingRemovesBufferChainAndPreservesFunction) {
  auto p = graph->get_input_pin("a");
  for (unsigned i = 0; i < 3; ++i) {
    p = gate("sky130_fd_sc_hd__buf_1", "b" + std::to_string(i), p);
  }
  p = gate("sky130_fd_sc_hd__inv_1", "invert", p);
  graph->get_output_pin("y").connect_driver(p);
  auto       placed   = place(*graph, flat, options);
  const auto before   = timing(placed);
  const auto repaired = repair_buffers(std::move(placed), options, [&](const Result& r) { return timing(r); });
  const auto after    = timing(repaired.placed);
  EXPECT_LT(after.delay, before.delay);
  EXPECT_LT(after.area, before.area);
  EXPECT_GE(after.wns.value(), before.wns.value() - 1e-6);
  EXPECT_EQ(count(repaired.placed, "b"), 0);
  equivalent(repaired.placed);
  // The caller's source netlist is unchanged.
  unsigned original = 0;
  for (auto n : graph->body().nodes()) {
    if (n.get_subnode_io()) {
      ++original;
    }
  }
  EXPECT_EQ(original, 4);
}
TEST_F(Buffer_repair, ConstantTopOutputBitsUsePhysicalTieSourcesForTiming) {
  auto p = gate("sky130_fd_sc_hd__inv_1", "logic", graph->get_input_pin("a"));
  graph->get_output_pin("y").connect_driver(p);
  graph->get_io()->add_output("bus", 1);
  graph->get_io()->set_bits("bus", 3);
  auto       concat = gu::create_typed_node(*graph, Ntype_op::Concat);
  const auto one    = gu::create_const(*graph, *Dlop::create_integer(1));
  const auto zero   = gu::create_const(*graph, *Dlop::create_integer(0));
  concat.create_sink_pin(0).connect_driver(graph->get_input_pin("a"));
  concat.create_sink_pin(1).connect_driver(one);
  concat.create_sink_pin(2).connect_driver(one);
  concat.create_sink_pin(3).connect_driver(one);
  concat.create_sink_pin(4).connect_driver(zero);
  concat.create_sink_pin(5).connect_driver(one);
  auto out = concat.create_driver_pin(0);
  gu::set_bits(out, 3);
  gu::set_unsign(out);
  graph->get_output_pin("bus").connect_driver(out);
  options.timing_seed = true;
  const auto initial  = place(*graph, flat, options);
  const auto before   = timing(initial);  // rejects a physical/STA endpoint mismatch
  EXPECT_TRUE(std::isfinite(before.delay));
  hhds::GraphLibrary warm;
  const auto         again = place(*initial.graph, warm, options);
  const auto         after = timing(again);
  EXPECT_FLOAT_EQ(before.delay, after.delay);
  EXPECT_EQ(initial.floorplan.components.size(), again.floorplan.components.size());
  EXPECT_EQ(count(initial, "__place_tie"), 2U);
}
TEST_F(Buffer_repair, PostSynthesisWeightsSeedFirstPlacementThenUsePlacedRc) {
  auto p = gate("sky130_fd_sc_hd__buf_1", "buffer", graph->get_input_pin("a"));
  p      = gate("sky130_fd_sc_hd__inv_1", "invert", p);
  graph->get_output_pin("y").connect_driver(p);
  auto seed_options         = options;
  seed_options.timing_seed  = true;
  seed_options.output_dir  += "/post_synth";
  hhds::GraphLibrary seed_library, placed_library;
  const auto         seed = place(*graph, seed_library, seed_options);
  EXPECT_FALSE(std::filesystem::exists(seed_options.output_dir + "/out.def"));
  for (const auto& net : seed.parasitics->nets) {
    for (const auto& node : net.nodes) {
      EXPECT_EQ(node.capacitance_ff, 0);
    }
  }
  options.net_weights = timing_weights(*seed.parasitics, timing(seed));
  ASSERT_FALSE(options.net_weights.empty());
  auto initial = place(*seed.graph, placed_library, options);
  EXPECT_EQ(initial.net_weights, options.net_weights);
  double wire_cap = 0;
  for (const auto& net : initial.parasitics->nets) {
    for (const auto& node : net.nodes) {
      wire_cap += node.capacitance_ff;
    }
  }
  EXPECT_GT(wire_cap, 0);
  const auto result = timing_place(std::move(initial), options, [&](const Result& r) { return timing(r); }, 2);
  equivalent(result.placed);
}

TEST_F(Buffer_repair, FloorplanTimingCheckpointsAreScopedAndResizeRunsStaTwice) {
  auto b = gate("sky130_fd_sc_hd__buf_1", "buffer", graph->get_input_pin("a"));
  auto p = gate("sky130_fd_sc_hd__inv_1", "macro", b);
  graph->get_output_pin("y").connect_driver(p);
  for (auto n : graph->body().nodes()) {
    n.attr(attrs::synth_color_group).set(std::string("bank:color:1"));
  }
  // A real Liberty cell with macro LEF geometry avoids a second timing library.
  std::ifstream input(options.lefs[1]);
  std::string   lef{std::istreambuf_iterator<char>(input), {}};
  const auto    start = lef.find("MACRO sky130_fd_sc_hd__inv_1\n");
  ASSERT_NE(start, std::string::npos);
  const auto klass = lef.find("CLASS CORE", start);
  ASSERT_NE(klass, std::string::npos);
  const auto directory = options.output_dir;
  std::filesystem::create_directories(directory);
  const auto macro_lef = directory + "/macro.lef";
  lef.replace(klass, std::string("CLASS CORE").size(), "CLASS BLOCK");
  std::ofstream(macro_lef) << lef;
  options.lefs[1]        = macro_lef;
  // Add an equivalent wider drive variant, absent from the base tiny library.
  const std::string from = "sky130_fd_sc_hd__buf_1", to = "sky130_fd_sc_hd__buf_2";
  std::ifstream     base_lib(options.liberty);
  std::string       lib{std::istreambuf_iterator<char>(base_lib), {}};
  const auto        cell_start = lib.find("cell (\"" + from + "\")");
  ASSERT_NE(cell_start, std::string::npos);
  auto cell_end = lib.find('{', cell_start);
  int  depth    = 1;
  for (++cell_end; depth && cell_end < lib.size(); ++cell_end) {
    if (lib[cell_end] == '{') {
      ++depth;
    } else if (lib[cell_end] == '}') {
      --depth;
    }
  }
  auto variant = lib.substr(cell_start, cell_end - cell_start);
  variant.replace(variant.find(from), from.size(), to);
  variant = std::regex_replace(variant, std::regex(R"(area\s*:\s*[0-9.]+)"), "area : 7.5072");
  lib.insert(lib.rfind('}'), variant);
  options.liberty = directory + "/sizing.lib";
  std::ofstream(options.liberty) << lib;
  const auto begin  = lef.find("MACRO " + from);
  const auto end    = lef.find("END " + from, begin) + 4 + from.size();
  auto       larger = lef.substr(begin, end - begin);
  for (auto pos = larger.find(from); pos != std::string::npos; pos = larger.find(from, pos + to.size())) {
    larger.replace(pos, from.size(), to);
  }
  larger = std::regex_replace(larger, std::regex(R"(SIZE\s+[0-9.]+)"), "SIZE 2.760");
  options.lefs.push_back(directory + "/sizing.lef");
  std::ofstream(options.lefs.back()) << "VERSION 5.8 ;\n" << larger << "\nEND LIBRARY\n";
  options.fp_place = options.final_cells = options.fp_colors = true;
  options.fp_minimum                                         = 0;
  options.fp_iterations                                      = 120;
  options.iterations                                         = 40;
  options.fp_timing_interval                                 = 50;
  options.utilization                                        = .1;  // The full centroid circle must fit inside the core.
  for (unsigned mode = 0; mode < 3; ++mode) {
    const bool resize = mode != 0, accept = mode == 1;
    unsigned   calls    = 0;
    options.fp_resize   = resize;
    options.output_dir  = directory + (resize ? (accept ? "/resize" : "/reject") : "/weights");
    options.fp_evaluate = [&](const Result& snapshot) {
      ++calls;
      EXPECT_TRUE(snapshot.parasitics->approximate);
      auto score = timing(snapshot);  // real STA validates the snapshot and RC
      if (resize) {
        // Force acceptance independently of this fixture's positive slack.
        bool stronger = false, present = false;
        for (auto n : snapshot.graph->body().nodes()) {
          if (n.get_name() == "buffer") {
            present  = true;
            stronger = n.get_subnode_io()->get_name() == "sky130_fd_sc_hd__buf_2";
          }
        }
        score.delay = !present ? 4 : stronger ? (accept ? 1 : 3) : 2;
        score.wns = score.tns   = -1;
        score.hold_wns          = 0;
        score.violations        = 0;
        score.electrical_excess = 0;
        for (auto& [name, pin] : score.pins) {
          (void)name;
          pin.slack = -1;
        }
      }
      return score;
    };
    hhds::GraphLibrary owner;
    const auto         placed = place(*graph, owner, options);
    if (resize) {
      EXPECT_GE(calls, 4U);
    } else {
      EXPECT_EQ(calls, 2U);
    }
    ASSERT_FALSE(placed.net_weights.empty());
    EXPECT_GT(std::max_element(placed.net_weights.begin(),
                               placed.net_weights.end(),
                               [](const auto& lhs, const auto& rhs) { return lhs.second < rhs.second; })
                  ->second,
              1);
    equivalent(placed);
    std::ifstream       report(options.output_dir + "/macro/fp_place.json");
    const std::string   text{std::istreambuf_iterator<char>(report), {}};
    rapidjson::Document doc;
    doc.Parse(text.c_str());
    ASSERT_FALSE(doc.HasParseError());
    EXPECT_EQ(doc["timing_checkpoints"].GetUint(), 2U);
    EXPECT_EQ(doc["timing_sta_calls"].GetUint(), calls);
    EXPECT_EQ(doc["timing_resized_cells"].GetUint(), accept ? 1U : 0U);
    if (resize) {
      const auto sized = std::find_if(placed.floorplan.components.begin(), placed.floorplan.components.end(), [](const auto& c) {
        return c.name == "buffer";
      });
      ASSERT_NE(sized, placed.floorplan.components.end());
      EXPECT_EQ(sized->macro, accept ? "sky130_fd_sc_hd__buf_2" : "sky130_fd_sc_hd__buf_1");
      for (auto n : placed.graph->body().nodes()) {
        if (n.get_name() == "buffer") {
          EXPECT_EQ(n.get_subnode_io()->get_name(), sized->macro);
        }
      }
      EXPECT_EQ(placed.fp_resized_cells, accept ? 1U : 0U);
    }
    // Disabling the interval suppresses both timing and sizing, in FP only.
    options.fp_timing_interval = 0;
    calls                      = 0;
    hhds::GraphLibrary disabled;
    (void)place(*graph, disabled, options);
    EXPECT_EQ(calls, 0U);
    options.fp_timing_interval = 50;
  }
}
TEST_F(Buffer_repair, FloorplanMovementCoalescesStationaryTimingAndSamplesTheCap) {
  auto b = gate("sky130_fd_sc_hd__buf_1", "buffer", graph->get_input_pin("a"));
  graph->get_output_pin("y").connect_driver(gate("sky130_fd_sc_hd__inv_1", "macro", b));
  enable_floorplan();
  options.fp_resize          = false;
  options.fp_timing_movement = 1e9;  // isolate initial/final calls despite normal GP motion
  unsigned calls             = 0;
  options.fp_evaluate        = [&](const Result& state) {
    ++calls;
    return timing(state);
  };
  const auto placed = place(*graph, flat, options);
  equivalent(placed);
  std::ifstream            trace(options.output_dir + "/macro/fp_timing.jsonl");
  std::string              line;
  std::vector<int>         iterations;
  std::vector<std::string> reasons;
  while (std::getline(trace, line)) {
    rapidjson::Document d;
    d.Parse(line.c_str());
    ASSERT_FALSE(d.HasParseError());
    iterations.push_back(d["iteration"].GetInt());
    reasons.push_back(d["trigger"].GetString());
  }
  EXPECT_EQ(iterations, std::vector<int>({50, 120}));
  EXPECT_EQ(reasons, std::vector<std::string>({"initial", "final"}));
  EXPECT_EQ(calls, 2U);
}
TEST_F(Buffer_repair, FloorplanAndMacroTimingApplyCriticalityCurve) {
  auto b = gate("sky130_fd_sc_hd__buf_1", "buffer", graph->get_input_pin("a"));
  graph->get_output_pin("y").connect_driver(gate("sky130_fd_sc_hd__inv_1", "macro", b));
  enable_floorplan();
  options.fp_resize             = false;
  options.macro_timing_interval = 10;
  options.final_timing_start    = 0;  // Short fixture: exercise in-loop timing.
  options.iterations            = 60;
  options.timing_fraction       = 1;
  options.timing_exponent       = 3;
  options.timing_smoothing      = 1;
  unsigned calls                = 0;
  options.fp_evaluate           = [&](const Result& snapshot) {
    ++calls;
    auto score = timing(snapshot);  // Validate actual RC/STA in both stages.
    if (calls == 3) {
      // First macro checkpoint inherits the cubic FP objective.
      EXPECT_DOUBLE_EQ(snapshot.net_weights.at("a"), 5);
      EXPECT_DOUBLE_EQ(snapshot.net_weights.at("buffer:X"), 1.5);
      EXPECT_DOUBLE_EQ(snapshot.net_weights.at("macro:Y"), 1);
    }
    for (auto& [name, pin] : score.pins) {
      (void)name;
      pin.slack = 1000;
    }
    // Simulate criticality changing with the macro placement. The later solve
    // must reweight the opposite end of this path, not keep its FP weights.
    score.pins["a"].slack        = calls <= 2 ? -100 : 0;
    score.pins["buffer:X"].slack = -50;
    score.pins["macro:Y"].slack  = calls <= 2 ? 0 : -100;
    score.wns                    = 0;
    return score;
  };
  const auto placed = place(*graph, flat, options);
  EXPECT_GT(calls, 3U);
  EXPECT_DOUBLE_EQ(placed.net_weights.at("a"), 1);
  EXPECT_DOUBLE_EQ(placed.net_weights.at("buffer:X"), 1.5);
  EXPECT_DOUBLE_EQ(placed.net_weights.at("macro:Y"), 5);
  equivalent(placed);
  for (const auto* stage : {"fp", "macro"}) {
    std::ifstream trace(options.output_dir + "/macro/" + stage + "_timing.jsonl");
    std::string   line;
    ASSERT_TRUE(bool(std::getline(trace, line)));
    rapidjson::Document d;
    d.Parse(line.c_str());
    ASSERT_FALSE(d.HasParseError());
    EXPECT_EQ(d["timing_exponent"].GetDouble(), 3);
  }
}
TEST_F(Buffer_repair, FloorplanDensityGateDefersTimingAndRepairUntilTheCap) {
  auto b = gate("sky130_fd_sc_hd__buf_1", "buffer", graph->get_input_pin("a"));
  graph->get_output_pin("y").connect_driver(gate("sky130_fd_sc_hd__inv_1", "macro", b));
  enable_floorplan();
  options.density = .0001;                             // Deliberately tight for the coarse 8x8 density map.
  options.die = options.core      = {0, 0, 500, 500};  // Keep the area/density centroid circle feasible.
  options.fp_timing_after_density = true;
  options.fp_timing_movement      = 1e9;
  unsigned calls                  = 0;
  options.fp_evaluate             = [&](const Result& state) {
    ++calls;
    return timing(state);
  };
  const auto placed = place(*graph, flat, options);
  equivalent(placed);
  std::ifstream       report(options.output_dir + "/macro/fp_place.json");
  const std::string   text{std::istreambuf_iterator<char>(report), {}};
  rapidjson::Document doc;
  doc.Parse(text.c_str());
  ASSERT_FALSE(doc.HasParseError());
  ASSERT_EQ(doc["timing_checkpoints"].GetUint(), 1U);
  EXPECT_GE(calls, 2U);  // The final callback still evaluates the resize sweep.
  EXPECT_EQ(doc["timing_sta_calls"].GetUint(), calls);
  const auto& schedule = doc["timing_density_schedule"];
  ASSERT_EQ(schedule.Size(), 3U);
  EXPECT_FALSE(schedule[0]["invoked"].GetBool());
  EXPECT_FALSE(schedule[1]["invoked"].GetBool());
  EXPECT_TRUE(schedule[2]["forced"].GetBool());
  EXPECT_TRUE(schedule[2]["invoked"].GetBool());
  EXPECT_EQ(schedule[2]["iteration"].GetInt(), 120);
}
TEST_F(Buffer_repair, FloorplanRemovesBuffersAndRollsBackRejectedTopology) {
  auto b = gate("sky130_fd_sc_hd__buf_1", "buffer", graph->get_input_pin("a"));
  b.attr(attrs::preroute_net_exclude).set();
  graph->get_output_pin("y").connect_driver(gate("sky130_fd_sc_hd__inv_1", "macro", b));
  enable_floorplan();
  const auto directory = options.output_dir;
  for (const auto* source_name : {"synthesis", "placement", "memory_first"}) {
    options.fp_color_source = source_name;
    options.fp_pin_adjust   = options.fp_color_source != "synthesis";
    for (bool accept : {false, true}) {
      options.output_dir  = directory + "/" + source_name + (accept ? "/accepted" : "/rejected");
      options.fp_evaluate = [&](const Result& state) {
        auto       score   = timing(state);  // validates real graph, cell/pin geometry and RC
        const auto buffers = count(state, "buffer");
        score.delay        = buffers ? 2 : accept ? 1 : 3;
        score.wns = score.tns   = -1;
        score.hold_wns          = 0;
        score.violations        = 0;
        score.electrical_excess = 0;
        return score;
      };
      hhds::GraphLibrary owner;
      const auto         placed = place(*graph, owner, options);
      EXPECT_EQ(count(placed, "buffer"), accept ? 0U : 1U);
      EXPECT_EQ(placed.fp_repair_edits, accept ? 1U : 0U);
      EXPECT_EQ(gu::preroute_net_excluded(placed.graph->get_input_pin("a")), accept);
      EXPECT_FALSE(gu::preroute_net_excluded(graph->get_input_pin("a")));
      equivalent(placed);
      EXPECT_EQ(count(Result{.graph = graph}, "buffer"), 1U);  // source stays read-only
      check_legality(physical::read_lef(options.lefs), placed.floorplan, options.halo_x, options.halo_y);
    }
  }
}
TEST_F(Buffer_repair, FloorplanRebuildsFanoutWithSteinerSeedsAndOwnerColors) {
  auto root = gate("sky130_fd_sc_hd__buf_1", "root", graph->get_input_pin("a"));
  root.attr(attrs::preroute_net_exclude).set();
  for (unsigned i = 0; i < 6; ++i) {
    const auto name = i ? "y" + std::to_string(i) : std::string("y");
    if (i) {
      graph->get_io()->add_output(name, i);
      graph->get_io()->set_bits(name, 1);
    }
    graph->get_output_pin(name).connect_driver(gate("sky130_fd_sc_hd__inv_1", "macro" + std::to_string(i), root));
  }
  enable_floorplan();
  const auto directory = options.output_dir;
  for (const auto* source_name : {"synthesis", "placement", "memory_first"}) {
    options.fp_color_source = source_name;
    options.fp_pin_adjust   = options.fp_color_source != "synthesis";
    options.output_dir      = directory + "/" + source_name;
    unsigned saw_tree       = 0;
    options.fp_evaluate     = [&](const Result& state) {
      auto       score     = timing(state);
      const auto inserted  = count(state, "__place_rebuffer");
      saw_tree            += inserted > 0;
      for (auto n : state.graph->body().nodes()) {
        if (n.get_name().starts_with("__place_rebuffer")) {
          EXPECT_TRUE(n.attr(attrs::place).has());
          for (auto pin : n.out_sorted_pins()) {
            EXPECT_TRUE(gu::preroute_net_excluded(pin));
          }
          EXPECT_TRUE(n.attr(attrs::synth_color_group).has());
          if (n.attr(attrs::synth_color_group).has()) {
            EXPECT_EQ(n.attr(attrs::synth_color_group).get(), "bank:color:1");
          }
        }
      }
      score.delay = inserted ? 1 : count(state, "root") ? 2 : 3;
      score.area  = inserted ? 0 : 100;
      score.wns = score.tns   = -1;
      score.hold_wns          = 0;
      score.violations        = 0;
      score.electrical_excess = 0;
      return score;
    };
    hhds::GraphLibrary owner;
    const auto         placed = place(*graph, owner, options);
    EXPECT_GT(saw_tree, 0U);
    EXPECT_GT(placed.fp_repair_edits, 0U);
    EXPECT_GT(count(placed, "__place_rebuffer"), 0U);
    equivalent(placed);
    check_legality(physical::read_lef(options.lefs), placed.floorplan, options.halo_x, options.halo_y);
    if (options.fp_color_source != "synthesis") {
      std::ifstream       f(options.output_dir + "/macro/fp_placement_colors.json");
      const std::string   text{std::istreambuf_iterator<char>(f), {}};
      rapidjson::Document report;
      report.Parse(text.c_str());
      ASSERT_FALSE(report.HasParseError());
      EXPECT_TRUE(report["membership_frozen_during_repair"].GetBool());
      ASSERT_EQ(report["colors"].Size(), 1);
      EXPECT_EQ(report["colors"][0]["macros"].GetInt(), 6);
      EXPECT_GT(report["colors"][0]["members"].GetUint64(), 6);
    }
  }
}
TEST_F(Buffer_repair, TimingPlacementUsesSlackWeightsAndRejectsRegressions) {
  auto p = graph->get_input_pin("a");
  for (unsigned i = 0; i < 3; ++i) {
    p = gate("sky130_fd_sc_hd__buf_1", "b" + std::to_string(i), p);
  }
  p = gate("sky130_fd_sc_hd__inv_1", "invert", p);
  graph->get_output_pin("y").connect_driver(p);
  auto       placed = place(*graph, flat, options);
  const auto before = timing(placed);
  ASSERT_FALSE(before.pins.empty());
  const auto result = timing_place(std::move(placed), options, [&](const Result& r) { return timing(r); }, 2);
  const auto after  = timing(result.placed);
  EXPECT_LE(after.delay, before.delay + 1e-6);
  EXPECT_GE(after.wns.value(), before.wns.value() - 1e-6);
  rapidjson::Document report;
  report.Parse(result.report.c_str());
  ASSERT_EQ(report["rounds"].Size(), 2);
  for (const auto& round : report["rounds"].GetArray()) {
    EXPECT_GT(round["weighted_nets"].GetUint64(), 0);
    bool weighted = false;
    for (const auto& entry : round["weights"].GetObject()) {
      EXPECT_GE(entry.value.GetDouble(), 1);
      EXPECT_LE(entry.value.GetDouble(), 5);
      weighted |= entry.value.GetDouble() > 1;
    }
    EXPECT_TRUE(weighted);
  }
  equivalent(result.placed);
}
TEST(Timing_weights, InheritsReducedPullAcrossEveryTreeEdgeAndRefreshesMembership) {
  std::map<std::string, double> weights{
      {"root", 5},
      {"gone", 4}
  };
  inherit_buffer_weights(weights, "root", {"b0:X", "b1:X", "b2:X"});
  for (const auto* label : {"root", "b0:X", "b1:X", "b2:X"}) {
    EXPECT_DOUBLE_EQ(weights.at(label), 3);
  }
  Result state;
  state.net_weights = weights;
  state.parasitics  = std::make_shared<physical::Parasitics>();
  Timing_feedback timing;
  for (const auto* label : {"root", "b0:X", "b1:X", "b2:X", "new:X"}) {
    physical::Rc_net net;
    net.driver = label;
    net.nodes.push_back({});
    net.nodes.back().pin = label;
    state.parasitics->nets.push_back(net);
    timing.pins[label].slack = std::string_view(label) == "new:X" ? -5 : 1;
  }
  Options options;
  options.timing_smoothing = 1;
  refresh_timing_weights(state, options, timing);
  EXPECT_FALSE(state.net_weights.contains("gone"));
  EXPECT_DOUBLE_EQ(state.net_weights.at("new:X"), 5);
  for (const auto* label : {"root", "b0:X", "b1:X", "b2:X"}) {
    EXPECT_DOUBLE_EQ(state.net_weights.at(label), 1);
  }
  timing.pins["root"].slack = -10;
  refresh_timing_weights(state, options, timing);
  EXPECT_DOUBLE_EQ(state.net_weights.at("root"), 5);
  EXPECT_DOUBLE_EQ(state.net_weights.at("new:X"), 1);
}

TEST_F(Buffer_repair, PreparsedTimingRolesRetainDynamicControlSeeds) {
  Result state;
  state.floorplan.nets
      = {{"control", {{"PIN", "a"}, {"PIN", "x"}}, {}, "SIGNAL"}, {"data", {{"PIN", "d"}, {"PIN", "y"}}, {}, "SIGNAL"}};
  state.parasitics = std::make_shared<physical::Parasitics>();
  Timing_feedback feedback;
  for (const auto* driver : {"a", "d"}) {
    physical::Rc_net net;
    net.driver = driver;
    net.nodes.push_back({});
    net.nodes.back().pin = driver;
    state.parasitics->nets.push_back(net);
    feedback.pins[driver].slack = std::string_view(driver) == "a" ? -100 : -10;
  }
  options.timing_smoothing = .5;
  const auto roles         = prects_library(options.liberty, options.control_ports);
  auto       reused        = state;
  for (const auto* control : {"a", "d"}) {
    state.prects.ports = reused.prects.ports = {control};
    refresh_timing_weights(state, options, feedback);
    refresh_timing_weights(reused, options, feedback, roles);
    EXPECT_EQ(state.net_weights, reused.net_weights);
    EXPECT_DOUBLE_EQ(reused.net_weights.at(control), 1);
    EXPECT_DOUBLE_EQ(reused.net_weights.at(std::string_view(control) == "a" ? "d" : "a"), 3);
  }
  EXPECT_TRUE(roles.ports.empty());
}

TEST_F(Buffer_repair, FinalRecoveryCollapsesLegalCellBufferChainsAndRepeatsSafely) {
  auto p = gate("sky130_fd_sc_hd__inv_1", "driver", graph->get_input_pin("a"));
  p      = gate("sky130_fd_sc_hd__buf_1", "b0", p);
  p      = gate("sky130_fd_sc_hd__buf_1", "b1", p);
  p      = gate("sky130_fd_sc_hd__inv_1", "sink", p);
  graph->get_output_pin("y").connect_driver(p);
  auto                placed = place(*graph, flat, options);
  rapidjson::Document seeded_qor;
  seeded_qor.Parse(placed.qor.c_str());
  rapidjson::Value alignment(rapidjson::kObjectType);
  alignment.AddMember("retained", 2, seeded_qor.GetAllocator());
  seeded_qor.AddMember("satlegalize", alignment, seeded_qor.GetAllocator());
  rapidjson::StringBuffer                    seeded_json;
  rapidjson::Writer<rapidjson::StringBuffer> seeded_writer(seeded_json);
  seeded_qor.Accept(seeded_writer);
  placed.qor          = seeded_json.GetString();
  const auto original = timing(placed);
  auto recovered = repair_buffers(std::move(placed), options, [&](const Result& r) { return timing(r); }, Repair_scope::recovery);
  EXPECT_EQ(count(recovered.placed, "b"), 0);
  EXPECT_LT(recovered.timing->area, original.area);
  rapidjson::Document recovered_qor;
  recovered_qor.Parse(recovered.placed.qor.c_str());
  ASSERT_TRUE(recovered_qor.HasMember("satlegalize"));
  EXPECT_EQ(recovered_qor["satlegalize"]["retained"].GetUint(), 0);
  EXPECT_EQ(recovered_qor["satlegalize"]["previous_retained"].GetUint(), 2);
  EXPECT_STREQ(recovered_qor["satlegalize"]["relaxed_by"].GetString(), "physical_repair");
  equivalent(recovered.placed, 0);
  auto repeated
      = repair_buffers(std::move(recovered.placed), options, [&](const Result& r) { return timing(r); }, Repair_scope::recovery);
  EXPECT_EQ(count(repeated.placed, "b"), 0);
  equivalent(repeated.placed, 0);
}

TEST_F(Buffer_repair, FinalRecoveryRejectsElectricalRegressionAndExtraFanout) {
  auto p = gate("sky130_fd_sc_hd__inv_1", "driver", graph->get_input_pin("a"));
  auto b = gate("sky130_fd_sc_hd__buf_1", "b0", p);
  auto q = gate("sky130_fd_sc_hd__inv_1", "sink", b);
  graph->get_output_pin("y").connect_driver(q);
  auto       placed   = place(*graph, flat, options);
  const auto feedback = [&](const Result& r) {
    auto t = timing(r);
    if (!count(r, "b")) {
      ++t.violations;
      t.electrical_excess += 1;
    }
    return t;
  };
  const auto rejected = repair_buffers(std::move(placed), options, feedback, Repair_scope::recovery);
  EXPECT_EQ(count(rejected.placed, "b"), 1);
  equivalent(rejected.placed, 0);
  auto io = graph->get_io();
  io->add_output("branch", 1);
  io->set_bits("branch", 1);
  graph->get_output_pin("branch").connect_driver(p);  // input net now has a port branch
  timing_session.reset();                             // the interface changed; rebuild its port constraints and timing identities
  hhds::GraphLibrary branch_owner;
  auto               branched = place(*graph, branch_owner, options);
  unsigned           calls    = 0;
  const auto         retained = repair_buffers(
      std::move(branched),
      options,
      [&](const Result& r) {
        ++calls;
        return timing(r);
      },
      Repair_scope::recovery);
  EXPECT_EQ(count(retained.placed, "b"), 1);
  EXPECT_EQ(calls, 1);  // no eligible bypass trial reaches STA
}

TEST(Timing_weights, ConfigurableFractionPreservesDefaultAndExcludesUntimedClocks) {
  physical::Parasitics rc;
  Timing_feedback      timing;
  for (int i = 0; i < 100; ++i) {
    const auto driver = "d" + std::to_string(i), sink = "s" + std::to_string(i);
    rc.nets.push_back({
        driver,
        {{driver, 0}, {sink, 0}},
        {}
    });
    timing.pins[driver].slack = 1000;
    timing.pins[sink].slack   = i - 100.;
  }
  rc.nets.push_back({"clock", {{"clock", 0}}, {}});
  timing.pins["clock"].slack = -1000;
  timing.pins["clock"].clock = true;
  rc.nets.push_back({"untimed", {{"untimed", 0}}, {}});
  const auto original = timing_weights(rc, timing);
  EXPECT_EQ(original.size(), 20U);
  EXPECT_EQ(original, timing_weights(rc, timing, .2));
  EXPECT_EQ(original.at("d0"), 5);
  EXPECT_EQ(original.at("d19"), 1);
  EXPECT_FALSE(original.contains("clock"));
  EXPECT_FALSE(original.contains("untimed"));
  for (auto [fraction, count] : {
           std::pair{  .1,  10U},
           { .14,  14U},
           { .29,  29U},
           {.101,  11U},
           {.001,   1U},
           {  1., 100U}
  }) {
    const auto weights = timing_weights(rc, timing, fraction);
    EXPECT_EQ(weights.size(), count);
    EXPECT_EQ(weights.at("d0"), 5);
    EXPECT_TRUE(weights.contains("d" + std::to_string(count - 1)));
    EXPECT_FALSE(weights.contains("d" + std::to_string(count)));
  }
  for (double bad : {0., -.1, 1.01, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
    EXPECT_THROW(timing_weights(rc, timing, bad), std::invalid_argument);
  }
  const auto weaker = timing_weights(rc, timing, .29, 3);
  EXPECT_EQ(weaker.at("d0"), 3);
  EXPECT_EQ(weaker.at("d28"), 1);
  for (const auto& [name, weight] : timing_weights(rc, timing, .29, 1)) {
    (void)name;
    EXPECT_EQ(weight, 1);
  }
  EXPECT_THROW(timing_weights(rc, timing, .2, .5), std::invalid_argument);
  EXPECT_THROW(timing_weights(rc, timing, .2, std::numeric_limits<double>::infinity()), std::invalid_argument);
  const auto cubic = timing_weights(rc, timing, .2, 5, 3);
  EXPECT_EQ(cubic.size(), original.size());
  EXPECT_DOUBLE_EQ(cubic.at("d0"), 5);
  EXPECT_DOUBLE_EQ(cubic.at("d19"), 1);
  EXPECT_NEAR(cubic.at("d10"), 1 + 4 * std::pow(9. / 19., 3), 1e-12);
  EXPECT_GT((cubic.at("d1") - 1) / (cubic.at("d10") - 1), 7.9);
  for (double bad : {0., .5, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
    EXPECT_THROW(timing_weights(rc, timing, .2, 5, bad), std::invalid_argument);
  }
}
TEST_F(Buffer_repair, TimingWeightsExcludeClockNetsAndRevertWorsePlacements) {
  auto b = gate("sky130_fd_sc_hd__buf_1", "clock_buffer", graph->get_input_pin("a"));
  auto p = gate("sky130_fd_sc_hd__inv_1", "invert", b);
  graph->get_output_pin("y").connect_driver(p);
  auto       placed         = place(*graph, flat, options);
  const auto identity       = placed.parasitics->identity;
  options.timing_max_weight = 3;
  options.timing_smoothing  = .25;
  unsigned   evaluations    = 0;
  const auto result         = timing_place(
      std::move(placed),
      options,
      [&](const Result& r) {
        auto f                      = timing(r);
        f.pins.at("invert:A").clock = true;
        f.delay                     = evaluations++ ? 100 : 1;
        return f;
      },
      2);
  EXPECT_EQ(result.placed.parasitics->identity, identity);
  EXPECT_TRUE(result.libraries.empty());
  rapidjson::Document report;
  report.Parse(result.report.c_str());
  ASSERT_EQ(report["rounds"].Size(), 2);
  EXPECT_EQ(report["timing_max_weight"].GetDouble(), 3);
  EXPECT_EQ(report["timing_smoothing"].GetDouble(), .25);
  double peak = 1;
  for (const auto& weight : report["rounds"][0]["weights"].GetObject()) {
    peak = std::max(peak, weight.value.GetDouble());
  }
  // The entire buffered clock, including its upstream port net, is excluded.
  EXPECT_DOUBLE_EQ(peak, 1);
  for (const auto& round : report["rounds"].GetArray()) {
    EXPECT_EQ(round["weights"]["clock_buffer:X"].GetDouble(), 1);
    EXPECT_FALSE(round["accepted"].GetBool());
  }
}
TEST_F(Buffer_repair, PreservesCriticalAndNameTieOrderForRepairCandidates) {
  // Creation order deliberately differs from the priority/name order. Rejected
  // trials leave the graph and feedback fixed throughout candidate enumeration.
  unsigned port_id = 0;
  for (const auto* name : {"z_noncritical", "b_critical", "a_noncritical"}) {
    const auto port = port_id ? "y" + std::to_string(port_id) : "y";
    if (port_id) {
      graph->get_io()->add_output(port, port_id);
      graph->get_io()->set_bits(port, 1);
    }
    graph->get_output_pin(port).connect_driver(gate("sky130_fd_sc_hd__buf_1", name, graph->get_input_pin("a")));
    ++port_id;
  }
  auto            placed = place(*graph, flat, options);
  Timing_feedback before;
  before.delay = before.area = 1;
  before.path                = {"b_critical:X"};
  const auto result          = repair_buffers(
      std::move(placed),
      options,
      [&](const Result&) {
        auto after  = before;
        after.delay = 2;
        return after;
      },
      Repair_scope::sizing,
      before);
  rapidjson::Document report;
  report.Parse(result.report.c_str());
  ASSERT_GE(report["trials"].Size(), 3);
  const std::vector<std::string> expected{"b_critical", "a_noncritical", "z_noncritical"};
  for (unsigned i = 0; i < expected.size(); ++i) {
    const auto& trial = report["trials"][i];
    EXPECT_EQ(std::string_view(trial["action"].GetString()), "remove");
    EXPECT_EQ(std::string_view(trial["instance"].GetString()), expected[i]);
    EXPECT_FALSE(trial["accepted"].GetBool());
  }
  EXPECT_EQ(report["accepted"].GetUint(), 0);
}
TEST_F(Buffer_repair, ProtectsClockLogicThroughAliasesAndBuffers) {
  auto p  = gate("sky130_fd_sc_hd__buf_1", "clock_buffer", graph->get_input_pin("a"));
  p       = gate("sky130_fd_sc_hd__inv_1", "clock_inverter", p);
  p       = gu::create_get_mask(*graph, p, 0).get_driver_pin(0);
  auto io = source.create_io("sky130_fd_sc_hd__dfxtp_1");
  io->add_input("D", 1);
  io->add_input("CLK", 2);
  io->add_output("Q", 3);
  for (const auto& name : {"D", "CLK", "Q"}) {
    io->set_bits(name, 1);
  }
  auto n = gu::create_typed_node(*graph, Ntype_op::Sub);
  n.set_subnode(io);
  n.set_name("state");
  n.create_sink_pin("D").connect_driver(graph->get_input_pin("a"));
  n.create_sink_pin("CLK").connect_driver(p);
  auto q = n.create_driver_pin("Q");
  gu::set_bits(q, 1);
  graph->get_output_pin("y").connect_driver(q);
  auto       placed = place(*graph, flat, options);
  const auto result = repair_buffers(std::move(placed), options, [&](const Result& r) {
    auto f = timing(r);
    f.wns = f.tns       = -1;
    f.hold_wns          = 0;
    f.violations        = 0;
    f.electrical_excess = 0;
    f.delay             = count(r, "clock_buffer") ? 10 : 1;
    f.area              = 1;
    return f;
  });
  EXPECT_EQ(count(result.placed, "clock_buffer"), 1);
  rapidjson::Document report;
  report.Parse(result.report.c_str());
  EXPECT_EQ(report["attempts"].GetUint(), 0);
}
TEST_F(Buffer_repair, ExplicitControlRootProtectsEveryBufferedBranch) {
  auto root   = gate("sky130_fd_sc_hd__buf_1", "root", graph->get_input_pin("a"));
  auto first  = gate("sky130_fd_sc_hd__inv_1", "first", root);
  auto second = gate("sky130_fd_sc_hd__buf_1", "second", root);
  graph->get_output_pin("y").connect_driver(first);
  (void)second;
  options.control_ports = "a";
  const auto evaluate   = [&](const Result& r) {
    Timing_feedback feedback;
    feedback.delay = 10 * r.floorplan.components.size();
    feedback.area  = r.floorplan.components.size();
    return feedback;
  };
  auto initial = place(*graph, flat, options);
  auto result  = repair_buffers(std::move(initial), options, evaluate);
  EXPECT_EQ(count(result.placed, "root"), 1);
  EXPECT_EQ(count(result.placed, "first"), 1);
  EXPECT_EQ(count(result.placed, "second"), 1);
  rapidjson::Document report;
  report.Parse(result.report.c_str());
  EXPECT_EQ(report["attempts"].GetUint(), 0);
}

TEST_F(Buffer_repair, ResetLogicIsProtectedAndRepairOverrideIsRejected) {
  // A buffer chain whose only load is the reset pin of a state cell: the cone is
  // protected like clock logic, so a high-fanout reset net would never be sized
  // or buffered. The former reset-repair override must now be rejected.
  std::filesystem::create_directories(options.output_dir);
  const auto slurp = [](const std::string& path) {
    std::ifstream input(path);
    return std::string{std::istreambuf_iterator<char>(input), {}};
  };
  const auto clone_cell = [](std::string        text,
                             const std::string& open,
                             const std::string& close,
                             const std::string& extra,
                             const std::string& extra_anchor) {
    const auto start = text.find(open);
    EXPECT_NE(start, std::string::npos);
    const auto end = text.find(close, start);
    EXPECT_NE(end, std::string::npos);
    std::string clone = text.substr(start, end - start + close.size());
    for (size_t at = clone.find("dfxtp_1"); at != std::string::npos; at = clone.find("dfxtp_1", at)) {
      clone.replace(at, 7, "dfrtp_1");
    }
    const auto anchor = clone.find(extra_anchor);
    EXPECT_NE(anchor, std::string::npos);
    clone.insert(anchor, extra);
    text.insert(end + close.size(), "\n" + clone + "\n");
    return text;
  };
  // A reset-pin clone of the plain flop: same geometry and timing plus RESET_B.
  auto       lef        = clone_cell(slurp(options.lefs[1]),
                        "MACRO sky130_fd_sc_hd__dfxtp_1\n",
                        "END sky130_fd_sc_hd__dfxtp_1\n",
                        "  PIN RESET_B\n    DIRECTION INPUT ;\n    USE SIGNAL ;\n    PORT\n      LAYER li1 ;\n"
                                     "        RECT 3.370 0.715 3.650 1.665 ;\n    END\n  END RESET_B\n",
                        "  PIN VGND\n");
  auto       lib        = slurp(options.liberty);
  const auto cell_start = lib.find("cell (\"sky130_fd_sc_hd__dfxtp_1\") {");
  ASSERT_NE(cell_start, std::string::npos);
  size_t depth = 0, cell_end = cell_start;
  for (size_t at = lib.find('{', cell_start); at < lib.size(); ++at) {
    depth += lib[at] == '{';
    depth -= lib[at] == '}';
    if (depth == 0) {
      cell_end = at + 1;
      break;
    }
  }
  std::string clone = lib.substr(cell_start, cell_end - cell_start);
  for (size_t at = clone.find("dfxtp_1"); at != std::string::npos; at = clone.find("dfxtp_1", at)) {
    clone.replace(at, 7, "dfrtp_1");
  }
  const auto pin_at = clone.find("pin (\"CLK\")");
  ASSERT_NE(pin_at, std::string::npos);
  clone.insert(pin_at,
               "pin (\"RESET_B\") {\n capacitance : 0.0016780000;\n clock : \"false\";\n direction : \"input\";\n"
               " max_transition : 1.5000000000;\n related_ground_pin : \"VGND\";\n related_power_pin : \"VPWR\";\n }\n");
  lib.insert(cell_end, "\n" + clone + "\n");
  options.lefs[1] = options.output_dir + "/reset.lef";
  options.liberty = options.output_dir + "/reset.lib";
  std::ofstream(options.lefs[1]) << lef;
  std::ofstream(options.liberty) << lib;

  auto p  = gate("sky130_fd_sc_hd__buf_1", "reset_buffer", graph->get_input_pin("a"));
  p       = gate("sky130_fd_sc_hd__inv_1", "reset_inverter", p);
  auto io = source.create_io("sky130_fd_sc_hd__dfrtp_1");
  io->add_input("D", 1);
  io->add_input("CLK", 2);
  io->add_input("RESET_B", 3);
  io->add_output("Q", 4);
  for (const auto& name : {"D", "CLK", "RESET_B", "Q"}) {
    io->set_bits(name, 1);
  }
  auto n = gu::create_typed_node(*graph, Ntype_op::Sub);
  n.set_subnode(io);
  n.set_name("state");
  n.create_sink_pin("D").connect_driver(graph->get_input_pin("a"));
  n.create_sink_pin("CLK").connect_driver(graph->get_input_pin("a"));
  n.create_sink_pin("RESET_B").connect_driver(p);
  auto q = n.create_driver_pin("Q");
  gu::set_bits(q, 1);
  graph->get_output_pin("y").connect_driver(q);

  // A controlled objective isolates the protection rule from the timing heuristic:
  // only dropping the reset buffer is an improvement.
  const auto evaluate = [&](const Result& r) {
    Timing_feedback f;
    f.delay = count(r, "reset_buffer") ? 10 : 1;
    f.area  = r.floorplan.components.size();
    return f;
  };
  auto       placed     = place(*graph, flat, options);
  const auto protected_ = repair_buffers(std::move(placed), options, evaluate);
  EXPECT_EQ(count(protected_.placed, "reset_buffer"), 1);
  rapidjson::Document report;
  report.Parse(protected_.report.c_str());
  EXPECT_EQ(report["attempts"].GetUint(), 0);

  options.repair_reset = true;
  EXPECT_THROW((void)place(*graph, flat, options), std::invalid_argument);
  options.repair_reset = false;
  options.reset_fanout = 32;  // the reset tree (reset_tree.hpp) is not reset repair: a small net stays as it is
  hhds::GraphLibrary treed_flat;
  const auto         treed = repair_buffers(place(*graph, treed_flat, options), options, evaluate);
  EXPECT_EQ(count(treed.placed, "reset_buffer"), 1);
}
TEST_F(Buffer_repair, RejectsWorseTrialsAndRebuildsEveryFanoutBranch) {
  auto root = gate("sky130_fd_sc_hd__buf_1", "root", graph->get_input_pin("a"));
  for (unsigned i = 0; i < 6; ++i) {
    auto       p    = gate("sky130_fd_sc_hd__inv_1", "leaf" + std::to_string(i), root);
    const auto name = i == 0 ? std::string("y") : "y" + std::to_string(i);
    if (i) {
      graph->get_io()->add_output(name, i);
      graph->get_io()->set_bits(name, 1);
    }
    graph->get_output_pin(name).connect_driver(p);
  }
  auto       placed   = place(*graph, flat, options);
  // Controlled objective isolates the rebuffer mutation/rollback machinery
  // from the timing heuristic: only a newly balanced tree is an improvement.
  const auto evaluate = [&](const Result& r) {
    Timing_feedback f;
    f.delay = count(r, "__place_rebuffer") >= 3 ? 5 : count(r, "root") ? 10 : 20;
    f.area  = r.floorplan.components.size();
    return f;
  };
  const auto repaired = repair_buffers(std::move(placed), options, evaluate);
  EXPECT_GT(count(repaired.placed, "__place_rebuffer"), 1);
  EXPECT_EQ(count(repaired.placed, "root"), 0);
  equivalent(repaired.placed);
  rapidjson::Document report;
  report.Parse(repaired.report.c_str());
  bool rejected_removal = false, accepted_rebuild = false;
  for (const auto& trial : report["trials"].GetArray()) {
    rejected_removal |= std::string_view(trial["action"].GetString()) == "remove" && !trial["accepted"].GetBool();
    accepted_rebuild |= std::string_view(trial["action"].GetString()) == "rebuild" && trial["accepted"].GetBool();
  }
  EXPECT_TRUE(rejected_removal);
  EXPECT_TRUE(accepted_rebuild);
}
TEST_F(Buffer_repair, LegacySizingHonorsDontUseWithoutLosingExistingModels) {
  add_allowed_buffer_variant();
  const std::string weak = "sky130_fd_sc_hd__buf_1", strong = "sky130_fd_sc_hd__buf_2";
  graph->get_output_pin("y").connect_driver(gate(weak, "root", graph->get_input_pin("a")));
  const auto evaluate = [&](const Result& r) {
    Timing_feedback f;
    const bool      sized    = r.floorplan.components.at(0).macro == strong;
    f.delay                  = sized ? 1 : 10;
    f.area                   = r.floorplan.components.size();
    f.pins["root:X"].load_ff = 100;
    f.pins["root:X"].slack   = -1;
    return f;
  };
  for (bool excluded : {false, true}) {
    SCOPED_TRACE(excluded);
    // Excluding the existing master must still allow its characterization and
    // replacement by a legal master. Excluding both leaves the design intact.
    options.resize_dont_use = {excluded ? "*__buf_?" : "*__buf_1"};
    hhds::GraphLibrary owner;
    auto               initial = place(*graph, owner, options);
    const auto         result  = repair_buffers(std::move(initial), options, evaluate, Repair_scope::sizing);
    ASSERT_EQ(result.placed.floorplan.components.size(), 1);
    EXPECT_EQ(result.placed.floorplan.components.front().macro, excluded ? weak : strong) << result.report;
    equivalent(result.placed, 0);
    check_legality(physical::read_lef(options.lefs), result.placed.floorplan);
  }
}
TEST_F(Buffer_repair, LegacyRebufferHonorsDontUseForEveryInsertedDrive) {
  add_allowed_buffer_variant();
  auto root = gate("sky130_fd_sc_hd__buf_1", "root", graph->get_input_pin("a"));
  for (unsigned i = 0; i < 6; ++i) {
    auto       p    = gate("sky130_fd_sc_hd__inv_1", "leaf" + std::to_string(i), root);
    const auto port = i ? "y" + std::to_string(i) : "y";
    if (i) {
      graph->get_io()->add_output(port, i);
      graph->get_io()->set_bits(port, 1);
    }
    graph->get_output_pin(port).connect_driver(p);
  }
  options.resize_dont_use = {"*__buf_1"};
  for (bool pin_feedback : {false, true}) {
    SCOPED_TRACE(pin_feedback);
    hhds::GraphLibrary owner;
    auto               placed   = place(*graph, owner, options);
    const auto         evaluate = [&](const Result& r) {
      Timing_feedback f;
      f.delay = count(r, "__place_rebuffer") ? 5 : count(r, "root") ? 10 : 20;
      f.area  = r.floorplan.components.size();
      if (pin_feedback) {
        f.wns = f.tns = -f.delay;
        for (const auto& net : r.parasitics->nets) {
          for (const auto& point : net.nodes) {
            if (point.pin.empty()) {
              continue;
            }
            auto& pin   = f.pins[point.pin];
            pin.slack   = -1;
            pin.load_ff = pin.capacitance_ff = 100;
            pin.required                     = 0;
          }
        }
      }
      return f;
    };
    const auto result = repair_buffers(std::move(placed), options, evaluate, Repair_scope::design);
    EXPECT_GT(count(result.placed, "__place_rebuffer"), 0) << result.report;
    for (const auto& cell : result.placed.floorplan.components) {
      if (cell.name.starts_with("__place_rebuffer")) {
        EXPECT_EQ(cell.macro, "sky130_fd_sc_hd__buf_2") << result.report;
      }
    }
    equivalent(result.placed);
    check_legality(physical::read_lef(options.lefs), result.placed.floorplan);
  }
}
TEST_F(Buffer_repair, RealTimingSteinerRepairReducesLongWireViolations) {
  options.resistance_ohm_um = 50;
  options.capacitance_ff_um = 5;
  auto root                 = gate("sky130_fd_sc_hd__buf_1", "root", graph->get_input_pin("a"));
  for (unsigned i = 0; i < 6; ++i) {
    auto       p    = gate("sky130_fd_sc_hd__inv_1", "leaf" + std::to_string(i), root);
    const auto name = i == 0 ? std::string("y") : "y" + std::to_string(i);
    if (i) {
      graph->get_io()->add_output(name, i);
      graph->get_io()->set_bits(name, 1);
    }
    graph->get_output_pin(name).connect_driver(p);
  }
  auto       initial = place(*graph, flat, options);
  auto       frame   = initial.floorplan;
  const auto site    = frame.rows.front().site;
  frame.rows.clear();
  frame.die = {0, 0, 276000, 27200};
  for (unsigned i = 0; i < 10; ++i) {
    frame.rows.push_back({"ROW_" + std::to_string(i), site, 0, i * 2720, 600, 460, Ann_place::Orientation::N});
  }
  unsigned row = 0;
  for (auto& c : frame.components) {
    c.place.status      = Ann_place::Status::PLACED;
    c.place.orientation = Ann_place::Orientation::N;
    c.place.x           = c.name == "root" ? 460 : 267260;
    c.place.y           = c.name == "root" ? 0 : row++ * 2720;
  }
  for (auto& t : frame.terminals) {
    t.status = Ann_place::Status::FIXED;
    t.x      = t.name == "a" ? 0 : 276000;
    t.y      = 0;
  }
  options.previous = std::make_shared<physical::Floorplan>(frame);
  hhds::GraphLibrary stretched;
  auto               placed = place(*graph, stretched, options);
  const auto         before = timing(placed);
  ASSERT_GT(before.violations, 0);
  ASSERT_TRUE(before.electrical_pin_feedback);
  unsigned pin_violations = 0;
  double   pin_excess     = 0;
  for (const auto& [name, pin] : before.pins) {
    pin_violations += pin.violations;
    pin_excess     += pin.electrical_excess;
  }
  EXPECT_EQ(pin_violations, before.violations);
  EXPECT_NEAR(pin_excess, before.electrical_excess, before.electrical_excess * 1e-7);
  const auto result = repair_buffers(std::move(placed), options, [&](const Result& r) { return timing(r); }, Repair_scope::design);
  const auto after  = timing(result.placed);
  SCOPED_TRACE(result.report);
  EXPECT_GT(count(result.placed, "__place_rebuffer"), 0);
  EXPECT_LT(after.violations, before.violations);
  EXPECT_LT(after.delay, before.delay);
  equivalent(result.placed);
  rapidjson::Document qor;
  qor.Parse(result.placed.qor.c_str());
  EXPECT_TRUE(qor["eco"]["single_cell"].GetBool());
}
TEST_F(Buffer_repair, SteinerRepairSplitsAliasesAndPreservesOtherPackedBits) {
  graph->get_io()->add_input("guard", 1);
  graph->get_io()->set_bits("guard", 1);
  gu::set_bits(graph->get_input_pin("guard"), 1);
  auto root  = gate("sky130_fd_sc_hd__buf_1", "root", graph->get_input_pin("a"));
  auto alias = gu::create_get_mask(*graph, root, 0).get_driver_pin(0);
  for (unsigned i = 0; i < 6; ++i) {
    auto       p    = gate("sky130_fd_sc_hd__inv_1", "leaf" + std::to_string(i), alias);
    const auto name = i == 0 ? std::string("y") : "y" + std::to_string(i);
    if (i) {
      graph->get_io()->add_output(name, i);
      graph->get_io()->set_bits(name, 1);
    }
    graph->get_output_pin(name).connect_driver(p);
  }
  graph->get_io()->add_output("packed", 6);
  graph->get_io()->set_bits("packed", 2);
  auto concat = gu::create_typed_node(*graph, Ntype_op::Concat);
  auto one    = gu::create_const(*graph, *Dlop::create_integer(1));
  concat.create_sink_pin(0).connect_driver(graph->get_input_pin("guard"));
  concat.create_sink_pin(1).connect_driver(one);
  concat.create_sink_pin(2).connect_driver(alias);
  concat.create_sink_pin(3).connect_driver(one);
  auto packed = concat.create_driver_pin(0);
  gu::set_bits(packed, 2);
  gu::set_unsign(packed);
  graph->get_output_pin("packed").connect_driver(packed);
  auto       placed   = place(*graph, flat, options);
  const auto repaired = repair_buffers(std::move(placed), options, [&](const Result& r) {
    Timing_feedback f;
    f.delay = count(r, "__place_rebuffer") >= 3 ? 5 : count(r, "root") ? 10 : 20;
    f.area  = r.floorplan.components.size();
    return f;
  });
  ASSERT_GT(count(repaired.placed, "__place_rebuffer"), 0) << repaired.report;
  const auto evaluate = [&](auto&& self, hhds::Pin_class p, int bit, unsigned pattern, unsigned depth) -> bool {
    if (depth > 512 || p.is_invalid()) {
      throw std::runtime_error("invalid repaired wiring");
    }
    if (p.is_const()) {
      return gu::const_of(p).bit_test(bit);
    }
    auto n = p.get_master_node();
    if (n.is_input_node()) {
      return (pattern >> (p.get_pin_name() == "guard" ? 1 : 0)) & 1;
    }
    const auto input
        = [&](const char* name, int b) { return self(self, gu::get_driver_of_sink_name(n, name), b, pattern, depth + 1); };
    if (const auto io = n.get_subnode_io()) {
      return input("A", 0) ^ (io->get_name() == "sky130_fd_sc_hd__inv_1");
    }
    const auto op = gu::type_op_of(n);
    if (op == Ntype_op::Get_mask || op == Ntype_op::Set_mask) {
      const auto range = gu::bit_range(n).value();
      if (op == Ntype_op::Get_mask) {
        return input("a", bit + range.first);
      }
      return bit >= range.first && bit < range.second ? input("value", bit - range.first) : input("a", bit);
    }
    if (op == Ntype_op::Concat) {
      for (const auto& lane : gu::concat_lanes(n)) {
        if (bit >= lane.offset && bit < lane.offset + lane.width) {
          return self(self, lane.value, bit - lane.offset, pattern, depth + 1);
        }
      }
    }
    throw std::runtime_error("unexpected repaired Boolean operation");
  };
  for (unsigned pattern = 0; pattern < 4; ++pattern) {
    for (const auto& port : repaired.placed.graph->get_io()->get_output_pin_decls()) {
      const auto p = repaired.placed.graph->get_output_pin(port.name).get_driver_pin();
      if (port.name == "packed") {
        EXPECT_EQ(evaluate(evaluate, p, 0, pattern, 0), bool(pattern & 1));
        EXPECT_EQ(evaluate(evaluate, p, 1, pattern, 0), bool(pattern & 2));
      } else {
        EXPECT_EQ(evaluate(evaluate, p, 0, pattern, 0), !(pattern & 1));
      }
    }
  }
  EXPECT_NE(repaired.report.find("flute_steiner"), std::string::npos);
}
TEST_F(Buffer_repair, BuffersPrimaryInputBusBitsWithoutChangingOtherBits) {
  graph->get_io()->set_bits("a", 2);
  auto a = graph->get_input_pin("a");
  gu::set_bits(a, 2);
  auto bit0 = gu::create_get_mask(*graph, a, 0).get_driver_pin(0);
  auto bit1 = gu::create_get_mask(*graph, a, 1).get_driver_pin(0);
  for (unsigned i = 0; i < 6; ++i) {
    const auto port = i ? "y" + std::to_string(i) : "y";
    if (i) {
      graph->get_io()->add_output(port, i);
      graph->get_io()->set_bits(port, 1);
    }
    graph->get_output_pin(port).connect_driver(gate("sky130_fd_sc_hd__inv_1", "leaf" + std::to_string(i), bit0));
  }
  graph->get_io()->add_output("guard", 6);
  graph->get_io()->set_bits("guard", 1);
  graph->get_output_pin("guard").connect_driver(bit1);
  auto       initial = place(*graph, flat, options);
  const auto result  = repair_buffers(
      std::move(initial),
      options,
      [&](const Result& trial) {
        auto       score = timing(trial);  // real STA verifies every new connection/RC endpoint
        const bool split = count(trial, "__place_rebuffer") > 0;
        score.delay      = split ? 1 : 2;
        score.area       = split ? 0 : 100;
        score.wns = score.tns   = -1;
        score.hold_wns          = 0;
        score.violations        = 0;
        score.electrical_excess = 0;
        return score;
      },
      Repair_scope::design);
  ASSERT_GT(count(result.placed, "__place_rebuffer"), 0) << result.report;
  const auto value = [&](auto&& self, hhds::Pin_class p, unsigned bit, unsigned pattern, unsigned depth) -> bool {
    if (depth > 512 || p.is_invalid()) {
      throw std::runtime_error("broken bus repair");
    }
    const auto n = p.get_master_node();
    if (n.is_input_node()) {
      return (pattern >> bit) & 1;
    }
    if (const auto io = n.get_subnode_io()) {
      return self(self, n.get_sink_pin("A").get_driver_pin(), 0, pattern, depth + 1) ^ (io->get_name() == "sky130_fd_sc_hd__inv_1");
    }
    if (gu::type_op_of(n) == Ntype_op::Get_mask) {
      return self(self, gu::get_driver_of_sink_name(n, "a"), bit + gu::bit_range(n)->first, pattern, depth + 1);
    }
    throw std::runtime_error("unexpected bus repair operation");
  };
  for (unsigned pattern = 0; pattern < 4; ++pattern) {
    for (const auto& port : result.placed.graph->get_io()->get_output_pin_decls()) {
      EXPECT_EQ(value(value, result.placed.graph->get_output_pin(port.name).get_driver_pin(), 0, pattern, 0),
                port.name == "guard" ? bool(pattern & 2) : !(pattern & 1));
    }
  }
  EXPECT_EQ(count(Result{.graph = graph}, "__place_rebuffer"), 0);
  check_legality(physical::read_lef(options.lefs), result.placed.floorplan);
}
TEST_F(Buffer_repair, HealthyCharacterizedStateOutputDoesNotRequestAFanoutTree) {
  std::ifstream input(options.liberty);
  std::string   lib{std::istreambuf_iterator<char>(input), {}};
  const auto    begin = lib.find("cell (\"sky130_fd_sc_hd__buf_1\") {");
  ASSERT_NE(begin, std::string::npos);
  const auto open  = lib.find('{', begin);
  size_t     end   = open + 1;
  int        depth = 1;
  for (; end < lib.size() && depth; ++end) {
    depth += (lib[end] == '{') - (lib[end] == '}');
  }
  ASSERT_EQ(depth, 0);
  auto cell                = lib.substr(open + 1, end - open - 2);
  // A one-pin state element (data and clock share A) reuses this small LEF.
  // Synthetic timing below isolates eligibility; it is not a clock model test.
  cell                     = std::regex_replace(cell, std::regex(R"rx(function\s*:\s*"\(A\)")rx"), "function : \"IQ\"");
  const std::string master = "test__buf_state";
  lib.insert(lib.rfind('}'), "cell (\"" + master + "\") {ff(IQ,IQN) {next_state : \"A\"; clocked_on : \"A\";}\n" + cell + "}\n");
  std::filesystem::create_directories(options.output_dir);
  options.liberty = options.output_dir + "/state.lib";
  std::ofstream(options.liberty) << lib;
  std::ifstream     lef_input(options.lefs.back());
  std::string       lef{std::istreambuf_iterator<char>(lef_input), {}};
  const std::string original    = "sky130_fd_sc_hd__buf_1";
  const auto        macro_begin = lef.find("MACRO " + original);
  const auto        macro_end   = lef.find("END " + original, macro_begin) + 4 + original.size();
  auto              macro       = lef.substr(macro_begin, macro_end - macro_begin);
  for (size_t at = macro.find(original); at != std::string::npos; at = macro.find(original, at + master.size())) {
    macro.replace(at, original.size(), master);
  }
  options.lefs.push_back(options.output_dir + "/state.lef");
  std::ofstream(options.lefs.back()) << "VERSION 5.8 ;\n" << macro << "\nEND LIBRARY\n";
  auto root = gate(master, "state", graph->get_input_pin("a"));
  for (unsigned i = 0; i < 6; ++i) {
    const auto port = i ? "y" + std::to_string(i) : "y";
    if (i) {
      graph->get_io()->add_output(port, i);
      graph->get_io()->set_bits(port, 1);
    }
    graph->get_output_pin(port).connect_driver(root);
  }
  auto       initial  = place(*graph, flat, options);
  const auto evaluate = [&](const Result& state) {
    Timing_feedback f;
    f.delay = 1;
    f.area  = state.floorplan.components.size();
    f.wns   = 100;
    f.tns   = 0;
    for (const auto& net : state.parasitics->nets) {
      for (const auto& point : net.nodes) {
        if (point.pin.empty()) {
          continue;
        }
        auto& pin          = f.pins[point.pin];
        pin.slack          = 100;
        pin.slew           = .001;
        pin.load_ff        = .01;
        pin.capacitance_ff = .01;
      }
    }
    return f;
  };
  const auto          result = repair_buffers(initial, options, evaluate, Repair_scope::forest);
  rapidjson::Document report;
  report.Parse(result.report.c_str());
  ASSERT_TRUE(report.IsObject()) << result.report;
  EXPECT_TRUE(report["trials"].Empty()) << result.report;
  EXPECT_EQ(count(result.placed, "__place_rebuffer"), 0);
  const auto stressed = repair_buffers(
      initial,
      options,
      [&](const Result& r) {
        auto f                    = evaluate(r);
        f.pins.at("state:X").slew = 1e6;
        return f;
      },
      Repair_scope::forest);
  report.Parse(stressed.report.c_str());
  EXPECT_FALSE(report["trials"].Empty()) << stressed.report;
}
TEST_F(Buffer_repair, HealthyPositiveSlackGateDoesNotGetAnArtificialFanoutFourTree) {
  auto root = gate("sky130_fd_sc_hd__inv_1", "driver", graph->get_input_pin("a"));
  for (unsigned i = 0; i < 6; ++i) {
    const auto port = i ? "y" + std::to_string(i) : "y";
    if (i) {
      graph->get_io()->add_output(port, i);
      graph->get_io()->set_bits(port, 1);
    }
    graph->get_output_pin(port).connect_driver(root);
  }
  auto initial = place(*graph, flat, options);
  auto result  = repair_buffers(
      std::move(initial),
      options,
      [&](const Result& trial) {
        auto score = timing(trial);
        for (auto& [name, pin] : score.pins) {
          (void)name;
          pin.slack   = 1;
          pin.slew    = .001;
          pin.load_ff = .01;
        }
        return score;
      },
      Repair_scope::design);
  EXPECT_EQ(count(result.placed, "__place_rebuffer"), 0) << result.report;
  // Rejected-by-STA trials also leave no buffer behind: no tree may even be proposed.
  rapidjson::Document report;
  report.Parse(result.report.c_str());
  ASSERT_TRUE(report.IsObject()) << result.report;
  for (const auto& trial : report["trials"].GetArray()) {
    EXPECT_FALSE(trial.HasMember("action") && std::string(trial["action"].GetString()) == "forest_sweep") << result.report;
  }
  equivalent(result.placed);
}
TEST_F(Buffer_repair, KeepsDistributionForksWhileRemovingLinearBufferChains) {
  auto root  = gate("sky130_fd_sc_hd__inv_1", "driver", graph->get_input_pin("a"));
  auto chain = gate("sky130_fd_sc_hd__buf_1", "old_linear", root);
  auto fork  = gate("sky130_fd_sc_hd__buf_1", "distribution_fork", chain);
  for (unsigned i = 0; i < 6; ++i) {
    const auto port = i ? "y" + std::to_string(i) : "y";
    if (i) {
      graph->get_io()->add_output(port, i);
      graph->get_io()->set_bits(port, 1);
    }
    graph->get_output_pin(port).connect_driver(gate("sky130_fd_sc_hd__buf_1", "leaf_chain" + std::to_string(i), fork));
  }
  auto       original = place(*graph, flat, options);
  const auto result   = repair_buffers(
      std::move(original),
      options,
      [&](const Result& trial) {
        auto score  = timing(trial);
        score.delay = count(trial, "old_linear") ? 2 : 1;
        score.area  = count(trial, "leaf_chain");
        score.wns = score.tns   = -1;
        score.hold_wns          = 0;
        score.violations        = 0;
        score.electrical_excess = 0;
        return score;
      },
      Repair_scope::design);
  EXPECT_EQ(count(result.placed, "old_linear"), 0) << result.report;
  EXPECT_EQ(count(result.placed, "leaf_chain"), 0) << result.report;
  EXPECT_EQ(count(result.placed, "distribution_fork"), 1) << result.report;
  equivalent(result.placed);
  check_legality(physical::read_lef(options.lefs), result.placed.floorplan);
}
TEST_F(Buffer_repair, RepairsDownstreamForestOfFixedMacroWithoutMovingIt) {
  std::filesystem::create_directories(options.output_dir);
  std::ifstream input(options.lefs.back());
  std::string   lef{std::istreambuf_iterator<char>(input), {}};
  const auto    start = lef.find("MACRO sky130_fd_sc_hd__inv_1\n"), klass = lef.find("CLASS CORE", start);
  ASSERT_NE(start, std::string::npos);
  ASSERT_NE(klass, std::string::npos);
  lef.replace(klass, std::string("CLASS CORE").size(), "CLASS BLOCK");
  options.lefs.back() = options.output_dir + "/macro.lef";
  std::ofstream(options.lefs.back()) << lef;
  auto root = gate("sky130_fd_sc_hd__inv_1", "macro", graph->get_input_pin("a"));
  for (unsigned i = 0; i < 6; ++i) {
    const auto port = i ? "y" + std::to_string(i) : "y";
    if (i) {
      graph->get_io()->add_output(port, i);
      graph->get_io()->set_bits(port, 1);
    }
    graph->get_output_pin(port).connect_driver(gate("sky130_fd_sc_hd__buf_1", "old_buffer" + std::to_string(i), root));
  }
  const auto original = place(*graph, flat, options);
  auto       frame    = std::make_shared<physical::Floorplan>(original.floorplan);
  Ann_place  macro_position;
  for (auto& cell : frame->components) {
    if (cell.name == "macro") {
      cell.place.status = Ann_place::Status::FIXED;
      macro_position    = cell.place;
    }
  }
  options.previous = frame;
  hhds::GraphLibrary fixed_owner;
  auto               fixed_placement = place(*graph, fixed_owner, options);
  const auto         result          = repair_buffers(
      std::move(fixed_placement),
      options,
      [&](const Result& trial) {
        auto       score = timing(trial);
        const bool split = count(trial, "__place_rebuffer") > 0;
        score.delay      = split ? 1 : 2;
        score.area       = split ? 0 : 100;
        score.wns = score.tns   = -1;
        score.hold_wns          = 0;
        score.violations        = 0;
        score.electrical_excess = 0;
        return score;
      },
      Repair_scope::design);
  ASSERT_GT(count(result.placed, "__place_rebuffer"), 0) << result.report;
  EXPECT_EQ(count(result.placed, "old_buffer"), 0);
  for (const auto& cell : result.placed.floorplan.components) {
    if (cell.name == "macro") {
      EXPECT_EQ(cell.place, macro_position);
    }
  }
  equivalent(result.placed);
  check_legality(physical::read_lef(options.lefs), result.placed.floorplan);
  options.density                                     = .5;
  options.terminal_legalize_options.padding_sites     = 0;
  options.terminal_legalize_options.critical_fraction = 0;
  const auto finalized = finalize_placement(result.placed, options, [&](const Result& state) { return timing(state); });
  EXPECT_EQ(count(finalized.placed, "__place_rebuffer"), count(result.placed, "__place_rebuffer"));
  EXPECT_EQ(finalized.placed.floorplan.components.size(), result.placed.floorplan.components.size());
  for (const auto& cell : finalized.placed.floorplan.components) {
    if (cell.name == "macro") {
      EXPECT_EQ(cell.place, macro_position);
    }
  }
  EXPECT_TRUE(std::filesystem::exists(options.output_dir + "/final/terminal_legalize.json"));
  equivalent(finalized.placed);
  check_legality(physical::read_lef(options.lefs), finalized.placed.floorplan);
}
TEST_F(Buffer_repair, BuffersEveryBitOfFixedMacroOutput) {
  std::filesystem::create_directories(options.output_dir);
  options.lefs.push_back(options.output_dir + "/bus_macro.lef");
  std::ofstream lef(options.lefs.back());
  lef << "VERSION 5.8 ;\nMACRO BUS_SOURCE\n CLASS BLOCK ;\n ORIGIN 0 0 ;\n SIZE 10 BY 10 ;\n SYMMETRY X Y ;\n";
  for (unsigned bit = 0; bit < 2; ++bit) {
    for (const auto& pin : {std::string("D"), std::string("Q")}) {
      lef << " PIN " << pin << '[' << bit << "]\n DIRECTION " << (pin == "D" ? "INPUT" : "OUTPUT")
          << " ;\n PORT\n LAYER li1 ;\n RECT " << bit + 1 << " 1 " << bit + 1.1 << " 1.1 ;\n END\n END " << pin << '[' << bit
          << "]\n";
    }
  }
  lef << "END BUS_SOURCE\nEND LIBRARY\n";
  lef.close();
  graph->get_io()->set_bits("a", 2);
  auto a = graph->get_input_pin("a");
  gu::set_bits(a, 2);
  auto io = source.create_io("BUS_SOURCE");
  io->add_input("D", 0);
  io->add_output("Q", 0);
  io->set_bits("D", 2);
  io->set_bits("Q", 2);
  auto macro = gu::create_typed_node(*graph, Ntype_op::Sub);
  macro.set_subnode(io);
  macro.set_name("macro");
  macro.create_sink_pin("D").connect_driver(a);
  auto q = macro.create_driver_pin("Q");
  gu::set_bits(q, 2);
  for (unsigned bit = 0; bit < 2; ++bit) {
    for (unsigned leaf = 0; leaf < 6; ++leaf) {
      const auto port = bit == 0 && leaf == 0 ? std::string("y") : "y" + std::to_string(bit) + "_" + std::to_string(leaf);
      if (port != "y") {
        graph->get_io()->add_output(port, 1 + bit * 6 + leaf);
        graph->get_io()->set_bits(port, 1);
      }
      const auto value = gu::create_get_mask(*graph, q, bit).get_driver_pin(0);
      graph->get_output_pin(port).connect_driver(
          gate("sky130_fd_sc_hd__inv_1", "leaf" + std::to_string(bit) + "_" + std::to_string(leaf), value));
    }
  }
  const auto first = place(*graph, flat, options);
  auto       frame = std::make_shared<physical::Floorplan>(first.floorplan);
  Ann_place  fixed_position;
  for (auto& cell : frame->components) {
    if (cell.name == "macro") {
      cell.place.status = Ann_place::Status::FIXED;
      fixed_position    = cell.place;
    }
  }
  options.previous = frame;
  hhds::GraphLibrary fixed_owner;
  auto               fixed = place(*graph, fixed_owner, options);
  // Macro spacing stays requested; the placed-repair API propagates only the
  // smaller CORE exclusion to its edited-cell legalization.
  options.halo_x = options.halo_y = 2.72;
  options.cell_halo               = std::pair{0., 0.};
  const auto repaired             = repair_buffers(
      std::move(fixed),
      options,
      [&](const Result& trial) {
        Timing_feedback score;
        if (count(trial, "__place_rebuffer")) {
          rapidjson::Document placement_qor;
          placement_qor.Parse(trial.qor.c_str());
          EXPECT_TRUE(placement_qor["cell_halo"]["active"].GetBool());
          EXPECT_EQ(placement_qor["halo"]["x_um"].GetDouble(), 0);
          EXPECT_EQ(placement_qor["macro_halo"]["x_um"].GetDouble(), 2.72);
        }
        score.delay = count(trial, "__place_rebuffer") ? 1 : 2;
        score.area  = count(trial, "__place_rebuffer") ? 0 : 100;
        for (const auto& [name, xy] : trial.pin_locations) {
          (void)xy;
          auto& pin          = score.pins[name];
          pin.capacitance_ff = 1;
          pin.required       = 1;
        }
        return score;
      },
      Repair_scope::design);
  ASSERT_GE(count(repaired.placed, "__place_rebuffer"), 2) << repaired.report;
  bool               through_buffer = false;
  std::set<unsigned> buffered_bits;
  const auto         value = [&](auto&& self, hhds::Pin_class p, unsigned bit, unsigned pattern, unsigned depth) -> bool {
    if (depth > 512 || p.is_invalid()) {
      throw std::runtime_error("broken macro bus repair");
    }
    const auto n = p.get_master_node();
    if (const auto cell = n.get_subnode_io()) {
      if (cell->get_name() == "BUS_SOURCE") {
        if (through_buffer) {
          buffered_bits.insert(bit);
        }
        return (pattern >> bit) & 1;
      }
      through_buffer |= n.get_name().starts_with("__place_rebuffer");
      return self(self, n.get_sink_pin("A").get_driver_pin(), 0, pattern, depth + 1)
             ^ (cell->get_name() == "sky130_fd_sc_hd__inv_1");
    }
    if (gu::type_op_of(n) == Ntype_op::Get_mask) {
      return self(self, gu::get_driver_of_sink_name(n, "a"), bit + gu::bit_range(n)->first, pattern, depth + 1);
    }
    throw std::runtime_error("unexpected macro bus repair operation");
  };
  for (unsigned pattern = 0; pattern < 4; ++pattern) {
    for (const auto& port : repaired.placed.graph->get_io()->get_output_pin_decls()) {
      const auto bit = port.name.starts_with("y1_") ? 1u : 0u;
      through_buffer = false;
      EXPECT_EQ(value(value, repaired.placed.graph->get_output_pin(port.name).get_driver_pin(), 0, pattern, 0),
                !((pattern >> bit) & 1));
    }
  }
  EXPECT_EQ(buffered_bits, (std::set<unsigned>{0, 1}));
  for (const auto& cell : repaired.placed.floorplan.components) {
    if (cell.name == "macro") {
      EXPECT_EQ(cell.place, fixed_position);
    }
  }
  check_legality(physical::read_lef(options.lefs), repaired.placed.floorplan);
}
TEST_F(Buffer_repair, ResizesAcrossEquivalentDriveCellsAndKeepsPortConnectivity) {
  const auto read = [](const std::string& path) {
    std::ifstream input(path);
    return std::string{std::istreambuf_iterator<char>(input), {}};
  };
  std::filesystem::create_directories(options.output_dir);
  const std::string from = "sky130_fd_sc_hd__buf_1", to = "sky130_fd_sc_hd__buf_2";
  auto              lib   = read(options.liberty);
  const auto        start = lib.find("cell (\"" + from + "\")");
  ASSERT_NE(start, std::string::npos);
  auto end   = lib.find('{', start);
  int  depth = 1;
  for (++end; depth && end < lib.size(); ++end) {
    if (lib[end] == '{') {
      ++depth;
    } else if (lib[end] == '}') {
      --depth;
    }
  }
  auto cell = lib.substr(start, end - start);
  cell.replace(cell.find(from), from.size(), to);
  cell = std::regex_replace(cell, std::regex(R"(area\s*:\s*[0-9.]+)"), "area : 7.5072");
  lib.insert(lib.rfind('}'), cell);
  options.liberty = options.output_dir + "/sizing.lib";
  std::ofstream(options.liberty) << lib;
  auto       lef    = read(options.lefs.back());
  const auto begin  = lef.find("MACRO " + from);
  const auto finish = lef.find("END " + from, begin) + 4 + from.size();
  auto       macro  = lef.substr(begin, finish - begin);
  for (size_t pos = macro.find(from); pos != std::string::npos; pos = macro.find(from, pos + to.size())) {
    macro.replace(pos, from.size(), to);
  }
  macro = std::regex_replace(macro, std::regex(R"(SIZE\s+[0-9.]+)"), "SIZE 2.760");
  options.lefs.push_back(options.output_dir + "/sizing.lef");
  std::ofstream(options.lefs.back()) << "VERSION 5.8 ;\n" << macro << "\nEND LIBRARY\n";
  auto b = gate(from, "b", graph->get_input_pin("a"));
  auto p = gate("sky130_fd_sc_hd__inv_1", "invert", b);
  graph->get_output_pin("y").connect_driver(p);
  auto       placed   = place(*graph, flat, options);
  const auto repaired = repair_buffers(std::move(placed), options, [&](const Result& r) {
    Timing_feedback f;
    f.delay = 20;
    f.area  = 1;
    for (const auto& c : r.floorplan.components) {
      if (c.name == "b") {
        f.delay = c.macro == to ? 5 : 10;
        f.area  = c.macro == to ? 2 : 1;
      }
    }
    return f;
  });
  bool       resized  = false;
  for (const auto& c : repaired.placed.floorplan.components) {
    if (c.name == "b") {
      EXPECT_EQ(c.macro, to);
      EXPECT_EQ(c.place.width, 2760);
      resized = true;
    }
  }
  EXPECT_TRUE(resized);
  equivalent(repaired.placed);
}
TEST_F(Buffer_repair, SizesMultiInputLogicAndRejectsSameFootprintWrongFunction) {
  const auto read = [](const std::string& path) {
    std::ifstream input(path);
    return std::string{std::istreambuf_iterator<char>(input), {}};
  };
  std::filesystem::create_directories(options.output_dir);
  const std::string from = "sky130_fd_sc_hd__nand2_1", to = "sky130_fd_sc_hd__nand2_2", wrong = "nand_wrong";
  auto              lib   = read(options.liberty);
  const auto        start = lib.find("cell (\"" + from + "\")");
  ASSERT_NE(start, std::string::npos);
  auto end   = lib.find('{', start);
  int  depth = 1;
  for (++end; depth && end < lib.size(); ++end) {
    if (lib[end] == '{') {
      ++depth;
    } else if (lib[end] == '}') {
      --depth;
    }
  }
  const auto original = lib.substr(start, end - start);
  auto       cell     = original;
  cell.replace(cell.find(from), from.size(), to);
  lib.insert(lib.rfind('}'), cell);
  cell = original;
  cell.replace(cell.find(from), from.size(), wrong);
  cell = std::regex_replace(cell, std::regex(R"(function\s*:\s*"[^"]*")"), "function : \"A & B\"");
  lib.insert(lib.rfind('}'), cell);
  options.liberty = options.output_dir + "/logic.lib";
  std::ofstream(options.liberty) << lib;
  const auto lef    = read(options.lefs.back());
  const auto begin  = lef.find("MACRO " + from);
  const auto finish = lef.find("END " + from, begin) + 4 + from.size();
  options.lefs.push_back(options.output_dir + "/logic.lef");
  std::ofstream extra(options.lefs.back());
  extra << "VERSION 5.8 ;\n";
  for (const auto& master : {to, wrong}) {
    auto macro = lef.substr(begin, finish - begin);
    for (size_t pos = macro.find(from); pos != std::string::npos; pos = macro.find(from, pos + master.size())) {
      macro.replace(pos, from.size(), master);
    }
    extra << macro << '\n';
  }
  extra << "END LIBRARY\n";
  extra.close();
  graph->get_io()->add_input("b", 1);
  graph->get_io()->set_bits("b", 1);
  gu::set_bits(graph->get_input_pin("b"), 1);
  auto io = source.create_io(from);
  io->add_input("A", 7);
  io->add_input("B", 3);
  io->add_output("Y", 9);
  for (const auto& name : {"A", "B", "Y"}) {
    io->set_bits(name, 1);
  }
  auto n = gu::create_typed_node(*graph, Ntype_op::Sub);
  n.set_subnode(io);
  n.set_name("logic");
  n.create_sink_pin("A").connect_driver(graph->get_input_pin("a"));
  n.create_sink_pin("B").connect_driver(graph->get_input_pin("b"));
  auto out = n.create_driver_pin("Y");
  gu::set_bits(out, 1);
  graph->get_output_pin("y").connect_driver(out);
  auto       placed        = place(*graph, flat, options);
  bool       invalid_trial = false;
  const auto repaired      = repair_buffers(std::move(placed), options, [&](const Result& r) {
    Timing_feedback f;
    f.delay = 10;
    f.area  = 1;
    for (const auto& c : r.floorplan.components) {
      invalid_trial |= c.macro == wrong;
      if (c.macro == to) {
        f.delay = 5;
      }
    }
    return f;
  });
  EXPECT_FALSE(invalid_trial);
  auto driver  = repaired.placed.graph->get_output_pin("y").get_driver_pin();
  auto resized = driver.get_master_node();
  ASSERT_EQ(resized.get_subnode_io()->get_name(), to);
  EXPECT_EQ(resized.get_sink_pin("A").get_driver_pin().get_pin_name(), "a");
  EXPECT_EQ(resized.get_sink_pin("B").get_driver_pin().get_pin_name(), "b");
  const auto cells = liberty::scan_comb_cells(options.liberty);
  for (const auto& c : cells) {
    if (c.name == to) {
      const auto truth = liberty::cell_truth(c);
      ASSERT_TRUE(truth);
      for (unsigned bits = 0; bits < 4; ++bits) {
        EXPECT_EQ((*truth >> bits) & 1, bits != 3);
      }
    }
  }
}
TEST_F(Buffer_repair, ProvedConeResynthesisPreservesSideOutputsAndImprovesRealTiming) {
  // y = ~a through five inverters while side = i0 = ~a: a proved mapping can
  // drop the redundant stages even with the IO pins kept where they are.
  auto first = gate("sky130_fd_sc_hd__inv_1", "i0", graph->get_input_pin("a"));
  auto p     = first;
  for (unsigned i = 1; i < 5; ++i) {
    p = gate("sky130_fd_sc_hd__inv_1", "i" + std::to_string(i), p);
  }
  graph->get_output_pin("y").connect_driver(p);
  graph->get_io()->add_output("side", 1);
  graph->get_io()->set_bits("side", 1);
  graph->get_output_pin("side").connect_driver(first);
  auto       placed = place(*graph, flat, options);
  const auto before = timing(placed);
  ASSERT_TRUE(synth::has_cone_provider("abc"));
  bool       side_cut = false;
  const auto result   = resynthesize_cones(
      std::move(placed),
      options,
      [&](const Result& r) { return timing(r); },
      [&](const synth::Lnet& logic, const synth::Cone_boundary& boundary, const std::string& lib) {
        side_cut |= logic.outputs().size() == 2;
        EXPECT_EQ(logic.inputs().size(), boundary.inputs.size());
        for (const auto& in : boundary.inputs) {
          EXPECT_GE(in.arrival_ps, 0);
          EXPECT_GE(in.slew_ps, 0);
        }
        for (const auto& out : boundary.outputs) {
          EXPECT_GE(out.load_ff, 0);
          EXPECT_GT(out.required_ps, 0);
        }
        return synth::map_cone("abc", logic, boundary, lib);
      });
  SCOPED_TRACE(result.report);
  EXPECT_TRUE(side_cut);
  EXPECT_LT(timing(result.placed).delay, before.delay);
  EXPECT_GT(count(result.placed, "__place_resynth"), 0);
  equivalent(result.placed);
  rapidjson::Document report;
  report.Parse(result.report.c_str());
  EXPECT_GT(report["accepted"].GetUint(), 0);
  bool proved = false;
  for (const auto& trial : report["trials"].GetArray()) {
    if (trial.HasMember("proof")) {
      proved = true;
      EXPECT_EQ(trial["assignments"].GetUint(), 2);
    }
  }
  EXPECT_TRUE(proved);
}
TEST_F(Buffer_repair, ConeProofRejectsIncorrectSideOutputBeforePlacementOrTiming) {
  auto first = gate("sky130_fd_sc_hd__inv_1", "i0", graph->get_input_pin("a"));
  auto p     = gate("sky130_fd_sc_hd__inv_1", "i1", first);
  p          = gate("sky130_fd_sc_hd__inv_1", "i2", p);
  graph->get_output_pin("y").connect_driver(p);
  graph->get_io()->add_output("side", 1);
  graph->get_io()->set_bits("side", 1);
  graph->get_output_pin("side").connect_driver(first);
  auto       placed      = place(*graph, flat, options);
  const auto identity    = placed.parasitics->identity;
  unsigned   evaluations = 0;
  const auto result      = resynthesize_cones(
      std::move(placed),
      options,
      [&](const Result& r) {
        ++evaluations;
        return timing(r);
      },
      [&](const synth::Lnet& logic, const synth::Cone_boundary& boundary, const std::string& lib) {
        auto mapping = synth::map_cone("abc", logic, boundary, lib);
        if (mapping) {
          mapping->netlist.outputs.front() = mapping->netlist.sources.front();
        }
        return mapping;
      });
  SCOPED_TRACE(result.report);
  EXPECT_EQ(evaluations, 1);
  EXPECT_EQ(result.placed.parasitics->identity, identity);
  EXPECT_TRUE(result.libraries.empty());
  EXPECT_NE(result.report.find("REFUTED"), std::string::npos);
  equivalent(result.placed);
}
TEST_F(Buffer_repair, ProvedConeTimingRegressionRestoresOriginalPlacement) {
  auto p = graph->get_input_pin("a");
  for (unsigned i = 0; i < 3; ++i) {
    p = gate("sky130_fd_sc_hd__inv_1", "i" + std::to_string(i), p);
  }
  graph->get_output_pin("y").connect_driver(p);
  auto       placed      = place(*graph, flat, options);
  const auto identity    = placed.parasitics->identity;
  const auto components  = placed.floorplan.components;
  unsigned   evaluations = 0;
  const auto result      = resynthesize_cones(
      std::move(placed),
      options,
      [&](const Result& r) {
        auto f  = timing(r);
        f.delay = evaluations++ ? 100 : 1;
        return f;
      },
      [](const auto& logic, const auto& boundary, const auto& lib) { return synth::map_cone("abc", logic, boundary, lib); });
  SCOPED_TRACE(result.report);
  EXPECT_GT(evaluations, 1);
  EXPECT_EQ(result.placed.parasitics->identity, identity);
  EXPECT_EQ(result.placed.floorplan.components, components);
  EXPECT_TRUE(result.libraries.empty());
  equivalent(result.placed);
}
TEST_F(Buffer_repair, RejectsAllDegradationsWithoutChangingThePlacedGraph) {
  auto b = gate("sky130_fd_sc_hd__buf_1", "b", graph->get_input_pin("a"));
  auto p = gate("sky130_fd_sc_hd__inv_1", "invert", b);
  graph->get_output_pin("y").connect_driver(p);
  auto       placed     = place(*graph, flat, options);
  const auto identity   = placed.parasitics->identity;
  const auto components = placed.floorplan.components;
  const auto repaired   = repair_buffers(std::move(placed), options, [&](const Result& r) {
    Timing_feedback f;
    f.delay = count(r, "b") ? 1 : 2;
    f.area  = r.floorplan.components.size();
    return f;
  });
  EXPECT_EQ(repaired.placed.parasitics->identity, identity);
  EXPECT_EQ(repaired.placed.floorplan.components, components);
  EXPECT_TRUE(repaired.libraries.empty());
  equivalent(repaired.placed);
}
}  // namespace
}  // namespace livehd::placement
