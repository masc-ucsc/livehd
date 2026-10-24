//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// `lhd synth`: the one-shot synthesis flow.
//
//   compile -> optional pass.color reduce -> pass.color synth -> pass.abc -> pass.opentimer
//
// over ONE in-memory design. Reduction is experimental and disabled by default.
// The steps, including optional reduction, run by hand as
//
//   lhd compile cpu.prp --top Cpu --emit-dir lg:L --workdir W
//   lhd pass color reduce --top Cpu.Cpu lg:L --workdir W
//   lhd pass color synth --top Cpu.Cpu lg:L --workdir W
//   lhd pass abc   --top Cpu.Cpu lg:L --emit-dir lg:N --workdir W
//   lhd pass opentimer --top Cpu.Cpu lg:N cells.lib --workdir W
//
// and the manual steps stay the way to run a DIFFERENT coloring or to inspect
// the intermediates. What the fused command changes:
//
//   * --top is resolved ONCE (a bare entity name is enough) and every pass
//     gets the full internal `file.entity` name;
//   * the coloring is `synth`, always: per-(def, color) regions are what keep
//     a big design inside ABC's memory budget and what make pass.abc's
//     per-region reuse possible (`flat` fuses the design into one region by
//     construction). Other colorings are the manual steps;
//   * a user-supplied lg: input is READ-ONLY — the coloring happens on the
//     in-memory graphs (pass color alone rewrites its input in place);
//   * one Liberty (`synth.liberty`) feeds both pass.abc and pass.opentimer;
//   * --workdir is optional. With one, <workdir>/synth/ keeps the compiled
//     design (`lg/`), the mapped netlist (`net/`), `qor.json` and
//     `timing.json`, and the incremental tiers (compile cache, abc_cache/)
//     are live under the same `lhd.incremental` switch every command shares.
//     Without one, the flow runs in a scratch dir and only --emit-dir lg:/
//     verilog:/report: and the printed report survive.
//
// Reuse is a speedup, never an oracle: a warm run produces the same netlist
// as a cold one (the abc region digest is content-based, the coloring is
// seeded and deterministic).

#include "debug_env.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "liberty_dff.hpp"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_split.h"
#include "attrs.hpp"
#include "diag.hpp"
#include "export_file.hpp"
#include "graph_library_singleton.hpp"
#include "lhd_kernel_internal.hpp"
#include "node_util.hpp"
#include "pass.hpp"
#include "pass/opentimer/pass_opentimer.hpp"
#include "pass/opentimer/sta_salt.hpp"
#include "pass/place/io_timing.hpp"
#include "pass/place/place.hpp"
#include "pass/place/repair.hpp"
#include "pass/place/resize_engine.hpp"
#include "physical_cache.hpp"
#include "physical_salt.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "synth_policy.hpp"

