// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// legroute L1: the netlist view, the tile grid, per-tile free row segments, per-layer track capacity and the
// region tile sets.
#include <algorithm>
#include <cmath>
#include <format>
#include <numeric>
#include <set>
#include <stdexcept>
#include <unordered_map>

#include "inou/lefdef/routing_geometry.hpp"
#include "legroute_internal.hpp"
#include "row_occupancy.hpp"

namespace livehd::placement {
namespace ph = livehd::physical;
namespace lr {
namespace {
bool has_clock_pin(const ph::Macro& m) {
  return std::any_of(m.pins.begin(), m.pins.end(), [](const auto& p) { return p.second.use == "CLOCK"; });
}
bool power_use(const std::string& use) { return use == "POWER" || use == "GROUND"; }
// A flop input that is not data, scan or clock: an asynchronous set/reset (left out of criticality).
bool reset_pin(const ph::Macro& m, const std::string& pin, const ph::Pin& p) {
  if (!has_clock_pin(m) || p.direction != "INPUT" || p.use == "CLOCK") {
    return false;
  }
  return !(pin.starts_with("D") || pin.starts_with("SI") || pin.starts_with("SE") || pin.starts_with("Q"));
}
int64_t row_height(const ph::Technology& tech, const ph::Floorplan& fp) {
  if (fp.rows.empty()) {
    throw std::invalid_argument("legroute requires placement rows");
  }
  const auto site = tech.sites.find(fp.rows.front().site);
  if (site == tech.sites.end()) {
    throw std::invalid_argument("legroute: unknown row site " + fp.rows.front().site);
  }
  return std::llround(site->second.height * fp.dbu);
}
}  // namespace

std::pair<int64_t, int64_t> Grid::tracks(int l, int band) const {
  const auto& L    = layers[l];
  auto        lo   = L.horizontal ? bottom(band) : left(band);
  auto        hi   = lo + (L.horizontal ? tile_h : tile_w);
  // a track keeps one pitch inside the die (edge tiles may reach past it)
  lo               = std::max(lo, int64_t(std::ceil(L.horizontal ? die.yl : die.xl)) + L.pitch);
  hi               = std::min(hi, int64_t(std::floor(L.horizontal ? die.yh : die.xh)) - L.pitch + 1);
  const auto first = int64_t(std::ceil(double(lo - L.offset) / L.pitch));
  const auto last  = int64_t(std::ceil(double(hi - L.offset) / L.pitch)) - 1;
  return {first, last};
}

Design build_design(const ph::Technology& tech, ph::Floorplan& fp, const timing::Feedback& timing, const Legroute_options& opt,
                    const ph::Tech_profile* profile) {
  Design                                    d{tech, fp, opt, profile};
  const auto                                row_h = row_height(tech, fp);
  std::unordered_map<std::string_view, int> comp_of, term_of;
  d.macro.resize(fp.components.size());
  d.cell.assign(fp.components.size(), -1);
  for (size_t i = 0; i < fp.components.size(); ++i) {
    const auto& c = fp.components[i];
    const auto  m = tech.macros.find(c.macro);
    if (m == tech.macros.end()) {
      throw std::invalid_argument("legroute: unknown master " + c.macro);
    }
    d.macro[i] = &m->second;
    comp_of.emplace(c.name, int(i));
    if (frame_role(tech, c) != Frame_role::movable) {
      continue;
    }
    if (std::llround(m->second.height * fp.dbu) > row_h) {
      throw std::invalid_argument("legroute: multi-row cell " + c.name + " must be anchored (fixed) first");
    }
    d.cell[i] = int(d.movable.size());
    d.movable.push_back(int(i));
    d.width.push_back(std::llround(m->second.width * fp.dbu));
    d.flop.push_back(has_clock_pin(m->second));
    d.widest = std::max(d.widest, d.width.back());
    if (d.flop.back()) {
      d.flop_w = std::max(d.flop_w, d.width.back());
    }
  }
  for (size_t i = 0; i < fp.terminals.size(); ++i) {
    term_of.emplace(fp.terminals[i].name, int(i));
  }
  d.nets_of.resize(d.movable.size());
  const double unit = timing.time_unit_ps > 0 && std::isfinite(timing.time_unit_ps) ? timing.time_unit_ps : 1;
  std::vector<std::pair<double, int>> timed;
  for (size_t n = 0; n < fp.nets.size(); ++n) {
    const auto& fn = fp.nets[n];
    Net         net;
    net.frame = n;
    net.clock = fn.use == "CLOCK";
    net.power = power_use(fn.use);
    for (const auto& c : fn.connections) {
      Pin p;
      if (c.instance == "PIN") {
        const auto t = term_of.find(c.pin);
        if (t == term_of.end()) {
          continue;
        }
        p.term     = t->second;
        p.name     = fp.terminals[t->second].name;
        p.output   = fp.terminals[t->second].direction == "INPUT";  // a design input drives the net
        net.power |= power_use(fp.terminals[t->second].use);
      } else {
        const auto ci = comp_of.find(c.instance);
        if (ci == comp_of.end()) {
          continue;
        }
        p.comp         = ci->second;
        const auto& m  = *d.macro[ci->second];
        const auto  lp = m.pins.find(c.pin);
        if (lp == m.pins.end()) {
          continue;
        }
        p.lef      = &lp->second;
        p.name     = lp->first;
        p.output   = lp->second.direction == "OUTPUT";
        p.clock    = lp->second.use == "CLOCK";
        net.clock |= p.clock;
        net.power |= power_use(lp->second.use);
        net.reset |= reset_pin(m, c.pin, lp->second);
      }
      if (p.output && net.driver < 0) {
        net.driver = int(net.pins.size());
      }
      net.pins.push_back(p);
    }
    if (net.driver < 0 && !net.pins.empty()) {
      net.driver = 0;
    }
    net.routed = !net.clock && !net.power && net.pins.size() >= 2;
    if (net.routed && !net.reset) {
      for (const auto& p : net.pins) {
        // full or ranking-only feedback alike
        const auto r = timing.pin_rank(p.comp >= 0 ? fp.components[p.comp].name : "PIN", std::string(p.name));
        if (!r.clock && std::isfinite(r.slack)) {
          net.slack = std::min(net.slack, r.slack * unit);
        }
      }
      if (std::isfinite(net.slack)) {
        timed.emplace_back(net.slack, int(d.nets.size()));
      }
    }
    for (const auto& p : net.pins) {
      if (p.comp >= 0 && d.cell[p.comp] >= 0) {
        auto& v = d.nets_of[d.cell[p.comp]];
        if (v.empty() || v.back() != int(d.nets.size())) {
          v.push_back(int(d.nets.size()));
        }
      }
    }
    d.nets.push_back(std::move(net));
  }
  std::ranges::sort(timed);
  if (!timed.empty()) {
    d.wns           = timed.front().first;
    const double th = d.wns + opt.crit_window_ps;
    for (size_t k = 0; k < timed.size(); ++k) {
      auto& net    = d.nets[timed[k].second];
      net.rank     = double(k) / timed.size();
      net.ramp     = std::max(0.0, 1 - net.rank / opt.crit_fraction);
      net.critical = net.rank < opt.crit_fraction;
      net.crit     = std::clamp((th - net.slack) / std::max(1e-9, th - d.wns), 0.0, 1.0);
      net.weight   = 1 + unsigned(std::lround(8 * net.crit * net.crit));
    }
  }
  for (auto& v : d.nets_of) {
    std::ranges::sort(v);
    v.erase(std::unique(v.begin(), v.end()), v.end());
  }
  return d;
}

std::vector<int> pack_segments(const std::vector<int>& widths, const std::vector<int>& room0, const std::vector<int>* prefer) {
  std::vector<int> room = room0, out(widths.size(), -1), order(widths.size());
  std::iota(order.begin(), order.end(), 0);
  std::ranges::stable_sort(order, [&](int a, int b) { return widths[a] > widths[b]; });
  for (int i : order) {
    int best = -1;
    if (prefer && (*prefer)[i] >= 0 && room[(*prefer)[i]] >= widths[i]) {
      best = (*prefer)[i];
    } else {
      for (size_t s = 0; s < room.size(); ++s) {
        if (room[s] >= widths[i] && (best < 0 || room[s] > room[best])) {
          best = int(s);
        }
      }
    }
    if (best < 0) {
      return {};
    }
    room[best] -= widths[i];
    out[i]      = best;
  }
  return out;
}

Grid build_grid(const Design& d, const Legroute_regions& regions) {
  const auto& tech = d.tech;
  const auto& fp   = d.fp;
  Grid        g;
  g.dbu     = fp.dbu;
  auto rows = row_gaps(tech, fp, "legroute");
  g.site    = rows.front().step;
  g.row_h   = rows.front().height;
  for (const auto& r : rows) {
    if (r.step != g.site || r.height != g.row_h) {
      throw std::invalid_argument("legroute requires one row site");
    }
  }
  for (size_t i = 0; i < fp.components.size(); ++i) {
    const auto& c = fp.components[i];
    switch (frame_role(tech, c)) {
      case Frame_role::obstacle: reserve_gaps(rows, obstacle_rect(tech, c, d.opt.macro_halo_x_dbu, d.opt.macro_halo_y_dbu)); break;
      case Frame_role::fixed_logic: reserve_gaps(rows, padded_rect(tech, c, 0)); break;
      default                     : break;
    }
  }
  int64_t xh = std::numeric_limits<int64_t>::min(), yh = xh;
  g.x0 = g.y0 = std::numeric_limits<int64_t>::max();
  for (const auto& r : rows) {
    g.x0 = std::min(g.x0, r.x);
    g.y0 = std::min(g.y0, r.y);
    xh   = std::max(xh, r.x + r.count * r.step);
    yh   = std::max(yh, r.y + r.height);
  }
  for (const auto& r : rows) {
    if ((r.x - g.x0) % g.site || (r.y - g.y0) % g.row_h) {
      throw std::invalid_argument("legroute requires rows on one site and row grid");
    }
  }
  const auto widest = std::max({2 * d.flop_w, d.widest, g.site});
  g.tile_w          = (widest + g.site - 1) / g.site * g.site;
  g.tile_h          = 2 * g.row_h;
  g.sites           = int(g.tile_w / g.site);
  g.die             = fp.die;
  g.cols            = int((xh - g.x0 + g.tile_w - 1) / g.tile_w);
  const int prow    = int((yh - g.y0) / g.row_h);
  g.rows            = (prow + 1) / 2;
  g.row_orient.assign(2 * g.rows, ph::Orientation::N);
  g.row_present.assign(2 * g.rows, 0);
  // free sites per placement row, absolute site index from x0
  std::vector<std::vector<std::pair<int64_t, int64_t>>> free(2 * g.rows);
  for (const auto& r : rows) {
    const auto k      = (r.y - g.y0) / g.row_h;
    g.row_orient[k]   = fp.rows[r.source].orientation;
    g.row_present[k]  = 1;
    const auto offset = (r.x - g.x0) / g.site;
    for (auto [lo, hi] : r.free) {
      free[k].emplace_back(lo + offset, hi + offset);
    }
  }
  std::map<std::vector<Seg>, uint32_t> key_of;
  g.key.resize(g.size());
  g.blocked.assign(g.size(), 0);
  for (int r = 0; r < g.rows; ++r) {
    for (int c = 0; c < g.cols; ++c) {
      std::vector<Seg> segs;
      const int64_t    lo = int64_t(c) * g.sites, hi = lo + g.sites;
      for (int k = 0; k < 2; ++k) {
        for (auto [a, b] : free[2 * r + k]) {
          const auto s = std::max(a, lo), e = std::min(b, hi);
          if (s < e) {
            segs.push_back({uint8_t(k), int32_t(s - lo), int32_t(e - lo)});
          }
        }
      }
      std::ranges::sort(segs);
      const auto [it, fresh] = key_of.try_emplace(segs, uint32_t(g.keys.size()));
      if (fresh) {
        g.keys.push_back(segs);
      }
      g.key[g.tile(c, r)]     = it->second;
      g.blocked[g.tile(c, r)] = segs.empty();
    }
  }
  // regions: a tile belongs to the rectangle holding its center (the nearest one otherwise)
  g.region.assign(g.size(), 0);
  if (!regions.rect_region.empty() && regions.rect_region.size() != regions.rects.size()) {
    throw std::invalid_argument("legroute: rect_region must name the region of every rectangle");
  }
  const auto region_of_rect = [&](size_t k) { return regions.rect_region.empty() ? int(k) : regions.rect_region[k]; };
  g.regions                 = 1;
  for (size_t k = 0; k < regions.rects.size(); ++k) {
    if (region_of_rect(k) < 0) {
      throw std::invalid_argument("legroute: negative region id");
    }
    g.regions = std::max(g.regions, region_of_rect(k) + 1);
  }
  if (!regions.rects.empty()) {
    for (size_t t = 0; t < g.size(); ++t) {
      const double x = g.cx(g.col(int(t))), y = g.cy(g.row(int(t)));
      double       best = INFINITY;
      for (size_t k = 0; k < regions.rects.size(); ++k) {
        const auto& q    = regions.rects[k];
        const auto  dist = std::max({0.0, q.xl - x, x - q.xh}) + std::max({0.0, q.yl - y, y - q.yh});
        const bool  in   = x >= q.xl && x < q.xh && y >= q.yl && y < q.yh;
        if (in || dist < best) {
          best        = in ? -1 : dist;
          g.region[t] = region_of_rect(k);
          if (in) {
            break;
          }
        }
      }
    }
  }
  g.region_tiles.assign(g.regions, {});
  for (size_t t = 0; t < g.size(); ++t) {
    g.region_tiles[g.region[t]].push_back(int(t));
  }
  // routing layers: the profile's signal layers above the box (escape) layer, else the LEF stack above the
  // second routing layer, at most six
  std::vector<std::string> stack;
  if (d.profile) {
    g.pin_layer = d.profile->route.access.pin;
    g.box_layer = d.profile->route.access.escape;
    g.box_cut   = d.profile->route.access.cut;
    stack       = d.profile->route.signal_layers;
  } else {
    if (tech.routing_layers.size() < 3) {
      throw std::invalid_argument("legroute requires at least three routing layers");
    }
    g.pin_layer = tech.routing_layers[0];
    g.box_layer = tech.routing_layers[1];
    stack.assign(tech.routing_layers.begin() + 1, tech.routing_layers.begin() + std::min<size_t>(7, tech.routing_layers.size()));
  }
  bool above = false;
  for (const auto& name : stack) {
    if (name == g.box_layer) {
      above = true;
      continue;
    }
    if (!above) {
      continue;
    }
    const auto it = tech.layers.find(name);
    if (it == tech.layers.end()) {
      throw std::invalid_argument("legroute: unknown routing layer " + name);
    }
    const auto& L = it->second;
    Layer       l;
    l.name             = name;
    l.horizontal       = L.direction == "HORIZONTAL";
    const double pitch = l.horizontal ? (L.pitch_y > 0 ? L.pitch_y : L.pitch_x) : (L.pitch_x > 0 ? L.pitch_x : L.pitch_y);
    const double off   = L.has_offset ? (l.horizontal ? L.offset_y : L.offset_x) : pitch / 2;
    l.pitch            = std::llround(pitch * fp.dbu);
    l.offset           = std::llround(off * fp.dbu);
    for (const auto& t : fp.tracks) {  // DEF TRACKS win over the LEF pitch: TRACKS Y are horizontal lines
      if (t.layer == name && t.supported() && (t.axis == "Y") == l.horizontal) {
        l.pitch  = t.step;
        l.offset = t.start;
        break;
      }
    }
    if (l.pitch <= 0) {
      throw std::invalid_argument("legroute: layer " + name + " has no pitch");
    }
    l.width = std::llround(L.width * fp.dbu);
    l.r_um  = L.width > 0 && L.resistance > 0 ? L.resistance / L.width / 1000 : .03;
    l.c_um  = L.capacitance > 0 ? (L.capacitance * L.width + 2 * L.edge_cap) * 1000 : .18;
    g.layers.push_back(std::move(l));
  }
  if (g.layers.size() < 2 || std::none_of(g.layers.begin(), g.layers.end(), [](const auto& l) { return l.horizontal; })
      || std::all_of(g.layers.begin(), g.layers.end(), [](const auto& l) { return l.horizontal; })) {
    throw std::invalid_argument("legroute needs horizontal and vertical routing layers above " + g.box_layer);
  }
  g.cap.assign(g.layers.size(), std::vector<int16_t>(g.size(), 0));
  for (size_t l = 0; l < g.layers.size(); ++l) {
    for (int t = 0; t < int(g.size()); ++t) {
      const auto [a, b] = g.tracks(int(l), g.layers[l].horizontal ? g.row(t) : g.col(t));
      g.cap[l][t]       = int16_t(std::max<int64_t>(0, b - a + 1 - d.opt.reserved_tracks));
    }
  }
  std::map<std::string, size_t> layer_index;
  for (size_t l = 0; l < g.layers.size(); ++l) {
    layer_index[g.layers[l].name] = l;
  }
  // power stripes take the tracks they cover in every tile they cross
  g.obstacles.assign(g.layers.size(), {});
  const auto take = [&](size_t l, const ph::Rect& r) {
    const auto& L = g.layers[l];
    g.obstacles[l].push_back(r);
    if (L.horizontal != (r.xh - r.xl >= r.yh - r.yl)) {
      return;  // a via landing or a perpendicular strap: it blocks its span, not whole tracks
    }
    const double lo = (L.horizontal ? r.yl : r.xl) - L.pitch / 2.0, hi = (L.horizontal ? r.yh : r.xh) + L.pitch / 2.0;
    const int    c0 = g.col_of(r.xl), c1 = g.col_of(r.xh - 1), r0 = g.row_of(r.yl), r1 = g.row_of(r.yh - 1);
    for (int rr = r0; rr <= r1; ++rr) {
      for (int cc = c0; cc <= c1; ++cc) {
        const auto [a, b] = g.tracks(int(l), L.horizontal ? rr : cc);
        int covered       = 0;
        for (auto k = a; k <= b; ++k) {
          const auto x  = g.track_coord(int(l), k);
          covered      += x > lo && x < hi;
        }
        auto& cap = g.cap[l][g.tile(cc, rr)];
        cap       = int16_t(std::max(0, cap - covered));
      }
    }
  };
  for (const auto& net : fp.special_nets) {
    if (net.wires.empty()) {
      continue;
    }
    for (const auto& s : ph::route_geometry(tech, fp, net, true)) {
      if (const auto l = layer_index.find(s.layer); l != layer_index.end()) {
        take(l->second, s.rect);
      }
    }
  }
  for (const auto& s : regions.obstacles) {
    if (const auto l = layer_index.find(s.layer); l != layer_index.end()) {
      take(l->second, s.rect);
    }
  }
  // macros block the layers of their obstructions and pins over their footprint
  for (size_t i = 0; i < fp.components.size(); ++i) {
    const auto& c = fp.components[i];
    if (frame_role(tech, c) != Frame_role::obstacle || d.macro[i]->klass.starts_with("CORE")) {
      continue;
    }
    std::set<std::string> used = d.macro[i]->blocked_layers;
    for (const auto& o : d.macro[i]->obstructions) {
      used.insert(o.layer);
    }
    for (const auto& [_, p] : d.macro[i]->pins) {
      for (const auto& s : p.shapes) {
        used.insert(s.layer);
      }
    }
    const ph::Rect body{double(c.place.x),
                        double(c.place.y),
                        double(c.place.x + std::llround(d.macro[i]->width * fp.dbu)),
                        double(c.place.y + std::llround(d.macro[i]->height * fp.dbu))};
    if (ph::swaps_axes(c.place.orientation)) {
      continue;  // rotated hard macros are not supported by the snap either; their rows stay blocked
    }
    for (const auto& name : used) {
      const auto l = layer_index.find(name);
      if (l == layer_index.end()) {
        continue;
      }
      g.obstacles[l->second].push_back(body);
      for (int rr = g.row_of(body.yl); rr <= g.row_of(body.yh - 1); ++rr) {
        for (int cc = g.col_of(body.xl); cc <= g.col_of(body.xh - 1); ++cc) {
          g.cap[l->second][g.tile(cc, rr)] = 0;
        }
      }
    }
  }
  // box-layer tracks per tile: in-box routes and local nets use them, never the global routes. The union of the
  // layer's DEF TRACKS patterns (multi-pattern layers), less the tracks that the power rails' spacing takes.
  {
    std::vector<int64_t> ys_all;
    for (const auto& t : fp.tracks) {
      if (t.layer == g.box_layer && t.axis == "Y" && t.supported()) {
        for (int64_t k = 0; k < t.count; ++k) {
          ys_all.push_back(t.start + k * t.step);
        }
      }
    }
    const auto box = tech.layers.find(g.box_layer);
    if (ys_all.empty() && box != tech.layers.end()) {
      const auto& L     = box->second;
      const auto  pitch = std::llround((L.pitch_y > 0 ? L.pitch_y : L.pitch_x) * fp.dbu);
      const auto  off   = L.has_offset ? std::llround(L.offset_y * fp.dbu) : pitch / 2;
      for (auto y = g.y0 + off; pitch > 0 && y < g.y0 + int64_t(g.rows) * g.tile_h; y += pitch) {
        ys_all.push_back(y);
      }
    }
    std::ranges::sort(ys_all);
    ys_all.erase(std::unique(ys_all.begin(), ys_all.end()), ys_all.end());
    int64_t keep = 0;  // half a wire plus the minimum spacing, from a rail edge
    if (box != tech.layers.end()) {
      double space = INFINITY;
      for (const auto& sp : box->second.rules.spacing) {
        space = std::min(space, sp.distance);
      }
      keep = std::llround(box->second.width * fp.dbu / 2 + (std::isfinite(space) ? space * fp.dbu : 0));
    }
    std::vector<ph::Rect> rails;
    const auto            add_rail = [&](const ph::Shape& s) {
      if (s.layer == g.box_layer && s.rect.xh - s.rect.xl > s.rect.yh - s.rect.yl) {
        rails.push_back(s.rect);
      }
    };
    for (const auto& net : fp.special_nets) {
      for (const auto& s : ph::route_geometry(tech, fp, net, true)) {
        add_rail(s);
      }
    }
    for (const auto& s : regions.obstacles) {
      add_rail(s);
    }
    g.box_cap.assign(g.size(), 0);
    for (int tr = 0; tr < g.rows; ++tr) {
      const auto lo = std::lower_bound(ys_all.begin(), ys_all.end(), g.bottom(tr));
      const auto hi = std::lower_bound(ys_all.begin(), ys_all.end(), g.bottom(tr) + g.tile_h);
      for (int tc = 0; tc < g.cols; ++tc) {
        const double txl = double(g.left(tc)), txh = txl + double(g.tile_w);
        int          n = 0;
        for (auto it = lo; it != hi; ++it) {
          const double y  = double(*it);
          n              += std::none_of(rails.begin(), rails.end(), [&](const ph::Rect& q) {
            return q.xl < txh && q.xh > txl && y > q.yl - double(keep) && y < q.yh + double(keep);
          });
        }
        const int t  = g.tile(tc, tr);
        g.box_cap[t] = g.blocked[t] ? 0 : int16_t(std::max<int64_t>(0, n - d.opt.reserved_tracks));
      }
    }
  }
  // obstacles by band (tile row for horizontal layers, column for vertical), one pitch of slack
  g.obstacle_band.assign(g.layers.size(), {});
  for (size_t l = 0; l < g.layers.size(); ++l) {
    const auto& L = g.layers[l];
    g.obstacle_band[l].assign(L.horizontal ? g.rows : g.cols, {});
    for (int k = 0; k < int(g.obstacles[l].size()); ++k) {
      const auto& r  = g.obstacles[l][k];
      const auto  lo = (L.horizontal ? r.yl : r.xl) - L.pitch, hi = (L.horizontal ? r.yh : r.xh) + L.pitch;
      const int   b0 = L.horizontal ? g.row_of(lo) : g.col_of(lo), b1 = L.horizontal ? g.row_of(hi) : g.col_of(hi);
      for (int b = b0; b <= b1; ++b) {
        g.obstacle_band[l][b].push_back(k);
      }
    }
  }
  return g;
}
}  // namespace lr

Legroute_tile legroute_tile(const ph::Technology& tech, const ph::Floorplan& fp) {
  Legroute_tile t;
  if (fp.rows.empty()) {
    return t;
  }
  const auto row_h = lr::row_height(tech, fp);
  int64_t    flop = 0, site = std::llround(tech.sites.at(fp.rows.front().site).width * fp.dbu);
  for (const auto& c : fp.components) {
    const auto m = tech.macros.find(c.macro);
    if (m != tech.macros.end() && m->second.klass.starts_with("CORE") && lr::has_clock_pin(m->second)) {
      flop = std::max(flop, std::llround(m->second.width * fp.dbu));
    }
  }
  if (flop <= 0 || site <= 0) {
    return t;
  }
  t.x0 = t.y0 = std::numeric_limits<int64_t>::max();
  for (const auto& r : fp.rows) {
    t.x0 = std::min(t.x0, r.x);
    t.y0 = std::min(t.y0, r.y);
  }
  t.w = (2 * flop + site - 1) / site * site;
  t.h = 2 * row_h;
  return t;
}

std::pair<int64_t, int64_t> snap_to_tiles(const Legroute_tile& t, int64_t x, int64_t y, int64_t halo_x, int64_t halo_y) {
  if (!t.valid()) {
    return {x, y};
  }
  const auto snap = [](int64_t v, int64_t o, int64_t step) {
    const double k = std::round(double(v - o) / step);
    return o + int64_t(k) * step;
  };
  return {snap(x - halo_x, t.x0, t.w) + halo_x, snap(y - halo_y, t.y0, t.h) + halo_y};
}
}  // namespace livehd::placement
