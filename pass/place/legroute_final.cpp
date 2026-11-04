// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// legroute.final: cells placed inside their box (F1); the global metal (segments, risers, jogs, clock spines) is
// written and checked, then the shared signal router (satlegalize.route) taps each box pin onto its net's metal and
// routes the local nets whole on the low layers (F2/F3); every net is checked for connectivity and the wiring is
// written all or nothing (F4). DRC stays external (F5).
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <exception>
#include <format>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <queue>
#include <regex>
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
#include "inou/lefdef/signal_via.hpp"
#include "legroute_internal.hpp"
#include "pin_escape_guard.hpp"
#include "signal_router.hpp"
#include "steiner_topology.hpp"

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
  // Track_state of the L5 track each path was drawn on (the caller sets `state` before wire_on)
  uint8_t                     state = 0;
  std::vector<uint8_t>        path_state;
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
    path_state.push_back(state);
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

// A pin's center need not be a routing track. Select an access coordinate from
// the actual finite DEF patterns, including their origin and any second pattern.
// This only chooses a candidate: rectangle, occupancy and via checks still apply.
std::optional<int64_t> pin_track(const ph::Floorplan& fp, const std::string& layer, bool horizontal, const ph::Rect& pin) {
  const double           lo = horizontal ? pin.yl : pin.xl, hi = horizontal ? pin.yh : pin.xh;
  const double           center = (lo + hi) / 2;
  std::optional<int64_t> best;
  for (const auto& track : fp.tracks) {
    if (track.layer != layer || track.axis != (horizontal ? "Y" : "X") || !track.supported()) {
      continue;
    }
    const auto [first, last] = track.index_range(lo, hi);
    if (first > last) {
      continue;
    }
    const auto index      = std::clamp<int64_t>(std::llround((center - track.start) / track.step), first, last);
    const auto coordinate = track.start + index * track.step;
    if (!best || std::abs(coordinate - center) < std::abs(*best - center)
        || (std::abs(coordinate - center) == std::abs(*best - center) && coordinate < *best)) {
      best = coordinate;
    }
  }
  return best;
}
}  // namespace

struct Final_cut_state::State {
  ph::Signal_cut_set                                                   cuts;
  std::set<std::tuple<std::string, ph::Orientation, int64_t, int64_t>> vias;
  State(const ph::Technology& tech, const ph::Floorplan& frame) : cuts(tech, frame) {}
};
Final_cut_state::Final_cut_state(const ph::Technology& tech, const ph::Floorplan& frame)
    : state_(std::make_unique<State>(tech, frame)) {}
Final_cut_state::~Final_cut_state() = default;

std::vector<Signal_route_via> Final_cut_state::vias() const {
  std::vector<Signal_route_via> result;
  result.reserve(state_->vias.size());
  for (const auto& [name, orientation, x, y] : state_->vias) {
    result.push_back({
        {name, orientation},
        x,
        y
    });
  }
  return result;
}

std::string Final_cut_state::add(std::span<const ph::Route_path> paths) {
  // Signal_cut_set::add can insert some cuts before a later cut fails. Neither
  // those cuts nor the reuse identities may leak from a rejected proposal.
  auto next = std::make_unique<State>(*state_);
  try {
    for (const auto& path : paths) {
      std::optional<ph::Route_point> at;
      for (const auto& step : path.steps) {
        if (const auto* point = std::get_if<ph::Route_point>(&step)) {
          at = *point;
        } else if (const auto* via = std::get_if<ph::Route_via>(&step)) {
          if (!at) {
            return "route cut via without a preceding point";
          }
          if (next->vias.emplace(via->name, via->orientation, at->x, at->y).second && !next->cuts.add(*via, at->x, at->y)) {
            return "self cut spacing across route pieces";
          }
        }
      }
    }
  } catch (const std::exception& e) {
    return std::string("route cuts: ") + e.what();
  }
  state_.swap(next);
  return {};
}

