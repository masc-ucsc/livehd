// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "tree_search.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <set>
#include <stdexcept>

#include "buffer_cell_fixture.hpp"
#include "gtest/gtest.h"

namespace b = livehd::placement::buffering;
namespace m = livehd::liberty::timing;
namespace {
m::Table table(double base, double slew, double cap) {
  return {
      m::Variable::input_slew,
      m::Variable::output_load,
      {0, 128},
      {0, 128},
      {base, base + 128 * cap, base + 128 * slew, base + 128 * (slew + cap)}
  };
}
struct Oracle {
  std::array<double, 2>              required{}, root_slew{};
  std::vector<std::array<double, 2>> arrival, slew;
  double                             margin = 0, cost = 0, area = 0;
  std::array<bool, 100>              occupied{};
};
struct Fixture {
  m::Library              lib;
  b::Context              c;
  b::Routing_tree         route;
  b::Tree_search_settings settings;
  b::Site_reservations    sites{
      b::build_legal_sites({{"r", {0, 0, 100, 1}, 1}},
      {                       }
      )
  };
  explicit Fixture(size_t n = 1) {
    const auto add_cell = [&](std::string name, double cin, double d, double ds, double dc, double s, double ss, double sc) {
      lib.cells.emplace(name,
                        b::test_fixture::buffer_cell(name,
                                                     {cin, cin + 1},
                                                     100,
                                                     {table(d, ds, dc), table(d + 1, ds, dc)},
                                                     {table(s, ss, sc), table(s + 1, ss, sc)}));
    };
    add_cell("D", 1, 5, .5, 1, 2, .25, 2);
    add_cell("B1", 1, 2, .25, .25, 1, .125, .5);
    add_cell("B2", 2, 1, .125, .125, .5, .0625, .25);
    add_cell("S", 4, 0, 0, 0, 0, 0, 0);
    c.driver = {
        "d",
        {"D", "A", "Y"},
        {0, .5},
        {10, 20},
        {3, 7}
    };
    c.core                    = {0, 0, 100, 1};
    c.capacity["gap"]         = 100;
    c.normalization_period_ps = 128;
    route.vertices.push_back({c.driver.output, {}, {}});
    for (size_t i = 0; i < n; ++i) {
      const double x = 10 + 10 * double(i);
      c.sinks.push_back({
          "s" + std::to_string(i),
          "S",
          "A",
          {80 + 3 * double(i),              .5},
          {               100,             120},
          {   i == 0 ? .5 : 0, i == 0 ? .5 : 0}
      });
      b::Tree_vertex cut{
          {x, .5},
          {},
          {}
      };
      for (size_t type = 1; type <= 2; ++type) {
        b::Buffer buffer{
            "b" + std::to_string(i) + "_" + std::to_string(type),
            {"B" + std::to_string(type), "A", "Y"},
            {x, .5},
            {x, .5},
            {x - double(type), 0, x, 1},
            true,
            {{"gap", type}}
        };
        cut.options.push_back({
            buffer,
            {"r", uint64_t(x) - type, type},
            true
        });
      }
      const auto vertex = route.vertices.size();
      route.vertices.push_back(cut);
      route.vertices.push_back({c.sinks.back().position, {i}, {}});
      route.segments.push_back({0, vertex, 0, 0, x, true});
      route.segments.push_back({vertex, vertex + 1, 0, 0, c.sinks.back().position.x - x, true});
    }
    settings.dbu_per_um  = 1;
    settings.max_seconds = 10;
  }
  b::Tree_search_result search() const { return b::search_tree(lib, c, route, sites, "net", settings); }
  // Independent stage equations for this ideal-wire star; no producer or
  // waveform library is called. Each branch has one of two real cell types.
  Oracle                oracle(const std::vector<size_t>& choices) const {
    Oracle out;
    out.arrival.resize(c.sinks.size());
    out.slew.resize(c.sinks.size());
    out.margin = std::numeric_limits<double>::infinity();
    for (unsigned t = 0; t < 2; ++t) {
      double load = 0;
      for (size_t i = 0; i < c.sinks.size(); ++i) {
        load += choices[1 + 2 * i] ? double(choices[1 + 2 * i]) + t : 4 + t;
      }
      const double delay = 5 + t + .5 * c.driver.input_slew_ps[t] + load;
      out.root_slew[t]   = 2 + t + .25 * c.driver.input_slew_ps[t] + 2 * load;
      out.required[t]    = std::numeric_limits<double>::infinity();
      for (size_t i = 0; i < c.sinks.size(); ++i) {
        const auto   type   = choices[1 + 2 * i];
        const double extra  = type == 1   ? 2 + t + .25 * out.root_slew[t] + .25 * (4 + t)
                                             : type == 2 ? 1 + t + .125 * out.root_slew[t] + .125 * (4 + t)
                                                         : 0;
        const double slew   = type == 1   ? 1 + t + .125 * out.root_slew[t] + .5 * (4 + t)
                                             : type == 2 ? .5 + t + .0625 * out.root_slew[t] + .25 * (4 + t)
                                                         : out.root_slew[t];
        out.arrival[i][t]   = c.driver.input_arrival_ps[t] + delay + extra;
        out.slew[i][t]      = slew;
        out.required[t]     = std::min(out.required[t], *c.sinks[i].required_ps[t] - delay - extra);
        out.cost           += c.sinks[i].multiplier[t] * (delay + extra) / c.normalization_period_ps;
        if (t == 0) {
          out.area += double(type);
          if (type) {
            const auto& span = route.vertices[1 + 2 * i].options[type - 1].site;
            for (auto at = span.first; at < span.first + span.count; ++at) {
              out.occupied.at(at) = true;
            }
          }
        }
      }
      out.margin = std::min(out.margin, out.required[t] - c.driver.input_arrival_ps[t]);
    }
    return out;
  }
};
bool oracle_dominates(const Oracle& a, const Oracle& other) {
  if (a.area > other.area || a.cost > other.cost) {
    return false;
  }
  for (unsigned t = 0; t < 2; ++t) {
    if (a.required[t] < other.required[t]) {
      return false;
    }
    for (size_t i = 0; i < a.arrival.size(); ++i) {
      if (a.arrival[i][t] > other.arrival[i][t] || a.slew[i][t] > other.slew[i][t]) {
        return false;
      }
    }
  }
  for (size_t at = 0; at < a.occupied.size(); ++at) {
    if (a.occupied[at] && !other.occupied[at]) {
      return false;
    }
  }
  return true;
}
void close(const b::Tree_solution& actual, const Oracle& expected, const b::Context& context) {
  ASSERT_TRUE(actual.worst_input_margin_ps);
  EXPECT_NEAR(*actual.worst_input_margin_ps, expected.margin, 1e-8);
  EXPECT_NEAR(actual.timing.multiplier_cost, expected.cost, 1e-10);
  EXPECT_NEAR(actual.timing.area_um2, expected.area, 1e-12);
  for (unsigned t = 0; t < 2; ++t) {
    ASSERT_TRUE(actual.timing.root_required_ps[t]);
    EXPECT_NEAR(*actual.timing.root_required_ps[t], expected.required[t], 1e-8);
    for (size_t i = 0; i < context.sinks.size(); ++i) {
      const auto& pin = actual.timing.pins.at(context.sinks[i].name);
      EXPECT_NEAR(pin.arrival_ps[t], expected.arrival[i][t], 1e-8);
      EXPECT_NEAR(pin.slew_ps[t], expected.slew[i][t], 1e-8);
    }
  }
}
}  // namespace

