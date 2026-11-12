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
#include "cell_criticality.hpp"
#include "color_centroids.hpp"
#include "core/diag.hpp"
#include "def_binding.hpp"
#include "dreamplace/native/placement.hpp"
#include "dreamplace/native/rudy.hpp"
#include "filler_budget.hpp"
#include "forces/gp_rejection.hpp"
#include "forces/gp_timing.hpp"
#include "gui/gui_session.hpp"
#include "forces/legalization.hpp"
#include "fp_repetition_graph.hpp"
#include "growth_budget.hpp"
#include "h2d.hpp"
#include "handoff_pins.hpp"
#include "handoff_rows.hpp"
#include "hash_util.hpp"
#include "inou/lefdef/index_union_find.hpp"
#include "inou/lefdef/routing_geometry.hpp"
#include "macro_edge.hpp"
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
#include "placement_hints.hpp"
#include "placement_technology.hpp"
#include "planning_selection.hpp"
#include "power_grid.hpp"
#include "power_grid_rules.hpp"
#include "provisional_cell.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "repair.hpp"
#include "pass/pcrs/pcrs.hpp"
#include "reset_tree.hpp"
#include "resize_engine.hpp"
#include "routing_tracks.hpp"
#include "signal_router.hpp"
#include "pass/timing/physical_rank.hpp"
#include "steiner_net.hpp"
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

