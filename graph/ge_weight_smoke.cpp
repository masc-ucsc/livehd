//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Tests ge_weight() in node_util.hpp: the GATE-EQUIVALENT weight that the
// pass.color min/max size window sizes regions with (todo/livehd/2c-color-size.html
// R1). The whole point of the metric is that it is WIDTH-aware where a node count
// is not, so every test here pins a width -- and the bits==0 degradation, which is
// what a pre-bitwidth run gets, is pinned too.
//
// Named *_smoke, not *_test: ge_weight is header-only (node_util.hpp has no
// node_util.cpp), and AGENTS.md reserves the _test suffix for a file that pairs
// with a same-named .cpp.

#include <cstdint>

#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"

using livehd::graph_util::create_const;
using livehd::graph_util::create_typed_node;
using livehd::graph_util::ge_weight;
using livehd::graph_util::set_bits;

namespace {

// A fresh library per test: Hhds_graph_library::instance keys on the directory
// name, so a shared one would leak def names across tests.
hhds::GraphLibrary& lib_for(const char* dir) { return livehd::Hhds_graph_library::instance(dir); }

// A one-node def holding `op`, with `bits` stamped on the node's driver pin
// (0 => leave the width unset, the pre-bitwidth shape).
struct One_node {
  std::shared_ptr<hhds::Graph> g;
  hhds::Node_class             n;
};

One_node one_node(const char* dir, const char* name, Ntype_op op, int32_t bits) {
  auto& lib = lib_for(dir);
  auto  gio = lib.create_io(name);
  gio->add_input("a", 0);
  gio->add_output("y", 1);
  auto g = gio->create_graph();
  auto n = create_typed_node(*g, op);
  g->get_input_pin("a").connect_sink(n.create_sink_pin(0));
  auto d = n.create_driver_pin(0);
  if (bits != 0) {
    set_bits(d, bits);
  }
  d.connect_sink(g->get_output_pin("y"));
  return {g, n};
}

}  // namespace

// The default rule: one gate per output bit. A node count would call all three
// of these 1; they differ by 32x.
TEST(GeWeight, WidthIsTheWeightForBitwiseAndArith) {
  EXPECT_EQ(ge_weight(one_node("lgdb_ge_and", "and1", Ntype_op::And, 1).n), 1u);
  EXPECT_EQ(ge_weight(one_node("lgdb_ge_and", "and32", Ntype_op::And, 32).n), 32u);
  EXPECT_EQ(ge_weight(one_node("lgdb_ge_sum", "sum32", Ntype_op::Sum, 32).n), 32u);
  EXPECT_EQ(ge_weight(one_node("lgdb_ge_mux", "mux16", Ntype_op::Mux, 16).n), 16u);
  EXPECT_EQ(ge_weight(one_node("lgdb_ge_not", "not8", Ntype_op::Not, 8).n), 8u);
  EXPECT_EQ(ge_weight(one_node("lgdb_ge_shl", "shl8", Ntype_op::SHL, 8).n), 8u);
  EXPECT_EQ(ge_weight(one_node("lgdb_ge_sext", "sext8", Ntype_op::Sext, 8).n), 8u);
}

// Get_mask/Set_mask are bit-select/insert: a gate per output bit, like the rest.
TEST(GeWeight, MaskOpsWeighTheirWidth) {
  EXPECT_EQ(ge_weight(one_node("lgdb_ge_gm", "gm12", Ntype_op::Get_mask, 12).n), 12u);
  EXPECT_EQ(ge_weight(one_node("lgdb_ge_sm", "sm12", Ntype_op::Set_mask, 12).n), 12u);
}

// A register costs a flop per bit -- the same shape as combinational logic, and
// the reason a wide pipeline register is not "one node" to the size window.
TEST(GeWeight, FlopsWeighTheirWidth) {
  EXPECT_EQ(ge_weight(one_node("lgdb_ge_flop", "flop64", Ntype_op::Flop, 64).n), 64u);
  EXPECT_EQ(ge_weight(one_node("lgdb_ge_latch", "latch8", Ntype_op::Latch, 8).n), 8u);
  EXPECT_EQ(ge_weight(one_node("lgdb_ge_fflop", "fflop8", Ntype_op::Fflop, 8).n), 8u);
}

// An array multiplier is quadratic: this is the single biggest reason node
// counts mis-size a datapath region. A 64-bit Mult is 4096 GE, not 1 node.
TEST(GeWeight, MultIsQuadratic) {
  EXPECT_EQ(ge_weight(one_node("lgdb_ge_mult", "mult8", Ntype_op::Mult, 8).n), 64u);
  EXPECT_EQ(ge_weight(one_node("lgdb_ge_mult", "mult64", Ntype_op::Mult, 64).n), 4096u);
}