namespace lhd {

namespace {

bool truthy(std::string_view v) { return !v.empty() && v != "false" && v != "0" && v != "off"; }

std::string physical_descriptor(const Options& opts) {
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
  w.StartArray();
  w.Uint64(kPhysicalSrcSalt);
  w.String(opts.top.c_str());
  w.String(opts.seed.c_str());
  w.Uint(opts.jobs);
  for (const auto& [key, value] : opts.sets) {
    w.StartArray();
    w.String(key.c_str());
    w.String(value.c_str());
    w.EndArray();
  }
  // Experimental process-level overrides must not silently replay a different
  // physical/synthesis policy. Normal runs leave these unset.
  const std::pair<const char*, const char*> env_overrides[] = {
      {"LHD_ABC_INCR_COMPARE_ONLY", livehd::debug::env<"LHD_ABC_INCR_COMPARE_ONLY">()},
      { "LHD_ABC_INCR_NO_BLACKBOX",  livehd::debug::env<"LHD_ABC_INCR_NO_BLACKBOX">()},
      {"LHD_ABC_INCR_NO_TRAVERSAL", livehd::debug::env<"LHD_ABC_INCR_NO_TRAVERSAL">()},
      {        "LHD_RESIZE_PHASES",         livehd::debug::env<"LHD_RESIZE_PHASES">()},
      {          "OMP_NUM_THREADS",                    std::getenv("OMP_NUM_THREADS")},
      {     "LHD_MEMORY_BUDGET_MB",               std::getenv("LHD_MEMORY_BUDGET_MB")},
  };
  for (const auto& [name, value] : env_overrides) {
    w.String(name);
    w.String(value ? value : "");
  }
  w.EndArray();
  return buffer.GetString();
}

std::string restored_io_report(const std::string& text) {
  rapidjson::Document old, out;
  old.Parse(text.c_str());
  out.SetObject();
  auto& a = out.GetAllocator();
  out.AddMember("schema", 1, a);
  if (!old.HasParseError() && old.IsObject() && old.HasMember("percent")) {
    out.AddMember("percent", rapidjson::Value(old["percent"], a), a);
  }
  for (const auto* key : {"restarts", "checked", "fired", "accepted", "rejected", "warnings"}) {
    out.AddMember(rapidjson::Value(key, a).Move(), 0, a);
  }
  out.AddMember("seconds", 0.0, a);
  out.AddMember("restarted_stage", "", a);
  out.AddMember("reused_final_state", true, a);
  std::map<std::string, const rapidjson::Value*> final;
  if (!old.HasParseError() && old.IsObject() && old.HasMember("colors") && old["colors"].IsArray()) {
    for (const auto& sample : old["colors"].GetArray()) {
      if (sample.IsObject() && sample.HasMember("module") && sample["module"].IsString() && sample.HasMember("placed")) {
        final[sample["module"].GetString()] = &sample;
      }
    }
  }
  rapidjson::Value colors(rapidjson::kArrayType);
  for (const auto& [module, sample] : final) {
    rapidjson::Value row(rapidjson::kObjectType);
    row.AddMember("module", rapidjson::Value(module.c_str(), a), a);
    row.AddMember("stage", "restored_final", a);
    row.AddMember("decision", "restored repaired netlist and boundary timing", a);
    row.AddMember("checked", false, a);
    row.AddMember("automatic", false, a);
    row.AddMember("fired", false, a);
    row.AddMember("accepted", false, a);
    row.AddMember("tightening_ps", 0.0, a);
    row.AddMember("used", rapidjson::Value((*sample)["placed"], a), a);
    row.AddMember("placed", rapidjson::Value((*sample)["placed"], a), a);
    colors.PushBack(row, a);
  }
  out.AddMember("colors", colors, a);
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  out.Accept(writer);
  return buffer.GetString();
}

livehd::placement::Options placement_options(const Options& opts) {
  livehd::placement::Options out;
  auto                       number = [&](std::string_view key, std::string_view fallback) {
    const auto   text  = synth_set(opts, key, fallback);
    char*        end   = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end != text.c_str() + text.size() || end == text.c_str() || !std::isfinite(value)) {
      throw Lhd_error{"usage", "invalid synth." + std::string(key) + ": " + text, "expected a finite number"};
    }
    return value;
  };
  auto integer = [&](std::string_view key, std::string_view fallback) {
    const double value = number(key, fallback);
    if (value < 0 || value > 1000000 || std::floor(value) != value) {
      throw Lhd_error{"usage", "invalid synth." + std::string(key), "expected a nonnegative integer <= 1000000"};
    }
    return static_cast<int>(value);
  };
  std::string lefs = synth_set(opts, "lef", "");
  if (lefs.empty()) {
    const char* tech = std::getenv("LHD_TECH_DIR");
    if (tech == nullptr || *tech == '\0') {
      throw Lhd_error{"missing_file", "placement needs synth.lef or $LHD_TECH_DIR", "set synth.lef=tech.lef,cells.lef"};
    }
    lefs = (fs::path(tech) / "../techlef/sky130_fd_sc_hd__nom.tlef").string() + ","
           + (fs::path(tech) / "../lef/sky130_fd_sc_hd.lef").string();
  }
  size_t begin = 0;
  for (;;) {
    const auto comma = lefs.find(',', begin);
    auto       path  = lefs.substr(begin, comma == std::string::npos ? comma : comma - begin);
    if (path.empty()) {
      throw Lhd_error{"usage", "empty entry in synth.lef", "expected tech.lef,cells.lef"};
    }
    out.lefs.push_back(path);
    if (comma == std::string::npos) {
      break;
    }
    begin = comma + 1;
  }
  check_inputs_exist(out.lefs);
  out.floorplan = synth_set(opts, "place.floorplan", "");
  if (!out.floorplan.empty()) {
    check_inputs_exist({out.floorplan});
  }
  const auto mode = synth_set(opts, "place.mode", "full");
  if (mode != "full" && mode != "gp") {
    throw Lhd_error{"usage", "synth.place.mode must be full or gp", ""};
  }
  out.full                    = mode == "full";
  out.fp_timing_interval      = static_cast<int>(integer("place.fp.timing_interval", "50"));
  out.fp_timing_movement      = number("place.fp.timing_movement", "0");
  out.fp_timing_after_density = truthy(synth_set(opts, "place.fp.timing_after_density", "false"));
  out.eco_connectivity_weight = number("place.legalize.eco_connectivity_weight", "0");
  if (out.eco_connectivity_weight < 0 || out.eco_connectivity_weight > 100) {
    throw Lhd_error{"usage", "invalid ECO connectivity weight", "expected 0..100"};
  }
  out.legalize_timing_corrections = integer("place.legalize.timing_corrections", "0");
  for (const auto& stage : {std::string("fp"), std::string("macro")}) {
    auto&      policy = stage == "fp" ? out.fp_warp : out.macro_warp;
    const auto key    = "place." + stage + ".warpspace";
    policy.enabled    = truthy(synth_set(opts, key, "false"));
    policy.weight     = number(key + ".weight", stage == "fp" ? "1" : "0.25");
    policy.bins       = integer(key + ".bins", "16");
    policy.iterations = integer(key + ".iterations", "2");
    policy.seconds    = number(key + ".seconds", "3");
    if (stage == "macro") {
      policy.once_after_density = truthy(synth_set(opts, key + ".once_after_density", "false"));
      policy.updates            = integer(key + ".updates", "10");
      policy.warmup             = number(key + ".warmup", "0.1");
    }
    livehd::placement::validate_warp_options(policy);
  }
  out.terminal_legalize           = truthy(synth_set(opts, "place.legalize.terminal", "true"));
  out.boundarypins.enabled             = truthy(synth_set(opts, "place.boundarypins", "false"));
  out.boundarypins.split               = truthy(synth_set(opts, "place.boundarypins.split", "true"));
  out.boundarypins.order               = truthy(synth_set(opts, "place.boundarypins.order", "false"));
  out.boundarypins.iterations          = integer("place.boundarypins.iterations", "50");
  out.boundarypins.max_displacement_um = number("place.boundarypins.max_displacement_um", "20");
  out.boundarypins.align               = truthy(synth_set(opts, "place.pinalign", "false"));
  livehd::placement::validate_boundary_pin_options(out.boundarypins);
  out.busorder.enabled             = truthy(synth_set(opts, "place.busorder", "false"));
  out.busorder.iterations          = integer("place.busorder.iterations", "100");
  out.busorder.refresh             = integer("place.busorder.refresh", "20");
  out.busorder.max_cells           = integer("place.busorder.max_cells", "10000");
  out.busorder.weight              = number("place.busorder.weight", "4");
  out.busorder.merge_um            = number("place.busorder.merge_um", "5");
  out.busorder.max_displacement_um = number("place.busorder.max_displacement_um", "20");
  livehd::placement::validate_busorder_options(out.busorder);
  auto& terminal                  = out.terminal_legalize_options;
  terminal.remainder_warp.enabled = truthy(synth_set(opts, "place.legalize.warpspace", "false"));
  terminal.remainder_warp.weight  = number("place.legalize.warpspace.weight", "1");
  terminal.remainder_warp.bins    = integer("place.legalize.warpspace.bins", "16");
  terminal.remainder_warp.seconds = number("place.legalize.warpspace.seconds", "3");
  livehd::placement::validate_warp_options(terminal.remainder_warp);
  terminal.max_density            = number("place.legalize.max_density", "0.8");
  terminal.critical_fraction      = number("place.legalize.critical_fraction", "0.1");
  terminal.local_pair_fraction    = number("place.legalize.local_pair_fraction", "0.5");
  terminal.padding_sites          = integer("place.legalize.padding_sites", "1");
  terminal.remainder_wire_weight  = number("place.legalize.remainder_wire_weight", "4");
  terminal.padding_max_extra_rows = integer("place.legalize.padding_max_extra_rows", "0");
  terminal.critical_padding_sites = integer("place.legalize.critical_padding_sites", "0");
  terminal.critical_fanin_depth   = integer("place.legalize.critical_fanin_depth", "0");
  terminal.joint_minimize_total   = truthy(synth_set(opts, "place.legalize.joint_minimize_total", "true"));
  terminal.candidates             = integer("place.legalize.candidates", "12");
  terminal.pair_move_sites        = integer("place.legalize.pair_move_sites", "4");
  terminal.pair_move_rows         = integer("place.legalize.pair_move_rows", "1");
  terminal.critical_pair_limit    = integer("place.legalize.critical_pair_limit", "256");
  terminal.local_pair_limit       = integer("place.legalize.local_pair_limit", "0");
  if (out.legalize_timing_corrections > 2) {
    throw Lhd_error{"usage", "timing corrections expect 0..2", ""};
  }
  out.legalize_timing_strength  = number("place.legalize.timing_strength", "0");
  out.legalize_timing_threshold = number("place.legalize.timing_threshold", "0");
  if (out.legalize_timing_strength < 0 || out.legalize_timing_strength > 99 || out.legalize_timing_threshold < 0
      || out.legalize_timing_threshold > 1) {
    throw Lhd_error{"usage", "invalid timing legalization controls", "strength: 0..99, threshold: 0..1"};
  }
  out.timing_fraction        = number("place.timing_fraction", "0.2");
  out.timing_max_weight      = number("place.timing_max_weight", "5");
  out.timing_exponent        = number("place.timing_exponent", "1");
  out.timing_spread_weight   = number("place.timing_spread_weight", "0");
  out.macro_timing_interval  = static_cast<int>(integer("place.macro.timing_interval", "50"));
  out.timing_smoothing       = number("place.timing_smoothing", "0.5");
  out.fp_timing_max_updates  = integer("place.fp.timing_max_updates", "10");
  out.stop_overflow          = number("place.stop_overflow", "0.1");
  out.initial_density_weight = number("place.initial_density_weight", "0.01");
  if (out.timing_max_weight < 1 || out.timing_max_weight > livehd::placement::max_net_weight || out.timing_smoothing < 0
      || out.timing_smoothing > 1 || out.fp_timing_max_updates < 2 || out.stop_overflow < 0 || out.stop_overflow > 1
      || out.initial_density_weight <= 0 || out.timing_exponent < 1 || out.macro_timing_interval < 0 || out.timing_spread_weight < 0
      || out.timing_spread_weight > 1) {
    throw Lhd_error{"usage", "invalid synth.place timing/density tuning controls", "check lhd describe synth for allowed values"};
  }
  if (out.timing_fraction <= 0 || out.timing_fraction > 1) {
    throw Lhd_error{"usage", "synth.place.timing_fraction expects a fraction in (0,1]", "use 0.1 for 10% or 0.29 for 29%"};
  }
  out.snapshots = truthy(synth_set(opts, "place.snapshots", "false"));
  out.cohesion  = number("place.cohesion", "0");
  if (out.cohesion < 0) {
    throw Lhd_error{"usage", "synth.place.cohesion must be nonnegative", ""};
  }
  out.utilization = number("place.utilization", "0.30");
  out.aspect      = number("place.aspect", "1");
  for (const auto& [key, frame] : {
           std::pair<const char*, std::vector<double>*>{ "place.die",  &out.die},
           std::pair<const char*, std::vector<double>*>{"place.core", &out.core}
  }) {
    const auto text = synth_set(opts, key, "");
    if (text.empty()) {
      continue;
    }
    std::string spaced = text;
    std::replace(spaced.begin(), spaced.end(), ',', ' ');
    std::istringstream words(spaced);
    for (std::string word; words >> word;) {
      char*        end   = nullptr;
      const double value = std::strtod(word.c_str(), &end);
      if (end != word.c_str() + word.size() || !std::isfinite(value)) {
        throw Lhd_error{"usage", "invalid synth." + std::string(key) + ": " + text, "expected xl,yl,xh,yh in microns"};
      }
      frame->push_back(value);
    }
    if (frame->size() != 4 || (*frame)[2] <= (*frame)[0] || (*frame)[3] <= (*frame)[1] || (*frame)[0] < 0 || (*frame)[1] < 0) {
      throw Lhd_error{"usage",
                      "invalid synth." + std::string(key) + ": " + text,
                      "expected xl,yl,xh,yh in microns with xh>xl, yh>yl"};
    }
  }
  if (!out.floorplan.empty() && (!out.die.empty() || !out.core.empty())) {
    throw Lhd_error{"usage", "synth.place.die/core cannot be combined with synth.place.floorplan", "supply one frame source"};
  }
  out.density               = number("place.density", "0.5");
  out.iterations            = integer("place.iterations", "1500");
  out.min_iterations        = integer("place.min_iterations", "40");
  out.post_density_factor   = integer("place.post_density_factor", "4");
  out.convergence_tolerance = number("place.convergence_tolerance", "0.01");
  out.detailed_iterations   = integer("place.detailed_iterations", "2");
  out.macro_detailed_place  = truthy(synth_set(opts, "place.macro.detailed", "true"));
  out.macro_legalize_cells  = truthy(synth_set(opts, "place.macro.legalize_cells", "true"));
  out.final_independent_timing = truthy(synth_set(opts, "place.final.independent_timing", "false"));
  out.satlegalize           = truthy(synth_set(opts, "place.satlegalize", "false"));
  auto& sat                 = out.satlegalize_options;
  sat.backend               = synth_set(opts, "place.satlegalize.backend", "cadical");
  sat.encoding              = synth_set(opts, "place.satlegalize.encoding", "candidate");
  if ((sat.backend != "cadical" && sat.backend != "cms-cnf" && sat.backend != "cms-xor")
      || (sat.encoding != "candidate" && sat.encoding != "binary")) {
    throw Lhd_error{"usage",
                    "invalid synth.place.satlegalize backend or encoding",
                    "expected cadical|cms-cnf|cms-xor and candidate|binary"};
  }
  const auto sat_limit = [&](const char* key, const char* fallback, int maximum, bool zero = false) {
    // Not integer(): its 1e6 cap would hide the larger conflicts budget.
    const double value = number(key, fallback);
    if (value < (zero ? 0 : 1) || value > maximum || std::floor(value) != value) {
      throw Lhd_error{"usage", std::string("invalid synth.") + key, "SAT budget out of range"};
    }
    return static_cast<unsigned>(value);
  };
  sat.max_cells            = sat_limit("place.satlegalize.max_cells", "24", 65536);
  sat.max_candidates       = sat_limit("place.satlegalize.max_candidates", "12", 128);
  sat.max_windows          = sat_limit("place.satlegalize.max_windows", "8", 1024);
  sat.max_pairs            = sat_limit("place.satlegalize.max_pairs", "32", 1024);
  sat.max_pairs_per_window = sat_limit("place.satlegalize.max_pairs_per_window", "0", 1024, true);
  sat.check_packing        = truthy(synth_set(opts, "place.satlegalize.check_packing", "false"));
  sat.max_moved_cells      = sat_limit("place.satlegalize.max_moved_cells", "0", 128, true);
  sat.move_sites           = sat_limit("place.satlegalize.move_sites", "4", 128, true);
  sat.move_rows            = sat_limit("place.satlegalize.move_rows", "1", 16, true);
  sat.route_grid           = truthy(synth_set(opts, "place.satlegalize.route_grid", "false"));
  sat.route_guidance       = truthy(synth_set(opts, "place.satlegalize.route_guidance", "true"));
  sat.route_peak_floor     = number("place.satlegalize.route_peak_floor", "0");
  if (!std::isfinite(sat.route_peak_floor) || sat.route_peak_floor < 0 || sat.route_peak_floor > 1) {
    throw Lhd_error{"usage", "invalid synth.place.satlegalize.route_peak_floor", "expected 0..1"};
  }
  sat.spill_neighbors       = truthy(synth_set(opts, "place.satlegalize.spill_neighbors", "false"));
  sat.spill_slack_margin_ps = number("place.satlegalize.spill_slack_margin_ps", "0");
  if (!std::isfinite(sat.spill_slack_margin_ps) || sat.spill_slack_margin_ps < 0) {
    throw Lhd_error{"usage", "invalid synth.place.satlegalize.spill_slack_margin_ps", "expected a finite nonnegative margin in ps"};
  }
  sat.conflicts       = sat_limit("place.satlegalize.conflicts", "10000", 1000000000);
  sat.milliseconds    = sat_limit("place.satlegalize.milliseconds", "50", 60000);
  sat.skip_conflicts  = truthy(synth_set(opts, "place.satlegalize.skip_conflicts", "true"));
  sat.boundary_growth = number("place.satlegalize.boundary_growth", "0.05");
  if (sat.boundary_growth < 0 || sat.boundary_growth > 1) {
    throw Lhd_error{"usage", "invalid synth.place.satlegalize.boundary_growth", "expected 0..1"};
  }

