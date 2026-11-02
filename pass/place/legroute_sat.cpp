// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// legroute.local placement: per region, a SAT model places every cell-box on a free tile so that the box-to-box
// routes fit the tracks by construction. Each connection is straight or one of its two L-shapes (one bit); a leg
// covers every tile from end to end in its row or column; a tile counts a net once per direction however many of
// its legs cover it; and per tile edge and direction the nets crossing the edge stay within the tracks of the
// narrower tile less the edges the routes fixed ahead (hops, trees, IO pre-routes) cross. The in-box wires keep to
// their own tile (the boxing pin limit and legroute.final bound them). The bounds are lazy (added when an answer
// overflows them) and the fallback placement is tried first as assumptions. Any answer is routable. Each box
// stays in a window around its placement position (window_tiles), which keeps the model linear in the region size.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <numeric>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <unordered_map>

#include "algorithms/flow/push_relabel.h"  // VieCut: the s-t min cuts that pick a failing region's spill
#include "data_structure/mutable_graph.h"
#include "legroute_internal.hpp"
#include "sat_solver.hpp"

namespace livehd::placement::lr {
namespace {
// Packing first: every region must hold its boxes on tiles they fit (taps and fixed cells narrow tiles, so a count
// is not enough; an infeasible packing would leave the SAT a pigeonhole proof). Per region a bipartite matching of
// boxes to fitting tiles; a box left over moves to the nearest region with a free tile it fits, through that
// region's augmenting paths. Returns the moves.
size_t balance_regions(const Design& d, const Grid& g, Boxes& boxes) {
  std::vector<std::vector<int>> tiles(g.regions);
  for (int t = 0; t < int(g.size()); ++t) {
    if (!g.blocked[t]) {
      tiles[g.region[t]].push_back(t);
    }
  }
  std::vector<std::unordered_map<int, int>> owner(g.regions);  // per region: tile -> box
  std::vector<char>                         seen(g.size(), 0);
  std::vector<int>                          touched;
  const auto                                dist = [&](int b, int t) {
    return std::abs(double(g.cx(g.col(t))) - boxes.boxes[b].cx) + std::abs(double(g.cy(g.row(t))) - boxes.boxes[b].cy);
  };
  std::function<bool(int, int)> augment = [&](int r, int b) {
    std::vector<int> cand;
    for (int t : tiles[r]) {
      if (!seen[t] && box_fits(d, g, boxes.boxes[b], t)) {
        cand.push_back(t);
      }
    }
    std::ranges::sort(cand, [&](int x, int y) { return dist(b, x) < dist(b, y); });
    for (int t : cand) {
      if (seen[t]) {
        continue;
      }
      seen[t] = 1;
      touched.push_back(t);
      const auto it = owner[r].find(t);
      if (it == owner[r].end() || augment(r, it->second)) {
        owner[r][t] = b;
        return true;
      }
    }
    return false;
  };
  const auto try_region = [&](int r, int b) {
    const bool ok = augment(r, b);
    for (int t : touched) {
      seen[t] = 0;
    }
    touched.clear();
    return ok;
  };
  std::vector<int> left;
  for (int b = 0; b < int(boxes.boxes.size()); ++b) {
    if (!try_region(boxes.boxes[b].region, b)) {
      left.push_back(b);
    }
  }
  size_t moved = 0;
  for (int b : left) {
    std::vector<std::pair<double, int>> order;  // regions by their nearest tile to the box
    for (int r = 0; r < g.regions; ++r) {
      double best = INFINITY;
      for (int t : tiles[r]) {
        best = std::min(best, dist(b, t));
      }
      order.emplace_back(best, r);
    }
    std::ranges::sort(order);
    bool placed = false;
    for (const auto& [_, r] : order) {
      if (r != boxes.boxes[b].region && try_region(r, b)) {
        boxes.boxes[b].region = r;
        ++moved;
        placed = true;
        break;
      }
    }
    if (!placed) {
      throw Legroute_unfit(std::format("legroute: no free tile fits box {}", b));
    }
  }
  return moved;
}

// CNF with a constant-true literal: clauses fold constants away.
struct Cnf {
  Sat_solver solver;
  int        t;  // the true literal
  size_t     clauses = 0, variables = 1;
  Cnf() : solver(Sat_backend::cadical) {
    t = solver.new_variable();
    solver.clause({t});
  }
  int var() {
    ++variables;
    return solver.new_variable();
  }
  void add(std::initializer_list<int> lits) {
    int    buf[8];
    size_t n = 0;
    for (int l : lits) {
      if (l == t) {
        return;
      }
      if (l != -t) {
        buf[n++] = l;
      }
    }
    solver.clause(std::span<const int>(buf, n));
    ++clauses;
  }
  void add(const std::vector<int>& lits) {
    std::vector<int> c;
    for (int l : lits) {
      if (l == t) {
        return;
      }
      if (l != -t) {
        c.push_back(l);
      }
    }
    solver.clause(c);
    ++clauses;
  }
  // at most k of xs: a totalizer whose unary counts stop at k + 1 (only the upward implications)
  std::vector<int> count(const std::vector<int>& xs, size_t lo, size_t hi, int k) {
    if (hi - lo == 1) {
      return {xs[lo]};
    }
    const size_t     mid = (lo + hi) / 2;
    const auto       a = count(xs, lo, mid, k), b = count(xs, mid, hi, k);
    const int        m = std::min<int>(int(a.size() + b.size()), k + 1);
    std::vector<int> c(m);
    for (auto& v : c) {
      v = var();
    }
    for (int i = 0; i < int(a.size()); ++i) {
      add({-a[i], c[std::min(i, m - 1)]});
    }
    for (int j = 0; j < int(b.size()); ++j) {
      add({-b[j], c[std::min(j, m - 1)]});
    }
    for (int i = 0; i < int(a.size()); ++i) {
      for (int j = 0; j < int(b.size()) && i + j + 1 < m; ++j) {
        add({-a[i], -b[j], c[i + j + 1]});
      }
    }
    return c;
  }
  // the literal "more than k of xs" (0: never), for a bound an assumption holds
  int over(const std::vector<int>& xs, int k) {
    if (int(xs.size()) <= k) {
      return 0;
    }
    if (k <= 0) {
      const int o = var();
      for (int x : xs) {
        add({-x, o});
      }
      return o;
    }
    return count(xs, 0, xs.size(), k)[k];
  }
  void at_most(const std::vector<int>& xs, int k) {
    if (int(xs.size()) <= k) {
      return;
    }
    if (k <= 0) {
      for (int x : xs) {
        add({-x});
      }
      return;
    }
    if (k == 1) {  // pairwise-free ladder for one
      int prev = 0;
      for (size_t i = 0; i < xs.size(); ++i) {
        if (prev) {
          add({-xs[i], -prev});
        }
        if (i + 1 < xs.size()) {
          const int s = var();
          add({-xs[i], s});
          if (prev) {
            add({-prev, s});
          }
          prev = s;
        }
      }
      return;
    }
    const auto c = count(xs, 0, xs.size(), k);
    add({-c[k]});
  }
};

// diagnostic: clauses Cnf::at_most(n inputs, k) adds (the same totalizer recursion, nothing built)
size_t at_most_clauses(int n, int k) {
  if (n <= k) {
    return 0;
  }
  if (k <= 0) {
    return size_t(n);
  }
  if (k == 1) {
    return 3 * size_t(n);
  }
  std::map<int, std::pair<int, size_t>>      memo;  // inputs -> (outputs, clauses)
  std::function<std::pair<int, size_t>(int)> count = [&](int s) -> std::pair<int, size_t> {
    if (s == 1) {
      return {1, 0};
    }
    if (const auto it = memo.find(s); it != memo.end()) {
      return it->second;
    }
    const auto [a, ca] = count(s / 2);
    const auto [b, cb] = count(s - s / 2);
    const int m        = std::min(a + b, k + 1);
    size_t    c        = ca + cb + size_t(a) + size_t(b);
    for (int i = 0; i < a; ++i) {
      c += size_t(std::max(0, std::min(b, m - 1 - i)));
    }
    return memo[s] = {m, c};
  };
  return count(n).second + 1;
}

struct Endpoint {
  int box = -1, tile = -1;  // a box of the region (placed by the solver) or a fixed tile
  int hop = -1;             // hop_choice: a free hop, its tile chosen by this region's SAT among the hop's candidates
                            // (tile stays the hop chooser's tile for every reader outside the model)
};
struct Region_conn {
  int net = -1, a = -1, b = -1;  // endpoint ids of the region
};
struct Net_ends {
  std::map<int, std::vector<Endpoint>> per_region;
  int                                  driver_region = -1;
};

// Bipartite matching of items to their candidate tiles (augmenting paths, candidates in preference order). The
// tile of each item, or empty when some item finds none.
std::vector<int> match_tiles(const std::vector<std::vector<int>>& pref, size_t tiles) {
  std::unordered_map<int, int> owner;  // tile -> item
  std::vector<char>            seen(tiles, 0);
  std::vector<int>             touched;
  std::function<bool(int)>     augment = [&](int i) {
    for (int t : pref[i]) {
      if (seen[t]) {
        continue;
      }
      seen[t] = 1;
      touched.push_back(t);
      const auto it = owner.find(t);
      if (it == owner.end() || augment(it->second)) {
        owner[t] = i;
        return true;
      }
    }
    return false;
  };
  for (int i = 0; i < int(pref.size()); ++i) {
    const bool ok = augment(i);
    for (int t : touched) {
      seen[t] = 0;
    }
    touched.clear();
    if (!ok) {
      return {};
    }
  }
  std::vector<int> tile(pref.size(), -1);
  for (const auto& [t, i] : owner) {
    tile[i] = t;
  }
  return tile;
}

// Critical pre-placement: in each region, the crit_boxes fraction of its boxes with the highest criticality take tiles for timing
// before the SAT, which then keeps them fixed (fewer free boxes, the critical routes known). Per region, critical
// boxes go greedily (most critical first) to the tile in their window with the least weighted star length of their
// nets (Manhattan tiles plus half a tile per bend: short and straight for RC), the other endpoints at their
// placement tiles; a few passes then move each to a better free tile. Critical boxes that are neighbors without a
// shared net pay a spread penalty, so routes around them keep room. A region whose other boxes no longer match
// its free tiles loses its pins. Marks Box::fixed and Box::tile.
void place_critical(const Design& d, const Legroute_options& o, const Grid& g, Boxes& boxes, const std::vector<Net_ends>& ends,
                    Legroute_report& rep) {
  std::vector<int> order;
  for (int b = 0; b < int(boxes.boxes.size()); ++b) {
    if (boxes.boxes[b].crit > 0) {
      order.push_back(b);
    }
  }
  std::ranges::stable_sort(order, [&](int a, int b) {
    const auto &x = boxes.boxes[a], &y = boxes.boxes[b];
    return x.crit != y.crit ? x.crit > y.crit : x.ramp > y.ramp;
  });
  {  // the top fraction of each region (critical boxes often crowd a few regions)
    std::vector<size_t> in_region(g.regions, 0), quota(g.regions, 0);
    for (const auto& box : boxes.boxes) {
      ++in_region[box.region];
    }
    for (int r = 0; r < g.regions; ++r) {
      quota[r] = size_t(std::ceil(o.crit_boxes * double(in_region[r])));
    }
    std::erase_if(order, [&](int b) {
      auto& q = quota[boxes.boxes[b].region];
      return q == 0 || (--q, false);
    });
  }
  if (order.empty()) {
    return;
  }
  std::vector<int> home(boxes.boxes.size()), cur(boxes.boxes.size(), -1);  // placement tile, chosen tile (pinned)
  for (size_t b = 0; b < boxes.boxes.size(); ++b) {
    home[b] = g.tile_of(boxes.boxes[b].cx, boxes.boxes[b].cy);
  }
  // per pinned box: its nets with two or more endpoints in its region
  std::unordered_map<int, std::vector<int>> nets_of;
  std::set<int>                             all_nets;
  for (int b : order) {
    std::set<int> seen;
    for (int c : boxes.boxes[b].cells) {
      for (int n : d.nets_of[c]) {
        const auto it = ends[n].per_region.find(boxes.boxes[b].region);
        if (it != ends[n].per_region.end() && it->second.size() >= 2 && seen.insert(n).second) {
          nets_of[b].push_back(n);
          all_nets.insert(n);
        }
      }
    }
  }
  const auto pos      = [&](const Endpoint& e) { return e.box < 0 ? e.tile : cur[e.box] >= 0 ? cur[e.box] : home[e.box]; };
  const auto net_cost = [&](int n, int region) {
    const auto& list = ends[n].per_region.at(region);
    const int   t0   = pos(list[0]);
    double      cost = 0;
    for (size_t k = 1; k < list.size(); ++k) {
      const int t = pos(list[k]), dc = std::abs(g.col(t) - g.col(t0)), dr = std::abs(g.row(t) - g.row(t0));
      cost += dc + dr + (dc && dr ? .5 : 0);
    }
    return cost * d.nets[n].weight;
  };
  const auto total = [&] {
    double cost = 0;
    for (int n : all_nets) {
      for (const auto& [region, list] : ends[n].per_region) {
        if (list.size() >= 2) {
          cost += net_cost(n, region);
        }
      }
    }
    return cost;
  };
  rep.crit_cost_before += total();
  std::vector<char>            taken(g.size(), 0);
  std::unordered_map<int, int> at;  // tile -> pinned box
  constexpr double             spread   = 4;
  const auto                   box_cost = [&](int b, int t) {
    const int was = cur[b];
    cur[b]        = t;
    double cost   = 0;
    for (int n : nets_of[b]) {
      cost += net_cost(n, boxes.boxes[b].region);
    }
    for (int dr = -1; dr <= 1; ++dr) {
      for (int dc = -1; dc <= 1; ++dc) {
        const int c = g.col(t) + dc, r = g.row(t) + dr;
        if ((dc || dr) && c >= 0 && c < g.cols && r >= 0 && r < g.rows) {
          const auto it = at.find(g.tile(c, r));
          if (it != at.end() && it->second != b && std::ranges::none_of(nets_of[b], [&](int n) {
                return std::ranges::any_of(ends[n].per_region.at(boxes.boxes[b].region),
                                           [&](const Endpoint& e) { return e.box == it->second; });
              })) {
            cost += spread;
          }
        }
      }
    }
    cur[b] = was;
    return cost;
  };
  const auto candidates = [&](int b) {
    const auto&      box = boxes.boxes[b];
    std::vector<int> list;
    for (int w = int(o.window_tiles); list.empty() && w <= 4 * int(o.window_tiles) + 4; w += 2) {
      for (int r = std::max(0, g.row(home[b]) - w); r <= std::min(g.rows - 1, g.row(home[b]) + w); ++r) {
        for (int c = std::max(0, g.col(home[b]) - w); c <= std::min(g.cols - 1, g.col(home[b]) + w); ++c) {
          const int t = g.tile(c, r);
          if (g.region[t] == box.region && !g.blocked[t] && (!taken[t] || at.at(t) == b) && box_fits(d, g, box, t)) {
            list.push_back(t);
          }
        }
      }
    }
    return list;
  };
  const auto best_tile = [&](int b) {
    int    best = -1;
    double cost = INFINITY;
    for (int t : candidates(b)) {
      const double c = box_cost(b, t);
      if (c < cost - 1e-9 || (c < cost + 1e-9 && best >= 0 && t == cur[b])) {
        cost = c;
        best = t;
      }
    }
    return best;
  };
  const auto put = [&](int b, int t) {
    if (cur[b] >= 0) {
      taken[cur[b]] = 0;
      at.erase(cur[b]);
    }
    cur[b] = t;
    if (t >= 0) {
      taken[t] = 1;
      at[t]    = b;
    }
  };
  std::vector<int> placed;
  for (int b : order) {
    const int t = best_tile(b);
    if (t >= 0) {
      put(b, t);
      placed.push_back(b);
    }
  }
  for (int pass = 0; pass < 8; ++pass) {
    bool moved = false;
    for (int b : placed) {
      const int t = best_tile(b);
      if (t >= 0 && t != cur[b]) {
        put(b, t);
        moved = true;
      }
    }
    if (!moved) {
      break;
    }
  }
  // the other boxes of each region must still match its free tiles
  std::map<int, std::vector<int>> free_boxes, region_tiles;
  for (int b = 0; b < int(boxes.boxes.size()); ++b) {
    if (cur[b] < 0) {
      free_boxes[boxes.boxes[b].region].push_back(b);
    }
  }
  for (int t = 0; t < int(g.size()); ++t) {
    if (!g.blocked[t] && !taken[t]) {
      region_tiles[g.region[t]].push_back(t);
    }
  }
  std::set<int> lost;
  for (const auto& [r, list] : free_boxes) {
    std::vector<std::vector<int>> pref(list.size());
    for (size_t i = 0; i < list.size(); ++i) {
      for (int t : region_tiles[r]) {
        if (box_fits(d, g, boxes.boxes[list[i]], t)) {
          pref[i].push_back(t);
        }
      }
    }
    if (match_tiles(pref, g.size()).empty()) {
      lost.insert(r);
    }
  }
  rep.crit_unpinned_regions += lost.size();
  for (int b : placed) {
    if (lost.contains(boxes.boxes[b].region)) {
      put(b, -1);
    }
  }
  rep.crit_cost_after += total();
  for (int b : placed) {
    if (cur[b] >= 0) {
      boxes.boxes[b].fixed = true;
      boxes.boxes[b].tile  = cur[b];
      ++rep.crit_pinned;
    }
  }
}
// A piece of a fixed tree: a straight run between two tiles (a trunk or a rib), for the routing model.
struct Tree_piece {
  int net = -1, region = -1, a = -1, b = -1;
};
// A fixed tree: the boxes it fixed, the (tile, h) tracks it took (one each) and its pieces.
struct Fixed_tree {
  int                              net = -1, region = -1;
  std::vector<int>                 boxes, members;  // boxes it fixed; every box of the net in the region
  std::vector<std::pair<int, int>> uses;
  std::vector<Tree_piece>          pieces;
  bool                             dropped = false;
};

// Fixed 1:n trees (Legroute_options::tree_boxes): per region, the 1:n nets from the highest fanout down, whole fanout
// levels, while the region's fixed boxes (the pinned critical ones count first) stay within tree_boxes of its boxes.
// Most critical net first, its free boxes move at most tree_moves tiles from their placement tiles to shorten a
// single-trunk Steiner tree (an H trunk on one row with vertical ribs to its members, or a V trunk with horizontal
// ribs; lengths in um, so the tile's aspect counts), away from tiles other trees already load. The boxes are then
// fixed and the tree's tracks leave the tile capacities: the SAT sees the boxes as fixed endpoints and never the net.
// A region whose other boxes no longer match its free tiles keeps no tree. Marks treed (net, region).
std::vector<Fixed_tree> place_trees(const Design& d, const Legroute_options& o, const Grid& g, Boxes& boxes,
                                    const std::vector<Net_ends>& ends, const std::vector<int>& dem_h, const std::vector<int>& dem_v,
                                    std::vector<int>& cap_h, std::vector<int>& cap_v, std::set<std::pair<int, int>>& treed,
                                    Legroute_report& rep) {
  const int                     moves = int(o.tree_moves);
  const double                  tw = double(g.tile_w) / double(g.dbu), th = double(g.tile_h) / double(g.dbu);  // um
  constexpr double              overlap = 4, displace = .1;  // per unit of another tree's load share; per um moved
  std::vector<Fixed_tree>       trees;
  std::vector<std::vector<int>> region_boxes(g.regions);
  for (int b = 0; b < int(boxes.boxes.size()); ++b) {
    region_boxes[boxes.boxes[b].region].push_back(b);
  }
  std::vector<int>  load_h(g.size(), 0), load_v(g.size(), 0);
  std::vector<char> taken(g.size(), 0);
  for (const auto& box : boxes.boxes) {
    if (box.fixed) {
      taken[box.tile] = 1;
    }
  }
  const auto                    home    = [&](int b) { return g.tile_of(boxes.boxes[b].cx, boxes.boxes[b].cy); };
  // tree_load selection: the fixed demand per tile and direction besides the tree tracks (fixed boxes' in-box demand,
  // straight connections between fixed endpoints), and each box's nets
  const bool                    by_load = o.tree_load > 0;
  std::vector<int>              fixed_h(g.size(), 0), fixed_v(g.size(), 0);
  std::vector<std::vector<int>> box_nets(boxes.boxes.size());
  if (by_load) {
    for (size_t n = 0; n < d.nets.size(); ++n) {
      for (const auto& [r, list] : ends[n].per_region) {
        for (const auto& e : list) {
          if (e.box >= 0 && (box_nets[e.box].empty() || box_nets[e.box].back() != int(n))) {
            box_nets[e.box].push_back(int(n));
          }
        }
      }
    }
  }
  // the straight connections (the SAT's star from a net's first endpoint) between two placed endpoints with one of
  // the changed boxes at an end, nets other than skip and not treed, onto add as (tile, h) -> tracks
  const auto straight = [&](int                                 r,
                            const std::vector<int>&             changed,
                            const std::function<int(int)>&      tile_of_box,
                            int                                 skip,
                            std::map<std::pair<int, int>, int>& add) {
    std::set<std::pair<int, int>> done;  // (net, endpoint)
    const std::set<int>           changed_set(changed.begin(), changed.end());
    for (int b : changed) {
      for (int m : box_nets[b]) {
        const auto it = ends[m].per_region.find(r);
        if (m == skip || it == ends[m].per_region.end() || treed.contains({m, r})) {
          continue;
        }
        const auto& list = it->second;
        const auto  at   = [&](const Endpoint& e) { return e.box < 0 ? e.tile : tile_of_box(e.box); };
        const int   ta   = at(list[0]);
        for (size_t k = 1; ta >= 0 && k < list.size(); ++k) {
          const bool mine
              = (list[0].box >= 0 && changed_set.contains(list[0].box)) || (list[k].box >= 0 && changed_set.contains(list[k].box));
          const int tb = at(list[k]);
          if (!mine || tb < 0 || tb == ta || (g.row(ta) != g.row(tb) && g.col(ta) != g.col(tb))
              || !done.emplace(m, int(k)).second) {
            continue;
          }
          if (list[0].box >= 0 && list[k].box >= 0 && d.nets[m].pins.size() == 2
              && std::abs(g.col(ta) - g.col(tb)) + std::abs(g.row(ta) - g.row(tb)) == 1) {
            continue;  // neighbors: in-box metal
          }
          const int h = g.row(ta) == g.row(tb);
          for (int i = std::min(g.col(ta), g.col(tb)); i <= std::max(g.col(ta), g.col(tb)); ++i) {
            for (int j = std::min(g.row(ta), g.row(tb)); j <= std::max(g.row(ta), g.row(tb)); ++j) {
              if (g.region[g.tile(i, j)] == r) {
                ++add[{g.tile(i, j), h}];
              }
            }
          }
        }
      }
    }
  };
  const auto       fixed_tile_of = [&](int b) { return boxes.boxes[b].fixed ? boxes.boxes[b].tile : -1; };
  // the capacity clauses of the region's SAT model (its totalizers, ~97% of the model): per tile and direction, the
  // nets whose legs may cover it (windows of window_tiles around the placement, fixed boxes on their tiles) and the
  // occupant's in-box demand, over the tracks the trees leave
  std::vector<int> stamp(2 * g.size(), -1), inputs(2 * g.size(), 0);
  const auto       estimate = [&](int r, const std::vector<int>& B) {
    int rc0 = g.cols, rc1 = -1, rr0 = g.rows, rr1 = -1;
    for (int t : g.region_tiles[r]) {
      rc0 = std::min(rc0, g.col(t));
      rc1 = std::max(rc1, g.col(t));
      rr0 = std::min(rr0, g.row(t));
      rr1 = std::max(rr1, g.row(t));
    }
    const int  w   = int(o.window_tiles);
    const auto win = [&](const Endpoint& e) -> std::array<int, 4> {
      const int t = e.box < 0 ? e.tile : boxes.boxes[e.box].fixed ? boxes.boxes[e.box].tile : -1;
      if (t >= 0) {
        return {g.col(t), g.col(t), g.row(t), g.row(t)};
      }
      const int h0 = home(e.box);
      return {std::max(rc0, g.col(h0) - w),
              std::min(rc1, g.col(h0) + w),
              std::max(rr0, g.row(h0) - w),
              std::min(rr1, g.row(h0) + w)};
    };
    for (int t : g.region_tiles[r]) {
      stamp[2 * t] = stamp[2 * t + 1] = -1;
      inputs[2 * t] = inputs[2 * t + 1] = 0;
    }
    const auto mark = [&](int n, int t, int h) {
      if (g.region[t] == r && stamp[2 * t + h] != n) {
        stamp[2 * t + h] = n;
        ++inputs[2 * t + h];
      }
    };
    for (size_t n = 0; n < d.nets.size(); ++n) {
      const auto it = ends[n].per_region.find(r);
      if (it == ends[n].per_region.end() || it->second.size() < 2 || treed.contains({int(n), r})) {
        continue;
      }
      const auto& list = it->second;
      const auto  wa   = win(list[0]);
      for (size_t k = 1; k < list.size(); ++k) {
        const auto wb = win(list[k]);
        const int  x0 = std::min(wa[0], wb[0]), x1 = std::max(wa[1], wb[1]), y0 = std::min(wa[2], wb[2]),
                   y1 = std::max(wa[3], wb[3]);
        for (const auto& wv : {wa, wb}) {
          for (int j = wv[2]; j <= wv[3]; ++j) {
            for (int i = x0; i <= x1; ++i) {
              mark(int(n), g.tile(i, j), 1);
            }
          }
          for (int i = wv[0]; i <= wv[1]; ++i) {
            for (int j = y0; j <= y1; ++j) {
              mark(int(n), g.tile(i, j), 0);
            }
          }
        }
      }
    }
    std::vector<std::array<int, 2>> top(g.size(), {0, 0});
    for (int b : B) {
      const auto wv = win(Endpoint{b, -1});
      for (int i = wv[0]; i <= wv[1]; ++i) {
        for (int j = wv[2]; j <= wv[3]; ++j) {
          auto& tp = top[g.tile(i, j)];
          tp       = {std::max(tp[0], dem_v[b]), std::max(tp[1], dem_h[b])};
        }
      }
    }
    size_t total = 0;
    for (int t : g.region_tiles[r]) {
      for (int h : {1, 0}) {
        const int k  = std::max(0, int(std::floor(((h ? cap_h : cap_v)[t] - (h ? load_h : load_v)[t]) * o.cap_scale)));
        total       += at_most_clauses(inputs[2 * t + h] + top[t][h], k);
      }
    }
    return total;
  };
  // tree_congestion (DCGP-style congestion potential): per tile and direction, the routing demand of every region net's
  // star connections at their placement tiles (each spreads its length over its bounding box, RUDY) plus the boxes'
  // in-box demand, over the tracks; a Poisson potential of it (Jacobi, zero-flux edges, scaled to [0, 1]) lets a trunk
  // or rib feel congestion nearby, so the trees route around it
  std::vector<double> psi_h, psi_v;
  const double        cong = 4 * o.tree_congestion;  // per tile at full potential, in tile lengths
  if (cong > 0) {
    std::vector<double> rho_h(g.size(), 0), rho_v(g.size(), 0);
    const auto          at = [&](const Endpoint& e) {
      return e.box < 0 ? e.tile : boxes.boxes[e.box].fixed ? boxes.boxes[e.box].tile : home(e.box);
    };
    for (size_t b = 0; b < boxes.boxes.size(); ++b) {
      rho_h[size_t(at({int(b), -1}))] += dem_h[b];
      rho_v[size_t(at({int(b), -1}))] += dem_v[b];
    }
    for (const auto& E : ends) {
      for (const auto& [r, list] : E.per_region) {
        for (size_t k = 1; k < list.size(); ++k) {
          const int    a = at(list[0]), b = at(list[k]);
          const int    c0 = std::min(g.col(a), g.col(b)), c1 = std::max(g.col(a), g.col(b));
          const int    r0 = std::min(g.row(a), g.row(b)), r1 = std::max(g.row(a), g.row(b));
          const double area = double(c1 - c0 + 1) * double(r1 - r0 + 1);
          for (int j = r0; j <= r1; ++j) {
            for (int i = c0; i <= c1; ++i) {
              rho_h[size_t(g.tile(i, j))] += double(c1 - c0) / area;
              rho_v[size_t(g.tile(i, j))] += double(r1 - r0) / area;
            }
          }
        }
      }
    }
    const auto potential = [&](std::vector<double>& rho, const std::vector<int>& cap) {
      double mean = 0;
      for (size_t t = 0; t < g.size(); ++t) {
        rho[t] /= double(std::max(1, cap[t]));
        mean   += rho[t];
      }
      mean /= double(std::max<size_t>(1, g.size()));
      std::vector<double> psi(g.size(), 0), next(g.size(), 0);
      for (int iter = 0; iter < 200; ++iter) {
        for (int t = 0; t < int(g.size()); ++t) {
          double sum = rho[size_t(t)] - mean;
          int    deg = 0;
          for (int q : {g.col(t) > 0 ? t - 1 : -1, g.col(t) + 1 < g.cols ? t + 1 : -1, t - g.cols, t + g.cols}) {
            if (q >= 0 && q < int(g.size())) {
              sum += psi[size_t(q)];
              ++deg;
            }
          }
          next[size_t(t)] = deg ? sum / deg : 0;
        }
        psi.swap(next);
      }
      const auto [lo, hi] = std::ranges::minmax_element(psi);
      for (auto& v : psi) {
        v = *hi > *lo ? (v - *lo) / (*hi - *lo) : 0;
      }
      return psi;
    };
    psi_h = potential(rho_h, cap_h);
    psi_v = potential(rho_v, cap_v);
  }
  // the tree of members on tiles with its trunk on axis (a row for H, a column for V): length (um) and, with the
  // penalties, the cost of the tracks it shares with other trees (and with tree_congestion, the potential it crosses)
  const auto tree = [&](const std::vector<int>& tiles, bool h, int axis, bool penalized) {
    int lo = INT32_MAX, hi = INT32_MIN;
    for (int t : tiles) {
      lo = std::min(lo, h ? g.col(t) : g.row(t));
      hi = std::max(hi, h ? g.col(t) : g.row(t));
    }
    double cost = (hi - lo) * (h ? tw : th);
    for (int t : tiles) {
      cost += std::abs((h ? g.row(t) : g.col(t)) - axis) * (h ? th : tw);
    }
    if (penalized) {
      for (int k = lo; k <= hi; ++k) {
        const int t  = h ? g.tile(k, axis) : g.tile(axis, k);
        cost        += overlap * (h ? tw : th) * (h ? load_h : load_v)[t] / std::max(1, (h ? cap_h : cap_v)[t]);
        if (cong > 0) {
          cost += cong * (h ? tw : th) * (h ? psi_h : psi_v)[size_t(t)];
        }
      }
      for (int t : tiles) {  // the ribs
        const int along = h ? g.col(t) : g.row(t), across = h ? g.row(t) : g.col(t);
        for (int k = std::min(across, axis); cong > 0 && across != axis && k <= std::max(across, axis); ++k) {
          cost += cong * (h ? th : tw) * (h ? psi_v : psi_h)[size_t(h ? g.tile(along, k) : g.tile(k, along))];
        }
      }
    }
    return cost;
  };
  const auto median = [&](const std::vector<int>& tiles, bool h) {
    std::vector<int> v;
    for (int t : tiles) {
      v.push_back(h ? g.row(t) : g.col(t));
    }
    std::ranges::nth_element(v, v.begin() + v.size() / 2);
    return v[v.size() / 2];
  };
  for (int r = 0; r < g.regions; ++r) {
    const auto& B = region_boxes[r];
    if (B.empty()) {
      continue;
    }
    const size_t                     pinned = size_t(std::ranges::count_if(B, [&](int b) { return boxes.boxes[b].fixed; }));
    std::vector<std::pair<int, int>> by_fan;  // (fanout, net): endpoints in the region beyond the first
    for (size_t n = 0; n < d.nets.size(); ++n) {
      const auto it = ends[n].per_region.find(r);
      if (it != ends[n].per_region.end() && it->second.size() >= 3) {
        by_fan.emplace_back(int(it->second.size()) - 1, int(n));
      }
    }
    std::ranges::sort(by_fan, std::greater<>{});
    std::set<int>    touched;
    std::vector<int> chosen;
    if (by_load) {  // every 1:n net in order; the load limit and the clause target decide
      for (const auto& [_, n] : by_fan) {
        chosen.push_back(n);
      }
      if (o.tree_by_crit) {
        std::ranges::stable_sort(chosen, [&](int a, int b) { return d.nets[a].crit > d.nets[b].crit; });
      }
      std::ranges::fill(fixed_h, 0);  // the region's fixed demand so far (its pinned critical boxes)
      std::ranges::fill(fixed_v, 0);
      std::vector<int> pinned_here;
      for (int b : B) {
        if (boxes.boxes[b].fixed) {
          pinned_here.push_back(b);
          fixed_h[boxes.boxes[b].tile] += dem_h[b];
          fixed_v[boxes.boxes[b].tile] += dem_v[b];
        }
      }
      std::map<std::pair<int, int>, int> add;
      straight(r, pinned_here, fixed_tile_of, -1, add);
      for (const auto& [key, v] : add) {
        (key.second ? fixed_h : fixed_v)[key.first] += v;
      }
    }
    for (size_t i = 0; !by_load && i < by_fan.size();) {
      size_t        j    = i;
      std::set<int> more = touched;
      for (; j < by_fan.size() && by_fan[j].first == by_fan[i].first; ++j) {
        for (const auto& e : ends[by_fan[j].second].per_region.at(r)) {
          if (e.box >= 0 && !boxes.boxes[e.box].fixed) {
            more.insert(e.box);
          }
        }
      }
      if (double(more.size() + pinned) > o.tree_boxes * double(B.size())) {
        break;
      }
      touched = std::move(more);
      for (size_t k = i; k < j; ++k) {
        chosen.push_back(by_fan[k].second);
      }
      i = j;
    }
    if (!by_load) {
      std::ranges::stable_sort(chosen, [&](int a, int b) { return d.nets[a].crit > d.nets[b].crit; });
    }
    size_t est = by_load ? estimate(r, B) : 0;
    if (by_load) {
      rep.tree_clauses_before += est;
    }
    std::vector<int>                       fixed_here;
    std::vector<std::tuple<int, int, int>> loaded;  // (tile, h, tracks)
    const size_t                           trees_mark = trees.size();
    size_t                                 nets_here  = 0;
    double                                 before = 0, after = 0;
    for (int n : chosen) {
      if (by_load && double(est) < o.tree_clauses) {
        break;
      }
      if (by_load && o.tree_boxes > 0
          && double(std::ranges::count_if(B, [&](int b) { return boxes.boxes[b].fixed; })) > o.tree_boxes * double(B.size())) {
        break;
      }
      const auto&                   list = ends[n].per_region.at(r);
      std::vector<int>              fixed(list.size(), -1);  // tile of a fixed member
      std::vector<std::vector<int>> cand(list.size());
      std::vector<int>              start(list.size());
      bool                          stuck = false;
      for (size_t i = 0; i < list.size(); ++i) {
        const auto& e = list[i];
        if (e.box < 0 || boxes.boxes[e.box].fixed) {
          fixed[i] = start[i] = e.box < 0 ? e.tile : boxes.boxes[e.box].tile;
          continue;
        }
        const int h0 = start[i] = home(e.box);
        for (int dr = -moves; dr <= moves; ++dr) {
          for (int dc = -moves; dc <= moves; ++dc) {
            const int c = g.col(h0) + dc, rr = g.row(h0) + dr;
            if (c < 0 || c >= g.cols || rr < 0 || rr >= g.rows) {
              continue;
            }
            const int t = g.tile(c, rr);
            if (g.region[t] == r && !g.blocked[t] && !taken[t] && box_fits(d, g, boxes.boxes[e.box], t)) {
              cand[i].push_back(t);
            }
          }
        }
        stuck = stuck || cand[i].empty();
      }
      if (stuck) {
        ++rep.tree_skipped_nets;
        continue;
      }
      double before_net = INFINITY;
      for (bool h : {true, false}) {
        before_net = std::min(before_net, tree(start, h, median(start, h), false));
      }
      double           best      = INFINITY;
      bool             best_h    = true;
      int              best_axis = 0;
      std::vector<int> best_tiles;
      for (bool h : {true, false}) {
        std::vector<int> tiles = start;
        int              axis  = median(tiles, h);
        for (int iter = 0; iter < 3; ++iter) {
          std::set<int> used;
          for (size_t i = 0; i < list.size(); ++i) {
            if (fixed[i] >= 0) {
              used.insert(fixed[i]);
            }
          }
          for (size_t i = 0; i < list.size(); ++i) {
            if (fixed[i] >= 0) {
              continue;
            }
            int lo = INT32_MAX, hi = INT32_MIN;  // the others along the trunk
            for (size_t j = 0; j < list.size(); ++j) {
              if (j != i) {
                lo = std::min(lo, h ? g.col(tiles[j]) : g.row(tiles[j]));
                hi = std::max(hi, h ? g.col(tiles[j]) : g.row(tiles[j]));
              }
            }
            double cost = INFINITY;
            int    pick = -1;
            for (int t : cand[i]) {
              if (used.contains(t)) {
                continue;
              }
              const int along = h ? g.col(t) : g.row(t), across = h ? g.row(t) : g.col(t);
              double    c = std::abs(across - axis) * (h ? th : tw)
                            + (std::max(0, lo - along) + std::max(0, along - hi)) * (h ? tw : th)
                            + displace * (std::abs(g.col(t) - g.col(start[i])) * tw + std::abs(g.row(t) - g.row(start[i])) * th);
              for (int k = std::min(across, axis); k <= std::max(across, axis); ++k) {  // the rib's tiles
                const int q  = h ? g.tile(g.col(t), k) : g.tile(k, g.row(t));
                c           += overlap * (h ? th : tw) * (h ? load_v : load_h)[q] / std::max(1, (h ? cap_v : cap_h)[q]);
                if (cong > 0 && across != axis) {
                  c += cong * (h ? th : tw) * (h ? psi_v : psi_h)[size_t(q)];
                }
              }
              if (c < cost) {
                cost = c;
                pick = t;
              }
            }
            if (pick < 0) {
              stuck = true;
              break;
            }
            tiles[i] = pick;
            used.insert(pick);
          }
          if (stuck) {
            break;
          }
          axis = median(tiles, h);
        }
        if (stuck) {
          break;
        }
        if (cong > 0) {  // the trunk's Steiner axis: the cheapest near the median within the members' span
          int lo = INT32_MAX, hi = INT32_MIN;
          for (int t : tiles) {
            lo = std::min(lo, h ? g.row(t) : g.col(t));
            hi = std::max(hi, h ? g.row(t) : g.col(t));
          }
          int    pick = axis;
          double low  = tree(tiles, h, axis, true);
          for (int a = std::max(lo, axis - moves); a <= std::min(hi, axis + moves); ++a) {
            const double c = tree(tiles, h, a, true);
            if (c < low) {
              low  = c;
              pick = a;
            }
          }
          axis = pick;
        }
        double cost = tree(tiles, h, axis, true);
        if (cost < best) {
          best       = cost;
          best_h     = h;
          best_axis  = axis;
          best_tiles = tiles;
        }
      }
      if (stuck || best_tiles.empty()) {
        ++rep.tree_skipped_nets;
        continue;
      }
      if (by_load) {  // the tree tracks, the new boxes' in-box demand and the straight connections they make stay
                      // within tree_load of every tile's tracks
        std::map<std::pair<int, int>, int> add;
        std::vector<int>                   moved;
        std::unordered_map<int, int>       at;
        const bool                         th_ = best_h;
        int                                lo_ = INT32_MAX, hi_ = INT32_MIN;
        for (size_t i = 0; i < list.size(); ++i) {
          lo_ = std::min(lo_, th_ ? g.col(best_tiles[i]) : g.row(best_tiles[i]));
          hi_ = std::max(hi_, th_ ? g.col(best_tiles[i]) : g.row(best_tiles[i]));
          if (fixed[i] < 0) {
            moved.push_back(list[i].box);
            at[list[i].box]          = best_tiles[i];
            add[{best_tiles[i], 1}] += dem_h[list[i].box];
            add[{best_tiles[i], 0}] += dem_v[list[i].box];
          }
        }
        std::set<std::pair<int, int>> tracks;
        for (int k = lo_; k <= hi_; ++k) {
          tracks.emplace(th_ ? g.tile(k, best_axis) : g.tile(best_axis, k), th_ ? 1 : 0);
        }
        for (int t : best_tiles) {
          const int along = th_ ? g.col(t) : g.row(t), across = th_ ? g.row(t) : g.col(t);
          for (int k = std::min(across, best_axis); across != best_axis && k <= std::max(across, best_axis); ++k) {
            tracks.emplace(th_ ? g.tile(along, k) : g.tile(k, along), th_ ? 0 : 1);
          }
        }
        for (const auto& key : tracks) {
          ++add[key];
        }
        straight(
            r,
            moved,
            [&](int b) {
              const auto it = at.find(b);
              return it != at.end() ? it->second : fixed_tile_of(b);
            },
            n,
            add);
        bool over = false;
        for (const auto& [key, v] : add) {
          const auto [t, hh] = key;
          over
              = over || double((hh ? load_h : load_v)[t] + (hh ? fixed_h : fixed_v)[t] + v) > o.tree_load * (hh ? cap_h : cap_v)[t];
        }
        if (over) {
          ++rep.tree_load_skipped;
          continue;
        }
        for (const auto& [key, v] : add) {
          if (!tracks.contains(key)) {  // the tree tracks go to load below
            (key.second ? fixed_h : fixed_v)[key.first] += v;
          } else {
            (key.second ? fixed_h : fixed_v)[key.first] += v - 1;
          }
        }
      }
      // commit: fix the boxes, reserve the tracks (a net counts once per tile and direction), keep the pieces
      const bool h  = best_h;
      int        lo = INT32_MAX, hi = INT32_MIN;
      Fixed_tree ft;
      ft.net    = n;
      ft.region = r;
      for (size_t i = 0; i < list.size(); ++i) {
        const int t = best_tiles[i];
        lo          = std::min(lo, h ? g.col(t) : g.row(t));
        hi          = std::max(hi, h ? g.col(t) : g.row(t));
        if (fixed[i] < 0) {
          auto& box = boxes.boxes[list[i].box];
          box.fixed = true;
          box.tile  = t;
          taken[t]  = 1;
          fixed_here.push_back(list[i].box);
          ft.boxes.push_back(list[i].box);
        }
        if (list[i].box >= 0) {
          ft.members.push_back(list[i].box);
        }
      }
      std::set<std::pair<int, int>> uses;  // (tile, h)
      for (int k = lo; k <= hi; ++k) {
        uses.emplace(h ? g.tile(k, best_axis) : g.tile(best_axis, k), h ? 1 : 0);
      }
      if (lo < hi) {
        ft.pieces.push_back(
            {n, r, h ? g.tile(lo, best_axis) : g.tile(best_axis, lo), h ? g.tile(hi, best_axis) : g.tile(best_axis, hi)});
      }
      for (int t : best_tiles) {
        const int along = h ? g.col(t) : g.row(t), across = h ? g.row(t) : g.col(t);
        for (int k = std::min(across, best_axis); across != best_axis && k <= std::max(across, best_axis); ++k) {
          uses.emplace(h ? g.tile(along, k) : g.tile(k, along), h ? 0 : 1);  // a rib (none on the trunk)
        }
        if (across != best_axis) {
          ft.pieces.push_back({n, r, t, h ? g.tile(along, best_axis) : g.tile(best_axis, along)});
        }
      }
      for (const auto& [t, dir] : uses) {
        ++(dir ? load_h : load_v)[t];
        loaded.emplace_back(t, dir, 1);
        ft.uses.emplace_back(t, dir);
      }
      trees.push_back(std::move(ft));
      treed.emplace(n, r);
      ++nets_here;
      before += before_net;
      after  += tree(best_tiles, h, best_axis, false);
      if (by_load) {
        est = estimate(r, B);
      }
    }
    if (by_load) {
      rep.tree_clauses_after += est;
    }
    // the other boxes of the region must still match its free tiles
    std::vector<std::vector<int>> pref;
    for (int b : B) {
      if (boxes.boxes[b].fixed) {
        continue;
      }
      pref.emplace_back();
      for (int t = 0; t < int(g.size()); ++t) {
        if (g.region[t] == r && !g.blocked[t] && !taken[t] && box_fits(d, g, boxes.boxes[b], t)) {
          pref.back().push_back(t);
        }
      }
    }
    if (!pref.empty() && match_tiles(pref, g.size()).empty()) {
      for (int b : fixed_here) {
        taken[boxes.boxes[b].tile] = 0;
        boxes.boxes[b].fixed       = false;
        boxes.boxes[b].tile        = -1;
      }
      for (const auto& [t, dir, k] : loaded) {
        (dir ? load_h : load_v)[t] -= k;
      }
      for (size_t k = trees_mark; k < trees.size(); ++k) {  // only this pass's entries: the region may hold global pieces
        treed.erase({trees[k].net, trees[k].region});
      }
      trees.resize(trees_mark);
      ++rep.tree_dropped_regions;
      continue;
    }
    rep.tree_nets      += nets_here;
    rep.tree_boxes     += fixed_here.size();
    rep.tree_um_before += before;
    rep.tree_um_after  += after;
  }
  for (size_t t = 0; t < g.size(); ++t) {
    for (int h : {1, 0}) {
      const int load            = (h ? load_h : load_v)[t];
      auto&     cap             = (h ? cap_h : cap_v)[t];
      rep.tree_max_load         = std::max(rep.tree_max_load, double(load) / std::max(1, cap));
      rep.tree_tiles_over_half += 2 * load > cap;
      cap                      -= load;
    }
  }
  return trees;
}

// Demand no SAT placement can avoid: a fixed box's in-box demand on its tile plus each straight connection (the SAT's
// star from a net's first endpoint) between two fixed endpoints, on the tiles it crosses. While a tile's fixed demand
// is over its tracks, drop the least critical tree with a box among the tile's contributors (its boxes come free and
// its tracks return), else unpin the least critical contributing box.
void repair_forced(const Design& d, const Legroute_options& o, const Grid& g, Boxes& boxes, const std::vector<Net_ends>& ends,
                   const std::vector<int>& dem_h, const std::vector<int>& dem_v, std::vector<int>& cap_h, std::vector<int>& cap_v,
                   std::vector<Fixed_tree>& trees, std::set<std::pair<int, int>>& treed, Legroute_report& rep) {
  std::unordered_map<int, int> tree_of;  // box -> tree that fixed it
  for (int i = 0; i < int(trees.size()); ++i) {
    for (int b : trees[i].boxes) {
      tree_of[b] = i;
    }
  }
  const auto fixed_tile = [&](const Endpoint& e) {
    return e.box < 0 ? e.tile : boxes.boxes[e.box].fixed ? boxes.boxes[e.box].tile : -1;
  };
  for (;;) {
    std::map<std::pair<int, int>, int>              forced;  // (tile, h) -> tracks
    std::map<std::pair<int, int>, std::vector<int>> who;     // (tile, h) -> contributing boxes
    for (int b = 0; b < int(boxes.boxes.size()); ++b) {
      if (boxes.boxes[b].fixed) {
        for (int h : {1, 0}) {
          forced[{boxes.boxes[b].tile, h}] += (h ? dem_h : dem_v)[b];
          who[{boxes.boxes[b].tile, h}].push_back(b);
        }
      }
    }
    for (size_t n = 0; n < d.nets.size(); ++n) {
      for (const auto& [r, list] : ends[n].per_region) {
        if (list.size() < 2 || treed.contains({int(n), r})) {
          continue;
        }
        const int ta = fixed_tile(list[0]);
        for (size_t k = 1; ta >= 0 && k < list.size(); ++k) {
          const int tb = fixed_tile(list[k]);
          if (tb < 0 || tb == ta || (g.row(ta) != g.row(tb) && g.col(ta) != g.col(tb))) {
            continue;
          }
          if (list[0].box >= 0 && list[k].box >= 0 && d.nets[n].pins.size() == 2
              && std::abs(g.col(ta) - g.col(tb)) + std::abs(g.row(ta) - g.row(tb)) == 1) {
            continue;  // neighbors: in-box metal
          }
          const int h = g.row(ta) == g.row(tb);
          for (int i = std::min(g.col(ta), g.col(tb)); i <= std::max(g.col(ta), g.col(tb)); ++i) {
            for (int j = std::min(g.row(ta), g.row(tb)); j <= std::max(g.row(ta), g.row(tb)); ++j) {
              const int t = g.tile(i, j);
              if (g.region[t] != r) {
                continue;
              }
              ++forced[{t, h}];
              for (const auto* e : {&list[0], &list[k]}) {
                if (e->box >= 0) {
                  who[{t, h}].push_back(e->box);
                }
              }
            }
          }
        }
      }
    }
    std::set<int> drop_trees, unpin;
    for (const auto& [key, v] : forced) {
      const int k = std::max(0, int(std::floor((key.second ? cap_h : cap_v)[key.first] * o.cap_scale)));
      if (v <= k) {
        continue;
      }
      int    tree = -1, box = -1;
      double tree_crit = INFINITY, box_crit = INFINITY;
      for (int b : who[key]) {
        if (const auto it = tree_of.find(b); it != tree_of.end() && !trees[it->second].dropped) {
          const double c = d.nets[trees[it->second].net].crit;
          if (c < tree_crit) {
            tree_crit = c;
            tree      = it->second;
          }
        } else if (boxes.boxes[b].fixed && boxes.boxes[b].crit < box_crit) {
          box_crit = boxes.boxes[b].crit;
          box      = b;
        }
      }
      if (tree >= 0) {
        drop_trees.insert(tree);
      } else if (box >= 0) {
        unpin.insert(box);
      }
    }
    if (drop_trees.empty() && unpin.empty()) {
      return;
    }
    for (int b : unpin) {
      boxes.boxes[b].fixed = false;
      boxes.boxes[b].tile  = -1;
      ++rep.forced_crit_unpinned;
    }
    // a dropped tree frees its boxes; a tree whose member came free lost its shape and goes too
    for (bool more = true; more;) {
      more = false;
      for (int i = 0; i < int(trees.size()); ++i) {
        auto& ft = trees[i];
        if (ft.dropped
            || (!drop_trees.contains(i) && std::ranges::all_of(ft.members, [&](int b) { return boxes.boxes[b].fixed; }))) {
          continue;
        }
        ft.dropped = true;
        more       = true;
        for (int b : ft.boxes) {
          boxes.boxes[b].fixed = false;
          boxes.boxes[b].tile  = -1;
        }
        for (const auto& [t, h] : ft.uses) {
          ++(h ? cap_h : cap_v)[t];
        }
        treed.erase({ft.net, ft.region});
        ++rep.forced_trees_dropped;
      }
    }
  }
}

// An IO pre-route: one track between two fixed pin tiles of a region (H leg on ta's row then V on tb's column when
// h_first). A feedthrough net (net_a, no box) runs on it alone; a pin -> box -> pin pair (net_a at ta, net_b at tb)
// shares it end to end and the box sits on one of its tiles.
struct Io_route {
  int  region = -1, ta = -1, tb = -1, box = -1, net_a = -1, net_b = -1;
  bool h_first = true;
};
std::vector<std::pair<int, int>> io_path(const Grid& g, int ta, int tb, bool h_first) {  // (tile, h)
  std::vector<std::pair<int, int>> path;
  const int                        row = h_first ? g.row(ta) : g.row(tb), col = h_first ? g.col(tb) : g.col(ta);
  for (int i = std::min(g.col(ta), g.col(tb)); g.col(ta) != g.col(tb) && i <= std::max(g.col(ta), g.col(tb)); ++i) {
    path.emplace_back(g.tile(i, row), 1);
  }
  for (int j = std::min(g.row(ta), g.row(tb)); g.row(ta) != g.row(tb) && j <= std::max(g.row(ta), g.row(tb)); ++j) {
    path.emplace_back(g.tile(col, j), 0);
  }
  return path;
}

// Legroute_options::io_preroute: per region, the 2-endpoint nets between fixed pins (feedthroughs) and the free boxes
// whose only pin-to-box nets are one pair of 2-endpoint nets get one track each before the SAT: the track leaves the
// tile capacities, the nets leave the model (marked in routed_ahead) and such a box may only take the track's tiles
// near its placement (allowed). The orientation with more room (and, with a box, closer to the box) wins.
std::vector<Io_route> io_preroute(const Design& d, const Legroute_options& o, const Grid& g, const Boxes& boxes,
                                  const std::vector<Net_ends>& ends, std::vector<int>& cap_h, std::vector<int>& cap_v,
                                  const std::vector<int>& cap_h0, const std::vector<int>& cap_v0,
                                  std::set<std::pair<int, int>>& routed_ahead, std::unordered_map<int, std::vector<int>>& allowed,
                                  Legroute_report& rep) {
  std::vector<Io_route> routes;
  std::vector<char>     taken(g.size(), 0);
  for (const auto& box : boxes.boxes) {
    if (box.fixed) {
      taken[box.tile] = 1;
    }
  }
  // per (box, region): its 2-endpoint nets whose other endpoint is a fixed pin, (net, pin tile)
  std::map<std::pair<int, int>, std::vector<std::pair<int, int>>> io_nets;
  std::vector<std::tuple<int, int, int, int>>                     feed;  // (net, region, ta, tb)
  for (size_t n = 0; n < d.nets.size(); ++n) {
    for (const auto& [r, list] : ends[n].per_region) {
      if (list.size() != 2 || routed_ahead.contains({int(n), r})) {
        continue;
      }
      if (list[0].box < 0 && list[1].box < 0) {
        feed.emplace_back(int(n), r, list[0].tile, list[1].tile);
      } else if ((list[0].box < 0) != (list[1].box < 0)) {
        const auto& b = list[0].box >= 0 ? list[0] : list[1];
        const auto& p = list[0].box >= 0 ? list[1] : list[0];
        io_nets[{b.box, r}].emplace_back(int(n), p.tile);
      }
    }
  }
  const int  w    = int(o.window_tiles);
  // load share, or INFINITY: a tile whose routes fixed ahead (with this one) would pass reserve_share of its tracks
  // has no room
  const auto room = [&](const std::vector<std::pair<int, int>>& path, int r) {
    double cost = 0;
    for (const auto& [t, h] : path) {
      const int c = (h ? cap_h : cap_v)[t], c0 = (h ? cap_h0 : cap_v0)[t];
      if (g.region[t] != r || c < 1 || double(c0 - c + 1) > o.reserve_share * double(c0)) {
        return double(INFINITY);
      }
      cost += 1.0 / c;
    }
    return cost;
  };
  const auto reserve = [&](const std::vector<std::pair<int, int>>& path) {
    for (const auto& [t, h] : path) {
      --(h ? cap_h : cap_v)[t];
    }
  };
  for (const auto& [n, r, ta, tb] : feed) {
    double best = INFINITY;
    bool   hf   = true;
    for (bool h : {true, false}) {
      if (const double c = room(io_path(g, ta, tb, h), r); c < best) {
        best = c;
        hf   = h;
      }
    }
    if (best == INFINITY) {
      ++rep.io_skipped;
      continue;
    }
    reserve(io_path(g, ta, tb, hf));
    routed_ahead.emplace(n, r);
    routes.push_back({r, ta, tb, -1, n, -1, hf});
    ++rep.io_feedthroughs;
  }
  for (const auto& [key, list] : io_nets) {
    const auto [b, r] = key;
    const auto& box   = boxes.boxes[b];
    if (list.size() != 2 || box.fixed) {
      continue;
    }
    const int        h0 = g.tile_of(box.cx, box.cy);
    const int        ta = list[0].second, tb = list[1].second;
    double           best = INFINITY;
    bool             hf   = true;
    std::vector<int> best_tiles;
    for (bool h : {true, false}) {
      const auto path = io_path(g, ta, tb, h);
      double     cost = room(path, r);
      if (cost == INFINITY) {
        continue;
      }
      std::vector<int> tiles;  // the track's free fitting tiles near the box's placement
      std::set<int>    seen;
      for (int t : {ta, tb}) {
        seen.insert(t);
      }
      std::vector<int> cand(seen.begin(), seen.end());
      for (const auto& [t, _] : path) {
        if (seen.insert(t).second) {
          cand.push_back(t);
        }
      }
      int near = INT32_MAX;
      for (int t : cand) {
        const int dist = std::max(std::abs(g.col(t) - g.col(h0)), std::abs(g.row(t) - g.row(h0)));
        if (dist <= 2 * w + 1 && !g.blocked[t] && !taken[t] && box_fits(d, g, box, t)) {
          tiles.push_back(t);
          near = std::min(near, dist);
        }
      }
      if (tiles.empty()) {
        continue;
      }
      cost += near;
      if (cost < best) {
        best       = cost;
        hf         = h;
        best_tiles = std::move(tiles);
      }
    }
    if (best_tiles.empty()) {
      ++rep.io_skipped;
      continue;
    }
    reserve(io_path(g, ta, tb, hf));
    routed_ahead.emplace(list[0].first, r);
    routed_ahead.emplace(list[1].first, r);
    allowed[b] = std::move(best_tiles);
    routes.push_back({r, ta, tb, b, list[0].first, list[1].first, hf});
    ++rep.io_boxes;
  }
  return routes;
}
// legroute.global: the run of a critical net through a region holding none of its pins, fixed ahead (an L from a to
// b, H first or V first) and reserved on the edges it crosses
struct Global_piece {
  int  net = -1, region = -1, a = -1, b = -1;
  bool h_first = true;
};
// The tracks the routes fixed ahead (hops, fixed trees, IO pre-routes) take on each tile edge, one per
// net and edge it crosses (keyed by the left / lower tile), instead of a whole tile per route piece: a hop takes its
// boundary edge only, a trunk the edges along it
struct Edge_reserve {
  std::vector<int> h, v;
};
Edge_reserve edge_reserve(const Grid& g, const std::vector<std::pair<int, int>>& hops, const std::vector<int>& hop_net,
                          const std::vector<Fixed_tree>& trees, const std::vector<Io_route>& io_routes,
                          const std::vector<Global_piece>& global) {
  Edge_reserve                        res{std::vector<int>(g.size(), 0), std::vector<int>(g.size(), 0)};
  std::set<std::tuple<int, int, int>> seen;  // (net, edge tile, h)
  const auto                          take = [&](int n, int t, int h) {
    if (seen.emplace(n, t, h).second) {
      ++(h ? res.h : res.v)[size_t(t)];
    }
  };
  const auto segment = [&](int n, int ta, int tb, bool h_first) {  // an L from ta to tb: the edges its legs cross
    const int hr = h_first ? g.row(ta) : g.row(tb), vc = h_first ? g.col(tb) : g.col(ta);
    for (int i = std::min(g.col(ta), g.col(tb)); i < std::max(g.col(ta), g.col(tb)); ++i) {
      take(n, g.tile(i, hr), 1);
    }
    for (int j = std::min(g.row(ta), g.row(tb)); j < std::max(g.row(ta), g.row(tb)); ++j) {
      take(n, g.tile(vc, j), 0);
    }
  };
  for (size_t k = 0; k < hops.size(); ++k) {
    const auto [a, b] = hops[k];
    take(hop_net[k], std::min(a, b), g.row(a) == g.row(b) ? 1 : 0);
  }
  for (const auto& ft : trees) {
    for (const auto& piece : ft.pieces) {
      if (!ft.dropped) {
        segment(piece.net, piece.a, piece.b, true);
      }
    }
  }
  for (const auto& io : io_routes) {
    if (io.box < 0) {
      segment(io.net_a, io.ta, io.tb, io.h_first);
    }  // a pin -> box -> pin route: its box moves on the track, the legs are the region's connections
  }
  for (const auto& gp : global) {  // legroute.global: the pass-through runs
    segment(gp.net, gp.a, gp.b, gp.h_first);
  }
  return res;
}

// One region's SAT placement under one policy's options and pre-placement (boxes, edge reservations, nets routed ahead,
// boxes held to a track): the boxes' tiles land in boxes, the connections, adjacent nets and counters in out.
struct Region_out {
  struct Piece {
    int  net = -1, ta = -1, tb = -1;
    bool h_first = true;
  };
  Sat_status                       status  = Sat_status::unknown;
  bool                             skipped = false;
  std::string                      unfit;    // spill rounds: the region had more boxes than fitting tiles (no model)
  std::vector<Piece>               conns;  // the SAT's connections, then the policy's trees and IO pre-routes here
  std::vector<int>                 local;  // 1-1 nets between neighbor boxes
  std::vector<std::pair<int, int>> tiles;      // (box, tile)
  std::vector<std::tuple<int, int, int>> hop_tiles;  // (net, the hop chooser's tile, the tile chosen) per free hop
  Legroute_report                  rep;        // the sat_* counters
  double                           cost = 0, seconds = 0;
};
void solve_region(const Design& d, const Legroute_options& o, const Grid& g, int r, const std::vector<int>& B,
                  const std::vector<int>& T, Boxes& boxes, const std::vector<Net_ends>& ends,
                  const std::set<std::pair<int, int>>& treed, const std::unordered_map<int, std::vector<int>>& allowed,
                  const std::vector<char>& pinned_tile, const std::string& tag, const std::vector<int>& cap_h0,
                  const std::vector<int>& cap_v0, const Edge_reserve& reserve, const std::vector<std::vector<int>>& hop_tiles,
                  const std::vector<int>& hop_room, Region_out& out) {
  auto&      rep   = out.rep;
  const auto start = Clock::now();
  if (T.empty()) {
    return;
  }
  int c0 = g.cols, c1 = -1, r0 = g.rows, r1 = -1;
  for (int t : T) {
    c0 = std::min(c0, g.col(t));
    c1 = std::max(c1, g.col(t));
    r0 = std::min(r0, g.row(t));
    r1 = std::max(r1, g.row(t));
  }
  Cnf                          cnf;
  // order literals of a box: col >= k for k in (c0, c1], row >= k for k in (r0, r1]
  std::unordered_map<int, int> slot;  // box -> index
  for (int i = 0; i < int(B.size()); ++i) {
    slot[B[i]] = i;
  }
  std::vector<std::vector<int>> X(B.size()), Y(B.size());
  for (size_t i = 0; i < B.size(); ++i) {
    for (int k = c0 + 1; k <= c1; ++k) {
      X[i].push_back(cnf.var());
      if (k > c0 + 1) {
        cnf.add({-X[i].back(), X[i][X[i].size() - 2]});
      }
    }
    for (int k = r0 + 1; k <= r1; ++k) {
      Y[i].push_back(cnf.var());
      if (k > r0 + 1) {
        cnf.add({-Y[i].back(), Y[i][Y[i].size() - 2]});
      }
    }
  }
  // free hops (hop_choice): a crossing net's hop this region chooses among its candidate boundary tiles: order
  // literals over the candidates' hull and a tile literal per candidate (no tile is taken by it), and at most
  // hop_room hops on one boundary tile
  struct Free_hop {
    int                              id = -1, net = -1, def = -1, c0 = 0, c1 = 0, r0 = 0, r1 = 0;
    std::vector<int>                 X, Y;
    std::vector<std::pair<int, int>> tiles;  // (tile, literal)
  };
  std::vector<Free_hop>        hops_free;
  std::unordered_map<int, int> hslot;  // hop id -> index in hops_free
  {
    std::map<int, std::vector<int>> hops_at;  // boundary tile -> the free hops' literals there
    for (size_t n = 0; n < d.nets.size(); ++n) {
      const auto it = ends[n].per_region.find(r);
      if (it == ends[n].per_region.end() || it->second.size() < 2 || treed.contains({int(n), r})) {
        continue;
      }
      for (const auto& e : it->second) {
        if (e.box >= 0 || e.hop < 0 || size_t(e.hop) >= hop_tiles.size() || hop_tiles[size_t(e.hop)].size() < 2 || hslot.contains(e.hop)) {
          continue;
        }
        Free_hop fh;
        fh.id  = e.hop;
        fh.net = int(n);
        fh.def = e.tile;
        fh.c0 = g.cols;
        fh.c1 = -1;
        fh.r0 = g.rows;
        fh.r1 = -1;
        for (int t : hop_tiles[size_t(e.hop)]) {
          fh.c0 = std::min(fh.c0, g.col(t));
          fh.c1 = std::max(fh.c1, g.col(t));
          fh.r0 = std::min(fh.r0, g.row(t));
          fh.r1 = std::max(fh.r1, g.row(t));
        }
        for (int k = fh.c0 + 1; k <= fh.c1; ++k) {
          fh.X.push_back(cnf.var());
          if (k > fh.c0 + 1) {
            cnf.add({-fh.X.back(), fh.X[fh.X.size() - 2]});
          }
        }
        for (int k = fh.r0 + 1; k <= fh.r1; ++k) {
          fh.Y.push_back(cnf.var());
          if (k > fh.r0 + 1) {
            cnf.add({-fh.Y.back(), fh.Y[fh.Y.size() - 2]});
          }
        }
        const auto cge = [&](int k) { return k <= fh.c0 ? cnf.t : k > fh.c1 ? -cnf.t : fh.X[size_t(k - fh.c0 - 1)]; };
        const auto rge = [&](int k) { return k <= fh.r0 ? cnf.t : k > fh.r1 ? -cnf.t : fh.Y[size_t(k - fh.r0 - 1)]; };
        std::vector<int> any;
        for (int t : hop_tiles[size_t(e.hop)]) {
          const int p = cnf.var();
          cnf.add({-p, cge(g.col(t))});
          cnf.add({-p, -cge(g.col(t) + 1)});
          cnf.add({-p, rge(g.row(t))});
          cnf.add({-p, -rge(g.row(t) + 1)});
          fh.tiles.emplace_back(t, p);
          any.push_back(p);
          hops_at[t].push_back(p);
        }
        cnf.add(any);
        hslot[e.hop] = int(hops_free.size());
        hops_free.push_back(std::move(fh));
      }
    }
    for (const auto& [t, lits] : hops_at) {
      const int room = size_t(t) < hop_room.size() ? hop_room[size_t(t)] : 0;
      if (int(lits.size()) > room) {
        cnf.at_most(lits, std::max(0, room));
      }
    }
  }
  const auto free_hop = [&](const Endpoint& e) -> const Free_hop* {
    if (e.box >= 0 || e.hop < 0) {
      return nullptr;
    }
    const auto hs = hslot.find(e.hop);
    return hs == hslot.end() ? nullptr : &hops_free[size_t(hs->second)];
  };
  const auto col_ge = [&](const Endpoint& e, int k) {
    if (e.box < 0) {
      if (const auto* fh = free_hop(e)) {
        return k <= fh->c0 ? cnf.t : k > fh->c1 ? -cnf.t : fh->X[size_t(k - fh->c0 - 1)];
      }
      return g.col(e.tile) >= k ? cnf.t : -cnf.t;
    }
    const int i = slot.at(e.box);
    return k <= c0 ? cnf.t : k > c1 ? -cnf.t : X[size_t(i)][size_t(k - c0 - 1)];
  };
  const auto row_ge = [&](const Endpoint& e, int k) {
    if (e.box < 0) {
      if (const auto* fh = free_hop(e)) {
        return k <= fh->r0 ? cnf.t : k > fh->r1 ? -cnf.t : fh->Y[size_t(k - fh->r0 - 1)];
      }
      return g.row(e.tile) >= k ? cnf.t : -cnf.t;
    }
    const int i = slot.at(e.box);
    return k <= r0 ? cnf.t : k > r1 ? -cnf.t : Y[size_t(i)][size_t(k - r0 - 1)];
  };
  // each box stays in a window of window_tiles around its placement tile (grown until it holds a few tiles the box
  // fits): the window bounds every leg and capacity term, so the model grows with the region, not its square
  struct Win {
    int c0, c1, r0, r1;
  };
  std::vector<Win> win(B.size());
  for (size_t i = 0; i < B.size(); ++i) {
    const auto& box = boxes.boxes[B[i]];
    const int   c = std::clamp(g.col_of(box.cx), c0, c1), rr = std::clamp(g.row_of(box.cy), r0, r1);
    if (box.fixed) {  // a pinned box: its tile
      win[i] = {g.col(box.tile), g.col(box.tile), g.row(box.tile), g.row(box.tile)};
    }
    const auto al = allowed.find(B[i]);
    if (al != allowed.end()) {  // on an IO pre-route's track: its tiles
      win[i] = {g.cols, -1, g.rows, -1};
      for (int t : al->second) {
        win[i] = {std::min(win[i].c0, g.col(t)),
                  std::max(win[i].c1, g.col(t)),
                  std::min(win[i].r0, g.row(t)),
                  std::max(win[i].r1, g.row(t))};
      }
    }
    for (int w = box.window >= 0 ? box.window : int(o.window_tiles); !box.fixed && al == allowed.end(); ++w) {
      win[i]   = {std::max(c0, c - w), std::min(c1, c + w), std::max(r0, rr - w), std::min(r1, rr + w)};
      int fits = 0;
      for (int t : T) {
        fits += g.col(t) >= win[i].c0 && g.col(t) <= win[i].c1 && g.row(t) >= win[i].r0 && g.row(t) <= win[i].r1 && !g.blocked[t]
                && !pinned_tile[t] && box_fits(d, g, box, t);
      }
      if (fits >= 4 || (win[i].c0 == c0 && win[i].c1 == c1 && win[i].r0 == r0 && win[i].r1 == r1)) {
        break;
      }
    }
  }
  std::set<int> lifted;  // boxes taken off their IO track (the track stays reserved, the box is free again)
  const auto    on_track = [&](int b, int t) {
    const auto it = allowed.find(b);
    return it == allowed.end() || lifted.contains(b) || std::ranges::find(it->second, t) != it->second.end();
  };
  // the windows together must hold the boxes (fixed boxes take tiles in clusters, so a few free tiles per window are
  // not enough): grow the windows of the boxes a matching leaves out until every box has a tile
  const auto window_tiles = [&](size_t i) {
    std::vector<int> list;
    const auto&      box = boxes.boxes[B[i]];
    for (int t : T) {
      if (g.col(t) >= win[i].c0 && g.col(t) <= win[i].c1 && g.row(t) >= win[i].r0 && g.row(t) <= win[i].r1 && !g.blocked[t]
          && (box.fixed || !pinned_tile[t]) && box_fits(d, g, box, t) && on_track(B[i], t)) {
        list.push_back(t);
      }
    }
    return list;
  };
  // the fallback placement: boxes on fitting tiles of their windows, nearest first, by a bipartite matching
  // (augmenting paths, so a box with few fitting tiles still gets one); a box whose window is full takes any fitting
  // tile of the region. Used for a region without a routable answer, and by freeze for every region.
  const auto fallback_tiles = [&](const std::function<std::vector<int>(size_t)>& candidates) {
    std::unordered_map<int, int>  owner;  // tile -> box index
    std::vector<std::vector<int>> pref(B.size());
    const auto                    nearest = [&](size_t i) {
      const auto& box = boxes.boxes[B[i]];
      std::ranges::sort(pref[i], [&](int x, int y) {
        return std::abs(double(g.cx(g.col(x))) - box.cx) + std::abs(double(g.cy(g.row(x))) - box.cy)
               < std::abs(double(g.cx(g.col(y))) - box.cx) + std::abs(double(g.cy(g.row(y))) - box.cy);
      });
    };
    for (size_t i = 0; i < B.size(); ++i) {
      pref[i] = candidates(i);
      nearest(i);
    }
    std::vector<char>        seen;
    std::function<bool(int)> augment = [&](int i) {
      for (int t : pref[size_t(i)]) {
        if (seen[size_t(t)]) {
          continue;
        }
        seen[size_t(t)] = 1;
        const auto it   = owner.find(t);
        if (it == owner.end() || augment(it->second)) {
          owner[t] = i;
          return true;
        }
      }
      return false;
    };
    for (size_t i = 0; i < B.size(); ++i) {
      seen.assign(g.size(), 0);
      if (augment(int(i))) {
        continue;
      }
      const auto& box = boxes.boxes[B[i]];  // its window is full: any tile of the region it fits, nearest first
      pref[i].clear();
      for (int t : T) {
        if (!g.blocked[size_t(t)] && (box.fixed || !pinned_tile[size_t(t)]) && box_fits(d, g, box, t)) {
          pref[i].push_back(t);
        }
      }
      nearest(i);
      seen.assign(g.size(), 0);
      if (!augment(int(i))) {
        throw Legroute_unfit(std::format("legroute: region {} has more boxes than fitting tiles", r));
      }
    }
    std::vector<int> tile(B.size(), -1);
    for (const auto& [t, i] : owner) {
      tile[size_t(i)] = t;
    }
    return tile;
  };
  for (int round = 0;; ++round) {
    std::vector<std::vector<int>> pref(B.size());
    for (size_t i = 0; i < B.size(); ++i) {
      pref[i] = window_tiles(i);
    }
    // a maximum matching: the boxes left out grow
    std::unordered_map<int, int> owner;
    std::vector<char>            seen(g.size(), 0);
    std::vector<int>             seen_list;
    std::function<bool(int)>     augment = [&](int i) {
      for (int t : pref[i]) {
        if (seen[t]) {
          continue;
        }
        seen[t] = 1;
        seen_list.push_back(t);
        const auto it = owner.find(t);
        if (it == owner.end() || augment(it->second)) {
          owner[t] = i;
          return true;
        }
      }
      return false;
    };
    std::vector<size_t> left;
    for (size_t i = 0; i < B.size(); ++i) {
      if (!augment(int(i))) {
        left.push_back(i);
      }
      for (int t : seen_list) {
        seen[t] = 0;
      }
      seen_list.clear();
    }
    if (left.empty() || round > g.cols + g.rows) {
      break;
    }
    for (size_t i : left) {
      if (allowed.contains(B[i]) && lifted.insert(B[i]).second) {  // off its IO track
        ++rep.io_lifted;
      }
      win[i] = {std::max(c0, win[i].c0 - 1), std::min(c1, win[i].c1 + 1), std::max(r0, win[i].r0 - 1), std::min(r1, win[i].r1 + 1)};
      ++rep.sat_relaxed_windows;
    }
  }
  if (o.freeze) {  // every box on the tile the fallback gives it (the placement a run without an answer keeps and an
    // external router can be given): the SAT decides the route shapes only
    const auto tiles = fallback_tiles(window_tiles);
    for (size_t i = 0; i < B.size(); ++i) {
      if (tiles[i] >= 0) {
        win[i] = {g.col(tiles[i]), g.col(tiles[i]), g.row(tiles[i]), g.row(tiles[i])};
      }
    }
    rep.frozen_boxes += B.size();
  }
  if (out.skipped) {  // fail_fast skipped this region: its boxes take fitting tiles (the fallback), nothing is solved
    const auto tiles = fallback_tiles(window_tiles);
    for (size_t i = 0; i < B.size(); ++i) {
      if (tiles[i] >= 0) {
        boxes.boxes[B[i]].tile = tiles[i];
      }
    }
    for (int b : B) {
      out.tiles.emplace_back(b, boxes.boxes[b].tile);
    }
    rep.sat_regions += 1;
    ++rep.sat_skipped_regions;
    out.status = Sat_status::unknown;
    return;
  }
  for (size_t i = 0; i < B.size(); ++i) {
    const Endpoint e{B[i], -1};
    cnf.add({col_ge(e, win[i].c0)});
    cnf.add({-col_ge(e, win[i].c1 + 1)});
    cnf.add({row_ge(e, win[i].r0)});
    cnf.add({-row_ge(e, win[i].r1 + 1)});
  }
  const auto win_of = [&](const Endpoint& e) {
    if (e.box >= 0) {
      return win[slot.at(e.box)];
    }
    if (const auto* fh = free_hop(e)) {
      return Win{fh->c0, fh->c1, fh->r0, fh->r1};
    }
    return Win{g.col(e.tile), g.col(e.tile), g.row(e.tile), g.row(e.tile)};
  };
  const std::set<int> here(T.begin(), T.end());
  // the cut bound: a net with an endpoint held on each side of a tile-column (row) boundary covers an edge across it,
  // so the nets forced across a boundary are at most the tracks of its edges. A region over the bound is unsat before
  // any solving, and the boundary is the diagnosis. A 2-pin net between boxes that may sit side by side across the
  // boundary takes no track (in-box metal) and does not count.
  if (!o.sat_relax_rounds) {
    std::vector<int> fc(size_t(c1 - c0 + 2), 0), fr(size_t(r1 - r0 + 2), 0);  // forced nets per boundary, as differences
    for (size_t n = 0; n < d.nets.size(); ++n) {
      const auto it = ends[n].per_region.find(r);
      if (it == ends[n].per_region.end() || it->second.size() < 2 || treed.contains({int(n), r})) {
        continue;
      }
      int lo_c = g.cols, hi_c = -1, lo_r = g.rows, hi_r = -1;  // the leftmost endpoint's last column, the rightmost's first
      for (const auto& e : it->second) {
        const auto wn = win_of(e);
        lo_c          = std::min(lo_c, std::clamp(wn.c1, c0, c1));
        hi_c          = std::max(hi_c, std::clamp(wn.c0, c0, c1));
        lo_r          = std::min(lo_r, std::clamp(wn.r1, r0, r1));
        hi_r          = std::max(hi_r, std::clamp(wn.r0, r0, r1));
      }
      if (it->second.size() == 2 && d.nets[n].pins.size() == 2 && it->second[0].box >= 0 && it->second[1].box >= 0) {
        const auto wa = win_of(it->second[0]), wb = win_of(it->second[1]);
        if (hi_c == lo_c + 1 && wa.r0 <= wb.r1 && wb.r0 <= wa.r1) {
          hi_c = lo_c;  // side by side across its one forced column boundary
        }
        if (hi_r == lo_r + 1 && wa.c0 <= wb.c1 && wb.c0 <= wa.c1) {
          hi_r = lo_r;
        }
      }
      if (hi_c > lo_c) {  // forced across the column boundaries c with lo_c < c <= hi_c
        ++fc[size_t(lo_c + 1 - c0)];
        --fc[size_t(hi_c + 1 - c0)];
      }
      if (hi_r > lo_r) {
        ++fr[size_t(lo_r + 1 - r0)];
        --fr[size_t(hi_r + 1 - r0)];
      }
    }
    struct Cut {
      int  at = -1, forced = 0, tracks = 0;
      bool h = true;  // a column boundary (H edges cross it) or a row boundary (V edges)
    } worst;
    for (int c = c0 + 1, run = 0; c <= c1; ++c) {
      run += fc[size_t(c - c0)];
      int tracks = 0;
      for (int j = r0; j <= r1; ++j) {
        const int t = g.tile(c - 1, j), q = g.tile(c, j);
        if (here.contains(t) && here.contains(q)) {
          tracks += std::max(0, int(std::floor(std::min(cap_h0[t], cap_h0[q]) * o.cap_scale)) - reserve.h[size_t(t)]);
        }
      }
      if (run - tracks > worst.forced - worst.tracks) {
        worst = {c, run, tracks, true};
      }
    }
    for (int j = r0 + 1, run = 0; j <= r1; ++j) {
      run += fr[size_t(j - r0)];
      int tracks = 0;
      for (int i = c0; i <= c1; ++i) {
        const int t = g.tile(i, j - 1), q = g.tile(i, j);
        if (here.contains(t) && here.contains(q)) {
          tracks += std::max(0, int(std::floor(std::min(cap_v0[t], cap_v0[q]) * o.cap_scale)) - reserve.v[size_t(t)]);
        }
      }
      if (run - tracks > worst.forced - worst.tracks) {
        worst = {j, run, tracks, false};
      }
    }
    if (worst.at >= 0) {
      ++rep.sat_regions;
      ++rep.sat_unsat_regions;
      ++rep.sat_cut_regions;
      const double secs   = seconds_since(start);
      rep.sat_seconds_max = std::max(rep.sat_seconds_max, secs);
      std::fprintf(stderr,
                   "legroute sat region %d: boxes %zu tiles %zu status unsat by the cut bound: %d nets forced across %s %d, "
                   "%d tracks, %.2f s%s\n",
                   r,
                   B.size(),
                   T.size(),
                   worst.forced,
                   worst.h ? "column boundary" : "row boundary",
                   worst.at,
                   worst.tracks,
                   secs,
                   tag.c_str());
      out.status       = Sat_status::unsatisfiable;
      const auto tiles = fallback_tiles(window_tiles);  // the region keeps its boxes on fitting tiles (not routable)
      for (size_t i = 0; i < B.size(); ++i) {
        if (tiles[i] >= 0) {
          boxes.boxes[B[i]].tile = tiles[i];
        }
      }
      for (int b : B) {
        out.tiles.emplace_back(b, boxes.boxes[b].tile);
      }
      return;
    }
  }
  // tile literals: a box on a free tile it fits; one tile per box (the order literals pin it), one box per tile
  std::unordered_map<int, std::vector<std::pair<int, int>>> at_tile;  // tile -> (box index, literal)
  std::vector<std::vector<std::pair<int, int>>>             tiles_of(B.size());
  for (size_t i = 0; i < B.size(); ++i) {
    const auto&      box = boxes.boxes[B[i]];
    std::vector<int> any;
    for (int t : T) {
      if (g.col(t) < win[i].c0 || g.col(t) > win[i].c1 || g.row(t) < win[i].r0 || g.row(t) > win[i].r1 || g.blocked[t]
          || (pinned_tile[t] && !box.fixed) || !box_fits(d, g, box, t) || !on_track(B[i], t)) {
        continue;
      }
      const int      p = cnf.var();
      const Endpoint e{B[i], -1};
      cnf.add({-p, col_ge(e, g.col(t))});
      cnf.add({-p, -col_ge(e, g.col(t) + 1)});
      cnf.add({-p, row_ge(e, g.row(t))});
      cnf.add({-p, -row_ge(e, g.row(t) + 1)});
      at_tile[t].emplace_back(int(i), p);
      tiles_of[i].emplace_back(t, p);
      any.push_back(p);
    }
    if (any.empty()) {
      throw Legroute_unfit(std::format("legroute: box {} fits no tile of region {}", B[i], r));
    }
    cnf.add(any);
  }
  for (auto& [t, list] : at_tile) {
    std::vector<int> xs;
    for (const auto& [_, p] : list) {
      xs.push_back(p);
    }
    cnf.at_most(xs, 1);
  }
  // connections: from the net's first endpoint in the region (its driver when here) to each other endpoint; a net
  // with 3..steiner_pins endpoints here is one single-trunk Steiner tree instead (below)
  struct Steiner {
    int              net = -1, first = -1, count = 0;
    int              h_trunk = 0;     // true: an H trunk on a row (rib V), false: a V trunk on a column (ribs H)
    std::vector<int> row_ge, col_ge;  // the trunk's row / column, order encoded over the endpoints' window hull
    int              x0 = 0, x1 = 0, y0 = 0, y1 = 0;
  };
  std::vector<Endpoint>    eps;
  std::vector<Region_conn> conns;
  std::vector<Steiner>     steiner;
  std::vector<int>         nets_here;
  for (size_t n = 0; n < d.nets.size(); ++n) {
    const auto it = ends[n].per_region.find(r);
    if (it == ends[n].per_region.end() || it->second.size() < 2 || treed.contains({int(n), r})) {
      continue;
    }
    nets_here.push_back(int(n));
    const int first = int(eps.size());
    eps.insert(eps.end(), it->second.begin(), it->second.end());
    if (it->second.size() >= std::max(2u, o.steiner_min) && it->second.size() <= o.steiner_pins) {
      steiner.push_back({int(n), first, int(it->second.size())});
      continue;
    }
    for (int k = first + 1; k < int(eps.size()); ++k) {
      conns.push_back({int(n), first, k});
    }
  }
  // per (net, tile, direction): the net covers the tile
  std::map<std::tuple<int, int, int>, int> cover;
  const auto                               cover_lit = [&](int n, int t, bool h) {
    auto [it, fresh] = cover.try_emplace({n, t, h ? 1 : 0}, 0);
    if (fresh) {
      it->second = cnf.var();
    }
    return it->second;
  };
  std::vector<int> orient(conns.size()), adjacent(conns.size(), 0), near(conns.size(), 0);
  for (size_t k = 0; k < conns.size(); ++k) {
    const auto& cn  = conns[k];
    const auto& a   = eps[cn.a];
    const auto& b   = eps[cn.b];
    const int   ori = cnf.var();  // true: H-first (H leg on a's row, V leg on b's column)
    orient[k]       = ori;
    // the legs stay inside the hull of the two endpoints' windows
    const auto wa = win_of(a), wb = win_of(b);
    const int  x0 = std::min(wa.c0, wb.c0), x1 = std::max(wa.c1, wb.c1), y0 = std::min(wa.r0, wb.r0), y1 = std::max(wa.r1, wb.r1);
    // a 1-1 net between two boxes costs no track when the boxes are neighbors (in-box metal): lone may hold only
    // then (columns or rows equal, the other off by one)
    int        lone = -cnf.t;
    if (a.box >= 0 && b.box >= 0 && d.nets[cn.net].pins.size() == 2) {
      lone          = cnf.var();
      const int eqc = cnf.var(), eqr = cnf.var();
      for (int i = x0; i <= x1 + 1; ++i) {
        cnf.add({-lone, -col_ge(a, i + 1), col_ge(b, i)});
        cnf.add({-lone, -col_ge(b, i + 1), col_ge(a, i)});
        cnf.add({-eqc, -col_ge(a, i), col_ge(b, i)});
        cnf.add({-eqc, col_ge(a, i), -col_ge(b, i)});
      }
      for (int j = y0; j <= y1 + 1; ++j) {
        cnf.add({-lone, -row_ge(a, j + 1), row_ge(b, j)});
        cnf.add({-lone, -row_ge(b, j + 1), row_ge(a, j)});
        cnf.add({-eqr, -row_ge(a, j), row_ge(b, j)});
        cnf.add({-eqr, row_ge(a, j), -row_ge(b, j)});
      }
      cnf.add({-lone, eqc, eqr});
      adjacent[k] = lone;
    }
    // a critical connection between two boxes: near may hold only when the boxes are within one tile of each other
    // in both directions (a tier's literal; the adjacency above also needs a shared row or column)
    if (a.box >= 0 && b.box >= 0 && !o.tiers.empty() && d.nets[cn.net].rank < o.tier_fraction) {
      const int nr = cnf.var();
      for (int i = x0; i <= x1 + 1; ++i) {
        cnf.add({-nr, -col_ge(a, i + 1), col_ge(b, i)});
        cnf.add({-nr, -col_ge(b, i + 1), col_ge(a, i)});
      }
      for (int j = y0; j <= y1 + 1; ++j) {
        cnf.add({-nr, -row_ge(a, j + 1), row_ge(b, j)});
        cnf.add({-nr, -row_ge(b, j + 1), row_ge(a, j)});
      }
      near[k] = nr;
    }
    // a leg of zero length takes no track of its direction (same column: the V leg alone covers the tiles)
    const int same_c = cnf.var(), same_r = cnf.var();
    for (int i = x0; i <= x1 + 1; ++i) {
      cnf.add({-same_c, -col_ge(a, i), col_ge(b, i)});
      cnf.add({-same_c, col_ge(a, i), -col_ge(b, i)});
    }
    for (int j = y0; j <= y1 + 1; ++j) {
      cnf.add({-same_r, -row_ge(a, j), row_ge(b, j)});
      cnf.add({-same_r, row_ge(a, j), -row_ge(b, j)});
    }
    // between literals: column i between a's and b's columns, row j between their rows
    std::vector<int> bc(x1 - x0 + 1), br(y1 - y0 + 1);
    for (int i = x0; i <= x1; ++i) {
      const int v = bc[i - x0] = cnf.var();
      for (const auto* x : {&a, &b}) {
        for (const auto* y : {&a, &b}) {
          cnf.add({col_ge(*x, i + 1), -col_ge(*y, i), v});  // col(x) <= i and col(y) >= i
        }
      }
    }
    for (int j = y0; j <= y1; ++j) {
      const int v = br[j - y0] = cnf.var();
      for (const auto* x : {&a, &b}) {
        for (const auto* y : {&a, &b}) {
          cnf.add({row_ge(*x, j + 1), -row_ge(*y, j), v});
        }
      }
    }
    // a leg lies on an endpoint's row or column: only that endpoint's window rows (H) or columns (V) cross the span
    const auto lit = [&](int i, int j, bool h) {
      const int t = g.tile(i, j);
      return g.region[t] == r ? cover_lit(cn.net, t, h) : -cnf.t;
    };
    for (const auto& [e, w, first] : {
             std::tuple{&a, wa,  true},
             std::tuple{&b, wb, false}
    }) {
      const int sel = first ? -ori : ori;   // H-first: H leg on row(a), V leg on col(b); V-first: the other way
      for (int j = w.r0; j <= w.r1; ++j) {  // H leg on row(e)
        for (int i = x0; i <= x1; ++i) {
          cnf.add({sel, -row_ge(*e, j), row_ge(*e, j + 1), -bc[i - x0], lone, same_c, lit(i, j, true)});
        }
      }
    }
    for (const auto& [e, w, h_first] : {
             std::tuple{&b, wb,  true},
             std::tuple{&a, wa, false}
    }) {
      const int sel = h_first ? -ori : ori;  // the V leg: col(b) when H-first, col(a) when V-first
      for (int i = w.c0; i <= w.c1; ++i) {
        for (int j = y0; j <= y1; ++j) {
          cnf.add({sel, -col_ge(*e, i), col_ge(*e, i + 1), -br[j - y0], lone, same_r, lit(i, j, false)});
        }
      }
    }
  }
  // single-trunk Steiner trees (the RSMT for three endpoints): a trunk on a row (H) or a column (V) from the first to
  // the last endpoint along it, and a perpendicular rib from each endpoint to the trunk; the trunk's row or column is
  // a variable over the hull of the endpoints' windows
  for (auto& st : steiner) {
    const auto h = [&](int k) -> const Endpoint& { return eps[size_t(st.first + k)]; };
    st.x0 = g.cols, st.x1 = -1, st.y0 = g.rows, st.y1 = -1;
    for (int k = 0; k < st.count; ++k) {
      const auto w = win_of(h(k));
      st.x0        = std::min(st.x0, w.c0);
      st.x1        = std::max(st.x1, w.c1);
      st.y0        = std::min(st.y0, w.r0);
      st.y1        = std::max(st.y1, w.r1);
    }
    st.h_trunk = cnf.var();
    for (auto [axis, lo, hi] : {
             std::tuple{&st.row_ge, st.y0, st.y1},
             std::tuple{&st.col_ge, st.x0, st.x1}
    }) {
      for (int k = lo + 1; k <= hi; ++k) {
        axis->push_back(cnf.var());
        if (axis->size() > 1) {
          cnf.add({-axis->back(), (*axis)[axis->size() - 2]});
        }
      }
    }
    const auto axis_ge = [&](bool rows, int k) {
      const auto& v  = rows ? st.row_ge : st.col_ge;
      const int   lo = rows ? st.y0 : st.x0, hi = rows ? st.y1 : st.x1;
      return k <= lo ? cnf.t : k > hi ? -cnf.t : v[size_t(k - lo - 1)];
    };
    const auto lit = [&](int i, int j, bool hh) {
      const int t = g.tile(i, j);
      return g.region[t] == r ? cover_lit(st.net, t, hh) : -cnf.t;
    };
    for (bool ht : {true, false}) {
      const int        sel = ht ? -st.h_trunk : st.h_trunk;  // a clause holds when this orientation is chosen
      // between along the trunk: some endpoint at or before k and some at or after it
      const int        a0 = ht ? st.x0 : st.y0, a1 = ht ? st.x1 : st.y1;  // along
      const int        m0 = ht ? st.y0 : st.x0, m1 = ht ? st.y1 : st.x1;  // across (the axis)
      std::vector<int> between(size_t(a1 - a0 + 1));
      const auto       along_ge  = [&](const Endpoint& e, int k) { return ht ? col_ge(e, k) : row_ge(e, k); };
      const auto       across_ge = [&](const Endpoint& e, int k) { return ht ? row_ge(e, k) : col_ge(e, k); };
      for (int k = a0; k <= a1; ++k) {
        const int v = between[size_t(k - a0)] = cnf.var();
        for (int x = 0; x < st.count; ++x) {
          for (int y = 0; y < st.count; ++y) {
            cnf.add({along_ge(h(x), k + 1), -along_ge(h(y), k), v});
          }
        }
      }
      for (int m = m0; m <= m1; ++m) {  // the trunk on axis m
        for (int k = a0; k <= a1; ++k) {
          cnf.add({sel, -axis_ge(ht, m), axis_ge(ht, m + 1), -between[size_t(k - a0)], ht ? lit(k, m, true) : lit(m, k, false)});
        }
      }
      for (int x = 0; x < st.count; ++x) {  // the ribs: along fixed at the endpoint, across from it to the axis
        const auto w  = win_of(h(x));
        const int  b0 = ht ? w.c0 : w.r0, b1 = ht ? w.c1 : w.r1;
        const int  on = cnf.var();  // the endpoint lies on the trunk: no rib (a zero-length rib takes no track)
        for (int m = m0; m <= m1 + 1; ++m) {
          cnf.add({-on, -across_ge(h(x), m), axis_ge(ht, m)});
          cnf.add({-on, across_ge(h(x), m), -axis_ge(ht, m)});
        }
        for (int k = b0; k <= b1; ++k) {
          for (int m = m0; m <= m1; ++m) {
            const int cov = ht ? lit(k, m, false) : lit(m, k, true);
            // endpoint at or below m, axis at or above m; and the mirror
            cnf.add({sel, on, -along_ge(h(x), k), along_ge(h(x), k + 1), across_ge(h(x), m + 1), -axis_ge(ht, m), cov});
            cnf.add({sel, on, -along_ge(h(x), k), along_ge(h(x), k + 1), -across_ge(h(x), m), axis_ge(ht, m + 1), cov});
          }
        }
      }
    }
  }
  // capacity per tile edge and direction: the nets that cross the edge (cover both its tiles). The in-box wires keep
  // to their own tile (charging them on the edges rejected a placement OpenROAD routed), and two nets that end in a
  // tile from opposite sides share a track, each on its own edge
  std::map<std::pair<int, int>, std::vector<int>> inputs;  // (left / lower tile, h) -> the crossing literals
  const auto next_tile = [&](int t, int h) {  // the neighbor across the right (H) or upper (V) edge, -1 if outside
    const int q = h ? (g.col(t) + 1 < g.cols ? t + 1 : -1) : (t + g.cols < int(g.size()) ? t + g.cols : -1);
    return q >= 0 && here.contains(q) ? q : -1;
  };
  for (const auto& [key, lit] : cover) {
    const auto [n, t, h] = key;
    const int q          = next_tile(t, h);
    if (q < 0) {
      continue;
    }
    if (const auto it = cover.find({n, q, h}); it != cover.end()) {
      const int e = cnf.var();  // the net crosses the edge t | q
      cnf.add({-lit, -it->second, e});
      inputs[{t, h}].push_back(e);
    }
  }
  size_t       binding = 0;
  const size_t cl_conn = cnf.clauses;
  size_t       cap_in = 0, cap_k = 0, cap_n = 0, cap_est = 0;
  struct Bound {
    int t, h, k, over;
  };
  std::vector<Bound> bounds;  // with sat_relax_rounds: each edge's bound is an assumption
  // capacity_lazy: a bound is added only once an answer overflows it (1: the V bounds, 2: every bound); the model
  // starts with the placement and the legs alone, and pays for counting where the answers are tight
  struct Lazy {
    int              t, h, k;
    std::vector<int> xs;
  };
  std::vector<Lazy> lazy;
  const auto        lazy_dir = [&](int h) { return o.capacity_lazy && !o.sat_relax_rounds && (o.capacity_lazy == 2 || !h); };
  for (int t : T) {
    for (int h : {1, 0}) {
      const int q = next_tile(t, h);
      if (q < 0) {
        continue;  // no edge of the region to the right / above
      }
      auto&     xs  = inputs[{t, h}];
      // the edge's tracks: the physical tracks of the narrower tile less the routes fixed ahead that cross the edge
      const int k   = std::max(0,
                               int(std::floor(std::min((h ? cap_h0 : cap_v0)[t], (h ? cap_h0 : cap_v0)[q]) * o.cap_scale))
                                   - (h ? reserve.h : reserve.v)[size_t(t)]);
      binding      += int(xs.size()) > k;
      cap_in       += xs.size();
      cap_k        += k;
      ++cap_n;
      cap_est += at_most_clauses(int(xs.size()), k);
      if (lazy_dir(h)) {
        if (int(xs.size()) > k) {
          lazy.push_back({t, h, k, xs});
        }
      } else if (!o.sat_relax_rounds) {
        cnf.at_most(xs, k);
      } else if (const int ov = cnf.over(xs, k)) {
        bounds.push_back({t, h, k, ov});
      }
    }
  }
  std::fprintf(stderr,
               "legroute sat region %d model: clauses connections %zu capacity %zu, per capacity row inputs %.1f bound %.1f\n",
               r,
               cl_conn,
               cnf.clauses - cl_conn,
               double(cap_in) / double(std::max<size_t>(1, cap_n)),
               double(cap_k) / double(std::max<size_t>(1, cap_n)));
  {  // diagnostic: per edge, the routes fixed ahead plus the straight connections between two fixed endpoints (no
    // shape choice) against the edge's tracks: an edge over here is UNSAT before any search
    const auto fixed_at = [&](const Endpoint& e) {
      return e.box < 0 ? (free_hop(e) ? -1 : e.tile) : boxes.boxes[e.box].fixed ? boxes.boxes[e.box].tile : -1;
    };
    std::map<std::pair<int, int>, int> straight;  // (edge tile, h) -> straight fixed connections crossing it
    std::map<std::pair<int, int>, int> through;   // of those, between two fixed tiles (hops, pins): pass-through nets
    for (const auto& cn : conns) {
      const int ta = fixed_at(eps[size_t(cn.a)]), tb = fixed_at(eps[size_t(cn.b)]);
      if (ta < 0 || tb < 0 || ta == tb || (g.row(ta) != g.row(tb) && g.col(ta) != g.col(tb))) {
        continue;
      }
      const bool pass = eps[size_t(cn.a)].box < 0 && eps[size_t(cn.b)].box < 0;
      if (pass) {
        if (g.row(ta) == g.row(tb)) {
          for (int i = std::min(g.col(ta), g.col(tb)); i < std::max(g.col(ta), g.col(tb)); ++i) {
            ++through[{g.tile(i, g.row(ta)), 1}];
          }
        } else {
          for (int j = std::min(g.row(ta), g.row(tb)); j < std::max(g.row(ta), g.row(tb)); ++j) {
            ++through[{g.tile(g.col(ta), j), 0}];
          }
        }
      }
      if (eps[size_t(cn.a)].box >= 0 && eps[size_t(cn.b)].box >= 0 && d.nets[cn.net].pins.size() == 2
          && std::abs(g.col(ta) - g.col(tb)) + std::abs(g.row(ta) - g.row(tb)) == 1) {
        continue;  // neighbors: in-box metal
      }
      if (g.row(ta) == g.row(tb)) {
        for (int i = std::min(g.col(ta), g.col(tb)); i < std::max(g.col(ta), g.col(tb)); ++i) {
          ++straight[{g.tile(i, g.row(ta)), 1}];
        }
      } else {
        for (int j = std::min(g.row(ta), g.row(tb)); j < std::max(g.row(ta), g.row(tb)); ++j) {
          ++straight[{g.tile(g.col(ta), j), 0}];
        }
      }
    }
    struct Over {
      int t, h, reserved, straight, tracks;
    };
    std::vector<Over> over;
    for (int t : T) {
      for (int h : {1, 0}) {
        const int q = next_tile(t, h);
        if (q < 0) {
          continue;
        }
        const int tracks = std::max(0, int(std::floor(std::min((h ? cap_h0 : cap_v0)[t], (h ? cap_h0 : cap_v0)[q]) * o.cap_scale)));
        const int res = (h ? reserve.h : reserve.v)[size_t(t)], st = straight[{t, h}];
        if (res + st > tracks) {
          over.push_back({t, h, res, st, tracks});
        }
      }
    }
    std::ranges::sort(over, [](const Over& a, const Over& b) {
      return a.reserved + a.straight - a.tracks > b.reserved + b.straight - b.tracks;
    });
    std::string worst;
    for (size_t i = 0; i < over.size() && i < 5; ++i) {
      worst += std::format(" ({},{}){}: fixed {}+straight {} (pass-through {}) of {}",
                           g.col(over[i].t),
                           g.row(over[i].t),
                           over[i].h ? "H" : "V",
                           over[i].reserved,
                           over[i].straight,
                           through[{over[i].t, over[i].h}],
                           over[i].tracks);
    }
    std::fprintf(stderr, "legroute sat region %d forced edges: %zu over their tracks%s\n", r, over.size(), worst.c_str());
    if (!over.empty()) {  // the straight fixed connections crossing the worst edge: their endpoints (tile, kind)
      std::string list;
      int         shown = 0;
      for (const auto& cn : conns) {
        const int ta = fixed_at(eps[size_t(cn.a)]), tb = fixed_at(eps[size_t(cn.b)]);
        if (ta < 0 || tb < 0 || ta == tb || !(over[0].h ? g.row(ta) == g.row(tb) : g.col(ta) == g.col(tb))) {
          continue;
        }
        const bool crosses = over[0].h ? (g.row(ta) == g.row(over[0].t) && std::min(g.col(ta), g.col(tb)) <= g.col(over[0].t)
                                          && g.col(over[0].t) < std::max(g.col(ta), g.col(tb)))
                                       : (g.col(ta) == g.col(over[0].t) && std::min(g.row(ta), g.row(tb)) <= g.row(over[0].t)
                                          && g.row(over[0].t) < std::max(g.row(ta), g.row(tb)));
        if (!crosses || shown++ >= 12) {
          continue;
        }
        const auto kind = [&](const Endpoint& e, int t) {
          if (e.box >= 0) {
            return std::string("box");
          }
          for (const auto& p : d.nets[cn.net].pins) {
            if (p.term >= 0 && g.tile_of(double(d.fp.terminals[p.term].x), double(d.fp.terminals[p.term].y)) == t) {
              return std::string("pin");
            }
          }
          return std::string("hop");
        };
        list += std::format(" {}:({},{}){}->({},{}){}",
                            d.fp.nets[d.nets[cn.net].frame].name,
                            g.col(ta),
                            g.row(ta),
                            kind(eps[size_t(cn.a)], ta),
                            g.col(tb),
                            g.row(tb),
                            kind(eps[size_t(cn.b)], tb));
      }
      std::fprintf(stderr,
                   "legroute sat region %d forced edge (%d,%d)%s connections:%s\n",
                   r,
                   g.col(over[0].t),
                   g.row(over[0].t),
                   over[0].h ? "H" : "V",
                   list.c_str());
    }
  }
  std::vector<int> assume;
  for (const auto& b : bounds) {
    assume.push_back(-b.over);
  }
  // sat_min_fill: a region this empty is taken as routable without a solve (its boxes on their nearest tiles)
  const auto free_tiles     = std::ranges::count_if(T, [&](int t) { return !g.blocked[t]; });
  out.skipped               = o.sat_min_fill > 0 && double(B.size()) <= o.sat_min_fill * double(free_tiles);
  auto             deadline = Clock::now() + std::chrono::milliseconds(o.sat_ms);
  // sat_seed: the fallback placement is tried first (its tile literals assumed); a routable answer there is kept,
  // an UNSAT drops the assumptions and the free model continues with what the solver learned
  std::vector<int> seed;
  if (o.sat_seed && !o.freeze && !out.skipped) {
    const auto tiles = fallback_tiles(window_tiles);
    for (size_t i = 0; i < B.size(); ++i) {
      if (tiles[i] < 0) {
        continue;
      }
      for (const auto& [t, p] : tiles_of[i]) {
        if (t == tiles[i]) {
          seed.push_back(p);
          break;
        }
      }
    }
  }
  bool       seeded  = !seed.empty();
  const auto left_ms = [&] {
    return unsigned(std::max<long long>(1, std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count()));
  };
  // one solve under the bounds' assumptions, the seed (dropped on its first UNSAT) and extra assumptions
  const auto solve_with = [&](const std::vector<int>& extra) {
    std::vector<int> a = assume;
    a.insert(a.end(), extra.begin(), extra.end());
    if (seeded) {
      std::vector<int> b = a;
      b.insert(b.end(), seed.begin(), seed.end());
      const auto st = cnf.solver.solve(b, o.sat_conflicts, left_ms());
      ++rep.sat_solves;
      if (st != Sat_status::unsatisfiable) {
        return st;
      }
      seeded = false;
    }
    ++rep.sat_solves;
    return cnf.solver.solve(a, o.sat_conflicts, left_ms());
  };
  // the answer's decoded legs (connections, Steiner trunks and ribs) per tile and direction
  const auto decoded = [&] {
    std::map<std::pair<int, int>, std::set<int>> covers;  // (tile, h) -> nets whose legs cover it
    std::vector<int>                             at_box(B.size(), -1);
    for (size_t i = 0; i < B.size(); ++i) {
      for (const auto& [t, p] : tiles_of[i]) {
        if (cnf.solver.value(p)) {
          at_box[i] = t;
        }
      }
    }
    const auto ep_tile = [&](const Endpoint& e) { return e.box >= 0 ? at_box[size_t(slot.at(e.box))] : e.tile; };
    const auto hcover  = [&](int n, int c1, int c2, int rr) {
      for (int i = std::min(c1, c2); i <= std::max(c1, c2); ++i) {
        covers[{g.tile(i, rr), 1}].insert(n);
      }
    };
    const auto vcover = [&](int n, int cc, int r1, int r2) {
      for (int j = std::min(r1, r2); j <= std::max(r1, r2); ++j) {
        covers[{g.tile(cc, j), 0}].insert(n);
      }
    };
    for (size_t k = 0; k < conns.size(); ++k) {
      if (adjacent[k] && cnf.solver.value(adjacent[k])) {
        continue;
      }
      const int  ta = ep_tile(eps[size_t(conns[k].a)]), tb = ep_tile(eps[size_t(conns[k].b)]);
      const bool hf = cnf.solver.value(orient[k]);
      const int  hr = hf ? g.row(ta) : g.row(tb), vc = hf ? g.col(tb) : g.col(ta);
      if (g.col(ta) != g.col(tb)) {
        hcover(conns[k].net, g.col(ta), g.col(tb), hr);
      }
      if (g.row(ta) != g.row(tb)) {
        vcover(conns[k].net, vc, g.row(ta), g.row(tb));
      }
    }
    for (const auto& st : steiner) {
      const bool ht   = cnf.solver.value(st.h_trunk);
      int        axis = ht ? st.y0 : st.x0;
      for (int lit : (ht ? st.row_ge : st.col_ge)) {
        axis += cnf.solver.value(lit) ? 1 : 0;
      }
      int lo = INT32_MAX, hi = INT32_MIN;
      for (int k = 0; k < st.count; ++k) {
        const int t     = ep_tile(eps[size_t(st.first + k)]);
        const int along = ht ? g.col(t) : g.row(t), across = ht ? g.row(t) : g.col(t);
        lo = std::min(lo, along);
        hi = std::max(hi, along);
        if (across != axis) {
          if (ht) {
            vcover(st.net, g.col(t), g.row(t), axis);
          } else {
            hcover(st.net, g.col(t), axis, g.row(t));
          }
        }
      }
      if (lo != hi) {
        if (ht) {
          hcover(st.net, lo, hi, axis);
        } else {
          vcover(st.net, axis, lo, hi);
        }
      }
    }
    return std::pair{covers, at_box};
  };
  unsigned   lazy_rounds = 0;
  size_t     lazy_added  = 0;
  // settle: solve under extra assumptions; an answer that overflows a lazy bound adds the bound and solves again,
  // incrementally, until no bound overflows, UNSAT or the time is out. The answer kept is overflow-free on every
  // bound, lazy or not
  const auto settle      = [&](const std::vector<int>& extra) {
    Sat_status st = solve_with(extra);
    while (st == Sat_status::satisfiable && !lazy.empty()) {
      const auto  dec    = decoded();
      const auto& covers = dec.first;
      size_t      added  = 0;
      for (auto& L : lazy) {
        if (L.xs.empty()) {
          continue;  // added in an earlier round
        }
        int        use = 0;  // the nets crossing the edge: covering both its tiles
        const auto a   = covers.find({L.t, L.h});
        const auto b   = covers.find({next_tile(L.t, L.h), L.h});
        if (a != covers.end() && b != covers.end()) {
          for (int n : a->second) {
            use += b->second.contains(n);
          }
        }
        if (use > L.k) {
          cnf.at_most(L.xs, L.k);
          L.xs.clear();
          L.xs.shrink_to_fit();
          ++added;
        }
      }
      if (!added) {
        break;
      }
      lazy_added += added;
      ++lazy_rounds;
      if (Clock::now() >= deadline) {
        st = Sat_status::unknown;
        break;
      }
      st = solve_with(extra);
    }
    return st;
  };
  // the answer: the boxes' tiles, the connections with their shapes (neighbor boxes of a 1-1 net: in-box metal, no
  // lane), the trees as straight trunk pieces between the ribs' feet and straight ribs. Decoded after every routable
  // solve, so a tier that fails later leaves the last routable answer
  size_t     adjacent_nets = 0;
  const auto decode_answer = [&] {
    out.conns.clear();
    out.local.clear();
    out.hop_tiles.clear();
    adjacent_nets = 0;
    for (size_t i = 0; i < B.size(); ++i) {
      for (const auto& [t, p] : tiles_of[i]) {
        if (cnf.solver.value(p)) {
          boxes.boxes[B[i]].tile = t;
          break;
        }
      }
    }
    std::unordered_map<int, int> hop_at;  // hop id -> the tile chosen
    for (const auto& fh : hops_free) {
      for (const auto& [t, p] : fh.tiles) {
        if (cnf.solver.value(p)) {
          hop_at[fh.id] = t;
          out.hop_tiles.emplace_back(fh.net, fh.def, t);
          break;
        }
      }
    }
    const auto ep_tile = [&](const Endpoint& e) {
      if (e.box >= 0) {
        return boxes.boxes[e.box].tile;
      }
      if (e.hop >= 0) {
        if (const auto h = hop_at.find(e.hop); h != hop_at.end()) {
          return h->second;
        }
      }
      return e.tile;
    };
    for (size_t k = 0; k < conns.size(); ++k) {
      const auto& cn = conns[k];
      const auto  ta = ep_tile(eps[cn.a]);
      const auto  tb = ep_tile(eps[cn.b]);
      if (adjacent[k] && cnf.solver.value(adjacent[k])) {
        out.local.push_back(cn.net);
        ++adjacent_nets;
        continue;
      }
      out.conns.push_back({cn.net, ta, tb, static_cast<bool>(cnf.solver.value(orient[k]))});
    }
    for (const auto& st : steiner) {
      const bool  ht   = cnf.solver.value(st.h_trunk);
      const auto& v    = ht ? st.row_ge : st.col_ge;
      int         axis = ht ? st.y0 : st.x0;
      for (int lit : v) {
        axis += cnf.solver.value(lit) ? 1 : 0;
      }
      std::vector<int> feet;  // along coordinate of each rib's foot on the trunk
      for (int k = 0; k < st.count; ++k) {
        const auto& e    = eps[size_t(st.first + k)];
        const int   t    = ep_tile(e);
        const int   foot = ht ? g.tile(g.col(t), axis) : g.tile(axis, g.row(t));
        if (foot != t) {
          out.conns.push_back({st.net, t, foot, ht});
        }
        feet.push_back(ht ? g.col(t) : g.row(t));
      }
      std::ranges::sort(feet);
      feet.erase(std::unique(feet.begin(), feet.end()), feet.end());
      for (size_t k = 1; k < feet.size(); ++k) {
        const int ta = ht ? g.tile(feet[k - 1], axis) : g.tile(axis, feet[k - 1]);
        const int tb = ht ? g.tile(feet[k], axis) : g.tile(axis, feet[k]);
        out.conns.push_back({st.net, ta, tb, ht});
      }
    }
  };
  // the timing proxy: weighted route length (tiles) of the answer's connections, and the plain length of the tier
  // nets' connections (slack rank under tier_fraction)
  const auto route_cost = [&](double& crit) {
    double c = 0;
    crit     = 0;
    for (const auto& pc : out.conns) {
      const double len  = double(std::abs(g.col(pc.ta) - g.col(pc.tb)) + std::abs(g.row(pc.ta) - g.row(pc.tb)));
      c                += d.nets[pc.net].weight * len;
      crit             += d.nets[pc.net].rank < o.tier_fraction ? len : 0;
    }
    return c;
  };
  Sat_status status = out.skipped ? Sat_status::unknown : settle({});
  out.status        = status;
  if (status == Sat_status::satisfiable) {
    rep.sat_seeded += seeded;  // the answer is the fallback placement
    decode_answer();
  }
  if (!lazy.empty() && !out.skipped) {
    std::fprintf(stderr,
                 "legroute sat region %d lazy: rounds %u, bounds added %zu of %zu, clauses %zu (eager estimate %zu)%s\n",
                 r,
                 lazy_rounds,
                 lazy_added,
                 lazy.size(),
                 cnf.clauses,
                 cl_conn + cap_est,
                 tag.c_str());
  }
  // ---- timing tiers: on a routable answer, each tier's constraints are tried in batches, most critical first, with
  // every box held on its tile by assumptions, so the answer moves only where a constraint needs it: an UNSAT core
  // frees the held boxes in it, or drops the constraints in it when it holds no box; a batch out of rounds or time
  // is dropped. Kept constraints become units for the batches and tiers after
  if (status == Sat_status::satisfiable && !o.sat_relax_rounds && !o.tiers.empty()) {
    seeded                = false;  // the fallback seed gives way to the current answer's tiles
    deadline              = std::min(deadline, Clock::now() + std::chrono::milliseconds(o.tier_ms));
    double crit_len       = 0;
    rep.tier_cost_before += route_cost(crit_len);
    rep.tier_crit_before += crit_len;
    const auto tile_lit   = [&](size_t i, int t) {  // the box's literal on tile t, 0 when it cannot be there
      for (const auto& [q, p] : tiles_of[i]) {
        if (q == t) {
          return p;
        }
      }
      return 0;
    };
    // the box's tile literals within one tile of t: on or beside it (Manhattan), or any neighbor (Chebyshev)
    const auto near_lits = [&](size_t i, int t, bool diagonal) {
      std::vector<int> lits;
      for (const auto& [q, p] : tiles_of[i]) {
        const int dc = std::abs(g.col(q) - g.col(t)), dr = std::abs(g.row(q) - g.row(t));
        if (diagonal ? std::max(dc, dr) <= 1 : dc + dr <= 1) {
          lits.push_back(p);
        }
      }
      return lits;
    };
    const auto implies_any = [&](const std::vector<int>& lits) {  // a fresh literal that forces one of lits
      const int        v = cnf.var();
      std::vector<int> cl{-v};
      cl.insert(cl.end(), lits.begin(), lits.end());
      cnf.add(cl);
      return v;
    };
    std::map<std::pair<size_t, int>, int> near_cache;  // (box index, tile) -> the literal holding it within one tile
    const auto                            near_lit = [&](size_t i, int t) {
      auto [it, fresh] = near_cache.try_emplace({i, t}, 0);
      if (fresh) {
        const auto lits = near_lits(i, t, true);
        it->second      = lits.empty() ? 0 : implies_any(lits);
      }
      return it->second;
    };
    const auto box_rank = [&](int b) {  // the slack rank of the box's most critical routed net
      double rk = 1;
      for (int c : boxes.boxes[b].cells) {
        for (int n : d.nets_of[size_t(c)]) {
          if (d.nets[n].routed) {
            rk = std::min(rk, d.nets[n].rank);
          }
        }
      }
      return rk;
    };
    struct Cand {
      double rank;
      int    lit;
    };
    for (const auto& name : tier_names(o.tiers)) {
      std::vector<Cand> cands;
      if (name == "adjacent") {  // a critical 1-1 net between two boxes: neighbors
        for (size_t k = 0; k < conns.size(); ++k) {
          if (adjacent[k] && d.nets[conns[k].net].rank < o.tier_fraction) {
            cands.push_back({d.nets[conns[k].net].rank, adjacent[k]});
          }
        }
      } else if (name == "near") {  // a critical connection's boxes within one tile of each other
        for (size_t k = 0; k < conns.size(); ++k) {
          if (near[k]) {
            cands.push_back({d.nets[conns[k].net].rank, near[k]});
          }
        }
      } else if (name == "pins") {           // a critical net's box on or beside the net's fixed tile
        std::set<std::pair<int, int>> seen;  // (box, fixed tile)
        for (size_t k = 0; k < conns.size(); ++k) {
          const auto& a = eps[size_t(conns[k].a)];
          const auto& b = eps[size_t(conns[k].b)];
          if ((a.box < 0) == (b.box < 0) || d.nets[conns[k].net].rank >= o.tier_fraction) {
            continue;
          }
          const int bx = a.box >= 0 ? a.box : b.box, pin = a.box < 0 ? a.tile : b.tile;
          if (boxes.boxes[bx].fixed || !seen.emplace(bx, pin).second) {
            continue;
          }
          if (const auto lits = near_lits(size_t(slot.at(bx)), pin, false); !lits.empty()) {
            cands.push_back({d.nets[conns[k].net].rank, implies_any(lits)});
          }
        }
      } else if (name == "home") {  // a critical box within one tile of its placement tile
        for (size_t i = 0; i < B.size(); ++i) {
          const auto&  box = boxes.boxes[B[i]];
          const double rk  = box_rank(B[i]);
          if (box.fixed || rk >= o.tier_fraction) {
            continue;
          }
          if (const auto lits = near_lits(i, g.tile_of(box.cx, box.cy), true); !lits.empty()) {
            cands.push_back({rk, implies_any(lits)});
          }
        }
      }
      if (cands.empty()) {
        continue;
      }
      std::ranges::stable_sort(cands, {}, &Cand::rank);
      size_t   kept_n = 0, batches = 0, solves = 0;
      unsigned rounds_total = 0;
      bool     timed_out    = false;
      for (size_t at = 0; at < cands.size() && !timed_out; at += std::max(1u, o.tier_batch), ++batches) {
        std::vector<int> active;
        for (size_t k = at; k < std::min(cands.size(), at + std::max(1u, o.tier_batch)); ++k) {
          active.push_back(cands[k].lit);
        }
        // boxes held on their current tiles (2), then within one tile of them (1), then free (0)
        std::vector<char> held(B.size(), 2);
        std::vector<int>  hold(B.size(), 0);  // the literal holding each box this round
        Sat_status        st     = Sat_status::unknown;
        unsigned          rounds = 0;
        for (; rounds <= o.tier_rounds && !active.empty(); ++rounds) {
          if (Clock::now() >= deadline) {
            st = Sat_status::unknown;
            break;
          }
          std::vector<int> extra = active;
          for (size_t i = 0; i < B.size(); ++i) {
            const int t = boxes.boxes[B[i]].tile;
            hold[i]     = held[i] == 2 ? tile_lit(i, t) : held[i] == 1 ? near_lit(i, t) : 0;
            if (hold[i]) {
              extra.push_back(hold[i]);
            }
          }
          st = settle(extra);
          ++solves;
          if (st != Sat_status::unsatisfiable) {
            break;
          }
          size_t loosened = 0;  // the core: the held boxes in it get a tile of room, then go free
          for (size_t i = 0; i < B.size(); ++i) {
            if (hold[i] && cnf.solver.failed(hold[i])) {
              --held[i];
              ++loosened;
            }
          }
          if (loosened) {
            continue;
          }
          std::vector<int> rest;  // no box to free: the constraints in the core go
          for (int v : active) {
            if (!cnf.solver.failed(v)) {
              rest.push_back(v);
            }
          }
          if (rest.size() == active.size()) {
            rest.clear();
          }
          active.swap(rest);
        }
        rounds_total += rounds;
        if (st == Sat_status::satisfiable && !active.empty()) {  // the answer first: a clause invalidates the model
          decode_answer();
          for (int v : active) {
            cnf.add({v});
          }
          kept_n += active.size();
        } else {
          timed_out = st == Sat_status::unknown;
        }
      }
      rep.tier_kept     += kept_n;
      rep.tier_dropped  += cands.size() - kept_n;
      rep.tier_timeouts += timed_out;
      rep.tier_solves   += solves;
      std::fprintf(stderr,
                   "legroute sat region %d tier %s: kept %zu of %zu, batches %zu, rounds %u, solves %zu%s%s\n",
                   r,
                   name.c_str(),
                   kept_n,
                   cands.size(),
                   batches,
                   rounds_total,
                   solves,
                   timed_out ? ", out of time" : "",
                   tag.c_str());
    }
    rep.tier_cost_after += route_cost(crit_len);
    rep.tier_crit_after += crit_len;
  }
  rep.lazy_rounds  += lazy_rounds;
  rep.lazy_bounds  += lazy_added;
  rep.lazy_skipped += size_t(std::ranges::count_if(lazy, [](const Lazy& L) { return !L.xs.empty(); }));
  if (status == Sat_status::unsatisfiable && o.sat_relax_rounds) {
    // diagnostic: what would make the region routable. Drop the tile bounds of each UNSAT core until it solves; the
    // answer's demand on the dropped tiles over their tracks is the deficit (the region stays unrouted)
    std::vector<char> dropped(bounds.size(), 0);
    unsigned          round   = 0;
    Sat_status        relaxed = status;
    while (relaxed == Sat_status::unsatisfiable && round < o.sat_relax_rounds) {
      std::vector<size_t> core;
      for (size_t i = 0; i < bounds.size(); ++i) {
        if (!dropped[i] && cnf.solver.failed(-bounds[i].over)) {
          core.push_back(i);
        }
      }
      if (core.empty()) {  // not capacity: the placement itself
        break;
      }
      for (size_t i : core) {
        dropped[i] = 1;
      }
      assume.clear();
      for (size_t i = 0; i < bounds.size(); ++i) {
        if (!dropped[i]) {
          assume.push_back(-bounds[i].over);
        }
      }
      relaxed = cnf.solver.solve(assume, o.sat_conflicts, o.sat_ms);
      ++round;
    }
    size_t        tiles = 0, short_h = 0, short_v = 0, deficit_h = 0, deficit_v = 0, worst = 0, demand = 0, tracks = 0;
    // where the over-capacity tiles are: on the region's edge, holding a fixed endpoint (virtual pin, hop, fixed cell),
    // under a fixed box; and their use split into routes (covering nets) and the occupant's in-box demand
    std::set<int> pin_tiles, fixed_tiles;
    for (const auto& e : eps) {
      if (e.box < 0) {
        pin_tiles.insert(e.tile);
      }
    }
    for (int b : B) {
      if (boxes.boxes[b].fixed) {
        fixed_tiles.insert(boxes.boxes[b].tile);
      }
    }
    std::map<std::pair<int, int>, size_t> routes_at;  // (tile, h) -> covering nets in the answer
    if (relaxed == Sat_status::satisfiable) {
      for (const auto& [key, lit] : cover) {
        routes_at[{std::get<1>(key), std::get<2>(key)}] += cnf.solver.value(lit) ? 1 : 0;
      }
    }
    size_t at_edge = 0, at_pin = 0, at_fixed = 0, over_routes = 0, over_inbox = 0;
    for (size_t i = 0; i < bounds.size(); ++i) {
      if (!dropped[i]) {
        continue;
      }
      ++tiles;
      if (relaxed != Sat_status::satisfiable) {
        continue;
      }
      size_t used = 0;
      for (int x : inputs[{bounds[i].t, bounds[i].h}]) {
        used += cnf.solver.value(x);
      }
      const size_t over = used > size_t(bounds[i].k) ? used - size_t(bounds[i].k) : 0;
      if (over > 0) {
        const int t    = bounds[i].t;
        bool      edge = false;
        for (int q : {g.col(t) > 0 ? t - 1 : -1, g.col(t) + 1 < g.cols ? t + 1 : -1, t - g.cols, t + g.cols}) {
          edge = edge || q < 0 || q >= int(g.size()) || g.region[q] != r;
        }
        at_edge         += edge;
        at_pin          += pin_tiles.contains(t);
        at_fixed        += fixed_tiles.contains(t);
        const size_t rt  = routes_at[{t, bounds[i].h}];
        over_routes     += rt;
        over_inbox      += used > rt ? used - rt : 0;
      }
      (bounds[i].h ? short_h : short_v)     += over > 0;
      (bounds[i].h ? deficit_h : deficit_v) += over;
      worst                                  = std::max(worst, over);
      demand                                += used;
      tracks                                += size_t(bounds[i].k);
    }
    rep.sat_relaxed_tiles  += tiles;
    rep.sat_deficit_tracks += deficit_h + deficit_v;
    std::fprintf(stderr,
                 "legroute sat region %d relax: rounds %u %s, relaxed bounds %zu, over capacity H %zu V %zu tiles, deficit "
                 "H %zu V %zu tracks (worst %zu), relaxed tiles demand %zu of %zu tracks; over tiles at the edge %zu, with a "
                 "fixed endpoint %zu, under a fixed box %zu, their use routes %zu in-box %zu\n",
                 r,
                 round,
                 relaxed == Sat_status::satisfiable     ? "sat"
                 : relaxed == Sat_status::unsatisfiable ? "still unsat"
                                                        : "unknown",
                 tiles,
                 short_h,
                 short_v,
                 deficit_h,
                 deficit_v,
                 worst,
                 demand,
                 tracks,
                 at_edge,
                 at_pin,
                 at_fixed,
                 over_routes,
                 over_inbox);
  }
  rep.sat_variables     += cnf.variables;
  rep.sat_clauses       += cnf.clauses;
  rep.sat_binding_tiles += binding;
  rep.sat_regions       += 1;
  const double secs      = seconds_since(start);
  size_t pass_nets = 0;  // nets with no box in the region: hop to hop (or pin) runs the region only carries
  for (int n : nets_here) {
    pass_nets += std::ranges::none_of(ends[n].per_region.at(r), [](const Endpoint& e) { return e.box >= 0; });
  }
  std::fprintf(stderr,
               "legroute sat region %d: boxes %zu tiles %zu nets %zu (pass %zu) conns %zu vars %zu clauses %zu binding %zu solves "
               "%zu status %s %.2f s%s\n",
               r,
               B.size(),
               T.size(),
               nets_here.size(),
               pass_nets,
               conns.size(),
               cnf.variables,
               cnf.clauses,
               binding,
               rep.sat_solves,
               status == Sat_status::satisfiable     ? "sat"
               : status == Sat_status::unsatisfiable ? "unsat"
                                                     : "unknown",
               secs,
               tag.c_str());
  rep.sat_seconds_max = std::max(rep.sat_seconds_max, secs);
  if (status != Sat_status::satisfiable) {
    ++(out.skipped                           ? rep.sat_skipped_regions
       : status == Sat_status::unsatisfiable ? rep.sat_unsat_regions
                                             : rep.sat_unknown_regions);
    // the region keeps its boxes on fitting tiles, nearest first (not routable by construction)
    const auto tiles = fallback_tiles([&](size_t i) {
      std::vector<int> list;
      for (const auto& [t, _] : tiles_of[i]) {
        list.push_back(t);
      }
      return list;
    });
    for (size_t i = 0; i < B.size(); ++i) {
      if (tiles[i] >= 0) {
        boxes.boxes[B[i]].tile = tiles[i];
      }
    }
    for (int b : B) {
      out.tiles.emplace_back(b, boxes.boxes[b].tile);
    }
    return;
  }
  rep.sat_adjacent_nets += adjacent_nets;
  rep.sat_seconds       += secs;
  for (int b : B) {
    out.tiles.emplace_back(b, boxes.boxes[b].tile);
  }
}

// The routed nets' endpoints per region (boxes, fixed tiles) and the hops that join their regions: one per fixed
// virtual pin, then chains through the regions between those the pins leave unjoined. The hops take their tracks.
struct Crossings {
  std::vector<Net_ends>            ends;
  std::vector<std::pair<int, int>> hops;  // tile pairs across region boundaries
  std::vector<int>                 hop_net;
  size_t                           unjoined   = 0;
  size_t                           over_share = 0;  // hops placed past hop_share on every tile of their boundary
  std::vector<Global_piece>        pieces;          // legroute.global: the runs fixed ahead through pass-through regions
  size_t                           global_nets = 0;
  size_t                           global_unrouted = 0, global_full = 0;  // global_route: nets without a path, edges at the share
  double                           global_seconds = 0;
};
Crossings crossings(const Design& d, const Grid& g, const Boxes& boxes, const Legroute_regions& regions, std::vector<int>& cap_h,
                    std::vector<int>& cap_v, bool diagnose) {
  Crossings  out;
  auto&      ends        = out.ends;
  auto&      hops        = out.hops;
  auto&      hop_net     = out.hop_net;
  std::vector<std::vector<int>> net_hops(d.nets.size());  // per net: its hops (a scan of every hop per net was quadratic)
  const auto add_hop     = [&](int n, int a, int b) {
    net_hops[n].push_back(int(hops.size()));
    hops.emplace_back(a, b);
    hop_net.push_back(n);
  };
  const auto box_of_comp = [&](int comp) { return comp >= 0 && d.cell[comp] >= 0 ? boxes.box_of[d.cell[comp]] : -1; };
  ends.resize(d.nets.size());
  std::unordered_map<size_t, std::vector<std::pair<double, double>>> fixed_pins;
  for (const auto& p : regions.pins) {
    fixed_pins[p.net].emplace_back(p.x, p.y);
  }
  for (size_t n = 0; n < d.nets.size(); ++n) {
    const auto& N = d.nets[n];
    if (!N.routed || N.clock) {
      continue;
    }
    auto& E = ends[n];
    for (size_t k = 0; k < N.pins.size(); ++k) {
      const auto& p   = N.pins[k];
      const int   box = box_of_comp(p.comp);
      Endpoint    e;
      int         region;
      if (box >= 0) {
        e.box  = box;
        region = boxes.boxes[box].region;
      } else {
        double x = 0, y = 0;
        if (p.comp >= 0) {
          const auto& c = d.fp.components[p.comp];
          x             = double(c.place.x) + d.macro[p.comp]->width * double(d.fp.dbu) / 2;
          y             = double(c.place.y) + d.macro[p.comp]->height * double(d.fp.dbu) / 2;
        } else if (p.term >= 0) {
          x = double(d.fp.terminals[p.term].x);
          y = double(d.fp.terminals[p.term].y);
        }
        e.tile = g.tile_of(x, y);
        region = g.region[e.tile];
      }
      if (int(k) == N.driver) {
        E.driver_region = region;
      }
      auto& list = E.per_region[region];
      if (std::ranges::none_of(list, [&](const Endpoint& o) { return o.box == e.box && o.tile == e.tile; })) {
        // the driver first
        if (int(k) == N.driver) {
          list.insert(list.begin(), e);
        } else {
          list.push_back(e);
        }
      }
    }
    if (E.per_region.size() < 2) {
      continue;
    }
    // region crossings: each fixed virtual pin is a hop between the two tiles beside it; the hop's tiles are fixed
    // endpoints in their regions
    std::set<std::pair<int, int>> joined;  // region pairs with a hop
    if (const auto it = fixed_pins.find(N.frame); it != fixed_pins.end()) {
      for (const auto& [x, y] : it->second) {
        const int t0 = g.tile_of(x, y);
        int       bu = -1, bv = -1;
        double    best = INFINITY;
        for (int t : {t0, t0 - 1, t0 + 1, t0 - g.cols, t0 + g.cols}) {
          if (t < 0 || t >= int(g.size())) {
            continue;
          }
          for (int q : {t + 1, t + g.cols}) {
            if (q >= int(g.size()) || (q == t + 1 && g.col(q) == 0) || g.region[q] == g.region[t]
                || !E.per_region.contains(g.region[t]) || !E.per_region.contains(g.region[q])) {
              continue;
            }
            const double mx = (g.cx(g.col(t)) + g.cx(g.col(q))) / 2.0, my = (g.cy(g.row(t)) + g.cy(g.row(q))) / 2.0;
            const double dd = std::abs(mx - x) + std::abs(my - y);
            if (dd < best) {
              best = dd;
              bu   = t;
              bv   = q;
            }
          }
        }
        if (bu < 0) {
          continue;
        }
        for (int t : {bu, bv}) {
          auto& list = E.per_region[g.region[t]];
          if (std::ranges::none_of(list, [&](const Endpoint& o) { return o.tile == t; })) {
            list.push_back({-1, t});
          }
        }
        add_hop(int(n), bu, bv);
        joined.emplace(std::min(g.region[bu], g.region[bv]), std::max(g.region[bu], g.region[bv]));
      }
    }
  }
  if (diagnose) {  // diagnostic: 1:n nets (n distinct sink endpoints) and the boxes they touch
    const std::array<int, 6>     fan{2, 3, 4, 6, 8, 16};
    std::array<size_t, 6>        nets{};
    std::array<std::set<int>, 6> touched;
    for (size_t n = 0; n < d.nets.size(); ++n) {
      const auto& N = d.nets[n];
      if (!N.routed || N.clock) {
        continue;
      }
      std::set<int> bs;
      std::set<int> tiles;
      for (const auto& p : N.pins) {
        const int box = box_of_comp(p.comp);
        if (box >= 0) {
          bs.insert(box);
        } else if (p.term >= 0) {
          tiles.insert(g.tile_of(double(d.fp.terminals[p.term].x), double(d.fp.terminals[p.term].y)));
        } else if (p.comp >= 0) {
          tiles.insert(g.tile_of(double(d.fp.components[p.comp].place.x), double(d.fp.components[p.comp].place.y)));
        }
      }
      const int sinks = int(bs.size() + tiles.size()) - 1;
      for (size_t k = 0; k < fan.size(); ++k) {
        if (sinks >= fan[k]) {
          ++nets[k];
          touched[k].insert(bs.begin(), bs.end());
        }
      }
    }
    for (size_t k = 0; k < fan.size(); ++k) {
      std::fprintf(stderr,
                   "legroute fanout 1:n n>=%d: nets %zu boxes %zu of %zu (%.1f%%)\n",
                   fan[k],
                   nets[k],
                   touched[k].size(),
                   boxes.boxes.size(),
                   100.0 * double(touched[k].size()) / double(std::max<size_t>(1, boxes.boxes.size())));
    }
  }
  // nets whose regions the fixed virtual pins do not join: a hop at a shared boundary, chained through the regions
  // between them along the shortest region path to the driver's region (critical nets first). A hop takes the
  // boundary edge nearest to its net's pins among those with hop room left (half the tracks of the direction).
  std::map<std::pair<int, int>, std::vector<std::pair<int, int>>> edges;  // (r, s), r < s -> tile pairs (in r, in s)
  std::vector<std::set<int>>                                      adj(g.regions);
  for (int t = 0; t < int(g.size()); ++t) {
    for (int q : {g.col(t) + 1 < g.cols ? t + 1 : -1, t + g.cols < int(g.size()) ? t + g.cols : -1}) {
      if (q < 0 || g.region[q] == g.region[t]) {
        continue;
      }
      const int a = g.region[t], b = g.region[q];
      if (a < b) {
        edges[{a, b}].emplace_back(t, q);
      } else {
        edges[{b, a}].emplace_back(q, t);
      }
      adj[a].insert(b);
      adj[b].insert(a);
    }
  }
  std::vector<std::array<int, 2>> hop_use(g.size(), {0, 0});  // per tile: H, V hops
  // hop_spread: the straight runs of pass-through nets (hop to hop inside a region) per tile edge, H right of the
  // tile and V above it, so the next pass-through takes a row or column with room instead of the same one
  std::vector<std::array<int, 2>> through(g.size(), {0, 0});
  std::vector<size_t>             order;
  for (size_t n = 0; n < d.nets.size(); ++n) {
    if (ends[n].per_region.size() >= 2) {
      order.push_back(n);
    }
  }
  std::ranges::stable_sort(order, [&](size_t a, size_t b) { return d.nets[a].crit > d.nets[b].crit; });
  const auto centroid = [&](const std::vector<Endpoint>& list) {
    double x = 0, y = 0;
    for (const auto& e : list) {
      const int t  = e.box >= 0 ? g.tile_of(boxes.boxes[e.box].cx, boxes.boxes[e.box].cy) : e.tile;
      x           += double(g.cx(g.col(t)));
      y           += double(g.cy(g.row(t)));
    }
    return std::pair{x / double(list.size()), y / double(list.size())};
  };
  // legroute.global (global_route): every crossing net's tree over the tile grid ahead of the chains below; a net
  // the router finds no path for keeps the chain
  Global_routes       GR;
  std::vector<size_t> gr_index(d.nets.size(), SIZE_MAX);
  if (d.opt.global_route) {
    std::vector<Global_net> gnets;
    for (const size_t n : order) {
      Global_net  gn;
      const auto& E = ends[n];
      gn.net        = int(n);
      gn.crit       = d.nets[n].crit;
      std::set<int> seen;
      const auto    push = [&](const Endpoint& e) {
        const int t = e.box >= 0 ? g.tile_of(boxes.boxes[e.box].cx, boxes.boxes[e.box].cy) : e.tile;
        if (seen.insert(t).second) {
          gn.endpoints.push_back(t);
        }
      };
      if (const auto dr = E.per_region.find(E.driver_region); dr != E.per_region.end()) {
        for (const auto& e : dr->second) {
          push(e);
        }
      }
      for (const auto& [r, list] : E.per_region) {
        for (const auto& e : list) {
          push(e);
        }
      }
      std::set<int> pin_hops;  // the fixed virtual pins' hop tiles: not the net's own pins
      for (int k : net_hops[n]) {
        gn.fixed_edges.push_back(hops[k]);
        pin_hops.insert(hops[k].first);
        pin_hops.insert(hops[k].second);
      }
      for (const auto& [r, list] : E.per_region) {
        if (std::ranges::any_of(list, [&](const Endpoint& e) { return e.box >= 0 || !pin_hops.contains(e.tile); })) {
          gn.own_regions.push_back(r);
        }
      }
      gr_index[n] = gnets.size();
      gnets.push_back(std::move(gn));
    }
    GR                  = global_route(g, cap_h, cap_v, d.opt.global_share, d.opt.global_own_share, gnets, d.opt.global_margin);
    out.global_unrouted = GR.unrouted;
    out.global_full     = GR.edges_full;
    out.global_seconds  = GR.seconds;
    std::fprintf(stderr,
                 "legroute global: %zu crossing nets routed over the grid, %zu without a path, %zu edges at the share, %.1f s\n",
                 gnets.size() - GR.unrouted,
                 GR.unrouted,
                 GR.edges_full,
                 GR.seconds);
  }
  const auto t_start    = Clock::now();
  double     next_print = 30;
  size_t     done       = 0;
  for (const size_t n : order) {
    if (++done % 256 == 0 && seconds_since(t_start) >= next_print) {  // a large design: where the crossings are
      std::fprintf(stderr,
                   "legroute crossings: %zu of %zu crossing nets, %zu hops, %zu traced, %.0f s\n",
                   done,
                   order.size(),
                   hops.size(),
                   out.global_nets,
                   seconds_since(t_start));
      next_print = seconds_since(t_start) + 30;
    }
    auto&         E = ends[n];
    std::set<int> hop_tiles;  // the net's endpoints that are hops, not its own pins or boxes
    for (int k : net_hops[n]) {
      hop_tiles.insert(hops[k].first);
      hop_tiles.insert(hops[k].second);
    }
    std::map<int, int> comp;  // region -> component (union-find by relabeling, few regions per net)
    for (const auto& [r, _] : E.per_region) {
      comp[r] = r;
    }
    const auto find = [&](int r) {
      while (comp.at(r) != r) {
        r = comp.at(r);
      }
      return r;
    };
    const auto unite = [&](int a, int b) {
      comp.try_emplace(a, a);
      comp.try_emplace(b, b);
      a = find(a);
      b = find(b);
      if (a != b) {
        comp[a] = b;
      }
    };
    for (size_t k = 0; k < hops.size(); ++k) {  // the fixed pins already joined
      if (hop_net[k] == int(n)) {
        unite(g.region[hops[k].first], g.region[hops[k].second]);
      }
    }
    const int  root_region = E.driver_region >= 0 ? E.driver_region : E.per_region.begin()->first;
    // legroute.global: a critical net joins a region with an L over the tile grid from the nearest endpoint of the
    // joined part (its pins, boxes or earlier hops) to the region's nearest own endpoint. Every boundary the L
    // crosses is a hop; a region the L only passes through keeps the run as a fixed piece. The orientation with
    // the lighter edges wins; an L over a blocked edge (a macro) is not taken, and with both blocked the hop chain
    // below joins the region instead
    const bool global      = d.opt.global_fraction > 0 && d.nets[n].rank < d.opt.global_fraction;
    bool       traced      = false;
    if (const size_t gi = gr_index[n]; gi != SIZE_MAX && GR.trees[gi].routed) {
      // the routed tree: its boundary crossings are the net's hops, its runs through the regions holding none of
      // the net's own endpoints are fixed pieces (an L each), the regions it touches are joined
      const auto&   tedges = GR.trees[gi].edges;
      std::set<int> own;  // regions with an endpoint of the net itself (a box, a pin, a fixed cell)
      for (const auto& [r, list] : E.per_region) {
        if (std::ranges::any_of(list, [&](const Endpoint& e) { return e.box >= 0 || !hop_tiles.contains(e.tile); })) {
          own.insert(r);
        }
      }
      std::set<std::pair<int, int>> had;  // the net's hops so far (its fixed virtual pins)
      for (int k : net_hops[n]) {
        had.emplace(std::min(hops[k].first, hops[k].second), std::max(hops[k].first, hops[k].second));
      }
      std::map<int, std::vector<std::pair<int, int>>> interior;  // per region: the tree's edges inside it
      for (const auto& [a, b] : tedges) {
        const int dir = g.row(a) == g.row(b) ? 0 : 1;
        if (g.region[a] == g.region[b]) {
          ++through[a][dir];
          interior[g.region[a]].emplace_back(a, b);
          continue;
        }
        unite(g.region[a], g.region[b]);
        if (had.contains({a, b})) {
          continue;
        }
        ++hop_use[a][dir];
        ++hop_use[b][dir];
        if (double(hop_use[a][dir]) > d.opt.hop_share * (dir ? cap_v : cap_h)[a]
            || double(hop_use[b][dir]) > d.opt.hop_share * (dir ? cap_v : cap_h)[b]) {
          ++out.over_share;
        }
        add_hop(int(n), a, b);
        for (int x : {a, b}) {
          auto& list = E.per_region[g.region[x]];
          if (std::ranges::none_of(list, [&](const Endpoint& o) { return o.tile == x; })) {
            list.push_back({-1, x});
          }
          hop_tiles.insert(x);
        }
      }
      // a path of the tree inside a pass-through region, split into straight runs; two runs make an L piece
      const auto emit = [&](int r, const std::vector<int>& path) {
        std::vector<int> corner{0};
        for (size_t i = 1; i + 1 < path.size(); ++i) {
          if ((g.row(path[i - 1]) == g.row(path[i])) != (g.row(path[i]) == g.row(path[i + 1]))) {
            corner.push_back(int(i));
          }
        }
        corner.push_back(int(path.size()) - 1);
        for (size_t k = 0; k + 1 < corner.size(); k += 2) {
          const size_t e = std::min(k + 2, corner.size() - 1);
          out.pieces.push_back({int(n), r, path[corner[k]], path[corner[e]], g.row(path[corner[k]]) == g.row(path[corner[k] + 1])});
        }
      };
      for (const auto& [r, ie] : interior) {
        if (own.contains(r)) {
          continue;  // the region's model routes the net among its endpoints itself
        }
        std::map<int, std::vector<int>> adj_t;
        for (const auto& [a, b] : ie) {
          adj_t[a].push_back(b);
          adj_t[b].push_back(a);
        }
        std::set<int> node;  // piece ends: hop tiles and branch tiles (a tree: every path ends at one)
        for (const auto& [t, nb] : adj_t) {
          if (nb.size() != 2 || hop_tiles.contains(t)) {
            node.insert(t);
          }
        }
        std::set<std::pair<int, int>> walked;
        for (int s0 : node) {
          for (int q : adj_t.at(s0)) {
            if (!walked.emplace(std::min(s0, q), std::max(s0, q)).second) {
              continue;
            }
            std::vector<int> path{s0, q};
            for (int prev = s0, cur = q; !node.contains(cur);) {
              const auto& nb  = adj_t.at(cur);
              const int   nxt = nb[0] == prev ? nb[1] : nb[0];
              walked.emplace(std::min(cur, nxt), std::max(cur, nxt));
              path.push_back(nxt);
              prev = cur;
              cur  = nxt;
            }
            emit(r, path);
          }
        }
      }
      traced = true;
    }
    const auto tile_of
        = [&](const Endpoint& e) { return e.box >= 0 ? g.tile_of(boxes.boxes[e.box].cx, boxes.boxes[e.box].cy) : e.tile; };
    const auto real_endpoint = [&](const Endpoint& e) { return e.box >= 0 || !hop_tiles.contains(e.tile); };
    const auto join_global   = [&](int r) {
      int    ta = -1, tb = -1;  // the nearest pair: an endpoint of the joined part, r's own endpoint
      double best = INFINITY;
      for (const auto& er : E.per_region.at(r)) {
        const int t1 = tile_of(er);
        if (!real_endpoint(er) || g.region[t1] != r) {  // a box's placement tile can fall outside its region
          continue;
        }
        for (const auto& [q, list] : E.per_region) {
          if (!comp.contains(q) || find(q) != find(root_region)) {
            continue;
          }
          for (const auto& eq : list) {
            const int t0 = tile_of(eq);
            if (g.region[t0] != q) {
              continue;
            }
            const double dd = std::abs(g.col(t0) - g.col(t1)) + std::abs(g.row(t0) - g.row(t1));
            if (dd < best) {
              best = dd;
              ta   = t0;
              tb   = t1;
            }
          }
        }
      }
      if (ta < 0 || ta == tb) {
        return false;
      }
      struct Walk {
        std::vector<int> tiles;
        double           load    = 0;
        bool             blocked = false;
      };
      const auto walk = [&](bool h_first) {  // the L's tiles from ta to tb, the worst edge load on the way
        Walk       w;
        w.tiles.reserve(size_t(std::abs(g.col(ta) - g.col(tb)) + std::abs(g.row(ta) - g.row(tb))) + 1);
        const auto step = [&](int t) {
          if (!w.tiles.empty()) {
            const int p = w.tiles.back(), dir = g.row(p) == g.row(t) ? 0 : 1, lo = std::min(p, t);
            const int cap = std::min((dir ? cap_v : cap_h)[p], (dir ? cap_v : cap_h)[t]);
            if (cap <= 0 || g.blocked[t]) {
              w.blocked = true;
            }
            const int used = through[lo][dir] + (g.region[p] != g.region[t] ? std::max(hop_use[p][dir], hop_use[t][dir]) : 0);
            w.load         = std::max(w.load, double(used) / double(std::max(1, cap)));
          }
          w.tiles.push_back(t);
        };
        const int hr = h_first ? g.row(ta) : g.row(tb), vc = h_first ? g.col(tb) : g.col(ta);
        const int dc = g.col(tb) > g.col(ta) ? 1 : -1, dr = g.row(tb) > g.row(ta) ? 1 : -1;
        step(ta);
        if (h_first) {
          for (int i = g.col(ta); i != g.col(tb); i += dc) {
            step(g.tile(i + dc, hr));
          }
          for (int j = g.row(ta); j != g.row(tb); j += dr) {
            step(g.tile(vc, j + dr));
          }
        } else {
          for (int j = g.row(ta); j != g.row(tb); j += dr) {
            step(g.tile(vc, j + dr));
          }
          for (int i = g.col(ta); i != g.col(tb); i += dc) {
            step(g.tile(i + dc, hr));
          }
        }
        return w;
      };
      const Walk wh = walk(true), wv = walk(false);
      if (wh.blocked && wv.blocked) {
        return false;
      }
      const bool  h_first = !wh.blocked && (wv.blocked || wh.load <= wv.load);
      const Walk& W       = h_first ? wh : wv;
      int         entry   = -1;  // the hop tile the trace entered the current region by (the start region: none)
      for (size_t i = 0; i + 1 < W.tiles.size(); ++i) {
        const int p = W.tiles[i], t = W.tiles[i + 1], dir = g.row(p) == g.row(t) ? 0 : 1;
        ++through[std::min(p, t)][dir];
        if (g.region[p] == g.region[t]) {
          continue;
        }
        const bool pass = entry >= 0 && std::ranges::none_of(E.per_region[g.region[p]], real_endpoint);
        if (pass) {  // the region left held none of the net's pins: its run is a fixed piece
          out.pieces.push_back({int(n), g.region[p], entry, p, h_first});
        }
        ++hop_use[p][dir];
        ++hop_use[t][dir];
        if (double(hop_use[p][dir]) > d.opt.hop_share * (dir ? cap_v : cap_h)[p]
            || double(hop_use[t][dir]) > d.opt.hop_share * (dir ? cap_v : cap_h)[t]) {
          ++out.over_share;
        }
        add_hop(int(n), p, t);
        hop_tiles.insert(p);
        hop_tiles.insert(t);
        for (int x : {p, t}) {
          auto& list = E.per_region[g.region[x]];
          if (std::ranges::none_of(list, [&](const Endpoint& o) { return o.tile == x; })) {
            list.push_back({-1, x});
          }
        }
        unite(g.region[p], g.region[t]);
        entry = t;
      }
      unite(g.region[ta], r);
      return find(r) == find(root_region);
    };
    size_t rounds = 0;
    for (bool progress = true; progress;) {
      progress = false;
      // every round joins a region or ends the loop; a loop still running past the regions it could join is a bug
      // in the join, reported and cut rather than spun on
      if (++rounds > 2 * E.per_region.size() + 8) {
        ++out.unjoined;
        std::fprintf(stderr,
                     "legroute crossings: net %zu join stalled after %zu rounds (%zu regions, %zu hops)\n",
                     n,
                     rounds,
                     E.per_region.size(),
                     net_hops[n].size());
        break;
      }
      std::vector<int> keys;  // the regions at the round's start (the joins add pass-through regions meanwhile)
      keys.reserve(E.per_region.size());
      for (const auto& [r, _] : E.per_region) {
        keys.push_back(r);
      }
      for (const int r : keys) {
        if (find(r) == find(root_region)) {
          continue;
        }
        if (global && join_global(r)) {
          traced   = true;
          progress = true;
          continue;
        }
        // shortest region path from r's component to the root's component
        std::map<int, int> from;
        std::vector<int>   frontier;
        for (const auto& [q, _2] : comp) {
          if (find(q) == find(r)) {
            from[q] = -1;
            frontier.push_back(q);
          }
        }
        // the shortest join, and among the shortest the region holding the net's own endpoints nearest to r's
        // (a region the net only passes through, joined by earlier hops, would make r's part a detour through it)
        const auto real_in = [&](int q) {
          const auto it = E.per_region.find(q);
          return it != E.per_region.end()
                 && std::ranges::any_of(it->second, [&](const Endpoint& e) { return e.box >= 0 || !hop_tiles.contains(e.tile); });
        };
        int    hit         = -1;
        size_t level_begin = 0;
        while (hit < 0 && level_begin < frontier.size()) {
          const size_t level_end = frontier.size();
          double       best_hit  = INFINITY;
          for (size_t i = level_begin; i < level_end; ++i) {
            for (int q : adj[frontier[i]]) {
              if (from.contains(q)) {
                continue;
              }
              if (comp.contains(q) && find(q) == find(root_region)) {
                double score = real_in(q) ? 0 : 1e30;
                if (real_in(q)) {
                  const auto cr = centroid(E.per_region.at(r)), cq = centroid(E.per_region.at(q));
                  score = std::abs(cr.first - cq.first) + std::abs(cr.second - cq.second);
                }
                if (score < best_hit) {
                  best_hit = score;
                  hit      = q;
                  from[q]  = frontier[i];
                }
                continue;
              }
              from[q] = frontier[i];
              frontier.push_back(q);
            }
          }
          level_begin = level_end;
        }
        if (hit < 0) {
          ++out.unjoined;
          comp[find(r)] = find(root_region);  // unreachable: give up on this part
          continue;
        }
        std::vector<int> path{hit};
        while (from.at(path.back()) >= 0) {
          path.push_back(from.at(path.back()));
        }
        for (size_t i = 0; i + 1 < path.size(); ++i) {
          const int  a = path[i + 1], b = path[i];  // a toward r, b toward the root
          const auto key  = std::pair{std::min(a, b), std::max(a, b)};
          const auto ta   = E.per_region.contains(a) ? centroid(E.per_region.at(a)) : std::pair{0.0, 0.0};
          const auto tb   = E.per_region.contains(b) ? centroid(E.per_region.at(b)) : ta;
          const auto aim  = E.per_region.contains(a) ? std::pair{(ta.first + tb.first) / 2, (ta.second + tb.second) / 2} : tb;
          // b is a pass-through region when the net's endpoints there are hop tiles only (the hop into it was chosen
          // a step earlier): the run from that hop to this one is straight or an L with no other choice, so it costs
          // the rows and columns other pass-throughs already took (hop_spread tile widths per full load)
          int        prev = -1;
          if (d.opt.hop_spread > 0 && E.per_region.contains(b)) {
            const auto& lb = E.per_region.at(b);
            if (std::ranges::all_of(lb, [](const Endpoint& e) { return e.box < 0; })) {
              prev = lb.back().tile;
            }
          }
          // the L from prev to v, H first (H on prev's row, V on v's column) or V first (V on prev's column, H on
          // v's row): the edges it crosses
          const auto run_edges = [&](int v, bool h_first, const auto& visit) {
            const int hr = h_first ? g.row(prev) : g.row(v), vc = h_first ? g.col(v) : g.col(prev);
            for (int i = std::min(g.col(prev), g.col(v)); i < std::max(g.col(prev), g.col(v)); ++i) {
              visit(g.tile(i, hr), 0);
            }
            for (int j = std::min(g.row(prev), g.row(v)); j < std::max(g.row(prev), g.row(v)); ++j) {
              visit(g.tile(vc, j), 1);
            }
          };
          // the fullest edge on that L, sampled every few tiles on a long run (a large die's L is thousands of
          // tiles; the chooser pays this per candidate)
          const auto run_load = [&](int v, bool h_first) {
            const int len    = std::abs(g.col(prev) - g.col(v)) + std::abs(g.row(prev) - g.row(v));
            const int stride = std::max(1, len / 32);
            double    load   = 0;
            int       k      = 0;
            run_edges(v, h_first, [&](int t, int dir) {
              if (k++ % stride == 0) {
                load = std::max(load, double(through[t][dir]) / std::max(1.0, double((dir ? cap_v : cap_h)[t])));
              }
            });
            return load;
          };
          int                                       bu = -1, bv = -1;
          double                                    best = INFINITY;
          // the candidates with hop room, nearest to the aim first; the spread term (an L walk each) is paid only
          // for the nearest few, so a long boundary of a large design costs the same as a short one
          std::vector<std::tuple<double, int, int>> cands;
          for (auto [u, v] : edges.at(key)) {
            if (g.region[u] != a) {
              std::swap(u, v);
            }
            const int h = g.row(u) == g.row(v) ? 0 : 1;
            if (double(hop_use[u][h]) >= d.opt.hop_share * (h ? cap_v : cap_h)[u]
                || double(hop_use[v][h]) >= d.opt.hop_share * (h ? cap_v : cap_h)[v]) {
              continue;
            }
            const double mx = (g.cx(g.col(u)) + g.cx(g.col(v))) / 2.0, my = (g.cy(g.row(u)) + g.cy(g.row(v))) / 2.0;
            cands.emplace_back(std::abs(mx - aim.first) + std::abs(my - aim.second), u, v);
          }
          constexpr size_t nearest = 32;
          if (cands.size() > nearest) {
            std::ranges::partial_sort(cands, cands.begin() + nearest);
            cands.resize(nearest);
          }
          for (const auto& [dist, u, v] : cands) {
            double cost = dist;
            if (prev >= 0) {
              cost += d.opt.hop_spread * double(g.tile_w) * std::min(run_load(v, true), run_load(v, false));
            }
            if (cost < best) {
              best = cost;
              bu   = u;
              bv   = v;
            }
          }
          if (bu < 0) {  // every tile of the boundary is at the share: the least loaded one, nearest on a tie
            double least = INFINITY;
            for (auto [u, v] : edges.at(key)) {
              if (g.region[u] != a) {
                std::swap(u, v);
              }
              const int    h    = g.row(u) == g.row(v) ? 0 : 1;
              const double load = std::max(double(hop_use[u][h]) / std::max(1, (h ? cap_v : cap_h)[u]),
                                           double(hop_use[v][h]) / std::max(1, (h ? cap_v : cap_h)[v]));
              const double mx = (g.cx(g.col(u)) + g.cx(g.col(v))) / 2.0, my = (g.cy(g.row(u)) + g.cy(g.row(v))) / 2.0;
              const double cost = load + 1e-6 * (std::abs(mx - aim.first) + std::abs(my - aim.second)) / double(g.tile_w);
              if (cost < least) {
                least = cost;
                bu    = u;
                bv    = v;
              }
            }
            if (bu >= 0) {
              ++out.over_share;
            }
          }
          if (bu >= 0 && prev >= 0) {
            run_edges(bv, run_load(bv, true) <= run_load(bv, false), [&](int t, int dir) { ++through[t][dir]; });
          }
          if (bu < 0) {
            ++out.unjoined;
            break;
          }
          const int h = g.row(bu) == g.row(bv) ? 0 : 1;
          ++hop_use[bu][h];
          ++hop_use[bv][h];
          for (int t : {bu, bv}) {
            auto& list = E.per_region[g.region[t]];
            if (std::ranges::none_of(list, [&](const Endpoint& o) { return o.tile == t; })) {
              list.push_back({-1, t});
            }
          }
          add_hop(int(n), bu, bv);
          hop_tiles.insert(bu);
          hop_tiles.insert(bv);
          unite(a, b);
          progress = true;
        }
      }
    }
    out.global_nets += traced;
  }
  for (size_t k = 0; k < hops.size(); ++k) {
    for (int t : {hops[k].first, hops[k].second}) {
      (g.row(hops[k].first) == g.row(hops[k].second) ? cap_h : cap_v)[t] -= 1;
    }
  }
  return out;
}

// ---------------------------------------------------------------- spill selection: min cuts inside a failing region
// The boxes of a failing region r to push out, and where: the region and its receiving neighbours as one graph, the
// region's movable boxes its nodes, each neighbour contracted to a terminal T_j, one terminal S for what stays. A net
// is a star of edges from its first box to its other boxes (weight 100 x the net's weight over its endpoint count
// less one: a net costs about the same however many boxes it joins); a net reaching neighbour j also pulls its boxes
// to T_j, a net with a fixed endpoint in r or an endpoint past the neighbours pulls them to S. Each neighbour's side
// is its isolating s-t min cut (VieCut's push-relabel: T_j against S and the other terminals), so the netlist decides
// which boxes leave and to which neighbour, connected clusters together. A uniform pull a of every box to the
// neighbours is bisected to the smallest value that moves at least `want` boxes; a box two cuts claim goes to the
// neighbour its nets reach most (the roomier one when tied), and a neighbour takes at most its room (the boxes its
// nets reach most first). Returns (box, region) pairs.
struct Spill_target {
  int region = -1, room = 0;
};
std::vector<std::pair<int, int>> spill_cut(const Design& d, const Boxes& boxes, const std::vector<Net_ends>& ends,
                                           const std::vector<int>& cand, int r, const std::vector<Spill_target>& to, size_t want) {
  const size_t B = cand.size(), M = to.size();
  if (B == 0 || M == 0 || want == 0) {
    return {};
  }
  want = std::min(want, B);
  std::vector<int>      node_of(boxes.boxes.size(), -1);
  std::map<int, size_t> jof;  // region -> terminal
  for (size_t i = 0; i < B; ++i) {
    node_of[size_t(cand[i])] = int(i);
  }
  for (size_t j = 0; j < M; ++j) {
    jof[to[j].region] = j;
  }
  std::vector<uint64_t>                   to_s(B, 0);
  std::vector<std::vector<uint64_t>>      to_t(M, std::vector<uint64_t>(B, 0));
  std::map<std::pair<int, int>, uint64_t> bb;  // box-box star edges (lower node first)
  for (size_t n = 0; n < d.nets.size(); ++n) {
    const auto& per = ends[n].per_region;
    const auto  it  = per.find(r);
    if (it == per.end()) {
      continue;
    }
    std::vector<int> mine;
    bool             anchored = false;
    for (const auto& e : it->second) {
      if (e.box >= 0 && node_of[size_t(e.box)] >= 0) {
        mine.push_back(node_of[size_t(e.box)]);
      } else {
        anchored = true;  // a fixed tile, or a box of r that stays
      }
    }
    if (mine.empty()) {
      continue;
    }
    std::ranges::sort(mine);
    mine.erase(std::unique(mine.begin(), mine.end()), mine.end());
    std::vector<size_t> reach;  // the receiving neighbours the net reaches
    for (const auto& [rr, _] : per) {
      if (rr == r) {
        continue;
      }
      if (const auto j = jof.find(rr); j != jof.end()) {
        reach.push_back(j->second);
      } else {
        anchored = true;
      }
    }
    const size_t   k = mine.size() + reach.size() + (anchored ? 1 : 0);
    const uint64_t w = std::max<uint64_t>(1, 100 * uint64_t(d.nets[n].weight) / std::max<size_t>(1, k - 1));
    for (int i : mine) {
      if (anchored) {
        to_s[size_t(i)] += w;
      }
      for (size_t j : reach) {
        to_t[j][size_t(i)] += w;
      }
    }
    for (size_t q = 1; q < mine.size(); ++q) {
      bb[{mine[0], mine[q]}] += w;
    }
  }
  std::vector<uint64_t> hold(B, 0);  // the most a box can be held back: a pull above it leaves for sure
  for (size_t i = 0; i < B; ++i) {
    hold[i] = to_s[i];
    for (size_t j = 0; j < M; ++j) {
      hold[i] += to_t[j][i];
    }
  }
  for (const auto& [e, w] : bb) {
    hold[size_t(e.first)] += w;
    hold[size_t(e.second)] += w;
  }
  const NodeID U = NodeID(B), T = NodeID(B + 1);
  const auto   side = [&](size_t j, uint64_t a) {  // the boxes on neighbour j's side of its isolating cut at pull a
    auto G = std::make_shared<mutable_graph>();
    G->start_construction(NodeID(B + 2));
    for (size_t i = 0; i < B + 2; ++i) {
      G->new_node();
    }
    for (const auto& [e, w] : bb) {
      G->new_edge(NodeID(e.first), NodeID(e.second), w);
    }
    for (size_t i = 0; i < B; ++i) {
      uint64_t u = to_s[i];
      for (size_t k = 0; k < M; ++k) {
        if (k != j) {
          u += to_t[k][i];
        }
      }
      if (u > 0) {
        G->new_edge(NodeID(i), U, u);
      }
      if (to_t[j][i] + a > 0) {
        G->new_edge(NodeID(i), T, to_t[j][i] + a);
      }
    }
    G->finish_construction();
    push_relabel<>    pr;
    const auto        stay = pr.solve_max_flow_min_cut(G, {U, T}, 0, true).second;
    std::vector<char> on(B, 1);
    for (NodeID v : stay) {
      if (v < B) {
        on[v] = 0;
      }
    }
    return on;
  };
  const auto claim = [&](uint64_t a) {  // per box: the neighbours whose cuts claim it
    std::vector<std::vector<size_t>> who(B);
    for (size_t j = 0; j < M; ++j) {
      const auto on = side(j, a);
      for (size_t i = 0; i < B; ++i) {
        if (on[i]) {
          who[i].push_back(j);
        }
      }
    }
    return who;
  };
  // the assignment at pull a: a box two cuts claim goes to the neighbour its nets reach most (the roomier one when
  // tied), each neighbour keeps the boxes its nets reach most, at most its room
  const auto assign = [&](uint64_t a) {
    const auto                                         who = claim(a);
    std::vector<std::vector<std::pair<uint64_t, int>>> take(M);  // per neighbour: (pull, box)
    for (size_t i = 0; i < B; ++i) {
      if (who[i].empty()) {
        continue;
      }
      size_t pick = who[i][0];
      for (size_t j : who[i]) {
        if (to_t[j][i] > to_t[pick][i] || (to_t[j][i] == to_t[pick][i] && to[j].room > to[pick].room)) {
          pick = j;
        }
      }
      take[pick].emplace_back(to_t[pick][i], cand[i]);
    }
    for (size_t j = 0; j < M; ++j) {
      std::ranges::sort(take[j], std::greater<>());
      take[j].resize(std::min(take[j].size(), size_t(std::max(0, to[j].room))));
    }
    return take;
  };
  const auto moved = [&](const std::vector<std::vector<std::pair<uint64_t, int>>>& take) {
    size_t n = 0;
    for (const auto& t : take) {
      n += t.size();
    }
    return n;
  };
  size_t room_sum = 0;
  for (const auto& t : to) {
    room_sum += size_t(std::max(0, t.room));
  }
  want = std::min(want, room_sum);
  uint64_t lo = 0, hi = 1;
  for (size_t i = 0; i < B; ++i) {
    hi = std::max(hi, hold[i] + 1);
  }
  auto best = assign(0);
  if (moved(best) < want) {  // the smallest uniform pull that moves enough: a bisection (the claims grow with it)
    auto top = assign(hi);
    while (hi - lo > 1) {
      const uint64_t mid = lo + (hi - lo) / 2;
      auto           c   = assign(mid);
      if (moved(c) >= want) {
        hi  = mid;
        top = std::move(c);
      } else {
        lo = mid;
      }
    }
    best = std::move(top);
  }
  std::vector<std::pair<int, int>> out;
  for (size_t j = 0; j < M; ++j) {
    for (const auto& [_, b] : best[j]) {
      out.emplace_back(b, to[j].region);
    }
  }
  return out;
}

// ---------------------------------------------------------------- spill by boundary shift
// The pull of region r toward each region of `to`: the weight of the nets with an endpoint in r (a box or a fixed
// pin) and one in that region.
std::vector<uint64_t> spill_pulls(const Design& d, const std::vector<Net_ends>& ends, int r, const std::vector<int>& to) {
  std::vector<uint64_t> pull(to.size(), 0);
  std::map<int, size_t> jof;
  for (size_t j = 0; j < to.size(); ++j) {
    jof[to[j]] = j;
  }
  for (size_t n = 0; n < d.nets.size(); ++n) {
    const auto& per = ends[n].per_region;
    if (!per.contains(r)) {
      continue;
    }
    for (const auto& [rr, _] : per) {
      if (const auto j = jof.find(rr); j != jof.end()) {
        pull[j->second] += 100 * uint64_t(d.nets[n].weight);
      }
    }
  }
  return pull;
}
// The four tile neighbours of t.
std::vector<int> tile_neighbours(const Grid& g, int t) {
  std::vector<int> out;
  if (g.col(t) > 0) {
    out.push_back(t - 1);
  }
  if (g.col(t) + 1 < g.cols) {
    out.push_back(t + 1);
  }
  if (g.row(t) > 0) {
    out.push_back(t - g.cols);
  }
  if (g.row(t) + 1 < g.rows) {
    out.push_back(t + g.cols);
  }
  return out;
}
// The tile strip of region r next to region s, k tiles deep (breadth-first from s through r's tiles).
std::vector<int> boundary_strip(const Grid& g, int r, int s, int k) {
  std::vector<int> depth(g.size(), 0), frontier;
  for (int t = 0; t < int(g.size()); ++t) {
    if (g.region[t] == r && std::ranges::any_of(tile_neighbours(g, t), [&](int q) { return g.region[q] == s; })) {
      depth[size_t(t)] = 1;
      frontier.push_back(t);
    }
  }
  for (int dpt = 1; dpt < k && !frontier.empty(); ++dpt) {
    std::vector<int> next;
    for (int t : frontier) {
      for (int q : tile_neighbours(g, t)) {
        if (g.region[q] == r && depth[size_t(q)] == 0) {
          depth[size_t(q)] = dpt + 1;
          next.push_back(q);
        }
      }
    }
    frontier = std::move(next);
  }
  std::vector<int> strip;
  for (int t = 0; t < int(g.size()); ++t) {
    if (depth[size_t(t)] > 0) {
      strip.push_back(t);
    }
  }
  return strip;
}
// Whether region r's tiles outside `strip` stay one connected piece (and there is one).
bool connected_without(const Grid& g, int r, const std::vector<int>& strip) {
  const std::set<int> gone(strip.begin(), strip.end());
  std::vector<char>   seen(g.size(), 0);
  int                 start = -1, left = 0;
  for (int t = 0; t < int(g.size()); ++t) {
    if (g.region[t] == r && !gone.contains(t)) {
      ++left;
      if (start < 0) {
        start = t;
      }
    }
  }
  if (left == 0) {
    return false;
  }
  std::vector<int> stack{start};
  seen[size_t(start)] = 1;
  int reached         = 0;
  while (!stack.empty()) {
    const int t = stack.back();
    stack.pop_back();
    ++reached;
    for (int q : tile_neighbours(g, t)) {
      if (g.region[q] == r && !gone.contains(q) && !seen[size_t(q)]) {
        seen[size_t(q)] = 1;
        stack.push_back(q);
      }
    }
  }
  return reached == left;
}

// ---------------------------------------------------------------- alternative placements of a failing region
// A candidate: new homes (and windows) for the region's movable boxes.
struct Alt_home {
  int    box;
  double cx, cy;
  int    window;  // -1: the default
};
struct Alternative {
  std::string           kind;
  std::vector<Alt_home> homes;
};
// noise: every movable box's home moves by a uniform random offset of up to `reach` tiles, inside the region's bounds.
Alternative alt_noise(const Grid& g, const Boxes& boxes, const std::vector<int>& movable, const std::vector<int>& tiles, int reach,
                      unsigned seed) {
  Alternative out{"noise", {}};
  int         c0 = g.cols, c1 = -1, r0 = g.rows, r1 = -1;
  for (int t : tiles) {
    c0 = std::min(c0, g.col(t));
    c1 = std::max(c1, g.col(t));
    r0 = std::min(r0, g.row(t));
    r1 = std::max(r1, g.row(t));
  }
  std::mt19937                           rng(seed);
  std::uniform_real_distribution<double> u(-double(reach), double(reach));
  for (int b : movable) {
    const auto&  bx = boxes.boxes[size_t(b)];
    const double cx = std::clamp(bx.cx + u(rng) * double(g.tile_w), double(g.cx(c0)), double(g.cx(c1)));
    const double cy = std::clamp(bx.cy + u(rng) * double(g.tile_h), double(g.cy(r0)), double(g.cy(r1)));
    out.homes.push_back({b, cx, cy, -1});
  }
  return out;
}
// anchor: a few boxes of the region's densest 4x4 patch (non-critical first) move to the centre of its sparsest patch
// and keep a one-tile window there.
Alternative alt_anchor(const Grid& g, const Boxes& boxes, const std::vector<int>& movable, const std::vector<int>& tiles) {
  Alternative out{"anchor", {}};
  int         c0 = g.cols, c1 = -1, r0 = g.rows, r1 = -1;
  for (int t : tiles) {
    c0 = std::min(c0, g.col(t));
    c1 = std::max(c1, g.col(t));
    r0 = std::min(r0, g.row(t));
    r1 = std::max(r1, g.row(t));
  }
  const int  nb     = 4;
  const auto bin_of = [&](double cx, double cy) {
    const int bx = std::clamp(int((cx - g.cx(c0)) / std::max(1.0, double(g.cx(c1) - g.cx(c0))) * nb), 0, nb - 1);
    const int by = std::clamp(int((cy - g.cy(r0)) / std::max(1.0, double(g.cy(r1) - g.cy(r0))) * nb), 0, nb - 1);
    return by * nb + bx;
  };
  std::vector<int> count(size_t(nb * nb), 0), free_tiles(size_t(nb * nb), 0);
  for (int b : movable) {
    ++count[size_t(bin_of(boxes.boxes[size_t(b)].cx, boxes.boxes[size_t(b)].cy))];
  }
  for (int t : tiles) {
    free_tiles[size_t(bin_of(double(g.cx(g.col(t))), double(g.cy(g.row(t)))))] += !g.blocked[t];
  }
  int densest = 0, sparsest = 0;
  for (int k = 0; k < nb * nb; ++k) {  // fill = boxes over free tiles
    const auto fill = [&](int q) { return free_tiles[size_t(q)] ? double(count[size_t(q)]) / double(free_tiles[size_t(q)]) : 2.0; };
    if (fill(k) > fill(densest)) {
      densest = k;
    }
    if (free_tiles[size_t(k)] > 0 && fill(k) < fill(sparsest)) {
      sparsest = k;
    }
  }
  if (densest == sparsest || count[size_t(densest)] == 0) {
    return out;
  }
  const double tx = double(g.cx(c0)) + (sparsest % nb + .5) * double(g.cx(c1) - g.cx(c0)) / nb;
  const double ty = double(g.cy(r0)) + (sparsest / nb + .5) * double(g.cy(r1) - g.cy(r0)) / nb;
  const size_t want = std::max<size_t>(3, (movable.size() + 19) / 20);
  std::vector<int> pick;
  for (int pass = 0; pass < 2 && pick.size() < want; ++pass) {  // non-critical boxes first
    for (int b : movable) {
      const auto& bx = boxes.boxes[size_t(b)];
      if (pick.size() < want && bin_of(bx.cx, bx.cy) == densest && (pass == 1 || !bx.critical)
          && std::ranges::find(pick, b) == pick.end()) {
        pick.push_back(b);
      }
    }
  }
  for (size_t k = 0; k < pick.size(); ++k) {  // spread along x, a tile apart
    const double cx = std::clamp(tx + (double(k) - double(pick.size() - 1) / 2) * double(g.tile_w), double(g.cx(c0)), double(g.cx(c1)));
    out.homes.push_back({pick[k], cx, ty, 1});
  }
  return out;
}
// tsm: the region's box graph (a star per net) loses edges, highest degree first and never those of nets with an
// endpoint outside the region's movable boxes, until every node has four or fewer; the reduced graph is drawn
// breadth first from its highest-degree node, each box on the free tile nearest the centroid of its placed
// neighbours (ports north, east, south and west: connected boxes adjacent), and the drawing is the new homes.
Alternative alt_tsm(const Design& d, const Grid& g, const Boxes& boxes, const std::vector<Net_ends>& ends, const std::vector<int>& movable,
                    const std::vector<int>& tiles, int r, const std::vector<char>& pinned_tile) {
  Alternative            out{"tsm", {}};
  const size_t           B = movable.size();
  std::vector<int>       node_of(boxes.boxes.size(), -1);
  for (size_t i = 0; i < B; ++i) {
    node_of[size_t(movable[i])] = int(i);
  }
  struct Edge {
    int  a, b;
    bool kept = true, held = false;  // held: the net has an endpoint past the movable boxes (a fixed pin, a hop, another region)
  };
  std::vector<Edge> edges;
  for (size_t n = 0; n < d.nets.size(); ++n) {
    const auto& per = ends[n].per_region;
    const auto  it  = per.find(r);
    if (it == per.end()) {
      continue;
    }
    std::vector<int> mine;
    bool             held = per.size() > 1;
    for (const auto& e : it->second) {
      if (e.box >= 0 && node_of[size_t(e.box)] >= 0) {
        mine.push_back(node_of[size_t(e.box)]);
      } else {
        held = true;
      }
    }
    std::ranges::sort(mine);
    mine.erase(std::unique(mine.begin(), mine.end()), mine.end());
    for (size_t q = 1; q < mine.size(); ++q) {
      edges.push_back({mine[0], mine[q], true, held});
    }
  }
  std::vector<std::vector<int>> adj(B);  // edge ids
  std::vector<int>              degree(B, 0);
  for (size_t e = 0; e < edges.size(); ++e) {
    adj[size_t(edges[e].a)].push_back(int(e));
    adj[size_t(edges[e].b)].push_back(int(e));
    ++degree[size_t(edges[e].a)];
    ++degree[size_t(edges[e].b)];
  }
  for (bool again = true; again;) {  // reduce: the highest-degree node drops its free edge to its busiest neighbour
    again   = false;
    int top = -1;
    for (size_t i = 0; i < B; ++i) {
      if (degree[i] > 4 && (top < 0 || degree[i] > degree[size_t(top)])) {
        top = int(i);
      }
    }
    if (top < 0) {
      break;
    }
    int drop = -1;
    for (int e : adj[size_t(top)]) {
      if (!edges[size_t(e)].kept || edges[size_t(e)].held) {
        continue;
      }
      const int other = edges[size_t(e)].a == top ? edges[size_t(e)].b : edges[size_t(e)].a;
      if (drop < 0 || degree[size_t(other)] > degree[size_t(edges[size_t(drop)].a == top ? edges[size_t(drop)].b : edges[size_t(drop)].a)]) {
        drop = e;
      }
    }
    if (drop < 0) {  // every edge held: this node keeps its degree, the others go on
      degree[size_t(top)] = -degree[size_t(top)];  // marked done
      again               = true;
      continue;
    }
    edges[size_t(drop)].kept = false;
    --degree[size_t(edges[size_t(drop)].a)];
    --degree[size_t(edges[size_t(drop)].b)];
    again = true;
  }
  for (auto& dg : degree) {
    dg = std::abs(dg);
  }
  // the drawing: breadth first from the busiest node
  std::vector<int> free;
  for (int t : tiles) {
    if (!g.blocked[t] && !pinned_tile[size_t(t)]) {
      free.push_back(t);
    }
  }
  if (free.empty() || B == 0) {
    return out;
  }
  std::vector<char>   placed(B, 0), taken(g.size(), 0);
  std::vector<double> px(B, 0), py(B, 0);
  std::vector<int>    order;
  std::vector<int>    queue;
  std::vector<char>   seen(B, 0);
  while (order.size() < B) {  // one component after another, busiest node first
    int root = -1;
    for (size_t i = 0; i < B; ++i) {
      if (!seen[i] && (root < 0 || degree[i] > degree[size_t(root)])) {
        root = int(i);
      }
    }
    queue.assign(1, root);
    seen[size_t(root)] = 1;
    for (size_t q = 0; q < queue.size(); ++q) {
      const int i = queue[q];
      order.push_back(i);
      std::vector<int> next;
      for (int e : adj[size_t(i)]) {
        if (!edges[size_t(e)].kept) {
          continue;
        }
        const int other = edges[size_t(e)].a == i ? edges[size_t(e)].b : edges[size_t(e)].a;
        if (!seen[size_t(other)]) {
          seen[size_t(other)] = 1;
          next.push_back(other);
        }
      }
      std::ranges::sort(next, [&](int x, int y) { return degree[size_t(x)] > degree[size_t(y)]; });
      queue.insert(queue.end(), next.begin(), next.end());
    }
  }
  double rcx = 0, rcy = 0;  // the region's centre
  for (int t : tiles) {
    rcx += double(g.cx(g.col(t)));
    rcy += double(g.cy(g.row(t)));
  }
  rcx /= double(tiles.size());
  rcy /= double(tiles.size());
  for (int i : order) {
    double tx = 0, ty = 0;
    int    n = 0;
    for (int e : adj[size_t(i)]) {
      if (!edges[size_t(e)].kept) {
        continue;
      }
      const int other = edges[size_t(e)].a == i ? edges[size_t(e)].b : edges[size_t(e)].a;
      if (placed[size_t(other)]) {
        tx += px[size_t(other)];
        ty += py[size_t(other)];
        ++n;
      }
    }
    if (n == 0) {  // a root: its own home, pulled to the region's centre
      tx = (boxes.boxes[size_t(movable[size_t(i)])].cx + rcx) / 2;
      ty = (boxes.boxes[size_t(movable[size_t(i)])].cy + rcy) / 2;
    } else {
      tx /= n;
      ty /= n;
    }
    const auto& bx     = boxes.boxes[size_t(movable[size_t(i)])];
    int         best_t = -1;
    double      best_d = INFINITY;
    for (int t : free) {
      if (taken[size_t(t)]) {
        continue;
      }
      const double dd = std::abs(double(g.cx(g.col(t))) - tx) + std::abs(double(g.cy(g.row(t))) - ty);
      if (dd < best_d && box_fits(d, g, bx, t)) {
        best_d = dd;
        best_t = t;
      }
    }
    if (best_t < 0) {
      px[size_t(i)] = bx.cx;
      py[size_t(i)] = bx.cy;
    } else {
      taken[size_t(best_t)] = 1;
      px[size_t(i)]         = double(g.cx(g.col(best_t)));
      py[size_t(i)]         = double(g.cy(g.row(best_t)));
      out.homes.push_back({movable[size_t(i)], px[size_t(i)], py[size_t(i)], -1});
    }
    placed[size_t(i)] = 1;
  }
  return out;
}

}  // namespace

void sat_place(const Design& d, Grid& g, Boxes& boxes, const Legroute_regions& regions, Routing& R, Legroute_report& rep) {
  rep.spilled_regions = balance_regions(d, g, boxes);
  // per box: in-box demand per direction, approximate (refined from legroute.final's results later): taps of the
  // external pins and the internal nets that leave the box layer
  std::vector<int> dem_h(boxes.boxes.size(), 0), dem_v(boxes.boxes.size(), 0);
  for (size_t b = 0; b < boxes.boxes.size(); ++b) {
    std::map<int, int> per_net;
    for (int c : boxes.boxes[b].cells) {
      for (int n : d.nets_of[c]) {
        if (d.nets[n].routed) {
          for (const auto& p : d.nets[n].pins) {
            per_net[n] += p.comp == d.movable[c];
          }
        }
      }
    }
    int                                  external = 0, internal = 0;
    // inbox_span: x spans of the internal nets (cell centres; pack_cells keeps the cells' x order)
    std::vector<std::pair<int64_t, int>> ends;
    for (const auto& [n, k] : per_net) {
      const bool inside               = k == int(d.nets[n].pins.size());
      (inside ? internal : external) += inside ? 1 : k;
      if (inside && d.opt.inbox_span) {
        int64_t lo = std::numeric_limits<int64_t>::max(), hi = std::numeric_limits<int64_t>::min();
        for (const auto& p : d.nets[n].pins) {
          const int64_t x = d.fp.components[p.comp].place.x + d.width[d.cell[p.comp]] / 2;
          lo              = std::min(lo, x);
          hi              = std::max(hi, x);
        }
        ends.emplace_back(lo, 1);
        ends.emplace_back(hi + 1, -1);
      }
    }
    int box_h = internal;  // the internal nets run on the box layer, counted with H
    if (d.opt.inbox_span) {
      std::ranges::sort(ends);
      int depth = 0;
      box_h     = 0;
      for (const auto& [x, e] : ends) {
        depth += e;
        box_h  = std::max(box_h, depth);
      }
    }
    dem_v[b] = (external + 2 * internal + 3) / 4;
    dem_h[b] = box_h + (external + 3) / 4;
  }
  // per tile and direction: summed tracks of the routed layers, less the clock spine (a vertical tap and trunk
  // anywhere, a rib every rib_tiles rows)
  std::vector<int> cap_h(g.size(), 0), cap_v(g.size(), 0);
  for (size_t l = 0; l < g.layers.size(); ++l) {
    for (size_t t = 0; t < g.size(); ++t) {
      (g.layers[l].horizontal ? cap_h : cap_v)[t] += g.cap[l][t];
    }
  }
  for (size_t t = 0; t < g.size(); ++t) {  // no layer is reserved for the boxes: the box layer's tracks are H too
    cap_h[t] += g.box_cap[t];
  }
  if (d.opt.clock_spine) {
    for (size_t t = 0; t < g.size(); ++t) {
      cap_v[t] -= 1;
      cap_h[t] -= g.row(int(t)) % std::max(1u, d.opt.rib_tiles) == 0;
    }
  }
  // endpoints of every routed signal net, per region
  const auto box_of_comp = [&](int comp) { return comp >= 0 && d.cell[comp] >= 0 ? boxes.box_of[d.cell[comp]] : -1; };
  const auto cap_h0 = cap_h, cap_v0 = cap_v;  // the tracks before hops and pre-placement
  // the policies: the options, and with sat_alt a second set; each region solves under both at once and keeps the
  // better answer
  std::vector<Legroute_options> policies{d.opt};
  if (!d.opt.sat_alt.empty()) {
    policies.push_back(sat_alt_options(d.opt));
  }
  struct Policy {
    Boxes                                     boxes;
    std::vector<int>                          cap_h, cap_v;
    std::set<std::pair<int, int>>             treed;    // (net, region) routed by a fixed tree
    std::unordered_map<int, std::vector<int>> allowed;  // box -> the only tiles it may take (an IO pre-route's track)
    std::vector<Fixed_tree>                   trees;
    std::vector<Io_route>                     io_routes;
    std::vector<char>                         pinned_tile;
    Edge_reserve                              reserve;  // the fixed routes' tracks per edge
    Legroute_report                           rep;      // pre-placement counters
  };
  std::vector<std::vector<Region_out>> last(policies.size(), std::vector<Region_out>(g.regions));
  std::vector<int>                     chosen(g.regions, 0);
  std::vector<Policy>                  P;
  Crossings                            x;
  std::vector<uint64_t> sig_prev(size_t(g.regions), 0);
  std::vector<unsigned> region_extra(size_t(g.regions), 0);  // window_grow: extra window tiles of a region that failed
  std::vector<int>      gave_to(size_t(g.regions), -1);      // spill_shift: the region each region last handed a strip to
  for (unsigned round = 0;; ++round) {
    // spill rounds: the region adjacency (a boundary shift changes it)
    std::vector<std::set<int>> region_adj(size_t(g.regions));
    for (int t = 0; t < int(g.size()); ++t) {
      for (int q : {g.col(t) + 1 < g.cols ? t + 1 : -1, t + g.cols < int(g.size()) ? t + g.cols : -1}) {
        if (q >= 0 && g.region[t] != g.region[q]) {
          region_adj[size_t(g.region[t])].insert(g.region[q]);
          region_adj[size_t(g.region[q])].insert(g.region[t]);
        }
      }
    }
    x = crossings(d, g, boxes, regions, cap_h, cap_v, true);
    P.assign(policies.size(), {});
    for (size_t p = 0; p < policies.size(); ++p) {  // the policy's pre-placement on its own copy
      const auto& o = policies[p];
      auto&       Q = P[p];
      Q.boxes       = boxes;
      Q.cap_h       = cap_h;
      Q.cap_v       = cap_v;
      for (const auto& gp : x.pieces) {  // legroute.global: the net is routed ahead through these regions
        Q.treed.emplace(gp.net, gp.region);
      }
      if (o.crit_boxes > 0) {
        place_critical(d, o, g, Q.boxes, x.ends, Q.rep);
      }
      if (o.tree_boxes > 0 || o.tree_load > 0) {
        Q.trees = place_trees(d, o, g, Q.boxes, x.ends, dem_h, dem_v, Q.cap_h, Q.cap_v, Q.treed, Q.rep);
      }
      if (o.io_preroute) {
        Q.io_routes = io_preroute(d, o, g, Q.boxes, x.ends, Q.cap_h, Q.cap_v, cap_h0, cap_v0, Q.treed, Q.allowed, Q.rep);
      }
      if (o.crit_boxes > 0 || !Q.trees.empty()) {
        repair_forced(d, o, g, Q.boxes, x.ends, dem_h, dem_v, Q.cap_h, Q.cap_v, Q.trees, Q.treed, Q.rep);
      }
      Q.pinned_tile.assign(g.size(), 0);
      for (const auto& box : Q.boxes.boxes) {
        if (box.fixed) {
          Q.pinned_tile[box.tile] = 1;
        }
      }
      Q.reserve = edge_reserve(g, x.hops, x.hop_net, Q.trees, Q.io_routes, x.pieces);
    }
    // ---- one SAT model per region and policy (the policies in parallel)
    std::vector<std::vector<int>> region_boxes(g.regions), region_tiles(g.regions);
    for (int b = 0; b < int(boxes.boxes.size()); ++b) {
      region_boxes[boxes.boxes[b].region].push_back(b);
    }
    for (int t = 0; t < int(g.size()); ++t) {
      region_tiles[g.region[t]].push_back(t);
    }
    // a region whose boxes, fixed boxes, hops, global pieces, trees and IO pre-routes are what they were in the last
    // round keeps its answer (one policy): only the regions a spill touched solve again
    std::vector<char> reuse(size_t(g.regions), 0);
    {
      std::vector<uint64_t> sig(size_t(g.regions), 0x9e3779b97f4a7c15ull);
      const auto            mix = [](uint64_t& h, uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); };
      for (int r = 0; r < g.regions; ++r) {
        mix(sig[size_t(r)], region_extra[size_t(r)]);
        for (int b : region_boxes[size_t(r)]) {
          mix(sig[size_t(r)], uint64_t(b) * 3 + (boxes.boxes[size_t(b)].fixed ? 1 : 0));
          mix(sig[size_t(r)], uint64_t(boxes.boxes[size_t(b)].tile + 1));
          if (P[0].allowed.contains(b)) {
            mix(sig[size_t(r)], 7);
          }
        }
      }
      for (size_t k = 0; k < x.hops.size(); ++k) {
        const auto [a, b] = x.hops[k];
        for (int r : {g.region[a], g.region[b]}) {
          mix(sig[size_t(r)], (uint64_t(x.hop_net[k]) << 32) ^ (uint64_t(a) << 16) ^ uint64_t(b));
        }
      }
      for (const auto& gp : x.pieces) {
        mix(sig[size_t(gp.region)], (uint64_t(gp.net) << 32) ^ (uint64_t(gp.a) << 16) ^ uint64_t(gp.b));
      }
      for (const auto& ft : P[0].trees) {
        for (const auto& piece : ft.pieces) {
          mix(sig[size_t(piece.region)], (uint64_t(piece.net) << 32) ^ (uint64_t(piece.a) << 16) ^ uint64_t(piece.b) ^ (ft.dropped ? 1 : 0));
        }
      }
      for (const auto& io : P[0].io_routes) {
        mix(sig[size_t(io.region)], (uint64_t(io.net_a) << 32) ^ (uint64_t(io.ta) << 16) ^ uint64_t(io.tb));
      }
      for (int r = 0; r < g.regions; ++r) {
        reuse[size_t(r)] = round > 0 && policies.size() == 1 && sig[size_t(r)] == sig_prev[size_t(r)]
                           && last[0][size_t(r)].status == Sat_status::satisfiable;
      }
      sig_prev = sig;
    }
    std::fprintf(stderr,
                 "legroute sat: %d regions, %zu virtual pins, %zu boxes, %zu hops (%zu over the share, %zu unjoined), %zu global "
                 "pieces; solving %zu at a time\n",
                 g.regions,
                 regions.pins.size(),
                 boxes.boxes.size(),
                 x.hops.size(),
                 x.over_share,
                 x.unjoined,
                 x.pieces.size(),
                 size_t(std::max(1u, d.opt.threads)));
    // the regions most at risk first: box fill plus the share of the region's tracks the routes fixed ahead took
    // (hops, pieces, trees, IO pre-routes). A failing design then fails early (fail_fast ends the solve at the
    // first failure), and the long solves start first, so the threads end together
    std::vector<double> risk(size_t(g.regions), 0), cut_ratio(size_t(g.regions), 0);
    size_t              over_cut = 0;
    {
      std::vector<double> free_t(size_t(g.regions), 0), cap(size_t(g.regions), 0), taken(size_t(g.regions), 0);
      for (int t = 0; t < int(g.size()); ++t) {
        const auto r  = size_t(g.region[t]);
        free_t[r]    += !g.blocked[t];
        cap[r]       += cap_h0[t] + cap_v0[t];
        taken[r]     += P[0].reserve.h[size_t(t)] + P[0].reserve.v[size_t(t)];
      }
      // the cut bound from the placement (the exact one is in solve_region): nets whose endpoints' windows are held
      // on both sides of a tile boundary of the region, over the tracks of the boundary's edges; a ratio above one
      // is unsat before any solving, so these regions go first
      struct Rect {
        int c0, c1, r0, r1;
      };
      std::vector<Rect> rect(size_t(g.regions), {g.cols, -1, g.rows, -1});
      for (int t = 0; t < int(g.size()); ++t) {
        auto& rc = rect[size_t(g.region[t])];
        rc       = {std::min(rc.c0, g.col(t)), std::max(rc.c1, g.col(t)), std::min(rc.r0, g.row(t)), std::max(rc.r1, g.row(t))};
      }
      std::vector<std::vector<int>> fc(size_t(g.regions)), fr(size_t(g.regions));
      for (int r = 0; r < g.regions; ++r) {
        fc[size_t(r)].assign(size_t(std::max(0, rect[size_t(r)].c1 - rect[size_t(r)].c0) + 2), 0);
        fr[size_t(r)].assign(size_t(std::max(0, rect[size_t(r)].r1 - rect[size_t(r)].r0) + 2), 0);
      }
      const int   win_t = int(policies[0].window_tiles);
      const auto& Q0    = P[0];
      for (size_t n = 0; n < d.nets.size(); ++n) {
        for (const auto& [r, eps_r] : x.ends[n].per_region) {
          const auto& rc = rect[size_t(r)];
          if (eps_r.size() < 2 || rc.c1 < 0 || Q0.treed.contains({int(n), r})) {
            continue;
          }
          int lo_c = g.cols, hi_c = -1, lo_r = g.rows, hi_r = -1;
          for (const auto& ep : eps_r) {
            int ec0, ec1, er0, er1;
            if (ep.box >= 0 && !Q0.boxes.boxes[size_t(ep.box)].fixed) {
              const auto& bx = Q0.boxes.boxes[size_t(ep.box)];
              const int   cc = std::clamp(g.col_of(bx.cx), rc.c0, rc.c1), rr = std::clamp(g.row_of(bx.cy), rc.r0, rc.r1);
              ec0 = std::max(rc.c0, cc - win_t);
              ec1 = std::min(rc.c1, cc + win_t);
              er0 = std::max(rc.r0, rr - win_t);
              er1 = std::min(rc.r1, rr + win_t);
            } else {
              const int t = ep.box >= 0 ? Q0.boxes.boxes[size_t(ep.box)].tile : ep.tile;
              ec0 = ec1 = std::clamp(g.col(t), rc.c0, rc.c1);
              er0 = er1 = std::clamp(g.row(t), rc.r0, rc.r1);
            }
            lo_c = std::min(lo_c, ec1);
            hi_c = std::max(hi_c, ec0);
            lo_r = std::min(lo_r, er1);
            hi_r = std::max(hi_r, er0);
          }
          if (eps_r.size() == 2 && d.nets[n].pins.size() == 2 && eps_r[0].box >= 0 && eps_r[1].box >= 0) {
            if (hi_c == lo_c + 1) {  // may sit side by side: no track
              hi_c = lo_c;
            }
            if (hi_r == lo_r + 1) {
              hi_r = lo_r;
            }
          }
          if (hi_c > lo_c) {
            ++fc[size_t(r)][size_t(lo_c + 1 - rc.c0)];
            --fc[size_t(r)][size_t(hi_c + 1 - rc.c0)];
          }
          if (hi_r > lo_r) {
            ++fr[size_t(r)][size_t(lo_r + 1 - rc.r0)];
            --fr[size_t(r)][size_t(hi_r + 1 - rc.r0)];
          }
        }
      }
      for (int r = 0; r < g.regions; ++r) {
        const auto& rc = rect[size_t(r)];
        if (rc.c1 >= 0) {
          for (int c = rc.c0 + 1, run = 0; c <= rc.c1; ++c) {
            run += fc[size_t(r)][size_t(c - rc.c0)];
            int tracks = 0;
            for (int j = rc.r0; j <= rc.r1; ++j) {
              const int t = g.tile(c - 1, j), q = g.tile(c, j);
              if (g.region[t] == r && g.region[q] == r) {
                tracks += std::max(0, int(std::floor(std::min(cap_h0[t], cap_h0[q]) * policies[0].cap_scale)) - Q0.reserve.h[size_t(t)]);
              }
            }
            cut_ratio[size_t(r)] = std::max(cut_ratio[size_t(r)], double(run) / std::max(1.0, double(tracks)));
          }
          for (int j = rc.r0 + 1, run = 0; j <= rc.r1; ++j) {
            run += fr[size_t(r)][size_t(j - rc.r0)];
            int tracks = 0;
            for (int i = rc.c0; i <= rc.c1; ++i) {
              const int t = g.tile(i, j - 1), q = g.tile(i, j);
              if (g.region[t] == r && g.region[q] == r) {
                tracks += std::max(0, int(std::floor(std::min(cap_v0[t], cap_v0[q]) * policies[0].cap_scale)) - Q0.reserve.v[size_t(t)]);
              }
            }
            cut_ratio[size_t(r)] = std::max(cut_ratio[size_t(r)], double(run) / std::max(1.0, double(tracks)));
          }
        }
        over_cut       += cut_ratio[size_t(r)] > 1;
        risk[size_t(r)] = cut_ratio[size_t(r)] + double(region_boxes[r].size()) / std::max(1.0, free_t[size_t(r)])
                          + taken[size_t(r)] / std::max(1.0, cap[size_t(r)]);
      }
    }
    std::vector<size_t> task_order(size_t(g.regions) * policies.size());
    std::iota(task_order.begin(), task_order.end(), 0);
    std::ranges::stable_sort(task_order, [&](size_t a, size_t b) { return risk[a / policies.size()] > risk[b / policies.size()]; });
    {
      std::string head;
      for (size_t k = 0; k < std::min<size_t>(8, task_order.size()); k += policies.size()) {
        const auto r = task_order[k] / policies.size();
        head += std::format(" {}({:.2f}, cut {:.2f})", r, risk[r], cut_ratio[r]);
      }
      std::fprintf(stderr,
                   "legroute sat: regions by risk (cut ratio + fill + fixed share), %zu over the cut bound by the placement, "
                   "first:%s\n",
                   over_cut,
                   head.c_str());
    }
    // hop_choice: the regions solve in checkerboard waves (a greedy colouring of the region adjacency, in risk
    // order, so the riskiest regions choose first); a hop between two regions is chosen by the earlier wave's region
    // among the boundary tiles within hop_choice of the hop chooser's tile, and is the later region's fixed
    // endpoint. A net routed ahead (a tree, an IO pre-route, a global piece) keeps its hops, and so does every hop
    // when a second policy runs (the policies would disagree)
    std::vector<unsigned>                         wave(size_t(g.regions), 0);
    unsigned                                      waves = 1;
    std::vector<std::vector<int>>                 hop_tiles(x.hops.size());   // per hop: the owner's candidate tiles
    std::vector<std::vector<std::pair<int, int>>> hop_across(x.hops.size());  // per hop: (owner tile, the tile across)
    std::vector<int>                              hop_owner(x.hops.size(), -1), hop_other_tile(x.hops.size(), -1);
    std::vector<int>                              hop_room(g.size(), 0);  // per boundary tile: hops it may still take
    if (d.opt.hop_choice > 0 && policies.size() == 1) {
      std::vector<std::set<int>> adj(size_t(g.regions));
      for (int t = 0; t < int(g.size()); ++t) {
        for (int q : {g.col(t) + 1 < g.cols ? t + 1 : -1, t + g.cols < int(g.size()) ? t + g.cols : -1}) {
          if (q >= 0 && g.region[t] != g.region[q]) {
            adj[size_t(g.region[t])].insert(g.region[q]);
            adj[size_t(g.region[q])].insert(g.region[t]);
          }
        }
      }
      std::vector<char> colored(size_t(g.regions), 0);
      for (size_t k = 0; k < task_order.size(); k += policies.size()) {
        const auto         rr = task_order[k] / policies.size();
        std::set<unsigned> used;
        for (int s2 : adj[rr]) {
          if (colored[size_t(s2)]) {
            used.insert(wave[size_t(s2)]);
          }
        }
        unsigned w = 0;
        while (used.contains(w)) {
          ++w;
        }
        wave[rr]    = w;
        colored[rr] = 1;
        waves       = std::max(waves, w + 1);
      }
      std::vector<char> net_fixed(d.nets.size(), 0);  // nets with routes fixed ahead keep their hops
      for (const auto& gp : x.pieces) {
        net_fixed[size_t(gp.net)] = 1;
      }
      for (const auto& [nr, rr] : P[0].treed) {
        net_fixed[size_t(nr)] = 1;
      }
      for (const auto& io : P[0].io_routes) {
        if (io.net_a >= 0) {
          net_fixed[size_t(io.net_a)] = 1;
        }
        if (io.net_b >= 0) {
          net_fixed[size_t(io.net_b)] = 1;
        }
      }
      std::vector<int>  fixed_hops(g.size(), 0), default_hops(g.size(), 0);
      std::vector<char> hop_free(x.hops.size(), 0);
      size_t            hops_free = 0;  // this round's
      for (size_t k = 0; k < x.hops.size(); ++k) {
        const auto [a, b] = x.hops[k];
        hop_free[k] = !net_fixed[size_t(x.hop_net[k])] && g.region[a] != g.region[b] && wave[size_t(g.region[a])] != wave[size_t(g.region[b])];
        if (!hop_free[k]) {
          ++fixed_hops[size_t(a)];
          ++fixed_hops[size_t(b)];
        }
      }
      for (size_t k = 0; k < x.hops.size(); ++k) {
        if (!hop_free[k]) {
          continue;
        }
        const auto [a, b]  = x.hops[k];
        const int  ra = g.region[a], rb = g.region[b];
        const bool a_owns  = wave[size_t(ra)] < wave[size_t(rb)];
        const int  ao = a_owns ? a : b, bo = a_owns ? b : a, owner = a_owns ? ra : rb, other = a_owns ? rb : ra;
        const bool h       = g.row(a) == g.row(b);  // across a vertical boundary: the candidates run along the rows
        const int  dc = g.col(bo) - g.col(ao), dr = g.row(bo) - g.row(ao), hc = int(d.opt.hop_choice);
        ++default_hops[size_t(ao)];
        for (int i = -hc; i <= hc; ++i) {
          const int c = g.col(ao) + (h ? 0 : i), rr = g.row(ao) + (h ? i : 0);
          if (c < 0 || c >= g.cols || rr < 0 || rr >= g.rows || c + dc < 0 || c + dc >= g.cols || rr + dr < 0 || rr + dr >= g.rows) {
            continue;
          }
          const int t = g.tile(c, rr), q = g.tile(c + dc, rr + dr);
          if (g.region[t] != owner || g.region[q] != other || g.blocked[t] || g.blocked[q]) {
            continue;
          }
          const int room = int(std::floor(d.opt.hop_share * double((h ? cap_h0 : cap_v0)[size_t(t)]))) - fixed_hops[size_t(t)];
          if (room <= 0 && t != ao) {
            continue;  // no room there; the chooser's own tile stays a candidate
          }
          hop_room[size_t(t)] = std::max(hop_room[size_t(t)], room);
          hop_tiles[k].push_back(t);
          hop_across[k].emplace_back(t, q);
        }
        if (hop_tiles[k].size() < 2) {  // nothing to choose
          hop_tiles[k].clear();
          hop_across[k].clear();
          continue;
        }
        hop_owner[k]      = owner;
        hop_other_tile[k] = bo;
        auto& eps_o       = x.ends[size_t(x.hop_net[k])].per_region[owner];
        for (auto& e : eps_o) {  // the owner's endpoint is free (its tile stays the default for every other reader)
          if (e.box < 0 && e.tile == ao && e.hop < 0) {
            e.hop = int(k);
            break;
          }
        }
        ++hops_free;
      }
      rep.sat_hops_free += hops_free;
      for (size_t t = 0; t < g.size(); ++t) {  // the hops already there stay possible
        hop_room[t] = std::max(hop_room[t], default_hops[t]);
      }
      std::fprintf(stderr,
                   "legroute sat: hop_choice %u: %u waves, %zu of %zu hops free (within %u tiles of their chooser's tile)\n",
                   d.opt.hop_choice,
                   waves,
                   hops_free,
                   x.hops.size(),
                   d.opt.hop_choice);
    }
    rep.sat_waves += waves;
    std::atomic<bool> failed_fast{false};
    // every (region, policy) solve is a task: the regions of one wave are independent once the hops are fixed
    const auto solve_task = [&](size_t slot) {
      const size_t task  = task_order[slot];
      const int    r     = int(task / policies.size());
      const size_t p     = task % policies.size();
      auto&        Q     = P[p];
      auto&        out   = last[p][r];
      const auto   start = Clock::now();
      if (reuse[size_t(r)]) {  // a spill round: nothing of this region changed, its answer stands
        for (const auto& [b, t] : out.tiles) {
          Q.boxes.boxes[size_t(b)].tile = t;
        }
        return;
      }
      out = Region_out{};
      if (failed_fast.load()) {  // fail_fast: the placement restarts anyway; the region's boxes take fitting tiles, no model
        out.skipped = true;
        out.status  = Sat_status::unknown;
        ++out.rep.sat_fail_fast_skipped;
      }
      auto po = policies[p];
      po.window_tiles += region_extra[size_t(r)];
      try {
        solve_region(d,
                     po,
                     g,
                     r,
                     region_boxes[r],
                     region_tiles[r],
                     Q.boxes,
                     x.ends,
                     Q.treed,
                     Q.allowed,
                     Q.pinned_tile,
                     policies.size() > 1 ? std::format(" policy {}", p) : "",
                     cap_h0,
                     cap_v0,
                     Q.reserve,
                     hop_tiles,
                     hop_room,
                     out);
      } catch (const Legroute_unfit& e) {  // more boxes than fitting tiles: a spill round pushes boxes out first
        if (round >= d.opt.spill_rounds || policies.size() > 1) {
          throw;
        }
        out        = Region_out{};
        out.status = Sat_status::unsatisfiable;
        out.unfit  = e.what();
        std::fprintf(stderr, "legroute sat: %s; a spill round pushes its boxes out\n", e.what());
      }
      out.seconds = seconds_since(start);
      if (d.opt.fail_fast && out.status != Sat_status::satisfiable && !out.skipped) {
        if (!failed_fast.exchange(true)) {
          std::fprintf(stderr, "legroute sat: fail fast: region %d has no routable answer, the other regions are left\n", r);
        }
      }
      for (const auto& ft : Q.trees) {  // the policy's fixed trees and IO pre-routes in the region
        for (const auto& piece : ft.pieces) {
          if (!ft.dropped && piece.region == r) {
            out.conns.push_back({piece.net, piece.a, piece.b, true});
          }
        }
      }
      for (const auto& gp : x.pieces) {  // legroute.global: the pass-through runs through this region
        if (gp.region == r) {
          out.conns.push_back({gp.net, gp.a, gp.b, gp.h_first});
        }
      }
      for (const auto& io : Q.io_routes) {  // (a box off its lifted track bends to it)
        if (io.region != r) {
          continue;
        }
        if (io.box < 0) {
          out.conns.push_back({io.net_a, io.ta, io.tb, io.h_first});
        } else {
          out.conns.push_back({io.net_a, io.ta, Q.boxes.boxes[io.box].tile, io.h_first});
          out.conns.push_back({io.net_b, Q.boxes.boxes[io.box].tile, io.tb, io.h_first});
        }
      }
      for (const auto& c : out.conns) {  // the timing proxy: weighted route length (tiles)
        out.cost += d.nets[c.net].weight * double(std::abs(g.col(c.ta) - g.col(c.tb)) + std::abs(g.row(c.ta) - g.row(c.tb)));
      }
    };
    for (unsigned w = 0; w < waves; ++w) {
      std::vector<size_t> slots;
      for (size_t k = 0; k < task_order.size(); ++k) {
        if (wave[task_order[k] / policies.size()] == w) {
          slots.push_back(k);
        }
      }
      lr::parallel(slots.size(), d.opt.threads, [&](size_t i) { solve_task(slots[i]); });
      if (w + 1 < waves) {  // this wave's hop choices are the later regions' fixed endpoints; the reserves follow
        for (size_t k = 0; k < x.hops.size(); ++k) {
          if (hop_owner[k] < 0 || wave[size_t(hop_owner[k])] != w) {
            continue;
          }
          const auto& res    = last[0][size_t(hop_owner[k])];
          const auto [a, b]  = x.hops[k];
          const int   ao     = g.region[a] == hop_owner[k] ? a : b;
          int         picked = -1;
          if (res.status == Sat_status::satisfiable) {
            for (const auto& [nn, dt, t] : res.hop_tiles) {
              if (nn == x.hop_net[k] && dt == ao) {
                picked = t;
                break;
              }
            }
          }
          if (picked < 0) {
            picked = ao;  // the region failed or kept the default: the hop stays
          }
          int across = -1;
          for (const auto& [t, q] : hop_across[k]) {
            if (t == picked) {
              across = q;
              break;
            }
          }
          const int n = x.hop_net[k];
          for (auto& e : x.ends[size_t(n)].per_region[hop_owner[k]]) {
            if (e.box < 0 && e.hop == int(k)) {
              e.tile = across >= 0 ? picked : ao;
              e.hop  = -1;
            }
          }
          if (across < 0 || picked == ao) {
            continue;
          }
          ++rep.sat_hops_moved;
          x.hops[k]       = {picked, across};
          const int other = g.region[across];
          for (auto& e : x.ends[size_t(n)].per_region[other]) {
            if (e.box < 0 && e.hop < 0 && e.tile == hop_other_tile[k]) {
              e.tile = across;
              break;
            }
          }
        }
        for (auto& Q : P) {
          Q.reserve = edge_reserve(g, x.hops, x.hop_net, Q.trees, Q.io_routes, x.pieces);
        }
      }
    }
    for (int r = 0; r < g.regions; ++r) {  // a routable answer first, then the lower cost; none: the first policy's
      int best = -1;
      for (int p = 0; p < int(policies.size()); ++p) {
        if (last[size_t(p)][r].status == Sat_status::satisfiable
            && (best < 0 || last[size_t(p)][r].cost < last[size_t(best)][r].cost)) {
          best = p;
        }
      }
      chosen[r] = std::max(best, 0);
    }
    std::vector<int> failed;
    for (int r = 0; r < g.regions; ++r) {
      if (last[size_t(chosen[r])][size_t(r)].status != Sat_status::satisfiable) {
        failed.push_back(r);
      }
    }
    if (failed.empty() || round >= d.opt.spill_rounds || policies.size() > 1) {
      break;
    }
    // spill: each failing region pushes about spill_fraction of its boxes into its neighbours with room (the next ring
    // of regions too when the neighbours' room is short), the boxes and their neighbours chosen by min cuts inside the
    // region (spill_cut), each box onto the nearest free tile it fits in its new region; the boxes keep their cells,
    // the next round redoes the crossings and solves what changed. A failing neighbour receives too: the two sides
    // of a hot boundary fail together, and a box crossing it toward its nets takes its hops with it
    // a box the policy pinned (critical, tree, IO pre-route) is fixed on the policy's copy of the boxes
    const auto       is_fixed = [&](int b) { return P[0].boxes.boxes[size_t(b)].fixed || P[0].allowed.contains(b); };
    std::vector<int> boxes_in(size_t(g.regions), 0), fit_tiles(size_t(g.regions), 0);
    for (int b = 0; b < int(boxes.boxes.size()); ++b) {
      boxes_in[size_t(boxes.boxes[size_t(b)].region)] += !is_fixed(b);
    }
    for (int t = 0; t < int(g.size()); ++t) {  // tiles a movable box can take: unblocked and not pinned by a fixed box
      fit_tiles[size_t(g.region[t])] += !g.blocked[t] && !P[0].pinned_tile[size_t(t)];
    }
    std::set<int> taken;  // tiles given to boxes this round
    size_t        moved = 0, grown = 0;
    std::vector<std::string> kinds;  // alternatives, in priority order
    for (size_t at = 0; at < d.opt.alternatives.size();) {
      const size_t comma = d.opt.alternatives.find(',', at);
      const auto   kind  = d.opt.alternatives.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
      if (kind != "noise" && kind != "anchor" && kind != "tsm") {
        throw std::invalid_argument("legroute: unknown alternative placement '" + kind + "' (noise, anchor, tsm)");
      }
      kinds.push_back(kind);
      at = comma == std::string::npos ? d.opt.alternatives.size() : comma + 1;
    }
    for (int r : failed) {
      if (!kinds.empty()) {  // alternative placements of the region's boxes, solved in parallel with the round's hops fixed
        std::vector<int> movable;
        for (int b : region_boxes[size_t(r)]) {
          if (!P[0].boxes.boxes[size_t(b)].fixed && !P[0].allowed.contains(b)) {
            movable.push_back(b);
          }
        }
        std::vector<Alternative> cands;
        for (const auto& kind : kinds) {
          if (kind == "noise") {
            cands.push_back(alt_noise(g, boxes, movable, region_tiles[size_t(r)], int(d.opt.window_tiles), 1234u + round * 131u + unsigned(r)));
          } else if (kind == "anchor") {
            cands.push_back(alt_anchor(g, boxes, movable, region_tiles[size_t(r)]));
          } else {
            cands.push_back(alt_tsm(d, g, boxes, x.ends, movable, region_tiles[size_t(r)], r, P[0].pinned_tile));
          }
        }
        std::erase_if(cands, [](const Alternative& a) { return a.homes.empty(); });
        std::vector<Region_out>              outs(cands.size());
        std::vector<std::thread>             pool;
        const std::vector<std::vector<int>>  no_free_hops(hop_tiles.size());
        const auto                           t0 = Clock::now();
        for (size_t c = 0; c < cands.size(); ++c) {
          pool.emplace_back([&, c] {
            Boxes bx = P[0].boxes;
            for (const auto& h : cands[c].homes) {
              bx.boxes[size_t(h.box)].cx     = h.cx;
              bx.boxes[size_t(h.box)].cy     = h.cy;
              bx.boxes[size_t(h.box)].window = h.window;
            }
            auto po = policies[0];
            po.window_tiles += region_extra[size_t(r)];
            if (d.opt.alt_ms > 0) {
              po.sat_ms = d.opt.alt_ms;
            }
            try {
              solve_region(d, po, g, r, region_boxes[size_t(r)], region_tiles[size_t(r)], bx, x.ends, P[0].treed, P[0].allowed, P[0].pinned_tile,
                           " alternative " + cands[c].kind, cap_h0, cap_v0, P[0].reserve, no_free_hops, hop_room, outs[c]);
            } catch (const Legroute_unfit&) {
              outs[c].status = Sat_status::unsatisfiable;
            }
          });
        }
        for (auto& th : pool) {
          th.join();
        }
        rep.alt_tried += cands.size();
        rep.alt_seconds += seconds_since(t0);
        std::string line;
        int         winner = -1;
        for (size_t c = 0; c < cands.size(); ++c) {
          const auto st = outs[c].status == Sat_status::satisfiable ? "sat" : outs[c].status == Sat_status::unsatisfiable ? "unsat" : "unknown";
          line += std::format(" {} {} {:.1f} s", cands[c].kind, st, outs[c].seconds);
          if (winner < 0 && outs[c].status == Sat_status::satisfiable) {
            winner = int(c);
          }
        }
        std::fprintf(stderr, "legroute sat: spill round %u: region %d alternatives:%s%s\n", round + 1, r, line.c_str(),
                     winner >= 0 ? std::format(" -> {} adopted", cands[size_t(winner)].kind).c_str() : " -> none routed");
        if (winner >= 0) {  // the candidate's homes and answer stand; no spill for this region
          for (const auto& h : cands[size_t(winner)].homes) {
            for (Boxes* set : {&boxes, &P[0].boxes}) {
              set->boxes[size_t(h.box)].cx     = h.cx;
              set->boxes[size_t(h.box)].cy     = h.cy;
              set->boxes[size_t(h.box)].window = h.window;
            }
          }
          last[0][size_t(r)] = std::move(outs[size_t(winner)]);
          ++rep.alt_rescued;
          ++grown;  // the round changed something: it goes on
          continue;
        }
      }
      // window_grow: freedom of movement first; the region spills once its windows are grown twice
      if (d.opt.window_grow > 0 && region_extra[size_t(r)] < 2 * d.opt.window_grow) {
        region_extra[size_t(r)] += d.opt.window_grow;
        ++grown;
        std::fprintf(stderr, "legroute sat: spill round %u: region %d widens its windows to %u tiles\n", round + 1, r,
                     d.opt.window_tiles + region_extra[size_t(r)]);
        continue;
      }
      std::vector<int> cand;
      for (int b : region_boxes[size_t(r)]) {
        if (!is_fixed(b)) {
          cand.push_back(b);
        }
      }
      const size_t want
          = std::min(cand.size(), std::max<size_t>(1, size_t(std::ceil(d.opt.spill_fraction * double(region_boxes[size_t(r)].size())))));
      if (d.opt.spill_shift) {  // the boundary moves: the strip next to the neighbour the nets reach most, boxes included
        std::vector<int> around;  // never straight back to a region that just handed r a strip (no ping-pong)
        for (int s2 : region_adj[size_t(r)]) {
          if (gave_to[size_t(s2)] != r) {
            around.push_back(s2);
          }
        }
        const auto pulls = spill_pulls(d, x.ends, r, around);
        size_t     best  = 0;
        for (size_t j = 0; j < pulls.size(); ++j) {
          if (pulls[j] > pulls[best]) {
            best = j;
          }
        }
        if (around.empty() || pulls[best] == 0) {
          std::fprintf(stderr, "legroute sat: spill round %u: region %d has no neighbour its nets reach\n", round + 1, r);
          continue;
        }
        const int        s2 = around[best];
        std::vector<int> strip;
        size_t           on_strip = 0;
        int              deep     = 0;
        for (int k = 1; k <= 4; ++k) {  // the smallest depth holding the wanted boxes; r keeps a connected quarter at least
          const auto try_strip = boundary_strip(g, r, s2, k);
          if (try_strip.size() * 4 > region_tiles[size_t(r)].size() * 3 || !connected_without(g, r, try_strip)) {
            break;
          }
          const std::set<int> in_strip(try_strip.begin(), try_strip.end());
          size_t              n = 0;
          for (int b : region_boxes[size_t(r)]) {  // a box's tile is set by the answer: its home is its centroid's tile
            n += in_strip.contains(g.tile_of(boxes.boxes[size_t(b)].cx, boxes.boxes[size_t(b)].cy));
          }
          strip    = try_strip;
          on_strip = n;
          deep     = k;
          if (n >= std::max<size_t>(1, want)) {
            break;
          }
        }
        if (strip.empty()) {
          std::fprintf(stderr, "legroute sat: spill round %u: region %d cannot hand a strip to region %d (it would split or empty)\n",
                       round + 1, r, s2);
          continue;
        }
        const std::set<int> in_strip(strip.begin(), strip.end());
        for (int t : strip) {
          g.region[t] = s2;
        }
        for (int b : region_boxes[size_t(r)]) {
          if (in_strip.contains(g.tile_of(boxes.boxes[size_t(b)].cx, boxes.boxes[size_t(b)].cy))) {
            boxes.boxes[size_t(b)].region = s2;
          }
        }
        g.region_tiles.assign(size_t(g.regions), {});
        for (int t = 0; t < int(g.size()); ++t) {
          g.region_tiles[size_t(g.region[t])].push_back(t);
        }
        moved += on_strip + 1;  // the tiles moved even when no box did
        gave_to[size_t(r)] = s2;
        rep.shifted_tiles += strip.size();
        std::fprintf(stderr, "legroute sat: spill round %u: region %d hands %zu tiles (%d deep, %zu boxes) next to region %d to it\n",
                     round + 1, r, strip.size(), deep, on_strip, s2);
        continue;
      }
      if (cand.empty()) {
        std::fprintf(stderr, "legroute sat: spill round %u: region %d has no movable box\n", round + 1, r);
        continue;
      }
      // a region's room: up to the fill that evens it with the failing region, and never past nine tenths of its
      // tiles (the solver needs slack); its tiles counted as those a typical box leaving (the median width) fits (a
      // tile a tap or an obstacle narrows takes no full box); the balance after the round catches the rest
      std::vector<int> by_sites = cand;
      std::ranges::sort(by_sites, [&](int a, int b) { return boxes.boxes[size_t(a)].sites < boxes.boxes[size_t(b)].sites; });
      const int          typical = by_sites.empty() ? -1 : by_sites[by_sites.size() / 2];
      std::map<int, int> fits_in;  // per region: tiles the typical box fits
      const auto         fit_count = [&](int s2) {
        auto it = fits_in.find(s2);
        if (it == fits_in.end()) {
          int n = 0;
          for (int t : region_tiles[size_t(s2)]) {
            n += !g.blocked[t] && !P[0].pinned_tile[size_t(t)] && (typical < 0 || box_fits(d, g, boxes.boxes[size_t(typical)], t));
          }
          it = fits_in.emplace(s2, n).first;
        }
        return it->second;
      };
      const auto room_of = [&](int s2) {
        const double fill = double(boxes_in[size_t(r)] + boxes_in[size_t(s2)]) / std::max(1, fit_tiles[size_t(r)] + fit_count(s2));
        return int(std::floor(std::min(0.9, fill) * double(fit_count(s2)))) - boxes_in[size_t(s2)];
      };
      std::vector<Spill_target> to;
      int                       room_sum = 0;
      const auto                add_ring = [&](const std::set<int>& ring) {
        for (int s2 : ring) {
          const int room = room_of(s2);
          if (s2 == r || room <= 0 || std::ranges::any_of(to, [&](const auto& x2) { return x2.region == s2; })) {
            continue;
          }
          to.push_back({s2, room});
          room_sum += room;
        }
      };
      add_ring(region_adj[size_t(r)]);
      if (size_t(room_sum) < want) {  // the neighbours are short of room: their neighbours too
        std::set<int> ring2;
        for (int s2 : region_adj[size_t(r)]) {
          for (int s3 : region_adj[size_t(s2)]) {
            if (!region_adj[size_t(r)].contains(s3)) {
              ring2.insert(s3);
            }
          }
        }
        add_ring(ring2);
      }
      if (to.empty()) {
        std::string around;
        for (int s2 : region_adj[size_t(r)]) {
          around += std::format(" {} (fill {:.2f}, room {})", s2, double(boxes_in[size_t(s2)]) / std::max(1, fit_tiles[size_t(s2)]), room_of(s2));
        }
        std::fprintf(stderr, "legroute sat: spill round %u: region %d (%zu boxes, fill %.2f) has no region around it with room:%s\n",
                     round + 1, r, region_boxes[size_t(r)].size(), double(boxes_in[size_t(r)]) / std::max(1, fit_tiles[size_t(r)]),
                     around.c_str());
        continue;
      }
      const auto         picks = spill_cut(d, boxes, x.ends, cand, r, to, want);
      std::map<int, int> per_target;
      for (const auto& [b, s2] : picks) {
        auto&  bx     = boxes.boxes[size_t(b)];
        int    best_t = -1;
        double best_d = INFINITY;
        for (int t : region_tiles[size_t(s2)]) {
          if (g.blocked[t] || taken.contains(t) || P[0].pinned_tile[size_t(t)] || !box_fits(d, g, bx, t)) {
            continue;
          }
          const double dd = std::abs(double(g.cx(g.col(t))) - bx.cx) + std::abs(double(g.cy(g.row(t))) - bx.cy);
          if (dd < best_d) {
            best_d = dd;
            best_t = t;
          }
        }
        if (best_t < 0) {
          continue;
        }
        taken.insert(best_t);
        bx.region = s2;
        bx.tile   = best_t;
        bx.cx     = double(g.cx(g.col(best_t)));
        bx.cy     = double(g.cy(g.row(best_t)));
        ++boxes_in[size_t(s2)];
        --boxes_in[size_t(r)];
        ++per_target[s2];
        ++moved;
      }
      std::string where;
      for (const auto& tg : to) {
        where += std::format(" {} -> region {} (room {})", per_target.contains(tg.region) ? per_target[tg.region] : 0, tg.region, tg.room);
      }
      std::fprintf(stderr, "legroute sat: spill round %u: region %d (%zu boxes, %zu wanted) pushes%s\n", round + 1, r,
                   region_boxes[size_t(r)].size(), want, where.c_str());
    }
    rep.windows_grown += grown;
    if (moved == 0 && grown == 0) {
      break;
    }
    // a receiver that cannot hold what it took (the room is an estimate): its extra boxes go to the nearest region
    // with a fitting tile, as before the first round
    if (const size_t bounced = moved && !d.opt.spill_shift ? balance_regions(d, g, boxes) : 0; bounced > 0) {
      std::fprintf(stderr, "legroute sat: spill round %u: %zu boxes bounced to the nearest region with a fitting tile\n", round + 1,
                   bounced);
    }
    ++rep.spill_rounds;
    rep.spilled_boxes += moved;
  }
  for (int r = 0; r < g.regions; ++r) {  // a region the spill rounds left with more boxes than tiles: the usual error
    if (!last[size_t(chosen[r])][size_t(r)].unfit.empty()) {
      throw Legroute_unfit(last[size_t(chosen[r])][size_t(r)].unfit);
    }
  }
  // ---- the chosen answers: tiles, counters, connections
  rep.sat_unjoined_crossings += x.unjoined;
  rep.sat_hops_over_share    += x.over_share;
  rep.global_nets            += x.global_nets;
  rep.global_pieces          += x.pieces.size();
  rep.global_unrouted        += x.global_unrouted;
  rep.global_full_edges      += x.global_full;
  rep.global_seconds         += x.global_seconds;
  {  // pre-placement counters: the first policy's
    const auto& q              = P[0].rep;
    rep.crit_pinned           += q.crit_pinned;
    rep.crit_unpinned_regions += q.crit_unpinned_regions;
    rep.crit_cost_before      += q.crit_cost_before;
    rep.crit_cost_after       += q.crit_cost_after;
    rep.tree_nets             += q.tree_nets;
    rep.tree_boxes            += q.tree_boxes;
    rep.tree_skipped_nets     += q.tree_skipped_nets;
    rep.tree_dropped_regions  += q.tree_dropped_regions;
    rep.tree_tiles_over_half  += q.tree_tiles_over_half;
    rep.tree_um_before        += q.tree_um_before;
    rep.tree_um_after         += q.tree_um_after;
    rep.tree_max_load          = std::max(rep.tree_max_load, q.tree_max_load);
    rep.forced_trees_dropped  += q.forced_trees_dropped;
    rep.forced_crit_unpinned  += q.forced_crit_unpinned;
    rep.tree_load_skipped     += q.tree_load_skipped;
    rep.tree_clauses_before   += q.tree_clauses_before;
    rep.tree_clauses_after    += q.tree_clauses_after;
    rep.io_feedthroughs       += q.io_feedthroughs;
    rep.io_boxes              += q.io_boxes;
    rep.io_skipped            += q.io_skipped;
  }

