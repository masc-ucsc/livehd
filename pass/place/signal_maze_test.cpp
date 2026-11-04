// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <set>

#include "gtest/gtest.h"
#include "inou/lefdef/routing_connectivity.hpp"
#include "inou/lefdef/routing_geometry.hpp"
#include "inou/lefdef/signal_via.hpp"
#include "signal_router.hpp"

namespace livehd::placement {
namespace {
namespace ph = physical;
std::vector<Signal_route_via> route_vias(const Signal_route_geometry& geometry) {
  std::vector<Signal_route_via> result;
  for (const auto& path : geometry.paths) {
    ph::Route_point point;
    for (const auto& step : path.steps) {
      if (const auto* at = std::get_if<ph::Route_point>(&step)) {
        point = *at;
      } else if (const auto* via = std::get_if<ph::Route_via>(&step)) {
        result.push_back({*via, point.x, point.y});
      }
    }
  }
  return result;
}
struct Maze_fixture {
  ph::Technology       tech;
  ph::Floorplan        frame;
  Signal_route_profile profile{
      {"pin", "horizontal", "contact"},
      "vertical", "cut", {"horizontal", "vertical"}
  };
  ph::Net                net{"signal", {}, {}, "SIGNAL"};
  std::vector<ph::Shape> sources{
      {"horizontal", {980, 990, 1020, 1010}}
  };
  std::vector<ph::Shape> targets{
      {"horizontal", {4980, 4990, 5020, 5010}}
  };

  Maze_fixture() {
    tech.manufacturing_grid = .001;
    tech.routing_layers     = {"horizontal", "vertical"};
    for (const auto& name : tech.routing_layers) {
      auto& layer         = tech.layers[name];
      layer.width         = .020;
      layer.direction     = name == "horizontal" ? "HORIZONTAL" : "VERTICAL";
      layer.rules.loaded  = true;
      layer.rules.area    = .0004;
      layer.rules.spacing = {
          {.020, {}, {}}
      };
    }
    tech.cut_layers["cut"].width   = .010;
    tech.cut_layers["cut"].spacing = {.010};
    tech.vias["via"].shapes        = {
        {"horizontal", {-.010, -.010, .010, .010}},
        {  "vertical", {-.010, -.010, .010, .010}},
        {       "cut", {-.005, -.005, .005, .005}}
    };
    frame.die    = {0, 0, 10000, 10000};
    frame.tracks = {
        {"Y", "horizontal", 0, 251, 40},
        {"X",   "vertical", 0, 251, 40}
    };
  }

