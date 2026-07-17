// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// The region digest that keys the incremental-synthesis cache (2opt-incr A).
// The property under test is the CACHE KEY CONTRACT: equal digests must mean
// "the same mapping problem" (a collision reuses the wrong netlist -- a
// miscompile), and digests must be a function of the region's CONTENT alone --
// never of nids, construction order, or wire spellings, all of which shift
// under the small RTL edits the cache exists to survive. Where the digest
// cannot canonicalize it must refuse (valid=false), never guess.

#include "abc_incr.hpp"

#include <string>
#include <vector>

#include "cell.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/attrs/name.hpp"
#include "hhds/graph.hpp"
#include "node_util.hpp"

using livehd::abc::region_digest;
using livehd::abc::Region_digest;
using livehd::graph_util::create_typed_node;
using livehd::graph_util::set_bits;
using livehd::partition::Region_body;

namespace {

// A tiny two-cone region: y0 = (a OP0 b), y1 = flop(y0 OP1 c). Parameterized
// so tests can permute construction order and operand order without changing
// the function. Everything lives in ONE def; the region is the whole body.
struct Fixture {
  std::shared_ptr<hhds::Graph>  g;
  std::vector<hhds::Node_class> nodes;    // region members, forward order
  Region_body                   rb;       // body left null: digesting reads src only
  std::vector<Region_body::Port> in, out; // storage rb points at
};

// `swap_ops`: connect b before a (same sink port class -- the commutative
// case). `flop_name`: empty = anonymous flop.
Fixture make_region(const char* dir, const char* name, bool swap_ops, const char* flop_name, int bits) {
  Fixture f;
  auto&   lib = livehd::Hhds_graph_library::instance(dir);
  auto    gio = lib.create_io(name);
  gio->add_input("a", 0);
  gio->add_input("b", 1);
  gio->add_input("c", 2);
  gio->add_output("y", 3);
  auto g = gio->create_graph();
  f.g    = g;

  auto ia = g->get_input_pin("a");
  auto ib = g->get_input_pin("b");
  auto ic = g->get_input_pin("c");
  set_bits(ia, bits);
  set_bits(ib, bits);
  set_bits(ic, bits);

  auto x = create_typed_node(*g, Ntype_op::Xor);
  if (swap_ops) {
    ib.connect_sink(x.create_sink_pin(0));
    ia.connect_sink(x.create_sink_pin(0));
  } else {
    ia.connect_sink(x.create_sink_pin(0));
    ib.connect_sink(x.create_sink_pin(0));
  }
  auto xd = x.create_driver_pin(0);
  set_bits(xd, bits);

  auto an = create_typed_node(*g, Ntype_op::And);
  xd.connect_sink(an.create_sink_pin(0));
  ic.connect_sink(an.create_sink_pin(0));
  auto ad = an.create_driver_pin(0);
  set_bits(ad, bits);

  auto fl = create_typed_node(*g, Ntype_op::Flop);
  ad.connect_sink(fl.create_sink_pin(3));  // din
  if (flop_name[0] != '\0') {
    fl.attr(hhds::attrs::name).set(std::string{flop_name});
  }
  auto q = fl.create_driver_pin(0);
  set_bits(q, bits);
  q.connect_sink(g->get_output_pin("y"));

  // Region membership in forward order (loop breaks first, like the
  // partitioner's collect()).
  f.nodes = {fl, x, an};

  f.in.push_back({.name = "a", .src_driver = ia, .bits = bits, .sign = false});
  f.in.push_back({.name = "b", .src_driver = ib, .bits = bits, .sign = false});
  f.in.push_back({.name = "c", .src_driver = ic, .bits = bits, .sign = false});
  f.out.push_back({.name = "y", .src_driver = q, .bits = bits, .sign = false});

  f.rb.body        = nullptr;
  f.rb.src         = g.get();
  f.rb.color       = 1;
  f.rb.module_name = name;
  f.rb.inputs      = f.in;
  f.rb.outputs     = f.out;
  f.rb.nodes       = std::span<const hhds::Node_class>(f.nodes.data(), f.nodes.size());
  return f;
}

}  // namespace

// The same function built twice -- different library, different nids -- must
// digest identically: this is the whole cache-across-recompiles premise.
TEST(AbcIncr, SameLogicSameDigestAcrossConstructions) {
  auto f1 = make_region("lgdb_incr_a1", "r", false, "st", 8);
  auto f2 = make_region("lgdb_incr_a2", "r", false, "st", 8);
  // Perturb nid allocation in the second library before building: extra defs
  // shift every nid, which is exactly what an unrelated source edit does.
  auto f3 = [] {
    auto& lib = livehd::Hhds_graph_library::instance("lgdb_incr_a3");
    for (int i = 0; i < 3; ++i) {
      auto io = lib.create_io(std::string{"pad"} + std::to_string(i));
      io->add_output("z", 0);
      auto g = io->create_graph();
      (void)create_typed_node(*g, Ntype_op::Not);
    }
    return make_region("lgdb_incr_a3", "r", false, "st", 8);
  }();

  auto d1 = region_digest(f1.rb, 42);
  auto d2 = region_digest(f2.rb, 42);
  auto d3 = region_digest(f3.rb, 42);
  ASSERT_TRUE(d1.valid);
  EXPECT_EQ(d1.hex(), d2.hex());
  EXPECT_EQ(d1.hex(), d3.hex()) << "nid shifts must not reach the digest";
}

