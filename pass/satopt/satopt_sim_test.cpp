// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "satopt_sim.hpp"

#include "gtest/gtest.h"
#include "node_util.hpp"
#include "prove.hpp"

namespace gu = livehd::graph_util;
using livehd::satopt::detail::Word_sim;

namespace {
// y = a + b over u8 inputs (a stamped u8 sum wraps).
struct Adder {
  hhds::GraphLibrary           lib;
  std::shared_ptr<hhds::Graph> g;
  hhds::Pin_class              a, b, y;
  explicit Adder(std::string_view name) {
    auto io = lib.create_io(name);
    io->add_input("a", 1);
    io->set_bits("a", 8);
    io->set_unsign("a", true);
    io->add_input("b", 2);
    io->set_bits("b", 8);
    io->set_unsign("b", true);
    io->add_output("y", 3);
    io->set_bits("y", 8);
    g = io->create_graph();
    a = g->get_input_pin("a");
    b = g->get_input_pin("b");
    gu::set_ubits(a, 8);
    gu::set_ubits(b, 8);
    auto sum = gu::create_typed_node(*g, Ntype_op::Sum);
    a.connect_sink(gu::setup_sink_pid(sum, 0));
    b.connect_sink(gu::setup_sink_pid(sum, 0));
    y = sum.create_driver_pin(0);
    gu::set_ubits(y, 8);
    y.connect_sink(g->get_output_pin("y"));
  }
};
}  // namespace

// The corner patterns come first: 0, all ones, 0101.., 1010.., signed min,
// signed max, 1 -- and every value is a whole column.
TEST(WordSim, CornerPatternsLeadEveryLeaf) {
  Adder    f("sim_corners");
  Word_sim sim({.samples = 16});
  const auto* a = sim.values(f.a);
  ASSERT_NE(a, nullptr);
  ASSERT_EQ(a->size(), 16u);
  EXPECT_EQ((*a)[0].to_just_i64(), 0);
  EXPECT_EQ((*a)[1].to_just_i64(), 255);
  EXPECT_EQ((*a)[2].to_just_i64(), 0x55);
  EXPECT_EQ((*a)[3].to_just_i64(), 0xaa);
  EXPECT_EQ((*a)[4].to_just_i64(), 0x80);
  EXPECT_EQ((*a)[5].to_just_i64(), 0x7f);
  EXPECT_EQ((*a)[6].to_just_i64(), 1);
  const auto* y = sim.values(f.y);
  ASSERT_NE(y, nullptr);
  EXPECT_EQ((*y)[1].to_just_i64(), 254);  // 255 + 255 wraps in a u8
  for (size_t j = 0; j < y->size(); ++j) {
    const auto& av = (*a)[j];
    const auto& bv = (*sim.values(f.b))[j];
    EXPECT_EQ((*y)[j].to_just_i64(), (av.to_just_i64() + bv.to_just_i64()) & 255) << j;
  }
}

// A leaf's pattern depends on the leaf, not on what was evaluated first.
TEST(WordSim, PatternsDoNotDependOnEvaluationOrder) {
  Adder    f("sim_order");
  Word_sim first({.samples = 32});
  Word_sim second({.samples = 32});
  first.values(f.y);
  const auto a1 = *first.values(f.a);
  second.values(f.b);
  const auto a2 = *second.values(f.a);
  ASSERT_EQ(a1.size(), a2.size());
  for (size_t j = 0; j < a1.size(); ++j) {
    EXPECT_TRUE(a1[j].is_known_eq(a2[j])) << j;
  }
  // Random columns differ between leaves.
  const auto b = *first.values(f.b);
  bool       differ = false;
  for (size_t j = Word_sim::kCorners; j < b.size(); ++j) {
    differ |= !a1[j].is_known_eq(b[j]);
  }
  EXPECT_TRUE(differ);
}

// An operation the simulator cannot evaluate, or a combinational loop, has
// no value (and so rejects nothing).
TEST(WordSim, UnsupportedAndLoopsAreUnavailable) {
  Adder f("sim_unsupported");
  auto  div = gu::create_typed_node(*f.g, Ntype_op::Div);
  f.a.connect_sink(div.create_sink_pin(0));
  f.b.connect_sink(div.create_sink_pin(1));
  gu::set_ubits(div.create_driver_pin(0), 8);
  auto loop = gu::create_typed_node(*f.g, Ntype_op::Xor);
  f.a.connect_sink(gu::setup_sink_pid(loop, 0));
  loop.create_driver_pin(0).connect_sink(gu::setup_sink_pid(loop, 0));
  gu::set_ubits(loop.create_driver_pin(0), 8);
  Word_sim sim({.samples = 8});
  EXPECT_EQ(sim.values(div.create_driver_pin(0)), nullptr);
  EXPECT_EQ(sim.values(loop.create_driver_pin(0)), nullptr);
  EXPECT_NE(sim.values(f.y), nullptr);
}

