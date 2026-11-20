// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "routing_tracks.hpp"

#include "inou/lefdef/tech_profile.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace livehd::placement {
namespace {
struct Io_tracks {
  bool                known = false;
  std::vector<double> positions;
};
Io_tracks io_tracks(const physical::Floorplan& fp, std::string_view layer, std::string_view axis, double half) {
  Io_tracks    result;
  const double lo = (axis == "X" ? fp.die.xl : fp.die.yl) + half;
  const double hi = (axis == "X" ? fp.die.xh : fp.die.yh) - half;
  for (const auto& track : fp.tracks) {
    if (track.layer != layer || track.axis != axis) {
      continue;
    }
    result.known = true;
    if (track.step <= 0 || track.count <= 0) {
      throw std::invalid_argument("IO routing tracks need positive count and step");
    }
    const double first = std::max(0.0, std::ceil((lo - track.start) / track.step));
    const double last  = std::min(double(track.count - 1), std::floor((hi - track.start) / track.step));
    if (last - first > 1000000 || result.positions.size() + std::max(0.0, last - first + 1) > 1000000) {
      throw std::invalid_argument("IO routing track census exceeds one million positions");
    }
    for (double i = first; i <= last; ++i) {
      result.positions.push_back(track.start + i * track.step);
    }
  }
  std::sort(result.positions.begin(), result.positions.end());
  result.positions.erase(std::unique(result.positions.begin(), result.positions.end()), result.positions.end());
  if (result.known && result.positions.empty()) {
    throw std::invalid_argument("IO routing tracks do not fit inside the die on layer " + std::string(layer));
  }
  return result;
}
}  // namespace

std::vector<std::pair<double, double>> io_perimeter_slots(const physical::Technology& tech, const physical::Floorplan& fp,
                                                          std::string_view layer, double pitch, std::string_view vertical_layer,
                                                          const Io_slot_spacing& spacing) {
  if (!std::isfinite(pitch) || pitch <= 0) {
    throw std::invalid_argument("IO slot pitch must be positive and finite");
  }
  if (!(spacing.min_tracks >= 1) || !(spacing.corner_tracks >= 0) || !(spacing.corner_cap >= 0) || !(spacing.spread_dbu >= 0)
      || !std::isfinite(spacing.min_tracks + spacing.corner_tracks + spacing.corner_cap + spacing.spread_dbu)) {
    throw std::invalid_argument("IO slot spacing must be finite, with at least one track between pins");
  }
  std::vector<std::pair<double, double>> slots;
  for (const auto* axis : {"X", "Y"}) {
    const bool   x  = std::string_view(axis) == "X";
    const double lo = x ? fp.die.xl : fp.die.yl, hi = x ? fp.die.xh : fp.die.yh;
    const auto   slot_layer = x && !vertical_layer.empty() ? vertical_layer : layer;
    const auto   found      = tech.layers.find(std::string(slot_layer));
    const double half       = found == tech.layers.end() ? 0 : std::max(1.0, std::round(found->second.width * tech.dbu / 2));
    auto         grid       = io_tracks(fp, slot_layer, axis, half);
    if (!grid.known) {
      for (double v = lo + pitch / 2; v < hi; v += pitch) {
        grid.positions.push_back(v);
      }
    }
    // The track step of this edge's layer: the smallest gap between distinct tracks (else the site pitch).
    double step = pitch;
    if (grid.known && grid.positions.size() > 1) {
      double smallest = std::numeric_limits<double>::infinity();
      for (size_t i = 1; i < grid.positions.size(); ++i) {
        if (const double gap = grid.positions[i] - grid.positions[i - 1]; gap > 0) {
          smallest = std::min(smallest, gap);
        }
      }
      if (std::isfinite(smallest)) {
        step = smallest;
      }
    }
    const double min_gap = spacing.min_tracks == 1 && spacing.spread_dbu == 0
                               ? pitch
                               : std::max({pitch, spacing.min_tracks * step, spacing.spread_dbu}) - 1e-9;
    double       corner  = spacing.corner_tracks * step;
    if (spacing.corner_cap > 0) {
      corner = std::min(corner, spacing.corner_cap);
    }
    double previous = -std::numeric_limits<double>::infinity();
    for (double v : grid.positions) {
      if (v - previous < min_gap || v < lo + corner || v > hi - corner) {
        continue;
      }
      previous = v;
      slots.emplace_back(x ? v : fp.die.xl, x ? fp.die.yl : v);
      slots.emplace_back(x ? v : fp.die.xh, x ? fp.die.yh : v);
    }
  }
  return slots;
}

