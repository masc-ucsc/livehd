// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// legroute routing side: in-box demand, the clock spines, layer assignment and track assignment of the connections
// the SAT placement chose (legroute_sat.cpp).
#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <numeric>
#include <set>
#include <stdexcept>
#include <thread>
#include <unordered_map>

#include "inou/lefdef/routing_rules.hpp"
#include "legroute_internal.hpp"
#include "steiner_topology.hpp"

namespace livehd::placement::lr {
namespace {
constexpr double kDriverKohm = .5, kPinFf = .6;

struct Union_find {
  std::vector<int> p;
  explicit Union_find(size_t n) : p(n) { std::iota(p.begin(), p.end(), 0); }
  int find(int x) {
    while (p[x] != x) {
      x = p[x] = p[p[x]];
    }
    return x;
  }
  bool unite(int a, int b) {
    a = find(a);
    b = find(b);
    if (a == b) {
      return false;
    }
    p[a] = b;
    return true;
  }
};

int h_layer(const Grid& g, int k) {  // k-th horizontal routed layer from the bottom (clamped)
  int last = -1, seen = 0;
  for (int l = 0; l < int(g.layers.size()); ++l) {
    if (g.layers[l].horizontal) {
      last = l;
      if (seen++ == k) {
        return l;
      }
    }
  }
  return last;
}
int v_layer(const Grid& g, int k) {
  int last = -1, seen = 0;
  for (int l = 0; l < int(g.layers.size()); ++l) {
    if (!g.layers[l].horizontal) {
      last = l;
      if (seen++ == k) {
        return l;
      }
    }
  }
  return last;
}
// Length class of a connection, in tile widths (technology independent): short, medium, long.
// Local nets stay on the box layer and the lowest vertical one (legroute.final), so a routed connection starts one
// class up: away from the pins, and higher still when critical.
int length_class(double tiles, bool critical) {
  const int k = tiles < 8 ? 1 : 2;
  return std::min(2, k + int(critical));
}
std::pair<int, int> class_layers(const Grid& g, int k) {  // (H, V) layer of a class: M4/M3, M4/M5, M6/M7
  return {h_layer(g, k / 2), v_layer(g, k)};
}

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
int add_node(Tree& T, std::unordered_map<int, int>& at, int tile) {
  const auto [it, fresh] = at.try_emplace(tile, int(T.nodes.size()));
  if (fresh) {
    T.nodes.push_back({});
    T.nodes.back().tile = tile;
  }
  return it->second;
}

int add_conn(Routing& R, int net, int a, int b, const Net& N) {
  Conn c;
  c.net      = net;
  c.a        = a;
  c.b        = b;
  c.weight   = N.weight;
  c.critical = N.critical;
  c.clock    = N.clock;
  R.conns.push_back(std::move(c));
  R.trees[net].conns.push_back(int(R.conns.size()) - 1);
  return int(R.conns.size()) - 1;
}
}  // namespace

void box_demand(const Design& d, const Grid& g, const Boxes& boxes, Routing& R) {
  R.cap_h.assign(g.size(), 0);
  R.cap_v.assign(g.size(), 0);
  R.dem_h.assign(g.size(), 0);
  R.dem_v.assign(g.size(), 0);
  R.hist_h.assign(g.size(), 0);
  R.hist_v.assign(g.size(), 0);
  R.layer_dem.assign(g.layers.size(), std::vector<int32_t>(g.size(), 0));
  for (size_t l = 0; l < g.layers.size(); ++l) {
    for (size_t t = 0; t < g.size(); ++t) {
      (g.layers[l].horizontal ? R.cap_h : R.cap_v)[t] += g.cap[l][t];
    }
  }
  // in-box demand per box tile, for the layer assignment: a pin of a net leaving the box taps up to its net's metal
  // over the box (half a box-layer track and a quarter of a vertical one, plus half a vertical track for the riser).
  // The box layer absorbs what fits; the rest, and the vertical part, take routed tracks. The SAT placement already
  // kept every tile within its tracks; this only steers the layers.
  const int low_v = v_layer(g, 0), low_h = h_layer(g, 0);
  if (R.local.size() != d.nets.size()) {
    R.local.assign(d.nets.size(), 0);
  }
  R.box_dem_h.assign(g.size(), 0);
  R.box_dem_v.assign(g.size(), 0);
  for (size_t n = 0; n < d.nets.size(); ++n) {
    const auto& N = d.nets[n];
    if (!N.routed || N.clock || N.power) {
      continue;
    }
    std::map<int, int> per_tile;  // box tile -> pins
    for (const auto& p : N.pins) {
      if (p.comp >= 0 && d.cell[p.comp] >= 0) {
        ++per_tile[boxes.boxes[boxes.box_of[d.cell[p.comp]]].tile];
      }
    }
    if (per_tile.size() < 2) {
      continue;
    }
    for (const auto& [t, k] : per_tile) {
      R.box_dem_h[t] += .5 * k;
      R.box_dem_v[t] += .5 + .25 * k;
    }
  }
  for (size_t t = 0; t < g.size(); ++t) {
    const int over         = int(std::ceil(std::max(0.0, R.box_dem_h[t] - g.box_cap[t])));
    const int vert         = int(std::ceil(R.box_dem_v[t]));
    R.cap_h[t]            -= over;
    R.layer_dem[low_h][t] += over;
    R.cap_v[t]            -= vert;
    R.layer_dem[low_v][t] += vert;
  }
}

void clock_spines(const Design& d, const Grid& g, const Boxes& boxes, Routing& R) {
  R.trees.resize(d.nets.size());
  const int  rib_l = h_layer(g, 1 << 20), trunk_l = v_layer(g, 1 << 20), tap_l = v_layer(g, 0);
  const int  pitch = int(std::max(1u, d.opt.rib_tiles));
  const auto take  = [&](int l, int t) {
    (g.layers[l].horizontal ? R.cap_h : R.cap_v)[t] -= 1;
    R.layer_dem[l][t]                               += 1;
  };
  for (int n = 0; n < int(d.nets.size()); ++n) {
    const auto& N = d.nets[n];
    if (!N.clock || N.power || N.pins.size() < 2) {
      continue;
    }
    // flop (clock pin) boxes per region
    std::map<int, std::set<int>> per;
    for (const auto& p : N.pins) {
      if (p.clock && p.comp >= 0 && d.cell[p.comp] >= 0) {
        const int b = boxes.box_of[d.cell[p.comp]];
        per[boxes.boxes[b].region].insert(b);
      }
    }
    auto&                        T = R.trees[n];
    std::unordered_map<int, int> at;
    T = {};
    // the driver and the clock sinks that are not boxed flops are ordinary tree nodes
    for (size_t k = 0; k < N.pins.size(); ++k) {
      const auto& p = N.pins[k];
      if (p.clock && p.comp >= 0 && d.cell[p.comp] >= 0 && int(k) != N.driver) {
        continue;
      }
      int t;
      int box = -1;
      if (p.comp >= 0 && d.cell[p.comp] >= 0) {
        box = boxes.box_of[d.cell[p.comp]];
        t   = boxes.boxes[box].tile;
      } else if (p.comp >= 0) {
        const auto& pl = d.fp.components[p.comp].place;
        t              = g.tile_of(pl.x + d.macro[p.comp]->width * g.dbu / 2, pl.y + d.macro[p.comp]->height * g.dbu / 2);
      } else {
        t = g.tile_of(d.fp.terminals[p.term].x, d.fp.terminals[p.term].y);
      }
      const int node = add_node(T, at, t);
      T.nodes[node].pins.push_back(int(k));
      T.nodes[node].box  = std::max(T.nodes[node].box, box);
      T.nodes[node].term = std::max(T.nodes[node].term, p.term);
    }
    for (const auto& [region, bs] : per) {
      ++R.clock_domains;
      Spine sp;
      sp.net    = n;
      sp.region = region;
      std::vector<int> cols, rows;
      for (int b : bs) {
        cols.push_back(g.col(boxes.boxes[b].tile));
        rows.push_back(g.row(boxes.boxes[b].tile));
      }
      std::ranges::sort(cols);
      const int        rmin = *std::min_element(rows.begin(), rows.end()), rmax = *std::max_element(rows.begin(), rows.end());
      int              trunk_c = cols[cols.size() / 2];
      // ribs every `pitch` tile rows, centered so every flop row is within pitch / 2 of one
      std::vector<int> ribs;
      for (int r = rmin + pitch / 2; r <= rmax + pitch / 2; r += pitch) {
        ribs.push_back(std::min(r, rmax));
      }
      ribs.erase(std::unique(ribs.begin(), ribs.end()), ribs.end());
      // the trunk column must stay inside the region on every rib row
      for (int r : ribs) {
        if (g.region[g.tile(trunk_c, r)] != region) {
          for (int dc = 1; dc < g.cols; ++dc) {
            if (trunk_c + dc < g.cols && g.region[g.tile(trunk_c + dc, r)] == region) {
              trunk_c += dc;
              break;
            }
            if (trunk_c - dc >= 0 && g.region[g.tile(trunk_c - dc, r)] == region) {
              trunk_c -= dc;
              break;
            }
          }
        }
      }
      std::map<int, std::pair<int, int>> span;  // rib row -> col range
      for (int r : ribs) {
        span[r] = {trunk_c, trunk_c};
      }
      std::vector<std::pair<int, int>> tap_rib;
      for (int b : bs) {
        const int t    = boxes.boxes[b].tile;
        int       best = ribs.front();
        for (int r : ribs) {
          if (std::abs(r - g.row(t)) < std::abs(best - g.row(t))) {
            best = r;
          }
        }
        auto& s  = span[best];
        s.first  = std::min(s.first, g.col(t));
        s.second = std::max(s.second, g.col(t));
        tap_rib.emplace_back(b, best);
      }
      Segment trunk;
      trunk.net   = n;
      trunk.clock = true;
      trunk.layer = trunk_l;
      trunk.band  = trunk_c;
      trunk.lo    = ribs.front();
      trunk.hi    = ribs.back();
      for (int r = trunk.lo; r <= trunk.hi; ++r) {
        take(trunk_l, g.tile(trunk_c, r));
      }
      R.segments.push_back(trunk);
      sp.trunk = int(R.segments.size()) - 1;
      for (const auto& [r, cr] : span) {
        Segment rib;
        rib.net   = n;
        rib.clock = true;
        rib.layer = rib_l;
        rib.band  = r;
        rib.lo    = cr.first;
        rib.hi    = cr.second;
        for (int c = rib.lo; c <= rib.hi; ++c) {
          take(rib_l, g.tile(c, r));
        }
        R.segments.push_back(rib);
        sp.ribs.push_back(int(R.segments.size()) - 1);
        ++R.clock_ribs;
      }
      for (const auto& [b, r] : tap_rib) {
        const int t = boxes.boxes[b].tile;
        Segment   tap;
        tap.net   = n;
        tap.clock = true;
        tap.layer = tap_l;
        tap.band  = g.col(t);
        tap.lo    = std::min(r, g.row(t));
        tap.hi    = std::max(r, g.row(t));
        for (int rr = tap.lo; rr <= tap.hi; ++rr) {
          take(tap_l, g.tile(tap.band, rr));
        }
        R.segments.push_back(tap);
        sp.taps.emplace_back(b, int(R.segments.size()) - 1);
      }
      // the clock tree enters the region at the trunk's middle rib
      const int mid = ribs[ribs.size() / 2];
      sp.node       = add_node(T, at, g.tile(trunk_c, mid));
      R.spines.push_back(std::move(sp));
    }
    if (T.nodes.size() < 2) {
      continue;
    }
    // MST from the driver over the spine entries and the other clock sinks
    std::vector<std::pair<double, double>> pts;
    std::vector<int>                       order(T.nodes.size());
    std::iota(order.begin(), order.end(), 0);
    for (size_t k = 0; k < T.nodes.size(); ++k) {
      for (int pin : T.nodes[k].pins) {
        if (pin == N.driver) {
          std::swap(order[0], order[k]);
        }
      }
    }
    for (int k : order) {
      pts.emplace_back(g.cx(g.col(T.nodes[k].tile)), g.cy(g.row(T.nodes[k].tile)));
    }
    for (auto [a, b] : mst(pts)) {
      add_conn(R, n, order[a], order[b], N);
    }
  }
}

double wire_um(const Grid& g, const Conn& c) {
  double s = 0;
  for (size_t i = 0; i + 1 < c.poly.size(); ++i) {
    s += std::abs(c.poly[i].first - c.poly[i + 1].first) * double(g.tile_w)
         + std::abs(c.poly[i].second - c.poly[i + 1].second) * double(g.tile_h);
  }
  return s / g.dbu;
}

// ---------------------------------------------------------------- L6 layers
void assign_layers(const Design&, const Grid& g, Routing& R) {
  std::vector<int> order;
  for (int ci = 0; ci < int(R.conns.size()); ++ci) {
    if (R.conns[ci].poly.size() >= 2) {
      order.push_back(ci);
    }
  }
  std::ranges::stable_sort(order, [&](int a, int b) {
    const auto& A = R.conns[a];
    const auto& B = R.conns[b];
    if (A.critical != B.critical) {
      return A.critical;
    }
    if (A.weight != B.weight) {
      return A.weight > B.weight;
    }
    return wire_um(g, A) > wire_um(g, B);
  });
  for (int ci : order) {
    auto&        c      = R.conns[ci];
    const double tiles  = wire_um(g, c) * g.dbu / g.tile_w;
    const int    cls    = length_class(tiles, c.critical);
    const auto [ph, pv] = class_layers(g, cls);
    c.segs.clear();
    for (size_t i = 0; i + 1 < c.poly.size(); ++i) {
      const auto [c0, r0] = c.poly[i];
      const auto [c1, r1] = c.poly[i + 1];
      const bool h        = r0 == r1;
      Segment    s;
      s.conn             = ci;
      s.net              = c.net;
      s.critical         = c.critical;
      s.clock            = c.clock;
      s.band             = h ? r0 : c0;
      s.lo               = h ? std::min(c0, c1) : std::min(r0, r1);
      s.hi               = h ? std::max(c0, c1) : std::max(r0, r1);
      const bool forward = h ? c1 > c0 : r1 > r0;  // walking a -> b goes toward hi
      if (i == 0) {
        (forward ? s.node_lo : s.node_hi) = c.a;
      }
      if (i + 2 == c.poly.size()) {
        (forward ? s.node_hi : s.node_lo) = c.b;
      }
      // tiles this segment loads (the conn's terminal tiles are the boxes' own escape room)
      std::vector<int> load;
      for (int k = s.lo; k <= s.hi; ++k) {
        const int t = h ? g.tile(k, s.band) : g.tile(s.band, k);
        if ((i == 0 && t == g.tile(c.poly.front().first, c.poly.front().second))
            || (i + 2 == c.poly.size() && t == g.tile(c.poly.back().first, c.poly.back().second))) {
          continue;
        }
        load.push_back(t);
      }
      // the class layer first, then higher layers of the direction, then lower ones; the first that fits
      std::vector<int> cand{h ? ph : pv};
      for (int l = cand[0] + 1; l < int(g.layers.size()); ++l) {
        if (g.layers[l].horizontal == h) {
          cand.push_back(l);
        }
      }
      for (int l = cand[0] - 1; l >= 0; --l) {
        if (g.layers[l].horizontal == h) {
          cand.push_back(l);
        }
      }
      int    pick  = -1;
      double least = INFINITY;
      for (int l : cand) {
        int over = 0;
        for (int t : load) {
          over += std::max(0, R.layer_dem[l][t] + 1 - g.cap[l][t]);
        }
        if (over == 0) {
          pick = l;
          break;
        }
        if (over < least) {
          least = over;
          pick  = l;
        }
      }
      s.layer = pick;
      for (int t : load) {
        ++R.layer_dem[pick][t];
      }
      R.segments.push_back(s);
      c.segs.push_back(int(R.segments.size()) - 1);
    }
  }
}

// ---------------------------------------------------------------- L7 tracks
namespace {
struct Band {
  std::vector<int>                              segs;
  std::map<int64_t, std::map<int64_t, int64_t>> busy;  // track -> spans (start -> end)
  std::map<int64_t, std::map<int64_t, int64_t>> hard;  // track -> spans of power metal and macros (never shared)
};
bool free_on(const std::map<int64_t, int64_t>& m, int64_t lo, int64_t hi) {
  auto it = m.upper_bound(lo);
  if (it != m.begin() && std::prev(it)->second > lo) {
    return false;
  }
  return it == m.end() || it->first >= hi;
}
}  // namespace

void assign_tracks(const Design& d, const Grid& g, Routing& R, unsigned threads) {
  const int  low_v   = v_layer(g, 0);
  // span of a segment end along its axis: a node's riser (H) / bend partner track (H), else the whole tile
  const auto tile_lo = [&](const Segment& s, int k) { return g.layers[s.layer].horizontal ? g.left(k) : g.bottom(k); };
  const auto tile_hi
      = [&](const Segment& s, int k) { return g.layers[s.layer].horizontal ? g.left(k) + g.tile_w : g.bottom(k) + g.tile_h; };
  // which nodes have a vertical arrival (its track becomes the node's riser)
  std::map<std::pair<int, int>, int> v_arrival;  // (net, node) -> segment
  for (int si = 0; si < int(R.segments.size()); ++si) {
    const auto& s = R.segments[si];
    if (g.layers[s.layer].horizontal) {
      continue;
    }
    for (int node : {s.node_lo, s.node_hi}) {
      if (node >= 0) {
        auto [it, fresh] = v_arrival.try_emplace({s.net, node}, si);
        if (!fresh && s.layer < R.segments[it->second].layer) {
          it->second = si;  // the lowest arriving layer is the riser
        }
      }
    }
  }
  // per layer and band
  std::vector<std::map<int, Band>> bands(g.layers.size());
  const auto                       band_of = [&](int l, int b) -> Band& {
    auto [it, fresh] = bands[l].try_emplace(b);
    if (fresh) {
      const auto& L     = g.layers[l];
      const auto [a, e] = g.tracks(l, b);
      for (auto t = a; t <= e; ++t) {
        auto& m = it->second.busy[t];
        if (t < a + int64_t(d.opt.reserved_tracks)) {
          m[std::numeric_limits<int64_t>::min() / 2] = std::numeric_limits<int64_t>::max() / 2;  // reserved
          it->second.hard[t]                         = m;
          continue;
        }
        const auto x = g.track_coord(l, t);
        for (int oi : g.obstacle_band[l][b]) {
          const auto&  r  = g.obstacles[l][oi];
          const double c0 = L.horizontal ? r.yl : r.xl, c1 = L.horizontal ? r.yh : r.xh;
          if (x > c0 - L.pitch / 2.0 - L.width && x < c1 + L.pitch / 2.0 + L.width) {
            m[int64_t(L.horizontal ? r.xl : r.yl)]                  = int64_t(L.horizontal ? r.xh : r.yh);
            it->second.hard[t][int64_t(L.horizontal ? r.xl : r.yl)] = int64_t(L.horizontal ? r.xh : r.yh);
          }
        }
      }
    }
    return it->second;
  };
  // a riser on the lowest vertical layer for the nodes without a vertical arrival
  struct Riser {
    int net, node;
  };
  std::set<std::pair<int, int>> attached;  // (net, node) some segment ends at
  for (const auto& s : R.segments) {
    for (int node : {s.node_lo, s.node_hi}) {
      if (node >= 0) {
        attached.emplace(s.net, node);
      }
    }
  }
  std::vector<Riser> risers;
  for (const auto& [n, k] : attached) {
    if (!v_arrival.contains({n, k})) {
      risers.push_back({n, k});
    }
  }
  // per layer: the reach of its spacing rules (end of line included) from a wire edge
  std::vector<int64_t> keep(g.layers.size());
  {
    const physical::Signal_metal_rules rules(d.tech, d.fp);
    for (size_t l = 0; l < g.layers.size(); ++l) {
      keep[l] = std::max(g.layers[l].pitch - g.layers[l].width, rules.clearance(g.layers[l].name).value_or(g.layers[l].pitch));
    }
  }
  // line ends: no metal ahead of an end of the new span on the neighbor tracks within the rule reach, and the span
  // never lies ahead of a neighbor's end (the metal runs a wire width past each centerline end)
  const auto ends_free = [&](const Band& B, int l, int64_t t, int64_t lo, int64_t hi) {
    const auto&   L = g.layers[l];
    const int64_t k = keep[l], ext = L.width;
    const int64_t a0 = lo - ext, b0 = hi + ext;
    for (int64_t dt = 1; dt * L.pitch - L.width < k; ++dt) {
      for (auto u : {t - dt, t + dt}) {
        const auto it = B.busy.find(u);
        if (it == B.busy.end()) {
          continue;
        }
        const auto& m = it->second;
        auto        q = m.upper_bound(a0 - k - ext);
        if (q != m.begin()) {
          --q;
        }
        for (; q != m.end() && q->first - ext <= b0 + k; ++q) {
          const int64_t a = q->first - ext, b = q->second + ext;
          if ((a < a0 && b > a0 - k) || (b > b0 && a < b0 + k) || (a0 < a && b0 > a - k) || (b0 > b && a0 < b + k)) {
            return false;
          }
        }
      }
    }
    return true;
  };
  const auto pick = [&](Band& B, int l, int64_t lo, int64_t hi, bool critical, int64_t pref) -> std::pair<int64_t, bool> {
    const auto&                       L      = g.layers[l];
    const auto                        margin = std::max(L.pitch, keep[l] + 2 * L.width);
    int64_t                           best   = -1;
    std::tuple<int, int64_t, int64_t> key{1 << 30, 0, 0};
    for (auto& [t, m] : B.busy) {
      if (!free_on(m, lo - margin, hi + margin) || !ends_free(B, l, t, lo, hi)) {
        continue;
      }
      int     nb   = 0;
      int64_t near = std::numeric_limits<int64_t>::max();
      for (int64_t dt = 1; dt <= 3; ++dt) {
        for (auto u : {t - dt, t + dt}) {
          const auto it = B.busy.find(u);
          if (it != B.busy.end() && !free_on(it->second, lo, hi)) {
            near  = std::min(near, dt);
            nb   += dt == 1;
          }
        }
      }
      const std::tuple<int, int64_t, int64_t> k{nb * (critical ? 10 : 1), -std::min<int64_t>(near, 3), std::abs(t - pref)};
      if (k < key) {
        key  = k;
        best = t;
      }
    }
    if (best >= 0) {
      return {best, true};
    }
    // no free track: the least-overlapping one clear of power metal and macros (counted as unassigned)
    size_t least = SIZE_MAX;
    for (auto& [t, m] : B.busy) {
      if (const auto h = B.hard.find(t); h != B.hard.end() && !free_on(h->second, lo - margin, hi + margin)) {
        continue;
      }
      size_t o = 0;
      for (auto it = m.lower_bound(lo - margin); it != m.end() && it->first < hi + margin; ++it) {
        ++o;
      }
      if (o < least) {
        least = o;
        best  = t;
      }
    }
    return {best, false};
  };
  std::atomic<size_t> unassigned = 0;
  const auto          occupy     = [](Band& B, int64_t t, int64_t lo, int64_t hi) { B.busy[t][lo] = hi; };
  // phase 1: fixed tracks (virtual pin hops) and the clock spine, every band
  for (auto& s : R.segments) {
    if (s.track < 0 && !s.clock) {
      continue;
    }
    auto&      B  = band_of(s.layer, s.band);
    const auto lo = tile_lo(s, s.lo), hi = tile_hi(s, s.hi);
    if (s.track < 0) {
      const auto [t, ok] = pick(B, s.layer, lo, hi, true, (g.tracks(s.layer, s.band).first + g.tracks(s.layer, s.band).second) / 2);
      s.track            = t;
      unassigned += !ok;
    }
    occupy(B, s.track, lo, hi);
  }
  // a vertical arrival is its node's riser
  const auto set_riser = [&](int si) {
    const auto& s = R.segments[si];
    if (g.layers[s.layer].horizontal || s.track < 0) {
      return;
    }
    for (int node : {s.node_lo, s.node_hi}) {
      if (node >= 0) {
        const auto it = v_arrival.find({s.net, node});
        if (it != v_arrival.end() && it->second == si) {
          R.trees[s.net].nodes[node].riser_layer = s.layer;
          R.trees[s.net].nodes[node].riser_track = s.track;
        }
      }
    }
  };
  std::mutex                                     failed_mutex;
  std::vector<std::tuple<int, int64_t, int64_t>> failed;  // (segment, lo, hi) with no free track on its layer
  // phase 2: vertical segments (whole end tiles), critical and long first, bands in parallel
  const auto                                     run_phase = [&](bool horizontal) {
    std::vector<std::vector<int>>                   work(g.layers.size());
    std::map<std::pair<int, int>, std::vector<int>> by_band;
    for (int si = 0; si < int(R.segments.size()); ++si) {
      const auto& s = R.segments[si];
      if (s.track >= 0 || s.clock || g.layers[s.layer].horizontal != horizontal) {
        continue;
      }
      by_band[{s.layer, s.band}].push_back(si);
    }
    for (auto& [lb, v] : by_band) {
      band_of(lb.first, lb.second);  // create before the parallel phase (map insertion is not thread safe)
    }
    if (!horizontal) {
      for (const auto& r : risers) {
        band_of(low_v, g.col(R.trees[r.net].nodes[r.node].tile));
      }
    }
    std::vector<std::pair<int, int>> keys;
    for (auto& [lb, v] : by_band) {
      keys.push_back(lb);
    }
    if (!horizontal) {  // bands holding only risers
      for (const auto& r : risers) {
        const std::pair<int, int> lb{low_v, g.col(R.trees[r.net].nodes[r.node].tile)};
        if (!by_band.contains(lb)) {
          by_band[lb];
          keys.push_back(lb);
        }
      }
    }
    std::map<std::pair<int, int>, std::vector<int>> risers_in;
    if (!horizontal) {
      for (int k = 0; k < int(risers.size()); ++k) {
        risers_in[{low_v, g.col(R.trees[risers[k].net].nodes[risers[k].node].tile)}].push_back(k);
      }
    }
    parallel(keys.size(), threads, [&](size_t k) {
      const auto [l, b] = keys[k];
      auto& B           = bands[l].at(b);
      // risers first (short, one tile, near the net's pins)
      if (const auto it = risers_in.find(keys[k]); it != risers_in.end()) {
        for (int ri : it->second) {
          auto&     node = R.trees[risers[ri].net].nodes[risers[ri].node];
          const int row  = g.row(node.tile);
          int64_t   px   = g.cx(g.col(node.tile));
          for (int pi : node.pins) {  // a die IO pin first: its riser lands right at the pin
            const auto& p = d.nets[risers[ri].net].pins[pi];
            if (p.term >= 0) {
              px = d.fp.terminals[p.term].x;
              break;
            }
            if (p.comp >= 0 && pi == node.pins.front()) {
              px = d.fp.components[p.comp].place.x + std::llround(d.macro[p.comp]->width * g.dbu / 2);
            }
          }
          const auto pref     = int64_t(std::lround(double(px - g.layers[l].offset) / g.layers[l].pitch));
          const auto [t, ok]  = pick(B, l, g.bottom(row), g.bottom(row) + g.tile_h, false, pref);
          node.riser_layer    = l;
          node.riser_track    = t;
          unassigned         += !ok;
          occupy(B, t, g.bottom(row), g.bottom(row) + g.tile_h);
        }
      }
      auto segs = by_band.at(keys[k]);
      std::ranges::stable_sort(segs, [&](int a, int e) {
        const auto& A = R.segments[a];
        const auto& E = R.segments[e];
        if (A.critical != E.critical) {
          return A.critical;
        }
        return A.hi - A.lo > E.hi - E.lo;
      });
      for (int si : segs) {
        auto&   s  = R.segments[si];
        int64_t lo = tile_lo(s, s.lo), hi = tile_hi(s, s.hi);
        if (horizontal) {  // exact ends: the node riser or the bend partner's track
          const auto end_x = [&](int node, int tile, bool low) -> int64_t {
            if (node >= 0) {
              const auto& nd = R.trees[s.net].nodes[node];
              if (nd.riser_track >= 0) {
                return g.track_coord(nd.riser_layer, nd.riser_track);
              }
            } else if (s.conn >= 0) {
              for (int o : R.conns[s.conn].segs) {
                const auto& p = R.segments[o];
                if (o != si && !g.layers[p.layer].horizontal && p.band == tile && p.track >= 0) {
                  return g.track_coord(p.layer, p.track);
                }
              }
            }
            return low ? tile_lo(s, tile) : tile_hi(s, tile);
          };
          lo = end_x(s.node_lo, s.lo, true);
          hi = end_x(s.node_hi, s.hi, false);
          if (lo > hi) {
            std::swap(lo, hi);
          }
        }
        const auto mid     = (g.tracks(l, b).first + g.tracks(l, b).second) / 2;
        const auto [t, ok] = pick(B, l, lo, hi, s.critical, mid);
        if (!ok) {  // retried on the other layers of its direction after this pass
          std::lock_guard lock(failed_mutex);
          failed.push_back({si, lo, hi});
          continue;
        }
        s.track = t;
        occupy(B, t, lo, hi);
        set_riser(si);
      }
    });
    // sequential retry: the nearest other layer of the same direction with a free track, else the least
    // conflicting track of its own layer (counted as unassigned)
    std::ranges::sort(failed);
    for (const auto& [si, lo, hi] : failed) {
      auto&            s = R.segments[si];
      std::vector<int> others;
      for (int l = 0; l < int(g.layers.size()); ++l) {
        if (l != s.layer && g.layers[l].horizontal == horizontal) {
          others.push_back(l);
        }
      }
      std::ranges::stable_sort(others, [&](int a, int e) { return std::abs(a - s.layer) < std::abs(e - s.layer); });
      bool placed = false;
      for (int l : others) {
        auto& B            = band_of(l, s.band);
        const auto [a, e]  = g.tracks(l, s.band);
        const auto [t, ok] = pick(B, l, lo, hi, s.critical, (a + e) / 2);
        if (ok) {
          s.layer = l;
          s.track = t;
          occupy(B, t, lo, hi);
          placed = true;
          break;
        }
      }
      if (!placed) {
        auto& B             = band_of(s.layer, s.band);
        const auto [a, e]   = g.tracks(s.layer, s.band);
        const auto [t, ok]  = pick(B, s.layer, lo, hi, s.critical, (a + e) / 2);
        s.track             = t;
        unassigned         += !ok;
        if (t >= 0) {
          occupy(B, t, lo, hi);
        }
      }
      set_riser(si);
    }
    failed.clear();
  };
  run_phase(false);
  // a vertical arrival that is not its node's riser joins it through a jog on the lowest horizontal layer inside
  // the node tile, on a checked track (horizontal segments see it)
  const int low_h = h_layer(g, 0);
  for (int si = 0; si < int(R.segments.size()); ++si) {
    auto& s = R.segments[si];
    if (g.layers[s.layer].horizontal || s.track < 0) {
      continue;
    }
    for (int e = 0; e < 2; ++e) {
      const int node = e ? s.node_hi : s.node_lo;
      if (node < 0) {
        continue;
      }
      const auto& nd = R.trees[s.net].nodes[node];
      if (nd.riser_track < 0 || (nd.riser_layer == s.layer && nd.riser_track == s.track)) {
        continue;
      }
      const int  row = g.row(nd.tile);
      auto&      B   = band_of(low_h, row);
      const auto xv = g.track_coord(s.layer, s.track), xr = g.track_coord(nd.riser_layer, nd.riser_track);
      const auto [a, b]   = g.tracks(low_h, row);
      const auto [t, ok]  = pick(B, low_h, std::min(xv, xr), std::max(xv, xr), false, (a + b) / 2);
      unassigned         += !ok;
      occupy(B, t, std::min(xv, xr), std::max(xv, xr));
      (e ? s.jog_hi : s.jog_lo) = t;
    }
  }
  // die IO and fixed-cell pins reach their node's riser along a checked track of the lowest horizontal layer near
  // the pin (F joins the pin to it with a short vertical piece)
  for (int n = 0; n < int(R.trees.size()); ++n) {
    for (auto& nd : R.trees[n].nodes) {
      if (nd.riser_track < 0) {
        continue;
      }
      for (int pi : nd.pins) {
        const auto& p = d.nets[n].pins[pi];
        if (p.comp >= 0 && d.cell[p.comp] >= 0) {
          continue;
        }
        double px = 0, py = 0;
        if (p.term >= 0) {
          px = double(d.fp.terminals[p.term].x);
          py = double(d.fp.terminals[p.term].y);
        } else {
          const auto& pl = d.fp.components[p.comp].place;
          px             = pl.x + d.macro[p.comp]->width * g.dbu / 2;
          py             = pl.y + d.macro[p.comp]->height * g.dbu / 2;
        }
        const int  row = g.row_of(py);
        auto&      B   = band_of(low_h, row);
        const auto xr  = g.track_coord(nd.riser_layer, nd.riser_track);
        const auto lo = std::min(int64_t(px), xr), hi = std::max(int64_t(px), xr);
        const auto pref     = int64_t(std::lround((py - g.layers[low_h].offset) / g.layers[low_h].pitch));
        const auto [t, ok]  = pick(B, low_h, lo, hi, false, pref);
        unassigned         += !ok;
        occupy(B, t, lo, hi);
        nd.stub_track = t;
        break;  // one stub per node: the node's other non-box pins share it
      }
    }
  }
  run_phase(true);
  R.unassigned = unassigned;
}

double net_delay_ps(const Design& d, const Grid& g, const Boxes& boxes, const Routing& R, int net) {
  (void)boxes;
  const auto& N = d.nets[net];
  const auto& T = R.trees[net];
  if (T.nodes.size() < 2 || N.driver < 0) {
    return 0;
  }
  int drv = -1;
  for (size_t k = 0; k < T.nodes.size(); ++k) {
    for (int p : T.nodes[k].pins) {
      if (p == N.driver) {
        drv = int(k);
      }
    }
  }
  if (drv < 0) {
    return 0;
  }
  const size_t                                     n = T.nodes.size();
  std::vector<std::vector<std::pair<int, double>>> adj(n);
  std::vector<double>                              cap(n, 0);
  for (size_t k = 0; k < n; ++k) {
    cap[k] = kPinFf * double(T.nodes[k].pins.size() - (int(k) == drv));
  }
  const auto add = [&](int a, int b, double r, double c) {
    cap[a] += c / 2;
    cap[b] += c / 2;
    adj[a].emplace_back(b, r);
    adj[b].emplace_back(a, r);
  };
  for (int ci : T.conns) {
    const auto& c = R.conns[ci];
    double      r = 0, cc = 0;
    for (int si : c.segs) {
      const auto&  s    = R.segments[si];
      const double len  = (s.hi - s.lo) * double(g.layers[s.layer].horizontal ? g.tile_w : g.tile_h) / g.dbu;
      r                += g.layers[s.layer].r_um * len;
      cc               += g.layers[s.layer].c_um * len;
    }
    if (c.segs.empty() && c.poly.size() >= 2) {  // before layer assignment: the class layers of its length
      const double len    = wire_um(g, c);
      const auto [lh, lv] = class_layers(g, length_class(len * g.dbu / g.tile_w, c.critical));
      r                   = (g.layers[lh].r_um + g.layers[lv].r_um) / 2 * len;
      cc                  = (g.layers[lh].c_um + g.layers[lv].c_um) / 2 * len;
    }
    if (c.a != c.b) {
      add(c.a, c.b, r, cc);
    }
  }
  for (const auto& v : R.vpins) {
    if (v.net == net) {
      const auto&  s   = R.segments[v.seg];
      const double len = (s.hi - s.lo) * double(g.layers[s.layer].horizontal ? g.tile_w : g.tile_h) / g.dbu;
      add(v.inside, v.outside, g.layers[s.layer].r_um * len, g.layers[s.layer].c_um * len);
    }
  }
  std::vector<int>    order{drv}, parent(n, -1);
  std::vector<double> rin(n, 0);
  std::vector<char>   seen(n, 0);
  seen[drv] = 1;
  for (size_t i = 0; i < order.size(); ++i) {
    for (auto [v, r] : adj[order[i]]) {
      if (!seen[v]) {
        seen[v]   = 1;
        parent[v] = order[i];
        rin[v]    = r;
        order.push_back(v);
      }
    }
  }
  std::vector<double> down = cap;
  for (size_t i = order.size(); i-- > 1;) {
    down[parent[order[i]]] += down[order[i]];
  }
  std::vector<double> delay(n, 0);
  double              worst = 0;
  for (size_t i = 1; i < order.size(); ++i) {
    const int v = order[i];
    delay[v]    = delay[parent[v]] + rin[v] * down[v];
    if (!T.nodes[v].pins.empty()) {
      worst = std::max(worst, delay[v]);
    }
  }
  return kDriverKohm * down[drv] + worst;
}
}  // namespace livehd::placement::lr