void final_place(const Design& d, const Grid& g, const Boxes& boxes, const Routing& R, unsigned threads) {
  const auto risers = box_risers(g, boxes, R);
  parallel(boxes.boxes.size(), threads, [&](size_t bi) {
    const auto& b = boxes.boxes[bi];
    if (b.placed && b.tile == g.tile_of(b.cx, b.cy)) {
      return;  // legal_boxes preserves the given cell placement through final pin attachment too
    }
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
    // Row assignment first: the cells' rows minimize the box nets split across the two rows (a cross-row net needs
    // an extra M2 track and an M3 one; a same-row one usually a single M2), under the rows' segment capacity, then
    // the fewest cells moved. Exhaustive up to 14 single-row cells; boxes with two-row cells keep their rows.
    if (!in.empty() && b.cells.size() <= 14 && std::ranges::none_of(b.cells, [&](int i) { return d.tall[size_t(i)]; })) {
      std::vector<int> cells, rows_now;
      for (const auto& [sg, members] : in) {
        for (int i : members) {
          cells.push_back(i);
          rows_now.push_back(segs[size_t(sg)].row);
        }
      }
      std::array<std::vector<int>, 2> room_of, seg_of;  // per row: free segment rooms and their segment ids
      for (size_t sg = 0; sg < segs.size(); ++sg) {
        room_of[segs[sg].row].push_back(segs[sg].hi - segs[sg].lo);
        seg_of[segs[sg].row].push_back(int(sg));
      }
      std::vector<std::vector<int>> nets;  // box nets with 2+ pins here, as cell index lists
      {
        std::map<int, std::vector<int>> by_net;
        for (size_t k = 0; k < cells.size(); ++k) {
          for (int n : d.nets_of[size_t(cells[k])]) {
            if (inner[n] > 1 && !d.nets[size_t(n)].power) {
              by_net[n].push_back(int(k));
            }
          }
        }
        for (auto& [_, v] : by_net) {
          nets.push_back(std::move(v));
        }
      }
      const size_t                    n         = cells.size();
      auto                            best_key  = std::pair{std::numeric_limits<int>::max(), std::numeric_limits<int>::max()};
      uint32_t                        best_mask = 0;
      bool                            found     = false;
      std::array<std::vector<int>, 2> best_pack;
      for (uint32_t mask = 0; mask < (uint32_t{1} << n); ++mask) {
        int cut = 0, moved = 0;
        for (const auto& v : nets) {
          bool lo = false, hi = false;
          for (int k : v) {
            ((mask >> k) & 1 ? hi : lo) = true;
          }
          cut += lo && hi;
        }
        for (size_t k = 0; k < n; ++k) {
          moved += int((mask >> k) & 1) != rows_now[k];
        }
        if (std::pair{cut, moved} >= best_key) {
          continue;
        }
        std::array<std::vector<int>, 2> widths, pack;
        for (size_t k = 0; k < n; ++k) {
          widths[(mask >> k) & 1].push_back(cell_sites(d, g, cells[k]));
        }
        bool fits = true;
        for (int row = 0; row < 2 && fits; ++row) {
          if (!widths[row].empty()) {
            pack[row] = pack_segments(widths[row], room_of[row]);
            fits      = !pack[row].empty();
          }
        }
        if (fits) {
          best_key  = {cut, moved};
          best_mask = mask;
          best_pack = std::move(pack);
          found     = true;
        }
      }
      if (found && best_key.second > 0) {
        in.clear();
        std::array<size_t, 2> next{};
        for (size_t k = 0; k < n; ++k) {
          const int row = int((best_mask >> k) & 1);
          const int sg  = seg_of[row][size_t(best_pack[row][next[row]++])];
          in[sg].push_back(cells[k]);
          auto& p       = d.fp.components[d.movable[cells[k]]].place;
          p.y           = g.bottom(r) + row * g.row_h;
          p.orientation = g.row_orient[size_t(2 * r + row)];
        }
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
          const double cost = cost_of(o, start);
          if (cost < best) {
            best       = cost;
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

Final_planned_geometry final_planned_geometry(const Design& d, const Grid& g, const Boxes& boxes, const Routing& R) {
  (void)boxes;
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
  std::vector<std::string>                         io_stub_failure(d.nets.size());
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
            // the recorded provenance, also when the pick failed (track -1, fallback): the default track drawn here is not checked
            geo.state = (e ? s.jog_hi_state : s.jog_lo_state) != uint8_t(Track_state::none) ? (e ? s.jog_hi_state : s.jog_lo_state)
                                                                                            : uint8_t(Track_state::none);
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
      geo.state = s.track_state;
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
        std::optional<int64_t>   px, py;
        if (p.term >= 0) {
          const auto& t = d.fp.terminals[p.term];
          for (const auto& sh : t.shapes) {
            if (!st.index.contains(sh.layer)) {
              continue;
            }
            const auto rect       = ph::orient_rect(sh.rect, 0, 0, t.orientation).translated(t.x, t.y);
            px                    = pin_track(d.fp, lname(low_v), false, rect);
            const bool horizontal = d.tech.layers.at(sh.layer).direction == "HORIZONTAL";
            py                    = horizontal ? pin_track(d.fp, sh.layer, true, rect)
                                               : std::optional<int64_t>{snap(std::llround((rect.yl + rect.yh) / 2), st.grid)};
            if (px && py && *py >= rect.yl && *py <= rect.yh) {
              s = ph::Shape{sh.layer, rect};
              break;
            }
          }
        }
        if (!s) {
          // No partial stub may enter occupancy. The ordinary whole-net router
          // can try a different access topology, with the original pin intact.
          io_stub_failure[n] = "die IO stub: no explicit preferred track inside the pin";
          geo.paths.clear();
          geo.joins.clear();
          geo.path_state.clear();
          return;
        }
        const auto  rx = riser_x(k);
        // pin -> lowest vertical layer -> up or down to the stub track (L7) -> across on the lowest horizontal layer
        // -> the riser
        const auto  ys = node.stub_track >= 0 ? g.track_coord(low_h, node.stub_track) : *py;
        const auto& hl = lname(low_h);
        const auto& vl = lname(low_v);
        if (ys != *py) {
          geo.stack(s->layer, vl, *px, *py);
          geo.state = uint8_t(Track_state::none);  // derived here, never picked by L5 (only the horizontal stub is)
          geo.wire_on(vl, false, *py, ys, *px);
          geo.stack(vl, hl, *px, ys);
        } else {
          geo.stack(s->layer, hl, *px, ys);  // no vertical piece: the pin joins the stub directly (none if same layer)
        }
        geo.state = node.stub_state;
        geo.wire_on(hl, true, *px, rx, ys);
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
      geo.state           = T.nodes[node].riser_state;
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
        std::vector<std::pair<int, int64_t>> trunk_joins;  // (rib layer, rib y): made only if the trunk wire exists
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
            geo.state = T.nodes[sp->node].riser_state;
            geo.wire_on(lname(T.nodes[sp->node].riser_layer),
                        false,
                        std::min(ry, g.cy(trunk.band >= 0 ? rib.band : 0)),
                        std::max(ry, g.cy(rib.band)),
                        rx);
          }
          geo.state = rib.track_state;
          geo.wire_on(lname(rib.layer), true, xlo, xhi, ry);
          trunk_joins.emplace_back(rib.layer, ry);
        }
        // A spine with a single drawn rib has no trunk wire (nothing to connect): a rib->trunk join there would name
        // trunk metal that does not exist.
        if (yhi > ylo) {
          geo.state = trunk.track_state;
          geo.wire_on(lname(trunk.layer), false, ylo, yhi, tx);
          for (const auto& [layer, ry] : trunk_joins) {
            geo.stack(lname(layer), lname(trunk.layer), tx, ry);
          }
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
          geo.state = tap.track_state;
          geo.wire_on(lname(tap.layer), false, std::min(ry, box_lo), std::max(ry, box_hi), coord(tap));
        }
      }
    }
  });
  Final_planned_geometry out;
  out.paths.resize(d.nets.size());
  out.joins.resize(d.nets.size());
  out.path_state.resize(d.nets.size());
  for (size_t n = 0; n < d.nets.size(); ++n) {
    out.path_state[n] = std::move(net_geo[n].path_state);
    out.paths[n]      = std::move(net_geo[n].paths);
    out.joins[n]      = std::move(net_geo[n].joins);
  }
  out.io_stub_failure = std::move(io_stub_failure);
  return out;
}

