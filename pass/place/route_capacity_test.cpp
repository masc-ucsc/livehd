// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "route_capacity.hpp"

#include "gtest/gtest.h"

#include <cmath>
#include <random>

namespace livehd::placement {
namespace {
namespace ph = physical;
struct Fixture {
  ph::Technology         tech;
  ph::Floorplan          fp;
  ph::Routing_occupancy  obstacles{40};
  Route_capacity_grid    grid{{0, 0, 200, 200}, 1, 1, {"M2", "M3"}, .30};
  Remaining_route_demand demand{{0}, {0}, .05, .05};
  Fixture() {
    fp.die              = {0, 0, 200, 200};
    tech.routing_layers = {"M2", "M3"};
    for (const auto& name : tech.routing_layers) {
      auto& l         = tech.layers[name];
      l.direction     = name == "M2" ? "HORIZONTAL" : "VERTICAL";
      l.width         = .010;
      l.rules.loaded  = true;
      l.rules.spacing = {{.010, {}, {}}};
    }
    fp.tracks = {{"Y", "M2", 10, 10, 20}, {"X", "M3", 10, 10, 20}};
  }
  std::vector<ph::Shape> wire(double y) const { return {{"M2", {0, y - 5, 200, y + 5}}}; }
};
}  // namespace
TEST(RouteCapacity, ConstructionCheckpointCancelsBetweenBins) {
  Fixture f;
  int     calls = 0;
  EXPECT_THROW(Route_capacity(f.tech,
                              f.fp,
                              f.obstacles,
                              f.grid,
                              [&] {
                                if (++calls == 2) {
                                  throw std::runtime_error("cancel capacity");
                                }
                              }),
               std::runtime_error);
  EXPECT_EQ(calls, 2);
  EXPECT_EQ(f.obstacles.size(), 0);
  Route_capacity complete(f.tech, f.fp, f.obstacles, f.grid);
  EXPECT_EQ(complete.report()[0].usable_tracks, 10);
}
TEST(RouteCapacity, TrackUnionAndAtomicHeadroom) {
  Fixture        f;
  Route_capacity c(f.tech, f.fp, f.obstacles, f.grid);
  ASSERT_EQ(c.report()[0].usable_tracks, 10);
  auto shapes = f.wire(10);
  shapes.push_back(shapes.front());
  auto trial = c.check(shapes, f.demand);
  ASSERT_TRUE(trial.legal) << trial.reason;
  EXPECT_EQ(c.report()[0].reserved_tracks, 0);
  c.commit(trial);
  EXPECT_EQ(c.report()[0].reserved_tracks, 1);
  EXPECT_THROW(c.commit(trial), std::invalid_argument);
  for (double y : {30, 50}) {
    auto next = c.check(f.wire(y), f.demand);
    ASSERT_TRUE(next.legal) << next.reason;
    c.commit(next);
  }
  const auto refused = c.check(f.wire(70), f.demand);
  EXPECT_FALSE(refused.legal);
  EXPECT_EQ(refused.reason, "pre-route capacity fraction");
  EXPECT_THROW(c.commit(refused), std::invalid_argument);
  EXPECT_EQ(c.report()[0].reserved_tracks, 3);
}
TEST(RouteCapacity, OnlyMetalBelowTheModeledLayersIsIgnored) {
  Fixture f;
  f.tech.routing_layers = {"M1", "M2", "M3", "M4"};
  f.tech.layers["M1"] = f.tech.layers["M4"] = f.tech.layers.at("M3");
  Route_capacity c(f.tech, f.fp, f.obstacles, f.grid);
  auto           shapes = f.wire(10);
  shapes.push_back({
      "M1",
      {0, 0, 10, 20}
  });
  EXPECT_TRUE(c.check(shapes, f.demand).legal);
  shapes.back().layer = "M4";
  EXPECT_EQ(c.check(shapes, f.demand).reason, "route uses layer outside capacity model");
}
TEST(RouteCapacity, PrecheckCannotCommitOrBypassRemainingDemand) {
  Fixture        f;
  Route_capacity c(f.tech, f.fp, f.obstacles, f.grid);
  const auto     preliminary = c.precheck(f.wire(10));
  ASSERT_TRUE(preliminary.legal);
  EXPECT_THROW(c.commit(preliminary), std::invalid_argument);
  EXPECT_EQ(c.report()[0].reserved_tracks, 0);
  f.demand.horizontal[0] = 1.1;
  EXPECT_FALSE(c.check(f.wire(10), f.demand).legal);
  f.obstacles.insert({{"M2", {80, 25, 90, 35}}, "VDD"});
  Route_capacity blocked(f.tech, f.fp, f.obstacles, f.grid);
  const auto     rejected = blocked.precheck(f.wire(30));
  EXPECT_FALSE(rejected.legal);
  EXPECT_EQ(rejected.reason, "route consumes obstructed track capacity");
  EXPECT_EQ(rejected.reason, blocked.check(f.wire(30), f.demand).reason);
  EXPECT_THROW(blocked.commit(rejected), std::invalid_argument);
}
TEST(RouteCapacity, DemandPrecheckCannotAuthorizeWiresOrReservations) {
  Fixture        f;
  Route_capacity c(f.tech, f.fp, f.obstacles, f.grid);
  const auto     necessary = c.precheck_demand(f.demand);
  ASSERT_TRUE(necessary.legal);
  EXPECT_THROW(c.commit(necessary), std::invalid_argument);
  EXPECT_THROW(c.check_remaining(necessary, f.demand), std::invalid_argument);
  EXPECT_FALSE(c.check({}, f.demand).legal);
  // Remaining demand alone fits, but adding this wire exceeds layer headroom.
  f.demand.horizontal[0] = .95;
  EXPECT_TRUE(c.precheck_demand(f.demand).legal);
  EXPECT_FALSE(c.check(f.wire(10), f.demand).legal);
  f.demand.pin_utilization = {1.25};
  const auto early         = c.precheck_demand(f.demand);
  const auto full          = c.check(f.wire(10), f.demand);
  EXPECT_FALSE(early.legal);
  EXPECT_EQ(early.reason, full.reason);
  EXPECT_EQ(early.detail, full.detail);
  EXPECT_EQ(c.report()[0].reserved_tracks, 0);
}
TEST(RouteCapacity, ReusedPrecheckMatchesFullCheckAndRejectsStaleOrForgedProofs) {
  Fixture        f;
  Route_capacity c(f.tech, f.fp, f.obstacles, f.grid);
  const auto     shapes      = f.wire(10);
  const auto     preliminary = c.precheck(shapes);
  ASSERT_TRUE(preliminary.legal);
  for (double utilization : {0., .2, .9, 1., 1.1}) {
    f.demand.horizontal[0] = utilization;
    const auto full        = c.check(shapes, f.demand);
    const auto reused      = c.check_remaining(preliminary, f.demand);
    EXPECT_EQ(reused.legal, full.legal);
    EXPECT_EQ(reused.reason, full.reason);
    EXPECT_EQ(reused.detail, full.detail);
  }
  f.demand.horizontal[0]   = 0;
  f.demand.pin_utilization = {1.1};
  EXPECT_EQ(c.check_remaining(preliminary, f.demand).reason, c.check(shapes, f.demand).reason);
  EXPECT_EQ(c.check_remaining(preliminary, f.demand).detail, c.check(shapes, f.demand).detail);
  f.demand.pin_utilization.clear();
  Route_capacity foreign(f.tech, f.fp, f.obstacles, f.grid);
  EXPECT_THROW(foreign.check_remaining(preliminary, f.demand), std::invalid_argument);
  Route_capacity_trial forged;
  forged.legal = true;
  EXPECT_THROW(c.check_remaining(forged, f.demand), std::invalid_argument);
  auto failed  = c.precheck({{"M4", {0, 0, 10, 10}}});
  failed.legal = true;
  EXPECT_THROW(c.check_remaining(failed, f.demand), std::invalid_argument);
  const auto completed = c.check_remaining(preliminary, f.demand);
  ASSERT_TRUE(completed.legal);
  c.commit(completed);
  EXPECT_EQ(c.report()[0].reserved_tracks, 1);
  EXPECT_THROW(c.check_remaining(preliminary, f.demand), std::invalid_argument);
  EXPECT_THROW(c.commit(completed), std::invalid_argument);
}
TEST(RouteCapacity, RestoringExistingWiresPreservesUnionAndHeadroom) {
  Fixture                f;
  Route_capacity         continuous(f.tech, f.fp, f.obstacles, f.grid);
  std::vector<ph::Shape> existing;
  for (double y : {10, 30, 50}) {
    const auto shapes = f.wire(y);
    const auto trial  = continuous.check(shapes, f.demand);
    ASSERT_TRUE(trial.legal);
    continuous.commit(trial);
    existing.insert(existing.end(), shapes.begin(), shapes.end());
  }
  existing.push_back(existing.front());
  Route_capacity restored(f.tech, f.fp, f.obstacles, f.grid);
  restored.reserve_existing(existing);
  EXPECT_EQ(restored.report()[0].usable_tracks, continuous.report()[0].usable_tracks);
  EXPECT_EQ(restored.report()[0].reserved_tracks, 3);
  EXPECT_EQ(restored.check(f.wire(70), f.demand).reason, continuous.check(f.wire(70), f.demand).reason);
  EXPECT_EQ(restored.check(f.wire(70), f.demand).reason, "pre-route capacity fraction");
  restored.reserve_existing(existing);
  EXPECT_EQ(restored.report()[0].reserved_tracks, 3);  // idempotent geometry union
  auto invalid = f.wire(70);
  invalid.push_back({"unknown", {0, 0, 10, 10}});
  EXPECT_THROW(restored.reserve_existing(invalid), std::invalid_argument);
  EXPECT_EQ(restored.report()[0].reserved_tracks, 3);
  // Imported wiring may already exceed the policy limit; preserve its actual
  // usage and reject further routing rather than resetting the budget.
  restored.reserve_existing(f.wire(70));
  EXPECT_EQ(restored.report()[0].reserved_tracks, 4);
  EXPECT_FALSE(restored.check(f.wire(90), f.demand).legal);
}
TEST(RouteCapacity, RemainingDemandReservesSpaceEvenBelowFractionLimit) {
  Fixture        f;
  Route_capacity c(f.tech, f.fp, f.obstacles, f.grid);
  f.demand.horizontal[0] = .95;
  auto trial             = c.check(f.wire(10), f.demand);
  EXPECT_FALSE(trial.legal);
  EXPECT_EQ(trial.reason, "remaining RUDY plus pre-route exceeds layer capacity");
  EXPECT_EQ(trial.detail, "layer=M2 bin=(0,0) remaining_demand_tracks=9.5 occupied_tracks=1 usable_tracks=10");
  f.demand.horizontal[0] = .9;
  trial                  = c.check(f.wire(10), f.demand);
  ASSERT_TRUE(trial.legal) << trial.reason;
  c.commit(trial);
  f.demand.horizontal[0] = 1.1;
  trial                  = c.check(f.wire(30), f.demand);
  EXPECT_FALSE(trial.legal);
  EXPECT_EQ(trial.reason, "remaining RUDY exceeds usable capacity");
  EXPECT_NE(trial.detail.find("bin=(0,0) horizontal_demand_tracks=11"), std::string::npos);
}
TEST(RouteCapacity, MacroOrPowerObstructionRemovesWholeTrackOnce) {
  Fixture f;
  f.obstacles.insert({{"M2", {80, 25, 90, 35}}, "VDD"});
  f.obstacles.insert({{"M2", {80, 25, 90, 35}}, "VDD"});
  Route_capacity c(f.tech, f.fp, f.obstacles, f.grid);
  EXPECT_EQ(c.report()[0].usable_tracks, 9);
  EXPECT_EQ(c.report()[1].usable_tracks, 10);
  auto trial = c.check(f.wire(30), f.demand);
  EXPECT_FALSE(trial.legal);
  EXPECT_EQ(trial.reason, "route consumes obstructed track capacity");
  EXPECT_EQ(trial.detail, "layer=M2 bin=(0,0) track=1 usable_tracks=9");
  EXPECT_TRUE(c.check(f.wire(10), f.demand).legal);
}
TEST(RouteCapacity, DirectPinAccountingDoesNotCreditBlockedTracks) {
  Fixture f;
  f.grid.bins_x       = 2;
  f.demand.horizontal = f.demand.vertical = {0, 0};
  f.obstacles.insert({{"M2", {80, 25, 90, 35}}, "pin_net"});
  f.grid.count_only_usable_tracks = true;
  Route_capacity c(f.tech, f.fp, f.obstacles, f.grid);
  EXPECT_EQ(c.report()[0].usable_tracks, 9);
  const auto trial = c.check(f.wire(30), f.demand);
  ASSERT_TRUE(trial.legal) << trial.reason;
  c.commit(trial);
  EXPECT_EQ(c.report()[0].usable_tracks, 9);
  EXPECT_EQ(c.report()[0].reserved_tracks, 0);
  // The next cell was not preblocked and must pay for the new route there.
  EXPECT_EQ(c.report()[1].reserved_tracks, 1);
  f.demand.horizontal[0] = 1.1;
  EXPECT_FALSE(c.check(f.wire(30), f.demand).legal);
}
TEST(RouteCapacity, ObstacleBeyondBinBoundaryStillRemovesNearbyTrack) {
  Fixture f;
  f.grid.bins_y = 2;
  // This rectangle lies wholly in the second bin, but its keepout reaches
  // the 90-DBU track in the first bin as well as the 110-DBU track.
  f.obstacles.insert({{"M2", {80, 101, 90, 102}}, "VDD"});
  Route_capacity c(f.tech, f.fp, f.obstacles, f.grid);
  EXPECT_EQ(c.report()[0].usable_tracks, 4);
  EXPECT_EQ(c.report()[1].usable_tracks, 4);
}
TEST(RouteCapacity, FractionalRudyBinsAndDuplicateTracks) {
  Fixture f;
  f.grid.bins_x       = 3;
  f.demand.horizontal = f.demand.vertical = std::vector<double>(3, 0);
  f.fp.tracks.push_back(f.fp.tracks.front());
  Route_capacity c(f.tech, f.fp, f.obstacles, f.grid);
  EXPECT_EQ(c.report()[0].usable_tracks, 10);
  const auto trial = c.check(f.wire(10), f.demand);
  ASSERT_TRUE(trial.legal) << trial.reason;
  c.commit(trial);
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(c.report()[i].reserved_tracks, 1);
  }
}
TEST(RouteCapacity, RejectsMissingDemandForeignTrialsAndUnknownLayers) {
  Fixture        f;
  Route_capacity a(f.tech, f.fp, f.obstacles, f.grid), b(f.tech, f.fp, f.obstacles, f.grid);
  const auto     trial = a.check(f.wire(10), f.demand);
  ASSERT_TRUE(trial.legal);
  EXPECT_THROW(b.commit(trial), std::invalid_argument);
  auto invalid  = trial;
  invalid.legal = false;
  // Approval is internal, not granted by editing the public report flag.
  Route_capacity_trial forged;
  forged.legal = true;
  EXPECT_THROW(a.commit(forged), std::invalid_argument);
  EXPECT_FALSE(a.check({{"M4", {0, 0, 10, 10}}}, f.demand).legal);
  f.demand.horizontal.clear();
  EXPECT_FALSE(a.check(f.wire(10), f.demand).legal);
}
TEST(RouteCapacity, SpacingKeepoutReservesAdjacentBinTracks) {
  Fixture f;
  f.grid.bins_y                = 2;
  f.grid.max_preroute_fraction = 1;
  f.demand.horizontal = f.demand.vertical       = std::vector<double>(2, 0);
  f.tech.layers["M2"].rules.spacing[0].distance = .025;
  Route_capacity c(f.tech, f.fp, f.obstacles, f.grid);
  auto           trial = c.check(f.wire(90), f.demand);
  ASSERT_TRUE(trial.legal) << trial.reason;
  c.commit(trial);
  EXPECT_EQ(c.report()[0].reserved_tracks, 2);
  EXPECT_EQ(c.report()[1].reserved_tracks, 1);
}
TEST(RouteCapacity, ReportsRemainingPinHotspotWithXMajorCoordinates) {
  Fixture f;
  f.grid.bins_x = 2;
  f.grid.bins_y = 3;
  f.demand.horizontal.assign(6, 0);
  f.demand.vertical.assign(6, 0);
  f.demand.pin_utilization.assign(6, 0);
  f.demand.pin_utilization[4] = 1.25;
  Route_capacity c(f.tech, f.fp, f.obstacles, f.grid);
  const auto     rejected = c.check(f.wire(10), f.demand);
  EXPECT_FALSE(rejected.legal);
  EXPECT_EQ(rejected.reason, "remaining pin access exceeds capacity");
  EXPECT_EQ(rejected.detail, "bin=(1,1) pin_utilization=1.25 maximum_utilization=1");
  EXPECT_THROW(c.commit(rejected), std::invalid_argument);
}
TEST(RouteCapacity, RejectsRemainingAccessFailures) {
  Fixture        f;
  Route_capacity c(f.tech, f.fp, f.obstacles, f.grid);
  f.demand.blocked_pins = 1;
  EXPECT_FALSE(c.check(f.wire(10), f.demand).legal);
  f.demand.blocked_pins    = 0;
  f.demand.unroutable_nets = 1;
  EXPECT_FALSE(c.check(f.wire(10), f.demand).legal);
  f.demand.unroutable_nets = 0;
  f.demand.pin_utilization = {1.1};
  EXPECT_FALSE(c.check(f.wire(10), f.demand).legal);
  f.demand.pin_utilization = {.5};
  EXPECT_TRUE(c.check(f.wire(10), f.demand).legal);
}
TEST(RouteCapacity, ObstacleTrackSweepMatchesExhaustiveSpacing) {
  namespace ph = livehd::physical;
  using namespace livehd::placement;
  std::mt19937 random(37);
  for (int iteration = 0; iteration < 20; ++iteration) {
    Fixture f;
    f.grid.bins_x = 3;
    f.grid.bins_y = 4;
    std::vector<ph::Shape> shapes;
    for (int i = 0; i < 24; ++i) {
      const double x = int(random() % 260) - 30;
      const double y = int(random() % 260) - 30;
      ph::Shape shape{i % 2 ? "M2" : "M3", {x, y, x + 1 + random() % 20, y + 1 + random() % 20}};
      f.obstacles.insert({shape, "VDD"});
      shapes.push_back(shape);
    }
    const Route_capacity capacity(f.tech, f.fp, f.obstacles, f.grid);
    const ph::Signal_metal_rules rules(f.tech, f.fp);
    for (const auto& cell : capacity.report()) {
      const bool horizontal = cell.layer == "M2";
      const double xl = cell.x * 200. / f.grid.bins_x;
      const double xh = (cell.x + 1) * 200. / f.grid.bins_x;
      const double yl = cell.y * 200. / f.grid.bins_y;
      const double yh = (cell.y + 1) * 200. / f.grid.bins_y;
      size_t expected = 0;
      for (int coordinate = 10; coordinate < 200; coordinate += 20) {
        if (coordinate < (horizontal ? yl : xl) || coordinate >= (horizontal ? yh : xh)) {
          continue;
        }
        const double c = coordinate;
        const ph::Shape footprint{cell.layer, horizontal ? ph::Rect{std::floor(xl), c - 5, std::ceil(xh), c + 5}
                                                        : ph::Rect{c - 5, std::floor(yl), c + 5, std::ceil(yh)}};
        bool usable = true;
        for (const auto& shape : shapes) {
          if (shape.layer == cell.layer && !rules.spacing(footprint, shape).legal) {
            usable = false;
          }
        }
        expected += usable;
      }
      EXPECT_EQ(cell.usable_tracks, expected) << iteration << " " << cell.layer << " " << cell.x << " " << cell.y;
    }
  }
}

