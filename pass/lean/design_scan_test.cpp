// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "design_scan.hpp"

#include <sstream>

#include "certificate_ir.hpp"
#include "emit_design_cert.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"

namespace lean_export {
namespace {
using livehd::graph_util::create_typed_node;
using livehd::graph_util::set_bits;
TEST(DesignScan, OwnsPinsAndPreservesPortDeclarationRoots) {
  DesignScan design;
  {
    auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_owned");
    auto  io  = lib.create_io("owned");
    io->add_input("b", 1);
    io->set_bits("b", 8);
    io->add_input("a", 2);
    io->set_bits("a", 8);
    io->add_output("z", 1);
    io->set_bits("z", 8);
    io->add_output("y", 2);
    io->set_bits("y", 8);
    auto g      = io->create_graph();
    auto first  = create_typed_node(*g, Ntype_op::And);
    auto second = create_typed_node(*g, Ntype_op::Or);
    set_bits(first.create_driver_pin(0), 8);
    set_bits(second.create_driver_pin(0), 8);
    g->get_input_pin("a").connect_sink(first.create_sink_pin(0));
    g->get_input_pin("b").connect_sink(first.create_sink_pin(0));
    first.create_driver_pin(0).connect_sink(g->get_output_pin("z"));
    first.create_driver_pin(0).connect_sink(second.create_sink_pin(0));
    second.create_driver_pin(0).connect_sink(g->get_output_pin("y"));
    design = scan_design(*g, {});
    EXPECT_EQ(design.inputs[0].name, "a");
    EXPECT_EQ(design.inputs[0].id, 2000000001);
    EXPECT_EQ(design.outputs[0].name, "y");
    EXPECT_EQ(design.roots[0].id, first.get_debug_nid());
    EXPECT_EQ(design.nodes[0].op, ScanOp::And);
    // Mutating the graph after scanning must not affect certificate building.
    set_bits(first.create_driver_pin(0), 16);
  }
  const auto cert = build_certificate(design, {});
  ASSERT_EQ(cert.nodes.size(), 2);
  EXPECT_EQ(cert.nodes[0].width, 8);
  std::ostringstream text;
  emit_design_cert(design, cert, text);
  EXPECT_NE(text.str().find("owned_designCert"), std::string::npos);
}
TEST(DesignScan, SynchronousRomRetainsContentsAndEnable) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_rom");
  auto  io  = lib.create_io("sync_rom");
  io->add_input("address", 1);
  io->set_bits("address", 2);
  io->add_input("enable", 2);
  io->set_bits("enable", 1);
  io->add_output("data", 1);
  io->set_bits("data", 8);
  auto g      = io->create_graph();
  auto memory = create_typed_node(*g, Ntype_op::Memory);
  set_bits(memory.create_driver_pin(0), 8);
  auto policy = [&](std::string_view name, int64_t value) {
    auto pin = livehd::graph_util::create_const(*g, *Dlop::create_integer(value));
    pin.connect_sink(memory.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Memory, name)));
  };
  policy("bits", 8);
  policy("size", 3);
  policy("type", 1);
  policy("rdport", 1);
  policy("init", 0x030201);
  g->get_input_pin("address").connect_sink(memory.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Memory, "addr")));
  g->get_input_pin("enable").connect_sink(memory.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Memory, "enable")));
  memory.create_driver_pin(0).connect_sink(g->get_output_pin("data"));
  const auto design = scan_design(*g, {});
  const auto cert   = build_certificate(design, {});
  ASSERT_EQ(cert.flops.size(), 1);
  EXPECT_TRUE(cert.memories.empty());
  EXPECT_EQ(cert.sources[0].rom_contents, (std::vector<std::string>{"1", "2", "3"}));
  EXPECT_EQ(cert.nodes.back().op.kind, Operation::MuxBool);
}
TEST(DesignScan, RejectsCombinationalCycleAndZeroWidth) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_cycle");
  auto  io  = lib.create_io("cycle");
  io->add_output("y", 1);
  io->set_bits("y", 1);
  auto g = io->create_graph();
  auto a = create_typed_node(*g, Ntype_op::Not);
  auto b = create_typed_node(*g, Ntype_op::Not);
  set_bits(a.create_driver_pin(0), 1);
  set_bits(b.create_driver_pin(0), 1);
  a.create_driver_pin(0).connect_sink(b.create_sink_pin(0));
  b.create_driver_pin(0).connect_sink(a.create_sink_pin(0));
  a.create_driver_pin(0).connect_sink(g->get_output_pin("y"));
  EXPECT_THROW(scan_design(*g, {}), std::runtime_error);
  auto empty_io = lib.create_io("zero");
  empty_io->add_input("x", 0);
  auto empty = empty_io->create_graph();
  EXPECT_THROW(scan_design(*empty, {}), std::runtime_error);
}
}  // namespace
}  // namespace lean_export
