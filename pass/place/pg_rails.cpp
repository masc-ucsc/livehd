// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pg_rails.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <set>
#include <stdexcept>

namespace livehd::placement {
namespace {
namespace ph = physical;
bool flips_y(ph::Orientation o) { return o == ph::Orientation::FS || o == ph::Orientation::S; }
}  // namespace

std::string_view pg_rail_name(Pg_rail rail) {
  switch (rail) {
    case Pg_rail::power : return "POWER";
    case Pg_rail::ground: return "GROUND";
    case Pg_rail::none  : return "none";
  }
  return "none";
}

std::vector<Pg_rail> master_edge_rails(const ph::Technology& tech, const ph::Macro& m) {
  const auto site = tech.sites.find(m.site);
  if (site == tech.sites.end()) {
    return {};
  }
  const auto row = std::llround(site->second.height * tech.dbu), height = std::llround(m.height * tech.dbu);
  if (row <= 0 || height <= 0 || height % row) {
    return {};
  }
  const std::set<std::string> routing(tech.routing_layers.begin(), tech.routing_layers.end());
  std::vector<Pg_rail>        edges(size_t(height / row) + 1, Pg_rail::none);
  for (const auto& [name, pin] : m.pins) {
    const auto rail = pin.use == "POWER" ? Pg_rail::power : pin.use == "GROUND" ? Pg_rail::ground : Pg_rail::none;
    if (rail == Pg_rail::none) {
      continue;
    }
    for (const auto& shape : pin.shapes) {
      if (!routing.contains(shape.layer)) {
        continue;  // wells, implants and cuts are not rails
      }
      // Twice the center, in DBU, so a half-DBU center is never an edge.
      const auto twice = std::llround((shape.rect.yl + shape.rect.yh + 2 * m.origin_y) * tech.dbu);
      if (twice % (2 * row) || twice < 0 || twice > 2 * height) {
        continue;
      }
      auto& edge = edges[size_t(twice / (2 * row))];
      if (edge != Pg_rail::none && edge != rail) {
        throw std::invalid_argument(
            std::format("power and ground rails share local row edge {} (pin {})", twice / (2 * row), name));
      }
      edge = rail;
    }
  }
  return edges;
}

std::optional<std::array<Pg_rail, 2>> site_row_rails(const ph::Technology& tech, const std::string& site) {
  std::optional<std::array<Pg_rail, 2>> result;
  for (const auto& [name, m] : tech.macros) {
    if (m.site != site || !m.klass.starts_with("CORE") || m.virtual_proxy) {
      continue;
    }
    std::vector<Pg_rail> edges;
    try {
      edges = master_edge_rails(tech, m);
    } catch (const std::invalid_argument&) {
      return std::nullopt;  // a shorted one-row cell leaves the row rails unknown
    }
    if (edges.size() != 2 || edges[0] == Pg_rail::none || edges[1] == Pg_rail::none) {
      continue;  // multi-row masters and cells without edge rails
    }
    const std::array<Pg_rail, 2> found{edges[0], edges[1]};
    if (found[0] == found[1]) {
      continue;  // one polarity on both edges (a tap or well cell): it says nothing of the row
    }
    if (result && *result != found) {
      return std::nullopt;
    }
    result = found;
  }
  return result;
}

const std::vector<Pg_rail>& Pg_rail_model::edges(const std::string& master) const {
  auto it = masters.find(master);
  if (it == masters.end()) {
    try {
      it = masters.emplace(master, master_edge_rails(tech, tech.macros.at(master))).first;
    } catch (const std::invalid_argument& error) {
      throw std::invalid_argument("LEF master " + master + ": " + error.what());
    }
  }
  return it->second;
}

const std::optional<std::array<Pg_rail, 2>>& Pg_rail_model::site(const std::string& name) const {
  auto it = sites.find(name);
  if (it == sites.end()) {
    it = sites.emplace(name, site_row_rails(tech, name)).first;
  }
  return it->second;
}

Rail_fit Pg_rail_model::fit(const std::string& master, ph::Orientation o, std::span<const ph::Orientation> rows,
                            std::string* why) const {
  const auto& cell = edges(master);
  if (std::all_of(cell.begin(), cell.end(), [](Pg_rail r) { return r == Pg_rail::none; })) {
    return Rail_fit::unknown;
  }
  const auto& row = site(tech.macros.at(master).site);
  if (!row) {
    return Rail_fit::unknown;
  }
  const auto mismatch = [&](std::string reason) {
    if (why) {
      *why = std::move(reason);
    }
    return Rail_fit::mismatch;
  };
  const size_t k = cell.size() - 1;
  if (rows.size() != k || ph::swaps_axes(o)) {
    return mismatch(std::format("{} spans {} rows, not {} in its orientation", master, k, rows.size()));
  }
  // The rail a row edge has seen from row r: its bottom (top = false) or top.
  const auto row_edge = [&](size_t r, bool top) { return (*row)[size_t(top != flips_y(rows[r]))]; };
  for (size_t j = 0; j <= k; ++j) {
    const auto rail = cell[j];
    if (rail == Pg_rail::none) {
      continue;
    }
    const auto edge = flips_y(o) ? k - j : j;  // the core row edge local edge j lands on
    for (const auto& [r, top] : {
             std::pair{    edge, false},
             std::pair{edge - 1,  true}
    }) {
      if (r < k && row_edge(r, top) != rail) {  // edge - 1 wraps past k at edge 0
        return mismatch(std::format("{} {} rail at local row edge {} lands on the {} of a {} row with a {} rail",
                                    master,
                                    pg_rail_name(rail),
                                    j,
                                    top ? "top" : "bottom",
                                    flips_y(rows[r]) ? "flipped" : "N",
                                    pg_rail_name(row_edge(r, top))));
      }
    }
  }
  return Rail_fit::fit;
}
}  // namespace livehd::placement
