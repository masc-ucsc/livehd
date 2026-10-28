// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Native repair-only diagnostic: retain a supplied placement and compare fresh
// placement-RC timing before/after repair, without synthesis or global placement.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <stdexcept>

#include "attrs.hpp"
#include "graph_library_singleton.hpp"
#include "inou/lefdef/mapped_graph.hpp"
#include "node_util.hpp"
#include "pass/opentimer/pass_opentimer.hpp"
#include "pass/place/growth_budget.hpp"
#include "pass/place/placement_technology.hpp"
#include "pass/place/prects_networks.hpp"
#include "pass/place/repair.hpp"
#include "pass/place/resize_engine.hpp"
#include "pass/place/track_capacity.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace pl = livehd::placement;
namespace ph = livehd::physical;
std::string read(const std::string& path) {
  std::ifstream file(path);
  if (!file) {
    throw std::runtime_error("cannot read " + path);
  }
  return {std::istreambuf_iterator<char>(file), {}};
}

Eprp_var timing_options(const std::string& liberty, const std::string& sdc, const std::string& top) {
  Eprp_var options;
  options.dict = {
      {"files", liberty + (sdc.empty() ? "" : "," + sdc)},
      {  "top",                                      top},
      { "hier",                                  "false"}
  };
  options.set_stage_labels(options.dict);
  return options;
}
pl::Timing_feedback evaluate_timing(const pl::Result& placed, livehd::opentimer::Physical_session& session, unsigned& serial,
                                    const ph::Technology* placement_tech = nullptr, const std::string& profile_path = {},
                                    livehd::opentimer::Physical_session::Purpose purpose
                                    = livehd::opentimer::Physical_session::Purpose::detailed,
                                    const pl::Prects_networks* cached_controls = nullptr) {
  const auto started = std::chrono::steady_clock::now();
  const auto before  = session.statistics();
  ++serial;
  pl::Timing_feedback result;
  if (cached_controls) {
    result = pl::prects_data_timing(session.evaluate(placed.graph, placed.parasitics, cached_controls->timing_cuts, purpose),
                                    *cached_controls);
  } else if (placement_tech) {
    const auto controls = pl::prects_networks(*placement_tech, placed.floorplan, placed.prects);
    result = pl::prects_data_timing(session.evaluate(placed.graph, placed.parasitics, controls.timing_cuts, purpose), controls);
  } else {
    result = session.evaluate(placed.graph, placed.parasitics, {}, purpose);
  }
  if (!profile_path.empty()) {
    const auto&   after = session.statistics();
    std::ofstream out(profile_path, std::ios::app);
    out << "{\"evaluation\":" << serial << ",\"ranking_only\":" << (result.ranking_only ? "true" : "false")
        << ",\"wall_s\":" << std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count()
        << ",\"engine_s\":" << after.seconds - before.seconds
        << ",\"validation_s\":" << after.validation_seconds - before.validation_seconds
        << ",\"reconcile_s\":" << after.reconcile_seconds - before.reconcile_seconds
        << ",\"exclusions_s\":" << after.exclusions_seconds - before.exclusions_seconds
        << ",\"propagation_s\":" << after.propagation_seconds - before.propagation_seconds
        << ",\"feedback_s\":" << after.feedback_seconds - before.feedback_seconds
        << ",\"update_calls\":" << after.update_calls - before.update_calls
        << ",\"updated_rc_nets\":" << after.updated_nets - before.updated_nets << ",\"rc_nets\":" << placed.parasitics->nets.size()
        << "}\n";
    if (!out) {
      throw std::runtime_error("cannot write timing profile");
    }
  }
  return result;
}

// Import the mapped DEF connectivity directly: no HDL optimization may delete
// dangling or redundant cells in a timing-engine comparison.
std::shared_ptr<hhds::Graph> mapped_def(hhds::GraphLibrary& library, const ph::Floorplan& fp, const ph::Technology& tech) {
  return ph::mapped_graph(library, fp, tech);
}

