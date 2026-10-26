// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "graph_search.hpp"

#include <algorithm>
#include <numeric>
#include <set>

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
void cell(m::Library& lib, std::string name, double cin, double d, double cap, double slew) {
  lib.cells.emplace(name,
                    b::test_fixture::buffer_cell(name,
                                                 {cin, cin + 1},
                                                 100,
                                                 {table(d, .125, cap), table(d + 1, .125, cap)},
                                                 {table(slew, .0625, cap), table(slew + 1, .0625, cap)}));
}
struct Fixture {
  m::Library                               lib;
  b::Context                               context;
  b::Visibility_graph                      graph;
  std::vector<std::vector<b::Tree_option>> options;
  b::Site_reservations                     sites{
      b::build_legal_sites({{"lo", {0, 0, 100, 10}, 1}, {"hi", {0, 10, 100, 20}, 1}},
      {}
      )
  };
  b::Graph_search_settings settings;
  Fixture(size_t fanout = 1) {
    cell(lib, "D", 1, 5, 1, 2);
    cell(lib, "B1", 1, 2, .25, 1);
    cell(lib, "B2", 2, 1, .125, .5);
    cell(lib, "S", 4, 0, 0, 0);
    graph.vertices = {
        {10,  5},
        {20,  5},
        {30,  5},
        {10, 15},
        {20, 15},
        {30, 15}
    };
    graph.edges = {
        {0, 1, 10},
        {1, 2, 10},
        {3, 4, 10},
        {4, 5, 10},
        {0, 3, 10},
        {1, 4, 10},
        {2, 5, 10}
    };
    graph.terminals = {0};
    const std::vector<std::vector<size_t>> sinks{
        {5},
        {2, 5},
        {2, 4, 5},
        {1, 2, 4, 5}
    };
    for (auto v : sinks.at(fanout - 1)) {
      graph.terminals.push_back(v);
    }
    context.driver = {
        "d",
        {"D", "A", "Y"},
        {1, .5},
        {10, 20},
        {3, 7}
    };
    context.core                    = {0, 0, 10, 2};
    context.capacity["gap"]         = 10;
    context.normalization_period_ps = 128;
    for (size_t s = 1; s < graph.terminals.size(); ++s) {
      auto p = graph.vertices[graph.terminals[s]];
      context.sinks.push_back({
          "s" + std::to_string(s),
          "S",
          "A",
          {double(p.x) / 10, double(p.y) / 10},
          {             100,              120},
          {     .5 / fanout,      .5 / fanout}
      });
    }
    options.resize(graph.vertices.size());
    for (size_t v : std::vector<size_t>{1, 3}) {
      auto         p = graph.vertices[v];
      const double x = double(p.x) / 10, y = double(p.y) / 10;
      for (size_t type = 1; type <= 2; ++type) {
        b::Buffer buffer{
            "b" + std::to_string(v),
            {"B" + std::to_string(type), "A", "Y"},
            {x, y},
            {x, y},
            {x, y - .1, x + .1, y + .1},
            true,
            {{"gap", type}}
        };
        options[v].push_back({
            buffer,
            {p.y == 5 ? "lo" : "hi", uint64_t(p.x), 1},
            true
        });
      }
    }
    settings.tree.dbu_per_um          = 10;
    settings.tree.max_seconds         = 10;
    settings.max_seconds              = 10;
    settings.max_states               = 100000;
    settings.max_state_vertex_entries = 1000000;
    settings.max_transitions          = 1000000;
  }
  b::Graph_search_result search() const {
    return b::search_graph(lib, context, graph, options, {0, 0, 0, 0}, sites, "net", settings);
  }
};
// Independent exhaustive EDGE-SUBSET oracle, not extension/merge recurrence.
std::set<std::vector<size_t>> oracle_topologies(const b::Visibility_graph& graph) {
  std::set<std::vector<size_t>> out;
  for (uint64_t mask = 0; mask < (uint64_t{1} << graph.edges.size()); ++mask) {
    std::vector<size_t> parent(graph.vertices.size()), degree(graph.vertices.size());
    std::iota(parent.begin(), parent.end(), 0);
    const auto find = [&](size_t v) {
      while (parent[v] != v) {
        v = parent[v];
      }
      return v;
    };
    bool                cycle = false;
    std::vector<size_t> edges;
    for (size_t e = 0; e < graph.edges.size(); ++e) {
      if (mask & (uint64_t{1} << e)) {
        auto edge = graph.edges[e];
        auto a = find(edge.a), b = find(edge.b);
        if (a == b) {
          cycle = true;
          break;
        }
        parent[a] = b;
        ++degree[edge.a];
        ++degree[edge.b];
        edges.push_back(e);
      }
    }
    if (cycle) {
      continue;
    }
    const auto root      = graph.terminals[0];
    const auto component = find(root);
    if (std::any_of(graph.terminals.begin(), graph.terminals.end(), [&](size_t v) { return find(v) != component; })) {
      continue;
    }
    bool bad = false;
    for (size_t v = 0; v < degree.size(); ++v) {
      if (degree[v] && find(v) != component) {
        bad = true;
      }
      if (degree[v] == 1 && v != root && std::find(graph.terminals.begin(), graph.terminals.end(), v) == graph.terminals.end()) {
        bad = true;
      }
    }
    if (!bad) {
      out.insert(std::move(edges));
    }
  }
  return out;
}
b::Routing_tree oracle_tree(const Fixture& fixture, const std::vector<size_t>& edges) {
  const auto&                      graph = fixture.graph;
  std::vector<std::vector<size_t>> adj(graph.vertices.size());
  for (auto e : edges) {
    auto x = graph.edges[e];
    adj[x.a].push_back(x.b);
    adj[x.b].push_back(x.a);
  }
  b::Routing_tree     tree;
  std::vector<size_t> order{graph.terminals[0]}, index(graph.vertices.size(), 10000);
  index[order[0]] = 0;
  for (size_t i = 0; i < order.size(); ++i) {
    auto v = order[i];
    auto p = graph.vertices[v];
    tree.vertices.push_back({
        {double(p.x) / 10, double(p.y) / 10},
        {},
        fixture.options[v]
    });
    for (auto next : adj[v]) {
      if (index[next] == 10000) {
        index[next] = order.size();
        order.push_back(next);
        tree.segments.push_back({i, index[next], 0, 0, 1, true});
      }
    }
  }
  for (size_t s = 1; s < graph.terminals.size(); ++s) {
    tree.vertices[index[graph.terminals[s]]].sinks.push_back(s - 1);
  }
  return tree;
}
std::vector<b::Tree_solution> exhaustive_candidates(const Fixture& fixture, const std::set<std::vector<size_t>>& topologies) {
  std::vector<b::Tree_solution> all;
  for (const auto& edges : topologies) {
    auto                tree = oracle_tree(fixture, edges);
    std::vector<size_t> choices(tree.vertices.size());
    for (;;) {
      auto candidate = b::expand_tree(fixture.context, tree, choices);
      auto timing    = b::evaluate(fixture.lib, fixture.context, candidate);
      if (timing.feasible()) {
        b::Tree_solution solution{choices, std::move(candidate), std::move(timing), {}, {}};
        for (size_t v = 0; v < choices.size(); ++v) {
          if (choices[v]) {
            solution.sites.push_back(tree.vertices[v].options[choices[v] - 1].site);
          }
        }
        std::sort(solution.sites.begin(), solution.sites.end(), [](auto a, auto b) {
          return a.row == b.row ? a.first < b.first : a.row < b.row;
        });
        for (unsigned t = 0; t < 2; ++t) {
          if (solution.timing.root_required_ps[t]) {
            double margin                  = *solution.timing.root_required_ps[t] - fixture.context.driver.input_arrival_ps[t];
            solution.worst_input_margin_ps = std::min(solution.worst_input_margin_ps.value_or(margin), margin);
          }
        }
        all.push_back(std::move(solution));
      }
      size_t digit = choices.size();
      while (digit) {
        --digit;
        if (choices[digit] < tree.vertices[digit].options.size()) {
          ++choices[digit];
          break;
        }
        choices[digit] = 0;
      }
      if (!digit && choices[0] == 0) {
        break;
      }
    }
  }
  return all;
}
bool equivalent(const b::Tree_solution& a, const b::Tree_solution& b, const b::Context& c) {
  return b::dominates_tree_solution(a, b, c) && b::dominates_tree_solution(b, a, c);
}
}  // namespace