TEST(RouteCapacity, AdditiveSeedMatchesFullGeometryAndRemainingDemand) {
  Fixture f;
  auto& macro = f.tech.macros["buffer"];
  macro.width = macro.height = .020;
  macro.obstructions = {{"M2", {0, 0, .020, .010}}};
  macro.pins["A"].shapes = {{"M3", {.005, 0, .015, .020}}};
  f.grid.bins_x = 3;
  f.grid.bins_y = 4;
  f.grid.max_preroute_fraction = 1;
  f.fp.components.push_back({"original", "buffer", Ann_place(40, 40, 20, 20)});
  f.fp.components.back().place.status = ph::Status::PLACED;
  Route_capacity_seed seed(f.tech, f.fp, f.grid);
  std::mt19937 random(71);
  for (int iteration = 0; iteration < 24; ++iteration) {
    auto frame = f.fp;
    frame.components[0].place.status = ph::Status::FIXED;
    for (int i = 0; i < 8; ++i) {
      ph::Component cell{"added" + std::to_string(i), "buffer", Ann_place(int(random() % 240) - 20, int(random() % 240) - 20, 20, 20)};
      cell.place.status = ph::Status::FIXED;
      cell.place.orientation = static_cast<ph::Orientation>(random() % 8);
      frame.components.push_back(cell);
    }
    auto reused = seed.instantiate(f.tech, frame, f.grid);
    ASSERT_NE(reused, nullptr);
    const auto obstacles = ph::routing_occupancy(f.tech, frame, frame.dbu, {}, nullptr, ph::Signal_wires::exclude);
    Route_capacity full(f.tech, frame, obstacles, f.grid);
    EXPECT_EQ(reused->report(), full.report());
    Remaining_route_demand demand;
    demand.horizontal_capacity = demand.vertical_capacity = .05;
    for (size_t i = 0; i < 12; ++i) {
      demand.horizontal.push_back((random() % 100) / 100.);
      demand.vertical.push_back((random() % 100) / 100.);
    }
    const auto a = reused->precheck_demand(demand), b = full.precheck_demand(demand);
    EXPECT_EQ(a.legal, b.legal);
    EXPECT_EQ(a.reason, b.reason);
    EXPECT_EQ(a.detail, b.detail);
    for (int y = 10; y < 200; y += 20) {
      const auto x = reused->check(f.wire(y), demand), z = full.check(f.wire(y), demand);
      EXPECT_EQ(x.legal, z.legal);
      EXPECT_EQ(x.reason, z.reason);
      EXPECT_EQ(x.detail, z.detail);
      if (x.legal && z.legal) {
        reused->commit(x);
        full.commit(z);
        EXPECT_EQ(reused->report(), full.report());
      }
    }
  }
}

