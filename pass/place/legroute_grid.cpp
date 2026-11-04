// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// legroute L1: the netlist view, the tile grid, per-tile free row segments, per-layer track capacity and the
// region tile sets.
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <functional>
#include <map>
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

Track_reservations::Track_reservations(const Legroute_options& options, const ph::Technology& tech, int64_t dbu)
    : legacy_count_(options.reserved_tracks), per_um_(options.reserved_tracks_per_um), dbu_(dbu) {
  if (dbu <= 0 || !std::isfinite(options.reserved_tracks_per_um)
      || (options.reserved_tracks_per_um < 0 && options.reserved_tracks_per_um != -1)) {
    throw std::invalid_argument("legroute: invalid track reservation density or DBU");
  }
  const std::string_view names = options.reserved_layers;
  if (names.empty()) {
    return;
  }
  for (size_t start = 0;;) {
    const auto comma = names.find(',', start);
    auto       token = names.substr(start, comma == names.npos ? names.size() - start : comma - start);
    while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front()))) {
      token.remove_prefix(1);
    }
    while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back()))) {
      token.remove_suffix(1);
    }
    const auto layer = tech.layers.find(std::string(token));
    if (token.empty() || layer == tech.layers.end()
        || (layer->second.direction != "HORIZONTAL" && layer->second.direction != "VERTICAL")) {
      throw std::invalid_argument("legroute: reserved_layers contains an empty or unknown routing layer: " + std::string(token));
    }
    layers_.emplace(token, true);
    if (comma == names.npos) {
      break;
    }
    start = comma + 1;
  }
}

bool Track_reservations::selected(std::string_view layer, int64_t coordinate, int64_t previous, int64_t ordinal) const {
  if (!enabled(layer)) {
    return false;
  }
  if (!density_mode()) {
    return ordinal < int64_t(legacy_count_);
  }
  if (previous >= coordinate) {
    throw std::logic_error("legroute: non-increasing reservation track coordinates");
  }
  return std::floor(static_cast<long double>(coordinate) * per_um_ / dbu_)
         > std::floor(static_cast<long double>(previous) * per_um_ / dbu_);
}

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
    // a two-row cell (a domino master) is a tile tall and moves like any other cell: its box takes the same sites
    // in both rows; taller cells must be anchored (fixed) first
    const auto height = std::llround(m->second.height * fp.dbu);
    if (height > row_h && height != 2 * row_h) {
      throw std::invalid_argument("legroute: multi-row cell " + c.name + " must be anchored (fixed) first");
    }
    d.cell[i] = int(d.movable.size());
    d.movable.push_back(int(i));
    d.width.push_back(std::llround(m->second.width * fp.dbu));
    d.flop.push_back(has_clock_pin(m->second));
    d.tall.push_back(height == 2 * row_h);
    d.widest = std::max(d.widest, d.width.back());
    if (d.flop.back() && !d.tall.back()) {  // the tile is two flops wide: single-row flops set it
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
      return exact_pack_segments(widths, room0);
    }
    room[best] -= widths[i];
    out[i]      = best;
  }
  return out;
}

// Greedy roomiest-first misses packings ({3,2,2} into {4,3} is {3}+{2,2}). A bounded exact search: widest
// first, never two segments of equal remaining room at one depth (symmetry), pruned by the total free room.
// Budget exhaustion returns empty like an impossible packing; a returned assignment is always valid.
std::vector<int> exact_pack_segments(const std::vector<int>& widths, const std::vector<int>& room0) {
  std::vector<int> order(widths.size()), out(widths.size(), -1), room = room0;
  std::iota(order.begin(), order.end(), 0);
  std::ranges::stable_sort(order, [&](int a, int b) { return widths[a] > widths[b]; });
  int64_t need = 0, have = 0;
  for (int w : widths) {
    need += w;
  }
  for (int r : room) {
    have += std::max(0, r);
  }
  if (need > have) {
    return {};
  }
  size_t                            nodes = 0;
  const std::function<bool(size_t)> place = [&](size_t k) {
    if (k == order.size()) {
      return true;
    }
    if (++nodes > 2000) {
      return false;
    }
    const int        i = order[k];
    std::vector<int> tried;
    for (size_t s = 0; s < room.size(); ++s) {
      if (room[s] < widths[i] || std::ranges::find(tried, room[s]) != tried.end()) {
        continue;
      }
      tried.push_back(room[s]);
      room[s] -= widths[i];
      out[i]   = int(s);
      if (place(k + 1)) {
        return true;
      }
      room[s] += widths[i];
      out[i]   = -1;
    }
    return false;
  };
  return place(0) ? out : std::vector<int>{};
}

