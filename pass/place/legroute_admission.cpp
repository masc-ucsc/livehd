// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <numeric>
#include <queue>
#include <unordered_map>
#include <set>
#include <tuple>

#include "cell_criticality.hpp"
#include "core/json_util.hpp"
#include "legroute_internal.hpp"
#include "inou/lefdef/routing_rules.hpp"

namespace livehd::placement::lr {
namespace {
bool ordinary(const Design& d, const Net& n) {
  return !n.power && !n.control_scope && (d.opt.cellbox_full_flow || (!n.clock && !n.reset));
}

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

std::vector<int64_t> reserved(const Grid& g, int tile, std::string_view name, int span, size_t count) {
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
  if (available.size() < count) {
    return {};
  }
  std::vector<int64_t> selected;
  for (size_t i = 0; i < count; ++i) {  // spread over the available tracks
    selected.push_back(available[count == 1 ? (available.size() - 1) / 2 : i * (available.size() - 1) / (count - 1)]);
  }
  return selected;
}

void counts(const Design& d, Cellbox_slot& box) {
  std::map<int, unsigned> local;
  for (int m : box.cells) {
    const int component = d.movable.at(size_t(m));
    for (int n : d.nets_of.at(size_t(m))) {
      if (!ordinary(d, d.nets[size_t(n)])) {
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
    if (ordinary(d, n)) {
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
      << ",\"reservation_semantics\":\"" << (d.opt.cellbox_plan_only ? 0u : d.opt.cellbox_reserve_tracks)
      << " usable M2 and M3 slots deducted per regular grid tile before routing; exact listed local "
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
    if (!ordinary(d, n)) {
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

std::vector<Long_preroute> select_long_preroutes(const Design& d, const Grid& g) {
  std::vector<Long_preroute> out;
  const int                  min_tiles = int(d.opt.cellbox_preroute_tiles);
  if (!min_tiles) {
    return out;
  }
  const auto tile_of = [&](int m) {
    const auto& c = d.fp.components[size_t(d.movable[size_t(m)])];
    return g.tile_of(c.place.x + d.macro[size_t(d.movable[size_t(m)])]->width * d.fp.dbu / 2,
                     c.place.y + d.macro[size_t(d.movable[size_t(m)])]->height * d.fp.dbu / 2);
  };
  struct Candidate {
    int n, a, b, ta, tb, length;
  };
  std::vector<Candidate> candidates;
  for (size_t n = 0; n < d.nets.size(); ++n) {
    const auto& net = d.nets[n];
    if (!ordinary(d, net) || !net.routed || net.pins.size() < 2 || net.pins.size() > d.opt.cellbox_preroute_max_pins
        || net.driver < 0) {
      continue;
    }
    // the trace: the driver to its farthest sink (the other sinks of a multi-pin net join that run)
    const auto& pd = net.pins[size_t(net.driver)];
    if (pd.comp < 0 || d.cell[size_t(pd.comp)] < 0) {
      continue;
    }
    const int da = tile_of(d.cell[size_t(pd.comp)]);
    int       far = -1, far_len = -1;
    for (size_t k = 0; k < net.pins.size(); ++k) {
      const auto& ps = net.pins[k];
      if (int(k) == net.driver || ps.comp < 0 || d.cell[size_t(ps.comp)] < 0 || ps.comp == pd.comp || da < 0) {
        continue;
      }
      const int tb  = tile_of(d.cell[size_t(ps.comp)]);
      const int len = tb < 0 ? -1 : std::abs(g.col(da) - g.col(tb)) + std::abs(g.row(da) - g.row(tb));
      if (len > far_len) {
        far     = int(k);
        far_len = len;
      }
    }
    if (far < 0) {
      continue;
    }
    const auto& ps = net.pins[size_t(far)];
    const int   a = d.cell[size_t(pd.comp)], b = d.cell[size_t(ps.comp)];
    if (d.width[size_t(a)] > g.tile_w || d.width[size_t(b)] > g.tile_w) {
      continue;  // wide cells take multi-tile boxes
    }
    const int ta = tile_of(a), tb = tile_of(b);
    if (ta < 0 || tb < 0) {
      continue;
    }
    const int length = std::abs(g.col(ta) - g.col(tb)) + std::abs(g.row(ta) - g.row(tb));
    if (length >= min_tiles) {
      candidates.push_back({int(n), a, b, ta, tb, length});
    }
  }
  std::ranges::stable_sort(candidates, [](const auto& x, const auto& y) { return x.length > y.length; });
  // runs already taken: (layer, track) -> spans along the track
  std::map<std::pair<int, int64_t>, std::vector<std::pair<int64_t, int64_t>>> taken;
  std::set<int>                                                                 cells_used;
  const physical::Signal_metal_rules                                            rules(d.tech, d.fp);
  auto [cap_h, cap_v]  = transit_capacity(g);
  const double share   = admission_share(d.opt);
  // a run is fixed for good: every tile on it keeps at least two admitted tracks of its direction for the rest
  const auto run_room = [&](int t0, int t1, bool h) {
    const int step = h ? 1 : g.cols;
    for (int t = t0;; t += step) {
      if (std::floor(share * (h ? cap_h : cap_v)[size_t(t)]) < 3) {
        return false;
      }
      if (t == t1) {
        return true;
      }
    }
  };
  for (const auto& c : candidates) {
    if (cells_used.contains(c.a) || cells_used.contains(c.b)) {
      continue;
    }
    const int  dc = std::abs(g.col(c.ta) - g.col(c.tb)), dr = std::abs(g.row(c.ta) - g.row(c.tb));
    const bool h  = dc >= dr;
    // the shared row (H) / column (V): inside the pair's span, the farther cell moving least first
    const int  lo_band = h ? std::min(g.row(c.ta), g.row(c.tb)) : std::min(g.col(c.ta), g.col(c.tb));
    const int  hi_band = h ? std::max(g.row(c.ta), g.row(c.tb)) : std::max(g.col(c.ta), g.col(c.tb));
    std::vector<int> bands;
    for (int k = lo_band; k <= hi_band; ++k) {
      bands.push_back(k);
    }
    const int along_a = h ? g.col(c.ta) : g.row(c.ta), along_b = h ? g.col(c.tb) : g.row(c.tb);
    const int band_a = h ? g.row(c.ta) : g.col(c.ta), band_b = h ? g.row(c.tb) : g.col(c.tb);
    std::ranges::stable_sort(bands, [&](int x, int y) {
      return std::max(std::abs(x - band_a), std::abs(x - band_b)) < std::max(std::abs(y - band_a), std::abs(y - band_b));
    });
    // upper layers of the direction, highest first; never the box layers (M2/M3 carry the boxes' private tracks)
    std::vector<int> layers;
    for (int l = int(g.layers.size()) - 1; l >= 0; --l) {
      if (g.layers[size_t(l)].horizontal == h && g.layers[size_t(l)].name != "M2" && g.layers[size_t(l)].name != "M3") {
        layers.push_back(l);
      }
    }
    const int along_lo = std::min(along_a, along_b), along_hi = std::max(along_a, along_b);
    bool      done     = false;
    for (int band : bands) {
      const int t0 = h ? g.tile(along_lo, band) : g.tile(band, along_lo);
      const int t1 = h ? g.tile(along_hi, band) : g.tile(band, along_hi);
      if (g.blocked[size_t(t0)] || g.blocked[size_t(t1)] || !run_room(t0, t1, h)) {
        continue;
      }
      // the run between the two box edges, as Local emits a box-tap carrier
      const int64_t a0 = carrier_tap_end(g, h, along_lo, false), a1 = carrier_tap_end(g, h, along_hi, true);
      for (int l : layers) {
        const auto& L         = g.layers[size_t(l)];
        const auto  clearance = rules.clearance(L.name).value_or(L.pitch);
        const auto [first, last] = g.tracks(l, band);
        for (int64_t step = 0; step <= (last - first) && !done; ++step) {  // from the band's middle outward
          const int64_t t = (first + last) / 2 + (step % 2 ? (step + 1) / 2 : -(step / 2));
          if (t < first || t > last) {
            continue;
          }
          const int64_t at   = g.track_coord(l, t);
          const double  half = L.width / 2. + clearance;
          bool          free = true;
          for (int bb = band - 1; bb <= band + 1 && free; ++bb) {
            if (bb < 0 || size_t(bb) >= g.obstacle_band[size_t(l)].size()) {
              continue;
            }
            for (int oi : g.obstacle_band[size_t(l)][size_t(bb)]) {
              const auto& r = g.obstacles[size_t(l)][size_t(oi)];
              const bool  hit = h ? r.xh >= a0 - clearance - L.width && r.xl <= a1 + clearance + L.width && r.yh >= at - half && r.yl <= at + half
                                  : r.yh >= a0 - clearance - L.width && r.yl <= a1 + clearance + L.width && r.xh >= at - half && r.xl <= at + half;
              if (hit) {
                free = false;
                break;
              }
            }
          }
          for (int64_t u = t - 1; u <= t + 1 && free; ++u) {  // another preroute on this or an adjacent track
            if (const auto it = taken.find({l, u}); it != taken.end()) {
              for (auto [x0, x1] : it->second) {
                if (x1 + 2 * clearance >= a0 && x0 - 2 * clearance <= a1) {
                  free = false;
                }
              }
            }
          }
          if (!free) {
            continue;
          }
          Long_preroute p;
          p.net        = c.n;
          p.cells      = {c.a, c.b};
          p.tiles      = {std::min(t0, t1), std::max(t0, t1)};
          p.layer      = l;
          p.track      = t;
          p.horizontal = h;
          out.push_back(p);
          taken[{l, t}].emplace_back(a0, a1);
          cells_used.insert(c.a);
          cells_used.insert(c.b);
          done = true;
        }
        if (done) {
          break;
        }
      }
      if (done) {
        break;
      }
    }
  }
  return out;
}

Cellbox_admission admit_critical_cellboxes(const Design& d, const Grid& g, const timing::Feedback& timing,
                                           const std::map<int, int>* pins, std::set<int>* unpinned) {
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
    if (ordinary(d, net)) {
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
      if (ordinary(d, d.nets[size_t(n)])) {
        out.critical[m] |= d.nets[size_t(n)].critical;
      }
    }
    out.order.push_back(int(m));
  }
  const auto pinned = [&](int m) { return pins && pins->contains(m) && !(unpinned && unpinned->contains(m)); };
  std::ranges::sort(out.order, [&](int a, int b) {
    // Rare wide cells reserve their real multi-tile footprint before ordinary
    // admission. Within each class the current snapshot determines priority.
    // Preroute-pinned cells claim their tiles first.
    return std::tuple{!pinned(a),
                      d.width[size_t(a)] <= g.tile_w,
                      !out.critical[size_t(a)],
                      out.slack_ps[size_t(a)],
                      d.fp.components[size_t(d.movable[size_t(a)])].name}
           < std::tuple{!pinned(b),
                        d.width[size_t(b)] <= g.tile_w,
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
    const auto   candidates   = pinned(m) ? std::vector<int>{pins->at(m)}
                                              : nearest(g,
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
      proposal.reserved_h = reserved(g, tile, "M2", span, cellbox_reserve(d.opt));
      proposal.reserved_v = reserved(g, tile, "M3", span, cellbox_reserve(d.opt));
      if (proposal.reserved_h.size() != cellbox_reserve(d.opt) || proposal.reserved_v.size() != cellbox_reserve(d.opt)) {
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
      if (pinned(m) && unpinned) {  // the pin cannot be met: the cell admits as usual, its preroute is dropped
        unpinned->insert(m);
        if (admit(m, false)) {
          continue;
        }
      }
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
  auto                 preroutes = select_long_preroutes(d, g);
  std::map<int, int>   pins;
  const auto tile_of_cell = [&](const Long_preroute& p, int k) {  // the pinned tile of p.cells[k]: same row/column as the other
    const auto& comp = d.fp.components[size_t(d.movable[size_t(p.cells[size_t(k)])])];
    const int   t    = g.tile_of(comp.place.x + d.macro[size_t(d.movable[size_t(p.cells[size_t(k)])])]->width * d.fp.dbu / 2,
                            comp.place.y + d.macro[size_t(d.movable[size_t(p.cells[size_t(k)])])]->height * d.fp.dbu / 2);
    // the end tile on the run whose along-coordinate is this cell's
    const int along = p.horizontal ? g.col(t) : g.row(t);
    for (int e : p.tiles) {
      if ((p.horizontal ? g.col(e) : g.row(e)) == along) {
        return e;
      }
    }
    return p.tiles[size_t(k)];
  };
  for (const auto& p : preroutes) {
    pins[p.cells[0]] = tile_of_cell(p, 0);
    pins[p.cells[1]] = tile_of_cell(p, 1);
  }
  std::set<int>        unpinned;
  const size_t         selected  = preroutes.size();
  auto                 admission = admit_critical_cellboxes(d, g, timing, &pins, &unpinned);
  std::erase_if(preroutes, [&](const Long_preroute& p) {
    if (unpinned.contains(p.cells[0]) || unpinned.contains(p.cells[1])) {
      return true;
    }
    const int ba = admission.boxes.box_of[size_t(p.cells[0])], bb = admission.boxes.box_of[size_t(p.cells[1])];
    return ba < 0 || bb < 0 || admission.slots[size_t(ba)].tile != pins.at(p.cells[0])
           || admission.slots[size_t(bb)].tile != pins.at(p.cells[1]) || ba == bb;
  });
  if (selected) {
    std::fprintf(stderr, "legroute preroutes: %zu long traces selected, %zu kept after pinned admission\n", selected,
                 preroutes.size());
  }
  if (!d.opt.output_dir.empty()) {
    std::filesystem::create_directories(d.opt.output_dir);
    std::ofstream tsv(d.opt.output_dir + "/critical_preroutes.tsv");
    tsv << "net\tpins\thorizontal\ttile_lo\ttile_hi\tlength\tlayer\ttrack\n";
    for (const auto& p : preroutes) {
      tsv << d.fp.nets[d.nets[size_t(p.net)].frame].name << '\t' << d.nets[size_t(p.net)].pins.size() << '\t' << p.horizontal
          << '\t' << p.tiles[0] << '\t' << p.tiles[1] << '\t'
          << std::abs(g.col(p.tiles[0]) - g.col(p.tiles[1])) + std::abs(g.row(p.tiles[0]) - g.row(p.tiles[1])) << '\t'
          << g.layers[size_t(p.layer)].name << '\t' << p.track << '\n';
    }
  }
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
    if (ordinary(d, n) && n.routed) {
      ++out.signals;
      out.timed_signals += std::isfinite(n.slack);
    }
  }
  std::vector<Global_net> nets;
  Global_routes           routes;
  Plan_fixed              plan_fixed;
  if (!admission.unassigned.empty()) {
    out.route_skip_reason        = Legroute_plan_report::Route_skip_reason::window_density;
    out.unchecked_signals        = out.signals;
    out.gates.cell_dense_regions = admission.unassigned.size();
    out.gates.unfittable_boxes   = admission.unassigned.size();
  } else {
    nets = all_signal_requests(d, g, admission.boxes);
    std::erase_if(nets, [&](const auto& n) { return !ordinary(d, d.nets[size_t(n.net)]); });
    // a preroute's net runs only on its straight run (free for it, counted against the capacity like any route)
    std::map<int, const Long_preroute*> preroute_of;
    for (const auto& p : preroutes) {
      preroute_of[p.net] = &p;
    }
    for (auto& n : nets) {
      if (const auto it = preroute_of.find(n.net); it != preroute_of.end()) {
        const auto& p    = *it->second;
        const int   step = p.horizontal ? 1 : g.cols;
        for (int t = p.tiles[0]; t != p.tiles[1]; t += step) {
          n.fixed_edges.emplace_back(t, t + step);
        }
      }
    }
    auto [cap_h, cap_v] = transit_capacity(g);
    const auto route    = [&] {
      return global_route(g,
                          cap_h,
                          cap_v,
                          admission_share(d.opt),
                          0,
                          nets,
                          d.opt.global_margin,
                          d.opt.global_rounds,
                          net_pops,
                          d.opt.global_astar_weight,
                          d.opt.global_pattern,
                          d.opt.cellbox_tile_capacity);
    };
    routes = route();
    if (routes.overflow_edges && !preroutes.empty()) {
      // A preroute run crossing an overflowing edge or tile cannot step aside: release it (it routes like any net,
      // its cells stay where they were pinned) and route once more.
      std::set<int> hot;
      for (auto [t, h] : routes.over) {
        hot.insert(t);
        hot.insert(h ? t + 1 : t + g.cols);
      }
      std::set<int> released;
      for (const auto& p : preroutes) {
        const int step = p.horizontal ? 1 : g.cols;
        for (int t = p.tiles[0];; t += step) {
          if (hot.contains(t)) {
            released.insert(p.net);
            break;
          }
          if (t == p.tiles[1]) {
            break;
          }
        }
      }
      if (!released.empty()) {
        std::erase_if(preroutes, [&](const Long_preroute& p) { return released.contains(p.net); });
        for (auto& n : nets) {
          if (released.contains(n.net)) {
            n.fixed_edges.clear();
          }
        }
        std::fprintf(stderr, "legroute preroutes: %zu released (their runs crossed overflow), %zu kept\n", released.size(),
                     preroutes.size());
        routes = route();
      }
    }
    // End of Plan: on a legal route, the multi-region nets take their exact tracks alone and are fixed; the remaining
    // nets route again with those tracks taken. An overflow then fails this attempt (pressure on its tiles), so the
    // feedback rounds raise the density until the fixed routes and the rest both fit.
    if (d.opt.cellbox_full_flow && d.opt.cellbox_fixed_min_regions && admission.unassigned.empty() && !routes.unrouted
        && !routes.overflow_edges) {
      auto probe          = std::make_shared<Critical_cellbox_handoff>();
      probe->source_frame = d.fp;
      probe->options      = d.opt;
      probe->options.cellbox_handoff.reset();
      probe->grid      = g;
      probe->admission = admission;
      probe->requests  = nets;
      probe->routes    = routes;
      probe->preroutes = preroutes;
      plan_fixed       = plan_fixed_tracks(d, g, probe, select_region_nets(d, g, admission.boxes), d.opt.threads);
      if (!plan_fixed.nets.empty()) {
        const std::set<int> fixed(plan_fixed.nets.begin(), plan_fixed.nets.end());
        auto                rest_h = cap_h, rest_v = cap_v;
        // the remaining nets get share x (tracks - fixed / share): together they stay within share x tracks
        const double share = admission_share(d.opt);
        for (size_t t = 0; t < g.size(); ++t) {
          rest_h[t] = std::max(0, rest_h[t] - int(std::ceil(plan_fixed.use_h[t] / share)));
          rest_v[t] = std::max(0, rest_v[t] - int(std::ceil(plan_fixed.use_v[t] / share)));
        }
        std::vector<Global_net> rest;
        std::vector<size_t>     rest_index;
        for (size_t i = 0; i < nets.size(); ++i) {
          if (!fixed.contains(nets[i].net)) {
            rest.push_back(nets[i]);
            rest_index.push_back(i);
          }
        }
        auto again = global_route(g, rest_h, rest_v, admission_share(d.opt), 0, rest, d.opt.global_margin, d.opt.global_rounds,
                                  net_pops, d.opt.global_astar_weight, d.opt.global_pattern, d.opt.cellbox_tile_capacity);
        auto merged  = again;
        merged.trees = routes.trees;  // the fixed nets keep their Plan trees
        for (size_t k = 0; k < rest.size(); ++k) {
          merged.trees[rest_index[k]] = again.trees[k];
        }
        routes = std::move(merged);
        std::fprintf(stderr,
                     "legroute plan fixed: %zu multi-region nets, %zu fixed with exact tracks (%zu without), the remaining %zu "
                     "nets routed again: %zu unrouted, %zu overflow edges\n",
                     plan_fixed.candidates,
                     plan_fixed.nets.size(),
                     plan_fixed.failed,
                     rest.size(),
                     again.unrouted,
                     again.overflow_edges);
      }
    }
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
      // Directional transit capacity per tile (after reservations), so residual edges can be located offline.
      const auto ints = [&](const char* key, const std::vector<int>& values) {
        forest << ",\"" << key << "\":[";
        for (size_t k = 0; k < values.size(); ++k) {
          forest << (k ? "," : "") << values[k];
        }
        forest << ']';
      };
      forest << "],\"admission_share\":" << admission_share(d.opt) << ",\"cols\":" << g.cols << ",\"rows\":" << g.rows;
      ints("cap_h", cap_h);
      ints("cap_v", cap_v);
      forest << "}\n";
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
  if (d.opt.cellbox_full_flow && admission.unassigned.empty() && out.route_attempted && !out.gates.requires_replan()
      && std::ranges::none_of(out.component_pressure, [](double p) { return p > 1; })) {
    auto handoff          = std::make_shared<Critical_cellbox_handoff>();
    handoff->source_frame = d.fp;
    handoff->options      = d.opt;
    handoff->options.cellbox_handoff.reset();
    handoff->grid       = g;
    handoff->admission  = std::move(admission);
    handoff->requests   = std::move(nets);
    handoff->routes     = std::move(routes);
    handoff->preroutes  = std::move(preroutes);
    handoff->fixed_nets = plan_fixed.nets;
    if (!plan_fixed.nets.empty()) {
      handoff->fixed_kept = plan_fixed.kept;
    }
    out.cellbox_handoff = std::move(handoff);
  }
  return out;
}

namespace {
Box box_from_slot(const Design& d, const Grid& g, const Cellbox_slot& slot) {
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
  return box;
}
}  // namespace

Cellbox_relocation relocate_failed_cellboxes(const Design& d, const std::vector<int>& failed_boxes, unsigned window) {
  if (!d.opt.cellbox_handoff) {
    throw std::invalid_argument("cellbox relocation requires an installed Plan handoff");
  }
  Cellbox_relocation out;
  const auto&        h = *d.opt.cellbox_handoff;
  const Grid&        g = h.grid;
  auto               a = h.admission;  // working copy; the handoff stays immutable
  const int          w = int(window);
  std::vector<int>   owner(g.size(), -1);
  for (size_t b = 0; b < a.slots.size(); ++b) {
    for (int k = 0; k < a.slots[b].span; ++k) {
      owner[size_t(a.slots[b].tile + k)] = int(b);
    }
  }
  const std::set<int> failing(failed_boxes.begin(), failed_boxes.end());
  // The pins a cell has on nets that leave its box (its IO pins as a box member).
  const auto io_pins = [&](const Cellbox_slot& slot, int m) {
    const int component = d.movable[size_t(m)];
    unsigned  io        = 0;
    for (int n : d.nets_of[size_t(m)]) {
      const auto& net = d.nets[size_t(n)];
      if (!ordinary(d, net)) {
        continue;
      }
      unsigned inside = 0, own = 0;
      for (const auto& p : net.pins) {
        const int cell  = p.comp >= 0 ? d.cell[size_t(p.comp)] : -1;
        inside         += cell >= 0 && std::ranges::find(slot.cells, cell) != slot.cells.end();
        own            += p.comp == component;
      }
      if (inside < net.pins.size()) {
        io += own;
      }
    }
    return io;
  };
  struct Move {
    int m = -1, from = -1, to = -1;
  };
  std::vector<Move>             moves;
  std::map<int, Cellbox_slot>   original;  // first-touch copies, for a region's undo
  std::set<int>                 preroute_cells;  // cells of the nets fixed before Local: never relocated
  for (int n : fixed_net_set(d, g, a.boxes)) {
    for (const auto& p : d.nets[size_t(n)].pins) {
      if (p.comp >= 0 && d.cell[size_t(p.comp)] >= 0) {
        preroute_cells.insert(d.cell[size_t(p.comp)]);
      }
    }
  }
  for (int b : failing) {
    if (b < 0 || size_t(b) >= a.slots.size()) {
      continue;
    }
    const auto& slot = a.slots[size_t(b)];
    // The cell with a single IO pin (else the fewest, at least one), the narrowest first.
    int      pick    = -1;
    unsigned pick_io = 0;
    if (slot.span == 1 && slot.cells.size() >= 2) {
      for (int m : slot.cells) {
        const unsigned io = io_pins(slot, m);
        if (io && !preroute_cells.contains(m) && (pick < 0 || std::tie(io, d.width[size_t(m)]) < std::tie(pick_io, d.width[size_t(pick)]))) {
          pick    = m;
          pick_io = io;
        }
      }
    }
    if (pick < 0) {
      ++out.without_candidate;
      continue;
    }
    int          best = -1, best_dist = 0;
    unsigned     best_io = 0;
    Cellbox_slot best_slot;
    for (int dr = -w; dr <= w; ++dr) {
      for (int dc = -w; dc <= w; ++dc) {
        const int col = g.col(slot.tile) + dc, row = g.row(slot.tile) + dr;
        if (col < 0 || row < 0 || col >= g.cols || row >= g.rows) {
          continue;
        }
        const int o = owner[size_t(g.tile(col, row))];
        if (o < 0 || o == b || failing.contains(o) || a.slots[size_t(o)].span != 1) {
          continue;
        }
        const unsigned io   = a.slots[size_t(o)].external_pins;
        const int      dist = std::abs(dc) + std::abs(dr);
        if (best >= 0 && std::tie(io, dist) >= std::tie(best_io, best_dist)) {
          continue;
        }
        auto proposal = a.slots[size_t(o)];
        proposal.cells.push_back(pick);
        counts(d, proposal);
        if (proposal.external_pins > d.opt.cellbox_external_pins || proposal.connections > d.opt.cellbox_connections
            || !pack(d, g, proposal)) {
          continue;
        }
        best      = o;
        best_io   = io;
        best_dist = dist;
        best_slot = std::move(proposal);
      }
    }
    if (best < 0) {
      ++out.without_destination;
      continue;
    }
    auto source = slot;
    std::erase(source.cells, pick);
    counts(d, source);
    if (!pack(d, g, source)) {
      ++out.without_destination;
      continue;
    }
    original.try_emplace(b, a.slots[size_t(b)]);
    original.try_emplace(best, a.slots[size_t(best)]);
    a.slots[size_t(b)]    = std::move(source);
    a.slots[size_t(best)] = std::move(best_slot);
    moves.push_back({pick, b, best});
  }
  const auto sync_box = [&](int b) {
    a.boxes.boxes[size_t(b)] = box_from_slot(d, g, a.slots[size_t(b)]);
    for (int m : a.slots[size_t(b)].cells) {
      a.boxes.box_of[size_t(m)] = b;
    }
  };
  for (const auto& [b, slot] : original) {
    (void)slot;
    sync_box(b);
  }
  if (moves.empty()) {
    return out;
  }
  // Regions: the window around each source box, merged until disjoint (independent retries, fixed crossings).
  struct Rect {
    int c0, r0, c1, r1;
  };
  std::vector<Rect> rects;
  std::vector<int>  region_of_move;
  for (const auto& mv : moves) {
    const int t = a.slots[size_t(mv.from)].tile;
    rects.push_back({std::max(0, g.col(t) - w),
                     std::max(0, g.row(t) - w),
                     std::min(g.cols - 1, g.col(t) + w),
                     std::min(g.rows - 1, g.row(t) + w)});
  }
  std::vector<int> group(moves.size());
  std::iota(group.begin(), group.end(), 0);
  for (bool merged = true; merged;) {
    merged = false;
    for (size_t i = 0; i < rects.size() && !merged; ++i) {
      for (size_t j = i + 1; j < rects.size() && !merged; ++j) {
        if (group[i] == int(i) && group[j] == int(j) && rects[i].c0 <= rects[j].c1 && rects[j].c0 <= rects[i].c1
            && rects[i].r0 <= rects[j].r1 && rects[j].r0 <= rects[i].r1) {
          rects[i] = {std::min(rects[i].c0, rects[j].c0),
                      std::min(rects[i].r0, rects[j].r0),
                      std::max(rects[i].c1, rects[j].c1),
                      std::max(rects[i].r1, rects[j].r1)};
          for (auto& gr : group) {
            if (gr == int(j)) {
              gr = int(i);
            }
          }
          merged = true;
        }
      }
    }
  }
  // Forest state per net (edges as (lower, higher) tile pairs) and shared edge use.
  const auto filtered_requests = [&](const Boxes& boxes) {
    auto requests = all_signal_requests(d, g, boxes);
    std::erase_if(requests, [&](const auto& r) {
      const auto& n = d.nets.at(size_t(r.net));
      return n.power || n.control_scope;
    });
    return requests;
  };
  std::vector<std::vector<int>>              endpoints(d.nets.size()), old_endpoints(d.nets.size());
  std::vector<std::set<std::pair<int, int>>> edges(d.nets.size());
  std::vector<char>                          changed(d.nets.size(), 0);
  for (size_t i = 0; i < h.requests.size(); ++i) {
    const auto n     = size_t(h.requests[i].net);
    old_endpoints[n] = h.requests[i].endpoints;
    for (auto [x, y] : h.routes.trees[i].edges) {
      edges[n].insert(std::minmax(x, y));
    }
  }
  for (const auto& r : filtered_requests(a.boxes)) {
    endpoints[size_t(r.net)] = r.endpoints;
  }
  auto [cap_h, cap_v] = transit_capacity(g);
  const double share  = admission_share(d.opt);
  const auto   key    = [&](int x, int y) { return int64_t(std::min(x, y)) * 2 + (g.row(x) == g.row(y) ? 0 : 1); };
  const auto   limit  = [&](int x, int y) {
    const auto& caps = g.row(x) == g.row(y) ? cap_h : cap_v;
    return std::max(0, int(std::floor(share * std::min(caps[size_t(x)], caps[size_t(y)]))));
  };
  std::unordered_map<int64_t, int> use;
  for (const auto& es : edges) {
    for (auto [x, y] : es) {
      ++use[key(x, y)];
    }
  }
  // Reroute the part of net n inside rect: crossings (edges with one tile inside) stay; dead branches go; the
  // remaining pieces and the endpoints inside join by shortest paths over edges with room, inside the rect.
  const auto inside = [&](const Rect& r, int t) {
    return g.col(t) >= r.c0 && g.col(t) <= r.c1 && g.row(t) >= r.r0 && g.row(t) <= r.r1;
  };
  const auto drop = [&](size_t n, std::pair<int, int> e) {
    edges[n].erase(e);
    --use[key(e.first, e.second)];
  };
  const auto rip = [&](const Rect& r, size_t n) {
    for (auto it = edges[n].begin(); it != edges[n].end();) {
      auto e = *it++;
      if (inside(r, e.first) && inside(r, e.second)) {
        drop(n, e);
      }
    }
    const std::set<int> wanted(endpoints[n].begin(), endpoints[n].end());
    for (bool pruned = true; pruned;) {
      pruned = false;
      std::map<int, std::vector<std::pair<int, int>>> incident;
      for (const auto& e : edges[n]) {
        incident[e.first].push_back(e);
        incident[e.second].push_back(e);
      }
      for (const auto& [t, list] : incident) {
        if (list.size() == 1 && !wanted.contains(t) && !inside(r, t)) {
          drop(n, list.front());
          pruned = true;
        }
      }
    }
  };
  const auto join = [&](const Rect& r, size_t n) {
    // pieces: union-find over the remaining edges; terminals: tiles inside with an edge, endpoints inside
    std::map<int, int> parent;
    const auto         find = [&](int t) {
      parent.try_emplace(t, t);
      while (parent[t] != t) {
        parent[t] = parent[parent[t]];
        t         = parent[t];
      }
      return t;
    };
    for (auto [x, y] : edges[n]) {
      parent[find(x)] = find(y);
    }
    std::set<int> terminals;
    for (auto [x, y] : edges[n]) {
      for (int t : {x, y}) {
        if (inside(r, t)) {
          terminals.insert(t);
        }
      }
    }
    for (int t : endpoints[n]) {
      if (inside(r, t)) {
        terminals.insert(t);
      }
      (void)find(t);
    }
    // endpoints outside the rect must already be on a piece that reaches it, else the net is not ours to fix
    if (terminals.size() <= 1) {
      return true;
    }
    std::set<int> connected;  // terminals of the joined pieces
    const int     root0 = find(*terminals.begin());
    for (int t : terminals) {
      if (find(t) == root0) {
        connected.insert(t);
      }
    }
    while (connected.size() < terminals.size()) {
      std::map<int, double> dist;
      std::map<int, int>    from;
      using Entry = std::pair<double, int>;
      std::priority_queue<Entry, std::vector<Entry>, std::greater<>> queue;
      for (int t : connected) {
        dist[t] = 0;
        queue.emplace(0, t);
      }
      int hit = -1;
      while (!queue.empty()) {
        auto [c, t] = queue.top();
        queue.pop();
        if (c > dist[t]) {
          continue;
        }
        if (terminals.contains(t) && !connected.contains(t)) {
          hit = t;
          break;
        }
        const int col = g.col(t), row = g.row(t);
        for (int q : {col ? t - 1 : -1, col + 1 < g.cols ? t + 1 : -1, row ? t - g.cols : -1, row + 1 < g.rows ? t + g.cols : -1}) {
          if (q < 0 || !inside(r, q)) {
            continue;
          }
          const int cap = limit(t, q), used = use[key(t, q)];
          if (used >= cap) {
            continue;
          }
          const double next = c + 1 + double(used + 1) / std::max(1, cap);
          if (!dist.contains(q) || next < dist[q]) {
            dist[q] = next;
            from[q] = t;
            queue.emplace(next, q);
          }
        }
      }
      if (hit < 0) {
        return false;
      }
      for (int t = hit; !connected.contains(t);) {
        const int p = from.at(t);
        edges[n].insert(std::minmax(p, t));
        ++use[key(p, t)];
        t = p;
      }
      const int root = find(hit);
      for (int t : terminals) {
        if (find(t) == root) {
          connected.insert(t);
        }
      }
      parent[root] = root0;
    }
    return true;
  };
  for (size_t ri = 0; ri < rects.size(); ++ri) {
    if (group[ri] != int(ri)) {
      continue;
    }
    ++out.regions;
    const auto&         r = rects[ri];
    std::vector<size_t> region_moves;
    std::set<size_t>    moved_nets;
    for (size_t k = 0; k < moves.size(); ++k) {
      if (group[k] == int(ri)) {
        region_moves.push_back(k);
        for (int n : d.nets_of[size_t(moves[k].m)]) {
          if (ordinary(d, d.nets[size_t(n)]) && endpoints[size_t(n)] != old_endpoints[size_t(n)]) {
            moved_nets.insert(size_t(n));
          }
        }
      }
    }
    std::set<size_t> region_nets = moved_nets;  // try 2: every net with metal or an endpoint inside
    for (size_t n = 0; n < d.nets.size(); ++n) {
      const bool touches = std::ranges::any_of(edges[n], [&](auto e) { return inside(r, e.first) || inside(r, e.second); })
                           || std::ranges::any_of(endpoints[n], [&](int t) { return inside(r, t); });
      if (touches) {
        region_nets.insert(n);
      }
    }
    std::map<size_t, std::set<std::pair<int, int>>> saved;
    for (size_t n : region_nets) {
      saved[n] = edges[n];
    }
    const auto saved_use = use;
    const auto restore   = [&] {
      for (auto& [n, es] : saved) {
        edges[n] = es;
      }
      use = saved_use;
    };
    const auto attempt = [&](const std::set<size_t>& nets) {
      for (size_t n : nets) {
        rip(r, n);
      }
      std::vector<size_t> order(moved_nets.begin(), moved_nets.end());
      for (size_t n : nets) {
        if (!moved_nets.contains(n)) {
          order.push_back(n);
        }
      }
      return std::ranges::all_of(order, [&](size_t n) { return join(r, n); });
    };
    bool ok = attempt(moved_nets);
    if (ok) {
      ++out.fixed_routed;
    } else {
      restore();
      ok = attempt(region_nets);
      if (ok) {
        ++out.region_rerouted;
      } else {
        restore();
      }
    }
    if (ok) {
      for (size_t n : region_nets) {
        changed[n] |= edges[n] != saved[n];
      }
      continue;
    }
    // undo the region's moves: its boxes, their cells' endpoints and trees (restored above) return as they were
    ++out.region_reverted;
    for (size_t k : region_moves) {
      for (int b : {moves[k].from, moves[k].to}) {
        a.slots[size_t(b)] = original.at(b);
        sync_box(b);
      }
      moves[k].m = -1;
    }
    for (auto& e : endpoints) {
      e.clear();
    }
    for (const auto& req : filtered_requests(a.boxes)) {  // a net moved in another region keeps that region's change
      endpoints[size_t(req.net)] = req.endpoints;
    }
  }
  std::erase_if(moves, [](const Move& mv) { return mv.m < 0; });
  out.moves = moves.size();
  if (moves.empty()) {
    return out;
  }
  auto next       = std::make_shared<Critical_cellbox_handoff>(h);
  next->admission = std::move(a);
  next->requests  = filtered_requests(next->admission.boxes);
  next->routes    = {};
  next->routes.trees.resize(next->requests.size());
  std::vector<int> old_index(d.nets.size(), -1);
  for (size_t i = 0; i < h.requests.size(); ++i) {
    old_index[size_t(h.requests[i].net)] = int(i);
  }
  for (size_t i = 0; i < next->requests.size(); ++i) {
    const auto n    = size_t(next->requests[i].net);
    auto&      tree = next->routes.trees[i];
    tree.routed     = true;
    if (!changed[n] && old_index[n] >= 0) {
      tree.edges = h.routes.trees[size_t(old_index[n])].edges;  // untouched nets keep their exact forest order
    } else {
      tree.edges.assign(edges[n].begin(), edges[n].end());
    }
  }
  std::set<int> touched;
  for (const auto& mv : moves) {
    touched.insert(mv.from);
    touched.insert(mv.to);
  }
  out.priority_boxes.assign(touched.begin(), touched.end());
  out.handoff = std::move(next);
  return out;
}
Cellbox_moves move_cellboxes(const Design& d, const std::vector<std::vector<std::pair<int, int>>>& forest,
                             const std::vector<int>& candidates, const std::vector<double>& tile_cost, unsigned window) {
  if (!d.opt.cellbox_handoff) {
    throw std::invalid_argument("cellbox moves require an installed Plan handoff");
  }
  Cellbox_moves out;
  const auto&   h = *d.opt.cellbox_handoff;
  const Grid&   g = h.grid;
  auto          a = h.admission;
  const int     w = int(window);
  if (forest.size() != h.requests.size()) {
    throw std::invalid_argument("cellbox moves: forest does not match the handoff requests");
  }
  std::vector<int> owner(g.size(), -1);
  for (size_t b = 0; b < a.slots.size(); ++b) {
    for (int k = 0; k < a.slots[b].span; ++k) {
      owner[size_t(a.slots[b].tile + k)] = int(b);
    }
  }
  std::vector<std::set<std::pair<int, int>>> edges(d.nets.size());
  std::vector<int>                           request_of(d.nets.size(), -1);
  std::vector<char>                          changed(d.nets.size(), 0);
  std::unordered_map<int64_t, int>           use;
  for (size_t i = 0; i < h.requests.size(); ++i) {
    const auto n  = size_t(h.requests[i].net);
    request_of[n] = int(i);
    for (auto [x, y] : forest[i]) {
      edges[n].insert(std::minmax(x, y));
      ++use[forest_edge_key(g, x, y)];
    }
  }
  auto [cap_h, cap_v] = transit_capacity(g);
  const double                       share = admission_share(d.opt);
  const std::function<int(int, int)> limit = [&](int x, int y) {
    const auto& caps = g.row(x) == g.row(y) ? cap_h : cap_v;
    return std::max(0, int(std::floor(share * std::min(caps[size_t(x)], caps[size_t(y)]))));
  };
  // a net's endpoint tiles under the working admission (all_signal_requests' rule)
  const auto terminals = [&](size_t n) {
    std::set<int> out_tiles;
    for (const auto& p : d.nets[n].pins) {
      const int box = p.comp >= 0 && d.cell[size_t(p.comp)] >= 0 ? a.boxes.box_of[size_t(d.cell[size_t(p.comp)])] : -1;
      const int t   = box >= 0 ? a.slots[size_t(box)].tile : physical_pin_access(d, g, p).tile;
      if (t >= 0) {
        out_tiles.insert(t);
      }
    }
    return out_tiles;
  };
  const auto around = [&](int t) {
    double    cost = tile_cost[size_t(t)];
    const int c = g.col(t), r = g.row(t);
    for (auto [dc, dr] : {std::pair{-1, 0}, {1, 0}, {0, -1}, {0, 1}}) {
      if (c + dc >= 0 && c + dc < g.cols && r + dr >= 0 && r + dr < g.rows) {
        cost += .5 * tile_cost[size_t(g.tile(c + dc, r + dr))];
      }
    }
    return cost;
  };
  std::set<int> tried, pinned_cells;
  for (int n : fixed_net_set(d, g, a.boxes)) {  // the nets fixed before Local keep their cells' boxes where they are
    for (const auto& p : d.nets[size_t(n)].pins) {
      if (p.comp >= 0 && d.cell[size_t(p.comp)] >= 0) {
        pinned_cells.insert(d.cell[size_t(p.comp)]);
      }
    }
  }
  for (int b : candidates) {
    if (b < 0 || size_t(b) >= a.slots.size() || !tried.insert(b).second || a.slots[size_t(b)].span != 1
        || std::ranges::any_of(a.slots[size_t(b)].cells, [&](int m) { return pinned_cells.contains(m); })) {
      continue;
    }
    const auto original = a.slots[size_t(b)];
    const int  from     = original.tile;
    double     best     = around(from);
    std::optional<Cellbox_slot> chosen;
    for (int dr = -w; dr <= w; ++dr) {
      for (int dc = -w; dc <= w; ++dc) {
        const int c = g.col(from) + dc, r = g.row(from) + dr;
        if ((!dc && !dr) || c < 0 || c >= g.cols || r < 0 || r >= g.rows) {
          continue;
        }
        const int t = g.tile(c, r);
        if (owner[size_t(t)] >= 0 || g.blocked[size_t(t)] || (!g.box_short.empty() && g.box_short[size_t(t)])) {
          continue;
        }
        const double cost = around(t) + .25 * (std::abs(dc) + std::abs(dr));
        if (cost >= best) {
          continue;
        }
        auto proposal = original;
        proposal.tile = t;
        if (!pack(d, g, proposal)) {
          continue;
        }
        proposal.reserved_h = reserved(g, t, "M2", 1, cellbox_reserve(d.opt));
        proposal.reserved_v = reserved(g, t, "M3", 1, cellbox_reserve(d.opt));
        if (proposal.reserved_h.size() != cellbox_reserve(d.opt) || proposal.reserved_v.size() != cellbox_reserve(d.opt)) {
          continue;
        }
        best   = cost;
        chosen = std::move(proposal);
      }
    }
    if (!chosen) {
      ++out.without_destination;
      continue;
    }
    // apply, then reconnect every net of the box to its new tile
    const int to         = chosen->tile;
    a.slots[size_t(b)]   = std::move(*chosen);
    a.boxes.boxes[size_t(b)] = box_from_slot(d, g, a.slots[size_t(b)]);
    owner[size_t(from)]  = -1;
    owner[size_t(to)]    = b;
    std::set<size_t> nets;
    for (int m : a.slots[size_t(b)].cells) {
      for (int n : d.nets_of[size_t(m)]) {
        if (request_of[size_t(n)] >= 0) {
          nets.insert(size_t(n));
        }
      }
    }
    std::map<size_t, std::set<std::pair<int, int>>> saved;
    for (size_t n : nets) {
      saved[n] = edges[n];
    }
    const auto saved_use = use;
    bool       ok        = true;
    for (size_t n : nets) {
      const auto ends = terminals(n);
      ok              = repair_forest(g, edges[n], ends, use, limit, {}, 4) || repair_forest(g, edges[n], ends, use, limit, {}, 12);
      if (!ok) {
        break;
      }
    }
    if (!ok) {
      ++out.unroutable;
      for (auto& [n, es] : saved) {
        edges[n] = es;
      }
      use                      = saved_use;
      a.slots[size_t(b)]       = original;
      a.boxes.boxes[size_t(b)] = box_from_slot(d, g, original);
      owner[size_t(to)]        = -1;
      owner[size_t(from)]      = b;
      continue;
    }
    for (size_t n : nets) {
      changed[n] |= edges[n] != saved[n];
    }
    out.moved_boxes.push_back(b);
  }
  if (out.moved_boxes.empty()) {
    return out;
  }
  auto next       = std::make_shared<Critical_cellbox_handoff>(h);
  next->admission = std::move(a);
  next->requests  = all_signal_requests(d, g, next->admission.boxes);
  std::erase_if(next->requests, [&](const auto& r) {
    const auto& n = d.nets.at(size_t(r.net));
    return n.power || n.control_scope;
  });
  next->routes = {};
  next->routes.trees.resize(next->requests.size());
  for (size_t i = 0; i < next->requests.size(); ++i) {
    const auto n    = size_t(next->requests[i].net);
    auto&      tree = next->routes.trees[i];
    tree.routed     = true;
    if (!changed[n] && request_of[n] >= 0) {
      tree.edges = forest[size_t(request_of[n])];  // untouched nets keep their exact forest order
    } else {
      tree.edges.assign(edges[n].begin(), edges[n].end());
    }
  }
  out.handoff = std::move(next);
  return out;
}
}  // namespace livehd::placement::lr
