// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "row_solve.hpp"

#include <algorithm>
#include <random>

#include "gtest/gtest.h"
#include "inou/lefdef/routing_connectivity.hpp"

namespace livehd::routefirst {
namespace ph = physical;
namespace {
std::vector<int> sites(int lo, int hi, int step = 1) {
  std::vector<int> r;
  for (int x = lo; x <= hi; x += step) {
    r.push_back(x);
  }
  return r;
}
Row_solution enumerate(const std::vector<Row_variable>& vars, const std::vector<Row_constraint>& rules) {
  Row_solution     best;
  std::vector<int> x;
  const auto       visit = [&](auto&& self, int64_t cost) -> void {
    if (x.size() == vars.size()) {
      for (const auto& c : rules) {
        if (int64_t(x[c.to]) - x[c.from] < c.gap) {
          return;
        }
      }
      if (!best.feasible || cost < best.cost) {
        best = {true, x, cost, {}};
      }
      return;
    }
    const auto& v = vars[x.size()];
    for (int position : v.sites) {
      x.push_back(position);
      self(self, cost + int64_t(position) * v.cost);
      x.pop_back();
    }
  };
  visit(visit, 0);
  return best;
}

TEST(RouteFirstRow, WeightedCostAndFeasibilityMatchExhaustiveEnumeration) {
  std::mt19937 rng(8713);
  for (int trial = 0; trial < 400; ++trial) {
    std::vector<Row_variable> vars;
    for (int i = 0; i < 4; ++i) {
      std::vector<int> domain;
      for (int x = -2; x < 4; ++x) {
        if (rng() % 3) {
          domain.push_back(x);
        }
      }
      if (domain.empty()) {
        domain.push_back(0);
      }
      vars.push_back({std::to_string(i), domain, int(rng() % 7) - 3});
    }
    std::vector<Row_constraint> rules;
    for (int r = 0; r < 5; ++r) {
      rules.push_back({rng() % 4, rng() % 4, int(rng() % 7) - 3, "r" + std::to_string(r)});
    }
    const auto exact = solve_row(vars, rules), exhaustive = enumerate(vars, rules);
    ASSERT_EQ(exact.feasible, exhaustive.feasible) << trial;
    if (exact.feasible) {
      EXPECT_EQ(exact.cost, exhaustive.cost);
      for (const auto& c : rules) {
        EXPECT_GE(exact.x[c.to] - exact.x[c.from], c.gap);
      }
    } else {
      EXPECT_FALSE(exact.conflict.empty());
    }
  }
}

TEST(RouteFirstRow, FixedFlopAndAlternativeAttachmentGiveACertificate) {
  // The facing stubs require three sites. Only two sites fit before the flop.
  std::vector<Row_variable> vars = {
      {"left", {0}, 0},
      {"gate", sites(0, 2), 1},
      {"fixed flop", {4}, 0}
  };
  const std::vector<Row_constraint> rules = {
      {0, 1, 3, "tip spacing"},
      {1, 2, 2,  "fixed flop"}
  };
  auto fail = solve_row(vars, rules);
  EXPECT_FALSE(fail.feasible);
  EXPECT_NE(std::find(fail.conflict.begin(), fail.conflict.end(), "tip spacing"), fail.conflict.end());
  auto repaired     = rules;
  repaired[0]       = {0, 1, 2, "different M2 track"};
  const auto solved = solve_row(vars, repaired);
  ASSERT_TRUE(solved.feasible);
  EXPECT_EQ(solved.x[1], 2);
}

TEST(RouteFirstRow, MixedAttachmentSlidesTogetherAcrossRows) {
  std::vector<Row_variable> vars = {
      {  "row0", {0, 1, 2, 3, 4}, -2},
      {  "row1",       {0, 2, 4},  1},
      {"anchor",             {3},  0}
  };
  std::vector<Row_constraint> rules = {
      {0, 1, 0,  "aligned M3"},
      {1, 0, 0,  "aligned M3"},
      {0, 2, 0, "stretch cap"}
  };
  const auto solution = solve_row(vars, rules);
  ASSERT_TRUE(solution.feasible);
  EXPECT_EQ(solution.x, (std::vector<int>{2, 2, 3}));
  vars[0].sites = {1, 3};
  EXPECT_FALSE(solve_row(vars, rules).feasible);
}

TEST(RouteFirstRow, MinimumLengthPieceCanSlideBetweenDifferentNetTips) {
  // X variables for cell, M2 left and right ends, and the two fixed endpoints.
  const std::vector<Row_variable> vars = {
      {"cell", {10}, 0},
      {"left end", sites(4, 10), -1},
      {"right end", sites(10, 16), 1},
      {"left obstacle", {3}, 0},
      {"right obstacle", {15}, 0}
  };
  std::vector<Row_constraint> rules = {
      {1, 0, 0,  "via left enclosure"},
      {0, 2, 0, "via right enclosure"},
      {1, 2, 6,          "min length"},
      {3, 1, 2,            "left tip"},
      {2, 4, 2,           "right tip"}
  };
  auto layout = solve_row(vars, rules);
  ASSERT_TRUE(layout.feasible);
  EXPECT_EQ(layout.cost, 6);
  rules.back().gap = 7;
  EXPECT_FALSE(solve_row(vars, rules).feasible);
}

TEST(RouteFirstRow, JitteredTwoAndThreePinNetsRecoverConnectedWitness) {
  // The checker fixture uses 1,000 DBU/um, M1 terminals and an explicit V1 cut.
  ph::Technology tech;
  ph::Floorplan  frame;
  frame.dbu                   = 1000;
  tech.layers["M1"].width     = .02;
  tech.layers["M2"].width     = .02;
  tech.cut_layers["V1"].width = .01;
  tech.vias["via"].shapes     = {
      {"M1",     {-.01, -.01, .01, .01}},
      {"V1", {-.005, -.005, .005, .005}},
      {"M2",     {-.01, -.01, .01, .01}}
  };
  auto& gate            = tech.macros["gate"];
  gate.width            = .10;
  gate.height           = .25;
  gate.pins["A"].shapes = {
      {"M1", {.04, .04, .06, .06}}
  };
  gate.pins["Y"].shapes = {
      {"M1", {.04, .14, .06, .16}}
  };
  tech.macros["flop"]                    = gate;
  tech.macros["flop"].pins["CLK"].shapes = {
      {"M1", {.08, .09, .10, .11}}
  };
  for (const auto& [name, x] : std::vector<std::pair<std::string, int>>{
           {"a", 100},
           {"b", 100},
           {"q", 500}
  }) {
    Ann_place pose(double(x), 0., 100., 250.);
    pose.status = name == "q" ? ph::Status::FIXED : ph::Status::PLACED;
    frame.components.push_back({name, name == "q" ? "flop" : "gate", pose});
  }
  const std::vector<Row_variable> vars = {
      {"a", sites(1, 4), -2},
      {"b", sites(2, 8), 1},
      {"flop", {10}, 0}
  };
  const std::vector<Row_constraint> rules = {
      {0, 1, 2,    "cell order"},
      {1, 2, 2, "flop boundary"}
  };
  auto layout = solve_row(vars, rules);
  ASSERT_TRUE(layout.feasible);
  EXPECT_EQ(layout.cost, enumerate(vars, rules).cost);
  EXPECT_EQ(layout.x, (std::vector<int>{4, 6, 10}));
  for (size_t i = 0; i < 2; ++i) {
    frame.components[i].place.x = layout.x[i] * 50;
  }
  const auto connect = [&](std::string name, std::string pin, int y, unsigned count) {
    ph::Net net;
    net.name = name;
    ph::Wire wire;
    for (unsigned i = 0; i < count; ++i) {
      const auto& cell = frame.components[i];
      auto        x    = int(cell.place.x) + 50;
      net.connections.push_back({cell.name, pin});
      wire.paths.push_back({
          "M1",
          {},
          {},
          {ph::Route_point{x, y, {}}, ph::Route_via{"via"}}
      });
      // A shared three-pin branch has a junction at the middle tap.
      if (i) {
        int previous = int(frame.components[i - 1].place.x) + 50;
        wire.paths.push_back({
            "M2",
            {},
            {},
            {ph::Route_point{previous, y, 0}, ph::Route_point{x, y, 0}}
        });
      }
    }
    net.wires.push_back(std::move(wire));
    return net;
  };
  auto local = connect("two", "A", 50, 2), branch = connect("three", "Y", 150, 3);
  EXPECT_TRUE(ph::check_route_connectivity(tech, frame, local).connected);
  EXPECT_TRUE(ph::check_route_connectivity(tech, frame, branch).connected);
  branch.wires.front().paths.pop_back();
  EXPECT_FALSE(ph::check_route_connectivity(tech, frame, branch).connected);
}
}  // namespace
}  // namespace livehd::routefirst