  out.density_upper_bound        = truthy(synth_set(opts, "place.density_upper_bound", "false"));
  out.fixed_macro_overlap_weight = number("place.fixed_macro_overlap", "0");
  if (out.min_iterations < 0 || out.post_density_factor < 0 || out.post_density_factor > 100 || out.convergence_tolerance < 0
      || out.convergence_tolerance > 1 || out.detailed_iterations < 1 || out.detailed_iterations > 100
      || out.fixed_macro_overlap_weight < 0) {
    throw Lhd_error{"usage", "invalid synth.place cell-placement controls", ""};
  }
  out.bins                      = integer("place.bins", "64");
  out.dynamic_bins              = truthy(synth_set(opts, "place.dynamic_bins", "false"));
  out.virtual_fillers             = truthy(synth_set(opts, "place.virtual_fillers", "true"));
  out.virtual_filler_max_count    = integer("place.virtual_filler_max_count", "0");
  out.virtual_filler_fraction   = number("place.virtual_filler_fraction", "1");
  out.virtual_filler_cell_scale   = number("place.virtual_filler_cell_scale", "1");
  out.virtual_filler_bin_fraction = number("place.virtual_filler_bin_fraction", "0.25");
  out.bin_max                   = integer("place.bin_max", "512");
  out.local_density_weight      = number("place.local_density_weight", "0");
  out.local_density_bins        = integer("place.local_density_bins", "512");
  out.local_density_range       = number("place.local_density_range", "4");
  out.local_density_jitter      = number("place.local_density_jitter", "0.01");
  out.macro_cell_density_weight = number("place.macro.cell_density_weight", "0");
  out.macro_density_pull_weight = number("place.macro.density_pull_weight", "0");
  out.macro_density_pull_range  = number("place.macro.density_pull_range", "4");
  out.scale_macro_density       = truthy(synth_set(opts, "place.scale_macro_density", "true"));
  out.automatic_bins            = truthy(synth_set(opts, "place.auto_bins", "false"));
  out.exact_macro_density       = truthy(synth_set(opts, "place.exact_macro_density", "true"));
  out.adaptive_density          = truthy(synth_set(opts, "place.adaptive_density", "true"));
  const auto unknown_const      = synth_set(opts, "place.unknown_const", "error");
  if (unknown_const != "error" && unknown_const != "zero") {
    throw Lhd_error{"usage", "synth.place.unknown_const must be error or zero", ""};
  }
  out.unknown_const_zero     = unknown_const == "zero";
  out.preserve_imported_ties = truthy(synth_set(opts, "place.preserve_imported_ties", "false"));
  out.routability            = truthy(synth_set(opts, "place.routability", "true"));
  out.route_model            = synth_set(opts, "place.route.model", "rsmt");
  out.route_degree           = integer("place.route.degree", "40");
  out.route_accuracy         = integer("place.route.accuracy", "3");
  const auto read_route  = [&](livehd::placement::Stage_route& stage, const std::string& name) {
    const auto prefix             = "place." + name + ".route.";
    stage.model                   = synth_set(opts, prefix + "model", "inherit");
    stage.policy.switch_overflow  = number(prefix + "switch", "0.05");
    stage.policy.target_overflow  = number(prefix + "target", "0.000000001");
    stage.policy.hold             = integer(prefix + "hold", "1");
    stage.policy.probe            = truthy(synth_set(opts, prefix + "probe", "false"));
    stage.policy.probe_iterations = integer(prefix + "probe_iterations", "100");
    const auto model              = stage.model == "inherit" ? out.route_model : stage.model;
    if ((model != "off" && model != "rudy" && model != "rsmt" && model != "auto")
        || stage.policy.switch_overflow < stage.policy.target_overflow || stage.policy.target_overflow < 0 || stage.policy.hold < 1
        || stage.policy.hold > 1000 || stage.policy.probe_iterations < 1 || stage.policy.probe_iterations > 10000) {
      throw Lhd_error{"usage", "invalid synth." + prefix + " controls", "target must be <= switch; model off/rudy/rsmt/auto"};
    }
  };
  if (out.route_model != "off" && out.route_model != "rudy" && out.route_model != "rsmt" && out.route_model != "auto") {
    throw Lhd_error{"usage", "invalid synth.place.route.model", "use off, rudy, rsmt or auto"};
  }
  read_route(out.fp_route, "fp");
  read_route(out.macro_route, "macro");
  read_route(out.final_route, "final");
  bool uses_flute = false;
  for (const auto* stage : {&out.fp_route, &out.macro_route, &out.final_route}) {
    const auto model  = stage->model == "inherit" ? out.route_model : stage->model;
    uses_flute       |= model == "rsmt" || model == "auto" || (model != "off" && stage->policy.probe);
  }
  if (out.route_degree < 2 || out.route_degree > 1000 || out.route_accuracy < 1 || out.route_accuracy > 10
      || (!uses_flute && std::any_of(opts.sets.begin(), opts.sets.end(), [](const auto& kv) {
           return kv.first == "synth.place.route.degree" || kv.first == "synth.place.route.accuracy";
         }))) {
    throw Lhd_error{"usage",
                    "invalid FLUTE route options",
                    "degree 2..1000, accuracy 1..10, and an rsmt/auto/probe stage required"};
  }
  out.routability_charge_compensation = truthy(synth_set(opts, "place.routability_charge_compensation", "true"));
  out.routability_macro_weight        = number("place.routability_macro_weight", "1");
  out.routability_macro_range         = number("place.routability_macro_range", "2");
  if (out.routability_macro_weight < 0 || out.routability_macro_range <= 0 || out.routability_macro_range > 16) {
    throw Lhd_error{"usage", "invalid synth.place macro routing pressure controls", "weight >= 0; range in (0,16]"};
  }
  out.macro_overlap_weight = number("place.macro_overlap", "50");
  out.macro_jitter         = number("place.macro_jitter", "0.01");
  out.io_iterations        = integer("place.io_iterations", "500");
  out.schedule_iterations  = integer("place.schedule_iterations", "500");
  if (out.macro_overlap_weight < 0 || out.macro_jitter < 0 || out.macro_jitter > .1 || out.schedule_iterations < 0
      || out.io_iterations < 0) {
    throw Lhd_error{"usage", "invalid synth.place repulsion controls", ""};
  }
  out.threads           = integer("place.threads", "1");
  const auto parse_halo = [](const std::string& value, const char* name) {
    const auto invalid
        = [&]() { return Lhd_error{"usage", std::string("invalid synth.") + name, "expected nonnegative microns: H or X,Y"}; };
    char*      end = nullptr;
    const auto x   = std::strtod(value.c_str(), &end);
    if (end == value.c_str()) {
      throw invalid();
    }
    auto y = x;
    if (*end == ',') {
      const auto* y_begin = end + 1;
      y                   = std::strtod(y_begin, &end);
      if (end == y_begin) {
        throw invalid();
      }
    }
    if (*end != '\0' || !std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0) {
      throw invalid();
    }
    return std::pair{x, y};
  };
  const auto halo_text = synth_set(opts, "place.halo", "auto");
  if (halo_text != "auto") {
    const auto halo = parse_halo(halo_text, "place.halo");
    out.halo_x      = halo.first;
    out.halo_y      = halo.second;
  }
  const auto cell_halo = synth_set(opts, "place.cell_halo", "");
  if (!cell_halo.empty()) {
    out.cell_halo = parse_halo(cell_halo, "place.cell_halo");
    if ((out.halo_x >= 0 && out.cell_halo->first > out.halo_x) || (out.halo_y >= 0 && out.cell_halo->second > out.halo_y)) {
      throw Lhd_error{"usage", "invalid synth.place.cell_halo", "each axis must not exceed synth.place.halo"};
    }
  }
  const auto sat_region = synth_set(opts, "place.satlegalize.region_size", "");
  if (!sat_region.empty()) {
    const auto dimensions = parse_halo(sat_region, "place.satlegalize.region_size");
    sat.region_width_um   = dimensions.first;
    sat.region_height_um  = dimensions.second;
    if (sat.region_width_um <= 0 || sat.region_height_um <= 0) {
      throw Lhd_error{"usage", "invalid synth.place.satlegalize.region_size", "expected positive width,height in microns"};
    }
  }
  if (sat.route_grid
      && (!sat_region.empty() || !out.routability || out.automatic_bins
          || livehd::placement::resolve_route_policy(out, out.final_route).model != DreamPlace::native::RouteModel::rsmt)) {
    throw Lhd_error{"usage",
                    "invalid synth.place.satlegalize.route_grid",
                    "requires explicit final rsmt, routability, fixed bins and no region_size"};
  }
  if (sat.spill_neighbors && sat_region.empty() && !sat.route_grid) {
    throw Lhd_error{"usage", "synth.place.satlegalize.spill_neighbors requires region_size or route_grid", "select fixed regions"};
  }
  out.final_cells           = truthy(synth_set(opts, "place.final", "true"));
  out.final_iterations      = integer("place.final.iterations", "0");
  out.fp_place              = truthy(synth_set(opts, "place.fp", "false"));
  out.fp_snap_enabled       = truthy(synth_set(opts, "place.fp.snap", "true"));
  out.fp_colors             = truthy(synth_set(opts, "place.fp.colors", "false"));
  out.fp_color_source       = synth_set(opts, "place.fp.color_source", "synthesis");
  out.fp_color_minimum      = number("place.fp.color_minimum", "-1");
  out.fp_color_maximum      = number("place.fp.color_maximum", "30000");
  out.fp_pin_adjust         = truthy(synth_set(opts, "place.fp.pin_adjust", "true"));
  out.fp_pin_interval       = integer("place.fp.pin_interval", "50");
  out.fp_pin_step           = number("place.fp.pin_step", "0.01");
  out.fp_pin_improvement    = number("place.fp.pin_improvement", "0.001");
  out.macro_pin_adjust      = truthy(synth_set(opts, "place.macro.pin_adjust", "true"));
  out.macro_pin_interval    = integer("place.macro.pin_interval", "50");
  out.macro_pin_step        = number("place.macro.pin_step", "0.01");
  out.macro_pin_improvement = number("place.macro.pin_improvement", "0.001");
  if (out.macro_pin_interval < 1 || out.macro_pin_step <= 0 || out.macro_pin_step > .5 || out.macro_pin_improvement < 0
      || out.macro_pin_improvement >= 1) {
    throw Lhd_error{"usage",
                    "invalid macro pin adjustment settings",
                    "positive interval, step in (0, 0.5], gain in [0, 1) required"};
  }
  if ((out.fp_color_source != "synthesis" && out.fp_color_source != "placement" && out.fp_color_source != "memory_first")
      || (out.fp_color_minimum < 0 && out.fp_color_minimum != -1) || out.fp_color_maximum <= 0 || out.fp_pin_interval < 1
      || out.fp_pin_step <= 0 || out.fp_pin_step > .5 || out.fp_pin_improvement < 0 || out.fp_pin_improvement >= 1) {
    throw Lhd_error{"usage", "invalid FP color/pin adjustment settings", ""};
  }
  out.repair_reset  = truthy(synth_set(opts, "place.repair.reset", "false"));
  out.resize_engine = synth_set(opts, "place.resize.engine", "legacy");
  if (out.resize_engine != "new" && out.resize_engine != "legacy") {
    throw Lhd_error{"usage", "invalid synth.place.resize.engine: " + out.resize_engine, "expected new or legacy"};
  }
  out.resize_vt_swap = truthy(synth_set(opts, "place.resize.vt_swap", "false"));
  for (const auto& pattern : absl::StrSplit(synth_set(opts, "place.resize.dont_use", ""), ',', absl::SkipWhitespace())) {
    out.resize_dont_use.emplace_back(pattern);
  }
  out.reset_fanout  = static_cast<unsigned>(integer("place.reset_fanout", "0"));
  out.control_ports = synth_set(opts, "place.control_ports", "");
  if (out.reset_fanout != 0 || out.repair_reset) {
    throw Lhd_error{"usage",
                    "pre-CTS clock/reset optimization is disabled",
                    "use place.reset_fanout=0 and place.repair.reset=false"};
  }
  out.fp_iterations   = integer("place.fp.iterations", "1500");
  out.fp_minimum      = number("place.fp.minimum", "3000");
  out.fp_min_copies   = integer("place.fp.min_copies", "4");
  out.fp_min_macros   = integer("place.fp.min_macros", "2");
  out.fp_attraction   = number("place.fp.attraction", "0.25");
  out.fp_repulsion    = number("place.fp.repulsion", "5");
  out.fp_align_weight = number("place.fp.align_weight", "0");
  out.fp_align_sigma  = number("place.fp.align_sigma", "0.25");
  out.fp_align_start  = integer("place.fp.align_start", "100");
  out.fp_align_full   = integer("place.fp.align_full", "500");
  if (out.fp_align_weight < 0 || out.fp_align_sigma < 0
      || (out.fp_align_weight > 0 && (out.fp_align_start < 0 || out.fp_align_start >= out.fp_align_full))) {
    throw Lhd_error{"usage", "invalid synth.place.fp alignment controls", "nonnegative weight/sigma and start < full required"};
  }
  out.fp_parent_weight            = number("place.fp.parent_weight", "0");
  out.fp_parent_outside           = number("place.fp.parent_outside", "4");
  out.fp_anchor_macros_only       = truthy(synth_set(opts, "place.fp.anchor_macros_only", "false"));
  out.fp_anchor                   = number("place.fp.anchor", "0.25");
  out.fp_distance_attraction      = truthy(synth_set(opts, "place.fp.distance_attraction", "true"));
  out.fp_attraction_outside       = number("place.fp.attraction_outside", "4");
  out.fp_snap.alignment_tolerance = number("place.fp.alignment", "1");
  out.fp_snap.spacing_tolerance   = number("place.fp.spacing_tolerance", "0.25");
  out.fp_snap.spacing_penalty     = number("place.fp.spacing_penalty", "0.02");
  out.fp_snap.symmetry_tolerance  = number("place.fp.symmetry", "1");
  out.fp_snap.max_displacement    = number("place.fp.max_displacement", "1");
  if (out.fp_min_macros < 0) {
    throw Lhd_error{"usage", "synth.place.fp.min_macros must be at least 0", "a candidate needs more than this many movable macros"};
  }
  if (out.fp_min_copies < 2) {
    throw Lhd_error{"usage", "synth.place.fp.min_copies must be at least 2", "copies are counted separately per hierarchy parent"};
  }
  if (out.fp_minimum < 0 || out.fp_attraction <= 0 || out.fp_repulsion <= 0 || out.fp_anchor < 0 || out.fp_attraction_outside < 0
      || out.fp_parent_weight < 0 || out.fp_parent_outside < 0 || out.fp_snap.alignment_tolerance < 0
      || out.fp_snap.spacing_tolerance < 0 || out.fp_snap.spacing_penalty < 0 || out.fp_snap.symmetry_tolerance < 0
      || out.fp_snap.max_displacement < 0) {
    throw Lhd_error{"usage",
                    "invalid synth.place.fp controls",
                    "expected nonnegative values; attraction/repulsion must be positive"};
  }
  out.macro_mode = synth_set(opts, "place.macro", "none");
  if (out.fp_place && out.macro_mode == "h2d") {
    throw Lhd_error{"usage", "synth.place.fp requires the native plain placer", "use synth.place.macro=none"};
  }
  if (out.macro_mode != "none" && out.macro_mode != "h2d") {
    throw Lhd_error{"usage", "synth.place.macro must be none or h2d", ""};
  }
  out.macro_align_weight                     = number("place.macro.align_weight", "25");
  out.macro_align_sigma                      = number("place.macro.align_sigma", "0.25");
  out.macro_align_overlap_boost              = number("place.macro.align_overlap_boost", "0");
  out.macro_align_start                      = integer("place.macro.align_start", "100");
  out.macro_align_full                       = integer("place.macro.align_full", "500");
  out.macro_align_nearest                    = truthy(synth_set(opts, "place.macro.align_nearest", "false"));
  out.macro_align_global                     = truthy(synth_set(opts, "place.macro.align_global", "true"));
  out.macro_snap                             = truthy(synth_set(opts, "place.macro.snap", out.full ? "true" : "false"));
  out.macro_snap_mode                        = synth_set(opts, "place.macro.snap_mode", "alignment");
  out.macro_snap_options.alignment_tolerance = number("place.macro.snap_alignment", "1");
  out.macro_snap_options.spacing_tolerance   = number("place.macro.snap_spacing_tolerance", "0.25");
  out.macro_snap_options.spacing_penalty     = number("place.macro.snap_spacing_penalty", "0.02");
  out.macro_snap_options.symmetry_tolerance  = number("place.macro.snap_symmetry", "1");
  out.macro_snap_options.max_displacement    = number("place.macro.snap_displacement", "1");
  if ((out.macro_snap_mode != "alignment" && out.macro_snap_mode != "symmetry") || out.macro_snap_options.alignment_tolerance < 0
      || out.macro_snap_options.spacing_tolerance < 0 || out.macro_snap_options.spacing_penalty < 0
      || out.macro_snap_options.symmetry_tolerance < 0 || out.macro_snap_options.max_displacement < 0) {
    throw Lhd_error{"usage",
                    "invalid macro snap mode or regularity controls",
                    "expected alignment or symmetry and nonnegative controls"};
  }
  out.macro_snap_tolerance = number("place.macro.snap_tolerance", "0.25");
  out.macro_snap_weight    = integer("place.macro.snap_weight", "100");
  if (out.macro_align_weight < 0 || out.macro_align_sigma < 0 || out.macro_align_overlap_boost < 0
      || (out.macro_align_weight > 0 && out.macro_align_start >= out.macro_align_full) || out.macro_snap_tolerance < 0
      || out.macro_snap_weight == 0) {
    throw Lhd_error{"usage", "invalid synth.place.macro alignment/snap controls", "alignment requires start < full"};
  }
  if ((out.macro_align_weight > 0 || out.macro_snap || out.macro_pin_adjust) && out.macro_mode == "h2d") {
    throw Lhd_error{"usage", "macro alignment/snap/pin adjustment requires synth.place.macro=none", ""};
  }
  if (out.macro_snap && !out.full) {
    throw Lhd_error{"usage", "macro snapping requires full placement", "use synth.place.mode=full"};
  }
  if (out.macro_mode == "h2d" && !out.full) {
    throw Lhd_error{"usage", "H2D requires synth.place.mode=full", "macro realization includes legal cell placement"};
  }
  namespace mp = DreamPlace::native::macro;
  try {
    for (const auto& spec : mp::parameter_specs()) {
      const std::string key   = "place.macro." + std::string(spec.name);
      auto              value = synth_set(opts, key, spec.default_value);
      if (spec.kind == mp::ParameterKind::boolean) {
        if (value != "true" && value != "false" && value != "1" && value != "0" && value != "on" && value != "off") {
          throw std::invalid_argument("expected boolean for " + key);
        }
        value = truthy(value) ? "true" : "false";
      }
      mp::set_parameter(out.macro_parameters, spec.name, value);
    }
    // Explicit global placement grid/worker settings are inherited unless a
    // corresponding H2D-specific setting was supplied.
    if (!std::any_of(opts.sets.begin(), opts.sets.end(), [](const auto& s) { return s.first == "synth.place.macro.bins"; })) {
      out.macro_parameters.bins = out.bins;
    }
    if (!std::any_of(opts.sets.begin(), opts.sets.end(), [](const auto& s) { return s.first == "synth.place.macro.threads"; })) {
      out.macro_parameters.threads = out.threads;
    }
    out.macro_parameters.validate();
  } catch (const std::invalid_argument& error) {
    throw Lhd_error{"usage", "invalid synth.place.macro parameter", error.what()};
  }
  out.tracks_file = synth_set(opts, "place.tracks", "");
  if (!out.tracks_file.empty()) {
    check_inputs_exist({out.tracks_file});
  }
  out.tech_rc_file = synth_set(opts, "place.tech_rc", "");
  if (!out.tech_rc_file.empty()) {
    check_inputs_exist({out.tech_rc_file});
  }
  const auto rc = synth_set(opts, "place.wire_rc", "");
  if (!rc.empty()) {
    char* end             = nullptr;
    out.resistance_ohm_um = std::strtod(rc.c_str(), &end);
    if (end == rc.c_str() || *end != ',') {
      throw Lhd_error{"usage", "invalid synth.place.wire_rc", "expected ohm/um,fF/um"};
    }
    const auto* cap       = end + 1;
    out.capacitance_ff_um = std::strtod(cap, &end);
    if (end == cap || *end != '\0' || !std::isfinite(out.resistance_ohm_um) || !std::isfinite(out.capacitance_ff_um)
        || out.resistance_ohm_um < 0 || out.capacitance_ff_um < 0) {
      throw Lhd_error{"usage", "invalid synth.place.wire_rc", "expected nonnegative ohm/um,fF/um"};
    }
  }

