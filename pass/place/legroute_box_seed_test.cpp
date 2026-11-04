// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <cmath>
#include <numeric>
#include <set>

#include "gtest/gtest.h"
#include "legroute_internal.hpp"

namespace livehd::placement::lr {
namespace {
struct Fixture {
  physical::Technology tech;
  physical::Floorplan  fp;
  Legroute_options     options;
  Design               design{tech, fp, options};
  Grid                 grid;
  Boxes                boxes;
  std::vector<int>     cap_h, cap_v;

  Fixture(int cols = 4, int rows = 2) {
    fp.dbu = grid.dbu       = 1;
    options.box_route_seed  = true;
    options.partition_cells = 32;
    options.legal_boxes     = false;
    options.window_tiles    = 1;
    options.sat_ms          = 10000;
    options.global_share    = .95;
    options.global_rounds   = 1;
    options.global_net_pops = 10000;
    grid.cols               = cols;
    grid.rows               = rows;
    grid.regions            = 1;
    grid.site = grid.row_h = 1;
    grid.tile_w = grid.sites = 10;
    grid.tile_h              = 2;
    grid.region.assign(grid.size(), 0);
    grid.key.assign(grid.size(), 0);
    grid.keys = {
        {{0, 0, 10}, {1, 0, 10}},
        { {0, 0, 1},  {1, 0, 1}}
    };
    grid.blocked.assign(grid.size(), 0);
    cap_h.assign(grid.size(), 8);
    cap_v.assign(grid.size(), 8);
  }

  int box(int home, int width = 2, bool fixed = false) {
    const int  id     = int(boxes.boxes.size());
    const auto name   = "c" + std::to_string(id);
    auto&      master = tech.macros[name];
    master.width      = width;
    master.height     = 1;
    master.klass      = "CORE";
    Ann_place pose(grid.left(grid.col(home)), grid.bottom(grid.row(home)), width, 1);
    pose.status = physical::Status::PLACED;
    fp.components.push_back({name, name, pose});
    design.macro.push_back(&master);
    design.cell.push_back(id);
    design.movable.push_back(id);
    design.width.push_back(width);
    design.tall.push_back(false);
    design.flop.push_back(false);
    design.nets_of.emplace_back();
    Box b;
    b.cells  = {id};
    b.tile   = home;
    b.region = grid.region[size_t(home)];
    b.cx     = grid.cx(grid.col(home));
    b.cy     = grid.cy(grid.row(home));
    b.sites  = width;
    b.fixed  = fixed;
    boxes.boxes.push_back(b);
    boxes.box_of.push_back(id);
    return id;
  }

  void net(std::initializer_list<int> ids, int terminal_tile = -1) {
    Net n;
    n.frame  = design.nets.size();
    n.routed = true;
    n.driver = 0;
    for (int id : ids) {
      Pin p;
      p.comp = id;
      n.pins.push_back(p);
      design.nets_of[size_t(id)].push_back(int(n.frame));
    }
    if (terminal_tile >= 0) {
      fp.terminals.emplace_back();
      auto& term = fp.terminals.back();
      term.name  = "io" + std::to_string(fp.terminals.size());
      term.x     = grid.cx(grid.col(terminal_tile));
      term.y     = grid.cy(grid.row(terminal_tile));
      Pin p;
      p.term = int(fp.terminals.size() - 1);
      n.pins.push_back(p);
    }
    design.nets.push_back(std::move(n));
  }

  Box_route_seed_result run() { return prepare_box_route_seed(design, grid, boxes, cap_h, cap_v, "plan"); }

