// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "signal_route_domain.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>

#include "inou/lefdef/routing_geometry.hpp"
#include "inou/lefdef/routing_occupancy.hpp"
#include "inou/lefdef/signal_via.hpp"

namespace livehd::placement {
namespace ph = physical;
namespace {
bool   valid(const ph::Rect& r) { return ph::integral_dbu(r) && !r.empty(); }
bool   horizontal(const ph::Technology& t, const std::string& l) { return t.layers.at(l).direction == "HORIZONTAL"; }
double coordinate(const ph::Shape& s, bool h) { return h ? (s.rect.yl + s.rect.yh) / 2 : (s.rect.xl + s.rect.xh) / 2; }
bool   contact(const ph::Rect& a, const ph::Rect& b) {
  const auto dx = std::min(a.xh, b.xh) - std::max(a.xl, b.xl);
  const auto dy = std::min(a.yh, b.yh) - std::max(a.yl, b.yl);
  return dx >= 0 && dy >= 0 && (dx > 0 || dy > 0);
}
bool covered(const ph::Rect& r, const std::vector<ph::Rect>& metal) {
  std::vector<double> xs{r.xl, r.xh};
  for (const auto& s : metal) {
    if (s.xl > r.xl && s.xl < r.xh) {
      xs.push_back(s.xl);
    }
    if (s.xh > r.xl && s.xh < r.xh) {
      xs.push_back(s.xh);
    }
  }
  std::sort(xs.begin(), xs.end());
  xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
  for (size_t i = 1; i < xs.size(); ++i) {
    const double                           x = (xs[i - 1] + xs[i]) / 2;
    std::vector<std::pair<double, double>> intervals;
    for (const auto& s : metal) {
      if (s.xl <= x && s.xh >= x) {
        intervals.emplace_back(s.yl, s.yh);
      }
    }
    std::sort(intervals.begin(), intervals.end());
    double end = r.yl;
    for (const auto& [lo, hi] : intervals) {
      if (lo > end) {
        break;
      }
      end = std::max(end, hi);
    }
    if (end < r.yh) {
      return false;
    }
  }
  return true;
}
size_t rank(const ph::Technology& t, const std::string& l) {
  return std::find(t.routing_layers.begin(), t.routing_layers.end(), l) - t.routing_layers.begin();
}
bool collinear(const ph::Technology& t, const ph::Shape& a, const ph::Shape& b) {
  if (a.layer != b.layer || !t.layers.contains(a.layer)) {
    return false;
  }
  const bool h = horizontal(t, a.layer);
  return coordinate(a, h) == coordinate(b, h);
}
bool carrier_shape(const Signal_route_domain& d, const ph::Technology& t, const Signal_route_carrier& c, const ph::Shape& s) {
  return c.owner == d.owner && collinear(t, c.seed, s) && c.extension.contains(s.rect);
}
bool reserved_shape(const Signal_route_domain& d, const ph::Technology& t, const ph::Shape& s) {
  if (std::ranges::find(d.local_layers, s.layer) == d.local_layers.end() || !t.layers.contains(s.layer)) {
    return false;
  }
  const auto c = coordinate(s, horizontal(t, s.layer));
  for (const auto& r : d.regions) {
    const auto it = r.tracks.find(s.layer);
    if (r.bounds.contains(s.rect) && it != r.tracks.end() && std::ranges::find(it->second, c) != it->second.end()) {
      return true;
    }
  }
  return false;
}
bool stack_layer(const Signal_route_domain& d, const ph::Technology& t, const std::string& l) {
  if (d.max_stack_bridge_dbu <= 0 || !t.layers.contains(l) || d.local_layers.empty()) {
    return false;
  }
  size_t local = 0, incoming = 0;
  for (const auto& x : d.local_layers) {
    local = std::max(local, rank(t, x));
  }
  for (const auto& c : d.carriers) {
    if (c.owner == d.owner) {
      incoming = std::max(incoming, rank(t, c.seed.layer));
    }
  }
  return rank(t, l) > local && rank(t, l) < incoming;
}
bool inside_stack_window(const Signal_route_domain& d, const ph::Rect& s) {
  for (const auto& r : d.regions) {
    if (r.bounds.contains(s)) {
      return true;
    }
  }
  const double margin = d.max_stack_bridge_dbu;
  for (const auto& c : d.carriers) {
    const auto& r = c.extension;
    if (ph::Rect{r.xl - margin, r.yl - margin, r.xh + margin, r.yh + margin}.contains(s)) {
      return true;
    }
  }
  return false;
}
struct Via_metal {
  std::vector<ph::Shape> plates;
  size_t                 lower = 0, upper = 0;
  int64_t                x = 0, y = 0;
};
struct Decoded {
  std::vector<ph::Shape> wires;
  std::vector<Via_metal> vias;
};
Decoded decode(const ph::Technology& t, const ph::Floorplan& f, const ph::Net& net, std::span<const ph::Route_path> paths,
               const ph::Route_width_rules* rules) {
  Decoded out;
  for (const auto& p : paths) {
    std::string                    layer = p.layer;
    std::optional<ph::Route_point> at;
    const auto                     wire = [&](auto steps) {
      // Copy only the path header. Copying p before replacing steps makes
      // decoding a multi-step path quadratic in its number of steps.
      ph::Route_path one{layer, p.width, p.shape, std::move(steps), p.taper_rule};
      ph::Net        n;
      n.name = net.name;
      n.wires.push_back({"ROUTED", {}});
      n.wires.back().paths.push_back(std::move(one));
      const auto metal = ph::route_geometry(t, f, n, false, rules);
      out.wires.insert(out.wires.end(), metal.begin(), metal.end());
    };
    for (const auto& step : p.steps) {
      if (const auto* point = std::get_if<ph::Route_point>(&step)) {
        if (at) {
          wire(std::vector<std::variant<ph::Route_point, ph::Route_via, ph::Route_rect>>{*at, *point});
        }
        at = *point;
      } else if (const auto* v = std::get_if<ph::Route_via>(&step)) {
        if (!at) {
          throw std::invalid_argument("via before point");
        }
        Via_metal via;
        via.x     = at->x;
        via.y     = at->y;
        via.lower = t.routing_layers.size();
        for (auto s : ph::via_geometry(t, f, *v)) {
          if (!t.layers.contains(s.layer)) {
            continue;
          }
          s.rect    = s.rect.translated(at->x, at->y);
          via.lower = std::min(via.lower, rank(t, s.layer));
          via.upper = std::max(via.upper, rank(t, s.layer));
          via.plates.push_back(std::move(s));
        }
        if (via.plates.size() != 2 || via.upper != via.lower + 1) {
          throw std::invalid_argument("non-adjacent native stack via");
        }
        if (via.plates[0].layer == layer) {
          layer = via.plates[1].layer;
        } else if (via.plates[1].layer == layer) {
          layer = via.plates[0].layer;
        } else {
          throw std::invalid_argument("via does not touch current layer");
        }
        out.vias.push_back(std::move(via));
      } else {
        if (!at) {
          throw std::invalid_argument("patch before point");
        }
        wire(std::vector<std::variant<ph::Route_point, ph::Route_via, ph::Route_rect>>{*at, std::get<ph::Route_rect>(step)});
      }
    }
  }
  return out;
}
std::string check_domain(const Signal_route_domain& d, const ph::Technology& t, const ph::Floorplan& f, const ph::Net& n) {
  if (d.owner.empty() || d.owner != n.name) {
    return "route domain owner mismatch";
  }
  if (d.max_stack_bridge_dbu < 0) {
    return "negative route domain stack bound";
  }
  if (!t.layers.contains(d.pin_layer)) {
    return "unknown route domain pin layer";
  }
  for (const auto& l : d.local_layers) {
    if (!t.layers.contains(l)) {
      return "unknown reserved layer";
    }
  }
  for (const auto& r : d.regions) {
    if (!valid(r.bounds) || !f.die.contains(r.bounds)) {
      return "invalid reserved region bounds";
    }
    for (const auto& [l, coords] : r.tracks) {
      if (std::ranges::find(d.local_layers, l) == d.local_layers.end()) {
        return "reservation outside local layers";
      }
      for (auto c : coords) {
        bool native = false;
        for (const auto& tr : f.tracks) {
          if (tr.layer == l && tr.supported() && tr.axis == (horizontal(t, l) ? "Y" : "X")) {
            const auto [lo, hi]  = tr.index_range(double(c), double(c));
            native              |= lo <= hi;
          }
        }
        if (!native) {
          return "reservation is not a native DEF track";
        }
      }
    }
  }
  for (const auto& c : d.carriers) {
    if (c.owner != d.owner || c.component.empty()) {
      return "foreign or unidentified incoming carrier";
    }
    if (!t.layers.contains(c.seed.layer) || !valid(c.seed.rect) || !valid(c.extension) || !f.die.contains(c.extension)) {
      return "invalid incoming carrier geometry";
    }
    const bool h = horizontal(t, c.seed.layer);
    if ((h ? c.seed.rect.yl != c.extension.yl || c.seed.rect.yh != c.extension.yh
           : c.seed.rect.xl != c.extension.xl || c.seed.rect.xh != c.extension.xh)
        || !contact(c.seed.rect, c.extension)) {
      return "noncollinear or detached incoming interval";
    }
  }
  return {};
}
}  // namespace

std::optional<ph::Rect> signal_domain_tap_window(const Signal_route_domain& d) {
  if (d.regions.empty()) {
    return std::nullopt;
  }
  auto bounds = d.regions.front().bounds;
  for (const auto& region : d.regions) {
    bounds = bounds.hull(region.bounds);
  }
  const double margin = d.max_stack_bridge_dbu;
  for (const auto& carrier : d.carriers) {
    if (carrier.owner == d.owner) {
      const auto& r = carrier.extension;
      bounds        = bounds.hull({r.xl - margin, r.yl - margin, r.xh + margin, r.yh + margin});
    }
  }
  return bounds;
}

bool signal_domain_track(const Signal_route_domain& d, const ph::Technology& t, const std::string& layer, int64_t c) {
  for (const auto& r : d.regions) {
    const auto it = r.tracks.find(layer);
    if (it != r.tracks.end() && std::ranges::find(it->second, c) != it->second.end()) {
      return true;
    }
  }
  for (const auto& s : d.carriers) {
    if (s.owner == d.owner && s.seed.layer == layer && coordinate(s.seed, horizontal(t, layer)) == c) {
      return true;
    }
  }
  return stack_layer(d, t, layer);  // finite bridges are screened separately, then checked against actual via plates
}
bool signal_domain_wire(const Signal_route_domain& d, const ph::Technology& t, const ph::Floorplan& f, const ph::Shape& s,
                        bool stack_candidate) {
  if (!valid(s.rect) || !t.layers.contains(s.layer) || !f.die.contains(s.rect)) {
    return false;
  }
  if (reserved_shape(d, t, s)) {
    return true;
  }
  for (const auto& c : d.carriers) {
    if (carrier_shape(d, t, c, s)) {
      return true;
    }
  }
  if (!stack_candidate || !stack_layer(d, t, s.layer) || !inside_stack_window(d, s.rect)) {
    return false;
  }
  const bool h     = horizontal(t, s.layer);
  const auto width = ph::wire_width_dbu(t.layers.at(s.layer), f.dbu);
  const auto grid  = ph::explicit_manufacturing_grid_dbu(t, f.dbu);
  if (!width || !grid || (h ? s.rect.yh - s.rect.yl : s.rect.xh - s.rect.xl) != *width) {
    return false;
  }
  const double half = ph::min_area_half_length(*width, t.layers.at(s.layer).rules.area * f.dbu * f.dbu, *grid);
  return (h ? s.rect.xh - s.rect.xl : s.rect.yh - s.rect.yl) <= d.max_stack_bridge_dbu + 2 * half;
}
std::string validate_signal_route_domain(const ph::Technology& t, const ph::Floorplan& f, const ph::Net& n,
                                         std::span<const ph::Route_path> proposed, const Signal_route_domain& d,
                                         const ph::Route_width_rules* rules, const ph::Route_components* components,
                                         std::span<const ph::Route_path> retained, std::span<const ph::Route_path> extra_retained) {
  try {
    if (auto error = check_domain(d, t, f, n); !error.empty()) {
      return error;
    }
    const auto added = decode(t, f, n, proposed, rules), old = decode(t, f, n, retained, rules),
               extra     = decode(t, f, n, extra_retained, rules);
    auto       all_wires = added.wires;
    all_wires.insert(all_wires.end(), old.wires.begin(), old.wires.end());
    all_wires.insert(all_wires.end(), extra.wires.begin(), extra.wires.end());
    auto all_vias = added.vias;
    all_vias.insert(all_vias.end(), old.vias.begin(), old.vias.end());
    all_vias.insert(all_vias.end(), extra.vias.begin(), extra.vias.end());
    const auto carrier_cover = [&](const Signal_route_carrier& c) {
      std::vector<ph::Rect> metal{c.seed.rect};
      for (const auto& other : d.carriers) {
        if (other.owner == d.owner && collinear(t, c.seed, other.seed)) {
          metal.push_back(other.seed.rect);
        }
      }
      for (const auto& s : all_wires) {
        if (collinear(t, c.seed, s)) {
          metal.push_back(s.rect);
        }
      }
      return metal;
    };
    std::vector<ph::Shape> stack_wires;
    for (const auto& s : added.wires) {
      if (s.layer == d.pin_layer) {
        return "new pin-layer wire is outside reserved domain";
      }
      if (reserved_shape(d, t, s)) {
        continue;
      }
      bool incoming = false;
      for (const auto& c : d.carriers) {
        if (carrier_shape(d, t, c, s)) {
          if (!covered(c.seed.rect.hull(s.rect), carrier_cover(c))) {
            return "incoming extension detached from frozen seed";
          }
          incoming = true;
          break;
        }
      }
      if (incoming) {
        continue;
      }
      if (!signal_domain_wire(d, t, f, s, true) || !stack_layer(d, t, s.layer)) {
        return "wire outside reserved/carrier domain";
      }
      const bool              h     = horizontal(t, s.layer);
      bool                    below = false, above = false;
      double                  lo = std::numeric_limits<double>::infinity(), hi = -lo;
      std::optional<ph::Rect> envelope;
      const auto              width = ph::wire_width_dbu(t.layers.at(s.layer), f.dbu);
      const auto              grid  = ph::explicit_manufacturing_grid_dbu(t, f.dbu);
      if (!width || !grid) {
        return "unsupported intermediate landing geometry";
      }
      const double half = ph::min_area_half_length(*width, t.layers.at(s.layer).rules.area * f.dbu * f.dbu, *grid);
      for (const auto& via : all_vias) {
        for (const auto& p : via.plates) {
          if (!collinear(t, s, p) || !contact(s.rect, p.rect)) {
            continue;
          }
          const double at  = h ? via.x : via.y;
          below           |= via.upper == rank(t, s.layer);
          above           |= via.lower == rank(t, s.layer);
          lo               = std::min(lo, at);
          hi               = std::max(hi, at);
          auto r           = p.rect;
          if (h) {
            r.xl = std::min(r.xl, at - half);
            r.xh = std::max(r.xh, at + half);
          } else {
            r.yl = std::min(r.yl, at - half);
            r.yh = std::max(r.yh, at + half);
          }
          envelope = envelope ? envelope->hull(r) : r;
        }
      }
      if (!below || !above || !envelope || hi - lo > d.max_stack_bridge_dbu || !envelope->contains(s.rect)) {
        return "upper wire is not a bounded monotone native stack landing";
      }
      stack_wires.push_back(s);
    }
    std::optional<ph::Route_components> own_components;
    if (!components) {
      own_components.emplace(f);
      components = &*own_components;
    }
    std::vector<std::vector<ph::Rect>> pin_unions;
    for (const auto& c : n.connections) {
      if (c.instance == "PIN") {
        continue;
      }
      const auto it = components->find(c.instance);
      if (it == components->end()) {
        return "unknown domain endpoint component";
      }
      const auto& cell  = *it->second;
      const auto& macro = t.macros.at(cell.macro);
      const auto  pin   = macro.pins.find(c.pin);
      if (pin == macro.pins.end()) {
        return "unknown domain endpoint pin";
      }
      auto& rects = pin_unions.emplace_back();
      for (const auto& s : pin->second.shapes) {
        if (s.layer == d.pin_layer) {
          rects.push_back(ph::placed_rect(macro, cell.place, f.dbu, s.rect));
        }
      }
    }
    for (const auto& via : added.vias) {
      for (const auto& p : via.plates) {
        if (p.layer == d.pin_layer) {
          if (!std::ranges::any_of(pin_unions, [&](const auto& pin) { return covered(p.rect, pin); })) {
            return "native access plate outside actual endpoint pin union";
          }
          continue;
        }
        if (reserved_shape(d, t, p)) {
          continue;
        }
        bool incoming = false;
        for (const auto& c : d.carriers) {
          if (carrier_shape(d, t, c, p) && covered(p.rect, carrier_cover(c))) {
            incoming = true;
            break;
          }
        }
        if (incoming) {
          continue;
        }
        std::vector<ph::Rect> landing;
        for (const auto& s : stack_wires) {
          if (s.layer == p.layer) {
            landing.push_back(s.rect);
          }
        }
        if (!covered(p.rect, landing)) {
          return "native via plate lacks reserved, seeded carrier, or stack ownership";
        }
      }
    }
    return {};
  } catch (const std::exception& e) {
    return std::string("invalid route domain geometry: ") + e.what();
  }
}
}  // namespace livehd::placement
