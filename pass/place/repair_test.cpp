// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "repair.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>

#include "attrs.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "pass/liberty/liberty_dff.hpp"
#include "pass/opentimer/pass_opentimer.hpp"
#include "rapidjson/document.h"
#include "steiner_repair.hpp"
namespace livehd::placement {
namespace {
namespace gu = graph_util;
class Buffer_repair : public ::testing::Test {
protected:
  hhds::GraphLibrary           source, flat;
  std::shared_ptr<hhds::Graph> graph;
  Options                      options;
  unsigned                     serial = 0;
  void                         SetUp() override {
    options.lefs       = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
    options.liberty    = "inou/lefdef/tests/data/sky130hd.lib";
    options.iterations = 10;
    options.bins       = 8;
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
    const auto path = options.output_dir + "/timing_" + std::to_string(serial++) + ".json";
    const auto sdc  = options.output_dir + "/clock.sdc";
    std::ofstream(sdc) << "create_clock -name virtual -period 10\nset_input_delay -clock virtual 0 [all_inputs]\nset_output_delay "
                          "-clock virtual 0 [all_outputs]\n";
    Eprp_var var;
    var.graphs        = {state.graph};
    var.physical_rc   = state.parasitics;
    var.dict["files"] = options.liberty + "," + sdc;
    var.dict["top"]   = "top";
    var.dict["hier"]  = "false";
    var.dict["qor"]   = path;
    var.set_stage_labels(var.dict);
    Pass_opentimer::time_work(var);
    std::ifstream     input(path);
    const std::string text{std::istreambuf_iterator<char>(input), {}};
    return timing_feedback(text);
  }
  // Independent identity/inversion interpretation of the repaired netlist.
  // Every primary output must still be an inverter of a, through any number
  // of non-inverting buffers; catches disconnected branches and cycles.
  void equivalent(const Result& state) {
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
      EXPECT_EQ(inversions % 2, 1);
      EXPECT_EQ(driver.get_pin_name(), "a");
    }
  }
};
TEST_F(Buffer_repair, SingleCellLegalizationKeepsEveryExistingNeighborFixed) {
  options.utilization = 0.25;
  auto b              = gate("sky130_fd_sc_hd__buf_1", "b", graph->get_input_pin("a"));
  auto p              = gate("sky130_fd_sc_hd__inv_1", "invert", b);
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
TEST_F(Buffer_repair, TimingWeightsExcludeClockNetsAndRevertWorsePlacements) {
  auto b = gate("sky130_fd_sc_hd__buf_1", "clock_buffer", graph->get_input_pin("a"));
  auto p = gate("sky130_fd_sc_hd__inv_1", "invert", b);
  graph->get_output_pin("y").connect_driver(p);
  auto       placed      = place(*graph, flat, options);
  const auto identity    = placed.parasitics->identity;
  unsigned   evaluations = 0;
  const auto result      = timing_place(
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
  for (const auto& round : report["rounds"].GetArray()) {
    EXPECT_EQ(round["weights"]["clock_buffer:X"].GetDouble(), 1);
    EXPECT_FALSE(round["accepted"].GetBool());
  }
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
    Timing_feedback f;
    f.delay = count(r, "clock_buffer") ? 10 : 1;
    f.area  = 1;
    return f;
  });
  EXPECT_EQ(count(result.placed, "clock_buffer"), 1);
  rapidjson::Document report;
  report.Parse(result.report.c_str());
  EXPECT_EQ(report["attempts"].GetUint(), 0);
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