  void unchanged(const Boxes& original, const std::vector<physical::Component>& components) const {
    EXPECT_EQ(fp.components, components);
    ASSERT_EQ(boxes.boxes.size(), original.boxes.size());
    EXPECT_EQ(boxes.box_of, original.box_of);
    for (size_t b = 0; b < original.boxes.size(); ++b) {
      EXPECT_EQ(boxes.boxes[b].cells, original.boxes[b].cells);
      EXPECT_EQ(boxes.boxes[b].tile, original.boxes[b].tile);
      EXPECT_EQ(boxes.boxes[b].cx, original.boxes[b].cx);
      EXPECT_EQ(boxes.boxes[b].cy, original.boxes[b].cy);
      EXPECT_EQ(boxes.boxes[b].fixed, original.boxes[b].fixed);
    }
  }

  void verify(const Box_route_seed_result& result) {
    ASSERT_TRUE(result.accepted) << result.reason;
    ASSERT_EQ(result.matched, boxes.boxes.size());
    std::vector<int> B(boxes.boxes.size()), T(grid.size());
    std::iota(B.begin(), B.end(), 0);
    std::iota(T.begin(), T.end(), 0);
    const std::unordered_map<int, std::vector<int>> allowed;
    const std::vector<char>                         pinned(grid.size());
    const auto                                      check = [] {};
    const auto    domains = prepare_box_domains(design, options, grid, 0, B, T, boxes, allowed, pinned, check);
    std::set<int> occupied;
    for (size_t b = 0; b < boxes.boxes.size(); ++b) {
      const auto choices
          = box_domain_tiles(design, grid, int(b), domains.windows[b], boxes, allowed, domains.lifted, pinned, T, check);
      const auto& placed = result.boxes.boxes[b];
      EXPECT_NE(std::ranges::find(choices, placed.tile), choices.end());
      EXPECT_TRUE(box_fits(design, grid, boxes.boxes[b], placed.tile));
      EXPECT_TRUE(occupied.insert(placed.tile).second);
      if (boxes.boxes[b].fixed) {
        EXPECT_EQ(placed.tile, boxes.boxes[b].tile);
      }
    }
    ASSERT_EQ(result.routes.trees.size(), design.nets.size());
    std::map<std::pair<int, int>, std::set<int>> users;
    for (const auto& request : all_signal_requests(design, grid, result.boxes)) {
      const auto& tree = result.routes.trees[size_t(request.net)];
      ASSERT_TRUE(tree.routed);
      std::map<int, std::set<int>>  adjacency;
      std::set<std::pair<int, int>> unique;
      for (const auto& [a, b] : tree.edges) {
        ASSERT_GE(a, 0);
        ASSERT_LT(a, b);
        ASSERT_LT(size_t(b), grid.size());
        EXPECT_EQ(std::abs(grid.col(a) - grid.col(b)) + std::abs(grid.row(a) - grid.row(b)), 1);
        EXPECT_TRUE(unique.emplace(a, b).second);
        adjacency[a].insert(b);
        adjacency[b].insert(a);
        users[{a, b}].insert(request.net);
      }
      ASSERT_FALSE(request.endpoints.empty());
      std::set<int>    reached{request.endpoints.front()};
      std::vector<int> queue{request.endpoints.front()};
      for (size_t at = 0; at < queue.size(); ++at) {
        for (int t : adjacency[queue[at]]) {
          if (reached.insert(t).second) {
            queue.push_back(t);
          }
        }
      }
      for (int endpoint : request.endpoints) {
        EXPECT_TRUE(reached.contains(endpoint));
      }
    }
    for (const auto& [edge, nets] : users) {
      const auto [a, b]    = edge;
      const auto& capacity = grid.row(a) == grid.row(b) ? cap_h : cap_v;
      const int   admitted
          = int(std::floor(std::min(.95, options.global_share) * std::min(capacity[size_t(a)], capacity[size_t(b)])));
      EXPECT_LE(int(nets.size()), admitted);
    }
  }
};
}  // namespace

TEST(LegrouteBoxSeed, ExactNativeDomainsGiveACompleteRouteWithoutChangingThePhysicalFrame) {
  Fixture f;
  f.grid.key[1]     = 1;  // too narrow for any box, despite having free sites
  f.grid.blocked[2] = 1;
  f.box(0);
  f.box(0);
  f.box(7, 2, true);
  f.net({0, 1, 2}, 3);
  f.net({0, 2});
  const auto original   = f.boxes;
  const auto components = f.fp.components;
  const auto terminals  = f.fp.terminals;
  const auto result     = f.run();
  f.verify(result);
  EXPECT_GT(result.moved, 0u);
  f.unchanged(original, components);
  EXPECT_EQ(f.fp.terminals, terminals);
}

TEST(LegrouteBoxSeed, DisabledAndExpiredProposalsDoNotChangeTheOriginalState) {
  for (bool expired : {false, true}) {
    Fixture f;
    f.box(0);
    f.box(0);
    f.net({0, 1});
    f.options.box_route_seed = expired;
    f.options.sat_ms         = 0;
    const auto original      = f.boxes;
    const auto components    = f.fp.components;
    const auto result        = f.run();
    EXPECT_FALSE(result.accepted);
    EXPECT_EQ(result.reason, expired ? "matching_budget" : "disabled");
    EXPECT_TRUE(result.routes.trees.empty());
    f.unchanged(original, components);
  }
}

TEST(LegrouteBoxSeed, IncompleteMatchingAndCollidingFixedBoxesAreNotAccepted) {
  for (bool fixed : {false, true}) {
    Fixture f(fixed ? 4 : 1, 1);
    f.box(0, 2, fixed);
    f.box(0, 2, fixed);
    f.net({0, 1});
    const auto original   = f.boxes;
    const auto components = f.fp.components;
    const auto result     = f.run();
    EXPECT_FALSE(result.accepted);
    f.unchanged(original, components);
  }
}

TEST(LegrouteBoxSeed, RawCapacityIsNotPermissionToExceedTheNinetyFivePercentAdmissionLimit) {
  for (int capacity : {2, 3}) {
    Fixture f(4, 1);
    f.box(0, 2, true);
    f.box(3, 2, true);
    f.net({0, 1});
    f.net({0, 1});  // two distinct nets must each consume the bottleneck
    f.cap_h.assign(f.grid.size(), capacity);
    f.cap_v.assign(f.grid.size(), capacity);
    const auto original   = f.boxes;
    const auto components = f.fp.components;
    const auto result     = f.run();
    if (capacity == 3) {
      f.verify(result);
    } else {
      EXPECT_FALSE(result.accepted);
      EXPECT_GT(result.routes.unrouted + result.routes.overflow_tracks, 0u);
    }
    f.unchanged(original, components);
  }
}

TEST(LegrouteBoxSeed, SearchLimitedCandidateDoesNotBecomeACompleteSeed) {
  Fixture f(12, 8);
  f.box(0, 2, true);
  f.box(int(f.grid.size()) - 1, 2, true);
  f.net({0, 1});
  f.options.global_net_pops = 1;
  const auto original       = f.boxes;
  const auto components     = f.fp.components;
  const auto result         = f.run();
  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.matched, 2u);
  EXPECT_GT(result.routes.unrouted, 0u);
  EXPECT_GT(result.routes.search_limited, 0u);
  f.unchanged(original, components);
}

TEST(LegrouteBoxSeed, CandidateCannotSilentlyCrossAProtectedCriticalRegionCut) {
  Fixture f(4, 1);
  f.grid.regions = 2;
  f.grid.region  = {0, 0, 1, 1};
  f.box(0, 2, true);
  f.box(3, 2, true);
  f.net({0, 1});
  f.design.nets[0].rank             = 0;
  f.options.partition_critical      = 1;
  f.options.partition_critical_cost = -1;
  const auto original               = f.boxes;
  const auto components             = f.fp.components;
  const auto result                 = f.run();
  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.routes.unrouted, 0u);  // a complete forest can still violate cut policy
  EXPECT_EQ(result.reason, "candidate_admission_failed");
  f.unchanged(original, components);
}
}  // namespace livehd::placement::lr
