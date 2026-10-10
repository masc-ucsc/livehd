// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Unit tests for the structural matcher (task 2f-semdiff). Two designs are built
// in SEPARATE graph libraries (independent gids/attr stores — the cross-library
// trap) and matched in place; assertions read the `match` attribute back.

#include "semdiff.hpp"

#include <format>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string_view>
#include <tuple>

#include "attrs.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"

using livehd::graph_util::create_typed_node;
using livehd::graph_util::match_of;
// Sink pins are minted through setup_sink_pid, never create_sink_pin(<literal>):
// on a BANKED op (And/Or/Xor/Sum/Mult/EQ/...) a literal pid piles every operand
// onto ONE sink pin, which is the shape one-driver-per-sink-pin forbids and
// pass/legalize flags. setup_sink_pid appends a fresh operand slot for those and
// is the plain create_sink_pin for everything else, so the fixtures below build
// the same cells they always did.
using livehd::graph_util::setup_sink_pid;

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
  g->get_input_pin("a").connect_sink(setup_sink_pid(a_and, 0));
  g->get_input_pin("b").connect_sink(setup_sink_pid(a_and, 0));

  auto an_or = create_typed_node(*g, Ntype_op::Or);
  a_and.create_driver_pin(0).connect_sink(setup_sink_pid(an_or, 0));
  g->get_input_pin("c").connect_sink(setup_sink_pid(an_or, 0));
  an_or.create_driver_pin(0).connect_sink(g->get_output_pin("y"));
  return g;
}

std::shared_ptr<hhds::Graph> build_feedback(const std::string& dir, bool swap_op = false) {
  auto& lib = livehd::Hhds_graph_library::instance(dir);
  auto  gio = lib.create_io("feedback");
  gio->add_input("sel", 0);
  gio->add_input("d", 1);
  gio->add_output("y", 1);
  auto g = gio->create_graph();

  auto mux = create_typed_node(*g, swap_op ? Ntype_op::Or : Ntype_op::Mux);
  g->get_input_pin("sel").connect_sink(setup_sink_pid(mux, 0));
  g->get_input_pin("d").connect_sink(setup_sink_pid(mux, 2));
  auto out = mux.create_driver_pin(0);
  out.connect_sink(setup_sink_pid(mux, 1));
  out.connect_sink(g->get_output_pin("y"));
  return g;
}

std::shared_ptr<hhds::Graph> build_ambiguous_state_cuts(const std::string& dir) {
  auto& lib = livehd::Hhds_graph_library::instance(dir);
  auto  gio = lib.create_io("ambiguous_state");
  gio->add_input("d", 0);
  gio->add_output("q", 1);
  auto g = gio->create_graph();

  auto join = create_typed_node(*g, Ntype_op::Concat);
  for (int port = 0; port < 2; ++port) {
    auto flop = create_typed_node(*g, Ntype_op::Flop);
    flop.set_name("duplicate_state_name");
    g->get_input_pin("d").connect_sink(setup_sink_pid(flop, 3));
    auto q = flop.create_driver_pin(0);
    livehd::graph_util::set_bits(q, 1);
    livehd::graph_util::set_pin_name(q, "duplicate_state_name");
    q.connect_sink(join.create_sink_pin(port));
  }
  join.create_driver_pin(0).connect_sink(g->get_output_pin("q"));
  return g;
}

std::shared_ptr<hhds::Graph> build_compact_loop(const std::string& dir, uint64_t count) {
  auto& lib = livehd::Hhds_graph_library::instance(dir);

  auto body_io = lib.create_io("loop_body");
  body_io->add_input("x", 0);
  body_io->add_output("y", 1);
  body_io->set_bits("x", 9);
  body_io->set_bits("y", 9);
  body_io->set_unsign("x", true);
  body_io->set_unsign("y", true);
  auto body = body_io->create_graph();
  body->get_input_pin("x").connect_sink(body->get_output_pin("y"));

  auto top_io = lib.create_io("loop_top");
  top_io->add_input("x", 0);
  top_io->add_output("y", 1);
  top_io->set_bits("x", 9);
  top_io->set_bits("y", 9);
  top_io->set_unsign("x", true);
  top_io->set_unsign("y", true);
  auto top  = top_io->create_graph();
  auto loop = create_typed_node(*top, Ntype_op::Sub);
  loop.set_name("loop_site");
  loop.set_subnode(body_io,
                   hhds::Subnode_loop{
                       .first              = 0,
                       .step               = 1,
                       .count              = count,
                       .index_input        = std::nullopt,
                       .activation_input   = std::nullopt,
                       .next_active_output = std::nullopt,
                   });
  top->get_input_pin("x").connect_sink(setup_sink_pid(loop, 0));
  auto out = loop.create_driver_pin(1);
  livehd::graph_util::set_bits(out, 9);
  livehd::graph_util::set_unsign(out);
  out.connect_sink(top->get_output_pin("y"));
  loop.subnode_group().validate();
  return top;
}

// y = ~x, optionally with a Get_mask boundary between x and Not. `mask ==
// nullopt` is the direct form.
std::shared_ptr<hhds::Graph> build_mask_boundary(const std::string& dir, std::optional<std::pair<int, int>> range,
                                                 int mask_bits = 8) {
  auto& lib = livehd::Hhds_graph_library::instance(dir);
  auto  gio = lib.create_io("mask_boundary");
  gio->add_input("x", 0);
  gio->add_output("y", 1);
  gio->set_bits("x", 8);
  gio->set_bits("y", 8);
  auto g = gio->create_graph();
  livehd::graph_util::set_bits(g->get_input_pin("x"), 8);

  hhds::Pin_class value = g->get_input_pin("x");
  if (range) {
    auto gm = create_typed_node(*g, Ntype_op::Get_mask);
    value.connect_sink(livehd::graph_util::setup_sink_by_name(gm, "a"));
    livehd::graph_util::connect_bit_range(gm, range->first, range->second);
    value = gm.create_driver_pin(0);
    livehd::graph_util::set_bits(value, mask_bits);
  }
  auto inv = create_typed_node(*g, Ntype_op::Not);
  value.connect_sink(setup_sink_pid(inv, 0));
  auto out = inv.create_driver_pin(0);
  livehd::graph_util::set_bits(out, 8);
  out.connect_sink(g->get_output_pin("y"));
  return g;
}

std::shared_ptr<hhds::Graph> build_memory_kind(const std::string& dir, int64_t type) {
  auto& lib = livehd::Hhds_graph_library::instance(dir);
  auto  gio = lib.create_io("memory_kind");
  gio->add_output("q", 0);
  gio->set_bits("q", 8);
  auto g   = gio->create_graph();
  auto mem = create_typed_node(*g, Ntype_op::Memory);
  mem.set_name("m");
  livehd::graph_util::create_const(*g, *Dlop::create_integer(type))
      .connect_sink(livehd::graph_util::setup_sink_by_name(mem, "type"));
  livehd::graph_util::create_const(*g, *Dlop::create_integer(8)).connect_sink(livehd::graph_util::setup_sink_by_name(mem, "bits"));
  livehd::graph_util::create_const(*g, *Dlop::create_integer(4)).connect_sink(livehd::graph_util::setup_sink_by_name(mem, "size"));
  auto q = mem.create_driver_pin(0);
  livehd::graph_util::set_bits(q, 8);
  livehd::graph_util::set_pin_name(q, "m");
  q.connect_sink(g->get_output_pin("q"));
  return g;
}

