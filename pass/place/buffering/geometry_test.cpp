// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <random>
#include <set>

#include "gtest/gtest.h"

namespace b = livehd::placement::buffering;
namespace m = livehd::liberty::timing;
namespace {
b::Geometry_request request() {
  return {
      {0, 0, 20, 10},
      {{1, 5}, {19, 5}},
      {},
      {{8, 2, 12, 8}},
      0
  };
}
// Independent unit-lattice BFS: no visibility graph/corner logic is reused.
std::vector<int> lattice(const b::Geometry_request& r) {
  const int        width = int(r.core.xh - r.core.xl + 1), height = int(r.core.yh - r.core.yl + 1);
  std::vector<int> d(width * height, -1);
  auto             index = [&](b::Route_point p) { return int(p.y - r.core.yl) * width + int(p.x - r.core.xl); };
  auto             free  = [&](b::Route_point p) {
    if (p.x < r.core.xl || p.x > r.core.xh || p.y < r.core.yl || p.y > r.core.yh) {
      return false;
    }
    for (auto box : r.routing_exclusions) {
      if (p.x >= box.xl - r.clearance_dbu && p.x <= box.xh + r.clearance_dbu && p.y >= box.yl - r.clearance_dbu
          && p.y <= box.yh + r.clearance_dbu) {
        return false;
      }
    }
    return true;
  };
  std::queue<b::Route_point> q;
  if (free(r.terminals[0])) {
    q.push(r.terminals[0]);
    d[index(r.terminals[0])] = 0;
  }
  while (!q.empty()) {
    const auto p = q.front();
    q.pop();
    for (auto step : std::vector<b::Route_point>{
             { 1,  0},
             {-1,  0},
             { 0,  1},
             { 0, -1}
    }) {
      b::Route_point next{p.x + step.x, p.y + step.y};
      if (free(next) && d[index(next)] == -1) {
        d[index(next)] = d[index(p)] + 1;
        q.push(next);
      }
    }
  }
  std::vector<int> answer;
  for (auto t : r.terminals) {
    answer.push_back(d[index(t)]);
  }
  return answer;
}
std::vector<double> tree_distances(const b::Routing_tree& tree, size_t sinks) {
  std::vector<double> d(tree.vertices.size());
  for (const auto& e : tree.segments) {
    d[e.child] = d[e.parent] + e.length_um;
  }
  std::vector<double> result(sinks);
  for (size_t v = 0; v < tree.vertices.size(); ++v) {
    for (auto s : tree.vertices[v].sinks) {
      result[s] = d[v];
    }
  }
  return result;
}
m::Table table(double base, double cap) {
  return {
      m::Variable::input_slew,
      m::Variable::output_load,
      {0, 100},
      {0, 100},
      {base, base + 100 * cap, base, base + 100 * cap}
  };
}
void cell(m::Library& lib, std::string name, double cin, double cap_limit) {
  m::Cell c;
  c.name                 = name;
  auto& in               = c.pins["A"];
  in.name                = "A";
  in.direction           = "input";
  in.capacitance_ff      = {cin, cin};
  in.max_transition_ps   = 1000;
  auto& out              = c.pins["Y"];
  out.name               = "Y";
  out.direction          = "output";
  out.function           = "A";
  out.max_capacitance_ff = cap_limit;
  out.max_transition_ps  = 1000;
  m::Arc arc;
  arc.related_pin = "A";
  arc.type        = "combinational";
  arc.sense       = m::Sense::positive;
  for (unsigned t = 0; t < 2; ++t) {
    arc.delay[t] = table(1, .1);
    arc.slew[t]  = table(1, .2);
  }
  out.arcs.push_back(arc);
  lib.cells.emplace(name, std::move(c));
}
}  // namespace