// a^b == b^a on one sink-port class: the fold_operands commutative rule. The
// LiveSynth lineage's positional diff reported this as "changed" and oversized
// every region it touched.
TEST(AbcIncr, CommutativeOperandSwapIsTheSameRegion) {
  auto fwd  = make_region("lgdb_incr_b1", "r", false, "st", 8);
  auto swp  = make_region("lgdb_incr_b2", "r", true, "st", 8);
  auto dfwd = region_digest(fwd.rb, 42);
  auto dswp = region_digest(swp.rb, 42);
  ASSERT_TRUE(dfwd.valid);
  ASSERT_TRUE(dswp.valid);
  EXPECT_EQ(dfwd.hex(), dswp.hex());
}

// Real differences must always split the key: logic width, and the resolved
// ABC recipe (same logic mapped with a different flow is a different netlist).
TEST(AbcIncr, WidthAndRecipeSplitTheKey) {
  auto w8  = make_region("lgdb_incr_c1", "r", false, "st", 8);
  auto w16 = make_region("lgdb_incr_c2", "r", false, "st", 16);
  auto d8  = region_digest(w8.rb, 42);
  auto d16 = region_digest(w16.rb, 42);
  ASSERT_TRUE(d8.valid);
  ASSERT_TRUE(d16.valid);
  EXPECT_NE(d8.hex(), d16.hex());

  auto recipe = region_digest(w8.rb, 43);
  ASSERT_TRUE(recipe.valid);
  EXPECT_NE(d8.hex(), recipe.hex()) << "opts_sig must salt the digest";
}

// A renamed flop is a DIFFERENT mapping product: read-back rebuilds the
// register under its original name and LEC pairs flops by name. And an
// anonymous flop has no identity an edit preserves -- refuse, never guess
// (the semdiff rule).
TEST(AbcIncr, StateNamesAreLoadBearing) {
  auto n1 = make_region("lgdb_incr_d1", "r", false, "st_a", 8);
  auto n2 = make_region("lgdb_incr_d2", "r", false, "st_b", 8);
  auto d1 = region_digest(n1.rb, 42);
  auto d2 = region_digest(n2.rb, 42);
  ASSERT_TRUE(d1.valid);
  ASSERT_TRUE(d2.valid);
  EXPECT_NE(d1.hex(), d2.hex());

  auto anon = make_region("lgdb_incr_d3", "r", false, "", 8);
  EXPECT_FALSE(region_digest(anon.rb, 42).valid) << "anonymous state must refuse to cache";
}

// The canonical boundary ranks: same content => the same port is at the same
// rank on both sides, which is what lets a reuse stitch cached ports onto
// freshly named ones. a/b/c have distinct roles here, so all ranks resolve.
TEST(AbcIncr, BoundaryRanksAreContentDerived) {
  auto f1 = make_region("lgdb_incr_e1", "r", false, "st", 8);
  auto f2 = make_region("lgdb_incr_e2", "r", false, "st", 8);
  auto d1 = region_digest(f1.rb, 42);
  auto d2 = region_digest(f2.rb, 42);
  ASSERT_TRUE(d1.valid);
  ASSERT_TRUE(d2.valid);
  ASSERT_EQ(d1.in_by_rank.size(), 3u);
  for (size_t r = 0; r < 3; ++r) {
    EXPECT_EQ(f1.rb.inputs[d1.in_by_rank[r]].name, f2.rb.inputs[d2.in_by_rank[r]].name)
        << "rank " << r << " must denote the same boundary role on both sides";
  }
}

