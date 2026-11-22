// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "core/file_utils.hpp"
#include "gtest/gtest.h"
#include "inou/lefdef/mapped_graph.hpp"
#include "node_util.hpp"
#include "pass/timing/physical_rank.hpp"
#include "placement_technology.hpp"
#include "rapidjson/document.h"
#include "repair.hpp"
#include "technology_rc.hpp"

namespace livehd::placement {
namespace {
class ClockGateMerge : public ::testing::Test {
protected:
  Options opts;
  void    SetUp() override {
    const auto        root   = std::filesystem::path(std::getenv("TEST_TMPDIR"));
    const std::string data   = "inou/lefdef/tests/data/sky130hd";
    const auto        splice = [&](const std::string& extension, const std::string& trailer) {
      auto base = file_utils::read_file(data + extension).value();
      base.insert(base.rfind(trailer), file_utils::read_file(data + "_seq" + extension + ".fragment").value());
      const auto path = root / ("cells" + extension);
      std::ofstream(path) << base;
      return path.string();
    };
    opts.output_dir   = root.string();
    opts.liberty      = splice(".lib", "}");
    opts.lefs         = {data + ".tlef", splice(".lef", "END LIBRARY")};
    opts.tech_rc_file = (root / "wire.tcl").string();
    std::ofstream(opts.tech_rc_file) << "set_wire_rc -signal -layer met2\nset_wire_rc -clock -layer met3\n";
  }
  struct Run {
    Result                placed;
    std::set<std::string> buffers;
    rapidjson::Document   report;
  };
  Run build(physical::Floorplan fp, double period, unsigned trials, double fraction = .05, const std::string& extra_sdc = {},
            const std::map<std::string, std::string>& colors = {}, const std::string& history = {}) {
    const auto& tech = opts.technology();
    Run         out;
    auto&       result = out.placed;
    result.fp_library  = std::make_shared<hhds::GraphLibrary>();
    result.graph       = physical::mapped_graph(*result.fp_library, fp, tech.lef);
    if (!colors.empty()) {
      for (auto node : result.graph->body().nodes()) {
        if (auto it = colors.find(std::string(node.get_name())); it != colors.end()) {
          graph_util::Color_group::set(node, it->second);
        }
      }
    }
    result.prects = prects_library(tech.cells);
    result.prects.clock_ports.insert("io_aclk");
    for (const auto& t : fp.terminals) {
      result.pin_locations[t.name] = {t.x, t.y};
    }
    for (const auto& c : fp.components) {
      const auto& m = tech.lef.macros.at(c.macro);
      for (const auto& [name, p] : m.pins) {
        if (p.use == "POWER" || p.use == "GROUND") {
          continue;
        }
        const auto [x, y] = placement_pin_offset(m, name, c.place.orientation, fp.dbu);
        result.pin_locations[physical::connection_label({c.name, name})] = {c.place.x + x, c.place.y + y};
      }
    }
    std::string sdc = std::string("create_clock -name io_aclk -period ") + std::to_string(period)
        + " [get_ports io_aclk]\nset_input_delay -clock io_aclk 2 [get_ports {en d0 d1 d2 d3 d4 d5 d6 d7}]\n"
          "set_output_delay -clock io_aclk 2 [all_outputs]\n";
    sdc += extra_sdc;
    timing::fast::Physical_ranker::Options timing;
    timing.liberty_texts            = {tech.cells.contents};
    timing.sdc_text                 = sdc;
    opts.clock_gate_merge_trials    = trials;
    opts.clock_skew_period_fraction = fraction;
    result.floorplan                = std::move(fp);
    auto tree   = build_clock_handoff(result, opts, {}, sdc, std::make_unique<timing::fast::Physical_ranker>(std::move(timing)),
                                      history);
    out.buffers = std::move(tree.buffers);
    size_t clock_nets = 0;
    for (const auto& net : result.floorplan.nets) {
      const auto drivers
          = std::ranges::count_if(net.connections, [&](const auto& pin) { return tree.drivers.contains({pin.instance, pin.pin}); });
      EXPECT_EQ(drivers, net.use == "CLOCK" ? 1 : 0) << net.name.str();
      clock_nets += net.use == "CLOCK";
    }
    EXPECT_EQ(clock_nets, tree.drivers.size());
    out.report.Parse(file_utils::read_file((std::filesystem::path(opts.output_dir) / "pcrs_clock.json").string()).value().c_str());
    return out;
  }
  static physical::Floorplan pair() { return physical::read_def("pass/place/tests/clock_gate_pair.def"); }
  // Constrain a launch output and an independent hold input. The geographically
  // local gate copy has a measurable benefit on this pair with distributed RC.
  static constexpr auto      copy_sdc
      = "set_output_delay -max -clock io_aclk 5 [get_ports q0]\n"
        "set_input_delay -min -clock io_aclk 1.5 [get_ports d3]\n";
  static physical::Floorplan copy_fixture() {
    auto fp = pair();
    for (int i = 8; i < 32; ++i) {
      const auto name = "r" + std::to_string(i);
      auto       flop = fp.components.at(2 + i % 4);
      flop.name       = name;
      flop.place.x    = (11 + (i % 4) * 18) * 460;
      flop.place.y    = (4 + (i - 8) / 4) * 2720;
      fp.components.push_back(flop);
      std::ranges::find(fp.nets, "g0", &physical::Net::name)->connections.push_back({name, "CLK"});
      std::ranges::find(fp.nets, "d" + std::to_string(i % 8), &physical::Net::name)->connections.push_back({name, "D"});
      auto port = fp.terminals.back();
      port.name = port.net = "q" + std::to_string(i);
      fp.terminals.push_back(port);
      fp.nets.push_back({
          port.net,
          {{"PIN", port.name}, {name, "Q"}}
      });
    }
    return fp;
  }
};
TEST_F(ClockGateMerge, MeasuresLeafStarAfterItsMembersAreLegalized) {
  const auto path = std::filesystem::path(opts.output_dir) / "pcrs_clock.json";
  std::ofstream(path) << R"({"networks":[{"branches":[{"leaf":true,"children":["a:CK","b:CK","c:CK"]},)"
                         R"({"leaf":false,"children":["clock_buffer"]}],"gate_leaf_sinks":["a:CK"]}]})";
  Result placed;
  placed.floorplan.dbu = 1000;
  placed.pin_locations = {{"a:CK", {0, 0}}, {"b:CK", {4000, 0}}, {"c:CK", {2000, 2000}}};
  record_final_clock_leaf_geometry(placed, opts.output_dir);
  placed.pin_locations.at("c:CK").second = 6000;
  record_final_clock_leaf_geometry(placed, opts.output_dir);
  rapidjson::Document report;
  report.Parse(file_utils::read_file(path.string()).value().c_str());
  const auto& network = report["networks"][0];
  const auto& leaf    = network["branches"][0]["final_placement"];
  EXPECT_DOUBLE_EQ(leaf["center_x_um"].GetDouble(), 2);
  EXPECT_DOUBLE_EQ(leaf["center_y_um"].GetDouble(), 3);
  EXPECT_DOUBLE_EQ(leaf["star_wire_um"].GetDouble(), 13);
  EXPECT_DOUBLE_EQ(leaf["max_member_distance_um"].GetDouble(), 5);
  EXPECT_FALSE(network["branches"][1].HasMember("final_placement"));
  EXPECT_DOUBLE_EQ(network["final_placement"]["star_wire_um"].GetDouble(), 0);
}

TEST_F(ClockGateMerge, ObservedEnableDutySelectsTheLowerCostLegalGate) {
  opts.clock_activity = {
      {"g0", .5},
      {"g1", .5}
  };
  const auto  run    = build(pair(), 10, 2);
  const auto& trials = run.report["gate_merge_trials"];
  ASSERT_EQ(trials.Size(), 2u);
  const auto& best = trials[0];
  EXPECT_STREQ(best["decision"].GetString(), "accepted");
  EXPECT_GT(best["normalized_cost_gain"].GetDouble(), trials[1]["normalized_cost_gain"].GetDouble());
  EXPECT_GT(best["saved_activity_weighted_cap_ff"].GetDouble(), 0.);
  EXPECT_GE(best["minimum_gating_slack_ps"].GetDouble(), 0.);
}

TEST_F(ClockGateMerge, SkipsBalanceWhenTheBaselineCannotScoreATrial) {
  opts.clock_balance = true;
  auto run = build(pair(), 10, 0, .05, "set_false_path -rise_from [get_ports en]\n");
  const auto& search = run.report["useful_skew"];
  EXPECT_STREQ(search["balance_decision"].GetString(), "unsupported_constraints");
  EXPECT_EQ(search["timed_plans"].GetUint(), 1u);
  EXPECT_FALSE(search.HasMember("balanced_setup_ps"));
  ASSERT_TRUE(search.HasMember("unsupported_checks"));
  EXPECT_TRUE(std::ranges::any_of(search["unsupported_checks"].GetArray(), [](const auto& reason) {
    return std::string_view(reason.GetString()).find("-rise_from") != std::string_view::npos;
  }));
}

TEST_F(ClockGateMerge, BalancesThroughABufferAndADiscreteGate) {
  auto fp = pair();
  auto net = [&](Name name) -> auto& { return *std::ranges::find(fp.nets, name, &physical::Net::name); };
  auto& gate = *std::ranges::find(fp.components, Name("a"), &physical::Component::name);
  gate.macro = "sky130_fd_sc_hd__nand2_1";
  fp.components.push_back({"feeder", "sky130_fd_sc_hd__buf_1", fp.components[1].place});
  fp.components.push_back({"phase", "sky130_fd_sc_hd__inv_1", fp.components[2].place});
  // r7 samples the enable on the opposite edge. r6 stays on the ungated
  // clock, alongside the input of the buffer serving the gate.
  std::erase(net("en").connections, physical::Connection{"a", "GATE"});
  net("q7").connections.push_back({"a", "B"});
  std::erase(net("g1").connections, physical::Connection{"r7", "CLK"});
  std::erase(net("g1").connections, physical::Connection{"r6", "CLK"});
  net("g0").connections.front() = {"a", "Y"};
  std::erase(net("io_aclk").connections, physical::Connection{"a", "CLK"});
  net("io_aclk").connections.push_back({"feeder", "A"});
  net("io_aclk").connections.push_back({"r6", "CLK"});
  fp.nets.push_back({"before_gate", {{"feeder", "X"}, {"a", "A"}, {"phase", "A"}}});
  fp.nets.push_back({"opposite", {{"phase", "Y"}, {"r7", "CLK"}}});

  auto run = build(std::move(fp), 10, 0);
  ASSERT_EQ(run.report["discrete_clock_gates"].Size(), 1u);
  opts.clock_balance = true;
  opts.clock_arrival_targets["r0:CLK"] = {1000, 0};
  build_clock_handoff(run.placed, opts, {}, "create_clock -period 10 [get_ports io_aclk]", nullptr);
  rapidjson::Document report;
  report.Parse(file_utils::read_file((std::filesystem::path(opts.output_dir) / "pcrs_clock.json").string()).value().c_str());
  std::map<std::string, double> arrivals;
  for (const auto& network : report["networks"].GetArray()) {
    if (network["source_net"] == "before_gate") {
      EXPECT_TRUE(network["minimize_source_latency"].GetBool());
    }
    if (network["source_net"] != "io_aclk") {
      continue;
    }
    for (const auto& leaf : network["branches"].GetArray()) {
      if (leaf["leaf"].GetBool()) {
        for (rapidjson::SizeType i = 0; i < leaf["children"].Size(); ++i) {
          arrivals.emplace(leaf["children"][i].GetString(), leaf["estimated_sink_arrivals_ps"][i].GetDouble());
        }
      }
    }
  }
  EXPECT_GT(arrivals.at("r6:CLK") - arrivals.at("feeder:A"), 500);
}

TEST_F(ClockGateMerge, RebuildPreservesUnchangedIdentitiesAndReusesAnOverlappingLeaf) {
  auto       run         = build(pair(), 10, 0);
  auto&      placed      = run.placed;
  const auto old_buffers = run.buffers;
  const auto before      = placed.floorplan;
  const auto rebuild
      = [&] { return build_clock_handoff(placed, opts, {}, "create_clock -period 10 [get_ports io_aclk]", nullptr); };
  const auto baseline = rebuild();
  EXPECT_EQ(baseline.buffers, old_buffers);
  size_t published = 0;
  EXPECT_TRUE(graph_util::visit_published_clock_drivers(*placed.graph, [&](const hhds::Pin_class&) { ++published; }));
  EXPECT_EQ(published, baseline.drivers.size());
  for (const auto& [instance, port] : baseline.drivers) {
    EXPECT_TRUE(placed.prects.published_clock_drivers.contains({instance, port}));
  }
  EXPECT_EQ(placed.floorplan.components, before.components);
  for (const auto& cell : placed.floorplan.components) {
    if (cell.name == "a" || cell.name == "b") {
      EXPECT_FALSE(baseline.buffers.contains(cell.name.str()));
    }
  }
  // Transfer one real flop from a's previous leaf to b's previous leaf.
  // Both old leaves still have a majority of their original members.
  physical::Net *source = nullptr, *target = nullptr;
  for (auto& net : placed.floorplan.nets) {
    for (const auto& pin : net.connections) {
      if (pin == physical::Connection{"r0", "CLK"}) {
        source = &net;
      }
      if (pin == physical::Connection{"r4", "CLK"}) {
        target = &net;
      }
    }
  }
  ASSERT_NE(source, nullptr);
  ASSERT_NE(target, nullptr);
  ASSERT_NE(source, target);
  std::erase(source->connections, physical::Connection{"r0", "CLK"});
  target->connections.push_back({"r0", "CLK"});
  hhds::Pin_class r0, r4;
  for (auto node : placed.graph->body().nodes()) {
    if (node.get_name() == "r0") {
      r0 = node.get_sink_pin("CLK");
    }
    if (node.get_name() == "r4") {
      r4 = node.get_sink_pin("CLK");
    }
  }
  ASSERT_FALSE(r0.is_invalid());
  ASSERT_FALSE(r4.is_invalid());
  graph_util::drop_drivers(r0);
  r0.connect_driver(r4.get_driver_pin());
  EXPECT_EQ(rebuild().buffers, old_buffers);
  size_t flops = 0;
  for (const auto& net : placed.floorplan.nets) {
    for (const auto& pin : net.connections) {
      if (pin.pin == "CLK" && pin.instance.view().starts_with("r")) {
        ++flops;
      }
    }
  }
  EXPECT_EQ(flops, 8u);
}

TEST_F(ClockGateMerge, SourceEditMatchesPreviousBranchesByDriverAndSurvivingPins) {
  const auto before = build(pair(), 10, 0);
  const auto history = std::filesystem::path(opts.output_dir) / "previous.json";
  std::filesystem::copy_file(std::filesystem::path(opts.output_dir) / "pcrs_clock.json", history);
  auto edited = pair();
  auto& a = *std::ranges::find(edited.nets, "g0", &physical::Net::name);
  auto& b = *std::ranges::find(edited.nets, "g1", &physical::Net::name);
  std::swap(a.name, b.name);
  std::erase(a.connections, physical::Connection{"r0", "CLK"});
  b.connections.push_back({"r0", "CLK"});
  EXPECT_NE(build(edited, 10, 0).buffers, before.buffers);
  const auto after = build(edited, 10, 0, .05, {}, {}, history.string());
  EXPECT_EQ(after.buffers, before.buffers);
  std::map<std::string, std::string> roots;
  for (const auto& network : before.report["networks"].GetArray()) {
    roots[network["source_instance"].GetString()] = network["root_buffer"].GetString();
  }
  for (const auto& network : after.report["networks"].GetArray()) {
    const std::string source = network["source_instance"].GetString();
    EXPECT_EQ(roots.at(source), network["root_buffer"].GetString());
    if (source == "a" || source == "b") {
      EXPECT_EQ(network["sinks"].GetUint64(), source == "a" ? 3u : 5u);
    }
  }
}

TEST_F(ClockGateMerge, ReducesJointDelayWhenACharacterizedStageWouldFailHold) {
  opts.clock_skew_rounds = 1;
  const auto run = build(pair(), 5, 0, .4,
                         "set_input_delay -max -clock io_aclk 7 [get_ports d0]\n"
                         "set_input_delay -max -clock io_aclk 5.4 [get_ports d4]\n"
                         "set_input_delay -min -clock io_aclk 0.45 [get_ports d4]\n");
  const auto& search = run.report["useful_skew"];
  const auto& trials = search["trials"];
  ASSERT_EQ(trials.Size(), 2u);
  const auto& full = trials[0];
  const auto& half = trials[1];
  ASSERT_TRUE(full["joint_leaf_schedule"].GetBool());
  ASSERT_TRUE(half["joint_leaf_schedule"].GetBool());
  EXPECT_STREQ(full["decision"].GetString(), "endpoint_hold_margin");
  ASSERT_TRUE(full.HasMember("limiting_pin"));
  EXPECT_STREQ(full["limiting_pin"]["pin"].GetString(), "r4:D");
  EXPECT_STREQ(half["decision"].GetString(), "accepted");
  for (const auto* leaf : {"r0:CLK", "r4:CLK"}) {
    EXPECT_DOUBLE_EQ(half["delay_ps"][leaf].GetDouble(), full["delay_ps"][leaf].GetDouble() / 2);
  }
  EXPECT_GT(search["selected"]["worst_setup_ps"].GetDouble(), search["baseline"]["worst_setup_ps"].GetDouble());
  EXPECT_DOUBLE_EQ(search["selected"]["worst_hold_ps"].GetDouble(), search["baseline"]["worst_hold_ps"].GetDouble());
}

TEST_F(ClockGateMerge, StuckPortPathDoesNotBlockImprovingAnotherClockLeaf) {
  // The compact platform fixture contains one buffer. Add a second characterized
  // size so the competing launch has a physically realizable timing move.
  auto lib = file_utils::read_file(opts.liberty).value();
  lib.insert(lib.rfind('}'), R"lib(cell(clock_fast) { area : 5.0;
    pin(A) { direction : input; capacitance : 0.002103; }
    pin(X) { direction : output; function : "A"; max_capacitance : 0.13;
      timing() { related_pin : "A"; timing_sense : positive_unate;
        cell_rise(scalar) { values("0.005"); } cell_fall(scalar) { values("0.005"); }
        rise_transition(scalar) { values("0.005"); } fall_transition(scalar) { values("0.005"); }
      }
    }
  })lib");
  auto              lef    = file_utils::read_file(opts.lefs.at(1)).value();
  const std::string master = "sky130_fd_sc_hd__buf_1";
  const auto        start  = lef.find("MACRO " + master);
  ASSERT_NE(start, std::string::npos);
  auto extra = lef.substr(start, lef.find("END " + master, start) + 4 + master.size() - start);
  for (size_t i = 0; (i = extra.find(master, i)) != std::string::npos;) {
    extra.replace(i, master.size(), "clock_fast");
  }
  lef.insert(lef.rfind("END LIBRARY"), extra + "\n");
  std::ofstream(opts.liberty) << lib;
  std::ofstream(opts.lefs.at(1)) << lef;
  auto fp      = pair();
  auto blocked = fp.terminals.back();
  blocked.name = "fixed_out";
  blocked.net  = "d0";
  fp.terminals.push_back(blocked);
  std::ranges::find(fp.nets, "d0", &physical::Net::name)->connections.push_back({"PIN", "fixed_out"});
  opts.clock_skew_rounds = 2;
  const auto  run        = build(std::move(fp),
                         5,
                         0,
                         .45,
                         "set_output_delay -max -clock io_aclk 50 [get_ports fixed_out]\n"
                                 "set_output_delay -max -clock io_aclk 8 [get_ports q4]\n");
  const auto& search     = run.report["useful_skew"];
  ASSERT_FALSE(search["trials"].Empty());
  bool tried_second = false;
  for (const auto& trial : search["trials"].GetArray()) {
    tried_second |= std::string_view(trial["endpoint_pin"].GetString()) == "q4";
  }
  EXPECT_TRUE(tried_second);
  EXPECT_GT(search["accepted_rounds"].GetUint(), 0u);
  EXPECT_NEAR(search["selected"]["worst_setup_ps"].GetDouble(), search["baseline"]["worst_setup_ps"].GetDouble(), 0.01);
  EXPECT_GT(search["selected"]["setup_tns_ps"].GetDouble(), search["baseline"]["setup_tns_ps"].GetDouble());

