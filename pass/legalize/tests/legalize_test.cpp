// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Fidelity of the pass.legalize rebuild.
//
// The contract is exact: a rebuilt def is the SAME design, so
// `semdiff::structural_identical(src, rebuilt)` must hold. That predicate is a
// total-bijection isomorphism check -- every node paired, matching op, width,
// Sub interface, and the full input-edge multiset under the bijection -- so it
// catches a dropped node, a mis-wired edge, a lost width and a swapped operand
// alike. It is the same gate pass/lec trusts for its no-solver skip.

#include "legalize.hpp"

#include <string>
#include <vector>

#include "attrs.hpp"
#include "cell.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "loop_split.hpp"
#include "node_util.hpp"
#include "query.hpp"
#include "semdiff.hpp"

namespace gu = livehd::graph_util;
namespace la = livehd::attrs;

namespace {

// A rebuild preserves every name, so internal state cells are anchored by name.
// Without this semdiff leaves an internal flop with no forward signature (it is
// a seeded cut point, not a folded cone), which reads as `cut_unknown` and an
// unmatched node -- for two byte-identical designs as much as for a rebuild.
livehd::semdiff::Semdiff_options match_opts() {
  livehd::semdiff::Semdiff_options o;
  o.matching_names = true;
  return o;
}

// `out = (a & b) | (a ^ c)` over 8-bit inputs, plus a flop on the result so the
// body has a loop-last node whose fan-in is created after it.
std::shared_ptr<hhds::Graph> build_design(hhds::GraphLibrary& lib, const std::string& name) {
  auto gio = lib.create_io(name);
  gio->add_input("a", 1);
  gio->add_input("b", 2);
  gio->add_input("c", 3);
  gio->add_input("clk", 4);
  gio->add_output("o", 5);
  for (const auto* p : {"a", "b", "c"}) {
    gio->set_bits(p, 8);
    gio->set_unsign(p, true);
  }
  gio->set_bits("clk", 1);
  gio->set_bits("o", 8);
  auto g = gio->create_graph();

  auto in  = g->get_input_node();
  auto out = g->get_output_node();
  auto pa  = in.create_driver_pin(1);
  auto pb  = in.create_driver_pin(2);
  auto pc  = in.create_driver_pin(3);
  auto clk = in.create_driver_pin(4);
  for (const auto& p : {pa, pb, pc}) {
    gu::set_ubits(p, 8);
  }
  gu::set_ubits(clk, 1);

  auto and_n = gu::create_typed_node(*g, Ntype_op::And, 8);
  pa.connect_sink(gu::setup_sink_by_name(and_n, "as"));
  pb.connect_sink(gu::setup_sink_by_name(and_n, "as"));

  auto xor_n = gu::create_typed_node(*g, Ntype_op::Xor, 8);
  pa.connect_sink(gu::setup_sink_by_name(xor_n, "as"));
  pc.connect_sink(gu::setup_sink_by_name(xor_n, "as"));

  auto or_n = gu::create_typed_node(*g, Ntype_op::Or, 8);
  and_n.create_driver_pin(0).connect_sink(gu::setup_sink_by_name(or_n, "as"));
  xor_n.create_driver_pin(0).connect_sink(gu::setup_sink_by_name(or_n, "as"));

  auto flop = gu::create_typed_node(*g, Ntype_op::Flop, 8);
  // Named on purpose: semdiff anchors STATE cells by name, so an unnamed flop is
  // a degenerate pairing case that says nothing about the rebuild.
  flop.attr(hhds::attrs::name).set(std::string{"acc"});
  or_n.create_driver_pin(0).connect_sink(gu::setup_sink_by_name(flop, "din"));
  clk.connect_sink(gu::setup_sink_by_name(flop, "clock_pin"));
  flop.create_driver_pin(0).connect_sink(out.create_sink_pin(5));
  return g;
}

std::size_t live_nodes(hhds::Graph* g) {
  std::size_t n = 0;
  for (auto node : g->body().nodes(hhds::Node_order::forward)) {
    if (!node.is_invalid() && !gu::is_builtin_node(node)) {
      ++n;
    }
  }
  return n;
}

}  // namespace

