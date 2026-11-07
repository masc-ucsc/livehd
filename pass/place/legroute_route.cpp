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
#include <unordered_set>

#include "inou/lefdef/routing_occupancy.hpp"
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
  auto [cap_h, cap_v] = transit_capacity(g);
  R.cap_h             = std::move(cap_h);
  R.cap_v             = std::move(cap_v);
  R.dem_h.assign(g.size(), 0);
  R.dem_v.assign(g.size(), 0);
  R.hist_h.assign(g.size(), 0);
  R.hist_v.assign(g.size(), 0);
  R.layer_dem.assign(g.layers.size(), std::vector<int32_t>(g.size(), 0));
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
  if (d.opt.paired_pin_reserve) {
    return;  // The shared per-box resolver already charged the exact experimental M2/M3 allowance.
  }
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
    std::map<int, std::vector<int>> per;    // region -> flop unit tiles (boxes once each)
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
using Spans                  = std::map<int64_t, int64_t>;
constexpr int kObstacleOwner = -1;
constexpr int kReservedOwner = -2;
void          merge_span(Spans& spans, int64_t lo, int64_t hi) {
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
  std::map<int, Spans> owners;  // net -> union of its spans; negative owners distinguish metal from empty reservations
  void                 occupy(int net, int64_t lo, int64_t hi) {
    merge_span(all, lo, hi);
    merge_span(owners[net], lo, hi);
  }
};
struct Band {
  std::vector<int>         segs;
  std::map<int64_t, Track> busy;
  std::map<int64_t, Spans> hard;  // power metal, macros and reserved tracks (never shared)
  int                      layer = -1, index = -1;
  bool                     shadow_view = false;
  Band*                    shadow      = nullptr;  // l5_fallback_shadow: every contribution except FALLBACK ones
  Band*                    ledger      = nullptr;  // l5_fallback_shadow: every contribution (checked against busy)
  struct Contribution {
    int     net;
    int64_t lo, hi;
    uint8_t state;         // Track_state; 255 marks hard/reserved initialization
    int     request = -1;  // regular H identity, or immutable background (-1) / IO transaction (<= -2)
  };
  std::map<int64_t, std::vector<Contribution>> contributions;  // shadow ledger, or opt-in H repair provenance
  struct Request {
    int     segment, net;
    int64_t lo, hi, track;
    uint8_t state;
  };
  std::vector<Request> requests;  // opt-in repair only; exact regular H requests, including immutable fallbacks
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
  using Destinations = std::array<int, 2>;  // actual endpoint box identities, never all boxes incident to a net
  struct Allowance_state {
    std::vector<int64_t>      physical, eligible;
    unsigned                  required = 0;  // authoritative catalog budget: nominal paired tracks plus separate internal estimate
    std::set<int>             destination_nets;
    std::map<int64_t, size_t> foreign, shadow_foreign;
  };
  std::vector<Allowance_state> allowance(d.opt.paired_pin_reserve ? g.paired_pin_reservations.size() : 0);
  for (size_t i = 0; i < allowance.size(); ++i) {
    const auto& reservation = g.paired_pin_reservations[i];
    const auto& catalog     = g.resources.layers.at(size_t(reservation.vertical_layer));
    const auto& tile        = catalog.tiles.at(size_t(reservation.tile));
    auto&       state       = allowance[i];
    state.physical          = catalog.bands.at(tile.band);
    state.required          = tile.cellbox_budget;
    if (!tile.whole_band_exclusions) {
      const auto& coordinates = catalog.bands.at(tile.band);
      for (size_t k = 0; k < coordinates.size(); ++k) {
        if (std::ranges::none_of(tile.exclusions, [&](const auto& exclusion) { return exclusion.track == k; })) {
          state.eligible.push_back(coordinates[k]);
        }
      }
    }
    for (const auto& pin : reservation.external_pins) {
      state.destination_nets.insert(pin.net);
    }
    state.destination_nets.insert(reservation.internal_nets.begin(), reservation.internal_nets.end());
  }
  const auto segment_destinations = [&](const Segment& segment) {
    Destinations out{-1, -1};
    if (!d.opt.paired_pin_reserve) {
      return out;
    }
    for (int e = 0; e < 2; ++e) {
      const int node = e ? segment.node_hi : segment.node_lo;
      if (node < 0) {
        continue;
      }
      const auto& endpoint = R.trees[segment.net].nodes[node];
      const int   along    = e ? segment.hi : segment.lo;
      const int   tile     = g.layers[segment.layer].horizontal ? g.tile(along, segment.band) : g.tile(segment.band, along);
      if (endpoint.tile == tile) {
        out[e] = endpoint.box;  // this piece terminates inside the named box; passing through the tile is insufficient
      }
    }
    return out;
  };
  enum class Allowance_slot { unrepresented, excluded, destination, foreign };
  const auto visit_allowance
      = [&](int layer, int band, int64_t track, int64_t lo, int64_t hi, int net, Destinations destinations, const auto& visit) {
          if (!d.opt.paired_pin_reserve || track < 0 || g.layers[layer].horizontal) {
            return;
          }
          const auto coordinate = g.track_coord(layer, track);
          const int  first = g.row_of(lo), last = g.row_of(hi > lo ? hi - 1 : hi);
          for (int row = first; row <= last; ++row) {
            const int tile = g.tile(band, row);
            const int id   = g.paired_pin_reservation_of_tile.at(size_t(tile));
            if (id < 0) {
              continue;
            }
            const auto& reservation = g.paired_pin_reservations[size_t(id)];
            const auto& state       = allowance[size_t(id)];
            if (reservation.vertical_layer != layer) {
              continue;
            }
            Allowance_slot slot = Allowance_slot::foreign;
            if (!std::ranges::binary_search(state.physical, coordinate)) {
              slot = Allowance_slot::unrepresented;  // Grid::tracks must not extrapolate beyond the finite catalog.
            } else if (!std::ranges::binary_search(state.eligible, coordinate)) {
              slot = Allowance_slot::excluded;  // reject the choice, but never deduct an already excluded PG track twice
            } else if ((destinations[0] == reservation.box || destinations[1] == reservation.box)
                       && state.destination_nets.contains(net)) {
              slot = Allowance_slot::destination;
            }
            visit(size_t(id), coordinate, slot);
          }
        };
  const auto allowance_free = [&](const Band& band, int64_t track, int64_t lo, int64_t hi, int net, Destinations destinations) {
    if (d.opt.paired_pin_reserve && track >= 0) {
      // Enforce the represented physical universe even on transit-only tiles and upper H/V layers.
      const auto& catalog    = g.resources.layers.at(size_t(band.layer));
      const bool  horizontal = g.layers[band.layer].horizontal;
      const auto  coordinate = g.track_coord(band.layer, track);
      const int   first      = horizontal ? g.col_of(lo) : g.row_of(lo);
      const int   last       = horizontal ? g.col_of(hi > lo ? hi - 1 : hi) : g.row_of(hi > lo ? hi - 1 : hi);
      for (int along = first; along <= last; ++along) {
        const auto  tile        = size_t(horizontal ? g.tile(along, band.index) : g.tile(band.index, along));
        const auto& resource    = catalog.tiles.at(tile);
        const auto& coordinates = catalog.bands.at(resource.band);
        const auto  at          = std::ranges::lower_bound(coordinates, coordinate);
        if (resource.whole_band_exclusions || at == coordinates.end() || *at != coordinate) {
          return false;
        }
        const auto index = size_t(at - coordinates.begin());
        if (std::ranges::any_of(resource.exclusions, [&](const auto& exclusion) { return exclusion.track == index; })) {
          return false;
        }
      }
    }
    bool free = true;
    visit_allowance(band.layer,
                    band.index,
                    track,
                    lo,
                    hi,
                    net,
                    destinations,
                    [&](size_t id, int64_t coordinate, Allowance_slot slot) {
                      if (slot != Allowance_slot::foreign) {
                        free &= slot == Allowance_slot::destination;
                        return;
                      }
                      const auto& reservation  = g.paired_pin_reservations[id];
                      const auto& used         = band.shadow_view ? allowance[id].shadow_foreign : allowance[id].foreign;
                      const auto  after        = used.size() + !used.contains(coordinate);
                      free                    &= reservation.vertical_usable >= allowance[id].required
                                                 && after <= reservation.vertical_usable - allowance[id].required;
                    });
    return free;
  };
  // per layer and band
  std::vector<std::map<int, Band>> bands(g.layers.size());
  const bool                       shadow_on = d.opt.l5_fallback_shadow;
  std::vector<std::map<int, Band>> shadows(shadow_on ? g.layers.size() : 0), ledgers(shadow_on ? g.layers.size() : 0);
  const auto                       band_of = [&](int l, int b) -> Band& {
    auto [it, fresh] = bands[l].try_emplace(b);
    if (fresh) {
      it->second.layer  = l;
      it->second.index  = b;
      const auto& L     = g.layers[l];
      const auto [a, e] = g.tracks(l, b);
      for (auto t = a; t <= e; ++t) {
        auto&      m = it->second.busy[t];
        const auto x = g.track_coord(l, t);
        if (reservations.selected(L.name, x, x - L.pitch, t - a)) {
          // An empty reserved track excludes this choice, but has no metal/EOL geometry on adjacent tracks.
          m.occupy(kReservedOwner, std::numeric_limits<int64_t>::min() / 2, std::numeric_limits<int64_t>::max() / 2);
          it->second.hard[t] = m.all;
        }
        // Real metal on a reserved track still constrains its neighbors; keep its spans independently.
        for (int oi : g.obstacle_band[l][b]) {
          const auto&  r  = g.obstacles[l][oi];
          const double c0 = L.horizontal ? r.yl : r.xl, c1 = L.horizontal ? r.yh : r.xh;
          if (x > c0 - L.pitch / 2.0 - L.width && x < c1 + L.pitch / 2.0 + L.width) {
            const auto lo = int64_t(L.horizontal ? r.xl : r.yl), hi = int64_t(L.horizontal ? r.xh : r.yh);
            m.occupy(kObstacleOwner, lo, hi);
            merge_span(it->second.hard[t], lo, hi);
          }
        }
      }
      if (d.opt.l5_repair_ms && L.horizontal) {
        for (const auto& [t, track] : it->second.busy) {
          for (const auto& [owner, spans] : track.owners) {
            for (const auto& [lo, hi] : spans) {
              it->second.contributions[t].push_back({owner, lo, hi, 255});
            }
          }
        }
      }
      if (shadow_on) {  // hard and reserved contributions are in both
        auto& S = shadows[l][b];
        auto& G = ledgers[l][b];
        S.layer = G.layer = l;
        S.index = G.index = b;
        S.shadow_view     = true;
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
  std::vector<int64_t>               keep(g.layers.size());
  std::vector<char>                  ordinary_spacing(g.layers.size(), 0);
  const physical::Signal_metal_rules rules(d.tech, d.fp);
  for (size_t l = 0; l < g.layers.size(); ++l) {
    const auto clearance = rules.clearance(g.layers[l].name);
    keep[l]              = std::max(g.layers[l].pitch - g.layers[l].width, clearance.value_or(g.layers[l].pitch));
    if (const auto layer = d.tech.layers.find(g.layers[l].name); layer != d.tech.layers.end()) {
      // Without end-specific rules, containment is conservative for spacing/PRL. EOL/corner rules keep the
      // whole-reach endpoint envelope until the joints are placed: an exact check on a provisional span does not
      // certify the shorter base metal final emits (a measured A/B on sha3 also showed no final-closure gain).
      ordinary_spacing[l]
          = clearance && layer->second.rules.properties.empty()
            && std::ranges::none_of(layer->second.rules.spacing, [](const auto& rule) { return rule.eol.has_value(); });
    }
  }
  // Opt-in l5_rule_reach: the end envelope against signal neighbors only scans the track offsets whose edge gap is
  // below Signal_metal_rules::lateral_reach (the largest across-track extent of any rule branch on the layer, every
  // PRL row/length included; a gap equal to a rule distance is legal). A sampled exact-checker scan of nominal pairs
  // can only widen it (a cross-check, never a narrowing). Physical obstacles keep the full query-radius reach.
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
        return physical::Shape{
            L.name,
            L.horizontal ? physical::Rect{double(a), at - half, double(b), at + half}
                         : physical::Rect{at - half, double(a), at + half, double(b)}
        };
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
      signal_reach[l]                                               = reach;
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
  const auto ends_free
      = [&](const Band& B, int l, int64_t t, int64_t lo, int64_t hi, End_blocker* why = nullptr, bool exact = false) {
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
                if (owner == kReservedOwner) {
                  continue;
                }
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
                      return physical::Shape{
                          L.name,
                          L.horizontal ? physical::Rect{double(begin),     at - half, double(end),   at + half}
                                       : physical::Rect{    at - half, double(begin),   at + half, double(end)}
                      };
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
  std::vector<std::array<std::atomic<size_t>, 6>> rejects(g.layers.size());
  const auto                                      pick = [&](Band&        B,
                                                             int          l,
                                                             int64_t      lo,
                                                             int64_t      hi,
                                                             int          net,
                                                             bool         critical,
                                                             int64_t      pref,
                                                             bool         telemetry    = true,
                                                             Destinations destinations = {-1, -1}) -> std::pair<int64_t, bool> {
    const auto&                       L      = g.layers[l];
    const auto                        margin = std::max(L.pitch, keep[l] + 2 * L.width);
    int64_t                           best   = -1;
    std::tuple<int, int64_t, int64_t> key{1 << 30, 0, 0};
    for (auto& [t, m] : B.busy) {
      if (!free_on(m, lo - margin, hi + margin, net) || !ends_free(B, l, t, lo, hi)
          || !allowance_free(B, t, lo, hi, net, destinations)) {
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
      const auto                              mine  = net >= 0 ? m.owners.find(net) : m.owners.end();
      const bool                              share = mine != m.owners.end() && !free_on(mine->second, lo, hi);
      // Pack legal neighbors before opening a gap: preferring distance three over legal distance two can leave
      // two empty tracks that the wires' end envelopes prevent later wires from using. This is a packing heuristic.
      const std::tuple<int, int64_t, int64_t> k{share ? -1 : nb * (critical ? 10 : 1),
                                                std::min<int64_t>(near, 3),
                                                std::abs(t - pref)};
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
      // telemetry: the most permissive rejection among the band's tracks (0 no track, 1 obstacle/reserved span,
      // 2 another net on the span, 3 only the end margin, 4 neighbor spacing, 5 only the destination allowance)
      int cls = 0;
      for (const auto& [t, m] : B.busy) {
        const auto ob = B.hard.find(t);
        const int  c  = ob != B.hard.end() && !free_on(ob->second, lo, hi) ? 1
                        : !free_on(m, lo, hi, net)                         ? 2
                        : !free_on(m, lo - margin, hi + margin, net)       ? 3
                        : !ends_free(B, l, t, lo, hi)                      ? 4
                        : !allowance_free(B, t, lo, hi, net, destinations) ? 5
                                                                           : 4;
        cls           = std::max(cls, c);
      }
      ++rejects[size_t(l)][size_t(cls)];
    }
    // no free track: the least-overlapping one clear of hard obstacles/reservations (counted as unassigned)
    size_t least = SIZE_MAX;
    for (auto& [t, m] : B.busy) {
      if (!allowance_free(B, t, lo, hi, net, destinations)) {
        continue;  // A fallback proposal must not consume another box's promised destination headroom.
      }
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
  const auto                         occupy = [&](Band&        B,
                                                  int64_t      t,
                                                  int64_t      lo,
                                                  int64_t      hi,
                                                  int          net,
                                                  uint8_t      state,
                                                  Destinations destinations = {-1, -1},
                                                  int          request      = -1) {
    if (t >= 0) {
      visit_allowance(B.layer, B.index, t, lo, hi, net, destinations, [&](size_t id, int64_t coordinate, Allowance_slot slot) {
        if (slot != Allowance_slot::foreign) {
          return;
        }
        ++allowance[id].foreign[coordinate];
        if (state != uint8_t(Track_state::fallback)) {
          ++allowance[id].shadow_foreign[coordinate];
        }
      });
      B.busy[t].occupy(net, lo, hi);
      if (d.opt.l5_repair_ms && g.layers[B.layer].horizontal) {
        B.contributions[t].push_back({net, lo, hi, state, request});
      }
      if (B.ledger) {
        B.ledger->busy[t].occupy(net, lo, hi);
        B.ledger->contributions[t].push_back({net, lo, hi, state, request});
      }
      if (B.shadow && state != uint8_t(Track_state::fallback)) {
        B.shadow->busy[t].occupy(net, lo, hi);
      }
    }
  };
  const auto occupy_segment = [&](Band& B, int si, int64_t lo, int64_t hi) {
    const auto& segment = R.segments[si];
    if (d.opt.l5_repair_ms && g.layers[B.layer].horizontal) {
      B.requests.push_back({si, segment.net, lo, hi, segment.track, segment.track_state});
    }
    occupy(B, segment.track, lo, hi, segment.net, segment.track_state, segment_destinations(segment), si);
  };
  // l5_fallback_shadow: after a failed pick (before its proposal is inserted), rerun the same predicates on the shadow
  std::mutex shadow_mutex;
  const auto probe = [&](Band&        B,
                         int          l,
                         int64_t      lo,
                         int64_t      hi,
                         int          net,
                         bool         critical,
                         int64_t      pref,
                         const char*  kind,
                         Destinations destinations = {-1, -1}) {
    if (!B.shadow) {
      return;
    }
    const bool  ok            = pick(*B.shadow, l, lo, hi, net, critical, pref, false, destinations).second;
    // the same real band: is some track rejected only by the end envelope, would it pass the exact rules, and does
    // the first envelope blocker share an end with the request (coincident provisional ends)
    const auto& L             = g.layers[l];
    const auto  margin        = std::max(L.pitch, keep[l] + 2 * L.width);
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
      envelope_only  = true;
      coincident    |= e.a == lo || e.b == hi || e.a == hi || e.b == lo;
      exact_ok      |= ends_free(B, l, t, lo, hi, nullptr, true);
    }
    std::lock_guard lock(shadow_mutex);
    ++R.track_fallback_shadow[std::format("{}:{}:failed", g.layers[l].name, kind)];
    R.track_fallback_shadow[std::format("{}:{}:shadow_free", g.layers[l].name, kind)]         += ok;
    R.track_fallback_shadow[std::format("{}:{}:envelope_only", g.layers[l].name, kind)]       += envelope_only;
    R.track_fallback_shadow[std::format("{}:{}:envelope_coincident", g.layers[l].name, kind)] += coincident;
    R.track_fallback_shadow[std::format("{}:{}:exact_rules_free", g.layers[l].name, kind)]    += exact_ok;
  };
  // l5_fallback_shadow: per candidate track of a failed final request, the deciding predicate and its blocker, with
  // the blocker's contribution provenance (sequential kinds only, the first few per layer and kind: deterministic)
  const auto state_name = [](uint8_t st) -> const char* {
    switch (st) {
      case uint8_t(Track_state::checked)   : return "checked";
      case uint8_t(Track_state::prescribed): return "prescribed";
      case uint8_t(Track_state::fallback)  : return "fallback";
      case 255                             : return "hard";
      default                              : return "none";
    }
  };
  std::map<std::string, int> sampled;
  const auto                 sample = [&](Band&              B,
                                          int                l,
                                          int64_t            lo,
                                          int64_t            hi,
                                          int                net,
                                          const char*        kind,
                                          const std::string& ends,
                                          Destinations       destinations = {-1, -1}) {
    if (!B.ledger || sampled[std::format("{}:{}", g.layers[l].name, kind)]++ >= 6) {
      return;
    }
    const auto& L      = g.layers[l];
    const auto  margin = std::max(L.pitch, keep[l] + 2 * L.width);
    std::string out    = std::format("{} {} net {} tracks {}..{} span [{},{}] ends {}:",
                                     g.layers[l].name,
                                     kind,
                                     net,
                                     B.busy.empty() ? 0 : B.busy.begin()->first,
                                     B.busy.empty() ? 0 : B.busy.rbegin()->first,
                                     lo,
                                     hi,
                                     ends);
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
          why = allowance_free(B, t, lo, hi, net, destinations) ? "free" : "cellbox destination allowance";
        }
      } else if (why.empty()) {
        why = "blocked (merged)";
      }
      out += std::format(" t{}={};", t, why);
    }
    std::lock_guard lock(shadow_mutex);
    R.track_failure_samples.push_back(std::move(out));
  };
  struct Track_choice {
    int     layer   = -1;
    int64_t track   = -1;
    bool    checked = false;
  };
  // Check each compatible layer without inserting failed proposals. The lowest layer remains preferred;
  // only the final choice is published and occupied, so a failed lower-layer attempt cannot block a retry.
  const auto choose_direction = [&](bool    horizontal,
                                    int     band,
                                    int64_t lo,
                                    int64_t hi,
                                    int     net,
                                    std::optional<int64_t>
                                                       preferred_coordinate,
                                    const char*        kind,
                                    const std::string& ends,
                                    Destinations       destinations = {-1, -1}) -> Track_choice {
    Track_choice fallback;
    for (int l = 0; l < int(g.layers.size()); ++l) {
      const auto& L = g.layers[l];
      if (L.horizontal != horizontal) {
        continue;
      }
      auto& B           = band_of(l, band);
      const auto [a, b] = g.tracks(l, band);
      const auto pref
          = preferred_coordinate ? int64_t(std::lround(double(*preferred_coordinate - L.offset) / L.pitch)) : (a + b) / 2;
      const auto [t, ok] = pick(B, l, lo, hi, net, false, pref, true, destinations);
      if (ok) {
        return {l, t, true};
      }
      if (fallback.layer < 0) {
        fallback = {l, t, false};
      }
      probe(B, l, lo, hi, net, false, pref, kind, destinations);
    }
    if (fallback.layer < 0) {
      throw std::logic_error("legroute: attachment has no compatible routing layer");
    }
    sample(band_of(fallback.layer, band), fallback.layer, lo, hi, net, kind, ends, destinations);
    return fallback;
  };
  std::optional<Clock::time_point>     repair_deadline;
  std::map<int, Planned_wire_geometry> repair_geometry;
  // First test stable H endpoint geometry, then search a small closure with the original conservative predicates.
  // Fixed hops, auxiliary wires and fallback proposals stay immutable. Exhaustion is conditional on that domain.
  const auto                           repair_horizontal = [&](Band& home, int target, int64_t lo, int64_t hi) {
    if (!d.opt.l5_repair_ms) {
      return false;
    }
    if (!repair_deadline) {
      repair_deadline = Clock::now() + std::chrono::milliseconds(std::min(d.opt.l5_repair_ms, 55000u));
    }
    ++R.l5_repair_attempts;
    bool       cutoff   = false;
    size_t     explored = 0;
    const auto expired  = [&] {
      cutoff |= Clock::now() >= *repair_deadline;
      return cutoff;
    };
    const auto unknown = [&] {
      ++R.l5_repair_unknown;
      return false;
    };
    if (expired() || home.busy.size() > 64) {
      return unknown();
    }
    std::vector<int> candidate_layers{home.layer};
    bool             exact_layers_complete = true;
    for (int layer = 0; layer < int(g.layers.size()); ++layer) {
      if (layer != home.layer && g.layers[layer].horizontal) {
        candidate_layers.push_back(layer);
      }
    }
    std::stable_sort(candidate_layers.begin() + 1, candidate_layers.end(), [&](int a, int b) {
      return std::pair{std::abs(a - home.layer), a} < std::pair{std::abs(b - home.layer), b};
    });
    // The failed request has not occupied its original band. An alternate H layer changes only the new
    // piece and its join requirements; its risers and the Plan's tile path remain fixed throughout the trial.
    for (int candidate_layer : candidate_layers) {
      auto& B = band_of(candidate_layer, home.index);
      if (expired()) {
        return unknown();
      }
      if (B.busy.size() > 64) {
        exact_layers_complete = false;
        continue;
      }
      const auto& layer   = g.layers[B.layer];
      const auto  margin  = std::max(layer.pitch, keep[B.layer] + 2 * layer.width);
      // A known H piece has final endpoints already fixed by its two risers. Check its canonical emitted
      // rectangle, not the longer provisional envelope used by the diagnostic exact-mode probe. This is only
      // straight-wire allocation: the selected via landing footprints remain an explicit later obligation.
      auto        profile = repair_geometry.find(B.layer);
      if (profile == repair_geometry.end()) {
        profile = repair_geometry.emplace(B.layer, planned_wire_geometry(d.tech, g.dbu, g.die, layer.name)).first;
      }
      const auto stable_shape = [&](int segment, int64_t a, int64_t b, int64_t track) -> std::optional<physical::Shape> {
        const auto& s = R.segments[segment];
        if (s.clock || !g.layers[s.layer].horizontal || (segment != target && s.layer != B.layer) || s.band != B.index
            || s.node_lo < 0 || s.node_hi < 0) {
          return {};
        }
        std::array<int64_t, 2> ends;
        for (int e = 0; e < 2; ++e) {
          const auto& node = R.trees[s.net].nodes[e ? s.node_hi : s.node_lo];
          if (node.riser_track < 0
              || (node.riser_state != uint8_t(Track_state::checked) && node.riser_state != uint8_t(Track_state::prescribed))) {
            return {};
          }
          ends[e] = g.track_coord(node.riser_layer, node.riser_track);
        }
        if (std::min(ends[0], ends[1]) != a || std::max(ends[0], ends[1]) != b) {
          return {};
        }
        const auto shape = planned_wire_shape(profile->second, true, a, b, g.track_coord(B.layer, track));
        // Keep the existing same-track/unknown-background envelope conservative even for minimum-area or
        // die-edge adjustments. Larger extensions require a separate occupancy model and are not repaired here.
        if (shape.rect.xl < a - layer.width || shape.rect.xh > b + layer.width || shape.rect.yh - shape.rect.yl != layer.width
            || !rules.rectangle(shape).legal) {
          return {};
        }
        return shape;
      };
      const auto first_track = g.tracks(B.layer, B.index).first;
      if (stable_shape(target, lo, hi, first_track)) {
        Band unknown_background;
        unknown_background.layer = B.layer;
        unknown_background.index = B.index;
        unknown_background.hard  = B.hard;
        struct Known_wire {
          int             net;
          int64_t         track;
          physical::Shape shape;
        };
        std::vector<Known_wire> known;
        size_t                  contribution_count = 0;
        for (const auto& [track, occupancy] : B.busy) {
          (void)occupancy;
          unknown_background.busy.try_emplace(track);
        }
        for (const auto& [track, contributions] : B.contributions) {
          for (const auto& contribution : contributions) {
            if (expired() || ++contribution_count > 4096) {
              return unknown();
            }
            const auto shape = contribution.request >= 0 && contribution.state == uint8_t(Track_state::checked)
                                   ? stable_shape(contribution.request, contribution.lo, contribution.hi, track)
                                   : std::nullopt;
            if (shape) {
              known.push_back({contribution.net, track, *shape});
            } else if (contribution.net != kObstacleOwner || g.obstacles[B.layer].empty()) {
              unknown_background.busy[track].occupy(contribution.net, contribution.lo, contribution.hi);
            }
            // Real fixed-metal rectangles are checked below at their physical coordinates. Their projections onto
            // neighboring nominal tracks are conservative occupancy, not additional metal with fabricated ends.
          }
        }
        for (const auto& [track, occupancy] : B.busy) {
          if (expired()) {
            return unknown();
          }
          const auto shape = stable_shape(target, lo, hi, track);
          if (!shape || !free_on(occupancy, lo - margin, hi + margin, R.segments[target].net)
              || !ends_free(unknown_background, B.layer, track, lo, hi)
              || !allowance_free(B, track, lo, hi, R.segments[target].net, segment_destinations(R.segments[target]))) {
            continue;
          }
          bool legal = true;
          for (const auto& obstacle : g.obstacles[B.layer]) {
            if (expired()) {
              return unknown();
            }
            if (!rules.spacing(*shape, {layer.name, obstacle}).legal) {
              legal = false;
              break;
            }
          }
          if (!legal) {
            continue;
          }
          for (const auto& wire : known) {
            if (expired()) {
              return unknown();
            }
            if (wire.track == track && wire.net == R.segments[target].net) {
              continue;  // Existing collinear same-net sharing; parallel same-net pieces still obey spacing.
            }
            if (!rules.spacing(*shape, wire.shape).legal) {
              legal = false;
              break;
            }
          }
          if (!legal) {
            continue;
          }
          // Prepare every view before publishing. Existing pieces never move or disappear in this branch.
          const auto net           = R.segments[target].net;
          const auto state         = uint8_t(Track_state::checked);
          auto       busy          = B.busy;
          auto       contributions = B.contributions;
          auto       requests      = B.requests;
          busy.at(track).occupy(net, lo, hi);
          contributions[track].push_back({net, lo, hi, state, target});
          requests.push_back({target, net, lo, hi, track, state});
          auto shadow = B.shadow ? B.shadow->busy : std::map<int64_t, Track>{};
          if (B.shadow) {
            shadow.at(track).occupy(net, lo, hi);
          }
          auto    ledger_busy          = B.ledger ? busy : std::map<int64_t, Track>{};
          auto    ledger_contributions = B.ledger ? contributions : decltype(contributions){};
          auto&   wire_only_repairs    = R.track_fallback_shadow["repair:stable_h_wire_only"];
          size_t* other_layer_repairs  = B.layer != home.layer ? &R.track_fallback_shadow["repair:stable_h_other_layer"] : nullptr;
          if (expired()) {
            return unknown();
          }
          B.busy.swap(busy);
          B.contributions.swap(contributions);
          B.requests.swap(requests);
          if (B.shadow) {
            B.shadow->busy.swap(shadow);
          }
          if (B.ledger) {
            B.ledger->busy.swap(ledger_busy);
            B.ledger->contributions.swap(ledger_contributions);
          }
          R.segments[target].layer       = B.layer;
          R.segments[target].track       = track;
          R.segments[target].track_state = state;
          ++R.l5_repair_successes;
          ++wire_only_repairs;
          if (other_layer_repairs) {
            ++*other_layer_repairs;
          }
          return true;
        }
      }
    }
    auto&                      B      = home;
    const auto&                layer  = g.layers[B.layer];
    const auto                 margin = std::max(layer.pitch, keep[B.layer] + 2 * layer.width);
    std::vector<Band::Request> moving{
        {target, R.segments[target].net, lo, hi, -1, uint8_t(Track_state::checked)}
    };
    std::set<int> selected{target};
    int64_t       left = lo, right = hi;
    bool          changed = true;
    while (changed) {
      changed = false;
      for (const auto& request : B.requests) {
        if (expired()) {
          return unknown();
        }
        if (request.state != uint8_t(Track_state::checked) || selected.contains(request.segment) || request.lo >= right + margin
            || request.hi <= left - margin) {
          continue;
        }
        selected.insert(request.segment);
        moving.push_back(request);
        left    = std::min(left, request.lo);
        right   = std::max(right, request.hi);
        changed = true;
        if (moving.size() > 6) {
          return unknown();
        }
      }
    }
    // Rebuild from exact contributions: subtracting a merged net span would erase retained same-net pieces.
    Band base;
    base.layer = B.layer;
    base.index = B.index;
    base.hard  = B.hard;
    for (const auto& [track, occupancy] : B.busy) {
      (void)occupancy;
      base.busy.try_emplace(track);
    }
    std::set<int> moving_nets;
    bool          complete_domain = true;
    for (const auto& request : moving) {
      complete_domain &= moving_nets.insert(request.net).second;
    }
    for (const auto& [track, entries] : B.contributions) {
      for (const auto& contribution : entries) {
        if (expired()) {
          return unknown();
        }
        if (contribution.request < 0 || !selected.contains(contribution.request)) {
          base.busy[track].occupy(contribution.net, contribution.lo, contribution.hi);
          complete_domain &= !moving_nets.contains(contribution.net);
        }
      }
    }
    // Same-net merging can change an endpoint envelope nonmonotonically. Such a search may find a validated
    // repair, but exhausting its incremental domains is only unknown, never a conditional infeasibility claim.
    const auto legal = [&](const Band& trial, const Band::Request& request, int64_t track) {
      return free_on(trial.busy.at(track), request.lo - margin, request.hi + margin, request.net)
             && ends_free(trial, B.layer, track, request.lo, request.hi)
             && allowance_free(trial,
                               track,
                               request.lo,
                               request.hi,
                               request.net,
                               segment_destinations(R.segments[request.segment]));
    };
    std::vector<std::vector<int64_t>> domains(moving.size());
    const auto [first, last] = g.tracks(B.layer, B.index);
    const auto preferred     = (first + last) / 2;
    for (size_t i = 0; i < moving.size(); ++i) {
      for (const auto& [track, occupancy] : base.busy) {
        (void)occupancy;
        if (expired()) {
          return unknown();
        }
        if (legal(base, moving[i], track)) {
          domains[i].push_back(track);
        }
      }
      std::ranges::stable_sort(domains[i], [&](int64_t a, int64_t b) {
        return std::tuple{a != moving[i].track, std::abs(a - preferred), a}
               < std::tuple{b != moving[i].track, std::abs(b - preferred), b};
      });
    }
    std::vector<size_t> order(moving.size());
    std::iota(order.begin(), order.end(), 0);
    std::ranges::stable_sort(order, [&](size_t a, size_t b) {
      return std::pair{domains[a].size(), moving[a].segment} < std::pair{domains[b].size(), moving[b].segment};
    });
    Band trial;
    trial.layer = B.layer;
    trial.index = B.index;
    trial.hard  = B.hard;
    trial.busy  = base.busy;  // shadow/ledger pointers intentionally remain null in every trial
    std::vector<int64_t> chosen(moving.size(), -1);
    const auto           search = [&](auto&& self, size_t depth) -> bool {
      if (expired() || ++explored > 4096) {
        cutoff = true;
        return false;
      }
      if (depth == order.size()) {
        for (size_t i = 0; i < moving.size(); ++i) {
          Band check;
          check.layer = B.layer;
          check.index = B.index;
          check.hard  = B.hard;
          check.busy  = base.busy;
          for (size_t j = 0; j < moving.size(); ++j) {
            if (j != i) {
              check.busy.at(chosen[j]).occupy(moving[j].net, moving[j].lo, moving[j].hi);
            }
          }
          if (expired() || !legal(check, moving[i], chosen[i])) {
            return false;
          }
        }
        return true;
      }
      const auto  index   = order[depth];
      const auto& request = moving[index];
      for (int64_t track : domains[index]) {
        if (expired()) {
          return false;
        }
        if (!legal(trial, request, track)) {
          continue;
        }
        auto before = trial.busy.at(track);
        trial.busy.at(track).occupy(request.net, request.lo, request.hi);
        chosen[index] = track;
        if (self(self, depth + 1)) {
          return true;
        }
        trial.busy.at(track) = std::move(before);
        chosen[index]        = -1;
        if (cutoff) {
          return false;
        }
      }
      return false;
    };
    if (!search(search, 0)) {
      if (cutoff || !complete_domain || !exact_layers_complete) {
        return unknown();
      }
      ++R.l5_repair_unsat;
      return false;
    }
    auto contributions = B.contributions;
    for (auto& [track, entries] : contributions) {
      (void)track;
      std::erase_if(entries, [&](const auto& entry) { return entry.request >= 0 && selected.contains(entry.request); });
    }
    auto requests = B.requests;
    for (size_t i = 0; i < moving.size(); ++i) {
      auto request  = moving[i];
      request.track = chosen[i];
      contributions[request.track].push_back({request.net, request.lo, request.hi, request.state, request.segment});
      const auto old = std::ranges::find(requests, request.segment, &Band::Request::segment);
      if (old == requests.end()) {
        requests.push_back(request);
      } else {
        *old = request;
      }
    }
    std::map<int64_t, Track> rebuilt, shadow;
    for (const auto& [track, occupancy] : B.busy) {
      (void)occupancy;
      rebuilt.try_emplace(track);
      shadow.try_emplace(track);
    }
    for (const auto& [track, entries] : contributions) {
      for (const auto& entry : entries) {
        if (expired()) {
          return unknown();
        }
        rebuilt[track].occupy(entry.net, entry.lo, entry.hi);
        if (entry.state != uint8_t(Track_state::fallback)) {
          shadow[track].occupy(entry.net, entry.lo, entry.hi);
        }
      }
    }
    auto ledger_busy          = B.ledger ? rebuilt : std::map<int64_t, Track>{};
    auto ledger_contributions = B.ledger ? contributions : decltype(contributions){};
    if (expired()) {
      return unknown();
    }
    // Publish only after the entire proposal passes. All following swaps/primitive assignments are nonthrowing.
    B.busy.swap(rebuilt);
    B.contributions.swap(contributions);
    B.requests.swap(requests);
    if (B.shadow) {
      B.shadow->busy.swap(shadow);
    }
    if (B.ledger) {
      B.ledger->busy.swap(ledger_busy);
      B.ledger->contributions.swap(ledger_contributions);
    }
    for (size_t i = 0; i < moving.size(); ++i) {
      auto& segment       = R.segments[moving[i].segment];
      segment.track       = chosen[i];
      segment.track_state = uint8_t(Track_state::checked);
    }
    ++R.l5_repair_successes;
    return true;
  };
  // phase 1: fixed tracks (virtual pin hops) and the clock spine, every band
  for (auto& s : R.segments) {
    if (s.track < 0 && !s.clock) {
      continue;
    }
    auto&      B  = band_of(s.layer, s.band);
    const auto lo = tile_lo(s, s.lo), hi = tile_hi(s, s.hi);
    if (s.track < 0) {
      const auto [t, ok] = pick(B,
                                s.layer,
                                lo,
                                hi,
                                s.net,
                                true,
                                (g.tracks(s.layer, s.band).first + g.tracks(s.layer, s.band).second) / 2,
                                true,
                                segment_destinations(s));
      if (!ok) {
        probe(B,
              s.layer,
              lo,
              hi,
              s.net,
              true,
              (g.tracks(s.layer, s.band).first + g.tracks(s.layer, s.band).second) / 2,
              "fixed",
              segment_destinations(s));
      }
      s.track        = t;
      s.track_state  = track_state(ok);
      unassigned    += !ok;
      failures[0]   += !ok;
    } else {
      const bool allowed  = allowance_free(B, s.track, lo, hi, s.net, segment_destinations(s));
      s.track_state       = allowed ? uint8_t(Track_state::prescribed) : uint8_t(Track_state::fallback);
      unassigned         += !allowed;
      failures[0]        += !allowed;  // Prescribed geometry is immutable, but cannot claim a passing allowance.
      if (!allowed) {
        sample(B, s.layer, lo, hi, s.net, "fixed", "prescribed", segment_destinations(s));
      }
    }
    occupy(B, s.track, lo, hi, s.net, s.track_state, segment_destinations(s));
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
  // Independent risers settle before any horizontal endpoint is bound. Keep the retries sequential so
  // inserting another layer/band cannot race the parallel vertical-segment pass below.
  for (const auto& r : risers) {
    auto&     node = R.trees[r.net].nodes[r.node];
    const int col = g.col(node.tile), row = g.row(node.tile);
    int64_t   px = g.cx(col);
    for (int pi : node.pins) {
      const auto& p = d.nets[r.net].pins[pi];
      if (p.term >= 0) {
        px = std::llround(physical_pin_access(d, g, p).x);
        break;
      }
      if (p.comp >= 0 && pi == node.pins.front()) {
        px = d.cell[p.comp] >= 0 ? d.fp.components[p.comp].place.x + std::llround(d.macro[p.comp]->width * g.dbu / 2)
                                 : std::llround(physical_pin_access(d, g, p).x);
      }
    }
    const auto lo = g.bottom(row), hi = lo + g.tile_h;
    const auto choice  = choose_direction(false, col, lo, hi, r.net, px, "riser", "tile", {node.box, -1});
    node.riser_layer   = choice.layer;
    node.riser_track   = choice.track;
    node.riser_state   = track_state(choice.checked);
    unassigned        += !choice.checked;
    failures[1]       += !choice.checked;
    occupy(band_of(choice.layer, col), choice.track, lo, hi, r.net, node.riser_state, {node.box, -1});
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
    std::vector<std::pair<int, int>> keys;
    for (auto& [lb, v] : by_band) {
      keys.push_back(lb);
    }
    parallel(keys.size(), threads, [&](size_t k) {
      const auto [l, b] = keys[k];
      auto& B           = bands[l].at(b);
      auto  segs        = by_band.at(keys[k]);
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
        const auto [t, ok] = pick(B, l, lo, hi, s.net, s.critical, mid, true, segment_destinations(s));
        if (!ok) {  // retried on the other layers of its direction after this pass
          probe(B, l, lo, hi, s.net, s.critical, mid, "segment_first", segment_destinations(s));
          std::lock_guard lock(failed_mutex);
          failed.push_back({si, lo, hi});
          continue;
        }
        s.track       = t;
        s.track_state = uint8_t(Track_state::checked);
        occupy_segment(B, si, lo, hi);
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
        const auto [t, ok] = pick(B, l, lo, hi, s.net, s.critical, (a + e) / 2, true, segment_destinations(s));
        if (ok) {
          s.layer       = l;
          s.track       = t;
          s.track_state = uint8_t(Track_state::checked);
          occupy_segment(B, si, lo, hi);
          placed = true;
          break;
        }
        probe(B, l, lo, hi, s.net, s.critical, (a + e) / 2, "segment_other_layer", segment_destinations(s));
      }
      if (!placed && horizontal && d.opt.l5_repair_ms) {
        placed = repair_horizontal(band_of(s.layer, s.band), si, lo, hi);
      }
      if (!placed) {
        auto& B            = band_of(s.layer, s.band);
        const auto [a, e]  = g.tracks(s.layer, s.band);
        const auto [t, ok] = pick(B, s.layer, lo, hi, s.net, s.critical, (a + e) / 2, true, segment_destinations(s));
        if (!ok) {
          probe(B, s.layer, lo, hi, s.net, s.critical, (a + e) / 2, "segment_final", segment_destinations(s));
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
            sample(B, s.layer, lo, hi, s.net, "segment_final", ends, segment_destinations(s));
          }
        }
        s.track                       = t;
        s.track_state                 = track_state(ok);
        unassigned                   += !ok;
        failures[horizontal ? 3 : 2] += !ok;
        occupy_segment(B, si, lo, hi);
      }
      set_riser(si);
    }
    failed.clear();
  };
  run_phase(false);
  // A vertical arrival that is not its node's riser joins it through a checked horizontal jog inside
  // the node tile. Try the lowest horizontal layer first; subsequent horizontal segments see the chosen jog.
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
      const auto xv = g.track_coord(s.layer, s.track), xr = g.track_coord(nd.riser_layer, nd.riser_track);
      const auto lo = std::min(xv, xr), hi = std::max(xv, xr);
      const auto choice = choose_direction(true,
                                           row,
                                           lo,
                                           hi,
                                           s.net,
                                           {},
                                           "jog",
                                           std::format("seg-{}/riser-{}", state_name(s.track_state), state_name(nd.riser_state)));
      (e ? s.jog_hi_layer : s.jog_lo_layer)  = choice.layer;
      (e ? s.jog_hi : s.jog_lo)              = choice.track;
      (e ? s.jog_hi_state : s.jog_lo_state)  = track_state(choice.checked);
      unassigned                            += !choice.checked;
      failures[4]                           += !choice.checked;
      occupy(band_of(choice.layer, row), choice.track, lo, hi, s.net, track_state(choice.checked));
    }
  }
  // Admit a node's H stub and every IO pin-to-stub V stem together. The shared access/shape helpers define
  // exactly the geometry materialization emits; selected via landings remain a separate obligation.
  struct Io_piece {
    int     layer, band;
    int64_t track, lo, hi;
  };
  struct Io_choice {
    int                                 layer   = -1;
    int64_t                             track   = -1;
    bool                                checked = false;
    std::vector<Io_piece>               pieces;
    std::vector<physical::Shape>        shapes;
    std::map<std::pair<int, int>, Band> trial;
  };
  struct Pending_io {
    int                          net, node, request;
    std::vector<Io_piece>        pieces;
    std::vector<physical::Shape> shapes;
  };
  std::vector<Pending_io> pending_io;
  int                     next_io_request = -2;
  for (int n = 0; n < int(R.trees.size()); ++n) {
    for (int node = 0; node < int(R.trees[n].nodes.size()); ++node) {
      auto& nd = R.trees[n].nodes[node];
      if (nd.riser_track < 0) {
        continue;
      }
      struct Access {
        int64_t x, y;
        int     vertical_layer = -1;
        bool    terminal       = false;
      };
      std::vector<Access> accesses;
      const auto          xr = g.track_coord(nd.riser_layer, nd.riser_track);
      int64_t             lo = xr, hi = xr;
      bool                access_ok = true, terminals_only = true;
      for (int pi : nd.pins) {
        const auto& p = d.nets[n].pins[pi];
        if (p.comp >= 0 && d.cell[p.comp] >= 0) {
          continue;
        }
        Access access;
        if (p.term >= 0) {
          const auto physical_access = planned_terminal_access(d, g, d.fp.terminals[size_t(p.term)]);
          if (!physical_access) {
            access_ok = false;
            break;
          }
          access.x         = physical_access->x;
          access.y         = physical_access->y;
          access.terminal  = true;
          const auto layer = std::ranges::find(g.layers, physical_access->access_layer, &Layer::name);
          if (layer != g.layers.end() && !layer->horizontal) {
            access.vertical_layer = int(layer - g.layers.begin());
          }
        } else {
          terminals_only             = false;
          const auto physical_access = physical_pin_access(d, g, p);
          access.x                   = std::llround(physical_access.x);
          access.y                   = std::llround(physical_access.y);
        }
        lo = std::min(lo, access.x);
        hi = std::max(hi, access.x);
        accesses.push_back(access);
      }
      if (!access_ok) {
        nd.stub_layer = -1;
        nd.stub_track = -1;
        nd.stub_state = nd.stub_access_state = uint8_t(Track_state::fallback);
        ++unassigned;
        ++failures[5];
        if (shadow_on && sampled["io_stub_access"]++ < 6) {
          R.track_failure_samples.push_back(std::format("IO stub net {}: no explicit preferred track inside a terminal pin", n));
        }
        continue;
      }
      if (accesses.empty()) {
        continue;
      }
      const int                row         = g.row_of(accesses.front().y);
      int                      first_layer = -1;
      std::optional<Io_choice> selected, fallback;
      for (int layer = 0; layer < int(g.layers.size()) && !selected; ++layer) {
        const auto& horizontal = g.layers[layer];
        if (!horizontal.horizontal) {
          continue;
        }
        if (first_layer < 0) {
          first_layer = layer;
        }
        auto&                hband = band_of(layer, row);
        std::vector<int64_t> candidates;
        for (const auto& [track, occupancy] : hband.busy) {
          (void)occupancy;
          candidates.push_back(track);
        }
        // Short access stems avoid consuming whole pin-side columns merely to compact the H tracks.
        const auto access_length = [&](int64_t track) {
          const auto y     = g.track_coord(layer, track);
          int64_t    total = 0;
          for (const auto& access : accesses) {
            total += std::abs(y - access.y);
          }
          return total;
        };
        std::ranges::stable_sort(candidates, [&](int64_t a, int64_t b) {
          return std::pair{access_length(a), a} < std::pair{access_length(b), b};
        });
        for (int64_t track : candidates) {
          Io_choice candidate;
          candidate.layer                          = layer;
          candidate.track                          = track;
          bool                                safe = true, legal = true;
          std::map<size_t, std::set<int64_t>> new_foreign;
          const auto                          add_piece = [&](const Io_piece& piece) {
            auto [at, fresh] = candidate.trial.try_emplace({piece.layer, piece.band});
            auto& trial      = at->second;
            if (fresh) {
              const auto& real = band_of(piece.layer, piece.band);
              trial.layer      = piece.layer;
              trial.index      = piece.band;
              trial.busy       = real.busy;
              trial.hard       = real.hard;  // No shadow or ledger pointers escape into a trial.
            }
            const auto occupied = trial.busy.find(piece.track);
            if (occupied == trial.busy.end()) {
              safe = false;
              return;
            }
            const auto& metal  = g.layers[piece.layer];
            const auto  margin = std::max(metal.pitch, keep[piece.layer] + 2 * metal.width);
            if (const auto hard = trial.hard.find(piece.track);
                hard != trial.hard.end() && !free_on(hard->second, piece.lo - margin, piece.hi + margin)) {
              safe = false;
            }
            safe &= allowance_free(trial, piece.track, piece.lo, piece.hi, n, {-1, -1});
            visit_allowance(piece.layer,
                            piece.band,
                            piece.track,
                            piece.lo,
                            piece.hi,
                            n,
                            {-1, -1},
                            [&](size_t id, int64_t coordinate, Allowance_slot slot) {
                              if (slot == Allowance_slot::foreign && !allowance[id].foreign.contains(coordinate)) {
                                new_foreign[id].insert(coordinate);
                              }
                            });
            legal &= free_on(occupied->second, piece.lo - margin, piece.hi + margin, n)
                     && ends_free(trial, piece.layer, piece.track, piece.lo, piece.hi);
            occupied->second.occupy(n, piece.lo, piece.hi);
            candidate.pieces.push_back(piece);
          };
          const auto canonical = [&](int l, bool h, int64_t a, int64_t b, int64_t at) {
            auto profile = repair_geometry.find(l);
            if (profile == repair_geometry.end()) {
              profile = repair_geometry.emplace(l, planned_wire_geometry(d.tech, g.dbu, g.die, g.layers[l].name)).first;
            }
            return planned_wire_shape(profile->second, h, a, b, at);
          };
          const auto y    = g.track_coord(layer, track);
          int64_t    h_lo = lo, h_hi = hi;
          for (const auto& access : accesses) {
            const auto hshape = canonical(layer, true, access.x, xr, y);
            if (d.opt.l5_repair_ms) {
              candidate.shapes.push_back(hshape);
            }
            safe  &= hshape.rect.yh - hshape.rect.yl == horizontal.width;
            legal &= g.die.contains(hshape.rect) && rules.rectangle(hshape).legal;
            // Expand the reserved centerline interval when minimum area extends a short piece beyond the
            // normal width-sized envelope. The exact emitted footprint always lies inside this reservation.
            h_lo   = std::min(h_lo, int64_t(std::floor(hshape.rect.xl)) + horizontal.width);
            h_hi   = std::max(h_hi, int64_t(std::ceil(hshape.rect.xh)) - horizontal.width);
            if (!access.terminal || access.y == y) {
              continue;
            }
            if (access.vertical_layer < 0) {
              safe = false;
              continue;
            }
            const auto& vertical = g.layers[access.vertical_layer];
            if ((access.x - vertical.offset) % vertical.pitch != 0) {
              safe = false;  // A finite DEF access on another lattice is not a represented Local track.
              continue;
            }
            const auto vtrack = (access.x - vertical.offset) / vertical.pitch;
            const auto vshape = canonical(access.vertical_layer, false, access.y, y, access.x);
            if (d.opt.l5_repair_ms) {
              candidate.shapes.push_back(vshape);
            }
            safe            &= vshape.rect.xh - vshape.rect.xl == vertical.width;
            legal           &= g.die.contains(vshape.rect) && rules.rectangle(vshape).legal;
            const auto v_lo  = std::min(std::min(access.y, y), int64_t(std::floor(vshape.rect.yl)) + vertical.width);
            const auto v_hi  = std::max(std::max(access.y, y), int64_t(std::ceil(vshape.rect.yh)) - vertical.width);
            add_piece({access.vertical_layer, g.col_of(access.x), vtrack, v_lo, v_hi});
          }
          add_piece({layer, row, track, h_lo, h_hi});
          for (const auto& [id, coordinates] : new_foreign) {
            const auto& reservation = g.paired_pin_reservations[id];
            safe &= reservation.vertical_usable >= allowance[id].required
                    && allowance[id].foreign.size() + coordinates.size() <= reservation.vertical_usable - allowance[id].required;
          }
          if (!safe) {
            continue;
          }
          candidate.checked = legal;
          if (legal) {
            selected = std::move(candidate);
            break;
          }
          if (!fallback) {
            fallback = std::move(candidate);  // Keep an explicit proposal only if every hard/reserved resource is safe.
          }
        }
      }
      if (!selected && fallback) {
        selected = std::move(fallback);
      }
      const bool checked    = selected && selected->checked;
      const auto state      = track_state(checked);
      const int  io_request = d.opt.l5_repair_ms ? next_io_request-- : -1;
      if (selected) {
        struct Commit {
          Band*                                              real;
          std::map<int64_t, Track>                           busy, shadow, ledger_busy;
          std::map<int64_t, std::vector<Band::Contribution>> contributions, ledger_contributions;
        };
        std::map<std::pair<int, int>, Commit>                                             commits;
        std::map<size_t, std::pair<std::map<int64_t, size_t>, std::map<int64_t, size_t>>> foreign;
        for (auto& [key, trial] : selected->trial) {
          auto&  real = band_of(key.first, key.second);
          Commit commit;
          commit.real          = &real;
          commit.busy          = std::move(trial.busy);
          commit.contributions = real.contributions;
          if (real.shadow) {
            commit.shadow = real.shadow->busy;
          }
          if (real.ledger) {
            commit.ledger_busy          = commit.busy;
            commit.ledger_contributions = real.ledger->contributions;
          }
          commits.emplace(key, std::move(commit));
        }
        for (const auto& piece : selected->pieces) {
          auto& commit = commits.at({piece.layer, piece.band});
          if (d.opt.l5_repair_ms && g.layers[piece.layer].horizontal) {
            commit.contributions[piece.track].push_back({n, piece.lo, piece.hi, state, io_request});
          }
          if (commit.real->ledger) {
            commit.ledger_contributions[piece.track].push_back({n, piece.lo, piece.hi, state, io_request});
          }
          if (checked && commit.real->shadow) {
            commit.shadow[piece.track].occupy(n, piece.lo, piece.hi);
          }
          visit_allowance(piece.layer,
                          piece.band,
                          piece.track,
                          piece.lo,
                          piece.hi,
                          n,
                          {-1, -1},
                          [&](size_t id, int64_t coordinate, Allowance_slot slot) {
                            if (slot != Allowance_slot::foreign) {
                              return;
                            }
                            auto [at, fresh] = foreign.try_emplace(id);
                            if (fresh) {
                              at->second = {allowance[id].foreign, allowance[id].shadow_foreign};
                            }
                            ++at->second.first[coordinate];
                            if (checked) {
                              ++at->second.second[coordinate];
                            }
                          });
        }
        // The complete H/V proposal and all accounting views are ready. Publish without allocating or rerouting.
        for (auto& [key, commit] : commits) {
          (void)key;
          commit.real->busy.swap(commit.busy);
          commit.real->contributions.swap(commit.contributions);
          if (commit.real->shadow) {
            commit.real->shadow->busy.swap(commit.shadow);
          }
          if (commit.real->ledger) {
            commit.real->ledger->busy.swap(commit.ledger_busy);
            commit.real->ledger->contributions.swap(commit.ledger_contributions);
          }
        }
        for (auto& [id, counts] : foreign) {
          allowance[id].foreign.swap(counts.first);
          allowance[id].shadow_foreign.swap(counts.second);
        }
      }
      if (d.opt.l5_repair_ms && selected && !checked && terminals_only) {
        pending_io.push_back({n, node, io_request, std::move(selected->pieces), std::move(selected->shapes)});
      }
      nd.stub_layer = selected ? selected->layer : first_layer;
      nd.stub_track = selected ? selected->track : -1;
      nd.stub_state = nd.stub_access_state  = state;
      unassigned                           += !checked;
      failures[5]                          += !checked;
      if (!checked && shadow_on && sampled["io_stub_joint"]++ < 6) {
        R.track_failure_samples.push_back(std::format("IO stub net {}: no jointly checked H stub and V pin access", n));
      }
    }
  }
  run_phase(true);
  // An early IO stem can meet a whole-tile riser envelope even though the settled riser metal ends well
  // before it. Recheck only already reserved, immutable H/V proposals, after every non-IO request passed.
  // This is a metal-only check: no cell access or selected join-via geometry is certified. Clock spines
  // need the caller's actual Boxes; this narrow signal-only repair deliberately leaves them untouched.
  if (d.opt.l5_repair_ms && !pending_io.empty()) {
    if (!repair_deadline) {
      repair_deadline = Clock::now() + std::chrono::milliseconds(std::min(d.opt.l5_repair_ms, 55000u));
    }
    const auto checkpoint = [&] {
      if (Clock::now() >= *repair_deadline) {
        throw std::runtime_error("shared Local repair deadline");
      }
    };
    const auto unknown_sample = [&](const std::string& reason) {
      if (sampled["io_exact_unknown"]++ < 6 && R.track_failure_samples.size() < 64) {
        R.track_failure_samples.push_back("IO exact recheck unknown: " + reason.substr(0, 256));
      }
    };
    try {
      checkpoint();
      size_t nodes = 0;
      for (const auto& tree : R.trees) {
        nodes += tree.nodes.size();
      }
      // Bound pure materialization, which has no cancellation callback. Other failures can leave omitted
      // backbone geometry; do not use such an incomplete scene to upgrade an IO proposal.
      if (!R.spines.empty() || unassigned != pending_io.size() || R.segments.size() > 4096 || d.nets.size() > 4096
          || nodes > 8192) {
        throw std::runtime_error("late IO exact scene outside bounded complete signal-backbone domain");
      }
      const auto planned = final_planned_geometry(d, g, {}, R);
      checkpoint();
      auto work = d.fp;
      for (auto& net : work.nets) {
        if (net.use != "POWER" && net.use != "GROUND") {
          net.wires.clear();
        }
      }
      for (size_t n = 0; n < d.nets.size(); ++n) {
        checkpoint();
        const auto& net   = d.nets[n];
        auto&       frame = work.nets.at(net.frame);
        if (frame.name.empty()) {
          throw std::runtime_error("late IO exact scene has an unnamed net");
        }
        for (const auto& pin : net.pins) {
          if (pin.term >= 0) {
            work.terminals.at(size_t(pin.term)).net = frame.name;
          }
        }
        if (!planned.io_stub_failure[n].empty() && planned.paths[n].empty()) {
          throw std::runtime_error("late IO exact scene has missing terminal geometry");
        }
        if (!net.power) {
          frame.wires.push_back({"ROUTED", planned.paths[n]});
        }
      }
      auto occupancy = physical::routing_occupancy(d.tech, work, work.dbu, checkpoint);
      // Region PG may exist only in the Grid rather than the imported floorplan. Include every real
      // rectangle; duplicate PG in the native index remains a conservative, harmless second check.
      for (size_t layer = 0; layer < g.obstacles.size(); ++layer) {
        for (const auto& obstacle : g.obstacles[layer]) {
          checkpoint();
          occupancy.insert({
              {g.layers[layer].name, obstacle},
              {},
              false
          });
        }
      }
      checkpoint();
      const physical::Signal_metal_rules metal_rules(d.tech, work);
      for (const auto& pending : pending_io) {
        checkpoint();
        ++R.track_fallback_shadow["repair:io_exact_attempts"];
        bool        legal = true, supported = true;
        const auto& name = work.nets.at(d.nets[pending.net].frame).name;
        for (const auto& shape : pending.shapes) {
          checkpoint();
          const auto clearance = metal_rules.clearance(shape.layer);
          if (!clearance) {
            supported = false;
            unknown_sample("unsupported metal rules on " + shape.layer + ": " + metal_rules.rectangle(shape).reason);
            break;
          }
          if (!g.die.contains(shape.rect) || !metal_rules.rectangle(shape).legal) {
            legal = false;
            break;
          }
          for (auto id : occupancy.query(shape.layer, shape.rect, *clearance, name)) {
            checkpoint();
            if (!metal_rules.spacing(shape, occupancy.at(id).shape).legal) {
              legal = false;
              break;
            }
          }
          if (!legal) {
            break;
          }
        }
        if (!supported || !legal) {
          ++R.track_fallback_shadow[supported ? "repair:io_exact_conflicts" : "repair:io_exact_unknown"];
          continue;
        }
        struct Checked_io_commit {
          Band*                                              real;
          std::map<int64_t, Track>                           shadow;
          std::map<int64_t, std::vector<Band::Contribution>> contributions, ledger_contributions;
        };
        std::map<std::pair<int, int>, Checked_io_commit> commits;
        std::map<size_t, std::map<int64_t, size_t>>      shadow_foreign;
        for (const auto& piece : pending.pieces) {
          checkpoint();
          auto& real = band_of(piece.layer, piece.band);
          if (!allowance_free(real, piece.track, piece.lo, piece.hi, pending.net, {-1, -1})) {
            legal = false;
            break;
          }
          auto [at, fresh] = commits.try_emplace({piece.layer, piece.band});
          auto& commit     = at->second;
          if (fresh) {
            commit.real          = &real;
            commit.contributions = real.contributions;
            if (real.shadow) {
              commit.shadow = real.shadow->busy;
            }
            if (real.ledger) {
              commit.ledger_contributions = real.ledger->contributions;
            }
          }
          if (real.shadow) {
            commit.shadow[piece.track].occupy(pending.net, piece.lo, piece.hi);
          }
          visit_allowance(piece.layer,
                          piece.band,
                          piece.track,
                          piece.lo,
                          piece.hi,
                          pending.net,
                          {-1, -1},
                          [&](size_t id, int64_t coordinate, Allowance_slot slot) {
                            if (slot == Allowance_slot::foreign) {
                              auto [entry, inserted] = shadow_foreign.try_emplace(id);
                              if (inserted) {
                                entry->second = allowance[id].shadow_foreign;
                              }
                              ++entry->second[coordinate];
                            }
                          });
        }
        if (!legal) {
          ++R.track_fallback_shadow["repair:io_exact_conflicts"];
          continue;
        }
        for (auto& [key, commit] : commits) {
          (void)key;
          for (auto* contributions : {&commit.contributions, &commit.ledger_contributions}) {
            for (auto& [track, pieces] : *contributions) {
              (void)track;
              checkpoint();
              for (auto& piece : pieces) {
                if (piece.request == pending.request) {
                  piece.state = uint8_t(Track_state::checked);
                }
              }
            }
          }
        }
        auto samples             = R.track_failure_samples;
        bool another_failed_node = false;
        for (size_t node = 0; node < R.trees[pending.net].nodes.size(); ++node) {
          another_failed_node
              |= int(node) != pending.node && R.trees[pending.net].nodes[node].stub_state == uint8_t(Track_state::fallback);
        }
        if (!another_failed_node) {
          std::erase(samples, std::format("IO stub net {}: no jointly checked H stub and V pin access", pending.net));
        }
        auto& successes = R.track_fallback_shadow["repair:io_exact_wire_only"];
        checkpoint();
        // Real busy/foreign and ledger busy already contain these exact accepted H/V reservations. Keep
        // them intact; publish provenance, checked-only occupancy and both node states in one transaction.
        for (auto& [key, commit] : commits) {
          (void)key;
          commit.real->contributions.swap(commit.contributions);
          if (commit.real->shadow) {
            commit.real->shadow->busy.swap(commit.shadow);
          }
          if (commit.real->ledger) {
            commit.real->ledger->contributions.swap(commit.ledger_contributions);
          }
        }
        for (auto& [id, counts] : shadow_foreign) {
          allowance[id].shadow_foreign.swap(counts);
        }
        auto& node      = R.trees[pending.net].nodes[pending.node];
        node.stub_state = node.stub_access_state = uint8_t(Track_state::checked);
        R.track_failure_samples.swap(samples);
        --unassigned;
        --failures[5];
        ++successes;
      }
    } catch (const std::exception& error) {
      ++R.track_fallback_shadow["repair:io_exact_unknown"];
      unknown_sample(error.what());
    }
  }
  R.unassigned                    = unassigned;
  R.paired_allowance_short_tiles  = 0;
  R.paired_allowance_short_tracks = 0;
  for (size_t id = 0; id < allowance.size(); ++id) {
    const auto& reservation          = g.paired_pin_reservations[id];
    const auto  used                 = allowance[id].foreign.size();
    const auto  left                 = reservation.vertical_usable > used ? reservation.vertical_usable - used : size_t(0);
    const auto  missing              = allowance[id].required > left ? allowance[id].required - left : size_t(0);
    R.paired_allowance_short_tiles  += missing != 0;
    R.paired_allowance_short_tracks += missing;
    size_t horizontal_missing        = 0;
    for (int row = 0; row < 2; ++row) {
      horizontal_missing += reservation.horizontal_tracks[row] > reservation.horizontal_usable[row]
                                ? reservation.horizontal_tracks[row] - reservation.horizontal_usable[row]
                                : 0;
    }
    if (reservation.internal_horizontal_estimate) {
      const auto& catalog    = g.resources.layers.back();
      const auto& tile       = catalog.tiles.at(size_t(reservation.tile));
      const auto represented = tile.whole_band_exclusions ? size_t(0) : catalog.bands.at(tile.band).size() - tile.exclusions.size();
      const auto usable      = represented > tile.anonymous_headroom ? represented - tile.anonymous_headroom : size_t(0);
      const auto internal_missing
          = reservation.internal_horizontal_estimate > usable ? reservation.internal_horizontal_estimate - usable : size_t(0);
      horizontal_missing = std::max(horizontal_missing, internal_missing);
    }
    R.paired_allowance_short_tiles  += horizontal_missing != 0;
    R.paired_allowance_short_tracks += horizontal_missing;
  }
  if (shadow_on) {  // the ledger must reproduce the real occupancy exactly (every occupy call went through it)
    size_t bands_checked = 0, mismatched = 0;
    for (size_t l = 0; l < bands.size(); ++l) {
      for (const auto& [b, B] : bands[l]) {
        ++bands_checked;
        bool same = B.ledger && B.ledger->busy.size() == B.busy.size();
        for (auto it = B.busy.begin(); same && it != B.busy.end(); ++it) {
          const auto q = B.ledger->busy.find(it->first);
          same         = q != B.ledger->busy.end() && q->second.all == it->second.all && q->second.owners == it->second.owners;
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
  const std::array<const char*, 6> classes{"no_track",
                                           "hard_or_reserved",
                                           "other_net",
                                           "end_margin",
                                           "neighbor_spacing",
                                           "cellbox_allowance"};
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
