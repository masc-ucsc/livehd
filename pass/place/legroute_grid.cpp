// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// legroute grid: the netlist view, the tile grid, the free row sites per tile and the per-layer track capacity
// (todo/livehd/2r-route.html, R7).
#include <algorithm>
#include <cmath>
#include <map>
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
    // a two-row cell (a domino master) is a tile tall; taller cells must be anchored (fixed) first
    const auto height = std::llround(m->second.height * fp.dbu);
    if (height > row_h && height != 2 * row_h) {
      throw std::invalid_argument("legroute: multi-row cell " + c.name + " must be anchored (fixed) first");
    }
    d.cell[i] = int(d.movable.size());
    d.movable.push_back(int(i));
    const auto width = std::llround(m->second.width * fp.dbu);
    d.widest         = std::max<int64_t>(d.widest, width);
    if (has_clock_pin(m->second) && height != 2 * row_h) {  // the tile is two flops wide: single-row flops set it
      d.flop_w = std::max<int64_t>(d.flop_w, width);
    }
  }
  for (size_t i = 0; i < fp.terminals.size(); ++i) {
    term_of.emplace(fp.terminals[i].name, int(i));
  }
  const double unit = timing.time_unit_ps > 0 && std::isfinite(timing.time_unit_ps) ? timing.time_unit_ps : 1;
  double       wns  = INFINITY;
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
      wns = std::min(wns, net.slack);
    }
    d.nets.push_back(std::move(net));
  }
  if (std::isfinite(wns)) {  // window criticality: the router's order
    const double th = wns + opt.crit_window_ps;
    for (auto& net : d.nets) {
      if (net.routed && !net.reset && std::isfinite(net.slack)) {
        net.crit = std::clamp((th - net.slack) / std::max(1e-9, th - wns), 0.0, 1.0);
      }
    }
  }
  return d;
}

Pin_access physical_pin_access(const Design& d, const Grid& g, const Pin& pin) {
  const auto access = [&](double x, double y) { return Pin_access{x, y, g.tile_of(x, y)}; };
  if (pin.term >= 0) {
    const auto& term = d.fp.terminals.at(size_t(pin.term));
    if (term.shapes.empty()) {
      return access(term.x, term.y);
    }
    const auto rect = ph::orient_rect(term.shapes.front().rect, 0, 0, term.orientation).translated(term.x, term.y);
    return access((rect.xl + rect.xh) / 2, (rect.yl + rect.yh) / 2);
  }
  const auto&    component = d.fp.components.at(size_t(pin.comp));
  const auto&    macro     = *d.macro.at(size_t(pin.comp));
  const double   width     = (ph::swaps_axes(component.place.orientation) ? macro.height : macro.width) * d.fp.dbu;
  const double   height    = (ph::swaps_axes(component.place.orientation) ? macro.width : macro.height) * d.fp.dbu;
  const ph::Rect body{double(component.place.x), double(component.place.y), component.place.x + width, component.place.y + height};
  auto           result = access((body.xl + body.xh) / 2, (body.yl + body.yh) / 2);
  if (!pin.lef || pin.lef->shapes.empty()) {
    return result;  // No rectangular pin geometry: preserve the prior fallback; final access is still required.
  }
  const auto first = ph::placed_rect(macro, component.place, d.fp.dbu, pin.lef->shapes.front().rect);
  result           = access((first.xl + first.xh) / 2, (first.yl + first.yh) / 2);
  if (macro.klass.starts_with("CORE")) {
    return result;
  }
  double best = INFINITY;
  for (const auto& shape : pin.lef->shapes) {
    const auto found = std::ranges::find(g.layers, shape.layer, &Layer::name);
    if (found == g.layers.end()) {
      continue;
    }
    const auto   layer     = size_t(found - g.layers.begin());
    const auto&  L         = *found;
    const auto   rect      = ph::placed_rect(macro, component.place, d.fp.dbu, shape.rect);
    const double half      = L.width / 2.0;
    // A whole-width preferred-direction track must touch this pin. Select the
    // nearest such track to its center; no nearest-free-tile snapping is used.
    const double across_lo = (L.horizontal ? rect.yl : rect.xl) + half;
    const double across_hi = (L.horizontal ? rect.yh : rect.xh) - half;
    const auto   lo        = int64_t(std::ceil((across_lo - L.offset) / L.pitch));
    const auto   hi        = int64_t(std::floor((across_hi - L.offset) / L.pitch));
    if (lo > hi) {
      continue;
    }
    const auto   middle = int64_t(std::llround(((across_lo + across_hi) / 2 - L.offset) / L.pitch));
    const double track  = g.track_coord(int(layer), std::clamp(middle, lo, hi));
    for (bool high : {false, true}) {
      const double edge   = L.horizontal ? (high ? body.xh : body.xl) : (high ? body.yh : body.yl);
      const double pin_lo = L.horizontal ? rect.xl : rect.yl, pin_hi = L.horizontal ? rect.xh : rect.yh;
      if (pin_lo > edge || pin_hi < edge) {
        continue;  // Interior pins cannot teleport through the macro body.
      }
      const double origin = L.horizontal ? g.x0 : g.y0, step = L.horizontal ? g.tile_w : g.tile_h;
      const int    band = high ? int(std::ceil((edge - origin) / step)) : int(std::floor((edge - origin) / step)) - 1;
      const int    col = L.horizontal ? band : g.col_of(track), row = L.horizontal ? g.row_of(track) : band;
      if (col < 0 || col >= g.cols || row < 0 || row >= g.rows || g.cap[layer][size_t(g.tile(col, row))] <= 0) {
        continue;
      }
      const double   along = L.horizontal ? g.cx(col) : g.cy(row);
      const ph::Rect span  = L.horizontal ? ph::Rect{std::min(edge, along), track - half, std::max(edge, along), track + half}
                                          : ph::Rect{track - half, std::min(edge, along), track + half, std::max(edge, along)};
      if (span.xl < g.die.xl || span.yl < g.die.yl || span.xh > g.die.xh || span.yh > g.die.yh) {
        continue;
      }
      const auto overlaps = [&](const ph::Rect& obstacle) {
        return span.xl < obstacle.xh && obstacle.xl < span.xh && span.yl < obstacle.yh && obstacle.yl < span.yh;
      };
      if (std::ranges::any_of(g.obstacles[layer], overlaps)) {
        continue;
      }
      const double distance = std::abs(along - (pin_lo + pin_hi) / 2);
      if (distance < best) {
        best   = distance;
        result = L.horizontal ? access(along, track) : access(track, along);
      }
    }
  }
  return result;
}

