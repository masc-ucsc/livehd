// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gtest/gtest.h"
#include "inou/lefdef/routing_geometry.hpp"
#include "signal_router.hpp"

namespace livehd::placement {
namespace {
namespace ph = physical;
struct Fixture {
  ph::Technology       tech;
  ph::Floorplan        frame;
  ph::Net              net{"signal", {}, {}, "SIGNAL"};
  Signal_route_profile profile{
      {"M1", "M2", "V1"},
      "M3", "V2", {"M2", "M3", "M4", "M5"}
  };
  std::shared_ptr<Signal_route_domain> domain = std::make_shared<Signal_route_domain>();
  ph::Routing_occupancy                occupancy{1000};
  Fixture() {
    tech.dbu = frame.dbu    = 1000;
    tech.manufacturing_grid = .001;
    frame.die               = {0, 0, 1200, 1200};
    for (int i = 1; i <= 5; ++i) {
      const auto name = "M" + std::to_string(i);
      auto&      l    = tech.layers[name];
      l.width         = .020;
      l.direction     = i % 2 ? "VERTICAL" : "HORIZONTAL";
      l.rules.loaded  = true;
      l.rules.area    = .0004;
      l.rules.spacing = {
          {.020, {}, {}}
      };
      l.pitch_x = l.pitch_y = .05;
      l.offset_x = l.offset_y = .025;
      l.has_offset            = true;
      tech.routing_layers.push_back(name);
      frame.tracks.push_back({i % 2 ? "X" : "Y", name, 25, 24, 50});
      if (i > 1) {
        const auto cut                 = "V" + std::to_string(i - 1);
        tech.cut_layers[cut].width     = .010;
        tech.cut_layers[cut].spacing   = {.010};
        tech.vias[cut + "_DEF"].shapes = {
            {"M" + std::to_string(i - 1), {-.010, -.010, .010, .010}},
            {                        cut, {-.005, -.005, .005, .005}},
            {                       name, {-.010, -.010, .010, .010}}
        };
      }
    }
    auto& m              = tech.macros["cell"];
    m.klass              = "CORE";
    m.width              = .1;
    m.height             = .25;
    m.pins["A"]          = {"INPUT", "SIGNAL", {{"M1", {.015, .015, .035, .035}}}};
    domain->owner        = net.name;
    domain->pin_layer    = "M1";
    domain->local_layers = {"M2", "M3"};
    domain->regions.push_back({
        {100, 100, 1100, 1100},
        {{"M2", {225, 475, 725, 975}}, {"M3", {325, 825}}}
    });
    frame.nets.push_back(net);
  }
  void pin(int64_t x, int64_t y) {
    const auto name = "u" + std::to_string(frame.components.size());
    Ann_place  pose(x - 25, y - 25, 100, 250);
    pose.status = ph::Status::PLACED;
    frame.components.push_back({name, "cell", pose});
    net.connections.push_back({name, "A"});
    frame.nets[0] = net;
  }
  Signal_route_limits limits() const { return {256, 8000, std::chrono::steady_clock::now() + std::chrono::seconds(3)}; }
  void                check(const Signal_route_trial& result) const {
    ASSERT_TRUE(result.geometry) << result.reason;
    EXPECT_TRUE(validate_signal_route_domain(tech, frame, net, result.geometry->paths, *domain, &result.geometry->nondefault_rules)
                    .empty());
    auto routed = net;
    routed.wires.push_back({"ROUTED", result.geometry->paths});
    EXPECT_TRUE(ph::check_route_connectivity(tech, frame, routed, nullptr, &result.geometry->nondefault_rules).connected);
  }
};
}  // namespace

TEST(SignalRouteDomain, EveryDirectSearchUsesConstructorReservation) {
  Fixture f;
  f.pin(225, 225);
  f.pin(725, 725);
  Signal_router router(f.tech, f.frame, f.occupancy, f.profile, nullptr, f.domain);
  f.check(router.propose_pattern(f.net, f.limits()));
  f.check(router.propose_maze(f.net, f.limits()));
  // Leave legal native tracks in the frame but remove this net's private H access.
  f.domain->regions[0].tracks["M2"] = {275};
  EXPECT_FALSE(router.propose_span(f.net, f.limits()).geometry);
  EXPECT_FALSE(router.propose_pattern(f.net, f.limits()).geometry);
  EXPECT_FALSE(router.propose_maze(f.net, f.limits()).geometry);
  EXPECT_TRUE(router.propose_spans(f.net, f.limits(), 4).candidates.empty());
}

TEST(SignalRouteDomain, RequestFacadeCannotReplaceAnExistingDomain) {
  Fixture f;
  f.pin(225, 225);
  f.pin(725, 225);
  Signal_router        legacy(f.tech, f.frame, f.occupancy, f.profile);
  Signal_route_request request;
  request.limits   = f.limits();
  request.domain   = f.domain;
  const auto first = legacy.try_route(f.net, request);
  ASSERT_FALSE(first.candidates.empty()) << first.reason;
  EXPECT_TRUE(validate_signal_route_domain(f.tech, f.frame, f.net, first.candidates.front().paths, *f.domain).empty());
  Signal_router bound(f.tech, f.frame, f.occupancy, f.profile, nullptr, f.domain);
  request.domain      = std::make_shared<Signal_route_domain>(*f.domain);
  const auto rejected = bound.try_route(f.net, request);
  EXPECT_TRUE(rejected.candidates.empty());
  EXPECT_EQ(rejected.reason, "cannot replace constructor-bound route domain");
  // Off-mode remains a native unrestricted proposal, independently of this new policy.
  EXPECT_TRUE(legacy.propose_span(f.net, f.limits()).geometry);
}

TEST(SignalRouteDomain, TapWindowContainsEveryAdmittedFiniteWire) {
  Fixture f;
  f.domain->regions = {
      {{200, 200, 580, 600}, {{"M2", {225, 475}}, {"M3", {325, 525}}}}
  };
  f.domain->max_stack_bridge_dbu = 64;
  // This native carrier crosses the region's right boundary transversely;
  // neither its full seed nor its finite extension may be clipped to that box.
  const ph::Shape seed{
      "M5",
      {565, 25, 585, 1175}
  };
  f.domain->carriers = {
      {f.net.name, "long_seed", seed, {565, 200, 585, 600}}
  };
  const auto window = signal_domain_tap_window(*f.domain);
  ASSERT_TRUE(window);
  EXPECT_EQ(*window, (ph::Rect{200, 136, 649, 664}));
  EXPECT_FALSE(window->contains(seed.rect));  // Retained metal is deliberately not a new routing right.
  std::map<std::string, size_t> admitted;
  for (const auto& layer : f.tech.routing_layers) {
    const bool horizontal = f.tech.layers.at(layer).direction == "HORIZONTAL";
    for (int x = 25; x < 1200; x += 50) {
      for (int y = 25; y < 1200; y += 50) {
        for (const int length : {20, 40, 84, 86, 150}) {
          for (const int width : {20, 40}) {
            const ph::Shape shape{
                layer,
                horizontal
                    ? ph::Rect{double(x - length / 2),  double(y - width / 2), double(x + length / 2),  double(y + width / 2)}
                    : ph::Rect{ double(x - width / 2), double(y - length / 2),  double(x + width / 2), double(y + length / 2)}
            };
            const bool before = signal_domain_wire(*f.domain, f.tech, f.frame, shape, true);
            const bool after  = window->contains(shape.rect) && before;
            EXPECT_EQ(after, before) << layer << " at " << x << ',' << y << " size " << length << ',' << width;
            admitted[layer] += before;
          }
        }
      }
    }
  }
  for (const auto& layer : {"M2", "M3", "M4", "M5"}) {
    EXPECT_GT(admitted[layer], 0u) << layer;
  }
  // Exact stack-window boundary, including the maximum allowed wire length.
  EXPECT_TRUE(signal_domain_wire(*f.domain,
                                 f.tech,
                                 f.frame,
                                 {
                                     "M4",
                                     {501, 215, 585, 235}
  },
                                 true));
  EXPECT_FALSE(signal_domain_wire(*f.domain,
                                  f.tech,
                                  f.frame,
                                  {
                                      "M4",
                                      {500, 215, 584, 235}
  },
                                  true));
  EXPECT_EQ(f.domain->carriers.front().seed, seed);
  f.domain->regions.clear();
  EXPECT_FALSE(signal_domain_tap_window(*f.domain));  // Global joins keep the existing window policy.
}

TEST(SignalRouteDomain, TapWindowPreservesLongCarrierAndAsymmetricNativeLandings) {
  Fixture f;
  f.domain->regions = {
      {{200, 200, 580, 600}, {{"M2", {225, 475}}, {"M3", {325, 525}}}}
  };
  f.domain->max_stack_bridge_dbu = 64;
  f.pin(225, 225);
  for (auto& [name, layer] : f.tech.layers) {
    if (name != "M1") {
      layer.rules.area = .0008;
    }
  }
  // The native access enumerator uses a centered minimum-area landing. Its
  // half-length must cover the asymmetric plate's full 25-DBU left extent.
  f.tech.layers.at("M2").rules.area = .001;
  // Native plates have unequal axial extensions; search bounds must not trim
  // their physical rectangles or the original pin/arrival geometry.
  for (int i = 1; i <= 4; ++i) {
    auto& shapes = f.tech.vias.at("V" + std::to_string(i) + "_DEF").shapes;
    for (auto& shape : shapes) {
      if (shape.layer == "M1" || !f.tech.layers.contains(shape.layer)) {
        continue;
      }
      if (f.tech.layers.at(shape.layer).direction == "HORIZONTAL") {
        // M2's escape lands exactly on the region's left boundary. M4's
        // shorter native plate fits the independently fixed stack window.
        shape.rect.xl = shape.layer == "M2" ? -.025 : -.020;
      } else {
        shape.rect.yh = .020;
      }
    }
  }
  const ph::Shape seed{
      "M5",
      {565, 25, 585, 1175}
  };
  const ph::Shape local_view{
      "M5",
      {565, 200, 585, 600}
  };
  f.domain->carriers = {
      {f.net.name, "long_seed", seed, local_view.rect}
  };
  f.occupancy.insert({seed, f.net.name, true});
  const ph::Signal_metal_rules rules(f.tech, f.frame);
  const auto                   access = ph::enumerate_pin_access(f.tech,
                                                                 f.frame,
                                                                 f.frame.components.front(),
                                                                 "A",
                                                                 f.occupancy,
                                                                 rules,
                                                                 256,
                                                                 f.net.name,
                                                                 f.profile.access);
  ASSERT_EQ(access.points.size(), 1u) << ::testing::PrintToString(access.rejected);
  EXPECT_EQ(access.points.front().escape.rect, (ph::Rect{200, 215, 250, 235}));
  EXPECT_TRUE(signal_domain_wire(*f.domain,
                                 f.tech,
                                 f.frame,
                                 {
                                     "M4",
                                     {505, 215, 595, 235}
  },
                                 true));
  EXPECT_FALSE(signal_domain_wire(*f.domain,
                                  f.tech,
                                  f.frame,
                                  {
                                      "M4",
                                      {500, 215, 600, 235}
  },
                                  true));
  Signal_router router(f.tech, f.frame, f.occupancy, f.profile, nullptr, f.domain);
  const auto    whole = router.propose_tap(f.net, f.limits(), {seed}, {});
  const auto    local = router.propose_tap(f.net, f.limits(), {local_view}, {});
  ASSERT_TRUE(whole.geometry) << whole.reason;
  ASSERT_TRUE(local.geometry) << local.reason;
  EXPECT_EQ(whole.geometry->paths, local.geometry->paths);
  EXPECT_EQ(whole.geometry->nondefault_rules, local.geometry->nondefault_rules);
  EXPECT_EQ(whole.patterns_examined, local.patterns_examined);
  EXPECT_EQ(
      validate_signal_route_domain(f.tech, f.frame, f.net, whole.geometry->paths, *f.domain, &whole.geometry->nondefault_rules),
      "");
  auto         routed = f.net;
  auto         frame  = f.frame;
  ph::Terminal arrival;
  arrival.name   = "arrival";
  arrival.net    = f.net.name;
  arrival.status = ph::Status::FIXED;
  arrival.shapes = {seed};
  frame.terminals.push_back(arrival);
  routed.connections.push_back({"PIN", arrival.name});
  routed.wires.push_back({"ROUTED", whole.geometry->paths});
  EXPECT_TRUE(ph::check_route_connectivity(f.tech, frame, routed, nullptr, &whole.geometry->nondefault_rules).connected);
  const auto window = signal_domain_tap_window(*f.domain);
  ASSERT_TRUE(window);
  for (const auto& shape : ph::route_geometry(f.tech, frame, routed, false, &whole.geometry->nondefault_rules)) {
    EXPECT_TRUE(window->contains(shape.rect)) << shape.layer;
  }
  EXPECT_EQ(f.domain->carriers.front().seed, seed);
  EXPECT_EQ(frame.terminals.back().shapes, (std::vector<ph::Shape>{seed}));
}

TEST(SignalRouteDomain, MazePrunesAccumulatedUpperRunWithoutWideningItsBudget) {
  for (const int x : {375, 475}) {
    Fixture f;
    f.domain->max_stack_bridge_dbu = 64;
    const ph::Shape source{
        "M5",
        {double(x - 10), 200, double(x + 10), 250}
    };
    const ph::Shape target{
        "M3",
        {315, 200, 335, 250}
    };
    f.domain->carriers.push_back({f.net.name, "immutable", source, source.rect});
    f.occupancy.insert({source, f.net.name, true});
    f.occupancy.insert({target, f.net.name, true});
    Signal_router router(f.tech, f.frame, f.occupancy, f.profile, nullptr, f.domain);
    const auto    result = router.propose_join(f.net, f.limits(), {source}, {target}, {});
    if (x == 375) {
      ASSERT_TRUE(result.geometry) << result.reason;
      EXPECT_EQ(validate_signal_route_domain(f.tech, f.frame, f.net, result.geometry->paths, *f.domain), "");
    } else {
      // Every individual short M4 maze edge can fit, but no whole native
      // intermediate run can bridge150DBU within this fixed64DBU domain.
      EXPECT_FALSE(result.geometry);
      EXPECT_EQ(result.reason, "maze exhausted");
      EXPECT_EQ(f.domain->max_stack_bridge_dbu, 64);
      EXPECT_EQ(f.domain->carriers.size(), 1u);
    }
  }
}

TEST(SignalRouteDomain, GlobalCarrierJoinDoesNotNeedOrBorrowLocalTracks) {
  Fixture f;
  f.domain->regions.clear();
  const ph::Shape a{
      "M4",
      {200, 215, 400, 235}
  },
      b{"M5", {365, 200, 385, 400}};
  f.domain->carriers = {
      {"signal", "horizontal_seed", a, a.rect},
      {"signal",   "vertical_seed", b, b.rect}
  };
  f.occupancy.insert_many({
      {a, "signal", true},
      {b, "signal", true}
  });
  Signal_router router(f.tech, f.frame, f.occupancy, f.profile, nullptr, f.domain);
  const auto    result = router.propose_join(f.net, f.limits(), {a}, {b}, {});
  ASSERT_TRUE(result.geometry) << result.reason;
  EXPECT_TRUE(validate_signal_route_domain(f.tech, f.frame, f.net, result.geometry->paths, *f.domain).empty());
  EXPECT_GE(result.geometry->vias, 1u);
  for (const auto& path : result.geometry->paths) {
    EXPECT_NE(path.layer, "M2");
  }
}

TEST(SignalRouteDomain, ComponentLabelsNeverConnectDisconnectedSeeds) {
  Fixture f;
  f.domain->regions.clear();
  const ph::Shape a{
      "M2",
      {200, 215, 240, 235}
  },
      b{"M2", {700, 715, 740, 735}};
  f.domain->carriers = {
      {"signal", "same_label", a, a.rect},
      {"signal", "same_label", b, b.rect}
  };
  f.occupancy.insert_many({
      {a, "signal", true},
      {b, "signal", true}
  });
  Signal_router router(f.tech, f.frame, f.occupancy, f.profile, nullptr, f.domain);
  EXPECT_FALSE(router.propose_join(f.net, f.limits(), {a}, {b}, {}).geometry);
  f.domain->carriers[0].owner = "foreign";
  const auto bad              = router.propose_join(f.net, f.limits(), {a}, {b}, {});
  EXPECT_FALSE(bad.geometry);
  EXPECT_EQ(bad.reason, "foreign or unidentified incoming carrier");
}

TEST(SignalRouteDomain, PendingCarrierContinuationUsesBorrowedPathsAndWidthRules) {
  Fixture f;
  f.pin(525, 225);
  f.domain->regions.clear();
  const ph::Shape seed{
      "M2",
      {200, 215, 240, 235}
  },
      pending{"M2", {240, 215, 400, 235}};
  f.domain->carriers = {
      {"signal", "immutable_seed", seed, {200, 215, 600, 235}}
  };
  f.occupancy.insert_many({
      {seed, "signal", true}
  });
  const std::vector<ph::Route_path> retained{ph::flush_wire_path(pending, true, "pending_width")};
  ph::Route_width_rules             pending_rules;
  for (const auto& layer : f.tech.routing_layers) {
    pending_rules["pending_width"][layer] = 20;
  }
  Signal_router        router(f.tech, f.frame, f.occupancy, f.profile);
  Signal_route_request request;
  request.limits         = f.limits();
  request.anchors        = {pending};
  request.domain         = f.domain;
  request.retained_paths = retained;
  request.retained_rules = &pending_rules;
  const auto result      = router.try_route(f.net, request);
  ASSERT_FALSE(result.candidates.empty()) << result.reason;
  EXPECT_TRUE(
      validate_signal_route_domain(f.tech, f.frame, f.net, result.candidates[0].paths, *f.domain, &pending_rules, nullptr, retained)
          .empty());
  // Context has not promoted the pending wire to a new immutable carrier.
  EXPECT_EQ(f.domain->carriers.size(), 1u);
  EXPECT_FALSE(validate_signal_route_domain(f.tech, f.frame, f.net, result.candidates[0].paths, *f.domain).empty());
  EXPECT_FALSE(
      validate_signal_route_domain(f.tech, f.frame, f.net, result.candidates[0].paths, *f.domain, nullptr, nullptr, retained)
          .empty());
}

TEST(SignalRouteDomain, MultiStepHeadersPreserveGeometryAndDomainVerdicts) {
  Fixture                           f;
  // One path changes layers twice and finishes with a relative patch. Nondefault
  // widths and unequal endpoint extensions make header/point loss observable.
  const ph::Route_point             a{225, 225, 0}, b{325, 225, 7}, c{325, 475, 12}, e{825, 475, 0};
  const ph::Route_via               via{"V2_DEF", ph::Orientation::FS};
  const ph::Route_rect              patch{-5, -15, 15, 15};
  const std::vector<ph::Route_path> joined{
      {"M2", {}, "STRIPE", {a, b, via, c, via, e, patch}, "wide"}
  };
  const std::vector<ph::Route_path> pieces{
      {"M2", {}, "STRIPE",     {a, b}, "wide"},
      {"M2", {}, "STRIPE",   {b, via}, "wide"},
      {"M3", {}, "STRIPE",     {b, c}, "wide"},
      {"M3", {}, "STRIPE",   {c, via}, "wide"},
      {"M2", {}, "STRIPE",     {c, e}, "wide"},
      {"M2", {}, "STRIPE", {e, patch}, "wide"}
  };
  const ph::Route_width_rules widths{
      {"wide", {{"M2", 30}, {"M3", 26}}}
  };
  const auto metal = [&](const auto& paths) {
    auto net = f.net;
    net.wires.push_back({"ROUTED", paths});
    return ph::route_geometry(f.tech, f.frame, net, false, &widths);
  };
  const auto whole_metal = metal(joined);
  EXPECT_EQ(whole_metal, metal(pieces));
  ASSERT_EQ(whole_metal.size(), 10u);
  EXPECT_EQ(whole_metal[0],
            (ph::Shape{
                "M2",
                {225, 210, 332, 240}
  }));
  EXPECT_EQ(whole_metal[4],
            (ph::Shape{
                "M3",
                {312, 218, 338, 487}
  }));
  EXPECT_EQ(whole_metal[8],
            (ph::Shape{
                "M2",
                {313, 460, 825, 490}
  }));
  EXPECT_EQ(whole_metal[9],
            (ph::Shape{
                "M2",
                {820, 460, 840, 490}
  }));
  const auto check = [&](const auto& paths, const ph::Route_width_rules* rules = nullptr) {
    return validate_signal_route_domain(f.tech, f.frame, f.net, paths, *f.domain, rules);
  };
  EXPECT_EQ(check(joined, &widths), "");
  EXPECT_EQ(check(pieces, &widths), "");
  EXPECT_FALSE(check(joined).empty());  // The path's nondefault rule cannot be dropped.
  EXPECT_EQ(check(joined), check(pieces));

  f.domain->regions[0].tracks["M3"] = {825};
  EXPECT_FALSE(check(joined, &widths).empty());
  EXPECT_EQ(check(joined, &widths), check(pieces, &widths));
  f.domain->regions[0].tracks["M3"] = {325, 825};

  auto bad_joined = joined, bad_pieces = pieces;
  bad_joined[0].width = bad_pieces[0].width = 30;  // Ordinary NETS cannot carry explicit WIDTH.
  EXPECT_FALSE(check(bad_joined, &widths).empty());
  EXPECT_EQ(check(bad_joined, &widths), check(bad_pieces, &widths));
  bad_joined                                               = joined;
  bad_pieces                                               = pieces;
  std::get<ph::Route_rect>(bad_joined[0].steps.back())     = ph::Route_rect{-5, 85, 15, 115};
  std::get<ph::Route_rect>(bad_pieces.back().steps.back()) = ph::Route_rect{-5, 85, 15, 115};
  EXPECT_FALSE(check(bad_joined, &widths).empty());
  EXPECT_EQ(check(bad_joined, &widths), check(bad_pieces, &widths));
}
}  // namespace livehd::placement