TEST(BufferTreeSearch, AllFanoutOneThroughFourChoicesMatchIndependentStageEquations) {
  size_t total = 0;
  for (size_t n = 1; n <= 4; ++n) {
    Fixture    f(n);
    auto       result  = f.search();
    const auto count   = size_t(std::pow(3, n));
    total             += count;
    ASSERT_EQ(result.status, b::Tree_search_status::complete);
    ASSERT_TRUE(result.assignment_space_exhausted);
    EXPECT_EQ(result.assignments, count);
    EXPECT_EQ(result.evaluated, count);
    ASSERT_TRUE(result.best);
    ASSERT_FALSE(result.frontier.empty());
    std::vector<size_t>                                 choices(f.route.vertices.size());
    std::vector<std::pair<std::vector<size_t>, Oracle>> alternatives;
    double                                              best = -std::numeric_limits<double>::infinity();
    std::function<void(size_t)>                         walk = [&](size_t i) {
      if (i < n) {
        for (size_t type = 0; type <= 2; ++type) {
          choices[1 + 2 * i] = type;
          walk(i + 1);
        }
        return;
      }
      best = std::max(best, f.oracle(choices).margin);
      alternatives.emplace_back(choices, f.oracle(choices));
    };
    walk(0);
    EXPECT_NEAR(*result.best->worst_input_margin_ps, best, 1e-8);
    close(*result.best, f.oracle(result.best->choices), f.c);
    for (const auto& solution : result.frontier) {
      close(solution, f.oracle(solution.choices), f.c);
    }
    std::set<std::vector<size_t>> expected_frontier, actual_frontier;
    for (size_t i = 0; i < alternatives.size(); ++i) {
      bool dominated = false;
      for (size_t j = 0; j < alternatives.size(); ++j) {
        if (i == j) {
          continue;
        }
        if (oracle_dominates(alternatives[j].second, alternatives[i].second)
            && (!oracle_dominates(alternatives[i].second, alternatives[j].second) || j < i)) {
          dominated = true;
        }
      }
      if (!dominated) {
        expected_frontier.insert(alternatives[i].first);
      }
    }
    for (const auto& solution : result.frontier) {
      actual_frontier.insert(solution.choices);
    }
    EXPECT_EQ(actual_frontier, expected_frontier);
    if (n == 1) {
      EXPECT_EQ(result.frontier.size(), 3);  // Required/area/slew tradeoffs: never keep just two.
    }
    EXPECT_TRUE(f.sites.reservations().empty());
  }
  EXPECT_EQ(total, 120);
}

