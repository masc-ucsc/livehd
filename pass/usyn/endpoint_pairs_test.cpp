// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "endpoint_pairs.hpp"

#include <algorithm>

#include "gtest/gtest.h"

namespace livehd::usyn {
TEST(EndpointPairs, DistinctLabelsSaturateAndClockDomainsMustMatch) {
  Xag                        g;
  const auto                 a = g.input("a"), b = g.input("b"), c = g.input("c");
  const auto                 shared = g.lxor(a, b), root = g.land(shared, c);
  std::vector<Pair_endpoint> endpoints{
      {{root, root}, 0},
      {    {shared}, 0}
  };
  Budget     work{10000};
  const auto pair = find_endpoint_pairs(g, endpoints, 32, 100, work);
  ASSERT_EQ(pair.status, Status::feasible);
  EXPECT_EQ(pair.pairs,
            (std::vector<std::array<uint32_t, 2>>{
                {0, 1}
  }));
  EXPECT_EQ(pair.shared_nodes, 1U);
  for (const auto domain : {1U, unknown_clock_domain}) {
    endpoints[1].domain = domain;
    const auto skipped  = find_endpoint_pairs(g, endpoints, 32, 100, work);
    EXPECT_TRUE(skipped.pairs.empty());
    EXPECT_EQ(skipped.domain_skips, 1U);
  }
  endpoints[1].domain = 0;
  endpoints.push_back({{~shared}, 0});
  const auto saturated = find_endpoint_pairs(g, endpoints, 32, 100, work);
  EXPECT_TRUE(saturated.pairs.empty());
  EXPECT_EQ(saturated.more_than_two, 1U);
}

TEST(EndpointPairs, JointDiscoveryCanUseTwoCommonSourcesWithoutAnExistingSharedGate) {
  Xag                        g;
  const auto                 a = g.input("a"), b = g.input("b"), c = g.input("c"), d = g.input("d");
  const auto                 left = g.land(a, g.land(b, c)), right = g.lor(a, g.land(b, d));
  std::vector<Pair_endpoint> endpoints{
      { {left}, 0},
      {{right}, 0}
  };
  Budget work{100000};
  EXPECT_TRUE(find_endpoint_pairs(g, endpoints, 32, 100, work).pairs.empty());
  const auto joint = find_endpoint_pairs(g, endpoints, 32, 100, work, true);
  ASSERT_EQ(joint.status, Status::feasible);
  EXPECT_EQ(joint.pairs,
            (std::vector<Endpoint_pair>{
                {0, 1}
  }));
  EXPECT_EQ(joint.shared_nodes, 0U);
  EXPECT_EQ(joint.source_pairs, 1U);
  endpoints[1].inputs = {g.lor(a, d)};
  EXPECT_TRUE(find_endpoint_pairs(g, endpoints, 32, 100, work, true).pairs.empty());
  endpoints[1].inputs = {right};
  endpoints[1].domain = 1;
  EXPECT_TRUE(find_endpoint_pairs(g, endpoints, 32, 100, work, true).pairs.empty());
  endpoints[1].domain = unknown_clock_domain;
  EXPECT_TRUE(find_endpoint_pairs(g, endpoints, 32, 100, work, true).pairs.empty());
  endpoints[1].domain = 0;
  endpoints.push_back({
      {a, b},
      0
  });
  EXPECT_TRUE(find_endpoint_pairs(g, endpoints, 32, 100, work, true).pairs.empty());
  Budget tiny{1};
  EXPECT_EQ(find_endpoint_pairs(g, endpoints, 32, 100, tiny, true).status, Status::search_exhausted);
}

TEST(EndpointPairs, CommonWindowGrowsBothRootsWithinJointAndIndividualCaps) {
  Xag              g;
  const auto       a = g.input("a"), b = g.input("b"), c = g.input("c"), d = g.input("d");
  const auto       e = g.input("e"), f = g.input("f");
  const auto       ab = g.land(a, b), cd = g.land(c, d), shared = g.lxor(ab, cd);
  const std::array roots{g.land(shared, e), g.land(shared, f)};
  Budget           work{100000};
  const auto       result = grow_pair_windows(g, roots, {3, 100}, 4, work);
  ASSERT_EQ(result.status, Status::feasible);
  EXPECT_FALSE(result.exhausted);
  EXPECT_EQ(result.basis, (std::vector<Id>{e.id, f.id, ab.id, cd.id}));
  for (size_t i = 0; i < roots.size(); ++i) {
    EXPECT_EQ(result.windows[i].root, roots[i]);
    EXPECT_EQ(result.windows[i].leaves.size(), 3U);
    EXPECT_FALSE(result.windows[i].whole_cone);
    const auto checked = collect_window(g, roots[i], result.windows[i].leaves, {3, 100}, work);
    ASSERT_EQ(checked.status, Status::feasible);
    EXPECT_EQ(checked.interior, result.windows[i].interior);
  }
  Budget     smaller_work{100000};
  const auto smaller = grow_pair_windows(g, roots, {2, 100}, 4, smaller_work);
  ASSERT_EQ(smaller.status, Status::feasible);
  for (const auto& window : smaller.windows) {
    EXPECT_LE(window.leaves.size(), 2U);
  }
  EXPECT_LE(smaller.basis.size(), 4U);
  Budget     node_work{100000};
  const auto nodes = grow_pair_windows(g, roots, {3, 2}, 4, node_work);
  ASSERT_EQ(nodes.status, Status::feasible);
  EXPECT_LE(nodes.windows[0].interior.size() + nodes.windows[1].interior.size(), 2U);
  Budget     missing_work{100};
  const auto missing = grow_pair_windows(g, roots, {3, 100}, 1, missing_work);
  EXPECT_EQ(missing.status, Status::search_exhausted);
  EXPECT_TRUE(missing.exhausted);
  Budget tiny{1};
  EXPECT_EQ(grow_pair_windows(g, roots, {3, 100}, 4, tiny).status, Status::search_exhausted);
  bool retained = false;
  for (uint64_t cap : {32U, 64U, 128U}) {
    Budget     limited{cap};
    const auto bounded = grow_pair_windows(g, roots, {3, 100}, 4, limited);
    if (bounded.status == Status::feasible && bounded.exhausted) {
      retained = true;
      for (size_t i = 0; i < roots.size(); ++i) {
        Budget check{10000};
        EXPECT_EQ(collect_window(g, roots[i], bounded.windows[i].leaves, {3, 100}, check).status, Status::feasible);
      }
    }
  }
  EXPECT_TRUE(retained);
}

TEST(EndpointPairs, FanoutFreeWindowRetainsLiveOutsidePortsAndIgnoresDeadFanouts) {
  Xag              g;
  const auto       a = g.input("a"), b = g.input("b"), c = g.input("c"), d = g.input("d"), e = g.input("e");
  const auto       ab = g.land(a, b), shared = g.lxor(ab, c);
  const std::array roots{g.land(shared, d), g.lor(shared, e)};
  const auto       outside = g.land(ab, e);
  const auto       dead    = g.lxor(shared, e);
  EXPECT_GT(g.node(shared.id).fanouts, 2U);
  Budget     work{100000};
  const auto result = fanout_free_pair_windows(g, roots, std::array{~outside}, {3, 100}, 4, 100, work);
  ASSERT_EQ(result.status, Status::feasible) << result.reason;
  EXPECT_FALSE(result.exhausted);
  EXPECT_EQ(result.basis, (std::vector<Id>{c.id, d.id, e.id, ab.id}));
  EXPECT_EQ(result.outside_boundary, (std::vector<Id>{ab.id}));
  for (size_t i = 0; i < roots.size(); ++i) {
    EXPECT_FALSE(result.windows[i].whole_cone);
    EXPECT_EQ(result.windows[i].leaves.size(), 3U);
    EXPECT_NE(std::find(result.windows[i].interior.begin(), result.windows[i].interior.end(), shared.id),
              result.windows[i].interior.end());
    const auto checked = collect_window(g, roots[i], result.windows[i].leaves, {3, 100}, work);
    EXPECT_EQ(checked.status, Status::feasible);
    EXPECT_EQ(checked.interior, result.windows[i].interior);
    // Check the independent boundary truth functions, including complemented
    // root edges. Upstream ab remains an opaque input with both values legal.
    const auto function = basis_function(g, roots[i], result.basis, {4, 100}, work);
    ASSERT_EQ(function.status, Status::feasible);
    for (uint32_t x = 0; x < 16; ++x) {
      const bool value = bool(x & 8) != bool(x & 1);
      EXPECT_EQ(function.table.get(x), i == 0 ? value && bool(x & 2) : value || bool(x & 4));
    }
  }
  // Making the formerly dead reader live changes exactly the stopping port.
  const auto live = fanout_free_pair_windows(g, roots, std::array{dead}, {3, 100}, 4, 100, work);
  ASSERT_EQ(live.status, Status::feasible);
  EXPECT_EQ(live.outside_boundary, (std::vector<Id>{shared.id}));
  EXPECT_EQ(live.basis, (std::vector<Id>{d.id, e.id, shared.id}));
  // A direct outside reader protects the root implementation too.
  const auto root_port = fanout_free_pair_windows(g, roots, std::array{~roots[0]}, {3, 100}, 4, 100, work);
  ASSERT_EQ(root_port.status, Status::feasible);
  EXPECT_TRUE(root_port.windows[0].interior.empty());
  EXPECT_EQ(root_port.windows[0].leaves, (std::vector<Id>{roots[0].id}));
}

TEST(EndpointPairs, FanoutFreeWindowRefusalNeverPublishesPartialCones) {
  Xag              g;
  const auto       a = g.input("a"), b = g.input("b"), c = g.input("c"), d = g.input("d");
  const auto       ab = g.land(a, b);
  const std::array roots{g.lxor(ab, c), g.land(ab, d)};
  for (uint32_t mode = 0; mode < 5; ++mode) {
    SCOPED_TRACE(mode);
    Budget     work{mode == 0 ? 1U : 100000U};
    const auto refused = fanout_free_pair_windows(g,
                                                  roots,
                                                  std::array{ab},
                                                  {mode == 1 ? 1U : 3U, mode == 2 ? 1U : 100U},
                                                  mode == 3 ? 2U : 4U,
                                                  mode == 4 ? 2U : 100U,
                                                  work);
    EXPECT_EQ(refused.status, Status::search_exhausted);
    EXPECT_TRUE(refused.exhausted);
    EXPECT_TRUE(refused.basis.empty());
    EXPECT_TRUE(refused.outside_boundary.empty());
    EXPECT_TRUE(refused.windows[0].interior.empty());
    EXPECT_TRUE(refused.windows[1].interior.empty());
  }
  Budget     work{100000};
  const auto invalid = fanout_free_pair_windows(g,
                                                roots,
                                                std::array{
                                                    Xsignal{999, false}
  },
                                                {3, 100},
                                                4,
                                                100,
                                                work);
  EXPECT_EQ(invalid.status, Status::invalid);
  EXPECT_TRUE(invalid.basis.empty());
  const auto complete = fanout_free_pair_windows(g, roots, {}, {3, 100}, 4, 100, work);
  ASSERT_EQ(complete.status, Status::feasible);
  EXPECT_TRUE(complete.outside_boundary.empty());
  EXPECT_TRUE(complete.windows[0].whole_cone);
  EXPECT_TRUE(complete.windows[1].whole_cone);
  Budget   cancelled{100000};
  uint64_t calls               = 0;
  cancelled.admission_interval = 1;
  cancelled.admission          = [&] { return ++calls < 8; };
  const auto refused           = fanout_free_pair_windows(g, roots, std::array{ab}, {3, 100}, 4, 100, cancelled);
  EXPECT_EQ(refused.status, Status::search_exhausted);
  EXPECT_TRUE(cancelled.resource_exhausted);
  EXPECT_TRUE(refused.basis.empty());
}

TEST(EndpointPairs, CandidateStorageAndTraversalAreBounded) {
  Xag                              g;
  const auto                       a = g.input("a"), b = g.input("b"), c = g.input("c");
  const auto                       s = g.land(a, b), t = g.land(a, c);
  const std::vector<Pair_endpoint> endpoints{
      {   {s}, 0},
      {{s, t}, 0},
      {   {t}, 0}
  };
  Budget     work{10000};
  const auto result = find_endpoint_pairs(g, endpoints, 1, 100, work);
  ASSERT_EQ(result.status, Status::feasible);
  EXPECT_EQ(result.pairs,
            (std::vector<std::array<uint32_t, 2>>{
                {1, 2}
  }));
  EXPECT_TRUE(result.capped);
  Budget     tiny{2};
  const auto exhausted = find_endpoint_pairs(g, endpoints, 32, 100, tiny);
  EXPECT_EQ(exhausted.status, Status::search_exhausted);
  EXPECT_TRUE(exhausted.pairs.empty());
  EXPECT_EQ(find_endpoint_pairs(g, endpoints, 32, 2, work).status, Status::search_exhausted);
  const std::vector<Pair_endpoint> sources{
      {{a, b}, 0},
      {{a, b}, 0}
  };
  EXPECT_TRUE(find_endpoint_pairs(g, sources, 32, 100, work).pairs.empty());
}
TEST(EndpointPairs, DependentsTrackPaidSharingAndInverseRailsWithoutMarkingEveryCommonInput) {
  Xag                              g;
  const auto                       a = g.input("a"), b = g.input("b"), c = g.input("c");
  const auto                       shared = g.land(a, b), reader = g.land(shared, c), unrelated = g.land(a, c);
  const std::vector<Pair_endpoint> endpoints{
      {   {reader}, 0},
      {{unrelated}, 0},
      {        {a}, 0}
  };
  Budget     work{10000};
  const auto gates = pair_dependents(g, std::array{shared}, endpoints, 100, work);
  ASSERT_EQ(gates.status, Status::feasible);
  EXPECT_EQ(gates.endpoints, (std::vector<uint8_t>{1, 0, 0}));
  const auto positive = pair_dependents(g, std::array{a}, endpoints, 100, work);
  EXPECT_EQ(positive.endpoints, (std::vector<uint8_t>{0, 0, 0}));
  const auto inverse = pair_dependents(g, std::array{~a}, endpoints, 100, work);
  EXPECT_EQ(inverse.endpoints, (std::vector<uint8_t>{1, 1, 1}));
  Budget     tiny{1};
  const auto refused = pair_dependents(g, std::array{shared}, endpoints, 100, tiny);
  EXPECT_EQ(refused.status, Status::search_exhausted);
  EXPECT_TRUE(refused.endpoints.empty());
}

TEST(EndpointPairs, QueueRetriesAffectedFailuresAndPreservesPendingWorkAcrossUnrelatedCommits) {
  const Endpoint_pair a{0, 1}, b{2, 3}, c{4, 5};
  Pair_queue          queue(3, 5);
  Budget              work{10000};
  ASSERT_EQ(queue.refresh(std::array{a, b, c}, {}, work), Status::feasible);
  EXPECT_EQ(queue.pop(work), a);  // failed candidate
  EXPECT_EQ(queue.pop(work), b);  // committed candidate
  const std::array<uint8_t, 6> affected_a{1, 0, 0, 0, 0, 0};
  ASSERT_EQ(queue.refresh(std::array{c, a, b}, affected_a, work), Status::feasible);
  EXPECT_EQ(queue.report().requeues, 1U);
  EXPECT_EQ(queue.pop(work), c);  // a remains dirty and queued
  const std::array<uint8_t, 6> unrelated{0, 0, 0, 0, 1, 1};
  ASSERT_EQ(queue.refresh(std::array{a, b}, unrelated, work), Status::feasible);
  EXPECT_EQ(queue.pop(work), a);
  EXPECT_FALSE(queue.pop(work));  // unchanged failed b was not retried
  EXPECT_EQ(queue.report().trials, 4U);
  EXPECT_EQ(queue.report().requeues, 1U);
  EXPECT_FALSE(queue.report().exhausted);
}

TEST(EndpointPairs, QueueDropsStaleCandidatesAndCapsTotalRevisits) {
  const Endpoint_pair a{0, 1}, b{1, 2};
  Pair_queue          queue(2, 2);
  Budget              work{10000};
  ASSERT_EQ(queue.refresh(std::array{a, b}, {}, work), Status::feasible);
  EXPECT_EQ(queue.pop(work), a);
  const std::array<uint8_t, 3> affected{1, 0, 0};
  ASSERT_EQ(queue.refresh(std::array<Endpoint_pair, 1>{a}, affected, work), Status::feasible);
  EXPECT_EQ(queue.report().stale_skips, 1U);
  EXPECT_EQ(queue.pop(work), a);
  ASSERT_EQ(queue.refresh(std::array<Endpoint_pair, 1>{a}, affected, work), Status::feasible);
  EXPECT_FALSE(queue.pop(work));
  EXPECT_TRUE(queue.report().exhausted);
  EXPECT_EQ(queue.report().trials, 2U);
  Pair_queue bounded(1, 2);
  ASSERT_EQ(bounded.refresh(std::array<Endpoint_pair, 1>{a}, {}, work), Status::feasible);
  EXPECT_EQ(bounded.refresh(std::array{a, b}, {}, work), Status::search_exhausted);
  EXPECT_EQ(bounded.pop(work), a);  // failed refresh did not replace the agenda
  EXPECT_EQ(bounded.refresh(
                std::array<Endpoint_pair, 1>{
                    Endpoint_pair{1, 1}
  },
                {},
                work),
            Status::invalid);
}
}  // namespace livehd::usyn
