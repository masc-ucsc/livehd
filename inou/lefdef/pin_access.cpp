// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pin_access.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

#include "routing_geometry.hpp"
#include "routing_occupancy.hpp"

namespace livehd::physical {
namespace {
bool contains(const Rect& a, const Rect& b) { return a.xl <= b.xl && a.yl <= b.yl && a.xh >= b.xh && a.yh >= b.yh; }
bool valid(const Rect& r) {
  for (auto v : {r.xl, r.yl, r.xh, r.yh}) {
    if (!std::isfinite(v) || std::abs(v) > std::numeric_limits<int32_t>::max() || v != std::round(v)) {
      return false;
    }
  }
  return r.xl < r.xh && r.yl < r.yh;
}
Rect shifted(Rect r, int64_t x, int64_t y) { return {r.xl + x, r.yl + y, r.xh + x, r.yh + y}; }
bool covered(const Rect& r, const std::vector<Rect>& pin) {
  std::vector<double> xs{r.xl, r.xh};
  for (const auto& s : pin) {
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
    std::vector<std::pair<double, double>> ys;
    for (const auto& s : pin) {
      if (s.xl <= xs[i - 1] && s.xh >= xs[i]) {
        ys.emplace_back(s.yl, s.yh);
      }
    }
    std::sort(ys.begin(), ys.end());
    auto high = r.yl;
    for (const auto& [lo, hi] : ys) {
      if (lo > high) {
        break;
      }
      high = std::max(high, hi);
    }
    if (high < r.yh) {
      return false;
    }
  }
  return true;
}
}  // namespace
static Pin_access_result enumerate_pin_access_impl(const Technology& tech, const Floorplan& fp, const Component& cell,
                                                   const std::string& pin_name, std::span<const Shape> obstacles,
                                                   const Signal_metal_rules& rules, size_t budget,
                                                   const Routing_occupancy* occupancy, const std::string& own_net,
                                                   const Pin_access_layers& layers) {
  Pin_access_result out;
  const auto        fail = [&](const std::string& reason) {
    ++out.rejected[reason];
    return out;
  };
  if (fp.dbu <= 0 || fp.dbu > std::numeric_limits<int32_t>::max() || !valid(fp.die)) {
    return fail("invalid floorplan units/die");
  }
  const auto master_it = tech.macros.find(cell.macro);
  if (master_it == tech.macros.end() || !master_it->second.klass.starts_with("CORE") || master_it->second.virtual_proxy
      || cell.place.status == Status::UNPLACED) {
    return fail("pin access requires a placed physical standard cell");
  }
  const auto& master = master_it->second;
  const auto  pin_it = master.pins.find(pin_name);
  if (pin_it == master.pins.end()) {
    return fail("unknown physical pin");
  }
  if (!tech.layers.contains(layers.escape) || tech.layers.at(layers.escape).direction != "HORIZONTAL"
      || !tech.cut_layers.contains(layers.cut)) {
    return fail("unsupported " + layers.pin + "/" + layers.escape + " access stack");
  }
  if (tech.routing_layers.size() < 2 || tech.routing_layers[0] != layers.pin || tech.routing_layers[1] != layers.escape) {
    return fail("unsupported access routing-layer order");
  }
  const auto& cut_rule = tech.cut_layers.at(layers.cut);
  if (!cut_rule.unsupported.empty() || !cut_rule.properties.empty() || cut_rule.spacing.empty()) {
    return fail("unsupported " + layers.cut + " cut rules");
  }
  double cut_spacing = 0;
  for (auto spacing : cut_rule.spacing) {
    if (!std::isfinite(spacing) || spacing < 0) {
      return fail("invalid cut spacing");
    }
    cut_spacing = std::max(cut_spacing, std::ceil(spacing * fp.dbu - 1e-9));
  }
  if (!std::isfinite(cut_spacing) || cut_spacing > INT32_MAX) {
    return fail("unrepresentable cut spacing");
  }
  const auto clearance = rules.clearance(layers.escape);
  if (occupancy && !clearance) {
    return fail("unsupported " + layers.escape + " query clearance");
  }
  std::vector<Rect> pin;
  for (const auto& shape : pin_it->second.shapes) {
    if (shape.layer != layers.pin) {
      return fail("pin is not entirely " + layers.pin);
    }
    const auto transform = [&](double x, double y) {
      const auto [ox, oy]
          = orient_point(x + master.origin_x, y + master.origin_y, master.width, master.height, cell.place.orientation);
      return std::pair{cell.place.x + ox * fp.dbu, cell.place.y + oy * fp.dbu};
    };
    const auto [x0, y0] = transform(shape.rect.xl, shape.rect.yl);
    const auto [x1, y1] = transform(shape.rect.xh, shape.rect.yh);
    for (auto coordinate : {x0, y0, x1, y1}) {
      if (!std::isfinite(coordinate) || std::abs(coordinate - std::round(coordinate)) > 1e-6) {
        return fail("pin geometry is not representable in DEF DBU");
      }
    }
    Rect rect{std::round(std::min(x0, x1)),
              std::round(std::min(y0, y1)),
              std::round(std::max(x0, x1)),
              std::round(std::max(y0, y1))};
    if (!valid(rect) || !contains(fp.die, rect)) {
      return fail("invalid transformed pin");
    }
    pin.push_back(rect);
  }
  if (pin.empty()) {
    return fail("empty pin geometry");
  }
  for (const auto& shape : obstacles) {
    if (!valid(shape.rect)) {
      return fail("invalid obstacle geometry");
    }
  }
  Rect bounds = pin.front();
  for (const auto& r : pin) {
    bounds = {std::min(bounds.xl, r.xl), std::min(bounds.yl, r.yl), std::max(bounds.xh, r.xh), std::max(bounds.yh, r.yh)};
  }
  std::set<int64_t> ys;
  for (const auto& track : fp.tracks) {
    if (track.layer != layers.escape || track.axis != "Y") {
      continue;
    }
    if (track.mask || track.same_mask || track.step <= 0 || track.count <= 0 || track.step > INT32_MAX || track.count > INT32_MAX
        || track.start < INT32_MIN || track.start > INT32_MAX) {
      return fail("unsupported " + layers.escape + " tracks");
    }
    const auto first = std::max<int64_t>(0, std::ceil((bounds.yl - track.start) / track.step));
    const auto last  = std::min<int64_t>(track.count - 1, std::floor((bounds.yh - track.start) / track.step));
    if (last >= first && uint64_t(last - first) >= budget) {
      out.exhausted = true;
      return fail("track enumeration budget");
    }
    for (auto i = first; i <= last; ++i) {
      ys.insert(track.start + i * track.step);
      if (ys.size() > budget) {
        out.exhausted = true;
        return fail("track enumeration budget");
      }
    }
  }
  if (ys.empty()) {
    return fail("no " + layers.escape + " track crosses pin");
  }
  const auto grid_value = tech.manufacturing_grid == 0 ? 1. : tech.manufacturing_grid * fp.dbu;
  if (!std::isfinite(grid_value) || grid_value < 1 || grid_value > INT32_MAX
      || std::abs(grid_value - std::round(grid_value)) > 1e-9) {
    return fail("unsupported manufacturing grid");
  }
  const int64_t     grid = std::llround(grid_value);
  // Candidate positions are track crossings and pin-rectangle centers, not
  // every manufacturing-grid coordinate across a wide pin. Besides bounding
  // work, this avoids inventing a dense set of redundant access points that
  // the downstream pin-access engine does not enumerate.
  std::set<int64_t> xs;
  for (const auto& r : pin) {
    xs.insert(int64_t(std::floor((r.xl + r.xh) / (2 * grid))) * grid);
    if (xs.size() > budget) {
      out.exhausted = true;
      return fail("track enumeration budget");
    }
  }
  for (const auto& track : fp.tracks) {
    if (track.layer != layers.pin || track.axis != "X") {
      continue;
    }
    if (track.mask || track.same_mask || track.step <= 0 || track.count <= 0 || track.step > INT32_MAX || track.count > INT32_MAX
        || track.start < INT32_MIN || track.start > INT32_MAX) {
      return fail("unsupported " + layers.pin + " tracks");
    }
    const auto first = std::max<int64_t>(0, std::ceil((bounds.xl - track.start) / track.step));
    const auto last  = std::min<int64_t>(track.count - 1, std::floor((bounds.xh - track.start) / track.step));
    if (last >= first && uint64_t(last - first) >= budget) {
      out.exhausted = true;
      return fail("track enumeration budget");
    }
    for (auto i = first; i <= last; ++i) {
      const auto x = track.start + i * track.step;
      if (x % grid == 0) {
        xs.insert(x);
      }
      if (xs.size() > budget) {
        out.exhausted = true;
        return fail("track enumeration budget");
      }
    }
  }
  Floorplan via_frame;
  via_frame.dbu = fp.dbu;
  for (const auto& [name, via] : tech.vias) {
    // DEF VIAS override identically named LEF templates at emission/readback.
    // This enumerator validates LEF templates only; never silently emit a
    // different via than the one whose pin containment and spacing we checked.
    if (std::any_of(fp.vias.begin(), fp.vias.end(), [&](const auto& local) { return local.name == name; })) {
      ++out.rejected["DEF overrides LEF access via"];
      continue;
    }
    if (!via.unsupported.empty() || via.shapes.size() != 3) {
      continue;
    }
    const auto   shapes = via_geometry(tech, via_frame, {name});
    const Shape *lower = nullptr, *upper = nullptr, *cut = nullptr;
    for (const auto& shape : shapes) {
      if (shape.layer == layers.pin) {
        lower = &shape;
      }
      if (shape.layer == layers.escape) {
        upper = &shape;
      }
      if (shape.layer == layers.cut) {
        cut = &shape;
      }
    }
    if (!lower || !upper || !cut || !valid(lower->rect) || !valid(upper->rect) || !valid(cut->rect)
        || !contains(lower->rect, cut->rect) || !contains(upper->rect, cut->rect)) {
      continue;
    }
    if (!std::isfinite(cut_rule.width) || cut_rule.width < 0
        || std::min(cut->rect.xh - cut->rect.xl, cut->rect.yh - cut->rect.yl) < std::ceil(cut_rule.width * fp.dbu - 1e-9)) {
      continue;
    }
    const auto width = upper->rect.yh - upper->rect.yl;
    if (upper->rect.yl != -upper->rect.yh) {
      continue;
    }
    auto length
        = std::max(upper->rect.xh - upper->rect.xl, std::ceil(tech.layers.at(layers.escape).rules.area * fp.dbu * fp.dbu / width));
    for (const auto& size : tech.layers.at(layers.escape).rules.min_size) {
      // Requiring every minimum-size alternative is conservative.
      length = std::max(length, std::max(size[0], size[1]) * fp.dbu);
    }
    if (!std::isfinite(length) || length <= 0 || length > INT32_MAX) {
      continue;
    }
    const int64_t half  = std::ceil(length / (2 * grid)) * grid;
    const auto    start = int64_t(std::ceil((bounds.xl - lower->rect.xl) / grid)) * grid;
    const auto    end   = int64_t(std::floor((bounds.xh - lower->rect.xh) / grid)) * grid;
    for (auto y : ys) {
      for (auto x : xs) {
        if (x < start || x > end) {
          continue;
        }
        if (out.examined >= budget) {
          out.exhausted = true;
          return out;
        }
        ++out.examined;
        if (!covered(shifted(lower->rect, x, y), pin)) {
          ++out.rejected["lower landing outside pin union"];
          continue;
        }
        Shape escape{
            layers.escape,
            {double(x - half), upper->rect.yl + y, double(x + half), upper->rect.yh + y}
        };
        if (!contains(escape.rect, shifted(upper->rect, x, y)) || !contains(fp.die, escape.rect)) {
          ++out.rejected["escape outside available geometry"];
          continue;
        }
        const auto check = rules.rectangle(escape);
        if (!check.legal) {
          ++out.rejected[check.reason];
          continue;
        }
        const auto cut_rect = shifted(cut->rect, x, y);
        bool       clear    = true;
        const auto blocked  = [&](const Shape& obstacle) {
          if (obstacle.layer == layers.escape) {
            return !rules.spacing(escape, obstacle).legal;
          }
          if (obstacle.layer == layers.cut) {
            const auto dx = std::max({obstacle.rect.xl - cut_rect.xh, cut_rect.xl - obstacle.rect.xh, 0.});
            const auto dy = std::max({obstacle.rect.yl - cut_rect.yh, cut_rect.yl - obstacle.rect.yh, 0.});
            return (dx == 0 && dy == 0) || (dx < cut_spacing && dy < cut_spacing);
          }
          return false;
        };
        if (occupancy) {
          for (auto id : occupancy->query(layers.escape, escape.rect, *clearance, own_net)) {
            if (blocked(occupancy->at(id).shape)) {
              clear = false;
              break;
            }
          }
          // Same-net cuts are not exempt from cut spacing.
          if (clear) {
            for (auto id : occupancy->query(layers.cut, cut_rect, int64_t(cut_spacing))) {
              if (blocked(occupancy->at(id).shape)) {
                clear = false;
                break;
              }
            }
          }
        } else {
          for (const auto& obstacle : obstacles) {
            if (blocked(obstacle)) {
              clear = false;
              break;
            }
          }
        }
        if (!clear) {
          ++out.rejected["metal or cut obstacle"];
          continue;
        }
        out.points.push_back({x, y, name, escape, layers.pin});
      }
    }
  }
  if (out.examined == 0) {
    ++out.rejected["no supported fitting via"];
  }
  return out;
}
Pin_access_result enumerate_pin_access(const Technology& tech, const Floorplan& fp, const Component& cell, const std::string& pin,
                                       const Routing_occupancy& occupancy, const Signal_metal_rules& rules, size_t budget,
                                       const std::string& own_net, const Pin_access_layers& layers) {
  return enumerate_pin_access_impl(tech, fp, cell, pin, {}, rules, budget, &occupancy, own_net, layers);
}
Pin_access_result enumerate_m1_pin_access(const Technology& tech, const Floorplan& fp, const Component& cell,
                                          const std::string& pin, std::span<const Shape> obstacles, const Signal_metal_rules& rules,
                                          size_t budget) {
  return enumerate_pin_access_impl(tech, fp, cell, pin, obstacles, rules, budget, nullptr, {}, asap7_pin_access_layers());
}
Pin_access_result enumerate_m1_pin_access(const Technology& tech, const Floorplan& fp, const Component& cell,
                                          const std::string& pin, const Routing_occupancy& occupancy,
                                          const Signal_metal_rules& rules, size_t budget, const std::string& own_net) {
  return enumerate_pin_access_impl(tech, fp, cell, pin, {}, rules, budget, &occupancy, own_net, asap7_pin_access_layers());
}
}  // namespace livehd::physical
