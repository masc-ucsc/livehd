// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "placement_session.hpp"

#include "gtest/gtest.h"

namespace livehd::placement {
namespace dp = DreamPlace::native;
namespace {
dp::Design fixture() {
  dp::Design d;
  d.layout       = {0, 0, 80, 40, 1, 1};
  d.num_movable  = 2;
  d.node_x       = {10, 40, 80};
  d.node_y       = {10, 20, 10};
  d.node_size_x  = {2, 2, 0};
  d.node_size_y  = {1, 1, 0};
  d.node_macro   = {0, 0, 0};
  d.pin2node     = {0, 1, 1, 2};
  d.pin_offset_x = d.pin_offset_y = {0, 0, 0, 0};
  d.flat_net2pin                  = {0, 1, 2, 3};
  d.flat_net2pin_start            = {0, 2, 4};
  d.build_maps();
  return d;
}
dp::Options controls() {
  dp::Options o;
  o.max_iterations = 20;
  o.min_iterations = 0;
  o.bins_x = o.bins_y = 8;
  o.filler_max_count  = 32;
  o.num_threads       = 1;
  o.legalize = o.detailed_place = false;
  return o;
}
const std::vector<std::string> names{"cell:a", "cell:b", "io:out"};
}  // namespace
TEST(PlacementSession, InflationRetainsParticlesAndBudgetAfterConvergence) {
  auto              d = fixture();
  auto              o = controls();
  Placement_session session;
  session.advance(d, o, names, "initial", 20);
  const auto before = session.snapshot();
  ASSERT_GT(before.optimizer.x.size(), d.node_x.size());
  d.node_size_x[0]  *= 1.5;
  d.node_x[0]       -= .5;
  d.pin_offset_x[0] += .5;
  session.synchronize(d, o, names, "inflate");
  const auto rebound = session.snapshot();
  EXPECT_EQ(rebound.optimizer.iteration, 20);
  EXPECT_EQ(rebound.optimizer.density_weight, before.optimizer.density_weight);
  EXPECT_EQ(rebound.optimizer.x.size(), before.optimizer.x.size());
  for (size_t i = d.node_x.size(); i < before.optimizer.x.size(); ++i) {
    EXPECT_EQ(rebound.optimizer.x[i], before.optimizer.x[i]);
    EXPECT_EQ(rebound.optimizer.y[i], before.optimizer.y[i]);
  }
  EXPECT_EQ(rebound.optimizer.momentum, 1);
  EXPECT_TRUE(rebound.optimizer.restart);
  o.checkpoint_interval = 5;
  unsigned checks       = 0;
  o.checkpoint          = [&](const dp::Design& actual, int) {
    ++checks;
    dp::GpUpdate update;
    update.net_weights    = actual.net_weights;
    update.net_weights[0] = 2;
    return update;
  };
  EXPECT_EQ(session.advance(d, o, names, "rebalance", 20).iterations, 20);
  EXPECT_EQ(checks, 4u);
  EXPECT_EQ(session.cold_starts(), 1u);
  EXPECT_EQ(session.iteration(), 40);
  const auto accepted_design = d;
  const auto accepted        = session.snapshot();
  session.advance(d, o, names, "rejected_candidate", 20);
  d = accepted_design;
  session.restore(accepted);
  EXPECT_EQ(session.snapshot().optimizer.x, accepted.optimizer.x);
  EXPECT_EQ(session.snapshot().optimizer.old_gx, accepted.optimizer.old_gx);
  EXPECT_EQ(session.spent_iterations(), 60u);
  EXPECT_EQ(session.iteration(), 40);
  EXPECT_EQ(session.cold_starts(), 1u);
}
TEST(PlacementSession, FailedRebindLeavesDesignAndOptimizerIntact) {
  auto              d = fixture();
  auto              o = controls();
  Placement_session session;
  session.advance(d, o, names, "initial", 20);
  const auto saved      = session.snapshot();
  auto       bad        = d;
  bad.layout.xl         = 1;
  const auto bad_before = bad;
  EXPECT_THROW(session.synchronize(bad, o, names, "bad_core"), std::invalid_argument);
  EXPECT_EQ(bad.node_x, bad_before.node_x);
  EXPECT_EQ(session.snapshot().optimizer.x, saved.optimizer.x);
  EXPECT_EQ(session.iteration(), saved.optimizer.iteration);
  o.checkpoint_interval = 5;
  o.checkpoint          = [](const dp::Design&, int) -> dp::GpUpdate { throw std::runtime_error("STA failure"); };
  EXPECT_THROW(session.advance(d, o, names, "failed_timing", 20), std::runtime_error);
  EXPECT_EQ(d.node_x, saved.optimizer.physical.node_x);
  EXPECT_EQ(session.snapshot().optimizer.x, saved.optimizer.x);
  EXPECT_EQ(session.spent_iterations(), 20u);
}
TEST(PlacementSession, TopologyCallbackPublishesInsertedCellIdentity) {
  auto              d         = fixture();
  auto              o         = controls();
  auto              published = names;
  Placement_session session;
  o.checkpoint_interval = 5;
  o.checkpoint          = [&](const dp::Design& physical, int iteration) {
    dp::GpUpdate update;
    if (iteration != 5) {
      return update;
    }
    auto replacement        = physical;
    replacement.num_movable = 3;
    replacement.node_x.insert(replacement.node_x.begin() + 2, 25);
    replacement.node_y.insert(replacement.node_y.begin() + 2, 15);
    replacement.node_size_x.insert(replacement.node_size_x.begin() + 2, 2);
    replacement.node_size_y.insert(replacement.node_size_y.begin() + 2, 1);
    replacement.node_macro.insert(replacement.node_macro.begin() + 2, 0);
    for (auto& node : replacement.pin2node) {
      if (node >= 2) {
        ++node;
      }
    }
    replacement.build_maps();
    update.replacement   = std::move(replacement);
    update.previous_node = {0, 1, -1, 2};
    published            = {"cell:a", "cell:b", "cell:inserted", "io:out"};
    return update;
  };
  session.advance(d, o, names, "topology", 20, [&] { return published; });
  ASSERT_EQ(d.num_movable, 3);
  EXPECT_EQ(session.snapshot().identities, published);
  EXPECT_EQ(session.snapshot().optimizer.physical.node_x.size(), 4u);
  EXPECT_EQ(session.advance(d, o, published, "continue", 20).iterations, 20);
  EXPECT_EQ(session.cold_starts(), 1u);
  EXPECT_EQ(session.iteration(), 40);
}
TEST(PlacementSession, CoreGrowthResumesPhysicalPoseWithOneOptimizer) {
  auto              d = fixture();
  auto              o = controls();
  Placement_session session;
  session.advance(d, o, names, "initial", 20);
  const auto before = session.snapshot();
  auto       grown  = fixture();
  grown.layout.xh   = 100;
  grown.layout.yh   = 50;
  grown.node_x      = {1, 2, 100};
  grown.node_y      = {1, 2, 12};
  EXPECT_EQ(session.seed_movable(grown, names), 2u);
  EXPECT_DOUBLE_EQ(grown.node_x[0], (d.node_x[0] + 1) * 1.25 - 1);
  EXPECT_DOUBLE_EQ(grown.node_y[1], (d.node_y[1] + .5) * 1.25 - .5);
  EXPECT_EQ(grown.node_x[2], 100);  // retain the new core's IO location
  session.synchronize(grown, o, names, "core_growth");
  const auto rebound = session.snapshot();
  EXPECT_EQ(rebound.optimizer.density_weight, o.initial_density_weight);
  EXPECT_FALSE(rebound.optimizer.density_initialized);
  EXPECT_EQ(rebound.optimizer.x.size(), before.optimizer.x.size());
  for (size_t i = grown.node_x.size(); i < before.optimizer.x.size(); ++i) {
    EXPECT_EQ(rebound.optimizer.x[i], before.optimizer.x[i]);
    EXPECT_EQ(rebound.optimizer.y[i], before.optimizer.y[i]);
  }
  unsigned timing       = 0;
  o.checkpoint_interval = 5;
  o.checkpoint          = [&](const dp::Design&, int) {
    ++timing;
    return dp::GpUpdate{};
  };
  o.target_density = .75;
  EXPECT_EQ(session.advance(grown, o, names, "area_resume", 20).iterations, 20);
  EXPECT_EQ(timing, 4u);
  EXPECT_EQ(session.cold_starts(), 1u);
  EXPECT_EQ(session.iteration(), 40);
  EXPECT_EQ(session.snapshot().physical.layout.xh, 100);
}
TEST(PlacementSession, MacroReplanAfterCoreGrowthRetainsTheCellOptimizer) {
  auto d = fixture();
  d.node_x.insert(d.node_x.end(), {30, 65});
  d.node_y.insert(d.node_y.end(), {30, 30});
  d.node_size_x.insert(d.node_size_x.end(), {6, 2});
  d.node_size_y.insert(d.node_size_y.end(), {4, 1});
  d.node_macro.insert(d.node_macro.end(), {1, 0});
  d.node_orientation.assign(5, dp::Orientation::N);
  d.build_maps();
  auto identities = names;
  identities.insert(identities.end(), {"macro:3:ram:ram_master", "cell:fixed"});
  auto              o = controls();
  Placement_session session;
  session.advance(d, o, identities, "initial", 20);
  const auto before = session.snapshot();
  auto       grown  = d;
  grown.layout.xh   = 100;
  grown.layout.yh   = 50;
  grown.node_x[2]   = 100;
  grown.node_x[3]   = 70;
  grown.node_y[3]   = 35;
  dp::set_orientation(grown, 3, dp::Orientation::FN);
  // The fresh macro plan provides an adapted cell seed; unlike ordinary core
  // growth, the handoff must not replace it with the old affine cell positions.
  grown.node_x[0]   = 4;
  grown.node_y[0]   = 4;
  grown.node_x[1]   = 20;
  grown.node_y[1]   = 10;
  const auto seeded = grown;
  EXPECT_THROW(session.synchronize(grown, o, identities, "untrusted_macro_move"), std::invalid_argument);
  EXPECT_EQ(session.snapshot().optimizer.x, before.optimizer.x);
  auto zero           = o;
  zero.max_iterations = 0;
  EXPECT_EQ(session.advance(grown, zero, identities, "macro_plan_handoff", 0, {}, true).iterations, 0);
  const auto rebound = session.snapshot();
  EXPECT_EQ(session.cold_starts(), 1u);
  EXPECT_EQ(session.iteration(), 20);
  EXPECT_EQ(grown.node_x, seeded.node_x);
  EXPECT_EQ(grown.node_y, seeded.node_y);
  EXPECT_EQ(rebound.physical.node_orientation[3], dp::Orientation::FN);
  EXPECT_EQ(rebound.optimizer.x.size(), before.optimizer.x.size());
  for (size_t i = grown.node_x.size(); i < before.optimizer.x.size(); ++i) {
    EXPECT_EQ(rebound.optimizer.x[i], before.optimizer.x[i]);
    EXPECT_EQ(rebound.optimizer.y[i], before.optimizer.y[i]);
  }
  EXPECT_TRUE(session.history().back().macro_replan);
  EXPECT_TRUE(session.history().back().density_reset);
  EXPECT_EQ(rebound.optimizer.momentum, 1);
  EXPECT_TRUE(rebound.optimizer.restart);
  // The handoff grants no lasting permission to move fixed bodies.
  auto bad       = grown;
  bad.node_x[3] += 1;
  EXPECT_THROW(session.synchronize(bad, o, identities, "later_macro_move"), std::invalid_argument);
  bad            = grown;
  bad.node_x[4] += 1;
  EXPECT_THROW(session.advance(bad, zero, identities, "fixed_cell_move", 0, {}, true), std::invalid_argument);
  EXPECT_EQ(session.snapshot().optimizer.x, rebound.optimizer.x);
  unsigned timing       = 0;
  o.checkpoint_interval = 5;
  o.checkpoint          = [&](const dp::Design&, int) {
    ++timing;
    return dp::GpUpdate{};
  };
  EXPECT_EQ(session.advance(grown, o, identities, "area_resume", 20).iterations, 20);
  EXPECT_EQ(timing, 4u);
  EXPECT_EQ(session.cold_starts(), 1u);
  EXPECT_EQ(session.iteration(), 40);
  EXPECT_EQ(grown.node_x[3], 70);
  EXPECT_EQ(grown.node_y[3], 35);
  EXPECT_FALSE(session.history().back().macro_replan);
}
TEST(PlacementSession, GlobalRecoveryRestartsSmoothingWithoutLosingTheSession) {
  auto              d = fixture();
  auto              o = controls();
  Placement_session session;
  session.advance(d, o, names, "initial", 20);
  const auto before     = session.snapshot();
  unsigned   timing     = 0;
  o.checkpoint_interval = 5;
  o.checkpoint          = [&](const dp::Design& actual, int iteration) {
    EXPECT_EQ(actual.node_x.size(), d.node_x.size());
    EXPECT_GT(iteration, before.optimizer.iteration);
    ++timing;
    return dp::GpUpdate{};
  };
  EXPECT_EQ(session.advance(d, o, names, "global_recovery", 20, {}, false, true).iterations, 20);
  EXPECT_EQ(timing, 4u);
  EXPECT_EQ(session.cold_starts(), 1u);
  EXPECT_EQ(session.iteration(), 40);
  const auto recovered = session.snapshot();
  EXPECT_EQ(recovered.optimizer.wirelength_epoch, 20);
  EXPECT_EQ(recovered.optimizer.x.size(), before.optimizer.x.size());
  EXPECT_TRUE(session.history().back().density_reset);
  EXPECT_TRUE(session.history().back().wirelength_restart);
  EXPECT_TRUE(session.history().back().routing_inflation_reset);
  session.advance(d, o, names, "continued_recovery", 20);
  EXPECT_EQ(session.snapshot().optimizer.wirelength_epoch, 20);
  EXPECT_FALSE(session.history().back().wirelength_restart);
  session.restore(recovered);
  EXPECT_EQ(session.snapshot().optimizer.wirelength_epoch, 20);
  EXPECT_EQ(session.iteration(), 40);
}
}  // namespace livehd::placement