void shape_io_perimeter_pins(physical::Floorplan& fp) {
  std::map<std::pair<std::string, double>, std::pair<Io_tracks, Io_tracks>> grids;
  for (auto& terminal : fp.terminals) {
    if (terminal.status == physical::Status::FIXED) {
      continue;
    }
    for (auto& shape : terminal.shapes) {
      const bool   side = terminal.x == fp.die.xl || terminal.x == fp.die.xh;
      const double half = (side ? shape.rect.yh - shape.rect.yl : shape.rect.xh - shape.rect.xl) / 2;
      const auto   key  = std::make_pair(shape.layer, half);
      auto         it   = grids.find(key);
      if (it == grids.end()) {
        it = grids.emplace(key, std::make_pair(io_tracks(fp, shape.layer, "X", half), io_tracks(fp, shape.layer, "Y", half))).first;
      }
      const auto& [xs, ys] = it->second;
      if (terminal.x == fp.die.xl) {
        const double reach = xs.known ? xs.positions.front() - terminal.x + half : 2 * half;
        shape.rect         = {0, -half, std::max(2 * half, reach), half};
      } else if (terminal.x == fp.die.xh) {
        const double reach = xs.known ? terminal.x - xs.positions.back() + half : 2 * half;
        shape.rect         = {-std::max(2 * half, reach), -half, 0, half};
      } else if (terminal.y == fp.die.yl) {
        const double reach = ys.known ? ys.positions.front() - terminal.y + half : 2 * half;
        shape.rect         = {-half, 0, half, std::max(2 * half, reach)};
      } else if (terminal.y == fp.die.yh) {
        const double reach = ys.known ? terminal.y - ys.positions.back() + half : 2 * half;
        shape.rect         = {-half, -std::max(2 * half, reach), half, 0};
      }
    }
  }
}

Track_lattice imported_tracks(const physical::Floorplan& fp, std::string_view layer, bool x) {
  Track_lattice result;
  for (const auto& track : fp.tracks) {
    if (track.layer == layer && track.axis == (x ? "X" : "Y")) {
      result.imported = true;
      if (track.step > 0 && track.count > 0) {
        result.patterns.push_back({track.start, track.step, track.count});
      }
    }
  }
  return result;
}

Track_pattern lef_track_pattern(const physical::Technology& tech, const physical::Layer& layer, bool x) {
  return {std::llround((x ? layer.offset_x : layer.offset_y) * tech.dbu),
          std::llround((x ? layer.pitch_x : layer.pitch_y) * tech.dbu),
          std::numeric_limits<int32_t>::max()};
}

namespace {
void append_grid(const physical::Technology& tech, const physical::Floorplan& fp, const std::string& layer, int64_t x_offset,
                 int64_t x_pitch, int64_t y_offset, int64_t y_pitch, std::vector<physical::Track>& tracks) {
  const double half_width = tech.layers.at(layer).width * tech.dbu / 2;
  for (bool x : {true, false}) {
    const auto   pitch  = x ? x_pitch : y_pitch;
    const auto   offset = x ? x_offset : y_offset;
    const double lo = x ? fp.die.xl : fp.die.yl, hi = x ? fp.die.xh : fp.die.yh;
    // Keep the full wire inside the die, preserving the requested phase.
    const double skip  = std::max(0.0, std::ceil((half_width - offset) / pitch));
    const double first = lo + offset + skip * pitch;
    const double count = std::floor((hi - half_width - first) / pitch) + 1;
    if (!std::isfinite(first) || !std::isfinite(count) || count < 1 || count > std::numeric_limits<int32_t>::max()
        || std::abs(first - std::round(first)) > 1e-7 || std::abs(first) > double(std::numeric_limits<int64_t>::max() / 4)) {
      throw std::invalid_argument("track grid does not fit the die or DBU range on layer " + layer);
    }
    tracks.push_back({x ? "X" : "Y", layer, std::llround(first), std::llround(count), pitch});
  }
}
}  // namespace

