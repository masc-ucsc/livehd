// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <cmath>
#include <cstdio>
#include <set>
#include <stdexcept>

#include "legroute_internal.hpp"

namespace livehd::placement::lr {

std::vector<Global_net> all_signal_requests(const Design& d, const Grid& g, const Boxes& boxes) {
  std::vector<Global_net> nets;
  for (size_t n = 0; n < d.nets.size(); ++n) {
    const auto& net = d.nets[n];
    if (!net.routed || (!d.opt.cellbox_full_flow && net.clock)) {
      continue;
    }
    Global_net gn;
    gn.net  = int(n);
    gn.crit = net.crit;
    std::set<int> seen;
    const auto    push = [&](const Pin& p) {
      int       t   = -1;
      const int box = p.comp >= 0 && d.cell[size_t(p.comp)] >= 0 ? boxes.box_of[size_t(d.cell[size_t(p.comp)])] : -1;
      if (box >= 0) {
        const auto& b = boxes.boxes[size_t(box)];
        t             = d.opt.cellbox_full_flow ? b.tile : g.tile_of(b.cx, b.cy);
      } else {
        t = physical_pin_access(d, g, p).tile;
      }
      if (t >= 0 && seen.insert(t).second) {
        gn.endpoints.push_back(t);
      }
    };
    if (net.driver >= 0) {
      push(net.pins[size_t(net.driver)]);
    }
    for (const auto& pin : net.pins) {
      push(pin);
    }
    nets.push_back(std::move(gn));
  }
  return nets;
}

Global_routes route_all_signals(const Design& d, const Grid& g, const Boxes& boxes, const std::vector<int>& cap_h,
                                const std::vector<int>& cap_v) {
  auto nets = all_signal_requests(d, g, boxes);
  // Search uses dimensions and region labels only. One unowned region removes the seed mesh's boundary
  // penalty and own-region capacity exemption; every signal edge contributes to congestion.
  Grid routing_grid;
  routing_grid.cols   = g.cols;
  routing_grid.rows   = g.rows;
  routing_grid.tile_w = g.tile_w;
  routing_grid.tile_h = g.tile_h;
  routing_grid.region.assign(g.size(), 0);
  auto out   = global_route(routing_grid,
                            cap_h,
                            cap_v,
                            admission_share(d.opt),
                            0,
                            nets,
                            d.opt.global_margin,
                            d.opt.global_rounds,
                            d.opt.global_net_pops,
                            d.opt.global_astar_weight,
                            d.opt.global_pattern);
  auto trees = std::move(out.trees);
  out.trees.resize(d.nets.size());
  for (size_t k = 0; k < nets.size(); ++k) {
    out.trees[size_t(nets[k].net)] = std::move(trees[k]);
  }
  std::fprintf(stderr,
               "legroute partition route: %zu signals, %zu without a path, %zu edges over share by %zu tracks, "
               "%zu queue pops, %zu search-limited attempts, %zu topology-disconnected signals, %.3f s\n",
               nets.size(),
               out.unrouted,
               out.overflow_edges,
               out.overflow_tracks,
               out.search_pops,
               out.search_limited,
               out.topology_disconnected,
               out.seconds);
  return out;
}

Route_cuts route_cuts(const Grid& g, const Global_routes& routes, const std::vector<char>& critical, const std::vector<int>& cap_h,
                      const std::vector<int>& cap_v, double share, bool protect_critical) {
  if (g.cols <= 0 || g.rows <= 0 || routes.unrouted || critical.size() != routes.trees.size() || cap_h.size() != g.size()
      || cap_v.size() != g.size() || !(share > 0 && share <= 1)) {
    throw std::invalid_argument("legroute.partition requires complete signal trees, grid-sized capacities and a share in (0, 1]");
  }
  Route_cuts out;
  out.load_h.resize(g.size());
  out.load_v.resize(g.size());
  out.critical_h.resize(g.size());
  out.critical_v.resize(g.size());
  out.limit_h.resize(g.size());
  out.limit_v.resize(g.size());
  out.forbidden_h.resize(g.size());
  out.forbidden_v.resize(g.size());
  for (size_t n = 0; n < routes.trees.size(); ++n) {
    for (const auto& [a, b] : routes.trees[n].edges) {
      if (a < 0 || b <= a || size_t(b) >= g.size() || !((b == a + 1 && g.row(a) == g.row(b)) || b == a + g.cols)) {
        throw std::invalid_argument("legroute.partition requires ordered, adjacent tile edges");
      }
      const bool h = g.row(a) == g.row(b);
      ++(h ? out.load_h : out.load_v)[size_t(a)];
      if (critical[n]) {
        ++(h ? out.critical_h : out.critical_v)[size_t(a)];
        (h ? out.forbidden_h : out.forbidden_v)[size_t(a)] = protect_critical;
      }
    }
  }
  for (size_t t = 0; t < g.size(); ++t) {
    for (bool h : {true, false}) {
      if (h ? g.col(int(t)) + 1 >= g.cols : g.row(int(t)) + 1 >= g.rows) {
        continue;
      }
      const size_t q     = t + (h ? 1 : size_t(g.cols));
      const int    limit = std::max(0, int(std::floor(share * std::min((h ? cap_h : cap_v)[t], (h ? cap_h : cap_v)[q]))));
      (h ? out.limit_h : out.limit_v)[t]          = limit;
      (h ? out.forbidden_h : out.forbidden_v)[t] |= (h ? out.load_h : out.load_v)[t] > size_t(limit);
    }
  }
  return out;
}

bool Route_cuts::allows(const Grid& g, const std::vector<int>& regions) const {
  if (regions.size() != g.size() || forbidden_h.size() != g.size() || forbidden_v.size() != g.size()) {
    throw std::invalid_argument("legroute.partition cut checks require grid-sized regions and constraints");
  }
  for (size_t t = 0; t < g.size(); ++t) {
    if (g.col(int(t)) + 1 < g.cols && regions[t] != regions[t + 1] && forbidden_h[t]) {
      return false;
    }
    if (g.row(int(t)) + 1 < g.rows && regions[t] != regions[t + size_t(g.cols)] && forbidden_v[t]) {
      return false;
    }
  }
  return true;
}

size_t Route_cuts::minimum_region_cells(const Grid& g, const std::vector<size_t>& cells, bool capacity_only) const {
  if (cells.size() != g.size() || forbidden_h.size() != g.size() || forbidden_v.size() != g.size()
      || (capacity_only
          && (load_h.size() != g.size() || load_v.size() != g.size() || limit_h.size() != g.size()
              || limit_v.size() != g.size()))) {
    throw std::invalid_argument("legroute.partition lower bound requires grid-sized cells and constraints");
  }
  // Every connected component of forbidden edges must stay in one region, for any cut geometry.
  std::vector<char> seen(g.size());
  std::vector<int>  queue;
  size_t            largest = 0;
  for (size_t seed = 0; seed < g.size(); ++seed) {
    if (seen[seed]) {
      continue;
    }
    seen[seed] = 1;
    queue.assign(1, int(seed));
    size_t count = 0;
    for (size_t at = 0; at < queue.size(); ++at) {
      const int t  = queue[at];
      count       += cells[size_t(t)];
      for (int q : {g.col(t) > 0 ? t - 1 : -1,
                    g.col(t) + 1 < g.cols ? t + 1 : -1,
                    g.row(t) > 0 ? t - g.cols : -1,
                    g.row(t) + 1 < g.rows ? t + g.cols : -1}) {
        if (q < 0 || seen[size_t(q)]) {
          continue;
        }
        const bool h      = g.row(t) == g.row(q);
        const auto edge   = size_t(std::min(t, q));
        const bool joined = capacity_only ? (h ? load_h : load_v)[edge] > size_t((h ? limit_h : limit_v)[edge])
                                          : (h ? forbidden_h : forbidden_v)[edge];
        if (joined) {
          seen[size_t(q)] = 1;
          queue.push_back(q);
        }
      }
    }
    if (!capacity_only || queue.size() > 1) {
      largest = std::max(largest, count);
    }
  }
  return largest;
}

Route_partition partition_routes(const Grid& g, const std::vector<size_t>& cells, const Global_routes& routes,
                                 const std::vector<char>& critical, const std::vector<int>& cap_h, const std::vector<int>& cap_v,
                                 double share, unsigned max_cells, double critical_cost, bool mesh) {
  if (!max_cells || cells.size() != g.size()) {
    throw std::invalid_argument("legroute.partition requires grid-sized cell counts and a positive target");
  }
  if (!std::isfinite(critical_cost) || !(critical_cost == -1 || critical_cost >= 0)) {
    throw std::invalid_argument("legroute.partition critical cost must be -1 or nonnegative");
  }
  const auto      cuts = route_cuts(g, routes, critical, cap_h, cap_v, share, critical_cost < 0);
  Route_partition out;
  out.indivisible_cells = cuts.minimum_region_cells(g, cells);
  out.region.resize(g.size());
  const auto split = [&](auto&& self, int c0, int c1, int r0, int r1) -> void {
    size_t              total = 0;
    std::vector<size_t> columns(size_t(c1 - c0)), rows(size_t(r1 - r0));
    for (int r = r0; r < r1; ++r) {
      for (int c = c0; c < c1; ++c) {
        const size_t count       = cells[size_t(g.tile(c, r))];
        columns[size_t(c - c0)] += count;
        rows[size_t(r - r0)]    += count;
        total                   += count;
      }
    }
    double best   = std::numeric_limits<double>::infinity();
    int    line   = -1;
    bool   column = true;
    if (total > max_cells) {
      for (bool h : {true, false}) {
        size_t      left   = 0;
        const auto& counts = h ? columns : rows;
        for (int k = (h ? c0 : r0) + 1; k < (h ? c1 : r1); ++k) {
          left += counts[size_t(k - (h ? c0 : r0) - 1)];
          if (!left || left == total) {
            continue;
          }
          size_t load = 0, capacity = 0, critical_load = 0;
          bool   allowed = true;
          for (int j = h ? r0 : c0; j < (h ? r1 : c1); ++j) {
            const int    a     = h ? g.tile(k - 1, j) : g.tile(j, k - 1);
            const int    limit = (h ? cuts.limit_h : cuts.limit_v)[size_t(a)];
            const size_t use   = (h ? cuts.load_h : cuts.load_v)[size_t(a)];
            if ((h ? cuts.forbidden_h : cuts.forbidden_v)[size_t(a)]) {
              allowed = false;
              break;
            }
            load          += use;
            critical_load += (h ? cuts.critical_h : cuts.critical_v)[size_t(a)];
            capacity      += size_t(limit);
          }
          if (!allowed) {
            continue;
          }
          const double cost
              = (double(load) + std::max(0.0, critical_cost) * double(critical_load)) / double(std::max<size_t>(1, capacity))
                + std::abs(2.0 * double(left) - double(total)) / double(total);
          if (cost < best) {
            best   = cost;
            line   = k;
            column = h;
          }
        }
      }
    }
    if (line >= 0) {
      if (column) {
        self(self, c0, line, r0, r1);
        self(self, line, c1, r0, r1);
      } else {
        self(self, c0, c1, r0, line);
        self(self, c0, c1, line, r1);
      }
      return;
    }
    out.oversized += total > max_cells;
    out.max_cells  = std::max(out.max_cells, total);
    for (int r = r0; r < r1; ++r) {
      for (int c = c0; c < c1; ++c) {
        out.region[size_t(g.tile(c, r))] = out.regions;
      }
    }
    ++out.regions;
  };
  if (mesh) {
    out.region  = partition_mesh_regions(g, cells, cuts, critical_cost, max_cells);
    out.regions = *std::max_element(out.region.begin(), out.region.end()) + 1;
    std::vector<size_t> counts(size_t(out.regions), 0);
    for (size_t t = 0; t < g.size(); ++t) {
      counts[size_t(out.region[t])] += cells[t];
    }
    for (size_t count : counts) {
      out.oversized += count > max_cells;
      out.max_cells  = std::max(out.max_cells, count);
    }
  } else {
    split(split, 0, g.cols, 0, g.rows);
  }
  for (size_t t = 0; t < g.size(); ++t) {
    for (bool h : {true, false}) {
      if (h ? g.col(int(t)) + 1 >= g.cols : g.row(int(t)) + 1 >= g.rows) {
        continue;
      }
      const size_t q = t + (h ? 1 : size_t(g.cols));
      if (out.region[t] != out.region[q]) {
        out.crossings          += (h ? cuts.load_h : cuts.load_v)[t];
        out.critical_crossings += (h ? cuts.critical_h : cuts.critical_v)[t];
      }
    }
  }
  return out;
}
}  // namespace livehd::placement::lr
