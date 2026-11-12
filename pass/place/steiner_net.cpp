// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "steiner_net.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>

#include "steiner_topology.hpp"
#include "thirdparty/flute/flute.hpp"

namespace livehd::placement {

// Placement RC uses driver-aware Steiner trees, with half each wire's capacitance on each end. Coincident pins
// attach to a shared junction through 1e-9 ohm edges. If driver-aware construction exceeds its work bound, spatial
// FLUTE groups are joined by representative trees and any cycles are removed.
Steiner_net steiner_net(const std::vector<std::pair<double, double>>& pins, size_t driver_index, const std::array<double, 4>& per_um,
                        double dbu, bool* approximate) {
  initialize_flute_tables();
  if (driver_index >= pins.size()) {
    throw std::invalid_argument("Steiner RC requires a driver pin");
  }
  struct {
    double horizontal_r, horizontal_c, vertical_r, vertical_c;
  } wire{per_um[0], per_um[1], per_um[2], per_um[3]};
  Steiner_net rc;
  std::vector<int>                        xs, ys;
  std::map<std::pair<int, int>, unsigned> junction;
  std::vector<std::pair<int, int>>        positions;
  for (const auto& xy : pins) {
    if (std::abs(xy.first) > std::numeric_limits<int>::max() / 4.0 || std::abs(xy.second) > std::numeric_limits<int>::max() / 4.0) {
      throw std::runtime_error("coordinates exceed FLUTE integer range");
    }
    positions.emplace_back(std::llround(xy.first), std::llround(xy.second));
    rc.node_cap_ff.push_back(0);
  }
  if (wire.horizontal_r == 0 && wire.vertical_r == 0 && wire.horizontal_c == 0 && wire.vertical_c == 0) {
    // Post-synthesis timing has no physical wire model. Its seed positions must
    // not affect criticality through arbitrary junctions or epsilon resistors.
    // One ideal junction gives every pin the same load and zero wire delay.
    const auto hub = static_cast<unsigned>(rc.node_cap_ff.size());
    rc.node_cap_ff.push_back(0);
    for (unsigned pin = 0; pin < hub; ++pin) {
      rc.edges.push_back({pin, hub, 0});
    }
    return rc;
  }
  auto vertex = [&](std::pair<int, int> xy) {
    auto [it, added] = junction.emplace(xy, rc.node_cap_ff.size());
    if (added) {
      rc.node_cap_ff.push_back(0);
    }
    return it->second;
  };
  std::vector<double> edge_capacitance, edge_length;
  for (size_t i = 0; i < positions.size(); ++i) {
    edge_capacitance.push_back(0);
    edge_length.push_back(0);
    rc.edges.push_back({static_cast<unsigned>(i), vertex(positions[i]), 1e-9});
  }
  for (const auto& [xy, index] : junction) {
    (void)index;
    xs.push_back(xy.first);
    ys.push_back(xy.second);
  }
  auto add_edge = [&](unsigned a, unsigned b, double dx, double dy) {
    if (a == b) {
      return;
    }
    const double resistance  = (wire.horizontal_r * dx + wire.vertical_r * dy) / dbu;
    const double capacitance = (wire.horizontal_c * dx + wire.vertical_c * dy) / dbu;
    rc.edges.push_back({a, b, std::max(1e-9, resistance)});
    edge_capacitance.push_back(capacitance);
    edge_length.push_back((dx + dy) / dbu);
    rc.wire_length_um          += edge_length.back();
    rc.node_cap_ff[a] += capacitance / 2;
    rc.node_cap_ff[b] += capacitance / 2;
  };
  bool timing_tree_built = false;
  if (xs.size() > 1) {
    std::vector<Steiner_location> locations;
    for (size_t i = 0; i < xs.size(); ++i) {
      locations.emplace_back(xs[i], ys[i]);
    }
    const auto       root_location = std::find(locations.begin(),
                                               locations.end(),
                                               Steiner_location{positions[driver_index].first, positions[driver_index].second});
    Steiner_topology tree;
    try {
      tree              = timing_steiner_topology(locations, root_location - locations.begin());
      timing_tree_built = true;
    } catch (const std::length_error&) {
      // Construction has not modified the RC graph, so fallback starts clean.
    }
    if (timing_tree_built) {
      std::vector<unsigned> rc_vertex(tree.locations.size());
      for (const auto terminal : tree.terminals) {
        rc_vertex[terminal] = vertex(tree.locations[terminal]);
      }
      for (size_t i = locations.size(); i < tree.locations.size(); ++i) {
        rc_vertex[i] = rc.node_cap_ff.size();
        rc.node_cap_ff.push_back(0);
      }
      for (const auto& [ai, bi] : tree.edges) {
        const auto& a = tree.locations[ai];
        const auto& b = tree.locations[bi];
        add_edge(rc_vertex[ai], rc_vertex[bi], std::abs(double(a.first) - b.first), std::abs(double(a.second) - b.second));
      }
    }
  }
  if (xs.size() > 1 && !timing_tree_built) {
    // Work-bounded fallback: FLUTE trees over spatial groups of at most
    // 1000 locations (recursive median bisection), joined by a FLUTE tree over
    // each group's location nearest the driver. A star from the driver would
    // charge every sink its full Manhattan distance (a 1036-sink net timed 6x
    // slower than a Steiner estimate).
    if (approximate) {
      *approximate = true;
    }
    std::vector<std::pair<int, int>> points;
    for (const auto& [xy, index] : junction) {
      (void)index;
      points.push_back(xy);
    }
    std::set<std::pair<unsigned, unsigned>> edges;
    const auto                              tree = [&](const std::vector<std::pair<int, int>>& group) {
      if (group.size() < 2) {
        return;
      }
      std::vector<int> gx, gy;
      for (const auto& p : group) {
        gx.push_back(p.first);
        gy.push_back(p.second);
      }
      auto t = flute::flute(gx.size(), gx.data(), gy.data(), 3);
      for (int i = 0; i < 2 * t.deg - 2; ++i) {
        const auto a  = std::pair{t.branch[i].x, t.branch[i].y};
        const auto b  = std::pair{t.branch[t.branch[i].n].x, t.branch[t.branch[i].n].y};
        const auto ai = vertex(a), bi = vertex(b);
        if (edges.emplace(std::min(ai, bi), std::max(ai, bi)).second) {
          add_edge(ai, bi, std::abs(double(a.first) - b.first), std::abs(double(a.second) - b.second));
        }
      }
      std::free(t.branch);
    };
    const auto root  = positions[driver_index];
    const auto close = [&](const std::pair<int, int>& p) {
      return std::abs(double(p.first) - root.first) + std::abs(double(p.second) - root.second);
    };
    // Groups of <= 1000 locations; their representatives form the next level.
    std::vector<std::pair<int, int>> level = points;
    while (level.size() > 1000) {
      std::vector<std::vector<std::pair<int, int>>> groups;
      const auto                                    split = [&](auto&& self, std::vector<std::pair<int, int>> items) -> void {
        if (items.size() <= 1000) {
          groups.push_back(std::move(items));
          return;
        }
        int x0 = items.front().first, x1 = x0, y0 = items.front().second, y1 = y0;
        for (const auto& p : items) {
          x0 = std::min(x0, p.first);
          x1 = std::max(x1, p.first);
          y0 = std::min(y0, p.second);
          y1 = std::max(y1, p.second);
        }
        const bool by_x = x1 - x0 >= y1 - y0;
        const auto mid  = items.begin() + items.size() / 2;
        std::nth_element(items.begin(), mid, items.end(), [&](const auto& a, const auto& b) {
          return by_x ? a < b : std::pair{a.second, a.first} < std::pair{b.second, b.first};
        });
        self(self, std::vector<std::pair<int, int>>(items.begin(), mid));
        self(self, std::vector<std::pair<int, int>>(mid, items.end()));
      };
      split(split, level);
      level.clear();
      for (const auto& g : groups) {
        tree(g);
        level.push_back(*std::min_element(g.begin(), g.end(), [&](const auto& a, const auto& b) { return close(a) < close(b); }));
      }
    }
    tree(level);
  }
  // Coordinate coalescing can make independently built FLUTE trees share
  // several junctions. Their geometric union is connected but need not be a
  // tree: edge deduplication alone leaves cycles, which RC traversal cannot
  // time. Leave an already valid tree's edges and capacitance sums untouched.
  if (!rc.node_cap_ff.empty()) {
    std::vector<unsigned> parent(rc.node_cap_ff.size()), size(rc.node_cap_ff.size(), 1);
    const auto            reset = [&] {
      std::iota(parent.begin(), parent.end(), 0);
      std::fill(size.begin(), size.end(), 1);
    };
    const auto root = [&](unsigned n) {
      while (parent[n] != n) {
        parent[n] = parent[parent[n]];
        n         = parent[n];
      }
      return n;
    };
    const auto join = [&](unsigned a, unsigned b) {
      a = root(a);
      b = root(b);
      if (a == b) {
        return false;
      }
      if (size[a] < size[b]) {
        std::swap(a, b);
      }
      parent[b]  = a;
      size[a]   += size[b];
      return true;
    };
    reset();
    bool cyclic = false;
    for (const auto& edge : rc.edges) {
      cyclic |= !join(edge.from, edge.to);
    }
    for (unsigned i = 0; i < rc.node_cap_ff.size(); ++i) {
      if (root(i) != root(0)) {
        throw std::runtime_error("disconnected FLUTE RC graph");
      }
    }
    if (cyclic) {
      reset();
      std::vector<bool> keep(rc.edges.size());
      // Pin-to-junction edges are mandatory leaves, including coincident pins.
      for (size_t i = 0; i < pins.size(); ++i) {
        const auto& edge = rc.edges[i];
        if (!join(edge.from, edge.to)) {
          throw std::logic_error("cyclic FLUTE pin attachments");
        }
        keep[i] = true;
      }
      std::vector<size_t> candidates(rc.edges.size() - pins.size());
      std::iota(candidates.begin(), candidates.end(), pins.size());
      std::stable_sort(candidates.begin(), candidates.end(), [&](size_t a, size_t b) {
        return rc.edges[a].resistance_ohm < rc.edges[b].resistance_ohm;
      });
      for (const auto i : candidates) {
        const auto& edge = rc.edges[i];
        keep[i]          = join(edge.from, edge.to);
      }
      std::fill(rc.node_cap_ff.begin(), rc.node_cap_ff.end(), 0.0);
      rc.wire_length_um = 0;
      size_t retained   = 0;
      for (size_t i = 0; i < rc.edges.size(); ++i) {
        if (keep[i]) {
          rc.wire_length_um                  += edge_length[i];
          const auto edge                     = rc.edges[i];
          rc.node_cap_ff[edge.from] += edge_capacitance[i] / 2;
          rc.node_cap_ff[edge.to]   += edge_capacitance[i] / 2;
          rc.edges[retained++]                = edge;
        }
      }
      rc.edges.resize(retained);
      if (retained + 1 != rc.node_cap_ff.size()) {
        throw std::logic_error("FLUTE spanning tree did not retain every junction");
      }
      if (approximate) {
        *approximate = true;
      }
    }
  }
  return rc;
}
}  // namespace livehd::placement
