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