  std::vector<ph::Route_path> with_prior(const Signal_route_geometry& geometry, std::span<const Signal_route_via> prior) {
    auto paths = geometry.paths;
    for (const auto& p : prior) {
      const auto shapes = ph::via_geometry(tech, frame, p.via);
      const auto layer  = std::find_if(tech.routing_layers.begin(), tech.routing_layers.end(), [&](const auto& name) {
        return std::any_of(shapes.begin(), shapes.end(), [&](const auto& shape) { return shape.layer == name; });
      });
      if (layer == tech.routing_layers.end()) {
        ADD_FAILURE() << "prior via missing routing layer";
        return {};
      }
      auto path                                  = ph::via_path(*layer, p.x, p.y, p.via.name);
      std::get<ph::Route_via>(path.steps.back()) = p.via;
      paths.push_back(std::move(path));
    }
    return paths;
  }
  void check_connected(const Signal_route_geometry& geometry, std::span<const Signal_route_via> prior = {}) {
    auto routed = net;
    routed.wires.push_back({"ROUTED", with_prior(geometry, prior)});
    auto fp = frame;
    for (const auto& [name, shape] : {
             std::pair{"source", sources.front()},
             std::pair{"target", targets.front()}
    }) {
      ph::Terminal terminal;
      terminal.name   = name;
      terminal.net    = net.name;
      terminal.status = ph::Status::FIXED;
      terminal.shapes = {shape};
      fp.terminals.push_back(terminal);
      routed.connections.push_back({"PIN", name});
    }
    EXPECT_TRUE(ph::check_route_connectivity(tech, fp, routed, nullptr, &geometry.nondefault_rules).connected);
  }
  void check_cut_union(const Signal_route_geometry& geometry, std::span<const Signal_route_via> prior) {
    ph::Signal_cut_set                                                   cuts(tech, frame);
    std::set<std::tuple<std::string, ph::Orientation, int64_t, int64_t>> seen;
    auto       all   = std::vector<Signal_route_via>(prior.begin(), prior.end());
    const auto added = route_vias(geometry);
    all.insert(all.end(), added.begin(), added.end());
    for (const auto& p : all) {
      if (seen.emplace(p.via.name, p.via.orientation, p.x, p.y).second) {
        EXPECT_TRUE(cuts.add(p.via, p.x, p.y)) << p.via.name << " at " << p.x << ',' << p.y;
      }
    }
  }
  void pin_cells(const std::vector<std::pair<int64_t, int64_t>>& points) {
    tech.routing_layers.insert(tech.routing_layers.begin(), "pin");
    tech.layers["pin"]         = tech.layers.at("vertical");
    tech.cut_layers["contact"] = tech.cut_layers.at("cut");
    tech.vias["access"].shapes = {
        {       "pin", {-.010, -.010, .010, .010}},
        {"horizontal", {-.010, -.010, .010, .010}},
        {   "contact", {-.005, -.005, .005, .005}}
    };
    auto& cell = tech.macros["cell"];
    cell.klass = "CORE";
    cell.width = cell.height = .100;
    cell.pins["A"]           = {"INPUT", "SIGNAL", {{"pin", {.040, .040, .060, .060}}}};
    for (const auto& [x, y] : points) {
      const auto name = "u" + std::to_string(frame.components.size());
      Ann_place  pose(x - 50, y - 50, 100, 100);
      pose.status = ph::Status::PLACED;
      frame.components.push_back({name, "cell", pose});
      net.connections.push_back({name, "A"});
    }
  }
};
}  // namespace

TEST(SignalMaze, DistantJoinDoesNotFillTheEndpointRectangle) {
  for (bool vertical : {false, true}) {
    Maze_fixture f;
    if (vertical) {
      f.sources = {
          {"vertical", {990, 980, 1010, 1020}}
      };
      f.targets = {
          {"vertical", {4990, 4980, 5010, 5020}}
      };
    }
    ph::Routing_occupancy     occupancy(1000);
    Signal_router             router(f.tech, f.frame, occupancy, f.profile);
    const Signal_route_limits limits{0, 1000, std::chrono::steady_clock::now() + std::chrono::seconds(5)};
    const auto                result = router.propose_join(f.net, limits, f.sources, f.targets, {});
    ASSERT_TRUE(result.geometry) << result.reason << " expansions=" << result.patterns_examined;
    EXPECT_LE(result.patterns_examined, limits.patterns);
    EXPECT_EQ(result.geometry->vias, 2u);
    f.check_connected(*result.geometry);
    EXPECT_EQ(occupancy.size(), 0u);
    RecordProperty(vertical ? "vertical_expansions" : "horizontal_expansions", int(result.patterns_examined));
  }
}

TEST(SignalMaze, SameTrackJoinDoesNotRequireLayerChanges) {
  Maze_fixture f;
  f.targets.front().rect = {4980, 990, 5020, 1010};
  ph::Routing_occupancy     occupancy(1000);
  Signal_router             router(f.tech, f.frame, occupancy, f.profile);
  const Signal_route_limits limits{0, 1000, std::chrono::steady_clock::now() + std::chrono::seconds(5)};
  const auto                result = router.propose_join(f.net, limits, f.sources, f.targets, {});
  ASSERT_TRUE(result.geometry) << result.reason;
  EXPECT_EQ(result.geometry->vias, 0u);
  f.check_connected(*result.geometry);
}

TEST(SignalMaze, SaturatedNodeStorageIsReportedAsABudgetLimit) {
  Maze_fixture              f;
  ph::Routing_occupancy     occupancy(1000);
  Signal_router             router(f.tech, f.frame, occupancy, f.profile);
  const Signal_route_limits limits{0, 256, std::chrono::steady_clock::now() + std::chrono::seconds(5)};
  const auto                result = router.propose_join(f.net, limits, f.sources, f.targets, {});
  EXPECT_FALSE(result.geometry);
  EXPECT_EQ(result.reason, "maze node budget");
  EXPECT_LE(result.patterns_examined, limits.patterns);
  EXPECT_EQ(occupancy.size(), 0u);
}

TEST(SignalMaze, ForeignOverlapCannotBeRepairedByTryingWiderLandings) {
  Maze_fixture f;
  f.net.name = "nominal_overlap";
  for (const auto& layer : f.tech.routing_layers) {
    f.tech.layers[layer].rules.properties["LEF58_WIDTHTABLE"] = {"WIDTHTABLE .020 .024 .028 .032 .036 .040 .044 .048 ;"};
  }
  ph::Routing_occupancy occupancy(1000);
  // A fixed blockage covers the vertical layer. Source/target horizontal metal
  // remains legal, but no width of via landing can enter the vertical layer.
  occupancy.insert({
      {"vertical", f.frame.die},
      "foreign",
      true
  });
  const Signal_route_limits limits{0, 768, std::chrono::steady_clock::now() + std::chrono::seconds(5)};
  const auto result = Signal_router(f.tech, f.frame, occupancy, f.profile).propose_join(f.net, limits, f.sources, f.targets, {});
  EXPECT_FALSE(result.geometry);
  EXPECT_EQ(result.reason, "maze exhausted");
  // Exhaust the single reachable horizontal track, not the width-pair budget.
  // LHD_SIGNAL_MAZE_TRACE reports nominal_overlap_sites>0 and zero via trials.
  EXPECT_LT(result.patterns_examined, 300u);
  EXPECT_EQ(occupancy.size(), 1u);
}

TEST(SignalMaze, NonOverlappingEolFailureStillTriesWiderLandings) {
  Maze_fixture f;
  f.net.name = "nonoverlap_eol";
  f.targets  = {
      {"vertical", {990, 980, 1010, 1020}}
  };
  auto& rules                          = f.tech.layers["vertical"].rules;
  rules.properties["LEF58_WIDTHTABLE"] = {"WIDTHTABLE .020 .032 .040 ;"};
  rules.properties["LEF58_SPACING"]    = {"SPACING .100 ENDOFLINE .025 WITHIN .080 ENDTOEND .100 PARALLELEDGE .100 WITHIN .080 ;"};
  const ph::Signal_metal_rules compiled(f.tech, f.frame);
  const ph::Shape              obstacle{
      "vertical",
      {1060, 1080, 1100, 1120}
  };
  EXPECT_FALSE(compiled
                   .spacing(
                       {
                           "vertical",
                           {990, 990, 1010, 1010}
  },
                       obstacle)
                   .legal);
  EXPECT_TRUE(compiled
                  .spacing(
                      {
                          "vertical",
                          {984, 984, 1016, 1016}
  },
                      obstacle)
                  .legal);
  ph::Routing_occupancy occupancy(1000);
  occupancy.insert({obstacle, "foreign", true});
  const Signal_route_limits limits{0, 1000, std::chrono::steady_clock::now() + std::chrono::seconds(5)};
  const auto result = Signal_router(f.tech, f.frame, occupancy, f.profile).propose_join(f.net, limits, f.sources, f.targets, {});
  ASSERT_TRUE(result.geometry) << result.reason;
  EXPECT_FALSE(result.geometry->nondefault_rules.empty());
  EXPECT_EQ(result.geometry->vias, 1u);
  f.check_connected(*result.geometry);
  EXPECT_EQ(occupancy.size(), 1u);
}

TEST(SignalMaze, IntrinsicWidthFailuresAreCachedOnlyWithinOneRequest) {
  Maze_fixture f;
  f.net.name = "intrinsic_width_cache";
  for (const auto& layer : f.tech.routing_layers) {
    f.tech.layers[layer].rules.properties["LEF58_WIDTHTABLE"] = {"WIDTHTABLE .020 .024 .028 .032 .040 ;"};
  }
  f.tech.cut_layers["cut"].properties["LEF58_CUTCLASS"]  = {"CUTCLASS square WIDTH .010 ;"};
  f.tech.cut_layers["cut"].properties["LEF58_ENCLOSURE"] = {"ENCLOSURE CUTCLASS square EOL .03225 .012 .001 ;"};
  ph::Routing_occupancy     occupancy(1000);
  const Signal_route_limits limits{0, 700, std::chrono::steady_clock::now() + std::chrono::seconds(5)};
  const auto wide = Signal_router(f.tech, f.frame, occupancy, f.profile).propose_join(f.net, limits, f.sources, f.targets, {});
  ASSERT_TRUE(wide.geometry) << wide.reason << " charged=" << wide.patterns_examined;
  EXPECT_FALSE(wide.geometry->nondefault_rules.empty());
  EXPECT_LT(wide.patterns_examined, limits.patterns);
  f.check_connected(*wide.geometry);
  // A new technology/request permits nominal widths. No previous invalid-pair
  // result may survive into this call, even though the via/layer names match.
  f.tech.cut_layers["cut"].properties.clear();
  for (const auto& layer : f.tech.routing_layers) {
    f.tech.layers[layer].rules.properties.clear();
  }
  const auto nominal = Signal_router(f.tech, f.frame, occupancy, f.profile).propose_join(f.net, limits, f.sources, f.targets, {});
  ASSERT_TRUE(nominal.geometry) << nominal.reason;
  EXPECT_TRUE(nominal.geometry->nondefault_rules.empty());
  f.check_connected(*nominal.geometry);
  EXPECT_EQ(occupancy.size(), 0u);
}
TEST(SignalMaze, ProposedCutsForceALegalUpperLayerDetour) {
  Maze_fixture f;
  f.targets = {
      {"horizontal", {4980, 1030, 5020, 1050}}
  };
  f.tech.cut_layers["cut"].spacing = {.050};
  // Two lower-layer turns on the neighboring tracks have a 30 DBU cut gap,
  // below the unchanged 50 DBU rule. The upper layer permits a legal detour.
  ph::Signal_cut_set direct(f.tech, f.frame);
  ASSERT_TRUE(direct.add({"via"}, 1000, 1000));
  ASSERT_FALSE(direct.add({"via"}, 1000, 1040));
  f.tech.routing_layers.push_back("upper");
  f.tech.layers["upper"]          = f.tech.layers.at("horizontal");
  f.tech.cut_layers["upper_cut"]  = f.tech.cut_layers.at("cut");
  f.tech.vias["upper_via"].shapes = {
      { "vertical", {-.010, -.010, .010, .010}},
      {    "upper", {-.010, -.010, .010, .010}},
      {"upper_cut", {-.005, -.005, .005, .005}}
  };
  f.frame.tracks.push_back({"Y", "upper", 0, 251, 40});
  f.profile.signal_layers.push_back("upper");
  ph::Routing_occupancy occupancy(1000);
  occupancy.insert({
      {"horizontal", {1100, 980, 4800, 1010}},
      "foreign"
  });
  const Signal_route_limits limits{0, 8192, std::chrono::steady_clock::now() + std::chrono::seconds(5)};
  const auto result = Signal_router(f.tech, f.frame, occupancy, f.profile).propose_join(f.net, limits, f.sources, f.targets, {});
  ASSERT_TRUE(result.geometry) << result.reason << " charged=" << result.patterns_examined;
  EXPECT_LE(result.patterns_examined, limits.patterns);
  EXPECT_GT(result.geometry->vias, 2u);
  EXPECT_TRUE(result.geometry->layer_wire_length_um.contains("upper"));
  ph::Signal_cut_set cuts(f.tech, f.frame);
  for (const auto& path : result.geometry->paths) {
    ph::Route_point point;
    for (const auto& step : path.steps) {
      if (const auto* at = std::get_if<ph::Route_point>(&step)) {
        point = *at;
      } else if (const auto* via = std::get_if<ph::Route_via>(&step)) {
        EXPECT_TRUE(cuts.add(*via, point.x, point.y));
      }
    }
  }
  f.check_connected(*result.geometry);
  EXPECT_EQ(occupancy.size(), 1u);
}
TEST(SignalMaze, AnchorsOutsideTheWindowDoNotShapeTheSearch) {
  // A join with one anchor inside the die and a remote piece outside it gives exactly the result without the remote
  // piece; with only the remote piece there is no target in the window.
  Maze_fixture f;
  const ph::Shape remote{"horizontal", {12000, 990, 12100, 1010}};  // die is [0,10000]^2
  ph::Routing_occupancy     occupancy(1000);
  Signal_router             router(f.tech, f.frame, occupancy, f.profile);
  const Signal_route_limits limits{0, 8192, std::chrono::steady_clock::now() + std::chrono::seconds(5)};
  const auto                plain = router.propose_join(f.net, limits, f.sources, f.targets, {});
  auto                      mixed_targets = f.targets;
  mixed_targets.push_back(remote);
  auto mixed_sources = f.sources;
  mixed_sources.push_back(remote);  // also on the source side: a discarded pair must not move the window
  const auto mixed = router.propose_join(f.net, limits, mixed_sources, mixed_targets, {});
  ASSERT_TRUE(plain.geometry) << plain.reason;
  ASSERT_TRUE(mixed.geometry) << mixed.reason;
  EXPECT_EQ(mixed.geometry->paths, plain.geometry->paths);
  EXPECT_EQ(mixed.patterns_examined, plain.patterns_examined);
  const auto none = router.propose_join(f.net, limits, f.sources, {remote}, {});
  EXPECT_FALSE(none.geometry);
  EXPECT_EQ(none.reason, "no anchor inside the maze window");
}

TEST(SignalMaze, PartlyOutsidePiecesSearchLikeTheirInWindowPart) {
  // Pieces that reach past the die behave exactly like their clipped part: the window reference, extent and goals all
  // come from the trimmed view.
  Maze_fixture f;
  ph::Routing_occupancy     occupancy(1000);
  Signal_router             router(f.tech, f.frame, occupancy, f.profile);
  const Signal_route_limits limits{0, 8192, std::chrono::steady_clock::now() + std::chrono::seconds(5)};
  const std::vector<ph::Shape> clipped_s{{"horizontal", {980, 990, 10000, 1010}}}, clipped_t{{"horizontal", {4980, 2990, 10000, 3010}}};
  const std::vector<ph::Shape> long_s{{"horizontal", {980, 990, 50000, 1010}}}, long_t{{"horizontal", {4980, 2990, 50000, 3010}}};
  const auto a = router.propose_join(f.net, limits, clipped_s, clipped_t, {});
  const auto b = router.propose_join(f.net, limits, long_s, long_t, {});
  ASSERT_TRUE(a.geometry) << a.reason;
  ASSERT_TRUE(b.geometry) << b.reason;
  EXPECT_EQ(b.geometry->paths, a.geometry->paths);
  EXPECT_EQ(b.patterns_examined, a.patterns_examined);
}

TEST(SignalMaze, KeptCutHistoriesRecoverAPathTheSingleBestLabelLoses) {
  // The cheap prefix turns at via A (1000,1000); the only exit is via B (1040,1000), whose cut is 30 DBU from A's
  // (spacing 50). The one legal route avoids A: E(1000,880) -> D(880,880) -> C(880,1000) -> B. With one label per
  // coordinate the cheaper A-prefix wins (1000,1000) and the search exhausts with work left; four labels keep it.
  Maze_fixture f;
  f.tech.cut_layers["cut"].spacing = {.050};
  f.frame.tracks                   = {
      {"Y", "horizontal",  880, 2, 120},
      {"X",   "vertical",  880, 2, 120},
      {"X",   "vertical", 1040, 1,   1}
  };
  f.sources = {
      {"vertical", {990, 870, 1010, 890}}
  };
  f.targets = {
      {"vertical", {1030, 1110, 1050, 1130}}
  };
  ph::Routing_occupancy occupancy(1000);
  occupancy.insert({
      {"vertical", {1030, 900, 1050, 940}},
      "foreign"
  });
  const auto run = [&](size_t labels) {
    Signal_route_limits limits{0, 100000, std::chrono::steady_clock::now() + std::chrono::seconds(5)};
    limits.maze_labels = labels;
    return Signal_router(f.tech, f.frame, occupancy, f.profile).propose_join(f.net, limits, f.sources, f.targets, {});
  };
  const auto single = run(1);
  EXPECT_FALSE(single.geometry);
  EXPECT_EQ(single.reason, "maze exhausted");  // lost during the search, not a later geometry rejection
  EXPECT_LT(single.patterns_examined, 100000u) << single.reason;  // exhausted, not out of budget
  EXPECT_FALSE(single.timed_out);
  const auto kept = run(4);
  ASSERT_TRUE(kept.geometry) << kept.reason << " charged=" << kept.patterns_examined;
  ph::Signal_cut_set cuts(f.tech, f.frame);  // the whole cut union, independently
  for (const auto& path : kept.geometry->paths) {
    ph::Route_point point;
    for (const auto& step : path.steps) {
      if (const auto* at = std::get_if<ph::Route_point>(&step)) {
        point = *at;
      } else if (const auto* via = std::get_if<ph::Route_via>(&step)) {
        EXPECT_TRUE(cuts.add(*via, point.x, point.y)) << via->name << " at " << point.x << ',' << point.y;
      }
    }
  }
  f.check_connected(*kept.geometry);
  EXPECT_EQ(occupancy.size(), 1u);  // proposals never change the occupancy
}
TEST(SignalMaze, ConflictingSourceAndTargetAccessCutsAreNotAccepted) {
  Maze_fixture f;
  f.pin_cells({
      {1000, 1000},
      {1040, 1000}
  });
  f.tech.cut_layers["contact"].spacing = {.050};
  ph::Routing_occupancy     occupancy(1000);
  const Signal_route_limits limits{100, 1000, std::chrono::steady_clock::now() + std::chrono::seconds(5)};
  const auto                result = Signal_router(f.tech, f.frame, occupancy, f.profile).propose_maze(f.net, limits);
  EXPECT_FALSE(result.geometry);
  EXPECT_FALSE(result.timed_out);
  EXPECT_TRUE(result.reason == "maze exhausted" || result.reason == "maze node budget") << result.reason;
  EXPECT_LE(result.patterns_examined, limits.patterns);
  EXPECT_EQ(occupancy.size(), 0u);
}
TEST(SignalMaze, EarlierBranchesReuseExactlyTheSameVia) {
  Maze_fixture f;
  f.pin_cells({
      {1000, 1000},
      {1000, 3000},
      {1000, 5000}
  });
  ph::Routing_occupancy     occupancy(1000);
  const Signal_route_limits limits{100, 8192, std::chrono::steady_clock::now() + std::chrono::seconds(5)};
  const auto                result = Signal_router(f.tech, f.frame, occupancy, f.profile).propose_maze(f.net, limits);
  ASSERT_TRUE(result.geometry) << result.reason;
  // Three pin contacts and three shared lower/upper transitions. The middle
  // transition belongs to both topology branches and must be emitted once.
  EXPECT_EQ(result.geometry->vias, 6u);
  ph::Signal_cut_set cuts(f.tech, f.frame);
  for (const auto& path : result.geometry->paths) {
    ph::Route_point point;
    for (const auto& step : path.steps) {
      if (const auto* at = std::get_if<ph::Route_point>(&step)) {
        point = *at;
      } else if (const auto* via = std::get_if<ph::Route_via>(&step)) {
        EXPECT_TRUE(cuts.add(*via, point.x, point.y));
      }
    }
  }
  auto routed = f.net;
  routed.wires.push_back({"ROUTED", result.geometry->paths});
  EXPECT_TRUE(ph::check_route_connectivity(f.tech, f.frame, routed, nullptr, &result.geometry->nondefault_rules).connected);
  EXPECT_EQ(occupancy.size(), 0u);
}
TEST(SignalMaze, PriorPieceCutsForceADetourWithoutChangingEmptyContextSearch) {
  Maze_fixture f;
  f.tech.cut_layers["cut"].spacing = {.050};
  ph::Routing_occupancy     occupancy(1000);
  Signal_router             router(f.tech, f.frame, occupancy, f.profile);
  const Signal_route_limits limits{0, 8192, std::chrono::steady_clock::now() + std::chrono::seconds(5)};
  const auto                baseline = router.propose_join(f.net, limits, f.sources, f.targets, {});
  const auto                empty    = router.propose_join(f.net, limits, f.sources, f.targets, {}, {});
  ASSERT_TRUE(baseline.geometry) << baseline.reason;
  ASSERT_TRUE(empty.geometry) << empty.reason;
  EXPECT_EQ(empty.geometry->paths, baseline.geometry->paths);
  EXPECT_EQ(empty.patterns_examined, baseline.patterns_examined);
  auto prior = route_vias(*baseline.geometry);
  ASSERT_FALSE(prior.empty());
  prior.resize(1);
  prior[0].x               += 40;  // 30 DBU edge gap is smaller than the unchanged 50 DBU rule.
  const auto         saved  = prior;
  ph::Signal_cut_set old_union(f.tech, f.frame);
  ASSERT_TRUE(old_union.add(prior[0].via, prior[0].x, prior[0].y));
  const auto first = route_vias(*baseline.geometry).front();
  ASSERT_FALSE(old_union.add(first.via, first.x, first.y));
  const auto detour = router.propose_join(f.net, limits, f.sources, f.targets, {}, prior);
  ASSERT_TRUE(detour.geometry) << detour.reason;
  EXPECT_GT(detour.prior_cut_rejections, 0u);
  EXPECT_NE(detour.geometry->paths, baseline.geometry->paths);
  EXPECT_LE(detour.patterns_examined, limits.patterns);
  EXPECT_EQ(prior, saved);
  f.check_cut_union(*detour.geometry, prior);
  f.check_connected(*detour.geometry, prior);
  EXPECT_EQ(occupancy.size(), 0u);
}
TEST(SignalMaze, ExactPriorViaReuseIsNotReemittedAndAccessIdentityRemainsExact) {
  Maze_fixture f;
  f.pin_cells({
      {1000, 1000},
      {3000, 3000}
  });
  ph::Routing_occupancy     occupancy(1000);
  Signal_router             router(f.tech, f.frame, occupancy, f.profile);
  const Signal_route_limits limits{100, 8192, std::chrono::steady_clock::now() + std::chrono::seconds(5)};
  const auto                baseline = router.propose_maze(f.net, limits);
  ASSERT_TRUE(baseline.geometry) << baseline.reason;
  const auto prior = route_vias(*baseline.geometry);
  ASSERT_FALSE(prior.empty());
  const auto reuse = router.propose_maze(f.net, limits, prior);
  ASSERT_TRUE(reuse.geometry) << reuse.reason;
  EXPECT_TRUE(route_vias(*reuse.geometry).empty());
  EXPECT_EQ(reuse.geometry->vias, 0u);
  EXPECT_EQ(reuse.patterns_examined, baseline.patterns_examined);
  f.check_cut_union(*reuse.geometry, prior);
  auto complete = f.net;
  complete.wires.push_back({"ROUTED", reuse.geometry->paths});
  for (const auto& path : baseline.geometry->paths) {
    if (path.steps.size() == 2 && std::holds_alternative<ph::Route_via>(path.steps.back())) {
      complete.wires.back().paths.push_back(path);
    }
  }
  EXPECT_TRUE(ph::check_route_connectivity(f.tech, f.frame, complete).connected);
  // DEF-only alias constrains context but cannot become a freshly enumerated
  // LEF pin access, so exact identity cannot accidentally change in this test.
  f.frame.vias.push_back({"access_alias", ph::via_geometry(f.tech, f.frame, {"access"}), {}});
  for (size_t pin = 0; pin < 2; ++pin) {
    const auto& p = baseline.geometry->endpoints[pin];
    for (bool rotate : {false, true}) {
      const std::vector<Signal_route_via> conflict{
          {{rotate ? p.via : "access_alias", rotate ? ph::Orientation::S : ph::Orientation::N}, p.x, p.y}
      };
      const auto rejected = Signal_router(f.tech, f.frame, occupancy, f.profile).propose_maze(f.net, limits, conflict);
      EXPECT_FALSE(rejected.geometry) << "pin=" << pin << " rotate=" << rotate;
      EXPECT_GT(rejected.prior_cut_rejections, 0u);
      EXPECT_FALSE(rejected.timed_out);
      EXPECT_LE(rejected.patterns_examined, limits.patterns);
    }
  }
  EXPECT_EQ(occupancy.size(), 0u);
}
TEST(SignalMaze, PriorCutRejectsPatternThenUsesOnlyRemainingMazeBudget) {
  Maze_fixture f;
  f.pin_cells({
      {1000, 1000},
      {3000, 3000}
  });
  f.tech.cut_layers["cut"].spacing = {.050};
  ph::Routing_occupancy     occupancy(1000);
  Signal_router             router(f.tech, f.frame, occupancy, f.profile);
  const Signal_route_limits limits{300, 8192, std::chrono::steady_clock::now() + std::chrono::seconds(5)};
  const auto                pattern = router.propose_pattern(f.net, limits);
  ASSERT_TRUE(pattern.geometry) << pattern.reason;
  const auto pattern_vias = route_vias(*pattern.geometry);
  const auto turn = std::find_if(pattern_vias.begin(), pattern_vias.end(), [](const auto& p) { return p.via.name == "via"; });
  ASSERT_NE(turn, pattern_vias.end());
  const std::vector<Signal_route_via> prior{
      {turn->via, turn->x + 40, turn->y}
  };
  Signal_route_request request;
  request.limits         = limits;
  request.search_detours = true;
  request.prior_vias     = prior;
  const auto result      = router.try_route(f.net, request);
  ASSERT_FALSE(result.candidates.empty()) << result.reason;
  EXPECT_GT(result.prior_cut_rejections, 0u);
  EXPECT_LE(result.patterns_examined, limits.patterns);
  EXPECT_LE(result.access_examined, limits.access_candidates);
  f.check_cut_union(result.candidates.front(), prior);
  auto routed = f.net;
  routed.wires.push_back({"ROUTED", f.with_prior(result.candidates.front(), prior)});
  EXPECT_TRUE(ph::check_route_connectivity(f.tech, f.frame, routed).connected);
  request.limits.patterns = 1;
  const auto limited      = router.try_route(f.net, request);
  EXPECT_TRUE(limited.candidates.empty());
  EXPECT_LE(limited.patterns_examined, 1u);
  request.limits.deadline = std::chrono::steady_clock::now();
  const auto expired      = router.try_route(f.net, request);
  EXPECT_TRUE(expired.candidates.empty());
  EXPECT_TRUE(expired.timed_out);
  EXPECT_EQ(expired.patterns_examined, 0u);
  EXPECT_EQ(occupancy.size(), 0u);
}
}  // namespace livehd::placement
