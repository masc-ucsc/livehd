// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "place.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>
#include <numeric>
#include <optional>
#include <print>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "attrs.hpp"
#include "buffering/checkpoint.hpp"
#include "color_centroids.hpp"
#include "dreamplace/native/placement.hpp"
#include "dreamplace/native/rudy.hpp"
#include "filler_budget.hpp"
#include "forces/gp_rejection.hpp"
#include "forces/gp_timing.hpp"
#include "forces/legalization.hpp"
#include "growth_budget.hpp"
#include "h2d.hpp"
#include "handoff_pins.hpp"
#include "handoff_rows.hpp"
#include "hash_util.hpp"
#include "inou/lefdef/index_union_find.hpp"
#include "macro_snap.hpp"
#include "macro_tracks.hpp"
#include "magnet_report.hpp"
#include "memory_proxy.hpp"
#include "node_util.hpp"
#include "pass/liberty/liberty_dff.hpp"
#include "pass/partition/flatten.hpp"
#include "pass/place/place_salt.hpp"
#include "pg_rails.hpp"
#include "pin_adjust.hpp"
#include "pin_escape_guard.hpp"
#include "placement_technology.hpp"
#include "power_grid.hpp"
#include "provisional_cell.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "repair.hpp"
#include "reset_tree.hpp"
#include "resize_engine.hpp"
#include "routing_tracks.hpp"
#include "rules_cc/cc/runfiles/runfiles.h"
#include "signal_router.hpp"
#include "steiner_topology.hpp"
#include "technology_rc.hpp"
#include "thirdparty/flute/flute.hpp"
#include "track_capacity.hpp"

namespace livehd::placement {
namespace {
namespace ph      = physical;
namespace gu      = graph_util;
namespace dp      = DreamPlace::native;
namespace mp      = dp::macro;
namespace fs      = std::filesystem;
using Node        = hhds::Node_class;
using Pin         = hhds::Pin_class;
using Orientation = ph::Orientation;
using Status      = ph::Status;
// The routing grid (synth.place.grid.route) consumed by RUDY/RSMT/URoute,
// native legalization/detailed placement, terminal bins and SAT route-grid
// bins. The native auxiliary grid: an explicit grid.route, or the configured
// density count when the dynamic density grid would otherwise move it; 0
// follows the density grid in use.
int auxiliary_route_bins(const Options& o) { return o.route_bins ? o.route_bins : o.dynamic_bins ? o.bins : 0; }
int route_grid_bins(int auxiliary_bins, int density_bins) { return auxiliary_bins ? auxiliary_bins : density_bins; }

std::string memory_proxy_lef(const std::map<uint64_t, std::string>& memories, const ph::Technology& tech) {
  if (memories.empty()) {
    return {};
  }
  std::ostringstream lef;
  lef << "VERSION 5.8 ;\nBUSBITCHARS \"[]\" ;\nDIVIDERCHAR \"/\" ;\n";
  lef.precision(12);
  for (const auto& [id, name] : memories) {
    (void)id;
    const auto& macro = tech.macros.at(name);
    lef << "MACRO " << name << "\n CLASS BLOCK ;\n ORIGIN 0 0 ;\n SIZE " << macro.width << " BY " << macro.height << " ;\n";
    for (const auto& [pin_label, pin] : macro.pins) {
      lef << " PIN " << pin_label << "\n  DIRECTION " << pin.direction << " ;\n  USE SIGNAL ;\n  PORT\n";
      for (const auto& shape : pin.shapes) {
        const auto& r = shape.rect;
        lef << "   LAYER " << shape.layer << " ;\n   RECT " << r.xl << ' ' << r.yl << ' ' << r.xh << ' ' << r.yh << " ;\n";
      }
      lef << "  END\n END " << pin_label << "\n";
    }
    lef << "END " << name << "\n";
  }
  lef << "END LIBRARY\n";
  return lef.str();
}

void sat_window_statistics(rapidjson::Value& json, const Satlegalize_report& report, const Satlegalize_options& options,
                           rapidjson::Document::AllocatorType& allocator) {
  rapidjson::Value windows(rapidjson::kArrayType);
  uint64_t         total = 0, minimum = std::numeric_limits<uint64_t>::max(), maximum = 0;
  for (const auto& window : report.window_stats) {
    rapidjson::Value entry(rapidjson::kObjectType), bounds(rapidjson::kArrayType);
    for (double value : {window.bounds.xl, window.bounds.yl, window.bounds.xh, window.bounds.yh}) {
      bounds.PushBack(value, allocator);
    }
    entry.AddMember("bounds_dbu", bounds, allocator);
    entry.AddMember("movable_cells", static_cast<uint64_t>(window.movable_cells), allocator);
    entry.AddMember("oversized", window.oversized, allocator);
    entry.AddMember("trials", window.trials, allocator);
    entry.AddMember("accepted", window.accepted, allocator);
    entry.AddMember("accepted_already_aligned_at_trial_start", window.already_aligned, allocator);
    entry.AddMember("accepted_newly_aligned_at_trial_start", window.accepted - window.already_aligned, allocator);
    entry.AddMember("conflicts", window.conflicts, allocator);
    entry.AddMember("unknown", window.unknown, allocator);
    entry.AddMember("proposed_moved_cells", window.moved_cells, allocator);
    entry.AddMember("proposed_displacement_dbu", window.displacement_dbu, allocator);
    entry.AddMember("proposed_max_displacement_dbu", window.max_displacement_dbu, allocator);
    entry.AddMember("candidate_packing", rapidjson::Value(window.packing_status.c_str(), allocator), allocator);
    if (options.route_grid) {
      entry.AddMember("route_utilization", window.route_utilization, allocator);
      entry.AddMember("source_gap_cells", static_cast<uint64_t>(window.source_gap_cells), allocator);
      entry.AddMember("source_gap_candidates", static_cast<uint64_t>(window.source_gap_candidates), allocator);
    }
    if (options.region_width_um > 0 || options.route_grid) {
      entry.AddMember("candidate_cells", static_cast<uint64_t>(window.candidate_cells), allocator);
      entry.AddMember("retained_candidates", static_cast<uint64_t>(window.retained_candidates), allocator);
      entry.AddMember("spill_eligible_cells", static_cast<uint64_t>(window.spill_eligible_cells), allocator);
      if (!window.first_no_candidate_cell.empty()) {
        rapidjson::Value blocker(rapidjson::kObjectType), pose(rapidjson::kArrayType);
        blocker.AddMember("cell", rapidjson::Value(window.first_no_candidate_cell.c_str(), allocator), allocator);
        blocker.AddMember("master", rapidjson::Value(window.first_no_candidate_master.c_str(), allocator), allocator);
        blocker.AddMember("spill_reason", rapidjson::Value(window.first_no_candidate_spill_reason.c_str(), allocator), allocator);
        const auto& place = window.first_no_candidate_place;
        for (int64_t value : {place.x, place.y, place.width, place.height}) {
          pose.PushBack(value, allocator);
        }
        blocker.AddMember("xywh_dbu", pose, allocator);
        blocker.AddMember("orientation", rapidjson::Value(ph::orientation_name(place.orientation).c_str(), allocator), allocator);
        blocker.AddMember("minimum_connected_slack_ps",
                          window.first_no_candidate_slack_ps ? rapidjson::Value(*window.first_no_candidate_slack_ps)
                                                             : rapidjson::Value(rapidjson::kNullType),
                          allocator);
        entry.AddMember("first_no_candidate", blocker, allocator);
      }
      entry.AddMember("usable_area_dbu2", window.usable_area, allocator);
      entry.AddMember("movable_area_before_dbu2", window.movable_area_before, allocator);
      entry.AddMember("movable_area_after_dbu2", window.movable_area_after, allocator);
      entry.AddMember(
          "density_before",
          std::isfinite(window.density_before) ? rapidjson::Value(window.density_before) : rapidjson::Value(rapidjson::kNullType),
          allocator);
      entry.AddMember(
          "density_after",
          std::isfinite(window.density_after) ? rapidjson::Value(window.density_after) : rapidjson::Value(rapidjson::kNullType),
          allocator);
      entry.AddMember("spill_candidates", window.spill_candidates, allocator);
      entry.AddMember("spilled_cells", window.spilled_cells, allocator);
      rapidjson::Value neighbors(rapidjson::kArrayType);
      for (const auto& neighbor : window.neighbors) {
        rapidjson::Value tile(rapidjson::kObjectType), tile_bounds(rapidjson::kArrayType);
        for (double value : {neighbor.bounds.xl, neighbor.bounds.yl, neighbor.bounds.xh, neighbor.bounds.yh}) {
          tile_bounds.PushBack(value, allocator);
        }
        tile.AddMember("bounds_dbu", tile_bounds, allocator);
        tile.AddMember("movable_cells", static_cast<uint64_t>(neighbor.movable_cells), allocator);
        if (options.route_grid) {
          tile.AddMember("route_utilization", neighbor.route_utilization, allocator);
        }
        tile.AddMember("usable_area_dbu2", neighbor.usable_area, allocator);
        tile.AddMember("movable_area_before_dbu2", neighbor.movable_area_before, allocator);
        tile.AddMember("movable_area_after_dbu2", neighbor.movable_area_after, allocator);
        tile.AddMember("density_before",
                       std::isfinite(neighbor.density_before) ? rapidjson::Value(neighbor.density_before)
                                                              : rapidjson::Value(rapidjson::kNullType),
                       allocator);
        tile.AddMember("density_after",
                       std::isfinite(neighbor.density_after) ? rapidjson::Value(neighbor.density_after)
                                                             : rapidjson::Value(rapidjson::kNullType),
                       allocator);
        neighbors.PushBack(tile, allocator);
      }
      entry.AddMember("neighbors", neighbors, allocator);
    }
    windows.PushBack(entry, allocator);
    total   += window.movable_cells;
    minimum  = std::min(minimum, static_cast<uint64_t>(window.movable_cells));
    maximum  = std::max(maximum, static_cast<uint64_t>(window.movable_cells));
  }
  rapidjson::Value counts(rapidjson::kObjectType);
  counts.AddMember("regions", static_cast<uint64_t>(report.window_stats.size()), allocator);
  counts.AddMember("total_memberships", total, allocator);
  counts.AddMember("minimum", report.window_stats.empty() ? uint64_t{0} : minimum, allocator);
  counts.AddMember("maximum", maximum, allocator);
  counts.AddMember("mean", report.window_stats.empty() ? 0. : double(total) / report.window_stats.size(), allocator);
  json.AddMember("window_cell_counts", counts, allocator);
  json.AddMember("window_stats", windows, allocator);
  json.AddMember("max_pairs_per_window", options.max_pairs_per_window, allocator);
  json.AddMember("check_packing", options.check_packing, allocator);
  json.AddMember("route_peak_floor", options.route_peak_floor, allocator);
  json.AddMember("max_moved_cells", options.max_moved_cells, allocator);
  json.AddMember("accepted_already_aligned_at_trial_start", report.already_aligned, allocator);
  json.AddMember("accepted_newly_aligned_at_trial_start",
                 static_cast<uint64_t>(report.accepted.size() - report.already_aligned),
                 allocator);
  if (options.region_width_um > 0 || options.route_grid) {
    rapidjson::Value region(rapidjson::kObjectType);
    region.AddMember("width_um", options.region_width_um, allocator);
    region.AddMember("height_um", options.region_height_um, allocator);
    region.AddMember("spill_neighbors", options.spill_neighbors, allocator);
    region.AddMember("spill_slack_margin_ps", options.spill_slack_margin_ps, allocator);
    region.AddMember("sampled_windows", report.sampled_windows, allocator);
    region.AddMember("routing_grid", options.route_grid, allocator);
    region.AddMember("routing_guidance", options.route_grid && options.route_guidance, allocator);
    region.AddMember("cross_grid_pairs", report.cross_grid_pairs, allocator);
    region.AddMember("ordering",
                     rapidjson::Value(options.route_grid && options.route_guidance
                                          ? "initial route utilization descending, current cell density tie-break"
                                          : "current density descending within sampled timing-selected regions",
                                      allocator),
                     allocator);
    region.AddMember("spill_candidates", report.spill_candidates, allocator);
    region.AddMember("accepted_cell_transfers", report.spilled_cells, allocator);
    region.AddMember("density_rejected", report.density_rejected, allocator);
    region.AddMember("region_backoff", false, allocator);
    region.AddMember("prefix_backoff", false, allocator);
    json.AddMember("fixed_regions", region, allocator);
  }
}

// Fully assigned combinational Verilog regs can retain a Latch declaration
// after mapping. Like cgen's always_comb emission, an always-open latch with
// no reset is just its data value. Normalize only the private physical graph.
void remove_transparent_latches(hhds::Graph& graph) {
  std::vector<Node> aliases;
  for (auto node : graph.body().nodes()) {
    if (gu::type_op_of(node) != Ntype_op::Latch || !gu::get_driver_of_sink_name(node, "reset_pin").is_invalid()
        || !gu::get_driver_of_sink_name(node, "clock_pin").is_invalid()) {
      continue;
    }
    const auto enable = gu::get_driver_of_sink_name(node, "enable");
    if (!enable.is_invalid()) {
      const auto polarity = gu::get_driver_of_sink_name(node, "posclk");
      if (!enable.is_const() || gu::const_of(enable).has_unknowns() || (!polarity.is_invalid() && !polarity.is_const())
          || (polarity.is_const() && gu::const_of(polarity).has_unknowns())) {
        continue;
      }
      const bool active_low = polarity.is_known_false();
      if (active_low != gu::const_of(enable).is_known_zero()) {
        continue;
      }
    }
    const auto din = gu::get_driver_of_sink_name(node, "din");
    const auto q   = node.try_get_driver_pin(0);
    if (!din.is_invalid() && !q.is_invalid() && din != q && gu::bits_of(din) == gu::bits_of(q)) {
      aliases.push_back(node);
    }
  }
  for (auto node : aliases) {
    const auto din = gu::get_driver_of_sink_name(node, "din");
    const auto q   = node.get_driver_pin(0);
    if (din == q) {
      throw std::runtime_error("cycle in always-transparent latch aliases");
    }
    std::vector<Pin> sinks;
    for (const auto& edge : q.out_edges()) {
      sinks.push_back(edge.sink);
    }
    gu::carry_signal_roles(q, din);
    for (auto sink : sinks) {
      sink.del_sink(q);
      sink.connect_driver(din);
    }
    node.del_node();
  }
}
uint64_t    hash_text(std::string_view text, uint64_t hash = hash_util::kFnv1a64_offset) { return hash_util::fnv1a64(text, hash); }
std::string file_text(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot read " + path);
  }
  return {std::istreambuf_iterator<char>(input), {}};
}
std::pair<double, double> effective_halo(const Options& options) {
  return options.cell_stage && options.cell_halo && !options.timing_seed && !options.retain_placement
             ? *options.cell_halo
             : std::pair{options.halo_x, options.halo_y};
}
void validate_cell_halo(const Options& options) {
  if (options.cell_halo
      && (!std::isfinite(options.cell_halo->first) || !std::isfinite(options.cell_halo->second) || options.cell_halo->first < 0
          || options.cell_halo->second < 0)) {
    throw std::invalid_argument("cell halo must be finite and nonnegative");
  }
  if (options.cell_halo
      && ((options.halo_x >= 0 && options.cell_halo->first > options.halo_x)
          || (options.halo_y >= 0 && options.cell_halo->second > options.halo_y))) {
    throw std::invalid_argument("cell halo cannot exceed macro halo on either axis");
  }
}
Options resolve_macro_halo(const Options& options, double row_height) {
  auto resolved = options;
  if (resolved.halo_x == -1 || resolved.halo_y == -1) {
    if (!std::isfinite(row_height) || row_height <= 0) {
      throw std::invalid_argument("automatic macro halo requires a positive placement-row height");
    }
    if (resolved.halo_x == -1) {
      resolved.halo_x = 10 * row_height;
    }
    if (resolved.halo_y == -1) {
      resolved.halo_y = 10 * row_height;
    }
  }
  validate_cell_halo(resolved);
  return resolved;
}
void check_macro_halo(const ph::Technology& tech, const ph::Floorplan& fp, const Options& options) {
  if (!options.cell_stage || !options.cell_halo || options.timing_seed || options.retain_placement) {
    return;
  }
  ph::Floorplan macros;
  macros.die  = fp.die;
  macros.dbu  = fp.dbu;
  macros.rows = fp.rows;
  for (const auto& c : fp.components) {
    if (!tech.macros.at(c.macro).klass.starts_with("CORE")) {
      macros.components.push_back(c);
    }
  }
  for (const auto& c : macros.components) {
    if (c.place.status != Status::FIXED && c.place.status != Status::LOCKED) {
      throw std::invalid_argument("cell halo requires a fixed macro frame: " + c.name);
    }
  }
  check_legality(tech, macros, options.halo_x, options.halo_y);
}
int64_t macro_halo(double microns, int64_t dbu, int64_t pitch) {
  const double value = std::ceil(microns * dbu / pitch - 1e-9) * pitch;
  if (!std::isfinite(microns) || microns < 0 || !std::isfinite(value)
      || value > static_cast<double>(std::numeric_limits<int64_t>::max() / 4)) {
    throw std::invalid_argument("invalid macro halo or coordinate range");
  }
  return static_cast<int64_t>(value);
}
// Process policy for these options: an enabled native power grid names the
// technology profile explicitly; otherwise it is detected from the LEF.
static const ph::Tech_profile& placement_tech_profile(const ph::Technology& tech, const Options& options) {
  const bool pg = options.pg != "none" && options.pg != "off";
  return ph::resolve_tech_profile(tech, pg ? std::string_view(options.pg) : std::string_view("auto"));
}
std::string environment_key(const Options& o, const ph::Technology& tech) {
  uint64_t h = kPlaceSalt;
  h          = hash_text(std::to_string(o.technology().content_hash), h);
  if (!o.floorplan.empty()) {
    h = hash_text(file_text(o.floorplan), h);
  }
  h = hash_text("placement-mask-semantics-v2", h);
  h = hash_text(std::format("rc_pessimism:{}", o.rc_pessimism), h);
  if (!o.tech_rc_file.empty()) {
    h = hash_text(file_text(o.tech_rc_file), h);
  }
  if (!o.tracks_file.empty()) {
    h = hash_text(file_text(o.tracks_file), h);
  }
  h = hash_text(std::format("{}:{}:{}:{}:{}:{}:{}:{}:{}:{}",
                            o.utilization,
                            o.aspect,
                            o.density,
                            o.full,
                            o.iterations,
                            o.bins,
                            o.threads,
                            tech.dbu,
                            o.halo_x,
                            o.halo_y),
                h);
  if (o.cell_halo) {
    h = hash_text(std::format("cell-halo:{}:{}:{}", o.cell_halo->first, o.cell_halo->second, o.cell_stage), h);
  }
  h = hash_text(o.macro_mode, h);
  if (o.macro_magnet.enabled) {
    const auto& m = o.macro_magnet;
    h             = hash_text(std::format("macro-magnet-v7-convergence-continuation:{}:{}:{}:{}:{}:{}:{}:{}",
                                          m.settle_window,
                                          m.settle_displacement,
                                          m.max_gap,
                                          m.density_fraction,
                                          m.max_per_round,
                                          m.max_rounds,
                                          m.start_displacement,
                                          m.before_snap),
                              h);
  }
  for (const auto& [stage, policy] : {
           std::pair{   "fp",    o.fp_warp},
           std::pair{"macro", o.macro_warp}
  }) {
    if (policy.enabled) {
      h = hash_text(std::format("warp-once-after-density:{}", policy.once_after_density), h);
      h = hash_text(
          std::format("warp-constant-v1:{}:{}:{}", policy.start_stability, policy.stability_window, policy.refresh_interval),
          h);
      h = hash_text(std::format("density-warp-v6-constant-projected:{}:{}:{}:{}:{}:{}:{}",
                                stage,
                                policy.bins,
                                policy.iterations,
                                policy.weight,
                                policy.warmup,
                                policy.updates,
                                policy.seconds),
                    h);
    }
  }

  h = hash_text("prects-controls:" + o.control_ports, h);
  if (o.reset_fanout) {
    // v2: every reset net (Liberty clear/preset pins, `reset_pins`, synthesis reset roles).
    h = hash_text(std::format("reset-fanout-v2:{}", o.reset_fanout), h);
    for (const auto& [macro, names] : o.reset_pins) {
      for (const auto& name : names) {
        h = hash_text(macro + ":" + name, h);
      }
    }
  }
  if (o.domino_rail_fanout && !o.domino_rail_pins.empty()) {
    h = hash_text(std::format("domino-rail-fanout:{}", o.domino_rail_fanout), h);
    for (const auto& [macro, names] : o.domino_rail_pins) {
      for (const auto& name : names) {
        h = hash_text(macro + ":" + name, h);
      }
    }
  }
  if (!o.die.empty() || !o.core.empty()) {
    h = hash_text("fixed-frame", h);
    for (const auto v : o.die) {
      h = hash_text(std::format(":d{}", v), h);
    }
    for (const auto v : o.core) {
      h = hash_text(std::format(":c{}", v), h);
    }
  }
  if (o.timing_max_weight != 5 || o.timing_smoothing != .5) {
    h = hash_text(std::format("timing-force:{}:{}", o.timing_max_weight, o.timing_smoothing), h);
  }
  if (o.satlegalize_options.route_grid) {
    h = hash_text(std::format("sat-route-grid-v1:{}", o.satlegalize_options.route_guidance), h);
  }
  if (o.satlegalize_route_vias) {
    h = hash_text("satlegalize-route-vias-v1", h);
  }
  if (o.satlegalize_pg_after) {
    h = hash_text("satlegalize-pg-after-v1", h);
  }
  if (o.satlegalize_route) {
    h = hash_text(std::format("satlegalize-route-v2:{}", o.satlegalize_route_critical_fraction), h);
  }
  if (o.satlegalize) {
    const auto& a = o.satlegalize_options;
    h             = hash_text(std::format("satlegalize:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}",
                                          a.backend,
                                          a.encoding,
                                          a.max_cells,
                                          a.max_candidates,
                                          a.max_windows,
                                          a.max_pairs,
                                          a.move_sites,
                                          a.move_rows,
                                          a.conflicts,
                                          a.milliseconds,
                                          a.skip_conflicts,
                                          a.boundary_growth),
                              h);
    if (a.max_pairs_per_window || a.check_packing) {
      h = hash_text(std::format("satlegalize-search-v1:{}:{}", a.max_pairs_per_window, a.check_packing), h);
    }
    if (a.route_peak_floor) {
      h = hash_text(std::format("satlegalize-route-peak-floor:{}", a.route_peak_floor), h);
    }
    if (a.max_moved_cells) {
      h = hash_text(std::format("satlegalize-moved-cells:{}", a.max_moved_cells), h);
    }
    if (a.region_width_um || a.region_height_um || a.spill_neighbors) {
      h = hash_text(std::format("satlegalize-region-v1:{}:{}:{}:{}",
                                a.region_width_um,
                                a.region_height_um,
                                a.spill_neighbors,
                                a.spill_slack_margin_ps),
                    h);
    }
  }
  if (o.legalize_timing_corrections) {
    h = hash_text(std::format("timing-corrections:{}", o.legalize_timing_corrections), h);
  }
  if (o.legalize_timing_strength > 0) {
    h = hash_text(std::format("timing-legalize:{}:{}", o.legalize_timing_strength, o.legalize_timing_threshold), h);
  }
  if (!o.optimization_pending) {
    const auto& t = o.terminal_legalize_options;
    if (t.remainder_warp.enabled) {
      h = hash_text(std::format("remainder-warp:{}:{}:{}:{}",
                                t.remainder_warp.bins,
                                t.remainder_warp.weight,
                                t.remainder_warp.iterations,
                                t.remainder_warp.seconds),
                    h);
    }
    h = hash_text(std::format("terminal-legalize:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}",
                              t.max_density,
                              t.critical_fraction,
                              t.local_pair_fraction,
                              t.padding_sites,
                              t.candidates,
                              t.pair_move_sites,
                              t.pair_move_rows,
                              t.sat_conflicts,
                              t.sat_milliseconds,
                              t.critical_pair_limit,
                              t.local_pair_limit,
                              t.backend,
                              t.critical_padding_sites,
                              t.critical_fanin_depth,
                              t.joint_minimize_total,
                              t.padding_max_extra_rows,
                              t.remainder_wire_weight),
                  h);
  }
  if (o.timing_exponent != 1) {
    h = hash_text(std::format("timing-exponent:{}", o.timing_exponent), h);
  }
  if (o.timing_spread_weight != 0) {
    h = hash_text(std::format("timing-spread:{}", o.timing_spread_weight), h);
  }
  if (o.fp_evaluate && o.macro_timing_interval > 0) {
    h = hash_text(std::format("macro-timing:{}:{}:{}:{}",
                              o.macro_timing_interval,
                              o.fp_timing_movement,
                              o.fp_timing_after_density,
                              o.fp_timing_max_updates),
                  h);
  }
  if (o.stop_overflow != .1 || o.initial_density_weight != .01) {
    h = hash_text(std::format("density-controls:{}:{}", o.stop_overflow, o.initial_density_weight), h);
  }
  if (o.fp_timing_max_updates != 10) {
    h = hash_text(std::format("fp-timing-budget:{}", o.fp_timing_max_updates), h);
  }
  if (o.timing_fraction != .2) {
    h = hash_text(std::format("timing-fraction:{}", o.timing_fraction), h);
  }
  if (o.fixed_macro_overlap_weight > 0) {
    h = hash_text(std::format("fixed-macro-overlap:{}", o.fixed_macro_overlap_weight), h);
  }
  if (o.cell_jump.depth > 1) {
    const auto& jump = o.cell_jump;
    h                = hash_text(std::format("cell-jump:{}:{}:{}:{}", jump.depth, jump.settle, jump.interval, jump.fix_period), h);
  }
  if (o.gp_search.enabled) {
    const auto& a = o.gp_search;
    h             = hash_text(std::format("gp_ab_v1:{}:{}:{}:{}:{}:{}:{}:{}:{}",
                                          a.start,
                                          a.interval,
                                          a.steps,
                                          a.rounds,
                                          a.filler_step,
                                          a.local_step,
                                          a.route_tolerance,
                                          a.timing_tolerance,
                                          a.hpwl_tolerance),
                              h);
  }
  if (o.vanished_memory) {
    h = hash_text(std::format("vanished_memory_v5:{}:{}:{}:{}",
                              o.vanished_memory_local_floor,
                              o.vanished_memory_initial_scale,
                              o.vanished_memory_growth_iterations,
                              o.vanished_memory_start),
                  h);
  }
  if (o.macro_transparency.enabled()) {
    h = hash_text(std::format("macro-transparency:{}:{}:{}:{}",
                              o.macro_transparency.cycles,
                              o.macro_transparency.warmup,
                              o.macro_transparency.period,
                              o.macro_transparency.always),
                  h);
  }
  if (o.eco_connectivity_weight > 0) {
    h = hash_text(std::format("eco-connectivity:{}", o.eco_connectivity_weight), h);
  }
  if (o.detailed_iterations != 2) {
    h = hash_text(std::format("detail-iterations:{}", o.detailed_iterations), h);
  }
  if (!o.macro_detailed_place) {
    h = hash_text("macro-detail:false", h);
  }
  if (o.handoff_taps) {
    h = hash_text("handoff-taps-v1:" + placement_tech_profile(tech, o).name, h);
  }
  if (o.row_access) {
    h = hash_text("row-access:" + o.row_access->identity(), h);
  }
  for (const auto& [master, t] : o.multirow_templates) {
    h = hash_text(std::format("multirow-template:{}:{}:{}", master, ph::row_access_name(t.half[0]), ph::row_access_name(t.half[1])),
                  h);
    if (!t.rail_twin.empty()) {
      h = hash_text("multirow-rail-twin-v1:" + t.rail_twin, h);
    }
  }
  if (o.multirow_eco) {
    h = hash_text(std::format("multirow-eco-v1:{}:{}", o.multirow_eco->shift_sites, o.multirow_eco->max_displaced), h);
  }
  if (o.custom_cells) {
    h = hash_text("custom-cells:" + o.custom_cells->identity, h);
  }
  if (o.pg != "none") {
    h = hash_text("native-pg-v1:" + o.pg, h);
  }
  for (const auto& channel : o.pg_channels) {
    h = hash_text(std::format("pg-channel:{}:{}:{},{},{},{}",
                              channel.style,
                              channel.layer,
                              channel.region.xl,
                              channel.region.yl,
                              channel.region.xh,
                              channel.region.yh),
                  h);
  }
  if (o.pg_channel_max_ir_drop_v) {
    h = hash_text(std::format("pg-channel-max-ir-drop-v:{}", *o.pg_channel_max_ir_drop_v), h);
  }
  if (!o.macro_legalize_cells) {
    h = hash_text("macro-legalize-cells:false", h);
  }
  h = hash_text(std::format("final-timing-start:{}", o.final_timing_start), h);
  h = hash_text("final-timing-schedule:" + o.final_timing_schedule, h);
  if (o.final_independent_timing) {
    h = hash_text("final-independent-timing:true", h);
  }
  if (o.wirelength_gamma_end != .05) {
    h = hash_text(std::format("wirelength-gamma-end:{}", o.wirelength_gamma_end), h);
  }
  if (o.warm_refinement) {
    h = hash_text("warm-refinement-smoothing", h);
  }
  if (o.min_iterations != 40) {
    h = hash_text(std::format("gp-min:{}", o.min_iterations), h);
  }
  if (o.post_density_factor) {
    h = hash_text(std::format("gp-post-density:{}:{}", o.post_density_factor, o.convergence_tolerance), h);
  }
  if (o.boundarypins.enabled) {
    h = hash_text(std::format("boundarypins:{}:{}:{}:{}:{}:{}:{}",
                              o.boundarypins.split,
                              o.boundarypins.order,
                              o.boundarypins.align,
                              o.boundarypins.iterations,
                              o.boundarypins.max_displacement_um,
                              o.region.merge_um,
                              o.region.max_cells),
                  h);
  }
  if (o.busorder.enabled) {
    const auto& b = o.busorder;
    h             = hash_text(std::format("busorder:{}:{}:{}:{}:{}:{}",
                                          b.iterations,
                                          b.refresh,
                                          o.region.max_cells,
                                          b.weight,
                                          o.region.merge_um,
                                          b.max_displacement_um),
                              h);
  }
  if (o.virtual_fillers && o.macro_fill_cell_space) {
    h = hash_text("macro-cell-space-fillers-v1", h);
  }
  if (o.virtual_fillers && o.macro_filler_fraction > 0) {
    h = hash_text(std::format("macro-sized-fillers-v1:{}", o.macro_filler_fraction), h);
  }
  if (o.virtual_fillers) {
    h = hash_text(std::format("virtual-fillers:{}:{}:{}:{}",
                              o.virtual_filler_max_count,
                              o.virtual_filler_fraction,
                              o.virtual_filler_cell_scale,
                              o.virtual_filler_bin_fraction),
                  h);
  }
  if (o.route_bins) {
    h = hash_text(std::format("route-grid:{}", o.route_bins), h);
  }
  if (o.legalize_bins) {
    h = hash_text(std::format("legalize-bin-grid:{}", o.legalize_bins), h);
  }
  if (o.dynamic_bins || o.local_density_weight > 0 || o.macro_cell_density_weight > 0 || o.macro_density_pull_weight > 0) {
    h = hash_text(std::format("local-spread:{}:{}:{}:{}:{}:{}:{}",
                              o.dynamic_bins,
                              o.bin_max,
                              o.local_density_weight,
                              o.local_density_bins,
                              o.local_density_range,
                              o.local_density_jitter,
                              o.local_density_target),
                  h);
  }
  if (o.macro_cell_density_weight > 0) {
    h = hash_text(std::format("macro-cell-density-long-range:{}:{}", o.macro_cell_density_weight, o.macro_cell_density_reach), h);
  }
  if (o.local_density_weight > 0 && o.local_density_target >= 0) {
    h = hash_text(std::format("local-density-target:{}", o.local_density_target), h);
  }
  if (o.macro_density_pull_weight > 0) {
    h = hash_text(std::format("macro-density-pull:{}:{}", o.macro_density_pull_weight, o.macro_density_pull_range), h);
  }
  if (o.exact_cell_density) {
    h = hash_text("exact-cell-density", h);
  }
  if (o.density_upper_bound) {
    h = hash_text("density-upper-bound", h);
  }
  if (o.fp_place) {
    h = hash_text(std::format("centroid-copies:per-parent:{}", o.fp_min_copies), h);
    if (o.fp_min_macros != 2) {
      h = hash_text(std::format("centroid-min-macros:{}", o.fp_min_macros), h);
    }
  }
  if (o.gp_resize) {
    h = hash_text(std::format("gp-buffer-repair:{}", o.gp_buffer_margin), h);
    h = hash_text(std::format("gp-buffer-trials:{}", o.gp_buffer_trials), h);
    for (const auto& pattern : o.resize_dont_use) {
      h = hash_text("gp-dont-use:" + pattern, h);
    }
  }
  if (o.fp_place) {
    h = hash_text(std::format("fp-timing:{}:{}:{}:{}", o.fp_timing_interval, bool(o.fp_evaluate), o.fp_resize, bool(o.gp_rank)), h);
    if (o.repair_reset) {
      h = hash_text("repair-reset", h);  // FP resize may then touch reset cones
    }
    if (o.fp_resize && o.resize_engine != "legacy") {
      h = hash_text(std::format("resize-engine:{}:{}", o.resize_engine, o.resize_vt_swap), h);
    }
    if (o.fp_resize) {
      for (const auto& pattern : o.resize_dont_use) {
        h = hash_text("dont-use:" + pattern, h);
      }
    }
    h = hash_text(std::format("fp-movement:{}", o.fp_timing_movement), h);
    if (o.fp_timing_after_density) {
      h = hash_text("fp-timing-after-density", h);
    }
    h = hash_text(std::format("floorplan:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}",
                              o.fp_iterations,
                              o.fp_minimum,
                              o.fp_attraction,
                              o.fp_repulsion,
                              o.fp_anchor,
                              o.fp_snap.alignment_tolerance,
                              o.fp_snap.spacing_tolerance,
                              o.fp_snap.spacing_penalty,
                              o.fp_snap.symmetry_tolerance,
                              o.fp_snap.max_displacement),
                  h);
  }
  if (o.fp_place && o.fp_anchor_macros_only) {
    h = hash_text("fp:macro-only-frozen-anchors", h);
  }
  if (o.fp_place && o.fp_distance_attraction) {
    h = hash_text(std::format("fp:distance-attraction:{}", o.fp_attraction_outside), h);
  }
  if (o.fp_place && o.fp_snap_enabled) {
    h = hash_text("fp:circle-core-bounds", h);
  }
  if (o.fp_place && o.fp_parent_weight > 0) {
    h = hash_text(std::format("fp:parent-spring:{}:{}", o.fp_parent_weight, o.fp_parent_outside), h);
  }
  if (o.fp_place && !o.fp_snap_enabled) {
    h = hash_text("fp:no_snap", h);
  }
  if (o.fp_place && o.fp_colors) {
    h = hash_text("fp:uncovered-colors", h);
    if (o.fp_color_source != "synthesis" || o.fp_color_minimum != -1) {
      h = hash_text(std::format("fp-colors:{}:{}", o.fp_color_source, o.fp_color_minimum), h);
      if (o.fp_color_source == "memory_first") {
        h = hash_text(std::format("memory-first:{}", o.fp_color_maximum), h);
      }
    }
  }
  if (o.fp_place && o.fp_pin_adjust) {
    h = hash_text(std::format("fp-pins:{}:{}:{}", o.fp_pin_interval, o.fp_pin_step, o.fp_pin_improvement), h);
  }
  if (o.macro_pin_adjust) {
    h = hash_text(std::format("macro-pins:{}:{}:{}", o.macro_pin_interval, o.macro_pin_step, o.macro_pin_improvement), h);
  }
  if (o.scale_macro_density || o.automatic_bins || o.exact_macro_density || o.adaptive_density || o.routability
      || o.macro_overlap_weight || o.macro_jitter || o.schedule_iterations || o.io_iterations) {
    h = hash_text(std::format("repulsion:{}:{}:{}:{}:{}:{}:{}:{}:{}",
                              o.automatic_bins,
                              o.exact_macro_density,
                              o.adaptive_density,
                              o.routability,
                              o.macro_overlap_weight,
                              o.macro_jitter,
                              o.schedule_iterations,
                              o.io_iterations,
                              o.scale_macro_density),
                  h);
  }
  if (o.macro_snap) {
    h             = hash_text(o.macro_snap_mode, h);
    const auto& p = o.macro_snap_options;
    h             = hash_text(std::format("scope-size-snap:{}:{}:{}:{}:{}",
                                          p.alignment_tolerance,
                                          p.spacing_tolerance,
                                          p.spacing_penalty,
                                          p.symmetry_tolerance,
                                          p.max_displacement),
                              h);
  }
  if (o.fp_align_weight > 0) {
    h = hash_text(
        std::format("centroid-alignment:{}:{}:{}:{}", o.fp_align_weight, o.fp_align_sigma, o.fp_align_start, o.fp_align_full),
        h);
  }
  if (o.macro_align_weight > 0 && o.macro_align_global) {
    h = hash_text("global-master-strip-alignment-v1", h);
  }
  if (o.macro_align_weight > 0 || o.macro_snap) {
    h = hash_text(std::format("alignment:{}:{}:{}:{}:{}:{}:{}:{}:{}",
                              o.macro_align_weight,
                              o.macro_align_sigma,
                              o.macro_align_start,
                              o.macro_align_full,
                              o.macro_align_overlap_boost,
                              o.macro_align_nearest,
                              o.macro_snap,
                              o.macro_snap_tolerance,
                              o.macro_snap_weight),
                  h);
  }
  h = hash_text(std::format("uroute:{}:{}:{}:{}:{}:{}",
                            o.uroute.dir,
                            o.uroute.export_features,
                            o.uroute.shots,
                            o.uroute.min_shots,
                            o.uroute.fallback,
                            o.uroute.drc_level),
                h);
  h = hash_text(std::format("route-model:{}:{}:{}", o.route_model, o.route_degree, o.route_accuracy), h);
  for (const auto* stage : {&o.fp_route, &o.macro_route, &o.final_route}) {
    h = hash_text(std::format("route-stage:{}:{}:{}:{}:{}:{}:{}:{}:{}",
                              stage->model,
                              stage->policy.switch_overflow,
                              stage->policy.target_overflow,
                              stage->policy.hold,
                              stage->policy.probe,
                              stage->policy.probe_iterations,
                              dp::route_model_name(stage->policy.probe_model),
                              stage->policy.uroute_switch_overflow,
                              stage->policy.uroute_hold),
                  h);
  }
  if (o.routability && !o.routability_charge_compensation) {
    h = hash_text("inflation-only-charge", h);
  }
  if (o.routability) {
    h = hash_text("opaque-macro-routing-v1", h);
    if (o.routability_macro_weight > 0) {
      h = hash_text(std::format("macro-route-pressure:{}:{}", o.routability_macro_weight, o.routability_macro_range), h);
    }
  }
  if (o.cohesion > 0) {
    h = hash_text(std::format("cohesion:{}", o.cohesion), h);
  }
  if (o.preserve_imported_ties) {
    h = hash_text("preserve-imported-ties", h);
  }
  if (o.unknown_const_zero) {
    h = hash_text("unknown-const-zero", h);
  }
  if (o.macro_mode == "h2d") {
    h = hash_text(mp::parameter_key(o.macro_parameters), h);
  }
  for (const auto& [name, weight] : o.net_weights) {
    h = hash_text(std::format("{}:{}", name, weight), h);
  }
  return std::format("{:016x}", h);
}
struct Tie_cell {
  liberty::Constant_cell cell;
  std::string            output;
  double                 area     = 0;
  bool                   flipless = false;  // no LEF SYMMETRY X: cannot sit in an FS row
};
// Match characterized constant functions to physically usable cells. Neither
// technology names nor conventional tie/pin spellings identify their polarity.
// X-symmetric masters rank first so alternating N/FS rows stay usable.
std::array<std::optional<Tie_cell>, 2> find_tie_cells(const Options& options, const ph::Technology& tech,
                                                      const std::string& site_name) {
  std::array<std::optional<Tie_cell>, 2> selected;
  const auto&                            site = tech.sites.at(site_name);
  for (const auto& cell : options.technology().cells.constants) {
    const auto it = tech.macros.find(cell.name);
    if (it == tech.macros.end()) {
      continue;
    }
    const auto& macro   = it->second;
    const auto  aligned = [](double value) { return std::abs(value - std::round(value)) < 1e-6; };
    if (!macro.klass.starts_with("CORE") || (!macro.site.empty() && macro.site != site_name)
        || !std::isfinite(macro.width * macro.height) || macro.width <= 0 || macro.height <= 0 || !aligned(macro.width / site.width)
        || !aligned(macro.height / site.height)) {
      continue;
    }
    bool usable = true;
    for (const auto& [name, value] : cell.outputs) {
      (void)value;
      const auto pin  = macro.pins.find(name);
      usable         &= pin != macro.pins.end() && pin->second.direction == "OUTPUT" && !pin->second.shapes.empty();
    }
    for (const auto& [name, pin] : macro.pins) {
      if (pin.use != "POWER" && pin.use != "GROUND") {
        usable &= pin.direction == "OUTPUT" && std::any_of(cell.outputs.begin(), cell.outputs.end(), [&](const auto& output) {
                    return output.first == name;
                  });
      }
    }
    if (!usable) {
      continue;
    }
    for (const auto& [name, value] : cell.outputs) {
      Tie_cell candidate{cell, name, macro.width * macro.height, !macro.symmetry_x};
      auto&    best = selected[value];
      if (!best
          || std::tie(candidate.flipless, candidate.area, candidate.cell.name, candidate.output)
                 < std::tie(best->flipless, best->area, best->cell.name, best->output)) {
        best = std::move(candidate);
      }
    }
  }
  return selected;
}
std::string pin_name(Pin pin) {
  auto n = pin.get_master_node();
  if (auto io = n.get_subnode_io()) {
    const auto& decls = pin.is_driver() ? io->get_output_pin_decls() : io->get_input_pin_decls();
    for (const auto& d : decls) {
      if (d.port_id == pin.get_port_id()) {
        return d.name;
      }
    }
  }
  return std::string(pin.get_pin_name());
}
std::string bit_name(const std::string& name, int bits, int bit) { return bits > 1 ? std::format("{}[{}]", name, bit) : name; }
struct Endpoint {
  std::string instance, pin;
  int         constant                                     = -1;
  friend bool operator==(const Endpoint&, const Endpoint&) = default;
  friend bool operator<(const Endpoint& a, const Endpoint& b) {
    return std::tie(a.constant, a.instance, a.pin) < std::tie(b.constant, b.instance, b.pin);
  }
  std::string label() const { return ph::connection_label(instance, pin); }
};
// Lower wiring by demand. The placed graph retains its bus interfaces and pure
// wiring nodes; only physical endpoints enter the placer's array model.
class Resolver {
  std::map<std::pair<uint64_t, int>, Endpoint> memo;
  std::set<std::pair<uint64_t, int>>           busy;
  // Distinct unknown constant bits tied low: a top output's driver is resolved twice (the net,
  // then the rewrite to its tie cell), so count the (driver, bit) once, not each lookup.
  std::set<std::pair<uint64_t, int>>           tied_unknown;
  const ph::Technology&                        tech;
  const std::map<uint64_t, std::string>&       memories;
  bool                                         unknown_as_zero;

public:
  // unknown_as_zero: tie an unknown constant bit low (a legal refinement of a don't-care)
  // instead of refusing the net; the number of bits tied this way is unknown_tied().
  explicit Resolver(const ph::Technology& t, const std::map<uint64_t, std::string>& m, bool unknown_zero = false)
      : tech(t), memories(m), unknown_as_zero(unknown_zero) {}
  size_t   unknown_tied() const { return tied_unknown.size(); }
  Endpoint bit(Pin p, int b, int depth = 0) {
    if (p.is_invalid() || b < 0) {
      throw std::runtime_error("invalid physical wiring operand");
    }
    if (depth > 4096) {
      throw std::runtime_error("physical wiring depth exceeds 4096");
    }
    if (p.is_const()) {
      const auto& c = gu::const_of(p);
      if (c.unknown_bit_test(b)) {
        if (!unknown_as_zero) {
          throw std::runtime_error("physical net has an unknown constant (synth.place.unknown_const=zero ties it low)");
        }
        tied_unknown.emplace(static_cast<uint64_t>(p.get_debug_pid()), b);
        return {{}, {}, 0};
      }
      return {{}, {}, c.bit_test(b) ? 1 : 0};
    }
    auto key = std::pair{static_cast<uint64_t>(p.get_debug_pid()), b};
    if (auto it = memo.find(key); it != memo.end()) {
      return it->second;
    }
    if (!busy.insert(key).second) {
      throw std::runtime_error("cycle in physical wiring aliases");
    }
    auto     n     = p.get_master_node();
    auto     input = [&](std::string_view name, int which) {
      const auto driver = gu::get_driver_of_sink_name(n, name);
      if (driver.is_invalid()) {
        throw std::runtime_error(std::format("invalid physical wiring operand: {} input {} of {} is undriven",
                                             gu::type_op_of(n) == Ntype_op::Sub ? "instance" : "wiring node",
                                             name,
                                             gu::debug_name(n)));
      }
      return bit(driver, which, depth + 1);
    };
    Endpoint out;
    if (n.is_input_node()) {
      const auto io    = n.get_graph()->get_io();
      const auto name  = std::string(p.get_pin_name());
      const auto width = std::max(1, static_cast<int>(io->get_bits(name)));
      if (b >= width) {
        out = gu::is_unsign(p) ? Endpoint{{}, {}, 0} : Endpoint{"PIN", bit_name(name, width, width - 1), -1};
      } else {
        out = {"PIN", bit_name(name, width, b), -1};
      }
    } else if (memories.contains(n.get_debug_nid())) {
      // The proxy only has pins below the read width; extend past it like the
      // generic branch (a signed pin repeats its MSB).
      const int width = std::max(1, gu::bits_of(p));
      if (b >= width) {
        out = gu::is_unsign(p) ? Endpoint{{}, {}, 0} : bit(p, width - 1, depth + 1);
      } else {
        out = {std::string(n.get_name()), memory_pin(p.get_port_id(), b, true), -1};
      }
    } else if (auto io = n.get_subnode_io(); io && tech.macros.contains(std::string(io->get_name()))) {
      const auto& macro = tech.macros.at(std::string(io->get_name()));
      auto        name  = pin_name(p);
      if (macro.pins.contains(name)) {
        if (b == 0) {
          out = {std::string(n.get_name()), name, -1};
        } else {
          out.constant = 0;  // scalar physical cell outputs are zero-extended
        }
      } else {
        name = std::format("{}[{}]", name, b);
        if (!macro.pins.contains(name)) {
          throw std::runtime_error("LEF output pin not found: " + std::string(io->get_name()) + "/" + name);
        }
        out = {std::string(n.get_name()), name, -1};
      }
    } else {
      const auto op    = gu::type_op_of(n);
      const int  width = std::max(1, gu::bits_of(p));
      if (b >= width) {
        // Bits past the declared width extend the value; a signed pin repeats its
        // MSB (the op-specific indexing below would read an unrelated operand bit).
        out = gu::is_unsign(p) ? Endpoint{{}, {}, 0} : bit(p, width - 1, depth + 1);
      } else if (op == Ntype_op::Get_mask || op == Ntype_op::Set_mask) {
        const auto range = gu::bit_range(n);
        if (!range) {
          throw std::runtime_error("nonconstant physical bit range");
        }
        if (op == Ntype_op::Get_mask) {
          out = input("a", b + range->first);
        } else {
          out = b >= range->first && b < range->second ? input("value", b - range->first) : input("a", b);
        }
      } else if (op == Ntype_op::Concat) {
        const auto lanes = gu::concat_lanes(n);
        if (lanes.empty()) {
          throw std::runtime_error("malformed physical concat");
        }
        out.constant = 0;
        for (const auto& lane : lanes) {
          if (b >= lane.offset && b < lane.offset + lane.width) {
            out = bit(lane.value, b - lane.offset, depth + 1);
          }
        }
      } else if (op == Ntype_op::Or || op == Ntype_op::And) {
        out.constant = op == Ntype_op::Or ? 0 : 1;
        std::vector<Pin> operands;
        for (auto sink : n.inp_sorted_pins()) {
          for (auto driver : sink.get_driver_pins()) {
            operands.push_back(driver);
          }
        }
        for (auto operand : operands) {
          const auto v = bit(operand, b, depth + 1);
          if (v.constant == (op == Ntype_op::Or ? 1 : 0)) {
            out = v;
            break;
          }
          if (v.constant >= 0) {
            continue;
          }
          if (out.constant < 0 && out != v) {
            throw std::runtime_error("unmapped Boolean operation in physical netlist");
          }
          out = v;
        }
      } else if (op == Ntype_op::SHL || op == Ntype_op::SRA || op == Ntype_op::Sext) {
        const auto value  = gu::get_driver_of_sink_name(n, "a");
        const auto amount = gu::get_driver_of_sink_name(n, "b");
        if (!amount.is_const() || !gu::const_of(amount).is_just_i64()) {
          throw std::runtime_error("unmapped shift in physical netlist");
        }
        const auto shift = gu::const_of(amount).to_just_i64();
        if (shift < 0 || shift > std::numeric_limits<int>::max() - b) {
          throw std::runtime_error("invalid physical shift");
        }
        if (op == Ntype_op::SHL) {
          out = b < shift ? Endpoint{{}, {}, 0} : bit(value, b - static_cast<int>(shift), depth + 1);
        } else if (op == Ntype_op::SRA) {
          out = bit(value, b + static_cast<int>(shift), depth + 1);
        } else {
          out = bit(value, std::min(b, static_cast<int>(shift)), depth + 1);
        }
      } else {
        throw std::runtime_error("physical placement requires mapped cells; found " + std::string(Ntype::get_name(op)) + " '"
                                 + std::string(n.get_name()) + "'");
      }
    }
    busy.erase(key);
    memo.emplace(key, out);
    return out;
  }
};
struct Cell {
  Node        node;
  std::string name, macro, block;
  Ann_place   place;
  bool        locked = false;
};
struct FloorplanPorts {
  color::Floorplan_kind kind            = color::Floorplan_kind::logic;
  bool                  input_allowlist = false;
  bool                  constant        = false;
  std::set<std::string> data, controls;
};
std::map<std::string, FloorplanPorts> floorplan_ports(const liberty::Cell_library& library) {
  std::map<std::string, FloorplanPorts> ports;
  for (const auto& c : library.drives) {
    if (c.sequential) {
      ports[c.name].kind = color::Floorplan_kind::barrier;
    }
  }
  auto flops = library.dffs;
  for (const auto& c : library.async_dffs) {
    flops.push_back(c);
  }
  for (const auto& c : library.latches) {
    flops.push_back(c);
  }
  for (const auto& c : flops) {
    auto& p           = ports[c.name];
    p.kind            = color::Floorplan_kind::state;
    p.input_allowlist = true;
    p.data.insert(c.d_pin);
    p.controls.insert(c.clk_pin);
    if (!c.reset0_pin.empty()) {
      p.controls.insert(c.reset0_pin);
    }
    if (!c.reset1_pin.empty()) {
      p.controls.insert(c.reset1_pin);
    }
  }
  for (const auto& c : library.memories) {
    auto& p           = ports[c.name];
    p.kind            = color::Floorplan_kind::macro;
    p.input_allowlist = true;
    for (const auto& pin : c.pins) {
      if (pin.address_data_input) {
        p.data.insert(pin.name);
      }
      if (pin.clock_input) {
        p.controls.insert(pin.name);
      }
    }
  }
  for (const auto& c : library.icgs) {
    ports[c.name].kind = color::Floorplan_kind::barrier;
  }
  for (const auto& c : library.constants) {
    ports[c.name].kind     = color::Floorplan_kind::barrier;
    ports[c.name].constant = true;
  }
  return ports;
}
void collect_control_roles(hhds::Graph& graph, Resolver& resolve, Prects_library& roles) {
  if (!roles.override_controls) {
    roles.ports.clear();
    roles.pins.clear();
  }
  roles.clock_ports.clear();
  roles.clock_pins.clear();
  roles.reset_ports.clear();
  roles.reset_pins.clear();
  const auto collect = [&](const hhds::Node_class& node) {
    for (auto pin : node.out_sorted_pins()) {
      if (!gu::signal_roles_of(pin)) {
        continue;
      }
      for (int bit = 0; bit < std::max(1, gu::bits_of(pin)); ++bit) {
        const auto endpoint = resolve.bit(pin, bit);
        if (endpoint.constant >= 0) {
          continue;
        }
        const bool port  = endpoint.instance == "PIN";
        const auto label = endpoint.label();
        if (!roles.override_controls) {
          (port ? roles.ports : roles.pins).insert(label);
        }
        if (gu::signal_roles_of(pin) & attrs::signal_clock) {
          (port ? roles.clock_ports : roles.clock_pins).insert(label);
        }
        if (gu::signal_roles_of(pin) & attrs::signal_reset) {
          (port ? roles.reset_ports : roles.reset_pins).insert(label);
        }
      }
    }
  };
  collect(graph.get_input_node());
  for (auto node : graph.body().nodes()) {
    collect(node);
  }
}

struct View {
  Prects_library                                prects;
  Prects_networks                               ignored;
  dp::Design                                    design;
  bool                                          hierarchy_snap         = false;
  bool                                          native_orientations    = false;
  bool                                          align_macros           = false;
  bool                                          global_macro_alignment = false;
  bool                                          centroids              = false;
  bool                                          color_centroids        = false;
  bool                                          placement_colors       = false;
  bool                                          memory_colors          = false;
  double                                        color_maximum          = 30000;
  double                                        color_minimum          = 0;
  std::map<std::string, FloorplanPorts>         fp_ports;
  color::Floorplan_coloring                     fp_coloring;
  size_t                                        hierarchy_centroids = 0;
  std::optional<std::vector<dp::CentroidGroup>> frozen_centroids;
  size_t                                        frozen_hierarchy_centroids = 0;
  std::vector<std::vector<std::string>>         frozen_members;
  std::vector<std::string>                      frozen_color_names;
  std::map<std::string, int>                    frozen_placement_owner;
  double                                        centroid_minimum    = 2000;
  int                                           centroid_min_copies = 4;
  int                                           centroid_min_macros = 2;
  int64_t                                       halo_x = 0, halo_y = 0;
  std::map<Endpoint, Bus_bit>                   bus_names;
  ph::Floorplan                                 fp;
  // Solver rows span a rectangle; exports retain the imported row segments.
  std::shared_ptr<const ph::Floorplan>          original_row_frame;
  std::vector<std::string>                      io_buses;
  std::vector<int>                              io_bits;
  std::vector<Cell>                             cells;
  size_t                                        terminal_begin = 0;  // after cells and placement-only mask geometry
  std::set<std::string>                         constant_masters;
  std::vector<Endpoint>                         pins;
  std::vector<Endpoint>                         drivers;
  std::map<Endpoint, std::vector<Endpoint>>     nets;
  std::map<std::string, size_t>                 terminal;
  std::map<std::string, size_t>                 cell;
  std::map<std::string, double>                 weights;
  std::map<std::string, std::string>            cohesion_owners;
};
// A timing checkpoint borrows connectivity and solver geometry. Ordinary GP
// retains fractional pin positions. Legal checkpoints round positions and apply
// row/macro orientations without copying topology or rebuilding native arrays.
// None of these references escape the synchronous checkpoint.
struct Geometry_view {
  const View&           view;
  const dp::Design&     design;
  bool                  solver_positions;
  const ph::Technology* legal_technology = nullptr;

  Geometry_view(const View& v) : view(v), design(v.design), solver_positions(false) {}
  Geometry_view(const View& v, const dp::Design& physical) : view(v), design(physical), solver_positions(true) {}
  Geometry_view(const View& v, const dp::Design& physical, const ph::Technology& tech)
      : view(v), design(physical), solver_positions(true), legal_technology(&tech) {}

  Ann_place                 pose(size_t index) const;
  std::pair<double, double> pin_offset(size_t pin) const;
  std::pair<double, double> pin_position(size_t pin, std::pair<double, double> offset) const;
};
using CentroidSample = dp::CentroidSample;
auto summarize_centroids(const dp::Design& d) { return dp::detail::centroid_summary(d); }
void restore_imported_rows(ph::Floorplan& frame, const View& v) {
  if (v.original_row_frame) {
    frame.rows      = v.original_row_frame->rows;
    frame.blockages = v.original_row_frame->blockages;
  }
}
ph::Floorplan blockage_frame(const View& v) {
  // v.fp is a frame, with signal components deliberately removed. Bind only
  // the associated fixed components from the authoritative current cell view.
  ph::Floorplan result;
  result.blockages = v.fp.blockages;
  std::set<std::string> bound;
  for (const auto& blockage : result.blockages) {
    if (blockage.component.empty() || !bound.insert(blockage.component).second) {
      continue;
    }
    const auto it = v.cell.find(blockage.component);
    if (it == v.cell.end()) {
      throw std::runtime_error("placement blockage associated component absent from current graph: " + blockage.component);
    }
    const auto& c = v.cells.at(it->second);
    result.components.push_back({c.name, c.macro, c.place});
  }
  return result;
}
std::pair<double, double> offset(const ph::Macro& macro, const std::string& name, Orientation o, int64_t dbu) {
  auto it = macro.pins.find(name);
  if (it == macro.pins.end() || it->second.shapes.empty()) {
    throw std::runtime_error("missing LEF pin geometry: " + name);
  }
  const auto [x, y] = ph::oriented_center(macro, it->second.shapes.front().rect, o);
  return {x * dbu, y * dbu};
}
color::Floorplan_coloring analyze_floorplan_colors(const View& v, const ph::Technology& tech, const std::vector<int>& scopes) {
  using Kind = color::Floorplan_kind;
  std::vector<color::Floorplan_node> nodes(v.design.node_x.size());
  std::vector<FloorplanPorts>        roles(v.cells.size());
  for (size_t i = 0; i < nodes.size(); ++i) {
    nodes[i].kind = Kind::barrier;
  }
  for (size_t i = 0; i < v.cells.size(); ++i) {
    const auto& cell = v.cells[i];
    if (auto found = v.fp_ports.find(cell.macro); found != v.fp_ports.end()) {
      roles[i] = found->second;
    }
    if (gu::type_op_of(cell.node) == Ntype_op::Memory) {
      roles[i].input_allowlist = true;
      for (auto pin : cell.node.inp_sorted_pins()) {
        const auto offset = pin.get_port_id() % Ntype::Memory_port_stride;
        if (offset == 0 || offset == 3 || offset == 12) {  // address, data, whole-array data
          roles[i].data.insert(std::format("in_{}", pin.get_port_id()));
        } else if (offset == 2 || offset == 14) {  // clock and synchronous reset
          roles[i].controls.insert(std::format("in_{}", pin.get_port_id()));
        }
      }
    }
    nodes[i] = {cell.name, v.design.node_macro[i] ? Kind::macro : roles[i].kind, double(cell.place.width) * cell.place.height};
  }
  std::vector<std::pair<int, int>>              edges;
  std::vector<std::vector<std::pair<int, int>>> input_nets(nodes.size());
  int                                           net_id = 0;
  std::vector<std::vector<int>>                 backward(nodes.size()), forward(nodes.size());
  std::vector<int>                              controls;
  const auto                                    pin_base = [](const std::string& pin) { return pin.substr(0, pin.find('[')); };
  const auto                                    index    = [&](const Endpoint& p) {
    return int(p.instance == "PIN" ? v.terminal_begin + v.terminal.at(p.pin) : v.cell.at(p.instance));
  };
  for (const auto& [driver, sinks] : v.nets) {
    if (v.ignored.ignored[net_id++]) {
      continue;
    }
    const int from = index(driver);
    for (const auto& sink : sinks) {
      const int to = index(sink);
      backward[to].push_back(from);
      forward[from].push_back(to);
      if (sink.instance == "PIN") {
        continue;
      }
      const auto& p        = roles[to];
      const auto  base     = pin_base(sink.pin);
      const auto& physical = tech.macros.at(v.cells[to].macro).pins.at(sink.pin);
      if (physical.use == "CLOCK" || p.controls.contains(base) || p.controls.contains(sink.pin)
          || (nodes[to].kind == Kind::barrier && p.kind == Kind::barrier)) {
        controls.push_back(from);
        continue;
      }
      if (!p.input_allowlist || p.data.contains(base) || p.data.contains(sink.pin)) {
        edges.emplace_back(from, to);
        if (nodes[to].kind == Kind::macro) {
          input_nets[to].emplace_back(from, net_id);
        }
      }
    }
  }
  // Constant ties may feed buffers or other combinational gates. Such paths
  // must not become shared-cone evidence merely because a buffer was retained.
  // Stop this propagation at every state element and macro: constant inputs
  // do not establish the value of stored state.
  std::vector<size_t> remaining;
  std::vector<int>    constant;
  for (size_t n = 0; n < nodes.size(); ++n) {
    remaining.push_back(backward[n].size());
    if (n < roles.size() && roles[n].constant) {
      constant.push_back(n);
    }
  }
  while (!constant.empty()) {
    const int n = constant.back();
    constant.pop_back();
    for (int next : forward[n]) {
      if (--remaining[next] == 0 && nodes[next].kind == Kind::logic) {
        nodes[next].kind = Kind::barrier;
        constant.push_back(next);
      }
    }
  }
  std::vector<bool> seen(nodes.size());
  while (!controls.empty()) {
    const int n = controls.back();
    controls.pop_back();
    if (seen[n] || nodes[n].kind != Kind::logic) {
      continue;
    }
    seen[n]       = true;
    nodes[n].kind = Kind::barrier;
    controls.insert(controls.end(), backward[n].begin(), backward[n].end());
  }
  for (size_t n = 0; n < nodes.size(); ++n) {
    for (const auto& [from, id] : input_nets[n]) {
      if (size_t(from) >= v.cells.size() || nodes[from].kind != Kind::barrier) {
        nodes[n].input_nets.push_back(id);
      }
    }
  }
  color::Floorplan_coloring preserved;
  if (v.frozen_centroids && v.placement_colors) {
    preserved.initial_memory_groups = v.fp_coloring.initial_memory_groups;
    preserved.merges_per_round      = v.fp_coloring.merges_per_round;
    preserved.truncated_cones       = v.fp_coloring.truncated_cones;
    preserved.owner.assign(nodes.size(), -1);
    for (const auto& name : v.frozen_color_names) {
      preserved.colors.push_back({name, {}, 0, 0});
    }
    for (size_t i = 0; i < v.cells.size(); ++i) {
      if (auto it = v.frozen_placement_owner.find(v.cells[i].name);
          it != v.frozen_placement_owner.end() && (nodes[i].kind == Kind::logic || nodes[i].kind == Kind::macro)) {
        preserved.owner[i] = it->second;
      }
    }
  }
  return color::floorplan_color(nodes,
                                edges,
                                v.frozen_centroids && v.placement_colors ? &preserved : nullptr,
                                {v.memory_colors, v.color_maximum * dp::detail::centroid_cell_area(v.design), scopes});
}
// Timing and repair callbacks need the same connectivity as the placement
// objective. v.fp contains only the frame until final DEF export; v.nets owns
// the current mapped connectivity, including changes accepted by FP repair.
void snapshot_nets(ph::Floorplan& frame, const View& v) {
  frame.nets.clear();
  frame.nets.reserve(v.nets.size());
  std::set<std::string> names;
  for (auto& terminal : frame.terminals) {
    names.insert(terminal.name);
    terminal.net.clear();
  }
  size_t serial = 0;
  for (const auto& [driver, sinks] : v.nets) {
    auto name = driver.pin;
    if (driver.instance != "PIN") {
      do {
        name = std::format("snapshot_net_{}", serial++);
      } while (!names.insert(name).second);
    }
    ph::Net net{name, {{driver.instance, driver.pin}}};
    for (const auto& sink : sinks) {
      net.connections.push_back({sink.instance, sink.pin});
    }
    for (const auto& endpoint : net.connections) {
      if (endpoint.instance == "PIN") {
        frame.terminals.at(v.terminal.at(endpoint.pin)).net = name;
      }
    }
    frame.nets.push_back(std::move(net));
  }
  for (auto& terminal : frame.terminals) {
    if (terminal.net.empty()) {
      terminal.net = terminal.name;
      frame.nets.push_back({terminal.name, {{"PIN", terminal.name}}});
    }
  }
}
// The view's frame with its current mapped nets and cell poses, as timing
// and repair callbacks receive a placement checkpoint.
void snapshot_frame(ph::Floorplan& frame, const Geometry_view& geometry) {
  const auto& v = geometry.view;
  frame = v.fp;
  restore_imported_rows(frame, v);
  snapshot_nets(frame, v);
  frame.components.clear();
  for (size_t i = 0; i < v.cells.size(); ++i) {
    const auto& c = v.cells[i];
    frame.components.push_back({c.name, c.macro, geometry.pose(i)});
  }
}
void make_arrays(View& v, const ph::Technology& tech) {
  v.design             = {};
  // Pin offsets below are built in each cell's current orientation, and the
  // native set_orientation() composes from node_orientation (empty means N).
  // Record every non-N cell, CORE row flips included, so a later orientation
  // install (sat_install, H2D fixed poses) never flips an offset twice.
  const bool  oriented = v.native_orientations || std::any_of(v.cells.begin(), v.cells.end(), [](const auto& c) {
                          return c.place.orientation != Orientation::N;
                        });
  auto&       d        = v.design;
  const auto& rows     = v.fp.rows;
  // Integer DBU row pitch, as validate_rows and check_legality use.
  const auto  rh       = std::llround(tech.sites.at(rows.front().site).height * tech.dbu);
  d.layout             = {double(rows.front().x),
                          double(rows.front().y),
                          double(rows.front().x + rows.front().count * rows.front().step),
                          double(rows.back().y + rh),
                          double(rows.front().step),
                          double(rh)};
  std::stable_sort(v.cells.begin(), v.cells.end(), [](const auto& a, const auto& b) {
    const auto movable
        = [](const auto& c) { return !c.locked && c.place.status != Status::FIXED && c.place.status != Status::LOCKED; };
    if (movable(a) != movable(b)) {
      return movable(a);
    }
    return a.name < b.name;
  });
  v.cell.clear();
  for (const auto& c : v.cells) {
    if (!v.cell.emplace(c.name, d.node_x.size()).second) {
      throw std::runtime_error("duplicate physical instance name: " + c.name);
    }
    d.node_x.push_back(c.place.x);
    d.node_y.push_back(c.place.y);
    d.node_size_x.push_back(c.place.width);
    d.node_size_y.push_back(c.place.height);
    d.node_legalize_last.push_back(v.constant_masters.contains(c.macro));
    const bool standard = tech.macros.at(c.macro).klass.starts_with("CORE");
    const auto [blocked_h, blocked_v]
        = standard ? std::pair{0., 0.} : lef_track_capacity(tech, tech.macros.at(c.macro).blocked_layers);
    d.node_blocked_capacity_h.push_back(blocked_h);
    d.node_blocked_capacity_v.push_back(blocked_v);
    const auto mask = ph::placement_mask({c.name, c.macro, c.place}, tech);
    d.node_placement_mask.push_back(mask == ph::Placement_mask::none   ? dp::PlacementMask::none
                                    : mask == ph::Placement_mask::soft ? dp::PlacementMask::soft
                                                                       : dp::PlacementMask::hard);
    d.node_macro.push_back(!standard);
    d.node_inflatable.push_back(standard);
    d.node_halo_x.push_back(standard || mask != ph::Placement_mask::none ? 0 : v.halo_x);
    d.node_halo_y.push_back(standard || mask != ph::Placement_mask::none ? 0 : v.halo_y);
    if (oriented) {
      d.node_orientation.push_back(native_orientation(c.place.orientation));
    }
    if (!c.locked && c.place.status != Status::FIXED && c.place.status != Status::LOCKED) {
      ++d.num_movable;
    }
  }
  std::vector<ph::Rect> fixed_occupancy;
  for (size_t i = 0; i < v.cells.size(); ++i) {
    const auto& c = v.cells[i];
    if ((c.place.status == Status::FIXED || c.place.status == Status::LOCKED) && d.placement_mask(i) != dp::PlacementMask::soft) {
      fixed_occupancy.push_back({d.node_x[i] - d.node_halo_x[i],
                                 d.node_y[i] - d.node_halo_y[i],
                                 d.node_x[i] + d.node_size_x[i] + d.node_halo_x[i],
                                 d.node_y[i] + d.node_size_y[i] + d.node_halo_y[i]});
    }
  }
  const ph::Rect domain{d.layout.xl, d.layout.yl, d.layout.xh, d.layout.yh};
  for (const auto& exclusion : ph::placement_exclusions(blockage_frame(v), domain, fixed_occupancy)) {
    const auto& r = exclusion.rect;
    d.node_x.push_back(r.xl);
    d.node_y.push_back(r.yl);
    d.node_size_x.push_back(r.xh - r.xl);
    d.node_size_y.push_back(r.yh - r.yl);
    d.node_legalize_last.push_back(0);
    d.node_blocked_capacity_h.push_back(0);
    d.node_blocked_capacity_v.push_back(0);
    d.node_macro.push_back(1);
    d.node_placement_mask.push_back(exclusion.kind == ph::Placement_mask::soft ? dp::PlacementMask::soft : dp::PlacementMask::hard);
    d.node_inflatable.push_back(0);
    d.node_halo_x.push_back(0);
    d.node_halo_y.push_back(0);
    if (oriented) {
      d.node_orientation.push_back(dp::Orientation::N);
    }
  }
  v.terminal_begin = d.node_x.size();
  for (size_t i = 0; i < v.fp.terminals.size(); ++i) {
    const auto& p      = v.fp.terminals[i];
    v.terminal[p.name] = i;
    d.node_x.push_back(p.x);
    d.node_y.push_back(p.y);
    d.node_size_x.push_back(0);
    d.node_size_y.push_back(0);
    d.node_legalize_last.push_back(0);
    d.node_blocked_capacity_h.push_back(0);
    d.node_blocked_capacity_v.push_back(0);
    d.node_macro.push_back(0);
    d.node_placement_mask.push_back(dp::PlacementMask::none);
    d.node_inflatable.push_back(0);
    d.node_halo_x.push_back(0);
    d.node_halo_y.push_back(0);
    if (oriented) {
      d.node_orientation.push_back(dp::Orientation::N);
    }
  }
  if (v.align_macros) {
    d.node_align_class.assign(d.node_x.size(), -1);
    std::map<std::pair<std::string, std::string>, int> classes;
    for (const auto& c : v.cells) {
      const auto owner = v.cohesion_owners.find(c.name);
      const int  i     = v.cell.at(c.name);
      if (d.node_macro[i] && (v.global_macro_alignment || owner != v.cohesion_owners.end())) {
        const auto [it, inserted]
            = classes.try_emplace({c.macro, v.global_macro_alignment ? std::string{} : owner->second}, classes.size());
        (void)inserted;
        d.node_align_class[i] = it->second;
      }
    }
  }
  if (v.hierarchy_snap) {
    std::vector<std::string>               names(d.node_x.size());
    std::vector<std::pair<double, double>> sizes(d.node_x.size());
    for (size_t i = 0; i < v.cells.size(); ++i) {
      names[i] = v.cells[i].name;
      sizes[i] = {d.node_size_x[i], d.node_size_y[i]};
    }
    d.node_snap_class = hierarchy_macro_classes(names, sizes, d.node_macro, d.num_movable);
  }
  if (!v.cohesion_owners.empty()) {
    std::map<std::string, std::vector<int>> groups;
    for (const auto& c : v.cells) {
      if (auto found = v.cohesion_owners.find(c.name); found != v.cohesion_owners.end()) {
        groups[found->second].push_back(static_cast<int>(v.cell.at(c.name)));
      }
    }
    for (auto& [name, members] : groups) {
      (void)name;
      if (members.size() > 1) {
        d.cohesion_groups.push_back({std::move(members), 1, {}});
      }
    }
  }
  ph::Floorplan connectivity;
  for (const auto& c : v.cells) {
    connectivity.components.push_back({c.name, c.macro, c.place});
    if (gu::type_op_of(c.node) == Ntype_op::Memory) {
      for (auto pin : c.node.inp_sorted_pins()) {
        const auto port = pin.get_port_id() % Ntype::Memory_port_stride;
        if (port == 2 || port == 14) {
          v.prects.controls[c.macro].insert(std::format("in_{}", pin.get_port_id()));
        }
      }
    }
  }
  for (const auto& [driver, sinks] : v.nets) {
    ph::Net net{{}, {{driver.instance, driver.pin}}};
    for (const auto& sink : sinks) {
      net.connections.push_back({sink.instance, sink.pin});
    }
    connectivity.nets.push_back(std::move(net));
  }
  v.ignored = prects_networks(tech, connectivity, v.prects);
  v.pins.clear();
  v.drivers.clear();
  for (const auto& [driver, sinks] : v.nets) {
    v.drivers.push_back(driver);
    auto append = [&](const Endpoint& e) {
      v.pins.push_back(e);
      d.flat_net2pin.push_back(d.pin2node.size());
      if (e.instance == "PIN") {
        d.pin2node.push_back(v.terminal_begin + v.terminal.at(e.pin));
        d.pin_offset_x.push_back(0);
        d.pin_offset_y.push_back(0);
      } else {
        const auto  index = v.cell.at(e.instance);
        const auto& c     = v.cells[index];
        const auto [x, y] = offset(tech.macros.at(c.macro), e.pin, c.place.orientation, tech.dbu);
        d.pin2node.push_back(index);
        d.pin_offset_x.push_back(x);
        d.pin_offset_y.push_back(y);
      }
    };
    append(driver);
    for (const auto& sink : sinks) {
      append(sink);
    }
    d.flat_net2pin_start.push_back(d.pin2node.size());
    const auto key    = driver.label();
    const auto weight = v.weights.contains(key) ? v.weights.at(key) : 1;
    if (!std::isfinite(weight) || weight < 1 || weight > max_net_weight) {
      throw std::invalid_argument("invalid physical net weight");
    }
    d.net_weights.push_back(weight);
    // Pre-CTS nets remain in DEF/STA, but have no placement edges. A reset or
    // rail tree's nets (reset_tree.hpp) keep theirs: the tree bounds each of
    // them to its fanout, and without them its buffers would sit wherever the
    // density pushes them. They stay out of repair and criticality.
    const bool tree = is_tree_buffer(driver.instance)
                      || std::any_of(sinks.begin(), sinks.end(), [](const auto& s) { return is_tree_buffer(s.instance); });
    d.net_mask.push_back((tree || !v.ignored.ignored[v.drivers.size() - 1]) && !sinks.empty());
  }
  d.build_maps();
  if (v.centroids) {
    std::vector<std::string> paths, masters;
    for (const auto& c : v.cells) {
      paths.push_back(original_hierarchy_path(c.name));
      masters.push_back(c.macro);
    }
    paths.resize(d.node_x.size());
    masters.resize(d.node_x.size());
    d.node_centroid_color.assign(d.node_x.size(), -1);
    d.centroid_node_area.resize(d.node_x.size());
    for (size_t i = 0; i < d.node_x.size(); ++i) {
      d.centroid_node_area[i] = d.node_size_x[i] * d.node_size_y[i];
    }
    std::map<std::string, int> colors;
    if (v.frozen_centroids) {
      d.centroid_color_names = v.frozen_color_names;
      for (size_t i = 0; i < d.centroid_color_names.size(); ++i) {
        colors.emplace(d.centroid_color_names[i], i);
      }
    }
    for (size_t i = 0; !v.placement_colors && i < v.cells.size(); ++i) {
      if (auto attr = v.cells[i].node.attr(attrs::synth_color_group); attr.has()) {
        auto key               = std::string(attr.get());
        const auto [it, added] = colors.try_emplace(key, colors.size());
        if (added) {
          d.centroid_color_names.push_back(key);
        }
        d.node_centroid_color[i] = it->second;
      }
    }
    if (v.frozen_centroids) {
      d.centroid_groups = *v.frozen_centroids;
      for (size_t g = 0; g < d.centroid_groups.size(); ++g) {
        auto& nodes = d.centroid_groups[g].nodes;
        nodes.clear();
        for (const auto& name : v.frozen_members.at(g)) {
          if (auto it = v.cell.find(name); it != v.cell.end()) {
            nodes.push_back(it->second);
          }
        }
      }
      v.hierarchy_centroids = v.frozen_hierarchy_centroids;
    } else {
      d.centroid_groups
          = dp::detail::centroid_candidates(d, paths, masters, v.centroid_minimum, v.centroid_min_copies, v.centroid_min_macros);
      v.hierarchy_centroids = d.centroid_groups.size();
      if (v.color_centroids && !v.placement_colors) {
        add_color_centroids(d, v.color_minimum);
      }
    }
    if (v.placement_colors) {
      v.fp_coloring = analyze_floorplan_colors(v, tech, v.frozen_centroids ? std::vector<int>{} : centroid_node_scopes(d, paths));
      d.node_centroid_color = v.fp_coloring.owner;
      d.centroid_color_names.clear();
      for (const auto& c : v.fp_coloring.colors) {
        d.centroid_color_names.push_back(c.name);
      }
      placement_color_centroids(d, paths, v.fp_coloring, v.color_minimum, v.frozen_centroids.has_value());
      v.hierarchy_centroids = std::count_if(d.centroid_groups.begin(), d.centroid_groups.end(), [](const auto& g) {
        return !g.name.starts_with("@placement:");
      });
    }
  }
  if (v.native_orientations) {
    // Net-driver enumeration changes after standard-cell ECOs. A macro-frame
    // fingerprint describes pin geometry, so give that geometry a stable order.
    for (size_t i = 0; i < v.cells.size(); ++i) {
      if (!tech.macros.at(v.cells[i].macro).klass.starts_with("CORE")) {
        std::sort(d.flat_node2pin.begin() + d.flat_node2pin_start[i],
                  d.flat_node2pin.begin() + d.flat_node2pin_start[i + 1],
                  [&](auto a, auto b) {
                    return std::pair{d.pin_offset_x[a], d.pin_offset_y[a]} < std::pair{d.pin_offset_x[b], d.pin_offset_y[b]};
                  });
      }
    }
  }
  d.validate();
}
void append_previous_boundary(std::vector<int>& previous, const View& current, const View& baseline) {
  if (current.terminal_begin - current.cells.size() != baseline.terminal_begin - baseline.cells.size()
      || current.fp.terminals.size() != baseline.fp.terminals.size()) {
    throw std::runtime_error("GP replacement changed fixed placement exclusions or IO boundary");
  }
  for (size_t i = current.cells.size(); i < current.terminal_begin; ++i) {
    const auto  old = baseline.cells.size() + i - current.cells.size();
    const auto& a   = current.design;
    const auto& b   = baseline.design;
    if (a.node_x[i] != b.node_x[old] || a.node_y[i] != b.node_y[old] || a.node_size_x[i] != b.node_size_x[old]
        || a.node_size_y[i] != b.node_size_y[old] || a.placement_mask(i) != b.placement_mask(old)) {
      throw std::runtime_error("GP replacement changed fixed placement exclusion geometry");
    }
    previous.push_back(old);
  }
  for (size_t i = 0; i < current.fp.terminals.size(); ++i) {
    previous.push_back(baseline.terminal_begin + i);
  }
}
// Rebuild a topology trial at the current overlapping GP coordinates. Stable
// names carry all surviving positions; inserted buffers have Steiner seeds.
void refresh_floorplan_view(View& v, const View& baseline, Result& state, const ph::Technology& tech, bool unknown_zero) {
  v = baseline;
  std::map<std::string, Cell> previous;
  for (const auto& cell : baseline.cells) {
    previous.emplace(cell.name, cell);
  }
  v.cells.clear();
  v.nets.clear();
  v.cohesion_owners.clear();
  std::map<uint64_t, std::string> memories;
  for (auto node : state.graph->body().nodes()) {
    const std::string name(node.get_name());
    const auto        old = previous.find(name);
    const auto        io  = node.get_subnode_io();
    if (old == previous.end() && !io) {
      continue;
    }
    const auto master = io ? std::string(io->get_name()) : old->second.macro;
    if (!tech.macros.contains(master)) {
      throw std::runtime_error("FP repair inserted an unmapped cell: " + name);
    }
    const auto& geometry = tech.macros.at(master);
    Cell        cell;
    if (old != previous.end()) {
      cell = old->second;
    } else {
      if (!geometry.klass.starts_with("CORE")) {
        throw std::runtime_error("FP repair inserted a macro");
      }
      const auto seed = node.attr(attrs::place);
      if (!seed.has()) {
        throw std::runtime_error("FP repair buffer has no placement seed");
      }
      cell.place                   = seed.get();
      cell.name                    = name;
      auto                  origin = name;
      std::set<std::string> visited;
      while (!previous.contains(origin) && state.fp_buffer_origins.contains(origin) && visited.insert(origin).second) {
        origin = state.fp_buffer_origins.at(origin);
      }
      if (auto it = v.frozen_placement_owner.find(origin); it != v.frozen_placement_owner.end()) {
        v.frozen_placement_owner[name] = it->second;
      }
      for (auto& members : v.frozen_members) {
        if (std::find(members.begin(), members.end(), origin) != members.end()) {
          members.push_back(name);
        }
      }
      if (auto block = node.attr(attrs::placement_block); block.has()) {
        cell.block = block.get();
      }
    }
    if (old != previous.end() && !tech.macros.at(old->second.macro).klass.starts_with("CORE") && master != old->second.macro) {
      throw std::runtime_error("FP repair changed a macro master");
    }
    cell.node         = node;
    cell.macro        = master;
    cell.place.width  = std::llround(geometry.width * tech.dbu);
    cell.place.height = std::llround(geometry.height * tech.dbu);
    if (cell.place.width > v.design.layout.xh - v.design.layout.xl || cell.place.height > v.design.layout.yh - v.design.layout.yl) {
      throw std::runtime_error("FP repair cell does not fit the current core: " + name);
    }
    if (cell.place.status != Status::FIXED && cell.place.status != Status::LOCKED && !cell.locked) {
      cell.place.x = std::clamp(cell.place.x, int64_t(v.design.layout.xl), int64_t(v.design.layout.xh) - cell.place.width);
      cell.place.y = std::clamp(cell.place.y, int64_t(v.design.layout.yl), int64_t(v.design.layout.yh) - cell.place.height);
    }
    if (!io) {
      memories.emplace(node.get_debug_nid(), master);
    }
    if (auto color = node.attr(attrs::synth_color_group); color.has()) {
      v.cohesion_owners.emplace(name, color.get());
    }
    v.cells.push_back(std::move(cell));
  }
  Resolver resolve(tech, memories, unknown_zero);
  collect_control_roles(*state.graph, resolve, v.prects);
  state.prects        = v.prects;
  const auto endpoint = [&](Pin p, int bit) {
    auto e = resolve.bit(p, bit);
    if (e.constant >= 0) {
      throw std::runtime_error("FP repair introduced an unmaterialized constant driver");
    }
    return e;
  };
  for (const auto& decl : state.graph->get_io()->get_output_pin_decls()) {
    const auto drivers = state.graph->get_output_pin(decl.name).get_driver_pins();
    if (drivers.size() != 1) {
      throw std::runtime_error("FP repair changed top output drivers");
    }
    for (int b = 0; b < std::max(1, int(decl.bits)); ++b) {
      v.nets[endpoint(drivers.front(), b)].push_back({"PIN", bit_name(decl.name, std::max(1, int(decl.bits)), b), -1});
    }
  }
  for (const auto& cell : v.cells) {
    const bool memory = memories.contains(cell.node.get_debug_nid());
    for (auto sink : cell.node.inp_sorted_pins()) {
      if (memory && !memory_runtime_pin(sink.get_port_id())) {
        continue;
      }
      const auto drivers = sink.get_driver_pins();
      if (drivers.size() != 1) {
        throw std::runtime_error("FP repair input needs one driver: " + cell.name);
      }
      if (memory) {
        for (int b = 0; b < std::max(1, gu::bits_of(drivers.front())); ++b) {
          v.nets[endpoint(drivers.front(), b)].push_back({cell.name, memory_pin(sink.get_port_id(), b, false), -1});
        }
      } else {
        const auto  pin  = pin_name(sink);
        const auto& pins = tech.macros.at(cell.macro).pins;
        if (pins.contains(pin)) {
          v.nets[endpoint(drivers.front(), 0)].push_back({cell.name, pin, -1});
        } else {
          unsigned count = 0;
          for (int b = 0; pins.contains(std::format("{}[{}]", pin, b)); ++b, ++count) {
            v.nets[endpoint(drivers.front(), b)].push_back({cell.name, std::format("{}[{}]", pin, b), -1});
          }
          if (!count) {
            throw std::runtime_error("FP repair input absent from LEF: " + cell.name + ":" + pin);
          }
        }
      }
    }
  }
  make_arrays(v, tech);
  snapshot_frame(state.floorplan, v);
  for (const auto& c : v.cells) {
    c.node.attr(attrs::place).set(c.place);
  }
}
// Routing-track correction belongs between macro legalization and row packing.
// Movable CORE coordinates are not obstacles until the native cell legalizer
// packs them; explicit fixed cells and all macro hard halos remain obstacles.
bool align_macro_geometry(const View& v, const ph::Technology& tech, dp::Design& physical, bool move, bool freeze_all) {
  if (std::none_of(physical.node_macro.begin(), physical.node_macro.begin() + v.cells.size(), [](auto macro) { return macro; })) {
    return false;
  }
  std::vector<ph::Component> frame;
  std::vector<size_t>        indexes;
  std::vector<uint64_t>      rigid_ids;
  for (size_t i = 0; i < v.cells.size(); ++i) {
    if (physical.placement_mask(i) == dp::PlacementMask::soft) {
      continue;
    }
    const bool macro = physical.node_macro[i];
    if (!macro && i < static_cast<size_t>(physical.num_movable)) {
      continue;
    }
    const auto& c     = v.cells[i];
    auto        place = c.place;
    place.x           = std::llround(physical.node_x[i]);
    place.y           = std::llround(physical.node_y[i]);
    place.width       = std::llround(physical.node_size_x[i]);
    place.height      = std::llround(physical.node_size_y[i]);
    if (!physical.node_orientation.empty()) {
      place.orientation = physical_orientation(physical.node_orientation[i]);
    }
    place.status = i >= static_cast<size_t>(physical.num_movable) || (macro && freeze_all) ? Status::FIXED : Status::PLACED;
    frame.push_back({c.name, c.macro, place});
    indexes.push_back(i);
    rigid_ids.push_back(physical.node_group_id.empty() ? 0 : physical.node_group_id[i]);
  }
  if (std::any_of(rigid_ids.begin(), rigid_ids.end(), [](auto id) { return id != 0; })) {
    legalize_rigid_macro_tracks(tech, v.fp, frame, rigid_ids, v.halo_x, v.halo_y, move);
  } else {
    legalize_macro_tracks(tech, v.fp, frame, v.halo_x, v.halo_y, move);
  }
  bool changed = false;
  if (move) {
    for (size_t j = 0; j < frame.size(); ++j) {
      const auto i = indexes[j];
      if (!physical.node_macro[i]) {
        continue;
      }
      changed            |= physical.node_x[i] != frame[j].place.x || physical.node_y[i] != frame[j].place.y;
      physical.node_x[i]  = frame[j].place.x;
      physical.node_y[i]  = frame[j].place.y;
    }
  }
  return changed;
}
bool macro_tracks_valid(const View& v, const ph::Technology& tech, dp::Design& physical) {
  try {
    align_macro_geometry(v, tech, physical, false, true);
    return true;
  } catch (const std::runtime_error&) {
    return false;
  }
}
// Row flips apply only to CORE cells; BLOCK macros keep their supplied
// orientation. Shared by capture() and restore_cache() so cold and warm runs
// agree. Returns false when an FS row needs a master without SYMMETRY X.
bool apply_row_orientation(Ann_place& place, const ph::Macro& master, Orientation row) {
  if (!master.klass.starts_with("CORE")) {
    return true;
  }
  if (row == Orientation::FS && !master.symmetry_x) {
    return false;
  }
  place.orientation = row;
  return true;
}
bool apply_row_orientation(Cell& c, const ph::Technology& tech, Orientation row) {
  return apply_row_orientation(c.place, tech.macros.at(c.macro), row);
}
Ann_place Geometry_view::pose(size_t index) const {
  const auto& cell   = view.cells[index];
  auto        result = cell.place;
  if (solver_positions) {
    result.x = std::llround(design.node_x[index]);
    result.y = std::llround(design.node_y[index]);
  }
  // The old legal snapshot installed rounded solver positions before capture's
  // fixed-cell check. Fixed/locked orientations and status remain authoritative.
  if (!legal_technology || cell.locked || result.status == Status::FIXED || result.status == Status::LOCKED) {
    return result;
  }
  const auto& master = legal_technology->macros.at(cell.macro);
  if (master.klass.starts_with("CORE")) {
    const auto row = static_cast<size_t>(std::llround((result.y - view.fp.rows.front().y) / design.layout.row_height));
    if (row >= view.fp.rows.size()) {
      throw std::runtime_error("placed cell outside rows");
    }
    if (!apply_row_orientation(result, master, view.fp.rows[row].orientation)) {
      throw std::runtime_error("cell cannot use flipped row: " + cell.name);
    }
  } else if (!design.node_orientation.empty()) {
    result.orientation = physical_orientation(design.node_orientation[index]);
  }
  result.status = Status::PLACED;
  return result;
}
std::pair<double, double> Geometry_view::pin_offset(size_t pin) const {
  if (!legal_technology) {
    return {design.pin_offset_x[pin], design.pin_offset_y[pin]};
  }
  const auto node = design.pin2node[pin];
  if (view.pins[pin].instance == "PIN") {
    return {0, 0};
  }
  return offset(legal_technology->macros.at(view.cells[node].macro),
                view.pins[pin].pin,
                pose(node).orientation,
                legal_technology->dbu);
}
std::pair<double, double> Geometry_view::pin_position(size_t pin, std::pair<double, double> pin_offset) const {
  const auto node = design.pin2node[pin];
  if (!legal_technology) {
    return {design.node_x[node] + pin_offset.first, design.node_y[node] + pin_offset.second};
  }
  if (view.pins[pin].instance == "PIN") {
    // capture/make_arrays rebuilt terminals from the frame, not solver nodes.
    const auto& terminal = view.fp.terminals.at(view.terminal.at(view.pins[pin].pin));
    return {terminal.x, terminal.y};
  }
  return {std::llround(design.node_x[node]) + pin_offset.first, std::llround(design.node_y[node]) + pin_offset.second};
}
void capture(View& v, const ph::Technology& tech, bool full) {
  for (size_t i = 0; i < v.cells.size(); ++i) {
    auto& c = v.cells[i];
    if (c.locked || c.place.status == Status::FIXED || c.place.status == Status::LOCKED) {
      continue;
    }
    c.place.x = std::llround(v.design.node_x[i]);
    c.place.y = std::llround(v.design.node_y[i]);
    if (!tech.macros.at(c.macro).klass.starts_with("CORE") && !v.design.node_orientation.empty()) {
      c.place.orientation = physical_orientation(v.design.node_orientation[i]);
    }
    if (full && tech.macros.at(c.macro).klass.starts_with("CORE")) {
      const auto row = static_cast<size_t>(std::llround((c.place.y - v.fp.rows.front().y) / v.design.layout.row_height));
      if (row >= v.fp.rows.size()) {
        throw std::runtime_error("placed cell outside rows");
      }
      if (!apply_row_orientation(c, tech, v.fp.rows[row].orientation)) {
        throw std::runtime_error("cell cannot use flipped row: " + c.name);
      }
    }
    c.place.status = Status::PLACED;
  }
  // Recompute offsets after changing rows/orientations and round GP coordinates
  // consistently with the graph that STA and subsequent exports will read.
  make_arrays(v, tech);
}
// Settle constant sources after their functional users have finished moving.
// Search free row intervals, using actual oriented pin locations. Functional
// cells and fixed objects keep their final sites. The transaction falls back to
// the already legal frame if fragmented free space cannot fit every tie.
void localize_tie_sites(View& v, const ph::Technology& tech, const liberty::Cell_library& library, bool preserve_imported_ties) {
  std::set<std::string> masters;
  for (const auto& model : library.constants) {
    masters.insert(model.name);
  }
  // A shared imported source has no unique user to follow. Leave its GP/LG
  // position alone, including any other output of the same physical cell.
  std::set<std::string> shared_sources;
  if (preserve_imported_ties) {
    for (const auto& [driver, sinks] : v.nets) {
      if (driver.instance != "PIN" && masters.contains(v.cells[v.cell.at(driver.instance)].macro) && sinks.size() > 1) {
        shared_sources.insert(driver.instance);
      }
    }
  }
  std::map<size_t, std::vector<Endpoint>>    targets;
  std::map<size_t, std::vector<std::string>> outputs;
  for (const auto& [driver, sinks] : v.nets) {
    const auto label = driver.label();
    if (v.ignored.pins.contains(label)) {
      continue;
    }
    if (driver.instance != "PIN" && masters.contains(v.cells[v.cell.at(driver.instance)].macro)) {
      if (shared_sources.contains(driver.instance)) {
        continue;
      }
      if (sinks.size() != 1) {
        throw std::runtime_error("constant output must have exactly one physical load: " + driver.instance);
      }
      const auto  index = v.cell.at(driver.instance);
      const auto& cell  = v.cells[index];
      if (!cell.locked && cell.place.status != Status::FIXED && cell.place.status != Status::LOCKED) {
        targets[index].push_back(sinks.front());
        outputs[index].push_back(driver.pin);
      }
    }
  }
  if (targets.empty()) {
    return;
  }
  using Interval = std::pair<int64_t, int64_t>;
  std::vector<std::vector<Interval>> occupied(v.fp.rows.size());
  const auto                         rh   = int64_t(std::llround(v.design.layout.row_height));
  const auto                         sw   = int64_t(std::llround(v.design.layout.site_width));
  const auto                         mark = [&](const Cell& c) {
    const bool macro  = !tech.macros.at(c.macro).klass.starts_with("CORE");
    const auto halo_x = macro ? v.halo_x : 0, halo_y = macro ? v.halo_y : 0;
    const auto bottom = c.place.y - halo_y;
    const auto top    = c.place.y + c.place.height + halo_y;
    auto       row
        = std::lower_bound(v.fp.rows.begin(), v.fp.rows.end(), bottom, [&](const auto& r, int64_t y) { return r.y + rh <= y; });
    for (; row != v.fp.rows.end() && row->y < top; ++row) {
      occupied[row - v.fp.rows.begin()].emplace_back(c.place.x - halo_x, c.place.x + c.place.width + halo_x);
    }
  };
  std::map<size_t, Ann_place> original;
  for (size_t i = 0; i < v.cells.size(); ++i) {
    if (targets.contains(i)) {
      original.emplace(i, v.cells[i].place);
    } else {
      mark(v.cells[i]);
    }
  }
  const ph::Rect domain{v.design.layout.xl, v.design.layout.yl, v.design.layout.xh, v.design.layout.yh};
  for (const auto& exclusion : ph::placement_exclusions(blockage_frame(v), domain)) {
    if (exclusion.kind != ph::Placement_mask::hard) {
      continue;
    }
    const auto& r = exclusion.rect;
    auto        row
        = std::lower_bound(v.fp.rows.begin(), v.fp.rows.end(), r.yl, [&](const auto& item, double y) { return item.y + rh <= y; });
    for (; row != v.fp.rows.end() && row->y < r.yh; ++row) {
      occupied[row - v.fp.rows.begin()].emplace_back(std::llround(r.xl), std::llround(r.xh));
    }
  }
  for (auto& row : occupied) {
    std::sort(row.begin(), row.end());
  }
  for (const auto& [index, sinks] : targets) {
    auto&  cell = v.cells[index];
    double x = 0, y = 0;
    for (const auto& sink : sinks) {
      if (sink.instance == "PIN") {
        const auto& terminal  = v.fp.terminals[v.terminal.at(sink.pin)];
        x                    += terminal.x;
        y                    += terminal.y;
      } else {
        const auto& user     = v.cells[v.cell.at(sink.instance)];
        const auto [dx, dy]  = offset(tech.macros.at(user.macro), sink.pin, user.place.orientation, tech.dbu);
        x                   += user.place.x + dx;
        y                   += user.place.y + dy;
      }
    }
    x                 /= sinks.size();
    y                 /= sinks.size();
    double     best    = INFINITY;
    auto       chosen  = cell.place;
    const auto cut
        = std::lower_bound(v.fp.rows.begin(), v.fp.rows.end(), y, [](const auto& row, double target) { return row.y < target; });
    size_t                row_right = cut - v.fp.rows.begin();
    std::optional<size_t> row_left;
    if (row_right) {
      row_left = row_right - 1;
    }
    while (row_left || row_right < v.fp.rows.size()) {
      size_t row;
      if (row_left
          && (row_right == v.fp.rows.size() || std::abs(v.fp.rows[*row_left].y - y) <= std::abs(v.fp.rows[row_right].y - y))) {
        row = *row_left;
        if (*row_left) {
          --*row_left;
        } else {
          row_left.reset();
        }
      } else {
        row = row_right++;
      }
      const auto& r = v.fp.rows[row];
      // Rows are visited by increasing distance, so no farther row can improve
      // the current pin-distance bound once this conservative limit is crossed.
      if (std::abs(r.y - y) > best + cell.place.height) {
        break;
      }
      if (r.y + cell.place.height > v.design.layout.yh
          || (r.orientation == Orientation::FS && !tech.macros.at(cell.macro).symmetry_x)) {
        continue;
      }
      double dx = 0, dy = 0;
      for (const auto& pin : outputs.at(index)) {
        const auto [px, py]  = offset(tech.macros.at(cell.macro), pin, r.orientation, tech.dbu);
        dx                  += px;
        dy                  += py;
      }
      dx /= outputs.at(index).size();
      dy /= outputs.at(index).size();
      std::vector<Interval> blocked;
      for (size_t j = row; j < occupied.size() && v.fp.rows[j].y < r.y + cell.place.height; ++j) {
        blocked.insert(blocked.end(), occupied[j].begin(), occupied[j].end());
      }
      std::sort(blocked.begin(), blocked.end());
      blocked.emplace_back(r.x + r.count * r.step, r.x + r.count * r.step);
      int64_t left = r.x;
      for (const auto& [lo, hi] : blocked) {
        const auto right      = std::min(lo, r.x + r.count * r.step) - cell.place.width;
        const auto first_site = r.x + ((left - r.x + sw - 1) / sw) * sw;
        const auto last_site  = r.x + int64_t(std::floor(double(right - r.x) / sw)) * sw;
        if (last_site >= first_site) {
          const auto   candidate = std::clamp(r.x + int64_t(std::llround((x - dx - r.x) / sw)) * sw, first_site, last_site);
          const double cost      = std::abs(candidate + dx - x) + std::abs(r.y + dy - y);
          if (cost < best) {
            best               = cost;
            chosen.x           = candidate;
            chosen.y           = r.y;
            chosen.orientation = r.orientation;
          }
        }
        left = std::max(left, hi);
      }
    }
    if (!std::isfinite(best)) {
      for (const auto& [i, place] : original) {
        v.cells[i].place = place;
      }
      return;
    }
    cell.place = chosen;
    mark(cell);
  }
  // When nearby whitespace is exhausted, permit one bounded equal-footprint
  // exchange with a movable functional neighbor. This keeps the complete row
  // packing legal and limits collateral motion to four rows. Two passes revisit
  // tie users moved by an earlier exchange; explicit constraints never relax.
  const double movement_limit = 4 * rh;
  using Footprint             = std::pair<int64_t, int64_t>;
  using Row_cells             = std::map<int64_t, std::multimap<int64_t, size_t>>;
  std::map<Footprint, Row_cells> nearby;
  for (size_t i = 0; i < v.cells.size(); ++i) {
    const auto& c = v.cells[i];
    if (!targets.contains(i) && !masters.contains(c.macro) && !c.locked && c.place.status != Status::FIXED
        && c.place.status != Status::LOCKED && tech.macros.at(c.macro).klass.starts_with("CORE")) {
      nearby[{c.place.width, c.place.height}][c.place.y].emplace(c.place.x, i);
    }
  }
  for (int round = 0; round < 2; ++round) {
    bool moved = false;
    for (const auto& [index, sinks] : targets) {
      auto&      tie               = v.cells[index];
      const auto distance_to_users = [&](const Ann_place& place) {
        double cost = 0;
        for (size_t i = 0; i < sinks.size(); ++i) {
          const auto& sink = sinks[i];
          double      x, y;
          if (sink.instance == "PIN") {
            const auto& terminal = v.fp.terminals[v.terminal.at(sink.pin)];
            x                    = terminal.x;
            y                    = terminal.y;
          } else {
            const auto& user    = v.cells[v.cell.at(sink.instance)];
            const auto [dx, dy] = offset(tech.macros.at(user.macro), sink.pin, user.place.orientation, tech.dbu);
            x                   = user.place.x + dx;
            y                   = user.place.y + dy;
          }
          const auto [dx, dy]  = offset(tech.macros.at(tie.macro), outputs.at(index)[i], place.orientation, tech.dbu);
          cost                += std::abs(place.x + dx - x) + std::abs(place.y + dy - y);
        }
        return cost;
      };
      const auto original_cost = distance_to_users(tie.place);
      if (original_cost <= movement_limit) {
        continue;
      }
      size_t              best      = v.cells.size();
      double              best_cost = original_cost;
      std::vector<size_t> candidates;
      const auto          footprint = nearby.find({tie.place.width, tie.place.height});
      if (footprint == nearby.end()) {
        continue;
      }
      auto& rows = footprint->second;
      for (auto row = rows.lower_bound(tie.place.y - int64_t(movement_limit));
           row != rows.end() && row->first <= tie.place.y + movement_limit;
           ++row) {
        const auto dx = int64_t(movement_limit) - std::abs(row->first - tie.place.y);
        for (auto cell = row->second.lower_bound(tie.place.x - dx); cell != row->second.end() && cell->first <= tie.place.x + dx;
             ++cell) {
          candidates.push_back(cell->second);
        }
      }
      // Keep the previous deterministic candidate tie break, independently of
      // the spatial index's row/site traversal order.
      std::sort(candidates.begin(), candidates.end());
      for (size_t i : candidates) {
        const auto& candidate = v.cells[i];
        if (targets.contains(i) || masters.contains(candidate.macro) || candidate.locked || candidate.place.status == Status::FIXED
            || candidate.place.status == Status::LOCKED || !tech.macros.at(candidate.macro).klass.starts_with("CORE")
            || candidate.place.width != tie.place.width || candidate.place.height != tie.place.height
            || std::abs(candidate.place.x - tie.place.x) + std::abs(candidate.place.y - tie.place.y) > movement_limit
            || (candidate.place.orientation == Orientation::FS && !tech.macros.at(tie.macro).symmetry_x)
            || (tie.place.orientation == Orientation::FS && !tech.macros.at(candidate.macro).symmetry_x)
            || std::any_of(sinks.begin(), sinks.end(), [&](const Endpoint& sink) { return sink.instance == candidate.name; })) {
          continue;
        }
        auto destination        = candidate.place;
        destination.orientation = v.fp.rows.at((destination.y - v.fp.rows.front().y) / rh).orientation;
        const auto cost         = distance_to_users(destination);
        if (cost + sw < best_cost) {
          best      = i;
          best_cost = cost;
        }
      }
      if (best < v.cells.size()) {
        auto& other              = v.cells[best];
        auto& old_row            = rows.at(other.place.y);
        const auto [first, last] = old_row.equal_range(other.place.x);
        for (auto cell = first; cell != last; ++cell) {
          if (cell->second == best) {
            old_row.erase(cell);
            break;
          }
        }
        std::swap(tie.place.x, other.place.x);
        std::swap(tie.place.y, other.place.y);
        tie.place.orientation   = v.fp.rows.at((tie.place.y - v.fp.rows.front().y) / rh).orientation;
        other.place.orientation = v.fp.rows.at((other.place.y - v.fp.rows.front().y) / rh).orientation;
        rows[other.place.y].emplace(other.place.x, best);
        moved = true;
      }
    }
    if (!moved) {
      break;
    }
  }
  make_arrays(v, tech);
}
void validate_rows(const ph::Technology& tech, ph::Floorplan& fp) {
  if (fp.dbu != tech.dbu || fp.rows.empty()) {
    throw std::runtime_error("DEF needs rows and the same DBU as LEF");
  }
  std::sort(fp.rows.begin(), fp.rows.end(), [](const auto& a, const auto& b) { return a.y < b.y; });
  const auto& first = fp.rows.front();
  const auto  it    = tech.sites.find(first.site);
  if (it == tech.sites.end()) {
    throw std::runtime_error("DEF row SITE missing from LEF: " + first.site);
  }
  const auto sw = std::llround(it->second.width * tech.dbu), rh = std::llround(it->second.height * tech.dbu);
  for (size_t i = 0; i < fp.rows.size(); ++i) {
    const auto& r = fp.rows[i];
    if (r.site != first.site || r.count != first.count || r.x != first.x || r.step != sw
        || r.y != first.y + static_cast<int64_t>(i) * rh || (r.orientation != Orientation::N && r.orientation != Orientation::FS)) {
      throw std::runtime_error("floorplan requires contiguous equal-width N/FS rows; row gaps and rotated rows are unsupported");
    }
    if (r.x < fp.die.xl || r.y < fp.die.yl || r.x + r.count * r.step > fp.die.xh || r.y + rh > fp.die.yh) {
      throw std::runtime_error("DEF rows outside DIEAREA");
    }
  }
}
std::shared_ptr<const ph::Floorplan> prepare_rows(const ph::Technology& tech, ph::Floorplan& fp) {
  auto rows = fp.rows;
  std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.y < b.y; });
  bool rectangular = !rows.empty() && tech.sites.contains(rows.front().site);
  if (rectangular) {
    const auto& first = rows.front();
    const auto  rh    = std::llround(tech.sites.at(first.site).height * tech.dbu);
    for (size_t i = 0; i < rows.size(); ++i) {
      const auto& row  = rows[i];
      rectangular     &= row.x == first.x && row.count == first.count && row.y == first.y + static_cast<int64_t>(i) * rh;
    }
  }
  if (rectangular) {
    validate_rows(tech, fp);
    return {};
  }
  // The shared adapter validates the lattice and turns absent sites into hard
  // exclusions. Keep these masks private: original rows encode them in DEF.
  ph::Floorplan solver;
  try {
    solver = ph::rectangular_placement_frame(fp, tech);
  } catch (const std::invalid_argument& error) {
    throw std::runtime_error(error.what());  // Preserve the placer import error contract.
  }
  auto original       = std::make_shared<ph::Floorplan>();
  original->rows      = std::move(fp.rows);
  original->blockages = std::move(fp.blockages);
  fp.rows             = std::move(solver.rows);
  fp.blockages        = std::move(solver.blockages);
  validate_rows(tech, fp);
  return original;
}
// Rectangular Hungarian assignment; cost is Manhattan distance to each net's
// current cell-pin centroid. Already fixed terminals reserve their slots. Slots
// lie on the DIEAREA boundary, which an imported floorplan may set outside the core.
void assign_io(View& v, const ph::Technology& tech) {
  const auto&       l              = v.design.layout;
  const double      pitch          = std::min(l.site_width, l.row_height);
  // Imported positions are frozen by the caller. Automatic left/right pins
  // use the first horizontal layer; top/bottom pins use its next vertical
  // routing layer so an inward access stem follows the preferred direction.
  auto              horizontal     = std::find_if(tech.routing_layers.begin(), tech.routing_layers.end(), [&](const auto& name) {
    return tech.layers.at(name).direction == "HORIZONTAL";
  });
  const std::string layer          = horizontal == tech.routing_layers.end() ? "" : *horizontal;
  auto              vertical       = horizontal == tech.routing_layers.end()
                                         ? tech.routing_layers.end()
                                         : std::find_if(std::next(horizontal), tech.routing_layers.end(), [&](const auto& name) {
                          return tech.layers.at(name).direction == "VERTICAL";
                                           });
  const std::string vertical_layer = vertical == tech.routing_layers.end() ? layer : *vertical;
  auto              slots          = io_perimeter_slots(tech, v.fp, layer, pitch, vertical_layer);
  std::erase_if(slots, [&](const auto& s) {
    for (const auto& t : v.fp.terminals) {
      if (t.status == Status::FIXED && std::abs(t.x - s.first) + std::abs(t.y - s.second) < pitch / 2) {
        return true;
      }
    }
    return false;
  });
  std::vector<size_t> movable;
  for (size_t i = 0; i < v.fp.terminals.size(); ++i) {
    if (v.fp.terminals[i].status != Status::FIXED) {
      movable.push_back(i);
    }
  }
  if (movable.empty()) {
    return;
  }
  if (slots.size() < movable.size()) {
    throw std::runtime_error("not enough die-edge slots for IO pins; enlarge the floorplan");
  }
  std::vector<std::pair<double, double>>       centers(v.fp.terminals.size(), {(l.xl + l.xh) / 2, (l.yl + l.yh) / 2});
  // One pass over the nets: accumulate every touching net's cell pins per terminal.
  std::vector<std::tuple<double, double, int>> sums(v.fp.terminals.size());
  for (size_t net = 0; net < v.drivers.size(); ++net) {
    if (!v.design.net_mask[net]) {
      continue;
    }
    const int begin = v.design.flat_net2pin_start[net], end = v.design.flat_net2pin_start[net + 1];
    double    x = 0, y = 0;
    int       count = 0;
    for (int p = begin; p < end; ++p) {
      const int node = v.design.pin2node[p];
      if (node < static_cast<int>(v.cells.size())) {
        x += v.design.node_x[node] + v.design.pin_offset_x[p];
        y += v.design.node_y[node] + v.design.pin_offset_y[p];
        ++count;
      }
    }
    for (int p = begin; p < end; ++p) {
      const int node = v.design.pin2node[p];
      if (node >= static_cast<int>(v.terminal_begin)) {
        auto& [sx, sy, sc]  = sums[node - v.terminal_begin];
        sx                 += x;
        sy                 += y;
        sc                 += count;
      }
    }
  }
  for (const auto terminal : movable) {
    if (const auto& [x, y, count] = sums[terminal]; count) {
      centers[terminal] = {x / count, y / count};
    }
  }
  const size_t        n = movable.size(), m = slots.size();
  std::vector<double> u(n + 1), w(m + 1);
  std::vector<size_t> p(m + 1), way(m + 1);
  for (size_t i = 1; i <= n; ++i) {
    p[0]                   = i;
    size_t              j0 = 0;
    std::vector<double> minv(m + 1, std::numeric_limits<double>::infinity());
    std::vector<bool>   used(m + 1);
    do {
      used[j0]  = true;
      size_t i0 = p[j0], j1 = 0;
      double delta = std::numeric_limits<double>::infinity();
      for (size_t j = 1; j <= m; ++j) {
        if (!used[j]) {
          auto [cx, cy] = centers[movable[i0 - 1]];
          double cur    = std::abs(cx - slots[j - 1].first) + std::abs(cy - slots[j - 1].second) - u[i0] - w[j];
          if (cur < minv[j]) {
            minv[j] = cur;
            way[j]  = j0;
          }
          if (minv[j] < delta) {
            delta = minv[j];
            j1    = j;
          }
        }
      }
      for (size_t j = 0; j <= m; ++j) {
        if (used[j]) {
          u[p[j]] += delta;
          w[j]    -= delta;
        } else {
          minv[j] -= delta;
        }
      }
      j0 = j1;
    } while (p[j0]);
    do {
      size_t j1 = way[j0];
      p[j0]     = p[j1];
      j0        = j1;
    } while (j0);
  }
  for (size_t j = 1; j <= m; ++j) {
    if (p[j]) {
      auto& t                    = v.fp.terminals[movable[p[j] - 1]];
      t.x                        = std::llround(slots[j - 1].first);
      t.y                        = std::llround(slots[j - 1].second);
      const auto& selected_layer = t.y == v.fp.die.yl || t.y == v.fp.die.yh ? vertical_layer : layer;
      if (!selected_layer.empty()) {
        const double half = std::max(1.0, std::round(tech.layers.at(selected_layer).width * tech.dbu / 2));
        t.shapes          = {
            {selected_layer, {-half, -half, half, half}}
        };
      }
      t.status = Status::PLACED;
    }
  }
}
Uroute_options resolve_uroute_options(Uroute_options resolved_uroute) {
  if (resolved_uroute.dir == "@pretrained") {
    const fs::path relative("pass/place/uroute/pretrained/manifest.json");
    if (fs::is_regular_file(relative)) {
      resolved_uroute.dir = fs::absolute(relative).parent_path().string();
    } else {
      try {
        const auto manifest = runfiles()->Rlocation("livehd/pass/place/uroute/pretrained/manifest.json");
        if (fs::is_regular_file(manifest)) {
          resolved_uroute.dir = fs::path(manifest).parent_path().string();
        }
      } catch (const std::runtime_error&) {
        // An unavailable bundled directory is a data-admission skip. Plain
        // RUDY placement and feature export do not require model runfiles.
      }
    }
  }
  if (resolved_uroute.dir.empty()) {
    resolved_uroute.export_features = false;
  }
  return resolved_uroute;
}
dp::RoutePolicy route_policy(const Options& options, const Stage_route& stage) {
  auto       policy = stage.policy;
  const auto model  = stage.model == "inherit" ? options.route_model : stage.model;
  policy.model      = dp::route_model_from_name(model);
  // Learned feedback is a final-cell rung. Earlier stages can opt into the
  // same model through their bounded convergence probe.
  if (model == "uroute") {
    policy.model = &stage == &options.final_route ? dp::RouteModel::uroute : dp::RouteModel::rsmt;
  }
  policy.uroute_ladder = &stage == &options.final_route;
  policy.degree        = options.route_degree;
  policy.accuracy      = options.route_accuracy;
  dp::validate_route_policy(policy);
  return policy;
}
}  // namespace
DreamPlace::native::RoutePolicy resolve_route_policy(const Options& options, const Stage_route& stage) {
  return route_policy(options, stage);
}
// True when any stage can evaluate the learned model (rung, ladder or probe).
static bool uroute_requested_by(const Options& options) {
  for (const auto* stage : {&options.fp_route, &options.macro_route, &options.final_route}) {
    if (dp::uroute_requested(route_policy(options, *stage))) {
      return true;
    }
  }
  return false;
}
// The preloaded snapshot when it was built from these resolved controls, else a
// fresh load of the manifest and weights.
static Uroute_pool uroute_pool_for(const Options& options) {
  auto resolved = resolve_uroute_options(options.uroute);
  if (options.uroute_pool && options.uroute_pool->options() == resolved) {
    return *options.uroute_pool;  // shares the immutable snapshot
  }
  return Uroute_pool(std::move(resolved));
}
std::optional<Uroute_pool> load_uroute_pool(const Options& options) {
  if (!uroute_requested_by(options)) {
    return std::nullopt;
  }
  return uroute_pool_for(options);
}
// place_single() consults the pool only for a fresh routability solve.
static bool uroute_pool_used(const Options& options) {
  return (options.routability || (options.macro_mode == "h2d" && options.macro_parameters.routability)) && !options.timing_seed
         && !options.retain_placement;
}
std::string uroute_cache_identity(const Options& options) {
  // Whole-flow replay can bypass place() entirely. Validate and fingerprint
  // the same pool snapshot before looking up that outer cache too.
  const auto pool = load_uroute_pool(options);
  if (!pool) {
    return {};
  }
  // An unchanged manifest can become unusable when a sample disappears or
  // fails its digest. Replay must retain the current fallback diagnostic.
  return uroute_digest(pool->cache_identity() + ":" + pool->skipped());
}
void       initialize_route_tables() { initialize_flute_tables(); }
// Placement RC uses driver-aware Steiner trees, with half each wire's
// capacitance on each end. Coincident pins attach to a shared junction through
// 1e-9 ohm edges. If driver-aware construction exceeds its work bound, spatial
// FLUTE groups are joined by representative trees and any cycles are removed.
ph::Rc_net steiner_rc(std::string driver, const std::vector<std::pair<std::string, std::pair<double, double>>>& pins,
                      const Wire_rc& wire, double dbu, bool* approximate) {
  initialize_flute_tables();
  const auto driver_pin = std::find_if(pins.begin(), pins.end(), [&](const auto& pin) { return pin.first == driver; });
  if (driver_pin == pins.end()) {
    throw std::invalid_argument("Steiner RC requires a driver pin");
  }
  const auto driver_index = static_cast<size_t>(driver_pin - pins.begin());
  ph::Rc_net rc;
  rc.driver = std::move(driver);
  std::vector<int>                        xs, ys;
  std::map<std::pair<int, int>, unsigned> junction;
  std::vector<std::pair<int, int>>        positions;
  for (const auto& [label, xy] : pins) {
    if (std::abs(xy.first) > std::numeric_limits<int>::max() / 4.0 || std::abs(xy.second) > std::numeric_limits<int>::max() / 4.0) {
      throw std::runtime_error("coordinates exceed FLUTE integer range");
    }
    positions.emplace_back(std::llround(xy.first), std::llround(xy.second));
    rc.nodes.push_back({label, 0});
  }
  if (wire.horizontal_r == 0 && wire.vertical_r == 0 && wire.horizontal_c == 0 && wire.vertical_c == 0) {
    // Post-synthesis timing has no physical wire model. Its seed positions must
    // not affect criticality through arbitrary junctions or epsilon resistors.
    // One ideal junction gives every pin the same load and zero wire delay.
    const auto hub = static_cast<unsigned>(rc.nodes.size());
    rc.nodes.push_back({{}, 0});
    for (unsigned pin = 0; pin < hub; ++pin) {
      rc.edges.push_back({pin, hub, 0});
    }
    return rc;
  }
  auto vertex = [&](std::pair<int, int> xy) {
    auto [it, added] = junction.emplace(xy, rc.nodes.size());
    if (added) {
      rc.nodes.push_back({{}, 0});
    }
    return it->second;
  };
  std::vector<double> edge_capacitance, edge_length;
  for (size_t i = 0; i < positions.size(); ++i) {
    edge_capacitance.push_back(0);
    edge_length.push_back(0);
    rc.edges.push_back({static_cast<unsigned>(i), vertex(positions[i]), 1e-9});
  }
  for (const auto& [xy, index] : junction) {
    (void)index;
    xs.push_back(xy.first);
    ys.push_back(xy.second);
  }
  auto add_edge = [&](unsigned a, unsigned b, double dx, double dy) {
    if (a == b) {
      return;
    }
    const double resistance  = (wire.horizontal_r * dx + wire.vertical_r * dy) / dbu;
    const double capacitance = (wire.horizontal_c * dx + wire.vertical_c * dy) / dbu;
    rc.edges.push_back({a, b, std::max(1e-9, resistance)});
    edge_capacitance.push_back(capacitance);
    edge_length.push_back((dx + dy) / dbu);
    rc.wire_length_um          += edge_length.back();
    rc.nodes[a].capacitance_ff += capacitance / 2;
    rc.nodes[b].capacitance_ff += capacitance / 2;
  };
  bool timing_tree_built = false;
  if (xs.size() > 1) {
    std::vector<Steiner_location> locations;
    for (size_t i = 0; i < xs.size(); ++i) {
      locations.emplace_back(xs[i], ys[i]);
    }
    const auto       root_location = std::find(locations.begin(),
                                               locations.end(),
                                               Steiner_location{positions[driver_index].first, positions[driver_index].second});
    Steiner_topology tree;
    try {
      tree              = timing_steiner_topology(locations, root_location - locations.begin());
      timing_tree_built = true;
    } catch (const std::length_error&) {
      // Construction has not modified the RC graph, so fallback starts clean.
    }
    if (timing_tree_built) {
      std::vector<unsigned> rc_vertex(tree.locations.size());
      for (const auto terminal : tree.terminals) {
        rc_vertex[terminal] = vertex(tree.locations[terminal]);
      }
      for (size_t i = locations.size(); i < tree.locations.size(); ++i) {
        rc_vertex[i] = rc.nodes.size();
        rc.nodes.push_back({{}, 0});
      }
      for (const auto& [ai, bi] : tree.edges) {
        const auto& a = tree.locations[ai];
        const auto& b = tree.locations[bi];
        add_edge(rc_vertex[ai], rc_vertex[bi], std::abs(double(a.first) - b.first), std::abs(double(a.second) - b.second));
      }
    }
  }
  if (xs.size() > 1 && !timing_tree_built) {
    // Work-bounded fallback: FLUTE trees over spatial groups of at most
    // 1000 locations (recursive median bisection), joined by a FLUTE tree over
    // each group's location nearest the driver. A star from the driver would
    // charge every sink its full Manhattan distance (a 1036-sink net timed 6x
    // slower than a Steiner estimate).
    if (approximate) {
      *approximate = true;
    }
    std::vector<std::pair<int, int>> points;
    for (const auto& [xy, index] : junction) {
      (void)index;
      points.push_back(xy);
    }
    std::set<std::pair<unsigned, unsigned>> edges;
    const auto                              tree = [&](const std::vector<std::pair<int, int>>& group) {
      if (group.size() < 2) {
        return;
      }
      std::vector<int> gx, gy;
      for (const auto& p : group) {
        gx.push_back(p.first);
        gy.push_back(p.second);
      }
      auto t = flute::flute(gx.size(), gx.data(), gy.data(), 3);
      for (int i = 0; i < 2 * t.deg - 2; ++i) {
        const auto a  = std::pair{t.branch[i].x, t.branch[i].y};
        const auto b  = std::pair{t.branch[t.branch[i].n].x, t.branch[t.branch[i].n].y};
        const auto ai = vertex(a), bi = vertex(b);
        if (edges.emplace(std::min(ai, bi), std::max(ai, bi)).second) {
          add_edge(ai, bi, std::abs(double(a.first) - b.first), std::abs(double(a.second) - b.second));
        }
      }
      std::free(t.branch);
    };
    const auto root  = positions[driver_index];
    const auto close = [&](const std::pair<int, int>& p) {
      return std::abs(double(p.first) - root.first) + std::abs(double(p.second) - root.second);
    };
    // Groups of <= 1000 locations; their representatives form the next level.
    std::vector<std::pair<int, int>> level = points;
    while (level.size() > 1000) {
      std::vector<std::vector<std::pair<int, int>>> groups;
      const auto                                    split = [&](auto&& self, std::vector<std::pair<int, int>> items) -> void {
        if (items.size() <= 1000) {
          groups.push_back(std::move(items));
          return;
        }
        int x0 = items.front().first, x1 = x0, y0 = items.front().second, y1 = y0;
        for (const auto& p : items) {
          x0 = std::min(x0, p.first);
          x1 = std::max(x1, p.first);
          y0 = std::min(y0, p.second);
          y1 = std::max(y1, p.second);
        }
        const bool by_x = x1 - x0 >= y1 - y0;
        const auto mid  = items.begin() + items.size() / 2;
        std::nth_element(items.begin(), mid, items.end(), [&](const auto& a, const auto& b) {
          return by_x ? a < b : std::pair{a.second, a.first} < std::pair{b.second, b.first};
        });
        self(self, std::vector<std::pair<int, int>>(items.begin(), mid));
        self(self, std::vector<std::pair<int, int>>(mid, items.end()));
      };
      split(split, level);
      level.clear();
      for (const auto& g : groups) {
        tree(g);
        level.push_back(*std::min_element(g.begin(), g.end(), [&](const auto& a, const auto& b) { return close(a) < close(b); }));
      }
    }
    tree(level);
  }
  // Coordinate coalescing can make independently built FLUTE trees share
  // several junctions. Their geometric union is connected but need not be a
  // tree: edge deduplication alone leaves cycles, which RC traversal cannot
  // time. Leave an already valid tree's edges and capacitance sums untouched.
  if (!rc.nodes.empty()) {
    std::vector<unsigned> parent(rc.nodes.size()), size(rc.nodes.size(), 1);
    const auto            reset = [&] {
      std::iota(parent.begin(), parent.end(), 0);
      std::fill(size.begin(), size.end(), 1);
    };
    const auto root = [&](unsigned n) {
      while (parent[n] != n) {
        parent[n] = parent[parent[n]];
        n         = parent[n];
      }
      return n;
    };
    const auto join = [&](unsigned a, unsigned b) {
      a = root(a);
      b = root(b);
      if (a == b) {
        return false;
      }
      if (size[a] < size[b]) {
        std::swap(a, b);
      }
      parent[b]  = a;
      size[a]   += size[b];
      return true;
    };
    reset();
    bool cyclic = false;
    for (const auto& edge : rc.edges) {
      cyclic |= !join(edge.from, edge.to);
    }
    for (unsigned i = 0; i < rc.nodes.size(); ++i) {
      if (root(i) != root(0)) {
        throw std::runtime_error("disconnected FLUTE RC graph for " + rc.driver);
      }
    }
    if (cyclic) {
      reset();
      std::vector<bool> keep(rc.edges.size());
      // Pin-to-junction edges are mandatory leaves, including coincident pins.
      for (size_t i = 0; i < pins.size(); ++i) {
        const auto& edge = rc.edges[i];
        if (!join(edge.from, edge.to)) {
          throw std::logic_error("cyclic FLUTE pin attachments");
        }
        keep[i] = true;
      }
      std::vector<size_t> candidates(rc.edges.size() - pins.size());
      std::iota(candidates.begin(), candidates.end(), pins.size());
      std::stable_sort(candidates.begin(), candidates.end(), [&](size_t a, size_t b) {
        return rc.edges[a].resistance_ohm < rc.edges[b].resistance_ohm;
      });
      for (const auto i : candidates) {
        const auto& edge = rc.edges[i];
        keep[i]          = join(edge.from, edge.to);
      }
      for (auto& node : rc.nodes) {
        node.capacitance_ff = 0;
      }
      rc.wire_length_um = 0;
      size_t retained   = 0;
      for (size_t i = 0; i < rc.edges.size(); ++i) {
        if (keep[i]) {
          rc.wire_length_um                  += edge_length[i];
          const auto edge                     = rc.edges[i];
          rc.nodes[edge.from].capacitance_ff += edge_capacitance[i] / 2;
          rc.nodes[edge.to].capacitance_ff   += edge_capacitance[i] / 2;
          rc.edges[retained++]                = edge;
        }
      }
      rc.edges.resize(retained);
      if (retained + 1 != rc.nodes.size()) {
        throw std::logic_error("FLUTE spanning tree did not retain every junction");
      }
      if (approximate) {
        *approximate = true;
      }
    }
  }
  return rc;
}
namespace {
std::shared_ptr<ph::Parasitics> estimate_rc(const Geometry_view& geometry, const ph::Technology& tech, const Options& options,
                                            std::map<std::string, std::pair<double, double>>* pin_locations,
                                            Rc_cache* cache = nullptr, Rc_reuse_stats* reuse = nullptr, bool provenance = true) {
  const auto& v                    = geometry.view;
  const auto& design               = geometry.design;
  auto result               = std::make_shared<ph::Parasitics>();
  result->approximate       = !options.full;
  const auto   wire         = technology_wire_rc(tech,
                                                 options.liberty,
                                                 options.tech_rc_file,
                                                 options.resistance_ohm_um,
                                                 options.capacitance_ff_um,
                                                 options.rc_pessimism,
                                                 options.technology().cells.contents);
  const double horizontal_r = wire.horizontal_r, horizontal_c = wire.horizontal_c;
  const double vertical_r = wire.vertical_r, vertical_c = wire.vertical_c;
  if (provenance) {
    rapidjson::StringBuffer                    buffer;
    rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
    json.StartObject();
    json.Key("model");
    json.String("estimated_driver_aware_Steiner_RC");
    json.Key("rc_pessimism");
    json.Double(options.rc_pessimism);
    json.Key("wire_r_c_scale");
    json.Double(std::sqrt(options.rc_pessimism));
    json.Key("source");
    json.String(wire.source.c_str());
    json.Key("resistance_units");
    json.String("ohm_per_um");
    json.Key("capacitance_units");
    json.String("fF_per_um");
    for (auto [key, value] : {
             std::pair{"horizontal_r", horizontal_r},
             {"horizontal_c", horizontal_c},
             {  "vertical_r",   vertical_r},
             {  "vertical_c",   vertical_c}
    }) {
      json.Key(key);
      json.Double(value);
    }
    json.EndObject();
    std::ofstream out(options.output_dir + "/wire_rc.json");
    if (!(out << buffer.GetString() << '\n')) {
      throw std::runtime_error("cannot write technology RC provenance");
    }
  }
  initialize_flute_tables();
  uint64_t hash
      = hash_text(std::format("{}:{}:{}:{}:{}:{}", horizontal_r, horizontal_c, vertical_r, vertical_c, options.full, tech.dbu));
  // Reuse the formatting storage across pins and RC edges. The byte stream is
  // unchanged, so this does not change the persistent RC identity.
  std::string hash_buffer;
  hash_buffer.reserve(256);
  const auto hash_format = [&]<typename... Args>(std::format_string<Args...> format, Args&&... args) {
    hash_buffer.clear();
    std::format_to(std::back_inserter(hash_buffer), format, std::forward<Args>(args)...);
    hash = hash_text(hash_buffer, hash);
  };
  result->nets.reserve(v.drivers.size());
  const auto append = [&](ph::Rc_net rc, bool approximate) {
    result->approximate |= approximate;
    for (const auto& node : rc.nodes) {
      hash_format("{}:{:.17g};", node.pin, node.capacitance_ff);
    }
    for (const auto& edge : rc.edges) {
      hash_format("{}:{}:{:.17g};", edge.from, edge.to, edge.resistance_ohm);
    }
    result->nets.push_back(std::move(rc));
  };
  // Keep identity hashing in its historical per-net order under either path.
  std::vector<Rc_geometry>                                       geometries;
  std::vector<std::pair<std::string, std::pair<double, double>>> pins;
  for (size_t net = 0; net < v.drivers.size(); ++net) {
    const int begin = design.flat_net2pin_start[net], end = design.flat_net2pin_start[net + 1];
    if (end - begin < 2) {
      continue;
    }
    pins.clear();
    uint64_t load_hash = hash_util::kFnv1a64_offset;
    for (int p = begin; p < end; ++p) {
      const auto   node = design.pin2node[p];
      const auto   pin_offset = geometry.pin_offset(p);
      const auto [x, y]       = geometry.pin_position(p, pin_offset);
      pins.emplace_back(v.pins[p].label(), std::pair{x, y});
      if (pin_locations) {
        pin_locations->emplace(pins.back().first, std::pair{x, y});
      }
      if (!cache) {
        hash_format("{}:{}:{};", pins.back().first, std::llround(x), std::llround(y));
      }
      if (cache && size_t(node) < v.cells.size()) {
        const auto& cell = v.cells[node];
        load_hash        = hash_text(std::format("{}:{}:{}:{}:{}:{:.17g}:{:.17g};",
                                                 cell.macro,
                                                 cell.place.width,
                                                 cell.place.height,
                                                 static_cast<unsigned>(geometry.pose(node).orientation),
                                                 v.pins[p].pin,
                                                 pin_offset.first,
                                                 pin_offset.second),
                                     load_hash);
      }
    }
    if (cache) {
      geometries.push_back({v.drivers[net].label(), pins, std::format("{:016x}", load_hash)});
      continue;
    }
    bool approximate = false;
    auto rc          = steiner_rc(v.drivers[net].label(), pins, wire, tech.dbu, &approximate);
    append(std::move(rc), approximate);
  }
  if (cache) {
    const Rc_policy policy{per_um(wire), double(tech.dbu), wire.source};
    const auto      snapshot = cache->update(std::move(geometries), policy, [&](const Rc_geometry& net, const Rc_policy&) {
      Rc_cache::Built built;
      built.rc = steiner_rc(net.driver, net.pins, wire, tech.dbu, &built.approximate);
      return built;
    });
    if (reuse) {
      *reuse = snapshot.stats;
    }
    hash = hash_text(std::format("{}:{}:{}:{}:{}:{}", horizontal_r, horizontal_c, vertical_r, vertical_c, options.full, tech.dbu));
    for (const auto& entry : snapshot.nets) {
      for (const auto& [name, xy] : entry->geometry.pins) {
        hash_format("{}:{}:{};", name, std::llround(xy.first), std::llround(xy.second));
      }
      append(entry->built->rc, entry->built->approximate);
    }
  }
  result->identity = std::format("{:016x}", hash);
  return result;
}
// Attach the view's cell poses and IO terminal positions to its RC estimate,
// as timing and repair consumers of a placement checkpoint expect them.
void record_poses(ph::Parasitics& rc, const Geometry_view& geometry) {
  const auto& v = geometry.view;
  for (size_t i = 0; i < v.cells.size(); ++i) {
    const auto& c    = v.cells[i];
    const auto  pose = geometry.pose(i);
    rc.cells.push_back({c.name, pose.x, pose.y, static_cast<unsigned>(pose.orientation), c.macro, pose.width, pose.height});
  }
  for (const auto& t : v.fp.terminals) {
    rc.terminals.push_back({t.name, t.x, t.y});
  }
}
struct CacheInfo {
  size_t reused = 0, changed = 0;
  bool   hit = false;
};
std::map<std::string, uint64_t> block_hashes(const View& v) {
  std::map<std::string, uint64_t>    hashes;
  std::map<std::string, std::string> cell_blocks;
  for (const auto& c : v.cells) {
    cell_blocks.emplace(c.name, c.block);
    auto [it, added] = hashes.emplace(c.block, hash_util::kFnv1a64_offset);
    (void)added;
    it->second = hash_text(std::format("{}:{}:{}:{};", c.name, c.macro, c.place.width, c.place.height), it->second);
  }
  for (const auto& [driver, sinks] : v.nets) {
    for (const auto& sink : sinks) {
      const auto mix = [&](const Endpoint& e) {
        if (e.instance == "PIN") {
          return;
        }
        auto& h = hashes.at(cell_blocks.at(e.instance));
        h       = hash_text(std::format("{}/{}->{}/{};", driver.instance, driver.pin, sink.instance, sink.pin), h);
      };
      mix(driver);
      mix(sink);
    }
  }
  return hashes;
}
CacheInfo restore_cache(View& v, const ph::Technology& tech, const std::string& path, const std::string& environment,
                        const std::map<std::string, uint64_t>& hashes) {
  CacheInfo info;
  info.changed = hashes.size();
  if (path.empty() || !fs::is_regular_file(path)) {
    return info;
  }
  rapidjson::Document json;
  json.Parse(file_text(path).c_str());
  if (json.HasParseError() || !json.IsObject() || !json.HasMember("environment") || !json["environment"].IsString()
      || json["environment"].GetString() != environment || !json.HasMember("blocks") || !json["blocks"].IsObject()) {
    return info;
  }
  // rapidjson member lookup is a linear scan; index each object once.
  using Members    = std::unordered_map<std::string_view, const rapidjson::Value*>;
  const auto index = [](const rapidjson::Value& object) {
    Members out;
    for (const auto& m : object.GetObject()) {
      out.emplace(std::string_view(m.name.GetString(), m.name.GetStringLength()), &m.value);
    }
    return out;
  };
  const auto                          blocks = index(json["blocks"]);
  std::map<std::string_view, Members> unchanged;  // block -> cell positions, hash-matching blocks only
  for (const auto& [name, hash] : hashes) {
    const auto it = blocks.find(name);
    if (it == blocks.end()) {
      continue;
    }
    const auto& block = *it->second;
    if (!block.IsObject() || !block.HasMember("hash") || !block["hash"].IsString() || !block.HasMember("cells")
        || !block["cells"].IsObject()) {
      return info;
    }
    if (block["hash"].GetString() == std::format("{:016x}", hash)) {
      unchanged.emplace(name, index(block["cells"]));
    }
  }
  // Validate the complete record before applying any cached coordinate.
  auto cells = v.cells;
  for (auto& cell : cells) {
    const auto block = unchanged.find(cell.block);
    if (block == unchanged.end()) {
      const auto old = blocks.find(cell.block);
      if (old != blocks.end() && old->second->IsObject()) {
        const auto& record = *old->second;
        if (record.HasMember("anchor") && record["anchor"].IsArray() && record["anchor"].Size() == 4
            && record["anchor"][0].IsInt64() && record["anchor"][1].IsInt64() && record["anchor"][2].IsInt64()
            && record["anchor"][3].IsInt64() && record["anchor"][0].GetInt64() < record["anchor"][2].GetInt64()
            && record["anchor"][1].GetInt64() < record["anchor"][3].GetInt64() && cell.place.status != Status::FIXED
            && cell.place.status != Status::LOCKED) {
          cell.place.x         = record["anchor"][0].GetInt64()
                                 + (record["anchor"][2].GetInt64() - record["anchor"][0].GetInt64() - cell.place.width) / 2;
          cell.place.y         = record["anchor"][1].GetInt64()
                                 + (record["anchor"][3].GetInt64() - record["anchor"][1].GetInt64() - cell.place.height) / 2;
          const auto positions = record.HasMember("cells") && record["cells"].IsObject() ? index(record["cells"]) : Members{};
          const auto xy        = positions.find(cell.name);
          if (xy != positions.end() && xy->second->IsArray() && xy->second->Size() == 2 && (*xy->second)[0].IsInt64()
              && (*xy->second)[1].IsInt64()) {
            cell.place.x = (*xy->second)[0].GetInt64();
            cell.place.y = (*xy->second)[1].GetInt64();
          }
          cell.place.status = Status::PLACED;
        }
      }
      continue;
    }
    const auto position = block->second.find(cell.name);
    if (position == block->second.end()) {
      return info;
    }
    const auto& xy = *position->second;
    if (!xy.IsArray() || xy.Size() != 2 || !xy[0].IsInt64() || !xy[1].IsInt64()) {
      return info;
    }
    if (cell.place.status == Status::FIXED || cell.place.status == Status::LOCKED) {
      if (cell.place.x != xy[0].GetInt64() || cell.place.y != xy[1].GetInt64()) {
        return info;
      }
    } else {
      cell.place.x = xy[0].GetInt64();
      cell.place.y = xy[1].GetInt64();
      cell.locked  = true;
    }
  }
  std::set<std::string> reused;
  for (auto& cell : cells) {
    if (cell.locked) {
      reused.insert(cell.block);
    }
  }
  info.reused  = reused.size();
  info.changed = hashes.size() - info.reused;
  info.hit     = info.changed == 0;
  // IO positions affect placement even if every logic block is unchanged.
  if (!json.HasMember("terminals") || !json["terminals"].IsObject()) {
    return CacheInfo{0, hashes.size(), false};
  }
  auto       terminals = v.fp.terminals;
  const auto terms     = index(json["terminals"]);
  for (auto& terminal : terminals) {
    const auto it = terms.find(terminal.name);
    if (it == terms.end()) {
      return CacheInfo{0, hashes.size(), false};
    }
    const auto& xy = *it->second;
    if (!xy.IsArray() || xy.Size() != 2 || !xy[0].IsInt64() || !xy[1].IsInt64()) {
      return CacheInfo{0, hashes.size(), false};
    }
    if (terminal.status != Status::FIXED) {
      terminal.x = xy[0].GetInt64();
      terminal.y = xy[1].GetInt64();
    }
  }
  const auto& first  = v.fp.rows.front();
  const auto  step   = first.step;
  const auto  height = v.fp.rows.size() > 1 ? v.fp.rows[1].y - first.y : static_cast<int64_t>(v.fp.die.yh - first.y);
  for (auto& cell : cells) {
    if (cell.locked) {
      if (cell.place.x < first.x || cell.place.y < first.y || cell.place.x > first.x + first.count * step - cell.place.width
          || cell.place.y > v.fp.rows.back().y + height - cell.place.height || (cell.place.x - first.x) % step
          || (cell.place.y - first.y) % height) {
        return CacheInfo{0, hashes.size(), false};
      }
      // Tie cells are materialized after the previous DEF frame was seeded.
      // Cache locks bypass capture(), so restore their status/orientation here
      // as well as their coordinates (same CORE-only rule as capture()). A
      // cached spot the master cannot legally take is re-placed instead.
      cell.place.status = Status::PLACED;
      if (!apply_row_orientation(cell, tech, v.fp.rows.at((cell.place.y - first.y) / height).orientation)) {
        return CacheInfo{0, hashes.size(), false};
      }
    }
  }
  v.cells        = std::move(cells);
  v.fp.terminals = std::move(terminals);
  return info;
}
// Dynamic incident-net extrema avoid rescanning a high-fanout net for every
// dirty sink. Own pins are removed during a cell's search and reinserted only
// after its chosen site; pending cells therefore retain their original seeds.
class Eco_connectivity {
  struct Net {
    double                weight = 1;
    std::multiset<double> x, y;
  };
  struct Pin_point {
    size_t               net = 0;
    double               x = 0, y = 0;
    std::array<Point, 2> offsets;
  };
  struct Term {
    double                  weight = 1;
    std::array<ph::Rect, 2> origins;
  };
  std::vector<Net>                         nets;
  std::vector<Pin_point>                   pins;
  std::vector<std::vector<size_t>>         incident;
  std::vector<Term>                        terms;
  std::array<std::pair<double, double>, 2> minima;
  double                                   strength;
  static double outside(double value, double lo, double hi) { return std::max({lo - value, 0.0, value - hi}); }

public:
  Eco_connectivity(const View& v, const ph::Technology& tech, const std::vector<size_t>& edits, double weight)
      : incident(v.cells.size()), strength(weight) {
    std::vector<bool> dirty(v.cells.size());
    for (const auto i : edits) {
      dirty[i] = true;
    }
    const auto& d = v.design;
    for (size_t n = 0; n < d.net_weights.size(); ++n) {
      if (!d.net_mask.empty() && !d.net_mask[n]) {
        continue;  // pre-CTS clocks and empty nets are absent from HPWL
      }
      const auto begin = d.flat_net2pin_start[n], end = d.flat_net2pin_start[n + 1];
      bool       relevant = false;
      for (int k = begin; k < end; ++k) {
        const auto node  = d.pin2node[d.flat_net2pin[k]];
        relevant        |= node < static_cast<int>(dirty.size()) && dirty[node];
      }
      if (!relevant) {
        continue;
      }
      const auto net = nets.size();
      nets.emplace_back();
      nets.back().weight = d.net_weights[n];
      for (int k = begin; k < end; ++k) {
        const auto   p = d.flat_net2pin[k], node = d.pin2node[p];
        const double x = d.node_x[node] + d.pin_offset_x[p], y = d.node_y[node] + d.pin_offset_y[p];
        nets.back().x.insert(x);
        nets.back().y.insert(y);
        if (node < static_cast<int>(dirty.size()) && dirty[node]) {
          const auto& cell  = v.cells[node];
          const auto& macro = tech.macros.at(cell.macro);
          incident[node].push_back(pins.size());
          pins.push_back({
              net,
              x,
              y,
              {offset(macro, v.pins[p].pin, Orientation::N, tech.dbu), offset(macro, v.pins[p].pin, Orientation::FS, tech.dbu)}
          });
        }
      }
    }
  }
  size_t net_count() const { return nets.size(); }
  size_t pin_count() const { return pins.size(); }
  bool   active() const { return !terms.empty(); }
  void   prepare(size_t cell, double seed_x) {
    terms.clear();
    // incident[cell] is grouped by net, since construction visits whole nets.
    for (size_t first = 0; first < incident[cell].size();) {
      const auto              net   = pins[incident[cell][first]].net;
      auto&                   other = nets[net];
      std::array<ph::Rect, 2> own;
      for (auto& box : own) {
        box = {std::numeric_limits<double>::infinity(),
               std::numeric_limits<double>::infinity(),
               -std::numeric_limits<double>::infinity(),
               -std::numeric_limits<double>::infinity()};
      }
      size_t last = first;
      while (last < incident[cell].size() && pins[incident[cell][last]].net == net) {
        const auto& pin = pins[incident[cell][last++]];
        other.x.erase(other.x.find(pin.x));
        other.y.erase(other.y.find(pin.y));
        for (size_t o = 0; o < own.size(); ++o) {
          const auto [x, y] = pin.offsets[o];
          own[o].xl         = std::min(own[o].xl, x);
          own[o].xh         = std::max(own[o].xh, x);
          own[o].yl         = std::min(own[o].yl, y);
          own[o].yh         = std::max(own[o].yh, y);
        }
      }
      if (!other.x.empty()) {
        Term term;
        term.weight = strength * other.weight;
        for (size_t o = 0; o < own.size(); ++o) {
          const double xa = *other.x.begin() - own[o].xl, xb = *other.x.rbegin() - own[o].xh;
          const double ya = *other.y.begin() - own[o].yl, yb = *other.y.rbegin() - own[o].yh;
          term.origins[o] = {std::min(xa, xb), std::min(ya, yb), std::max(xa, xb), std::max(ya, yb)};
        }
        terms.push_back(term);
      }
      first = last;
    }
    for (size_t o = 0; o < minima.size(); ++o) {
      std::vector<std::pair<double, double>> points{
          {seed_x, 1}
      };
      double total = 1;
      for (const auto& term : terms) {
        points.emplace_back(term.origins[o].xl, term.weight / 2);
        points.emplace_back(term.origins[o].xh, term.weight / 2);
        total += term.weight;
      }
      std::sort(points.begin(), points.end());
      double cumulative = 0;
      for (size_t k = 0; k < points.size(); ++k) {
        cumulative += points[k].second;
        if (cumulative >= total / 2) {
          minima[o] = {points[k].first, cumulative == total / 2 && k + 1 < points.size() ? points[k + 1].first : points[k].first};
          break;
        }
      }
    }
  }
  double preferred_x(double seed_x, bool fs) const { return std::clamp(seed_x, minima[fs].first, minima[fs].second); }
  double vertical(double y, bool fs) const {
    double result = 0;
    for (const auto& term : terms) {
      const auto& box  = term.origins[fs];
      result          += term.weight * outside(y, box.yl, box.yh);
    }
    return result;
  }
  double horizontal(double x, bool fs) const {
    double result = 0;
    for (const auto& term : terms) {
      const auto& box  = term.origins[fs];
      result          += term.weight * outside(x, box.xl, box.xh);
    }
    return result;
  }
  void commit(size_t cell, const Ann_place& chosen) {
    const bool fs = chosen.orientation == Orientation::FS;
    for (const auto id : incident[cell]) {
      auto& pin = pins[id];
      pin.x     = chosen.x + pin.offsets[fs].first;
      pin.y     = chosen.y + pin.offsets[fs].second;
      nets[pin.net].x.insert(pin.x);
      nets[pin.net].y.insert(pin.y);
    }
  }
};
struct Eco_trace {
  std::string name, master;
  Ann_place   seed, chosen;
  size_t      row = 0, order = 0;
  int64_t     gap_lo = 0, gap_hi = 0;
};
struct Eco_stats {
  bool                   used           = false;
  bool                   single_cell    = false;
  bool                   released_locks = false;  // whole-core retry made cache-reused cells movable
  unsigned               attempts = 0, moved = 0;
  double                 max_displacement_um = 0;
  ph::Rect               window;
  std::vector<Eco_trace> trace;
  size_t                 connectivity_nets = 0, connectivity_pins = 0, candidate_sites = 0, candidate_rows = 0;
  double                 connectivity_seconds = 0;
};
// An ECO re-places a cell that is new to the reference or whose master or
// footprint changed.
bool eco_dirty(const std::map<std::string, const ph::Component*>& reference, const Cell& c) {
  const auto it = reference.find(c.name);
  return it == reference.end() || it->second->macro != c.macro || it->second->place.width != c.place.width
         || it->second->place.height != c.place.height;
}
// Terminal legalization's bin grid: an explicit legalize.bin.grid as given,
// else the routing grid, bounded for tiny cores.
void set_terminal_grid(const ph::Technology& tech, const ph::Floorplan& frame, const Options& options, int bins_x, int bins_y,
                       Terminal_legalize_options& terminal) {
  terminal.bins_x = bins_x;
  terminal.bins_y = bins_y;
  if (options.legalize_bins) {
    terminal.bins_x = terminal.bins_y = options.legalize_bins;
  } else {
    fit_terminal_legalize_grid(tech, frame, terminal);
  }
}
// The anchor prepass controls: the provisional domino masters and their
// row-access templates, in terminal legalization's occupancy model.
Multirow_options anchor_controls(const Options& options, const Terminal_legalize_options& terminal,
                                 std::map<std::string, int64_t> core_origins) {
  Multirow_options anchors;
  anchors.access       = options.row_access ? &*options.row_access : nullptr;
  anchors.templates    = &options.multirow_templates;
  anchors.occupancy    = occupancy_controls(terminal);
  anchors.core_origins = std::move(core_origins);
  anchors.admits       = livehd::is_provisional_domino_cell;
  return anchors;
}
// Cell i and its netlist instance take `macro`, the rail twin of its master
// (same footprint and signal pins, the other PG rail stack).
void take_rail_twin(View& v, size_t i, const std::string& macro) {
  auto& c = v.cells[i];
  if (c.macro != macro) {
    change_custom_declaration(c.node, macro);
    c.macro = macro;
  }
}
// Each cell takes the master its component in frame has: the anchor prepass
// and the two-row ECO pick the rail twin a start needs. frame lists v.cells
// first, in order.
void adopt_rail_twins(View& v, const ph::Floorplan& frame) {
  for (size_t i = 0; i < v.cells.size(); ++i) {
    take_rail_twin(v, i, frame.components.at(i).macro);
  }
}
// Every placed multi-row cell with a rail twin takes the master the PG rails
// of its rows need (a restored placement keeps the netlist's template).
// Returns the cells switched.
size_t sync_rail_twins(View& v, const ph::Technology& tech, const Options& options) {
  if (options.multirow_templates.empty() || v.fp.rows.empty()) {
    return 0;
  }
  const Pg_rail_model rails(tech);
  const auto          rh       = std::llround(tech.sites.at(v.fp.rows.front().site).height * tech.dbu);
  size_t              switched = 0;
  for (size_t i = 0; i < v.cells.size(); ++i) {
    const auto& c = v.cells[i];
    const auto  t = options.multirow_templates.find(c.macro);
    if (t == options.multirow_templates.end() || t->second.rail_twin.empty() || c.place.status == Status::UNPLACED
        || c.place.height <= rh) {
      continue;
    }
    std::vector<Orientation> rows;
    for (int64_t y = c.place.y; y < c.place.y + c.place.height; y += rh) {
      const auto row = std::find_if(v.fp.rows.begin(), v.fp.rows.end(), [&](const ph::Row& r) {
        return r.y == y && c.place.x >= r.x && c.place.x < r.x + r.count * r.step;
      });
      if (row == v.fp.rows.end()) {
        break;  // check_legality names a cell outside the rows
      }
      rows.push_back(row->orientation);
    }
    if (rows.size() * rh == size_t(c.place.height) && rails.fit(c.macro, c.place.orientation, rows) == Rail_fit::mismatch
        && rails.fit(t->second.rail_twin, c.place.orientation, rows) == Rail_fit::fit) {
      take_rail_twin(v, i, t->second.rail_twin);
      ++switched;
    }
  }
  return switched;
}
// Nearest-whitespace ECO legalization of the dirty single-row CORE cells
// around every other cell. Multi-row obstacles keep multirow_padding_sites
// free on both sides, as the two-row ECO (multirow_eco.hpp) keeps them.
bool legalize_edits(View& v, const ph::Technology& tech, const ph::Floorplan& reference, const std::vector<std::string>& forced,
                    Eco_stats& stats, const std::vector<std::string>& trace_cells, double connectivity_weight,
                    unsigned multirow_padding_sites = 0) {
  const auto                                  connectivity_start = std::chrono::steady_clock::now();
  const std::set<std::string>                 traced(trace_cells.begin(), trace_cells.end());
  std::vector<Eco_trace>                      trace;
  const auto                                  original = v.cells;
  std::map<std::string, const ph::Component*> old;
  for (const auto& c : reference.components) {
    old.emplace(c.name, &c);
  }
  std::set<std::string> dirty(forced.begin(), forced.end());
  for (const auto& c : v.cells) {
    if (eco_dirty(old, c)) {
      dirty.insert(c.name);
    }
  }
  const auto&            first = v.fp.rows.front();
  const int64_t          sw = first.step, rh = std::llround(tech.sites.at(first.site).height * tech.dbu);
  std::vector<Ann_place> obstacles;
  std::vector<size_t>    edits;
  for (size_t i = 0; i < v.cells.size(); ++i) {
    const auto& c    = v.cells[i];
    const auto  mask = ph::placement_mask({c.name, c.macro, c.place}, tech);
    if (mask == ph::Placement_mask::soft) {
      continue;
    }
    if (!dirty.contains(c.name) || c.locked || c.place.status == Status::FIXED || c.place.status == Status::LOCKED) {
      auto obstacle = c.place;
      if (mask == ph::Placement_mask::none && !tech.macros.at(c.macro).klass.starts_with("CORE")) {
        obstacle.x      -= v.halo_x;
        obstacle.y      -= v.halo_y;
        obstacle.width  += 2 * v.halo_x;
        obstacle.height += 2 * v.halo_y;
      } else if (multirow_padding_sites && is_multirow_core(tech, tech.macros.at(c.macro))) {
        const auto padded = padded_rect(tech, {c.name, c.macro, c.place}, multirow_padding_sites);
        obstacle.x        = std::llround(padded.xl);
        obstacle.width    = std::llround(padded.xh - padded.xl);
      }
      obstacles.push_back(obstacle);
    } else if (!tech.macros.at(c.macro).klass.starts_with("CORE") || c.place.height != rh) {
      return false;
    } else {
      edits.push_back(i);
    }
  }
  std::sort(edits.begin(), edits.end(), [&](size_t a, size_t b) {
    return v.cells[a].place.width != v.cells[b].place.width ? v.cells[a].place.width > v.cells[b].place.width
                                                            : v.cells[a].name < v.cells[b].name;
  });
  std::unique_ptr<Eco_connectivity> connectivity;
  if (connectivity_weight > 0) {
    connectivity = std::make_unique<Eco_connectivity>(v, tech, edits, connectivity_weight);
  }
  size_t                                                candidate_sites = 0, candidate_rows = 0;
  // Preserve the exhaustive row/gap search, but collect each row's obstacles
  // once. Scanning every cell for every row of every inserted buffer is costly
  // even when a large core has abundant whitespace.
  std::vector<std::vector<std::pair<int64_t, int64_t>>> occupied_rows(v.fp.rows.size());
  const auto                                            add_obstacle = [&](const Ann_place& p, bool sorted) {
    auto row = std::upper_bound(v.fp.rows.begin(), v.fp.rows.end(), p.y - rh, [](int64_t y, const auto& r) { return y < r.y; });
    for (; row != v.fp.rows.end() && row->y < p.y + p.height; ++row) {
      auto&                             intervals = occupied_rows[row - v.fp.rows.begin()];
      const std::pair<int64_t, int64_t> interval{p.x, p.x + p.width};
      if (sorted) {
        intervals.insert(std::lower_bound(intervals.begin(), intervals.end(), interval), interval);
      } else {
        intervals.push_back(interval);
      }
    }
  };
  for (const auto& p : obstacles) {
    add_obstacle(p, false);
  }
  const ph::Rect domain{double(first.x), double(first.y), double(first.x + first.count * sw), double(v.fp.rows.back().y + rh)};
  for (const auto& exclusion : ph::placement_exclusions(blockage_frame(v), domain)) {
    if (exclusion.kind == ph::Placement_mask::hard) {
      const auto& r = exclusion.rect;
      add_obstacle(Ann_place(std::llround(r.xl), std::llround(r.yl), std::llround(r.xh - r.xl), std::llround(r.yh - r.yl)), false);
    }
  }
  for (auto& intervals : occupied_rows) {
    std::sort(intervals.begin(), intervals.end());
  }
  for (size_t order = 0; order < edits.size(); ++order) {
    const auto index = edits[order];
    auto&      c     = v.cells[index];
    const auto seed  = c.place;
    if (connectivity) {
      connectivity->prepare(index, seed.x);
    }
    const bool connected = connectivity && connectivity->active();
    double     best      = std::numeric_limits<double>::infinity();
    Ann_place  chosen;
    size_t     chosen_row    = v.fp.rows.size();
    int64_t    chosen_gap_lo = 0, chosen_gap_hi = 0;
    // Visit rows by their vertical lower bound. Once that alone exceeds the
    // best Manhattan displacement, no farther row can improve it. Resolve
    // equal-cost choices by original row order, as the exhaustive search did.
    size_t     below
        = std::lower_bound(v.fp.rows.begin(), v.fp.rows.end(), seed.y, [](const auto& row, int64_t y) { return row.y < y; })
          - v.fp.rows.begin();
    size_t above = below;
    while (below || above < v.fp.rows.size()) {
      const double lower_distance
          = below ? std::abs(double(v.fp.rows[below - 1].y - seed.y)) : std::numeric_limits<double>::infinity();
      const double upper_distance
          = above < v.fp.rows.size() ? std::abs(double(v.fp.rows[above].y - seed.y)) : std::numeric_limits<double>::infinity();
      if (std::min(lower_distance, upper_distance) > best) {
        break;
      }
      const size_t row_index = lower_distance <= upper_distance ? --below : above++;
      const auto&  row       = v.fp.rows[row_index];
      if (row.orientation == Orientation::FS && !tech.macros.at(c.macro).symmetry_x) {
        continue;
      }
      const bool   fs       = row.orientation == Orientation::FS;
      const double vertical = std::abs(double(row.y - seed.y)) + (connected ? connectivity->vertical(row.y, fs) : 0);
      if (vertical > best) {
        continue;
      }
      ++candidate_rows;
      const auto&   occupied = occupied_rows[row_index];
      int64_t       left     = row.x;
      const int64_t right    = row.x + row.count * sw;
      const auto    consider = [&](int64_t lo, int64_t hi) {
        const auto gap_lo = lo, gap_hi = hi;
        lo = row.x + static_cast<int64_t>(std::ceil(double(lo - row.x) / sw)) * sw;
        hi = row.x + static_cast<int64_t>(std::floor(double(hi - c.place.width - row.x) / sw)) * sw;
        if (hi < lo) {
          return;
        }
        const auto evaluate = [&](int64_t x) {
          ++candidate_sites;
          const double distance = std::abs(double(x - seed.x)) + vertical + (connected ? connectivity->horizontal(x, fs) : 0);
          if (distance < best
              || (distance == best && (row_index < chosen_row || (connected && row_index == chosen_row && x < chosen.x)))) {
            chosen_row         = row_index;
            chosen_gap_lo      = gap_lo;
            chosen_gap_hi      = gap_hi;
            best               = distance;
            chosen             = seed;
            chosen.x           = x;
            chosen.y           = row.y;
            chosen.orientation = row.orientation;
            chosen.status      = Status::PLACED;
          }
        };
        if (!connected) {
          // Keep the legacy llround half-site choice, including clock-only or
          // disconnected cells when the opt-in coefficient is positive.
          const auto desired = row.x + std::llround(double(seed.x - row.x) / sw) * sw;
          evaluate(std::clamp<int64_t>(desired, lo, hi));
        } else {
          const double desired = std::clamp(connectivity->preferred_x(seed.x, fs), double(lo), double(hi));
          const auto   lower   = std::clamp<int64_t>(row.x + std::floor((desired - row.x) / sw) * sw, lo, hi);
          const auto   upper   = std::clamp<int64_t>(row.x + std::ceil((desired - row.x) / sw) * sw, lo, hi);
          evaluate(lower);
          if (upper != lower) {
            evaluate(upper);
          }
        }
      };
      for (const auto& [lo, hi] : occupied) {
        if (hi <= left || lo >= right) {
          continue;
        }
        consider(left, std::min(lo, right));
        left = std::max(left, hi);
        if (left >= right) {
          break;
        }
      }
      if (left < right) {
        consider(left, right);
      }
    }
    if (!std::isfinite(best)) {
      v.cells = original;
      return false;
    }
    if (traced.contains(c.name)) {
      trace.push_back({c.name, c.macro, seed, chosen, chosen_row, order, chosen_gap_lo, chosen_gap_hi});
    }
    if (connectivity) {
      connectivity->commit(index, chosen);
    }
    c.place = chosen;
    add_obstacle(chosen, true);
  }
  ph::Floorplan check = v.fp;
  check.components.clear();
  for (const auto& c : v.cells) {
    check.components.push_back({c.name, c.macro, c.place});
  }
  try {
    check_legality(tech, check, double(v.halo_x) / tech.dbu, double(v.halo_y) / tech.dbu);
  } catch (const std::runtime_error&) {
    v.cells = original;
    return false;
  }
  if (connectivity) {
    stats.connectivity_nets    = connectivity->net_count();
    stats.connectivity_pins    = connectivity->pin_count();
    stats.candidate_sites      = candidate_sites;
    stats.candidate_rows       = candidate_rows;
    stats.connectivity_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - connectivity_start).count();
  }
  stats.trace       = std::move(trace);
  stats.used        = true;
  stats.attempts    = 1;
  stats.single_cell = true;
  stats.window      = v.fp.die;
  for (size_t i = 0; i < v.cells.size(); ++i) {
    const auto& p  = original[i].place;
    const auto& q  = v.cells[i].place;
    stats.moved   += p.x != q.x || p.y != q.y;
    stats.max_displacement_um
        = std::max(stats.max_displacement_um, (std::abs(double(p.x - q.x)) + std::abs(double(p.y - q.y))) / tech.dbu);
  }
  make_arrays(v, tech);
  return true;
}
dp::Result local_place(View& v, const ph::Technology& tech, const Options& options, const ph::Floorplan& reference,
                       const std::vector<std::string>& forced, Eco_stats& stats) {
  std::map<std::string, const ph::Component*> old;
  std::set<std::string>                       current, dirty(forced.begin(), forced.end());
  for (const auto& c : reference.components) {
    old.emplace(c.name, &c);
  }
  ph::Rect   bounds{std::numeric_limits<double>::infinity(),
                    std::numeric_limits<double>::infinity(),
                    -std::numeric_limits<double>::infinity(),
                    -std::numeric_limits<double>::infinity()};
  const auto include = [&](const Ann_place& p) {
    bounds = bounds.hull({double(p.x), double(p.y), double(p.x + p.width), double(p.y + p.height)});
  };
  for (const auto& c : v.cells) {
    current.insert(c.name);
    if (eco_dirty(old, c)) {
      dirty.insert(c.name);
    }
    if (dirty.contains(c.name)) {
      include(c.place);
    }
  }
  for (const auto& [name, c] : old) {
    if (!current.contains(name) || dirty.contains(name)) {
      include(c->place);
    }
  }
  dp::Result metrics;
  if (!std::isfinite(bounds.xl)) {
    make_arrays(v, tech);
    metrics.legal = dp::is_legal(v.design);
    if (metrics.legal) {
      return metrics;
    }
    // A changed halo can invalidate an otherwise unchanged explicit reference.
    // Re-place its movable cells rather than accepting the stale geometry.
    for (const auto& c : v.cells) {
      if (!c.locked && c.place.status != Status::FIXED && c.place.status != Status::LOCKED) {
        include(c.place);
      }
    }
    if (!std::isfinite(bounds.xl)) {
      throw std::runtime_error("fixed reference violates placement clearance");
    }
  }
  stats.used            = true;
  auto         original = v.cells;  // the whole-core retry releases its cache locks
  const auto&  first    = v.fp.rows.front();
  const double sw = first.step, rh = std::llround(tech.sites.at(first.site).height * tech.dbu);
  const double core_xh = first.x + first.count * sw, core_yh = v.fp.rows.back().y + rh;
  for (unsigned attempt = 0; attempt < 32; ++attempt) {
    v.cells             = original;
    const double margin = std::ldexp(2 * rh, attempt);
    ph::Rect     window{std::max(double(first.x), first.x + std::floor((bounds.xl - first.x - margin) / sw) * sw),
                        std::max(double(first.y), first.y + std::floor((bounds.yl - first.y - margin) / rh) * rh),
                        std::min(core_xh, first.x + std::ceil((bounds.xh - first.x + margin) / sw) * sw),
                        std::min(core_yh, first.y + std::ceil((bounds.yh - first.y + margin) / rh) * rh)};
    const bool   entire_core = window.xl == first.x && window.yl == first.y && window.xh == core_xh && window.yh == core_yh;
    stats.window             = window;
    stats.attempts           = attempt + 1;
    for (auto& c : v.cells) {
      const bool intersects = c.place.x < window.xh && c.place.x + c.place.width > window.xl && c.place.y < window.yh
                              && c.place.y + c.place.height > window.yl;
      c.locked              = c.locked || !intersects;
    }
    try {
      make_arrays(v, tech);
      auto& d  = v.design;
      d.layout = {window.xl, window.yl, window.xh, window.yh, sw, rh};
      for (int i = 0; i < static_cast<int>(d.node_x.size()); ++i) {
        if (i < d.num_movable) {
          if (d.node_size_x[i] + 2 * d.node_halo_x[i] > window.xh - window.xl
              || d.node_size_y[i] + 2 * d.node_halo_y[i] > window.yh - window.yl) {
            throw std::runtime_error("ECO cell does not fit window");
          }
          d.node_x[i] = std::clamp(d.node_x[i], window.xl + d.node_halo_x[i], window.xh - d.node_size_x[i] - d.node_halo_x[i]);
          d.node_y[i] = std::clamp(d.node_y[i], window.yl + d.node_halo_y[i], window.yh - d.node_size_y[i] - d.node_halo_y[i]);
        } else if (d.node_size_x[i] > 0 && d.node_size_y[i] > 0) {
          // Clip obstacles to the local solver domain, preserving every pin's
          // absolute location and the physical object's original graph geometry.
          const auto x = d.node_x[i], y = d.node_y[i];
          const auto hx = d.node_halo_x[i], hy = d.node_halo_y[i];
          const auto xl = std::clamp(x - hx, window.xl, window.xh), yl = std::clamp(y - hy, window.yl, window.yh);
          const auto xh    = std::clamp(x + d.node_size_x[i] + hx, window.xl, window.xh);
          const auto yh    = std::clamp(y + d.node_size_y[i] + hy, window.yl, window.yh);
          d.node_x[i]      = xl;
          d.node_y[i]      = yl;
          d.node_size_x[i] = xh - xl;
          d.node_size_y[i] = yh - yl;
          d.node_halo_x[i] = d.node_halo_y[i] = 0;
          for (int k = d.flat_node2pin_start[i]; k < d.flat_node2pin_start[i + 1]; ++k) {
            const int p        = d.flat_node2pin[k];
            d.pin_offset_x[p] += x - xl;
            d.pin_offset_y[p] += y - yl;
          }
        }
      }
      dp::Options control;
      control.max_iterations = std::min(options.iterations, 60);
      control.bins_x = control.bins_y = std::min(options.bins, 16);
      control.num_threads             = options.threads;
      control.target_density          = options.density;
      control.stop_overflow           = options.stop_overflow;
      control.initial_density_weight  = options.initial_density_weight;
      control.add_fillers             = false;
      control.legalize                = true;
      control.detailed_place          = true;
      metrics                         = dp::place(d, control);
      capture(v, tech, true);
      ph::Floorplan check = v.fp;
      check.components.clear();
      std::map<std::string, const Cell*> before;
      for (const auto& c : original) {
        before.emplace(c.name, &c);
      }
      stats.moved               = 0;
      stats.max_displacement_um = 0;
      for (auto& c : v.cells) {
        check.components.push_back({c.name, c.macro, c.place});
        const auto& p = before.at(c.name)->place;
        if (c.place.x != p.x || c.place.y != p.y) {
          ++stats.moved;
        }
        stats.max_displacement_um = std::max(stats.max_displacement_um,
                                             (std::abs(double(c.place.x) - p.x) + std::abs(double(c.place.y) - p.y)) / tech.dbu);
        c.locked                  = before.at(c.name)->locked;
      }
      check_legality(tech, check, options);
      make_arrays(v, tech);
      return metrics;
    } catch (const std::runtime_error&) {
      if (entire_core) {
        // Changed blocks may need whitespace (or a new proxy's footprint) held by
        // cache-locked cells. Retry the whole core once with those unlocked;
        // FIXED/LOCKED status still pins real obstacles.
        if (!stats.released_locks && std::any_of(original.begin(), original.end(), [](const Cell& c) { return c.locked; })) {
          stats.released_locks = true;
          for (auto& c : original) {
            c.locked = false;
          }
          --attempt;  // rerun the same whole-core window
          continue;
        }
        v.cells = original;
        make_arrays(v, tech);
        throw;
      }
    }
  }
  throw std::runtime_error("ECO window expansion exhausted");
}
void save_cache(const View& v, const std::string& path, const std::string& environment,
                const std::map<std::string, uint64_t>& hashes) {
  if (path.empty()) {
    return;
  }
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
  w.StartObject();
  w.Key("environment");
  w.String(environment.c_str());
  w.Key("blocks");
  w.StartObject();
  std::map<std::string_view, std::vector<const Cell*>> members;
  for (const auto& c : v.cells) {
    members[c.block].push_back(&c);
  }
  for (const auto& [block, hash] : hashes) {
    w.Key(block.c_str());
    w.StartObject();
    w.Key("hash");
    w.String(std::format("{:016x}", hash).c_str());
    w.Key("cells");
    w.StartObject();
    int64_t xl = std::numeric_limits<int64_t>::max(), yl = xl, xh = std::numeric_limits<int64_t>::min(), yh = xh;
    for (const auto* c : members[block]) {
      w.Key(c->name.c_str());
      w.StartArray();
      w.Int64(c->place.x);
      w.Int64(c->place.y);
      w.EndArray();
      xl = std::min(xl, c->place.x);
      yl = std::min(yl, c->place.y);
      xh = std::max(xh, c->place.x + c->place.width);
      yh = std::max(yh, c->place.y + c->place.height);
    }
    w.EndObject();
    w.Key("anchor");
    w.StartArray();
    w.Int64(xl);
    w.Int64(yl);
    w.Int64(xh);
    w.Int64(yh);
    w.EndArray();
    w.EndObject();
  }
  w.EndObject();
  w.Key("terminals");
  w.StartObject();
  for (const auto& t : v.fp.terminals) {
    w.Key(t.name.c_str());
    w.StartArray();
    w.Int64(t.x);
    w.Int64(t.y);
    w.EndArray();
  }
  w.EndObject();
  w.EndObject();
  fs::create_directories(fs::path(path).parent_path());
  const auto temp = path + ".tmp";
  {
    std::ofstream file(temp);
    file << buffer.GetString() << '\n';
    file.close();
    if (!file) {
      throw std::runtime_error("cannot save placement cache");
    }
  }
  fs::rename(temp, path);
}
}  // namespace

void check_legality(const ph::Technology& tech, const ph::Floorplan& fp, double halo_x, double halo_y) {
  if (!std::isfinite(halo_x) || !std::isfinite(halo_y) || halo_x < 0 || halo_y < 0) {
    throw std::invalid_argument("invalid macro halo");
  }
  if (fp.rows.empty() || tech.dbu <= 0) {
    throw std::runtime_error("placement has no rows or invalid units");
  }
  // Raw DEF components carry no dimensions. Resolve them from LEF before
  // checking row coverage and overlap; zero-area rectangles are not evidence
  // of placement legality.
  if (std::any_of(fp.components.begin(), fp.components.end(), [](const auto& c) {
        return c.place.width == 0 || c.place.height == 0;
      })) {
    auto sized = fp;
    for (auto& c : sized.components) {
      if (c.place.width != 0 && c.place.height != 0) {
        continue;
      }
      const auto& master = tech.macros.at(c.macro);
      auto        width = std::llround(master.width * tech.dbu), height = std::llround(master.height * tech.dbu);
      if (width <= 0 || height <= 0) {
        throw std::runtime_error("invalid LEF cell dimensions: " + c.macro);
      }
      if (ph::swaps_axes(c.place.orientation)) {
        std::swap(width, height);
      }
      if (c.place.width == 0) {
        c.place.width = width;
      }
      if (c.place.height == 0) {
        c.place.height = height;
      }
    }
    check_legality(tech, sized, halo_x, halo_y);
    return;
  }
  const bool    has_die = fp.die.xh > fp.die.xl && fp.die.yh > fp.die.yl;
  const auto&   site    = tech.sites.at(fp.rows.front().site);
  const int64_t sw = std::llround(site.width * tech.dbu), rh = std::llround(site.height * tech.dbu);
  if (sw <= 0 || rh <= 0) {
    throw std::runtime_error("placement has invalid site dimensions");
  }
  int64_t                                        xl = std::numeric_limits<int64_t>::max(), yl = xl;
  int64_t                                        xh = std::numeric_limits<int64_t>::min(), yh = xh;
  std::map<int64_t, std::vector<const ph::Row*>> rows_at_y;
  for (const auto& row : fp.rows) {
    const double right = double(row.x) + double(row.count) * row.step;
    const double top   = double(row.y) + rh;
    if (row.site != fp.rows.front().site || row.step != sw || row.count <= 0
        || (has_die && (row.x < fp.die.xl || row.y < fp.die.yl || right > fp.die.xh || top > fp.die.yh))
        || right > std::numeric_limits<int32_t>::max() || top > std::numeric_limits<int32_t>::max()) {
      throw std::runtime_error("invalid physical row: " + row.name);
    }
    xl = std::min(xl, row.x);
    yl = std::min(yl, row.y);
    xh = std::max(xh, int64_t(right));
    yh = std::max(yh, int64_t(top));
    rows_at_y[row.y].push_back(&row);
  }
  for (auto& [y, segments] : rows_at_y) {
    if ((y - yl) % rh != 0) {
      throw std::runtime_error("physical row bands have inconsistent pitch");
    }
    std::sort(segments.begin(), segments.end(), [](auto a, auto b) { return a->x < b->x; });
    for (size_t i = 1; i < segments.size(); ++i) {
      if (segments[i - 1]->x + segments[i - 1]->count * sw > segments[i]->x) {
        throw std::runtime_error("physical row segments overlap");
      }
    }
  }
  if (halo_x > 0 || halo_y > 0) {
    auto       padded = fp;
    const auto x = macro_halo(halo_x, tech.dbu, sw), y = macro_halo(halo_y, tech.dbu, rh);
    for (auto& c : padded.components) {
      if (ph::placement_mask(c, tech) == ph::Placement_mask::none && !tech.macros.at(c.macro).klass.starts_with("CORE")) {
        auto& p = c.place;
        // A fixed macro may touch a core edge. Its physical body must fit,
        // while clearance outside the core has no placeable area to reserve.
        // Check the body before clipping so an out-of-bounds macro cannot pass.
        if (p.x < xl || p.y < yl || p.x + p.width > xh || p.y + p.height > yh) {
          throw std::runtime_error("cell outside core/unplaced: " + c.name);
        }
        const bool fixed = p.status == Status::FIXED || p.status == Status::LOCKED;
        const auto left = p.x - static_cast<int64_t>(x), bottom = p.y - static_cast<int64_t>(y);
        const auto right = p.x + p.width + static_cast<int64_t>(x), top = p.y + p.height + static_cast<int64_t>(y);
        p.x      = fixed ? std::max(xl, left) : left;
        p.y      = fixed ? std::max(yl, bottom) : bottom;
        p.width  = (fixed ? std::min(xh, right) : right) - p.x;
        p.height = (fixed ? std::min(yh, top) : top) - p.y;
      }
    }
    check_legality(tech, padded);
    return;
  }
  const auto                       exclusions = ph::placement_exclusions(fp, {double(xl), double(yl), double(xh), double(yh)});
  std::vector<std::vector<size_t>> by_row((yh - yl + rh - 1) / rh);
  struct Hard_interval {
    ph::Rect rect;
    double   prefix_xh;
  };
  std::vector<std::vector<Hard_interval>> hard_by_row(by_row.size());
  for (const auto& exclusion : exclusions) {
    if (exclusion.kind != ph::Placement_mask::hard) {
      continue;
    }
    const auto& r = exclusion.rect;
    for (size_t row = (r.yl - yl) / rh; row < hard_by_row.size() && yl + static_cast<int64_t>(row) * rh < r.yh; ++row) {
      hard_by_row[row].push_back({r, r.xh});
    }
  }
  for (auto& intervals : hard_by_row) {
    std::sort(intervals.begin(), intervals.end(), [](const auto& a, const auto& b) { return a.rect.xl < b.rect.xl; });
    for (size_t i = 1; i < intervals.size(); ++i) {
      intervals[i].prefix_xh = std::max(intervals[i].rect.xh, intervals[i - 1].prefix_xh);
    }
  }
  // PG rail parity of multi-row cells (pg_rails.hpp), built on first use.
  std::optional<Pg_rail_model> rails;
  for (size_t i = 0; i < fp.components.size(); ++i) {
    const auto& a = fp.components[i];
    if (a.place.status == Status::UNPLACED || a.place.x < xl || a.place.y < yl || a.place.x + a.place.width > xh
        || a.place.y + a.place.height > yh) {
      throw std::runtime_error("cell outside core/unplaced: " + a.name);
    }
    const auto& macro    = tech.macros.at(a.macro);
    const bool  standard = macro.klass.starts_with("CORE");
    if (standard) {
      const auto& p = a.place;
      if ((p.y - yl) % rh || p.height <= 0 || p.height % rh || p.width <= 0) {
        throw std::runtime_error("cell off row/site: " + a.name);
      }
      std::vector<Orientation> spanned;  // orientations of the rows it covers, bottom up
      for (int64_t y = p.y; y < p.y + p.height; y += rh) {
        const auto     band       = rows_at_y.find(y);
        const ph::Row* containing = nullptr;
        if (band != rows_at_y.end()) {
          for (const auto* row : band->second) {
            if (p.x >= row->x && p.x + p.width <= row->x + row->count * sw && (p.x - row->x) % sw == 0) {
              containing = row;
              break;
            }
          }
        }
        if (!containing) {
          throw std::runtime_error("cell outside row segments/off site: " + a.name);
        }
        if (p.height > rh) {
          spanned.push_back(containing->orientation);
        }
        if (y != p.y) {
          continue;
        }
        const auto orientation = containing->orientation;
        const bool mirrored_y  = (orientation == Orientation::N && p.orientation == Orientation::FN)
                                 || (orientation == Orientation::FN && p.orientation == Orientation::N)
                                 || (orientation == Orientation::FS && p.orientation == Orientation::S)
                                 || (orientation == Orientation::S && p.orientation == Orientation::FS);
        if (p.orientation != orientation && !(macro.symmetry_y && mirrored_y)) {
          throw std::runtime_error("cell orientation differs from row: " + a.name);
        }
      }
      if (spanned.size() > 1 && livehd::is_provisional_domino_cell(a.macro)) {
        // Its PG rails must land on rails of the same polarity (OpenROAD's
        // check_placement rule); a provisional domino master with rails is
        // never passed unchecked. A library's own multi-row cells keep the
        // legalizer's placement (their rows are its contract, not ours).
        if (!rails) {
          rails.emplace(tech);
        }
        std::string why;
        const auto  fit = rails->fit(a.macro, p.orientation, spanned, &why);
        if (fit == Rail_fit::mismatch) {
          throw std::runtime_error("multi-row cell PG rails differ from its rows (rail parity): " + a.name + ": " + why);
        }
        const auto& edges = rails->edges(a.macro);
        if (fit == Rail_fit::unknown && std::any_of(edges.begin(), edges.end(), [](Pg_rail r) { return r != Pg_rail::none; })) {
          throw std::runtime_error("cannot check the PG rail parity of multi-row cell " + a.name + ": no one-row CORE cell of site "
                                   + macro.site + " has distinct rails on both edges");
        }
      }
    }
    if (ph::placement_mask(a, tech) == ph::Placement_mask::soft) {
      continue;
    }
    if (a.place.status != Status::FIXED && a.place.status != Status::LOCKED) {
      for (size_t row = (a.place.y - yl) / rh;
           row < hard_by_row.size() && yl + static_cast<int64_t>(row) * rh < a.place.y + a.place.height;
           ++row) {
        const auto& intervals = hard_by_row[row];
        auto        it        = std::lower_bound(intervals.begin(),
                                                 intervals.end(),
                                                 double(a.place.x + a.place.width),
                                                 [](const auto& interval, double x) { return interval.rect.xl < x; });
        while (it != intervals.begin()) {
          --it;
          if (it->prefix_xh <= a.place.x) {
            break;
          }
          const auto& r = it->rect;
          if (a.place.x < r.xh && a.place.y < r.yh && a.place.y + a.place.height > r.yl) {
            throw std::runtime_error("cell overlaps hard placement blockage: " + a.name);
          }
        }
      }
    }
    // Rows below are bucket indices only; macros need not be row-aligned.
    for (int64_t row = (a.place.y - yl) / rh; row * rh + yl < a.place.y + a.place.height; ++row) {
      by_row[row].push_back(i);
    }
  }
  // Two overlapping rectangles share a row band; within a band sorted by x, the
  // left one's span reaches the right one's origin.
  for (auto& members : by_row) {
    std::sort(members.begin(), members.end(), [&](size_t i, size_t j) {
      return fp.components[i].place.x < fp.components[j].place.x;
    });
    for (size_t i = 0; i < members.size(); ++i) {
      const auto& a = fp.components[members[i]];
      for (size_t j = i + 1; j < members.size() && fp.components[members[j]].place.x < a.place.x + a.place.width; ++j) {
        const auto& b = fp.components[members[j]];
        if (a.place.y < b.place.y + b.place.height && b.place.y < a.place.y + a.place.height) {
          throw std::runtime_error("overlapping cells: " + a.name + ", " + b.name);
        }
      }
    }
  }
}

Options final_cell_options(const Options& options) {
  validate_cell_halo(options);
  if (options.gp_buffer_trials < 1 || options.gp_buffer_trials > 16) {
    throw std::invalid_argument("GP buffer trial budget must be between 1 and 16");
  }
  if (!options.cell_halo) {
    return options;  // Unset must preserve every existing staged/default decision.
  }
  auto resolved       = options;
  resolved.cell_stage = true;
  // A placed-context request must not re-enter fresh floorplan or macro optimization.
  if (resolved.cell_halo) {
    resolved.fp_place    = false;
    resolved.final_cells = false;
    resolved.macro_mode  = "none";
  }
  return resolved;
}
void check_placed_legality(const Result& result, const Options& options) {
  auto tech = options.technology().lef;
  if (result.graph) {
    memory_proxies(*result.graph, tech, options.liberty);
    align_memory_proxy_pins(tech, result.floorplan);
  }
  check_legality(tech, result.floorplan, final_cell_options(options));
}
void check_legality(const ph::Technology& tech, const ph::Floorplan& fp, const Options& requested) {
  const auto options = resolve_macro_halo(requested, fp.rows.empty() ? 0 : tech.sites.at(fp.rows.front().site).height);
  check_macro_halo(tech, fp, options);
  const auto [x, y]              = effective_halo(options);
  const auto tap_master          = requested.handoff_taps ? placement_tech_profile(tech, requested).tap_master : std::string{};
  const auto physical_only_fixed = [&](const auto& c) {
    const auto& kind = tech.macros.at(c.macro).klass;
    return (c.place.status == Status::FIXED || c.place.status == Status::LOCKED)
           && (kind == "CORE WELLTAP" || kind == "CORE SPACER" || (!tap_master.empty() && c.macro == tap_master));
  };
  if (std::any_of(fp.components.begin(), fp.components.end(), physical_only_fixed)) {
    // Imported physical-only cells follow the same rule as generated handoff
    // taps and terminal legalization: halos protect logic, while every actual
    // body (including taps/spacers) must remain nonoverlapping.
    auto logical = fp;
    std::erase_if(logical.components, physical_only_fixed);
    check_legality(tech, logical, x, y);
    check_legality(tech, fp, 0, 0);
  } else {
    check_legality(tech, fp, x, y);
  }
}

Steiner_tree steiner_tree(const std::vector<Point>& points) {
  if (points.empty() || points.size() > std::numeric_limits<unsigned>::max() / 4) {
    throw std::invalid_argument("invalid Steiner terminal count");
  }
  initialize_flute_tables();
  Steiner_tree                            result;
  std::map<std::pair<int, int>, unsigned> indices;
  const auto                              vertex = [&](Point p) {
    if (!std::isfinite(p.first) || !std::isfinite(p.second) || std::abs(p.first) > std::numeric_limits<int>::max() / 4.0
        || std::abs(p.second) > std::numeric_limits<int>::max() / 4.0) {
      throw std::invalid_argument("invalid Steiner coordinate");
    }
    const auto key         = std::pair{int(std::llround(p.first)), int(std::llround(p.second))};
    const auto [it, added] = indices.emplace(key, result.points.size());
    if (added) {
      result.points.emplace_back(key.first, key.second);
    }
    return it->second;
  };
  for (const auto& p : points) {
    result.terminals.push_back(vertex(p));
  }
  if (indices.size() == 1) {
    return result;
  }
  if (indices.size() > 1000) {
    // FLUTE has a bounded degree. A deterministic spatial tree keeps large nets
    // repairable without passing an unsupported degree to FLUTE. Median splits
    // bound depth and avoid the quadratic all-pairs search on high-fanout nets.
    std::vector<unsigned> terminals(result.points.size());
    std::iota(terminals.begin(), terminals.end(), 0);
    const auto build = [&](auto&& self, size_t begin, size_t end) -> unsigned {
      if (end - begin == 1) {
        return terminals[begin];
      }
      double xl = std::numeric_limits<double>::infinity(), yl = xl, xh = -xl, yh = -xl;
      for (size_t i = begin; i < end; ++i) {
        const auto p = result.points[terminals[i]];
        xl           = std::min(xl, p.first);
        xh           = std::max(xh, p.first);
        yl           = std::min(yl, p.second);
        yh           = std::max(yh, p.second);
      }
      const bool x   = xh - xl >= yh - yl;
      const auto mid = begin + (end - begin) / 2;
      std::nth_element(terminals.begin() + begin, terminals.begin() + mid, terminals.begin() + end, [&](unsigned a, unsigned b) {
        const auto pa = result.points[a], pb = result.points[b];
        return x ? std::tie(pa.first, pa.second) < std::tie(pb.first, pb.second)
                 : std::tie(pa.second, pa.first) < std::tie(pb.second, pb.first);
      });
      const auto a = self(self, begin, mid), b = self(self, mid, end);
      const auto hub = vertex({(xl + xh) / 2, (yl + yh) / 2});
      if (a != hub) {
        result.edges.emplace_back(a, hub);
      }
      if (b != hub) {
        result.edges.emplace_back(b, hub);
      }
      return hub;
    };
    build(build, 0, terminals.size());
    // Coincident split points can close a cycle. Retain a spanning tree; all
    // original terminals remain connected and preserve their terminal IDs.
    ph::Index_union_find sets(result.points.size());
    std::erase_if(result.edges, [&](const auto& edge) { return !sets.merge(edge.second, edge.first); });
    return result;
  }
  std::vector<int> xs, ys;
  for (const auto& [xy, index] : indices) {
    (void)index;
    xs.push_back(xy.first);
    ys.push_back(xy.second);
  }
  auto                                    tree = flute::flute(xs.size(), xs.data(), ys.data(), 3);
  std::set<std::pair<unsigned, unsigned>> edges;
  for (int i = 0; i < 2 * tree.deg - 2; ++i) {
    const auto a = vertex({tree.branch[i].x, tree.branch[i].y});
    const auto b = vertex({tree.branch[tree.branch[i].n].x, tree.branch[tree.branch[i].n].y});
    if (a != b && edges.emplace(std::min(a, b), std::max(a, b)).second) {
      result.edges.emplace_back(a, b);
    }
  }
  std::free(tree.branch);
  return result;
}
// Area-weighted RMS movement, in density bins, of the movable cells since the
// previous timing checkpoint. Shared by the FP and macro GP checkpoints.
static double checkpoint_movement(const dp::Design& physical, const std::vector<Cell>& cells,
                                  const std::map<std::string, Point>& previous, int bins) {
  double       movement = 0, area = 0;
  const double bin_x = (physical.layout.xh - physical.layout.xl) / bins;
  const double bin_y = (physical.layout.yh - physical.layout.yl) / bins;
  const size_t count = std::min(cells.size(), static_cast<size_t>(std::max(0, physical.num_movable)));
  for (size_t i = 0; i < count; ++i) {
    if (auto old = previous.find(cells[i].name); old != previous.end()) {
      const double cell_area  = physical.node_size_x[i] * physical.node_size_y[i];
      movement               += cell_area
                                * (std::pow((physical.node_x[i] - old->second.first) / bin_x, 2)
                                   + std::pow((physical.node_y[i] - old->second.second) / bin_y, 2));
      area                   += cell_area;
    }
  }
  return area ? std::sqrt(movement / area) : 0;
}
// Movement/budget gate shared by the FP and macro GP timing checkpoints. One
// budgeted update stays reserved for the final coordinates; rapid early motion
// is coalesced so the whole solve, not just its start, is timed.
static bool defer_timing_checkpoint(const Options& options, unsigned done, bool final, int iteration, int max_iterations,
                                    double movement) {
  if (options.fp_timing_movement <= 0 || !done || final) {
    return false;
  }
  const unsigned quota = std::max(1, int(std::ceil(double(options.fp_timing_max_updates - 1) * iteration / max_iterations)));
  return done >= quota || movement < options.fp_timing_movement;
}
static Result place_single_frame(hhds::Graph& source, hhds::GraphLibrary& library, const Options& requested,
                                 const std::function<void()>& release_source, bool& notify_after_stage) {
  std::optional<Options> automatic_options;
  if (requested.halo_x == -1 || requested.halo_y == -1) {
    automatic_options = requested;
    // Resolving geometry must not clone the caller's stateful callbacks.
    if (requested.after_stage) {
      automatic_options->after_stage
          = [&requested](Placement_stage stage, const Result& state) { return requested.after_stage(stage, state); };
    }
    if (requested.fp_evaluate) {
      automatic_options->fp_evaluate = [&requested](const Result& state) { return requested.fp_evaluate(state); };
    }
    if (requested.gp_rank) {
      automatic_options->gp_rank = [&requested](const Ranking_view& state) { return requested.gp_rank(state); };
    }
  }
  const auto&                                     options = automatic_options ? *automatic_options : requested;
  std::shared_ptr<timeclosure::Gp_timing_session> native_session;
  const auto write_legal_acceptance = [&](const timeclosure::Legal_timing_check& check, bool final, bool reference_legal) {
    fs::create_directories(options.output_dir);
    rapidjson::StringBuffer                    buffer;
    rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
    json.StartObject();
    json.Key("reference_legal");
    json.Bool(reference_legal);
    json.Key("final_evaluated");
    json.Bool(final);
    json.Key("characterized_driver_boundary");
    json.Bool(options.native_gp_timing && options.native_gp_timing->driver.characterized_boundary);
    json.Key("bounded_driver_boundary");
    json.Bool(options.native_gp_timing && options.native_gp_timing->driver.bounded_boundary);
    json.Key("native_legalize_displacement_strength");
    json.Double(options.native_gp_timing ? options.native_gp_timing->legalization.displacement : 0);
    json.Key("native_legalize_net_strength");
    json.Double(options.native_gp_timing ? options.native_gp_timing->legalization.net : 0);
    json.Key("native_legalize_timing");
    json.Bool(options.native_gp_timing && options.native_gp_timing->legalize_timing_enabled());
    json.Key("native_timing_recalibrate_on_mismatch");
    json.Bool(options.native_gp_timing && options.native_gp_timing->recalibrate_on_mismatch);
    json.Key("supported_gp_updates");
    json.Uint64(check.supported_gp_updates);
    json.Key("response_interval_complete");
    json.Bool(check.response_interval_complete);
    json.Key("native_nonregression_pass");
    json.Bool(final && check.passed());
    const auto period = [&](const char* key, std::optional<double> value) {
      json.Key(key);
      if (value) {
        json.Double(*value);
      } else {
        json.Null();
      }
    };
    period("reference_period_ps", check.reference_period_ps);
    period("candidate_period_ps", check.candidate_period_ps);
    json.Key("reason");
    json.String(check.reason.c_str());
    json.Key("accepted_for_optimization");
    json.Bool(false);
    json.EndObject();
    std::ofstream report(options.output_dir + "/native_legal_timing.json");
    report << buffer.GetString() << '\n';
    if (!report) {
      throw std::runtime_error("cannot write native legal timing acceptance report");
    }
  };
  if (options.native_gp_buffering) {
    options.native_gp_buffering->validate();
    if (!options.native_gp_timing || !options.virtual_fillers) {
      throw std::invalid_argument("joint native buffering requires native timing and enabled virtual fillers");
    }
#if DREAMPLACE_NATIVE_DENSITY_RESERVATION_API < 2 || DREAMPLACE_NATIVE_PIN_TIMING_API < 3
    throw std::invalid_argument("joint native buffering requires native gap-force and density reservation APIs");
#endif
  }
  if (options.native_gp_timing) {
    options.native_gp_timing->validate();
    if (options.native_gp_timing->legal_output_guard && (!options.full || options.floorplan.empty())) {
      throw std::invalid_argument("native legal output guard requires a complete imported legal floorplan and full legalization");
    }
#if !defined(DREAMPLACE_NATIVE_PIN_TIMING_API)
    throw std::invalid_argument("native GP pin timing requires the DREAMPlace pin-timing API; published dependency lacks it");
#endif
    if (options.threads != 1 || options.fp_evaluate || options.whatif || options.after_stage || options.gp_resize
        || options.fp_resize || options.gp_search.enabled || options.vanished_memory || options.macro_transparency.enabled()
        || options.routability || options.fp_place || options.macro_mode != "none" || !options.cache_dir.empty()
        || options.retain_placement || options.timing_seed || options.cell_jump.depth > 1 || options.iterations <= 0
        || options.output_dir.empty() || !options.optimization_pending || !options.preserve_imported_ties
        || options.legalize_timing_strength > 0 || options.satlegalize) {
      throw std::invalid_argument(
          "native GP pin timing experiment requires single-thread fixed-footprint final GP without legacy timing/repair, cache, "
          "graph/stage callbacks or resynthesis");
    }
  }
  validate_cell_halo(options);
  if (options.gp_observer_interval < 0 || (options.gp_observer && options.gp_observer_interval == 0)) {
    throw std::invalid_argument("GP observation requires a positive interval");
  }
  if (options.gp_observer_rc_verify && !options.gp_observer_rc_reuse) {
    throw std::invalid_argument("RC reuse verification requires checkpoint RC reuse");
  }
  if (options.final_timing_start < 0) {
    throw std::invalid_argument("final placement timing start must be nonnegative");
  }
  if (options.final_timing_schedule != "fixed" && options.final_timing_schedule != "late") {
    throw std::invalid_argument("final timing schedule must be fixed or late");
  }
  if (!std::isfinite(options.gp_buffer_margin) || options.gp_buffer_margin < 0 || options.gp_buffer_margin > 1) {
    throw std::invalid_argument("GP buffer relative slack margin must be finite and in [0,1]");
  }
  if (!std::isfinite(options.eco_connectivity_weight) || options.eco_connectivity_weight < 0
      || options.eco_connectivity_weight > 100) {
    throw std::invalid_argument("ECO connectivity weight must be finite and between 0 and 100");
  }
  if (options.retain_placement && (!options.previous || options.timing_seed || options.fp_place || options.macro_mode != "none")) {
    throw std::invalid_argument("timing snapshot requires an explicit previous placement and no placement stages");
  }
  if (!(options.utilization > 0 && options.utilization < 1) || !(options.aspect > 0 && std::isfinite(options.aspect))
      || !(options.density > 0 && options.density <= 1) || options.iterations < 0 || options.bins < 2 || options.threads < 1
      || (options.route_bins && (options.route_bins < 2 || options.route_bins > 1024)) || options.legalize_bins < 0
      || options.legalize_bins > 1024) {
    throw std::invalid_argument("invalid placement utilization/aspect/density/iteration/bin/thread settings");
  }
  if (!std::isfinite(options.halo_x) || !std::isfinite(options.halo_y) || (options.halo_x < 0 && options.halo_x != -1)
      || (options.halo_y < 0 && options.halo_y != -1)) {
    throw std::invalid_argument("invalid macro halo");
  }
  for (const auto* frame : {&options.die, &options.core}) {
    if (!frame->empty()
        && (frame->size() != 4 || !std::all_of(frame->begin(), frame->end(), [](double x) { return std::isfinite(x) && x >= 0; })
            || (*frame)[2] <= (*frame)[0] || (*frame)[3] <= (*frame)[1])) {
      throw std::invalid_argument("synth.place.die/core expect nonnegative xl,yl,xh,yh microns with xh>xl and yh>yl");
    }
  }
  if (options.macro_mode != "none" && options.macro_mode != "h2d") {
    throw std::invalid_argument("macro placement mode must be none or h2d");
  }
  if (options.legalize_timing_corrections > 2) {
    throw std::invalid_argument("timing correction budget must be 0..2");
  }
  if (options.full && !options.optimization_pending
      && ((options.satlegalize && !options.satlegalize_route) || options.legalize_timing_corrections)) {
    throw std::invalid_argument("terminal legalization replaces local satlegalize/timing corrections");
  }
  if (!std::isfinite(options.legalize_timing_strength) || options.legalize_timing_strength < 0
      || options.legalize_timing_strength > 99 || !std::isfinite(options.legalize_timing_threshold)
      || options.legalize_timing_threshold < 0 || options.legalize_timing_threshold > 1) {
    throw std::invalid_argument("invalid timing legalization strength/threshold");
  }
  if (!std::isfinite(options.timing_max_weight) || options.timing_max_weight < 1 || options.timing_max_weight > max_net_weight
      || !std::isfinite(options.timing_smoothing) || options.timing_smoothing < 0 || options.timing_smoothing > 1
      || options.fp_timing_max_updates < 2 || !std::isfinite(options.timing_exponent) || options.timing_exponent < 1
      || !std::isfinite(options.timing_spread_weight) || options.timing_spread_weight < 0 || options.timing_spread_weight > 1
      || options.macro_timing_interval < 0) {
    throw std::invalid_argument("invalid timing weight/smoothing/update budget");
  }
  validate_uroute_options(options.uroute);
  for (const auto* stage : {&options.fp_route, &options.macro_route, &options.final_route}) {
    route_policy(options, *stage);
  }
  if (!std::isfinite(options.routability_macro_weight) || options.routability_macro_weight < 0
      || !std::isfinite(options.routability_macro_range) || options.routability_macro_range <= 0
      || options.routability_macro_range > 16) {
    throw std::invalid_argument("invalid macro routing pressure controls");
  }
  if (!std::isfinite(options.macro_cell_density_weight) || options.macro_cell_density_weight < 0) {
    throw std::invalid_argument("invalid macro cell density weight");
  }
  if (!std::isfinite(options.local_density_target)
      || (options.local_density_target != -1 && (options.local_density_target < 0 || options.local_density_target > 1))) {
    throw std::invalid_argument("local density target must be -1 (inherit) or in [0,1]");
  }
  if (!std::isfinite(options.macro_cell_density_reach) || options.macro_cell_density_reach <= 0) {
    throw std::invalid_argument("invalid macro cell density reach");
  }
  if (!std::isfinite(options.macro_filler_fraction) || options.macro_filler_fraction < 0 || options.macro_filler_fraction > 1) {
    throw std::invalid_argument("macro filler fraction must be in [0,1]");
  }
  if (options.virtual_filler_max_count < 0 || options.virtual_filler_max_count > 100000000
      || !std::isfinite(options.virtual_filler_cell_scale) || options.virtual_filler_cell_scale <= 0
      || !std::isfinite(options.virtual_filler_bin_fraction) || options.virtual_filler_bin_fraction <= 0
      || options.virtual_filler_bin_fraction > 1 || !std::isfinite(options.virtual_filler_fraction)
      || options.virtual_filler_fraction <= 0 || options.virtual_filler_fraction > 2) {
    throw std::invalid_argument("invalid virtual filler budget");
  }
  if (!std::isfinite(options.macro_density_pull_weight) || options.macro_density_pull_weight < 0
      || options.macro_density_pull_weight > 1 || !std::isfinite(options.macro_density_pull_range)
      || options.macro_density_pull_range <= 0 || options.macro_density_pull_range > 32) {
    throw std::invalid_argument("invalid macro density pull controls");
  }
  if (!std::isfinite(options.stop_overflow) || options.stop_overflow < 0 || options.stop_overflow > 1
      || !std::isfinite(options.initial_density_weight) || options.initial_density_weight <= 0) {
    throw std::invalid_argument("invalid density stopping/initial weight controls");
  }
  if (!std::isfinite(options.fp_timing_movement) || options.fp_timing_movement < 0) {
    throw std::invalid_argument("invalid FP timing movement threshold");
  }
  if (!std::isfinite(options.timing_fraction) || options.timing_fraction <= 0 || options.timing_fraction > 1) {
    throw std::invalid_argument("timing fraction must be in (0, 1]");
  }
  if (!std::isfinite(options.fp_attraction_outside) || options.fp_attraction_outside < 0) {
    throw std::invalid_argument("invalid floorplan outside-circle attraction stiffness");
  }
  if (options.fp_iterations < 0 || options.fp_timing_interval < 0 || options.fp_min_copies < 2 || options.fp_min_macros < 0
      || options.fp_attraction <= 0 || options.fp_repulsion <= 0 || (options.fp_place && options.macro_mode == "h2d")) {
    throw std::invalid_argument("invalid floorplan stage settings or incompatible H2D mode");
  }
  if (options.analyze_colors_only
      && (!options.fp_place || !options.fp_colors || options.fp_color_source == "synthesis" || options.final_cells
          || options.previous || options.timing_seed || options.retain_placement || !options.cache_dir.empty()
          || options.macro_mode != "none")) {
    throw std::invalid_argument("color analysis requires uncached physical FP colors without other placement modes");
  }
  if ((options.fp_color_source != "synthesis" && options.fp_color_source != "placement"
       && options.fp_color_source != "memory_first")
      || !std::isfinite(options.fp_color_minimum) || (options.fp_color_minimum < 0 && options.fp_color_minimum != -1)
      || !std::isfinite(options.fp_color_maximum) || options.fp_color_maximum <= 0 || options.fp_pin_interval < 1
      || !std::isfinite(options.fp_pin_step) || options.fp_pin_step <= 0 || options.fp_pin_step > .5
      || !std::isfinite(options.fp_pin_improvement) || options.fp_pin_improvement < 0 || options.fp_pin_improvement >= 1) {
    throw std::invalid_argument("invalid FP color/pin adjustment controls");
  }
  if (options.macro_pin_interval < 1 || !std::isfinite(options.macro_pin_step) || options.macro_pin_step <= 0
      || options.macro_pin_step > .5 || !std::isfinite(options.macro_pin_improvement) || options.macro_pin_improvement < 0
      || options.macro_pin_improvement >= 1) {
    throw std::invalid_argument("invalid macro pin adjustment controls");
  }
  for (double x : {options.fp_minimum,
                   options.fp_attraction,
                   options.fp_parent_weight,
                   options.fp_parent_outside,
                   options.fp_repulsion,
                   options.fp_anchor,
                   options.fp_snap.alignment_tolerance,
                   options.fp_snap.spacing_tolerance,
                   options.fp_snap.spacing_penalty,
                   options.fp_snap.symmetry_tolerance,
                   options.fp_snap.max_displacement}) {
    if (!std::isfinite(x) || x < 0) {
      throw std::invalid_argument("invalid floorplan coefficient");
    }
  }
  if (options.macro_snap_mode != "alignment" && options.macro_snap_mode != "symmetry") {
    throw std::invalid_argument("macro snap mode must be alignment or symmetry");
  }
  for (double value : {options.macro_snap_options.alignment_tolerance,
                       options.macro_snap_options.spacing_tolerance,
                       options.macro_snap_options.spacing_penalty,
                       options.macro_snap_options.symmetry_tolerance,
                       options.macro_snap_options.max_displacement}) {
    if (!std::isfinite(value) || value < 0) {
      throw std::invalid_argument("invalid macro regularity control");
    }
  }
  if (!std::isfinite(options.fp_align_weight) || options.fp_align_weight < 0 || !std::isfinite(options.fp_align_sigma)
      || options.fp_align_sigma < 0
      || (options.fp_align_weight > 0 && (options.fp_align_start < 0 || options.fp_align_start >= options.fp_align_full))) {
    throw std::invalid_argument("invalid FP centroid alignment controls");
  }
  const bool macro_active = options.macro_mode == "h2d" && !options.timing_seed;
  if (macro_active && !options.macro_legalize_cells) {
    throw std::invalid_argument("macro-only legalization currently requires plain macro placement mode");
  }
  if (macro_active
      && (options.automatic_bins || options.exact_macro_density || options.scale_macro_density || options.adaptive_density
          || options.routability || options.macro_overlap_weight || options.macro_jitter || options.schedule_iterations
          || options.io_iterations)) {
    throw std::invalid_argument("native repulsion experiment controls currently require synth.place.macro=none");
  }
  if (!std::isfinite(options.cohesion) || options.cohesion < 0 || (macro_active && options.cohesion > 0)) {
    throw std::invalid_argument("cohesion requires a nonnegative coefficient and plain placement mode");
  }
  if (macro_active && !options.full) {
    throw std::invalid_argument("H2D requires full placement mode");
  }
  if ((options.macro_align_weight > 0 || options.macro_snap || options.macro_pin_adjust) && macro_active) {
    throw std::invalid_argument("alignment/snap/pin adjustment require plain macro placement mode");
  }
  for (double value :
       {options.macro_align_weight, options.macro_align_sigma, options.macro_align_overlap_boost, options.macro_snap_tolerance}) {
    if (!std::isfinite(value) || value < 0) {
      throw std::invalid_argument("invalid macro alignment/snap coefficient");
    }
  }
  if ((options.macro_align_weight > 0 && (options.macro_align_start < 0 || options.macro_align_full <= options.macro_align_start))
      || options.macro_snap_weight <= 0 || options.macro_snap_weight > 1000000 || (options.macro_snap && !options.full)) {
    throw std::invalid_argument("invalid macro alignment/snap schedule or legalization mode");
  }
  options.macro_parameters.validate();
  if (options.lefs.empty()) {
    throw std::runtime_error("no LEF inputs");
  }
  auto                    tech         = options.technology().lef;
  // Only features that need process policy resolve the technology profile.
  const ph::Tech_profile* tech_profile = nullptr;
  if (options.handoff_taps || options.satlegalize_route_vias || (options.pg != "none" && options.pg != "off")) {
    tech_profile = &placement_tech_profile(tech, options);
  }
  if (options.handoff_taps && !tech.macros.contains(tech_profile->tap_master)) {
    throw std::invalid_argument("handoff taps require the " + tech_profile->name
                                + " tap master in the LEF: " + tech_profile->tap_master);
  }
  if (!options.pg_channels.empty() && (options.pg == "none" || options.pg == "off")) {
    throw std::invalid_argument("PG channel patches require an enabled native power grid");
  }
  if (options.pg_channel_max_ir_drop_v
      && (!std::isfinite(*options.pg_channel_max_ir_drop_v) || *options.pg_channel_max_ir_drop_v < 0)) {
    throw std::invalid_argument("PG channel IR-drop limit must be finite and nonnegative (volts)");
  }
  std::optional<Pg_profile> pg_profile;
  if (options.pg != "none" && options.pg != "off") {
    if (!options.handoff_taps) {
      throw std::invalid_argument("synth.place.pg requires synth.place.handoff.taps=true");
    }
    pg_profile                        = power_grid_profile(*tech_profile);
    pg_profile->channels              = options.pg_channels;
    pg_profile->channel_max_ir_drop_v = options.pg_channel_max_ir_drop_v;
  }
  std::optional<Macro_hierarchy> hierarchy;
  if (macro_active) {
    hierarchy = macro_hierarchy(source, tech, options.liberty);
  }

  std::unordered_set<hhds::Gid> preserved;
  for (const auto& g : source.definitions().graphs()) {
    if (tech.macros.contains(std::string(g->get_name()))) {
      preserved.insert(g->get_gid());
    }
  }
  Result result;
  result.graph = partition::flatten_hierarchy(&source, &library, source.get_name(), nullptr, false, preserved, {}, true);
  if (!result.graph) {
    throw std::runtime_error("cannot flatten mapped netlist for placement");
  }
  auto& graph = *result.graph;
  if (release_source) {
    release_source();  // Nothing below this point may read source or its library.
  }
  remove_transparent_latches(graph);
  View       v;
  const auto weight_roles = prects_library(options.technology().cells, options.control_ports);
  v.prects                = weight_roles;
  v.native_orientations   = macro_active;
  // ECO starts from the supplied physical frame; centroid initialization is a
  // fresh-placement stage and must not overwrite its unchanged neighbors.
  v.centroids             = !options.previous && !options.timing_seed && options.fp_place;
  v.color_centroids       = v.centroids && options.fp_place && options.fp_colors;
  v.placement_colors      = v.color_centroids && options.fp_color_source != "synthesis";
  v.memory_colors         = options.fp_color_source == "memory_first";
  v.color_maximum         = options.fp_color_maximum;
  v.color_minimum         = options.fp_color_minimum < 0 ? options.fp_minimum : options.fp_color_minimum;
  if (v.placement_colors) {
    if (options.liberty.empty()) {
      throw std::invalid_argument("placement colors require Liberty pin-role metadata");
    }
    v.fp_ports = floorplan_ports(options.technology().cells);
  }
  v.centroid_minimum       = options.fp_minimum;
  v.centroid_min_copies    = options.fp_min_copies;
  v.centroid_min_macros    = options.fp_min_macros;
  v.hierarchy_snap         = options.macro_snap;
  v.align_macros           = options.macro_align_weight > 0 || options.macro_snap;
  v.global_macro_alignment = options.macro_align_global && options.macro_align_weight > 0;
  v.weights                = options.net_weights;
  v.fp.design              = std::string(graph.get_name());
  v.fp.dbu                 = tech.dbu;
  const auto memories      = memory_proxies(graph, tech, options.liberty);
  Resolver   resolver(tech, memories, options.unknown_const_zero);
  collect_control_roles(graph, resolver, v.prects);
  result.prects          = v.prects;
  // The flat reset nets are bounded before the first cell is read. The tree's
  // nets keep their wirelength edges (make_arrays), so the buffers sit near
  // their loads, while the reset stays out of repair and criticality.
  // A driver that resolves to a constant (the tied inactive pin of a clear+preset flop) is not a reset net.
  const auto reset_constant = [&](const Pin& driver) {
    try {
      return resolver.bit(driver, 0).constant >= 0;
    } catch (const std::exception&) {
      return true;  // unknown constant: left to the tie logic
    }
  };
  const auto reset_tree
      = options.reset_fanout
            ? buffer_reset_nets(graph, tech, options.technology().cells, options.reset_fanout, reset_constant, options.reset_pins)
            : Reset_tree_stats{};
  const auto rail_tree = options.domino_rail_fanout && !options.domino_rail_pins.empty()
                             ? buffer_pin_nets(graph,
                                               tech,
                                               options.technology().cells,
                                               options.domino_rail_fanout,
                                               options.domino_rail_pins,
                                               "railtree",
                                               reset_constant)
                             : Reset_tree_stats{};
  if (const auto minted = std::max(reset_tree.buffer_um2, rail_tree.buffer_um2); options.growth_budget && minted > 0) {
    // Both trees mint the smallest buffer: only an upsize above it is growth.
    options.growth_budget->tree_master_um2 = minted;
  }
  double      area       = 0;
  int64_t     max_width = 0, max_height = 0;
  std::string site_name;
  for (auto node : graph.body().nodes()) {
    if (auto io = node.get_subnode_io(); io || memories.contains(node.get_debug_nid())) {
      const auto name = io ? std::string(io->get_name()) : memories.at(node.get_debug_nid());
      auto       it   = tech.macros.find(name);
      if (it == tech.macros.end()) {
        throw std::runtime_error("mapped cell/macro absent from LEF: " + name);
      }
      const auto& macro = it->second;
      if (site_name.empty() && !macro.site.empty()) {
        site_name = macro.site;
      }
      auto instance = std::string(node.get_name());
      if (instance.empty()) {
        throw std::runtime_error("mapped physical cell has no stable instance name");
      }
      Ann_place p(0, 0, macro.width * tech.dbu, macro.height * tech.dbu);
      if (auto attr = node.attr(attrs::place); attr.has()) {
        p.x           = attr.get().x;
        p.y           = attr.get().y;
        p.status      = attr.get().status;
        p.orientation = attr.get().orientation;
      }
      if (p.width <= 0 || p.height <= 0) {
        throw std::runtime_error("invalid LEF cell size: " + name);
      }
      const auto dot    = instance.rfind('.');
      const auto prefix = dot == std::string::npos ? std::string{} : instance.substr(0, dot);
      const auto owner  = node.attr(attrs::placement_block);
      const auto block  = owner.has() ? owner.get() : std::format("{}:{}", prefix, gu::node_color_of(node));
      node.attr(attrs::placement_block).set(block);
      if (options.cohesion > 0 || v.align_macros) {
        if (auto group = node.attr(attrs::synth_color_group); group.has()) {
          v.cohesion_owners.emplace(instance, std::string(group.get()));
        }
      }
      v.cells.push_back({node, instance, name, block, p});
      area       += double(p.width) * p.height;
      max_width   = std::max(max_width, p.width);
      max_height  = std::max(max_height, p.height);
    } else if (!Ntype::is_pin_trackable(gu::type_op_of(node)) && gu::type_op_of(node) != Ntype_op::AttrSet) {
      throw std::runtime_error("unmapped physical object: " + std::string(Ntype::get_name(gu::type_op_of(node))));
    }
  }
  if (v.cells.empty()) {
    throw std::runtime_error("placement needs at least one mapped physical cell");
  }
  if (site_name.empty() && !tech.sites.empty()) {
    site_name = tech.sites.begin()->first;
  }
  if (site_name.empty() || !tech.sites.contains(site_name)) {
    throw std::runtime_error("mapped cells have no LEF SITE");
  }
  const auto&   site = tech.sites.at(site_name);
  const int64_t sw = std::llround(site.width * tech.dbu), rh = std::llround(site.height * tech.dbu);
  if (sw <= 0 || rh <= 0) {
    throw std::runtime_error("invalid LEF SITE dimensions");
  }
  std::optional<ph::Floorplan> imported_frame;
  if (!options.floorplan.empty()) {
    imported_frame = ph::read_def(options.floorplan);
  }
  const auto* halo_frame = imported_frame ? &*imported_frame : options.previous.get();
  const auto  row_height
      = halo_frame && !halo_frame->rows.empty() ? tech.sites.at(halo_frame->rows.front().site).height : site.height;
  if (automatic_options) {
    *automatic_options = resolve_macro_halo(options, row_height);
  }
  const auto [cell_halo_x, cell_halo_y] = effective_halo(options);
  const auto bus_endpoint               = [&](Pin pin, int bit) {
    const auto endpoint = resolver.bit(pin, bit);
    if (options.busorder.enabled || options.boundarypins.enabled) {
      if (const auto name = pin.attr(attrs::pin_name); name.has()) {
        if (gu::bits_of(pin) > 1) {
          v.bus_names.try_emplace(endpoint, Bus_bit{std::string(name.get()), bit});
        } else if (const auto identity = indexed_bus_bit(std::string(name.get()))) {
          v.bus_names.try_emplace(endpoint, *identity);
        }
      }
    }
    return endpoint;
  };
  // Build physical connectivity before sizing the frame: replicated ties are
  // real cells and must consume capacity from the first placement iteration.
  for (const auto& decl : graph.get_io()->get_output_pin_decls()) {
    const auto drivers = graph.get_output_pin(decl.name).get_driver_pins();
    if (drivers.size() != 1) {
      throw std::runtime_error("top output needs exactly one driver: " + decl.name);
    }
    for (int bit = 0; bit < std::max(1, int(decl.bits)); ++bit) {
      v.nets[bus_endpoint(drivers.front(), bit)].push_back({"PIN", bit_name(decl.name, std::max(1, int(decl.bits)), bit), -1});
    }
  }
  for (const auto& cell : v.cells) {
    const bool memory = memories.contains(cell.node.get_debug_nid());
    for (auto sink : cell.node.inp_sorted_pins()) {
      if (memory && !memory_runtime_pin(sink.get_port_id())) {
        continue;
      }
      const auto drivers = sink.get_driver_pins();
      if (drivers.size() != 1) {
        throw std::runtime_error("physical input needs exactly one driver: " + cell.name);
      }
      if (memory) {
        for (int b = 0; b < std::max(1, gu::bits_of(drivers.front())); ++b) {
          v.nets[bus_endpoint(drivers.front(), b)].push_back({cell.name, memory_pin(sink.get_port_id(), b, false), -1});
        }
        continue;
      }
      const auto& macro = tech.macros.at(cell.macro);
      const auto  name  = pin_name(sink);
      if (macro.pins.contains(name)) {
        v.nets[bus_endpoint(drivers.front(), 0)].push_back({cell.name, name, -1});
      } else {
        bool found = false;
        for (int b = 0; macro.pins.contains(std::format("{}[{}]", name, b)); ++b) {
          v.nets[bus_endpoint(drivers.front(), b)].push_back({cell.name, std::format("{}[{}]", name, b), -1});
          found = true;
        }
        if (!found) {
          std::string declared;
          if (auto cell_io = cell.node.get_subnode_io()) {
            for (const auto& d : cell_io->get_input_pin_decls()) {
              declared += std::format(" in {}={}", d.name, d.port_id);
            }
            for (const auto& d : cell_io->get_output_pin_decls()) {
              declared += std::format(" out {}={}", d.name, d.port_id);
            }
          }
          throw std::runtime_error(std::format("physical input pin absent from LEF: {}/{} (instance {}, sink port {}, declared{})",
                                               cell.macro,
                                               name,
                                               cell.name,
                                               sink.get_port_id(),
                                               declared));
        }
      }
    }
  }
  std::map<Endpoint, bool> characterized_ties;
  if (!options.liberty.empty()) {
    for (const auto& model : options.technology().cells.constants) {
      v.constant_masters.insert(model.name);
      for (const auto& cell : v.cells) {
        if (cell.macro == model.name) {
          for (const auto& [pin, value] : model.outputs) {
            characterized_ties.emplace(Endpoint{cell.name, pin, -1}, value);
          }
        }
      }
    }
  }
  std::array<std::optional<Tie_cell>, 2> tie_cells;
  if (v.nets.contains(Endpoint{{}, {}, 0}) || v.nets.contains(Endpoint{{}, {}, 1})) {
    tie_cells = find_tie_cells(options, tech, site_name);
  }
  for (const auto& [driver, sinks] : v.nets) {
    if (driver.constant >= 0) {
      const auto& selected = tie_cells[driver.constant];
      if (!selected) {
        throw std::runtime_error(
            std::format("no usable Liberty/LEF constant-{} driver; provide matching synth.liberty and synth.lef cells",
                        driver.constant));
      }
      const auto& master  = tech.macros.at(selected->cell.name);
      area               += sinks.size() * selected->area * tech.dbu * tech.dbu;
      max_width           = std::max(max_width, int64_t(std::llround(master.width * tech.dbu)));
      max_height          = std::max(max_height, int64_t(std::llround(master.height * tech.dbu)));
    } else if (!options.preserve_imported_ties && characterized_ties.contains(driver) && sinks.size() > 1) {
      const auto cell  = std::find_if(v.cells.begin(), v.cells.end(), [&](const Cell& c) { return c.name == driver.instance; });
      area            += (sinks.size() - 1) * double(cell->place.width) * cell->place.height;
    }
  }
  v.halo_x              = macro_halo(cell_halo_x, tech.dbu, sw);
  v.halo_y              = macro_halo(cell_halo_y, tech.dbu, rh);
  double occupancy_area = area;
  for (const auto& c : v.cells) {
    if (!tech.macros.at(c.macro).klass.starts_with("CORE")) {
      max_width  = std::max(max_width, c.place.width + 2 * v.halo_x);
      max_height = std::max(max_height, c.place.height + 2 * v.halo_y);
      occupancy_area
          += double(c.place.width + 2 * v.halo_x) * (c.place.height + 2 * v.halo_y) - double(c.place.width) * c.place.height;
    }
  }
  auto                       resolved_uroute  = resolve_uroute_options(options.uroute);
  const bool                 uroute_requested = uroute_requested_by(options);
  std::optional<Uroute_pool> uroute_data;
  Uroute_selection           selected_uroute;
  if (uroute_requested && uroute_pool_used(options)) {
    uroute_data = uroute_pool_for(options);
  }
  std::string uroute_tech;
  const auto  uroute_id = [&](const View& view) {
    std::string macros;
    for (const auto& cell : view.cells) {
      if (!tech.macros.at(cell.macro).klass.starts_with("CORE")
          && ph::placement_mask({cell.name, cell.macro, cell.place}, tech) == ph::Placement_mask::none) {
        macros += std::format("{}:{}:{}:{};", cell.name, cell.macro, cell.place.width, cell.place.height);
      }
    }
    return Uroute_identity{view.fp.design,
                           uroute_tech,
                           uroute_digest(macros),
                           double(view.fp.die.xh - view.fp.die.xl) / tech.dbu,
                           double(view.fp.die.yh - view.fp.die.yl) / tech.dbu,
                           options.utilization,
                           double(tech.dbu)};
  };
  // A skipped pool reports its reason without building features. view()
  // yields the physical View whose features and identity form the query.
  const auto select_uroute = [&](const auto& view, int bins) {
    if (!uroute_data->skipped().empty()) {
      return uroute_data->select({}, {});
    }
    const View& physical = view();
    return uroute_data->select(dp::uroute_features(physical.design, bins, bins), uroute_id(physical));
  };
  auto env = environment_key(options, tech);
  if (uroute_data) {
    env += ":uroute:" + uroute_data->cache_identity();
  }
  if (options.cohesion > 0 || v.align_macros) {
    uint64_t membership = hash_util::kFnv1a64_offset;
    for (const auto& [name, group] : v.cohesion_owners) {
      membership = hash_text(name, membership);
      membership = hash_text(group, membership);
    }
    env += std::format(":groups:{:016x}", membership);
  }
  const auto cache_path       = (options.timing_seed || options.retain_placement || options.restart_seed || options.handoff_taps
                                 || options.cache_dir.empty())
                                    ? std::string{}
                                    : options.cache_dir + "/placement.json";
  auto       previous         = options.previous;
  bool       cached_reference = false;
  if (!previous && options.floorplan.empty() && !cache_path.empty() && fs::is_regular_file(cache_path)
      && fs::is_regular_file(options.cache_dir + "/floorplan.def")) {
    rapidjson::Document cached;
    cached.Parse(file_text(cache_path).c_str());
    if (!cached.HasParseError() && cached.IsObject() && cached.HasMember("environment") && cached["environment"].IsString()
        && cached["environment"].GetString() == env) {
      try {
        auto frame  = ph::read_def(options.cache_dir + "/floorplan.def");
        auto solver = frame;
        prepare_rows(tech, solver);
        for (auto& c : frame.components) {
          const auto master = tech.macros.find(c.macro);
          if (master != tech.macros.end()) {
            c.place.width  = std::llround(master->second.width * tech.dbu);
            c.place.height = std::llround(master->second.height * tech.dbu);
          }
        }
        const auto width = solver.rows.front().count * sw, height = static_cast<int64_t>(solver.rows.size()) * rh;
        if (frame.design == graph.get_name() && area <= double(width) * height && max_width <= width
            && max_height <= static_cast<int64_t>(height)) {
          previous         = std::make_shared<ph::Floorplan>(std::move(frame));
          cached_reference = true;
        }
      } catch (const std::exception&) { /* A stale frame never blocks a fresh placement. */
      }
    }
  }
  // Rows this run may still regenerate: fresh ones, or a private cached frame
  // (a cache must never make a placement fail that a cold run would accept).
  const bool generated_rows = options.floorplan.empty() && (!previous || cached_reference);
  if (options.floorplan.empty() && previous) {
    v.fp = *previous;
  } else if (options.floorplan.empty()) {
    int64_t columns = 0, rows = 0, origin_x = 0, origin_y = 0;
    if (options.die.empty() && options.core.empty()) {
      // A large halo can consume the complete minimum-width frame. Reserve
      // density capacity for padded macros as well as the real standard cells.
      const double core_area = std::max(area / options.utilization, occupancy_area / options.density);
      columns                = std::max<int64_t>(std::ceil(std::sqrt(core_area * options.aspect) / sw), (max_width + sw - 1) / sw);
      rows = std::max<int64_t>({2, static_cast<int64_t>(std::ceil(core_area / (columns * sw * rh))), (max_height + rh - 1) / rh});
      v.fp.die = {0, 0, double(columns * sw), double(rows * rh)};
      if (pg_profile) {
        // Keep the same usable core area, but reserve a site/row of die margin
        // for physical edge rails and via enclosures. Explicit frames are never
        // resized and must supply their own inset.
        origin_x     = sw;
        origin_y     = rh;
        v.fp.die.xh += 2 * sw;
        v.fp.die.yh += 2 * rh;
      }
    } else {
      // A benchmark-fixed frame (ORFS DIE_AREA/CORE_AREA, microns). The core snaps inward to the
      // site/row grid like OpenROAD's initialize_floorplan; utilization and aspect do not apply.
      const auto& die_um  = options.die.empty() ? options.core : options.die;
      const auto& core_um = options.core.empty() ? options.die : options.core;
      const auto  dbu     = [&](double um) { return um * tech.dbu; };
      v.fp.die = {std::round(dbu(die_um[0])), std::round(dbu(die_um[1])), std::round(dbu(die_um[2])), std::round(dbu(die_um[3]))};
      const auto lo_x = static_cast<int64_t>(std::ceil(dbu(core_um[0]) / sw - 1e-9));
      const auto lo_y = static_cast<int64_t>(std::ceil(dbu(core_um[1]) / rh - 1e-9));
      const auto hi_x = static_cast<int64_t>(std::floor(dbu(core_um[2]) / sw + 1e-9));
      const auto hi_y = static_cast<int64_t>(std::floor(dbu(core_um[3]) / rh + 1e-9));
      origin_x        = lo_x * sw;
      origin_y        = lo_y * rh;
      columns         = hi_x - lo_x;
      rows            = hi_y - lo_y;
      if (columns < 1 || rows < 1) {
        throw std::runtime_error("synth.place.core holds no complete site/row");
      }
      if (origin_x < v.fp.die.xl || origin_y < v.fp.die.yl || (origin_x + columns * sw) > v.fp.die.xh
          || (origin_y + rows * rh) > v.fp.die.yh) {
        throw std::runtime_error("synth.place.core must lie inside synth.place.die");
      }
      if (area > double(columns * sw) * double(rows * rh)) {
        throw std::runtime_error(std::format("fixed core {} x {} um holds {:.1f} um2 of cells in {:.1f} um2",
                                             double(columns * sw) / tech.dbu,
                                             double(rows * rh) / tech.dbu,
                                             area / tech.dbu / tech.dbu,
                                             double(columns * sw) * double(rows * rh) / tech.dbu / tech.dbu));
      }
      if (max_width > columns * sw || max_height > rows * rh) {
        throw std::runtime_error("a mapped cell or macro halo does not fit in the fixed core");
      }
    }
    // Alternating N/FS rows require X-symmetric cells, as in sky130HD.
    const bool flip
        = std::all_of(v.cells.begin(), v.cells.end(), [&](const auto& c) { return tech.macros.at(c.macro).symmetry_x; });
    for (int64_t row = 0; row < rows; ++row) {
      v.fp.rows.push_back({"ROW_" + std::to_string(row),
                           site_name,
                           origin_x,
                           origin_y + row * rh,
                           columns,
                           sw,
                           flip && row % 2 ? Orientation::FS : Orientation::N});
    }
  } else {
    v.fp = *imported_frame;
    if (v.fp.design != graph.get_name()) {
      throw std::runtime_error("floorplan DEF DESIGN differs from selected top");
    }
    std::unordered_map<std::string_view, Cell*> by_name;
    for (auto& c : v.cells) {
      by_name.emplace(c.name, &c);
    }
    for (const auto& imported : v.fp.components) {
      const auto it = by_name.find(imported.name);
      if (it == by_name.end()) {
        if (imported.place.status == Status::FIXED) {
          throw std::runtime_error("fixed DEF component absent from mapped netlist: " + imported.name);
        }
        continue;
      }
      auto& c = *it->second;
      if (imported.macro != c.macro) {
        throw std::runtime_error("floorplan cell master mismatch: " + c.name);
      }
      if (imported.place.status == Status::FIXED) {
        auto p   = imported.place;
        p.width  = c.place.width;
        p.height = c.place.height;
        c.place  = p;
      }
    }
  }
  v.original_row_frame = prepare_rows(tech, v.fp);
  if ((options.macro_transparency.enabled() || options.vanished_memory)
      && std::any_of(v.fp.blockages.begin(), v.fp.blockages.end(), [](const auto& b) { return !b.soft; })) {
    throw std::invalid_argument("hard DEF placement blockages require opaque fixed macros until mask opacity is independent");
  }
  if (previous) {
    std::map<std::string, const ph::Component*> old;
    for (const auto& c : previous->components) {
      old.emplace(c.name, &c);
    }
    for (auto& c : v.cells) {
      if (const auto it = old.find(c.name); it != old.end()) {
        const auto source_orientation = c.place.orientation;
        c.place.x                     = it->second->place.x;
        c.place.y                     = it->second->place.y;
        c.place.orientation           = it->second->place.orientation;
        if (c.macro == it->second->macro) {
          // Persisted H2D FIXED positions are a cache frame, not new user
          // constraints. Restore them through the macro cache after matching
          // the original movable-macro fingerprint.
          const bool cached_macro = macro_active && cached_reference && !tech.macros.at(c.macro).klass.starts_with("CORE")
                                    && c.place.status != Status::FIXED && c.place.status != Status::LOCKED;
          c.place.status          = cached_macro ? Status::PLACED : it->second->place.status;
          if (cached_macro) {
            // The solved orientation changes pin geometry in the fingerprint.
            // Reuse must compare the original movable-macro inputs first.
            c.place.orientation = source_orientation;
          }
        } else {
          c.place.status = Status::PLACED;
        }
      } else if (c.place.status == Status::UNPLACED && !options.changed_cells.empty()) {
        for (const auto& name : options.changed_cells) {
          if (const auto anchor = old.find(name); anchor != old.end()) {
            c.place.x      = anchor->second->place.x;
            c.place.y      = anchor->second->place.y;
            c.place.status = Status::PLACED;
            break;
          }
        }
      }
    }
  }
  if (!options.tracks_file.empty()) {
    apply_routing_tracks(tech, v.fp, file_text(options.tracks_file));
  }
  align_memory_proxy_pins(tech, v.fp);
  // Pin alignment determines the generated LEF contents. Use those exact
  // bytes for both inference/export identity and the later routed handoff.
  const auto proxy_lef = memory_proxy_lef(memories, tech);
  if (uroute_requested || resolved_uroute.export_features) {
    uroute_tech = uroute_technology(options.lefs, proxy_lef);
  }
  std::unordered_map<std::string, ph::Terminal> imported_terms;
  for (auto& t : v.fp.terminals) {
    imported_terms.emplace(t.name, std::move(t));
  }
  v.fp.terminals.clear();
  v.fp.components.clear();
  if (options.busorder.enabled || options.boundarypins.enabled) {
    for (const auto& net : v.fp.nets) {
      if (const auto identity = indexed_bus_bit(net.name)) {
        for (const auto& pin : net.connections) {
          v.bus_names[{pin.instance, pin.pin, -1}] = *identity;
        }
      }
    }
  }
  v.fp.nets.clear();
  std::map<std::string, Pin>      top_pins;
  // Terminals whose position came from the floorplan/previous frame; only the
  // rest get the default spread (and, on the ECO path, a slot from assign_io).
  std::unordered_set<std::string> imported_pos;
  auto add_terminal = [&](Pin p, const std::string& name, const std::string& direction, const std::string& bus, int bit) {
    ph::Terminal t;
    t.name      = name;
    t.direction = direction;
    t.status    = Status::PLACED;
    t.x         = std::llround((v.fp.die.xl + v.fp.die.xh) / 2);
    t.y         = std::llround(v.fp.die.yl);
    auto layer  = std::find_if(tech.routing_layers.begin(), tech.routing_layers.end(), [&](const auto& layer_name) {
      return tech.layers.at(layer_name).direction == "HORIZONTAL";
    });
    if (layer == tech.routing_layers.end()) {
      throw std::runtime_error("LEF has no horizontal routing layer for IO pins");
    }
    const double half = std::max(1.0, std::round(tech.layers.at(*layer).width * tech.dbu / 2));
    t.shapes.push_back({
        *layer,
        {-half, -half, half, half}
    });
    if (const auto it = imported_terms.find(name); it != imported_terms.end()) {
      const bool adjustable = (options.fp_place && options.fp_pin_adjust) || options.macro_pin_adjust;
      if (it->second.status != Status::FIXED && !previous && !adjustable) {
        throw std::runtime_error("floorplan PINS must be FIXED unless IO adjustment is enabled: " + name);
      }
      if (it->second.status != Status::UNPLACED) {
        t           = it->second;
        t.direction = direction;
        imported_pos.insert(name);
      }
      // UNPLACED declares a port, not a position or an explicit constraint.
      // Keep the generated legal-layer shape and default perimeter seed; the
      // initial spread/IO assignment must treat it like any new movable pin.
    }
    top_pins.emplace(name, p);
    v.fp.terminals.push_back(std::move(t));
    v.io_buses.push_back(bus);
    v.io_bits.push_back(bit);
  };
  auto io = graph.get_io();
  for (const auto& d : io->get_input_pin_decls()) {
    const auto p = graph.get_input_pin(d.name);
    for (int b = 0; b < std::max(1, static_cast<int>(d.bits)); ++b) {
      add_terminal(p, bit_name(d.name, std::max(1, static_cast<int>(d.bits)), b), "INPUT", d.name, b);
    }
  }
  for (const auto& d : io->get_output_pin_decls()) {
    const auto p       = graph.get_output_pin(d.name);
    const auto drivers = p.get_driver_pins();
    if (drivers.size() != 1) {
      throw std::runtime_error("top output needs exactly one driver: " + d.name);
    }
    for (int b = 0; b < std::max(1, static_cast<int>(d.bits)); ++b) {
      auto name = bit_name(d.name, std::max(1, static_cast<int>(d.bits)), b);
      add_terminal(p, name, "OUTPUT", d.name, b);
    }
  }
  // Start with distributed side pins so the first GP does not pull every
  // cell toward one die edge. The subsequent Hungarian assignment may move
  // these pins to any available edge slot.
  for (const auto* direction : {"INPUT", "OUTPUT"}) {
    size_t count = 0, index = 0;
    for (const auto& terminal : v.fp.terminals) {
      if (terminal.direction == direction && terminal.status != Status::FIXED && !imported_pos.contains(terminal.name)) {
        ++count;
      }
    }
    for (auto& terminal : v.fp.terminals) {
      if (terminal.direction == direction && terminal.status != Status::FIXED && !imported_pos.contains(terminal.name)) {
        terminal.x = std::llround(std::string_view(direction) == "INPUT" ? v.fp.die.xl : v.fp.die.xh);
        terminal.y = std::llround(v.fp.die.yl + (++index) * (v.fp.die.yh - v.fp.die.yl) / (count + 1));
      }
    }
  }
  // Constants remain logical constants in the graph. Instantiate real tie cells
  // for the physical view and graph so DEF/Verilog/STA share the same netlist.
  if (v.nets.contains(Endpoint{{}, {}, 0}) || v.nets.contains(Endpoint{{}, {}, 1})) {
    // The generated N/FS decision above only saw the mapped cells. If a needed
    // tie cannot flip (no X-symmetric candidate exists), generated rows fall
    // back to all-N; imported or caller-supplied previous FS rows cannot use it.
    const bool fs_rows
        = std::any_of(v.fp.rows.begin(), v.fp.rows.end(), [](const auto& r) { return r.orientation == Orientation::FS; });
    for (int value = 0; value < 2 && fs_rows; ++value) {
      if (tie_cells[value] && tie_cells[value]->flipless && v.nets.contains(Endpoint{{}, {}, value})) {
        if (generated_rows) {
          for (auto& row : v.fp.rows) {
            row.orientation = Orientation::N;
          }
          if (v.original_row_frame) {
            // Private cached rows may change orientation with the generated
            // solve. Explicit imported/previous rows never enter this branch.
            auto frame = std::make_shared<ph::Floorplan>(*v.original_row_frame);
            for (auto& row : frame->rows) {
              row.orientation = Orientation::N;
            }
            v.original_row_frame = std::move(frame);
          }
          // A cached frame already seeded its CORE cells with the old FS flips.
          for (auto& c : v.cells) {
            (void)apply_row_orientation(c, tech, Orientation::N);
          }
        } else {
          tie_cells[value].reset();
        }
      }
    }
  }
  // Each physical pin/bit owns its driver. Keep an imported single-load tie
  // (identified by its Liberty function), and split shared ties before GP
  // unless a fixed-netlist replay explicitly preserves their topology.
  // Additional names depend on the user, so refresh and cache rounds cannot
  // accidentally merge local sources back into a global constant net.
  std::set<Endpoint> preserved_ties;
  if (options.preserve_imported_ties) {
    for (const auto& [driver, sinks] : v.nets) {
      if (characterized_ties.contains(driver) && sinks.size() > 1) {
        preserved_ties.insert(driver);
      }
    }
  }
  std::map<Endpoint, Pin>                   tie_drivers;
  std::map<Endpoint, std::vector<Endpoint>> constant_nets;
  for (const auto& [driver, sinks] : v.nets) {
    if (driver.constant >= 0 || (characterized_ties.contains(driver) && !preserved_ties.contains(driver))) {
      constant_nets.emplace(driver, sinks);
    }
  }
  for (const auto& [constant_driver, sinks] : constant_nets) {
    const bool  imported = constant_driver.constant < 0;
    const int   value    = imported ? int(characterized_ties.at(constant_driver)) : constant_driver.constant;
    std::string master_name, output;
    if (imported) {
      const auto cell
          = std::find_if(v.cells.begin(), v.cells.end(), [&](const Cell& c) { return c.name == constant_driver.instance; });
      master_name = cell->macro;
      output      = constant_driver.pin;
    } else {
      if (!tie_cells[value]) {
        throw std::runtime_error(
            std::format("no usable Liberty/LEF constant-{} driver (flipped FS rows need a SYMMETRY X master)", value));
      }
      master_name = tie_cells[value]->cell.name;
      output      = tie_cells[value]->output;
    }
    auto tie_io = library.find_io(master_name);
    if (!tie_io) {
      tie_io = library.create_io(master_name);
    }
    if (!tie_io->get_input_pin_decls().empty()) {
      throw std::runtime_error("constant cell declaration has inputs: " + master_name);
    }
    if (!imported) {
      hhds::Port_id next = 0;
      for (const auto& decl : tie_io->get_output_pin_decls()) {
        next = std::max(next, static_cast<hhds::Port_id>(decl.port_id + 1));
        if (decl.bits != 1
            || std::none_of(tie_cells[value]->cell.outputs.begin(), tie_cells[value]->cell.outputs.end(), [&](const auto& pin) {
                 return pin.first == decl.name;
               })) {
          throw std::runtime_error("incompatible constant cell declaration: " + master_name);
        }
      }
      for (const auto& [name, polarity] : tie_cells[value]->cell.outputs) {
        (void)polarity;
        const auto& decls = tie_io->get_output_pin_decls();
        if (std::none_of(decls.begin(), decls.end(), [&](const auto& decl) { return decl.name == name; })) {
          tie_io->add_output(name, next++);
          tie_io->set_bits(name, 1);
          tie_io->set_unsign(name, true);
        }
      }
    }
    v.nets.erase(constant_driver);
    for (size_t i = 0; i < sinks.size(); ++i) {
      const auto& target = sinks[i];
      const auto  base   = imported ? constant_driver.instance : std::string(value ? "__place_tiehi" : "__place_tielo");
      const auto  name = imported && i == 0 ? base : std::format("{}_{:016x}", base, hash_text(target.instance + ":" + target.pin));
      auto        existing = std::find_if(v.cells.begin(), v.cells.end(), [&](const Cell& c) { return c.name == name; });
      Node        node;
      if (existing != v.cells.end()) {
        if (existing->macro != master_name) {
          throw std::runtime_error("tie cell master mismatch: " + name);
        }
        node = existing->node;
      } else {
        node = graph.create_node();
        gu::set_type_op(node, Ntype_op::Sub);
        node.set_subnode(tie_io);
        node.set_name(name);
        const auto& master = tech.macros.at(master_name);
        Ann_place   place(0, 0, std::llround(master.width * tech.dbu), std::llround(master.height * tech.dbu));
        if (previous) {
          for (const auto& old : previous->components) {
            if (old.name == name && old.macro == master_name) {
              place = old.place;
            }
          }
        }
        v.cells.push_back({node, name, master_name, "__ties", place});
      }
      auto driver = node.create_driver_pin(output);
      gu::set_bits(driver, 1);
      tie_drivers.emplace(target, driver);
      v.nets[{name, output, -1}].push_back(target);
    }
  }
  // Materialize tie sources inside bus aliases too, including memory ports.
  // Timing, DEF and the saved logical graph must see the same bit connectivity.
  for (const auto& c : v.cells) {
    for (auto sink : c.node.inp_pins_snapshot()) {
      const bool memory = memories.contains(c.node.get_debug_nid());
      if (memory && !memory_runtime_pin(sink.get_port_id())) {
        continue;
      }
      const auto drivers = sink.get_driver_pins();
      if (drivers.size() != 1) {
        continue;
      }
      const auto original = drivers.front();
      int        width    = std::max(1, gu::bits_of(original));
      bool       scalar   = false;
      if (!memory) {
        const auto  name  = pin_name(sink);
        const auto& macro = tech.macros.at(c.macro);
        if (macro.pins.contains(name)) {
          width  = 1;
          scalar = true;
        } else {
          width = 0;
          while (macro.pins.contains(std::format("{}[{}]", name, width))) {
            ++width;
          }
        }
      }
      // Name sink bits as the net builder does: a LEF bus with only D[0] keeps
      // its index even though the bus is one bit wide.
      const auto tie_sink_pin = [&](int b) {
        return memory   ? memory_pin(sink.get_port_id(), b, false)
               : scalar ? pin_name(sink)
                        : std::format("{}[{}]", pin_name(sink), b);
      };
      std::vector<bool> constants;
      bool              changed = false;
      for (int b = 0; b < width; ++b) {
        const auto endpoint = resolver.bit(original, b);
        const bool constant
            = endpoint.constant >= 0 || (characterized_ties.contains(endpoint) && !preserved_ties.contains(endpoint));
        constants.push_back(constant);
        if (constant) {
          const auto physical_pin  = tie_sink_pin(b);
          const auto driver        = tie_drivers.at(Endpoint{c.name, physical_pin, -1});
          changed                 |= endpoint != Endpoint{std::string(driver.get_master_node().get_name()), pin_name(driver), -1};
        }
      }
      if (!changed) {
        continue;
      }
      std::vector<Pin> values;
      for (int b = 0; b < width; ++b) {
        if (constants[b]) {
          values.push_back(tie_drivers.at(Endpoint{c.name, tie_sink_pin(b), -1}));
        } else {
          values.push_back(width == 1 ? original : gu::create_get_mask(graph, original, b).get_driver_pin(0));
        }
      }
      gu::drop_drivers(sink);
      if (width == 1) {
        sink.connect_driver(values.front());
      } else {
        auto       concat = gu::create_typed_node(graph, Ntype_op::Concat);
        const auto one    = gu::create_const(graph, *Dlop::create_integer(1));
        for (int lane = 0; lane < width; ++lane) {
          concat.create_sink_pin(2 * lane).connect_driver(values[width - 1 - lane]);
          concat.create_sink_pin(2 * lane + 1).connect_driver(one);
        }
        auto out = concat.create_driver_pin(0);
        gu::set_bits(out, width);
        gu::set_unsign(out);
        sink.connect_driver(out);
      }
    }
  }
  // Top-level constant bits need the same physical tie sources as cell inputs.
  // Otherwise DEF/RC names a tie while STA connects that output to a const net.
  for (const auto& decl : graph.get_io()->get_output_pin_decls()) {
    auto       target  = graph.get_output_pin(decl.name);
    const auto drivers = target.get_driver_pins();
    if (drivers.size() != 1) {
      throw std::runtime_error("top output needs exactly one driver: " + std::string(decl.name));
    }
    const auto        original = drivers.front();
    const int         width    = std::max(1, int(decl.bits));
    std::vector<bool> constants;
    bool              changed = false;
    for (int b = 0; b < width; ++b) {
      const auto endpoint = resolver.bit(original, b);
      const bool constant = endpoint.constant >= 0 || (characterized_ties.contains(endpoint) && !preserved_ties.contains(endpoint));
      constants.push_back(constant);
      if (constant) {
        const auto driver  = tie_drivers.at(Endpoint{"PIN", bit_name(decl.name, width, b), -1});
        changed           |= endpoint != Endpoint{std::string(driver.get_master_node().get_name()), pin_name(driver), -1};
      }
    }
    if (!changed) {
      continue;
    }
    std::vector<Pin> values;
    for (int b = 0; b < width; ++b) {
      if (constants[b]) {
        values.push_back(tie_drivers.at(Endpoint{"PIN", bit_name(decl.name, width, b), -1}));
      } else {
        values.push_back(width == 1 ? original : gu::create_get_mask(graph, original, b).get_driver_pin(0));
      }
    }
    gu::drop_drivers(target);
    if (width == 1) {
      target.connect_driver(values.front());
    } else {
      auto       concat = gu::create_typed_node(graph, Ntype_op::Concat);
      const auto one    = gu::create_const(graph, *Dlop::create_integer(1));
      for (int lane = 0; lane < width; ++lane) {
        concat.create_sink_pin(2 * lane).connect_driver(values[width - 1 - lane]);
        concat.create_sink_pin(2 * lane + 1).connect_driver(one);
      }
      auto out = concat.create_driver_pin(0);
      gu::set_bits(out, width);
      gu::set_unsign(out);
      target.connect_driver(out);
    }
  }
  // A saved DEF includes the physical tie cells materialized above. Validate
  // the complete inventory only now, so re-importing the original mapped graph
  // does not mistake those generated cells for extra netlist components.
  if (options.retain_placement) {
    std::map<std::string, const ph::Component*> old;
    for (const auto& c : previous->components) {
      old.emplace(c.name, &c);
    }
    if (old.size() != v.cells.size()) {
      throw std::runtime_error("timing snapshot component count differs from netlist");
    }
    for (const auto& c : v.cells) {
      const auto it = old.find(c.name);
      if (it == old.end() || it->second->macro != c.macro || it->second->place.status == Status::UNPLACED
          || c.place.status == Status::UNPLACED) {
        throw std::runtime_error("timing snapshot requires every mapped cell placed with its original master: " + c.name);
      }
    }
  }
  for (auto& c : v.cells) {
    if (c.place.status == Status::UNPLACED
        || (v.centroids && !c.locked && c.place.status != Status::FIXED && c.place.status != Status::LOCKED)) {
      const auto  h   = hash_text(c.name);
      const auto& row = v.fp.rows[(h >> 32) % v.fp.rows.size()];
      c.place.x       = row.x + static_cast<int64_t>(h % std::max<int64_t>(1, row.count - c.place.width / sw + 1)) * sw;
      c.place.y       = row.y;
    }
  }
  if (options.cell_stage && options.cell_halo && !options.timing_seed && !options.retain_placement) {
    auto frame = v.fp;
    for (const auto& c : v.cells) {
      if (!tech.macros.at(c.macro).klass.starts_with("CORE")) {
        frame.components.push_back({c.name, c.macro, c.place});
      }
    }
    check_macro_halo(tech, frame, options);
  }
  std::optional<Macro_run>  macro_run;
  dp::Design                macro_report_design;
  std::vector<mp::NodeInfo> macro_report_info;
  bool                      macro_solved     = false;
  size_t                    macro_seeded_ios = 0;  // restart-seed IOs kept before macro planning
  if (macro_active) {
    // A restart seed's IOs (same die only) are kept as the regular flow keeps
    // them, and before macro planning: the macro frame's identity includes
    // every IO, so a reassigned IO would discard a reusable frame.
    std::unordered_set<std::string> seeded_ios;
    if (options.restart_seed && v.fp.die == options.restart_seed->die) {
      std::map<std::string, const ph::Terminal*> old;
      for (const auto& t : options.restart_seed->terminals) {
        old.emplace(t.name, &t);
      }
      for (auto& t : v.fp.terminals) {
        const auto it = old.find(t.name);
        if (it != old.end() && t.status != Status::FIXED && t.status != Status::LOCKED && !imported_pos.contains(t.name)) {
          t.x = it->second->x;
          t.y = it->second->y;
          seeded_ios.insert(t.name);
        }
      }
      macro_seeded_ios = seeded_ios.size();
    }
    make_arrays(v, tech);
    // Cold automatic IO assignment uses the same first GP as the regular flow.
    // Warm floorplan/cache/seed IOs are already placed and avoid this prototype call.
    if (std::any_of(v.fp.terminals.begin(), v.fp.terminals.end(), [&](const auto& t) {
          return t.status != Status::FIXED && !imported_pos.contains(t.name) && !seeded_ios.contains(t.name);
        })) {
      dp::Options prototype;
      prototype.max_iterations = options.iterations;
      prototype.bins_x = prototype.bins_y = options.bins;
      prototype.num_threads               = options.threads;
      prototype.target_density            = options.density;
      prototype.add_fillers               = false;
      prototype.legalize = prototype.detailed_place = false;
      dp::place(v.design, prototype);
      capture(v, tech, false);
      std::vector<Status> statuses;
      for (auto& t : v.fp.terminals) {
        statuses.push_back(t.status);
        if (imported_pos.contains(t.name) || seeded_ios.contains(t.name)) {
          t.status = Status::FIXED;
        }
      }
      assign_io(v, tech);
      for (size_t i = 0; i < statuses.size(); ++i) {
        v.fp.terminals[i].status = statuses[i];
      }
      make_arrays(v, tech);
    }
    Options resolved          = options;
    resolved.uroute           = resolved_uroute;
    resolved.macro_parameters = macro_parameters(options, tech);
    std::map<std::string, const mp::Hierarchy::Instance*> origin;
    for (const auto& i : hierarchy->hierarchy.instances) {
      origin[i.name] = &i;
    }
    for (const auto& c : v.cells) {
      const auto& m      = tech.macros.at(c.macro);
      const auto  dot    = c.name.rfind('.');
      std::string parent = dot == std::string::npos ? std::string{} : c.name.substr(0, dot);
      bool        stable = true;
      if (auto it = origin.find(c.name); it != origin.end()) {
        parent = it->second->parent;
        stable = it->second->stable;
      }
      unsigned mask = 1;
      if (m.symmetry_y) {
        mask |= 1U << unsigned(dp::Orientation::FN);
      }
      if (m.symmetry_x) {
        mask |= 1U << unsigned(dp::Orientation::FS);
      }
      if (m.symmetry_x && m.symmetry_y) {
        mask |= 1U << unsigned(dp::Orientation::S);
      }
      macro_report_info.push_back({c.name,
                                   parent,
                                   c.macro,
                                   !m.klass.starts_with("CORE"),
                                   hierarchy->sequential_masters.contains(c.macro),
                                   false,
                                   stable,
                                   mask});
    }
    for (const auto& t : v.fp.terminals) {
      macro_report_info.push_back({"@io:" + t.name, {}, {}, false, false, true});
    }
    std::string identity
        = env + ":" + mp::parameter_key(resolved.macro_parameters) + ":" + mp::fingerprint(v.design, macro_report_info);
    if (options.macro_topology) {
      identity += ":" + mp::topology_key(*options.macro_topology);
    }
    for (const auto& t : v.fp.terminals) {
      identity += std::format(":{}:{},{}", t.name, t.x, t.y);
    }
    resolved.uroute_stage_identity = uroute_id(v);
    if (uroute_data && options.macro_route.policy.probe && options.macro_route.policy.probe_model == dp::RouteModel::uroute) {
      resolved.uroute_selection  = select_uroute([&]() -> const View& { return v; }, resolved.macro_parameters.bins);
      const auto& selected       = resolved.uroute_selection;
      identity                  += ":uroute-selection:" + selected.cache_identity + ":" + selected.skipped;
    }
    macro_run              = place_macros(v.design, macro_report_info, *hierarchy, resolved, identity);
    macro_solved           = !macro_run->cache_hit;
    const bool track_moved = align_macro_geometry(v, tech, v.design, true, false);
    macro_report_design    = v.design;
    for (size_t i = 0; i < v.cells.size(); ++i) {
      if (macro_report_info[i].macro) {
        auto& c   = v.cells[i];
        c.place.x = std::llround(v.design.node_x[i]);
        c.place.y = std::llround(v.design.node_y[i]);
        if (!tech.macros.at(c.macro).klass.starts_with("CORE") && !v.design.node_orientation.empty()) {
          c.place.orientation = physical_orientation(v.design.node_orientation[i]);
        }
        c.place.orientation
            = physical_orientation(v.design.node_orientation.empty() ? dp::Orientation::N : v.design.node_orientation[i]);
        c.place.status = Status::FIXED;
      }
    }
    capture(v, tech, macro_solved);
    if (track_moved) {
      dp::Options rows;
      rows.max_iterations = 0;
      rows.add_fillers    = false;
      rows.bins_x = rows.bins_y                                        = options.bins;
      rows.num_threads                                                 = options.threads;
      const auto repaired                                              = dp::place(v.design, rows);
      macro_run->result.placement.alignment.cell_legalization_seconds += repaired.alignment.cell_legalization_seconds;
      macro_run->result.placement.detailed_placement_seconds          += repaired.detailed_placement_seconds;
      capture(v, tech, true);
    }
  }
  // Square proxy macros are packed first and become obstacles for DREAMPlace.
  // Their exact square dimensions need not be a multiple of the standard site.
  std::vector<Cell*> macros;
  for (auto& c : v.cells) {
    if (!macro_active && memories.contains(c.node.get_debug_nid())) {
      macros.push_back(&c);
    }
  }
  std::sort(macros.begin(), macros.end(), [](const auto* a, const auto* b) {
    return a->place.width != b->place.width ? a->place.width > b->place.width : a->name < b->name;
  });
  std::unordered_set<Cell*> packed_memories;
  for (auto* macro : macros) {
    if (macro->place.status == Status::FIXED || macro->place.status == Status::LOCKED) {
      continue;
    }
    bool        found = false;
    const auto& first = v.fp.rows.front();
    for (int64_t y = first.y + v.halo_y; !found && y + macro->place.height + v.halo_y <= v.fp.rows.back().y + rh; y += rh) {
      for (int64_t x = first.x + v.halo_x; !found && x + macro->place.width + v.halo_x <= first.x + first.count * sw; x += sw) {
        bool overlap = false;
        for (const auto& c : v.cells) {
          const bool standard = tech.macros.at(c.macro).klass.starts_with("CORE");
          const auto cx = standard ? 0 : v.halo_x, cy = standard ? 0 : v.halo_y;
          if (&c != macro && (c.place.status == Status::FIXED || c.place.status == Status::LOCKED)
              && x - v.halo_x < c.place.x + c.place.width + cx && x + macro->place.width + v.halo_x > c.place.x - cx
              && y - v.halo_y < c.place.y + c.place.height + cy && y + macro->place.height + v.halo_y > c.place.y - cy) {
            overlap = true;
            break;
          }
        }
        if (!overlap) {
          macro->place.x           = x;
          macro->place.y           = y;
          macro->place.orientation = Orientation::N;
          macro->place.status      = Status::FIXED;
          packed_memories.insert(macro);
          found = true;
        }
      }
    }
    if (!found) {
      throw std::runtime_error("no space for square memory proxy: " + macro->name);
    }
  }
  if (!packed_memories.empty()) {
    std::vector<ph::Component> frame;
    std::vector<Cell*>         owners;
    for (auto& c : v.cells) {
      // The packed proxies move; fixed geometry is their obstacle. A movable
      // LEF macro (a fakeram) is neither: global placement places it later
      // and aligns it like any macro, so it must not be validated here as if
      // fixed at its unplaced origin.
      if (!packed_memories.contains(&c) && c.place.status != Status::FIXED && c.place.status != Status::LOCKED) {
        continue;
      }
      auto p   = c.place;
      p.status = packed_memories.contains(&c) ? Status::PLACED : Status::FIXED;
      frame.push_back({c.name, c.macro, p});
      owners.push_back(&c);
    }
    // These newly packed proxies have no user-fixed origin yet. Correct their
    // routing phase before they become obstacles for standard-cell packing.
    legalize_macro_tracks(tech, v.fp, frame, v.halo_x, v.halo_y, true);
    for (size_t i = 0; i < frame.size(); ++i) {
      if (packed_memories.contains(owners[i])) {
        owners[i]->place.x = frame[i].place.x;
        owners[i]->place.y = frame[i].place.y;
      }
    }
  }
  std::sort(v.cells.begin(), v.cells.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
  if (uroute_data) {
    // Select from a physical pre-cache snapshot. Both the immutable pool digest
    // and the actual selected IDs are part of the placement cache identity.
    selected_uroute = select_uroute(
        [&] {
          auto snapshot = v;
          make_arrays(snapshot, tech);
          return snapshot;
        },
        route_grid_bins(auxiliary_route_bins(options), options.bins));
    env += ":uroute-selection:" + selected_uroute.cache_identity + ":" + selected_uroute.skipped;
  }
  const auto hashes = block_hashes(v);
  // H2D already legalized the complete cell frame after changing the macro
  // topology. Old block anchors must not overwrite that solved placement.
  // Experimental FP targets are not persisted by the placement cache yet.
  auto       cache
      = (macro_solved || options.fp_place) ? CacheInfo{0, hashes.size(), false} : restore_cache(v, tech, cache_path, env, hashes);
  if (options.restart_seed) {
    // This is an initial guess after resynthesis, not an ECO lock. In
    // particular, never copy the old status or create position boxes. A fresh
    // H2D solve already legalized the complete cell frame and is kept as the
    // final placement below; like the placement cache, the seed must not
    // overwrite it (old coordinates under H2D's row orientations are illegal).
    // A macro solve keeps the seed's IOs it took before planning.
    size_t reused_cells = 0, reused_macros = 0, new_cells = 0, reused_ios = macro_solved ? macro_seeded_ios : 0;
    if (!macro_solved) {
      std::map<std::string, Ann_place> positions;
      for (const auto& c : options.restart_seed->components) {
        positions.emplace(c.name, c.place);
      }
      for (auto& c : v.cells) {
        const auto old = positions.find(c.name);
        if (c.place.status == Status::FIXED || c.place.status == Status::LOCKED) {
          continue;
        }
        if (old != positions.end()) {
          c.place.x = old->second.x;
          c.place.y = old->second.y;
          ++reused_cells;
          reused_macros += !tech.macros.at(c.macro).klass.starts_with("CORE");
        } else {
          ++new_cells;
        }
      }
      for (auto& terminal : v.fp.terminals) {
        for (const auto& old : options.restart_seed->terminals) {
          // A resynthesis restart can resize the generated die. Keep the fresh
          // perimeter seeds in that case; old edge coordinates may now be interior.
          if (v.fp.die == options.restart_seed->die && terminal.name == old.name && terminal.status != Status::FIXED
              && terminal.status != Status::LOCKED) {
            terminal.x = old.x;
            terminal.y = old.y;
            ++reused_ios;
            break;
          }
        }
      }
    }
    fs::create_directories(options.output_dir);
    std::ofstream report(options.output_dir + "/restart_seed.json");
    report << std::format(
        R"({{"coordinates_only":true,"superseded_by_macro_solve":{},"reused_movable_cells":{},"reused_movable_macros":{},"new_movable_cells":{},"reused_ios":{}}})",
        macro_solved,
        reused_cells,
        reused_macros,
        new_cells,
        reused_ios)
           << '\n';
    if (!report) {
      throw std::runtime_error("cannot write placement restart seed report");
    }
  }
  make_arrays(v, tech);
  dp::Options control;
  double      feature_capacity_h = 0, feature_capacity_v = 0;
  control.max_iterations  = options.iterations;
  control.centroid_weight = v.centroids ? .25 : 0;
  // Fail early only where the final track check will run; timing seeds and
  // retained placements do not certify legality.
  if (options.full && !options.timing_seed && !options.retain_placement && !options.analyze_colors_only) {
    align_macro_geometry(v, tech, v.design, false, false);
  }
  control.after_macro_legalize = [&](dp::Design& physical) { align_macro_geometry(v, tech, physical, true, false); };
  control.bins_x = control.bins_y    = options.bins;
  control.num_threads                = options.threads;
  control.dynamic_bins               = options.dynamic_bins;
  control.bin_max                    = options.bin_max;
  control.auxiliary_bins             = auxiliary_route_bins(options);
  control.local_density_weight       = options.local_density_weight;
  control.local_density_target       = options.local_density_target;
  control.timing_spread_weight       = options.timing_spread_weight;
  control.local_density_bins         = options.local_density_bins;
  control.local_density_range        = options.local_density_range;
  control.local_density_jitter       = options.local_density_jitter;
  control.macro_cell_density_reach   = options.macro_cell_density_reach;
  control.macro_cell_density_weight  = std::any_of(v.design.node_macro.begin(),
                                                   v.design.node_macro.begin() + v.design.num_movable,
                                                   [](auto macro) { return macro != 0; })
                                           ? options.macro_cell_density_weight
                                           : 0;
  control.macro_density_pull_weight  = std::any_of(v.design.node_macro.begin(),
                                                   v.design.node_macro.begin() + v.design.num_movable,
                                                   [](auto macro) { return macro != 0; })
                                           ? options.macro_density_pull_weight
                                           : 0;
  control.macro_density_pull_range   = options.macro_density_pull_range;
  control.target_density             = options.density;
  control.stop_overflow              = options.stop_overflow;
  control.initial_density_weight     = options.initial_density_weight;
  control.density_weight_seed        = options.density_weight_seed;
  // Internal density particles never consume legal sites or
  // escape to the physical graph/DEF. The supplied outline stays authoritative.
  control.add_fillers                = options.virtual_fillers;
  control.filler_max_count           = options.virtual_filler_max_count;
  control.filler_area_fraction       = options.virtual_filler_fraction;
  control.filler_cell_scale          = options.virtual_filler_cell_scale;
  control.filler_bin_fraction        = options.virtual_filler_bin_fraction;
  control.density_upper_bound        = options.density_upper_bound;
  control.min_iterations             = options.min_iterations;
  control.post_density_factor        = options.post_density_factor;
  control.convergence_tolerance      = options.convergence_tolerance;
  control.detailed_iterations        = options.detailed_iterations;
  control.fixed_macro_overlap_weight = options.fixed_macro_overlap_weight;
  control.wirelength_gamma_end       = options.wirelength_gamma_end;
  control.wirelength_gamma_start     = options.warm_refinement ? options.wirelength_gamma_end : 1;
  control.cohesion_weight            = options.cohesion / tech.dbu;
  // The default alignment ramp only applies when movable macros can pair.
  // Cell-only/fixed-macro runs must not wait for an inactive macro ramp.
  bool          has_alignment_pair   = false;
  std::set<int> alignment_classes;
  for (int i = 0; i < v.design.num_movable; ++i) {
    if (!v.design.node_macro.empty() && v.design.node_macro[i] && !v.design.node_align_class.empty()
        && v.design.node_align_class[i] >= 0 && !alignment_classes.insert(v.design.node_align_class[i]).second) {
      has_alignment_pair = true;
      break;
    }
  }
  control.macro_align_weight        = (has_alignment_pair || options.macro_magnet.enabled) ? options.macro_align_weight : 0;
  control.macro_align_sigma         = options.macro_align_sigma;
  control.macro_align_start         = options.macro_align_start;
  control.macro_align_full          = options.macro_align_full;
  control.macro_align_overlap_boost = options.macro_align_overlap_boost;
  control.macro_align_nearest       = options.macro_align_nearest;
  control.macro_align_global        = options.macro_align_global;
  control.magnet                    = options.macro_magnet;
  if (options.macro_magnet.enabled) {
    control.macro_merge_check
        = [&, check = make_rigid_macro_track_checker(tech, v.fp)](const std::vector<dp::MagnetMacroFrame>& members) mutable {
            std::vector<ph::Component> proposal;
            proposal.reserve(members.size());
            for (const auto& member : members) {
              const auto& c     = v.cells.at(member.source);
              auto        place = c.place;
              place.x           = std::llround(member.x);
              place.y           = std::llround(member.y);
              place.width       = std::llround(member.width);
              place.height      = std::llround(member.height);
              place.orientation = physical_orientation(member.orientation);
              proposal.push_back({c.name, c.macro, place});
            }
            return check(proposal) ? std::string{} : std::string{"routing_track_phase"};
          };
  }
  control.macro_snap           = options.macro_snap;
  control.macro_snap_regular   = options.macro_snap && options.macro_snap_mode == "symmetry";
  control.macro_snap_options   = options.macro_snap_options;
  control.macro_snap_tolerance = options.macro_snap_tolerance;
  control.macro_snap_weight    = options.macro_snap_weight;
  control.automatic_bins       = options.automatic_bins;
  control.scale_macro_density  = options.scale_macro_density;
  control.exact_macro_density  = options.exact_macro_density;
  control.exact_cell_density   = options.exact_cell_density;
  control.adaptive_density     = options.adaptive_density;
  control.macro_overlap_weight = options.macro_overlap_weight;
  control.macro_jitter         = options.macro_jitter;
  control.schedule_iterations  = options.schedule_iterations;
  control.routability = control.improved_routability = options.routability;
  control.route
      = route_policy(options, options.completed_stage == Placement_stage::macro ? options.macro_route : options.final_route);
  control.route_initialize = initialize_flute_tables;
  selected_uroute.apply(control);
  if (control.route.model == dp::RouteModel::off) {
    control.routability = control.improved_routability = false;
  }

  control.routability_charge_compensation = options.routability_charge_compensation;
  control.routability_macro_weight        = options.routability_macro_weight;
  control.routability_macro_range         = options.routability_macro_range;
  const bool route_capacity_required
      = options.routability
        && (control.routability || (options.fp_place && route_policy(options, options.fp_route).model != dp::RouteModel::off));
  if (resolved_uroute.export_features || route_capacity_required) {
    std::tie(feature_capacity_h, feature_capacity_v) = lef_track_capacity(tech);
    if (route_capacity_required && !(feature_capacity_h > 0 && feature_capacity_v > 0)) {
      throw std::runtime_error("routability requires horizontal and vertical LEF routing pitches");
    }
    // Feature export retains missing physical capacity as zero. Inactive
    // routing and timing observers keep the native defaults when the LEF
    // cannot supply both directions.
    if (feature_capacity_h > 0 && feature_capacity_v > 0) {
      control.route_capacity_h = feature_capacity_h;
      control.route_capacity_v = feature_capacity_v;
    }
  }
  // Probe snapshots export with the final feature capacities and the current
  // stage identity.
  const auto probe_exporter = [&](std::string_view stage) {
    return uroute_probe_exporter(
        fs::path(options.output_dir) / "uroute_export" / stage,
        [&] { return uroute_id(v); },
        feature_capacity_h,
        feature_capacity_v);
  };
  if (resolved_uroute.export_features && control.route.probe) {
    control.route_probe_observer
        = probe_exporter(options.completed_stage == Placement_stage::macro ? "macro_probe" : "final_probe");
  }
  control.legalize       = false;
  control.detailed_place = false;
  std::shared_ptr<std::ofstream> progress;
  if (options.snapshots) {
    fs::create_directories(options.output_dir);
    progress                  = std::make_shared<std::ofstream>(options.output_dir + "/gp_history.jsonl");
    control.progress_observer = [progress, dbu = tech.dbu](const auto& s) {
      auto line = std::format(
          "{{\"iteration\":{},\"overflow\":{},\"raw_overflow\":{},\"hpwl_um\":{},\"density_weight\":{},\"route_overflow\":{},"
          "\"inflation_area_ratio\":{},\"model_density\":{},\"filler_charge_scale\":{},\"macro_overlap_pairs\":{},\"macro_stack_"
          "pairs\":{},\"align_pairs\":{},\"align_residual\":{},\"align_ramp\":{}",
          s.iteration,
          s.overflow,
          s.raw_overflow,
          s.hpwl / dbu,
          s.density_weight,
          s.route_overflow,
          s.inflation_area_ratio,
          s.model_density,
          s.filler_charge_scale,
          s.macro_overlaps,
          s.macro_stacks,
          s.align_pairs,
          s.align_residual,
          s.align_ramp);
      line += std::format(",\"overflow_floor\":{}", s.overflow_floor);
      if constexpr (requires {
                      s.filler_count;
                      s.filler_area;
                      s.filler_size_x;
                      s.filler_size_y;
                    }) {
        line += std::format(
            ",\"filler_count\":{},\"filler_area_um2\":{},\"filler_effective_area_um2\":{},\"filler_size_x_um\":{},\"filler_size_y_"
            "um\":{}",
            s.filler_count,
            s.filler_area / dbu / dbu,
            s.filler_area * s.filler_charge_scale / dbu / dbu,
            s.filler_size_x / dbu,
            s.filler_size_y / dbu);
      } else {
        // Older pinned placers do not expose per-sample filler statistics.
        // Keep absent measurements distinct from measured zero, while sibling
        // co-development retains all of its richer diagnostic fields.
        line += ",\"filler_count\":null,\"filler_area_um2\":null,\"filler_effective_area_um2\":null,\"filler_size_x_um\":null,"
                "\"filler_size_y_um\":null";
      }
      line += std::format(",\"route_peak_utilization\":{},\"macro_routing_force\":{},\"macro_routing_channels\":{}",
                          s.route_peak_utilization,
                          s.macro_routing_force,
                          s.macro_routing_channels);
      line += std::format(",\"macro_opacity\":{},\"macro_exclusion_scale\":{},\"local_density_weight\":{}",
                          s.macro_opacity,
                          s.macro_exclusion_scale,
                          s.local_density_weight);
      line += std::format(
          R"(,"magnet_round":{},"magnet_groups":{},"macro_move_max":{},"macro_move_rms":{},"last_instability_iteration":{})",
          s.magnet_round,
          s.magnet_groups,
          s.macro_move_max,
          s.macro_move_rms,
          s.last_instability_iteration);
      line += std::format(
          ",\"route_model\":\"{}\",\"route_sampled\":{},\"route_seconds\":{},\"route_bins\":{},\"route_switch_iteration\":{},"
          "\"route_switch_rudy\":{},\"route_switch_rsmt\":{},\"route_probe\":{}",
          dp::route_model_name(s.route_model),
          s.route_sampled,
          s.route_seconds,
          s.route_bins,
          s.route_switch_iteration,
          s.route_switch_rudy,
          s.route_switch_rsmt,
          s.route_probe);
      line += std::format(
          ",\"route_blocked_pins\":{},\"route_unroutable_nets\":{},\"route_calls\":{},\"route_rudy_seconds\":{},\"route_rsmt_"
          "seconds\":{}",
          s.route_blocked_pins,
          s.route_unroutable_nets,
          s.route_calls,
          s.route_rudy_seconds,
          s.route_rsmt_seconds);
      line += std::format(
          ",\"route_uroute_seconds\":{},\"route_uroute_switch_iteration\":{},\"route_switch_uroute_rsmt\":{},\"route_switch_"
          "uroute\":{},\"route_probe_overflow\":{}",
          s.route_uroute_seconds,
          s.route_uroute_switch_iteration,
          s.route_switch_uroute_rsmt,
          s.route_switch_uroute,
          s.route_probe_overflow);
      line += std::format(",\"align_force\":{}", s.align_force);
      if (s.centroid_repulsion_weight > 0) {
        line += std::format(",\"centroid_overlap_pairs\":{},\"centroid_max_overlap\":{},\"centroid_repulsion_weight\":{}",
                            s.centroid_overlaps,
                            s.centroid_max_overlap,
                            s.centroid_repulsion_weight);
      }
      *progress << line << "}\n";
      progress->flush();
    };
  }
  std::vector<CentroidSample> centroid_initial, centroid_gp;
  dp::Result                  metrics, fp_metrics;
  // Diagnostics describe this solve only, including when a reused output
  // directory previously had warpspace enabled and now has it disabled.
  fs::remove(options.output_dir + "/macro_warp_stability.jsonl");
  std::optional<dp::MagnetReport>               macro_replay;
  std::shared_ptr<dp::detail::MagnetRuntime>    macro_replay_motion;
  std::optional<Satlegalize_report>             sat_report;
  size_t                                        sat_retained = 0;
  std::optional<dp::FloorplanSnap>              fp_snap;
  std::vector<std::pair<int, Pin_adjust_stats>> fp_pin_history, macro_pin_history;
  bool                                          movable_macros = false;
  for (int i = 0; i < v.design.num_movable; ++i) {
    movable_macros |= !v.design.node_macro.empty() && v.design.node_macro[i];
  }
  if (options.macro_magnet.enabled && !movable_macros) {
    throw std::invalid_argument("macro magnet requires movable macros");
  }
  if (options.handoff_taps && !options.timing_seed && !options.retain_placement && options.completed_stage == Placement_stage::final
      && (!options.full || options.optimization_pending || options.previous || macro_active)) {
    throw std::invalid_argument("handoff taps require fresh final terminal legalization with fixed macros");
  }
  const bool terminal_active   = options.full && !options.optimization_pending && !options.fp_place
                                 && options.completed_stage == Placement_stage::final && !options.timing_seed
                                 && !options.retain_placement && !options.analyze_colors_only;
  // Optional centroid FP still feeds this macro stage. It must not enable
  // cell legalization or discard the selected GP timing used by resynthesis.
  const bool defer_macro_cells = options.full && !options.macro_legalize_cells && options.completed_stage == Placement_stage::macro
                                 && !options.timing_seed && !options.retain_placement && !options.analyze_colors_only;
  if (terminal_active && movable_macros) {
    throw std::invalid_argument("terminal legalization requires the final cell stage with fixed macros");
  }
  const bool bus_active = options.busorder.enabled && options.full && !options.fp_place
                          && options.completed_stage == Placement_stage::final && !options.timing_seed && !options.retain_placement
                          && !options.analyze_colors_only;
  const bool boundary_active   = options.boundarypins.enabled && options.full && !options.fp_place
                                 && options.completed_stage == Placement_stage::final && !options.timing_seed
                                 && !options.retain_placement && !options.analyze_colors_only;
  const bool refinement_active = bus_active || boundary_active;
  if (refinement_active && movable_macros) {
    throw std::invalid_argument("final boundary refinement requires fixed macros");
  }
  // Movable two-row CORE cells (provisional domino masters) are packed only by
  // the anchor prepass of final terminal legalization (multirow.hpp). The macro
  // stage, native and SAT legalization, the region refinements and ECO
  // re-placement assume single-row cells, so they refuse them. Only the
  // reserved domino masters opt in: a library multi-row cell keeps the
  // single-row paths (and their failures) unchanged.
  size_t multirow_cells = 0;
  for (const auto& cell : v.cells) {
    multirow_cells += !cell.locked && cell.place.status != Ann_place::Status::FIXED
                      && cell.place.status != Ann_place::Status::LOCKED && livehd::is_provisional_domino_cell(cell.macro)
                      && is_multirow_core(tech, tech.macros.at(cell.macro));
  }
  if (multirow_cells) {
    const auto refuse = [&](std::string_view what) {
      throw std::invalid_argument(
          std::format("{} movable two-row cell(s) need final terminal legalization: {}", multirow_cells, what));
    };
    if (macro_active) {
      refuse("the macro placement stage is not supported");
    }
    if (options.full && !terminal_active && !options.timing_seed && !options.retain_placement && !options.analyze_colors_only) {
      refuse("native legalization outside the final stage is not supported");
    }
    if (options.satlegalize) {
      refuse("satlegalize treats multi-height cells as immutable");
    }
    if (options.busorder.enabled || options.boundarypins.enabled || options.boundarypins.align) {
      refuse("the detail-final region mesh (busorder, boundarypins) needs single-row cells");
    }
    if (!options.multirow_eco) {
      if (options.previous || !options.changed_cells.empty() || options.legalize_cells) {
        refuse("ECO legalization is single-row");
      }
    } else if (!options.previous || !options.legalize_cells) {
      throw std::invalid_argument("the two-row ECO requires a previous placement and nearest-whitespace legalization");
    }
    // A changed design would re-place around a cached reference through the
    // same single-row ECO legalization. The cache must never fail a placement
    // a cold run accepts, so these designs place cold: unlocked, every movable
    // cell unflipped (terminal legalization orients it by its row again).
    if (cached_reference && !cache.hit) {
      previous.reset();
      cache = CacheInfo{0, hashes.size(), false};
      for (auto& c : v.cells) {
        c.locked = false;
        if (c.place.status != Ann_place::Status::FIXED && c.place.status != Ann_place::Status::LOCKED) {
          c.place.orientation = Orientation::N;
        }
      }
      make_arrays(v, tech);
    }
  }
  std::optional<Busorder>      bus_order;
  std::optional<Boundary_pins> boundary_pins;
  const auto                   refine_final_boundaries = [&](dp::Design& physical) {
    std::vector<int> bus_macros;
    for (size_t i = 0; i < v.cells.size(); ++i) {
      if (!tech.macros.at(v.cells[i].macro).klass.starts_with("CORE") && !v.cells[i].name.starts_with("__lhd_frame_mask_")) {
        bus_macros.push_back(i);
      }
    }
    std::vector<Region_io> ios;
    for (const auto& t : v.fp.terminals) {
      ios.push_back({t.name, double(t.x), double(t.y)});
    }
    std::vector<Bus_bit> identities;
    for (const auto& [driver, sinks] : v.nets) {
      Bus_bit    identity;
      const auto named = [&](const Endpoint& endpoint) {
        const auto it = v.bus_names.find(endpoint);
        if (it == v.bus_names.end()) {
          return false;
        }
        identity = it->second;
        return true;
      };
      if (!named(driver)) {
        for (const auto& sink : sinks) {
          if (named(sink)) {
            break;
          }
        }
      }
      const auto fallback = [&](const Endpoint& endpoint) {
        if (identity.bit >= 0) {
          return;
        }
        if (endpoint.instance != "PIN") {
          const auto cell = v.cell.find(endpoint.instance);
          if (cell == v.cell.end() || tech.macros.at(v.cells[cell->second].macro).klass.starts_with("CORE")) {
            return;
          }
        }
        if (auto bit = indexed_bus_bit(endpoint.pin)) {
          if (endpoint.instance != "PIN") {
            bit->bus = endpoint.instance + "/" + bit->bus;
          }
          identity = *bit;
        }
      };
      fallback(driver);
      for (const auto& sink : sinks) {
        fallback(sink);
      }
      identities.push_back(std::move(identity));
    }
    const auto router = [](const std::vector<Busorder_point>& points) {
      const auto    tree = steiner_tree(points);
      Busorder_tree segments;
      for (auto [a, b] : tree.edges) {
        segments.emplace_back(tree.points[a], tree.points[b]);
      }
      return segments;
    };
    const auto regions = Region_mesh::build(physical, bus_macros, ios, options.region, tech.dbu);
    regions.write(options.output_dir);
    if (boundary_active) {
      bus_order.emplace(physical,
                        regions,
                        identities,
                        router,
                        tech.dbu,
                        options.busorder,
                        options.output_dir.empty() ? "" : options.output_dir + "/boundarypins/rsmt_order");
      bus_order->observe(physical, "before");
      boundary_pins.emplace(regions,
                            router,
                            tech.dbu,
                            options.boundarypins,
                            options.output_dir.empty() ? "" : options.output_dir + "/boundarypins",
                            identities,
                            bus_macros);
      boundary_pins->refine(physical, control);
      bus_order->observe(physical, "after_gp");
    } else {
      bus_order.emplace(physical,
                        regions,
                        identities,
                        router,
                        tech.dbu,
                        options.busorder,
                        options.output_dir.empty() ? "" : options.output_dir + "/busorder");
      bus_order->refine(physical, control);
    }
    if (control.observer) {
      control.observer(physical, dp::PlacementStage::global_placement, control.max_iterations);
    }
  };
  std::optional<Terminal_legalize_report> terminal_report;
  std::optional<Multirow_report>          multirow_report;
  std::optional<Multirow_eco_report>      multirow_eco_report;
  std::pair<unsigned, unsigned>           multirow_eco_grid;  // its terminal bin grid
  std::vector<ph::Component>              handoff_cells;
  std::vector<ph::Row>                    handoff_rows;
  const bool macro_pin_adjust = options.macro_pin_adjust && movable_macros && !options.previous && !options.timing_seed;
  const bool adjustable_io
      = v.centroids && options.fp_pin_adjust
        && std::any_of(v.fp.terminals.begin(), v.fp.terminals.end(), [](const auto& t) { return t.status != Status::FIXED; });
  double io_pitch = 1;
  if ((v.centroids && options.fp_pin_adjust) || macro_pin_adjust) {
    for (const auto& t : v.fp.terminals) {
      for (const auto& shape : t.shapes) {
        const auto&  layer  = tech.layers.at(shape.layer);
        const double extent = std::max(shape.rect.xh - shape.rect.xl, shape.rect.yh - shape.rect.yl);
        io_pitch            = std::max(io_pitch, std::ceil(extent + std::max(layer.pitch_x, layer.pitch_y) * tech.dbu));
      }
    }
  }
  const auto adjust_pins = [&](dp::Design& d, int iteration, bool initialize, bool macro) {
    const auto stats   = adjust_fp_pins(d,
                                        v.fp,
                                        v.io_buses,
                                        v.io_bits,
                                        io_pitch,
                                        macro ? options.macro_pin_step : options.fp_pin_step,
                                        macro ? options.macro_pin_improvement : options.fp_pin_improvement,
                                        initialize);
    auto&      history = macro ? macro_pin_history : fp_pin_history;
    const auto stage   = macro ? "macro" : "fp";
    const bool first   = history.empty();
    history.emplace_back(iteration, stats);
    fs::create_directories(options.output_dir);
    std::ofstream           trace(options.output_dir + "/" + stage + "_pins.jsonl", first ? std::ios::trunc : std::ios::app);
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
    json.StartObject();
    json.Key("stage");
    json.String(stage);
    json.Key("iteration");
    json.Int(iteration);
    json.Key("initialize");
    json.Bool(initialize);
    json.Key("moved");
    json.Uint(stats.moved);
    json.Key("reversals");
    json.Uint(stats.reversals);
    json.Key("travel_dbu");
    json.Double(stats.travel);
    json.Key("weighted_hpwl_before_dbu");
    json.Double(stats.before);
    json.Key("weighted_hpwl_after_dbu");
    json.Double(stats.after);
    json.Key("pins");
    json.StartArray();
    for (size_t i = 0; i < v.fp.terminals.size(); ++i) {
      const auto& t = v.fp.terminals[i];
      json.StartObject();
      json.Key("name");
      json.String(t.name.c_str());
      json.Key("bus");
      json.String(v.io_buses[i].c_str());
      json.Key("bit");
      json.Int(v.io_bits[i]);
      json.Key("fixed");
      json.Bool(t.status == Status::FIXED);
      json.Key("x");
      json.Int64(t.x);
      json.Key("y");
      json.Int64(t.y);
      json.EndObject();
    }
    json.EndArray();
    json.EndObject();
    if (!(trace << buffer.GetString() << '\n')) {
      throw std::runtime_error("cannot write IO movement report");
    }
  };
  const auto pin_updater = [&](bool macro) {
    return [&, macro](const dp::Design& snapshot, int iteration) {
      auto adjusted = snapshot;
      adjust_pins(adjusted, iteration, false, macro);
      std::vector<dp::TerminalPosition> updates;
      for (size_t i = v.terminal_begin; i < snapshot.node_x.size(); ++i) {
        if (adjusted.node_x[i] != snapshot.node_x[i] || adjusted.node_y[i] != snapshot.node_y[i]) {
          updates.push_back({static_cast<int>(i), adjusted.node_x[i], adjusted.node_y[i]});
        }
      }
      return updates;
    };
  };
  const auto write_warp_nodes = [&] {
    fs::create_directories(options.output_dir);
    std::ofstream nodes(options.output_dir + "/warp_nodes.tsv");
    nodes << "node\tname\n";
    for (size_t n = 0; n < v.cells.size(); ++n) {
      nodes << n << '\t' << v.cells[n].name << '\n';
    }
    if (!nodes) {
      throw std::runtime_error("cannot write warp node identities");
    }
  };
  const auto warp_memories = [](const dp::Design& design) {
    std::vector<int> ids;
    for (int n = 0; n < design.num_movable; ++n) {
      if (!design.node_macro.empty() && design.node_macro[n]) {
        ids.push_back(n);
      }
    }
    return ids;
  };
  std::vector<dp::FloorplanCircle> fp_before;
  std::vector<CentroidSample>      fp_gp, fp_final;
  unsigned                         fp_checkpoints = 0, fp_sta_calls = 0, fp_resized = 0, fp_repairs = 0;

  const double initial_hpwl = [&] {
    auto d = v.design;
    std::fill(d.net_weights.begin(), d.net_weights.end(), 1);
    return dp::hpwl(d);
  }();
  Eco_stats eco;
  if (options.timing_seed || options.retain_placement || options.analyze_colors_only) {
    metrics.legal = false;
  } else if (macro_solved && !cache.reused) {
    metrics       = macro_run->result.placement;
    metrics.legal = dp::is_legal(v.design);
    metrics.hpwl  = dp::hpwl(v.design);
  } else if (!cache.hit && previous && options.full && !adjustable_io && !macro_pin_adjust
             && macro_tracks_valid(v, tech, v.design)) {
    // The ECO path keeps imported IO positions; slot only the new terminals,
    // treating the imported ones as fixed obstacles for assign_io.
    if (std::any_of(v.fp.terminals.begin(), v.fp.terminals.end(), [&](const auto& t) {
          return t.status != Status::FIXED && !imported_pos.contains(t.name);
        })) {
      std::vector<Status> saved;
      for (auto& t : v.fp.terminals) {
        saved.push_back(t.status);
        if (imported_pos.contains(t.name)) {
          t.status = Status::FIXED;
        }
      }
      assign_io(v, tech);
      for (size_t i = 0; i < saved.size(); ++i) {
        v.fp.terminals[i].status = saved[i];
      }
      make_arrays(v, tech);
    }
    auto changed = options.changed_cells;
    if (changed.empty() && cached_reference && cache.changed) {
      for (const auto& c : v.cells) {
        if (!c.locked && c.place.status != Status::FIXED && c.place.status != Status::LOCKED) {
          changed.push_back(c.name);
        }
      }
    }
    if (multirow_cells && options.multirow_eco) {
      // The two-row ECO (multirow_eco.hpp) poses the edited two-row cells
      // first; locked, with their padding, they are obstacles while
      // nearest-whitespace legalization re-places the other edited cells and
      // the single-row cells they displaced. It never reaches local_place,
      // whose native GP and single-row legalization would split them.
      std::map<std::string, const ph::Component*> old;
      for (const auto& c : previous->components) {
        old.emplace(c.name, &c);
      }
      const std::set<std::string> forced(changed.begin(), changed.end());
      std::set<std::string>       edited;
      auto                        frame = v.fp;
      frame.components.clear();
      for (const auto& c : v.cells) {
        frame.components.push_back({c.name, c.macro, c.place});
        if (!c.locked && c.place.status != Status::FIXED && c.place.status != Status::LOCKED
            && livehd::is_provisional_domino_cell(c.macro) && is_multirow_core(tech, tech.macros.at(c.macro))
            && (forced.contains(c.name) || eco_dirty(old, c))) {
          edited.insert(c.name);
        }
      }
      auto terminal             = options.terminal_legalize_options;
      terminal.macro_halo_x_dbu = v.halo_x;
      terminal.macro_halo_y_dbu = v.halo_y;
      // The hard bin cap is checked on the grid the reference's terminal
      // legalization used: GP bins as dp::place resolves them (automatic
      // bins from the design; dynamic bins always have an auxiliary grid).
      auto eco_bins             = std::pair{control.bins_x, control.bins_y};
      if (control.automatic_bins) {
        eco_bins = dp::detail::automatic_bins(v.design);
      }
      set_terminal_grid(tech,
                        frame,
                        options,
                        route_grid_bins(control.auxiliary_bins, eco_bins.first),
                        route_grid_bins(control.auxiliary_bins, eco_bins.second),
                        terminal);
      multirow_eco_grid   = {terminal.bins_x, terminal.bins_y};
      // No row is cut (handoff taps refuse a previous frame), so the frame's
      // first core rows are the row-access origins.
      multirow_eco_report = legalize_multirow_edits(tech,
                                                    frame,
                                                    *previous,
                                                    {edited.begin(), edited.end()},
                                                    {anchor_controls(options, terminal, {}), *options.multirow_eco});
      for (size_t i = 0; i < v.cells.size(); ++i) {
        v.cells[i].place = frame.components[i].place;
        if (edited.contains(v.cells[i].name)) {
          v.cells[i].locked = true;
        }
      }
      adopt_rail_twins(v, frame);
      changed.insert(changed.end(), multirow_eco_report->displaced.begin(), multirow_eco_report->displaced.end());
      if (!legalize_edits(v,
                          tech,
                          *previous,
                          changed,
                          eco,
                          options.eco_trace_cells,
                          options.eco_connectivity_weight,
                          terminal.padding_sites)) {
        throw Multirow_failure("two-row ECO cannot re-place its single-row cells; no output committed");
      }
      for (auto& c : v.cells) {
        if (edited.contains(c.name)) {
          c.locked = false;  // movable again, PLACED in the output
        }
      }
      make_arrays(v, tech);
      metrics.legal = true;
    } else if (options.legalize_cells
               && legalize_edits(v, tech, *previous, changed, eco, options.eco_trace_cells, options.eco_connectivity_weight)) {
      metrics.legal = true;
    } else {
      metrics = local_place(v, tech, options, *previous, changed, eco);
      if (eco.released_locks) {
        cache = {0, hashes.size(), false};  // reused blocks were movable, as below
      }
    }
  } else if (!cache.hit && multirow_cells && options.multirow_eco) {
    throw std::invalid_argument("the two-row ECO requires the full-placement ECO branch (fixed IO, valid macro tracks)");
  } else if (!cache.hit) {
    if (!macro_active && !options.restart_seed) {
      // This prototype determines automatic IO locations. Keep it a plain GP
      // (no post-density continuation) so a cohesion comparison starts with the
      // same fixed IOs and GP seed.
      const auto scratch_cells             = v.centroids ? v.cells : std::vector<Cell>{};
      auto       prototype                 = control;
      prototype.magnet.enabled             = false;
      prototype.progress_observer          = {};
      prototype.cohesion_weight            = 0;
      prototype.fixed_macro_overlap_weight = 0;
      prototype.macro_cell_density_weight  = 0;
      prototype.macro_density_pull_weight  = 0;
      prototype.wirelength_gamma_end       = .05;
      prototype.post_density_factor        = 0;
      prototype.macro_align_weight         = 0;
      prototype.macro_snap                 = false;
      prototype.scale_macro_density        = false;
      prototype.automatic_bins = prototype.exact_macro_density = prototype.adaptive_density = false;
      prototype.routability = prototype.improved_routability = false;
      prototype.macro_overlap_weight = prototype.macro_jitter = 0;
      if (options.io_iterations) {
        prototype.max_iterations = options.io_iterations;
      }
      prototype.schedule_iterations         = 0;
      prototype.centroid_weight             = 0;
      prototype.centroid_repulsion_weight   = 0;
      prototype.centroid_adaptive_repulsion = false;
      metrics                               = dp::place(v.design, prototype);
      capture(v, tech, false);
      if ((v.centroids && options.fp_pin_adjust) || macro_pin_adjust) {
        adjust_pins(v.design, 0, true, !(v.centroids && options.fp_pin_adjust));
      } else {
        assign_io(v, tech);
      }
      if (v.centroids) {
        v.cells = scratch_cells;
      }
      make_arrays(v, tech);
    }
    control.legalize            = options.full && !terminal_active;
    control.macro_only_legalize = defer_macro_cells;
    control.detailed_place
        = options.full && !terminal_active && !defer_macro_cells && (!movable_macros || options.macro_detailed_place);
    if (options.snapshots || v.centroids) {
      control.observer_interval = options.iterations + 1;
      control.observer          = [&](const dp::Design& snapshot, dp::PlacementStage stage, int iteration) {
        if (options.snapshots
            && (stage == dp::PlacementStage::global_placement || stage == dp::PlacementStage::legalization
                || stage == dp::PlacementStage::detailed_placement)) {
          auto frame = v.fp;
          frame.components.clear();
          frame.nets.clear();
          for (size_t i = 0; i < v.cells.size(); ++i) {
            const auto& cell  = v.cells[i];
            auto        place = cell.place;
            place.x           = std::llround(snapshot.node_x[i]);
            place.y           = std::llround(snapshot.node_y[i]);
            if (!snapshot.node_orientation.empty()) {
              place.orientation = physical_orientation(snapshot.node_orientation[i]);
            }
            if (i < static_cast<size_t>(snapshot.num_movable)) {
              place.status = Status::PLACED;
              if (!defer_macro_cells && stage != dp::PlacementStage::global_placement
                  && tech.macros.at(cell.macro).klass.starts_with("CORE")) {
                // capture() applies physical CORE row flips after the native
                // solver. Export the same legal orientation without mutating it.
                auto observed  = cell;
                observed.place = place;
                const auto row = static_cast<size_t>(std::llround((place.y - v.fp.rows.front().y) / snapshot.layout.row_height));
                if (row >= v.fp.rows.size()) {
                  throw std::runtime_error("placed cell outside rows");
                }
                if (!apply_row_orientation(observed, tech, v.fp.rows[row].orientation)) {
                  throw std::runtime_error("cell cannot use flipped row: " + cell.name);
                }
                place.orientation = observed.place.orientation;
              }
            }
            frame.components.push_back({cell.name, cell.macro, place});
          }
          std::set<std::string> net_names;
          for (size_t i = 0; i < frame.terminals.size(); ++i) {
            frame.terminals[i].x = std::llround(snapshot.node_x[v.terminal_begin + i]);
            frame.terminals[i].y = std::llround(snapshot.node_y[v.terminal_begin + i]);
            // The imported DEF name is not evidence of current connectivity.
            // Rebuild every terminal alias and reserve all port identities,
            // including unused ports, before choosing internal net names.
            frame.terminals[i].net.clear();
            net_names.insert(frame.terminals[i].name);
          }
          size_t index = 0;
          for (const auto& [driver, sinks] : v.nets) {
            auto net_name = driver.instance == "PIN" ? driver.pin : "snapshot_net_" + std::to_string(index++);
            if (driver.instance != "PIN") {
              const auto base   = net_name;
              unsigned   suffix = 0;
              while (!net_names.insert(net_name).second) {
                net_name = base + "_" + std::to_string(++suffix);
              }
            }
            ph::Net net{net_name, {{driver.instance, driver.pin}}};
            for (const auto& sink : sinks) {
              net.connections.push_back({sink.instance, sink.pin});
            }
            for (const auto& endpoint : net.connections) {
              if (endpoint.instance == "PIN") {
                frame.terminals[v.terminal.at(endpoint.pin)].net = net.name;
              }
            }
            frame.nets.push_back(std::move(net));
          }
          for (auto& terminal : frame.terminals) {
            if (terminal.net.empty()) {
              terminal.net = terminal.name;
              frame.nets.push_back({terminal.net, {{"PIN", terminal.name}}});
            }
          }
          const std::string name = stage == dp::PlacementStage::global_placement ? "before_legalization.def"
                                   : stage == dp::PlacementStage::legalization   ? "after_legalization.def"
                                                                                 : "after_detailed.def";
          restore_imported_rows(frame, v);
          if (!handoff_rows.empty()) {
            frame.rows = handoff_rows;
            frame.components.insert(frame.components.end(), handoff_cells.begin(), handoff_cells.end());
          }
          ph::write_def(frame, options.output_dir + "/" + name);
        }
        if (stage == dp::PlacementStage::centroid_start) {
          centroid_initial = summarize_centroids(snapshot);
        } else if (stage == dp::PlacementStage::global_placement) {
          centroid_gp = summarize_centroids(snapshot);
        } else {
          return;
        }
        if (!options.snapshots) {
          return;
        }
        std::vector<mp::NodeInfo> info;
        for (const auto& c : v.cells) {
          info.push_back({c.name});
        }
        info.resize(v.terminal_begin);  // placement-only masks have no signal identity
        for (const auto& t : v.fp.terminals) {
          info.push_back({"@io:" + t.name, {}, {}, false, false, true});
        }
        write_gp_snapshot(snapshot,
                          info,
                          options.output_dir + (stage == dp::PlacementStage::centroid_start ? "/gp_start.json" : "/gp.json"),
                          iteration);
      };
    }
    if (options.fp_place && (!v.design.centroid_groups.empty() || adjustable_io)) {
      control.centroid_distance_attraction = options.fp_distance_attraction;
      control.centroid_attraction_outside  = options.fp_attraction_outside;
      auto fp_control                      = control;
      fp_control.magnet.enabled            = false;
      // Macro-cell pressure belongs to macro GP, after this FP solve.
      fp_control.macro_cell_density_weight = 0;
      fp_control.macro_density_pull_weight = 0;
      fp_control.route                     = route_policy(options, options.fp_route);
      fp_control.route_probe_observer      = {};
      if (resolved_uroute.export_features && fp_control.route.probe) {
        fp_control.route_probe_observer = probe_exporter("fp_probe");
      }
      fp_control.routability = fp_control.improved_routability
          = options.routability && fp_control.route.model != dp::RouteModel::off;
      // This experiment acts during macro GP, after the optional FP stage.
      if (options.macro_align_global) {
        fp_control.macro_align_weight = 0;
      }
      fp_control.legalize = fp_control.detailed_place = false;
      fp_control.macro_only_legalize                  = false;
      fp_control.centroid_weight                      = options.fp_attraction;
      fp_control.centroid_align_weight                = options.fp_align_weight;
      fp_control.centroid_align_sigma                 = options.fp_align_sigma;
      fp_control.centroid_align_start                 = options.fp_align_start;
      fp_control.centroid_align_full                  = options.fp_align_full;
      fp_control.centroid_parent_weight               = options.fp_parent_weight;
      fp_control.centroid_parent_outside              = options.fp_parent_outside;
      fp_control.centroid_repulsion_weight            = options.fp_repulsion;
      fp_control.centroid_adaptive_repulsion          = true;
      fp_control.max_iterations                       = options.fp_iterations ? options.fp_iterations : options.iterations;
      fp_control.observer_interval                    = fp_control.max_iterations + 1;
      // The FP stage has its own history and physical before-snap snapshot.
      fs::create_directories(options.output_dir);
      std::ofstream fp_progress(options.output_dir + "/fp_history.jsonl");
      if (!fp_progress) {
        throw std::runtime_error("cannot write floorplan progress history");
      }
      fp_control.progress_observer = [&](const dp::GpSample& sample) {
        fp_progress << std::format(
            R"({{"iteration":{},"overflow":{},"hpwl_dbu":{},"centroid_max_overlap":{},"centroid_align_pairs":{},"centroid_align_residual":{},"centroid_align_ramp":{},"centroid_align_force":{},"centroid_parent_edges":{},"centroid_parent_max_distance":{},"centroid_parent_force":{}}})",
            sample.iteration,
            sample.overflow,
            sample.hpwl,
            sample.centroid_max_overlap,
            sample.centroid_align_pairs,
            sample.centroid_align_residual,
            sample.centroid_align_ramp,
            sample.centroid_align_force,
            sample.centroid_parent_edges,
            sample.centroid_parent_max_distance,
            sample.centroid_parent_force)
                    << '\n';
        fp_progress.flush();
      };
      const auto observer = control.observer;
      fp_control.observer = [&](const dp::Design& snapshot, dp::PlacementStage stage, int iteration) {
        if (stage != dp::PlacementStage::centroid_start && stage != dp::PlacementStage::global_placement) {
          return;
        }
        if (!options.snapshots) {
          return;
        }
        std::vector<mp::NodeInfo> info;
        for (const auto& c : v.cells) {
          info.push_back({c.name});
        }
        info.resize(v.terminal_begin);
        for (const auto& t : v.fp.terminals) {
          info.push_back({"@io:" + t.name, {}, {}, false, false, true});
        }
        write_gp_snapshot(snapshot,
                          info,
                          options.output_dir + (stage == dp::PlacementStage::centroid_start ? "/fp_start.json" : "/fp_gp.json"),
                          iteration);
      };
      if (options.fp_pin_adjust) {
        fp_control.terminal_interval = options.fp_pin_interval;
        fp_control.terminal_update   = pin_updater(false);
      }
      std::map<std::string, Point> timed_positions;
      bool                         fp_checkpoint_forced = false;
      if (options.fp_evaluate && options.fp_timing_interval > 0) {
        v.frozen_centroids           = v.design.centroid_groups;
        v.frozen_hierarchy_centroids = v.hierarchy_centroids;
        v.frozen_color_names         = v.design.centroid_color_names;
        if (v.placement_colors) {
          for (size_t i = 0; i < v.cells.size(); ++i) {
            v.frozen_placement_owner[v.cells[i].name] = v.fp_coloring.owner[i];
          }
        }
        for (const auto& group : v.design.centroid_groups) {
          std::vector<std::string> names;
          for (int i : group.nodes) {
            names.push_back(v.cells.at(i).name);
          }
          v.frozen_members.push_back(std::move(names));
        }
        fp_control.checkpoint_interval      = options.fp_timing_interval;
        fp_control.checkpoint_final         = options.fp_timing_movement > 0;
        fp_control.checkpoint_after_density = options.fp_timing_after_density;
        if (options.gp_rank && !options.fp_resize) {
          fp_control.checkpoint_after_density       = false;
          fp_control.checkpoint_overflow_milestones = {.2, .1};
        }
        fp_control.checkpoint_observer = [&](const dp::GpCheckpointSample& sample) { fp_checkpoint_forced = sample.forced; };
        fp_control.checkpoint          = [&](const dp::Design& physical, int iteration) {
          const double movement = checkpoint_movement(physical, v.cells, timed_positions, options.bins);
          const bool   final    = iteration == fp_control.max_iterations || fp_checkpoint_forced;
          const char*  trigger  = options.fp_timing_after_density ? (final ? "final" : "density-ready") : "interval";
          if (options.fp_timing_movement > 0) {
            if (defer_timing_checkpoint(options, fp_checkpoints, final, iteration, fp_control.max_iterations, movement)) {
              return dp::GpUpdate{};
            }
            trigger = final ? "final" : fp_checkpoints ? "movement" : "initial";
          }

          const auto timing_start = std::chrono::steady_clock::now();
          fs::create_directories(options.output_dir);
          // This snapshot may overlap: STA needs physical pins/RC, not a legal DEF.
          v.design = physical;
          for (size_t i = 0; i < v.cells.size(); ++i) {
            v.cells[i].place.x = std::llround(physical.node_x[i]);
            v.cells[i].place.y = std::llround(physical.node_y[i]);
          }
          Result checkpoint;
          checkpoint.prects            = v.prects;
          checkpoint.graph             = result.graph;
          checkpoint.net_weights       = v.weights;
          checkpoint.fp_library        = result.fp_library;
          checkpoint.fp_buffer_origins = result.fp_buffer_origins;
          snapshot_frame(checkpoint.floorplan, v);
          const auto baseline       = v;
          const auto baseline_owner = result.fp_library;
          const auto refresh        = [&](Result& state) {
            refresh_floorplan_view(v, baseline, state, tech, options.unknown_const_zero);
            state.pin_locations.clear();
            auto rc_options  = options;
            rc_options.full  = false;
            state.parasitics = estimate_rc(v, tech, rc_options, &state.pin_locations);
            record_poses(*state.parasitics, v);
          };
          refresh(checkpoint);
          const auto evaluate = [&](const Result& state) {
            ++fp_sta_calls;
            auto feedback = options.gp_rank && !options.fp_resize ? options.gp_rank({state.graph, state.parasitics, v.ignored})
                                                                  : options.fp_evaluate(state);
            return prects_data_timing(feedback, v.ignored);
          };
          const auto before  = evaluate(checkpoint);
          auto       after   = before;
          unsigned   resized = 0, proposed = 0, repairs = 0;
          const auto sta_before_repair = fp_sta_calls;
          if (options.fp_resize) {
            const auto sized = options.resize_engine == "new"
                                   ? resize_floorplan_engine(checkpoint, options, before, evaluate, refresh)
                                   : repair_floorplan(checkpoint, options, before, evaluate, refresh);
            after            = sized.timing;
            resized          = sized.accepted;
            proposed         = sized.proposed;
            repairs          = sized.repair_accepted;
            std::ofstream(options.output_dir + "/fp_repair_" + std::to_string(iteration) + ".json") << sized.report;
            // Replace the graph before its owning library: members assign in
            // declaration order, which would otherwise drop the old library first.
            result.graph             = checkpoint.graph;
            result.fp_library        = checkpoint.fp_library;
            result.fp_buffer_origins = checkpoint.fp_buffer_origins;
          }
          const auto target = timing_weights(*checkpoint.parasitics,
                                             after,
                                             options.timing_fraction,
                                             options.timing_max_weight,
                                             options.timing_exponent);
          if (target.empty()) {
            throw std::runtime_error("FP timing has no constrained data nets for weighting");
          }
          refresh_timing_weights(checkpoint, options, after, weight_roles);
          v.weights = checkpoint.net_weights;
          make_arrays(v, tech);
          timed_positions.clear();
          for (const auto& c : v.cells) {
            timed_positions.emplace(c.name, Point{c.place.x, c.place.y});
          }
          ++fp_checkpoints;
          fp_resized += resized;
          fp_repairs += repairs;
          fs::create_directories(options.output_dir);
          std::ofstream trace(options.output_dir + "/fp_timing.jsonl", fp_checkpoints == 1 ? std::ios::trunc : std::ios::app);
          trace << std::format(
              R"({{"iteration":{},"trigger":"{}","movement_bins":{},"timing_seconds":{},"weighted_nets":{},"sta_calls":{},"resize_proposed":{},"resize_accepted":{},"repair_accepted":{},"before_delay":{},"after_delay":{},"time_unit_ps":{},"timing_exponent":{},"ranking_only":{}}})",
              iteration,
              trigger,
              movement,
              std::chrono::duration<double>(std::chrono::steady_clock::now() - timing_start).count(),
              target.size(),
              1 + fp_sta_calls - sta_before_repair,
              proposed,
              resized,
              repairs,
              before.ranking_only ? "null" : std::format("{}", before.delay),
              after.ranking_only ? "null" : std::format("{}", after.delay),
              after.time_unit_ps,
              options.timing_exponent,
              after.ranking_only)
                << '\n';
          if (!trace) {
            throw std::runtime_error("cannot write FP timing checkpoint report");
          }
          dp::GpUpdate update;
          update.replacement = v.design;
          if (after.wns) {
            update.timing_violation = std::max(0., -*after.wns);
          }
          for (const auto& c : v.cells) {
            const auto old = baseline.cell.find(c.name);
            update.previous_node.push_back(old == baseline.cell.end() ? -1 : int(old->second));
          }
          append_previous_boundary(update.previous_node, v, baseline);
          return update;
        };
      }
      fp_metrics = dp::place(v.design, fp_control);
      if (options.after_stage) {
        auto view = v;
        capture(view, tech, false);
        Result checkpoint;
        checkpoint.prects     = v.prects;
        checkpoint.graph      = result.graph;
        checkpoint.fp_library = result.fp_library;
        snapshot_frame(checkpoint.floorplan, view);
        auto rc_options       = options;
        rc_options.full       = false;
        checkpoint.parasitics = estimate_rc(view, tech, rc_options, &checkpoint.pin_locations);
        for (const auto& c : view.cells) {
          c.node.attr(attrs::place).set(c.place);
        }
        record_poses(*checkpoint.parasitics, view);
        if (options.after_stage(Placement_stage::floorplan, checkpoint)) {
          throw Placement_restart(checkpoint.floorplan, Placement_stage::floorplan);
        }
      }
      fp_gp = summarize_centroids(v.design);
      for (size_t i = 0; i < fp_gp.size(); ++i) {
        const auto& g = v.design.centroid_groups[i];
        const auto& p = fp_gp[i];
        fp_before.push_back({g.name,
                             p.x,
                             p.y,
                             std::sqrt((g.repulsion_area < 0 ? p.area : g.repulsion_area) / (std::acos(-1.) * options.density)),
                             p.area,
                             g.family,
                             g.parent});
      }
      const auto                       fp_before_warp = fp_before;
      std::vector<Warp_centroid_shift> warp_shifts;
      if (options.fp_warp.enabled && !warp_memories(v.design).empty()) {
        try {
          write_warp_nodes();
          auto fp_policy       = options.fp_warp;
          fp_policy.dbu_per_um = tech.dbu;
          const auto field     = placement_warp(v.design, warp_memories(v.design), fp_policy);
          warp_shifts          = shift_warp_centroids(v.design, field.delta, fp_before, options.fp_warp.weight);
          write_warp_field(field, options.fp_warp, options.output_dir + "/fp_warp_field.json", fp_metrics.iterations, v.design);
        } catch (const std::exception& error) {
          fp_before = fp_before_warp;
          warp_shifts.clear();
          write_warp_status(options.output_dir + "/fp_warp_field.json", "retained", error.what());
          std::print(stderr, "WARNING: FP warpspace retained original targets: {}\n", error.what());
        }
      }
      const auto& l = v.design.layout;
      if (options.fp_snap_enabled) {
        fp_snap = snap_centroid_targets(fp_before, v.hierarchy_centroids, l, options.fp_snap);
      } else {
        // A true bypass: do not call the point fitter or repair its circles.
        fp_snap.emplace();
        fp_snap->circles         = fp_before;
        fp_snap->maximum_overlap = fp_metrics.centroid.max_circle_overlap;
      }
      if (options.fp_warp.enabled) {
        std::ofstream report(options.output_dir + "/fp_warp_targets.tsv");
        report << "name\tbefore_x\tbefore_y\twarped_x\twarped_y\tsnapped_x\tsnapped_y\tsum_dx\tsum_dy\tleaves\tmemories\n";
        report.precision(17);
        for (size_t g = 0; g < fp_before.size(); ++g) {
          const auto shift = warp_shifts.empty() ? Warp_centroid_shift{} : warp_shifts[g];
          report << fp_before[g].name << '\t' << fp_before_warp[g].x << '\t' << fp_before_warp[g].y << '\t' << fp_before[g].x
                 << '\t' << fp_before[g].y << '\t' << fp_snap->circles[g].x << '\t' << fp_snap->circles[g].y << '\t' << shift.sum.x
                 << '\t' << shift.sum.y << '\t' << shift.count << '\t' << shift.memories << '\n';
        }
        if (!report) {
          throw std::runtime_error("cannot write FP warp target report");
        }
      }
      if (control.max_iterations > 0 && options.fp_anchor > 0) {
        for (const auto& p : fp_snap->circles) {
          v.design.centroid_targets.emplace_back(p.x, p.y);
        }
      }
      // Warm-start the next mixed-size GP at the FP positions, with fixed
      // targets and NO centroid repulsion. Cells/macros are still movable.
      control.centroid_anchor_macros_only = options.fp_anchor_macros_only && !v.design.centroid_targets.empty();
      control.centroid_weight             = options.fp_anchor;
      control.centroid_repulsion_weight   = 0;
      control.centroid_adaptive_repulsion = false;
      control.macro_jitter                = 0;
      control.observer                    = observer;
      if (control.max_iterations == 0) {
        // Direct macro-frame control: legalize the completed floorplan GP,
        // without another GP or frozen centroid targets. Export its input frame.
        centroid_gp = fp_gp;
        if (control.observer) {
          control.observer(v.design, dp::PlacementStage::global_placement, 0);
        }
      }
      if (!fp_snap->feasible) {
        std::print(stderr,
                   "WARNING: floorplan snap retained {} unresolved families; max circle penetration {:.6f}\n",
                   fp_snap->infeasible_families,
                   fp_snap->maximum_overlap);
      }
    }
    // The second mixed-size solve must refresh its own RC/criticalities. FP
    // weights alone become stale as macros settle around the frozen centroids.
    // This callback changes only the objective: macro geometry, centroid targets
    // and graph topology stay unchanged. Electrical repair follows legal GP.
    std::map<std::string, Point> macro_timed_positions;
    unsigned                     macro_checkpoints         = 0;
    bool                         macro_checkpoint_forced   = false;
    double                       macro_checkpoint_overflow = 0;
    if (macro_pin_adjust) {
      control.terminal_interval = options.macro_pin_interval;
      control.terminal_update   = pin_updater(true);
    }
    const bool final_timing = !defer_macro_cells && !movable_macros && !options.fp_place;
    if (options.native_gp_timing && !final_timing) {
      throw std::invalid_argument("native GP pin timing requires fixed-macro final cell placement");
    }
    Timing_feedback final_gp_timing, trial_gp_timing;
    bool            final_gp_timing_started     = false;
    int             last_final_timing_iteration = -1;
    bool            trial_scoring               = false;
    const char*     timing_stage                = final_timing ? "final" : "macro";
    bool            legal_timing_snapshot       = false;
    bool            save_macro_gp_timing        = false;
    if ((movable_macros || final_timing || defer_macro_cells) && options.fp_evaluate
        && (options.macro_timing_interval > 0 || (defer_macro_cells && options.after_stage))) {
      control.checkpoint_interval = options.macro_timing_interval;
      control.checkpoint_final    = true;
      control.checkpoint_start    = final_timing ? options.final_timing_start : 0;
      if (final_timing && control.checkpoint_interval > 0) {
        // Do not converge into final timing before completing the warm-up,
        // except when the user explicitly requested a shorter diagnostic run.
        control.min_iterations = std::max(control.min_iterations, std::min(control.max_iterations, options.final_timing_start));
      }
      control.checkpoint_after_density = options.fp_timing_after_density || (final_timing && options.final_independent_timing);
      // Ranking is needed as the placement spreads into feasible density,
      // including mixed-size macro GP. The interval samples overflow; it must
      // not run a full ranking update at every sample on large macro designs.
      if ((final_timing || options.gp_rank) && !options.native_gp_timing) {
        control.checkpoint_after_density       = false;
        control.checkpoint_overflow_milestones = {.2, .1};
      }
      control.checkpoint_observer = [&](const dp::GpCheckpointSample& sample) {
        macro_checkpoint_forced   = sample.forced;
        macro_checkpoint_overflow = sample.overflow;
      };
      control.checkpoint = [&](const dp::Design& physical, int iteration) {
        const auto started = std::chrono::steady_clock::now();
        const bool final   = macro_checkpoint_forced || iteration == control.max_iterations;
        if (final_timing && options.final_timing_schedule == "late" && !trial_scoring && !final
            && last_final_timing_iteration >= 0) {
          // Start with sparse timing after warm-up, then increase cadence in
          // the last half/quarter. Forced/final scoring is always fresh.
          const double timing_progress
              = double(iteration - options.final_timing_start) / std::max(1, control.max_iterations - options.final_timing_start);
          const int multiple = timing_progress < .5 ? 4 : timing_progress < .75 ? 2 : 1;
          if (int64_t(iteration) - last_final_timing_iteration < int64_t(control.checkpoint_interval) * multiple) {
            return dp::GpUpdate{};
          }
        }
        const double movement = checkpoint_movement(physical, v.cells, macro_timed_positions, options.bins);
        if (!trial_scoring
            && defer_timing_checkpoint(options, macro_checkpoints, final, iteration, control.max_iterations, movement)) {
          return dp::GpUpdate{};
        }
        // Names and movable/fixed membership do not change, so native cell
        // and pin indices remain valid for both GP and legal geometry.
        const auto geometry = legal_timing_snapshot ? Geometry_view{v, physical, tech} : Geometry_view{v, physical};
        for (size_t i = 0; i < geometry.view.cells.size(); ++i) {
          // STA validates graph poses against the RC provenance. Do not update
          // v.cells: the original placement remains the base for later trials.
          geometry.view.cells[i].node.attr(attrs::place).set(geometry.pose(i));
        }
        Result checkpoint;
        checkpoint.prects      = v.prects;
        checkpoint.graph       = result.graph;
        checkpoint.net_weights = v.weights;
        checkpoint.fp_library  = result.fp_library;
        const bool ranking     = options.gp_rank && !trial_scoring;
        if (!ranking) {
          snapshot_frame(checkpoint.floorplan, geometry);
        }
        auto rc_options = options;
        rc_options.full = false;
        fs::create_directories(options.output_dir);
        // Ordinary ranking consumes RC, not a duplicate string-keyed map of
        // every pin position. The selected macro handoff still owns that map.
        auto* pin_locations   = !ranking || save_macro_gp_timing ? &checkpoint.pin_locations : nullptr;
        checkpoint.parasitics = estimate_rc(geometry, tech, rc_options, pin_locations);
        record_poses(*checkpoint.parasitics, geometry);
        // Propagation belongs to untimed GP. Do not defer STA to finish jumps.
        final_gp_timing_started |= final_timing;
        auto timing              = prects_data_timing(
            ranking ? options.gp_rank({checkpoint.graph, checkpoint.parasitics, v.ignored}) : options.fp_evaluate(checkpoint),
            v.ignored);
        if (trial_scoring) {
          trial_gp_timing = timing;
          return dp::GpUpdate{};  // Losing branches never refresh shared weights or timing history.
        }
        dp::GpUpdate topology_update;
        topology_update.stop_movable_updates = final_gp_timing_started;
        // Electrical/topology repair runs after GP on legal geometry. Trial
        // buffering of provisional GP coordinates must not trigger full STA.
        final_gp_timing                      = timing;
        last_final_timing_iteration          = iteration;
        if (ranking) {
          // Build the weighting snapshot after the timer retires the previous
          // RC generation, avoiding simultaneous copies of both at its peak.
          snapshot_frame(checkpoint.floorplan, geometry);
        }
        refresh_timing_weights(checkpoint, options, timing, weight_roles);
        v.weights           = checkpoint.net_weights;
        dp::GpUpdate update = std::move(topology_update);
        for (const auto& driver : v.drivers) {
          const auto name = driver.label();
          update.net_weights.push_back(v.weights.contains(name) ? v.weights.at(name) : 1.);
        }
        if (timing.wns) {
          update.timing_violation = std::max(0., -*timing.wns);
        }
        macro_timed_positions.clear();
        for (size_t i = 0; i < v.cells.size(); ++i) {
          const auto pose = geometry.pose(i);
          macro_timed_positions.emplace(v.cells[i].name, Point{pose.x, pose.y});
        }
        ++macro_checkpoints;
        std::ofstream trace(options.output_dir + "/" + timing_stage + "_timing.jsonl",
                            macro_checkpoints == 1 ? std::ios::trunc : std::ios::app);
        trace << std::format(
            R"({{"stage":"{}","iteration":{},"forced":{},"overflow":{},"movement_bins":{},"timing_seconds":{},"weighted_nets":{},"timing_exponent":{},"max_applied_weight":{},"delay":{},"time_unit_ps":{},"ranking_only":{}}})",
            timing_stage,
            iteration,
            final,
            macro_checkpoint_overflow,
            movement,
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(),
            std::count_if(v.weights.begin(), v.weights.end(), [](const auto& entry) { return entry.second > 1; }),
            options.timing_exponent,
            update.net_weights.empty() ? 1. : *std::max_element(update.net_weights.begin(), update.net_weights.end()),
            timing.ranking_only ? "null" : std::format("{}", timing.delay),
            timing.time_unit_ps,
            timing.ranking_only)
              << '\n';
        if (!trace) {
          throw std::runtime_error("cannot write macro timing checkpoint report");
        }
        const auto membership = timing_weights(*checkpoint.parasitics,
                                               timing,
                                               options.timing_fraction,
                                               options.timing_max_weight,
                                               options.timing_exponent);
        if (save_macro_gp_timing) {
          result.macro_gp_timing = Macro_gp_timing{timing, std::move(checkpoint.parasitics), std::move(checkpoint.pin_locations)};
        }
        rapidjson::StringBuffer                    weights_json;
        rapidjson::Writer<rapidjson::StringBuffer> weights_writer(weights_json);
        weights_writer.StartObject();
        weights_writer.Key("stage");
        weights_writer.String(timing_stage);
        weights_writer.Key("checkpoint");
        weights_writer.Uint(macro_checkpoints);
        weights_writer.Key("iteration");
        weights_writer.Int(iteration);
        weights_writer.Key("nets");
        weights_writer.StartArray();
        for (const auto& [driver, weight] : v.weights) {
          weights_writer.StartObject();
          weights_writer.Key("driver");
          weights_writer.String(driver.c_str());
          weights_writer.Key("critical");
          weights_writer.Bool(membership.contains(driver));
          weights_writer.Key("weight");
          weights_writer.Double(weight);
          weights_writer.EndObject();
        }
        weights_writer.EndArray();
        weights_writer.EndObject();
        std::ofstream weights_file(options.output_dir + "/" + timing_stage + "_weights.json");
        weights_file << weights_json.GetString() << '\n';
        if (!weights_file) {
          throw std::runtime_error("cannot write placement weight report");
        }
        if (update.replacement) {
          update.replacement->net_weights = std::move(update.net_weights);
          v.design.net_weights            = update.replacement->net_weights;
        }
        return update;
      };
    }
    if (final_timing && options.gp_search.enabled) {
      if (!control.checkpoint || options.boundarypins.enabled || options.busorder.enabled || options.cell_jump.depth > 1) {
        throw std::invalid_argument("gp_ab requires timing feedback and fixed terminals, with cell_jump at most one");
      }
      control.gp_search    = options.gp_search;
      control.trial_timing = [&](const dp::Design& physical) {
        // The timer owns mutable incremental caches. Score serially, restore
        // host graph placements, and leave net weights/normal checkpoints alone.
        std::vector<Ann_place> saved;
        saved.reserve(v.cells.size());
        for (const auto& c : v.cells) {
          saved.push_back(c.node.attr(attrs::place).get());
        }
        const auto restore = [&] {
          trial_scoring = false;
          for (size_t i = 0; i < v.cells.size(); ++i) {
            v.cells[i].node.attr(attrs::place).set(saved[i]);
          }
        };
        trial_scoring = true;
        try {
          control.checkpoint(physical, -1);
        } catch (...) {
          restore();
          throw;
        }
        restore();
        if (!trial_gp_timing.wns || !trial_gp_timing.tns || !std::isfinite(*trial_gp_timing.wns)
            || !std::isfinite(*trial_gp_timing.tns) || !(trial_gp_timing.time_unit_ps > 0)) {
          throw std::runtime_error("gp_ab requires finite WNS/TNS and a timing unit for every trial");
        }
        const double ns = trial_gp_timing.time_unit_ps / 1000.;
        return std::pair{*trial_gp_timing.wns * ns, *trial_gp_timing.tns * ns};
      };
      control.trial_observer = [&](const dp::GpTrialSample& sample) {
        rapidjson::StringBuffer                    buffer;
        rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
        json.StartObject();
        json.Key("iteration");
        json.Int(sample.iteration);
        json.Key("steps");
        json.Int(sample.steps);
        json.Key("round");
        json.Int(sample.round);
        json.Key("parameter");
        json.String(sample.adjusting_fillers ? "filler_charge" : "local_density_weight");
        json.Key("selected");
        json.Int(sample.selected);
        json.Key("reason");
        json.String(sample.reason.c_str());
        json.Key("priority");
        json.String("rsmt_rudy,wns,hpwl,tns");
        json.Key("relative_tie_bands");
        json.StartArray();
        for (double t : {options.gp_search.route_tolerance,
                         options.gp_search.timing_tolerance,
                         options.gp_search.hpwl_tolerance,
                         options.gp_search.timing_tolerance}) {
          json.Double(t);
        }
        json.EndArray();
        const auto score = [&](const dp::GpTrialScore& q) {
          json.StartObject();
          json.Key("rsmt_rudy_overflow");
          json.Double(q.route_overflow);
          json.Key("wns_ns");
          json.Double(q.wns);
          json.Key("hpwl_um");
          json.Double(q.hpwl / tech.dbu);
          json.Key("tns_ns");
          json.Double(q.tns);
          json.Key("physical_overflow");
          json.Double(q.physical_overflow);
          json.Key("route_unroutable_nets");
          json.Int(q.route_unroutable_nets);
          json.Key("route_blocked_pins");
          json.Int(q.route_blocked_pins);
          json.Key("route_measured_nets");
          json.Int(q.route_measured_nets);
          json.Key("route_congestion_overflow");
          json.Double(q.route_congestion_overflow);
          json.Key("route_peak_utilization");
          json.Double(q.route_peak_utilization);
          json.Key("route_detour_wirelength_um");
          json.Double(q.route_detour_wirelength / tech.dbu);
          json.EndObject();
        };
        json.Key("parent");
        score(sample.before);
        json.Key("candidates");
        json.StartArray();
        for (size_t i = 0; i < sample.settings.size(); ++i) {
          json.StartObject();
          json.Key("filler_charge");
          json.Double(sample.settings[i].filler_charge);
          json.Key("local_density_weight");
          json.Double(sample.settings[i].local_weight);
          json.Key("selected");
          json.Bool(i == size_t(sample.selected));
          json.Key("score");
          score(sample.scores[i]);
          json.EndObject();
        }
        json.EndArray();
        json.EndObject();
        std::ofstream trace(options.output_dir + "/gp_ab.jsonl", sample.round ? std::ios::app : std::ios::trunc);
        trace << buffer.GetString() << '\n';
        if (!trace) {
          throw std::runtime_error("cannot write GP A/B trial trace");
        }
      };
    }
    if (defer_macro_cells && options.after_stage && control.checkpoint) {
      control.before_legalize = [&](dp::Design& physical) {
        // GP can restore an earlier selected iterate. Measure that exact frame
        // before macro snap, and retain it for the resynthesis callback only.
        macro_checkpoint_forced = true;
        save_macro_gp_timing    = true;
        const auto update       = control.checkpoint(physical, control.max_iterations);
        save_macro_gp_timing    = false;
        physical.net_weights    = update.net_weights;
      };
    }
    if (final_timing && ((options.fp_evaluate && control.checkpoint) || options.legalize_timing_strength > 0)) {
      if (!options.fp_evaluate || !control.checkpoint) {
        throw std::invalid_argument("timing-aware legalization requires timing feedback and a positive timing interval");
      }
      control.before_legalize = [&](dp::Design& physical) {
        // The selected GP checkpoint can precede the last iteration. Reanalyze
        // precisely the coordinates handed to the legalizer, never stale weights.
        macro_checkpoint_forced = true;
        const auto update       = control.checkpoint(physical, control.max_iterations);
        physical.net_weights    = update.net_weights;
        // Fresh priorities also reach the zero-strength control, so comparing
        // displacement costs does not accidentally compare stale detail weights.
        if (options.legalize_timing_strength == 0) {
          return;
        }
        physical.node_legalize_weights.assign(physical.node_x.size(), 1);
        physical.node_legalize_anchor_x = physical.node_x;
        physical.node_legalize_anchor_y = physical.node_y;
        // Rank current cell path slack, including positive-slack designs.
        // Negative-slack normalization alone loses all priorities as soon as
        // every path meets timing, and depends on arbitrary timing units.
        std::vector<std::optional<double>> cell_slacks(v.cells.size());
        std::vector<double>                ranked;
        for (size_t i = 0; i < v.cells.size(); ++i) {
          cell_slacks[i] = final_gp_timing.cell_slack(v.cells[i].name);
          if (cell_slacks[i]) {
            ranked.push_back(*cell_slacks[i]);
          }
        }
        std::sort(ranked.begin(), ranked.end());
        ranked.erase(std::unique(ranked.begin(), ranked.end()), ranked.end());
        std::ofstream priorities(options.output_dir + "/legalize_priorities.tsv");
        priorities << "cell\tslack\tcriticality\tweight\tgp_x_dbu\tgp_y_dbu\n";
        for (size_t i = 0; i < v.cells.size(); ++i) {
          double criticality = 0;
          if (cell_slacks[i]) {
            criticality = ranked.size() <= 1
                              ? 1
                              : 1.
                                    - double(std::lower_bound(ranked.begin(), ranked.end(), *cell_slacks[i]) - ranked.begin())
                                          / (ranked.size() - 1);
          }
          if (criticality >= options.legalize_timing_threshold) {
            physical.node_legalize_weights[i] = 1 + options.legalize_timing_strength * criticality;
          }
          priorities << v.cells[i].name << '\t' << (cell_slacks[i] ? std::format("{}", *cell_slacks[i]) : "nan") << '\t'
                     << criticality << '\t' << physical.node_legalize_weights[i] << '\t' << physical.node_x[i] << '\t'
                     << physical.node_y[i] << '\n';
        }
        if (!priorities) {
          throw std::runtime_error("cannot write timing legalization priorities");
        }
      };
    }
    std::optional<dp::Design>                     sat_gp;
    std::shared_ptr<const Satlegalize_route_grid> sat_routing;
    std::optional<dp::RudyOptions>                sat_route_options;
    if (final_timing && options.satlegalize_options.route_grid) {
      if (!options.fp_evaluate || !control.checkpoint || control.route.model != dp::RouteModel::rsmt
          || !control.improved_routability || control.automatic_bins || options.satlegalize_options.region_width_um
          || options.satlegalize_options.region_height_um) {
        throw std::invalid_argument(
            "satlegalize bin.grid=route requires timing feedback, explicit rsmt, routability, a non-macro density grid and no "
            "bin.size_um");
      }
      dp::RudyOptions routing;
      routing.bins_x              = route_grid_bins(control.auxiliary_bins, control.bins_x);
      routing.bins_y              = route_grid_bins(control.auxiliary_bins, control.bins_y);
      routing.horizontal_capacity = control.route_capacity_h;
      routing.vertical_capacity   = control.route_capacity_v;
      routing.macro_blockages = routing.pin_access = true;
      routing.model                                = dp::RudyModel::rsmt;
      routing.degree                               = control.route.degree;
      routing.accuracy                             = control.route.accuracy;
      routing.initialize                           = control.route_initialize;
      sat_route_options                            = routing;
      const auto prior                             = control.before_legalize;
      control.before_legalize                      = [&, prior, routing](dp::Design& physical) {
        if (prior) {
          prior(physical);
        } else {
          macro_checkpoint_forced = true;
          physical.net_weights    = control.checkpoint(physical, control.max_iterations).net_weights;
        }
        const auto map        = dp::rudy(physical, routing);
        auto       grid       = std::make_shared<Satlegalize_route_grid>();
        grid->bounds          = {physical.layout.xl, physical.layout.yl, physical.layout.xh, physical.layout.yh};
        grid->bins_x          = routing.bins_x;
        grid->bins_y          = routing.bins_y;
        grid->horizontal      = map.horizontal;
        grid->vertical        = map.vertical;
        grid->pin_utilization = map.pin_utilization;
        sat_routing           = std::move(grid);
        uint64_t geometry     = hash_util::kFnv1a64_offset;
        for (const auto& c : v.cells) {
          geometry = hash_text(std::format("{}:{};", c.name, c.macro), geometry);
        }
        for (const auto& [driver, sinks] : v.nets) {
          geometry = hash_text(std::format("{}:{}=", driver.instance, driver.pin), geometry);
          for (const auto& sink : sinks) {
            geometry = hash_text(std::format("{}:{};", sink.instance, sink.pin), geometry);
          }
        }
        const auto bind = [&](const auto& values) {
          geometry = hash_text(std::format("{}:", values.size()), geometry);
          if (!values.empty()) {
            geometry
                = hash_text(std::string_view(reinterpret_cast<const char*>(values.data()), values.size() * sizeof(values.front())),
                            geometry);
          }
        };
        bind(physical.node_x);
        bind(physical.node_y);
        bind(physical.node_size_x);
        bind(physical.node_size_y);
        bind(physical.node_orientation);
        bind(physical.node_macro);
        bind(physical.pin_offset_x);
        bind(physical.pin_offset_y);
        bind(physical.pin2node);
        bind(physical.flat_net2pin);
        bind(physical.flat_net2pin_start);
        bind(physical.net_mask);
        geometry                            = hash_text(std::format("{}:{}:{}:{}:{}:{}",
                                                                    physical.layout.xl,
                                                                    physical.layout.yl,
                                                                    physical.layout.xh,
                                                                    physical.layout.yh,
                                                                    physical.layout.site_width,
                                                                    physical.layout.row_height),
                                                        geometry);
        const auto              weight_hash = hash_text(std::string_view(reinterpret_cast<const char*>(physical.net_weights.data()),
                                                                         physical.net_weights.size() * sizeof(double)));
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
        json.StartObject();
        json.Key("schema");
        json.Int(1);
        json.Key("model");
        json.String("rsmt");
        json.Key("index_order");
        json.String("x*bins_y+y");
        json.Key("bins_x");
        json.Int(routing.bins_x);
        json.Key("bins_y");
        json.Int(routing.bins_y);
        json.Key("bounds_dbu");
        json.StartArray();
        for (double x : {physical.layout.xl, physical.layout.yl, physical.layout.xh, physical.layout.yh}) {
          json.Double(x);
        }
        json.EndArray();
        json.Key("dbu_per_um");
        json.Int64(tech.dbu);
        json.Key("degree");
        json.Int(routing.degree);
        json.Key("accuracy");
        json.Int(routing.accuracy);
        json.Key("capacity_h_per_dbu");
        json.Double(routing.horizontal_capacity);
        json.Key("capacity_v_per_dbu");
        json.Double(routing.vertical_capacity);
        json.Key("macro_blockages");
        json.Bool(routing.macro_blockages);
        json.Key("pin_access");
        json.Bool(routing.pin_access);
        json.Key("geometry_topology_fnv1a64");
        json.String(std::format("{:016x}", geometry).c_str());
        json.Key("net_weights_fnv1a64");
        json.String(std::format("{:016x}", weight_hash).c_str());
        json.Key("net_count");
        json.Uint64(physical.net_weights.size());
        json.Key("overflow");
        json.Double(map.overflow);
        json.Key("max_utilization");
        json.Double(map.max_utilization);
        json.Key("measured_nets");
        json.Int(map.measured_nets);
        json.Key("blocked_pins");
        json.Int(map.blocked_pins);
        json.Key("unroutable_nets");
        json.Int(map.unroutable_nets);
        json.Key("detour_wirelength_dbu");
        json.Double(map.detour_wirelength);
        const auto array = [&](const char* key, const auto& values) {
          json.Key(key);
          json.StartArray();
          for (double value : values) {
            json.Double(value);
          }
          json.EndArray();
        };
        array("horizontal", map.horizontal);
        array("vertical", map.vertical);
        array("pin_utilization", map.pin_utilization);
        array("blocked_fraction", map.blocked_fraction);
        json.EndObject();
        std::ofstream file(options.output_dir + "/sat_route_grid.json");
        file << buffer.GetString() << '\n';
        if (!file) {
          throw std::runtime_error("cannot write SAT route grid");
        }
      };
    }
    auto sat_frame = [&](const dp::Design& physical, bool legal) {
      auto view   = v;
      view.design = physical;
      capture(view, tech, legal);
      auto frame = view.fp;
      restore_imported_rows(frame, view);
      frame.components.clear();
      frame.nets.clear();
      for (const auto& c : view.cells) {
        frame.components.push_back({c.name, c.macro, c.place});
      }
      size_t serial = 0;
      for (const auto& [driver, sinks] : v.nets) {
        ph::Net net{"sat_net_" + std::to_string(serial++), {{driver.instance, driver.pin}}};
        for (const auto& sink : sinks) {
          net.connections.push_back({sink.instance, sink.pin});
        }
        frame.nets.push_back(std::move(net));
      }
      return frame;
    };
    auto sat_install = [&](dp::Design& physical, const ph::Floorplan& frame) {
      for (size_t i = 0; i < v.cells.size(); ++i) {
        physical.node_x[i] = frame.components[i].place.x;
        physical.node_y[i] = frame.components[i].place.y;
        dp::set_orientation_unchecked(physical, i, native_orientation(frame.components[i].place.orientation));
      }
      physical.validate();
    };
    if (final_timing && options.satlegalize && !terminal_active) {
      if (!options.fp_evaluate || !control.checkpoint) {
        throw std::invalid_argument("satlegalize requires timing feedback and a positive timing interval");
      }
      const auto prior        = control.before_legalize;
      control.before_legalize = [&, prior](dp::Design& physical) {
        if (prior) {
          prior(physical);
        } else {
          macro_checkpoint_forced = true;
          const auto update       = control.checkpoint(physical, control.max_iterations);
          physical.net_weights    = update.net_weights;
        }
        sat_gp     = physical;
        auto frame = sat_frame(physical, false);
        if (options.satlegalize_options.region_width_um || options.satlegalize_options.region_height_um
            || options.satlegalize_options.route_grid) {
          auto sat_options             = options.satlegalize_options;
          sat_options.prects           = v.prects;
          sat_options.macro_halo_x_dbu = v.halo_x;
          sat_options.macro_halo_y_dbu = v.halo_y;
          sat_options.routing          = sat_routing;
          sat_report                   = satlegalize(tech, frame, final_gp_timing, sat_options);
        } else {
          auto sat_options   = options.satlegalize_options;
          sat_options.prects = v.prects;
          sat_report         = satlegalize(tech, frame, final_gp_timing, sat_options);
        }
        sat_retained = sat_report->accepted.size();
        sat_install(physical, frame);
      };
    }
    const auto write_legalize_timing = [&](std::string_view stage) {
      std::ofstream report(options.output_dir + "/legalize_timing.jsonl",
                           stage == "global_placement" ? std::ios::trunc : std::ios::app);
      report << std::format(
          R"({{"stage":"{}","wns":{},"tns":{},"violations":{},"electrical_excess":{},"delay":{},"time_unit_ps":{},"ranking_only":{}}})",
          stage,
          final_gp_timing.wns ? std::format("{}", *final_gp_timing.wns) : "null",
          final_gp_timing.tns ? std::format("{}", *final_gp_timing.tns) : "null",
          final_gp_timing.ranking_only ? "null" : std::format("{}", final_gp_timing.violations),
          final_gp_timing.ranking_only ? "null" : std::format("{}", final_gp_timing.electrical_excess),
          final_gp_timing.ranking_only ? "null" : std::format("{}", final_gp_timing.delay),
          final_gp_timing.time_unit_ps,
          final_gp_timing.ranking_only)
             << '\n';
      if (!report) {
        throw std::runtime_error("cannot write legalization timing report");
      }
    };
    if (final_timing && options.fp_evaluate && control.checkpoint) {
      const auto observer = control.observer;
      control.observer    = [&, observer](const dp::Design& physical, dp::PlacementStage stage, int iteration) {
        if (observer) {
          observer(physical, stage, iteration);
        }
        if (stage != dp::PlacementStage::legalization && stage != dp::PlacementStage::detailed_placement) {
          return;
        }
        legal_timing_snapshot   = true;
        macro_checkpoint_forced = true;
        control.checkpoint(physical, control.max_iterations);
        legal_timing_snapshot = false;
        write_legalize_timing(stage == dp::PlacementStage::legalization ? "legalization" : "detail");
      };
    }
    // FP has already completed using its cell-derived particles. Final cells
    // receive zero here because their physical macros are fixed.
    control.filler_macro_fraction = movable_macros ? options.macro_filler_fraction : 0;
    control.filler_cell_space     = movable_macros && options.macro_fill_cell_space;
    if (final_timing && (options.macro_transparency.enabled() || options.vanished_memory)) {
      control.vanished_memory                   = options.vanished_memory;
      control.vanished_memory_local_floor       = options.vanished_memory_local_floor;
      control.vanished_memory_initial_scale     = options.vanished_memory_initial_scale;
      control.vanished_memory_growth_iterations = options.vanished_memory_growth_iterations;
      control.vanished_memory_start             = options.vanished_memory_start;
      control.macro_transparency                = options.macro_transparency;
      const auto prior_observer                 = control.observer;
      const int  prior_interval                 = control.observer_interval;
      const int  snapshot_start
          = (options.macro_transparency.always || options.vanished_memory) ? 0 : options.macro_transparency.warmup;
      const auto cycle_end = (options.macro_transparency.always || options.vanished_memory)
                                 ? std::int64_t(control.max_iterations)
                                 : std::int64_t(options.macro_transparency.warmup)
                                       + 2 * std::int64_t(options.macro_transparency.period) * options.macro_transparency.cycles;
      fs::create_directories(options.output_dir + "/macro_transparency");
      std::ofstream nodes(options.output_dir + "/macro_transparency/nodes.tsv");
      nodes << "index\tname\tx_dbu\ty_dbu\twidth_dbu\theight_dbu\tmovable\tmacro\n";
      for (size_t i = 0; i < v.cells.size(); ++i) {
        nodes << std::setprecision(17) << i << '\t' << v.cells[i].name << '\t' << v.design.node_x[i] << '\t' << v.design.node_y[i]
              << '\t' << v.design.node_size_x[i] << '\t' << v.design.node_size_y[i] << '\t' << (i < size_t(v.design.num_movable))
              << '\t' << unsigned(v.design.node_macro[i]) << '\n';
      }
      if (!nodes) {
        throw std::runtime_error("cannot write macro transparency node map");
      }
      control.observer_interval = std::gcd(prior_interval, options.macro_transparency.period);
      if (snapshot_start > 0) {
        control.observer_interval = std::gcd(control.observer_interval, snapshot_start);
      }
      control.observer = [&, prior_observer, prior_interval, cycle_end, snapshot_start](const dp::Design&  physical,
                                                                                        dp::PlacementStage stage,
                                                                                        int                iteration) {
        if (prior_observer
            && (stage != dp::PlacementStage::global_placement || iteration % prior_interval == 0
                || iteration == control.max_iterations)) {
          prior_observer(physical, stage, iteration);
        }
        if (stage != dp::PlacementStage::global_placement || iteration < snapshot_start || iteration > cycle_end
            || (iteration - snapshot_start) % options.macro_transparency.period) {
          return;
        }
        // Local diagnostic format: native-endian doubles, all movable x then y;
        // node order and physical sizes are recorded once in nodes.tsv.
        std::ofstream positions(options.output_dir + "/macro_transparency/iteration" + std::to_string(iteration) + ".bin",
                                std::ios::binary);
        const auto    bytes = std::streamsize(physical.num_movable) * sizeof(double);
        positions.write(reinterpret_cast<const char*>(physical.node_x.data()), bytes);
        positions.write(reinterpret_cast<const char*>(physical.node_y.data()), bytes);
        if (!positions) {
          throw std::runtime_error("cannot write macro transparency positions");
        }
      };
    }
    if (final_timing && options.gp_observer && control.max_iterations > 0) {
      fs::create_directories(options.output_dir);
      const auto prior          = control.observer;
      const int  prior_interval = control.observer_interval;
      control.observer_interval = std::gcd(prior_interval, options.gp_observer_interval);
      int last_observed         = -1;
      // Keep duplicate-suppression state alive with the callback itself.
      control.observer = [&,
                          prior,
                          prior_interval,
                          last_observed,
                          rc_cache = options.gp_observer_rc_reuse
                                         ? std::make_shared<Rc_cache>()
                                         : nullptr](const dp::Design& physical, dp::PlacementStage stage, int iteration) mutable {
        if (prior
            && (stage != dp::PlacementStage::global_placement || iteration % prior_interval == 0
                || iteration == control.max_iterations)) {
          prior(physical, stage, iteration);
        }
        if (stage != dp::PlacementStage::global_placement || iteration <= 0 || iteration % options.gp_observer_interval
            || iteration == last_observed) {
          return;
        }
        last_observed = iteration;
        auto view     = v;
        view.design   = physical;
        // Node handles share the private placement graph. Restore annotations
        // even if the observer throws to terminate a diagnostic run.
        struct Restore {
          std::vector<std::pair<Node, std::optional<Ann_place>>> poses;
          ~Restore() {
            for (auto& [node, pose] : poses) {
              if (pose) {
                node.attr(attrs::place).set(*pose);
              } else {
                node.attr(attrs::place).del();
              }
            }
          }
        } restore;
        restore.poses.reserve(view.cells.size());
        for (size_t i = 0; i < view.cells.size(); ++i) {
          auto&      cell = view.cells[i];
          const auto attr = cell.node.attr(attrs::place);
          restore.poses.emplace_back(cell.node, attr.has() ? std::optional<Ann_place>(attr.get()) : std::nullopt);
          cell.place.x = std::llround(physical.node_x[i]);
          cell.place.y = std::llround(physical.node_y[i]);
          cell.node.attr(attrs::place).set(cell.place);
        }
        Result checkpoint;
        checkpoint.prects      = v.prects;
        checkpoint.graph       = result.graph;
        checkpoint.fp_library  = result.fp_library;
        checkpoint.net_weights = v.weights;
        snapshot_frame(checkpoint.floorplan, view);
        auto rc_options = options;
        rc_options.full = false;
        Rc_reuse_stats reuse;
        checkpoint.parasitics = estimate_rc(view, tech, rc_options, &checkpoint.pin_locations, rc_cache.get(), &reuse);
        if (options.gp_observer_rc_verify) {
          std::map<std::string, std::pair<double, double>> fresh_locations;
          const auto                                       fresh = estimate_rc(view, tech, rc_options, &fresh_locations);
          if (fresh->nets != checkpoint.parasitics->nets || fresh->identity != checkpoint.parasitics->identity
              || fresh->approximate != checkpoint.parasitics->approximate || fresh_locations != checkpoint.pin_locations) {
            throw std::runtime_error("reused checkpoint RC differs from fresh full construction");
          }
        }
        record_poses(*checkpoint.parasitics, view);
        Gp_observation sample;
        if (rc_cache) {
          sample.rc_reuse          = reuse;
          sample.rc_reuse_verified = options.gp_observer_rc_verify;
        }
        sample.iteration        = iteration;
        sample.hpwl_um          = dp::hpwl(physical) / tech.dbu;
        sample.density_overflow = dp::density_overflow(physical, options.bins, options.bins, options.density);
        sample.macro_scale = options.vanished_memory ? dp::vanished_memory_scale(iteration,
                                                                                 options.vanished_memory_initial_scale,
                                                                                 options.vanished_memory_growth_iterations,
                                                                                 options.vanished_memory_start)
                                                     : (dp::macro_transparent(options.macro_transparency, iteration - 1) ? 0 : 1);
        dp::RudyOptions routing;
        routing.bins_x = routing.bins_y = route_grid_bins(control.auxiliary_bins, options.bins);
        routing.horizontal_capacity     = control.route_capacity_h;
        routing.vertical_capacity       = control.route_capacity_v;
        routing.macro_blockages = routing.pin_access = true;
        routing.model                                = dp::RudyModel::rsmt;
        routing.degree                               = control.route.degree;
        routing.accuracy                             = control.route.accuracy;
        routing.initialize                           = control.route_initialize;
        const auto opaque                            = dp::rudy(physical, routing);
        routing.fixed_macro_scale                    = sample.macro_scale;
        const auto effective                         = sample.macro_scale == 1 ? opaque : dp::rudy(physical, routing);
        sample.route_overflow                        = effective.overflow;
        sample.opaque_route_overflow                 = opaque.overflow;
        sample.route_peak                            = effective.max_utilization;
        sample.opaque_route_peak                     = opaque.max_utilization;
        sample.unroutable_nets                       = effective.unroutable_nets;
        sample.opaque_unroutable_nets                = opaque.unroutable_nets;
        sample.blocked_pins                          = effective.blocked_pins;
        sample.opaque_blocked_pins                   = opaque.blocked_pins;
        options.gp_observer(checkpoint, sample);
      };
    }
#if defined(DREAMPLACE_NATIVE_PIN_TIMING_API)
    if (final_timing && options.native_gp_timing) {
      fs::create_directories(options.output_dir);
      if (control.checkpoint) {
        throw std::invalid_argument("native GP timing cannot share a legacy checkpoint");
      }
      const auto& config = *options.native_gp_timing;
      if (options.native_gp_buffering
          && (options.native_gp_buffering->native_units_per_um != double(tech.dbu)
              || options.native_gp_buffering->site_dbu_per_um != double(tech.dbu))) {
        throw std::invalid_argument("joint native checkpoint units differ from technology DBU");
      }
      if (config.start >= control.max_iterations) {
        throw std::invalid_argument("native GP timing start exceeds the fixed GP budget");
      }
      if (options.native_gp_buffering && options.native_gp_buffering->start >= control.max_iterations) {
        throw std::invalid_argument("joint native buffering starts beyond the fixed GP budget");
      }
      const auto wire    = technology_wire_rc(tech,
                                              options.liberty,
                                              options.tech_rc_file,
                                              options.resistance_ohm_um,
                                              options.capacitance_ff_um,
                                              options.rc_pessimism,
                                              options.technology().cells.contents);
      auto       session = native_session
          = std::make_shared<timeclosure::Gp_timing_session>(*result.graph,
                                                             config,
                                                             Rc_policy{per_um(wire), double(tech.dbu), wire.source});
      // Bind every weighted timing sink to its physical pin and net driver;
      // native timing names physical pins as a top port or instance:pin.
      const auto bind_weights = [](const std::vector<Endpoint>&                          pins,
                                   const dp::Design&                                     physical,
                                   const std::map<std::string, timeclosure::Pin_weight>& weights,
                                   dp::PinTiming&                                        timing,
                                   const std::string&                                    what) {
        timing.driver.assign(physical.pin2node.size(), -1);
        timing.weight.assign(physical.pin2node.size(), 0);
        size_t matched = 0;
        for (size_t p = 0; p < pins.size(); ++p) {
          const auto found = weights.find(pins[p].label());
          if (found == weights.end()) {
            continue;
          }
          const int net = physical.pin2net[p], driver = physical.flat_net2pin[physical.flat_net2pin_start[net]];
          if (found->second.driver != pins[driver].label()) {
            throw std::invalid_argument(what + " driver differs from physical net");
          }
          timing.driver[p] = driver;
          timing.weight[p] = found->second.weight;
          ++matched;
        }
        if (matched != weights.size()) {
          throw std::invalid_argument(what + " pins lack exact physical binding");
        }
        return matched;
      };
      if (config.legal_output_guard) {
        bool legal = false;
        try {
          auto reference = v;
          reference.fp   = *imported_frame;
          std::map<std::string, const ph::Component*> original;
          for (const auto& cell : imported_frame->components) {
            original.emplace(cell.name, &cell);
          }
          reference.fp.components.clear();
          reference.terminal.clear();
          for (auto& cell : reference.cells) {
            const auto old = original.find(cell.name);
            if (old == original.end() || old->second->macro != cell.macro) {
              throw std::invalid_argument("native legal reference must preserve every signal instance/master: " + cell.name);
            }
            const auto width = cell.place.width, height = cell.place.height;
            cell.place        = old->second->place;
            cell.place.width  = width;
            cell.place.height = height;
            reference.fp.components.push_back({cell.name, cell.macro, cell.place});
          }
          check_legality(tech, reference.fp, options);
          legal = true;
          make_arrays(reference, tech);
          auto reference_options = options;
          reference_options.full = false;
          std::map<std::string, std::pair<double, double>> locations;
          const auto   rc     = estimate_rc(reference, tech, reference_options, &locations, nullptr, nullptr, false);
          const double period = session->set_legal_reference(*rc, true);
          write_legal_acceptance({.reference_period_ps = period, .reason = "final output not evaluated"}, false, true);
        } catch (const std::exception& error) {
          write_legal_acceptance({.reason = error.what()}, false, legal);
          throw;
        }
      }
      if (config.startup_coefficient > 0) {
        const auto                                       begin = std::chrono::steady_clock::now();
        std::map<std::string, std::pair<double, double>> locations;
        auto                                             rc_options = options;
        rc_options.full                                             = false;
        const auto    rc      = estimate_rc(v, tech, rc_options, &locations, nullptr, nullptr, false);
        const auto    initial = session->startup_weights(*rc);
        dp::PinTiming bias;
        bias.coefficient     = config.startup_coefficient;
        const size_t matched = bind_weights(v.pins, v.design, initial.weights.pins, bias, "startup timing");
        if (initial.supported) {
          v.design.pin_timing = std::move(bias);
        }
        std::ofstream                              report(options.output_dir + "/native_gp_startup.json");
        rapidjson::StringBuffer                    startup_json;
        rapidjson::Writer<rapidjson::StringBuffer> startup_writer(startup_json);
        startup_writer.StartObject();
        startup_writer.Key("coefficient");
        startup_writer.Double(config.startup_coefficient);
        startup_writer.Key("path_fraction");
        startup_writer.Double(config.startup_path_fraction);
        startup_writer.Key("supported");
        startup_writer.Bool(initial.supported);
        startup_writer.Key("reason");
        startup_writer.String(initial.reason.c_str());
        startup_writer.Key("driver_fit_failures");
        startup_writer.Uint64(initial.driver_fit_failures);
        startup_writer.Key("endpoints");
        startup_writer.Uint64(initial.endpoints);
        startup_writer.Key("selected_paths");
        startup_writer.Uint64(initial.selected);
        startup_writer.Key("weighted_pins");
        startup_writer.Uint64(matched);
        startup_writer.Key("seconds");
        startup_writer.Double(std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count());
        startup_writer.Key("rc_model");
        startup_writer.String("mean_wire_technology_prior");
        startup_writer.Key("mean_source");
        startup_writer.String(config.startup_average_wire_um ? "explicit_prior" : "current_wire_average");
        startup_writer.Key("mean_branch_length_um");
        if (initial.mean_branch_length_um) {
          startup_writer.Double(*initial.mean_branch_length_um);
        } else {
          startup_writer.Null();
        }
        startup_writer.Key("fixed_until_native_feedback");
        startup_writer.Bool(true);
        startup_writer.EndObject();
        report << startup_json.GetString() << '\n';
        if (!report) {
          throw std::runtime_error("cannot write native startup timing report");
        }
      }
      auto rc_cache = std::make_shared<Rc_cache>();
      auto trace    = std::make_shared<std::ofstream>(options.output_dir + "/native_gp_timing.jsonl");
      if (!*trace) {
        throw std::runtime_error("cannot create native GP timing trace");
      }
#if DREAMPLACE_NATIVE_PIN_TIMING_API >= 2
      control.checkpoint_rollback = config.continuing_rollback;
#else
      if (config.continuing_rollback || config.maximum_density_overflow) {
        throw std::invalid_argument("native GP rollback/density guards require the typed checkpoint transaction API");
      }
#endif
      control.checkpoint_interval = config.interval;
      control.checkpoint_start    = config.start;
      control.checkpoint_final    = true;
      const auto first = std::max<int64_t>(1, (int64_t(config.start) + config.interval - 1) / config.interval) * config.interval;
      control.min_iterations = std::max(control.min_iterations, int(std::min<int64_t>(control.max_iterations, first)));
      auto buffering_state   = std::make_shared<std::optional<buffering::Checkpoint_state>>();
      control.checkpoint = [&, session, rc_cache, trace, buffering_state, bind_weights](const dp::Design& physical, int iteration) {
        auto view       = v;
        view.design     = physical;
        auto rc_options = options;
        rc_options.full = false;
        std::map<std::string, std::pair<double, double>> locations;
        const auto rc = estimate_rc(view, tech, rc_options, &locations, rc_cache.get(), nullptr, false);
        if (options.native_gp_buffering) {
          for (size_t i = 0; i < view.cells.size(); ++i) {
            auto& cell        = view.cells[i];
            cell.place.x      = std::llround(physical.node_x[i]);
            cell.place.y      = std::llround(physical.node_y[i]);
            cell.place.width  = std::llround(physical.node_size_x[i]);
            cell.place.height = std::llround(physical.node_size_y[i]);
            rc->cells.push_back({cell.name,
                                 cell.place.x,
                                 cell.place.y,
                                 static_cast<unsigned>(cell.place.orientation),
                                 cell.macro,
                                 cell.place.width,
                                 cell.place.height});
          }
          for (const auto& t : view.fp.terminals) {
            const auto p = locations.find(t.name);
            rc->terminals.push_back({t.name,
                                     p == locations.end() ? t.x : std::llround(p->second.first),
                                     p == locations.end() ? t.y : std::llround(p->second.second)});
          }
        }
        double density = dp::density_overflow(physical, options.bins, options.bins, options.density);
#if DREAMPLACE_NATIVE_DENSITY_RESERVATION_API >= 2
        if (options.native_gp_buffering && !physical.density_reservations.empty())
          density = dp::reservation_density_overflow(physical, options.bins, options.bins, options.density);
#endif
        dp::PinTiming                               weights;
        size_t                                      matched = 0;
        std::optional<buffering::Checkpoint_result> combined;
        const auto prepare = [&](const late::Result& fresh_timing, const timeclosure::Pin_weights& fresh_weights) {
          weights.coefficient = options.native_gp_timing->coefficient;
          matched             = bind_weights(view.pins, physical, fresh_weights.pins, weights, "native timing");

#if DREAMPLACE_NATIVE_DENSITY_RESERVATION_API >= 1 && DREAMPLACE_NATIVE_PIN_TIMING_API >= 3
          if (options.native_gp_buffering && iteration >= options.native_gp_buffering->start) {
            if (control.vanished_memory && iteration < control.vanished_memory_start + control.vanished_memory_growth_iterations) {
              throw std::invalid_argument("joint reservation refresh requires full macro exclusion");
            }
            std::map<std::string, int> drivers;
            for (size_t n = 0; n < physical.net_weights.size(); ++n) {
              if (physical.flat_net2pin_start[n] == physical.flat_net2pin_start[n + 1])
                continue;
              const int p = physical.flat_net2pin[physical.flat_net2pin_start[n]];
              drivers.emplace(view.pins[p].label(), p);
            }
            const buffering::Checkpoint_input input{physical,
                                                    fresh_timing,
                                                    fresh_weights,
                                                    *rc,
                                                    locations,
                                                    iteration,
                                                    buffering_state->has_value() ? &buffering_state->value() : nullptr};
            const auto                        problem = options.native_gp_buffering->generate(input);
            for (const auto& [instance_name, node] : problem.materialized) {
              if (node < 0 || size_t(node) >= view.cells.size() || view.cells[node].name != instance_name)
                throw std::invalid_argument("materialized checkpoint binding differs from actual physical instance identity");
            }
            combined = buffering::prepare_checkpoint(input,
                                                     problem,
                                                     *buffering_state,
                                                     *config.models,
                                                     weights,
                                                     drivers,
                                                     config.driver,
                                                     *options.native_gp_buffering);
            if (!combined->supported_api)
              throw std::invalid_argument("joint checkpoint unavailable on this native runtime");
            weights = combined->forces.timing;
          }
#else
          (void)fresh_timing;
#endif
        };
        try {
          const auto feedback = session->update(
              *rc,
              locations,
              iteration,
              density,
              [&](const auto& rejected) {
                const auto suffix = rejected.attempt ? "_attempt_" + std::to_string(rejected.attempt) : "";
                timeclosure::write_gp_timing_rejection(
                    options.output_dir + "/native_gp_rejected_" + std::to_string(iteration) + suffix,
                    rejected,
                    *rc,
                    locations,
                    iteration,
                    density);
              },
              prepare);
          if (feedback.recalibrated_after_mismatch) {
            rapidjson::StringBuffer                    buffer;
            rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
            json.StartObject();
            json.Key("iteration");
            json.Int(iteration);
            json.Key("extra_native_evaluations");
            json.Uint(1);
            json.Key("comparison_gates_pass");
            json.Bool(true);
            json.Key("supported_coverage_pass");
            json.Bool(true);
            json.Key("calibrated_nets");
            json.Uint64(feedback.calibration.calibrated);
            json.Key("reused_nets");
            json.Uint64(feedback.calibration.reused);
            json.Key("hard_period_rollback");
            json.Bool(feedback.rollback);
            json.Key("calibration_committed");
            json.Bool(!feedback.rollback);
            json.Key("reference_period_ps");
            json.Double(feedback.reference_period_ps.value());
            json.Key("continuous_period_ps");
            json.Double(feedback.continuous_period_ps.value());
            json.Key("accepted_for_optimization");
            json.Bool(false);
            json.EndObject();
            std::ofstream report(options.output_dir + "/native_gp_recalibration.jsonl", std::ios::app);
            report << buffer.GetString() << '\n';
            if (!report) {
              throw std::runtime_error("cannot write native GP recalibration trace");
            }
          }
#if DREAMPLACE_NATIVE_PIN_TIMING_API >= 2
          if (feedback.rollback || feedback.deferred) {
            rapidjson::StringBuffer                    buffer;
            rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
            json.StartObject();
            json.Key("iteration");
            json.Int(iteration);
            json.Key("rollback");
            json.Bool(feedback.rollback);
            json.Key("deferred");
            json.Bool(feedback.deferred);
            json.Key("reason");
            json.String(feedback.reason.c_str());
            json.Key("reference_period_ps");
            if (feedback.reference_period_ps)
              json.Double(*feedback.reference_period_ps);
            else
              json.Null();
            json.Key("retained_period_ps");
            if (feedback.rollback)
              json.Double(feedback.retained_period_ps);
            else
              json.Null();
            json.Key("temperature_fraction");
            json.Double(feedback.temperature_fraction);
            json.Key("density_overflow");
            json.Double(density);
            json.Key("accepted_for_optimization");
            json.Bool(false);
            json.EndObject();
            *trace << buffer.GetString() << '\n';
            trace->flush();
            if (!*trace)
              throw std::runtime_error("cannot write native GP transaction trace");
            dp::GpUpdate update;
            if (feedback.rollback)
              update.action = dp::GpCheckpointAction::restore;
            return update;
          }
#endif
          *trace << std::format(
              R"({{"iteration":{},"reference_period_ps":{},"continuous_period_ps":{},"retained_period_ps":{},"temperature_fraction":{},"weighted_pins":{},"removed_pins":{},"related_output_path_arcs":{},"density_overflow":{},"calibrated_nets":{},"reused_nets":{},"prior_force_active":{},"accepted_for_optimization":false}})",
              iteration,
              feedback.reference_period_ps.value(),
              feedback.continuous_period_ps.value(),
              feedback.retained_period_ps,
              feedback.temperature_fraction,
              matched,
              feedback.weights.removed,
              feedback.weights.related_output_path_arcs,
              density,
              feedback.calibration.calibrated,
              feedback.calibration.reused,
              physical.pin_timing && physical.pin_timing->enabled())
                 << '\n';
          trace->flush();
          if (!*trace) {
            throw std::runtime_error("cannot write native GP timing trace");
          }
          dp::GpUpdate update;
          update.pin_timing = std::move(weights);
#if DREAMPLACE_NATIVE_DENSITY_RESERVATION_API >= 1 && DREAMPLACE_NATIVE_PIN_TIMING_API >= 3
          if (combined) {
            update.density_reservations = combined->density.proposal.footprints;
            rapidjson::StringBuffer                    buffer;
            rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
            json.StartObject();
            json.Key("iteration");
            json.Int(iteration);
            json.Key("critical_nets");
            json.Uint64(combined->critical_nets);
            json.Key("window_nets");
            json.Uint64(combined->window_nets);
            json.Key("outside_nets");
            json.Uint64(combined->outside_nets);
            json.Key("missing_contexts");
            json.Uint64(combined->missing_contexts);
            json.Key("virtual_buffers");
            json.Uint64(combined->density.virtual_buffers);
            json.Key("reserved_area_um2");
            json.Double(combined->density.virtual_area / tech.dbu / tech.dbu);
            json.Key("gap_pulls");
            json.Uint64(combined->forces.applied);
            json.Key("unbound_pulls");
            json.Uint64(combined->forces.unbound_driver);
            json.Key("virtual_driver_pulls");
            json.Uint64(combined->forces.virtual_driver);
            json.Key("input_search_complete");
            json.Bool(combined->negotiation->input_search_complete);
            json.Key("graph_edits");
            json.Bool(false);
            json.Key("accepted_for_optimization");
            json.Bool(false);
            json.EndObject();
            std::ofstream journal(options.output_dir + "/native_gp_buffering.jsonl", std::ios::app);
            journal << buffer.GetString() << '\n';
            if (!journal)
              throw std::runtime_error("cannot write joint native checkpoint trace");
            *buffering_state = std::move(combined->proposal);
          }
#endif
#if DREAMPLACE_NATIVE_PIN_TIMING_API >= 2
          if (options.native_gp_timing->continuing_rollback) {
            update.action = dp::GpCheckpointAction::retain;
          }
#endif
          return update;
        } catch (const std::exception& error) {
          rapidjson::StringBuffer                    buffer;
          rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
          json.StartObject();
          json.Key("iteration");
          json.Int(iteration);
          json.Key("rejected");
          json.Bool(true);
          json.Key("reason");
          json.String(error.what());
          json.Key("accepted_for_optimization");
          json.Bool(false);
          json.EndObject();
          *trace << buffer.GetString() << '\n';
          trace->flush();
          throw;
        }
      };
      if (config.legalize_timing_enabled()) {
        const auto prior        = control.before_legalize;
        const auto strength     = config.legalization;
        const auto driver       = config.driver;
        control.before_legalize = [&, session, prior, strength, driver](dp::Design& physical) {
          if (prior) {
            prior(physical);
          }
          const auto started = std::chrono::steady_clock::now();
          auto       view    = v;
          view.design        = physical;
          auto rc_options    = options;
          rc_options.full    = false;
          std::map<std::string, std::pair<double, double>> locations;
          const auto                                       rc    = estimate_rc(view, tech, rc_options, &locations);
          const auto                                       fresh = session->legalization_weights(*rc);
          timeclosure::Legalization_binding                binding;
          binding.nodes        = physical.node_x.size();
          binding.movable      = physical.num_movable;
          binding.node_weights = physical.node_legalize_weights;
          binding.net_weights  = physical.net_weights;
          if (view.pins.size() != physical.pin2node.size()) {
            throw std::invalid_argument("native legalization physical pin census differs");
          }
          for (size_t pin = 0; pin < view.pins.size(); ++pin) {
            binding.pins.push_back({view.pins[pin].label(), size_t(physical.pin2node[pin]), size_t(physical.pin2net[pin])});
          }
          for (size_t net = 0; net + 1 < physical.flat_net2pin_start.size(); ++net) {
            const auto begin = physical.flat_net2pin_start.at(net);
            if (begin == physical.flat_net2pin_start.at(net + 1)) {
              throw std::invalid_argument("native legalization net has no physical driver");
            }
            binding.drivers.push_back(physical.flat_net2pin.at(begin));
          }
          auto          priorities = timeclosure::legalization_priorities(binding, fresh.weights, strength);
          std::ofstream cells(options.output_dir + "/native_legalize_cells.tsv");
          cells << "node\tcell\tcriticality\tweight\tgp_x_dbu\tgp_y_dbu\n" << std::setprecision(17);
          for (size_t i = 0; i < v.cells.size(); ++i) {
            const double weight = priorities.node_weights.empty() ? 1 : priorities.node_weights.at(i);
            cells << i << '\t' << v.cells[i].name << '\t' << priorities.node_criticality.at(i) << '\t' << weight << '\t'
                  << physical.node_x.at(i) << '\t' << physical.node_y.at(i) << '\n';
          }
          std::ofstream nets(options.output_dir + "/native_legalize_nets.tsv");
          nets << "net\tdriver\tcriticality\tbase_weight\tweight\n" << std::setprecision(17);
          for (size_t i = 0; i < binding.drivers.size(); ++i) {
            const double base_weight = binding.net_weights.empty() ? 1 : binding.net_weights[i];
            const double weight      = priorities.net_weights.empty() ? 1 : priorities.net_weights[i];
            nets << i << '\t' << binding.pins[binding.drivers[i]].name << '\t' << priorities.net_criticality[i] << '\t'
                 << base_weight << '\t' << weight << '\n';
          }
          rapidjson::StringBuffer                    buffer;
          rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
          json.StartObject();
          json.Key("stage");
          json.String("fresh_retained_gp_before_legalization");
          json.Key("period_ps");
          json.Double(fresh.period_ps);
          json.Key("weighted_pins");
          json.Uint64(priorities.weighted_pins);
          json.Key("protected_movable");
          json.Uint64(priorities.protected_movable);
          json.Key("protected_nets");
          json.Uint64(priorities.protected_nets);
          json.Key("displacement_strength");
          json.Double(strength.displacement);
          json.Key("net_strength");
          json.Double(strength.net);
          json.Key("gp_history_updated");
          json.Bool(false);
          json.Key("extra_gp_iterations");
          json.Int(0);
          json.Key("characterized_driver_boundary");
          json.Bool(driver.characterized_boundary);
          json.Key("bounded_driver_boundary");
          json.Bool(driver.bounded_boundary);
          json.Key("timing_seconds");
          json.Double(std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
          json.Key("accepted_for_optimization");
          json.Bool(false);
          json.EndObject();
          std::ofstream report(options.output_dir + "/native_legalize_timing.json");
          report << buffer.GetString() << '\n';
          if (!cells || !nets || !report) {
            throw std::runtime_error("cannot write native legalization priorities");
          }
          physical.node_legalize_weights = std::move(priorities.node_weights);
          physical.net_weights           = std::move(priorities.net_weights);
          if (strength.displacement > 0) {
            physical.node_legalize_anchor_x = physical.node_x;
            physical.node_legalize_anchor_y = physical.node_y;
          }
        };
        const auto previous_observer = control.observer;
        control.observer = [&, session, previous_observer](const dp::Design& physical, dp::PlacementStage stage, int iteration) {
          if (previous_observer) {
            previous_observer(physical, stage, iteration);
          }
          if (stage != dp::PlacementStage::legalization && stage != dp::PlacementStage::detailed_placement) {
            return;
          }
          const auto started = std::chrono::steady_clock::now();
          auto       view    = v;
          view.design        = physical;
          auto rc_options    = options;
          rc_options.full    = false;
          std::map<std::string, std::pair<double, double>> locations;
          const auto                                       rc       = estimate_rc(view, tech, rc_options, &locations);
          const bool                                       legal    = dp::is_legal(physical);
          const auto                                       observed = session->check_legal_output(*rc, legal);
          rapidjson::StringBuffer                          buffer;
          rapidjson::Writer<rapidjson::StringBuffer>       json(buffer);
          json.StartObject();
          json.Key("stage");
          json.String(stage == dp::PlacementStage::legalization ? "legalization" : "detail");
          json.Key("iteration");
          json.Int(iteration);
          json.Key("legal");
          json.Bool(legal);
          json.Key("period_ps");
          if (observed.candidate_period_ps) {
            json.Double(*observed.candidate_period_ps);
          } else {
            json.Null();
          }
          json.Key("reference_period_ps");
          json.Double(observed.reference_period_ps.value());
          json.Key("native_nonregression_pass");
          json.Bool(observed.passed());
          json.Key("reason");
          json.String(observed.reason.c_str());
          json.Key("supported_gp_updates");
          json.Uint64(observed.supported_gp_updates);
          json.Key("response_interval_complete");
          json.Bool(observed.response_interval_complete);
          json.Key("gp_history_updated");
          json.Bool(false);
          json.Key("extra_gp_iterations");
          json.Int(0);
          json.Key("timing_seconds");
          json.Double(std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
          json.Key("accepted_for_optimization");
          json.Bool(false);
          json.EndObject();
          std::ofstream report(options.output_dir + "/native_legalize_stages.jsonl",
                               stage == dp::PlacementStage::legalization ? std::ios::trunc : std::ios::app);
          report << buffer.GetString() << '\n';
          if (!report) {
            throw std::runtime_error("cannot write native legalization stage timing");
          }
        };
      }
    }
#endif
    std::optional<Cell_jump> cell_jump;
    if (final_timing && options.cell_jump.depth > 1) {
      std::vector<int> jump_macros;
      for (int i = v.design.num_movable; i < int(v.cells.size()); ++i) {
        if (v.design.node_macro[i] && !v.cells[i].name.starts_with("__lhd_frame_mask_")) {
          jump_macros.push_back(i);
        }
      }
      cell_jump.emplace(options.cell_jump, std::move(jump_macros));
      control.movable_interval = std::gcd(options.cell_jump.settle, options.cell_jump.interval);
      if (!control.checkpoint) {
        control.min_iterations = std::max(control.min_iterations,
                                          int(options.cell_jump.settle + (options.cell_jump.depth - 2) * options.cell_jump.interval
                                              + std::max(options.cell_jump.interval, options.cell_jump.fix_period + 1)));
        if (control.min_iterations > control.max_iterations) {
          throw std::invalid_argument("cell_jump iteration budget must include the requested depth and settling interval");
        }
      }
      control.movable_update = [&](const dp::Design& snapshot, int iteration) {
        if (final_gp_timing_started) {
          return std::vector<dp::MovablePosition>{};
        }
        const auto previous_samples = cell_jump->samples().size();
        auto       moves            = cell_jump->update(snapshot, iteration);
        if (cell_jump->samples().size() != previous_samples) {
          const auto& s = cell_jump->samples().back();
          fs::create_directories(options.output_dir);
          std::ofstream report(options.output_dir + "/cell_jump.jsonl", previous_samples ? std::ios::app : std::ios::trunc);
          report << std::format(
              R"({{"iteration":{},"depth":{},"roots":{},"candidates":{},"crossing_cells":{},"moved":{},"unavailable":{},"crossing_edges_before":{},"crossing_edges_after":{},"total_displacement_um":{},"maximum_displacement_um":{}}})",
              s.iteration,
              s.depth,
              s.roots,
              s.candidates,
              s.crossing_cells,
              s.moved,
              s.unavailable,
              s.crossing_edges_before,
              s.crossing_edges_after,
              s.total_displacement / tech.dbu,
              s.maximum_displacement / tech.dbu)
                 << '\n';
          std::ofstream detail(options.output_dir + "/cell_jump_moves.tsv", previous_samples ? std::ios::app : std::ios::trunc);
          if (!previous_samples) {
            detail << "iteration\tdepth\tcell\told_x_dbu\told_y_dbu\tx_dbu\ty_dbu\tfix_period\n";
          }
          for (const auto& move : moves) {
            detail << std::setprecision(17) << iteration << '\t' << s.depth << '\t' << v.cells[move.node].name << '\t'
                   << snapshot.node_x[move.node] << '\t' << snapshot.node_y[move.node] << '\t' << move.x << '\t' << move.y << '\t'
                   << move.fix_iterations << '\n';
          }
          if (!report || !detail) {
            throw std::runtime_error("cannot write cell_jump diagnostics");
          }
        }
        return moves;
      };
    }
    std::optional<Macro_warp> macro_warp;
    if (options.macro_warp.enabled && movable_macros && !warp_memories(v.design).empty()) {
      write_warp_nodes();
      auto macro_policy       = options.macro_warp;
      macro_policy.dbu_per_um = tech.dbu;
      macro_warp.emplace(macro_policy, control.max_iterations, options.output_dir + "/macro_warp");
      const auto prior            = control.macro_step_observer;
      control.macro_step_observer = [&, prior](int iteration, double sum, bool valid) {
        macro_warp->observe_motion(iteration, sum, valid);
        if (prior) {
          prior(iteration, sum, valid);
        }
      };
      control.macro_pull_interval          = 1;
      control.macro_pull_after_density     = macro_policy.once_after_density;
      control.macro_pull_settle_iterations = macro_policy.once_after_density ? 20 : 0;
      control.macro_pull_update            = [&](const dp::Design& snapshot, int iteration) {
        auto replacement = macro_warp->refresh(snapshot, warp_memories(snapshot), iteration);
        if (macro_replay && replacement) {
          macro_replay->events.push_back({iteration, dp::MagnetEvent::pull});
        }
        return replacement;
      };
    }
    if (refinement_active && !terminal_active) {
      const auto prior        = control.before_legalize;
      control.before_legalize = [&, prior](dp::Design& physical) {
        refine_final_boundaries(physical);
        if (prior) {
          prior(physical);
        }
      };
    }
    bool zero_gp_snapshot_emitted = false;
    if (options.snapshots && control.max_iterations == 0) {
      const auto prior        = control.before_legalize;
      const auto observer     = control.observer;
      control.before_legalize = [prior, observer, &zero_gp_snapshot_emitted](dp::Design& physical) {
        if (!zero_gp_snapshot_emitted && observer) {
          observer(physical, dp::PlacementStage::global_placement, 0);
          zero_gp_snapshot_emitted = true;
        }
        if (prior) {
          prior(physical);
        }
      };
    }
    if (options.snapshots && defer_macro_cells && movable_macros && !options.macro_magnet.enabled) {
      // Read-only replay recording for the no-magnet macro-only comparison.
      // Keep the ordinary GP force state, stopping rules and legalization.
      macro_replay.emplace();
      macro_replay->layout               = v.design.layout;
      macro_replay->target_density       = control.target_density;
      macro_replay->options              = options.macro_magnet;
      auto replay_control                = control;
      // This recorder is diagnostic only. Native GP already delivers the
      // adaptive-control signal; delivering twice would reset its window.
      replay_control.macro_step_observer = nullptr;
      macro_replay_motion                = std::make_shared<dp::detail::MagnetRuntime>(replay_control);
      const auto motion_observer         = control.macro_motion_observer;
      control.macro_motion_observer =
          [motion = macro_replay_motion,
           motion_observer](const dp::Design& physical, const std::vector<double>& x, const std::vector<double>& y, int iteration) {
            motion->observe(physical, x, y, iteration);
            if (motion_observer) {
              motion_observer(physical, x, y, iteration);
            }
          };
      const auto record = [&](const dp::Design& physical, int iteration, const char* event) {
        dp::MagnetFrame frame;
        frame.iteration = iteration;
        frame.event     = event;
        for (size_t i = 0; i < physical.node_x.size(); ++i) {
          if (physical.node_macro.empty() || !physical.node_macro[i]) {
            continue;
          }
          frame.macros.push_back({int(i),
                                  physical.node_x[i],
                                  physical.node_y[i],
                                  physical.node_size_x[i],
                                  physical.node_size_y[i],
                                  physical.node_orientation.empty() ? dp::Orientation::N : physical.node_orientation[i],
                                  0,
                                  i >= size_t(physical.num_movable)});
        }
        const dp::MagnetDensity density(physical, 64, 64);
        frame.bins_x = frame.bins_y = 64;
        frame.density               = density.grid();
        macro_replay->frames.push_back(std::move(frame));
      };
      const auto observer       = control.observer;
      control.observer_interval = 20;
      control.observer          = [&, observer, record](const dp::Design& physical, dp::PlacementStage stage, int iteration) {
        record(physical, iteration, dp::stage_name(stage));
        if (observer) {
          observer(physical, stage, iteration);
        }
      };
      const auto checkpoint = control.checkpoint;
      if (checkpoint) {
        control.checkpoint = [&, checkpoint](const dp::Design& physical, int iteration) {
          auto update = checkpoint(physical, iteration);
          macro_replay->events.push_back({iteration, dp::MagnetEvent::timing});
          return update;
        };
      }
      const auto after_macro       = control.after_macro_legalize;
      control.after_macro_legalize = [&, after_macro, record](dp::Design& physical) {
        if (after_macro) {
          after_macro(physical);
        }
        const int iteration = macro_replay->frames.empty() ? 0 : macro_replay->frames.back().iteration;
        record(physical, iteration, "snap");
        macro_replay->events.push_back({iteration, dp::MagnetEvent::snap});
      };
    }
    const auto filler_budget = relative_filler_budget(v.design, control);
    fs::create_directories(options.output_dir);
    {
      std::ofstream report(options.output_dir + "/virtual_filler_budget.json");
      report << std::format(
          R"({{"policy":"relative_whitespace_charge","fraction":{},"available_charge_area_um2":{},"requested_charge_area_um2":{},"nonmacro_area_um2":{}}})",
          filler_budget.fraction,
          filler_budget.available_area / tech.dbu / tech.dbu,
          filler_budget.requested_area / tech.dbu / tech.dbu,
          filler_budget.nonmacro_area / tech.dbu / tech.dbu);
      if (!report) {
        throw std::runtime_error("cannot write relative filler budget");
      }
    }
    try {
      metrics = dp::place(v.design, control);
    } catch (const std::runtime_error&) {
      if (sat_gp) {
        v.design                 = *sat_gp;
        auto fallback            = control;
        fallback.max_iterations  = 0;
        fallback.before_legalize = {};
        sat_retained             = 0;
        metrics                  = dp::place(v.design, fallback);
      } else {
        if (cache.reused == 0) {
          throw;
        }
        // Changed blocks may need whitespace outside their old anchors. Retry
        // with reusable blocks movable; imported FIXED cells remain fixed.
        for (auto& c : v.cells) {
          c.locked = false;
        }
        cache = {0, hashes.size(), false};
        make_arrays(v, tech);
        // The retry is a fresh solve: restart the macro timing budget and trace.
        macro_checkpoints       = 0;
        macro_checkpoint_forced = false;
        macro_timed_positions.clear();
        metrics = dp::place(v.design, control);
      }
    }
#if defined(DREAMPLACE_NATIVE_PIN_TIMING_API) && DREAMPLACE_NATIVE_PIN_TIMING_API >= 2
    if (options.native_gp_timing && options.native_gp_timing->continuing_rollback) {
      std::ofstream report(options.output_dir + "/native_gp_transactions.json");
      report << std::format(R"({{"iterations":{},"selected_iteration":{},"rollback_history":[)",
                            metrics.iterations,
                            metrics.selected_iteration);
      bool first = true;
      for (const auto& event : metrics.rollback_history) {
        report << (first ? "" : ",")
               << std::format(R"({{"iteration":{},"restored_iteration":{}}})", event.iteration, event.restored_iteration);
        first = false;
      }
      report << "]}";
      if (!report) {
        throw std::runtime_error("cannot write native GP transaction report");
      }
    }
#endif
    if (macro_warp) {
      macro_warp->finish(metrics.iterations);
    }
    if (refinement_active && terminal_active) {
      refine_final_boundaries(v.design);
    }
    if (terminal_active) {
      if (options.snapshots && control.max_iterations == 0 && !zero_gp_snapshot_emitted && control.observer) {
        control.observer(v.design, dp::PlacementStage::global_placement, 0);
        zero_gp_snapshot_emitted = true;
      }
      // Geometric-only flows have no timed nets to protect. When a timer is
      // available, refresh it at the exact final GP coordinates before selection.
      if (control.checkpoint) {
        macro_checkpoint_forced = true;
        const auto update       = control.checkpoint(v.design, control.max_iterations);
        v.design.net_weights    = update.net_weights;
        write_legalize_timing("global_placement");
      }
      auto frame = sat_frame(v.design, false);
      // The two-row cells' row-access origin: the first core row before the
      // handoff taps cut rows under macros.
      const auto core_origins = multirow_cells ? first_core_rows(frame) : std::map<std::string, int64_t>{};
      if (options.handoff_taps) {
        const auto& tap_master     = tech_profile->tap_master;
        const auto& master         = tech.macros.at(tap_master);
        const auto  original_count = frame.components.size();
        frame.rows                 = cut_macro_rows(frame,
                                                    tech,
                                                    std::llround(tech_profile->tap_row_halo * frame.dbu),
                                                    std::llround(tech_profile->tap_row_halo * frame.dbu),
                                                    std::llround(2 * master.width * frame.dbu));
        auto caps                  = row_endcaps(frame, tech, tap_master);
        frame.components.insert(frame.components.end(), caps.begin(), caps.end());
        auto taps = row_taps(frame, tech, tap_master, std::llround(tech_profile->tap_distance * frame.dbu));
        frame.components.insert(frame.components.end(), taps.begin(), taps.end());
        handoff_cells.assign(frame.components.begin() + original_count, frame.components.end());
        handoff_rows = frame.rows;
      }
      auto terminal_options             = options.terminal_legalize_options;
      terminal_options.prects           = v.prects;
      terminal_options.route_drc        = options.satlegalize_route;
      terminal_options.route_vias       = options.satlegalize_route_vias ? tech_profile : nullptr;
      terminal_options.target_density   = options.density;
      terminal_options.macro_halo_x_dbu = v.halo_x;
      terminal_options.macro_halo_y_dbu = v.halo_y;
      set_terminal_grid(tech,
                        frame,
                        options,
                        route_grid_bins(control.auxiliary_bins, metrics.bins_x),
                        route_grid_bins(control.auxiliary_bins, metrics.bins_y),
                        terminal_options);
      // Anchor the two-row cells first; terminal legalization packs the
      // single-row cells around them as fixed obstacles, then they are released.
      if (multirow_cells) {
        multirow_report = anchor_multirow_cells(tech, frame, anchor_controls(options, terminal_options, core_origins));
      }
      terminal_report = terminal_legalize(tech, frame, final_gp_timing, terminal_options);
      if (multirow_report) {
        release_multirow_anchors(frame, *multirow_report);
        adopt_rail_twins(v, frame);
      }
      sat_install(v.design, frame);
      metrics.legal                               = dp::is_legal(v.design);
      metrics.hpwl                                = dp::hpwl(v.design);
      metrics.alignment.cell_legalization_seconds = terminal_report->seconds;
      if (!metrics.legal) {
        throw std::runtime_error("terminal legalization failed independent native geometry validation");
      }
      write_terminal_legalize_report(*terminal_report, frame, terminal_options, options.output_dir + "/terminal_legalize");
      if (options.handoff_taps) {
        std::ofstream report(options.output_dir + "/handoff_taps.json");
        report << std::format(
            R"({{"schema_version":1,"profile":"{}","rows":{},"fixed_components":{},"row_halo_um":{},"distance_um":{},"stage":"terminal_legalization","inner_block_cache":false}})",
            tech_profile->name,
            handoff_rows.size(),
            handoff_cells.size(),
            tech_profile->tap_row_halo,
            tech_profile->tap_distance)
               << '\n';
        if (!report) {
          throw std::runtime_error("cannot write handoff tap report");
        }
      }
      if (control.observer) {
        control.observer(v.design, dp::PlacementStage::legalization, metrics.iterations);
      }
    }
    if (final_timing && options.legalize_timing_strength > 0 && !options.satlegalize && !terminal_active) {
      for (unsigned attempt = 0; attempt < options.legalize_timing_corrections; ++attempt) {
        const auto before         = v.design;
        const auto before_metrics = metrics;
        const auto before_timing  = final_gp_timing;
        auto       correction     = control;
        correction.max_iterations = correction.min_iterations = 8;
        correction.post_density_factor                        = 0;
        correction.checkpoint_interval                        = 0;
        bool accepted                                         = false;
        try {
          auto trial = dp::place(v.design, correction);
          accepted   = trial.legal && timing_improves(before_timing, final_gp_timing)
                       && final_gp_timing.violations <= before_timing.violations
                       && final_gp_timing.electrical_excess <= before_timing.electrical_excess + 1e-9;
          if (accepted) {
            metrics.hpwl                                 = trial.hpwl;
            metrics.alignment.cell_legalization_seconds += trial.alignment.cell_legalization_seconds;
            metrics.detailed_placement_seconds          += trial.detailed_placement_seconds;
          }
        } catch (const std::runtime_error&) {
        }
        std::ofstream report(options.output_dir + "/legalize_correction.jsonl", std::ios::app);
        report << std::format(R"({{"attempt":{},"iterations":8,"accepted":{}}})", attempt, accepted) << '\n';
        if (!accepted) {
          v.design                = before;
          metrics                 = before_metrics;
          legal_timing_snapshot   = true;
          macro_checkpoint_forced = true;
          control.checkpoint(v.design, control.max_iterations);
          legal_timing_snapshot = false;
          break;
        }
      }
    }
    if (sat_gp && sat_report && sat_retained) {
      const auto accepted          = v.design;
      const auto accepted_metrics  = metrics;
      const auto accepted_feedback = final_gp_timing;
      auto       baseline          = *sat_gp;
      auto       fallback          = control;
      fallback.max_iterations      = 0;
      fallback.before_legalize     = {};
      auto       baseline_metrics  = dp::place(baseline, fallback);
      const auto baseline_feedback = final_gp_timing;
      const auto accepted_timing   = [&](const Timing_feedback& after) {
        return after.violations <= baseline_feedback.violations
               && after.electrical_excess <= baseline_feedback.electrical_excess + 1e-9
               && (!baseline_feedback.wns || (after.wns && *after.wns >= *baseline_feedback.wns - 1e-9))
               && (!baseline_feedback.tns || (after.tns && *after.tns >= *baseline_feedback.tns - 1e-9))
               && ((!options.satlegalize_options.route_grid && options.satlegalize_options.region_width_um <= 0)
                   || !baseline_feedback.hold_wns || (after.hold_wns && *after.hold_wns >= *baseline_feedback.hold_wns - 1e-9));
      };
      bool route_accepted = true;
      if (sat_route_options) {
        auto scored_accepted = accepted, scored_baseline = baseline;
        // Hold timing weights fixed while comparing geometry, including demand
        // outside the SAT neighborhood. STA callbacks may refresh weights.
        scored_accepted.net_weights = scored_baseline.net_weights = sat_gp->net_weights;
        const auto a                                              = dp::rudy(scored_accepted, *sat_route_options);
        const auto b                                              = dp::rudy(scored_baseline, *sat_route_options);
        route_accepted = satlegalize_route_preserved({a.overflow, a.max_utilization, a.blocked_pins, a.unroutable_nets},
                                                     {b.overflow, b.max_utilization, b.blocked_pins, b.unroutable_nets},
                                                     options.satlegalize_options.route_peak_floor);
        std::ofstream file(options.output_dir + "/sat_route_guard.json");
        file << std::format(
            R"({{"accepted":{},"peak_floor":{},"scope":"full post-legalization geometry with common pre-SAT weights","candidate":{{"overflow":{},"peak":{},"blocked_pins":{},"unroutable_nets":{}}},"baseline":{{"overflow":{},"peak":{},"blocked_pins":{},"unroutable_nets":{}}}}})",
            route_accepted,
            options.satlegalize_options.route_peak_floor,
            a.overflow,
            a.max_utilization,
            a.blocked_pins,
            a.unroutable_nets,
            b.overflow,
            b.max_utilization,
            b.blocked_pins,
            b.unroutable_nets)
             << '\n';
        if (!file) {
          throw std::runtime_error("cannot write SAT route guard");
        }
      }
      const bool alignments_preserved = satlegalize_preserved(tech, sat_frame(accepted, true), *sat_report);
      const bool timing_accepted      = accepted_timing(accepted_feedback);
      if (options.snapshots) {
        // Keep both evaluated geometries even when the native guard rejects
        // the proposal. These are diagnostic poses, never accepted outputs.
        // A routing experiment must bind them to the pre-SAT topology and
        // audit masters, fixed geometry and IOs before constructing a DEF.
        const auto snapshot = [&](const dp::Design& design, bool legal, const char* name) {
          const auto                                 frame = sat_frame(design, true);
          rapidjson::StringBuffer                    buffer;
          rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
          json.StartObject();
          json.Key("schema");
          json.Int(1);
          json.Key("scope");
          json.String("post-legalization SAT comparison; diagnostic poses, not accepted placement or topology");
          json.Key("legal");
          json.Bool(legal);
          json.Key("dbu_per_um");
          json.Int64(frame.dbu);
          json.Key("component_columns");
          json.StartArray();
          for (const char* column : {"name", "master", "x", "y", "orientation"}) {
            json.String(column);
          }
          json.EndArray();
          json.Key("components");
          json.StartArray();
          for (const auto& cell : frame.components) {
            json.StartArray();
            json.String(cell.name.c_str());
            json.String(cell.macro.c_str());
            json.Int64(cell.place.x);
            json.Int64(cell.place.y);
            json.String(ph::orientation_name(cell.place.orientation).c_str());
            json.EndArray();
          }
          json.EndArray();
          json.Key("terminal_columns");
          json.StartArray();
          for (const char* column : {"name", "x", "y", "orientation"}) {
            json.String(column);
          }
          json.EndArray();
          json.Key("terminals");
          json.StartArray();
          for (const auto& terminal : frame.terminals) {
            json.StartArray();
            json.String(terminal.name.c_str());
            json.Int64(terminal.x);
            json.Int64(terminal.y);
            json.String(ph::orientation_name(terminal.orientation).c_str());
            json.EndArray();
          }
          json.EndArray();
          json.EndObject();
          std::ofstream file(options.output_dir + "/" + name);
          file << buffer.GetString() << '\n';
          if (!file) {
            throw std::runtime_error("cannot write SAT comparison poses");
          }
        };
        snapshot(accepted, accepted_metrics.legal, "sat_proposal_positions.json");
        snapshot(baseline, baseline_metrics.legal, "sat_baseline_positions.json");
      }
      const auto feedback_json = [](const Timing_feedback& f) {
        const auto value = [](const auto& x) { return x ? std::format("{}", *x) : "null"; };
        return std::format(R"({{"wns":{},"tns":{},"hold_wns":{},"violations":{},"electrical_excess":{}}})",
                           value(f.wns),
                           value(f.tns),
                           value(f.hold_wns),
                           f.violations,
                           f.electrical_excess);
      };
      std::ofstream validation(options.output_dir + "/sat_validation.json");
      validation << std::format(
          R"({{"route_accepted":{},"alignments_preserved":{},"timing_accepted":{},"candidate":{},"baseline":{}}})",
          route_accepted,
          alignments_preserved,
          timing_accepted,
          feedback_json(accepted_feedback),
          feedback_json(baseline_feedback))
                 << '\n';
      if (!validation) {
        throw std::runtime_error("cannot write SAT validation report");
      }
      if (route_accepted && alignments_preserved && timing_accepted) {
        v.design = accepted;
        metrics  = accepted_metrics;
      } else {
        const size_t total = sat_report->accepted.size();
        sat_retained       = 0;
        v.design           = std::move(baseline);
        metrics            = std::move(baseline_metrics);
        // The fixed-region experiment accepts the complete proposal or restores
        // the ordinary legal baseline. It does not retry smaller prefixes.
        // Legacy windows retain their existing bounded prefix relaxation.
        for (size_t prefix : {total / 2, (3 * total) / 4}) {
          if (options.satlegalize_options.region_width_um > 0 || options.satlegalize_options.route_grid) {
            break;
          }
          if (!prefix) {
            continue;
          }
          auto candidate = *sat_gp;
          auto frame     = sat_frame(candidate, false);
          satlegalize_restore(frame, *sat_report, prefix);
          sat_install(candidate, frame);
          try {
            auto candidate_metrics = dp::place(candidate, fallback);
            if (!satlegalize_preserved(tech, sat_frame(candidate, true), *sat_report, prefix)
                || !accepted_timing(final_gp_timing)) {
              continue;
            }
            v.design     = std::move(candidate);
            metrics      = std::move(candidate_metrics);
            sat_retained = prefix;
          } catch (const std::runtime_error&) {
          }
        }
      }
      // Rejected baseline/prefix analyses changed the persistent timer. Restore
      // it to the accepted graph/coordinates before further repair consumes it.
      legal_timing_snapshot   = true;
      macro_checkpoint_forced = true;
      control.checkpoint(v.design, control.max_iterations);
      legal_timing_snapshot = false;
    }
    capture(v, tech, options.full && !defer_macro_cells);
  } else {
    metrics.legal     = dp::is_legal(v.design);
    metrics.converged = false;
    capture(v, tech, options.full && !defer_macro_cells);
  }
  if (options.full && !options.timing_seed && !options.retain_placement && !options.analyze_colors_only && !terminal_active) {
    align_macro_geometry(v, tech, v.design, false, true);
    if (!defer_macro_cells) {
      localize_tie_sites(v, tech, options.technology().cells, options.preserve_imported_ties);
    }
    metrics.legal = dp::is_legal(v.design);
    metrics.hpwl  = dp::hpwl(v.design);
  }
  if (boundary_pins) {
    for (const auto& sample : boundary_pins->samples()) {
      if (sample.stage == "after_gp") {
        metrics.overflow                  = sample.overflow;
        metrics.global_placement_seconds += sample.seconds;
      }
    }
    boundary_pins->observe(v.design, "after_legalization");
  }
  if (bus_order) {
    for (const auto& sample : bus_order->samples()) {
      if (sample.stage == "after_gp") {
        metrics.overflow                  = sample.overflow;
        metrics.global_placement_seconds += sample.seconds;
      }
    }
    bus_order->observe(v.design, "after_legalization");
  }
  if (sat_report) {
    auto final_frame = v.fp;
    final_frame.components.clear();
    for (const auto& c : v.cells) {
      final_frame.components.push_back({c.name, c.macro, c.place});
    }
    // Final tie relocation may exchange a functional user with a tie. Explicitly
    // relax any affected alignment rather than persisting an obsolete promise.
    if (sat_retained && !satlegalize_preserved(tech, final_frame, *sat_report, sat_retained)) {
      sat_retained = 0;
    }
    std::ofstream report(options.output_dir + "/satlegalize.json");
    const auto    summary = std::format(
        R"({{"backend":"{}","encoding":"{}","windows":{},"trials":{},"accepted":{},"retained":{},"unknown":{},"conflicts":{},"oversized":{},"boundary_rejected":{},"hpwl_before":{},"hpwl_after":{},"seconds":{},"fallback":{}}})",
        options.satlegalize_options.backend,
        options.satlegalize_options.encoding,
        sat_report->windows,
        sat_report->trials,
        sat_report->accepted.size(),
        sat_retained,
        sat_report->unknown,
        sat_report->conflicts,
        sat_report->oversized,
        sat_report->boundary_rejected,
        sat_report->hpwl_before,
        sat_report->hpwl_after,
        sat_report->seconds,
        sat_retained == 0 ? "true" : "false");
    rapidjson::Document document;
    document.Parse(summary.c_str());
    sat_window_statistics(document, *sat_report, options.satlegalize_options, document.GetAllocator());
    rapidjson::StringBuffer                    buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    document.Accept(writer);
    report << buffer.GetString() << '\n';
    if (!report) {
      throw std::runtime_error("cannot write satlegalize report");
    }
  }
  fs::create_directories(options.output_dir);
  if (options.fp_place) {
    fp_final = summarize_centroids(v.design);
    rapidjson::StringBuffer                    buffer;
    rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
    json.StartObject();
    json.Key("mode");
    json.String(options.iterations == 0 ? "fp-direct-macro-frame"
                                        : (options.fp_snap_enabled ? "fp-snap-frozen-targets" : "fp-unsnapped-frozen-targets"));
    json.Key("snap_enabled");
    json.Bool(options.fp_snap_enabled);
    json.Key("fixed_targets_enabled");
    json.Bool(options.iterations > 0 && options.fp_anchor > 0 && !fp_before.empty());
    json.Key("refinement_iterations");
    json.Int(metrics.iterations);
    json.Key("coordinate_units");
    json.String("DBU");
    json.Key("bins");
    json.Int(options.bins);
    json.Key("minimum_equivalent_cells");
    json.Double(options.fp_minimum);
    json.Key("color_candidates_enabled");
    json.Bool(v.color_centroids);
    json.Key("color_source");
    json.String(options.fp_color_source.c_str());
    json.Key("color_minimum_equivalent_cells");
    json.Double(v.color_minimum);
    json.Key("pin_adjust_enabled");
    json.Bool(options.fp_pin_adjust);
    json.Key("pin_interval");
    json.Int(options.fp_pin_interval);
    json.Key("pin_pitch_dbu");
    json.Double(io_pitch);
    json.Key("pin_updates");
    json.Uint64(fp_pin_history.size());
    json.Key("macro_pin_adjust_enabled");
    json.Bool(options.macro_pin_adjust);
    json.Key("macro_pin_updates");
    json.Uint64(macro_pin_history.size());
    json.Key("minimum_copies_per_parent");
    json.Int(options.fp_min_copies);
    json.Key("minimum_macros");
    json.Int(options.fp_min_macros);
    json.Key("copy_scope");
    json.String("immediate_hierarchy_parent");
    json.Key("target_density");
    json.Double(options.density);
    json.Key("attraction");
    json.Double(options.fp_attraction);
    json.Key("initial_repulsion_weight");
    json.Double(options.fp_repulsion);
    json.Key("fixed_anchor_macros_only");
    json.Bool(options.fp_anchor_macros_only);
    json.Key("fixed_anchor_weight");
    json.Double(options.fp_anchor);
    json.Key("distance_attraction");
    json.Bool(options.fp_distance_attraction);
    json.Key("attraction_outside");
    json.Double(options.fp_attraction_outside);
    json.Key("centroid_parent_weight");
    json.Double(options.fp_parent_weight);
    json.Key("centroid_parent_outside");
    json.Double(options.fp_parent_outside);
    json.Key("centroid_parent_seconds");
    json.Double(fp_metrics.centroid.parent_seconds);
    json.Key("macro_centroid_parent_seconds");
    json.Double(metrics.centroid.parent_seconds);
    json.Key("centroid_align_weight");
    json.Double(options.fp_align_weight);
    json.Key("centroid_align_sigma");
    json.Double(options.fp_align_sigma);
    json.Key("centroid_align_start");
    json.Int(options.fp_align_start);
    json.Key("centroid_align_full");
    json.Int(options.fp_align_full);
    json.Key("centroid_align_seconds");
    json.Double(fp_metrics.centroid.alignment_seconds);
    json.Key("macro_centroid_align_seconds");
    json.Double(metrics.centroid.alignment_seconds);
    json.Key("fp_attraction_coefficient");
    json.Double(fp_metrics.centroid.attraction_coefficient);
    json.Key("fp_attraction_calibration_iteration");
    json.Int(fp_metrics.centroid.attraction_calibration_iteration);
    json.Key("macro_attraction_coefficient");
    json.Double(metrics.centroid.attraction_coefficient);
    json.Key("macro_attraction_calibration_iteration");
    json.Int(metrics.centroid.attraction_calibration_iteration);
    json.Key("refinement_repulsion_weight");
    json.Double(control.centroid_repulsion_weight);
    json.Key("placement_cache_reused");
    json.Bool(false);
    json.Key("fp_iterations");
    json.Int(fp_metrics.iterations);
    json.Key("fp_virtual_filler_count");
    json.Int(fp_metrics.filler_count);
    json.Key("fp_virtual_filler_area_um2");
    json.Double(fp_metrics.filler_area / tech.dbu / tech.dbu);
    json.Key("fp_gp_seconds");
    json.Double(fp_metrics.global_placement_seconds);
    json.Key("fp_poisson_seconds");
    json.Double(fp_metrics.poisson_seconds);
    json.Key("fp_macro_cell_density_seconds");
    json.Double(fp_metrics.macro_cell_density_seconds);
    json.Key("fp_macro_cell_density_force");
    json.Double(fp_metrics.macro_cell_density_force);
    json.Key("fp_macro_density_pull_seconds");
    json.Double(fp_metrics.macro_density_pull_seconds);
    json.Key("fp_macro_density_pull_force");
    json.Double(fp_metrics.macro_density_pull_force);
    json.Key("fp_macro_density_pull_anchor_ratio");
    json.Double(fp_metrics.macro_density_pull_anchor_ratio);
    json.Key("fp_macro_routing_seconds");
    json.Double(fp_metrics.macro_routing_seconds);
    json.Key("fp_rudy_seconds");
    json.Double(fp_metrics.rudy_seconds);
    json.Key("fp_rudy_calls");
    json.Int(fp_metrics.rudy_calls);
    json.Key("fp_overflow");
    json.Double(fp_metrics.overflow);
    json.Key("fp_converged");
    json.Bool(fp_metrics.converged);
    json.Key("routability_macro_weight");
    json.Double(options.routability_macro_weight);
    json.Key("routability_macro_range");
    json.Double(options.routability_macro_range);
    json.Key("fp_routing_history");
    json.StartArray();
    for (const auto& sample : fp_metrics.history) {
      json.StartObject();
      json.Key("iteration");
      json.Int(sample.iteration);
      json.Key("route_peak_utilization");
      json.Double(sample.route_peak_utilization);
      json.Key("macro_routing_force");
      json.Double(sample.macro_routing_force);
      json.Key("macro_routing_channels");
      json.Uint64(sample.macro_routing_channels);
      json.EndObject();
    }
    json.EndArray();
    json.Key("fp_density_ready_iteration");
    json.Int(fp_metrics.density_iteration);
    json.Key("fp_continuation_deadline");
    json.Int(fp_metrics.continuation_deadline);
    json.Key("fp_continuation_limit");
    json.Bool(fp_metrics.continuation_limit);
    json.Key("fp_timing_observed");
    json.Bool(fp_metrics.timing_observed);
    json.Key("fp_timing_satisfied_at_last_checkpoint");
    json.Bool(fp_metrics.timing_satisfied);
    json.Key("timing_interval");
    json.Int(options.fp_timing_interval);
    json.Key("timing_fraction");
    json.Double(options.timing_fraction);
    json.Key("timing_exponent");
    json.Double(options.timing_exponent);
    json.Key("timing_spread_weight");
    json.Double(options.timing_spread_weight);
    json.Key("timing_max_weight");
    json.Double(options.timing_max_weight);
    json.Key("timing_smoothing");
    json.Double(options.timing_smoothing);
    json.Key("timing_max_updates");
    json.Int(options.fp_timing_max_updates);
    json.Key("initial_density_weight");
    json.Double(options.initial_density_weight);
    json.Key("timing_movement");
    json.Double(options.fp_timing_movement);
    json.Key("timing_after_density");
    json.Bool(options.fp_timing_after_density);
    json.Key("timing_stop_overflow");
    json.Double(control.stop_overflow);
    json.Key("timing_density_schedule");
    json.StartArray();
    for (const auto& sample : fp_metrics.checkpoint_history) {
      json.StartObject();
      json.Key("iteration");
      json.Int(sample.iteration);
      json.Key("overflow");
      json.Double(sample.overflow);
      json.Key("invoked");
      json.Bool(sample.invoked);
      json.Key("forced");
      json.Bool(sample.forced);
      json.EndObject();
    }
    json.EndArray();
    json.Key("timing_checkpoints");
    json.Uint(fp_checkpoints);
    json.Key("timing_sta_calls");
    json.Uint(fp_sta_calls);
    json.Key("timing_resized_cells");
    json.Uint(fp_resized);
    json.Key("timing_repair_edits");
    json.Uint(fp_repairs);
    json.Key("fp_max_circle_overlap");
    json.Double(fp_metrics.centroid.max_circle_overlap);
    json.Key("snap_feasible");
    if (options.fp_snap_enabled) {
      json.Bool(!fp_snap || fp_snap->feasible);
    } else {
      json.Null();
    }
    json.Key("snap_seconds");
    json.Double(fp_snap ? fp_snap->seconds : 0);
    json.Key("snap_rejected");
    json.Int(fp_snap ? fp_snap->rejected : 0);
    json.Key("snap_attempted");
    json.Int(fp_snap ? fp_snap->attempted : 0);
    json.Key("snap_infeasible_families");
    json.Int(fp_snap ? fp_snap->infeasible_families : 0);
    json.Key("snap_max_overlap");
    json.Double(fp_snap ? fp_snap->maximum_overlap : 0);
    json.Key("snap_max_boundary_violation_dbu");
    json.Double(fp_snap ? fp_snap->maximum_boundary_violation : 0);
    json.Key("snap_max_displacement");
    json.Double(fp_snap ? fp_snap->maximum_displacement : 0);
    json.Key("candidates");
    json.StartArray();
    for (size_t i = 0; i < fp_before.size(); ++i) {
      const auto &p = fp_before[i], &t = fp_snap->circles[i];
      json.StartObject();
      json.Key("name");
      json.String(p.name.c_str());
      json.Key("kind");
      json.String(i < v.hierarchy_centroids ? "hierarchy" : "color");
      json.Key("snap_eligible");
      json.Bool(i < v.hierarchy_centroids);
      json.Key("repulsion_scope");
      json.Int(v.design.centroid_groups[i].repulsion_scope);
      json.Key("family");
      json.Int(p.family);
      json.Key("parent");
      json.Int(p.parent);
      json.Key("copy_parent");
      json.String(v.design.centroid_groups[i].copy_parent.c_str());
      json.Key("copies_per_parent");
      json.Int(v.design.centroid_groups[i].copies_per_parent);
      json.Key("radius");
      json.Double(p.radius);
      json.Key("area");
      json.Double(p.area);
      json.Key("repulsion_area_dbu2");
      json.Double(v.design.centroid_groups[i].repulsion_area < 0 ? p.area : v.design.centroid_groups[i].repulsion_area);
      for (auto [key, x, y] : {
               std::tuple{       "before",              p.x,              p.y},
               std::tuple{       "target",              t.x,              t.y},
               std::tuple{"refinement_gp", centroid_gp[i].x, centroid_gp[i].y},
               std::tuple{        "legal",    fp_final[i].x,    fp_final[i].y}
      }) {
        json.Key(key);
        json.StartArray();
        json.Double(x);
        json.Double(y);
        json.EndArray();
      }
      json.EndObject();
    }
    json.EndArray();
    json.Key("relations");
    json.StartArray();
    if (fp_snap) {
      for (const auto& relation : fp_snap->relations) {
        json.StartObject();
        json.Key("kind");
        json.String(relation.kind.c_str());
        json.Key("axis");
        json.Int(relation.axis);
        json.Key("value");
        json.Double(relation.value);
        json.Key("nodes");
        json.StartArray();
        for (int n : relation.nodes) {
          json.Int(n);
        }
        json.EndArray();
        json.EndObject();
      }
    }
    json.EndArray();
    json.Key("fp_history");
    json.StartArray();
    for (const auto& s : fp_metrics.history) {
      json.StartObject();
      json.Key("route_model");
      json.String(dp::route_model_name(s.route_model));
      json.Key("route_probe");
      json.Bool(s.route_probe);
      json.Key("route_probe_overflow");
      json.Double(s.route_probe_overflow);
      json.Key("route_switch_rsmt");
      json.Double(s.route_switch_rsmt);
      json.Key("route_switch_uroute_rsmt");
      json.Double(s.route_switch_uroute_rsmt);
      json.Key("route_switch_uroute");
      json.Double(s.route_switch_uroute);
      json.Key("route_uroute_switch_iteration");
      json.Int(s.route_uroute_switch_iteration);
      json.Key("route_uroute_seconds");
      json.Double(s.route_uroute_seconds);
      json.Key("iteration");
      json.Int(s.iteration);
      json.Key("overflow");
      json.Double(s.overflow);
      json.Key("hpwl_dbu");
      json.Double(s.hpwl);
      json.Key("centroid_max_overlap");
      json.Double(s.centroid_max_overlap);
      json.Key("centroid_parent_edges");
      json.Uint64(s.centroid_parent_edges);
      json.Key("centroid_parent_max_distance");
      json.Double(s.centroid_parent_max_distance);
      json.Key("centroid_parent_force");
      json.Double(s.centroid_parent_force);
      json.Key("centroid_align_pairs");
      json.Uint64(s.centroid_align_pairs);
      json.Key("centroid_align_residual");
      json.Double(s.centroid_align_residual);
      json.Key("centroid_align_ramp");
      json.Double(s.centroid_align_ramp);
      json.Key("centroid_align_force");
      json.Double(s.centroid_align_force);
      json.EndObject();
    }
    json.EndArray();
    json.EndObject();
    std::ofstream out(options.output_dir + "/fp_place.json");
    if (!(out << buffer.GetString() << '\n')) {
      throw std::runtime_error("cannot write floorplan snap report");
    }
  }
  if (v.placement_colors) {
    const auto&      colors = v.fp_coloring;
    std::vector<int> candidate(colors.colors.size(), -1);
    for (size_t g = v.hierarchy_centroids; g < v.design.centroid_groups.size(); ++g) {
      for (int c : v.design.centroid_groups[g].colors) {
        candidate[c] = g;
      }
    }
    std::vector<double> reserved(colors.colors.size());
    double              physical = 0, uncolored = 0, unassigned = 0;
    std::ofstream       membership(options.output_dir + "/fp_placement_colors.tsv");
    membership << "node\tcolor\tphysical_area_dbu2\treservation_recipients\n";
    for (size_t n = 0; n < v.cells.size(); ++n) {
      const double node_area  = v.design.centroid_node_area[n];
      physical               += node_area;
      std::vector<int> recipients;
      if (colors.owner[n] < 0) {
        uncolored += node_area;
        for (int c : colors.recipients[n]) {
          if (candidate[c] >= 0) {
            recipients.push_back(c);
          }
        }
        if (recipients.empty()) {
          unassigned += node_area;
        }
        for (int c : recipients) {
          reserved[c] += node_area / recipients.size();
        }
      }
      membership << v.cells[n].name << '\t' << colors.owner[n] << '\t' << node_area << '\t';
      for (size_t k = 0; k < recipients.size(); ++k) {
        membership << (k ? "," : "") << recipients[k];
      }
      membership << '\n';
    }
    rapidjson::StringBuffer                    buffer;
    rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
    json.StartObject();
    json.Key("source");
    json.String(options.fp_color_source.c_str());
    json.Key("hierarchy_scoped");
    json.Bool(true);
    json.Key("maximum_logic_equivalent_cells");
    json.Double(v.memory_colors ? v.color_maximum : 0);
    json.Key("initial_memory_groups");
    json.Int(colors.initial_memory_groups);
    json.Key("truncated_cones");
    json.Int(colors.truncated_cones);
    json.Key("merges_per_round");
    json.StartArray();
    for (int count : colors.merges_per_round) {
      json.Int(count);
    }
    json.EndArray();
    json.Key("physical_area_dbu2");
    json.Double(physical);
    json.Key("uncolored_area_dbu2");
    json.Double(uncolored);
    json.Key("reserved_area_dbu2");
    json.Double(uncolored - unassigned);
    json.Key("unassigned_area_dbu2");
    json.Double(unassigned);
    json.Key("equivalent_cell_area_dbu2");
    json.Double(dp::detail::centroid_cell_area(v.design));
    json.Key("minimum_equivalent_cells");
    json.Double(v.color_minimum);
    json.Key("membership_frozen_during_repair");
    json.Bool(v.frozen_centroids.has_value());
    json.Key("colors");
    json.StartArray();
    for (size_t c = 0; c < colors.colors.size(); ++c) {
      const auto& color = colors.colors[c];
      json.StartObject();
      json.Key("id");
      json.Uint64(c);
      json.Key("seed");
      json.String(color.name.c_str());
      json.Key("macros");
      json.Int(color.macros);
      json.Key("members");
      json.Uint64(color.members.size());
      json.Key("merged_seeds");
      json.Int(std::max(0, color.macros - 1));
      json.Key("candidate");
      json.Int(candidate[c]);
      json.Key("physical_area_dbu2");
      json.Double(color.area);
      json.Key("reserved_area_dbu2");
      json.Double(reserved[c]);
      json.Key("allocated_member_area_dbu2");
      json.Double(candidate[c] >= 0 ? v.design.centroid_groups[candidate[c]].repulsion_area - reserved[c] : 0);
      json.Key("repulsion_area_dbu2");
      json.Double(candidate[c] >= 0 ? v.design.centroid_groups[candidate[c]].repulsion_area : 0);
      json.EndObject();
    }
    json.EndArray();
    json.EndObject();
    std::ofstream out(options.output_dir + "/fp_placement_colors.json");
    if (!membership || !(out << buffer.GetString() << '\n')) {
      throw std::runtime_error("cannot write FP placement color report");
    }
  }
  if (v.centroids) {
    const auto                                 final                = summarize_centroids(v.design);
    const double                               equivalent_cell_area = dp::detail::centroid_cell_area(v.design);
    rapidjson::StringBuffer                    buffer;
    rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
    json.StartObject();
    json.Key("mode");
    json.String(options.iterations == 0 ? "fp-direct-macro-frame"
                                        : (options.fp_snap_enabled ? "fp-snap-frozen-targets" : "fp-unsnapped-frozen-targets"));
    json.Key("coordinate_units");
    json.String("DBU");
    json.Key("weight");
    json.Double(control.centroid_weight);
    json.Key("repulsion_weight");
    json.Double(control.centroid_repulsion_weight);
    json.Key("adaptive_repulsion");
    json.Bool(control.centroid_adaptive_repulsion);
    json.Key("circle_overlap_tolerance");
    json.Double(control.centroid_stop_overlap);
    json.Key("gp_circles_converged");
    json.Bool(metrics.centroid.circles_converged);
    json.Key("gp_circle_overlap_pairs");
    json.Uint64(metrics.centroid.circle_overlaps);
    json.Key("gp_max_circle_overlap");
    json.Double(metrics.centroid.max_circle_overlap);
    json.Key("effective_repulsion_weight");
    json.Double(metrics.centroid.effective_repulsion_weight);
    json.Key("target_density");
    json.Double(control.target_density);
    json.Key("equivalent_cell_area_dbu2");
    json.Double(equivalent_cell_area);
    json.Key("cache_reuse");
    json.Bool(cache.hit);
    json.Key("started");
    json.Bool(metrics.centroid.started);
    json.Key("completed");
    json.Bool(metrics.centroid.completed);
    json.Key("from_scratch");
    json.Bool(false);
    json.Key("start_iteration");
    json.Int(metrics.centroid.start_iteration);
    json.Key("iterations");
    json.Int(metrics.centroid.iterations);
    json.Key("force_seconds");
    json.Double(metrics.centroid.force_seconds);
    json.Key("minimum_equivalent_cells");
    json.Double(v.centroid_minimum);
    json.Key("minimum_copies_per_parent");
    json.Int(v.centroid_min_copies);
    json.Key("minimum_macros");
    json.Int(v.centroid_min_macros);
    json.Key("copy_scope");
    json.String("immediate_hierarchy_parent");
    json.Key("instances");
    json.StartArray();
    for (size_t i = 0; i < v.design.centroid_groups.size(); ++i) {
      const auto& g = v.design.centroid_groups[i];
      json.StartObject();
      json.Key("path");
      json.String(g.name.c_str());
      json.Key("kind");
      json.String(i < v.hierarchy_centroids ? "hierarchy" : "color");
      json.Key("snap_eligible");
      json.Bool(i < v.hierarchy_centroids);
      json.Key("repulsion_scope");
      json.Int(g.repulsion_scope);
      json.Key("copy_parent");
      json.String(g.copy_parent.c_str());
      json.Key("copies_per_parent");
      json.Int(g.copies_per_parent);
      json.Key("family");
      json.Int(g.family);
      json.Key("parent");
      json.Int(g.parent);
      json.Key("macros");
      json.Int(g.scope_macros);
      json.Key("scope_cells");
      json.Int(g.scope_cells);
      json.Key("anchor_cells");
      json.Int(final[i].cells);
      json.Key("anchor_macros");
      json.Int(final[i].macros);
      json.Key("anchor_area_dbu2");
      json.Double(final[i].area);
      json.Key("anchor_equivalent_cells");
      json.Double(final[i].area / equivalent_cell_area);
      json.Key("repulsion_radius_dbu");
      json.Double(std::sqrt((g.repulsion_area < 0 ? final[i].area : g.repulsion_area) / (std::acos(-1.) * control.target_density)));
      json.Key("repulsion_area_dbu2");
      json.Double(g.repulsion_area < 0 ? final[i].area : g.repulsion_area);
      json.Key("colors");
      json.StartArray();
      for (int c : g.colors) {
        json.Int(c);
      }
      json.EndArray();
      json.Key("equivalent_cells");
      json.Double(g.equivalent_cells);
      const auto write_sample = [&](const char* key, const std::vector<CentroidSample>& values) {
        json.Key(key);
        if (values.size() != v.design.centroid_groups.size()) {
          json.Null();
          return;
        }
        json.StartObject();
        json.Key("x");
        json.Double(values[i].x);
        json.Key("y");
        json.Double(values[i].y);
        json.Key("rms_radius");
        json.Double(values[i].radius);
        json.Key("parent_dx");
        json.Double(values[i].x - (g.parent < 0 ? (v.design.layout.xl + v.design.layout.xh) / 2 : values[g.parent].x));
        json.Key("parent_dy");
        json.Double(values[i].y - (g.parent < 0 ? (v.design.layout.yl + v.design.layout.yh) / 2 : values[g.parent].y));
        json.EndObject();
      };
      write_sample("initial", centroid_initial);
      write_sample("gp", centroid_gp);
      write_sample("final", final);
      json.EndObject();
    }
    json.EndArray();
    std::vector<int> anchors(v.design.centroid_color_names.size()), colored_cells(anchors.size()), colored_macros(anchors.size());
    for (const auto& g : v.design.centroid_groups) {
      for (int c : g.colors) {
        ++anchors[c];
      }
    }
    for (size_t n = 0; n < v.design.node_centroid_color.size(); ++n) {
      int c = v.design.node_centroid_color[n];
      if (c >= 0) {
        ++colored_cells[c];
        colored_macros[c] += v.design.node_macro[n] != 0;
      }
    }
    json.Key("colors");
    json.StartArray();
    for (size_t c = 0; c < anchors.size(); ++c) {
      json.StartObject();
      json.Key("id");
      json.Uint64(c);
      json.Key("key");
      json.String(v.design.centroid_color_names[c].c_str());
      json.Key("candidate_anchors");
      json.Int(anchors[c]);
      json.Key("cells");
      json.Int(colored_cells[c]);
      json.Key("macros");
      json.Int(colored_macros[c]);
      json.EndObject();
    }
    json.EndArray();
    json.EndObject();
    std::ofstream membership(options.output_dir + "/fp_membership.tsv");
    for (size_t n = 0; n < v.cells.size(); ++n) {
      membership << v.cells[n].name << '\t' << v.design.node_centroid_color[n] << '\n';
    }
    std::ofstream out(options.output_dir + "/fp_candidates.json");
    out << buffer.GetString() << '\n';
  }
  if (options.analyze_colors_only) {
    result.floorplan = v.fp;
    restore_imported_rows(result.floorplan, v);
    for (const auto& c : v.cells) {
      result.floorplan.components.push_back({c.name, c.macro, c.place});
    }
    result.qor = std::format(R"({{"schema_version":1,"kind":"floorplan_color_analysis","legal":false,"colors":{}}})",
                             v.fp_coloring.colors.size());
    return result;
  }
  // A restored or edited placement keeps the netlist's geometry template; the
  // rows each two-row cell landed on decide its PG rail stack.
  sync_rail_twins(v, tech, options);
  for (auto& c : v.cells) {
    c.node.attr(attrs::place).set(c.place);
    c.node.attr(attrs::placement_block).set(c.block);
    v.fp.components.push_back({c.name, c.macro, c.place});
  }
  v.fp.components.insert(v.fp.components.end(), handoff_cells.begin(), handoff_cells.end());
  // Internal array order depends on which cells were cache-locked. Keep the
  // exported frame identical for the same physical placement.
  std::ranges::sort(v.fp.components, {}, &ph::Component::name);
  // Keep input port and net names identical: OpenROAD's structural Verilog
  // writer does not emit input aliases for otherwise valid DEF pin/net names.
  // Reserve every port name before choosing names for internal nets.
  std::set<std::string> net_names;
  for (auto& terminal : v.fp.terminals) {
    // Imported/staged aliases can name nets that are absent from the rebuilt
    // connectivity, especially unused inputs. Reconstruct every alias below.
    terminal.net.clear();
    net_names.insert(terminal.name);
  }
  for (size_t net = 0; net < v.drivers.size(); ++net) {
    auto name = v.drivers[net].instance == "PIN" ? v.drivers[net].pin : std::format("net_{}", net);
    if (v.drivers[net].instance != "PIN") {
      // Preserve identified buses through the macro-to-final handoff when the
      // experimental final refinement is requested. Never rename input nets.
      if (options.busorder.enabled || options.boundarypins.enabled) {
        auto identity = v.bus_names.find(v.drivers[net]);
        if (identity == v.bus_names.end()) {
          for (const auto& sink : v.nets.at(v.drivers[net])) {
            identity = v.bus_names.find(sink);
            if (identity != v.bus_names.end()) {
              break;
            }
          }
        }
        if (identity != v.bus_names.end()) {
          const auto candidate = std::format("{}[{}]", identity->second.bus, identity->second.bit);
          if (!net_names.contains(candidate)) {
            name = candidate;
          }
        }
      }
      const auto base   = name;
      unsigned   suffix = 0;
      while (!net_names.insert(name).second) {
        name = base + "_" + std::to_string(++suffix);
      }
    }
    ph::Net n{name, {}};
    n.connections.push_back({v.drivers[net].instance, v.drivers[net].pin});
    for (const auto& sink : v.nets.at(v.drivers[net])) {
      n.connections.push_back({sink.instance, sink.pin});
    }
    for (const auto& c : n.connections) {
      if (c.instance == "PIN") {
        v.fp.terminals[v.terminal.at(c.pin)].net = name;
      }
    }
    v.fp.nets.push_back(std::move(n));
  }
  // Unconnected input ports still need a DEF NET and stable terminal identity.
  for (auto& t : v.fp.terminals) {
    if (t.net.empty()) {
      t.net = t.name;
      v.fp.nets.push_back({t.net, {{"PIN", t.name}}});
    }
  }
  // A macro-to-cell handoff freezes coordinates only for that solve. Do not
  // persist it as a user constraint into saved DEFs, graphs or cache frames.
  for (auto& terminal : v.fp.terminals) {
    if (const auto status = options.stage_pin_status.find(terminal.name); status != options.stage_pin_status.end()) {
      terminal.status = status->second;
    }
  }
  shape_io_perimeter_pins(v.fp);
  restore_imported_rows(v.fp, v);
  std::optional<ph::Floorplan> handoff_grid;
  Pg_channel_stats             handoff_channel_stats;
  // Only a fresh final placement exports the physical handoff frame.
  const bool final_handoff = options.full && options.completed_stage == Placement_stage::final && !options.timing_seed
                             && !options.retain_placement && !options.fp_place;
  if (pg_profile && final_handoff) {
    if (!handoff_rows.empty()) {
      v.fp.rows = handoff_rows;
    }
    handoff_grid            = make_power_grid(tech, v.fp, *pg_profile, &handoff_channel_stats);
    handoff_grid->terminals = handoff_pins(tech, *handoff_grid, tech_profile->io_pin_layers[0], tech_profile->io_pin_layers[1]);
    // Power terminals are appended only to the exported frame. The placement
    // and RC arrays continue to contain the original signal IO suffix.
    for (size_t i = 0; i < v.fp.terminals.size(); ++i) {
      if (v.fp.terminals[i].name != handoff_grid->terminals.at(i).name) {
        throw std::runtime_error("power-grid generation reordered signal IOs");
      }
      v.fp.terminals[i]                      = handoff_grid->terminals[i];
      v.design.node_x.at(v.cells.size() + i) = v.fp.terminals[i].x;
      v.design.node_y.at(v.cells.size() + i) = v.fp.terminals[i].y;
    }
  }
  for (const auto& t : v.fp.terminals) {
    Ann_place p;
    p.x           = t.x;
    p.y           = t.y;
    p.orientation = t.orientation;
    p.status      = t.status;
    auto pin      = top_pins.at(t.name);
    if (result.fp_library) {
      const auto name = std::string(pin.get_pin_name());
      pin = pin.get_master_node().is_input_node() ? result.graph->get_input_pin(name) : result.graph->get_output_pin(name);
    }
    const auto decl_name = std::string(pin.get_pin_name());
    if (result.graph->get_io()->get_bits(decl_name) <= 1) {
      pin.attr(attrs::pin_place).set(p);
    }
  }
  if (!handoff_rows.empty()) {
    v.fp.rows = handoff_rows;
  }
  if (options.full && !options.timing_seed && !options.retain_placement) {
    if (defer_macro_cells) {
      auto frame = v.fp;
      std::erase_if(frame.components, [&](const auto& cell) {
        return tech.macros.at(cell.macro).klass.starts_with("CORE") && cell.place.status != Status::FIXED
               && cell.place.status != Status::LOCKED;
      });
      check_legality(tech, frame, options);
    } else {
      check_legality(tech, v.fp, options);
    }
  }
  if (options.timing_seed) {
    auto rc_options              = options;
    rc_options.resistance_ohm_um = 0;
    rc_options.capacitance_ff_um = 0;
    result.parasitics            = estimate_rc(v, tech, rc_options, &result.pin_locations);
  } else {
    result.parasitics = estimate_rc(v, tech, options, &result.pin_locations);
  }
  if (native_session && options.native_gp_timing->legal_output_guard) {
    // This follows actual final-frame legality and fresh final RC construction.
    // The independently frozen legal baseline is never replaced by GP timing.
    const auto acceptance = native_session->check_legal_output(*result.parasitics, true);
    write_legal_acceptance(acceptance, true, true);
    if (!acceptance.passed()) {
      throw std::invalid_argument("native final legal timing rejected output: " + acceptance.reason);
    }
  }
  record_poses(*result.parasitics, v);
  {
    // Classification is only needed to attach the cuts. Release its pin sets
    // before allocating the persisted RC text.
    const auto persisted_controls = prects_networks(tech, v.fp, v.prects);
    for (const auto& net : result.parasitics->nets) {
      if (persisted_controls.timing_cuts.contains(net.driver)) {
        result.parasitics->timing_cuts.insert(net.driver);
      }
    }
  }
  fs::create_directories(options.output_dir);
  const bool export_final_features = resolved_uroute.export_features && !options.timing_seed && !options.retain_placement
                                     && options.completed_stage == Placement_stage::final;
  if (metrics.uroute_skipped.empty()) {
    metrics.uroute_skipped = selected_uroute.skipped;
  }
  if (metrics.uroute_sample_ids.empty()) {
    metrics.uroute_sample_ids = selected_uroute.sample_ids;
  }
  {
    rapidjson::StringBuffer                    buffer;
    rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
    json.StartObject();
    json.Key("schema_version");
    json.Int(1);
    json.Key("uroute");
    json.StartObject();
    json.Key("dir");
    json.String(resolved_uroute.dir.c_str());
    json.Key("export");
    json.Bool(resolved_uroute.export_features);
    json.Key("shots");
    json.Int(resolved_uroute.shots);
    json.Key("min_shots");
    json.Int(resolved_uroute.min_shots);
    json.Key("fallback");
    json.String(resolved_uroute.fallback.c_str());
    json.Key("drc_level");
    json.Double(resolved_uroute.drc_level);
    json.Key("cache_identity");
    json.String(selected_uroute.cache_identity.c_str());
    json.Key("manifest_sha256");
    json.String(selected_uroute.manifest_sha256.c_str());
    json.Key("weights_sha256");
    json.String(selected_uroute.weights_sha256.c_str());
    json.Key("uroute_skipped");
    json.String(metrics.uroute_skipped.c_str());
    json.Key("sample_ids");
    write_uroute_sample_ids(json, metrics.uroute_sample_ids);
    json.EndObject();
    json.Key("global_model");
    json.String(options.route_model.c_str());
    json.Key("degree");
    json.Int(options.route_degree);
    json.Key("accuracy");
    json.Int(options.route_accuracy);
    json.Key("routability");
    json.Bool(options.routability);
    json.Key("stages");
    json.StartObject();
    for (const auto& [name, stage] : {
             std::pair{   "fp",    &options.fp_route},
             std::pair{"macro", &options.macro_route},
             std::pair{"final", &options.final_route}
    }) {
      const auto policy = route_policy(options, *stage);
      json.Key(name);
      json.StartObject();
      json.Key("requested_model");
      json.String(stage->model.c_str());
      json.Key("model");
      json.String(dp::route_model_name(policy.model));
      json.Key("feedback_enabled");
      const bool feedback = stage == &options.macro_route && options.macro_mode == "h2d" ? options.macro_parameters.routability
                                                                                         : options.routability;
      json.Bool(feedback && policy.model != dp::RouteModel::off);
      json.Key("switch");
      json.Double(policy.switch_overflow);
      json.Key("target");
      json.Double(policy.target_overflow);
      json.Key("hold");
      json.Int(policy.hold);
      json.Key("probe");
      json.Bool(policy.probe);
      json.Key("probe_iterations");
      json.Int(policy.probe_iterations);
      json.Key("probe_model");
      json.String(dp::route_model_name(policy.probe_model));
      json.Key("uroute_switch");
      json.Double(policy.uroute_switch_overflow);
      json.Key("uroute_hold");
      json.Int(policy.uroute_hold);
      json.Key("uroute_ladder");
      json.Bool(policy.uroute_ladder);
      json.EndObject();
    }
    json.EndObject();
    json.EndObject();
    std::ofstream resolved(options.output_dir + "/route_options.json");
    resolved << buffer.GetString() << '\n';
    if (!resolved) {
      throw std::runtime_error("cannot write resolved route options");
    }
  }
  if (!metrics.history.empty() || !metrics.detailed_history.empty()) {
    rapidjson::StringBuffer                    buffer;
    rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
    json.StartObject();
    json.Key("bins_x");
    json.Int(metrics.bins_x);
    json.Key("route_model");
    json.String(dp::route_model_name(metrics.route_model));
    json.Key("uroute_skipped");
    json.String(metrics.uroute_skipped.c_str());
    json.Key("uroute_manifest_sha256");
    json.String(selected_uroute.manifest_sha256.c_str());
    json.Key("uroute_weights_sha256");
    json.String(selected_uroute.weights_sha256.c_str());
    json.Key("uroute_sample_ids");
    write_uroute_sample_ids(json, metrics.uroute_sample_ids);
    json.Key("route_never_switched");
    json.Bool(control.route.model == dp::RouteModel::automatic && metrics.route_switch_iteration < 0);
    json.Key("route_switch_iteration");
    json.Int(metrics.route_switch_iteration);
    json.Key("route_uroute_switch_iteration");
    json.Int(metrics.route_uroute_switch_iteration);
    json.Key("route_switch_uroute_rsmt");
    json.Double(metrics.route_switch_uroute_rsmt);
    json.Key("route_switch_uroute");
    json.Double(metrics.route_switch_uroute);
    json.Key("route_probe_overflow");
    json.Double(metrics.route_probe_overflow);
    json.Key("route_switch_rudy");
    json.Double(metrics.route_switch_rudy);
    json.Key("route_switch_rsmt");
    json.Double(metrics.route_switch_rsmt);
    json.Key("route_switch");
    json.Double(control.route.switch_overflow);
    json.Key("route_target");
    json.Double(control.route.target_overflow);
    json.Key("route_hold");
    json.Int(control.route.hold);
    json.Key("route_degree");
    json.Int(control.route.degree);
    json.Key("route_accuracy");
    json.Int(control.route.accuracy);

    json.Key("routability_macro_weight");
    json.Double(options.routability_macro_weight);
    json.Key("routability_macro_range");
    json.Double(options.routability_macro_range);
    json.Key("macro_routing_seconds");
    json.Double(metrics.macro_routing_seconds);
    json.Key("magnet_seconds");
    json.Double(metrics.magnet_seconds);
    json.Key("local_density_seconds");
    json.Double(metrics.local_density_seconds);
    json.Key("macro_fill_cell_space");
    json.Bool(control.filler_cell_space);
    json.Key("macro_filler_fraction");
    json.Double(control.filler_macro_fraction);
    json.Key("virtual_fillers");
    json.Bool(options.virtual_fillers);
    json.Key("virtual_filler_count");
    json.Int(metrics.filler_count);
    json.Key("virtual_filler_area_um2");
    json.Double(metrics.filler_area / tech.dbu / tech.dbu);
    json.Key("local_density_force");
    json.Double(metrics.local_density_force);
    json.Key("local_density_weight");
    json.Double(control.local_density_weight);
    json.Key("local_density_target");
    json.Double(control.local_density_target < 0 ? control.target_density : control.local_density_target);
    json.Key("timing_spread_weight");
    json.Double(control.timing_spread_weight);
    json.Key("timing_spread_cells");
    json.Int(metrics.timing_spread_cells);
    json.Key("timing_spread_min_mobility");
    json.Double(metrics.timing_spread_min_mobility);
    json.Key("macro_cell_density_weight");
    json.Double(control.macro_cell_density_weight);
    json.Key("macro_cell_density_reach");
    json.Double(control.macro_cell_density_reach);
    json.Key("macro_cell_density_screening_length_dbu");
    json.Double(metrics.macro_cell_density_screening_length);
    json.Key("macro_cell_density_coefficient");
    json.Double(metrics.macro_cell_density_coefficient);
    json.Key("macro_cell_density_seconds");
    json.Double(metrics.macro_cell_density_seconds);
    json.Key("macro_cell_density_force");
    json.Double(metrics.macro_cell_density_force);
    json.Key("local_density_grid_seconds");
    json.Double(metrics.local_density_grid_seconds);
    json.Key("macro_density_pull_weight");
    json.Double(control.macro_density_pull_weight);
    json.Key("macro_density_pull_seconds");
    json.Double(metrics.macro_density_pull_seconds);
    json.Key("macro_density_pull_force");
    json.Double(metrics.macro_density_pull_force);
    json.Key("macro_density_pull_anchor_ratio");
    json.Double(metrics.macro_density_pull_anchor_ratio);
    json.Key("local_density_bins");
    json.Int(options.local_density_bins);
    json.Key("dynamic_bins");
    json.Bool(options.dynamic_bins);
    json.Key("rudy_seconds");
    json.Double(metrics.rudy_seconds);
    json.Key("rudy_calls");
    json.Int(metrics.rudy_calls);
    json.Key("bins_y");
    json.Int(metrics.bins_y);
    json.Key("stop_overflow");
    json.Double(control.stop_overflow);
    json.Key("initial_density_weight");
    json.Double(control.density_weight_seed.value_or(control.initial_density_weight));
    json.Key("density_weight_seeded");
    json.Bool(control.density_weight_seed.has_value());
    json.Key("poisson_seconds");
    json.Double(metrics.poisson_seconds);
    json.Key("global_placement_seconds");
    json.Double(metrics.global_placement_seconds);
    json.Key("macro_legalization_seconds");
    json.Double(metrics.alignment.macro_legalization_seconds);
    json.Key("cell_legalization_seconds");
    json.Double(metrics.alignment.cell_legalization_seconds);
    json.Key("detailed_placement_seconds");
    json.Double(metrics.detailed_placement_seconds);
    json.Key("detailed_kernels");
    json.StartArray();
    for (const auto& sample : metrics.detailed_history) {
      json.StartObject();
      json.Key("kernel");
      json.String(sample.kernel.c_str());
      json.Key("weighted_hpwl_before");
      json.Double(sample.before);
      json.Key("weighted_hpwl_after");
      json.Double(sample.after);
      json.Key("legal");
      json.Bool(sample.legal);
      json.Key("legality_issue");
      json.String(sample.legality_issue.c_str());
      json.Key("macros_preserved");
      json.Bool(sample.macros_preserved);
      json.Key("accepted");
      json.Bool(sample.accepted);
      json.EndObject();
    }
    json.EndArray();
    json.Key("converged");
    json.Bool(metrics.converged);
    json.Key("density_ready_iteration");
    json.Int(metrics.density_iteration);
    json.Key("continuation_deadline");
    json.Int(metrics.continuation_deadline);
    json.Key("continuation_limit");
    json.Bool(metrics.continuation_limit);
    json.Key("density_stalled");
    json.Bool(metrics.density_stalled);
    json.Key("overflow_floor");
    json.Double(metrics.overflow_floor);
    json.Key("target_density");
    json.Double(control.target_density);
    json.Key("route_stalled");
    json.Bool(metrics.route_stalled);
    json.Key("objective_converged");
    json.Bool(metrics.objective_converged);
    json.Key("timing_observed_in_gp");
    json.Bool(metrics.timing_observed);
    json.Key("timing_satisfied_at_last_checkpoint");
    json.Bool(metrics.timing_satisfied);
    json.Key("iterations");
    json.Int(metrics.iterations);
    json.Key("selected_iteration");
    json.Int(metrics.selected_iteration);
    json.Key("samples");
    json.StartArray();
    for (const auto& s : metrics.history) {
      json.StartObject();
      json.Key("iteration");
      json.Int(s.iteration);
      json.Key("overflow");
      json.Double(s.overflow);
      json.Key("hpwl_dbu");
      json.Double(s.hpwl);
      json.Key("raw_overflow");
      json.Double(s.raw_overflow);
      json.Key("overflow_floor");
      json.Double(s.overflow_floor);
      json.Key("density_weight");
      json.Double(s.density_weight);
      json.Key("route_overflow");
      json.Double(s.route_overflow);
      json.Key("route_model");
      json.String(dp::route_model_name(s.route_model));
      json.Key("route_sampled");
      json.Bool(s.route_sampled);
      json.Key("route_seconds");
      json.Double(s.route_seconds);
      json.Key("route_bins");
      json.Int(s.route_bins);
      json.Key("route_switch_iteration");
      json.Int(s.route_switch_iteration);
      json.Key("route_uroute_switch_iteration");
      json.Int(s.route_uroute_switch_iteration);
      json.Key("route_switch_uroute_rsmt");
      json.Double(s.route_switch_uroute_rsmt);
      json.Key("route_switch_uroute");
      json.Double(s.route_switch_uroute);
      json.Key("route_probe_overflow");
      json.Double(s.route_probe_overflow);
      json.Key("route_uroute_seconds");
      json.Double(s.route_uroute_seconds);
      json.Key("route_switch_rudy");
      json.Double(s.route_switch_rudy);
      json.Key("route_switch_rsmt");
      json.Double(s.route_switch_rsmt);
      json.Key("route_probe");
      json.Bool(s.route_probe);
      json.Key("route_blocked_pins");
      json.Int(s.route_blocked_pins);
      json.Key("route_unroutable_nets");
      json.Int(s.route_unroutable_nets);
      json.Key("route_calls");
      json.Int(s.route_calls);
      json.Key("route_rudy_seconds");
      json.Double(s.route_rudy_seconds);
      json.Key("route_rsmt_seconds");
      json.Double(s.route_rsmt_seconds);

      json.Key("route_peak_utilization");
      json.Double(s.route_peak_utilization);
      json.Key("macro_routing_force");
      json.Double(s.macro_routing_force);
      json.Key("macro_routing_channels");
      json.Uint64(s.macro_routing_channels);
      json.Key("inflation_area_ratio");
      json.Double(s.inflation_area_ratio);
      json.Key("model_density");
      json.Double(s.model_density);
      json.Key("filler_charge_scale");
      json.Double(s.filler_charge_scale);
      json.Key("macro_opacity");
      json.Double(s.macro_opacity);
      json.Key("macro_exclusion_scale");
      json.Double(s.macro_exclusion_scale);
      json.Key("local_density_weight");
      json.Double(s.local_density_weight);
      json.Key("macro_overlap_pairs");
      json.Uint64(s.macro_overlaps);
      json.Key("macro_stack_pairs");
      json.Uint64(s.macro_stacks);
      json.Key("align_pairs");
      json.Uint64(s.align_pairs);
      json.Key("align_residual");
      json.Double(s.align_residual);
      json.Key("align_ramp");
      json.Double(s.align_ramp);
      json.Key("align_force");
      json.Double(s.align_force);
      json.Key("magnet_round");
      json.Int(s.magnet_round);
      json.Key("magnet_groups");
      json.Int(s.magnet_groups);
      json.Key("macro_move_max");
      json.Double(s.macro_move_max);
      json.Key("macro_move_rms");
      json.Double(s.macro_move_rms);
      json.Key("last_instability_iteration");
      json.Int(s.last_instability_iteration);
      if (control.centroid_adaptive_repulsion) {
        json.Key("centroid_overlap_pairs");
        json.Uint64(s.centroid_overlaps);
        json.Key("centroid_max_overlap");
        json.Double(s.centroid_max_overlap);
        json.Key("centroid_repulsion_weight");
        json.Double(s.centroid_repulsion_weight);
      }
      json.EndObject();
    }
    json.EndArray();
    json.EndObject();
    std::ofstream output(options.output_dir + "/gp_history.json");
    if (!(output << buffer.GetString() << '\n')) {
      throw std::runtime_error("cannot write GP history");
    }
    if (!metrics.converged) {
      if (control.centroid_adaptive_repulsion) {
        std::print(stderr,
                   "WARNING: centroid-circle maximum relative overlap {:.6f} (target {:.6f}); effective penalty {:.1f}\n",
                   metrics.centroid.max_circle_overlap,
                   control.centroid_stop_overlap,
                   metrics.centroid.effective_repulsion_weight);
      }
      std::print(stderr,
                 "WARNING: native DREAMPlace GP stopped at {} iterations ({}) with overflow {:.6f} (target {:.6f}, floor {:.6f}); "
                 "objective settled {}, timing observed {}, last timing satisfied {}\n",
                 metrics.iterations,
                 metrics.density_stalled && metrics.overflow_floor > control.stop_overflow
                     ? "density infeasible; handing off to legalization"
                 : metrics.density_stalled    ? "density stalled"
                 : metrics.route_stalled      ? "routing stalled; handing off to legalization"
                 : metrics.continuation_limit ? "post-density budget"
                                              : "iteration cap",
                 metrics.overflow,
                 control.stop_overflow,
                 metrics.overflow_floor,
                 metrics.objective_converged,
                 metrics.timing_observed,
                 metrics.timing_satisfied);
    }
  }
  if (v.align_macros && !options.output_dir.empty()) {
    rapidjson::StringBuffer                    buffer;
    rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
    json.StartObject();
    json.Key("coordinate_units");
    json.String("microns");
    json.Key("weight");
    json.Double(options.macro_align_weight);
    json.Key("global_same_master");
    json.Bool(options.macro_align_global);
    json.Key("gp_alignment_seconds");
    json.Double(metrics.macro_alignment_seconds);
    json.Key("sigma");
    json.Double(options.macro_align_sigma);
    json.Key("start");
    json.Int(options.macro_align_start);
    json.Key("full");
    json.Int(options.macro_align_full);
    json.Key("snap");
    json.Bool(options.macro_snap);
    json.Key("snap_tolerance");
    json.Double(options.macro_snap_tolerance);
    json.Key("snap_weight");
    json.Int(options.macro_snap_weight);
    json.Key("snap_mode");
    json.String(options.macro_snap_mode.c_str());
    const auto& a = metrics.alignment;
    json.Key("regularity_attempted");
    json.Int(a.regularity.attempted);
    json.Key("regularity_rejected");
    json.Int(a.regularity.rejected);
    json.Key("regularity_infeasible_classes");
    json.Int(a.regularity.infeasible_families);
    json.Key("regularity_seconds");
    json.Double(a.regularity.seconds);
    json.Key("snap_classes");
    json.StartArray();
    if (v.hierarchy_snap) {
      for (size_t n = 0; n < v.cells.size(); ++n) {
        if (v.design.node_snap_class[n] < 0) {
          continue;
        }
        json.StartObject();
        json.Key("node");
        json.String(v.cells[n].name.c_str());
        json.Key("master");
        json.String(v.cells[n].macro.c_str());
        json.Key("class");
        json.Int(v.design.node_snap_class[n]);
        json.EndObject();
      }
    }
    json.EndArray();
    json.Key("regularity_relations");
    json.StartArray();
    for (const auto& relation : a.regularity.relations) {
      json.StartObject();
      json.Key("kind");
      json.String(relation.kind.c_str());
      json.Key("axis");
      json.Int(relation.axis);
      json.Key("nodes");
      json.StartArray();
      for (int c : relation.nodes) {
        int n = std::stoi(a.regularity.circles.at(c).name);
        json.String(v.cells.at(n).name.c_str());
      }
      json.EndArray();
      json.EndObject();
    }
    json.EndArray();
    json.Key("soft_constraints");
    json.StartArray();
    for (const auto& constraint : a.constraints) {
      json.StartArray();
      json.String(v.cells.at(constraint.a).name.c_str());
      json.String(v.cells.at(constraint.b).name.c_str());
      json.Int(constraint.axis);
      json.Double(constraint.offset / tech.dbu);
      json.EndArray();
    }
    json.EndArray();
    json.Key("macro_legalization_s");
    json.Double(a.macro_legalization_seconds);
    json.Key("cell_legalization_s");
    json.Double(a.cell_legalization_seconds);
    json.Key("groups_x");
    json.Uint64(a.x_groups);
    json.Key("groups_y");
    json.Uint64(a.y_groups);
    json.Key("members");
    json.Uint64(a.members);
    json.Key("members_both_axes");
    json.Uint64(a.both_axes);
    json.Key("exact_groups");
    json.Uint64(a.exact_groups);
    json.Key("broken_groups");
    json.Uint64(a.broken_groups);
    json.Key("violated_soft_arcs_after_site_rounding");
    json.Uint64(a.soft_violations);
    auto displacement = a.macro_displacements;
    std::sort(displacement.begin(), displacement.end());
    json.Key("macro_displacement_median_um");
    json.Double(displacement.empty() ? 0 : displacement[(displacement.size() - 1) / 2] / tech.dbu);
    json.Key("macro_displacement_max_um");
    json.Double(displacement.empty() ? 0 : displacement.back() / tech.dbu);
    json.Key("macro_displacement_above100um");
    json.Uint64(std::count_if(displacement.begin(), displacement.end(), [&](double x) { return x > 100 * tech.dbu; }));
    json.Key("groups");
    json.StartArray();
    for (const auto& group : a.groups) {
      json.StartObject();
      json.Key("class");
      json.Int(group.align_class);
      json.Key("axis");
      json.String(group.axis == dp::Axis::x ? "x" : "y");
      json.Key("padded_target_um");
      json.Double(group.target / tech.dbu);
      json.Key("members");
      json.StartArray();
      for (int i : group.nodes) {
        json.String(v.cells.at(i).name.c_str());
      }
      json.EndArray();
      json.EndObject();
    }
    json.EndArray();
    json.Key("macros");
    json.StartArray();
    for (size_t i = 0; i < v.cells.size(); ++i) {
      if (!v.design.node_macro[i]) {
        continue;
      }
      const auto& cell  = v.cells[i];
      const auto  owner = v.cohesion_owners.find(cell.name);
      json.StartObject();
      json.Key("name");
      json.String(cell.name.c_str());
      json.Key("master");
      json.String(cell.macro.c_str());
      json.Key("color");
      json.String(owner == v.cohesion_owners.end() ? "" : owner->second.c_str());
      json.Key("class");
      json.Int(v.design.node_align_class[i]);
      json.EndObject();
    }
    json.EndArray();
    json.EndObject();
    fs::create_directories(options.output_dir);
    std::ofstream output(options.output_dir + "/alignment.json");
    if (!(output << buffer.GetString() << '\n')) {
      throw std::runtime_error("cannot write macro alignment report");
    }
  }
  if (options.cohesion > 0) {
    std::map<std::string, std::vector<const Cell*>> groups;
    for (const auto& c : v.cells) {
      if (auto found = v.cohesion_owners.find(c.name); found != v.cohesion_owners.end()) {
        groups[found->second].push_back(&c);
      }
    }
    rapidjson::StringBuffer                    buffer;
    rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
    json.StartObject();
    json.Key("coefficient_per_um");
    json.Double(options.cohesion);
    json.Key("schedule");
    json.String("linear zero to coefficient during final GP; IO prototype unchanged");
    json.Key("groups");
    json.StartArray();
    for (const auto& [key, members] : groups) {
      if (members.size() < 2) {
        continue;
      }
      json.StartObject();
      json.Key("key");
      json.String(key.c_str());
      json.Key("macro_members");
      json.Uint(std::count_if(members.begin(), members.end(), [&](const auto* c) {
        return !tech.macros.at(c->macro).klass.starts_with("CORE");
      }));
      json.Key("nodes");
      json.StartArray();
      for (const auto* c : members) {
        json.String(c->name.c_str());
      }
      json.EndArray();
      json.EndObject();
    }
    json.EndArray();
    json.EndObject();
    std::ofstream output(options.output_dir + "/cohesion_groups.json");
    if (!(output << buffer.GetString() << '\n')) {
      throw std::runtime_error("cannot write cohesion groups");
    }
  }
  if (macro_run) {
    // Reconcile the report's original array order with the host's final row
    // orientations and any standard-cell ECO/cache legalization.
    for (size_t i = 0; i < macro_report_info.size(); ++i) {
      const auto& n = macro_report_info[i];
      if (n.io) {
        auto terminal                 = v.terminal.at(n.name.substr(4));
        macro_report_design.node_x[i] = v.fp.terminals[terminal].x;
        macro_report_design.node_y[i] = v.fp.terminals[terminal].y;
      } else {
        const auto& c                           = v.cells[v.cell.at(n.name)];
        macro_report_design.node_x[i]           = c.place.x;
        macro_report_design.node_y[i]           = c.place.y;
        macro_report_design.node_orientation[i] = native_orientation(c.place.orientation);
      }
    }
    // Net and pin CSR order is invariant under cell-array permutation.
    macro_report_design.pin_offset_x = v.design.pin_offset_x;
    macro_report_design.pin_offset_y = v.design.pin_offset_y;
    write_macro_report(*macro_run, macro_report_design, macro_report_info, options, tech.dbu);
  } else if (!options.timing_seed) {
    std::error_code error;
    fs::remove(options.output_dir + "/macro_topology.json", error);
    fs::remove(options.output_dir + "/h2d.png", error);
  }

  if (memories.empty()) {
    std::error_code error;
    fs::remove(options.output_dir + "/memory_proxies.lef", error);
  }
  if (!memories.empty()) {
    std::ofstream lef(options.output_dir + "/memory_proxies.lef");
    lef << proxy_lef;
    if (!lef) {
      throw std::runtime_error("cannot write memory proxy LEF");
    }
  }
  const auto def = options.output_dir + "/out.def";
  if (handoff_grid) {
    // Keep the signal-only frame for feature export. The handoff appends
    // power terminals, which are not part of the placement feature tensor.
    if (export_final_features) {
      std::swap(v.fp, *handoff_grid);
    } else {
      v.fp = std::move(*handoff_grid);
    }
    write_power_grid_report(v.fp, *pg_profile, options.output_dir + "/power_grid.json", &handoff_channel_stats);
  } else if (final_handoff) {
    std::error_code error;
    fs::remove(options.output_dir + "/power_grid.json", error);
  }
  if (options.full && !options.timing_seed && !options.retain_placement) {
    ph::write_def(v.fp, def);
    ph::verify_def_roundtrip(v.fp, ph::read_def(def));
  } else {
    std::error_code ec;
    fs::remove(def, ec);
  }
  if (metrics.density_weight > 0) {
    result.gp_density_weight = metrics.density_weight;
  }
  result.unknown_const_tied = resolver.unknown_tied();
  result.qor                = std::format(
      R"({{"schema_version":1,"kind":"placement","mode":"{}","legal":{},"gp_converged":{},"gp_overflow":{},"hpwl_um":{},"initial_hpwl_um":{},"cells":{},"nets":{},"estimated_macros":{},"virtual_fillers":{},"density_constraint":"soft","timing_model":"placement_rc","timing_approximate":{},"rc_identity":"{}","incremental":{{"enabled":{},"hit":{},"reused_blocks":{},"changed_blocks":{}}}}})",
      options.retain_placement ? "timing_snapshot"
      : options.timing_seed    ? "post_synthesis_zero_wire"
      : options.full           ? "full"
                               : "gp",
      (options.timing_seed || options.retain_placement) ? "false"
      : options.full && !defer_macro_cells              ? "true"
                                                        : (metrics.legal ? "true" : "false"),
      metrics.converged ? "true" : "false",
      cache.hit ? "null" : std::format("{:.9g}", metrics.overflow),
      [&] {
        auto d = v.design;
        std::fill(d.net_weights.begin(), d.net_weights.end(), 1);
        return dp::hpwl(d) / tech.dbu;
      }(),
      initial_hpwl / tech.dbu,
      v.cells.size(),
      v.nets.size(),
      memories.size(),
      options.virtual_fillers ? "true" : "false",
      result.parasitics->approximate ? "true" : "false",
      result.parasitics->identity,
      cache_path.empty() ? "false" : "true",
      cache.hit ? "true" : "false",
      cache.reused,
      cache.changed);
  {
    rapidjson::Document json;
    json.Parse(result.qor.c_str());
    auto&            allocator = json.GetAllocator();
    rapidjson::Value technology_statistics(rapidjson::kObjectType);
    technology_statistics.AddMember("liberty_file_reads", uint64_t(options.technology().cells.file_reads), allocator);
    technology_statistics.AddMember("lef_parses", uint64_t(options.lefs.size()), allocator);
    technology_statistics.AddMember("lef_hash_reads", uint64_t(options.lefs.size()), allocator);
    json.AddMember("technology_snapshot", technology_statistics, allocator);
    json.AddMember("gp_iterations", metrics.iterations, allocator);
    json.AddMember("gp_target_density", control.target_density, allocator);
    json.AddMember("gp_overflow_floor", cache.hit ? rapidjson::Value() : rapidjson::Value(metrics.overflow_floor), allocator);
    json.AddMember("gp_density_ready_iteration", metrics.density_iteration, allocator);
    json.AddMember("gp_continuation_deadline", metrics.continuation_deadline, allocator);
    json.AddMember("gp_objective_converged", metrics.objective_converged, allocator);
    json.AddMember("gp_timing_satisfied",
                   options.gp_rank ? rapidjson::Value() : rapidjson::Value(metrics.timing_satisfied),
                   allocator);
    const char* stop_reason = cache.hit                                                                   ? "cache_hit"
                              : options.timing_seed || options.retain_placement                           ? "snapshot"
                              : metrics.converged                                                         ? "converged"
                              : metrics.density_stalled && metrics.overflow_floor > control.stop_overflow ? "density_infeasible"
                              : metrics.density_stalled                                                   ? "density_stalled"
                              : metrics.route_stalled                                                     ? "route_stalled"
                              : metrics.continuation_limit                                                ? "continuation_limit"
                              : metrics.iterations >= control.max_iterations                              ? "iteration_limit"
                                                                                                          : "stopped";
    json.AddMember("gp_stop_reason", rapidjson::Value(stop_reason, allocator), allocator);
    add_uroute_status(json, allocator, metrics.uroute_skipped, metrics.uroute_sample_ids);
    json.AddMember(
        "route_model",
        cache.hit ? rapidjson::Value(rapidjson::kNullType) : rapidjson::Value(dp::route_model_name(metrics.route_model), allocator),
        allocator);
    json.AddMember("cells_legalized",
                   options.full && !defer_macro_cells && !options.timing_seed && !options.retain_placement,
                   allocator);
    json.AddMember("macro_gp_timing_for_resynthesis", bool(result.macro_gp_timing), allocator);
    json["virtual_fillers"].SetBool(options.virtual_fillers);
    json.AddMember("fp_warpspace", options.fp_warp.enabled, allocator);
    json.AddMember("macro_warpspace", options.macro_warp.enabled, allocator);
    if (metrics.magnet) {
      const auto&      magnet = *metrics.magnet;
      rapidjson::Value details(rapidjson::kObjectType), shape(rapidjson::kArrayType);
      size_t           members = 0, rounds = 0;
      double           largest_area = 0, largest_width = 0, largest_height = 0;
      for (const auto& attempt : magnet.rounds) {
        rounds += attempt.reason == "merged";
      }
      for (const auto& group : magnet.mapping.groups) {
        members   += group.members.size();
        double xl = std::numeric_limits<double>::infinity(), yl = xl, xh = -xl, yh = -xl;
        for (const auto& member : group.members) {
          const auto i = member.source;
          xl           = std::min(xl, v.design.node_x[i]);
          yl           = std::min(yl, v.design.node_y[i]);
          xh           = std::max(xh, v.design.node_x[i] + v.design.node_size_x[i]);
          yh           = std::max(yh, v.design.node_y[i] + v.design.node_size_y[i]);
        }
        if ((xh - xl) * (yh - yl) > largest_area) {
          largest_area   = (xh - xl) * (yh - yl);
          largest_width  = xh - xl;
          largest_height = yh - yl;
        }
      }
      shape.PushBack(largest_width / tech.dbu, allocator).PushBack(largest_height / tech.dbu, allocator);
      details.AddMember("rounds", uint64_t(rounds), allocator);
      details.AddMember("groups", uint64_t(magnet.mapping.groups.size()), allocator);
      details.AddMember("members", uint64_t(members), allocator);
      details.AddMember("largest_shape_um", shape, allocator);
      details.AddMember("stopped_settled", magnet.stopped_settled, allocator);
      details.AddMember("seconds", metrics.magnet_seconds, allocator);
      json.AddMember("macro_magnet", details, allocator);
      std::vector<std::string> names;
      for (const auto& cell : v.cells) {
        names.push_back(cell.name);
      }
      write_magnet_report(magnet, names, tech.dbu, options.output_dir);
    }
    if (macro_replay) {
      macro_replay->motion_history = std::move(macro_replay_motion->report->motion_history);
      std::vector<std::string> names;
      for (const auto& cell : v.cells) {
        names.push_back(cell.name);
      }
      write_magnet_report(*macro_replay, names, tech.dbu, options.output_dir);
    }

    json.AddMember("preserve_imported_ties", options.preserve_imported_ties, allocator);
    rapidjson::Value controls(rapidjson::kObjectType), ports(rapidjson::kArrayType), pins(rapidjson::kArrayType);
    rapidjson::Value reset_ports(rapidjson::kArrayType), reset_pins(rapidjson::kArrayType);
    for (const auto& name : v.prects.ports) {
      ports.PushBack(rapidjson::Value(name.c_str(), allocator), allocator);
    }
    for (const auto& name : v.prects.pins) {
      pins.PushBack(rapidjson::Value(name.c_str(), allocator), allocator);
    }
    for (const auto& name : v.prects.reset_ports) {
      reset_ports.PushBack(rapidjson::Value(name.c_str(), allocator), allocator);
    }
    for (const auto& name : v.prects.reset_pins) {
      reset_pins.PushBack(rapidjson::Value(name.c_str(), allocator), allocator);
    }
    controls.AddMember("override", v.prects.override_controls, allocator);
    controls.AddMember("ports", ports, allocator);
    controls.AddMember("pins", pins, allocator);
    controls.AddMember("reset_ports", reset_ports, allocator);
    controls.AddMember("reset_pins", reset_pins, allocator);
    controls.AddMember("ignored_nets", uint64_t(std::count(v.ignored.ignored.begin(), v.ignored.ignored.end(), true)), allocator);
    json.AddMember("prects_controls", controls, allocator);
    if (options.growth_budget) {
      rapidjson::Document growth;
      growth.Parse(growth_budget_report(*result.graph, options).c_str());
      json.AddMember("growth_budget", rapidjson::Value(growth, allocator), allocator);
    }
    json.AddMember("macro_fill_cell_space", control.filler_cell_space, allocator);
    json.AddMember("macro_filler_fraction", control.filler_macro_fraction, allocator);
    json.AddMember("virtual_filler_count", metrics.filler_count, allocator);
    json.AddMember("virtual_filler_area_um2", metrics.filler_area / tech.dbu / tech.dbu, allocator);
    json.AddMember("virtual_filler_size_x_um", metrics.filler_size_x / tech.dbu, allocator);
    json.AddMember("virtual_filler_size_y_um", metrics.filler_size_y / tech.dbu, allocator);
    json.AddMember("virtual_filler_count_limited", metrics.filler_count_limited, allocator);
    rapidjson::Value halo(rapidjson::kObjectType);
    halo.AddMember("x_um", cell_halo_x, allocator);
    halo.AddMember("y_um", cell_halo_y, allocator);
    halo.AddMember("x_dbu", v.halo_x, allocator);
    halo.AddMember("y_dbu", v.halo_y, allocator);
    halo.AddMember(
        "macros",
        static_cast<uint64_t>(std::count_if(
            v.cells.begin(),
            v.cells.end(),
            [&](const Cell& c) { return (v.halo_x || v.halo_y) && !tech.macros.at(c.macro).klass.starts_with("CORE"); })),
        allocator);
    json.AddMember("halo", halo, allocator);
    if (options.cell_halo) {
      rapidjson::Value macro_halo_json(rapidjson::kObjectType), cell_halo_json(rapidjson::kObjectType);
      macro_halo_json.AddMember("x_um", options.halo_x, allocator);
      macro_halo_json.AddMember("y_um", options.halo_y, allocator);
      macro_halo_json.AddMember("x_dbu", macro_halo(options.halo_x, tech.dbu, sw), allocator);
      macro_halo_json.AddMember("y_dbu", macro_halo(options.halo_y, tech.dbu, rh), allocator);
      cell_halo_json.AddMember("x_um", options.cell_halo->first, allocator);
      cell_halo_json.AddMember("y_um", options.cell_halo->second, allocator);
      cell_halo_json.AddMember("active", options.cell_stage && !options.timing_seed && !options.retain_placement, allocator);
      cell_halo_json.AddMember("effective_x_um", cell_halo_x, allocator);
      cell_halo_json.AddMember("effective_y_um", cell_halo_y, allocator);
      json.AddMember("macro_halo", macro_halo_json, allocator);
      json.AddMember("cell_halo", cell_halo_json, allocator);
    }

    if (options.reset_fanout) {
      rapidjson::Value tree(rapidjson::kObjectType);
      tree.AddMember("fanout", options.reset_fanout, allocator);
      // nets/sinks/added/levels describe this call; the seed placement of a timing-driven run already built the
      // tree, so `buffers` counts the tree cells present in the placed netlist.
      tree.AddMember("nets", reset_tree.nets, allocator);
      tree.AddMember("sinks", reset_tree.sinks, allocator);
      tree.AddMember("added", reset_tree.buffers, allocator);
      tree.AddMember("buffers",
                     static_cast<uint64_t>(
                         std::count_if(v.cells.begin(), v.cells.end(), [](const Cell& c) { return is_reset_tree_buffer(c.name); })),
                     allocator);
      tree.AddMember("levels", reset_tree.levels, allocator);
      tree.AddMember("skipped", reset_tree.skipped, allocator);
      json.AddMember("reset_tree", tree, allocator);
    }
    if (options.domino_rail_fanout && !options.domino_rail_pins.empty()) {
      rapidjson::Value tree(rapidjson::kObjectType);
      tree.AddMember("fanout", options.domino_rail_fanout, allocator);
      tree.AddMember("nets", rail_tree.nets, allocator);
      tree.AddMember("sinks", rail_tree.sinks, allocator);
      tree.AddMember("added", rail_tree.buffers, allocator);
      tree.AddMember("levels", rail_tree.levels, allocator);
      tree.AddMember("buffers",
                     static_cast<uint64_t>(
                         std::count_if(v.cells.begin(), v.cells.end(), [](const Cell& c) { return is_rail_tree_buffer(c.name); })),
                     allocator);
      json.AddMember("domino_rail_tree", tree, allocator);
    }
    if (macro_run) {
      rapidjson::Value macro(rapidjson::kObjectType);
      macro.AddMember("mode", "h2d", allocator);
      macro.AddMember("cache_hit", macro_run->cache_hit, allocator);
      macro.AddMember("groups", static_cast<uint64_t>(macro_run->result.topology.groups.size()), allocator);
      macro.AddMember("iterations", static_cast<uint64_t>(macro_run->result.iterations.size()), allocator);
      macro.AddMember("legal", true, allocator);
      macro.AddMember("topology_file", "macro_topology.json", allocator);
      macro.AddMember("image_file", "h2d.png", allocator);
      json.AddMember("macro", macro, allocator);
    }

    rapidjson::Document details;
    const auto          text = std::format(
        R"({{"used":{},"single_cell":{},"released_locks":{},"attempts":{},"moved_cells":{},"max_displacement_um":{},"window_dbu":[{},{},{},{}]}})",
        eco.used,
        eco.single_cell,
        eco.released_locks,
        eco.attempts,
        eco.moved,
        eco.max_displacement_um,
        eco.window.xl,
        eco.window.yl,
        eco.window.xh,
        eco.window.yh);
    details.Parse(text.c_str());
    if (options.eco_connectivity_weight > 0) {
      auto&            a = details.GetAllocator();
      rapidjson::Value connectivity(rapidjson::kObjectType);
      connectivity.AddMember("weight", options.eco_connectivity_weight, a);
      connectivity.AddMember("nets", static_cast<uint64_t>(eco.connectivity_nets), a);
      connectivity.AddMember("dirty_pins", static_cast<uint64_t>(eco.connectivity_pins), a);
      connectivity.AddMember("candidate_sites", static_cast<uint64_t>(eco.candidate_sites), a);
      connectivity.AddMember("candidate_rows", static_cast<uint64_t>(eco.candidate_rows), a);
      connectivity.AddMember("search_seconds", eco.connectivity_seconds, a);
      details.AddMember("connectivity", connectivity, a);
    }
    if (!options.eco_trace_cells.empty()) {
      auto&            trace_allocator = details.GetAllocator();
      rapidjson::Value traces(rapidjson::kArrayType);
      for (const auto& entry : eco.trace) {
        rapidjson::Value item(rapidjson::kObjectType);
        item.AddMember("name", rapidjson::Value(entry.name.c_str(), trace_allocator), trace_allocator);
        item.AddMember("master", rapidjson::Value(entry.master.c_str(), trace_allocator), trace_allocator);
        item.AddMember("edit_order", static_cast<uint64_t>(entry.order), trace_allocator);
        item.AddMember("row_index", static_cast<uint64_t>(entry.row), trace_allocator);
        const auto pair = [&](int64_t a, int64_t b) {
          rapidjson::Value values(rapidjson::kArrayType);
          values.PushBack(a, trace_allocator).PushBack(b, trace_allocator);
          return values;
        };
        item.AddMember("seed_dbu", pair(entry.seed.x, entry.seed.y), trace_allocator);
        item.AddMember("chosen_dbu", pair(entry.chosen.x, entry.chosen.y), trace_allocator);
        item.AddMember("gap_dbu", pair(entry.gap_lo, entry.gap_hi), trace_allocator);
        item.AddMember("row_y_dbu", entry.chosen.y, trace_allocator);
        item.AddMember(
            "displacement_um",
            (std::abs(double(entry.chosen.x - entry.seed.x)) + std::abs(double(entry.chosen.y - entry.seed.y))) / tech.dbu,
            trace_allocator);
        traces.PushBack(item, trace_allocator);
      }
      details.AddMember("trace", traces, trace_allocator);
    }
    json.AddMember("eco", rapidjson::Value(details, json.GetAllocator()), json.GetAllocator());
    if (multirow_eco_report) {
      // The two-row ECO; sites count every row a cell spans.
      using Outcome = Multirow_eco_cell::Outcome;
      constexpr std::array    outcomes{Outcome::in_place, Outcome::shifted, Outcome::displaced, Outcome::re_anchored};
      std::array<uint64_t, 4> counts{};
      int64_t                 released = 0, grown = 0;
      uint64_t                rail_twins       = 0;
      double                  max_displacement = 0;
      for (const auto& cell : multirow_eco_report->cells) {
        ++counts[static_cast<size_t>(cell.outcome)];
        rail_twins        += cell.rail_twin;
        released         += cell.released_sites;
        grown            += cell.grown_sites;
        max_displacement  = std::max(max_displacement, cell.displacement_um);
      }
      rapidjson::Value block(rapidjson::kObjectType);
      block.AddMember("cells", static_cast<uint64_t>(multirow_eco_report->cells.size()), allocator);
      for (const auto outcome : outcomes) {
        const auto name = multirow_eco_outcome_name(outcome);
        block.AddMember(rapidjson::StringRef(name.data(), name.size()), counts[static_cast<size_t>(outcome)], allocator);
      }
      block.AddMember("released_sites", released, allocator);
      block.AddMember("grown_sites", grown, allocator);
      block.AddMember("rail_twins", rail_twins, allocator);
      block.AddMember("displaced_cells", static_cast<uint64_t>(multirow_eco_report->displaced.size()), allocator);
      block.AddMember("max_displacement_um", max_displacement, allocator);
      block.AddMember("shift_sites", options.multirow_eco->shift_sites, allocator);
      block.AddMember("max_displaced", options.multirow_eco->max_displaced, allocator);
      block.AddMember("bins_x", multirow_eco_grid.first, allocator);
      block.AddMember("bins_y", multirow_eco_grid.second, allocator);
      json.AddMember("multirow_eco", block, allocator);
    }
    if (sat_report) {
      rapidjson::Value sat(rapidjson::kObjectType);
      sat.AddMember("stage", "final_cell", allocator);
      sat.AddMember("accepted", static_cast<uint64_t>(sat_report->accepted.size()), allocator);
      sat.AddMember("retained", static_cast<uint64_t>(sat_retained), allocator);
      sat.AddMember("windows", sat_report->windows, allocator);
      sat.AddMember("unknown", sat_report->unknown, allocator);
      sat.AddMember("seconds", sat_report->seconds, allocator);
      sat_window_statistics(sat, *sat_report, options.satlegalize_options, allocator);
      rapidjson::Value relations(rapidjson::kArrayType);
      for (size_t i = 0; i < sat_retained; ++i) {
        const auto&      pair = sat_report->accepted[i];
        rapidjson::Value relation(rapidjson::kObjectType);
        relation.AddMember("first", rapidjson::Value(v.cells.at(pair.first).name.c_str(), allocator), allocator);
        relation.AddMember("first_pin", rapidjson::Value(pair.first_pin.c_str(), allocator), allocator);
        relation.AddMember("second", rapidjson::Value(v.cells.at(pair.second).name.c_str(), allocator), allocator);
        relation.AddMember("second_pin", rapidjson::Value(pair.second_pin.c_str(), allocator), allocator);
        relations.PushBack(relation, allocator);
      }
      sat.AddMember("relations", relations, allocator);
      json.AddMember("satlegalize", sat, allocator);
    }
    if (multirow_report) {
      // The anchor prepass of the two-row cells; start parity counts from the
      // first core row, as the row-access convention does.
      rapidjson::Value multirow(rapidjson::kObjectType);
      multirow.AddMember("cells", static_cast<uint64_t>(multirow_report->anchors.size()), allocator);
      multirow.AddMember("even_starts", static_cast<uint64_t>(multirow_report->even_starts), allocator);
      multirow.AddMember("odd_starts", static_cast<uint64_t>(multirow_report->odd_starts), allocator);
      multirow.AddMember("rail_twins", static_cast<uint64_t>(multirow_report->rail_twins), allocator);
      multirow.AddMember("candidates", static_cast<uint64_t>(multirow_report->candidates), allocator);
      multirow.AddMember("full_core_scans", static_cast<uint64_t>(multirow_report->full_core_scans), allocator);
      multirow.AddMember("work", multirow_report->work, allocator);
      multirow.AddMember("total_displacement_um", multirow_report->total_displacement_um, allocator);
      multirow.AddMember("max_displacement_um", multirow_report->max_displacement_um, allocator);
      multirow.AddMember("templates", static_cast<uint64_t>(options.multirow_templates.size()), allocator);
      multirow.AddMember("row_access",
                         options.row_access ? rapidjson::Value(options.row_access->name.c_str(), allocator)
                                            : rapidjson::Value(rapidjson::kNullType),
                         allocator);
      json.AddMember("multirow", multirow, allocator);
    }
    rapidjson::StringBuffer                    buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    json.Accept(writer);
    result.qor = buffer.GetString();
  }
  {
    std::ofstream out(options.output_dir + "/qor.json");
    out << result.qor << '\n';
    if (!out) {
      throw std::runtime_error("cannot write placement QoR");
    }
  }
  if (options.full && !options.timing_seed && !options.fp_place) {
    save_cache(v, cache_path, env, hashes);
    if (!cache_path.empty()) {
      ph::write_def(v.fp, options.cache_dir + "/floorplan.def");
    }
  }
  for (const auto& driver : v.drivers) {
    const auto name = driver.label();
    if (v.weights.contains(name)) {
      result.net_weights.emplace(name, v.weights.at(name));
    }
  }
  result.prects = v.prects;
  if (options.retain_preroute_design && final_handoff) {
    auto snapshot    = std::make_shared<Preroute_design>();
    snapshot->design = std::move(v.design);
    snapshot->pin_labels.reserve(v.pins.size());
    std::set<std::string> excluded_drivers;
    const auto            exclusion_memories = memory_proxies(*result.graph, tech, options.liberty);
    Resolver              exclusion_resolver(tech, exclusion_memories, options.unknown_const_zero);
    const auto            collect_exclusions = [&](const Node& node) {
      for (auto pin : node.out_sorted_pins()) {
        if (!gu::preroute_net_excluded(pin)) {
          continue;
        }
        for (int bit = 0; bit < std::max(1, gu::bits_of(pin)); ++bit) {
          const auto endpoint = exclusion_resolver.bit(pin, bit);
          if (endpoint.constant < 0) {
            excluded_drivers.insert(endpoint.label());
          }
        }
      }
    };
    collect_exclusions(result.graph->get_input_node());
    for (auto node : result.graph->body().nodes()) {
      collect_exclusions(node);
    }
    for (const auto& pin : v.pins) {
      snapshot->pin_labels.push_back(pin.label());
      const auto cell = v.cell.find(pin.instance);
      if (excluded_drivers.contains(snapshot->pin_labels.back())
          || (cell != v.cell.end() && v.cells.at(cell->second).node.attr(livehd::attrs::preroute_exclude).has())) {
        snapshot->dont_touch_pins.insert(snapshot->pin_labels.back());
      }
      if (cell != v.cell.end() && v.constant_masters.contains(v.cells.at(cell->second).macro)) {
        snapshot->tie_pins.insert(snapshot->pin_labels.back());
      }
    }
    if (options.satlegalize_route && (sat_report || terminal_report)) {
      const physical::Signal_metal_rules route_rules(tech, v.fp);
      std::vector<size_t>                obstacles(v.fp.components.size());
      std::iota(obstacles.begin(), obstacles.end(), 0);
      std::map<std::string, size_t> component_index;
      for (size_t i = 0; i < v.fp.components.size(); ++i) {
        component_index.emplace(v.fp.components[i].name, i);
      }
      const auto&                                alignments = terminal_report ? terminal_report->alignments : sat_report->accepted;
      const auto                                 retained   = terminal_report ? alignments.size() : sat_retained;
      // The final frame is shared by every retained pair. Build its physical
      // access index once; per-net search budgets must not repeatedly pay for
      // copying the whole design and discovering all unrelated pin escapes.
      std::optional<physical::Routing_occupancy> route_occupancy;
      std::unique_ptr<Pin_escape_guard>          pin_escapes;
      std::unique_ptr<Signal_router>             router;
      std::map<std::set<std::string>, const physical::Net*> two_pin_nets;
      if (options.satlegalize_route_vias && retained) {
        route_occupancy.emplace(physical::routing_occupancy(tech, v.fp, v.fp.dbu));
        pin_escapes = std::make_unique<Pin_escape_guard>(tech,
                                                         v.fp,
                                                         *route_occupancy,
                                                         1024,
                                                         std::function<void()>{},
                                                         tech_profile->route.access,
                                                         &route_rules);
        router      = std::make_unique<Signal_router>(tech,
                                                      v.fp,
                                                      *route_occupancy,
                                                      route_rules,
                                                      signal_route_profile(*tech_profile, tech),
                                                      pin_escapes.get());
        for (const auto& net : v.fp.nets) {
          if (net.connections.size() == 2) {
            std::set<std::string> pins;
            for (const auto& connection : net.connections) {
              pins.insert(connection.instance + ":" + connection.pin);
            }
            two_pin_nets.emplace(std::move(pins), &net);
          }
        }
      }
      for (size_t i = 0; i < retained; ++i) {
        auto pair                     = alignments.at(i);
        pair.first                    = component_index.at(v.cells.at(pair.first).name);
        pair.second                   = component_index.at(v.cells.at(pair.second).name);
        const auto&                 a = v.fp.components.at(pair.first);
        const auto&                 b = v.fp.components.at(pair.second);
        physical::Shape             witness;
        const std::set<std::string> endpoints{a.name + ":" + pair.first_pin, b.name + ":" + pair.second_pin};
        bool aligned = satlegalize_aligned(tech, v.fp, pair, a.place, b.place, obstacles, &witness, &route_rules, nullptr);
        if (!aligned && router) {
          const auto net = two_pin_nets.find(endpoints);
          if (net != two_pin_nets.end()) {
            const Signal_route_limits limits{1024, 256, std::chrono::steady_clock::now() + std::chrono::milliseconds(5)};
            const auto                probe = router->probe_route(*net->second, {limits, {}, false, true});
            if (probe.routable && probe.corridor) {
              witness = *probe.corridor;
              aligned = true;
            }
          }
        }
        if (aligned) {
          snapshot->sat_routes.emplace(endpoints, witness);
        }
      }
    }
    result.preroute_design = std::move(snapshot);
  }
  if (export_final_features) {
    // Reports, cache entries and retained preroute data have consumed the
    // solver arrays. Rebuild them in place for export instead of copying the
    // entire View alongside final timing/RC. Only row preparation changes the
    // returned floorplan; preserve its original segments and blockages.
    if (handoff_grid) {
      std::swap(v.fp, *handoff_grid);
    }
    auto rows      = v.fp.rows;
    auto blockages = v.fp.blockages;
    prepare_rows(tech, v.fp);
    make_arrays(v, tech);
    const int nx = route_grid_bins(control.auxiliary_bins, metrics.bins_x ? metrics.bins_x : options.bins);
    const int ny = route_grid_bins(control.auxiliary_bins, metrics.bins_y ? metrics.bins_y : options.bins);
    export_uroute(fs::path(options.output_dir) / "uroute_export",
                  v.design,
                  uroute_id(v),
                  nx,
                  ny,
                  feature_capacity_h,
                  feature_capacity_v);
    v.fp.rows      = std::move(rows);
    v.fp.blockages = std::move(blockages);
    if (handoff_grid) {
      std::swap(v.fp, *handoff_grid);
    }
  }
  result.floorplan        = std::move(v.fp);
  result.fp_resized_cells = fp_resized;
  result.fp_repair_edits  = fp_repairs;
  notify_after_stage      = !terminal_active;
  return result;
}

static Result place_single(hhds::Graph& source, hhds::GraphLibrary& library, const Options& options,
                           const std::function<void()>& release_source = {}) {
  bool notify_after_stage = false;
  auto result             = place_single_frame(source, library, options, release_source, notify_after_stage);
  // Solver arrays, endpoint indices and reporting temporaries are no longer
  // live while the persisted RC text is allocated. Consumers and stage
  // callbacks still receive the same fully annotated graph.
  // A zero-wire seed is an in-memory intermediate; color-only analysis has no
  // electrical snapshot. Neither should acquire a persisted RC attribute.
  if (result.parasitics && !options.timing_seed) {
    result.graph->get_input_node().attr(attrs::placement_rc).set(ph::serialize_parasitics(*result.parasitics));
  }
  if (notify_after_stage && options.after_stage && !options.timing_seed && options.after_stage(options.completed_stage, result)) {
    throw Placement_restart(result.floorplan, options.completed_stage);
  }
  return result;
}

Result place(hhds::Graph& source, hhds::GraphLibrary& library, const Options& requested) {
  return place(source, library, requested, {});
}

Result place(hhds::Graph& source, hhds::GraphLibrary& library, const Options& requested,
             const std::function<void()>& release_source) {
  if (release_source && source.get_io() && source.get_io()->get_library() == &library) {
    throw std::invalid_argument("releasing placement input requires a separate destination library");
  }
  if (requested.gp_search.enabled && requested.gp_resize) {
    throw std::invalid_argument("gp_ab requires fixed topology; disable gp_resize (post-placement resize remains available)");
  }
  if (!std::isfinite(requested.rc_pessimism) || requested.rc_pessimism < 1) {
    throw std::invalid_argument("rc_pessimism must be finite and at least 1");
  }
  if (!std::isfinite(requested.vanished_memory_initial_scale) || requested.vanished_memory_initial_scale < 0
      || requested.vanished_memory_initial_scale > 1 || requested.vanished_memory_growth_iterations < 1
      || requested.vanished_memory_growth_iterations > 1000000 || requested.vanished_memory_start < 0
      || requested.vanished_memory_start > 1000000
      || (!requested.vanished_memory
          && (requested.vanished_memory_initial_scale != .6 || requested.vanished_memory_growth_iterations != 25
              || requested.vanished_memory_start != 70))) {
    throw std::invalid_argument(
        "vanished_memory initial scale must be in [0,1], growth iterations in [1,1000000], start in [0,1000000]; nondefaults "
        "require vanished_memory");
  }
  if (!std::isfinite(requested.vanished_memory_local_floor) || requested.vanished_memory_local_floor < 0
      || requested.vanished_memory_local_floor > requested.local_density_weight
      || (requested.vanished_memory_local_floor != 0 && !requested.vanished_memory)) {
    throw std::invalid_argument(
        "vanished_memory_local_floor requires vanished_memory and a value from zero to local_density_weight");
  }
  if (requested.repair_reset) {
    throw std::invalid_argument(
        "pre-CTS clock/reset sizing is disabled; repair_reset must be false (reset_fanout builds the reset tree)");
  }
  validate_busorder_options(requested.busorder);
  validate_region_options(requested.region);
  validate_boundary_pin_options(requested.boundarypins);
  if (requested.busorder.enabled && (requested.boundarypins.enabled || requested.boundarypins.align)) {
    throw std::invalid_argument("busorder and boundarypins are separate experiments; enable only one");
  }
  validate_warp_options(requested.fp_warp);
  validate_warp_options(requested.macro_warp);
  requested.macro_magnet.validate();
  if (requested.macro_magnet.enabled
      && (!requested.full || requested.macro_mode != "none" || requested.timing_seed || requested.retain_placement
          || requested.analyze_colors_only || requested.previous)) {
    throw std::invalid_argument("macro magnet requires native full macro placement with movable macros");
  }
  validate_cell_halo(requested);
  if (requested.cell_halo && !requested.full && !requested.timing_seed && !requested.retain_placement) {
    throw std::invalid_argument("cell halo requires full placement with a final cell stage");
  }
  (void)requested.technology();
  std::optional<Options> resolved_options;
  if (requested.cell_halo || requested.boundarypins.align) {
    resolved_options = requested;
    if (requested.boundarypins.align) {
      resolved_options->boundarypins.enabled = true;
    }
  }
  const auto& options = resolved_options ? *resolved_options : requested;
  if (options.final_iterations < 0) {
    throw std::invalid_argument("invalid final cell iteration budget");
  }
  bool stage_macros = false;
  if (!options.fp_place && options.full && !options.timing_seed && !options.cell_stage && !options.lefs.empty()) {
    const auto technology = options.technology().lef;
    for (const auto& graph : source.definitions().graphs()) {
      for (auto node : graph->body().nodes()) {
        if (gu::type_op_of(node) == Ntype_op::Memory) {
          stage_macros = true;
        }
        if (auto io = node.get_subnode_io()) {
          const auto found = technology.macros.find(std::string(io->get_name()));
          if (found != technology.macros.end() && !found->second.klass.starts_with("CORE")) {
            stage_macros = true;
          }
        }
      }
    }
  }
  if (options.cell_halo && !options.cell_stage && !options.fp_place && !stage_macros && !options.timing_seed
      && !options.retain_placement) {
    *resolved_options = final_cell_options(options);  // CORE-only solve has no macro-spacing ambiguity.
  }
  if (options.cell_halo && !options.cell_stage && stage_macros && !options.final_cells && !options.timing_seed
      && !options.retain_placement) {
    throw std::invalid_argument("cell halo with movable macros requires the final cell stage");
  }
  if (options.cell_halo && !options.cell_stage && stage_macros && options.previous && !options.timing_seed
      && !options.retain_placement) {
    throw std::invalid_argument("cell halo with a previous macro frame requires explicit final_cell_options context");
  }
  if (stage_macros && options.full && !options.final_cells && !options.timing_seed && !options.retain_placement && !options.previous
      && !options.cell_stage) {
    auto macro_options            = options;
    macro_options.completed_stage = Placement_stage::macro;
    if (options.after_stage) {
      macro_options.after_stage = std::ref(options.after_stage);
    }
    return place_single(source, library, macro_options, release_source);
  }
  if ((!options.fp_place && !stage_macros) || !options.final_cells || !options.full || options.timing_seed || options.previous) {
    return place_single(source, library, options, release_source);
  }
  // Keep the macro frame in a separate library: the returned final graph must
  // belong to the caller's library, and the original synthesis graph stays intact.
  hhds::GraphLibrary macro_library;
  auto               macro_options = options;
  macro_options.final_cells        = false;
  macro_options.completed_stage    = Placement_stage::macro;
  macro_options.cache_dir.clear();
  macro_options.output_dir += "/macro";
  // Both staged passes select from one pool snapshot, reusing a matching host
  // preload, so the manifest and weights are hashed once.
  if (uroute_pool_used(options)) {
    macro_options.uroute_pool = load_uroute_pool(options);
  }
  const auto  start          = std::chrono::steady_clock::now();
  auto        macro          = place_single(source, macro_library, macro_options);
  const auto  macro_end      = std::chrono::steady_clock::now();
  const auto& tech           = options.technology().lef;
  const auto  staged_options = resolve_macro_halo(options, tech.sites.at(macro.floorplan.rows.front().site).height);
  int         fixed_macros   = 0;
  for (auto& c : macro.floorplan.components) {
    const auto master = tech.macros.find(c.macro);
    if (master == tech.macros.end() || !master->second.klass.starts_with("CORE")) {
      c.place.status = Status::FIXED;
      ++fixed_macros;
    }
  }
  std::map<std::string, Status> pin_status;
  for (auto& terminal : macro.floorplan.terminals) {
    pin_status.emplace(terminal.name, terminal.status);
    terminal.status = Status::FIXED;
  }
  // Macro planning has already spread the cells around the chosen macro/IO
  // frame. Retain that geometry as a movable initial guess, including when
  // cell legalization/detail were deferred. Importing fixed.def alone drops
  // all non-fixed poses and needlessly starts final GP from scratch.
  const auto cell_seed = std::make_shared<physical::Floorplan>(macro.floorplan);
  if (!options.macro_detailed_place || !options.macro_legalize_cells) {
    // Final placement rebuilds movable cells/connectivity from source and
    // imports only fixed poses. Pass the actual handoff, not provisional cells.
    std::erase_if(macro.floorplan.components,
                  [](const auto& cell) { return cell.place.status != Status::FIXED && cell.place.status != Status::LOCKED; });
    macro.floorplan.nets.clear();
  }
  const auto frame = macro_options.output_dir + "/fixed.def";
  ph::write_def(macro.floorplan, frame);
  auto cell_options             = final_cell_options(staged_options);
  cell_options.uroute_pool      = macro_options.uroute_pool;
  cell_options.stage_pin_status = std::move(pin_status);
  if (!options.final_independent_timing) {
    cell_options.net_weights = macro.net_weights;
  }
  cell_options.fp_place              = false;
  // FP centroids and macro warp pulls end at the fixed-macro handoff.
  cell_options.fp_warp.enabled       = false;
  cell_options.macro_warp.enabled    = false;
  cell_options.macro_filler_fraction = 0;
  cell_options.macro_fill_cell_space = false;
  cell_options.macro_magnet.enabled  = false;
  cell_options.completed_stage       = Placement_stage::final;
  cell_options.restart_seed          = cell_seed;
  rapidjson::Document macro_seed_qor;
  macro_seed_qor.Parse(macro.qor.c_str());
  if (macro_seed_qor.HasMember("gp_converged") && macro_seed_qor["gp_converged"].GetBool()) {
    cell_options.density_weight_seed = macro.gp_density_weight;
  }
  cell_options.fp_pin_adjust             = false;
  cell_options.macro_pin_adjust          = false;
  cell_options.final_cells               = false;
  cell_options.macro_mode                = "none";
  cell_options.macro_snap                = false;
  cell_options.macro_align_weight        = 0;
  cell_options.macro_cell_density_weight = 0;
  cell_options.macro_density_pull_weight = 0;
  cell_options.floorplan                 = frame;
  cell_options.iterations                = options.final_iterations ? options.final_iterations : options.iterations;
  auto result                            = place_single((macro.fp_resized_cells || macro.fp_repair_edits) ? *macro.graph : source,
                                                        library,
                                                        cell_options,
                                                        release_source);
  result.fp_resized_cells = macro.fp_resized_cells;
  result.fp_repair_edits  = macro.fp_repair_edits;
  if (options.macro_magnet.enabled) {
    for (const auto* artifact : {"magnet.json", "macro_animation.html"}) {
      fs::copy_file(macro_options.output_dir + "/" + artifact,
                    options.output_dir + "/" + artifact,
                    fs::copy_options::overwrite_existing);
    }
    rapidjson::Document macro_qor, final_qor;
    macro_qor.Parse(macro.qor.c_str());
    final_qor.Parse(result.qor.c_str());
    if (macro_qor.HasMember("macro_magnet")) {
      auto& allocator = final_qor.GetAllocator();
      final_qor.AddMember("macro_magnet", rapidjson::Value(macro_qor["macro_magnet"], allocator), allocator);
      rapidjson::StringBuffer                    buffer;
      rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
      final_qor.Accept(writer);
      result.qor = buffer.GetString();
      std::ofstream qor(options.output_dir + "/qor.json");
      if (!(qor << result.qor << '\n')) {
        throw std::runtime_error("cannot write staged magnet QoR");
      }
    }
  }
  const auto end     = std::chrono::steady_clock::now();
  const auto seconds = [](auto a, auto b) { return std::chrono::duration<double>(b - a).count(); };
  auto       text    = std::format(
      R"({{"schema_version":1,"mode":"{}","centroid_snap":{},"macro_snap":{},"macro_detailed":{},"macro_legalize_cells":{},"final_independent_timing":{},"final_centroid_anchors":false,"fixed_macros":{},"macro_source_hpwl_um":{},"final_hpwl_um":{},"macro_source_seconds":{},"final_cells_seconds":{}}})",
      options.fp_place ? "floorplan-macro-cells" : "macro-cells",
      options.fp_snap_enabled,
      options.macro_snap,
      options.macro_detailed_place,
      options.macro_legalize_cells,
      options.final_independent_timing,
      fixed_macros,
      [&] {
        rapidjson::Document q;
        q.Parse(macro.qor.c_str());
        return q["hpwl_um"].GetDouble();
      }(),
      [&] {
        rapidjson::Document q;
        q.Parse(result.qor.c_str());
        return q["hpwl_um"].GetDouble();
      }(),
      seconds(start, macro_end),
      seconds(macro_end, end));
  if (options.cell_halo) {
    text.pop_back();
    text += std::format(R"(,"macro_halo_um":[{},{}],"cell_halo_um":[{},{}]}})",
                        staged_options.halo_x,
                        staged_options.halo_y,
                        options.cell_halo->first,
                        options.cell_halo->second);
  }
  std::ofstream out(options.output_dir + "/flow.json");
  if (!(out << text << '\n')) {
    throw std::runtime_error("cannot write staged placement report");
  }
  return result;
}
}  // namespace livehd::placement
