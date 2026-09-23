// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "unate.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <random>
#include <set>

#include "gtest/gtest.h"

namespace livehd::synth {
TEST(UnateBudget, RecipeObservationIncludesRefusedAttemptsWithoutChangingSearch) {
  Logic_network source;
  source.outputs.push_back(source.add_source());
  Search_options options;
  options.recipes = {
      {1, 13, 8, 2},
      {1, 2, 8, 2},
      {2, 2, 8, 2}
  };
  std::vector<std::pair<size_t, bool>> events;
  options.observe_recipe = [&](size_t recipe, bool enter) { events.emplace_back(recipe, enter); };
  auto result            = optimize(source, options);
  ASSERT_EQ(result.attempts.size(), 3);
  EXPECT_EQ(result.attempts[0].status, Status::unsupported);
  EXPECT_EQ(result.attempts[1].status, Status::feasible);
  EXPECT_EQ(events,
            (std::vector<std::pair<size_t, bool>>{
                {0,  true},
                {0, false},
                {1,  true},
                {1, false},
                {2,  true},
                {2, false}
  }));
  events.clear();
  options.admission = [] { return false; };
  result            = optimize(source, options);
  ASSERT_EQ(result.attempts.size(), 2);
  EXPECT_EQ(result.attempts[1].status, Status::search_exhausted);
  EXPECT_EQ(events,
            (std::vector<std::pair<size_t, bool>>{
                {0,  true},
                {0, false},
                {1,  true},
                {1, false}
  }));
}
TEST(UnateBudget, ResourceCancellationStopsSearchWithoutInfeasibilityClaim) {
  Logic_network source;
  source.outputs.push_back(source.add_source());
  Search_options options;
  unsigned       calls = 0;
  options.admission    = [&] {
    ++calls;
    return false;
  };
  const auto result = optimize(source, options);
  ASSERT_EQ(result.attempts.size(), 1);
  EXPECT_EQ(result.status, Status::search_exhausted);
  EXPECT_EQ(result.attempts[0].reason, "resource budget exhausted");
  EXPECT_EQ(calls, 1);
}
namespace {
Truth_table table(uint32_t n, uint64_t bits) {
  Truth_table t(n);
  t.words[0] = bits;
  return t;
}
Id gate(Logic_network& g, Id a, Id b, uint64_t bits = 8) { return g.add_function({a, b}, table(2, bits)); }

bool evaluate(const Logic_network& g, uint64_t assignment) {
  std::vector<bool> values;
  uint32_t          next = 0;
  for (const auto& node : g.nodes) {
    if (node.source) {
      values.push_back(((assignment >> next++) & 1) != 0);
    } else {
      uint32_t x = 0;
      for (uint32_t j = 0; j < node.inputs.size(); ++j) {
        x |= uint32_t(values[node.inputs[j]]) << j;
      }
      values.push_back(node.table.get(x));
    }
  }
  return values[g.outputs[0]];
}
std::vector<bool> evaluate(const Unate_network& g, const Logic_network& source, uint64_t assignment) {
  std::vector<bool> inputs(source.nodes.size());
  uint32_t          next = 0;
  for (Id id = 0; id < source.nodes.size(); ++id) {
    if (source.nodes[id].source) {
      inputs[id] = ((assignment >> next++) & 1) != 0;
    }
  }
  std::vector<bool> values;
  for (const auto& node : g.nodes) {
    if (node.kind == Node_kind::source) {
      values.push_back(inputs[node.origin]);
    } else if (node.kind == Node_kind::source_inverter) {
      values.push_back(!values[node.ports[0]]);
    } else {
      bool value = false;
      for (const auto& term : node.terms) {
        bool product = true;
        for (auto port : term) {
          product = product && values[node.ports[port]];
        }
        value = value || product;
      }
      values.push_back(value);
    }
  }
  std::vector<bool> outputs;
  for (auto out : g.outputs) {
    outputs.push_back(values[out]);
  }
  return outputs;
}

TEST(Unate, EveryFourInputFunctionHasVerifiedTotalCover) {
  for (uint32_t bits = 0; bits < 65536; ++bits) {
    const auto t = table(4, bits);
    Budget     work{100000};
    auto       f = make_form(t, 64, 4, work);
    ASSERT_EQ(f.status, Status::feasible) << bits;
    // Deliberately evaluate without using check_form/covers.
    for (uint32_t x = 0; x < 16; ++x) {
      bool y = false;
      for (auto c : f.cubes) {
        bool term = true;
        for (uint32_t j = 0; j < 4; ++j) {
          if ((c.care >> j) & 1) {
            term &= ((x >> j) & 1) == ((c.ones >> j) & 1);
          }
        }
        y |= term;
      }
      ASSERT_EQ(y, ((bits >> x) & 1) != 0) << bits << ':' << x;
    }
  }
}

TEST(Unate, EveryThreeInputPartialFunctionHasAValidTotalCompletion) {
  for (uint32_t code = 0; code < 6561; ++code) {
    Truth_table onset(3), care(3);
    auto        digits = code;
    for (uint32_t x = 0; x < 8; ++x) {
      const auto digit  = digits % 3;
      digits           /= 3;
      care.set(x, digit != 2);
      onset.set(x, digit == 1);
    }
    Budget     budget{100000};
    const auto form = make_form(onset, care, 24, 3, budget);
    ASSERT_EQ(form.status, Status::feasible) << code;
    for (uint32_t x = 0; x < 8; ++x) {
      bool actual = false;
      for (const auto& cube : form.cubes) {
        actual |= (x & cube.care) == cube.ones;
      }
      if (care.get(x)) {
        ASSERT_EQ(actual, onset.get(x)) << code << ':' << x;
      }
    }
  }
  Budget     budget{10000};
  const auto partial = make_form(table(2, 6), table(2, 7), 2, 1, budget);
  ASSERT_EQ(partial.status, Status::feasible);
  EXPECT_EQ(partial.positive, 3);
  EXPECT_EQ(partial.negative, 0);
  EXPECT_EQ(partial.series, 1);  // XOR on this image completes to OR.
}

TEST(Unate, ExactImagesPreserveOrderAndRefuseUnprovedAbsence) {
  Logic_network g;
  auto          x = g.add_source(), y = g.add_source();
  const auto    a = gate(g, x, y);
  Budget        budget{10000};
  const auto    image = exact_image(g, {a, x}, 2, budget);
  ASSERT_EQ(image.status, Status::feasible);
  EXPECT_EQ(image.care, table(2, 13));
  EXPECT_EQ(image.sources, (std::vector<Id>{x, y}));
  EXPECT_EQ(exact_image(g, {x, a}, 2, budget).care, table(2, 11));
  EXPECT_EQ(exact_image(g, {a, x}, 1, budget).status, Status::unsupported);
  EXPECT_EQ(exact_image(g, {a, a}, 2, budget).status, Status::invalid);
  Budget empty{0};
  EXPECT_EQ(exact_image(g, {a, x}, 2, empty).status, Status::search_exhausted);
}

TEST(Unate, ImageCompletionEliminatesUnnecessaryInternalTwins) {
  Logic_network g;
  const auto    x = g.add_source(), y = g.add_source(), z = g.add_source();
  const auto    a  = gate(g, x, y);
  const auto    ny = g.add_function({y}, table(1, 1));
  const auto    b  = gate(g, gate(g, x, ny), z);
  g.outputs        = {gate(g, a, b, 6)};
  Search_options options;
  options.recipes = {
      {3, 2, 8, 2}
  };
  options.recovery_rounds = 0;
  options.joint_limit     = 0;
  options.image_inputs    = 0;
  const auto total        = optimize(g, options);
  ASSERT_EQ(total.status, Status::feasible);
  options.image_inputs = 3;
  const auto result    = optimize(g, options);
  ASSERT_EQ(result.status, Status::feasible);
  const auto& attempt = result.attempts[0];
  const auto& net     = attempt.network;
  ASSERT_LT(net.nodes.size(), total.attempts[0].network.nodes.size());
  EXPECT_GT(attempt.image_candidates, 0);
  const auto root = net.outputs[0];
  ASSERT_TRUE(net.nodes[root].care);
  EXPECT_EQ(*net.nodes[root].care, table(2, 7));
  EXPECT_EQ(net.nodes[root].completion, table(2, 14));
  EXPECT_EQ(net.nodes[root].image_sources, (std::vector<Id>{x, y, z}));
  for (uint32_t assignment = 0; assignment < 8; ++assignment) {
    EXPECT_EQ(evaluate(net, g, assignment)[0], evaluate(g, assignment));
  }
  Budget proof{100000};
  EXPECT_TRUE(verify(g, net, options.recipes[0], proof));
  auto corrupt = net;
  corrupt.nodes[root].care->set(0, false);
  EXPECT_FALSE(verify(g, corrupt, options.recipes[0], proof));
  corrupt = net;
  corrupt.nodes[root].image_sources.pop_back();
  EXPECT_FALSE(verify(g, corrupt, options.recipes[0], proof));
  corrupt = net;
  corrupt.nodes[root].completion.set(3, false);  // Form must match completion even OFF image.
  EXPECT_FALSE(verify(g, corrupt, options.recipes[0], proof));
  auto changed           = g;
  changed.nodes[a].table = table(2, 14);
  EXPECT_FALSE(verify(changed, net, options.recipes[0], proof));
  options.image_inputs   = 1;
  options.symbolic_nodes = 0;  // Isolate the exhaustive-source admission limit.
  const auto limited     = optimize(g, options);
  ASSERT_EQ(limited.status, Status::feasible);
  EXPECT_GT(limited.attempts[0].image_limited, 0);
  EXPECT_FALSE(limited.attempts[0].network.nodes[limited.attempts[0].network.outputs[0]].care);
}

Logic_network paid_divisor_fixture() {
  Logic_network g;
  const auto    a = g.add_source(), b = g.add_source(), c = g.add_source();
  const auto    paid = gate(g, a, b, 14);
  const auto    ac = gate(g, a, c), bc = gate(g, b, c);
  g.outputs = {paid, gate(g, ac, bc, 14)};
  return g;
}

TEST(Unate, DependencyProofUsesOriginalSourcesAndDistinguishesRefutationFromLimits) {
  auto       g = paid_divisor_fixture();
  Budget     budget{100000};
  const auto root = g.outputs[1], paid = g.outputs[0];
  const auto proof = exact_dependency(g, root, {paid, 2}, 3, budget);
  ASSERT_EQ(proof.status, Dependency_status::proven);
  EXPECT_EQ(proof.table, table(2, 8));
  EXPECT_EQ(proof.care, table(2, 15));
  EXPECT_EQ(proof.sources, (std::vector<Id>{0, 1, 2}));
  EXPECT_EQ(exact_dependency(g, root, {paid}, 3, budget).status, Dependency_status::refuted);
  EXPECT_EQ(exact_dependency(g, root, {paid, 2}, 2, budget).status, Dependency_status::unsupported);
  EXPECT_EQ(exact_dependency(g, root, {root}, 3, budget).status, Dependency_status::invalid);
  const auto asymmetric = gate(g, paid, 2, 2);
  EXPECT_EQ(exact_dependency(g, asymmetric, {paid, 2}, 3, budget).table, table(2, 2));
  EXPECT_EQ(exact_dependency(g, asymmetric, {2, paid}, 3, budget).table, table(2, 4));
  const auto constant = g.add_function({}, table(0, 1));
  EXPECT_EQ(exact_dependency(g, constant, {}, 0, budget).status, Dependency_status::proven);
  EXPECT_EQ(exact_dependency(g, root, {}, 3, budget).status, Dependency_status::refuted);
  Budget empty{0};
  EXPECT_EQ(exact_dependency(g, root, {paid, 2}, 3, empty).status, Dependency_status::exhausted);
}

TEST(Unate, PaidDivisorsOutsideStructuralFanInReduceTheCompleteSharedNetwork) {
  const auto     g = paid_divisor_fixture();
  Search_options options;
  options.recipes = {
      {2, 2, 8, 2}
  };
  options.recovery_rounds = 0;
  options.joint_limit     = 0;
  options.image_inputs    = 3;
  options.divisor_limit   = 0;
  const auto seed         = optimize(g, options);
  ASSERT_EQ(seed.status, Status::feasible);
  options.divisor_limit = 64;
  const auto result     = optimize(g, options);
  ASSERT_EQ(result.status, Status::feasible);
  const auto& attempt = result.attempts[0];
  ASSERT_TRUE(attempt.divisor_recovered) << attempt.divisor_reason;
  const auto count = [](const Unate_network& net) {
    return std::count_if(net.nodes.begin(), net.nodes.end(), [](const auto& n) { return n.kind != Node_kind::source; });
  };
  EXPECT_EQ(count(seed.attempts[0].network), 4);
  const auto& net = *attempt.divisor_recovered;
  EXPECT_EQ(count(net), 2);
  EXPECT_FALSE(attempt.divisor_exhausted);
  EXPECT_GT(attempt.divisor_refuted, 0);
  EXPECT_GT(attempt.divisor_improvements, 0);
  EXPECT_TRUE(net.nodes[net.outputs[1]].functional);
  EXPECT_EQ(net.nodes[net.outputs[1]].witness_inputs, (std::vector<Id>{2, 3}));
  Budget proof{100000};
  EXPECT_TRUE(verify(g, net, options.recipes[0], proof));
  for (uint32_t x = 0; x < 8; ++x) {
    const auto values = evaluate(net, g, x);
    EXPECT_EQ(values[0], (x & 3) != 0);
    EXPECT_EQ(values[1], (x & 3) != 0 && (x & 4) != 0);
  }
  auto corrupted                                   = net;
  corrupted.nodes[corrupted.outputs[1]].functional = false;
  EXPECT_FALSE(verify(g, corrupted, options.recipes[0], proof));
  auto changed           = g;
  changed.nodes[5].table = table(2, 14);
  EXPECT_FALSE(verify(changed, net, options.recipes[0], proof));
  options.work       = seed.attempts[0].work;
  const auto stopped = optimize(g, options);
  ASSERT_EQ(stopped.status, Status::feasible);
  EXPECT_TRUE(stopped.attempts[0].divisor_exhausted);
  EXPECT_FALSE(stopped.attempts[0].divisor_recovered);
  EXPECT_EQ(count(stopped.attempts[0].network), 4);
  options.work    = attempt.work - 1;
  const auto late = optimize(g, options);
  ASSERT_EQ(late.status, Status::feasible);
  EXPECT_TRUE(late.attempts[0].divisor_exhausted);
  ASSERT_TRUE(late.attempts[0].divisor_recovered);
  EXPECT_TRUE(verify(g, *late.attempts[0].divisor_recovered, options.recipes[0], proof));
  options.work          = 5000000;
  options.divisor_limit = 1;
  const auto capped     = optimize(g, options);
  ASSERT_EQ(capped.status, Status::feasible);
  EXPECT_LE(capped.attempts[0].divisor_queries, static_cast<uint64_t>(count(seed.attempts[0].network)));
  EXPECT_FALSE(capped.attempts[0].divisor_recovered);
  EXPECT_FALSE(capped.attempts[0].divisor_exhausted);
  options.divisor_limit  = 64;
  options.image_inputs   = 1;
  options.symbolic_nodes = 0;  // Isolate the exhaustive-source admission limit.
  const auto limited     = optimize(g, options);
  ASSERT_EQ(limited.status, Status::feasible);
  EXPECT_GT(limited.attempts[0].divisor_limited, 0);
  EXPECT_FALSE(limited.attempts[0].divisor_recovered);
  options.divisor_limit = 4097;
  EXPECT_EQ(optimize(g, options).status, Status::unsupported);
}

TEST(Unate, PaidDivisorRecoveryProvesBothDemandedPolarities) {
  auto       g    = paid_divisor_fixture();
  const auto root = g.outputs[1];
  const auto q    = g.add_source();
  g.outputs.push_back(gate(g, root, q, 4));  // q & !root requires the opposite rail too.
  Search_options options;
  options.recipes = {
      {3, 2, 8, 2}
  };
  options.recovery_rounds = 0;
  options.joint_limit     = 0;
  const auto result       = optimize(g, options);
  ASSERT_EQ(result.status, Status::feasible);
  ASSERT_TRUE(result.attempts[0].divisor_recovered);
  const auto& net = *result.attempts[0].divisor_recovered;
  EXPECT_TRUE(std::any_of(net.nodes.begin(), net.nodes.end(), [&](const auto& node) {
    return node.origin == root && node.negative && node.functional;
  }));
  Budget proof{100000};
  EXPECT_TRUE(verify(g, net, options.recipes[0], proof));
  for (uint32_t x = 0; x < 16; ++x) {
    const auto values = evaluate(net, g, x);
    const bool paid = (x & 3) != 0, root_value = paid && (x & 4) != 0;
    EXPECT_EQ(values, (std::vector<bool>{paid, root_value, (x & 8) != 0 && !root_value}));
  }
}

TEST(Unate, UncoveredOutputUsesExistingDivisorsBeforeStructuralFallback) {
  Logic_network g;
  const auto    a = g.add_source(), b = g.add_source(), c = g.add_source(), d = g.add_source();
  const auto    paid = gate(g, c, d), ab = gate(g, a, b);
  const auto    root = gate(g, gate(g, ab, c), d);
  g.outputs          = {paid, root, root};  // Repeated outputs must share the recovered root.
  Search_options options;
  options.recipes = {
      {2, 2, 8, 2}
  };
  options.recovery_rounds = 0;
  options.joint_limit     = 0;
  options.divisor_limit   = 0;
  options.cover_limit     = 0;
  options.encoding_limit  = 0;  // Isolate recovery using existing producers.
  options.reshape_limit   = 0;
  const auto structural   = optimize(g, options);
  ASSERT_EQ(structural.status, Status::search_exhausted);
  EXPECT_EQ(structural.attempts[0].covered_outputs, 1);
  EXPECT_TRUE(structural.attempts[0].network.nodes.empty());
  options.cover_limit = 32;
  const auto result   = optimize(g, options);
  ASSERT_EQ(result.status, Status::feasible);
  const auto& attempt = result.attempts[0];
  EXPECT_EQ(attempt.cover_roots, 1);
  EXPECT_EQ(attempt.covered_outputs, 3);
  EXPECT_GT(attempt.cover_refuted, 0);
  EXPECT_LE(attempt.cover_queries, 32);
  EXPECT_EQ(attempt.cover_reason, "covered_by_existing_divisors");
  const auto& net = attempt.network;
  EXPECT_EQ(net.depth, 2);
  EXPECT_EQ(net.outputs[1], net.outputs[2]);
  EXPECT_TRUE(net.nodes[net.outputs[1]].functional);
  EXPECT_EQ(net.nodes[net.outputs[1]].witness_inputs, (std::vector<Id>{paid, ab}));
  Budget proof{100000};
  EXPECT_TRUE(verify(g, net, options.recipes[0], proof));
  for (uint32_t x = 0; x < 16; ++x) {
    EXPECT_EQ(evaluate(net, g, x), (std::vector<bool>{(x & 12) == 12, x == 15, x == 15}));
  }
  auto recovered_options            = options;
  recovered_options.recovery_rounds = 2;
  recovered_options.joint_limit     = 256;
  recovered_options.divisor_limit   = 64;
  const auto recovered              = optimize(g, recovered_options);
  ASSERT_EQ(recovered.status, Status::feasible);
  const auto& recovered_attempt = recovered.attempts[0];
  // The only retained local alternative equals the selected recovered choice.
  EXPECT_EQ(recovered_attempt.recovery_checks, 0);
  EXPECT_FALSE(recovered_attempt.recovery_exhausted);
  EXPECT_GT(recovered_attempt.joint_checks, 0);
  EXPECT_TRUE(verify(g, recovered_attempt.network, options.recipes[0], proof));
  for (const auto* candidate :
       {&recovered_attempt.recovered, &recovered_attempt.joint_recovered, &recovered_attempt.divisor_recovered}) {
    if (*candidate) {
      EXPECT_TRUE(verify(g, **candidate, options.recipes[0], proof));
    }
  }
  // Exhaustion before completing the initial proof must never publish a network,
  // even when all outputs already have candidate choices.
  options.work       = attempt.work - 1;
  const auto stopped = optimize(g, options);
  ASSERT_EQ(stopped.status, Status::search_exhausted);
  EXPECT_EQ(stopped.attempts[0].covered_outputs, 3);
  EXPECT_TRUE(stopped.attempts[0].network.nodes.empty());
  options.work     = structural.attempts[0].work + 1;
  const auto early = optimize(g, options);
  EXPECT_EQ(early.status, Status::search_exhausted);
  EXPECT_TRUE(early.attempts[0].cover_exhausted);
  EXPECT_TRUE(early.attempts[0].network.nodes.empty());
  options.work        = 5000000;
  options.cover_limit = 1;
  const auto capped   = optimize(g, options);
  EXPECT_EQ(capped.status, Status::search_exhausted);
  EXPECT_EQ(capped.attempts[0].cover_queries, 1);
  EXPECT_EQ(capped.attempts[0].cover_refuted, 1);
  EXPECT_EQ(capped.attempts[0].cover_reason, "bounded_dependency_search_incomplete");
  options.cover_limit    = 32;
  options.image_inputs   = 3;
  options.symbolic_nodes = 0;  // Isolate the exhaustive-source admission limit.
  const auto limited     = optimize(g, options);
  EXPECT_EQ(limited.status, Status::search_exhausted);
  EXPECT_GT(limited.attempts[0].cover_limited, 0);
  EXPECT_EQ(limited.attempts[0].cover_refuted, 0);
  options.cover_limit = 4097;
  EXPECT_EQ(optimize(g, options).status, Status::unsupported);
}

TEST(Unate, SymbolicRelationsMatchExhaustiveQueries) {
  std::mt19937 random(0xbed51);
  for (unsigned trial = 0; trial < 100; ++trial) {
    Logic_network g;
    for (unsigned i = 0; i < 6; ++i) {
      g.add_source();
    }
    for (unsigned i = 0; i < 12; ++i) {
      gate(g, random() % g.nodes.size(), random() % g.nodes.size(), random() & 15);
    }
    const Id              root = g.nodes.size() - 1;
    const std::vector<Id> leaves{0, 8, 14};
    Budget                exact_work{1000000}, symbolic_work{1000000};
    const auto            exact    = exact_image(g, leaves, 12, exact_work);
    const auto            symbolic = exact_image(g, leaves, 0, symbolic_work, 4096);
    ASSERT_EQ(symbolic.status, exact.status);
    EXPECT_TRUE(symbolic.symbolic);
    EXPECT_EQ(symbolic.care, exact.care);
    EXPECT_EQ(symbolic.sources, exact.sources);
    const auto a = exact_dependency(g, root, leaves, 12, exact_work);
    const auto b = exact_dependency(g, root, leaves, 0, symbolic_work, 4096);
    ASSERT_EQ(a.status, b.status);
    if (a.status == Dependency_status::proven) {
      EXPECT_EQ(a.table, b.table);
      EXPECT_EQ(a.care, b.care);
      EXPECT_EQ(a.sources, b.sources);
    }
  }
}

TEST(Unate, SymbolicImageProvesLargeSourceCorrelationAndLimitsRemainInconclusive) {
  Logic_network g;
  for (unsigned i = 0; i < 16; ++i) {
    g.add_source();
  }
  Id forward = 0, reverse = 15;
  for (Id i = 1; i < 16; ++i) {
    forward = gate(g, forward, i);
  }
  for (Id i = 15; i > 0; --i) {
    reverse = gate(g, reverse, i - 1);
  }
  const auto root = gate(g, forward, reverse, 6);
  g.outputs       = {root};
  Budget work{1000000};
  EXPECT_EQ(exact_image(g, {forward, reverse}, 12, work).status, Status::unsupported);
  const auto image = exact_image(g, {forward, reverse}, 12, work, 4096);
  ASSERT_EQ(image.status, Status::feasible);
  EXPECT_TRUE(image.symbolic);
  EXPECT_EQ(image.care, table(2, 9));
  EXPECT_EQ(image.sources.size(), 16);
  const auto dependency = exact_dependency(g, reverse, {forward}, 12, work, 4096);
  ASSERT_EQ(dependency.status, Dependency_status::proven);
  EXPECT_EQ(dependency.table, table(1, 2));
  EXPECT_EQ(dependency.care, table(1, 3));
  EXPECT_EQ(exact_dependency(g, reverse, {0}, 12, work, 4096).status, Dependency_status::refuted);
  const auto limited = exact_image(g, {forward, reverse}, 12, work, 1);
  EXPECT_EQ(limited.status, Status::unsupported);
  EXPECT_TRUE(limited.symbolic);
  EXPECT_FALSE(work.exhausted);
  Budget     short_work{2000};
  const auto interrupted = exact_image(g, {forward, reverse}, 12, short_work, 4096);
  EXPECT_EQ(interrupted.status, Status::search_exhausted);
  EXPECT_TRUE(interrupted.symbolic);
  EXPECT_TRUE(short_work.exhausted);
  Budget   cancelled{1000000};
  unsigned admissions = 0;
  cancelled.admission_interval = 1024;
  cancelled.admission          = [&] { return ++admissions == 1; };
  const auto resource = exact_image(g, {forward, reverse}, 12, cancelled, 4096);
  EXPECT_EQ(resource.status, Status::search_exhausted);
  EXPECT_TRUE(resource.symbolic);
  EXPECT_TRUE(cancelled.resource_exhausted);
  Search_options options;
  options.recipes = {
      {2, 2, 8, 2}
  };
  options.recovery_rounds = options.joint_limit = options.divisor_limit = options.cover_limit = 0;
  options.symbolic_nodes                                                                      = 0;
  EXPECT_EQ(optimize(g, options).status, Status::search_exhausted);
  options.symbolic_nodes = 1;
  const auto refused     = optimize(g, options);
  EXPECT_EQ(refused.status, Status::search_exhausted);
  EXPECT_GT(refused.attempts[0].image_limited, 0);
  EXPECT_TRUE(refused.attempts[0].network.nodes.empty());
  options.symbolic_nodes = 4096;
  const auto result      = optimize(g, options);
  ASSERT_EQ(result.status, Status::feasible);
  EXPECT_GT(result.attempts[0].image_symbolic, 0);
  const auto& net = result.attempts[0].network;
  ASSERT_EQ(net.nodes.size(), 1);
  EXPECT_TRUE(net.nodes[0].care);
  EXPECT_EQ(net.nodes[0].image_sources.size(), 16);
  EXPECT_TRUE(net.nodes[0].logical_inputs.empty());
  EXPECT_FALSE(net.nodes[0].completion.get(0));
  EXPECT_TRUE(verify(g, net, options.recipes[0], work));
  auto corrupted = net;
  corrupted.nodes[0].care->set(1, true);
  EXPECT_FALSE(verify(g, corrupted, options.recipes[0], work));
  options.symbolic_nodes = max_symbolic_nodes + 1;
  EXPECT_EQ(optimize(g, options).status, Status::unsupported);

  Logic_network oversize;
  for (unsigned i = 0; i <= max_symbolic_sources; ++i) {
    oversize.add_source();
  }
  Id merged = 0;
  for (Id i = 1; i <= max_symbolic_sources; ++i) {
    merged = gate(oversize, merged, i);
  }
  const auto admitted = exact_image(oversize, {merged}, 12, work, 4096);
  EXPECT_EQ(admitted.status, Status::unsupported);
  EXPECT_FALSE(admitted.symbolic);
  EXPECT_FALSE(work.exhausted);
}

TEST(Unate, NewSharedCofactorEncodingRecoversOutputsAndChargesBothRails) {
  Logic_network g;
  for (unsigned i = 0; i < 5; ++i) {
    g.add_source();
  }
  Truth_table first(4), second(4);
  for (uint32_t x = 0; x < 16; ++x) {
    const bool parity = (std::popcount(x & 7) & 1) != 0;
    first.set(x, parity && (x & 8));
    second.set(x, !parity || (x & 8));
  }
  const auto a = g.add_function({0, 1, 2, 3}, first);
  const auto b = g.add_function({0, 1, 2, 4}, second);
  g.outputs    = {a, b, a};
  Search_options options;
  options.recipes = {
      {2, 3, 16, 3}
  };
  options.cover_limit = options.divisor_limit = options.joint_limit = options.recovery_rounds = 0;
  options.encoding_pair_limit                                                                 = 0;
  options.encoding_limit                                                                      = 0;
  const auto structural                                                                       = optimize(g, options);
  ASSERT_EQ(structural.status, Status::search_exhausted);
  options.encoding_limit = 16;
  const auto result      = optimize(g, options);
  ASSERT_EQ(result.status, Status::feasible) << result.attempts[0].reason;
  const auto& attempt = result.attempts[0];
  EXPECT_EQ(attempt.encoding_reason, "verified_shared_encoding");
  EXPECT_EQ(attempt.encoding_classes, 2);
  EXPECT_EQ(attempt.encoding_bits, 1);
  const auto& net = attempt.network;
  ASSERT_EQ(net.encodings.size(), 1);
  EXPECT_EQ(net.encodings[0].inputs, (std::vector<Id>{0, 1, 2}));
  EXPECT_EQ(net.encodings[0].table, table(3, 150));
  EXPECT_EQ(net.outputs[0], net.outputs[2]);
  EXPECT_EQ(net.depth, 2);
  EXPECT_EQ(std::count_if(net.nodes.begin(), net.nodes.end(), [&](const auto& n) { return n.origin == g.nodes.size(); }), 2);
  EXPECT_EQ(net.source_inverters, 3);
  Budget proof{1000000};
  EXPECT_TRUE(verify(g, net, options.recipes[0], proof));
  for (uint32_t x = 0; x < 32; ++x) {
    const bool parity = (std::popcount(x & 7) & 1) != 0;
    EXPECT_EQ(evaluate(net, g, x), (std::vector<bool>{parity && (x & 8), !parity || (x & 16), parity && (x & 8)}));
  }
  auto corrupted = net;
  corrupted.encodings[0].table.set(0, true);
  EXPECT_FALSE(verify(g, corrupted, options.recipes[0], proof));
  corrupted                        = net;
  corrupted.encodings[0].inputs[0] = a;
  EXPECT_FALSE(verify(g, corrupted, options.recipes[0], proof));
  corrupted = net;
  corrupted.encodings.clear();
  EXPECT_FALSE(verify(g, corrupted, options.recipes[0], proof));
  options.work       = attempt.work - 1;
  const auto stopped = optimize(g, options);
  EXPECT_EQ(stopped.status, Status::search_exhausted);
  EXPECT_TRUE(stopped.attempts[0].network.nodes.empty());
  EXPECT_EQ(stopped.attempts[0].encoding_reason, "work_or_resource_exhausted");
  options.work  = 5000000;
  auto permuted = g;
  for (auto& node : permuted.nodes) {
    for (auto& input : node.inputs) {
      if (input == 1) {
        input = 3;
      } else if (input == 3) {
        input = 1;
      }
    }
  }
  options.encoding_limit = 1;
  const auto capped      = optimize(permuted, options);
  EXPECT_EQ(capped.status, Status::search_exhausted);
  EXPECT_EQ(capped.attempts[0].encoding_queries, 1);
  EXPECT_EQ(capped.attempts[0].encoding_reason, "proposal_limit");
  options.encoding_limit = 16;
  const auto later       = optimize(permuted, options);
  ASSERT_EQ(later.status, Status::feasible);
  EXPECT_GT(later.attempts[0].encoding_queries, 1);
  EXPECT_TRUE(verify(permuted, later.attempts[0].network, options.recipes[0], proof));
  options.recipes[0].literals = 2;
  EXPECT_EQ(optimize(g, options).status, Status::search_exhausted);
  options.encoding_limit = 4097;
  EXPECT_EQ(optimize(g, options).status, Status::unsupported);
}

TEST(Unate, DisjointCofactorBoundSetsRecoverAFunctionThatNoSingleSetCanCover) {
  Logic_network g;
  for (unsigned i = 0; i < 6; ++i) {
    g.add_source();
  }
  Truth_table first(6), second(6);
  for (uint32_t x = 0; x < 64; ++x) {
    const bool a = (std::popcount(x & 7) & 1) != 0;
    const bool b = (std::popcount(x >> 3) & 1) != 0;
    first.set(x, a && b);
    second.set(x, a || !b);
  }
  const auto y = g.add_function({0, 1, 2, 3, 4, 5}, first);
  const auto z = g.add_function({0, 1, 2, 3, 4, 5}, second);
  g.outputs    = {y, z, y, 5};
  Search_options options;
  options.recipes = {
      {2, 3, 16, 3}
  };
  options.cover_limit = options.divisor_limit = options.joint_limit = options.recovery_rounds = 0;
  options.encoding_pair_limit                                                                 = 0;
  EXPECT_EQ(optimize(g, options).status, Status::search_exhausted);
  options.encoding_pair_limit = 16;
  const auto result           = optimize(g, options);
  ASSERT_EQ(result.status, Status::feasible);
  const auto& attempt = result.attempts[0];
  const auto& net     = attempt.network;
  EXPECT_EQ(attempt.encoding_bound_sets, 2);
  EXPECT_EQ(attempt.encoding_pair_queries, 1);
  EXPECT_EQ(attempt.encoding_classes, 4);
  EXPECT_EQ(attempt.encoding_bits, 2);
  ASSERT_EQ(net.encodings.size(), 2);
  EXPECT_EQ(net.encodings[0].inputs, (std::vector<Id>{0, 1, 2}));
  EXPECT_EQ(net.encodings[1].inputs, (std::vector<Id>{3, 4, 5}));
  EXPECT_EQ(net.encodings[0].table, table(3, 150));
  EXPECT_EQ(net.encodings[1].table, table(3, 150));
  EXPECT_EQ(net.outputs[0], net.outputs[2]);
  EXPECT_EQ(net.depth, 2);
  EXPECT_EQ(net.source_inverters, 6);
  EXPECT_EQ(std::count_if(net.nodes.begin(), net.nodes.end(), [&](const auto& node) { return node.origin >= g.nodes.size(); }), 3);
  Budget proof{1000000};
  EXPECT_TRUE(verify(g, net, options.recipes[0], proof));
  for (uint32_t x = 0; x < 64; ++x) {
    EXPECT_EQ(evaluate(net, g, x), (std::vector<bool>{first.get(x), second.get(x), first.get(x), (x & 32) != 0}));
  }
  auto corrupted = net;
  corrupted.encodings[1].table.set(0, true);
  EXPECT_FALSE(verify(g, corrupted, options.recipes[0], proof));
  corrupted                        = net;
  corrupted.encodings[1].inputs[0] = 0;
  EXPECT_FALSE(verify(g, corrupted, options.recipes[0], proof));
  options.work       = attempt.work - 1;
  const auto stopped = optimize(g, options);
  EXPECT_EQ(stopped.status, Status::search_exhausted);
  EXPECT_TRUE(stopped.attempts[0].network.nodes.empty());
  EXPECT_EQ(stopped.attempts[0].encoding_reason, "work_or_resource_exhausted");
  options.work  = 5000000;
  auto permuted = g;
  for (auto& node : permuted.nodes) {
    for (auto& input : node.inputs) {
      if (input == 2) {
        input = 3;
      } else if (input == 3) {
        input = 2;
      }
    }
  }
  options.encoding_pair_limit = 1;
  const auto capped           = optimize(permuted, options);
  EXPECT_EQ(capped.status, Status::search_exhausted);
  EXPECT_EQ(capped.attempts[0].encoding_pair_queries, 1);
  EXPECT_EQ(capped.attempts[0].encoding_reason, "pair_proposal_limit");
  options.encoding_pair_limit = 16;
  const auto recovered        = optimize(permuted, options);
  ASSERT_EQ(recovered.status, Status::feasible);
  EXPECT_GT(recovered.attempts[0].encoding_pair_queries, 1);
  EXPECT_TRUE(verify(permuted, recovered.attempts[0].network, options.recipes[0], proof));
  options.recipes[0].literals = 2;
  EXPECT_EQ(optimize(g, options).status, Status::search_exhausted);
  options.encoding_pair_limit = 4097;
  EXPECT_EQ(optimize(g, options).status, Status::unsupported);
}

TEST(Unate, DisjointCofactorClassesProveTheJointUnusedCodeImage) {
  Logic_network g;
  for (unsigned i = 0; i < 8; ++i) {
    g.add_source();
  }
  Truth_table first(8), second(8);
  for (uint32_t x = 0; x < 256; ++x) {
    first.set(x, (x & 7) && (x & 112));
    second.set(x, ((x & 7) == 7) != ((x & 112) == 112));
  }
  const auto y = g.add_function({0, 1, 2, 3, 4, 5, 6, 7}, first);
  const auto z = g.add_function({0, 1, 2, 3, 4, 5, 6, 7}, second);
  g.outputs    = {y, z};
  Search_options options;
  options.recipes = {
      {2, 4, 32, 4}
  };
  options.cover_limit = options.divisor_limit = options.joint_limit = options.recovery_rounds = 0;
  const auto result                                                                           = optimize(g, options);
  ASSERT_EQ(result.status, Status::feasible);
  const auto& attempt = result.attempts[0];
  EXPECT_EQ(attempt.encoding_bound_sets, 2);
  EXPECT_EQ(attempt.encoding_classes, 9);
  EXPECT_EQ(attempt.encoding_bits, 4);
  const auto& net = attempt.network;
  ASSERT_EQ(net.encodings.size(), 4);
  ASSERT_TRUE(net.nodes[net.outputs[0]].care);
  EXPECT_EQ(*net.nodes[net.outputs[0]].care, table(4, 0x777));
  Budget proof{1000000};
  EXPECT_TRUE(verify(g, net, options.recipes[0], proof));
  for (uint32_t x = 0; x < 256; ++x) {
    EXPECT_EQ(evaluate(net, g, x), (std::vector<bool>{first.get(x), second.get(x)}));
  }
  auto corrupted = net;
  corrupted.nodes[corrupted.outputs[0]].care->set(12, true);
  EXPECT_FALSE(verify(g, corrupted, options.recipes[0], proof));
}

TEST(Unate, CofactorEncodingUsesTwoBitsForThreeClassesAndProvesUnusedCodeCare) {
  Logic_network g;
  for (unsigned i = 0; i < 4; ++i) {
    g.add_source();
  }
  Truth_table first(4), second(4);
  for (uint32_t x = 0; x < 16; ++x) {
    first.set(x, (x & 3) && (x & 8));
    second.set(x, ((x & 3) == 3) != ((x & 8) != 0));
  }
  const auto a = g.add_function({0, 1, 2, 3}, first);
  const auto b = g.add_function({0, 1, 2, 3}, second);
  g.outputs    = {a, b, 2};
  Search_options options;
  options.recipes = {
      {2, 3, 16, 3}
  };
  options.cover_limit = options.divisor_limit = options.joint_limit = options.recovery_rounds = 0;
  const auto result                                                                           = optimize(g, options);
  ASSERT_EQ(result.status, Status::feasible);
  const auto& attempt = result.attempts[0];
  EXPECT_EQ(attempt.encoding_classes, 3);
  EXPECT_EQ(attempt.encoding_bits, 2);
  const auto& net = attempt.network;
  ASSERT_EQ(net.encodings.size(), 2);
  ASSERT_TRUE(net.nodes[net.outputs[0]].care);
  EXPECT_EQ(*net.nodes[net.outputs[0]].care, table(3, 0x77));
  Budget work{1000000};
  EXPECT_TRUE(verify(g, net, options.recipes[0], work));
  for (uint32_t x = 0; x < 16; ++x) {
    EXPECT_EQ(evaluate(net, g, x), (std::vector<bool>{(x & 3) && (x & 8), ((x & 3) == 3) != ((x & 8) != 0), (x & 4) != 0}));
  }
  auto corrupted = net;
  corrupted.nodes[corrupted.outputs[0]].care->set(3, true);
  EXPECT_FALSE(verify(g, corrupted, options.recipes[0], work));
}

TEST(Unate, CofactorEncodingCanDiscardIrrelevantBoundSourcesWithoutInventingBits) {
  Logic_network g;
  g.add_source();
  g.add_source();
  const auto constant    = g.add_function({0, 1}, table(2, 0));
  const auto late_input  = g.add_source();
  const auto passthrough = g.add_function({late_input}, table(1, 2));
  g.outputs              = {constant, passthrough};
  Attempt attempt;
  attempt.recipe = {2, 2, 8, 2};
  attempt.status = Status::search_exhausted;
  Budget work{1000000};
  encode_residue(g, 16, attempt, work);
  ASSERT_EQ(attempt.status, Status::feasible);
  EXPECT_EQ(attempt.encoding_classes, 1);
  EXPECT_EQ(attempt.encoding_bits, 0);
  EXPECT_TRUE(attempt.network.encodings.empty());
  // The constant's functional witness includes a free source introduced after
  // its original definition. Verification must still query the original logic.
  EXPECT_TRUE(verify(g, attempt.network, attempt.recipe, work));
  for (uint32_t x = 0; x < 8; ++x) {
    EXPECT_EQ(evaluate(attempt.network, g, x), (std::vector<bool>{false, (x & 4) != 0}));
  }
}

TEST(Unate, ConsensusPrimeDoesNotForceSeriesDepth) {
  Truth_table mux(5);
  for (uint32_t x = 0; x < 32; ++x) {
    mux.set(x, (x & 1) ? (x & 6) == 6 : (x & 24) == 24);
  }
  Budget work{100000};
  auto   f = make_form(mux, 6, 3, work);
  ASSERT_EQ(f.status, Status::feasible);
  EXPECT_EQ(f.series, 3);
  EXPECT_EQ(f.literals, 6);
}

TEST(Unate, DynamicTablesAndExplicitExhaustion) {
  Truth_table parity(9);
  for (uint32_t x = 0; x < 512; ++x) {
    parity.set(x, (__builtin_popcount(x) & 1) != 0);
  }
  Budget enough{10000000};
  auto   f = make_form(parity, 2304, 9, enough);
  ASSERT_EQ(f.status, Status::feasible);
  EXPECT_EQ(f.cubes.size(), 256);
  Budget none{1};
  EXPECT_EQ(make_form(parity, 2304, 9, none).status, Status::search_exhausted);
  EXPECT_TRUE(none.exhausted);
  Budget too_small{1000000};
  EXPECT_EQ(make_form(parity, 4, 9, too_small).status, Status::search_exhausted);
  EXPECT_FALSE(too_small.exhausted);  // greedy complexity failure, not impossibility
}

TEST(Unate, TwoLevelsShareBothRealRails) {
  Logic_network g;
  auto          a = g.add_source(), b = g.add_source(), c = g.add_source(), d = g.add_source();
  auto          shared = gate(g, a, b);
  auto          y      = gate(g, shared, c);
  auto          z      = gate(g, shared, d, 4);  // !shared & d needs a real twin
  g.outputs            = {y, z, y};
  Search_options opts;
  opts.recipes = {
      {2, 2, 8, 2}
  };
  auto result = optimize(g, opts);
  ASSERT_EQ(result.status, Status::feasible);
  const auto& n = result.attempts[0].network;
  EXPECT_EQ(n.depth, 2);
  EXPECT_EQ(n.source_inverters, 2);
  EXPECT_EQ(n.outputs[0], n.outputs[2]);
  size_t twins = 0;
  for (const auto& node : n.nodes) {
    twins += node.origin == shared;
  }
  EXPECT_EQ(twins, 2);
  for (uint32_t x = 0; x < 16; ++x) {
    const auto outputs = evaluate(n, g, x);
    const bool s       = (x & 3) == 3;
    EXPECT_EQ(outputs[0], s && (x & 4));
    EXPECT_EQ(outputs[1], !s && (x & 8));
    EXPECT_EQ(outputs[0], outputs[2]);
  }
}

TEST(Unate, OrderedRelaxationAndRetainedCandidates) {
  Logic_network   g;
  std::vector<Id> layer;
  for (uint32_t j = 0; j < 8; ++j) {
    layer.push_back(g.add_source());
  }
  while (layer.size() > 1) {
    std::vector<Id> next;
    for (uint32_t j = 0; j < layer.size(); j += 2) {
      next.push_back(gate(g, layer[j], layer[j + 1]));
    }
    layer = next;
  }
  g.outputs = layer;
  Search_options opts;
  opts.recipes = {
      {2, 2, 2, 2},
      {3, 2, 2, 2},
      {3, 4, 4, 4},
      {4, 4, 4, 4}
  };
  auto r = optimize(g, opts);
  ASSERT_EQ(r.attempts.size(), 4);
  EXPECT_EQ(r.attempts[0].status, Status::search_exhausted);
  EXPECT_EQ(r.attempts[1].status, Status::feasible);
  EXPECT_EQ(r.attempts[1].network.depth, 3);
  EXPECT_EQ(r.attempts[2].status, Status::feasible);
  EXPECT_EQ(r.attempts[2].network.depth, 2);
  EXPECT_EQ(r.attempts[3].status, Status::feasible);
}

TEST(Unate, DiverseCutsKeepWideShallowCoverUnderTwoCutLimit) {
  Logic_network   g;
  std::vector<Id> layer;
  for (uint32_t i = 0; i < 8; ++i) {
    layer.push_back(g.add_source());
  }
  while (layer.size() > 1) {
    std::vector<Id> next;
    for (size_t i = 0; i < layer.size(); i += 2) {
      next.push_back(gate(g, layer[i], layer[i + 1]));
    }
    layer = std::move(next);
  }
  g.outputs = layer;
  Search_options options;
  options.divisor_limit = 0;  // Isolate this stage when measuring its exact work budget.
  options.recipes       = {
      {2, 4, 4, 4}
  };
  options.cuts_per_node   = 2;
  options.recovery_rounds = 0;
  options.joint_limit     = 0;
  const auto result       = optimize(g, options);
  ASSERT_EQ(result.status, Status::feasible);
  ASSERT_EQ(result.attempts.size(), 1);
  const auto& network = result.attempts[0].network;
  // Size-only retention drops the four-source cuts below the root and leaves
  // a three-level tree. Depth diversity must retain a two-level AND8 cover.
  EXPECT_EQ(network.depth, 2);
  EXPECT_EQ(network.max_support, 4);
  for (uint32_t x = 0; x < 256; ++x) {
    EXPECT_EQ(evaluate(network, g, x), (std::vector<bool>{x == 255}));
  }
  Budget proof{100000};
  EXPECT_TRUE(verify(g, network, options.recipes[0], proof));
  options.work       = result.attempts[0].work - 1;
  const auto stopped = optimize(g, options);
  EXPECT_EQ(stopped.status, Status::search_exhausted);
  EXPECT_TRUE(stopped.attempts[0].network.nodes.empty());
}

TEST(Unate, SupportShrinkingRetainsSeparatingWitness) {
  Logic_network g;
  auto          a = g.add_source(), b = g.add_source();
  auto          xor_ab = gate(g, a, b, 6);
  auto          y      = gate(g, xor_ab, b, 6);
  g.outputs            = {y};
  auto result          = optimize(g);
  ASSERT_EQ(result.status, Status::feasible);
  const auto& net = result.attempts[0].network;
  EXPECT_EQ(net.max_support, 1);
  for (uint32_t x = 0; x < 4; ++x) {
    EXPECT_EQ(evaluate(net, g, x)[0], (x & 1) != 0);
  }
}

TEST(Unate, AreaFlowDiversityPreservesCheaperSharedCover) {
  Logic_network g;
  for (unsigned i = 0; i < 6; ++i) {
    g.add_source();
  }
  const std::array<std::array<uint32_t, 3>, 18> gates{
      {{5, 5, 8},
       {6, 4, 14},
       {1, 7, 14},
       {8, 4, 14},
       {9, 3, 14},
       {9, 0, 14},
       {0, 5, 14},
       {9, 8, 14},
       {12, 13, 14},
       {7, 1, 14},
       {5, 13, 8},
       {13, 1, 14},
       {12, 11, 8},
       {6, 9, 14},
       {18, 0, 14},
       {20, 3, 14},
       {14, 19, 14},
       {18, 10, 8}}
  };
  for (const auto& row : gates) {
    gate(g, row[0], row[1], row[2]);
  }
  g.outputs = {21, 22, 23};
  Search_options options;
  options.recipes = {
      {3, 3, 16, 4}
  };
  options.cuts_per_node   = 3;
  options.recovery_rounds = 0;
  options.joint_limit     = 0;
  const auto result       = optimize(g, options);
  ASSERT_EQ(result.status, Status::feasible);
  const auto& network = result.attempts[0].network;
  // Size/depth ranks alone produce nine gates in this reconvergent network.
  // The area-flow slot retains a cheaper cover before any recovery sweep.
  EXPECT_LE(
      std::count_if(network.nodes.begin(), network.nodes.end(), [](const auto& node) { return node.kind != Node_kind::source; }),
      8);
  for (uint32_t x = 0; x < 64; ++x) {
    const auto outputs = evaluate(network, g, x);
    for (size_t output = 0; output < g.outputs.size(); ++output) {
      auto one    = g;
      one.outputs = {g.outputs[output]};
      EXPECT_EQ(outputs[output], evaluate(one, x));
    }
  }
  Budget proof{100000};
  EXPECT_TRUE(verify(g, network, options.recipes[0], proof));
}

TEST(Unate, RejectCorruptWitnessAndLimits) {
  Logic_network g;
  auto          a = g.add_source(), b = g.add_source();
  g.outputs = {gate(g, a, b, 6)};
  auto r    = optimize(g);
  ASSERT_EQ(r.status, Status::feasible);
  const auto& good      = r.attempts[0].network;
  const auto  recipe    = r.attempts[0].recipe;
  auto        corrupted = good;
  corrupted.nodes.back().terms[0].pop_back();
  Budget work{100000};
  EXPECT_FALSE(verify(g, corrupted, recipe, work));
  corrupted                                   = good;
  corrupted.nodes.back().completion.words[0] ^= 1;
  EXPECT_FALSE(verify(g, corrupted, recipe, work));
  corrupted                       = good;
  corrupted.nodes.back().ports[0] = static_cast<Id>(corrupted.nodes.size());
  EXPECT_FALSE(verify(g, corrupted, recipe, work));
  corrupted = good;
  ++corrupted.depth;
  EXPECT_FALSE(verify(g, corrupted, recipe, work));
  // Exhausted search work: an unbounded-depth recipe still decomposes every
  // node through its own fan-in cut; a depth-capped one reports exhaustion.
  Search_options opts;
  opts.work              = 1;
  const auto exhausted   = optimize(g, opts);
  ASSERT_EQ(exhausted.status, Status::feasible);
  EXPECT_GT(exhausted.attempts[0].fanin_fallbacks, 0);
  opts.recipes = {
      {2, 6, 16, 4}
  };
  EXPECT_EQ(optimize(g, opts).status, Status::search_exhausted);
  opts.max_nodes = 1;
  EXPECT_EQ(optimize(g, opts).status, Status::unsupported);
  g.nodes.back().inputs[0] = 999;
  EXPECT_EQ(optimize(g).status, Status::invalid);
}

TEST(Unate, ConstantsPassThroughAndRepeatedInputs) {
  Logic_network g;
  auto          a      = g.add_source();
  auto          zero   = g.add_function({}, Truth_table(0));
  auto          one    = g.add_function({}, Truth_table(0, true));
  auto          cancel = gate(g, a, a, 6);
  g.outputs            = {a, zero, one, cancel};
  auto r               = optimize(g);
  ASSERT_EQ(r.status, Status::feasible);
  for (uint32_t x = 0; x < 2; ++x) {
    auto y = evaluate(r.attempts[0].network, g, x);
    EXPECT_EQ(y, (std::vector<bool>{x != 0, false, true, false}));
  }
}

TEST(Unate, RandomDagEquivalenceAndDeterminism) {
  std::mt19937 random(0x51a7);
  for (uint32_t trial = 0; trial < 80; ++trial) {
    Logic_network g;
    for (uint32_t j = 0; j < 5; ++j) {
      g.add_source();
    }
    for (uint32_t j = 0; j < 12; ++j) {
      auto a = static_cast<Id>(random() % g.nodes.size());
      auto b = static_cast<Id>(random() % g.nodes.size());
      gate(g, a, b, random() & 15);
    }
    g.outputs = {static_cast<Id>(g.nodes.size() - 1)};
    Search_options options;
    options.cuts_per_node = trial % 3 == 0 ? 32 : 1 + trial % 8;
    options.joint_limit   = trial % 4 == 0 ? 4 : 256;
    auto r                = optimize(g, options);
    auto repeat           = optimize(g, options);
    ASSERT_EQ(r.status, repeat.status);
    ASSERT_EQ(r.attempts.size(), repeat.attempts.size());
    for (uint32_t i = 0; i < r.attempts.size(); ++i) {
      ASSERT_EQ(r.attempts[i].status, repeat.attempts[i].status);
      ASSERT_EQ(r.attempts[i].work, repeat.attempts[i].work);
      EXPECT_EQ(r.attempts[i].cover_queries, repeat.attempts[i].cover_queries);
      EXPECT_EQ(r.attempts[i].cover_roots, repeat.attempts[i].cover_roots);
      EXPECT_EQ(r.attempts[i].cover_reason, repeat.attempts[i].cover_reason);
      if (r.attempts[i].status != Status::feasible) {
        continue;
      }
      const auto& n = r.attempts[i].network;
      for (uint32_t x = 0; x < 32; ++x) {
        ASSERT_EQ(evaluate(n, g, x)[0], evaluate(g, x)) << trial << ':' << x;
        if (r.attempts[i].recovered) {
          ASSERT_EQ(evaluate(*r.attempts[i].recovered, g, x)[0], evaluate(g, x)) << trial << ':' << x;
        }
        if (r.attempts[i].joint_recovered) {
          ASSERT_EQ(evaluate(*r.attempts[i].joint_recovered, g, x)[0], evaluate(g, x)) << trial << ':' << x;
        }
        if (r.attempts[i].divisor_recovered) {
          ASSERT_EQ(evaluate(*r.attempts[i].divisor_recovered, g, x)[0], evaluate(g, x)) << trial << ':' << x;
        }
      }
      EXPECT_EQ(r.attempts[i].recovery_checks, repeat.attempts[i].recovery_checks);
      EXPECT_EQ(r.attempts[i].recovery_improvements, repeat.attempts[i].recovery_improvements);
      EXPECT_EQ(r.attempts[i].recovered.has_value(), repeat.attempts[i].recovered.has_value());
      EXPECT_EQ(r.attempts[i].joint_combinations, repeat.attempts[i].joint_combinations);
      EXPECT_EQ(r.attempts[i].joint_checks, repeat.attempts[i].joint_checks);
      EXPECT_EQ(r.attempts[i].divisor_queries, repeat.attempts[i].divisor_queries);
      EXPECT_EQ(r.attempts[i].divisor_improvements, repeat.attempts[i].divisor_improvements);
      EXPECT_EQ(r.attempts[i].divisor_recovered.has_value(), repeat.attempts[i].divisor_recovered.has_value());
      EXPECT_EQ(r.attempts[i].joint_complete, repeat.attempts[i].joint_complete);
      EXPECT_EQ(r.attempts[i].joint_scope, repeat.attempts[i].joint_scope);
      EXPECT_EQ(r.attempts[i].joint_windows, repeat.attempts[i].joint_windows);
      EXPECT_EQ(r.attempts[i].joint_windows_complete, repeat.attempts[i].joint_windows_complete);
      ASSERT_EQ(r.attempts[i].joint_recovered.has_value(), repeat.attempts[i].joint_recovered.has_value());
      if (r.attempts[i].joint_recovered) {
        const auto& a = *r.attempts[i].joint_recovered;
        const auto& b = *repeat.attempts[i].joint_recovered;
        ASSERT_EQ(a.nodes.size(), b.nodes.size());
        EXPECT_EQ(a.outputs, b.outputs);
        for (size_t j = 0; j < a.nodes.size(); ++j) {
          EXPECT_EQ(a.nodes[j].origin, b.nodes[j].origin);
          EXPECT_EQ(a.nodes[j].negative, b.nodes[j].negative);
          EXPECT_EQ(a.nodes[j].ports, b.nodes[j].ports);
          EXPECT_EQ(a.nodes[j].terms, b.nodes[j].terms);
          EXPECT_EQ(a.nodes[j].completion, b.nodes[j].completion);
        }
        Budget proof{100000};
        EXPECT_TRUE(verify(g, a, options.recipes[i], proof));
      }
    }
  }
}

TEST(Unate, RecoveryReusesPaidProducerAndKeepsSeedForPhysicalComparison) {
  Logic_network g;
  const auto    a = g.add_source(), b = g.add_source(), c = g.add_source(), d = g.add_source();
  const auto    shared = gate(g, a, b);
  const auto    y = gate(g, shared, c), z = gate(g, shared, d);
  g.outputs = {shared, y, z};
  Search_options options;
  options.divisor_limit = 0;  // Isolate this stage when measuring its exact work budget.
  options.recipes       = {
      {2, 3, 16, 4}
  };
  options.joint_limit = 0;
  auto result         = optimize(g, options);
  ASSERT_EQ(result.status, Status::feasible);
  const auto& attempt = result.attempts[0];
  ASSERT_TRUE(attempt.recovered);
  EXPECT_EQ(attempt.network.depth, 1);
  EXPECT_EQ(attempt.recovered->depth, 2);
  EXPECT_GE(attempt.recovery_improvements, 2);
  for (uint32_t x = 0; x < 16; ++x) {
    const std::vector<bool> expected{(x & 3) == 3, (x & 7) == 7, (x & 11) == 11};
    EXPECT_EQ(evaluate(attempt.network, g, x), expected);
    EXPECT_EQ(evaluate(*attempt.recovered, g, x), expected);
  }
  EXPECT_EQ(std::count_if(attempt.recovered->nodes.begin(),
                          attempt.recovered->nodes.end(),
                          [&](const auto& n) { return n.origin == shared; }),
            1);
  options.recovery_rounds = 0;
  options.joint_limit     = 0;
  auto seed               = optimize(g, options);
  EXPECT_FALSE(seed.attempts[0].recovered);
  EXPECT_EQ(seed.attempts[0].network.depth, 1);
  // Enough work for the proved seed, but no recovery. It must stay feasible.
  options.recovery_rounds = 2;
  options.work            = seed.attempts[0].work;
  auto stopped            = optimize(g, options);
  ASSERT_EQ(stopped.status, Status::feasible);
  EXPECT_TRUE(stopped.attempts[0].recovery_exhausted);
  EXPECT_FALSE(stopped.attempts[0].recovered);
  Budget proof{100000};
  EXPECT_TRUE(verify(g, stopped.attempts[0].network, options.recipes[0], proof));
  options.work   = attempt.work - 1;
  auto late_stop = optimize(g, options);
  ASSERT_EQ(late_stop.status, Status::feasible);
  ASSERT_TRUE(late_stop.attempts[0].recovered);
  EXPECT_TRUE(late_stop.attempts[0].recovery_exhausted);
  Budget recovered_proof{100000};
  EXPECT_TRUE(verify(g, *late_stop.attempts[0].recovered, options.recipes[0], recovered_proof));
}

TEST(Unate, ExactClosureRecountReducesSharedGateCount) {
  Logic_network g;
  for (unsigned i = 0; i < 6; ++i) {
    g.add_source();
  }
  const std::array<std::array<uint32_t, 3>, 14> gates{
      {{3, 5, 14},
       {3, 2, 8},
       {4, 6, 14},
       {5, 4, 8},
       {4, 4, 14},
       {0, 8, 14},
       {0, 8, 14},
       {5, 12, 8},
       {11, 9, 14},
       {14, 14, 8},
       {10, 2, 14},
       {6, 6, 8},
       {11, 9, 14},
       {11, 17, 8}}
  };
  for (const auto& row : gates) {
    gate(g, row[0], row[1], row[2]);
  }
  g.outputs = {17, 18, 19};
  Search_options options;
  options.image_inputs = 0;  // Isolate structural recovery from image-based simplification.
  options.recipes      = {
      {3, 3, 16, 4}
  };
  options.joint_limit = 0;
  const auto result   = optimize(g, options);
  ASSERT_EQ(result.status, Status::feasible);
  const auto& attempt = result.attempts[0];
  // The count-first seed already has the fewest functions here; the exact
  // closure recount still strictly improves the lexicographic objective (its
  // literal/port tiebreak) without adding a function.
  ASSERT_TRUE(attempt.recovered);
  EXPECT_GT(attempt.recovery_improvements, 0);
  const auto count = [](const Unate_network& net) {
    return std::count_if(net.nodes.begin(), net.nodes.end(), [](const auto& node) { return node.kind == Node_kind::function; });
  };
  EXPECT_LE(count(*attempt.recovered), count(attempt.network));
  for (uint32_t x = 0; x < 64; ++x) {
    for (size_t output = 0; output < g.outputs.size(); ++output) {
      auto one    = g;
      one.outputs = {g.outputs[output]};
      EXPECT_EQ(evaluate(*attempt.recovered, g, x)[output], evaluate(one, x));
    }
  }
}

TEST(Unate, JointSelectionEnumeratesSmallClosureAndKeepsSeed) {
  Logic_network g;
  const auto    a = g.add_source(), b = g.add_source(), c = g.add_source(), d = g.add_source();
  const auto    shared = gate(g, a, b);
  g.outputs            = {shared, gate(g, shared, c), gate(g, shared, d)};
  Search_options options;
  options.recipes = {
      {2, 3, 16, 4}
  };
  options.recovery_rounds = 0;
  options.joint_limit     = 4;
  const auto result       = optimize(g, options);
  ASSERT_EQ(result.status, Status::feasible);
  const auto& attempt = result.attempts[0];
  ASSERT_TRUE(attempt.joint_recovered);
  EXPECT_FALSE(attempt.recovered);
  EXPECT_TRUE(attempt.joint_complete);
  EXPECT_FALSE(attempt.joint_exhausted);
  // Each consumer can either flatten a,b or use their shared producer. All
  // four combinations are examined, including the choice with both sharing.
  EXPECT_EQ(attempt.joint_combinations, 4);
  EXPECT_EQ(attempt.joint_checks, 4);
  EXPECT_EQ(attempt.joint_reason, "enumerated_candidate_optimum");
  EXPECT_EQ(attempt.network.depth, 1);
  EXPECT_EQ(attempt.joint_recovered->depth, 2);
  uint64_t literals = 0;
  for (const auto& node : attempt.joint_recovered->nodes) {
    for (const auto& term : node.terms) {
      literals += term.size();
    }
  }
  EXPECT_EQ(literals, 6);
  for (uint32_t x = 0; x < 16; ++x) {
    const std::vector<bool> expected{(x & 3) == 3, (x & 7) == 7, (x & 11) == 11};
    EXPECT_EQ(evaluate(attempt.network, g, x), expected);
    EXPECT_EQ(evaluate(*attempt.joint_recovered, g, x), expected);
  }
}

Logic_network joint_fixture(uint32_t copies = 1) {
  Logic_network                                 g;
  const std::array<std::array<uint32_t, 3>, 10> gates{
      {{1, 4, 8}, {1, 0, 14}, {6, 4, 14}, {6, 3, 8}, {1, 0, 14}, {3, 9, 8}, {8, 7, 8}, {11, 5, 8}, {5, 1, 8}, {5, 10, 14}}
  };
  for (uint32_t copy = 0; copy < copies; ++copy) {
    const auto offset = static_cast<Id>(g.nodes.size());
    for (unsigned i = 0; i < 5; ++i) {
      g.add_source();
    }
    for (const auto& row : gates) {
      gate(g, offset + row[0], offset + row[1], row[2]);
    }
    for (auto output : {12U, 13U, 14U}) {
      g.outputs.push_back(offset + output);
    }
  }
  return g;
}

// Count-first cut selection lands on the closure optimum this fixture was built
// around (the depth-first selection used to stop at 7 and needed joint recovery
// to reach 6). Joint recovery still enumerates the complete retained closure,
// confirms nothing beats the seed, and honours its limit and the work budget.
TEST(Unate, JointRecoveryConfirmsCountFirstSeedAndStopsOnExhaustion) {
  const auto     g = joint_fixture();
  Search_options options;
  options.image_inputs = 0;  // Isolate structural recovery from image-based simplification.
  options.recipes      = {
      {3, 3, 16, 4}
  };
  options.cuts_per_node   = 8;
  options.recovery_rounds = 8;
  options.joint_limit     = 4096;
  const auto result       = optimize(g, options);
  ASSERT_EQ(result.status, Status::feasible);
  const auto& attempt   = result.attempts[0];
  const auto  functions = [](const Unate_network& net) {
    return std::count_if(net.nodes.begin(), net.nodes.end(), [](const auto& node) { return node.kind == Node_kind::function; });
  };
  EXPECT_EQ(functions(attempt.recovered ? *attempt.recovered : attempt.network), 6);
  EXPECT_FALSE(attempt.joint_recovered);
  EXPECT_TRUE(attempt.joint_complete);
  EXPECT_EQ(attempt.joint_reason, "enumerated_candidate_optimum");
  EXPECT_EQ(attempt.joint_combinations, 576);
  EXPECT_EQ(attempt.joint_checks, 576);
  Budget proof{100000};
  EXPECT_TRUE(verify(g, attempt.network, options.recipes[0], proof));
  for (uint32_t x = 0; x < 32; ++x) {
    const auto outputs = evaluate(attempt.network, g, x);
    for (size_t output = 0; output < g.outputs.size(); ++output) {
      auto one    = g;
      one.outputs = {g.outputs[output]};
      EXPECT_EQ(outputs[output], evaluate(one, x));
    }
  }
  options.joint_limit   = 575;
  options.joint_windows = 0;
  const auto capped     = optimize(g, options);
  ASSERT_EQ(capped.status, Status::feasible);
  EXPECT_EQ(capped.attempts[0].joint_reason, "candidate_limit");
  EXPECT_FALSE(capped.attempts[0].joint_complete);
  EXPECT_FALSE(capped.attempts[0].joint_recovered);
  EXPECT_EQ(capped.attempts[0].joint_checks, 0);
  options.joint_limit = 0;
  const auto seed     = optimize(g, options);
  EXPECT_EQ(seed.attempts[0].joint_reason, "disabled");
  options.joint_limit = 4096;
  options.work        = seed.attempts[0].work;
  const auto stopped  = optimize(g, options);
  ASSERT_EQ(stopped.status, Status::feasible);
  EXPECT_TRUE(stopped.attempts[0].joint_exhausted);
  EXPECT_FALSE(stopped.attempts[0].joint_recovered);
  EXPECT_FALSE(stopped.attempts[0].joint_complete);
  options.work    = attempt.work - 1;
  const auto late = optimize(g, options);
  ASSERT_EQ(late.status, Status::feasible);
  EXPECT_TRUE(late.attempts[0].joint_exhausted);
  EXPECT_FALSE(late.attempts[0].joint_complete);
  EXPECT_EQ(functions(late.attempts[0].network), 6);
}

// Three copies of the joint fixture exceed joint_limit, so recovery falls back to
// bounded windows. The count-first seed already reaches 18 (3 x the per-copy
// optimum); the window sweep checks its windows and finds nothing better, and
// never claims a global optimum.
TEST(Unate, JointWindowsSweepLargerRegionsWithoutClaimingGlobalOptimality) {
  const auto     g = joint_fixture(3);
  Search_options options;
  options.image_inputs = 0;  // Isolate structural recovery from image-based simplification.
  options.recipes      = {
      {3, 3, 16, 4}
  };
  options.cuts_per_node   = 8;
  options.recovery_rounds = 8;
  options.joint_limit     = 256;
  options.joint_windows   = 0;
  const auto seed         = optimize(g, options);
  ASSERT_EQ(seed.status, Status::feasible);
  EXPECT_EQ(seed.attempts[0].joint_reason, "candidate_limit");
  EXPECT_FALSE(seed.attempts[0].joint_recovered);
  const auto functions = [](const Unate_network& net) {
    return std::count_if(net.nodes.begin(), net.nodes.end(), [](const auto& n) { return n.kind == Node_kind::function; });
  };
  EXPECT_EQ(functions(seed.attempts[0].recovered ? *seed.attempts[0].recovered : seed.attempts[0].network), 18);
  options.joint_windows = 32;
  const auto result     = optimize(g, options);
  ASSERT_EQ(result.status, Status::feasible);
  const auto& attempt = result.attempts[0];
  EXPECT_FALSE(attempt.joint_recovered);
  EXPECT_EQ(attempt.joint_reason, "window_sweep_completed");
  EXPECT_EQ(attempt.joint_scope, "bounded_windows");
  EXPECT_FALSE(attempt.joint_complete);
  EXPECT_GT(attempt.joint_windows, 0);
  EXPECT_LE(attempt.joint_windows, options.joint_windows);
  EXPECT_EQ(attempt.joint_windows_complete, attempt.joint_windows);
  EXPECT_LE(attempt.joint_combinations, uint64_t(options.joint_windows) * options.joint_limit);
  EXPECT_EQ(attempt.joint_checks, attempt.joint_combinations);
  EXPECT_FALSE(attempt.joint_exhausted);
  Budget proof{1000000};
  EXPECT_TRUE(verify(g, attempt.network, options.recipes[0], proof));
  // Vary each component exhaustively while the other two use independent
  // assignments. The production verifier also checks every separating cut.
  for (uint32_t i = 0; i < 96; ++i) {
    const uint32_t shift   = 5 * (i / 32);
    const uint32_t x       = ((i % 32) << shift) | ((i * 7919) & 32767 & ~(31U << shift));
    const auto     outputs = evaluate(attempt.network, g, x);
    for (size_t output = 0; output < g.outputs.size(); ++output) {
      auto one    = g;
      one.outputs = {g.outputs[output]};
      EXPECT_EQ(outputs[output], evaluate(one, x));
    }
  }
  options.work    = attempt.work - 1;
  const auto late = optimize(g, options);
  ASSERT_EQ(late.status, Status::feasible);
  EXPECT_TRUE(late.attempts[0].joint_exhausted);
  EXPECT_FALSE(late.attempts[0].joint_complete);
  options.work          = 5000000;
  options.joint_windows = 1;
  const auto bounded    = optimize(g, options);
  EXPECT_EQ(bounded.attempts[0].joint_windows, 1);
  EXPECT_EQ(bounded.attempts[0].joint_windows_complete, 1);
  EXPECT_LE(bounded.attempts[0].joint_checks, options.joint_limit);
  EXPECT_EQ(bounded.attempts[0].joint_reason, "window_limit");
  EXPECT_FALSE(bounded.attempts[0].joint_complete);
  options.joint_windows = 257;
  EXPECT_EQ(optimize(g, options).status, Status::unsupported);
}

TEST(Unate, AssociativeReshapingSharesTwinGroupsBeyondEnumerationWidth) {
  Logic_network g;
  for (unsigned i = 0; i < 16; ++i) {
    g.add_source();
  }
  Id root = 0;
  for (Id i = 1; i < 16; ++i) {
    root = gate(g, root, i);
  }
  const auto inverse = g.add_function({root}, table(1, 1));
  g.outputs          = {root, inverse, root, 0};
  Search_options options;
  options.recipes = {
      {2, 4, 16, 4}
  };
  options.cuts_per_node = 1;
  options.cover_limit = options.divisor_limit = options.joint_limit = options.recovery_rounds = options.encoding_limit = 0;
  options.reshape_limit                                                                                                = 0;
  EXPECT_EQ(optimize(g, options).status, Status::search_exhausted);
  options.reshape_limit = 32;
  const auto result     = optimize(g, options);
  ASSERT_EQ(result.status, Status::feasible) << result.attempts[0].reshape_reason;
  const auto& attempt = result.attempts[0];
  EXPECT_EQ(attempt.reshape_reason, "verified_associative_reshaping");
  EXPECT_EQ(attempt.reshape_roots, 2);
  EXPECT_EQ(attempt.reshape_groups, 4);
  const auto& net = attempt.network;
  ASSERT_EQ(net.encodings.size(), 4);
  EXPECT_EQ(net.depth, 2);
  EXPECT_EQ(net.source_inverters, 16);
  EXPECT_EQ(std::count_if(net.nodes.begin(), net.nodes.end(), [](const auto& n) { return n.kind == Node_kind::function; }), 10);
  EXPECT_EQ(net.outputs[0], net.outputs[2]);
  for (uint32_t x = 0; x < 65536; ++x) {
    const bool value = x == 65535;
    ASSERT_EQ(evaluate(net, g, x), (std::vector<bool>{value, !value, value, (x & 1) != 0}));
  }
  auto corrupted = net;
  corrupted.encodings[0].table.set(0, true);
  Budget verify_budget{5000000};
  EXPECT_FALSE(verify(g, corrupted, options.recipes[0], verify_budget));
  options.work         = attempt.work - 1;
  const auto exhausted = optimize(g, options);
  EXPECT_EQ(exhausted.status, Status::search_exhausted);
  EXPECT_TRUE(exhausted.attempts[0].network.nodes.empty());
  EXPECT_EQ(exhausted.attempts[0].reshape_reason, "work_or_resource_exhausted");
  options.work           = 5000000;
  options.symbolic_nodes = 0;
  EXPECT_EQ(optimize(g, options).attempts[0].reshape_reason, "dependency_query_inconclusive");
  options.symbolic_nodes = 4096;
  options.reshape_limit  = 1;
  EXPECT_EQ(optimize(g, options).attempts[0].reshape_reason, "output_admission_limit");
  options.reshape_limit = 4097;
  EXPECT_EQ(optimize(g, options).status, Status::unsupported);
}

TEST(Unate, AssociativeReshapingPreservesSignedOrConesAndConstants) {
  for (bool disjunction : {false, true}) {
    Logic_network g;
    for (unsigned i = 0; i < 7; ++i) {
      g.add_source();
    }
    Id root = 0;
    for (Id i = 1; i < 7; ++i) {
      const auto input = (i & 1) ? g.add_function({i}, table(1, 1)) : i;
      root             = gate(g, root, input, disjunction ? 14 : 8);
    }
    const auto constant = gate(g, 0, g.add_function({0}, table(1, 1)));
    g.outputs           = {root, constant};
    Search_options options;
    options.recipes = {
        {2, 3, 16, 3}
    };
    options.cuts_per_node = 1;
    options.cover_limit = options.divisor_limit = options.joint_limit = options.recovery_rounds = options.encoding_limit = 0;
    const auto result = optimize(g, options);
    ASSERT_EQ(result.status, Status::feasible);
    EXPECT_EQ(result.attempts[0].reshape_reason, "verified_associative_reshaping");
    EXPECT_EQ(result.attempts[0].reshape_groups, 2);
    for (uint32_t x = 0; x < 128; ++x) {
      EXPECT_EQ(evaluate(result.attempts[0].network, g, x), (std::vector<bool>{evaluate(g, x), false}));
    }
  }
}

TEST(Unate, HierarchicalReshapingRelaxesDepthAndProvesEveryRail) {
  for (const auto [count, support, depth] : {
           std::array<unsigned, 3>{27, 3, 3},
           {28, 3, 4},
           {16, 2, 4}
  }) {
    SCOPED_TRACE(count);
    Logic_network g;
    for (unsigned i = 0; i < count; ++i) {
      g.add_source();
    }
    Id root = 0;
    for (Id i = 1; i < count; ++i) {
      const auto input = (i & 1) ? g.add_function({i}, table(1, 1)) : i;
      root             = gate(g, root, input);
    }
    const auto inverse = g.add_function({root}, table(1, 1));
    g.outputs          = {root, inverse, root, 0};
    Search_options options;
    options.recipes = {
        {2, support, 16, support},
        {3, support, 16, support},
        {4, support, 16, support}
    };
    options.cuts_per_node = 1;
    options.cover_limit = options.divisor_limit = options.joint_limit = options.recovery_rounds = options.encoding_limit = 0;
    const auto result = optimize(g, options);
    ASSERT_EQ(result.status, Status::feasible);
    ASSERT_EQ(result.attempts.size(), 3);
    for (unsigned i = 0; i < depth - 2; ++i) {
      EXPECT_EQ(result.attempts[i].status, Status::search_exhausted);
      EXPECT_TRUE(result.attempts[i].network.nodes.empty());
    }
    const auto& attempt = result.attempts[depth - 2];
    ASSERT_EQ(attempt.status, Status::feasible) << attempt.reshape_reason;
    const auto& net = attempt.network;
    EXPECT_EQ(net.depth, depth);
    EXPECT_EQ(net.source_inverters, count);
    EXPECT_EQ(net.outputs[0], net.outputs[2]);
    EXPECT_TRUE(std::any_of(net.encodings.begin(), net.encodings.end(), [&](const auto& n) {
      return std::any_of(n.inputs.begin(), n.inputs.end(), [&](Id id) { return id >= g.nodes.size(); });
    }));
    if (depth == 4) {
      EXPECT_GT(net.encodings.size(), 12);
    }
    std::mt19937_64       random(29);
    const auto            all        = (uint64_t{1} << count) - 1;
    const auto            satisfying = all & 0x5555555555555555ULL;
    std::vector<uint64_t> assignments{0, all, satisfying};
    for (unsigned i = 0; i < count; ++i) {
      assignments.push_back(satisfying ^ (uint64_t{1} << i));
    }
    for (unsigned i = 0; i < 256; ++i) {
      assignments.push_back(random() & all);
    }
    for (auto x : assignments) {
      const bool expected = x == satisfying;
      ASSERT_EQ(evaluate(net, g, x), (std::vector<bool>{expected, !expected, expected, (x & 1) != 0}));
    }
    for (unsigned mutation = 0; mutation < 4; ++mutation) {
      auto bad = net;
      if (mutation == 0) {
        bad.encodings.back().inputs[0] = g.nodes.size() + bad.encodings.size() - 1;
      }
      if (mutation == 1) {
        bad.encodings.back().inputs[0] = root;
      }
      if (mutation == 2) {
        bad.encodings.back().table.set(0, true);
      }
      if (mutation == 3) {
        bad.encodings.resize(max_encoder_nodes + 1);
      }
      Budget budget{5000000};
      EXPECT_FALSE(verify(g, bad, attempt.recipe, budget));
    }
    options.recipes      = {attempt.recipe};
    options.work         = attempt.work - 1;
    const auto exhausted = optimize(g, options);
    EXPECT_EQ(exhausted.status, Status::search_exhausted);
    EXPECT_TRUE(exhausted.attempts[0].network.nodes.empty());
    EXPECT_EQ(exhausted.attempts[0].reshape_reason, "work_or_resource_exhausted");
  }
}
// Discovery-order codes 00,01,10 require an expensive "not all equal"
// encoder. The next injective code assignment 00,01,11 uses OR/AND rails.
TEST(Unate, AlternativeCofactorClassCodesRecoverBoundedTwoLevelNetwork) {
  Logic_network source;
  for (unsigned i = 0; i < 4; ++i) {
    source.add_source();
  }
  Truth_table any(4), all(4);
  for (uint32_t x = 0; x < 16; ++x) {
    any.set(x, (x & 7) != 0 && (x & 8) != 0);
    all.set(x, (x & 7) == 7 && (x & 8) != 0);
  }
  source.outputs = {source.add_function({0, 1, 2, 3}, any), source.add_function({0, 1, 2, 3}, all)};
  Search_options options;
  options.recipes = {
      {2, 3, 3, 3}
  };
  options.cover_limit = options.divisor_limit = options.joint_limit = options.recovery_rounds = options.reshape_limit = 0;
  options.encoding_pair_limit                                                                                         = 0;
  options.encoding_limit                                                                                              = 1;
  options.encoding_code_limit                                                                                         = 1;
  auto canonical = optimize(source, options);
  ASSERT_EQ(canonical.status, Status::search_exhausted);
  EXPECT_EQ(canonical.attempts[0].encoding_code_queries, 1);
  EXPECT_TRUE(canonical.attempts[0].encoding_code_limited);
  options.encoding_code_limit = 2;
  auto result                 = optimize(source, options);
  ASSERT_EQ(result.status, Status::feasible) << result.attempts[0].reason;
  const auto& attempt = result.attempts[0];
  EXPECT_EQ(attempt.encoding_code_queries, 2);
  EXPECT_EQ(attempt.encoding_classes, 3);
  EXPECT_EQ(attempt.encoding_bits, 2);
  EXPECT_EQ(attempt.network.depth, 2);
  ASSERT_EQ(attempt.network.encodings.size(), 2);
  EXPECT_EQ(attempt.network.encodings[0].table, table(3, 0xfe));
  EXPECT_EQ(attempt.network.encodings[1].table, table(3, 0x80));
  Budget proof{1000000};
  EXPECT_TRUE(verify(source, attempt.network, options.recipes[0], proof));
  for (uint32_t x = 0; x < 16; ++x) {
    EXPECT_EQ(evaluate(attempt.network, source, x), (std::vector<bool>{any.get(x), all.get(x)}));
  }
  options.work = attempt.work - 1;
  auto stopped = optimize(source, options);
  EXPECT_EQ(stopped.status, Status::search_exhausted);
  EXPECT_TRUE(stopped.attempts[0].network.nodes.empty());
  EXPECT_EQ(stopped.attempts[0].encoding_reason, "work_or_resource_exhausted");
  options.work                = 5000000;
  // All 4P3=24 injective assignments fail the tighter encoder bound.
  // Raising the cap past 24 must not repeat unused-code permutations.
  options.recipes[0].literals = 2;
  options.encoding_code_limit = 25;
  auto complete               = optimize(source, options);
  EXPECT_EQ(complete.status, Status::search_exhausted);
  EXPECT_EQ(complete.attempts[0].encoding_code_queries, 24);
  EXPECT_FALSE(complete.attempts[0].encoding_code_limited);
  for (auto limit : {0u, 4097u}) {
    options.encoding_code_limit = limit;
    EXPECT_EQ(optimize(source, options).status, Status::unsupported);
  }
}

TEST(Unate, AlternativeCodesCarryAcrossTwoBoundSets) {
  Logic_network   source;
  std::vector<Id> inputs;
  for (unsigned i = 0; i < 8; ++i) {
    inputs.push_back(source.add_source());
  }
  Truth_table any(8), all(8);
  for (uint32_t x = 0; x < 256; ++x) {
    any.set(x, x != 0);
    all.set(x, x == 255);
  }
  source.outputs = {source.add_function(inputs, any), source.add_function(inputs, all)};
  Search_options options;
  options.recipes = {
      {2, 4, 4, 4}
  };
  options.cover_limit = options.divisor_limit = options.joint_limit = options.recovery_rounds = options.reshape_limit = 0;
  options.encoding_limit = options.encoding_pair_limit = 1;
  options.encoding_code_limit                          = 25;
  auto capped                                          = optimize(source, options);
  ASSERT_EQ(capped.status, Status::search_exhausted);
  EXPECT_EQ(capped.attempts[0].encoding_code_queries, 25);
  EXPECT_TRUE(capped.attempts[0].encoding_code_limited);
  options.encoding_code_limit = 26;
  auto result                 = optimize(source, options);
  ASSERT_EQ(result.status, Status::feasible) << result.attempts[0].reason;
  const auto& attempt = result.attempts[0];
  EXPECT_EQ(attempt.encoding_bound_sets, 2);
  EXPECT_EQ(attempt.encoding_code_queries, 26);
  EXPECT_EQ(attempt.encoding_classes, 9);
  EXPECT_EQ(attempt.encoding_bits, 4);
  EXPECT_EQ(attempt.network.depth, 2);
  Budget proof{1000000};
  EXPECT_TRUE(verify(source, attempt.network, options.recipes[0], proof));
  for (uint32_t x = 0; x < 256; ++x) {
    EXPECT_EQ(evaluate(attempt.network, source, x), (std::vector<bool>{x != 0, x == 255}));
  }
}

}  // namespace
}  // namespace livehd::synth

