// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "cellbox_sat_router.hpp"

#include <algorithm>
#include <chrono>
#include <set>

#include "gtest/gtest.h"
#include "inou/lefdef/routing_geometry.hpp"

namespace livehd::placement {
namespace {
namespace ph = physical;

struct Fixture {
  ph::Technology        tech;
  ph::Floorplan         frame;
  ph::Routing_occupancy background{256};
  Cellbox_sat_problem   problem;

  Fixture() {
    tech.dbu = frame.dbu    = 1000;
    tech.manufacturing_grid = .001;
    frame.die = problem.bounds = {0, 0, 800, 1000};
    problem.profile            = {
        {"M1", "M2", "V1"},
        "M3",
        "V2",
        {"M2", "M3"}
    };
    tech.sites["site"] = {.05, .25, true};
    for (int row = 0; row < 4; ++row) {
      frame.rows.push_back({"r" + std::to_string(row), "site", 0, row * 250, 16, 50, ph::Orientation::N});
    }
    for (int i = 1; i <= 3; ++i) {
      const auto name     = "M" + std::to_string(i);
      auto&      layer    = tech.layers[name];
      layer.width         = .020;
      layer.direction     = i % 2 ? "VERTICAL" : "HORIZONTAL";
      layer.rules.loaded  = true;
      layer.rules.area    = .0004;
      layer.rules.spacing = {
          {.020, {}, {}}
      };
      layer.pitch_x = layer.pitch_y = .05;
      layer.offset_x = layer.offset_y = .025;
      layer.has_offset                = true;
      tech.routing_layers.push_back(name);
      frame.tracks.push_back({i % 2 ? "X" : "Y", name, 25, i % 2 ? 16 : 20, 50});
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
    auto& macro      = tech.macros["cell"];
    macro.klass      = "CORE";
    macro.site       = "site";
    macro.width      = .05;
    macro.height     = .25;
    macro.symmetry_x = macro.symmetry_y = true;
    macro.pins["A"]                     = {"INPUT", "SIGNAL", {{"M1", {.015, .015, .035, .035}}}};
  }

  static Ann_place pose(int64_t x, int64_t y) {
    Ann_place result(x, y, 50, 250);
    result.orientation = ph::Orientation::N;
    result.status      = ph::Status::PLACED;
    return result;
  }

  size_t cell(int64_t x, int64_t y, std::vector<Ann_place> alternatives = {}, std::string macro = "cell") {
    const size_t index   = frame.components.size();
    const auto   initial = pose(x, y);
    frame.components.push_back({"u" + std::to_string(index), std::move(macro), initial});
    if (alternatives.empty()) {
      alternatives.push_back(initial);
    }
    problem.cells.push_back({index, std::move(alternatives)});
    return index;
  }

  size_t net(std::string name, const std::vector<std::pair<size_t, std::string>>& pins,
             std::vector<int64_t> hs = {25, 75, 225, 275, 525, 775}, std::vector<int64_t> vs = {125, 325, 525}) {
    ph::Net native{name, {}, {}, "SIGNAL"};
    for (const auto& [cell_index, pin] : pins) {
      native.connections.push_back({frame.components.at(cell_index).name, pin});
    }
    frame.nets.push_back(native);
    auto domain                  = std::make_shared<Signal_route_domain>();
    domain->owner                = name;
    domain->pin_layer            = "M1";
    domain->local_layers         = {"M2", "M3"};
    domain->max_stack_bridge_dbu = 64;
    domain->regions.push_back({
        problem.bounds,
        {{"M2", std::move(hs)}, {"M3", std::move(vs)}}
    });
    const size_t id = problem.nets.size();
    problem.nets.push_back({100 + id, std::move(native), false, {}, std::move(domain)});
    return id;
  }

  void incoming(size_t n, int64_t y, std::string id) {
    auto& input    = problem.nets.at(n);
    input.external = true;
    const ph::Shape seed{
        "M3",
        {715, double(y - 10), 735, double(y + 10)}
    };
    input.incoming.push_back({id, {ph::flush_wire_path(seed, false)}, {}});
    auto domain = std::make_shared<Signal_route_domain>(*input.domain);
    domain->carriers.push_back({
        input.local_net.name,
        id,
        seed,
        {715, 0, 735, 1000}
    });
    input.domain = std::move(domain);
  }

  Cellbox_sat_limits limits() const {
    Cellbox_sat_limits result;
    result.deadline                    = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    result.layouts                     = 8;
    result.candidates_per_net_layout   = 4;
    result.route_trials_per_net_layout = 6;
    result.access_candidates           = 256;
    return result;
  }

  Cellbox_sat_result solve() const { return solve_cellbox_sat(tech, frame, background, problem, limits()); }

  ph::Floorplan selected_frame(const Cellbox_sat_result& result) const {
    auto selected = frame;
    for (size_t i = 0; i < result.poses.size(); ++i) {
      selected.components[problem.cells[i].component].place = result.poses[i];
    }
    return selected;
  }

  ph::Route_connectivity connectivity(const Cellbox_sat_result& result, size_t n, bool restore_initial = false) const {
    const auto  selected = restore_initial ? frame : selected_frame(result);
    const auto& input    = problem.nets.at(n);
    const auto  found    = std::ranges::find(result.routes, input.source_net, &Cellbox_sat_net_route::source_net);
    if (found == result.routes.end()) {
      return {false, 0, "missing route"};
    }
    auto                   paths = found->geometry.paths;
    std::vector<ph::Shape> fixed;
    for (const auto& root : input.incoming) {
      paths.insert(paths.end(), root.paths.begin(), root.paths.end());
      fixed.insert(fixed.end(), root.terminals.begin(), root.terminals.end());
    }
    std::vector<std::vector<ph::Shape>> pins;
    for (const auto& connection : input.local_net.connections) {
      const auto  instance = std::ranges::find(selected.components, connection.instance, &ph::Component::name);
      const auto& macro    = tech.macros.at(instance->macro);
      auto&       shapes   = pins.emplace_back();
      for (const auto& shape : macro.pins.at(connection.pin).shapes) {
        shapes.push_back({shape.layer, ph::placed_rect(macro, instance->place, selected.dbu, shape.rect)});
      }
    }
    return ph::check_conductor_connectivity(tech, selected, paths, fixed, pins, &found->geometry.nondefault_rules);
  }
};

TEST(CellboxSatRouter, SelectsMovedLayoutAndExtendsOnlyItsActualIncomingCarrier) {
  Fixture    f;
  const auto u = f.cell(0, 0, {Fixture::pose(0, 0), Fixture::pose(300, 250)});
  const auto n = f.net("external",
                       {
                           {u, "A"}
  },
                       {25, 275},
                       {});
  f.incoming(n, 25, "source");
  f.background.insert({
      {"M2", {0, 15, 100, 35}},
      "fixed_other",
      true
  });
  const auto before        = f.frame.components;
  const auto seed          = f.problem.nets[n].incoming.front().paths;
  auto       fixed_problem = f.problem;
  fixed_problem.cells.front().poses.resize(1);
  const auto fixed = solve_cellbox_sat(f.tech, f.frame, f.background, fixed_problem, f.limits());
  EXPECT_NE(fixed.status, Cellbox_sat_status::complete);
  EXPECT_TRUE(fixed.poses.empty());
  EXPECT_TRUE(fixed.routes.empty());
  const auto result = f.solve();
  ASSERT_EQ(result.status, Cellbox_sat_status::complete) << result.reason;
  ASSERT_EQ(result.poses.size(), 1u);
  EXPECT_EQ(result.poses.front(), Fixture::pose(300, 250));
  EXPECT_GE(result.metrics.layouts, 2u);
  EXPECT_GE(result.metrics.sat_calls, 2u);
  EXPECT_TRUE(f.connectivity(result, n).connected);
  EXPECT_FALSE(f.connectivity(result, n, true).connected);  // The route cannot be rebound to the old layout.
  auto routed = f.problem.nets[n].local_net;
  routed.wires.push_back({"ROUTED", result.routes.front().geometry.paths});
  const auto shapes
      = ph::route_geometry(f.tech, f.selected_frame(result), routed, false, &result.routes.front().geometry.nondefault_rules);
  EXPECT_TRUE(std::ranges::any_of(shapes, [](const auto& shape) {
    return shape.layer == "M3" && shape.rect.xl == 715 && shape.rect.xh == 735 && shape.rect.yh >= 275;
  }));
  EXPECT_EQ(f.frame.components, before);
  EXPECT_EQ(f.problem.nets[n].incoming.front().paths, seed);
  EXPECT_EQ(f.background.size(), 1u);
}

TEST(CellboxSatRouter, JointWholeNetChoicesResolveAnOtherwiseConflictingHorizontalSpan) {
  Fixture f;
  f.tech.macros["cell"].pins["A"].shapes.front().rect.yh = .235;
  for (int x : {0, 100, 200, 300}) {
    f.cell(x, 0);
  }
  f.net("outer",
        {
            {0, "A"},
            {3, "A"}
  },
        {25, 225},
        {});
  f.net("inner",
        {
            {1, "A"},
            {2, "A"}
  },
        {25, 225},
        {});
  const auto result = f.solve();
  ASSERT_EQ(result.status, Cellbox_sat_status::complete) << result.reason;
  ASSERT_EQ(result.routes.size(), 2u);
  EXPECT_GE(result.metrics.candidates, 4u);
  std::vector<std::set<double>> levels;
  for (size_t n = 0; n < result.routes.size(); ++n) {
    EXPECT_TRUE(f.connectivity(result, n).connected);
    auto routed = f.problem.nets[n].local_net;
    routed.wires.push_back({"ROUTED", result.routes[n].geometry.paths});
    auto& used = levels.emplace_back();
    for (const auto& shape : ph::route_geometry(f.tech, f.frame, routed, false, &result.routes[n].geometry.nondefault_rules)) {
      if (shape.layer == "M2") {
        used.insert((shape.rect.yl + shape.rect.yh) / 2);
      }
    }
    EXPECT_EQ(used.size(), 1u);
  }
  EXPECT_NE(levels[0], levels[1]);
  // Restrict the generated physical choices to the colliding single H level.
  for (auto& input : f.problem.nets) {
    auto domain                          = std::make_shared<Signal_route_domain>(*input.domain);
    domain->regions.front().tracks["M2"] = {25};
    input.domain                         = std::move(domain);
  }
  const auto blocked = f.solve();
  EXPECT_EQ(blocked.status, Cellbox_sat_status::finite_pool_unsat) << blocked.reason;
  EXPECT_GT(blocked.metrics.incompatible_pairs, 0u);
  EXPECT_TRUE(blocked.routes.empty());
  EXPECT_TRUE(blocked.poses.empty());
}

TEST(CellboxSatRouter, InternalThreePinNetAndZeroSignalCellKeepEveryPhysicalObligation) {
  Fixture f;
  for (int x : {0, 200, 400}) {
    f.cell(x, 0);
  }
  f.tech.macros["empty"] = f.tech.macros.at("cell");
  f.tech.macros["empty"].pins.clear();
  f.cell(600, 0, {}, "empty");
  const auto n      = f.net("internal",
                            {
                                {0, "A"},
                                {1, "A"},
                                {2, "A"}
  },
                            {25},
                            {});
  const auto result = f.solve();
  ASSERT_EQ(result.status, Cellbox_sat_status::complete) << result.reason;
  EXPECT_EQ(result.poses.size(), 4u);
  EXPECT_TRUE(f.connectivity(result, n).connected);
  f.problem.nets[n].local_net.connections.pop_back();
  const auto dropped = f.solve();
  EXPECT_EQ(dropped.status, Cellbox_sat_status::invalid);
  EXPECT_TRUE(dropped.poses.empty());
  EXPECT_TRUE(dropped.routes.empty());
}

TEST(CellboxSatRouter, SameCellPinsAndInternalSingletonHaveDistinctNativeSemantics) {
  Fixture f;
  f.tech.macros["cell"].pins["B"] = {"INPUT", "SIGNAL", {{"M1", {.015, .065, .035, .085}}}};
  f.cell(0, 0);
  const auto n      = f.net("same_cell",
                            {
                                {0, "A"},
                                {0, "B"}
  },
                            {25, 75},
                            {125});
  const auto result = f.solve();
  ASSERT_EQ(result.status, Cellbox_sat_status::complete) << result.reason;
  EXPECT_TRUE(f.connectivity(result, n).connected);
  EXPECT_FALSE(result.routes.front().geometry.paths.empty());

  Fixture singleton;
  singleton.cell(0, 0);
  singleton.net("one_real_pin",
                {
                    {0, "A"}
  });
  const auto one = singleton.solve();
  ASSERT_EQ(one.status, Cellbox_sat_status::complete) << one.reason;
  ASSERT_EQ(one.routes.size(), 1u);
  EXPECT_TRUE(one.routes.front().geometry.paths.empty());
  EXPECT_TRUE(singleton.connectivity(one, 0).connected);
}

TEST(CellboxSatRouter, EveryIncomingRootIsGeometricAndMandatoryEvenWithoutResidentPins) {
  Fixture    f;
  const auto n = f.net("junction", {}, {25, 525}, {});
  f.incoming(n, 25, "first");
  f.incoming(n, 525, "second");
  const auto result = f.solve();
  ASSERT_EQ(result.status, Cellbox_sat_status::complete) << result.reason;
  EXPECT_TRUE(result.poses.empty());
  EXPECT_TRUE(f.connectivity(result, n).connected);
  EXPECT_FALSE(result.routes.front().geometry.paths.empty());
  auto& roots = f.problem.nets[n].incoming;
  roots.front().paths.insert(roots.front().paths.end(), roots.back().paths.begin(), roots.back().paths.end());
  roots.pop_back();
  const auto fabricated_equivalence = f.solve();
  EXPECT_EQ(fabricated_equivalence.status, Cellbox_sat_status::invalid);
  EXPECT_TRUE(fabricated_equivalence.poses.empty());
  EXPECT_TRUE(fabricated_equivalence.routes.empty());
}

TEST(CellboxSatRouter, ExternalPinCannotBecomeAnInternalSingletonWhenItsCarrierIsMissing) {
  Fixture f;
  f.cell(0, 0);
  const auto n               = f.net("external",
                                     {
                                         {0, "A"}
  });
  f.problem.nets[n].external = true;
  const auto result          = f.solve();
  EXPECT_EQ(result.status, Cellbox_sat_status::invalid);
  EXPECT_TRUE(result.routes.empty());
  EXPECT_TRUE(result.poses.empty());
}

TEST(CellboxSatRouter, ExpiredDeadlineAndZeroSignalBoxPreserveInputState) {
  Fixture f;
  f.tech.macros["cell"].pins.clear();
  f.cell(0, 0, {Fixture::pose(0, 0), Fixture::pose(100, 250)});
  const auto before    = f.frame.components;
  auto       limits    = f.limits();
  limits.deadline      = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
  const auto timed_out = solve_cellbox_sat(f.tech, f.frame, f.background, f.problem, limits);
  EXPECT_EQ(timed_out.status, Cellbox_sat_status::unknown);
  EXPECT_TRUE(timed_out.metrics.deadline_hit);
  EXPECT_TRUE(timed_out.poses.empty());
  EXPECT_TRUE(timed_out.routes.empty());
  const auto complete = f.solve();
  ASSERT_EQ(complete.status, Cellbox_sat_status::complete) << complete.reason;
  EXPECT_EQ(complete.poses.size(), 1u);
  EXPECT_TRUE(complete.routes.empty());
  EXPECT_EQ(f.frame.components, before);
  EXPECT_EQ(f.background.size(), 0u);
  EXPECT_TRUE(f.frame.nets.empty());
  EXPECT_LE(complete.metrics.setup_seconds + complete.metrics.layout_seconds + complete.metrics.candidate_seconds
                + complete.metrics.sat_seconds + complete.metrics.verification_seconds,
            complete.metrics.total_seconds + 1e-6);
}

TEST(CellboxSatRouter, MovedObstructionCannotHideOnUntouchedIncomingMetal) {
  Fixture f;
  f.tech.macros["cell"].pins.clear();
  f.tech.macros["cell"].obstructions.push_back({
      "M3",
      {.015, .015, .035, .035}
  });
  f.cell(0, 0);
  const auto n = f.net("incoming_only", {}, {25}, {});
  f.incoming(n, 25, "actual_root");
  const auto clear = f.solve();
  ASSERT_EQ(clear.status, Cellbox_sat_status::complete) << clear.reason;
  ASSERT_EQ(clear.routes.size(), 1u);
  EXPECT_TRUE(clear.routes.front().geometry.paths.empty());
  f.problem.cells.front().poses = {Fixture::pose(700, 0)};
  const auto collision          = f.solve();
  EXPECT_NE(collision.status, Cellbox_sat_status::complete);
  EXPECT_GT(collision.metrics.native_rejections, 0u);
  EXPECT_TRUE(collision.poses.empty());
  EXPECT_TRUE(collision.routes.empty());
  EXPECT_EQ(f.frame.components.front().place, Fixture::pose(0, 0));
}

TEST(CellboxSatRouter, CarrierPermissionRequiresExactActualComponentMetalCoverage) {
  Fixture f;
  f.cell(0, 0);
  const auto n = f.net("external",
                       {
                           {0, "A"}
  },
                       {25},
                       {});
  f.incoming(n, 25, "actual_root");
  auto& root = f.problem.nets[n].incoming.front();
  root.paths = {
      ph::flush_wire_path({"M3", {715, 15, 735, 25}},
      false),
      ph::flush_wire_path({"M3", {715, 25, 735, 35}},
      false)
  };
  const auto covered_union = f.solve();
  ASSERT_EQ(covered_union.status, Cellbox_sat_status::complete) << covered_union.reason;
  EXPECT_TRUE(f.connectivity(covered_union, n).connected);
  auto fabricated                           = std::make_shared<Signal_route_domain>(*f.problem.nets[n].domain);
  fabricated->carriers.front().seed.rect.yh = 85;
  f.problem.nets[n].domain                  = fabricated;
  const auto missing_metal                  = f.solve();
  EXPECT_EQ(missing_metal.status, Cellbox_sat_status::invalid);
  EXPECT_TRUE(missing_metal.routes.empty());
  fabricated->carriers.front().seed.rect.yh = 35;
  fabricated->carriers.front().component    = "not_an_actual_root";
  const auto missing_group                  = f.solve();
  EXPECT_EQ(missing_group.status, Cellbox_sat_status::invalid);
  EXPECT_TRUE(missing_group.poses.empty());
  EXPECT_TRUE(missing_group.routes.empty());
}

TEST(CellboxSatRouter, RouteSatRunsBeforeSpendingAlternativeMenuAndLayoutBudgets) {
  Fixture f;
  f.cell(0, 0, {Fixture::pose(0, 0), Fixture::pose(100, 0)});
  f.cell(400, 0);
  f.net("internal",
        {
            {0, "A"},
            {1, "A"}
  },
        {25, 75, 225},
        {});
  const auto result = f.solve();
  ASSERT_EQ(result.status, Cellbox_sat_status::complete) << result.reason;
  EXPECT_GE(result.metrics.layouts, 2u);
  EXPECT_EQ(result.metrics.route_trials, 1u);
  EXPECT_EQ(result.metrics.candidates, 1u);
  EXPECT_EQ(result.metrics.route_sat_calls, 1u);
  EXPECT_GE(result.metrics.layout_sat_calls, 2u);
  EXPECT_EQ(result.metrics.sat_calls, result.metrics.layout_sat_calls + result.metrics.route_sat_calls);
  EXPECT_TRUE(f.connectivity(result, 0).connected);
  EXPECT_LE(result.metrics.setup_seconds + result.metrics.layout_seconds + result.metrics.candidate_seconds
                + result.metrics.sat_seconds + result.metrics.verification_seconds,
            result.metrics.total_seconds + 1e-6);
  EXPECT_LE(result.metrics.setup_cpu_seconds + result.metrics.layout_cpu_seconds + result.metrics.candidate_cpu_seconds
                + result.metrics.sat_cpu_seconds + result.metrics.verification_cpu_seconds,
            result.metrics.total_cpu_seconds + 1e-6);
}

TEST(CellboxSatRouter, NativePinUnionSuppliesViaLandingAreaWithoutInventingMetal) {
  Fixture f;
  f.tech.layers["M1"].rules.area                      = .0009;
  f.tech.macros["cell"].pins["A"].shapes.front().rect = {.005, .005, .045, .045};
  f.cell(0, 0);
  f.cell(200, 0);
  f.net("internal",
        {
            {0, "A"},
            {1, "A"}
  },
        {25},
        {});
  const auto enough = f.solve();
  ASSERT_EQ(enough.status, Cellbox_sat_status::complete) << enough.reason;
  EXPECT_TRUE(f.connectivity(enough, 0).connected);
  f.tech.macros["cell"].pins["A"].shapes.front().rect = {.020, .015, .030, .035};
  const auto insufficient                             = f.solve();
  EXPECT_NE(insufficient.status, Cellbox_sat_status::complete);
  EXPECT_TRUE(insufficient.poses.empty());
  EXPECT_TRUE(insufficient.routes.empty());
}

TEST(CellboxSatRouter, DistinctTouchingPinsNeedNoInventedRouteButSeparatedPinsStillFail) {
  Fixture f;
  f.tech.macros["cell"].pins["B"] = f.tech.macros["cell"].pins.at("A");
  f.cell(0, 0);
  const auto n        = f.net("touching",
                              {
                                  {0, "A"},
                                  {0, "B"}
  },
                              {},
                              {});
  const auto touching = f.solve();
  ASSERT_EQ(touching.status, Cellbox_sat_status::complete) << touching.reason;
  ASSERT_EQ(touching.routes.size(), 1u);
  EXPECT_TRUE(touching.routes.front().geometry.paths.empty());
  EXPECT_EQ(touching.metrics.route_trials, 0u);
  EXPECT_GE(touching.metrics.route_sat_calls, 1u);
  EXPECT_TRUE(f.connectivity(touching, n).connected);
  f.tech.macros["cell"].pins["B"].shapes.front().rect = {.015, .065, .035, .085};
  const auto separated                                = f.solve();
  EXPECT_NE(separated.status, Cellbox_sat_status::complete);
  EXPECT_GT(separated.metrics.native_rejections, 0u);
  EXPECT_TRUE(separated.poses.empty());
  EXPECT_TRUE(separated.routes.empty());
}

TEST(CellboxSatRouter, MovedCutObstructionCannotOverlapAnUntouchedIncomingVia) {
  Fixture f;
  f.tech.macros["cell"].pins.clear();
  f.tech.macros["cell"].obstructions.push_back({
      "V2",
      {.020, .020, .030, .030}
  });
  f.cell(0, 0);
  const auto n = f.net("incoming_only", {}, {25}, {});
  f.incoming(n, 25, "actual_root");
  ph::Route_path via;
  via.layer = "M2";
  via.steps = {
      ph::Route_point{725, 25, {}},
      ph::Route_via{"V2_DEF"}
  };
  f.problem.nets[n].incoming.front().paths.push_back(via);
  const auto paths = f.problem.nets[n].incoming.front().paths;
  const auto cells = f.frame.components;
  const auto clear = f.solve();
  ASSERT_EQ(clear.status, Cellbox_sat_status::complete) << clear.reason;
  ASSERT_EQ(clear.routes.size(), 1u);
  EXPECT_TRUE(clear.routes.front().geometry.paths.empty());
  f.problem.cells.front().poses = {Fixture::pose(700, 0)};
  const auto collision          = f.solve();
  EXPECT_NE(collision.status, Cellbox_sat_status::complete);
  EXPECT_GT(collision.metrics.native_rejections, 0u);
  EXPECT_TRUE(collision.poses.empty());
  EXPECT_TRUE(collision.routes.empty());
  EXPECT_EQ(f.frame.components, cells);
  EXPECT_EQ(f.problem.nets[n].incoming.front().paths, paths);
}

TEST(CellboxSatRouter, NativeSourcePinAndObstructionContoursPreservePgAndForeignSpacing) {
  for (const bool obstruction : {false, true}) {
    SCOPED_TRACE(obstruction);
    Fixture f;
    auto&   layer       = f.tech.layers["M1"];
    layer.width         = .018;
    layer.rules.spacing = {
        {.018, {}, {}}
    };
    layer.rules.properties["LEF58_EOLKEEPOUT"] = {"EOLKEEPOUT .01825 EXTENSION 0 0 .031 ;"};
    // The translated source contour has the exact native DFF QN dimensions.
    const std::vector<ph::Shape> capped{
        {"M1", {.032, .027, .050, .243}},
        {"M1",    {0, .225, .050, .243}},
        {"M1",    {0, .027, .050, .045}}
    };
    auto& macro = f.tech.macros["cell"];
    if (obstruction) {
      macro.pins.clear();
      macro.obstructions = capped;
    } else {
      macro.pins["A"].shapes = capped;
    }
    f.cell(100, 250);
    if (!obstruction) {
      f.net("internal",
            {
                {0, "A"}
      },
            {},
            {});
    }
    f.background.insert({
        {"M1", {0, 511, 800, 529}},
        "VDD"
    });
    const auto original = f.frame.components;
    const auto legal    = f.solve();
    ASSERT_EQ(legal.status, Cellbox_sat_status::complete) << legal.reason;
    EXPECT_EQ(legal.poses.size(), 1u);
    if (!obstruction) {
      ASSERT_EQ(legal.routes.size(), 1u);
      EXPECT_TRUE(legal.routes.front().geometry.paths.empty());
      EXPECT_TRUE(f.connectivity(legal, 0).connected);
    }
    auto& source = obstruction ? macro.obstructions : macro.pins["A"].shapes;
    source.erase(source.begin() + 1);  // Expose the real narrow upper end.
    const auto exposed = f.solve();
    EXPECT_NE(exposed.status, Cellbox_sat_status::complete);
    EXPECT_TRUE(exposed.poses.empty());
    EXPECT_TRUE(exposed.routes.empty());
    source = capped;
    f.background.insert({
        {"M1", {132, 480, 150, 498}},
        "foreign"
    });
    const auto shorted = f.solve();
    EXPECT_NE(shorted.status, Cellbox_sat_status::complete);
    EXPECT_TRUE(shorted.poses.empty());
    EXPECT_TRUE(shorted.routes.empty());
    EXPECT_EQ(f.frame.components, original);
  }
}

TEST(CellboxSatRouter, SourceGroupsNeverHideDifferentPinsOrOutOfDieOverhangs) {
  Fixture f;
  auto&   macro   = f.tech.macros["cell"];
  macro.pins["B"] = macro.pins.at("A");
  f.cell(0, 0);
  f.net("one",
        {
            {0, "A"}
  },
        {},
        {});
  f.net("two",
        {
            {0, "B"}
  },
        {},
        {});
  const auto foreign = f.solve();
  EXPECT_NE(foreign.status, Cellbox_sat_status::complete);
  EXPECT_TRUE(foreign.poses.empty());
  EXPECT_TRUE(foreign.routes.empty());
  macro.pins.erase("B");
  f.frame.nets.pop_back();
  f.problem.nets.pop_back();
  ASSERT_EQ(f.solve().status, Cellbox_sat_status::complete);
  macro.pins["A"].shapes.front().rect.xl = -.001;
  const auto outside                     = f.solve();
  EXPECT_NE(outside.status, Cellbox_sat_status::complete);
  EXPECT_TRUE(outside.poses.empty());
  EXPECT_TRUE(outside.routes.empty());
  macro.pins.clear();
  f.frame.nets.clear();
  f.problem.nets.clear();
  macro.obstructions.push_back({
      "M1",
      {-.001, .015, .035, .035}
  });
  EXPECT_NE(f.solve().status, Cellbox_sat_status::complete);
}

TEST(CellboxSatRouter, RoutedViaOnCappedSourcePinUsesExactContourAgainstPg) {
  Fixture f;
  auto&   layer       = f.tech.layers["M1"];
  layer.width         = .018;
  layer.rules.spacing = {
      {.018, {}, {}}
  };
  layer.rules.properties["LEF58_EOLKEEPOUT"] = {"EOLKEEPOUT .01825 EXTENSION 0 0 .031 ;"};
  f.tech.macros["cell"].pins["A"].shapes     = {
      {"M1", {.032, .027, .050, .243}},
      {"M1",    {0, .225, .050, .243}},
      {"M1",    {0, .027, .050, .045}}
  };
  f.tech.vias["V1_DEF"].shapes.front().rect = {-.009, -.010, .009, .010};
  f.cell(100, 250);
  f.cell(400, 250);
  f.net("internal",
        {
            {0, "A"},
            {1, "A"}
  },
        {325},
        {});
  f.background.insert({
      {"M1", {0, 511, 800, 529}},
      "VDD"
  });
  const auto routed = f.solve();
  ASSERT_EQ(routed.status, Cellbox_sat_status::complete) << routed.reason;
  ASSERT_EQ(routed.routes.size(), 1u);
  EXPECT_FALSE(routed.routes.front().geometry.paths.empty());
  EXPECT_TRUE(f.connectivity(routed, 0).connected);
  EXPECT_GT(routed.metrics.route_trials, 0u);
  EXPECT_GT(routed.metrics.route_sat_calls, 0u);
}

}  // namespace
}  // namespace livehd::placement