  R.trees.assign(d.nets.size(), {});
  R.local.assign(d.nets.size(), 0);
  std::vector<std::map<int, int>> node_at(d.nets.size());  // per net: tile -> node
  const auto                      node = [&](int n, int tile) {
    auto [it, fresh] = node_at[n].try_emplace(tile, int(R.trees[n].nodes.size()));
    if (fresh) {
      R.trees[n].nodes.push_back({});
      R.trees[n].nodes.back().tile = tile;
    }
    return it->second;
  };
  const auto add_conn = [&](int n, int ta, int tb, bool h_first, int region) {
    Conn c;
    c.net      = n;
    c.a        = node(n, ta);
    c.b        = node(n, tb);
    c.region   = region;
    c.weight   = d.nets[n].weight;
    c.critical = d.nets[n].critical;
    c.poly.emplace_back(g.col(ta), g.row(ta));
    const std::pair<int, int> corner = h_first ? std::pair{g.col(tb), g.row(ta)} : std::pair{g.col(ta), g.row(tb)};
    if (corner != c.poly.back() && corner != std::pair{g.col(tb), g.row(tb)}) {
      c.poly.push_back(corner);
    }
    if (std::pair{g.col(tb), g.row(tb)} != c.poly.back()) {
      c.poly.emplace_back(g.col(tb), g.row(tb));
    }
    if (c.poly.size() < 2) {
      return;
    }
    R.conns.push_back(std::move(c));
    R.trees[n].conns.push_back(int(R.conns.size()) - 1);
  };
  for (int r = 0; r < g.regions; ++r) {
    const auto& out = last[chosen[r]][r];
    for (const auto& [b, t] : out.tiles) {
      boxes.boxes[b].tile  = t;
      boxes.boxes[b].fixed = P[chosen[r]].boxes.boxes[b].fixed;
    }
    for (const auto& c : out.conns) {
      add_conn(c.net, c.ta, c.tb, c.h_first, r);
    }
    for (int n : out.local) {
      R.local[n] = 1;
    }
    const auto& q            = out.rep;
    rep.sat_regions         += q.sat_regions;
    rep.sat_variables       += q.sat_variables;
    rep.sat_clauses         += q.sat_clauses;
    rep.sat_solves          += q.sat_solves;
    rep.sat_binding_tiles   += q.sat_binding_tiles;
    rep.sat_relaxed_windows += q.sat_relaxed_windows;
    rep.sat_relaxed_tiles   += q.sat_relaxed_tiles;
    rep.sat_deficit_tracks  += q.sat_deficit_tracks;
    rep.sat_unsat_regions   += q.sat_unsat_regions;
    rep.sat_cut_regions     += q.sat_cut_regions;
    rep.sat_unknown_regions += q.sat_unknown_regions;
    rep.sat_skipped_regions += q.sat_skipped_regions;
    rep.sat_fail_fast_skipped += q.sat_fail_fast_skipped;
    rep.lazy_rounds         += q.lazy_rounds;
    rep.lazy_bounds         += q.lazy_bounds;
    rep.lazy_skipped        += q.lazy_skipped;
    rep.frozen_boxes        += q.frozen_boxes;
    rep.sat_seeded          += q.sat_seeded;
    rep.tier_kept           += q.tier_kept;
    rep.tier_dropped        += q.tier_dropped;
    rep.tier_timeouts       += q.tier_timeouts;
    rep.tier_solves         += q.tier_solves;
    rep.tier_cost_before    += q.tier_cost_before;
    rep.tier_cost_after     += q.tier_cost_after;
    rep.tier_crit_before    += q.tier_crit_before;
    rep.tier_crit_after     += q.tier_crit_after;
    {  // per region: its answer, and the cells of a region without a routable answer
      Legroute_report::Region_stat st;
      st.status = out.skipped ? 3 : out.status == Sat_status::satisfiable ? 0 : out.status == Sat_status::unsatisfiable ? 1 : 2;
      for (const auto& b : boxes.boxes) {
        st.boxes += b.region == r;
      }
      for (size_t t = 0; t < g.size(); ++t) {
        st.tiles += g.region[t] == r && !g.blocked[t];
      }
      rep.region_stats.push_back(st);
      if (st.status == 1 || st.status == 2) {
        for (const auto& b : boxes.boxes) {
          if (b.region == r) {
            for (int c : b.cells) {
              rep.unroutable_components.push_back(size_t(d.movable[c]));
              rep.unroutable_regions.push_back(r);
            }
          }
        }
      }
    }
    rep.sat_adjacent_nets += q.sat_adjacent_nets;
    rep.io_lifted         += q.io_lifted;
    double wall            = 0;  // the policies ran side by side
    for (const auto& per : last) {
      wall = std::max(wall, per[r].seconds);
    }
    rep.sat_seconds     += wall;
    rep.sat_seconds_max  = std::max(rep.sat_seconds_max, wall);
    if (chosen[r] > 0) {
      ++rep.sat_alt_regions;
    }
    if (std::ranges::all_of(last, [&](const auto& per) { return per[r].status == Sat_status::satisfiable; })) {
      ++rep.sat_both_regions;
    }
  }
  // the hops between regions
  for (size_t k = 0; k < x.hops.size(); ++k) {
    add_conn(x.hop_net[k], x.hops[k].first, x.hops[k].second, true, g.region[x.hops[k].first]);
  }
  if (!d.opt.output_dir.empty()) {  // legroute_tiles.json: the model's per-tile tracks and in-box charges (calibration)
    std::ofstream out(d.opt.output_dir + "/legroute_tiles.json");
    out << R"({"tile_dbu":[)" << g.tile_w << ',' << g.tile_h << R"(],"origin_dbu":[)" << g.x0 << ',' << g.y0 << R"(],"cols":)"
        << g.cols << R"(,"rows":)" << g.rows
        << R"(,"fields":["tile","col","row","region","blocked","cap_h0","cap_v0","cap_h","cap_v","cap_h_region","cap_v_region","box","dem_h","dem_v"],"tiles":[)";
    std::vector<int> occupant(g.size(), -1);
    for (size_t b = 0; b < boxes.boxes.size(); ++b) {
      if (boxes.boxes[b].tile >= 0) {
        occupant[size_t(boxes.boxes[b].tile)] = int(b);
      }
    }
    for (size_t t = 0; t < g.size(); ++t) {
      const int   b  = occupant[t];
      const auto& Pc = P[size_t(chosen[size_t(g.region[t])])];
      out << (t ? "," : "") << '[' << t << ',' << g.col(int(t)) << ',' << g.row(int(t)) << ',' << g.region[t] << ','
          << int(g.blocked[t]) << ',' << cap_h0[t] << ',' << cap_v0[t] << ',' << cap_h[t] << ',' << cap_v[t] << ',' << Pc.cap_h[t]
          << ',' << Pc.cap_v[t] << ',' << b << ',' << (b >= 0 ? dem_h[size_t(b)] : 0) << ',' << (b >= 0 ? dem_v[size_t(b)] : 0)
          << ']';
    }
    out << "]}\n";
  }
  // box and pin bookkeeping on the nodes
  for (size_t n = 0; n < d.nets.size(); ++n) {
    const auto& N = d.nets[n];
    if (R.trees[n].nodes.empty()) {
      continue;
    }
    for (size_t k = 0; k < N.pins.size(); ++k) {
      const auto& p   = N.pins[k];
      const int   box = box_of_comp(p.comp);
      int         t;
      if (box >= 0) {
        t = boxes.boxes[box].tile;
      } else if (p.comp >= 0) {
        t = g.tile_of(double(d.fp.components[p.comp].place.x), double(d.fp.components[p.comp].place.y));
      } else if (p.term >= 0) {
        t = g.tile_of(double(d.fp.terminals[p.term].x), double(d.fp.terminals[p.term].y));
      } else {
        continue;
      }
      const int nd = node(int(n), t);
      auto&     ND = R.trees[n].nodes[nd];
      ND.pins.push_back(int(k));
      if (box >= 0) {
        ND.box = box;
      } else if (p.term >= 0) {
        ND.term = p.term;
      }
    }
  }
  for (const auto& c : R.conns) {
    rep.critical_connections += c.critical;
  }
}
}  // namespace livehd::placement::lr
