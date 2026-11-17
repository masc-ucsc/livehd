// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "tile_spread.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace livehd::placement {
namespace ph = livehd::physical;
namespace {
int64_t floor_div(int64_t a, int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
int64_t ceil_div(int64_t a, int64_t b) { return -floor_div(-a, b); }
}  // namespace

int64_t routing_tile_width(const ph::Technology& tech, const ph::Floorplan& fp) {
  if (fp.rows.empty() || tech.routing_layers.empty()) {
    return 0;
  }
  const auto site_it = tech.sites.find(fp.rows.front().site);
  if (site_it == tech.sites.end() || site_it->second.width <= 0) {
    return 0;
  }
  const auto  index = std::min<size_t>(3, tech.routing_layers.size() - 1);
  const auto  layer = tech.layers.find(tech.routing_layers[index]);
  if (layer == tech.layers.end()) {
    return 0;
  }
  const auto& l     = layer->second;
  const auto  pitch = l.direction == "HORIZONTAL" ? l.pitch_y : l.direction == "VERTICAL" ? l.pitch_x : std::max(l.pitch_x, l.pitch_y);
  if (pitch <= 0) {
    return 0;
  }
  const auto site = std::llround(site_it->second.width * fp.dbu);
  const auto raw  = std::llround(15 * pitch * fp.dbu);
  return site > 0 ? std::max<int64_t>(site, (raw + site / 2) / site * site) : 0;
}

Tile_spread_report spread_cells_in_tiles(const ph::Technology& tech, ph::Floorplan& fp, int64_t tile_w,
                                         const std::vector<Spread_rect>& obstacles, const std::vector<char>& frozen) {
  Tile_spread_report report;
  if (tile_w <= 0) {
    tile_w = routing_tile_width(tech, fp);
  }
  report.tile_w = tile_w;
  if (tile_w <= 0 || fp.rows.empty()) {
    return report;
  }
  // Row segments per row y: [x, x + count * step) on that row's site grid.
  struct Segment {
    int64_t lo, hi, origin, step;
  };
  std::map<int64_t, std::vector<Segment>> rows;
  int64_t                                 row_h = 0;
  for (const auto& r : fp.rows) {
    if (r.count <= 0 || r.step <= 0) {
      continue;
    }
    rows[r.y].push_back({r.x, r.x + r.count * r.step, r.x, r.step});
    const auto site = tech.sites.find(r.site);
    if (site != tech.sites.end()) {
      row_h = std::max<int64_t>(row_h, std::llround(site->second.height * fp.dbu));
    }
  }
  if (rows.empty() || row_h <= 0) {
    return report;
  }
  int64_t tile_x0 = std::numeric_limits<int64_t>::max();
  for (const auto& [y, segments] : rows) {
    for (const auto& s : segments) {
      tile_x0 = std::min(tile_x0, s.lo);
    }
  }
  // Components: movable single-row CORE cells may move; everything else with area is an obstacle in the rows it
  // covers.
  const auto movable = [&](const ph::Component& c) {
    const auto index = size_t(&c - fp.components.data());
    if ((!frozen.empty() && frozen[index]) || c.place.status != ph::Status::PLACED || c.place.height > row_h || c.place.width <= 0) {
      return false;
    }
    const auto m = tech.macros.find(c.macro);
    return m != tech.macros.end() && m->second.klass.starts_with("CORE") && m->second.klass.find("WELLTAP") == std::string::npos
           && m->second.klass.find("SPACER") == std::string::npos && rows.contains(c.place.y);
  };
  std::map<int64_t, std::vector<std::pair<int64_t, int64_t>>> blocked;  // row y -> keep-out x intervals
  for (const auto& o : obstacles) {
    for (auto it = rows.lower_bound(o.yl - row_h + 1); it != rows.end() && it->first < o.yh; ++it) {
      blocked[it->first].push_back({o.xl, o.xh});
    }
  }
  std::map<int64_t, std::vector<size_t>> by_row;  // row y -> components overlapping the row band
  for (size_t i = 0; i < fp.components.size(); ++i) {
    const auto& p = fp.components[i].place;
    if (p.width <= 0 || p.height <= 0 || p.status == ph::Status::UNPLACED) {
      continue;
    }
    for (auto it = rows.lower_bound(p.y - row_h + 1); it != rows.end() && it->first < p.y + p.height; ++it) {
      by_row[it->first].push_back(i);
    }
  }
  // Pin x offsets (DBU from the component's lower-left), per master, orientation and pin.
  std::map<std::tuple<std::string, int, std::string>, std::optional<int64_t>> pin_dx_cache;
  const auto pin_dx = [&](const ph::Component& c, const std::string& pin) -> std::optional<int64_t> {
    auto [it, fresh] = pin_dx_cache.try_emplace({c.macro, int(c.place.orientation), pin});
    if (!fresh) {
      return it->second;
    }
    const auto m = tech.macros.find(c.macro);
    if (m == tech.macros.end()) {
      return std::nullopt;
    }
    const auto p = m->second.pins.find(pin);
    if (p == m->second.pins.end() || p->second.shapes.empty()) {
      return std::nullopt;
    }
    double xl = std::numeric_limits<double>::max(), xh = std::numeric_limits<double>::lowest();
    for (const auto& shape : p->second.shapes) {
      const auto r = ph::orient_rect(shape.rect.translated(m->second.origin_x, m->second.origin_y),
                                     m->second.width,
                                     m->second.height,
                                     c.place.orientation);
      xl           = std::min(xl, r.xl);
      xh           = std::max(xh, r.xh);
    }
    it->second = std::llround((xl + xh) / 2 * fp.dbu);
    return it->second;
  };
  // Signal-net connections of each component: (net index, pin name).
  std::unordered_map<std::string_view, size_t> index_of;
  for (size_t i = 0; i < fp.components.size(); ++i) {
    index_of.emplace(fp.components[i].name.view(), i);
  }
  std::vector<std::vector<std::pair<size_t, std::string_view>>> pins_of(fp.components.size());
  for (size_t n = 0; n < fp.nets.size(); ++n) {
    const auto& net = fp.nets[n];
    if (!(net.use.empty() || net.use == "SIGNAL") || net.connections.size() < 2 || net.connections.size() > 16) {
      continue;  // clocks, supplies and wide fanouts do not want straight stubs
    }
    for (const auto& conn : net.connections) {
      const auto it = index_of.find(conn.instance.view());
      if (it != index_of.end()) {
        pins_of[it->second].push_back({n, conn.pin.view()});
      }
    }
  }
  // net -> absolute pin x of components already final in the row below (filled as rows complete)
  std::unordered_map<size_t, std::vector<std::pair<int64_t, int64_t>>> placed_pins;  // net -> (row y, pin x)
  for (auto& [y, segments] : rows) {
    auto& members = by_row[y];
    std::sort(members.begin(), members.end(), [&](size_t a, size_t b) { return fp.components[a].place.x < fp.components[b].place.x; });
    std::sort(segments.begin(), segments.end(), [](const Segment& a, const Segment& b) { return a.lo < b.lo; });
    for (const auto& seg : segments) {
      for (int64_t t0 = tile_x0 + floor_div(seg.lo - tile_x0, tile_w) * tile_w; t0 < seg.hi; t0 += tile_w) {
        const int64_t lo = std::max(seg.lo, t0), hi = std::min(seg.hi, t0 + tile_w);
        if (hi <= lo) {
          continue;
        }
        // Free stretches of [lo, hi): split at every obstacle (non-movable, or crossing the stretch's edges).
        std::vector<size_t> group;
        int64_t             a = lo;
        const auto          flush = [&](int64_t b) {
          if (!group.empty() && b > a) {
            int64_t width = 0;
            for (const auto i : group) {
              width += fp.components[i].place.width;
            }
            const int64_t free = b - a - width;
            if (free > 0) {
              // even share: inner gaps weigh one, the two edge gaps half each
              const double unit = double(free) / double(group.size());
              int64_t      cursor = a, rest = width;
              double       even   = a + unit / 2;
              for (const auto i : group) {
                auto&         p      = fp.components[i].place;
                const int64_t window_lo = ceil_div(cursor - seg.origin, seg.step) * seg.step + seg.origin;
                const int64_t window_hi = floor_div(b - rest - seg.origin, seg.step) * seg.step + seg.origin;
                int64_t       target    = std::llround((even - seg.origin) / double(seg.step)) * seg.step + seg.origin;
                bool          aligned   = false;
                // a pin of this cell on a net with a pin already final in the row below, within the window
                int64_t best_cost = std::numeric_limits<int64_t>::max();
                for (const auto& [net, pin] : pins_of[i]) {
                  const auto below = placed_pins.find(net);
                  const auto dx    = below == placed_pins.end() ? std::nullopt : pin_dx(fp.components[i], std::string(pin));
                  if (!dx) {
                    continue;
                  }
                  for (const auto& [ry, px] : below->second) {
                    if (ry != y - row_h || px < lo || px >= hi) {
                      continue;
                    }
                    const int64_t x = px - *dx;
                    if (x < window_lo || x > window_hi || (x - seg.origin) % seg.step != 0) {
                      continue;
                    }
                    const int64_t cost = std::abs(x - target);
                    if (cost < best_cost && double(cost) <= unit) {
                      best_cost = cost;
                      target    = x;
                      aligned   = true;
                    }
                  }
                }
                const int64_t x = std::clamp(target, window_lo, std::max(window_lo, window_hi));
                if (x != p.x) {
                  report.distance += std::abs(x - p.x);
                  ++report.moved;
                  p.x = x;
                }
                report.aligned += aligned;
                cursor  = p.x + p.width;
                rest   -= p.width;
                even   += p.width + unit;
              }
            }
          }
          group.clear();
        };
        // components and keep-outs of this stretch, in x order; keep-outs carry index SIZE_MAX
        std::vector<std::tuple<int64_t, int64_t, size_t>> items;
        for (const auto i : members) {
          const auto& p = fp.components[i].place;
          if (p.x + p.width > lo && p.x < hi) {
            items.push_back({p.x, p.x + p.width, i});
          }
        }
        if (const auto b = blocked.find(y); b != blocked.end()) {
          for (const auto& [bl, bh] : b->second) {
            if (bh > lo && bl < hi) {
              items.push_back({bl, bh, std::numeric_limits<size_t>::max()});
            }
          }
        }
        std::sort(items.begin(), items.end());
        for (const auto& [xl, xh, i] : items) {
          const bool inside = i != std::numeric_limits<size_t>::max() && fp.components[i].place.y == y && xl >= lo
                              && xh <= hi && movable(fp.components[i]);
          if (inside) {
            group.push_back(i);
          } else {
            flush(std::max(a, std::min(hi, xl)));
            a = std::max(a, std::min(hi, xh));
          }
        }
        flush(hi);
      }
    }
    // this row is final: record its pins for the row above
    for (const auto i : members) {
      if (fp.components[i].place.y != y) {
        continue;
      }
      for (const auto& [net, pin] : pins_of[i]) {
        if (const auto dx = pin_dx(fp.components[i], std::string(pin))) {
          placed_pins[net].push_back({y, fp.components[i].place.x + *dx});
        }
      }
    }
  }
  return report;
}
}  // namespace livehd::placement
