// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "design_scan.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
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
    g->get_input_pin("b").connect_sink(first.create_sink_pin(1));
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
TEST(DesignScan, BankedSumPreservesEveryOperandIncludingDuplicates) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_banked_sum");
  auto  io  = lib.create_io("banked_sum");
  io->add_output("y", 1);
  io->set_bits("y", 8);
  auto g   = io->create_graph();
  auto sum = create_typed_node(*g, Ntype_op::Sum);
  set_bits(sum.create_driver_pin(0), 8);
  // Non-dense, deliberately interleaved slots: 10 + 10 + 30 - 7 - 2 = 41.
  // Both tens share a constant-pool pin but must remain two dependencies.
  for (const auto& [port, value] : std::vector<std::pair<int, int>>{
           {4, 30},
           {3,  2},
           {0, 10},
           {7,  7},
           {2, 10}
  }) {
    livehd::graph_util::create_const(*g, *Dlop::create_integer(value)).connect_sink(sum.create_sink_pin(port));
  }
  sum.create_driver_pin(0).connect_sink(g->get_output_pin("y"));
  const auto design = scan_design(*g, {});
  const auto cert   = build_certificate(design, {});
  ASSERT_EQ(cert.nodes.size(), 1);
  const auto& n = cert.nodes[0];
  EXPECT_EQ(n.op.kind, Operation::Sum);
  ASSERT_EQ(n.op.parameter, 3);
  ASSERT_EQ(n.deps.size(), 5);
  int result = 0;
  for (size_t i = 0; i < n.deps.size(); ++i) {
    const auto it
        = std::find_if(cert.sources.begin(), cert.sources.end(), [&](const Source& source) { return source.id == n.deps[i]; });
    ASSERT_NE(it, cert.sources.end());
    result += (i < n.op.parameter ? 1 : -1) * std::stoi(it->const_int);
  }
  EXPECT_EQ(result, 41);
  if (const char* path = std::getenv("LEAN_BANKED_SUM_FIXTURE")) {
    std::ofstream out(path);
    emit_design_cert(design, cert, out);
    out << R"(
example : bv_uint ((banked_sum_step #[] ⟨#[], #[]⟩).outputs[0]!) = 41 := by native_decide
)";
    ASSERT_TRUE(out.good());
  }
}
TEST(DesignScan, ConstantPoolPreservesUnsizedValuesAndSignedWidths) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_const_pool");
  auto  io  = lib.create_io("constant_pool");
  io->add_output("positive", 1);
  io->set_bits("positive", 32);
  io->add_output("negative", 2);
  io->set_bits("negative", 8);
  auto g = io->create_graph();
  livehd::graph_util::create_const(*g, *Dlop::create_integer(0x6000000)).connect_sink(g->get_output_pin("positive"));
  livehd::graph_util::create_const(*g, *Dlop::create_integer(-64)).connect_sink(g->get_output_pin("negative"));
  const auto design = scan_design(*g, {});
  ASSERT_EQ(design.outputs.size(), 2);
  ASSERT_TRUE(design.outputs[0].driver.has_value());
  ASSERT_TRUE(design.outputs[1].driver.has_value());
  EXPECT_EQ(design.outputs[0].driver->value, "-64");
  EXPECT_EQ(design.outputs[0].driver->intrinsic_width, 7);
  EXPECT_EQ(design.outputs[1].driver->value, "100663296");
  EXPECT_EQ(design.outputs[1].driver->intrinsic_width, 27);
  const auto cert = build_certificate(design, {});
  ASSERT_EQ(cert.sources.size(), 2);
  EXPECT_EQ(cert.sources[0].const_int, "-64");
  EXPECT_EQ(cert.sources[0].width, 8);
  EXPECT_EQ(cert.sources[1].const_int, "100663296");
  EXPECT_EQ(cert.sources[1].width, 32);
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
  policy("initial", 0x030201);
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
TEST(DesignScan, ActiveLowAsyncResetSurvivesResizingAndControlsNextState) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_reset");
  auto  io  = lib.create_io("reset_fixture");
  io->add_input("clk", 1);
  io->set_bits("clk", 1);
  io->add_input("enable", 2);
  io->set_bits("enable", 1);
  io->add_input("rst", 3);
  io->set_bits("rst", 1);
  io->add_output("q", 1);
  io->set_bits("q", 8);
  auto g    = io->create_graph();
  auto flop = create_typed_node(*g, Ntype_op::Flop);
  set_bits(flop.create_driver_pin(0), 8);
  auto resize = create_typed_node(*g, Ntype_op::Or);
  set_bits(resize.create_driver_pin(0), 1);
  g->get_input_pin("rst").connect_sink(resize.create_sink_pin(0));
  resize.create_driver_pin(0).connect_sink(flop.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Flop, "reset_pin")));
  g->get_input_pin("enable").connect_sink(flop.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Flop, "enable")));
  g->get_input_pin("clk").connect_sink(flop.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Flop, "clock_pin")));
  auto drive = [&](std::string_view name, int64_t value) {
    livehd::graph_util::create_const(*g, *Dlop::create_integer(value))
        .connect_sink(flop.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Flop, name)));
  };
  drive("din", 12);
  drive("initial", 7);
  drive("async", 1);
  drive("negreset", 1);
  flop.create_driver_pin(0).connect_sink(g->get_output_pin("q"));
  const auto design = scan_design(*g, {});
  ASSERT_EQ(design.flops.size(), 1);
  EXPECT_TRUE(design.flops[0].active_low);
  EXPECT_EQ(design.flops[0].reset_input, design.inputs[2].id);
  const auto cert = build_certificate(design, {});
  ASSERT_EQ(cert.flops.size(), 1);
  EXPECT_TRUE(cert.flops[0].reset_active_low);
  EXPECT_TRUE(cert.sources[0].async_reset);
  EXPECT_TRUE(cert.sources[0].reset_active_low);
  // Optional end-to-end Lean oracle, always written under the caller's project
  // runtime directory. Run this test with LEAN_RESET_FIXTURE=<path>.lean, then
  // elaborate that file with the compiler library on LEAN_PATH.
  if (const char* path = std::getenv("LEAN_RESET_FIXTURE")) {
    std::ofstream out(path);
    emit_design_cert(design, cert, out);
    out << R"(
def reset_fixture_initial : RuntimeState := ⟨#[mk_bv 8 5], #[]⟩
-- rst=0 asserts reset, even with enable=0: both Q and the next state are 7.
example : bv_uint ((reset_fixture_step #[mk_bv 1 0, mk_bv 1 0, mk_bv 1 0]
    reset_fixture_initial).outputs[0]!) = 7 := by native_decide
example : bv_uint ((reset_fixture_step #[mk_bv 1 0, mk_bv 1 0, mk_bv 1 0]
    reset_fixture_initial).nextState.flops[0]!) = 7 := by native_decide
-- rst=1 releases reset. Disabled state holds 5; enabled state takes din=12.
example : bv_uint ((reset_fixture_step #[mk_bv 1 0, mk_bv 1 0, mk_bv 1 1]
    reset_fixture_initial).nextState.flops[0]!) = 5 := by native_decide
example : bv_uint ((reset_fixture_step #[mk_bv 1 0, mk_bv 1 1, mk_bv 1 1]
    reset_fixture_initial).nextState.flops[0]!) = 12 := by native_decide
)";
    ASSERT_TRUE(out.good());
  }
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