Grid build_grid(const Design& d, const Legroute_power& power) {
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
  // Use the same row complement and hard DEF blockages as the placer.
  // Derive on a frame-only copy, never copy a large routed netlist.
  ph::Floorplan row_frame;
  row_frame.rows      = fp.rows;
  row_frame.die       = fp.die;
  row_frame.dbu       = fp.dbu;
  row_frame.blockages = fp.blockages;
  std::set<std::string_view> associated;
  for (const auto& b : fp.blockages) {
    if (!b.component.empty()) {
      associated.insert(b.component);
    }
  }
  for (const auto& c : fp.components) {
    if (associated.contains(c.name.view())
        || ((c.place.status == ph::Status::FIXED || c.place.status == ph::Status::LOCKED) && ph::hard_macro(tech.macros.at(c.macro)))) {
      row_frame.components.push_back(c);
    }
    if (ph::placement_mask(c, tech) == ph::Placement_mask::hard) {
      row_frame.blockages.push_back({false, {}, {obstacle_rect(tech, c, 0, 0)}});
    }
  }
  std::vector<ph::Rect> routing_gaps;
  ph::rectangular_placement_frame(row_frame, tech, &routing_gaps);
  // rows already contain only the original site segments and have reserved fixed occupancy.
  // Keep the true notches blocked on every layer; per-master LEF geometry governs a fixed macro.
  if (!routing_gaps.empty()) {
    row_frame.blockages.push_back({false, {}, std::move(routing_gaps)});
  }
  auto exclusions = ph::placement_exclusions(row_frame, {double(g.x0), double(g.y0), double(xh), double(yh)});
  std::erase_if(exclusions, [](const auto& e) { return e.kind != ph::Placement_mask::hard; });
  for (const auto& e : exclusions) {
    reserve_gaps(rows, e.rect);
  }
  const auto widest = std::max({2 * d.flop_w, d.widest, g.site});
  g.tile_w          = (widest + g.site - 1) / g.site * g.site;
  g.tile_h          = g.rows_per_tile * g.row_h;
  g.sites           = int(g.tile_w / g.site);
  g.die             = fp.die;
  g.cols            = int((xh - g.x0 + g.tile_w - 1) / g.tile_w);
  const int prow    = int((yh - g.y0) / g.row_h);
  g.rows            = (prow + int(g.rows_per_tile) - 1) / int(g.rows_per_tile);
  // Free sites per tile after fixed bodies and hard blockages. A movable cell's own sites are not subtracted:
  // its area is the load this capacity is compared with.
  g.placeable_sites.assign(g.size(), 0);
  for (const auto& r : rows) {
    const auto k      = (r.y - g.y0) / g.row_h;
    const auto offset = (r.x - g.x0) / g.site;
    for (auto [lo, hi] : r.free) {
      const int64_t a = lo + offset, b = hi + offset;  // absolute site index from x0
      for (int64_t c = a / g.sites; c * g.sites < b; ++c) {
        const auto tile          = size_t(g.tile(int(c), int(k) / int(g.rows_per_tile)));
        g.placeable_sites[tile] += std::min(b, (c + 1) * g.sites) - std::max(a, c * g.sites);
      }
    }
  }
  // routing layers: the profile's signal layers above the escape layer, else the LEF stack above the second
  // routing layer, at most six. A two-layer stack has no layer to spare for pin escape: both layers route (one
  // horizontal, one vertical; the pins land on them directly), so the grid models what a router has there.
  std::vector<std::string> stack;
  bool                     above = false;
  if (d.profile) {
    g.escape_layer = d.profile->route.access.escape;
    stack          = d.profile->route.signal_layers;
  } else if (tech.routing_layers.size() == 2) {
    g.escape_layer.clear();
    stack = tech.routing_layers;
    above = true;
  } else {
    if (tech.routing_layers.size() < 2) {
      throw std::invalid_argument("legroute requires at least two routing layers (one horizontal, one vertical)");
    }
    g.escape_layer = tech.routing_layers[1];
    stack.assign(tech.routing_layers.begin() + 1, tech.routing_layers.begin() + std::min<size_t>(7, tech.routing_layers.size()));
  }
  for (const auto& name : stack) {
    if (name == g.escape_layer) {
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
    g.layers.push_back(std::move(l));
  }
  if (g.layers.size() < 2 || std::none_of(g.layers.begin(), g.layers.end(), [](const auto& l) { return l.horizontal; })
      || std::all_of(g.layers.begin(), g.layers.end(), [](const auto& l) { return l.horizontal; })) {
    throw std::invalid_argument(g.escape_layer.empty() ? std::string("legroute needs a horizontal and a vertical routing layer")
                                                        : "legroute needs horizontal and vertical routing layers above " + g.escape_layer);
  }
  // the first reserved_tracks tracks of every band are kept out of the capacity
  const auto reserved = [&](int64_t ordinal) { return ordinal < int64_t(d.opt.reserved_tracks); };
  g.cap.assign(g.layers.size(), std::vector<int16_t>(g.size(), 0));
  for (size_t l = 0; l < g.layers.size(); ++l) {
    const auto&          layer = g.layers[l];
    std::vector<int16_t> band_capacity(layer.horizontal ? g.rows : g.cols);
    for (int band = 0; band < int(band_capacity.size()); ++band) {
      const auto [a, b] = g.tracks(int(l), band);
      int count         = 0;
      for (auto k = a; k <= b; ++k) {
        count += !reserved(k - a);
      }
      band_capacity[band] = int16_t(count);
    }
    for (size_t t = 0; t < g.size(); ++t) {
      g.cap[l][t] = band_capacity[size_t(layer.horizontal ? g.row(int(t)) : g.col(int(t)))];
    }
  }
  std::map<std::string, size_t> layer_index;
  for (size_t l = 0; l < g.layers.size(); ++l) {
    layer_index[g.layers[l].name] = l;
  }
  // power stripes take the tracks they cover in every tile they cross
  g.obstacles.assign(g.layers.size(), {});
  // PG paths and via landings can overlap. Their physical track is unavailable
  // once, including when the reservation already took that track.
  std::vector<std::unordered_map<int, std::set<int64_t>>> taken(g.layers.size());
  const auto take = [&](size_t l, const ph::Rect& r, bool all_directions = false) {
    const auto& L = g.layers[l];
    g.obstacles[l].push_back(r);
    if (!all_directions && L.horizontal != (r.xh - r.xl >= r.yh - r.yl)) {
      return;  // a via landing or a perpendicular strap: it blocks its span, not whole tracks
    }
    const double lo = (L.horizontal ? r.yl : r.xl) - L.pitch / 2.0, hi = (L.horizontal ? r.yh : r.xh) + L.pitch / 2.0;
    const int    c0 = g.col_of(r.xl), c1 = g.col_of(r.xh - 1), r0 = g.row_of(r.yl), r1 = g.row_of(r.yh - 1);
    for (int rr = r0; rr <= r1; ++rr) {
      for (int cc = c0; cc <= c1; ++cc) {
        const auto [a, b] = g.tracks(int(l), L.horizontal ? rr : cc);
        int covered       = 0;
        for (auto k = a; k <= b; ++k) {
          const auto x = g.track_coord(int(l), k);
          if (x > lo && x < hi && !reserved(k - a) && taken[l][g.tile(cc, rr)].insert(k).second) {
            ++covered;
          }
        }
        auto& cap = g.cap[l][g.tile(cc, rr)];
        cap       = int16_t(std::max(0, cap - covered));
      }
    }
  };
  for (const auto& e : exclusions) {
    for (size_t layer = 0; layer < g.layers.size(); ++layer) {
      take(layer, e.rect, true);
    }
  }
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
  for (const auto& s : power.obstacles) {
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
    const bool     rotated = ph::swaps_axes(c.place.orientation);
    const ph::Rect body{double(c.place.x),
                        double(c.place.y),
                        double(c.place.x + std::llround((rotated ? d.macro[i]->height : d.macro[i]->width) * fp.dbu)),
                        double(c.place.y + std::llround((rotated ? d.macro[i]->width : d.macro[i]->height) * fp.dbu))};
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
  return g;
}

std::pair<std::vector<int>, std::vector<int>> transit_capacity(const Grid& g) {
  std::vector<int> h(g.size()), v(g.size());
  for (size_t l = 0; l < g.layers.size(); ++l) {
    for (size_t t = 0; t < g.size(); ++t) {
      (g.layers[l].horizontal ? h : v)[t] += g.cap[l][t];
    }
  }
  return {std::move(h), std::move(v)};
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
      flop = std::max<int64_t>(flop, std::llround(m->second.width * fp.dbu));
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
