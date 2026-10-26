// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "capacity_bins.hpp"
#include "multirow.hpp"
#include "require.hpp"
#include "row_occupancy.hpp"

// Private to //pass/place:multirow: the anchor packer, shared by the anchor
// prepass (multirow.cpp) and the two-row ECO (multirow_eco.cpp).
namespace livehd::placement::multirow_internal {
namespace ph   = physical;
using Interval = std::pair<int64_t, int64_t>;  // [first, last) sites

// Row-access origin of a site: the recorded first core row, else its lowest row here.
inline int64_t first_core_y(const ph::Floorplan& fp, const Multirow_options& options, const std::string& site) {
  if (const auto it = options.core_origins.find(site); it != options.core_origins.end()) {
    return it->second;
  }
  const auto origins = first_core_rows(fp);
  const auto it      = origins.find(site);
  require(it != origins.end(), "multirow packing found no row of the master's site");
  return it->second;
}
enum class Refusal : uint8_t { none, symmetry, access };
// Why a k-row master may not start on core row `band` with orientation o.
inline Refusal start_refusal(const ph::Macro& m, const std::string& master, size_t rows, size_t band, ph::Orientation o,
                             const Multirow_options& options) {
  if (!ph::orientation_allowed(m, o)) {
    return Refusal::symmetry;
  }
  if (!options.access || !options.templates) {
    return Refusal::none;
  }
  const auto t = options.templates->find(master);
  if (t == options.templates->end()) {
    return Refusal::none;
  }
  if (rows != t->second.half.size()) {
    throw std::invalid_argument("multirow row-access template needs a two-row master: " + master);
  }
  return ph::row_access_matches(*options.access, band, o, t->second.half) ? Refusal::none : Refusal::access;
}
// Append the gaps of row, shifted by offset sites, that overlap [lo, hi).
inline void gaps_within(const Row_gaps& row, int64_t offset, int64_t lo, int64_t hi, std::vector<Interval>& out) {
  auto it = row.free.upper_bound(lo - offset);
  if (it != row.free.begin()) {
    --it;
  }
  for (; it != row.free.end() && it->first + offset < hi; ++it) {
    if (it->second + offset > lo) {
      out.emplace_back(it->first + offset, it->second + offset);
    }
  }
}
// Pairwise overlaps of two ascending lists of disjoint intervals.
inline std::vector<Interval> intersect(const std::vector<Interval>& a, const std::vector<Interval>& b) {
  std::vector<Interval> result;
  for (size_t i = 0, j = 0; i < a.size() && j < b.size();) {
    const auto lo = std::max(a[i].first, b[j].first), hi = std::min(a[i].second, b[j].second);
    if (lo < hi) {
      result.emplace_back(lo, hi);
    }
    if (a[i].second < b[j].second) {
      ++i;
    } else {
      ++j;
    }
  }
  return result;
}
// The controls and frame units anchor_multirow_cells and the two-row ECO accept.
inline void require_controls(const ph::Technology& tech, const ph::Floorplan& fp, const Multirow_options& options) {
  const auto& o = options.occupancy;
  require(o.bins_x && o.bins_x <= 1024 && o.bins_y && o.bins_y <= 1024 && std::isfinite(o.max_density) && o.max_density > 0
              && o.max_density <= 1 && o.padding_sites <= 128 && o.macro_halo_x_dbu >= 0 && o.macro_halo_y_dbu >= 0 && options.work,
          "invalid multirow packing controls");
  require(fp.dbu > 0 && fp.dbu == tech.dbu, "multirow packing requires matching positive DBU units");
}

struct Packer {
  // Search outcome counts for one cell; the furthest stage reached names the failure.
  struct Tally {
    size_t starts = 0, symmetry = 0, access = 0, dense = 0;
  };
  struct Pose {
    Ann_place place;
    int64_t   band;
    double    cost;
  };
  struct Site_rows {
    int64_t                                origin = 0;  // y of the first core row
    std::map<int64_t, std::vector<size_t>> bands;       // rows on the core row grid, by core row index
  };
  const ph::Technology&            tech;
  ph::Floorplan&                   fp;
  const Multirow_options&          options;
  Multirow_report                  report;
  std::vector<Row_gaps>            rows;
  Capacity_bins                    grid;
  std::map<std::string, Site_rows> by_site;

