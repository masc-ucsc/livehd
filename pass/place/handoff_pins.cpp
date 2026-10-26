// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "handoff_pins.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>

#include "inou/lefdef/routing_occupancy.hpp"
#include "inou/lefdef/routing_rules.hpp"

namespace livehd::placement {
namespace ph = physical;
std::vector<ph::Terminal> handoff_pins(const ph::Technology& tech, const ph::Floorplan& fp, const std::string& horizontal_layer,
                                       const std::string& vertical_layer) {
  if (fp.dbu <= 0 || fp.dbu != tech.dbu || fp.die.xl >= fp.die.xh || fp.die.yl >= fp.die.yh) {
    throw std::invalid_argument("IO handoff requires a valid die and matching DBU");
  }
  for (double bound : {fp.die.xl, fp.die.yl, fp.die.xh, fp.die.yh}) {
    if (!std::isfinite(bound) || std::abs(bound) > std::numeric_limits<int32_t>::max()) {
      throw std::invalid_argument("IO die exceeds supported DBU coordinates");
    }
  }
  const auto grid_dbu = tech.manufacturing_grid > 0 ? ph::manufacturing_grid_dbu(tech, fp.dbu) : std::nullopt;
  if (!grid_dbu) {
    throw std::invalid_argument("IO handoff requires an integer manufacturing grid");
  }
  const double grid = *grid_dbu;
  const auto adjustable = [](const ph::Terminal& t) {
    return t.status != ph::Status::FIXED && t.status != ph::Status::LOCKED && t.use != "POWER" && t.use != "GROUND";
  };
  auto seed = fp;
  for (auto& t : seed.terminals) {
    if (adjustable(t)) {
      t.shapes.clear();
    }
  }
  auto                         occupancy = ph::routing_occupancy(tech, seed, fp.dbu);
  const ph::Signal_metal_rules rules(tech, fp);
  struct Layer {
    std::string          name;
    double               width, length;
    int64_t              clearance;
    std::vector<int64_t> tracks;
  };
  std::array<Layer, 2> layers;
  for (size_t i = 0; i < layers.size(); ++i) {
    auto& l               = layers[i];
    l.name                = i == 0 ? horizontal_layer : vertical_layer;
    const auto& source    = tech.layers.at(l.name);
    const auto  clearance = rules.clearance(l.name);
    if (source.direction != (i == 0 ? "HORIZONTAL" : "VERTICAL") || !clearance) {
      throw std::invalid_argument("unsupported IO handoff layer: " + l.name);
    }
    l.clearance      = *clearance;
    const auto width = std::max(source.width, source.rules.min_width.value_or(0.)) * fp.dbu;
    const auto area  = source.rules.area * fp.dbu * fp.dbu;
    if (!std::isfinite(width) || width <= 0 || !std::isfinite(area) || area < 0) {
      throw std::invalid_argument("invalid IO width/area rule");
    }
    l.width  = std::ceil(width / (2 * grid)) * 2 * grid;
    l.length = std::ceil(std::max(l.width, area / l.width) / (2 * grid)) * 2 * grid;
    for (const auto& track : fp.tracks) {
      if (track.layer != l.name || track.axis != (i == 0 ? "Y" : "X")) {
        continue;
      }
      if (track.mask || track.same_mask || track.count <= 0 || track.step <= 0
          || std::abs(double(track.start)) > double(std::numeric_limits<int64_t>::max() / 4)
          || double(track.count - 1) * track.step > double(std::numeric_limits<int64_t>::max() / 4)
          || double(track.start) + double(track.count - 1) * track.step > double(std::numeric_limits<int64_t>::max() / 2)) {
        throw std::invalid_argument("unsupported IO track pattern");
      }
      const auto low  = (i == 0 ? fp.die.yl : fp.die.xl) + l.width / 2;
      const auto high = (i == 0 ? fp.die.yh : fp.die.xh) - l.width / 2;
      for (int64_t n = 0; n < track.count; ++n) {
        const auto position = track.start + n * track.step;
        if (position >= low && position <= high) {
          l.tracks.push_back(position);
        }
      }
    }
    std::sort(l.tracks.begin(), l.tracks.end());
    l.tracks.erase(std::unique(l.tracks.begin(), l.tracks.end()), l.tracks.end());
    if (l.tracks.empty()) {
      throw std::invalid_argument("no literal preferred tracks for IO layer " + l.name);
    }
  }
  struct Pin {
    size_t  index, edge;
    int64_t coordinate;
  };
  std::vector<Pin> pins;
  for (size_t i = 0; i < fp.terminals.size(); ++i) {
    const auto& t = fp.terminals[i];
    if (!adjustable(t)) {
      continue;
    }
    const std::array<double, 4> distance
        = {std::abs(t.x - fp.die.xl), std::abs(t.x - fp.die.xh), std::abs(t.y - fp.die.yl), std::abs(t.y - fp.die.yh)};
    const size_t edge = std::min_element(distance.begin(), distance.end()) - distance.begin();
    pins.push_back({i, edge, edge < 2 ? t.y : t.x});
  }
  std::sort(pins.begin(), pins.end(), [&](const auto& a, const auto& b) {
    return std::tie(a.edge, a.coordinate, fp.terminals[a.index].name) < std::tie(b.edge, b.coordinate, fp.terminals[b.index].name);
  });
  auto                  result = fp.terminals;
  std::array<double, 4> previous;
  previous.fill(-std::numeric_limits<double>::infinity());
  for (const auto& pin : pins) {
    auto&        t          = result[pin.index];
    const bool   horizontal = pin.edge < 2;
    const auto&  l          = layers[horizontal ? 0 : 1];
    const double width = horizontal ? l.length : l.width, height = horizontal ? l.width : l.length;
    if (width > fp.die.xh - fp.die.xl || height > fp.die.yh - fp.die.yl) {
      throw std::invalid_argument("IO rectangle does not fit die");
    }
    const double xl = std::ceil((fp.die.xl + width / 2) / grid) * grid;
    const double xh = std::floor((fp.die.xh - width / 2) / grid) * grid;
    const double yl = std::ceil((fp.die.yl + height / 2) / grid) * grid;
    const double yh = std::floor((fp.die.yh - height / 2) / grid) * grid;
    if (xl > xh || yl > yh) {
      throw std::invalid_argument("IO rectangle has no manufacturing-grid origin inside die");
    }
    auto        right  = std::lower_bound(l.tracks.begin(), l.tracks.end(), pin.coordinate) - l.tracks.begin();
    auto        left   = right - 1;
    bool        found  = false;
    std::string reason = "tracks exhausted";
    while (left >= 0 || right < static_cast<std::ptrdiff_t>(l.tracks.size())) {
      const bool take_left
          = left >= 0
            && (right == static_cast<std::ptrdiff_t>(l.tracks.size())
                || std::abs(double(l.tracks[left]) - pin.coordinate) <= std::abs(double(l.tracks[right]) - pin.coordinate));
      const auto coord = l.tracks[take_left ? left-- : right++];
      if (coord <= previous[pin.edge]) {
        continue;
      }
      const double    x = horizontal ? std::clamp(std::round(t.x / grid) * grid, xl, xh) : coord;
      const double    y = horizontal ? coord : std::clamp(std::round(t.y / grid) * grid, yl, yh);
      const ph::Shape shape{
          l.name,
          {x - width / 2, y - height / 2, x + width / 2, y + height / 2}
      };
      auto check = rules.rectangle(shape);
      if (!check.legal) {
        reason = check.reason;
        continue;
      }
      for (auto id : occupancy.query(l.name, shape.rect, l.clearance, t.net)) {
        check = rules.spacing(shape, occupancy.at(id).shape);
        if (!check.legal) {
          break;
        }
      }
      if (!check.legal) {
        reason = check.reason;
        continue;
      }
      occupancy.insert({shape, t.net});
      t.x           = std::llround(x);
      t.y           = std::llround(y);
      t.orientation = ph::Orientation::N;
      t.status      = ph::Status::PLACED;
      t.shapes      = {
          {l.name, {-width / 2, -height / 2, width / 2, height / 2}}
      };
      previous[pin.edge] = coord;
      found              = true;
      break;
    }
    if (!found) {
      throw std::invalid_argument("no legal IO handoff slot for " + t.name + ": " + reason);
    }
  }
  return result;
}
}  // namespace livehd::placement