TEST(BufferTreeSearch, ShortNetCannotOverrideAnActualCapacitanceViolation) {
  Fixture f;
  f.lib.cells.at("D").pins.at("Y").max_capacitance_ff = 3;
  f.settings.prefer_unbuffered_if_feasible            = true;
  // A very short geometry with the same nonzero physical pin load.
  for (auto& vertex : f.route.vertices) {
    vertex.position.x /= 1000;
    vertex.position.y /= 1000;
    for (auto& option : vertex.options) {
      auto& p     = option.buffer;
      p.input.x  /= 1000;
      p.input.y  /= 1000;
      p.output.x /= 1000;
      p.output.y /= 1000;
      p.body.xl  /= 1000;
      p.body.xh  /= 1000;
      p.body.yl  /= 1000;
      p.body.yh  /= 1000;
    }
  }
  for (auto& e : f.route.segments) {
    e.length_um /= 1000;
  }
  f.c.driver.output     = f.route.vertices[0].position;
  f.c.sinks[0].position = f.route.vertices.back().position;
  f.c.core              = {0, 0, .1, .001};
  f.settings.dbu_per_um = 1000;
  auto result           = f.search();
  ASSERT_EQ(result.status, b::Tree_search_status::complete);
  ASSERT_TRUE(result.best);
  EXPECT_EQ(result.electrical_rejected, 1);
  EXPECT_EQ(result.assignments, 3);
  EXPECT_EQ(result.best->candidate.buffers.size(), 1);
  Fixture feasible;
  feasible.settings.prefer_unbuffered_if_feasible = true;
  result                                          = feasible.search();
  ASSERT_TRUE(result.best);
  EXPECT_EQ(result.status, b::Tree_search_status::unbuffered_retained);
  EXPECT_EQ(result.assignments, 1);
  EXPECT_FALSE(result.assignment_space_exhausted);
  EXPECT_TRUE(result.best->candidate.buffers.empty());
}

TEST(BufferTreeSearch, OutsideWindowSelectsAreaOnlyAfterPhysicalRequirementsPass) {
  Fixture f(4);
  f.settings.selection = b::Tree_selection::outside_window;
  for (auto& sink : f.c.sinks) {
    sink.required_ps = {30, 50};
  }
  const auto result = f.search();
  ASSERT_EQ(result.status, b::Tree_search_status::complete);
  ASSERT_TRUE(result.best);
  EXPECT_GT(result.required_rejected, 0);
  ASSERT_TRUE(result.best->timing.meets_required.value());
  std::vector<size_t>         choice(f.route.vertices.size());
  double                      area = 100;
  std::function<void(size_t)> walk = [&](size_t i) {
    if (i < 4) {
      for (size_t type = 0; type <= 2; ++type) {
        choice[1 + 2 * i] = type;
        walk(i + 1);
      }
      return;
    }
    const auto candidate = f.oracle(choice);
    if (candidate.margin >= 0) {
      area = std::min(area, candidate.area);
    }
  };
  walk(0);
  EXPECT_EQ(result.best->timing.area_um2, area);
  EXPECT_GT(area, 0);
  close(*result.best, f.oracle(result.best->choices), f.c);
}

