// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2022-2025, The OpenROAD Authors
// Adapted row-cut and tap semantics from OpenDB src/odb/src/db/util.cpp and
// OpenROAD src/tap/src/tapcell.cpp, revision
// b65c274cadefe2151aa7eee32e1be9026e8ff14a. See LICENSE for license terms.
#include "handoff_rows.hpp"
#include "repeater_wire_filter.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace livehd::placement {
namespace {
namespace ph = physical;
int64_t dbu(double value) {
  if (!std::isfinite(value) || value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max()) {
    throw std::runtime_error("row-cut dimension exceeds DEF range");
  }
  return std::llround(value);
}
int64_t align(int64_t x, int64_t width, int64_t origin, bool left) {
  const double site = double(x - origin) / width;
  return origin + int64_t(left ? std::floor(site) : std::ceil(site)) * width;
}
}  // namespace

std::vector<physical::Row> cut_macro_rows(const ph::Floorplan& frame, const ph::Technology& tech, int64_t halo_x, int64_t halo_y,
                                          int64_t min_row_width) {
  if (frame.dbu <= 0 || halo_x < 0 || halo_y < 0 || min_row_width < 0) {
    throw std::invalid_argument("row-cut units, halos and minimum width must be valid nonnegative dimensions");
  }
  dbu(double(halo_x));
  dbu(double(halo_y));
  dbu(double(min_row_width));
  std::vector<ph::Rect> blockages, fixed_cells;
  for (const auto& inst : frame.components) {
    if (inst.place.status == ph::Status::UNPLACED) {
      continue;
    }
    const auto& master = tech.macros.at(inst.macro);
    auto        width = dbu(master.width * frame.dbu), height = dbu(master.height * frame.dbu);
    if (width <= 0 || height <= 0) {
      throw std::runtime_error("row cutting requires positive component dimensions");
    }
    dbu(double(inst.place.x));
    dbu(double(inst.place.y));
    if (ph::swaps_axes(inst.place.orientation)) {
      std::swap(width, height);
    }
    const ph::Rect bounds{double(inst.place.x), double(inst.place.y), double(inst.place.x) + width, double(inst.place.y) + height};
    if (master.klass.starts_with("BLOCK") || master.virtual_proxy) {
      blockages.push_back({bounds.xl - halo_x, bounds.yl - halo_y, bounds.xh + halo_x, bounds.yh + halo_y});
    } else if ((master.klass.starts_with("CORE") || master.klass.starts_with("ENDCAP"))
               && (inst.place.status == ph::Status::FIXED || inst.place.status == ph::Status::LOCKED)) {
      fixed_cells.push_back(bounds);
    }
  }
  if (blockages.empty()) {
    return frame.rows;
  }
  std::vector<ph::Row> result;
  for (const auto& row : frame.rows) {
    dbu(double(row.x));
    dbu(double(row.y));
    const auto& site = tech.sites.at(row.site);
    const auto  sw = dbu(site.width * frame.dbu), rh = dbu(site.height * frame.dbu);
    if (sw <= 0 || rh <= 0 || row.step <= 0 || row.count <= 0 || ph::swaps_axes(row.orientation)) {
      throw std::runtime_error("row cutting requires horizontal rows with positive site dimensions");
    }
    const int64_t                            right = dbu(double(row.x) + double(row.count - 1) * row.step + sw);
    const ph::Rect                           bounds{double(row.x), double(row.y), double(right), double(row.y) + rh};
    std::vector<std::pair<int64_t, int64_t>> intervals;
    for (const auto& obstruction : blockages) {
      if (bounds.overlaps(obstruction)) {
        intervals.emplace_back(dbu(obstruction.xl), dbu(obstruction.xh));
      }
    }
    if (intervals.empty()
        || std::any_of(fixed_cells.begin(), fixed_cells.end(), [&](const auto& cell) { return bounds.contains(cell); })) {
      result.push_back(row);
      continue;
    }
    std::sort(intervals.begin(), intervals.end());
    std::vector<std::pair<int64_t, int64_t>> segments;
    int64_t                                  start = row.x;
    for (const auto& [lo, hi] : intervals) {
      segments.emplace_back(start, align(lo, sw, start, true));
      start = std::max(start, align(hi, sw, start, false));
    }
    segments.emplace_back(start, right);
    const auto minimum = min_row_width + 2 * sw;
    const auto valid   = [&](const auto& segment) { return (segment.second - segment.first) / sw * sw >= minimum; };
    const auto endcap  = min_row_width / 2;
    if (endcap > 0) {
      const auto first = std::find_if(segments.begin(), segments.end(), valid);
      if (first != segments.end() && first->first > row.x && first->first - row.x < endcap) {
        first->first = align(row.x + endcap, sw, row.x, false);
      }
      const auto last = std::find_if(segments.rbegin(), segments.rend(), valid);
      if (last != segments.rend()) {
        const auto end = last->first + (last->second - last->first) / sw * sw;
        if (right > end && right - end < endcap) {
          last->second = align(right - endcap, sw, last->first, true);
        }
      }
    }
    for (size_t i = 0; i < segments.size(); ++i) {
      const auto [lo, hi] = segments[i];
      const auto count    = (hi - lo) / sw;
      if (count > 0 && count * sw >= minimum) {
        result.push_back({row.name + "_" + std::to_string(i + 1), row.site, lo, row.y, count, sw, row.orientation});
      }
    }
  }
  return result;
}
std::vector<ph::Component> row_endcaps(const ph::Floorplan& frame, const ph::Technology& tech, const std::string& master_name,
                                       const std::string& prefix) {
  if (frame.dbu <= 0 || prefix.empty()) {
    throw std::invalid_argument("endcaps require positive DBU and a nonempty name prefix");
  }
  const auto& master = tech.macros.at(master_name);
  const auto  width = dbu(master.width * frame.dbu), height = dbu(master.height * frame.dbu);
  if (width <= 0 || height <= 0 || master.site.empty()) {
    throw std::invalid_argument("endcap master requires positive dimensions and a site");
  }
  const auto& site = tech.sites.at(master.site);
  const auto  sw = dbu(site.width * frame.dbu), sh = dbu(site.height * frame.dbu);
  if (sw <= 0 || sh != height || width % sw != 0) {
    throw std::invalid_argument("endcap master must occupy whole sites in a single row");
  }
  std::set<std::string> names;
  for (const auto& inst : frame.components) {
    names.insert(inst.name);
  }
  // Group rows by Y to find joined segments without a quadratic row scan.
  std::map<int64_t, std::vector<const ph::Row*>> bands;
  for (const auto& row : frame.rows) {
    if (row.site != master.site) {
      continue;
    }
    dbu(double(row.x));
    dbu(double(row.y));
    if (row.count <= 0 || row.step != sw
        || (row.orientation != ph::Orientation::N && row.orientation != ph::Orientation::FS
            && row.orientation != ph::Orientation::FN && row.orientation != ph::Orientation::S)) {
      throw std::invalid_argument("endcaps require contiguous horizontal site rows");
    }
    dbu(double(row.x) + double(row.count) * sw);
    bands[row.y].push_back(&row);
  }
  std::vector<ph::Component> result;
  std::optional<int64_t>     previous_y;
  for (auto& [y, rows] : bands) {
    if (previous_y && y < *previous_y + height) {
      throw std::invalid_argument("endcap rows overlap vertically");
    }
    previous_y = y;
    std::sort(rows.begin(), rows.end(), [](auto a, auto b) { return a->x < b->x; });
    for (size_t i = 0; i < rows.size(); ++i) {
      const auto& row   = *rows[i];
      const auto  right = row.x + row.count * sw;
      if (i && rows[i - 1]->x + rows[i - 1]->count * sw > row.x) {
        throw std::invalid_argument("endcap rows overlap horizontally");
      }
      const bool left_edge  = i == 0 || rows[i - 1]->x + rows[i - 1]->count * sw != row.x;
      const bool right_edge = i + 1 == rows.size() || right != rows[i + 1]->x;
      if (row.count * sw < width * (int(left_edge) + int(right_edge))) {
        throw std::invalid_argument("row cannot contain its endcaps");
      }
      for (const bool is_right : {false, true}) {
        if (is_right ? !right_edge : !left_edge) {
          continue;
        }
        auto orient = row.orientation;
        if (is_right && master.symmetry_y) {
          switch (orient) {
            case ph::Orientation::N : orient = ph::Orientation::FN; break;
            case ph::Orientation::FS: orient = ph::Orientation::S; break;
            case ph::Orientation::FN: orient = ph::Orientation::N; break;
            case ph::Orientation::S : orient = ph::Orientation::FS; break;
            default                 : break;
          }
        }
        const bool supported = orient == ph::Orientation::N || (orient == ph::Orientation::FS && master.symmetry_x)
                               || (orient == ph::Orientation::FN && master.symmetry_y)
                               || (orient == ph::Orientation::S && master.symmetry_x && master.symmetry_y);
        if (!supported) {
          throw std::invalid_argument("endcap master symmetry does not support row orientation");
        }
        const auto        x    = is_right ? right - width : row.x;
        const std::string name = prefix + row.name + (is_right ? "_Right" : "_Left");
        if (!names.insert(name).second) {
          throw std::invalid_argument("endcap instance name collision: " + name);
        }
        Ann_place place(x, y, width, height);
        place.orientation = orient;
        place.status      = ph::Status::LOCKED;
        result.push_back({name, master_name, place});
      }
    }
  }
  return result;
}

