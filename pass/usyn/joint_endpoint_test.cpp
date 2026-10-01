// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "joint_endpoint.hpp"

#include <algorithm>
#include <bit>

#include "gtest/gtest.h"

namespace livehd::usyn {
namespace {
struct Fixture {
  Xag                        graph;
  Pair_windows               windows;
  std::array<Truth_table, 2> functions;
  Endpoint_options           options;
  Fixture() {
    const auto       a = graph.input("a"), b = graph.input("b"), c = graph.input("c"), d = graph.input("d");
    const auto       shared = graph.lxor(a, b);
    const std::array roots{graph.land(shared, c), graph.lor(shared, d)};
    Budget           work{100000};
    windows.status = Status::feasible;
    windows.basis  = {a.id, b.id, c.id, d.id};
    for (size_t i = 0; i < roots.size(); ++i) {
      windows.windows[i] = whole_cone(graph, roots[i], {3, 100}, work);
      functions[i]       = basis_function(graph, roots[i], windows.basis, {4, 100}, work).table;
    }
    options.gates  = {2, 2, 2};
    options.window = {3, 100};
  }
};

// Three reachable classes of (a,b): 00, 01/10, and 11. The fourth
// binary code is unreachable, so each endpoint can choose its value there.
struct Care_fixture {
  Xag                        graph;
  Pair_windows               windows;
  std::array<Truth_table, 2> functions;
  Endpoint_options           options;
  Care_fixture() {
    const auto       a = graph.input("a"), b = graph.input("b"), c = graph.input("c"), d = graph.input("d");
    const std::array roots{graph.land(graph.land(a, b), c), graph.land(graph.lor(a, b), d)};
    Budget           work{100000};
    windows.status = Status::feasible;
    windows.basis  = {a.id, b.id, c.id, d.id};
    for (size_t i = 0; i < roots.size(); ++i) {
      windows.windows[i] = whole_cone(graph, roots[i], {3, 100}, work);
      functions[i]       = basis_function(graph, roots[i], windows.basis, {4, 100}, work).table;
    }
    options.gates  = {3, 2, 2};
    options.window = {3, 100};
  }
};
}  // namespace

TEST(JointEndpoint, CareCompletionMakesBothTopsLegalAndDropsAnUnusedProducer) {
  Care_fixture f;
  Budget       baseline_work{1000000};
  const auto baseline = synthesize_joint_endpoints(f.graph, f.windows, f.functions, {"left", "right"}, 3, f.options, baseline_work);
  EXPECT_FALSE(baseline.endpoints);
  Budget     work{1000000};
  const auto choices = complete_joint_endpoints(f.graph, f.windows, f.functions, {"left", "right"}, 3, f.options, 4, work);
  ASSERT_EQ(choices.status, Status::feasible);
  EXPECT_GT(choices.phases, 0U);
  EXPECT_GE(choices.attempts, choices.retained);
  EXPECT_LE(choices.bytes, f.options.boundary_bytes);
  for (size_t root = 0; root < 2; ++root) {
    ASSERT_FALSE(choices.endpoints[root].empty());
    EXPECT_LE(choices.endpoints[root].size(), 5U);
    for (const auto& endpoint : choices.endpoints[root]) {
      EXPECT_EQ(endpoint.origin, "joint-care");
      EXPECT_TRUE(validate_endpoint(f.graph, endpoint, f.options, work));
    }
  }
  const auto compact = std::find_if(choices.endpoints[0].begin(), choices.endpoints[0].end(), [](const auto& endpoint) {
    return endpoint.cells.size() == 2;
  });
  ASSERT_NE(compact, choices.endpoints[0].end());
  const auto& left  = *compact;
  const auto& right = choices.endpoints[1].front();
  ASSERT_EQ(left.cells.size(), 2U);
  ASSERT_EQ(right.cells.size(), 3U);
  EXPECT_TRUE(std::any_of(right.cells.begin(), right.cells.end() - 1, [&](const auto& cell) {
    return cell.function == left.cells[0].function;
  }));
}

TEST(JointEndpoint, CareCompletionSkipsFullImagesAndHonorsChoiceAndStorageLimits) {
  Fixture    full;
  Budget     work{1000000};
  const auto skipped
      = complete_joint_endpoints(full.graph, full.windows, full.functions, {"left", "right"}, 3, full.options, 4, work);
  EXPECT_EQ(skipped.status, Status::unsupported);
  EXPECT_EQ(skipped.phases, 0U);
  EXPECT_EQ(skipped.retained, 0U);
  Care_fixture f;
  for (uint64_t cap : {1U, 2048U, 4096U, 8192U, 65536U}) {
    SCOPED_TRACE(cap);
    f.options.boundary_bytes = cap;
    Budget     limited{1000000};
    const auto choices = complete_joint_endpoints(f.graph, f.windows, f.functions, {"left", "right"}, 3, f.options, 1, limited);
    EXPECT_LE(choices.bytes, cap);
    for (const auto& pool : choices.endpoints) {
      EXPECT_LE(pool.size(), 2U);
      for (const auto& endpoint : pool) {
        Budget validate{1000000};
        EXPECT_TRUE(validate_endpoint(f.graph, endpoint, f.options, validate));
      }
    }
    if (cap == 1) {
      EXPECT_EQ(choices.status, Status::search_exhausted);
      EXPECT_TRUE(choices.exhausted);
      EXPECT_EQ(choices.retained, 0U);
    }
    if (cap == 65536) {
      EXPECT_EQ(choices.status, Status::feasible);
      EXPECT_FALSE(choices.endpoints[0].empty());
      EXPECT_FALSE(choices.endpoints[1].empty());
    }
  }
}

TEST(JointEndpoint, CareCompletionKeepsOnlyValidatedEntriesAfterLateCancellation) {
  Care_fixture f;
  Budget       measured{1000000};
  uint64_t     samples        = 0;
  measured.admission_interval = 1;
  measured.admission          = [&] {
    ++samples;
    return true;
  };
  const auto full = complete_joint_endpoints(f.graph, f.windows, f.functions, {"left", "right"}, 3, f.options, 4, measured);
  ASSERT_EQ(full.status, Status::feasible);
  ASSERT_GT(samples, 1U);
  Budget   cancelled{1000000};
  uint64_t calls               = 0;
  cancelled.admission_interval = 1;
  cancelled.admission          = [&] { return ++calls < samples; };
  const auto partial = complete_joint_endpoints(f.graph, f.windows, f.functions, {"left", "right"}, 3, f.options, 4, cancelled);
  EXPECT_TRUE(cancelled.resource_exhausted);
  EXPECT_TRUE(partial.exhausted);
  EXPECT_EQ(partial.status, Status::feasible);
  EXPECT_GT(partial.retained, 0U);
  EXPECT_LE(partial.retained, full.retained);
  for (const auto& pool : partial.endpoints) {
    for (const auto& endpoint : pool) {
      Budget validate{1000000};
      EXPECT_TRUE(validate_endpoint(f.graph, endpoint, f.options, validate));
    }
  }
}

TEST(JointEndpoint, CareCompletionHonorsDisabledSearchAndWorkAdmission) {
  Care_fixture f;
  Budget       work{1000000};
  f.options.care_phases = 0;
  EXPECT_EQ(complete_joint_endpoints(f.graph, f.windows, f.functions, {"left", "right"}, 3, f.options, 4, work).status,
            Status::invalid);
  EXPECT_EQ(work.remaining, 1000000U);
  f.options.care_phases = 16;
  for (uint32_t choices : {0U, 9U}) {
    EXPECT_EQ(complete_joint_endpoints(f.graph, f.windows, f.functions, {"left", "right"}, 3, f.options, choices, work).status,
              Status::invalid);
    EXPECT_EQ(work.remaining, 1000000U);
  }
  for (uint64_t cap : {0U, 16U, 128U, 512U}) {
    SCOPED_TRACE(cap);
    Budget     limited{cap};
    const auto result = complete_joint_endpoints(f.graph, f.windows, f.functions, {"left", "right"}, 3, f.options, 4, limited);
    EXPECT_TRUE(result.exhausted);
    EXPECT_EQ(result.status, Status::search_exhausted);
    EXPECT_EQ(result.retained, 0U);
    EXPECT_LE(result.bytes, f.options.boundary_bytes);
  }
}

TEST(JointEndpoint, CareCompletionCanDiscardAnUnrealizableSharedProducer) {
  Xag              graph;
  const auto       a = graph.input("a"), b = graph.input("b"), c = graph.input("c");
  const auto       d = graph.input("d"), e = graph.input("e");
  // f and g are mutually exclusive, giving three joint cofactor classes.
  // The code bit for g cannot meet the stack/branch limits. Completing the
  // first top to depend only on f must discard g before testing availability.
  const auto       f = graph.land(graph.lor(a, b), c);
  const auto       g = graph.land(graph.lxor(a, b), ~c);
  const std::array roots{graph.land(f, d), graph.land(g, e)};
  Pair_windows     windows;
  windows.status = Status::feasible;
  windows.basis  = {a.id, b.id, c.id, d.id, e.id};
  std::array<Truth_table, 2> functions;
  Budget                     collect{100000};
  for (size_t i = 0; i < roots.size(); ++i) {
    windows.windows[i] = whole_cone(graph, roots[i], {4, 100}, collect);
    functions[i]       = basis_function(graph, roots[i], windows.basis, {5, 100}, collect).table;
  }
  Endpoint_options options;
  options.gates  = {3, 2, 2};
  options.window = {4, 100};
  Budget     work{1000000};
  const auto g_function = basis_function(graph, g, std::array{a.id, b.id, c.id}, {3, 100}, work);
  ASSERT_EQ(g_function.status, Status::feasible);
  EXPECT_FALSE(synthesize_gate(g_function.table, options.gates, work, options.functions).formula);
  const auto result = complete_joint_endpoints(graph, windows, functions, {"left", "right"}, 7, options, 4, work);
  ASSERT_EQ(result.status, Status::feasible);
  ASSERT_FALSE(result.endpoints[0].empty());
  EXPECT_TRUE(result.endpoints[1].empty());
  for (const auto& endpoint : result.endpoints[0]) {
    EXPECT_EQ(endpoint.cells.size(), 2U);
    EXPECT_EQ(endpoint.origin, "joint-care");
    EXPECT_TRUE(validate_endpoint(graph, endpoint, options, work));
  }
}

TEST(JointEndpoint, SharesOneSynthesizedCellWithIndependentPrivateInputsAndValidation) {
  Fixture    f;
  Budget     work{1000000};
  const auto result = synthesize_joint_endpoints(f.graph, f.windows, f.functions, {"left", "right"}, 3, f.options, work);
  ASSERT_EQ(result.status, Status::feasible);
  ASSERT_TRUE(result.endpoints);
  EXPECT_EQ(result.shared_divisors, 1U);
  const auto& endpoints = *result.endpoints;
  for (size_t i = 0; i < endpoints.size(); ++i) {
    const auto& endpoint = endpoints[i];
    EXPECT_EQ(endpoint.name, i ? "right" : "left");
    ASSERT_EQ(endpoint.cells.size(), 2U);
    EXPECT_EQ(endpoint.cells[0].phase, 1U);
    EXPECT_FALSE(endpoint.cells[0].latch);
    EXPECT_FALSE(endpoint.cells[0].function->root);
    EXPECT_EQ(endpoint.cells[1].phase, 2U);
    EXPECT_TRUE(endpoint.cells[1].latch);
    EXPECT_EQ(endpoint.functional_basis, f.windows.windows[i].leaves);
    EXPECT_TRUE(validate_endpoint(f.graph, endpoint, f.options, work));
  }
  EXPECT_EQ(endpoints[0].cells[0].function, endpoints[1].cells[0].function);
  EXPECT_EQ(endpoints[0].cells[0].producers, (std::vector<int32_t>{-1, -1}));
  // Every source assignment is checked by the validator on each root's own
  // three-input cut, although the common analysis basis has four inputs.
  auto bad                  = endpoints[0];
  bad.cells[0].producers[0] = 1;
  EXPECT_FALSE(validate_endpoint(f.graph, bad, f.options, work));
  auto changed     = f.functions;
  changed[1]       = changed[1].complement();
  const auto wrong = synthesize_joint_endpoints(f.graph, f.windows, changed, {"left", "right"}, 3, f.options, work);
  EXPECT_FALSE(wrong.endpoints);  // supplied tables cannot bypass source validation
}

TEST(JointEndpoint, RecodingMakesFullImageTopsLegalEvenWithCareSearchDisabled) {
  Xag              graph;
  const auto       a = graph.input("a"), b = graph.input("b"), c = graph.input("c"), d = graph.input("d");
  const auto       parity = graph.lxor(a, b);
  const std::array roots{graph.land(parity, c), graph.lor(graph.land(parity, d), graph.land(b, ~d))};
  Pair_windows     windows;
  windows.status = Status::feasible;
  windows.basis  = {a.id, b.id, c.id, d.id};
  std::array<Truth_table, 2> functions;
  Budget                     work{1000000};
  for (size_t i = 0; i < roots.size(); ++i) {
    windows.windows[i] = whole_cone(graph, roots[i], {4, 100}, work);
    functions[i]       = basis_function(graph, roots[i], windows.basis, {4, 100}, work).table;
  }
  Endpoint_options options;
  options.gates            = {3, 2, 2};
  options.window           = {4, 100};
  options.care_phases      = 0;
  const auto original_code = decompose_pair(functions, 3, 4, work);
  ASSERT_EQ(original_code.status, Status::feasible);
  EXPECT_EQ(original_code.top_care, Truth_table(original_code.top_care.inputs, true));
  EXPECT_FALSE(synthesize_joint_endpoints(graph, windows, functions, {"left", "right"}, 3, options, work).endpoints);
  const auto recoded = recode_joint_endpoints(graph, windows, functions, {"left", "right"}, 3, {0, 1}, options, 4, work);
  ASSERT_EQ(recoded.status, Status::feasible);
  EXPECT_EQ(recoded.code_bits, 2U);
  EXPECT_EQ(recoded.phases, 0U);
  EXPECT_EQ(recoded.retained, 2U);
  for (const auto& pool : recoded.endpoints) {
    ASSERT_EQ(pool.size(), 1U);
    EXPECT_EQ(pool[0].origin, "joint-recode");
    EXPECT_TRUE(validate_endpoint(graph, pool[0], options, work));
  }
  EXPECT_EQ(recoded.endpoints[0][0].cells[0].function, recoded.endpoints[1][0].cells[0].function);
  const auto absent = recode_joint_endpoints(graph, windows, functions, {"left", "right"}, 3, {0, 2}, options, 4, work);
  EXPECT_EQ(absent.status, Status::unsupported);
  EXPECT_EQ(absent.code_bits, 2U);
  EXPECT_EQ(absent.retained, 0U);
  options.boundary_bytes = 1;
  const auto refused     = recode_joint_endpoints(graph, windows, functions, {"left", "right"}, 3, {0, 1}, options, 4, work);
  EXPECT_EQ(refused.status, Status::search_exhausted);
  EXPECT_EQ(refused.retained, 0U);
}

TEST(JointEndpoint, RecodingPreservesCareCompletionsAndValidatedEntriesOnCancellation) {
  Care_fixture f;
  Budget       measured{1000000};
  uint64_t     samples        = 0;
  measured.admission_interval = 1;
  measured.admission          = [&] {
    ++samples;
    return true;
  };
  const auto full = recode_joint_endpoints(f.graph, f.windows, f.functions, {"left", "right"}, 3, {0, 1}, f.options, 4, measured);
  ASSERT_EQ(full.status, Status::feasible);
  EXPECT_EQ(full.code_bits, 2U);
  EXPECT_GT(full.phases, 0U);
  EXPECT_LE(full.bytes, f.options.boundary_bytes);
  ASSERT_FALSE(full.endpoints[0].empty());
  ASSERT_FALSE(full.endpoints[1].empty());
  EXPECT_TRUE(std::any_of(full.endpoints[0].begin(), full.endpoints[0].end(), [](const auto& endpoint) {
    return endpoint.origin == "joint-recode-care";
  }));
  ASSERT_GT(samples, 1U);
  Budget   cancelled{1000000};
  uint64_t calls               = 0;
  cancelled.admission_interval = 1;
  cancelled.admission          = [&] { return ++calls < samples; };
  const auto partial
      = recode_joint_endpoints(f.graph, f.windows, f.functions, {"left", "right"}, 3, {0, 1}, f.options, 4, cancelled);
  EXPECT_TRUE(cancelled.resource_exhausted);
  EXPECT_TRUE(partial.exhausted);
  EXPECT_EQ(partial.status, Status::feasible);
  EXPECT_GT(partial.retained, 0U);
  for (const auto* result : {&full, &partial}) {
    for (const auto& pool : result->endpoints) {
      EXPECT_LE(pool.size(), 5U);
      for (const auto& endpoint : pool) {
        Budget validate{1000000};
        EXPECT_TRUE(validate_endpoint(f.graph, endpoint, f.options, validate));
      }
    }
  }
}

TEST(JointEndpoint, WideCofactorWorkspaceIsRefusedBeforeConstruction) {
  Xag          graph;
  Pair_windows windows;
  auto         all = graph.constant(true), parity = graph.constant(false);
  for (uint32_t i = 0; i < 16; ++i) {
    const auto source = graph.input(std::to_string(i));
    windows.basis.push_back(source.id);
    all    = graph.land(all, source);
    parity = graph.lxor(parity, source);
  }
  Budget collect{100000};
  windows.status     = Status::feasible;
  windows.windows[0] = whole_cone(graph, all, {16, 100}, collect);
  windows.windows[1] = whole_cone(graph, parity, {16, 100}, collect);
  std::array functions{Truth_table(16), Truth_table(16)};
  for (uint32_t x = 0; x < 65536; ++x) {
    functions[0].set(x, x == 65535);
    functions[1].set(x, std::popcount(x) & 1);
  }
  Endpoint_options options;
  options.boundary_bytes = 100000;
  Budget     work{1000000};
  // Two bound inputs leave wide free-variable cofactors. Include both top
  // tables, their care relation, all divisor tables and representative storage
  // in admission, rather than starting construction with an incomplete estimate.
  const auto refused = synthesize_joint_endpoints(graph, windows, functions, {"all", "parity"}, 3, options, work);
  EXPECT_EQ(refused.status, Status::search_exhausted);
  EXPECT_TRUE(refused.exhausted);
  EXPECT_FALSE(refused.endpoints);
  EXPECT_EQ(work.remaining, 1000000U);
}

TEST(JointEndpoint, RejectsPrivateBoundInputsAndPreservesAtomicityOnResourceRefusal) {
  Fixture    f;
  Budget     work{1000000};
  const auto private_input = synthesize_joint_endpoints(f.graph, f.windows, f.functions, {"left", "right"}, 5, f.options, work);
  EXPECT_EQ(private_input.status, Status::invalid);
  EXPECT_FALSE(private_input.endpoints);
  f.options.clock_phases = 1;
  EXPECT_EQ(synthesize_joint_endpoints(f.graph, f.windows, f.functions, {"left", "right"}, 3, f.options, work).status,
            Status::invalid);
  f.options.clock_phases   = 2;
  f.options.boundary_bytes = 1;
  const auto memory        = synthesize_joint_endpoints(f.graph, f.windows, f.functions, {"left", "right"}, 3, f.options, work);
  EXPECT_EQ(memory.status, Status::search_exhausted);
  EXPECT_FALSE(memory.endpoints);
  f.options.boundary_bytes = 8 * 1024 * 1024;
  for (uint64_t cap : {0U, 16U, 128U, 512U}) {
    Budget     limited{cap};
    const auto refused = synthesize_joint_endpoints(f.graph, f.windows, f.functions, {"left", "right"}, 3, f.options, limited);
    EXPECT_TRUE(refused.exhausted);
    EXPECT_FALSE(refused.endpoints);
  }
  Budget   measured{1000000};
  uint64_t samples            = 0;
  measured.admission_interval = 1;
  measured.admission          = [&] {
    ++samples;
    return true;
  };
  ASSERT_TRUE(synthesize_joint_endpoints(f.graph, f.windows, f.functions, {"left", "right"}, 3, f.options, measured).endpoints);
  ASSERT_GT(samples, 1U);
  Budget   cancelled{1000000};
  uint64_t calls               = 0;
  cancelled.admission_interval = 1;
  cancelled.admission          = [&] { return ++calls < samples; };
  const auto partial = synthesize_joint_endpoints(f.graph, f.windows, f.functions, {"left", "right"}, 3, f.options, cancelled);
  EXPECT_TRUE(cancelled.resource_exhausted);
  EXPECT_EQ(partial.status, Status::search_exhausted);
  EXPECT_FALSE(partial.endpoints);
}
}  // namespace livehd::usyn
