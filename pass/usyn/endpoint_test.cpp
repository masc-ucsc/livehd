// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "endpoint.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <string>

#include "gtest/gtest.h"

namespace livehd::usyn {
namespace {
bool native_value(const Xag& g, Xsignal s, uint32_t assignment) {
  const auto& n     = g.node(s.id);
  bool        value = false;
  switch (n.kind) {
    case Xag::Kind::constant: break;
    case Xag::Kind::source  : value = (assignment >> n.source_index) & 1; break;
    case Xag::Kind::and_gate: value = native_value(g, n.inputs[0], assignment) && native_value(g, n.inputs[1], assignment); break;
    case Xag::Kind::xor_gate: value = native_value(g, n.inputs[0], assignment) != native_value(g, n.inputs[1], assignment); break;
  }
  return value != s.inverted;
}

// Compose cells with their actual upstream functions on original source
// assignments, rather than using the search's cut table as the oracle.
void check_composition(const Xag& g, const Endpoint_solution& solution, const Endpoint_options& options) {
  Budget work{10000000};
  ASSERT_TRUE(validate_endpoint(g, solution, options, work));
  ASSERT_LE(g.input_names().size(), 8U);
  for (uint32_t x = 0; x < (1U << g.input_names().size()); ++x) {
    std::vector<bool> values;
    for (const auto& cell : solution.cells) {
      uint32_t input = 0;
      for (size_t j = 0; j < cell.function->inputs.size(); ++j) {
        const auto producer  = cell.producers[j];
        const bool value     = producer < 0 ? native_value(g, {cell.function->inputs[j], false}, x) : values.at(producer);
        input               |= uint32_t{value} << j;
      }
      values.push_back(cell.function->formula.evaluate(input));
    }
    ASSERT_EQ(values.back(), native_value(g, solution.root, x)) << x;
  }
}

Endpoint_options small_gates() {
  Endpoint_options o;
  o.gates           = {2, 2, 2};
  // Deliberately favor absorption so these cases exercise decomposition,
  // without implying these weights predict characterized CMOS area.
  o.cost.static_and = 20;
  o.cost.static_xor = 20;
  return o;
}
}  // namespace

TEST(Endpoint, WholeConeExposesSharedInputFactoring) {
  Xag        g;
  const auto a = g.input("a"), b = g.input("b"), c = g.input("c");
  const auto root = g.land(g.lor(a, b), g.lor(a, c));
  Budget     work{1000000};
  const auto r = select_endpoint(g, root, "pipe.r[3]", {}, {}, {}, work);
  ASSERT_EQ(r.status, Status::feasible);
  ASSERT_TRUE(r.selected);
  EXPECT_EQ(r.selected->name, "pipe.r[3]");
  EXPECT_TRUE(r.selected->whole_cone);
  ASSERT_EQ(r.selected->cells.size(), 1U);
  EXPECT_TRUE(r.selected->cells.back().latch);
  EXPECT_EQ(r.selected->cells.back().function->metrics.transistors, 3U);
  EXPECT_EQ(r.report.one_cell_attempts, 1U);
  EXPECT_EQ(r.report.two_phase_attempts, 0U);
  EXPECT_EQ(r.report.boundaries, 0U);
  EXPECT_EQ(r.report.boundary_work, 0U);
  check_composition(g, *r.selected, {});
}

TEST(Endpoint, PinnedJointBoundaryCannotExpandIntoUpstreamLogic) {
  Xag              g;
  const auto       a = g.input("a"), b = g.input("b"), c = g.input("c"), unused = g.input("unused");
  const auto       ab = g.land(a, b), ac = g.land(a, c), root = g.land(ab, ac);
  Endpoint_options o;
  o.cost.static_and = 20;
  o.fast_accept     = false;
  o.window.inputs   = 2;
  const std::array basis{unused.id, ab.id, ac.id};  // common basis includes the other root's unused input
  Budget           work{1000000};
  const auto       result = select_endpoint(g, root, "q", {}, {}, o, work, basis);
  ASSERT_TRUE(result.selected);
  EXPECT_TRUE(result.report.window_used);
  EXPECT_FALSE(result.report.whole_admitted);
  EXPECT_FALSE(result.selected->whole_cone);
  ASSERT_EQ(result.selected->cells.size(), 1U);
  EXPECT_EQ(result.selected->cells.back().function->inputs, (std::vector<Id>{ab.id, ac.id}));
  EXPECT_EQ(result.selected->static_cost, 40U);
  check_composition(g, *result.selected, o);
  EXPECT_EQ(select_endpoint(g, root, "q", {}, {}, o, work, std::array{ab.id, ab.id}).status, Status::invalid);
  EXPECT_EQ(select_endpoint(g, root, "q", {}, {}, o, work, std::array{static_cast<Id>(g.size())}).status, Status::invalid);
}

TEST(Endpoint, TwoPhasesAllowMultipleParallelCells) {
  Xag        g;
  const auto a = g.input("a"), b = g.input("b"), c = g.input("c"), d = g.input("d");
  const auto root = g.land(g.land(a, b), g.land(c, d));
  const auto o    = small_gates();
  Budget     work{10000000};
  const auto r = select_endpoint(g, root, "q", {}, {}, o, work);
  ASSERT_TRUE(r.selected);
  EXPECT_TRUE(r.selected->whole_cone);
  ASSERT_EQ(r.selected->cells.size(), 3U);
  EXPECT_EQ(r.selected->cells[0].phase, 1U);
  EXPECT_EQ(r.selected->cells[1].phase, 1U);
  EXPECT_EQ(r.selected->cells[2].phase, 2U);
  EXPECT_EQ(r.selected->residual_nodes, 0U);
  check_composition(g, *r.selected, o);

  auto invalid                = *r.selected;
  invalid.cells.front().phase = 2;
  Budget validation{100000};
  EXPECT_FALSE(validate_endpoint(g, invalid, o, validation));
  invalid                           = *r.selected;
  invalid.cells.back().producers[0] = 2;  // self-dependence
  EXPECT_FALSE(validate_endpoint(g, invalid, o, validation));
  invalid                           = *r.selected;
  invalid.cells.back().producers[0] = -1;  // leaves one phase-1 cell unused
  EXPECT_FALSE(validate_endpoint(g, invalid, o, validation));
}

TEST(Endpoint, SynthesizesAbsentDivisorAndChecksCompositionOnOriginalInputs) {
  Xag        g;
  const auto a = g.input("a"), b = g.input("b"), c = g.input("c");
  const auto root            = g.lor(g.land(a, b), g.land(a, c));
  auto       options         = small_gates();
  options.divisor_partitions = 0;
  Budget     old_work{10000000};
  const auto structural = select_endpoint(g, root, "q", {}, {}, options, old_work);
  ASSERT_TRUE(structural.selected);
  ASSERT_EQ(structural.selected->cells.size(), 3U);
  options.divisor_partitions = 32;
  Budget     work{10000000};
  const auto result = select_endpoint(g, root, "q", {}, {}, options, work);
  ASSERT_TRUE(result.selected);
  ASSERT_EQ(result.selected->cells.size(), 2U);
  EXPECT_EQ(result.selected->origin, "functional-two-phase");
  EXPECT_TRUE(result.selected->whole_cone);
  EXPECT_FALSE(result.selected->cells.front().function->root);
  EXPECT_GT(result.report.new_divisor_attempts, 0U);
  EXPECT_EQ(result.report.boundaries, 0U);
  EXPECT_EQ(result.report.boundary_work, 0U);
  EXPECT_EQ(result.report.residual_attempts, 0U);
  EXPECT_EQ(result.report.analysis_tables, 1U);
  EXPECT_GT(result.report.analysis_hits, 0U);
  EXPECT_LT(result.selected->total_cost(), structural.selected->total_cost());
  check_composition(g, *result.selected, options);

  // A locally consistent formula/table pair must also compose to original D.
  auto corrupted                   = *result.selected;
  auto f                           = std::make_shared<Endpoint_function>(*corrupted.cells.front().function);
  f->table                         = f->table.complement();
  f->formula.output_inverted       = !f->formula.output_inverted;
  corrupted.cells.front().function = f;
  Budget validation{1000000};
  EXPECT_FALSE(validate_endpoint(g, corrupted, options, validation));
}

TEST(Endpoint, OnePhaseLeavesUpstreamLogicButReplacesTheRegister) {
  Xag        g;
  const auto a = g.input("a"), b = g.input("b"), c = g.input("c"), d = g.input("d");
  const auto root = g.land(g.land(a, b), g.land(c, d));
  auto       o    = small_gates();
  o.clock_phases  = 1;
  Budget     work{10000000};
  const auto r = select_endpoint(g, root, "original_q", {}, {}, o, work);
  ASSERT_TRUE(r.selected);
  ASSERT_EQ(r.selected->cells.size(), 1U);
  EXPECT_TRUE(r.selected->cells.back().latch);
  EXPECT_EQ(r.selected->cells.back().phase, 1U);
  EXPECT_EQ(r.selected->name, "original_q");
  EXPECT_FALSE(r.selected->whole_cone);
  EXPECT_EQ(r.selected->residual_nodes, 2U);
  EXPECT_EQ(r.report.two_phase_attempts, 0U);
  check_composition(g, *r.selected, o);
}

TEST(Endpoint, NewDivisorPartitionLimitRetainsLegalIncumbent) {
  Xag        g;
  const auto a = g.input("a"), b = g.input("b"), c = g.input("c");
  const auto root      = g.lor(g.land(a, b), g.land(a, c));
  auto       o         = small_gates();
  o.divisor_partitions = 1;
  o.fast_accept        = false;
  Budget     work{10000000};
  const auto result = select_endpoint(g, root, "q", {}, {}, o, work);
  ASSERT_TRUE(result.selected);
  EXPECT_EQ(result.report.new_divisor_attempts, 1U);
  EXPECT_TRUE(result.report.exhausted);
  EXPECT_NE(std::find(result.report.limits.begin(), result.report.limits.end(), "new-divisor partition candidate budget"),
            result.report.limits.end());
  check_composition(g, *result.selected, o);
}

TEST(Endpoint, AdmittedFunctionReuseRespectsCacheCapacityAndSavesWork) {
  Xag        g;
  const auto a = g.input("a"), b = g.input("b"), c = g.input("c");
  const auto root = g.lor(g.land(a, b), g.land(a, c));
  auto       o    = small_gates();
  Budget     cached_work{10000000};
  const auto cached = select_endpoint(g, root, "q", {}, {}, o, cached_work);
  ASSERT_TRUE(cached.selected);
  ASSERT_EQ(cached.selected->origin, "functional-two-phase");
  EXPECT_EQ(cached.report.analysis_tables, 1U);
  EXPECT_GT(cached.report.analysis_hits, 0U);
  EXPECT_GT(cached.report.analysis_cache_bytes, 0U);
  EXPECT_LE(cached.report.analysis_cache_bytes, o.gate_cache_bytes);
  // Exhausting either the entry or payload allowance only disables retention;
  // it must not turn a valid function into an infeasible cached gate result.
  for (bool no_entries : {false, true}) {
    SCOPED_TRACE(no_entries);
    auto limited = o;
    if (no_entries) {
      limited.gate_cache_entries = 0;
    } else {
      limited.gate_cache_bytes = 1;
    }
    Budget     uncached_work{10000000};
    const auto uncached = select_endpoint(g, root, "q", {}, {}, limited, uncached_work);
    ASSERT_TRUE(uncached.selected);
    EXPECT_EQ(uncached.selected->origin, cached.selected->origin);
    EXPECT_EQ(uncached.selected->total_cost(), cached.selected->total_cost());
    EXPECT_EQ(uncached.report.analysis_cache_bytes, 0U);
    EXPECT_EQ(uncached.report.analysis_hits, 0U);
    EXPECT_GT(uncached.report.analysis_tables, cached.report.analysis_tables);
    EXPECT_LT(uncached_work.remaining, cached_work.remaining);
    check_composition(g, *uncached.selected, limited);
  }
}

TEST(Endpoint, FactoringRefusalDoesNotPoisonReusableAnalysis) {
  Xag              g;
  const auto       a = g.input("a"), b = g.input("b"), c = g.input("c");
  const auto       root = g.land(g.lor(a, b), g.lor(a, c));
  Endpoint_options o;
  o.clock_phases    = 1;
  o.cost.static_and = 20;
  // Find a bounded allowance that can factor the cached table but cannot also
  // construct it in the same candidate slice. Avoid pinning internal work costs.
  bool recovered    = false;
  for (uint32_t allowance = 64; allowance <= 4096 && !recovered; allowance += 16) {
    o.candidate_work = allowance;
    Budget     work{1000000};
    const auto result = select_endpoint(g, root, "q", {}, {}, o, work);
    ASSERT_TRUE(result.selected);
    if (result.selected->origin != "residual" || !result.selected->whole_cone) {
      continue;
    }
    recovered = true;
    EXPECT_TRUE(result.report.exhausted);
    EXPECT_GT(result.report.analysis_hits, 0U);
    check_composition(g, *result.selected, o);
    auto uncached_options               = o;
    uncached_options.gate_cache_entries = 0;
    Budget     uncached_work{1000000};
    const auto uncached = select_endpoint(g, root, "q", {}, {}, uncached_options, uncached_work);
    ASSERT_TRUE(uncached.selected);
    EXPECT_FALSE(uncached.selected->whole_cone);
    check_composition(g, *uncached.selected, uncached_options);
  }
  EXPECT_TRUE(recovered);
}

TEST(Endpoint, TwoCellSearchPrecedesAnEarlierParallelPartition) {
  Xag        g;
  const auto a = g.input("a"), b = g.input("b"), c = g.input("c"), d = g.input("d");
  const auto root        = g.lor(g.land(a, g.land(b, d)), g.land(a, g.land(c, ~d)));
  auto       o           = small_gates();
  o.gates.logical_inputs = 3;
  Budget     work{10000000};
  const auto result = select_endpoint(g, root, "q", {}, {}, o, work);
  ASSERT_TRUE(result.selected);
  EXPECT_EQ(result.selected->origin, "functional-two-phase");
  // Mask 7 yields two first-phase functions; the later mask 14 yields a
  // single mux-like function. Fast acceptance must prefer that two-cell path.
  ASSERT_EQ(result.selected->cells.size(), 2U);
  EXPECT_TRUE(result.selected->whole_cone);
  EXPECT_GT(result.report.single_divisor_attempts, 0U);
  EXPECT_EQ(result.report.parallel_divisor_attempts, 0U);
  EXPECT_GT(result.report.deferred_divisor_bytes, 0U);
  EXPECT_LE(result.report.deferred_divisor_bytes, o.boundary_bytes);
  EXPECT_EQ(result.report.boundaries, 0U);
  check_composition(g, *result.selected, o);
}

TEST(Endpoint, ExistingTwoCellCandidatePrecedesQueuedParallelFunctionalCandidate) {
  Xag        g;
  const auto a = g.input("a"), b = g.input("b"), c = g.input("c"), d = g.input("d");
  const auto mux         = g.lor(g.land(b, d), g.land(c, ~d));
  const auto root        = g.land(a, mux);
  auto       o           = small_gates();
  o.gates.logical_inputs = 3;
  o.divisor_partitions   = 4;  // retain the earlier two-output functional encoding
  o.local_candidates     = 0;
  o.care_phases          = 0;
  o.boundaries           = 1;
  Budget     baseline_work{10000000};
  const auto baseline = select_endpoint(g, root, "q", {}, {}, o, baseline_work);
  ASSERT_TRUE(baseline.selected);
  ASSERT_EQ(baseline.selected->cells.size(), 3U);
  EXPECT_GT(baseline.report.parallel_divisor_attempts, 0U);
  check_composition(g, *baseline.selected, o);

  // With only two slots, the admitted window and root seed can evict the
  // mux boundary before the later structural-decomposition sweep. Price its
  // complete two-cell realization before pruning it.
  o.boundaries = 2;
  Budget     work{10000000};
  const auto result = select_endpoint(g, root, "q", {}, {}, o, work);
  ASSERT_TRUE(result.selected);
  ASSERT_EQ(result.selected->cells.size(), 2U);
  EXPECT_EQ(result.selected->origin, "two-phase");
  EXPECT_TRUE(result.selected->whole_cone);
  EXPECT_EQ(result.selected->cells.front().function->root, (Xsignal{mux.id, false}));
  EXPECT_GT(result.report.deferred_divisor_bytes, 0U);
  EXPECT_LE(result.report.deferred_divisor_bytes, o.boundary_bytes / 2);
  EXPECT_EQ(result.report.parallel_divisor_attempts, 0U);
  EXPECT_GT(result.report.boundary_work, 0U);
  EXPECT_GT(result.report.boundary_two_cell_work, 0U);
  EXPECT_GT(result.report.boundary_wins, 0U);
  EXPECT_EQ(result.report.boundary_multi_cell_work, 0U);  // competitive two-cell fast path
  EXPECT_LE(result.report.boundary_two_cell_work, result.report.boundary_work);
  EXPECT_LE(result.report.boundary_two_cell_work, o.tier_work / 8);
  EXPECT_LE(result.report.two_phase_work + result.report.boundary_two_cell_work, o.tier_work);
  EXPECT_LT(result.selected->total_cost(), baseline.selected->total_cost());
  check_composition(g, *result.selected, o);

  Budget   measured{10000000};
  uint64_t samples            = 0;
  measured.admission_interval = 1;
  measured.admission          = [&] {
    ++samples;
    return true;
  };
  const auto replay = select_endpoint(g, root, "q", {}, {}, o, measured);
  ASSERT_TRUE(replay.selected);
  ASSERT_GT(samples, 1U);
  EXPECT_EQ(replay.report, result.report);
  EXPECT_EQ(measured.remaining, work.remaining);

  Budget   refused{10000000};
  uint64_t calls             = 0;
  refused.admission_interval = 1;
  refused.admission          = [&] { return ++calls != samples - 1; };
  const auto interrupted     = select_endpoint(g, root, "q", {}, {}, o, refused);
  ASSERT_TRUE(interrupted.selected);
  EXPECT_TRUE(refused.resource_exhausted);
  EXPECT_EQ(calls, samples - 1);
  EXPECT_EQ(interrupted.selected->total_cost(), result.selected->total_cost());
  EXPECT_EQ(interrupted.selected->cells.size(), 2U);
  check_composition(g, *interrupted.selected, o);

  // The same mux has no deletion credit when another output still reads it.
  // Joint ranking must retain that static cone in its cost ledger and prefer
  // the cheaper single endpoint reusing the existing mux value.
  Budget           shared_work{10000000};
  const std::array protected_roots{mux};
  const auto       shared = select_endpoint(g, root, "q", protected_roots, {}, o, shared_work);
  ASSERT_TRUE(shared.selected);
  EXPECT_EQ(shared.selected->cells.size(), 1U);
  EXPECT_FALSE(shared.selected->whole_cone);
  EXPECT_EQ(shared.selected->cells.back().function->inputs, (std::vector<Id>{a.id, mux.id}));
  EXPECT_GT(shared.report.boundary_two_cell_work, 0U);
  check_composition(g, *shared.selected, o);
}

TEST(Endpoint, MultipleNewDivisorsUseDistinctProducerBindings) {
  Xag        g;
  const auto a = g.input("a"), b = g.input("b"), c = g.input("c"), d = g.input("d");
  // Neither a&b nor a&c occurs as an existing node. The bound group {a,b,c}
  // has four cofactors over d, requiring two parallel synthesized outputs.
  const auto root        = g.lor(g.land(a, g.land(b, d)), g.land(a, g.land(c, ~d)));
  auto       o           = small_gates();
  o.gates.logical_inputs = 3;
  // Admit masks 3, 5, 6 and 7. A later partition has a two-cell solution,
  // but this bounded search must still realize its queued parallel candidate.
  o.divisor_partitions   = 4;
  Budget     work{10000000};
  const auto result = select_endpoint(g, root, "q", {}, {}, o, work);
  ASSERT_TRUE(result.selected);
  ASSERT_EQ(result.selected->origin, "functional-two-phase");
  ASSERT_EQ(result.selected->cells.size(), 3U);
  EXPECT_EQ(result.report.new_divisor_attempts, 4U);
  EXPECT_GT(result.report.parallel_divisor_attempts, 0U);
  EXPECT_GT(result.report.deferred_divisor_bytes, 0U);
  EXPECT_LE(result.report.deferred_divisor_bytes, o.boundary_bytes);
  EXPECT_FALSE(result.selected->cells[0].function->root);
  EXPECT_FALSE(result.selected->cells[1].function->root);
  EXPECT_EQ(result.selected->cells[0].phase, 1U);
  EXPECT_EQ(result.selected->cells[1].phase, 1U);
  const auto& top = result.selected->cells.back();
  ASSERT_EQ(top.producers.size(), 3U);
  EXPECT_EQ(top.producers[0], 0);
  EXPECT_EQ(top.producers[1], 1);
  EXPECT_EQ(top.function->inputs[0], 0U);
  EXPECT_EQ(top.function->inputs[1], 0U);
  check_composition(g, *result.selected, o);
  auto corrupted                      = *result.selected;
  corrupted.cells.back().producers[1] = 0;
  Budget validation{1000000};
  EXPECT_FALSE(validate_endpoint(g, corrupted, o, validation));

  o.boundary_bytes = 1;
  Budget     limited{10000000};
  const auto refused = select_endpoint(g, root, "q", {}, {}, o, limited);
  ASSERT_TRUE(refused.selected);
  EXPECT_EQ(refused.report.deferred_divisor_bytes, 0U);
  EXPECT_EQ(refused.report.parallel_divisor_attempts, 0U);
  EXPECT_NE(std::find(refused.report.limits.begin(), refused.report.limits.end(), "pending decomposition memory budget"),
            refused.report.limits.end());
  check_composition(g, *refused.selected, o);
}

TEST(Endpoint, UnreachableDivisorCodesRescueAConstrainedTopGate) {
  Xag        g;
  const auto a = g.input("a"), b = g.input("b"), c = g.input("c"), d = g.input("d");
  const auto root        = g.lor(g.land(a, b), g.land(g.lor(a, c), d));
  auto       o           = small_gates();
  o.gates.logical_inputs = 3;
  o.divisor_partitions   = 4;  // includes bound group {a,b,c}, with three cofactors over d
  o.boundaries           = 1;  // isolate functional decomposition from structural alternatives
  o.local_candidates     = 0;  // isolate completion from the separate local divisor search
  o.care_phases          = 0;
  Budget     baseline_work{10000000};
  const auto baseline = select_endpoint(g, root, "q", {}, {}, o, baseline_work);
  ASSERT_TRUE(baseline.selected);
  EXPECT_EQ(baseline.report.completion_attempts, 0U);
  EXPECT_FALSE(baseline.selected->whole_cone);
  o.care_phases = 16;
  Budget     work{10000000};
  const auto result = select_endpoint(g, root, "q", {}, {}, o, work);
  ASSERT_TRUE(result.selected);
  EXPECT_EQ(result.selected->origin, "functional-two-phase");
  EXPECT_TRUE(result.selected->whole_cone);
  EXPECT_GT(result.report.completion_attempts, 0U);
  EXPECT_GT(result.report.completion_phases, 0U);
  EXPECT_LT(result.selected->total_cost(), baseline.selected->total_cost());
  check_composition(g, *result.selected, o);
}

TEST(Endpoint, CandidatePricingDoesNotCopyProtectedConeAndTracksNewPolarity) {
  Xag  g;
  auto protected_root = g.input("p0");
  for (uint32_t i = 1; i <= 100; ++i) {
    protected_root = g.land(protected_root, g.input("p" + std::to_string(i)));
  }
  const auto       a = g.input("a");
  Endpoint_options o;
  o.candidate_work = 16;  // less than the protected cone, enough for an identity
  for (bool protected_inverse : {false, true}) {
    for (bool free_inverse : {false, true}) {
      const std::array      readers{protected_root, protected_inverse ? ~a : a};
      const std::vector<Id> rails = free_inverse ? std::vector<Id>{a.id} : std::vector<Id>{};
      Budget                work{100000};
      const auto            result = select_endpoint(g, ~a, "q", readers, rails, o, work);
      ASSERT_TRUE(result.selected);
      EXPECT_EQ(result.selected->static_cost, 100U * o.cost.static_and);
      EXPECT_EQ(result.selected->residual_nodes, 0U);
      // The identity endpoint reads `a` and selects its own free !Q rail. Only a
      // protected outside reader of ~a still needs a static NOT, unless `a`'s
      // opposite rail is already free.
      EXPECT_EQ(result.selected->inverter_cost, protected_inverse && !free_inverse ? o.cost.static_not : 0U);
      Budget validation{1000};
      EXPECT_TRUE(validate_endpoint(g, *result.selected, o, validation));
    }
  }
}

TEST(Endpoint, OutsideReadersRetainSharedLogicInCost) {
  Xag              g;
  const auto       a = g.input("a"), b = g.input("b"), c = g.input("c");
  const auto       shared = g.land(a, b), root = g.land(shared, c);
  const std::array readers{shared, shared};
  Endpoint_options o;
  o.cost.static_and = 20;
  Budget     work{10000000};
  const auto r = select_endpoint(g, root, "q", readers, {}, o, work);
  ASSERT_TRUE(r.selected);
  EXPECT_EQ(r.selected->static_cost, 20U);  // one retained gate, not two readers
  EXPECT_EQ(r.selected->residual_nodes, 0U);
  EXPECT_GT(r.report.boundary_work, 0U);  // outside sharing prevents blind fast acceptance
  check_composition(g, *r.selected, o);
}

TEST(Endpoint, ExistingDivisorCarePreservesCorrelatedStaticAndDominoInputs) {
  Xag        g;
  const auto a = g.input("a"), b = g.input("b"), c = g.input("c");
  const auto ab = g.land(a, b), nc = g.land(~a, c);
  const auto root      = g.lxor(ab, nc);
  auto       o         = small_gates();
  o.gates.stack        = 1;  // OR fits; XOR on independent ports does not
  o.divisor_partitions = 0;
  for (uint32_t phases : {1U, 2U}) {
    SCOPED_TRACE(phases);
    o.clock_phases = phases;
    o.care_phases  = 0;
    Budget     baseline_work{10000000};
    const auto baseline = select_endpoint(g, root, "q", {}, {}, o, baseline_work);
    ASSERT_TRUE(baseline.selected);
    o.care_phases = 16;
    Budget     work{10000000};
    const auto result = select_endpoint(g, root, "q", {}, {}, o, work);
    ASSERT_TRUE(result.selected);
    EXPECT_EQ(result.selected->origin, phases == 1 ? "existing-care-residual" : "existing-care-two-phase");
    EXPECT_GT(result.report.existing_care_images, 0U);
    EXPECT_LT(result.selected->total_cost(), baseline.selected->total_cost());
    EXPECT_EQ(result.selected->whole_cone, phases == 2);
    EXPECT_EQ(result.selected->cells.size(), phases == 1 ? 1U : 3U);
    check_composition(g, *result.selected, o);
    if (phases == 1) {
      auto corrupted                  = *result.selected;
      auto f                          = std::make_shared<Endpoint_function>(*corrupted.cells.back().function);
      f->table                        = f->table.complement();
      f->formula.output_inverted      = !f->formula.output_inverted;
      corrupted.cells.back().function = f;
      Budget validation{1000000};
      EXPECT_FALSE(validate_endpoint(g, corrupted, o, validation));

      // This fanout computes F itself, so substituting it for either OR input
      // still computes F. It is nevertheless outside the admitted cone and
      // must not become an endpoint dependency.
      const auto outside              = g.land(root, g.lor(ab, nc));
      corrupted                       = *result.selected;
      f                               = std::make_shared<Endpoint_function>(*corrupted.cells.back().function);
      f->inputs[0]                    = outside.id;
      corrupted.cells.back().function = f;
      for (uint32_t x = 0; x < 8; ++x) {
        uint32_t assignment = 0;
        for (uint32_t j = 0; j < f->inputs.size(); ++j) {
          assignment |= uint32_t{native_value(g, {f->inputs[j], false}, x)} << j;
        }
        EXPECT_EQ(f->formula.evaluate(assignment), native_value(g, root, x));
      }
      EXPECT_FALSE(validate_endpoint(g, corrupted, o, validation));
    }
  }
}

TEST(Endpoint, CorrelatedBoundaryWinsSurviveAFrontierOfTwo) {
  // ab and !a*cd are mutually exclusive. Their XOR can be an OR on the
  // reachable image, despite the one-stack gate limit. The old structural
  // ranking pruned this boundary and kept the cost-90 identity endpoint.
  Xag        g;
  const auto a = g.input("a"), b = g.input("b"), c = g.input("c"), d = g.input("d");
  const auto ab = g.land(a, b), ncd = g.land(~a, g.land(c, d));
  const auto root = g.lxor(ab, ncd);
  for (uint32_t phases : {1U, 2U}) {
    SCOPED_TRACE(phases);
    auto o               = small_gates();
    o.gates.stack        = 1;
    o.boundaries         = 2;
    o.clock_phases       = phases;
    o.divisor_partitions = 0;
    o.local_candidates   = 0;
    o.fast_accept        = false;
    Budget     work{1000000};
    const auto result = select_endpoint(g, root, "q", {}, {}, o, work);
    ASSERT_TRUE(result.selected);
    EXPECT_EQ(result.selected->total_cost(), phases == 1 ? 71U : 59U);
    EXPECT_EQ(result.selected->origin, phases == 1 ? "existing-care-residual" : "existing-care-mixed");
    EXPECT_EQ(result.selected->cells.size(), phases);
    EXPECT_GT(result.report.boundary_wins, 0U);
    EXPECT_GT(result.report.existing_care_images, 0U);
    EXPECT_EQ(result.selected->cells.back().function->metrics.transistors, 2U);
    EXPECT_FALSE(result.selected->whole_cone);
    check_composition(g, *result.selected, o);
    const auto& p = result.report;
    EXPECT_LE(p.boundary_work, o.tier_work);
    EXPECT_LE(p.boundary_two_cell_work + p.boundary_multi_cell_work, p.boundary_work);
    EXPECT_LE(p.two_phase_work + p.boundary_two_cell_work + p.boundary_multi_cell_work, o.tier_work);
    EXPECT_LE(p.boundary_trials, (phases == 1 ? 8 : 12) * o.boundaries + phases);
    EXPECT_LE(p.boundary_bytes_peak, o.boundary_bytes);

    auto no_care        = o;
    no_care.care_phases = 0;
    Budget     plain_work{1000000};
    const auto plain = select_endpoint(g, root, "q", {}, {}, no_care, plain_work);
    ASSERT_TRUE(plain.selected);
    EXPECT_LT(result.selected->total_cost(), plain.selected->total_cost());
    EXPECT_EQ(plain.report.existing_care_attempts, 0U);
    check_composition(g, *plain.selected, no_care);

    // Cache admission affects work, never the interpretation of correlated
    // inputs. Outside readers remain protected even when a producer is used.
    auto no_cache                = o;
    no_cache.image_cache_entries = 0;
    Budget     uncached_work{1000000};
    const auto uncached = select_endpoint(g, root, "q", {}, {}, no_cache, uncached_work);
    ASSERT_TRUE(uncached.selected);
    EXPECT_EQ(uncached.selected->total_cost(), result.selected->total_cost());
    check_composition(g, *uncached.selected, no_cache);
    const std::array readers{ab, ncd};
    Budget           shared_work{1000000};
    const auto       shared = select_endpoint(g, root, "q", readers, {}, o, shared_work);
    ASSERT_TRUE(shared.selected);
    EXPECT_EQ(shared.selected->residual_nodes, 0U);
    check_composition(g, *shared.selected, o);

    Budget   measured{1000000};
    uint64_t samples            = 0;
    measured.admission_interval = 1;
    measured.admission          = [&] {
      ++samples;
      return true;
    };
    const auto replay = select_endpoint(g, root, "q", {}, {}, o, measured);
    ASSERT_TRUE(replay.selected);
    ASSERT_GT(samples, 2U);
    EXPECT_EQ(replay.report, result.report);
    EXPECT_EQ(measured.remaining, work.remaining);
    for (const auto stop : {samples / 2, samples - 1}) {
      Budget   refused{1000000};
      uint64_t calls             = 0;
      refused.admission_interval = 1;
      refused.admission          = [&] { return ++calls != stop; };
      const auto interrupted     = select_endpoint(g, root, "q", {}, {}, o, refused);
      ASSERT_TRUE(interrupted.selected);
      EXPECT_TRUE(refused.resource_exhausted);
      EXPECT_EQ(calls, stop);
      check_composition(g, *interrupted.selected, o);
      if (stop == samples - 1) {
        EXPECT_EQ(interrupted.selected->total_cost(), result.selected->total_cost());
      }
    }
  }
}

TEST(Endpoint, DualRailXorAndFreeComplementaryProducer) {
  Xag              g;
  const auto       a = g.input("a"), b = g.input("b");
  const auto       root = g.lxor(a, b);
  Endpoint_options o;
  o.cost.static_xor = 20;
  const std::array rails{a.id, b.id};
  Budget           work{1000000};
  const auto       free    = select_endpoint(g, root, "q", {}, rails, o, work);
  const auto       charged = select_endpoint(g, root, "q", {}, {}, o, work);
  ASSERT_TRUE(free.selected);
  ASSERT_TRUE(charged.selected);
  EXPECT_TRUE(free.selected->whole_cone);
  ASSERT_EQ(free.selected->cells.size(), 1U);
  EXPECT_EQ(free.selected->cells.front().function->inputs.size(), 2U);
  EXPECT_EQ(free.selected->inverter_cost, 0U);
  EXPECT_EQ(charged.selected->inverter_cost, 2U);
  check_composition(g, *free.selected, o);
  check_composition(g, *charged.selected, o);
}

TEST(Endpoint, ExistingCareCanMixAPhaseOneDivisorWithCorrelatedStaticLogic) {
  Xag        g;
  const auto a = g.input("a"), b = g.input("b"), c = g.input("c"), d = g.input("d");
  const auto root      = g.lxor(g.land(a, b), g.land(~a, g.land(c, d)));
  auto       o         = small_gates();
  o.gates.stack        = 1;
  o.divisor_partitions = 0;
  Budget     work{10000000};
  const auto result = select_endpoint(g, root, "q", {}, {}, o, work);
  ASSERT_TRUE(result.selected);
  EXPECT_EQ(result.selected->origin, "existing-care-mixed");
  EXPECT_FALSE(result.selected->whole_cone);
  ASSERT_EQ(result.selected->cells.size(), 2U);
  const auto& producers = result.selected->cells.back().producers;
  EXPECT_NE(std::find(producers.begin(), producers.end(), -1), producers.end());
  EXPECT_NE(std::find(producers.begin(), producers.end(), 0), producers.end());
  check_composition(g, *result.selected, o);
}

TEST(Endpoint, MixedBoundaryPromotesOnlyTheProfitableProducer) {
  Xag              g;
  const auto       a = g.input("a"), b = g.input("b"), c = g.input("c"), d = g.input("d");
  const auto       expensive = g.lxor(a, b), cheap = g.land(c, d);
  const auto       root = g.lxor(expensive, cheap);
  Endpoint_options o;
  o.gates              = {2, 2, 2};
  o.cost.static_xor    = 20;
  o.divisor_partitions = 0;
  o.care_phases        = 0;
  o.local_candidates   = 0;
  o.fast_accept        = false;
  Budget     work{10000000};
  const auto result = select_endpoint(g, root, "q", {}, {}, o, work);
  ASSERT_TRUE(result.selected);
  ASSERT_EQ(result.selected->cells.size(), 2U);
  EXPECT_EQ(result.selected->origin, "mixed-residual");
  EXPECT_EQ(result.selected->cells.front().function->root, (Xsignal{expensive.id, false}));
  EXPECT_EQ(result.selected->cells.back().function->inputs, (std::vector<Id>{expensive.id, cheap.id}));
  EXPECT_EQ(result.selected->cells.back().producers, (std::vector<int32_t>{0, -1}));
  EXPECT_EQ(result.selected->residual_nodes, 1U);
  EXPECT_EQ(result.selected->static_cost, o.cost.static_and);
  EXPECT_GT(result.report.boundary_wins, 0U);
  EXPECT_GT(result.report.boundary_two_cell_work, 0U);
  EXPECT_LE(result.report.two_phase_work + result.report.boundary_two_cell_work, o.tier_work);
  check_composition(g, *result.selected, o);

  // Reusing an externally required XOR removes its conversion gain; neither
  // producer should be duplicated into a first-phase cell in that case.
  const std::array readers{expensive};
  Budget           shared_work{10000000};
  const auto       shared = select_endpoint(g, root, "q", readers, {}, o, shared_work);
  ASSERT_TRUE(shared.selected);
  EXPECT_EQ(shared.selected->cells.size(), 1U);
  EXPECT_EQ(shared.selected->static_cost, o.cost.static_xor + o.cost.static_and);
  check_composition(g, *shared.selected, o);

  Budget   measured{10000000};
  uint64_t samples            = 0;
  measured.admission_interval = 1;
  measured.admission          = [&] {
    ++samples;
    return true;
  };
  const auto replay = select_endpoint(g, root, "q", {}, {}, o, measured);
  ASSERT_TRUE(replay.selected);
  EXPECT_EQ(replay.report, result.report);
  EXPECT_EQ(measured.remaining, work.remaining);
  ASSERT_GT(samples, 4U);
  for (const auto stop : {samples / 4, samples / 2, samples - 1}) {
    Budget   refused{10000000};
    uint64_t calls             = 0;
    refused.admission_interval = 1;
    refused.admission          = [&] { return ++calls != stop; };
    const auto interrupted     = select_endpoint(g, root, "q", {}, {}, o, refused);
    ASSERT_TRUE(interrupted.selected);
    EXPECT_TRUE(refused.resource_exhausted);
    EXPECT_EQ(calls, stop);
    EXPECT_LE(interrupted.report.two_phase_work + interrupted.report.boundary_two_cell_work, o.tier_work);
    check_composition(g, *interrupted.selected, o);
    if (stop == samples - 1) {
      EXPECT_EQ(interrupted.selected->total_cost(), result.selected->total_cost());
    }
  }
}

TEST(Endpoint, MixedBoundaryCanReleaseSharedStaticLogicOnlyByPromotingBothReaders) {
  Xag              g;
  const auto       a = g.input("a"), b = g.input("b"), c = g.input("c"), d = g.input("d");
  const auto       e = g.input("e"), f = g.input("f");
  const auto       shared = g.lxor(c, d);
  const auto       x = g.land(shared, a), y = g.land(shared, b), cheap = g.land(e, f);
  const auto       root = g.lxor(g.lxor(x, y), cheap);
  Endpoint_options o;
  o.gates              = {3, 3, 4};
  o.cost.static_xor    = 40;
  o.divisor_partitions = 0;
  o.care_phases        = 0;
  o.local_candidates   = 0;
  o.fast_accept        = false;
  o.boundaries         = 2;  // the useful wide realization must survive pruning
  Budget     work{10000000};
  const auto result = select_endpoint(g, root, "q", {}, {}, o, work);
  ASSERT_TRUE(result.selected);
  ASSERT_EQ(result.selected->cells.size(), 3U);
  EXPECT_EQ(result.selected->cells[0].function->root, (Xsignal{x.id, false}));
  EXPECT_EQ(result.selected->cells[1].function->root, (Xsignal{y.id, false}));
  EXPECT_EQ(result.selected->cells.back().producers, (std::vector<int32_t>{0, 1, -1}));
  EXPECT_EQ(result.selected->residual_nodes, 1U);
  EXPECT_EQ(result.selected->static_cost, o.cost.static_and);
  check_composition(g, *result.selected, o);
}

TEST(Endpoint, CareCompletionCanDemoteAProducerWithoutLosingItsCorrelation) {
  for (bool cheap_first : {false, true}) {
    SCOPED_TRACE(cheap_first);
    Xag        g;
    const auto s = g.input("s"), a = g.input("a"), b = g.input("b"), c = g.input("c");
    Xsignal    expensive, cheap;
    if (cheap_first) {
      cheap     = g.land(~s, c);
      expensive = g.land(s, g.lxor(a, b));
    } else {
      expensive = g.land(s, g.lxor(a, b));
      cheap     = g.land(~s, c);
    }
    const auto       root = g.lxor(expensive, cheap);
    Endpoint_options o;
    o.gates              = {3, 3, 4};
    o.cost.static_xor    = 20;
    o.divisor_partitions = 0;
    o.local_candidates   = 0;
    o.fast_accept        = false;
    const std::array readers{cheap};
    auto             no_care = o;
    no_care.care_phases      = 0;
    Budget     baseline_work{10000000};
    const auto baseline = select_endpoint(g, root, "q", readers, {}, no_care, baseline_work);
    ASSERT_TRUE(baseline.selected);
    Budget     work{10000000};
    const auto result = select_endpoint(g, root, "q", readers, {}, o, work);
    ASSERT_TRUE(result.selected);
    EXPECT_EQ(result.selected->origin, "existing-care-mixed");
    ASSERT_TRUE(result.selected->functional_basis);
    ASSERT_EQ(result.selected->cells.size(), 2U);
    EXPECT_LT(result.selected->total_cost(), baseline.selected->total_cost());
    const auto expected_inputs    = cheap_first ? std::vector<Id>{cheap.id, 0} : std::vector<Id>{0, cheap.id};
    const auto expected_producers = cheap_first ? std::vector<int32_t>{-1, 0} : std::vector<int32_t>{0, -1};
    EXPECT_EQ(result.selected->cells.back().function->inputs, expected_inputs);
    EXPECT_EQ(result.selected->cells.back().producers, expected_producers);
    EXPECT_EQ(result.selected->cells.back().function->metrics.transistors, 2U);  // OR completion
    EXPECT_EQ(result.selected->residual_nodes, 0U);                              // cheap has an outside reader
    check_composition(g, *result.selected, o);

    // Restoring an arbitrary native signal instead of the original divisor
    // breaks the relation even though the independent basis still contains it.
    auto invalid                         = *result.selected;
    auto changed                         = std::make_shared<Endpoint_function>(*invalid.cells.back().function);
    changed->inputs[cheap_first ? 0 : 1] = a.id;
    invalid.cells.back().function        = changed;
    Budget validation{1000000};
    EXPECT_FALSE(validate_endpoint(g, invalid, o, validation));

    Budget   measured{10000000};
    uint64_t samples            = 0;
    measured.admission_interval = 1;
    measured.admission          = [&] {
      ++samples;
      return true;
    };
    const auto replay = select_endpoint(g, root, "q", readers, {}, o, measured);
    ASSERT_TRUE(replay.selected);
    ASSERT_GT(samples, 2U);
    EXPECT_EQ(replay.report, result.report);
    EXPECT_EQ(measured.remaining, work.remaining);
    for (const auto stop : {samples / 2, samples - 1}) {
      Budget   refused{10000000};
      uint64_t calls             = 0;
      refused.admission_interval = 1;
      refused.admission          = [&] { return ++calls != stop; };
      const auto interrupted     = select_endpoint(g, root, "q", readers, {}, o, refused);
      ASSERT_TRUE(interrupted.selected);
      EXPECT_TRUE(refused.resource_exhausted);
      EXPECT_EQ(calls, stop);
      check_composition(g, *interrupted.selected, o);
      if (stop == samples - 1) {
        EXPECT_EQ(interrupted.selected->total_cost(), result.selected->total_cost());
      }
    }
  }
}

TEST(Endpoint, DivisorImagesAreReusedAcrossImplementationTrialsWithinMemoryBounds) {
  Xag        g;
  const auto a = g.input("a"), b = g.input("b"), c = g.input("c"), d = g.input("d");
  const auto root      = g.lxor(g.land(a, b), g.land(~a, g.land(c, d)));
  auto       o         = small_gates();
  o.gates.stack        = 1;
  o.divisor_partitions = 0;
  o.local_candidates   = 0;
  Budget     cached_work{10000000};
  const auto cached = select_endpoint(g, root, "q", {}, {}, o, cached_work);
  ASSERT_TRUE(cached.selected);
  EXPECT_GT(cached.report.divisor_image_hits, 0U);
  EXPECT_GT(cached.report.image_cache_bytes, 0U);
  EXPECT_LE(cached.report.image_cache_bytes, o.image_cache_bytes);
  for (bool entries : {false, true}) {
    auto uncached_options = o;
    if (entries) {
      uncached_options.image_cache_entries = 0;
    } else {
      uncached_options.image_cache_bytes = 1;
    }
    Budget     uncached_work{10000000};
    const auto uncached = select_endpoint(g, root, "q", {}, {}, uncached_options, uncached_work);
    ASSERT_TRUE(uncached.selected);
    EXPECT_EQ(uncached.selected->total_cost(), cached.selected->total_cost());
    EXPECT_EQ(uncached.report.image_cache_bytes, 0U);
    EXPECT_EQ(uncached.report.divisor_image_hits, 0U);
    EXPECT_GT(uncached.report.divisor_images, cached.report.divisor_images);
    EXPECT_LT(uncached_work.remaining, cached_work.remaining);
    check_composition(g, *uncached.selected, uncached_options);
  }
}

TEST(Endpoint, LocalNonCutDivisorWinsByTargetCostWithoutDeletingSharedLogic) {
  Xag              g;
  const auto       a = g.input("a"), b = g.input("b"), c = g.input("c");
  const auto       shared = g.land(a, b);
  const auto       root   = g.lxor(g.lxor(shared, c), c);
  const std::array readers{shared};
  auto             o   = small_gates();
  o.boundaries         = 1;
  o.divisor_partitions = 0;
  o.local_candidates   = 0;
  Budget     baseline_work{10000000};
  const auto baseline = select_endpoint(g, root, "q", readers, {}, o, baseline_work);
  ASSERT_TRUE(baseline.selected);
  EXPECT_TRUE(baseline.selected->whole_cone);
  // The c paths bypass shared, so {shared} is not a structural cut.
  EXPECT_EQ(collect_window(g, root, std::array{shared.id}, o.window, baseline_work).status, Status::invalid);
  o.local_candidates = 1;
  Budget     work{10000000};
  const auto result = select_endpoint(g, root, "q", readers, {}, o, work);
  ASSERT_TRUE(result.selected);
  EXPECT_EQ(result.selected->origin, "local-divisor-residual");
  EXPECT_EQ(result.report.local_attempts, 1U);
  EXPECT_EQ(result.report.local_wins, 1U);
  EXPECT_EQ(result.selected->static_cost, o.cost.static_and);
  EXPECT_EQ(result.selected->total_cost() + 1, baseline.selected->total_cost());
  EXPECT_FALSE(result.selected->whole_cone);
  check_composition(g, *result.selected, o);
}

TEST(Endpoint, LocalPairCanDetermineFunctionWithoutFormingAStructuralCut) {
  Xag              g;
  const auto       a = g.input("a"), b = g.input("b"), c = g.input("c"), d = g.input("d");
  const auto       s = g.land(a, b), t = g.land(a, c);
  const auto       root = g.lxor(g.lxor(g.lor(s, t), d), d);
  const std::array readers{s, t};
  auto             o = small_gates();
  o.clock_phases     = 1;
  o.boundaries       = 1;
  o.local_divisors   = 2;  // retain the two externally used divisors
  o.local_candidates = 2;  // singleton trials cannot determine the function
  Budget     baseline_work{10000000};
  const auto baseline = select_endpoint(g, root, "q", readers, {}, o, baseline_work);
  ASSERT_TRUE(baseline.selected);
  EXPECT_EQ(baseline.report.local_wins, 0U);
  EXPECT_EQ(collect_window(g, root, std::array{s.id, t.id}, o.window, baseline_work).status, Status::invalid);
  o.local_candidates = 3;  // adds exactly the pair
  Budget     work{10000000};
  const auto result = select_endpoint(g, root, "q", readers, {}, o, work);
  ASSERT_TRUE(result.selected);
  EXPECT_EQ(result.selected->origin, "local-divisor-residual");
  EXPECT_EQ(result.report.local_attempts, 3U);
  EXPECT_EQ(result.report.local_wins, 1U);
  EXPECT_EQ(result.selected->static_cost, 2 * o.cost.static_and);
  EXPECT_LT(result.selected->total_cost(), baseline.selected->total_cost());
  check_composition(g, *result.selected, o);
}

TEST(Endpoint, LocalWitnessForestFindsLargerNonCutDivisorSets) {
  for (unsigned width : {3U, 4U}) {
    SCOPED_TRACE(width);
    Xag                  g;
    const auto           a = g.input("a"), bypass = g.input("bypass");
    std::vector<Xsignal> readers;
    std::vector<Id>      ids;
    // Reverse source order relative to divisor priority. The first witness
    // needs a late-ranked divisor; an earlier non-separator is needed later.
    std::vector<Xsignal> inputs(width);
    for (unsigned i = width; i > 0; --i) {
      inputs[i - 1] = g.input("b" + std::to_string(i - 1));
    }
    auto sum = g.constant(false);
    for (unsigned i = 0; i < width; ++i) {
      auto divisor = g.land(a, inputs[i]);
      readers.push_back(divisor);
      ids.push_back(divisor.id);
      sum = g.lor(sum, divisor);
    }
    const auto root      = g.lxor(g.lxor(sum, bypass), bypass);
    auto       o         = small_gates();
    o.clock_phases       = 1;
    o.gates              = {width, 4, 10};
    o.boundaries         = 1;
    o.divisor_partitions = 0;
    o.local_divisors     = width;
    o.local_candidates   = 0;
    Budget     baseline_work{10000000};
    const auto baseline = select_endpoint(g, root, "q", readers, {}, o, baseline_work);
    ASSERT_TRUE(baseline.selected);
    EXPECT_EQ(collect_window(g, root, ids, o.window, baseline_work).status, Status::invalid);
    o.local_candidates = 3 * width;
    Budget     work{10000000};
    const auto result = select_endpoint(g, root, "q", readers, {}, o, work);
    ASSERT_TRUE(result.selected);
    EXPECT_EQ(result.selected->origin, "local-divisor-residual");
    EXPECT_EQ(result.selected->cells.back().function->inputs, ids);
    EXPECT_LT(result.selected->total_cost(), baseline.selected->total_cost());
    EXPECT_EQ(result.selected->static_cost, width * o.cost.static_and);
    EXPECT_EQ(result.selected->residual_nodes, 0U);
    EXPECT_GT(result.report.local_wins, 0U);
    EXPECT_LE(result.report.local_attempts, o.local_candidates);
    EXPECT_LE(result.report.local_work, o.tier_work);
    check_composition(g, *result.selected, o);

    auto no_cache                = o;
    no_cache.image_cache_entries = 0;
    Budget     uncached_work{10000000};
    const auto uncached = select_endpoint(g, root, "q", readers, {}, no_cache, uncached_work);
    ASSERT_TRUE(uncached.selected);
    EXPECT_EQ(uncached.selected->total_cost(), result.selected->total_cost());
    EXPECT_EQ(uncached.report.local_attempts, result.report.local_attempts);
    EXPECT_EQ(uncached.report.image_cache_bytes, 0U);
    check_composition(g, *uncached.selected, no_cache);

    Budget   measured{10000000};
    uint64_t samples            = 0;
    measured.admission_interval = 1;
    measured.admission          = [&] {
      ++samples;
      return true;
    };
    const auto replay = select_endpoint(g, root, "q", readers, {}, o, measured);
    ASSERT_TRUE(replay.selected);
    ASSERT_GT(samples, 1U);
    EXPECT_EQ(replay.report, result.report);
    EXPECT_EQ(measured.remaining, work.remaining);
    Budget   refused{10000000};
    uint64_t calls             = 0;
    refused.admission_interval = 1;
    refused.admission          = [&] { return ++calls != samples - 1; };
    const auto interrupted     = select_endpoint(g, root, "q", readers, {}, o, refused);
    ASSERT_TRUE(interrupted.selected);
    EXPECT_TRUE(refused.resource_exhausted);
    EXPECT_EQ(calls, samples - 1);
    EXPECT_EQ(interrupted.selected->total_cost(), result.selected->total_cost());
    check_composition(g, *interrupted.selected, o);
    if (width == 4) {
      auto limited             = o;
      limited.local_candidates = 2 * width;
      Budget     limited_work{10000000};
      const auto partial = select_endpoint(g, root, "q", readers, {}, limited, limited_work);
      ASSERT_TRUE(partial.selected);
      EXPECT_TRUE(partial.report.exhausted);
      EXPECT_EQ(partial.report.local_wins, 0U);
      EXPECT_NE(std::find(partial.report.limits.begin(), partial.report.limits.end(), "local divisor seed candidate budget"),
                partial.report.limits.end());
      EXPECT_LE(partial.report.local_attempts, limited.local_candidates);
      check_composition(g, *partial.selected, limited);
    }
  }
}

TEST(Endpoint, OptionalImprovementSearchCanBeatAnEarlyFullConeResult) {
  Xag        g;
  const auto a = g.input("a"), b = g.input("b"), c = g.input("c"), d = g.input("d");
  const auto root        = g.lor(g.land(a, b), g.land(g.lor(a, c), d));
  auto       o           = small_gates();
  o.gates.logical_inputs = 3;
  o.divisor_partitions   = 4;
  o.boundaries           = 1;
  Budget     fast_work{10000000};
  const auto fast = select_endpoint(g, root, "q", {}, {}, o, fast_work);
  ASSERT_TRUE(fast.selected);
  EXPECT_TRUE(fast.selected->whole_cone);
  EXPECT_EQ(fast.report.local_attempts, 0U);
  o.fast_accept = false;
  Budget     work{10000000};
  const auto result = select_endpoint(g, root, "q", {}, {}, o, work);
  ASSERT_TRUE(result.selected);
  EXPECT_GT(result.report.local_attempts, 0U);
  EXPECT_GT(result.report.local_wins, 0U);
  EXPECT_LT(result.selected->total_cost(), fast.selected->total_cost());
  check_composition(g, *result.selected, o);
}

TEST(Endpoint, FunctionalSupportReductionRetainsStructuralBoundaryForValidation) {
  Xag        g;
  const auto a = g.input("a"), b = g.input("b");
  const auto root = g.lxor(g.lxor(a, b), b);
  Budget     work{1000000};
  const auto r = select_endpoint(g, root, "q", {}, {}, {}, work);
  ASSERT_TRUE(r.selected);
  const auto& f = *r.selected->cells.back().function;
  EXPECT_EQ(f.inputs, (std::vector<Id>{a.id}));
  EXPECT_EQ(f.boundary.size(), 2U);
  check_composition(g, *r.selected, {});
}

TEST(Endpoint, CandidateExhaustionPreservesIntegratedIdentityIncumbent) {
  Xag              g;
  const auto       a = g.input("a"), b = g.input("b"), c = g.input("c");
  const auto       root = g.lxor(g.land(a, b), c);
  Endpoint_options o;
  o.candidate_work = 10;
  Budget     work{10000};
  const auto r = select_endpoint(g, root, "q", {}, {}, o, work);
  ASSERT_TRUE(r.selected);
  EXPECT_EQ(r.status, Status::feasible);
  EXPECT_TRUE(r.report.exhausted);
  EXPECT_EQ(r.selected->origin, "identity");
  EXPECT_EQ(r.selected->cells.size(), 1U);
  EXPECT_TRUE(r.selected->cells.back().latch);
  check_composition(g, *r.selected, o);
}

TEST(Endpoint, OversizedWholeConeUsesBoundedWindow) {
  Xag        g;
  const auto a = g.input("a"), b = g.input("b"), c = g.input("c"), d = g.input("d");
  const auto root = g.land(g.land(a, b), g.land(c, d));
  auto       o    = small_gates();
  o.window.inputs = 2;
  Budget     work{1000000};
  const auto r = select_endpoint(g, root, "q", {}, {}, o, work);
  ASSERT_TRUE(r.selected);
  EXPECT_FALSE(r.report.whole_admitted);
  EXPECT_TRUE(r.report.window_used);
  EXPECT_FALSE(r.selected->whole_cone);
  check_composition(g, *r.selected, o);
}

TEST(Endpoint, AlternativeWindowExposesSharedInputBeyondInitialOpaqueBoundary) {
  Xag              g;
  const auto       a = g.input("a"), b = g.input("b"), c = g.input("c");
  const auto       d = g.input("d"), e = g.input("e"), f = g.input("f");
  const auto       shared = g.land(g.lor(a, b), g.lor(a, c));
  const auto       other  = g.land(g.land(d, e), f);
  const auto       root   = g.land(shared, other);
  Endpoint_options o;
  o.clock_phases       = 1;
  o.window.inputs      = 4;
  o.gates              = {4, 4, 4};
  o.cost.static_and    = 20;
  o.local_candidates   = 0;
  o.care_phases        = 0;
  o.divisor_partitions = 0;

  // The greedy seed expands the newer, unrelated branch and leaves the
  // reconvergent subfunction opaque. Contracting that cut cannot expose a.
  Budget     admission{10000};
  const auto initial = grow_window(g, root, o.window, admission);
  ASSERT_EQ(initial.status, Status::feasible);
  EXPECT_EQ(initial.leaves, (std::vector<Id>{d.id, e.id, f.id, shared.id}));
  o.boundaries = 1;
  Budget     baseline_work{1000000};
  const auto baseline = select_endpoint(g, root, "q", {}, {}, o, baseline_work);
  ASSERT_TRUE(baseline.selected);
  check_composition(g, *baseline.selected, o);

  o.boundaries = 4;
  Budget     work{1000000};
  const auto r = select_endpoint(g, root, "q", {}, {}, o, work);
  ASSERT_TRUE(r.selected);
  EXPECT_FALSE(r.report.whole_admitted);
  EXPECT_TRUE(r.report.window_used);
  EXPECT_LT(r.selected->total_cost(), baseline.selected->total_cost());
  EXPECT_EQ(r.selected->cells.back().function->inputs, (std::vector<Id>{a.id, b.id, c.id, other.id}));
  EXPECT_LE(r.report.boundaries, o.boundaries);
  EXPECT_GT(r.report.boundary_replacements, 0U);
  EXPECT_GT(r.report.boundary_wins, 0U);
  EXPECT_GT(r.report.boundary_trials, r.report.boundaries);
  EXPECT_LE(r.report.boundary_trials, 8 * o.boundaries + 1);
  EXPECT_LE(r.report.boundary_bytes_peak, o.boundary_bytes);
  EXPECT_LE(r.report.boundary_work, o.tier_work);
  check_composition(g, *r.selected, o);

  Budget   measured{1000000};
  uint64_t samples            = 0;
  measured.admission_interval = 1;
  measured.admission          = [&] {
    ++samples;
    return true;
  };
  const auto measured_result = select_endpoint(g, root, "q", {}, {}, o, measured);
  ASSERT_TRUE(measured_result.selected);
  ASSERT_GT(samples, 1U);
  EXPECT_EQ(measured_result.report, r.report);
  EXPECT_EQ(measured.remaining, work.remaining);
  Budget   refused{1000000};
  uint64_t calls             = 0;
  refused.admission_interval = 1;
  refused.admission          = [&] { return ++calls != samples - 1; };
  const auto interrupted     = select_endpoint(g, root, "q", {}, {}, o, refused);
  ASSERT_TRUE(interrupted.selected);
  EXPECT_TRUE(refused.resource_exhausted);
  EXPECT_EQ(calls, samples - 1);
  EXPECT_GT(interrupted.report.boundary_wins, 0U);
  EXPECT_EQ(interrupted.selected->total_cost(), r.selected->total_cost());
  check_composition(g, *interrupted.selected, o);

  auto limited_options           = o;
  limited_options.boundary_bytes = r.report.boundary_bytes_peak / 2;
  Budget     limited_work{1000000};
  const auto limited = select_endpoint(g, root, "q", {}, {}, limited_options, limited_work);
  ASSERT_TRUE(limited.selected);
  EXPECT_LE(limited.report.boundary_bytes_peak, limited_options.boundary_bytes);
  EXPECT_NE(std::find(limited.report.limits.begin(), limited.report.limits.end(), "boundary memory budget"),
            limited.report.limits.end());
  check_composition(g, *limited.selected, limited_options);

  // No deletion credit for the shared cone when an outside output needs it.
  const std::array readers{shared};
  Budget           shared_work{1000000};
  const auto       protected_result = select_endpoint(g, root, "q", readers, {}, o, shared_work);
  ASSERT_TRUE(protected_result.selected);
  EXPECT_NE(protected_result.selected->cells.back().function->inputs, (std::vector<Id>{a.id, b.id, c.id, other.id}));
  check_composition(g, *protected_result.selected, o);
}

TEST(Endpoint, CoordinatedExpansionCrossesAnIntermediateSupportLimit) {
  Xag              g;
  const auto       a = g.input("a"), b = g.input("b"), c = g.input("c"), d = g.input("d");
  const auto       x = g.lor(a, b), y = g.land(a, b);
  const auto       shared = g.lxor(x, y);  // a XOR b, exposed only after expanding both branches
  const auto       other  = g.land(c, d);
  const auto       root   = g.land(shared, other);
  Endpoint_options o;
  o.clock_phases       = 1;
  o.window.inputs      = 3;
  o.gates              = {3, 4, 4};
  o.cost.static_and    = 20;
  o.cost.static_xor    = 20;
  o.local_candidates   = 0;
  o.care_phases        = 0;
  o.divisor_partitions = 0;
  o.boundaries         = 4;
  Budget     work{1000000};
  const auto r = select_endpoint(g, root, "q", {}, {}, o, work);
  ASSERT_TRUE(r.selected);
  EXPECT_FALSE(r.report.whole_admitted);
  // {x,y,other} has three inputs. Either single expansion has four inputs,
  // exceeding the analysis limit; the joint move exposes {a,b,other} directly.
  EXPECT_EQ(r.selected->cells.back().function->inputs, (std::vector<Id>{a.id, b.id, other.id}));
  EXPECT_EQ(r.selected->residual_nodes, 1U);
  EXPECT_LE(r.report.boundaries, 4U);
  check_composition(g, *r.selected, o);

  Budget     repeat_work{1000000};
  const auto repeat = select_endpoint(g, root, "q", {}, {}, o, repeat_work);
  ASSERT_TRUE(repeat.selected);
  EXPECT_EQ(repeat.report, r.report);
  EXPECT_EQ(repeat_work.remaining, work.remaining);
  EXPECT_EQ(repeat.selected->total_cost(), r.selected->total_cost());
}

TEST(Endpoint, CoordinatedCycleExpansionCrossesEveryProperSubsetSupportBarrier) {
  for (const unsigned width : {3U, 4U}) {
    SCOPED_TRACE(width);
    Xag                  g;
    std::vector<Xsignal> inputs, branches;
    for (unsigned i = 0; i < width; ++i) {
      inputs.push_back(g.input("a" + std::to_string(i)));
    }
    auto d = g.input("d"), e = g.input("e");
    auto shared = g.constant(true);
    for (unsigned i = 0; i < width; ++i) {
      branches.push_back(g.lor(inputs[i], inputs[(i + 1) % width]));
      shared = g.land(shared, branches.back());
    }
    const auto       other = g.land(d, e), root = g.land(shared, other);
    Endpoint_options o;
    o.clock_phases       = 1;
    o.window.inputs      = width + 1;
    o.gates              = {width + 1, 4, 4};
    o.cost.static_and    = 20;
    o.local_candidates   = 0;
    o.care_phases        = 0;
    o.divisor_partitions = 0;
    o.boundaries         = 16;

    // At {branches...,other}, every nonempty proper subset expansion is
    // too wide, including all pairs and (for the four-cycle) all triples.
    for (uint32_t mask = 1; mask + 1 < (1U << width); ++mask) {
      std::set<Id> leaves{other.id};
      for (unsigned i = 0; i < width; ++i) {
        if (mask & (1U << i)) {
          leaves.insert(inputs[i].id);
          leaves.insert(inputs[(i + 1) % width].id);
        } else {
          leaves.insert(branches[i].id);
        }
      }
      EXPECT_GT(leaves.size(), o.window.inputs);
    }
    Budget     work{1000000};
    const auto result = select_endpoint(g, root, "q", {}, {}, o, work);
    ASSERT_TRUE(result.selected);
    EXPECT_FALSE(result.report.whole_admitted);
    std::vector<Id> expected;
    for (auto input : inputs) {
      expected.push_back(input.id);
    }
    expected.push_back(other.id);
    EXPECT_EQ(result.selected->cells.back().function->inputs, expected);
    EXPECT_EQ(result.selected->residual_nodes, 1U);
    EXPECT_LE(result.report.boundary_trials, 8 * o.boundaries + 1);
    EXPECT_LE(result.report.boundaries, o.boundaries);
    EXPECT_LE(result.report.boundary_bytes_peak, o.boundary_bytes);
    EXPECT_EQ(result.report.boundary_two_cell_work, 0U);  // one-phase search
    EXPECT_EQ(result.report.boundary_multi_cell_work, 0U);
    check_composition(g, *result.selected, o);

    Budget     replay_work{1000000};
    const auto replay = select_endpoint(g, root, "q", {}, {}, o, replay_work);
    ASSERT_TRUE(replay.selected);
    EXPECT_EQ(replay.report, result.report);
    EXPECT_EQ(replay_work.remaining, work.remaining);
    EXPECT_EQ(replay.selected->cells.back().function->inputs, expected);

    // Refusal during group search must retain a valid incumbent and obey the
    // same total, per-tier and move caps as single/pair boundary search.
    auto bounded      = o;
    bounded.tier_work = result.report.boundary_work / 2;
    Budget     bounded_work{1000000};
    const auto interrupted = select_endpoint(g, root, "q", {}, {}, bounded, bounded_work);
    ASSERT_TRUE(interrupted.selected);
    EXPECT_TRUE(interrupted.report.exhausted);
    EXPECT_LE(interrupted.report.boundary_work, bounded.tier_work);
    EXPECT_LE(interrupted.report.boundary_trials, 8 * o.boundaries + 1);
    check_composition(g, *interrupted.selected, bounded);
  }
}

TEST(Endpoint, CoordinatedContractionRetainsThreeRelatedDivisors) {
  Xag              g;
  const auto       a = g.input("a"), b = g.input("b"), c = g.input("c");
  const auto       x = g.lxor(a, b), y = g.lxor(b, c), z = g.lxor(a, c);
  const auto       root = g.lor(g.land(x, y), z);
  Endpoint_options o;
  o.clock_phases       = 1;
  o.window.inputs      = 3;
  o.gates              = {3, 2, 2};
  o.cost.static_and    = 20;
  o.local_candidates   = 0;
  o.care_phases        = 0;
  o.divisor_partitions = 0;
  o.fast_accept        = false;
  Budget     work{1000000};
  const auto result = select_endpoint(g, root, "q", {}, {}, o, work);
  ASSERT_TRUE(result.selected);
  EXPECT_EQ(result.selected->cells.back().function->inputs, (std::vector<Id>{x.id, y.id, z.id}));
  EXPECT_EQ(result.selected->residual_nodes, 3U);
  EXPECT_LE(result.report.boundary_trials, 8 * o.boundaries + 1);
  check_composition(g, *result.selected, o);
}

TEST(Endpoint, ExpensiveTwoPhaseSearchLeavesWorkForSelectiveRemoval) {
  Xag                  g;
  std::vector<Xsignal> level;
  for (uint32_t i = 0; i < 8; ++i) {
    level.push_back(g.input(std::to_string(i)));
  }
  while (level.size() > 1) {
    std::vector<Xsignal> next;
    for (size_t i = 0; i < level.size(); i += 2) {
      next.push_back(g.land(level[i], level[i + 1]));
    }
    level = std::move(next);
  }
  bool continued_after_limit = false;
  // Sweep word-scaled allowances rather than depending on old per-bit charges.
  for (uint64_t tier_work = 64; tier_work <= 4096; tier_work += 64) {
    auto o        = small_gates();
    o.fast_accept = false;
    o.tier_work   = tier_work;
    Budget     work{100000};
    const auto r = select_endpoint(g, level.front(), "q", {}, {}, o, work);
    ASSERT_TRUE(r.selected);
    const auto& p = r.report;
    EXPECT_LE(p.admission_work, tier_work);
    EXPECT_LE(p.one_cell_work, tier_work);
    EXPECT_LE(p.boundary_work, tier_work);
    EXPECT_LE(p.boundary_two_cell_work, p.boundary_work);
    EXPECT_LE(p.boundary_two_cell_work, tier_work / 8);
    EXPECT_LE(p.boundary_multi_cell_work, tier_work / 8);
    EXPECT_LE(p.boundary_two_cell_work + p.boundary_multi_cell_work, p.boundary_work);
    EXPECT_LE(p.two_phase_work + p.boundary_two_cell_work + p.boundary_multi_cell_work, tier_work);
    EXPECT_LE(p.residual_work, tier_work);
    EXPECT_LE(p.local_work, tier_work);
    const bool hit         = std::find(p.limits.begin(), p.limits.end(), "two-phase tier budget") != p.limits.end();
    continued_after_limit |= hit && p.residual_attempts > 0 && p.residual_work > 0;
    EXPECT_GT(work.remaining, 0U);
    check_composition(g, *r.selected, o);
  }
  EXPECT_TRUE(continued_after_limit);
}

TEST(Endpoint, MemoryAdmissionAndExternalResourceRefusalHaveExplicitOutcomes) {
  Xag        g;
  const auto a = g.input("a"), b = g.input("b"), c = g.input("c");
  const auto root  = g.land(g.land(a, b), c);
  auto       o     = small_gates();
  o.fast_accept    = false;  // explicitly exercise boundary admission after full-cone attempts
  o.boundary_bytes = 1;
  Budget     work{1000000};
  const auto r = select_endpoint(g, root, "q", {}, {}, o, work);
  ASSERT_TRUE(r.selected);
  EXPECT_TRUE(r.report.exhausted);
  EXPECT_NE(std::find(r.report.limits.begin(), r.report.limits.end(), "boundary memory budget"), r.report.limits.end());
  check_composition(g, *r.selected, o);
  Budget refusal{1000000};
  refusal.admission  = [] { return false; };
  const auto stopped = select_endpoint(g, root, "q", {}, {}, o, refusal);
  EXPECT_EQ(stopped.status, Status::search_exhausted);
  EXPECT_FALSE(stopped.selected);
  EXPECT_TRUE(refusal.resource_exhausted);
}

TEST(Endpoint, ComposedSelectionMatchesSmallReconvergentNetworks) {
  uint32_t   random = 0x7f4219;
  const auto next   = [&] {
    random = random * 1664525U + 1013904223U;
    return random;
  };
  for (uint32_t trial = 0; trial < 24; ++trial) {
    Xag                  g;
    std::vector<Xsignal> signals;
    for (uint32_t i = 0; i < 4; ++i) {
      signals.push_back(g.input(std::to_string(i)));
    }
    for (uint32_t i = 0; i < 16; ++i) {
      auto a = signals[next() % signals.size()], b = signals[next() % signals.size()];
      if (next() & 0x100) {
        a = ~a;
      }
      const auto op = next() % 3;
      signals.push_back(op == 0 ? g.land(a, b) : op == 1 ? g.lxor(a, b) : g.lor(a, b));
    }
    Endpoint_options o;
    o.gates          = {3, 3, 4};
    o.clock_phases   = 1 + trial % 2;
    o.fast_accept    = false;
    o.candidate_work = 2000;
    Budget           work{100000};
    const std::array readers{signals[signals.size() - 2]};
    const auto       r = select_endpoint(g, signals.back(), "q", readers, {}, o, work);
    ASSERT_TRUE(r.selected) << trial;
    SCOPED_TRACE(trial);
    check_composition(g, *r.selected, o);
  }
}

TEST(Endpoint, WiderRankingImprovesCostWithoutLosingTwoCellCandidates) {
  // These costs were measured with the original one-round frontier. The first
  // case benefits from wider ranking; the others guard its two-cell allowance.
  const std::array<uint32_t, 3> trials{67, 30, 81};
  const std::array<uint64_t, 3> previous_cost{129, 22, 25};
  for (unsigned case_index = 0; case_index < trials.size(); ++case_index) {
    const auto trial = trials[case_index];
    SCOPED_TRACE(trial);
    uint32_t   random = 0x192a31U + trial;
    const auto next   = [&] {
      random = random * 1664525U + 1013904223U;
      return random;
    };
    Xag                  g;
    std::vector<Xsignal> signals;
    for (unsigned i = 0; i < 6; ++i) {
      signals.push_back(g.input(std::to_string(i)));
    }
    for (unsigned i = 0; i < 30; ++i) {
      auto a = signals[next() % signals.size()], b = signals[next() % signals.size()];
      if (next() & 0x100) {
        a = ~a;
      }
      const auto op = next() % 3;
      signals.push_back(op == 0 ? g.land(a, b) : op == 1 ? g.lxor(a, b) : g.lor(a, b));
    }
    const auto       root = g.lxor(g.land(signals[20], signals[21]), g.lor(signals[30], signals.back()));
    Endpoint_options o;
    o.gates              = {3, 3, 4};
    o.boundaries         = 2;
    o.cost.static_xor    = 20;
    o.cost.static_and    = 5;
    o.care_phases        = 0;
    o.local_candidates   = 0;
    o.divisor_partitions = 0;
    o.fast_accept        = false;
    Budget     work{1000000};
    const auto result = select_endpoint(g, root, "q", {}, {}, o, work);
    ASSERT_TRUE(result.selected);
    EXPECT_LE(result.selected->total_cost(), previous_cost[case_index]);
    if (case_index == 0) {
      EXPECT_LT(result.selected->total_cost(), previous_cost[case_index]);
      EXPECT_EQ(result.selected->cells.size(), 3U);
    }
    const auto& p = result.report;
    EXPECT_GT(p.boundary_multi_cell_work, 0U);
    EXPECT_LE(p.boundary_multi_cell_work, o.tier_work / 8);
    EXPECT_LE(p.boundary_two_cell_work + p.boundary_multi_cell_work, p.boundary_work);
    EXPECT_LE(p.two_phase_work + p.boundary_two_cell_work + p.boundary_multi_cell_work, o.tier_work);
    EXPECT_LE(p.boundary_work, o.tier_work);
    EXPECT_LE(p.boundary_trials, 12 * o.boundaries + 2);
    EXPECT_LE(p.boundary_bytes_peak, o.boundary_bytes);
    check_composition(g, *result.selected, o);
    if (case_index == 0) {
      Budget   measured{1000000};
      uint64_t samples            = 0;
      measured.admission_interval = 1;
      measured.admission          = [&] {
        ++samples;
        return true;
      };
      const auto replay = select_endpoint(g, root, "q", {}, {}, o, measured);
      ASSERT_TRUE(replay.selected);
      ASSERT_GT(samples, 1U);
      EXPECT_EQ(replay.report, result.report);
      EXPECT_EQ(measured.remaining, work.remaining);
      Budget   refused{1000000};
      uint64_t calls             = 0;
      refused.admission_interval = 1;
      refused.admission          = [&] { return ++calls != samples - 1; };
      const auto interrupted     = select_endpoint(g, root, "q", {}, {}, o, refused);
      ASSERT_TRUE(interrupted.selected);
      EXPECT_TRUE(refused.resource_exhausted);
      EXPECT_EQ(calls, samples - 1);
      EXPECT_EQ(interrupted.selected->total_cost(), result.selected->total_cost());
      check_composition(g, *interrupted.selected, o);

      auto one_phase         = o;
      one_phase.clock_phases = 1;
      Budget     one_work{1000000};
      const auto one = select_endpoint(g, root, "q", {}, {}, one_phase, one_work);
      ASSERT_TRUE(one.selected);
      EXPECT_EQ(one.report.boundary_multi_cell_work, 0U);
      EXPECT_LE(one.report.boundary_trials, 8 * o.boundaries + 1);
      check_composition(g, *one.selected, one_phase);
    }
  }
}

TEST(Endpoint, JointChoicePoolRetainsDistinctLegalInterfacesWithinItsStorageLimit) {
  Xag              g;
  const auto       a = g.input("a"), b = g.input("b"), c = g.input("c"), d = g.input("d");
  const auto       shared = g.lxor(a, b);
  const auto       root   = g.land(g.lor(shared, c), g.lor(a, d));
  Endpoint_options o;
  o.gates           = {3, 3, 4};
  o.clock_phases    = 1;
  o.fast_accept     = false;
  o.boundaries      = 4;
  o.cost.static_and = o.cost.static_xor = 20;
  bool phased                           = false;
  for (uint32_t phases : {1U, 2U}) {
    o.clock_phases = phases;
    for (uint32_t count : {0U, 1U, 4U, 8U}) {
      Budget     work{10000000};
      const auto result = select_endpoint(g, root, "q", {}, {}, o, work, {}, count);
      ASSERT_TRUE(result.selected);
      check_composition(g, *result.selected, o);
      EXPECT_LE(result.choices.size(), count);
      if (count) {
        EXPECT_FALSE(result.choices.empty());
      }
      std::set<std::vector<Xsignal>> interfaces;
      for (const auto& choice : result.choices) {
        check_composition(g, choice, o);
        phased |= choice.cells.size() > 1;
        std::vector<Xsignal> ports;
        for (const auto& cell : choice.cells) {
          for (size_t j = 0; j < cell.producers.size(); ++j) {
            if (cell.producers[j] < 0) {
              ports.push_back({cell.function->inputs[j], (cell.function->metrics.negative & (1U << j)) != 0});
            }
          }
        }
        std::sort(ports.begin(), ports.end());
        ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
        EXPECT_TRUE(interfaces.insert(ports).second);
      }
    }
  }
  EXPECT_TRUE(phased);
  o.boundary_bytes = 1;
  Budget     work{10000000};
  const auto limited = select_endpoint(g, root, "q", {}, {}, o, work, {}, 4);
  ASSERT_TRUE(limited.selected);
  check_composition(g, *limited.selected, o);
  EXPECT_TRUE(limited.choices.empty());
  EXPECT_TRUE(limited.report.exhausted);
  EXPECT_EQ(select_endpoint(g, root, "q", {}, {}, o, work, {}, 9).status, Status::invalid);
}

TEST(Endpoint, ConstantsStillHaveOneNamedStateEndpointAndInvalidOptionsRefuse) {
  Xag g;
  for (bool value : {false, true}) {
    Budget     work{10000};
    const auto r = select_endpoint(g, g.constant(value), "constant_q", {}, {}, {}, work);
    ASSERT_TRUE(r.selected);
    EXPECT_EQ(r.selected->name, "constant_q");
    ASSERT_EQ(r.selected->cells.size(), 1U);
    EXPECT_TRUE(r.selected->cells.back().latch);
    check_composition(g, *r.selected, {});
  }
  Endpoint_options o;
  o.clock_phases = 3;
  Budget work{10000};
  EXPECT_EQ(select_endpoint(g, {}, "q", {}, {}, o, work).status, Status::invalid);
}

namespace {
// Every decision of a selected endpoint: two runs that agree here select the
// same cells, bindings and costs.
std::string describe(const Endpoint_solution& s) {
  std::string out = s.name + '/' + s.origin + '/' + std::to_string(s.static_cost) + '/' + std::to_string(s.domino_cost) + '/'
                    + std::to_string(s.inverter_cost) + '/' + std::to_string(s.residual_nodes) + (s.whole_cone ? "/w" : "/p");
  if (s.functional_basis) {
    for (auto id : *s.functional_basis) {
      out += ' ' + std::to_string(id);
    }
  }
  for (const auto& cell : s.cells) {
    out += " |" + std::to_string(cell.phase) + (cell.latch ? "L" : "") + ':';
    for (size_t i = 0; i < cell.function->inputs.size(); ++i) {
      out += std::to_string(cell.function->inputs[i]) + '@' + std::to_string(cell.producers[i]) + ',';
    }
    for (const auto& node : cell.function->formula.nodes) {
      out += std::to_string(static_cast<int>(node.kind)) + '.' + std::to_string(node.left) + '.' + std::to_string(node.right) + '.'
             + std::to_string(node.variable) + (node.inverted ? "~" : "") + ';';
    }
    out += cell.function->formula.output_inverted ? "!" : "";
  }
  return out;
}
std::string describe(const Endpoint_result& r) {
  std::string out = std::string(status_name(r.status)) + ' ' + r.reason + '\n';
  if (r.selected) {
    out += describe(*r.selected) + '\n';
  }
  for (const auto& choice : r.choices) {
    out += "choice " + describe(choice) + '\n';
  }
  return out;
}
}  // namespace

// Credit floor tracking (unate.hpp): any credits at or above an unbound
// search's recorded floor replay the identical search -- the same selection,
// report and consumed work -- and record the same floor again.
TEST(Endpoint, SearchReplaysIdenticallyAtItsCreditFloorAndAbove) {
  uint32_t   random = 0x5eed;
  const auto next   = [&](uint32_t n) {
    random = random * 1664525U + 1013904223U;
    return (random >> 8) % n;
  };
  uint32_t unbound = 0, bound = 0, below = 0;
  for (uint32_t trial = 0; trial < 24; ++trial) {
    Xag                  g;
    std::vector<Xsignal> pool;
    for (uint32_t i = 0; i < 6; ++i) {
      pool.push_back(g.input(std::to_string(i)));
    }
    for (uint32_t i = 0; i < 22; ++i) {
      auto a = pool[next(pool.size())], b = pool[next(pool.size())];
      a = next(2) ? ~a : a;
      b = next(2) ? ~b : b;
      pool.push_back(next(2) ? g.land(a, b) : g.lxor(a, b));
    }
    const auto                 root = pool.back();
    const std::vector<Xsignal> outside{pool[pool.size() - 3]};
    Endpoint_options           o = small_gates();
    o.gates                      = {3, 3, 4};
    o.clock_phases               = 1 + trial % 2;
    o.fast_accept                = trial % 3 != 0;
    o.boundaries                 = 4 + trial % 5;
    SCOPED_TRACE(trial);
    for (const uint64_t credits : {uint64_t{40000000}, uint64_t{2000000}, uint64_t{200000}, uint64_t{20000}}) {
      Budget     work{credits};
      const auto cold     = select_endpoint(g, root, "q", outside, {}, o, work, {}, trial % 4);
      const auto recorded = work.credit_floor();
      ASSERT_EQ(recorded.work, credits - work.remaining);
      ASSERT_LE(recorded.work, recorded.floor);
      ASSERT_LE(recorded.floor, credits);
      if (recorded.bound) {
        ++bound;
        continue;  // replays only under exactly these credits
      }
      ++unbound;
      below += recorded.floor < credits;
      for (const auto other : {recorded.floor, recorded.floor + 17, 3 * credits}) {
        Budget     again{other};
        const auto replay = select_endpoint(g, root, "q", outside, {}, o, again, {}, trial % 4);
        ASSERT_EQ(describe(replay), describe(cold)) << credits << " -> " << other;
        ASSERT_EQ(replay.report, cold.report) << credits << " -> " << other;
        ASSERT_EQ(again.credit_floor(), recorded) << credits << " -> " << other;
      }
    }
  }
  // Plentiful credits leave most searches unbound with a floor below them.
  EXPECT_GT(unbound, 40U);
  EXPECT_GT(below, 30U);
  EXPECT_GT(bound, 0U);
}
}  // namespace livehd::usyn