// A counterexample becomes one more column: the leaves it names take its
// values, and every computed value is extended.
TEST(WordSim, ModelColumnsExtendEveryValue) {
  Adder    f("sim_model");
  Word_sim sim({.samples = 8});
  ASSERT_NE(sim.values(f.y), nullptr);
  livehd::formal::Model model{{{}, f.a, *Dlop::create_integer(200)}, {{}, f.b, *Dlop::create_integer(100)}};
  ASSERT_TRUE(sim.add_model(model));
  EXPECT_EQ(sim.columns(), 9u);
  const auto* y = sim.values(f.y);
  ASSERT_NE(y, nullptr);
  ASSERT_EQ(y->size(), 9u);
  EXPECT_EQ(y->back().to_just_i64(), (200 + 100) & 255);
  // Signatures pack one bit per column.
  const auto sig = sim.bit_signature(f.y, 5);  // 300 & 255 = 44 = 0b101100
  ASSERT_EQ(sig.size(), 1u);
  EXPECT_TRUE((sig[0] >> 8) & 1);
  EXPECT_FALSE((sig[0] >> 0) & 1);  // column 0: 0 + 0
  // The number of model columns is bounded.
  Word_sim capped({.samples = 8, .max_models = 1});
  EXPECT_TRUE(capped.add_model(model));
  EXPECT_FALSE(capped.add_model(model));
}

// The prover's counterexample names its leaves by pin: fed back, it makes the
// simulation reproduce the refuted case.
TEST(WordSim, ProverModelRefutesInSimulation) {
  Adder f("sim_prover_model");
  auto  eq = gu::create_typed_node(*f.g, Ntype_op::EQ);
  f.y.connect_sink(gu::setup_sink_pid(eq, 0));
  gu::create_const(*f.g, *Dlop::create_integer(0x3c)).connect_sink(gu::setup_sink_pid(eq, 0));
  const auto hit = eq.create_driver_pin(0);
  gu::set_ubits(hit, 1);
  livehd::formal::Prover prover(f.g.get(), {.min_rlimit = 1 << 16, .produce_model = true});
  const auto             out = prover.is_false(hit);
  ASSERT_EQ(out.verdict, livehd::formal::Verdict::Refuted);
  ASSERT_FALSE(out.model.empty());
  Word_sim sim({.samples = 8});
  ASSERT_TRUE(sim.add_model(out.model));
  const auto* v = sim.values(hit);
  ASSERT_NE(v, nullptr);
  EXPECT_FALSE(v->back().is_known_zero());
}

// LGraph values are integers: a bitwise cell extends each operand by its own
// sign. Or(u8 x, s8 -1) is -1 -- all ones at any width -- and the prover and
// the simulation must agree (satopt commits what the prover proves).
TEST(WordSim, BitwiseOperandsExtendByTheirOwnSign) {
  Adder f("sim_signed_or");
  auto  neg = gu::create_const(*f.g, *Dlop::create_integer(-1));
  auto  n   = gu::create_typed_node(*f.g, Ntype_op::Or);
  f.a.connect_sink(gu::setup_sink_pid(n, 0));
  neg.connect_sink(gu::setup_sink_pid(n, 0));
  const auto out = n.create_driver_pin(0);
  gu::set_ubits(out, 9);
  Word_sim    sim({.samples = 16});
  const auto* v = sim.values(out);
  ASSERT_NE(v, nullptr);
  for (const auto& x : *v) {
    EXPECT_EQ(x.to_just_i64(), 511);
  }
  livehd::formal::Prover prover(f.g.get(), {.min_rlimit = 1 << 20});
  const auto             all = *Dlop::get_mask_value(9);
  EXPECT_EQ(prover.masked_const(out, 9, all, *Dlop::create_integer(511)).verdict, livehd::formal::Verdict::Proven);
  EXPECT_EQ(prover.masked_const(out, 9, all, *Dlop::create_integer(255)).verdict, livehd::formal::Verdict::Refuted);
}
