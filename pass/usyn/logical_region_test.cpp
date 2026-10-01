// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "logical_region.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>

#include "endpoint_netlist.hpp"
#include "gtest/gtest.h"

namespace livehd::usyn {
namespace {
Xsignal input(Xag_region& r, std::string name) {
  auto s = r.graph.input(std::move(name));
  r.inputs.push_back(s);
  return s;
}

uint32_t state(Xag_region& r, std::string name, Xsignal d, char init = 'x') {
  auto q = r.graph.input(name);
  r.state.push_back({std::move(name), init, q, d});
  return static_cast<uint32_t>(r.state.size() - 1);
}

std::vector<bool> evaluate(const Xag_region& r, uint32_t assignment) {
  std::vector<bool> values(r.graph.size());
  const auto        read = [&](Xsignal s) { return values[s.id] != s.inverted; };
  for (Id i = 1; i < r.graph.size(); ++i) {
    const auto& n = r.graph.node(i);
    switch (n.kind) {
      case Xag::Kind::constant: break;
      case Xag::Kind::source  : values[i] = (assignment >> n.source_index) & 1; break;
      case Xag::Kind::and_gate: values[i] = read(n.inputs[0]) && read(n.inputs[1]); break;
      case Xag::Kind::xor_gate: values[i] = read(n.inputs[0]) != read(n.inputs[1]); break;
    }
  }
  std::vector<bool> out;
  for (const auto& po : r.outputs) {
    out.push_back(read(po.signal));
  }
  for (const auto& s : r.state) {
    out.push_back(read(s.d));
  }
  return out;
}

Xag_region pair_choice_fixture(uint32_t trial) {
  uint32_t   random = 0x813faU + trial;
  const auto next   = [&] {
    random = random * 1664525U + 1013904223U;
    return random;
  };
  Xag_region           r;
  std::vector<Xsignal> signals;
  for (unsigned i = 0; i < 5; ++i) {
    signals.push_back(input(r, std::to_string(i)));
  }
  for (unsigned i = 0; i < 20; ++i) {
    auto a = signals[next() % signals.size()], b = signals[next() % signals.size()];
    if (next() & 0x100) {
      a = ~a;
    }
    const auto op = next() % 3;
    signals.push_back(op == 0 ? r.graph.land(a, b) : op == 1 ? r.graph.lxor(a, b) : r.graph.lor(a, b));
  }
  const auto shared = r.graph.lxor(signals[10], signals[15]);
  const auto s0     = state(r, "left", r.graph.land(shared, signals[20]));
  const auto s1     = state(r, "right", r.graph.lor(shared, signals[24]));
  r.outputs         = {
      { "left", r.state[s0].q},
      {"right", r.state[s1].q}
  };
  if (trial % 3 == 0) {
    r.outputs.push_back({"protected", signals[12]});
  }
  r.status = Status::feasible;
  return r;
}

void check(const Xag_region& original, const Logical_result& result, size_t endpoints) {
  ASSERT_EQ(result.status, Status::feasible) << result.reason;
  ASSERT_TRUE(result.region);
  const auto& r = *result.region;
  EXPECT_EQ(r.endpoints.size(), endpoints);
  ASSERT_EQ(r.logic.state.size(), original.state.size());
  ASSERT_EQ(r.logic.outputs.size(), original.outputs.size());
  EXPECT_EQ(r.logic.graph.input_names(), original.graph.input_names());
  for (size_t i = 0; i < r.logic.state.size(); ++i) {
    EXPECT_EQ(r.logic.state[i].name, original.state[i].name);
    EXPECT_EQ(r.logic.state[i].init, original.state[i].init);
  }
  for (size_t i = 0; i < r.logic.outputs.size(); ++i) {
    EXPECT_EQ(r.logic.outputs[i].name, original.outputs[i].name);
  }
  for (const auto& e : r.endpoints) {
    EXPECT_EQ(e.name, r.logic.state[e.state_index].name);
    ASSERT_FALSE(e.cells.empty());
    for (size_t i = 0; i < e.cells.size(); ++i) {
      EXPECT_EQ(e.cells[i].latch, i + 1 == e.cells.size());
    }
  }
  ASSERT_LE(original.graph.input_names().size(), 10U);
  for (uint32_t x = 0; x < (1U << original.graph.input_names().size()); ++x) {
    ASSERT_EQ(evaluate(original, x), evaluate(r.logic, x)) << x;
  }
  EXPECT_LE(r.cost.total(), result.report.before.total());
  EXPECT_EQ(r.cost.total(), result.report.after.total());
  EXPECT_LE(result.report.feedback_rounds, 1U);
  Budget export_work{1000000};
  auto   net = export_lnet(r.logic, export_work);
  ASSERT_TRUE(net.net) << net.reason;
  auto roundtrip = import_lnet(*net.net, export_work);
  ASSERT_EQ(roundtrip.status, Status::feasible);
  // Xag_region permits interleaved sources; export/import uses PI then state
  // order. These fixtures create all primary inputs before their state sources.
  for (uint32_t x = 0; x < (1U << original.graph.input_names().size()); ++x) {
    ASSERT_EQ(evaluate(original, x), evaluate(roundtrip, x)) << x;
  }
}
}  // namespace

TEST(LogicalRegion, FullyAbsorbedEndpointsSkipResidualAndKeepState) {
  Xag_region r;
  const auto a = input(r, "a"), b = input(r, "b"), c = input(r, "c");
  const auto d  = r.graph.land(r.graph.lor(a, b), r.graph.lor(a, c));
  const auto si = state(r, "pipe.r[0]", d, '1');
  r.outputs     = {
      { "q",  r.state[si].q},
      {"nq", ~r.state[si].q}
  };
  r.status = Status::feasible;
  Logical_options o;
  o.endpoint.cost.static_and = 20;
  Budget     work{100000000};
  const auto result = synthesize_logical_region(r, std::array{si}, o, work);
  check(r, result, 1);
  ASSERT_TRUE(result.region);
  EXPECT_TRUE(result.report.residual.skipped);
  EXPECT_EQ(result.report.feedback_attempts, 0U);
  EXPECT_TRUE(result.region->endpoints[0].whole_cone);
  EXPECT_EQ(result.region->cost.static_logic, 0U);
  EXPECT_EQ(result.region->cost.inverters, 0U);  // Q and !Q of selected state
}

TEST(LogicalRegion, NonCutLocalDivisorsPreserveStateAndSharedOutputsThroughResidualOptimization) {
  Xag_region r;
  const auto a = input(r, "a"), b = input(r, "b"), c = input(r, "c");
  const auto shared = r.graph.land(a, b);
  const auto d      = r.graph.lxor(r.graph.lxor(shared, c), c);
  const auto si     = state(r, "pipe.state", d, '1');
  r.outputs         = {
      {     "q",  r.state[si].q},
      {    "nq", ~r.state[si].q},
      {"shared",         shared}
  };
  r.status = Status::feasible;
  Logical_options o;
  o.endpoint.cost.static_and    = 20;
  o.endpoint.boundaries         = 1;
  o.endpoint.divisor_partitions = 0;
  o.endpoint.local_candidates   = 1;
  for (bool feedback : {false, true}) {
    o.feedback = feedback;
    Budget     work{100000000};
    const auto result = synthesize_logical_region(r, std::array{si}, o, work);
    check(r, result, 1);
    ASSERT_TRUE(result.region);
    ASSERT_EQ(result.report.initial.size(), 1U);
    EXPECT_EQ(result.report.initial[0].local_wins, 1U);
    EXPECT_EQ(result.region->endpoints[0].origin, "local-divisor-residual");
    EXPECT_EQ(result.region->cost.static_logic, o.endpoint.cost.static_and);
    EXPECT_FALSE(result.report.residual.skipped);
  }
}

TEST(LogicalRegion, SharedAbsorbedLogicRemainsForOutputsAndUnselectedState) {
  Xag_region r;
  const auto a = input(r, "a"), b = input(r, "b"), c = input(r, "c");
  const auto d        = r.graph.land(r.graph.lor(a, b), r.graph.lor(a, c));
  const auto selected = state(r, "selected", d), special = state(r, "barrier", ~d, '0');
  r.outputs = {
      {      "data",                   d},
      {"nq_barrier", ~r.state[special].q}
  };
  r.status = Status::feasible;
  Logical_options o;
  o.endpoint.cost.static_and = 20;
  Budget     work{100000000};
  const auto result = synthesize_logical_region(r, std::array{selected}, o, work);
  check(r, result, 1);
  ASSERT_TRUE(result.region);
  EXPECT_GT(result.region->cost.static_logic, 0U);
  EXPECT_GT(result.region->cost.inverters, 0U);  // unselected register has no free complementary rail
  EXPECT_LT(result.report.residual.cost_after, result.report.residual.cost_before);
}

TEST(LogicalRegion, PairAbsorptionUnlocksGainThatNeitherEndpointCanClaimAlone) {
  Xag_region r;
  const auto a = input(r, "a"), b = input(r, "b"), c = input(r, "c"), d = input(r, "d");
  const auto shared = r.graph.lxor(a, b);
  const auto s0     = state(r, "left", r.graph.land(shared, c), '0');
  const auto s1     = state(r, "right", r.graph.land(shared, d), '1');
  r.outputs         = {
      { "left", r.state[s0].q},
      {"right", r.state[s1].q}
  };
  r.status = Status::feasible;
  Logical_options o;
  o.endpoint.clock_phases    = 1;
  o.endpoint.cost.static_xor = 10;  // pair gain also pays shared input-rail inversions
  o.optimize_residual        = false;
  const std::array eligible{s0, s1};
  const std::array domains{0U, 0U};
  o.pair_candidates = 0;
  Budget     baseline_work{100000000};
  const auto baseline = synthesize_logical_region(r, eligible, o, baseline_work, domains);
  check(r, baseline, 2);
  ASSERT_TRUE(baseline.region);
  EXPECT_EQ(baseline.region->cost.static_logic, 10U);
  EXPECT_FALSE(baseline.region->endpoints[0].whole_cone);
  EXPECT_FALSE(baseline.region->endpoints[1].whole_cone);
  o.pair_candidates = 32;
  Budget     work{100000000};
  const auto result = synthesize_logical_region(r, eligible, o, work, domains);
  check(r, result, 2);
  ASSERT_TRUE(result.region);
  EXPECT_EQ(result.report.pairs.attempts, 1U);
  EXPECT_EQ(result.report.pairs.wins, 1U);
  EXPECT_EQ(result.region->cost.static_logic, 0U);
  EXPECT_LT(result.region->cost.total(), baseline.region->cost.total());
  EXPECT_LE(result.report.pairs.work, o.pair_work);
  for (const auto& e : result.region->endpoints) {
    EXPECT_TRUE(e.whole_cone);
    EXPECT_TRUE(e.origin.starts_with("pair-"));
  }
  // The full pair has four independent inputs, despite each endpoint having
  // only three. Reject it under a three-input joint limit.
  o.pair_inputs  = 3;
  o.pair_choices = 0;  // isolate the admission count from the second choice sweep
  Budget     capped_work{100000000};
  const auto capped = synthesize_logical_region(r, eligible, o, capped_work, domains);
  check(r, capped, 2);
  EXPECT_EQ(capped.report.pairs.support_skips, 1U);
  EXPECT_EQ(capped.region->cost.total(), baseline.region->cost.total());
  o.pair_inputs = 16;
  o.pair_work   = 1;
  Budget     refused_work{100000000};
  const auto refused = synthesize_logical_region(r, eligible, o, refused_work, domains);
  check(r, refused, 2);
  EXPECT_TRUE(refused.report.pairs.exhausted);
  EXPECT_EQ(refused.region->cost.total(), baseline.region->cost.total());
  bool refused_trial = false;
  for (const auto budget : {128U, 512U, 2048U}) {
    o.pair_work = budget;
    Budget     bounded_work{100000000};
    const auto bounded = synthesize_logical_region(r, eligible, o, bounded_work, domains);
    check(r, bounded, 2);
    ASSERT_TRUE(bounded.region);
    EXPECT_LE(bounded.report.pairs.work, budget);
    if (bounded.report.pairs.wins == 0) {
      EXPECT_EQ(bounded.region->cost.total(), baseline.region->cost.total());
      refused_trial |= bounded.report.pairs.attempts > 0 && bounded.report.pairs.exhausted;
    } else {
      EXPECT_TRUE(bounded.region->endpoints[0].origin.starts_with("pair-"));
      EXPECT_TRUE(bounded.region->endpoints[1].origin.starts_with("pair-"));
    }
  }
  EXPECT_TRUE(refused_trial);  // refusal after admission, not just during discovery
  o.pair_work = 16000000;
  // A third reader keeps the shared XOR alive, removing the joint gain.
  r.outputs.push_back({"shared", shared});
  Budget     protected_work{100000000};
  const auto protected_result = synthesize_logical_region(r, eligible, o, protected_work, domains);
  check(r, protected_result, 2);
  EXPECT_EQ(protected_result.report.pairs.wins, 0U);
  EXPECT_EQ(protected_result.region->cost.static_logic, 10U);
}

TEST(LogicalRegion, PairGainScreenPreservesProtectedLogicAndSkipsUnimprovableEndpoints) {
  Xag_region r;
  const auto a = input(r, "a"), b = input(r, "b");
  const auto shared = r.graph.lxor(a, b);
  const auto s0     = state(r, "left", shared, '0');
  const auto s1     = state(r, "right", shared, '1');
  r.outputs         = {
      { "shared",        shared},
      {"inverse",       ~shared},
      {   "left", r.state[s0].q},
      {  "right", r.state[s1].q}
  };
  r.status = Status::feasible;
  Logical_options o;
  o.optimize_residual     = false;
  o.endpoint.clock_phases = 1;
  const std::array eligible{s0, s1}, domains{0U, 0U};
  Budget           work{100000000};
  const auto       result = synthesize_logical_region(r, eligible, o, work, domains);
  check(r, result, 2);
  ASSERT_TRUE(result.region);
  EXPECT_EQ(result.report.pairs.trials, 1U);
  EXPECT_EQ(result.report.pairs.gain_skips, 1U);
  EXPECT_EQ(result.report.pairs.attempts, 0U);
  EXPECT_EQ(result.report.pairs.wins, 0U);
  EXPECT_FALSE(result.report.pairs.exhausted);
  EXPECT_EQ(result.region->cost.static_logic, o.endpoint.cost.static_xor);
  EXPECT_EQ(result.region->cost.inverters, o.endpoint.cost.static_not);
  EXPECT_EQ(result.region->cost.domino, 2 * (o.endpoint.cost.domino_latch + 1));
  EXPECT_EQ(result.report.before, result.report.after_pairs);

  Budget   measured{100000000};
  uint64_t samples            = 0;
  measured.admission_interval = 1;
  measured.admission          = [&] {
    ++samples;
    return true;
  };
  const auto replay = synthesize_logical_region(r, eligible, o, measured, domains);
  check(r, replay, 2);
  ASSERT_GT(samples, 3U);
  EXPECT_EQ(replay.report, result.report);
  EXPECT_EQ(measured.remaining, work.remaining);
  for (const auto stop : {samples - 2, samples - 1}) {
    Budget   refused{100000000};
    uint64_t calls             = 0;
    refused.admission_interval = 1;
    refused.admission          = [&] { return ++calls != stop; };
    const auto interrupted     = synthesize_logical_region(r, eligible, o, refused, domains);
    check(r, interrupted, 2);
    EXPECT_TRUE(refused.resource_exhausted);
    EXPECT_TRUE(interrupted.report.exhausted);
    EXPECT_EQ(calls, stop);
    EXPECT_EQ(interrupted.region->cost, result.region->cost);
  }

  o.pair_candidates = 0;
  Budget     disabled_work{100000000};
  const auto disabled = synthesize_logical_region(r, eligible, o, disabled_work, domains);
  check(r, disabled, 2);
  EXPECT_EQ(disabled.region->cost, result.region->cost);
  EXPECT_EQ(disabled.report.pairs.gain_skips, 0U);

  // Removing the protected data outputs makes absorption profitable again.
  r.outputs.erase(r.outputs.begin(), r.outputs.begin() + 2);
  o.pair_candidates          = 32;
  o.endpoint.cost.static_xor = 20;
  Budget     removable_work{100000000};
  const auto removable = synthesize_logical_region(r, eligible, o, removable_work, domains);
  check(r, removable, 2);
  EXPECT_EQ(removable.report.pairs.gain_skips, 0U);
  EXPECT_EQ(removable.report.pairs.wins, 1U);
  EXPECT_EQ(removable.region->cost.static_logic, 0U);
}

TEST(LogicalRegion, UniformPairSamplesRemainInconclusiveRatherThanProvingConstancy) {
  uint64_t skipped = 0, attempted = 0;
  // Of these two seven-input minterms, only one is seen by the 64 samples:
  // the first six variables enumerate assignments, and the seventh selects
  // which minterm fires. Both functions are nonconstant on the full basis.
  for (bool inverse : {false, true}) {
    Xag_region             r;
    std::array<Xsignal, 7> inputs;
    for (size_t i = 0; i < inputs.size(); ++i) {
      inputs[i] = input(r, std::to_string(i));
    }
    auto shared = inputs[0];
    for (size_t i = 1; i < 6; ++i) {
      shared = r.graph.land(shared, inputs[i]);
    }
    shared        = r.graph.land(shared, inverse ? ~inputs[6] : inputs[6]);
    const auto s0 = state(r, "left", shared), s1 = state(r, "right", shared);
    r.outputs = {
        {"shared",        shared},
        {  "left", r.state[s0].q},
        { "right", r.state[s1].q}
    };
    r.status = Status::feasible;
    Logical_options o;
    o.pair_choices          = 0;
    o.endpoint.gates        = {8, 8, 10};
    o.endpoint.clock_phases = 1;
    o.optimize_residual     = false;
    Budget     work{100000000};
    const auto result = synthesize_logical_region(r, std::array{s0, s1}, o, work, std::array{0U, 0U});
    check(r, result, 2);
    EXPECT_EQ(result.report.pairs.trials, 1U);
    EXPECT_EQ(result.report.pairs.wins, 0U);
    EXPECT_EQ(result.report.before, result.report.after_pairs);
    skipped   += result.report.pairs.gain_skips;
    attempted += result.report.pairs.attempts;
  }
  EXPECT_EQ(skipped, 1U);
  EXPECT_EQ(attempted, 1U);
}

TEST(LogicalRegion, BoundedPairWindowFindsGainInsideOversizedSourceCones) {
  Xag_region r;
  const auto a = input(r, "a"), b = input(r, "b"), c = input(r, "c"), d = input(r, "d");
  const auto e = input(r, "e"), f = input(r, "f");
  const auto ab = r.graph.land(a, b), cd = r.graph.land(c, d);
  const auto shared = r.graph.lxor(ab, cd);
  const auto s0     = state(r, "left", r.graph.land(shared, e));
  const auto s1     = state(r, "right", r.graph.land(shared, f));
  r.outputs         = {
      { "left", r.state[s0].q},
      {"right", r.state[s1].q}
  };
  r.status = Status::feasible;
  Logical_options o;
  o.endpoint.window.inputs   = 4;
  o.endpoint.clock_phases    = 1;
  o.endpoint.cost.static_xor = 10;
  o.pair_inputs              = 4;
  o.optimize_residual        = false;
  const std::array eligible{s0, s1}, domains{0U, 0U};
  o.pair_candidates = 0;
  Budget     baseline_work{100000000};
  const auto baseline = synthesize_logical_region(r, eligible, o, baseline_work, domains);
  check(r, baseline, 2);
  o.pair_candidates = 32;
  Budget     work{100000000};
  const auto result = synthesize_logical_region(r, eligible, o, work, domains);
  check(r, result, 2);
  EXPECT_GT(result.report.pairs.bounded_windows, 0U);
  EXPECT_EQ(result.report.pairs.wins, 1U);
  EXPECT_LT(result.region->cost.total(), baseline.region->cost.total());
  EXPECT_EQ(result.region->cost.static_logic, 2 * o.endpoint.cost.static_and);
  EXPECT_LE(result.report.pairs.work, o.pair_work);
  for (const auto& endpoint : result.region->endpoints) {
    EXPECT_FALSE(endpoint.whole_cone);
    EXPECT_TRUE(endpoint.origin.starts_with("pair-"));
  }
  Budget   measured{100000000};
  uint64_t samples            = 0;
  measured.admission_interval = 1;
  measured.admission          = [&] {
    ++samples;
    return true;
  };
  const auto replay = synthesize_logical_region(r, eligible, o, measured, domains);
  check(r, replay, 2);
  EXPECT_EQ(replay.report, result.report);
  EXPECT_EQ(measured.remaining, work.remaining);
  ASSERT_GT(samples, 2U);
  Budget   refused{100000000};
  uint64_t calls             = 0;
  refused.admission_interval = 1;
  refused.admission          = [&] { return ++calls != samples - 1; };
  const auto interrupted     = synthesize_logical_region(r, eligible, o, refused, domains);
  check(r, interrupted, 2);
  EXPECT_TRUE(refused.resource_exhausted);
  EXPECT_TRUE(interrupted.report.exhausted);
  EXPECT_EQ(calls, samples - 1);
  EXPECT_EQ(interrupted.region->cost, result.region->cost);
  for (uint64_t cap : {32U, 128U, 1024U, 4096U}) {
    auto limited_options      = o;
    limited_options.pair_work = cap;
    Budget     limited_work{100000000};
    const auto limited = synthesize_logical_region(r, eligible, limited_options, limited_work, domains);
    check(r, limited, 2);
    EXPECT_LE(limited.report.pairs.work, cap);
    EXPECT_LE(limited.region->cost.total(), baseline.region->cost.total());
  }
  // A third output keeps the XOR, so the same local window earns no joint
  // deletion credit. The caller's full-graph ledger remains authoritative.
  r.outputs.push_back({"protected", shared});
  Budget     protected_work{100000000};
  const auto protected_result = synthesize_logical_region(r, eligible, o, protected_work, domains);
  check(r, protected_result, 2);
  EXPECT_GT(protected_result.report.pairs.bounded_windows, 0U);
  EXPECT_EQ(protected_result.report.pairs.wins, 0U);
  EXPECT_EQ(protected_result.region->cost.static_logic, o.endpoint.cost.static_xor + 2 * o.endpoint.cost.static_and);
}

TEST(LogicalRegion, PairCombinationsRetainTheCheaperExistingPartner) {
  // Costs with the former both-replacements-only selector were 46 and 55.
  // These networks expose sharing penalties hidden by separate endpoint costs.
  for (uint32_t trial : {11U, 26U}) {
    SCOPED_TRACE(trial);
    auto            r  = pair_choice_fixture(trial);
    const uint32_t  s0 = 0, s1 = 1;
    Logical_options o;
    o.pair_choices             = 0;
    o.endpoint.gates           = {3, 3, 4};
    o.endpoint.clock_phases    = 1;
    o.endpoint.cost.static_xor = 10;
    o.endpoint.fast_accept     = false;
    o.endpoint.boundaries      = 4;
    o.optimize_residual        = false;
    Budget     work{100000000};
    const auto result = synthesize_logical_region(r, std::array{s0, s1}, o, work, std::array{0U, 0U});
    check(r, result, 2);
    EXPECT_EQ(result.region->cost.total(), trial == 11 ? 44U : 51U);
    EXPECT_LT(result.region->cost.total(), trial == 11 ? 46U : 55U);
    EXPECT_GT(result.report.pairs.combinations, result.report.pairs.attempts);
    EXPECT_LE(result.report.pairs.combinations, 3 * result.report.pairs.attempts);
    EXPECT_LE(result.report.pairs.work, o.pair_work);
    if (trial == 11) {
      EXPECT_EQ(result.report.pairs.wins, 1U);
      const auto changed = std::count_if(result.region->endpoints.begin(),
                                         result.region->endpoints.end(),
                                         [](const auto& endpoint) { return endpoint.origin.starts_with("pair-"); });
      EXPECT_EQ(changed, 1);
    }
    Budget   measured{100000000};
    uint64_t samples            = 0;
    measured.admission_interval = 1;
    measured.admission          = [&] {
      ++samples;
      return true;
    };
    const auto replay = synthesize_logical_region(r, std::array{s0, s1}, o, measured, std::array{0U, 0U});
    check(r, replay, 2);
    EXPECT_EQ(replay.report, result.report);
    EXPECT_EQ(measured.remaining, work.remaining);
    ASSERT_GT(samples, 2U);
    Budget   refused{100000000};
    uint64_t calls             = 0;
    refused.admission_interval = 1;
    refused.admission          = [&] { return ++calls != samples - 1; };
    const auto interrupted     = synthesize_logical_region(r, std::array{s0, s1}, o, refused, std::array{0U, 0U});
    check(r, interrupted, 2);
    EXPECT_TRUE(refused.resource_exhausted);
    EXPECT_TRUE(interrupted.report.exhausted);
    EXPECT_EQ(calls, samples - 1);
    EXPECT_EQ(interrupted.region->cost, result.region->cost);
    if (trial == 11) {
      // Stop at the first checkpoint inside the third combination. The first
      // two have been completely priced; refusal must retain their best result
      // instead of losing it with the incomplete candidate snapshot.
      const auto interrupt_at = [&](uint64_t stop) {
        Budget   partial_work{100000000};
        uint64_t visits                 = 0;
        partial_work.admission_interval = 1;
        partial_work.admission          = [&] { return ++visits != stop; };
        auto partial                    = synthesize_logical_region(r, std::array{s0, s1}, o, partial_work, std::array{0U, 0U});
        EXPECT_EQ(visits, stop);
        EXPECT_TRUE(partial_work.resource_exhausted);
        return partial;
      };
      uint64_t low = 1, high = samples - 1;
      while (low < high) {
        const auto middle  = low + (high - low) / 2;
        const auto partial = interrupt_at(middle);
        if (partial.report.pairs.combinations >= 3) {
          high = middle;
        } else {
          low = middle + 1;
        }
      }
      const auto partial = interrupt_at(low);
      check(r, partial, 2);
      EXPECT_EQ(partial.report.pairs.combinations, 3U);
      EXPECT_EQ(partial.report.pairs.wins, 1U);
      EXPECT_TRUE(partial.report.pairs.exhausted);
      EXPECT_LE(partial.region->cost.total(), 46U);
    }
  }
}

TEST(LogicalRegion, PairQueueRefreshesAnOverlappingCandidateAfterAnAtomicCommit) {
  Xag_region r;
  const auto a = input(r, "a"), b = input(r, "b"), c = input(r, "c"), d = input(r, "d");
  const auto e = input(r, "e"), f = input(r, "f");
  const auto s = r.graph.lxor(a, b), t = r.graph.lxor(c, d);
  const auto s0 = state(r, "both", r.graph.lor(s, t));
  const auto s1 = state(r, "left", r.graph.land(s, e));
  const auto s2 = state(r, "right", r.graph.land(t, f));
  r.outputs     = {
      { "both", r.state[s0].q},
      { "left", r.state[s1].q},
      {"right", r.state[s2].q}
  };
  r.status = Status::feasible;
  Logical_options o;
  o.endpoint.clock_phases    = 1;
  o.endpoint.cost.static_xor = 10;
  o.optimize_residual        = false;
  Budget     work{100000000};
  const auto result = synthesize_logical_region(r, std::array{s0, s1, s2}, o, work, std::array{0U, 0U, 0U});
  check(r, result, 3);
  EXPECT_EQ(result.report.pairs.trials, 2U);
  EXPECT_EQ(result.report.pairs.wins, 2U);
  EXPECT_EQ(result.report.pairs.refreshes, 3U);
  ASSERT_TRUE(result.region);
  EXPECT_EQ(result.region->cost.static_logic, 0U);
  o.pair_trials = 1;
  Budget     limited_work{100000000};
  const auto limited = synthesize_logical_region(r, std::array{s0, s1, s2}, o, limited_work, std::array{0U, 0U, 0U});
  check(r, limited, 3);
  EXPECT_EQ(limited.report.pairs.trials, 1U);
  EXPECT_EQ(limited.report.pairs.wins, 1U);
  EXPECT_TRUE(limited.report.pairs.exhausted);
  ASSERT_TRUE(limited.region);
  EXPECT_LT(result.region->cost.total(), limited.region->cost.total());
}

TEST(LogicalRegion, ResidualRewriteExposesBetterEndpointInOneFeedbackRound) {
  Xag_region r;
  const auto a = input(r, "a"), b = input(r, "b"), c = input(r, "c");
  const auto d  = r.graph.lxor(r.graph.lxor(a, b), r.graph.lxor(b, r.graph.land(a, c)));
  const auto si = state(r, "r", d);
  r.outputs     = {
      {"q", r.state[si].q}
  };
  r.status = Status::feasible;
  Logical_options o;
  o.endpoint.gates           = {2, 2, 2};
  o.endpoint.window.inputs   = 2;  // initial reconvergent three-source cone is not admitted
  o.endpoint.clock_phases    = 1;
  o.endpoint.cost.static_xor = 1;  // retain cheap XOR instead of forcing an expensive dual-rail cell
  o.residual.resub_inputs    = 8;
  Budget     work{100000000};
  const auto result = synthesize_logical_region(r, std::array{si}, o, work);
  check(r, result, 1);
  ASSERT_TRUE(result.region);
  EXPECT_GT(result.report.residual.rewrite_wins + result.report.residual.resub_wins, 0U);
  EXPECT_EQ(result.report.feedback_rounds, 1U);
  EXPECT_EQ(result.report.feedback_attempts, 1U);
  EXPECT_EQ(result.report.feedback_wins, 1U);
  EXPECT_EQ(result.report.work.total(), 100000000 - work.remaining);
  EXPECT_GT(result.report.work.admission, 0U);
  EXPECT_GT(result.report.work.selection, 0U);
  EXPECT_GT(result.report.work.residual, 0U);
  EXPECT_GT(result.report.work.feedback, 0U);
  EXPECT_GT(result.report.work.cleanup, 0U);
  EXPECT_EQ(result.report.work.pairs, result.report.pairs.work);
  EXPECT_LT(result.report.after.total(), result.report.after_residual.total());
  EXPECT_EQ(result.region->cost.static_logic, 0U);
}

TEST(LogicalRegion, NewDivisorBindingsSurviveSharedResidualOptimization) {
  Xag_region r;
  const auto a = input(r, "a"), b = input(r, "b"), c = input(r, "c");
  const auto shared = r.graph.land(a, b);
  const auto si     = state(r, "pipe.r", r.graph.lor(shared, r.graph.land(a, c)), '0');
  r.outputs         = {
      {"shared",         shared},
      {     "q",  r.state[si].q},
      {    "nq", ~r.state[si].q}
  };
  r.status = Status::feasible;
  Logical_options o;
  o.endpoint.gates           = {2, 2, 2};
  o.endpoint.cost.static_and = 20;
  Budget     work{100000000};
  const auto result = synthesize_logical_region(r, std::array{si}, o, work);
  check(r, result, 1);
  ASSERT_TRUE(result.region);
  ASSERT_EQ(result.report.initial.size(), 1U);
  EXPECT_GT(result.report.initial[0].new_divisor_attempts, 0U);
  ASSERT_EQ(result.region->endpoints.size(), 1U);
  EXPECT_EQ(result.region->endpoints[0].origin, "functional-two-phase");
  EXPECT_EQ(result.region->endpoints[0].cells.size(), 2U);
  EXPECT_FALSE(result.report.residual.skipped);
  EXPECT_GT(result.region->cost.static_logic, 0U);
}

TEST(LogicalRegion, ResidualReplacementRebindsSyntheticEndpointInputs) {
  Xag_region r;
  const auto d = input(r, "d"), e = input(r, "e"), b = input(r, "b"), c = input(r, "c");
  const auto a  = r.graph.lxor(r.graph.lxor(d, e), e);
  const auto si = state(r, "pipe.r", r.graph.lor(r.graph.land(a, b), r.graph.land(a, c)), '1');
  r.outputs     = {
      { "q",  r.state[si].q},
      {"nq", ~r.state[si].q}
  };
  r.status = Status::feasible;
  Logical_options o;
  o.endpoint.gates           = {2, 2, 2};
  o.endpoint.window.inputs   = 3;  // admit {a,b,c}, leaving the two XORs upstream
  o.endpoint.cost.static_and = 20;
  o.endpoint.cost.static_xor = 20;
  for (bool feedback : {false, true}) {
    SCOPED_TRACE(feedback);
    o.feedback = feedback;
    Budget     work{100000000};
    const auto result = synthesize_logical_region(r, std::array{si}, o, work);
    check(r, result, 1);
    ASSERT_TRUE(result.region);
    ASSERT_EQ(result.report.initial.size(), 1U);
    EXPECT_GT(result.report.initial[0].new_divisor_attempts, 0U);
    EXPECT_TRUE(result.report.residual_accepted);
    EXPECT_GT(result.report.residual.rewrite_wins + result.report.residual.resub_wins, 0U);
    EXPECT_LT(result.report.after_residual.total(), result.report.before.total());
    const auto& endpoint = result.region->endpoints[0];
    ASSERT_EQ(endpoint.origin, "functional-two-phase");
    ASSERT_EQ(endpoint.cells.size(), 2U);
    EXPECT_EQ(result.region->cost.static_logic, 0U);
    for (const auto& cell : endpoint.cells) {
      for (size_t i = 0; i < cell.inputs.size(); ++i) {
        if (cell.producers[i] < 0) {
          EXPECT_EQ(result.region->logic.graph.node(cell.inputs[i].id).kind, Xag::Kind::source);
        }
      }
    }
  }
}

TEST(LogicalRegion, ExistingDivisorCarePreservesSharedStaticReadersAndState) {
  Xag_region r;
  const auto a = input(r, "a"), b = input(r, "b"), c = input(r, "c");
  const auto ab = r.graph.land(a, b), nc = r.graph.land(~a, c);
  const auto si = state(r, "care.r", r.graph.lxor(ab, nc), '1');
  r.outputs     = {
      {     "q",  r.state[si].q},
      {    "nq", ~r.state[si].q},
      {"shared",             ab}
  };
  r.status = Status::feasible;
  Logical_options o;
  o.endpoint.gates              = {2, 1, 2};
  o.endpoint.clock_phases       = 1;
  o.endpoint.divisor_partitions = 0;
  o.endpoint.cost.static_and    = 20;
  o.endpoint.cost.static_xor    = 20;
  Budget     work{100000000};
  const auto result = synthesize_logical_region(r, std::array{si}, o, work);
  check(r, result, 1);
  ASSERT_TRUE(result.region);
  EXPECT_EQ(result.region->endpoints[0].origin, "existing-care-residual");
  EXPECT_EQ(result.region->endpoints[0].cells.size(), 1U);
  EXPECT_GT(result.report.initial[0].existing_care_images, 0U);
  EXPECT_GT(result.region->cost.static_logic, 0U);
  EXPECT_FALSE(result.report.residual.skipped);
}

TEST(LogicalRegion, SharedResidualChangeReconsidersEveryDependentEndpoint) {
  Xag_region r;
  const auto a = input(r, "a"), b = input(r, "b"), c = input(r, "c");
  const auto d     = r.graph.lxor(r.graph.lxor(a, b), r.graph.lxor(b, r.graph.land(a, c)));
  const auto first = state(r, "a.r", d), second = state(r, "b.r", ~d), quiet = state(r, "quiet", a);
  r.status = Status::feasible;
  Logical_options o;
  o.endpoint.gates           = {2, 2, 2};
  o.endpoint.window.inputs   = 2;
  o.endpoint.clock_phases    = 1;
  o.endpoint.cost.static_xor = 1;
  Budget     work{100000000};
  const auto result = synthesize_logical_region(r, std::array{second, quiet, first}, o, work);
  check(r, result, 3);
  ASSERT_TRUE(result.region);
  EXPECT_EQ(result.report.feedback_rounds, 1U);
  EXPECT_EQ(result.report.feedback_attempts, 2U);
  // Each alone can lose because the shared residual remains for its reader.
  // Joint endpoint search is a separate stage, not an unearned deletion credit.
  EXPECT_LE(result.report.after.total(), result.report.after_residual.total());
}

TEST(LogicalRegion, ParallelFirstPhaseCellsRemainCombinationalInCmos) {
  Xag_region r;
  const auto a = input(r, "a"), b = input(r, "b"), c = input(r, "c"), d = input(r, "d");
  const auto root = r.graph.land(r.graph.land(a, b), r.graph.land(c, d));
  const auto si   = state(r, "r", root, '0');
  r.status        = Status::feasible;
  Logical_options o;
  o.endpoint.gates           = {2, 2, 2};
  o.endpoint.cost.static_and = 20;
  Budget     work{100000000};
  const auto result = synthesize_logical_region(r, std::array{si}, o, work);
  check(r, result, 1);
  ASSERT_TRUE(result.region);
  ASSERT_EQ(result.region->endpoints[0].cells.size(), 3U);
  EXPECT_EQ(result.region->endpoints[0].cells.back().phase, 2U);
  EXPECT_TRUE(result.report.residual.skipped);
}

TEST(LogicalRegion, OutputOnlyLogicUsesResidualOptimization) {
  Xag_region r;
  const auto a = input(r, "a"), b = input(r, "b"), c = input(r, "c");
  const auto d = r.graph.land(r.graph.lor(a, b), r.graph.lor(a, c));
  r.outputs    = {
      {    "out",  d},
      {"inverse", ~d}
  };
  r.status = Status::feasible;
  Budget     work{100000000};
  const auto result = synthesize_logical_region(r, {}, {}, work);
  check(r, result, 0);
  EXPECT_LT(result.report.after.total(), result.report.before.total());
  EXPECT_EQ(result.report.feedback_rounds, 0U);
}

TEST(LogicalRegion, InvalidEligibilityAndResourceLimitsPublishNoPartialSelection) {
  Xag_region r;
  const auto a = input(r, "a"), b = input(r, "b");
  const auto si = state(r, "r", r.graph.land(a, b));
  r.status      = Status::feasible;
  Budget work{10000000};
  auto   result = synthesize_logical_region(r, std::array{si, si}, {}, work);
  EXPECT_EQ(result.status, Status::invalid);
  EXPECT_FALSE(result.region);
  result = synthesize_logical_region(r, std::array{si + 1}, {}, work);
  EXPECT_EQ(result.status, Status::invalid);
  EXPECT_FALSE(result.region);
  Budget tiny{1};
  result = synthesize_logical_region(r, std::array{si}, {}, tiny);
  EXPECT_EQ(result.status, Status::search_exhausted);
  EXPECT_FALSE(result.region);
  Budget denied{10000000};
  denied.admission = [] { return false; };
  result           = synthesize_logical_region(r, std::array{si}, {}, denied);
  EXPECT_EQ(result.status, Status::search_exhausted);
  EXPECT_FALSE(result.region);
  Logical_options o;
  o.residual.max_nodes = 1;  // initial selection survives a refused residual rebuild
  result               = synthesize_logical_region(r, std::array{si}, o, work);
  check(r, result, 1);
  EXPECT_TRUE(result.report.exhausted);
  EXPECT_FALSE(result.report.residual_accepted);
}

TEST(LogicalRegion, EndpointSearchExhaustionDoesNotConsumeValidationAllowance) {
  Xag_region r;
  auto       root = input(r, "a0");
  for (uint32_t i = 1; i < 8; ++i) {
    root = r.graph.land(root, input(r, "a" + std::to_string(i)));
  }
  const auto si = state(r, "q", root);
  r.status      = Status::feasible;
  Logical_options o;
  o.optimize_residual    = false;
  o.endpoint.gates       = {2, 2, 2};
  o.endpoint.fast_accept = false;
  bool exercised         = false;
  for (uint64_t cap = 64; cap <= 2048; cap += 64) {
    Budget     search{cap};
    const auto found = select_endpoint(r.graph, root, "q", {}, {}, o.endpoint, search);
    if (!found.selected || (!search.exhausted && search.remaining != 0)) {
      continue;
    }
    Budget validation{cap};
    if (!validate_endpoint(r.graph, *found.selected, o.endpoint, validation)) {
      continue;
    }
    exercised       = true;
    o.endpoint_work = cap;
    Budget     work{1000000};
    const auto result = synthesize_logical_region(r, std::array{si}, o, work);
    check(r, result, 1);
    EXPECT_EQ(result.report.work.total(), 1000000 - work.remaining);
    EXPECT_FALSE(work.resource_exhausted);
    break;
  }
  EXPECT_TRUE(exercised);
}

TEST(LogicalRegion, WorkAccountingIncludesRefusedTrialsAndDisabledStages) {
  Xag_region r;
  const auto a = input(r, "a"), b = input(r, "b"), c = input(r, "c");
  const auto shared = r.graph.lxor(a, b);
  const auto si     = state(r, "r", r.graph.lxor(shared, r.graph.lxor(b, c)));
  r.outputs         = {
      {"outside", shared}
  };
  r.status = Status::feasible;
  // Exercise admission refusal, failure inside the child endpoint slice,
  // complete incumbents and successful work. The caller already spent credits.
  for (uint64_t credits : {0ULL, 1ULL, 16ULL, 64ULL, 256ULL, 1024ULL, 4096ULL, 65536ULL, 1000000ULL}) {
    SCOPED_TRACE(credits);
    for (bool residual : {false, true}) {
      Logical_options o;
      o.optimize_residual     = residual;
      o.endpoint.gates        = {2, 2, 2};
      o.endpoint.clock_phases = 1;
      Budget work{credits + 13};
      ASSERT_TRUE(work.spend(13));
      const auto result = synthesize_logical_region(r, std::array{si}, o, work);
      EXPECT_EQ(result.report.work.total(), credits - work.remaining);
      EXPECT_EQ(result.report.work.pairs, result.report.pairs.work);
      if (!residual) {
        EXPECT_EQ(result.report.work.residual, 0U);
        EXPECT_EQ(result.report.work.feedback, 0U);
        EXPECT_EQ(result.report.work.cleanup, 0U);
      }
      if (result.region) {
        check(r, result, 1);
      }
    }
  }
  for (uint64_t checkpoints : {0ULL, 3ULL, 20ULL, 100ULL}) {
    Budget   work{1000000};
    uint64_t polls          = 0;
    work.admission_interval = 1;
    work.admission          = [&] { return polls++ < checkpoints; };
    const auto result       = synthesize_logical_region(r, std::array{si}, {}, work);
    EXPECT_EQ(result.report.work.total(), 1000000 - work.remaining);
    EXPECT_TRUE(result.report.exhausted);
  }
}

TEST(LogicalRegion, StaticInverterCostCanRejectAnAndXorProxyWin) {
  Xag_region r;
  const auto s = input(r, "s"), a = input(r, "a"), b = input(r, "b");
  r.outputs = {
      {"mux", r.graph.mux(s, a, b)}
  };
  r.status = Status::feasible;
  Logical_options o;
  o.endpoint.cost.static_not = 3;
  Budget     work{10000000};
  const auto result = synthesize_logical_region(r, {}, o, work);
  check(r, result, 0);
  EXPECT_LT(result.report.residual.cost_after, result.report.residual.cost_before);
  EXPECT_FALSE(result.report.residual_accepted);
  EXPECT_EQ(result.report.after.total(), result.report.before.total());
}

TEST(LogicalRegion, ConstantAndIdentityStateSurviveEmptyResidual) {
  Xag_region r;
  const auto a      = input(r, "a");
  const auto first  = state(r, "constant", r.graph.constant(true), '0');
  const auto second = state(r, "identity", ~a, '1');
  r.status          = Status::feasible;
  Budget     work{10000000};
  const auto result = synthesize_logical_region(r, std::array{second, first}, {}, work);
  check(r, result, 2);
  EXPECT_TRUE(result.report.residual.skipped);
}

TEST(LogicalRegion, ReconvergentStateAndOutputNetworksPreserveAllFunctions) {
  uint32_t   random = 391;
  const auto next   = [&] {
    random = random * 1664525U + 1013904223U;
    return random;
  };
  for (uint32_t trial = 0; trial < 12; ++trial) {
    Xag_region           r;
    std::vector<Xsignal> pool;
    for (uint32_t i = 0; i < 4; ++i) {
      pool.push_back(input(r, std::to_string(i)));
    }
    for (uint32_t i = 0; i < 3; ++i) {
      state(r, "state" + std::to_string(i), {}, i == 0 ? '0' : 'x');
      pool.push_back(r.state.back().q);
    }
    for (uint32_t i = 0; i < 18; ++i) {
      auto a = pool[next() % pool.size()], b = pool[next() % pool.size()];
      if (next() & 2U) {
        a = ~a;
      }
      if (next() & 4U) {
        b = ~b;
      }
      pool.push_back((next() & 8U) ? r.graph.land(a, b) : r.graph.lxor(a, b));
    }
    for (uint32_t i = 0; i < 3; ++i) {
      r.state[i].d = pool[pool.size() - 1 - i];
      r.outputs.push_back({"out" + std::to_string(i), ~pool[pool.size() - 4 - i]});
    }
    r.status = Status::feasible;
    Logical_options o;
    o.endpoint.gates         = {2, 2, 2};
    o.endpoint.clock_phases  = 1 + (trial % 2);
    o.endpoint.window.inputs = 4;
    o.endpoint_work          = 500000;
    o.residual.window_work   = 10000;
    Budget work{20000000};
    auto   result = synthesize_logical_region(r, std::array<uint32_t, 2>{2, 0}, o, work, std::array{0U, 0U, 0U});
    SCOPED_TRACE(trial);
    check(r, result, 2);
    ASSERT_TRUE(result.region);
    const auto frozen = freeze_endpoint_netlist(*result.region, o, work, std::array{0U, 0U, 0U});
    ASSERT_TRUE(frozen.netlist) << frozen.reason;
    const auto expanded = expand_endpoint_netlist(*frozen.netlist, work);
    ASSERT_EQ(expanded.status, Status::feasible) << expanded.reason;
    for (uint32_t x = 0; x < 128; ++x) {
      EXPECT_EQ(evaluate(expanded, x), evaluate(r, x)) << x;
    }
  }
}

TEST(LogicalRegion, JointInterfaceChoicesImproveBeyondTheSingleBestPairTrajectory) {
  // Retaining only the independent winner misses these reuse-aware choices.
  // Seed 26 also guards scheduling: choices before the original pair trajectory
  // become trapped at cost 61, while the original trajectory reaches 51.
  for (uint32_t seed : {26U, 49U, 156U}) {
    SCOPED_TRACE(seed);
    auto            r = pair_choice_fixture(seed);
    Logical_options o;
    o.endpoint.gates           = {3, 3, 4};
    o.endpoint.clock_phases    = 1;
    o.endpoint.cost.static_xor = 10;
    o.endpoint.fast_accept     = false;
    o.endpoint.boundaries      = 4;
    o.optimize_residual        = false;
    o.pair_choices             = 0;
    Budget old_work{100000000};
    auto   old = synthesize_logical_region(r, std::array{0U, 1U}, o, old_work, std::array{0U, 0U});
    check(r, old, 2);
    ASSERT_TRUE(old.region);
    EXPECT_EQ(old.region->cost.total(), seed == 26 ? 51U : seed == 49 ? 82U : 73U);
    o.pair_choices = 4;
    Budget work{100000000};
    auto   result = synthesize_logical_region(r, std::array{0U, 1U}, o, work, std::array{0U, 0U});
    check(r, result, 2);  // all assignments, including seed 156's outside reader
    ASSERT_TRUE(result.region);
    EXPECT_EQ(result.region->cost.total(), seed == 26 ? 50U : seed == 49 ? 76U : 67U);
    EXPECT_LT(result.region->cost.total(), old.region->cost.total());
    EXPECT_GT(result.report.pairs.choices, 0U);
    EXPECT_GT(result.report.pairs.choice_combinations, 0U);
    EXPECT_EQ(result.report.pairs.joint_windows, 0U);  // one phase never constructs joint two-phase cells
    EXPECT_EQ(result.report.pairs.joint_care_windows, 0U);
    EXPECT_EQ(result.report.pairs.joint_recode_windows, 0U);
    EXPECT_LE(result.report.pairs.choices, 2 * o.pair_choices * result.report.pairs.attempts);
    EXPECT_LE(result.report.pairs.choice_combinations,
              ((o.pair_choices + 2) * (o.pair_choices + 2) - 4) * result.report.pairs.attempts);
    EXPECT_LE(result.report.pairs.trials, o.pair_trials);
    EXPECT_LE(result.report.pairs.work, o.pair_work);
    EXPECT_EQ(result.report.work.total(), 100000000 - work.remaining);
    Budget   replay_work{100000000};
    uint64_t samples               = 0;
    replay_work.admission_interval = 1;
    replay_work.admission          = [&] {
      ++samples;
      return true;
    };
    auto replay = synthesize_logical_region(r, std::array{0U, 1U}, o, replay_work, std::array{0U, 0U});
    check(r, replay, 2);
    EXPECT_EQ(replay.report, result.report);
    EXPECT_EQ(replay_work.remaining, work.remaining);
    ASSERT_GT(samples, 1U);
    Budget   refused{100000000};
    uint64_t visits            = 0;
    refused.admission_interval = 1;
    refused.admission          = [&] { return ++visits < samples - 1; };
    auto partial               = synthesize_logical_region(r, std::array{0U, 1U}, o, refused, std::array{0U, 0U});
    check(r, partial, 2);
    EXPECT_TRUE(refused.resource_exhausted);
    EXPECT_TRUE(partial.report.exhausted);
    EXPECT_EQ(partial.region->cost, result.region->cost);
    EXPECT_EQ(partial.report.work.total(), 100000000 - refused.remaining);
  }
}

TEST(LogicalRegion, FanoutFreePairWindowImprovesReuseWithoutChangingOutsideReaders) {
  // Seed 54 needs an outside-derived boundary not retained by either preceding
  // pair sweep. A live external output must keep its implementation and rails.
  // Exercise the same consumer as unselected sequential state as well.
  for (bool state_reader : {false, true}) {
    SCOPED_TRACE(state_reader);
    auto                  r = pair_choice_fixture(54);
    std::vector<uint32_t> domains{0, 0};
    if (state_reader) {
      const auto outside      = state(r, "outside_state", r.outputs.back().signal, '1');
      r.outputs.back().signal = r.state[outside].q;
      domains.push_back(0);
    }
    Logical_options o;
    o.endpoint.gates           = {3, 3, 4};
    o.endpoint.clock_phases    = 1;
    o.endpoint.cost.static_xor = 10;
    o.endpoint.fast_accept     = false;
    o.endpoint.boundaries      = 4;
    o.optimize_residual        = false;
    o.pair_trials              = 2;  // the original greedy and interface-choice sweeps
    Budget     baseline_work{100000000};
    const auto baseline = synthesize_logical_region(r, std::array{0U, 1U}, o, baseline_work, domains);
    check(r, baseline, 2);
    ASSERT_TRUE(baseline.region);
    EXPECT_EQ(baseline.region->cost.total(), 87U);
    EXPECT_EQ(baseline.report.pairs.fanout_windows, 0U);
    o.pair_trials = 64;
    Budget     work{100000000};
    const auto result = synthesize_logical_region(r, std::array{0U, 1U}, o, work, domains);
    check(r, result, 2);
    ASSERT_TRUE(result.region);
    EXPECT_EQ(result.region->cost.total(), 86U);
    EXPECT_EQ(result.report.pairs.fanout_wins, 1U);
    EXPECT_GT(result.report.pairs.fanout_windows, 0U);
    EXPECT_GE(result.report.pairs.fanout_ports, result.report.pairs.fanout_windows);
    EXPECT_LE(result.report.pairs.trials, o.pair_trials);
    EXPECT_LE(result.report.pairs.work, o.pair_work);
    EXPECT_EQ(result.report.work.total(), 100000000 - work.remaining);
    EXPECT_TRUE(std::any_of(result.region->endpoints.begin(), result.region->endpoints.end(), [](const auto& endpoint) {
      return endpoint.origin.starts_with("pair-fanout-");
    }));
    Budget     frozen_work{1000000};
    const auto frozen = freeze_endpoint_netlist(*result.region, o, frozen_work, domains);
    ASSERT_TRUE(frozen.netlist) << frozen.reason;
    const auto expanded = expand_endpoint_netlist(*frozen.netlist, frozen_work);
    ASSERT_EQ(expanded.status, Status::feasible) << expanded.reason;
    for (uint32_t x = 0; x < (1U << r.graph.input_names().size()); ++x) {
      EXPECT_EQ(evaluate(expanded, x), evaluate(r, x));
    }
    Budget   replay_work{100000000};
    uint64_t samples               = 0;
    replay_work.admission_interval = 1;
    replay_work.admission          = [&] {
      ++samples;
      return true;
    };
    const auto replay = synthesize_logical_region(r, std::array{0U, 1U}, o, replay_work, domains);
    check(r, replay, 2);
    EXPECT_EQ(replay.report, result.report);
    EXPECT_EQ(replay_work.remaining, work.remaining);
    ASSERT_GT(samples, 1U);
    Budget   refused{100000000};
    uint64_t calls             = 0;
    refused.admission_interval = 1;
    refused.admission          = [&] { return ++calls < samples - 1; };
    const auto partial         = synthesize_logical_region(r, std::array{0U, 1U}, o, refused, domains);
    check(r, partial, 2);
    EXPECT_TRUE(refused.resource_exhausted);
    EXPECT_TRUE(partial.report.exhausted);
    EXPECT_EQ(partial.region->cost, result.region->cost);
    EXPECT_EQ(partial.report.work.total(), 100000000 - refused.remaining);
  }
}

TEST(LogicalRegion, JointGainScreenRetainsTheCompleteCostOfAnOutsideReader) {
  auto       r       = pair_choice_fixture(35);
  const auto outside = r.state[0].d;
  r.outputs.push_back({"protected", outside});
  Logical_options o;
  o.endpoint.gates                 = {3, 3, 4};
  o.endpoint.clock_phases          = 2;
  o.endpoint.cost.static_xor       = 10;
  o.endpoint.fast_accept           = false;
  o.endpoint.boundaries            = 4;
  o.optimize_residual              = false;
  uint64_t          protected_cost = 0;
  std::vector<bool> seen(r.graph.size());
  std::vector<Id>   pending{outside.id};
  while (!pending.empty()) {
    const auto id = pending.back();
    pending.pop_back();
    if (!id || seen[id]) {
      continue;
    }
    seen[id]         = true;
    const auto& node = r.graph.node(id);
    if (node.kind == Xag::Kind::source) {
      continue;
    }
    protected_cost += node.kind == Xag::Kind::and_gate ? o.endpoint.cost.static_and : o.endpoint.cost.static_xor;
    for (auto input : node.inputs) {
      pending.push_back(input.id);
    }
  }
  ASSERT_GT(protected_cost, 0U);
  Budget     work{100000000};
  const auto result = synthesize_logical_region(r, std::array{0U, 1U}, o, work, std::array{0U, 0U});
  check(r, result, 2);
  ASSERT_TRUE(result.region);
  EXPECT_GE(result.region->cost.static_logic, protected_cost);
  EXPECT_GT(result.report.pairs.gain_skips, 0U);
  EXPECT_EQ(result.report.pairs.joint_candidates, 0U);
  EXPECT_EQ(result.report.pairs.joint_wins, 0U);
}

TEST(LogicalRegion, JointGainScreenChargesAMandatoryNewPhaseCell) {
  Xag_region r;
  const auto a = input(r, "a"), b = input(r, "b"), c = input(r, "c"), d = input(r, "d");
  const auto shared = r.graph.land(a, b);
  const auto s0     = state(r, "left", r.graph.land(shared, c));
  const auto s1     = state(r, "right", r.graph.lor(shared, d));
  r.outputs         = {
      { "left", r.state[s0].q},
      {"right", r.state[s1].q}
  };
  r.status = Status::feasible;
  Logical_options o;
  o.endpoint.cost.static_and = 20;
  o.optimize_residual        = false;
  Budget     work{100000000};
  const auto result = synthesize_logical_region(r, std::array{s0, s1}, o, work, std::array{0U, 0U});
  check(r, result, 2);
  ASSERT_TRUE(result.region);
  EXPECT_EQ(result.region->cost.static_logic, 0U);
  EXPECT_EQ(result.region->cost.total(), 22U);
  EXPECT_GT(result.report.pairs.gain_skips, 0U);
  EXPECT_EQ(result.report.pairs.joint_partitions, 0U);
  EXPECT_EQ(result.report.pairs.joint_wins, 0U);
}

TEST(LogicalRegion, JointFunctionalDivisorsImprovePairsIncludingFullyAbsorbedFunctions) {
  // A more expensive standalone first-phase implementation can pay for itself
  // when both endpoints share it. Individual interface choices discard these
  // candidates; full absorption must not hide them from joint discovery.
  for (uint32_t seed : {35U, 65U}) {
    SCOPED_TRACE(seed);
    auto            r = pair_choice_fixture(seed);
    Logical_options o;
    o.endpoint.gates           = {3, 3, 4};
    o.endpoint.clock_phases    = 2;
    o.endpoint.cost.static_xor = 10;
    o.endpoint.fast_accept     = false;
    o.endpoint.boundaries      = 4;
    o.optimize_residual        = false;
    o.pair_trials              = 1;
    Budget     baseline_work{100000000};
    const auto baseline = synthesize_logical_region(r, std::array{0U, 1U}, o, baseline_work, std::array{0U, 0U});
    check(r, baseline, 2);
    ASSERT_TRUE(baseline.region);
    EXPECT_EQ(baseline.region->cost.total(), seed == 35 ? 55U : 54U);
    EXPECT_EQ(baseline.region->cost.static_logic, seed == 35 ? 0U : 2U);
    EXPECT_EQ(baseline.report.pairs.joint_windows, 0U);
    o.pair_trials = 64;
    Budget     work{100000000};
    const auto result = synthesize_logical_region(r, std::array{0U, 1U}, o, work, std::array{0U, 0U});
    check(r, result, 2);
    ASSERT_TRUE(result.region);
    EXPECT_EQ(result.region->cost.total(), seed == 35 ? 49U : 47U);
    EXPECT_EQ(result.report.pairs.joint_wins, 1U);
    EXPECT_GT(result.report.pairs.joint_candidates, 0U);
    EXPECT_GE(result.report.pairs.joint_divisors, result.report.pairs.joint_candidates);
    EXPECT_LE(result.report.pairs.joint_partitions, o.endpoint.divisor_partitions * result.report.pairs.joint_windows);
    EXPECT_LE(result.report.pairs.joint_combinations, 3 * result.report.pairs.joint_candidates);
    EXPECT_LE(result.report.pairs.trials, o.pair_trials);
    EXPECT_LE(result.report.pairs.work, o.pair_work);
    EXPECT_EQ(result.report.work.total(), 100000000 - work.remaining);
    for (const auto& endpoint : result.region->endpoints) {
      EXPECT_EQ(endpoint.origin, "pair-joint-functional");
    }
    Budget     frozen_work{1000000};
    const auto frozen = freeze_endpoint_netlist(*result.region, o, frozen_work, std::array{0U, 0U});
    ASSERT_TRUE(frozen.netlist) << frozen.reason;
    size_t local_cells = 0;
    for (const auto& endpoint : result.region->endpoints) {
      local_cells += endpoint.cells.size();
    }
    EXPECT_LT(frozen.netlist->cells.size(), local_cells);
    const auto expanded = expand_endpoint_netlist(*frozen.netlist, frozen_work);
    ASSERT_EQ(expanded.status, Status::feasible) << expanded.reason;
    for (uint32_t x = 0; x < (1U << r.graph.input_names().size()); ++x) {
      EXPECT_EQ(evaluate(expanded, x), evaluate(r, x));
    }
    Budget   replay_work{100000000};
    uint64_t samples               = 0;
    replay_work.admission_interval = 1;
    replay_work.admission          = [&] {
      ++samples;
      return true;
    };
    const auto replay = synthesize_logical_region(r, std::array{0U, 1U}, o, replay_work, std::array{0U, 0U});
    check(r, replay, 2);
    EXPECT_EQ(replay.report, result.report);
    EXPECT_EQ(replay_work.remaining, work.remaining);
    ASSERT_GT(samples, 1U);
    Budget   refused{100000000};
    uint64_t calls             = 0;
    refused.admission_interval = 1;
    refused.admission          = [&] { return ++calls < samples - 1; };
    const auto partial         = synthesize_logical_region(r, std::array{0U, 1U}, o, refused, std::array{0U, 0U});
    check(r, partial, 2);
    EXPECT_TRUE(refused.resource_exhausted);
    EXPECT_EQ(partial.region->cost, result.region->cost);
    EXPECT_EQ(partial.report.work.total(), 100000000 - refused.remaining);
  }
}

TEST(LogicalRegion, JointCareCompletionImprovesBothOrOneEndpointAfterTheBaselineTrajectory) {
  // Seed 30 changes both tops and drops a redundant producer from the first.
  // Seed 87 improves only one endpoint while retaining the original partner.
  // Both include a third output, whose cost and function remain protected.
  for (uint32_t seed : {30U, 87U}) {
    SCOPED_TRACE(seed);
    auto            r = pair_choice_fixture(seed);
    Logical_options o;
    o.endpoint.gates           = {3, 3, 4};
    o.endpoint.clock_phases    = 2;
    o.endpoint.cost.static_xor = 10;
    o.endpoint.fast_accept     = false;
    o.endpoint.boundaries      = 4;
    o.optimize_residual        = false;
    // Admit the complete preceding pair trajectory, stopping at the next tier.
    o.pair_trials              = seed == 30 ? 2 : 1;
    Budget     baseline_work{100000000};
    const auto baseline = synthesize_logical_region(r, std::array{0U, 1U}, o, baseline_work, std::array{0U, 0U});
    check(r, baseline, 2);
    ASSERT_TRUE(baseline.region);
    EXPECT_EQ(baseline.region->cost.total(), seed == 30 ? 40U : 63U);
    EXPECT_EQ(baseline.report.pairs.joint_care_windows, 0U);
    o.pair_trials = 64;
    Budget     work{100000000};
    const auto result = synthesize_logical_region(r, std::array{0U, 1U}, o, work, std::array{0U, 0U});
    check(r, result, 2);
    ASSERT_TRUE(result.region);
    EXPECT_EQ(result.region->cost.total(), seed == 30 ? 37U : 48U);
    EXPECT_EQ(result.report.pairs.joint_care_wins, 1U);
    EXPECT_GT(result.report.pairs.joint_care_phases, 0U);
    EXPECT_GT(result.report.pairs.joint_care_retained, 0U);
    EXPECT_GT(result.report.pairs.joint_care_combinations, 0U);
    EXPECT_LE(result.report.pairs.joint_care_bytes, o.endpoint.boundary_bytes);
    EXPECT_LE(result.report.pairs.joint_care_partitions, o.endpoint.divisor_partitions * result.report.pairs.joint_care_windows);
    EXPECT_LE(result.report.pairs.trials, o.pair_trials);
    EXPECT_LE(result.report.pairs.work, o.pair_work);
    EXPECT_EQ(result.report.work.total(), 100000000 - work.remaining);
    EXPECT_EQ(result.region->endpoints[0].origin, "pair-joint-care");
    EXPECT_EQ(result.region->endpoints[1].origin, seed == 30 ? "pair-joint-care" : baseline.region->endpoints[1].origin);
    EXPECT_EQ(result.region->endpoints[0].cells.size(), 2U);
    Budget     frozen_work{1000000};
    const auto frozen = freeze_endpoint_netlist(*result.region, o, frozen_work, std::array{0U, 0U});
    ASSERT_TRUE(frozen.netlist) << frozen.reason;
    if (seed == 30) {
      EXPECT_LT(frozen.netlist->cells.size(), result.region->endpoints[0].cells.size() + result.region->endpoints[1].cells.size());
    }
    const auto expanded = expand_endpoint_netlist(*frozen.netlist, frozen_work);
    ASSERT_EQ(expanded.status, Status::feasible) << expanded.reason;
    for (uint32_t x = 0; x < (1U << r.graph.input_names().size()); ++x) {
      EXPECT_EQ(evaluate(expanded, x), evaluate(r, x));
    }
    Budget   replay_work{100000000};
    uint64_t samples               = 0;
    replay_work.admission_interval = 1;
    replay_work.admission          = [&] {
      ++samples;
      return true;
    };
    const auto replay = synthesize_logical_region(r, std::array{0U, 1U}, o, replay_work, std::array{0U, 0U});
    check(r, replay, 2);
    EXPECT_EQ(replay.report, result.report);
    EXPECT_EQ(replay_work.remaining, work.remaining);
    ASSERT_GT(samples, 1U);
    Budget   cancelled{100000000};
    uint64_t calls               = 0;
    cancelled.admission_interval = 1;
    cancelled.admission          = [&] { return ++calls < samples - 1; };
    const auto partial           = synthesize_logical_region(r, std::array{0U, 1U}, o, cancelled, std::array{0U, 0U});
    check(r, partial, 2);
    EXPECT_TRUE(cancelled.resource_exhausted);
    EXPECT_EQ(partial.region->cost, result.region->cost);
    EXPECT_EQ(partial.report.work.total(), 100000000 - cancelled.remaining);
  }
}
TEST(LogicalRegion, AlternateJointEncodingsImproveBeyondTheCompleteCareTrajectory) {
  // 40 improves a care-completed endpoint; 47 changes just one endpoint;
  // 183 changes both while preserving a separate outside output.
  for (uint32_t seed : {40U, 47U, 183U}) {
    SCOPED_TRACE(seed);
    auto            r = pair_choice_fixture(seed);
    Logical_options o;
    o.endpoint.gates           = {3, 3, 4};
    o.endpoint.cost.static_xor = 10;
    o.endpoint.fast_accept     = false;
    o.endpoint.boundaries      = 4;
    o.optimize_residual        = false;
    o.pair_trials              = seed == 183 ? 4 : 3;
    Budget     baseline_work{100000000};
    const auto baseline = synthesize_logical_region(r, std::array{0U, 1U}, o, baseline_work, std::array{0U, 0U});
    check(r, baseline, 2);
    ASSERT_TRUE(baseline.region);
    EXPECT_EQ(baseline.region->cost.total(), seed == 40 ? 33U : seed == 47 ? 83U : 46U);
    EXPECT_EQ(baseline.report.pairs.joint_recode_windows, 0U);
    if (seed == 40) {
      EXPECT_EQ(baseline.report.pairs.joint_care_wins, 1U);
    }
    o.pair_trials = 64;
    Budget     work{100000000};
    const auto result = synthesize_logical_region(r, std::array{0U, 1U}, o, work, std::array{0U, 0U});
    check(r, result, 2);
    ASSERT_TRUE(result.region);
    EXPECT_EQ(result.region->cost.total(), seed == 40 ? 32U : seed == 47 ? 50U : 44U);
    EXPECT_EQ(result.report.pairs.joint_recode_wins, 1U);
    EXPECT_GT(result.report.pairs.joint_recode_retained, 0U);
    EXPECT_GT(result.report.pairs.joint_recode_combinations, 0U);
    EXPECT_LE(result.report.pairs.joint_recode_bytes, o.endpoint.boundary_bytes);
    EXPECT_LE(result.report.pairs.joint_recode_partitions,
              o.endpoint.divisor_partitions * result.report.pairs.joint_recode_windows);
    EXPECT_LE(result.report.pairs.joint_recode_encodings, o.pair_choices * result.report.pairs.joint_recode_partitions);
    EXPECT_LE(result.report.pairs.joint_recode_retained, 2 * (o.pair_choices + 1) * result.report.pairs.joint_recode_encodings);
    EXPECT_LE(result.report.pairs.joint_recode_combinations,
              ((o.pair_choices + 2) * (o.pair_choices + 2) - 1) * result.report.pairs.joint_recode_encodings);
    EXPECT_LE(result.report.pairs.trials, o.pair_trials);
    EXPECT_LE(result.report.pairs.work, o.pair_work);
    EXPECT_EQ(result.report.work.total(), 100000000 - work.remaining);
    EXPECT_EQ(result.region->endpoints[0].origin, seed == 40 ? "pair-joint-recode-care" : "pair-joint-recode");
    EXPECT_EQ(result.region->endpoints[1].origin, seed == 183 ? "pair-joint-recode" : baseline.region->endpoints[1].origin);
    Budget     frozen_work{1000000};
    const auto frozen = freeze_endpoint_netlist(*result.region, o, frozen_work, std::array{0U, 0U});
    ASSERT_TRUE(frozen.netlist) << frozen.reason;
    if (seed == 183) {
      EXPECT_LT(frozen.netlist->cells.size(), result.region->endpoints[0].cells.size() + result.region->endpoints[1].cells.size());
    }
    const auto expanded = expand_endpoint_netlist(*frozen.netlist, frozen_work);
    ASSERT_EQ(expanded.status, Status::feasible) << expanded.reason;
    for (uint32_t x = 0; x < (1U << r.graph.input_names().size()); ++x) {
      EXPECT_EQ(evaluate(expanded, x), evaluate(r, x));
    }
    Budget   replay_work{100000000};
    uint64_t samples               = 0;
    replay_work.admission_interval = 1;
    replay_work.admission          = [&] {
      ++samples;
      return true;
    };
    const auto replay = synthesize_logical_region(r, std::array{0U, 1U}, o, replay_work, std::array{0U, 0U});
    check(r, replay, 2);
    EXPECT_EQ(replay.report, result.report);
    EXPECT_EQ(replay_work.remaining, work.remaining);
    ASSERT_GT(samples, 1U);
    Budget   cancelled{100000000};
    uint64_t calls               = 0;
    cancelled.admission_interval = 1;
    cancelled.admission          = [&] { return ++calls < samples - 1; };
    const auto partial           = synthesize_logical_region(r, std::array{0U, 1U}, o, cancelled, std::array{0U, 0U});
    check(r, partial, 2);
    EXPECT_TRUE(cancelled.resource_exhausted);
    EXPECT_EQ(partial.region->cost, result.region->cost);
    EXPECT_EQ(partial.report.work.total(), 100000000 - cancelled.remaining);
  }
}

namespace {
// Every decision and the complete expansion of a logical selection: two runs
// that agree here and in their reports publish identical artifacts.
std::string describe(const Logical_result& r) {
  std::string out = std::string(status_name(r.status)) + ' ' + r.reason + '\n';
  if (!r.region) {
    return out;
  }
  const auto& region  = *r.region;
  const auto  signal  = [](Xsignal s) { return std::to_string(s.id) + (s.inverted ? "~" : ""); };
  out                += std::to_string(region.cost.static_logic) + '/' + std::to_string(region.cost.inverters) + '/'
                        + std::to_string(region.cost.domino) + '\n';
  for (const auto& e : region.endpoints) {
    out += std::to_string(e.state_index) + ' ' + e.name + ' ' + e.origin + (e.whole_cone ? " w" : " p");
    for (const auto& c : e.cells) {
      out += " |" + std::to_string(c.phase) + (c.latch ? "L" : "") + ':';
      for (size_t i = 0; i < c.inputs.size(); ++i) {
        out += signal(c.inputs[i]) + '@' + std::to_string(c.producers[i]) + ',';
      }
      for (const auto& n : c.formula.nodes) {
        out += std::to_string(static_cast<int>(n.kind)) + '.' + std::to_string(n.left) + '.' + std::to_string(n.right) + '.'
               + std::to_string(n.variable) + (n.inverted ? "~" : "") + ';';
      }
      out += c.formula.output_inverted ? "!" : "";
    }
    out += '\n';
  }
  const auto& g = region.logic.graph;
  for (Id id = 1; id < g.size(); ++id) {
    const auto& n = g.node(id);
    out += std::to_string(static_cast<int>(n.kind))
           + (n.kind == Xag::Kind::source ? std::to_string(n.source_index) : signal(n.inputs[0]) + ',' + signal(n.inputs[1])) + ' ';
  }
  for (const auto& s : region.logic.state) {
    out += "\n" + s.name + '=' + signal(s.d);
  }
  for (const auto& po : region.logic.outputs) {
    out += "\n" + po.name + '=' + signal(po.signal);
  }
  return out;
}
}  // namespace

// Credit floor tracking (unate.hpp) through a whole region: any credits at or
// above an unbound selection's recorded floor replay it identically -- the
// same decisions, residual network, report (including every stage's work)
// and consumed work -- and record the same floor again. Includes pair,
// joint, residual and feedback stages on generated reconvergent networks.
TEST(LogicalRegion, SelectionsReplayIdenticallyAtTheirCreditFloorAndAbove) {
  uint32_t   unbound = 0, bound = 0, below = 0, exhausted = 0;
  const auto replay_all
      = [&](const Xag_region& r, std::span<const uint32_t> eligible, const Logical_options& o, std::span<const uint32_t> domains) {
          for (const uint64_t credits : {uint64_t{400000000}, uint64_t{20000000}, uint64_t{1000000}, uint64_t{60000}}) {
            Budget     work{credits};
            const auto cold     = synthesize_logical_region(r, eligible, o, work, domains);
            const auto recorded = work.credit_floor();
            ASSERT_EQ(recorded.work, credits - work.remaining);
            ASSERT_EQ(recorded.work, cold.report.work.total());
            ASSERT_LE(recorded.work, recorded.floor);
            ASSERT_LE(recorded.floor, credits);
            exhausted += cold.report.exhausted;
            if (recorded.bound) {
              ++bound;
              continue;  // replays only under exactly these credits
            }
            ++unbound;
            below += recorded.floor < credits;
            for (const auto other : {recorded.floor, recorded.floor + 1, 2 * credits}) {
              Budget     again{other};
              const auto replay = synthesize_logical_region(r, eligible, o, again, domains);
              ASSERT_EQ(describe(replay), describe(cold)) << credits << " -> " << other;
              ASSERT_EQ(replay.report, cold.report) << credits << " -> " << other;
              ASSERT_EQ(again.credit_floor(), recorded) << credits << " -> " << other;
            }
          }
        };
  for (uint32_t seed : {26U, 30U, 35U, 49U, 87U, 156U}) {
    SCOPED_TRACE(seed);
    auto            r = pair_choice_fixture(seed);
    Logical_options o;
    o.endpoint.gates        = {3, 3, 4};
    o.endpoint.clock_phases = seed % 2 ? 2 : 1;
    o.endpoint.fast_accept  = false;
    o.endpoint.boundaries   = 4;
    o.endpoint_work         = 4000000;
    o.pair_work             = 4000000;
    replay_all(r, std::array{0U, 1U}, o, std::array{0U, 0U});
  }
  uint32_t   random = 391;
  const auto next   = [&] {
    random = random * 1664525U + 1013904223U;
    return random;
  };
  for (uint32_t trial = 0; trial < 4; ++trial) {
    SCOPED_TRACE(trial);
    Xag_region           r;
    std::vector<Xsignal> pool;
    for (uint32_t i = 0; i < 4; ++i) {
      pool.push_back(input(r, std::to_string(i)));
    }
    for (uint32_t i = 0; i < 3; ++i) {
      state(r, "state" + std::to_string(i), {}, i == 0 ? '0' : 'x');
      pool.push_back(r.state.back().q);
    }
    for (uint32_t i = 0; i < 18; ++i) {
      auto a = pool[next() % pool.size()], b = pool[next() % pool.size()];
      a = (next() & 2U) ? ~a : a;
      b = (next() & 4U) ? ~b : b;
      pool.push_back((next() & 8U) ? r.graph.land(a, b) : r.graph.lxor(a, b));
    }
    for (uint32_t i = 0; i < 3; ++i) {
      r.state[i].d = pool[pool.size() - 1 - i];
      r.outputs.push_back({"out" + std::to_string(i), ~pool[pool.size() - 4 - i]});
    }
    r.status = Status::feasible;
    Logical_options o;
    o.endpoint.gates         = {2, 2, 2};
    o.endpoint.clock_phases  = 1 + (trial % 2);
    o.endpoint.window.inputs = 4;
    o.endpoint_work          = 500000;
    o.residual.window_work   = 10000;
    replay_all(r, std::array<uint32_t, 3>{2, 0, 1}, o, std::array{0U, 0U, 0U});
  }
  // Plentiful credits leave selections unbound with a floor below them; the
  // starved runs exercise exhaustion and binding.
  EXPECT_GT(unbound, 12U);
  EXPECT_GT(below, 10U);
  EXPECT_GT(bound, 0U);
  EXPECT_GT(exhausted, 0U);
}
// Two ledgers: the structural one pays admission, validation and the identity
// baseline (report.work.admission) the same way under any search credits,
// unless a searched selection must be priced or rebuilt structurally (see the
// tests below); the search pays everything else. However few search credits
// remain, the region publishes a legal selection -- with none, exactly the
// identity baseline -- and the search's credit floor never includes
// structural work, so an unbound search replays identically at its floor.
// Only a structural refusal publishes nothing.
TEST(LogicalRegion, AStarvedSearchPublishesTheStructuralIdentityBaseline) {
  uint32_t complete = 0;
  for (uint32_t seed : {26U, 35U, 87U}) {
    SCOPED_TRACE(seed);
    const auto      r = pair_choice_fixture(seed);
    Logical_options o;
    o.endpoint.gates        = {3, 3, 4};
    o.endpoint.clock_phases = seed % 2 ? 2 : 1;
    o.endpoint_work         = 4000000;
    o.pair_work             = 4000000;
    const std::array        eligible{0U, 1U}, domains{0U, 0U};
    std::optional<uint64_t> admission;
    for (uint64_t credits = 0; credits < 400000; credits = credits * 5 / 4 + 1) {
      SCOPED_TRACE(credits);
      Budget     structural{100000000}, search{credits};
      const auto result = synthesize_logical_region(r, eligible, o, structural, search, domains);
      check(r, result, eligible.size());
      ASSERT_TRUE(result.region);
      // The structural charge is the same under every credit level.
      EXPECT_EQ(structural.consumed, result.report.work.admission);
      if (!admission) {
        admission = structural.consumed;
      }
      EXPECT_EQ(structural.consumed, *admission);
      EXPECT_FALSE(structural.bound);
      EXPECT_EQ(search.consumed, result.report.work.total() - result.report.work.admission);
      const auto& report = result.report;
      if (credits == 0) {
        EXPECT_EQ(search.consumed, 0U);
        EXPECT_EQ(report.identity_fallbacks, eligible.size());
        EXPECT_EQ(report.before, report.after);
        for (const auto& endpoint : result.region->endpoints) {
          EXPECT_EQ(endpoint.origin, "identity");
        }
      }
      complete += !report.identity_fallbacks;
      ASSERT_EQ(report.initial.size(), eligible.size());
      if (report.identity_fallbacks) {
        EXPECT_TRUE(report.exhausted);
      }
      const auto recorded = search.credit_floor();
      if (!recorded.bound) {
        Budget     other_structural{100000000}, again{recorded.floor};
        const auto replay = synthesize_logical_region(r, eligible, o, other_structural, again, domains);
        ASSERT_EQ(describe(replay), describe(result));
        ASSERT_EQ(replay.report, result.report);
        EXPECT_EQ(again.credit_floor(), recorded);
        EXPECT_EQ(other_structural.consumed, structural.consumed);
      }
    }
    // One credit short of the structural part refuses the region, before any search.
    Budget     structural{*admission - 1}, search{400000000};
    const auto refused = synthesize_logical_region(r, eligible, o, structural, search, domains);
    EXPECT_FALSE(refused.region);
    EXPECT_EQ(refused.status, Status::search_exhausted);
    EXPECT_EQ(search.consumed, 0U);
  }
  EXPECT_GT(complete, 0U);
}

// An endpoint whose own search cannot finish keeps its identity while the
// other endpoints of the region are still searched and priced: the first of
// three alike endpoints gets a fifth of the search credits, the next a
// quarter of what is left.
TEST(LogicalRegion, AnEndpointShortOfSearchKeepsItsIdentityWhileOthersAreSearched) {
  Xag_region r;
  for (uint32_t i = 0; i < 6; ++i) {
    input(r, "a" + std::to_string(i));
  }
  std::vector<uint32_t> eligible;
  for (uint32_t k = 0; k < 3; ++k) {
    const auto a = r.inputs[2 * k], b = r.inputs[2 * k + 1], c = r.inputs[(2 * k + 2) % 6];
    eligible.push_back(state(r, "s" + std::to_string(k), r.graph.lxor(r.graph.land(a, b), r.graph.land(b, ~c))));
  }
  for (const auto si : eligible) {
    r.outputs.push_back({r.state[si].name, r.state[si].q});
  }
  r.status = Status::feasible;
  Logical_options o;
  o.endpoint.gates        = {3, 3, 4};
  o.endpoint.clock_phases = 1;
  o.optimize_residual     = false;
  o.pair_candidates       = 0;
  uint32_t partial        = 0;
  for (uint64_t credits = 1; credits < 3000; ++credits) {
    Budget     structural{100000000}, search{credits};
    const auto result    = synthesize_logical_region(r, eligible, o, structural, search);
    const auto fallbacks = result.report.identity_fallbacks;
    if (!fallbacks || fallbacks == eligible.size()) {
      continue;
    }
    ++partial;
    check(r, result, eligible.size());
    size_t identities = 0;
    for (const auto& endpoint : result.region->endpoints) {
      identities += endpoint.origin == "identity";
    }
    EXPECT_GE(identities, fallbacks) << credits;
    EXPECT_TRUE(result.report.exhausted);
    EXPECT_GT(result.report.work.selection, 0U);
  }
  EXPECT_GT(partial, 0U);
}

namespace {
// Three alike endpoints: the first gets a fifth of the search credits, the
// next a quarter of what is left.
Xag_region alike_endpoints(std::vector<uint32_t>& eligible) {
  Xag_region r;
  for (uint32_t i = 0; i < 6; ++i) {
    input(r, "a" + std::to_string(i));
  }
  for (uint32_t k = 0; k < 3; ++k) {
    const auto a = r.inputs[2 * k], b = r.inputs[2 * k + 1], c = r.inputs[(2 * k + 2) % 6];
    eligible.push_back(state(r, "s" + std::to_string(k), r.graph.lxor(r.graph.land(a, b), r.graph.land(b, ~c))));
  }
  for (const auto si : eligible) {
    r.outputs.push_back({r.state[si].name, r.state[si].q});
  }
  r.status = Status::feasible;
  return r;
}
}  // namespace

// A search that stops while pricing its selection keeps its searched
// endpoints. In this credit sweep the searches that ran out while pricing had
// only kept identities (the loop leaves at least twice the last endpoint's
// share for pricing, so a searched cell is usually affordable to price), so
// the selection is the already priced baseline: no copy and no structural
// charge, and one budget for both ledgers publishes it too. A search stopped
// with a searched cell (here by its process admission) has the selection
// priced on the structural ledger, adding that pricing to the structural
// charge (report.work.admission); identical runs replay it, and a structural
// ledger one credit short refuses the region.
TEST(LogicalRegion, ASearchStoppedWhilePricingKeepsItsSearchedEndpoints) {
  std::vector<uint32_t> eligible;
  const auto            r = alike_endpoints(eligible);
  Logical_options       o;
  o.endpoint.gates        = {3, 3, 4};
  o.endpoint.clock_phases = 1;
  o.optimize_residual     = false;  // only the loop and pricing spend on the search ledger itself
  o.pair_candidates       = 0;
  Budget     baseline_structural{100000000}, nothing{0};
  const auto baseline = synthesize_logical_region(r, eligible, o, baseline_structural, nothing);
  ASSERT_TRUE(baseline.region);
  const auto structural_baseline = baseline_structural.consumed;
  uint32_t   priced_out          = 0;
  for (uint64_t credits = 1; credits < 3000; ++credits) {
    SCOPED_TRACE(credits);
    Budget     structural{100000000}, search{credits};
    const auto result = synthesize_logical_region(r, eligible, o, structural, search);
    ASSERT_TRUE(result.region);
    EXPECT_EQ(structural.consumed, structural_baseline);
    EXPECT_EQ(result.report.work.admission, structural_baseline);
    if (!search.exhausted || result.report.identity_fallbacks == eligible.size()) {
      continue;
    }
    ++priced_out;  // some endpoint was searched, then pricing ran out
    check(r, result, eligible.size());
    for (const auto& endpoint : result.region->endpoints) {
      EXPECT_EQ(endpoint.origin, "identity");
    }
    EXPECT_EQ(result.region->cost, baseline.report.before);
    Budget     one{structural_baseline + credits};
    const auto single = synthesize_logical_region(r, eligible, o, one);
    ASSERT_EQ(describe(single), describe(result));
    EXPECT_EQ(single.report, result.report);
  }
  EXPECT_GT(priced_out, 0U);

  // Count the search's admission samples in a complete run, then stop it at
  // a spread of them.
  uint64_t samples = 0;
  {
    Budget structural{100000000}, search{100000000};
    search.admission_interval = 1;
    search.admission          = [&] {
      ++samples;
      return true;
    };
    const auto complete = synthesize_logical_region(r, eligible, o, structural, search);
    ASSERT_TRUE(complete.region);
    ASSERT_EQ(structural.consumed, structural_baseline);
  }
  uint32_t retried = 0, kept = 0;
  for (uint64_t stop = 1; stop <= samples; stop += 1 + stop / 16) {
    SCOPED_TRACE(stop);
    const auto run = [&](Budget& structural) {
      Budget   search{100000000};
      uint64_t calls            = 0;
      search.admission_interval = 1;
      search.admission          = [&] { return ++calls != stop; };
      return synthesize_logical_region(r, eligible, o, structural, search);
    };
    Budget     structural{100000000};
    const auto result = run(structural);
    check(r, result, eligible.size());
    EXPECT_EQ(result.report.work.admission, structural.consumed);
    if (structural.consumed == structural_baseline) {
      continue;
    }
    ++retried;
    EXPECT_GT(structural.consumed, structural_baseline);
    EXPECT_TRUE(result.report.exhausted);
    EXPECT_LT(result.report.identity_fallbacks, eligible.size());
    kept += std::any_of(result.region->endpoints.begin(), result.region->endpoints.end(), [](const auto& e) {
      return e.origin != "identity";
    });
    EXPECT_EQ(result.region->cost, result.report.before);
    Budget     again{100000000};
    const auto replay = run(again);
    ASSERT_EQ(describe(replay), describe(result));
    ASSERT_EQ(replay.report, result.report);
    EXPECT_EQ(again.consumed, structural.consumed);
    Budget     one_short{structural.consumed - 1};
    const auto refused = run(one_short);
    EXPECT_FALSE(refused.region);
    EXPECT_EQ(refused.status, Status::search_exhausted);
  }
  EXPECT_GT(retried, 0U);
  EXPECT_GT(kept, 0U);
}

// A selection whose expansion exceeds max_nodes cannot be priced on either
// ledger: the region publishes the identity baseline, rebuilt from the source,
// and that copy is charged to the structural ledger like any copy. Later
// stages still run from the baseline; identity_fallbacks counts only the
// fallbacks still published as their identity, so a feedback win that
// replaces one leaves it out (every counted fallback is an identity endpoint,
// which a cache record checks).
TEST(LogicalRegion, ASelectionBeyondTheNodeLimitRebuildsTheBaselineOnTheStructuralLedger) {
  for (uint32_t seed : {26U, 49U}) {
    SCOPED_TRACE(seed);
    const auto      r = pair_choice_fixture(seed);
    Logical_options o;
    o.endpoint.gates        = {3, 3, 4};
    o.endpoint.clock_phases = seed % 2 ? 2 : 1;
    const std::array eligible{0U, 1U}, domains{0U, 0U};
    Budget           roomy_structural{100000000}, roomy_search{100000000}, zero_structural{100000000}, zero{0};
    const auto       roomy = synthesize_logical_region(r, eligible, o, roomy_structural, roomy_search, domains);
    const auto       none  = synthesize_logical_region(r, eligible, o, zero_structural, zero, domains);
    ASSERT_TRUE(roomy.region);
    ASSERT_TRUE(none.region);
    EXPECT_EQ(roomy_structural.consumed, zero_structural.consumed);
    // Identity endpoints append no nodes; the searched ones do not fit.
    o.max_nodes = r.graph.size() + 1;
    Budget     structural{100000000}, search{100000000};
    const auto result = synthesize_logical_region(r, eligible, o, structural, search, domains);
    check(r, result, eligible.size());
    // Each identity is one literal cell of one input.
    const uint64_t copy = r.graph.size() + r.inputs.size() + r.state.size() + r.outputs.size() + 2 * eligible.size();
    EXPECT_EQ(structural.consumed, zero_structural.consumed + copy);
    EXPECT_EQ(result.report.work.admission, structural.consumed);
    EXPECT_EQ(search.consumed, result.report.work.total() - result.report.work.admission);
    EXPECT_FALSE(search.exhausted);  // node limits, not credits, stopped the pricing
    EXPECT_TRUE(result.report.exhausted);
    EXPECT_EQ(result.report.before, none.report.before);
    const auto identities
        = static_cast<uint64_t>(std::count_if(result.region->endpoints.begin(), result.region->endpoints.end(), [](const auto& e) {
            return e.origin == "identity";
          }));
    EXPECT_EQ(result.report.identity_fallbacks, identities);
    if (seed == 26) {
      // Feedback replaced both fallbacks after the residual shrank the network.
      EXPECT_EQ(result.report.feedback_wins, 2U);
      EXPECT_EQ(identities, 0U);
    } else {
      EXPECT_EQ(identities, eligible.size());
    }
    Budget     again_structural{100000000}, again{100000000};
    const auto replay = synthesize_logical_region(r, eligible, o, again_structural, again, domains);
    ASSERT_EQ(describe(replay), describe(result));
    ASSERT_EQ(replay.report, result.report);
    EXPECT_EQ(again_structural.consumed, structural.consumed);
    Budget     short_structural{structural.consumed - 1}, plenty{100000000};
    const auto refused = synthesize_logical_region(r, eligible, o, short_structural, plenty, domains);
    EXPECT_FALSE(refused.region);
    EXPECT_EQ(refused.status, Status::search_exhausted);
  }
}
}  // namespace livehd::usyn
