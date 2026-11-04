// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// legroute.global: the region-crossing nets routed over the tile grid ahead of the regions (2l-legroute.html).
//
// Every crossing net gets a tree of tile edges joining its endpoint tiles, by shortest paths on the edges with room:
// an edge carries at most a share of its tracks (the rest stays for the regions' own routes), the fuller an edge the
// costlier, most critical nets first. The regions then see the tree's boundary crossings as hops and its runs through
// the regions holding none of the net's pins as fixed pieces, so a thin region between two macros is never asked to
// carry what its tracks cannot: the paths go around it here.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <numeric>
#include <queue>
#include <tuple>
#include <unordered_set>

#include "fast_route_pattern.hpp"
#include "legroute_internal.hpp"
#include "steiner_topology.hpp"

namespace livehd::placement::lr {
namespace {
struct Search {  // the per-tile state of one search, valid for the stamp of that search only
  std::vector<unsigned> stamp, tree_stamp;
  std::vector<double>   dist;
  std::vector<int>      parent;
  unsigned              gen = 0;
  explicit Search(size_t n) : stamp(n, 0), tree_stamp(n, 0), dist(n, 0), parent(n, -1) {}
};

Pattern_route pattern_tree(const Grid& g, const Global_net& net, const std::function<double(int, int)>& cost, size_t budget) {
  Pattern_route result;
  const auto    tick = [&] {
    if (result.work == budget) {
      result.status = Pattern_route_status::work_limit;
      return false;
    }
    ++result.work;
    return true;
  };
  // Relative physical coordinates preserve rectangular-tile geometry without
  // depending on the die origin or wasting the topology's integer range.
  const int64_t     width = std::max<int64_t>(1, g.tile_w), height = std::max<int64_t>(1, g.tile_h);
  const auto        divisor = std::gcd(width, height);
  const int64_t     sx = width / divisor, sy = height / divisor;
  constexpr int64_t limit = std::numeric_limits<int>::max() / 4;
  if (g.cols - 1 > limit / sx || g.rows - 1 > limit / sy) {
    return result;
  }
  std::vector<Steiner_location> pins;
  for (int t : net.endpoints) {
    if (!tick()) {
      return result;
    }
    pins.emplace_back(g.col(t) * sx, g.row(t) * sy);
  }
  const auto       topology = timing_steiner_topology(pins, 0);
  std::vector<int> vertices;
  for (const auto& [x, y] : topology.locations) {
    if (!tick()) {
      return result;
    }
    if (x % sx || y % sy) {
      return result;
    }
    vertices.push_back(g.tile(int(x / sx), int(y / sy)));
  }
  auto pattern
      = fast_route_pattern(g.cols, g.rows, vertices, topology.edges, cost, budget - result.work, Pattern_route_mode::l_first);
  result.work += pattern.work;
  if (pattern.status != Pattern_route_status::complete) {
    result.status = pattern.status;
    return result;
  }
  // Pattern branches may overlap in cycles. A deterministic BFS spanning tree,
  // restricted to root-to-endpoint paths, removes cycles and dangling Steiner
  // branches before any edge is reserved. Cleanup shares the same work bound.
  std::map<int, std::vector<int>> adjacent;
  for (const auto& [a, b] : pattern.edges) {
    if (!tick()) {
      return result;
    }
    adjacent[a].push_back(b);
    adjacent[b].push_back(a);
  }
  std::map<int, int> parent{
      {net.endpoints.front(), -1}
  };
  std::vector<int> queue{net.endpoints.front()};
  for (size_t at = 0; at < queue.size(); ++at) {
    if (!tick()) {
      return result;
    }
    for (int q : adjacent[queue[at]]) {
      if (!tick()) {
        return result;
      }
      if (parent.emplace(q, queue[at]).second) {
        queue.push_back(q);
      }
    }
  }
  std::unordered_set<int>          kept{net.endpoints.front()};
  std::vector<std::pair<int, int>> edges;
  for (int endpoint : net.endpoints) {
    if (!tick()) {
      return result;
    }
    if (!parent.contains(endpoint)) {
      return result;
    }
    for (int t = endpoint; kept.insert(t).second;) {
      if (!tick()) {
        return result;
      }
      const int p = parent.at(t);
      edges.emplace_back(std::min(t, p), std::max(t, p));
      t = p;
    }
  }
  std::ranges::sort(edges);
  result.edges  = std::move(edges);
  result.status = Pattern_route_status::complete;
  return result;
}
}  // namespace

Global_routes global_route(const Grid& g, const std::vector<int>& cap_h, const std::vector<int>& cap_v, double share,
                           double own_share, const std::vector<Global_net>& nets, unsigned margin_tiles, unsigned rounds,
                           size_t net_pops, double astar_weight, bool pattern_seed) {
  if (!std::isfinite(astar_weight) || astar_weight < 1) {
    throw std::invalid_argument("legroute: global_astar_weight must be finite and at least 1");
  }

  const auto    start = Clock::now();
  Global_routes out;
  out.trees.resize(nets.size());
  const size_t     N = g.size();
  // the room of each edge (keyed by its left / lower tile): the share of the narrower tile's tracks
  std::vector<int> lim_h(N, 0), lim_v(N, 0), use_h(N, 0), use_v(N, 0);
  // negotiated congestion: an edge over its room costs its overuse times a present factor that doubles every round,
  // plus the history of its overuse in the rounds before, so the nets that can detour do and the ones that cannot keep
  // the edge (PathFinder / FastRoute). With rounds == 0 a full edge is closed and a net without a path keeps the chain
  std::vector<double> hist_h(N, 0), hist_v(N, 0);
  double              pres = 1;
  for (size_t t = 0; t < N; ++t) {
    const int c = g.col(int(t)), r = g.row(int(t));
    if (c + 1 < g.cols) {
      lim_h[t] = int(std::floor(share * double(std::min(cap_h[t], cap_h[t + 1]))));
    }
    if (r + 1 < g.rows) {
      lim_v[t] = int(std::floor(share * double(std::min(cap_v[t], cap_v[t + size_t(g.cols)]))));
    }
  }
  const auto       check_topology = [](const Global_net& net) { return net.fixed_edges.empty() && net.own_regions.empty(); };
  std::vector<int> component;
  if (std::ranges::any_of(nets, [&](const auto& net) { return net.endpoints.size() > 1 && check_topology(net); })) {
    // This whole-grid graph contains every edge a counted search can use in
    // any window/negotiation round. Occupancy can only remove edges; costs do
    // not change connectivity. Fixed hops may bridge zero capacity, and own
    // regions may bypass a rounded-to-zero share, so those nets bypass it.
    component.assign(N, -1);
    std::vector<int> queue;
    int              label = 0;
    for (size_t seed = 0; seed < N; ++seed) {
      if (component[seed] >= 0) {
        continue;
      }
      component[seed] = label;
      queue.assign(1, int(seed));
      for (size_t at = 0; at < queue.size(); ++at) {
        const int  t     = queue[at];
        const auto visit = [&](int q, int limit) {
          if (limit > 0 && component[size_t(q)] < 0) {
            component[size_t(q)] = label;
            queue.push_back(q);
          }
        };
        if (g.col(t) + 1 < g.cols) {
          visit(t + 1, lim_h[size_t(t)]);
        }
        if (g.col(t) > 0) {
          visit(t - 1, lim_h[size_t(t - 1)]);
        }
        if (g.row(t) + 1 < g.rows) {
          visit(t + g.cols, lim_v[size_t(t)]);
        }
        if (g.row(t) > 0) {
          visit(t - g.cols, lim_v[size_t(t - g.cols)]);
        }
      }
      ++label;
    }
  }
  std::vector<size_t> order(nets.size());
  std::iota(order.begin(), order.end(), 0);
  const auto span = [&](const Global_net& n) {
    int c0 = g.cols, c1 = -1, r0 = g.rows, r1 = -1;
    for (int t : n.endpoints) {
      c0 = std::min(c0, g.col(t));
      c1 = std::max(c1, g.col(t));
      r0 = std::min(r0, g.row(t));
      r1 = std::max(r1, g.row(t));
    }
    return std::array{c0, c1, r0, r1};
  };
  // most critical first; among the rest the short nets first (few ways to go), the long ones detour around them
  std::ranges::stable_sort(order, [&](size_t a, size_t b) {
    if (nets[a].crit != nets[b].crit) {
      return nets[a].crit > nets[b].crit;
    }
    const auto sa = span(nets[a]), sb = span(nets[b]);
    return (sa[1] - sa[0]) + (sa[3] - sa[2]) < (sb[1] - sb[0]) + (sb[3] - sb[2]);
  });
  Search S(N);
  // A* for small endpoint sets. Large broadcasts retain the incremental Dijkstra
  // frontier; scanning every remaining sink per expansion would be quadratic.
  using Item = std::tuple<double, double, int, size_t>;  // f, -g, tile, goal epoch
  std::vector<int>      tree_tiles;
  std::vector<unsigned> pend(N, 0);  // pending endpoint: pend[t] == this net's mark
  std::vector<unsigned> own(size_t(g.regions), 0);  // own region of the net: own[r] == mark
  unsigned              mark = 0;
  double                next_progress = 30;
  // an edge inside a region of the net's own pins takes no room (the region routes it) unless own_share counts it
  const auto counted = [&](const Global_net& net, int a, int b) {
    return own_share > 0 || g.region[a] != g.region[b] || std::ranges::find(net.own_regions, g.region[a]) == net.own_regions.end();
  };
  const auto unroute = [&](size_t k) {
    auto& tree = out.trees[k];
    for (const auto& [a, b] : tree.edges) {
      if (counted(nets[k], a, b)) {
        --(g.row(a) == g.row(b) ? use_h : use_v)[size_t(a)];
      }
    }
    tree.edges.clear();
    tree.routed = false;
    tree.search_limited = false;
  };
  const auto counted_cost = [&](int a, int b, int e, int h, bool allow_over, double length) -> double {
    const int lim = (h ? lim_h : lim_v)[size_t(e)], use = (h ? use_h : use_v)[size_t(e)];
    if (lim <= 0 || (!allow_over && use >= lim)) {
      return INFINITY;
    }
    double cost = length + 2.0 * double(use + 1) / double(lim) + (g.region[a] != g.region[b] ? 1 : 0);
    if (const int over = use + 1 - lim; over > 0) {
      cost += pres * double(over);
    }
    cost += (h ? hist_h : hist_v)[size_t(e)];
    return cost;
  };
  // the net's tree: a search joins the nearest pending endpoint to the tree; the box grows twice (4x and 16x the
  // margin, plus a quarter then all of the net's span) before the net is given up: a whole-die search per
  // unroutable net cost a large design its minutes
  const auto route_one = [&](size_t k, bool allow_over) {
    const auto& net  = nets[k];
    auto&       tree = out.trees[k];
    tree.edges.clear();
    tree.routed = false;
    tree.last_attempt_pops = 0;
    tree.search_limited = tree.topology_disconnected = false;
    ++tree.search_attempts;
    if (net.endpoints.size() < 2) {
      tree.routed = true;
      return true;
    }
    if (check_topology(net) && std::ranges::any_of(net.endpoints, [&](int t) {
          return component[size_t(t)] != component[size_t(net.endpoints.front())];
        })) {
      tree.topology_disconnected = true;
      return false;  // zero queue pops, no partial reservation, not a search-budget failure
    }
    if (pattern_seed && check_topology(net) && net.endpoints.size() <= 32) {
      ++out.pattern_attempts;
      const double unit = double(std::max<int64_t>(1, std::min(g.tile_w, g.tile_h)));
      const auto   cost = [&](int a, int b) {
        const bool   h      = g.row(a) == g.row(b);
        // Pattern candidates respect hard remaining capacity, even during a
        // negotiated round; the unchanged maze may negotiate after fallback.
        const double length = double(std::max<int64_t>(1, h ? g.tile_w : g.tile_h)) / unit;
        return counted_cost(a, b, a, h, false, length);
      };
      const auto budget  = net_pops ? std::min<size_t>(net_pops, 100000) : 100000;
      auto       seed    = pattern_tree(g, net, cost, budget);
      out.pattern_work  += seed.work;
      if (seed.status == Pattern_route_status::complete) {
        ++out.pattern_completed;
        tree.routed = true;
        tree.edges  = std::move(seed.edges);
        for (const auto& [a, b] : tree.edges) {
          ++(g.row(a) == g.row(b) ? use_h : use_v)[size_t(a)];
        }
        return true;
      }
      ++out.pattern_fallbacks;
      out.pattern_work_limited += seed.status == Pattern_route_status::work_limit;
    }
    ++mark;
    for (int r : net.own_regions) {
      own[size_t(r)] = mark;
    }
    std::unordered_set<int64_t> free_edges;  // (lower tile, h): the net's own fixed edges cost nothing
    for (const auto& [a, b] : net.fixed_edges) {
      const int lo = std::min(a, b);
      free_edges.insert(int64_t(lo) * 2 + (g.row(a) == g.row(b) ? 1 : 0));
    }
    const auto sp = span(net);
    for (int t : net.endpoints) {
      pend[size_t(t)] = mark;
    }
    tree_tiles.assign(1, net.endpoints.front());
    pend[size_t(net.endpoints.front())] = 0;
    std::vector<std::pair<int, int>> edges;
    size_t                           left         = net.endpoints.size() - 1;
    const size_t                     initial_pops = out.search_pops;
    bool                             limited      = false;
    const bool                       guided       = net.fixed_edges.empty() && net.endpoints.size() <= 32;
    const auto                       heuristic    = [&](int t) {
      int nearest = g.cols + g.rows;
      if (!guided || !left) {
        return 0.0;
      }
      for (int goal : net.endpoints) {
        if (pend[size_t(goal)] == mark) {
          nearest = std::min(nearest, std::abs(g.col(t) - g.col(goal)) + std::abs(g.row(t) - g.row(goal)));
        }
      }
      // Every nonfixed edge costs at least one. A weight above one deliberately
      // sacrifices shortest-path optimality, while the same edge checks and
      // complete-tree construction still determine whether this net is routed.
      return astar_weight * double(nearest);
    };
    // Edge costs do not change while this net is being built. Keep the distance labels and frontier between
    // sinks, lowering newly attached path tiles to zero-distance sources. Reopening improved labels is the
    // incremental multi-source Dijkstra update; rebuilding from the whole tree per sink was quadratic in fanout.
    // Only a larger search window needs a restart, because previously clipped neighbors were not relaxed.
    for (int grow = 0; grow < 3 && left && !limited; ++grow) {
      const int len    = (sp[1] - sp[0]) + (sp[3] - sp[2]);
      const int margin = int(margin_tiles) * (1 << (2 * grow)) + (grow == 0 ? len / 8 : grow == 1 ? len / 4 : len);
      const int c0 = std::max(0, sp[0] - margin), c1 = std::min(g.cols - 1, sp[1] + margin);
      const int r0 = std::max(0, sp[2] - margin), r1 = std::min(g.rows - 1, sp[3] + margin);
      ++S.gen;
      ++out.search_restarts;
      std::priority_queue<Item, std::vector<Item>, std::greater<>> heap;
      for (int t : tree_tiles) {
        S.stamp[size_t(t)]      = S.gen;
        S.tree_stamp[size_t(t)] = S.gen;
        S.dist[size_t(t)]       = 0;
        S.parent[size_t(t)]     = -1;
        heap.emplace(heuristic(t), 0.0, t, left);
        ++out.search_pushes;
      }
      while (!heap.empty() && left) {
        if (net_pops && out.search_pops - initial_pops >= net_pops) {
          limited = true;
          ++out.search_limited;
          break;
        }
        const auto [priority, negative_distance, t, epoch] = heap.top();
        const double dd                                    = -negative_distance;
        heap.pop();
        ++out.search_pops;
        if (out.search_pops % 65536 == 0 && seconds_since(start) >= next_progress) {
          std::fprintf(stderr,
                       "legroute global search: net %zu (%zu pins), %zu pending, %zu queue pops, %.1f s\n",
                       k,
                       net.endpoints.size(),
                       left,
                       out.search_pops,
                       seconds_since(start));
          next_progress = seconds_since(start) + 30;
        }
        if (dd > S.dist[size_t(t)]) {
          continue;
        }
        if (guided && epoch != left) {
          // Removing a goal only increases its heuristic (also when weighted).
          // Rekey stale entries to preserve the current search-priority order.
          heap.emplace(dd + heuristic(t), -dd, t, left);
          ++out.search_pushes;
          continue;
        }
        if (pend[size_t(t)] == mark) {
          pend[size_t(t)] = 0;
          --left;
          for (int q = t; S.tree_stamp[size_t(q)] != S.gen;) {
            const int p = S.parent[size_t(q)];
            edges.emplace_back(std::min(p, q), std::max(p, q));
            S.tree_stamp[size_t(q)] = S.gen;
            tree_tiles.push_back(q);
            S.dist[size_t(q)]   = 0;
            S.parent[size_t(q)] = -1;
            heap.emplace(heuristic(q), 0.0, q, left);
            ++out.search_pushes;
            q = p;
          }
          continue;
        }
        const int                               c = g.col(t), r = g.row(t);
        // the four neighbors: (tile, edge key tile, h)
        const std::array<std::array<int, 3>, 4> nb{
            {{c + 1 <= c1 ? t + 1 : -1, t, 1},
             {c - 1 >= c0 ? t - 1 : -1, t - 1, 1},
             {r + 1 <= r1 ? t + g.cols : -1, t, 0},
             {r - 1 >= r0 ? t - g.cols : -1, t - g.cols, 0}}
        };
        for (const auto& [q, e, h] : nb) {
          if (q < 0 || (S.stamp[size_t(q)] == S.gen && S.tree_stamp[size_t(q)] == S.gen)) {
            continue;
          }
          double cost = 0;
          if (free_edges.contains(int64_t(e) * 2 + h)) {
          } else if (g.region[t] == g.region[q] && own[size_t(g.region[t])] == mark) {
            // inside a region of the net's own pins: the region routes it, nothing is reserved here; an edge
            // without tracks (a macro) stays closed, or a hop could land in a pocket the region cannot reach.
            // With own_share the edge's load still costs (never closes): the exit that loads the region's
            // internal cuts least wins
            const int mc = std::min((h ? cap_h : cap_v)[size_t(t)], (h ? cap_h : cap_v)[size_t(q)]);
            if (mc <= 0) {
              continue;
            }
            cost = 1;
            if (own_share > 0) {
              const int lim_own  = std::max(1, int(std::floor(own_share * mc)));
              cost              += 2.0 * double((h ? use_h : use_v)[size_t(e)] + 1) / double(lim_own);
            }
          } else {
            cost = counted_cost(t, q, e, h, allow_over, 1);
            if (!std::isfinite(cost)) {
              continue;  // no tracks at all, or no room and no negotiation
            }
          }
          const double nd = dd + cost;
          if (S.stamp[size_t(q)] != S.gen || nd < S.dist[size_t(q)]) {
            S.stamp[size_t(q)]  = S.gen;
            S.dist[size_t(q)]   = nd;
            S.parent[size_t(q)] = t;
            heap.emplace(nd + heuristic(q), -nd, q, left);
            ++out.search_pushes;
          }
        }
      }
    }
    for (int t : net.endpoints) {  // a net given up leaves no pending marks behind
      pend[size_t(t)] = 0;
    }
    tree.last_attempt_pops  = out.search_pops - initial_pops;
    tree.search_pops       += tree.last_attempt_pops;
    if (left) {
      tree.search_limited = limited;
      return false;
    }
    tree.routed = true;
    tree.edges  = std::move(edges);
    for (const auto& [a, b] : tree.edges) {  // the hops and the pass-through runs take room
      if (counted(net, a, b)) {
        ++(g.row(a) == g.row(b) ? use_h : use_v)[size_t(a)];
      }
    }
    return true;
  };
  for (const size_t k : order) {
    route_one(k, rounds > 0);
  }
  // negotiation: the nets through an edge over its room reroute under the raised cost, until no edge is over
  std::vector<char> rip(nets.size(), 0);
  for (unsigned round = 1; round <= rounds; ++round) {
    size_t over_edges = 0;
    for (size_t t = 0; t < N; ++t) {
      if (use_h[t] > lim_h[t]) {
        hist_h[t] += double(use_h[t] - lim_h[t]) / double(std::max(1, lim_h[t]));
        ++over_edges;
      }
      if (use_v[t] > lim_v[t]) {
        hist_v[t] += double(use_v[t] - lim_v[t]) / double(std::max(1, lim_v[t]));
        ++over_edges;
      }
    }
    if (over_edges == 0) {
      break;
    }
    out.rounds = round;
    pres       = std::min(pres * 2, 4096.0);
    std::ranges::fill(rip, 0);
    for (size_t k = 0; k < nets.size(); ++k) {
      for (const auto& [a, b] : out.trees[k].edges) {
        const bool h = g.row(a) == g.row(b);
        if ((h ? use_h : use_v)[size_t(a)] > (h ? lim_h : lim_v)[size_t(a)] && counted(nets[k], a, b)) {
          rip[k] = 1;
          break;
        }
      }
    }
    for (const size_t k : order) {
      if (rip[k]) {
        unroute(k);
      }
    }
    for (const size_t k : order) {
      if (rip[k]) {
        route_one(k, true);
      }
    }
  }
  for (size_t k = 0; k < nets.size(); ++k) {
    out.unrouted += !out.trees[k].routed;
    out.topology_disconnected += out.trees[k].topology_disconnected;
  }
  for (size_t t = 0; t < N; ++t) {
    for (int h : {1, 0}) {
      const int lim = (h ? lim_h : lim_v)[t], use = (h ? use_h : use_v)[t];
      if (lim > 0 && use >= lim) {
        ++out.edges_full;
      }
      if (use > lim) {
        ++out.overflow_edges;
        out.overflow_tracks += size_t(use - lim);
        out.over.emplace_back(int(t), h);
      }
    }
  }
  out.seconds = seconds_since(start);
  return out;
}
}  // namespace livehd::placement::lr