  return out;
}

// The timing target the run was asked to meet against what STA measured, so a
// reader (and the delay-driven decisions that feed on it) can tell "target met",
// "slightly missed" and "out of reach at this partition" apart. The target is the
// mapper's explicit `abc.delay` budget (ps) when set, else the first
// `create_clock -period` of the SDC (Liberty time units). Achieved is STA's
// worst endpoint delay of the top design. Empty when there is nothing to compare.
std::string timing_target_json(const std::string& abc_qor, const std::string& sta_qor, const std::string& sdc_path) {
  if (sta_qor.empty()) {
    return {};
  }
  rapidjson::Document sta;
  sta.Parse(sta_qor.c_str());
  if (sta.HasParseError() || !sta.IsObject() || !sta.HasMember("designs") || !sta["designs"].IsArray() || sta["designs"].Empty()
      || !sta.HasMember("time_unit") || !sta["time_unit"].IsString()) {
    return {};
  }
  const std::string unit  = sta["time_unit"].GetString();
  const double      to_ps = unit == "ns" ? 1e3 : unit == "us" ? 1e6 : unit == "ps" ? 1.0 : unit == "fs" ? 1e-3 : 0.0;
  const auto&       top   = sta["designs"][0];
  if (to_ps == 0.0 || !top.IsObject() || !top.HasMember("max_delay") || !top["max_delay"].IsNumber()) {
    return {};
  }
  const double achieved = top["max_delay"].GetDouble() * to_ps;
  double       target   = 0.0;
  std::string  source   = "none";
  if (!abc_qor.empty()) {
    rapidjson::Document abc;
    abc.Parse(abc_qor.c_str());
    if (!abc.HasParseError() && abc.IsObject() && abc.HasMember("delay_target") && abc["delay_target"].IsString()) {
      const std::string t   = abc["delay_target"].GetString();
      char*             end = nullptr;
      const double      v   = std::strtod(t.c_str(), &end);
      if (!t.empty() && end != t.c_str() && v > 0) {
        target = v;
        source = "abc.delay";
      }
    }
  }
  if (source == "none" && !sdc_path.empty()) {
    std::ifstream     in(sdc_path);
    const std::string text{std::istreambuf_iterator<char>(in), {}};
    if (const auto at = text.find("-period"); at != std::string::npos) {
      char*        end = nullptr;
      const char*  b   = text.c_str() + at + 7;
      const double v   = std::strtod(b, &end);
      if (end != b && v > 0) {
        target = v * to_ps;
        source = "sdc";
      }
    }
  }
  if (source == "none") {
    return std::format(R"({{"source":"none","achieved_ps":{:.1f}}})", achieved);
  }
  const double slack       = target - achieved;
  const bool   met         = slack >= 0;
  // Beyond 25 % over, the timing objective is degenerate: every adder/shifter
  // choice lands on the same path, so trials rank on noise.
  const bool   unreachable = achieved > target * 1.25;
  if (unreachable) {
    livehd::diag::info("synth", "synth-target-unreachable", "unsupported")
        .msg(
            "timing target {:.0f} ps ({}) is out of reach: achieved {:.0f} ps (+{:.0f} %); delay-driven choices "
            "are degenerate here -- relax the target or compare designs by delay instead",
            target,
            source,
            achieved,
            (achieved / target - 1.0) * 100.0)
        .emit();
  }
  return std::format(R"({{"source":"{}","target_ps":{:.1f},"achieved_ps":{:.1f},"slack_ps":{:.1f},"met":{},"unreachable":{}}})",
                     source,
                     target,
                     achieved,
                     slack,
                     met ? "true" : "false",
                     unreachable ? "true" : "false");
}

// One JSON value from a sidecar file ("" when absent/empty), trailing
// whitespace stripped so it can ride RawValue into the envelope.
std::string slurp_json(const std::string& path) {
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs.is_open()) {
    return {};
  }
  std::string j((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  while (!j.empty() && (j.back() == '\n' || j.back() == '\r' || j.back() == ' ' || j.back() == '\t')) {
    j.pop_back();
  }
  return j;
}

std::string canon(const std::string& p) {
  std::error_code ec;
  const auto      c = fs::weakly_canonical(p, ec);
  return ec ? p : c.lexically_normal().string();
}

// True when `a` and `b` are the same directory or one lies inside the other.
bool paths_nest(const std::string& a, const std::string& b) {
  const auto ca     = fs::path(canon(a));
  const auto cb     = fs::path(canon(b));
  const auto inside = [](const fs::path& outer, const fs::path& inner) {
    return std::mismatch(outer.begin(), outer.end(), inner.begin(), inner.end()).first == outer.end();
  };
  return inside(ca, cb) || inside(cb, ca);
}

// Drop what an earlier `lhd synth` left under <root> so a reused --workdir
// runs exactly like a fresh one: the compiled library, the default netlist dir
// <root>/net (a run whose --emit-dir lg: relocates the netlist would otherwise
// leave an earlier run's netlist of an older design there), and every sidecar
// a mapper or STA run writes next to qor.json (an earlier run's timing.json or
// mapper report must never be declared, embedded or exported as this run's).
// The netlist dir is purged again right before the mapper fills it, wherever
// --emit-dir lg: puts it. An lg: input that lives under <root>/net (re-mapping
// an earlier run's netlist) is read by the compile first, so that dir is left
// alone here. The incremental caches live outside <root>.
void purge_synth_products(const std::string& root, const std::string& lg_dir, const std::vector<std::string>& lg_inputs) {
  std::error_code ec;
  fs::remove_all(lg_dir, ec);
  if (ec) {
    throw Lhd_error{"config", std::format("could not clear {}: {}", lg_dir, ec.message()), "check --workdir permissions"};
  }
  const std::string        qor = root + "/qor.json";
  std::vector<std::string> stale{qor, qor + ".provenance", root + "/timing.json"};
  const std::string        net = root + "/net";
  if (std::none_of(lg_inputs.begin(), lg_inputs.end(), [&](const std::string& in) { return paths_nest(in, net); })) {
    stale.push_back(net);
  }
  for (const auto& m : kMappers) {
    if (!m.report.empty()) {
      stale.push_back(std::format("{}.{}.json", qor, m.report));
      stale.push_back(std::format("{}.{}.artifacts", qor, m.report));
    }
  }
  for (const auto& p : stale) {
    fs::remove_all(p, ec);
    if (ec) {
      throw Lhd_error{"config", std::format("could not clear {}: {}", p, ec.message()), "check --workdir permissions"};
    }
  }
}

}  // namespace

// `--set synth.<flag>` value, else `def` (kSynthSetOptions validated the name).
std::string synth_set(const Options& opts, std::string_view flag, std::string_view def) {
  std::string v{def};
  const auto  key = std::format("synth.{}", flag);
  for (const auto& [k, val] : opts.sets) {
    if (k == key) {
      v = val;
    }
  }
  return v;
}

// Resolve THE Liberty: synth.liberty, else the sky130 default under
// $LHD_TECH_DIR. `synth.liberty` is the ONE spelling every Liberty reader
// shares -- `lhd synth` (pass.abc + pass.opentimer) and the standalone
// `lhd pass abc` / `lhd pass opentimer` all resolve through here, so no two
// of them can ever fall back to a different file. A missing library is a
// directed missing_file error.
std::string resolve_liberty(const Options& opts) {
  std::string lib = synth_set(opts, "liberty", "");
  if (lib.empty()) {
    if (const char* tech = std::getenv("LHD_TECH_DIR"); tech != nullptr && *tech != '\0') {
      lib = (fs::path(tech) / std::string{kSynthDefaultLiberty}).string();
    }
  }
  if (lib.empty()) {
    throw Lhd_error{"missing_file",
                    "no Liberty cell library: neither --set synth.liberty nor $LHD_TECH_DIR is set",
                    "pass --set synth.liberty=cells.lib, or point LHD_TECH_DIR at a sky130 PDK (install one with `ciel`)"};
  }
  if (!fs::is_regular_file(lib)) {
    throw Lhd_error{"missing_file",
                    std::format("Liberty cell library not found: {}", lib),
                    "pass --set synth.liberty=cells.lib, or point LHD_TECH_DIR at a sky130 PDK (install one with `ciel`)"};
  }
  return lib;
}

void synth_command(Options& opts, Result& res) {
  setup_diag(opts, "synth");

  auto       ir          = gather_ir_inputs(opts, "synth");
  const bool has_sources = !opts.files.empty() || (opts.language == "verilog" && !opts.raw_args.empty());
  if (!has_sources && ir.lg_dirs.empty() && ir.ln_dirs.empty()) {
    throw Lhd_error{"usage",
                    "synth requires source files (.prp/.v/.sv) or ln:/lg: inputs",
                    "e.g. `lhd synth cpu.prp --top Cpu --workdir W` or `lhd synth lg:cpu_lg --top Cpu --emit-dir lg:net`"};
  }

  // ---- the synth.* knobs --------------------------------------------------
  // (`pass.abc.library` is refused for every command by check_known_set_passes:
  // synth.liberty is the one spelling, so no two Liberty readers can disagree.)
  const auto*       mapper        = find_mapper(synth_set(opts, "mapper", "abc"));  // validated by check_known_set_passes
  const auto        mapper_method = std::string{mapper->method};
  const bool        mapped_output = mapper_maps_cells(opts, mapper_method);
  const std::string liberty       = mapped_output ? resolve_liberty(opts) : std::string{};
  const bool        run_sta       = mapped_output && truthy(synth_set(opts, "opentimer", "true"));
  const bool        run_place     = truthy(synth_set(opts, "place", "false"));
  auto              place_options = run_place ? placement_options(opts) : livehd::placement::Options{};
  const bool        run_resynth   = truthy(synth_set(opts, "place.resynth", "false"));
  const bool run_resize = truthy(synth_set(opts, "place.resize", run_place && run_sta && place_options.full ? "true" : "false"));
  // Timing weights are the normal full-placement objective. Synthesis-only
  // and GP-only commands do not enter the full-placement refinement loop.
  const bool run_timing_place = truthy(synth_set(opts, "place.timing_driven", run_place && place_options.full ? "true" : "false"));
  const int  timing_rounds    = std::stoi(synth_set(opts, "place.timing_rounds", "3"));
  if (timing_rounds < 1 || timing_rounds > 8) {
    throw Lhd_error{"usage", "synth.place.timing_rounds expects 1..8", ""};
  }
  if ((run_resize || run_timing_place || run_resynth) && (!run_place || !run_sta || !place_options.full)) {
    throw Lhd_error{"usage", "physical optimization requires placement in full mode and OpenTimer", ""};
  }
  if (run_resynth && !livehd::synth::has_cone_provider("abc")) {
    throw Lhd_error{"unsupported", "cone resynthesis requires the ABC provider", ""};
  }
  if (run_place && !mapped_output) {
    throw Lhd_error{"usage", "placement requires technology-mapped output", "use pass.usyn.tmap=abc"};
  }
  if (find_slot(opts.emit_dirs, "def") && (!run_place || !place_options.full)) {
    throw Lhd_error{"usage", "def: output requires synth.place=true and synth.place.mode=full", ""};
  }
  if (!mapped_output && truthy(synth_set(opts, "opentimer", ""))) {
    throw Lhd_error{"usage", "logical-only USYN cannot run OpenTimer", "use tmap=abc or synth.opentimer=false"};
  }
  const bool        run_reduce = truthy(synth_set(opts, "reduce", "false"));
  const std::string sdc        = synth_set(opts, "sdc", "");
  const std::string spef       = synth_set(opts, "spef", "");
  if (run_place && !spef.empty()) {
    throw Lhd_error{"usage", "synth.spef cannot be combined with generated placement RC", ""};
  }
  {
    std::vector<std::string> extra;
    if (!sdc.empty()) {
      extra.push_back(sdc);
    }
    if (!spef.empty()) {
      extra.push_back(spef);
    }
    check_inputs_exist(extra);
    if (!liberty.empty()) {
      res.inputs.push_back(liberty);
    }
    for (const auto& f : extra) {
      res.inputs.push_back(f);
    }
  }

  // ---- the workdir layout ---------------------------------------------------
  // <workdir>/synth/{lg,net,qor.json,timing.json}; workdir() mints a scratch dir
  // when the user named none (nothing under it is then a declared artifact).
  const bool user_workdir = !opts.workdir.empty();
  const bool explicit_io_resynth
      = std::any_of(opts.sets.begin(), opts.sets.end(), [](const auto& value) { return value.first == "synth.place.io_resynth"; });
  double io_resynth_percent = 0;
  try {
    const auto text    = synth_set(opts, "place.io_resynth", "5");
    size_t     end     = 0;
    io_resynth_percent = std::stod(text, &end);
    if (end != text.size() || !std::isfinite(io_resynth_percent) || io_resynth_percent < 0 || io_resynth_percent > 100) {
      throw std::invalid_argument("percent");
    }
  } catch (const std::exception&) {
    throw Lhd_error{"usage", "synth.place.io_resynth expects a percent in [0,100]", "0 disables whole-color feedback"};
  }
  if (explicit_io_resynth && io_resynth_percent > 0 && !user_workdir) {
    throw Lhd_error{"usage", "synth.place.io_resynth needs --workdir", "use a separate workdir for each placement configuration"};
  }
  if (explicit_io_resynth && io_resynth_percent > 0
      && (!run_place || !place_options.full || !run_sta || mapper_method != "pass.abc")) {
    throw Lhd_error{"usage", "synth.place.io_resynth requires ABC full placement and OpenTimer", "use 0 to disable"};
  }
  std::shared_ptr<livehd::synth::Io_store> io_store;
  if (io_resynth_percent > 0 && user_workdir && run_place && place_options.full && run_sta && mapper_method == "pass.abc") {
    const auto period = sdc.empty() ? std::nullopt : sdc_min_clock_period_ps(sdc, liberty);
    if (!period) {
      throw Lhd_error{"usage", "synth.place.io_resynth needs a resolvable SDC clock period", "set synth.sdc or disable io_resynth"};
    }
    io_store = std::make_shared<livehd::synth::Io_store>(opts.workdir + "/abc_cache",
                                                         *period,
                                                         opts.incremental,
                                                         std::format("{:016x}", livehd::opentimer::kStaSrcSalt));
  }
  const std::string root = workdir(opts) + "/synth";
  ensure_dir(root);
  const std::string lg_dir      = root + "/lg";
  const auto*       lg_emit     = find_slot(opts.emit_dirs, "lg");
  const auto*       report_emit = find_slot(opts.emit_dirs, "report");
  // --emit-dir lg: RELOCATES the mapped netlist (one copy, not two).
  const std::string net_dir     = lg_emit != nullptr ? lg_emit->path : root + "/net";
  if (canon(net_dir) == canon(lg_dir)) {
    throw Lhd_error{"usage",
                    std::format("--emit-dir lg:{} is the flow's own compiled-design directory", net_dir),
                    "the mapped netlist goes to <workdir>/synth/net by default; name another directory"};
  }
  for (const auto& in : ir.lg_dirs) {
    if (canon(in) == canon(lg_dir)) {
      throw Lhd_error{"usage",
                      std::format("lg:{} is the flow's own compiled-design directory under --workdir", in),
                      "synth never rewrites an lg: input; point it at the design library, or at a different --workdir"};
    }
  }

  // ---- 1. the design ----------------------------------------------------
  // `lhd compile` owns <root>/lg exactly as it owns any `--emit-dir lg:`:
  // sources, ln:, lg: and mixed linking all go through compile_command, and so
  // does the compile cache (a warm run restores the library generation into
  // the dir). The user's own emits are held back: they describe the NETLIST.
  //
  // <root>/lg is rebuilt from scratch every run. A compile MERGES into an
  // existing lg: library (a shared emit dir keeps other compiles' modules), so
  // a previous run's library would leak into this one. tolg now re-declares a
  // module's GraphIO exactly as the source does, but what is left still
  // matters:
  //   - a module the source no longer defines stays in the library and in the
  //     design this flow loads below: a Verilog compile never prunes one, and
  //     a Pyrope compile prunes only the leftovers of its own cache scope;
  //   - such a leftover still instantiating a module whose ports this run
  //     changes makes tolg refuse the compile (`stale-instance`);
  //   - the yosys reader still only ADDS ports, so renaming a top input
  //     `a` -> `a2` keeps both declared on one port id (pass.partition then
  //     refuses the library with `io-port-clash`);
  //   - the stale sidecars and <root>/net go with it (purge_synth_products).
  // Purging before the compile leaves the incremental
  // tiers intact: the compile cache restores its generation into the empty
  // dir (materialize_artifact_files clones each file on macOS and copies it
  // elsewhere, so <root>/lg never shares storage with the cache and removing
  // it never touches the cache), and the region caches live outside <root>.
  //
  // Cost: a warm all-clean run re-materializes the generation every time (the
  // total-restore fast path skips that only while <root>/lg still matches it).
  // On a 302-graph design (macOS clonefile) that is ~10-25 ms more in
  // compile.cache.lg_artifact of a ~240 ms warm synth; elsewhere it is a full
  // data copy. Purging only off the fast path needs compile_sources' own
  // decision, which is not known here before the sources are parsed.
  purge_synth_products(root, lg_dir, ir.lg_dirs);
  const std::vector<Typed_path> user_emits     = opts.emits;
  const std::vector<Typed_path> user_emit_dirs = opts.emit_dirs;
  opts.emits.clear();
  for (const auto& emit : user_emits) {
    if (emit.kind == "diagnostics") {
      opts.emits.push_back(emit);
    }
  }
  opts.emit_dirs = {
      Typed_path{"lg", lg_dir}
  };
  try {
    compile_command(opts, res);
  } catch (...) {
    // Frontend failures still owe the caller its requested diagnostics emit.
    // The temporary compile outputs must not hide that slot during unwinding.
    opts.emits     = user_emits;
    opts.emit_dirs = user_emit_dirs;
    throw;
  }
  opts.emits     = user_emits;
  opts.emit_dirs = user_emit_dirs;
  if (res.status != "pass") {
    return;
  }
  livehd::diag::sink().set_step("synth");

  Eprp_var var;
  load_lg_into_var(lg_dir, var);
  if (var.graphs.empty()) {
    throw Lhd_error{"config",
                    "the design compiled to no LGraphs -- nothing to synthesize",
                    "a type/constant-only unit has no module"};
  }
  if (user_workdir && std::find(res.outputs.begin(), res.outputs.end(), lg_dir) == res.outputs.end()) {
    // compile_command already declared it (the flow hands it `--emit-dir
    // lg:<root>/lg`), so an unconditional push listed the same directory twice
    // in the report and in the envelope's `outputs`.
    res.outputs.push_back(lg_dir);
  }

  // --top, resolved ONCE: a bare entity resolves to the unique `file.entity`
  // (with the standard fallback warning), a sole module needs no --top at all.
  // Every pass below receives the full name and matches it silently.
  auto              top_g = pick_top_graph(var, "", opts.top, "", "synth", "synth");
  const std::string top{top_g->get_name()};
  opts.top = top;

  std::unique_ptr<Physical_cache> physical_cache;
  if (user_workdir && opts.incremental && run_place && place_options.full && io_store) {
    auto        dependencies = place_options.lefs;
    std::string libraries    = liberty;
    std::replace(libraries.begin(), libraries.end(), ',', ' ');
    std::istringstream paths(libraries);
    for (std::string path; paths >> path;) {
      dependencies.push_back(path);
    }
    for (const auto& path : {sdc, place_options.tech_rc_file, place_options.tracks_file, place_options.floorplan}) {
      if (!path.empty()) {
        dependencies.push_back(path);
      }
    }
    physical_cache = std::make_unique<Physical_cache>(fs::path(opts.workdir) / "physical_cache",
                                                      lg_dir,
                                                      physical_descriptor(opts),
                                                      std::move(dependencies));
  }

  // ---- 2. repeated-cone reduction + coloring ---------------------------------
  // Always `synth`: the per-(def, color) regions are what keep a large design
  // inside pass.abc's memory admission and what its incremental reuse is keyed
  // on. The colors live on the in-memory graphs only — <root>/lg is NOT
  // rewritten: the coloring is seeded and deterministic, pass.abc digests
  // region CONTENT, and <root>/lg stays exactly the compile's output.
  if (run_reduce) {
    Eprp_var::Eprp_dict labels;
    labels["seed"]      = opts.seed;
    labels["top"]       = top;
    labels["min_nodes"] = "1";
    labels["max_nodes"] = "2";
    labels["min_count"] = "3";
    labels["min_win"]   = "1";
    merge_color_sets(opts, labels);
    labels["alg"] = "reduce";  // forced AFTER merge: a user --set color.alg never re-targets this step
    if (opts.stats) {
      labels["stats"] = "true";
    }
    run_step("pass.color", var, labels, opts, res);

    // reduce creates content-addressed pat_* definitions in the same in-memory
    // GraphLibrary. Eprp_var is a snapshot from before that rewrite, so append
    // the new definitions before synth coloring. A separate `lhd pass color`
    // command naturally reloads them from disk; the fused command must expose
    // the identical graph set without saving/reloading the compiled cache.
    absl::flat_hash_set<hhds::Gid> loaded;
    loaded.reserve(var.graphs.size());
    for (const auto& g : var.graphs) {
      if (g) {
        loaded.insert(g->get_gid());
      }
    }
    auto& lib = livehd::Hhds_graph_library::instance(lg_dir);
    for (const auto gid : lib.all_gids()) {
      if (!loaded.contains(gid)) {
        if (auto g = lib.get_graph(gid)) {
          var.add(g);
        }
      }
    }
  }
  {
    Eprp_var::Eprp_dict labels;
    labels["alg"]        = "synth";
    labels["seed"]       = opts.seed;
    labels["top"]        = top;
    // The coloring profile follows the mapper: ABC wants the stop_* cuts (small
    // regions), the unate/domino mapper wants register-to-register colors.
    // Merged BEFORE the user's sets so an explicit pass.color.synth.* wins.
    // synth.partition decouples the region granularity from the mapper: the same
    // coloring profile can be requested for abc and usyn, so a QoR difference is
    // attributable to the mapper and not to a different partition.
    const auto partition = synth_set(opts, "partition", "coarse");
    labels["mapper"]     = partition == "coarse" ? std::string{"usyn"}
                           : partition == "fine" ? std::string{"abc"}
                                                 : std::string{mapper->color_profile};
    merge_color_sets(opts, labels);
    labels["liberty"] = liberty;
    if (labels["alg"] != "synth") {
      throw Lhd_error{"usage",
                      std::format("synth always colors with `synth` (got --set color.alg={})", labels["alg"]),
                      "another coloring is the manual flow: `lhd pass color <alg>` then `lhd pass abc`"};
    }
    if (opts.stats) {
      labels["stats"] = "true";
    }
    run_step("pass.color", var, labels, opts, res);
  }

  {
    std::map<hhds::Gid, std::shared_ptr<hhds::Graph>> definitions;
    for (const auto& g : var.graphs) {
      if (g) {
        definitions[g->get_gid()] = g;
      }
    }
    std::map<hhds::Gid, uint64_t>        occurrences;
    std::vector<hhds::Gid>               order;
    std::set<hhds::Gid>                  visited;
    const std::function<void(hhds::Gid)> visit = [&](hhds::Gid id) {
      if (!visited.insert(id).second) {
        return;
      }
      auto it = definitions.find(id);
      if (it == definitions.end()) {
        return;
      }
      for (auto n : it->second->body().nodes()) {
        if (livehd::graph_util::type_op_of(n) == Ntype_op::Sub) {
          if (auto io = n.get_subnode_io()) {
            visit(io->get_gid());
          }
        }
      }
      order.push_back(id);
    };
    for (const auto& [id, g] : definitions) {
      if (g->get_name() == top) {
        occurrences[id] = 1;
        visit(id);
      }
    }
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
      for (auto n : definitions.at(*it)->body().nodes()) {
        if (livehd::graph_util::type_op_of(n) == Ntype_op::Sub) {
          if (auto io = n.get_subnode_io()) {
            occurrences[io->get_gid()] += occurrences[*it];
          }
        }
      }
    }
    std::string groups = "{\"schema_version\":1,\"top\":" + livehd::synth_attr::quote(top) + ",\"definitions\":[";
    bool        first  = true;
    for (const auto& graph : var.graphs) {
      if (!first) {
        groups += ",";
      }
      first      = false;
      groups    += "{\"module\":" + livehd::synth_attr::quote(graph->get_name())
                   + ",\"physical_instances\":" + std::to_string(occurrences[graph->get_gid()]) + ",\"specialized\":"
                   + (graph->get_name().find("__s_") != std::string_view::npos ? "true" : "false") + ",\"coloring\":";
      auto info  = graph->get_input_node().attr(livehd::attrs::coloring_info);
      groups    += info.has() ? std::string(info.get()) : "{}";
      if (auto policy = graph->get_input_node().attr(livehd::attrs::synth_policy); policy.has()) {
        groups += ",\"definition_policy\":" + std::string(policy.get());
      }
      groups            += ",\"members\":[";
      bool member_first  = true;
      for (auto node : graph->body().nodes()) {
        if (!member_first) {
          groups += ",";
        }
        member_first    = false;
        const auto sid  = node.attr(hhds::attrs::srcid).has() ? node.attr(hhds::attrs::srcid).get() : 0;
        groups         += std::format("{{\"node\":{},\"color\":{},\"source_id\":{}",
                                      node.get_debug_nid(),
                                      livehd::graph_util::node_color_of(node),
                                      sid);
        // The source locator turns the id into the file and line a person can open.
        if (sid != 0) {
          const auto span = graph->source_locator().resolve_spans(static_cast<hhds::SourceId>(sid)).primary;
          if (!span.is_null()) {
            groups
                += ",\"file\":" + livehd::synth_attr::quote(span.file) + ",\"line\":" + std::to_string(span.start_line.value_or(0));
          }
        }
        if (auto a = node.attr(livehd::attrs::synth_policy); a.has()) {
          groups += ",\"source_policy\":" + std::string(a.get());
        }
        groups += "}";
      }
      groups += "]}";
    }
    groups          += "]}";
    const auto path  = root + "/synth_groups.json";
    {
      std::ofstream out(path);
      if (!(out << groups << '\n')) {
        throw Lhd_error{"config", "cannot write synth_groups.json", "check workdir permissions"};
      }
    }
    if (user_workdir) {
      res.outputs.push_back(path);
    }
  }

