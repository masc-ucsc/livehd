// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "routed_rc.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <tuple>

#include "inou/lefdef/routing_connectivity.hpp"
#include "inou/lefdef/routing_geometry.hpp"

namespace livehd::placement {
namespace ph = physical;
namespace {
using Point = std::tuple<std::string, int64_t, int64_t>;
struct Segment {
  std::string layer;
  bool        horizontal;
  int64_t     cross, lo, hi;
  double      width;
};
bool on(const Point& p, const Segment& s) {
  const auto& [layer, x, y] = p;
  return layer == s.layer && (s.horizontal ? y : x) == s.cross && (s.horizontal ? x : y) >= s.lo && (s.horizontal ? x : y) <= s.hi;
}
Point at(const Segment& s, int64_t along) { return {s.layer, s.horizontal ? along : s.cross, s.horizontal ? s.cross : along}; }
}  // namespace
ph::Rc_net routed_rc(const ph::Technology& tech, const ph::Floorplan& fp, const ph::Net& net, const std::string& driver,
                     const std::vector<Route_rc_pin>& pins, const Route_technology_rc& rc, const ph::Route_components* components) {
  std::optional<ph::Route_components> local;
  if (!components) {
    local.emplace(fp);
    components = &*local;
  }
  if (!components->matches(fp)) {
    throw std::invalid_argument("component index does not match RC floorplan");
  }
  const auto connected = ph::check_route_connectivity(tech, fp, net, components);
  if (!connected.connected) {
    throw std::invalid_argument("routed RC connectivity: " + connected.reason);
  }
  if (pins.size() != net.connections.size()) {
    throw std::invalid_argument("routed RC missing pin bindings");
  }
  const auto&                                  cells = *components;
  std::map<std::string, const ph::Connection*> endpoints;
  for (const auto& p : net.connections) {
    if (!endpoints.emplace(p.instance + ":" + p.pin, &p).second) {
      throw std::invalid_argument("duplicate routed endpoint");
    }
  }
  std::set<std::string> labels;
  for (const auto& p : pins) {
    const auto e = endpoints.find(p.pin);
    if (e == endpoints.end() || !labels.insert(p.pin).second) {
      throw std::invalid_argument("invalid routed RC pin label");
    }
    const auto& c        = *cells.find(e->second->instance)->second;
    const auto& m        = tech.macros.at(c.macro);
    bool        contains = false;
    for (const auto& s : m.pins.at(e->second->pin).shapes) {
      if (s.layer != p.layer) {
        continue;
      }
      const auto point = [&](double x, double y) {
        const auto [u, v] = ph::orient_point(x + m.origin_x, y + m.origin_y, m.width, m.height, c.place.orientation);
        return std::pair{c.place.x + u * fp.dbu, c.place.y + v * fp.dbu};
      };
      const auto [x0, y0]  = point(s.rect.xl, s.rect.yl);
      const auto [x1, y1]  = point(s.rect.xh, s.rect.yh);
      contains            |= p.x > std::min(x0, x1) && p.x < std::max(x0, x1) && p.y > std::min(y0, y1) && p.y < std::max(y0, y1);
    }
    if (!contains) {
      throw std::invalid_argument("routed RC access is outside its physical pin");
    }
  }
  if (!labels.contains(driver)) {
    throw std::invalid_argument("routed RC driver is not an endpoint");
  }
  std::vector<Segment> segments;
  bool                 variable_width = false;
  struct Via {
    Point                  a, b;
    double                 resistance;
    std::vector<ph::Shape> pads;
  };
  std::vector<Via>                                                     vias;
  std::set<std::tuple<std::string, int64_t, int64_t, ph::Orientation>> seen_vias;
  for (const auto& wire : net.wires) {
    for (const auto& path : wire.paths) {
      variable_width |= !path.taper_rule.empty();
      std::optional<ph::Route_point> previous;
      auto                           layer = path.layer;
      for (const auto& step : path.steps) {
        const auto* p = std::get_if<ph::Route_point>(&step);
        if (!previous) {
          previous = *p;
          continue;
        }
        if (p) {
          ph::Net piece;
          piece.wires.push_back({wire.type, {{layer, path.width, path.shape, {*previous, *p}, path.taper_rule}}});
          const auto  shapes = ph::route_geometry(tech, fp, piece);
          const auto& r      = shapes.at(0).rect;
          const bool  h      = previous->y == p->y;
          segments.push_back({layer,
                              h,
                              h ? p->y : p->x,
                              std::llround(h ? r.xl : r.yl),
                              std::llround(h ? r.xh : r.yh),
                              h ? r.yh - r.yl : r.xh - r.xl});
          previous = *p;
        } else if (std::holds_alternative<ph::Route_rect>(step)) {
          variable_width = true;
          ph::Net piece;
          piece.wires.push_back({wire.type, {{layer, {}, {}, {*previous, step}}}});
          const auto  shapes    = ph::route_geometry(tech, fp, piece);
          const auto& r         = shapes.at(0).rect;
          const auto& direction = tech.layers.at(layer).direction;
          if (direction != "HORIZONTAL" && direction != "VERTICAL") {
            throw std::invalid_argument("routed RC patch requires a preferred layer direction");
          }
          const bool   h      = direction == "HORIZONTAL";
          const double center = h ? (r.yl + r.yh) / 2 : (r.xl + r.xh) / 2;
          if (center != (h ? previous->y : previous->x)) {
            throw std::invalid_argument("non-centerline patch in routed RC");
          }
          const double width   = h ? r.yh - r.yl : r.xh - r.xl;
          const double nominal = tech.layers.at(layer).width * fp.dbu;
          if (!std::isfinite(nominal) || nominal <= 0 || width < nominal) {
            throw std::invalid_argument("routed RC patch is narrower than nominal metal");
          }
          segments.push_back({layer, h, std::llround(center), std::llround(h ? r.xl : r.yl), std::llround(h ? r.xh : r.yh), width});
        } else {
          const auto& v = std::get<ph::Route_via>(step);
          if (std::any_of(fp.vias.begin(), fp.vias.end(), [&](const auto& d) { return d.name == v.name; })) {
            throw std::invalid_argument("routed RC supports characterized LEF vias only");
          }
          const auto            shapes = ph::via_geometry(tech, fp, v);
          std::set<std::string> metals;
          std::string           cut;
          for (const auto& s : shapes) {
            if (tech.layers.contains(s.layer)) {
              metals.insert(s.layer);
            } else {
              if (!cut.empty()) {
                throw std::invalid_argument("multi-cut routed RC unsupported");
              }
              cut = s.layer;
            }
          }
          if (shapes.size() != 3 || cut.empty() || metals.size() != 2 || !metals.erase(layer)) {
            throw std::invalid_argument("unsupported routed RC via");
          }
          const auto other      = *metals.begin();
          double     resistance = -1;
          if (rc.cut_resistance_ohm.contains(cut)) {
            resistance = rc.cut_resistance_ohm.at(cut);
          } else if (rc.via_resistance_ohm.contains(v.name)) {
            resistance = rc.via_resistance_ohm.at(v.name);
          }
          if (!std::isfinite(resistance) || resistance < 0) {
            throw std::invalid_argument("missing routed via resistance: " + v.name);
          }
          if (seen_vias.emplace(v.name, previous->x, previous->y, v.orientation).second) {
            vias.push_back({{layer, previous->x, previous->y}, {other, previous->x, previous->y}, resistance, {}});
            for (auto pad : shapes) {
              if (tech.layers.contains(pad.layer)) {
                pad.rect.xl += previous->x;
                pad.rect.xh += previous->x;
                pad.rect.yl += previous->y;
                pad.rect.yh += previous->y;
                vias.back().pads.push_back(std::move(pad));
              }
            }
          }
          layer = other;
        }
      }
    }
  }
  std::vector<Segment> merged;
  if (!variable_width) {
    // Keep the ordinary-width hot path allocation-light.
    std::sort(segments.begin(), segments.end(), [](const auto& a, const auto& b) {
      return std::tie(a.layer, a.horizontal, a.cross, a.lo, a.hi) < std::tie(b.layer, b.horizontal, b.cross, b.lo, b.hi);
    });
    for (const auto& s : segments) {
      if (!merged.empty()) {
        auto& last = merged.back();
        if (std::tie(last.layer, last.horizontal, last.cross) == std::tie(s.layer, s.horizontal, s.cross) && s.lo <= last.hi) {
          last.hi = std::max(last.hi, s.hi);
          continue;
        }
      }
      merged.push_back(s);
    }
  } else {
    // Union collinear rectangles, splitting at width transitions. A patch
    // overlapping a wire contributes only its additional physical metal.
    using Line  = std::tuple<std::string, bool, int64_t>;
    using Event = std::pair<double, bool>;  // width, starts
    std::map<Line, std::map<int64_t, std::vector<Event>>> lines;
    for (const auto& s : segments) {
      auto& events = lines[{s.layer, s.horizontal, s.cross}];
      events[s.lo].emplace_back(s.width, true);
      events[s.hi].emplace_back(s.width, false);
    }
    for (const auto& [line, events] : lines) {
      const auto& [layer, horizontal, cross] = line;
      std::multiset<double>  active;
      std::optional<int64_t> previous;
      for (const auto& [position, changes] : events) {
        if (previous && !active.empty()) {
          const double width = *active.rbegin();
          if (!merged.empty() && merged.back().layer == layer && merged.back().horizontal == horizontal
              && merged.back().cross == cross && merged.back().hi == *previous && merged.back().width == width) {
            merged.back().hi = position;
          } else {
            merged.push_back({layer, horizontal, cross, *previous, position, width});
          }
        }
        for (const auto& [width, starts] : changes) {
          if (starts) {
            active.insert(width);
          } else {
            const auto found = active.find(width);
            if (found == active.end()) {
              throw std::invalid_argument("invalid routed metal interval");
            }
            active.erase(found);
          }
        }
        previous = position;
      }
    }
  }
  std::map<Point, unsigned> points;
  for (const auto& s : merged) {
    points.emplace(at(s, s.lo), 0);
    points.emplace(at(s, s.hi), 0);
  }
  for (const auto& v : vias) {
    points.emplace(v.a, 0);
    points.emplace(v.b, 0);
  }
  for (const auto& p : pins) {
    points.emplace(Point{p.layer, p.x, p.y}, 0);
  }
  for (size_t i = 0; i < merged.size(); ++i) {
    for (size_t j = 0; j < i; ++j) {
      const auto& a = merged[i];
      const auto& b = merged[j];
      if (a.layer != b.layer) {
        continue;
      }
      const auto box = [&](const Segment& e) {
        const auto width = e.width;
        return e.horizontal ? ph::Rect{double(e.lo), e.cross - width / 2, double(e.hi), e.cross + width / 2}
                            : ph::Rect{e.cross - width / 2, double(e.lo), e.cross + width / 2, double(e.hi)};
      };
      const auto ar = box(a), br = box(b);
      const auto ox      = std::min(ar.xh, br.xh) - std::max(ar.xl, br.xl);
      const auto oy      = std::min(ar.yh, br.yh) - std::max(ar.yl, br.yl);
      const bool touches = ox >= 0 && oy >= 0 && (ox > 0 || oy > 0);
      if (a.horizontal == b.horizontal) {
        if (touches && a.cross != b.cross) {
          throw std::invalid_argument("non-centerline parallel metal contact in routed RC");
        }
        continue;
      }
      Point cross{a.layer, a.horizontal ? b.cross : a.cross, a.horizontal ? a.cross : b.cross};
      if (on(cross, a) && on(cross, b)) {
        points.emplace(cross, 0);
      } else if (touches) {
        throw std::invalid_argument("non-centerline wire junction in routed RC");
      }
    }
  }
  for (const auto& v : vias) {
    for (const auto& pad : v.pads) {
      for (const auto& s : merged) {
        if (s.layer != pad.layer) {
          continue;
        }
        const auto     width = s.width;
        const ph::Rect box   = s.horizontal ? ph::Rect{double(s.lo), s.cross - width / 2, double(s.hi), s.cross + width / 2}
                                            : ph::Rect{s.cross - width / 2, double(s.lo), s.cross + width / 2, double(s.hi)};
        const auto     ox    = std::min(box.xh, pad.rect.xh) - std::max(box.xl, pad.rect.xl);
        const auto     oy    = std::min(box.yh, pad.rect.yh) - std::max(box.yl, pad.rect.yl);
        const Point    origin{pad.layer, std::get<1>(v.a), std::get<2>(v.a)};
        if (ox >= 0 && oy >= 0 && (ox > 0 || oy > 0) && !on(origin, s)) {
          // A width transition can split a continuous centerline inside the
          // via pad. Its adjacent section still reaches this via through the
          // widened section; do not mistake that for an offset contact.
          bool reaches = false;
          if ((s.horizontal ? std::get<2>(origin) : std::get<1>(origin)) == s.cross) {
            const auto along  = s.horizontal ? std::get<1>(origin) : std::get<2>(origin);
            auto       cursor = std::min(along, s.lo);
            const auto end    = std::max(along, s.hi);
            for (const auto& part : merged) {
              if (part.layer != s.layer || part.horizontal != s.horizontal || part.cross != s.cross || part.hi < cursor) {
                continue;
              }
              if (part.lo > cursor) {
                break;
              }
              cursor = part.hi;
              if (cursor >= end) {
                reaches = true;
                break;
              }
            }
          }
          if (!reaches) {
            throw std::invalid_argument("non-centerline via-to-wire contact in routed RC");
          }
        }
      }
    }
  }
  ph::Rc_net out;
  out.driver = driver;
  for (auto& [p, id] : points) {
    id = out.nodes.size();
    out.nodes.push_back({{}, 0});
  }
  for (const auto& s : merged) {
    const auto found = rc.layers.find(s.layer);
    if (found == rc.layers.end() || !std::isfinite(found->second.r) || !std::isfinite(found->second.c) || found->second.r < 0
        || found->second.c < 0) {
      throw std::invalid_argument("missing routed layer RC: " + s.layer);
    }
    std::vector<std::pair<int64_t, unsigned>> split;
    for (const auto& [p, id] : points) {
      if (on(p, s)) {
        split.emplace_back(s.horizontal ? std::get<1>(p) : std::get<2>(p), id);
      }
    }
    std::sort(split.begin(), split.end());
    for (size_t i = 1; i < split.size(); ++i) {
      const auto   length = double(split[i].first - split[i - 1].first) / fp.dbu;
      const auto   a = split[i - 1].second, b = split[i].second;
      const double nominal_width = tech.layers.at(s.layer).width * fp.dbu;
      if (!std::isfinite(nominal_width) || nominal_width <= 0) {
        throw std::invalid_argument("invalid routed layer nominal width");
      }
      const double width_ratio = s.width / nominal_width;
      const auto   resistance  = length * found->second.r / width_ratio;
      const auto   capacitance = length * found->second.c * width_ratio;
      out.edges.push_back({a, b, resistance});
      out.nodes[a].capacitance_ff += capacitance / 2;
      out.nodes[b].capacitance_ff += capacitance / 2;
      out.wire_length_um          += length;
    }
  }
  for (const auto& v : vias) {
    out.edges.push_back({points.at(v.a), points.at(v.b), v.resistance});
  }
  for (const auto& p : pins) {
    const auto id = points.at(Point{p.layer, p.x, p.y});
    if (out.nodes[id].pin.empty()) {
      out.nodes[id].pin = p.pin;
    } else {
      const auto leaf = unsigned(out.nodes.size());
      out.nodes.push_back({p.pin, 0});
      out.edges.push_back({id, leaf, 0});
    }
  }
  for (const auto& edge : out.edges) {
    if (!std::isfinite(edge.resistance_ohm)) {
      throw std::invalid_argument("routed resistance overflow");
    }
  }
  for (const auto& node : out.nodes) {
    if (!std::isfinite(node.capacitance_ff)) {
      throw std::invalid_argument("routed capacitance overflow");
    }
  }
  ph::validate_rc_tree(out);
  return out;
}
}  // namespace livehd::placement
