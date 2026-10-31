// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// legroute.final: cells placed inside their box (F1); the global metal (segments, risers, jogs, clock spines) is
// written and checked, then the shared signal router (satlegalize.route) taps each box pin onto its net's metal and
// routes the local nets whole on the low layers (F2/F3); every net is checked for connectivity and the wiring is
// written all or nothing (F4). DRC stays external (F5).
#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <format>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <queue>
#include <set>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <unordered_map>

#include "inou/lefdef/cut_rules.hpp"
#include "inou/lefdef/routing_connectivity.hpp"
#include "inou/lefdef/routing_geometry.hpp"
#include "inou/lefdef/routing_occupancy.hpp"
#include "inou/lefdef/routing_rules.hpp"
#include "legroute_internal.hpp"
#include "pin_escape_guard.hpp"
#include "signal_router.hpp"

namespace livehd::placement::lr {
namespace ph = livehd::physical;
namespace {
// Riser x of each (box, net): where the net's metal over the box comes down to the box layer.
std::vector<std::unordered_map<int, int64_t>> box_risers(const Grid& g, const Boxes& boxes, const Routing& R) {
  std::vector<std::unordered_map<int, int64_t>> out(boxes.boxes.size());
  for (int n = 0; n < int(R.trees.size()); ++n) {
    for (const auto& node : R.trees[n].nodes) {
      if (node.box >= 0 && node.riser_track >= 0) {
        out[node.box][n] = g.track_coord(node.riser_layer, node.riser_track);
      }
    }
  }
  for (const auto& sp : R.spines) {
    for (const auto& [b, seg] : sp.taps) {
      const auto& s = R.segments[seg];
      if (s.track >= 0) {
        out[b][sp.net] = g.track_coord(s.layer, s.track);
      }
    }
  }
  return out;
}

// The metal stack bottom-up and the via between adjacent metals.
struct Stack {
  std::vector<std::string>       metals;
  std::map<std::string, int>     index;
  std::map<std::string, int64_t> width;
  std::map<std::string, double>  area;  // um^2
  int64_t                        grid = 1, dbu = 1000;
  ph::Rect                       die{-1e18, -1e18, 1e18, 1e18};
  Stack(const ph::Technology& tech, int64_t dbu_, const ph::Rect& die_) : dbu(dbu_), die(die_) {
    metals = tech.routing_layers;
    for (size_t k = 0; k < metals.size(); ++k) {
      index[metals[k]] = int(k);
      const auto& L    = tech.layers.at(metals[k]);
      auto        w    = ph::wire_width_dbu(L, dbu);
      width[metals[k]] = w ? *w : std::max<int64_t>(2, std::llround(L.width * dbu) / 2 * 2);
      area[metals[k]]  = L.rules.area;
    }
    if (const auto gr = ph::manufacturing_grid_dbu(tech, dbu)) {
      grid = *gr;
    }
  }
};

struct Geo {
  const Stack&                st;
  std::vector<ph::Route_path> paths;
  size_t                      vias = 0;
  explicit Geo(const Stack& s) : st(s) {}
  void wire_on(const std::string& layer, bool horizontal, int64_t a, int64_t b, int64_t at) {
    if (a > b) {
      std::swap(a, b);
    }
    const int64_t w   = st.width.at(layer);
    int64_t       ext = w / 2;
    // minimum area: stretch short pieces symmetrically, on the manufacturing grid
    if (const double area = st.area.at(layer); area > 0) {
      const auto need = int64_t(std::ceil(area * double(st.dbu) * st.dbu / w));
      if (b - a + 2 * ext < need) {
        const auto half = (need - (b - a) + 1) / 2;  // rounded up, then onto the manufacturing grid
        ext             = (half + st.grid - 1) / st.grid * st.grid;
      }
    }
    // an extension past the die grows inward instead
    int64_t      lo = a - ext, hi = b + ext;
    const double dl = horizontal ? st.die.xl : st.die.yl, dh = horizontal ? st.die.xh : st.die.yh;
    if (double(hi) > dh) {
      lo -= hi - int64_t(std::floor(dh));
      hi  = int64_t(std::floor(dh));
    }
    if (double(lo) < dl) {
      hi += int64_t(std::ceil(dl)) - lo;
      lo  = int64_t(std::ceil(dl));
    }
    ph::Shape s;
    s.layer = layer;
    if (horizontal) {
      s.rect = {double(lo), double(at - w / 2), double(hi), double(at + w / 2)};
    } else {
      s.rect = {double(at - w / 2), double(lo), double(at + w / 2), double(hi)};
    }
    paths.push_back(ph::flush_wire_path(s, horizontal));
  }
  // a connection between two layers at (x, y): the shared router makes it (vias checked against everything placed)
  std::vector<std::tuple<std::string, std::string, int64_t, int64_t>> joins;
  void                                                                stack(int from, int to, int64_t x, int64_t y) {
    if (from != to) {
      joins.emplace_back(st.metals[from], st.metals[to], x, y);
    }
  }
  void stack(const std::string& a, const std::string& b, int64_t x, int64_t y) { stack(st.index.at(a), st.index.at(b), x, y); }
};

int64_t snap(int64_t v, int64_t grid) { return grid > 1 ? (v + grid / 2) / grid * grid : v; }
}  // namespace

void final_place(const Design& d, const Grid& g, const Boxes& boxes, const Routing& R, unsigned threads) {
  const auto risers = box_risers(g, boxes, R);
  parallel(boxes.boxes.size(), threads, [&](size_t bi) {
    const auto&                     b    = boxes.boxes[bi];
    const auto&                     segs = g.segs(b.tile);
    const int                       c = g.col(b.tile), r = g.row(b.tile);
    // current segment of each cell (from the interim packing)
    std::map<int, std::vector<int>> in;
    for (int i : b.cells) {
      const auto& p   = d.fp.components[d.movable[i]].place;
      const int   row = int((p.y - g.bottom(r)) / g.row_h);
      const auto  x   = (p.x - g.left(c)) / g.site;
      for (size_t s = 0; s < segs.size(); ++s) {
        if (segs[s].row == row && x >= segs[s].lo && x < segs[s].hi) {
          in[int(s)].push_back(i);
          break;
        }
      }
    }
    // a cell's pull: the risers of its external nets over this box
    std::unordered_map<int, double> target;
    std::unordered_map<int, int>    inner;  // net -> pins of this box
    for (int i : b.cells) {
      double sx = 0;
      int    k  = 0;
      for (int n : d.nets_of[i]) {
        const auto it = risers[bi].find(n);
        if (it != risers[bi].end()) {
          sx += double(it->second);
          ++k;
        }
        ++inner[n];
      }
      if (k) {
        target[i] = sx / k;
      }
    }
    for (auto& [s, cells] : in) {
      const auto& seg   = segs[s];
      int         total = 0;
      for (int i : cells) {
        total += cell_sites(d, g, i);
      }
      // a site between neighbor cells, then at both segment ends, when the segment has room (pin access)
      const int k         = int(cells.size());
      const int room      = seg.hi - seg.lo;
      const int gap       = total + k - 1 <= room ? 1 : 0;
      const int edge      = total + k + 1 <= room ? 1 : 0;
      total              += gap * (k - 1);
      const auto cost_of  = [&](const std::vector<int>& order, int start) {
        double                                             cost = 0;
        int                                                x    = start;
        std::unordered_map<int, std::pair<double, double>> span;
        for (int i : order) {
          const double center = double(g.left(c) + int64_t(x) * g.site) + d.width[i] / 2.0;
          if (const auto t = target.find(i); t != target.end()) {
            cost += std::abs(center - t->second);
          }
          for (int n : d.nets_of[i]) {
            if (inner[n] > 1) {
              auto [it, fresh]  = span.try_emplace(n, center, center);
              it->second.first  = std::min(it->second.first, center);
              it->second.second = std::max(it->second.second, center);
            }
          }
          x += cell_sites(d, g, i) + gap;
        }
        for (const auto& [_, sp] : span) {
          cost += sp.second - sp.first;
        }
        return cost;
      };
      std::vector<int> order = cells, best_order = cells;
      std::ranges::sort(order, [&](int a, int e) {
        const double ta = target.contains(a) ? target[a] : double(d.fp.components[d.movable[a]].place.x);
        const double te = target.contains(e) ? target[e] : double(d.fp.components[d.movable[e]].place.x);
        return ta < te;
      });
      double     best       = INFINITY;
      int        best_start = seg.lo + edge;
      const auto try_order  = [&](const std::vector<int>& o) {
        for (int start = seg.lo + edge; start <= seg.hi - total - edge; ++start) {
          const double k = cost_of(o, start);
          if (k < best) {
            best       = k;
            best_order = o;
            best_start = start;
          }
        }
      };
      if (order.size() <= 6) {
        std::ranges::sort(order);
        do {
          try_order(order);
        } while (std::next_permutation(order.begin(), order.end()));
      } else {
        try_order(order);
      }
      int x = best_start;
      for (int i : best_order) {
        auto& p  = d.fp.components[d.movable[i]].place;
        p.x      = g.left(c) + int64_t(x) * g.site;
        x       += cell_sites(d, g, i) + gap;
      }
    }
  });
}

void final_wires(const Design& d, const Grid& g, const Boxes& boxes, const Routing& R, const Legroute_regions& regions,
                 Legroute_report& rep) {
  const Stack st(d.tech, d.fp.dbu, d.fp.die);
  const int   low_v = [&] {
    for (int l = 0; l < int(g.layers.size()); ++l) {
      if (!g.layers[l].horizontal) {
        return l;
      }
    }
    return 0;
  }();
  const int low_h = [&] {
    for (int l = 0; l < int(g.layers.size()); ++l) {
      if (g.layers[l].horizontal) {
        return l;
      }
    }
    return 0;
  }();
  const auto       coord = [&](const Segment& s) { return g.track_coord(s.layer, s.track); };
  const auto       lname = [&](int l) -> const std::string& { return g.layers[l].name; };
  std::vector<Geo> net_geo;
  net_geo.reserve(d.nets.size());
  for (size_t n = 0; n < d.nets.size(); ++n) {
    net_geo.emplace_back(st);
  }
  // per (net, node): the y values its riser must reach
  std::vector<std::map<int, std::vector<int64_t>>> reach(d.nets.size());
  // ---- clock spines: trunk, ribs, taps and their joints
  std::map<int, std::vector<const Spine*>>         spines_of;
  for (const auto& sp : R.spines) {
    spines_of[sp.net].push_back(&sp);
  }
  // ---- per net: segments with their bends and node ends, node risers, terminals and fixed pins
  std::vector<std::vector<int>> segs_of(d.nets.size());
  for (int si = 0; si < int(R.segments.size()); ++si) {
    if (!R.segments[si].clock || R.segments[si].conn >= 0 || R.segments[si].node_lo >= 0 || R.segments[si].node_hi >= 0) {
      segs_of[R.segments[si].net].push_back(si);
    }
  }
  std::vector<std::map<int, std::vector<int64_t>>> reach_local = std::move(reach);
  parallel(d.nets.size(), d.opt.threads, [&](size_t n) {
    const auto& T   = R.trees[n];
    auto&       geo = net_geo[n];
    auto&       rch = reach_local[n];
    if (T.nodes.empty() && !spines_of.contains(int(n))) {
      return;
    }
    const auto riser_x   = [&](int node) { return g.track_coord(T.nodes[node].riser_layer, T.nodes[node].riser_track); };
    const auto has_riser = [&](int node) { return node >= 0 && T.nodes[node].riser_track >= 0; };
    // bend partner coordinate of a segment end inside a conn
    const auto partner   = [&](const Segment& s, int si, int k) -> std::optional<int64_t> {
      if (s.conn < 0) {
        return {};
      }
      for (int o : R.conns[s.conn].segs) {
        const auto& p = R.segments[o];
        if (o != si && p.band == k && g.layers[p.layer].horizontal != g.layers[s.layer].horizontal && p.track >= 0) {
          // the partner's band is this end's tile index, and this segment's band lies in the partner's range
          if (s.band >= p.lo && s.band <= p.hi) {
            return coord(p);
          }
        }
      }
      return {};
    };
    for (int si : segs_of[n]) {
      const auto& s = R.segments[si];
      if (s.track < 0) {
        continue;
      }
      const bool    h  = g.layers[s.layer].horizontal;
      const int64_t at = coord(s);
      int64_t       ends[2];
      for (int e = 0; e < 2; ++e) {
        const int k    = e ? s.hi : s.lo;
        const int node = e ? s.node_hi : s.node_lo;
        const int tile = h ? g.tile(k, s.band) : g.tile(s.band, k);
        if (node >= 0 && has_riser(node)) {
          if (h) {
            ends[e] = riser_x(node);
            geo.stack(lname(s.layer), lname(T.nodes[node].riser_layer), ends[e], at);
            rch[node].push_back(at);
          } else if (T.nodes[node].riser_layer == s.layer && T.nodes[node].riser_track == s.track) {
            ends[e] = g.cy(g.row(tile));  // the riser itself: it spans the node tile
            rch[node].push_back(ends[e]);
          } else {
            // the jog L7 put on the lowest horizontal layer of the node tile
            const auto jt = e ? s.jog_hi : s.jog_lo;
            const auto yj = g.track_coord(low_h, jt >= 0 ? jt : g.tracks(low_h, g.row(tile)).first);
            ends[e]       = yj;
            geo.wire_on(lname(low_h), true, at, riser_x(node), yj);
            geo.stack(lname(s.layer), lname(low_h), at, yj);
            geo.stack(lname(low_h), lname(T.nodes[node].riser_layer), riser_x(node), yj);
            rch[node].push_back(yj);
          }
        } else if (const auto p = partner(s, si, k)) {
          ends[e] = *p;
          if (h) {  // one via stack per bend, from the horizontal side
            for (int o : R.conns[s.conn].segs) {
              const auto& q = R.segments[o];
              if (o != si && q.band == k && !g.layers[q.layer].horizontal && q.track >= 0 && s.band >= q.lo && s.band <= q.hi) {
                geo.stack(lname(s.layer), lname(q.layer), *p, at);
                break;
              }
            }
          }
        } else {
          ends[e] = h ? g.cx(k) : g.cy(k);
        }
      }
      geo.wire_on(lname(s.layer), h, ends[0], ends[1], at);
    }
    // die IO terminals: a stub joins the node riser (cell pins, boxed or fixed, tap with the router)
    for (int k = 0; k < int(T.nodes.size()); ++k) {
      const auto& node = T.nodes[k];
      if (!has_riser(k)) {
        continue;
      }
      for (int pi : node.pins) {
        const auto& p = d.nets[n].pins[pi];
        if (p.comp >= 0) {
          continue;
        }
        std::optional<ph::Shape> s;
        if (p.term >= 0) {
          const auto& t = d.fp.terminals[p.term];
          if (!t.shapes.empty()) {
            const auto& sh = t.shapes.front();
            s              = ph::Shape{
                sh.layer,
                {t.x + sh.rect.xl, t.y + sh.rect.yl, t.x + sh.rect.xh, t.y + sh.rect.yh}
            };
          }
        }
        if (!s || !st.index.contains(s->layer)) {
          continue;
        }
        const auto  px = snap(std::llround((s->rect.xl + s->rect.xh) / 2), st.grid);
        const auto  py = snap(std::llround((s->rect.yl + s->rect.yh) / 2), st.grid);
        const auto  rx = riser_x(k);
        // pin -> lowest vertical layer -> up or down to the stub track (L7) -> across on the lowest horizontal layer
        // -> the riser
        const auto  ys = node.stub_track >= 0 ? g.track_coord(low_h, node.stub_track) : py;
        const auto& hl = lname(low_h);
        const auto& vl = lname(low_v);
        geo.stack(s->layer, vl, px, py);
        if (ys != py) {
          geo.wire_on(vl, false, py, ys, px);
        }
        geo.stack(vl, hl, px, ys);
        geo.wire_on(hl, true, px, rx, ys);
        geo.stack(hl, lname(node.riser_layer), rx, ys);
        rch[k].push_back(ys);
      }
    }
    // risers: from the lowest to the highest y any attachment needs, at the riser track
    for (const auto& [node, ys] : rch) {
      if (!has_riser(node) || ys.empty()) {
        continue;
      }
      const auto [lo, hi] = std::minmax_element(ys.begin(), ys.end());
      const int  tile     = T.nodes[node].tile;
      const auto ylo      = std::min(*lo, g.bottom(g.row(tile)) + g.tile_h / 4),
                 yhi      = std::max(*hi, g.bottom(g.row(tile)) + 3 * g.tile_h / 4);
      geo.wire_on(lname(T.nodes[node].riser_layer), false, ylo, yhi, riser_x(node));
    }
    // clock spines of this net
    if (const auto it = spines_of.find(int(n)); it != spines_of.end()) {
      for (const auto* sp : it->second) {
        const auto& trunk = R.segments[sp->trunk];
        if (trunk.track < 0) {
          continue;
        }
        const auto tx  = coord(trunk);
        int64_t    ylo = INT64_MAX, yhi = INT64_MIN;
        for (int ri : sp->ribs) {
          const auto& rib = R.segments[ri];
          if (rib.track < 0) {
            continue;
          }
          const auto ry = coord(rib);
          ylo           = std::min(ylo, ry);
          yhi           = std::max(yhi, ry);
          int64_t xlo = tx, xhi = tx;
          for (const auto& [b, ts] : sp->taps) {
            const auto& tap = R.segments[ts];
            if (tap.track < 0 || (tap.lo != rib.band && tap.hi != rib.band)) {
              continue;
            }
            const auto x = coord(tap);
            xlo          = std::min(xlo, x);
            xhi          = std::max(xhi, x);
            geo.stack(lname(tap.layer), lname(rib.layer), x, ry);
          }
          // the clock tree enters on the middle rib through the entry node's riser
          if (sp->node >= 0 && has_riser(sp->node) && g.row(T.nodes[sp->node].tile) == rib.band) {
            const auto rx = riser_x(sp->node);
            xlo           = std::min(xlo, rx);
            xhi           = std::max(xhi, rx);
            geo.stack(lname(T.nodes[sp->node].riser_layer), lname(rib.layer), rx, ry);
            geo.wire_on(lname(T.nodes[sp->node].riser_layer),
                        false,
                        std::min(ry, g.cy(trunk.band >= 0 ? rib.band : 0)),
                        std::max(ry, g.cy(rib.band)),
                        rx);
          }
          geo.wire_on(lname(rib.layer), true, xlo, xhi, ry);
          geo.stack(lname(rib.layer), lname(trunk.layer), tx, ry);
        }
        if (yhi > ylo) {
          geo.wire_on(lname(trunk.layer), false, ylo, yhi, tx);
        }
        // taps: from the box's tap point (in-box route) up to the rib
        for (const auto& [b, ts] : sp->taps) {
          const auto& tap = R.segments[ts];
          if (tap.track < 0) {
            continue;
          }
          const int rib_row = tap.lo == g.row(boxes.boxes[b].tile) ? tap.hi : tap.lo;
          int64_t   ry      = g.cy(rib_row);
          for (int ri : sp->ribs) {
            if (R.segments[ri].band == rib_row && R.segments[ri].track >= 0) {
              ry = coord(R.segments[ri]);
            }
          }
          // over the whole box tile (the in-box route taps it on any box-layer track) and up to the rib
          const int64_t box_lo = g.bottom(g.row(boxes.boxes[b].tile)), box_hi = box_lo + g.tile_h;
          geo.wire_on(lname(tap.layer), false, std::min(ry, box_lo), std::max(ry, box_hi), coord(tap));
        }
      }
    }
  });
  // ---- routing with the shared signal router (satlegalize.route) on the placed cells. The global metal goes in
  // first and is checked against the cells, the power grid and each other; then, critical nets first, every box pin
  // of a routed net taps onto its net's metal (the low layers weigh more, so a tap climbs early instead of running
  // over other pins) and every local net is routed whole on the low layers. All or nothing: when any net stays open
  // no signal wire is written, so an external router starts from the legal placement instead of partial wiring.
  const auto               saved_rules = d.fp.nondefault_rules;
  std::vector<std::string> why(d.nets.size());
  std::vector<char>        job(d.nets.size(), 0);
  const auto               is_local = [&](size_t n) { return n < R.local.size() && R.local[n]; };
  for (size_t n = 0; n < d.nets.size(); ++n) {
    const auto& N = d.nets[n];
    job[n]        = N.routed && (is_local(n) || !net_geo[n].paths.empty());
    if (job[n]) {
      d.fp.nets[N.frame].wires.clear();
    }
  }
  for (size_t n = 0; n < d.nets.size(); ++n) {
    rep.routed_nets += job[n];
    rep.local_nets  += job[n] && is_local(n);
  }
  // Passes: the router has no rip-up, so a pass that leaves nets open routes again from scratch with those nets
  // first (each pass moves the nets that failed in it ahead). The pass with the fewest open nets is kept.
  std::vector<std::vector<ph::Route_path>>                                         base_paths(d.nets.size());
  std::vector<std::vector<std::tuple<std::string, std::string, int64_t, int64_t>>> base_joins(d.nets.size());
  for (size_t n = 0; n < d.nets.size(); ++n) {
    base_paths[n] = net_geo[n].paths;
    base_joins[n] = net_geo[n].joins;
  }
  const auto                               rep0 = rep;
  std::vector<unsigned>                    prio(d.nets.size(), 0);
  std::optional<Legroute_report>           best_rep;
  std::vector<std::string>                 best_why;
  std::vector<std::vector<ph::Route_path>> best_paths;
  std::vector<size_t>                      best_vias;
  ph::Route_width_rules                    best_rules;
  size_t                                   best_failed = SIZE_MAX;
  for (unsigned pass = 0; pass < std::max(1u, d.opt.route_passes); ++pass) {
    rep                   = rep0;
    rep.route_passes      = pass + 1;
    d.fp.nondefault_rules = saved_rules;
    for (size_t n = 0; n < d.nets.size(); ++n) {
      why[n]           = {};
      net_geo[n].paths = base_paths[n];
      net_geo[n].joins = base_joins[n];
      net_geo[n].vias  = 0;
    }
    // the router works on a copy of the frame with the planned power grid in it (supply pins attached to their rails);
    // the routes it accepts go to both
    ph::Floorplan work = d.fp;
    for (const auto& N : d.nets) {  // die IO pins belong to the nets as the frame names them
      for (const auto& p : N.pins) {
        if (p.term >= 0) {
          work.terminals[p.term].net = d.fp.nets[N.frame].name;
        }
      }
    }
    work.special_nets.insert(work.special_nets.end(), regions.power_nets.begin(), regions.power_nets.end());
    work.vias.insert(work.vias.end(), regions.power_vias.begin(), regions.power_vias.end());
    ph::Routing_occupancy occ = ph::routing_occupancy(d.tech, work, work.dbu);
    if (regions.power_nets.empty()) {
      for (const auto& sh : regions.obstacles) {
        occ.insert({sh, {}, false});
      }
    }
    const ph::Signal_metal_rules rules(d.tech, work);
    const auto                   conflict = [&](const ph::Shape& s, const std::string& net) -> std::string {
      const auto cut   = d.tech.cut_layers.find(s.layer);
      const auto reach = cut == d.tech.cut_layers.end() ? rules.clearance(s.layer)
                                                        : std::optional<int64_t>{ph::signal_cut_spacing(cut->second, d.fp.dbu)};
      if (!reach) {
        return {};
      }
      for (const auto q : occ.query(s.layer, s.rect, *reach, net)) {
        const auto o     = occ.at(q);
        const bool legal = cut == d.tech.cut_layers.end() ? rules.spacing(s, o.shape).legal
                                                          : !ph::violates_cut_spacing(s.rect, o.shape.rect, double(*reach));
        if (!legal) {
          return std::format("global metal: {} to {} ({} [{} {} {} {}] vs [{} {} {} {}])",
                             s.layer,
                             o.net.empty() ? "a blockage"
                             : o.routed    ? "routed metal"
                                           : "a pin",
                             o.net,
                             s.rect.xl,
                             s.rect.yl,
                             s.rect.xh,
                             s.rect.yh,
                             o.shape.rect.xl,
                             o.shape.rect.yl,
                             o.shape.rect.xh,
                             o.shape.rect.yh);
        }
      }
      return {};
    };
    // per net: its metal so far (the taps' anchors)
    std::vector<std::vector<ph::Shape>> metal(d.nets.size());
    parallel(d.nets.size(), d.opt.threads, [&](size_t n) {
      if (!job[n] || net_geo[n].paths.empty()) {
        return;
      }
      ph::Net global;
      global.name = d.fp.nets[d.nets[n].frame].name;
      global.wires.push_back({"ROUTED", net_geo[n].paths});
      metal[n] = ph::route_geometry(d.tech, work, global);
      for (const auto& s : metal[n]) {
        if (auto w = conflict(s, global.name); !w.empty()) {
          why[n] = std::move(w);
          return;
        }
      }
    });
    // the global metal goes in critical nets first; a net whose metal breaks a rule against what is in is routed whole
    // by the router instead (its global metal never goes in)
    std::vector<size_t> global_order;
    for (size_t n = 0; n < d.nets.size(); ++n) {
      if (!metal[n].empty()) {
        global_order.push_back(n);
      }
    }
    std::ranges::stable_sort(global_order, [&](size_t a, size_t b) {
      return d.nets[a].critical != d.nets[b].critical ? d.nets[a].critical : d.nets[a].crit > d.nets[b].crit;
    });
    std::vector<char> whole(d.nets.size(), 0);
    for (const size_t n : global_order) {
      const auto& name = d.fp.nets[d.nets[n].frame].name;
      if (why[n].empty()) {
        for (const auto& s : metal[n]) {
          if (auto w = conflict(s, name); !w.empty()) {
            why[n] = std::move(w);
            break;
          }
        }
      }
      if (!why[n].empty()) {
        ++rep.global_conflict_nets;
        if (rep.failure_sample.size() < 5) {
          rep.failure_sample.push_back(name + ": rerouted whole, " + why[n]);
        }
        why[n].clear();
        whole[n] = 1;
        metal[n].clear();
        net_geo[n].paths.clear();
        net_geo[n].joins.clear();
        continue;
      }
      std::vector<ph::Occupied_shape> add;
      add.reserve(metal[n].size());
      for (const auto& s : metal[n]) {
        add.push_back({s, name, true});
      }
      occ.insert_many(add);
    }
    std::unique_ptr<Pin_escape_guard> escapes;
    std::unique_ptr<Signal_router>    router;
    if (d.profile) {
      escapes = std::make_unique<Pin_escape_guard>(d.tech,
                                                   work,
                                                   occ,
                                                   d.opt.route_access,
                                                   std::function<void()>{},
                                                   d.profile->route.access,
                                                   &rules);
      router  = std::make_unique<Signal_router>(d.tech, work, occ, rules, signal_route_profile(*d.profile, d.tech), escapes.get());
    }
    // A net's router pieces stay pending until the whole net is routed; then they go into the occupancy, the escape
    // guard, the width rules and the net's wiring together. A failed net leaves nothing behind but its global metal.
    struct Pending {
      std::vector<ph::Shape>      shapes;
      std::vector<ph::Route_path> paths;
      ph::Route_width_rules       rules;
      size_t                      vias = 0;
    };
    const auto piece = [&](size_t n, const ph::Net& q, Signal_route_request req, Pending& pend) -> std::string {
      req.limits     = {d.opt.route_access, d.opt.route_patterns, Clock::now() + std::chrono::milliseconds(d.opt.route_ms)};
      auto proposals = router->try_route(q, req);
      if (proposals.candidates.empty() && !req.try_hard) {  // routable first: a critical net drops the coupling screen
        req.try_hard        = true;
        req.limits.deadline = Clock::now() + std::chrono::milliseconds(d.opt.route_ms);
        proposals           = router->try_route(q, req);
      }
      if (proposals.candidates.empty() && q.connections.size() >= 2 && req.anchors.empty()) {
        // a quick span or pattern answer the checks rejected ends try_route; the maze searches around them
        auto maze = router->propose_maze(
            q,
            {req.limits.access_candidates, req.limits.patterns, Clock::now() + std::chrono::milliseconds(d.opt.route_ms)});
        if (maze.geometry) {
          proposals.candidates.push_back(std::move(*maze.geometry));
        } else if (!maze.reason.empty()) {
          proposals.reason += "; maze: " + maze.reason;
        }
      }
      if (proposals.candidates.empty()) {
        return proposals.reason.empty() ? "no route" : proposals.reason;
      }
      const auto& c = proposals.candidates.front();
      ph::Net     trial;
      trial.name = q.name;
      trial.wires.push_back({"ROUTED", c.paths});
      auto shapes = ph::route_geometry(d.tech, work, trial, false, &c.nondefault_rules);
      metal[n].insert(metal[n].end(), shapes.begin(), shapes.end());  // the next pieces may end on it
      pend.shapes.insert(pend.shapes.end(), shapes.begin(), shapes.end());
      pend.paths.insert(pend.paths.end(), c.paths.begin(), c.paths.end());
      pend.rules.insert(c.nondefault_rules.begin(), c.nondefault_rules.end());
      pend.vias += c.vias;
      return {};
    };
    const auto finalize = [&](size_t n, const Pending& pend) -> std::string {
      const auto& name = d.fp.nets[d.nets[n].frame].name;
      const auto  esc  = escapes->check(pend.shapes, name);
      if (!esc.legal) {
        return "escape: " + esc.reason;
      }
      for (const auto& [rule, widths] : pend.rules) {
        if (const auto it = work.nondefault_rules.find(rule); it != work.nondefault_rules.end() && it->second != widths) {
          return "conflicting width rule " + rule;
        }
      }
      work.nondefault_rules.insert(pend.rules.begin(), pend.rules.end());
      d.fp.nondefault_rules.insert(pend.rules.begin(), pend.rules.end());
      std::vector<ph::Occupied_shape> add;
      add.reserve(pend.shapes.size());
      for (const auto& sh : pend.shapes) {
        add.push_back({sh, name, true});
      }
      occ.insert_many(add);
      escapes->commit(esc);
      net_geo[n].paths.insert(net_geo[n].paths.end(), pend.paths.begin(), pend.paths.end());
      net_geo[n].vias += pend.vias;
      return {};
    };
    std::vector<size_t> order;
    for (size_t n = 0; n < d.nets.size(); ++n) {
      if (job[n] && why[n].empty()) {
        order.push_back(n);
      }
    }
    std::ranges::stable_sort(order, [&](size_t a, size_t b) {
      const auto &A = d.nets[a], &B = d.nets[b];
      if (prio[a] != prio[b]) {
        return prio[a] > prio[b];
      }
      if (A.critical != B.critical) {
        return A.critical;
      }
      if (A.crit != B.crit) {
        return A.crit > B.crit;
      }
      return A.pins.size() < B.pins.size();
    });
    std::map<std::string, double> tap_cost{
        { g.box_layer, 4},
        {lname(low_v), 2}
    };
    const auto                                       cell_pin = [&](const Pin& p) { return p.comp >= 0 && p.lef; };
    // die IO pin metal, per net and pin: a join may start on it
    std::vector<std::vector<std::vector<ph::Shape>>> term_metal(d.nets.size());
    for (size_t n = 0; n < d.nets.size(); ++n) {
      for (const auto& p : d.nets[n].pins) {
        if (p.term >= 0) {
          const auto& t = d.fp.terminals[p.term];
          auto&       v = term_metal[n].emplace_back();
          for (const auto& sh : t.shapes) {
            v.push_back({
                sh.layer,
                {t.x + sh.rect.xl, t.y + sh.rect.yl, t.x + sh.rect.xh, t.y + sh.rect.yh}
            });
          }
        }
      }
    }
    // the global tree: every layer change of its metal (a join), then every cell pin taps onto the net's metal nearby
    const auto route_tree = [&](size_t n, Pending& pend) -> std::string {
      const auto& N     = d.nets[n];
      const auto& fn    = d.fp.nets[N.frame];
      auto&       joins = net_geo[n].joins;
      std::ranges::sort(joins);
      joins.erase(std::unique(joins.begin(), joins.end()), joins.end());
      for (const auto& [la, lb, x, y] : joins) {
        const auto at = [&](const std::string& layer) {
          std::vector<ph::Shape> found;
          const auto             hit = [&](const ph::Shape& sh) {
            return sh.layer == layer && sh.rect.xl <= double(x) && sh.rect.xh >= double(x) && sh.rect.yl <= double(y)
                   && sh.rect.yh >= double(y);
          };
          for (const auto& sh : metal[n]) {
            if (hit(sh)) {
              found.push_back(sh);
            }
          }
          for (const auto& pin : term_metal[n]) {
            for (const auto& sh : pin) {
              if (hit(sh)) {
                found.push_back(sh);
              }
            }
          }
          return found;
        };
        ++rep.joins;
        auto from = at(la), to = at(lb);
        if (from.empty() || to.empty()) {
          ++rep.join_failures;
          return std::format("join: no {} metal at {} {}", from.empty() ? la : lb, x, y);
        }
        Signal_route_request req;
        req.sources  = std::move(from);
        req.anchors  = std::move(to);
        req.try_hard = !N.critical;
        if (auto w = piece(n, {fn.name, {}, {}, "SIGNAL"}, req, pend); !w.empty()) {
          ++rep.join_failures;
          return "join: " + w;
        }
      }
      for (const auto& p : N.pins) {
        if (!cell_pin(p)) {
          continue;
        }
        ++rep.taps;
        // the net's metal over the pin's tile (its box's tile) and its neighbors
        const int  tile = d.cell[p.comp] >= 0 ? boxes.boxes[boxes.box_of[d.cell[p.comp]]].tile
                                              : g.tile_of(d.fp.components[p.comp].place.x, d.fp.components[p.comp].place.y);
        const auto xl = double(g.left(g.col(tile)) - g.tile_w), yl = double(g.bottom(g.row(tile)) - g.tile_h);
        const auto xh = xl + 3.0 * double(g.tile_w), yh = yl + 3.0 * double(g.tile_h);
        std::vector<ph::Shape> anchors;
        for (const auto& sh : metal[n]) {
          if (d.tech.layers.contains(sh.layer) && sh.rect.xl < xh && sh.rect.xh > xl && sh.rect.yl < yh && sh.rect.yh > yl) {
            anchors.push_back(sh);
          }
        }
        if (anchors.empty()) {
          ++rep.tap_failures;
          return "tap: no metal of its net near the pin";
        }
        Signal_route_request req;
        req.anchors    = std::move(anchors);
        req.layer_cost = tap_cost;
        req.try_hard   = !N.critical;
        if (auto w = piece(n, {fn.name, {{d.fp.components[p.comp].name, std::string(p.name)}}, {}, "SIGNAL"}, req, pend);
            !w.empty()) {
          ++rep.tap_failures;
          return "tap: " + w;
        }
      }
      return {};
    };
    // the whole net by the router: its cell pins together (or one pin onto a die IO pin), then each die IO pin joins
    const auto route_whole = [&](size_t n, Pending& pend) -> std::string {
      const auto& N  = d.nets[n];
      const auto& fn = d.fp.nets[N.frame];
      ph::Net     q{fn.name, {}, {}, "SIGNAL"};
      for (const auto& p : N.pins) {
        if (cell_pin(p)) {
          q.connections.push_back({d.fp.components[p.comp].name, std::string(p.name)});
        }
      }
      size_t joined = 0;  // die IO pins already on the route
      if (q.connections.size() >= 2) {
        Signal_route_request req;
        req.search_detours = true;
        req.try_hard       = !N.critical;
        if (auto w = piece(n, q, req, pend); !w.empty()) {
          return w;
        }
      } else if (q.connections.size() == 1 && !term_metal[n].empty()) {
        Signal_route_request req;
        req.anchors    = term_metal[n].front();
        req.layer_cost = tap_cost;
        req.try_hard   = !N.critical;
        if (auto w = piece(n, q, req, pend); !w.empty()) {
          return "die IO pin: " + w;
        }
        joined = 1;
      } else {
        return "no cell pin to route from";
      }
      for (size_t k = joined; k < term_metal[n].size(); ++k) {
        Signal_route_request req;
        req.sources  = term_metal[n][k];
        req.anchors  = pend.shapes;
        req.try_hard = !N.critical;
        if (auto w = piece(n, {fn.name, {}, {}, "SIGNAL"}, req, pend); !w.empty()) {
          return "die IO pin: " + w;
        }
      }
      return {};
    };
    for (const size_t n : order) {
      if (!router) {
        why[n] = "no technology profile for the signal router";
        continue;
      }
      const size_t global = metal[n].size();
      Pending      pend;
      std::string  w;
      if (!is_local(n) && !whole[n]) {
        w = route_tree(n, pend);
        if (!w.empty()) {  // the tree did not close: the router takes the whole net (the global metal is not written)
          ++rep.tree_fallbacks;
          if (rep.failure_sample.size() < 8) {
            rep.failure_sample.push_back(d.fp.nets[d.nets[n].frame].name + ": rerouted whole, " + w);
          }
          pend = {};
          metal[n].resize(global);
          net_geo[n].paths.clear();
          whole[n] = 1;
        }
      }
      if (is_local(n) || whole[n]) {
        w = route_whole(n, pend);
        if (!w.empty()) {
          w = (is_local(n) ? "local: " : "whole: ") + w;
        }
      }
      if (w.empty()) {
        w = finalize(n, pend);
      }
      if (!w.empty()) {
        why[n] = std::move(w);
        metal[n].resize(global);
      }
    }
    size_t failed = 0;
    for (size_t n = 0; n < d.nets.size(); ++n) {
      if (job[n] && !why[n].empty()) {
        ++failed;
        prio[n] += 1;
      }
    }
    if (failed < best_failed) {
      best_failed = failed;
      best_rep    = rep;
      best_why    = why;
      best_rules  = d.fp.nondefault_rules;
      best_paths.assign(d.nets.size(), {});
      best_vias.assign(d.nets.size(), 0);
      for (size_t n = 0; n < d.nets.size(); ++n) {
        best_paths[n] = net_geo[n].paths;
        best_vias[n]  = net_geo[n].vias;
      }
    }
    if (failed == 0 || !d.profile) {
      break;
    }
  }
  rep                   = *best_rep;
  why                   = std::move(best_why);
  d.fp.nondefault_rules = std::move(best_rules);
  for (size_t n = 0; n < d.nets.size(); ++n) {
    net_geo[n].paths = std::move(best_paths[n]);
    net_geo[n].vias  = best_vias[n];
  }
  // ---- write the wiring and check every net
  for (size_t n = 0; n < d.nets.size(); ++n) {
    if (job[n] && !net_geo[n].paths.empty()) {
      d.fp.nets[d.nets[n].frame].wires = {
          {"ROUTED", net_geo[n].paths}
      };
    }
  }
  const ph::Route_components comps(d.fp);
  std::atomic<size_t>        connected = 0, unchecked = 0;
  parallel(d.nets.size(), d.opt.threads, [&](size_t n) {
    if (!job[n] || !why[n].empty()) {
      return;
    }
    // the connectivity check supports cell pins only: nets with a die IO pin are counted as unchecked
    if (std::ranges::any_of(d.nets[n].pins, [](const Pin& p) { return p.term >= 0; })) {
      ++unchecked;
      return;
    }
    try {
      const auto c = ph::check_route_connectivity(d.tech, d.fp, d.fp.nets[d.nets[n].frame], &comps);
      if (c.connected) {
        ++connected;
        return;
      }
      why[n] = std::format("disconnected: {} ({} components)", c.reason, c.components);
    } catch (const std::exception& e) {
      why[n] = std::string("disconnected: ") + e.what();
    }
  });
  rep.connected_nets = connected;
  rep.unchecked_nets = unchecked;
  for (size_t n = 0; n < d.nets.size(); ++n) {
    if (!job[n] || why[n].empty()) {
      continue;
    }
    ++rep.failed_nets;
    rep.disconnected_nets += why[n].starts_with("disconnected");
    ++rep.failure_reasons[why[n].substr(0, why[n].find(" ("))];
    if (rep.failure_sample.size() < 10) {
      rep.failure_sample.push_back(d.fp.nets[d.nets[n].frame].name + ": " + why[n]);
    }
  }
  rep.wires_emitted = rep.failed_nets == 0 && d.profile != nullptr;
  for (size_t n = 0; n < d.nets.size(); ++n) {
    if (job[n] && !d.fp.nets[d.nets[n].frame].wires.empty()) {
      if (rep.wires_emitted) {
        rep.vias += net_geo[n].vias;
      } else {
        d.fp.nets[d.nets[n].frame].wires.clear();
      }
    }
  }
  if (!rep.wires_emitted) {
    d.fp.nondefault_rules = saved_rules;
  }
  // ---- where it fails: per box, its fill, internal and external pins and in-box demand, against the failed nets
  std::vector<size_t> internal(boxes.boxes.size(), 0), external(boxes.boxes.size(), 0), failing(boxes.boxes.size(), 0);
  for (size_t n = 0; n < d.nets.size(); ++n) {
    const auto& N = d.nets[n];
    if (N.power || N.clock) {
      continue;
    }
    std::map<int, size_t> in_box;
    bool                  outside = false;
    for (const auto& p : N.pins) {
      if (p.comp >= 0 && d.cell[p.comp] >= 0) {
        ++in_box[boxes.box_of[d.cell[p.comp]]];
      } else {
        outside = true;
      }
    }
    for (const auto& [b, k] : in_box) {
      (in_box.size() == 1 && !outside ? internal : external)[b] += k;
      failing[b]                                                += job[n] && !why[n].empty();
    }
  }
  const auto bucket = [](std::vector<Legroute_report::Bucket>& v, const std::vector<std::string>& names, size_t k, bool fail) {
    if (v.empty()) {
      for (const auto& name : names) {
        v.push_back({name, 0, 0});
      }
    }
    ++v[k].boxes;
    v[k].failing += fail;
  };
  const std::vector<std::string> fill_names{"<0.5", "0.5-0.6", "0.6-0.7", "0.7-0.8", "0.8-0.9", ">=0.9"};
  const std::vector<std::string> pin_names{"0-3", "4-7", "8-11", "12-15", "16-23", ">=24"};
  const auto                     pin_bucket = [](size_t k) -> size_t { return k < 16 ? k / 4 : k < 24 ? 4 : 5; };
  for (size_t b = 0; b < boxes.boxes.size(); ++b) {
    const auto&  B    = boxes.boxes[b];
    const double fill = double(B.sites) / (2.0 * g.sites);
    const bool   fail = failing[b] > 0;
    bucket(rep.fill_pattern, fill_names, fill < .5 ? 0 : std::min<size_t>(5, size_t((fill - .4) * 10)), fail);
    bucket(rep.internal_pattern, pin_names, pin_bucket(internal[b]), fail);
    bucket(rep.external_pattern, pin_names, pin_bucket(external[b]), fail);
    if (fail) {
      rep.failure_boxes.push_back({b,
                                   size_t(g.col(B.tile)),
                                   size_t(g.row(B.tile)),
                                   B.cells.size(),
                                   internal[b],
                                   external[b],
                                   failing[b],
                                   fill,
                                   R.box_dem_h.empty() ? 0 : R.box_dem_h[B.tile],
                                   double(g.box_cap[B.tile]),
                                   R.box_dem_v.empty() ? 0 : R.box_dem_v[B.tile]});
    }
  }
  std::ranges::stable_sort(rep.failure_boxes, [](const auto& a, const auto& b) { return a.failed_nets > b.failed_nets; });
  if (rep.failure_boxes.size() > 20) {
    rep.failure_boxes.resize(20);
  }
}
}  // namespace livehd::placement::lr
