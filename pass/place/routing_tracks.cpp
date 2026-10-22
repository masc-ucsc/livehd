// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "routing_tracks.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace livehd::placement {
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
    const double half_width = tech.layers.at(layer).width * tech.dbu / 2;
    for (bool x : {true, false}) {
      const auto   pitch  = values.at(x ? "-x_pitch" : "-y_pitch");
      const auto   offset = values.at(x ? "-x_offset" : "-y_offset");
      const double lo = x ? fp.die.xl : fp.die.yl, hi = x ? fp.die.xh : fp.die.yh;
      // Keep the full wire inside the die, preserving the requested phase.
      const double skip  = std::max(0.0, std::ceil((half_width - offset) / pitch));
      const double first = lo + offset + skip * pitch;
      const double count = std::floor((hi - half_width - first) / pitch) + 1;
      if (!std::isfinite(first) || !std::isfinite(count) || count < 1 || count > std::numeric_limits<int32_t>::max()
          || std::abs(first - std::round(first)) > 1e-7 || std::abs(first) > double(std::numeric_limits<int64_t>::max() / 4)) {
        fail("track grid does not fit the die or DBU range");
      }
      tracks.push_back({x ? "X" : "Y", layer, std::llround(first), std::llround(count), pitch});
    }
  }
  if (tracks.empty()) {
    throw std::invalid_argument("routing tracks file contains no make_tracks data");
  }
  fp.tracks = std::move(tracks);
}
}  // namespace livehd::placement