TEST(BufferGeometry, ObstacleDetourAndDirectionalWireConservation) {
  auto r     = request();
  auto built = b::build_visibility_graph(r);
  ASSERT_EQ(built.status, b::Geometry_status::complete);
  ASSERT_TRUE(built.graph);
  auto routed = b::shortest_path_tree(*built.graph, 1, {2, 3, 5, 7});
  ASSERT_EQ(routed.status, b::Route_status::connected);
  ASSERT_TRUE(routed.tree);
  EXPECT_EQ(routed.length_dbu, 26);
  EXPECT_EQ(tree_distances(*routed.tree, 1)[0], lattice(r)[1]);
  b::Context c;
  c.driver = {
      "d",
      {"D", "A", "Y"},
      {1, 5},
      {0, 0},
      {1, 1}
  };
  c.sinks.push_back({
      "s",
      "S",
      "A",
      {19, 5},
      {1000, 1000},
      {}
  });
  auto expanded = b::expand_tree(c, *routed.tree, std::vector<size_t>(routed.tree->vertices.size()));
  ASSERT_EQ(expanded.trees.size(), 1);
  double cap = 0, resistance = 0;
  for (auto n : expanded.trees[0].rc.nodes) {
    cap += n.capacitance_ff;
  }
  for (auto e : expanded.trees[0].rc.edges) {
    resistance += e.resistance_ohm;
  }
  // 18 horizontal + 8 vertical DBU, directional RC, each pi C once.
  EXPECT_DOUBLE_EQ(cap, 18 * 3 + 8 * 7);
  EXPECT_DOUBLE_EQ(resistance, 18 * 2 + 8 * 5);
  EXPECT_DOUBLE_EQ(expanded.trees[0].rc.wire_length_um, 26);
}
TEST(BufferGeometry, FanoutOneThroughFourAgainstIndependentLattice) {
  for (size_t fanout = 1; fanout <= 4; ++fanout) {
    auto r = request();
    r.terminals.resize(1);
    for (size_t s = 0; s < fanout; ++s) {
      r.terminals.push_back({19, int64_t(1 + 2 * s)});
    }
    const auto expected = lattice(r);
    auto       built    = b::build_visibility_graph(r);
    ASSERT_TRUE(built.graph);
    auto routed = b::shortest_path_tree(*built.graph, 1, {0, 0, 0, 0});
    ASSERT_TRUE(routed.tree);
    auto actual = tree_distances(*routed.tree, fanout);
    for (size_t s = 0; s < fanout; ++s) {
      EXPECT_EQ(actual[s], expected[s + 1]);
    }
    std::vector<size_t> counts(fanout);
    for (auto v : routed.tree->vertices) {
      for (auto sink : v.sinks) {
        ++counts[sink];
      }
    }
    EXPECT_EQ(counts, std::vector<size_t>(fanout, 1));
  }
}
TEST(BufferGeometry, RandomObstacleUnionsAgainstIndependentUnitGrid) {
  std::mt19937 gen(7139);
  for (unsigned trial = 0; trial < 200; ++trial) {
    b::Geometry_request r{
        {0, 0, 10, 10},
        {{int64_t(gen() % 11), int64_t(gen() % 11)}},
        {},
        {},
        int64_t(gen() % 2)
    };
    for (unsigned s = 0; s < 1 + trial % 4; ++s) {
      r.terminals.push_back({int64_t(gen() % 11), int64_t(gen() % 11)});
    }
    for (unsigned o = 0; o < trial % 6; ++o) {
      const int64_t x = gen() % 10, y = gen() % 10;
      r.routing_exclusions.push_back({x, y, x + 1 + int64_t(gen() % 3), y + 1 + int64_t(gen() % 3)});
    }
    auto expected = lattice(r);
    auto built    = b::build_visibility_graph(r);
    ASSERT_TRUE(built.graph);
    auto       routed    = b::shortest_path_tree(*built.graph, 1, {0, 0, 0, 0});
    const bool connected = std::all_of(expected.begin(), expected.end(), [](auto d) { return d >= 0; });
    ASSERT_EQ(routed.status, connected ? b::Route_status::connected : b::Route_status::disconnected) << trial;
    if (connected) {
      ASSERT_TRUE(routed.tree);
      const auto actual = tree_distances(*routed.tree, r.terminals.size() - 1);
      for (size_t s = 0; s < actual.size(); ++s) {
        EXPECT_EQ(actual[s], expected[s + 1]) << trial << " sink " << s;
      }
    } else {
      EXPECT_FALSE(routed.tree);
    }
  }
}
TEST(BufferGeometry, TouchingUnionSeamsCannotManufacturePassage) {
  b::Geometry_request r{
      {0, 0, 10, 10},
      {{1, 5}, {9, 5}},
      {},
      {{4, 0, 6, 5}, {4, 5, 6, 10}},
      0
  };
  const auto graph = b::build_visibility_graph(r).graph;
  ASSERT_TRUE(graph);
  EXPECT_EQ(b::shortest_path_tree(*graph, 1, {0, 0, 0, 0}).status, b::Route_status::disconnected);
  // Placement masks are deliberately not part of the routing API. No routing
  // obstacle means a wire can geometrically span a placement-only exclusion.
  r.routing_exclusions.clear();
  auto       open  = b::build_visibility_graph(r).graph;
  const auto route = b::shortest_path_tree(*open, 1, {0, 0, 0, 0});
  ASSERT_TRUE(route.tree);
  EXPECT_EQ(route.length_dbu, 8);
}
TEST(BufferGeometry, ClearanceBlockedTerminalsAndNarrowGap) {
  b::Geometry_request r{
      {0, 0, 10, 10},
      {{1, 5}, {9, 5}},
      {},
      {{4, 0, 6, 4}, {4, 6, 6, 10}},
      0
  };
  auto graph = b::build_visibility_graph(r).graph;
  ASSERT_TRUE(graph);
  EXPECT_EQ(b::shortest_path_tree(*graph, 1, {0, 0, 0, 0}).status, b::Route_status::connected);
  r.clearance_dbu = 1;
  graph           = b::build_visibility_graph(r).graph;
  ASSERT_TRUE(graph);
  EXPECT_EQ(b::shortest_path_tree(*graph, 1, {0, 0, 0, 0}).status, b::Route_status::disconnected);
  r.terminals[0] = {4, 4};
  graph          = b::build_visibility_graph(r).graph;
  ASSERT_TRUE(graph);
  EXPECT_EQ(graph->terminals[0], std::numeric_limits<size_t>::max());
}
TEST(BufferGeometry, DuplicateCoordinatesRetainEverySinkIdentity) {
  b::Geometry_request r{
      {0, 0, 10, 10},
      {{1, 1}, {1, 1}, {9, 1}, {9, 1}},
      {},
      {},
      0
  };
  auto graph = b::build_visibility_graph(r).graph;
  ASSERT_TRUE(graph);
  auto route = b::shortest_path_tree(*graph, 1, {0, 0, 0, 0});
  ASSERT_TRUE(route.tree);
  EXPECT_EQ(route.tree->vertices[0].sinks, std::vector<size_t>{0});
  EXPECT_EQ(tree_distances(*route.tree, 3), (std::vector<double>{0, 8, 8}));
  EXPECT_EQ(route.length_dbu, 8);
}
TEST(BufferGeometry, CacheNormalizationInvalidationLimitsAndEviction) {
  b::Geometry_cache cache(2, 10000);
  auto              r     = request();
  auto              first = cache.get(r);
  ASSERT_TRUE(first.graph);
  EXPECT_FALSE(first.cache_hit);
  r.routing_exclusions.push_back(r.routing_exclusions[0]);
  r.sites = {
      {1, 5},
      {1, 5}
  };
  auto distinct = cache.get(r);
  EXPECT_FALSE(distinct.cache_hit);
  r.routing_exclusions.pop_back();
  r.sites.pop_back();
  auto normalized = cache.get(r);
  EXPECT_TRUE(normalized.cache_hit);
  EXPECT_EQ(distinct.graph, normalized.graph);
  b::Geometry_limits tiny;
  tiny.max_grid_points = 1;
  auto capped          = cache.get(r, tiny);
  EXPECT_TRUE(capped.cache_hit);
  EXPECT_FALSE(capped.graph);
  EXPECT_EQ(capped.status, b::Geometry_status::budget_exhausted);
  EXPECT_EQ(cache.get(r).graph, normalized.graph);
  r.clearance_dbu = 1;
  EXPECT_FALSE(cache.get(r).cache_hit);
  EXPECT_EQ(cache.size(), 2);
  EXPECT_FALSE(cache.get(request()).cache_hit);  // first entry was least recent
  EXPECT_LE(cache.weight(), 10000);
  b::Geometry_cache no_room(2, 0);
  EXPECT_TRUE(no_room.get(r).graph);
  EXPECT_EQ(no_room.size(), 0);
  b::Geometry_cache disabled(0, 10000);
  EXPECT_TRUE(disabled.get(r).graph);
  EXPECT_EQ(disabled.weight(), 0);
}
TEST(BufferGeometry, ExplicitBudgetsNeverClaimDisconnectionOrCompletePartialGraph) {
  auto               r = request();
  b::Geometry_limits limits;
  limits.max_input_items = 1;
  EXPECT_EQ(b::build_visibility_graph(r, limits).status, b::Geometry_status::budget_exhausted);
  limits                 = {};
  limits.max_grid_points = 1;
  EXPECT_EQ(b::build_visibility_graph(r, limits).status, b::Geometry_status::budget_exhausted);
  limits           = {};
  limits.max_edges = 0;
  auto edges       = b::build_visibility_graph(r, limits);
  EXPECT_EQ(edges.status, b::Geometry_status::budget_exhausted);
  EXPECT_FALSE(edges.graph);
  limits                     = {};
  limits.max_obstacle_checks = 0;
  EXPECT_EQ(b::build_visibility_graph(r, limits).status, b::Geometry_status::budget_exhausted);
  auto graph = b::build_visibility_graph(r).graph;
  ASSERT_TRUE(graph);
  for (unsigned which = 0; which < 3; ++which) {
    b::Route_limits bound;
    if (which == 0) {
      bound.max_relaxations = 0;
    }
    if (which == 1) {
      bound.max_vertices = 0;
    }
    if (which == 2) {
      bound.max_edges = 0;
    }
    auto route = b::shortest_path_tree(*graph, 1, {0, 0, 0, 0}, bound);
    EXPECT_EQ(route.status, b::Route_status::budget_exhausted);
    EXPECT_FALSE(route.tree);
  }
}
TEST(BufferGeometry, InvalidInputUnitsAndCoordinateOverflowStayExplicit) {
  auto r    = request();
  r.core.xh = 0;
  EXPECT_THROW(b::build_visibility_graph(r), std::invalid_argument);
  r              = request();
  r.terminals[0] = {-1, 5};
  EXPECT_THROW(b::build_visibility_graph(r), std::invalid_argument);
  r               = request();
  r.clearance_dbu = -1;
  EXPECT_THROW(b::build_visibility_graph(r), std::invalid_argument);
  r                    = request();
  r.routing_exclusions = {
      {std::numeric_limits<int64_t>::min(), 0, 1, 1}
  };
  r.clearance_dbu = 1;
  EXPECT_THROW(b::build_visibility_graph(r), std::invalid_argument);
  auto graph = b::build_visibility_graph(request()).graph;
  ASSERT_TRUE(graph);
  EXPECT_THROW(b::shortest_path_tree(*graph, 0, {1, 1, 1, 1}), std::invalid_argument);
  EXPECT_THROW(b::shortest_path_tree(*graph, 1, {1, -1, 1, 1}), std::invalid_argument);
  auto broken = *graph;
  broken.edges[0].length_dbu++;
  EXPECT_THROW(b::shortest_path_tree(broken, 1, {1, 1, 1, 1}), std::invalid_argument);
  broken               = *graph;
  broken.vertices[0].x = std::numeric_limits<int64_t>::max();
  EXPECT_THROW(b::shortest_path_tree(broken, 1, {1, 1, 1, 1}), std::invalid_argument);
}
TEST(BufferGeometry, PhysicalUnitsAndExplicitRcScaleConsistently) {
  auto a = b::build_visibility_graph(request()).graph;
  ASSERT_TRUE(a);
  const auto first = b::shortest_path_tree(*a, 1, {2, 3, 5, 7});
  ASSERT_TRUE(first.tree);
  auto scaled = *a;
  for (auto& v : scaled.vertices) {
    v.x *= 1000;
    v.y *= 1000;
  }
  for (auto& e : scaled.edges) {
    e.length_dbu *= 1000;
  }
  const auto second = b::shortest_path_tree(scaled, 1000, {2, 3, 5, 7});
  ASSERT_TRUE(second.tree);
  ASSERT_EQ(first.tree->segments.size(), second.tree->segments.size());
  for (size_t i = 0; i < first.tree->segments.size(); ++i) {
    EXPECT_DOUBLE_EQ(first.tree->segments[i].resistance_ohm, second.tree->segments[i].resistance_ohm);
    EXPECT_DOUBLE_EQ(first.tree->segments[i].capacitance_ff, second.tree->segments[i].capacitance_ff);
    EXPECT_DOUBLE_EQ(first.tree->segments[i].length_um, second.tree->segments[i].length_um);
  }
}
TEST(BufferGeometry, RoutedTopologyFeedsNativeLibraryAndSharedSiteSelection) {
  auto graph = b::build_visibility_graph(request()).graph;
  ASSERT_TRUE(graph);
  auto route = b::shortest_path_tree(*graph, 1, {0, 0, 0, 0});
  ASSERT_TRUE(route.tree);
  m::Library lib;
  cell(lib, "D", 1, 1);
  cell(lib, "B1", 1, 100);
  cell(lib, "B2", 2, 100);
  cell(lib, "S", 4, 100);
  b::Context c;
  c.core   = {0, 0, 20, 10};
  c.driver = {
      "d",
      {"D", "A", "Y"},
      {1, 5},
      {0, 0},
      {1, 1}
  };
  c.sinks = {
      {"s", "S", "A", {19, 5}, {100, 100}, {.5, .5}}
  };
  c.capacity["gap"] = 1;
  for (auto name : std::vector<std::string>{"B1", "B2"}) {
    b::Buffer buffer{
        "b",
        {name, "A", "Y"},
        {1, 5},
        {1, 5},
        {1, 4, 2, 6},
        true,
        {{"gap", 1}}
    };
    route.tree->vertices[0].options.push_back({
        buffer,
        {"r", 1, 1},
        true
    });
  }
  b::Site_reservations    sites(b::build_legal_sites(
      {
          {"r", {0, 4, 20, 6}, 1}
  },
      {}));
  b::Tree_search_settings settings;
  settings.dbu_per_um  = 1;
  settings.max_seconds = 10;
  auto found           = b::search_tree(lib, c, *route.tree, sites, "net", settings);
  EXPECT_EQ(found.status, b::Tree_search_status::complete);
  EXPECT_EQ(found.evaluated, 3);
  ASSERT_TRUE(found.best);
  ASSERT_EQ(found.best->candidate.buffers.size(), 1);
  EXPECT_EQ(found.best->candidate.buffers[0].model.cell, "B1");
  EXPECT_EQ(found.electrical_rejected, 2);  // direct sink C and B2 input C
  EXPECT_TRUE(found.best->timing.feasible());
  EXPECT_TRUE(sites.reservations().empty());
  ASSERT_EQ(sites
                .replace("other",
                         {
                             {"r", 1, 1}
  })
                .status,
            b::Reservation_status::reserved);
  found = b::search_tree(lib, c, *route.tree, sites, "net", settings);
  EXPECT_FALSE(found.best);
  EXPECT_EQ(found.site_rejected, 2);
}

