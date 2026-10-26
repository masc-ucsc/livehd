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
    if (channel.region.xl >= channel.region.xh || channel.region.yl >= channel.region.yh) {
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
Pg_profile power_grid_profile(std::string_view name, const physical::Technology& tech) {
  if (name == "auto") {
    if (!tech.macros.contains("TAPCELL_ASAP7_75t_R") || !tech.sites.contains("asap7sc7p5t")) {
      throw std::invalid_argument("cannot auto-detect a supported native power-grid profile");
    }
    name = "pg_asap7";
  }
  if (name == "asap7") {
    name = "pg_asap7";
  }
  if (name != "pg_asap7") {
    throw std::invalid_argument("unknown native power-grid profile: " + std::string(name));
  }
  Pg_profile p;
  p.name               = name;
  p.reference_revision = "fb65d5b487d2afd443fc1892330f622c776a57ac";
  p.reference_file     = "flow/platforms/asap7/openRoad/pdn/grid_strategy-M1-M2-M5-M6.tcl";
  p.followpin_layers   = {"M1", "M2"};
  p.stripes            = {{"M5", .120, .072, 5.4, .300}, {"M6", .288, .096, 5.4, .513}};
  p.connect            = {{{"M1", "M2"}}, {{"M2", "M5"}}, {{"M5", "M6"}}};
  p.pin_layer          = "M6";
  p.macro_halo         = 2;
  p.macro_connect      = {{{"M4", "M5"}}};
  p.nets               = {"VDD", "VSS"};
  p.pin_names          = {{{"VDD", "VDDPE", "VDDCE"}, {"VSS", "VSSE"}}};
  return p;
}
}  // namespace livehd::placement