TEST(Legalize, RebuiltDefIsStructurallyIdentical) {
  auto& src_lib = livehd::Hhds_graph_library::instance("lgdb_legalize_src");
  auto& dst_lib = livehd::Hhds_graph_library::instance("lgdb_legalize_dst");
  auto  src     = build_design(src_lib, "ident");

  auto dst_gio = livehd::legalize::clone_io_decls(src.get(), dst_lib);
  ASSERT_TRUE(dst_gio);
  auto dst = livehd::legalize::rebuild_def(src.get(), dst_gio);
  ASSERT_TRUE(dst);

  EXPECT_EQ(live_nodes(dst.get()), live_nodes(src.get()));
  EXPECT_TRUE(livehd::semdiff::structural_identical(src.get(), dst.get(), match_opts()))
      << "a rebuilt def must be a total-bijection isomorphism of its source";
}

// CONTROL: two independently built copies of the SAME design. If this fails,
// the harness/options are wrong, not the rebuild.
TEST(Legalize, ControlTwoIdenticalBuildsMatch) {
  auto& l1 = livehd::Hhds_graph_library::instance("lgdb_legalize_ctl1");
  auto& l2 = livehd::Hhds_graph_library::instance("lgdb_legalize_ctl2");
  auto  a  = build_design(l1, "ctl");
  auto  b  = build_design(l2, "ctl");
  EXPECT_TRUE(livehd::semdiff::structural_identical(a.get(), b.get(), match_opts()))
      << "the harness itself is wrong if two identical builds do not match";
}

TEST(Legalize, RebuildCompactsAwayTombstones) {
  auto& src_lib = livehd::Hhds_graph_library::instance("lgdb_legalize_tomb_src");
  auto& dst_lib = livehd::Hhds_graph_library::instance("lgdb_legalize_tomb_dst");
  auto  src     = build_design(src_lib, "tomb");

  // Leave the shape of an in-place pass behind: dead nodes created and deleted,
  // which is exactly the tombstone litter tolg + cprop hand downstream.
  const auto before = live_nodes(src.get());
  for (int i = 0; i < 32; ++i) {
    auto dead = gu::create_typed_node(*src, Ntype_op::Or, 8);
    dead.del_node();
  }
  EXPECT_EQ(live_nodes(src.get()), before) << "deleted nodes must not count as live";

  auto dst_gio = livehd::legalize::clone_io_decls(src.get(), dst_lib);
  ASSERT_TRUE(dst_gio);
  auto dst = livehd::legalize::rebuild_def(src.get(), dst_gio);
  ASSERT_TRUE(dst);

  EXPECT_EQ(live_nodes(dst.get()), before);
  EXPECT_TRUE(livehd::semdiff::structural_identical(src.get(), dst.get(), match_opts()));
}

TEST(Legalize, IoDeclarationsRoundTrip) {
  auto& src_lib = livehd::Hhds_graph_library::instance("lgdb_legalize_io_src");
  auto& dst_lib = livehd::Hhds_graph_library::instance("lgdb_legalize_io_dst");
  auto  src     = build_design(src_lib, "iodecl");

  auto dst_gio = livehd::legalize::clone_io_decls(src.get(), dst_lib);
  ASSERT_TRUE(dst_gio);
  auto src_io = src->get_io();

  ASSERT_EQ(dst_gio->get_input_pin_decls().size(), src_io->get_input_pin_decls().size());
  for (size_t i = 0; i < src_io->get_input_pin_decls().size(); ++i) {
    const auto& s = src_io->get_input_pin_decls()[i];
    const auto& d = dst_gio->get_input_pin_decls()[i];
    EXPECT_EQ(d.name, s.name);
    EXPECT_EQ(d.port_id, s.port_id) << "port ids index Sub pins; renumbering re-binds every caller";
    EXPECT_EQ(d.bits, s.bits);
    EXPECT_EQ(d.unsign, s.unsign);
    EXPECT_EQ(d.loop_break, s.loop_break);
  }
  ASSERT_EQ(dst_gio->get_output_pin_decls().size(), src_io->get_output_pin_decls().size());
  EXPECT_EQ(dst_gio->get_output_pin_decls()[0].name, src_io->get_output_pin_decls()[0].name);
  EXPECT_EQ(dst_gio->get_output_pin_decls()[0].port_id, src_io->get_output_pin_decls()[0].port_id);
}

// ---------------------------------------------------------------------------
// Frozen graphs. The point of the check is the DISTINCTION: a pass recording
// what it learned (an attribute) is legal and must not trip it; a pass changing
// the design (a node, an edge, a width, a type) is exactly what it exists to
// catch.
// ---------------------------------------------------------------------------

