// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "routing_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace livehd::physical {
namespace {
// DEF coordinates are signed 32-bit in the reader and in OpenDB. Check before
// conversion; neither fractional DBU nor arithmetic overflow may shrink metal.
int64_t coordinate(double value) {
  if (const auto dbu = rounded_dbu(value, 1e-6)) {
    return *dbu;
  }
  throw std::runtime_error("routing geometry cannot be represented in integer DEF DBU");
}
Rect scaled(Rect r, double scale) {
  r = {double(coordinate(r.xl * scale)),
       double(coordinate(r.yl * scale)),
       double(coordinate(r.xh * scale)),
       double(coordinate(r.yh * scale))};
  if (r.empty()) {
    throw std::runtime_error("empty or inverted routing rectangle");
  }
  return r;
}
std::vector<Shape> generated_geometry(const Generated_via& g, const Technology& tech) {
  if (!tech.via_rules.contains(g.rule) || !tech.layers.contains(g.layers[0]) || !tech.cut_layers.contains(g.layers[1])
      || !tech.layers.contains(g.layers[2]) || g.layers[0] == g.layers[2]) {
    throw std::runtime_error("generated via references unknown rule/layers: " + g.rule);
  }
  const auto        count    = g.row_col.value_or(std::array<int64_t, 2>{1, 1});
  constexpr int64_t max_cuts = 1000000;
  if (count[0] <= 0 || count[1] <= 0 || count[0] > max_cuts || count[1] > max_cuts / count[0] || g.cut_size[0] <= 0
      || g.cut_size[1] <= 0 || g.cut_spacing[0] < 0 || g.cut_spacing[1] < 0
      || std::any_of(g.enclosure.begin(), g.enclosure.end(), [](auto n) { return n < 0; })) {
    throw std::runtime_error("invalid or oversized generated via array");
  }
  const auto         width  = coordinate(double(count[1]) * g.cut_size[0] + double(count[1] - 1) * g.cut_spacing[0]);
  const auto         height = coordinate(double(count[0]) * g.cut_size[1] + double(count[0] - 1) * g.cut_spacing[1]);
  const auto         origin = g.origin.value_or(std::array<int64_t, 2>{0, 0});
  // OpenDB centers odd-sized arrays with integer division (extra DBU on the
  // positive edge), rather than rounding both bounds independently.
  const double       xl = -width / 2 + double(origin[0]), yl = -height / 2 + double(origin[1]);
  const auto         offsets = g.offset.value_or(std::array<int64_t, 4>{0, 0, 0, 0});
  std::vector<Shape> shapes;
  shapes.reserve(count[0] * count[1] + 2);
  for (int64_t row = 0; row < count[0]; ++row) {
    for (int64_t col = 0; col < count[1]; ++col) {
      const double x = xl + col * (double(g.cut_size[0]) + g.cut_spacing[0]);
      const double y = yl + row * (double(g.cut_size[1]) + g.cut_spacing[1]);
      shapes.push_back({g.layers[1], scaled({x, y, x + g.cut_size[0], y + g.cut_size[1]}, 1)});
    }
  }
  for (int metal : {1, 0}) {
    const auto i = 2 * metal;
    shapes.push_back({g.layers[2 * metal],
                      scaled({xl - g.enclosure[i] + offsets[i],
                              yl - g.enclosure[i + 1] + offsets[i + 1],
                              xl + width + g.enclosure[i] + offsets[i],
                              yl + height + g.enclosure[i + 1] + offsets[i + 1]},
                             1)});
  }
  return shapes;
}
}  // namespace

std::vector<Shape> via_geometry(const Technology& tech, const Floorplan& fp, const Route_via& via) {
  if (fp.dbu <= 0) {
    throw std::runtime_error("invalid routing DBU");
  }
  std::vector<Shape> shapes;
  const auto         local = std::find_if(fp.vias.begin(), fp.vias.end(), [&](const auto& v) { return v.name == via.name; });
  if (local != fp.vias.end()) {
    if (local->generated && !local->shapes.empty()) {
      throw std::runtime_error("mixed generated/explicit via: " + via.name);
    }
    shapes = local->generated ? generated_geometry(*local->generated, tech) : local->shapes;
    for (auto& shape : shapes) {
      shape.rect = scaled(shape.rect, 1);
    }
  } else {
    const auto it = tech.vias.find(via.name);
    if (it == tech.vias.end() || !it->second.unsupported.empty()) {
      throw std::runtime_error("unknown or unsupported LEF via: " + via.name);
    }
    shapes = it->second.shapes;
    for (auto& shape : shapes) {
      shape.rect = scaled(shape.rect, fp.dbu);
    }
  }
  if (shapes.empty()) {
    throw std::runtime_error("via has no geometry: " + via.name);
  }
  for (auto& shape : shapes) {
    if (!tech.layers.contains(shape.layer) && !tech.cut_layers.contains(shape.layer)) {
      throw std::runtime_error("unknown via layer: " + shape.layer);
    }
    shape.rect = orient_rect(shape.rect, 0, 0, via.orientation);
  }
  return shapes;
}