TEST(BufferTreeSearch, SharedSitesRespectCompetingOwnersAndAllowOwnReplacementWithoutCommit) {
  Fixture f;
  ASSERT_EQ(f.sites
                .replace("other",
                         {
                             {"r", 8, 1}
  })
                .status,
            b::Reservation_status::reserved);
  ASSERT_EQ(f.sites
                .replace("net",
                         {
                             {"r", 9, 1}
  })
                .status,
            b::Reservation_status::reserved);
  const auto before = f.sites.reservations();
  auto       result = f.search();
  EXPECT_EQ(result.status, b::Tree_search_status::complete);
  EXPECT_EQ(result.site_rejected, 1);
  ASSERT_TRUE(result.best);
  for (const auto& solution : result.frontier) {
    EXPECT_NE(solution.choices[1], 2);
  }
  EXPECT_EQ(f.sites.reservations(), before);
  f.sites.release("other");
  result = f.search();
  EXPECT_EQ(result.site_rejected, 0);
  EXPECT_EQ(result.frontier.size(), 3);
  EXPECT_EQ(f.sites.reservations().size(), 1);
}

TEST(BufferTreeSearch, EqualTimingUsesTheSmallerReservationInsteadOfTheEarlierLargerOne) {
  Fixture f;
  f.lib.cells.at("D").pins.at("Y").max_capacitance_ff = 3;
  auto& options                                       = f.route.vertices[1].options;
  options[0].site                                     = {"r", 8, 2};
  options[1].buffer                                   = options[0].buffer;
  options[1].site                                     = {"r", 9, 1};
  const auto result                                   = f.search();
  ASSERT_TRUE(result.best);
  ASSERT_EQ(result.frontier.size(), 1);
  EXPECT_EQ(result.frontier[0].choices[1], 2);
  EXPECT_EQ(result.best->choices[1], 2);
}

TEST(BufferTreeSearch, HighFanoutIsolationKeepsTheCriticalSinkDirectAndChecksEveryOtherLoad) {
  Fixture f(4);
  auto    cut    = f.route.vertices[1];
  cut.position.x = 20;
  for (auto& option : cut.options) {
    option.buffer.input.x  += 10;
    option.buffer.output.x += 10;
    option.buffer.body.xl  += 10;
    option.buffer.body.xh  += 10;
    option.site.first      += 10;
  }
  f.c.sinks[0].required_ps = {50, 60};
  for (size_t i = 1; i < 4; ++i) {
    f.c.sinks[i].required_ps = {1000, 1000};
  }
  f.route.vertices = {
      b::Tree_vertex{    f.c.driver.output,  {}, {}},
      b::Tree_vertex{f.c.sinks[0].position, {0}, {}},
      cut
  };
  f.route.segments = {
      {0, 1, 0, 0, 80, true},
      {0, 2, 0, 0, 20, true}
  };
  for (size_t i = 1; i < 4; ++i) {
    const auto vertex = f.route.vertices.size();
    f.route.vertices.push_back({f.c.sinks[i].position, {i}, {}});
    f.route.segments.push_back({2, vertex, 0, 0, f.c.sinks[i].position.x - 20, true});
  }
  auto result = f.search();
  ASSERT_TRUE(result.best);
  EXPECT_EQ(result.best->choices[2], 1);
  const auto& e = result.best->timing;
  for (unsigned t = 0; t < 2; ++t) {
    const double delay = 5 + t + .5 * f.c.driver.input_slew_ps[t] + 5 + 2 * t;
    const double slew  = 2 + t + .25 * f.c.driver.input_slew_ps[t] + 2 * (5 + 2 * t);
    EXPECT_NEAR(e.pins.at("s0").arrival_ps[t], f.c.driver.input_arrival_ps[t] + delay, 1e-8);
    const double other_delay = 2 + t + .25 * slew + .25 * (3 * (4 + t));
    for (size_t i = 1; i < 4; ++i) {
      EXPECT_NEAR(e.pins.at("s" + std::to_string(i)).arrival_ps[t], f.c.driver.input_arrival_ps[t] + delay + other_delay, 1e-8);
    }
  }
  // An isolation buffer must drive all three off-window pin loads, including
  // the fall capacitances. It cannot borrow a zero-load timing boundary.
  f.lib.cells.at("B1").pins.at("Y").max_capacitance_ff = 10;
  result                                               = f.search();
  ASSERT_TRUE(result.best);
  EXPECT_EQ(result.best->choices[2], 2);
  EXPECT_EQ(result.electrical_rejected, 1);
}

