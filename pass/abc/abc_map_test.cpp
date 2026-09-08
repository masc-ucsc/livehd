// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "abc_map.hpp"

#include <cstdlib>
#include <string>
#include <vector>

#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"

namespace gu = livehd::graph_util;

TEST(AbcMap, NativeBoundarySchedulesReverseOrderedSliceConsumers) {
  const auto root       = std::string(std::getenv("TEST_TMPDIR")) + "/native_schedule";
  auto&      source_lib = livehd::Hhds_graph_library::instance(root + "/source");
  auto&      output_lib = livehd::Hhds_graph_library::instance(root + "/mapped");
  auto       source_io  = source_lib.create_io("source");
  source_io->add_input("clk", 1);
  source_io->add_input("d", 2);
  source_io->add_output("y", 3);
  source_io->set_bits("clk", 1);
  source_io->set_bits("d", 8);
  source_io->set_bits("y", 1);
  auto source = source_io->create_graph();
  auto clk    = source->get_input_pin("clk");
  auto d      = source->get_input_pin("d");
  gu::set_ubits(clk, 1);
  gu::set_ubits(d, 8);
  auto flop = gu::create_typed_node(*source, Ntype_op::Flop);
  d.connect_sink(gu::setup_sink_by_name(flop, "din"));
  clk.connect_sink(gu::setup_sink_by_name(flop, "clock_pin"));
  auto q = flop.create_driver_pin(0);
  gu::set_ubits(q, 8);
  auto slice = gu::create_typed_node(*source, Ntype_op::Get_mask);
  q.connect_sink(gu::setup_sink_by_name(slice, "a"));
  gu::create_const(*source, *Dlop::create_integer(8)).connect_sink(gu::setup_sink_by_name(slice, "mask"));
  auto bit = slice.create_driver_pin(0);
  gu::set_ubits(bit, 1);
  auto inv = gu::create_typed_node(*source, Ntype_op::Not);
  bit.connect_sink(gu::setup_sink_by_name(inv, "a"));
  auto y = inv.create_driver_pin(0);
  gu::set_ubits(y, 1);
  y.connect_sink(source->get_output_pin("y"));

  auto mapped_io = output_lib.create_io("mapped");
  mapped_io->add_input("clk", 1);
  mapped_io->add_input("d", 2);
  mapped_io->add_output("y", 3);
  mapped_io->set_bits("clk", 1);
  mapped_io->set_bits("d", 8);
  mapped_io->set_bits("y", 1);
  auto                           mapped = mapped_io->create_graph();
  // The region API does not promise topological order. Before the repair, the
  // lazy native Q was absent from the ready set, leaving inv before slice.
  std::vector<hhds::Node_class>  nodes{inv, slice, flop};
  livehd::partition::Region_body rb;
  rb.src         = source.get();
  rb.body        = mapped.get();
  rb.module_name = "mapped";
  rb.color       = 1;
  rb.nodes       = nodes;
  rb.inputs      = {
      {.name = "clk", .src_driver = clk, .bits = 1, .sign = false},
      {  .name = "d",   .src_driver = d, .bits = 8, .sign = false}
  };
  rb.outputs = {
      {.name = "y", .src_driver = y, .bits = 1, .sign = false}
  };
  livehd::abc::Map_options options;
  options.library      = "inou/prp/tests/abc/test.lib";
  options.map_register = false;
  livehd::abc::Mapper mapper(options);
  mapper.set_outlib(&output_lib);
  mapper.map_region(rb);
  ASSERT_EQ(mapper.qor().size(), 1);
  EXPECT_GT(mapper.qor().front().gates, 0);
  int drivers = 0;
  for ([[maybe_unused]] const auto& edge : mapped->get_output_pin("y").inp_edges()) {
    ++drivers;
  }
  EXPECT_EQ(drivers, 1);
}

TEST(AbcMap, WareUsesTimingOrAreaObjective) {
  using livehd::abc::ware_qor_better;
  EXPECT_TRUE(ware_qor_better(
      {
          10,
          {100, 80}
  },
      {12, {90, 85}},
      true));  // fastest, even larger
  EXPECT_FALSE(ware_qor_better(
      {
          10,
          {100, 80}
  },
      {9, {110, 70}},
      true));
  EXPECT_TRUE(ware_qor_better(
      {
          10,
          {100, 100}
  },
      {12, {100, 90}},
      true));                                                    // tied critical path
  EXPECT_TRUE(ware_qor_better({10, {100}}, {9, {110}}, false));  // area without timing
  EXPECT_FALSE(ware_qor_better({10, {100}}, {12, {90}}, false));
  EXPECT_TRUE(ware_qor_better({10, {100}}, {9, {100}}, true));  // area breaks timing ties
  EXPECT_FALSE(ware_qor_better(
      {
          10,
          {100, 80}
  },
      {9, {70}},
      true));  // endpoint loss
  EXPECT_FALSE(ware_qor_better({10, {}}, {9, {}}, true));
}