// Geometry-only export for the native RUDY evaluator and placement PNGs. It
// uses the same LEF pin centers, ideal-clock masks and layer capacities as GP.
void physical_snapshot(const ph::Technology& tech, const ph::Floorplan& fp, const std::string& root,
                       const std::set<std::string>& ignored_nets = {}) {
  if (fp.rows.empty() || fp.dbu != tech.dbu) {
    throw std::invalid_argument("physical snapshot requires rows and matching LEF/DEF units");
  }
  const auto&  row = fp.rows.front();
  const double sw = row.step, rh = std::llround(tech.sites.at(row.site).height * tech.dbu);
  const auto [capacity_h, capacity_v] = pl::lef_track_capacity(tech);
  std::ofstream out(root + "/physical_snapshot.txt"), boxes(root + "/boxes.tsv");
  out << std::setprecision(17) << row.x << ' ' << row.y << ' ' << row.x + row.count * row.step << ' ' << fp.rows.back().y + rh
      << ' ' << sw << ' ' << rh << ' ' << capacity_h << ' ' << capacity_v << " 64\n"
      << fp.components.size() + fp.terminals.size() << '\n';
  std::map<std::string, int> cells, terminals;
  for (size_t i = 0; i < fp.components.size(); ++i) {
    const auto& c       = fp.components[i];
    const auto& m       = tech.macros.at(c.macro);
    const bool  macro   = !m.klass.starts_with("CORE");
    const auto [bh, bv] = macro ? pl::lef_track_capacity(tech, m.blocked_layers) : std::pair{0., 0.};
    cells.emplace(c.name, i);
    // Placement halos reserve whitespace for access/routing; only the physical
    // macro body is an opaque routing obstacle.
    out << c.place.x << ' ' << c.place.y << ' ' << c.place.width << ' ' << c.place.height << ' ' << macro << ' ' << bh << ' ' << bv
        << '\n';
    boxes << c.name << '\t' << c.macro << '\t' << c.place.x << '\t' << c.place.y << '\t' << c.place.width << '\t' << c.place.height
          << '\t' << macro << '\n';
  }
  for (size_t i = 0; i < fp.terminals.size(); ++i) {
    const auto& t = fp.terminals[i];
    terminals.emplace(t.name, i);
    out << t.x << ' ' << t.y << " 0 0 0 0 0\n";
  }
  const auto measured = [&](const ph::Net& net) {
    return !ignored_nets.contains(net.name) && net.connections.size() >= 2
           && std::none_of(net.connections.begin(), net.connections.end(), [&](const auto& c) {
                return c.instance != "PIN"
                       && tech.macros.at(fp.components.at(cells.at(c.instance)).macro).pins.at(c.pin).use == "CLOCK";
              });
  };
  out << std::count_if(fp.nets.begin(), fp.nets.end(), measured) << '\n';
  for (const auto& net : fp.nets) {
    if (!measured(net)) {
      continue;
    }
    out << net.connections.size();
    for (const auto& endpoint : net.connections) {
      if (endpoint.instance == "PIN") {
        const auto  i = terminals.at(endpoint.pin);
        const auto& t = fp.terminals[i];
        out << ' ' << fp.components.size() + i << ' ' << t.x << ' ' << t.y;
      } else {
        const auto  i   = cells.at(endpoint.instance);
        const auto& c   = fp.components[i];
        const auto& m   = tech.macros.at(c.macro);
        const auto& pin = m.pins.at(endpoint.pin);
        if (pin.shapes.empty()) {
          throw std::runtime_error("physical snapshot pin has no LEF geometry");
        }
        const auto [x, y] = ph::oriented_center(m, pin.shapes.front().rect, c.place.orientation);
        out << ' ' << i << ' ' << c.place.x + x * tech.dbu << ' ' << c.place.y + y * tech.dbu;
      }
    }
    out << '\n';
  }
  if (!out || !boxes) {
    throw std::runtime_error("cannot write physical snapshot");
  }
}

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      throw std::invalid_argument("usage: physical_repair config.json");
    }
    rapidjson::Document config;
    config.Parse(read(argv[1]).c_str());
    if (config.HasParseError() || !config.IsObject()) {
      throw std::invalid_argument("invalid repair config");
    }
    // Partition keys follow the lhd grid/region/bin names (synth.place.grid.density
    // is grid_density). Pre-3g keys stay accepted; giving both spellings is an error.
    const std::pair<const char*, const char*> renamed_keys[] = {
        {                            "bins",                            "grid_density"},
        {                         "bin_max",                        "grid_density_max"},
        {              "local_density_bins",                              "grid_local"},
        {                        "busorder",                         "region_busorder"},
        {                 "busorder_weight",                  "region_busorder_weight"},
        {             "busorder_iterations",              "region_busorder_iterations"},
        {                "busorder_refresh",                 "region_busorder_refresh"},
        {    "busorder_max_displacement_um",     "region_busorder_max_displacement_um"},
        {              "busorder_max_cells",                        "region_max_cells"},
        {               "busorder_merge_um",                         "region_merge_um"},
        {                    "boundarypins",                     "region_boundarypins"},
        {              "boundarypins_split",               "region_boundarypins_split"},
        {              "boundarypins_order",               "region_boundarypins_order"},
        {         "boundarypins_iterations",          "region_boundarypins_iterations"},
        {"boundarypins_max_displacement_um", "region_boundarypins_max_displacement_um"},
        {                        "pinalign",                         "region_pinalign"},
        {            "legalize_max_density",                "legalize_bin_max_density"},
        {         "satlegalize_bin_size_um",                 "satlegalize_bin_size_um"},
    };
    auto& allocator = config.GetAllocator();
    for (const auto& [old_key, new_key] : renamed_keys) {
      if (!config.HasMember(old_key)) {
        continue;
      }
      if (config.HasMember(new_key)) {
        throw std::invalid_argument(std::format("{} conflicts with its deprecated spelling {}", new_key, old_key));
      }
      rapidjson::Value value(config[old_key], allocator);
      config.RemoveMember(old_key);
      config.AddMember(rapidjson::Value(new_key, allocator), value, allocator);
    }
    if (config.HasMember("satlegalize_route_grid")) {
      if (!config["satlegalize_route_grid"].IsBool() || config.HasMember("satlegalize_bin_grid")) {
        throw std::invalid_argument("deprecated satlegalize_route_grid requires a boolean and no satlegalize_bin_grid");
      }
      rapidjson::Value grid(config["satlegalize_route_grid"].GetBool() ? "route" : "none", allocator);
      config.RemoveMember("satlegalize_route_grid");
      config.AddMember("satlegalize_bin_grid", grid, allocator);
    }
    const auto text = [&](const char* key) -> std::string {
      if (!config.HasMember(key) || !config[key].IsString()) {
        throw std::invalid_argument(std::string("missing string: ") + key);
      }
      return config[key].GetString();
    };
    const auto root = text("output"), top = text("top"), sdc = text("sdc");
    std::filesystem::create_directories(root);
    const bool  floorplan_mode = config.HasMember("floorplan_place") && config["floorplan_place"].GetBool();
    pl::Options options;
    // Expose existing production spreading controls to this diagnostic without
    // changing their defaults. Type errors must fail instead of being ignored.
    const auto  number_option = [&](const char* key, double& value) {
      if (config.HasMember(key)) {
        if (!config[key].IsNumber()) {
          throw std::invalid_argument(std::string(key) + " requires a number");
        }
        value = config[key].GetDouble();
      }
    };
    const auto integer_option = [&](const char* key, int& value) {
      if (config.HasMember(key)) {
        if (!config[key].IsInt()) {
          throw std::invalid_argument(std::string(key) + " requires an integer");
        }
        value = config[key].GetInt();
      }
    };
    const auto boolean_option = [&](const char* key, bool& value) {
      if (config.HasMember(key)) {
        if (!config[key].IsBool()) {
          throw std::invalid_argument(std::string(key) + " requires a boolean");
        }
        value = config[key].GetBool();
      }
    };
    bool macro_only = false;
    boolean_option("macro_only", macro_only);
    if (macro_only && !floorplan_mode) {
      throw std::invalid_argument("macro_only requires floorplan_place");
    }
    if (config.HasMember("eco_trace_cells")) {
      if (!config["eco_trace_cells"].IsArray()) {
        throw std::invalid_argument("eco_trace_cells requires an array of exact cell names");
      }
      for (const auto& name : config["eco_trace_cells"].GetArray()) {
        if (!name.IsString() || !name.GetStringLength()) {
          throw std::invalid_argument("eco_trace_cells requires nonempty exact cell names");
        }
        options.eco_trace_cells.emplace_back(name.GetString(), name.GetStringLength());
      }
    }
    bool prects_timing = false;
    boolean_option("prects_timing", prects_timing);
    if (prects_timing
        && (!(config.HasMember("timing_only") && config["timing_only"].GetBool())
            || (config.HasMember("source_def") && config["source_def"].GetBool()))) {
      throw std::invalid_argument("prects_timing requires timing_only and a mapped LGraph with synthesis control metadata");
    }
    bool retained_repair = false;
    boolean_option("retained_repair", retained_repair);
    if (retained_repair
        && (floorplan_mode || (config.HasMember("cell_place") && config["cell_place"].GetBool())
            || (config.HasMember("timing_only") && config["timing_only"].GetBool())
            || (config.HasMember("snapshot_only") && config["snapshot_only"].GetBool()) || !config.HasMember("net_weights"))) {
      throw std::invalid_argument(
          "retained_repair requires explicit net_weights and no placement, timing_only or snapshot_only mode");
    }
    bool forest_only = false, sizing_forest = false;
    if (config.HasMember("repair_scope")) {
      const auto scope = text("repair_scope");
      if (scope != "forest" && scope != "sizing_forest") {
        throw std::invalid_argument("repair_scope supports only forest or sizing_forest");
      }
      if (!retained_repair) {
        throw std::invalid_argument("repair_scope requires retained_repair");
      }
      forest_only   = scope == "forest";
      sizing_forest = scope == "sizing_forest";
    }
    number_option("density", options.density);
    if (config.HasMember("density_weight_seed")) {
      double seed = 0;
      number_option("density_weight_seed", seed);
      options.density_weight_seed = seed;
    }
    number_option("rc_pessimism", options.rc_pessimism);
    integer_option("grid_density", options.bins);
    integer_option("threads", options.threads);
    integer_option("final_iterations", options.final_iterations);
    boolean_option("dynamic_bins", options.dynamic_bins);  // deprecated: grid_density_mode
    boolean_option("preserve_imported_ties", options.preserve_imported_ties);
    integer_option("grid_density_max", options.bin_max);
    boolean_option("auto_bins", options.automatic_bins);  // deprecated: grid_density_mode
    if (config.HasMember("grid_density_mode")) {
      const auto mode = text("grid_density_mode");
      if ((mode != "fixed" && mode != "dynamic" && mode != "macro") || config.HasMember("dynamic_bins")
          || config.HasMember("auto_bins")) {
        throw std::invalid_argument("grid_density_mode requires fixed, dynamic or macro, without dynamic_bins/auto_bins");
      }
      options.dynamic_bins   = mode == "dynamic";
      options.automatic_bins = mode == "macro";
    }
    integer_option("grid_route", options.route_bins);
    integer_option("legalize_bin_grid", options.legalize_bins);
    number_option("local_density_weight", options.local_density_weight);
    number_option("local_density_target", options.local_density_target);
    integer_option("grid_local", options.local_density_bins);
    boolean_option("gp_ab", options.gp_search.enabled);
    integer_option("gp_ab_start", options.gp_search.start);
    integer_option("gp_ab_interval", options.gp_search.interval);
    integer_option("gp_ab_steps", options.gp_search.steps);
    integer_option("gp_ab_rounds", options.gp_search.rounds);
    number_option("gp_ab_filler_step", options.gp_search.filler_step);
    number_option("gp_ab_local_step", options.gp_search.local_step);
    if (options.gp_search.enabled && !(config.HasMember("cell_place") && config["cell_place"].GetBool())) {
      throw std::invalid_argument("gp_ab requires final cell_place");
    }
    boolean_option("vanished_memory", options.vanished_memory);
    number_option("vanished_memory_initial_scale", options.vanished_memory_initial_scale);
    integer_option("vanished_memory_growth_iterations", options.vanished_memory_growth_iterations);
    integer_option("vanished_memory_start", options.vanished_memory_start);
    number_option("vanished_memory_local_floor", options.vanished_memory_local_floor);
    if (options.vanished_memory_local_floor != 0 && !options.vanished_memory) {
      throw std::invalid_argument("vanished_memory_local_floor requires vanished_memory");
    }
    boolean_option("macro_transparency_always", options.macro_transparency.always);
    integer_option("macro_transparency_cycles", options.macro_transparency.cycles);
    integer_option("macro_transparency_warmup", options.macro_transparency.warmup);
    integer_option("macro_transparency_period", options.macro_transparency.period);
    if (options.macro_transparency.cycles < 0 || options.macro_transparency.warmup < 0 || options.macro_transparency.period <= 0) {
      throw std::invalid_argument("invalid macro transparency schedule");
    }
    if ((options.macro_transparency.enabled() || options.vanished_memory)
        && !(config.HasMember("cell_place") && config["cell_place"].GetBool())) {
      throw std::invalid_argument("macro transparency requires final cell_place");
    }
    number_option("local_density_range", options.local_density_range);
    number_option("local_density_jitter", options.local_density_jitter);
    boolean_option("virtual_fillers", options.virtual_fillers);
    integer_option("virtual_filler_max_count", options.virtual_filler_max_count);
    number_option("virtual_filler_fraction", options.virtual_filler_fraction);
    number_option("virtual_filler_cell_scale", options.virtual_filler_cell_scale);
    number_option("virtual_filler_bin_fraction", options.virtual_filler_bin_fraction);
    boolean_option("scale_macro_density", options.scale_macro_density);
    boolean_option("exact_macro_density", options.exact_macro_density);
    boolean_option("adaptive_density", options.adaptive_density);
    boolean_option("routability", options.routability);
    boolean_option("routability_charge_compensation", options.routability_charge_compensation);
    number_option("macro_cell_density_weight", options.macro_cell_density_weight);
    number_option("macro_density_pull_weight", options.macro_density_pull_weight);
    number_option("macro_density_pull_range", options.macro_density_pull_range);
    options.fp_place = options.final_cells = options.macro_snap = false;
    options.legalize_cells                                      = true;
    options.retain_placement    = config.HasMember("timing_only") && config["timing_only"].GetBool();
    options.density_upper_bound = config.HasMember("density_upper_bound") && config["density_upper_bound"].GetBool();
    options.exact_cell_density  = config.HasMember("exact_cell_density") && config["exact_cell_density"].GetBool();
    options.min_iterations      = config.HasMember("min_iterations") ? config["min_iterations"].GetInt() : 40;
    if (config.HasMember("post_density_factor")) {
      options.post_density_factor = config["post_density_factor"].GetDouble();
    }
    if (config.HasMember("convergence_tolerance")) {
      options.convergence_tolerance = config["convergence_tolerance"].GetDouble();
    }
    if (config.HasMember("timing_max_weight")) {
      options.timing_max_weight = config["timing_max_weight"].GetDouble();
    }
    boolean_option("satlegalize", options.satlegalize);
    if (config.HasMember("terminal_legalize")) {
      throw std::invalid_argument("terminal_legalize was removed; final legalization is mandatory and resize is independent");
    }
    boolean_option("region_boundarypins", options.boundarypins.enabled);
    boolean_option("region_boundarypins_split", options.boundarypins.split);
    boolean_option("region_boundarypins_order", options.boundarypins.order);
    boolean_option("region_pinalign", options.boundarypins.align);
    number_option("region_boundarypins_max_displacement_um", options.boundarypins.max_displacement_um);
    boolean_option("region_busorder", options.busorder.enabled);
    number_option("region_busorder_weight", options.busorder.weight);
    number_option("region_merge_um", options.region.merge_um);
    number_option("region_busorder_max_displacement_um", options.busorder.max_displacement_um);
    auto& terminal_options = options.terminal_legalize_options;
    boolean_option("legalize_joint_minimize_total", terminal_options.joint_minimize_total);
    number_option("legalize_remainder_wire_weight", terminal_options.remainder_wire_weight);
    number_option("legalize_bin_max_density", terminal_options.max_density);
    number_option("legalize_critical_fraction", terminal_options.critical_fraction);
    number_option("legalize_local_pair_fraction", terminal_options.local_pair_fraction);
    if (config.HasMember("satlegalize_backend")) {
      options.satlegalize_options.backend = text("satlegalize_backend");
    }
    if (config.HasMember("satlegalize_encoding")) {
      options.satlegalize_options.encoding = text("satlegalize_encoding");
    }
    const auto sat_limit = [&](const char* key, unsigned& value) {
      if (!config.HasMember(key)) {
        return;
      }
      if (!config[key].IsUint()) {
        throw std::invalid_argument(std::string(key) + " requires an unsigned integer");
      }
      value = config[key].GetUint();
    };
    auto& sat_options = options.satlegalize_options;
    sat_limit("region_boundarypins_iterations", options.boundarypins.iterations);
    sat_limit("cell_jump", options.cell_jump.depth);
    sat_limit("cell_jump_settle", options.cell_jump.settle);
    sat_limit("cell_jump_interval", options.cell_jump.interval);
    sat_limit("cell_jump_fix_period", options.cell_jump.fix_period);
    if (options.cell_jump.depth > 4 || !options.cell_jump.settle || !options.cell_jump.interval) {
      throw std::invalid_argument("cell_jump requires depth 0..4 and positive settle/interval");
    }
    if (options.cell_jump.depth > 1
        && (!(config.HasMember("cell_place") && config["cell_place"].GetBool())
            || !(config.HasMember("restart_seed_def") && config["restart_seed_def"].GetBool()))) {
      throw std::invalid_argument(
          "experimental cell_jump propagation requires final cell_place and a distance-one restart_seed_def");
    }
    sat_limit("region_busorder_iterations", options.busorder.iterations);
    sat_limit("region_busorder_refresh", options.busorder.refresh);
    sat_limit("region_max_cells", options.region.max_cells);
    sat_limit("legalize_padding_sites", terminal_options.padding_sites);
    sat_limit("legalize_padding_max_extra_rows", terminal_options.padding_max_extra_rows);
    sat_limit("legalize_critical_padding_sites", terminal_options.critical_padding_sites);
    sat_limit("legalize_critical_fanin_depth", terminal_options.critical_fanin_depth);
    sat_limit("legalize_sat_milliseconds", terminal_options.sat_milliseconds);
    sat_limit("legalize_sat_conflicts", terminal_options.sat_conflicts);
    sat_limit("legalize_candidates", terminal_options.candidates);
    sat_limit("legalize_pair_move_sites", terminal_options.pair_move_sites);
    sat_limit("legalize_pair_move_rows", terminal_options.pair_move_rows);
    sat_limit("legalize_critical_pair_limit", terminal_options.critical_pair_limit);
    sat_limit("legalize_local_pair_limit", terminal_options.local_pair_limit);
    if (config.HasMember("satlegalize_bin_grid")) {
      const auto grid = text("satlegalize_bin_grid");
      if (grid != "none" && grid != "route") {
        throw std::invalid_argument("satlegalize_bin_grid requires none or route");
      }
      sat_options.route_grid = grid == "route";
    }
    boolean_option("satlegalize_route_guidance", sat_options.route_guidance);
    if (config.HasMember("satlegalize_route_peak_floor")) {
      if (!config["satlegalize_route_peak_floor"].IsNumber()) {
        throw std::invalid_argument("satlegalize_route_peak_floor requires a number");
      }
      sat_options.route_peak_floor = config["satlegalize_route_peak_floor"].GetDouble();
    }
    sat_limit("satlegalize_max_cells", sat_options.max_cells);
    sat_limit("satlegalize_max_candidates", sat_options.max_candidates);
    sat_limit("satlegalize_max_windows", sat_options.max_windows);
    sat_limit("satlegalize_max_pairs", sat_options.max_pairs);
    sat_limit("satlegalize_max_pairs_per_window", sat_options.max_pairs_per_window);
    boolean_option("satlegalize_check_packing", sat_options.check_packing);
    sat_limit("satlegalize_max_moved_cells", sat_options.max_moved_cells);
    sat_limit("satlegalize_move_sites", sat_options.move_sites);
    sat_limit("satlegalize_move_rows", sat_options.move_rows);
    if (config.HasMember("satlegalize_spill_neighbors")) {
      if (!config["satlegalize_spill_neighbors"].IsBool()) {
        throw std::invalid_argument("satlegalize_spill_neighbors requires a boolean");
      }
      sat_options.spill_neighbors = config["satlegalize_spill_neighbors"].GetBool();
    }
    if (config.HasMember("satlegalize_bin_size_um")) {
      const auto& region = config["satlegalize_bin_size_um"];
      if (region.IsNumber()) {
        sat_options.region_width_um = sat_options.region_height_um = region.GetDouble();
      } else if (region.IsArray() && region.Size() == 2 && region[0].IsNumber() && region[1].IsNumber()) {
        sat_options.region_width_um  = region[0].GetDouble();
        sat_options.region_height_um = region[1].GetDouble();
      } else {
        throw std::invalid_argument("satlegalize_bin_size_um requires positive microns: number or [width,height]");
      }
      if (!std::isfinite(sat_options.region_width_um) || !std::isfinite(sat_options.region_height_um)
          || sat_options.region_width_um <= 0 || sat_options.region_height_um <= 0) {
        throw std::invalid_argument("satlegalize_bin_size_um requires finite positive microns");
      }
    }
    if (sat_options.route_grid && config.HasMember("satlegalize_bin_size_um")) {
      throw std::invalid_argument("satlegalize_bin_grid=route cannot be combined with satlegalize_bin_size_um");
    }
    if (sat_options.spill_neighbors && !config.HasMember("satlegalize_bin_size_um") && !sat_options.route_grid) {
      throw std::invalid_argument("satlegalize_spill_neighbors requires satlegalize_bin_size_um or satlegalize_bin_grid=route");
    }
    number_option("satlegalize_spill_slack_margin_ps", sat_options.spill_slack_margin_ps);
    sat_limit("satlegalize_conflicts", sat_options.conflicts);
    sat_limit("satlegalize_milliseconds", sat_options.milliseconds);
    boolean_option("satlegalize_skip_conflicts", sat_options.skip_conflicts);
    number_option("satlegalize_boundary_growth", sat_options.boundary_growth);
    number_option("eco_connectivity_weight", options.eco_connectivity_weight);
    sat_limit("legalize_timing_corrections", options.legalize_timing_corrections);
    number_option("legalize_timing_strength", options.legalize_timing_strength);
    number_option("legalize_timing_threshold", options.legalize_timing_threshold);
    if (config.HasMember("timing_exponent")) {
      options.timing_exponent = config["timing_exponent"].GetDouble();
    }
    if (config.HasMember("timing_spread_weight")) {
      options.timing_spread_weight = config["timing_spread_weight"].GetDouble();
    }
    if (config.HasMember("routability_macro_weight")) {
      options.routability_macro_weight = config["routability_macro_weight"].GetDouble();
    }
    if (config.HasMember("routability_macro_range")) {
      options.routability_macro_range = config["routability_macro_range"].GetDouble();
    }
    integer_option("final_timing_start", options.final_timing_start);
    if (config.HasMember("final_timing_schedule")) {
      options.final_timing_schedule = text("final_timing_schedule");
    }
    if (config.HasMember("macro_timing_interval")) {
      options.macro_timing_interval = config["macro_timing_interval"].GetInt();
    }
    if (config.HasMember("timing_smoothing")) {
      options.timing_smoothing = config["timing_smoothing"].GetDouble();
    }
    if (config.HasMember("stop_overflow")) {
      options.stop_overflow = config["stop_overflow"].GetDouble();
    }
    if (config.HasMember("initial_density_weight")) {
      options.initial_density_weight = config["initial_density_weight"].GetDouble();
    }
    if (config.HasMember("timing_fraction")) {
      options.timing_fraction = config["timing_fraction"].GetDouble();
    }
    if (config.HasMember("resize_engine")) {
      options.resize_engine = text("resize_engine");
      if (options.resize_engine != "new" && options.resize_engine != "legacy") {
        throw std::invalid_argument("resize_engine must be new or legacy");
      }
    }
    if ((forest_only || sizing_forest) && options.resize_engine != "legacy") {
      throw std::invalid_argument("repair_scope requires resize_engine=legacy");
    }
    if (config.HasMember("resize_vt_swap")) {
      options.resize_vt_swap = config["resize_vt_swap"].GetBool();
    }
    if (config.HasMember("dont_use")) {
      for (const auto& pattern : config["dont_use"].GetArray()) {
        options.resize_dont_use.emplace_back(pattern.GetString());
      }
    }
    // A retained-placement repair replay needs the exact incoming GP weights
    // too. Keep explicit weights common to both diagnostic entry paths.
    if (config.HasMember("net_weights")) {
      rapidjson::Document weights;
      weights.Parse(read(text("net_weights")).c_str());
      if (weights.HasParseError() || !weights.IsObject()) {
        throw std::invalid_argument("invalid net_weights file");
      }
      for (const auto& item : weights.GetObject()) {
        if (!item.value.IsNumber() || !std::isfinite(item.value.GetDouble()) || item.value.GetDouble() < 1
            || item.value.GetDouble() > pl::max_net_weight
            || !options.net_weights.emplace(item.name.GetString(), item.value.GetDouble()).second) {
          throw std::invalid_argument("invalid or duplicate net weight");
        }
      }
    }
    boolean_option("remainder_warpspace", options.terminal_legalize_options.remainder_warp.enabled);
    number_option("remainder_warp_weight", options.terminal_legalize_options.remainder_warp.weight);
    number_option("remainder_warp_seconds", options.terminal_legalize_options.remainder_warp.seconds);
    sat_limit("remainder_warp_bins", options.terminal_legalize_options.remainder_warp.bins);
    boolean_option("fp_warpspace", options.fp_warp.enabled);
    boolean_option("macro_warpspace", options.macro_warp.enabled);
    boolean_option("macro_warp_once_after_density", options.macro_warp.once_after_density);
    sat_limit("fp_warp_bins", options.fp_warp.bins);
    sat_limit("macro_warp_bins", options.macro_warp.bins);
    sat_limit("fp_warp_iterations", options.fp_warp.iterations);
    sat_limit("macro_warp_iterations", options.macro_warp.iterations);
    sat_limit("macro_warp_updates", options.macro_warp.updates);
    number_option("fp_warp_weight", options.fp_warp.weight);
    number_option("macro_warp_weight", options.macro_warp.weight);
    number_option("fp_warp_seconds", options.fp_warp.seconds);
    number_option("macro_warp_seconds", options.macro_warp.seconds);
    number_option("macro_warp_warmup", options.macro_warp.warmup);

    options.liberty       = text("liberty");
    options.control_ports = config.HasMember("control_ports") ? text("control_ports") : "";
    options.tech_rc_file  = text("tech_rc");
    options.tracks_file   = config.HasMember("tracks") ? text("tracks") : "";
    options.output_dir    = root + "/initial";
    options.floorplan     = text("def");
    options.previous      = std::make_shared<ph::Floorplan>(ph::read_def(options.floorplan));
    for (const auto& path : config["lefs"].GetArray()) {
      options.lefs.emplace_back(path.GetString());
    }
    const auto& tech  = options.technology().lef;
    auto        frame = std::make_shared<ph::Floorplan>(*options.previous);
    for (auto& cell : frame->components) {
      const auto& macro = tech.macros.at(cell.macro);
      cell.place.width  = std::llround(macro.width * tech.dbu);
      cell.place.height = std::llround(macro.height * tech.dbu);
    }
    options.previous = std::move(frame);
    if (config.HasMember("io_iterations")) {
      options.io_iterations = config["io_iterations"].GetInt();
    }
    if (config.HasMember("fixed_macro_overlap_weight")) {
      options.fixed_macro_overlap_weight = config["fixed_macro_overlap_weight"].GetDouble();
    }
    if (config.HasMember("wirelength_gamma_end")) {
      options.wirelength_gamma_end = config["wirelength_gamma_end"].GetDouble();
    }
    if (config.HasMember("detailed_iterations")) {
      options.detailed_iterations = config["detailed_iterations"].GetInt();
    }
    if (config.HasMember("halo")) {
      options.halo_x = options.halo_y = config["halo"].GetDouble();
    }
    if (config.HasMember("cell_halo")) {
      const auto& halo = config["cell_halo"];
      if (halo.IsNumber()) {
        options.cell_halo = std::pair{halo.GetDouble(), halo.GetDouble()};
      } else if (halo.IsArray() && halo.Size() == 2 && halo[0].IsNumber() && halo[1].IsNumber()) {
        options.cell_halo = std::pair{halo[0].GetDouble(), halo[1].GetDouble()};
      } else {
        throw std::invalid_argument("cell_halo requires nonnegative microns: number or [x,y]");
      }
      if (!std::isfinite(options.cell_halo->first) || !std::isfinite(options.cell_halo->second) || options.cell_halo->first < 0
          || options.cell_halo->second < 0) {
        throw std::invalid_argument("cell_halo requires finite nonnegative microns");
      }
      if (options.cell_halo->first > options.halo_x || options.cell_halo->second > options.halo_y) {
        throw std::invalid_argument("each cell_halo axis must not exceed halo");
      }
    }
    if (config.HasMember("snapshot_only") && config["snapshot_only"].GetBool()) {
      std::set<std::string> ignored_nets;
      bool                  snapshot_prects = false;
      boolean_option("snapshot_prects", snapshot_prects);
      if (snapshot_prects) {
        const auto controls
            = pl::prects_networks(tech, *options.previous, pl::prects_library(options.liberty, options.control_ports));
        for (size_t n = 0; n < options.previous->nets.size(); ++n) {
          if (controls.ignored[n]) {
            ignored_nets.insert(options.previous->nets[n].name);
          }
        }
      }
      physical_snapshot(tech, *options.previous, root, ignored_nets);
      std::ofstream filters(root + "/snapshot_filters.json");
      filters << "{\"prects_controls\":" << (snapshot_prects ? "true" : "false") << ",\"ignored_nets\":" << ignored_nets.size()
              << "}\n";
      if (!filters) {
        throw std::runtime_error("cannot write physical snapshot filter report");
      }
      return 0;
    }
    std::cout << "loading mapped graph" << std::endl;
    hhds::GraphLibrary           source_library;
    std::shared_ptr<hhds::Graph> source;
    if (config.HasMember("source_def") && config["source_def"].GetBool()) {
      source = mapped_def(source_library, *options.previous, tech);
    } else {
      auto&      library = livehd::Hhds_graph_library::instance(text("lg"));
      const auto io      = library.find_io(top);
      if (!io || !io->get_graph()) {
        throw std::runtime_error("missing top graph");
      }
      source = io->get_graph();
    }
    pl::initialize_growth_budget(*source, options);
    hhds::GraphLibrary seeded_source_library;
    if (config.HasMember("seed_def_positions") && config["seed_def_positions"].GetBool()) {
      // Resolve hierarchical physical names through the normal retained-frame
      // importer. Updating root nodes alone misses nested cells and leaves
      // their old row orientations on an otherwise exact coordinate replay.
      auto restore_options                                 = options;
      restore_options.retain_placement                     = true;
      restore_options.output_dir                           = root + "/seed_graph";
      auto                                        restored = pl::place(*source, seeded_source_library, restore_options);
      std::map<std::string, const ph::Component*> seeds;
      for (const auto& cell : options.previous->components) {
        seeds.emplace(cell.name, &cell);
      }
      if (restored.floorplan.components.size() != seeds.size()) {
        throw std::runtime_error("DEF seed changed the mapped component count");
      }
      for (const auto& cell : restored.floorplan.components) {
        const auto old = seeds.find(cell.name);
        if (old == seeds.end() || cell.macro != old->second->macro || cell.place.x != old->second->place.x
            || cell.place.y != old->second->place.y || cell.place.orientation != old->second->place.orientation) {
          throw std::runtime_error("DEF seed failed to preserve mapped component geometry: " + cell.name);
        }
      }
      source = std::move(restored.graph);
    }
    hhds::GraphLibrary unlocked_seed_library;
    bool               unlock_seed_components = false;
    boolean_option("unlock_seed_components", unlock_seed_components);
    if (unlock_seed_components) {
      if (!floorplan_mode || !config.HasMember("seed_def_positions") || !config["seed_def_positions"].GetBool()) {
        throw std::invalid_argument("unlock_seed_components requires floorplan_place and seed_def_positions");
      }
      // A replayed final GP graph carries fixed-macro annotations. Work on a
      // private copy so the saved source remains an immutable comparison input.
      if (!livehd::copy_with_callees(unlocked_seed_library, *source->get_io()->get_library(), top)) {
        throw std::runtime_error("cannot clone seed for macro/FP replay");
      }
      source              = unlocked_seed_library.find_io(top)->get_graph();
      size_t     unlocked = 0;
      const auto unlock   = [&](hhds::Graph& definition) {
        for (auto node : definition.body().nodes()) {
          if (auto attr = node.attr(livehd::attrs::place); attr.has()) {
            auto pose = attr.get();
            if (pose.status == ph::Status::FIXED || pose.status == ph::Status::LOCKED) {
              pose.status = ph::Status::PLACED;
              attr.set(pose);
              ++unlocked;
            }
          }
        }
      };
      unlock(*source);
      for (const auto& definition : source->definitions().graphs()) {
        unlock(*definition);
      }
      std::ofstream(root + "/unlocked_seed.json") << "{\"components\":" << unlocked << "}\n";
    }
    bool restart_seed_def = false;
    boolean_option("restart_seed_def", restart_seed_def);
    if (restart_seed_def) {
      // The restart seed is the production GP coordinate initialization path;
      // node attributes alone do not select it for a fresh placement run.
      options.restart_seed = options.previous;
    }
    hhds::GraphLibrary placed_library;
    hhds::GraphLibrary cleanup_library;
    unsigned           removed_synthesis_buffers = 0;
    if (floorplan_mode || (config.HasMember("cell_place") && config["cell_place"].GetBool())) {
      // Controlled full cell-GP import, not the ECO/retained-placement path.
      if (config.HasMember("remove_synthesis_buffers") && config["remove_synthesis_buffers"].GetBool()) {
        const auto cleaned        = pl::remove_synthesis_buffers(*source, cleanup_library, options);
        source                    = cleaned.graph;
        removed_synthesis_buffers = cleaned.removed;
        std::ofstream(root + "/buffer_cleanup.json")
            << "{\"removed\":" << cleaned.removed << ",\"boundary\":" << cleaned.boundary
            << ",\"protected\":" << cleaned.protected_cells << ",\"unsupported\":" << cleaned.unsupported << "}\n";
      }
      const auto input = options.previous;
      options.previous.reset();
      options.snapshots = true;
      if (config.HasMember("route_model")) {
        options.route_model = text("route_model");
      }
      if (config.HasMember("iterations")) {
        options.iterations = config["iterations"].GetInt();
      }
      if (floorplan_mode) {
        options.fp_place = options.final_cells = options.macro_snap = true;
        options.fp_colors                                           = true;
        boolean_option("fp_snap", options.fp_snap_enabled);
        // Request the production macro-to-cell handoff even when the
        // diagnostic disables centroid FP. No resynthesis restart is needed.
        options.after_stage = [](pl::Placement_stage, const pl::Result&) { return false; };
        // Diagnostic comparisons use the same mapped inputs/timing policy
        // while selecting the production FP/color switches independently.
        if (config.HasMember("macro_align_global")) {
          options.macro_align_global = config["macro_align_global"].GetBool();
        }
        if (config.HasMember("macro_align_weight")) {
          options.macro_align_weight = config["macro_align_weight"].GetDouble();
        }
        if (config.HasMember("macro_align_sigma")) {
          options.macro_align_sigma = config["macro_align_sigma"].GetDouble();
        }
        if (config.HasMember("macro_align_start")) {
          options.macro_align_start = config["macro_align_start"].GetInt();
        }
        if (config.HasMember("macro_align_full")) {
          options.macro_align_full = config["macro_align_full"].GetInt();
        }
        if (config.HasMember("macro_snap_mode")) {
          options.macro_snap_mode = text("macro_snap_mode");
        }
        if (config.HasMember("fp_place")) {
          options.fp_place = config["fp_place"].GetBool();
        }
        if (config.HasMember("fp_colors")) {
          options.fp_colors = config["fp_colors"].GetBool();
        }
        if (config.HasMember("fp_anchor_macros_only")) {
          options.fp_anchor_macros_only = config["fp_anchor_macros_only"].GetBool();
        }
        if (config.HasMember("fp_distance_attraction")) {
          options.fp_distance_attraction = config["fp_distance_attraction"].GetBool();
        }
        if (config.HasMember("fp_attraction_outside")) {
          options.fp_attraction_outside = config["fp_attraction_outside"].GetDouble();
        }
        if (config.HasMember("fp_color_source")) {
          options.fp_color_source = text("fp_color_source");
        }
        if (config.HasMember("fp_color_maximum")) {
          options.fp_color_maximum = config["fp_color_maximum"].GetDouble();
        }
        if (config.HasMember("fp_color_minimum")) {
          options.fp_color_minimum = config["fp_color_minimum"].GetDouble();
        }
        if (config.HasMember("fp_pin_adjust")) {
          options.fp_pin_adjust = config["fp_pin_adjust"].GetBool();
        }
        if (config.HasMember("fp_pin_interval")) {
          options.fp_pin_interval = config["fp_pin_interval"].GetInt();
        }
        if (config.HasMember("macro_pin_adjust")) {
          options.macro_pin_adjust = config["macro_pin_adjust"].GetBool();
        }
        if (config.HasMember("macro_pin_interval")) {
          options.macro_pin_interval = config["macro_pin_interval"].GetInt();
        }
        if (config.HasMember("macro_pin_step")) {
          options.macro_pin_step = config["macro_pin_step"].GetDouble();
        }
        if (config.HasMember("macro_pin_improvement")) {
          options.macro_pin_improvement = config["macro_pin_improvement"].GetDouble();
        }
        if (config.HasMember("final_cells")) {
          options.final_cells = config["final_cells"].GetBool();
        }
        if (config.HasMember("fp_parent_weight")) {
          options.fp_parent_weight = config["fp_parent_weight"].GetDouble();
        }
        if (config.HasMember("fp_parent_outside")) {
          options.fp_parent_outside = config["fp_parent_outside"].GetDouble();
        }
        if (config.HasMember("fp_align_weight")) {
          options.fp_align_weight = config["fp_align_weight"].GetDouble();
        }
        if (config.HasMember("fp_align_sigma")) {
          options.fp_align_sigma = config["fp_align_sigma"].GetDouble();
        }
        if (config.HasMember("fp_align_start")) {
          options.fp_align_start = config["fp_align_start"].GetInt();
        }
        if (config.HasMember("fp_align_full")) {
          options.fp_align_full = config["fp_align_full"].GetInt();
        }
        if (config.HasMember("fp_iterations")) {
          options.fp_iterations = config["fp_iterations"].GetInt();
        }
        if (config.HasMember("fp_timing_interval")) {
          options.fp_timing_interval = config["fp_timing_interval"].GetInt();
        }
        if (config.HasMember("fp_timing_after_density")) {
          options.fp_timing_after_density = config["fp_timing_after_density"].GetBool();
        }
        if (config.HasMember("fp_timing_movement")) {
          options.fp_timing_movement = config["fp_timing_movement"].GetDouble();
        }
        if (config.HasMember("fp_resize")) {
          options.fp_resize = config["fp_resize"].GetBool();
        }
      }
      if (macro_only) {
        if (options.final_cells || options.cell_halo || options.gp_search.enabled || options.vanished_memory
            || options.macro_transparency.enabled() || options.cell_jump.depth) {
          throw std::invalid_argument("macro_only requires final_cells=false and no final-cell controls");
        }
        // Optional centroid FP feeds the production macro stage; export its legal frame.
        // Physical rows/tapcells can be rebuilt before a separate final solve.
        options.completed_stage      = pl::Placement_stage::macro;
        options.macro_legalize_cells = false;
        options.macro_detailed_place = false;
      }
      if (!floorplan_mode && options.cell_halo) {
        // This diagnostic mode already supplies the fixed macro frame; only
        // its final cell GP is requested, without another macro-stage solve.
        options = pl::final_cell_options(options);
      }
      if (config.HasMember("color_analysis_only") && config["color_analysis_only"].GetBool()) {
        if (!floorplan_mode) {
          throw std::runtime_error("color_analysis_only requires floorplan_place");
        }
        options.analyze_colors_only = true;
        options.final_cells         = false;
        options.output_dir          = root + "/analysis";
        auto analysis               = pl::place(*source, placed_library, options);
        std::ofstream(root + "/qor.json") << analysis.qor;
        return 0;
      }
      livehd::opentimer::Physical_session timing_session(timing_options(options.liberty, sdc, top), options.threads);
      options.whatif          = &timing_session.whatif();
      unsigned   serial       = 0;
      const auto raw_evaluate = [&](const pl::Result& placed) {
        return evaluate_timing(placed, timing_session, serial, &tech, root + "/timing_profile.jsonl");
      };
      const auto rank = [&](const pl::Ranking_view& view) {
        pl::Result placed;
        placed.graph      = view.graph;
        placed.parasitics = view.parasitics;
        return evaluate_timing(placed,
                               timing_session,
                               serial,
                               nullptr,
                               root + "/timing_profile.jsonl",
                               livehd::opentimer::Physical_session::Purpose::ranking,
                               &view.controls);
      };
      pl::Evaluate evaluate     = raw_evaluate;
      const auto   start        = std::chrono::steady_clock::now();
      auto         seed_options = options;
      // Explicit replay weights used to be loaded after the timing seed.
      seed_options.net_weights.clear();
      seed_options.timing_seed = true;
      seed_options.output_dir  = root + "/seed";
      hhds::GraphLibrary seed_library;
      auto               seed          = pl::place(*source, seed_library, seed_options);
      const bool         timing_driven = !config.HasMember("timing_driven") || config["timing_driven"].GetBool();
      const auto seed_controls = timing_driven ? pl::prects_networks(tech, seed.floorplan, seed.prects) : pl::Prects_networks{};
      const auto seed_timing   = timing_driven ? rank({seed.graph, seed.parasitics, seed_controls}) : pl::Timing_feedback{};
      // A separately invoked fixed-macro cell pass can carry the macro pass's
      // last timing weights, exactly as the in-process staged placer does.
      if (!config.HasMember("net_weights") && timing_driven) {
        options.net_weights = pl::timing_weights(*seed.parasitics,
                                                 seed_timing,
                                                 options.timing_fraction,
                                                 options.timing_max_weight,
                                                 options.timing_exponent);
      }
      if (timing_driven) {
        options.fp_evaluate = raw_evaluate;
        options.gp_rank     = rank;
      }
      const auto seeded            = std::chrono::steady_clock::now();
      const bool run_resize        = config.HasMember("resize") && config["resize"].GetBool();
      const auto timing_rounds     = config.HasMember("timing_rounds") ? config["timing_rounds"].GetUint() : 0;
      options.optimization_pending = run_resize || timing_rounds;
      if (config.HasMember("virtual_timing")) {
        throw std::invalid_argument("virtual_timing was removed; use resize with gp_resize for real GP buffer repair");
      }
      options.gp_resize = run_resize && (!config.HasMember("gp_resize") || config["gp_resize"].GetBool());
      integer_option("gp_buffer_trials", options.gp_buffer_trials);
      if (config.HasMember("gp_buffer_margin")) {
        options.gp_buffer_margin = config["gp_buffer_margin"].GetDouble();
      }
      if (options.gp_resize) {
        options.fp_evaluate = raw_evaluate;
      }
      int trace_interval = 0, stop_iteration = 0;
      integer_option("gp_trace_interval", trace_interval);
      integer_option("gp_stop_iteration", stop_iteration);
      if (trace_interval < 0 || stop_iteration < 0 || stop_iteration > options.iterations
          || (stop_iteration && (!trace_interval || stop_iteration % trace_interval))
          || ((trace_interval || stop_iteration) && floorplan_mode)) {
        throw std::invalid_argument(
            "GP trace requires cell_place, nonnegative interval and a stop divisible by the interval within the GP budget");
      }
      struct Trace_stop {};
      std::unique_ptr<livehd::opentimer::Physical_session> trace_session;
      unsigned                                             trace_serial = 0;
      if (trace_interval) {
        trace_session
            = std::make_unique<livehd::opentimer::Physical_session>(timing_options(options.liberty, sdc, top), options.threads);
        options.gp_observer_interval = trace_interval;
        options.min_iterations       = std::max(options.min_iterations, stop_iteration);
        options.gp_observer          = [&](const pl::Result& snapshot, const pl::Gp_observation& sample) {
          const auto begin  = std::chrono::steady_clock::now();
          const auto timing = evaluate_timing(snapshot, *trace_session, trace_serial, &tech, root + "/observation_profile.jsonl");
          const auto value  = [](std::optional<double> v, double scale) {
            return v && std::isfinite(*v) ? std::format("{}", *v * scale) : std::string("null");
          };
          std::ofstream out(root + "/gp_observations.jsonl", std::ios::app);
          out << std::format(
              R"({{"iteration":{},"wns_ns":{},"tns_us":{},"hpwl_um":{},"macro_scale":{},"route_overflow":{},"opaque_route_overflow":{},"route_peak":{},"opaque_route_peak":{},"unroutable_nets":{},"opaque_unroutable_nets":{},"blocked_pins":{},"opaque_blocked_pins":{},"timing_seconds":{}}})",
              sample.iteration,
              value(timing.wns, timing.time_unit_ps / 1000),
              value(timing.tns, timing.time_unit_ps / 1e6),
              sample.hpwl_um,
              sample.macro_scale,
              sample.route_overflow,
              sample.opaque_route_overflow,
              sample.route_peak,
              sample.opaque_route_peak,
              sample.unroutable_nets,
              sample.opaque_unroutable_nets,
              sample.blocked_pins,
              sample.opaque_blocked_pins,
              std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count())
              << '\n';
          out.close();
          if (!out) {
            throw std::runtime_error("cannot write GP observation");
          }
          if (sample.iteration == stop_iteration) {
            // This DEF is an overlapping GP diagnostic, never a legal handoff.
            ph::write_def(snapshot.floorplan, root + "/gp_stop.def");
            throw Trace_stop{};
          }
        };
      }
      pl::Result state;
      try {
        state = pl::place(*source, placed_library, options);
      } catch (const Trace_stop&) {
        std::ofstream(root + "/gp_stop.json") << std::format(
            R"({{"iteration":{},"samples":{},"legalized":false,"post_gp_repair":false}})",
            stop_iteration,
            trace_serial) << '\n';
        return 0;
      }

      options.restart_seed.reset();
      ph::write_def(state.floorplan, root + "/initial.def");
      const pl::Trial_admission admit_trial = [&] {
        const auto& stats = timing_session.statistics();
        return stats.evaluations - stats.rankings < 6;
      };
      const auto        placed = std::chrono::steady_clock::now();
      pl::Repair_result refined;
      if (timing_rounds) {
        refined = pl::timing_place(std::move(state), options, evaluate, timing_rounds, std::nullopt, admit_trial);
      } else {
        refined.placed = std::move(state);
        refined.report = "{\"model\":\"placement_only\",\"rounds\":[],\"accepted\":0}";
      }
      std::ofstream(root + "/timing_place.json") << refined.report;
      const auto        timing_placed = std::chrono::steady_clock::now();
      pl::Repair_result sized, repaired;
      const bool        new_engine = options.resize_engine == "new";
      if (run_resize && new_engine) {
        repaired = pl::resize_placed(std::move(refined.placed), options, evaluate);
        std::ofstream(root + "/repair.json") << repaired.report;
      } else if (run_resize) {
        sized = pl::repair_buffers(std::move(refined.placed),
                                   options,
                                   evaluate,
                                   pl::Repair_scope::sizing,
                                   std::move(refined.timing),
                                   admit_trial);
        std::ofstream(root + "/resize.json") << sized.report;
      }
      const auto sized_at = std::chrono::steady_clock::now();
      if (run_resize && !new_engine) {
        repaired = pl::repair_buffers(std::move(sized.placed),
                                      options,
                                      evaluate,
                                      pl::Repair_scope::design_after_sizing,
                                      std::move(sized.timing),
                                      admit_trial);
        std::ofstream(root + "/repair.json") << repaired.report;
      }
      const auto        repaired_at = std::chrono::steady_clock::now();
      auto&             optimized   = run_resize ? repaired.placed : refined.placed;
      pl::Repair_result finalized;
      if (options.optimization_pending) {
        finalized = pl::finalize_placement(optimized,
                                           options,
                                           raw_evaluate,
                                           run_resize ? std::move(repaired.timing) : std::move(refined.timing));
      }
      auto&      result         = options.optimization_pending ? finalized.placed : optimized;
      const auto final_feedback = finalized.timing ? *finalized.timing : raw_evaluate(result);
      pl::refresh_timing_weights(result, options, final_feedback);
      const auto                                 timed_at = std::chrono::steady_clock::now();
      rapidjson::StringBuffer                    weights_json;
      rapidjson::Writer<rapidjson::StringBuffer> weights_writer(weights_json);
      weights_writer.StartObject();
      for (const auto& [name, weight] : result.net_weights) {
        weights_writer.Key(name.c_str());
        weights_writer.Double(weight);
      }
      weights_writer.EndObject();
      if (!(std::ofstream(root + "/net_weights.json") << weights_json.GetString() << '\n')) {
        throw std::runtime_error("cannot write net_weights");
      }
      const auto                                 membership = pl::timing_weights(*result.parasitics,
                                                                                 final_feedback,
                                                                                 options.timing_fraction,
                                                                                 options.timing_max_weight,
                                                                                 options.timing_exponent);
      rapidjson::StringBuffer                    final_weights_json;
      rapidjson::Writer<rapidjson::StringBuffer> final_weights(final_weights_json);
      final_weights.StartObject();
      final_weights.Key("stage");
      final_weights.String(run_resize ? "post_repair" : "final");
      final_weights.Key("checkpoint");
      final_weights.Uint(serial);
      final_weights.Key("nets");
      final_weights.StartArray();
      for (const auto& [driver, weight] : result.net_weights) {
        final_weights.StartObject();
        final_weights.Key("driver");
        final_weights.String(driver.c_str());
        final_weights.Key("critical");
        final_weights.Bool(membership.contains(driver));
        final_weights.Key("weight");
        final_weights.Double(weight);
        final_weights.EndObject();
      }
      final_weights.EndArray();
      final_weights.EndObject();
      if (!(std::ofstream(root + "/final_weights.json") << final_weights_json.GetString() << '\n')) {
        throw std::runtime_error("cannot write final timing weights");
      }
      rapidjson::StringBuffer                    final_timing_json;
      rapidjson::Writer<rapidjson::StringBuffer> final_timing(final_timing_json);
      final_timing.StartObject();
      final_timing.Key("stage");
      final_timing.String(run_resize ? "post_repair" : "final");
      final_timing.Key("checkpoint");
      final_timing.Uint(serial);
      const auto optional_number = [&](const char* name, const std::optional<double>& value) {
        final_timing.Key(name);
        if (value) {
          final_timing.Double(*value);
        } else {
          final_timing.Null();
        }
      };
      optional_number("wns", final_feedback.wns);
      optional_number("tns", final_feedback.tns);
      optional_number("hold_wns", final_feedback.hold_wns);
      final_timing.Key("delay");
      final_timing.Double(final_feedback.delay);
      final_timing.Key("area");
      final_timing.Double(final_feedback.area);
      final_timing.Key("violations");
      final_timing.Uint(final_feedback.violations);
      final_timing.Key("electrical_excess");
      final_timing.Double(final_feedback.electrical_excess);
      final_timing.Key("time_unit_ps");
      final_timing.Double(final_feedback.time_unit_ps);
      final_timing.EndObject();
      if (!(std::ofstream(root + "/final_timing.json") << final_timing_json.GetString() << '\n')) {
        throw std::runtime_error("cannot write final timing report");
      }
      std::map<std::string, const ph::Component*> originals;
      for (const auto& cell : (floorplan_mode ? seed.floorplan.components : input->components)) {
        originals.emplace(cell.name, &cell);
      }
      const bool editable = options.fp_resize || run_resize;
      if (!editable && result.floorplan.components.size() + (floorplan_mode ? 0 : removed_synthesis_buffers) != originals.size()) {
        throw std::runtime_error("cell placement changed component count");
      }
      unsigned fixed = 0;
      for (const auto& cell : result.floorplan.components) {
        const auto original = originals.find(cell.name);
        if (original == originals.end()) {
          if (!editable) {
            throw std::runtime_error("unexpected placement component: " + cell.name);
          }
          continue;
        }
        const auto& old = *original->second;
        if (!editable && cell.macro != old.macro) {
          throw std::runtime_error("cell placement changed master: " + cell.name);
        }
        if (old.place.status == ph::Status::FIXED || old.place.status == ph::Status::LOCKED) {
          ++fixed;
          if (cell.macro != old.macro || cell.place.x != old.place.x || cell.place.y != old.place.y
              || cell.place.orientation != old.place.orientation) {
            throw std::runtime_error("cell placement moved fixed component: " + cell.name);
          }
        }
      }
      const auto expected_fixed = std::count_if(originals.begin(), originals.end(), [](const auto& entry) {
        const auto status = entry.second->place.status;
        return status == ph::Status::FIXED || status == ph::Status::LOCKED;
      });
      if (static_cast<std::ptrdiff_t>(fixed) != expected_fixed) {
        throw std::runtime_error("cell placement removed a fixed component");
      }
      std::ofstream(root + "/qor.json") << result.qor << '\n';
      ph::write_def(result.floorplan, root + "/out.def");
      // Physical repair can change masters and topology. Preserve that exact
      // graph for a separately invoked fixed-macro cell stage.
      if (editable) {
        hhds::GraphLibrary output;
        if (!livehd::copy_with_callees(output, *result.graph->get_io()->get_library(), top)) {
          throw std::runtime_error("cannot save placed graph");
        }
        output.save(root + "/lg");
      }
      const auto done = std::chrono::steady_clock::now();
      std::ofstream(root + "/runtime.json") << "{\"seed_s\":" << std::chrono::duration<double>(seeded - start).count()
                                            << ",\"place_s\":" << std::chrono::duration<double>(placed - seeded).count()
                                            << ",\"timing_place_s\":"
                                            << std::chrono::duration<double>(timing_placed - placed).count()
                                            << ",\"sizing_s\":" << std::chrono::duration<double>(sized_at - timing_placed).count()
                                            << ",\"repair_s\":" << std::chrono::duration<double>(repaired_at - sized_at).count()
                                            << ",\"endpoint_sta_s\":"
                                            << std::chrono::duration<double>(timed_at - repaired_at).count()
                                            << ",\"export_s\":" << std::chrono::duration<double>(done - timed_at).count()
                                            << ",\"timing_refine_s\":" << std::chrono::duration<double>(done - placed).count()
                                            << ",\"fixed_components\":" << fixed << ",\"timing_evaluations\":" << serial
                                            << ",\"timing_engine_s\":" << timing_session.statistics().seconds
                                            << ",\"timing_builds\":" << timing_session.statistics().builds
                                            << ",\"timing_repowered_gates\":" << timing_session.statistics().repowered_gates
                                            << ",\"timing_inserted_gates\":" << timing_session.statistics().inserted_gates
                                            << ",\"timing_removed_gates\":" << timing_session.statistics().removed_gates
                                            << ",\"timing_updated_rc_nets\":" << timing_session.statistics().updated_nets << "}\n";
      std::cout << (floorplan_mode ? "Floorplan/macro/cell placement completed"
                    : editable     ? "Cell placement and repair completed; fixed components unchanged"
                                   : "Cell placement completed; component masters and fixed positions unchanged")
                << std::endl;
      return 0;
    }
    std::cout << "reconstructing retained placement and RC" << std::endl;
    auto import_options = options;
    if (retained_repair) {
      // Import the certified pre-repair frame without rerunning tie localization
      // or legalization. Repair trials below retain ordinary placement options.
      import_options.retain_placement = true;
    } else if (options.cell_halo && !options.retain_placement) {
      import_options = pl::final_cell_options(options);
    }
    auto                                        state = pl::place(*source, placed_library, import_options);
    std::map<std::string, const ph::Component*> initial_geometry;
    for (const auto& cell : options.previous->components) {
      initial_geometry.emplace(cell.name, &cell);
    }
    if (state.floorplan.components.size() != initial_geometry.size()) {
      throw std::runtime_error("retained placement cell count changed");
    }
    for (const auto& cell : state.floorplan.components) {
      const auto& old = *initial_geometry.at(cell.name);
      if (old.macro != cell.macro || old.place.x != cell.place.x || old.place.y != cell.place.y
          || old.place.orientation != cell.place.orientation) {
        throw std::runtime_error("retained placement moved " + cell.name);
      }
    }
    std::map<std::string, const ph::Terminal*> initial_io;
    for (const auto& port : options.previous->terminals) {
      initial_io.emplace(port.name, &port);
    }
    if (state.floorplan.terminals.size() != initial_io.size()) {
      throw std::runtime_error("retained placement IO count changed");
    }
    for (const auto& port : state.floorplan.terminals) {
      const auto& old = *initial_io.at(port.name);
      if (port.x != old.x || port.y != old.y || port.orientation != old.orientation) {
        throw std::runtime_error("retained placement moved IO " + port.name);
      }
    }
    if (config.HasMember("net_weights")) {
      std::set<std::string> drivers;
      for (const auto& net : state.parasitics->nets) {
        drivers.insert(net.driver);
        if (!options.net_weights.contains(net.driver)) {
          throw std::runtime_error("retained replay missing current driver weight: " + net.driver);
        }
      }
      if (drivers.size() != options.net_weights.size() || state.net_weights != options.net_weights) {
        throw std::runtime_error("retained replay weights include obsolete drivers or changed values");
      }
      ph::write_def(state.floorplan, root + "/retained_initial.def");
      std::ofstream(root + "/retained_import.json")
          << "{\"component_poses_unchanged\":true,\"io_poses_unchanged\":true,"
             "\"exact_current_driver_weights\":true,\"components\":"
          << initial_geometry.size() << ",\"io\":" << initial_io.size() << ",\"weights\":" << drivers.size() << "}\n";
    }
    std::cout << "retained placement ready: all masters, positions and orientations unchanged" << std::endl;
    if (config.HasMember("rc_file")) {
      auto rc          = std::make_shared<ph::Parasitics>(ph::deserialize_parasitics(read(text("rc_file"))));
      rc->cells        = state.parasitics->cells;
      rc->terminals    = state.parasitics->terminals;
      state.parasitics = std::move(rc);
    }
    if (config.HasMember("zero_wire") && config["zero_wire"].GetBool()) {
      auto rc       = std::make_shared<ph::Parasitics>(*state.parasitics);
      rc->identity += ":zero-wire";
      for (auto& net : rc->nets) {
        for (auto& node : net.nodes) {
          node.capacitance_ff = 0;
        }
        for (auto& edge : net.edges) {
          edge.resistance_ohm = 0;
        }
      }
      state.parasitics = std::move(rc);
    }
    unsigned                            serial = 0;
    livehd::opentimer::Physical_session timing_session(timing_options(options.liberty, sdc, top), options.threads);
    options.whatif      = &timing_session.whatif();
    const auto evaluate = [&](const pl::Result& placed) {
      return evaluate_timing(placed, timing_session, serial, prects_timing ? &tech : nullptr, root + "/timing_profile.jsonl");
    };
    if (options.retain_placement && (!config.HasMember("export_rc") || config["export_rc"].GetBool())) {
      std::ofstream(root + "/parasitics.json") << ph::serialize_parasitics(*state.parasitics);
    }
    auto initial = evaluate(state);
    std::cout << "initial delay " << initial.delay << " wns " << initial.wns.value_or(0) * initial.time_unit_ps << " tns "
              << initial.tns.value_or(0) * initial.time_unit_ps << " violations " << initial.violations << std::endl;
    if (config.HasMember("timing_only") && config["timing_only"].GetBool()) {
      // Bounded diagnostic report, separate from the optimizer's in-memory
      // feedback path. Keep the worst pins and path without dumping every pin
      // in a large design on each timing evaluation.
      std::vector<std::pair<std::string, const pl::Timing_feedback::Pin*>> pins;
      initial.for_each_pin([&](const auto& name, const auto& pin) {
        if (pin.slack && !pin.clock) {
          pins.emplace_back(name, &pin);
        }
      });
      std::sort(pins.begin(), pins.end(), [](const auto& a, const auto& b) {
        return *a.second->slack != *b.second->slack ? *a.second->slack < *b.second->slack : a.first < b.first;
      });
      if (pins.size() > 256) {
        pins.resize(256);
      }
      std::set<std::string> selected(initial.path.begin(), initial.path.end());
      for (const auto& pin : pins) {
        selected.insert(pin.first);
      }
      if (config.HasMember("timing_pins")) {
        for (const auto& pin : config["timing_pins"].GetArray()) {
          selected.insert(pin.GetString());
        }
      }
      rapidjson::StringBuffer                    buffer;
      rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
      json.StartObject();
      const auto field = [&](const char* key, std::optional<double> value) {
        json.Key(key);
        if (value) {
          json.Double(*value);
        } else {
          json.Null();
        }
      };
      json.Key("prects_filtered");
      json.Bool(prects_timing);
      field("time_unit_ps", initial.time_unit_ps);
      field("max_delay", initial.delay);
      field("wns", initial.wns);
      field("tns", initial.tns);
      field("hold_wns", initial.hold_wns);
      json.Key("path");
      json.StartArray();
      for (const auto& pin : initial.path) {
        json.String(pin.c_str());
      }
      json.EndArray();
      json.Key("pins");
      json.StartObject();
      for (const auto& name : selected) {
        const auto it = initial.find_label(name);
        if (it == nullptr) {
          continue;
        }
        json.Key(name.c_str());
        json.StartObject();
        field("arrival", it->arrival);
        field("required", it->required);
        field("slack", it->slack);
        field("slew", it->slew);
        field("load_ff", it->load_ff);
        json.EndObject();
      }
      json.EndObject();
      json.EndObject();
      std::ofstream(root + "/timing.json") << buffer.GetString() << '\n';
      ph::write_def(state.floorplan, root + "/out.def");
      return 0;
    }
    const bool repair_first
        = forest_only || (!sizing_forest && config.HasMember("repair_first") && config["repair_first"].GetBool());
    const auto start = std::chrono::steady_clock::now();
    if (options.resize_engine == "new") {
      // Path-based engine: plan on the incremental timer, legalize, confirm (resize.json).
      options.output_dir  = root;
      auto       repaired = pl::resize_placed(std::move(state), options, evaluate);
      const auto done     = std::chrono::steady_clock::now();
      auto       report   = timing_options(options.liberty, sdc, top);
      report.graphs       = {repaired.placed.graph};
      report.physical_rc  = repaired.placed.parasitics;
      report.dict["qor"]  = root + "/final_timing.json";
      report.set_stage_labels(report.dict);
      Pass_opentimer::time_work(report);
      ph::write_def(repaired.placed.floorplan, root + "/out.def");
      hhds::GraphLibrary output;
      if (!livehd::copy_with_callees(output, *repaired.placed.graph->get_io()->get_library(), top)) {
        throw std::runtime_error("cannot save repaired graph");
      }
      output.save(root + "/lg");
      std::ofstream(root + "/runtime.json") << "{\"engine\":\"new\",\"resize_s\":"
                                            << std::chrono::duration<double>(done - start).count()
                                            << ",\"timing_evaluations\":" << serial
                                            << ",\"timing_engine_s\":" << timing_session.statistics().seconds
                                            << ",\"timing_builds\":" << timing_session.statistics().builds << "}\n";
      return 0;
    }
    auto sizing = pl::repair_buffers(std::move(state),
                                     options,
                                     evaluate,
                                     forest_only    ? pl::Repair_scope::forest
                                     : repair_first ? pl::Repair_scope::design
                                                    : pl::Repair_scope::sizing,
                                     std::move(initial));
    std::ofstream(root + (repair_first ? "/repair.json" : "/resize.json")) << sizing.report;
    const auto sized  = std::chrono::steady_clock::now();
    auto       repair = forest_only ? std::move(sizing)
                                    : pl::repair_buffers(std::move(sizing.placed),
                                                         options,
                                                         evaluate,
                                                         sizing_forest  ? pl::Repair_scope::forest
                                                         : repair_first ? pl::Repair_scope::sizing
                                                                        : pl::Repair_scope::design_after_sizing,
                                                         std::move(sizing.timing));
    if (!forest_only) {
      std::ofstream(root + (repair_first ? "/resize.json" : "/repair.json")) << repair.report;
    }
    const auto repaired = std::chrono::steady_clock::now();
    // The diagnostic's final report is an output artifact, not feedback.
    // The optimizer never reads it or writes reports for candidate trials.
    auto       report   = timing_options(options.liberty, sdc, top);
    report.graphs       = {repair.placed.graph};
    report.physical_rc  = repair.placed.parasitics;
    report.dict["qor"]  = root + "/final_timing.json";
    report.set_stage_labels(report.dict);
    Pass_opentimer::time_work(report);
    ph::write_def(repair.placed.floorplan, root + "/out.def");
    std::ofstream colors(root + "/colors.tsv");
    for (auto node : repair.placed.graph->body().nodes()) {
      if (!node.get_subnode_io() && livehd::graph_util::type_op_of(node) != Ntype_op::Memory) {
        continue;
      }
      const auto group = node.attr(livehd::attrs::synth_color_group);
      colors << node.get_name() << '\t' << livehd::graph_util::node_color_of(node) << '\t'
             << (group.has() ? group.get() : std::string{}) << '\n';
    }

    hhds::GraphLibrary output;
    if (!livehd::copy_with_callees(output, *repair.placed.graph->get_io()->get_library(), top)) {
      throw std::runtime_error("cannot save repaired graph");
    }
    output.save(root + "/lg");
    std::ofstream(root + "/runtime.json") << "{\"sizing_s\":"
                                          << std::chrono::duration<double>(repair_first ? repaired - sized : sized - start).count()
                                          << ",\"repair_s\":"
                                          << std::chrono::duration<double>(repair_first ? sized - start : repaired - sized).count()
                                          << ",\"timing_evaluations\":" << serial
                                          << ",\"timing_engine_s\":" << timing_session.statistics().seconds
                                          << ",\"timing_builds\":" << timing_session.statistics().builds
                                          << ",\"timing_repowered_gates\":" << timing_session.statistics().repowered_gates
                                          << ",\"timing_inserted_gates\":" << timing_session.statistics().inserted_gates
                                          << ",\"timing_removed_gates\":" << timing_session.statistics().removed_gates
                                          << ",\"timing_updated_rc_nets\":" << timing_session.statistics().updated_nets << "}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
