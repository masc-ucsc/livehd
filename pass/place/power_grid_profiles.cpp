// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <cctype>
#include <charconv>
#include <cmath>
#include <stdexcept>

#include "power_grid.hpp"
#include "rapidjson/document.h"

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
  return power_grid_profile(physical::resolve_tech_profile(tech, name, physical::Tech_policy::power_grid));
}
Pg_profile tuned_power_grid(const physical::Tech_profile& tech, const rapidjson::Value& record) {
  auto result = power_grid_profile(tech);
  if (!record.IsObject() || !record.HasMember("schema_version") || !record["schema_version"].IsUint()
      || record["schema_version"].GetUint() != 1 || !record.HasMember("stripes") || !record["stripes"].IsObject()
      || record["stripes"].ObjectEmpty()) {
    throw std::invalid_argument("power-grid metadata requires schema_version 1 and a nonempty stripes object");
  }
  for (const auto& [name, dimensions] : record["stripes"].GetObject()) {
    const std::string layer(name.GetString(), name.GetStringLength());
    auto stripe = std::ranges::find(result.stripes, layer, &Pg_stripe::layer);
    if (stripe == result.stripes.end() || !dimensions.IsObject() || dimensions.ObjectEmpty()) {
      throw std::invalid_argument("expected dimensions for a core strap layer in " + tech.name + ": " + layer);
    }
    for (const auto& [key, number] : dimensions.GetObject()) {
      const std::string_view field(key.GetString(), key.GetStringLength());
      if (!number.IsNumber() || !std::isfinite(number.GetDouble()) || number.GetDouble() < 0) {
        throw std::invalid_argument("power-grid " + layer + " dimensions must be finite nonnegative microns");
      }
      const auto value = number.GetDouble();
      if (field == "width") {
        stripe->width = value;
      } else if (field == "pitch") {
        stripe->pitch = value;
      } else if (field == "spacing") {
        stripe->spacing = value;
      } else if (field == "offset") {
        stripe->offset = value;
      } else {
        throw std::invalid_argument("unknown power-grid strap field: " + std::string(field));
      }
    }
    if (stripe->width <= 0 || stripe->spacing <= 0 || stripe->pitch <= 2 * stripe->width + stripe->spacing || stripe->offset >= stripe->pitch) {
      throw std::invalid_argument("invalid power-grid strap width, spacing, pitch or offset on " + layer);
    }
  }
  return result;
}
}  // namespace livehd::placement