  // ---- 3. ABC tech-map --------------------------------------------------------
  const std::string qor_path = root + "/qor.json";
  var.synthesis_feedback     = io_store;
  std::map<std::string, std::string> io_mapper_options;
  {
    std::error_code ec;
    fs::remove_all(net_dir, ec);  // a stale netlist must never shadow a region shell the mapper fills
    ensure_dir(net_dir);
    Eprp_var::Eprp_dict labels;
    labels["top"]             = top;
    labels["out"]             = net_dir;
    labels["qor"]             = qor_path;
    // ABC keeps its independent worker setting; USYN uses shared admission.
    labels["threads"]         = "0";
    labels["specialize"]      = synth_set(opts, "specialize", "true");
    labels["state_sweep"]     = synth_set(opts, "state_sweep", "true");
    labels["merge_registers"] = synth_set(opts, "merge_registers", "true");
    // Clock gating needs the Liberty's integrated clock-gate cell: without one the gated registers' clock would be computed
    // logic and the registers would stay native flops (no mapped DFF, no timing). The library decides, the user can still say false.
    labels["clock_gate"] = synth_set(opts, "clock_gate", "true") == "true" && !livehd::liberty::resolve_dff_cells(liberty, "").icg_ladder.empty()
                               ? "true"
                               : "false";
    labels["clock_gate_min_bits"] = synth_set(opts, "clock_gate_min_bits", "16");
    labels["clock_gate_depth"]    = synth_set(opts, "clock_gate_depth", "1");
    // Each mapper receives its own labels. Native USYN does not inherit
    // ABC optimization settings; standalone dispatch uses the same rule.
    merge_mapper_sets(opts, mapper_method, labels);
    labels["library"] = liberty;  // synth.liberty is the one spelling (pass.abc.library is refused)
    if (mapper->timing_files) {
      labels["timing_files"] = liberty + (sdc.empty() ? "" : "," + sdc) + (spef.empty() ? "" : "," + spef);
    }
    if (opts.stats) {
      labels["stats"] = "true";
    }
    // Incremental region reuse: the same <workdir>/abc_cache the standalone
    // `lhd pass abc` uses, under the same gate (a user workdir + lhd.incremental).
    if (user_workdir && opts.incremental) {
      labels["cache_dir"] = (fs::path(opts.workdir) / mapper->cache_dir).string();
    }
    for (const auto& [key, value] : labels) {
      io_mapper_options.emplace(key, value);
    }
    run_step(mapper_method, var, labels, opts, res);
    if (user_workdir || lg_emit != nullptr) {
      Phase_timer phase(res, "lg.save");
      livehd::Hhds_graph_library::save(net_dir);
      res.outputs.push_back(net_dir);
    }
    if (user_workdir && fs::exists(qor_path)) {
      res.outputs.push_back(qor_path);
    }
  }
  const std::string abc_qor         = mapped_output ? slurp_json(qor_path) : std::string{};
  // The mapper's own report (USYN endpoint selection and residual optimization).
  const bool        has_report      = !mapper->report.empty();
  const std::string report_path     = has_report ? std::format("{}.{}.json", qor_path, mapper->report) : std::string{};
  const std::string provenance_path = qor_path + ".provenance";
  const std::string artifact_path   = mapper_method == "pass.usyn" ? qor_path + ".usyn.artifacts" : std::string{};
  const std::string mapper_qor      = has_report ? slurp_json(report_path) : std::string{};
  if (user_workdir && !mapper_qor.empty()) {
    res.outputs.push_back(report_path);
    res.outputs.push_back(provenance_path);
    if (!artifact_path.empty()) {
      res.outputs.push_back(artifact_path);
    }
  }