TEST(RouteCapacity, SeedTrialsKeepIndependentReservationsAndProofOwnership) {
  Fixture f;
  Route_capacity_seed seed(f.tech, f.fp, f.grid);
  auto a = seed.instantiate(f.tech, f.fp, f.grid);
  auto b = seed.instantiate(f.tech, f.fp, f.grid);
  ASSERT_TRUE(a && b);
  const auto trial = a->check(f.wire(10), f.demand);
  ASSERT_TRUE(trial.legal);
  EXPECT_THROW(b->commit(trial), std::invalid_argument);
  a->commit(trial);
  EXPECT_EQ(a->report()[0].reserved_tracks, 1);
  EXPECT_EQ(b->report()[0].reserved_tracks, 0);
  b->reserve_existing(f.wire(10));
  EXPECT_EQ(a->report(), b->report());
  auto fresh = seed.instantiate(f.tech, f.fp, f.grid);
  EXPECT_EQ(fresh->report()[0].reserved_tracks, 0);
  EXPECT_THROW(fresh->check_remaining(a->precheck(f.wire(50)), f.demand), std::invalid_argument);
  EXPECT_THROW(seed.instantiate(f.tech, f.fp, f.grid, [] { throw std::runtime_error("cancel"); }), std::runtime_error);
  EXPECT_EQ(seed.instantiate(f.tech, f.fp, f.grid)->report(), fresh->report());
}

