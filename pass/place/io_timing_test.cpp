// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "io_timing.hpp"

#include <filesystem>

#include "attrs.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "pass/partition/flatten.hpp"
#include "rapidjson/document.h"

namespace livehd::placement {
TEST(IoTiming, CrossingColorsAndSingleAutomaticRound) {
  hhds::GraphLibrary library;
  auto               inv = library.create_io("INVx1");
  inv->add_input("A", 1);
  inv->add_output("Y", 2);
  inv->set_bits("A", 1);
  inv->set_bits("Y", 1);
  std::vector<std::shared_ptr<hhds::GraphIO>> colors;
  for (int c = 1; c <= 2; ++c) {
    auto io = library.create_io("top__c" + std::to_string(c));
    io->add_input("a", 1);
    io->add_output("y", 2);
    io->set_bits("a", 1);
    io->set_bits("y", 1);
    auto graph = io->create_graph();
    graph->get_input_node().attr(attrs::synth_region).set(std::string(io->get_name()));
    graph->get_input_node().attr(attrs::synth_region_id).set(c);
    auto driver = graph->get_input_pin("a");
    graph_util::set_bits(driver, 1);
    for (int i = 0; i < 4; ++i) {
      auto n = graph_util::create_typed_node(*graph, Ntype_op::Sub);
      n.set_subnode(inv);
      n.set_name("inv" + std::to_string(i));
      n.attr(attrs::synth_region_id).set(c);
      driver.connect_sink(n.create_sink_pin("A"));
      driver = n.create_driver_pin("Y");
      graph_util::set_bits(driver, 1);
    }
    driver.connect_sink(graph->get_output_pin("y"));
    graph->commit();
    colors.push_back(io);
  }
  auto top_io = library.create_io("top");
  top_io->add_input("a", 1);
  top_io->add_output("y", 2);
  top_io->set_bits("a", 1);
  top_io->set_bits("y", 1);
  auto top    = top_io->create_graph();
  auto driver = top->get_input_pin("a");
  graph_util::set_bits(driver, 1);
  for (int i = 0; i < 2; ++i) {
    auto n = graph_util::create_typed_node(*top, Ntype_op::Sub);
    n.set_subnode(colors[i]);
    n.set_name(i ? "right" : "left");
    driver.connect_sink(n.create_sink_pin("a"));
    driver = n.create_driver_pin("y");
    graph_util::set_bits(driver, 1);
  }
  driver.connect_sink(top->get_output_pin("y"));
  top->commit();
  Options options;
  options.liberty           = "inou/prp/tests/abc/timing.lib";
  options.lefs              = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  options.resistance_ohm_um = .1;
  options.capacitance_ff_um = .2;
  options.output_dir        = std::string(std::getenv("TEST_TMPDIR")) + "/io_flow";
  std::filesystem::remove_all(options.output_dir);
  auto       store    = std::make_shared<synth::Io_store>(options.output_dir + "/abc_cache", 1000);
  const auto evaluate = [](const Result& placed) {
    Timing_feedback timing;
    timing.time_unit_ps = 1;
    timing.delay        = 1000;
    for (auto n : placed.graph->body().nodes()) {
      if (auto io = n.get_subnode_io()) {
        for (const auto& p : io->get_input_pin_decls()) {
          timing.pins[std::string(n.get_name()) + ":" + p.name].arrival = 950;
        }
        for (const auto& p : io->get_output_pin_decls()) {
          timing.pins[std::string(n.get_name()) + ":" + p.name].required = 1000;
        }
      }
    }
    return timing;
  };
  Io_timing_flow flow(top,
                      options,
                      store,
                      {
                          {"library", options.liberty},
                          {  "delay",          "1000"}
  },
                      evaluate,
                      5);
  EXPECT_FALSE(flow.before_placement());
  hhds::GraphLibrary flat_library;
  Result             placed;
  placed.graph = partition::flatten_hierarchy(top.get(), &flat_library, "top", nullptr, false, {}, {}, true);
  ASSERT_NE(placed.graph, nullptr);
  placed.parasitics = std::make_shared<physical::Parasitics>();
  for (auto node : placed.graph->body().nodes()) {
    if (node.get_subnode_io()) {
      physical::Rc_net net;
      net.driver = std::string(node.get_name()) + ":Y";
      net.nodes  = {
          {                         net.driver, 1},
          {std::string(node.get_name()) + ":A", 1}
      };
      net.wire_length_um = 10;
      placed.parasitics->nets.push_back(std::move(net));
    }
  }
  EXPECT_TRUE(flow.after_stage(Placement_stage::floorplan, placed));
  EXPECT_EQ(store->wires.size(), 2);
  EXPECT_EQ(store->rows.at("top__c1").used.inputs.at("a[0]"), 950);
  EXPECT_EQ(store->rows.at("top__c2").used.inputs.at("a[0]"), 950);
  EXPECT_FALSE(flow.after_stage(Placement_stage::macro, placed));
  EXPECT_FALSE(flow.after_stage(Placement_stage::final, placed));
  rapidjson::Document report;
  report.Parse(flow.report().c_str());
  EXPECT_EQ(report["restarts"].GetUint(), 1);
  EXPECT_EQ(report["accepted"].GetUint(), 2);
  EXPECT_STREQ(report["restarted_stage"].GetString(), "fp");
}
}  // namespace livehd::placement
