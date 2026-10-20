// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "floorplan_color.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numeric>
#include <stdexcept>
#include <tuple>

namespace livehd::color {
namespace {
// Physical counterpart of the synthesis memory-first policy: first ownership,
// area-weighted overlap evidence, then one bounded best merge per memory group
// per round. Ordinary register/output cones supply growth opportunities, but
// only regions containing a macro survive as placement colors.
Floorplan_coloring memory_first_colors(const std::vector<Floorplan_node>&                  nodes,
                                       const std::array<std::vector<std::vector<int>>, 2>& edges, const std::vector<int>& macros,
                                       double limit) {
  Floorplan_coloring out;
  out.owner.assign(nodes.size(), -1);
  std::vector<int>                   parent, memory_roots, visited(nodes.size(), -1);
  std::vector<double>                area;
  std::vector<std::map<int, double>> adjacent;
  const auto                         region = [&]() {
    const int id = parent.size();
    parent.push_back(id);
    area.push_back(0);
    adjacent.emplace_back();
    return id;
  };
  const auto root = [&](int a) {
    while (parent[a] != a) {
      parent[a] = parent[parent[a]];
      a         = parent[a];
    }
    return a;
  };
  const auto walk = [&](int id, std::vector<int> pending) {
    double reached_area = 0;
    bool   truncated    = false;
    while (!pending.empty()) {
      const int n = pending.back();
      pending.pop_back();
      if (nodes[n].kind != Floorplan_kind::logic || visited[n] == id) {
        continue;
      }
      visited[n] = id;
      if (reached_area + nodes[n].area > limit) {
        truncated = true;
        continue;
      }
      reached_area += nodes[n].area;
      if (out.owner[n] < 0) {
        out.owner[n]  = id;
        area[id]     += nodes[n].area;
      } else if (out.owner[n] != id) {
        // A node is visited once per cone even when reconvergent paths reach it.
        adjacent[id][out.owner[n]] += nodes[n].area;
        adjacent[out.owner[n]][id] += nodes[n].area;
      }
      pending.insert(pending.end(), edges[0][n].begin(), edges[0][n].end());
    }
    out.truncated_cones += truncated;
  };
  std::map<std::vector<int>, int> identical;
  for (int macro : macros) {
    auto signature = nodes[macro].input_nets;
    std::sort(signature.begin(), signature.end());
    signature.erase(std::unique(signature.begin(), signature.end()), signature.end());
    if (!signature.empty()) {
      if (auto it = identical.find(signature); it != identical.end()) {
        out.owner[macro] = it->second;
        continue;
      }
    }
    const int id = region();
    memory_roots.push_back(id);
    out.owner[macro] = id;
    if (!signature.empty()) {
      identical.emplace(std::move(signature), id);
    }
    walk(id, edges[0][macro]);
  }
  out.initial_memory_groups = memory_roots.size();
  if (memory_roots.empty()) {
    return out;
  }
  // One ordinary backward cone per state/output boundary. Control closures
  // have already become barriers, so they cannot join functional data colors.
  std::vector<int> sinks;
  for (int n = 0; n < int(nodes.size()); ++n) {
    if ((nodes[n].kind == Floorplan_kind::state || nodes[n].kind == Floorplan_kind::barrier)
        && std::any_of(edges[0][n].begin(), edges[0][n].end(), [&](int p) { return nodes[p].kind == Floorplan_kind::logic; })) {
      sinks.push_back(n);
    }
  }
  std::sort(sinks.begin(), sinks.end(), [&](int a, int b) { return std::pair(nodes[a].name, a) < std::pair(nodes[b].name, b); });
  for (int n : sinks) {
    walk(region(), edges[0][n]);
  }
  std::vector<int> served(parent.size(), -1);
  for (int round = 0;; ++round) {
    int merges = 0;
    for (int seed : memory_roots) {
      int a = root(seed);
      if (served[a] == round) {
        continue;
      }
      served[a]      = round;
      int    best    = -1;
      double overlap = 0, best_size = 0;
      for (const auto& [b, shared] : adjacent[a]) {
        const double size = area[a] + area[b];
        if (shared <= 0 || served[b] == round || size > limit) {
          continue;
        }
        if (best < 0 || shared > overlap || (shared == overlap && std::pair(size, b) < std::pair(best_size, best))) {
          best      = b;
          overlap   = shared;
          best_size = size;
        }
      }
      if (best < 0) {
        continue;
      }
      // Memory roots precede all ordinary regions. Keeping the smaller id
      // preserves the deterministic canonical macro name through every merge.
      int b = best;
      if (a > b) {
        std::swap(a, b);
      }
      parent[b]  = a;
      area[a]   += area[b];
      adjacent[a].erase(b);
      for (const auto& [other, shared] : adjacent[b]) {
        adjacent[other].erase(b);
        if (other == a) {
          continue;
        }
        adjacent[a][other] += shared;
        adjacent[other][a] += shared;
      }
      adjacent[b].clear();
      served[a] = round;
      ++merges;
    }
    out.merges_per_round.push_back(merges);
    if (!merges) {
      break;
    }
  }
  std::vector<int> compact(parent.size(), -1);
  for (int macro : macros) {
    const int r = root(out.owner[macro]);
    if (compact[r] < 0) {
      compact[r] = out.colors.size();
      out.colors.push_back({nodes[macro].name, {}, 0, 0});
    }
  }
  for (auto& owner : out.owner) {
    if (owner >= 0) {
      owner = compact[root(owner)];
    }
  }
  return out;
}
}  // namespace
Floorplan_coloring floorplan_color(const std::vector<Floorplan_node>& nodes, const std::vector<std::pair<int, int>>& data_edges,
                                   const Floorplan_coloring* preserved, Floorplan_color_options options) {
  if (options.memory_first && (!std::isfinite(options.max_logic_area) || options.max_logic_area <= 0)) {
    throw std::invalid_argument("memory-first floorplan coloring requires a positive logic-area budget");
  }
  const int count = static_cast<int>(nodes.size());
  if (!options.hierarchy_scope.empty()
      && (options.hierarchy_scope.size() != nodes.size() || preserved
          || std::any_of(options.hierarchy_scope.begin(), options.hierarchy_scope.end(), [](int g) { return g < -1; }))) {
    throw std::invalid_argument("invalid hierarchy scopes for floorplan coloring");
  }
  std::array<std::vector<std::vector<int>>, 2> edges;
  for (auto& direction : edges) {
    direction.resize(count);
  }
  for (auto [from, to] : data_edges) {
    if (from < 0 || from >= count || to < 0 || to >= count) {
      throw std::invalid_argument("floorplan coloring edge outside physical graph");
    }
    edges[0][to].push_back(from);
    edges[1][from].push_back(to);
  }
  for (auto& direction : edges) {
    for (auto& adjacent : direction) {
      std::sort(adjacent.begin(), adjacent.end());
      adjacent.erase(std::unique(adjacent.begin(), adjacent.end()), adjacent.end());
    }
  }
  std::vector<int> macros;
  for (int n = 0; n < count; ++n) {
    if (!std::isfinite(nodes[n].area) || nodes[n].area < 0) {
      throw std::invalid_argument("invalid floorplan coloring physical area");
    }
    if (nodes[n].kind == Floorplan_kind::macro) {
      macros.push_back(n);
    }
  }
  std::sort(macros.begin(), macros.end(), [&](int a, int b) { return nodes[a].name < nodes[b].name; });
  std::vector<int> parent(macros.size());
  std::iota(parent.begin(), parent.end(), 0);
  const auto root = [&](int a) {
    while (parent[a] != a) {
      parent[a] = parent[parent[a]];
      a         = parent[a];
    }
    return a;
  };
  Floorplan_coloring out;
  out.owner.assign(count, -1);
  out.recipients.resize(count);
  std::array<std::vector<int>, 2> visited;
  for (auto& direction : visited) {
    direction.assign(count, -1);
  }
  std::vector<std::tuple<int, int, int>> boundaries;  // state, seed, direction
  for (int seed = 0; !options.memory_first && !preserved && seed < static_cast<int>(macros.size()); ++seed) {
    const int macro  = macros[seed];
    out.owner[macro] = seed;
    for (int dir = 0; dir < 2; ++dir) {
      auto pending = edges[dir][macro];
      while (!pending.empty()) {
        const int n = pending.back();
        pending.pop_back();
        if (nodes[n].kind == Floorplan_kind::state) {
          boundaries.emplace_back(n, seed, dir);
          continue;
        }
        if (nodes[n].kind != Floorplan_kind::logic) {
          continue;
        }
        if (out.owner[n] < 0) {
          out.owner[n] = seed;
        } else {
          const int a = root(seed), b = root(out.owner[n]);
          parent[std::max(a, b)] = std::min(a, b);
        }
        if (visited[dir][n] >= 0) {
          continue;  // Its complete same-direction cone was already traversed.
        }
        visited[dir][n] = seed;
        pending.insert(pending.end(), edges[dir][n].begin(), edges[dir][n].end());
      }
    }
  }
  std::vector<int> compact(macros.size(), -1);
  for (int seed = 0; seed < static_cast<int>(macros.size()); ++seed) {
    const int r = root(seed);
    if (compact[r] < 0) {
      compact[r] = out.colors.size();
      out.colors.push_back({nodes[macros[r]].name, {}, 0, 0});
    }
  }
  for (int n = 0; n < count; ++n) {
    if (out.owner[n] >= 0) {
      out.owner[n] = compact[root(out.owner[n])];
      auto& color  = out.colors[out.owner[n]];
      color.members.push_back(n);
      color.area   += nodes[n].area;
      color.macros += nodes[n].kind == Floorplan_kind::macro;
    }
  }
  Floorplan_coloring memory;
  if (options.memory_first && !preserved) {
    memory    = memory_first_colors(nodes, edges, macros, options.max_logic_area);
    preserved = &memory;
  }
  if (preserved) {
    out.initial_memory_groups = preserved->initial_memory_groups;
    out.merges_per_round      = preserved->merges_per_round;
    out.truncated_cones       = preserved->truncated_cones;
    if (preserved->owner.size() != nodes.size()) {
      throw std::invalid_argument("preserved floorplan color size mismatch");
    }
    out.owner = preserved->owner;
    out.colors.clear();
    for (const auto& c : preserved->colors) {
      out.colors.push_back({c.name, {}, 0, 0});
    }
    boundaries.clear();
    for (int n = 0; n < count; ++n) {
      const int c = out.owner[n];
      if (c < -1 || c >= int(out.colors.size())
          || (c >= 0 && nodes[n].kind != Floorplan_kind::logic && nodes[n].kind != Floorplan_kind::macro)) {
        throw std::invalid_argument("preserved floorplan color has invalid member");
      }
      if (c < 0) {
        continue;
      }
      auto& color = out.colors[c];
      color.members.push_back(n);
      color.area   += nodes[n].area;
      color.macros += nodes[n].kind == Floorplan_kind::macro;
      for (int dir = 0; dir < 2; ++dir) {
        for (int next : edges[dir][n]) {
          if (nodes[next].kind == Floorplan_kind::state) {
            boundaries.emplace_back(next, c, dir);
          }
        }
      }
    }
  } else {
    for (auto& [boundary, seed, dir] : boundaries) {
      seed = compact[root(seed)];
    }
  }
  if (!options.hierarchy_scope.empty()) {
    std::vector<Floorplan_color> scoped;
    for (const auto& color : out.colors) {
      std::map<int, Floorplan_color> portions;
      for (int n : color.members) {
        auto& part = portions[options.hierarchy_scope[n]];
        part.members.push_back(n);
        part.area   += nodes[n].area;
        part.macros += nodes[n].kind == Floorplan_kind::macro;
      }
      for (auto& [scope, part] : portions) {
        part.name = color.name;
        if (portions.size() > 1) {
          part.name += "@scope:" + std::to_string(scope);
        }
        for (int n : part.members) {
          out.owner[n] = scoped.size();
        }
        scoped.push_back(std::move(part));
      }
    }
    out.colors = std::move(scoped);
    // Recompute reachability from local members. Copying an old recipient list
    // to every portion would reserve area for unrelated sibling candidates.
    boundaries.clear();
    for (int n = 0; n < count; ++n) {
      if (out.owner[n] < 0) {
        continue;
      }
      for (int dir = 0; dir < 2; ++dir) {
        for (int next : edges[dir][n]) {
          if (nodes[next].kind == Floorplan_kind::state) {
            boundaries.emplace_back(next, out.owner[n], dir);
          }
        }
      }
    }
  }
  // Each (color,direction) visit is bounded by the next state element. A
  // direction-specific stamp suppresses reconvergence without losing recipients.
  for (auto& direction : visited) {
    direction.assign(count, -1);
  }
  std::sort(boundaries.begin(), boundaries.end(), [](const auto& a, const auto& b) {
    return std::tuple{std::get<1>(a), std::get<2>(a), std::get<0>(a)} < std::tuple{std::get<1>(b), std::get<2>(b), std::get<0>(b)};
  });
  boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
  for (auto [boundary, color, dir] : boundaries) {
    if (visited[dir][boundary] == color) {
      continue;
    }
    visited[dir][boundary] = color;
    out.recipients[boundary].push_back(color);
    auto pending = edges[dir][boundary];
    while (!pending.empty()) {
      const int n = pending.back();
      pending.pop_back();
      if (out.owner[n] >= 0 || nodes[n].kind != Floorplan_kind::logic || visited[dir][n] == color) {
        continue;
      }
      visited[dir][n] = color;
      out.recipients[n].push_back(color);
      pending.insert(pending.end(), edges[dir][n].begin(), edges[dir][n].end());
    }
  }
  for (auto& recipients : out.recipients) {
    std::sort(recipients.begin(), recipients.end());
    recipients.erase(std::unique(recipients.begin(), recipients.end()), recipients.end());
  }
  return out;
}
}  // namespace livehd::color