TEST(RouteCapacity, SeedRejectsChangedExistingGeometryAndConfiguration) {
  Fixture f;
  f.tech.macros["buffer"].width = f.tech.macros["buffer"].height = .02;
  f.fp.components.push_back({"original", "buffer", Ann_place(40, 40, 20, 20)});
  f.fp.components.back().place.status = ph::Status::PLACED;
  Route_capacity_seed seed(f.tech, f.fp, f.grid);
  auto changed = f.fp;
  ++changed.components[0].place.x;
  EXPECT_EQ(seed.instantiate(f.tech, changed, f.grid), nullptr);
  changed = f.fp;
  changed.components[0].place.status = ph::Status::UNPLACED;
  EXPECT_EQ(seed.instantiate(f.tech, changed, f.grid), nullptr);
  changed = f.fp;
  ++changed.tracks[0].start;
  EXPECT_EQ(seed.instantiate(f.tech, changed, f.grid), nullptr);
  changed = f.fp;
  changed.special_nets.push_back({});
  EXPECT_EQ(seed.instantiate(f.tech, changed, f.grid), nullptr);
  changed = f.fp;
  changed.nondefault_rules["new_rule"]["M2"] = 20;
  EXPECT_EQ(seed.instantiate(f.tech, changed, f.grid), nullptr);
  auto grid = f.grid;
  grid.max_preroute_fraction = .5;
  EXPECT_EQ(seed.instantiate(f.tech, f.fp, grid), nullptr);
  const auto other_tech = f.tech;
  EXPECT_EQ(seed.instantiate(other_tech, f.fp, f.grid), nullptr);
}

