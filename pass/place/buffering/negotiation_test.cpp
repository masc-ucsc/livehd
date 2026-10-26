// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "negotiation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "buffer_cell_fixture.hpp"
#include "gtest/gtest.h"

namespace b = livehd::placement::buffering;
namespace m = livehd::liberty::timing;
namespace {
m::Table table(double base, double cap) {
  return {
      m::Variable::input_slew,
      m::Variable::output_load,
      {0, 128},
      {0, 128},
      {base, base + 128 * cap, base, base + 128 * cap}
  };
}
struct Fixture {
  m::Library          lib;
  b::Allocation_state state{
      b::Site_reservations(b::build_legal_sites({{"r", {0, 0, 10, 1}, 1}},
      {                      }
      ))
  };
  b::Allocation_settings settings;
  Fixture() {
    for (auto [name, cin, delay, dc] : std::vector<std::tuple<std::string, double, double, double>>{
             {"D", 1, 5,  1},
             {"B", 1, 1, .1},
             {"S", 4, 0,  0}
    }) {
      lib.cells.emplace(name,
                        b::test_fixture::buffer_cell(name,
                                                     {cin, cin},
                                                     name == "D" ? 1 : 100,
                                                     {table(delay, dc), table(delay, dc)},
                                                     {table(1, .1), table(1, .1)}));
    }
    settings.dbu_per_um  = 1;
    settings.max_seconds = 10;
  }
  b::Allocation_net net(std::string owner, b::Net_class kind = b::Net_class::critical) const {
    b::Allocation_net net;
    net.owner          = owner;
    net.classification = kind;
    b::Context context;
    context.driver = {
        owner + "d",
        {"D", "A", "Y"},
        {0, .5},
        {0, 0},
        {1, 1}
    };
    context.core  = {0, 0, 10, 1};
    context.sinks = {
        {owner + "s", "S", "A", {8, .5}, {100, 100}, {.5, .5}}
    };
    context.capacity["sites:r"]     = 10;
    context.normalization_period_ps = 128;
    net.context                     = context;
    return net;
  }
  b::Allocation_candidate alternative(const b::Allocation_net& net, uint64_t first = 3, uint64_t width = 1,
                                      bool buffered = true) const {
    const double    x = double(first + width);
    b::Routing_tree tree;
    tree.vertices = {
        {{0, .5},  {}, {}},
        {{x, .5},  {}, {}},
        {{8, .5}, {0}, {}}
    };
    tree.segments = {
        {0, 1, 0, 0,               x, true},
        {1, 2, 0, 0, std::abs(8 - x), true}
    };
    b::Buffer buffer{
        net.owner + "b",
        {"B", "A", "Y"},
        {x, .5},
        {x, .5},
        {double(first), 0, x, 1},
        true,
        {{"sites:r", width}}
    };
    tree.vertices[1].options.push_back({
        buffer,
        {"r", first, width},
        true
    });
    std::vector<b::Site_span> spans;
    if (buffered) {
      spans.push_back({"r", first, width});
    }
    return {b::expand_tree(*net.context, tree, {0, buffered ? 1u : 0u, 0}), std::move(spans), {}};
  }
};
size_t allocated(const b::Allocation_result& result) {
  return std::count_if(result.nets.begin(), result.nets.end(), [](const auto& net) { return bool(net.candidate); });
}
}  // namespace
TEST(BufferNegotiation, TwoMandatoryNetsCannotClaimTheSameLastSlot) {
  Fixture f;
  auto    a = f.net("a"), z = f.net("z");
  a.candidates = {f.alternative(a)};
  z.candidates = {f.alternative(z)};
  auto result  = b::negotiate(f.lib, f.state, {a, z}, {}, {}, f.settings);
  EXPECT_EQ(result.status, b::Allocation_status::budget_exhausted);
  EXPECT_EQ(allocated(result), 1);
  EXPECT_EQ(result.proposal.sites.reservations().size(), 1);
  EXPECT_TRUE(f.state.sites.reservations().empty());
  ASSERT_FALSE(result.proposal.history.gaps.empty());
  EXPECT_GT(result.proposal.history.gaps.begin()->second, 0);
  for (const auto& round : result.rounds) {
    EXPECT_EQ(round.unmet, 1);
  }
}
TEST(BufferNegotiation, UnmetPromotionAndAlternateSlotCanSettleBothNets) {
  Fixture f;
  auto    a = f.net("a"), z = f.net("z");
  a.candidates = {f.alternative(a, 3), f.alternative(a, 6)};
  z.candidates = {f.alternative(z, 3)};
  auto result  = b::negotiate(f.lib, f.state, {a, z}, {}, {}, f.settings);
  EXPECT_EQ(result.status, b::Allocation_status::settled);
  EXPECT_EQ(allocated(result), 2);
  EXPECT_EQ(result.proposal.sites.reservations().at("a")[0].first, 6);
  EXPECT_EQ(result.proposal.sites.reservations().at("z")[0].first, 3);
  EXPECT_GE(result.rounds.size(), 2);
  EXPECT_TRUE(f.state.sites.reservations().empty());
}
TEST(BufferNegotiation, CriticalNetsPrecedeOutsideNetsRegardlessOfInputOrder) {
  Fixture f;
  auto    outside = f.net("a", b::Net_class::outside_window), critical = f.net("z");
  outside.severity    = 100;
  outside.candidates  = {f.alternative(outside)};
  critical.candidates = {f.alternative(critical)};
  auto result         = b::negotiate(f.lib, f.state, {outside, critical}, {}, {}, f.settings);
  ASSERT_EQ(result.nets.size(), 2);
  EXPECT_FALSE(result.nets[0].candidate);
  EXPECT_TRUE(result.nets[1].candidate);
}
TEST(BufferNegotiation, EnteringTheWindowReordersAllocationAndHistoryEpochCanRebase) {
  Fixture f;
  auto    a = f.net("a", b::Net_class::outside_window), z = f.net("z", b::Net_class::window);
  a.candidates = {f.alternative(a)};
  z.candidates = {f.alternative(z)};
  auto prior   = b::negotiate(f.lib, f.state, {a, z}, {}, {}, f.settings);
  EXPECT_TRUE(prior.nets[1].candidate);
  a.classification          = b::Net_class::critical;
  f.settings.geometry_epoch = 1;
  auto next                 = b::negotiate(f.lib, prior.proposal, {a, z}, {}, {}, f.settings);
  EXPECT_TRUE(next.nets[0].candidate);
  EXPECT_FALSE(next.nets[1].candidate);
  EXPECT_TRUE(next.history_rebased);
  EXPECT_EQ(next.proposal.history.geometry_epoch, 1);
}
TEST(BufferNegotiation, CongestionHistoryPricesAlternativeGapsWithoutTimeOffsets) {
  Fixture f;
  f.state                           = b::Allocation_state(b::Site_reservations(b::build_legal_sites(
      {
          {"r", {0, 0, 10, 1}, 1}
  },
      {{4, 0, 5, 1}})));
  auto net                          = f.net("n", b::Net_class::outside_window);
  net.candidates                    = {f.alternative(net, 1), f.alternative(net, 5, 2)};
  f.state.history.gaps[{"r", 0, 4}] = 50;
  auto result                       = b::negotiate(f.lib, f.state, {net}, {}, {}, f.settings);
  ASSERT_TRUE(result.nets[0].candidate);
  EXPECT_EQ(*result.nets[0].candidate, 1);
  ASSERT_TRUE(result.nets[0].timing);
  EXPECT_TRUE(result.nets[0].timing->meets_required.value());
  EXPECT_EQ(result.nets[0].timing->root_required_ps[0], result.nets[0].timing->root_required_ps[1]);
  EXPECT_DOUBLE_EQ(f.state.history.gaps.begin()->second, 50);
}
TEST(BufferNegotiation, PhysicalWindowAndCapSlewRoutingRemainHardAtAnyPrice) {
  Fixture f;
  auto    net                       = f.net("n", b::Net_class::outside_window);
  net.context->sinks[0].required_ps = {1, 1};
  net.candidates                    = {f.alternative(net)};
  auto result                       = b::negotiate(f.lib, f.state, {net}, {}, {}, f.settings);
  EXPECT_EQ(allocated(result), 0);
  EXPECT_GT(result.rejected, 0);
  net.classification = b::Net_class::critical;
  result             = b::negotiate(f.lib, f.state, {net}, {}, {}, f.settings);
  ASSERT_EQ(allocated(result), 1);
  EXPECT_FALSE(result.nets[0].timing->meets_required.value());
  net.candidates[0].candidate.trees[0].routing_allowed = false;
  result                                               = b::negotiate(f.lib, f.state, {net}, {}, {}, f.settings);
  EXPECT_EQ(allocated(result), 0);
  net.candidates                                      = {f.alternative(net)};
  f.lib.cells.at("B").pins.at("Y").max_capacitance_ff = 0;
  result                                              = b::negotiate(f.lib, f.state, {net}, {}, {}, f.settings);
  EXPECT_EQ(allocated(result), 0);
}
TEST(BufferNegotiation, ZeroUnknownAndCompetingCrossingCapacityAreDistinct) {
  Fixture f;
  auto    a = f.net("a"), z = f.net("z");
  a.candidates              = {f.alternative(a, 3)};
  z.candidates              = {f.alternative(z, 6)};
  a.candidates[0].resources = {
      {"crossing", 1}
  };
  z.candidates[0].resources = {
      {"crossing", 1}
  };
  auto unknown = b::negotiate(f.lib, f.state, {a}, {}, {}, f.settings);
  EXPECT_EQ(unknown.status, b::Allocation_status::incomplete_models);
  EXPECT_EQ(allocated(unknown), 0);
  auto zero = b::negotiate(f.lib,
                           f.state,
                           {
                               a
  },
                           {{"crossing", 0}},
                           {},
                           f.settings);
  EXPECT_EQ(allocated(zero), 0);
  EXPECT_EQ(zero.unsupported, 0);
  auto limited = b::negotiate(f.lib,
                              f.state,
                              {
                                  a,
                                  z
  },
                              {{"crossing", 1}},
                              {},
                              f.settings);
  EXPECT_EQ(allocated(limited), 1);
  uint64_t used = 0;
  for (const auto& [owner, resources] : limited.proposal.resources) {
    if (resources.contains("crossing")) {
      used += resources.at("crossing");
    }
  }
  EXPECT_EQ(used, 1);
  EXPECT_GT(limited.proposal.history.resources.at("crossing"), 0);
}
TEST(BufferNegotiation, ProtectedOwnersAndMissingModelsRetainExistingReservations) {
  Fixture f;
  ASSERT_EQ(f.state.sites
                .replace("protected",
                         {
                             {"r", 3, 1}
  })
                .status,
            b::Reservation_status::reserved);
  auto net       = f.net("n");
  net.candidates = {f.alternative(net)};
  auto result    = b::negotiate(f.lib, f.state, {net}, {}, {}, f.settings);
  EXPECT_EQ(allocated(result), 0);
  EXPECT_TRUE(result.proposal.sites.reservations().contains("protected"));
  b::Allocation_net absent;
  absent.owner = "protected";
  result       = b::negotiate(f.lib, f.state, {absent, net}, {}, {}, f.settings);
  EXPECT_EQ(result.status, b::Allocation_status::incomplete_models);
  EXPECT_EQ(allocated(result), 0);
  EXPECT_EQ(result.proposal.sites.reservations(), f.state.sites.reservations());
}
TEST(BufferNegotiation, RetiredOrUnbufferedOwnersReleaseStaleDemand) {
  Fixture f;
  ASSERT_EQ(f.state.sites
                .replace("old",
                         {
                             {"r", 3, 1}
  })
                .status,
            b::Reservation_status::reserved);
  f.state.resources["old"]["crossing"] = 1;
  auto retired                         = b::negotiate(f.lib,
                              f.state,
                                                      {
  },
                                                      {{"crossing", 1}},
                                                      {"old"},
                              f.settings);
  EXPECT_EQ(retired.status, b::Allocation_status::settled);
  EXPECT_TRUE(retired.proposal.sites.reservations().empty());
  EXPECT_TRUE(retired.proposal.resources.empty());
  auto net                                            = f.net("old", b::Net_class::outside_window);
  net.candidates                                      = {f.alternative(net, 3, 1, false), f.alternative(net)};
  f.lib.cells.at("D").pins.at("Y").max_capacitance_ff = 100;
  auto direct                                         = b::negotiate(f.lib,
                             f.state,
                                                                     {
                                 net
  },
                                                                     {{"crossing", 1}},
                                                                     {},
                             f.settings);
  ASSERT_TRUE(direct.nets[0].candidate);
  EXPECT_EQ(*direct.nets[0].candidate, 0);
  EXPECT_TRUE(direct.proposal.sites.reservations().empty());
  EXPECT_TRUE(direct.pulls.empty());
}
TEST(BufferNegotiation, IncompleteInputSearchIsReportedWithoutInventingAnOptimum) {
  Fixture f;
  auto    net               = f.net("n");
  net.candidates            = {f.alternative(net)};
  net.input_search_complete = false;
  auto result               = b::negotiate(f.lib, f.state, {net}, {}, {}, f.settings);
  EXPECT_EQ(result.status, b::Allocation_status::settled);
  EXPECT_FALSE(result.input_search_complete);
  EXPECT_EQ(allocated(result), 1);
}
TEST(BufferNegotiation, CandidateFitRoundAndTimeWorkBoundsKeepTheOriginalSafe) {
  for (unsigned which = 0; which < 3; ++which) {
    Fixture f;
    auto    net    = f.net("n");
    net.candidates = {f.alternative(net)};
    if (which == 0) {
      f.settings.max_candidates = 0;
    }
    if (which == 1) {
      f.settings.max_waveform_fits = 0;
    }
    if (which == 2) {
      f.settings.max_rounds = 0;
    }
    auto result = b::negotiate(f.lib, f.state, {net}, {}, {}, f.settings);
    EXPECT_EQ(result.status, b::Allocation_status::budget_exhausted);
    EXPECT_EQ(allocated(result), 0);
    EXPECT_TRUE(result.proposal.sites.reservations().empty());
    EXPECT_TRUE(f.state.sites.reservations().empty());
  }
}
TEST(BufferNegotiation, GapEntryPullsFollowActualPathsAndStayWithinTheNetBound) {
  Fixture f;
  auto    net    = f.net("n");
  net.candidates = {f.alternative(net)};
  auto result    = b::negotiate(f.lib, f.state, {net}, {}, {}, f.settings);
  ASSERT_EQ(result.pulls.size(), 1);
  EXPECT_EQ(result.pulls[0].target, (b::Point{0, .5}));
  EXPECT_EQ(result.pulls[0].driver_stage, 0);
  EXPECT_EQ(result.pulls[0].owner, "n");
  EXPECT_LE(result.pulls[0].fraction, f.settings.maximum_pull_fraction);
  EXPECT_GT(result.pulls[0].fraction, 0);
  f.settings.maximum_pull_fraction = 0;
  result                           = b::negotiate(f.lib, f.state, {net}, {}, {}, f.settings);
  EXPECT_TRUE(result.pulls.empty());
  EXPECT_EQ(result.missing_gap_entries, 0);
}
TEST(BufferNegotiation, InvalidHistoryGeometryAndOwnerInputsNeverMutateOriginalState) {
  Fixture f;
  auto    net    = f.net("n");
  net.candidates = {f.alternative(net)};
  EXPECT_THROW(b::negotiate(f.lib, f.state, {net, net}, {}, {}, f.settings), std::invalid_argument);
  f.state.history.gaps[{"r", 0, 10}] = -1;
  EXPECT_THROW(b::negotiate(f.lib, f.state, {net}, {}, {}, f.settings), std::invalid_argument);
  EXPECT_DOUBLE_EQ(f.state.history.gaps.begin()->second, -1);
  f.state.history.gaps.clear();
  auto invalid = net;
  invalid.candidates[0].sites.clear();
  EXPECT_THROW(b::negotiate(f.lib, f.state, {invalid}, {}, {}, f.settings), std::invalid_argument);
  EXPECT_TRUE(f.state.sites.reservations().empty());
}
TEST(BufferNegotiation, GapEntryIsTheFirstCrossingAlongTheActualRootPath) {
  Fixture f;
  f.state           = b::Allocation_state(b::Site_reservations(b::build_legal_sites(
      {
          {"r", {0, 0, 10, 1}, 1}
  },
      {{4, 0, 5, 1}})));
  auto net          = f.net("n");
  net.candidates    = {f.alternative(net, 6)};
  const auto result = b::negotiate(f.lib, f.state, {net}, {}, {}, f.settings);
  ASSERT_EQ(result.pulls.size(), 1);
  EXPECT_EQ(result.pulls[0].target, (b::Point{5, .5}));
}
TEST(BufferNegotiation, CoincidentRootAndBufferInputStillHaveAGapEntry) {
  Fixture f;
  auto    net                            = f.net("n");
  auto    alternative                    = f.alternative(net, 0);
  alternative.candidate.buffers[0].input = {0, .5};
  auto& tree                             = alternative.candidate.trees[0];
  tree.rc.nodes.resize(1);
  tree.rc.edges.clear();
  tree.rc.wire_length_um = 0;
  tree.positions.resize(1);
  tree.terminals[0].node = 0;
  net.candidates         = {alternative};
  const auto result      = b::negotiate(f.lib, f.state, {net}, {}, {}, f.settings);
  ASSERT_EQ(allocated(result), 1);
  ASSERT_EQ(result.pulls.size(), 1);
  EXPECT_EQ(result.pulls[0].target, (b::Point{0, .5}));
  EXPECT_EQ(result.missing_gap_entries, 0);
}
TEST(BufferNegotiation, EarlyBudgetReportsEveryOwnerAndAllUpstreamIncompleteness) {
  for (bool time_bound : {false, true}) {
    Fixture f;
    auto    a = f.net("a"), z = f.net("z");
    a.candidates            = {f.alternative(a)};
    z.candidates            = {f.alternative(z, 6)};
    z.input_search_complete = false;
    if (time_bound) {
      f.settings.max_seconds = std::numeric_limits<double>::min();
    } else {
      f.settings.max_candidates = 0;
    }
    const auto result = b::negotiate(f.lib, f.state, {a, z}, {}, {}, f.settings);
    EXPECT_EQ(result.status, b::Allocation_status::budget_exhausted);
    ASSERT_EQ(result.nets.size(), 2);
    EXPECT_EQ(result.nets[0].owner, "a");
    EXPECT_EQ(result.nets[1].owner, "z");
    EXPECT_FALSE(result.nets[1].reason.empty());
    EXPECT_FALSE(result.input_search_complete);
    EXPECT_EQ(allocated(result), 0);
    EXPECT_TRUE(result.proposal.sites.reservations().empty());
  }
}
