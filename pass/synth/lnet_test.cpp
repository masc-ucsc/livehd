// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "lnet.hpp"

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <vector>

#include "lnet_ops.hpp"

namespace {
using livehd::synth::Lid;
using livehd::synth::Lnet;
using livehd::synth::Lnet_ops;

TEST(Lnet, NodeZeroIsTheConstantAndTheBoundaryTablesKeepTheirOrder) {
  Lnet net;
  ASSERT_EQ(net.size(), 1u);
  EXPECT_EQ(net.kind(Lnet::kConst0), Lnet::Kind::constant);
  EXPECT_EQ(net.fn(Lnet::kConst0), 0u);

  const auto latch = net.add_latch("q_%r0_0", '1');
  const auto q     = net.latch(latch).q;
  const auto a     = net.add_input("a_b0");
  const auto b     = net.add_input({});
  EXPECT_TRUE(net.is_latch_source(q));
  EXPECT_EQ(net.source_index(q), latch);
  EXPECT_FALSE(net.is_latch_source(a));
  EXPECT_EQ(net.source_index(a), 0u);
  EXPECT_EQ(net.source_index(b), 1u);
  EXPECT_EQ(net.inputs()[1].name, "");
  EXPECT_EQ(net.latch(latch).init, '1');

  const auto g = net.add_lut({a, b}, Lnet::kAnd2);
  net.add_output(g, "y");  // recorded between g and h
  const auto h = net.add_lut({g, q}, Lnet::kXor2);
  EXPECT_EQ(net.fanin_count(h), 2u);
  EXPECT_EQ(net.fanin(h, 1), q);
  EXPECT_EQ(net.level(g), 1u);
  EXPECT_EQ(net.level(h), 2u);
  ASSERT_EQ(net.outputs().size(), 1u);
  EXPECT_EQ(net.outputs()[0].created, h);  // replayed right before h

  net.set_latch_input(latch, g);
  EXPECT_EQ(net.fanout_count(g), 3u);  // h, the output and the latch
  net.set_latch_input(latch, h);       // re-pointing moves the latch's read
  EXPECT_EQ(net.fanout_count(g), 2u);
  EXPECT_EQ(net.fanout_count(h), 1u);
  EXPECT_EQ(net.latch(latch).d, h);
}

// The region blaster's style: every binary gate materializes constant 0, then
// constant 1, before it folds.
TEST(LnetOps, EagerConstantsAppearBeforeTheFirstGate) {
  Lnet       net;
  Lnet_ops   ops(net);
  const auto a = net.add_input("a");
  const auto b = net.add_input("b");
  const auto g = ops.and_(a, b);
  ASSERT_EQ(net.size(), 6u);  // const0, a, b, 0, 1, g
  EXPECT_EQ(ops.zero(), 3u);
  EXPECT_EQ(ops.one(), 4u);
  EXPECT_EQ(g, 5u);
  EXPECT_EQ(net.fn(g), Lnet::kAnd2);

  EXPECT_EQ(ops.and_(a, ops.zero()), ops.zero());
  EXPECT_EQ(ops.and_(ops.one(), b), b);
  EXPECT_EQ(ops.and_(a, a), a);
  EXPECT_EQ(ops.or_(a, ops.one()), ops.one());
  EXPECT_EQ(ops.or_(ops.zero(), b), b);
  EXPECT_EQ(ops.xor_(a, a), ops.zero());
  EXPECT_EQ(ops.xor_(ops.zero(), b), b);
  EXPECT_EQ(ops.inv(ops.one()), ops.zero());
  EXPECT_EQ(ops.inv(ops.zero()), ops.one());
  EXPECT_EQ(net.size(), 6u);  // every fold above created nothing

  const auto n = ops.inv(a);
  EXPECT_EQ(net.fn(n), Lnet::kNot);
  EXPECT_EQ(net.fanin(n, 0), a);
}

// satopt's style: a constant exists only once a test reads it.
TEST(LnetOps, LazyConstantsAppearOnlyWhenRead) {
  Lnet       net;
  Lnet_ops   ops(net, Lnet_ops::Constants::lazy);
  const auto a = net.add_input("a");
  EXPECT_EQ(ops.xor_(a, a), 2u);  // only constant 0, created by the fold
  EXPECT_EQ(net.size(), 3u);
  EXPECT_EQ(net.fn(ops.zero()), 0u);
  const auto b = net.add_input("b");
  (void)ops.xor_(a, b);  // a != b, then a == 0?, b == 0?: no constant 1
  EXPECT_EQ(net.size(), 5u);
  (void)ops.and_(a, b);  // a == 1? creates constant 1
  EXPECT_EQ(net.size(), 7u);
  EXPECT_EQ(net.fn(ops.one()), ~uint64_t{0});
}

TEST(LnetOps, NodeLimitThrowsBeforeCreating) {
  Lnet       net;
  Lnet_ops   ops(net, Lnet_ops::Constants::lazy, 5);
  const auto a = ops.input("a");
  const auto b = ops.input("b");
  (void)ops.inv(a);  // constant 0, constant 1 and the inverter: five nodes
  EXPECT_EQ(net.size(), 6u);
  EXPECT_THROW((void)ops.and_(a, b), Lnet_ops::Too_large);
  EXPECT_EQ(net.size(), 6u);
}

TEST(LnetOps, MuxIsAndOrOfTheSelect) {
  Lnet       net;
  Lnet_ops   ops(net);
  const auto s = net.add_input("s");
  const auto t = net.add_input("t");
  const auto f = net.add_input("f");
  const auto m = ops.mux(s, t, f);
  EXPECT_EQ(net.fn(m), Lnet::kOr2);
  EXPECT_EQ(ops.mux(ops.one(), t, f), t);
  EXPECT_EQ(ops.mux(ops.zero(), t, f), f);
}

// The outputs (then latch inputs) for the source assignment `inputs`
// (sources in id order).
std::vector<bool> evaluate(const Lnet& net, uint32_t inputs) {
  std::vector<bool> value(net.size());
  uint32_t          source = 0;
  for (Lid i = 0; i < net.size(); ++i) {
    if (net.kind(i) == Lnet::Kind::source) {
      value[i] = ((inputs >> source++) & 1) != 0;
      continue;
    }
    uint32_t x = 0;
    for (uint32_t k = 0; k < net.fanin_count(i); ++k) {
      x |= uint32_t{value[net.fanin(i, k)]} << k;
    }
    value[i] = net.eval(i, x);
  }
  std::vector<bool> out;
  for (auto o : livehd::synth::combinational_outputs(net)) {
    out.push_back(value[o]);
  }
  return out;
}

// STRASH: sources in CI order (inputs, then latches, although the latch was
// recorded first), hashed and folded gates, complements absorbed into tables,
// dead gates dropped, one combinational output per CO.
TEST(LnetStrash, HashesFoldsAndKeepsTheCiCoOrder) {
  Lnet       raw;
  const auto latch = raw.add_latch("q_%r0_0", '0');
  const auto q     = raw.latch(latch).q;
  const auto a     = raw.add_input("a_b0");
  const auto b     = raw.add_input({});
  const auto zero  = raw.add_constant(false);
  const auto one   = raw.add_constant(true);
  const auto nb    = raw.add_lut({b}, Lnet::kNot);
  const auto y     = raw.add_lut({a, nb}, Lnet::kAnd2);
  const auto z     = raw.add_lut({nb, a}, Lnet::kAnd2);  // the same gate, commuted
  (void)raw.add_lut({a, b}, Lnet::kOr2);                 // never observed
  const auto xnor  = raw.add_lut({raw.add_lut({a, q}, Lnet::kXor2)}, Lnet::kNot);
  const auto k     = raw.add_lut({a, one}, Lnet::kAnd2);  // folds to a
  raw.add_output(y, "y");
  raw.add_output(z, "z");
  raw.add_output(xnor, "w");
  raw.add_output(zero, "c");
  raw.add_output(k, "k");
  raw.set_latch_input(latch, xnor);
  for (const bool xor_nodes : {true, false}) {
    const auto net = livehd::synth::strash(raw, {.xor_nodes = xor_nodes, .admit = {}});
    ASSERT_TRUE(net.has_value());
    ASSERT_EQ(net->inputs().size(), 2u);
    EXPECT_EQ(net->inputs()[0].node, 1u);
    EXPECT_EQ(net->inputs()[0].name, "a_b0");
    EXPECT_EQ(net->inputs()[1].node, 2u);
    ASSERT_EQ(net->latches().size(), 1u);
    EXPECT_EQ(net->latch(0).q, 3u);
    EXPECT_EQ(net->latch(0).init, '0');
    const auto& o = net->outputs();
    ASSERT_EQ(o.size(), 5u);
    EXPECT_EQ(o[0].node, o[1].node);
    EXPECT_EQ(o[3].node, Lnet::kConst0);
    EXPECT_EQ(o[4].node, 1u);  // a itself
    EXPECT_EQ(o[2].node, net->latch(0).d);
    size_t gates = 0, inverters = 0;
    for (Lid i = 0; i < net->size(); ++i) {
      gates += net->kind(i) == Lnet::Kind::lut && net->fanin_count(i) == 2;
      inverters += net->kind(i) == Lnet::Kind::lut && net->fanin_count(i) == 1;
    }
    // y, then the XOR as one node or three ANDs (whose last one is the XNOR).
    EXPECT_EQ(gates, xor_nodes ? 2u : 4u);
    // NOT(xor) only with an XOR node.
    EXPECT_EQ(inverters, xor_nodes ? 1u : 0u);
    for (uint32_t v = 0; v < 8; ++v) {
      const bool va = (v & 1) != 0, vb = (v & 2) != 0, vq = (v & 4) != 0;
      const auto out = evaluate(*net, v);
      ASSERT_EQ(out.size(), 6u);  // five outputs, then the latch input
      EXPECT_EQ(out[0], va && !vb) << v;
      EXPECT_EQ(out[1], va && !vb) << v;
      EXPECT_EQ(out[2], va == vq) << v;
      EXPECT_FALSE(out[3]) << v;
      EXPECT_EQ(out[4], va) << v;
      EXPECT_EQ(out[5], va == vq) << v;
    }
  }
  Lnet unset;  // a latch input never set
  (void)unset.add_latch("q", 'x');
  EXPECT_FALSE(livehd::synth::strash(unset).has_value());
}

TEST(Lnet, WideTablesAndSops) {
  Lnet             net;
  std::vector<Lid> in;
  for (int i = 0; i < 8; ++i) {
    in.push_back(net.add_input({}));
  }
  // AND8: only minterm 255.
  const std::array<uint64_t, 4> and8{0, 0, 0, uint64_t{1} << 63};
  const auto                    g = net.add_lut(in, and8);
  EXPECT_EQ(net.table(g).size(), 4u);
  EXPECT_TRUE(net.eval(g, 255));
  EXPECT_FALSE(net.eval(g, 254));
  // OR7 over two words, replicated.
  const std::array<uint64_t, 2> or7{~uint64_t{1}, ~uint64_t{0}};
  const auto                    h = net.add_lut(std::span<const Lid>(in.data(), 7), or7);
  EXPECT_EQ(net.table(h).size(), 2u);
  EXPECT_FALSE(net.eval(h, 0));
  EXPECT_TRUE(net.eval(h, 127));
  EXPECT_EQ(net.sop(h), nullptr);
  net.set_sop(h, {{{1, 1}, {2, 2}}, false});
  ASSERT_NE(net.sop(h), nullptr);
  EXPECT_EQ(net.sop(h)->cubes.size(), 2u);
  EXPECT_EQ(net.sop(g), nullptr);
  // A narrow table is replicated from its low bits.
  const auto n = net.add_lut({in[0]}, 0x1);  // NOT
  EXPECT_EQ(net.fn(n), Lnet::kNot);
}

}  // namespace