TEST(RouteCapacity, SeedRestoresExistingWiresAfterAddedPinObstructions) {
  for (const bool count_only_usable : {false, true}) {
    Fixture f;
    f.grid.count_only_usable_tracks = count_only_usable;
    auto& macro = f.tech.macros["buffer"];
    macro.width = macro.height = .020;
    macro.pins["A"].shapes = {{"M2", {0, 0, .020, .010}}};
    Route_capacity_seed seed(f.tech, f.fp, f.grid);
    auto frame = f.fp;
    frame.components.push_back({"added", "buffer", Ann_place(40, 10, 20, 20)});
    frame.components.back().place.status = ph::Status::FIXED;
    auto reused = seed.instantiate(f.tech, frame, f.grid);
    ASSERT_NE(reused, nullptr);
    const auto obstacles = ph::routing_occupancy(f.tech, frame, frame.dbu, {}, nullptr, ph::Signal_wires::exclude);
    Route_capacity full(f.tech, frame, obstacles, f.grid);
    ASSERT_EQ(reused->report(), full.report());
    // An added pin blocks an existing route's track. Both construction paths
    // must apply the same policy and restore reservations atomically.
    const auto before = reused->report();
    if (count_only_usable) {
      reused->reserve_existing(f.wire(10));
      full.reserve_existing(f.wire(10));
    } else {
      EXPECT_THROW(reused->reserve_existing(f.wire(10)), std::invalid_argument);
      EXPECT_THROW(full.reserve_existing(f.wire(10)), std::invalid_argument);
      EXPECT_EQ(reused->report(), before);
    }
    reused->reserve_existing(f.wire(170));
    full.reserve_existing(f.wire(170));
    EXPECT_EQ(reused->report(), full.report());
    EXPECT_EQ(seed.instantiate(f.tech, f.fp, f.grid)->report()[0].usable_tracks, 10);
  }
}