TEST(BufferTreeSearch, BudgetExhaustionRetainsFeasibleEvidenceWithoutAnOptimumClaim) {
  Fixture f;
  f.settings.max_assignments = 1;
  auto result                = f.search();
  EXPECT_EQ(result.status, b::Tree_search_status::budget_exhausted);
  EXPECT_FALSE(result.assignment_space_exhausted);
  EXPECT_EQ(result.assignments, 1);
  ASSERT_TRUE(result.best);
  EXPECT_TRUE(result.best->candidate.buffers.empty());
  f.settings.max_assignments = 0;
  result                     = f.search();
  EXPECT_EQ(result.assignments, 0);
  EXPECT_FALSE(result.best);
  f.settings.max_assignments = 100;
  f.settings.max_frontier    = 1;
  result                     = f.search();
  EXPECT_EQ(result.status, b::Tree_search_status::budget_exhausted);
  EXPECT_EQ(result.frontier.size(), 1);
  ASSERT_TRUE(result.best);
  f.settings.max_frontier      = 100;
  f.settings.max_waveform_fits = 2;
  result                       = f.search();
  EXPECT_EQ(result.status, b::Tree_search_status::budget_exhausted);
  EXPECT_LE(result.waveform_fits, 2);
  ASSERT_TRUE(result.best);
  f.settings.max_topology_vertices = 0;
  result                           = f.search();
  EXPECT_EQ(result.assignments, 0);
  EXPECT_FALSE(result.best);
}

TEST(BufferTreeSearch, UnsupportedModelsRemainDistinctFromProvenFiniteSpaceInfeasibility) {
  Fixture f;
  f.lib.cells.erase("B2");
  auto result = f.search();
  EXPECT_EQ(result.status, b::Tree_search_status::incomplete_models);
  EXPECT_TRUE(result.assignment_space_exhausted);
  EXPECT_EQ(result.unsupported, 1);
  ASSERT_TRUE(result.best);
  EXPECT_FALSE(result.unsupported_examples.empty());
  f.lib.cells.erase("D");
  result = f.search();
  EXPECT_EQ(result.unsupported, 3);
  EXPECT_FALSE(result.best);
  EXPECT_EQ(result.status, b::Tree_search_status::incomplete_models);
  Fixture blocked;
  blocked.route.segments[0].routing_allowed = false;
  result                                    = blocked.search();
  EXPECT_EQ(result.status, b::Tree_search_status::complete);
  EXPECT_TRUE(result.assignment_space_exhausted);
  EXPECT_EQ(result.resource_rejected, 3);
  EXPECT_FALSE(result.best);
  Fixture zero;
  zero.c.capacity["gap"] = 0;
  result                 = zero.search();
  EXPECT_EQ(result.resource_rejected, 2);
  ASSERT_TRUE(result.best);
  EXPECT_TRUE(result.best->candidate.buffers.empty());
}

TEST(BufferTreeSearch, ExpansionPreservesWireCapAndAddsDirectionalPhysicalPinAttachments) {
  Fixture f;
  f.route.attachment_rc              = {2, 3, 5, 7};
  f.route.segments[0].resistance_ohm = 11;
  f.route.segments[0].capacitance_ff = 13;
  f.route.segments[1].resistance_ohm = 17;
  f.route.segments[1].capacitance_ff = 19;
  auto& buffer                       = f.route.vertices[1].options[0].buffer;
  buffer.body                        = {8, 0, 12, 2};
  buffer.input                       = {9, 1};
  buffer.output                      = {11, 1.5};
  auto candidate                     = b::expand_tree(f.c, f.route, {0, 1, 0});
  ASSERT_EQ(candidate.buffers.size(), 1);
  ASSERT_EQ(candidate.trees.size(), 2);
  double cap = 0, length = 0, resistance = 0;
  for (const auto& tree : candidate.trees) {
    livehd::physical::validate_rc_tree(tree.rc);
    for (const auto& node : tree.rc.nodes) {
      cap += node.capacitance_ff;
    }
    for (const auto& edge : tree.rc.edges) {
      resistance += edge.resistance_ohm;
    }
    length += tree.rc.wire_length_um;
  }
  // Attachments: vertex->input (dx=1,dy=.5), output->vertex (dx=1,dy=1).
  EXPECT_EQ(cap, 13 + 19 + 2 * 3 + 1.5 * 7);
  EXPECT_EQ(resistance, 11 + 17 + 2 * 2 + 1.5 * 5);
  EXPECT_EQ(length, 80 + 2 + 1.5);
  EXPECT_EQ(candidate.trees[0].positions[0], f.c.driver.output);
  EXPECT_EQ(candidate.trees[1].positions[0], buffer.output);
  EXPECT_EQ(candidate.trees[0].terminals.size(), 1);
  EXPECT_TRUE(candidate.trees[0].terminals[0].buffer);
  EXPECT_EQ(candidate.trees[1].terminals.size(), 1);
  EXPECT_FALSE(candidate.trees[1].terminals[0].buffer);
  EXPECT_EQ(candidate.trees[1].rc.nodes[candidate.trees[1].terminals[0].node].pin, f.c.sinks[0].name);
  const auto unbuffered = b::expand_tree(f.c, f.route, {0, 0, 0});
  cap                   = 0;
  for (const auto& node : unbuffered.trees[0].rc.nodes) {
    cap += node.capacitance_ff;
  }
  EXPECT_EQ(cap, 32);
}

