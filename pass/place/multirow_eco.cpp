// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "multirow_eco.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

#include "capacity_bins.hpp"
#include "multirow_packer.hpp"
#include "require.hpp"
#include "row_occupancy.hpp"

namespace livehd::placement {
namespace {
namespace ph = physical;
using multirow_internal::Packer;
using multirow_internal::Refusal;
using multirow_internal::start_master;
using Outcome = Multirow_eco_cell::Outcome;

// Movable single-row cells by row y, each row in ascending x: the cells a
// two-row pose may displace.
struct Single_rows {
  const ph::Floorplan&                   fp;
  std::map<int64_t, std::vector<size_t>> by_y;
  int64_t                                tallest = 0, widest = 0;
  Single_rows(const ph::Floorplan& frame, const std::vector<size_t>& cells) : fp(frame) {
    for (const size_t i : cells) {
      const auto& p = fp.components[i].place;
      by_y[p.y].push_back(i);
      tallest = std::max(tallest, p.height);
      widest  = std::max(widest, p.width);
    }
    for (auto& [y, row] : by_y) {
      std::sort(row.begin(), row.end(), [&](size_t a, size_t b) {
        return std::tie(fp.components[a].place.x, a) < std::tie(fp.components[b].place.x, b);
      });
    }
  }
  // Cells whose footprint overlaps r, by component index.
  std::vector<size_t> overlapping(const ph::Rect& r) const {
    std::vector<size_t> result;
    for (auto it = by_y.upper_bound(int64_t(std::floor(r.yl)) - tallest); it != by_y.end() && double(it->first) < r.yh; ++it) {
      const auto& row   = it->second;
      auto        first = std::partition_point(row.begin(), row.end(), [&](size_t i) {
        return double(fp.components[i].place.x) <= r.xl - double(widest);
      });
      for (; first != row.end() && double(fp.components[*first].place.x) < r.xh; ++first) {
        if (Capacity_bins::overlap(Capacity_bins::footprint(fp.components[*first].place), r) > 0) {
          result.push_back(*first);
        }
      }
    }
    std::sort(result.begin(), result.end());
    return result;
  }
};

struct Edit {
  size_t  cell;
  int64_t old_width;
};

// The fp.rows index of the row segment a cell starts on, if any.
std::optional<size_t> start_row(const ph::Floorplan& fp, const ph::Component& c, const std::string& site) {
  for (size_t r = 0; r < fp.rows.size(); ++r) {
    const auto& row = fp.rows[r];
    if (row.site == site && row.y == c.place.y && row.x <= c.place.x && c.place.x < row.x + row.count * row.step) {
      return r;
    }
  }
  return std::nullopt;
}

// Pose one edited cell of `proposal` (see multirow_eco.hpp). `pending` holds
// the edited cells not placed yet, by component, with their reference width:
// they stay obstacles at their reference footprint.
Multirow_eco_cell place_edit(const ph::Technology& tech, ph::Floorplan& proposal, const Edit& edit,
                             const std::map<size_t, int64_t>& pending, const Multirow_eco_options& options,
                             std::set<std::string>& displaced) {
  auto&             c         = proposal.components[edit.cell];
  const auto&       m         = tech.macros.at(c.macro);
  const auto        k         = multirow_rows(tech, m);
  const auto        step      = std::llround(tech.sites.at(m.site).width * tech.dbu);
  const int64_t     old_sites = (edit.old_width + step - 1) / step, new_sites = (c.place.width + step - 1) / step;
  Multirow_eco_cell entry;
  entry.name           = c.name;
  entry.released_sites = std::max<int64_t>(0, old_sites - new_sites) * int64_t(k);
  entry.grown_sites    = std::max<int64_t>(0, new_sites - old_sites) * int64_t(k);
  const auto row       = start_row(proposal, c, m.site);
  if (c.place.width <= edit.old_width && row) {
    if (const auto master = multirow_start_master(tech, proposal, *row, c.macro, options.anchor)) {
      entry.rail_twin = *master != c.macro;
      c.macro         = *master;
      return entry;  // 1. in place: a subset of its legal footprint
    }
  }

  // Every other multi-row cell is a fixed obstacle with its padding; the
  // movable single-row cells are displaceable, and count toward the density.
  auto scratch = proposal;
  for (size_t i = 0; i < scratch.components.size(); ++i) {
    auto& other = scratch.components[i];
    if (i != edit.cell && multirow_rows(tech, tech.macros.at(other.macro)) && frame_role(tech, other) == Frame_role::movable) {
      other.place.status = ph::Status::FIXED;
      if (const auto it = pending.find(i); it != pending.end()) {
        other.place.width = it->second;
      }
    }
  }
  Packer              packer(tech, scratch, options.anchor);
  std::vector<size_t> movable;
  for (size_t i = 0; i < scratch.components.size(); ++i) {
    if (i != edit.cell && frame_role(tech, scratch.components[i]) == Frame_role::movable) {
      movable.push_back(i);
      packer.grid.deposit(scratch.components[i].place, true);
    }
  }
  const Single_rows singles(scratch, movable);
  const auto        pad = options.anchor.occupancy.padding_sites;
  const auto overlaps   = [&](const Ann_place& p) { return singles.overlapping(padded_rect(tech, {c.name, c.macro, p}, pad)); };
  const auto fits       = [&](const Ann_place& p) { return packer.grid.fits(p, nullptr, options.anchor.occupancy.max_density); };

  // 2-3. Site-aligned x in its own start row, within shift_sites, free of
  // hard occupancy in every row it spans, nearest first, left first on ties.
  std::vector<std::pair<Ann_place, std::string>> candidates;  // pose and the master it needs
  const auto                                     height = c.place.height / int64_t(k);
  const auto&            site   = packer.site_rows(m.site, height);
  const auto             band   = (c.place.y - site.origin) / height;
  const auto             span
      = (c.place.y - site.origin) % height ? std::vector<const std::vector<size_t>*>{} : Packer::band_span(site, band, k);
  if (!span.empty()) {
    for (const size_t s : *span.front()) {
      const auto& start       = packer.rows[s];
      const auto  orientation = scratch.rows[start.source].orientation;
      Refusal     refusal     = Refusal::none;
      const auto* master      = start_master(tech,
                                        packer.rail_model,
                                        c.macro,
                                        k,
                                        size_t(band),
                                        orientation,
                                        packer.span_orientations(start, span),
                                        options.anchor,
                                        refusal);
      if (!master) {
        continue;
      }
      const int64_t sites = (c.place.width + start.step - 1) / start.step;
      const double  goal  = double(c.place.x - start.x) / start.step;
      const int64_t lo = std::ceil(goal - options.bounds.shift_sites), hi = std::floor(goal + options.bounds.shift_sites);
      for (const auto& [first, last] : packer.common_gaps(start, span, lo - pad, hi + sites + pad)) {
        // The cell starts at site j and keeps [j - pad, j + sites + pad) free.
        for (int64_t j = std::max<int64_t>(first + pad, lo); j <= std::min<int64_t>(last - sites - pad, hi); ++j) {
          Ann_place p   = c.place;
          p.x           = start.x + j * start.step;
          p.orientation = orientation;
          candidates.emplace_back(p, *master);
        }
      }
    }
  }
  std::sort(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b) {
    return std::tuple(std::abs(a.first.x - c.place.x), a.first.x) < std::tuple(std::abs(b.first.x - c.place.x), b.first.x);
  });
  // A pose that displaces nothing wins over any nearer one that displaces.
  std::optional<Ann_place> chosen;
  std::string              chosen_master;
  std::vector<size_t>      overlapped;
  for (const auto& [p, master] : candidates) {
    if (!fits(p)) {
      continue;
    }
    auto hit = overlaps(p);
    if (hit.empty()) {
      chosen        = p;
      chosen_master = master;
      overlapped    = {};
      entry.outcome = p.x == c.place.x ? Outcome::in_place : Outcome::shifted;
      break;
    }
    if (!chosen && hit.size() <= options.bounds.max_displaced) {
      chosen        = p;
      chosen_master = master;
      overlapped    = std::move(hit);
      entry.outcome = Outcome::displaced;
    }
  }