void final_wires(const Design& d, const Grid& g, const Boxes& boxes, const Routing& R, const Legroute_regions& regions,
                 Legroute_report& rep) {
  // Steiner topologies need the FLUTE tables: a missing runtime file is an experiment/packaging failure, never
  // thousands of per-net routing failures. Fail the whole stage up front.
  initialize_flute_tables();
  const Stack st(d.tech, d.fp.dbu, d.fp.die);
  const int   low_v = [&] {
    for (int l = 0; l < int(g.layers.size()); ++l) {
      if (!g.layers[l].horizontal) {
        return l;
      }
    }
    return 0;
  }();
  const auto       lname   = [&](int l) -> const std::string& { return g.layers[l].name; };
  // the planned global metal (segments, bends, risers, jogs, stubs, clock spines) and its layer joins
  auto             planned = final_planned_geometry(d, g, boxes, R);
  std::vector<Geo> net_geo;
  net_geo.reserve(d.nets.size());
  size_t dropped_fallback = 0;
  for (size_t n = 0; n < d.nets.size(); ++n) {
    net_geo.emplace_back(st);
    if (d.opt.final_drop_fallback_planned) {
      // opt-in: a path drawn on an L5 fallback proposal (a track that failed every L5 check) is not admitted as planned
      // metal; the net's checked pieces stay and its pins tap/join around the gap
      auto&                       paths  = planned.paths[n];
      auto&                       states = planned.path_state[n];
      std::vector<ph::Route_path> kept;
      for (size_t k = 0; k < paths.size(); ++k) {
        if (k < states.size() && states[k] == uint8_t(Track_state::fallback)) {
          ++dropped_fallback;
          continue;
        }
        kept.push_back(std::move(paths[k]));
      }
      paths = std::move(kept);
    }
    net_geo[n].paths = std::move(planned.paths[n]);
    net_geo[n].joins = std::move(planned.joins[n]);
  }
  if (d.opt.final_drop_fallback_planned) {
    std::fprintf(stderr,
                 "legroute final: %zu planned paths on L5 fallback tracks not admitted (final_drop_fallback_planned)\n",
                 dropped_fallback);
  }
  const auto                io_stub_failure = std::move(planned.io_stub_failure);
  // ---- routing with the shared signal router (satlegalize.route) on the placed cells. The global metal goes in
  // first and is checked against the cells, the power grid and each other; then, critical nets first, every box pin
  // of a routed net taps onto its net's metal (the low layers weigh more, so a tap climbs early instead of running
  // over other pins) and every local net is routed whole on the low layers. All or nothing: when any net stays open
  // no signal or clock wire is written, so closure never means a partially connected clock network.
  const auto               saved_rules = d.fp.nondefault_rules;
  std::vector<std::string> why(d.nets.size());
  std::vector<char>        job(d.nets.size(), 0);
  const auto               is_local = [&](size_t n) { return n < R.local.size() && R.local[n]; };
  // Opt-in diagnostic: leave clock nets and/or the nets matching final_skip_nets out of Final entirely (no planned
  // metal admitted, no detail, not counted), to measure what the remaining nets can close without them.
  std::optional<std::regex> skip_re;
  if (!d.opt.final_skip_nets.empty()) {
    skip_re.emplace(d.opt.final_skip_nets, std::regex::ECMAScript | std::regex::icase);
  }
  size_t skipped = 0;
  for (size_t n = 0; n < d.nets.size(); ++n) {
    const auto& N = d.nets[n];
    // Coarse signal planning excludes clocks; final physical closure must include them, even with spines disabled.
    job[n]        = N.requires_final_route();
    if (job[n]) {
      d.fp.nets[N.frame].wires.clear();
      if ((d.opt.final_skip_clock && N.clock) || (skip_re && std::regex_search(d.fp.nets[N.frame].name, *skip_re))) {
        job[n] = 0;
        ++skipped;
        std::fprintf(stderr,
                     "legroute final: skipping %s%s (diagnostic)\n",
                     d.fp.nets[N.frame].name.c_str(),
                     N.clock ? " [clock]" : "");
      }
    }
  }
  rep.final_skipped_nets = skipped;
  for (size_t n = 0; n < d.nets.size(); ++n) {
    rep.routed_nets         += job[n];
    rep.clock_required_nets += job[n] && d.nets[n].clock;
    rep.local_nets          += job[n] && is_local(n);
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
  // Per net, how it entered detailed routing in the retained pass (see final_nets.tsv).
  std::vector<std::string>                 entry(d.nets.size()), best_entry;
  std::vector<char>                        best_checked;
  std::vector<std::vector<ph::Route_path>> best_paths;
  std::vector<size_t>                      best_vias;
  ph::Route_width_rules                    best_rules;
  size_t                                   best_failed          = SIZE_MAX;
  size_t                                   prior_cut_rejections = 0, pending_cut_rejections = 0, union_cut_rejections = 0;
  size_t                                   precommit_disconnected = 0;
  // Opt-in final_box_report: per cell pin / join / whole attempt of the retained pass, the box it belongs to (a join:
  // the box on the join's tile, -1 outside boxes), its phase (unattempted, failed, committed, rolled_back) and reason,
  // plus the M2/M3 metal each committed net has inside each box's tile (retained planned vs new detailed metal).
  struct Box_event {
    size_t      net = 0;
    int         box = -1;
    std::string pin, kind, layer;
    int64_t     x = 0, y = 0;
    std::string phase, reason;
  };
  struct Box_usage {
    int         box = -1;
    size_t      net = 0;
    std::string layer;
    bool        local = false;  // the net has a cell pin in this box (a destination here), else transit through it
    std::string metal;          // "new" detailed metal, "retained" planned metal of a closed tree, or "abandoned" planned
                                // metal of a tree that fell back to a whole route (still occupancy, no longer the net's path)
    ph::Rect    rect;           // clipped to the box's tile
  };
  const bool             box_report = d.opt.final_box_report && !d.opt.output_dir.empty();
  std::vector<Box_event> box_ev, best_box_ev;
  std::vector<Box_usage> box_use, best_box_use;
  std::vector<int>       box_in_tile(size_t(g.cols * g.rows), -1);
  for (size_t b = 0; b < boxes.boxes.size(); ++b) {
    if (boxes.boxes[b].tile >= 0 && size_t(boxes.boxes[b].tile) < box_in_tile.size()) {
      box_in_tile[size_t(boxes.boxes[b].tile)] = int(b);
    }
  }
  const auto pin_box = [&](const Pin& p) { return p.comp >= 0 && d.cell[p.comp] >= 0 ? boxes.box_of[d.cell[p.comp]] : -1; };
  std::vector<std::set<int>> net_boxes(box_report ? d.nets.size() : 0);  // boxes holding a cell pin of each net
  for (size_t n = 0; n < net_boxes.size(); ++n) {
    for (const auto& p : d.nets[n].pins) {
      if (const int b = pin_box(p); b >= 0) {
        net_boxes[n].insert(b);
      }
    }
  }
  // the vias a committed piece added (name@x,y in path order): its access chain from the pin to its arrival layer
  const auto via_chain = [](const std::vector<ph::Route_path>& paths, size_t from) {
    std::string out;
    for (size_t i = from; i < paths.size(); ++i) {
      std::optional<ph::Route_point> last;
      for (const auto& step : paths[i].steps) {
        if (const auto* pt = std::get_if<ph::Route_point>(&step)) {
          last = *pt;
        } else if (const auto* via = std::get_if<ph::Route_via>(&step); via && last) {
          out += std::format("{}{}@{},{}", out.empty() ? "" : ";", via->name, last->x, last->y);
        }
      }
    }
    return out;
  };
  const auto xy_box       = [&](double x, double y) { return box_in_tile[size_t(g.tile_of(x, y))]; };
  size_t     attempt      = 0;  // first event of the current net attempt
  // phase of the current attempt's open events: every "ok" event becomes committed or rolled_back
  const auto close_events = [&](const std::string& phase, const std::string& reason) {
    for (size_t i = attempt; i < box_ev.size(); ++i) {
      if (box_ev[i].phase == "ok") {
        box_ev[i].phase  = phase;
        box_ev[i].reason = reason;
      }
    }
  };
  for (unsigned pass = 0; pass < std::max(1u, d.opt.route_passes); ++pass) {
    box_ev.clear();
    box_use.clear();
    rep                   = rep0;
    rep.route_passes      = pass + 1;
    d.fp.nondefault_rules = saved_rules;
    for (size_t n = 0; n < d.nets.size(); ++n) {
      entry[n]         = {};
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
    const ph::Route_components work_comps(work);  // opt-in precommit connectivity: cell lookup on this pass's frame
    if (regions.power_nets.empty()) {
      for (const auto& sh : regions.obstacles) {
        occ.insert({sh, {}, false});
      }
    }
    const ph::Signal_metal_rules rules(d.tech, work);
    const auto                   conflict = [&](const ph::Shape& s, const std::string& net) -> std::string {
      const auto cut = d.tech.cut_layers.find(s.layer);
      if (s.rect.xl < work.die.xl || s.rect.yl < work.die.yl || s.rect.xh > work.die.xh || s.rect.yh > work.die.yh) {
        return "global metal: " + s.layer + " outside die";
      }
      if (cut == d.tech.cut_layers.end()) {
        if (const auto check = rules.rectangle(s); !check.legal) {
          return "global metal: " + s.layer + ": " + check.reason;
        }
      }
      const auto clearance = cut == d.tech.cut_layers.end() ? rules.clearance(s.layer)
                                                            : std::optional<int64_t>{ph::signal_cut_spacing(cut->second, d.fp.dbu)};
      if (!clearance) {
        return {};
      }
      for (const auto q : occ.query(s.layer, s.rect, *clearance, net)) {
        const auto o     = occ.at(q);
        const bool legal = cut == d.tech.cut_layers.end() ? rules.spacing(s, o.shape).legal
                                                          : !ph::violates_cut_spacing(s.rect, o.shape.rect, double(*clearance));
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
      // opt-in final_clock_first: the clock tree's planned metal goes in before any signal (L5 assigns it first too)
      if (d.opt.final_clock_first && d.nets[a].clock != d.nets[b].clock) {
        return bool(d.nets[a].clock);
      }
      return d.nets[a].critical != d.nets[b].critical ? d.nets[a].critical : d.nets[a].crit > d.nets[b].crit;
    });
    std::vector<char> whole(d.nets.size(), 0);
    for (size_t n = 0; n < d.nets.size(); ++n) {
      if (job[n] && !is_local(n) && metal[n].empty()) {
        entry[n] = "no_planned_metal";
        whole[n] = 1;
        ++rep.tree_fallbacks;
        if (!io_stub_failure[n].empty() && rep.failure_sample.size() < 5) {
          rep.failure_sample.push_back(d.fp.nets[d.nets[n].frame].name + ": rerouted whole, " + io_stub_failure[n]);
        }
      }
    }
    for (const size_t n : global_order) {
      const auto& name     = d.fp.nets[d.nets[n].frame].name;
      const bool  precheck = !why[n].empty();  // found against the initial occupancy, before any planned net went in
      if (why[n].empty()) {
        for (const auto& s : metal[n]) {
          if (auto w = conflict(s, name); !w.empty()) {
            why[n] = std::move(w);
            break;
          }
        }
      }
      if (!why[n].empty()) {
        entry[n] = "planned_metal_conflict: " + why[n].substr(0, why[n].find(" ("));
        ++rep.global_conflict_nets;
        if (d.nets[n].clock) {  // the full first conflict of a clock, kept apart from the five-entry failure sample
          rep.clock_conflicts.push_back(
              std::format("pass {} {}: {}: {}",
                          pass + 1,
                          name,
                          precheck ? "initial occupancy (PG, fixed metal, pins)" : "planned-net admission",
                          why[n]));
        }
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
                                                   &rules,
                                                   d.opt.protect_supply_access);
      router  = std::make_unique<Signal_router>(d.tech, work, occ, rules, signal_route_profile(*d.profile, d.tech), escapes.get());
    }
    // A net's router pieces stay pending until the whole net is routed; then they go into the occupancy, the escape
    // guard, the width rules and the net's wiring together. A failed net leaves nothing behind but its global metal.
    struct Pending {
      std::vector<ph::Shape>           shapes;
      std::vector<ph::Route_path>      paths;
      ph::Route_width_rules            rules;
      size_t                           vias = 0;
      std::unique_ptr<Final_cut_state> cuts;
    };
    const auto piece = [&](size_t n, const ph::Net& q, Signal_route_request req, Pending& pend) -> std::string {
      if (!pend.cuts) {
        auto cuts = std::make_unique<Final_cut_state>(d.tech, work);
        if (auto cut_error = cuts->add(net_geo[n].paths); !cut_error.empty()) {
          return cut_error;
        }
        pend.cuts = std::move(cuts);
      }
      req.prior_vias        = pend.cuts->vias();
      // route_access is the per-call cap. Opt-in experiment (route_access_per_pin > 0): a high-fanout request gets at least
      // that many access trials per pin, since every accepted pin needs one; off by default, so the cap is unchanged.
      const size_t   access = d.opt.route_access_per_pin
                                  ? std::max<size_t>(d.opt.route_access, size_t(d.opt.route_access_per_pin) * q.connections.size())
                                  : size_t(d.opt.route_access);
      // opt-in final_clock_route_ms: a clock net's pieces (joins, taps, whole/maze retries) get their own wall budget
      const unsigned ms     = d.nets[n].clock && d.opt.final_clock_route_ms ? d.opt.final_clock_route_ms : d.opt.route_ms;
      const unsigned patterns
          = d.nets[n].clock && d.opt.final_clock_route_patterns ? d.opt.final_clock_route_patterns : d.opt.route_patterns;
      req.limits            = {access, patterns, Clock::now() + std::chrono::milliseconds(ms)};
      auto proposals        = router->try_route(q, req);
      prior_cut_rejections += proposals.prior_cut_rejections;
      if (proposals.candidates.empty() && !req.try_hard) {  // routable first: a critical net drops the coupling screen
        req.try_hard          = true;
        req.limits.deadline   = Clock::now() + std::chrono::milliseconds(ms);
        proposals             = router->try_route(q, req);
        prior_cut_rejections += proposals.prior_cut_rejections;
      }
      if (proposals.candidates.empty() && q.connections.size() >= 2 && req.anchors.empty()) {
        // a quick span or pattern answer the checks rejected ends try_route; the maze searches around them
        auto maze = router->propose_maze(
            q,
            {req.limits.access_candidates, req.limits.patterns, Clock::now() + std::chrono::milliseconds(ms)},
            req.prior_vias);
        prior_cut_rejections += maze.prior_cut_rejections;
        if (maze.geometry) {
          proposals.candidates.push_back(std::move(*maze.geometry));
        } else if (!maze.reason.empty()) {
          proposals.reason += "; maze: " + maze.reason;
        }
      }
      if (proposals.candidates.empty()) {
        return proposals.reason.empty() ? "no route" : proposals.reason;
      }
      std::string cut_failure;
      for (const auto& c : proposals.candidates) {
        ph::Net trial;
        trial.name = q.name;
        trial.wires.push_back({"ROUTED", c.paths});
        auto shapes = ph::route_geometry(d.tech, work, trial, false, &c.nondefault_rules);
        if (auto cut_error = pend.cuts->add(c.paths); !cut_error.empty()) {
          ++pending_cut_rejections;
          cut_failure = std::move(cut_error);
          continue;  // another existing candidate may avoid the earlier pieces
        }
        metal[n].insert(metal[n].end(), shapes.begin(), shapes.end());  // the next pieces may end on it
        pend.shapes.insert(pend.shapes.end(), shapes.begin(), shapes.end());
        pend.paths.insert(pend.paths.end(), c.paths.begin(), c.paths.end());
        pend.rules.insert(c.nondefault_rules.begin(), c.nondefault_rules.end());
        pend.vias += c.vias;
        return {};
      }
      return cut_failure;
    };
    const auto finalize = [&](size_t n, const Pending& pend) -> std::string {
      // Reconstruct the complete union independently of the incremental state
      // before any width rule, escape reservation, occupancy or wire commit.
      Final_cut_state cuts(d.tech, work);
      if (auto cut_error = cuts.add(net_geo[n].paths); !cut_error.empty()) {
        ++union_cut_rejections;
        return cut_error;
      }
      if (auto cut_error = cuts.add(pend.paths); !cut_error.empty()) {
        ++union_cut_rejections;
        return cut_error;
      }
      const auto& name = d.fp.nets[d.nets[n].frame].name;
      if (d.opt.final_precommit_connectivity) {
        // Opt-in: the complete net (retained global metal plus every pending piece) must connect all of its pins before
        // its pending detail is committed, so a disconnected tree commits no new metal and counts as failed for best-pass
        // ranking (its already admitted global metal stays, as for any failed net). The
        // pending width rules are resolved over the installed ones without installing them; this is not a DRC check.
        ph::Net trial = d.fp.nets[d.nets[n].frame];
        auto    paths = net_geo[n].paths;
        paths.insert(paths.end(), pend.paths.begin(), pend.paths.end());
        trial.wires = {
            {"ROUTED", std::move(paths)}
        };
        try {
          const auto c = ph::check_route_connectivity(d.tech, work, trial, &work_comps, &pend.rules);
          if (!c.connected) {
            ++precommit_disconnected;
            return std::format("precommit connectivity: {} ({} components)", c.reason, c.components);
          }
        } catch (const std::exception& e) {
          ++precommit_disconnected;
          return std::string("precommit connectivity: ") + e.what();
        }
      }
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
      if (d.opt.final_clock_detail_first && A.clock != B.clock) {  // opt-in: clock taps/joins before any signal detail
        return bool(A.clock);
      }
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
            v.push_back({sh.layer, ph::orient_rect(sh.rect, 0, 0, t.orientation).translated(t.x, t.y)});
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
        const auto join_event = [&](const std::string& phase, const std::string& reason) {
          if (box_report) {
            box_ev.push_back({n, xy_box(double(x), double(y)), "", "join", la + ">" + lb, x, y, phase, reason});
          }
        };
        const auto rest_unattempted = [&] {  // the tree stops here: its cell pins were never tapped
          if (box_report) {
            for (const auto& p : N.pins) {
              if (cell_pin(p)) {
                box_ev.push_back({n,
                                  pin_box(p),
                                  d.fp.components[p.comp].name + "/" + std::string(p.name),
                                  "tap",
                                  "",
                                  0,
                                  0,
                                  "unattempted",
                                  "tree stopped at a join"});
              }
            }
          }
        };
        auto from = at(la), to = at(lb);
        if (from.empty() || to.empty()) {
          ++rep.join_failures;
          auto w = std::format("join: no {} metal at {} {}", from.empty() ? la : lb, x, y);
          join_event("failed", w);
          rest_unattempted();
          return w;
        }
        Signal_route_request req;
        req.sources  = std::move(from);
        req.anchors  = std::move(to);
        req.try_hard = !N.critical;
        if (auto w = piece(n, {fn.name, {}, {}, "SIGNAL"}, req, pend); !w.empty()) {
          ++rep.join_failures;
          join_event("failed", w);
          rest_unattempted();
          return "join: " + w;
        }
        join_event("ok", "");
      }
      bool        tap_failed = false;
      std::string tap_reason;  // the first failed tap ends the tree (later pins are reported unattempted)
      for (const auto& p : N.pins) {
        if (!cell_pin(p)) {
          continue;
        }
        const auto tap_event = [&](const std::string& phase, const std::string& reason) {
          if (box_report) {
            box_ev.push_back(
                {n, pin_box(p), d.fp.components[p.comp].name + "/" + std::string(p.name), "tap", "", 0, 0, phase, reason});
          }
        };
        if (tap_failed) {
          tap_event("unattempted", "an earlier tap of the net failed");
          continue;
        }
        ++rep.taps;
        // the net's metal over the pin's tile (its box's tile) and its neighbors
        const int  tile = d.cell[p.comp] >= 0 ? boxes.boxes[boxes.box_of[d.cell[p.comp]]].tile : physical_pin_access(d, g, p).tile;
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
          tap_event("failed", "no metal of its net near the pin");
          tap_failed = true;
          tap_reason = "tap: no metal of its net near the pin";
          continue;
        }
        Signal_route_request req;
        req.anchors    = std::move(anchors);
        req.layer_cost = tap_cost;
        req.try_hard   = !N.critical;
        const size_t tap_from = pend.paths.size();
        if (auto w = piece(n, {fn.name, {{d.fp.components[p.comp].name, std::string(p.name)}}, {}, "SIGNAL"}, req, pend);
            !w.empty()) {
          ++rep.tap_failures;
          tap_event("failed", w);
          tap_failed = true;
          tap_reason = "tap: " + w;
          continue;
        }
        if (box_report) {
          box_ev.push_back({n,
                            pin_box(p),
                            d.fp.components[p.comp].name + "/" + std::string(p.name),
                            "tap",
                            via_chain(pend.paths, tap_from),
                            0,
                            0,
                            "ok",
                            ""});
        }
      }
      return tap_reason;
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
    const auto        route_start = Clock::now();
    size_t            completed = 0, failed_so_far = 0;
    std::vector<char> checked(d.nets.size(), 0);
    double            next_progress = 30;
    for (const size_t n : order) {
      if (d.opt.final_net_limit && completed >= d.opt.final_net_limit) {
        break;
      }
      checked[n] = 1;
      ++completed;
      if (!router) {
        why[n] = "no technology profile for the signal router";
        continue;
      }
      const size_t global = metal[n].size();
      Pending      pend;
      std::string  w;
      attempt = box_ev.size();
      if (!is_local(n) && !whole[n]) {
        w = route_tree(n, pend);
        if (!w.empty()) {  // the tree did not close: the router takes the whole net (the global metal is not written)
          close_events("rolled_back", "tree failed: " + w);
          entry[n] = "planned_tree_failed: " + w.substr(0, w.find(" ("));
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
      if (entry[n].empty()) {
        entry[n] = is_local(n) ? "local" : whole[n] ? "whole" : "planned_tree_closed";
      }
      if (is_local(n) || whole[n]) {
        w = route_whole(n, pend);
        if (!w.empty()) {
          w = (is_local(n) ? "local: " : "whole: ") + w;
        }
        if (box_report) {  // a whole/local request covers every cell pin of the net at once (a net-level outcome)
          for (const auto& p : d.nets[n].pins) {
            if (cell_pin(p)) {
              box_ev.push_back({n,
                                pin_box(p),
                                d.fp.components[p.comp].name + "/" + std::string(p.name),
                                is_local(n) ? "local" : "whole",
                                "",
                                0,
                                0,
                                w.empty() ? "ok" : "failed",
                                w});
            }
          }
        }
      }
      if (w.empty()) {
        w = finalize(n, pend);
      }
      if (box_report) {
        close_events(w.empty() ? "committed" : "rolled_back", w);
        if (w.empty()) {  // committed metal on the box layer and the lowest vertical layer, clipped to each box tile
          const auto use = [&](const ph::Shape& sh, const char* kind) {
            if (!d.tech.layers.contains(sh.layer) || sh.layer == "M1") {  // routing metal only (cut shapes carry no tracks)
              return;
            }
            const int c0 = g.col_of(sh.rect.xl), c1 = g.col_of(sh.rect.xh), r0 = g.row_of(sh.rect.yl), r1 = g.row_of(sh.rect.yh);
            for (int c = c0; c <= c1; ++c) {
              for (int r = r0; r <= r1; ++r) {
                const int tile = g.tile(c, r), b = box_in_tile[size_t(tile)];
                if (b < 0) {
                  continue;
                }
                const ph::Rect t{double(g.left(c)),
                                 double(g.bottom(r)),
                                 double(g.left(c) + g.tile_w),
                                 double(g.bottom(r) + g.tile_h)};
                const ph::Rect clip{std::max(sh.rect.xl, t.xl),
                                    std::max(sh.rect.yl, t.yl),
                                    std::min(sh.rect.xh, t.xh),
                                    std::min(sh.rect.yh, t.yh)};
                if (clip.xl < clip.xh && clip.yl < clip.yh) {
                  box_use.push_back({b, n, sh.layer, net_boxes[n].contains(b), kind, clip});
                }
              }
            }
          };
          for (size_t i = 0; i < global && i < metal[n].size(); ++i) {
            use(metal[n][i], whole[n] ? "abandoned" : "retained");
          }
          for (const auto& sh : pend.shapes) {
            use(sh, "new");
          }
        }
      }
      if (!w.empty()) {
        why[n] = std::move(w);
        metal[n].resize(global);
        ++failed_so_far;
      }
      if (completed % 64 == 0 && seconds_since(route_start) >= next_progress) {
        std::fprintf(stderr,
                     "legroute final pass %u: %zu/%zu nets, %zu failed, %zu tree fallbacks, %.1f s\n",
                     pass + 1,
                     completed,
                     order.size(),
                     failed_so_far,
                     rep.tree_fallbacks,
                     seconds_since(route_start));
        next_progress = seconds_since(route_start) + 30;
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
      best_box_ev  = box_ev;
      best_box_use = box_use;
      best_failed  = failed;
      best_rep     = rep;
      best_why     = why;
      best_entry   = entry;
      best_checked = checked;
      best_rules   = d.fp.nondefault_rules;
      best_paths.assign(d.nets.size(), {});
      best_vias.assign(d.nets.size(), 0);
      for (size_t n = 0; n < d.nets.size(); ++n) {
        best_paths[n] = net_geo[n].paths;
        best_vias[n]  = net_geo[n].vias;
      }
    }
    if (completed < order.size() || failed == 0 || !d.profile) {
      break;
    }
  }
  rep                              = *best_rep;
  rep.final_prior_cut_rejections   = prior_cut_rejections;
  rep.final_pending_cut_rejections = pending_cut_rejections;
  rep.final_union_cut_rejections   = union_cut_rejections;
  rep.final_precommit_disconnected = precommit_disconnected;
  why                              = std::move(best_why);
  d.fp.nondefault_rules            = std::move(best_rules);
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
  std::atomic<size_t>        connected = 0, connected_clocks = 0;
  parallel(d.nets.size(), d.opt.threads, [&](size_t n) {
    if (!job[n] || !best_checked[n] || !why[n].empty()) {
      return;
    }
    try {
      const auto c = ph::check_route_connectivity(d.tech, d.fp, d.fp.nets[d.nets[n].frame], &comps);
      if (c.connected) {
        ++connected;
        connected_clocks += d.nets[n].clock;
        return;
      }
      why[n] = std::format("disconnected: {} ({} components)", c.reason, c.components);
    } catch (const std::exception& e) {
      why[n] = std::string("disconnected: ") + e.what();
    }
  });
  rep.connected_nets       = connected;
  rep.clock_connected_nets = connected_clocks;
  rep.unchecked_nets       = 0;
  rep.clock_unchecked_nets = 0;
  for (size_t n = 0; n < d.nets.size(); ++n) {
    if (job[n] && !best_checked[n]) {
      ++rep.unchecked_nets;
      rep.clock_unchecked_nets += d.nets[n].clock;
    }
    if (!job[n] || !best_checked[n] || why[n].empty()) {
      continue;
    }
    ++rep.failed_nets;
    rep.clock_failed_nets += d.nets[n].clock;
    rep.disconnected_nets += why[n].starts_with("disconnected");
    ++rep.failure_reasons[why[n].substr(0, why[n].find(" ("))];
    if (rep.failure_sample.size() < 10) {
      rep.failure_sample.push_back(d.fp.nets[d.nets[n].frame].name + ": " + why[n]);
    }
  }
  // Per-net handoff record: one box or several, how the net entered detailed routing (local, no planned metal,
  // planned metal rejected, planned tree failed or closed), and its outcome. Classifies where the coarse/SAT
  // witness is lost before choosing a model fix.
  if (!d.opt.output_dir.empty() && !best_entry.empty()) {
    std::map<std::string, std::array<size_t, 4>> table;  // entry kind -> {one-box ok, one-box failed, multi ok, multi failed}
    std::ofstream                                nets_out(d.opt.output_dir + "/final_nets.tsv");
    nets_out << "net_index\tnet\tpins\tboxes\tio\tentry\toutcome\n";
    for (size_t n = 0; n < d.nets.size(); ++n) {
      if (!job[n] || !best_checked[n]) {
        continue;
      }
      std::set<int> in_boxes;
      bool          io = false;
      for (const auto& pin : d.nets[n].pins) {
        if (pin.comp >= 0 && d.cell[size_t(pin.comp)] >= 0) {
          in_boxes.insert(boxes.box_of[size_t(d.cell[size_t(pin.comp)])]);
        } else {
          io = true;
        }
      }
      const bool  one_box = in_boxes.size() <= 1 && !io;
      const auto& e       = best_entry[n];
      const auto  kind    = e.substr(0, e.find(':'));
      ++table[kind][(one_box ? 0 : 2) + (why[n].empty() ? 0 : 1)];
      nets_out << n << '\t' << std::quoted(d.fp.nets[d.nets[n].frame].name) << '\t' << d.nets[n].pins.size() << '\t'
               << in_boxes.size() << '\t' << io << '\t' << std::quoted(e) << '\t'
               << std::quoted(why[n].empty() ? std::string("connected") : why[n]) << '\n';
    }
    for (const auto& [kind, c] : table) {
      std::fprintf(stderr,
                   "legroute final nets %-24s one-box ok %zu failed %zu | multi-box ok %zu failed %zu\n",
                   kind.c_str(),
                   c[0],
                   c[1],
                   c[2],
                   c[3]);
    }
  }
  if (box_report) {
    // per box: identity, members, the cellbox budget on its tile, attempt phases of its cell pins and joins, and the
    // committed M2/M3 metal (retained planned vs new detailed) inside its tile; then every event and every rectangle
    const auto budget = [&](const std::string& layer, int tile) -> long {
      for (const auto& L : g.resources.layers) {
        if (L.name == layer && tile >= 0 && size_t(tile) < L.tiles.size()) {
          return long(L.tiles[size_t(tile)].cellbox_budget);
        }
      }
      return -1;
    };
    const std::string m2 = g.box_layer, m3 = lname(low_v);
    struct Sum {
      std::map<std::string, size_t> phase;  // kind:phase -> count
      std::set<size_t>              nets;
      double                        len[2][2] = {};  // [M2, M3][retained, new]
    };
    std::vector<Sum> sum(boxes.boxes.size());
    for (const auto& e : best_box_ev) {
      if (e.box >= 0) {
        ++sum[size_t(e.box)].phase[e.kind + ":" + e.phase];
        sum[size_t(e.box)].nets.insert(e.net);
      }
    }
    for (const auto& u : best_box_use) {  // rectangle lengths (same-net overlaps are not merged here)
      if (u.metal != "abandoned" && (u.layer == m2 || u.layer == m3)) {  // the summary columns: box layer and low_v
        const double len = std::max(u.rect.xh - u.rect.xl, u.rect.yh - u.rect.yl);
        sum[size_t(u.box)].len[u.layer == m2 ? 0 : 1][u.metal == "new" ? 1 : 0] += len;
      }
    }
    std::ofstream out(d.opt.output_dir + "/final_boxes.tsv");
    out << "box\tcol\trow\tcells\tmembers\tbudget_" << m2 << "\tbudget_" << m3 << "\tnets\tphases\t" << m2 << "_retained\t" << m2
        << "_new\t" << m3 << "_retained\t" << m3 << "_new\n";
    for (size_t b = 0; b < boxes.boxes.size(); ++b) {
      const auto& B = boxes.boxes[b];
      std::string members, phases;
      for (int c : B.cells) {
        members += (members.empty() ? "" : ",") + d.fp.components[size_t(d.movable[size_t(c)])].name;
      }
      for (const auto& [k, v] : sum[b].phase) {
        phases += std::format("{}{}={}", phases.empty() ? "" : ",", k, v);
      }
      out << b << '\t' << (B.tile >= 0 ? g.col(B.tile) : -1) << '\t' << (B.tile >= 0 ? g.row(B.tile) : -1) << '\t' << B.cells.size()
          << '\t' << members << '\t' << budget(m2, B.tile) << '\t' << budget(m3, B.tile) << '\t' << sum[b].nets.size() << '\t'
          << phases << '\t' << sum[b].len[0][0] << '\t' << sum[b].len[0][1] << '\t' << sum[b].len[1][0] << '\t' << sum[b].len[1][1]
          << '\n';
    }
    std::ofstream events(d.opt.output_dir + "/final_box_events.tsv");
    events << "box\tnet\tpin\tkind\tlayer\tx\ty\tphase\treason\n";
    for (const auto& e : best_box_ev) {
      events << e.box << '\t' << std::quoted(d.fp.nets[d.nets[e.net].frame].name) << '\t' << e.pin << '\t' << e.kind << '\t'
             << e.layer << '\t' << e.x << '\t' << e.y << '\t' << e.phase << '\t' << std::quoted(e.reason) << '\n';
    }
    std::ofstream usage(d.opt.output_dir + "/final_box_usage.tsv");
    usage << "box\tnet\tlayer\trole\tmetal\txl\tyl\txh\tyh\n";
    for (const auto& u : best_box_use) {
      usage << u.box << '\t' << std::quoted(d.fp.nets[d.nets[u.net].frame].name) << '\t' << u.layer << '\t'
            << (u.local ? "destination" : "transit") << '\t' << u.metal << '\t' << u.rect.xl << '\t' << u.rect.yl << '\t'
            << u.rect.xh << '\t' << u.rect.yh << '\n';
    }
  }
  if (d.opt.final_net_limit && !d.opt.output_dir.empty()) {
    std::ofstream diagnostic(d.opt.output_dir + "/final_diagnostic.tsv");
    diagnostic << "net_index\tnet\tlocal\tpins\tcritical\tclock\treason\tendpoints\n";
    for (size_t n = 0; n < d.nets.size(); ++n) {
      if (!job[n] || !best_checked[n]) {
        continue;
      }
      const auto& net = d.nets[n];
      diagnostic << n << '\t' << std::quoted(d.fp.nets[net.frame].name) << '\t' << is_local(n) << '\t' << net.pins.size() << '\t'
                 << net.critical << '\t' << net.clock << '\t' << std::quoted(why[n]);
      for (const auto& pin : net.pins) {
        diagnostic << '\t'
                   << std::quoted(pin.comp >= 0 ? d.fp.components[pin.comp].name + "/" + std::string(pin.name)
                                                : "PIN/" + std::string(pin.name));
      }
      diagnostic << '\n';
    }
    if (!diagnostic) {
      throw std::runtime_error("legroute: cannot write final diagnostic");
    }
  }
  rep.wires_emitted = rep.failed_nets == 0 && rep.unchecked_nets == 0 && d.profile != nullptr;
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