TEST(BufferTreeSearch, CascadedSearchUsesActualIncomingSlewAtEveryStage) {
  Fixture f;
  auto    extra  = f.route.vertices[1];
  extra.position = {20, .5};
  for (auto& option : extra.options) {
    option.buffer.name     += "extra";
    option.buffer.input.x  += 10;
    option.buffer.output.x += 10;
    option.buffer.body.xl  += 10;
    option.buffer.body.xh  += 10;
    option.site.first      += 10;
  }
  f.route.vertices.push_back(extra);
  f.route.segments[1] = {1, 3, 0, 0, 10, true};
  f.route.segments.push_back({3, 2, 0, 0, 60, true});
  const auto candidate  = b::expand_tree(f.c, f.route, {0, 1, 0, 2});
  const auto evaluation = b::evaluate(f.lib, f.c, candidate);
  ASSERT_EQ(evaluation.status, b::Status::evaluated);
  ASSERT_TRUE(evaluation.feasible());
  for (unsigned t = 0; t < 2; ++t) {
    const double driver_slew  = 2 + t + .25 * f.c.driver.input_slew_ps[t] + 2 * (1 + t);
    const double first_slew   = 1 + t + .125 * driver_slew + .5 * (2 + t);
    const double second_delay = 1 + t + .125 * first_slew + .125 * (4 + t);
    const auto&  input        = evaluation.pins.at(extra.options[1].buffer.name + ":A");
    const auto&  output       = evaluation.pins.at(extra.options[1].buffer.name + ":Y");
    EXPECT_NEAR(input.slew_ps[t], first_slew, 1e-8);
    EXPECT_NEAR(output.arrival_ps[t] - input.arrival_ps[t], second_delay, 1e-8);
  }
  const auto result = f.search();
  EXPECT_EQ(result.assignments, 9);
  EXPECT_EQ(result.status, b::Tree_search_status::complete);
}

TEST(BufferTreeSearch, InvalidTopologyGeometrySitesAndSettingsCannotMutateReservations) {
  Fixture    f;
  const auto before       = f.sites.reservations();
  auto       route        = f.route;
  route.segments[1].child = 1;
  EXPECT_THROW(b::search_tree(f.lib, f.c, route, f.sites, "net", f.settings), std::invalid_argument);
  route = f.route;
  route.vertices[2].sinks.clear();
  EXPECT_THROW(b::expand_tree(f.c, route, {0, 0, 0}), std::invalid_argument);
  route                        = f.route;
  route.vertices[0].position.x = 1;
  EXPECT_THROW(b::expand_tree(f.c, route, {0, 0, 0}), std::invalid_argument);
  EXPECT_THROW(b::expand_tree(f.c, f.route, {0, 3, 0}), std::invalid_argument);
  route                                       = f.route;
  route.vertices[1].options[0].buffer.body.xl = 7;
  EXPECT_THROW(b::search_tree(f.lib, f.c, route, f.sites, "net", f.settings), std::invalid_argument);
  f.settings.dbu_per_um = 0;
  EXPECT_THROW(f.search(), std::invalid_argument);
  EXPECT_EQ(f.sites.reservations(), before);
}