  // 4. The anchor packer, for this cell alone, with the single-row cells
  // reserved too: the nearest pose that displaces none.
  if (!chosen) {
    for (const size_t i : movable) {
      reserve_gaps(packer.rows, Capacity_bins::footprint(scratch.components[i].place));
    }
    try {
      packer.anchor(edit.cell);
    } catch (const Multirow_failure& error) {
      throw Multirow_failure("two-row ECO cannot place " + c.name + " (" + c.macro + "): no pose within "
                             + std::to_string(options.bounds.shift_sites) + " sites of its start row displaces at most "
                             + std::to_string(options.bounds.max_displaced) + " single-row cells, and " + error.what());
    }
    chosen        = scratch.components[edit.cell].place;
    chosen_master = scratch.components[edit.cell].macro;
    entry.outcome = Outcome::re_anchored;
  }
  entry.rail_twin       = chosen_master != c.macro;
  c.macro               = chosen_master;  // its rail twin where the new start needs the other parity
  entry.displacement_um = (std::abs(double(chosen->x - c.place.x)) + std::abs(double(chosen->y - c.place.y))) / double(tech.dbu);
  entry.displaced_cells = overlapped.size();
  c.place.x             = chosen->x;
  c.place.y             = chosen->y;
  c.place.orientation   = chosen->orientation;
  for (const size_t i : overlapped) {
    displaced.insert(proposal.components[i].name);
  }
  return entry;
}
}  // namespace

