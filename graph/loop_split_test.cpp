// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Carry classification for compact loops (graph/loop_split.hpp).
//
// The contract that matters is the CONSERVATIVE one: `induction` is always a
// correct answer, so every case this cannot prove must land there. A wrong
// `disjoint_slice` or `assoc_reduction` would let a consumer take a shortcut it
// has not earned, so the negative tests below are the load-bearing ones.

#include "loop_split.hpp"

#include <functional>
#include <memory>
#include <string>

#include "attrs.hpp"
#include "cell.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"

namespace gu = livehd::graph_util;

namespace {

constexpr hhds::Port_id kIdx      = 1;  // loop index input
constexpr hhds::Port_id kInv      = 2;  // invariant input
constexpr hhds::Port_id kCarryIn  = 3;
constexpr hhds::Port_id kCarryOut = 4;

// Callee body with the standard loop ABI: index, invariant, carry-in ->
// carry-out. `shape` wires the carry-out.
using Shape = std::function<hhds::Pin_class(hhds::Graph&, hhds::Pin_class idx, hhds::Pin_class inv, hhds::Pin_class carry)>;

std::shared_ptr<hhds::Graph> make_body(hhds::GraphLibrary& lib, const std::string& name, const Shape& shape) {
  auto gio = lib.create_io(name);
  gio->add_input("idx", kIdx);
  gio->add_input("inv", kInv);
  gio->add_input("cin", kCarryIn);
  gio->add_output("cout", kCarryOut);
  gio->set_bits("idx", 8);
  gio->set_bits("inv", 8);
  gio->set_bits("cin", 32);
  gio->set_bits("cout", 32);
  auto g   = gio->create_graph();
  auto in  = g->get_input_node();
  auto idx = in.create_driver_pin(kIdx);
  auto inv = in.create_driver_pin(kInv);
  auto cin = in.create_driver_pin(kCarryIn);
  gu::set_ubits(idx, 8);
  gu::set_ubits(inv, 8);
  gu::set_ubits(cin, 32);
  auto cout = shape(*g, idx, inv, cin);
  cout.connect_sink(g->get_output_node().create_sink_pin(kCarryOut));
  return g;
}

// A loop Sub in `host` instantiating `body`, with the carry fully wired: the
// self-edge (replica r>0 reads replica r-1) AND an external seed (replica 0's
// initial value). hhds's Subnode_group::validate rejects one without the other,
// so the fixture is validated, not merely constructed -- the classifier is
// exercised on the same shape the rest of the tree accepts.
hhds::Node_class make_loop(hhds::Graph& host, const std::shared_ptr<hhds::Graph>& body, uint64_t count) {
  auto               sub = gu::create_typed_node(host, Ntype_op::Sub);
  hhds::Subnode_loop loop;
  loop.first       = 0;
  loop.step        = 1;
  loop.count       = count;
  loop.index_input = kIdx;
  sub.set_subnode(body->get_io(), loop);
  auto zero = gu::create_const(host, *Dlop::create_integer(0));
  zero.connect_sink(sub.create_sink_pin(kCarryIn));
  sub.create_driver_pin(kCarryOut).connect_sink(sub.create_sink_pin(kCarryIn));
  sub.subnode_group().validate();
  return sub;
}

hhds::Graph* host_of(hhds::GraphLibrary& lib, const std::string& name, std::shared_ptr<hhds::Graph>& keep) {
  auto gio = lib.create_io(name);
  keep     = gio->create_graph();
  return keep.get();
}

}  // namespace