  // The selected logical or mapped netlist in the output library (in memory).
  Eprp_var net;
  load_lg_into_var(net_dir, net);
  if (net.graphs.empty()) {
    throw Lhd_error{"internal", mapper_method + " produced an empty netlist library", ""};
  }

  // ---- placement: one persistent flat graph for physical outputs and STA ----
  hhds::GraphLibrary                  placed_library;
  std::unique_ptr<hhds::GraphLibrary> restored_library;
  std::unique_ptr<hhds::GraphLibrary> retry_library;
  livehd::placement::Repair_result    repaired;
  livehd::placement::Repair_result    design_repaired;
  livehd::placement::Repair_result    timed_placement;
  livehd::placement::Repair_result    resynthesized;
  std::string                         place_qor;
  const std::string                   place_qor_path = root + "/place/qor.json";
  if (run_place) {
    bool restored = false;
    if (physical_cache) {
      Phase_timer phase(res, "pass.place.restore");
      const auto  restored_net = root + "/repaired_net";
      restored                 = physical_cache->restore(restored_net, root + "/place");
      if (restored) {
        restored_library = std::make_unique<hhds::GraphLibrary>();
        restored_library->load(restored_net);
        const auto io = restored_library->find_io(top);
        if (!io || !io->get_graph()) {
          throw Lhd_error{"internal", "completed placement has no repaired top", restored_net};
        }
        net.graphs = {io->get_graph()};
        restored_library->save(net_dir);
        const auto io_path  = root + "/place/io_resynth.json";
        const auto feedback = restored_io_report(slurp_json(io_path));
        std::ofstream(io_path) << feedback << '\n';
        rapidjson::Document q, io_report;
        q.Parse(slurp_json(place_qor_path).c_str());
        io_report.Parse(feedback.c_str());
        if (q.HasParseError() || !q.IsObject()) {
          throw Lhd_error{"internal", "completed placement has invalid QoR", place_qor_path};
        }
        auto& a = q.GetAllocator();
        if (q.HasMember("incremental") && q["incremental"].IsObject()) {
          auto& incremental = q["incremental"];
          if (incremental.HasMember("hit")) {
            incremental["hit"].SetBool(true);
          }
        }
        q.RemoveMember("io_resynth");
        q.AddMember("io_resynth", rapidjson::Value(io_report, a), a);
        q.AddMember("reused_final_state", true, a);
        rapidjson::StringBuffer                    buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        q.Accept(writer);
        place_qor = buffer.GetString();
        res.recipe_steps.emplace_back("pass.place.restore");
        res.outputs.push_back(io_path);
        res.outputs.push_back(net_dir);
      }
    }
    if (!restored) {
      const auto found
          = std::find_if(net.graphs.begin(), net.graphs.end(), [&](const auto& g) { return g && g->get_name() == top; });
      if (found == net.graphs.end()) {
        throw Lhd_error{"internal", "mapped top missing before placement", ""};
      }
      place_options.liberty    = liberty;
      place_options.output_dir = root + "/place";
      if (user_workdir && opts.incremental) {
        place_options.cache_dir = opts.workdir + "/place_cache";
      }
      Eprp_var timing_options;
      timing_options.dict = {
          {"files", liberty + (sdc.empty() ? "" : "," + sdc)},
          {  "top",                                      top},
          { "hier",                                  "false"}
      };
      merge_sets(opts, "pass.opentimer", timing_options.dict);
      timing_options.dict["cache_dir"] = "";
      timing_options.dict["qor"]       = "";
      timing_options.set_stage_labels(timing_options.dict);
      livehd::opentimer::Physical_session timing_session(timing_options);
      place_options.whatif = &timing_session.whatif();
      std::string phase_step;
      std::string phase_log;
      // The session runs in-process (no run_step), so keep Liberty/timer prints off the protocol stdout here.
      // Nested redirects (calls from inside place()/repair, already logged) restore correctly.
      const auto  timing_log = next_log_path(opts, "pass.opentimer.incremental");
      const auto  timing_tech = livehd::physical::read_lef(place_options.lefs);
      const auto  evaluate   = [&](const livehd::placement::Result& state) {
        Phase_timer   phase(res, "pass.opentimer.incremental");
        Stdout_to_log redirect(timing_log);
        const auto    controls = livehd::placement::prects_networks(timing_tech, state.floorplan, state.prects);
        auto          feedback = timing_session.evaluate(state.graph, state.parasitics, controls.timing_cuts);
        set_crash_context(phase_step, phase_log);
        return livehd::placement::prects_data_timing(feedback, controls);
      };
      std::unique_ptr<livehd::placement::Io_timing_flow> io_flow;
      if (io_store) {
        const auto    io_log = next_log_path(opts, "pass.place.io_resynth");
        Phase_timer   phase(res, "pass.place.io_resynth.before");
        Stdout_to_log redirect(io_log);
        io_flow = std::make_unique<livehd::placement::Io_timing_flow>(*found,
                                                                      place_options,
                                                                      io_store,
                                                                      io_mapper_options,
                                                                      evaluate,
                                                                      io_resynth_percent);
        (void)io_flow->before_placement();
        place_options.after_stage = [&, io_log](auto stage, const auto& state) {
          Phase_timer   io_phase(res, "pass.place.io_resynth");
          Stdout_to_log io_redirect(io_log);
          return io_flow->after_stage(stage, state);
        };
      }
      // `synth.place.unknown_const=zero` is a refinement of a don't-care, so say how many bits it touched.
      const auto note_unknown_tied = [](const livehd::placement::Result& r) {
        if (r.unknown_const_tied > 0) {
          livehd::diag::warn("synth", "unknown-const-tied-low", "unsupported")
              .msg("{} physical net bit(s) driven by an unknown (X) constant were tied low (synth.place.unknown_const=zero)",
                   r.unknown_const_tied)
              .hint("the RTL assigns 'x on a path that reaches mapped logic; any value is a legal refinement")
              .emit();
        }
      };
      hhds::GraphLibrary seed_library;
      auto               placement_input = *found;
      if (run_timing_place && (place_options.fp_timing_interval > 0 || place_options.macro_timing_interval > 0)) {
        place_options.fp_evaluate = evaluate;
        place_options.fp_resize   = run_resize;
      }
      if (io_flow && !place_options.macro_legalize_cells && !place_options.fp_evaluate) {
        // Resynthesis still needs one selected GP measurement when periodic
        // placement timing is disabled. It replaces the post-macro STA call.
        place_options.fp_evaluate          = evaluate;
        place_options.macro_timing_interval = 0;
      }
      if (run_timing_place) {
        auto seed_options        = place_options;
        seed_options.timing_seed = true;
        seed_options.cache_dir.clear();
        seed_options.output_dir += "/post_synth";
        Phase_timer phase(res, "pass.place.post_synth_timing");
        // Like pass.place below: DREAMPlace prints to stdout, which is reserved
        // for the result protocol, and a refusal is a placement failure.
        const auto  seed_log = next_log_path(opts, "pass.place.post_synth_timing");
        phase_step           = "pass.place.post_synth_timing";
        phase_log            = seed_log;
        set_crash_context(phase_step, phase_log);
        livehd::placement::Result seed;
        try {
          Stdout_to_log redirect(seed_log);
          seed = livehd::placement::place(**found, seed_library, seed_options);
        } catch (const std::exception& e) {
          throw Lhd_error{"unsupported", std::string("timing seed placement failed: ") + e.what(), "see " + seed_log};
        }
        phase_step.clear();
        phase_log.clear();
        set_crash_context({}, {});
        note_unknown_tied(seed);
        const auto timing         = evaluate(seed);
        const auto controls       = livehd::placement::prects_networks(livehd::physical::read_lef(place_options.lefs),
                                                                 seed.floorplan,
                                                                 seed.prects,
                                                                 timing);
        const auto data_timing    = livehd::placement::prects_data_timing(timing, controls);
        place_options.net_weights = livehd::placement::timing_weights(*seed.parasitics,
                                                                      data_timing,
                                                                      place_options.timing_fraction,
                                                                      place_options.timing_max_weight,
                                                                      place_options.timing_exponent);
        if (place_options.net_weights.empty()) {
          throw Lhd_error{"unsupported",
                          "post-synthesis timing has no constrained data nets for placement weighting",
                          "check synth.sdc"};
        }
        placement_input = seed.graph;
        res.recipe_steps.emplace_back("pass.place.post_synth_timing");
      }
      const auto log = next_log_path(opts, "pass.place");
      phase_step     = "pass.place";
      phase_log      = log;
      set_crash_context(phase_step, phase_log);
      livehd::placement::Result placed;
      try {
        Phase_timer   phase(res, "pass.place");
        Stdout_to_log redirect(log);
        auto*         generation = &placed_library;
        for (unsigned attempt = 0;; ++attempt) {
          try {
            placed = livehd::placement::place(*placement_input, *generation, place_options);
            break;
          } catch (const livehd::placement::Placement_restart& restart) {
            if (attempt != 0) {
              throw std::runtime_error("whole-color resynthesis exceeded one automatic restart");
            }
            placement_input            = *found;
            retry_library              = std::make_unique<hhds::GraphLibrary>();
            generation                 = retry_library.get();
            place_options.restart_seed = std::make_shared<livehd::physical::Floorplan>(restart.seed);
            place_options.cache_dir.clear();
            res.recipe_steps.emplace_back("pass.place.io_resynth.restart");
          }
        }
      } catch (const std::exception& e) {
        throw Lhd_error{"unsupported", std::string("placement failed: ") + e.what(), "see " + log};
      }
      phase_step.clear();
      phase_log.clear();
      set_crash_context({}, {});
      if (!run_timing_place) {
        note_unknown_tied(placed);  // the seed (above) already reported it when it ran
      }
      res.recipe_steps.emplace_back("pass.place");
      // Optimization trials can be rejected; only their accepted final state
      // should replace the completed-stage feedback.
      place_options.after_stage = {};
      place_options.restart_seed.reset();
      if ((run_resize || run_timing_place || run_resynth) && !place_options.terminal_legalize) {
        // Every round/trial below re-runs placement (and resynth a fresh ABC frame
        // per cone): keep their output off the protocol stdout, as for pass.place.
        phase_step = "pass.place.optimize";
        phase_log  = next_log_path(opts, phase_step);
        set_crash_context(phase_step, phase_log);
        try {
          Stdout_to_log redirect(phase_log);
          if (run_timing_place) {
            Phase_timer phase(res, "pass.place.timing_driven");
            timed_placement = livehd::placement::timing_place(std::move(placed), place_options, evaluate, timing_rounds);
            placed          = std::move(timed_placement.placed);
            res.recipe_steps.emplace_back("pass.place.timing_driven");
          }
          if (run_resize && place_options.resize_engine == "new") {
            Phase_timer phase(res, "pass.place.resize");
            repaired = livehd::placement::resize_placed(std::move(placed), place_options, evaluate);
            placed   = std::move(repaired.placed);
            res.recipe_steps.emplace_back("pass.place.resize");
          } else if (run_resize) {
            Phase_timer phase(res, "pass.place.resize");
            repaired = livehd::placement::repair_buffers(std::move(placed),
                                                         place_options,
                                                         evaluate,
                                                         livehd::placement::Repair_scope::sizing,
                                                         std::move(timed_placement.timing));
            placed   = std::move(repaired.placed);
            res.recipe_steps.emplace_back("pass.place.resize");
          }
          if (run_resize && place_options.resize_engine != "new") {
            Phase_timer phase(res, "pass.place.repair");
            design_repaired = livehd::placement::repair_buffers(std::move(placed),
                                                                place_options,
                                                                evaluate,
                                                                livehd::placement::Repair_scope::design,
                                                                std::move(repaired.timing));
            placed          = std::move(design_repaired.placed);
            res.recipe_steps.emplace_back("pass.place.repair");
          }
          if (run_resynth) {
            Phase_timer phase(res, "pass.place.resynth");
            resynthesized = livehd::placement::resynthesize_cones(std::move(placed),
                                                                  place_options,
                                                                  evaluate,
                                                                  [](const auto& logic, const auto& boundary, const auto& library) {
                                                                    return livehd::synth::map_cone("abc", logic, boundary, library);
                                                                  });
            placed        = std::move(resynthesized.placed);
            res.recipe_steps.emplace_back("pass.place.resynth");
          }
        } catch (const std::exception& e) {
          throw Lhd_error{"unsupported", std::string("placement optimization failed: ") + e.what(), "see " + phase_log};
        }
        phase_step.clear();
        phase_log.clear();
        set_crash_context({}, {});
      }
      net.graphs = {placed.graph};
      if (io_flow) {
        const auto    io_log = next_log_path(opts, "pass.place.io_resynth.final");
        Stdout_to_log redirect(io_log);
        (void)io_flow->after_stage(livehd::placement::Placement_stage::final, placed);
        rapidjson::Document q, feedback;
        q.Parse(placed.qor.c_str());
        feedback.Parse(io_flow->report().c_str());
        if (!q.HasParseError() && q.IsObject() && !feedback.HasParseError()) {
          q.AddMember("io_resynth", rapidjson::Value(feedback, q.GetAllocator()), q.GetAllocator());
          rapidjson::StringBuffer                    buffer;
          rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
          q.Accept(writer);
          placed.qor = buffer.GetString();
        }
        res.outputs.push_back(root + "/place/io_resynth.json");
      }
      net.physical_rc = placed.parasitics;
      place_qor       = placed.qor;
      placed.graph->get_io()->get_library()->save(net_dir);
    }
    for (const auto& path : place_options.lefs) {
      res.inputs.push_back(path);
    }
    if (!place_options.tech_rc_file.empty()) {
      res.inputs.push_back(place_options.tech_rc_file);
    }
    if (!place_options.tracks_file.empty()) {
      res.inputs.push_back(place_options.tracks_file);
    }
    if (!place_options.floorplan.empty()) {
      res.inputs.push_back(place_options.floorplan);
    }
    if (user_workdir) {
      res.outputs.push_back(place_qor_path);
      if (place_options.full) {
        res.outputs.push_back(root + "/place/out.def");
      }
    }
    if (const auto* def_out = find_slot(opts.emit_dirs, "def")) {
      ensure_dir(def_out->path);
      const auto destination = fs::path(def_out->path) / "out.def";
      if (canon(destination.string()) != canon(root + "/place/out.def")) {
        copy_export_file(root + "/place/out.def", destination);
      }
      res.outputs.push_back(destination.string());
      // A DEF alone is not a portable physical handoff. Preserve the exact
      // ordered technology/cell views in the requested export, including their
      // copy cost in this invocation rather than in a downstream STA script.
      const auto lef_directory = fs::path(def_out->path) / "lef";
      ensure_dir(lef_directory.string());
      std::string lef_manifest = "[";
      for (size_t index = 0; index < place_options.lefs.size(); ++index) {
        const auto& source = place_options.lefs[index];
        const auto  name   = std::to_string(index) + "_" + fs::path(source).filename().string();
        const auto  target = lef_directory / name;
        if (canon(source) != canon(target.string())) {
          copy_export_file(source, target);
        }
        if (index) {
          lef_manifest += ",";
        }
        lef_manifest += livehd::synth_attr::quote("lef/" + name);
        res.outputs.push_back(target.string());
      }
      const auto proxy_lef = fs::path(root) / "place/memory_proxies.lef";
      if (fs::is_regular_file(proxy_lef)) {
        const auto proxy_destination = fs::path(def_out->path) / "memory_proxies.lef";
        if (canon(proxy_destination.string()) != canon(proxy_lef.string())) {
          copy_export_file(proxy_lef, proxy_destination);
        }
        res.outputs.push_back(proxy_destination.string());
        if (!place_options.lefs.empty()) {
          lef_manifest += ",";
        }
        lef_manifest += livehd::synth_attr::quote("memory_proxies.lef");
      }
      lef_manifest             += "]\n";
      const auto manifest_path  = fs::path(def_out->path) / "lefs.json";
      std::ofstream(manifest_path) << lef_manifest;
      res.outputs.push_back(manifest_path.string());
    }
  }

