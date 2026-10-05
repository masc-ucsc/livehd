// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "xag_balance.hpp"

#include <array>
#include <bit>
#include <random>

#include "gtest/gtest.h"

namespace livehd::usyn {
namespace {
std::vector<bool> evaluate(const Xag& graph, std::span<const Xsignal> outputs, uint32_t assignment) {
  std::vector<bool> values(graph.size());
  const auto        value = [&](Xsignal s) { return values[s.id] != s.inverted; };
  for (Id id = 1; id < graph.size(); ++id) {
    const auto& n = graph.node(id);
    if (n.kind == Xag::Kind::source) {
      values[id] = (assignment >> n.source_index) & 1;
    } else if (n.kind == Xag::Kind::and_gate) {
      values[id] = value(n.inputs[0]) && value(n.inputs[1]);
    } else if (n.kind == Xag::Kind::xor_gate) {
      values[id] = value(n.inputs[0]) != value(n.inputs[1]);
    }
  }
  std::vector<bool> result;
  for (auto output : outputs) {
    result.push_back(value(output));
  }
  return result;
}
void equivalent(const Xag& graph, std::span<const Xsignal> outputs, const Balance_result& result) {
  ASSERT_EQ(result.status, Status::feasible);
  ASSERT_EQ(graph.input_names(), result.graph.input_names());
  ASSERT_EQ(outputs.size(), result.outputs.size());
  ASSERT_LE(graph.input_names().size(), 8U);
  for (uint32_t x = 0; x < (1U << graph.input_names().size()); ++x) {
    ASSERT_EQ(evaluate(graph, outputs, x), evaluate(result.graph, result.outputs, x)) << x;
  }
}
}  // namespace

TEST(XagBalance, BalancesPositiveAndNegativeAssociativeChains) {
  for (bool xor_gate : {false, true}) {
    Xag  graph;
    auto root = graph.input("0");
    for (uint32_t i = 1; i < 8; ++i) {
      const auto input = graph.input(std::to_string(i));
      root             = xor_gate ? graph.lxor(~root, input) : graph.land(root, ~input);
    }
    const std::array outputs{root, ~root};
    Budget           work{100000};
    const auto       result = balance_xag(graph, outputs, work);
    equivalent(graph, outputs, result);
    EXPECT_EQ(result.graph.node(result.outputs[0].id).level, 3U);
    EXPECT_EQ(result.outputs[0], ~result.outputs[1]);
    EXPECT_EQ(graph.node(root.id).level, 7U);  // incumbent and saved cuts stay intact
  }
}

TEST(XagBalance, SharedPrefixAndProtectedInternalRootRemainBoundaries) {
  Xag                    graph;
  std::array<Xsignal, 8> inputs;
  for (size_t i = 0; i < inputs.size(); ++i) {
    inputs[i] = graph.input(std::to_string(i));
  }
  auto prefix = graph.land(graph.land(inputs[0], inputs[1]), inputs[2]);
  auto a = prefix, b = prefix;
  for (size_t i = 3; i < 6; ++i) {
    a = graph.land(a, inputs[i]);
    b = graph.lxor(b, inputs[i]);
  }
  const std::array outputs{a, b, ~prefix, inputs[7]};
  Budget           work{100000};
  const auto       result = balance_xag(graph, outputs, work);
  equivalent(graph, outputs, result);
  const auto cut
      = collect_window(result.graph, result.outputs[0], std::array{result.outputs[2].id, result.outputs[3].id}, {}, work);
  // The unused port is still present, and the independently checked function
  // below covers the dependent prefix without assuming independent sources.
  EXPECT_EQ(cut.status, Status::invalid);
  EXPECT_EQ(result.graph.node(result.outputs[2].id).level, 2U);
}

TEST(XagBalance, ComputedLeavesCancellationAndContradictionAreExact) {
  Xag              graph;
  const auto       a = graph.input("a"), b = graph.input("b"), c = graph.input("c");
  const auto       u = graph.lor(a, b), v = graph.land(b, c);
  const auto       parity = graph.lxor(graph.lxor(u, v), ~u);
  const auto       zero   = graph.land(graph.land(u, v), ~u);
  const std::array outputs{parity, zero, graph.constant(true)};
  Budget           work{100000};
  const auto       result = balance_xag(graph, outputs, work);
  equivalent(graph, outputs, result);
  EXPECT_EQ(result.outputs[1], result.graph.constant(false));
}
TEST(XagBalance, CriticalSharedConeDuplicationIsSmallExplicitAndProtectsOutputs) {
  Xag                    graph;
  std::array<Xsignal, 7> inputs;
  for (unsigned i = 0; i < inputs.size(); ++i) {
    inputs[i] = graph.input(std::to_string(i));
  }
  auto prefix = inputs[0];
  for (unsigned i = 1; i < 5; ++i) {
    prefix = graph.land(prefix, inputs[i]);
  }
  auto             root   = graph.land(graph.land(prefix, inputs[5]), inputs[6]);
  auto             shared = graph.lxor(prefix, inputs[0]);
  const std::array outputs{root, shared};
  Budget           normal{100000}, duplicate{100000}, too_small{100000};
  auto             baseline = balance_xag(graph, outputs, normal);
  auto             trial    = balance_xag(graph, outputs, duplicate, 2000000, 128, 4);
  auto             bounded  = balance_xag(graph, outputs, too_small, 2000000, 128, 3);
  equivalent(graph, outputs, baseline);
  equivalent(graph, outputs, trial);
  equivalent(graph, outputs, bounded);
  EXPECT_EQ(trial.duplications, 4U);
  EXPECT_EQ(bounded.duplications, 0U);
  EXPECT_LT(trial.graph.node(trial.outputs[0].id).level, baseline.graph.node(baseline.outputs[0].id).level);
  const std::array protected_outputs{root, shared, prefix};
  Budget           protected_work{100000};
  auto             protected_trial = balance_xag(graph, protected_outputs, protected_work, 2000000, 128, 4);
  equivalent(graph, protected_outputs, protected_trial);
  EXPECT_EQ(protected_trial.duplications, 0U);
}

TEST(XagBalance, ExhaustiveGeneratedGraphsAndDeterministicWork) {
  std::mt19937 random(1241);
  for (uint32_t trial = 0; trial < 100; ++trial) {
    Xag                  graph;
    std::vector<Xsignal> signals;
    for (uint32_t i = 0; i < 6; ++i) {
      signals.push_back(graph.input(std::to_string(i)));
    }
    for (uint32_t i = 0; i < 50; ++i) {
      auto a = signals[random() % signals.size()], b = signals[random() % signals.size()];
      if (random() & 1) {
        a = ~a;
      }
      if (random() & 1) {
        b = ~b;
      }
      signals.push_back((random() & 1) ? graph.land(a, b) : graph.lxor(a, b));
    }
    const std::array outputs{signals.back(), signals[signals.size() - 2], ~signals[20]};
    Budget           work{100000}, replay{100000};
    const auto       first  = balance_xag(graph, outputs, work, 10000, 8);
    const auto       second = balance_xag(graph, outputs, replay, 10000, 8);
    equivalent(graph, outputs, first);
    equivalent(graph, outputs, second);
    EXPECT_EQ(work.credit_floor(), replay.credit_floor());
    EXPECT_EQ(first.outputs, second.outputs);
    EXPECT_EQ(first.graph.size(), second.graph.size());
  }
}

TEST(XagBalance, AbsorbedChainNodesAreNotRebuiltAsDeadGroupRoots) {
  // A bounded group stops every eight leaves. Each absorbed chain node must
  // not get a balanced group of its own: the candidate stays linear in size.
  Xag  graph;
  auto root = graph.input("0");
  for (uint32_t i = 1; i <= 1000; ++i) {
    root = graph.land(root, graph.input(std::to_string(i)));
  }
  const std::array outputs{root};
  Budget           work{1000000};
  const auto       result = balance_xag(graph, outputs, work, 2000000, 8);
  ASSERT_EQ(result.status, Status::feasible);
  EXPECT_LE(result.graph.size(), graph.size());
  EXPECT_LT(result.graph.node(result.outputs[0].id).level, graph.node(root.id).level);
}

TEST(XagBalance, ExhaustionAndCancellationPublishNoPartialNetwork) {
  Xag  graph;
  auto root = graph.input("a");
  for (uint32_t i = 0; i < 200; ++i) {
    root = graph.land(root, graph.input(std::to_string(i)));
  }
  const std::array outputs{root};
  Budget           tiny{10};
  const auto       partial = balance_xag(graph, outputs, tiny);
  EXPECT_EQ(partial.status, Status::search_exhausted);
  EXPECT_TRUE(partial.outputs.empty());
  EXPECT_EQ(partial.graph.size(), 1U);
  Budget cancelled{100000};
  cancelled.admission = [] { return false; };
  const auto refused  = balance_xag(graph, outputs, cancelled);
  EXPECT_EQ(refused.status, Status::search_exhausted);
  EXPECT_TRUE(cancelled.resource_exhausted);
  EXPECT_TRUE(refused.outputs.empty());
  EXPECT_EQ(graph.node(root.id).level, 200U);
}
}  // namespace livehd::usyn