void apply_default_routing_tracks(const physical::Technology& tech, physical::Floorplan& fp) {
  if (!fp.tracks.empty()) {
    return;
  }
  const auto* profile = physical::detect_tech_profile(tech, physical::Tech_policy::power_grid);
  if (!profile || profile->routing_grids.empty()) {
    return;
  }
  if (fp.dbu <= 0 || fp.dbu != tech.dbu || fp.die.xh <= fp.die.xl || fp.die.yh <= fp.die.yl) {
    throw std::invalid_argument("routing tracks require a finite die with matching technology DBU");
  }
  const auto dbu = [&](double um) {
    const auto n = std::llround(um * fp.dbu);
    if (std::abs(um * fp.dbu - n) > 1e-7) {
      throw std::invalid_argument("platform routing tracks are not representable in floorplan DBU");
    }
    return n;
  };
  std::vector<physical::Track> tracks;
  for (const auto& g : profile->routing_grids) {
    if (tech.layers.contains(g.layer)) {
      append_grid(tech, fp, g.layer, dbu(g.x_offset), dbu(g.x_pitch), dbu(g.y_offset), dbu(g.y_pitch), tracks);
    }
  }
  fp.tracks = std::move(tracks);
}

void apply_routing_tracks(const physical::Technology& tech, physical::Floorplan& fp, std::string_view input) {
  if (fp.dbu <= 0 || fp.dbu != tech.dbu || fp.die.xh <= fp.die.xl || fp.die.yh <= fp.die.yl) {
    throw std::invalid_argument("routing tracks require a finite die with matching technology DBU");
  }
  auto                         data = std::regex_replace(std::string(input), std::regex("\\\\\r?\n"), " ");
  std::istringstream           lines(data);
  std::vector<physical::Track> tracks;
  unsigned                     line_number = 0;
  for (std::string line; std::getline(lines, line);) {
    ++line_number;
    const auto fail = [&](const std::string& why) {
      throw std::invalid_argument("routing tracks line " + std::to_string(line_number) + ": " + why);
    };
    if (auto comment = line.find('#'); comment != std::string::npos) {
      line.resize(comment);
    }
    std::istringstream words(line);
    std::string        command, layer;
    if (!(words >> command)) {
      continue;
    }
    if (command != "make_tracks" || !(words >> layer) || !tech.layers.contains(layer)) {
      fail("expected literal make_tracks followed by a known LEF layer");
    }
    std::map<std::string, int64_t> values;
    for (std::string key, value; words >> key;) {
      if ((key != "-x_offset" && key != "-y_offset" && key != "-x_pitch" && key != "-y_pitch") || !(words >> value)
          || values.contains(key)) {
        fail("expected each offset/pitch option once with a numeric literal");
      }
      char*        end = nullptr;
      const double um  = std::strtod(value.c_str(), &end);
      const double dbu = um * tech.dbu;
      if (end == value.c_str() || *end || !std::isfinite(dbu) || dbu < 0 || dbu > double(std::numeric_limits<int32_t>::max())
          || std::abs(dbu - std::round(dbu)) > 1e-7) {
        fail("offset/pitch must be a nonnegative literal representable in DBU");
      }
      values.emplace(key, std::llround(dbu));
    }
    if (values.size() != 4 || values.at("-x_pitch") == 0 || values.at("-y_pitch") == 0) {
      fail("supply all four offset/pitch options with positive pitches");
    }
    append_grid(tech, fp, layer, values.at("-x_offset"), values.at("-x_pitch"),
                values.at("-y_offset"), values.at("-y_pitch"), tracks);
  }
  if (tracks.empty()) {
    throw std::invalid_argument("routing tracks file contains no make_tracks data");
  }
  fp.tracks = std::move(tracks);
}
}  // namespace livehd::placement