// Comparators and reduces answer in ONE bit but pay per INPUT bit. Weighing them
// by their driver width would size a 64-bit compare at 1 GE -- exactly the
// undercount the metric exists to prevent.
TEST(GeWeight, ComparatorsWeighTheirOperandNotTheirOneBitOutput) {
  auto& lib = lib_for("lgdb_ge_cmp");
  auto  gio = lib.create_io("cmp");
  gio->add_input("a", 0);
  gio->add_input("b", 1);
  gio->add_output("y", 2);
  auto g = gio->create_graph();

  auto ia = g->get_input_pin("a");
  auto ib = g->get_input_pin("b");
  set_bits(ia, 64);
  set_bits(ib, 8);

  auto eq = create_typed_node(*g, Ntype_op::EQ);
  ia.connect_sink(eq.create_sink_pin(0));
  ib.connect_sink(eq.create_sink_pin(1));
  auto d = eq.create_driver_pin(0);
  set_bits(d, 1);  // a comparator really does drive one bit
  d.connect_sink(g->get_output_pin("y"));

  EXPECT_EQ(ge_weight(eq), 64u) << "the widest operand, not the 1-bit result";

  auto lt = create_typed_node(*g, Ntype_op::LT);
  ia.connect_sink(lt.create_sink_pin(0));
  ib.connect_sink(lt.create_sink_pin(1));
  set_bits(lt.create_driver_pin(0), 1);
  EXPECT_EQ(ge_weight(lt), 64u);

  auto ror = create_typed_node(*g, Ntype_op::Ror);
  ib.connect_sink(ror.create_sink_pin(0));
  set_bits(ror.create_driver_pin(0), 1);
  EXPECT_EQ(ge_weight(ror), 8u) << "reduce pays per input bit";
}

// bits == 0 is what pass.color sees when it runs before pass.bitwidth (and what a
// driver nobody reads always looks like). Degrade to 1 -- never 0, or a region of
// unsized nodes would measure as empty and no window could bound it.
TEST(GeWeight, UnknownWidthDegradesToOne) {
  EXPECT_EQ(ge_weight(one_node("lgdb_ge_u", "u_and", Ntype_op::And, 0).n), 1u);
  EXPECT_EQ(ge_weight(one_node("lgdb_ge_u", "u_sum", Ntype_op::Sum, 0).n), 1u);
  EXPECT_EQ(ge_weight(one_node("lgdb_ge_u", "u_flop", Ntype_op::Flop, 0).n), 1u);
  EXPECT_EQ(ge_weight(one_node("lgdb_ge_u", "u_mult", Ntype_op::Mult, 0).n), 1u)
      << "0*0 would be 0: the quadratic rule must degrade too";
  EXPECT_EQ(ge_weight(one_node("lgdb_ge_u", "u_eq", Ntype_op::EQ, 0).n), 1u);
}

// A Sub is a blackbox: ABC sees its boundary and nothing else, so it weighs its
// DECLARED port bits. Read off the child GraphIO, which carries `bits` whether or
// not the parent wired the port up.
TEST(GeWeight, SubWeighsDeclaredPortBits) {
  auto& lib = lib_for("lgdb_ge_sub");
  auto  cio = lib.create_io("child");
  cio->add_input("x", 0);
  cio->add_input("y", 1);
  cio->add_output("o", 2);
  // NOTE: add_input's second arg is the PORT ID, not the width. Widths go
  // through set_bits -- a fixture that passes bits there silently tests the
  // bits==0 path instead.
  cio->set_bits("x", 8);
  cio->set_bits("y", 16);
  cio->set_bits("o", 32);
  auto cg = cio->create_graph();

  auto pio = lib.create_io("parent");
  pio->add_input("a", 0);
  pio->add_output("z", 1);
  auto pg   = pio->create_graph();
  auto sub  = create_typed_node(*pg, Ntype_op::Sub);
  sub.set_subnode(cio);
  pg->get_input_pin("a").connect_sink(sub.create_sink_pin(0));

  EXPECT_EQ(ge_weight(sub), 8u + 16u + 32u) << "every declared port, connected or not";
}

// A sub whose ports were never sized still has to weigh something.
TEST(GeWeight, UnsizedSubDegradesToOne) {
  auto& lib = lib_for("lgdb_ge_sub0");
  auto  cio = lib.create_io("child0");
  cio->add_input("x", 0);
  cio->add_output("o", 1);
  auto cg = cio->create_graph();

  auto pio = lib.create_io("parent0");
  pio->add_output("z", 0);
  auto pg  = pio->create_graph();
  auto sub = create_typed_node(*pg, Ntype_op::Sub);
  sub.set_subnode(cio);
  EXPECT_EQ(ge_weight(sub), 1u);
}

