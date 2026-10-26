// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pin_access.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

#include "cut_rules.hpp"
#include "routing_geometry.hpp"
#include "routing_occupancy.hpp"

namespace livehd::physical {
namespace {
bool valid(const Rect& r) { return integral_dbu(r) && !r.empty(); }
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
    auto rect = placed_rect(master, cell.place, fp.dbu, shape.rect);
    for (auto* coordinate : {&rect.xl, &rect.yl, &rect.xh, &rect.yh}) {
      if (!std::isfinite(*coordinate) || std::abs(*coordinate - std::round(*coordinate)) > 1e-6) {
        return fail("pin geometry is not representable in DEF DBU");
      }
      *coordinate = std::round(*coordinate);
    }
    if (!valid(rect) || !fp.die.contains(rect)) {
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
    bounds = bounds.hull(r);
  }
  std::set<int64_t> ys;
  for (const auto& track : fp.tracks) {
    if (track.layer != layers.escape || track.axis != "Y") {
      continue;
    }
    if (!track.supported()) {
      return fail("unsupported " + layers.escape + " tracks");
    }
    const auto [first, last] = track.index_range(bounds.yl, bounds.yh);
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
  const auto grid_value = manufacturing_grid_dbu(tech, fp.dbu);
  if (!grid_value) {
    return fail("unsupported manufacturing grid");
  }
  const int64_t     grid = *grid_value;
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
    if (!track.supported()) {
      return fail("unsupported " + layers.pin + " tracks");
    }
    const auto [first, last] = track.index_range(bounds.xl, bounds.xh);
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
        || !lower->rect.contains(cut->rect) || !upper->rect.contains(cut->rect)) {
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
        if (!covered(lower->rect.translated(x, y), pin)) {
          ++out.rejected["lower landing outside pin union"];
          continue;
        }
        Shape escape{
            layers.escape,
            {double(x - half), upper->rect.yl + y, double(x + half), upper->rect.yh + y}
        };
        if (!escape.rect.contains(upper->rect.translated(x, y)) || !fp.die.contains(escape.rect)) {
          ++out.rejected["escape outside available geometry"];
          continue;
        }
        const auto check = rules.rectangle(escape);
        if (!check.legal) {
          ++out.rejected[check.reason];
          continue;
        }
        const auto cut_rect = cut->rect.translated(x, y);
        bool       clear    = true;
        const auto blocked  = [&](const Shape& obstacle) {
          if (obstacle.layer == layers.escape) {
            return !rules.spacing(escape, obstacle).legal;
          }
          if (obstacle.layer == layers.cut) {
            return violates_cut_spacing(obstacle.rect, cut_rect, cut_spacing);
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
Pin_access_result enumerate_pin_access(const Technology& tech, const Floorplan& fp, const Component& cell, const std::string& pin,
                                       std::span<const Shape> obstacles, const Signal_metal_rules& rules, size_t budget,
                                       const Pin_access_layers& layers) {
  return enumerate_pin_access_impl(tech, fp, cell, pin, obstacles, rules, budget, nullptr, {}, layers);
}
}  // namespace livehd::physical