TEST(BufferGeometry, CoincidentTerminalsNeedNoRelaxationsAndStillValidatePhysicalUnits) {
  b::Geometry_request r{
      {0, 0, 10, 10},
      {{1, 1}, {1, 1}},
      {},
      {},
      0
  };
  auto graph = b::build_visibility_graph(r).graph;
  ASSERT_TRUE(graph);
  b::Route_limits limits;
  limits.max_relaxations = 0;
  auto route             = b::shortest_path_tree(*graph, 1, {0, 0, 0, 0}, limits);
  ASSERT_TRUE(route.tree);
  EXPECT_EQ(route.tree->vertices.size(), 1);
  EXPECT_EQ(route.relaxations, 0);
  EXPECT_THROW(b::shortest_path_tree(*graph, 1e-320, {0, 0, 0, 0}, limits), std::invalid_argument);
  // This lattice fits in exact integer doubles, but the physical conversion
  // merges two distinct adjacent points at this scale; do not accept it.
  b::Visibility_graph close;
  const int64_t       x = (int64_t{1} << 53) - 1;
  close.vertices        = {
      {    x, 0},
      {x + 1, 0}
  };
  close.edges = {
      {0, 1, 1}
  };
  close.terminals = {0, 1};
  EXPECT_THROW(b::shortest_path_tree(close, 1000, {0, 0, 0, 0}), std::invalid_argument);
}
