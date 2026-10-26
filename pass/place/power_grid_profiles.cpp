// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <cctype>
#include <charconv>
#include <cmath>
#include <stdexcept>

#include "power_grid.hpp"

namespace livehd::placement {
std::vector<Pg_channel> parse_pg_channels(std::string_view text) {
  const auto trim = [](std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
      value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
      value.remove_suffix(1);
    }
    return value;
  };
  text = trim(text);
  if (text.empty()) {
    return {};
  }
  std::vector<Pg_channel> result;
  while (true) {
    const auto                      end   = text.find(';');
    auto                            entry = text.substr(0, end);
    std::array<std::string_view, 6> fields;
    for (size_t i = 0; i < fields.size(); ++i) {
      const auto comma = entry.find(',');
      if ((i + 1 == fields.size()) != (comma == std::string_view::npos)) {
        throw std::invalid_argument("PG channel expects style,layer,xl,yl,xh,yh");
      }
      fields[i] = trim(entry.substr(0, comma));
      if (fields[i].empty()) {
        throw std::invalid_argument("empty PG channel field");
      }
      if (comma != std::string_view::npos) {
        entry.remove_prefix(comma + 1);
      }
    }
    Pg_channel channel;
    channel.style = fields[0];
    channel.layer = fields[1];
    if (channel.style != "skip" && channel.style != "gap") {
      throw std::invalid_argument("PG channel style must be skip or gap");
    }
    std::array<double*, 4> coordinates{&channel.region.xl, &channel.region.yl, &channel.region.xh, &channel.region.yh};
    for (size_t i = 0; i < coordinates.size(); ++i) {
      const auto value  = fields[i + 2];
      const auto parsed = std::from_chars(value.data(), value.data() + value.size(), *coordinates[i]);
      if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || !std::isfinite(*coordinates[i])) {
        throw std::invalid_argument("PG channel coordinates must be finite numbers in microns");
      }
    }
    if (channel.region.empty()) {
      throw std::invalid_argument("PG channel requires xl<xh and yl<yh");
    }
    result.push_back(std::move(channel));
    if (end == std::string_view::npos) {
      break;
    }
    text.remove_prefix(end + 1);
  }
  return result;
}
Pg_profile power_grid_profile(const physical::Tech_profile& profile) {
  if (profile.pg.name.empty()) {
    throw std::invalid_argument("technology profile has no native power-grid strategy: " + profile.name);
  }
  Pg_profile p;
  static_cast<physical::Pg_strategy&>(p) = profile.pg;
  return p;
}
Pg_profile power_grid_profile(std::string_view name, const physical::Technology& tech) {
  return power_grid_profile(physical::resolve_tech_profile(tech, name));
}
}  // namespace livehd::placement