TEST(RouteCapacity, WireReservationDistanceFilterMatchesExhaustiveSpacing) {
  Fixture f;
  f.grid.max_preroute_fraction = 1;
  std::mt19937 random(89);
  for (int iteration = 0; iteration < 40; ++iteration) {
    if (iteration == 20) {
      for (auto& [name, layer] : f.tech.layers) {
        layer.rules.properties = {
            {"LEF58_SPACING", {"SPACING .018 ENDOFLINE .025 WITHIN .02 ENDTOEND .031 PARALLELEDGE .025 WITHIN .02 ;"}},
            {"LEF58_EOLKEEPOUT", {"EOLKEEPOUT .025 EXTENSION 0 .0125 .031 CORNERONLY ;"}},
            {"LEF58_CORNERSPACING", {"CORNERSPACING CONVEXCORNER WIDTH 0 SPACING .02 ;"}}};
      }
    }
    const ph::Signal_metal_rules rules(f.tech, f.fp);
    Route_capacity capacity(f.tech, f.fp, f.obstacles, f.grid);
    std::vector<ph::Shape> shapes;
    for (int i = 0; i < 6; ++i) {
      const double x = random() % 190, y = random() % 190;
      shapes.push_back({i % 2 ? "M2" : "M3", {x, y, x + 1 + random() % 10, y + 1 + random() % 10}});
    }
    capacity.reserve_existing(shapes);
    for (const auto& cell : capacity.report()) {
      size_t expected = 0;
      for (int coordinate = 10; coordinate < 200; coordinate += 20) {
        const double c = coordinate;
        const ph::Shape track{cell.layer, cell.layer == "M2" ? ph::Rect{0, c - 5, 200, c + 5}
                                                            : ph::Rect{c - 5, 0, c + 5, 200}};
        bool reserved = false;
        for (const auto& shape : shapes) {
          reserved |= shape.layer == cell.layer && !rules.spacing(track, shape).legal;
        }
        expected += reserved;
      }
      EXPECT_EQ(cell.reserved_tracks, expected) << iteration << " " << cell.layer;
    }
  }
}


}  // namespace livehd::placement