// The movable route-driven flow legalizes CORE cells in local SAT. Intermediate
// timing/repair stages must retain the GP pose until that measured handoff.
// Macro placement and every other full-placement mode keep their own policy.
bool deferred_legroute_cells(const Options& o) {
  return o.full && o.optimization_pending && o.legroute && o.legroute_plan.enabled && !o.legroute_options.legal_boxes
         && o.completed_stage == Placement_stage::final && !o.fp_place && !o.timing_seed && !o.retain_placement
         && !o.analyze_colors_only;
}
void check_fixed_and_macro_legality(const ph::Technology& tech, const ph::Floorplan& fp, const Options& options) {
  auto frame = fp;
  std::erase_if(frame.components, [&](const auto& cell) {
    return tech.macros.at(cell.macro).klass.starts_with("CORE") && cell.place.status != Status::FIXED
           && cell.place.status != Status::LOCKED;
  });
  check_legality(tech, frame, options);
}

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
// Macro channel in DBU: explicit microns round up to the manufacturing grid only, never to sites or rows.
int64_t channel_grid(const ph::Technology& tech) {
  return std::max<int64_t>(1, ph::manufacturing_grid_dbu(tech, tech.dbu).value_or(1));
}
int64_t channel_dbu(double microns, const ph::Technology& tech) {
  const auto   grid  = channel_grid(tech);
  const double value = std::ceil(microns * tech.dbu / grid - 1e-9) * grid;
  if (!std::isfinite(microns) || microns < 0 || !std::isfinite(value)
      || value > static_cast<double>(std::numeric_limits<int64_t>::max() / 4)) {
    throw std::invalid_argument("invalid macro channel");
  }
  return static_cast<int64_t>(value);
}
// A layer's minimum metal spacing: plain SPACING, or the narrowest/shortest SPACINGTABLE entry.
double layer_min_spacing(const ph::Layer& layer) {
  double best = std::numeric_limits<double>::infinity();
  for (const auto& rule : layer.rules.spacing) {
    if (!rule.range && !rule.eol && rule.distance > 0) {
      best = std::min(best, rule.distance);
    }
  }
  for (const auto& table : layer.rules.parallel) {
    if (!table.spacing.empty() && !table.spacing.front().empty() && table.spacing.front().front() > 0) {
      best = std::min(best, table.spacing.front().front());
    }
  }
  return std::isfinite(best) ? best : 0;
}
// The power-grid strategy macro.pdn follows: the named native power-grid profile, else the one the LEF matches;
// nullptr when macro.pdn is off or no registered strategy applies.
const ph::Pg_strategy* macro_pdn_strategy(const ph::Technology& tech, const Options& options) {
  if (options.macro_pdn != "auto" && options.macro_pdn != "align" && options.macro_pdn != "gap" && options.macro_pdn != "off") {
    throw std::invalid_argument("macro.pdn must be auto, align, gap or off: " + options.macro_pdn);
  }
  if (options.macro_pdn == "off") {
    return nullptr;
  }
  return pg_macro_strategy(tech, options.pg);
}
// The level macro.pdn requests: auto starts at gap (and may degrade, see View::pdn_ladder); gap/align are strict.
Pg_macro_level requested_pdn_level(const Options& options) {
  return options.macro_pdn == "off"     ? Pg_macro_level::off
         : options.macro_pdn == "align" ? Pg_macro_level::align
                                        : Pg_macro_level::gap;
}
template <class Masters>
std::optional<Pg_macro_rules> macro_pdn_rules(const ph::Technology& tech, const Masters& masters, const Options& options) {
  const auto* pg = macro_pdn_strategy(tech, options);
  if (!pg) {
    return std::nullopt;
  }
  return pg_macro_rules(tech, *pg, std::set<std::string>(masters.begin(), masters.end()));
}
// The metal-spacing channel {x, y} in DBU: explicit macro.channel, else auto keeps two macro obstructions DRC-clean:
// the largest minimum spacing over the routing layers the macros obstruct (one isotropic value), at least one
// manufacturing grid, so two macros never abut.
template <class Masters>
std::pair<int64_t, int64_t> macro_metal_channel(const ph::Technology& tech, const Masters& masters, const Options& options) {
  if (options.macro_channel) {
    return {channel_dbu(options.macro_channel->first, tech), channel_dbu(options.macro_channel->second, tech)};
  }
  double spacing = 0;
  for (const auto& name : masters) {
    const auto& macro = tech.macros.at(name);
    if (!ph::hard_macro(macro)) {
      continue;
    }
    for (const auto& layer : macro.blocked_layers) {
      if (const auto found = tech.layers.find(layer); found != tech.layers.end()) {
        spacing = std::max(spacing, layer_min_spacing(found->second));
      }
    }
  }
  const auto value = std::max(channel_grid(tech), channel_dbu(spacing, tech));
  return {value, value};
}
// Macro channel {x, y} in DBU at a macro.pdn level: the metal-spacing channel, raised per axis to the power-grid
// requirement of that level (off: the metal-spacing channel alone).
std::pair<int64_t, int64_t> macro_channel_at(const ph::Technology& tech, std::pair<int64_t, int64_t> metal,
                                             const std::optional<Pg_macro_rules>& rules, Pg_macro_level level) {
  if (rules && level != Pg_macro_level::off) {
    metal.first  = std::max(metal.first, channel_dbu(pg_macro_requirement(rules->gap[0], level), tech));
    metal.second = std::max(metal.second, channel_dbu(pg_macro_requirement(rules->gap[1], level), tech));
  }
  return metal;
}
// Resolved macro channel {x, y} in DBU for the given masters at the requested level (README "Power-grid aware macro
// spacing").
template <class Masters>
std::pair<int64_t, int64_t> macro_channel(const ph::Technology& tech, const Masters& masters, const Options& options) {
  return macro_channel_at(tech,
                          macro_metal_channel(tech, masters, options),
                          macro_pdn_rules(tech, masters, options),
                          requested_pdn_level(options));
}
// The channel a legality check requires. Explicit macro.pdn=gap|align is a hard rule. auto is a placement target that
// degrades when the macros cannot be legalized with it, so only the metal-spacing channel is mandatory.
template <class Masters>
std::pair<int64_t, int64_t> macro_legality_channel(const ph::Technology& tech, const Masters& masters, const Options& options) {
  return options.macro_pdn == "auto" ? macro_metal_channel(tech, masters, options) : macro_channel(tech, masters, options);
}
std::set<std::string> component_masters(const ph::Floorplan& fp) {
  std::set<std::string> masters;
  for (const auto& c : fp.components) {
    masters.insert(c.macro);
  }
  return masters;
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
    if (ph::hard_macro(tech.macros.at(c.macro))) {
      macros.components.push_back(c);
    }
  }
  for (const auto& c : macros.components) {
    if (c.place.status != Status::FIXED && c.place.status != Status::LOCKED) {
      throw std::invalid_argument("cell halo requires a fixed macro frame: " + c.name);
    }
  }
  const auto [cx, cy] = macro_legality_channel(tech, component_masters(macros), options);
  check_legality(tech, macros, options.halo_x, options.halo_y, double(cx) / tech.dbu, double(cy) / tech.dbu);
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
// The power grid's metal as legroute obstacles: the grid depends on rows, macros and taps, not on cell positions,
// so building it on the legalization frame gives the exact rails, stripes and via-stack landings of the handoff grid.
// Taps, the native grid and handoff IO layers are power-grid policy (nangate45
// provides only that); SAT route vias also need the routing policy.
static const ph::Tech_profile& placement_tech_profile(const ph::Technology& tech, const Options& options) {
  const bool pg = options.pg != "none" && options.pg != "off";
  return ph::resolve_tech_profile(tech,
                                  pg ? std::string_view(options.pg) : std::string_view("auto"),
                                  options.satlegalize_route_vias ? ph::Tech_policy::route : ph::Tech_policy::power_grid);
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
  h = hash_text(o.macro_channel ? std::format("macro-channel:{}:{}", o.macro_channel->first, o.macro_channel->second)
                                : std::string("macro-channel:auto-min-spacing"),
                h);
  if (o.macro_pdn != "off") {
    h = hash_text(std::format("macro-pdn-v1:{}:{}",
                              o.macro_pdn,
                              o.macro_pdn_origin ? std::format("{}:{}", o.macro_pdn_origin->first, o.macro_pdn_origin->second)
                                                 : std::string("core")),
                  h);
  }
  if (o.macro_edge_snap) {
    h = hash_text(std::format("macro-edge-snap:{}:{}", o.macro_edge_snap->first, o.macro_edge_snap->second), h);
  }
  if (o.macro_refine_rounds) {
    h = hash_text(std::format("macro-refine:{}:{}", o.macro_refine_rounds, o.macro_refine_iterations), h);
  }
  if (o.macro_dead_space && o.macro_dead_space->collapse) {
    const auto& d = *o.macro_dead_space;
    h             = hash_text(std::format("macro-dead-space:{}:{}:{}:{}:{}:{}:{}",
                                          d.narrow_um,
                                          d.pocket_fraction,
                                          d.island_cells,
                                          d.hpwl_total,
                                          d.hpwl_move,
                                          d.max_moves,
                                          d.max_cells_axis),
                              h);
  }
  if (o.macro_anchor.enabled) {
    const auto& a = o.macro_anchor;
    std::string milestones;
    for (double m : a.milestones) {
      milestones += std::format("{},", m);
    }
    h = hash_text(std::format("macro-anchor-v2:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}",
                              a.weight,
                              a.start_overflow,
                              milestones,
                              a.refresh_interval,
                              a.ramp,
                              a.gate,
                              a.stable_fraction,
                              a.stable_distance,
                              a.max_refreshes,
                              a.settle,
                              a.swaps,
                              a.swap_tolerance,
                              a.swap_neighbors,
                              a.swap_lock,
                              tech.dbu),
                  h);
    h = hash_text(std::format("macro-anchor-inflate:{}:{}:{}:{}", a.inflate, a.final_weight, a.swap_jump, a.dead_space_refreshes),
                  h);
    if (a.select != "all" || a.spring_length > 0 || a.target_hysteresis > 0 || a.final_targets || !a.edge_chain) {
      h = hash_text(std::format("macro-anchor-select:{}:{}:{}:{}:{}:{}:{}:{}:{}",
                                a.select,
                                a.edge_chain,
                                a.final_targets,
                                a.edge_weight,
                                a.random_weight,
                                a.random_fraction,
                                a.edge_gate,
                                a.spring_length,
                                a.target_hysteresis),
                    h);
    }
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
  if (o.timing_max_weight != 2 || o.timing_min_weight != .5 || o.timing_smoothing != .5) {
    h = hash_text(std::format("timing-rank-weights:{}:{}:{}", o.timing_min_weight, o.timing_max_weight, o.timing_smoothing), h);
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
  if (o.optimization_multirow_eco) {
    h = hash_text(std::format("optimization-multirow-eco-v1:{}:{}",
                              o.optimization_multirow_eco->shift_sites,
                              o.optimization_multirow_eco->max_displaced),
                  h);
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
    // The native default patience selects which post-density iterate survives.
    h = hash_text(std::format("gp-post-density:{}:{}:{}",
                              o.post_density_factor,
                              o.convergence_tolerance,
                              dp::Options{}.post_density_patience),
                  h);
  }
  if (o.boundarypins.enabled) {
    h = hash_text(std::format("boundarypins:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}",
                              o.boundarypins.split,
                              o.boundarypins.order,
                              o.boundarypins.align,
                              o.boundarypins.iterations,
                              o.boundarypins.max_displacement_um,
                              o.boundarypins.settle_iterations,
                              o.boundarypins.pitch_um,
                              o.region.merge_um,
                              o.region.max_cells,
                              o.region.cut),
                  h);
  }
  if (o.boundarypins.enabled && (o.boundarypins.clean_cuts || o.boundarypins.align_near > 0)) {
    h = hash_text(std::format("boundarypins-cuts:{}:{}", o.boundarypins.clean_cuts, o.boundarypins.align_near), h);
  }
  if (o.legroute) {
    const auto& l = o.legroute_options;
    h             = hash_text(std::format("legroute-global-keep:{}", l.global_keep), h);
    h             = hash_text(std::format("legroute-global-net-pops:{}", l.global_net_pops), h);
    h             = hash_text(std::format("legroute-global-astar-weight:{}", l.global_astar_weight), h);
    h             = hash_text(std::format("legroute-global-pattern:{}", l.global_pattern), h);
    h             = hash_text(std::format("legroute-box-route-seed:{}", l.box_route_seed), h);
    h             = hash_text(std::format("legroute-fit:{}:{}:{}", l.fit_density, l.fit_window, l.fit_first), h);
    h             = hash_text(std::format("legroute-ceiling:{}", l.density_ceiling), h);
    h             = hash_text(std::format("legroute-box-reserve:{}", l.box_reserve), h);
    h             = hash_text(std::format("critical-cellboxes:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}",
                                          l.critical_cellboxes,
                                          l.cellbox_sites,
                                          l.cellbox_rows,
                                          l.cellbox_external_pins,
                                          l.cellbox_connections,
                                          l.cellbox_critical_choices,
                                          l.cellbox_other_choices,
                                          l.cellbox_wide_exceptions,
                                          l.cellbox_far_fallback,
                                          l.cellbox_full_flow,
                                          l.cellbox_diagnostic_final),
                              h);
    h             = hash_text(std::format("legroute-reservations:{}:{}", l.reserved_tracks_per_um, l.reserved_layers), h);
    h             = hash_text(std::format("legroute-partition:{}:{}:{}:{}:{}:{}",
                                          l.partition_cells,
                                          l.partition_critical,
                                          l.partition_critical_cost,
                                          l.partition_mesh,
                                          l.global_all,
                                          l.partition_timeout_rounds),
                              h);
    h = hash_text(std::format("legroute-v14:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:"
                              "{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{"
                              "}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}",
                              l.box_fill,
                              l.box_net_max,
                              l.merge_cost,
                              l.crit_fraction,
                              l.crit_window_ps,
                              l.reserved_tracks,
                              l.clock_spine,
                              l.rib_tiles,
                              l.final,
                              l.route_passes,
                              l.cap_scale,
                              l.window_tiles,
                              l.crit_boxes,
                              l.tree_boxes,
                              l.tree_moves,
                              l.sat_relax_rounds,
                              l.inbox_span,
                              l.tree_load,
                              l.tree_clauses,
                              l.tree_by_crit,
                              l.io_preroute,
                              l.sat_alt,
                              l.tree_congestion,
                              l.steiner_pins,
                              l.capacity_lazy,
                              l.freeze,
                              l.legal_boxes,
                              l.legal_window,
                              l.legal_shuffle,
                              l.steiner_min,
                              l.hop_share,
                              l.reserve_share,
                              l.global_fraction,
                              l.global_route,
                              l.global_share,
                              l.global_own_share,
                              l.hop_choice,
                              l.spill_rounds,
                              l.spill_fraction,
                              l.window_grow,
                              l.spill_shift,
                              l.mesh_join,
                              l.alternatives,
                              l.alt_ms,
                              l.region_pins,
                              l.alt_all,
                              l.checker_window,
                              l.global_margin,
                              l.global_rounds,
                              l.hop_spread,
                              l.sat_seed,
                              l.tiers,
                              l.tier_rounds,
                              l.tier_fraction,
                              l.tier_batch),
                  h);
    h = hash_text(std::format("legroute-budgets:{}:{}:{}:{}:{}:{}:{}:{}:{}",
                              l.sat_conflicts,
                              l.sat_ms,
                              l.tier_ms,
                              l.sat_min_fill,
                              l.route_access,
                              l.route_patterns,
                              l.route_ms,
                              l.threads,
                              o.allow_increase_area),
                  h);
    if (!o.region_seed_dir.empty()) {  // the virtual pins legroute takes from the region refinement
      for (const char* name : {"/boundarypins/pins.tsv", "/region_mesh.json"}) {
        std::ifstream file(o.region_seed_dir + name, std::ios::binary);
        std::string   text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        h = hash_text("region-seed:" + text, h);
      }
    }
    if (o.legroute_probe.enabled) {
      const auto& q = o.legroute_probe;
      h             = hash_text(std::format("legroute-probe:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}",
                                            q.cells,
                                            q.sat_ms,
                                            q.iterations,
                                            q.fill,
                                            q.target,
                                            q.max_inflate,
                                            l.sat_min_fill,
                                            q.tree_congestion,
                                            q.rounds,
                                            q.real,
                                            q.repulsion,
                                            q.shuffle),
                                h);
    }
    if (o.legroute_plan.enabled) {
      const auto& p = o.legroute_plan;
      h             = hash_text(std::format("legroute-plan:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}",
                                            p.rounds,
                                            p.iterations,
                                            p.net_pops,
                                            p.max_inflate,
                                            p.local_tiles,
                                            p.handoff,
                                            p.min_iterations,
                                            p.timing_interval,
                                            p.max_degradation_ps,
                                            p.route_model,
                                            p.route_interval,
                                            p.route_net_pops,
                                            p.export_coarse),
                                h);
    }
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
  h = hash_text(std::format("uroute:{}:{}:{}:{}:{}:{}:{}",
                            o.uroute.dir,
                            o.uroute.export_features,
                            o.uroute.shots,
                            o.uroute.min_shots,
                            o.uroute.fallback,
                            o.uroute.drc_level,
                            o.uroute.label_policy),
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
  Name        instance, pin;
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
    auto n     = p.get_master_node();
    auto input = [&](std::string_view name, int which) {
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
  Name        name, macro, block;
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
      // With pass.pcrs a reset is ordinary data behind its own pipelined tree: only clock roles stay controls.
      if (roles.reset_is_data && (gu::signal_roles_of(pin) & attrs::signal_reset) && !(gu::signal_roles_of(pin) & attrs::signal_clock)) {
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
  int64_t                                       channel_x = 0, channel_y = 0;  // macro-to-macro clear spacing, DBU
  // macro.pdn: the strategy's macro rules (unset: off or no strategy), the strap origin override (DBU), whether
  // edges snap onto the strap lattice, and how often the alignment had to fall back to routing tracks alone.
  std::optional<Pg_macro_rules>                 pdn_rules;
  std::optional<std::array<int64_t, 2>>         pdn_origin;
  bool                                          pdn_align      = false;
  mutable int64_t                               pdn_alignments = 0, pdn_fallbacks = 0;  // diagnostics only
  // macro.pdn levels this placement may use, strongest first (auto: gap, align, off with equal channels merged;
  // gap/align/off: that level alone), the one in use (channel_x/y, pdn_align follow it), whether it was chosen
  // (make_arrays picks it once the core is known) and every level tried, for QoR.
  struct Pdn_level {
    Pg_macro_level level     = Pg_macro_level::off;
    int64_t        channel_x = 0, channel_y = 0;
  };
  struct Pdn_attempt {
    Pg_macro_level level     = Pg_macro_level::off;
    int64_t        channel_x = 0, channel_y = 0;
    std::string    result;  // infeasible | legalization_failed | used | frame
  };
  std::vector<Pdn_level>                        pdn_ladder;
  size_t                                        pdn_level    = 0;
  bool                                          pdn_selected = false;
  std::vector<Pdn_attempt>                      pdn_attempts;
  // macro.edge_snap: resolved sliver limits (DBU, 0 = off) and what the snap did over the stage.
  int64_t                                       edge_snap_x = 0, edge_snap_y = 0;
  // Native padding of a movable hard macro per side while macros are planned: the halo, or half the macro channel
  // rounded up to sites/rows when that is larger, so the native macro legalizer already keeps the channel.
  int64_t                                       plan_pad_x = 0, plan_pad_y = 0;
  int64_t                                       edge_snap_moved = 0, edge_snap_moves = 0, edge_snap_distance = 0;
  // macro.dead_space collapse over the stage: the first measurement, the last one, and the moves in between.
  std::optional<Dead_space_collapse_report>     dead_space_collapse;
  // macro.refine_rounds: one record per GP burst + macro re-legalization round.
  struct Refine_round {
    int     iterations = 0;
    double  hpwl = 0, overflow = 0, density_weight = 0;
    int64_t dead_area = -1, macro_displacement = 0, moved_macros = 0;
    bool    rejected = false;  // the round lost wirelength and was undone
  };
  std::vector<Refine_round>                 refine_rounds;
  std::map<Endpoint, Bus_bit>               bus_names;
  ph::Floorplan                             fp;
  // Solver rows span a rectangle; exports retain the imported row segments.
  std::shared_ptr<const ph::Floorplan>      original_row_frame;
  std::vector<std::string>                  io_buses;
  std::vector<int>                          io_bits;
  std::vector<Cell>                         cells;
  size_t                                    terminal_begin = 0;  // after cells and placement-only mask geometry
  std::set<std::string>                     constant_masters;
  std::vector<Endpoint>                     pins;
  std::vector<Endpoint>                     drivers;
  std::map<Endpoint, std::vector<Endpoint>> nets;
  std::map<Name, size_t>                    terminal;
  std::map<Name, size_t>                    cell;
  std::map<std::string, double>             weights;
  std::map<Name, Name>                      cohesion_owners;
};
// DREAMPlace's macro legalizer reports "DREAMPlace macro legalization failed" when no legal macro pose was found.
bool macro_legalization_failure(const std::exception& error) {
  return std::string_view(error.what()).find("macro legalization failed") != std::string_view::npos;
}
// An explicit macro.pdn=gap|align is a hard requirement: say which channel could not be met and what degrades.
std::string pdn_legalization_error(const View& v, const ph::Technology& tech, const Options& options, const std::exception& error) {
  const auto& level = v.pdn_ladder.at(v.pdn_level);
  return std::format(
      "{}: the macros cannot be legalized with the macro.pdn={} channel {:.2f} x {:.2f} um (power-grid spacing). "
      "macro.pdn=auto degrades to the aligned and then the metal-spacing channel; macro.pdn=off drops the power-grid "
      "spacing; or enlarge the core / lower the macro count",
      error.what(),
      options.macro_pdn,
      double(level.channel_x) / tech.dbu,
      double(level.channel_y) / tech.dbu);
}
// Switch the placement to macro.pdn ladder entry `index`: the channel and the lattice snap follow it.
void set_pdn_level(View& v, size_t index) {
  const auto& level = v.pdn_ladder.at(index);
  v.pdn_level       = index;
  v.channel_x       = level.channel_x;
  v.channel_y       = level.channel_y;
  v.pdn_align       = v.pdn_rules && level.level == Pg_macro_level::align;
}
// The planning pad on the solver's own site/row lattice (the native solver rounds halos up to it), rounded down so
// the pad never exceeds the channel: a channel below one site/row per side adds no native padding, and the track
// alignment opens the remainder.
std::pair<int64_t, int64_t> plan_pad(const View& v, int64_t channel_x, int64_t channel_y) {
  const auto sx = std::max<int64_t>(1, std::llround(v.design.layout.site_width));
  const auto sy = std::max<int64_t>(1, std::llround(v.design.layout.row_height));
  return {std::max(v.halo_x, (channel_x + 1) / 2 / sx * sx), std::max(v.halo_y, (channel_y + 1) / 2 / sy * sy)};
}
bool planned_hard_macro(const Cell& c, const ph::Technology& tech) {
  return !c.locked && c.place.status != Status::FIXED && c.place.status != Status::LOCKED && ph::hard_macro(tech.macros.at(c.macro))
         && ph::placement_mask({c.name, c.macro, c.place}, tech) == ph::Placement_mask::none;
}
// macro.pdn=auto picks its starting level once the solver core is known (v.design.layout):
//  * with movable hard macros, the strongest level whose padded macros can fit the core at all
//    (pg_macro_boxes_fit on the native legalizer's footprints: the planning pad, rounded up to sites/rows); a level
//    that cannot fit is recorded as infeasible and never attempted. Macro legalization may still degrade further.
//  * with every hard macro fixed (a staged final pass, a later timing/repair pass on a planned frame), the strongest
//    level whose channel the frame keeps, so the stages after a degraded macro plan agree with it.
void select_pdn_level(View& v, const ph::Technology& tech) {
  if (v.pdn_selected) {
    return;
  }
  v.pdn_selected = true;
  if (v.pdn_ladder.size() < 2) {
    return;
  }
  const auto& l      = v.design.layout;
  const auto  core_w = std::llround(l.xh - l.xl), core_h = std::llround(l.yh - l.yl);
  const auto  sx = std::max<int64_t>(1, std::llround(l.site_width)), sy = std::max<int64_t>(1, std::llround(l.row_height));
  std::vector<const Cell*> movable, fixed;
  for (const auto& c : v.cells) {
    if (!ph::hard_macro(tech.macros.at(c.macro))
        || ph::placement_mask({c.name, c.macro, c.place}, tech) != ph::Placement_mask::none) {
      continue;
    }
    (planned_hard_macro(c, tech) ? movable : fixed).push_back(&c);
  }
  size_t chosen = v.pdn_ladder.size() - 1;
  if (!movable.empty()) {
    for (size_t i = 0; i < v.pdn_ladder.size(); ++i) {
      const auto [px, py] = plan_pad(v, v.pdn_ladder[i].channel_x, v.pdn_ladder[i].channel_y);
      std::vector<Pg_macro_box> boxes;
      for (const auto* c : movable) {
        boxes.push_back({(c->place.width + 2 * px + sx - 1) / sx * sx, (c->place.height + 2 * py + sy - 1) / sy * sy});
      }
      for (const auto* c : fixed) {  // fixed bodies (and their halo) clipped to the core
        const auto xl = std::max<int64_t>(c->place.x - v.halo_x, std::llround(l.xl));
        const auto yl = std::max<int64_t>(c->place.y - v.halo_y, std::llround(l.yl));
        const auto xh = std::min<int64_t>(c->place.x + c->place.width + v.halo_x, std::llround(l.xh));
        const auto yh = std::min<int64_t>(c->place.y + c->place.height + v.halo_y, std::llround(l.yh));
        if (xh > xl && yh > yl) {
          boxes.push_back({xh - xl, yh - yl});
        }
      }
      if (i + 1 == v.pdn_ladder.size() || pg_macro_boxes_fit(boxes, core_w, core_h)) {
        chosen = i;
        break;
      }
      v.pdn_attempts.push_back({v.pdn_ladder[i].level, v.pdn_ladder[i].channel_x, v.pdn_ladder[i].channel_y, "infeasible"});
    }
  } else {
    std::vector<const Cell*> sorted = fixed;
    std::sort(sorted.begin(), sorted.end(), [](auto a, auto b) {
      return std::pair{a->place.x, a->name} < std::pair{b->place.x, b->name};
    });
    const auto keeps = [&](int64_t cx, int64_t cy) {
      for (size_t i = 0; i < sorted.size(); ++i) {
        const auto& a = sorted[i]->place;
        for (size_t j = i + 1; j < sorted.size() && sorted[j]->place.x < a.x + a.width + cx; ++j) {
          const auto& b = sorted[j]->place;
          if (b.y < a.y + a.height + cy && a.y < b.y + b.height + cy) {
            return false;
          }
        }
      }
      return true;
    };
    for (size_t i = 0; i < v.pdn_ladder.size(); ++i) {
      if (keeps(v.pdn_ladder[i].channel_x, v.pdn_ladder[i].channel_y)) {
        chosen = i;
        break;
      }
    }
    v.pdn_attempts.push_back({v.pdn_ladder[chosen].level, v.pdn_ladder[chosen].channel_x, v.pdn_ladder[chosen].channel_y, "frame"});
  }
  set_pdn_level(v, chosen);
}
// A timing checkpoint borrows connectivity and solver geometry. Ordinary GP
// retains fractional pin positions. Legal checkpoints round positions and apply
// row/macro orientations without copying topology or rebuilding native arrays.
// None of these references escape the synchronous checkpoint.
// macro.pdn alignment lattice for the current frame: strap origin at the row core's lower-left corner (or the
// macro.pdn_origin override), windows for the resolved macro channel.
std::optional<Pg_macro_alignment> macro_pdn_alignment(const View& v, const ph::Technology& tech) {
  if (!v.pdn_align || v.fp.rows.empty()) {
    return std::nullopt;
  }
  std::array<int64_t, 2> origin{std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::max()};
  for (const auto& row : v.fp.rows) {
    origin[0] = std::min(origin[0], row.x);
    origin[1] = std::min(origin[1], row.y);
  }
  if (v.pdn_origin) {
    origin = *v.pdn_origin;
  }
  // Two manufacturing grids of tolerance: a downstream flow re-snapping macro origins stays on the lattice.
  auto alignment = pg_macro_alignment(*v.pdn_rules, {v.channel_x, v.channel_y}, origin, tech.dbu, 2 * channel_grid(tech));
  if (!alignment.active()) {
    return std::nullopt;
  }
  return alignment;
}
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
// Full native cellbox flow preserves complete imported connectivity, including
// intrinsic single-pin outputs absent from the placement graph's edge view.
void restore_critical_source_nets(ph::Floorplan& frame, const ph::Floorplan& source) {
  using Endpoints      = std::vector<std::pair<std::string, std::string>>;
  const auto endpoints = [](const ph::Net& n) {
    Endpoints out;
    for (const auto& p : n.connections) {
      out.emplace_back(p.instance, p.pin);
    }
    std::ranges::sort(out);
    return out;
  };
  std::map<Endpoints, size_t> source_ids;
  for (size_t n = 0; n < source.nets.size(); ++n) {
    if (source.nets[n].connections.empty()) {
      continue;
    }
    if (!source_ids.emplace(endpoints(source.nets[n]), n).second) {
      throw std::invalid_argument("complete source has duplicate endpoint partitions");
    }
  }
  auto              nets = source.nets;
  std::vector<bool> found(nets.size(), false);
  for (auto& n : nets) {
    n.wires.clear();
  }
  for (const auto& n : frame.nets) {
    if (n.connections.empty()) {
      continue;
    }
    auto it = source_ids.find(endpoints(n));
    if (it == source_ids.end() || found[it->second]) {
      throw std::invalid_argument("full cellbox source connectivity changed before handoff");
    }
    found[it->second]      = true;
    nets[it->second].wires = n.wires;
  }
  for (size_t n = 0; n < nets.size(); ++n) {
    if (!found[n] && nets[n].connections.size() > 1) {
      throw std::invalid_argument("full cellbox source lost a multi-pin net");
    }
  }
  std::map<std::string, std::string> terminal_net;
  for (const auto& n : nets) {
    for (const auto& pin : n.connections) {
      if (pin.instance == "PIN") {
        if (!terminal_net.emplace(pin.pin, n.name).second) {
          throw std::invalid_argument("full cellbox source port belongs to multiple nets");
        }
      }
    }
  }
  for (auto& t : frame.terminals) {
    if (auto it = terminal_net.find(t.name); it != terminal_net.end()) {
      t.net = it->second;
    }
  }
  frame.nets = std::move(nets);
}

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
  if ((it == macro.pins.end() || it->second.shapes.empty()) && ph::soft_block(macro)) {
    // A soft block pin without LEF geometry connects at the block center.
    const auto [x, y] = ph::oriented_center(macro, {0, 0, macro.width, macro.height}, o);
    return {x * dbu, y * dbu};
  }
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
  frame         = v.fp;
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
  select_pdn_level(v, tech);
  std::tie(v.plan_pad_x, v.plan_pad_y) = plan_pad(v, v.channel_x, v.channel_y);
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
    // A soft block (LEF CLASS BLOCK SOFT) is a movable area of cells: not a
    // native macro (no macro legalization, halo or rigid/alignment terms) and
    // not inflatable; its pins and full area enter wirelength and density.
    const bool soft     = ph::soft_block(tech.macros.at(c.macro));
    const auto [blocked_h, blocked_v]
        = standard ? std::pair{0., 0.} : lef_track_capacity(tech, tech.macros.at(c.macro).blocked_layers);
    d.node_blocked_capacity_h.push_back(blocked_h);
    d.node_blocked_capacity_v.push_back(blocked_v);
    const auto mask = ph::placement_mask({c.name, c.macro, c.place}, tech);
    d.node_placement_mask.push_back(mask == ph::Placement_mask::none   ? dp::PlacementMask::none
                                    : mask == ph::Placement_mask::soft ? dp::PlacementMask::soft
                                                                       : dp::PlacementMask::hard);
    d.node_macro.push_back(!standard && !soft);
    d.node_inflatable.push_back(standard);
    // Movable hard macros take the planning pad (halo or half the channel); fixed ones keep only the halo,
    // so standard cells may use the channel once the macro frame is fixed.
    const bool planned = !c.locked && c.place.status != Status::FIXED && c.place.status != Status::LOCKED;
    d.node_halo_x.push_back(standard || soft || mask != ph::Placement_mask::none ? 0
                            : planned                                            ? std::max(v.halo_x, v.plan_pad_x)
                                                                                 : v.halo_x);
    d.node_halo_y.push_back(standard || soft || mask != ph::Placement_mask::none ? 0
                            : planned                                            ? std::max(v.halo_y, v.plan_pad_y)
                                                                                 : v.halo_y);
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
    if ((c.place.status == Status::FIXED || c.place.status == Status::LOCKED) && d.placement_mask(i) != dp::PlacementMask::soft
        && !ph::soft_block(tech.macros.at(c.macro))) {
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
            = classes.try_emplace({c.macro, v.global_macro_alignment ? std::string{} : owner->second.str()}, classes.size());
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
    if (!std::isfinite(weight) || weight <= 0 || weight > max_net_weight) {
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
      if (gu::Color_group::has(v.cells[i].node)) {
        auto key               = std::string(gu::Color_group::text(v.cells[i].node));
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
    const auto master = io ? std::string(io->get_name()) : old->second.macro.str();
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
      if (gu::Placement_block::has(node)) {
        cell.block = gu::Placement_block::text(node);
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
    if (gu::Color_group::has(node)) {
      v.cohesion_owners.emplace(name, gu::Color_group::text(node));
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
// Macro tile snap (todo 2l-legroute L1, ruling Q3): a movable macro's halo frame starts on a corner of the
// legroute tile grid (2 x the widest flop, two rows) so the tiles around it stay whole; the track alignment that
// follows moves it by less than a pitch. Fixed macros keep their custom location, and a snap that leaves the
// rows or overlaps another macro's halo frame is skipped.
static void snap_macros_to_tiles(const View& v, const ph::Technology& tech, std::vector<ph::Component>& frame) {
  if (v.fp.rows.empty()) {
    return;
  }
  int64_t flop = 0;
  for (const auto& c : v.cells) {
    const auto& m = tech.macros.at(c.macro);
    if (m.klass.starts_with("CORE")
        && std::any_of(m.pins.begin(), m.pins.end(), [](const auto& p) { return p.second.use == "CLOCK"; })) {
      flop = std::max<int64_t>(flop, std::llround(m.width * v.fp.dbu));
    }
  }
  const auto site = tech.sites.find(v.fp.rows.front().site);
  if (flop <= 0 || site == tech.sites.end()) {
    return;
  }
  Legroute_tile tile;
  const auto    step = std::llround(site->second.width * v.fp.dbu);
  tile.w             = (2 * flop + step - 1) / step * step;
  tile.h             = 2 * std::llround(site->second.height * v.fp.dbu);
  tile.x0 = tile.y0 = std::numeric_limits<int64_t>::max();
  int64_t xh = std::numeric_limits<int64_t>::min(), yh = xh;
  for (const auto& r : v.fp.rows) {
    tile.x0 = std::min(tile.x0, r.x);
    tile.y0 = std::min(tile.y0, r.y);
    xh      = std::max(xh, r.x + r.count * r.step);
    yh      = std::max(yh, r.y + tile.h / 2);
  }
  const auto frame_of = [&](const ph::Component& c) {
    return ph::Rect{double(c.place.x - v.halo_x),
                    double(c.place.y - v.halo_y),
                    double(c.place.x + c.place.width + v.halo_x),
                    double(c.place.y + c.place.height + v.halo_y)};
  };
  for (auto& c : frame) {
    if (c.place.status != Status::PLACED || tech.macros.at(c.macro).klass.starts_with("CORE")) {
      continue;
    }
    const auto [x, y] = snap_to_tiles(tile, c.place.x, c.place.y, v.halo_x, v.halo_y);
    auto moved        = c;
    moved.place.x     = x;
    moved.place.y     = y;
    const auto f      = frame_of(moved);
    bool       ok     = f.xl >= tile.x0 - v.halo_x && f.yl >= tile.y0 - v.halo_y && f.xh <= xh + v.halo_x && f.yh <= yh + v.halo_y;
    for (const auto& o : frame) {
      if (!ok) {
        break;
      }
      if (&o != &c && !tech.macros.at(o.macro).klass.starts_with("CORE")) {
        // Two macro bodies keep max(channel, both halos); snapping never re-creates touching macros.
        const auto  sx = std::max(v.channel_x, 2 * v.halo_x), sy = std::max(v.channel_y, 2 * v.halo_y);
        const auto &a = moved.place, &b = o.place;
        ok = a.x + a.width + sx <= b.x || b.x + b.width + sx <= a.x || a.y + a.height + sy <= b.y || b.y + b.height + sy <= a.y;
      }
    }
    if (ok) {
      c.place.x = x;
      c.place.y = y;
    }
  }
}

// macro.edge_snap after native macro legalization: movable, ungrouped hard
// macros slide toward their nearest core edge across slivers of at most
// v.edge_snap_x/y. Fixed macros and fixed non-CORE/non-soft geometry are
// obstacles; movable cells and soft blocks are not (cells are legalized later).
// Track alignment runs afterwards and keeps every clearance.
void snap_macros_to_core_edges(View& v, const ph::Technology& tech, dp::Design& physical) {
  const auto& l = physical.layout;
  // auto: one tenth of the core extent per axis (the notch threshold of OpenROAD's mpl).
  if (v.edge_snap_x < 0) {
    v.edge_snap_x = std::llround(l.xh - l.xl) / 10;
  }
  if (v.edge_snap_y < 0) {
    v.edge_snap_y = std::llround(l.yh - l.yl) / 10;
  }
  if ((v.edge_snap_x <= 0 && v.edge_snap_y <= 0) || physical.node_macro.empty()) {
    return;
  }
  std::vector<Edge_object> objects;
  std::vector<size_t>      indexes;
  for (size_t i = 0; i < physical.node_x.size(); ++i) {
    const bool cell = i < v.cells.size();
    if ((cell && ph::soft_block(tech.macros.at(v.cells[i].macro))) || physical.placement_mask(i) == dp::PlacementMask::soft) {
      continue;
    }
    const bool macro   = physical.node_macro[i] && physical.placement_mask(i) == dp::PlacementMask::none;
    const bool movable = i < static_cast<size_t>(physical.num_movable);
    if (!macro && movable) {
      continue;  // movable standard cells are legalized after the macros
    }
    const auto w = std::llround(physical.node_size_x[i]), h = std::llround(physical.node_size_y[i]);
    if (w <= 0 || h <= 0) {
      continue;
    }
    const auto x = std::llround(physical.node_x[i]), y = std::llround(physical.node_y[i]);
    const bool grouped = !physical.node_group_id.empty() && physical.node_group_id[i] != 0;
    objects.push_back({x, y, x + w, y + h, macro && movable && !grouped, macro});
    indexes.push_back(i);
  }
  Edge_snap_limits limits;
  limits.core_xl    = std::llround(l.xl);
  limits.core_yl    = std::llround(l.yl);
  limits.core_xh    = std::llround(l.xh);
  limits.core_yh    = std::llround(l.yh);
  // The native legality check keeps each movable macro's planning pad inside the core and apart.
  limits.halo_x     = std::max(v.halo_x, v.plan_pad_x);
  limits.halo_y     = std::max(v.halo_y, v.plan_pad_y);
  limits.space_x    = std::max(v.channel_x, 2 * limits.halo_x);
  limits.space_y    = std::max(v.channel_y, 2 * limits.halo_y);
  limits.max_gap_x  = v.edge_snap_x;
  limits.max_gap_y  = v.edge_snap_y;
  limits.grid       = channel_grid(tech);
  const auto report = snap_macros_to_edges(objects, limits);
  for (size_t j = 0; j < objects.size(); ++j) {
    physical.node_x[indexes[j]] = objects[j].xl;
    physical.node_y[indexes[j]] = objects[j].yl;
  }
  v.edge_snap_moved    += report.moved;
  v.edge_snap_moves    += report.moves;
  v.edge_snap_distance += report.distance;
}
// macro.dead_space: the obstacles of the dead-space raster and the collapse
// pass. Soft blocks and movable standard cells are not obstacles (as in the
// edge snap); hard macros are, movable or fixed, and so is fixed geometry.
std::vector<Dead_space_object> dead_space_objects(const View& v, const ph::Technology& tech, const dp::Design& physical,
                                                  std::vector<size_t>& indexes) {
  std::vector<Dead_space_object> objects;
  indexes.clear();
  for (size_t i = 0; i < physical.node_x.size(); ++i) {
    const bool cell = i < v.cells.size();
    if ((cell && ph::soft_block(tech.macros.at(v.cells[i].macro))) || physical.placement_mask(i) == dp::PlacementMask::soft) {
      continue;
    }
    const bool macro   = physical.node_macro[i] && physical.placement_mask(i) == dp::PlacementMask::none;
    const bool movable = i < static_cast<size_t>(physical.num_movable);
    if (!macro && movable) {
      continue;
    }
    const auto w = std::llround(physical.node_size_x[i]), h = std::llround(physical.node_size_y[i]);
    if (w <= 0 || h <= 0) {
      continue;
    }
    const auto x = std::llround(physical.node_x[i]), y = std::llround(physical.node_y[i]);
    const auto group = physical.node_group_id.empty() ? uint64_t{0} : physical.node_group_id[i];
    objects.push_back({x, y, x + w, y + h, macro && movable, macro, group});
    indexes.push_back(i);
  }
  return objects;
}
// Technology-derived thresholds: a raster pitch of whole rows (at most
// max_cells_axis cells per axis), the macro channel, the notch width (one
// tenth of the core extent per axis unless given) and the island area
// (island_cells average movable standard cells; one row height squared each
// when the design has none).
Dead_space_limits dead_space_limits(const View& v, const ph::Technology& tech, const dp::Design& physical,
                                    const Dead_space_options& o) {
  const auto&       l = physical.layout;
  Dead_space_limits d;
  d.core_xl         = std::llround(l.xl);
  d.core_yl         = std::llround(l.yl);
  d.core_xh         = std::llround(l.xh);
  d.core_yh         = std::llround(l.yh);
  const auto row    = std::max<int64_t>(1, std::llround(l.row_height));
  const auto extent = std::max(d.core_xh - d.core_xl, d.core_yh - d.core_yl);
  const auto rows   = std::max<int64_t>(1, (extent + row * o.max_cells_axis - 1) / (row * o.max_cells_axis));
  d.pitch           = rows * row;
  d.channel_x       = v.channel_x;
  d.channel_y       = v.channel_y;
  d.narrow_x        = o.narrow_um < 0 ? (d.core_xh - d.core_xl) / 10 : std::llround(o.narrow_um * tech.dbu);
  d.narrow_y        = o.narrow_um < 0 ? (d.core_yh - d.core_yl) / 10 : std::llround(o.narrow_um * tech.dbu);
  d.pocket_fraction = o.pocket_fraction;
  double  area      = 0;
  int64_t cells     = 0;
  for (int i = 0; i < physical.num_movable && i < static_cast<int>(physical.node_x.size()); ++i) {
    const bool soft = (static_cast<size_t>(i) < v.cells.size() && ph::soft_block(tech.macros.at(v.cells[i].macro)))
                      || physical.placement_mask(i) == dp::PlacementMask::soft;
    if (!physical.node_macro[i] && !soft && physical.node_size_x[i] > 0 && physical.node_size_y[i] > 0) {
      area += physical.node_size_x[i] * physical.node_size_y[i];
      ++cells;
    }
  }
  const double average = cells ? area / double(cells) : double(row) * double(row);
  d.min_area           = std::llround(o.island_cells * average);
  return d;
}
// QoR `macro_dead_space`: the metric of the current macro frame, its
// thresholds, the largest components with raster outlines, and the collapse.
rapidjson::Value dead_space_json(const View& v, const ph::Technology& tech, const dp::Design& physical, const Dead_space_options& o,
                                 rapidjson::Document::AllocatorType& allocator) {
  std::vector<size_t>          indexes;
  const auto                   objects = dead_space_objects(v, tech, physical, indexes);
  const auto                   limits  = dead_space_limits(v, tech, physical, o);
  std::vector<Dead_space_rect> obstacles;
  for (const auto& b : objects) {
    const auto hx = b.macro ? v.halo_x : 0, hy = b.macro ? v.halo_y : 0;
    obstacles.push_back({b.xl - hx, b.yl - hy, b.xh + hx, b.yh + hy});
  }
  const auto report = measure_dead_space(obstacles, limits, true);
  const auto um     = [&](int64_t d) { return double(d) / tech.dbu; };
  const auto um2    = [&](int64_t a) { return double(a) / tech.dbu / tech.dbu; };
  const auto box    = [&](const Dead_space_rect& r) {
    rapidjson::Value a(rapidjson::kArrayType);
    for (const auto c : {r.xl, r.yl, r.xh, r.yh}) {
      a.PushBack(um(c), allocator);
    }
    return a;
  };
  const auto kind = [](Dead_kind k) { return k == Dead_kind::notch ? "notch" : k == Dead_kind::pocket ? "pocket" : "island"; };
  const auto pair = [&](int64_t x, int64_t y) {
    rapidjson::Value a(rapidjson::kArrayType);
    a.PushBack(um(x), allocator);
    a.PushBack(um(y), allocator);
    return a;
  };
  rapidjson::Value out(rapidjson::kObjectType);
  out.AddMember("collapse", o.collapse, allocator);
  out.AddMember("pitch_um", um(report.pitch), allocator);
  rapidjson::Value grid(rapidjson::kArrayType);
  grid.PushBack(report.grid_x, allocator);
  grid.PushBack(report.grid_y, allocator);
  out.AddMember("grid", grid, allocator);
  out.AddMember("channel_um", pair(limits.channel_x, limits.channel_y), allocator);
  out.AddMember("narrow_um", pair(limits.narrow_x, limits.narrow_y), allocator);
  out.AddMember("pocket_fraction", limits.pocket_fraction, allocator);
  out.AddMember("island_cells", o.island_cells, allocator);
  out.AddMember("island_area_um2", um2(limits.min_area), allocator);
  out.AddMember("free_um2", um2(report.free_area), allocator);
  out.AddMember("dead_um2", um2(report.dead_area), allocator);
  out.AddMember("dead_pct", report.free_area ? 100.0 * double(report.dead_area) / double(report.free_area) : 0.0, allocator);
  out.AddMember("notch_um2", um2(report.notch_area), allocator);
  out.AddMember("pocket_um2", um2(report.pocket_area), allocator);
  out.AddMember("island_um2", um2(report.island_area), allocator);
  out.AddMember("stranded_um2", um2(report.stranded_area), allocator);
  out.AddMember("count", static_cast<uint64_t>(report.components.size()), allocator);
  rapidjson::Value components(rapidjson::kArrayType);
  size_t           rects = 0;
  for (size_t k = 0; k < report.components.size() && k < 64; ++k) {
    const auto&      c = report.components[k];
    rapidjson::Value j(rapidjson::kObjectType);
    j.AddMember("area_um2", um2(c.area), allocator);
    j.AddMember("kind", rapidjson::StringRef(kind(c.kind)), allocator);
    j.AddMember("bbox_um", box(c.bbox), allocator);
    rapidjson::Value outline(rapidjson::kArrayType);
    for (const auto& r : c.rects) {
      if (rects++ >= 4096) {
        break;
      }
      outline.PushBack(box(r), allocator);
    }
    j.AddMember("rects_um", outline, allocator);
    if (k == 0) {
      rapidjson::Value largest(rapidjson::kObjectType);
      largest.AddMember("area_um2", um2(c.area), allocator);
      largest.AddMember("kind", rapidjson::StringRef(kind(c.kind)), allocator);
      largest.AddMember("bbox_um", box(c.bbox), allocator);
      out.AddMember("largest", largest, allocator);
    }
    components.PushBack(j, allocator);
  }
  out.AddMember("components", components, allocator);
  if (v.dead_space_collapse) {
    const auto&      r = *v.dead_space_collapse;
    rapidjson::Value c(rapidjson::kObjectType);
    c.AddMember("before_dead_um2", um2(r.before.dead_area), allocator);
    c.AddMember("after_dead_um2", um2(r.after.dead_area), allocator);
    c.AddMember("before_stranded_um2", um2(r.before.stranded_area), allocator);
    c.AddMember("after_stranded_um2", um2(r.after.stranded_area), allocator);
    c.AddMember("moves", r.moves, allocator);
    c.AddMember("moved_macros", r.moved, allocator);
    c.AddMember("distance_um", um(r.distance), allocator);
    c.AddMember("hpwl_delta_um", r.wl_delta / tech.dbu, allocator);
    c.AddMember("candidates", r.candidates, allocator);
    out.AddMember("collapse_report", c, allocator);
  }
  return out;
}
// macro.dead_space collapse after the edge snap: translate movable hard macros
// (or their aligned column/row or block slice) into dead regions. Legality
// uses the same planning pad and spacing as the edge snap; the wirelength
// estimate is the HPWL of the moved macros' nets at the current (global)
// cell positions, bounded per move and in total as fractions of the initial
// HPWL of all nets touching a movable macro.
void collapse_macro_dead_space(View& v, const ph::Technology& tech, dp::Design& physical, const Dead_space_options& o,
                               int threads) {
  if (physical.node_macro.empty() || o.max_moves <= 0) {
    return;
  }
  std::vector<size_t> indexes;
  auto                objects = dead_space_objects(v, tech, physical, indexes);
  if (std::none_of(objects.begin(), objects.end(), [](const auto& b) { return b.movable; })) {
    return;
  }
  Dead_space_collapse_limits limits;
  limits.metric        = dead_space_limits(v, tech, physical, o);
  limits.metric_halo_x = v.halo_x;
  limits.metric_halo_y = v.halo_y;
  limits.halo_x        = std::max(v.halo_x, v.plan_pad_x);
  limits.halo_y        = std::max(v.halo_y, v.plan_pad_y);
  limits.space_x       = std::max(v.channel_x, 2 * limits.halo_x);
  limits.space_y       = std::max(v.channel_y, 2 * limits.halo_y);
  limits.grid          = channel_grid(tech);
  limits.max_moves     = o.max_moves;
  limits.threads         = std::max(1, threads);
  limits.min_gain      = 4 * limits.metric.pitch * limits.metric.pitch;
  double     initial     = 0;
  const auto cost        = dead_space_hpwl_cost(physical, indexes, objects, &initial);
  limits.wl_move_budget  = o.hpwl_move * initial;
  limits.wl_total_budget = o.hpwl_total * initial;
  auto report = collapse_dead_space(objects, limits, cost);
  for (size_t j = 0; j < objects.size(); ++j) {
    physical.node_x[indexes[j]] = objects[j].xl;
    physical.node_y[indexes[j]] = objects[j].yl;
  }
  if (!v.dead_space_collapse) {
    v.dead_space_collapse = std::move(report);
    return;
  }
  auto& total       = *v.dead_space_collapse;
  total.after       = std::move(report.after);
  total.moves      += report.moves;
  total.moved      += report.moved;
  total.distance   += report.distance;
  total.wl_delta   += report.wl_delta;
  total.candidates += report.candidates;
}
// Movable CORE coordinates are not obstacles until the native cell legalizer
// packs them; explicit fixed cells and all macro hard halos remain obstacles.
bool align_macro_geometry(const View& v, const ph::Technology& tech, dp::Design& physical, bool move, bool freeze_all,
                          bool tile_snap = false) {
  if (std::none_of(physical.node_macro.begin(), physical.node_macro.begin() + v.cells.size(), [](auto macro) { return macro; })) {
    return false;
  }
  std::vector<ph::Component> frame;
  std::vector<size_t>        indexes;
  std::vector<uint64_t>      rigid_ids;
  for (size_t i = 0; i < v.cells.size(); ++i) {
    if (physical.placement_mask(i) == dp::PlacementMask::soft || ph::soft_block(tech.macros.at(v.cells[i].macro))) {
      continue;  // soft blocks are neither track-aligned nor obstacles
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
  if (move && tile_snap) {
    snap_macros_to_tiles(v, tech, frame);
  }
  // Moving macros are still being planned: keep their native planning pad (halo or half the channel).
  const auto hx = move ? std::max(v.halo_x, v.plan_pad_x) : v.halo_x;
  const auto hy = move ? std::max(v.halo_y, v.plan_pad_y) : v.halo_y;
  const auto pdn     = move ? macro_pdn_alignment(v, tech) : std::nullopt;
  bool       aligned = false;
  if (std::any_of(rigid_ids.begin(), rigid_ids.end(), [](auto id) { return id != 0; })) {
    legalize_rigid_macro_tracks(tech,
                                v.fp,
                                frame,
                                rigid_ids,
                                hx,
                                hy,
                                move,
                                v.channel_x,
                                v.channel_y,
                                pdn ? &*pdn : nullptr,
                                &aligned);
  } else {
    legalize_macro_tracks(tech, v.fp, frame, hx, hy, move, v.channel_x, v.channel_y, pdn ? &*pdn : nullptr, &aligned);
  }
  if (pdn) {
    ++v.pdn_alignments;
    v.pdn_fallbacks += !aligned;
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
// Native row/overlap legality. Soft blocks (LEF CLASS BLOCK SOFT) keep their
// GP pose and may overlap anything, so they are checked as zero-size nodes.
bool native_legal(const View& v, const ph::Technology& tech, const dp::Design& physical) {
  std::vector<size_t> soft;
  for (size_t i = 0; i < v.cells.size() && i < physical.node_x.size(); ++i) {
    if (ph::soft_block(tech.macros.at(v.cells[i].macro))) {
      soft.push_back(i);
    }
  }
  if (soft.empty()) {
    return dp::is_legal(physical);
  }
  // Check the geometry without the soft blocks (and without nets, which legality does not read).
  dp::Design rest;
  rest.layout     = physical.layout;
  size_t     next = 0;
  const auto copy = [&](const auto& from, auto& to, size_t i) {
    if (!from.empty()) {
      to.push_back(from[i]);
    }
  };
  for (size_t i = 0; i < physical.node_x.size(); ++i) {
    if (next < soft.size() && soft[next] == i) {
      ++next;
      continue;
    }
    rest.node_x.push_back(physical.node_x[i]);
    rest.node_y.push_back(physical.node_y[i]);
    rest.node_size_x.push_back(physical.node_size_x[i]);
    rest.node_size_y.push_back(physical.node_size_y[i]);
    copy(physical.node_macro, rest.node_macro, i);
    copy(physical.node_placement_mask, rest.node_placement_mask, i);
    copy(physical.node_halo_x, rest.node_halo_x, i);
    copy(physical.node_halo_y, rest.node_halo_y, i);
    copy(physical.node_orientation, rest.node_orientation, i);
    rest.num_movable += i < static_cast<size_t>(physical.num_movable);
  }
  rest.build_maps();
  return dp::is_legal(rest);
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
    if (ph::soft_block(tech.macros.at(c.macro))) {
      // Not legalized: keep the GP position, clamped inside the placement core.
      const auto& l = v.design.layout;
      c.place.x     = std::max<int64_t>(std::llround(l.xl), std::min<int64_t>(c.place.x, std::llround(l.xh) - c.place.width));
      c.place.y     = std::max<int64_t>(std::llround(l.yl), std::min<int64_t>(c.place.y, std::llround(l.yh) - c.place.height));
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
void assign_io(View& v, const ph::Technology& tech, const Options& options) {
  const auto&       l              = v.design.layout;
  const double      pitch          = std::min(l.site_width, l.row_height);
  // Imported positions are frozen by the caller. Automatic left/right pins
  // use the first horizontal layer; top/bottom pins use its next vertical
  // routing layer so an inward access stem follows the preferred direction.
  auto              horizontal     = std::find_if(tech.routing_layers.begin(), tech.routing_layers.end(), [&](const auto& name) {
    return tech.layers.at(name).direction == "HORIZONTAL";
  });
  std::string       layer          = horizontal == tech.routing_layers.end() ? "" : *horizontal;
  auto              vertical       = horizontal == tech.routing_layers.end()
                                         ? tech.routing_layers.end()
                                         : std::find_if(std::next(horizontal), tech.routing_layers.end(), [&](const auto& name) {
                          return tech.layers.at(name).direction == "VERTICAL";
                                           });
  std::string       vertical_layer = vertical == tech.routing_layers.end() ? layer : *vertical;
  // A registered technology names its IO pin layers (OpenROAD's IO_PLACER_H / IO_PLACER_V): the lowest layers are the
  // cell rows' (asap7 M2 tracks follow the rows), so pins there are off the routing grid the router reaches them on.
  try {
    const auto& profile = placement_tech_profile(tech, options);
    const auto  is      = [&](const std::string& name, const char* direction) {
      const auto it = tech.layers.find(name);
      return it != tech.layers.end() && it->second.direction == direction;
    };
    if (is(profile.io_pin_layers[0], "HORIZONTAL") && is(profile.io_pin_layers[1], "VERTICAL")) {
      layer          = profile.io_pin_layers[0];
      vertical_layer = profile.io_pin_layers[1];
    }
  } catch (const std::invalid_argument&) {
    // an unregistered technology keeps the LEF's lowest routing layers
  }
  auto              slots          = io_perimeter_slots(tech, v.fp, layer, pitch, vertical_layer);
  // A left/right pin's stub runs inward on its own track: skip the tracks whose first stretch (about a placement
  // tile) runs into supply metal on the pin layer (the power grid's via landings sit on every rail row there).
  if (!layer.empty() && !v.fp.special_nets.empty()) {
    const auto&  L        = tech.layers.at(layer);
    const double half     = std::round(L.width * tech.dbu / 2);
    const double lpitch   = L.direction == "HORIZONTAL" ? L.pitch_y : L.pitch_x;
    const double spacing  = std::max(0.0, std::round((lpitch - L.width) * tech.dbu));
    const double corridor = 5 * l.row_height;
    std::vector<ph::Rect> supply;
    for (const auto& net : v.fp.special_nets) {
      try {
        for (const auto& shape : ph::route_geometry(tech, v.fp, net, true)) {
          if (shape.layer == layer
              && (shape.rect.xl < v.fp.die.xl + corridor || shape.rect.xh > v.fp.die.xh - corridor)) {
            supply.push_back(shape.rect);
          }
        }
      } catch (const std::exception&) {
        // unreadable supply geometry: keep every slot (the router still checks the stub)
      }
    }
    const auto all_slots = slots;
    std::erase_if(slots, [&](const auto& slot) {
      const bool left = slot.first == v.fp.die.xl, right = slot.first == v.fp.die.xh;
      if (!left && !right) {
        return false;
      }
      return std::ranges::any_of(supply, [&](const ph::Rect& r) {
        const bool near = left ? r.xl < v.fp.die.xl + corridor : r.xh > v.fp.die.xh - corridor;
        return near && r.yl < slot.second + half + spacing && r.yh > slot.second - half - spacing;
      });
    });
    if (slots.size() < v.fp.terminals.size()) {
      slots = all_slots;  // the supply filter is a preference: never leave fewer slots than pins
    }
  }
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
  // "@pretrained" stays symbolic: Uroute_pool reads the compiled-in manifest.
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
void detail::install_handoff_io(dp::Design& design, std::vector<ph::Terminal>& signal_pins, size_t terminal_begin,
                                const std::vector<ph::Terminal>& measured_pins) {
  if (measured_pins.size() < signal_pins.size() || terminal_begin > design.node_x.size()
      || signal_pins.size() > design.node_x.size() - terminal_begin || design.node_y.size() != design.node_x.size()) {
    throw std::logic_error("planning handoff IOs do not match the placer terminal suffix");
  }
  for (size_t i = 0; i < measured_pins.size(); ++i) {
    const auto& pin = measured_pins[i];
    if (i < signal_pins.size()) {
      if (pin.name != signal_pins[i].name || pin.use == "POWER" || pin.use == "GROUND") {
        throw std::logic_error("planning handoff changed the signal IO prefix");
      }
    } else if (pin.use != "POWER" && pin.use != "GROUND") {
      throw std::logic_error("planning handoff appended an unmodelled signal IO");
    }
  }
  for (size_t i = 0; i < signal_pins.size(); ++i) {
    auto&       signal                = signal_pins[i];
    const auto& measured              = measured_pins[i];
    signal.x                          = measured.x;
    signal.y                          = measured.y;
    signal.orientation                = measured.orientation;
    signal.status                     = measured.status;
    signal.shapes                     = measured.shapes;
    design.node_x[terminal_begin + i] = measured.x;
    design.node_y[terminal_begin + i] = measured.y;
  }
}
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
  const auto driver_pin = std::find_if(pins.begin(), pins.end(), [&](const auto& pin) { return pin.first == driver; });
  if (driver_pin == pins.end()) {
    throw std::invalid_argument("Steiner RC requires a driver pin");
  }
  std::vector<std::pair<double, double>> xy;
  xy.reserve(pins.size());
  for (const auto& pin : pins) {
    xy.push_back(pin.second);
  }
  const auto net = steiner_net(xy, static_cast<size_t>(driver_pin - pins.begin()), per_um(wire), dbu, approximate);
  ph::Rc_net rc;
  rc.driver         = std::move(driver);
  rc.wire_length_um = net.wire_length_um;
  rc.nodes.reserve(net.node_cap_ff.size());
  for (size_t i = 0; i < net.node_cap_ff.size(); ++i) {
    rc.nodes.push_back({i < pins.size() ? Pin_label(pins[i].first) : Pin_label(), net.node_cap_ff[i]});
  }
  rc.edges.reserve(net.edges.size());
  for (const auto& edge : net.edges) {
    rc.edges.push_back({edge.from, edge.to, edge.resistance_ohm});
  }
  return rc;
}
namespace {
std::shared_ptr<ph::Parasitics> estimate_rc(const Geometry_view& geometry, const ph::Technology& tech, const Options& options,
                                            ph::Pin_locations* pin_locations, Rc_cache* cache = nullptr,
                                            Rc_reuse_stats* reuse = nullptr, bool provenance = true) {
  const auto& v             = geometry.view;
  const auto& design        = geometry.design;
  auto        result        = std::make_shared<ph::Parasitics>();
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
      const auto node       = design.pin2node[p];
      const auto pin_offset = geometry.pin_offset(p);
      const auto [x, y]     = geometry.pin_position(p, pin_offset);
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
// The wire trees of the nets of a placement view as numbers, for a ranker that never needs them as strings. The identity
// (which pins are on which tree, by label) is built once, while the view keeps its connectivity; each call then only
// reads the pin positions and builds the numeric tree of every net.
struct Tree_cache {
  std::shared_ptr<const timing::fast::Tree_topology> topology;
  std::shared_ptr<ph::Parasitics>                    skeleton;  // one net per tree, driver label only
  std::vector<uint32_t>                              tree_net;  // the view's net index of each tree
};
std::shared_ptr<Tree_cache> make_tree_cache(const Geometry_view& geometry) {
  const auto& v        = geometry.view;
  const auto& design   = geometry.design;
  auto        cache    = std::make_shared<Tree_cache>();
  auto        topology = std::make_shared<timing::fast::Tree_topology>();
  cache->skeleton      = std::make_shared<ph::Parasitics>();
  cache->skeleton->identity    = "tree-skeleton";
  cache->skeleton->approximate = true;
  topology->net_pin_start.push_back(0);
  for (size_t net = 0; net < v.drivers.size(); ++net) {
    const int begin = design.flat_net2pin_start[net], end = design.flat_net2pin_start[net + 1];
    if (end - begin < 2) {
      continue;
    }
    const auto& driver = v.drivers[net];
    int         found  = -1;
    for (int p = begin; p < end; ++p) {
      topology->pin_label.emplace_back(v.pins[p].instance, v.pins[p].pin);
      if (found < 0 && v.pins[p].instance == driver.instance && v.pins[p].pin == driver.pin) {
        found = p - begin;
      }
    }
    if (found < 0) {
      throw std::invalid_argument("Steiner RC requires a driver pin");
    }
    topology->driver_index.push_back(static_cast<uint32_t>(found));
    topology->net_pin_start.push_back(static_cast<uint32_t>(topology->pin_label.size()));
    cache->tree_net.push_back(static_cast<uint32_t>(net));
    cache->skeleton->nets.emplace_back();
    cache->skeleton->nets.back().driver = Pin_label(driver.instance, driver.pin);
  }
  cache->topology = std::move(topology);
  return cache;
}
timing::fast::Wire_trees estimate_wire_trees(const Geometry_view& geometry, const ph::Technology& tech, const Options& options,
                                             const Tree_cache& cache) {
  const auto& v      = geometry.view;
  const auto& design = geometry.design;
  const auto  wire   = technology_wire_rc(tech,
                                         options.liberty,
                                         options.tech_rc_file,
                                         options.resistance_ohm_um,
                                         options.capacitance_ff_um,
                                         options.rc_pessimism,
                                         options.technology().cells.contents);
  (void)v;
  timing::fast::Wire_trees trees;
  trees.topology = cache.topology;
  trees.node_start.reserve(cache.tree_net.size() + 1);
  trees.edge_start.reserve(cache.tree_net.size() + 1);
  trees.node_start.push_back(0);
  trees.edge_start.push_back(0);
  trees.node_cap_ff.reserve(cache.topology->pin_label.size() * 2);
  trees.edges.reserve(cache.topology->pin_label.size() * 2);
  initialize_flute_tables();
  std::vector<std::pair<double, double>> xy;
  for (size_t k = 0; k < cache.tree_net.size(); ++k) {
    const auto net   = cache.tree_net[k];
    const int  begin = design.flat_net2pin_start[net], end = design.flat_net2pin_start[net + 1];
    xy.clear();
    for (int p = begin; p < end; ++p) {
      xy.push_back(geometry.pin_position(p, geometry.pin_offset(p)));
    }
    const auto tree = steiner_net(xy, cache.topology->driver_index[k], per_um(wire), tech.dbu);
    for (const auto cap : tree.node_cap_ff) {
      trees.node_cap_ff.push_back(static_cast<float>(cap));
    }
    for (const auto& edge : tree.edges) {
      trees.edges.push_back({edge.from, edge.to, static_cast<float>(edge.resistance_ohm)});
    }
    trees.node_start.push_back(static_cast<uint32_t>(trees.node_cap_ff.size()));
    trees.edge_start.push_back(static_cast<uint32_t>(trees.edges.size()));
  }
  return trees;
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
// Region_options for the mesh: with cut = "ratio", the tracks a cut line crosses per micron come from the routing
// stack legroute routes on (the horizontal layers above the box layer for a vertical line, the vertical ones for a
// horizontal line), so a split weighs the two axes by what the region can carry.
Region_options region_options_with_tracks(const ph::Technology& tech, const Region_options& o) {
  Region_options ro = o;
  if (ro.cut == "ratio" && ro.h_tracks_per_um <= 0 && ro.v_tracks_per_um <= 0) {
    size_t k = 0;
    for (const auto& name : tech.routing_layers) {
      ++k;
      const auto it = tech.layers.find(name);
      if (it == tech.layers.end() || k <= 2 || k > 7) {
        continue;  // M1 holds the pins, M2 is legroute's box layer; the stack above M7 is not routed
      }
      const auto&  L     = it->second;
      const bool   h     = L.direction == "HORIZONTAL";
      const double pitch = h ? (L.pitch_y > 0 ? L.pitch_y : L.pitch_x) : (L.pitch_x > 0 ? L.pitch_x : L.pitch_y);
      if (pitch > 0) {
        (h ? ro.h_tracks_per_um : ro.v_tracks_per_um) += 1.0 / pitch;
      }
    }
  }
  return ro;
}
// Each cell takes the master its component in frame has: the anchor prepass
// and the two-row ECO pick the rail twin a start needs. frame lists v.cells
// first, in order.
void adopt_rail_twins(View& v, const ph::Floorplan& frame) {
  for (size_t i = 0; i < v.cells.size(); ++i) {
    take_rail_twin(v, i, frame.components.at(i).macro);
  }
}
// The same on a frame legroute placed: each two-row cell with a rail twin
// takes the master the PG rails of its rows need, before the frame is checked
// or cached. Returns the cells switched.
size_t sync_rail_twins(const ph::Technology& tech, ph::Floorplan& frame, const Options& options) {
  if (options.multirow_templates.empty() || frame.rows.empty()) {
    return 0;
  }
  const Pg_rail_model rails(tech);
  const auto          rh       = std::llround(tech.sites.at(frame.rows.front().site).height * tech.dbu);
  size_t              switched = 0, tall = 0, unmatched = 0;
  for (auto& c : frame.components) {
    const auto t = options.multirow_templates.find(c.macro);
    const auto m = tech.macros.find(c.macro);
    const auto h = m == tech.macros.end() ? int64_t{0} : std::llround(m->second.height * tech.dbu);  // the master's, not the pose's
    if (t == options.multirow_templates.end() || t->second.rail_twin.empty() || c.place.status == Status::UNPLACED || h <= rh) {
      continue;
    }
    ++tall;
    std::vector<Orientation> rows;
    for (int64_t y = c.place.y; y < c.place.y + h; y += rh) {
      const auto row = std::find_if(frame.rows.begin(), frame.rows.end(), [&](const ph::Row& r) {
        return r.y == y && c.place.x >= r.x && c.place.x < r.x + r.count * r.step;
      });
      if (row == frame.rows.end()) {
        break;
      }
      rows.push_back(row->orientation);
    }
    if (rows.size() * rh != size_t(h)) {
      ++unmatched;
      continue;
    }
    if (rails.fit(c.macro, c.place.orientation, rows) == Rail_fit::mismatch) {
      if (rails.fit(t->second.rail_twin, c.place.orientation, rows) == Rail_fit::fit) {
        c.macro = t->second.rail_twin;
        ++switched;
      } else {
        ++unmatched;
      }
    }
  }
  std::fprintf(
      stderr,
      "legroute: rail twins: %zu of %zu two-row cells switched to the twin their rows need, %zu without a fit (%zu templates)\n",
      switched,
      tall,
      unmatched,
      options.multirow_templates.size());
  return switched;
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
    check_legality(tech,
                   check,
                   double(v.halo_x) / tech.dbu,
                   double(v.halo_y) / tech.dbu,
                   double(v.channel_x) / tech.dbu,
                   double(v.channel_y) / tech.dbu);
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
// ---- 2i-interact: the adapter between the solver and gui::Session ---------------------------------------------------
// Pins and the action trace use node *names*; the scene maps them to this phase's solver indices.
std::shared_ptr<const gui::Scene> gui_scene(const View& v, const ph::Technology& tech, const std::string& phase) {
  auto        scene = std::make_shared<gui::Scene>();
  const auto& d     = v.design;
  scene->phase      = phase;
  scene->dbu_per_um = tech.dbu;
  scene->die        = {d.layout.xl, d.layout.yl, d.layout.xh, d.layout.yh};
  scene->frame_die  = {v.fp.die.xl, v.fp.die.yl, v.fp.die.xh, v.fp.die.yh};
  for (int i = 0; i < d.num_movable && size_t(i) < d.node_size_x.size(); ++i) {
    scene->cell_area += d.node_size_x[size_t(i)] * d.node_size_y[size_t(i)];
  }
  scene->nodes.reserve(d.node_x.size());
  // Registers: instances with a sink on a net that carries the clock role.
  std::unordered_set<std::string> clocked;
  for (const auto& [driver, sinks] : v.nets) {
    const auto& roles = driver.instance == "PIN" ? v.prects.clock_ports : v.prects.clock_pins;
    if (roles.contains(driver.label())) {
      for (const auto& sink : sinks) {
        if (sink.instance != "PIN") {
          clocked.insert(sink.instance);
        }
      }
    }
  }
  for (size_t i = 0; i < d.node_x.size(); ++i) {
    gui::Node n;
    n.w = d.node_size_x[i];
    n.h = d.node_size_y[i];
    if (i < v.cells.size()) {
      n.name    = v.cells[i].name;
      n.master  = v.cells[i].macro;
      if (const auto m = tech.macros.find(n.master); m != tech.macros.end()) {
        n.klass = m->second.klass;
      }
      n.pins    = i + 1 < d.flat_node2pin_start.size() ? uint32_t(d.flat_node2pin_start[i + 1] - d.flat_node2pin_start[i]) : 0;
      n.seq     = clocked.contains(v.cells[i].name);
      n.movable = int(i) < d.num_movable;
      const bool macro = i < d.node_macro.size() && d.node_macro[i];
      n.kind    = macro ? (n.movable ? gui::Kind::macro : gui::Kind::fixed) : gui::Kind::cell;
    } else if (i < v.terminal_begin) {
      n.name    = "blockage#" + std::to_string(i);
      n.kind    = gui::Kind::blockage;
      n.movable = false;
    } else {
      n.name    = v.fp.terminals.at(i - v.terminal_begin).name;
      n.master  = "PIN";
      n.kind    = gui::Kind::io;
      n.movable = false;
    }
    scene->nodes.push_back(std::move(n));
  }
  if (!d.centroid_groups.empty()) {  // FP candidates / frozen macro-stage targets, by group name
    const auto samples = dp::detail::centroid_summary(d);
    for (size_t g = 0; g < d.centroid_groups.size() && g < samples.size(); ++g) {
      scene->centroids.push_back({d.centroid_groups[g].name, samples[g].x, samples[g].y, samples[g].radius, true});
    }
  }
  scene->build_index();
  return scene;
}
// Edits from the session -> what the solver applies (pins, releases, centroid targets).
dp::InteractUpdate gui_update(const gui::Edits& edits) {
  dp::InteractUpdate update;
  for (const auto& p : edits.pins) {
    update.pins.push_back({p.node, p.x, p.y, 0});
  }
  update.releases = edits.releases;
  for (const auto& c : edits.centroids) {
    update.centroid_targets.push_back({c.group, c.x, c.y});
  }
  update.release_centroids = edits.release_centroids;
  return update;
}
// HPWL (um) of the solver's current positions, over the design's own connectivity.
double gui_hpwl_um(const dp::Design& d, const double* x, const double* y, double dbu) {
  double total = 0;
  for (size_t net = 0; net + 1 < d.flat_net2pin_start.size(); ++net) {
    double xl = 1e300, xh = -1e300, yl = 1e300, yh = -1e300;
    const int begin = d.flat_net2pin_start[net], end = d.flat_net2pin_start[net + 1];
    if (end - begin < 2) {
      continue;
    }
    for (int k = begin; k < end; ++k) {
      const int p = d.flat_net2pin[k], node = d.pin2node[p];
      const double px = x[node] + d.pin_offset_x[p], py = y[node] + d.pin_offset_y[p];
      xl = std::min(xl, px);
      xh = std::max(xh, px);
      yl = std::min(yl, py);
      yh = std::max(yh, py);
    }
    total += (xh - xl) + (yh - yl);
  }
  return total / dbu;
}
struct Gui_map_env {
  // the region mesh the page draws ("regions"): its options, the fixed macros and the IO terminals it is cut around
  Region_options         region;
  std::vector<int>       region_macros;
  std::vector<Region_io> region_ios;
  double                 dbu = 1;
  double                 ch = 1, cv = 1;
  std::function<void()> init;
  int                   bins = 32, degree = 40, accuracy = 3;
  std::shared_ptr<const dp::UrouteContext> uroute;  // loaded only when the run uses the URoute model
};
// The region mesh at the given positions (what bus order and boundary pins are cut along in the detail-final phase) and
// where the nets cross its borders: each net's Steiner tree against every mesh edge. The crossings are the places the
// boundary pins of the region refinement would sit; the list is thinned to a cap, the total is reported.
gui::Grid gui_regions(const dp::Design& base, const double* x, const double* y, size_t n, const Gui_map_env& env) {
  gui::Grid g;
  dp::Design d = base;
  for (size_t i = 0; i < n && i < d.node_x.size(); ++i) {
    d.node_x[i] = x[i];
    d.node_y[i] = y[i];
  }
  for (int i = 0; i < d.num_movable; ++i) {
    if (!d.node_macro.empty() && d.node_macro[size_t(i)]) {
      g.note = "regions are cut around placed macros: they appear in the final phase";
      return g;
    }
  }
  const auto mesh = Region_mesh::build(d, env.region_macros, env.region_ios, env.region, env.dbu);
  for (const auto& r : mesh.rects) {
    g.rects.insert(g.rects.end(), {float(r.xl), float(r.yl), float(r.xh), float(r.yh), float(r.cells)});
  }
  if (mesh.rects.size() < 2) {
    g.note = std::format("one region: {} cells fit region.max_cells={}; lower synth.place.region.max_cells to partition it",
                         d.num_movable,
                         env.region.max_cells);
    return g;
  }
  constexpr size_t cap = 40000;
  size_t           stride = 1, seen = 0, skipped = 0;
  const auto       keep   = [&](double cx, double cy, int axis) {
    ++g.crossing_total;
    if (seen++ % stride == 0) {
      g.crossings.insert(g.crossings.end(), {float(cx), float(cy), float(axis)});
      if (g.crossings.size() > 3 * cap) {  // thin to every other one
        std::vector<float> half;
        for (size_t i = 0; i + 2 < g.crossings.size(); i += 6) {
          half.insert(half.end(), g.crossings.begin() + long(i), g.crossings.begin() + long(i) + 3);
        }
        g.crossings = std::move(half);
        stride *= 2;
      }
    }
  };
  std::vector<Point> points;
  for (size_t net = 0; net + 1 < d.flat_net2pin_start.size(); ++net) {
    const int begin = d.flat_net2pin_start[net], end = d.flat_net2pin_start[net + 1];
    if (end - begin < 2) {
      continue;
    }
    if (end - begin > 64) {  // a wide net (clock, reset): its tree is not the story
      ++skipped;
      continue;
    }
    points.clear();
    int first = -2;
    bool crosses = false;
    for (int k = begin; k < end; ++k) {
      const int p = d.flat_net2pin[size_t(k)], node = d.pin2node[size_t(p)];
      const double px = d.node_x[size_t(node)] + d.pin_offset_x[size_t(p)], py = d.node_y[size_t(node)] + d.pin_offset_y[size_t(p)];
      points.emplace_back(px, py);
      const int at = mesh.rect_of(px, py).value_or(-1);
      if (first == -2) {
        first = at;
      } else if (at != first) {
        crosses = true;
      }
    }
    if (!crosses) {
      continue;
    }
    const auto tree = steiner_tree(points);
    for (const auto& [a, b] : tree.edges) {
      const auto [ax, ay] = tree.points[a];
      const auto [bx, by] = tree.points[b];
      for (const auto& e : mesh.edges) {
        const double pa = e.axis == 0 ? ax : ay, pb = e.axis == 0 ? bx : by;
        if ((pa < e.position) == (pb < e.position) || pa == pb) {
          continue;
        }
        const double t     = (e.position - pa) / (pb - pa);
        const double along = e.axis == 0 ? ay + t * (by - ay) : ax + t * (bx - ax);
        if (along < e.low || along > e.high) {
          continue;
        }
        e.axis == 0 ? keep(e.position, along, 0) : keep(along, e.position, 1);
      }
    }
  }
  g.note = std::format("{} regions, {} net crossings of their borders{}",
                       mesh.rects.size(),
                       g.crossing_total,
                       skipped ? std::format(" ({} wide nets left out)", skipped) : std::string{});
  return g;
}
// "rudy" (bounding-box demand) or "rsmt" (Steiner demand) utilization of the given positions: max of the two directions.
gui::Grid gui_map(const std::string& kind, const dp::Design& base, const double* x, const double* y, size_t n, const Gui_map_env& env) {
  if (kind == "regions") {
    return gui_regions(base, x, y, n, env);
  }
  dp::Design d = base;
  for (size_t i = 0; i < n && i < d.node_x.size(); ++i) {
    d.node_x[i] = x[i];
    d.node_y[i] = y[i];
  }
  if (kind == "uroute" && !env.uroute) {
    gui::Grid none;
    none.note = "the URoute model is not loaded (synth.place.route.model=uroute or auto)";
    return none;
  }
  dp::RudyOptions ro;
  ro.bins_x = ro.bins_y   = env.bins;
  ro.horizontal_capacity  = env.ch;
  ro.vertical_capacity    = env.cv;
  ro.macro_blockages      = ro.pin_access = true;
  ro.model                = kind == "uroute" ? dp::RudyModel::uroute : kind == "rsmt" ? dp::RudyModel::rsmt : dp::RudyModel::bbox;
  ro.uroute               = env.uroute;
  ro.degree               = env.degree;
  ro.accuracy             = env.accuracy;
  ro.initialize           = env.init;
  const auto r            = dp::rudy(d, ro);
  gui::Grid  g;
  g.bx = g.by = env.bins;
  g.v.resize(r.horizontal.size());
  for (size_t i = 0; i < g.v.size(); ++i) {
    g.v[i] = float(std::max(r.horizontal[i], i < r.vertical.size() ? r.vertical[i] : 0.));
    g.peak = std::max(g.peak, double(g.v[i]));
  }
  g.overflow = r.overflow;
  return g;
}

// WNS/TNS (ps) of the given positions through the flow's timer (Options::gui_rank), the way a GP checkpoint ranks them:
// RC from the geometry, then a ranking evaluation. It reads the solver and never feeds anything back.
bool gui_timing(const View& v, const ph::Technology& tech, const Options& options, const Result& result, const dp::Design& base,
                const double* x, const double* y, size_t n, gui::Timing_query& q) {
  if (!options.gui_rank || !result.graph) {
    return false;
  }
  dp::Design d = base;
  for (size_t i = 0; i < n && i < d.node_x.size(); ++i) {
    d.node_x[i] = x[i];
    d.node_y[i] = y[i];
  }
  const Geometry_view geometry{v, d};
  for (size_t i = 0; i < geometry.view.cells.size(); ++i) {
    geometry.view.cells[i].node.attr(attrs::place).set(geometry.pose(i));
  }
  Result checkpoint;
  checkpoint.prects      = v.prects;
  checkpoint.graph       = result.graph;
  checkpoint.net_weights = v.weights;
  checkpoint.fp_library  = result.fp_library;
  auto rc_options        = options;
  rc_options.full        = false;
  checkpoint.parasitics  = estimate_rc(geometry, tech, rc_options, nullptr);
  record_poses(*checkpoint.parasitics, geometry);
  const auto fb = prects_data_timing(options.gui_rank({checkpoint.graph, checkpoint.parasitics, v.ignored}), v.ignored);
  // The most critical cells: the worst pin slack of each instance (pin labels are "instance:pin"), ascending.
  const auto critical = [&] {
    if (!q.want_critical || !fb.ranking || !fb.ranking->layout) {
      return;
    }
    const auto& labels = fb.ranking->layout->labels;
    std::unordered_map<std::string, double> worst;
    for (size_t i = 0; i < fb.ranking->pins.size() && i < labels.size(); ++i) {
      const auto& pin = fb.ranking->pins[i];
      const auto  at  = labels[i].rfind(':');
      if (pin.clock || !std::isfinite(pin.slack) || at == std::string::npos) {
        continue;  // clock pins and the design's own ports are not cells
      }
      auto [it, fresh] = worst.emplace(labels[i].substr(0, at), pin.slack * fb.time_unit_ps);
      if (!fresh) {
        it->second = std::min(it->second, pin.slack * fb.time_unit_ps);
      }
    }
    std::vector<std::pair<std::string, double>> sorted(worst.begin(), worst.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second != b.second ? a.second < b.second : a.first < b.first; });
    for (const auto& entry : sorted) {
      if (int(q.critical.size()) >= q.n || (q.margin_ps >= 0 && entry.second > sorted.front().second + q.margin_ps)) {
        break;
      }
      q.critical.push_back(entry);
    }
  };
  if (fb.wns) {
    q.wns_ps = *fb.wns * fb.time_unit_ps;
    q.tns_ps = fb.tns ? *fb.tns * fb.time_unit_ps : std::nan("");
    critical();
    return true;
  }
  // A ranking evaluation has no sign-off WNS/TNS (and the shared detailed-evaluation allowance must not be spent here):
  // report the worst pin slack as an estimate and leave TNS unknown.
  if (fb.ranking) {
    double worst = std::numeric_limits<double>::infinity();
    for (const auto& pin : fb.ranking->pins) {
      if (!pin.clock && std::isfinite(pin.slack)) {
        worst = std::min(worst, pin.slack);
      }
    }
    if (std::isfinite(worst)) {
      q.wns_ps = worst * fb.time_unit_ps;
      q.tns_ps = std::nan("");
      critical();
      return true;
    }
  }
  return false;
}
gui::Timing_provider gui_timing_provider(const View* v, const ph::Technology* tech, const Options* options, const Result* result,
                                         const dp::Design* base, const double* x, const double* y, size_t n) {
  return [=](gui::Timing_query& q) { return gui_timing(*v, *tech, *options, *result, *base, x, y, n, q); };
}

gui::Fork_files gui_fork_build(const View& v, const ph::Technology& tech, const gui::Fork_request& req);

// Publishes this phase, blocks at its gate (the session starts paused), and installs the poll hook and the
// legalization gate on `control`. Pins that survived from an earlier phase move the design before the solve.
void gui_attach(dp::Options& control, const Options& options, View& v, const ph::Technology& tech, const Result& result,
                const std::string& phase) {
  auto session = options.gui;
  if (!session) {
    return;
  }
  auto&        d = v.design;
  const double dbu = tech.dbu;
  session->begin_phase(gui_scene(v, tech, phase), d.node_x.data(), d.node_y.data(), d.node_x.size());
  session->set_fork_builder([vp = &v, tp = &tech](const gui::Fork_request& r) { return gui_fork_build(*vp, *tp, r); });
  auto env = std::make_shared<Gui_map_env>();
  env->ch = control.route_capacity_h;
  env->cv = control.route_capacity_v;
  env->init = control.route_initialize;
  env->bins = std::clamp(route_grid_bins(control.auxiliary_bins, options.bins), 8, 64);
  env->uroute = control.uroute;
  env->degree = control.route.degree;
  env->accuracy = control.route.accuracy;
  env->dbu = tech.dbu;
  env->region = region_options_with_tracks(tech, options.region);
  for (size_t i = size_t(std::max(0, d.num_movable)); i < v.cells.size(); ++i) {  // the fixed macros (not placement masks)
    if (!tech.macros.at(v.cells[i].macro).klass.starts_with("CORE") && !v.cells[i].name.starts_with("__lhd_frame_mask_")) {
      env->region_macros.push_back(int(i));
    }
  }
  for (const auto& t : v.fp.terminals) {
    env->region_ios.push_back({t.name, double(t.x), double(t.y)});
  }
  gui::Stats start;
  start.hpwl_um = gui_hpwl_um(d, d.node_x.data(), d.node_y.data(), dbu);
  const gui::Map_provider start_maps = [&](const std::string& k) { return gui_map(k, d, d.node_x.data(), d.node_y.data(), d.node_x.size(), *env); };
  const auto start_timing = gui_timing_provider(&v, &tech, &options, &result, &d, d.node_x.data(), d.node_y.data(), d.node_x.size());
  auto pending = std::make_shared<gui::Edits>(session->gate(phase, d.node_x.data(), d.node_y.data(), d.node_x.size(), start, &start_maps, &start_timing));
  for (const auto& pin : pending->pins) {
    if (pin.node >= 0 && pin.node < d.num_movable) {
      d.node_x[size_t(pin.node)] = pin.x;
      d.node_y[size_t(pin.node)] = pin.y;
    }
  }
  control.interact_interval  = std::max(1, session->config().poll);
  control.interact_centroids = !d.centroid_groups.empty();
  const View*                vp = &v;
  const ph::Technology*      tp = &tech;
  const Options*             op = &options;
  const Result*              rp = &result;
  control.interact          = [session, pending, dbu, env, vp, tp, op, rp](const dp::InteractView& view) {
    gui::Stats st;
    st.hpwl_um = gui_hpwl_um(*view.design, view.x, view.y, dbu);
    const gui::Map_provider maps = [&](const std::string& k) { return gui_map(k, *view.design, view.x, view.y, size_t(view.count), *env); };
    const auto timing = gui_timing_provider(vp, tp, op, rp, view.design, view.x, view.y, size_t(view.count));
    if (view.centroids) {
      for (const auto& c : *view.centroids) {
        st.centroids.push_back({c.x, c.y, c.radius});
      }
    }
    if (view.finishing) {
      st.iteration = view.iteration;
      auto end     = session->gp_end(view.x, view.y, size_t(view.count), st, &maps, &timing);
      auto update  = gui_update(end.edits);
      update.extend = end.extend;
      return update;
    }
    auto edits = session->poll(view.iteration, view.x, view.y, size_t(view.count), st, &maps, &timing);
    if (view.first) {
      // A new place() call (the GP, then any retry or burst) holds nothing yet: re-send every pin of this phase.
      auto all = session->all_pins();
      for (const auto& p : edits.pins) {
        std::erase_if(all.pins, [&](const auto& q) { return q.node == p.node; });
      }
      edits.pins.insert(edits.pins.end(), all.pins.begin(), all.pins.end());
      for (const auto& c : edits.centroids) {
        std::erase_if(all.centroids, [&](const auto& q) { return q.group == c.group; });
      }
      edits.centroids.insert(edits.centroids.end(), all.centroids.begin(), all.centroids.end());
    }
    pending->pins.clear();
    return gui_update(edits);
  };
  const auto prior = control.before_legalize;
  const bool macro_only = control.macro_only_legalize;
  control.before_legalize = [prior, session, macro_only, dbu, env, vp, tp, op, rp](dp::Design& physical) {
    if (prior) {
      prior(physical);
    }
    gui::Stats st;
    st.hpwl_um = gui_hpwl_um(physical, physical.node_x.data(), physical.node_y.data(), dbu);
    const gui::Map_provider maps = [&](const std::string& k) {
      return gui_map(k, physical, physical.node_x.data(), physical.node_y.data(), physical.node_x.size(), *env);
    };
    const auto timing = gui_timing_provider(vp, tp, op, rp, &physical, physical.node_x.data(), physical.node_y.data(), physical.node_x.size());
    session->gate(macro_only ? "macro_legalize" : "legalize", physical.node_x.data(), physical.node_y.data(), physical.node_x.size(), st, &maps, &timing);
  };
}

// 2i-interact fork: the cells of a region as a design of their own. The netlist comes from the placer's own bit-level
// connectivity (View::nets), so buffers and ties added by the flow so far are real cells of it; every net that
// crosses the region becomes a top port of the new design, fixed in its floorplan DEF where the net crossed the box.
// Nets from a real top input / to a real top output keep that port's name, so the SDC still applies to them.
gui::Fork_files gui_fork_build(const View& v, const ph::Technology& tech, const gui::Fork_request& req) {
  gui::Fork_files out;
  const auto      fail = [&](std::string why) {
    out.error = std::move(why);
    return out;
  };
  const auto& scene = *req.scene;
  const auto& frame = *req.frame;
  std::set<std::string> region(req.cells.begin(), req.cells.end());
  std::map<std::string, const Cell*> mine;
  for (const auto& name : req.cells) {
    const auto it = v.cell.find(name);
    if (it == v.cell.end()) {
      return fail("cell " + name + " is not in the placement netlist");
    }
    const auto& c = v.cells.at(it->second);
    const auto  io = c.node.get_subnode_io();
    if (!io) {
      return fail("cell " + name + " has no master declaration");
    }
    for (const auto* decls : {&io->get_input_pin_decls(), &io->get_output_pin_decls()}) {
      for (const auto& d : *decls) {
        if (d.bits > 1) {
          return fail(std::format("{} ({}) has bus pins ({}); forks take regions of single-bit cells", name, c.macro, d.name));
        }
      }
    }
    mine.emplace(name, &c);
  }
  // Where an endpoint is in the parent (center of a cell, the pin of a terminal), from the live frame.
  const auto position = [&](const Endpoint& e) -> std::optional<std::pair<double, double>> {
    const auto f = scene.index.find(e.instance == "PIN" ? e.pin : e.instance);
    if (f == scene.index.end() || size_t(f->second) >= frame.x.size()) {
      return std::nullopt;
    }
    const auto i = size_t(f->second);
    return std::pair{double(frame.x[i]) + scene.nodes[i].w / 2, double(frame.y[i]) + scene.nodes[i].h / 2};
  };
  // The core: the box snapped inward to complete rows and sites of the parent's rows.
  std::vector<ph::Row> rows;
  for (const auto& r : v.fp.rows) {
    const auto site = tech.sites.find(r.site);
    if (site == tech.sites.end() || r.step <= 0) {
      continue;
    }
    const auto height = int64_t(std::llround(site->second.height * tech.dbu));
    if (double(r.y) < req.box.yl || double(r.y + height) > req.box.yh) {
      continue;
    }
    const auto first = std::max<int64_t>(0, int64_t(std::ceil((req.box.xl - double(r.x)) / double(r.step) - 1e-9)));
    const auto last  = std::min<int64_t>(r.count, int64_t(std::floor((req.box.xh - double(r.x)) / double(r.step) + 1e-9)));
    if (last - first < 1) {
      continue;
    }
    auto row  = r;
    row.x     = r.x + first * r.step;
    row.count = last - first;
    rows.push_back(std::move(row));
  }
  if (rows.empty()) {
    return fail("the region holds no complete row; make it taller");
  }
  // Keep a rectangle: the columns every kept row has, and the longest run of consecutive rows.
  std::ranges::sort(rows, {}, &ph::Row::y);
  int64_t x0 = rows.front().x, x1 = rows.front().x + rows.front().count * rows.front().step;
  for (const auto& r : rows) {
    x0 = std::max(x0, r.x);
    x1 = std::min(x1, r.x + r.count * r.step);
  }
  if (x1 <= x0) {
    return fail("the rows of the region share no columns");
  }
  for (auto& r : rows) {
    r.count = (x1 - x0) / r.step;
    r.x     = x0;
  }
  int64_t y0 = rows.front().y, y1 = rows.back().y + 1;
  {
    const auto site   = tech.sites.at(rows.back().site);
    y1                = rows.back().y + int64_t(std::llround(site.height * tech.dbu));
  }
  out.core = {double(x0), double(y0), double(x1), double(y1)};
  // Nets: classify each by where its driver and sinks are.
  struct Port {
    std::string name;
    bool        input = false;
    double      x = 0, y = 0;  // where the net crossed the core (the parent's position, snapped to the core edge)
  };
  std::vector<Port> ports;
  std::map<std::string, size_t> port_of;
  const auto edge_point = [&](double px, double py) {
    const auto& b = out.core;
    double      cx = std::clamp(px, b.xl, b.xh), cy = std::clamp(py, b.yl, b.yh);
    if (cx > b.xl && cx < b.xh && cy > b.yl && cy < b.yh) {  // inside: the nearest edge
      const double dl = cx - b.xl, dr = b.xh - cx, db = cy - b.yl, dt = b.yh - cy;
      const double m = std::min({dl, dr, db, dt});
      (m == dl ? cx = b.xl : m == dr ? cx = b.xh : m == db ? cy = b.yl : cy = b.yh);
    }
    return std::pair{cx, cy};
  };
  const auto add_port = [&](const std::string& name, bool input, const std::vector<std::pair<double, double>>& far) {
    if (port_of.contains(name)) {
      return;
    }
    double sx = 0, sy = 0;
    for (const auto& [px, py] : far) {
      sx += px;
      sy += py;
    }
    const auto n = far.empty() ? 1.0 : double(far.size());
    const auto [ex, ey] = edge_point(far.empty() ? (out.core.xl + out.core.xh) / 2 : sx / n, far.empty() ? out.core.yl : sy / n);
    port_of.emplace(name, ports.size());
    ports.push_back({name, input, ex, ey});
  };
  struct Net_use {
    Endpoint                 driver;
    std::vector<Endpoint>    inside;      // sinks in the region
    std::string              in_port;     // the region's input port feeding them (driver outside)
    std::vector<std::string> out_ports;   // output ports this (inside) driver feeds
  };
  std::vector<Net_use> uses;
  size_t cut_in = 0, cut_out = 0;
  for (const auto& [driver, sinks] : v.nets) {
    if (driver.constant >= 0) {
      continue;
    }
    const bool driver_in = driver.instance != "PIN" && region.contains(driver.instance);
    Net_use    use;
    use.driver = driver;
    std::vector<std::pair<double, double>> far_sinks;
    bool outside_sink = false;
    for (const auto& s : sinks) {
      if (s.instance != "PIN" && region.contains(s.instance)) {
        use.inside.push_back(s);
      } else if (s.instance == "PIN") {
        if (driver_in) {
          const auto at = position(s);
          add_port(s.pin, false, at ? std::vector{*at} : std::vector<std::pair<double, double>>{});
          use.out_ports.push_back(s.pin);
        }
      } else if (driver_in) {
        outside_sink = true;
        if (const auto at = position(s)) {
          far_sinks.push_back(*at);
        }
      }
    }
    if (driver_in) {
      if (outside_sink) {
        const auto name = std::format("cut_out_{}", cut_out++);
        add_port(name, false, far_sinks);
        use.out_ports.push_back(name);
      }
      if (use.inside.empty() && use.out_ports.empty()) {
        continue;
      }
    } else {
      if (use.inside.empty()) {
        continue;
      }
      const auto name = driver.instance == "PIN" ? driver.pin.str() : std::format("cut_in_{}", cut_in++);
      const auto at   = position(driver);
      add_port(name, true, at ? std::vector{*at} : std::vector<std::pair<double, double>>{});
      use.in_port = name;
    }
    uses.push_back(std::move(use));
  }
  out.cells   = mine.size();
  out.inputs  = std::ranges::count_if(ports, [](const auto& p) { return p.input; });
  out.outputs = ports.size() - out.inputs;
  out.top     = std::filesystem::path(req.dir).filename().string();
  // The netlist: masters declared as in the parent, one node per cell, one connection per net sink.
  hhds::GraphLibrary                                       library;
  std::map<std::string, std::shared_ptr<hhds::GraphIO>>  masters;
  for (const auto& [name, c] : mine) {
    if (masters.contains(c->macro)) {
      continue;
    }
    const auto src = c->node.get_subnode_io();
    auto       dst = library.create_io(c->macro);
    for (const auto& d : src->get_input_pin_decls()) {
      dst->add_input(d.name, d.port_id);
      dst->set_bits(d.name, 1);
    }
    for (const auto& d : src->get_output_pin_decls()) {
      dst->add_output(d.name, d.port_id);
      dst->set_bits(d.name, 1);
    }
    masters.emplace(c->macro, dst);
  }
  auto top = library.create_io(out.top);
  {
    hhds::Port_id next = 0;
    for (const auto& p : ports) {
      p.input ? top->add_input(p.name, next++) : top->add_output(p.name, next++);
      top->set_bits(p.name, 1);
    }
  }
  auto graph = top->create_graph();
  std::map<std::string, Node> nodes;
  for (const auto& [name, c] : mine) {
    auto node = gu::create_typed_node(*graph, Ntype_op::Sub);
    node.set_subnode(masters.at(c->macro));
    node.set_name(name);
    nodes.emplace(name, node);
  }
  std::map<std::pair<std::string, std::string>, Pin> driver_pins;
  for (const auto& use : uses) {
    Pin d;
    if (use.in_port.empty()) {
      auto& slot = driver_pins[{use.driver.instance, use.driver.pin}];
      if (slot.is_invalid()) {
        slot = nodes.at(use.driver.instance).create_driver_pin(use.driver.pin);
        gu::set_bits(slot, 1);
      }
      d = slot;
    } else {
      d = graph->get_input_pin(use.in_port);
      gu::set_bits(d, 1);
    }
    for (const auto& s : use.inside) {
      nodes.at(s.instance).create_sink_pin(s.pin).connect_driver(d);
    }
    for (const auto& name : use.out_ports) {
      graph->get_output_pin(name).connect_driver(d);
    }
  }
  std::filesystem::create_directories(req.dir);
  out.net_dir = req.dir + "/net";
  library.save(out.net_dir);
  // The floorplan: the rows of the region and the boundary pins, FIXED where the nets crossed. Pin shapes come from a
  // terminal of the parent (same layer and size).
  if (v.fp.terminals.empty()) {
    return fail("the design has no IO pins to take a pin shape from");
  }
  ph::Floorplan fp;
  fp.design = out.top;
  fp.dbu    = v.fp.dbu;
  fp.die    = {out.core.xl, out.core.yl, out.core.xh, out.core.yh};
  fp.rows   = rows;
  fp.tracks = v.fp.tracks;
  for (const auto& p : ports) {
    auto t        = v.fp.terminals.front();
    t.name        = p.name;
    t.net         = p.name;
    t.direction   = p.input ? "INPUT" : "OUTPUT";
    t.status      = ph::Status::FIXED;
    t.x           = int64_t(std::llround(p.x));
    t.y           = int64_t(std::llround(p.y));
    t.orientation = ph::Orientation::N;
    fp.terminals.push_back(std::move(t));
  }
  out.floorplan = req.dir + "/floorplan.def";
  ph::write_def(fp, out.floorplan);
  // Where every cell was in the parent: the region's own placement starts from it.
  out.seed = req.dir + "/seed.tsv";
  {
    std::ofstream seed(out.seed);
    for (const auto& [name, c] : mine) {
      if (const auto f = scene.index.find(name); f != scene.index.end() && size_t(f->second) < frame.x.size()) {
        seed << name << '\t' << std::llround(frame.x[size_t(f->second)]) << '\t' << std::llround(frame.y[size_t(f->second)]) << '\n';
      }
    }
  }
  return out;
}

dp::Result local_place(View& v, const ph::Technology& tech, const Options& options, const ph::Floorplan& reference,
                       const std::vector<std::string>& forced, Eco_stats& stats, const ph::Rect* window_hint = nullptr,
                       const std::map<std::string, std::pair<double, double>>* held = nullptr) {
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
  if (window_hint) {  // 2i-interact: the first window is the edited region, not just the hull of the changed cells
    bounds = bounds.hull(*window_hint);
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
      if (held && !held->empty()) {
        // Held cells keep their position through the window's GP (a hold, as in the interactive GP) and are then
        // legalized like any other cell.
        std::vector<dp::MovablePosition> pins;
        for (const auto& [name, pos] : *held) {
          const auto it = v.cell.find(name);
          if (it == v.cell.end() || int(it->second) >= d.num_movable) {
            continue;
          }
          const size_t i = it->second;
          pins.push_back({int(i),
                          std::clamp(pos.first, window.xl + d.node_halo_x[i], window.xh - d.node_size_x[i] - d.node_halo_x[i]),
                          std::clamp(pos.second, window.yl + d.node_halo_y[i], window.yh - d.node_size_y[i] - d.node_halo_y[i]),
                          0});
        }
        control.detailed_place    = false;  // detailed placement is HPWL-driven and would walk a held cell back
        control.interact_interval = 1 << 30;  // only the first step of the solve
        control.interact          = [pins](const dp::InteractView& view) {
          dp::InteractUpdate update;
          if (view.first) {
            update.pins = pins;
          }
          return update;
        };
      }
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

// 2i-interact: post-legalization edit mode. After the final legalization the session lets the user move cells and
// macros; the edits of one "Apply" are re-placed by the ECO windowed placer (a few GP iterations with the moved
// objects held, then legalization, no detailed placement), confined to the Region_mesh region that holds each object.
// A move that leaves its region re-places the whole core. The flow runs several final passes (restarts, region
// experiments); the user edits once, and every later pass re-applies the recorded edits without blocking.
using Named_edits = std::vector<std::pair<std::string, std::pair<double, double>>>;
bool gui_apply_edits(View& v, const ph::Technology& tech, const Options& options, const Named_edits& edits,
                     const std::function<std::vector<int>()>& macros, const std::function<std::vector<Region_io>()>& ios) {
  auto session = options.gui;
  make_arrays(v, tech);
  auto&                    d = v.design;
  ph::Floorplan            reference = v.fp;
  reference.components.clear();
  for (const auto& c : v.cells) {
    reference.components.push_back({c.name, c.macro, c.place});
  }
  const auto               mesh = Region_mesh::build(d, macros(), ios(), region_options_with_tracks(tech, options.region), tech.dbu);
  auto                     held = session->pin_positions();
  std::vector<std::string> forced;
  std::optional<ph::Rect>  window;
  bool                     whole_core = false;
  for (const auto& [name, pos] : edits) {
    const auto it = v.cell.find(name);
    if (it == v.cell.end() || int(it->second) >= d.num_movable) {
      continue;
    }
    auto&        c  = v.cells[it->second];
    const double ox = c.place.x + c.place.width / 2., oy = c.place.y + c.place.height / 2.;
    const double nx = pos.first + c.place.width / 2., ny = pos.second + c.place.height / 2.;
    const auto   ro = mesh.rect_of(ox, oy), rn = mesh.rect_of(nx, ny);
    if (!ro || !rn || *ro != *rn) {
      whole_core = true;  // crossed a region edge: regenerate every region
    } else {
      const auto&    r = mesh.rects[size_t(*rn)];
      const ph::Rect rect{r.xl, r.yl, r.xh, r.yh};
      window = window ? window->hull(rect) : rect;
    }
    c.place.x = std::llround(pos.first);
    c.place.y = std::llround(pos.second);
    forced.push_back(name);
    held[name] = pos;
  }
  for (const auto& c : v.cells) {  // pins that were not edited stay where the placement left them
    if (!std::ranges::contains(forced, c.name)) {
      if (auto it = held.find(c.name); it != held.end()) {
        it->second = {double(c.place.x), double(c.place.y)};
      }
    }
  }
  if (forced.empty()) {
    return false;
  }
  Eco_stats eco;
  try {
    local_place(v, tech, options, reference, forced, eco, whole_core || !window ? nullptr : &*window, &held);
    auto frame = v.fp;
    frame.components.clear();
    for (const auto& c : v.cells) {
      frame.components.push_back({c.name, c.macro, c.place});
    }
    check_legality(tech, frame, options);
    std::map<std::string, std::array<std::int64_t, 3>> legal;
    for (const auto& c : v.cells) {
      legal.emplace(c.name, std::array<std::int64_t, 3>{c.place.x, c.place.y, static_cast<std::int64_t>(c.place.orientation)});
    }
    session->set_placement(std::move(legal));
    session->set_note(std::format("edit applied to {} ({} moved, {} attempt(s))", whole_core ? "the whole core" : "its region", eco.moved, eco.attempts));
    return true;
  } catch (const std::exception& error) {
    std::map<std::string, Ann_place> before;  // put every cell back; the failed edit's pins are released
    for (const auto& c : reference.components) {
      before.emplace(c.name, c.place);
    }
    for (auto& c : v.cells) {
      // a cell the failed edit added (a buffer, a tie) has no earlier place; it keeps the one it has
      if (const auto it = before.find(c.name); it != before.end()) {
        c.place = it->second;
      }
      c.locked = false;
    }
    const auto json_text = [](std::string_view text) {  // a Verilog escaped identifier may hold a quote or a backslash
      std::string out;
      for (const char ch : text) {
        if (ch == '"' || ch == '\\') {
          out += '\\';
        }
        out += ch;
      }
      return out;
    };
    std::string list = "{\"cmd\":\"unpin\",\"nodes\":[";
    for (size_t k = 0; k < forced.size(); ++k) {
      list += std::format("{}\"{}\"", k ? "," : "", json_text(forced[k]));
    }
    session->submit(list + "]}");
    session->set_note(std::string("edit not applied: ") + error.what());
    make_arrays(v, tech);
    return false;
  }
}

void gui_edit_mode(View& v, const ph::Technology& tech, const Options& options, const Result& result, const std::function<std::vector<int>()>& macros,
                   const std::function<std::vector<Region_io>()>& ios) {
  auto session = options.gui;
  if (!session || v.fp.rows.empty()) {
    return;
  }
  if (session->edit_left()) {  // a later pass: the user already edited; reproduce the edits on this pass's placement
    if (const auto recorded = session->recorded_edits(); !recorded.empty()) {
      gui_apply_edits(v, tech, options, recorded, macros, ios);
    }
    return;
  }
  for (int round = 0; round < 10000; ++round) {
    make_arrays(v, tech);
    auto& d = v.design;
    session->begin_phase(gui_scene(v, tech, "edit"), d.node_x.data(), d.node_y.data(), d.node_x.size(), false);
    Gui_map_env env;
    env.bins = 32;
    gui::Stats st;
    st.hpwl_um = gui_hpwl_um(d, d.node_x.data(), d.node_y.data(), tech.dbu);
    const gui::Map_provider maps = [&](const std::string& k) { return gui_map(k, d, d.node_x.data(), d.node_y.data(), d.node_x.size(), env); };
    const auto timing = gui_timing_provider(&v, &tech, &options, &result, &d, d.node_x.data(), d.node_y.data(), d.node_x.size());
    const auto waited = session->edit_wait(d.node_x.data(), d.node_y.data(), d.node_x.size(), st, &maps, &timing);
    if (!waited.again) {
      return;
    }
    Named_edits edits;
    for (const auto& pin : waited.edits.pins) {
      if (pin.node >= 0 && size_t(pin.node) < v.cells.size() && pin.node < d.num_movable) {
        edits.push_back({v.cells[size_t(pin.node)].name, {pin.x, pin.y}});
      }
    }
    if (gui_apply_edits(v, tech, options, edits, macros, ios)) {
      session->record_edits(edits);
    }
  }
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

void check_legality(const ph::Technology& tech, const ph::Floorplan& fp, double halo_x, double halo_y, double channel_x,
                    double channel_y) {
  if (!std::isfinite(halo_x) || !std::isfinite(halo_y) || halo_x < 0 || halo_y < 0) {
    throw std::invalid_argument("invalid macro halo");
  }
  if (!std::isfinite(channel_x) || !std::isfinite(channel_y) || channel_x < 0 || channel_y < 0) {
    throw std::invalid_argument("invalid macro channel");
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
    check_legality(tech, sized, halo_x, halo_y, channel_x, channel_y);
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
  if (std::any_of(fp.components.begin(), fp.components.end(), [&](const auto& c) {
        return ph::soft_block(tech.macros.at(c.macro));
      })) {
    // Soft blocks (LEF CLASS BLOCK SOFT) only have to be placed inside the core:
    // they may overlap each other, macros and cells, and take no halo or channel.
    auto hard = fp;
    std::erase_if(hard.components, [&](const auto& c) {
      if (!ph::soft_block(tech.macros.at(c.macro))) {
        return false;
      }
      const auto& p = c.place;
      if (p.status == Status::UNPLACED || p.x < xl || p.y < yl || p.x + p.width > xh || p.y + p.height > yh) {
        throw std::runtime_error("soft block outside core/unplaced: " + c.name);
      }
      return true;
    });
    check_legality(tech, hard, halo_x, halo_y, channel_x, channel_y);
    return;
  }
  if (channel_x > 0 || channel_y > 0) {
    // Every pair of non-CORE bodies keeps the channel clear on at least one axis (touching is a violation).
    const auto                        cx = channel_dbu(channel_x, tech), cy = channel_dbu(channel_y, tech);
    std::vector<const ph::Component*> macros;
    for (const auto& c : fp.components) {
      if (ph::placement_mask(c, tech) == ph::Placement_mask::none && !tech.macros.at(c.macro).klass.starts_with("CORE")) {
        macros.push_back(&c);
      }
    }
    std::sort(macros.begin(), macros.end(), [](auto a, auto b) {
      return std::pair{a->place.x, a->name} < std::pair{b->place.x, b->name};
    });
    for (size_t i = 0; i < macros.size(); ++i) {
      const auto& a = macros[i]->place;
      for (size_t j = i + 1; j < macros.size() && macros[j]->place.x < a.x + a.width + cx; ++j) {
        const auto& b = macros[j]->place;
        if (b.y < a.y + a.height + cy && a.y < b.y + b.height + cy) {
          throw std::runtime_error(
              std::format("macros closer than the {}x{} DBU macro channel: {}, {}", cx, cy, macros[i]->name, macros[j]->name));
        }
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
    resolved.fp_auto     = false;
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
  const auto resolved = final_cell_options(options);
  if (resolved.legroute_options.critical_cellboxes && resolved.legroute_plan.enabled) {
    check_critical_plan_frame(tech, result.floorplan, resolved);
  } else if (deferred_legroute_cells(resolved)) {
    check_fixed_and_macro_legality(tech, result.floorplan, resolved);
  } else {
    check_legality(tech, result.floorplan, resolved);
  }
}
void check_critical_plan_frame(const ph::Technology& tech, const ph::Floorplan& fp, const Options& options) {
  if (!options.legroute_options.critical_cellboxes || !options.legroute_plan.enabled) {
    throw std::invalid_argument("raw Plan validation requires critical cellboxes");
  }
  // This checks the native rows, die, fixed CORE objects, macros and their
  // overlap before permitting any unlegalized movable CORE geometry.
  check_fixed_and_macro_legality(tech, fp, options);
  const int64_t rh = std::llround(tech.sites.at(fp.rows.front().site).height * tech.dbu);
  int64_t       xl = std::numeric_limits<int64_t>::max(), yl = xl;
  int64_t       xh = std::numeric_limits<int64_t>::min(), yh = xh;
  for (const auto& row : fp.rows) {
    xl = std::min(xl, row.x);
    yl = std::min(yl, row.y);
    xh = std::max(xh, int64_t(row.x + row.count * row.step));
    yh = std::max(yh, row.y + rh);
  }
  for (const auto& cell : fp.components) {
    const auto& macro = tech.macros.at(cell.macro);
    if (!macro.klass.starts_with("CORE") || cell.place.status == Status::FIXED || cell.place.status == Status::LOCKED) {
      continue;
    }
    auto width = std::llround(macro.width * tech.dbu), height = std::llround(macro.height * tech.dbu);
    if (ph::swaps_axes(cell.place.orientation)) {
      std::swap(width, height);
    }
    if (width <= 0 || height <= 0 || (cell.place.width && cell.place.width != width)
        || (cell.place.height && cell.place.height != height)) {
      throw std::runtime_error("raw Plan cell dimensions differ from LEF: " + cell.name);
    }
    if (cell.place.x < xl || cell.place.y < yl || int64_t(cell.place.x) + width > xh || int64_t(cell.place.y) + height > yh) {
      throw std::runtime_error("raw Plan cell lies outside the fixed core: " + cell.name);
    }
  }
}
void check_legality(const ph::Technology& tech, const ph::Floorplan& fp, const Options& requested) {
  const auto options = resolve_macro_halo(requested, fp.rows.empty() ? 0 : tech.sites.at(fp.rows.front().site).height);
  check_macro_halo(tech, fp, options);
  const auto [x, y]    = effective_halo(options);
  const auto [cx, cy]  = macro_legality_channel(tech, component_masters(fp), requested);
  const auto channel_x = double(cx) / tech.dbu, channel_y = double(cy) / tech.dbu;
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
    check_legality(tech, logical, x, y, channel_x, channel_y);
    check_legality(tech, fp, 0, 0);
  } else {
    check_legality(tech, fp, x, y, channel_x, channel_y);
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
  // A frame this run does not size (a fixed core, an imported floorplan) may need its GP target density resolved too.
  if (requested.halo_x == -1 || requested.halo_y == -1 || !requested.die.empty() || !requested.core.empty()
      || !requested.floorplan.empty()) {
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
    if (requested.gp_rank_trees) {
      automatic_options->gp_rank_trees = [&requested](const Trees_view& state) { return requested.gp_rank_trees(state); };
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
      || options.fp_timing_max_updates < 2 || !std::isfinite(options.timing_min_weight) || options.timing_min_weight <= 0
      || options.timing_min_weight > 1
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
  if (macro_active && options.gui) {
    options.gui->set_note("macro=h2d places macros and cells in one native solver run: there are no fp/macro/final GP gates here, "
                          "only edit mode on its result. Use the default macro=none flow to stop and move macros in the macro phase.");
  }
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
    pg_profile                             = power_grid_profile(*tech_profile);
    pg_profile->channels                   = options.pg_channels;
    pg_profile->channel_max_ir_drop_v      = options.pg_channel_max_ir_drop_v;
    pg_profile->tolerate_macro_pin_overlap = options.pg_tolerate_macro_pin_overlap;
  }
  if (macro_active && options.metadata && !options.metadata->select("livehd.placement.hints", source.get_name()).empty()) {
    livehd::diag::warn("pass.place", "placement-hint-mode", "unsupported")
        .msg("placement metadata hints are supported by native FP/macro GP, not the experimental h2d solver; hints ignored")
        .emit();
  }
  const auto hint_scopes
      = options.metadata && !macro_active && !options.timing_seed && !options.retain_placement && !options.metadata_finalized
            ? (options.metadata_scopes ? *options.metadata_scopes : placement_hint_scopes(source, tech))
            : std::vector<Hint_scope>{};
  const std::string              hint_design(source.get_name());
  std::optional<Placement_hints> placement_hints;
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
  // The flat netlist takes the source name in the caller's library. Every
  // placement owns a fresh library; a reused one would keep the stale name.
  if (library.find_io(source.get_name())) {
    throw std::invalid_argument(
        std::format("placement library already holds '{}': each placement needs its own library", source.get_name()));
  }
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
  auto weight_roles       = prects_library(options.technology().cells, options.control_ports);
  weight_roles.reset_is_data = pcrs_active(options);
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
  result.prects             = v.prects;
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
  const bool use_pcrs   = pcrs_active(options);
  pcrs::Stats pcrs_stats;
  if (use_pcrs) {
    // A tree built before the first placement groups its sinks by instance name. The first call that starts
    // from a placement regroups them by position, once: a reset hop then spans neighbours, not the die.
    std::unordered_map<std::string_view, std::pair<double, double>> placed_at;
    if (const auto* seed = options.previous ? options.previous.get() : options.restart_seed.get()) {
      placed_at.reserve(seed->components.size());
      for (const auto& c : seed->components) {
        if (c.place.status != Status::UNPLACED) {
          placed_at.emplace(c.name, std::pair{double(c.place.x), double(c.place.y)});
        }
      }
    }
    const pcrs::Position_of position_of = [&](std::string_view instance) -> std::optional<std::pair<double, double>> {
      const auto found = placed_at.find(instance);
      return found == placed_at.end() ? std::nullopt : std::optional{found->second};
    };
    // A pass with no GP iterations (final legalization, region refinement) cannot place a new tree: whatever the
    // depth state says, a tree in the graph stays as it is there.
    const bool frozen  = !placed_at.empty() && options.iterations == 0;
    const bool regroup = !frozen && !placed_at.empty() && options.pcrs_depth && !options.pcrs_depth->clustered;
    pcrs::Depth_policy depth_of = options.pcrs_depth ? options.pcrs_depth->policy(options.reset_fanout) : pcrs::Depth_policy{};
    if (frozen && depth_of) {
      depth_of = [wanted = std::move(depth_of)](std::string_view domain, size_t sinks, std::optional<unsigned> current) {
        return current ? *current : wanted(domain, sinks, current);
      };
    }
    // The tree's cells are sized by pcrs for the pins and the wire they drive (positions are in DBU).
    const auto   reset_wire = technology_wire_rc(tech,
                                                 options.liberty,
                                                 options.tech_rc_file,
                                                 options.resistance_ohm_um,
                                                 options.capacitance_ff_um,
                                                 options.rc_pessimism,
                                                 options.technology().cells.contents);
    const double reset_c    = std::max(reset_wire.horizontal_c, reset_wire.vertical_c) / tech.dbu;
    pcrs_stats = pcrs::build_reset(graph,
                                   tech,
                                   options.technology().cells,
                                   {.fanout = options.reset_fanout, .regroup = regroup, .wire_ff_per_unit = reset_c},
                                   depth_of,
                                   placed_at.empty() ? pcrs::Position_of{} : position_of);
    if (regroup) {
      options.pcrs_depth->clustered = true;
    }
    result.pcrs_stats = std::make_shared<const pcrs::Stats>(pcrs_stats);
  }
  const auto reset_tree
      = options.reset_fanout && !use_pcrs
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
  double      area      = 0;
  int64_t     max_width = 0, max_height = 0;
  std::string site_name;
  gu::Placement_block::Writer placement_blocks(&graph);  // for this loop only: flushed right after it
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
      // A cell keeps the block it already has; a new one gets its instance prefix and color. The node holds an
      // id and the graph each block text once (graph/shared_text.hpp).
      auto       block_id = gu::Placement_block::id_of(node);
      if (block_id == 0) {
        block_id = placement_blocks.id(std::format("{}:{}", prefix, gu::node_color_of(node)));
        placement_blocks.set(node, block_id);
      }
      const auto block = placement_blocks.text(block_id);
      if (options.cohesion > 0 || v.align_macros) {
        if (gu::Color_group::has(node)) {
          v.cohesion_owners.emplace(instance, gu::Color_group::text(node));
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
  placement_blocks.flush();
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
    Def_name_binding cells, ports;
    for (const auto& cell : v.cells) {
      cells.add(cell.name);
    }
    const auto io        = graph.get_io();
    const auto add_ports = [&](const auto& declarations) {
      for (const auto& d : declarations) {
        for (int b = 0; b < std::max(1, static_cast<int>(d.bits)); ++b) {
          ports.add(bit_name(d.name, std::max(1, static_cast<int>(d.bits)), b));
        }
      }
    };
    add_ports(io->get_input_pin_decls());
    add_ports(io->get_output_pin_decls());
    for (auto& component : imported_frame->components) {
      component.name = cells.resolve(component.name);
    }
    for (auto& terminal : imported_frame->terminals) {
      terminal.name = ports.resolve(terminal.name);
    }
    for (auto& net : imported_frame->nets) {
      for (auto& connection : net.connections) {
        if (connection.instance == "PIN") {
          connection.pin = ports.resolve(connection.pin);
        } else {
          connection.instance = cells.resolve(connection.instance);
        }
      }
    }
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
    if (options.boundarypins.enabled) {
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
  v.halo_x = macro_halo(cell_halo_x, tech.dbu, sw);
  v.halo_y = macro_halo(cell_halo_y, tech.dbu, rh);
  {
    std::set<std::string> masters;
    for (const auto& c : v.cells) {
      masters.insert(c.macro);
    }
    // Macro spacing never takes the final-cell halo override: macro-to-macro clearance keeps the macro halo.
    v.pdn_rules       = macro_pdn_rules(tech, masters, options);
    const auto metal  = macro_metal_channel(tech, masters, options);
    const auto levels = !v.pdn_rules || options.macro_pdn == "off" ? std::vector{Pg_macro_level::off}
                        : options.macro_pdn == "auto" ? std::vector{Pg_macro_level::gap, Pg_macro_level::align, Pg_macro_level::off}
                                                      : std::vector{requested_pdn_level(options)};
    v.pdn_ladder.clear();
    for (const auto level : levels) {
      const auto [x, y] = macro_channel_at(tech, metal, v.pdn_rules, level);
      if (v.pdn_ladder.empty() || v.pdn_ladder.back().channel_x != x || v.pdn_ladder.back().channel_y != y) {
        v.pdn_ladder.push_back({level, x, y});
      }
    }
    v.pdn_attempts.clear();
    v.pdn_selected = false;
    set_pdn_level(v, 0);
    if (options.macro_pdn_origin) {
      v.pdn_origin = std::array<int64_t, 2>{std::llround(options.macro_pdn_origin->first * tech.dbu),
                                            std::llround(options.macro_pdn_origin->second * tech.dbu)};
    }
  }
  if (options.macro_edge_snap) {
    // Explicit microns now; auto (-1) resolves against the placement core when the snap runs.
    v.edge_snap_x = options.macro_edge_snap->first < 0 ? -1 : channel_dbu(options.macro_edge_snap->first, tech);
    v.edge_snap_y = options.macro_edge_snap->second < 0 ? -1 : channel_dbu(options.macro_edge_snap->second, tech);
  }
  // Padded macro occupancy: each side reserves the halo, or half the channel when that is larger.
  const auto pad_x          = std::max(v.halo_x, (v.channel_x + 1) / 2);
  const auto pad_y          = std::max(v.halo_y, (v.channel_y + 1) / 2);
  double     occupancy_area = area;
  double     macro_area     = 0;  // hard macros, unpadded
  // A fixed core must hold every macro with the weakest channel macro.pdn may use (auto degrades); a generated core
  // is sized for the requested one.
  const auto must_pad_x     = std::max(v.halo_x, (v.pdn_ladder.back().channel_x + 1) / 2);
  const auto must_pad_y     = std::max(v.halo_y, (v.pdn_ladder.back().channel_y + 1) / 2);
  auto       must_width = max_width, must_height = max_height;
  for (const auto& c : v.cells) {
    if (ph::hard_macro(tech.macros.at(c.macro))) {
      must_width       = std::max(must_width, c.place.width + 2 * must_pad_x);
      must_height      = std::max(must_height, c.place.height + 2 * must_pad_y);
      max_width       = std::max(max_width, c.place.width + 2 * pad_x);
      max_height      = std::max(max_height, c.place.height + 2 * pad_y);
      occupancy_area += double(c.place.width + 2 * pad_x) * (c.place.height + 2 * pad_y) - double(c.place.width) * c.place.height;
      macro_area     += double(c.place.width) * c.place.height;
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
                                 || options.cache_dir.empty() || options.metadata)
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
      if (pg_profile) {
        // Keep the same usable core area, but reserve a site/row of die margin
        // for physical edge rails and via enclosures. Explicit frames are never
        // resized and must supply their own inset.
        origin_x = sw;
        origin_y = rh;
      }
      // IO resynthesis may reduce mapped area after an area-resume slice has
      // advanced the retained optimizer. Regenerating solely from that smaller
      // area would shrink its frame. Keep each generated extent monotonic;
      // imported/explicit frames below still follow their original contract.
      const bool has_movable_macro = std::ranges::any_of(v.cells, [&](const auto& cell) {
        return !cell.locked && cell.place.status != Status::FIXED && cell.place.status != Status::LOCKED
               && !tech.macros.at(cell.macro).klass.starts_with("CORE");
      });
      if (options.legroute_plan.enabled && options.placement_session && options.completed_stage == Placement_stage::final
          && !options.fp_place && !has_movable_macro && !options.timing_seed && !options.retain_placement
          && !options.analyze_colors_only) {
        if (const auto retained = options.placement_session->current_layout()) {
          if (retained->xl != origin_x || retained->yl != origin_y || retained->site_width != sw || retained->row_height != rh) {
            throw std::runtime_error("retained generated placement requires unchanged core origin and site/row lattice");
          }
          columns = std::max(columns, int64_t(std::ceil((retained->xh - origin_x) / sw)));
          rows    = std::max(rows, int64_t(std::ceil((retained->yh - origin_y) / rh)));
        }
      }
      v.fp.die = {0, 0, double(columns * sw + 2 * origin_x), double(rows * rh + 2 * origin_y)};
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
      if (must_width > columns * sw || must_height > rows * rh) {
        throw std::runtime_error("a mapped cell or macro halo does not fit in the fixed core");
      }
    }
    // Alternating N/FS rows require X-symmetric row cells, as in sky130HD. A block
    // macro (a memory, a hard macro) never sits in a row, so it does not decide
    // the flip: all-N rows would leave every two-row cell without shared rails.
    const bool flip = std::all_of(v.cells.begin(), v.cells.end(), [&](const auto& c) {
      const auto& m = tech.macros.at(c.macro);
      return !m.klass.starts_with("CORE") || m.symmetry_x;
    });
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
        // a two-row cell's rail twin (the frame's rows decided it in an earlier stage): the same cell, taken as is
        const auto t = options.multirow_templates.find(c.macro);
        if (t == options.multirow_templates.end() || t->second.rail_twin != imported.macro) {
          // pass.pcrs names a tree cell after what it drives and sizes it for the placement it is given: the
          // same cell may come back one size up or down. The netlist's master is the one that counts.
          if (pcrs::instance_leaf(c.name).starts_with("__pcrs_rst")) {
            continue;
          }
          throw std::runtime_error("floorplan cell master mismatch: " + c.name);
        }
        take_rail_twin(v, size_t(&c - v.cells.data()), imported.macro);
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
  if (automatic_options && (!options.die.empty() || !options.core.empty() || !options.floorplan.empty())) {
    // A frame this run did not size can be as full as the requested GP target density, or fuller: the overflow goal
    // is then out of reach, GP spends its iterations spreading and never trades density for wirelength. Target what
    // the frame holds plus a quarter of what it has left (the ORFS lower-bound-plus-addon rule). A sparse frame keeps
    // any target, however low.
    double row_area = 0;
    for (const auto& row : v.fp.rows) {
      row_area += double(row.count) * sw * rh;
    }
    const double free_area = row_area - (occupancy_area - area + macro_area);
    if (free_area > 0) {
      const double held = std::min(1., (area - macro_area) / free_area);
      if (options.density < 1.15 * held) {
        const double raised = held + .25 * (1 - held);
        livehd::diag::info("pass.place", "place-density", "progress")
            .msg("synth.place.density={:.3f} is too low for a fixed frame holding {:.3f}; using {:.3f}", options.density, held, raised)
            .emit();
        automatic_options->density = raised;
        // the legalizer's bin cap stays above the GP target, as it must
        automatic_options->terminal_legalize_options.max_density
            = std::max(options.terminal_legalize_options.max_density, std::min(1.0, raised + .05));
      }
    }
  }
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
  if (!options.seed_file.empty()) {  // initial positions only: nothing here is fixed
    std::map<std::string, std::pair<int64_t, int64_t>> seeds;
    std::istringstream                                 lines(file_text(options.seed_file));
    for (std::string line; std::getline(lines, line);) {
      std::istringstream f(line);
      std::string        name;
      int64_t            x = 0, y = 0;
      if (std::getline(f, name, '\t') && (f >> x >> y)) {
        seeds.emplace(std::move(name), std::pair{x, y});
      }
    }
    for (auto& c : v.cells) {
      // synthesis wraps the cells of a read-back netlist in generated modules (`__flat___sub_16.<instance>`)
      auto it = seeds.find(c.name);
      if (it == seeds.end() && c.name.starts_with("__flat_")) {
        if (const auto dot = c.name.find('.'); dot != std::string::npos) {
          it = seeds.find(c.name.substr(dot + 1));
        }
      }
      if (it != seeds.end() && !c.locked && c.place.status != Status::FIXED && c.place.status != Status::LOCKED) {
        c.place.x = it->second.first;
        c.place.y = it->second.second;
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
  if (uroute_requested || resolved_uroute.export_features || options.legroute_plan.export_coarse) {
    uroute_tech = uroute_technology(options.lefs, proxy_lef);
  }
  std::unordered_map<std::string, ph::Terminal> imported_terms;
  for (auto& t : v.fp.terminals) {
    imported_terms.emplace(t.name, std::move(t));
  }
  v.fp.terminals.clear();
  v.fp.components.clear();
  if (options.boundarypins.enabled) {
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
  // A large design can need thousands of single-load ties. Index existing
  // cells once instead of scanning the whole design for each source/user.
  // Owning names and vector indices stay valid when new ties grow v.cells;
  // make_arrays rebuilds this same index after sorting the completed cells.
  std::map<std::pair<std::string_view, std::string_view>, const ph::Component*> previous_ties;
  if (!constant_nets.empty()) {
    for (size_t i = 0; i < v.cells.size(); ++i) {
      v.cell.try_emplace(v.cells[i].name, i);
    }
    if (previous) {
      for (const auto& old : previous->components) {
        if (v.constant_masters.contains(old.macro)) {
          previous_ties.insert_or_assign({old.name, old.macro}, &old);
        }
      }
    }
  }
  for (const auto& [constant_driver, sinks] : constant_nets) {
    const bool  imported = constant_driver.constant < 0;
    const int   value    = imported ? int(characterized_ties.at(constant_driver)) : constant_driver.constant;
    std::string master_name, output;
    if (imported) {
      master_name = v.cells.at(v.cell.at(constant_driver.instance)).macro;
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
      const auto  base     = imported ? constant_driver.instance.str() : std::string(value ? "__place_tiehi" : "__place_tielo");
      const auto  name = imported && i == 0 ? base : std::format("{}_{:016x}", base, hash_text(target.instance + ":" + target.pin));
      const auto  existing = v.cell.find(name);
      Node        node;
      if (existing != v.cell.end()) {
        const auto& cell = v.cells.at(existing->second);
        if (cell.macro != master_name) {
          throw std::runtime_error("tie cell master mismatch: " + name);
        }
        node = cell.node;
      } else {
        node = graph.create_node();
        gu::set_type_op(node, Ntype_op::Sub);
        node.set_subnode(tie_io);
        node.set_name(name);
        const auto& master = tech.macros.at(master_name);
        Ann_place   place(0, 0, std::llround(master.width * tech.dbu), std::llround(master.height * tech.dbu));
        if (const auto old = previous_ties.find({name, master_name}); old != previous_ties.end()) {
          place = old->second->place;
        }
        v.cell.emplace(name, v.cells.size());
        v.cells.push_back({node, name, master_name, "__ties", place});
      }
      auto driver = node.create_driver_pin(output);
      gu::set_bits(driver, 1);
      tie_drivers.emplace(target, driver);
      v.nets[{name, output, -1}].push_back(target);
    }
  }
  previous_ties.clear();
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
      assign_io(v, tech, options);
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
      macro_report_info.push_back(
          {c.name, parent, c.macro, ph::hard_macro(m), hierarchy->sequential_masters.contains(c.macro), false, stable, mask});
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
    const bool track_moved = align_macro_geometry(v, tech, v.design, true, false, options.legroute);
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
          if (ph::soft_block(tech.macros.at(c.macro))) {
            continue;  // a soft block may overlap macros
          }
          // Body spacing: the halo against cells, max(channel, both halos) against another macro.
          const bool standard = tech.macros.at(c.macro).klass.starts_with("CORE");
          const auto sx       = standard ? v.halo_x : std::max(v.channel_x, 2 * v.halo_x);
          const auto sy       = standard ? v.halo_y : std::max(v.channel_y, 2 * v.halo_y);
          if (&c != macro && (c.place.status == Status::FIXED || c.place.status == Status::LOCKED)
              && x < c.place.x + c.place.width + sx && x + macro->place.width + sx > c.place.x
              && y < c.place.y + c.place.height + sy && y + macro->place.height + sy > c.place.y) {
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
    const auto pdn     = macro_pdn_alignment(v, tech);
    bool       aligned = false;
    legalize_macro_tracks(tech, v.fp, frame, v.halo_x, v.halo_y, true, v.channel_x, v.channel_y, pdn ? &*pdn : nullptr, &aligned);
    if (pdn) {
      ++v.pdn_alignments;
      v.pdn_fallbacks += !aligned;
    }
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
  // The hash of every block matters only to a placement cache that exists; without one only the number of blocks is read.
  const auto hashes = [&] {
    if (!cache_path.empty()) {
      return block_hashes(v);
    }
    std::map<std::string, uint64_t> blocks;
    for (const auto& c : v.cells) {
      blocks.emplace(c.block, 0);
    }
    return blocks;
  }();
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
      if (options.gui) {  // 2i-interact: the edit mode's legal placement supersedes the seed captured before it
        for (const auto& [name, p] : options.gui->placement_override()) {
          if (const auto it = positions.find(name); it != positions.end()) {
            it->second.x           = p[0];
            it->second.y           = p[1];
            it->second.orientation = static_cast<Ann_place::Orientation>(p[2]);
          }
        }
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
  const auto hint_objects = [&] {
    std::vector<Hint_object> objects;
    objects.reserve(v.cells.size());
    for (const auto& c : v.cells) {
      const auto& m = tech.macros.at(c.macro);
      objects.push_back({c.name, c.macro, m.symmetry_x, m.symmetry_y});
    }
    return objects;
  };
  dp::Options control;
  double      feature_capacity_h = 0, feature_capacity_v = 0;
  control.max_iterations  = options.iterations;
  control.centroid_weight = v.centroids ? .25 : 0;
  // Fail early only where the final track check will run; timing seeds and
  // retained placements do not certify legality.
  if (options.full && !options.timing_seed && !options.retain_placement && !options.analyze_colors_only) {
    align_macro_geometry(v, tech, v.design, false, false);
  }
  // macro.anchor target runs (macro_anchor.cpp) reuse this chain on a design copy; only their late refreshes run
  // the (slow) dead-space collapse.
  bool                             anchor_skip_dead_space = false;
  std::function<bool(dp::Design&)> anchor_final_override;  // macro.anchor.final_targets (final hard pass)
  control.after_macro_legalize = [&](dp::Design& physical) {
    if (anchor_final_override && anchor_final_override(physical)) {
      return;  // the last anchor targets already ran this chain
    }
    snap_macros_to_core_edges(v, tech, physical);
    if (options.macro_dead_space && options.macro_dead_space->collapse && !anchor_skip_dead_space) {
      collapse_macro_dead_space(v, tech, physical, *options.macro_dead_space, static_cast<int>(options.threads));
    }
    align_macro_geometry(v, tech, physical, true, false, options.legroute);
  };
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
  // Taps are inserted by the terminal legalization alone. A call with optimization pending (first placement,
  // timing round, resize/repair trial) is followed by finalize_placement, which inserts the taps in its fresh
  // final solve. It legalizes natively unless movable legroute owns cell legalization; other non-fresh calls are refused.
  if (options.handoff_taps && !options.timing_seed && !options.retain_placement && options.completed_stage == Placement_stage::final
      && (!options.full || macro_active || (options.previous && !options.optimization_pending))) {
    throw std::invalid_argument("handoff taps require fresh final terminal legalization with fixed macros");
  }
  const bool terminal_active   = options.full && !options.optimization_pending && !options.fp_place
                                 && options.completed_stage == Placement_stage::final && !options.timing_seed
                                 && !options.retain_placement && !options.analyze_colors_only;
  // Optional centroid FP still feeds this macro stage. It must not enable
  // cell legalization or discard the selected GP timing used by resynthesis.
  const bool defer_macro_cells = options.full && !options.macro_legalize_cells && options.completed_stage == Placement_stage::macro
                                 && !options.timing_seed && !options.retain_placement && !options.analyze_colors_only;
  const bool defer_legroute_cells = deferred_legroute_cells(options);
  const bool defer_cells          = defer_macro_cells || defer_legroute_cells;
  // Soft blocks keep their GP pose; no legalizer packs them into rows. Macro
  // planning must defer cell legalization; the final cell stage (fixed hard
  // macros, terminal legalization) moves them in GP like cells of their area
  // and then legalizes only the standard cells around them.
  if (options.full && !defer_macro_cells && !options.timing_seed && !options.retain_placement && !options.analyze_colors_only
      && (!terminal_active || options.legroute || movable_macros)
      && std::ranges::any_of(v.cells, [&](const Cell& c) { return ph::soft_block(tech.macros.at(c.macro)); })) {
    throw std::invalid_argument(
        "soft blocks (LEF CLASS BLOCK SOFT) are supported through macro planning with deferred cell legalization "
        "(macro_only / completed_stage=macro, macro_legalize_cells=false) and the staged final cell pass without legroute");
  }
  if (terminal_active && movable_macros) {
    throw std::invalid_argument("terminal legalization requires the final cell stage with fixed macros");
  }
  // While optimization is pending (timing-driven placement, resize, repair follow) only the dedicated
  // refine_regions call refines: it runs once on the timing-driven result, then timing gets a chance to react.
  const bool refine_here       = !options.optimization_pending || options.region_refine;
  const bool boundary_active   = options.boundarypins.enabled && options.full && !options.fp_place && refine_here
                                 && !(options.placement_area_resume && options.legroute_plan.enabled)
                                 && options.completed_stage == Placement_stage::final && !options.timing_seed
                                 && !options.retain_placement && !options.analyze_colors_only;
  const bool refinement_active = boundary_active;
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
    // Optimization pending: the legalization is deferred to finalize_placement,
    // which packs the two-row cells; the optimization trials in between place
    // the edits they make through the two-row ECO.
    if (options.full && !terminal_active && !options.optimization_pending && !options.timing_seed && !options.retain_placement
        && !options.analyze_colors_only) {
      refuse("native legalization outside the final stage is not supported");
    }
    if (options.satlegalize) {
      refuse("satlegalize treats multi-height cells as immutable");
    }
    if ((options.boundarypins.enabled || options.boundarypins.align) && !options.legroute) {
      refuse("the detail-final region mesh (busorder, boundarypins) needs single-row cells");  // legroute places them
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
  auto* live_session = options.legroute_plan.enabled && options.completed_stage == Placement_stage::final && !options.fp_place
                               && !movable_macros && !options.timing_seed && !options.retain_placement
                               && !options.analyze_colors_only
                           ? options.placement_session.get()
                           : nullptr;
  dp::GpCheckpoint session_checkpoint;
  const auto       session_identities = [&] {
    std::vector<std::string> names(v.design.node_x.size());
    for (size_t i = 0; i < v.cells.size(); ++i) {
      const auto& cell = v.cells[i];
      // Macro master changes require a new identity; standard cells may resize
      // in place during timing repair while retaining their optimizer particle.
      names[i]         = tech.macros.at(cell.macro).klass.starts_with("CORE")
                             ? "cell:" + cell.name
                             : "macro:" + std::to_string(cell.name.size()) + ":" + cell.name + ":" + cell.macro;
    }
    for (size_t i = v.cells.size(); i < v.terminal_begin; ++i) {
      names[i] = "mask:" + std::to_string(i - v.cells.size());
    }
    for (const auto& [name, index] : v.terminal) {
      names[v.terminal_begin + index] = "io:" + name;
    }
    return names;
  };
  std::optional<Busorder>      bus_order;
  std::optional<Boundary_pins> boundary_pins;
  std::optional<Region_mesh>   final_regions;  // the detail-final region mesh (legroute's regions)
  // The boundary refinements settle the converged GP placement with more DREAMPlace iterations. They must start
  // from GP's converged density weight: restarted at initial_density_weight, wirelength collapses the spread
  // placement (overflow 5% -> 50%+ within 100 iterations).
  double                       gp_density_weight = 0;
  // the region mesh's inputs: the fixed macros (not placement masks) and the IO terminals
  const auto                   region_macros     = [&] {
    std::vector<int> macro_indices;
    for (size_t i = 0; i < v.cells.size(); ++i) {
      if (!tech.macros.at(v.cells[i].macro).klass.starts_with("CORE") && !v.cells[i].name.starts_with("__lhd_frame_mask_")) {
        macro_indices.push_back(i);
      }
    }
    return macro_indices;
  };
  const auto region_ios = [&] {
    std::vector<Region_io> ios;
    for (const auto& t : v.fp.terminals) {
      ios.push_back({t.name, double(t.x), double(t.y)});
    }
    return ios;
  };
  const auto refine_final_boundaries = [&](dp::Design& physical) {
    auto refine_control = control;
    if (live_session && session_checkpoint) {
      refine_control.checkpoint = session_checkpoint;
    }
    if (gp_density_weight > 0) {
      refine_control.density_weight_seed = gp_density_weight;
    }
    const auto           bus_macros = region_macros();
    const auto           ios        = region_ios();
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
    const auto regions = Region_mesh::build(physical, bus_macros, ios, region_options_with_tracks(tech, options.region), tech.dbu);
    regions.write(options.output_dir);
    final_regions = regions;
    if (regions.rects.size() < 2) {
      return;  // one rectangle: no region edge to put virtual pins on (bus order is on by default)
    }
    {
      // the real nets' estimated crossing order, before and after the refinement (boundarypins/rsmt_order)
      bus_order.emplace(physical,
                        regions,
                        identities,
                        router,
                        tech.dbu,
                        options.output_dir.empty() ? "" : options.output_dir + "/boundarypins/rsmt_order");
      bus_order->measure_density(refine_control);
      bus_order->observe(physical, "before");
      // pins spaced one 2x2 cell-box apart by default (twice the widest flop): bits stay separable for
      // region-local placement and routing; a crowded edge narrows it
      auto pin_options = options.boundarypins;
      if (pin_options.pitch_um <= 0) {
        double flop = 0;
        for (const auto& cell : v.cells) {
          const auto& macro = tech.macros.at(cell.macro);
          if (macro.klass.starts_with("CORE")
              && std::any_of(macro.pins.begin(), macro.pins.end(), [](const auto& p) { return p.second.use == "CLOCK"; })) {
            flop = std::max(flop, macro.width);
          }
        }
        pin_options.pitch_um = 2 * flop;
      }
      std::vector<std::string> drivers;
      for (const auto& [driver, sinks] : v.nets) {
        drivers.push_back(driver.instance == "PIN" ? driver.pin.str() : driver.instance + "/" + driver.pin);
      }
      boundary_pins.emplace(regions,
                            router,
                            tech.dbu,
                            pin_options,
                            options.output_dir.empty() ? "" : options.output_dir + "/boundarypins",
                            identities,
                            bus_macros,
                            std::move(drivers));
      if (live_session && options.legroute_plan.timing_interval) {
        refine_control.checkpoint_interval = int(options.legroute_plan.timing_interval);
      }
      // The identities (one text per node) are only read by a session.
      boundary_pins->refine(physical,
                            refine_control,
                            live_session,
                            live_session ? session_identities() : std::vector<std::string>{});
      boundary_pins->release_design();  // only the anchors and the report are read from here on
      bus_order->observe(physical, "after_gp");
    }
    if (control.observer) {
      control.observer(physical, dp::PlacementStage::global_placement, control.max_iterations);
    }
  };
  // The region refinement's mesh and bus-ordered virtual pins from an earlier placement call (options.region_seed_dir):
  // a terminal stage without its own refinement gives them to legroute. Pins map to this stage's nets by driver.
  struct Region_seed {
    Region_mesh                        mesh;
    std::vector<Legroute_boundary_pin> pins;
  };
  std::optional<Region_seed> region_seed;
  bool                       region_seed_failed = false;
  const auto                 load_region_seed   = [&]() -> const std::optional<Region_seed>& {
    if (region_seed || region_seed_failed || options.region_seed_dir.empty() || options.placement_area_resume) {
      return region_seed;
    }
    try {
      std::ifstream       mesh_file(options.region_seed_dir + "/region_mesh.json");
      std::string         text((std::istreambuf_iterator<char>(mesh_file)), std::istreambuf_iterator<char>());
      rapidjson::Document doc;
      doc.Parse(text.c_str());
      if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("rectangles") || !doc["rectangles"].IsArray()) {
        throw std::runtime_error("region_mesh.json unreadable");
      }
      std::vector<Region_rect> rects;
      for (const auto& r : doc["rectangles"].GetArray()) {
        const auto& b = r["bounds_dbu"];
        rects.push_back({b[0].GetDouble(), b[1].GetDouble(), b[2].GetDouble(), b[3].GetDouble(), r["initial_cells"].GetUint()});
      }
      std::map<std::string, size_t> by_driver;  // the net index is the nets' iteration order (the refinement's too)
      size_t                        index = 0;
      for (const auto& [driver, sinks] : v.nets) {
        by_driver.emplace(driver.instance == "PIN" ? driver.pin.str() : driver.instance + "/" + driver.pin, index++);
      }
      std::ifstream pins_file(options.region_seed_dir + "/boundarypins/pins.tsv");
      std::string   line;
      Region_seed   seed{Region_mesh::from_rects(std::move(rects)), {}};
      size_t        unmatched = 0;
      std::getline(pins_file, line);  // the header
      while (std::getline(pins_file, line)) {
        std::vector<std::string> col;
        for (size_t at = 0;;) {
          const size_t tab = line.find('\t', at);
          col.push_back(line.substr(at, tab == std::string::npos ? std::string::npos : tab - at));
          if (tab == std::string::npos) {
            break;
          }
          at = tab + 1;
        }
        if (col.size() < 8) {
          continue;
        }
        const auto it = by_driver.find(col[7]);
        if (it == by_driver.end()) {
          ++unmatched;
          continue;
        }
        seed.pins.push_back({it->second, std::stod(col[2]), std::stod(col[3])});
      }
      std::fprintf(stderr,
                   "legroute: region seed from %s: %zu rectangles, %zu virtual pins (%zu without a net here)\n",
                   options.region_seed_dir.c_str(),
                   seed.mesh.rects.size(),
                   seed.pins.size(),
                   unmatched);
      region_seed = std::move(seed);
    } catch (const std::exception& e) {
      std::fprintf(stderr,
                   "legroute: region seed %s unusable (%s); the mesh is rebuilt without virtual pins\n",
                   options.region_seed_dir.c_str(),
                   e.what());
      region_seed_failed = true;
    }
    return region_seed;
  };
  std::optional<Terminal_legalize_report> terminal_report;
  std::optional<Legroute_report>          legroute_report;
  // legroute's routed signal wires by driver (instance, pin), with the connections and cells they were routed for:
  // out.def keeps a net's wires only while its connections and cells are unchanged
  struct Legroute_net {
    std::vector<ph::Connection> connections;
    std::vector<ph::Wire>       wires;
  };
  std::map<std::pair<std::string, std::string>, Legroute_net> legroute_wires;
  std::map<std::string, ph::Component, std::less<>>           legroute_cells;
  ph::Route_width_rules                                       legroute_rules;      // the router's wire-width rules
  std::map<std::string, ph::Terminal>                         legroute_terminals;  // the IO pins its wires end on
  std::optional<Multirow_report>                              multirow_report;
  std::optional<Multirow_eco_report>                          multirow_eco_report;
  std::pair<unsigned, unsigned>                               multirow_eco_grid;  // its terminal bin grid
  std::vector<ph::Component>                                  handoff_cells;
  std::vector<ph::Row>                                        handoff_rows;
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
    metrics.legal = native_legal(v, tech, v.design);
    metrics.hpwl  = dp::hpwl(v.design);
  } else if (!cache.hit && previous && options.full && !defer_legroute_cells && !adjustable_io && !macro_pin_adjust
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
      assign_io(v, tech, options);
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
        assign_io(v, tech, options);
      }
      if (v.centroids) {
        v.cells = scratch_cells;
      }
      make_arrays(v, tech);
    }
    control.legalize            = options.full && !terminal_active && !defer_legroute_cells;
    control.macro_only_legalize = defer_macro_cells;
    control.detailed_place = options.full && !terminal_active && !defer_cells && (!movable_macros || options.macro_detailed_place);
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
              if (!defer_cells && stage != dp::PlacementStage::global_placement
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
            auto net_name = driver.instance == "PIN" ? driver.pin.str() : "snapshot_net_" + std::to_string(index++);
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
            return prects_data_timing(std::move(feedback), v.ignored);
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
                                             options.timing_min_weight, options.timing_max_weight);
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
              R"({{"iteration":{},"trigger":"{}","movement_bins":{},"timing_seconds":{},"weighted_nets":{},"sta_calls":{},"resize_proposed":{},"resize_accepted":{},"repair_accepted":{},"before_delay":{},"after_delay":{},"time_unit_ps":{},"ranking_only":{}}})",
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
              after.ranking_only)
                << '\n';
          if (!trace) {
            throw std::runtime_error("cannot write FP timing checkpoint report");
          }
          dp::GpUpdate update;
          update.replacement = v.design;
          if (after.wns) {
            update.timing_violation = std::max(0., -*after.wns);
            if (options.gui) {  // read-only: the numbers this checkpoint already computed
              options.gui->set_timing(*after.wns * after.time_unit_ps, after.tns ? *after.tns * after.time_unit_ps : 0., iteration);
            }
          }
          for (const auto& c : v.cells) {
            const auto old = baseline.cell.find(c.name);
            update.previous_node.push_back(old == baseline.cell.end() ? -1 : int(old->second));
          }
          append_previous_boundary(update.previous_node, v, baseline);
          return update;
        };
      }
      if (options.metadata && !hint_scopes.empty()) {
        placement_hints.emplace(options.metadata, hint_design, hint_scopes, hint_objects(), tech.dbu);
        placement_hints->seed(v.design);
        placement_hints->attach(fp_control, hint_objects);
      }
      gui_attach(fp_control, options, v, tech, result, "fp");
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
    std::shared_ptr<Tree_cache>  tree_cache;  // the wire trees' identity, for the life of this placement
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
        const bool ranking     = options.gp_rank && !trial_scoring && !options.legroute_options.critical_cellboxes;
        if (!ranking) {
          snapshot_frame(checkpoint.floorplan, geometry);
        }
        auto rc_options = options;
        rc_options.full = false;
        fs::create_directories(options.output_dir);
        // Ordinary ranking consumes RC, not a duplicate string-keyed map of
        // every pin position. The selected macro handoff still owns that map.
        auto* pin_locations = !ranking || save_macro_gp_timing ? &checkpoint.pin_locations : nullptr;
        // Ranking from numeric trees: no RC strings, no hash text, no pin-location map; only a skeleton of the nets.
        const bool from_trees = ranking && options.gp_rank_trees && !save_macro_gp_timing;
        std::optional<timing::fast::Wire_trees> wire_trees;
        if (from_trees) {
          if (!tree_cache) {
            tree_cache = make_tree_cache(geometry);
          }
          checkpoint.parasitics = tree_cache->skeleton;
          wire_trees            = estimate_wire_trees(geometry, tech, rc_options, *tree_cache);
        } else {
          checkpoint.parasitics = estimate_rc(geometry, tech, rc_options, pin_locations);
          record_poses(*checkpoint.parasitics, geometry);
        }
        // Propagation belongs to untimed GP. Do not defer STA to finish jumps.
        final_gp_timing_started |= final_timing;
        auto timing              = prects_data_timing(
            from_trees ? options.gp_rank_trees({checkpoint.graph, *wire_trees, checkpoint.parasitics, v.ignored})
            : ranking  ? options.gp_rank({checkpoint.graph, checkpoint.parasitics, v.ignored})
                       : options.fp_evaluate(checkpoint),
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
          if (options.gui) {
            options.gui->set_timing(*timing.wns * timing.time_unit_ps, timing.tns ? *timing.tns * timing.time_unit_ps : 0., iteration);
          }
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
            R"({{"stage":"{}","iteration":{},"forced":{},"overflow":{},"movement_bins":{},"timing_seconds":{},"weighted_nets":{},"max_applied_weight":{},"delay":{},"time_unit_ps":{},"ranking_only":{}}})",
            timing_stage,
            iteration,
            final,
            macro_checkpoint_overflow,
            movement,
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(),
            std::count_if(v.weights.begin(), v.weights.end(), [](const auto& entry) { return entry.second != 1; }),
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
                                               options.timing_min_weight, options.timing_max_weight);
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
      if (!control.checkpoint || options.boundarypins.enabled || options.cell_jump.depth > 1) {
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
      for (const auto& c : view.cells) {
        frame.components.push_back({c.name, c.macro, c.place});
      }
      // DEF PINS and NETS must name the same connection. In particular, a
      // renamed clock net must not leave its top-level terminal on an empty net.
      snapshot_nets(frame, view);
      if (options.legroute_options.cellbox_full_flow) {
        if (!imported_frame) {
          throw std::invalid_argument("full cellbox flow requires complete imported source");
        }
        restore_critical_source_nets(frame, *imported_frame);
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
    if (final_timing && options.satlegalize && !terminal_active && !defer_legroute_cells
        && (!options.optimization_pending || options.region_sat)) {
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
          ph::Pin_locations                                fresh_locations;
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
          ph::Pin_locations locations;
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
        ph::Pin_locations                                locations;
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
        ph::Pin_locations locations;
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
          ph::Pin_locations                                locations;
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
          ph::Pin_locations                                locations;
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
    std::optional<Macro_anchor> macro_anchor;
    // anchor_final_override refers to macro_anchor: clear it whenever this scope ends.
    const auto anchor_override_guard = std::unique_ptr<void, std::function<void(void*)>>(reinterpret_cast<void*>(1), [&](void*) {
      anchor_final_override = nullptr;
    });
    // Movable hard macros (soft blocks and placement masks are not native macros), as node ids of v.design.
    const auto anchored_macros       = [&] {
      std::vector<int> ids;
      for (int i = 0; i < v.design.num_movable; ++i) {
        if (!v.design.node_macro.empty() && v.design.node_macro[i] && v.design.placement_mask(i) == dp::PlacementMask::none) {
          ids.push_back(i);
        }
      }
      return ids;
    };
    if (options.macro_anchor.enabled && movable_macros) {
      auto anchored = anchored_macros();
      if (!anchored.empty()) {
        Native_target_config target;
        target.bins_x = target.bins_y = control.auxiliary_bins ? control.auxiliary_bins : control.bins_x;
        target.macro_snap             = control.macro_snap;
        target.macro_snap_regular     = control.macro_snap_regular;
        target.macro_snap_options     = control.macro_snap_options;
        target.macro_snap_tolerance   = control.macro_snap_tolerance;
        target.macro_snap_weight      = control.macro_snap_weight;
        // The stage's own end-of-plan chain (edge snap, track snap, later passes); its counters describe only the
        // final hard pass, so target runs restore them.
        target.after_macro_legalize   = [&, after = control.after_macro_legalize](dp::Design& physical, bool late) {
          const auto counters    = std::tuple{v.edge_snap_moved, v.edge_snap_moves, v.edge_snap_distance};
          const auto pdn         = std::pair{v.pdn_alignments, v.pdn_fallbacks};
          const auto dead        = v.dead_space_collapse;
          anchor_skip_dead_space = !late;
          const auto restore     = [&] {
            anchor_skip_dead_space                                               = false;
            std::tie(v.edge_snap_moved, v.edge_snap_moves, v.edge_snap_distance) = counters;
            std::tie(v.pdn_alignments, v.pdn_fallbacks)                          = pdn;
            v.dead_space_collapse                                                = dead;
          };
          try {
            if (after) {
              after(physical);
            }
          } catch (...) {
            restore();
            throw;
          }
          restore();
        };
        auto anchor_policy       = options.macro_anchor;
        anchor_policy.dbu_per_um = tech.dbu;
        anchor_policy.channel_x  = v.channel_x;
        anchor_policy.channel_y  = v.channel_y;
        if (!options.macro_dead_space || !options.macro_dead_space->collapse) {
          anchor_policy.dead_space_refreshes = 0;  // no late pass to run: every refresh is comparable
        }
        macro_anchor.emplace(anchor_policy,
                             std::move(anchored),
                             control.max_iterations,
                             native_target_generator(std::move(target)),
                             options.output_dir.empty() ? std::string{} : options.output_dir + "/macro_anchor.json");
        const auto prior_progress = control.progress_observer;
        control.progress_observer = [&, prior_progress](const dp::GpSample& sample) {
          macro_anchor->observe_overflow(sample.overflow);
          if (prior_progress) {
            prior_progress(sample);
          }
        };
        control.macro_pull_interval          = 1;
        control.macro_pull_after_density     = false;
        control.macro_pull_settle_iterations = anchor_policy.settle;
        control.macro_pull_max_weight        = std::max(
            {1.0, anchor_policy.weight, anchor_policy.final_weight, anchor_policy.edge_weight, anchor_policy.random_weight});
        if (anchor_policy.inflate > 0) {
          control.macro_density_margin = [&](int) { return macro_anchor->density_margin(); };
        }
        control.macro_pull_update
            = [&](const dp::Design& snapshot, int iteration) { return macro_anchor->update(snapshot, iteration); };
        const auto prior        = control.before_legalize;
        control.before_legalize = [&, prior](dp::Design& physical) {
          if (prior) {
            prior(physical);
          }
          macro_anchor->before_legalize(physical);
        };
        if (anchor_policy.final_targets) {
          anchor_final_override = [&](dp::Design& physical) { return macro_anchor->final_override(physical); };
        }
      }
    }
    if (options.metadata && !hint_scopes.empty()) {
      if (!placement_hints) {
        placement_hints.emplace(options.metadata, hint_design, hint_scopes, hint_objects(), tech.dbu);
        placement_hints->seed(v.design);
      }
      placement_hints->rebind(hint_objects());
      placement_hints->attach(control, hint_objects);
    }
    // the routability probe (defined below) runs here, in the region-refinement call, before the mesh and the
    // bus-ordered pins are cut, so the inflation spreads the cells first and the boundaries follow; the terminal
    // stage then takes the refinement's mesh and pins (region_seed_dir) and does not probe again
    size_t probe_failing = 0;  // the regions the last probe round found unroutable
    if (refinement_active && !terminal_active && !live_session) {
      const auto prior_progress = control.progress_observer;
      control.progress_observer = [&, prior_progress](const dp::GpSample& sample) {
        gp_density_weight = sample.density_weight;  // the latest GP density weight seeds the refinement
        if (prior_progress) {
          prior_progress(sample);
        }
      };
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
    // legroute's frame additions: the handoff taps and end caps (rows cut under macros), and the planned power grid
    // (obstacles, power nets and vias; the IO pins as the handoff exports them). Returns the routing profile.
    const auto add_handoff_taps = [&](ph::Floorplan& frame) {
      const auto& tap_master = tech_profile->tap_master;
      const auto& master     = tech.macros.at(tap_master);
      frame.rows             = cut_macro_rows(frame,
                                              tech,
                                              std::llround(tech_profile->tap_row_halo * frame.dbu),
                                              std::llround(tech_profile->tap_row_halo * frame.dbu),
                                              std::llround(2 * master.width * frame.dbu));
      auto caps              = row_endcaps(frame, tech, tap_master);
      frame.components.insert(frame.components.end(), caps.begin(), caps.end());
      auto taps = row_taps(frame, tech, tap_master, std::llround(tech_profile->tap_distance * frame.dbu));
      frame.components.insert(frame.components.end(), taps.begin(), taps.end());
    };
    const auto legroute_power = [&](ph::Floorplan& frame, Legroute_regions& regions) -> const ph::Tech_profile* {
      const ph::Tech_profile* profile = nullptr;
      try {
        profile = &placement_tech_profile(tech, options);
      } catch (const std::invalid_argument&) {
        profile = nullptr;  // an unregistered technology: legroute takes the LEF routing stack
      }
      if (pg_profile) {
        auto grid      = make_power_grid(tech, frame, *pg_profile);
        auto terminals = grid.terminals;
        if (profile && !profile->io_pin_layers[0].empty() && !profile->io_pin_layers[1].empty()) {
          // the IO pins the handoff exports (their layer, shape and track), so legroute's wires end on them; the
          // handoff sees each pin's status as the export does (a stage freeze is not a user constraint)
          auto pin_frame = grid;
          for (auto& t : pin_frame.terminals) {
            if (const auto it = options.stage_pin_status.find(t.name); it != options.stage_pin_status.end()) {
              t.status = it->second;
            }
          }
          terminals = handoff_pins(tech, pin_frame, profile->io_pin_layers[0], profile->io_pin_layers[1]);
        }
        for (size_t i = 0; i < frame.terminals.size(); ++i) {
          if (i >= terminals.size() || terminals[i].name != frame.terminals[i].name) {
            throw std::runtime_error("legroute power-grid generation reordered signal IOs");
          }
          terminals[i].status = frame.terminals[i].status;
        }
        // Retain the supply terminals referenced by SPECIALNETS as well as the
        // PG-aware signal IOs. This routing frame is separate from v.fp and its
        // original signal-only placement/RC arrays.
        frame.terminals = std::move(terminals);
        for (const auto& net : grid.special_nets) {
          for (auto& shape : ph::route_geometry(tech, grid, net, true)) {
            regions.obstacles.push_back(std::move(shape));
          }
        }
        regions.power_nets = std::move(grid.special_nets);
        regions.power_vias = std::move(grid.vias);
      }
      return profile;
    };
    const auto terminal_controls = [&](const ph::Floorplan& frame) {
      auto terminal_config             = options.terminal_legalize_options;
      terminal_config.prects           = v.prects;
      terminal_config.route_drc        = options.satlegalize_route;
      terminal_config.route_vias       = options.satlegalize_route_vias ? tech_profile : nullptr;
      terminal_config.target_density   = options.density;
      terminal_config.macro_halo_x_dbu = v.halo_x;
      terminal_config.macro_halo_y_dbu = v.halo_y;
      set_terminal_grid(tech,
                        frame,
                        options,
                        route_grid_bins(control.auxiliary_bins, metrics.bins_x),
                        route_grid_bins(control.auxiliary_bins, metrics.bins_y),
                        terminal_config);
      return terminal_config;
    };
    struct Planned_handoff {
      std::shared_ptr<const lr::Critical_cellbox_handoff> critical;
      dp::Design                                          design;
      ph::Floorplan                                       frame;
      Legroute_regions                                    resources;
      const ph::Tech_profile*                             profile = nullptr;
      std::optional<Terminal_legalize_report>             report;
      timing::Feedback                                    timing;
      std::optional<Placement_session::Snapshot>          optimizer;
    };
    struct Plan_snapshot {
      dp::Design                                 design;
      std::vector<ph::Terminal>                  signal_pins;
      timing::Feedback                           timing;
      std::optional<Placement_session::Snapshot> optimizer;
    };
    std::optional<Plan_snapshot>          measured_plan;
    std::optional<Planned_handoff>        measured_handoff, chosen_handoff;
    // cellbox_local_replan: the Local failures of the selected Plan, inflated in one extra Plan feedback round
    std::vector<double>                   local_replan_pressure;
    std::optional<Planning_selection>     plan_selection;
    std::vector<Terminal_legalize_domain> plan_domains;
    const auto                            install_planned_pose = [&](dp::Design& design, const ph::Floorplan& frame) {
      sat_install(design, frame);
      // capture(false) and ordinary timing snapshots retain the view's CORE
      // orientations. Keep that view in sync with the native pose, including
      // when a later candidate is rejected and the measured incumbent returns.
      for (size_t i = 0; i < v.cells.size(); ++i) {
        v.cells[i].place = frame.components[i].place;
      }
      detail::install_handoff_io(design, v.fp.terminals, v.terminal_begin, frame.terminals);
    };
    const auto write_legroute_input
        = [&](const ph::Floorplan& frame, const Legroute_regions& regions, const std::string& directory) {
            if (!options.snapshots && !options.legroute_options.critical_cellboxes) {
              return;
            }
            // Keep the exact geometry and timing used by this routing measurement.
            // The regular GP snapshot precedes the planning updates, and final STA
            // need not retain the same routing priorities after legalization.
            fs::create_directories(directory);
            auto saved = frame;
            saved.special_nets.insert(saved.special_nets.end(), regions.power_nets.begin(), regions.power_nets.end());
            saved.vias.insert(saved.vias.end(), regions.power_vias.begin(), regions.power_vias.end());
            ph::write_def(saved, directory + "/input.def");
            std::ofstream timing_file(directory + "/timing.tsv");
            timing_file << "# physical_pin slack_ps clock_bit; routing priorities, not sign-off timing\n" << std::setprecision(17);
            std::set<std::string> written;
            for (const auto& net : frame.nets) {
              for (const auto& pin : net.connections) {
                const auto rank = final_gp_timing.pin_rank(pin.instance, pin.pin);
                const auto name = pin.instance == "PIN" ? pin.pin.str() : pin.instance + ":" + pin.pin;
                if ((std::isfinite(rank.slack) || rank.clock) && written.insert(name).second) {
                  timing_file << std::quoted(name) << '\t'
                              << (std::isfinite(rank.slack) ? rank.slack * final_gp_timing.time_unit_ps : 0) << '\t' << rank.clock
                              << '\n';
                }
              }
            }
            if (!timing_file) {
              throw std::runtime_error("cannot write legroute input timing");
            }
            if (options.legroute_options.critical_cellboxes) {
              std::ofstream snapshot(directory + "/timing_snapshot.json");
              snapshot << std::boolalpha << std::setprecision(17)
                       << "{\"source\":\"native_detailed_STA_at_input_def_pose\",\"ranking_only\":" << final_gp_timing.ranking_only
                       << ",\"time_unit_ps\":" << final_gp_timing.time_unit_ps << ",\"pin_slacks\":\"timing.tsv\",\"wns_ps\":";
              if (final_gp_timing.wns && std::isfinite(*final_gp_timing.wns)) {
                snapshot << *final_gp_timing.wns * final_gp_timing.time_unit_ps;
              } else {
                snapshot << "null";
              }
              snapshot << "}\n";
              if (!snapshot) {
                throw std::runtime_error("cannot write critical cellbox detailed timing snapshot");
              }
            }
          };
    // legroute.probe: a quick legroute on small regions (no virtual pins) finds the regions it cannot route; their
    // cells widen (GOALPlace-style inflation, x only: r = fill / target, less for timing-critical cells) and GP resumes
    // from the converged placement until it settles; the widths come back before bus order and the real legroute.
    // one probe round: legroute on the converged GP, the unroutable regions' cells widened, GP resumed; true when
    // every region routed (or nothing could be widened)
    // Boundary refinement operates on a separate split-net Design and releases
    // it before returning. Recovery must receive the physical topology itself,
    // not just an estimator with its boundary-pin vector cleared.
    const auto verify_physical_gp_input = [&](const dp::Design& design) {
      if (design.node_x.size() != v.terminal_begin + v.fp.terminals.size() || design.pin2node.size() != v.pins.size()
          || design.flat_net2pin_start.size() != v.nets.size() + 1) {
        throw std::logic_error("legroute recovery GP still contains virtual region nodes or split nets");
      }
      size_t net = 0, pin_index = 0;
      for (const auto& [driver, sinks] : v.nets) {
        (void)driver;
        if (size_t(design.flat_net2pin_start[net]) != pin_index
            || size_t(design.flat_net2pin_start[net + 1]) != pin_index + 1 + sinks.size()) {
          throw std::logic_error("legroute recovery GP retained split region connectivity");
        }
        pin_index += 1 + sinks.size();
        ++net;
      }
      for (size_t i = 0; i < v.pins.size(); ++i) {
        const auto&  pin  = v.pins[i];
        const size_t node = pin.instance == "PIN" ? v.terminal_begin + v.terminal.at(pin.pin) : v.cell.at(pin.instance);
        if (size_t(design.pin2node[i]) != node || size_t(design.flat_net2pin[i]) != i) {
          throw std::logic_error("legroute recovery GP retained a virtual region endpoint");
        }
      }
    };
    Legroute_gate_report measured_gates;
    bool                 measured_local_scope  = false;
    bool                 measured_fit_unknown  = false;  // the near-home box matching stopped on its budget
    bool                 measured_plan_pass    = false;
    bool                 plan_recovery_started = false;
    const bool           planning              = options.legroute_plan.enabled;
    const bool           coarse_feedback       = planning && options.legroute_plan.route_model == "coarse";
    // A movable flow retains its raw physical frame; the legacy handoff switch
    // additionally legalizes that frame as a fixed-placement diagnostic.
    const bool           capture_handoff       = options.legroute_plan.handoff || !options.legroute_options.legal_boxes;
    const bool           boxed_plan
        = options.legroute_plan.handoff || (!options.legroute_options.legal_boxes && options.legroute_options.partition_cells);
    const unsigned feedback_rounds    = planning ? options.legroute_plan.rounds : options.legroute_probe.rounds;
    const auto     run_legroute_probe = [&](dp::Design& design,
                                            unsigned    round,
                                            bool        measure_only    = false,
                                            size_t      incumbent_limit = std::numeric_limits<size_t>::max()) {
      measured_handoff.reset();
      measured_plan.reset();
      measured_gates       = {};
      measured_local_scope = false;
      measured_fit_unknown = false;
      measured_plan_pass   = false;
      auto po              = options.legroute_probe;
      if (planning) {
        po.real        = false;
        po.shuffle     = 0;
        po.target      = 1;
        po.max_inflate = options.legroute_plan.max_inflate;
        po.iterations  = std::max(50u, options.legroute_plan.iterations);
      }
      const auto start  = std::chrono::steady_clock::now();
      const bool seeded = po.real && !refinement_active && load_region_seed().has_value();
      const bool real   = po.real && (refinement_active || seeded);
      if (po.real && !real) {
        std::fprintf(stderr,
                     "legroute probe: probe.real asked but the boundary refinement is inactive here (boundary pins %d, full %d, "
                     "fp_place %d, optimization pending %d, region_refine %d, stage final %d): small regions without pins\n",
                     options.boundarypins.enabled,
                     options.full,
                     options.fp_place,
                     options.optimization_pending,
                     options.region_refine,
                     options.completed_stage == Placement_stage::final);
      }
      Legroute_regions regions;
      if (seeded) {  // the refinement's mesh and pins (fixed through the rounds)
        const auto& seed = *load_region_seed();
        if (seed.mesh.rects.size() > 1) {
          for (const auto& r : seed.mesh.rects) {
            regions.rects.push_back({r.xl, r.yl, r.xh, r.yh});
          }
          if (options.legroute_options.region_pins) {
            regions.pins = seed.pins;
          }
        }
      } else if (real) {  // the real legroute's regions and bus-ordered virtual pins, refined at this round's placement
        if (metrics.density_weight > 0) {
          gp_density_weight = metrics.density_weight;
        } else if (gp_density_weight <= 0) {  // before the refinement call's GP: the seed it was given
          gp_density_weight = control.density_weight_seed.value_or(0);
        }
        refine_final_boundaries(design);
        if (final_regions && final_regions->rects.size() > 1) {
          for (const auto& r : final_regions->rects) {
            regions.rects.push_back({r.xl, r.yl, r.xh, r.yh});
          }
          if (boundary_pins) {
            for (const auto& a : boundary_pins->pin_model().anchors) {
              if (a.original_net >= 0) {
                regions.pins.push_back({size_t(a.original_net), a.x, a.y});
              }
            }
          }
        }
      } else if (!planning) {
        Region_options ro = region_options_with_tracks(tech, options.region);
        ro.max_cells      = po.cells ? po.cells : options.region.max_cells;
        const auto mesh   = Region_mesh::build(design, region_macros(), region_ios(), ro, tech.dbu);
        if (mesh.rects.size() > 1) {
          for (const auto& r : mesh.rects) {
            regions.rects.push_back({r.xl, r.yl, r.xh, r.yh});
          }
        }
      }
      if (planning && control.checkpoint) {
        // The terminal refresh otherwise happens only after planning. Using an
        // empty or stale ranking here changes routing order and can hide the
        // very congestion the timed global pass will encounter.
        macro_checkpoint_forced = true;
        const auto update       = control.checkpoint(design, control.max_iterations);
        design.net_weights      = update.net_weights;
      }
      auto frame = sat_frame(design, false);
      if (options.handoff_taps) {
        add_handoff_taps(frame);
      }
      if (real) {  // the virtual pins name frame nets
        std::erase_if(regions.pins, [&](const auto& p) { return p.net >= frame.nets.size(); });
      }
      std::optional<Terminal_legalize_report> handoff_legalized;
      if (planning && options.legroute_plan.handoff) {
        auto controls    = terminal_controls(frame);
        controls.domains = plan_domains;
        Terminal_legalize_report legalized;
        try {
          legalized = terminal_legalize(tech, frame, final_gp_timing, controls);
        } catch (const Terminal_legalize_unfit& e) {
          std::fprintf(stderr, "legroute plan %u: native handoff failed: %s\n", round, e.what());
          if (round) {
            probe_failing = std::numeric_limits<size_t>::max();
            return true;  // retain the measured legal incumbent; this is a bounded search failure
          }
          if (!options.allow_increase_area || !options.generated_core) {
            throw;
          }
          const double utilization = options.utilization * options.area_step;
          throw Placement_area_restart(utilization, std::min(options.density, utilization + .05));
        }
        install_planned_pose(design, frame);
        handoff_legalized = std::move(legalized);
      } else if (planning && capture_handoff) {
        // sat_frame rounds into physical DBU coordinates without legalizing.
        // Refresh the placer and timer at exactly that raw, possibly
        // overlapping pose before measuring the movable-box handoff.
        install_planned_pose(design, frame);
      }
      const auto* profile = legroute_power(frame, regions);
      if (planning) {
        // Power-aware IO legalization can change both track and layer. STA,
        // later GP and rollback must all observe this measured signal pose;
        // appended supply terminals are outside the placement arrays.
        detail::install_handoff_io(design, v.fp.terminals, v.terminal_begin, frame.terminals);
        if (control.checkpoint && (capture_handoff || pg_profile)) {
          macro_checkpoint_forced = true;
          const auto update       = control.checkpoint(design, control.max_iterations);
          design.net_weights      = update.net_weights;
        }
        if (live_session) {
          live_session->synchronize(design, control, session_identities(), "plan_measure");
        }
        if (capture_handoff) {
          measured_handoff.emplace(Planned_handoff{{},
                                                   design,
                                                   frame,
                                                   regions,
                                                   profile,
                                                   std::move(handoff_legalized),
                                                   final_gp_timing,
                                                   live_session ? std::optional{live_session->snapshot()} : std::nullopt});
        }
      }
      auto lopt = options.legroute_options;
      if (lopt.critical_cellboxes) {
        lopt.cellbox_ignored_nets = prects_networks(tech, frame, v.prects, final_gp_timing).ignored;
        lopt.cellbox_final_retry  = planning && feedback_rounds == 3 && round >= 3;  // the last round and the Local replan
      }
      lopt.region_cell_limit = options.region.max_cells;
      lopt.final             = false;
      // This copy measures Plan/probe feedback only. The selected Local/Final
      // entry below derives a fresh copy and retains its diagnostic policy.
      lopt.cellbox_diagnostic_final = false;
      if (!planning) {
        lopt.legal_boxes = false;  // the probe judges the GP placement's regions with the SAT placement (nothing is legal yet)
      }
      lopt.sat_ms       = po.sat_ms;
      lopt.sat_min_fill = po.fill;
      lopt.spill_rounds = 0;  // the probe is the density signal: the moves and the alternatives belong to the real run
      lopt.alternatives.clear();
      // a failing probe restarts the placement: no need to solve the other regions; the real twin solves them all,
      // every failing region's cells widen
      lopt.fail_fast = po.strict && !real;
      lopt.sat_alt.clear();
      lopt.macro_halo_x_dbu = v.halo_x;
      lopt.macro_halo_y_dbu = v.halo_y;
      lopt.output_dir       = options.output_dir.empty()
                                  ? ""
                                  : options.output_dir + std::format("/legroute_{}{}", planning ? "plan" : "probe", round);
      if (!lopt.output_dir.empty()) {
        // An outer area retry replays round 0 into the same workdir: archive every earlier attempt's plan rounds and
        // its timing/selection reports whole (legroute_attempts/<k>/legroute_plan*), so no file mixes two attempts.
        if (planning && round == 0 && std::filesystem::exists(lopt.output_dir)) {
          namespace fs        = std::filesystem;
          const fs::path base = options.output_dir;
          size_t         k    = 0;
          while (fs::exists(base / "legroute_attempts" / std::to_string(k))) {
            ++k;
          }
          const fs::path archive = base / "legroute_attempts" / std::to_string(k);
          fs::create_directories(archive);
          for (const auto& entry : fs::directory_iterator(base)) {
            // the round directories plus the attempt's legroute_plan_timing.json / legroute_plan_selection.json
            if (entry.path().filename().string().starts_with("legroute_plan")) {
              fs::rename(entry.path(), archive / entry.path().filename());
            }
          }
        }
        std::filesystem::create_directories(lopt.output_dir);
        if (planning) {
          write_legroute_input(frame, regions, lopt.output_dir);
          if (measured_handoff && measured_handoff->report) {
            write_terminal_legalize_report(*measured_handoff->report,
                                           frame,
                                           terminal_controls(frame),
                                           lopt.output_dir + "/handoff");
          }
        }
      }
      const auto export_coarse_sample = [&](const dp::Design&           snapshot,
                                            const ph::Floorplan&        physical,
                                            const Legroute_congestion&  tiles,
                                            const Legroute_plan_report& measured,
                                            int                         bins_x,
                                            int                         bins_y,
                                            const fs::path&             directory) {
        if (!options.legroute_plan.export_coarse || !measured.route_attempted) {
          return;
        }
        const auto&    layout = snapshot.layout;
        const auto     maps   = resample_legroute_congestion(tiles, {layout.xl, layout.yl, layout.xh, layout.yh}, bins_x, bins_y);
        dp::RudyResult labels;
        labels.horizontal      = maps.horizontal;
        labels.vertical        = maps.vertical;
        labels.pin_utilization = maps.pin_utilization;
        labels.measured_nets   = int(maps.signals);
        labels.unroutable_nets = int(maps.unrouted);
        dp::detail::accumulate_overflow(labels);
        std::ostringstream pose, topology, capacity;
        pose << std::setprecision(17);
        capacity << std::setprecision(17) << tiles.bounds.xl << ' ' << tiles.bounds.yl << ' ' << tiles.bounds.xh << ' '
                 << tiles.bounds.yh << ' ' << tiles.cols << ' ' << tiles.rows << ' ' << tiles.admission_share << '\n';
        for (const auto& component : physical.components) {
          pose << std::quoted(component.name.str()) << ' ' << component.place.x << ' ' << component.place.y << ' '
               << int(component.place.orientation) << '\n';
          topology << std::quoted(component.name.str()) << ' ' << std::quoted(component.macro.str()) << '\n';
        }
        for (const auto& terminal : physical.terminals) {
          pose << std::quoted(terminal.name) << ' ' << terminal.x << ' ' << terminal.y << '\n';
        }
        for (const auto& net : physical.nets) {
          topology << std::quoted(net.name.str()) << ' ' << std::quoted(net.use.str());
          for (const auto& pin : net.connections) {
            topology << ' ' << std::quoted(pin.instance.str()) << ' ' << std::quoted(pin.pin.str());
          }
          topology << '\n';
        }
        for (size_t k = 0; k < tiles.limit_h.size(); ++k) {
          capacity << tiles.limit_h[k] << ' ' << tiles.limit_v[k] << '\n';
        }
        Uroute_coarse_provenance provenance;
        provenance.pose_sha256     = uroute_digest(pose.str());
        provenance.netlist_sha256  = uroute_digest(topology.str());
        provenance.capacity_sha256 = uroute_digest(capacity.str());
        provenance.endpoint_basis  = measured.endpoint_basis;
        provenance.admission_share = tiles.admission_share;
        provenance.complete        = measured.unrouted == 0;
        provenance.unrouted        = measured.unrouted;
        provenance.search_limited  = measured.search_limited;
        // A later area/recovery pass can reuse this round directory. Preserve
        // every distinct observation instead of replacing a previous sample.
        std::ostringstream observation;
        observation << std::setprecision(17) << kPlaceSalt << ':' << provenance.pose_sha256 << ':' << provenance.netlist_sha256
                    << ':' << provenance.capacity_sha256 << ':' << uroute_tech << ':' << bins_x << ':' << bins_y << ':'
                    << measured.unrouted << ':' << measured.search_limited;
        for (size_t i = 0; i < snapshot.node_x.size(); ++i) {
          observation << ':' << snapshot.node_x[i] << ':' << snapshot.node_y[i];
        }
        for (size_t k = 0; k < maps.horizontal.size(); ++k) {
          observation << ':' << maps.horizontal[k] << ':' << maps.vertical[k];
        }
        const auto destination = directory / uroute_digest(observation.str());
        export_uroute_coarse(destination,
                             snapshot,
                             uroute_id(v),
                             bins_x,
                             bins_y,
                             feature_capacity_h,
                             feature_capacity_v,
                             labels,
                             provenance);
        std::ofstream means(destination / "tile_means.json");
        means << std::setprecision(17);
        means << "{\"aggregation\":\"area_weighted_incident_edge_max\",\"horizontal\":[";
        for (size_t k = 0; k < maps.mean_horizontal.size(); ++k) {
          means << (k ? "," : "") << maps.mean_horizontal[k];
        }
        means << "],\"vertical\":[";
        for (size_t k = 0; k < maps.mean_vertical.size(); ++k) {
          means << (k ? "," : "") << maps.mean_vertical[k];
        }
        means << "]}\n";
        if (!means) {
          throw std::runtime_error("cannot write coarse congestion tile means");
        }
      };
      Legroute_report      probe;
      Legroute_plan_report plan;
      Legroute_congestion  congestion;
      try {
        if (planning) {
          plan = legroute_plan(tech,
                               frame,
                               final_gp_timing,
                               lopt,
                               regions,
                               profile,
                               options.legroute_plan.net_pops,
                               options.legroute_plan.handoff,
                               options.legroute_plan.export_coarse ? &congestion : nullptr);
        } else {
          probe = legroute(tech, frame, final_gp_timing, lopt, regions, profile);
        }
      } catch (const Legroute_unfit& e) {  // too dense for the tile grid: the core grows before any inflation
        if (planning && capture_handoff && round) {
          measured_handoff.reset();
          probe_failing = std::numeric_limits<size_t>::max();
          std::fprintf(stderr, "legroute plan %u: candidate cannot form a routable handoff: %s\n", round, e.what());
          return true;
        }
        if (options.legroute_options.critical_cellboxes || !options.allow_increase_area || !options.generated_core) {
          throw;
        }
        const double utilization = options.utilization * options.area_step;
        std::fprintf(stderr, "legroute probe: %s; placement restarts at utilization %.3f\n", e.what(), utilization);
        throw Placement_area_restart(utilization, utilization + .05);
      }
      if (planning && measured_handoff) {
        measured_handoff->critical = plan.cellbox_handoff;
      }
      if (planning && options.legroute_plan.export_coarse) {
        const int nx = route_grid_bins(control.auxiliary_bins, metrics.bins_x ? metrics.bins_x : options.bins);
        const int ny = route_grid_bins(control.auxiliary_bins, metrics.bins_y ? metrics.bins_y : options.bins);
        export_coarse_sample(design, frame, congestion, plan, nx, ny, fs::path(lopt.output_dir) / "uroute_coarse");
      }
      // per cell of an unroutable region: r = its region's box fill / target, at most max_inflate; the most timing-critical
      // cells (top 10% by timing weight, GOALPlace's 10-quantile guard) keep their size
      std::vector<double> fill(probe.region_stats.size(), 0), solved;
      for (size_t r = 0; r < probe.region_stats.size(); ++r) {
        const auto& st = probe.region_stats[r];
        fill[r]        = st.tiles ? double(st.boxes) / double(st.tiles) : 0;
        if (st.status == 0) {
          solved.push_back(fill[r]);
        }
      }
      auto              affected_components = probe.unroutable_components;
      auto              affected_regions    = probe.unroutable_regions;
      std::vector<char> local_cells(size_t(design.num_movable), 0);
      size_t            nearby_cells = 0;
      bool              local_scope  = false;
      // The window fit precondition failed: a placement-quality problem at the route's granularity, not a
      // shortage of core area. It is answered by spreading the cells of the dense windows, never by growth.
      bool              window_fit   = false;
      const unsigned    local_tiles  = std::max(1u, options.legroute_plan.local_tiles);
      if (planning) {
        measured_gates       = plan.gates;
        measured_fit_unknown = plan.fit_reason == "budget";
        if (plan.gates.requires_replan()) {
          for (size_t i = 0; i < local_cells.size(); ++i) {
            local_cells[i]  = plan.component_hot_distance[i] <= local_tiles;
            nearby_cells   += local_cells[i];
          }
          // A small absolute patch may still cover most of a small design.
          // Treat it as localized only when both the region target and a
          // quarter of movable cells bound the affected neighborhood.
          local_scope = nearby_cells > 0 && (!plan.gates.region_target || nearby_cells <= plan.gates.region_target)
                        && nearby_cells <= size_t(design.num_movable) / 4;
          std::fprintf(stderr,
                       "legroute plan %u: %zu hot tiles, %zu nearby cells, %s feedback\n",
                       round,
                       plan.hot_tiles,
                       nearby_cells,
                       local_scope ? "local" : "whole-placement");
        }
        measured_local_scope = local_scope;
        window_fit = !plan.route_attempted && plan.route_skip_reason == Legroute_plan_report::Route_skip_reason::window_density;
        fill       = plan.component_pressure;
        for (size_t i = 0; i < fill.size(); ++i) {
          if (fill[i] > 1) {
            affected_components.push_back(i);
            affected_regions.push_back(int(i));
          }
        }
        if (!local_replan_pressure.empty()) {
          // The cells on the tiles Local could not route widen; only they move (the passing Plan stays otherwise).
          size_t marked = 0;
          for (size_t i = 0; i < fill.size() && i < local_replan_pressure.size(); ++i) {
            if (local_replan_pressure[i] <= 1) {
              continue;
            }
            if (fill[i] <= 1) {
              affected_components.push_back(i);
              affected_regions.push_back(int(i));
            }
            fill[i] = std::max(fill[i], local_replan_pressure[i]);
            if (i < local_cells.size()) {
              marked         += !local_cells[i];
              local_cells[i]  = 1;
            }
          }
          nearby_cells += marked;
          local_scope   = true;
          window_fit    = false;
          std::fprintf(stderr, "legroute plan %u: %zu cells widen for the Local failures\n", round, marked);
        }
        probe_failing = plan.unrouted + plan.overflow_tracks + plan.gates.oversized_regions + plan.gates.cell_dense_regions
                        + plan.gates.routing_dense_edges + plan.gates.unfittable_regions;
        // Stages rank before residuals: a candidate whose cells do not fit their coarse tiles ranks behind every
        // candidate that reached the route, then by its matching deficit; a stopped matching search (budget) is a
        // computational unknown and never counts as progress.
        constexpr size_t fit_stage = size_t{1} << 40;
        if (!plan.route_attempted && plan.route_skip_reason == Legroute_plan_report::Route_skip_reason::window_density) {
          probe_failing
              = plan.fit_reason == "budget" ? std::numeric_limits<size_t>::max() - 1 : fit_stage + plan.gates.cell_dense_regions;
        } else if (plan.fit_reason == "budget") {
          // routed on the window fallback: an observation, not a near-home fit witness; behind every measured fit
          // deficit, ahead only of an unknown that did not route
          probe_failing = fit_stage + (size_t{1} << 30) + probe_failing;
        }
        if (!plan.route_attempted) {
          std::fprintf(stderr,
                       "legroute plan %u: routing not attempted (%s), %zu signals unchecked, physical cell density %.6f, "
                       "%zu of %zu fit windows over the cap, box matching %s%s%s (%zu boxes / %zu free tiles, %zu far, max move "
                       "%zu), %.3f s\n",
                       round,
                       plan.route_skip_name(),
                       plan.unchecked_signals,
                       plan.gates.max_cell_density,
                       plan.fit_dense_windows,
                       plan.fit_windows,
                       plan.fit_matched ? "complete" : "incomplete",
                       plan.fit_reason.empty() ? "" : " ",
                       plan.fit_reason.c_str(),
                       plan.fit_movable_boxes,
                       plan.fit_free_tiles,
                       plan.fit_far_boxes,
                       plan.fit_max_move,
                       plan.seconds);
        } else {
          std::fprintf(stderr,
                       "legroute plan %u: %zu signals (%zu timed), %zu unresolved (%zu search-limited, %zu topology-disconnected), "
                       "%zu overflow edges / %zu tracks, %zu "
                       "pops, %.3f s\n",
                       round,
                       plan.signals,
                       plan.timed_signals,
                       plan.unrouted,
                       plan.search_limited,
                       plan.topology_disconnected,
                       plan.overflow_edges,
                       plan.overflow_tracks,
                       plan.search_pops,
                       plan.seconds);
        }
        if (!lopt.output_dir.empty()) {
          write_legroute_plan_failures(plan, lopt.output_dir + "/plan_failures.tsv");
          std::ofstream report(lopt.output_dir + "/plan.json");
          report << std::format(
              R"({{"backend":"native","route_attempted":{},"route_skip_reason":"{}","unchecked_signals":{},"fit_windows":{},"fit_dense_windows":{},"fit_max_density":{},"fit_matched":{},"fit_reason":"{}","fit_movable_boxes":{},"fit_free_tiles":{},"fit_applied":{},"fit_far_boxes":{},"fit_max_move":{},"fit_unknown":{},"pg_missing":{},"signals":{},"timed_signals":{},"unrouted":{},"search_limited":{},"topology_disconnected":{},"overflow_edges":{},"overflow_tracks":{},"search_pops":{},"global_astar_weight":{},"global_pattern":{},"pattern_attempts":{},"pattern_completed":{},"pattern_fallbacks":{},"pattern_work":{},"pattern_work_limited":{},"seconds":{},"measure_only":{},"hot_tiles":{},"nearby_cells":{},"local_scope":{},"boxed_handoff":{},"endpoint_basis":"{}","box_route_seed_attempted":{},"box_route_seed_accepted":{},"handoff_legalized":{},"legalize_seconds":{},"pre_sat":{}}})",
              plan.route_attempted,
              plan.route_skip_name(),
              plan.unchecked_signals,
              plan.fit_windows,
              plan.fit_dense_windows,
              std::isfinite(plan.fit_max_density) ? std::format("{}", plan.fit_max_density) : std::string("null"),
              plan.fit_matched,
              plan.fit_reason,
              plan.fit_movable_boxes,
              plan.fit_free_tiles,
              plan.fit_applied,
              plan.fit_far_boxes,
              plan.fit_max_move,
              plan.fit_reason == "budget",
              plan.pg_missing,
              plan.signals,
              plan.timed_signals,
              plan.unrouted,
              plan.search_limited,
              plan.topology_disconnected,
              plan.overflow_edges,
              plan.overflow_tracks,
              plan.search_pops,
              plan.global_astar_weight,
              options.legroute_options.global_pattern,
              plan.pattern_attempts,
              plan.pattern_completed,
              plan.pattern_fallbacks,
              plan.pattern_work,
              plan.pattern_work_limited,
              plan.seconds,
              measure_only,
              plan.hot_tiles,
              nearby_cells,
              local_scope,
              boxed_plan,
              plan.endpoint_basis,
              plan.box_route_seed_attempted,
              plan.box_route_seed_accepted,
              measured_handoff && measured_handoff->report.has_value(),
              measured_handoff && measured_handoff->report ? measured_handoff->report->seconds : 0,
              legroute_gate_json(plan.gates))
                 << '\n';
        }
        if (!capture_handoff) {
          measured_plan.emplace(Plan_snapshot{design,
                                              v.fp.terminals,
                                              final_gp_timing,
                                              live_session ? std::optional{live_session->snapshot()} : std::nullopt});
        }
        if (plan_selection) {
          const auto report_selection = [&] {
            if (!options.output_dir.empty()) {
              std::ofstream report(options.output_dir + "/legroute_plan_timing.json");
              plan_selection->write(report);
              if (!report) {
                throw std::runtime_error("cannot write legroute planning timing decisions");
              }
            }
          };
          bool accepted = false;
          try {
            accepted = plan_selection->consider(round,
                                                probe_failing,
                                                measured_handoff ? measured_handoff->timing : measured_plan->timing);
          } catch (...) {
            report_selection();
            throw;
          }
          report_selection();
          const auto& decision = plan_selection->last();
          std::fprintf(stderr,
                       "legroute plan %u: candidate %s (%s), estimated worst data-pin slack %s ps\n",
                       round,
                       accepted ? "accepted" : "rejected",
                       decision.reason.c_str(),
                       decision.worst_data_slack_ps ? std::format("{:.3f}", *decision.worst_data_slack_ps).c_str() : "unavailable");
          measured_plan_pass = probe_failing == 0;
          if (options.legroute_options.critical_cellboxes) {
            measured_plan_pass = measured_plan_pass && plan.route_attempted
                                 && std::ranges::all_of(plan.component_pressure, [](double p) { return p <= 1; });
          }
          if (local_replan_pressure.empty()
              && !plan_selection
                      ->recovery_stop_reason(options.legroute_options.critical_cellboxes, feedback_rounds, measured_plan_pass)
                      .empty()) {
            return true;  // restore the measured incumbent without another GP slice
          }
        }
        // Excess physical occupancy needs room. Routing pressure alone may
        // instead be a poor placement (especially after remapping), so spend
        // the configured unpinned GP rounds before asking for more area.
        if (!options.legroute_options.critical_cellboxes && plan.gates.cell_dense_regions && !local_scope && !window_fit) {
          std::fprintf(stderr, "legroute plan %u: cell density exceeds the ceiling; request area recovery\n", round);
          return true;
        }
        // Do not spend another GP slice on a candidate already worse than the incumbent.
        if (measure_only || (!options.legroute_options.critical_cellboxes && (!probe_failing || probe_failing > incumbent_limit))) {
          return true;
        }
      }
      const bool local_bounds = local_scope && options.legroute_plan.local_tiles > 0;
      // the target fill this design routes at: the median fill of the regions the probe solved, else just under the
      // emptiest region it tried (all failed); never above probe.target
      // A window fit pressure is already the window's density over its cap: inflate by exactly that ratio.
      double     target       = window_fit ? 1. : po.target;
      if (solved.size() >= 2) {
        std::ranges::nth_element(solved, solved.begin() + solved.size() / 2);
        target = std::min(target, solved[solved.size() / 2]);
      } else if (!planning && solved.empty()) {
        double lowest = INFINITY;
        for (size_t r = 0; r < fill.size(); ++r) {
          if (probe.region_stats[r].status == 1 || probe.region_stats[r].status == 2) {
            lowest = std::min(lowest, fill[r]);
          }
        }
        if (std::isfinite(lowest)) {
          target = std::min(target, .9 * lowest);
        }
      }
      target            = std::max(target, .2);
      const auto weight = node_weight_criticality(design);  // max net weight, at least 1
      double     guard  = INFINITY;
      if (design.num_movable > 0) {
        std::vector<double> w(weight.begin(), weight.begin() + design.num_movable);
        std::ranges::nth_element(w, w.begin() + w.size() * 9 / 10);
        guard = w[w.size() * 9 / 10];
        if (guard <= 1) {  // untimed weights: nobody is protected
          guard = INFINITY;
        }
      }
      // shuffle: a few cells of each unroutable region's densest patch (a 4x4 grid over the region) move to its
      // sparsest patch and anchor there for the resume (their movement box shrinks to the cell): the noise the
      // user asked for, since the SAT's limited movement wants a slightly different placement, and the anchors
      // pull their nets toward the empty area. Timing-critical cells stay.
      struct Anchor {
        int             node;
        dp::PositionBox box;
      };
      std::vector<Anchor> anchored;
      std::set<int>       anchored_set;
      if (po.shuffle > 0) {
        std::map<int, std::vector<int>> cells_of;  // failing region -> its movable cells
        for (size_t k = 0; k < affected_components.size(); ++k) {
          const size_t i = affected_components[k];
          const int    r = affected_regions[k];
          if (i < size_t(design.num_movable) && r >= 0 && size_t(r) < fill.size()) {
            cells_of[r].push_back(int(i));
          }
        }
        if (design.node_boxes.empty()) {
          design.node_boxes.assign(design.node_x.size(), {design.layout.xl, design.layout.yl, design.layout.xh, design.layout.yh});
        }
        for (const auto& [r, cells] : cells_of) {
          double xl = design.layout.xl, yl = design.layout.yl, xh = design.layout.xh, yh = design.layout.yh;
          if (size_t(r) < regions.rects.size()) {
            xl = regions.rects[size_t(r)].xl;
            yl = regions.rects[size_t(r)].yl;
            xh = regions.rects[size_t(r)].xh;
            yh = regions.rects[size_t(r)].yh;
          }
          const int           nb = 4;
          std::vector<double> bin_area(size_t(nb * nb), 0);
          const auto          bin_of = [&](int i) {
            const double cx = design.node_x[i] + design.node_size_x[i] / 2, cy = design.node_y[i] + design.node_size_y[i] / 2;
            const int    bx = std::clamp(int((cx - xl) / std::max(1.0, xh - xl) * nb), 0, nb - 1);
            const int    by = std::clamp(int((cy - yl) / std::max(1.0, yh - yl) * nb), 0, nb - 1);
            return by * nb + bx;
          };
          for (int i : cells) {
            bin_area[size_t(bin_of(i))] += design.node_size_x[i] * design.node_size_y[i];
          }
          const int densest  = int(std::max_element(bin_area.begin(), bin_area.end()) - bin_area.begin());
          const int sparsest = int(std::min_element(bin_area.begin(), bin_area.end()) - bin_area.begin());
          if (densest == sparsest || bin_area[size_t(densest)] <= 0) {
            continue;
          }
          const double tx = xl + (sparsest % nb + .5) * (xh - xl) / nb, ty = yl + (sparsest / nb + .5) * (yh - yl) / nb;
          unsigned     picked = 0;
          for (int i : cells) {
            if (picked >= po.shuffle) {
              break;
            }
            if (bin_of(i) != densest || weight[i] >= guard || anchored_set.contains(i)) {
              continue;
            }
            anchored.push_back({i, design.node_boxes[i]});
            anchored_set.insert(i);
            // the anchors spread along x inside the sparse patch, a cell width apart
            const double x = std::clamp(
                tx + (double(picked) - double(po.shuffle - 1) / 2) * 2 * design.node_size_x[i] - design.node_size_x[i] / 2,
                xl,
                xh - design.node_size_x[i]);
            const double y       = std::clamp(ty - design.node_size_y[i] / 2, yl, yh - design.node_size_y[i]);
            design.node_x[i]     = x;
            design.node_y[i]     = y;
            design.node_boxes[i] = {x, y, x + design.node_size_x[i], y + design.node_size_y[i]};
            ++picked;
          }
        }
      }
      std::vector<double> grow(design.num_movable, 0);  // added width per movable cell
      double              movable_area = 0, added = 0;
      for (int i = 0; i < design.num_movable; ++i) {
        movable_area += design.node_size_x[i] * design.node_size_y[i];
      }
      for (size_t k = 0; k < affected_components.size(); ++k) {
        const size_t i = affected_components[k];
        const int    r = affected_regions[k];
        if (i >= size_t(design.num_movable) || r < 0 || size_t(r) >= fill.size() || anchored_set.contains(int(i))) {
          continue;
        }
        // actual route maps replace whole-tree width inflation, except for a fit failure: the cells did not fit
        // their coarse tiles, which no route map shows, so the named boxes' cells still spread; a distant endpoint
        // of a congested net does not widen the repair neighborhood
        if ((!options.legroute_options.critical_cellboxes && coarse_feedback && !window_fit) || (local_scope && !local_cells[i])) {
          continue;
        }
        const double ratio = std::clamp(fill[r] / target, 1.0, po.max_inflate);
        if (weight[i] >= guard && !options.legroute_options.critical_cellboxes) {
          continue;
        }
        grow[i]  = design.node_size_x[i] * (ratio - 1);
        added   += grow[i] * design.node_size_y[i];
      }
      // the widened cells must fit the core: the GP target density rises up to 0.90 of the free core area (fixed
      // nodes clipped to the core), beyond that the inflation scales down
      const auto& l          = design.layout;
      double      fixed_area = 0;
      for (size_t i = design.num_movable; i < design.node_x.size(); ++i) {
        const double w  = std::min(design.node_x[i] + design.node_size_x[i], l.xh) - std::max(design.node_x[i], l.xl);
        const double h  = std::min(design.node_y[i] + design.node_size_y[i], l.yh) - std::max(design.node_y[i], l.yl);
        fixed_area     += std::max(0.0, w) * std::max(0.0, h);
      }
      const double free_area = std::max(0.0, (l.xh - l.xl) * (l.yh - l.yl) - fixed_area);
      const double wanted    = added;
      // no room for the inflation at the target density: a generated core grows, and placement restarts at the
      // utilization that fits, the GP target density lowered with it (utilization + 0.05) so the cells spread over
      // the new core
      const double grow_core = free_area > 0 ? (movable_area + wanted) / control.target_density / free_area : 1;
      if (!planning && grow_core > 1.05 && options.allow_increase_area && options.generated_core) {
        const double utilization = options.utilization / grow_core;
        std::fprintf(stderr,
                     "legroute probe: the inflated cells need %.0f%% more core; placement restarts at utilization %.3f\n",
                     100 * (grow_core - 1),
                     utilization);
        throw Placement_area_restart(utilization, utilization + .05);
      }
      // A localized repair tries only the inflation that fits, then remeasures.
      // Broad gate failures already returned to area recovery above. An
      // oversized virtual proposal alone is not a physical capacity proof.
      if (movable_area + added > .90 * free_area) {  // the legroute density ceiling (0.90)
        const double scale = added > 0 ? std::max(0.0, .90 * free_area - movable_area) / added : 0;
        for (auto& g : grow) {
          g *= scale;
        }
        added *= scale;
      }
      const double density
          = free_area > 0 ? std::max(control.target_density, (movable_area + added) / free_area) : control.target_density;
      if (options.legroute_options.critical_cellboxes && !lopt.output_dir.empty()) {
        std::ofstream pressure(lopt.output_dir + "/critical_cellbox_inflation.json");
        pressure << std::boolalpha << std::setprecision(17)
                 << "{\"basis\":\"fresh_direct_and_whole_body_overlap_pressure\",\"critical_cells_exempt\":false,\"true_dimensions_"
                    "changed\":false"
                 << ",\"fixed_core_inflation_scale\":" << (wanted > 0 ? added / wanted : 1) << ",\"cells\":[";
        bool first = true;
        for (int i = 0; i < design.num_movable; ++i) {
          if (grow[size_t(i)] > 0) {
            pressure << (first ? "" : ",") << "{\"component\":" << i << ",\"physical_width_dbu\":" << design.node_size_x[size_t(i)]
                     << ",\"virtual_added_width_dbu\":" << grow[size_t(i)]
                     << ",\"legacy_timing_guard_would_skip\":" << (weight[size_t(i)] >= guard) << '}';
            first = false;
          }
        }
        pressure << "]}\n";
        if (!pressure) {
          throw std::runtime_error("cannot write critical cellbox inflation evidence");
        }
      }
      std::fprintf(stderr,
                   "legroute probe: target fill %.3f, movable area %.4g, free core %.4g (fill %.3f), inflation wanted %.4g kept "
                   "%.4g, GP target density %.3f -> %.3f\n",
                   target,
                   movable_area,
                   free_area,
                   free_area > 0 ? movable_area / free_area : 0.0,
                   wanted,
                   added,
                   control.target_density,
                   density);
      const double hpwl_before        = dp::hpwl(design);
      const auto   before_x           = local_scope ? design.node_x : std::vector<double>{};
      const auto   before_y           = local_scope ? design.node_y : std::vector<double>{};
      const auto   saved_boxes        = local_bounds ? design.node_boxes : std::vector<dp::PositionBox>{};
      size_t       inflated           = 0;
      size_t       local_moved        = 0;
      unsigned     iterations         = 0;
      unsigned     timing_checkpoints = 0;
      if (added > 0 || !anchored.empty() || (planning && probe_failing)) {
        if (planning && options.legroute_plan.handoff) {
          plan_domains.clear();
        }
        if (local_bounds) {
          if (design.node_boxes.empty()) {
            design.node_boxes.assign(design.node_x.size(), {l.xl, l.yl, l.xh, l.yh});
          }
          for (int i = 0; i < design.num_movable; ++i) {
            const double dx  = local_cells[size_t(i)] ? double(plan.tile_width_dbu) * local_tiles : 0;
            const double dy  = local_cells[size_t(i)] ? double(plan.tile_height_dbu) * local_tiles : 0;
            auto&        box = design.node_boxes[size_t(i)];
            box              = {std::max(box.xl, design.node_x[i] - dx),
                                std::max(box.yl, design.node_y[i] - dy),
                                std::min(box.xh, design.node_x[i] + dx),
                                std::min(box.yh, design.node_y[i] + dy)};
          }
          if (options.legroute_plan.handoff) {
            plan_domains.resize(frame.components.size());
            for (int i = 0; i < design.num_movable; ++i) {
              const auto& bounds      = design.node_boxes[size_t(i)];
              plan_domains[size_t(i)] = {
                  !local_cells[size_t(i)],
                  ph::Rect{bounds.xl, bounds.yl, bounds.xh, bounds.yh}
              };
            }
          }
        }
        verify_physical_gp_input(design);
        // Known local bounds above are temporary; saved_boxes is restored
        // after the slice. Physical/user node constraints are never cleared.
        // widen around the centre (pins keep their absolute positions), GP from the converged placement, narrow back
        const auto widen = [&](double sign) {
          for (int i = 0; i < design.num_movable; ++i) {
            if (grow[i] <= 0) {
              continue;
            }
            const double half      = sign * grow[i] / 2;
            design.node_size_x[i] += 2 * half;
            design.node_x[i]      -= half;
            for (int k = design.flat_node2pin_start[i]; k < design.flat_node2pin_start[i + 1]; ++k) {
              design.pin_offset_x[design.flat_node2pin[k]] += half;
            }
            if (!design.node_boxes.empty()) {  // origin bounds shift with the center-preserving width change
              design.node_boxes[i].xl -= half;
              design.node_boxes[i].xh -= half;
            }
          }
        };
        for (double g : grow) {
          inflated += g > 0;
        }
        const unsigned timing_interval
            = planning && control.checkpoint
                  ? std::max(1u, options.legroute_plan.timing_interval ? options.legroute_plan.timing_interval : 5u)
                  : 0;
        const unsigned minimum_steps     = planning ? std::max(50u, options.legroute_plan.min_iterations) : 0;
        // the real cell sizes and pin offsets, captured before widen(1): STA, coarse routing and coarse feature
        // exports all see the physical pose (the optimization's origin moved left by grow / 2)
        const auto     physical_template = timing_interval || coarse_feedback ? std::optional{design} : std::nullopt;
        const auto     to_physical       = [&](const dp::Design& snapshot) {
          auto actual        = *physical_template;
          actual.node_x      = snapshot.node_x;
          actual.node_y      = snapshot.node_y;
          actual.net_weights = snapshot.net_weights;
          for (int i = 0; i < actual.num_movable; ++i) {
            actual.node_x[size_t(i)] += grow[size_t(i)] / 2;
          }
          return actual;
        };
        const auto critical_physical_widths
            = options.legroute_options.critical_cellboxes ? design.node_size_x : std::vector<double>{};
        widen(1);
        auto ctl = control;
        if (const double dw = metrics.density_weight > 0 ? metrics.density_weight
                              : gp_density_weight > 0    ? gp_density_weight
                                                         : control.density_weight_seed.value_or(0);
            dw > 0) {
          ctl.density_weight_seed = dw * po.repulsion;
        }
        // Critical cellboxes recalibrate density on every slice (the session call below); a whole-placement slice there
        // keeps the held overflow and the small step too, else 100 steps collapse the cells (HPWL 8x lower, admission
        // 2400 -> 176 boxes on sha3/minimax).
        const bool global_recovery = planning && !local_scope && !options.legroute_options.critical_cellboxes;
        ctl.target_density         = density;
        ctl.fillers_in_whitespace  = true;
        ctl.hold_overflow          = !global_recovery;
        ctl.post_density_factor    = 0;
        if (!global_recovery) {
          ctl.wirelength_gamma_start = ctl.wirelength_gamma_end;
          ctl.max_step               = std::min(ctl.max_step, .05);
        } else {
          ctl.density_weight_seed.reset();
        }
        ctl.legalize = ctl.detailed_place = false;
        ctl.before_legalize               = {};
        ctl.after_macro_legalize          = {};
        ctl.progress_observer             = {};
        ctl.observer                      = {};
        ctl.checkpoint                    = {};
        ctl.checkpoint_interval           = 0;
        ctl.checkpoint_final              = false;
        ctl.checkpoint_after_density      = false;
        ctl.terminal_update               = {};
        ctl.terminal_interval             = 0;
        ctl.macro_pull_update             = {};
        ctl.macro_pull_interval           = 0;
        ctl.max_iterations = ctl.schedule_iterations = int(po.iterations);
        ctl.min_iterations                           = std::min<int>(ctl.min_iterations, int(po.iterations));
        if (minimum_steps) {
          if (minimum_steps > po.iterations) {
            throw std::invalid_argument("legroute.plan.min_iterations exceeds the planning iteration budget");
          }
          ctl.min_iterations = int(minimum_steps);
        }
        if (timing_interval) {
          if (!control.checkpoint) {
            throw std::invalid_argument("legroute.plan.timing_interval requires a timing checkpoint provider");
          }
          ctl.checkpoint_interval = int(timing_interval);
          ctl.checkpoint_start    = 0;
          ctl.checkpoint_final    = true;
          ctl.checkpoint_overflow_milestones.clear();
          ctl.checkpoint = [&](const dp::Design& snapshot, int iteration) {
            // Inflation is optimization geometry. STA sees the real cell sizes
            // and pin offsets at the corresponding physical lower-left origins.
            auto actual             = to_physical(snapshot);
            macro_checkpoint_forced = true;
            auto update             = control.checkpoint(actual, iteration);
            if (update.replacement || !update.node_size_x.empty() || !update.node_size_y.empty() || !update.pin_offset_x.empty()
                || !update.pin_offset_y.empty()) {
              throw std::runtime_error("planning timing feedback cannot resize or replace the inflated optimization topology");
            }
            ++timing_checkpoints;
            std::fprintf(stderr, "legroute plan feedback %u: physical timing refreshed at GP step %d\n", round, iteration);
            return update;
          };
        }
        size_t coarse_sample = 0;
        if (coarse_feedback) {
          ctl.routability = ctl.improved_routability = true;
          ctl.route.model                            = dp::RouteModel::external;
          ctl.route.probe                            = false;
          ctl.route_external_interval                = int(options.legroute_plan.route_interval);
          ctl.route_external                         = [&](const dp::Design& optimized, int bins_x, int bins_y) {
            const auto snapshot     = to_physical(optimized);
            // Keep the measured PG, row cuts, fixed cells and IO geometry. Only
            // movable physical component poses change; no virtual widths or
            // newly legalized IOs enter this in-flight routing observation.
            auto       current      = sat_frame(snapshot, false);
            auto       routed_frame = frame;
            if (current.components.size() > routed_frame.components.size()) {
              throw std::logic_error("coarse GP snapshot changed the measured component topology");
            }
            for (size_t i = 0; i < current.components.size(); ++i) {
              if (current.components[i].name != routed_frame.components[i].name
                  || current.components[i].macro != routed_frame.components[i].macro) {
                throw std::logic_error("coarse GP snapshot changed a measured component identity");
              }
              routed_frame.components[i].place = current.components[i].place;
            }
            Legroute_congestion tiles;
            auto                route_options = lopt;
            route_options.output_dir.clear();
            auto         measured               = legroute_plan(tech,
                                                                routed_frame,
                                                                final_gp_timing,
                                                                route_options,
                                                                regions,
                                                                profile,
                                                                options.legroute_plan.route_net_pops,
                                                                false,
                                                                &tiles,
                                                                true);
            const size_t initial_search_limited = measured.search_limited;
            double       sample_seconds         = measured.seconds;
            unsigned     sample_net_pops        = options.legroute_plan.route_net_pops;
            // A few budget-limited connections on an otherwise clear map are
            // not evidence for spreading a previously routable placement.
            // Resolve that ambiguity before their estimated demand inflates it.
            if (measured.search_limited && measured.unrouted == measured.search_limited && !measured.topology_disconnected
                && !measured.overflow_tracks && measured.unrouted <= std::max(size_t(1), measured.signals / 100)
                && sample_net_pops < options.legroute_plan.net_pops) {
              sample_net_pops  = options.legroute_plan.net_pops;
              measured         = legroute_plan(tech,
                                               routed_frame,
                                               final_gp_timing,
                                               route_options,
                                               regions,
                                               profile,
                                               sample_net_pops,
                                               false,
                                               &tiles,
                                               true);
              sample_seconds  += measured.seconds;
            }
            if (options.legroute_plan.export_coarse) {
              export_coarse_sample(snapshot,
                                   routed_frame,
                                   tiles,
                                   measured,
                                   bins_x,
                                   bins_y,
                                   fs::path(lopt.output_dir) / std::format("uroute_coarse_gp_{:04}", coarse_sample));
            }
            ++coarse_sample;
            const auto&    layout = snapshot.layout;
            const auto     maps = resample_legroute_congestion(tiles, {layout.xl, layout.yl, layout.xh, layout.yh}, bins_x, bins_y);
            dp::RudyResult route_result;
            route_result.horizontal      = maps.horizontal;
            route_result.vertical        = maps.vertical;
            route_result.pin_utilization = maps.pin_utilization;
            route_result.measured_nets   = int(maps.signals);
            route_result.unroutable_nets = int(maps.unrouted);
            for (size_t k = 0; k < route_result.horizontal.size(); ++k) {
              route_result.horizontal[k] += maps.unresolved_horizontal[k];
              route_result.vertical[k]   += maps.unresolved_vertical[k];
            }
            dp::detail::accumulate_overflow(route_result);
            route_result.overflow += double(maps.unrouted) / std::max(size_t(1), maps.signals);
            std::fprintf(stderr,
                         "legroute plan feedback %u: coarse sample %zu signals, %zu unresolved, %zu excess tracks, %.3fs\n",
                         round,
                         measured.signals,
                         measured.unrouted,
                         measured.overflow_tracks,
                         sample_seconds);
            if (!lopt.output_dir.empty()) {
              std::ofstream log(lopt.output_dir + "/coarse_gp.jsonl", std::ios::app);
              log << std::format(
                  R"({{"signals":{},"unrouted":{},"search_limited":{},"overflow_tracks":{},"bins_x":{},"bins_y":{},"overflow":{},"max_utilization":{},"tile_mean_h":{},"tile_mean_v":{},"seconds":{},"net_pops":{},"initial_search_limited":{}}})",
                  measured.signals,
                  measured.unrouted,
                  measured.search_limited,
                  measured.overflow_tracks,
                  bins_x,
                  bins_y,
                  route_result.overflow,
                  route_result.max_utilization,
                  std::accumulate(maps.mean_horizontal.begin(), maps.mean_horizontal.end(), 0.) / maps.mean_horizontal.size(),
                  std::accumulate(maps.mean_vertical.begin(), maps.mean_vertical.end(), 0.) / maps.mean_vertical.size(),
                  sample_seconds,
                  sample_net_pops,
                  initial_search_limited)
                  << '\n';
              if (!log) {
                throw std::runtime_error("cannot write coarse GP observations");
              }
            }
            return route_result;
          };
        }
        dp::GpState state;
        const auto  placed = live_session
                                 ? live_session->advance(design,
                                                         ctl,
                                                         session_identities(),
                                                         "plan_rebalance",
                                                         minimum_steps,
                                                         {},
                                                         false,
                                                         options.legroute_options.critical_cellboxes
                                                             || ((global_recovery || coarse_feedback) && !plan_recovery_started))
                                 : dp::place(design, ctl, ctl.checkpoint ? nullptr : &state);
        iterations         = unsigned(placed.iterations);
        plan_recovery_started |= global_recovery || coarse_feedback;
        if (planning && iterations < minimum_steps) {
          throw std::runtime_error("planning GP stopped before its requested minimum iteration count");
        }
        widen(-1);
        if (options.legroute_options.critical_cellboxes) {
          for (size_t i = 0; i < critical_physical_widths.size(); ++i) {
            if (std::abs(design.node_size_x[i] - critical_physical_widths[i]) > 1e-9 * std::max(1., critical_physical_widths[i])) {
              throw std::logic_error("critical Plan recovery did not restore a true physical cell width");
            }
          }
        }
        if (local_bounds) {
          for (int i = 0; i < design.num_movable; ++i) {
            const double dx    = std::abs(design.node_x[i] - before_x[size_t(i)]);
            const double dy    = std::abs(design.node_y[i] - before_y[size_t(i)]);
            const double max_x = local_cells[size_t(i)] ? double(plan.tile_width_dbu) * local_tiles : 0;
            const double max_y = local_cells[size_t(i)] ? double(plan.tile_height_dbu) * local_tiles : 0;
            if (dx > max_x + 1e-6 || dy > max_y + 1e-6) {
              throw std::runtime_error(
                  std::format("legroute plan cell {} moved ({}, {}) beyond local bounds ({}, {})", i, dx, dy, max_x, max_y));
            }
            local_moved += dx + dy > 1e-6;
          }
          design.node_boxes = saved_boxes;
        } else if (local_scope) {
          for (int i = 0; i < design.num_movable; ++i) {
            local_moved
                += local_cells[size_t(i)]
                   && std::abs(design.node_x[i] - before_x[size_t(i)]) + std::abs(design.node_y[i] - before_y[size_t(i)]) > 1e-6;
          }
        }
        for (const auto& a : anchored) {  // the anchors move freely again
          design.node_boxes[a.node] = a.box;
        }
        design.validate();
        if (live_session) {
          live_session->synchronize(design, control, session_identities(), "plan_physical");
        }
      }
      const double          hpwl_after = dp::hpwl(design);
      std::array<size_t, 4> count{};
      for (const auto& st : probe.region_stats) {
        ++count[size_t(std::clamp(st.status, 0, 3))];
      }
      const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
      if (planning) {
        std::fprintf(stderr,
                     "legroute plan feedback %u: %zu cells inflated (+%.1f%% area), GP %u iterations, HPWL %.4g -> %.4g, %.1f s\n",
                     round,
                     inflated,
                     movable_area > 0 ? 100 * added / movable_area : 0.0,
                     iterations,
                     hpwl_before,
                     hpwl_after,
                     seconds);
      } else {
        std::fprintf(
            stderr,
            "legroute probe round %u%s: %zu regions (routable %zu, unsat %zu, no answer %zu, sparse %zu), %zu cells widened "
            "(+%.1f%% area), %zu shuffled, GP %u iterations, HPWL %.4g -> %.4g, %.1f s\n",
            round,
            seeded ? " (the refinement's regions and pins)"
            : real ? " (real regions and pins)"
                   : "",
            probe.region_stats.size(),
            count[0],
            count[1],
            count[2],
            count[3],
            inflated,
            movable_area > 0 ? 100 * added / movable_area : 0.0,
            anchored.size(),
            iterations,
            hpwl_before,
            hpwl_after,
            seconds);
      }
      if (!lopt.output_dir.empty()) {
        std::ofstream report(lopt.output_dir + (planning ? "/feedback.json" : "/probe.json"));
        if (planning) {
          report << std::format(
              R"({{"inflated_cells":{},"added_area_ratio":{},"gp_iterations":{},"timing_checkpoints":{},"hpwl_before":{},"hpwl_after":{},"seconds":{},"local_scope":{},"local_movable_cells":{},"local_moved_cells":{},"bounded_local_motion":{},"gp_virtual_region_nodes":0,"gp_physical_nodes":{},"gp_original_nets":{},"gp_node_bounds":{}}})",
              inflated,
              movable_area > 0 ? added / movable_area : 0.0,
              iterations,
              timing_checkpoints,
              hpwl_before,
              hpwl_after,
              seconds,
              local_scope,
              local_scope ? nearby_cells : 0,
              local_moved,
              local_bounds,
              design.node_x.size(),
              design.flat_net2pin_start.size() - 1,
              design.node_boxes.size())
                 << '\n';
        } else {
          report << std::format(
              R"({{"regions":{},"routable":{},"unsat":{},"no_answer":{},"sparse":{},"inflated_cells":{},"added_area_ratio":{:.4f},)"
              R"("gp_iterations":{},"hpwl_before":{},"hpwl_after":{},"probe_seconds":{:.2f},"seconds":{:.2f}}})",
              probe.region_stats.size(),
              count[0],
              count[1],
              count[2],
              count[3],
              inflated,
              area > 0 ? added / area : 0.0,
              iterations,
              hpwl_before,
              hpwl_after,
              probe.seconds,
              seconds)
                 << '\n';
        }
      }
      if (!planning) {
        probe_failing = count[1] + count[2];
      }
      if (planning && options.legroute_options.critical_cellboxes) {
        return measured_plan_pass && plan_selection->last().accepted;
      }
      return probe_failing == 0 || (!planning && added <= 0 && anchored.empty());
    };
    const auto run_plan_rounds = [&](dp::Design& design) {
      plan_recovery_started = false;
      plan_selection.emplace(options.legroute_plan.max_degradation_ps);
      const auto           original_x = design.node_x, original_y = design.node_y;
      size_t               best     = std::numeric_limits<size_t>::max();
      unsigned             selected = 0;
      Legroute_gate_report selected_gates;
      bool                 selected_local_scope = false;
      bool                 selected_fit_unknown = false;
      bool                 selected_plan_pass   = false;
      unsigned             attempts_run         = 0;
      std::string          stop_reason;
      if (capture_handoff) {
        for (unsigned round = 0; round <= feedback_rounds; ++round) {
          const bool done = run_legroute_probe(design, round, round == feedback_rounds, best);
          ++attempts_run;
          stop_reason = plan_selection->recovery_stop_reason(options.legroute_options.critical_cellboxes,
                                                             feedback_rounds,
                                                             measured_plan_pass);
          if (!measured_handoff) {
            if (options.legroute_options.critical_cellboxes) {
              throw std::logic_error("critical Plan lost its measured handoff");
            }
            break;
          }
          if (plan_selection->last().accepted) {
            best                 = probe_failing;
            selected             = round;
            chosen_handoff       = std::move(measured_handoff);
            selected_gates       = measured_gates;
            selected_local_scope = measured_local_scope;
            selected_fit_unknown = measured_fit_unknown;
            selected_plan_pass   = measured_plan_pass;
          } else if (!options.legroute_options.critical_cellboxes) {
            break;
          }
          if (done) {
            if (stop_reason.empty()) {
              stop_reason = "no_actionable_recovery";
            }
            break;
          }
        }
        if (!chosen_handoff) {
          throw std::logic_error("legroute planning did not retain its initial measured handoff");
        }
        design          = chosen_handoff->design;
        final_gp_timing = chosen_handoff->timing;
        install_planned_pose(design, chosen_handoff->frame);
        if (live_session && chosen_handoff->optimizer) {
          live_session->restore(*chosen_handoff->optimizer);
        }
        if (options.legroute_options.cellbox_local_replan && options.legroute_options.cellbox_full_flow && selected_plan_pass
            && chosen_handoff->critical) {
          // Local on the selected Plan (no Final). Its failures widen their cells for one more Plan round.
          auto probe_frame          = chosen_handoff->frame;
          auto probe_regions        = chosen_handoff->resources;
          auto lopt                 = options.legroute_options;
          lopt.cellbox_handoff      = chosen_handoff->critical;
          lopt.cellbox_ignored_nets = prects_networks(tech, probe_frame, v.prects, chosen_handoff->timing).ignored;
          lopt.final = lopt.cellbox_relocate = lopt.cellbox_diagnostic_final = false;
          lopt.region_cell_limit                                              = options.region.max_cells;
          lopt.macro_halo_x_dbu                                               = v.halo_x;
          lopt.macro_halo_y_dbu                                               = v.halo_y;
          if (!options.output_dir.empty()) {
            lopt.output_dir = options.output_dir + "/legroute_local_probe";
            std::filesystem::create_directories(lopt.output_dir);
          }
          std::optional<Legroute_report> local;
          try {
            local = legroute(tech, probe_frame, chosen_handoff->timing, lopt, probe_regions, chosen_handoff->profile);
          } catch (const std::exception& e) {
            std::fprintf(stderr, "legroute local probe: %s; no Local replan\n", e.what());
          }
          if (local) {
            std::fprintf(stderr,
                         "legroute local probe: %zu segments unassigned, %zu nets failed, %zu private-coordinate conflicts\n",
                         local->unassigned_segments,
                         local->local_failed_nets,
                         local->final_domain_rejections);
          }
          if (local && (local->unassigned_segments || local->local_failed_nets)) {
            const auto incumbent  = best;
            local_replan_pressure = local->local_component_pressure;
            (void)run_legroute_probe(design, feedback_rounds + 1, false, best);  // remeasure + widen + GP slice
            local_replan_pressure.clear();
            (void)run_legroute_probe(design, feedback_rounds + 2, true, best);  // measure the replanned placement
            ++attempts_run;
            if (measured_handoff && measured_plan_pass && measured_handoff->critical) {
              std::fprintf(stderr, "legroute plan: the Local replan (round %u) passes Plan and replaces round %u\n",
                           feedback_rounds + 2, selected);
              selected       = feedback_rounds + 2;
              chosen_handoff = std::move(measured_handoff);
              selected_gates = measured_gates;
            } else {
              std::fprintf(stderr, "legroute plan: the Local replan does not pass Plan; round %u stays\n", selected);
            }
            best            = incumbent;
            design          = chosen_handoff->design;
            final_gp_timing = chosen_handoff->timing;
            install_planned_pose(design, chosen_handoff->frame);
            if (live_session && chosen_handoff->optimizer) {
              live_session->restore(*chosen_handoff->optimizer);
            }
          }
        }
      } else {
        std::optional<Plan_snapshot> best_plan;
        for (unsigned round = 0; round <= feedback_rounds; ++round) {
          const bool done = run_legroute_probe(design, round, round == feedback_rounds, best);
          ++attempts_run;
          stop_reason = plan_selection->recovery_stop_reason(options.legroute_options.critical_cellboxes,
                                                             feedback_rounds,
                                                             measured_plan_pass);
          if (!measured_plan) {
            if (options.legroute_options.critical_cellboxes) {
              throw std::logic_error("critical Plan lost its measured pose");
            }
            break;
          }
          if (plan_selection->last().accepted) {
            best                 = probe_failing;
            selected             = round;
            best_plan            = std::move(measured_plan);
            selected_gates       = measured_gates;
            selected_local_scope = measured_local_scope;
            selected_fit_unknown = measured_fit_unknown;
            selected_plan_pass   = measured_plan_pass;
          } else if (!options.legroute_options.critical_cellboxes) {
            break;
          }
          if (done) {
            if (stop_reason.empty()) {
              stop_reason = "no_actionable_recovery";
            }
            break;
          }
        }
        if (!best_plan) {
          throw std::logic_error("legroute plan did not retain its initial measured pose");
        }
        design          = best_plan->design;
        final_gp_timing = best_plan->timing;
        detail::install_handoff_io(design, v.fp.terminals, v.terminal_begin, best_plan->signal_pins);
        if (live_session && best_plan->optimizer) {
          live_session->restore(*best_plan->optimizer);
        }
      }
      size_t moved        = 0;
      double displacement = 0;
      for (int i = 0; i < design.num_movable; ++i) {
        const double distance  = std::abs(design.node_x[i] - original_x[i]) + std::abs(design.node_y[i] - original_y[i]);
        moved                 += distance > 1e-6;
        displacement          += distance;
      }
      std::fprintf(stderr,
                   "legroute plan: selected measured round %u, planning gate score %zu, %zu cells moved, displacement %.3f DBU\n",
                   selected,
                   best,
                   moved,
                   displacement);
      // The ranking penalty of an unknown fit is not a physical failure: when the selected candidate's measured gates
      // pass, it goes on to local (which repeats a complete regional matching) instead of growing area.
      const bool fit_unknown_only = best != 0 && selected_fit_unknown && !selected_gates.requires_replan()
                                    && selected_gates.unrouted == 0 && selected_gates.overflow_tracks == 0;
      const bool requires_replan
          = options.legroute_options.critical_cellboxes ? !selected_plan_pass : best != 0 && !fit_unknown_only;
      if (fit_unknown_only) {
        std::fprintf(stderr,
                     "legroute plan: selected round %u passes its measured gates; its near-home fit is unknown (matching "
                     "budget), so local decides\n",
                     selected);
      }
      if (!options.output_dir.empty()) {
        std::ofstream report(options.output_dir + "/legroute_plan_selection.json");
        report << std::format(
            R"({{"selected_round":{},"routing_gate_score":{},"unresolved_and_overflow":{},"moved_cells":{},"displacement_dbu":{},"boxed_handoff":{},"handoff_legalized":{},"requires_replan":{},"fit_unknown":{},"local_scope":{},"pre_sat":{}}})",
            selected,
            best,
            selected_gates.unrouted + selected_gates.overflow_tracks,
            moved,
            displacement,
            boxed_plan,
            chosen_handoff && chosen_handoff->report.has_value(),
            requires_replan,
            selected_fit_unknown,
            selected_local_scope,
            legroute_gate_json(selected_gates))
               << '\n';
      }
      if (options.legroute_options.critical_cellboxes && !options.output_dir.empty()) {
        const auto directory          = std::filesystem::path(options.output_dir);
        const auto selected_directory = directory / std::format("legroute_plan{}", selected);
        std::filesystem::copy_file(selected_directory / "critical_cellboxes.json",
                                   directory / "critical_cellboxes_selected.json",
                                   std::filesystem::copy_options::overwrite_existing);
        std::ofstream manifest(directory / "critical_cellboxes_selection.json");
        manifest << std::format(
            R"({{"selected_round":{},"source_directory":"legroute_plan{}","plan_pass":{},"attempts_run":{},"retries_run":{},"retry_budget":{},"stop_reason":"{}","output_geometry":"raw_gp_unlegalized","row_site_overlap_legalized":false,"fixed_macro_and_core_bounds_checked":true,"local_run":false,"final_run":false,"core_growth_allowed":false}})",
            selected,
            selected,
            !requires_replan,
            attempts_run,
            attempts_run ? attempts_run - 1 : 0,
            feedback_rounds,
            stop_reason)
                 << '\n';
        if (!manifest) {
          throw std::runtime_error("cannot write critical cellbox selection manifest");
        }
      }
      if (requires_replan) {
        if (options.legroute_options.critical_cellboxes) {
          // A post-Plan experiment keeps the fixed target frame and reports a
          // measured failure; it never invokes Local or grows the core.
          std::fprintf(stderr,
                       "critical cellboxes: fixed-frame Plan remains unresolved after bounded recovery; Local/Final not run\n");
          return;
        }
        verify_physical_gp_input(design);
        // Local movement bounds have already been restored. Invalidate only
        // computed region state; preserve physical IOs, masks and user bounds.
        plan_domains.clear();
        boundary_pins.reset();
        final_regions.reset();
        region_seed.reset();
        region_seed_failed = true;
        const auto why     = std::format(
            "pre-SAT planning screen still blocked after {}: {} oversized regions, {} unfittable regions ({} boxes), {} dense cell "
            "regions, "
            "{} dense routing edges, {} overflow tracks, {} unresolved ({} search-limited, {} topology-disconnected)",
            selected_local_scope ? "localized timed movement" : "broad pressure",
            selected_gates.oversized_regions,
            selected_gates.unfittable_regions,
            selected_gates.unfittable_boxes,
            selected_gates.cell_dense_regions,
            selected_gates.routing_dense_edges,
            selected_gates.overflow_tracks,
            selected_gates.unrouted,
            selected_gates.search_limited,
            selected_gates.topology_disconnected);
        if (!options.allow_increase_area || !options.generated_core) {
          throw std::runtime_error(why + "; area recovery is unavailable; SAT was not attempted");
        }
        const double utilization = options.utilization * options.area_step;
        std::fprintf(stderr,
                     "legroute plan: %s; retained global placement retries at utilization %.3f without region IO pins\n",
                     why.c_str(),
                     utilization);
        throw Placement_area_restart(utilization, std::min(options.density, utilization + .05));
      }
    };
    // the region-refinement call starts from a converged placement: the probe rounds run here, before its GP call
    // (never inside DREAMPlace's hooks: the detailed placer keeps its own copy of the cells' sizes and positions),
    // and the refinement in the call's before-legalize hook then cuts the mesh and places the pins after the inflation
    if (refinement_active && !terminal_active && options.legroute && options.legroute_probe.enabled && !multirow_cells) {
      // the rounds keep the best placement: a round that leaves more regions failing than the best so far is undone
      // (its inflation moved cells past the point of need) and the rounds stop
      std::vector<double> best_x = v.design.node_x, best_y = v.design.node_y;
      size_t              best_failing = std::numeric_limits<size_t>::max();
      bool                measured     = false;  // the placement now in v.design was measured by the last round
      for (unsigned round = 0; round < std::max(1u, feedback_rounds); ++round) {
        const auto before_x = v.design.node_x;
        const auto before_y = v.design.node_y;
        const bool done     = run_legroute_probe(v.design, round);
        if (probe_failing > best_failing) {
          std::fprintf(stderr,
                       "legroute probe: round %u left %zu regions failing, the best was %zu: that placement is kept\n",
                       round,
                       probe_failing,
                       best_failing);
          v.design.node_x = best_x;
          v.design.node_y = best_y;
          measured        = true;
          break;
        }
        if (probe_failing < best_failing) {
          best_failing = probe_failing;
          best_x       = before_x;  // the placement the probe measured, before this round's inflation moved it
          best_y       = before_y;
        }
        measured = done;  // a round that changed nothing ends with the placement it measured
        if (done) {
          break;
        }
      }
      if (!measured && best_failing > 0 && best_failing != std::numeric_limits<size_t>::max()) {
        // the last round's inflation was never measured: the best measured placement is the one to keep unless the
        // rounds ran out while improving (then the latest inflation stands, as the terminal legroute measures it)
        std::fprintf(
            stderr,
            "legroute probe: %zu regions failing at the best measured round; the latest inflation stands for the terminal run\n",
            best_failing);
      }
      // DREAMPlace's detailed placer asserts on the cells the probe's resumes leave overlapping; legalization alone
      // settles them, the terminal stage legalizes again anyway
      control.detailed_place = false;
    }
    if (live_session && control.checkpoint) {
      session_checkpoint = [&](const dp::Design& physical, int iteration) {
        macro_checkpoint_forced = true;
        return control.checkpoint(physical, iteration);
      };
    }
    const auto run_main_gp = [&] {
      if (!live_session) {
        return dp::place(v.design, control);
      }
      auto gp     = control;
      gp.legalize = gp.detailed_place = false;
      const bool resume               = options.placement_area_resume && live_session->live() && !terminal_active;
      unsigned   minimum              = 0;
      bool       remap_recovery       = false;
      if (resume) {
        // A new macro plan already supplied cells spread around its new fixed
        // bodies. Keep that candidate; ordinary core growth spreads old cells.
        const auto seeded = options.placement_macro_replan ? 0 : live_session->seed_movable(v.design, session_identities());
        remap_recovery    = !options.placement_macro_replan && seeded * 10 < size_t(v.design.num_movable) * 7;
        minimum           = std::max(50u, options.legroute_plan.min_iterations);
        gp.max_iterations = gp.schedule_iterations = int(std::max(minimum, options.legroute_plan.iterations));
        gp.min_iterations                          = int(minimum);
        verify_physical_gp_input(v.design);
        if (!options.output_dir.empty()) {
          std::ofstream report(options.output_dir + "/legroute_area_gp_input.json");
          report << std::format(
              R"({{"virtual_region_nodes":0,"physical_nodes":{},"original_nets":{},"node_bounds":{},"region_seed_ignored":true,"minimum_steps":{}}})",
              v.design.node_x.size(),
              v.design.flat_net2pin_start.size() - 1,
              v.design.node_boxes.size(),
              minimum);
        }
        // Large remaps are not settled placements: new cells need full motion
        // and a fresh smoothing/penalty schedule in the same live optimizer.
        gp.fillers_in_whitespace = true;
        gp.hold_overflow         = !remap_recovery;
        gp.post_density_factor   = 0;
        if (!remap_recovery) {
          gp.wirelength_gamma_start = gp.wirelength_gamma_end;
          gp.max_step               = std::min(gp.max_step, .05);
        } else {
          gp.density_weight_seed.reset();
        }
        if (session_checkpoint) {
          gp.checkpoint               = session_checkpoint;
          gp.checkpoint_interval      = int(options.legroute_plan.timing_interval ? options.legroute_plan.timing_interval : 5u);
          gp.checkpoint_start         = 0;
          gp.checkpoint_final         = true;
          // A warm recovery slice needs periodic ranking even if the original
          // solve consumed its density milestones or still exceeds them.
          gp.checkpoint_after_density = false;
          gp.checkpoint_overflow_milestones.clear();
        }
        std::fprintf(stderr,
                     "legroute area resume: %zu cells seeded, %d GP steps, utilization %.4f density %.4f\n",
                     seeded,
                     gp.max_iterations,
                     options.utilization,
                     gp.target_density);
        if (options.placement_macro_replan) {
          std::fprintf(stderr, "legroute area resume: retaining the fresh macro-plan cell seed\n");
        }
      }
      return live_session->advance(v.design,
                                   gp,
                                   session_identities(),
                                   resume ? "area_resume" : "global_placement",
                                   minimum,
                                   session_identities,
                                   options.placement_area_resume && options.placement_macro_replan,
                                   remap_recovery);
    };
    // macro.pdn=auto: keep the exact legalization input (after every pre-legalization hook) so a macro legalization
    // that fails with the PDN channel can be retried from the same pose at the next weaker level.
    const bool pdn_degradable
        = !live_session && control.legalize && options.macro_pdn == "auto" && v.pdn_level + 1 < v.pdn_ladder.size()
          && std::any_of(v.design.node_macro.begin(), v.design.node_macro.begin() + v.design.num_movable, [](auto macro) {
               return macro != 0;
             });
    std::optional<dp::Design> pdn_legalize_input;
    if (pdn_degradable) {
      const auto prior        = control.before_legalize;
      control.before_legalize = [&, prior](dp::Design& physical) {
        if (prior) {
          prior(physical);
        }
        pdn_legalize_input = physical;
      };
    }
    gui_attach(control, options, v, tech, result, options.completed_stage == Placement_stage::macro ? "macro" : "final");
    try {
      metrics = run_main_gp();
    } catch (const std::runtime_error& error) {
      if (macro_legalization_failure(error) && pdn_legalize_input) {
        // Degrade one level at a time (gap -> align -> off), each attempt legalizing the same GP pose with the
        // weaker channel; the first level that legalizes stays for the rest of the placement.
        auto input = std::move(*pdn_legalize_input);
        pdn_legalize_input.reset();
        bool legal = false;
        while (!legal && v.pdn_level + 1 < v.pdn_ladder.size()) {
          const auto& failed = v.pdn_ladder[v.pdn_level];
          v.pdn_attempts.push_back({failed.level, failed.channel_x, failed.channel_y, "legalization_failed"});
          set_pdn_level(v, v.pdn_level + 1);
          std::tie(v.plan_pad_x, v.plan_pad_y) = plan_pad(v, v.channel_x, v.channel_y);
          v.design                             = input;
          for (int i = 0; i < v.design.num_movable; ++i) {
            if (v.design.node_macro[i] && v.design.placement_mask(i) == dp::PlacementMask::none) {
              // the pose keeps each macro's body where GP put it; only the reserved pad shrinks
              v.design.node_halo_x[i] = double(std::max(v.halo_x, v.plan_pad_x));
              v.design.node_halo_y[i] = double(std::max(v.halo_y, v.plan_pad_y));
            }
          }
          livehd::diag::warn("pass.place", "macro-pdn", "unsupported")
              .msg(
                  "macro.pdn=auto: macro legalization failed with the {} channel ({:.2f} x {:.2f} um); retrying with the {} "
                  "channel ({:.2f} x {:.2f} um)",
                  pg_macro_level_name(failed.level),
                  double(failed.channel_x) / tech.dbu,
                  double(failed.channel_y) / tech.dbu,
                  pg_macro_level_name(v.pdn_ladder[v.pdn_level].level),
                  double(v.channel_x) / tech.dbu,
                  double(v.channel_y) / tech.dbu)
              .emit();
          auto retry            = control;
          retry.max_iterations  = 0;
          retry.before_legalize = {};
          // macro.anchor's final override was armed for the failed level (targets on its channel/lattice); the
          // retry legalizes the captured pose and runs the host chain at the new level.
          anchor_final_override = nullptr;
          try {
            metrics = dp::place(v.design, retry);
            legal   = true;
          } catch (const std::runtime_error& again) {
            if (!macro_legalization_failure(again) || v.pdn_level + 1 >= v.pdn_ladder.size()) {
              throw;
            }
          }
        }
      } else if (sat_gp) {
        v.design                 = *sat_gp;
        auto fallback            = control;
        fallback.max_iterations  = 0;
        fallback.before_legalize = {};
        // The failed solve may have armed macro.anchor's final override; the
        // fallback legalizes the SAT pose, not the anchor targets.
        anchor_final_override    = nullptr;
        sat_retained             = 0;
        metrics                  = dp::place(v.design, fallback);
      } else {
        if (cache.reused == 0) {
          if (macro_legalization_failure(error) && options.macro_pdn != "auto" && !v.pdn_ladder.empty()
              && v.pdn_ladder[v.pdn_level].level != Pg_macro_level::off) {
            throw std::runtime_error(pdn_legalization_error(v, tech, options, error));
          }
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
        // make_arrays re-sorted the nodes (and unlocked blocks may add macros): macro.anchor restarts on the new
        // node ids with a fresh refresh clock and history.
        if (macro_anchor) {
          macro_anchor->restart(anchored_macros());
        }
        metrics = run_main_gp();
      }
    }
    // macro.refine_rounds: alternate a short GP burst (macros movable again, starting from the legalized, snapped
    // and collapsed pose) with the macro legalization hook (edge snap, dead-space collapse, track alignment). The
    // burst is a warm restart like the legroute area resume: the density weight of the previous solve, the final
    // wirelength smoothing, a small step, fillers seeded in the free space and the overflow held where it is. A
    // GpState continuation would resume the pre-legalization optimizer pose and discard the macro edits.
    const bool refine_macros = options.macro_refine_rounds > 0 && options.macro_refine_iterations > 0 && !live_session
                               && !terminal_active && control.legalize && bool(control.after_macro_legalize)
                               && std::any_of(v.design.node_macro.begin(),
                                              v.design.node_macro.begin() + v.design.num_movable,
                                              [](auto macro) { return macro != 0; });
    for (unsigned round = 0; refine_macros && round < options.macro_refine_rounds; ++round) {
      const auto before_x = v.design.node_x, before_y = v.design.node_y;
      auto       burst     = control;
      const int  steps     = static_cast<int>(options.macro_refine_iterations);
      burst.max_iterations = burst.schedule_iterations = burst.min_iterations = steps;
      burst.post_density_factor                                               = 0;
      burst.hold_overflow                                                     = true;
      burst.fillers_in_whitespace                                             = true;
      burst.wirelength_gamma_start                                            = burst.wirelength_gamma_end;
      burst.max_step                                                          = std::min(burst.max_step, .05);
      if (metrics.density_weight > 0) {
        burst.density_weight_seed = metrics.density_weight;
      }
      burst.checkpoint           = {};
      burst.before_legalize      = {};
      // macro.anchor pulls belong to the main macro GP; a refine burst starts from the legal frame without them.
      burst.macro_pull_update    = {};
      burst.macro_pull_interval  = 0;
      burst.macro_density_margin = {};
      // A round whose macro legalization fails (e.g. no legal routing-track alignment) is dropped: the previous
      // round's legal frame stays and refinement stops.
      auto       saved           = v.design;
      const auto saved_snap      = std::tuple{v.edge_snap_moved, v.edge_snap_moves, v.edge_snap_distance};
      const auto saved_pdn       = std::pair{v.pdn_alignments, v.pdn_fallbacks};
      const auto saved_dead      = v.dead_space_collapse;
      dp::Result again;
      try {
        again = dp::place(v.design, burst);
      } catch (const std::runtime_error& error) {
        v.design                                                             = std::move(saved);
        std::tie(v.edge_snap_moved, v.edge_snap_moves, v.edge_snap_distance) = saved_snap;
        std::tie(v.pdn_alignments, v.pdn_fallbacks)                          = saved_pdn;
        v.dead_space_collapse                                                = saved_dead;
        View::Refine_round failed;
        failed.iterations = -1;
        v.refine_rounds.push_back(failed);
        std::fprintf(stderr, "macro refine round %u failed (%s); keeping the previous round\n", round + 1, error.what());
        break;
      }
      // A round is a local search step on the legal macro frame: keep it only when the legal, detailed-placed
      // wirelength improves. A burst that loses wirelength (macros dragged by a re-spread, a collapse that
      // stretches nets) is undone and refinement stops; the previous frame is already legal.
      if (!again.legal || again.hpwl >= metrics.hpwl) {
        std::fprintf(stderr,
                     "macro refine round %u rejected: hpwl %.6g um (legal %d) does not improve %.6g um\n",
                     round + 1,
                     again.hpwl / tech.dbu,
                     int(again.legal),
                     metrics.hpwl / tech.dbu);
        v.design                                                             = std::move(saved);
        std::tie(v.edge_snap_moved, v.edge_snap_moves, v.edge_snap_distance) = saved_snap;
        std::tie(v.pdn_alignments, v.pdn_fallbacks)                          = saved_pdn;
        v.dead_space_collapse                                                = saved_dead;
        View::Refine_round rejected;
        rejected.iterations = again.iterations;
        rejected.hpwl       = again.hpwl;
        rejected.overflow   = again.overflow;
        rejected.rejected   = true;
        v.refine_rounds.push_back(rejected);
        break;
      }
      metrics.legal = again.legal;
      metrics.hpwl  = again.hpwl;
      View::Refine_round record;
      record.iterations     = again.iterations;
      record.hpwl           = again.hpwl;
      record.overflow       = again.overflow;
      record.density_weight = again.density_weight;
      for (int i = 0; i < v.design.num_movable; ++i) {
        if (v.design.node_macro[i]) {
          const auto d = std::llround(std::abs(v.design.node_x[i] - before_x[i]) + std::abs(v.design.node_y[i] - before_y[i]));
          record.macro_displacement += d;
          record.moved_macros       += d != 0;
        }
      }
      if (v.dead_space_collapse) {
        record.dead_area = v.dead_space_collapse->after.dead_area;
      }
      v.refine_rounds.push_back(record);
      std::fprintf(stderr,
                   "macro refine round %u: %d GP steps, hpwl %.6g um, overflow %.4f, dead %.6g um2, %lld macros moved %.6g um\n",
                   round + 1,
                   record.iterations,
                   record.hpwl / tech.dbu,
                   record.overflow,
                   record.dead_area < 0 ? -1.0 : double(record.dead_area) / tech.dbu / tech.dbu,
                   static_cast<long long>(record.moved_macros),
                   double(record.macro_displacement) / tech.dbu);
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
    anchor_final_override = nullptr;
    if (macro_anchor) {
      macro_anchor->finish(v.design, metrics.iterations);
    }
    // a terminal stage with a region seed had its probe in the refinement call
    if (terminal_active && options.legroute && options.legroute_probe.enabled && !multirow_cells
        && options.region_seed_dir.empty()) {
      for (unsigned round = 0; round < std::max(1u, feedback_rounds); ++round) {
        if (run_legroute_probe(v.design, round)) {
          break;
        }
      }
    }
    if (refinement_active && (terminal_active || live_session)) {
      gp_density_weight = metrics.density_weight;
      refine_final_boundaries(v.design);
    }
    if (live_session && !terminal_active && control.legalize) {
      auto finish           = control;
      finish.max_iterations = finish.min_slice_iterations = 0;
      // Boundary refinement already ran with the retained session.
      const auto settled                                  = dp::place(v.design, finish);
      metrics.legal                                       = settled.legal;
      metrics.hpwl                                        = settled.hpwl;
      live_session->synchronize(v.design, control, session_identities(), "stage_legal_pose");
    }
    // Planning runs after all ordinary GP/refinement, outside DREAMPlace hooks.
    // Region-driven legroute then builds fresh cuts after terminal placement;
    // stale refinement pins never constrain the plan or its regional SAT.
    if (terminal_active && options.legroute && planning && !multirow_cells) {
      run_plan_rounds(v.design);
    }
    if (live_session && !options.output_dir.empty()) {
      live_session->write(options.output_dir + "/placement_session.json");
    }
    if (terminal_active && (!options.legroute_options.critical_cellboxes || options.legroute_options.cellbox_full_flow)) {
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
        if (chosen_handoff) {
          // Preserve the routing order used to score the exact accepted pose.
          final_gp_timing      = chosen_handoff->timing;
          v.design.net_weights = chosen_handoff->design.net_weights;
        }
        write_legalize_timing("global_placement");
      }
      auto       frame        = sat_frame(v.design, false);
      // The two-row cells' row-access origin: the first core row before the
      // handoff taps cut rows under macros.
      const auto core_origins = multirow_cells ? first_core_rows(frame) : std::map<std::string, int64_t>{};
      if (options.handoff_taps) {
        const auto original_count = frame.components.size();
        add_handoff_taps(frame);
        handoff_cells.assign(frame.components.begin() + original_count, frame.components.end());
        handoff_rows = frame.rows;
      }
      auto       terminal_options = terminal_controls(frame);
      // Anchor the two-row cells first; terminal legalization packs the
      // single-row cells around them as fixed obstacles, then they are released.
      // legroute places them itself (a two-row cell is a tile tall), and
      // sync_rail_twins gives each the master its rows' rails need.
      const bool legal_boxes      = options.legroute && options.legroute_options.legal_boxes;
      if (multirow_cells && (!options.legroute || legal_boxes)) {
        multirow_report = anchor_multirow_cells(tech, frame, anchor_controls(options, terminal_options, core_origins));
      }
      if (chosen_handoff) {
        const auto& accepted = chosen_handoff->frame;
        if (frame.components != accepted.components || frame.nets != accepted.nets || frame.rows != accepted.rows
            || frame.terminals.size() > accepted.terminals.size()
            || !std::equal(frame.terminals.begin(), frame.terminals.end(), accepted.terminals.begin())) {
          throw std::logic_error("legroute's measured handoff changed before regional routing");
        }
        // Raw movable and legalized diagnostic handoffs both consume the exact
        // measured frame, including PG-aware IOs and appended supply pins.
        frame           = std::move(chosen_handoff->frame);
        terminal_report = std::move(chosen_handoff->report);
      }
      if (legal_boxes && !chosen_handoff) {  // the optional fixed-placement witness legalizes before routing
        try {
          terminal_report = terminal_legalize(tech, frame, final_gp_timing, terminal_options);
        } catch (const Terminal_legalize_unfit& e) {
          if (!options.allow_increase_area || !options.generated_core) {
            throw;
          }
          const double utilization = options.utilization * options.area_step;
          std::fprintf(stderr, "legroute: %s; placement restarts at utilization %.3f\n", e.what(), utilization);
          throw Placement_area_restart(utilization, std::min(options.density, utilization + .05));
        }
      }
      if (options.legroute) {
        auto lopt = options.legroute_options;
        if (lopt.cellbox_full_flow) {
          if (!chosen_handoff || !chosen_handoff->critical) {
            throw std::invalid_argument("full cellbox flow has no passing selected Plan handoff");
          }
          lopt.cellbox_handoff      = chosen_handoff->critical;
          lopt.cellbox_ignored_nets = prects_networks(tech, frame, v.prects, final_gp_timing).ignored;
        }
        lopt.region_cell_limit = options.region.max_cells;
        if (options.legroute_probe.enabled) {  // the second pass: the fixed trees avoid the congestion
          lopt.tree_congestion = std::max(lopt.tree_congestion, options.legroute_probe.tree_congestion);
        }
        lopt.macro_halo_x_dbu = v.halo_x;
        lopt.macro_halo_y_dbu = v.halo_y;
        lopt.output_dir       = options.output_dir;
        lopt.fail_fast        = options.legroute_probe.strict;  // strict: any failure restarts or stops the run
        Legroute_regions regions;
        if (!final_regions && !boundary_pins && load_region_seed()) {  // the refinement's mesh and virtual pins
          final_regions = load_region_seed()->mesh;
        }
        if (!final_regions) {  // a final stage without the boundary refinement: the same mesh, without virtual pins
          final_regions = Region_mesh::build(v.design,
                                             region_macros(),
                                             region_ios(),
                                             region_options_with_tracks(tech, options.region),
                                             tech.dbu);
        }
        if (final_regions->rects.size() > 1) {
          for (const auto& r : final_regions->rects) {
            regions.rects.push_back({r.xl, r.yl, r.xh, r.yh});
          }
          if (boundary_pins) {
            for (const auto& a : boundary_pins->pin_model().anchors) {
              if (a.original_net >= 0 && size_t(a.original_net) < frame.nets.size()) {
                regions.pins.push_back({size_t(a.original_net), a.x, a.y});
              }
            }
          } else if (region_seed && options.legroute_options.region_pins) {
            for (const auto& pin : region_seed->pins) {
              if (pin.net < frame.nets.size()) {
                regions.pins.push_back(pin);
              }
            }
          }
          if (!options.legroute_options.region_pins) {
            regions.pins.clear();
          }
        }
        const ph::Tech_profile* profile = nullptr;
        if (chosen_handoff) {
          // The selected frame already has PG-aware IOs and supply terminals.
          // Reuse the exact measured grid rather than append a second grid or
          // legalize its IOs again after timing selection.
          auto& saved = chosen_handoff->resources;
          if (lopt.cellbox_full_flow) {
            regions = saved;
          }
          if (!lopt.cellbox_full_flow) {
            regions.obstacles.insert(regions.obstacles.end(), saved.obstacles.begin(), saved.obstacles.end());
          }
          regions.power_nets = std::move(saved.power_nets);
          regions.power_vias = std::move(saved.power_vias);
          profile            = chosen_handoff->profile;
        } else {
          profile = legroute_power(frame, regions);
        }
        write_legroute_input(frame, regions, options.output_dir + "/legroute_handoff");
        // too dense for the tile grid or the tracks: on a generated core the placement restarts at a lower
        // utilization (the failing regions' box fill pushed down to what routes), the GP target density with it
        const bool pushback = options.allow_increase_area && options.generated_core && options.legroute_probe.strict;
        const auto restart  = [&](double factor, const std::string& why) {
          const double utilization = options.utilization * (planning ? std::max(factor, options.area_step) : factor);
          std::fprintf(stderr, "legroute: %s; placement restarts at utilization %.3f\n", why.c_str(), utilization);
          throw Placement_area_restart(utilization, utilization + .05);
        };
        // a region without a routable answer: other region meshes at the same core first (the failing crossings
        // fall elsewhere), then the smaller core
        const auto recut = [&](const std::string& why) {
          if (options.mesh_variant < std::min(2u, options.mesh_variants)) {
            std::fprintf(stderr,
                         "legroute: %s; placement restarts with region mesh variant %u\n",
                         why.c_str(),
                         options.mesh_variant + 1);
            throw Placement_area_restart(options.utilization, options.density, options.mesh_variant + 1);
          }
        };
        try {
          legroute_report = legroute(tech, frame, final_gp_timing, lopt, regions, profile);
        } catch (const Legroute_replan& e) {
          if (!pushback) {
            throw;
          }
          restart(options.area_step, std::string("pre-SAT conservative planning screen: ") + e.what());
        } catch (const Legroute_unfit& e) {
          if (!pushback) {
            throw;
          }
          restart(options.area_step, e.what());
        }
        if (options.legroute_probe.strict && !legroute_report->wires_emitted && legroute_report->global_unrouted
            && legroute_report->global_search_limited) {
          throw std::runtime_error(
              std::format("legroute: global search exhausted its budget ({} attempts, {} queue pops); "
                          "{} nets remain unresolved; area growth requires routing evidence",
                          legroute_report->global_search_limited,
                          legroute_report->global_search_pops,
                          legroute_report->global_unrouted));
        }
        if (options.legroute_probe.strict && !legroute_report->wires_emitted && legroute_report->global_topology_disconnected) {
          throw std::runtime_error(
              std::format("legroute: coarse capacity graph disconnects {} signals; repair endpoint access or placement; "
                          "this model alone does not justify automatic area growth",
                          legroute_report->global_topology_disconnected));
        }
        if (const size_t failed = legroute_report->sat_unsat_regions + legroute_report->sat_unknown_regions;
            failed && !options.legroute_probe.strict) {
          std::fprintf(stderr,
                       "legroute: %zu of %zu regions without a routable answer; keeping the placement (legroute.strict=false)\n",
                       failed,
                       legroute_report->sat_regions);
        } else if (failed && !legroute_report->sat_unsat_regions) {
          // Internal timeout recuts have already run at this same density.
          // UNKNOWN is a search limit, not evidence that the core is too small.
          throw std::runtime_error(
              std::format("legroute: SAT search budget exhausted for {} of {} regions after {} same-density partition recut rounds "
                          "at utilization {:.3f}; no model UNSAT result justifies area growth",
                          legroute_report->sat_unknown_regions,
                          legroute_report->sat_regions,
                          legroute_report->partition_timeout_rounds,
                          options.utilization));
        } else if (failed && pushback) {
          double worst = 0;  // the densest failing region's box fill: pushed to about 0.7
          for (const auto& st : legroute_report->region_stats) {
            if (st.status == 1 && st.tiles) {
              worst = std::max(worst, double(st.boxes) / double(st.tiles));
            }
          }
          const auto why = std::format(
              "{} model UNSAT and {} budget-exhausted regions of {} "
              "(densest model-UNSAT region fill {:.2f})",
              legroute_report->sat_unsat_regions,
              legroute_report->sat_unknown_regions,
              legroute_report->sat_regions,
              worst);
          recut(why);
          restart(std::clamp(.7 / std::max(worst, 1e-9), .5, options.area_step), why);
        } else if (failed) {
          // Stop on an unsuccessful model solve; UNKNOWN is not proof of unroutability.
          const bool fixed_core = !options.generated_core;
          throw std::runtime_error(
              std::format("legroute did not find a placement for {} of {} regions ({} model UNSAT, {} without an answer in "
                          "time) at utilization {:.3f}{}",
                          failed,
                          legroute_report->sat_regions,
                          legroute_report->sat_unsat_regions,
                          legroute_report->sat_unknown_regions,
                          options.utilization,
                          !options.legroute_probe.enabled ? ""
                          : !options.allow_increase_area  ? "; allow_increase_area is off"
                          : fixed_core                    ? "; the core is fixed (die, core, floorplan or previous placement given)"
                                                          : ""));
        }
        if (options.legroute_probe.strict && !legroute_report->wires_emitted
            && (legroute_report->global_unrouted || legroute_report->global_overflow_tracks)) {
          const auto why = std::format("coarse routing still has {} unrouted nets and {} overflow tracks",
                                       legroute_report->global_unrouted,
                                       legroute_report->global_overflow_tracks);
          if (pushback) {
            // Regional SAT success does not discharge the global capacity check.
            // Grow heuristically after the timed planning budget has been used.
            restart(options.area_step, why);
          }
          throw std::runtime_error("legroute: " + why);
        }
        if (legroute_report->crossing_policy_invalid) {
          // a legacy fixed-IO result whose published boundary crossings exceed the density ceiling: kept as a
          // diagnostic in legroute.json, never accepted as a placement
          throw std::runtime_error(
              std::format("legroute: {} SAT round(s) published boundary crossings over the density ceiling {} (legacy fixed-IO "
                          "construction); result not accepted",
                          legroute_report->crossing_over_policy,
                          lopt.density_ceiling));
        }
        if (options.legroute_probe.strict && lopt.final && !lopt.cellbox_plan_only
            && (!legroute_report->wires_emitted || legroute_report->connected_nets != legroute_report->routed_nets
                || legroute_report->failed_nets || legroute_report->unchecked_nets
                || legroute_report->clock_connected_nets != legroute_report->clock_required_nets
                || legroute_report->clock_failed_nets || legroute_report->clock_unchecked_nets)) {
          // A SAT placement and a clean coarse graph do not establish physical
          // closure. Preserve legroute.json, but do not publish an unrouted
          // final DEF or infer that search exhaustion requires more area.
          throw std::runtime_error(std::format(
              "legroute: strict detailed closure incomplete: {} of {} signal/clock nets connected, {} failed, {} unchecked; "
              "clocks {} of {} connected, {} failed, {} unchecked; wires_emitted={}; "
              "unresolved detailed searches do not establish insufficient area",
              legroute_report->connected_nets,
              legroute_report->routed_nets,
              legroute_report->failed_nets,
              legroute_report->unchecked_nets,
              legroute_report->clock_connected_nets,
              legroute_report->clock_required_nets,
              legroute_report->clock_failed_nets,
              legroute_report->clock_unchecked_nets,
              legroute_report->wires_emitted));
        }
        if (multirow_cells && !legal_boxes) {  // the two-row cells legroute placed take the rail twin their rows need
          sync_rail_twins(tech, frame, options);
        }
        for (const auto& n : frame.nets) {
          if (!n.wires.empty() && !n.connections.empty()) {
            auto key = std::pair{n.connections.front().instance, n.connections.front().pin};
            if (lopt.cellbox_full_flow) {
              const auto driver = std::ranges::find_if(n.connections, [&](const auto& pin) {
                return v.nets.contains(Endpoint{pin.instance, pin.pin, -1});
              });
              if (driver == n.connections.end()) {
                throw std::invalid_argument("routed source net has no placement-graph driver");
              }
              key = {driver->instance, driver->pin};
            }
            legroute_wires[key] = {n.connections, n.wires};
          }
        }
        for (const auto& c : frame.components) {
          legroute_cells.emplace(c.name, c);
        }
        legroute_rules = frame.nondefault_rules;
        for (const auto& t : frame.terminals) {
          legroute_terminals.emplace(t.name, t);
        }
      } else {
        terminal_report = terminal_legalize(tech, frame, final_gp_timing, terminal_options);
      }
      if (multirow_report) {
        release_multirow_anchors(frame, *multirow_report);
        adopt_rail_twins(v, frame);
      } else if (multirow_cells && options.legroute) {
        adopt_rail_twins(v, frame);
      }
      sat_install(v.design, frame);
      metrics.legal = native_legal(v, tech, v.design);
      metrics.hpwl  = dp::hpwl(v.design);
      metrics.alignment.cell_legalization_seconds
          = (terminal_report ? terminal_report->seconds : 0) + (legroute_report ? legroute_report->seconds : 0);
      if (!metrics.legal) {
        throw std::runtime_error(options.legroute ? "legroute failed independent native geometry validation"
                                                  : "terminal legalization failed independent native geometry validation");
      }
      if (live_session) {
        install_planned_pose(v.design, frame);
        live_session->synchronize(v.design, control, session_identities(), "legroute_local_pose");
        if (!options.output_dir.empty()) {
          live_session->write(options.output_dir + "/placement_session.json");
        }
      }
      if (terminal_report) {
        write_terminal_legalize_report(*terminal_report, frame, terminal_options, options.output_dir + "/terminal_legalize");
        // Written: only the run time and the accepted alignments are read after this. The per-cell and per-bin
        // records are a before/after copy of every cell.
        terminal_report->cells = {};
        terminal_report->bins  = {};
      }
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
    if (final_timing && options.legalize_timing_strength > 0 && !options.satlegalize && !terminal_active && !defer_legroute_cells) {
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
    capture(v,
            tech,
            options.full && !defer_cells
                && (!options.legroute_options.critical_cellboxes || options.legroute_options.cellbox_full_flow));
  } else {
    metrics.legal     = native_legal(v, tech, v.design);
    metrics.converged = false;
    capture(v,
            tech,
            options.full && !defer_cells
                && (!options.legroute_options.critical_cellboxes || options.legroute_options.cellbox_full_flow));
  }
  if (options.full && !options.timing_seed && !options.retain_placement && !options.analyze_colors_only && !terminal_active) {
    align_macro_geometry(v, tech, v.design, false, true);
    if (!defer_cells) {
      localize_tie_sites(v, tech, options.technology().cells, options.preserve_imported_ties);
    }
    metrics.legal = native_legal(v, tech, v.design);
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
    json.Key("timing_min_weight");
    json.Double(options.timing_min_weight);
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
  if (options.gui && options.full && options.completed_stage == Placement_stage::final && !options.timing_seed
      && !options.retain_placement && !options.fp_place && !defer_cells && !terminal_active) {
    gui_edit_mode(v, tech, options, result, region_macros, region_ios);
  }
  // A restored or edited placement keeps the netlist's geometry template; the
  // rows each two-row cell landed on decide its PG rail stack.
  sync_rail_twins(v, tech, options);
  {
    gu::Placement_block::Writer blocks(&graph);
    for (auto& c : v.cells) {
      c.node.attr(attrs::place).set(c.place);
      blocks.set(c.node, c.block.view());
      v.fp.components.push_back({c.name, c.macro, c.place});
    }
    blocks.flush();
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
  std::map<std::string_view, const ph::Component*> final_cells;
  if (!legroute_wires.empty()) {
    for (const auto& c : v.fp.components) {
      final_cells.emplace(c.name, &c);
    }
  }
  size_t                   legroute_kept = 0;
  std::vector<std::string> legroute_changed;
  for (size_t net = 0; net < v.drivers.size(); ++net) {
    auto name = v.drivers[net].instance == "PIN" ? v.drivers[net].pin.str() : std::format("net_{}", net);
    if (v.drivers[net].instance != "PIN") {
      // Preserve identified buses through the macro-to-final handoff when the
      // experimental final refinement is requested. Never rename input nets.
      if (options.boundarypins.enabled) {
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
    if (const auto it = legroute_wires.find({v.drivers[net].instance, v.drivers[net].pin}); it != legroute_wires.end()) {
      // keep legroute's wires only for the same connections over the same cells (legroute.final F6 lists the rest)
      bool same = it->second.connections.size() == n.connections.size()
                  && std::is_permutation(n.connections.begin(), n.connections.end(), it->second.connections.begin());
      for (const auto& c : n.connections) {
        if (!same || c.instance == "PIN") {
          continue;
        }
        const auto then = legroute_cells.find(c.instance);
        const auto now  = final_cells.find(c.instance);
        same            = then != legroute_cells.end() && now != final_cells.end() && then->second.macro == now->second->macro
                          && then->second.place.x == now->second->place.x && then->second.place.y == now->second->place.y
                          && then->second.place.orientation == now->second->place.orientation;
      }
      if (same) {
        n.wires = it->second.wires;
        ++legroute_kept;
      } else {
        legroute_changed.push_back(name);
      }
    }
    v.fp.nets.push_back(std::move(n));
  }
  // All or nothing: a net changed after legroute would leave partial wiring an external router may fail on, so
  // every legroute wire is dropped and the router starts from the legal placement.
  const bool legroute_all = !legroute_wires.empty() && legroute_changed.empty() && legroute_kept == legroute_wires.size();
  if (!legroute_wires.empty()) {
    if (legroute_all) {
      v.fp.nondefault_rules.insert(legroute_rules.begin(), legroute_rules.end());
    } else {
      for (auto& n : v.fp.nets) {
        n.wires.clear();
      }
    }
  }
  if (!legroute_wires.empty() && !options.output_dir.empty()) {
    std::ofstream report(options.output_dir + "/legroute_eco.json");
    report << std::format(R"({{"routed":{},"kept":{},"emitted":{},"changed":[)",
                          legroute_wires.size(),
                          legroute_kept,
                          legroute_all);
    for (size_t k = 0; k < legroute_changed.size(); ++k) {
      report << (k ? "," : "") << '"' << legroute_changed[k] << '"';
    }
    report << "]}\n";
  }
  // Unconnected input ports still need a DEF NET and stable terminal identity.
  for (auto& t : v.fp.terminals) {
    if (t.net.empty()) {
      t.net = t.name;
      v.fp.nets.push_back({t.net, {{"PIN", t.name}}});
    }
  }
  if (options.legroute_options.cellbox_full_flow) {
    if (!imported_frame) {
      throw std::logic_error("full cellbox source snapshot unavailable at final export");
    }
    restore_critical_source_nets(v.fp, *imported_frame);
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
  // The power grid is exported by the terminal pass alone: an optimization-pending call is followed by
  // finalize_placement, which builds the frame (a second export would find the grid already in it).
  if (pg_profile && final_handoff && !options.optimization_pending) {
    if (!handoff_rows.empty()) {
      v.fp.rows = handoff_rows;
    }
    handoff_grid            = make_power_grid(tech, v.fp, *pg_profile, &handoff_channel_stats);
    handoff_grid->terminals = handoff_pins(tech, *handoff_grid, tech_profile->io_pin_layers[0], tech_profile->io_pin_layers[1]);
    if (legroute_all) {  // legroute's wires end on the IO pins it routed to
      for (auto& t : handoff_grid->terminals) {
        if (const auto it = legroute_terminals.find(t.name); it != legroute_terminals.end()) {
          t = it->second;
        }
      }
    }
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
    if (options.legroute_options.critical_cellboxes && options.legroute_plan.enabled
        && !options.legroute_options.cellbox_full_flow) {
      check_critical_plan_frame(tech, v.fp, options);
    } else if (defer_cells) {
      check_fixed_and_macro_legality(tech, v.fp, options);
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
  if (native_session && options.native_gp_timing->legal_output_guard && !defer_legroute_cells
      && (!options.legroute_options.critical_cellboxes || options.legroute_options.cellbox_full_flow)) {
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
    json.Key("label_policy");
    json.String(resolved_uroute.label_policy.c_str());
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
    json.Key("post_density_plateau");
    json.Bool(metrics.post_density_plateau);
    json.Key("post_density_best_iteration");
    json.Int(metrics.post_density_best_iteration);
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
                 : metrics.density_stalled      ? "density stalled"
                 : metrics.route_stalled        ? "routing stalled; handing off to legalization"
                 : metrics.post_density_plateau ? "post-density plateau; drifted iterate replaced"
                 : metrics.continuation_limit   ? "post-density budget"
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
  if (placement_hints) {
    placement_hints->rebind(hint_objects());
    placement_hints->write(v.design, options.output_dir + "/metadata.json");
  } else if (options.metadata) {
    options.metadata->write(options.output_dir + "/metadata.json");
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
  result.boundary_refined   = boundary_pins.has_value();
  result.unknown_const_tied = resolver.unknown_tied();
  result.qor                = std::format(
      R"({{"schema_version":1,"kind":"placement","mode":"{}","legal":{},"gp_converged":{},"gp_overflow":{},"hpwl_um":{},"initial_hpwl_um":{},"cells":{},"nets":{},"estimated_macros":{},"virtual_fillers":{},"density_constraint":"soft","timing_model":"placement_rc","timing_approximate":{},"rc_identity":"{}","incremental":{{"enabled":{},"hit":{},"reused_blocks":{},"changed_blocks":{}}}}})",
      options.retain_placement ? "timing_snapshot"
      : options.timing_seed    ? "post_synthesis_zero_wire"
      : options.full           ? "full"
                               : "gp",
      (options.timing_seed || options.retain_placement || defer_legroute_cells) ? "false"
      : options.full && !defer_macro_cells                                      ? "true"
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
                              : metrics.post_density_plateau                                              ? "post_density_plateau"
                              : metrics.continuation_limit                                                ? "continuation_limit"
                              : metrics.iterations >= control.max_iterations                              ? "iteration_limit"
                                                                                                          : "stopped";
    json.AddMember("gp_stop_reason", rapidjson::Value(stop_reason, allocator), allocator);
    add_uroute_status(json, allocator, metrics.uroute_skipped, metrics.uroute_sample_ids);
    json.AddMember(
        "route_model",
        cache.hit ? rapidjson::Value(rapidjson::kNullType) : rapidjson::Value(dp::route_model_name(metrics.route_model), allocator),
        allocator);
    json.AddMember("cells_legalized", options.full && !defer_cells && !options.timing_seed && !options.retain_placement, allocator);
    json.AddMember("cell_legalization_deferred", defer_legroute_cells, allocator);
    json.AddMember("macro_gp_timing_for_resynthesis", bool(result.macro_gp_timing), allocator);
    json["virtual_fillers"].SetBool(options.virtual_fillers);
    json.AddMember("fp_warpspace", options.fp_warp.enabled, allocator);
    json.AddMember("macro_warpspace", options.macro_warp.enabled, allocator);
    if (options.macro_anchor.enabled) {
      json.AddMember("macro_anchor", true, allocator);
    }
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
    halo.AddMember("macros",
                   static_cast<uint64_t>(std::count_if(
                       v.cells.begin(),
                       v.cells.end(),
                       [&](const Cell& c) { return (v.halo_x || v.halo_y) && ph::hard_macro(tech.macros.at(c.macro)); })),
                   allocator);
    json.AddMember("halo", halo, allocator);
    rapidjson::Value channel(rapidjson::kObjectType);
    channel.AddMember("x_um", double(v.channel_x) / tech.dbu, allocator);
    channel.AddMember("y_um", double(v.channel_y) / tech.dbu, allocator);
    channel.AddMember("x_dbu", v.channel_x, allocator);
    channel.AddMember("y_dbu", v.channel_y, allocator);
    channel.AddMember("auto", !options.macro_channel, allocator);
    json.AddMember("macro_channel", channel, allocator);
    {
      rapidjson::Value pdn(rapidjson::kObjectType);
      const auto       used_level = v.pdn_ladder.empty() ? requested_pdn_level(options) : v.pdn_ladder[v.pdn_level].level;
      pdn.AddMember("mode", rapidjson::Value(options.macro_pdn.c_str(), allocator), allocator);
      pdn.AddMember("requested", rapidjson::Value(options.macro_pdn.c_str(), allocator), allocator);
      pdn.AddMember("used_level", rapidjson::StringRef(pg_macro_level_name(used_level)), allocator);
      pdn.AddMember("degraded", options.macro_pdn == "auto" && v.pdn_rules && used_level != Pg_macro_level::gap, allocator);
      {
        rapidjson::Value attempts(rapidjson::kArrayType);
        auto             tried = v.pdn_attempts;
        if (!v.pdn_ladder.empty() && (tried.empty() || tried.back().result != "frame")) {
          const auto& used = v.pdn_ladder[v.pdn_level];
          tried.push_back({used.level, used.channel_x, used.channel_y, "used"});
        }
        for (const auto& a : tried) {
          rapidjson::Value entry(rapidjson::kObjectType);
          entry.AddMember("level", rapidjson::StringRef(pg_macro_level_name(a.level)), allocator);
          entry.AddMember("channel_x_um", double(a.channel_x) / tech.dbu, allocator);
          entry.AddMember("channel_y_um", double(a.channel_y) / tech.dbu, allocator);
          entry.AddMember("result", rapidjson::Value(a.result.c_str(), allocator), allocator);
          attempts.PushBack(entry, allocator);
        }
        pdn.AddMember("attempts", attempts, allocator);
      }
      pdn.AddMember("active", v.pdn_rules.has_value() && used_level != Pg_macro_level::off, allocator);
      if (v.pdn_rules) {
        const char* axes[]  = {"x", "y"};
        const auto  lattice = macro_pdn_alignment(v, tech);
        for (int axis = 0; axis < 2; ++axis) {
          const auto&      rule = v.pdn_rules->gap[axis];
          rapidjson::Value a(rapidjson::kObjectType);
          const char*      kind = rule.kind == Pg_gap_rule::Kind::lattice  ? "lattice"
                                  : rule.kind == Pg_gap_rule::Kind::feeder ? "feeder"
                                                                           : "none";
          a.AddMember("kind", rapidjson::StringRef(kind), allocator);
          a.AddMember("source", rapidjson::Value(rule.source.c_str(), allocator), allocator);
          a.AddMember("feed", rapidjson::Value(rule.feed.c_str(), allocator), allocator);
          a.AddMember("required_um", pg_macro_requirement(rule, used_level), allocator);
          a.AddMember("aligned_um", rule.aligned, allocator);
          a.AddMember("any_phase_um", rule.any_phase, allocator);
          if (lattice && lattice->axis[axis]) {
            // Hard macros whose final edges the strap lattice does not admit.
            const auto& l       = *lattice->axis[axis];
            int64_t     core_lo = std::numeric_limits<int64_t>::max(), core_hi = std::numeric_limits<int64_t>::min();
            for (const auto& row : v.fp.rows) {
              const auto height = std::llround(tech.sites.at(row.site).height * tech.dbu);
              core_lo           = std::min<int64_t>(core_lo, axis ? row.y : row.x);
              core_hi           = std::max<int64_t>(core_hi, axis ? row.y + height : row.x + row.count * row.step);
            }
            uint64_t unaligned = 0;
            for (const auto& c : v.cells) {
              const auto& master = tech.macros.at(c.macro);
              if (!master.klass.starts_with("BLOCK") || ph::soft_block(master)) {
                continue;  // pads, covers and end caps are not lattice macros
              }
              const auto low = axis ? c.place.y : c.place.x, size = axis ? c.place.height : c.place.width;
              unaligned += !l.allows(low, low + size, core_lo, core_hi);
            }
            a.AddMember("lattice_origin_um", double(l.origin) / tech.dbu, allocator);
            a.AddMember("unaligned_macros", unaligned, allocator);
          }
          pdn.AddMember(rapidjson::StringRef(axes[axis]), a, allocator);
        }
        pdn.AddMember("alignments", v.pdn_alignments, allocator);
        pdn.AddMember("alignment_fallbacks", v.pdn_fallbacks, allocator);
      }
      json.AddMember("macro_pdn", pdn, allocator);
    }
    if (options.macro_edge_snap) {
      rapidjson::Value edge(rapidjson::kObjectType);
      edge.AddMember("x_um", double(std::max<int64_t>(0, v.edge_snap_x)) / tech.dbu, allocator);
      edge.AddMember("y_um", double(std::max<int64_t>(0, v.edge_snap_y)) / tech.dbu, allocator);
      edge.AddMember("auto_x", options.macro_edge_snap->first < 0, allocator);
      edge.AddMember("auto_y", options.macro_edge_snap->second < 0, allocator);
      edge.AddMember("moved_macros", v.edge_snap_moved, allocator);
      edge.AddMember("axis_moves", v.edge_snap_moves, allocator);
      edge.AddMember("distance_um", double(v.edge_snap_distance) / tech.dbu, allocator);
      json.AddMember("macro_edge_snap", edge, allocator);
    }
    if (options.macro_refine_rounds) {
      rapidjson::Value refine(rapidjson::kObjectType), rounds(rapidjson::kArrayType);
      refine.AddMember("rounds", options.macro_refine_rounds, allocator);
      refine.AddMember("iterations", options.macro_refine_iterations, allocator);
      for (const auto& r : v.refine_rounds) {
        rapidjson::Value j(rapidjson::kObjectType);
        j.AddMember("gp_iterations", r.iterations, allocator);  // -1: the round failed and was dropped
        j.AddMember("rejected", r.rejected, allocator);
        j.AddMember("hpwl_um", r.hpwl / tech.dbu, allocator);
        j.AddMember("overflow", r.overflow, allocator);
        j.AddMember("density_weight", r.density_weight, allocator);
        j.AddMember("dead_um2", r.dead_area < 0 ? -1.0 : double(r.dead_area) / tech.dbu / tech.dbu, allocator);
        j.AddMember("moved_macros", r.moved_macros, allocator);
        j.AddMember("macro_displacement_um", double(r.macro_displacement) / tech.dbu, allocator);
        rounds.PushBack(j, allocator);
      }
      refine.AddMember("history", rounds, allocator);
      json.AddMember("macro_refine", refine, allocator);
    }
    if (options.macro_dead_space) {
      json.AddMember("macro_dead_space", dead_space_json(v, tech, v.design, *options.macro_dead_space, allocator), allocator);
    }
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
    if (use_pcrs) {
      // pass.pcrs: one balanced flop tree per (reset root, clock domain). `depth` is the flop levels every sink of
      // the domain sees, so post-placement simulation must hold reset for at least depth + 1 cycles.
      rapidjson::Value block(rapidjson::kObjectType);
      block.AddMember("fanout", options.reset_fanout, allocator);
      block.AddMember("networks", pcrs_stats.networks, allocator);
      block.AddMember("sinks", pcrs_stats.sinks, allocator);
      block.AddMember("flops_added", pcrs_stats.flops, allocator);
      block.AddMember("buffers_added", pcrs_stats.buffers, allocator);
      block.AddMember("removed", pcrs_stats.removed, allocator);
      block.AddMember("skipped", pcrs_stats.skipped, allocator);
      block.AddMember("flops",
                      static_cast<uint64_t>(std::count_if(v.cells.begin(), v.cells.end(), [](const Cell& c) {
                        return pcrs::instance_leaf(c.name).starts_with("__pcrs_rstf");
                      })),
                      allocator);
      unsigned max_depth = 0;
      rapidjson::Value domains(rapidjson::kArrayType);
      for (const auto& d : pcrs_stats.domains) {
        rapidjson::Value one(rapidjson::kObjectType);
        one.AddMember("name", rapidjson::Value(d.name.c_str(), allocator), allocator);
        one.AddMember("depth", d.depth, allocator);
        one.AddMember("sinks", d.sinks, allocator);
        one.AddMember("flops", d.flops, allocator);
        one.AddMember("buffers", d.buffers, allocator);
        domains.PushBack(one, allocator);
        max_depth = std::max(max_depth, d.depth);
      }
      block.AddMember("max_depth", max_depth, allocator);
      block.AddMember("domains", domains, allocator);
      json.AddMember("pcrs", block, allocator);
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
        rail_twins       += cell.rail_twin;
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
  if (result.parasitics && !options.timing_seed && options.persist_parasitics) {
    result.graph->get_input_node().attr(attrs::placement_rc).set(ph::serialize_parasitics(*result.parasitics));
  } else if (!options.persist_parasitics) {
    // A copied input may carry an obsolete annotation. The fused optimizer
    // uses the current in-memory RC and persists only its accepted final state.
    result.graph->get_input_node().attr(attrs::placement_rc).del();
  }
  if (notify_after_stage && options.after_stage && !options.timing_seed && options.after_stage(options.completed_stage, result)) {
    throw Placement_restart(result.floorplan, options.completed_stage);
  }
  return result;
}

Result place(hhds::Graph& source, hhds::GraphLibrary& library, const Options& requested) {
  return place(source, library, requested, {});
}

static Result place_requested(hhds::Graph& source, hhds::GraphLibrary& library, const Options& requested,
                              const std::function<void()>& release_source) {
  if (area_fit_applies(requested)) {  // place.area=auto: the smallest routable region of the imported core
    return place_area_fit(source, library, requested, release_source);
  }
  if (requested.legroute_options.cellbox_full_flow
      && (!requested.legroute_options.critical_cellboxes || !requested.legroute || !requested.legroute_plan.enabled
          || !requested.legroute_options.final || requested.floorplan.empty())) {
    throw std::invalid_argument("cellbox_full_flow requires complete source floorplan, critical Plan and physical Final");
  }
  if (requested.legroute_options.cellbox_diagnostic_final
      && (!requested.legroute_options.cellbox_full_flow || !requested.legroute_options.final)) {
    throw std::invalid_argument("cellbox_diagnostic_final requires cellbox_full_flow and physical Final");
  }
  if (requested.legroute_options.cellbox_far_fallback
      && (!requested.legroute_options.critical_cellboxes || !requested.legroute_plan.enabled
          || requested.legroute_plan.rounds != 3)) {
    throw std::invalid_argument("cellbox_far_fallback requires critical cellboxes and exactly three Plan GP retries");
  }
  if (requested.legroute_options.critical_cellboxes && !requested.timing_seed && !requested.retain_placement
      && (!requested.legroute_plan.enabled || requested.legroute_plan.rounds > 3 || requested.legroute_plan.handoff
          || requested.legroute_options.legal_boxes || !requested.fp_evaluate)) {
    throw std::invalid_argument(
        "critical_cellboxes requires movable Plan, detailed STA, no legal_boxes/handoff and at most 3 GP retries");
  }
  if ((requested.legroute_plan.route_model != "legacy" && requested.legroute_plan.route_model != "coarse")
      || !requested.legroute_plan.route_interval || !requested.legroute_plan.route_net_pops) {
    throw std::invalid_argument("legroute.plan route_model must be legacy or coarse with positive route budgets");
  }
  if (requested.legroute_plan.enabled && requested.legroute_plan.handoff && !requested.legroute_options.legal_boxes) {
    throw std::invalid_argument("legroute plan.handoff is a legalized diagnostic and requires legal_boxes=true");
  }
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
  validate_region_options(requested.region);
  validate_boundary_pin_options(requested.boundarypins);
  validate_warp_options(requested.fp_warp);
  validate_warp_options(requested.macro_warp);
  if (requested.macro_anchor.enabled) {
    validate_macro_anchor_options(requested.macro_anchor);
    if (requested.macro_warp.enabled || requested.macro_magnet.enabled) {
      throw std::invalid_argument("macro.anchor cannot be combined with macro.warpspace or macro.magnet");
    }
  }
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
  if (requested.legroute_options.critical_cellboxes) {
    resolved_options                      = requested;
    resolved_options->allow_increase_area = false;
  }
  if (requested.cell_halo || requested.boundarypins.align) {
    if (!resolved_options) {
      resolved_options = requested;
    }
    if (requested.boundarypins.align) {
      resolved_options->boundarypins.enabled = true;
    }
  }
  // the staged final-cell pass imports the macro pass's frame: whether the core may grow is decided here
  if (requested.allow_increase_area && (requested.legroute_probe.enabled || requested.legroute_plan.enabled)) {
    if (!resolved_options) {
      resolved_options = requested;
    }
    // (the staged final pass calls place() again with the macro frame: keep the outer decision)
    resolved_options->generated_core = requested.generated_core
                                       || (requested.floorplan.empty() && requested.die.empty() && requested.core.empty()
                                           && !requested.previous && !requested.timing_seed);
  }
  if (requested.legroute_plan.enabled && !requested.placement_session) {
    if (!resolved_options) {
      resolved_options = requested;
    }
    resolved_options->placement_session = std::make_shared<Placement_session>();
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
  if (!options.lefs.empty()) {
    // Soft blocks cannot be row-legalized: macro planning defers its cell legalization/detail, and the
    // final cell pass re-places every cell and soft block from the macro-stage GP seed anyway.
    const auto& technology = options.technology().lef;
    bool        soft       = false;
    for (const auto& graph : source.definitions().graphs()) {
      for (auto node : graph->body().nodes()) {
        if (auto io = node.get_subnode_io()) {
          const auto found  = technology.macros.find(std::string(io->get_name()));
          soft             |= found != technology.macros.end() && ph::soft_block(found->second);
        }
      }
    }
    if (soft) {
      macro_options.macro_legalize_cells = false;
      macro_options.macro_detailed_place = false;
    }
  }
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
    if (master == tech.macros.end() || ph::hard_macro(master->second)) {
      c.place.status = Status::FIXED;
      ++fixed_macros;
    }
    // A soft block stays movable: final GP moves it with the cells (seeded below).
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
  if (!macro_options.macro_detailed_place || !macro_options.macro_legalize_cells) {
    // Final placement rebuilds movable cells/connectivity from source and
    // imports only fixed poses. Pass the actual handoff, not provisional cells.
    std::erase_if(macro.floorplan.components,
                  [](const auto& cell) { return cell.place.status != Status::FIXED && cell.place.status != Status::LOCKED; });
    macro.floorplan.nets.clear();
  }
  const auto frame = macro_options.output_dir + "/fixed.def";
  ph::write_def(macro.floorplan, frame);
  auto cell_options               = final_cell_options(staged_options);
  cell_options.metadata_finalized = true;
  if (options.metadata && fs::is_regular_file(macro_options.output_dir + "/metadata.json")) {
    cell_options.metadata
        = std::make_shared<const metadata::Bundle>(metadata::Bundle::read(macro_options.output_dir + "/metadata.json"));
  }
  cell_options.uroute_pool      = macro_options.uroute_pool;
  cell_options.stage_pin_status = std::move(pin_status);
  if (!options.final_independent_timing) {
    cell_options.net_weights = macro.net_weights;
  }
  cell_options.fp_place               = false;
  cell_options.fp_auto                = false;
  // FP centroids and macro warp pulls end at the fixed-macro handoff.
  cell_options.fp_warp.enabled        = false;
  cell_options.macro_warp.enabled     = false;
  cell_options.macro_anchor.enabled   = false;
  cell_options.macro_filler_fraction  = 0;
  cell_options.macro_fill_cell_space  = false;
  cell_options.macro_magnet.enabled   = false;
  cell_options.completed_stage        = Placement_stage::final;
  cell_options.restart_seed           = cell_seed;
  // Only this completed macro-plan handoff may replace fixed macro poses in
  // the retained cell optimizer. Ordinary rebinds keep fixed bodies immutable.
  cell_options.placement_macro_replan = options.placement_area_resume;
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
  result.fp_resized_cells                = macro.fp_resized_cells;
  result.fp_repair_edits                 = macro.fp_repair_edits;
  {
    // macro.pdn is decided by the macro plan (the final pass only reads the level off the fixed frame): report the
    // plan's request, level and attempts.
    rapidjson::Document macro_qor, final_qor;
    macro_qor.Parse(macro.qor.c_str());
    final_qor.Parse(result.qor.c_str());
    if (macro_qor.IsObject() && final_qor.IsObject() && macro_qor.HasMember("macro_pdn")) {
      auto& allocator = final_qor.GetAllocator();
      final_qor.RemoveMember("macro_pdn");
      final_qor.AddMember("macro_pdn", rapidjson::Value(macro_qor["macro_pdn"], allocator), allocator);
      rapidjson::StringBuffer                    buffer;
      rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
      final_qor.Accept(writer);
      result.qor = buffer.GetString();
      if (!options.output_dir.empty()) {
        std::ofstream qor(options.output_dir + "/qor.json");
        if (!(qor << result.qor << '\n')) {
          throw std::runtime_error("cannot write staged macro.pdn QoR");
        }
      }
    }
  }
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
      macro_options.macro_detailed_place,
      macro_options.macro_legalize_cells,
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
// synth.place.fp=auto: the floorplan stage runs only for a design whose
// flop+memory area is dominated by repeated hierarchy (fp_repetition.hpp).
// Fresh full placement decides; ECO/seed/placed-context and h2d calls never
// start a floorplan, so auto resolves to off there without detection. The
// native GP pin-timing experiment rejects a floorplan stage, so auto is off
// there as well.
static Result place_fp_auto(hhds::Graph& source, hhds::GraphLibrary& library, const Options& requested,
                            const std::function<void()>& release_source) {
  if (!requested.fp_auto) {
    return place_requested(source, library, requested, release_source);
  }
  if (requested.fp_place) {  // an explicit floorplan request overrides the auto default
    auto forced    = requested;
    forced.fp_auto = false;
    return place_requested(source, library, forced, release_source);
  }
  auto resolved    = requested;
  resolved.fp_auto = false;
  std::string report;
  if (requested.full && !requested.previous && !requested.timing_seed && !requested.retain_placement && !requested.cell_stage
      && requested.macro_mode == "none" && !requested.native_gp_timing && !requested.lefs.empty()) {
    auto auto_options          = requested.fp_auto_options;
    auto_options.fp_min_macros = requested.fp_min_macros;  // what candidate discovery will accept
    auto_options.fp_min_copies = requested.fp_min_copies;
    const auto& tech           = requested.technology().lef;
    const auto  decision       = detect_fp_repetition(fp_repetition_design(source, tech, requested.liberty), auto_options);
    resolved.fp_place          = decision.enabled;
    report                     = fp_rep_json(decision, auto_options);
    livehd::diag::info("pass.place", "fp-auto", "progress")
        .msg("synth.place.fp=auto: floorplan {} (repeated flop+memory share {:.1f}%, as floorplan candidates {:.1f}%)",
             decision.enabled ? "on" : "off",
             100 * decision.share,
             100 * decision.candidate_share)
        .emit();
    if (!requested.output_dir.empty()) {
      fs::create_directories(requested.output_dir);
      std::ofstream out(requested.output_dir + "/fp_auto.json");
      if (!(out << report << '\n')) {
        throw std::runtime_error("cannot write fp_auto.json");
      }
    }
  }
  auto result = place_requested(source, library, resolved, release_source);
  if (!report.empty()) {
    result.fp_auto = report;
    rapidjson::Document qor, detail;
    qor.Parse(result.qor.c_str());
    detail.Parse(report.c_str());
    if (qor.IsObject() && detail.IsObject()) {
      qor.AddMember("fp_auto", rapidjson::Value(detail, qor.GetAllocator()), qor.GetAllocator());
      rapidjson::StringBuffer                    buffer;
      rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
      qor.Accept(writer);
      result.qor = buffer.GetString();
      if (!requested.output_dir.empty()) {
        std::ofstream out(requested.output_dir + "/qor.json");
        if (!(out << result.qor << '\n')) {
          throw std::runtime_error("cannot write placement QoR");
        }
      }
    }
  }
  return result;
}

Result place(hhds::Graph& source, hhds::GraphLibrary& library, const Options& requested, const std::function<void()>& release_source) {
  auto result = place_fp_auto(source, library, requested, release_source);
  if (requested.gui) {
    // The session ends on the placement the flow produced (cells by name; the IO pads are already where the frames left them).
    std::vector<std::tuple<std::string, double, double>> placed;
    placed.reserve(result.floorplan.components.size());
    for (const auto& c : result.floorplan.components) {
      placed.emplace_back(c.name, double(c.place.x), double(c.place.y));
    }
    requested.gui->set_result(placed);
  }
  return result;
}
}  // namespace livehd::placement
