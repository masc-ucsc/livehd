// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "signal_router.hpp"

#include <limits>

#include "gtest/gtest.h"
#include "inou/lefdef/routing_connectivity.hpp"
#include "inou/lefdef/routing_geometry.hpp"
#include "pin_escape_guard.hpp"
#include "routed_rc.hpp"

namespace livehd::placement {
namespace {
using namespace physical;
Technology technology() {
  Technology t;
  t.manufacturing_grid = .001;
  t.routing_layers     = {"M1", "M2"};
  for (const auto& name : t.routing_layers) {
    auto& layer         = t.layers[name];
    layer.width         = .018;
    layer.direction     = name == "M1" ? "VERTICAL" : "HORIZONTAL";
    layer.rules.loaded  = true;
    layer.rules.area    = .000666;
    layer.rules.spacing = {
        {.018, {}, {}}
    };
  }
  t.layers["M2"].rules.min_size = {
      {.037, .018}
  };
  t.layers["M2"].rules.properties["LEF58_RIGHTWAYONGRIDONLY"] = {"RIGHTWAYONGRIDONLY ;"};
  t.cut_layers["V1"].width                                    = .018;
  t.cut_layers["V1"].spacing                                  = {.018};
  t.vias["VIA12"].shapes                                      = {
      {"M1", {-.009, -.011, .009, .011}},
      {"M2", {-.014, -.009, .014, .009}},
      {"V1", {-.009, -.009, .009, .009}}
  };
  auto& cell     = t.macros["cell"];
  cell.klass     = "CORE";
  cell.width     = .162;
  cell.height    = .270;
  cell.pins["A"] = {"INPUT", "SIGNAL", {{"M1", {.018, .027, .036, .243}}}};
  return t;
}
Floorplan frame() {
  Floorplan f;
  f.die    = {0, 0, 3000, 3000};
  f.tracks = {
      {"Y", "M2", 1045, 6, 36}
  };
  Ann_place place(1000, 1000, 162, 270);
  place.status = Status::PLACED;
  f.components.push_back({"u0", "cell", place});
  return f;
}

struct Fixture {
  Technology          tech = technology();
  Floorplan           fp   = frame();
  Net                 net;
  Signal_route_limits limits{1000, 1000, std::chrono::steady_clock::now() + std::chrono::seconds(5)};
  Fixture(size_t count = 2, int64_t step = 500) {
    fp.die.xh = 2000 + count * step;
    net.name  = "signal";
    net.use   = "SIGNAL";
    fp.components.clear();
    for (size_t i = 0; i < count; ++i) {
      Ann_place p(1000 + i * step, 1000, 162, 270);
      p.status  = Status::PLACED;
      auto name = "u" + std::to_string(i);
      fp.components.push_back({name, "cell", p});
      net.connections.push_back({name, "A"});
    }
    fp.nets.push_back(net);
  }
  Signal_route_profile profile() const { return signal_route_profile(tech_profile("asap7"), tech); }
};
}  // namespace
TEST(SignalRouter, ProfileAndNetRequestDoNotDependOnAsapLayerNames) {
  Fixture f;
  for (int n = 3; n <= 4; ++n) {
    const auto layer = "M" + std::to_string(n), cut = "V" + std::to_string(n - 1);
    f.tech.routing_layers.push_back(layer);
    f.tech.layers[layer]                             = f.tech.layers.at("M2");
    f.tech.layers[layer].direction                   = n % 2 ? "VERTICAL" : "HORIZONTAL";
    f.tech.cut_layers[cut]                           = f.tech.cut_layers.at("V1");
    f.tech.vias["bridge" + std::to_string(n)].shapes = {
        {"M" + std::to_string(n - 1), {-.009, -.009, .009, .009}},
        {                      layer, {-.009, -.009, .009, .009}},
        {                        cut, {-.009, -.009, .009, .009}}
    };
  }
  f.fp.tracks.push_back({"Y", "M4", 1045, 100, 36});
  f.fp.tracks.push_back({"X", "M3", 1000, 2000, 1});
  const std::map<std::string, std::string> names{
      {"M1",      "local"},
      {"M2", "horizontal"},
      {"M3",   "vertical"},
      {"M4",      "upper"},
      {"V1",    "contact"},
      {"V2",     "cut_hv"},
      {"V3",     "cut_vh"}
  };
  const auto rename = [&](auto& layers) {
    auto old = std::move(layers);
    layers.clear();
    for (auto& [name, layer] : old) {
      layers.emplace(names.at(name), std::move(layer));
    }
  };
  rename(f.tech.layers);
  rename(f.tech.cut_layers);
  for (auto& name : f.tech.routing_layers) {
    name = names.at(name);
  }
  for (auto& [name, via] : f.tech.vias) {
    for (auto& shape : via.shapes) {
      shape.layer = names.at(shape.layer);
    }
  }
  for (auto& [name, pin] : f.tech.macros.at("cell").pins) {
    for (auto& shape : pin.shapes) {
      shape.layer = names.at(shape.layer);
    }
  }
  for (auto& track : f.fp.tracks) {
    track.layer = names.at(track.layer);
  }
  const Signal_route_profile profile{
      {     "local", "horizontal", "contact"},
      "vertical",
      "cut_hv",
      {"horizontal",   "vertical",   "upper"}
  };
  auto                 occupancy = routing_occupancy(f.tech, f.fp, 64);
  Signal_router        router(f.tech, f.fp, occupancy, profile);
  const auto           before = occupancy.size();
  Signal_route_request request{
      f.limits,
      Shape{"horizontal", {1000, 1036, 1550, 1054}},
      true
  };
  auto result = router.try_route(f.net, request);
  ASSERT_EQ(result.candidates.size(), 2) << result.reason;
  EXPECT_LE(result.patterns_examined, request.limits.patterns);
  EXPECT_LE(result.access_examined, request.limits.access_candidates);
  EXPECT_TRUE(result.candidates[0].layer_wire_length_um.contains("horizontal"));
  EXPECT_TRUE(result.candidates[1].layer_wire_length_um.contains("upper"));
  for (const auto& candidate : result.candidates) {
    auto net = f.net;
    net.wires.push_back({"FIXED", candidate.paths});
    EXPECT_TRUE(check_route_connectivity(f.tech, f.fp, net).connected);
    for (const auto& point : candidate.endpoints) {
      EXPECT_EQ(point.pin_layer, "local");
    }
  }
  auto restricted = profile;
  restricted.signal_layers.pop_back();
  Signal_router lower_only(f.tech, f.fp, occupancy, restricted);
  EXPECT_EQ(lower_only.try_route(f.net, request).candidates.size(), 1);
  auto excluded_request            = request;
  excluded_request.corridor->layer = "upper";
  EXPECT_TRUE(lower_only.try_route(f.net, excluded_request).candidates.empty());
  {
    // Screening the lower trunk must not discard a clean upper-layer witness.
    // Use renamed layers to keep this policy independent of the technology.
    auto crowded = occupancy;
    crowded.insert({
        {"horizontal", {1027, 1072, 1527, 1090}},
        "neighbor"
    });
    auto screened                  = request;
    screened.max_parallel_exposure = 10;
    Signal_router alternatives(f.tech, f.fp, crowded, profile);
    auto          clean = alternatives.try_route(f.net, screened);
    ASSERT_EQ(clean.candidates.size(), 1) << clean.reason;
    EXPECT_TRUE(clean.candidates.front().layer_wire_length_um.contains("upper"));
    EXPECT_LE(clean.candidates.front().parallel_exposure, screened.max_parallel_exposure);
    EXPECT_TRUE(alternatives.probe_route(f.net, screened).routable);
    auto lower_probe           = screened;
    lower_probe.compare_layers = false;
    EXPECT_FALSE(alternatives.probe_route(f.net, lower_probe).routable);
    Signal_router crowded_lower(f.tech, f.fp, crowded, restricted);
    EXPECT_FALSE(crowded_lower.probe_route(f.net, screened).routable);
    screened.try_hard = true;
    EXPECT_EQ(alternatives.try_route(f.net, screened).candidates.size(), 2);
  }
  EXPECT_EQ(occupancy.size(), before);
  EXPECT_TRUE(f.net.wires.empty());
  request.compare_layers = false;
  request.corridor.reset();
  EXPECT_EQ(router.try_route(f.net, request).candidates.size(), 1);
  request.compare_layers = true;
  EXPECT_TRUE(router.try_route(f.net, request).candidates.empty());
  request.corridor = Shape{
      "horizontal",
      {1000, 1036, 1550, 1054}
  };
  request.limits.deadline = std::chrono::steady_clock::now();
  EXPECT_TRUE(router.try_route(f.net, request).timed_out);
  EXPECT_TRUE(router.try_route(f.net, request).candidates.empty());
  // Arbitrary-row routing also uses profile-defined access and branch layers.
  f.fp.components[1].place.y += 540;
  f.fp.tracks.push_back({"Y", "horizontal", 1585, 6, 36});
  auto          moved_occupancy = routing_occupancy(f.tech, f.fp, 64);
  Signal_router moved(f.tech, f.fp, moved_occupancy, profile);
  const auto    pattern = moved.try_route(f.net, {f.limits, {}, false});
  ASSERT_EQ(pattern.candidates.size(), 1) << pattern.reason;
  EXPECT_TRUE(pattern.candidates.front().layer_wire_length_um.contains("vertical"));
}

TEST(SignalRouter, ProbeReturnsWitnessWithoutMutatingOrSearchingDetours) {
  Fixture              f;
  auto                 occupancy = routing_occupancy(f.tech, f.fp, 64);
  Signal_router        router(f.tech, f.fp, occupancy, f.profile());
  Signal_route_request request{f.limits, {}, false, true};
  const auto           before = occupancy.size();
  auto                 probe  = router.probe_route(f.net, request);
  ASSERT_TRUE(probe.routable) << probe.reason;
  ASSERT_TRUE(probe.corridor);
  EXPECT_EQ(probe.corridor->layer, "M2");
  const auto trial = router.try_route(f.net, request);
  ASSERT_EQ(trial.candidates.size(), 1);
  EXPECT_EQ(probe.patterns_examined, trial.patterns_examined);
  EXPECT_TRUE(f.net.wires.empty());
  EXPECT_EQ(occupancy.size(), before);
  request.limits.patterns = 0;
  probe                   = router.probe_route(f.net, request);
  EXPECT_FALSE(probe.routable);
  EXPECT_TRUE(probe.budget_exhausted);
  request.limits          = f.limits;
  request.limits.deadline = std::chrono::steady_clock::now();
  EXPECT_TRUE(router.probe_route(f.net, request).timed_out);
  request.limits = f.limits;
  occupancy.insert({
      {"M2", {1200, 1000, 1250, 1300}},
      "foreign"
  });
  EXPECT_FALSE(router.probe_route(f.net, request).routable);
  EXPECT_TRUE(f.net.wires.empty());
}

TEST(SignalRouter, LongParallelExposureCanRejectLegalRouteWithoutMutatingOccupancy) {
  Fixture              f(2, 5000);
  auto                 occupancy = routing_occupancy(f.tech, f.fp, 64);
  Signal_router        router(f.tech, f.fp, occupancy, f.profile());
  Signal_route_request request{f.limits, {}, false, true};
  auto                 clear = router.try_route(f.net, request);
  ASSERT_EQ(clear.candidates.size(), 1);
  ASSERT_TRUE(clear.candidates.front().straight_corridor);
  request.corridor = clear.candidates.front().straight_corridor;
  const auto trunk = request.corridor->rect;
  // One track away: legal 18-DBU edge spacing, but a long parallel run.
  occupancy.insert({
      {"M2", {trunk.xl, trunk.yl + 36, trunk.xh, trunk.yh + 36}},
      "neighbor"
  });
  const auto before   = occupancy.size();
  auto       rejected = router.try_route(f.net, request);
  EXPECT_TRUE(rejected.candidates.empty());
  EXPECT_NE(rejected.reason.find("parallel"), std::string::npos);
  EXPECT_FALSE(router.probe_route(f.net, request).routable);
  request.try_hard = true;
  auto forced      = router.try_route(f.net, request);
  ASSERT_EQ(forced.candidates.size(), 1) << forced.reason;
  EXPECT_GT(forced.candidates.front().parallel_exposure, request.max_parallel_exposure);
  EXPECT_EQ(occupancy.size(), before);
  EXPECT_TRUE(f.net.wires.empty());
  // A larger configured allowance also accepts the same legal geometry.
  request.try_hard              = false;
  request.max_parallel_exposure = forced.candidates.front().parallel_exposure + 1;
  EXPECT_EQ(router.try_route(f.net, request).candidates.size(), 1);
  // Try-hard cannot bypass a physical short.
  request.try_hard = true;
  occupancy.insert({
      {"M2", trunk},
      "short"
  });
  EXPECT_TRUE(router.try_route(f.net, request).candidates.empty());
}

TEST(SignalRouter, ParallelExposureIgnoresOwnMetalAndDistantOrShortNeighbors) {
  Fixture              f(2, 5000);
  auto                 occupancy = routing_occupancy(f.tech, f.fp, 64);
  Signal_router        router(f.tech, f.fp, occupancy, f.profile());
  Signal_route_request request{f.limits, {}, false, true};
  auto                 clear = router.try_route(f.net, request);
  ASSERT_EQ(clear.candidates.size(), 1);
  ASSERT_TRUE(clear.candidates.front().straight_corridor);
  request.corridor = clear.candidates.front().straight_corridor;
  const auto r     = request.corridor->rect;
  occupancy.insert({
      {"M2", {r.xl, r.yl + 36, r.xh, r.yh + 36}},
      f.net.name
  });
  occupancy.insert({
      {"M2", {r.xl, r.yl - 200, r.xh, r.yh - 200}},
      "distant"
  });
  occupancy.insert({
      {"M2", {r.xl + 100, r.yl - 36, r.xl + 200, r.yh - 36}},
      "short"
  });
  auto accepted = router.try_route(f.net, request);
  ASSERT_EQ(accepted.candidates.size(), 1) << accepted.reason;
  EXPECT_GT(accepted.candidates.front().parallel_exposure, 0);
  EXPECT_LT(accepted.candidates.front().parallel_exposure, request.max_parallel_exposure);
}

TEST(SignalRouter, TryHardCannotBlockUnassignedClockOrSupplyAccess) {
  for (const auto* use : {"CLOCK", "POWER", "GROUND"}) {
    Fixture              f;
    auto                 occupied = routing_occupancy(f.tech, f.fp, 64);
    Signal_route_request request{f.limits, {}, false, true};
    Signal_router        clear_router(f.tech, f.fp, occupied, f.profile());
    auto                 clear = clear_router.try_route(f.net, request);
    ASSERT_EQ(clear.candidates.size(), 1);
    request.corridor = clear.candidates.front().straight_corridor;
    ASSERT_TRUE(request.corridor);
    // This unassigned pin has only the trunk's access track. The signal trunk
    // clears the M1 pin itself, but removes its only legal M2 via landing.
    f.tech.macros["cell"].pins["control"] = {"INPUT", use, {{"M1", {.072, .027, .090, .063}}}};
    occupied                              = routing_occupancy(f.tech, f.fp, 64);
    Signal_router router(f.tech, f.fp, occupied, f.profile());
    for (bool hard : {false, true}) {
      request.try_hard = hard;
      auto blocked     = router.try_route(f.net, request);
      EXPECT_TRUE(blocked.candidates.empty()) << use;
      EXPECT_NE(blocked.reason.find("pin access"), std::string::npos) << blocked.reason;
      EXPECT_FALSE(router.probe_route(f.net, request).routable);
    }
    EXPECT_TRUE(f.net.wires.empty());
  }
}

TEST(SignalRouter, SpanSearchContinuesAfterAProtectedClockEscapeRejectsFirstTrack) {
  Fixture f;
  f.tech.macros["cell"].pins["CLK"] = {"INPUT", "CLOCK", {{"M1", {.072, .027, .090, .063}}}};
  auto          occupancy           = routing_occupancy(f.tech, f.fp, 64);
  Signal_router router(f.tech, f.fp, occupancy, f.profile());
  const auto    result = router.try_route(f.net, {f.limits, {}, false, true});
  ASSERT_EQ(result.candidates.size(), 1) << result.reason;
  ASSERT_TRUE(result.candidates.front().straight_corridor);
  EXPECT_GT(result.candidates.front().straight_corridor->rect.yl, 1036);
  EXPECT_GT(result.patterns_examined, 1);
}

TEST(SignalRouter, StraightSatWitnessChecksMetalAndConnectionOrder) {
  Fixture f;
  f.tech.layers["M1"].direction          = "HORIZONTAL";
  f.tech.macros["cell"].pins["A"].shapes = {
      {"M1", {.020, .020, .080, .080}}
  };
  f.fp.tracks.push_back({"Y", "M1", 1050, 1, 36});
  auto          occupancy = routing_occupancy(f.tech, f.fp, 64);
  Signal_router router(f.tech, f.fp, occupancy, f.profile());
  const Shape   witness{
      "M1",
        {1041, 1041, 1559, 1059}
  };
  const auto before = occupancy.size();
  auto       result = router.propose_straight(f.net, witness, f.limits);
  ASSERT_TRUE(result.geometry) << result.reason;
  EXPECT_EQ(result.geometry->vias, 0);
  EXPECT_EQ(result.geometry->paths.size(), 1);
  EXPECT_EQ(result.geometry->endpoints[0].x, 1050);
  EXPECT_EQ(result.geometry->endpoints[1].x, 1550);
  EXPECT_NEAR(result.geometry->wire_length_um, .5, 1e-12);
  std::reverse(f.net.connections.begin(), f.net.connections.end());
  result = router.propose_straight(f.net, witness, f.limits);
  ASSERT_TRUE(result.geometry) << result.reason;
  EXPECT_EQ(result.geometry->endpoints[0].x, 1550);
  EXPECT_EQ(occupancy.size(), before);
  EXPECT_FALSE(router
                   .propose_straight(f.net,
                                     {
                                         "M1",
                                         {1041, 1141, 1559, 1159}
  },
                                     f.limits)
                   .geometry);
  occupancy.insert_many({
      {{"M1", {1200, 1040, 1220, 1060}}, "foreign"}
  });
  EXPECT_FALSE(router.propose_straight(f.net, witness, f.limits).geometry);
}

TEST(SignalRouter, AlignedM2WitnessUsesViasWithoutEscapingItsTrackOrExtent) {
  Fixture       f;
  auto          occupancy = routing_occupancy(f.tech, f.fp, 64);
  Signal_router router(f.tech, f.fp, occupancy, f.profile());
  const Shape   corridor{
      "M2",
        {1000, 1036, 1550, 1054}
  };
  auto result = router.propose_aligned(f.net, corridor, f.limits);
  ASSERT_TRUE(result.geometry) << result.reason;
  EXPECT_EQ(result.geometry->vias, 2);
  auto net = f.net;
  net.wires.push_back({"FIXED", result.geometry->paths});
  EXPECT_TRUE(check_route_connectivity(f.tech, f.fp, net).connected);
  for (const auto& point : result.geometry->endpoints) {
    EXPECT_EQ(point.y, 1045);
    EXPECT_FALSE(point.via.empty());
  }
  for (const auto& shape : route_geometry(f.tech, f.fp, net)) {
    if (shape.layer == "M2") {
      EXPECT_GE(shape.rect.xl, corridor.rect.xl);
      EXPECT_LE(shape.rect.xh, corridor.rect.xh);
      EXPECT_GE(shape.rect.yl, corridor.rect.yl);
      EXPECT_LE(shape.rect.yh, corridor.rect.yh);
    }
  }
  EXPECT_FALSE(router
                   .propose_aligned(f.net,
                                    {
                                        "M2",
                                        {1050, 1036, 1550, 1054}
  },
                                    f.limits)
                   .geometry);
  occupancy.insert({
      {"M2", {1200, 1036, 1250, 1054}},
      "foreign"
  });
  EXPECT_FALSE(router.propose_aligned(f.net, corridor, f.limits).geometry);
  // Other M2 tracks are usable, but the retained corridor cannot be abandoned.
  EXPECT_TRUE(router.propose_span(f.net, f.limits).geometry);
}
TEST(SignalRouter, AlignedLiftPreservesCentersAndUsesOnlyOneUpperTrunk) {
  Fixture f;
  for (int n = 3; n <= 4; ++n) {
    const auto layer = "M" + std::to_string(n), lower = "M" + std::to_string(n - 1), cut = "V" + std::to_string(n - 1);
    f.tech.routing_layers.push_back(layer);
    f.tech.layers[layer]                                                  = f.tech.layers.at("M2");
    f.tech.layers[layer].direction                                        = n % 2 ? "VERTICAL" : "HORIZONTAL";
    f.tech.cut_layers[cut]                                                = f.tech.cut_layers.at("V1");
    f.tech.vias["VIA" + std::to_string(n - 1) + std::to_string(n)].shapes = {
        {lower, {-.009, -.009, .009, .009}},
        {layer, {-.009, -.009, .009, .009}},
        {  cut, {-.009, -.009, .009, .009}}
    };
  }
  f.fp.tracks.push_back({"Y", "M4", 1045, 6, 36});
  // The upper vertical landings must also sit on legal tracks.
  f.fp.tracks.push_back({"X", "M3", 1000, 2000, 1});
  auto          occupancy = routing_occupancy(f.tech, f.fp, 64);
  Signal_router router(f.tech, f.fp, occupancy, f.profile());
  auto          base = router.propose_aligned(f.net,
                                              {
                                         "M2",
                                         {1000, 1036, 1550, 1054}
  },
                                     f.limits);
  ASSERT_TRUE(base.geometry) << base.reason;
  const auto before = occupancy.size();
  auto       lifted = router.propose_aligned_layer(f.net, *base.geometry, "M4", f.limits);
  ASSERT_TRUE(lifted.geometry) << lifted.reason;
  EXPECT_EQ(lifted.geometry->vias, 6);
  EXPECT_EQ(occupancy.size(), before);
  for (size_t i = 0; i < 2; ++i) {
    EXPECT_EQ(lifted.geometry->endpoints[i].x, base.geometry->endpoints[i].x);
    EXPECT_EQ(lifted.geometry->endpoints[i].y, base.geometry->endpoints[i].y);
  }
  auto net = f.net;
  net.wires.push_back({"FIXED", lifted.geometry->paths});
  EXPECT_TRUE(check_route_connectivity(f.tech, f.fp, net).connected);
  size_t trunks = 0;
  for (const auto& shape : route_geometry(f.tech, f.fp, net)) {
    if (shape.rect.xh - shape.rect.xl > 100) {
      EXPECT_EQ(shape.layer, "M4");
      ++trunks;
    }
  }
  EXPECT_EQ(trunks, 1);
  EXPECT_FALSE(router.propose_aligned_layer(f.net, *base.geometry, "M3", f.limits).geometry);
  auto no_budget     = f.limits;
  no_budget.patterns = 0;
  EXPECT_FALSE(router.propose_aligned_layer(f.net, *base.geometry, "M4", no_budget).geometry);
  occupancy.insert({
      {"M4", {1200, 1036, 1250, 1054}},
      "foreign"
  });
  EXPECT_FALSE(router.propose_aligned_layer(f.net, *base.geometry, "M4", f.limits).geometry);
}

TEST(SignalRouter, M2EndpointViasFailClosedForUnsupportedPinAccessRules) {
  Fixture f(2, 54);
  auto    occupancy = routing_occupancy(f.tech, f.fp, 64);
  ASSERT_TRUE(Signal_router(f.tech, f.fp, occupancy, f.profile()).propose_span(f.net, f.limits).geometry);
  auto& cut                            = f.tech.cut_layers.at("V1");
  cut.properties["LEF58_CUTCLASS"]     = {"CUTCLASS square WIDTH 0.018 ;"};
  cut.properties["LEF58_SPACINGTABLE"] = {"SPACINGTABLE DEFAULT 0.072 CUTCLASS square square - - ;"};
  const auto rejected                  = Signal_router(f.tech, f.fp, occupancy, f.profile()).propose_span(f.net, f.limits);
  EXPECT_FALSE(rejected.geometry);
  EXPECT_EQ(rejected.reason, "pin has no legal access");
  EXPECT_EQ(rejected.access_examined, 0);
}
TEST(SignalRouter, PatternsRespectLayerDirectionWithoutRightWayProperties) {
  Fixture f;
  f.tech.layers["M2"].rules.properties.clear();
  f.tech.layers["M3"] = f.tech.layers.at("M2");
  auto occupancy      = routing_occupancy(f.tech, f.fp, 64);
  auto trial          = Signal_router(f.tech, f.fp, occupancy, f.profile()).propose_pattern(f.net, f.limits);
  EXPECT_FALSE(trial.geometry);
  EXPECT_EQ(trial.reason, "M2/M3 pattern requires horizontal/vertical layers");
  f.tech.layers["M2"].direction = "VERTICAL";
  trial                         = Signal_router(f.tech, f.fp, occupancy, f.profile()).propose_span(f.net, f.limits);
  EXPECT_FALSE(trial.geometry);
  EXPECT_EQ(trial.reason, "M2 span requires a horizontal layer");
}

TEST(SignalRouter, WidthRuleCommitRejectsConflictsBeforeMutation) {
  Floorplan fp;
  fp.nondefault_rules["z"] = {
      {"M1", 20}
  };
  Signal_route_geometry proposal;
  proposal.nondefault_rules["a"] = {
      {"M1", 40}
  };
  proposal.nondefault_rules["z"] = {
      {"M1", 80}
  };
  const auto before = fp.nondefault_rules;
  EXPECT_THROW(apply_signal_route_rules(fp, proposal), std::invalid_argument);
  EXPECT_EQ(fp.nondefault_rules, before);
  proposal.nondefault_rules["z"] = before.at("z");
  EXPECT_NO_THROW(apply_signal_route_rules(fp, proposal));
  EXPECT_EQ(fp.nondefault_rules.at("a").at("M1"), 40);
}
TEST(SignalRouter, ConnectsAllPinsWithoutMutatingPlacementOrOccupancy) {
  for (size_t count : {2, 3, 8}) {
    Fixture    f(count);
    auto       occupancy = routing_occupancy(f.tech, f.fp, 64);
    auto       before    = occupancy.size();
    const auto result    = Signal_router(f.tech, f.fp, occupancy, f.profile()).propose_span(f.net, f.limits);
    ASSERT_TRUE(result.geometry) << result.reason;
    EXPECT_EQ(result.geometry->endpoints.size(), count);
    EXPECT_EQ(result.geometry->vias, count);
    EXPECT_EQ(occupancy.size(), before);
    EXPECT_TRUE(f.fp.nets.front().wires.empty());
    Net routed = f.net;
    routed.wires.push_back({"FIXED", result.geometry->paths});
    const auto shapes = route_geometry(f.tech, f.fp, routed);
    ASSERT_EQ(shapes.size(), 3 * count + 1);
    const auto& span = shapes.back();
    EXPECT_EQ(span.layer, "M2");
    // Each via's M2 landing overlaps the same continuous span.
    for (const auto& p : result.geometry->endpoints) {
      EXPECT_LT(span.rect.xl, p.x);
      EXPECT_GT(span.rect.xh, p.x);
      EXPECT_LT(span.rect.yl, p.y);
      EXPECT_GT(span.rect.yh, p.y);
    }
    EXPECT_NEAR(result.geometry->wire_length_um, ((count - 1) * 500 + 38) / 1000., 1e-9);
  }
}
TEST(SignalRouter, AvoidsObstaclesBetweenEndpointsAndRejectsBlockedSpan) {
  Fixture f;
  auto    occupancy = routing_occupancy(f.tech, f.fp, 64);
  occupancy.insert({
      {"M2", {1200, 1036, 1300, 1054}},
      "other"
  });
  auto result = Signal_router(f.tech, f.fp, occupancy, f.profile()).propose_span(f.net, f.limits);
  ASSERT_TRUE(result.geometry) << result.reason;
  EXPECT_EQ(result.geometry->endpoints[0].y, 1081);
  occupancy.insert({
      {"M2", {1200, 1000, 1300, 1300}},
      "other"
  });
  result = Signal_router(f.tech, f.fp, occupancy, f.profile()).propose_span(f.net, f.limits);
  EXPECT_FALSE(result.geometry);
  EXPECT_EQ(result.reason, "M2 span blocked");
}
TEST(SignalRouter, BoundsSearchAndRejectsIneligibleEndpoints) {
  Fixture       f;
  auto          occupancy = routing_occupancy(f.tech, f.fp, 64);
  Signal_router router(f.tech, f.fp, occupancy, f.profile());
  auto          limits = f.limits;
  limits.patterns      = 0;
  EXPECT_EQ(router.propose_span(f.net, limits).reason, "pattern budget");
  limits                   = f.limits;
  limits.access_candidates = 0;
  EXPECT_EQ(router.propose_span(f.net, limits).reason, "pin-access budget");
  limits          = f.limits;
  limits.deadline = std::chrono::steady_clock::now();
  EXPECT_TRUE(router.propose_span(f.net, limits).timed_out);
  f.net.use = "CLOCK";
  EXPECT_FALSE(router.propose_span(f.net, f.limits).geometry);
  f.net.use                     = "SIGNAL";
  f.net.connections[1].instance = "PIN";
  EXPECT_FALSE(router.propose_span(f.net, f.limits).geometry);
  f.net.connections[1] = f.net.connections[0];
  EXPECT_FALSE(router.propose_span(f.net, f.limits).geometry);
}
TEST(SignalRouter, NewSameNetCutsMustRespectSpacing) {
  Fixture f(2, 20);
  auto    occupancy = routing_occupancy(f.tech, f.fp, 64);
  auto    result    = Signal_router(f.tech, f.fp, occupancy, f.profile()).propose_span(f.net, f.limits);
  EXPECT_FALSE(result.geometry);
  EXPECT_EQ(result.reason, "M2 span blocked");
}
TEST(SignalRouter, RoutesDifferentRowsThroughM3AndAvoidsBlockedTrunk) {
  Fixture f;
  f.tech.routing_layers.push_back("M3");
  f.tech.layers["M3"]           = f.tech.layers["M2"];
  f.tech.layers["M3"].direction = "VERTICAL";
  f.tech.cut_layers["V2"]       = f.tech.cut_layers["V1"];
  f.tech.vias["VIA23"].shapes   = {
      {"M2", {-.014, -.009, .014, .009}},
      {"M3", {-.009, -.014, .009, .014}},
      {"V2", {-.009, -.009, .009, .009}}
  };
  f.fp.components[1].place.y += 540;
  f.fp.tracks.push_back({"Y", "M2", 1585, 6, 36});
  f.fp.tracks.push_back({"X", "M3", 9, 80, 36});
  auto          occupancy = routing_occupancy(f.tech, f.fp, 64);
  Signal_router router(f.tech, f.fp, occupancy, f.profile());
  EXPECT_FALSE(router.propose_span(f.net, f.limits).geometry);
  auto result = router.propose_pattern(f.net, f.limits);
  ASSERT_TRUE(result.geometry) << result.reason;
  EXPECT_EQ(result.geometry->vias, 4);
  const auto first_trunk = std::get<Route_point>(result.geometry->paths.back().steps.front()).x;
  occupancy.insert({
      {"M3", {double(first_trunk - 9), 1300, double(first_trunk + 9), 1400}},
      "other"
  });
  result = Signal_router(f.tech, f.fp, occupancy, f.profile()).propose_pattern(f.net, f.limits);
  ASSERT_TRUE(result.geometry) << result.reason;
  EXPECT_NE(std::get<Route_point>(result.geometry->paths.back().steps.front()).x, first_trunk);
  occupancy.insert({
      {"M3", {1000, 1300, 1600, 1400}},
      "other"
  });
  EXPECT_FALSE(Signal_router(f.tech, f.fp, occupancy, f.profile()).propose_pattern(f.net, f.limits).geometry);
  auto limits     = f.limits;
  limits.patterns = 0;
  EXPECT_FALSE(router.propose_pattern(f.net, limits).geometry);
}
TEST(SignalRouter, MultiRowTrunkConnectsEveryPinAndSharesSameTrackBranches) {
  Fixture f(4);
  f.tech.routing_layers.push_back("M3");
  f.tech.layers["M3"]           = f.tech.layers["M2"];
  f.tech.layers["M3"].direction = "VERTICAL";
  f.tech.cut_layers["V2"]       = f.tech.cut_layers["V1"];
  f.tech.vias["VIA23"].shapes   = {
      {"M2", {-.014, -.009, .014, .009}},
      {"M3", {-.009, -.014, .009, .014}},
      {"V2", {-.009, -.009, .009, .009}}
  };
  // Two pins in the first row, one each in two further rows. Exactly one
  // legal access track per row makes the shared branch topology explicit.
  f.fp.components[2].place.y += 540;
  f.fp.components[3].place.y += 1080;
  f.fp.tracks                 = {
      {"Y", "M2", 1045,   3, 540},
      {"X", "M3",    9, 100,  36}
  };
  auto       occupancy = routing_occupancy(f.tech, f.fp, 64);
  const auto before    = occupancy.size();
  auto       result    = Signal_router(f.tech, f.fp, occupancy, f.profile()).propose_pattern(f.net, f.limits);
  ASSERT_TRUE(result.geometry) << result.reason;
  EXPECT_EQ(result.geometry->endpoints.size(), 4);
  EXPECT_DOUBLE_EQ(result.geometry->wire_length_um,
                   result.geometry->layer_wire_length_um.at("M2") + result.geometry->layer_wire_length_um.at("M3"));
  EXPECT_EQ(result.geometry->vias, 7);  // Four V1 cuts and three shared V2 cuts.
  Net routed = f.net;
  routed.wires.push_back({"FIXED", result.geometry->paths});
  EXPECT_TRUE(check_route_connectivity(f.tech, f.fp, routed).connected);
  EXPECT_EQ(occupancy.size(), before);
  EXPECT_TRUE(f.fp.nets.front().wires.empty());
  // The third row is essential: a router that only joins the first two
  // endpoints would incorrectly report success despite this obstruction.
  occupancy.insert({
      {"M3", {0, 1800, 4000, 1900}},
      "other"
  });
  EXPECT_FALSE(Signal_router(f.tech, f.fp, occupancy, f.profile()).propose_pattern(f.net, f.limits).geometry);
  EXPECT_EQ(occupancy.size(), before + 1);
  auto limits     = f.limits;
  limits.deadline = std::chrono::steady_clock::now();
  EXPECT_TRUE(Signal_router(f.tech, f.fp, occupancy, f.profile()).propose_pattern(f.net, limits).timed_out);
}
TEST(SignalRouter, MazeDetoursAroundBlockedSameRowWithoutPublishingFailures) {
  Fixture f;
  f.tech.routing_layers.push_back("M3");
  f.tech.layers["M3"]           = f.tech.layers["M2"];
  f.tech.layers["M3"].direction = "VERTICAL";
  f.tech.cut_layers["V2"]       = f.tech.cut_layers["V1"];
  f.tech.vias["VIA23"].shapes   = {
      {"M2", {-.014, -.009, .014, .009}},
      {"M3", {-.009, -.014, .009, .014}},
      {"V2", {-.009, -.009, .009, .009}}
  };
  f.fp.tracks = {
      {"Y", "M2", 901, 25, 36},
      {"X", "M3",   9, 80, 36}
  };
  auto occupancy = routing_occupancy(f.tech, f.fp, 64);
  occupancy.insert({
      {"M2", {1200, 900, 1300, 1300}},
      "blockage"
  });
  const auto before = occupancy.size();
  auto       limits = f.limits;
  limits.patterns   = 10000;
  Signal_router router(f.tech, f.fp, occupancy, f.profile());
  EXPECT_FALSE(router.propose_span(f.net, limits).geometry);
  EXPECT_FALSE(router.propose_pattern(f.net, limits).geometry);
  const auto result = router.propose(f.net, limits);
  ASSERT_TRUE(result.geometry) << result.reason << " nodes=" << result.patterns_examined;
  EXPECT_GT(result.geometry->vias, 4);
  EXPECT_LE(result.access_examined, limits.access_candidates);
  EXPECT_LE(result.patterns_examined, limits.patterns);
  auto routed = f.net;
  routed.wires.push_back({"FIXED", result.geometry->paths});
  EXPECT_TRUE(check_route_connectivity(f.tech, f.fp, routed).connected);
  EXPECT_EQ(occupancy.size(), before);
  EXPECT_TRUE(f.fp.nets.front().wires.empty());
  bool detoured = false;
  for (const auto& path : result.geometry->paths) {
    if (path.layer == "M2" && path.steps.size() == 2 && std::holds_alternative<Route_point>(path.steps.back())) {
      detoured |= std::get<Route_point>(path.steps.front()).y > 1300;
    }
  }
  EXPECT_TRUE(detoured);
  limits.patterns    = 1;
  const auto limited = router.propose_maze(f.net, limits);
  EXPECT_FALSE(limited.geometry);
  EXPECT_LE(limited.patterns_examined, limits.patterns);
  limits.deadline = std::chrono::steady_clock::now();
  EXPECT_TRUE(router.propose_maze(f.net, limits).timed_out);
  occupancy.insert({
      {"M2", {1200, 0, 1300, 3000}},
      "wall"
  });
  occupancy.insert({
      {"M3", {1200, 0, 1300, 3000}},
      "wall"
  });
  limits.patterns = 10000;
  limits.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  EXPECT_FALSE(router.propose_maze(f.net, limits).geometry);
  EXPECT_EQ(occupancy.size(), before + 2);
}
TEST(SignalRouter, MazeUsesUpperLayersAcrossLowerLayerBarrier) {
  Fixture f;
  f.fp.components[1].place.y += 540;
  f.fp.tracks                 = {
      {"Y", "M2", 901, 40, 36}
  };
  for (int number = 3; number <= 7; ++number) {
    const auto layer = "M" + std::to_string(number);
    const auto lower = "M" + std::to_string(number - 1);
    const auto cut   = "V" + std::to_string(number - 1);
    const auto via   = "VIA" + std::to_string(number - 1) + std::to_string(number);
    f.tech.routing_layers.push_back(layer);
    f.tech.layers[layer]           = f.tech.layers["M2"];
    const bool horizontal          = number % 2 == 0;
    f.tech.layers[layer].direction = horizontal ? "HORIZONTAL" : "VERTICAL";
    f.tech.cut_layers[cut]         = f.tech.cut_layers["V1"];
    f.tech.vias[via].shapes        = {
        {lower, horizontal ? Rect{-.009, -.014, .009, .014} : Rect{-.014, -.009, .014, .009}},
        {layer, horizontal ? Rect{-.014, -.009, .014, .009} : Rect{-.009, -.014, .009, .014}},
        {  cut,                                                   {-.009, -.009, .009, .009}}
    };
    f.fp.tracks.push_back({horizontal ? "Y" : "X", layer, horizontal ? 919 : (number == 5 ? 27 : 9), 80, 36});
  }
  auto occupancy = routing_occupancy(f.tech, f.fp, 64);
  for (int number = 2; number <= 6; ++number) {
    occupancy.insert({
        {"M" + std::to_string(number), {0, 1300, 4000, 1400}},
        "barrier"
    });
  }
  auto limits       = f.limits;
  limits.patterns   = 20000;
  const auto result = Signal_router(f.tech, f.fp, occupancy, f.profile()).propose(f.net, limits);
  ASSERT_TRUE(result.geometry) << result.reason << " nodes=" << result.patterns_examined;
  EXPECT_GT(result.geometry->layer_wire_length_um.at("M7"), 0);
  auto routed = f.net;
  routed.wires.push_back({"FIXED", result.geometry->paths});
  EXPECT_TRUE(check_route_connectivity(f.tech, f.fp, routed).connected);
  Route_technology_rc rc_model;
  double              expected_c = 0, expected_r = 0;
  for (int number = 2; number <= 7; ++number) {
    const auto layer        = "M" + std::to_string(number);
    rc_model.layers[layer]  = {double(number), double(number + 1)};
    const auto length       = result.geometry->layer_wire_length_um.at(layer);
    expected_r             += number * length;
    expected_c             += (number + 1) * length;
  }
  for (int number = 1; number <= 6; ++number) {
    rc_model.cut_resistance_ohm["V" + std::to_string(number)] = 10;
  }
  const auto& accesses = result.geometry->endpoints;
  const auto  rc       = routed_rc(f.tech,
                            f.fp,
                            routed,
                            "u0:A",
                                   {
                                {"u0:A", "M1", accesses[0].x, accesses[0].y},
                                {"u1:A", "M1", accesses[1].x, accesses[1].y}
  },
                            rc_model);
  double      actual_c = 0, actual_r = 0;
  for (const auto& node : rc.nodes) {
    actual_c += node.capacitance_ff;
  }
  for (const auto& edge : rc.edges) {
    actual_r += edge.resistance_ohm;
  }
  EXPECT_NEAR(actual_c, expected_c, 1e-9);
  EXPECT_NEAR(actual_r, expected_r + 10 * result.geometry->vias, 1e-9);
  EXPECT_NEAR(rc.wire_length_um, result.geometry->wire_length_um, 1e-9);
  EXPECT_NO_THROW(physical::validate_rc_tree(rc));
  // Model the ASAP7 V5 EOL condition: nominal-width metal cannot enclose
  // the cut. The only remaining detour must emit wider legal paths.
  for (const auto layer : {"M5", "M6"}) {
    f.tech.layers[layer].rules.properties["LEF58_WIDTHTABLE"] = {"WIDTHTABLE .018 .090 ;"};
  }
  f.tech.cut_layers["V5"].properties["LEF58_CUTCLASS"]  = {"CUTCLASS square WIDTH .018 ;"};
  f.tech.cut_layers["V5"].properties["LEF58_ENCLOSURE"] = {"ENCLOSURE CUTCLASS square EOL .01825 .005 .001 ;"};
  const auto wide = Signal_router(f.tech, f.fp, occupancy, f.profile()).propose_maze(f.net, limits);
  ASSERT_TRUE(wide.geometry) << wide.reason;
  bool wider = false;
  for (const auto& path : wide.geometry->paths) {
    wider |= !path.taper_rule.empty();
    for (const auto& step : path.steps) {
      EXPECT_FALSE(std::holds_alternative<Route_rect>(step));
    }
  }
  EXPECT_TRUE(wider);
  EXPECT_TRUE(f.fp.nondefault_rules.empty());  // Proposal must not mutate its input.
  auto wide_frame = f.fp;
  apply_signal_route_rules(wide_frame, *wide.geometry);
  auto wide_net = f.net;
  wide_net.wires.push_back({"FIXED", wide.geometry->paths});
  EXPECT_TRUE(check_route_connectivity(f.tech, wide_frame, wide_net).connected);
  const auto& wide_access = wide.geometry->endpoints;
  EXPECT_NO_THROW(routed_rc(f.tech,
                            wide_frame,
                            wide_net,
                            "u0:A",
                            {
                                {"u0:A", "M1", wide_access[0].x, wide_access[0].y},
                                {"u1:A", "M1", wide_access[1].x, wide_access[1].y}
  },
                            rc_model));
  // Imported names may collide with generated names. Preserve their meaning
  // and allocate a distinct name rather than silently changing existing wires.
  for (const auto& [name, widths] : wide.geometry->nondefault_rules) {
    f.fp.nondefault_rules[name] = {
        {"M1", 222}
    };
  }
  const auto collision = Signal_router(f.tech, f.fp, occupancy, f.profile()).propose_maze(f.net, limits);
  ASSERT_TRUE(collision.geometry) << collision.reason;
  for (const auto& [name, widths] : collision.geometry->nondefault_rules) {
    EXPECT_FALSE(f.fp.nondefault_rules.contains(name));
  }
  // Removing the M6/M7 transition must not silently jump the layer stack.
  f.tech.vias.erase("VIA67");
  const auto rejected = Signal_router(f.tech, f.fp, occupancy, f.profile()).propose_maze(f.net, limits);
  EXPECT_FALSE(rejected.geometry);
  EXPECT_TRUE(f.fp.nets.front().wires.empty());
}
TEST(SignalRouter, MultiPinSteinerMazeDetoursAndPublishesOnlyCompleteNet) {
  Fixture f(3);
  f.tech.routing_layers.push_back("M3");
  f.tech.layers["M3"]           = f.tech.layers["M2"];
  f.tech.layers["M3"].direction = "VERTICAL";
  f.tech.cut_layers["V2"]       = f.tech.cut_layers["V1"];
  f.tech.vias["VIA23"].shapes   = {
      {"M2", {-.014, -.009, .014, .009}},
      {"M3", {-.009, -.014, .009, .014}},
      {"V2", {-.009, -.009, .009, .009}}
  };
  f.fp.components[1].place.y += 540;
  f.fp.tracks                 = {
      {"Y", "M2", 901,  50, 36},
      {"X", "M3",   9, 100, 36}
  };
  auto occupancy = routing_occupancy(f.tech, f.fp, 64);
  occupancy.insert({
      {"M2", {1200, 900, 1300, 1300}},
      "blockage"
  });
  const auto before = occupancy.size();
  auto       limits = f.limits;
  limits.patterns   = 30000;
  Signal_router router(f.tech, f.fp, occupancy, f.profile());
  EXPECT_FALSE(router.propose_pattern(f.net, limits).geometry);
  auto result = router.propose(f.net, limits);
  ASSERT_TRUE(result.geometry) << result.reason;
  ASSERT_EQ(result.geometry->endpoints.size(), 3);
  auto routed = f.net;
  routed.wires.push_back({"FIXED", result.geometry->paths});
  EXPECT_TRUE(check_route_connectivity(f.tech, f.fp, routed).connected);
  EXPECT_EQ(occupancy.size(), before);
  EXPECT_TRUE(f.fp.nets.front().wires.empty());
  EXPECT_LE(result.patterns_examined, limits.patterns);
  EXPECT_LE(result.access_examined, limits.access_candidates);
  Route_technology_rc model;
  model.layers = {
      {"M1", {1, 1}},
      {"M2", {1, 1}},
      {"M3", {1, 1}}
  };
  model.cut_resistance_ohm = {
      {"V1", 1},
      {"V2", 1}
  };
  std::vector<Route_rc_pin> pins;
  for (size_t i = 0; i < 3; ++i) {
    const auto& p = result.geometry->endpoints[i];
    pins.push_back({"u" + std::to_string(i) + ":A", "M1", p.x, p.y});
  }
  const auto rc = routed_rc(f.tech, f.fp, routed, "u0:A", pins, model);
  EXPECT_NO_THROW(physical::validate_rc_tree(rc));
  EXPECT_NEAR(rc.wire_length_um, result.geometry->wire_length_um, 1e-9);
  limits.patterns = 1;
  EXPECT_FALSE(router.propose_maze(f.net, limits).geometry);
  occupancy.insert({
      {"M2", {1800, 0, 1900, 3000}},
      "wall"
  });
  occupancy.insert({
      {"M3", {1800, 0, 1900, 3000}},
      "wall"
  });
  limits.patterns = 30000;
  EXPECT_FALSE(router.propose_maze(f.net, limits).geometry);
  EXPECT_EQ(occupancy.size(), before + 2);
  EXPECT_TRUE(f.fp.nets.front().wires.empty());
}
TEST(SignalRouter, PatternExhaustionLeavesBudgetForMultiPinMaze) {
  Fixture f(4);
  f.tech.routing_layers.push_back("M3");
  f.tech.layers["M3"]           = f.tech.layers["M2"];
  f.tech.layers["M3"].direction = "VERTICAL";
  f.tech.cut_layers["V2"]       = f.tech.cut_layers["V1"];
  f.tech.vias["VIA23"].shapes   = {
      {"M2", {-.014, -.009, .014, .009}},
      {"M3", {-.009, -.014, .009, .014}},
      {"V2", {-.009, -.009, .009, .009}}
  };
  f.fp.components[1].place.y += 540;
  f.fp.components[3].place.y += 540;
  f.fp.tracks                 = {
      {"Y", "M2", 901,  50, 36},
      {"X", "M3",   9, 100, 36}
  };
  auto occupancy = routing_occupancy(f.tech, f.fp, 64);
  occupancy.insert({
      {"M2", {1200, 900, 1300, 1300}},
      "blockage"
  });
  Signal_router router(f.tech, f.fp, occupancy, f.profile());
  auto          limits = f.limits;
  limits.patterns      = 20000;
  const auto pattern   = router.propose_pattern(f.net, limits);
  ASSERT_FALSE(pattern.geometry);
  EXPECT_EQ(pattern.patterns_examined, limits.patterns);
  const auto result = router.propose(f.net, limits);
  ASSERT_TRUE(result.geometry) << result.reason;
  EXPECT_LE(result.patterns_examined, limits.patterns);
  EXPECT_LE(result.access_examined, limits.access_candidates);
  EXPECT_EQ(result.geometry->endpoints.size(), 4);
  auto routed = f.net;
  routed.wires.push_back({"FIXED", result.geometry->paths});
  EXPECT_TRUE(check_route_connectivity(f.tech, f.fp, routed).connected);
  EXPECT_TRUE(f.fp.nets.front().wires.empty());
  // Tiny budgets must not underflow when divided among strategies.
  for (size_t budget : {0, 1, 2}) {
    limits.patterns = limits.access_candidates = budget;
    const auto limited                         = router.propose(f.net, limits);
    EXPECT_FALSE(limited.geometry);
    EXPECT_LE(limited.patterns_examined, budget);
    EXPECT_LE(limited.access_examined, budget);
  }
}
TEST(SignalRouter, PatternEnforcesTableOnlySpacingBetweenNewCuts) {
  Fixture f;
  f.tech.routing_layers.push_back("M3");
  f.tech.layers["M3"]           = f.tech.layers["M2"];
  f.tech.layers["M3"].direction = "VERTICAL";
  f.tech.cut_layers["V2"]       = f.tech.cut_layers["V1"];
  f.tech.vias["VIA23"].shapes   = {
      {"M2", {-.014, -.009, .014, .009}},
      {"M3", {-.009, -.014, .009, .014}},
      {"V2", {-.009, -.009, .009, .009}}
  };
  f.fp.components[1].place.y += 540;
  f.fp.tracks                 = {
      {"Y", "M2", 1045,  2, 540},
      {"X", "M3",    9, 80,  36}
  };
  auto occupancy = routing_occupancy(f.tech, f.fp, 64);
  ASSERT_TRUE(Signal_router(f.tech, f.fp, occupancy, f.profile()).propose_pattern(f.net, f.limits).geometry);
  auto& cut = f.tech.cut_layers["V2"];
  cut.spacing.clear();
  cut.properties["LEF58_CUTCLASS"]     = {"CUTCLASS square WIDTH 0.018 ;"};
  cut.properties["LEF58_SPACINGTABLE"] = {"SPACINGTABLE DEFAULT 0.600 CUTCLASS square square - - ;"};
  EXPECT_FALSE(Signal_router(f.tech, f.fp, occupancy, f.profile()).propose_pattern(f.net, f.limits).geometry);
  // 540 nm center distance minus 18 nm cut width: exact 522 nm gap is legal.
  cut.properties["LEF58_SPACINGTABLE"] = {"SPACINGTABLE DEFAULT 0.522 CUTCLASS square square - - ;"};
  EXPECT_TRUE(Signal_router(f.tech, f.fp, occupancy, f.profile()).propose_pattern(f.net, f.limits).geometry);
}
}  // namespace livehd::placement