// Two boundary inputs the region uses INTERCHANGEABLY (same port of the same
// gate, same widths) cannot be told apart by any structural refinement; a
// guessed stitch could swap them. That is a refusal, not a coin flip. (Here
// they truly commute, but proving that in general is graph canonization --
// the cache declines the whole class.)
TEST(AbcIncr, SymmetricBoundaryRefusesToCache) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_incr_f");
  auto  gio = lib.create_io("sym");
  gio->add_input("a", 0);
  gio->add_input("b", 1);
  gio->add_output("y", 2);
  auto g  = gio->create_graph();
  auto ia = g->get_input_pin("a");
  auto ib = g->get_input_pin("b");
  set_bits(ia, 8);
  set_bits(ib, 8);
  auto an = create_typed_node(*g, Ntype_op::And);
  ia.connect_sink(an.create_sink_pin(0));
  ib.connect_sink(an.create_sink_pin(0));
  auto ad = an.create_driver_pin(0);
  set_bits(ad, 8);
  ad.connect_sink(g->get_output_pin("y"));

  Fixture f;
  f.g     = g;
  f.nodes = {an};
  f.in.push_back({.name = "a", .src_driver = ia, .bits = 8, .sign = false});
  f.in.push_back({.name = "b", .src_driver = ib, .bits = 8, .sign = false});
  f.out.push_back({.name = "y", .src_driver = ad, .bits = 8, .sign = false});
  f.rb.src     = g.get();
  f.rb.inputs  = f.in;
  f.rb.outputs = f.out;
  f.rb.nodes   = std::span<const hhds::Node_class>(f.nodes.data(), f.nodes.size());

  // NOTE: primary graph inputs are name-anchored, so a/b ARE distinguishable
  // here and the digest stays valid. The refusal case is two anonymous
  // CROSS-REGION wires -- simulate by dropping the name anchor: two mid-cone
  // pins of one external driver node.
  auto d = region_digest(f.rb, 42);
  EXPECT_TRUE(d.valid) << "named primary inputs are never ambiguous";

  // The genuinely ambiguous shape: one external NODE with two identically
  // shaped driver pins feeding the same gate port. Ranks cannot bind ports.
  auto  gio2 = lib.create_io("sym2");
  gio2->add_input("seed", 0);
  gio2->add_output("y", 1);
  auto g2 = gio2->create_graph();
  auto is = g2->get_input_pin("seed");
  set_bits(is, 8);
  auto ext = create_typed_node(*g2, Ntype_op::Not);  // stays OUTSIDE the region
  is.connect_sink(ext.create_sink_pin(0));
  auto e0 = ext.create_driver_pin(0);
  auto e1 = ext.create_driver_pin(1);
  set_bits(e0, 8);
  set_bits(e1, 8);
  auto an2 = create_typed_node(*g2, Ntype_op::And);
  e0.connect_sink(an2.create_sink_pin(0));
  e1.connect_sink(an2.create_sink_pin(0));
  auto ad2 = an2.create_driver_pin(0);
  set_bits(ad2, 8);
  ad2.connect_sink(g2->get_output_pin("y"));

  Fixture f2;
  f2.g     = g2;
  f2.nodes = {an2};
  f2.in.push_back({.name = "p0", .src_driver = e0, .bits = 8, .sign = false});
  f2.in.push_back({.name = "p1", .src_driver = e1, .bits = 8, .sign = false});
  f2.out.push_back({.name = "y", .src_driver = ad2, .bits = 8, .sign = false});
  f2.rb.src     = g2.get();
  f2.rb.inputs  = f2.in;
  f2.rb.outputs = f2.out;
  f2.rb.nodes   = std::span<const hhds::Node_class>(f2.nodes.data(), f2.nodes.size());

  EXPECT_FALSE(region_digest(f2.rb, 42).valid) << "indistinguishable boundary pins must refuse";
}

// Changing a constant changes the region: consts are anchored by VALUE.
TEST(AbcIncr, ConstantValueIsInTheKey) {
  auto build = [](const char* dir, int64_t k) {
    auto& lib = livehd::Hhds_graph_library::instance(dir);
    auto  gio = lib.create_io("kreg");
    gio->add_input("a", 0);
    gio->add_output("y", 1);
    auto g  = gio->create_graph();
    auto ia = g->get_input_pin("a");
    set_bits(ia, 8);
    auto an = create_typed_node(*g, Ntype_op::And);
    ia.connect_sink(an.create_sink_pin(0));
    livehd::graph_util::create_const(*g, *Dlop::create_integer(k)).connect_sink(an.create_sink_pin(0));
    auto ad = an.create_driver_pin(0);
    set_bits(ad, 8);
    ad.connect_sink(g->get_output_pin("y"));

    Fixture f;
    f.g     = g;
    f.nodes = {an};
    f.in.push_back({.name = "a", .src_driver = ia, .bits = 8, .sign = false});
    f.out.push_back({.name = "y", .src_driver = ad, .bits = 8, .sign = false});
    f.rb.src     = g.get();
    f.rb.inputs  = f.in;
    f.rb.outputs = f.out;
    f.rb.nodes   = std::span<const hhds::Node_class>(f.nodes.data(), f.nodes.size());
    return std::pair{std::move(f), Region_digest{}};
  };
  auto [fa, unused_a] = build("lgdb_incr_g1", 0x0f);
  auto [fb, unused_b] = build("lgdb_incr_g2", 0xf0);
  auto da = region_digest(fa.rb, 42);
  auto db = region_digest(fb.rb, 42);
  ASSERT_TRUE(da.valid);
  ASSERT_TRUE(db.valid);
  EXPECT_NE(da.hex(), db.hex());
}