  Packer(const ph::Technology& technology, ph::Floorplan& frame, const Multirow_options& controls)
      : tech(technology), fp(frame), options(controls) {
    auto occupancy = row_occupancy(tech, fp, options.occupancy, "multirow packing", /*hard_exclusions=*/true);
    rows           = std::move(occupancy.rows);
    grid           = std::move(occupancy.grid);
  }
  const Site_rows& site_rows(const std::string& site, int64_t row_height) {
    const auto [it, added] = by_site.try_emplace(site);
    if (added) {
      auto& s  = it->second;
      s.origin = first_core_y(fp, options, site);
      for (size_t r = 0; r < rows.size(); ++r) {
        const auto& row = rows[r];
        if (fp.rows[row.source].site == site && row.height == row_height && (row.y - s.origin) % row_height == 0) {
          s.bands[(row.y - s.origin) / row_height].push_back(r);  // ascending x within a core row
        }
      }
    }
    return it->second;
  }
  // The row segments of the k core rows from `band` up; empty unless all exist.
  static std::vector<const std::vector<size_t>*> band_span(const Site_rows& site, int64_t band, size_t k) {
    std::vector<const std::vector<size_t>*> span;
    for (size_t h = 0; h < k; ++h) {
      const auto it = site.bands.find(band + int64_t(h));
      if (it == site.bands.end()) {
        return {};
      }
      span.push_back(&it->second);
    }
    return span;
  }
  // Free sites common to every row of a band span, as sites of its start row
  // segment, within [lo, hi). Other rows must share the start row's site grid;
  // no interval spans two row segments.
  std::vector<Interval> common_gaps(const Row_gaps& start, const std::vector<const std::vector<size_t>*>& span, int64_t lo,
                                    int64_t hi) const {
    std::vector<Interval> common;
    gaps_within(start, 0, lo, hi, common);
    for (size_t h = 1; h < span.size() && !common.empty(); ++h) {
      std::vector<Interval> gaps;
      for (const size_t r : *span[h]) {
        const auto& row = rows[r];
        if ((row.x - start.x) % start.step == 0) {
          gaps_within(row, (row.x - start.x) / start.step, lo, hi, gaps);
        }
      }
      common = intersect(common, gaps);
    }
    return common;
  }
  void spend(const ph::Component& c) {
    if (++report.work > options.work) {
      throw Multirow_failure("multirow packing exhausted its work budget at " + c.name + "; no output committed");
    }
  }
  // Nearest legal pose of a k-row cell, within its window or in the whole core.
  std::optional<Pose> search(size_t cell, size_t k, bool windowed, Tally& tally) {
    const auto&          c          = fp.components[cell];
    const auto&          m          = tech.macros.at(c.macro);
    const auto&          gp         = c.place;
    const auto           row_height = gp.height / int64_t(k);
    const auto&          site       = site_rows(m.site, row_height);
    const auto&          by_band    = site.bands;
    const auto           first_y    = site.origin;
    const auto           dy         = [&](int64_t band) { return std::abs(double(first_y + band * row_height - gp.y)); };
    const auto           nearest    = std::llround(double(gp.y - first_y) / row_height);
    std::vector<int64_t> order;
    for (const auto& entry : by_band) {
      if (!windowed || std::abs(entry.first - nearest) <= options.window_rows) {
        order.push_back(entry.first);
      }
    }
    std::stable_sort(order.begin(), order.end(), [&](int64_t a, int64_t b) { return dy(a) < dy(b); });
    std::optional<Pose> best;
    const auto          better = [&](double cost, int64_t y, int64_t x) {
      return !best || std::tie(cost, y, x) < std::tie(best->cost, best->place.y, best->place.x);
    };
    for (const auto band : order) {
      if (best && dy(band) > best->cost) {
        break;  // farther core rows only cost more
      }
      const auto span = band_span(site, band, k);
      if (span.empty()) {
        continue;
      }
      for (const size_t s : *span.front()) {
        spend(c);
        ++tally.starts;
        const auto& start       = rows[s];
        const auto  orientation = fp.rows[start.source].orientation;
        const auto  refusal     = start_refusal(m, c.macro, k, band, orientation, options);
        if (refusal != Refusal::none) {
          ++(refusal == Refusal::symmetry ? tally.symmetry : tally.access);
          continue;
        }
        const int64_t     pad       = options.occupancy.padding_sites;
        const int64_t     sites     = (gp.width + start.step - 1) / start.step;
        const double      goal      = double(gp.x - start.x) / start.step;
        // Free sites common to all k rows, as sites of the start row, limited
        // to the window.
        constexpr int64_t unbounded = int64_t(1) << 62;
        const int64_t     reach_lo  = windowed ? int64_t(std::ceil(goal - options.window_sites)) - pad : -unbounded;
        const int64_t     reach_hi  = windowed ? int64_t(std::floor(goal + options.window_sites)) + sites + pad : unbounded;
        for (const auto& [lo, hi] : common_gaps(start, span, reach_lo, reach_hi)) {
          // The cell starts at site j and occupies [j - pad, j + sites + pad).
          int64_t first = lo + pad, last = hi - sites - pad;
          if (windowed) {
            first = std::max<int64_t>(first, std::ceil(goal - options.window_sites));
            last  = std::min<int64_t>(last, std::floor(goal + options.window_sites));
          }
          if (last < first) {
            continue;
          }
          // Sites in order of distance from the goal, left first on ties.
          int64_t left = std::min<int64_t>(std::floor(goal), last), right = std::max(left + 1, first);
          while (left >= first || right <= last) {
            const bool    take_left = left >= first && (right > last || goal - left <= right - goal);
            const int64_t x         = start.x + (take_left ? left-- : right++) * start.step;
            const double  cost      = dy(band) + std::abs(double(x - gp.x));
            if (!better(cost, start.y, x)) {
              break;
            }
            spend(c);
            ++report.candidates;
            Ann_place p   = gp;
            p.x           = x;
            p.y           = start.y;
            p.orientation = orientation;
            p.status      = ph::Status::FIXED;
            if (grid.fits(p, nullptr, options.occupancy.max_density)) {
              best = Pose{p, band, cost};
              break;
            }
            ++tally.dense;
          }
        }
      }
    }
    return best;
  }
  std::string failure(const ph::Component& c, size_t k, const Tally& tally) const {
    const auto& m = tech.macros.at(c.macro);
    std::string reason;
    if (tally.dense) {
      reason = "every free pose exceeds the hard bin density cap " + std::to_string(options.occupancy.max_density);
    } else if (tally.starts > tally.symmetry + tally.access) {
      reason = "no free interval of " + std::to_string(c.place.width) + " DBU plus "
               + std::to_string(options.occupancy.padding_sites) + " padding sites per side is common to " + std::to_string(k)
               + " consecutive rows";
    } else if (tally.starts) {
      reason = "every start row is refused: " + std::to_string(tally.symmetry) + " orientations lack LEF SYMMETRY, "
               + std::to_string(tally.access) + " starts miss the row-access template";
    } else {
      reason = "no " + std::to_string(k) + " consecutive core rows of site " + m.site;
    }
    return "multirow packing cannot anchor " + c.name + " (" + c.macro + "): " + reason + "; no output committed";
  }
  void anchor(size_t cell) {
    auto&      c = fp.components[cell];
    const auto k = multirow_rows(tech, tech.macros.at(c.macro));
    Tally      tally;
    auto       pose = search(cell, k, true, tally);
    if (!pose) {
      ++report.full_core_scans;
      tally = {};
      pose  = search(cell, k, false, tally);
    }
    if (!pose) {
      throw Multirow_failure(failure(c, k, tally));
    }
    const auto before = c.place;
    c.place           = pose->place;
    reserve_gaps(rows, padded_rect(tech, c, options.occupancy.padding_sites));
    grid.deposit(c.place, true);
    report.anchors.push_back({cell, before});
    ++(pose->band % 2 ? report.odd_starts : report.even_starts);
    const double displacement     = (std::abs(c.place.x - before.x) + std::abs(c.place.y - before.y)) / double(fp.dbu);
    report.total_displacement_um += displacement;
    report.max_displacement_um    = std::max(report.max_displacement_um, displacement);
  }
};
}  // namespace livehd::placement::multirow_internal
