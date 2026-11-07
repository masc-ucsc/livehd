// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <queue>
#include <set>
#include <tuple>

#include "cell_criticality.hpp"
#include "core/json_util.hpp"
#include "legroute_internal.hpp"

namespace livehd::placement::lr {
namespace {
bool ordinary(const Net& n) { return !n.clock && !n.power && !n.reset && !n.control_scope; }

std::vector<int> nearest(const Grid& g, double x, double y, unsigned count, int span) {
  using Entry = std::pair<double, int>;
  std::priority_queue<Entry, std::vector<Entry>, std::greater<>> queue;
  std::set<int>                                                  seen;
  const auto                                                     add = [&](int t) {
    if (t < 0 || !seen.insert(t).second) {
      return;
    }
    const double cx = g.left(g.col(t)) + span * g.tile_w / 2.;
    queue.emplace(std::abs(x - cx) + std::abs(y - g.cy(g.row(t))), t);
  };
  add(g.tile_of(x - (span - 1) * g.tile_w / 2., y));
  std::vector<int> out;
  while (!queue.empty() && out.size() < count) {
    const int t = queue.top().second;
    queue.pop();
    // Rank geometric footprints first. Occupancy, P/C, PG and fit do not let
    // this bounded search skip a nearby rejection in favor of distant space.
    if (g.col(t) + span <= g.cols) {
      out.push_back(t);
    }
    add(g.col(t) ? t - 1 : -1);
    add(g.col(t) + 1 < g.cols ? t + 1 : -1);
    add(g.row(t) ? t - g.cols : -1);
    add(g.row(t) + 1 < g.rows ? t + g.cols : -1);
  }
  return out;
}

std::vector<int64_t> available_tracks(const Grid& g, int tile, std::string_view name) {
  const auto found = std::ranges::find(g.resources.layers, name, &Track_resource_layer::name);
  if (found == g.resources.layers.end() || size_t(tile) >= found->tiles.size()) {
    return {};
  }
  const auto& record = found->tiles[size_t(tile)];
  if (record.whole_band_exclusions) {
    return {};
  }
  std::vector<int64_t> available;
  const auto&          coordinates = found->bands.at(record.band);
  std::set<size_t>     excluded;
  for (const auto& e : record.exclusions) {
    if (e.reasons) {
      excluded.insert(e.track);
    }
  }
  for (size_t i = 0; i < coordinates.size(); ++i) {
    if (!excluded.contains(i)) {
      available.push_back(coordinates[i]);
    }
  }
  // Anonymous legacy headroom remains unavailable. This is deliberately
  // conservative; it must never be reassigned as a promised local track.
  if (available.size() < record.anonymous_headroom) {
    return {};
  }
  available.erase(available.begin(), available.begin() + record.anonymous_headroom);
  return available;
}

std::vector<int64_t> reserved(const Grid& g, int tile, std::string_view name, int span) {
  auto available = available_tracks(g, tile, name);
  for (int k = 1; k < span; ++k) {
    const auto           other = available_tracks(g, tile + k, name);
    std::vector<int64_t> combined;
    if (name == "M2") {
      std::ranges::set_intersection(available, other, std::back_inserter(combined));
    } else {
      std::ranges::set_union(available, other, std::back_inserter(combined));
    }
    available = std::move(combined);
  }
  if (available.size() < 20) {
    return {};
  }
  std::vector<int64_t> selected;
  for (size_t i = 0; i < 20; ++i) {
    selected.push_back(available[i * (available.size() - 1) / 19]);
  }
  return selected;
}

void counts(const Design& d, Cellbox_slot& box) {
  std::map<int, unsigned> local;
  for (int m : box.cells) {
    const int component = d.movable.at(size_t(m));
    for (int n : d.nets_of.at(size_t(m))) {
      if (!ordinary(d.nets[size_t(n)])) {
        continue;
      }
      for (const auto& p : d.nets[size_t(n)].pins) {
        if (p.comp == component) {
          ++local[n];
        }
      }
    }
  }
  box.external_pins = box.connections = box.internal_nets = 0;
  for (const auto& [n, k] : local) {
    box.connections += k - 1;
    if (k < d.nets[size_t(n)].pins.size()) {
      box.external_pins += k;
    } else {
      ++box.internal_nets;
    }
  }
}

bool pack(const Design& d, const Grid& g, Cellbox_slot& box) {
  std::vector<Seg> segments;
  for (int col = 0; col < box.span; ++col) {
    for (auto s : g.segs(box.tile + col)) {
      s.lo += col * g.sites;
      s.hi += col * g.sites;
      segments.push_back(s);
    }
  }
  std::ranges::sort(segments);
  std::vector<Seg> merged;
  for (const auto& s : segments) {
    if (!merged.empty() && merged.back().row == s.row && merged.back().hi == s.lo) {
      merged.back().hi = s.hi;
    } else {
      merged.push_back(s);
    }
  }
  std::vector<int> widths, room;
  for (int m : box.cells) {
    const auto& macro = *d.macro.at(size_t(d.movable.at(size_t(m))));
    if (std::llround(macro.height * d.fp.dbu) != g.row_h) {
      return false;
    }
    widths.push_back(cell_sites(d, g, m));
  }
  for (const auto& s : merged) {
    room.push_back(s.hi - s.lo);
  }
  const auto assignment = exact_pack_segments(widths, room);
  if (assignment.size() != widths.size()) {
    return false;
  }
  std::vector<int> next;
  for (const auto& s : merged) {
    next.push_back(s.lo);
  }
  box.poses.clear();
  for (size_t i = 0; i < assignment.size(); ++i) {
    const size_t j      = size_t(assignment[i]);
    const int    row    = merged[j].row;
    const auto   orient = g.row_orient.at(size_t(g.row(box.tile) * g.rows_per_tile + row));
    const auto&  macro  = *d.macro.at(size_t(d.movable.at(size_t(box.cells[i]))));
    if (!physical::orientation_allowed(macro, orient)) {
      return false;
    }
    box.poses.push_back({box.cells[i],
                         row,
                         next[j],
                         g.left(g.col(box.tile)) + int64_t(next[j]) * g.site,
                         g.bottom(g.row(box.tile)) + row * g.row_h,
                         orient});
    next[j] += widths[i];
  }
  return true;
}

void write_boxes(std::ostream& out, const Design& d, const std::vector<Cellbox_slot>& slots) {
  out << '[';
  for (size_t i = 0; i < slots.size(); ++i) {
    const auto& box = slots[i];
    out << (i ? "," : "") << "{\"id\":" << i << ",\"tile\":" << box.tile << ",\"span_columns\":" << box.span
        << ",\"P\":" << box.external_pins << ",\"C\":" << box.connections << ",\"internal_nets\":" << box.internal_nets;
    const auto coordinates = [&](std::string_view key, const auto& values) {
      out << ",\"" << key << "\":[";
      for (size_t k = 0; k < values.size(); ++k) {
        out << (k ? "," : "") << values[k];
      }
      out << ']';
    };
    coordinates("reserved_M2_y", box.reserved_h);
    coordinates("reserved_M3_x", box.reserved_v);
    out << ",\"placement_witness\":[";
    for (size_t k = 0; k < box.poses.size(); ++k) {
      const auto& p = box.poses[k];
      out << (k ? "," : "") << "{\"component\":" << d.movable[size_t(p.movable)] << ",\"row\":" << p.row << ",\"site\":" << p.site
          << ",\"x\":" << p.x << ",\"y\":" << p.y << ",\"orientation\":\"" << physical::orientation_name(p.orientation) << "\"}";
    }
    out << "]}";
  }
  out << ']';
}

void write_admission(const Design& d, const Grid& g, const Cellbox_admission& a, const Legroute_plan_report& r,
                     const timing::Feedback& timing) {
  if (d.opt.output_dir.empty()) {
    return;
  }
  std::filesystem::create_directories(d.opt.output_dir);
  std::ofstream out(d.opt.output_dir + "/critical_cellboxes.json");
  out << std::boolalpha << std::setprecision(17);
  double body          = 0;
  size_t ordinary_pins = 0;
  for (int c : d.movable) {
    body += d.macro[size_t(c)]->width * d.macro[size_t(c)]->height * d.fp.dbu * d.fp.dbu;
  }
  for (const auto& n : d.nets) {
    if (ordinary(n)) {
      for (const auto& p : n.pins) {
        ordinary_pins += p.comp >= 0 && d.cell[size_t(p.comp)] >= 0;
      }
    }
  }
  const double                     grid_area     = double(g.size()) * g.tile_w * g.tile_h;
  const bool                       zero_pressure = std::ranges::all_of(r.component_pressure, [](double p) { return p <= 1; });
  std::vector<const Cellbox_pose*> assigned(d.movable.size(), nullptr);
  std::vector<char>                missing_after_regular(d.movable.size(), false);
  for (int m : a.regular_unassigned) {
    missing_after_regular[size_t(m)] = true;
  }
  double total_displacement = 0, max_displacement = 0;
  size_t moved = 0;
  for (const auto& box : a.slots) {
    for (const auto& p : box.poses) {
      assigned[size_t(p.movable)]  = &p;
      const auto&  raw             = d.fp.components[size_t(d.movable[size_t(p.movable)])].place;
      const double distance        = std::abs(double(p.x) - raw.x) + std::abs(double(p.y) - raw.y);
      total_displacement          += distance;
      max_displacement             = std::max(max_displacement, distance);
      moved                       += distance > 0 || p.orientation != raw.orientation;
    }
  }
  out << "{\"schema_version\":1,\"mode\":\"timing_priority_fixed_cellboxes\",\"local_routing_run\":false,"
         "\"physical_route_certificate\":false,\"handoff_ready\":"
      << (a.unassigned.empty() && r.route_attempted && !r.gates.requires_replan() && zero_pressure)
      << ",\"route_attempted\":" << r.route_attempted << ",\"zero_pressure\":" << zero_pressure << ",\"grid\":{\"x0\":" << g.x0
      << ",\"y0\":" << g.y0 << ",\"cols\":" << g.cols << ",\"rows\":" << g.rows << ",\"tile_width\":" << g.tile_w
      << ",\"tile_height\":" << g.tile_h << ",\"rows_per_tile\":" << g.rows_per_tile << ",\"site\":" << g.site
      << "},\"movable_cells\":" << d.movable.size() << ",\"assigned_cells\":" << d.movable.size() - a.unassigned.size()
      << ",\"occupied_boxes\":" << a.slots.size() << ",\"ordinary_physical_pins\":" << ordinary_pins
      << ",\"reservation_semantics\":\"20 usable M2 and M3 slots deducted per regular grid tile before routing; exact listed local "
         "coordinates are frozen; wide footprints retain both tiles deductions\""
      << ",\"wide_exception_boxes\":" << a.wide_exceptions << ",\"grid_slots\":" << g.size() << ",\"body_area_dbu2\":" << body
      << ",\"allocated_grid_area_dbu2\":" << grid_area << ",\"body_utilization\":" << (grid_area > 0 ? body / grid_area : 0)
      << ",\"P_cap\":" << d.opt.cellbox_external_pins << ",\"C_cap\":" << d.opt.cellbox_connections
      << ",\"timing_basis\":\"raw_GP_pose_only_not_assigned_witness\",\"assigned_witness_timing_unmeasured\":true"
      << ",\"assignment_displacement\":{\"basis\":\"raw_origin_to_row_packing_origin_Manhattan_DBU\",\"moved_cells\":" << moved
      << ",\"total_dbu\":" << total_displacement << ",\"max_dbu\":" << max_displacement << '}'
      << ",\"far_fallback\":{\"enabled\":" << d.opt.cellbox_far_fallback << ",\"final_retry\":" << d.opt.cellbox_final_retry
      << ",\"attempted\":" << a.fallback_attempted << ",\"basis\":\"latest_round3_raw_GP_regular_admission\""
      << ",\"search\":\"all_geometric_candidates_nearest_first_rechecking_near\",\"preserved_existing_box_membership\":true"
      << ",\"destination_row_repacking_allowed\":true,\"unassigned_before\":" << a.regular_unassigned.size()
      << ",\"unassigned_after\":" << a.unassigned.size() << ",\"cells_added\":" << a.fallback_assigned
      << ",\"candidate_attempts\":" << a.fallback_attempts << ",\"extra_GP_retries\":0}"
      << ",\"timing_ranking_only\":" << timing.ranking_only << ",\"wns_ps\":";
  if (!timing.ranking_only && timing.wns && std::isfinite(*timing.wns)) {
    out << *timing.wns * timing.time_unit_ps;
  } else {
    out << "null";
  }
  out << ",\"C_definition\":\"sum_over_ordinary_nets(max(local_physical_pins-1,0))\",\"candidate_attempts\":" << a.attempts
      << ",\"candidate_counter_semantics\":\"cumulative_regular_plus_fallback\""
      << ",\"rejections\":{\"geometry\":" << a.rejected_geometry << ",\"policy\":" << a.rejected_policy
      << ",\"resources\":" << a.rejected_resources << "},\"cells\":[";
  for (size_t k = 0; k < a.order.size(); ++k) {
    const int m = a.order[k], c = d.movable[size_t(m)];
    out << (k ? "," : "") << "{\"component\":" << c << ",\"name\":\"" << json_util::escape(d.fp.components[size_t(c)].name)
        << "\",\"priority\":" << k << ",\"unknown_timing\":" << bool(a.unknown[size_t(m)])
        << ",\"critical\":" << bool(a.critical[size_t(m)]) << ",\"slack_ps\":";
    if (std::isfinite(a.slack_ps[size_t(m)])) {
      out << a.slack_ps[size_t(m)];
    } else {
      out << "null";
    }
    out << ",\"box\":" << a.boxes.box_of[size_t(m)] << ",\"pressure\":" << r.component_pressure[size_t(c)]
        << ",\"direct_pressure\":" << a.direct_component_pressure[size_t(c)]
        << ",\"body_overlap_pressure\":" << a.body_overlap_pressure[size_t(c)] << ",\"near_limit\":" << a.near_limit[size_t(m)]
        << ",\"candidate_rank\":" << a.candidate_rank[size_t(m)]
        << ",\"candidate_center_distance_dbu\":" << a.candidate_distance[size_t(m)] << ",\"added_by_fallback\":"
        << (a.fallback_attempted && a.boxes.box_of[size_t(m)] >= 0 && missing_after_regular[size_t(m)]);
    if (assigned[size_t(m)]) {
      const auto& p   = *assigned[size_t(m)];
      const auto& raw = d.fp.components[size_t(c)].place;
      out << ",\"assigned_x\":" << p.x << ",\"assigned_y\":" << p.y << ",\"assignment_dx_dbu\":" << p.x - raw.x
          << ",\"assignment_dy_dbu\":" << p.y - raw.y;
    }
    out << '}';
  }
  out << "],\"source_frame\":\"input.def\",\"timing_snapshot\":\"timing_snapshot.json\",\"excluded_controls\":[";
  bool first_control = true;
  for (const auto& n : d.nets) {
    if (!ordinary(n)) {
      out << (first_control ? "" : ",") << "{\"source_net\":" << n.frame << ",\"name\":\""
          << json_util::escape(d.fp.nets[n.frame].name) << "\",\"native_prects_trace\":" << n.control_scope << '}';
      first_control = false;
    }
  }
  out << "],\"pressure_policy\":\"max_direct_or_positive_body_overlap_of_active_failed_admission_or_route_tile;edge_touch_excluded;"
         "critical_cells_included\""
      << ",\"pressure_tiles\":[";
  bool first_tile = true;
  for (size_t t = 0; t < a.tile_pressure.size(); ++t) {
    if (a.tile_pressure[t] > 1) {
      out << (first_tile ? "" : ",") << "{\"tile\":" << t << ",\"ratio\":" << a.tile_pressure[t] << '}';
      first_tile = false;
    }
  }
  out << "],\"boxes\":";
  write_boxes(out, d, a.slots);
  out << "}\n";
  if (!out) {
    throw std::runtime_error("cannot write critical cellbox handoff");
  }
  if (a.fallback_attempted) {
    std::ofstream before(d.opt.output_dir + "/critical_cellboxes_before_fallback.json");
    before << "{\"basis\":\"latest_round3_raw_GP_regular_admission\",\"source_frame\":\"input.def\",\"timing_snapshot\":\"timing_"
              "snapshot.json\""
           << ",\"candidate_attempts\":" << a.regular_attempts << ",\"rejections\":{\"geometry\":" << a.regular_rejected_geometry
           << ",\"policy\":" << a.regular_rejected_policy << ",\"resources\":" << a.regular_rejected_resources
           << "},\"unassigned_components\":[";
    for (size_t k = 0; k < a.regular_unassigned.size(); ++k) {
      before << (k ? "," : "") << d.movable[size_t(a.regular_unassigned[k])];
    }
    before << "],\"boxes\":";
    write_boxes(before, d, a.regular_slots);
    before << "}\n";
    if (!before) {
      throw std::runtime_error("cannot write pre-fallback admission witness");
    }
    if (a.unassigned.empty()) {
      auto witness = d.fp;
      for (const auto& box : a.slots) {
        for (const auto& p : box.poses) {
          auto& pose       = witness.components[size_t(d.movable[size_t(p.movable)])].place;
          pose.x           = p.x;
          pose.y           = p.y;
          pose.orientation = p.orientation;
        }
      }
      physical::write_def(witness, d.opt.output_dir + "/assigned_witness.def");
    }
  }
}
}  // namespace

std::vector<double> cellbox_body_pressure(const Design& d, const Grid& g, const std::vector<double>& tile_pressure) {
  if (tile_pressure.size() != g.size()) {
    throw std::invalid_argument("cellbox pressure has the wrong fixed grid");
  }
  std::vector<double> out(d.fp.components.size(), 1);
  for (int c : d.movable) {
    const auto& cell = d.fp.components[size_t(c)];
    if (cell.place.status == physical::Status::FIXED || cell.place.status == physical::Status::LOCKED) {
      continue;
    }
    const auto&  macro   = *d.macro[size_t(c)];
    const bool   rotated = physical::swaps_axes(cell.place.orientation);
    const double xl = cell.place.x, yl = cell.place.y;
    const double xh = xl + (rotated ? macro.height : macro.width) * d.fp.dbu;
    const double yh = yl + (rotated ? macro.width : macro.height) * d.fp.dbu;
    for (int row = g.row_of(yl); row <= g.row_of(yh); ++row) {
      for (int col = g.col_of(xl); col <= g.col_of(xh); ++col) {
        if (std::min(xh, double(g.left(col) + g.tile_w)) <= std::max(xl, double(g.left(col)))
            || std::min(yh, double(g.bottom(row) + g.tile_h)) <= std::max(yl, double(g.bottom(row)))) {
          continue;
        }
        out[size_t(c)] = std::max(out[size_t(c)], tile_pressure[size_t(g.tile(col, row))]);
      }
    }
  }
  return out;
}

Cellbox_admission admit_critical_cellboxes(const Design& d, const Grid& g, const timing::Feedback& timing) {
  if (d.opt.cellbox_rows != 4 || g.rows_per_tile != 4 || d.opt.cellbox_sites != 22 || !d.opt.cellbox_critical_choices
      || d.opt.cellbox_critical_choices > 4 || !d.opt.cellbox_other_choices || d.opt.cellbox_other_choices > 8) {
    throw std::invalid_argument("critical cellboxes require four rows and nearest choices in 1..4 / 1..8");
  }
  Cellbox_admission out;
  out.boxes.box_of.assign(d.movable.size(), -1);
  out.component_pressure.assign(d.fp.components.size(), 1);
  out.tile_pressure.assign(g.size(), 1);
  out.candidate_rank.assign(d.movable.size(), 0);
  out.near_limit.resize(d.movable.size());
  out.candidate_distance.assign(d.movable.size(), 0);
  out.slack_ps.assign(d.movable.size(), INFINITY);
  out.unknown.assign(d.movable.size(), true);
  out.critical.assign(d.movable.size(), false);
  std::vector<bool> ignored(d.fp.nets.size(), true);
  for (const auto& net : d.nets) {
    if (ordinary(net)) {
      ignored[net.frame] = false;
    }
  }
  const auto timing_cells = Cell_criticality::from(timing, d.fp, &ignored);
  for (size_t m = 0; m < d.movable.size(); ++m) {
    const auto c = size_t(d.movable[m]);
    if (timing_cells.slack_ps[c]) {
      out.slack_ps[m] = *timing_cells.slack_ps[c];
    }
    out.unknown[m] = !timing_cells.fully_timed(c);
    for (int n : d.nets_of[m]) {
      if (ordinary(d.nets[size_t(n)])) {
        out.critical[m] |= d.nets[size_t(n)].critical;
      }
    }
    out.order.push_back(int(m));
  }
  std::ranges::sort(out.order, [&](int a, int b) {
    // Rare wide cells reserve their real multi-tile footprint before ordinary
    // admission. Within each class the current snapshot determines priority.
    return std::tuple{d.width[size_t(a)] <= g.tile_w,
                      !out.critical[size_t(a)],
                      out.slack_ps[size_t(a)],
                      d.fp.components[size_t(d.movable[size_t(a)])].name}
           < std::tuple{d.width[size_t(b)] <= g.tile_w,
                        !out.critical[size_t(b)],
                        out.slack_ps[size_t(b)],
                        d.fp.components[size_t(d.movable[size_t(b)])].name};
  });
  std::vector<int>           owner(g.size(), -1);
  std::vector<std::set<int>> blocking_slots(d.movable.size());
  const auto                 admit = [&](int m, bool far) {
    const size_t   c     = size_t(d.movable[size_t(m)]);
    const auto&    macro = *d.macro[c];
    const int      span  = int((d.width[size_t(m)] + g.tile_w - 1) / g.tile_w);
    const unsigned limit
        = out.critical[size_t(m)] || out.unknown[size_t(m)] ? d.opt.cellbox_critical_choices : d.opt.cellbox_other_choices;
    out.near_limit[size_t(m)] = limit;
    const double raw_cx       = d.fp.components[c].place.x + macro.width * d.fp.dbu / 2;
    const double raw_cy       = d.fp.components[c].place.y + macro.height * d.fp.dbu / 2;
    const auto   candidates   = nearest(g,
                                        d.fp.components[c].place.x + macro.width * d.fp.dbu / 2,
                                        d.fp.components[c].place.y + macro.height * d.fp.dbu / 2,
                                        far ? unsigned(g.size()) : limit,
                                        span);
    // Recheck the nearest candidates too: later regular admissions may have
    // internalized a net and changed a destination's P/C feasibility.
    for (size_t rank = 0; rank < candidates.size(); ++rank) {
      const int tile = candidates[rank];
      ++out.attempts;
      out.fallback_attempts += far;
      if (span > 2 || (span > 1 && !d.opt.cellbox_wide_exceptions)) {
        ++out.rejected_geometry;
        continue;
      }
      int  index    = owner[size_t(tile)];
      bool occupied = false, unavailable = false;
      for (int k = 0; k < span; ++k) {
        const int previous = owner[size_t(tile + k)];
        if (previous >= 0) {
          blocking_slots[size_t(m)].insert(previous);
        }
        occupied    |= previous >= 0 && (span > 1 || out.slots[size_t(previous)].span > 1);
        unavailable |= g.blocked[size_t(tile + k)] || (!g.box_short.empty() && g.box_short[size_t(tile + k)]);
      }
      if (occupied) {
        ++out.rejected_geometry;
        continue;
      }
      if (unavailable) {
        ++out.rejected_resources;
        continue;
      }
      Cellbox_slot proposal = index >= 0 ? out.slots[size_t(index)] : Cellbox_slot{};
      proposal.tile         = tile;
      proposal.span         = span;
      proposal.cells.push_back(m);
      counts(d, proposal);
      if (proposal.external_pins > d.opt.cellbox_external_pins || proposal.connections > d.opt.cellbox_connections) {
        ++out.rejected_policy;
        continue;
      }
      if (!pack(d, g, proposal)) {
        ++out.rejected_geometry;
        continue;
      }
      proposal.reserved_h = reserved(g, tile, "M2", span);
      proposal.reserved_v = reserved(g, tile, "M3", span);
      if (proposal.reserved_h.size() != 20 || proposal.reserved_v.size() != 20) {
        ++out.rejected_resources;
        continue;
      }
      if (index < 0) {
        index = int(out.slots.size());
        out.slots.push_back(std::move(proposal));
        out.wide_exceptions += span > 1;
      } else {
        out.slots[size_t(index)] = std::move(proposal);
      }
      for (int k = 0; k < span; ++k) {
        owner[size_t(tile + k)] = index;
      }
      out.boxes.box_of[size_t(m)]   = index;
      out.candidate_rank[size_t(m)] = unsigned(rank + 1);
      out.candidate_distance[size_t(m)]
          = std::abs(raw_cx - (g.left(g.col(tile)) + span * g.tile_w / 2.)) + std::abs(raw_cy - g.cy(g.row(tile)));
      out.fallback_assigned += far;
      return true;
    }
    return false;
  };
  for (int m : out.order) {
    if (!admit(m, false)) {
      out.unassigned.push_back(m);
    }
  }
  out.regular_unassigned         = out.unassigned;
  out.regular_attempts           = out.attempts;
  out.regular_rejected_geometry  = out.rejected_geometry;
  out.regular_rejected_policy    = out.rejected_policy;
  out.regular_rejected_resources = out.rejected_resources;
  if (d.opt.cellbox_far_fallback && d.opt.cellbox_final_retry && !out.unassigned.empty()) {
    out.fallback_attempted = true;
    out.regular_slots      = out.slots;
    out.unassigned.clear();
    for (int m : out.regular_unassigned) {
      if (!admit(m, true)) {
        out.unassigned.push_back(m);
      }
    }
  }
  // Rebuild final pressure only from still-missing cells. Successfully
  // extended admissions must not retain the failed near search's pressure.
  for (int m : out.unassigned) {
    const size_t c            = size_t(d.movable[size_t(m)]);
    out.component_pressure[c] = std::max(out.component_pressure[c], 1.1);
    for (int index : blocking_slots[size_t(m)]) {
      const auto& box = out.slots[size_t(index)];
      for (int k = 0; k < box.span; ++k) {
        auto& p = out.tile_pressure[size_t(box.tile + k)];
        p       = std::min(1.5, p + .1);
      }
      for (int member : box.cells) {
        auto& p = out.component_pressure[size_t(d.movable[size_t(member)])];
        p       = std::min(1.5, p + .1);
      }
    }
  }
  out.direct_component_pressure = out.component_pressure;
  out.body_overlap_pressure     = cellbox_body_pressure(d, g, out.tile_pressure);
  for (size_t c = 0; c < out.component_pressure.size(); ++c) {
    out.component_pressure[c] = std::max(out.component_pressure[c], out.body_overlap_pressure[c]);
  }
  for (const auto& slot : out.slots) {
    Box box;
    box.cells    = slot.cells;
    box.tile     = slot.tile;
    box.cx       = g.left(g.col(slot.tile)) + slot.span * g.tile_w / 2.;
    box.cy       = g.cy(g.row(slot.tile));
    box.fixed    = true;
    box.external = int(slot.external_pins);
    box.internal = int(slot.internal_nets);
    for (int m : slot.cells) {
      box.sites += cell_sites(d, g, m);
    }
    out.boxes.boxes.push_back(std::move(box));
  }
  return out;
}

Legroute_plan_report critical_cellbox_plan(const Design& d, const Grid& g, const timing::Feedback& timing, size_t net_pops,
                                           Legroute_congestion* congestion) {
  const auto start = Clock::now();
  if (timing.ranking_only || !std::isfinite(timing.time_unit_ps) || timing.time_unit_ps <= 0) {
    throw std::invalid_argument("critical cellbox Plan requires a detailed timing snapshot in valid physical units");
  }
  auto                 admission = admit_critical_cellboxes(d, g, timing);
  Legroute_plan_report out;
  out.endpoint_basis     = "critical_cellbox_fixed_tiles";
  out.component_pressure = admission.direct_component_pressure;
  auto tile_pressure     = admission.tile_pressure;
  out.component_hot_distance.assign(d.fp.components.size(), std::numeric_limits<unsigned>::max());
  out.tile_width_dbu      = g.tile_w;
  out.tile_height_dbu     = g.tile_h;
  out.fit_matched         = admission.unassigned.empty();
  out.fit_applied         = true;
  out.fit_reason          = out.fit_matched ? "critical_cell_admission" : "cell_admission_deficit";
  out.fit_movable_boxes   = admission.slots.size();
  out.fit_free_tiles      = std::ranges::count(g.blocked, uint8_t{0});
  out.gates.density_limit = d.opt.density_ceiling;
  if (congestion) {
    *congestion = {};
  }
  for (const auto& n : d.nets) {
    if (ordinary(n) && n.routed) {
      ++out.signals;
      out.timed_signals += std::isfinite(n.slack);
    }
  }
  if (!admission.unassigned.empty()) {
    out.route_skip_reason        = Legroute_plan_report::Route_skip_reason::window_density;
    out.unchecked_signals        = out.signals;
    out.gates.cell_dense_regions = admission.unassigned.size();
    out.gates.unfittable_boxes   = admission.unassigned.size();
  } else {
    auto nets = all_signal_requests(d, g, admission.boxes);
    std::erase_if(nets, [&](const auto& n) { return !ordinary(d.nets[size_t(n.net)]); });
    auto [cap_h, cap_v] = transit_capacity(g);
    const auto routes   = global_route(g,
                                       cap_h,
                                       cap_v,
                                       admission_share(d.opt),
                                       0,
                                       nets,
                                       d.opt.global_margin,
                                       d.opt.global_rounds,
                                       net_pops,
                                       d.opt.global_astar_weight,
                                       d.opt.global_pattern);
    if (!d.opt.output_dir.empty()) {
      std::filesystem::create_directories(d.opt.output_dir);
      std::ofstream forest(d.opt.output_dir + "/critical_cellbox_routes.json");
      forest << "{\"scope\":\"coarse_reserved_capacity_only\",\"physical_certificate\":false,\"nets\":[";
      for (size_t n = 0; n < nets.size(); ++n) {
        const auto& net  = d.nets[size_t(nets[n].net)];
        const auto& tree = routes.trees[n];
        forest << (n ? "," : "") << "{\"source_net\":" << net.frame << ",\"name\":\""
               << json_util::escape(d.fp.nets[net.frame].name) << "\",\"routed\":" << (tree.routed ? "true" : "false")
               << ",\"endpoints\":[";
        for (size_t k = 0; k < nets[n].endpoints.size(); ++k) {
          forest << (k ? "," : "") << nets[n].endpoints[k];
        }
        forest << "],\"edges\":[";
        for (size_t k = 0; k < tree.edges.size(); ++k) {
          forest << (k ? "," : "") << '[' << tree.edges[k].first << ',' << tree.edges[k].second << ']';
        }
        forest << "]}";
      }
      forest << "]}\n";
      if (!forest) {
        throw std::runtime_error("cannot write critical cellbox route forest");
      }
    }
    out.route_attempted = true;
    out.unrouted = out.gates.unrouted = routes.unrouted;
    out.search_limited = out.gates.search_limited = routes.search_limited;
    out.topology_disconnected = out.gates.topology_disconnected = routes.topology_disconnected;
    out.overflow_edges = out.gates.overflow_edges = routes.overflow_edges;
    out.overflow_tracks = out.gates.overflow_tracks = routes.overflow_tracks;
    out.search_pops                                 = routes.search_pops;
    std::vector<size_t> use_h(g.size()), use_v(g.size());
    auto&               pressure = tile_pressure;
    for (const auto& tree : routes.trees) {
      for (const auto& [a, b] : tree.edges) {
        ++(g.row(a) == g.row(b) ? use_h : use_v)[size_t(std::min(a, b))];
      }
    }
    for (size_t n = 0; n < nets.size(); ++n) {
      const auto& tree = routes.trees[n];
      if (!tree.routed) {
        for (int tile : nets[n].endpoints) {
          pressure[size_t(tile)] = 1.2;
        }
      }
      for (const auto& [a, b] : tree.edges) {
        const bool  h       = g.row(a) == g.row(b);
        const auto& cap     = h ? cap_h : cap_v;
        const auto  limit   = std::max(1, int(std::floor(admission_share(d.opt) * std::min(cap[size_t(a)], cap[size_t(b)]))));
        const auto  ratio   = std::max(1., double((h ? use_h : use_v)[size_t(std::min(a, b))]) / limit);
        pressure[size_t(a)] = std::max(pressure[size_t(a)], ratio);
        pressure[size_t(b)] = std::max(pressure[size_t(b)], ratio);
      }
      if (!tree.routed) {
        const auto& net = d.nets[size_t(nets[n].net)];
        out.failures.push_back({net.frame,
                                d.fp.nets[net.frame].name,
                                net.pins.size(),
                                nets[n].endpoints.size(),
                                0,
                                0,
                                tree.search_limited,
                                tree.topology_disconnected,
                                tree.last_attempt_pops,
                                tree.search_pops,
                                tree.search_attempts});
        for (const auto& p : net.pins) {
          if (p.comp >= 0 && d.cell[size_t(p.comp)] >= 0) {
            out.component_pressure[size_t(p.comp)] = 1.2;
          }
        }
      }
    }
    // Transit pressure acts on populated boxes, not just endpoints of the
    // congested nets. No stale assignment survives this measured attempt.
    for (const auto& box : admission.slots) {
      for (int m : box.cells) {
        auto& p = out.component_pressure[size_t(d.movable[size_t(m)])];
        for (int k = 0; k < box.span; ++k) {
          p = std::max(p, pressure[size_t(box.tile + k)]);
        }
      }
    }
    if (congestion) {
      *congestion = route_congestion(d, g, routes, nets, cap_h, cap_v);
    }
  }
  admission.direct_component_pressure = out.component_pressure;
  admission.tile_pressure             = tile_pressure;
  admission.body_overlap_pressure     = cellbox_body_pressure(d, g, tile_pressure);
  for (size_t c = 0; c < out.component_pressure.size(); ++c) {
    out.component_pressure[c] = std::max(out.component_pressure[c], admission.body_overlap_pressure[c]);
  }
  std::set<int> hot;
  for (int c : d.movable) {
    if (out.component_pressure[size_t(c)] > 1) {
      out.component_hot_distance[size_t(c)] = 0;
      out.gates.affected_components.push_back(size_t(c));
      const auto& p = d.fp.components[size_t(c)].place;
      hot.insert(g.tile_of(p.x, p.y));
    }
  }
  out.hot_tiles = hot.size();
  out.seconds   = seconds_since(start);
  write_admission(d, g, admission, out, timing);
  return out;
}
}  // namespace livehd::placement::lr
