// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "inline_sub.hpp"

#include <format>

#include "cell.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"

namespace gu = livehd::graph_util;

TEST(SubInline, FeedThroughChainRetainsItsExternalDriver) {
  // The instance appears cyclic as a node, but each output simply feeds the
  // next independent lane. Its final output is exactly the parent input.
  constexpr int count = 128;
  auto&         lib   = livehd::Hhds_graph_library::instance("lgdb_inline_alias_chain");
  auto          cio   = lib.create_io("lanes");
  for (int i = 0; i < count; ++i) {
    cio->add_input(std::format("x{}", i), static_cast<hhds::Port_id>(i));
    cio->add_output(std::format("y{}", i), static_cast<hhds::Port_id>(count + i));
    cio->set_bits(std::format("x{}", i), 8);
    cio->set_bits(std::format("y{}", i), 8);
  }
  auto child = cio->create_graph();
  for (int i = 0; i < count; ++i) {
    child->get_input_pin(std::format("x{}", i)).connect_sink(child->get_output_pin(std::format("y{}", i)));
  }
  auto pio = lib.create_io("parent");
  pio->add_input("data", 0);
  pio->add_output("result", 1);
  pio->set_bits("data", 8);
  pio->set_bits("result", 8);
  auto parent = pio->create_graph();
  auto inst   = gu::create_typed_node(*parent, Ntype_op::Sub);
  inst.set_subnode(cio);
  auto data = parent->get_input_pin("data");
  data.connect_sink(inst.create_sink_pin(0));
  for (int i = 1; i < count; ++i) {
    inst.create_driver_pin(static_cast<hhds::Port_id>(count + i - 1))
        .connect_sink(inst.create_sink_pin(static_cast<hhds::Port_id>(i)));
  }
  inst.create_driver_pin(static_cast<hhds::Port_id>(2 * count - 1)).connect_sink(parent->get_output_pin("result"));
  ASSERT_TRUE(gu::inline_sub_instance(parent.get(), inst, "test"));
  int drivers = 0;
  for (const auto& edge : parent->get_output_pin("result").inp_edges()) {
    EXPECT_EQ(edge.driver, data);
    ++drivers;
  }
  EXPECT_EQ(drivers, 1);
}
