// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "signal_via.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "cut_rules.hpp"
#include "routing_geometry.hpp"

namespace livehd::physical {
namespace {
bool contains(const Rect& a, const Rect& b) { return a.xl <= b.xl && a.yl <= b.yl && a.xh >= b.xh && a.yh >= b.yh; }
}  // namespace
Rule_check check_signal_via(const Technology& tech, const Floorplan& fp, const Signal_metal_rules& rules,
                            const Routing_occupancy& occupancy, const Route_via& via, int64_t x, int64_t y,
                            const std::array<Shape, 2>& landings, const std::string& own_net) {
  const auto fail = [](const std::string& message) { return Rule_check{false, message}; };
  if (fp.dbu <= 0 || fp.dbu > INT32_MAX || x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX) {
    return fail("invalid via units/position");
  }
  if (landings[0].layer == landings[1].layer) {
    return fail("via needs two distinct landing layers");
  }
  const auto a = std::find(tech.routing_layers.begin(), tech.routing_layers.end(), landings[0].layer);
  const auto b = std::find(tech.routing_layers.begin(), tech.routing_layers.end(), landings[1].layer);
  if (a == tech.routing_layers.end() || b == tech.routing_layers.end() || std::abs(a - b) != 1) {
    return fail("via does not join adjacent routing layers");
  }
  if (std::any_of(fp.vias.begin(), fp.vias.end(), [&](const auto& local) { return local.name == via.name; })) {
    return fail("DEF overrides LEF signal via");
  }
  const auto grid = tech.manufacturing_grid == 0 ? 1. : tech.manufacturing_grid * fp.dbu;
  if (!std::isfinite(grid) || grid < 1 || grid > INT32_MAX || std::abs(grid - std::round(grid)) > 1e-9) {
    return fail("invalid manufacturing grid");
  }
  if (x % std::llround(grid) || y % std::llround(grid)) {
    return fail("off-grid via origin");
  }
  try {
    auto shapes = via_geometry(tech, fp, via);
    if (shapes.size() != 3) {
      return fail("signal via requires two metal shapes and one cut");
    }
    const Shape*        cut = nullptr;
    std::array<bool, 2> covered{};
    for (auto& s : shapes) {
      s.rect.xl += x;
      s.rect.xh += x;
      s.rect.yl += y;
      s.rect.yh += y;
      for (auto v : {s.rect.xl, s.rect.yl, s.rect.xh, s.rect.yh}) {
        if (v < INT32_MIN || v > INT32_MAX || std::llround(v) % std::llround(grid)) {
          return fail("off-grid via geometry");
        }
      }
      if (!contains(fp.die, s.rect)) {
        return fail("via outside die");
      }
      if (tech.cut_layers.contains(s.layer)) {
        if (cut) {
          return fail("multiple signal via cuts");
        }
        cut = &s;
      } else {
        bool matched = false;
        for (size_t i = 0; i < 2; ++i) {
          if (landings[i].layer == s.layer && !covered[i] && contains(landings[i].rect, s.rect)) {
            covered[i] = matched = true;
            break;
          }
        }
        if (!matched) {
          return fail("via metal not enclosed by route landing");
        }
      }
    }
    if (!cut || !covered[0] || !covered[1]) {
      return fail("missing via cut or metal");
    }
    for (const auto& s : shapes) {
      if (s.layer != cut->layer && !contains(s.rect, cut->rect)) {
        return fail("cut not enclosed by via metal");
      }
    }
    const auto&                  rule    = tech.cut_layers.at(cut->layer);
    const auto                   spacing = signal_cut_spacing(rule, fp.dbu);
    std::set<std::string>        single_cut;
    const auto                   classes = cut_classes(rule, fp.dbu, &single_cut);
    const std::array<int64_t, 2> cut_size{std::llround(cut->rect.xh - cut->rect.xl), std::llround(cut->rect.yh - cut->rect.yl)};
    if (classes.empty()) {
      const auto width = rule.width * fp.dbu;
      if (!std::isfinite(width) || width <= 0 || cut_size[0] != width || cut_size[1] != width) {
        return fail("unsupported signal cut width");
      }
    } else {
      if (std::none_of(single_cut.begin(), single_cut.end(), [&](const auto& name) { return classes.at(name) == cut_size; })) {
        return fail("signal via does not match a single-cut class");
      }
    }
    for (const auto& landing : landings) {
      const auto                   layer = std::find(tech.routing_layers.begin(), tech.routing_layers.end(), landing.layer);
      const bool                   above = layer == std::max(a, b);
      const std::array<int64_t, 2> extent{std::llround(landing.rect.xh - landing.rect.xl),
                                          std::llround(landing.rect.yh - landing.rect.yl)};
      const auto enclosures = cut_enclosures(rule, fp.dbu, classes, cut_size, above, tech.layers.at(landing.layer), extent);
      if (std::none_of(enclosures.begin(), enclosures.end(), [&](const auto& e) {
            return cut->rect.xl - landing.rect.xl >= e[0] && landing.rect.xh - cut->rect.xh >= e[0]
                   && cut->rect.yl - landing.rect.yl >= e[1] && landing.rect.yh - cut->rect.yh >= e[1];
          })) {
        return fail("signal cut enclosure");
      }
    }
    // Same-net cuts still need spacing. A trial cannot reuse an existing cut
    // implicitly; shared via reuse is an explicit routing-graph operation.
    for (auto id : occupancy.query(cut->layer, cut->rect, static_cast<int64_t>(spacing))) {
      const auto& r  = occupancy.at(id).shape.rect;
      const auto  dx = std::max({r.xl - cut->rect.xh, cut->rect.xl - r.xh, 0.});
      const auto  dy = std::max({r.yl - cut->rect.yh, cut->rect.yl - r.yh, 0.});
      if ((dx == 0 && dy == 0) || (dx < spacing && dy < spacing)) {
        return fail("signal cut spacing");
      }
    }
    for (const auto& landing : landings) {
      const auto valid = rules.rectangle(landing);
      if (!valid.legal) {
        return valid;
      }
      if (!contains(fp.die, landing.rect)) {
        return fail("landing outside die");
      }
      const auto clearance = rules.clearance(landing.layer);
      if (!clearance) {
        return fail("unsupported landing spacing rules");
      }
      for (auto id : occupancy.query(landing.layer, landing.rect, *clearance, own_net)) {
        const auto separated = rules.spacing(landing, occupancy.at(id).shape);
        if (!separated.legal) {
          return separated;
        }
      }
    }
  } catch (const std::exception& e) {
    return fail(e.what());
  }
  return {true, {}};
}
}  // namespace livehd::physical