TEST(LegalizeFreeze, AttributeWritesAreLegal) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_legalize_freeze_attr");
  auto  g   = build_design(lib, "attr_ok");
  livehd::legalize::freeze(g.get());

  // Everything a downstream pass legitimately records.
  for (auto n : g->body().nodes(hhds::Node_order::forward)) {
    if (n.is_invalid() || gu::is_builtin_node(n)) {
      continue;
    }
    n.attr(la::color).set(int32_t{3});
    n.attr(la::proven).set(uint32_t{1});
    n.attr(la::place).set(Ann_place{0.0F, 0.0F, 1.0F, 1.0F});
    gu::set_match(n.create_driver_pin(0), 77);
  }
  EXPECT_TRUE(livehd::legalize::verify_frozen(g.get(), "pass.hypothetical"))
      << "recording an attribute is how a pass reports what it learned; it must stay legal";
}

TEST(LegalizeFreeze, AddingANodeIsCaught) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_legalize_freeze_add");
  auto  g   = build_design(lib, "add_bad");
  livehd::legalize::freeze(g.get());
  ASSERT_TRUE(livehd::legalize::verify_frozen(g.get(), "control"));

  auto extra = gu::create_typed_node(*g, Ntype_op::Or, 8);
  g->get_input_node().create_driver_pin(1).connect_sink(gu::setup_sink_by_name(extra, "as"));
  extra.create_driver_pin(0).connect_sink(g->get_output_node().create_sink_pin(5));

  EXPECT_FALSE(livehd::legalize::verify_frozen(g.get(), "pass.rogue"));
}

TEST(LegalizeFreeze, RewiringIsCaught) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_legalize_freeze_wire");
  auto  g   = build_design(lib, "wire_bad");
  livehd::legalize::freeze(g.get());

  // Same node count, same ops, same widths -- only the edge relation moves. A
  // node-count check would miss this; the digest folds the full edge relation.
  auto in = g->get_input_node();
  for (auto n : g->body().nodes(hhds::Node_order::forward)) {
    if (!n.is_invalid() && gu::type_op_of(n) == Ntype_op::Xor) {
      in.create_driver_pin(2).connect_sink(gu::setup_sink_by_name(n, "as"));
      break;
    }
  }
  EXPECT_FALSE(livehd::legalize::verify_frozen(g.get(), "pass.rewire"));
}

TEST(LegalizeFreeze, AnUnfrozenGraphIsNotClaimed) {
  auto& lib   = livehd::Hhds_graph_library::instance("lgdb_legalize_freeze_none");
  auto  g     = build_design(lib, "never_frozen");
  // Never frozen: the checker claims nothing about it, so mutating is not an error.
  auto  extra = gu::create_typed_node(*g, Ntype_op::Or, 8);
  (void)extra;
  EXPECT_TRUE(livehd::legalize::verify_frozen(g.get(), "pass.whoever"));
}

TEST(LegalizeFreeze, RebuildOutputCanBeFrozen) {
  auto& src_lib = livehd::Hhds_graph_library::instance("lgdb_legalize_freeze_rb_src");
  auto& dst_lib = livehd::Hhds_graph_library::instance("lgdb_legalize_freeze_rb_dst");
  auto  src     = build_design(src_lib, "rb");

  auto dst_gio = livehd::legalize::clone_io_decls(src.get(), dst_lib);
  ASSERT_TRUE(dst_gio);
  auto dst = livehd::legalize::rebuild_def(src.get(), dst_gio);
  ASSERT_TRUE(dst);

  const auto before = livehd::legalize::frozen_count();
  livehd::legalize::freeze(dst.get());
  EXPECT_EQ(livehd::legalize::frozen_count(), before + 1) << "the rebuild's output must be digestable";
  EXPECT_TRUE(livehd::legalize::verify_frozen(dst.get(), "pass.downstream"));
}

// ---------------------------------------------------------------------------
// Loop split. The only claim that matters is SEMANTIC: the two-loop form must
// compute what the one-loop form computed. Structure deliberately differs, so
// semdiff cannot check it -- this is an LEC obligation, discharged by the same
// engine `lhd lec` uses.
// ---------------------------------------------------------------------------