  // ---- 4. STA -----------------------------------------------------------------
  const std::string timing_path = root + "/timing.json";
  std::string       sta_qor;
  if (run_sta) {
    std::string files = liberty;
    if (!sdc.empty()) {
      files += "," + sdc;
    }
    if (!spef.empty()) {
      files += "," + spef;
    }
    Eprp_var::Eprp_dict labels{
        {"files", files}
    };
    labels["top"] = top;
    labels["qor"] = timing_path;
    merge_sets(opts, "pass.opentimer", labels);
    if (opts.stats) {
      labels["stats"] = "true";
    }
    // Incremental STA reuse: <workdir>/sta_cache, under the same gate as the
    // compile and abc tiers. Set after merge_sets, so it is kernel-owned.
    if (user_workdir && opts.incremental) {
      labels["cache_dir"] = (fs::path(opts.workdir) / "sta_cache").string();
    }
    run_step("pass.opentimer", net, labels, opts, res);
    if (labels["critical"] == "true" || labels["critical"] == "1") {
      if (run_place) {
        net.graphs.front()->get_io()->get_library()->save(net_dir);
      } else {
        livehd::Hhds_graph_library::save(net_dir);
      }
    }
    sta_qor = slurp_json(timing_path);
    if (user_workdir && !sta_qor.empty()) {
      res.outputs.push_back(timing_path);
    }
  }