uint32_t unmatched_nodes(hhds::Graph* g) {
  uint32_t n = 0;
  for (auto node : g->body().nodes(hhds::Node_order::forward)) {
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
  for (auto n : a->body().nodes(hhds::Node_order::forward)) {
    if (livehd::graph_util::type_op_of(n) == Ntype_op::Or) {
      a_or = match_of(n);
    }
  }
  for (auto n : b->body().nodes(hhds::Node_order::forward)) {
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
  b->get_input_pin("c").connect_sink(setup_sink_pid(extra, 0));

  auto r = livehd::semdiff::structural_match(a.get(), b.get());

  EXPECT_EQ(0U, r.a_unmatched);  // ref fully matched
  EXPECT_EQ(1U, r.b_unmatched);  // only the extra Not is unmatched
  EXPECT_EQ(2U, r.a_matched);
  EXPECT_EQ(2U, r.b_matched);
}

// structural_identical: the fast boolean agrees with structural_match's verdict
// on identical designs, refuses on a broken bijection, and stamps NOTHING.
TEST(Semdiff, StructuralIdenticalFastPath) {
  auto a = build_and_or("lgdb_semdiff_fid_a", "m");
  auto b = build_and_or("lgdb_semdiff_fid_b", "m");
  EXPECT_TRUE(livehd::semdiff::structural_identical(a.get(), b.get()));

  // the fast path leaves the match attribute untouched on both graphs
  for (auto n : a->body().nodes(hhds::Node_order::forward)) {
    EXPECT_FALSE(livehd::graph_util::has_match(n));
  }
  for (auto n : b->body().nodes(hhds::Node_order::forward)) {
    EXPECT_FALSE(livehd::graph_util::has_match(n));
  }

  // an extra dangling gate breaks the node-set bijection => not identical
  auto extra = create_typed_node(*b, Ntype_op::Not);
  b->get_input_pin("c").connect_sink(setup_sink_pid(extra, 0));
  EXPECT_FALSE(livehd::semdiff::structural_identical(a.get(), b.get()));
}

// structural_identical: a swapped operation (functionally different) is refused
// even though the node COUNT matches.
TEST(Semdiff, StructuralIdenticalOpSwap) {
  auto  a   = build_and_or("lgdb_semdiff_so_a", "m");  // y = (a & b) | c
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_semdiff_so_b");
  auto  gio = lib.create_io("m");
  gio->add_input("a", 1);
  gio->add_input("b", 1);
  gio->add_input("c", 1);
  gio->add_output("y", 1);
  auto b    = gio->create_graph();
  auto a_or = create_typed_node(*b, Ntype_op::Or);  // swapped: y = (a | b) & c
  b->get_input_pin("a").connect_sink(setup_sink_pid(a_or, 0));
  b->get_input_pin("b").connect_sink(setup_sink_pid(a_or, 0));
  auto an_and = create_typed_node(*b, Ntype_op::And);
  a_or.create_driver_pin(0).connect_sink(setup_sink_pid(an_and, 0));
  b->get_input_pin("c").connect_sink(setup_sink_pid(an_and, 0));
  an_and.create_driver_pin(0).connect_sink(b->get_output_pin("y"));

  EXPECT_FALSE(livehd::semdiff::structural_identical(a.get(), b.get()));
}

TEST(Semdiff, StructuralIdenticalResolvesCombinationalFeedback) {
  auto a = build_feedback("lgdb_semdiff_fb_a");
  auto b = build_feedback("lgdb_semdiff_fb_b");
  auto c = build_feedback("lgdb_semdiff_fb_c", true);

  EXPECT_TRUE(livehd::semdiff::structural_identical(a.get(), b.get()));
  EXPECT_FALSE(livehd::semdiff::structural_identical(a.get(), c.get()));
}

TEST(Semdiff, ExactArtifactFallbackResolvesAmbiguousStateCuts) {
  auto a = build_ambiguous_state_cuts("lgdb_semdiff_amb_state_a");
  auto b = build_ambiguous_state_cuts("lgdb_semdiff_amb_state_b");

  livehd::semdiff::Semdiff_options options;
  options.matching_names = true;
  options.exact_fallback = true;
  EXPECT_TRUE(livehd::semdiff::structural_identical(a.get(), b.get(), options));
}

TEST(Semdiff, IdentityGetMaskIsTransparent) {
  auto direct = build_mask_boundary("lgdb_semdiff_gm_direct", std::nullopt);
  auto exact  = build_mask_boundary("lgdb_semdiff_gm_exact", std::pair{0, 8});
  auto all    = build_mask_boundary("lgdb_semdiff_gm_all", std::pair{0, 8});

  EXPECT_TRUE(livehd::semdiff::structural_identical(direct.get(), exact.get()));
  EXPECT_TRUE(livehd::semdiff::structural_identical(direct.get(), all.get()));

  auto r = livehd::semdiff::structural_match(direct.get(), exact.get());
  EXPECT_EQ(0U, r.a_unmatched);
  EXPECT_EQ(0U, r.b_unmatched);
  EXPECT_EQ(1U, r.a_matched);  // only Not; the identity wrapper is not a node obligation
  EXPECT_EQ(1U, r.b_matched);
}

TEST(Semdiff, NonIdentityGetMaskNeverDisappears) {
  auto direct = build_mask_boundary("lgdb_semdiff_gm_bad_direct", std::nullopt);
  auto narrow = build_mask_boundary("lgdb_semdiff_gm_narrow", std::pair{0, 7});
  auto offset = build_mask_boundary("lgdb_semdiff_gm_offset", std::pair{1, 8});  // bits [1,8): a slice, not a wrapper
  auto wider  = build_mask_boundary("lgdb_semdiff_gm_wider", std::pair{0, 9});
  auto fit_lo = build_mask_boundary("lgdb_semdiff_gm_fit_lo", std::pair{0, 8}, 7);
  auto fit_hi = build_mask_boundary("lgdb_semdiff_gm_fit_hi", std::pair{0, 8}, 9);

  EXPECT_FALSE(livehd::semdiff::structural_identical(direct.get(), narrow.get()));
  EXPECT_FALSE(livehd::semdiff::structural_identical(direct.get(), offset.get()));
  EXPECT_FALSE(livehd::semdiff::structural_identical(direct.get(), wider.get()));
  EXPECT_FALSE(livehd::semdiff::structural_identical(direct.get(), fit_lo.get()));
  EXPECT_FALSE(livehd::semdiff::structural_identical(direct.get(), fit_hi.get()));
}

TEST(Semdiff, CombinationalArrayMemoryIsNotStateCorrespondence) {
  auto comb_a = build_memory_kind("lgdb_semdiff_mem_comb_a", 2);
  auto comb_b = build_memory_kind("lgdb_semdiff_mem_comb_b", 2);
  auto seq_a  = build_memory_kind("lgdb_semdiff_mem_seq_a", 0);
  auto seq_b  = build_memory_kind("lgdb_semdiff_mem_seq_b", 0);

  livehd::semdiff::Semdiff_options options;
  options.matching_names = true;
  auto comb              = livehd::semdiff::structural_match(comb_a.get(), comb_b.get(), options);
  auto seq               = livehd::semdiff::structural_match(seq_a.get(), seq_b.get(), options);
  EXPECT_EQ(0U, comb.state.a_total);
  EXPECT_EQ(0U, comb.state.b_total);
  EXPECT_EQ(1U, seq.state.a_total);
  EXPECT_EQ(1U, seq.state.b_total);
  EXPECT_EQ(1U, seq.state.name_pairs_mem);
}

TEST(Semdiff, MemoryToRegisterCoverageUsesEachSidesRepresentation) {
  auto  ref  = build_memory_kind("lgdb_semdiff_mem_to_reg_ref", 0);
  auto& lib  = livehd::Hhds_graph_library::instance("lgdb_semdiff_mem_to_reg_impl");
  auto  impl = lib.create_io("memory_kind")->create_graph();
  auto  flop = create_typed_node(*impl, Ntype_op::Flop);
  flop.set_name("m");
  auto q = flop.create_driver_pin(0);
  livehd::graph_util::set_bits(q, 8);
  livehd::graph_util::set_pin_name(q, "m");
  livehd::semdiff::Semdiff_options options;
  options.matching_names = true;
  const auto r           = livehd::semdiff::structural_match(ref.get(), impl.get(), options);
  EXPECT_EQ(1U, r.state.name_pairs);
  EXPECT_EQ(1U, r.state.a_paired);
  EXPECT_EQ(1U, r.state.b_paired);
  EXPECT_EQ(1U, r.state.a_paired_mems);
  EXPECT_EQ(0U, r.state.b_paired_mems);
  EXPECT_EQ(0U, r.state.b_mems);
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

TEST(Semdiff, CompactLoopDescriptorParticipatesInIdentityAndDigest) {
  auto a = build_compact_loop("lgdb_semdiff_loop_a", 7);
  auto b = build_compact_loop("lgdb_semdiff_loop_b", 7);
  auto c = build_compact_loop("lgdb_semdiff_loop_c", 8);

  livehd::semdiff::Semdiff_options options;
  options.matching_names = true;
  EXPECT_TRUE(livehd::semdiff::structural_identical(a.get(), b.get(), options));
  EXPECT_FALSE(livehd::semdiff::structural_identical(a.get(), c.get(), options));

  const auto da = livehd::semdiff::canonical_digest(a.get());
  const auto db = livehd::semdiff::canonical_digest(b.get());
  const auto dc = livehd::semdiff::canonical_digest(c.get());
  ASSERT_TRUE(da.valid && db.valid && dc.valid);
  EXPECT_EQ(da, db);
  EXPECT_NE(da, dc);
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
  b->get_input_pin("a").connect_sink(setup_sink_pid(a_and, 0));
  b->get_input_pin("b").connect_sink(setup_sink_pid(a_and, 0));
  a_and.create_driver_pin(0).connect_sink(setup_sink_pid(an_or, 0));
  b->get_input_pin("c").connect_sink(setup_sink_pid(an_or, 0));
  an_or.create_driver_pin(0).connect_sink(b->get_output_pin("y"));

  auto da = livehd::semdiff::canonical_digest(a.get());
  auto db = livehd::semdiff::canonical_digest(b.get());
  EXPECT_TRUE(da.valid && db.valid);
  EXPECT_EQ(da, db);
}

// canonical_digest: a literal width or IO-name change must change the digest
// (LEC pairs IO by name). NOTE add_input's
// second arg is the PORT ID, not bits — width is the pin `bits` attribute.
TEST(Semdiff, DigestSensitiveToWidthAndIoName) {
  auto base = build_and_or("lgdb_semdiff_dw_base", "m");

  auto wg = build_and_or("lgdb_semdiff_dw_w", "m");
  livehd::graph_util::set_bits(wg->get_input_pin("a"), 2);  // widened input

  auto& ln = livehd::Hhds_graph_library::instance("lgdb_semdiff_dw_n");
  auto  gn = ln.create_io("m");
  gn->add_input("a", 1);
  gn->add_input("b", 2);
  gn->add_input("c", 3);
  gn->add_output("z", 4);  // renamed output (y -> z)
  auto ng    = gn->create_graph();
  auto n_and = create_typed_node(*ng, Ntype_op::And);
  ng->get_input_pin("a").connect_sink(setup_sink_pid(n_and, 0));
  ng->get_input_pin("b").connect_sink(setup_sink_pid(n_and, 0));
  auto n_or = create_typed_node(*ng, Ntype_op::Or);
  n_and.create_driver_pin(0).connect_sink(setup_sink_pid(n_or, 0));
  ng->get_input_pin("c").connect_sink(setup_sink_pid(n_or, 0));
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
    g->get_input_pin("d").connect_sink(setup_sink_pid(flop, 0));
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
    auto cg = cio->create_graph();
    auto op = create_typed_node(*cg, child_uses_or ? Ntype_op::Or : Ntype_op::And);
    cg->get_input_pin("x").connect_sink(setup_sink_pid(op, 0));
    cg->get_input_pin("y").connect_sink(setup_sink_pid(op, 0));
    op.create_driver_pin(0).connect_sink(cg->get_output_pin("o"));

    auto pio = lib.create_io("parent");
    pio->add_input("a", 1);
    pio->add_input("b", 1);
    pio->add_output("z", 1);
    auto pg  = pio->create_graph();
    auto sub = create_typed_node(*pg, Ntype_op::Sub);
    sub.set_subnode(cio);
    pg->get_input_pin("a").connect_sink(setup_sink_pid(sub, 0));
    pg->get_input_pin("b").connect_sink(setup_sink_pid(sub, 1));
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

// Sub_fold::interface folds a Sub as a port-shaped blackbox even WITH a resolver:
// the parent digest is insensitive to the child body (what abc region reuse
// needs), unlike the default Merkle fold. It equals the no-resolver blackbox.
TEST(Semdiff, DigestInterfaceModeIgnoresChildBody) {
  using livehd::semdiff::Sub_fold;
  auto build = [&](const std::string& dir, bool child_uses_or) {
    auto& lib = livehd::Hhds_graph_library::instance(dir);

    auto cio = lib.create_io("child");
    cio->add_input("x", 1);
    cio->add_input("y", 1);
    cio->add_output("o", 1);
    auto cg = cio->create_graph();
    auto op = create_typed_node(*cg, child_uses_or ? Ntype_op::Or : Ntype_op::And);
    cg->get_input_pin("x").connect_sink(setup_sink_pid(op, 0));
    cg->get_input_pin("y").connect_sink(setup_sink_pid(op, 0));
    op.create_driver_pin(0).connect_sink(cg->get_output_pin("o"));

    auto pio = lib.create_io("parent");
    pio->add_input("a", 1);
    pio->add_input("b", 1);
    pio->add_output("z", 1);
    auto pg  = pio->create_graph();
    auto sub = create_typed_node(*pg, Ntype_op::Sub);
    sub.set_subnode(cio);
    pg->get_input_pin("a").connect_sink(setup_sink_pid(sub, 0));
    pg->get_input_pin("b").connect_sink(setup_sink_pid(sub, 1));
    sub.create_driver_pin(0).connect_sink(pg->get_output_pin("z"));

    livehd::semdiff::Digest_resolver resolve = [&lib](hhds::Gid gid) -> hhds::Graph* {
      auto g = lib.get_graph(gid);
      return g ? g.get() : nullptr;
    };
    return std::tuple{livehd::semdiff::canonical_digest(pg.get(), resolve, Sub_fold::interface),
                      livehd::semdiff::canonical_digest(pg.get(), resolve, Sub_fold::merkle),
                      livehd::semdiff::canonical_digest(pg.get())};  // no resolver: blackbox
  };

  auto [and_if, and_mk, and_bb] = build("lgdb_semdiff_ifm_and", false);
  auto [or_if, or_mk, or_bb]    = build("lgdb_semdiff_ifm_or", true);

  EXPECT_TRUE(and_if.valid && or_if.valid);
  EXPECT_EQ(and_if, or_if);   // interface: child body invisible
  EXPECT_NE(and_mk, or_mk);   // merkle: child edit changes the parent digest
  EXPECT_EQ(and_if, and_bb);  // interface with a resolver == blackbox (no resolver)
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
  f0.set_name(n0);                                            // node name too (tolg stamps both) — the exported State_pair basis
  g->get_input_pin("d").connect_sink(setup_sink_pid(f0, 3));  // din
  auto q0 = f0.create_driver_pin(0);
  livehd::graph_util::set_pin_name(q0, n0);

  auto inv = create_typed_node(*g, Ntype_op::Not);
  q0.connect_sink(setup_sink_pid(inv, 0));

  auto f1 = create_typed_node(*g, Ntype_op::Flop);
  f1.set_name(n1);
  inv.create_driver_pin(0).connect_sink(setup_sink_pid(f1, 3));  // din
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

  // The exported pair list (the lec uncertain-pair feed): concrete raw names,
  // the true correspondence, flops not mems.
  ASSERT_EQ(2U, r.state_pairs.size());
  std::vector<std::pair<std::string, std::string>> got;
  for (const auto& p : r.state_pairs) {
    EXPECT_FALSE(p.is_mem);
    got.emplace_back(p.a_name, p.b_name);
  }
  std::sort(got.begin(), got.end());
  EXPECT_EQ(got[0], (std::pair<std::string, std::string>{"ra", "xa"}));
  EXPECT_EQ(got[1], (std::pair<std::string, std::string>{"rb", "xb"}));
  EXPECT_TRUE(r.a_state_unpaired.empty());
  EXPECT_TRUE(r.b_state_unpaired.empty());

  // The paired seeds let the WHOLE structure match: nothing left unmatched.
  EXPECT_EQ(0U, r.a_unmatched);
  EXPECT_EQ(0U, r.b_unmatched);
}

TEST(Semdiff, EscapedVerilogStateNameUsesCanonicalIdentity) {
  auto a = build_pipe2("lgdb_semdiff_escaped_a", "bank.x", "bank.y");
  auto b = build_pipe2("lgdb_semdiff_escaped_b", "`bank.x`", "`bank.y`");

  livehd::semdiff::Semdiff_options options;
  options.matching_names = true;
  auto r                 = livehd::semdiff::structural_match(a.get(), b.get(), options);
  EXPECT_EQ(2U, r.state.name_pairs);
  EXPECT_EQ(0U, r.state.full_pairs);
  EXPECT_EQ(0U, r.state.a_unpaired);
  EXPECT_EQ(0U, r.state.b_unpaired);
}

// cgen's reversible memory wrapper storage (`__lhdmem_h<hex>_e.data`, or the
// inline `_e_data` register) names its source state: it pairs by NAME with the
// golden's `mem`, with no structural signature needed.
TEST(Semdiff, CgenMemoryStorageNamePairsWithItsSource) {
  auto a = build_pipe2("lgdb_semdiff_cgenmem_a", "mem", "lane.mem");
  auto b = build_pipe2("lgdb_semdiff_cgenmem_b", "__lhdmem_h6d656d_e.data", "lane.__lhdmem_h6d656d_e_data");

  livehd::semdiff::Semdiff_options options;
  options.matching_names = true;
  auto r                 = livehd::semdiff::structural_match(a.get(), b.get(), options);
  EXPECT_EQ(2U, r.state.name_pairs);
  EXPECT_EQ(0U, r.state.a_unpaired);
  EXPECT_EQ(0U, r.state.b_unpaired);
}

// A `__flat___` instance component is not logical hierarchy (graph/README.md):
// foo.__flat___region.bar.x names the same state as foo.bar.x, while an
// ordinary `region` level is a different name.
TEST(Semdiff, TransparentInstanceComponentsNormalizeStateNames) {
  auto flat = build_pipe2("lgdb_semdiff_tr_flat", "foo.bar.x", "foo.bar.y");
  auto tr   = build_pipe2("lgdb_semdiff_tr_tr", "foo.__flat___region.bar.x", "foo.__flat___region.bar.y");
  auto ord  = build_pipe2("lgdb_semdiff_tr_ord", "foo.region.bar.x", "foo.region.bar.y");

  livehd::semdiff::Semdiff_options o;
  o.matching_names = true;
  EXPECT_TRUE(livehd::semdiff::structural_identical(flat.get(), tr.get(), o));
  EXPECT_EQ(livehd::semdiff::canonical_digest(flat.get()), livehd::semdiff::canonical_digest(tr.get()));
  EXPECT_FALSE(livehd::semdiff::structural_identical(flat.get(), ord.get(), o));
  EXPECT_NE(livehd::semdiff::canonical_digest(flat.get()), livehd::semdiff::canonical_digest(ord.get()));
}

// top: two instances of `lane` (s = ~a) on d0/d1, q = s0 - s1 (or s1 - s0 when
// `swap`). `n0`/`n1` name the instances ("" leaves one anonymous).
std::shared_ptr<hhds::Graph> build_two_lanes(const std::string& dir, const std::string& n0, const std::string& n1, bool swap) {
  auto& lib = livehd::Hhds_graph_library::instance(dir);
  auto  cio = lib.create_io("lane");
  cio->add_input("a", 0);
  cio->add_output("s", 1);
  auto cg  = cio->create_graph();
  auto inv = create_typed_node(*cg, Ntype_op::Not);
  cg->get_input_pin("a").connect_sink(setup_sink_pid(inv, 0));
  inv.create_driver_pin(0).connect_sink(cg->get_output_pin("s"));

  auto pio = lib.create_io("top");
  pio->add_input("d0", 0);
  pio->add_input("d1", 1);
  pio->add_output("q", 2);
  auto            pg = pio->create_graph();
  hhds::Pin_class s[2];
  for (int i = 0; i < 2; ++i) {
    auto sub = create_typed_node(*pg, Ntype_op::Sub);
    sub.set_subnode(cio);
    if (const auto& name = i == 0 ? n0 : n1; !name.empty()) {
      sub.set_name(name);
    }
    pg->get_input_pin(i == 0 ? "d0" : "d1").connect_sink(setup_sink_pid(sub, 0));
    s[i] = sub.create_driver_pin(1);
  }
  auto sum = create_typed_node(*pg, Ntype_op::Sum);
  s[swap ? 1 : 0].connect_sink(livehd::graph_util::setup_sink_by_name(sum, "as"));
  s[swap ? 0 : 1].connect_sink(livehd::graph_util::setup_sink_by_name(sum, "bs"));
  sum.create_driver_pin(0).connect_sink(pg->get_output_pin("q"));
  return pg;
}

// Sibling cut Subs are told apart by their OWN instance names, even transparent
// `__flat___` ones (dropping them keyed every sibling "u:" and the swapped
// operands proved PROVEN with no solver). Siblings that genuinely share a key
// (two anonymous instances of one def) must stay undecidable, never merged.
TEST(Semdiff, TransparentSiblingInstancesKeepDistinctCutKeys) {
  livehd::semdiff::Semdiff_options o;
  o.matching_names = true;
  o.blackbox_subs  = true;

  auto a  = build_two_lanes("lgdb_semdiff_tsib_a", "__flat___a", "__flat___b", false);
  auto a2 = build_two_lanes("lgdb_semdiff_tsib_a2", "__flat___a", "__flat___b", false);
  auto b  = build_two_lanes("lgdb_semdiff_tsib_b", "__flat___a", "__flat___b", true);
  EXPECT_TRUE(livehd::semdiff::structural_identical(a.get(), a2.get(), o));
  EXPECT_FALSE(livehd::semdiff::structural_identical(a.get(), b.get(), o));

  auto anon  = build_two_lanes("lgdb_semdiff_tsib_anon", "", "", false);
  auto anonb = build_two_lanes("lgdb_semdiff_tsib_anonb", "", "", true);
  EXPECT_FALSE(livehd::semdiff::structural_identical(anon.get(), anonb.get(), o));
  // ...but an UNCHANGED copy with two anonymous instances of one def (extracted
  // ware modules) is identical: they are signed structurally, not by a shared
  // def-name cut key whose obligations collided (a comment-only false miss).
  auto anon2 = build_two_lanes("lgdb_semdiff_tsib_anon2", "", "", false);
  EXPECT_TRUE(livehd::semdiff::structural_identical(anon.get(), anon2.get(), o));
}

// A mapped netlist keeps its registers inside Subs (Liberty flops, mapped
// regions), so a Q -> logic -> D loop runs through an instance. Digests
// seed a named instance -- or the only anonymous instance of its def -- as a
// cut point; before, the stalled loop left the native memory/flop operand
// unsigned and every mapped netlist was undigestable (STA never reused).
TEST(Semdiff, DigestBreaksLoopsThroughInstances) {
  using livehd::semdiff::Sub_fold;
  auto build = [](const std::string& dir, Ntype_op op, const std::string& inst) {
    auto& lib = livehd::Hhds_graph_library::instance(dir);
    auto  cio = lib.create_io("cell");
    cio->add_input("a", 1);
    cio->add_output("y", 2);  // bodyless: a Liberty leaf cell
    auto pio = lib.create_io("top");
    pio->add_input("d", 1);
    pio->add_output("q", 2);
    auto pg  = pio->create_graph();
    auto sub = create_typed_node(*pg, Ntype_op::Sub);
    sub.set_subnode(cio);
    if (!inst.empty()) {
      sub.set_name(inst);
    }
    auto gate = create_typed_node(*pg, op);  // the loop: sub.y -> gate -> sub.a
    sub.create_driver_pin(2).connect_sink(setup_sink_pid(gate, 0));
    pg->get_input_pin("d").connect_sink(setup_sink_pid(gate, 0));
    gate.create_driver_pin(0).connect_sink(setup_sink_pid(sub, 1));
    auto flop = create_typed_node(*pg, Ntype_op::Flop);
    flop.set_name("r");
    gate.create_driver_pin(0).connect_sink(setup_sink_pid(flop, 3));  // din
    auto q = flop.create_driver_pin(0);
    livehd::graph_util::set_pin_name(q, "r");
    q.connect_sink(pg->get_output_pin("q"));
    livehd::semdiff::Digest_resolver none = [](hhds::Gid) -> hhds::Graph* { return nullptr; };
    return livehd::semdiff::canonical_digest(pg.get(), none, Sub_fold::merkle);
  };
  const auto a  = build("lgdb_semdiff_loopi_a", Ntype_op::And, "u0");
  const auto a2 = build("lgdb_semdiff_loopi_a2", Ntype_op::And, "u0");
  const auto o  = build("lgdb_semdiff_loopi_o", Ntype_op::Or, "u0");
  const auto an = build("lgdb_semdiff_loopi_an", Ntype_op::And, "");
  EXPECT_TRUE(a.valid && a2.valid && o.valid && an.valid);
  EXPECT_EQ(a, a2);
  EXPECT_NE(a, o);   // the loop logic still reaches the digest
  EXPECT_NE(a, an);  // instance identity is part of it
}

TEST(Semdiff, AggregateProvenanceLossKeepsPhysicalLeafIdentity) {
  auto make = [](const std::string& dir, bool keep_provenance) {
    auto& lib = livehd::Hhds_graph_library::instance(dir);
    auto  gio = lib.create_io("m");
    auto  g   = gio->create_graph();
    for (int lane = 0; lane < 4; ++lane) {
      const auto canonical = std::format("bank.e{}", lane);
      auto       flop      = create_typed_node(*g, Ntype_op::Flop);
      flop.set_name(keep_provenance ? canonical : std::format("`{}`", canonical));
      auto q = flop.create_driver_pin(0);
      livehd::graph_util::set_bits(q, 8);
      livehd::graph_util::set_pin_name(q, keep_provenance ? canonical : std::format("`{}`", canonical));
      if (keep_provenance) {
        flop.attr(livehd::attrs::aggregate_origin).set("bank");
        flop.attr(livehd::attrs::aggregate_source_index).set(lane);
        flop.attr(livehd::attrs::aggregate_lane_ordinal).set(lane);
        flop.attr(livehd::attrs::aggregate_bit_offset).set(lane * 8);
        flop.attr(livehd::attrs::aggregate_bit_width).set(8);
        flop.attr(livehd::attrs::aggregate_extent).set(4);
      }
    }
    return g;
  };

  auto ref  = make("lgdb_semdiff_aggregate_provenance_ref", true);
  auto impl = make("lgdb_semdiff_aggregate_provenance_impl", false);

  livehd::semdiff::Semdiff_options options;
  options.matching_names = true;
  auto r                 = livehd::semdiff::structural_match(ref.get(), impl.get(), options);
  EXPECT_EQ(1U, r.state.a_total);
  EXPECT_EQ(4U, r.state.b_total);
  EXPECT_EQ(1U, r.state.a_paired);
  EXPECT_EQ(4U, r.state.b_paired);
  EXPECT_EQ(1U, r.state.name_pairs);
  EXPECT_EQ(0U, r.state.full_pairs);
  EXPECT_EQ(0U, r.state.a_unpaired);
  EXPECT_EQ(0U, r.state.b_unpaired);
  EXPECT_EQ(0U, r.a_unmatched);
  EXPECT_EQ(0U, r.b_unmatched);
}

TEST(Semdiff, SroaRegisterLeavesReaggregateForNamePairing) {
  auto make = [](const std::string& dir, bool split) {
    auto& lib = livehd::Hhds_graph_library::instance(dir);
    auto  gio = lib.create_io("m");
    auto  g   = gio->create_graph();
    if (!split) {
      auto flop = create_typed_node(*g, Ntype_op::Flop);
      auto q    = flop.create_driver_pin(0);
      livehd::graph_util::set_bits(q, 32);
      livehd::graph_util::set_pin_name(q, "bank");
      return g;
    }
    for (int lane = 0; lane < 4; ++lane) {
      auto flop = create_typed_node(*g, Ntype_op::Flop);
      auto q    = flop.create_driver_pin(0);
      livehd::graph_util::set_bits(q, 8);
      livehd::graph_util::set_pin_name(q, std::format("bank.e{}", lane));
      flop.attr(livehd::attrs::aggregate_origin).set("bank");
      flop.attr(livehd::attrs::aggregate_source_index).set(lane);
      flop.attr(livehd::attrs::aggregate_lane_ordinal).set(lane);
      flop.attr(livehd::attrs::aggregate_bit_offset).set(lane * 8);
      flop.attr(livehd::attrs::aggregate_bit_width).set(8);
      flop.attr(livehd::attrs::aggregate_extent).set(4);
    }
    return g;
  };

  auto ref  = make("lgdb_semdiff_sroa_ref", false);
  auto impl = make("lgdb_semdiff_sroa_impl", true);

  livehd::semdiff::Semdiff_options options;
  options.matching_names = true;
  auto r                 = livehd::semdiff::structural_match(ref.get(), impl.get(), options);
  EXPECT_EQ(1U, r.state.a_total);
  EXPECT_EQ(1U, r.state.b_total);
  EXPECT_EQ(1U, r.state.name_pairs);
  EXPECT_EQ(0U, r.state.a_unpaired);
  EXPECT_EQ(0U, r.state.b_unpaired);
}

// A netlist read back from Verilog has lost the aggregate_* attributes, but
// its per-bit cells follow the bus-expansion standard (core/bus_name.hpp):
// `bank[i]`, or `bank[i].flop_16` with the Liberty cell model inlined. The
// names alone regroup them into the reference's one wide `bank` -- and only
// when the regroup is complete, unique and width-consistent.
TEST(Semdiff, BusBitNamesReaggregateWithoutProvenance) {
  auto make = [](const std::string& dir, int wide_bits, const std::vector<std::string>& bit_names) {
    auto& lib = livehd::Hhds_graph_library::instance(dir);
    auto  gio = lib.create_io("m");
    auto  g   = gio->create_graph();
    if (wide_bits > 0) {
      auto flop = create_typed_node(*g, Ntype_op::Flop);
      auto q    = flop.create_driver_pin(0);
      livehd::graph_util::set_bits(q, wide_bits);
      livehd::graph_util::set_pin_name(q, "bank");
    }
    for (const auto& nm : bit_names) {
      auto flop = create_typed_node(*g, Ntype_op::Flop);
      auto q    = flop.create_driver_pin(0);
      livehd::graph_util::set_bits(q, 1);
      livehd::graph_util::set_pin_name(q, nm);
    }
    return g;
  };
  auto bits = [](int n, std::string_view suffix) {
    std::vector<std::string> v;
    for (int i = 0; i < n; ++i) {
      v.push_back(std::format("bank[{}]{}", i, suffix));
    }
    return v;
  };
  auto run = [](hhds::Graph* ref, hhds::Graph* impl) {
    livehd::semdiff::Semdiff_options o;
    o.matching_names = true;
    o.state_pairing  = true;
    return livehd::semdiff::structural_match(ref, impl, o);
  };

  {  // standard names and the inlined-model spelling both regroup
    for (std::string_view suffix : {"", ".flop_16"}) {
      auto ref  = make(std::format("lgdb_semdiff_busbit_ref{}", suffix.size()), 4, {});
      auto impl = make(std::format("lgdb_semdiff_busbit_impl{}", suffix.size()), 0, bits(4, suffix));
      auto r    = run(ref.get(), impl.get());
      EXPECT_EQ(1U, r.state.b_total) << suffix;
      EXPECT_EQ(1U, r.state.name_pairs) << suffix;
      EXPECT_EQ(0U, r.state.a_unpaired) << suffix;
      EXPECT_EQ(0U, r.state.b_unpaired) << suffix;
    }
  }
  {  // width-inconsistent: 3 bit cells against a 4-bit register stay unpaired
    auto ref  = make("lgdb_semdiff_busbit_w_ref", 4, {});
    auto impl = make("lgdb_semdiff_busbit_w_impl", 0, bits(3, ""));
    auto r    = run(ref.get(), impl.get());
    EXPECT_EQ(0U, r.state.name_pairs);
    EXPECT_EQ(1U, r.state.a_unpaired);
    EXPECT_EQ(3U, r.state.b_unpaired);
  }
  {  // ambiguous: bit 1 appears twice (two state elements in one bit's cell)
    auto names = bits(4, ".flop_16");
    names.push_back("bank[1].flop_17");
    auto ref  = make("lgdb_semdiff_busbit_d_ref", 4, {});
    auto impl = make("lgdb_semdiff_busbit_d_impl", 0, names);
    auto r    = run(ref.get(), impl.get());
    EXPECT_EQ(0U, r.state.name_pairs);
    EXPECT_EQ(1U, r.state.a_unpaired);
    EXPECT_EQ(5U, r.state.b_unpaired);
  }
  {  // a gap (bit 2 missing, bit 4 present) is no regroup either
    auto names = bits(5, "");
    names.erase(names.begin() + 2);
    auto ref  = make("lgdb_semdiff_busbit_g_ref", 4, {});
    auto impl = make("lgdb_semdiff_busbit_g_impl", 0, names);
    auto r    = run(ref.get(), impl.get());
    EXPECT_EQ(0U, r.state.name_pairs);
    EXPECT_EQ(4U, r.state.b_unpaired);
  }
  {  // per-bit names on BOTH sides keep their exact 1:1 pairs
    auto ref  = make("lgdb_semdiff_busbit_s_ref", 0, bits(4, ""));
    auto impl = make("lgdb_semdiff_busbit_s_impl", 0, bits(4, ""));
    auto r    = run(ref.get(), impl.get());
    EXPECT_EQ(4U, r.state.name_pairs);
    EXPECT_EQ(0U, r.state.b_unpaired);
  }
  {  // an unsplit one-bit register read back through its cell model, also
     // behind cgen's `_cgen<N>` collision uniquifier
    int k = 0;
    for (const char* impl_name : {"bank.flop_16", "bank_cgen1.IQ"}) {
      auto ref  = make(std::format("lgdb_semdiff_busbit_l_ref{}", k), 1, {});
      auto impl = make(std::format("lgdb_semdiff_busbit_l_impl{}", k++), 0, {impl_name});
      auto r    = run(ref.get(), impl.get());
      EXPECT_EQ(1U, r.state.name_pairs) << impl_name;
      EXPECT_EQ(0U, r.state.b_unpaired) << impl_name;
    }
  }
  {  // ...but not two state elements under one owner, nor a wider register
    auto ref  = make("lgdb_semdiff_busbit_l2_ref", 1, {});
    auto impl = make("lgdb_semdiff_busbit_l2_impl", 0, {"bank.flop_16", "bank.flop_17"});
    auto r    = run(ref.get(), impl.get());
    EXPECT_EQ(0U, r.state.name_pairs);
    auto ref4  = make("lgdb_semdiff_busbit_l4_ref", 4, {});
    auto impl4 = make("lgdb_semdiff_busbit_l4_impl", 0, {"bank.flop_16"});
    auto r4    = run(ref4.get(), impl4.get());
    EXPECT_EQ(0U, r4.state.name_pairs);
  }
}

// Two one-bit registers driving q1/q2: `gname` latches input a and `hname`
// input b (crossed when `swap`). `g_init` optionally ties an initial (reset)
// constant to the first one. The state names are what the test varies: `g`
// on the ref, `g.l` / `g_cgen1.l` (core/bus_name cell_state_owner) on the impl.
std::shared_ptr<hhds::Graph> build_named_regs(const std::string& dir, const std::string& gname, const std::string& hname, bool swap,
                                              std::optional<int64_t> g_init = std::nullopt) {
  auto& lib = livehd::Hhds_graph_library::instance(dir);
  auto  gio = lib.create_io("t");
  gio->add_input("a", 1);
  gio->add_input("b", 2);
  gio->add_output("q1", 3);
  gio->add_output("q2", 4);
  auto g  = gio->create_graph();
  auto mk = [&](const std::string& nm, const char* din, const char* out, std::optional<int64_t> init) {
    auto f = create_typed_node(*g, Ntype_op::Flop);
    f.set_name(nm);
    g->get_input_pin(din).connect_sink(setup_sink_pid(f, 3));  // din
    if (init) {
      livehd::graph_util::create_const(*g, *Dlop::create_integer(*init)).connect_sink(setup_sink_pid(f, 1));  // initial
    }
    auto q = f.create_driver_pin(0);
    livehd::graph_util::set_bits(q, 1);
    livehd::graph_util::set_pin_name(q, nm);
    q.connect_sink(g->get_output_pin(out));
  };
  mk(gname, swap ? "b" : "a", "q1", g_init);
  mk(hname, swap ? "a" : "b", "q2", std::nullopt);
  return g;
}

// A bus_name-reconstructed pair (`g` vs the read-back cell state `g.l`) is a
// HINT, so its din/initial obligation must still be checked. The obligations
// used to be keyed by the RAW names (`n:g` vs `n:g.l`): both one-sided, both
// skipped, while the renamed key made the pair a certain tier-1 match and the
// node sets a bijection. Swapped dins, or a different reset constant, then
// read as structurally identical and lec's no-solver skip PROVED them.
TEST(Semdiff, ReconstructedStateNameKeepsItsObligation) {
  livehd::semdiff::Semdiff_options o;
  o.matching_names = true;
  int k            = 0;
  for (const auto& [gl, hl] : std::initializer_list<std::pair<const char*, const char*>>{
           {      "g.l",       "h.l"},
           {"g_cgen1.l", "h_cgen2.l"}
  }) {
    const auto dir = [&](std::string_view tag) { return std::format("lgdb_semdiff_recon_{}_{}", tag, k); };
    auto       ref = build_named_regs(dir("ref"), "g", "h", false);
    auto       ok  = build_named_regs(dir("ok"), gl, hl, false);
    auto       sw  = build_named_regs(dir("sw"), gl, hl, true);
    ++k;

    auto rok = livehd::semdiff::structural_match(ref.get(), ok.get(), o);
    EXPECT_EQ(2U, rok.state.name_pairs) << gl;
    EXPECT_EQ(2U, rok.state.name_reconstructed) << gl;
    EXPECT_EQ(0U, rok.cut_violated) << gl;
    EXPECT_EQ(0U, rok.cut_unknown) << gl;
    EXPECT_TRUE(livehd::semdiff::is_structural_identity(rok)) << gl;  // a VERIFIED hint stays fast
    EXPECT_TRUE(livehd::semdiff::structural_identical(ref.get(), ok.get(), o)) << gl;

    auto rsw = livehd::semdiff::structural_match(ref.get(), sw.get(), o);
    EXPECT_EQ(2U, rsw.state.name_reconstructed) << gl;
    EXPECT_EQ(0U, rsw.a_unmatched) << gl;  // the node sets alone cannot see the swap...
    EXPECT_EQ(0U, rsw.b_unmatched) << gl;
    EXPECT_EQ(2U, rsw.cut_violated) << gl;  // ...the paired obligations do
    ASSERT_EQ(2U, rsw.cut_violations.size()) << gl;
    std::vector<std::string> v = rsw.cut_violations;
    std::sort(v.begin(), v.end());
    EXPECT_EQ(v[0], std::format("n:g <-> n:g [n:{}]", gl));  // names the ref key AND the impl's own cell
    EXPECT_FALSE(livehd::semdiff::is_structural_identity(rsw)) << gl;
    EXPECT_FALSE(livehd::semdiff::structural_identical(ref.get(), sw.get(), o)) << gl;
  }

  // Same structure, same names, only the reset constant differs.
  auto ref0 = build_named_regs("lgdb_semdiff_recon_init_ref", "g", "h", false, 0);
  auto ok0  = build_named_regs("lgdb_semdiff_recon_init_ok", "g.l", "h.l", false, 0);
  auto bad1 = build_named_regs("lgdb_semdiff_recon_init_bad", "g.l", "h.l", false, 1);
  EXPECT_TRUE(livehd::semdiff::structural_identical(ref0.get(), ok0.get(), o));
  auto rb = livehd::semdiff::structural_match(ref0.get(), bad1.get(), o);
  EXPECT_EQ(2U, rb.state.name_pairs);
  EXPECT_EQ(1U, rb.cut_violated);
  EXPECT_FALSE(livehd::semdiff::is_structural_identity(rb));
  EXPECT_FALSE(livehd::semdiff::structural_identical(ref0.get(), bad1.get(), o));
}

// The physical-name bridge (aggregate provenance on one side only: the direct
// Pyrope graph vs its Verilog read-back) pairs `bank.e0` with `bank.e0` by a
// token, while each side's raw key differs (`n:bank` from aggregate_origin vs
// `n:bank.e0`). Its obligations must follow the token, or two lanes whose dins
// were swapped by the emitter match with nothing checked.
TEST(Semdiff, PhysicalBridgePairKeepsItsObligation) {
  auto make = [](const std::string& dir, bool keep_provenance, bool swap) {
    auto& lib = livehd::Hhds_graph_library::instance(dir);
    auto  gio = lib.create_io("m");
    gio->add_input("d0", 1);
    gio->add_input("d1", 2);
    gio->add_output("q0", 3);
    gio->add_output("q1", 4);
    auto g = gio->create_graph();
    for (int lane = 0; lane < 2; ++lane) {
      const auto canonical = std::format("bank.e{}", lane);
      const auto spelled   = keep_provenance ? canonical : std::format("`{}`", canonical);
      auto       flop      = create_typed_node(*g, Ntype_op::Flop);
      flop.set_name(spelled);
      g->get_input_pin((lane == 0) != swap ? "d0" : "d1").connect_sink(setup_sink_pid(flop, 3));
      auto q = flop.create_driver_pin(0);
      livehd::graph_util::set_bits(q, 8);
      livehd::graph_util::set_pin_name(q, spelled);
      q.connect_sink(g->get_output_pin(lane == 0 ? "q0" : "q1"));
      if (keep_provenance) {
        flop.attr(livehd::attrs::aggregate_origin).set("bank");
        flop.attr(livehd::attrs::aggregate_source_index).set(lane);
        flop.attr(livehd::attrs::aggregate_lane_ordinal).set(lane);
        flop.attr(livehd::attrs::aggregate_bit_offset).set(lane * 8);
        flop.attr(livehd::attrs::aggregate_bit_width).set(8);
        flop.attr(livehd::attrs::aggregate_extent).set(2);
      }
    }
    return g;
  };
  auto ref = make("lgdb_semdiff_physob_ref", true, false);
  auto ok  = make("lgdb_semdiff_physob_ok", false, false);
  auto sw  = make("lgdb_semdiff_physob_sw", false, true);

  livehd::semdiff::Semdiff_options o;
  o.matching_names = true;
  auto rok         = livehd::semdiff::structural_match(ref.get(), ok.get(), o);
  EXPECT_EQ(1U, rok.state.name_pairs);  // the bridged aggregate
  EXPECT_EQ(0U, rok.state.name_reconstructed);
  EXPECT_EQ(0U, rok.cut_violated);
  EXPECT_EQ(0U, rok.cut_unknown);
  EXPECT_TRUE(livehd::semdiff::is_structural_identity(rok));

  auto rsw = livehd::semdiff::structural_match(ref.get(), sw.get(), o);
  EXPECT_EQ(0U, rsw.a_unmatched);
  EXPECT_EQ(0U, rsw.b_unmatched);
  EXPECT_EQ(2U, rsw.cut_violated);
  EXPECT_FALSE(livehd::semdiff::is_structural_identity(rsw));
  EXPECT_FALSE(livehd::semdiff::structural_identical(ref.get(), sw.get(), o));
}

TEST(Semdiff, SroaStructArrayLeavesReaggregateForNamePairing) {
  auto make = [](const std::string& dir, bool split) {
    auto& lib = livehd::Hhds_graph_library::instance(dir);
    auto  gio = lib.create_io("m");
    auto  g   = gio->create_graph();
    if (!split) {
      auto flop = create_typed_node(*g, Ntype_op::Flop);
      auto q    = flop.create_driver_pin(0);
      livehd::graph_util::set_bits(q, 36);
      livehd::graph_util::set_pin_name(q, "entries");
      return g;
    }
    for (int lane = 0; lane < 4; ++lane) {
      for (const auto& [field, field_offset, field_width] : std::initializer_list<std::tuple<std::string_view, int, int>>{
               { "data", 0, 8},
               {"valid", 8, 1}
      }) {
        auto flop = create_typed_node(*g, Ntype_op::Flop);
        auto q    = flop.create_driver_pin(0);
        livehd::graph_util::set_bits(q, field_width);
        livehd::graph_util::set_pin_name(q, std::format("entries.e{}.{}", lane, field));
        flop.attr(livehd::attrs::aggregate_origin).set("entries");
        flop.attr(livehd::attrs::aggregate_source_index).set(lane);
        flop.attr(livehd::attrs::aggregate_lane_ordinal).set(lane);
        flop.attr(livehd::attrs::aggregate_bit_offset).set(lane * 9 + field_offset);
        flop.attr(livehd::attrs::aggregate_bit_width).set(field_width);
        flop.attr(livehd::attrs::aggregate_extent).set(4);
      }
    }
    return g;
  };

  auto ref  = make("lgdb_semdiff_sroa_struct_ref", false);
  auto impl = make("lgdb_semdiff_sroa_struct_impl", true);

  livehd::semdiff::Semdiff_options options;
  options.matching_names = true;
  auto r                 = livehd::semdiff::structural_match(ref.get(), impl.get(), options);
  EXPECT_EQ(1U, r.state.a_total);
  EXPECT_EQ(1U, r.state.b_total);
  EXPECT_EQ(1U, r.state.name_pairs);
  EXPECT_EQ(0U, r.state.a_unpaired);
  EXPECT_EQ(0U, r.state.b_unpaired);
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
      g->get_input_pin("d").connect_sink(setup_sink_pid(f, 3));
      auto q = f.create_driver_pin(0);
      livehd::graph_util::set_pin_name(q, nm);
      q.connect_sink(setup_sink_pid(an_or, 0));
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

  // The unpaired report carries the reason.
  ASSERT_EQ(2U, r.a_state_unpaired.size());
  for (const auto& s : r.a_state_unpaired) {
    EXPECT_NE(s.find("(ambiguous)"), std::string::npos) << s;
  }
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
    g->get_input_pin("d").connect_sink(setup_sink_pid(f, 3));
    auto cval = Dlop::create_integer(init);
    auto c    = livehd::graph_util::create_const(*g, *cval);
    c.connect_sink(setup_sink_pid(f, 1));  // initial
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
  EXPECT_TRUE(r.state_pairs.empty());

  // The refusal names its reason: same structure, different kind/init fold.
  ASSERT_EQ(1U, r.a_state_unpaired.size());
  EXPECT_NE(r.a_state_unpaired[0].find("(kind/init mismatch)"), std::string::npos) << r.a_state_unpaired[0];
}

// A WIDTH difference must NOT refuse the pair. Independently imported
// implementations can give the same state element different valid declared
// widths. Folding width into the pair precondition leaves the flops as free,
// independent power-on symbols and can refute at step 1 on the initial value
// alone -- a false REFUTED when the observable state is equivalent.
//
// Sound because the miter crosses widths already: query.cpp shares ONE symbol
// per cut at the MIN width and Encoder::seed_state extends it to each side with
// SIGN_EXTEND/ZERO_EXTEND per that side's signedness. The high bits are derived,
// not assumed equal.
TEST(Semdiff, StatePairingWidthMismatchPairs) {
  auto with_bits = [](const std::string& dir, const std::string& nm, int bits) {
    auto& lib = livehd::Hhds_graph_library::instance(dir);
    auto  gio = lib.create_io("m");
    gio->add_input("d", bits);
    gio->add_output("q", bits);
    auto g = gio->create_graph();
    auto f = create_typed_node(*g, Ntype_op::Flop);
    g->get_input_pin("d").connect_sink(setup_sink_pid(f, 3));
    auto q = f.create_driver_pin(0);
    livehd::graph_util::set_bits(q, bits);
    livehd::graph_util::set_pin_name(q, nm);
    q.connect_sink(g->get_output_pin("q"));
    return g;
  };
  auto a = with_bits("lgdb_semdiff_wm_a", "ra", 8);
  auto b = with_bits("lgdb_semdiff_wm_b", "xa", 9);  // renamed AND one bit wider

  livehd::semdiff::Semdiff_options o;
  o.matching_names = true;
  o.state_pairing  = true;
  auto r           = livehd::semdiff::structural_match(a.get(), b.get(), o);

  EXPECT_EQ(1U, r.state.full_pairs);
  EXPECT_EQ(0U, r.state.a_unpaired);
  EXPECT_EQ(0U, r.state.b_unpaired);
}

// ...but a MEMORY keeps its width in the identity. The shared state is a cvc5
// ARRAY built from the exact `size x bits` shape and there is no extension path
// for an array element, so two memories of different data width are genuinely
// different state and must still refuse.
TEST(Semdiff, StatePairingMemoryWidthMismatchRefuses) {
  auto with_bits = [](const std::string& dir, const std::string& nm, int bits) {
    auto& lib = livehd::Hhds_graph_library::instance(dir);
    auto  gio = lib.create_io("m");
    gio->add_input("d", bits);
    gio->add_output("q", bits);
    auto g = gio->create_graph();
    auto m = create_typed_node(*g, Ntype_op::Memory);
    g->get_input_pin("d").connect_sink(setup_sink_pid(m, 4));
    auto q = m.create_driver_pin(0);
    livehd::graph_util::set_bits(q, bits);
    livehd::graph_util::set_pin_name(q, nm);
    q.connect_sink(g->get_output_pin("q"));
    return g;
  };
  auto a = with_bits("lgdb_semdiff_mwm_a", "ma", 8);
  auto b = with_bits("lgdb_semdiff_mwm_b", "mx", 9);

  livehd::semdiff::Semdiff_options o;
  o.matching_names = true;
  o.state_pairing  = true;
  auto r           = livehd::semdiff::structural_match(a.get(), b.get(), o);

  EXPECT_EQ(0U, r.state.full_pairs);
}

// Every memory PORT feeds the tier-2 signature: port k's pins sit at
// k*Memory_port_stride + role, so a second write port's data must count too.
// Two renamed memories that differ only in what their port 1 writes are
// different state and must stay unpaired; the same pair with equal ports pairs.
TEST(Semdiff, StatePairingMemorySecondPortCounts) {
  auto two_port = [](const std::string& dir, const std::string& nm, const std::string& port1_din) {
    auto& lib = livehd::Hhds_graph_library::instance(dir);
    auto  gio = lib.create_io("m");
    gio->add_input("d0", 1);
    gio->add_input("d1", 2);
    gio->add_output("q", 3);
    auto g = gio->create_graph();
    auto m = create_typed_node(*g, Ntype_op::Memory);
    g->get_input_pin("d0").connect_sink(setup_sink_pid(m, 4));
    g->get_input_pin(port1_din).connect_sink(setup_sink_pid(m, Ntype::Memory_port_stride + 4));
    auto q = m.create_driver_pin(0);
    livehd::graph_util::set_bits(q, 8);
    livehd::graph_util::set_pin_name(q, nm);
    q.connect_sink(g->get_output_pin("q"));
    return g;
  };
  livehd::semdiff::Semdiff_options o;
  o.matching_names = true;
  o.state_pairing  = true;

  auto a      = two_port("lgdb_semdiff_mp_a", "ma", "d1");
  auto same   = two_port("lgdb_semdiff_mp_b", "mx", "d1");
  auto paired = livehd::semdiff::structural_match(a.get(), same.get(), o);
  EXPECT_EQ(1U, paired.state.full_pairs);

  auto differ   = two_port("lgdb_semdiff_mp_c", "mx", "d0");
  auto unpaired = livehd::semdiff::structural_match(a.get(), differ.get(), o);
  EXPECT_EQ(0U, unpaired.state.full_pairs);
  EXPECT_EQ(1U, unpaired.state.a_unpaired);
  EXPECT_EQ(1U, unpaired.state.b_unpaired);
}

// Caller-supplied seed pairs (lec.match) are tier-1 anchors: the seeded pair
// resolves without a signature, and the remaining renamed flop full-matches
// against sharper (seed-anchored) signatures.
TEST(Semdiff, StatePairingSeedPairsAnchor) {
  auto a = build_pipe2("lgdb_semdiff_seed_a", "ra", "rb");
  auto b = build_pipe2("lgdb_semdiff_seed_b", "xa", "xb");

  livehd::semdiff::Semdiff_options o;
  o.matching_names = true;
  o.state_pairing  = true;
  o.seed_pairs.emplace_back("ra", "xa");
  auto r = livehd::semdiff::structural_match(a.get(), b.get(), o);

  EXPECT_EQ(1U, r.state.seed_pairs);
  EXPECT_EQ(1U, r.state.full_pairs);  // only rb<->xb is left for tier-2
  EXPECT_EQ(0U, r.state.a_unpaired);
  EXPECT_EQ(0U, r.state.b_unpaired);
  ASSERT_EQ(1U, r.state_pairs.size());
  EXPECT_EQ(r.state_pairs[0].a_name, "rb");
  EXPECT_EQ(r.state_pairs[0].b_name, "xb");
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
  auto& la   = livehd::Hhds_graph_library::instance("lgdb_semdiff_dv_a");
  auto  gioa = la.create_io("m");
  gioa->add_input("a", 1);
  gioa->add_input("b", 1);
  gioa->add_output("y", 1);
  auto a    = gioa->create_graph();
  auto a_op = create_typed_node(*a, Ntype_op::And);
  a->get_input_pin("a").connect_sink(setup_sink_pid(a_op, 0));
  a->get_input_pin("b").connect_sink(setup_sink_pid(a_op, 0));
  a_op.create_driver_pin(0).connect_sink(a->get_output_pin("y"));

  auto& lb   = livehd::Hhds_graph_library::instance("lgdb_semdiff_dv_b");
  auto  giob = lb.create_io("m");
  giob->add_input("a", 1);
  giob->add_input("b", 1);
  giob->add_output("y", 1);
  auto b    = giob->create_graph();
  auto b_op = create_typed_node(*b, Ntype_op::Or);
  b->get_input_pin("a").connect_sink(setup_sink_pid(b_op, 0));
  b->get_input_pin("b").connect_sink(setup_sink_pid(b_op, 0));
  b_op.create_driver_pin(0).connect_sink(b->get_output_pin("y"));

  auto r = livehd::semdiff::structural_match(a.get(), b.get());

  EXPECT_EQ(1U, r.a_unmatched);
  EXPECT_EQ(1U, r.b_unmatched);
  EXPECT_EQ(0U, r.a_matched);
  EXPECT_EQ(0U, r.regions);
}

// Mapping may retain only bits 1 and 3 of a declared register. Report their
// logical name correspondence, but never use that projection as a no-solver
// identity proof (even when just one physical bit remains).
TEST(Semdiff, ProjectedRegisterNamesRequireARealProof) {
  auto make = [](const std::string& dir, const std::vector<std::string>& names, bool wide) {
    auto& lib = livehd::Hhds_graph_library::instance(dir);
    auto  gio = lib.create_io("m");
    auto  g   = gio->create_graph();
    for (const auto& name : names) {
      auto f = create_typed_node(*g, Ntype_op::Flop);
      f.set_name(name);
      auto q = f.create_driver_pin(0);
      livehd::graph_util::set_ubits(q, wide ? 4 : 1);
      livehd::graph_util::set_pin_name(q, name);
    }
    return g;
  };
  auto                             ref = make("lgdb_semdiff_projection_ref", {"lane.state"}, true);
  livehd::semdiff::Semdiff_options opts;
  opts.matching_names     = true;
  opts.project_state_bits = true;
  int id                  = 0;
  for (const auto& names : std::vector<std::vector<std::string>>{
           {"lane.state[1].flop_16", "lane.state[3].flop_16"},
           {"lane.state[3].flop_16"}
  }) {
    auto impl = make(std::format("lgdb_semdiff_projection_impl{}", id++), names, false);
    auto r    = livehd::semdiff::structural_match(ref.get(), impl.get(), opts);
    EXPECT_EQ(1U, r.state.name_pairs);
    EXPECT_EQ(1U, r.state.projected_name_pairs);
    EXPECT_EQ(1U, r.state.a_total);
    EXPECT_EQ(1U, r.state.b_total);
    EXPECT_EQ(0U, r.state.a_unpaired);
    EXPECT_EQ(0U, r.state.b_unpaired);
    EXPECT_FALSE(livehd::semdiff::is_structural_identity(r));
    EXPECT_FALSE(livehd::semdiff::structural_identical(ref.get(), impl.get(), opts));
  }
  auto       renamed   = make("lgdb_semdiff_projection_renamed", {"other.state[1].flop_16", "other.state[3].flop_16"}, false);
  const auto candidate = livehd::semdiff::structural_match(ref.get(), renamed.get(), opts);
  ASSERT_EQ(1U, candidate.state_projections.size());
  EXPECT_EQ(1U, candidate.state.projection_candidates);
  EXPECT_EQ("lane.state", candidate.state_projections.front().wide_key);
  EXPECT_EQ((std::vector<std::string>{"", "other.state[1].flop_16", "", "other.state[3].flop_16"}),
            candidate.state_projections.front().bit_keys);
  EXPECT_FALSE(livehd::semdiff::is_structural_identity(candidate));
  auto ambiguous = make("lgdb_semdiff_projection_ambiguous", {"lane.state", "other.wide"}, true);
  EXPECT_TRUE(livehd::semdiff::structural_match(ambiguous.get(), renamed.get(), opts).state_projections.empty());
  auto duplicate = make("lgdb_semdiff_projection_duplicate", {"other.state[1].a", "other.state[1].b"}, false);
  EXPECT_TRUE(livehd::semdiff::structural_match(ref.get(), duplicate.get(), opts).state_projections.empty());
  for (const auto& names : std::vector<std::vector<std::string>>{
           {"lane.state[4].flop_16"},
           {"lane.state[1].a", "lane.state[1].b"},
           {"other.state[1].flop_16"}
  }) {
    auto impl = make(std::format("lgdb_semdiff_projection_bad{}", id++), names, false);
    auto r    = livehd::semdiff::structural_match(ref.get(), impl.get(), opts);
    EXPECT_EQ(0U, r.state.name_pairs);
    EXPECT_EQ(0U, r.state.projected_name_pairs);
    EXPECT_EQ(1U, r.state.a_unpaired);
  }
}

TEST(Semdiff, MemoryBankCandidatesAreSharedAndRequireProof) {
  const auto ref  = build_memory_kind("lgdb_semdiff_mem_bank_ref", 0);
  const auto make = [](const std::string& directory, bool incomplete) {
    auto& lib = livehd::Hhds_graph_library::instance(directory);
    auto  g   = lib.create_io("memory_kind")->create_graph();
    for (int entry = 0; entry < 4; ++entry) {
      for (int bit = 0; bit < 8; ++bit) {
        if (incomplete && entry == 3 && bit == 7) {
          continue;
        }
        auto       flop = create_typed_node(*g, Ntype_op::Flop);
        const auto name = std::format("m._mem[{}][{}].flop_16", entry, bit);
        flop.set_name(name);
        auto q = flop.create_driver_pin(0);
        livehd::graph_util::set_ubits(q, 1);
        livehd::graph_util::set_pin_name(q, name);
      }
    }
    return g;
  };
  livehd::semdiff::Semdiff_options options;
  options.matching_names     = true;
  options.project_state_bits = true;
  auto       complete        = make("lgdb_semdiff_mem_bank_complete", false);
  const auto result          = livehd::semdiff::structural_match(ref.get(), complete.get(), options);
  ASSERT_EQ(result.memory_projections.size(), 1U);
  EXPECT_EQ(result.state.memory_projection_candidates, 1U);
  const auto& projection = result.memory_projections.front();
  EXPECT_EQ(projection.memory_key, "m");
  EXPECT_EQ(projection.entries, 4);
  EXPECT_EQ(projection.bits, 8);
  EXPECT_FALSE(projection.memory_in_impl);
  ASSERT_EQ(projection.bank.bit_keys.size(), 4U);
  EXPECT_EQ(projection.bank.bit_keys[3][7], "m._mem[3][7].flop_16");
  EXPECT_FALSE(livehd::semdiff::is_structural_identity(result));
  auto incomplete = make("lgdb_semdiff_mem_bank_incomplete", true);
  EXPECT_TRUE(livehd::semdiff::structural_match(ref.get(), incomplete.get(), options).memory_projections.empty());
  const auto reverse = livehd::semdiff::structural_match(complete.get(), ref.get(), options);
  ASSERT_EQ(reverse.memory_projections.size(), 1U);
  EXPECT_TRUE(reverse.memory_projections.front().memory_in_impl);
}
