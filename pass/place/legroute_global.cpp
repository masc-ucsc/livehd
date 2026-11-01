// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// legroute.global: the region-crossing nets routed over the tile grid ahead of the regions (2l-legroute.html).
//
// Every crossing net gets a tree of tile edges joining its endpoint tiles, by shortest paths on the edges with room:
// an edge carries at most a share of its tracks (the rest stays for the regions' own routes), the fuller an edge the
// costlier, most critical nets first. The regions then see the tree's boundary crossings as hops and its runs through
// the regions holding none of the net's pins as fixed pieces, so a thin region between two macros is never asked to
// carry what its tracks cannot: the paths go around it here.
#include <cmath>
#include <queue>
#include <unordered_set>

#include "legroute_internal.hpp"

namespace livehd::placement::lr {
namespace {
struct Search {  // the per-tile state of one search, valid for the stamp of that search only
  std::vector<unsigned> stamp, tree_stamp;
  std::vector<double>   dist;
  std::vector<int>      parent;
  unsigned              gen = 0;
  explicit Search(size_t n) : stamp(n, 0), tree_stamp(n, 0), dist(n, 0), parent(n, -1) {}
};
}  // namespace

Global_routes global_route(const Grid& g, const std::vector<int>& cap_h, const std::vector<int>& cap_v, double share, double own_share,
                           const std::vector<Global_net>& nets, unsigned margin_tiles) {
  const auto    start = Clock::now();
  Global_routes out;
  out.trees.resize(nets.size());
  const size_t     N = g.size();
  // the room of each edge (keyed by its left / lower tile): the share of the narrower tile's tracks
  std::vector<int> lim_h(N, 0), lim_v(N, 0), use_h(N, 0), use_v(N, 0);
  for (size_t t = 0; t < N; ++t) {
    const int c = g.col(int(t)), r = g.row(int(t));
    if (c + 1 < g.cols) {
      lim_h[t] = int(std::floor(share * double(std::min(cap_h[t], cap_h[t + 1]))));
    }
    if (r + 1 < g.rows) {
      lim_v[t] = int(std::floor(share * double(std::min(cap_v[t], cap_v[t + size_t(g.cols)]))));
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
  using Item = std::pair<double, int>;
  std::vector<int>      tree_tiles;
  std::vector<unsigned> pend(N, 0);  // pending endpoint: pend[t] == this net's mark
  std::vector<unsigned> own(size_t(g.regions), 0);  // own region of the net: own[r] == mark
  unsigned              mark = 0;
  for (const size_t k : order) {
    const auto& net  = nets[k];
    auto&       tree = out.trees[k];
    if (net.endpoints.size() < 2) {
      tree.routed = true;
      continue;
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
    // the net's endpoints still to join: the first (the driver's region's first endpoint) seeds the tree
    for (int t : net.endpoints) {
      pend[size_t(t)] = mark;
    }
    tree_tiles.assign(1, net.endpoints.front());
    pend[size_t(net.endpoints.front())] = 0;
    std::vector<std::pair<int, int>> edges;
    bool                             ok = true;
    // a search joins the nearest pending endpoint to the tree; the box grows twice (4x and 16x the margin, plus
    // a quarter then all of the net's span) before the net is left to the hop chain: a whole-die search per
    // unroutable net cost a large design its minutes
    for (size_t left = net.endpoints.size() - 1; left && ok;) {
      bool joined = false;
      for (int grow = 0; grow < 3 && !joined; ++grow) {
        const int len    = (sp[1] - sp[0]) + (sp[3] - sp[2]);
        const int margin = int(margin_tiles) * (1 << (2 * grow)) + (grow == 0 ? len / 8 : grow == 1 ? len / 4 : len);
        const int c0 = std::max(0, sp[0] - margin), c1 = std::min(g.cols - 1, sp[1] + margin);
        const int r0 = std::max(0, sp[2] - margin), r1 = std::min(g.rows - 1, sp[3] + margin);
        ++S.gen;
        std::priority_queue<Item, std::vector<Item>, std::greater<>> heap;
        for (int t : tree_tiles) {
          S.stamp[size_t(t)]      = S.gen;
          S.tree_stamp[size_t(t)] = S.gen;
          S.dist[size_t(t)]       = 0;
          S.parent[size_t(t)]     = -1;
          heap.emplace(0.0, t);
        }
        int hit = -1;
        while (!heap.empty() && hit < 0) {
          const auto [dd, t] = heap.top();
          heap.pop();
          if (dd > S.dist[size_t(t)]) {
            continue;
          }
          if (pend[size_t(t)] == mark) {
            hit = t;
            break;
          }
          const int c = g.col(t), r = g.row(t);
          // the four neighbors: (tile, edge key tile, h)
          const std::array<std::array<int, 3>, 4> nb{{{c + 1 <= c1 ? t + 1 : -1, t, 1},
                                                      {c - 1 >= c0 ? t - 1 : -1, t - 1, 1},
                                                      {r + 1 <= r1 ? t + g.cols : -1, t, 0},
                                                      {r - 1 >= r0 ? t - g.cols : -1, t - g.cols, 0}}};
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
                const int lim_own = std::max(1, int(std::floor(own_share * mc)));
                cost += 2.0 * double((h ? use_h : use_v)[size_t(e)] + 1) / double(lim_own);
              }
            } else {
              const int lim = (h ? lim_h : lim_v)[size_t(e)], use = (h ? use_h : use_v)[size_t(e)];
              if (lim <= 0 || use >= lim) {
                continue;  // no room on the edge
              }
              // a boundary crossing costs one more: a path hugging a boundary should not hop back and forth
              cost = 1 + 2.0 * double(use + 1) / double(lim) + (g.region[t] != g.region[q] ? 1 : 0);
            }
            const double nd = dd + cost;
            if (S.stamp[size_t(q)] != S.gen || nd < S.dist[size_t(q)]) {
              S.stamp[size_t(q)]  = S.gen;
              S.dist[size_t(q)]   = nd;
              S.parent[size_t(q)] = t;
              heap.emplace(nd, q);
            }
          }
        }
        if (hit < 0) {
          continue;
        }
        for (int t = hit; S.tree_stamp[size_t(t)] != S.gen; t = S.parent[size_t(t)]) {  // back to the tree
          const int p = S.parent[size_t(t)];
          edges.emplace_back(std::min(p, t), std::max(p, t));
          S.tree_stamp[size_t(t)] = S.gen;
          tree_tiles.push_back(t);
        }
        pend[size_t(hit)] = 0;
        --left;
        joined = true;
      }
      ok = joined;
    }
    if (!ok) {
      ++out.unrouted;
      continue;
    }
    tree.routed = true;
    tree.edges  = std::move(edges);
    for (const auto& [a, b] : tree.edges) {  // the hops and the pass-through runs take room; own regions only with own_share
      if (g.region[a] == g.region[b] && own[size_t(g.region[a])] == mark && own_share <= 0) {
        continue;
      }
      const bool h   = g.row(a) == g.row(b);
      auto&      use = (h ? use_h : use_v)[size_t(a)];
      ++use;
      out.edges_full += use >= (h ? lim_h : lim_v)[size_t(a)];
    }
  }
  out.seconds = seconds_since(start);
  return out;
}
}  // namespace livehd::placement::lr
