// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "row_occupancy.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <tuple>

#include "capacity_bins.hpp"

namespace livehd::placement {
namespace {
namespace ph = physical;
bool fixed(const Ann_place& p) { return p.status == ph::Status::FIXED || p.status == ph::Status::LOCKED; }
}  // namespace

void subtract_gaps(std::map<int64_t, int64_t>& gaps, int64_t lo, int64_t hi) {
  auto it = gaps.upper_bound(lo);
  if (it != gaps.begin()) {
    --it;
  }
  while (it != gaps.end() && it->first < hi) {
    const auto [a, b] = *it;
    if (b <= lo) {
      ++it;
      continue;
    }
    it = gaps.erase(it);
    if (a < lo) {
      gaps.emplace(a, lo);
    }
    if (b > hi) {
      gaps.emplace(hi, b);
      break;
    }
  }
}

std::vector<Row_gaps> row_gaps(const ph::Technology& tech, const ph::Floorplan& fp, std::string_view owner) {
  const auto            fail = [&](const char* what) { throw std::invalid_argument(std::string(owner) + what); };
  std::vector<Row_gaps> rows;
  for (size_t i = 0; i < fp.rows.size(); ++i) {
    const auto& r    = fp.rows[i];
    const auto  site = tech.sites.find(r.site);
    if (site == tech.sites.end() || r.count <= 0 || r.step <= 0) {
      fail(" requires valid row sites");
    }
    const auto h = std::llround(site->second.height * fp.dbu);
    if (h <= 0 || std::abs(site->second.width * fp.dbu - r.step) > 1e-6) {
      fail(" requires contiguous row sites");
    }
    // Ignore sites outside the die, rounding inward to complete sites.
    const int64_t lo = std::max<int64_t>(0, std::ceil((fp.die.xl - r.x) / r.step));
    const int64_t hi = std::min<int64_t>(r.count, std::floor((fp.die.xh - r.x) / r.step));
    if (hi <= lo || r.y < fp.die.yl || r.y + h > fp.die.yh) {
      continue;
    }
    rows.push_back({i, r.x, r.y, r.step, h, r.count, {{lo, hi}}});
  }
  if (rows.empty()) {
    fail(" has no usable rows");
  }
  std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return std::tie(a.y, a.x) < std::tie(b.y, b.x); });
  for (size_t i = 0; i < rows.size(); ++i) {
    const auto& a = rows[i];
    for (size_t j = i + 1; j < rows.size() && rows[j].y < a.y + a.height; ++j) {
      const auto& b = rows[j];
      if (std::max(a.x, b.x) < std::min(a.x + a.count * a.step, b.x + b.count * b.step)) {
        fail(" does not accept overlapping row definitions");
      }
    }
  }
  return rows;
}

ph::Rect gaps_extent(const std::vector<Row_gaps>& rows) {
  ph::Rect bounds{INFINITY, INFINITY, -INFINITY, -INFINITY};
  for (const auto& r : rows) {
    for (const auto& [lo, hi] : r.free) {
      bounds = bounds.hull(gap_rect(r, lo, hi));
    }
  }
  return bounds;
}

void reserve_gaps(std::vector<Row_gaps>& rows, const ph::Rect& b) {
  for (auto& row : rows) {
    if (row.y >= b.yh) {
      break;
    }
    if (row.y + row.height <= b.yl) {
      continue;
    }
    const auto lo = std::clamp<int64_t>(std::floor((b.xl - row.x) / row.step), 0, row.count);
    const auto hi = std::clamp<int64_t>(std::ceil((b.xh - row.x) / row.step), 0, row.count);
    if (hi > lo) {
      subtract_gaps(row.free, lo, hi);
    }
  }
}

bool fixed_physical_cell(const ph::Component& c, const ph::Macro& m) {
  return fixed(c.place) && (m.klass == "CORE WELLTAP" || m.klass == "CORE SPACER");
}

Frame_role frame_role(const ph::Technology& tech, const ph::Component& c) {
  if (ph::placement_mask(c, tech) == ph::Placement_mask::soft) {
    return Frame_role::ignored;
  }
  const auto& m = tech.macros.at(c.macro);
  if (!m.klass.starts_with("CORE") || fixed_physical_cell(c, m)) {
    return Frame_role::obstacle;
  }
  return fixed(c.place) ? Frame_role::fixed_logic : Frame_role::movable;
}

ph::Rect obstacle_rect(const ph::Technology& tech, const ph::Component& c, int64_t halo_x_dbu, int64_t halo_y_dbu) {
  auto b = Capacity_bins::footprint(c.place);
  if (!tech.macros.at(c.macro).klass.starts_with("CORE") && ph::placement_mask(c, tech) == ph::Placement_mask::none) {
    b.xl -= halo_x_dbu;
    b.xh += halo_x_dbu;
    b.yl -= halo_y_dbu;
    b.yh += halo_y_dbu;
  }
  return b;
}

ph::Rect padded_rect(const ph::Technology& tech, const ph::Component& c, unsigned padding_sites) {
  const auto site = tech.sites.find(tech.macros.at(c.macro).site);
  if (site == tech.sites.end()) {
    throw std::invalid_argument("fixed CORE cell has no site");
  }
  auto         b    = Capacity_bins::footprint(c.place);
  const double pad  = padding_sites * site->second.width * tech.dbu;
  b.xl             -= pad;
  b.xh             += pad;
  return b;
}
}  // namespace livehd::placement