Clock_domain plan_clock_domain(const Grid& g, int region, const std::vector<int>& tiles, int pitch) {
  Clock_domain     out;
  std::vector<int> cols, rows;
  for (int t : tiles) {
    cols.push_back(g.col(t));
    rows.push_back(g.row(t));
  }
  std::ranges::sort(cols);
  const int rmin = *std::ranges::min_element(rows), rmax = *std::ranges::max_element(rows);
  out.trunk_c = cols[cols.size() / 2];
  // ribs every `pitch` tile rows, centered so every flop row is within pitch / 2 of one
  for (int r = rmin + pitch / 2; r <= rmax + pitch / 2; r += pitch) {
    out.ribs.push_back(std::min(r, rmax));
  }
  std::ranges::sort(out.ribs);
  out.ribs.erase(std::unique(out.ribs.begin(), out.ribs.end()), out.ribs.end());
  // the trunk column must stay inside the region on every rib row
  for (int r : out.ribs) {
    if (g.region[g.tile(out.trunk_c, r)] != region) {
      for (int dc = 1; dc < g.cols; ++dc) {
        if (out.trunk_c + dc < g.cols && g.region[g.tile(out.trunk_c + dc, r)] == region) {
          out.trunk_c += dc;
          break;
        }
        if (out.trunk_c - dc >= 0 && g.region[g.tile(out.trunk_c - dc, r)] == region) {
          out.trunk_c -= dc;
          break;
        }
      }
    }
  }
  for (int r : out.ribs) {
    out.span[r] = {out.trunk_c, out.trunk_c};
  }
  for (int t : tiles) {
    int best = out.ribs.front();
    for (int r : out.ribs) {
      if (std::abs(r - g.row(t)) < std::abs(best - g.row(t))) {
        best = r;
      }
    }
    auto& s  = out.span[best];
    s.first  = std::min(s.first, g.col(t));
    s.second = std::max(s.second, g.col(t));
    out.tap_rib.push_back(best);
  }
  return out;
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
  // The physical density denominator must not subtract movable overhangs below:
  // their full LEF area is already included in the region's movable-cell load.
  g.placeable_sites.assign(g.size(), 0);
  for (size_t k = 0; k < free.size(); ++k) {
    for (const auto& [a, b] : free[k]) {
      for (int64_t c = a / g.sites; c * g.sites < b; ++c) {
        const auto lo = std::max(a, c * g.sites), hi = std::min(b, (c + 1) * g.sites);
        g.placeable_sites[size_t(g.tile(int(c), int(k / 2)))] += hi - lo;
      }
    }
  }
  if (d.opt.legal_boxes) {  // a cell's extent beyond its home tile (the tile its centre is on) takes those sites of
    // the neighbour tile, so a box moving there (legal_window) packs around it; its home tile stays as the rows say
    const auto cut = [&](int k, int64_t a, int64_t b) {
      if (k < 0 || k >= int(free.size()) || a >= b) {
        return;
      }
      std::vector<std::pair<int64_t, int64_t>> out;
      for (auto [lo, hi] : free[size_t(k)]) {
        if (hi <= a || lo >= b) {
          out.emplace_back(lo, hi);
          continue;
        }
        if (lo < a) {
          out.emplace_back(lo, a);
        }
        if (b < hi) {
          out.emplace_back(b, hi);
        }
      }
      free[size_t(k)] = std::move(out);
    };
    for (size_t i = 0; i < d.movable.size(); ++i) {
      const auto&   p  = fp.components[size_t(d.movable[i])].place;
      const int64_t s0 = (p.x - g.x0) / g.site, s1 = s0 + (d.width[i] + g.site - 1) / g.site;
      const int     c  = g.col_of(double(p.x) + d.width[i] / 2.0);
      const int64_t lo = int64_t(c) * g.sites, hi = lo + g.sites;
      const int     k = int((p.y - g.y0) / g.row_h);
      for (int kk = k; kk < k + (d.tall[i] ? 2 : 1); ++kk) {
        cut(kk, s0, std::min(s1, lo));
        cut(kk, std::max(s0, hi), s1);
      }
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
  if (movable_route_partition(d.opt)) {
    // Connectivity boxes precede the new route cuts. Legacy seed regions must
    // not change their grouping between planning and local placement.
    g.regions = 1;
    g.region.assign(g.size(), 0);
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
  g.resources.layers.resize(g.layers.size() + 1);
  g.resources.anonymous_clock_h.assign(g.size(), 0);
  g.resources.anonymous_clock_v.assign(g.size(), 0);
  const Track_reservations reservations(d.opt, tech, fp.dbu);
  for (size_t l = 0; l < g.layers.size(); ++l) {
    const auto&          layer = g.layers[l];
    std::vector<int16_t> band_capacity(layer.horizontal ? g.rows : g.cols);
    auto&                resource = g.resources.layers[l];
    resource.name                 = layer.name;
    resource.horizontal           = layer.horizontal;
    resource.grid_layer           = int(l);
    resource.bands.resize(band_capacity.size());
    resource.tiles.resize(g.size());
    std::vector<std::vector<Track_resource_exclusion>> band_exclusions(band_capacity.size());
    for (int band = 0; band < int(band_capacity.size()); ++band) {
      const auto [a, b] = g.tracks(int(l), band);
      int count         = 0;
      for (auto k = a; k <= b; ++k) {
        const auto coordinate  = g.track_coord(int(l), k);
        const bool reserved    = reservations.selected(layer.name, coordinate, coordinate - layer.pitch, k - a);
        count                 += !reserved;
        resource.bands[size_t(band)].push_back(coordinate);
        if (reserved) {
          band_exclusions[size_t(band)].push_back({size_t(k - a), uint8_t(Track_exclusion::generic_reservation)});
        }
      }
      band_capacity[band] = int16_t(count);
    }
    for (size_t t = 0; t < g.size(); ++t) {
      const auto band              = size_t(layer.horizontal ? g.row(int(t)) : g.col(int(t)));
      g.cap[l][t]                  = band_capacity[band];
      resource.tiles[t].band       = band;
      resource.tiles[t].exclusions = band_exclusions[band];
    }
  }
  std::map<std::string, size_t> layer_index;
  for (size_t l = 0; l < g.layers.size(); ++l) {
    layer_index[g.layers[l].name] = l;
  }
  // power stripes take the tracks they cover in every tile they cross
  g.obstacles.assign(g.layers.size(), {});
  // PG paths and via landings can overlap. Their physical track is unavailable
  // once, including when the generic reservation already selected that track.
  std::vector<std::unordered_map<int, std::set<int64_t>>> taken(g.layers.size());
  const auto                                              take = [&](size_t l, const ph::Rect& r, Track_exclusion reason) {
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
          const auto x = g.track_coord(int(l), k);
          if (x > lo && x < hi) {
            // Retain overlapping exclusion identities too; the capacity below
            // still deducts the physical track only once.
            g.resources.layers[l].exclude(size_t(g.tile(cc, rr)), size_t(k - a), reason);
          }
          if (x > lo && x < hi && !reservations.selected(L.name, x, x - L.pitch, k - a)
              && taken[l][g.tile(cc, rr)].insert(k).second) {
            ++covered;
          }
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
        take(l->second, s.rect, Track_exclusion::power_metal);
      }
    }
  }
  for (const auto& s : regions.obstacles) {
    if (const auto l = layer_index.find(s.layer); l != layer_index.end()) {
      take(l->second, s.rect, Track_exclusion::region_obstacle);
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
          g.resources.layers[l->second].exclude_all(size_t(g.tile(cc, rr)), Track_exclusion::macro_footprint);
        }
      }
    }
  }
  // box-layer tracks per tile: in-box routes and local nets use them, never the global routes. The union of the
  // layer's DEF TRACKS patterns (multi-pattern layers), less the tracks that the power rails' spacing takes.
  {
    auto& resource      = g.resources.layers.back();
    resource.name       = g.box_layer;
    resource.horizontal = true;
    resource.bands.resize(size_t(g.rows));
    resource.tiles.resize(g.size());
    std::vector<int64_t> ys_all;
    int64_t              first_track = std::numeric_limits<int64_t>::max(), first_previous = 0;
    for (const auto& t : fp.tracks) {
      if (t.layer == g.box_layer && t.axis == "Y" && t.supported()) {
        if (t.count > 0 && t.start <= first_track) {
          first_previous = t.start < first_track ? t.start - t.step : std::max(first_previous, t.start - t.step);
          first_track    = t.start;
        }
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
      first_previous    = g.y0 + off - pitch;
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
    std::vector<ph::Rect>        rails;
    std::vector<Track_exclusion> rail_reasons;
    const auto                   add_rail = [&](const ph::Shape& s, Track_exclusion reason) {
      if (s.layer == g.box_layer && s.rect.xh - s.rect.xl > s.rect.yh - s.rect.yl) {
        rails.push_back(s.rect);
        rail_reasons.push_back(reason);
      }
    };
    for (const auto& net : fp.special_nets) {
      for (const auto& s : ph::route_geometry(tech, fp, net, true)) {
        add_rail(s, Track_exclusion::power_metal);
      }
    }
    for (const auto& s : regions.obstacles) {
      add_rail(s, Track_exclusion::region_obstacle);
    }
    // the rails by tile row (a rail's spacing band reaches the rows it touches): a tile checks its own row's rails.
    // Every rail of the die per tile and track was hours on a large design
    std::vector<std::vector<int>> rails_by_row(g.rows);
    for (int k = 0; k < int(rails.size()); ++k) {
      const auto& q = rails[k];
      for (int rr = g.row_of(q.yl - double(keep)); rr <= g.row_of(q.yh + double(keep)); ++rr) {
        rails_by_row[rr].push_back(k);
      }
    }
    g.box_cap.assign(g.size(), 0);
    for (int tr = 0; tr < g.rows; ++tr) {
      const auto  lo   = std::lower_bound(ys_all.begin(), ys_all.end(), g.bottom(tr));
      const auto  hi   = std::lower_bound(ys_all.begin(), ys_all.end(), g.bottom(tr) + g.tile_h);
      const auto& near = rails_by_row[tr];
      resource.bands[size_t(tr)].assign(lo, hi);
      for (int tc = 0; tc < g.cols; ++tc) {
        const double txl = double(g.left(tc)), txh = txl + double(g.tile_w);
        const auto   t         = size_t(g.tile(tc, tr));
        resource.tiles[t].band = size_t(tr);
        int n                  = 0;
        for (auto it = lo; it != hi; ++it) {
          const double y = double(*it);
          const bool   reserved
              = reservations.density_mode()
                && reservations.selected(g.box_layer, *it, it == ys_all.begin() ? first_previous : *std::prev(it), it - lo);
          if (reserved) {
            resource.exclude(t, size_t(it - lo), Track_exclusion::generic_reservation);
          }
          bool blocked = false;
          for (int k : near) {
            const auto& q = rails[k];
            if (q.xl < txh && q.xh > txl && y > q.yl - double(keep) && y < q.yh + double(keep)) {
              blocked = true;
              resource.exclude(t, size_t(it - lo), rail_reasons[size_t(k)]);
            }
          }
          n += !reserved && !blocked;
        }
        const auto reserve = !reservations.density_mode() && reservations.enabled(g.box_layer) ? d.opt.reserved_tracks : 0;
        g.box_cap[t]       = g.blocked[t] ? 0 : int16_t(std::max<int64_t>(0, int64_t(n) - int64_t(reserve)));
        resource.tiles[t].anonymous_headroom = reserve;
        if (g.blocked[t]) {
          resource.exclude_all(t, Track_exclusion::blocked_tile);
        }
        resource.tiles[t].coarse_capacity = g.box_cap[t];
      }
    }
  }
  // The per-cellbox budget: every free tile keeps these usable tracks for Final (counted after PG and obstacles,
  // so a PG-occupied track never stands in for a promised one). Shared by every stage through this grid. A tile
  // that cannot keep its budget cannot host a box (box_fits); it gets its own segment-pattern key so key-based fit
  // caches stay exact.
  g.box_short.assign(g.size(), 0);
  if (!d.opt.box_reserve.empty()) {
    std::string_view spec = d.opt.box_reserve;
    while (!spec.empty()) {
      const auto comma = spec.find(',');
      const auto item  = spec.substr(0, comma);
      spec             = comma == std::string_view::npos ? std::string_view{} : spec.substr(comma + 1);
      const auto colon = item.find(':');
      if (colon == std::string_view::npos || colon == 0) {
        throw std::invalid_argument(std::format("legroute box_reserve entry '{}' must be LAYER:N", item));
      }
      const auto layer = item.substr(0, colon);
      int        n     = 0;
      if (std::from_chars(item.data() + colon + 1, item.data() + item.size(), n).ec != std::errc{} || n < 0) {
        throw std::invalid_argument(std::format("legroute box_reserve entry '{}' needs a nonnegative track count", item));
      }
      const bool box   = layer == g.box_layer;
      const auto found = std::ranges::find(g.layers, layer, &Layer::name);
      if (!box && found == g.layers.end()) {
        std::fprintf(stderr,
                     "legroute grid: box_reserve layer %.*s is not a routing layer here; ignored\n",
                     int(layer.size()),
                     layer.data());
        continue;
      }
      if (box && (g.box_cap.size() != g.size() || g.resources.layers.back().tiles.size() != g.size())) {
        continue;  // no box-layer capacity model here
      }
      const size_t l           = box ? g.layers.size() : size_t(found - g.layers.begin());
      auto&        catalog     = g.resources.layers[l];
      size_t       short_tiles = 0;
      for (size_t t = 0; t < g.size(); ++t) {
        if (g.blocked[t]) {
          continue;
        }
        auto& cap = box ? g.box_cap[t] : g.cap[l][t];
        if (cap < n) {
          ++short_tiles;
          g.box_short[t] = 1;
        }
        cap                               = int16_t(std::max(0, cap - n));
        catalog.tiles[t].cellbox_budget  += unsigned(n);
        catalog.tiles[t].coarse_capacity  = cap;
      }
      if (short_tiles) {
        std::fprintf(stderr,
                     "legroute grid: box_reserve %.*s:%d exceeds the usable tracks of %zu free tiles; they host no box\n",
                     int(layer.size()),
                     layer.data(),
                     n,
                     short_tiles);
      }
    }
    std::map<uint32_t, uint32_t> short_key;
    for (size_t t = 0; t < g.size(); ++t) {
      if (g.box_short[t]) {
        const auto [it, fresh] = short_key.try_emplace(g.key[t], uint32_t(g.keys.size()));
        if (fresh) {
          g.keys.push_back(g.keys[g.key[t]]);
        }
        g.key[t] = it->second;
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
  for (size_t t = 0; t < g.size(); ++t) {
    for (size_t l = 0; l < g.layers.size(); ++l) {
      g.resources.layers[l].tiles[t].coarse_capacity = g.cap[l][t];
    }
    if (d.opt.clock_spine) {
      g.resources.anonymous_clock_v[t] = 1;
      g.resources.anonymous_clock_h[t] = g.row(int(t)) % std::max(1u, d.opt.rib_tiles) == 0;
    }
  }
  g.resources.finalize();
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