// Div is bit-blasted as a blackbox too: its cost to a region is its boundary.
// This also pins the fanout dedup -- out_edges() yields one entry per EDGE, so a
// driver read three times must still be counted once.
TEST(GeWeight, DivWeighsPortBitsAndDedupsFanout) {
  auto& lib = lib_for("lgdb_ge_div");
  auto  gio = lib.create_io("d");
  gio->add_input("a", 0);
  gio->add_input("b", 1);
  gio->add_output("y", 2);
  auto g = gio->create_graph();

  auto ia = g->get_input_pin("a");
  auto ib = g->get_input_pin("b");
  set_bits(ia, 32);
  set_bits(ib, 32);

  auto div = create_typed_node(*g, Ntype_op::Div);
  ia.connect_sink(div.create_sink_pin(0));
  ib.connect_sink(div.create_sink_pin(1));
  auto q = div.create_driver_pin(0);
  set_bits(q, 32);
  q.connect_sink(g->get_output_pin("y"));

  EXPECT_EQ(ge_weight(div), 32u + 32u + 32u) << "two 32-bit operands in, 32 bits out";

  // Three readers of the same 32-bit driver: still one port, still 32 bits.
  for (int i = 0; i < 3; ++i) {
    auto sink = create_typed_node(*g, Ntype_op::Not);
    q.connect_sink(sink.create_sink_pin(0));
  }
  EXPECT_EQ(ge_weight(div), 96u) << "fanout must not multiply the port width";
}

// A native Memory (pass.abc memory=false, the default) is a blackbox boundary.
// Its comptime `bits`/`size` config pins are constants, not ports -- counting
// them would charge the region for the width of the literal `256`.
TEST(GeWeight, MemoryWeighsPortsNotComptimeConfig) {
  auto& lib = lib_for("lgdb_ge_mem");
  auto  gio = lib.create_io("m");
  gio->add_input("addr", 0);
  gio->add_output("dout", 1);
  auto g = gio->create_graph();

  auto mem = create_typed_node(*g, Ntype_op::Memory);

  // Memory sink pids are the cell.cpp table (addr 0, bits 1, size 9). Pins must
  // be CREATED here: find_sink_pin only finds an already-wired one, so building
  // a fixture with it connects nothing at all.
  auto cbits = create_const(*g, *Dlop::create_integer(16));
  cbits.connect_sink(mem.create_sink_pin(1));  // bits: comptime config
  auto csize = create_const(*g, *Dlop::create_integer(256));
  csize.connect_sink(mem.create_sink_pin(9));  // size: comptime config

  auto ia = g->get_input_pin("addr");
  set_bits(ia, 8);
  ia.connect_sink(mem.create_sink_pin(0));  // addr: a real port

  auto dout = mem.create_driver_pin(0);
  set_bits(dout, 16);
  dout.connect_sink(g->get_output_pin("dout"));

  EXPECT_EQ(ge_weight(mem), 8u + 16u) << "the addr port and the dout port; not the const config pins";
}

// Constants and graph IO map to no gate at all. (is_partitionable already keeps
// them out of every region, but the metric must not depend on that.)
TEST(GeWeight, ConstantsAndIoWeighNothing) {
  auto& lib = lib_for("lgdb_ge_zero");
  auto  gio = lib.create_io("z");
  gio->add_input("a", 0);
  gio->add_output("y", 1);
  auto g = gio->create_graph();

  auto k = create_typed_node(*g, Ntype_op::Nconst, 32);
  EXPECT_EQ(ge_weight(k), 0u);

  EXPECT_EQ(ge_weight(g->get_input_pin("a").get_master_node()), 0u) << "graph IO is a builtin node";
  EXPECT_EQ(ge_weight(hhds::Node_class{}), 0u) << "an invalid handle weighs nothing";
}

// mappable_ge_weight: what the mapper will BLAST inside this def's region. A
// Sub is a boundary there (its logic is weighed in its own def), so it weighs
// ~1; everything else -- including Memory, which pass.abc can decompose --
// keeps ge_weight. (The size window and its stats use this measure; ge_weight
// keeps the boundary semantics for color_absorb's black-box weigher.)
TEST(GeWeight, MappableWeighsSubAsOne) {
  using livehd::graph_util::mappable_ge_weight;

  auto& lib = lib_for("lgdb_ge_map");
  auto  cio = lib.create_io("map_child");
  cio->add_input("x", 0);
  cio->add_output("o", 1);
  cio->set_bits("x", 64);
  cio->set_bits("o", 64);
  auto cg = cio->create_graph();
  (void)cg;

  auto pio = lib.create_io("map_parent");
  pio->add_input("a", 0);
  pio->add_output("z", 1);
  auto pg  = pio->create_graph();
  auto sub = create_typed_node(*pg, Ntype_op::Sub);
  sub.set_subnode(cio);
  ASSERT_EQ(ge_weight(sub), 128u);
  EXPECT_EQ(mappable_ge_weight(sub), 1u);

  auto n = one_node("lgdb_ge_map2", "and32m", Ntype_op::And, 32);
  EXPECT_EQ(mappable_ge_weight(n.n), 32u) << "plain logic keeps its ge_weight";
}
