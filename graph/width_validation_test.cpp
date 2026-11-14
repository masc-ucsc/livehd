// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "width_validation.hpp"

#include "diag.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"

namespace gu = livehd::graph_util;

TEST(WidthValidation, RejectsUnsizedMuxAndAcceptsTypedReplacement) {
  hhds::GraphLibrary library;
  auto               io = library.create_io("broadcast");
  io->add_input("s", 1);
  io->set_bits("s", 1);
  io->add_output("o", 2);
  auto graph = io->create_graph();
  auto mux   = gu::create_typed_node(*graph, Ntype_op::Mux);
  graph->get_input_pin("s").connect_sink(mux.create_sink_pin(0));
  gu::create_const(*graph, *Dlop::create_integer(0)).connect_sink(mux.create_sink_pin(1));
  gu::create_const(*graph, *Dlop::create_integer(0xfff)).connect_sink(mux.create_sink_pin(2));
  mux.create_driver_pin(0).connect_sink(graph->get_output_pin("o"));
  auto& sink = livehd::diag::sink();
  sink.clear();
  sink.set_human_stderr(false);
  EXPECT_THROW(gu::require_sized_design(*graph, "pass.lec"), std::runtime_error);
  ASSERT_FALSE(sink.records().empty());
  const auto& error = sink.records().back();
  EXPECT_EQ(error.severity, livehd::diag::Severity::error);
  EXPECT_EQ(error.code, "graph-width-unresolved");
  EXPECT_NE(error.message.find("broadcast"), std::string::npos);
  sink.clear();
  gu::set_ubits(mux.get_driver_pin(0), 12);
  EXPECT_NO_THROW(gu::require_sized_design(*graph, "inou.cgen.sim"));
  gu::set_sbits(mux.get_driver_pin(0), 13);
  EXPECT_NO_THROW(gu::require_sized_design(*graph, "pass.abc"));
}

TEST(WidthValidation, ChecksUnlistedChildAndEachInstanceOutput) {
  hhds::GraphLibrary library;
  auto               child_io = library.create_io("child");
  child_io->add_output("y", 7);
  child_io->set_bits("y", 8);
  auto child = child_io->create_graph();
  auto cell  = gu::create_typed_node(*child, Ntype_op::Or);
  gu::create_const(*child, *Dlop::create_integer(5)).connect_sink(cell.create_sink_pin(0));
  cell.create_driver_pin(0).connect_sink(child->get_output_pin("y"));
  auto top_io = library.create_io("top");
  top_io->add_output("y", 1);
  auto top      = top_io->create_graph();
  auto instance = gu::create_typed_node(*top, Ntype_op::Sub);
  instance.set_subnode(child_io);
  auto output = instance.create_driver_pin(7);
  gu::set_ubits(output, 8);
  output.connect_sink(top->get_output_pin("y"));
  auto& sink = livehd::diag::sink();
  sink.clear();
  sink.set_human_stderr(false);
  EXPECT_THROW(gu::require_sized_design(*top, "pass.usyn"), std::runtime_error);
  sink.clear();
  gu::set_ubits(cell.get_driver_pin(0), 8);
  EXPECT_NO_THROW(gu::require_sized_design(*top, "pass.usyn"));
  gu::set_bits(output, 0);
  EXPECT_THROW(gu::require_sized_design(*top, "pass.usyn"), std::runtime_error);
  sink.clear();
}