// `acc = acc + f(idx, inv)` -- associative, NOT idempotent, so count-sensitive.
TEST(LoopSplit, SumIsAnAssociativeNonIdempotentReduction) {
  auto&                        lib  = livehd::Hhds_graph_library::instance("lgdb_loopsplit_sum");
  auto                         body = make_body(lib, "b_sum", [](hhds::Graph& g, auto idx, auto inv, auto carry) {
    auto f = gu::create_typed_node(g, Ntype_op::Xor, 32);
    idx.connect_sink(gu::setup_sink_by_name(f, "as"));
    inv.connect_sink(gu::setup_sink_by_name(f, "as"));
    auto acc = gu::create_typed_node(g, Ntype_op::Sum, 32);
    carry.connect_sink(gu::setup_sink_by_name(acc, "as"));
    f.create_driver_pin(0).connect_sink(gu::setup_sink_by_name(acc, "as"));
    return acc.create_driver_pin(0);
  });
  std::shared_ptr<hhds::Graph> keep;
  auto*                        host = host_of(lib, "h_sum", keep);
  auto                         sub  = make_loop(*host, body, 8);

  auto s = gu::classify_loop(sub);
  ASSERT_TRUE(s.valid);
  ASSERT_EQ(s.carries.size(), 1u);
  EXPECT_EQ(s.carries[0].kind, gu::Carry_kind::assoc_reduction);
  EXPECT_EQ(s.carries[0].reduce_op, Ntype_op::Sum);
  EXPECT_FALSE(s.carries[0].idempotent) << "Sum is associative but NOT idempotent: extra iterations change it";
  EXPECT_TRUE(s.induction_nodes.empty()) << "a classified reduction is not a general recurrence";
}

// `acc = acc | f(idx)` -- idempotent, so a saturated value is count-INDEPENDENT.
// This is the shape that makes two loops with different counts legitimately equal.
TEST(LoopSplit, OrIsAnIdempotentReduction) {
  auto&                        lib  = livehd::Hhds_graph_library::instance("lgdb_loopsplit_or");
  auto                         body = make_body(lib, "b_or", [](hhds::Graph& g, auto idx, auto inv, auto carry) {
    (void)inv;
    auto acc = gu::create_typed_node(g, Ntype_op::Or, 32);
    carry.connect_sink(gu::setup_sink_by_name(acc, "as"));
    idx.connect_sink(gu::setup_sink_by_name(acc, "as"));
    return acc.create_driver_pin(0);
  });
  std::shared_ptr<hhds::Graph> keep;
  auto*                        host = host_of(lib, "h_or", keep);
  auto                         sub  = make_loop(*host, body, 8);

  auto s = gu::classify_loop(sub);
  ASSERT_TRUE(s.valid);
  ASSERT_EQ(s.carries.size(), 1u);
  EXPECT_EQ(s.carries[0].kind, gu::Carry_kind::assoc_reduction);
  EXPECT_TRUE(s.carries[0].idempotent);
}

// `acc = set_mask(acc, mask(idx), value)` -- the per-lane write. Parallel SHAPE,
// but overlap is a value question, so it must be flagged for the consumer.
TEST(LoopSplit, PerLaneSliceWriteIsParallelButNeedsADisjointnessProof) {
  auto&                        lib  = livehd::Hhds_graph_library::instance("lgdb_loopsplit_slice");
  auto                         body = make_body(lib, "b_slice", [](hhds::Graph& g, auto idx, auto inv, auto carry) {
    auto sm = gu::create_typed_node(g, Ntype_op::Set_mask, 32);
    carry.connect_sink(gu::setup_sink_by_name(sm, "a"));
    idx.connect_sink(gu::setup_sink_by_name(sm, "mask"));
    inv.connect_sink(gu::setup_sink_by_name(sm, "value"));
    return sm.create_driver_pin(0);
  });
  std::shared_ptr<hhds::Graph> keep;
  auto*                        host = host_of(lib, "h_slice", keep);
  auto                         sub  = make_loop(*host, body, 8);

  auto s = gu::classify_loop(sub);
  ASSERT_TRUE(s.valid);
  ASSERT_EQ(s.carries.size(), 1u);
  EXPECT_EQ(s.carries[0].kind, gu::Carry_kind::disjoint_slice);
  EXPECT_TRUE(s.carries[0].needs_disjoint_proof)
      << "whether two lanes overlap depends on mask VALUES; the structure alone cannot settle it";
  EXPECT_TRUE(s.fully_parallel());
}