  const auto bounded = build(pair(), 5, 0, 1e-6, "set_output_delay -max -clock io_aclk 8 [get_ports q4]\n");
  bool saw_bound = false;
  for (const auto& trial : bounded.report["useful_skew"]["trials"].GetArray()) {
    const std::string_view decision = trial["decision"].GetString();
    if (decision == "arrival_shift" || decision == "endpoint_hold_margin") {
      ASSERT_TRUE(trial.HasMember("limiting_pin"));
      const auto& limit = trial["limiting_pin"];
      EXPECT_GT(limit["pin"].GetStringLength(), 0u);
      EXPECT_GT(limit["excess_ps"].GetDouble(), 0);
      saw_bound = true;
    } else {
      EXPECT_FALSE(trial.HasMember("limiting_pin"));
    }
  }
  EXPECT_TRUE(saw_bound);
}

TEST_F(ClockGateMerge, GatedClockEntersRemoteHomeAtItsBufferAndLocalHomeAtItsSource) {
  opts.accepted_chunk_plan = std::make_shared<const std::string>(
      R"({"chunks":[{"id":"lower","box_dbu":[0,0,138000,7600],"members":[]},{"id":"upper","box_dbu":[0,7600,138000,54400],"members":[{"id":"moved","macro":false}]}]})");
  const auto run     = build(pair(), 10, 0);
  unsigned   checked = 0;
  for (const auto& net : run.report["networks"].GetArray()) {
    const auto name = std::string_view(net["source_net"].GetString());
    if (name != "g0" && name != "g1") {
      continue;
    }
    const auto& entries = net["chunk_entries"];
    ASSERT_EQ(entries.Size(), 1u);
    const auto& entry = entries[0];
    if (name == "g0") {
      EXPECT_STREQ(entry["chunk"].GetString(), "lower");
      EXPECT_STREQ(entry["instance"].GetString(), "a");
      EXPECT_STREQ(entry["output_pin"].GetString(), "GCLK");
    } else {
      EXPECT_STREQ(entry["chunk"].GetString(), "upper");
      EXPECT_TRUE(entry.HasMember("input_pin"));
      EXPECT_STREQ(net["source_chunk"].GetString(), "lower");
    }
    ++checked;
  }
  EXPECT_EQ(checked, 2u);
  // The accepted color assignment overrides the original lower-home position.
  const auto  edited = build(pair(),
                            10,
                            0,
                            .05,
                             {
  },
                             {{"r0", "moved"}});
  const auto& nets   = edited.report["networks"].GetArray();
  const auto  g0
      = std::ranges::find_if(nets, [](const auto& net) { return std::string_view(net["source_net"].GetString()) == "g0"; });
  ASSERT_NE(g0, nets.end());
  EXPECT_EQ((*g0)["chunk_entries"].Size(), 2u);
  for (const auto& b : (*g0)["branches"].GetArray()) {
    if (b["leaf"].GetBool() && std::string_view(b["chunk"].GetString()) == "upper") {
      ASSERT_EQ(b["children"].Size(), 1u);
      EXPECT_STREQ(b["children"][0].GetString(), "r0:CLK");
    }
  }
}

TEST_F(ClockGateMerge, RebuildsTheLiveGraphOnlyWhenTheBufferedLayoutMeetsTiming) {
  physical::Floorplan control;
  for (unsigned run = 0; run < 3; ++run) {
    const bool  loose   = run == 2;
    auto        out     = build(pair(), loose ? 10 : 2.78, run == 0 ? 0 : 2);
    auto&       result  = out.placed;
    auto&       fp      = result.floorplan;
    const auto& json    = out.report;
    const auto& buffers = out.buffers;
    ASSERT_FALSE(json.HasParseError());
    const auto trials = json["gate_merge_trials"].GetArray();
    if (run == 0) {
      EXPECT_TRUE(trials.Empty());
      EXPECT_EQ(buffers.size(), 3u);
      control = std::move(fp);
      continue;
    }
    ASSERT_EQ(trials.Size(), 2u);
    EXPECT_GT(json["before_gate_merge"]["setup_ps"].GetDouble(), 0);
    if (!loose) {
      EXPECT_EQ(fp.nets, control.nets);
      EXPECT_EQ(fp.components, control.components);
      for (const auto& t : trials) {
        EXPECT_STREQ(t["decision"].GetString(), "setup_regression");
        EXPECT_LT(t["setup_ps"].GetDouble(), 0);
      }
      continue;
    }
    ASSERT_STREQ(trials[1]["decision"].GetString(), "accepted");
    EXPECT_STREQ(trials[1]["keep"].GetString(), "b");
    EXPECT_GT(trials[1]["saved_area_um2"].GetDouble(), 0);
    EXPECT_EQ(buffers.size(), 2u);
    std::map<std::string, hhds::Node_class> nodes;
    for (auto node : result.graph->body().nodes()) {
      nodes.emplace(std::string(node.get_name()), node);
    }
    ASSERT_FALSE(nodes.contains("a"));
    for (const auto& n : fp.nets) {
      for (const auto& c : n.connections) {
        EXPECT_NE(c.instance.str(), "a");
      }
    }
    for (int i = 0; i < 8; ++i) {
      const auto clk  = nodes.at("r" + std::to_string(i)).get_sink_pin("CLK").get_driver_pin();
      auto       leaf = clk.get_master_node();
      ASSERT_TRUE(buffers.contains(std::string(leaf.get_name())));
      const auto inputs = leaf.get_subnode_io()->get_input_pin_decls();
      ASSERT_EQ(inputs.size(), 1u);
      EXPECT_EQ(leaf.get_sink_pin(inputs.front().name).get_driver_pin().get_master_node().get_name(), "b");
    }
  }
}
TEST_F(ClockGateMerge, CumulativeMergesRetireEarlierSurvivorsAndKeepTheOriginalArrivalBound) {
  auto       fp      = pair();
  // Split each gated domain in two. At the same height the four gates are
  // equally spaced, making the order of the three neighbouring pairs stable.
  const auto spacing = fp.components[1].place.x - fp.components[0].place.x;
  for (int i = 0; i < 2; ++i) {
    auto gate     = fp.components[i];
    gate.name     = i == 0 ? "c" : "d";
    gate.place.x += 2 * spacing;
    fp.components.push_back(gate);
    for (auto& net : fp.nets) {
      if (net.name == "io_aclk") {
        net.connections.push_back({gate.name, "CLK"});
      }
      if (net.name == "en") {
        net.connections.push_back({gate.name, "GATE"});
      }
    }
    auto old = std::ranges::find(fp.nets, i == 0 ? "g0" : "g1", &physical::Net::name);
    ASSERT_NE(old, fp.nets.end());
    physical::Net child{i == 0 ? "g2" : "g3", {{gate.name, "GCLK"}}};
    child.connections.insert(child.connections.end(), old->connections.end() - 2, old->connections.end());
    old->connections.resize(old->connections.size() - 2);
    fp.nets.push_back(std::move(child));
  }
  for (const auto& [fraction, budget, expected] : std::vector<std::tuple<double, unsigned, std::set<std::string>>>{
           { .05, 6,      {"c"}},
           {.008, 6, {"a", "c"}},
           { .05, 3, {"a", "d"}}
  }) {
    auto        out    = build(fp, 10, budget, fraction);
    const auto& trials = out.report["gate_merge_trials"];
    ASSERT_EQ(trials.Size(), budget);
    std::set<std::string> retired;
    double                saved   = 0;
    unsigned              offsets = 0;
    for (const auto& trial : trials.GetArray()) {
      const std::string_view decision = trial["decision"].GetString();
      if (decision == "accepted") {
        retired.insert(trial["drop"].GetString());
        saved += trial["saved_area_um2"].GetDouble();
      }
      if (decision == "clock_offset") {
        ++offsets;
      }
    }
    EXPECT_EQ(retired.size(), 4 - expected.size());
    EXPECT_NEAR(saved, 21.2704 * retired.size(), 1e-4);
    EXPECT_EQ(offsets, fraction == .008 ? 2u : 0u);
    std::map<std::string, hhds::Node_class> nodes;
    for (auto node : out.placed.graph->body().nodes()) {
      nodes.emplace(std::string(node.get_name()), node);
    }
    std::set<std::string> drivers;
    for (int i = 0; i < 8; ++i) {
      const auto leaf = nodes.at("r" + std::to_string(i)).get_sink_pin("CLK").get_driver_pin().get_master_node();
      const auto pin  = leaf.get_subnode_io()->get_input_pin_decls().front().name;
      drivers.emplace(leaf.get_sink_pin(pin).get_driver_pin().get_master_node().get_name());
    }
    EXPECT_EQ(drivers, expected);
    for (const auto& name : retired) {
      EXPECT_FALSE(nodes.contains(name));
    }
    for (const auto& n : out.placed.floorplan.nets) {
      for (const auto& c : n.connections) {
        EXPECT_FALSE(retired.contains(c.instance.str()));
      }
    }
  }
}
TEST_F(ClockGateMerge, WantedArrivalPreservesCharacterizedMacroInsertion) {
  // Give the fixture's eight sequential loads a hard macro clock-path model.
  auto lef = file_utils::read_file(opts.lefs.back()).value();
  const auto macro = lef.find("MACRO sky130_fd_sc_hd__dfxtp_1\n");
  ASSERT_NE(macro, std::string::npos);
  const auto cell_class = lef.find("CLASS CORE", macro);
  ASSERT_NE(cell_class, std::string::npos);
  lef.replace(cell_class, 10, "CLASS BLOCK");
  opts.lefs.back() = (std::filesystem::path(opts.output_dir) / "macro.lef").string();
  std::ofstream(opts.lefs.back()) << lef;

  auto lib = file_utils::read_file(opts.liberty).value();
  const auto cell = lib.find("cell (\"sky130_fd_sc_hd__dfxtp_1\")");
  ASSERT_NE(cell, std::string::npos);
  const std::string clock_pin = "pin (\"CLK\") {";
  const auto pin = lib.find(clock_pin, cell);
  ASSERT_NE(pin, std::string::npos);
  lib.insert(pin + clock_pin.size(), R"(timing() { timing_type : max_clock_tree_path; timing_sense : positive_unate;
                                          cell_rise(scalar) { values("0.5"); } })");
  opts.liberty = (std::filesystem::path(opts.output_dir) / "macro.lib").string();
  std::ofstream(opts.liberty) << lib;