namespace {

constexpr hhds::Port_id kLIdx  = 1;
constexpr hhds::Port_id kLInv  = 2;
constexpr hhds::Port_id kLPin  = 3;  // parallel carry in / out
constexpr hhds::Port_id kLPout = 4;
constexpr hhds::Port_id kLIin  = 5;  // induction carry in / out
constexpr hhds::Port_id kLIout = 6;

// Body with TWO independent carries: a per-lane slice write (parallel) and a
// running sum (recurrence). They share no logic, which is the v1 split case.
std::shared_ptr<hhds::Graph> two_carry_body(hhds::GraphLibrary& lib, const std::string& name) {
  auto gio = lib.create_io(name);
  gio->add_input("idx", kLIdx);
  gio->add_input("inv", kLInv);
  gio->add_input("pin", kLPin);
  gio->add_input("iin", kLIin);
  gio->add_output("pout", kLPout);
  gio->add_output("iout", kLIout);
  for (const auto* p : {"idx", "inv"}) {
    gio->set_bits(p, 8);
    gio->set_unsign(p, true);
  }
  for (const auto* p : {"pin", "iin", "pout", "iout"}) {
    gio->set_bits(p, 32);
    gio->set_unsign(p, true);
  }
  auto g   = gio->create_graph();
  auto in  = g->get_input_node();
  auto idx = in.create_driver_pin(kLIdx);
  auto inv = in.create_driver_pin(kLInv);
  auto pin = in.create_driver_pin(kLPin);
  auto iin = in.create_driver_pin(kLIin);
  gu::set_ubits(idx, 8);
  gu::set_ubits(inv, 8);
  gu::set_ubits(pin, 32);
  gu::set_ubits(iin, 32);

  auto sm = gu::create_typed_node(*g, Ntype_op::Set_mask, 32);  // parallel: pout = set_mask(pin, idx, inv)
  pin.connect_sink(gu::setup_sink_by_name(sm, "a"));
  idx.connect_sink(gu::setup_sink_by_name(sm, "mask"));
  inv.connect_sink(gu::setup_sink_by_name(sm, "value"));
  sm.create_driver_pin(0).connect_sink(g->get_output_node().create_sink_pin(kLPout));

  auto acc = gu::create_typed_node(*g, Ntype_op::Sum, 32);  // recurrence: iout = iin + inv
  iin.connect_sink(gu::setup_sink_by_name(acc, "as"));
  inv.connect_sink(gu::setup_sink_by_name(acc, "as"));
  acc.create_driver_pin(0).connect_sink(g->get_output_node().create_sink_pin(kLIout));
  return g;
}

// A HOST module around the loop: `inv` comes from a real graph input and both
// carry results reach real graph outputs. Every Sub input needs a binding or
// occurrence materialization cannot represent the call, so an unconnected
// invariant is a malformed fixture rather than an interesting case.
hhds::Node_class instantiate_loop(hhds::Graph& host, const std::shared_ptr<hhds::Graph>& body, uint64_t count) {
  auto hio = host.get_io();
  hio->add_input("inv", kLInv);
  hio->add_output("pout", kLPout);
  hio->add_output("iout", kLIout);
  hio->set_bits("inv", 8);
  hio->set_unsign("inv", true);
  hio->set_bits("pout", 32);
  hio->set_bits("iout", 32);

  auto               sub = gu::create_typed_node(host, Ntype_op::Sub);
  hhds::Subnode_loop loop;
  loop.first       = 0;
  loop.step        = 1;
  loop.count       = count;
  loop.index_input = kLIdx;
  sub.set_subnode(body->get_io(), loop);

  auto inv = host.get_input_node().create_driver_pin(kLInv);
  gu::set_ubits(inv, 8);
  inv.connect_sink(sub.create_sink_pin(kLInv));

  // A carry input needs BOTH drivers: the self-edge (replica r>0 reads replica
  // r-1) and an external initial value (replica 0's seed). One without the other
  // is an incomplete descriptor -- hhds rejects it in Subnode_group::validate.
  auto zero = gu::create_const(host, *Dlop::create_integer(0));
  zero.connect_sink(sub.create_sink_pin(kLPin));
  zero.connect_sink(sub.create_sink_pin(kLIin));
  sub.create_driver_pin(kLPout).connect_sink(sub.create_sink_pin(kLPin));
  sub.create_driver_pin(kLIout).connect_sink(sub.create_sink_pin(kLIin));

  sub.create_driver_pin(kLPout).connect_sink(host.get_output_node().create_sink_pin(kLPout));
  sub.create_driver_pin(kLIout).connect_sink(host.get_output_node().create_sink_pin(kLIout));
  return sub;
}

std::size_t loop_subs(hhds::Graph* g) {
  std::size_t n = 0;
  for (auto node : g->body().nodes(hhds::Node_order::forward)) {
    if (!node.is_invalid() && gu::type_op_of(node) == Ntype_op::Sub && node.is_loop_subnode()) {
      ++n;
    }
  }
  return n;
}

}  // namespace

