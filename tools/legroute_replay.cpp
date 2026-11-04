// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Route a saved legal placement without synthesis, GP, legalization, or cell movement.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>

#include "pass/place/legroute.hpp"
#include "pass/place/power_grid.hpp"
#include "rapidjson/document.h"

int main(int argc, char** argv) {
  namespace ph = livehd::physical;
  namespace pl = livehd::placement;
  namespace fs = std::filesystem;
  try {
    if (argc != 2) {
      throw std::invalid_argument("usage: legroute_replay config.json");
    }
    std::ifstream       input(argv[1]);
    const std::string   text{std::istreambuf_iterator<char>(input), {}};
    rapidjson::Document config;
    config.Parse(text.c_str());
    if (config.HasParseError() || !config.IsObject()) {
      throw std::invalid_argument("invalid configuration JSON");
    }
    std::set<std::string> known{"lefs",
                                "def",
                                "timing",
                                "output",
                                "power_grid",
                                "reserved_layers",
                                "final_box_dump",
                                "box_reserve"};  // keys read directly below; the typed helpers add theirs
    const auto            string = [&](const char* key) -> std::string {
      known.insert(key);
      if (!config.HasMember(key) || !config[key].IsString()) {
        throw std::invalid_argument(std::string("missing string: ") + key);
      }
      return config[key].GetString();
    };
    const auto boolean = [&](const char* key, bool& value) {
      known.insert(key);
      if (config.HasMember(key)) {
        if (!config[key].IsBool()) {
          throw std::invalid_argument(std::string("expected boolean: ") + key);
        }
        value = config[key].GetBool();
      }
    };
    const auto integer = [&](const char* key, unsigned& value) {
      known.insert(key);
      if (config.HasMember(key)) {
        if (!config[key].IsUint()) {
          throw std::invalid_argument(std::string("expected unsigned integer: ") + key);
        }
        value = config[key].GetUint();
      }
    };
    const auto number = [&](const char* key, double& value) {
      known.insert(key);
      if (config.HasMember(key)) {
        if (!config[key].IsNumber() || !std::isfinite(config[key].GetDouble())) {
          throw std::invalid_argument(std::string("expected finite number: ") + key);
        }
        value = config[key].GetDouble();
      }
    };
    if (!config.HasMember("lefs") || !config["lefs"].IsArray()) {
      throw std::invalid_argument("missing lefs array");
    }
    std::vector<std::string> lefs;
    for (const auto& lef : config["lefs"].GetArray()) {
      if (!lef.IsString()) {
        throw std::invalid_argument("LEF paths must be strings");
      }
      lefs.emplace_back(lef.GetString());
    }
    auto tech = ph::read_lef(lefs);
    auto fp   = ph::read_def(string("def"));
    tech.dbu  = fp.dbu;
    for (auto& c : fp.components) {
      const auto& master = tech.macros.at(c.macro);
      c.place.width      = std::llround(master.width * fp.dbu);
      c.place.height     = std::llround(master.height * fp.dbu);
      if (ph::swaps_axes(c.place.orientation)) {
        std::swap(c.place.width, c.place.height);
      }
    }
    // The witness contributes placement and constraints, never pre-existing signal wires.
    for (auto& net : fp.nets) {
      if (net.use != "POWER" && net.use != "GROUND") {
        net.wires.clear();
      }
    }
    const auto               components = fp.components;
    const auto               terminals  = fp.terminals;
    livehd::timing::Feedback timing;
    timing.time_unit_ps = 1;
    if (config.HasMember("timing")) {
      std::ifstream feedback(string("timing"));
      if (!feedback) {
        throw std::invalid_argument("cannot read timing feedback");
      }
      std::string line;
      while (std::getline(feedback, line)) {
        if (line.empty() || line.front() == '#') {
          continue;
        }
        std::istringstream row(line);
        std::string        pin;
        double             slack = 0;
        int                clock = 0;
        if (!(row >> std::quoted(pin) >> slack >> clock) || !std::isfinite(slack) || (clock != 0 && clock != 1)) {
          throw std::invalid_argument("invalid timing feedback line");
        }
        timing.pins[pin].slack = slack;
        timing.pins[pin].clock = clock;
      }
    }
    pl::Legroute_options options;
    bool                 plan_only                     = false;
    bool                 boxed_handoff                 = false;
    bool                 pg_tolerate_macro_pin_overlap = false;
    // movable: start from the saved pose as a *placement* (for example an OpenROAD witness), not a legal fixed
    // one: plan screens fit and the global route first, then local moves cell-boxes (fit_first, box seed).
    bool                 movable                       = false;
    unsigned             plan_net_pops                 = 100000;
    boolean("plan_only", plan_only);
    boolean("boxed_handoff", boxed_handoff);
    boolean("pg_tolerate_macro_pin_overlap", pg_tolerate_macro_pin_overlap);
    boolean("movable", movable);
    options.legal_boxes = options.global_route = options.global_all = options.global_keep = true;
    if (movable) {
      options.legal_boxes = options.global_keep = false;
      options.box_route_seed = options.fit_first = true;
    }
    options.region_pins = false;
    boolean("final", options.final);
    boolean("global_keep", options.global_keep);
    boolean("global_pattern", options.global_pattern);
    boolean("box_route_seed", options.box_route_seed);
    boolean("fit_first", options.fit_first);
    number("fit_density", options.fit_density);
    number("density_ceiling", options.density_ceiling);
    boolean("final_box_dump_stop", options.final_box_dump_stop);
    if (const auto it = config.FindMember("final_box_dump"); it != config.MemberEnd()) {
      if (!it->value.IsString()) {
        throw std::invalid_argument("replay config final_box_dump must be a string col:row;...");
      }
      options.final_box_dump = it->value.GetString();
    }
    if (const auto it = config.FindMember("box_reserve"); it != config.MemberEnd()) {
      if (!it->value.IsString()) {
        throw std::invalid_argument("replay config box_reserve must be a string LAYER:N,...");
      }
      options.box_reserve = it->value.GetString();
    }
    integer("fit_window", options.fit_window);
    integer("window_tiles", options.window_tiles);
    boolean("clock_spine", options.clock_spine);
    boolean("protect_supply_access", options.protect_supply_access);
    boolean("partition_mesh", options.partition_mesh);
    integer("partition_cells", options.partition_cells);
    integer("region_cell_limit", options.region_cell_limit);
    integer("partition_timeout_rounds", options.partition_timeout_rounds);
    integer("sat_ms", options.sat_ms);
    integer("threads", options.threads);
    integer("route_ms", options.route_ms);
    integer("route_access_per_pin", options.route_access_per_pin);
    boolean("final_drop_fallback_planned", options.final_drop_fallback_planned);
    boolean("l5_fallback_shadow", options.l5_fallback_shadow);
    boolean("l5_rule_reach", options.l5_rule_reach);
    boolean("final_clock_first", options.final_clock_first);
    boolean("final_precommit_connectivity", options.final_precommit_connectivity);
    integer("final_clock_route_ms", options.final_clock_route_ms);
    integer("final_clock_route_patterns", options.final_clock_route_patterns);
    boolean("final_box_report", options.final_box_report);
    boolean("final_clock_detail_first", options.final_clock_detail_first);
    boolean("final_skip_clock", options.final_skip_clock);
    if (const auto it = config.FindMember("final_skip_nets"); it != config.MemberEnd()) {
      known.insert("final_skip_nets");
      if (!it->value.IsString()) {
        throw std::invalid_argument("expected string: final_skip_nets");
      }
      options.final_skip_nets = it->value.GetString();
    }
    integer("route_patterns", options.route_patterns);
    integer("route_access", options.route_access);
    integer("route_passes", options.route_passes);
    integer("final_net_limit", options.final_net_limit);
    integer("global_rounds", options.global_rounds);
    integer("global_net_pops", options.global_net_pops);
    integer("plan_net_pops", plan_net_pops);
    integer("reserved_tracks", options.reserved_tracks);
    number("reserved_tracks_per_um", options.reserved_tracks_per_um);
    if (config.HasMember("reserved_layers")) {
      options.reserved_layers = string("reserved_layers");
    }
    number("global_share", options.global_share);
    number("box_fill", options.box_fill);
    integer("box_net_max", options.box_net_max);
    number("merge_cost", options.merge_cost);
    number("global_astar_weight", options.global_astar_weight);
    number("partition_critical_cost", options.partition_critical_cost);
    pl::validate_legroute_options(options);
    for (const auto& member : config.GetObject()) {  // an unread key would silently not vary anything
      if (!known.contains(member.name.GetString())) {
        std::cerr << "legroute_replay: warning: unknown configuration key ignored: " << member.name.GetString() << '\n';
      }
    }
    const auto output = string("output");
    if (fs::exists(output)) {
      throw std::invalid_argument("output directory already exists; use a fresh directory");
    }
    fs::create_directories(output);
    options.output_dir = output;
    fs::copy_file(argv[1], fs::path(output) / "config.json");
    if (config.HasMember("power_grid")) {
      auto pg_profile                       = pl::power_grid_profile(string("power_grid"), tech);
      pg_profile.tolerate_macro_pin_overlap = pg_tolerate_macro_pin_overlap;
      fp                                    = pl::make_power_grid(tech, fp, pg_profile, nullptr, true);
      if (fp.components != components) {
        throw std::runtime_error("power grid changed the saved cells");
      }
      for (const auto& terminal : terminals) {
        if (std::count(fp.terminals.begin(), fp.terminals.end(), terminal) != 1) {
          throw std::runtime_error("power grid changed the saved IO geometry");
        }
      }
    }
    // Share this exact physical input with an independent router, including any
    // generated PG metal. Signal cells and IOs above remain those of the witness.
    ph::write_def(fp, output + "/input.def");
    const auto               routing_terminals = fp.terminals;
    const auto*              profile           = ph::detect_tech_profile(tech);
    pl::Legroute_report      report;
    pl::Legroute_plan_report plan;
    if (plan_only) {
      plan = pl::legroute_plan(tech, fp, timing, options, {}, profile, plan_net_pops, boxed_handoff);
      pl::write_legroute_plan_failures(plan, output + "/plan_failures.tsv");
      std::ofstream output_plan(output + "/plan.json");
      output_plan << std::format(
          R"({{"route_attempted":{},"route_skip_reason":"{}","fit_reason":"{}","fit_matched":{},"fit_movable_boxes":{},"fit_free_tiles":{},"fit_far_boxes":{},"fit_max_move":{},"fit_dense_windows":{},"fit_windows":{},"unchecked_signals":{},"signals":{},"timed_signals":{},"unrouted":{},"search_limited":{},"topology_disconnected":{},"overflow_edges":{},"overflow_tracks":{},"search_pops":{},"global_astar_weight":{},"global_pattern":{},"pattern_attempts":{},"pattern_completed":{},"pattern_fallbacks":{},"pattern_work":{},"pattern_work_limited":{},"seconds":{},"endpoint_basis":"{}","box_route_seed_attempted":{},"box_route_seed_accepted":{},"pre_sat":{}}})",
          plan.route_attempted,
          plan.route_skip_name(),
          plan.fit_reason,
          plan.fit_matched,
          plan.fit_movable_boxes,
          plan.fit_free_tiles,
          plan.fit_far_boxes,
          plan.fit_max_move,
          plan.fit_dense_windows,
          plan.fit_windows,
          plan.unchecked_signals,
          plan.signals,
          plan.timed_signals,
          plan.unrouted,
          plan.search_limited,
          plan.topology_disconnected,
          plan.overflow_edges,
          plan.overflow_tracks,
          plan.search_pops,
          plan.global_astar_weight,
          options.global_pattern,
          plan.pattern_attempts,
          plan.pattern_completed,
          plan.pattern_fallbacks,
          plan.pattern_work,
          plan.pattern_work_limited,
          plan.seconds,
          plan.endpoint_basis,
          plan.box_route_seed_attempted,
          plan.box_route_seed_accepted,
          pl::legroute_gate_json(plan.gates))
                  << '\n';
      if (!output_plan) {
        throw std::runtime_error("cannot write plan report");
      }
    } else {
      if (movable) {  // the plan verdict on this pose first, exactly as the flow would see it
        plan = pl::legroute_plan(tech, fp, timing, options, {}, profile, plan_net_pops, false);
        std::cout << "Plan: route " << (plan.route_attempted ? "attempted" : plan.route_skip_name()) << "; fit matched "
                  << plan.fit_matched << " far " << plan.fit_far_boxes << " max move " << plan.fit_max_move << "; dense windows "
                  << plan.fit_dense_windows << "/" << plan.fit_windows << " max " << plan.fit_max_density << "; unresolved "
                  << plan.unrouted << "; overflow tracks " << plan.overflow_tracks << "; replan " << plan.gates.requires_replan()
                  << "; seconds " << plan.seconds << '\n';
        if (plan.gates.requires_replan()) {
          std::cout << "Plan gates: " << pl::legroute_gate_json(plan.gates).substr(0, 600) << '\n';
          return 1;
        }
      }
      report = pl::legroute(tech, fp, timing, options, {}, profile);
    }
    if (!movable && (fp.components != components || fp.terminals != routing_terminals)) {
      throw std::runtime_error("legroute changed the saved placement or IO geometry");
    }
    ph::write_def(fp, output + "/out.def");
    std::ofstream audit(output + "/placement_audit.json");
    size_t        moved_xy = 0, reoriented = 0;
    for (size_t c = 0; c < components.size() && c < fp.components.size(); ++c) {
      const auto& a = components[c].place;
      const auto& b = fp.components[c].place;
      if (a.x != b.x || a.y != b.y) {
        ++moved_xy;
      } else if (a.orientation != b.orientation) {
        ++reoriented;
      }
    }
    const bool unchanged = fp.components == components && fp.terminals == routing_terminals;
    audit << "{\"unchanged\":" << (unchanged ? "true" : "false") << ",\"xy_moved\":" << moved_xy
          << ",\"orientation_only\":" << reoriented << ",\"cells_moved\":" << report.cells_moved
          << ",\"cell_max_displacement_um\":" << report.cell_max_displacement_um
          << ",\"cell_p95_displacement_um\":" << report.cell_p95_displacement_um << ",\"components\":" << components.size()
          << ",\"terminals\":" << terminals.size() << ",\"routing_terminals\":" << routing_terminals.size()
          << ",\"power_grid_generated\":" << (config.HasMember("power_grid") ? "true" : "false")
          << ",\"pg_tolerate_macro_pin_overlap\":" << (pg_tolerate_macro_pin_overlap ? "true" : "false")
          << ",\"protect_supply_access\":" << (options.protect_supply_access ? "true" : "false")
          << ",\"timing_supplied\":" << (config.HasMember("timing") ? "true" : "false") << "}\n";
    if (!audit) {
      throw std::runtime_error("cannot write placement audit");
    }
    if (plan_only) {
      if (plan.route_attempted) {
        std::cout << "Planning unresolved " << plan.unrouted << "; overflow tracks " << plan.overflow_tracks << "; seconds "
                  << plan.seconds << '\n';
      } else {
        std::cout << "Planning routing not attempted: " << plan.route_skip_name() << "; unchecked signals "
                  << plan.unchecked_signals << "; fit " << (plan.fit_reason.empty() ? "ok" : plan.fit_reason) << " boxes "
                  << plan.fit_movable_boxes << " free tiles " << plan.fit_free_tiles << " dense windows " << plan.fit_dense_windows
                  << "/" << plan.fit_windows << "; seconds " << plan.seconds << '\n';
      }
      return plan.gates.requires_replan() ? 1 : 0;
    }
    if (report.crossing_policy_invalid) {
      std::cout << "Crossings over the density ceiling in " << report.crossing_over_policy
                << " SAT round(s): the result does not meet the policy\n";
    }
    std::cout << "SAT failures " << report.sat_unsat_regions + report.sat_unknown_regions << "; unassigned tracks "
              << report.unassigned_segments << "; wires emitted " << report.wires_emitted << "; failed nets " << report.failed_nets
              << "; global unresolved " << report.global_unrouted << "; global overflow tracks " << report.global_overflow_tracks
              << "; topology-disconnected signals " << report.global_topology_disconnected << "; search-limited attempts "
              << report.global_search_limited << "; seconds " << report.seconds << '\n';
    return report.sat_unsat_regions || report.sat_unknown_regions || report.crossing_policy_invalid
                   || (options.final ? !report.wires_emitted
                                     : report.unassigned_segments || report.global_unrouted || report.global_overflow_tracks)
               ? 1
               : 0;
  } catch (const pl::Legroute_replan& error) {
    std::cerr << error.what() << '\n';
    return 1;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 2;
  }
}