  const auto skew = [&] {
    const auto run = build(pair(), 10, 0);
    for (const auto& network : run.report["networks"].GetArray()) {
      if (network["source_net"] == "g0") {
        return network["target_skew_ps"].GetDouble();
      }
    }
    ADD_FAILURE() << "missing gated macro clock";
    return 0.;
  };
  opts.clock_arrival_targets["r0:CLK"] = {500., 25.};
  const auto characterized = skew();
  opts.clock_arrival_targets["r0:CLK"] = {std::nullopt, 25.};
  EXPECT_DOUBLE_EQ(skew(), characterized);
  opts.clock_arrival_targets["r0:CLK"] = {0., 25.};
  EXPECT_GT(skew(), characterized + 300.);
}

TEST_F(ClockGateMerge, BalancingHonorsAZeroSinkOffsetBudget) {
  opts.clock_arrival_targets.emplace("r0:CLK", std::pair{500., 0.});
  auto nominal = build(pair(), 10, 0, 0);
  opts.clock_balance = true;
  auto candidate = build(pair(), 10, 0, 0);
  const auto& search = candidate.report["useful_skew"];
  ASSERT_TRUE(search.HasMember("balanced_setup_ps"));
  EXPECT_STREQ(search["balance_decision"].GetString(), "clock_offset");
  EXPECT_EQ(candidate.buffers, nominal.buffers);
  EXPECT_EQ(candidate.placed.floorplan.nets, nominal.placed.floorplan.nets);
}