std::vector<ph::Component> row_taps(const ph::Floorplan& frame, const ph::Technology& tech, const std::string& master_name,
                                    int64_t distance, const std::string& prefix) {
  if (frame.dbu <= 0 || distance <= 0 || prefix.empty()) {
    throw std::invalid_argument("taps require positive DBU, distance and a nonempty prefix");
  }
  dbu(double(distance));
  const auto& master = tech.macros.at(master_name);
  const auto  width = dbu(master.width * frame.dbu), height = dbu(master.height * frame.dbu);
  const auto& site = tech.sites.at(master.site);
  const auto  sw = dbu(site.width * frame.dbu), sh = dbu(site.height * frame.dbu);
  if (width <= 0 || sw <= 0 || height <= 1 || height != sh || width % sw || distance < width) {
    throw std::invalid_argument("tap master requires whole sites, single-row height, and distance >= width");
  }
  struct Band {
    std::vector<const ph::Row*> rows;
    std::vector<ph::Rect>       fixed;
  };
  std::map<int64_t, Band> bands;
  for (const auto& row : frame.rows) {
    if (row.site != master.site || row.count <= 0 || row.step != sw
        || (row.orientation != ph::Orientation::N && row.orientation != ph::Orientation::FS)
        || (row.orientation == ph::Orientation::FS && !master.symmetry_x)) {
      throw std::invalid_argument("taps require a single site with supported contiguous N/FS rows");
    }
    dbu(double(row.x));
    dbu(double(row.y));
    dbu(double(row.x) + double(row.count) * sw);
    dbu(double(row.y) + height);
    bands[row.y].rows.push_back(&row);
  }
  std::optional<int64_t> previous_y;
  for (auto& [y, band] : bands) {
    if (previous_y && y < *previous_y + height) {
      throw std::invalid_argument("tap rows overlap vertically");
    }
    previous_y = y;
    std::sort(band.rows.begin(), band.rows.end(), [](auto a, auto b) { return a->x < b->x; });
    for (size_t i = 1; i < band.rows.size(); ++i) {
      if (band.rows[i - 1]->x + band.rows[i - 1]->count * sw > band.rows[i]->x) {
        throw std::invalid_argument("tap rows overlap horizontally");
      }
    }
  }
  bool one_site = false;
  for (const auto& [name, cell] : tech.macros) {
    if (cell.klass.starts_with("BLOCK") || cell.klass.starts_with("PAD") || cell.klass.starts_with("COVER")
        || cell.klass == "ENDCAP TOPLEFT" || cell.klass == "ENDCAP TOPRIGHT" || cell.klass == "ENDCAP BOTTOMLEFT"
        || cell.klass == "ENDCAP BOTTOMRIGHT" || cell.site.empty()) {
      continue;
    }
    const auto cell_site = tech.sites.find(cell.site);
    if (cell_site != tech.sites.end() && dbu(cell.width * frame.dbu) == dbu(cell_site->second.width * frame.dbu)) {
      one_site = true;
      break;
    }
  }
  std::set<std::string> names;
  for (const auto& inst : frame.components) {
    names.insert(inst.name);
    if (inst.place.status != ph::Status::FIXED && inst.place.status != ph::Status::LOCKED) {
      continue;
    }
    const auto& cell = tech.macros.at(inst.macro);
    if (cell.klass.starts_with("COVER")) {
      continue;
    }
    auto w = dbu(cell.width * frame.dbu), h = dbu(cell.height * frame.dbu);
    if (w <= 0 || h <= 0) {
      throw std::invalid_argument("fixed tap obstruction has invalid dimensions");
    }
    if (ph::swaps_axes(inst.place.orientation)) {
      std::swap(w, h);
    }
    const auto     x = dbu(double(inst.place.x)), y = dbu(double(inst.place.y));
    const ph::Rect rect{double(x), double(y), double(dbu(double(x) + w)), double(dbu(double(y) + h))};
    // Each fixed instance visits only the bands it intersects. Preserve input
    // order for OpenDB's first-obstacle relocation rule.
    for (auto it = bands.lower_bound(y - height + 1); it != bands.end() && it->first < y + h; ++it) {
      it->second.fixed.push_back(rect);
    }
  }
  // A horizontal row boundary is external if the adjacent band's union does
  // not cover the complete row span. This includes edges of macro row holes.
  const auto covered = [&](int64_t y, int64_t lo, int64_t hi) {
    const auto it = bands.find(y);
    if (it == bands.end()) {
      return false;
    }
    for (const auto* row : it->second.rows) {
      if (row->x > lo) {
        return false;
      }
      lo = std::max(lo, row->x + row->count * sw);
      if (lo >= hi) {
        return true;
      }
    }
    return false;
  };
  std::vector<ph::Component> result;
  for (const auto& [y, band] : bands) {
    for (const auto* row : band.rows) {
      const auto                               right  = row->x + row->count * sw;
      const bool                               edge   = !covered(y - height, row->x, right) || !covered(y + height, row->x, right);
      const int                                mult   = edge ? 1 : 2;
      const auto                               pitch  = (mult * distance / width) * width;
      const auto                               offset = row->orientation == ph::Orientation::N || edge ? pitch / mult : pitch;
      std::vector<std::pair<int64_t, int64_t>> occupied;
      for (const auto& rect : band.fixed) {
        // OpenDB intersects the row bbox shrunk by 1 DBU, including its boundary.
        if (rect.xl <= right - 1 && rect.xh >= row->x + 1) {
          occupied.emplace_back(int64_t(rect.xl), int64_t(rect.xh));
        }
      }
      size_t index = 0;
      for (auto x = row->x + offset; x < right; x += pitch) {
        x                               = align(x, sw, row->x, true);
        const auto             margin   = one_site ? 0 : sw + 1;
        std::optional<int64_t> location = x;
        bool                   overlap  = false;
        for (const auto& [lo, hi] : occupied) {
          if (x + width + margin > lo && x - margin < hi) {
            overlap = true;
            location.reset();
            if (x - margin < lo) {
              location = lo - width;
            } else if (x + width + margin > hi) {
              location = hi;
            }
            break;
          }
        }
        if (!location || *location < row->x || *location + width > right) {
          continue;
        }
        if (overlap && std::any_of(occupied.begin(), occupied.end(), [&](const auto& interval) {
              return *location < interval.second && *location + width > interval.first;
            })) {
          continue;
        }
        if ((*location - row->x) % sw != 0) {
          throw std::runtime_error("tap obstacle relocation is off the site grid");
        }
        const auto name = prefix + row->name + "_" + std::to_string(index++);
        if (!names.insert(name).second) {
          throw std::invalid_argument("tap instance name collision: " + name);
        }
        Ann_place place(*location, y, width, height);
        place.orientation = row->orientation;
        place.status      = ph::Status::LOCKED;
        result.push_back({name, master_name, place});
        occupied.emplace_back(*location, *location + width);
        x = *location;
      }
    }
  }
  return result;
}
std::optional<std::vector<physical::Component>> reserve_repeater_sites(const ph::Floorplan& frame, const ph::Technology& tech,
                                                                       const std::vector<Repeater_site_request>& requests,
                                                                       std::chrono::steady_clock::time_point     deadline) {
  if (frame.dbu <= 0 || !std::isfinite(frame.die.xl) || !std::isfinite(frame.die.yl) || !std::isfinite(frame.die.xh)
      || !std::isfinite(frame.die.yh) || frame.die.xl >= frame.die.xh || frame.die.yl >= frame.die.yh
      || deadline == std::chrono::steady_clock::time_point::max()) {
    throw std::invalid_argument("repeater sites require a valid die, DBU and finite deadline");
  }
  const auto expired   = [&] { return std::chrono::steady_clock::now() >= deadline; };
  const auto dimension = [&](double microns) {
    const double value   = microns * frame.dbu;
    const auto   rounded = dbu(value);
    if (rounded <= 0 || std::abs(value - rounded) > 1e-6) {
      throw std::invalid_argument("repeater site dimensions must be positive integer DBU");
    }
    return rounded;
  };
  std::set<std::string> names;
  std::vector<ph::Rect> occupied;
  for (const auto& cell : frame.components) {
    if (expired()) {
      return {};
    }
    if (!names.insert(cell.name).second || cell.place.status == ph::Status::UNPLACED) {
      throw std::invalid_argument("repeater reservation requires uniquely named placed components");
    }
    const auto& master = tech.macros.at(cell.macro);
    if (master.origin_x != 0 || master.origin_y != 0) {
      throw std::invalid_argument("repeater reservation does not support nonzero LEF origins");
    }
    auto w = dimension(master.width), h = dimension(master.height);
    if (ph::swaps_axes(cell.place.orientation)) {
      std::swap(w, h);
    }
    occupied.push_back({double(cell.place.x), double(cell.place.y), double(cell.place.x) + w, double(cell.place.y) + h});
  }
  struct Space {
    const ph::Row*                           row;
    int64_t                                  width, height, right;
    std::vector<std::pair<int64_t, int64_t>> blocked;
  };
  std::vector<Space> spaces;
  for (const auto& row : frame.rows) {
    if (expired()) {
      return {};
    }
    dbu(double(row.x));
    dbu(double(row.y));
    const auto& site = tech.sites.at(row.site);
    const auto  sw = dimension(site.width), rh = dimension(site.height);
    if (row.step != sw || row.count <= 0 || (row.orientation != ph::Orientation::N && row.orientation != ph::Orientation::FS)) {
      throw std::invalid_argument("repeater reservation requires contiguous N/FS rows");
    }
    const auto right = dbu(double(row.x) + double(row.count) * sw);
    spaces.push_back({&row, sw, rh, right, {}});
  }
  std::vector<Space*> rows_by_y;
  int64_t            max_row_height = 0;
  for (auto& space : spaces) {
    rows_by_y.push_back(&space);
    max_row_height = std::max(max_row_height, space.height);
  }
  std::stable_sort(rows_by_y.begin(), rows_by_y.end(), [](const auto* a, const auto* b) { return a->row->y < b->row->y; });
  const auto block = [&](const ph::Rect& rect) {
    // Most obstacles cover only one or two rows. Keep the original row order
    // for placement ties, but index overlap queries by row origin. The maximum
    // height keeps this conservative when the floorplan uses different sites.
    auto first = std::upper_bound(rows_by_y.begin(), rows_by_y.end(), rect.yl - max_row_height,
                                  [](double y, const auto* space) { return y < space->row->y; });
    for (auto it = first; it != rows_by_y.end() && (*it)->row->y < rect.yh; ++it) {
      auto&       space = **it;
      const auto& row = *space.row;
      if (rect.yl < double(row.y) + space.height && row.y < rect.yh) {
        // Outward-rounded obstacle edges: a partial site is unavailable.
        space.blocked.emplace_back(align(dbu(rect.xl), space.width, row.x, true), align(dbu(rect.xh), space.width, row.x, false));
      }
    }
  };
  for (const auto& rect : occupied) {
    if (expired()) {
      return {};
    }
    block(rect);
  }
  const auto permits = detail::repeater_wire_filter(tech, frame);
  std::vector<ph::Component> result;
  for (const auto& request : requests) {
    if (expired()) {
      return {};
    }
    if (request.name.empty() || !names.insert(request.name).second || !std::isfinite(request.center_x)
        || !std::isfinite(request.center_y)) {
      throw std::invalid_argument("invalid or duplicate repeater request");
    }
    const auto& master = tech.macros.at(request.master);
    if (!master.klass.starts_with("CORE") || master.virtual_proxy || master.origin_x != 0 || master.origin_y != 0) {
      throw std::invalid_argument("repeater master must be a physical CORE cell with zero origin");
    }
    const auto                   w = dimension(master.width), h = dimension(master.height);
    std::optional<ph::Component> best;
    double                       best_distance = std::numeric_limits<double>::infinity();
    for (auto& space : spaces) {
      if (expired()) {
        return {};
      }
      const auto& row = *space.row;
      if (master.site != row.site || h != space.height || w % space.width != 0
          || (row.orientation == ph::Orientation::FS && !master.symmetry_x) || row.y < frame.die.yl
          || double(row.y) + h > frame.die.yh) {
        continue;
      }
      std::sort(space.blocked.begin(), space.blocked.end());
      const auto consider = [&](int64_t lo, int64_t hi) {
        lo = std::max(lo, align(dbu(std::ceil(frame.die.xl)), space.width, row.x, false));
        hi = std::min(hi, align(dbu(std::floor(frame.die.xh)), space.width, row.x, true));
        if (hi - lo < w) {
          return;
        }
        const double desired = std::clamp(request.center_x - double(w) / 2, double(lo), double(hi - w));
        auto left = lo + int64_t(std::floor((desired - lo) / space.width)) * space.width;
        auto right = left + space.width;
        while (left >= lo || right <= hi - w) {
          if (expired()) {
            return;
          }
          // Match the old nearest-site tie: round halfway toward the right.
          const bool take_right = right <= hi - w && (left < lo || right - desired <= desired - left);
          const auto x = take_right ? right : left;
          if (take_right) {
            right += space.width;
          } else {
            left -= space.width;
          }
          const auto distance = std::abs(double(x) + double(w) / 2 - request.center_x)
                                + std::abs(double(row.y) + double(h) / 2 - request.center_y);
          if (distance >= best_distance) {
            return;
          }
          Ann_place place(x, row.y, w, h);
          place.orientation = row.orientation;
          place.status      = ph::Status::FIXED;
          ph::Component candidate{request.name, request.master, place};
          if (!permits || permits(candidate)) {
            best = std::move(candidate);
            best_distance = distance;
            return;
          }
        }
      };
      int64_t start = row.x;
      for (const auto& [lo, hi] : space.blocked) {
        if (lo >= space.right) {
          break;
        }
        if (hi <= start) {
          continue;
        }
        consider(start, std::min(lo, space.right));
        start = std::max(start, hi);
        if (start >= space.right) {
          break;
        }
      }
      consider(start, space.right);
    }
    if (!best) {
      return {};
    }
    block({double(best->place.x), double(best->place.y), double(best->place.x) + w, double(best->place.y) + h});
    result.push_back(std::move(*best));
  }
  if (expired()) {
    return {};
  }
  return result;
}

}  // namespace livehd::placement
