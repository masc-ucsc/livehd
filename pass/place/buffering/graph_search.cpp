// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "graph_search.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <set>

#include "checks.hpp"

namespace livehd::placement::buffering {
namespace {
using detail::Clock;
using detail::elapsed;
using detail::require;
constexpr auto absent = std::numeric_limits<size_t>::max();
struct State {
  size_t              root  = 0;
  uint64_t            sinks = 0;
  std::vector<size_t> vertices, edges;
};
// Edges in a state are a connected acyclic subset by construction. This
// producer validates integer-to-physical conversion on every selected edge.
Routing_tree produce(const Visibility_graph& graph, const std::vector<size_t>& edges,
                     const std::vector<std::vector<Tree_option>>& options, const std::array<double, 4>& rc, double dbu) {
  std::vector<std::vector<std::pair<size_t, size_t>>> adjacent(graph.vertices.size());
  for (auto id : edges) {
    const auto e = graph.edges[id];
    adjacent[e.a].push_back({e.b, id});
    adjacent[e.b].push_back({e.a, id});
  }
  const auto          root = graph.terminals[0];
  std::vector<size_t> index(graph.vertices.size(), absent), order{root};
  index[root] = 0;
  Routing_tree tree;
  tree.attachment_rc = rc;
  for (size_t at = 0; at < order.size(); ++at) {
    const auto v = order[at];
    const auto p = graph.vertices[v];
    tree.vertices.push_back({physical_point(p, dbu), {}, options[v]});
    for (auto [next, id] : adjacent[v]) {
      if (index[next] != absent) {
        continue;
      }
      index[next] = order.size();
      order.push_back(next);
      tree.segments.push_back(visibility_segment(p, graph.vertices[next], graph.edges[id].length_dbu, at, index[next], dbu, rc));
    }
  }
  require(tree.vertices.size() == edges.size() + 1, "graph-tree edges are not a connected tree");
  for (size_t sink = 1; sink < graph.terminals.size(); ++sink) {
    const auto vertex = index[graph.terminals[sink]];
    require(vertex != absent, "graph-tree misses original sink");
    tree.vertices[vertex].sinks.push_back(sink - 1);
  }
  return tree;
}
}  // namespace
Graph_search_result search_graph(const liberty::timing::Library& lib, const Context& context, const Visibility_graph& graph,
                                 const std::vector<std::vector<Tree_option>>& options, const std::array<double, 4>& rc,
                                 const Site_reservations& reservations, const std::string& owner,
                                 const Graph_search_settings& settings) {
  const auto started = Clock::now();
  require(std::isfinite(settings.max_seconds) && settings.max_seconds > 0 && !owner.empty(), "invalid graph search time/owner");
  require(!context.sinks.empty() && context.sinks.size() < 64 && context.sinks.size() <= settings.max_fanout,
          "graph search fanout exceeds declared subset bound");
  require(graph.terminals.size() == context.sinks.size() + 1 && options.size() == graph.vertices.size(),
          "graph search identity/option counts differ");
  require(settings.tree.max_frontier > 0, "graph search needs positive frontier bound");
  Graph_search_result result;
  const auto          finish = [&](Graph_search_status status) {
    result.status  = status;
    result.seconds = elapsed(started);
    return result;
  };
  if (graph.vertices.size() > settings.max_vertices || graph.edges.size() > settings.max_edges) {
    return finish(Graph_search_status::budget_exhausted);
  }
  // Reuse the geometry validator and seed, with the same total time/work budget.
  Route_limits limits;
  limits.max_vertices    = settings.max_vertices;
  limits.max_edges       = settings.max_edges;
  limits.max_relaxations = settings.max_transitions;
  limits.max_seconds     = settings.max_seconds;
  auto seed              = shortest_path_tree(graph, settings.tree.dbu_per_um, rc, limits);
  result.transitions     = seed.relaxations;
  if (seed.status == Route_status::budget_exhausted) {
    return finish(Graph_search_status::budget_exhausted);
  }
  if (seed.status == Route_status::disconnected) {
    result.geometry_disconnected    = true;
    result.topology_space_exhausted = true;
    return finish(Graph_search_status::complete);
  }
  const auto physical = [&](size_t id) { return physical_point(graph.vertices[id], settings.tree.dbu_per_um); };
  require(physical(graph.terminals[0]) == context.driver.output, "graph root differs from actual physical driver");
  for (size_t s = 0; s < context.sinks.size(); ++s) {
    require(physical(graph.terminals[s + 1]) == context.sinks[s].position, "graph terminal differs from actual physical sink");
  }
  // Parallel duplicate edges would give identical topology with different IDs;
  // reject malformed graph input rather than count them as separate optima.
  std::map<std::pair<size_t, size_t>, size_t>         edge_ids;
  std::vector<std::vector<std::pair<size_t, size_t>>> adjacency(graph.vertices.size());
  for (size_t id = 0; id < graph.edges.size(); ++id) {
    const auto e   = graph.edges[id];
    const auto key = std::minmax(e.a, e.b);
    require(edge_ids.emplace(key, id).second, "duplicate visibility edge");
    adjacency[e.a].push_back({e.b, id});
    adjacency[e.b].push_back({e.a, id});
  }
  const auto            root = graph.terminals[0];
  const uint64_t        full = (uint64_t{1} << context.sinks.size()) - 1;
  std::vector<uint64_t> sinks(graph.vertices.size());
  for (size_t s = 1; s < graph.terminals.size(); ++s) {
    sinks[graph.terminals[s]] |= uint64_t{1} << (s - 1);
  }
  std::set<std::vector<size_t>> topologies;
  bool                          incomplete_models = false;
  const auto                    admit             = [&](Graph_solution solution) {
    const auto& candidate = solution.solution;
    const bool  old_dominates = result.best && dominates_tree_solution(result.best->solution, candidate, context);
    const bool  new_dominates = result.best && dominates_tree_solution(candidate, result.best->solution, context);
    if (!result.best || (new_dominates && !old_dominates)
        || (!(old_dominates && !new_dominates)
            && prefer_tree_solution(candidate, result.best->solution, settings.tree.selection))) {
      result.best = solution;
    }
    if (std::any_of(result.frontier.begin(), result.frontier.end(), [&](const auto& old) {
          return dominates_tree_solution(old.solution, candidate, context);
        })) {
      return true;
    }
    std::erase_if(result.frontier, [&](const auto& old) { return dominates_tree_solution(candidate, old.solution, context); });
    if (result.frontier.size() >= settings.tree.max_frontier) {
      return false;
    }
    result.frontier.push_back(std::move(solution));
    result.peak_frontier = std::max(result.peak_frontier, result.frontier.size());
    return true;
  };
  std::optional<Graph_search_status> stop;
  const auto                         evaluate_topology = [&](const std::vector<size_t>& edges) {
    if (topologies.contains(edges)) {
      return;
    }
    if (topologies.size() >= settings.max_topologies || elapsed(started) >= settings.max_seconds) {
      stop = Graph_search_status::budget_exhausted;
      return;
    }
    topologies.insert(edges);
    auto tree   = produce(graph, edges, options, rc, settings.tree.dbu_per_um);
    auto budget = settings.tree;
    budget.max_assignments = budget.max_assignments > result.assignments ? budget.max_assignments - result.assignments : 0;
    budget.max_waveform_fits
        = budget.max_waveform_fits > result.waveform_fits ? budget.max_waveform_fits - result.waveform_fits : 0;
    budget.max_seconds = std::min(budget.max_seconds, settings.max_seconds - elapsed(started));
    if (budget.max_seconds <= 0) {
      stop = Graph_search_status::budget_exhausted;
      return;
    }
    auto selected = search_tree(lib, context, tree, reservations, owner, budget);
    result.trials.push_back({edges,
                             selected.status,
                             selected.assignments,
                             selected.evaluated,
                             selected.unsupported,
                             selected.assignment_space_exhausted});
    auto& trial               = result.trials.back();
    trial.electrical_rejected = selected.electrical_rejected;
    trial.site_rejected       = selected.site_rejected;
    trial.resource_rejected   = selected.resource_rejected;
    if (selected.best) {
      trial.best_input_margin_ps = selected.best->worst_input_margin_ps;
      trial.best_wire_length_um  = selected.best->timing.wire_length_um;
    }
    result.assignments   += selected.assignments;
    result.evaluated     += selected.evaluated;
    result.unsupported   += selected.unsupported;
    result.waveform_fits += selected.waveform_fits;
    for (const auto& reason : selected.unsupported_examples) {
      if (result.unsupported_examples.size() < 8
          && std::find(result.unsupported_examples.begin(), result.unsupported_examples.end(), reason)
                 == result.unsupported_examples.end()) {
        result.unsupported_examples.push_back(reason);
      }
    }
    // Best may contain feasible evidence even if frontier storage exhausted.
    if (selected.best && !admit({edges, tree, *selected.best})) {
      stop = Graph_search_status::budget_exhausted;
    }
    for (auto& solution : selected.frontier) {
      if (!admit({edges, tree, std::move(solution)})) {
        stop = Graph_search_status::budget_exhausted;
      }
    }
    if (selected.status == Tree_search_status::budget_exhausted) {
      stop = Graph_search_status::budget_exhausted;
    } else if (selected.status == Tree_search_status::incomplete_models) {
      incomplete_models = true;
    } else if (selected.status == Tree_search_status::unbuffered_retained) {
      require(bool(selected.best), "unbuffered policy returned no feasible candidate");
      result.best = Graph_solution{edges, tree, *selected.best};
      stop        = Graph_search_status::unbuffered_retained;
    }
    if (elapsed(started) >= settings.max_seconds) {
      stop = Graph_search_status::budget_exhausted;
    }
  };
  // Reconstruct the seed's visibility edge IDs from its original coordinates.
  std::map<Point, size_t, decltype([](Point a, Point b) { return a.x == b.x ? a.y < b.y : a.x < b.x; })> by_position;
  for (size_t v = 0; v < graph.vertices.size(); ++v) {
    require(by_position.emplace(physical(v), v).second, "visibility vertices lose physical identity");
  }
  std::vector<size_t> seed_edges;
  for (const auto& e : seed.tree->segments) {
    const auto a = by_position.at(seed.tree->vertices[e.parent].position),
               b = by_position.at(seed.tree->vertices[e.child].position);
    seed_edges.push_back(edge_ids.at(std::minmax(a, b)));
  }
  std::sort(seed_edges.begin(), seed_edges.end());
  evaluate_topology(seed_edges);
  if (stop) {
    return finish(*stop);
  }
  std::vector<State>                               states;
  std::set<std::pair<size_t, std::vector<size_t>>> seen;
  std::vector<std::vector<size_t>>                 at_root(graph.vertices.size());
  const auto                                       add = [&](State state) {
    if (seen.contains({state.root, state.edges})) {
      ++result.duplicate_states;
      return;
    }
    if (states.size() >= settings.max_states
        || state.vertices.size() > settings.max_state_vertex_entries - result.state_vertex_entries
        || elapsed(started) >= settings.max_seconds) {
      stop = Graph_search_status::budget_exhausted;
      return;
    }
    seen.emplace(state.root, state.edges);
    at_root[state.root].push_back(states.size());
    result.state_vertex_entries += state.vertices.size();
    states.push_back(std::move(state));
    result.states = states.size();
  };
  for (size_t v = 0; v < sinks.size(); ++v) {
    if (sinks[v]) {
      add({v, sinks[v], {v}, {}});
      if (stop) {
        return finish(*stop);
      }
    }
  }
  const auto spend = [&]() {
    if (result.transitions >= settings.max_transitions || elapsed(started) >= settings.max_seconds) {
      stop = Graph_search_status::budget_exhausted;
      return false;
    }
    ++result.transitions;
    return true;
  };
  for (size_t current = 0; current < states.size(); ++current) {
    const auto state = states[current];  // additions may reallocate states
    if (state.root == root && state.sinks == full) {
      evaluate_topology(state.edges);
      if (stop) {
        return finish(*stop);
      }
    }
    // The driver cannot be internal to a partial subtree rooted elsewhere.
    if (state.root != root) {
      for (auto [next, edge] : adjacency[state.root]) {
        if (!spend()) {
          return finish(*stop);
        }
        if (std::binary_search(state.vertices.begin(), state.vertices.end(), next)) {
          ++result.cycle_rejected;
          continue;
        }
        auto extended   = state;
        extended.root   = next;
        extended.sinks |= sinks[next];
        extended.vertices.insert(std::lower_bound(extended.vertices.begin(), extended.vertices.end(), next), next);
        extended.edges.insert(std::lower_bound(extended.edges.begin(), extended.edges.end(), edge), edge);
        add(std::move(extended));
        if (stop) {
          return finish(*stop);
        }
      }
    }
    // Every older state at this root is paired once with this newer state.
    // Reachable-set overlap beyond the root is a cycle or reconvergence.
    const auto others = at_root[state.root];
    for (auto other : others) {
      if (other >= current) {
        continue;
      }
      if (!spend()) {
        return finish(*stop);
      }
      const auto& branch = states[other];
      if ((state.sinks & branch.sinks & ~sinks[state.root]) != 0) {
        ++result.duplicate_sink_rejected;
        continue;
      }
      std::vector<size_t> intersection;
      std::set_intersection(state.vertices.begin(),
                            state.vertices.end(),
                            branch.vertices.begin(),
                            branch.vertices.end(),
                            std::back_inserter(intersection));
      if (intersection.size() != 1) {
        ++result.cycle_rejected;
        continue;
      }
      State merged;
      merged.root  = state.root;
      merged.sinks = state.sinks | branch.sinks;
      std::set_union(state.vertices.begin(),
                     state.vertices.end(),
                     branch.vertices.begin(),
                     branch.vertices.end(),
                     std::back_inserter(merged.vertices));
      std::set_union(state.edges.begin(),
                     state.edges.end(),
                     branch.edges.begin(),
                     branch.edges.end(),
                     std::back_inserter(merged.edges));
      add(std::move(merged));
      if (stop) {
        return finish(*stop);
      }
    }
  }
  result.topology_space_exhausted = true;
  return finish(incomplete_models ? Graph_search_status::incomplete_models : Graph_search_status::complete);
}
}  // namespace livehd::placement::buffering
