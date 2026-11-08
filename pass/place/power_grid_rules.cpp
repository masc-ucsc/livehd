// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "power_grid_rules.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "power_grid.hpp"

namespace livehd::placement {
namespace ph = physical;
double pg_layer_spacing(const ph::Layer& layer, double width) {
  double best = 0;
  for (const auto& rule : layer.rules.spacing) {
    if (!rule.range && !rule.eol) {
      best = std::max(best, rule.distance);
    }
  }
  for (const auto& table : layer.rules.parallel) {
    size_t row = 0;
    for (size_t i = 0; i < table.widths.size() && i < table.spacing.size(); ++i) {
      if (table.widths[i] <= width + 1e-9) {
        row = i;
      }
    }
    if (row < table.spacing.size() && !table.spacing[row].empty()) {
      best = std::max(best, *std::max_element(table.spacing[row].begin(), table.spacing[row].end()));
    }
  }
  return best;
}
double pg_feeder_spacing(const ph::Layer& layer, const ph::Pg_stripe& stripe) {
  const auto lef = pg_layer_spacing(layer, stripe.width);
  return lef > 0 ? std::min(stripe.spacing, lef) : stripe.spacing;
}
namespace {
const auto layer_spacing = pg_layer_spacing;
}  // namespace

Pg_macro_rules pg_macro_rules(const ph::Technology& tech, const ph::Pg_strategy& pg, const std::set<std::string>& masters) {
  std::set<std::string> blocked;
  bool                  any_macro = false;
  for (const auto& name : masters) {
    const auto found = tech.macros.find(name);
    if (found == tech.macros.end()) {
      throw std::invalid_argument("unknown macro master: " + name);
    }
    const auto& m = found->second;
    if (!ph::hard_macro(m)) {
      continue;
    }
    any_macro = true;
    if (m.virtual_proxy) {
      // A synthesized memory proxy has no real abstract: the generator keeps every layer off it.
      for (const auto& [layer, unused] : tech.layers) {
        blocked.insert(layer);
      }
    }
    blocked.insert(m.blocked_layers.begin(), m.blocked_layers.end());
    blocked.insert(m.conservative_obstructions.begin(), m.conservative_obstructions.end());
    for (const auto& shape : m.obstructions) {
      blocked.insert(shape.layer);
    }
    for (const auto& [pin_name, pin] : m.pins) {
      for (const auto& shape : pin.shapes) {
        blocked.insert(shape.layer);
      }
    }
  }
  Pg_macro_rules rules;
  if (!any_macro) {
    return rules;
  }
  // A strategy whose layers the LEF stack does not describe (a partial or foreign technology) states no rule.
  const auto known = [&](const std::string& name) {
    const auto it = tech.layers.find(name);
    return it != tech.layers.end() && (it->second.direction == "VERTICAL" || it->second.direction == "HORIZONTAL")
           && std::find(tech.routing_layers.begin(), tech.routing_layers.end(), name) != tech.routing_layers.end();
  };
  bool described = std::all_of(pg.followpin_layers.begin(), pg.followpin_layers.end(), known)
                   && std::all_of(pg.stripes.begin(), pg.stripes.end(), [&](const auto& s) { return known(s.layer); });
  for (const auto& [lower, upper] : pg.connect) {
    described &= known(lower) && known(upper);
  }
  if (!described) {
    return rules;
  }
  double halo = 0;  // the orientation is not known yet: the largest macro-grid halo
  for (const auto& grid : pg.macro_grids) {
    halo = std::max(halo, grid.halo);
  }
  const auto level = [&](const std::string& name) {
    const auto it = std::find(tech.routing_layers.begin(), tech.routing_layers.end(), name);
    if (it == tech.routing_layers.end()) {
      throw std::invalid_argument("power-grid layer is not a LEF routing layer: " + name);
    }
    return it - tech.routing_layers.begin();
  };
  const auto vertical = [&](const std::string& name) {
    const auto it = tech.layers.find(name);
    if (it == tech.layers.end() || (it->second.direction != "VERTICAL" && it->second.direction != "HORIZONTAL")) {
      throw std::invalid_argument("power-grid layer needs a LEF direction: " + name);
    }
    return it->second.direction == "VERTICAL";
  };
  const auto followpin = [&](const std::string& name) {
    return std::find(pg.followpin_layers.begin(), pg.followpin_layers.end(), name) != pg.followpin_layers.end();
  };
  const auto stripe = [&](const std::string& name) -> const ph::Pg_stripe* {
    const auto it = std::find_if(pg.stripes.begin(), pg.stripes.end(), [&](const auto& s) { return s.layer == name; });
    return it == pg.stripes.end() ? nullptr : &*it;
  };
  // The lowest layer `layer` connects to through the core grid.
  const auto feed_of = [&](const std::string& layer) -> std::optional<std::string> {
    std::optional<std::string> best;
    for (const auto& [lower, upper] : pg.connect) {
      if (lower == layer && (!best || level(upper) < level(*best))) {
        best = upper;
      }
    }
    return best;
  };
  for (int axis = 0; axis < 2; ++axis) {
    // axis 1: gaps along y, horizontal channel, crossed by vertical layers. Rails are horizontal.
    const auto crosses = [&](const std::string& name) { return followpin(name) ? axis == 0 : vertical(name) == (axis == 1); };
    std::vector<std::string> sources(pg.followpin_layers.begin(), pg.followpin_layers.end());
    for (const auto& s : pg.stripes) {
      sources.push_back(s.layer);
    }
    auto& best = rules.gap[axis];
    for (const auto& source : sources) {
      if (!crosses(source) || !blocked.contains(source)) {
        continue;
      }
      bool        rails = followpin(source);
      std::string cur   = source;
      Pg_gap_rule rule;
      for (size_t guard = 0; guard <= tech.routing_layers.size(); ++guard) {
        const auto up = feed_of(cur);
        if (!up) {
          break;  // nothing above: no requirement this strategy can state
        }
        if (followpin(*up) || crosses(*up)) {
          if (!blocked.contains(*up)) {
            break;  // a crossing layer that passes over the macros feeds every segment
          }
          cur = *up;
          continue;
        }
        const auto* spec = stripe(*up);
        if (!spec) {
          throw std::invalid_argument("power-grid connection to a layer without straps: " + *up);
        }
        // Via-stack clearance: the crossing layer and every layer up to (excluding) the feed, at the narrower width.
        const double width   = std::min(spec->width, stripe(cur) ? stripe(cur)->width : spec->width);
        double       spacing = 0;
        for (auto l = level(cur); l < level(*up); ++l) {
          spacing = std::max(spacing, layer_spacing(tech.layers.at(tech.routing_layers[l]), width));
        }
        const double feed_spacing = pg_feeder_spacing(tech.layers.at(*up), *spec);
        rule.source               = source;
        rule.feed                 = *up;
        if (rails || blocked.contains(*up)) {
          // One feeder per net beside a possible existing strap, each kept a spacing from the others and the halos.
          rule.kind      = Pg_gap_rule::Kind::feeder;
          rule.margin    = halo + feed_spacing;
          rule.any_phase = rule.aligned = 2 * rule.margin + 3 * spec->width + 3 * feed_spacing;
        } else {
          rule.kind       = Pg_gap_rule::Kind::lattice;
          rule.margin     = halo + spacing;
          rule.period     = spec->pitch;
          // Two nets per pitch (the generator's model): centers at offset and offset + width + spacing.
          const double c0 = spec->offset, c1 = spec->offset + spec->width + spec->spacing, w2 = spec->width / 2;
          rule.groups = {{{c0 - w2, c1 + w2}}, {{c1 - w2, c0 + spec->pitch + w2}}};
          double span = std::numeric_limits<double>::infinity();
          for (const auto& g : rule.groups) {
            span = std::min(span, g[1] - g[0]);
          }
          rule.aligned   = span + 2 * rule.margin;
          rule.any_phase = spec->pitch + spec->width + 2 * rule.margin;
        }
        break;
      }
      const auto strength = [](const Pg_gap_rule& r) { return std::pair{r.kind == Pg_gap_rule::Kind::lattice, r.any_phase}; };
      if (rule.kind != Pg_gap_rule::Kind::none && strength(rule) > strength(best)) {
        best = rule;
      }
    }
  }
  return rules;
}

bool Pg_edge_lattice::fits(int64_t lo, int64_t hi) const {
  if (period <= 0) {
    return true;
  }
  for (const auto& g : groups) {
    // Highest translate n with g[1] + n * period <= hi - origin, then check its low edge.
    const auto top = hi - origin - g[1];
    const auto n   = top >= 0 ? top / period : -((-top + period - 1) / period);
    if (origin + g[0] + n * period >= lo) {
      return true;
    }
  }
  return false;
}
bool Pg_edge_lattice::allows(int64_t low_edge, int64_t high_edge, int64_t core_lo, int64_t core_hi) const {
  if (period <= 0) {
    return true;
  }
  const auto below_hi = low_edge - margin;
  const auto below_lo = std::max(core_lo, low_edge - channel + margin);
  if (below_hi > core_lo && !fits(below_lo, below_hi)) {
    return false;
  }
  if (core_hi - high_edge < channel) {
    const auto above_lo = high_edge + margin;
    if (above_lo < core_hi && !fits(above_lo, core_hi)) {
      return false;
    }
  }
  return true;
}
Pg_macro_alignment pg_macro_alignment(const Pg_macro_rules& rules, std::array<int64_t, 2> channel, std::array<int64_t, 2> origin,
                                      int64_t dbu, int64_t tolerance) {
  Pg_macro_alignment result;
  for (int axis = 0; axis < 2; ++axis) {
    const auto& rule = rules.gap[axis];
    if (rule.kind != Pg_gap_rule::Kind::lattice) {
      continue;
    }
    // Outward rounding keeps the integer test at least as strict as the micron rule.
    Pg_edge_lattice lattice;
    lattice.origin  = origin[axis];
    lattice.period  = std::llround(rule.period * dbu);
    lattice.channel = channel[axis];
    lattice.margin  = static_cast<int64_t>(std::ceil(rule.margin * dbu - 1e-6)) + std::max<int64_t>(0, tolerance);
    if (lattice.period <= 0 || std::abs(double(lattice.period) - rule.period * dbu) > 1e-6) {
      throw std::invalid_argument("power-grid strap pitch is not an integral DBU");
    }
    for (const auto& g : rule.groups) {
      auto       lo    = static_cast<int64_t>(std::floor(g[0] * dbu + 1e-6));
      auto       hi    = static_cast<int64_t>(std::ceil(g[1] * dbu - 1e-6));
      // Normalize the low edge into [0, period).
      const auto shift = (lo % lattice.period + lattice.period) % lattice.period - lo;
      lattice.groups.push_back({lo + shift, hi + shift});
    }
    result.axis[axis] = std::move(lattice);
  }
  return result;
}
std::map<std::string, uint64_t> pg_unfed_strap_segments(const Power_grid_plan& plan, const ph::Pg_strategy& pg) {
  std::map<std::string, uint64_t> result;
  for (const auto& a : plan.metal) {
    if (a.followpin || a.optional) {
      continue;
    }
    bool has_upper = false, fed = false;
    for (const auto& [lower, upper] : pg.connect) {
      if (lower != a.layer) {
        continue;
      }
      has_upper = true;
      fed       = fed || std::any_of(plan.metal.begin(), plan.metal.end(), [&](const auto& b) {
              return b.layer == upper && b.net == a.net && b.rect.overlaps(a.rect);
            });
    }
    if (has_upper && !fed) {
      ++result[a.layer];
    }
  }
  return result;
}
const char* pg_macro_level_name(Pg_macro_level level) {
  switch (level) {
    case Pg_macro_level::gap  : return "gap";
    case Pg_macro_level::align: return "align";
    case Pg_macro_level::off  : return "off";
  }
  return "off";
}
double pg_macro_requirement(const Pg_gap_rule& rule, Pg_macro_level level) {
  if (level == Pg_macro_level::off) {
    return 0;
  }
  switch (rule.kind) {
    case Pg_gap_rule::Kind::none   : return 0;
    case Pg_gap_rule::Kind::feeder : return rule.any_phase;
    case Pg_gap_rule::Kind::lattice: return level == Pg_macro_level::align ? rule.aligned + rule.period / 10 : rule.any_phase;
  }
  return 0;
}
bool pg_macro_boxes_fit(const std::vector<Pg_macro_box>& boxes, int64_t core_w, int64_t core_h) {
  if (boxes.empty()) {
    return true;
  }
  if (core_w <= 0 || core_h <= 0) {
    return false;
  }
  double  area = 0, sum_w = 0, sum_h = 0;
  int64_t min_w = std::numeric_limits<int64_t>::max(), min_h = std::numeric_limits<int64_t>::max();
  for (const auto& b : boxes) {
    if (b.width <= 0 || b.height <= 0) {
      continue;
    }
    if (b.width > core_w || b.height > core_h) {
      return false;
    }
    area  += double(b.width) * double(b.height);
    sum_w += double(b.width);
    sum_h += double(b.height);
    min_w  = std::min(min_w, b.width);
    min_h  = std::min(min_h, b.height);
  }
  if (min_w == std::numeric_limits<int64_t>::max()) {
    return true;
  }
  return area <= double(core_w) * double(core_h) && sum_w <= double(core_w) * double(core_h / min_h)
         && sum_h <= double(core_h) * double(core_w / min_w);
}
const ph::Pg_strategy* pg_macro_strategy(const ph::Technology& tech, std::string_view pg) {
  // pg=auto enables the native grid with the LEF-detected profile: the same strategy as no pg.
  if (pg != "none" && pg != "off" && pg != "auto") {
    const auto& profile = ph::tech_profile(pg);
    return profile.provides(ph::Tech_policy::power_grid) ? &profile.pg : nullptr;
  }
  const auto* profile = ph::detect_tech_profile(tech, ph::Tech_policy::power_grid);
  return profile ? &profile->pg : nullptr;
}
}  // namespace livehd::placement
