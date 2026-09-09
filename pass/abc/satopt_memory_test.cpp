// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "satopt_memory.hpp"

#include "gtest/gtest.h"
#include "node_util.hpp"
#include "prove.hpp"
#include "query.hpp"
namespace gu = livehd::graph_util;
namespace {
struct Fixture {
  hhds::GraphLibrary           lib;
  std::shared_ptr<hhds::Graph> g;
  hhds::Node_class             mem;
  hhds::Pin_class              c(int v) { return gu::create_const(*g, *Dlop::create_integer(v)); }
  hhds::Pin_class              input(std::string_view name, int width) {
    auto p = g->get_input_pin(name);
    gu::set_ubits(p, width);
    return p;
  }
  void wire(int port, int field, hhds::Pin_class p) { p.connect_sink(mem.create_sink_pin(port * 16 + field)); }
  Fixture(std::string_view name, bool exclusive = true) {
    auto io  = lib.create_io(name);
    int  pid = 1;
    for (const auto& n : {"clk", "sel", "a", "b", "r", "d0", "d1"}) {
      io->add_input(n, pid++);
      io->set_bits(n, n == std::string_view("clk") || n == std::string_view("sel") ? 1 : 8);
    }
    io->add_output("y", pid);
    io->set_bits("y", 8);
    g        = io->create_graph();
    auto clk = input("clk", 1), s = input("sel", 1);
    auto inv = gu::create_typed_node(*g, Ntype_op::Not);
    s.connect_sink(inv.create_sink_pin(0));
    auto ns = inv.create_driver_pin(0);
    gu::set_ubits(ns, 1);
    mem = gu::create_typed_node(*g, Ntype_op::Memory);
    mem.attr(hhds::attrs::name).set("m");
    wire(0, 1, c(8));
    wire(0, 9, c(16));
    wire(0, 8, c(1));
    wire(0, 7, c(0));
    wire(0, 6, c(1));
    wire(0, 0, input("a", 8));
    wire(0, 3, input("d0", 8));
    wire(0, 4, s);
    wire(0, 2, clk);
    wire(0, 10, c(0));
    wire(1, 0, input("b", 8));
    wire(1, 3, input("d1", 8));
    wire(1, 4, exclusive ? ns : s);
    wire(1, 2, clk);
    wire(1, 10, c(0));
    wire(2, 0, input("r", 8));
    wire(2, 4, c(1));
    wire(2, 10, c(1));
    auto y = mem.create_driver_pin(2);
    gu::set_ubits(y, 8);
    y.connect_sink(g->get_output_pin("y"));
  }
};
}  // namespace
TEST(SatoptMemory, ExclusiveWritesMerge) {
  Fixture f("satopt_mem_exclusive");
  auto    r = livehd::abc::optimize_memories({f.g});
  EXPECT_EQ(r.merged_writes, 1);
  for (const auto n : f.g->body().nodes()) {
    if (gu::type_op_of(n) == Ntype_op::Memory) {
      int wr = 0;
      for (const auto& e : n.inp_edges()) {
        if (e.sink.get_port_id() % 16 == 3) {
          ++wr;
        }
      }
      EXPECT_EQ(wr, 1);
    }
  }
}
TEST(SatoptMemory, PossibleCollisionKeepsPriority) {
  Fixture f("satopt_mem_collision", false);
  auto    r = livehd::abc::optimize_memories({f.g});
  EXPECT_EQ(r.merged_writes, 0);
  for (const auto& [name, pairs] : r.independent) {
    (void)name;
    EXPECT_TRUE(pairs.empty());
  }
}
TEST(SatoptMemory, DistinctAddressesClearOnlyUnreachableCollision) {
  Fixture f("satopt_mem_fwd", false);
  for (int block : {0, 1, 2}) {
    for (const auto& e : f.mem.create_sink_pin(block * 16).inp_edges()) {
      e.del_edge();
    }
    f.wire(block, 0, f.c(block == 2 ? 3 : block));
  }
  f.wire(0, 5, f.c(3));
  f.wire(0, 15, f.c(3));
  auto r = livehd::abc::optimize_memories({f.g});
  EXPECT_EQ(r.collision_bits, 4);
  EXPECT_EQ(r.address_bits, 1);
  for (const auto& [name, pairs] : r.independent) {
    (void)name;
    EXPECT_TRUE(pairs.contains({0, 1}));
  }
}
TEST(SatoptMemory, MemoryDoutsAreIndependentFreeWords) {
  Fixture                f("satopt_mem_free");
  livehd::formal::Prover normal(f.g.get());
  EXPECT_EQ(normal.is_false(f.mem.create_driver_pin(2)).verdict, livehd::formal::Verdict::Unknown);
  livehd::formal::Prover proof(f.g.get(), {.memory_as_symbols = true, .reject_unknown_constants = true});
  EXPECT_EQ(proof.is_false(f.mem.create_driver_pin(2)).verdict, livehd::formal::Verdict::Refuted);
}

