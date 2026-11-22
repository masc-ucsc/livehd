// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "clock_tree.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <set>
#include <stdexcept>
#include <thread>

#include "gtest/gtest.h"

namespace livehd::pcrs {
namespace {
TEST(PcrsClock, PlacementAnchorsUseElectricallyFittedLeavesWithoutATrunk) {
  std::vector<Clock_sink> pins;
  for (int i = 0; i < 32; ++i) {
    pins.push_back({std::to_string(i), double(i), double(i % 3), 1.0});
    pins.back().chunk = i % 2 ? "west" : "east";
  }
  const std::vector<Clock_driver> drivers{{"buf", "A", "Y", 1, 20, 1, 90}};
  const Clock_limits limits{4, .02, 5};
  const auto leaves = plan_clock_leaves(pins, drivers, limits);
  const auto full = plan_clock_tree(pins, drivers, limits);
  size_t i = 0;
  for (const auto& branch : full.branches) {
    if (branch.leaf) {
      ASSERT_LT(i, leaves.size());
      const auto& leaf = leaves[i++];
      EXPECT_TRUE(leaf.leaf);
      EXPECT_EQ(leaf.children, branch.children);
      EXPECT_EQ(leaf.driver, branch.driver);
      EXPECT_EQ(leaf.x_um, branch.x_um);
      EXPECT_EQ(leaf.y_um, branch.y_um);
      EXPECT_EQ(leaf.load_ff, branch.load_ff);
    }
  }
  EXPECT_EQ(i, leaves.size());
  EXPECT_LT(leaves.size(), full.branches.size());

  // Each local pair is legal. The remote pairs have no legal connection at the available sites.
  std::vector<Clock_sink> separated{{"a", 0, 0, 1}, {"b", 1, 0, 1}, {"c", 1000, 0, 1}, {"d", 1001, 0, 1}};
  Clock_limits bounded{2, 1, 5};
  bounded.repeater_sites = 0;
  EXPECT_THROW(plan_clock_tree(separated, drivers, bounded), Clock_infeasible);
  ASSERT_EQ(plan_clock_leaves(separated, drivers, bounded).size(), 2u);
  separated.front().cap_ff = 1e4;
  EXPECT_THROW(plan_clock_leaves(separated, drivers, bounded), Clock_infeasible);
}

TEST(PcrsClock, RefitReusesFixedGroupsAndUnchangedTrunksAndMatchesFreshPlanning) {
  std::vector<Clock_sink> sinks;
  for (unsigned i = 0; i < 48; ++i) {
    sinks.push_back({std::to_string(i), double(i), double(i % 4), 1.});
  }
  std::vector<Clock_driver> drivers{
      {"buf", "A", "Y", 1, 50, 1, 0, 0, 0, 15}
  };
  Clock_limits     limits{4, .001, 1.};
  Clock_plan_cache cache;
  auto             nominal    = plan_clock_tree(sinks, drivers, limits, {}, &cache);
  sinks.front().leaf_delay_ps = 44.;
  auto trial                  = plan_clock_tree(sinks, drivers, limits, {}, &cache);
  auto fresh                  = plan_clock_tree(sinks, drivers, limits);
  EXPECT_EQ(trial.arrival_ps, fresh.arrival_ps);
  EXPECT_EQ(trial.roots, fresh.roots);
  EXPECT_EQ(trial.branches.size(), fresh.branches.size());
  EXPECT_NE(trial.arrival_ps, nominal.arrival_ps);
  EXPECT_EQ(cache.grouping_hits, 1u);
  EXPECT_GT(cache.reused_stages, 0u);
  sinks.front().x_um += 70;
  trial               = plan_clock_tree(sinks, drivers, limits, {}, &cache);
  EXPECT_EQ(cache.grouping_hits, 1u);
  EXPECT_EQ(trial.arrival_ps, plan_clock_tree(sinks, drivers, limits).arrival_ps);
}

TEST(PcrsClock, PhysicalLeafDelayAtAMacroUsesTheCharacterizedStageAndAvailableSites) {
  std::vector<Clock_sink> pins{
      {"ram:CLK", 2., 2., 1., true},
      {"flop:CK", 30., 5., 1.}
  };
  std::vector<Clock_driver> cells{
      {"buf", "A", "Y", 1, 50, 1, 0, 0, 0, 15}
  };
  Clock_limits limits{8, 0, 0};
  auto         baseline = plan_clock_tree(pins, cells, limits);
  pins[0].leaf_delay_ps = 44;
  auto delayed          = plan_clock_tree(pins, cells, limits);
  EXPECT_EQ(delayed.schedule_buffers, 3u);
  EXPECT_NEAR(delayed.arrival_ps.at(0) - baseline.arrival_ps.at(0), 45., 1e-5);
  EXPECT_NEAR(delayed.arrival_ps.at(1), baseline.arrival_ps.at(1), 1e-5);
  limits.repeater_sites = 1;
  EXPECT_EQ(plan_clock_tree(pins, cells, limits).schedule_buffers, 1u);
}

TEST(PcrsClock, InterleavedChunkSinksHaveIndependentSubtreesAndTimedEntries) {
  std::vector<Clock_sink> sinks;
  for (int i = 0; i < 20; ++i) {
    sinks.push_back({std::to_string(i), double(i), double(i % 3), 1});
    sinks.back().chunk = i % 2 ? "west" : "east";
  }
  const std::vector<Clock_driver> drivers{
      {"buf", "A", "Y", 1, 30, 1, 200, 0, 0, 20}
  };
  const Clock_limits limits{4, .02, 5};
  const Clock_source source{-5, 0, drivers.front()};
  auto               describe = [&](const std::vector<Clock_sink>& input) {
    const auto tree = plan_clock_tree(input, drivers, limits, source);
    EXPECT_EQ(tree.chunk_entries.size(), 2u);
    std::map<std::string, std::vector<std::tuple<std::string, double, double>>> leaves;
    std::set<std::string>                                                       seen;
    for (auto [home, entry] : tree.chunk_entries) {
      EXPECT_GT(tree.branches[entry].input_arrival_ps, 0);
      std::vector<size_t> todo{entry};
      while (!todo.empty()) {
        const auto& b = tree.branches[todo.back()];
        todo.pop_back();
        for (auto id : b.children) {
          if (b.leaf) {
            EXPECT_EQ(input[id].chunk, home);
            EXPECT_TRUE(seen.insert(input[id].name).second);
            leaves[home].emplace_back(input[id].name, b.x_um, b.y_um);
          } else {
            todo.push_back(id);
          }
        }
      }
      std::ranges::sort(leaves[home]);
    }
    EXPECT_EQ(seen.size(), input.size());
    return leaves;
  };
  const auto before  = describe(sinks);
  sinks.back().x_um += 50;  // A west-only edit leaves east's groups and drivers in place.
  std::ranges::reverse(sinks);
  EXPECT_EQ(describe(sinks).at("east"), before.at("east"));
  sinks[0].coupled_group = sinks[1].coupled_group = "gate";
  EXPECT_THROW(plan_clock_tree(sinks, drivers, limits, source), Clock_infeasible);
}

TEST(PcrsClock, SourceForkPaysForItsExtraLoadAndKeepsEverySink) {
  std::vector<Clock_sink> sinks{
      {"a",  0, 0, 1},
      {"b",  1, 0, 1},
      {"c",  9, 0, 1},
      {"d", 10, 0, 1}
  };
  Clock_driver buffer{"buf", "A", "Y", 1, 20, 1};
  buffer.intrinsic_ps = 50;
  Clock_source source{0, 0, buffer};
  Clock_limits limits{2, .01, 1};
  const auto   buffered = plan_clock_tree(sinks, {buffer}, limits, source);
  ASSERT_EQ(buffered.roots.size(), 1u);
  limits.allow_source_fork = true;
  const auto fork          = plan_clock_tree(sinks, {buffer}, limits, source);
  ASSERT_EQ(fork.roots.size(), 2u);
  ASSERT_EQ(fork.branches.size(), 2u);
  EXPECT_EQ(fork.arrival_ps.size(), sinks.size());
  for (auto root : fork.roots) {
    EXPECT_TRUE(fork.branches[root].leaf);
    EXPECT_EQ(fork.branches[root].children.size(), 2u);
  }
  EXPECT_GT(fork.arrival_ps[3], fork.arrival_ps[0]);
  EXPECT_LT(fork.arrival_ps[3], buffered.arrival_ps[3]);
  for (auto& sink : sinks) {
    sink.chunk = "home";
  }
  source.chunk     = "home";
  const auto local = plan_clock_tree(sinks, {buffer}, limits, source);
  EXPECT_EQ(local.roots, fork.roots);
  EXPECT_TRUE(local.chunk_entries.empty());
  EXPECT_EQ(local.arrival_ps, fork.arrival_ps);
  source.chunk.clear();
  const auto entering = plan_clock_tree(sinks, {buffer}, limits, source);
  ASSERT_EQ(entering.chunk_entries.size(), 1u);
  EXPECT_EQ(entering.roots.front(), entering.chunk_entries.front().second);
  for (auto& sink : sinks) {
    sink.chunk = Name{};
  }
  limits.source_fork_cap_ff = fork.source_load_ff * .99;
  EXPECT_EQ(plan_clock_tree(sinks, {buffer}, limits, source).roots.size(), 1u);
  limits.source_fork_cap_ff.reset();
  source.driver.max_fanout = 1;
  EXPECT_EQ(plan_clock_tree(sinks, {buffer}, limits, source).roots.size(), 1u);
  source.driver.max_fanout     = 0;
  source.driver.resistance_ohm = 100000;
  EXPECT_EQ(plan_clock_tree(sinks, {buffer}, limits, source).roots.size(), 1u);
}

TEST(PcrsClock, SourceForkComparesAgainstTheBestBufferedRootOutsideChunks) {
  const std::vector<Clock_sink> sinks{
      {"a", 0, 0, 1},
      {"b", 0, 0, 1},
      {"c", 0, 0, 1},
      {"d", 0, 0, 1}
  };
  const std::vector<Clock_driver> drivers{
      {"small", "A", "Y",  1., 20., 1., 1000.},
      { "fast", "A", "Y", 1.1, 20., 2.,   50.}
  };
  Clock_source source{0, 0, drivers.front()};
  source.driver.resistance_ohm = 1500.;
  Clock_limits limits{2, 0, 0};
  const auto   nominal = plan_clock_tree(sinks, drivers, limits, source);
  ASSERT_EQ(nominal.roots.size(), 1u);
  EXPECT_EQ(nominal.branches[nominal.roots.front()].driver, 0u);

  // Direct costs 3 ps and the small shared root costs 3.5 ps. A stronger
  // shared root costs 1.75 ps including its input load on the source.
  limits.allow_source_fork = true;
  const auto fitted        = plan_clock_tree(sinks, drivers, limits, source);
  ASSERT_EQ(fitted.roots.size(), 1u);
  EXPECT_TRUE(fitted.chunk_entries.empty());
  EXPECT_EQ(fitted.branches[fitted.roots.front()].driver, 1u);
  EXPECT_NEAR(fitted.source_cell_delay_ps + fitted.arrival_ps[0], 3.75, 1e-6);
  limits.source_fork_cap_ff = 1.05;
  const auto bounded        = plan_clock_tree(sinks, drivers, limits, source);
  ASSERT_EQ(bounded.roots.size(), 1u);
  EXPECT_EQ(bounded.branches[bounded.roots.front()].driver, 0u);
}

TEST(PcrsClock, GroupsByLocationAndElectricalLoadAndKeepsMacroSeparate) {
  std::vector<Clock_sink> sinks;
  for (int i = 0; i < 128; ++i) {
    sinks.push_back({std::to_string(i), double(i % 16), double(i / 16), 1.0, false});
  }
  sinks.push_back({"ram/CK", 0, 0, 12, true});
  const std::vector<Clock_driver> drivers{
      {"small", "A", "Z", 1,  9, 1, 100, 50, 0},
      {"large", "I", "Y", 2, 30, 2, 100, 50, 0}
  };
  const Clock_limits limits{16, 0.02, 0.5, 12};
  auto               describe = [&](const auto& input) {
    const auto                    tree = plan_clock_tree(input, drivers, limits);
    std::vector<std::string>      signature;
    std::map<std::string, size_t> seen;
    std::vector<size_t>           parents(tree.branches.size());
    for (size_t i = 0; i < tree.branches.size(); ++i) {
      const auto& b = tree.branches[i];
      EXPECT_LE(b.children.size(), limits.fanout);
      EXPECT_LE(b.load_ff, drivers[b.driver].max_load_ff);
      std::string name = drivers[b.driver].master;
      for (auto child : b.children) {
        if (b.leaf) {
          ++seen[input.at(child).name];
          if (input[child].macro) {
            EXPECT_EQ(b.children.size(), 1u);
            EXPECT_EQ(drivers[b.driver].master, "large");
          }
          name += ":" + input[child].name;
        } else {
          EXPECT_LT(child, i);
          ++parents.at(child);
          name += ":" + signature.at(child);
        }
      }
      signature.push_back(std::move(name));
    }
    EXPECT_EQ(seen.size(), input.size());
    for (const auto& [_, n] : seen) {
      EXPECT_EQ(n, 1u);
    }
    EXPECT_EQ(std::ranges::count(parents, 1u), std::ssize(parents) - 1);
    EXPECT_EQ(parents.back(), 0u);
    return signature;
  };
  const auto first = describe(sinks);
  std::ranges::reverse(sinks);
  EXPECT_EQ(first, describe(sinks));
}
TEST(PcrsClock, ImpossiblePinLoadFailsWithoutPublishingAPartialTree) {
  EXPECT_THROW(plan_clock_tree(
                   {
                       {"ram/CK", 0, 0, 30, true}
  },
                   {{"buf", "I", "Z", 1, 10, 1, 10, 10, 10}},
                   {}),
               std::runtime_error);
}

TEST(PcrsClock, RepeatsLongWiresWithinElectricalLimitsOnBothArms) {
  std::vector<Clock_sink> sinks{
      {"ramA/CK", -45, -15, 2, true},
      {"ramB/CK",  45,  15, 2, true}
  };
  const std::vector<Clock_driver> drivers{
      {"buf", "A", "Z", 1, 11, 1, 80, 10, 2, 1, 8}
  };
  Clock_limits limits{8, 1, 20, 0};
  const auto   tree = plan_clock_tree(sinks, drivers, limits);
  ASSERT_GT(tree.wire_repeaters, 2u);
  ASSERT_EQ(tree.branches.size(), tree.wire_repeaters + 3);
  const auto& root = tree.branches.back();
  ASSERT_EQ(root.children.size(), 2u);
  std::array<size_t, 2> depth{};
  for (size_t side = 0; side < 2; ++side) {
    size_t child = root.children[side];
    double px = root.x_um, py = root.y_um;
    double root_distance = 0;
    for (;;) {
      const auto&  branch = tree.branches.at(child);
      const double len    = std::abs(branch.x_um - px) + std::abs(branch.y_um - py);
      if (depth[side]++ == 0) {
        root_distance = len;
      }
      ASSERT_EQ(branch.children.size(), 1u);
      if (branch.leaf) {
        EXPECT_EQ(branch.children.front(), side);
        break;
      }
      px                = branch.x_um;
      py                = branch.y_um;
      child             = branch.children.front();
      const auto&  load = tree.branches.at(child);
      const auto   dbu  = limits.dbu_per_um;
      const double span = (std::abs(std::llround(px * dbu) - std::llround(load.x_um * dbu))
                           + std::abs(std::llround(py * dbu) - std::llround(load.y_um * dbu)))
                          / dbu;
      EXPECT_FLOAT_EQ(branch.load_ff, drivers[0].input_ff + limits.wire_ff_um * span);
      EXPECT_LE(branch.load_ff, drivers[0].max_load_ff);
      EXPECT_LE(branch.far_slew_ps, drivers[0].input_max_slew_ps);
    }
    EXPECT_NEAR(root.radius_um, root_distance, 1e-10);
  }
  EXPECT_EQ(depth[0], depth[1]);
  EXPECT_LE(root.load_ff, drivers[0].max_load_ff);
  EXPECT_LE(root.far_slew_ps, drivers[0].input_max_slew_ps);
  limits.repeater_sites = tree.wire_repeaters - 1;
  EXPECT_THROW(plan_clock_tree(sinks, drivers, limits), std::runtime_error);
  limits.repeater_sites = tree.wire_repeaters;
  EXPECT_EQ(plan_clock_tree(sinks, drivers, limits).wire_repeaters, tree.wire_repeaters);
}

TEST(PcrsClock, ImpossibleForkCannotBeFixedByAddingRepeaters) {
  const std::vector<Clock_sink> sinks{
      {"ramA/CK",   0, 0, 1, true},
      {"ramB/CK", 200, 0, 1, true}
  };
  EXPECT_THROW(plan_clock_tree(sinks,
                               {
                                   {"buf", "A", "Z", 3, 5, 1, 10, 100, 2}
  },
                               {8, 1, 10, 0}),
               std::runtime_error);
}

TEST(PcrsClock, ConnectsAnOffsetSourceWithinItsOwnLoadAndSiteLimits) {
  const std::vector<Clock_sink> sinks{
      {"ram/CK", 24, 19, 2, true}
  };
  const std::vector<Clock_driver> drivers{
      {"buf", "A", "Z", 1, 11, 1, 5, 35, 8}
  };
  Clock_limits limits{8, 1, 1, 0, 4};
  Clock_source source{0, 0, {}};
  source.driver.max_load_ff = 5;
  const auto tree           = plan_clock_tree(sinks, drivers, limits, source);
  ASSERT_EQ(tree.source_repeaters, 4u);
  ASSERT_EQ(tree.wire_repeaters, 4u);
  ASSERT_EQ(tree.branches.size(), 5u);
  const auto& first = tree.branches.back();
  const auto  dbu   = limits.dbu_per_um;
  EXPECT_DOUBLE_EQ(tree.source_wire_um, (std::llround(first.x_um * dbu) + std::llround(first.y_um * dbu)) / dbu);
  EXPECT_FLOAT_EQ(tree.source_load_ff, 1 + tree.source_wire_um);
  EXPECT_LE(tree.source_load_ff, source.driver.max_load_ff);
  double length = tree.source_wire_um;
  for (size_t i = 1; i < tree.branches.size(); ++i) {
    const auto& stage = tree.branches[i];
    ASSERT_EQ(stage.children, std::vector<size_t>{i - 1});
    EXPECT_FALSE(stage.leaf);
    EXPECT_LE(stage.load_ff, 11);
    length += stage.wire_um;
  }
  EXPECT_NEAR(length, 43, 1e-10);
  limits.repeater_sites = 3;
  EXPECT_THROW(plan_clock_tree(sinks, drivers, limits, source), std::runtime_error);
  source.x_um       = 24;
  source.y_um       = 17;
  const auto direct = plan_clock_tree(sinks, drivers, limits, source);
  ASSERT_EQ(direct.branches.size(), 1u);
  EXPECT_EQ(direct.source_repeaters, 0u);
  EXPECT_DOUBLE_EQ(direct.source_wire_um, 2);
  EXPECT_DOUBLE_EQ(direct.source_load_ff, 3);
}

TEST(PcrsClock, SourceTransitionCanRequireARepeaterOrMakeTheConnectionImpossible) {
  const std::vector<Clock_sink> sinks{
      {"ram/CK", 10, 0, 1, true}
  };
  const std::vector<Clock_driver> drivers{
      {"buf", "A", "Z", 1, 100, 1, 1, 100, 8, 0, 10}
  };
  Clock_limits limits{8, 1, 100, 0, 4};
  Clock_source source{0, 0, {}};
  source.driver.reference_slew_ps = 8;
  const auto tree                 = plan_clock_tree(sinks, drivers, limits, source);
  EXPECT_GT(tree.source_repeaters, 0u);
  EXPECT_GE(tree.source_far_slew_ps, 8);
  EXPECT_LE(tree.source_far_slew_ps, 10);
  source.driver.reference_slew_ps = 11;
  EXPECT_THROW(plan_clock_tree(sinks, drivers, limits, source), std::runtime_error);
}
TEST(PcrsClock, CommonGatedSourceRepeatersUseDelayAtPlacedLoad) {
  Clock_limits limits{8, 1, 0, 0};
  Clock_source source{};
  source.driver.max_load_ff = 10;
  const std::vector<Clock_sink> sinks{
      {"nested_gate:CK", 33, 0, 1}
  };
  const std::vector<Clock_driver> buffers{
      {"small", "A", "Y", 1, 10, 1, 900, 0, 8, 1},
      { "fast", "A", "Y", 1, 10, 2, 100, 0, 8, 2},
  };
  const auto area                = plan_clock_tree(sinks, buffers, limits, source);
  limits.minimize_source_latency = true;
  const auto latency             = plan_clock_tree(sinks, buffers, limits, source);
  ASSERT_GT(latency.source_repeaters, 1u);
  ASSERT_EQ(latency.source_repeaters, area.source_repeaters);
  ASSERT_EQ(latency.branches.size(), area.branches.size());
  EXPECT_LT(latency.arrival_ps.at(0), area.arrival_ps.at(0));
  for (size_t i = 1; i < latency.branches.size(); ++i) {
    EXPECT_EQ(area.branches[i].driver, 0u);
    EXPECT_EQ(latency.branches[i].driver, 1u);
  }
}
TEST(PcrsClock, BalancedSourcePairPricesTheRepeaterAndForkTogether) {
  const std::vector<Clock_sink> sinks{{"r0:CK", 28, 0, 1, true}, {"r1:CK", 32, 0, 1, true}};
  std::vector<Clock_driver>     drivers{
      {"small", "A", "Y", 1, 20, 1, 900},
      { "fast", "A", "Y", 1, 20, 2, 100}
  };
  Clock_source source{};
  source.driver.max_load_ff = 17;
  Clock_limits limits{2, 1, 0};
  const auto   nominal = plan_clock_tree(sinks, drivers, limits, source);
  ASSERT_EQ(nominal.source_repeaters, 1u);
  ASSERT_EQ(nominal.roots.size(), 1u);
  const auto head = nominal.roots.front();
  ASSERT_EQ(nominal.branches[head].children.size(), 1u);
  const auto fork = nominal.branches[head].children.front();
  ASSERT_EQ(nominal.branches[fork].children.size(), 2u);
  limits.balance   = true;
  const auto sized = plan_clock_tree(sinks, drivers, limits, source);
  ASSERT_EQ(nominal.roots, sized.roots);
  ASSERT_EQ(nominal.branches.size(), sized.branches.size());
  for (size_t i = 0; i < sized.branches.size(); ++i) {
    EXPECT_EQ(nominal.branches[i].children, sized.branches[i].children);
    EXPECT_EQ(sized.branches[i].driver, i == head || i == fork ? 1u : 0u);
  }
  EXPECT_LT(sized.arrival_ps[0], nominal.arrival_ps[0]);
  EXPECT_LT(sized.arrival_ps[1], nominal.arrival_ps[1]);
  EXPECT_TRUE(nominal.source_pair_available);
  limits.balance             = false;
  limits.source_drive_stages = 1;
  const auto first           = plan_clock_tree(sinks, drivers, limits, source);
  EXPECT_EQ(first.branches[head].driver, 1u);
  EXPECT_EQ(first.branches[fork].driver, 0u);
  EXPECT_LT(first.arrival_ps[0], nominal.arrival_ps[0]);
  limits.source_drive_stages = 2;
  EXPECT_EQ(plan_clock_tree(sinks, drivers, limits, source).arrival_ps, sized.arrival_ps);
  drivers[1].input_ff = 10;
  const auto bounded  = plan_clock_tree(sinks, drivers, limits, source);
  EXPECT_EQ(bounded.branches[bounded.roots.front()].driver, 0u);
  EXPECT_LE(bounded.source_load_ff, source.driver.max_load_ff);
}

TEST(PcrsClock, FasterSiblingLeavesChangeParentLoadAndRespectFixedSource) {
  const std::vector<Clock_sink> sinks{
      {"a",  0, 0, 1},
      {"b",  1, 0, 1},
      {"c",  9, 0, 1},
      {"d", 10, 0, 1}
  };
  std::vector<Clock_driver> drivers{
      {     "base", "A", "Y", 1,  4, 1, 100},
      {   "slower", "A", "Y", 1,  4, 2, 200},
      {   "faster", "A", "Y", 3,  8, 3,  50},
      {"slow_slew", "A", "Y", 3, 12, 4, 500}
  };
  Clock_limits limits{2, 0, 0, 0, 0};
  limits.leaf_drive_steps = 2;
  const auto check        = [&](bool sized, const std::optional<Clock_source>& source) {
    const auto tree = plan_clock_tree(sinks, drivers, limits, source);
    ASSERT_EQ(tree.branches.size(), 3u);
    for (const auto& b : tree.branches) {
      EXPECT_EQ(drivers[b.driver].master, sized ? "faster" : "base");
      EXPECT_EQ(b.drive_steps, b.leaf && sized ? 1u : 0u);
    }
    EXPECT_DOUBLE_EQ(tree.branches.back().load_ff, sized ? 6 : 2);
  };
  check(true, {});
  std::ranges::reverse(drivers);
  check(true, {});
  Clock_source source{};
  source.driver.max_load_ff = 2;
  check(false, source);
}
TEST(PcrsClock, SchedulesFinalLeavesIndividuallyAndRechecksTrunkLoad) {
  std::vector<Clock_sink> sinks{
      {"a",  0, 0, 1},
      {"b",  1, 0, 1},
      {"c",  9, 0, 1},
      {"d", 10, 0, 1}
  };
  const std::vector<Clock_driver> drivers{
      {"base", "A", "Y", 1, 4, 1, 100},
      {"fast", "A", "Y", 3, 8, 2,  50}
  };
  Clock_limits limits{2, 0, 0, 0, 0};
  limits.leaf_drive_steps = 1;
  sinks[2].drive_steps    = 0;
  const auto tree         = plan_clock_tree(sinks, drivers, limits, {});
  ASSERT_EQ(tree.branches.size(), 3u);
  EXPECT_EQ(tree.branches[0].drive_steps, 1u);
  EXPECT_EQ(tree.branches[1].drive_steps, 0u);
  EXPECT_EQ(tree.branches.back().load_ff, 4);
  sinks[3].drive_steps = 1;
  EXPECT_THROW(plan_clock_tree(sinks, drivers, limits, {}), std::invalid_argument);
  sinks[3].drive_steps = sinks[2].drive_steps = 2;
  EXPECT_THROW(plan_clock_tree(sinks, drivers, limits, {}), std::invalid_argument);
}

TEST(PcrsClock, ArrivalUsesEachArmAndIncludesSourceAndParentStages) {
  const std::vector<Clock_sink> sinks{
      {"a", 1, 0, 1},
      {"b", 1, 0, 1},
      {"c", 3, 0, 1},
      {"ram", 10, 0, 1, true}
  };
  const std::vector<Clock_driver> drivers{
      {"buf", "A", "Y", 1, 50, 1, 0, 0, 0, 2}
  };
  const Clock_limits limits{4, 0, 1000};
  const auto         tree = plan_clock_tree(sinks, drivers, limits, Clock_source{2, 0, {}});
  ASSERT_EQ(tree.branches.size(), 3u);
  // Source wire 4 ps. The root adds 2 ps and each arm charges one
  // 1 fF leaf input over 4 um. At the flop leaf both left pins share 1 um.
  const auto check = [](const Clock_tree& t, std::array<double, 4> expected) {
    ASSERT_EQ(t.arrival_ps.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
      EXPECT_NEAR(t.arrival_ps[i], expected[i], 1e-8);
    }
    EXPECT_NEAR(t.max_leaf_skew_ps, 1, 1e-8);
  };
  check(tree, {14, 14, 13, 12});
  auto asymmetric    = sinks;
  asymmetric[1].x_um = 2;
  check(plan_clock_tree(asymmetric, drivers, limits, Clock_source{2, 0, {}}), {13, 12, 13, 12});
}

TEST(PcrsClock, DirectionalRCDrivesSourceAndLeafArrival) {
  std::vector<Clock_sink> sinks{
      {"a", 0, 0, 1},
      {"b", 4, 0, 1}
  };
  const std::vector<Clock_driver> drivers{
      {"buf", "A", "Y", 1, 50, 1}
  };
  Clock_limits limits;
  limits.directional_rc = std::array{1000., 1., 2000., 3.};
  const auto tree       = plan_clock_tree(sinks, drivers, limits, Clock_source{2, 2, {}});
  ASSERT_EQ(tree.branches.size(), 1u);
  EXPECT_FLOAT_EQ(tree.source_load_ff, 7);
  EXPECT_FLOAT_EQ(tree.branches[0].load_ff, 6);
  // Vertical source: 4 kohm * (1+3) fF. Horizontal arms: 2 kohm * (1+1) fF.
  ASSERT_EQ(tree.arrival_ps.size(), 2u);
  for (double arrival : tree.arrival_ps) {
    EXPECT_NEAR(arrival, 20, 1e-6);
  }
}

TEST(PcrsClock, BalancesEffectiveMacroArrivalUnderPhysicalSiteBudget) {
  std::vector<Clock_sink> sinks{
      {"early", 0, 0, 1, true},
      {"macro", 0, 0, 1, true}
  };
  sinks[1].insertion_ps = 20;
  const std::vector<Clock_driver> buffers{
      {"buf", "A", "Y", 1, 10, 1, 0, 0, 0, 20}
  };
  Clock_limits limits;
  const auto   before = plan_clock_tree(sinks, buffers, limits);
  EXPECT_DOUBLE_EQ(before.target_skew_ps, 20);
  limits.balance      = true;
  const auto balanced = plan_clock_tree(sinks, buffers, limits);
  EXPECT_EQ(balanced.balance_buffers, 1u);
  EXPECT_DOUBLE_EQ(balanced.target_skew_ps, 0);
  EXPECT_DOUBLE_EQ(balanced.arrival_ps[0], balanced.arrival_ps[1] + 20);
  limits.repeater_sites = 0;
  EXPECT_DOUBLE_EQ(plan_clock_tree(sinks, buffers, limits).target_skew_ps, 20);
  sinks[1].wanted_ps = 20;
  EXPECT_DOUBLE_EQ(plan_clock_tree(sinks, buffers, limits).target_skew_ps, 0);
}

TEST(PcrsClock, NearbyArrivalTargetsShareElectricallyLegalLeavesWithinEachChunk) {
  const std::vector<Clock_driver> drivers{
      {"buf", "A", "Y", 1, 100, 1, 0, 0, 0, 20}
  };
  const std::vector<double> tails{0, 5, 10, 10.1, 15, 20, 20.2, 26};
  std::vector<Clock_sink>   sinks;
  for (const auto chunk : {"left", "right"}) {
    for (const double tail : tails) {
      auto& pin     = sinks.emplace_back(Clock_sink{std::to_string(sinks.size()), 0, 0, 1});
      pin.chunk     = chunk;
      pin.wanted_ps = -tail;
    }
  }
  sinks.back().separate_leaf = true;
  Clock_limits limits;
  limits.balance = true;
  const std::vector<std::vector<size_t>> expected{
      {0, 1, 2},
      {3, 4, 5},
      {6, 7},
      {8, 9, 10},
      {11, 12, 13},
      {14},
      {15}
  };
  const auto check = [&](const auto& branches) {
    std::vector<std::vector<size_t>> leaves;
    for (const auto& branch : branches) {
      if (branch.leaf) {
        leaves.push_back(branch.children);
        std::ranges::sort(leaves.back());
      }
    }
    std::ranges::sort(leaves);
    EXPECT_EQ(leaves, expected);
  };
  check(plan_clock_leaves(sinks, drivers, limits));
  check(plan_clock_tree(sinks, drivers, limits).branches);
}

TEST(PcrsClock, BalancedLeafSplitsWireSkewOnlyInTheChunkWithCompetingTargets) {
  const std::vector<Clock_driver> drivers{
      {"buf", "A", "Y", 1, 0, 1, 0, 0, 0, 20}
  };
  Clock_limits limits;
  limits.balance     = true;
  limits.wire_ff_um  = 0.1;
  limits.wire_ohm_um = 1000;
  std::vector<Clock_sink> sinks;
  for (const auto* home : {"targeted", "uniform", "locked"}) {
    for (int x : {0, 50, 100}) {
      auto& sink = sinks.emplace_back(std::string(home) + std::to_string(x), double(x), 0, 1);
      sink.chunk = home;
      if (sink.chunk == "locked") {
        sink.coupled_group = "gate_fork";
      }
    }
  }
  for (const auto* home : {"targeted", "locked"}) {
    auto& sink     = sinks.emplace_back(std::string(home) + "_late", 51, 0, 1);
    sink.chunk     = home;
    sink.wanted_ps = -60;
  }
  const auto leaves      = plan_clock_leaves(sinks, drivers, limits);
  const auto shares_leaf = [&](size_t first, size_t second) {
    return std::ranges::any_of(leaves, [&](const auto& leaf) {
      return std::ranges::find(leaf.children, first) != leaf.children.end()
             && std::ranges::find(leaf.children, second) != leaf.children.end();
    });
  };
  EXPECT_FALSE(shares_leaf(0, 2));  // The middle and outer pins see different wire delay.
  EXPECT_TRUE(shares_leaf(3, 5));   // The other chunk has no competing arrival requests.
  EXPECT_TRUE(shares_leaf(6, 8));   // A discrete gate fork must remain on one leaf.
}

TEST(PcrsClock, ExplicitDrivesRemainAvailableOnPreviouslySeparateLatePins) {
  std::vector<Clock_sink> sinks{
      {"early", 0, 0, 1},
      {"late0", 1, 0, 1},
      {"late1", 2, 0, 1}
  };
  sinks[1].wanted_ps = sinks[2].wanted_ps = -40;
  const std::vector<Clock_driver> drivers{
      {"base", "A", "Y", 1, 4, 1, 100, 0, 0, 20},
      {"fast", "A", "Y", 3, 8, 2,  50, 0, 0, 20}
  };
  Clock_limits limits;
  limits.balance = true;
  Clock_plan_cache cache;
  const auto       initial = plan_clock_tree(sinks, drivers, limits, {}, &cache);
  ASSERT_EQ(std::ranges::count_if(initial.branches, [](const auto& b) { return b.leaf; }), 2);
  sinks[1].drive_steps = 0;
  sinks[2].drive_steps = 1;
  const auto scheduled = plan_clock_tree(sinks, drivers, limits, {}, &cache);
  EXPECT_GT(cache.grouping_hits, 0u);
  for (size_t pin : {1u, 2u}) {
    const auto leaf
        = std::ranges::find_if(scheduled.branches, [&](const auto& b) { return b.leaf && b.children == std::vector<size_t>{pin}; });
    ASSERT_NE(leaf, scheduled.branches.end());
    EXPECT_EQ(leaf->drive_steps, *sinks[pin].drive_steps);
  }
}

TEST(PcrsClock, ConflictingArrivalBandDrivesOnlySplitTheirOwnLeaf) {
  const std::vector<Clock_driver> drivers{
      {"base", "A", "Y", 1, 12, 1, 100, 0, 0, 20},
      {"fast", "A", "Y", 2, 16, 2,  50, 0, 0, 20}
  };
  Clock_limits limits;
  limits.fanout  = 8;
  limits.balance = true;
  for (unsigned conflict_band : {0u, 1u}) {
    std::vector<Clock_sink> sinks;
    for (unsigned band = 0; band < 3; ++band) {
      for (unsigned pin = 0; pin < 4; ++pin) {
        auto& s     = sinks.emplace_back(std::to_string(band) + "_" + std::to_string(pin), double(10 * band + pin), 0, 1);
        s.wanted_ps = -40. * band - pin;
      }
    }
    const auto first = 4 * conflict_band;
    if (conflict_band == 0) {
      sinks[first + 1].coupled_group = sinks[first + 2].coupled_group = "phase";
    }
    Clock_plan_cache cache;
    auto             initial = plan_clock_tree(sinks, drivers, limits, {}, &cache);
    ASSERT_EQ(std::ranges::count_if(initial.branches, [](const auto& b) { return b.leaf; }), 3);
    sinks[first].drive_steps     = conflict_band;
    sinks[first + 1].drive_steps = 1 - conflict_band;
    auto test                    = [&](Clock_plan_cache* session) {
      const auto planned = plan_clock_tree(sinks, drivers, limits, {}, session);
      EXPECT_EQ(std::ranges::count_if(planned.branches, [](const auto& b) { return b.leaf; }), 4);
      for (const auto& leaf : planned.branches) {
        if (!leaf.leaf) {
          continue;
        }
        if (std::ranges::find(leaf.children, first + 1) != leaf.children.end()) {
          if (conflict_band == 0) {
            EXPECT_NE(std::ranges::find(leaf.children, first + 2), leaf.children.end());
          }
          EXPECT_EQ(leaf.drive_steps, 1 - conflict_band);
        }
        if (std::ranges::find(leaf.children, first + 3) != leaf.children.end()) {
          EXPECT_EQ(leaf.drive_steps, 0u);
        }
        if (leaf.children.front() / 4 != conflict_band) {
          EXPECT_EQ(leaf.children.size(), 4u);
        }
      }
    };
    test(nullptr);
    test(&cache);
    EXPECT_GT(cache.grouping_hits, 0u);
    auto reference                   = limits;
    reference.balance                = false;
    reference.split_scheduled_leaves = true;
    const auto nominal               = plan_clock_tree(sinks, drivers, reference);
    for (const auto& leaf : nominal.branches) {
      if (leaf.leaf) {
        for (auto id : leaf.children) {
          if (sinks[id].drive_steps) {
            EXPECT_EQ(leaf.drive_steps, *sinks[id].drive_steps);
          }
        }
      }
    }
    if (conflict_band == 0) {
      sinks[first + 2].drive_steps = 0;
      EXPECT_THROW(plan_clock_tree(sinks, drivers, limits, {}, &cache), std::invalid_argument);
    }
  }
}

TEST(PcrsClock, RetainedPinsContributeToEffectiveSkew) {
  const std::vector<Clock_driver> buffers{
      {"buf", "A", "Y", 1, 10, 1, 0, 0, 0, 20}
  };
  Clock_source source{
      0,
      0,
      {},
      {{"gate:CLK", 0, 0, 1}, {"phase:A", 0, 0, 1}}
  };
  source.direct[0].downstream_ps = 90;
  source.direct[0].wanted_ps     = 10;
  source.direct[1].insertion_ps  = 35;
  EXPECT_DOUBLE_EQ(plan_clock_tree({}, buffers, {}, source).target_skew_ps, 45);
  EXPECT_DOUBLE_EQ(plan_clock_tree({{"flop:CLK", 0, 0, 1}}, buffers, {}, source).target_skew_ps, 60);
}

TEST(PcrsClock, MovesBorderSinkWithinFanoutAndElectricalLimits) {
  std::vector<Clock_sink> sinks;
  for (size_t i = 0; i < 10; ++i) {
    sinks.push_back({"west" + std::to_string(i), double(i), 0, 1});
    sinks.push_back({"east" + std::to_string(i), 9.1 + .1 * i, 0, 1});
  }
  const auto tree = plan_clock_tree(sinks,
                                    {
                                        {"buf", "A", "Y", 1, 60, 1, 100, 30, 12}
  },
                                    {12, 1, 1, 5});
  ASSERT_EQ(tree.branches.size(), 3u);
  EXPECT_GT(tree.border_moves, 0u);
  for (size_t i = 0; i < 2; ++i) {
    EXPECT_GE(tree.branches[i].children.size(), 6u);
    EXPECT_LE(tree.branches[i].children.size(), 12u);
    EXPECT_LE(tree.branches[i].radius_um, 4.5);
  }
  // The two original bisection leaves have 25 and 2.5 um of star wiring.
  EXPECT_LT(tree.branches[0].wire_um + tree.branches[1].wire_um, 27.5);
}

TEST(PcrsClock, RadiusFailurePeelsOffDistantPinsAndReportsBothLengths) {
  std::vector<Clock_sink> sinks;
  for (int i = 0; i < 9; ++i) {
    sinks.push_back({std::to_string(i), double(i), 0, 1});
  }
  sinks.push_back({"outlier", 50, 0, 1});
  const std::vector<Clock_driver> drivers{
      {"buf", "A", "Y", 1, 80, 1, 5}
  };
  const Clock_limits limits{12, .1, 5, 6};
  const auto         tree    = plan_clock_tree(sinks, drivers, limits);
  const auto&        metrics = tree.refined_leaves;
  EXPECT_EQ(metrics.size_histogram,
            (std::map<size_t, size_t>{
                {1, 1},
                {9, 1}
  }));
  EXPECT_EQ(metrics.driver_histogram.at("buf"), 2u);
  EXPECT_DOUBLE_EQ(metrics.wire_um, 8);
  EXPECT_DOUBLE_EQ(metrics.star_wire_um, 20);
}

TEST(PcrsClock, ChecksTheLibertyTransitionAtTheSink) {
  const auto   timing = liberty::timing::parse(R"(library(test) {
    time_unit : "1ps"; capacitive_load_unit(1,ff);
    cell(buffer) {
      pin(A) { direction : input; }
      pin(Z) { direction : output; function : "A";
        timing() { related_pin : A; timing_sense : positive_unate;
          cell_rise(scalar) { values("5"); } cell_fall(scalar) { values("6"); }
          rise_transition(scalar) { values("10"); } fall_transition(scalar) { values("19"); }
        }
      }
    }
  })");
  Clock_driver driver{"buffer", "A", "Z", 1, 100, 1, 1, 100, 20};
  characterize_clock_driver(driver, timing.cells.at(driver.master));
  ASSERT_EQ(driver.arcs.size(), 2u);
  EXPECT_EQ(driver.reference_slew_ps, 19);
  EXPECT_THROW(plan_clock_tree(
                   {
                       {"flop:CK", 0, 0, 1, false, 18}
  },
                   {driver},
                   {}),
               std::runtime_error);
  const auto tree = plan_clock_tree(
      {
          {"flop:CK", 0, 0, 1, false, 20}
  },
      {driver},
      {});
  EXPECT_DOUBLE_EQ(tree.branches.at(0).far_slew_ps, 19);
  EXPECT_DOUBLE_EQ(tree.branches.at(0).delay_ps, 6);
}

TEST(PcrsClock, TransitionTargetSelectsAStrongerPlannedStageAndExemptsTheSource) {
  // Two pins of 5 fF at one point, no wire: the weak cell delivers 2.2 * 10 kOhm * 10 fF = 220 ps, the strong one 22 ps.
  const std::vector<Clock_sink>   sinks{{"a", 0, 0, 5}, {"b", 0, 0, 5}};
  const std::vector<Clock_driver> drivers{{"weak", "A", "Y", 1, 0, 1, 10000}, {"strong", "A", "Y", 1, 0, 4, 1000}};
  Clock_limits                    limits{2, 0, 0, 0};
  const auto                      smallest = plan_clock_tree(sinks, drivers, limits);
  ASSERT_EQ(smallest.branches.size(), 1u);
  EXPECT_EQ(drivers[smallest.branches[0].driver].master, "weak");  // the library has no limit: area decides
  EXPECT_NEAR(smallest.branches[0].far_slew_ps, 220, 1e-9);
  limits.target_slew_ps = 50;
  const auto bounded    = plan_clock_tree(sinks, drivers, limits);
  ASSERT_EQ(bounded.branches.size(), 1u);
  EXPECT_EQ(drivers[bounded.branches[0].driver].master, "strong");
  EXPECT_LE(bounded.branches[0].far_slew_ps, 50);
  // The existing source is not sized by the planner. A target it cannot meet on its own net is not a failure.
  Clock_source source{};
  source.driver.resistance_ohm = 100000;
  const auto driven            = plan_clock_tree(sinks, drivers, limits, source);
  EXPECT_EQ(drivers[driven.branches[driven.roots.front()].driver].master, "strong");
  // A target below what any cell delivers into one pin is infeasible, and says which pin.
  limits.target_slew_ps = 5;
  EXPECT_THROW(plan_clock_tree(sinks, drivers, limits), Clock_infeasible);
}

TEST(PcrsClock, RetainedSourcePinsDoNotCountAgainstTheLeafFanout) {
  // A source that keeps more phase pins than the leaf knob (discrete gate forks) still gets its buffered tree.
  const std::vector<Clock_sink>   sinks{{"a", 0, 0, 1}, {"b", 1, 0, 1}};
  const std::vector<Clock_driver> drivers{{"base", "A", "Y", 1, 0, 1, 100}};
  Clock_limits                    limits{2, 0, 0, 0};
  Clock_source                    source{};
  for (const char* name : {"g0:CLK", "l0:CLK", "g1:CLK", "l1:CLK", "g2:CLK"}) {
    source.direct.push_back({name, 0, 0, 1});
  }
  const auto tree = plan_clock_tree(sinks, drivers, limits, source);
  ASSERT_EQ(tree.roots.size(), 1u);
  EXPECT_EQ(tree.branches[tree.roots.front()].children.size(), 2u);
  EXPECT_EQ(tree.retained_arrival_ps.size(), source.direct.size());
  // The source's own Liberty fanout still binds the pins it drives: five retained pins and the root input.
  source.driver.max_fanout = 5;
  EXPECT_THROW(plan_clock_tree(sinks, drivers, limits, source), Clock_infeasible);
  source.driver.max_fanout = 6;
  EXPECT_NO_THROW(plan_clock_tree(sinks, drivers, limits, source));
}

TEST(PcrsClock, LeavesFillToTheFanoutLimitInsteadOfHalvingByCount) {
  // 51 pins at fanout 24: halving by count gives 25 and 26, then four leaves of about 12 and 13. Cutting whole
  // groups' worth gives three leaves of 17, and a design of 51,677 pins about half the buffers.
  std::vector<Clock_sink> sinks;
  for (int i = 0; i < 51; ++i) {
    sinks.push_back({std::format("s{:02d}", i), double(i % 10), double(i / 10), 1});
  }
  const std::vector<Clock_driver> buffers{
      {"buf", "A", "Y", 1, 100, 1, 0, 0, 0, 20}
  };
  const Clock_limits limits{24, 0, 0, 0};
  const auto         tree   = plan_clock_tree(sinks, buffers, limits);
  size_t             leaves = 0, largest = 0, smallest = 100;
  for (const auto& b : tree.branches) {
    if (b.leaf) {
      ++leaves;
      largest  = std::max(largest, b.children.size());
      smallest = std::min(smallest, b.children.size());
    }
  }
  EXPECT_EQ(leaves, 3u);
  EXPECT_LE(largest, 24u);
  EXPECT_GE(smallest, 17u);
  // An exact multiple needs no repair: 48 pins are two full leaves.
  sinks.resize(48);
  size_t full = 0;
  for (const auto& b : plan_clock_tree(sinks, buffers, limits).branches) {
    full += b.leaf && b.children.size() == 24;
  }
  EXPECT_EQ(full, 2u);
}

TEST(PcrsClock, TrunkStagesTakeTheFasterDriverWhenAsked) {
  // Two clusters far apart under fanout 4: two leaves and one trunk stage above them.
  std::vector<Clock_sink> sinks;
  for (int i = 0; i < 4; ++i) {
    sinks.push_back({std::format("l{}", i), double(i), 0, 1});
    sinks.push_back({std::format("r{}", i), 200. + i, 0, 1});
  }
  const std::vector<Clock_driver> buffers{
      {  "base", "A", "Y", 1, 100, 1, 0, 0, 0, 100},
      {"faster", "A", "Y", 1, 100, 3, 0, 0, 0,  50}
  };
  Clock_limits limits{4, 0, 0, 0};
  const auto   trunk_driver = [&](const Clock_tree& tree) {
    std::string master;
    for (const auto& b : tree.branches) {
      if (!b.leaf) {
        master = buffers[b.driver].master;
      }
    }
    return master;
  };
  const auto small = plan_clock_tree(sinks, buffers, limits);
  EXPECT_EQ(trunk_driver(small), "base");
  limits.trunk_drive_steps = 1;
  const auto fast          = plan_clock_tree(sinks, buffers, limits);
  EXPECT_EQ(trunk_driver(fast), "faster");
  for (const auto& b : fast.branches) {
    EXPECT_EQ(buffers[b.driver].master, b.leaf ? "base" : "faster");  // leaves keep their own policy
  }
  EXPECT_LT(fast.arrival_ps[0], small.arrival_ps[0]);
}

TEST(PcrsClock, BalancingPadsTheEarlyBandOnceAtTheTopInsteadOfEveryArm) {
  // Twelve flops and a gate whose own subtree takes 300 ps. Mixing the gate into a spatial fork pads every flop
  // arm of that fork. Merging by arrival keeps the flops one root, and that root gets one chain of stages.
  std::vector<Clock_sink> sinks;
  for (int i = 0; i < 12; ++i) {
    sinks.push_back({std::format("f{:02d}", i), double(i % 4), double(i / 4), 1});
  }
  sinks.push_back({"gate", 10, 0, 1});
  sinks.back().downstream_ps = 300;
  const std::vector<Clock_driver> buffers{
      {"buf", "A", "Y", 1, 100, 1, 0, 0, 0, 30}
  };
  Clock_limits limits{3, 0, 0, 0};
  limits.balance      = true;
  const auto balanced = plan_clock_tree(sinks, buffers, limits);
  EXPECT_GE(balanced.balance_buffers, 8u);
  EXPECT_LE(balanced.balance_buffers, 12u);
  EXPECT_LE(balanced.target_skew_ps, 30);
  // A gap the clock period tolerates is left alone.
  limits.skew_bound_ps = 1000;
  const auto relaxed   = plan_clock_tree(sinks, buffers, limits);
  EXPECT_EQ(relaxed.balance_buffers, 0u);
}

TEST(PcrsClock, AGateSubtreeReportsItsEarliestAndLatestArrivalNotOneNumber) {
  // Below this pin a gate and its flops arrive between 100 and 300 ps after it. The domain above sees the spread.
  std::vector<Clock_sink> sinks{
      {"gate", 0, 0, 1}
  };
  sinks[0].downstream_ps = 300;
  const std::vector<Clock_driver> buffers{
      {"buf", "A", "Y", 1, 100, 1, 0, 0, 0, 20}
  };
  const Clock_limits limits{4, 0, 0, 0};
  EXPECT_DOUBLE_EQ(plan_clock_tree(sinks, buffers, limits).target_skew_ps, 0);
  sinks[0].downstream_min_ps = 100;
  EXPECT_DOUBLE_EQ(plan_clock_tree(sinks, buffers, limits).target_skew_ps, 200);
  sinks[0].downstream_ps = -100;
  sinks[0].downstream_min_ps = -300;
  EXPECT_DOUBLE_EQ(plan_clock_tree(sinks, buffers, limits).target_skew_ps, 200);
  sinks[0].downstream_min_ps = 400;  // later than the latest: not an interval
  EXPECT_THROW(plan_clock_tree(sinks, buffers, limits), std::invalid_argument);
}

TEST(PcrsClock, ADiscreteGateForkTriesOtherSitesBeforeItIsInfeasible) {
  // Three pins of one gate fork in an L: the middle of their box is off the shortest tree, so a driver there pays a
  // stub of wire the corner pin does not, and exceeds the load limit that a driver at a pin meets.
  std::vector<Clock_sink> sinks{
      {"a",   0,   0, 1},
      {"b", 100,   0, 1},
      {"c",   0, 100, 1}
  };
  for (auto& sink : sinks) {
    sink.coupled_group = "fork";
  }
  const std::vector<Clock_driver> buffers{
      {"buf", "A", "Y", 1, 95, 1, 0, 0, 0, 20}
  };
  Clock_limits limits{8, 0.4, 0.001, 0};
  const auto   tree = plan_clock_tree(sinks, buffers, limits);
  ASSERT_EQ(tree.branches.size(), 1u);
  EXPECT_EQ(tree.branches[0].children.size(), 3u);
  EXPECT_LE(tree.branches[0].load_ff, 95);
  EXPECT_FALSE(tree.branches[0].x_um == 50 && tree.branches[0].y_um == 50) << "the box middle is not legal here";
  // Where no site works the group stays infeasible, and says which fork.
  limits.wire_ff_um = 2.0;
  try {
    plan_clock_tree(sinks, buffers, limits);
    FAIL() << "expected Clock_infeasible";
  } catch (const Clock_infeasible& error) {
    EXPECT_NE(std::string(error.what()).find("fork"), std::string::npos);
  }
}

TEST(PcrsClock, PlannersRunAtTheSameTimeAndGiveTheirSequentialResults) {
  // One planner per chunk runs in its own worker. They share nothing: the same inputs give the same tree whichever
  // thread plans it, and with how many others.
  const std::vector<Clock_driver> buffers{
      {  "base", "A", "Y", 1, 100, 1, 0, 0, 0, 100},
      {"faster", "A", "Y", 1, 100, 3, 0, 0, 0,  50}
  };
  const auto sinks = [](int seed) {
    std::vector<Clock_sink> out;
    for (int i = 0; i < 400; ++i) {
      out.push_back({std::format("s{}_{:03d}", seed, i), double((i * 37 + seed * 11) % 211), double((i * 53 + seed * 7) % 197), 1});
    }
    return out;
  };
  Clock_limits limits{12, 0.1, 0.5, 0};
  limits.balance                = true;
  limits.trunk_drive_steps      = 4;
  constexpr int           count = 8;
  std::vector<Clock_tree> expected;
  for (int seed = 0; seed < count; ++seed) {
    expected.push_back(plan_clock_tree(sinks(seed), buffers, limits));
  }
  std::vector<Clock_tree>  parallel(count);
  std::vector<std::thread> workers;
  for (int seed = 0; seed < count; ++seed) {
    workers.emplace_back([&, seed] { parallel[seed] = plan_clock_tree(sinks(seed), buffers, limits); });
  }
  for (auto& worker : workers) {
    worker.join();
  }
  for (int seed = 0; seed < count; ++seed) {
    ASSERT_EQ(parallel[seed].branches.size(), expected[seed].branches.size()) << seed;
    EXPECT_EQ(parallel[seed].arrival_ps, expected[seed].arrival_ps) << seed;
    EXPECT_EQ(parallel[seed].balance_buffers, expected[seed].balance_buffers) << seed;
    for (size_t i = 0; i < parallel[seed].branches.size(); ++i) {
      EXPECT_EQ(parallel[seed].branches[i].children, expected[seed].branches[i].children) << seed << ":" << i;
      EXPECT_EQ(parallel[seed].branches[i].driver, expected[seed].branches[i].driver) << seed << ":" << i;
    }
  }
}
}  // namespace
}  // namespace livehd::pcrs
