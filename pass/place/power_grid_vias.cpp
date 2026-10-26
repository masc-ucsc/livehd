// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "power_grid_vias.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "inou/lefdef/cut_rules.hpp"

namespace livehd::placement {
namespace {
namespace ph = physical;
using ph::cut_classes;
using ph::cut_enclosures;
using ph::cut_table_spacing;
using ph::exact_units;
struct Candidate {
  std::string                          name, cut, rule;
  std::array<int64_t, 2>               size{}, pitch{};
  std::array<int64_t, 4>               enc{};
  std::optional<std::array<double, 2>> lower_width, upper_width;
  bool                                 generated = false;
};
std::array<int64_t, 2> enclosure(const ph::Via_rule_layer& layer, const ph::Layer& metal, int64_t dbu) {
  const auto e = layer.enclosure.value_or(std::array<double, 2>{0, 0});
  auto       x = exact_units(e[0], dbu), y = exact_units(e[1], dbu);
  if ((metal.direction == "VERTICAL" && x > y) || (metal.direction == "HORIZONTAL" && x < y)) {
    std::swap(x, y);
  }
  return {x, y};
}
std::optional<Pg_via_array> build(Candidate c, const ph::Technology& tech, int64_t dbu, const Pg_via_request& req,
                                  bool apply_enclosures = true) {
  // Followpins can run against the LEF preferred direction (ASAP7 M1).
  // Extend enclosures across the actual strap width, not its nominal direction.
  const bool lower_vertical = req.lower_size[0] < req.lower_size[1];
  const bool upper_vertical = req.upper_size[0] < req.upper_size[1];
  const auto width_matches  = [&](const auto& rule, int64_t width) {
    return !rule || (double(width) / dbu >= (*rule)[0] - 1e-12 && double(width) / dbu <= (*rule)[1] + 1e-12);
  };
  if (!width_matches(c.lower_width, req.lower_size[lower_vertical ? 0 : 1])
      || !width_matches(c.upper_width, req.upper_size[upper_vertical ? 0 : 1])) {
    return {};
  }
  if (apply_enclosures) {
    // Enclosure alternatives reuse this validated candidate. Re-parsing the
    // LEF58 tables for each recursive alternative dominated the macro probe.
    const auto& cut     = tech.cut_layers.at(c.cut);
    const auto  classes = cut_classes(cut, dbu);
    auto        table   = cut_table_spacing(cut, dbu, classes);
    int64_t     spacing = 0;
    for (double s : cut.spacing) {
      spacing = std::max(spacing, exact_units(s, dbu));
    }
    for (const auto& feature : cut.unsupported) {
      if (feature != "PROPERTY LEF58_CUTCLASS" && feature != "PROPERTY LEF58_SPACINGTABLE"
          && feature != "PROPERTY LEF58_ENCLOSURE") {
        throw std::invalid_argument("unsupported PG cut layer rule: " + feature);
      }
    }
    if (spacing) {
      c.pitch = {c.size[0] + spacing, c.size[1] + spacing};
    }
    if (table) {
      const bool matched = std::any_of(classes.begin(), classes.end(), [&](const auto& item) { return item.second == c.size; });
      if (!matched) {
        return {};
      }
      c.pitch = {c.size[0] + *table, c.size[1] + *table};
    }
    if (c.pitch[0] < c.size[0] || c.pitch[1] < c.size[1] || c.pitch[0] == 0 || c.pitch[1] == 0) {
      throw std::invalid_argument("PG via has no supported cut pitch");
    }
    const auto                  lower = cut_enclosures(cut, dbu, classes, c.size, false, tech.layers.at(req.lower), req.lower_size, ph::Cut_enclosure_policy::pdngen);
    const auto                  upper = cut_enclosures(cut, dbu, classes, c.size, true, tech.layers.at(req.upper), req.upper_size, ph::Cut_enclosure_policy::pdngen);
    std::optional<Pg_via_array> best;
    for (const auto& lo : lower) {
      for (const auto& hi : upper) {
        auto variant = c;
        for (int k = 0; k < 2; ++k) {
          variant.enc[k]     = std::max(c.enc[k], lo[k]);
          variant.enc[k + 2] = std::max(c.enc[k + 2], hi[k]);
        }
        auto array = build(variant, tech, dbu, req, false);
        if (array && (!best || array->rows * array->columns > best->rows * best->columns)) {
          best = std::move(array);
        }
      }
    }
    return best;
  }
  std::array<int64_t, 2> count{};
  for (int axis = 0; axis < 2; ++axis) {
    auto available = std::min(req.lower_size[axis] - 2 * c.enc[axis], req.upper_size[axis] - 2 * c.enc[axis + 2]);
    if (req.lower_fixed || req.upper_fixed) {
      // At a pin contact, keep both via enclosures inside the crossing window.
      // This also avoids an overhanging upper pad at the end of a fixed pin.
      available
          = std::min(available, std::min(req.lower_size[axis], req.upper_size[axis]) - 2 * std::max(c.enc[axis], c.enc[axis + 2]));
    }
    if (available < c.size[axis]) {
      return {};
    }
    count[axis] = 1 + (available - c.size[axis]) / c.pitch[axis];
  }
  if (count[0] > 1000000 || count[1] > 1000000 / count[0]) {
    throw std::invalid_argument("oversized PG via array");
  }
  std::array<int64_t, 2> span{(count[0] - 1) * c.pitch[0] + c.size[0], (count[1] - 1) * c.pitch[1] + c.size[1]};
  if (span[0] % 2 || span[1] % 2) {
    throw std::invalid_argument("PG via array requires even dimensions");
  }
  for (int metal = 0; metal < 2; ++metal) {
    const bool  internal = metal == 0 ? req.lower_internal : req.upper_internal;
    const auto& size     = metal == 0 ? req.lower_size : req.upper_size;
    const int   axis     = (metal == 0 ? lower_vertical : upper_vertical) ? 0 : 1;
    if (!internal && !(metal == 0 ? req.lower_fixed : req.upper_fixed)) {
      c.enc[2 * metal + axis] = std::max(c.enc[2 * metal + axis], (size[axis] - span[axis]) / 2);
    }
  }
  Pg_via_array out;
  out.layers   = {req.lower, c.cut, req.upper};
  out.rows     = count[1];
  out.columns  = count[0];
  out.via.name = "LHD_PG_" + c.name + "_" + std::to_string(count[1]) + "_" + std::to_string(count[0]);
  for (auto v : c.pitch) {
    out.via.name += "_" + std::to_string(v);
  }
  for (auto v : c.enc) {
    out.via.name += "_" + std::to_string(v);
  }
  if (c.generated) {
    ph::Generated_via g;
    g.rule            = c.rule;
    g.layers          = out.layers;
    g.cut_size        = c.size;
    g.cut_spacing     = {c.pitch[0] - c.size[0], c.pitch[1] - c.size[1]};
    g.row_col         = std::array{count[1], count[0]};
    g.enclosure       = c.enc;
    out.via.generated = g;
  } else {
    for (int metal = 0; metal < 2; ++metal) {
      out.via.shapes.push_back({out.layers[metal * 2],
                                {double(-span[0] / 2 - c.enc[2 * metal]),
                                 double(-span[1] / 2 - c.enc[2 * metal + 1]),
                                 double(span[0] / 2 + c.enc[2 * metal]),
                                 double(span[1] / 2 + c.enc[2 * metal + 1])}});
    }
    for (int64_t row = 0; row < count[1]; ++row) {
      for (int64_t col = 0; col < count[0]; ++col) {
        const auto x = -span[0] / 2 + col * c.pitch[0], y = -span[1] / 2 + row * c.pitch[1];
        out.via.shapes.push_back({c.cut, {double(x), double(y), double(x + c.size[0]), double(y + c.size[1])}});
      }
    }
  }
  return out;
}
}  // namespace
Pg_via_array make_pg_via(const ph::Technology& tech, int64_t dbu, const Pg_via_request& req) {
  if (dbu <= 0 || !tech.layers.contains(req.lower) || !tech.layers.contains(req.upper) || req.lower == req.upper) {
    throw std::invalid_argument("invalid PG via layer pair/DBU");
  }
  for (auto size : {req.lower_size, req.upper_size}) {
    for (auto v : size) {
      if (v <= 0 || v > std::numeric_limits<int32_t>::max()) {
        throw std::invalid_argument("invalid PG via available metal extent");
      }
    }
  }
  std::optional<Pg_via_array> best;
  long double                 best_area = -1;
  const auto                  consider  = [&](const Candidate& c) {
    auto candidate = build(c, tech, dbu, req);
    if (candidate) {
      const long double area = static_cast<long double>(candidate->rows) * candidate->columns * c.size[0] * c.size[1];
      if (area > best_area) {
        best_area = area;
        best      = std::move(candidate);
      }
    }
  };
  for (const auto& [name, rule] : tech.via_rules) {
    if (!rule.generate || !rule.unsupported.empty() || rule.layers.size() != 3) {
      continue;
    }
    const ph::Via_rule_layer *lower = nullptr, *upper = nullptr, *cut = nullptr;
    for (const auto& layer : rule.layers) {
      if (layer.name == req.lower) {
        lower = &layer;
      } else if (layer.name == req.upper) {
        upper = &layer;
      } else if (tech.cut_layers.contains(layer.name)) {
        cut = &layer;
      }
    }
    if (!lower || !upper || !cut || !cut->cut || !cut->spacing) {
      continue;
    }
    Candidate c;
    c.name       = name;
    c.rule       = name;
    c.cut        = cut->name;
    c.generated  = true;
    c.size       = {exact_units(cut->cut->xh - cut->cut->xl, dbu), exact_units(cut->cut->yh - cut->cut->yl, dbu)};
    c.pitch      = {exact_units((*cut->spacing)[0], dbu), exact_units((*cut->spacing)[1], dbu)};
    const auto a = enclosure(*lower, tech.layers.at(req.lower), dbu), b = enclosure(*upper, tech.layers.at(req.upper), dbu);
    c.enc         = {a[0], a[1], b[0], b[1]};
    c.lower_width = lower->width;
    c.upper_width = upper->width;
    consider(c);
  }
  if (best) {
    return *best;
  }
  for (const auto& [name, via] : tech.vias) {
    if (!via.unsupported.empty() || via.shapes.size() != 3) {
      continue;
    }
    const ph::Shape *lower = nullptr, *upper = nullptr, *cut = nullptr;
    for (const auto& shape : via.shapes) {
      if (shape.layer == req.lower) {
        lower = &shape;
      } else if (shape.layer == req.upper) {
        upper = &shape;
      } else if (tech.cut_layers.contains(shape.layer)) {
        cut = &shape;
      }
    }
    if (!lower || !upper || !cut) {
      continue;
    }
    Candidate c;
    c.name = name;
    c.cut  = cut->layer;
    c.size = {exact_units(cut->rect.xh - cut->rect.xl, dbu), exact_units(cut->rect.yh - cut->rect.yl, dbu)};
    if (std::abs(cut->rect.xh + cut->rect.xl) > 1e-12 || std::abs(cut->rect.yh + cut->rect.yl) > 1e-12) {
      throw std::invalid_argument("off-center PG fixed via is unsupported");
    }
    int metal = 0;
    for (const auto* shape : {lower, upper}) {
      const auto& r        = shape->rect;
      c.enc[2 * metal]     = std::max(exact_units(cut->rect.xl - r.xl, dbu), exact_units(r.xh - cut->rect.xh, dbu));
      c.enc[2 * metal + 1] = std::max(exact_units(cut->rect.yl - r.yl, dbu), exact_units(r.yh - cut->rect.yh, dbu));
      ++metal;
    }
    consider(c);
  }
  if (!best) {
    throw Pg_via_no_fit("no supported PG via fits between " + req.lower + " and " + req.upper + " (available DBU "
                        + std::to_string(req.lower_size[0]) + "x" + std::to_string(req.lower_size[1]) + ", "
                        + std::to_string(req.upper_size[0]) + "x" + std::to_string(req.upper_size[1]) + ")");
  }
  return *best;
}
}  // namespace livehd::placement
