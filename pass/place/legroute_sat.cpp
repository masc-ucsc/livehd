// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// legroute.local placement: per region, a SAT model places every cell-box on a free tile so that the box-to-box
// routes fit the tracks by construction. Each connection is straight or one of its two L-shapes (one bit); a leg
// covers every tile from end to end in its row or column; a tile counts a net once per direction however many of
// its legs cover it; and per tile and direction the nets plus the occupant box's in-box demand stay within the
// summed tracks of that direction. Any answer is routable. Each box stays in a window around its placement
// position (window_tiles), which keeps the model linear in the region size.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <format>
#include <functional>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <unordered_map>

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
      throw std::runtime_error(std::format("legroute: no free tile fits box {}", b));
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
  uint64_t hash = 1469598103934665603ull;  // FNV-1a over the clauses added (model identity across rounds)
  void     mix(int l) { hash = (hash ^ uint64_t(uint32_t(l))) * 1099511628211ull; }
  int      var() {
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
    for (size_t i = 0; i < n; ++i) {
      mix(buf[i]);
    }
    mix(0);
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
    for (int l : c) {
      mix(l);
    }
    mix(0);
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
  // tree_room: a move first keeps the most tracks free on the tiles it touches (the worst used share of the box's tile
  // and its rib, then their sum), the length only breaks ties
  const double                  room_weight = 1000 * (tw + th);
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
  // the used share of a tile's tracks in one direction with extra more tracks (trees, fixed demand)
  const auto share = [&](int t, bool h, int extra) {
    return double((h ? load_h : load_v)[t] + (h ? fixed_h : fixed_v)[t] + extra) / double(std::max(1, (h ? cap_h : cap_v)[t]));
  };
  // tree_room cost of a whole tree: the worst and the summed used share over its trunk, ribs and new boxes' tiles
  const auto room = [&](const std::vector<int>& tiles, const std::vector<int>& movers, bool h, int axis) {
    std::map<std::pair<int, int>, int> extra;
    int                                lo = INT32_MAX, hi = INT32_MIN;
    for (int t : tiles) {
      lo              = std::min(lo, h ? g.col(t) : g.row(t));
      hi              = std::max(hi, h ? g.col(t) : g.row(t));
      const int along = h ? g.col(t) : g.row(t), across = h ? g.row(t) : g.col(t);
      for (int k = std::min(across, axis); across != axis && k <= std::max(across, axis); ++k) {
        extra[{h ? g.tile(along, k) : g.tile(k, along), h ? 0 : 1}] = 1;
      }
    }
    for (int k = lo; k <= hi; ++k) {
      extra[{h ? g.tile(k, axis) : g.tile(axis, k), h ? 1 : 0}] = 1;
    }
    for (size_t i = 0; i < tiles.size(); ++i) {
      if (movers[i] >= 0) {
        extra[{tiles[i], 1}] += dem_h[movers[i]];
        extra[{tiles[i], 0}] += dem_v[movers[i]];
      }
    }
    double worst = 0, sum = 0;
    for (const auto& [key, v] : extra) {
      const double u  = share(key.first, key.second, v);
      worst           = std::max(worst, u);
      sum            += u;
    }
    return room_weight * (worst + .1 * sum);
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
              double    worst = std::max(share(t, true, dem_h[list[i].box]), share(t, false, dem_v[list[i].box])), sum = worst;
              for (int k = std::min(across, axis); k <= std::max(across, axis); ++k) {  // the rib's tiles
                const int q  = h ? g.tile(g.col(t), k) : g.tile(k, g.row(t));
                c           += overlap * (h ? th : tw) * (h ? load_v : load_h)[q] / std::max(1, (h ? cap_v : cap_h)[q]);
                if (cong > 0 && across != axis) {
                  c += cong * (h ? th : tw) * (h ? psi_v : psi_h)[size_t(q)];
                }
                if (o.tree_room && across != axis) {
                  worst  = std::max(worst, share(q, !h, 1));
                  sum   += share(q, !h, 1);
                }
              }
              if (o.tree_room) {
                c += room_weight * (worst + .1 * sum);
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
        if (o.tree_room) {
          std::vector<int> movers(list.size(), -1);
          for (size_t i = 0; i < list.size(); ++i) {
            movers[i] = fixed[i] < 0 ? list[i].box : -1;
          }
          cost += room(tiles, movers, h, axis);
        }
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
      trees.resize(trees_mark);
      std::erase_if(treed, [&](const auto& nr) { return nr.second == r; });
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
  const auto room = [&](const std::vector<std::pair<int, int>>& path, int r) {  // load share, or INFINITY
    double cost = 0;
    for (const auto& [t, h] : path) {
      const int c = (h ? cap_h : cap_v)[t];
      if (g.region[t] != r || c < 1) {
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
// One region's SAT placement under one policy's options and pre-placement (boxes, tracks left, nets routed ahead,
// boxes held to a track): the boxes' tiles land in boxes, the connections, adjacent nets and counters in out. A
// model identical to previous's (same clauses, a definite answer) takes its answer without solving.
struct Region_out {
  struct Piece {
    int  net = -1, ta = -1, tb = -1;
    bool h_first = true;
  };
  Sat_status                       status = Sat_status::unknown;
  uint64_t                         hash   = 0;
  bool                             reused = false, skipped = false;
  std::vector<Piece>               conns;  // the SAT's connections, then the policy's trees and IO pre-routes here
  std::vector<int>                 local;  // 1-1 nets between neighbor boxes
  std::vector<std::pair<int, int>> tiles;  // (box, tile)
  Legroute_report                  rep;    // the sat_* counters
  double                           cost = 0, seconds = 0;
};
void solve_region(const Design& d, const Legroute_options& o, const Grid& g, int r, const std::vector<int>& B,
                  const std::vector<int>& T, Boxes& boxes, const std::vector<int>& cap_h, const std::vector<int>& cap_v,
                  const std::vector<int>& dem_h, const std::vector<int>& dem_v, const std::vector<Net_ends>& ends,
                  const std::set<std::pair<int, int>>& treed, std::unordered_map<int, std::vector<int>>& allowed,
                  const std::vector<char>& pinned_tile, const Region_out* previous, const std::string& tag, Region_out& out) {
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
  const auto col_ge = [&](const Endpoint& e, int k) {
    if (e.box < 0) {
      return g.col(e.tile) >= k ? cnf.t : -cnf.t;
    }
    return k <= c0 ? cnf.t : k > c1 ? -cnf.t : X[slot.at(e.box)][k - c0 - 1];
  };
  const auto row_ge = [&](const Endpoint& e, int k) {
    if (e.box < 0) {
      return g.row(e.tile) >= k ? cnf.t : -cnf.t;
    }
    return k <= r0 ? cnf.t : k > r1 ? -cnf.t : Y[slot.at(e.box)][k - r0 - 1];
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
    for (int w = int(o.window_tiles); !box.fixed && al == allowed.end(); ++w) {
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
  const auto on_track = [&](int b, int t) {
    const auto it = allowed.find(b);
    return it == allowed.end() || std::ranges::find(it->second, t) != it->second.end();
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
      if (allowed.erase(B[i])) {  // off its IO track: the track stays reserved, the box is free again
        ++rep.io_lifted;
      }
      win[i] = {std::max(c0, win[i].c0 - 1), std::min(c1, win[i].c1 + 1), std::max(r0, win[i].r0 - 1), std::min(r1, win[i].r1 + 1)};
      ++rep.sat_relaxed_windows;
    }
  }
  for (size_t i = 0; i < B.size(); ++i) {
    const Endpoint e{B[i], -1};
    cnf.add({col_ge(e, win[i].c0)});
    cnf.add({-col_ge(e, win[i].c1 + 1)});
    cnf.add({row_ge(e, win[i].r0)});
    cnf.add({-row_ge(e, win[i].r1 + 1)});
  }
  const auto win_of = [&](const Endpoint& e) {
    return e.box >= 0 ? win[slot.at(e.box)] : Win{g.col(e.tile), g.col(e.tile), g.row(e.tile), g.row(e.tile)};
  };
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
      throw std::runtime_error(std::format("legroute: box {} fits no tile of region {}", B[i], r));
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
  const size_t             cl_place = cnf.clauses;
  // connections: from the net's first endpoint in the region (its driver when here) to each other endpoint
  std::vector<Endpoint>    eps;
  std::vector<Region_conn> conns;
  std::vector<int>         nets_here;
  for (size_t n = 0; n < d.nets.size(); ++n) {
    const auto it = ends[n].per_region.find(r);
    if (it == ends[n].per_region.end() || it->second.size() < 2 || treed.contains({int(n), r})) {
      continue;
    }
    nets_here.push_back(int(n));
    const int first = int(eps.size());
    eps.insert(eps.end(), it->second.begin(), it->second.end());
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
  std::vector<int> orient(conns.size()), adjacent(conns.size(), 0);
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
  // capacity per tile and direction: covering nets plus the occupant's in-box demand (unary)
  std::map<std::pair<int, int>, std::vector<int>> inputs;  // (tile, h) -> literals
  for (const auto& [key, lit] : cover) {
    inputs[{std::get<1>(key), std::get<2>(key)}].push_back(lit);
  }
  size_t       binding = 0;
  const size_t cl_conn = cnf.clauses;
  size_t       cap_in = 0, cap_k = 0, cap_n = 0, cap_est = 0;
  struct Bound {
    int t, h, k, over;
  };
  std::vector<Bound> bounds;  // with sat_relax_rounds: each tile's bound is an assumption
  for (int t : T) {
    for (int h : {1, 0}) {
      auto&      xs  = inputs[{t, h}];
      const auto at  = at_tile.find(t);
      int        top = 0;
      if (at != at_tile.end()) {
        for (const auto& [i, _] : at->second) {
          top = std::max(top, (h ? dem_h : dem_v)[B[i]]);
        }
        for (int v = 1; v <= top; ++v) {
          const int u = cnf.var();
          for (const auto& [i, p] : at->second) {
            if ((h ? dem_h : dem_v)[B[i]] >= v) {
              cnf.add({-p, u});
            }
          }
          xs.push_back(u);
        }
      }
      const int k  = std::max(0, int(std::floor((h ? cap_h : cap_v)[t] * o.cap_scale)));
      binding     += int(xs.size()) > k;
      cap_in      += xs.size();
      cap_k       += k;
      ++cap_n;
      cap_est += at_most_clauses(int(xs.size()), k);
      if (!o.sat_relax_rounds) {
        cnf.at_most(xs, k);
      } else if (const int ov = cnf.over(xs, k)) {
        bounds.push_back({t, h, k, ov});
      }
    }
  }
  {  // diagnostic: clauses of the axis + span limit + top pair scheme (x = 8 tiles, super-tiles of 8 x 8)
    constexpr int x = 8, S = 8;
    int           top_h = -1, top_v = -1;
    for (int l = 0; l < int(g.layers.size()); ++l) {
      (g.layers[l].horizontal ? top_h : top_v) = l;
    }
    std::map<std::pair<int, int>, int>       fine_in;  // (tile, h) -> nets that may cover it
    std::map<std::tuple<int, int, int>, int> top_in;   // (super col, super row, h) -> long nets
    std::vector<int>                         long_pins(B.size(), 0);
    size_t                                   conn_new = 0, n_short = 0, n_long = 0;
    const auto                               home = [&](const Endpoint& e) {
      if (e.box < 0) {
        return std::pair{g.col(e.tile), g.row(e.tile)};
      }
      const auto& box = boxes.boxes[e.box];
      return std::pair{std::clamp(g.col_of(box.cx), c0, c1), std::clamp(g.row_of(box.cy), r0, r1)};
    };
    for (int n : nets_here) {
      const auto& list = ends[n].per_region.at(r);
      int         lc = g.cols, hc = -1, lr = g.rows, hr = -1;
      int         wc0 = g.cols, wc1 = -1, wr0 = g.rows, wr1 = -1, mc0 = -1, mc1 = g.cols, mr0 = -1, mr1 = g.rows;
      for (const auto& e : list) {
        const auto [c, rr] = home(e);
        lc                 = std::min(lc, c);
        hc                 = std::max(hc, c);
        lr                 = std::min(lr, rr);
        hr                 = std::max(hr, rr);
        const auto w       = win_of(e);
        wc0                = std::min(wc0, w.c0);
        wc1                = std::max(wc1, w.c1);
        wr0                = std::min(wr0, w.r0);
        wr1                = std::max(wr1, w.r1);
        mc0                = std::max(mc0, w.c0);
        mc1                = std::min(mc1, w.c1);
        mr0                = std::max(mr0, w.r0);
        mr1                = std::min(mr1, w.r1);
      }
      if (hc - lc > x || hr - lr > x) {  // long: the top pair over super-tiles
        ++n_long;
        for (int qc = wc0 / S; qc <= wc1 / S; ++qc) {
          for (int qr = wr0 / S; qr <= wr1 / S; ++qr) {
            ++top_in[{qc, qr, 1}];
            ++top_in[{qc, qr, 0}];
            conn_new += 2;
          }
        }
        conn_new += 10 * list.size() * size_t(wc1 / S - wc0 / S + wr1 / S - wr0 / S + 2);
        for (const auto& e : list) {
          if (e.box >= 0) {
            ++long_pins[slot.at(e.box)];
          }
        }
        continue;
      }
      ++n_short;
      // the axes stay within x of every member: R in [max r0 - x, min r1 + x] within the hull
      const int Rlo = std::max(wr0, mr0 - x), Rhi = std::min(wr1, mr1 + x);
      const int Clo = std::max(wc0, mc0 - x), Chi = std::min(wc1, mc1 + x);
      for (int h : {1, 0}) {
        for (int j = Rlo; j <= Rhi; ++j) {
          for (int i = Clo; i <= Chi; ++i) {
            // s = 0: the H trunk lies on a member's row, the V trunk on a member's column
            const int  axis = h ? j : i;
            const bool on   = std::ranges::any_of(list, [&](const Endpoint& e) {
              const auto w = win_of(e);
              return h ? w.r0 <= axis && axis <= w.r1 : w.c0 <= axis && axis <= w.c1;
            });
            const int  t    = g.tile(i, j);
            if (on && g.region[t] == r) {
              ++fine_in[{t, h}];
              ++conn_new;
            }
          }
        }
      }
      conn_new += size_t(2 + 6 * list.size()) * size_t(std::max(0, Rhi - Rlo + 1) + std::max(0, Chi - Clo + 1));
    }
    size_t fine = 0, top = 0, fine_inputs = 0, fine_bound = 0, fine_rows = 0;
    for (int t : T) {
      for (int h : {1, 0}) {
        int        n_in = fine_in[{t, h}];
        const auto at   = at_tile.find(t);
        if (at != at_tile.end()) {
          int dem = 0, lp = 0;
          for (const auto& [i, _] : at->second) {
            dem = std::max(dem, (h ? dem_h : dem_v)[B[i]]);
            lp  = std::max(lp, long_pins[i]);
          }
          n_in += dem + lp;
        }
        const int top_l = h ? top_h : top_v;
        const int k     = std::max(0, int(std::floor(((h ? cap_h : cap_v)[t] - (top_l >= 0 ? g.cap[top_l][t] : 0)) * o.cap_scale)));
        fine        += at_most_clauses(n_in, k);
        fine_inputs += n_in;
        fine_bound  += k;
        ++fine_rows;
      }
    }
    std::map<std::tuple<int, int, int>, double> top_cap;
    for (int t : T) {
      for (int h : {1, 0}) {
        const int top_l = h ? top_h : top_v;
        if (top_l >= 0) {
          top_cap[{g.col(t) / S, g.row(t) / S, h}] += g.cap[top_l][t] / double(S);
        }
      }
    }
    for (const auto& [key, n_in] : top_in) {
      top += at_most_clauses(n_in, int(std::floor(top_cap[key] * o.cap_scale)));
    }
    std::fprintf(stderr,
                 "legroute estimate region %d: now placement %zu connections %zu capacity %zu (recount %zu) total %zu | "
                 "new short %zu long %zu placement %zu connections %zu fine %zu (inputs %.1f bound %.1f) top %zu total %zu\n",
                 r,
                 cl_place,
                 cl_conn - cl_place,
                 cnf.clauses - cl_conn,
                 cap_est,
                 cnf.clauses,
                 n_short,
                 n_long,
                 cl_place,
                 conn_new,
                 fine,
                 double(fine_inputs) / double(std::max<size_t>(1, fine_rows)),
                 double(fine_bound) / double(std::max<size_t>(1, fine_rows)),
                 top,
                 cl_place + conn_new + fine + top);
  }
  std::fprintf(stderr,
               "legroute sat region %d model: clauses connections %zu capacity %zu, per capacity row inputs %.1f bound %.1f\n",
               r,
               cl_conn,
               cnf.clauses - cl_conn,
               double(cap_in) / double(std::max<size_t>(1, cap_n)),
               double(cap_k) / double(std::max<size_t>(1, cap_n)));
  {  // diagnostic: a lower bound of the region's demand per direction against its tracks: each box's in-box demand,
    // and per net its legs at least across the gap between its endpoints' windows (a leg covers its end tiles too)
    std::array<size_t, 2> inbox{}, wires{}, tracks{};
    for (int b : B) {
      inbox[1] += size_t(dem_h[b]);
      inbox[0] += size_t(dem_v[b]);
    }
    for (int n : nets_here) {
      const auto& list = ends[n].per_region.at(r);
      int         lo_c = INT32_MIN, hi_c = INT32_MAX, lo_r = INT32_MIN, hi_r = INT32_MAX;
      for (const auto& e : list) {
        const auto w = win_of(e);
        lo_c         = std::max(lo_c, w.c0);
        hi_c         = std::min(hi_c, w.c1);
        lo_r         = std::max(lo_r, w.r0);
        hi_r         = std::min(hi_r, w.r1);
      }
      const int  gap_c = std::max(0, lo_c - hi_c), gap_r = std::max(0, lo_r - hi_r);
      const bool lone
          = list.size() == 2 && list[0].box >= 0 && list[1].box >= 0 && d.nets[n].pins.size() == 2 && gap_c + gap_r <= 1;
      if (!lone) {
        wires[1] += gap_c ? size_t(gap_c + 1) : 0;
        wires[0] += gap_r ? size_t(gap_r + 1) : 0;
      }
    }
    for (int t : T) {
      for (int h : {1, 0}) {
        tracks[h] += size_t(std::max(0, int(std::floor((h ? cap_h : cap_v)[t] * o.cap_scale))));
      }
    }
    std::fprintf(stderr,
                 "legroute sat region %d bound: H >= %zu (in-box %zu, wires %zu) of %zu tracks (%.0f%%), V >= %zu (in-box %zu, "
                 "wires %zu) of %zu (%.0f%%)\n",
                 r,
                 inbox[1] + wires[1],
                 inbox[1],
                 wires[1],
                 tracks[1],
                 100.0 * double(inbox[1] + wires[1]) / double(std::max<size_t>(1, tracks[1])),
                 inbox[0] + wires[0],
                 inbox[0],
                 wires[0],
                 tracks[0],
                 100.0 * double(inbox[0] + wires[0]) / double(std::max<size_t>(1, tracks[0])));
  }
  {  // diagnostic: demand no placement can avoid, per tile and direction: a fixed occupant's in-box demand plus the
    // straight connections between two fixed endpoints, against the tracks
    const auto fixed_tile = [&](const Endpoint& e) {
      return e.box < 0 ? e.tile : boxes.boxes[e.box].fixed ? boxes.boxes[e.box].tile : -1;
    };
    std::map<std::pair<int, int>, int> forced;  // (tile, h) -> tracks
    for (int b : B) {
      if (boxes.boxes[b].fixed) {
        forced[{boxes.boxes[b].tile, 1}] += dem_h[b];
        forced[{boxes.boxes[b].tile, 0}] += dem_v[b];
      }
    }
    size_t straight = 0;
    for (const auto& cn : conns) {
      const int ta = fixed_tile(eps[cn.a]), tb = fixed_tile(eps[cn.b]);
      if (ta < 0 || tb < 0 || (g.row(ta) != g.row(tb) && g.col(ta) != g.col(tb)) || ta == tb) {
        continue;
      }
      if (eps[cn.a].box >= 0 && eps[cn.b].box >= 0 && d.nets[cn.net].pins.size() == 2
          && std::abs(g.col(ta) - g.col(tb)) + std::abs(g.row(ta) - g.row(tb)) == 1) {
        continue;  // neighbors: in-box metal
      }
      ++straight;
      const bool h = g.row(ta) == g.row(tb);
      for (int i = std::min(g.col(ta), g.col(tb)); i <= std::max(g.col(ta), g.col(tb)); ++i) {
        for (int j = std::min(g.row(ta), g.row(tb)); j <= std::max(g.row(ta), g.row(tb)); ++j) {
          if (g.region[g.tile(i, j)] == r) {
            ++forced[{g.tile(i, j), h}];
          }
        }
      }
    }
    std::array<size_t, 2> over{}, deficit{}, empty{};
    for (const auto& [key, v] : forced) {
      const int k = std::max(0, int(std::floor((key.second ? cap_h : cap_v)[key.first] * o.cap_scale)));
      if (v > k) {
        ++over[key.second];
        deficit[key.second] += size_t(v - k);
      }
    }
    for (int t : T) {
      for (int h : {1, 0}) {
        empty[h] += (h ? cap_h : cap_v)[t] <= 0 && !g.blocked[t];
      }
    }
    std::fprintf(stderr,
                 "legroute sat region %d forced: fixed boxes %zu, fixed straight conns %zu, tiles over H %zu (%zu tracks) V "
                 "%zu (%zu), no tracks left H %zu V %zu tiles\n",
                 r,
                 size_t(std::ranges::count_if(B, [&](int b) { return boxes.boxes[b].fixed; })),
                 straight,
                 over[1],
                 deficit[1],
                 over[0],
                 deficit[0],
                 empty[1],
                 empty[0]);
  }
  std::vector<int> assume;
  for (const auto& b : bounds) {
    assume.push_back(-b.over);
  }
  out.hash = cnf.hash;
  if (previous && previous->hash == cnf.hash) {  // the same model: its answer (an unknown would time out again)
    const auto hash = out.hash;
    out             = *previous;
    out.hash        = hash;
    out.reused      = true;
    for (const auto& [b, t] : out.tiles) {
      boxes.boxes[b].tile = t;
    }
    return;
  }
  // sat_min_fill: a region this empty is taken as routable without a solve (its boxes on their nearest tiles)
  const auto free_tiles  = std::ranges::count_if(T, [&](int t) { return !g.blocked[t]; });
  out.skipped            = o.sat_min_fill > 0 && double(B.size()) <= o.sat_min_fill * double(free_tiles);
  Sat_status status      = out.skipped ? Sat_status::unknown : cnf.solver.solve(assume, o.sat_conflicts, o.sat_ms);
  out.status             = status;
  rep.sat_solves        += !out.skipped;
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
  std::fprintf(stderr,
               "legroute sat region %d: boxes %zu tiles %zu nets %zu conns %zu vars %zu clauses %zu binding %zu solves %zu "
               "status %s %.2f s%s\n",
               r,
               B.size(),
               T.size(),
               nets_here.size(),
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
    // the region keeps its boxes on fitting tiles, nearest first (not routable by construction): a bipartite
    // matching (augmenting paths), so a box with few fitting tiles still gets one
    std::unordered_map<int, int>  owner;  // tile -> box index
    std::vector<std::vector<int>> pref(B.size());
    for (size_t i = 0; i < B.size(); ++i) {
      const auto& box = boxes.boxes[B[i]];
      for (const auto& [t, _] : tiles_of[i]) {
        pref[i].push_back(t);
      }
      std::ranges::sort(pref[i], [&](int x, int y) {
        return std::abs(double(g.cx(g.col(x))) - box.cx) + std::abs(double(g.cy(g.row(x))) - box.cy)
               < std::abs(double(g.cx(g.col(y))) - box.cx) + std::abs(double(g.cy(g.row(y))) - box.cy);
      });
    }
    std::vector<char>        seen;
    std::function<bool(int)> augment = [&](int i) {
      for (int t : pref[i]) {
        if (seen[t]) {
          continue;
        }
        seen[t]       = 1;
        const auto it = owner.find(t);
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
      // its window is full: any tile of the region it fits, nearest first
      const auto& box = boxes.boxes[B[i]];
      pref[i].clear();
      for (int t : T) {
        if (!g.blocked[t] && (box.fixed || !pinned_tile[t]) && box_fits(d, g, box, t)) {
          pref[i].push_back(t);
        }
      }
      std::ranges::sort(pref[i], [&](int x, int y) {
        return std::abs(double(g.cx(g.col(x))) - box.cx) + std::abs(double(g.cy(g.row(x))) - box.cy)
               < std::abs(double(g.cx(g.col(y))) - box.cx) + std::abs(double(g.cy(g.row(y))) - box.cy);
      });
      seen.assign(g.size(), 0);
      if (!augment(int(i))) {
        throw std::runtime_error(std::format("legroute: region {} has more boxes than fitting tiles", r));
      }
    }
    for (const auto& [t, i] : owner) {
      boxes.boxes[B[i]].tile = t;
    }
    for (int b : B) {
      out.tiles.emplace_back(b, boxes.boxes[b].tile);
    }
    return;
  }
  {  // diagnostic: where the tracks go in the answer, per direction: in-box demand, routes, against the tracks
    // the legs the answer draws (a cover literal may be true without a leg: only legs imply covers)
    std::vector<int> at_box(B.size(), -1);
    for (size_t i = 0; i < B.size(); ++i) {
      for (const auto& [t, p] : tiles_of[i]) {
        if (cnf.solver.value(p)) {
          at_box[i] = t;
        }
      }
    }
    const auto                          ep_tile = [&](const Endpoint& e) { return e.box >= 0 ? at_box[slot.at(e.box)] : e.tile; };
    std::set<std::tuple<int, int, int>> need;  // (net, tile, h)
    for (size_t k = 0; k < conns.size(); ++k) {
      if (adjacent[k] && cnf.solver.value(adjacent[k])) {
        continue;
      }
      const int  ta = ep_tile(eps[conns[k].a]), tb = ep_tile(eps[conns[k].b]);
      const bool hf = cnf.solver.value(orient[k]);
      const int  hr = hf ? g.row(ta) : g.row(tb), vc = hf ? g.col(tb) : g.col(ta);
      for (int i = std::min(g.col(ta), g.col(tb)); g.col(ta) != g.col(tb) && i <= std::max(g.col(ta), g.col(tb)); ++i) {
        need.emplace(conns[k].net, g.tile(i, hr), 1);
      }
      for (int j = std::min(g.row(ta), g.row(tb)); g.row(ta) != g.row(tb) && j <= std::max(g.row(ta), g.row(tb)); ++j) {
        need.emplace(conns[k].net, g.tile(vc, j), 0);
      }
    }
    std::map<std::pair<int, int>, size_t> routes;  // (tile, h) -> nets whose legs cross it
    for (const auto& [n, t, h] : need) {
      if (g.region[t] == r) {
        ++routes[{t, h}];
      }
    }
    std::array<size_t, 2> in_box{}, routed{}, tracks{}, tight{};
    for (int t : T) {
      int occupant = -1;
      if (const auto at = at_tile.find(t); at != at_tile.end()) {
        for (const auto& [i, p] : at->second) {
          if (cnf.solver.value(p)) {
            occupant = B[i];
          }
        }
      }
      for (int h : {1, 0}) {
        const size_t k    = size_t(std::max(0, int(std::floor((h ? cap_h : cap_v)[t] * o.cap_scale))));
        const size_t box  = occupant >= 0 ? size_t((h ? dem_h : dem_v)[occupant]) : 0;
        const size_t rt   = routes[{t, h}];
        in_box[h]        += box;
        routed[h]        += rt;
        tracks[h]        += k;
        tight[h]         += box + rt >= k;
      }
    }
    std::fprintf(stderr,
                 "legroute sat region %d use: H in-box %zu routes %zu of %zu tracks (tight %zu tiles), V in-box %zu routes %zu "
                 "of %zu (tight %zu)\n",
                 r,
                 in_box[1],
                 routed[1],
                 tracks[1],
                 tight[1],
                 in_box[0],
                 routed[0],
                 tracks[0],
                 tight[0]);
  }
  for (size_t i = 0; i < B.size(); ++i) {
    for (const auto& [t, p] : tiles_of[i]) {
      if (cnf.solver.value(p)) {
        boxes.boxes[B[i]].tile = t;
        break;
      }
    }
  }
  // the connections with their shapes
  for (size_t k = 0; k < conns.size(); ++k) {
    const auto& cn = conns[k];
    const auto  ta = eps[cn.a].box >= 0 ? boxes.boxes[eps[cn.a].box].tile : eps[cn.a].tile;
    const auto  tb = eps[cn.b].box >= 0 ? boxes.boxes[eps[cn.b].box].tile : eps[cn.b].tile;
    if (adjacent[k] && cnf.solver.value(adjacent[k])) {  // neighbor boxes: in-box metal, no lane
      out.local.push_back(cn.net);
      ++rep.sat_adjacent_nets;
      continue;
    }
    out.conns.push_back({cn.net, ta, tb, static_cast<bool>(cnf.solver.value(orient[k]))});
  }
  rep.sat_seconds += secs;
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
  size_t                           unjoined = 0;
};
Crossings crossings(const Design& d, const Grid& g, const Boxes& boxes, const Legroute_regions& regions, std::vector<int>& cap_h,
                    std::vector<int>& cap_v, bool diagnose) {
  Crossings  out;
  auto&      ends        = out.ends;
  auto&      hops        = out.hops;
  auto&      hop_net     = out.hop_net;
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
        hops.emplace_back(bu, bv);
        hop_net.push_back(int(n));
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
  for (const size_t n : order) {
    auto&              E = ends[n];
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
    const int root_region = E.driver_region >= 0 ? E.driver_region : E.per_region.begin()->first;
    for (bool progress = true; progress;) {
      progress = false;
      for (const auto& [r, _] : std::map<int, std::vector<Endpoint>>(E.per_region)) {
        if (find(r) == find(root_region)) {
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
        int hit = -1;
        for (size_t i = 0; i < frontier.size() && hit < 0; ++i) {
          for (int q : adj[frontier[i]]) {
            if (from.contains(q)) {
              continue;
            }
            from[q] = frontier[i];
            if (comp.contains(q) && find(q) == find(root_region)) {
              hit = q;
              break;
            }
            frontier.push_back(q);
          }
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
          const auto key = std::pair{std::min(a, b), std::max(a, b)};
          const auto ta  = E.per_region.contains(a) ? centroid(E.per_region.at(a)) : std::pair{0.0, 0.0};
          const auto tb  = E.per_region.contains(b) ? centroid(E.per_region.at(b)) : ta;
          const auto aim = E.per_region.contains(a) ? std::pair{(ta.first + tb.first) / 2, (ta.second + tb.second) / 2} : tb;
          int        bu = -1, bv = -1;
          double     best = INFINITY;
          for (auto [u, v] : edges.at(key)) {
            if (g.region[u] != a) {
              std::swap(u, v);
            }
            const int h = g.row(u) == g.row(v) ? 0 : 1;
            if (hop_use[u][h] * 2 >= (h ? cap_v : cap_h)[u] || hop_use[v][h] * 2 >= (h ? cap_v : cap_h)[v]) {
              continue;
            }
            const double mx = (g.cx(g.col(u)) + g.cx(g.col(v))) / 2.0, my = (g.cy(g.row(u)) + g.cy(g.row(v))) / 2.0;
            const double dd = std::abs(mx - aim.first) + std::abs(my - aim.second);
            if (dd < best) {
              best = dd;
              bu   = u;
              bv   = v;
            }
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
          hops.emplace_back(bu, bv);
          hop_net.push_back(int(n));
          unite(a, b);
          progress = true;
        }
      }
    }
  }
  for (size_t k = 0; k < hops.size(); ++k) {
    for (int t : {hops[k].first, hops[k].second}) {
      (g.row(hops[k].first) == g.row(hops[k].second) ? cap_h : cap_v)[t] -= 1;
    }
  }
  return out;
}

// Density push for regions without a routable answer: push_fraction of each such region's free boxes move to
// neighbor regions that have one and free tiles, the least critical boxes nearest to the neighbor first (crit-weighted
// distance in tiles). A box keeps its placement position, so its window lands on the shared boundary. Returns the
// boxes moved.
size_t push_boxes(const Grid& g, Boxes& boxes, const std::vector<int>& failed, double fraction) {
  std::vector<int>              room(g.regions, 0);  // free tiles less boxes
  std::vector<std::vector<int>> tiles(g.regions);
  std::vector<std::set<int>>    adj(g.regions);
  for (int t = 0; t < int(g.size()); ++t) {
    if (!g.blocked[t]) {
      ++room[g.region[t]];
      tiles[g.region[t]].push_back(t);
    }
    for (int q : {g.col(t) + 1 < g.cols ? t + 1 : -1, t + g.cols < int(g.size()) ? t + g.cols : -1}) {
      if (q >= 0 && g.region[q] != g.region[t]) {
        adj[g.region[t]].insert(g.region[q]);
        adj[g.region[q]].insert(g.region[t]);
      }
    }
  }
  for (const auto& b : boxes.boxes) {
    --room[b.region];
  }
  const std::set<int> bad(failed.begin(), failed.end());
  size_t              moved = 0;
  for (int r : failed) {
    std::vector<std::tuple<double, int, int>> cand;  // (score, box, region)
    size_t                                    free_boxes = 0;
    for (int b = 0; b < int(boxes.boxes.size()); ++b) {
      const auto& box = boxes.boxes[b];
      if (box.region != r || box.fixed) {
        continue;
      }
      ++free_boxes;
      for (int q : adj[r]) {
        if (bad.contains(q) || room[q] <= 0) {
          continue;
        }
        double dist = INFINITY;
        for (int t : tiles[q]) {
          dist = std::min(dist,
                          std::abs(double(g.cx(g.col(t))) - box.cx) / double(g.tile_w)
                              + std::abs(double(g.cy(g.row(t))) - box.cy) / double(g.tile_h));
        }
        cand.emplace_back(dist * (1 + 8 * box.crit * box.crit), b, q);
      }
    }
    std::ranges::sort(cand);
    size_t quota = std::max<size_t>(1, size_t(std::ceil(fraction * double(free_boxes))));
    for (const auto& [_, b, q] : cand) {
      if (!quota) {
        break;
      }
      if (boxes.boxes[b].region != r || room[q] <= 0) {
        continue;
      }
      boxes.boxes[b].region = q;
      --room[q];
      ++room[r];
      ++moved;
      --quota;
    }
  }
  return moved;
}
}  // namespace

void sat_place(const Design& d, const Grid& g, Boxes& boxes, const Legroute_regions& regions, Routing& R, Legroute_report& rep) {
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
    Legroute_report                           rep;  // pre-placement counters
  };
  std::vector<std::vector<Region_out>> last(policies.size(), std::vector<Region_out>(g.regions));
  std::vector<int>                     chosen(g.regions, 0);
  std::vector<Policy>                  P;
  Crossings                            x;
  // the round with the most routable regions (the earliest on a tie): a push can break the regions it fills
  struct Kept {
    Boxes                                boxes;
    std::vector<std::vector<Region_out>> last;
    std::vector<int>                     chosen;
    std::vector<Policy>                  P;
    Crossings                            x;
    size_t                               solved = 0;
    unsigned                             round  = 0;
  };
  std::optional<Kept> kept;
  for (unsigned round = 0;; ++round) {
    cap_h = cap_h0;
    cap_v = cap_v0;
    x     = crossings(d, g, boxes, regions, cap_h, cap_v, round == 0);
    P.assign(policies.size(), {});
    for (size_t p = 0; p < policies.size(); ++p) {  // the policy's pre-placement on its own copy
      const auto& o = policies[p];
      auto&       Q = P[p];
      Q.boxes       = boxes;
      Q.cap_h       = cap_h;
      Q.cap_v       = cap_v;
      if (o.crit_boxes > 0) {
        place_critical(d, o, g, Q.boxes, x.ends, Q.rep);
      }
      if (o.tree_boxes > 0 || o.tree_load > 0) {
        Q.trees = place_trees(d, o, g, Q.boxes, x.ends, dem_h, dem_v, Q.cap_h, Q.cap_v, Q.treed, Q.rep);
      }
      if (o.io_preroute) {
        Q.io_routes = io_preroute(d, o, g, Q.boxes, x.ends, Q.cap_h, Q.cap_v, Q.treed, Q.allowed, Q.rep);
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
    }
    // ---- one SAT model per region and policy (the policies in parallel)
    std::vector<std::vector<int>> region_boxes(g.regions), region_tiles(g.regions);
    for (int b = 0; b < int(boxes.boxes.size()); ++b) {
      region_boxes[boxes.boxes[b].region].push_back(b);
    }
    for (int t = 0; t < int(g.size()); ++t) {
      region_tiles[g.region[t]].push_back(t);
    }
    std::vector<int> failed;
    for (int r = 0; r < g.regions; ++r) {
      std::vector<Region_out> outs(policies.size());
      const auto              run = [&](size_t p) {
        auto&      Q     = P[p];
        auto&      out   = outs[p];
        const auto start = Clock::now();
        solve_region(d,
                     policies[p],
                     g,
                     r,
                     region_boxes[r],
                     region_tiles[r],
                     Q.boxes,
                     Q.cap_h,
                     Q.cap_v,
                     dem_h,
                     dem_v,
                     x.ends,
                     Q.treed,
                     Q.allowed,
                     Q.pinned_tile,
                     round ? &last[p][r] : nullptr,
                     policies.size() > 1 ? std::format(" policy {}{}", p, round ? std::format(" round {}", round) : "") : "",
                     out);
        out.seconds = seconds_since(start);
        if (out.reused) {
          return;
        }
        for (const auto& ft : Q.trees) {  // the policy's fixed trees and IO pre-routes in the region
          for (const auto& piece : ft.pieces) {
            if (!ft.dropped && piece.region == r) {
              out.conns.push_back({piece.net, piece.a, piece.b, true});
            }
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
      if (policies.size() == 1) {
        run(0);
      } else {  // an exception in a policy's thread comes back here
        std::vector<std::exception_ptr> errors(policies.size());
        std::vector<std::jthread>       threads;
        for (size_t p = 0; p < policies.size(); ++p) {
          threads.emplace_back([&, p] {
            try {
              run(p);
            } catch (...) {
              errors[p] = std::current_exception();
            }
          });
        }
        for (auto& t : threads) {
          t.join();
        }
        for (const auto& e : errors) {
          if (e) {
            std::rethrow_exception(e);
          }
        }
      }
      // a routable answer first, then the lower cost; no answer: the first policy's fallback
      int best = -1;
      for (int p = 0; p < int(outs.size()); ++p) {
        if (outs[p].status == Sat_status::satisfiable && (best < 0 || outs[p].cost < outs[best].cost)) {
          best = p;
        }
      }
      if (best < 0 && !outs[0].skipped) {
        failed.push_back(r);
      }
      chosen[r] = std::max(best, 0);
      for (size_t p = 0; p < outs.size(); ++p) {
        last[p][r] = std::move(outs[p]);
      }
    }
    const size_t solved = size_t(g.regions) - failed.size();
    if (!kept || solved > kept->solved) {
      kept = Kept{boxes, last, chosen, P, x, solved, round};
    }
    if (failed.empty() || round >= d.opt.push_rounds) {
      break;
    }
    const size_t moved = push_boxes(g, boxes, failed, d.opt.push_fraction);
    std::fprintf(stderr,
                 "legroute sat push round %u: %zu regions without an answer, %zu boxes to neighbor regions\n",
                 round + 1,
                 failed.size(),
                 moved);
    if (!moved) {
      break;
    }
    rep.push_rounds      = round + 1;
    rep.push_moved      += moved;
    rep.spilled_regions += balance_regions(d, g, boxes);
  }
  if (kept->round != rep.push_rounds) {  // an earlier round routed more regions: its boxes and answers
    std::fprintf(stderr, "legroute sat push: keeping round %u (%zu routable regions)\n", kept->round, kept->solved);
    boxes  = std::move(kept->boxes);
    last   = std::move(kept->last);
    chosen = std::move(kept->chosen);
    P      = std::move(kept->P);
    x      = std::move(kept->x);
  }
  rep.push_kept_round         = kept->round;
  // ---- the chosen answers: tiles, counters, connections
  rep.sat_unjoined_crossings += x.unjoined;
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
    rep.sat_unknown_regions += q.sat_unknown_regions;
    rep.sat_skipped_regions += q.sat_skipped_regions;
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
      wall = std::max(wall, per[r].reused ? 0.0 : per[r].seconds);
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