void visit_route_geometry(const Technology& tech, const Floorplan& fp, const Net& net,
                          const std::function<void(const Shape&)>& visit, bool special, const Route_width_rules* trial_rules) {
  if (trial_rules) {
    for (const auto& [name, widths] : *trial_rules) {
      const auto installed = fp.nondefault_rules.find(name);
      if (installed != fp.nondefault_rules.end() && installed->second != widths) {
        throw std::runtime_error("conflicting trial route width rule: " + name);
      }
    }
  }
  struct Cached_via {
    std::vector<Shape>    shapes;
    std::set<std::string> metals;
  };
  std::map<std::pair<std::string, Orientation>, Cached_via> vias;
  for (const auto& wire : net.wires) {
    for (const auto& path : wire.paths) {
      auto                       layer = path.layer;
      std::optional<Route_point> previous;
      if (!tech.layers.contains(layer) || path.steps.empty() || !std::holds_alternative<Route_point>(path.steps.front())
          || (special && (!path.width || !path.taper_rule.empty())) || (!special && path.width)) {
        throw std::runtime_error("invalid route path on " + net.name);
      }
      for (const auto& step : path.steps) {
        if (const auto* point = std::get_if<Route_point>(&step)) {
          coordinate(point->x);
          coordinate(point->y);
          if (point->extension && *point->extension < 0) {
            throw std::runtime_error("negative route extension");
          }
          if (previous) {
            double width = path.width ? double(*path.width) : tech.layers.at(layer).width * fp.dbu;
            if (!path.taper_rule.empty()) {
              const auto                            installed = fp.nondefault_rules.find(path.taper_rule);
              const std::map<std::string, int64_t>* widths = installed == fp.nondefault_rules.end() ? nullptr : &installed->second;
              if (!widths && trial_rules) {
                const auto trial = trial_rules->find(path.taper_rule);
                if (trial != trial_rules->end()) {
                  widths = &trial->second;
                }
              }
              if (!widths || !widths->contains(layer)) {
                throw std::runtime_error("undefined DEF taper rule width on " + layer);
              }
              width = widths->at(layer);
            }
            if (width <= 0 || coordinate(width) % 2 != 0) {
              throw std::runtime_error("route width must be positive and even in DEF DBU");
            }
            const double half              = width / 2;
            // SPECIALNETS endpoints describe the stripe's physical extent.
            // Ordinary NETS use the default half-width extension instead.
            const double default_extension = special ? 0 : half;
            const double start             = previous->extension ? double(*previous->extension) : default_extension;
            const double end               = point->extension ? double(*point->extension) : default_extension;
            Rect         r;
            if (previous->y == point->y && previous->x != point->x) {
              r = previous->x < point->x ? Rect{previous->x - start, point->y - half, point->x + end, point->y + half}
                                         : Rect{point->x - end, point->y - half, previous->x + start, point->y + half};
            } else if (previous->x == point->x && previous->y != point->y) {
              r = previous->y < point->y ? Rect{point->x - half, previous->y - start, point->x + half, point->y + end}
                                         : Rect{point->x - half, point->y - end, point->x + half, previous->y + start};
            } else {
              throw std::runtime_error("non-Manhattan or zero-length route segment");
            }
            visit({layer, scaled(r, 1)});
          }
          previous = *point;
        } else if (const auto* rect = std::get_if<Route_rect>(&step)) {
          if (special || rect->xl >= rect->xh || rect->yl >= rect->yh) {
            throw std::runtime_error("invalid signal route patch");
          }
          // Add in floating point before validating integer DBU bounds so
          // malformed offsets cannot overflow signed integer arithmetic.
          visit({layer,
                 scaled({double(previous->x) + coordinate(rect->xl),
                         double(previous->y) + coordinate(rect->yl),
                         double(previous->x) + coordinate(rect->xh),
                         double(previous->y) + coordinate(rect->yh)},
                        1)});
        } else {
          const auto& via = std::get<Route_via>(step);
          const auto  key = std::pair{via.name, via.orientation};
          auto        it  = vias.find(key);
          if (it == vias.end()) {
            Cached_via decoded{via_geometry(tech, fp, via), {}};
            for (const auto& shape : decoded.shapes) {
              if (tech.layers.contains(shape.layer)) {
                decoded.metals.insert(shape.layer);
              }
            }
            it = vias.emplace(key, std::move(decoded)).first;
          }
          for (const auto& shape : it->second.shapes) {
            visit({shape.layer, scaled(shape.rect.translated(previous->x, previous->y), 1)});
          }
          const auto& metals = it->second.metals;
          if (metals.size() != 2 || !metals.contains(layer)) {
            throw std::runtime_error("via does not connect current route layer");
          }
          layer = layer == *metals.begin() ? *metals.rbegin() : *metals.begin();
        }
      }
    }
  }
}
std::vector<Shape> route_geometry(const Technology& tech, const Floorplan& fp, const Net& net, bool special,
                                  const Route_width_rules* trial_rules) {
  std::vector<Shape> shapes;
  visit_route_geometry(tech, fp, net, [&](const Shape& shape) { shapes.push_back(shape); }, special, trial_rules);
  return shapes;
}
Route_path flush_wire_path(const Shape& metal, bool horizontal, std::string taper_rule) {
  const auto& r      = metal.rect;
  const auto  center = std::llround(horizontal ? (r.yl + r.yh) / 2 : (r.xl + r.xh) / 2);
  const auto  lo = std::llround(horizontal ? r.xl : r.yl), hi = std::llround(horizontal ? r.xh : r.yh);
  return {
      metal.layer,
      {},
      {},
      {Route_point{horizontal ? lo : center, horizontal ? center : lo, 0},
              Route_point{horizontal ? hi : center, horizontal ? center : hi, 0}},
      std::move(taper_rule)
  };
}
Route_path via_path(const std::string& lower_layer, int64_t x, int64_t y, const std::string& via) {
  return {
      lower_layer,
      {},
      {},
      {Route_point{x, y, {}}, Route_via{via}}
  };
}
}  // namespace livehd::physical
