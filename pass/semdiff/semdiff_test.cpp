// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Unit tests for the structural matcher (task 2f-semdiff). Two designs are built
// in SEPARATE graph libraries (independent gids/attr stores — the cross-library
// trap) and matched in place; assertions read the `match` attribute back.

#include "semdiff.hpp"

#include <memory>

#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "node_util.hpp"

using livehd::graph_util::create_typed_node;
using livehd::graph_util::match_of;

namespace {

// y = (a & b) | c, built in library `dir` under module `mod`.
std::shared_ptr<hhds::Graph> build_and_or(const std::string& dir, const std::string& mod) {
  auto& lib = livehd::Hhds_graph_library::instance(dir);
  auto  gio = lib.create_io(mod);
  gio->add_input("a", 1);
  gio->add_input("b", 1);
  gio->add_input("c", 1);
  gio->add_output("y", 1);
  auto g = gio->create_graph();

  auto a_and = create_typed_node(*g, Ntype_op::And);
  g->get_input_pin("a").connect_sink(a_and.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(a_and.create_sink_pin(0));

  auto an_or = create_typed_node(*g, Ntype_op::Or);
  a_and.create_driver_pin(0).connect_sink(an_or.create_sink_pin(0));
  g->get_input_pin("c").connect_sink(an_or.create_sink_pin(0));
  an_or.create_driver_pin(0).connect_sink(g->get_output_pin("y"));
  return g;
}

uint32_t unmatched_nodes(hhds::Graph* g) {
  uint32_t n = 0;
  for (auto node : g->forward_class()) {
    if (match_of(node) == 0) {
      ++n;
    }
  }
  return n;
}

}  // namespace

// Identical designs => every node gets a nonzero shared id.
TEST(Semdiff, IdenticalAllMatched) {
  auto a = build_and_or("lgdb_semdiff_id_a", "m");
  auto b = build_and_or("lgdb_semdiff_id_b", "m");

  auto r = livehd::semdiff::structural_match(a.get(), b.get());

  EXPECT_EQ(0U, r.a_unmatched);
  EXPECT_EQ(0U, r.b_unmatched);
  EXPECT_EQ(2U, r.a_matched);  // And, Or
  EXPECT_GT(r.regions, 0U);
  EXPECT_DOUBLE_EQ(1.0, r.similarity);
  EXPECT_EQ(0U, unmatched_nodes(a.get()));
  EXPECT_EQ(0U, unmatched_nodes(b.get()));

  // Corresponding nodes carry the SAME id across the two graphs.
  uint32_t a_or = 0, b_or = 0;
  for (auto n : a->forward_class()) {
    if (livehd::graph_util::type_op_of(n) == Ntype_op::Or) {
      a_or = match_of(n);
    }
  }
  for (auto n : b->forward_class()) {
    if (livehd::graph_util::type_op_of(n) == Ntype_op::Or) {
      b_or = match_of(n);
    }
  }
  EXPECT_NE(0U, a_or);
  EXPECT_EQ(a_or, b_or);
}

// An extra, structurally-distinct dangling gate on the impl side stays match=0;
// everything else still matches.
TEST(Semdiff, ExtraGateUnmatched) {
  auto a = build_and_or("lgdb_semdiff_xg_a", "m");
  auto b = build_and_or("lgdb_semdiff_xg_b", "m");

  // impl additionally computes an unused Not(c).
  auto extra = create_typed_node(*b, Ntype_op::Not);
  b->get_input_pin("c").connect_sink(extra.create_sink_pin(0));

  auto r = livehd::semdiff::structural_match(a.get(), b.get());

  EXPECT_EQ(0U, r.a_unmatched);   // ref fully matched
  EXPECT_EQ(1U, r.b_unmatched);   // only the extra Not is unmatched
  EXPECT_EQ(2U, r.a_matched);
  EXPECT_EQ(2U, r.b_matched);
}

// canonical_digest: two independently-built identical designs (separate
// libraries — independent gids, allocation order) produce the SAME digest.
TEST(Semdiff, DigestStableAcrossLibraries) {
  auto a = build_and_or("lgdb_semdiff_dg_a", "m");
  auto b = build_and_or("lgdb_semdiff_dg_b", "m");

  auto da = livehd::semdiff::canonical_digest(a.get());
  auto db = livehd::semdiff::canonical_digest(b.get());

  EXPECT_TRUE(da.valid);
  EXPECT_TRUE(db.valid);
  EXPECT_EQ(da, db);
}

// canonical_digest: construction ORDER must not leak in — build the same
// netlist creating the Or node before the And node.
TEST(Semdiff, DigestConstructionOrderIndependent) {
  auto a = build_and_or("lgdb_semdiff_do_a", "m");

  auto& lib = livehd::Hhds_graph_library::instance("lgdb_semdiff_do_b");
  auto  gio = lib.create_io("m");
  gio->add_input("a", 1);
  gio->add_input("b", 1);
  gio->add_input("c", 1);
  gio->add_output("y", 1);
  auto b = gio->create_graph();

  auto an_or = create_typed_node(*b, Ntype_op::Or);  // Or allocated FIRST
  auto a_and = create_typed_node(*b, Ntype_op::And);
  b->get_input_pin("a").connect_sink(a_and.create_sink_pin(0));
  b->get_input_pin("b").connect_sink(a_and.create_sink_pin(0));
  a_and.create_driver_pin(0).connect_sink(an_or.create_sink_pin(0));
  b->get_input_pin("c").connect_sink(an_or.create_sink_pin(0));
  an_or.create_driver_pin(0).connect_sink(b->get_output_pin("y"));

  auto da = livehd::semdiff::canonical_digest(a.get());
  auto db = livehd::semdiff::canonical_digest(b.get());
  EXPECT_TRUE(da.valid && db.valid);
  EXPECT_EQ(da, db);
}

// canonical_digest: a width or IO-name change must change the digest (the
// unsigned bits = magnitude+1 trap; lec pairs IO by name). NOTE add_input's
// second arg is the PORT ID, not bits — width is the pin `bits` attribute.
TEST(Semdiff, DigestSensitiveToWidthAndIoName) {
  auto base = build_and_or("lgdb_semdiff_dw_base", "m");

  auto wg = build_and_or("lgdb_semdiff_dw_w", "m");
  livehd::graph_util::set_bits(wg->get_input_pin("a"), 2);  // widened input

  auto& ln  = livehd::Hhds_graph_library::instance("lgdb_semdiff_dw_n");
  auto  gn  = ln.create_io("m");
  gn->add_input("a", 1);
  gn->add_input("b", 2);
  gn->add_input("c", 3);
  gn->add_output("z", 4);  // renamed output (y -> z)
  auto ng    = gn->create_graph();
  auto n_and = create_typed_node(*ng, Ntype_op::And);
  ng->get_input_pin("a").connect_sink(n_and.create_sink_pin(0));
  ng->get_input_pin("b").connect_sink(n_and.create_sink_pin(0));
  auto n_or = create_typed_node(*ng, Ntype_op::Or);
  n_and.create_driver_pin(0).connect_sink(n_or.create_sink_pin(0));
  ng->get_input_pin("c").connect_sink(n_or.create_sink_pin(0));
  n_or.create_driver_pin(0).connect_sink(ng->get_output_pin("z"));

  auto d0 = livehd::semdiff::canonical_digest(base.get());
  auto dw = livehd::semdiff::canonical_digest(wg.get());
  auto dn = livehd::semdiff::canonical_digest(ng.get());
  EXPECT_TRUE(d0.valid && dw.valid && dn.valid);
  EXPECT_NE(d0, dw);
  EXPECT_NE(d0, dn);
}

// canonical_digest: a named flop digests fine; an ANONYMOUS flop poisons the
// digest (valid=false) — its only key would be the per-run debug nid, and a
// constant fallback could fold two different graphs to one digest.
TEST(Semdiff, DigestAnonymousStateCellInvalid) {
  auto named = [&](const std::string& dir, bool name_it) {
    auto& lib = livehd::Hhds_graph_library::instance(dir);
    auto  gio = lib.create_io("m");
    gio->add_input("d", 1);
    gio->add_output("q", 1);
    auto g    = gio->create_graph();
    auto flop = create_typed_node(*g, Ntype_op::Flop);
    g->get_input_pin("d").connect_sink(flop.create_sink_pin(0));
    auto qpin = flop.create_driver_pin(0);
    if (name_it) {
      livehd::graph_util::set_pin_name(qpin, "r_state");
    }
    qpin.connect_sink(g->get_output_pin("q"));
    return g;
  };

  auto dg_named = livehd::semdiff::canonical_digest(named("lgdb_semdiff_df_n", true).get());
  auto dg_anon  = livehd::semdiff::canonical_digest(named("lgdb_semdiff_df_a", false).get());
  EXPECT_TRUE(dg_named.valid);
  EXPECT_FALSE(dg_anon.valid);
}

// canonical_digest is HIERARCHICAL (Merkle): with a resolver, a parent's digest
// folds its child's digest — an edited child body changes the parent digest.
// Without a resolver the Sub is a blackbox (name identity only) and the parent
// digest is insensitive to the child's internals.
TEST(Semdiff, DigestHierarchicalMerkle) {
  auto build = [&](const std::string& dir, bool child_uses_or) {
    auto& lib = livehd::Hhds_graph_library::instance(dir);

    auto cio = lib.create_io("child");
    cio->add_input("x", 1);
    cio->add_input("y", 1);
    cio->add_output("o", 1);
    auto cg  = cio->create_graph();
    auto op  = create_typed_node(*cg, child_uses_or ? Ntype_op::Or : Ntype_op::And);
    cg->get_input_pin("x").connect_sink(op.create_sink_pin(0));
    cg->get_input_pin("y").connect_sink(op.create_sink_pin(0));
    op.create_driver_pin(0).connect_sink(cg->get_output_pin("o"));

    auto pio = lib.create_io("parent");
    pio->add_input("a", 1);
    pio->add_input("b", 1);
    pio->add_output("z", 1);
    auto pg  = pio->create_graph();
    auto sub = create_typed_node(*pg, Ntype_op::Sub);
    sub.set_subnode(cio);
    pg->get_input_pin("a").connect_sink(sub.create_sink_pin(0));
    pg->get_input_pin("b").connect_sink(sub.create_sink_pin(1));
    sub.create_driver_pin(0).connect_sink(pg->get_output_pin("z"));

    livehd::semdiff::Digest_resolver resolve = [&lib](hhds::Gid gid) -> hhds::Graph* {
      auto g = lib.get_graph(gid);
      return g ? g.get() : nullptr;
    };
    return std::pair{livehd::semdiff::canonical_digest(pg.get(), resolve),
                     livehd::semdiff::canonical_digest(pg.get())};  // no resolver: blackbox
  };

  auto [and_deep, and_bb] = build("lgdb_semdiff_hm_and", false);
  auto [or_deep, or_bb]   = build("lgdb_semdiff_hm_or", true);

  EXPECT_TRUE(and_deep.valid && or_deep.valid && and_bb.valid && or_bb.valid);
  EXPECT_NE(and_deep, or_deep);  // Merkle: child edit changes the parent digest
  EXPECT_EQ(and_bb, or_bb);      // blackbox: child internals invisible by design
  EXPECT_NE(and_deep, and_bb);   // resolved vs blackbox digests are distinct forms
}

// ---- tier-2 full-match state pairing (state_pairing) ------------------------

// in d -> [flop r0] -> Not -> [flop r1] -> out q, with per-side flop names.
std::shared_ptr<hhds::Graph> build_pipe2(const std::string& dir, const std::string& n0, const std::string& n1) {
  auto& lib = livehd::Hhds_graph_library::instance(dir);
  auto  gio = lib.create_io("m");
  gio->add_input("d", 1);
  gio->add_output("q", 1);
  auto g = gio->create_graph();

  auto f0 = create_typed_node(*g, Ntype_op::Flop);
  g->get_input_pin("d").connect_sink(f0.create_sink_pin(3));  // din
  auto q0 = f0.create_driver_pin(0);
  livehd::graph_util::set_pin_name(q0, n0);

  auto inv = create_typed_node(*g, Ntype_op::Not);
  q0.connect_sink(inv.create_sink_pin(0));

  auto f1 = create_typed_node(*g, Ntype_op::Flop);
  inv.create_driver_pin(0).connect_sink(f1.create_sink_pin(3));  // din
  auto q1 = f1.create_driver_pin(0);
  livehd::graph_util::set_pin_name(q1, n1);
  q1.connect_sink(g->get_output_pin("q"));
  return g;
}

// Renamed pipeline flops: tier-1 (name) pairs nothing, the full-match signature
// pass pairs both — the chain stages are told apart by anchor DISTANCE.
TEST(Semdiff, StatePairingRenamedPipeline) {
  auto a = build_pipe2("lgdb_semdiff_sp_a", "ra", "rb");
  auto b = build_pipe2("lgdb_semdiff_sp_b", "xa", "xb");

  livehd::semdiff::Semdiff_options o;
  o.matching_names = true;
  o.state_pairing  = true;
  auto r           = livehd::semdiff::structural_match(a.get(), b.get(), o);

  EXPECT_EQ(2U, r.state.a_total);
  EXPECT_EQ(2U, r.state.b_total);
  EXPECT_EQ(0U, r.state.name_pairs);
  EXPECT_EQ(2U, r.state.full_pairs);
  EXPECT_EQ(0U, r.state.a_unpaired);
  EXPECT_EQ(0U, r.state.b_unpaired);

  // The paired seeds let the WHOLE structure match: nothing left unmatched.
  EXPECT_EQ(0U, r.a_unmatched);
  EXPECT_EQ(0U, r.b_unmatched);
}

// Without state_pairing the renamed flops stay unmatched frontiers (the
// pre-tier-2 behavior — this is the gap the pass exists to close).
TEST(Semdiff, RenamedFlopsUnpairedWithoutStatePairing) {
  auto a = build_pipe2("lgdb_semdiff_sn_a", "ra", "rb");
  auto b = build_pipe2("lgdb_semdiff_sn_b", "xa", "xb");

  livehd::semdiff::Semdiff_options o;
  o.matching_names = true;
  auto r           = livehd::semdiff::structural_match(a.get(), b.get(), o);

  EXPECT_EQ(0U, r.state.name_pairs);
  EXPECT_EQ(0U, r.state.full_pairs);
  EXPECT_EQ(2U, r.state.a_unpaired);
  EXPECT_EQ(2U, r.state.b_unpaired);
}

// Mixed: one flop keeps its name across the sides (tier-1), the other is
// renamed and full-matches (tier-2).
TEST(Semdiff, StatePairingMixedTiers) {
  auto a = build_pipe2("lgdb_semdiff_sm_a", "keep", "rb");
  auto b = build_pipe2("lgdb_semdiff_sm_b", "keep", "xb");

  livehd::semdiff::Semdiff_options o;
  o.matching_names = true;
  o.state_pairing  = true;
  auto r           = livehd::semdiff::structural_match(a.get(), b.get(), o);

  EXPECT_EQ(1U, r.state.name_pairs);
  EXPECT_EQ(1U, r.state.full_pairs);
  EXPECT_EQ(0U, r.state.a_unpaired);
  EXPECT_EQ(0U, r.state.b_unpaired);
}

// Two TRULY symmetric flops (independent, same input, both dead-ended into one
// output through symmetric ops) share the signature: the ambiguous bucket stays
// unpaired — never force-picked.
TEST(Semdiff, StatePairingAmbiguousTwinsStayUnpaired) {
  auto twins = [](const std::string& dir, const std::string& n0, const std::string& n1) {
    auto& lib = livehd::Hhds_graph_library::instance(dir);
    auto  gio = lib.create_io("m");
    gio->add_input("d", 1);
    gio->add_output("q", 1);
    auto g = gio->create_graph();

    auto an_or = create_typed_node(*g, Ntype_op::Or);
    for (const auto& nm : {n0, n1}) {
      auto f = create_typed_node(*g, Ntype_op::Flop);
      g->get_input_pin("d").connect_sink(f.create_sink_pin(3));
      auto q = f.create_driver_pin(0);
      livehd::graph_util::set_pin_name(q, nm);
      q.connect_sink(an_or.create_sink_pin(0));
    }
    an_or.create_driver_pin(0).connect_sink(g->get_output_pin("q"));
    return g;
  };
  auto a = twins("lgdb_semdiff_st_a", "ta", "tb");
  auto b = twins("lgdb_semdiff_st_b", "ua", "ub");

  livehd::semdiff::Semdiff_options o;
  o.matching_names = true;
  o.state_pairing  = true;
  auto r           = livehd::semdiff::structural_match(a.get(), b.get(), o);

  EXPECT_EQ(0U, r.state.full_pairs);
  EXPECT_EQ(2U, r.state.a_unpaired);
  EXPECT_EQ(2U, r.state.b_unpaired);
  EXPECT_EQ(2U, r.state.a_ambiguous);
  EXPECT_EQ(2U, r.state.b_ambiguous);
}

// Differing const reset values refuse to full-match (the 2f-lec pair
// precondition folds into the signature).
TEST(Semdiff, StatePairingInitMismatchRefuses) {
  auto with_init = [](const std::string& dir, const std::string& nm, int64_t init) {
    auto& lib = livehd::Hhds_graph_library::instance(dir);
    auto  gio = lib.create_io("m");
    gio->add_input("d", 1);
    gio->add_output("q", 1);
    auto g = gio->create_graph();
    auto f = create_typed_node(*g, Ntype_op::Flop);
    g->get_input_pin("d").connect_sink(f.create_sink_pin(3));
    auto cval = Dlop::create_integer(init);
    auto c    = livehd::graph_util::create_const(*g, *cval);
    c.connect_sink(f.create_sink_pin(1));  // initial
    auto q = f.create_driver_pin(0);
    livehd::graph_util::set_pin_name(q, nm);
    q.connect_sink(g->get_output_pin("q"));
    return g;
  };
  auto a = with_init("lgdb_semdiff_si_a", "ra", 0);
  auto b = with_init("lgdb_semdiff_si_b", "xa", 1);  // renamed AND different reset

  livehd::semdiff::Semdiff_options o;
  o.matching_names = true;
  o.state_pairing  = true;
  auto r           = livehd::semdiff::structural_match(a.get(), b.get(), o);

  EXPECT_EQ(0U, r.state.full_pairs);
  EXPECT_EQ(1U, r.state.a_unpaired);
  EXPECT_EQ(1U, r.state.b_unpaired);
}

// name_noise=1.0 destroys every impl key: tier-1 pairs nothing, tier-2 recovers
// both flops, and the ground-truth check scores them CORRECT.
TEST(Semdiff, StatePairingNoiseRecoveryScored) {
  auto a = build_pipe2("lgdb_semdiff_nz_a", "r0", "r1");
  auto b = build_pipe2("lgdb_semdiff_nz_b", "r0", "r1");  // same names — noise breaks them

  livehd::semdiff::Semdiff_options o;
  o.matching_names = true;
  o.state_pairing  = true;
  o.name_noise     = 1.0;
  auto r           = livehd::semdiff::structural_match(a.get(), b.get(), o);

  EXPECT_EQ(2U, r.state.noised);
  EXPECT_EQ(0U, r.state.name_pairs);
  EXPECT_EQ(2U, r.state.full_pairs);
  EXPECT_EQ(2U, r.state.noised_recovered);
  EXPECT_EQ(2U, r.state.noised_correct);
}

// A diverging op (And vs Or at the same spot) is the gap: neither side's node
// matches, surrounding primary IO is the anchored boundary.
TEST(Semdiff, DivergentOpIsGap) {
  auto& la  = livehd::Hhds_graph_library::instance("lgdb_semdiff_dv_a");
  auto  gioa = la.create_io("m");
  gioa->add_input("a", 1);
  gioa->add_input("b", 1);
  gioa->add_output("y", 1);
  auto a    = gioa->create_graph();
  auto a_op = create_typed_node(*a, Ntype_op::And);
  a->get_input_pin("a").connect_sink(a_op.create_sink_pin(0));
  a->get_input_pin("b").connect_sink(a_op.create_sink_pin(0));
  a_op.create_driver_pin(0).connect_sink(a->get_output_pin("y"));

  auto& lb  = livehd::Hhds_graph_library::instance("lgdb_semdiff_dv_b");
  auto  giob = lb.create_io("m");
  giob->add_input("a", 1);
  giob->add_input("b", 1);
  giob->add_output("y", 1);
  auto b    = giob->create_graph();
  auto b_op = create_typed_node(*b, Ntype_op::Or);
  b->get_input_pin("a").connect_sink(b_op.create_sink_pin(0));
  b->get_input_pin("b").connect_sink(b_op.create_sink_pin(0));
  b_op.create_driver_pin(0).connect_sink(b->get_output_pin("y"));

  auto r = livehd::semdiff::structural_match(a.get(), b.get());

  EXPECT_EQ(1U, r.a_unmatched);
  EXPECT_EQ(1U, r.b_unmatched);
  EXPECT_EQ(0U, r.a_matched);
  EXPECT_EQ(0U, r.regions);
}