// NEGATIVE: the write POSITION depends on the accumulated value, so where lane r
// writes depends on what earlier lanes wrote. That is a real recurrence and must
// NOT be called parallel.
TEST(LoopSplit, SliceWriteWithACarryDependentMaskIsInduction) {
  auto&                        lib  = livehd::Hhds_graph_library::instance("lgdb_loopsplit_badslice");
  auto                         body = make_body(lib, "b_badslice", [](hhds::Graph& g, auto idx, auto inv, auto carry) {
    auto pos = gu::create_typed_node(g, Ntype_op::Xor, 32);
    idx.connect_sink(gu::setup_sink_by_name(pos, "as"));
    carry.connect_sink(gu::setup_sink_by_name(pos, "as"));  // <- position uses the carry
    auto sm = gu::create_typed_node(g, Ntype_op::Set_mask, 32);
    carry.connect_sink(gu::setup_sink_by_name(sm, "a"));
    pos.create_driver_pin(0).connect_sink(gu::setup_sink_by_name(sm, "mask"));
    inv.connect_sink(gu::setup_sink_by_name(sm, "value"));
    return sm.create_driver_pin(0);
  });
  std::shared_ptr<hhds::Graph> keep;
  auto*                        host = host_of(lib, "h_badslice", keep);
  auto                         sub  = make_loop(*host, body, 8);

  auto s = gu::classify_loop(sub);
  ASSERT_TRUE(s.valid);
  ASSERT_EQ(s.carries.size(), 1u);
  EXPECT_EQ(s.carries[0].kind, gu::Carry_kind::induction);
  EXPECT_FALSE(s.induction_nodes.empty());
  EXPECT_GT(s.induction_ratio(), 0.0);
}

// NEGATIVE: `acc = (acc ^ inv) + idx` -- associative head, but the non-carry
// operand USES the carry, so it is not `op(acc, X)` with X carry-free.
TEST(LoopSplit, ReductionWhoseOtherOperandUsesTheCarryIsInduction) {
  auto&                        lib  = livehd::Hhds_graph_library::instance("lgdb_loopsplit_badred");
  auto                         body = make_body(lib, "b_badred", [](hhds::Graph& g, auto idx, auto inv, auto carry) {
    auto mixed = gu::create_typed_node(g, Ntype_op::Xor, 32);
    carry.connect_sink(gu::setup_sink_by_name(mixed, "as"));
    inv.connect_sink(gu::setup_sink_by_name(mixed, "as"));
    auto acc = gu::create_typed_node(g, Ntype_op::Sum, 32);
    mixed.create_driver_pin(0).connect_sink(gu::setup_sink_by_name(acc, "as"));
    idx.connect_sink(gu::setup_sink_by_name(acc, "as"));
    return acc.create_driver_pin(0);
  });
  std::shared_ptr<hhds::Graph> keep;
  auto*                        host = host_of(lib, "h_badred", keep);
  auto                         sub  = make_loop(*host, body, 8);

  auto s = gu::classify_loop(sub);
  ASSERT_TRUE(s.valid);
  ASSERT_EQ(s.carries.size(), 1u);
  ASSERT_EQ(s.carries[0].kind, gu::Carry_kind::induction)
      << "op(f(acc), X) is not op(acc, X); folding it as a reduction would be wrong";
}