TEST_F(ClockGateMerge, RaisingASharedGateRemovesUngatedDistributionAheadOfIt) {
  opts.clock_gate_raise_trials = 1;
  auto        run              = build(pair(), 10, 2);
  const auto& trials           = run.report["gate_leaf_trials"];
  ASSERT_EQ(trials.Size(), 1u);
  EXPECT_STREQ(trials[0]["operation"].GetString(), "raise_gate");
  ASSERT_STREQ(trials[0]["decision"].GetString(), "accepted");
  EXPECT_GT(trials[0]["saved_area_um2"].GetDouble(), 0);
  const std::string survivor = trials[0]["gate"].GetString();
  for (auto node : run.placed.graph->body().nodes()) {
    if (node.get_name() == survivor) {
      EXPECT_EQ(node.get_sink_pin("CLK").get_driver_pin(), run.placed.graph->get_input_pin("io_aclk"));
    }
  }
}
TEST_F(ClockGateMerge, GateDirectlyDrivesFlopsAfterMergeWhenArrivalAndElectricalLimitsAllowIt) {
  opts.clock_gate_leaf_trials = 1;
  auto        run             = build(pair(), 10, 2);
  const auto& leaves          = run.report["gate_leaf_trials"];
  ASSERT_EQ(leaves.Size(), 1u);
  ASSERT_STREQ(leaves[0]["decision"].GetString(), "accepted");
  EXPECT_GT(leaves[0]["saved_area_um2"].GetDouble(), 0);
  EXPECT_EQ(run.buffers.size(), 1u);
  size_t direct_pins = 0;
  for (const auto& network : run.report["networks"].GetArray()) {
    if (!network.HasMember("gate_leaf_sinks")) {
      continue;
    }
    const auto& arrivals = network["estimated_direct_sink_arrivals_ps"];
    for (const auto& pin : network["gate_leaf_sinks"].GetArray()) {
      EXPECT_GE(arrivals[pin.GetString()].GetDouble(), 0.);
      ++direct_pins;
    }
    EXPECT_EQ(arrivals.MemberCount(), network["gate_leaf_sinks"].Size());
  }
  EXPECT_GT(direct_pins, 0u);
  for (auto node : run.placed.graph->body().nodes()) {
    if (node.get_name().starts_with("r")) {
      EXPECT_EQ(node.get_sink_pin("CLK").get_driver_pin().get_master_node().get_name(), "b");
    }
  }
  const auto& nets = run.placed.floorplan.nets;
  const auto  net  = std::ranges::find(nets, "g1", &physical::Net::name);
  ASSERT_NE(net, nets.end());
  EXPECT_EQ(net->connections.size(), 9u);
  for (int i = 0; i < 8; ++i) {
    EXPECT_NE(std::ranges::find(net->connections, physical::Connection{"r" + std::to_string(i), "CLK"}), net->connections.end());
  }
  // Independently test the direct-drive guard on a pre-existing eight-flop gate.
  auto one = pair();
  auto a   = std::ranges::find(one.nets, "g0", &physical::Net::name);
  auto b   = std::ranges::find(one.nets, "g1", &physical::Net::name);
  b->connections.insert(b->connections.end(), a->connections.begin() + 1, a->connections.end());
  one.nets.erase(a);
  for (auto& n : one.nets) {
    std::erase_if(n.connections, [](const auto& p) { return p.instance == "a"; });
  }
  std::erase_if(one.components, [](const auto& c) { return c.name == "a"; });
  auto locked = build(one, 10, 0, .001);
  ASSERT_EQ(locked.report["gate_leaf_trials"].Size(), 1u);
  EXPECT_STREQ(locked.report["gate_leaf_trials"][0]["decision"].GetString(), "clock_offset");
  EXPECT_EQ(locked.buffers.size(), 2u);
  // The reference buffer uses one gate output load. Driving all eight flops
  // would violate this variant's per-output Liberty fanout limit.
  auto       lib  = file_utils::read_file(opts.liberty).value();
  const auto gate = lib.find("cell (\"sky130_fd_sc_hd__dlclkp_1\")");
  ASSERT_NE(gate, std::string::npos);
  const auto output = lib.find("pin (\"GCLK\")", gate);
  ASSERT_NE(output, std::string::npos);
  lib.insert(lib.find('{', output) + 1, "\n max_fanout : 4;\n");
  opts.liberty = (std::filesystem::path(opts.output_dir) / "limit.lib").string();
  std::ofstream(opts.liberty) << lib;
  opts.technology_cache.reset();
  auto limited = build(one, 10, 0);
  ASSERT_EQ(limited.report["gate_leaf_trials"].Size(), 1u);
  EXPECT_STREQ(limited.report["gate_leaf_trials"][0]["decision"].GetString(), "infeasible_clock_tree");
  EXPECT_EQ(limited.buffers.size(), 2u);
}
TEST_F(ClockGateMerge, LocalCopyPreservesOtherClockLeaves) {
  auto fp                      = copy_fixture();
  auto control                 = build(fp, 10, 0, .05, copy_sdc);
  opts.clock_gate_clone_trials = 2;
  auto       trial             = build(fp, 10, 0, 1, copy_sdc);
  const auto attempts          = trial.report["gate_clone_trials"].GetArray();
  ASSERT_EQ(attempts.Size(), 2u);
  ASSERT_STREQ(attempts[0]["decision"].GetString(), "accepted");
  EXPECT_STREQ(attempts[1]["decision"].GetString(), "last_gate_leaf");
  const std::string retired = attempts[0]["leaf"].GetString();
  const auto        nodes   = [](Run& run) {
    std::map<std::string, hhds::Node_class> result;
    for (auto node : run.placed.graph->body().nodes()) {
      result.emplace(node.get_name(), node);
    }
    return result;
  };
  const auto previous = nodes(control);
  const auto current  = nodes(trial);
  ASSERT_FALSE(current.contains(retired));
  EXPECT_FALSE(trial.buffers.contains(retired));
  const auto& networks = trial.report["networks"].GetArray();
  const auto  copy     = std::ranges::find_if(networks, [](const auto& n) { return n.HasMember("cloned_from"); });
  ASSERT_NE(copy, networks.end());
  const std::string name = (*copy)["source_instance"].GetString();
  EXPECT_STREQ((*copy)["source_master"].GetString(), "sky130_fd_sc_hd__dlclkp_1");
  auto clone = current.at(name);
  for (const auto& pin : {"CLK", "GATE"}) {
    EXPECT_EQ(clone.get_sink_pin(pin).get_driver_pin(), current.at("a").get_sink_pin(pin).get_driver_pin());
  }
  const auto& nets   = trial.placed.floorplan.nets;
  const auto  direct = std::ranges::find(nets, (*copy)["source_net"].GetString(), &physical::Net::name);
  ASSERT_NE(direct, nets.end());
  EXPECT_EQ(direct->use, "CLOCK");
  std::set<std::string> moved;
  for (const auto& pin : (*copy)["gate_leaf_sinks"].GetArray()) {
    moved.insert(pin.GetString());
  }
  EXPECT_EQ(direct->connections.size(), moved.size() + 1);
  EXPECT_EQ(moved.size(), attempts[0]["sinks"].GetUint());
  for (int i = 0; i < 32; ++i) {
    const auto flop = "r" + std::to_string(i);
    const auto old  = previous.at(flop).get_sink_pin("CLK").get_driver_pin().get_master_node().get_name();
    const auto now  = current.at(flop).get_sink_pin("CLK").get_driver_pin().get_master_node().get_name();
    if (old == retired) {
      EXPECT_EQ(now, name);
      EXPECT_TRUE(moved.contains(flop + ":CLK"));
      EXPECT_NE(std::ranges::find(direct->connections, physical::Connection{flop, "CLK"}), direct->connections.end());
    } else {
      EXPECT_EQ(now, old);
      EXPECT_FALSE(moved.contains(flop + ":CLK"));
    }
  }
  const auto locked = build(fp, 10, 0, 0, copy_sdc);
  for (const auto& t : locked.report["gate_clone_trials"].GetArray()) {
    EXPECT_STREQ(t["decision"].GetString(), "clock_offset");
  }
  EXPECT_EQ(locked.placed.floorplan.nets, control.placed.floorplan.nets);
  EXPECT_EQ(locked.placed.floorplan.components, control.placed.floorplan.components);
  // The enable driver already serves two clock gates. A third input exceeds
  // its Liberty fanout even if the new gate could drive its own flop group.
  auto       lib = file_utils::read_file(opts.liberty).value();
  const auto inv = lib.find("cell (\"sky130_fd_sc_hd__inv_1\")");
  ASSERT_NE(inv, std::string::npos);
  const auto output = lib.find("pin (\"Y\")", inv);
  ASSERT_NE(output, std::string::npos);
  lib.insert(lib.find('{', output) + 1, "\n max_fanout : 2;\n");
  opts.liberty = (std::filesystem::path(opts.output_dir) / "enable_limit.lib").string();
  std::ofstream(opts.liberty) << lib;
  opts.technology_cache.reset();
  auto cell    = fp.components.front();
  cell.name    = "enable_inv";
  cell.macro   = "sky130_fd_sc_hd__inv_1";
  cell.place.x = 4600;
  fp.components.push_back(cell);
  std::ranges::find(fp.nets, "en", &physical::Net::name)->connections = {
      {    "PIN", "en"},
      {cell.name,  "A"}
  };
  fp.nets.push_back({
      "gates_en",
      {{cell.name, "Y"}, {"a", "GATE"}, {"b", "GATE"}}
  });
  auto       limited = build(fp, 10, 0, 1, copy_sdc);
  const auto events  = limited.report["gate_clone_trials"].GetArray();
  ASSERT_EQ(events.Size(), 2u);
  for (const auto& event : events) {
    EXPECT_STREQ(event["decision"].GetString(), "clone_input_fanout");
  }
}
TEST_F(ClockGateMerge, ElectricallyLimitedCopyUsesOnlyAnEquivalentAllowedGateMaster) {
  const std::string old  = "sky130_fd_sc_hd__dlclkp_1";
  const std::string good = "larger_gate", wrong = "different_latch";
  auto              lib = file_utils::read_file(opts.liberty).value();
  auto              lef = file_utils::read_file(opts.lefs[1]).value();
  const auto        at  = lib.find("cell (\"" + old + "\")");
  const auto        end = lib.find("cell (\"sky130_fd_sc_hd__sdlclkp", at);
  ASSERT_NE(at, std::string::npos);
  ASSERT_NE(end, std::string::npos);
  auto strong = lib.substr(at, end - at);
  strong.replace(strong.find(old), old.size(), good);
  auto different = strong;
  different.replace(different.find(good), good.size(), wrong);
  const auto row = different.find("L L : - : L");
  ASSERT_NE(row, std::string::npos);
  different.replace(row, 11, "L L : - : H");
  lib.insert(end, strong + different);
  const auto output = lib.find("pin (\"GCLK\")", at);
  lib.insert(lib.find('{', output) + 1, "\n max_fanout : 4;\n");
  const auto header = lef.find("MACRO " + old), tail = lef.find("END " + old, header);
  ASSERT_NE(header, std::string::npos);
  ASSERT_NE(tail, std::string::npos);
  const auto macro = lef.substr(header, tail + old.size() + 4 - header);
  for (const auto& name : {good, wrong}) {
    auto extra = macro;
    extra.replace(extra.rfind(old), old.size(), name);
    extra.replace(extra.find(old), old.size(), name);
    lef.insert(lef.rfind("END LIBRARY"), extra + "\n");
  }
  opts.liberty = opts.output_dir + "/clones.lib";
  opts.lefs[1] = opts.output_dir + "/clones.lef";
  std::ofstream(opts.liberty) << lib;
  std::ofstream(opts.lefs[1]) << lef;
  opts.clock_gate_clone_trials = 1;
  auto       fp                = copy_fixture();
  auto       trial             = build(fp, 10, 0, 1, copy_sdc);
  const auto attempts          = trial.report["gate_clone_trials"].GetArray();
  ASSERT_GT(attempts.Size(), 0u);
  const auto& event = attempts[0];
  ASSERT_STREQ(event["decision"].GetString(), "accepted");
  ASSERT_STREQ(event["master"].GetString(), good.c_str());
  std::string instance = event["instance"].GetString();
  bool        bound    = false;
  for (auto node : trial.placed.graph->body().nodes()) {
    if (node.get_name() == instance) {
      bound = true;
      EXPECT_EQ(node.get_subnode_io()->get_name(), good);
    }
  }
  EXPECT_TRUE(bound);
  auto cell = std::ranges::find(trial.placed.floorplan.components, instance, &physical::Component::name);
  ASSERT_NE(cell, trial.placed.floorplan.components.end());
  EXPECT_EQ(cell->macro.str(), good);
  for (bool lock_size : {true, false}) {
    opts.runtime_policy            = std::make_shared<Mutation_policy>();
    opts.runtime_policy->no_sizing = lock_size;
    if (!lock_size) {
      opts.resize_dont_use = {good};
    }
    auto       blocked = build(fp, 10, 0, 1, copy_sdc);
    const auto events  = blocked.report["gate_clone_trials"].GetArray();
    ASSERT_GT(events.Size(), 0u);
    for (const auto& e : events) {
      EXPECT_STREQ(e["decision"].GetString(), "clone_drive_limit");
    }
  }
  // Both equivalent cells now meet the same load. A zero arrival budget
  // rejects the original size; the alternative must receive a separate STA trial.
  opts.runtime_policy.reset();
  opts.resize_dont_use.clear();
  opts.clock_gate_clone_trials = 12;
  opts.liberty                 = opts.output_dir + "/clone_timing.lib";
  lib.erase(lib.find("max_fanout : 4;", output), std::string("max_fanout : 4;").size());
  std::ofstream(opts.liberty) << lib;
  auto                  retries = build(fp, 10, 0, 0);
  std::set<std::string> measured;
  for (const auto& e : retries.report["gate_clone_trials"].GetArray()) {
    if (e.HasMember("setup_ps")) {
      measured.emplace(e["master"].GetString());
      EXPECT_STREQ(e["decision"].GetString(), "clock_offset");
    }
  }
  EXPECT_EQ(measured, (std::set<std::string>{old, good}));
}
TEST_F(ClockGateMerge, UnrelatedInternalExceptionRemainsEffectiveAndReferencedGatesSurvive) {
  auto unrelated = build(pair(), 10, 2, .05, "set_false_path -to [get_pins r0/D]\n");
  ASSERT_EQ(unrelated.report["gate_merge_trials"].Size(), 2u);
  EXPECT_EQ(unrelated.buffers.size(), 2u);
  auto protected_gate = build(pair(), 10, 2, .05, "set_false_path -to [get_pins a/GATE]\n");
  EXPECT_TRUE(protected_gate.report["gate_merge_trials"].Empty());
  EXPECT_EQ(protected_gate.buffers.size(), 3u);
  // The queries are quiet before CTS; the ECO changes the resolved command
  // after buffering even though the script never names a gate instance.
  auto conditional
      = build(pair(),
              10,
              2,
              .05,
              "if {[llength [get_cells -quiet __pcrs_clk_*]] == 3} {set_output_delay -clock io_aclk 1 [get_ports q0]}\n");
  ASSERT_EQ(conditional.report["gate_merge_trials"].Size(), 2u);
  for (const auto& trial : conditional.report["gate_merge_trials"].GetArray()) {
    EXPECT_STREQ(trial["decision"].GetString(), "constraint_targets_changed");
  }
  EXPECT_EQ(conditional.buffers.size(), 3u);
}
}  // namespace
}  // namespace livehd::placement
