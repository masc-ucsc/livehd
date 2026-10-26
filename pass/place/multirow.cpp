// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "multirow.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>
#include <utility>

#include "multirow_packer.hpp"
#include "pg_rails.hpp"
#include "require.hpp"
#include "row_occupancy.hpp"

namespace livehd::placement {
namespace {
namespace ph = physical;
using multirow_internal::first_core_y;
using multirow_internal::Packer;
using multirow_internal::Refusal;
using multirow_internal::start_refusal;
}  // namespace

size_t multirow_rows(const ph::Technology& tech, const ph::Macro& m) {
  const auto site = tech.sites.find(m.site);
  if (!m.klass.starts_with("CORE") || site == tech.sites.end()) {
    return 0;
  }
  const auto row = std::llround(site->second.height * tech.dbu), height = std::llround(m.height * tech.dbu);
  return row > 0 && height % row == 0 && height / row >= 2 ? size_t(height / row) : 0;
}

std::map<std::string, int64_t> first_core_rows(const ph::Floorplan& fp) {
  std::map<std::string, int64_t> origins;
  for (const auto& row : fp.rows) {
    const auto [it, added] = origins.try_emplace(row.site, row.y);
    it->second             = std::min(it->second, row.y);
  }
  return origins;
}

std::optional<std::string> multirow_start_master(const ph::Technology& tech, const ph::Floorplan& fp, size_t start_row,
                                                 const std::string& master, const Multirow_options& options) {
  const auto& row = fp.rows.at(start_row);
  const auto& m   = tech.macros.at(master);
  const auto  k   = multirow_rows(tech, m);
  require(k != 0, "multirow start check needs a multi-row CORE master");
  const auto row_height = std::llround(tech.sites.at(m.site).height * tech.dbu);
  const auto offset     = row.y - first_core_y(fp, options, m.site);
  if (row.site != m.site || offset % row_height != 0) {
    return std::nullopt;
  }
  // The orientations of the rows the span covers: the first row segment of
  // the site at each row above the start (a missing row refuses nothing here:
  // row availability is not checked).
  std::vector<ph::Orientation> orientations{row.orientation};
  for (size_t h = 1; h < k; ++h) {
    const auto above = std::find_if(fp.rows.begin(), fp.rows.end(), [&](const ph::Row& r) {
      return r.site == m.site && r.y == row.y + int64_t(h) * row_height;
    });
    if (above == fp.rows.end()) {
      orientations.clear();  // unknown span: rails are not judged
      break;
    }
    orientations.push_back(above->orientation);
  }
  const Pg_rail_model rails(tech);
  Refusal             refusal = Refusal::none;
  if (orientations.empty()) {
    for (const auto* candidate : {&master, &multirow_internal::rail_twin(options, master)}) {
      if (!candidate->empty()
          && start_refusal(tech.macros.at(*candidate), *candidate, k, offset / row_height, row.orientation, options)
                 == Refusal::none) {
        return *candidate;
      }
    }
    return std::nullopt;
  }
  const auto* chosen = multirow_internal::start_master(tech,
                                                       rails,
                                                       master,
                                                       k,
                                                       size_t(offset / row_height),
                                                       row.orientation,
                                                       orientations,
                                                       options,
                                                       refusal);
  return chosen ? std::optional<std::string>{*chosen} : std::nullopt;
}

bool multirow_start_allowed(const ph::Technology& tech, const ph::Floorplan& fp, size_t start_row, const std::string& master,
                            const Multirow_options& options) {
  return multirow_start_master(tech, fp, start_row, master, options) == master;
}

Multirow_report anchor_multirow_cells(const ph::Technology& tech, ph::Floorplan& fp, const Multirow_options& options) {
  multirow_internal::require_controls(tech, fp, options);
  std::vector<size_t> cells;
  for (size_t i = 0; i < fp.components.size(); ++i) {
    const auto& c      = fp.components[i];
    const auto  master = tech.macros.find(c.macro);
    require(master != tech.macros.end() && c.place.is_valid(), "multirow packing requires known masters and seeded cells");
    const auto k = multirow_rows(tech, master->second);
    if (!k || (options.admits && !options.admits(c.macro)) || frame_role(tech, c) != Frame_role::movable) {
      continue;
    }
    if (c.place.height != int64_t(k) * std::llround(tech.sites.at(master->second.site).height * tech.dbu)
        || ph::swaps_axes(c.place.orientation)) {
      throw std::invalid_argument("multirow cell footprint differs from its LEF rows: " + c.name);
    }
    cells.push_back(i);
  }
  if (cells.empty()) {
    return {};
  }
  std::sort(cells.begin(), cells.end(), [&](size_t a, size_t b) {
    const auto &ca = fp.components[a], &cb = fp.components[b];
    return std::tie(cb.place.width, ca.name, a) < std::tie(ca.place.width, cb.name, b);
  });
  auto   proposal = fp;
  Packer packer(tech, proposal, options);
  for (size_t cell : cells) {
    packer.anchor(cell);
  }
  fp = std::move(proposal);
  return std::move(packer.report);
}

void release_multirow_anchors(ph::Floorplan& fp, const Multirow_report& report) {
  for (const auto& anchor : report.anchors) {
    if (anchor.component >= fp.components.size() || fp.components[anchor.component].place.status != ph::Status::FIXED) {
      throw std::invalid_argument("multirow anchor is no longer fixed");
    }
  }
  for (const auto& anchor : report.anchors) {
    fp.components[anchor.component].place.status = ph::Status::PLACED;
  }
}
}  // namespace livehd::placement