  if (run_place) {
    rapidjson::Document placement, timing;
    placement.Parse(place_qor.c_str());
    timing.Parse(sta_qor.c_str());
    auto& allocator = placement.GetAllocator();
    if (!timing.HasParseError() && timing.IsObject() && timing.HasMember("designs") && timing["designs"].IsArray()
        && !timing["designs"].Empty()) {
      const auto& design = timing["designs"][0];
      for (const auto* metric : {"wns", "tns"}) {
        rapidjson::Value value;
        if (design.HasMember(metric) && design[metric].IsNumber()) {
          value.SetDouble(design[metric].GetDouble());
        }
        placement.AddMember(rapidjson::Value(metric, allocator), value, allocator);
      }
      if (timing.HasMember("time_unit")) {
        placement.AddMember("time_unit", rapidjson::Value(timing["time_unit"], allocator), allocator);
      }
    }
    rapidjson::StringBuffer                    buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    placement.Accept(writer);
    place_qor = buffer.GetString();
    std::ofstream output(place_qor_path);
    output << place_qor << '\n';
    output.close();
    if (!output) {
      throw Lhd_error{"io", "cannot write placement QoR", place_qor_path};
    }
    if (report_emit != nullptr) {
      ensure_dir(report_emit->path);
      const auto destination = fs::path(report_emit->path) / "placement.json";
      if (canon(destination.string()) != canon(place_qor_path)) {
        copy_export_file(place_qor_path, destination);
      }
      res.outputs.push_back(destination.string());
    }
  }

  // ---- reports ----------------------------------------------------------------
  // The envelope's "qor" member: {kind:"synth", abc:<abc-map>, sta:<sta>} (plus
  // the mapper's own report under its name, e.g. usyn:<decision report>) —
  // each sub-report byte-identical to what its pass alone embeds, so a
  // consumer checks each report kind before reading its fields. USYN mapping
  // uses technology-map; logical-only output has abc:null and no STA report.
  const auto timing_target = timing_target_json(abc_qor, sta_qor, sdc);
  res.qor_json = std::format(R"({{"schema_version":1,"kind":"synth","top":"{}","abc":{}{}{}{}}})",
                             json_escape_min(top),
                             abc_qor.empty() ? std::string{"null"} : abc_qor,
                             sta_qor.empty() ? std::string{} : std::format(R"(,"sta":{})", sta_qor),
                             timing_target.empty() ? std::string{} : std::format(R"(,"timing_target":{})", timing_target),
                             mapper_qor.empty() ? std::string{} : std::format(R"(,"{}":{})", mapper->report, mapper_qor));
  if (!place_qor.empty()) {
    res.qor_json.pop_back();
    res.qor_json += ",\"place\":" + place_qor + "}";
  }
  harvest_abc_incremental(res);           // the envelope's `incremental.abc` (one place for every reuse tier)
  harvest_sta_incremental(res, sta_qor);  // ... and `incremental.sta`
  if (report_emit != nullptr) {
    // --emit-dir report:DIR — the sidecars as files, for a run with no
    // --workdir to keep them in (or a build system that declares outputs).
    ensure_dir(report_emit->path);
    std::error_code ec;
    // A logical-only run does not produce timing: never export a timing.json
    // left by an earlier mapped run in the same workdir.
    for (const auto& src : {qor_path, run_sta ? timing_path : std::string{}, report_path}) {
      if (src.empty()) {
        continue;
      }
      if (!fs::exists(src)) {
        continue;
      }
      const auto dst = (fs::path(report_emit->path) / fs::path(src).filename()).string();
      try {
        copy_export_file(src, dst);
      } catch (const fs::filesystem_error& error) {
        throw Lhd_error{"config", std::format("could not write {}: {}", dst, error.what()), "check --emit-dir report: permissions"};
      }
      res.outputs.push_back(dst);
    }
    for (const auto& archive : {provenance_path, artifact_path}) {
      if (!has_report || archive.empty() || !fs::exists(archive)) {
        continue;
      }
      const auto dst = fs::path(report_emit->path) / fs::path(archive).filename();
      // Replace the whole archive: leaving older blobs would misrepresent its
      // bounded content and could conceal missing files in the new capture.
      if (canon(dst.string()) != canon(archive)) {
        fs::remove_all(dst, ec);
        if (!ec) {
          fs::copy(archive, dst, fs::copy_options::recursive, ec);
        }
      }
      if (ec) {
        throw Lhd_error{"config", std::format("could not write {}: {}", dst.string(), ec.message()), "check report permissions"};
      }
      res.outputs.push_back(dst.string());
    }
  }

  // The mapped netlist as Verilog: --emit verilog:FILE / --emit-dir verilog:DIR
  // (cgen over the cell-instantiating netlist, as `lhd compile lg:net` would).
  emit_verilog_outputs(opts, res, net);
  if (physical_cache && !restored_library) {
    Phase_timer phase(res, "pass.place.save");
    try {
      physical_cache->publish(net_dir, root + "/place");
    } catch (const std::exception& error) {
      livehd::diag::warn("synth", "physical-cache-save", "io").msg("cannot retain completed placement: {}", error.what()).emit();
    }
  }
}

}  // namespace lhd
