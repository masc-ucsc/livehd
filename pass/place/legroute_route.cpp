// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// legroute routing side: in-box demand, the clock spines, layer assignment and track assignment of the connections
// the SAT placement chose (legroute_sat.cpp).
#include <algorithm>
#include <atomic>
#include <unordered_set>
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
      } else {
        t = physical_pin_access(d, g, p).tile;
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
      std::vector<int> tiles;
      for (int b : bs) {
        tiles.push_back(boxes.boxes[b].tile);
      }
      const auto                       cd      = plan_clock_domain(g, region, tiles, pitch);
      const int                        trunk_c = cd.trunk_c;
      const auto&                      ribs    = cd.ribs;
      const auto&                      span    = cd.span;
      std::vector<std::pair<int, int>> tap_rib;
      size_t                           k = 0;
      for (int b : bs) {
        tap_rib.emplace_back(b, cd.tap_rib[k++]);
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

Clock_charge clock_tree_charge(const Design& d, const Grid& g, const Boxes* boxes, unsigned rib_tiles) {
  const int    pitch = int(std::max(1u, rib_tiles));
  Clock_charge out{std::vector<int>(g.size(), 0), std::vector<int>(g.size(), 0)};
  const auto   box_tile = [&](int b) {
    const auto& box = boxes->boxes[size_t(b)];
    return box.tile >= 0 ? box.tile : g.tile_of(box.cx, box.cy);
  };
  for (const auto& net : d.nets) {
    if (!net.clock || net.power || net.pins.size() < 2) {
      continue;
    }
    std::map<int, std::vector<int>> per;   // region -> flop unit tiles (boxes once each)
    std::vector<int>                nodes;  // tree nodes as clock_spines adds them: driver first, unboxed sinks
    std::set<int>                   seen;
    for (size_t k = 0; k < net.pins.size(); ++k) {
      const auto& p     = net.pins[k];
      const bool  boxed = p.comp >= 0 && d.cell[p.comp] >= 0;
      if (p.clock && boxed && int(k) != net.driver) {
        if (boxes) {
          const int b = boxes->box_of[size_t(d.cell[p.comp])];
          if (b >= 0 && seen.insert(b).second) {
            per[boxes->boxes[size_t(b)].region].push_back(box_tile(b));
          }
        } else {
          const int t = physical_pin_access(d, g, p).tile;
          per[g.region.empty() ? 0 : g.region[size_t(t)]].push_back(t);
        }
        continue;
      }
      const int t = boxed && boxes ? box_tile(boxes->box_of[size_t(d.cell[p.comp])]) : physical_pin_access(d, g, p).tile;
      if (std::ranges::find(nodes, t) == nodes.end()) {
        nodes.push_back(t);
      }
      if (int(k) == net.driver) {
        std::swap(nodes.front(), *std::ranges::find(nodes, t));
      }
    }
    for (const auto& [region, tiles] : per) {
      const auto cd = plan_clock_domain(g, region, tiles, pitch);
      for (int r = cd.ribs.front(); r <= cd.ribs.back(); ++r) {
        ++out.v[size_t(g.tile(cd.trunk_c, r))];
      }
      for (const auto& [r, cr] : cd.span) {
        for (int c = cr.first; c <= cr.second; ++c) {
          ++out.h[size_t(g.tile(c, r))];
        }
      }
      for (size_t k = 0; k < tiles.size(); ++k) {
        const int c = g.col(tiles[k]), row = g.row(tiles[k]);
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
    // the joins: an MST over the nodes, each edge an L, horizontal first (as legroute.cpp shapes them after L4)
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
  // A steering estimate, not an edge ledger: distinct-net demand per (layer, tile), since segments of one net on
  // the same layer and tile can share a track (the coarse model also counts a net once per edge). Keyed
  // (layer, tile, net). Track assignment may still move a segment to another layer of its direction.
  std::unordered_set<uint64_t> net_on;
  const auto                   key = [&](int l, int t, int net) {
    return (uint64_t(uint32_t(l)) << 56) ^ (uint64_t(uint32_t(t)) << 28) ^ uint64_t(uint32_t(net));
  };
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
          if (!net_on.contains(key(l, t, c.net))) {  // the net already pays for this layer and tile
            over += std::max(0, R.layer_dem[l][t] + 1 - g.cap[l][t]);
          }
        }
        if (over == 0) {
          pick  = l;
          least = 0;  // the selected layer fits: no overload to report
          break;
        }
        if (over < least) {
          least = over;
          pick  = l;
        }
      }
      s.layer = pick;
      if (least > 0 && std::isfinite(least)) {
        ++R.layer_overflow_segments;
        R.layer_overflow_tiles += size_t(least);
      }
      for (int t : load) {
        if (net_on.insert(key(pick, t, c.net)).second) {
          ++R.layer_dem[pick][t];
        }
      }
      R.segments.push_back(s);
      c.segs.push_back(int(R.segments.size()) - 1);
    }
  }
}

// ---------------------------------------------------------------- L7 tracks
namespace {
using Spans = std::map<int64_t, int64_t>;
void merge_span(Spans& spans, int64_t lo, int64_t hi) {
  auto it = spans.lower_bound(lo);
  if (it != spans.begin() && std::prev(it)->second >= lo) {
    --it;
  }
  while (it != spans.end() && it->first <= hi) {
    lo = std::min(lo, it->first);
    hi = std::max(hi, it->second);
    it = spans.erase(it);
  }
  spans.emplace(lo, hi);
}
struct Track {
  Spans                all;
  std::map<int, Spans> owners;  // net -> union of its spans; -1 is an unshareable obstacle
  void                 occupy(int net, int64_t lo, int64_t hi) {
    merge_span(all, lo, hi);
    merge_span(owners[net], lo, hi);
  }
};
struct Band {
  std::vector<int>         segs;
  std::map<int64_t, Track> busy;
  std::map<int64_t, Spans> hard;  // power metal and macros (never shared)
  Band*                    shadow = nullptr;  // l5_fallback_shadow: every contribution except FALLBACK ones
  Band*                    ledger = nullptr;  // l5_fallback_shadow: every contribution (checked against busy)
  struct Contribution {
    int     net;
    int64_t lo, hi;
    uint8_t state;  // Track_state; 255 marks hard/reserved initialization
  };
  std::map<int64_t, std::vector<Contribution>> contributions;  // l5_fallback_shadow ledger only: per track, in order
};
bool free_on(const Spans& m, int64_t lo, int64_t hi) {
  auto it = m.upper_bound(lo);
  if (it != m.begin() && std::prev(it)->second > lo) {
    return false;
  }
  return it == m.end() || it->first >= hi;
}
bool free_on(const Track& track, int64_t lo, int64_t hi, int net) {
  if (free_on(track.all, lo, hi)) {
    return true;
  }
  if (net < 0 || !track.owners.contains(net)) {
    return false;
  }
  for (const auto& [owner, spans] : track.owners) {
    if (owner != net && !free_on(spans, lo, hi)) {
      return false;
    }
  }
  return true;
}
}  // namespace

void assign_tracks(const Design& d, const Grid& g, Routing& R, unsigned threads) {
  const int                low_v = v_layer(g, 0);
  const Track_reservations reservations(d.opt, d.tech, g.dbu);
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
  const bool                       shadow_on = d.opt.l5_fallback_shadow;
  std::vector<std::map<int, Band>> shadows(shadow_on ? g.layers.size() : 0), ledgers(shadow_on ? g.layers.size() : 0);
  const auto                       band_of = [&](int l, int b) -> Band& {
    auto [it, fresh] = bands[l].try_emplace(b);
    if (fresh) {
      const auto& L     = g.layers[l];
      const auto [a, e] = g.tracks(l, b);
      for (auto t = a; t <= e; ++t) {
        auto&      m = it->second.busy[t];
        const auto x = g.track_coord(l, t);
        if (reservations.selected(L.name, x, x - L.pitch, t - a)) {
          m.occupy(-1, std::numeric_limits<int64_t>::min() / 2, std::numeric_limits<int64_t>::max() / 2);
          it->second.hard[t] = m.all;
          continue;
        }
        for (int oi : g.obstacle_band[l][b]) {
          const auto&  r  = g.obstacles[l][oi];
          const double c0 = L.horizontal ? r.yl : r.xl, c1 = L.horizontal ? r.yh : r.xh;
          if (x > c0 - L.pitch / 2.0 - L.width && x < c1 + L.pitch / 2.0 + L.width) {
            const auto lo = int64_t(L.horizontal ? r.xl : r.yl), hi = int64_t(L.horizontal ? r.xh : r.yh);
            m.occupy(-1, lo, hi);
            merge_span(it->second.hard[t], lo, hi);
          }
        }
      }
      if (shadow_on) {  // hard and reserved contributions are in both
        auto& S = shadows[l][b];
        auto& G = ledgers[l][b];
        S.busy = G.busy = it->second.busy;
        S.hard = G.hard = it->second.hard;
        for (const auto& [t, track] : it->second.busy) {
          for (const auto& [owner, spans] : track.owners) {
            for (const auto& [lo, hi] : spans) {
              G.contributions[t].push_back({owner, lo, hi, 255});
            }
          }
        }
        it->second.shadow = &S;
        it->second.ledger = &G;
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
  std::vector<char> ordinary_spacing(g.layers.size(), 0);
  const physical::Signal_metal_rules rules(d.tech, d.fp);
  for (size_t l = 0; l < g.layers.size(); ++l) {
    const auto clearance = rules.clearance(g.layers[l].name);
    keep[l] = std::max(g.layers[l].pitch - g.layers[l].width, clearance.value_or(g.layers[l].pitch));
    if (const auto layer = d.tech.layers.find(g.layers[l].name); layer != d.tech.layers.end()) {
      // Without end-specific rules, containment is conservative for spacing/PRL. EOL/corner rules keep the
      // whole-reach endpoint envelope until the joints are placed: an exact check on a provisional span does not
      // certify the shorter base metal final emits (a measured A/B on sha3 also showed no final-closure gain).
      ordinary_spacing[l] = clearance && layer->second.rules.properties.empty()
                            && std::ranges::none_of(layer->second.rules.spacing, [](const auto& rule) { return rule.eol.has_value(); });
    }
  }
  // Opt-in l5_rule_reach: the end envelope against signal neighbors only scans the track offsets whose edge gap is
  // below Signal_metal_rules::lateral_reach (the largest across-track extent of any rule branch on the layer, every
  // PRL row/length included; a gap equal to a rule distance is legal). A sampled exact-checker scan of nominal pairs
  // can only widen it (a cross-check, never a narrowing). Hard/reserved neighbors keep the full query-radius reach.
  // This bounds straight nominal-width wires only: wider via/pin landings and same-net unions are Final's to check.
  std::vector<int64_t> signal_reach(g.layers.size(), std::numeric_limits<int64_t>::max());
  if (d.opt.l5_rule_reach) {
    for (size_t l = 0; l < g.layers.size(); ++l) {
      const auto& L = g.layers[l];
      if (ordinary_spacing[l] || !rules.clearance(L.name) || L.pitch <= 0) {
        continue;
      }
      const int64_t k = keep[l], w = L.width;
      const auto    wire = [&](int64_t at, int64_t a, int64_t b) {
        const double half = w / 2.;
        return physical::Shape{L.name, L.horizontal ? physical::Rect{double(a), at - half, double(b), at + half}
                                                    : physical::Rect{at - half, double(a), at + half, double(b)}};
      };
      // L5 metal is a centerline span extended by a wire width at each end: never shorter than 2w
      const std::array<int64_t, 7> lens{2 * w, 3 * w, std::max<int64_t>(2 * w, k / 2), k, 2 * k, 4 * k, 10 * k};
      const int64_t                lateral = rules.lateral_reach(L.name).value_or(k);
      int64_t                      reach   = 0;
      for (int64_t dt = 1; dt * L.pitch - w < lateral; ++dt) {
        reach = dt;
      }
      R.track_fallback_shadow[std::format("rule_reach_analytic:{}", L.name)] = size_t(reach);
      for (int64_t dt = reach + 1; dt * L.pitch - w < k; ++dt) {
        bool illegal = false;
        for (auto A : lens) {
          for (auto Bn : lens) {
            for (int64_t x = -Bn - k - 2 * w - 2; x <= A + k + 2 * w + 2 && !illegal; ++x) {
              illegal = !rules.spacing(wire(0, 0, A), wire(dt * L.pitch, x, x + Bn)).legal;
            }
          }
        }
        if (illegal) {
          reach = dt;
        }
      }
      signal_reach[l] = reach;
      R.track_fallback_shadow[std::format("rule_reach:{}", L.name)] = size_t(reach);
    }
  }
  // line ends: no metal ahead of an end of the new span on the neighbor tracks within the rule reach, and the span
  // never lies ahead of a neighbor's end (the metal runs a wire width past each centerline end)
  struct End_blocker {
    int64_t track = 0, a = 0, b = 0;
    int     owner = 0;
    bool    rules = false;  // exact spacing rules (true) or the conservative end envelope (false)
  };
  // exact: evaluate signal neighbors with the full Signal_metal_rules check even on EOL layers (diagnostic only)
  const auto ends_free = [&](const Band& B, int l, int64_t t, int64_t lo, int64_t hi, End_blocker* why = nullptr,
                             bool exact = false) {
    const auto&   L = g.layers[l];
    const int64_t k = keep[l], ext = L.width;
    const int64_t a0 = lo - ext, b0 = hi + ext;
    for (int64_t dt = 1; dt * L.pitch - L.width < k; ++dt) {
      for (auto u : {t - dt, t + dt}) {
        const auto it = B.busy.find(u);
        if (it == B.busy.end()) {
          continue;
        }
        // Same-net sharing is only collinear on this track. Parallel shapes still obey line-end rules.
        for (const auto& [owner, m] : it->second.owners) {
          auto q = m.upper_bound(a0 - k - ext);
          if (q != m.begin()) {
            --q;
          }
          for (; q != m.end() && q->first - ext <= b0 + k; ++q) {
            const int64_t a = q->first - ext, b = q->second + ext;
            if (owner >= 0 && dt > signal_reach[l] && !ordinary_spacing[l] && !exact) {
              continue;  // l5_rule_reach: no nominal signal pair at this offset is illegal
            }
            if (owner >= 0 && (ordinary_spacing[l] || exact)) {
              const auto wire = [&](int64_t track, int64_t begin, int64_t end) {
                const double at = g.track_coord(l, track), half = L.width / 2.;
                return physical::Shape{L.name, L.horizontal ? physical::Rect{double(begin), at - half, double(end), at + half}
                                                            : physical::Rect{at - half, double(begin), at + half, double(end)}};
              };
              if (!rules.spacing(wire(t, a0, b0), wire(u, a, b)).legal) {
                if (why) {
                  *why = {u, q->first, q->second, owner, true};
                }
                return false;
              }
            } else if ((a <= a0 && b > a0 - k) || (b >= b0 && a < b0 + k) || (a0 <= a && b0 > a - k)
                       || (b0 >= b && a0 < b + k)) {
              // Equal ends are still neighboring ends, including their EOL keepouts.
              if (why) {
                *why = {u, q->first, q->second, owner, false};
              }
              return false;
            }
          }
        }
      }
    }
    return true;
  };
  std::vector<std::array<std::atomic<size_t>, 5>> rejects(g.layers.size());
  const auto pick = [&](Band& B, int l, int64_t lo, int64_t hi, int net, bool critical, int64_t pref,
                        bool telemetry = true) -> std::pair<int64_t, bool> {
    const auto&                       L      = g.layers[l];
    const auto                        margin = std::max(L.pitch, keep[l] + 2 * L.width);
    int64_t                           best   = -1;
    std::tuple<int, int64_t, int64_t> key{1 << 30, 0, 0};
    for (auto& [t, m] : B.busy) {
      if (!free_on(m, lo - margin, hi + margin, net) || !ends_free(B, l, t, lo, hi)) {
        continue;
      }
      int     nb   = 0;
      int64_t near = std::numeric_limits<int64_t>::max();
      for (int64_t dt = 1; dt <= 3; ++dt) {
        for (auto u : {t - dt, t + dt}) {
          const auto it = B.busy.find(u);
          if (it != B.busy.end() && !free_on(it->second, lo, hi, net)) {
            near  = std::min(near, dt);
            nb   += dt == 1;
          }
        }
      }
      // Collinear sharing with the same net's existing span costs no new track: prefer it first.
      const auto mine  = net >= 0 ? m.owners.find(net) : m.owners.end();
      const bool share = mine != m.owners.end() && !free_on(mine->second, lo, hi);
      const std::tuple<int, int64_t, int64_t> k{share ? -1 : nb * (critical ? 10 : 1), -std::min<int64_t>(near, 3), std::abs(t - pref)};
      if (k < key) {
        key  = k;
        best = t;
      }
    }
    if (best >= 0) {
      return {best, true};
    }
    if (!telemetry) {
      return {-1, false};
    }
    {
      // telemetry: the most permissive rejection among the band's tracks (0 no track, 1 obstacle on the span,
      // 2 another net on the span, 3 only the end margin, 4 only a neighbor's line-end rule)
      int cls = 0;
      for (const auto& [t, m] : B.busy) {
        const auto ob = m.owners.find(-1);
        const int  c  = ob != m.owners.end() && !free_on(ob->second, lo, hi) ? 1
                        : !free_on(m, lo, hi, net)                         ? 2
                        : !free_on(m, lo - margin, hi + margin, net)       ? 3
                                                                           : 4;
        cls           = std::max(cls, c);
      }
      ++rejects[size_t(l)][size_t(cls)];
    }
    // no free track: the least-overlapping one clear of power metal and macros (counted as unassigned)
    size_t least = SIZE_MAX;
    for (auto& [t, m] : B.busy) {
      if (const auto h = B.hard.find(t); h != B.hard.end() && !free_on(h->second, lo - margin, hi + margin)) {
        continue;
      }
      size_t o = 0;
      for (const auto& [owner, spans] : m.owners) {
        if (owner == net && net >= 0) {
          continue;
        }
        auto it = spans.upper_bound(lo - margin);
        if (it != spans.begin()) {
          --it;
        }
        for (; it != spans.end() && it->first < hi + margin; ++it) {
          o += it->second > lo - margin;
        }
      }
      if (o < least) {
        least = o;
        best  = t;
      }
    }
    return {best, false};
  };
  std::atomic<size_t>                unassigned = 0;
  std::array<std::atomic<size_t>, 6> failures{};  // fixed, riser, vertical, horizontal, jog, IO stub
  const auto                         occupy = [](Band& B, int64_t t, int64_t lo, int64_t hi, int net, uint8_t state) {
    if (t >= 0) {
      B.busy[t].occupy(net, lo, hi);
      if (B.ledger) {
        B.ledger->busy[t].occupy(net, lo, hi);
        B.ledger->contributions[t].push_back({net, lo, hi, state});
      }
      if (B.shadow && state != uint8_t(Track_state::fallback)) {
        B.shadow->busy[t].occupy(net, lo, hi);
      }
    }
  };
  // l5_fallback_shadow: after a failed pick (before its proposal is inserted), rerun the same predicates on the shadow
  std::mutex shadow_mutex;
  const auto probe = [&](Band& B, int l, int64_t lo, int64_t hi, int net, bool critical, int64_t pref, const char* kind) {
    if (!B.shadow) {
      return;
    }
    const bool ok = pick(*B.shadow, l, lo, hi, net, critical, pref, false).second;
    // the same real band: is some track rejected only by the end envelope, would it pass the exact rules, and does
    // the first envelope blocker share an end with the request (coincident provisional ends)
    const auto& L      = g.layers[l];
    const auto  margin = std::max(L.pitch, keep[l] + 2 * L.width);
    bool        envelope_only = false, exact_ok = false, coincident = false;
    for (const auto& [t, m] : B.busy) {
      if (!free_on(m, lo - margin, hi + margin, net)) {
        continue;
      }
      End_blocker e;
      if (ends_free(B, l, t, lo, hi, &e)) {
        continue;  // cannot happen for a failed pick
      }
      if (e.rules) {
        continue;
      }
      envelope_only = true;
      coincident |= e.a == lo || e.b == hi || e.a == hi || e.b == lo;
      exact_ok |= ends_free(B, l, t, lo, hi, nullptr, true);
    }
    std::lock_guard lock(shadow_mutex);
    ++R.track_fallback_shadow[std::format("{}:{}:failed", g.layers[l].name, kind)];
    R.track_fallback_shadow[std::format("{}:{}:shadow_free", g.layers[l].name, kind)] += ok;
    R.track_fallback_shadow[std::format("{}:{}:envelope_only", g.layers[l].name, kind)] += envelope_only;
    R.track_fallback_shadow[std::format("{}:{}:envelope_coincident", g.layers[l].name, kind)] += coincident;
    R.track_fallback_shadow[std::format("{}:{}:exact_rules_free", g.layers[l].name, kind)] += exact_ok;
  };
  // l5_fallback_shadow: per candidate track of a failed final request, the deciding predicate and its blocker, with
  // the blocker's contribution provenance (sequential kinds only, the first few per layer and kind: deterministic)
  const auto state_name = [](uint8_t st) -> const char* {
    switch (st) {
      case uint8_t(Track_state::checked): return "checked";
      case uint8_t(Track_state::prescribed): return "prescribed";
      case uint8_t(Track_state::fallback): return "fallback";
      case 255: return "hard";
      default: return "none";
    }
  };
  std::map<std::string, int> sampled;
  const auto sample = [&](Band& B, int l, int64_t lo, int64_t hi, int net, const char* kind, const std::string& ends) {
    if (!B.ledger || sampled[std::format("{}:{}", g.layers[l].name, kind)]++ >= 6) {
      return;
    }
    const auto& L      = g.layers[l];
    const auto  margin = std::max(L.pitch, keep[l] + 2 * L.width);
    std::string out    = std::format("{} {} net {} tracks {}..{} span [{},{}] ends {}:", g.layers[l].name, kind, net,
                                          B.busy.empty() ? 0 : B.busy.begin()->first, B.busy.empty() ? 0 : B.busy.rbegin()->first,
                                          lo, hi, ends);
    for (const auto& [t, m] : B.busy) {
      std::string why;
      if (const auto c = B.ledger->contributions.find(t); c != B.ledger->contributions.end()) {
        for (const auto& k : c->second) {
          if (net >= 0 && k.net == net) {
            continue;
          }
          if (k.lo < hi && lo < k.hi) {
            why = std::format("span {} [{},{}] {}", k.net, k.lo, k.hi, state_name(k.state));
            break;
          }
          if (why.empty() && k.lo < hi + margin && lo - margin < k.hi) {
            why = std::format("margin {} [{},{}] {}", k.net, k.lo, k.hi, state_name(k.state));
          }
        }
      }
      if (why.empty() && free_on(m, lo - margin, hi + margin, net)) {
        End_blocker e;
        if (!ends_free(B, l, t, lo, hi, &e)) {
          std::string states;
          if (const auto c = B.ledger->contributions.find(e.track); c != B.ledger->contributions.end()) {
            std::set<std::string> seen;
            for (const auto& k : c->second) {
              if (k.net == e.owner && k.lo <= e.b && e.a <= k.hi && seen.insert(state_name(k.state)).second) {
                states += (states.empty() ? "" : "+") + std::string(state_name(k.state));
              }
            }
          }
          why = std::format("{} t{:+} {} [{},{}] {}", e.rules ? "rules" : "envelope", e.track - t, e.owner, e.a, e.b, states);
        } else {
          why = "free";
        }
      } else if (why.empty()) {
        why = "blocked (merged)";
      }
      out += std::format(" t{}={};", t, why);
    }
    std::lock_guard lock(shadow_mutex);
    R.track_failure_samples.push_back(std::move(out));
  };
  // phase 1: fixed tracks (virtual pin hops) and the clock spine, every band
  for (auto& s : R.segments) {
    if (s.track < 0 && !s.clock) {
      continue;
    }
    auto&      B  = band_of(s.layer, s.band);
    const auto lo = tile_lo(s, s.lo), hi = tile_hi(s, s.hi);
    if (s.track < 0) {
      const auto [t, ok]
          = pick(B, s.layer, lo, hi, s.net, true, (g.tracks(s.layer, s.band).first + g.tracks(s.layer, s.band).second) / 2);
      if (!ok) {
        probe(B, s.layer, lo, hi, s.net, true, (g.tracks(s.layer, s.band).first + g.tracks(s.layer, s.band).second) / 2, "fixed");
      }
      s.track      = t;
      s.track_state = track_state(ok);
      unassigned  += !ok;
      failures[0] += !ok;
    } else {
      s.track_state = uint8_t(Track_state::prescribed);  // a virtual pin hop's fixed track: not checked by pick
    }
    occupy(B, s.track, lo, hi, s.net, s.track_state);
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
          R.trees[s.net].nodes[node].riser_state = s.track_state;
        }
      }
    }
  };
  // Fixed hops and clock segments skip run_phase(). Their vertical arrivals
  // still define the attachment coordinates used by horizontal wires and taps.
  for (int si = 0; si < int(R.segments.size()); ++si) {
    set_riser(si);
  }
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
              px = std::llround(physical_pin_access(d, g, p).x);
              break;
            }
            if (p.comp >= 0 && pi == node.pins.front()) {
              px = d.cell[p.comp] >= 0 ? d.fp.components[p.comp].place.x + std::llround(d.macro[p.comp]->width * g.dbu / 2)
                                       : std::llround(physical_pin_access(d, g, p).x);
            }
          }
          const auto pref      = int64_t(std::lround(double(px - g.layers[l].offset) / g.layers[l].pitch));
          const auto [t, ok]   = pick(B, l, g.bottom(row), g.bottom(row) + g.tile_h, risers[ri].net, false, pref);
          if (!ok) {
            probe(B, l, g.bottom(row), g.bottom(row) + g.tile_h, risers[ri].net, false, pref, "riser");
          }
          node.riser_layer    = l;
          node.riser_track    = t;
          node.riser_state    = track_state(ok);
          unassigned         += !ok;
          failures[1]         += !ok;
          occupy(B, t, g.bottom(row), g.bottom(row) + g.tile_h, risers[ri].net, node.riser_state);
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
        const auto [t, ok] = pick(B, l, lo, hi, s.net, s.critical, mid);
        if (!ok) {  // retried on the other layers of its direction after this pass
          probe(B, l, lo, hi, s.net, s.critical, mid, "segment_first");
          std::lock_guard lock(failed_mutex);
          failed.push_back({si, lo, hi});
          continue;
        }
        s.track       = t;
        s.track_state = uint8_t(Track_state::checked);
        occupy(B, t, lo, hi, s.net, s.track_state);
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
        const auto [t, ok] = pick(B, l, lo, hi, s.net, s.critical, (a + e) / 2);
        if (ok) {
          s.layer    = l;
          s.track       = t;
          s.track_state = uint8_t(Track_state::checked);
          occupy(B, t, lo, hi, s.net, s.track_state);
          placed = true;
          break;
        }
        probe(B, l, lo, hi, s.net, s.critical, (a + e) / 2, "segment_other_layer");
      }
      if (!placed) {
        auto& B                       = band_of(s.layer, s.band);
        const auto [a, e]             = g.tracks(s.layer, s.band);
        const auto [t, ok]            = pick(B, s.layer, lo, hi, s.net, s.critical, (a + e) / 2);
        if (!ok) {
          probe(B, s.layer, lo, hi, s.net, s.critical, (a + e) / 2, "segment_final");
          if (B.ledger) {
            std::string ends = "tile";
            if (horizontal) {  // where the exact ends came from: a node riser, a bend partner, or the tile edge
              ends.clear();
              for (int node : {s.node_lo, s.node_hi}) {
                ends += ends.empty() ? "" : "/";
                if (node >= 0 && R.trees[s.net].nodes[node].riser_track >= 0) {
                  ends += std::string("riser-") + state_name(R.trees[s.net].nodes[node].riser_state);
                } else if (node < 0 && s.conn >= 0) {
                  ends += "bend";
                } else {
                  ends += "tile";
                }
              }
            }
            sample(B, s.layer, lo, hi, s.net, "segment_final", ends);
          }
        }
        s.track                       = t;
        s.track_state                 = track_state(ok);
        unassigned                   += !ok;
        failures[horizontal ? 3 : 2] += !ok;
        occupy(B, t, lo, hi, s.net, s.track_state);
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
      const auto [t, ok]   = pick(B, low_h, std::min(xv, xr), std::max(xv, xr), s.net, false, (a + b) / 2);
      if (!ok) {
        probe(B, low_h, std::min(xv, xr), std::max(xv, xr), s.net, false, (a + b) / 2, "jog");
        sample(B, low_h, std::min(xv, xr), std::max(xv, xr), s.net, "jog",
               std::format("seg-{}/riser-{}", state_name(s.track_state), state_name(nd.riser_state)));
      }
      unassigned         += !ok;
      failures[4]         += !ok;
      occupy(B, t, std::min(xv, xr), std::max(xv, xr), s.net, track_state(ok));
      (e ? s.jog_hi : s.jog_lo)             = t;
      (e ? s.jog_hi_state : s.jog_lo_state) = track_state(ok);
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
        const auto   access = physical_pin_access(d, g, p);
        const double px = access.x, py = access.y;
        const int    row = g.row_of(py);
        auto&        B   = band_of(low_h, row);
        const auto   xr  = g.track_coord(nd.riser_layer, nd.riser_track);
        const auto lo = std::min(int64_t(px), xr), hi = std::max(int64_t(px), xr);
        const auto pref     = int64_t(std::lround((py - g.layers[low_h].offset) / g.layers[low_h].pitch));
        const auto [t, ok]   = pick(B, low_h, lo, hi, n, false, pref);
        if (!ok) {
          probe(B, low_h, lo, hi, n, false, pref, "io_stub");
          sample(B, low_h, lo, hi, n, "io_stub", std::format("pin/riser-{}", state_name(nd.riser_state)));
        }
        unassigned         += !ok;
        failures[5]         += !ok;
        occupy(B, t, lo, hi, n, track_state(ok));
        nd.stub_track = t;
        nd.stub_state = track_state(ok);
        break;  // one stub per node: the node's other non-box pins share it
      }
    }
  }
  run_phase(true);
  R.unassigned = unassigned;
  if (shadow_on) {  // the ledger must reproduce the real occupancy exactly (every occupy call went through it)
    size_t bands_checked = 0, mismatched = 0;
    for (size_t l = 0; l < bands.size(); ++l) {
      for (const auto& [b, B] : bands[l]) {
        ++bands_checked;
        bool same = B.ledger && B.ledger->busy.size() == B.busy.size();
        for (auto it = B.busy.begin(); same && it != B.busy.end(); ++it) {
          const auto q = B.ledger->busy.find(it->first);
          same = q != B.ledger->busy.end() && q->second.all == it->second.all && q->second.owners == it->second.owners;
        }
        mismatched += !same;
      }
    }
    R.track_fallback_shadow["ledger:bands"]      = bands_checked;
    R.track_fallback_shadow["ledger:mismatched"] = mismatched;
  }
  const std::array<const char*, 6> names{"fixed", "riser", "vertical", "horizontal", "jog", "io_stub"};
  for (size_t i = 0; i < names.size(); ++i) {
    R.track_failures[names[i]] = failures[i].load();
  }
  // failed pick attempts by the deepest filter any candidate reached (not final segments: one segment can be
  // rejected on several layers and still be rescued)
  const std::array<const char*, 5> classes{"no_track", "hard_or_reserved", "other_net", "end_margin", "neighbor_spacing"};
  for (size_t l = 0; l < g.layers.size(); ++l) {
    for (size_t c = 0; c < classes.size(); ++c) {
      if (const auto n = rejects[l][c].load()) {
        R.track_pick_rejections[std::format("{}:{}", g.layers[l].name, classes[c])] = n;
      }
    }
  }
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