TEST(BufferGraphSearch, FanoutOneThroughFourEnumeratesEveryOracleTree) {
  for (size_t fanout = 1; fanout <= 4; ++fanout) {
    Fixture f(fanout);
    auto    result = f.search();
    ASSERT_EQ(result.status, b::Graph_search_status::complete) << fanout;
    EXPECT_TRUE(result.topology_space_exhausted);
    std::set<std::vector<size_t>> actual;
    for (auto trial : result.trials) {
      EXPECT_TRUE(trial.assignment_space_exhausted);
      actual.insert(trial.edges);
    }
    EXPECT_EQ(actual, oracle_topologies(f.graph)) << fanout;
    EXPECT_EQ(actual.size(), result.trials.size());
    EXPECT_TRUE(f.sites.reservations().empty());
    ASSERT_TRUE(result.best);
  }
}
TEST(BufferGraphSearch, CompleteElectricalFrontierMatchesAllTreesAndCellChoices) {
  for (size_t fanout = 1; fanout <= 4; ++fanout) {
    Fixture f(fanout);
    auto    result = f.search();
    ASSERT_EQ(result.status, b::Graph_search_status::complete);
    auto                          all = exhaustive_candidates(f, oracle_topologies(f.graph));
    std::vector<b::Tree_solution> oracle;
    for (auto& candidate : all) {
      if (std::any_of(oracle.begin(), oracle.end(), [&](const auto& old) {
            return b::dominates_tree_solution(old, candidate, f.context);
          })) {
        continue;
      }
      std::erase_if(oracle, [&](const auto& old) { return b::dominates_tree_solution(candidate, old, f.context); });
      oracle.push_back(candidate);
    }
    ASSERT_EQ(result.frontier.size(), oracle.size()) << fanout;
    for (const auto& expected : oracle) {
      EXPECT_TRUE(std::any_of(result.frontier.begin(), result.frontier.end(), [&](const auto& found) {
        return equivalent(expected, found.solution, f.context);
      })) << fanout;
    }
    ASSERT_TRUE(result.best);
    for (const auto& expected : oracle) {
      EXPECT_FALSE(b::prefer_tree_solution(expected, result.best->solution, f.settings.tree.selection)
                   && !equivalent(expected, result.best->solution, f.context));
    }
  }
}
TEST(BufferGraphSearch, StateAndTopologyBudgetsKeepSeedWithoutClaimingOptimum) {
  for (unsigned mode = 0; mode < 4; ++mode) {
    Fixture f(2);
    if (mode == 0) {
      f.settings.max_states = 0;
    }
    if (mode == 1) {
      f.settings.max_state_vertex_entries = 0;
    }
    if (mode == 2) {
      f.settings.max_topologies = 1;
    }
    if (mode == 3) {
      f.settings.tree.max_assignments = 1;
    }
    auto result = f.search();
    EXPECT_EQ(result.status, b::Graph_search_status::budget_exhausted) << mode;
    EXPECT_FALSE(result.topology_space_exhausted);
    EXPECT_TRUE(result.best) << mode;
    EXPECT_TRUE(f.sites.reservations().empty());
  }
  Fixture f;
  f.settings.max_topologies = 0;
  auto result               = f.search();
  EXPECT_EQ(result.status, b::Graph_search_status::budget_exhausted);
  EXPECT_FALSE(result.best);
}
TEST(BufferGraphSearch, GlobalWorkLimitsAndFrontierBoundsRemainExplicit) {
  for (unsigned mode = 0; mode < 4; ++mode) {
    Fixture f;
    if (mode == 0) {
      f.settings.max_transitions = 0;
    }
    if (mode == 1) {
      f.settings.max_vertices = 0;
    }
    if (mode == 2) {
      f.settings.tree.max_waveform_fits = 0;
    }
    if (mode == 3) {
      f.settings.tree.max_frontier = 1;
    }
    auto result = f.search();
    EXPECT_EQ(result.status, b::Graph_search_status::budget_exhausted) << mode;
    EXPECT_FALSE(result.topology_space_exhausted);
    EXPECT_LE(result.frontier.size(), f.settings.tree.max_frontier);
  }
}
TEST(BufferGraphSearch, MissingModelsCannotProveInfeasibility) {
  Fixture f;
  f.lib.cells.erase("D");
  auto result = f.search();
  EXPECT_EQ(result.status, b::Graph_search_status::incomplete_models);
  EXPECT_TRUE(result.topology_space_exhausted);
  EXPECT_FALSE(result.best);
  EXPECT_GT(result.unsupported, 0);
  f.settings.max_states = 0;
  result                = f.search();
  EXPECT_EQ(result.status, b::Graph_search_status::budget_exhausted);
  EXPECT_FALSE(result.topology_space_exhausted);
}
TEST(BufferGraphSearch, DisconnectedGeometryIsSeparateFromSearchExhaustion) {
  Fixture f;
  f.graph.edges.clear();
  auto result = f.search();
  EXPECT_EQ(result.status, b::Graph_search_status::complete);
  EXPECT_TRUE(result.geometry_disconnected);
  EXPECT_TRUE(result.topology_space_exhausted);
  EXPECT_FALSE(result.best);
  EXPECT_EQ(result.evaluated, 0);
}
TEST(BufferGraphSearch, CoincidentOriginalSinksAndDriverKeepTheirLoads) {
  Fixture f(3);
  f.graph.terminals           = {0, 0, 5, 5};
  f.context.sinks[0].position = f.context.driver.output;
  f.context.sinks[1].position = {3, 1.5};
  f.context.sinks[2].position = {3, 1.5};
  auto result                 = f.search();
  ASSERT_EQ(result.status, b::Graph_search_status::complete);
  ASSERT_TRUE(result.best);
  std::set<std::vector<size_t>> actual;
  for (auto trial : result.trials) {
    actual.insert(trial.edges);
  }
  EXPECT_EQ(actual, oracle_topologies(f.graph));
  for (const auto& sink : f.context.sinks) {
    EXPECT_TRUE(result.best->solution.timing.pins.contains(sink.name));
  }
}
TEST(BufferGraphSearch, ShortNetPolicyOnlyRetainsAnElectricallyFeasibleUnbufferedTree) {
  Fixture f;
  f.settings.tree.prefer_unbuffered_if_feasible = true;
  auto result                                   = f.search();
  EXPECT_EQ(result.status, b::Graph_search_status::unbuffered_retained);
  ASSERT_TRUE(result.best);
  EXPECT_TRUE(result.best->solution.candidate.buffers.empty());
  EXPECT_FALSE(result.topology_space_exhausted);
  f.lib.cells.at("D").pins.at("Y").max_capacitance_ff = 2;
  result                                              = f.search();
  EXPECT_NE(result.status, b::Graph_search_status::unbuffered_retained);
  ASSERT_TRUE(result.best);
  EXPECT_FALSE(result.best->solution.candidate.buffers.empty());
  EXPECT_TRUE(result.best->solution.timing.feasible());
}
TEST(BufferGraphSearch, SharedReservationsAndOutsideWindowRemainHardConstraints) {
  Fixture f(2);
  ASSERT_EQ(f.sites
                .replace("other",
                         {
                             {"lo", 20, 1},
                             {"hi", 10, 1}
  })
                .status,
            b::Reservation_status::reserved);
  auto result = f.search();
  ASSERT_TRUE(result.best);
  EXPECT_TRUE(result.best->solution.candidate.buffers.empty());
  EXPECT_EQ(f.sites.reservations().size(), 1);
  f.settings.tree.selection = b::Tree_selection::outside_window;
  for (auto& sink : f.context.sinks) {
    sink.required_ps = {-1, -1};
  }
  result = f.search();
  EXPECT_EQ(result.status, b::Graph_search_status::complete);
  EXPECT_FALSE(result.best);
}
TEST(BufferGraphSearch, InvalidIdentityGraphAndFanoutBoundsAreRejected) {
  Fixture f;
  f.context.driver.output.x++;
  EXPECT_THROW(f.search(), std::invalid_argument);
  f = Fixture{};
  f.options.pop_back();
  EXPECT_THROW(f.search(), std::invalid_argument);
  f = Fixture{};
  f.graph.edges.push_back(f.graph.edges[0]);
  EXPECT_THROW(f.search(), std::invalid_argument);
  f                     = Fixture{};
  f.settings.max_fanout = 0;
  EXPECT_THROW(f.search(), std::invalid_argument);
  f = Fixture{};
  f.graph.edges[0].length_dbu++;
  EXPECT_THROW(f.search(), std::invalid_argument);
}