TEST(LoopSplit, IndependentCarriesSplitIntoTwoLoops) {
  auto& lib  = livehd::Hhds_graph_library::instance("lgdb_legalize_split_ok");
  auto  body = two_carry_body(lib, "sp_body");
  auto  hio  = lib.create_io("sp_host");
  auto  host = hio->create_graph();
  auto  sub  = instantiate_loop(*host, body, 4);
  (void)sub;

  ASSERT_EQ(loop_subs(host.get()), 1u);
  EXPECT_EQ(livehd::legalize::split_loops(host.get(), lib), 1);
  EXPECT_EQ(loop_subs(host.get()), 2u) << "one parallel loop and one recurrence loop, same domain";

  // Both halves keep the ORIGINAL domain: the iteration space is unchanged.
  for (auto n : host->body().nodes(hhds::Node_order::forward)) {
    if (n.is_invalid() || !n.is_loop_subnode()) {
      continue;
    }
    auto d = n.subnode_loop();
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->count, 4u);
    EXPECT_EQ(d->first, 0);
    EXPECT_EQ(d->step, 1);
    EXPECT_EQ(d->index_input, kLIdx);
  }
  // Derived, stable names -- abc_incr keys its region cache on the module name.
  EXPECT_TRUE(lib.find_io("sp_body__par"));
  EXPECT_TRUE(lib.find_io("sp_body__ind"));
}

// legalize's output is the SHARED frozen artifact that LEC, synthesis and
// simulation all reuse, so an orphaned def is serialized and cached for nothing.
// Splitting removes the only instance the original body had; legalize_design
// must not leave it behind.
TEST(LoopSplit, DesignSweepDropsTheOrphanedBody) {
  auto& lib  = livehd::Hhds_graph_library::instance("lgdb_legalize_orphan");
  auto  body = two_carry_body(lib, "orph_body");
  auto  host = lib.create_io("orph_host")->create_graph();
  (void)instantiate_loop(*host, body, 4);
  const auto body_gid = body->get_gid();
  body.reset();

  const auto r = livehd::legalize::legalize_design({host});

  EXPECT_EQ(r.added.size(), 2u) << "the two halves must be surfaced to the caller (var.graphs, the cache, the emits)";
  ASSERT_EQ(r.removed.size(), 1u) << "the orphaned body must be reported so the caller drops its own reference";
  EXPECT_EQ(r.removed[0]->get_gid(), body_gid);
  EXPECT_TRUE(lib.find_io("orph_body__par"));
  EXPECT_TRUE(lib.find_io("orph_body__ind"));
  EXPECT_FALSE(lib.has_graph(body_gid)) << "the replaced loop body must not survive into the shared artifact";
  EXPECT_FALSE(lib.find_io("orph_body")) << "its IO declaration is what an emit/save walks; it must go too";
}

