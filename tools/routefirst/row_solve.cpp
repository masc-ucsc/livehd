// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "row_solve.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <stdexcept>

#include "lemon/list_graph.h"
#include "lemon/preflow.h"

namespace livehd::routefirst {
namespace {
struct Bound {
  size_t      from, to;
  int64_t     gap;
  std::string reason;
};

// Recover an actual positive cycle including the implicit reference vertex
// for lower and upper bounds. Disjoint site classes can also cause a conflict
// without a positive cycle; the cut then identifies the incompatible implications.
std::vector<std::string> positive_cycle(const std::vector<Row_variable>& vars, const std::vector<Row_constraint>& rules) {
  const size_t       ref = vars.size();
  std::vector<Bound> edges;
  for (const auto& c : rules) {
    edges.push_back({c.from, c.to, c.gap, c.reason});
  }
  for (size_t i = 0; i < ref; ++i) {
    edges.push_back({ref, i, vars[i].sites.front(), vars[i].name + " lower bound"});
    edges.push_back({i, ref, -int64_t(vars[i].sites.back()), vars[i].name + " upper bound"});
  }
  std::vector<int64_t> value(ref + 1);
  std::vector<size_t>  prior(ref + 1);
  size_t               changed = 0;
  for (size_t pass = 0; pass <= ref; ++pass) {
    bool relaxed = false;
    for (size_t i = 0; i < edges.size(); ++i) {
      const auto& edge = edges[i];
      const auto  next = value[edge.from] + edge.gap;
      if (next > value[edge.to]) {
        value[edge.to] = next;
        prior[edge.to] = i;
        changed        = edge.to;
        relaxed        = true;
      }
    }
    if (!relaxed) {
      return {};
    }
  }
  for (size_t i = 0; i <= ref; ++i) {
    changed = edges[prior[changed]].from;
  }
  std::vector<std::string> cycle;
  auto                     at = changed;
  do {
    const auto& edge = edges[prior[at]];
    cycle.push_back(edge.reason);
    at = edge.from;
  } while (at != changed);
  std::reverse(cycle.begin(), cycle.end());
  return cycle;
}
}  // namespace

Row_solution solve_row(const std::vector<Row_variable>& vars, const std::vector<Row_constraint>& rules) {
  using Graph = lemon::ListDigraph;
  Graph                  graph;
  const auto             source = graph.addNode();
  const auto             sink   = graph.addNode();
  Graph::ArcMap<int64_t> capacities(graph);
  const auto             edge = [&](Graph::Node a, Graph::Node b, int64_t cap) {
    const auto arc  = graph.addArc(a, b);
    capacities[arc] = cap;
  };
  // Threshold node k is on the source side iff x is at least sites[k].
  // The first threshold is always selected. Forbidden phases have no node.
  std::vector<std::vector<Graph::Node>> thresholds(vars.size());
  int64_t                               magnitude = 0;
  for (size_t i = 0; i < vars.size(); ++i) {
    const auto& v = vars[i];
    if (v.sites.empty()) {
      return {false, {}, 0, {v.name + " has no legal site"}};
    }
    thresholds[i].push_back(source);
    // Bound both the signed objective and the total finite cut capacity.
    const int64_t span  = int64_t(v.sites.back()) - v.sites.front();
    const auto    bound = std::max({std::abs(int64_t(v.sites.front())), std::abs(int64_t(v.sites.back())), span});
    const auto    coeff = std::abs(int64_t(v.cost));
    if (coeff && bound > (std::numeric_limits<int64_t>::max() / 4 - magnitude) / coeff) {
      throw std::overflow_error("row objective exceeds capacity representation");
    }
    magnitude += coeff * bound;
    for (size_t k = 1; k < v.sites.size(); ++k) {
      const auto delta = int64_t(v.sites[k]) - v.sites[k - 1];
      if (delta <= 0) {
        throw std::invalid_argument("row sites must be strictly increasing");
      }
      const auto node = graph.addNode();
      thresholds[i].push_back(node);
      const auto weight = delta * v.cost;
      if (weight > 0) {
        edge(node, sink, weight);
      } else if (weight < 0) {
        edge(source, node, -weight);
      }
    }
  }
  const int64_t hard = magnitude + 1;
  for (size_t i = 0; i < vars.size(); ++i) {
    for (size_t k = 2; k < thresholds[i].size(); ++k) {
      edge(thresholds[i][k], thresholds[i][k - 1], hard);
    }
  }
  for (const auto& c : rules) {
    const auto& from = vars.at(c.from).sites;
    const auto& to   = vars.at(c.to).sites;
    for (size_t k = 0; k < from.size(); ++k) {
      const auto j = std::lower_bound(to.begin(), to.end(), int64_t(from[k]) + c.gap);
      if (j == to.begin()) {
        continue;
      }
      edge(thresholds[c.from][k], j == to.end() ? sink : thresholds[c.to][size_t(j - to.begin())], hard);
    }
  }
  // Cap total flow, even for many contradicting hard bounds in a wide row.
  const auto supply = graph.addNode();
  edge(supply, source, hard);
  lemon::Preflow<Graph, Graph::ArcMap<int64_t>> flow(graph, capacities, supply, sink);
  flow.run();
  Row_solution result;
  if (flow.flowValue() >= hard) {
    result.conflict = positive_cycle(vars, rules);
    if (result.conflict.empty()) {
      // Compute the least site sequence directly to trace a discrete-domain
      // contradiction. Preserve every participating reason for diagnosis.
      std::vector<int> lower;
      for (const auto& v : vars) {
        lower.push_back(v.sites.front());
      }
      bool changed;
      do {
        changed = false;
        for (const auto& c : rules) {
          const auto need = int64_t(lower[c.from]) + c.gap;
          if (need <= lower[c.to]) {
            continue;
          }
          result.conflict.push_back(c.reason);
          const auto& sites = vars[c.to].sites;
          auto        site  = std::lower_bound(sites.begin(), sites.end(), need);
          if (site == sites.end()) {
            result.conflict.push_back(vars[c.to].name + " legal sites exhausted");
            return result;
          }
          lower[c.to] = *site;
          changed     = true;
        }
      } while (changed);
      throw std::logic_error("inconsistent row feasibility checks");
    }
    return result;
  }
  result.feasible = true;
  for (size_t i = 0; i < vars.size(); ++i) {
    int x = vars[i].sites.front();
    for (size_t k = 1; k < thresholds[i].size(); ++k) {
      if (flow.minCut(thresholds[i][k])) {
        x = vars[i].sites[k];
      }
    }
    result.x.push_back(x);
    result.cost += int64_t(x) * vars[i].cost;
  }
  return result;
}
}  // namespace livehd::routefirst