TEST(BufferGraphSearch, NativeTimingCanImproveOnTheShortestPathSeedBySharingWire) {
  Fixture f(2);
  f.graph.vertices.clear();
  f.graph.edges.clear();
  for (int64_t y = 5; y <= 25; y += 10) {
    for (int64_t x = 10; x <= 30; x += 10) {
      f.graph.vertices.push_back({x, y});
    }
  }
  for (size_t y = 0; y < 3; ++y) {
    for (size_t x = 0; x < 3; ++x) {
      size_t v = y * 3 + x;
      if (x < 2) {
        f.graph.edges.push_back({v, v + 1, 10});
      }
      if (y < 2) {
        f.graph.edges.push_back({v, v + 3, 10});
      }
    }
  }
  f.graph.terminals = {0, 5, 7};
  f.options.assign(9, {});
  f.context.core.yh           = 3;
  f.context.sinks[0].position = {3, 1.5};
  f.context.sinks[1].position = {2, 2.5};
  const std::array<double, 4> rc{0, 1, 0, 1};
  auto                        seed = b::shortest_path_tree(f.graph, 10, rc);
  ASSERT_TRUE(seed.tree);
  EXPECT_EQ(seed.length_dbu, 50);
  auto direct
      = b::evaluate(f.lib, f.context, b::expand_tree(f.context, *seed.tree, std::vector<size_t>(seed.tree->vertices.size())));
  ASSERT_TRUE(direct.feasible());
  auto result = b::search_graph(f.lib, f.context, f.graph, f.options, rc, f.sites, "net", f.settings);
  ASSERT_EQ(result.status, b::Graph_search_status::complete);
  ASSERT_TRUE(result.best);
  EXPECT_DOUBLE_EQ(result.best->solution.timing.wire_length_um, 4);
  ASSERT_FALSE(result.trials.empty());
  ASSERT_TRUE(result.trials[0].best_wire_length_um);
  EXPECT_DOUBLE_EQ(*result.trials[0].best_wire_length_um, 5);
  ASSERT_TRUE(result.trials[0].best_input_margin_ps);
  EXPECT_GT(*result.best->solution.worst_input_margin_ps, *result.trials[0].best_input_margin_ps + .9);
  for (unsigned t = 0; t < 2; ++t) {
    ASSERT_TRUE(direct.root_required_ps[t]);
    ASSERT_TRUE(result.best->solution.timing.root_required_ps[t]);
    EXPECT_GT(*result.best->solution.timing.root_required_ps[t], *direct.root_required_ps[t] + .9);
  }
}
TEST(BufferGraphSearch, ActualInputSlewChangesThePreferredDiscreteLibraryChoice) {
  Fixture f(4);
  for (unsigned t = 0; t < 2; ++t) {
    f.lib.cells.at("B1").pins.at("Y").arcs[0].delay[t] = table(0, 2, .25);
    f.lib.cells.at("B2").pins.at("Y").arcs[0].delay[t] = table(10, 0, .125);
  }
  std::vector<std::vector<std::string>> choices;
  for (double slew : std::vector<double>{0, 128}) {
    f.context.driver.input_slew_ps = {slew, slew};
    auto result                    = f.search();
    ASSERT_EQ(result.status, b::Graph_search_status::complete);
    ASSERT_TRUE(result.best);
    std::vector<std::string> cells;
    for (const auto& buffer : result.best->solution.candidate.buffers) {
      cells.push_back(buffer.model.cell);
    }
    choices.push_back(cells);
    auto all = exhaustive_candidates(f, oracle_topologies(f.graph));
    for (const auto& candidate : all) {
      EXPECT_FALSE(candidate.worst_input_margin_ps > result.best->solution.worst_input_margin_ps);
    }
  }
  EXPECT_NE(choices[0], choices[1]);
  EXPECT_NE(std::find(choices[0].begin(), choices[0].end(), "B1"), choices[0].end());
  EXPECT_NE(std::find(choices[1].begin(), choices[1].end(), "B2"), choices[1].end());
}
