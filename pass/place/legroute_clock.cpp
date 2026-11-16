// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// legroute clock charge: the tracks a spine clock tree would take per tile, charged against the routing capacity
// before the signal nets are routed (todo/livehd/2r-route.html, R7).
#include <algorithm>
#include <cmath>
#include <map>

#include "legroute_internal.hpp"

namespace livehd::placement::lr {
namespace {
// Prim's minimum spanning tree (Manhattan), as (parent, child) index pairs.
std::vector<std::pair<int, int>> mst(const std::vector<std::pair<double, double>>& pts) {
  const size_t                     n = pts.size();
  std::vector<double>              best(n, INFINITY);
  std::vector<int>                 par(n, -1);
  std::vector<char>                in(n, 0);
  std::vector<std::pair<int, int>> out;
  best[0] = 0;
  for (size_t it = 0; it < n; ++it) {
    int u = -1;
    for (size_t k = 0; k < n; ++k) {
      if (!in[k] && (u < 0 || best[k] < best[u])) {
        u = int(k);
      }
    }
    in[u] = 1;
    if (par[u] >= 0) {
      out.emplace_back(par[u], u);
    }
    for (size_t k = 0; k < n; ++k) {
      const double dd = std::abs(pts[u].first - pts[k].first) + std::abs(pts[u].second - pts[k].second);
      if (!in[k] && dd < best[k]) {
        best[k] = dd;
        par[k]  = u;
      }
    }
  }
  return out;
}
}  // namespace

Clock_domain plan_clock_domain(const Grid& g, const std::vector<int>& tiles, int pitch) {
  Clock_domain     out;
  std::vector<int> cols, rows;
  for (int t : tiles) {
    cols.push_back(g.col(t));
    rows.push_back(g.row(t));
  }
  std::ranges::sort(cols);
  const int rmin = *std::ranges::min_element(rows), rmax = *std::ranges::max_element(rows);
  out.trunk_c = cols[cols.size() / 2];
  // ribs every `pitch` tile rows, centered so every flop row is within pitch / 2 of one
  for (int r = rmin + pitch / 2; r <= rmax + pitch / 2; r += pitch) {
    out.ribs.push_back(std::min(r, rmax));
  }
  std::ranges::sort(out.ribs);
  out.ribs.erase(std::unique(out.ribs.begin(), out.ribs.end()), out.ribs.end());
  for (int r : out.ribs) {
    out.span[r] = {out.trunk_c, out.trunk_c};
  }
  for (int t : tiles) {
    int best = out.ribs.front();
    for (int r : out.ribs) {
      if (std::abs(r - g.row(t)) < std::abs(best - g.row(t))) {
        best = r;
      }
    }
    auto& s  = out.span[best];
    s.first  = std::min(s.first, g.col(t));
    s.second = std::max(s.second, g.col(t));
    out.tap_rib.push_back(best);
  }
  return out;
}

Clock_charge clock_tree_charge(const Design& d, const Grid& g, unsigned rib_tiles) {
  const int    pitch = int(std::max(1u, rib_tiles));
  Clock_charge out{std::vector<int>(g.size(), 0), std::vector<int>(g.size(), 0)};
  for (const auto& net : d.nets) {
    if (!net.clock || net.power || net.pins.size() < 2) {
      continue;
    }
    std::vector<int> sinks;  // one tile per clock sink pin of a movable cell
    std::vector<int> nodes;  // the tree's other nodes: the driver first, then the remaining pins' tiles
    for (size_t k = 0; k < net.pins.size(); ++k) {
      const auto& p = net.pins[k];
      const int   t = physical_pin_access(d, g, p).tile;
      if (p.clock && p.comp >= 0 && d.cell[size_t(p.comp)] >= 0 && int(k) != net.driver) {
        sinks.push_back(t);
        continue;
      }
      if (std::ranges::find(nodes, t) == nodes.end()) {
        nodes.push_back(t);
      }
      if (int(k) == net.driver) {
        std::swap(nodes.front(), *std::ranges::find(nodes, t));
      }
    }
    if (!sinks.empty()) {
      const auto cd = plan_clock_domain(g, sinks, pitch);
      for (int r = cd.ribs.front(); r <= cd.ribs.back(); ++r) {
        ++out.v[size_t(g.tile(cd.trunk_c, r))];
      }
      for (const auto& [r, cr] : cd.span) {
        for (int c = cr.first; c <= cr.second; ++c) {
          ++out.h[size_t(g.tile(c, r))];
        }
      }
      for (size_t k = 0; k < sinks.size(); ++k) {
        const int c = g.col(sinks[k]), row = g.row(sinks[k]);
        for (int r = std::min(row, cd.tap_rib[k]); r <= std::max(row, cd.tap_rib[k]); ++r) {
          ++out.v[size_t(g.tile(c, r))];
        }
      }
      const int entry = g.tile(cd.trunk_c, cd.ribs[cd.ribs.size() / 2]);
      if (std::ranges::find(nodes, entry) == nodes.end()) {
        nodes.push_back(entry);
      }
    }
    if (nodes.size() < 2) {
      continue;
    }
    // the joins: an MST over the nodes, each edge an L, horizontal first
    std::vector<std::pair<double, double>> pts;
    for (int t : nodes) {
      pts.emplace_back(g.cx(g.col(t)), g.cy(g.row(t)));
    }
    for (auto [a, b] : mst(pts)) {
      const int ta = nodes[size_t(a)], tb = nodes[size_t(b)];
      const int ca = g.col(ta), ra = g.row(ta), cb = g.col(tb), rb = g.row(tb);
      if (ca != cb) {
        for (int c = std::min(ca, cb); c <= std::max(ca, cb); ++c) {
          ++out.h[size_t(g.tile(c, ra))];
        }
      }
      if (ra != rb) {
        for (int r = std::min(ra, rb); r <= std::max(ra, rb); ++r) {
          ++out.v[size_t(g.tile(cb, r))];
        }
      }
    }
  }
  return out;
}
}  // namespace livehd::placement::lr