std::string_view multirow_eco_outcome_name(Outcome outcome) {
  switch (outcome) {
    case Outcome::in_place   : return "in_place";
    case Outcome::shifted    : return "shifted";
    case Outcome::displaced  : return "displaced";
    case Outcome::re_anchored: return "re_anchored";
  }
  return "unknown";
}

Multirow_eco_report legalize_multirow_edits(const ph::Technology& tech, ph::Floorplan& fp, const ph::Floorplan& reference,
                                            const std::vector<std::string>& edited, const Multirow_eco_options& options) {
  multirow_internal::require_controls(tech, fp, options.anchor);
  std::map<std::string, size_t> index;
  for (size_t i = 0; i < fp.components.size(); ++i) {
    const auto& c = fp.components[i];
    require(tech.macros.contains(c.macro) && c.place.is_valid(), "multirow packing requires known masters and seeded cells");
    if (!index.emplace(c.name, i).second) {
      throw std::invalid_argument("two-row ECO frame repeats a component: " + c.name);
    }
  }
  std::map<std::string, const ph::Component*> old;
  for (const auto& c : reference.components) {
    old.emplace(c.name, &c);
  }
  std::vector<Edit> edits;
  for (const auto& name : std::set<std::string>(edited.begin(), edited.end())) {
    const auto it = index.find(name);
    if (it == index.end()) {
      throw std::invalid_argument("two-row ECO edited cell is not in the frame: " + name);
    }
    const auto& c = fp.components[it->second];
    const auto& m = tech.macros.at(c.macro);
    const auto  k = multirow_rows(tech, m);
    if (!k || frame_role(tech, c) != Frame_role::movable) {
      throw std::invalid_argument("two-row ECO edits movable multi-row CORE cells only: " + name);
    }
    if (c.place.height != int64_t(k) * std::llround(tech.sites.at(m.site).height * tech.dbu)
        || ph::swaps_axes(c.place.orientation)) {
      throw std::invalid_argument("multirow cell footprint differs from its LEF rows: " + name);
    }
    const auto before = old.find(name);
    if (before == old.end()) {
      throw std::invalid_argument("two-row ECO edited cell is absent from the reference: " + name);
    }
    edits.push_back({it->second, before->second->place.width});
  }
  std::sort(edits.begin(), edits.end(), [&](const Edit& a, const Edit& b) {
    const auto &ca = fp.components[a.cell], &cb = fp.components[b.cell];
    return std::tie(cb.place.width, ca.name) < std::tie(ca.place.width, cb.name);
  });
  std::map<size_t, int64_t> pending;
  for (const auto& edit : edits) {
    pending.emplace(edit.cell, edit.old_width);
  }
  auto                  proposal = fp;
  Multirow_eco_report   report;
  std::set<std::string> displaced;
  for (const auto& edit : edits) {
    pending.erase(edit.cell);
    report.cells.push_back(place_edit(tech, proposal, edit, pending, options, displaced));
  }
  report.displaced.assign(displaced.begin(), displaced.end());
  fp = std::move(proposal);
  return report;
}
}  // namespace livehd::placement