// NEGATIVE: the slice write's POSITION comes from the sibling counter carry.
// `l[cnt] = inv; cnt += 1`: the classifier must not call `l` parallel (its
// window depends on a carry -- somebody else's), and a split would have handed
// the parallel half a counter port it never self-wires, so every lane would
// read the seed. The loop stays whole.
TEST(LoopSplit, AHalfThatReadsTheOtherHalfsCarryIsRefused) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_legalize_split_cross");
  auto  gio = lib.create_io("cross_body");
  gio->add_input("idx", kLIdx);
  gio->add_input("inv", kLInv);
  gio->add_input("pin", kLPin);
  gio->add_input("iin", kLIin);
  gio->add_output("pout", kLPout);
  gio->add_output("iout", kLIout);
  for (const auto* p : {"idx", "inv"}) {
    gio->set_bits(p, 8);
    gio->set_unsign(p, true);
  }
  for (const auto* p : {"pin", "iin", "pout", "iout"}) {
    gio->set_bits(p, 32);
    gio->set_unsign(p, true);
  }
  auto g   = gio->create_graph();
  auto in  = g->get_input_node();
  auto inv = in.create_driver_pin(kLInv);
  auto pin = in.create_driver_pin(kLPin);
  auto iin = in.create_driver_pin(kLIin);
  gu::set_ubits(in.create_driver_pin(kLIdx), 8);
  gu::set_ubits(inv, 8);
  gu::set_ubits(pin, 32);
  gu::set_ubits(iin, 32);

  auto sm = gu::create_typed_node(*g, Ntype_op::Set_mask, 32);  // pout = set_mask(pin, iin, inv): position = the COUNTER
  pin.connect_sink(gu::setup_sink_by_name(sm, "a"));
  iin.connect_sink(gu::setup_sink_by_name(sm, "mask"));
  inv.connect_sink(gu::setup_sink_by_name(sm, "value"));
  sm.create_driver_pin(0).connect_sink(g->get_output_node().create_sink_pin(kLPout));

  auto acc = gu::create_typed_node(*g, Ntype_op::Sum, 32);  // iout = iin + inv
  iin.connect_sink(gu::setup_sink_by_name(acc, "as"));
  inv.connect_sink(gu::setup_sink_by_name(acc, "as"));
  acc.create_driver_pin(0).connect_sink(g->get_output_node().create_sink_pin(kLIout));

  auto host = lib.create_io("cross_host")->create_graph();
  auto sub  = instantiate_loop(*host, g, 4);

  auto cls = gu::classify_loop(sub);
  ASSERT_TRUE(cls.valid);
  ASSERT_EQ(cls.carries.size(), 2u);
  for (const auto& c : cls.carries) {
    EXPECT_EQ(c.kind == gu::Carry_kind::disjoint_slice, false) << "a window chosen by a sibling carry is not parallel";
  }
  EXPECT_EQ(livehd::legalize::split_loops(host.get(), lib), 0);
  EXPECT_EQ(loop_subs(host.get()), 1u);
}

// NEGATIVE: a body output that is NOT a carry (a per-lane "final" the host
// reads) has no half to live in; the loop stays whole rather than losing it.
TEST(LoopSplit, ABodyWithANonCarryOutputIsLeftWhole) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_legalize_split_final");
  auto  gio = lib.create_io("final_body");
  gio->add_input("idx", kLIdx);
  gio->add_input("inv", kLInv);
  gio->add_input("pin", kLPin);
  gio->add_input("iin", kLIin);
  gio->add_output("pout", kLPout);
  gio->add_output("iout", kLIout);
  gio->add_output("last", 7);
  for (const auto* p : {"idx", "inv"}) {
    gio->set_bits(p, 8);
    gio->set_unsign(p, true);
  }
  for (const auto* p : {"pin", "iin", "pout", "iout"}) {
    gio->set_bits(p, 32);
    gio->set_unsign(p, true);
  }
  gio->set_bits("last", 8);
  auto g   = gio->create_graph();
  auto in  = g->get_input_node();
  auto idx = in.create_driver_pin(kLIdx);
  auto inv = in.create_driver_pin(kLInv);
  auto pin = in.create_driver_pin(kLPin);
  auto iin = in.create_driver_pin(kLIin);
  gu::set_ubits(idx, 8);
  gu::set_ubits(inv, 8);
  gu::set_ubits(pin, 32);
  gu::set_ubits(iin, 32);
  auto sm = gu::create_typed_node(*g, Ntype_op::Set_mask, 32);
  pin.connect_sink(gu::setup_sink_by_name(sm, "a"));
  idx.connect_sink(gu::setup_sink_by_name(sm, "mask"));
  inv.connect_sink(gu::setup_sink_by_name(sm, "value"));
  sm.create_driver_pin(0).connect_sink(g->get_output_node().create_sink_pin(kLPout));
  auto acc = gu::create_typed_node(*g, Ntype_op::Sum, 32);
  iin.connect_sink(gu::setup_sink_by_name(acc, "as"));
  inv.connect_sink(gu::setup_sink_by_name(acc, "as"));
  acc.create_driver_pin(0).connect_sink(g->get_output_node().create_sink_pin(kLIout));
  inv.connect_sink(g->get_output_node().create_sink_pin(7));  // last = inv: a plain per-lane output

  auto host = lib.create_io("final_host")->create_graph();
  (void)instantiate_loop(*host, g, 4);

  EXPECT_EQ(livehd::legalize::split_loops(host.get(), lib), 0) << "a non-carry output has no half to live in";
  EXPECT_EQ(loop_subs(host.get()), 1u);
}