TEST(SatoptMemory, ExclusiveWriteRewriteIsEquivalent) {
  Fixture            f("satopt_mem_lec");
  hhds::GraphLibrary copy;
  ASSERT_TRUE(copy.copy_from(f.lib, f.g->get_name()));
  auto impl   = copy.find_io(f.g->get_name())->get_graph();
  auto result = livehd::abc::optimize_memories({impl});
  ASSERT_EQ(result.merged_writes, 1);
  livehd::lec::Lec_options opts;
  opts.engine      = "ind";
  opts.timeout     = 20;
  opts.min_timeout = 1;
  auto proof       = livehd::lec::prove_equal(f.g.get(), impl.get(), opts);
  EXPECT_EQ(proof.verdict, livehd::lec::Verdict::Proven) << proof.detail << " " << proof.witness;
}

static void exclusive_reads(bool sync) {
  Fixture f(sync ? "satopt_mem_sync_read_lec" : "satopt_mem_read_lec", false);
  if (sync) {
    for (const auto& e : f.mem.create_sink_pin(7).inp_edges()) {
      e.del_edge();
    }
    f.wire(0, 7, f.c(1));
  }
  f.g->get_io()->add_input("r2", 20);
  f.g->get_io()->set_bits("r2", 8);
  f.g->get_io()->add_output("y2", 21);
  f.g->get_io()->set_bits("y2", 8);
  auto sel = f.input("sel", 1);
  auto inv = gu::create_typed_node(*f.g, Ntype_op::Not);
  sel.connect_sink(inv.create_sink_pin(0));
  auto other = inv.create_driver_pin(0);
  gu::set_ubits(other, 1);
  for (const auto& e : f.mem.create_sink_pin(2 * 16 + 4).inp_edges()) {
    e.del_edge();
  }
  f.wire(2, 4, sel);
  f.wire(3, 0, f.input("r2", 8));
  f.wire(3, 4, other);
  f.wire(3, 10, f.c(1));
  auto output = f.mem.create_driver_pin(3);
  gu::set_ubits(output, 8);
  output.connect_sink(f.g->get_output_pin("y2"));
  hhds::GraphLibrary copy;
  ASSERT_TRUE(copy.copy_from(f.lib, f.g->get_name()));
  auto impl   = copy.find_io(f.g->get_name())->get_graph();
  auto result = livehd::abc::optimize_memories({impl});
  ASSERT_EQ(result.merged_reads, 1);
  livehd::lec::Lec_options opts;
  opts.engine      = "ind";
  opts.timeout     = 20;
  opts.min_timeout = 1;
  auto proof       = livehd::lec::prove_equal(f.g.get(), impl.get(), opts);
  EXPECT_EQ(proof.verdict, livehd::lec::Verdict::Proven) << proof.detail << " " << proof.witness;
}

TEST(SatoptMemory, MutuallyExclusiveReadsAreEquivalent) { exclusive_reads(false); }
TEST(SatoptMemory, MutuallyExclusiveSyncReadsAreEquivalent) { exclusive_reads(true); }

TEST(SatoptMemory, DisabledReadForwardingRemainsObservable) {
  Fixture f("satopt_mem_disabled_forward", false);
  for (const auto& e : f.mem.create_sink_pin(2 * 16 + 4).inp_edges()) {
    e.del_edge();
  }
  f.wire(2, 4, f.c(0));
  f.wire(0, 5, f.c(3));
  auto result = livehd::abc::optimize_memories({f.g});
  EXPECT_EQ(result.dead_ports, 0);
  EXPECT_EQ(result.collision_bits, 0);
}

TEST(SatoptMemory, AddressProofUsesUnsignedTruncation) {
  Fixture f("satopt_address_width", false);
  auto    narrow = f.input("sel", 1);
  gu::set_sbits(narrow, 1);
  livehd::formal::Prover proof(f.g.get());
  // Signed one-bit -1 still denotes memory address 1 after zero extension.
  EXPECT_EQ(proof.never_collide(narrow, f.c(1), {}, 4).verdict, livehd::formal::Verdict::Refuted);
  // Differing high bits cannot justify dropping priority in a 16-entry array.
  EXPECT_EQ(proof.never_collide(f.c(1), f.c(17), {}, 4).verdict, livehd::formal::Verdict::Refuted);
}

TEST(SatoptMemory, NarrowWriteDataIsZeroExtended) {
  Fixture f("satopt_mem_narrow_lec");
  for (const auto& e : f.mem.create_sink_pin(3).inp_edges()) {
    e.del_edge();
  }
  f.wire(0, 3, f.c(3));
  hhds::GraphLibrary copy;
  ASSERT_TRUE(copy.copy_from(f.lib, f.g->get_name()));
  auto impl   = copy.find_io(f.g->get_name())->get_graph();
  auto result = livehd::abc::optimize_memories({impl});
  ASSERT_EQ(result.merged_writes, 1);
  livehd::lec::Lec_options opts;
  opts.engine      = "ind";
  opts.timeout     = 20;
  opts.min_timeout = 1;
  auto proof       = livehd::lec::prove_equal(f.g.get(), impl.get(), opts);
  EXPECT_EQ(proof.verdict, livehd::lec::Verdict::Proven) << proof.detail << " " << proof.witness;
}