namespace livehd::synth {
namespace {
// A balanced AND tree over sources [lo, hi).
Id and_tree(Logic_network& g, const std::vector<Id>& s, size_t lo, size_t hi) {
  if (hi - lo == 1) {
    return s[lo];
  }
  const auto mid = lo + (hi - lo) / 2;
  return gate(g, and_tree(g, s, lo, mid), and_tree(g, s, mid, hi));
}
}  // namespace

// pass.synth.split: each output cone is classified by the fewest gates
// (6 inputs, 16 literals, series 4; either polarity of every leaf) that build
// it. A cone of at most 3 keeps all its gates; a larger one keeps its root
// gate and one more over a remainder.
TEST(UnateSplit, ClassifiesConesAndKeepsTopGatesOverARemainder) {
  Logic_network   g;
  std::vector<Id> s;
  for (int i = 0; i < 40; ++i) {
    s.push_back(g.add_source());
  }
  const auto one   = gate(g, s[1], s[2]);        // 1 gate
  const auto three = and_tree(g, s, 0, 12);       // two 6-input ANDs and their AND
  const auto wide  = and_tree(g, s, 0, 40);       // more than 3
  const auto tied  = g.add_function({}, table(0, 1));
  g.outputs        = {s[0], one, three, wide, tied};
  Search_options options;
  const auto     result = split_cones(g, Recipe{}, options);
  ASSERT_EQ(result.status, Status::feasible) << result.reason;
  EXPECT_EQ(result.cone_gates, (std::vector<uint8_t>{0, 1, 3, 4, 0}));
  EXPECT_EQ(result.cones_wire, 2u);
  EXPECT_EQ(result.cones_2, 1u);
  EXPECT_EQ(result.cones_3, 1u);
  EXPECT_EQ(result.cones_more, 1u);
  std::set<Id> roots;
  for (const auto& gate : result.gates) {
    roots.insert(gate.root);
    EXPECT_LE(gate.leaves.size(), 6u);
    EXPECT_LE(gate.form.literals, 16u);
    EXPECT_LE(gate.form.series, 4u);
  }
  EXPECT_TRUE(roots.contains(one));
  EXPECT_TRUE(roots.contains(three));
  EXPECT_TRUE(roots.contains(wide));
  EXPECT_TRUE(roots.contains(tied));
  // The 40-input cone keeps exactly two gates of its own (root + one under it);
  // everything else it needs is remainder logic read by those gates.
  ASSERT_FALSE(result.remainder_outputs.empty());
  EXPECT_GT(result.remainder_nodes, 0u);
  for (auto r : result.remainder_outputs) {
    EXPECT_FALSE(roots.contains(r));
    const bool read = std::any_of(result.gates.begin(), result.gates.end(), [&](const Split_gate& gate) {
      return std::find(gate.leaves.begin(), gate.leaves.end(), r) != gate.leaves.end();
    });
    EXPECT_TRUE(read) << r;
  }
  EXPECT_EQ(result.gates.size(), 1u + 3u + 2u + 1u);
}
}  // namespace livehd::synth