namespace livehd::usyn {
namespace {
void equivalent_mux(const Xag& graph, std::span<const Xsignal> outputs, const Mux_balance_result& result, uint32_t samples = 0) {
  ASSERT_EQ(result.status, Status::feasible);
  ASSERT_EQ(graph.input_names(), result.graph.input_names());
  ASSERT_EQ(outputs.size(), result.outputs.size());
  const auto n = graph.input_names().size();
  if (samples == 0) {
    ASSERT_LE(n, 16U);
    for (uint32_t x = 0; x < (1U << n); ++x) {
      ASSERT_EQ(evaluate(graph, outputs, x), evaluate(result.graph, result.outputs, x)) << x;
    }
    return;
  }
  ASSERT_LE(n, 32U);
  std::mt19937 rng(7);
  for (uint32_t i = 0; i < samples; ++i) {
    const uint32_t x = rng();
    ASSERT_EQ(evaluate(graph, outputs, x), evaluate(result.graph, result.outputs, x)) << x;
  }
}
// c0 ? v0 : (c1 ? v1 : ... (c[n-1] ? v[n-1] : d)), built inside out.
Xsignal priority_chain(Xag& g, uint32_t arms, std::vector<Xsignal>* inputs = nullptr) {
  std::vector<Xsignal> c, v;
  for (uint32_t i = 0; i < arms; ++i) {
    c.push_back(g.input("c" + std::to_string(i)));
    v.push_back(g.input("v" + std::to_string(i)));
  }
  auto y = g.input("d");
  for (uint32_t i = arms; i-- > 0;) {
    y = g.mux(c[i], v[i], y);
  }
  if (inputs) {
    *inputs = c;
  }
  return y;
}
}  // namespace

TEST(XagMuxBalance, RecognizesTheXagMuxEncodingWithEveryPhase) {
  for (int phase = 0; phase < 8; ++phase) {
    Xag        g;
    auto       s = g.input("s"), t = g.input("t"), f = g.input("f");
    const auto st = (phase & 1) ? ~t : t, sf = (phase & 2) ? ~f : f;
    auto       y = g.mux(s, st, sf);
    if (phase & 4) {
      y = ~y;
    }
    if (g.node(y.id).kind != Xag::Kind::xor_gate) {
      continue;  // Xag::mux folded a degenerate form
    }
    auto m = match_mux(g, y.id);
    ASSERT_TRUE(m) << phase;
    // Rebuild from the match and compare all eight assignments.
    Xag        h;
    auto       hs = h.input("s"), ht = h.input("t"), hf = h.input("f");
    const auto map = [&](Xsignal x) {
      const auto base = x.id == s.id ? hs : x.id == t.id ? ht : hf;
      return x.inverted ? ~base : base;
    };
    auto r = h.mux(map(m->select), map(m->when_true), map(m->when_false));
    if (m->inverted != y.inverted) {
      r = ~r;
    }
    for (uint32_t x = 0; x < 8; ++x) {
      ASSERT_EQ(evaluate(g, std::array{y}, x), evaluate(h, std::array{r}, x)) << phase << " " << x;
    }
  }
}

TEST(XagMuxBalance, ThreeArmChainsAreExactWhateverTheirOrientation) {
  for (int variant = 0; variant < 4; ++variant) {
    Xag     g;
    auto    c0 = g.input("c0"), c1 = g.input("c1"), c2 = g.input("c2");
    auto    v0 = g.input("v0"), v1 = g.input("v1"), v2 = g.input("v2"), d = g.input("d");
    Xsignal inner = g.mux(c2, v2, d);
    if (variant & 1) {
      inner = ~inner;  // an inverted nested mux
    }
    Xsignal mid  = (variant & 2) ? g.mux(c1, inner, v1) : g.mux(c1, v1, inner);  // chain on the true arm
    Xsignal root = g.mux(c0, v0, mid);
    Budget  work{10000000};
    auto    result = balance_mux_chains(g, std::array{root}, work);
    equivalent_mux(g, std::array{root}, result);
    EXPECT_EQ(result.chains, 1U) << variant;
    EXPECT_EQ(result.arms, 3U) << variant;
  }
}

TEST(XagMuxBalance, LongChainBecomesLogarithmicAndStaysExact) {
  Xag    g;
  auto   root   = priority_chain(g, 12);
  auto   before = g.node(root.id).level;
  Budget work{10000000};
  auto   result = balance_mux_chains(g, std::array{root}, work);
  equivalent_mux(g, std::array{root}, result, 20000);
  EXPECT_EQ(result.chains, 1U);
  EXPECT_EQ(result.arms, 12U);
  const auto after = result.graph.node(result.outputs[0].id).level;
  EXPECT_LT(after * 2, before) << "12 arms: " << before << " levels before, " << after << " after";
}

TEST(XagMuxBalance, SharedNestedMuxAndShortChainsStayAsBuilt) {
  Xag    g;
  auto   c0 = g.input("c0"), c1 = g.input("c1"), c2 = g.input("c2");
  auto   v0 = g.input("v0"), v1 = g.input("v1"), v2 = g.input("v2"), d = g.input("d");
  auto   shared = g.mux(c2, v2, d);
  auto   a      = g.mux(c1, v1, shared);
  auto   b      = g.mux(c0, v0, shared);  // `shared` has two readers: a boundary for both chains
  Budget work{10000000};
  auto   result = balance_mux_chains(g, std::array{a, b}, work);
  equivalent_mux(g, std::array{a, b}, result);
  EXPECT_EQ(result.chains, 0U) << "two-arm chains are below the default three";
  Budget work2{10000000};
  auto   two = balance_mux_chains(g, std::array{a, b}, work2, 2000000, 2);
  equivalent_mux(g, std::array{a, b}, two);
  EXPECT_EQ(two.chains, 0U) << "neither chain may absorb the shared mux, so each keeps a single arm";
  // Control: the same two-level shape with an unshared inner mux is one chain.
  Xag    h;
  auto   e0 = h.input("c0"), e1 = h.input("c1"), w0 = h.input("v0"), w1 = h.input("v1"), dd = h.input("d");
  auto   own = h.mux(e0, w0, h.mux(e1, w1, dd));
  Budget work3{10000000};
  auto   one = balance_mux_chains(h, std::array{own}, work3, 2000000, 2);
  equivalent_mux(h, std::array{own}, one);
  EXPECT_EQ(one.chains, 1U);
  EXPECT_EQ(one.arms, 2U);
}

// The Lnet import spells a mux (s & t) | (~s & f); chains of that AND/OR form
// are recognized and rebuilt exactly like the XOR form.
TEST(XagMuxBalance, AndOrFormChainsFromTheLnetImportAreRebuilt) {
  Xag                  g;
  std::vector<Xsignal> c, v;
  for (int i = 0; i < 4; ++i) {
    c.push_back(g.input("c" + std::to_string(i)));
    v.push_back(g.input("v" + std::to_string(i)));
  }
  auto y = g.input("d");
  for (int i = 4; i-- > 0;) {
    y = g.lor(g.land(c[i], v[i]), g.land(~c[i], y));
  }
  ASSERT_TRUE(match_mux(g, y.id));
  const auto before = g.node(y.id).level;
  Budget     work{10000000};
  auto       result = balance_mux_chains(g, std::array{y}, work);
  equivalent_mux(g, std::array{y}, result);
  EXPECT_EQ(result.chains, 1U);
  EXPECT_EQ(result.arms, 4U);
  EXPECT_LT(result.graph.node(result.outputs[0].id).level, before);
}

TEST(XagMuxBalance, ExhaustionPublishesNoPartialNetwork) {
  Xag  g;
  auto root = priority_chain(g, 8);
  for (uint64_t credits : {1ULL, 50ULL, 200ULL}) {
    Budget work{credits};
    auto   result = balance_mux_chains(g, std::array{root}, work);
    if (result.status != Status::feasible) {
      EXPECT_EQ(result.status, Status::search_exhausted);
      EXPECT_TRUE(result.outputs.empty());
    }
  }
}
TEST(XagMuxBalance, FinalFallbackMuxFitsTheNodeAdmissionLimit) {
  Xag        graph;
  const auto root = priority_chain(graph, 8);
  for (uint32_t limit = graph.size(); limit < graph.size() + 40; ++limit) {
    SCOPED_TRACE(limit);
    Budget     work{10000000};
    const auto result = balance_mux_chains(graph, std::array{root}, work, limit);
    if (result.status == Status::feasible) {
      EXPECT_LE(result.graph.size(), limit);
    } else {
      EXPECT_EQ(result.status, Status::search_exhausted);
      EXPECT_TRUE(result.outputs.empty());
      EXPECT_EQ(result.graph.size(), 1U);
    }
  }
}
}  // namespace livehd::usyn