// NEGATIVE: `acc = inv - acc` -- the head is a Sum, but the carry lands on the
// SUBTRACTING sink (`bs`). That recurrence alternates; it is not a fold.
TEST(LoopSplit, CarryOnTheSubtractingSinkIsNotAReduction) {
  auto&                        lib  = livehd::Hhds_graph_library::instance("lgdb_loopsplit_bs");
  auto                         body = make_body(lib, "b_bs", [](hhds::Graph& g, auto idx, auto inv, auto carry) {
    (void)idx;
    auto acc = gu::create_typed_node(g, Ntype_op::Sum, 32);
    inv.connect_sink(gu::setup_sink_by_name(acc, "as"));
    carry.connect_sink(gu::setup_sink_by_name(acc, "bs"));  // <- acc = inv - acc
    return acc.create_driver_pin(0);
  });
  std::shared_ptr<hhds::Graph> keep;
  auto*                        host = host_of(lib, "h_bs", keep);
  auto                         sub  = make_loop(*host, body, 8);

  auto s = gu::classify_loop(sub);
  ASSERT_TRUE(s.valid);
  ASSERT_EQ(s.carries.size(), 1u);
  EXPECT_EQ(s.carries[0].kind, gu::Carry_kind::induction) << "X - acc is an alternating recurrence, not a reduction";
  EXPECT_FALSE(s.fully_parallel());
}

// NEGATIVE: the write position is a CONSTANT, not index-derived: every lane
// writes the same slice, so the result is the LAST lane's value -- an ordered
// dependence, not a per-lane slice.
TEST(LoopSplit, ConstantMaskIsNotAPerLaneSlice) {
  auto&                        lib  = livehd::Hhds_graph_library::instance("lgdb_loopsplit_constmask");
  auto                         body = make_body(lib, "b_constmask", [](hhds::Graph& g, auto idx, auto inv, auto carry) {
    (void)idx;
    auto sm = gu::create_typed_node(g, Ntype_op::Set_mask, 32);
    carry.connect_sink(gu::setup_sink_by_name(sm, "a"));
    gu::create_const(g, *Dlop::create_integer(2)).connect_sink(gu::setup_sink_by_name(sm, "mask"));
    inv.connect_sink(gu::setup_sink_by_name(sm, "value"));
    return sm.create_driver_pin(0);
  });
  std::shared_ptr<hhds::Graph> keep;
  auto*                        host = host_of(lib, "h_constmask", keep);
  auto                         sub  = make_loop(*host, body, 8);

  auto s = gu::classify_loop(sub);
  ASSERT_TRUE(s.valid);
  ASSERT_EQ(s.carries.size(), 1u);
  EXPECT_EQ(s.carries[0].kind, gu::Carry_kind::induction);
}

// A reduction is a recurrence too: a loop whose only carry is `sum += x` has no
// induction NODES, but it is not parallel.
TEST(LoopSplit, AReductionLoopIsNotFullyParallel) {
  auto&                        lib  = livehd::Hhds_graph_library::instance("lgdb_loopsplit_redpar");
  auto                         body = make_body(lib, "b_redpar", [](hhds::Graph& g, auto idx, auto inv, auto carry) {
    (void)idx;
    auto acc = gu::create_typed_node(g, Ntype_op::Sum, 32);
    carry.connect_sink(gu::setup_sink_by_name(acc, "as"));
    inv.connect_sink(gu::setup_sink_by_name(acc, "as"));
    return acc.create_driver_pin(0);
  });
  std::shared_ptr<hhds::Graph> keep;
  auto*                        host = host_of(lib, "h_redpar", keep);
  auto                         sub  = make_loop(*host, body, 8);

  auto s = gu::classify_loop(sub);
  ASSERT_TRUE(s.valid);
  EXPECT_TRUE(s.induction_nodes.empty());
  EXPECT_FALSE(s.fully_parallel()) << "fully_parallel is derived from the carries, not from the induction node set";
}

// A non-loop Sub, and a body-less black box, must both classify as `invalid`
// rather than being mistaken for a fully-parallel loop.
TEST(LoopSplit, NonLoopSubsAreNotClassified) {
  auto&                        lib = livehd::Hhds_graph_library::instance("lgdb_loopsplit_nonloop");
  std::shared_ptr<hhds::Graph> keep;
  auto*                        host = host_of(lib, "h_nonloop", keep);

  auto plain = gu::create_typed_node(*host, Ntype_op::Sub);
  EXPECT_FALSE(gu::classify_loop(plain).valid);
  EXPECT_FALSE(gu::classify_loop(gu::create_typed_node(*host, Ntype_op::Or, 8)).valid);
}