namespace livehd::synth {
namespace {
// OR of three separately built product chains: (a&b|a&c|b&c) & tail, with no
// node for the majority or for the tail.
Id majority_times(Logic_network& g, Id a, Id b, Id c, const std::vector<Id>& tail) {
  const auto product = [&](Id x, Id y) {
    auto acc = gate(g, x, y);
    for (auto t : tail) {
      acc = gate(g, acc, t);
    }
    return acc;
  };
  const auto p1 = product(a, b), p2 = product(a, c), p3 = product(b, c);
  return gate(g, gate(g, p1, p2, 14), p3, 14);  // 14: OR
}
}  // namespace

// Clustering by variables: no node computes maj(a,b,c), so every gate-sized
// structural cut leaves the cone at four gates, but the bound set {a,b,c}
// splits it as G(H(a,b,c), d..g) -- two gates. A second cone with the same
// shared part reuses H.
TEST(UnateSplit, BoundSetSplitFindsAndSharesTheSharedPart) {
  Logic_network   g;
  std::vector<Id> s;
  for (int i = 0; i < 8; ++i) {
    s.push_back(g.add_source());
  }
  const auto f1 = majority_times(g, s[0], s[1], s[2], {s[3], s[4], s[5], s[6]});
  const auto f2 = majority_times(g, s[0], s[1], s[2], {s[3], s[4], s[5], s[7]});
  g.outputs     = {f1, f2};
  Search_options off;
  off.split_cut = 6;
  const auto structural = split_cones(g, Recipe{}, off);
  ASSERT_EQ(structural.status, Status::feasible);
  EXPECT_GE(structural.cone_gates[0], 3u);
  Search_options options;  // split_cut = 10, sharing on
  const auto     result = split_cones(g, Recipe{}, options);
  ASSERT_EQ(result.status, Status::feasible) << result.reason;
  EXPECT_EQ(result.cone_gates[0], 2u);
  EXPECT_EQ(result.bound_gates, 1u);         // one H ...
  EXPECT_EQ(result.shared_bound_gates, 1u);  // ... used by both cones
  EXPECT_EQ(result.synthetic, 1u);
  EXPECT_TRUE(result.remainder_outputs.empty());
}

TEST(UnateSplit, ExactFormIsMinimalAndFactoringAdmitsProductsOfSums) {
  // (a+b)(c+d)(e+f): 8 cubes of 3 literals (24 > 16) as an SOP, 6 factored.
  Truth_table t(6);
  for (uint32_t x = 0; x < 64; ++x) {
    t.set(x, ((x & 3) != 0) && ((x & 12) != 0) && ((x & 48) != 0));
  }
  Budget     b{100000000};
  const auto sop = exact_form(t, 16, 4, false, b);
  const auto fac = exact_form(t, 16, 4, true, b);
  EXPECT_EQ(sop.status, Status::search_exhausted);
  ASSERT_EQ(fac.status, Status::feasible);
  EXPECT_EQ(fac.literals, 24u);
  EXPECT_EQ(fac.cubes.size(), 8u);
  EXPECT_EQ(fac.factored, 6u);
  EXPECT_EQ(fac.series, 3u);
  // Majority: exactly 3 cubes of 2 literals.
  Truth_table m(3);
  for (uint32_t x = 0; x < 8; ++x) {
    m.set(x, std::popcount(x) >= 2);
  }
  const auto maj = exact_form(m, 16, 4, false, b);
  ASSERT_EQ(maj.status, Status::feasible);
  EXPECT_EQ(maj.literals, 6u);
  EXPECT_EQ(maj.cubes.size(), 3u);
}
}  // namespace livehd::synth