// A loop whose carries are ALL one kind has nothing to split; leaving it whole
// must not be mistaken for a split.
TEST(LoopSplit, OneSidedLoopsAreLeftWhole) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_legalize_split_onesided");
  auto  gio = lib.create_io("os_body");
  gio->add_input("idx", kLIdx);
  gio->add_input("inv", kLInv);
  gio->add_input("iin", kLIin);
  gio->add_output("iout", kLIout);
  gio->set_bits("idx", 8);
  gio->set_bits("inv", 8);
  gio->set_bits("iin", 32);
  gio->set_bits("iout", 32);
  auto g   = gio->create_graph();
  auto in  = g->get_input_node();
  auto inv = in.create_driver_pin(kLInv);
  auto iin = in.create_driver_pin(kLIin);
  gu::set_ubits(inv, 8);
  gu::set_ubits(iin, 32);
  auto acc = gu::create_typed_node(*g, Ntype_op::Sum, 32);
  iin.connect_sink(gu::setup_sink_by_name(acc, "as"));
  inv.connect_sink(gu::setup_sink_by_name(acc, "as"));
  acc.create_driver_pin(0).connect_sink(g->get_output_node().create_sink_pin(kLIout));

  auto               hio  = lib.create_io("os_host");
  auto               host = hio->create_graph();
  auto               sub  = gu::create_typed_node(*host, Ntype_op::Sub);
  hhds::Subnode_loop loop;
  loop.first       = 0;
  loop.step        = 1;
  loop.count       = 4;
  loop.index_input = kLIdx;
  sub.set_subnode(g->get_io(), loop);
  sub.create_driver_pin(kLIout).connect_sink(sub.create_sink_pin(kLIin));

  EXPECT_EQ(livehd::legalize::split_loops(host.get(), lib), 0) << "only a recurrence: nothing to separate";
  EXPECT_EQ(loop_subs(host.get()), 1u);
}

// THE gate for the whole split idea: the two-loop form must COMPUTE what the
// one-loop form computed. Structure deliberately differs, so this is an LEC
// obligation -- discharged by the same engine `lhd lec` uses, against the
// unsplit original.
TEST(LoopSplit, SplitLoopIsEquivalentToTheOriginal) {
  auto& ref_lib = livehd::Hhds_graph_library::instance("lgdb_legalize_spliteq_ref");
  auto& imp_lib = livehd::Hhds_graph_library::instance("lgdb_legalize_spliteq_imp");

  // Same source design, built twice: one left whole, one split.
  auto ref_body = two_carry_body(ref_lib, "eq_body");
  auto ref_host = ref_lib.create_io("eq_top")->create_graph();
  (void)instantiate_loop(*ref_host, ref_body, 4);

  auto imp_body = two_carry_body(imp_lib, "eq_body");
  auto imp_host = imp_lib.create_io("eq_top")->create_graph();
  (void)instantiate_loop(*imp_host, imp_body, 4);

  ASSERT_EQ(livehd::legalize::split_loops(imp_host.get(), imp_lib), 1);
  ASSERT_EQ(loop_subs(ref_host.get()), 1u);
  ASSERT_EQ(loop_subs(imp_host.get()), 2u);

  // Every def on each side has to be visible to the encoder: the split created
  // two new callee bodies that the miter must be able to descend into.
  absl::flat_hash_map<hhds::Gid, hhds::Graph*> sub_lib;
  std::vector<std::shared_ptr<hhds::Graph>>    keep;
  for (auto* lib : {&ref_lib, &imp_lib}) {
    for (const auto gid : lib->all_gids()) {
      if (auto g = lib->get_graph(gid)) {
        sub_lib[gid] = g.get();
        keep.push_back(std::move(g));
      }
    }
  }

  livehd::lec::Lec_options o;
  o.engine  = "ind";
  o.timeout = 30;
  auto r    = livehd::lec::prove_equal(ref_host.get(), imp_host.get(), o, &sub_lib);
  EXPECT_EQ(r.verdict, livehd::lec::Verdict::Proven)
      << "splitting a loop into a parallel half and a recurrence half must preserve behavior; got " << r.detail;
}
