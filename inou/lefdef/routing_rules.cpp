// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "routing_rules.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "cut_rules.hpp"

namespace livehd::physical {
namespace {
Rule_check reject(std::string reason) { return {false, std::move(reason)}; }
Rule_check supported(const Technology& tech, int64_t dbu, const Shape& shape) {
  if (dbu <= 0 || dbu > std::numeric_limits<int32_t>::max() || !integral_dbu(shape.rect) || shape.rect.empty()) {
    return reject("invalid metal rectangle or DBU");
  }
  const auto it = tech.layers.find(shape.layer);
  if (it == tech.layers.end() || !it->second.rules.loaded) {
    return reject("missing metal rule model: " + shape.layer);
  }
  const auto& rules = it->second.rules;
  if (!rules.unsupported.empty()) {
    return reject("unsupported metal rule: " + *rules.unsupported.begin());
  }
  if (!rules.properties.empty()) {
    return reject("unsupported metal property: " + rules.properties.begin()->first);
  }
  // A minimum-step rule is about the contour of the merged conductor; a pair
  // of rectangles cannot establish whether neighbouring same-net edges merge.
  if (!rules.min_step.empty()) {
    return reject("minimum-step contour check required");
  }
  return {true, {}};
}
double width(const Rect& r) { return std::min(r.xh - r.xl, r.yh - r.yl); }
bool   number(double x) { return std::isfinite(x) && x >= 0; }
}  // namespace
Rule_check check_metal_rectangle(const Technology& tech, int64_t dbu, const Shape& shape) {
  auto result = supported(tech, dbu, shape);
  if (!result.legal) {
    return result;
  }
  const auto& layer   = tech.layers.at(shape.layer);
  const auto& r       = layer.rules;
  const auto  w       = width(shape.rect);
  const auto  h       = std::max(shape.rect.xh - shape.rect.xl, shape.rect.yh - shape.rect.yl);
  const auto  minimum = r.min_width.value_or(layer.width);
  if (!number(minimum) || !number(r.area) || (r.max_width && !number(*r.max_width))) {
    return reject("invalid metal width/area rule");
  }
  if (w < rule_dbu(minimum, dbu)) {
    return reject("minimum width");
  }
  if (r.max_width && w > std::floor(*r.max_width * dbu + 1e-9)) {
    return reject("maximum width");
  }
  if (static_cast<long double>(w) * h + 1e-9L < static_cast<long double>(r.area) * dbu * dbu) {
    return reject("minimum area");
  }
  if (!r.min_size.empty()) {
    bool fits = false;
    for (const auto& size : r.min_size) {
      if (!number(size[0]) || !number(size[1])) {
        return reject("invalid minimum size rule");
      }
      fits |= w >= rule_dbu(std::min(size[0], size[1]), dbu) && h >= rule_dbu(std::max(size[0], size[1]), dbu);
    }
    if (!fits) {
      return reject("minimum size");
    }
  }
  if (!number(tech.manufacturing_grid)) {
    return reject("invalid manufacturing grid");
  }
  const auto grid = manufacturing_grid_dbu(tech, dbu);
  if (!grid) {
    return reject("unrepresentable manufacturing grid");
  }
  for (auto v : {shape.rect.xl, shape.rect.yl, shape.rect.xh, shape.rect.yh}) {
    if (std::fmod(v, double(*grid)) != 0) {
      return reject("off manufacturing grid");
    }
  }
  return {true, {}};
}
Rule_check check_metal_spacing(const Technology& tech, int64_t dbu, const Shape& a, const Shape& b) {
  if (a.layer != b.layer) {
    return reject("spacing check requires the same metal layer");
  }
  for (const auto* shape : {&a, &b}) {
    const auto result = supported(tech, dbu, *shape);
    if (!result.legal) {
      return result;
    }
  }
  const auto& r  = tech.layers.at(a.layer).rules;
  const auto  dx = std::max({a.rect.xl - b.rect.xh, b.rect.xl - a.rect.xh, 0.});
  const auto  dy = std::max({a.rect.yl - b.rect.yh, b.rect.yl - a.rect.yh, 0.});
  if (dx == 0 && dy == 0) {
    return reject("different-net metal touches or overlaps");
  }
  const auto max_width = std::max(width(a.rect), width(b.rect));
  const auto run       = std::max(std::min(a.rect.xh, b.rect.xh) - std::max(a.rect.xl, b.rect.xl),
                            std::min(a.rect.yh, b.rect.yh) - std::max(a.rect.yl, b.rect.yl));
  double     required  = 0;
  for (const auto& rule : r.spacing) {
    if (!number(rule.distance)) {
      return reject("invalid spacing rule");
    }
    if (rule.range) {
      const auto& range = *rule.range;
      if (!number(range[0]) || !number(range[1]) || range[0] > range[1]) {
        return reject("invalid spacing range");
      }
      const auto matches
          = [&](const Rect& rect) { return width(rect) >= rule_dbu(range[0], dbu) && width(rect) <= range[1] * dbu + 1e-9; };
      if (!matches(a.rect) && !matches(b.rect)) {
        continue;
      }
    }
    // Applying EOL spacing to the whole pair is a conservative superset of
    // its conditional keepout. Exact directional EOL checks can relax this.
    required = std::max(required, rule_dbu(rule.distance, dbu));
    if (rule.eol) {
      if (!number(rule.eol->width) || !number(rule.eol->within)) {
        return reject("invalid EOL rule");
      }
      required = std::max(required, rule_dbu(rule.eol->within, dbu));
      if (rule.eol->parallel_edge) {
        for (auto distance : *rule.eol->parallel_edge) {
          if (!number(distance)) {
            return reject("invalid EOL parallel-edge rule");
          }
          required = std::max(required, rule_dbu(distance, dbu));
        }
      }
    }
  }
  for (const auto& table : r.parallel) {
    if (table.widths.empty() || table.lengths.empty() || table.spacing.size() != table.widths.size()
        || !std::is_sorted(table.widths.begin(), table.widths.end())
        || !std::is_sorted(table.lengths.begin(), table.lengths.end())) {
      return reject("invalid parallel-run spacing table");
    }
    for (size_t i = 0; i < table.widths.size(); ++i) {
      if (!number(table.widths[i]) || table.spacing[i].size() != table.lengths.size()) {
        return reject("invalid parallel-run spacing row");
      }
      for (size_t j = 0; j < table.lengths.size(); ++j) {
        if (!number(table.lengths[j]) || !number(table.spacing[i][j])) {
          return reject("invalid parallel-run spacing value");
        }
        // Include the first entry even below its threshold; higher entries
        // apply at their threshold (conservative at strict LEF boundaries).
        if ((i == 0 || max_width >= rule_dbu(table.widths[i], dbu)) && (j == 0 || run >= rule_dbu(table.lengths[j], dbu))) {
          required = std::max(required, rule_dbu(table.spacing[i][j], dbu));
        }
      }
    }
  }
  if (r.spacing.empty() && r.parallel.empty()) {
    return reject("missing metal spacing rule");
  }
  // A square keepout is conservative relative to Euclidean corner distance.
  if (dx < required && dy < required) {
    return reject("metal spacing");
  }
  return {true, {}};
}

namespace {
// Strict token reader: unknown qualifiers make the entire layer ineligible.
struct Rect_edge {
  bool   vertical, positive;
  double coordinate, lo, hi;
};
std::array<Rect_edge, 4> edges(const Rect& r) {
  return {{{true, false, r.xl, r.yl, r.yh},
           {true, true, r.xh, r.yl, r.yh},
           {false, false, r.yl, r.xl, r.xh},
           {false, true, r.yh, r.xl, r.xh}}};
}
Rect edge_region(const Rect_edge& e, double backward, double forward, double side) {
  const auto lo = e.coordinate - (e.positive ? backward : forward);
  const auto hi = e.coordinate + (e.positive ? forward : backward);
  return e.vertical ? Rect{lo, e.lo - side, hi, e.hi + side} : Rect{e.lo - side, lo, e.hi + side, hi};
}
bool corner_inside(const Rect& bounds, const Rect& shape) {
  for (auto x : {shape.xl, shape.xh}) {
    for (auto y : {shape.yl, shape.yh}) {
      if (x > bounds.xl && x < bounds.xh && y > bounds.yl && y < bounds.yh) {
        return true;
      }
    }
  }
  return false;
}
class Property_tokens {
  std::vector<std::string> words_;
  size_t                   at_ = 0;

public:
  explicit Property_tokens(const std::string& text) : words_(lef58_tokens(text)) {}
  bool take(const std::string& word) {
    if (at_ < words_.size() && words_[at_] == word) {
      ++at_;
      return true;
    }
    return false;
  }
  void need(const std::string& word) {
    if (!take(word)) {
      throw std::invalid_argument("expected " + word);
    }
  }
  double value() {
    if (at_ == words_.size()) {
      throw std::invalid_argument("missing numeric value");
    }
    const auto& word  = words_[at_++];
    size_t      used  = 0;
    const auto  value = std::stod(word, &used);
    if (used != word.size() || !number(value)) {
      throw std::invalid_argument("invalid numeric value");
    }
    return value;
  }
  bool end() const { return at_ == words_.size(); }
  void finish() {
    need(";");
    if (!end()) {
      throw std::invalid_argument("unexpected rule suffix");
    }
  }
};
}  // namespace
Signal_metal_rules::Signal_metal_rules(const Technology& tech, const Floorplan& fp) : dbu_(fp.dbu) {
  rules_.manufacturing_grid = tech.manufacturing_grid;
  rules_.layers             = tech.layers;
  for (auto& [name, layer] : rules_.layers) {
    auto& extension = extensions_[name];
    for (const auto& track : fp.tracks) {
      if (track.layer == name) {
        extension.tracks.push_back(track);
      }
    }
    // Keep raw extension text and unsupported flags in the imported technology;
    // only this compiled view removes properties it has fully recognized.
    for (auto it = layer.rules.properties.begin(); it != layer.rules.properties.end();) {
      const auto& key = it->first;
      try {
        if (it->second.empty()) {
          throw std::invalid_argument("empty property");
        }
        for (const auto& text : it->second) {
          Property_tokens t(text);
          double          keepout = 0;
          if (key == "LEF58_SPACING") {
            t.need("SPACING");
            const auto spacing = t.value();
            t.need("ENDOFLINE");
            const auto eol_width = t.value();
            t.need("WITHIN");
            const auto within = t.value();
            t.need("ENDTOEND");
            const auto end_to_end = t.value();
            extension.eol.push_back({eol_width, spacing, within, end_to_end});
            keepout = std::max({spacing, eol_width, within, end_to_end});
            if (t.take("PARALLELEDGE")) {
              keepout = std::max(keepout, t.value());
              t.need("WITHIN");
              keepout = std::max(keepout, t.value());
            }
            t.finish();
          } else if (key == "LEF58_EOLKEEPOUT") {
            t.need("EOLKEEPOUT");
            const auto eol_width = t.value();
            t.need("EXTENSION");
            // LEF order is backward, side, forward (not x/y or wire direction).
            const auto backward = t.value(), side = t.value(), forward = t.value();
            const bool corner_only = t.take("CORNERONLY");
            extension.keepouts.push_back({eol_width, backward, side, forward, corner_only});
            keepout = eol_width + backward + side + forward;
            t.finish();
          } else if (key == "LEF58_CORNERSPACING") {
            t.need("CORNERSPACING");
            t.need("CONVEXCORNER");
            if (t.take("CORNERONLY")) {
              keepout = t.value();
            }
            t.need("WIDTH");
            t.value();
            t.need("SPACING");
            const auto spacing = t.value();
            extension.corner_spacing.push_back(spacing);
            keepout = std::max(keepout, spacing);
            t.finish();
          } else if (key == "LEF58_WIDTHTABLE") {
            t.need("WIDTHTABLE");
            std::vector<double> widths;
            while (!t.take(";")) {
              const auto w = t.value();
              if (w <= 0 || (!widths.empty() && w <= widths.back())) {
                throw std::invalid_argument("unordered width table");
              }
              widths.push_back(w);
            }
            if (widths.empty() || !t.end() || (!extension.widths.empty() && extension.widths != widths)) {
              throw std::invalid_argument("invalid or conflicting width table");
            }
            extension.widths = std::move(widths);
          } else if (key == "LEF58_RIGHTWAYONGRIDONLY") {
            t.need("RIGHTWAYONGRIDONLY");
            t.finish();
            extension.right_way = extension.explicit_tracks = true;
          } else if (key == "LEF58_RECTONLY") {
            t.need("RECTONLY");
            t.finish();
          } else if (key == "LEF58_PITCH") {
            t.need("PITCH");
            const auto pitch = t.value();
            t.need("FIRSTLASTPITCH");
            if (pitch <= 0 || t.value() < pitch) {
              throw std::invalid_argument("invalid first/last pitch");
            }
            t.finish();
            // Actual generated tracks carry the row-boundary pattern; never
            // replace them with an inferred uniform grid from LEF PITCH.
            extension.explicit_tracks = true;
          } else {
            throw std::invalid_argument("unmodeled property");
          }
          if (!number(keepout)) {
            throw std::invalid_argument("unrepresentable keepout");
          }
          extension.query_radius = std::max(extension.query_radius, keepout);
        }
      } catch (const std::exception& error) {
        extension.error = key + ": " + error.what();
        break;
      }
      layer.rules.unsupported.erase("PROPERTY " + key);
      it = layer.rules.properties.erase(it);
    }
  }
}
std::vector<int64_t> Signal_metal_rules::candidate_widths(const std::string& name) const {
  const auto it = extensions_.find(name);
  if (it == extensions_.end() || !it->second.error.empty()) {
    return {};
  }
  auto widths = it->second.widths;
  if (widths.empty()) {
    widths.push_back(rules_.layers.at(name).width);
  }
  std::vector<int64_t> result;
  for (const auto w : widths) {
    const auto value = w * dbu_;
    const auto dbu   = rounded_dbu(value, 1e-9);
    if (!dbu || value <= 0) {
      return {};
    }
    result.push_back(*dbu);
  }
  return result;
}
Rule_check Signal_metal_rules::rectangle(const Shape& shape) const {
  const auto it = extensions_.find(shape.layer);
  if (it == extensions_.end()) {
    return reject("unknown metal layer");
  }
  const auto& extension = it->second;
  if (!extension.error.empty()) {
    return reject(extension.error);
  }
  auto result = check_metal_rectangle(rules_, dbu_, shape);
  if (!result.legal) {
    return result;
  }
  if (!extension.widths.empty() && std::none_of(extension.widths.begin(), extension.widths.end(), [&](double w) {
        return std::abs(w * dbu_ - width(shape.rect)) < 1e-9;
      })) {
    return reject("LEF58 width table");
  }
  const auto& direction  = rules_.layers.at(shape.layer).direction;
  const bool  horizontal = direction == "HORIZONTAL";
  if ((extension.right_way || extension.explicit_tracks) && !horizontal && direction != "VERTICAL") {
    return reject("missing preferred routing direction");
  }
  if (extension.right_way
      && ((horizontal && shape.rect.xh - shape.rect.xl < shape.rect.yh - shape.rect.yl)
          || (!horizontal && shape.rect.yh - shape.rect.yl < shape.rect.xh - shape.rect.xl))) {
    return reject("LEF58 right-way routing required");
  }
  if (extension.explicit_tracks) {
    const auto axis         = horizontal ? "Y" : "X";
    const auto center_twice = std::llround(horizontal ? shape.rect.yl + shape.rect.yh : shape.rect.xl + shape.rect.xh);
    bool       on_track     = false;
    for (const auto& track : extension.tracks) {
      if (track.axis != "X" && track.axis != "Y") {
        return reject("invalid track axis");
      }
      if (track.axis != axis) {
        continue;
      }
      if (!track.supported()) {
        return reject("invalid or masked routing tracks");
      }
      const auto delta   = center_twice - 2 * track.start;
      const auto period  = 2 * track.step;
      on_track          |= delta >= 0 && delta % period == 0 && delta / period < track.count;
    }
    if (!on_track) {
      return reject("not on an explicit preferred-direction track");
    }
  }
  return {true, {}};
}
Rule_check Signal_metal_rules::spacing(const Shape& a, const Shape& b) const {
  for (const auto* shape : {&a, &b}) {
    const auto it = extensions_.find(shape->layer);
    if (it == extensions_.end()) {
      return reject("unknown metal layer");
    }
    if (!it->second.error.empty()) {
      return reject(it->second.error);
    }
  }
  const auto ordinary = check_metal_spacing(rules_, dbu_, a, b);
  if (!ordinary.legal) {
    return ordinary;
  }
  const auto& extension = extensions_.at(a.layer);
  const auto  directed  = [&](const Rect& source, const Rect& target) -> Rule_check {
    const auto source_edges = edges(source), target_edges = edges(target);
    for (const auto& rule : extension.eol) {
      for (const auto& edge : source_edges) {
        if (edge.hi - edge.lo >= rule_dbu(rule.width, dbu_)) {
          continue;
        }
        for (const auto& other : target_edges) {
          if (edge.vertical != other.vertical || edge.positive == other.positive) {
            continue;
          }
          const auto distance = (other.coordinate - edge.coordinate) * (edge.positive ? 1 : -1);
          if (distance < 0) {
            continue;
          }
          const bool both_eol = other.hi - other.lo < rule_dbu(rule.width, dbu_);
          const auto within   = both_eol ? 0. : rule_dbu(rule.within, dbu_);
          if (std::min(edge.hi + within, other.hi) <= std::max(edge.lo - within, other.lo)) {
            continue;
          }
          if (distance < rule_dbu(both_eol ? rule.end_to_end : rule.spacing, dbu_)) {
            return reject("LEF58 facing EOL spacing");
          }
        }
      }
    }
    for (const auto& rule : extension.keepouts) {
      for (const auto& edge : source_edges) {
        if (edge.hi - edge.lo >= rule_dbu(rule.width, dbu_)) {
          continue;
        }
        const auto region = edge_region(edge, rule_dbu(rule.backward, dbu_), rule_dbu(rule.forward, dbu_), rule_dbu(rule.side, dbu_));
        if (rule.corner_only ? corner_inside(region, target) : region.overlaps(target)) {
          return reject("LEF58 EOL keepout");
        }
      }
    }
    return {true, {}};
  };
  // Evaluate lazily: an initializer list runs both directional checks even
  // when the first already rejects this candidate.
  auto check = directed(a.rect, b.rect);
  if (!check.legal) {
    return check;
  }
  check = directed(b.rect, a.rect);
  if (!check.legal) {
    return check;
  }
  // Only diagonally facing convex corners qualify. Side-by-side rectangles
  // whose projections overlap are governed by spacing/PRL, not corner rules.
  if ((a.rect.xh <= b.rect.xl || b.rect.xh <= a.rect.xl) && (a.rect.yh <= b.rect.yl || b.rect.yh <= a.rect.yl)) {
    const auto dx = std::max(a.rect.xl - b.rect.xh, b.rect.xl - a.rect.xh);
    const auto dy = std::max(a.rect.yl - b.rect.yh, b.rect.yl - a.rect.yh);
    for (auto spacing : extension.corner_spacing) {
      // The supported one-row WIDTH table is conservatively applied to all
      // widths; CORNERTOCORNER/Euclidean and multi-row forms remain rejected.
      if (dx < rule_dbu(spacing, dbu_) && dy < rule_dbu(spacing, dbu_)) {
        return reject("LEF58 convex corner spacing");
      }
    }
  }
  return {true, {}};
}
std::optional<int64_t> Signal_metal_rules::clearance(const std::string& name) const {
  const auto extension = extensions_.find(name);
  if (extension == extensions_.end() || !extension->second.error.empty() || !supported(rules_, dbu_, {name, {0, 0, 1, 1}}).legal) {
    return {};
  }
  const auto& rules = rules_.layers.at(name).rules;
  double      bound = 0;
  bool        valid = true;
  const auto  add   = [&](double v) {
    if (!number(v) || v * dbu_ > std::numeric_limits<int32_t>::max()) {
      valid = false;
    } else {
      bound = std::max(bound, rule_dbu(v, dbu_));
    }
  };
  add(extension->second.query_radius);
  for (const auto& spacing : rules.spacing) {
    add(spacing.distance);
    if (spacing.range
        && (!number((*spacing.range)[0]) || !number((*spacing.range)[1]) || (*spacing.range)[0] > (*spacing.range)[1])) {
      valid = false;
    }
    if (spacing.eol) {
      if (!number(spacing.eol->width)) {
        valid = false;
      }
      add(spacing.eol->within);
      if (spacing.eol->parallel_edge) {
        for (auto v : *spacing.eol->parallel_edge) {
          add(v);
        }
      }
    }
  }
  for (const auto& table : rules.parallel) {
    if (table.widths.empty() || table.lengths.empty() || table.spacing.size() != table.widths.size()
        || !std::is_sorted(table.widths.begin(), table.widths.end())
        || !std::is_sorted(table.lengths.begin(), table.lengths.end())) {
      valid = false;
    }
    for (auto v : table.widths) {
      if (!number(v)) {
        valid = false;
      }
    }
    for (auto v : table.lengths) {
      if (!number(v)) {
        valid = false;
      }
    }
    for (const auto& row : table.spacing) {
      if (row.size() != table.lengths.size()) {
        valid = false;
      }
      for (auto v : row) {
        add(v);
      }
    }
  }
  if (!valid || (rules.spacing.empty() && rules.parallel.empty())) {
    return {};
  }
  return int64_t(bound);
}
}  // namespace livehd::physical
