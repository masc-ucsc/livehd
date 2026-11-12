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

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <fcntl.h>
#include <thread>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <charconv>
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

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_split.h"
#include "attrs.hpp"
#include "debug_env.hpp"
#include "diag.hpp"
#include "domino_bundle.hpp"
#include "compile_salt.hpp"
#include "export_file.hpp"
#include "file_utils.hpp"
#include "graph_library_singleton.hpp"
#include "inou/lefdef/route_store.hpp"
#include "lhd_kernel_internal.hpp"
#include "liberty_dff.hpp"
#include "node_util.hpp"
#include "pass.hpp"
#include "pass/abc/abc_salt.hpp"
#include "pass/domplace/timing/domino_liberty.hpp"
#include "pass/domplace/timing/stage_timing.hpp"
#include "pass/domplace/timing/stage_timing_salt.hpp"
#include "pass/opentimer/pass_opentimer.hpp"
#include "pass/timing/physical_rank.hpp"
#include "pass/opentimer/sta_salt.hpp"
#include "pass/place/custom_sizing.hpp"
#include "pass/place/growth_budget.hpp"
#include "pass/place/gui/gui_session.hpp"
#include "pass/place/io_timing.hpp"
#include "pass/place/place.hpp"
#include "pass/place/placement_hints.hpp"
#include "pass/place/placement_technology.hpp"
#include "pass/place/preroute_host.hpp"
#include "pass/place/repair.hpp"
#include "pass/synth/sram_macro.hpp"
#include "pass/place/resize_engine.hpp"
#include "physical_cache.hpp"
#include "physical_salt.hpp"
#include "woothash.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "synth_policy.hpp"

extern char** environ;

namespace lhd {

namespace {

bool truthy(std::string_view v) { return !v.empty() && v != "false" && v != "0" && v != "off"; }

std::string physical_descriptor(const Options& opts, const livehd::placement::Options& place_options) {
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
  w.StartArray();
  w.Uint64(kPhysicalSrcSalt);
  w.String(livehd::placement::uroute_cache_identity(place_options).c_str());
  w.String(opts.top.c_str());
  const std::array<std::string_view, 1> metadata_classes{"livehd.placement.hints"};
  w.String(place_options.metadata ? place_options.metadata->identity(metadata_classes, opts.top).c_str() : "[]");
  w.String(opts.seed.c_str());
  w.Uint(opts.jobs);
  // Report-only stage timing reruns on every restore and never changes the
  // placement, so it is not placement identity -- unless domino sizing
  // optimizes against it: then its settings and its code salt are.
  const bool sizing = truthy(synth_set(opts, "place.domino_sizing", "false"));
  if (sizing) {
    w.Uint64(livehd::domplace::timing::kStageTimingSalt);
  }
  for (const auto& [key, value] : opts.sets) {
    if (!sizing && (key == "synth.place.stage_timing" || key.starts_with("synth.place.stage_timing."))) {
      continue;
    }
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

namespace stage = livehd::domplace::timing;
// The stage-timing clock (2d-domplace E): synth.place.stage_timing.clock_port,
// else pass.usyn.domino_clock, else the SDC's one clock port. Period, name and
// waveform are those of the SDC clock on that port; an SDC with one clock
// whose port cannot be resolved lends its period. With several clocks and
// none (or more than one) on the port, the spec is left unresolved: a
// saved-SDC-subset SDC still times, anything else is refused at timing. The
// domino schedule is a fixed T/2, so a waveform it cannot read or a non-50%
// one is a usage error before synthesis.
stage::Clock_spec stage_timing_clock(const std::string& port, const Eprp_var::Eprp_dict& usyn_sets, const std::string& sdc,
                                     const std::string& liberty) {
  const auto        clocks = sdc.empty() ? std::vector<Sdc_clock>{} : sdc_clocks(sdc);
  stage::Clock_spec spec;
  spec.port = port;
  if (const auto it = usyn_sets.find("domino_clock"); spec.port.empty() && it != usyn_sets.end() && !it->second.empty()) {
    spec.port = it->second;
  } else if (spec.port.empty() && clocks.size() == 1 && clocks.front().port_resolved) {
    spec.port = clocks.front().port;
  }
  if (spec.port.empty()) {
    throw Lhd_error{"usage",
                    "synth.place.stage_timing needs one resolvable clock port",
                    "set synth.place.stage_timing.clock_port, or give synth.sdc one create_clock on one port"};
  }
  const Sdc_clock* match   = nullptr;
  size_t           on_port = 0;
  for (const auto& clock : clocks) {
    if (clock.port_resolved && clock.port == spec.port) {
      match = &clock;
      ++on_port;
    }
  }
  if (on_port != 1) {
    // The one clock of the SDC, on a port the scan cannot resolve, is taken
    // as the clock on `port`.
    match = clocks.size() == 1 && !clocks.front().port_resolved ? &clocks.front() : nullptr;
  }
  if (clocks.empty() || (match && !match->period)) {
    throw Lhd_error{"usage",
                    "synth.place.stage_timing needs a resolvable SDC clock period",
                    "set synth.sdc with create_clock -period"};
  }
  if (!match) {
    spec.unresolved = std::format("the SDC has {} clock{} and {} on port {}",
                                  clocks.size(),
                                  clocks.size() == 1 ? "" : "s",
                                  on_port == 0 ? "none resolves" : "several resolve",
                                  spec.port);
    return spec;
  }
  const double to_ps = liberty_sdc_units(liberty).time_ps;
  spec.name          = match->name;
  spec.period_ps     = *match->period * to_ps;
  if (match->waveform_unparsed) {
    throw Lhd_error{"usage",
                    "synth.place.stage_timing cannot read the SDC clock waveform",
                    "the domino stage schedule is a fixed T/2; spell -waveform {rise fall} with numbers"};
  }
  if (match->waveform) {
    spec.waveform_ps = std::array{(*match->waveform)[0] * to_ps, (*match->waveform)[1] * to_ps};
  }
  try {
    stage::check_clock_spec(spec);
  } catch (const std::exception& error) {
    throw Lhd_error{"usage", std::string("synth.place.stage_timing: ") + error.what(), ""};
  }
  return spec;
}
// The stage timer of this run (2d-domplace E): the Liberty, the bundle's
// provisional cells, the SDC or the clock spec and the placement LEFs, built
// once for the report and every domino sizing trial. A native memory is placed
// as a virtual proxy macro (pass/place/memory_proxy.cpp) declared only in
// `<place_dir>/memory_proxies.lef`; the bit-level DEF import of a netlist that
// keeps the memory's word-valued ports needs that LEF too.
std::unique_ptr<stage::Stage_timer> make_stage_timer(const std::string& liberty, const std::string& sdc, const std::string& top,
                                                     const std::string& bundle, std::vector<std::string> lefs,
                                                     const std::string& place_dir, const stage::Clock_spec& clock,
                                                     double clock_uncertainty_ps) {
  if (const auto proxy_lef = fs::path(place_dir) / "memory_proxies.lef"; fs::is_regular_file(proxy_lef)) {
    lefs.push_back(proxy_lef.string());
  }
  stage::Stage_timer_inputs inputs;
  std::string               first_liberty;
  for (const auto path : absl::StrSplit(liberty, absl::ByAnyChar(", \t\n"), absl::SkipEmpty())) {
    const auto contents = livehd::file_utils::read_file(std::string(path));
    if (!contents) {
      throw Lhd_error{"io", "stage timing cannot read the Liberty", std::string(path)};
    }
    if (first_liberty.empty()) {
      first_liberty = std::string(path);
    }
    inputs.liberty += *contents;
    inputs.liberty += '\n';
  }
  if (!bundle.empty()) {
    inputs.bundle = bundle;
  }
  if (!sdc.empty()) {
    inputs.sdc = livehd::file_utils::read_file(sdc);
  }
  inputs.design               = top;
  inputs.units                = liberty_sdc_units(first_liberty);
  inputs.clock                = clock;
  inputs.clock_uncertainty_ps = clock_uncertainty_ps;
  try {
    inputs.technology = livehd::physical::read_lef(lefs);
    return std::make_unique<stage::Stage_timer>(std::move(inputs));
  } catch (const std::exception& error) {
    throw Lhd_error{"unsupported", std::string("stage timing failed: ") + error.what(), ""};
  }
}
// The bundle's domino cells as a Liberty library next to the run's other
// artifacts (pass/domplace/timing/domino_liberty.hpp): OpenTimer reads Liberty
// only, and placement resizing, buffer repair and timing-driven placement run
// on its timing. The rail capture is written for the SDC clock `period_ps`.
std::string write_domino_liberty(const std::string& liberty, const std::string& bundle, double period_ps, const std::string& path) {
  std::string base_text;
  for (const auto file : absl::StrSplit(liberty, absl::ByAnyChar(", \t\n"), absl::SkipEmpty())) {
    const auto contents = livehd::file_utils::read_file(std::string(file));
    if (!contents) {
      throw Lhd_error{"io", "the domino Liberty cannot read the cell Liberty", std::string(file)};
    }
    base_text += *contents;
    base_text += '\n';
  }
  std::string why;
  const auto  view = livehd::domplace::timing::read_domino_timing(bundle, &why);
  if (!view) {
    throw Lhd_error{"internal", "pass.usyn published no usable domino timing view", why};
  }
  if (view->variants.empty()) {
    return {};  // nothing converted: no domino cell exists to time
  }
  try {
    const auto text = livehd::domplace::timing::domino_liberty(*view, livehd::liberty::timing::parse(base_text), period_ps);
    std::ofstream(path) << text;
  } catch (const std::exception& error) {
    throw Lhd_error{"unsupported",
                    std::string("domino cells cannot be timed by OpenTimer: ") + error.what(),
                    "use synth.opentimer=false, or fix the bundle's stage coefficients"};
  }
  return path;
}
// Native stage timing of the final placed netlist: the same timer for every
// arm, with the bundle's provisional cells when the domino profile is on. A
// netlist that keeps word-valued ports is timed on its bit-level DEF import.
// Writes the stage_timing.json report and returns the timing.
stage::Stage_timing stage_timing_report(const stage::Stage_timer& timer, const hhds::Graph& graph,
                                        const livehd::physical::Parasitics* rc, const std::string& def,
                                        const std::string& report_path) {
  try {
    std::optional<livehd::physical::Floorplan> floorplan;
    if (stage::word_valued_ports(graph)) {
      floorplan = livehd::physical::read_def(def);
    }
    auto          timing = timer.evaluate(graph, rc, floorplan ? &*floorplan : nullptr, {});
    std::ofstream report(report_path);
    report << timing.json;
    report.close();
    if (!report) {
      throw Lhd_error{"io", "cannot write the stage timing report", report_path};
    }
    return timing;
  } catch (const Lhd_error&) {
    throw;
  } catch (const std::exception& error) {
    throw Lhd_error{"unsupported", std::string("stage timing failed: ") + error.what(), ""};
  }
}

livehd::placement::Options placement_options(const Options& opts) {
  livehd::placement::Options out;
  out.metadata = opts.metadata;
  auto number  = [&](std::string_view key, std::string_view fallback) {
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
  out.seed_file = synth_set(opts, "place.seed", "");
  if (!out.seed_file.empty()) {
    check_inputs_exist({out.seed_file});
  }
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
    policy.weight     = number(key + ".weight", stage == "fp" ? "1" : "0.7");
    policy.bins       = integer(key + ".bins", "16");
    policy.iterations = integer(key + ".iterations", "2");
    policy.seconds    = number(key + ".seconds", "3");
    if (stage == "macro") {
      policy.once_after_density = truthy(synth_set(opts, key + ".once_after_density", "false"));
      policy.updates            = integer(key + ".updates", "10");
      policy.warmup             = number(key + ".warmup", "0.1");
      policy.start_stability    = number(key + ".start_stability", "0.20");
      policy.stability_window   = integer(key + ".stability_window", "10");
      policy.refresh_interval   = integer(key + ".refresh_interval", "25");
    }
    livehd::placement::validate_warp_options(policy);
  }
  {
    auto& anchor            = out.macro_anchor;
    anchor.enabled          = truthy(synth_set(opts, "place.macro.anchor", "false"));
    anchor.weight           = number("place.macro.anchor.weight", "0.5");
    anchor.gate             = number("place.macro.anchor.gate", "1");
    anchor.start_overflow   = number("place.macro.anchor.start_overflow", "0.3");
    anchor.refresh_interval = static_cast<int>(integer("place.macro.anchor.refresh_interval", "30"));
    anchor.ramp             = static_cast<int>(integer("place.macro.anchor.ramp", "3"));
    anchor.max_refreshes    = static_cast<int>(integer("place.macro.anchor.max_refreshes", "40"));
    anchor.final_weight     = number("place.macro.anchor.final_weight", "2");
    anchor.settle           = static_cast<int>(integer("place.macro.anchor.settle", "20"));
    anchor.swaps            = truthy(synth_set(opts, "place.macro.anchor.swaps", "false"));
    anchor.swap_tolerance   = number("place.macro.anchor.swap_tolerance", "0.05");
    anchor.swap_lock        = static_cast<int>(integer("place.macro.anchor.swap_lock", "2"));
    anchor.inflate          = number("place.macro.anchor.inflate", "0");
    anchor.swap_jump        = truthy(synth_set(opts, "place.macro.anchor.swap_jump", "true"));
    anchor.dead_space_refreshes
        = parse_macro_anchor_dead_space_refreshes(synth_set(opts, "place.macro.anchor.dead_space_refreshes", "2"));
    anchor.select            = synth_set(opts, "place.macro.anchor.select", "edge+random");
    anchor.edge_weight       = number("place.macro.anchor.edge_weight", "4");
    anchor.random_weight     = number("place.macro.anchor.random_weight", "1");
    anchor.random_fraction   = number("place.macro.anchor.random_fraction", "0.25");
    anchor.edge_gate         = truthy(synth_set(opts, "place.macro.anchor.edge_gate", "false"));
    anchor.edge_chain        = truthy(synth_set(opts, "place.macro.anchor.edge_chain", "true"));
    anchor.final_targets     = truthy(synth_set(opts, "place.macro.anchor.final_targets", "true"));
    anchor.spring_length     = number("place.macro.anchor.spring_length", "0.1");
    anchor.target_hysteresis = number("place.macro.anchor.target_hysteresis", "0.5");
    anchor.milestones        = parse_macro_anchor_milestones(synth_set(opts, "place.macro.anchor.milestones", "0.15,0.1"));
    try {
      livehd::placement::validate_macro_anchor_options(anchor);
    } catch (const std::invalid_argument& error) {
      throw Lhd_error{"usage", error.what(), ""};
    }
  }
  out.region.max_cells = integer("place.region.max_cells", "10000");
  out.mesh_variant     = integer("place.mesh_variant", "0");  // legroute push-back state (set by the restart loop)
  out.mesh_variants    = integer("place.mesh_variants", "0");
  out.area_search      = truthy(synth_set(opts, "place.area_search", "false"));  // set by the driver's search
  out.region.merge_um  = number("place.region.merge_um", "5");
  out.region.cut       = synth_set(opts, "place.region.cut", "macro");
  livehd::placement::validate_region_options(out.region);
  out.boundarypins.enabled             = truthy(synth_set(opts, "place.region.boundarypins", "false"));
  out.boundarypins.split               = truthy(synth_set(opts, "place.region.boundarypins.split", "true"));
  out.boundarypins.order               = truthy(synth_set(opts, "place.region.boundarypins.order", "false"));
  out.boundarypins.iterations          = integer("place.region.boundarypins.iterations", "50");
  out.boundarypins.max_displacement_um = number("place.region.boundarypins.max_displacement_um", "20");
  out.boundarypins.settle_iterations   = integer("place.region.boundarypins.settle_iterations", "20");
  out.boundarypins.pitch_um            = number("place.region.boundarypins.pitch_um", "0");
  out.boundarypins.align               = truthy(synth_set(opts, "place.region.pinalign", "false"));
  out.boundarypins.align_near          = number("place.region.pinalign.near", "0");
  out.boundarypins.clean_cuts          = truthy(synth_set(opts, "place.region.cleancuts", "false"));
  // legroute's regions want clean cuts (no net dipping into a region through one side with 0 or 1 cell there)
  if (truthy(synth_set(opts, "place.legroute", "false")) && synth_set(opts, "place.region.cleancuts", "").empty()) {
    out.boundarypins.clean_cuts = true;
  }
  // bus order across region edges is the boundary-pin flow with ordered, spaced, fixed virtual pins
  if (truthy(synth_set(opts, "place.region.busorder", "true"))) {
    out.boundarypins.enabled = out.boundarypins.split = out.boundarypins.order = true;
  }
  livehd::placement::validate_boundary_pin_options(out.boundarypins);
  out.legroute                                  = truthy(synth_set(opts, "place.legroute", "false"));
  out.legroute_options.final                    = truthy(synth_set(opts, "place.legroute.final", "true"));
  out.legroute_options.box_fill                 = number("place.legroute.box_fill", "1");
  out.legroute_options.merge_cost               = number("place.legroute.merge_cost", "20");
  out.legroute_options.clock_spine              = truthy(synth_set(opts, "place.legroute.clock_spine", "true"));
  out.legroute_options.rib_tiles                = integer("place.legroute.rib_tiles", "4");
  out.legroute_options.cap_scale                = number("place.legroute.cap_scale", "1");
  out.legroute_options.window_tiles             = integer("place.legroute.window_tiles", "4");
  out.legroute_options.crit_boxes               = number("place.legroute.crit_boxes", "0");
  out.legroute_options.tree_boxes               = number("place.legroute.tree_boxes", "0");
  out.legroute_options.tree_moves               = integer("place.legroute.tree_moves", "2");
  out.legroute_options.sat_relax_rounds         = integer("place.legroute.sat_relax_rounds", "0");
  out.legroute_options.tree_load                = number("place.legroute.tree_load", "0");
  out.legroute_options.tree_clauses             = number("place.legroute.tree_clauses", "10000000");
  out.legroute_options.tree_by_crit             = truthy(synth_set(opts, "place.legroute.tree_by_crit", "false"));
  out.legroute_options.io_preroute              = truthy(synth_set(opts, "place.legroute.io_preroute", "false"));
  out.legroute_options.sat_alt                  = synth_set(opts, "place.legroute.sat_alt", "");
  out.legroute_options.tree_congestion          = number("place.legroute.tree_congestion", "0");
  out.legroute_options.steiner_pins             = integer("place.legroute.steiner_pins", "0");
  out.legroute_options.steiner_min              = integer("place.legroute.steiner_min", "3");
  out.legroute_options.reserved_tracks          = integer("place.legroute.reserved_tracks", "1");
  out.legroute_options.box_reserve              = synth_set(opts, "place.legroute.box_reserve", "M2:1,M3:1");
  out.legroute_options.critical_cellboxes       = truthy(synth_set(opts, "place.legroute.critical_cellboxes", "false"));
  out.legroute_options.cellbox_sites            = integer("place.legroute.cellbox_sites", "22");
  out.legroute_options.cellbox_rows             = integer("place.legroute.cellbox_rows", "4");
  out.legroute_options.cellbox_external_pins    = integer("place.legroute.cellbox_external_pins", "28");
  out.legroute_options.cellbox_connections      = integer("place.legroute.cellbox_connections", "10");
  out.legroute_options.cellbox_critical_choices = integer("place.legroute.cellbox_critical_choices", "4");
  out.legroute_options.cellbox_other_choices    = integer("place.legroute.cellbox_other_choices", "8");
  out.legroute_options.cellbox_wide_exceptions  = truthy(synth_set(opts, "place.legroute.cellbox_wide_exceptions", "true"));
  out.legroute_options.cellbox_full_flow        = truthy(synth_set(opts, "place.legroute.cellbox_full_flow", "false"));
  out.legroute_options.cellbox_diagnostic_final = truthy(synth_set(opts, "place.legroute.cellbox_diagnostic_final", "false"));
  out.legroute_options.cellbox_relocate         = truthy(synth_set(opts, "place.legroute.cellbox_relocate", "false"));
  out.legroute_options.cellbox_relocate_window  = unsigned(integer("place.legroute.cellbox_relocate_window", "4"));
  out.legroute_options.cellbox_local_replan     = truthy(synth_set(opts, "place.legroute.cellbox_local_replan", "false"));
  out.legroute_options.cellbox_local_detour_rounds
      = unsigned(integer("place.legroute.cellbox_local_detour_rounds", "4"));
  out.legroute_options.cellbox_local_detour_boost
      = truthy(synth_set(opts, "place.legroute.cellbox_local_detour_boost", "true"));
  out.legroute_options.cellbox_far_fallback     = truthy(synth_set(opts, "place.legroute.cellbox_far_fallback", "false"));
  if (out.legroute_options.critical_cellboxes) {
    out.legroute_options.box_reserve = "M2:20,M3:20";
  }
  out.legroute_options.paired_pin_reserve     = truthy(synth_set(opts, "place.legroute.paired_pin_reserve", "false"));
  out.legroute_options.paired_pin_cap         = integer("place.legroute.paired_pin_cap", "0");
  out.legroute_options.l5_repair_ms           = integer("place.legroute.l5_repair_ms", "0");
  out.legroute_options.reserved_tracks_per_um = number("place.legroute.reserved_tracks_per_um", "-1");
  if (out.legroute_options.critical_cellboxes) {
    out.legroute_options.reserved_tracks        = 0;
    out.legroute_options.reserved_tracks_per_um = -1;
  }
  out.legroute_options.reserved_layers          = synth_set(opts, "place.legroute.reserved_layers", "");
  out.legroute_options.hop_share                = number("place.legroute.hop_share", "0.5");
  out.legroute_options.reserve_share            = number("place.legroute.reserve_share", "0.5");
  out.legroute_options.global_fraction          = number("place.legroute.global_fraction", "0.05");
  out.legroute_options.global_route             = truthy(synth_set(opts, "place.legroute.global_route", "false"));
  out.legroute_options.global_all               = truthy(synth_set(opts, "place.legroute.global_all", "false"));
  out.legroute_options.global_keep              = truthy(synth_set(opts, "place.legroute.global_keep", "false"));
  out.legroute_options.partition_cells          = static_cast<unsigned>(integer("place.legroute.partition_cells", "0"));
  out.legroute_options.partition_critical       = number("place.legroute.partition_critical", "0.05");
  out.legroute_options.partition_critical_cost  = number("place.legroute.partition_critical_cost", "-1");
  out.legroute_options.partition_mesh           = truthy(synth_set(opts, "place.legroute.partition_mesh", "false"));
  out.legroute_options.partition_timeout_rounds = static_cast<unsigned>(integer("place.legroute.partition_timeout_rounds", "0"));
  out.legroute_options.global_share             = number("place.legroute.global_share", "0.5");
  out.legroute_options.global_own_share         = number("place.legroute.global_own_share", "0");
  out.legroute_options.hop_choice               = static_cast<unsigned>(integer("place.legroute.hop_choice", "0"));
  out.legroute_options.spill_rounds             = static_cast<unsigned>(integer("place.legroute.spill_rounds", "0"));
  out.legroute_options.spill_fraction           = number("place.legroute.spill_fraction", "0.2");
  out.legroute_options.window_grow              = static_cast<unsigned>(integer("place.legroute.window_grow", "0"));
  out.legroute_options.spill_shift              = truthy(synth_set(opts, "place.legroute.spill_shift", "false"));
  out.legroute_options.mesh_join                = truthy(synth_set(opts, "place.legroute.mesh_join", "false"));
  out.legroute_options.alternatives             = synth_set(opts, "place.legroute.alternatives", "");
  out.legroute_options.alt_ms                   = static_cast<unsigned>(integer("place.legroute.alt_ms", "0"));
  out.legroute_options.region_pins              = truthy(synth_set(opts, "place.legroute.region_pins", "true"));
  out.legroute_options.alt_all                  = truthy(synth_set(opts, "place.legroute.alt_all", "false"));
  out.legroute_options.checker_window           = static_cast<unsigned>(integer("place.legroute.checker_window", "8"));
  out.legroute_options.global_margin            = integer("place.legroute.global_margin", "8");
  out.legroute_options.global_rounds            = static_cast<unsigned>(integer("place.legroute.global_rounds", "16"));
  out.legroute_options.global_net_pops          = integer("place.legroute.global_net_pops", "0");
  out.legroute_options.global_pattern           = truthy(synth_set(opts, "place.legroute.global_pattern", "false"));
  const auto box_route_seed                     = synth_set(opts, "place.legroute.box_route_seed", "auto");
  out.legroute_options.fit_density              = number("place.legroute.fit_density", "0.9");
  out.legroute_options.density_ceiling          = number("place.legroute.density_ceiling", "0.9");
  out.legroute_options.fit_window               = static_cast<unsigned>(integer("place.legroute.fit_window", "2"));
  out.legroute_options.fit_first                = truthy(synth_set(opts, "place.legroute.fit_first", "true"));
  out.legroute_options.global_astar_weight      = number("place.legroute.global_astar_weight", "1");
  out.legroute_options.dump_regions             = synth_set(opts, "place.legroute.dump_regions", "");
  out.legroute_options.threads                  = integer("place.legroute.threads", "0");
  out.legroute_options.hop_spread               = number("place.legroute.hop_spread", "6");
  out.legroute_options.sat_ms                   = integer("place.legroute.sat_ms", "60000");
  out.legroute_options.capacity_lazy            = integer("place.legroute.capacity_lazy", "2");
  out.legroute_options.freeze                   = truthy(synth_set(opts, "place.legroute.freeze", "false"));
  out.legroute_options.legal_boxes              = truthy(synth_set(opts, "place.legroute.legal_boxes", "false"));
  out.legroute_options.legal_window             = static_cast<unsigned>(integer("place.legroute.legal_window", "0"));
  out.legroute_options.legal_shuffle            = static_cast<unsigned>(integer("place.legroute.legal_shuffle", "0"));
  out.legroute_options.sat_seed                 = truthy(synth_set(opts, "place.legroute.sat_seed", "true"));
  out.legroute_options.tiers                    = synth_set(opts, "place.legroute.tiers", "adjacent,pins,home");
  out.legroute_options.tier_rounds              = integer("place.legroute.tier_rounds", "8");
  out.legroute_options.tier_fraction            = number("place.legroute.tier_fraction", "0.05");
  out.legroute_options.tier_batch               = integer("place.legroute.tier_batch", "8");
  out.legroute_options.tier_ms                  = integer("place.legroute.tier_ms", "30000");
  out.legroute_probe.tree_congestion            = number("place.legroute.probe.tree_congestion", "1");
  out.legroute_probe.enabled                    = truthy(synth_set(opts, "place.legroute.probe", "false"));
  out.legroute_probe.cells                      = integer("place.legroute.probe.cells", "1200");
  out.legroute_probe.fill                       = number("place.legroute.probe.fill", "0.5");
  out.legroute_probe.target                     = number("place.legroute.probe.target", "0.6");
  out.legroute_probe.max_inflate                = number("place.legroute.probe.max_inflate", "1.5");
  out.legroute_probe.sat_ms                     = integer("place.legroute.probe.sat_ms", "20000");
  out.legroute_probe.iterations                 = integer("place.legroute.probe.iterations", "100");
  out.legroute_probe.rounds                     = integer("place.legroute.probe.rounds", "1");
  out.legroute_probe.real                       = truthy(synth_set(opts, "place.legroute.probe.real", "false"));
  out.legroute_probe.repulsion                  = number("place.legroute.probe.repulsion", "1");
  out.legroute_probe.shuffle                    = integer("place.legroute.probe.shuffle", "0");
  out.legroute_probe.strict                     = truthy(synth_set(opts, "place.legroute.strict", "true"));
  out.legroute_plan.enabled                     = truthy(synth_set(opts, "place.legroute.plan", "false"));
  if (out.legroute_plan.enabled) {
    out.placement_session = std::make_shared<livehd::placement::Placement_session>();
  }
  out.legroute_plan.local_tiles    = integer("place.legroute.plan.local_tiles", "0");
  out.legroute_plan.route_model    = synth_set(opts, "place.legroute.plan.route_model", "legacy");
  out.legroute_plan.route_interval = integer("place.legroute.plan.route_interval", "50");
  out.legroute_plan.route_net_pops = integer("place.legroute.plan.route_net_pops", "2000");
  out.legroute_plan.export_coarse  = truthy(synth_set(opts, "place.legroute.plan.export_coarse", "false"));
  if ((out.legroute_plan.route_model != "legacy" && out.legroute_plan.route_model != "coarse") || !out.legroute_plan.route_interval
      || !out.legroute_plan.route_net_pops) {
    throw Lhd_error{"usage", "legroute.plan route_model must be legacy or coarse with positive route budgets", ""};
  }
  out.legroute_plan.handoff = truthy(synth_set(opts, "place.legroute.plan.handoff", "false"));
  out.legroute_plan.rounds  = integer("place.legroute.plan.rounds", "3");
  if (out.legroute_options.critical_cellboxes && out.legroute_plan.rounds > 3) {
    throw Lhd_error{"usage", "critical_cellboxes permits at most three legroute.plan.rounds", ""};
  }
  if (out.legroute_options.cellbox_full_flow
      && (!out.legroute_options.critical_cellboxes || !out.legroute_plan.enabled || !out.legroute || !out.legroute_options.final)) {
    throw Lhd_error{"usage", "cellbox_full_flow requires critical_cellboxes, legroute.plan and legroute.final", ""};
  }
  if (out.legroute_options.cellbox_diagnostic_final && (!out.legroute_options.cellbox_full_flow || !out.legroute_options.final)) {
    throw Lhd_error{"usage", "cellbox_diagnostic_final requires cellbox_full_flow and legroute.final", ""};
  }
  if (out.legroute_options.cellbox_far_fallback
      && (!out.legroute_options.critical_cellboxes || !out.legroute_plan.enabled || out.legroute_plan.rounds != 3)) {
    throw Lhd_error{"usage", "cellbox_far_fallback requires critical_cellboxes and legroute.plan with exactly three rounds", ""};
  }
  out.legroute_plan.iterations         = integer("place.legroute.plan.iterations", "100");
  out.legroute_plan.min_iterations     = integer("place.legroute.plan.min_iterations", "0");
  out.legroute_plan.timing_interval    = integer("place.legroute.plan.timing_interval", "0");
  out.legroute_plan.net_pops           = integer("place.legroute.plan.net_pops", "100000");
  out.legroute_plan.max_inflate        = number("place.legroute.plan.max_inflate", "1.5");
  out.legroute_plan.max_degradation_ps = number("place.legroute.plan.max_degradation_ps", "-1");
  if (!std::isfinite(out.legroute_plan.max_degradation_ps)
      || (out.legroute_plan.max_degradation_ps < 0 && out.legroute_plan.max_degradation_ps != -1)) {
    throw Lhd_error{"usage", "legroute.plan.max_degradation_ps must be -1 (disabled) or finite and nonnegative", ""};
  }
  if (out.legroute_plan.enabled
      && (!out.legroute || out.legroute_probe.enabled || !out.legroute_plan.rounds || !out.legroute_plan.iterations
          || out.legroute_plan.min_iterations > out.legroute_plan.iterations || !out.legroute_plan.net_pops
          || !std::isfinite(out.legroute_plan.max_inflate) || out.legroute_plan.max_inflate <= 1)) {
    throw Lhd_error{"usage", "legroute.plan requires legroute, positive budgets, max_inflate > 1 and probe=false", ""};
  }
  if (out.legroute_plan.enabled
      && ((!out.legroute_options.critical_cellboxes && !out.legroute_options.partition_cells)
          || out.legroute_options.region_pins)) {
    throw Lhd_error{"usage",
                    "legroute.plan requires region_pins=false and partition_cells > 0 outside critical_cellboxes mode",
                    ""};
  }
  if (out.legroute_probe.enabled
      && (!(out.legroute_probe.fill >= 0 && out.legroute_probe.fill < 1)
          || !(out.legroute_probe.target > 0 && out.legroute_probe.target <= 1) || !(out.legroute_probe.max_inflate >= 1)
          || !out.legroute_probe.iterations)) {
    throw Lhd_error{"usage", "invalid synth.place.legroute.probe.* value", ""};
  }
  out.legroute_options.inbox_span     = truthy(synth_set(opts, "place.legroute.inbox_span", "false"));
  // auto (default): the validated box seed whenever the configuration supports it
  out.legroute_options.box_route_seed = box_route_seed == "auto"
                                            ? livehd::placement::legroute_box_route_seed_compatible(out.legroute_options)
                                            : truthy(box_route_seed);
  livehd::placement::validate_legroute_options(out.legroute_options);
  auto& terminal                  = out.terminal_legalize_options;
  terminal.remainder_warp.enabled = truthy(synth_set(opts, "place.legalize.warpspace", "false"));
  terminal.remainder_warp.weight  = number("place.legalize.warpspace.weight", "1");
  terminal.remainder_warp.bins    = integer("place.legalize.warpspace.bins", "16");
  terminal.remainder_warp.seconds = number("place.legalize.warpspace.seconds", "3");
  livehd::placement::validate_warp_options(terminal.remainder_warp);
  terminal.max_density = number("place.legalize.bin.max_density", "0.8");
  if (const auto grid = synth_set(opts, "place.legalize.bin.grid", "route"); grid != "route") {
    out.legalize_bins = integer("place.legalize.bin.grid", grid);
    if (out.legalize_bins < 1 || out.legalize_bins > 1024) {
      throw Lhd_error{"usage", "invalid synth.place.legalize.bin.grid", "expected route or 1..1024 bins per axis"};
    }
  }
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
  out.timing_max_weight      = number("place.timing_max_weight", "2");
  out.timing_min_weight      = number("place.timing_min_weight", "0.5");
  out.timing_spread_weight   = number("place.timing_spread_weight", "0");
  out.macro_timing_interval  = static_cast<int>(integer("place.macro.timing_interval", "50"));
  out.timing_smoothing       = number("place.timing_smoothing", "0.5");
  out.fp_timing_max_updates  = integer("place.fp.timing_max_updates", "10");
  out.stop_overflow          = number("place.stop_overflow", "0.1");
  out.initial_density_weight = number("place.initial_density_weight", "0.01");
  if (out.timing_max_weight < 1 || out.timing_max_weight > livehd::placement::max_net_weight || out.timing_smoothing < 0
      || out.timing_smoothing > 1 || out.fp_timing_max_updates < 2 || out.stop_overflow < 0 || out.stop_overflow > 1
      || out.initial_density_weight <= 0 || out.timing_min_weight <= 0 || out.timing_min_weight > 1 || out.macro_timing_interval < 0 || out.timing_spread_weight < 0
      || out.timing_spread_weight > 1) {
    throw Lhd_error{"usage", "invalid synth.place timing/density tuning controls", "check lhd describe synth for allowed values"};
  }
  out.snapshots = truthy(synth_set(opts, "place.snapshots", "false"));
  out.cohesion  = number("place.cohesion", "0");
  if (out.cohesion < 0) {
    throw Lhd_error{"usage", "synth.place.cohesion must be nonnegative", ""};
  }
  out.utilization         = number("place.utilization", "0.30");
  out.allow_increase_area = truthy(synth_set(opts, "place.allow_increase_area", "true"));
  out.area_restarts       = integer("place.area_restarts", "12");
  out.area_step           = number("place.area_step", "0.92");
  if (!(out.area_step > 0 && out.area_step < 1)) {
    throw Lhd_error{"usage", "synth.place.area_step must be in (0, 1)", ""};
  }
  {
    const auto area = synth_set(opts, "place.area", "fixed");
    if (area != "fixed" && area != "auto") {
      throw Lhd_error{"usage", "synth.place.area must be fixed or auto", ""};
    }
    auto& fit           = out.area_fit;
    fit.enabled         = area == "auto";
    fit.min_fraction    = number("place.area.min_fraction", "0.5");
    fit.max_utilization = number("place.area.max_utilization", "0.9");
    fit.tolerance       = number("place.area.tolerance", "0.03");
    fit.probes          = static_cast<unsigned>(integer("place.area.probes", "4"));
    fit.overflow_slack  = number("place.area.overflow_slack", "0.05");
    fit.overflow_floor  = number("place.area.overflow_floor", "0.001");
    fit.anchor          = synth_set(opts, "place.area.anchor", "io");
    fit.oracle          = synth_set(opts, "place.area.oracle", "legroute");
    try {
      livehd::placement::validate_area_fit_options(fit);
    } catch (const std::invalid_argument& e) {
      throw Lhd_error{"usage", std::string("synth.") + e.what(), ""};
    }
  }
  out.aspect = number("place.aspect", "1");
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
  // nested placement calls (repair, timing trials) start from a previous frame: the user's frame decides core growth
  out.generated_core = out.floorplan.empty() && out.die.empty() && out.core.empty();
  if (out.area_fit.enabled && out.floorplan.empty()) {
    std::fprintf(stderr,
                 "lhd synth: synth.place.area=auto applies to an imported synth.place.floorplan; %s\n",
                 out.generated_core ? "a generated core is sized by synth.place.utilization (0: the routability search)"
                                    : "synth.place.die/core frames place in the whole core");
  }
  out.density = number("place.density", "0.5");
  // utilization 0: the driver searches for the core by bisection (synth_command); this run is its first point
  if (out.utilization <= 0) {
    if (!out.legroute || !out.allow_increase_area || !out.generated_core) {
      throw Lhd_error{"usage",
                      "synth.place.utilization=0 searches for the core: it needs synth.place.legroute=true, "
                      "synth.place.allow_increase_area=true and a generated core",
                      "set a utilization in (0,1) or enable legroute"};
    }
    out.area_search = true;
    out.utilization = number("place.area_search.start", "0.8");
    out.density     = placement_search_density(out.utilization, terminal.max_density);
  } else if (out.density <= 0) {
    out.density
        = out.legroute ? placement_search_density(out.utilization, terminal.max_density) : std::min(1.0, out.utilization + .05);
  }
  out.iterations            = integer("place.iterations", "1500");
  out.min_iterations        = integer("place.min_iterations", "40");
  out.post_density_factor   = number("place.post_density_factor", "4");
  out.convergence_tolerance = number("place.convergence_tolerance", "0.01");
  out.detailed_iterations   = integer("place.detailed_iterations", "2");
  out.macro_detailed_place  = truthy(synth_set(opts, "place.macro.detailed", "true"));
  out.macro_legalize_cells  = truthy(synth_set(opts, "place.macro.legalize_cells", "true"));
  out.handoff_taps          = truthy(synth_set(opts, "place.handoff.taps", "false"));
  out.pg                    = synth_set(opts, "place.pg", "none");
  if (!synth_set(opts, "place.pg.channel.max_ir_drop", "").empty()) {
    out.pg_channel_max_ir_drop_v = number("place.pg.channel.max_ir_drop", "");
    if (*out.pg_channel_max_ir_drop_v < 0) {
      throw Lhd_error{"usage", "synth.place.pg.channel.max_ir_drop must be nonnegative", "units are volts"};
    }
  }
  try {
    out.pg_channels                   = livehd::placement::parse_pg_channels(synth_set(opts, "place.pg.channel.patches", ""));
    out.pg_tolerate_macro_pin_overlap = truthy(synth_set(opts, "place.pg.macro_pin_overlap", "false"));
  } catch (const std::invalid_argument& e) {
    throw Lhd_error{"usage", std::string("invalid synth.place.pg.channel.patches: ") + e.what(), ""};
  }
  out.preroute.enabled         = truthy(synth_set(opts, "place.preroute", "false"));
  out.retain_preroute_design   = out.preroute.enabled;
  out.preroute.setup_margin_ps = number("place.preroute.setup_margin", "20");
  out.preroute.hold_margin_ps  = number("place.preroute.hold_margin", "-1");
  out.preroute.nominal_slew_ps = number("place.preroute.nominal_slew", "20");
  out.preroute.hold_buffer     = synth_set(opts, "place.preroute.hold_buffer", "auto");
  out.preroute.budget_ms       = integer("place.preroute.budget_ms", "10");
  out.preroute.stage_budget_ms = integer("place.preroute.stage_budget_ms", "10000");
  out.preroute.bins            = integer("place.preroute.bins", "64");
  out.preroute.max_rows        = integer("place.preroute.max_rows", "0");
  out.preroute.max_sites       = integer("place.preroute.max_sites", "0");
  if (out.preroute.max_rows < 0 || out.preroute.max_sites < 0) {
    throw Lhd_error{"usage", "invalid pre-route span cap", "max_rows and max_sites must be nonnegative; 0 is unlimited"};
  }
  out.preroute.capacity_fraction = number("place.preroute.capacity", "0.30");
  if (out.preroute.capacity_fraction < 0 || out.preroute.capacity_fraction > 1) {
    throw Lhd_error{"usage", "invalid synth.place.preroute.capacity", "expected a fraction in 0..1"};
  }
  out.final_independent_timing = truthy(synth_set(opts, "place.final.independent_timing", "false"));
  out.final_timing_start       = integer("place.final.timing_start", "200");
  out.final_timing_schedule    = synth_set(opts, "place.final.timing_schedule", "fixed");
  if (out.final_timing_schedule != "fixed" && out.final_timing_schedule != "late") {
    throw Lhd_error{"usage", "synth.place.final.timing_schedule expects fixed or late", ""};
  }
  if (out.final_timing_start < 0) {
    throw Lhd_error{"usage", "synth.place.final.timing_start must be nonnegative", "use a GP iteration count"};
  }
  out.satlegalize       = truthy(synth_set(opts, "place.satlegalize", "false"));
  out.satlegalize_route = truthy(synth_set(opts, "place.satlegalize.route", "false"));
  if (out.satlegalize_route && !out.satlegalize) {
    throw Lhd_error{"usage", "synth.place.satlegalize.route requires synth.place.satlegalize", ""};
  }
  out.satlegalize_pg_after = truthy(synth_set(opts, "place.satlegalize.route.pg_after", "false"));
  if (out.satlegalize_pg_after && !out.satlegalize_route) {
    throw Lhd_error{"usage", "synth.place.satlegalize.route.pg_after requires synth.place.satlegalize.route", ""};
  }
  out.satlegalize_route_vias = truthy(synth_set(opts, "place.satlegalize.route.vias", "false"));
  if (out.satlegalize_route_vias && !out.satlegalize_route) {
    throw Lhd_error{"usage", "synth.place.satlegalize.route.vias requires synth.place.satlegalize.route", ""};
  }
  out.satlegalize_route_critical_fraction = number("place.satlegalize.route.critical_fraction", "0.10");
  if (!std::isfinite(out.satlegalize_route_critical_fraction) || out.satlegalize_route_critical_fraction < 0
      || out.satlegalize_route_critical_fraction > 1) {
    throw Lhd_error{"usage", "invalid synth.place.satlegalize.route.critical_fraction", "expected a fraction in 0..1"};
  }
  out.preroute.enabled       |= out.satlegalize_route;
  out.retain_preroute_design |= out.satlegalize_route;
  auto& sat                   = out.satlegalize_options;
  sat.backend                 = synth_set(opts, "place.satlegalize.backend", "cadical");
  sat.encoding                = synth_set(opts, "place.satlegalize.encoding", "candidate");
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
  const auto sat_grid      = synth_set(opts, "place.satlegalize.bin.grid", "none");
  if (sat_grid != "none" && sat_grid != "route") {
    throw Lhd_error{"usage", "invalid synth.place.satlegalize.bin.grid: " + sat_grid, "expected none or route"};
  }
  sat.route_grid       = sat_grid == "route";
  sat.route_guidance   = truthy(synth_set(opts, "place.satlegalize.route_guidance", "true"));
  sat.route_peak_floor = number("place.satlegalize.route_peak_floor", "0");
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
  // place.macro=h2d is the native hierarchical macro algorithm: the movable-macro GP controls (pin adjustment, alignment,
  // snapping) and the native repulsion experiment controls (macro/density scaling, routability, IO and schedule
  // iterations, overlap, jitter) belong to the plain flow, so under h2d they default off. Turning one on explicitly is
  // still an error (the placement says which one conflicts).
  const bool h2d_macro    = synth_set(opts, "place.macro", "none") == "h2d";
  const auto plain        = [&](const char* on) { return h2d_macro ? "false" : on; };
  out.bins                = integer("place.grid.density", "64");
  // grid.density_mode is one choice; the deprecated booleans it replaces could
  // be combined, so they are still read when it is absent.
  const auto density_mode = synth_set(opts, "place.grid.density_mode", "");
  out.dynamic_bins        = truthy(synth_set(opts, "place.dynamic_bins", "false"));
  out.automatic_bins      = truthy(synth_set(opts, "place.auto_bins", "false"));
  if (!density_mode.empty()) {
    if (density_mode != "fixed" && density_mode != "dynamic" && density_mode != "macro") {
      throw Lhd_error{"usage", "invalid synth.place.grid.density_mode: " + density_mode, "expected fixed, dynamic or macro"};
    }
    if (out.dynamic_bins || out.automatic_bins) {
      throw Lhd_error{"usage",
                      "synth.place.grid.density_mode conflicts with deprecated synth.place.dynamic_bins/auto_bins",
                      "set only synth.place.grid.density_mode"};
    }
    out.dynamic_bins   = density_mode == "dynamic";
    out.automatic_bins = density_mode == "macro";
  }
  if (const auto route = synth_set(opts, "place.grid.route", "inherit"); route != "inherit") {
    out.route_bins = integer("place.grid.route", route);
    if (out.route_bins < 2 || out.route_bins > 1024) {
      throw Lhd_error{"usage", "invalid synth.place.grid.route", "expected inherit or 2..1024 bins per axis"};
    }
  }
  out.macro_fill_cell_space       = truthy(synth_set(opts, "place.macro.fill_cell_space", "false"));
  out.macro_filler_fraction       = number("place.macro.filler_fraction", "0");
  out.virtual_fillers             = truthy(synth_set(opts, "place.virtual_fillers", "true"));
  out.virtual_filler_max_count    = integer("place.virtual_filler_max_count", "0");
  out.virtual_filler_fraction     = number("place.virtual_filler_fraction", "1");
  out.virtual_filler_cell_scale   = number("place.virtual_filler_cell_scale", "2");
  out.virtual_filler_bin_fraction = number("place.virtual_filler_bin_fraction", "0.25");
  out.bin_max                     = integer("place.grid.density_max", "512");
  out.local_density_weight        = number("place.local_density_weight", "0");
  out.local_density_target        = number("place.local_density_target", "-1");
  out.local_density_bins          = integer("place.grid.local", "512");
  out.local_density_range         = number("place.local_density_range", "4");
  out.local_density_jitter        = number("place.local_density_jitter", "0.01");
  out.macro_cell_density_weight   = number("place.macro.cell_density_weight", "0");
  out.macro_cell_density_reach    = number("place.macro.cell_density_reach", "2");
  out.macro_density_pull_weight   = number("place.macro.density_pull_weight", "0");
  out.macro_density_pull_range    = number("place.macro.density_pull_range", "4");
  out.scale_macro_density         = truthy(synth_set(opts, "place.scale_macro_density", plain("true")));
  out.exact_macro_density         = truthy(synth_set(opts, "place.exact_macro_density", plain("true")));
  out.adaptive_density            = truthy(synth_set(opts, "place.adaptive_density", plain("true")));
  const auto unknown_const        = synth_set(opts, "place.unknown_const", "error");
  if (unknown_const != "error" && unknown_const != "zero") {
    throw Lhd_error{"usage", "synth.place.unknown_const must be error or zero", ""};
  }
  out.unknown_const_zero     = unknown_const == "zero";
  out.preserve_imported_ties = truthy(synth_set(opts, "place.preserve_imported_ties", "false"));
  out.routability            = truthy(synth_set(opts, "place.routability", plain("true")));
  out.route_model            = synth_set(opts, "place.route.model", "rsmt");
  out.route_degree           = integer("place.route.degree", "40");
  out.route_accuracy         = integer("place.route.accuracy", "3");
  out.uroute.dir             = synth_set(opts, "place.uroute.dir", "@pretrained");
  out.uroute.export_features = truthy(synth_set(opts, "place.uroute.export", "true"));
  out.uroute.shots           = integer("place.uroute.shots", "5");
  out.uroute.min_shots       = integer("place.uroute.min_shots", "1");
  out.uroute.fallback        = synth_set(opts, "place.uroute.fallback", "nearest");
  out.uroute.label_policy    = synth_set(opts, "place.uroute.label_policy", "detailed");
  out.uroute.drc_level       = number("place.uroute.drc_level", "0");
  try {
    livehd::placement::validate_uroute_options(out.uroute);
  } catch (const std::invalid_argument& e) {
    throw Lhd_error{"usage", e.what(), "check synth.place.uroute controls"};
  }
  const auto read_route = [&](livehd::placement::Stage_route& stage, const std::string& name) {
    const auto prefix            = "place." + name + ".route.";
    stage.model                  = synth_set(opts, prefix + "model", "inherit");
    stage.policy.switch_overflow = number(prefix + "switch", "0.05");
    stage.policy.target_overflow = number(prefix + "target", "0.000000001");
    stage.policy.hold            = integer(prefix + "hold", "1");
    const auto probe             = synth_set(opts, prefix + "probe", "false");
    if (probe != "false" && probe != "true" && probe != "0" && probe != "1" && probe != "rsmt" && probe != "uroute") {
      throw Lhd_error{"usage", "invalid synth." + prefix + "probe", "use false, true, rsmt or uroute"};
    }
    stage.policy.probe       = probe == "true" || probe == "1" || probe == "rsmt" || probe == "uroute";
    stage.policy.probe_model = probe == "uroute" ? DreamPlace::native::RouteModel::uroute : DreamPlace::native::RouteModel::rsmt;
    if (name == "final") {
      stage.policy.uroute_switch_overflow = number(prefix + "uroute_switch", "0.01");
      stage.policy.uroute_hold            = integer(prefix + "uroute_hold", "2");
    }
    stage.policy.probe_iterations = integer(prefix + "probe_iterations", "100");
    const auto model              = stage.model == "inherit" ? out.route_model : stage.model;
    if ((model != "off" && model != "rudy" && model != "rsmt" && model != "auto" && model != "uroute")
        || stage.policy.switch_overflow < stage.policy.target_overflow || stage.policy.target_overflow < 0 || stage.policy.hold < 1
        || stage.policy.hold > 1000 || stage.policy.probe_iterations < 1 || stage.policy.probe_iterations > 10000
        || stage.policy.uroute_hold < 1 || stage.policy.uroute_hold > 1000 || stage.policy.uroute_switch_overflow < 0
        || (name == "final" && model == "auto" && stage.policy.uroute_switch_overflow > stage.policy.switch_overflow)) {
      throw Lhd_error{"usage",
                      "invalid synth." + prefix + " controls",
                      "target must be <= switch; model off/rudy/rsmt/uroute/auto; uroute_switch <= switch"};
    }
  };
  if (out.route_model != "off" && out.route_model != "rudy" && out.route_model != "rsmt" && out.route_model != "auto"
      && out.route_model != "uroute") {
    throw Lhd_error{"usage", "invalid synth.place.route.model", "use off, rudy, rsmt, uroute or auto"};
  }
  read_route(out.fp_route, "fp");
  read_route(out.macro_route, "macro");
  read_route(out.final_route, "final");
  bool uses_flute = false;
  for (const auto* stage : {&out.fp_route, &out.macro_route, &out.final_route}) {
    const auto model  = stage->model == "inherit" ? out.route_model : stage->model;
    uses_flute       |= model == "rsmt" || model == "uroute" || model == "auto" || (model != "off" && stage->policy.probe);
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
  out.macro_overlap_weight = number("place.macro_overlap", h2d_macro ? "0" : "50");
  out.macro_jitter         = number("place.macro_jitter", h2d_macro ? "0" : "0.01");
  out.io_iterations        = integer("place.io_iterations", h2d_macro ? "0" : "500");
  out.schedule_iterations  = integer("place.schedule_iterations", h2d_macro ? "0" : "500");
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
  const auto channel_text = synth_set(opts, "place.macro.channel", "auto");
  if (channel_text != "auto") {
    out.macro_channel = parse_halo(channel_text, "place.macro.channel");
  }
  out.macro_pdn = synth_set(opts, "place.macro.pdn", "auto");
  if (out.macro_pdn != "auto" && out.macro_pdn != "align" && out.macro_pdn != "gap" && out.macro_pdn != "off") {
    throw Lhd_error{"usage", "invalid synth.place.macro.pdn", "expected auto, align, gap or off"};
  }
  if (const auto origin_text = synth_set(opts, "place.macro.pdn_origin", "core"); origin_text != "core") {
    char*      end   = nullptr;
    const auto x     = std::strtod(origin_text.c_str(), &end);
    const bool comma = end != origin_text.c_str() && *end == ',';
    const auto y     = comma ? std::strtod(end + 1, &end) : 0.0;
    if (!comma || *end != '\0' || !std::isfinite(x) || !std::isfinite(y)) {
      throw Lhd_error{"usage", "invalid synth.place.macro.pdn_origin", "expected core or X,Y microns"};
    }
    out.macro_pdn_origin = std::pair{x, y};
  }
  const auto edge_snap_text = synth_set(opts, "place.macro.edge_snap", "off");
  if (edge_snap_text == "auto") {
    out.macro_edge_snap = std::pair{-1.0, -1.0};
  } else if (edge_snap_text == "off") {
    out.macro_edge_snap.reset();
  } else {
    out.macro_edge_snap = parse_halo(edge_snap_text, "place.macro.edge_snap");
  }
  out.macro_dead_space = livehd::placement::parse_dead_space_options(synth_set(opts, "place.macro.dead_space", "off"));
  for (const auto& [key, fallback, target] : {
           std::tuple{    "place.macro.refine_rounds",  "0",     &out.macro_refine_rounds},
           std::tuple{"place.macro.refine_iterations", "30", &out.macro_refine_iterations}
  }) {
    const auto text         = synth_set(opts, key, fallback);
    unsigned   value        = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
      throw Lhd_error{"usage", std::string("synth.") + key + " expects a nonnegative integer", ""};
    }
    *target = value;
  }
  const auto cell_halo = synth_set(opts, "place.cell_halo", "");
  if (!cell_halo.empty()) {
    out.cell_halo = parse_halo(cell_halo, "place.cell_halo");
    if ((out.halo_x >= 0 && out.cell_halo->first > out.halo_x) || (out.halo_y >= 0 && out.cell_halo->second > out.halo_y)) {
      throw Lhd_error{"usage", "invalid synth.place.cell_halo", "each axis must not exceed synth.place.halo"};
    }
  }
  const auto sat_region = synth_set(opts, "place.satlegalize.bin.size_um", "");
  if (!sat_region.empty()) {
    const auto dimensions = parse_halo(sat_region, "place.satlegalize.bin.size_um");
    sat.region_width_um   = dimensions.first;
    sat.region_height_um  = dimensions.second;
    if (sat.region_width_um <= 0 || sat.region_height_um <= 0) {
      throw Lhd_error{"usage", "invalid synth.place.satlegalize.bin.size_um", "expected positive width,height in microns"};
    }
  }
  if (sat.route_grid
      && (!sat_region.empty() || !out.routability || out.automatic_bins
          || livehd::placement::resolve_route_policy(out, out.final_route).model != DreamPlace::native::RouteModel::rsmt)) {
    throw Lhd_error{"usage",
                    "invalid synth.place.satlegalize.bin.grid=route",
                    "requires explicit final rsmt, routability, a non-macro density grid and no bin.size_um"};
  }
  if (sat.spill_neighbors && sat_region.empty() && !sat.route_grid) {
    throw Lhd_error{"usage",
                    "synth.place.satlegalize.spill_neighbors requires fixed SAT bins",
                    "set synth.place.satlegalize.bin.size_um or synth.place.satlegalize.bin.grid=route"};
  }
  out.final_cells           = truthy(synth_set(opts, "place.final", "true"));
  out.final_iterations      = integer("place.final.iterations", "0");
  const auto fp_mode        = synth_set(opts, "place.fp", "auto");
  if (fp_mode != "true" && fp_mode != "false" && fp_mode != "1" && fp_mode != "0" && fp_mode != "on" && fp_mode != "off"
      && fp_mode != "auto") {
    throw Lhd_error{"usage", "invalid synth.place.fp: " + fp_mode, "expected true, false, on, off or auto"};
  }
  out.fp_auto         = fp_mode == "auto";
  out.fp_place        = !out.fp_auto && truthy(fp_mode);
  out.fp_auto_options = {.share          = number("place.fp.auto_share", "0.2"),
                         .min_copies     = static_cast<int>(integer("place.fp.auto_min_copies", "2")),
                         .min_copy_share = number("place.fp.auto_min_copy_share", "0.02"),
                         .tolerance      = number("place.fp.auto_tolerance", "0.05"),
                         .candidates     = truthy(synth_set(opts, "place.fp.auto_candidates", "true"))};
  try {
    out.fp_auto_options.validate();
  } catch (const std::invalid_argument& e) {
    throw Lhd_error{"usage", "invalid synth.place.fp.auto_* controls", e.what()};
  }
  out.fp_snap_enabled       = truthy(synth_set(opts, "place.fp.snap", "true"));
  out.fp_colors             = truthy(synth_set(opts, "place.fp.colors", "false"));
  out.fp_color_source       = synth_set(opts, "place.fp.color_source", "synthesis");
  out.fp_color_minimum      = number("place.fp.color_minimum", "-1");
  out.fp_color_maximum      = number("place.fp.color_maximum", "30000");
  out.fp_pin_adjust         = truthy(synth_set(opts, "place.fp.pin_adjust", "true"));
  out.fp_pin_interval       = integer("place.fp.pin_interval", "50");
  out.fp_pin_step           = number("place.fp.pin_step", "0.01");
  out.fp_pin_improvement    = number("place.fp.pin_improvement", "0.001");
  out.macro_pin_adjust      = truthy(synth_set(opts, "place.macro.pin_adjust", h2d_macro ? "false" : "true"));
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
  out.gp_resize        = truthy(synth_set(opts, "place.resize.gp", "true"));
  out.gp_buffer_trials = integer("place.resize.gp_trials", "4");
  if (out.gp_buffer_trials < 1 || out.gp_buffer_trials > 16) {
    throw Lhd_error{"usage", "synth.place.resize.gp_trials expects an integer in [1,16]", ""};
  }
  out.gp_buffer_margin = number("place.resize.gp_margin", "0.01");
  if (!std::isfinite(out.gp_buffer_margin) || out.gp_buffer_margin < 0 || out.gp_buffer_margin > 1) {
    throw Lhd_error{"usage", "synth.place.resize.gp_margin expects a fraction in [0,1]", ""};
  }
  out.eval_budget     = static_cast<unsigned>(std::max<int64_t>(4, integer("place.eval_budget", "6")));
  out.growth_fraction = number("place.growth", "0.05");
  out.growth_sizing_share = number("place.growth.sizing_share", "0.3");
  {
    const auto iterations = integer("place.repair.iterations", "3");
    if (iterations < 0 || iterations > 8) {
      throw Lhd_error{"usage", "synth.place.repair.iterations expects an integer in [0,8]", ""};
    }
    out.repair_iterations = static_cast<unsigned>(iterations);
  }
  if (!std::isfinite(out.growth_sizing_share) || out.growth_sizing_share <= 0 || out.growth_sizing_share > 1) {
    throw Lhd_error{"usage", "synth.place.growth.sizing_share expects a fraction in (0,1]", ""};
  }
  if (!std::isfinite(out.growth_fraction) || out.growth_fraction < 0 || out.growth_fraction > 4) {
    throw Lhd_error{"usage", "synth.place.growth expects a fraction in [0,4]", ""};
  }
  out.resize_vt_swap = truthy(synth_set(opts, "place.resize.vt_swap", "false"));
  for (const auto& pattern : absl::StrSplit(synth_set(opts, "place.resize.dont_use", ""), ',', absl::SkipWhitespace())) {
    out.resize_dont_use.emplace_back(pattern);
  }
  {
    const auto fanout = integer("place.reset_fanout", "32");
    if (fanout < 0 || fanout == 1) {
      throw Lhd_error{"usage",
                      std::format("invalid synth.place.reset_fanout: {}", fanout),
                      "expected 0 (off) or an integer >= 2: loads per reset buffer"};
    }
    out.reset_fanout = static_cast<unsigned>(fanout);
    out.pcrs         = truthy(synth_set(opts, "place.pcrs", "true"));
    out.pcrs_depth   = std::make_shared<livehd::pcrs::Depth_state>();
    const auto rails = integer("place.domino_rail_fanout", "32");
    if (rails < 0 || rails == 1) {
      throw Lhd_error{"usage",
                      std::format("invalid synth.place.domino_rail_fanout: {}", rails),
                      "expected 0 (off) or an integer >= 2: loads per buffer"};
    }
    out.domino_rail_fanout = static_cast<unsigned>(rails);
  }
  out.control_ports = synth_set(opts, "place.control_ports", "");
  if (out.repair_reset) {
    throw Lhd_error{"usage", "pre-CTS clock/reset sizing is disabled", "use place.repair.reset=false"};
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
    throw Lhd_error{"usage",
                    "synth.place.fp.min_macros must be at least 0",
                    "a candidate needs more than this many movable macros"};
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
  out.macro_align_weight               = number("place.macro.align_weight", out.macro_mode == "h2d" ? "0" : "25");
  out.macro_align_sigma                = number("place.macro.align_sigma", "0.25");
  out.macro_align_overlap_boost        = number("place.macro.align_overlap_boost", "0");
  out.macro_align_start                = integer("place.macro.align_start", "100");
  out.macro_align_full                 = integer("place.macro.align_full", "500");
  out.macro_align_nearest              = truthy(synth_set(opts, "place.macro.align_nearest", "false"));
  out.macro_align_global               = truthy(synth_set(opts, "place.macro.align_global", "true"));
  out.macro_magnet.enabled             = truthy(synth_set(opts, "place.macro.magnet", "false"));
  out.macro_magnet.before_snap         = truthy(synth_set(opts, "place.macro.magnet_before_snap", "false"));
  out.macro_magnet.start_displacement  = number("place.macro.magnet_start_displacement", "0.10");
  out.macro_magnet.settle_window       = integer("place.macro.magnet_settle_window", "10");
  out.macro_magnet.settle_displacement = number("place.macro.magnet_settle_displacement", "0.05");
  out.macro_magnet.max_gap             = number("place.macro.magnet_max_gap", "1");
  out.macro_magnet.density_fraction    = number("place.macro.magnet_density_fraction", "0.25");
  out.macro_magnet.max_per_round       = integer("place.macro.magnet_max_per_round", "0");
  out.macro_magnet.max_rounds          = integer("place.macro.magnet_max_rounds", "0");
  try {
    out.macro_magnet.validate();
  } catch (const std::invalid_argument& error) {
    throw Lhd_error{"usage", error.what(), "check synth.place.macro.magnet_* controls"};
  }
  if (out.macro_magnet.enabled && (out.macro_mode != "none" || !out.full)) {
    throw Lhd_error{"usage",
                    "macro magnet requires native full placement",
                    "use synth.place.macro=none; final=false stops after macro_plan"};
  }
  out.macro_snap                             = truthy(synth_set(opts, "place.macro.snap", out.full && out.macro_mode != "h2d" ? "true" : "false"));
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
    throw Lhd_error{"usage",
                    "macro alignment/snap/pin adjustment requires synth.place.macro=none",
                    "they are off by default under place.macro=h2d; do not set place.macro.align_weight>0, place.macro.snap=true or "
                    "place.macro.pin_adjust=true with it"};
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
  out.rc_pessimism = number("place.rc_pessimism", "1");
  if (!std::isfinite(out.rc_pessimism) || out.rc_pessimism < 1) {
    throw Lhd_error{"usage", "invalid synth.place.rc_pessimism", "expected a finite multiplier >=1"};
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

int parse_macro_anchor_dead_space_refreshes(std::string_view text) {
  int value               = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size() || value < -1 || value > 1000000) {
    throw Lhd_error{"usage", "invalid synth.place.macro.anchor.dead_space_refreshes", "expected an integer in [-1,1000000]"};
  }
  return value;
}

std::vector<double> parse_macro_anchor_milestones(std::string_view text) {
  std::vector<double> values;
  if (text.empty()) {
    return values;
  }
  for (size_t start = 0;;) {
    const auto        end = std::min(text.find(',', start), text.size());
    const std::string item(text.substr(start, end - start));
    char*             parsed_end = nullptr;
    const double      value      = std::strtod(item.c_str(), &parsed_end);
    if (parsed_end == item.c_str() || parsed_end != item.c_str() + item.size() || !std::isfinite(value)) {
      throw Lhd_error{"usage", "invalid synth.place.macro.anchor.milestones", "expected comma-separated finite overflows"};
    }
    values.push_back(value);
    if (end == text.size()) {
      return values;
    }
    start = end + 1;
  }
}

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

namespace {
void synth_command_once(Options& opts, Result& res, const std::shared_ptr<livehd::placement::Placement_session>& placement_session,
                        bool placement_area_resume);
}

// legroute can ask for a larger core from any placement phase (the probe, or the final placement when a region has
// no routable answer): the whole command runs again at another utilization (synthesis reuses its ABC cache).
// With a utilization given, each restart lowers it as legroute asks (the push-back), at most area_restarts times.
// With synth.place.utilization=0 the utilization is searched by bisection: the first point is area_search.start; a
// routable placement raises the lower bound and the next point is the middle of what is left above it, a failure
// lowers the upper bound and the next point is the middle below; it stops after area_restarts placements or when
// the bounds are area_search.tolerance apart, and the placement kept is routable (the best point placed once more
// when the last try failed).
static void synth_command_flow(Options& opts, Result& res) {
  const Result   start                 = res;
  const double   base_cells            = std::stod(synth_set(opts, "place.region.max_cells", "10000"));  // the mesh as asked
  const unsigned limit                 = unsigned(std::max(0.0, std::stod(synth_set(opts, "place.area_restarts", "12"))));
  const bool     search                = std::stod(synth_set(opts, "place.utilization", "0.30")) <= 0;
  // Monotonic area/mesh retries continue the same physical optimizer. The
  // bisection search may shrink the core and intentionally keeps independent solves.
  const auto     placement_session     = !search && truthy(synth_set(opts, "place.legroute.plan", "false"))
                                             ? std::make_shared<livehd::placement::Placement_session>()
                                             : nullptr;
  bool           placement_area_resume = false;
  double         lo                    = search ? std::stod(synth_set(opts, "place.area_search.min", "0.4")) : 0;
  double         hi   = search ? std::clamp(std::stod(synth_set(opts, "place.area_search.max", "0.99")), lo, .99) : 1;
  double         cur  = search ? std::clamp(std::stod(synth_set(opts, "place.area_search.start", "0.8")), lo, hi) : 0;
  // the search remembers where it landed (workdir/place_search.txt): the next search starts there, bracketed by
  // a tenth, unless a start is given; an incremental run on the same workdir then confirms the core in two or
  // three placements instead of six
  const auto     memo = fs::path(opts.workdir) / "place_search.txt";
  if (search && !opts.workdir.empty() && fs::exists(memo)
      && std::ranges::none_of(opts.sets, [](const auto& kv) { return kv.first == "synth.place.area_search.start"; })) {
    std::ifstream in(memo);
    std::string   key;
    double        value = 0;
    while (in >> key >> value) {
      if (key == "utilization" && value > 0 && value < 1) {
        cur = std::clamp(value, lo, hi);
        lo  = std::max(lo, cur - .1);
        hi  = std::min(hi, cur + .1);
        std::fprintf(stderr, "lhd synth: core search starts from the remembered utilization %.4f (%s)\n", cur, memo.c_str());
      }
    }
  }
  const double tolerance   = std::stod(synth_set(opts, "place.area_search.tolerance", "0.02"));
  bool         found       = false;                                               // a routable placement exists at lo
  const double given       = std::stod(synth_set(opts, "place.density", "0.5"));  // the GP density as asked (0: follows)
  const double density_cap = std::stod(synth_set(opts, "place.legalize.bin.max_density", "0.8"));
  if (search) {
    opts.sets.emplace_back("synth.place.area_search", "true");
  }
  // the next placement: its utilization, the GP target density following it, and the region mesh variant
  const auto set_point = [&](double u, unsigned variant, const char* why) {
    res                  = start;
    const double density = given > 0 && !search ? std::min(given, u + .05) : placement_search_density(u, density_cap);
    const double scale   = variant == 1 ? 1.25 : variant == 2 ? .8 : 1.0;
    opts.sets.emplace_back("synth.place.utilization", std::format("{:.4f}", u));
    // Four decimal places can round a computed target back up to the hard cap.
    opts.sets.emplace_back("synth.place.density", std::format("{:.17g}", density));
    opts.sets.emplace_back("synth.place.region.max_cells", std::format("{}", std::lround(base_cells * scale)));
    opts.sets.emplace_back("synth.place.mesh_variant", std::format("{}", variant));
    std::fprintf(stderr,
                 "lhd synth: %s: synth.place.utilization=%.4f density=%.17g region.max_cells=%ld (mesh variant %u)\n",
                 why,
                 u,
                 density,
                 std::lround(base_cells * scale),
                 variant);
  };
  for (unsigned restarts = 0;; ++restarts) {
    bool ok = false;
    try {
      synth_command_once(opts, res, placement_session, placement_area_resume);
      ok = true;
    } catch (const livehd::placement::Placement_area_restart& restart) {
      if (!search) {  // the push-back: the utilization legroute asks for, after the mesh variants
        if (restarts >= limit) {
          throw Lhd_error{"unsupported",
                          std::format("not routable: legroute asked for a larger core after {} restarts", restarts),
                          "lower synth.place.utilization, raise synth.place.area_restarts or relax the timing target"};
        }
        if (placement_session) {
          std::fprintf(stderr,
                       "lhd synth: area retry %u retains placement session: cold_starts=%u spent_iterations=%llu "
                       "selected_iteration=%d\n",
                       restarts + 1,
                       placement_session->cold_starts(),
                       placement_session->spent_iterations(),
                       placement_session->iteration());
          if (!opts.workdir.empty()) {
            placement_session->write((fs::path(opts.workdir) / std::format("placement_area_retry{}.json", restarts + 1)).string());
          }
        }
        placement_area_resume = true;
        set_point(restart.utilization, restart.mesh_variant, "restarting for routability");
        continue;
      }
      if (restart.mesh_variant > 0) {  // the same point with another region mesh
        set_point(cur, restart.mesh_variant, "the same point with another region mesh");
        continue;
      }
      hi = std::min(hi, cur);  // a failed point
    }
    if (!search) {
      return;
    }
    if (ok) {
      lo    = std::max(lo, cur);
      found = true;
    }
    std::fprintf(stderr,
                 "lhd synth: core search: utilization %.4f %s; routable up to %.4f, fails from %.4f\n",
                 cur,
                 ok ? "routable" : "not routable",
                 found ? lo : 0.0,
                 hi);
    const auto remember = [&] {  // where the search landed, for the next run on this workdir
      if (!opts.workdir.empty()) {
        std::ofstream out(memo);
        out << "utilization " << std::format("{:.4f}", lo) << "\ndensity "
            << std::format("{:.17g}", placement_search_density(lo, density_cap)) << '\n';
      }
    };
    if (restarts + 1 >= limit || hi - lo <= tolerance) {
      if (ok) {
        remember();
        return;  // the placement kept is this routable one
      }
      if (!found) {
        throw Lhd_error{
            "unsupported",
            std::format("not routable: no utilization between {:.3f} and {:.3f} routes within {} placements", lo, hi, limit),
            "lower synth.place.area_search.min, raise synth.place.area_restarts or relax the timing target"};
      }
      set_point(lo, 0, "core search: the best routable point placed once more");  // the last try failed
      synth_command_once(opts, res, placement_session, placement_area_resume);
      remember();
      return;
    }
    cur = std::clamp((lo + hi) / 2, lo, hi);
    set_point(cur, 0, ok ? "core search: denser" : "core search: sparser");
  }
}

// ---- incremental API: record, replay, and "already done" ---------------------
//
// With a --workdir, `<workdir>/synth/manifest.json` records the invocation (so
// `lhd synth` run inside that directory replays it) and the finished run's
// `synth` entry: a fingerprint of everything the result depends on plus the qor
// report. A rerun whose fingerprint still matches and whose artifacts are on
// disk does NO work and reports the stored qor again (`--stats` expands it).
// Only a replay takes that shortcut: the same full command typed again still
// runs the flow and reports its incremental tiers' hits.
// Reuse stays a speedup, never an oracle: anything the fingerprint cannot see
// (Verilog read through `--` flags/filelists) or that the caller asked to be
// written elsewhere (--emit/--emit-dir) always runs the flow.
namespace {
std::string synth_fingerprint(const Options& opts, const Result& res) {
  if (res.run_id.empty() || !opts.raw_args.empty() || opts.workdir.empty()) {
    return {};
  }
  // Display-only switches (`--stats`) must not make a finished run look new:
  // re-showing the stats is exactly what a replay is for.
  Options base = opts;
  std::erase_if(base.sets, [](const auto& kv) { return kv.first == "lhd.stats"; });
  std::string buf = compute_run_id(base);
  buf += std::format("|salts:{:x}:{:x}:{:x}:{:x}:{:x}",
                     livehd::kCompileSrcSalt,
                     livehd::abc::kAbcSrcSalt,
                     livehd::opentimer::kStaSrcSalt,
                     kPhysicalSrcSalt,
                     livehd::domplace::timing::kStageTimingSalt);
  // Every file named by an option (liberty, sdc, ...) is an input whose CONTENT
  // matters; run_id only sees the option text.
  std::vector<std::pair<std::string, std::string>> sets = opts.sets;
  std::ranges::sort(sets);
  for (const auto& [k, v] : sets) {
    std::error_code ec;
    if (!v.empty() && fs::is_regular_file(v, ec)) {
      const auto bytes = livehd::file_utils::read_file(v).value_or("");
      buf += std::format("|file:{}={:016x}", k, lh::woothash64(bytes.data(), bytes.size(), 1021));
    }
  }
  return std::format("{:016x}", lh::woothash64(buf.data(), buf.size(), 1021));
}
}  // namespace

void synth_command(Options& opts, Result& res) {
  const std::string fingerprint = synth_fingerprint(opts, res);
  std::string       root;
  if (!opts.workdir.empty()) {
    std::error_code ec;
    root = fs::absolute(opts.workdir, ec).string() + "/synth";
  }
  if (opts.replayed && !fingerprint.empty() && opts.incremental && opts.emits.empty() && opts.emit_dirs.empty()) {
    std::error_code ec;
    if (const auto entry = manifest_get(root, "synth"); entry && fs::exists(root + "/qor.json", ec) && fs::exists(root + "/net", ec)) {
      rapidjson::Document doc;
      doc.Parse(entry->c_str());
      if (!doc.HasParseError() && doc.IsObject() && doc.HasMember("fingerprint") && doc["fingerprint"].IsString()
          && fingerprint == doc["fingerprint"].GetString() && doc.HasMember("qor") && doc["qor"].IsObject()) {
        rapidjson::StringBuffer                    sb;
        rapidjson::Writer<rapidjson::StringBuffer> w(sb);
        doc["qor"].Accept(w);
        res.qor_json = std::string{sb.GetString(), sb.GetSize()};
        res.recipe_steps.push_back(std::format("synth: up to date, nothing re-run ({})", root));
        return;
      }
    }
  }
  synth_command_flow(opts, res);
  if (!root.empty() && !fingerprint.empty() && !livehd::diag::sink().has_errors() && !res.qor_json.empty()) {
    rapidjson::StringBuffer                    sb;
    rapidjson::Writer<rapidjson::StringBuffer> w(sb);
    w.StartObject();
    w.Key("fingerprint");
    w.String(fingerprint.c_str());
    w.Key("qor");
    w.RawValue(res.qor_json.data(), res.qor_json.size(), rapidjson::kObjectType);
    w.EndObject();
    manifest_put(root, "synth", std::string_view{sb.GetString(), sb.GetSize()});
    record_invocation(opts, "synth", opts.workdir, root);
  }
}

namespace {
// A placement the legalizer cannot close (DREAMPlace: insufficient row space) on a generated core with legroute is
// too dense for the core: placement runs again at a lower utilization, like a region without a routable answer.
bool density_failure(const std::exception& e, const livehd::placement::Options& po) {
  return po.legroute && po.allow_increase_area && po.generated_core
         && std::string_view(e.what()).find("legalization failed") != std::string_view::npos;
}
void synth_command_once(Options& opts, Result& res, const std::shared_ptr<livehd::placement::Placement_session>& placement_session,
                        bool placement_area_resume) {
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
  if (run_place && place_options.legroute_plan.enabled && placement_session) {
    place_options.placement_session     = placement_session;
    place_options.placement_area_resume = placement_area_resume;
  }
  const bool run_resynth  = truthy(synth_set(opts, "place.resynth", "false"));
  // 2i-interact: the interactive session serves the whole run and keeps the process alive afterwards.
  std::shared_ptr<livehd::placement::gui::Session> gui_session;
  std::string                                      gui_video_out;
  const std::string gui_replay = synth_set(opts, "place.gui.replay", "");
  if (opts.gui || !gui_replay.empty()) {
    if (!run_place) {
      throw Lhd_error{"usage", "--gui needs placement", "add --set synth.place=true"};
    }
    livehd::placement::gui::Config gc;
    const bool resuming    = !opts.gui_replay.empty();  // after "replay from a checkpoint": replay, then interactive
    const bool pure_replay = !gui_replay.empty() && !resuming;
    gc.replay_path  = resuming ? opts.gui_replay : gui_replay;
    if (resuming) {
      const auto colon = opts.gui_resume.rfind(':');
      gc.resume_label  = opts.gui_resume.substr(0, colon);
      gc.resume_nth    = colon == std::string::npos ? 1 : std::atoi(opts.gui_resume.c_str() + colon + 1);
      gc.replay_limit_seq = opts.gui_replay_seq;
    }
    if (!pure_replay) {  // a local agent (an assistant, a script) talks to the page's session through this Unix socket
      auto socket_path = (std::filesystem::temp_directory_path() / std::format("lhd-gui-{}.sock", int(::getpid()))).string();
      if (socket_path.size() > 90) {
        socket_path = std::format("/tmp/lhd-gui-{}.sock", int(::getpid()));  // sun_path is short (104 bytes on macOS)
      }
      gc.agent_socket = socket_path;
    }
    gc.token = opts.gui_token;  // a restart keeps the page's URL (and so its token); empty: a fresh random one
    const std::string gui_video = synth_set(opts, "place.gui.video", "");
    if (!gui_video.empty() && !gui_replay.empty()) {
      gc.video_dir = gui_video.ends_with(".mp4") ? gui_video + ".frames" : gui_video;
    }
    gui_video_out = gui_video.ends_with(".mp4") ? gui_video : std::string{};
    if (!opts.workdir.empty()) {
      gc.shot_dir = opts.workdir + "/synth/place/gui";
    }
    gc.port         = opts.gui_port;
    gc.open_browser = !opts.gui_no_open;
    gc.poll         = std::max(1, std::min(20, std::atoi(synth_set(opts, "place.gui.poll", "10").c_str())));
    gc.min_iters    = std::max(1, std::atoi(synth_set(opts, "place.gui.min_iters", "20").c_str()));
    gc.history_every = std::max(1, std::atoi(synth_set(opts, "place.gui.history_every", "50").c_str()));
    gc.history_mb    = std::max(1, std::atoi(synth_set(opts, "place.gui.history_mb", "256").c_str()));
    gc.timing_budget = std::clamp(std::atoi(synth_set(opts, "place.gui.timing_budget", "10").c_str()), 0, 100);
    gc.trace_path   = pure_replay ? std::string{} : !opts.gui_trace.empty() ? opts.gui_trace : opts.workdir.empty() ? std::string{} : opts.workdir + "/synth/place/gui_trace.jsonl";
    if (!gc.trace_path.empty()) {
      std::filesystem::create_directories(std::filesystem::path(gc.trace_path).parent_path());
    }
    // Fork a region: the placer wrote its netlist, floorplan and seed positions; start a new `lhd synth --gui` on them
    // (a child process with its own page, port and token, free to run with other flags) and hand back its page.
    if (!opts.workdir.empty()) {
      gc.fork_dir = opts.workdir + "/synth/place/forks";
      gc.fork     = [&opts](const livehd::placement::gui::Fork_files& files, const livehd::placement::gui::Fork_request& req)
          -> livehd::placement::gui::Fork_launch {
        namespace fs = std::filesystem;
        livehd::placement::gui::Fork_launch out;
        const auto bare = [](std::string k) { return k.starts_with("synth.") ? k.substr(6) : k; };
        // What the child must not inherit: the frame (the region has its own), the parent's GUI session and output files.
        static const std::set<std::string> own{"place.die", "place.core", "place.floorplan", "place.seed", "place.utilization",
                                                "place.aspect", "place.gui.replay", "place.gui.video", "place.gui.poll"};
        std::vector<std::string> args;
        args.push_back(opts.invocation_argv.empty() ? std::string{"lhd"} : opts.invocation_argv[0]);
        args.insert(args.end(), {"synth", "lg:" + files.net_dir, "--top", files.top, "--workdir", req.dir + "/W", "--gui", "--gui-no-open"});
        for (const auto& [key, value] : opts.sets) {
          if (!own.contains(bare(key))) {
            args.push_back("--set");
            args.push_back(key + "=" + value);
          }
        }
        args.insert(args.end(), {"--set", "synth.place.floorplan=" + files.floorplan, "--set", "synth.place.seed=" + files.seed});
        const auto log_path = req.dir + "/child.log";
        std::vector<char*> cargs;
        for (auto& a : args) {
          cargs.push_back(a.data());
        }
        cargs.push_back(nullptr);
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        pid_t pid = 0;
        const int rc = posix_spawn(&pid, cargs[0], &actions, nullptr, cargs.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        if (rc != 0) {
          out.error = std::string("cannot start lhd: ") + std::strerror(rc);
          return out;
        }
        for (int i = 0; i < 1200; ++i) {  // up to 60 s for its page
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
          std::ifstream log(log_path);
          const std::string text{std::istreambuf_iterator<char>(log), {}};
          if (const auto at = text.find("http://127"); at != std::string::npos) {
            const auto end = text.find_first_of(" \r\n", at);
            out.url        = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
            return out;
          }
          int status = 0;
          if (::waitpid(pid, &status, WNOHANG) == pid) {
            const auto tail = text.size() > 600 ? text.substr(text.size() - 600) : text;
            out.error       = "the new session exited before it started: " + tail;
            return out;
          }
        }
        out.error = "the new session did not start within 60 s (see " + log_path + ")";
        return out;
      };
    }
    gc.restart = [&opts, &gui_session, trace_path = gc.trace_path](const livehd::placement::gui::Restart_request& r) -> std::string {
      // Re-run the flow with the recorded actions up to the checkpoint under other flags: re-execute this process (same
      // pid, port and token, so the open page just reconnects). Caches that would skip the placement are removed.
      if (!r.from_start && (opts.workdir.empty() || trace_path.empty())) {
        return "restart needs --workdir (the action trace is what gets replayed)";
      }
      namespace fs = std::filesystem;
      std::error_code ec;
      const auto      resume_trace = trace_path + ".resume";
      if (!r.from_start) {
        fs::copy_file(trace_path, resume_trace, fs::copy_options::overwrite_existing, ec);
        if (ec) {
          return "cannot save the trace: " + ec.message();
        }
        if ((r.shift_x != 0 || r.shift_y != 0) && !livehd::placement::gui::shift_trace(resume_trace, r.shift_x, r.shift_y)) {
          return "cannot move the recorded actions into the new frame";
        }
      }
      if (!opts.workdir.empty()) {
        fs::remove_all(fs::path(opts.workdir) / "place_cache", ec);
        fs::remove_all(fs::path(opts.workdir) / "physical_cache", ec);
      }
      const auto bare = [](std::string k) { return k.starts_with("synth.") ? k.substr(6) : k; };
      std::vector<std::string> args;
      const auto&              in = opts.invocation_argv;
      args.push_back(in.empty() ? std::string{"lhd"} : in[0]);
      for (size_t i = 1; i < in.size(); ++i) {
        const auto& t = in[i];
        if (t == "--gui" || t == "--gui-no-open") {
          continue;
        }
        if ((t == "--gui-port" || t == "--gui-resume" || t == "--gui-replay" || t == "--gui-replay-seq" || t == "--gui-token") && i + 1 < in.size()) {
          ++i;
          continue;
        }
        if (t == "--set" && i + 1 < in.size()) {
          const auto key = in[i + 1].substr(0, in[i + 1].find('='));
          if (r.set.contains(key) || r.set.contains("synth." + bare(key)) || (key.starts_with("place.") && r.set.contains("synth." + key))) {
            ++i;
            continue;
          }
        }
        args.push_back(t);
      }
      for (const auto& [key, value] : r.set) {
        args.push_back("--set");
        args.push_back(key + "=" + value);
      }
      args.insert(args.end(), {"--gui", "--gui-no-open", "--gui-port", std::to_string(gui_session->port()), "--gui-token", gui_session->token()});
      if (!r.from_start) {
        args.insert(args.end(), {"--gui-resume", r.label + ":" + std::to_string(r.nth), "--gui-replay", resume_trace, "--gui-replay-seq", std::to_string(r.seq)});
      }
      std::vector<char*> cargs;
      for (auto& a : args) {
        cargs.push_back(a.data());
      }
      cargs.push_back(nullptr);
      std::fflush(nullptr);
      ::execv(cargs[0], cargs.data());
      return std::string("cannot re-execute lhd: ") + std::strerror(errno);
    };
    gui_session = std::make_shared<livehd::placement::gui::Session>(gc);
    livehd::placement::gui::set_active_session(gui_session);
    {  // the flags the page may edit: synth.place.* with the values this run uses
      std::string json = "[";
      bool        first = true;
      for (const auto& opt : kSynthSetOptions) {
        if (opt.name != "place" && !std::string_view{opt.name}.starts_with("place.")) {
          continue;  // exactly what `lhd list options synth.place` lists
        }
        if (std::any_of(std::begin(kSynthSetRenames), std::end(kSynthSetRenames), [&](const auto& r) { return r.old_name == opt.name; })) {
          continue;  // a deprecated spelling: `list options` leaves it out and so does the panel
        }
        const auto esc = [](std::string_view s) {
          std::string out;
          for (const char c : s) {
            if (c == '"' || c == '\\') {
              out += '\\';
              out += c;
            } else if (c == '\n') {
              out += "\\n";
            } else {
              out += c;
            }
          }
          return out;
        };
        json += std::format(R"({}{{"name":"synth.{}","value":"{}","default":"{}","kind":"{}","ro":{},"help":"{}"}})", first ? "" : ",", opt.name,
                            esc(synth_set(opts, opt.name, opt.default_value)), esc(opt.default_value),
                            opt.kind == Synth_set_option::Kind::boolean ? "boolean" : opt.kind == Synth_set_option::Kind::integer ? "integer" : "text",
                            // synth.place itself must stay on for a placement session; source attributes have no run-wide value
                            opt.kind == Synth_set_option::Kind::source_only || opt.name == "place" ? "true" : "false",
                            esc(opt.help));
        first = false;
      }
      gui_session->set_options_json(json + "]");
    }
    if (!pure_replay) {
      std::string gui_error;
      if (!gui_session->start(&gui_error)) {
        throw Lhd_error{"usage", "--gui: " + gui_error, "pick another --gui-port"};
      }
      gui_session->set_stages({{"fp", true, true}, {"macro", true, true}, {"macro_legalize", true, true}, {"final", true, true}, {"legalize", true, true}, {"edit", false, true}});
      std::fprintf(stderr, "lhd synth --gui: %s\n", gui_session->url().c_str());
      if (!gui_session->agent_socket().empty()) {
        std::fprintf(stderr, "lhd synth --gui agent socket (local, mode 0600): %s   (tools/lhd_gui.py --workdir W help)\n", gui_session->agent_socket().c_str());
        if (!opts.workdir.empty()) {  // where an agent finds it
          std::filesystem::create_directories(std::filesystem::path(opts.workdir) / "synth/place");
          std::ofstream(std::filesystem::path(opts.workdir) / "synth/place/gui_agent.json")
              << std::format(R"({{"socket":"{}","pid":{}}})", gui_session->agent_socket(), int(::getpid())) << "\n";
        }
      }
      if (gc.open_browser) {
        livehd::placement::gui::open_browser(gui_session->url());
      }
    }
    place_options.gui = gui_session;
  }
  struct Gui_finish {  // the flow is done: keep serving until the user presses finish (not after an error)
    std::shared_ptr<livehd::placement::gui::Session> session;
    int                                              uncaught;
    std::string&                                     video_out;
    ~Gui_finish() {
      if (session && std::uncaught_exceptions() == uncaught) {
        session->wait_finish();
        if (session->replaying() && !video_out.empty() && session->video_frames() > 0
            && !livehd::placement::gui::make_video(session->config().video_dir, video_out)) {
          std::fprintf(stderr, "synth.place.gui.video: ffmpeg not found; the frames are in %s\n", session->config().video_dir.c_str());
        }
      }
    }
  } gui_finish{gui_session, std::uncaught_exceptions(), gui_video_out};
  const bool run_resize   = truthy(synth_set(opts, "place.resize", run_place && run_sta && place_options.full ? "true" : "false"));
  place_options.gp_resize = run_resize && place_options.gp_resize;
  // Timing weights are the normal full-placement objective. Synthesis-only
  // and GP-only commands do not enter the full-placement refinement loop.
  const bool run_timing_place = truthy(synth_set(opts, "place.timing_driven", run_place && place_options.full ? "true" : "false"));
  const bool macro_plan_only  = run_place && place_options.full && !place_options.final_cells;
  // The provisional domino profile (pass.usyn.physical=domino_provisional,
  // pass/domplace) is opt-in, and every consumer it does not support is an
  // explicit usage error here: no default moves for it.
  Eprp_var::Eprp_dict usyn_sets;
  merge_sets(opts, "pass.usyn", usyn_sets);
  const bool        domino       = domino_profile(usyn_sets);
  // The profile reads the Liberty for logical output (pass.usyn.tmap=none)
  // too: its reference cells and CMOS estimate come from it.
  const std::string usyn_liberty = domino && liberty.empty() ? resolve_liberty(opts) : liberty;
  if (domino) {
    if (mapper_method != "pass.usyn") {
      throw Lhd_error{"usage", "pass.usyn.physical=domino_provisional needs synth.mapper=usyn", ""};
    }
    // bus order is on by default: two-row cells only refuse an explicit request, unless legroute places the
    // cells (it takes a two-row cell as a tile-tall box, on the busorder regions)
    const bool region_requested = !synth_set(opts, "place.region.busorder", "").empty()
                                  || truthy(synth_set(opts, "place.region.boundarypins", "false"))
                                  || truthy(synth_set(opts, "place.region.pinalign", "false"));
    const bool legroute_places  = truthy(synth_set(opts, "place.legroute", "false"));
    if (run_place && !region_requested && !legroute_places) {
      place_options.boundarypins.enabled = place_options.boundarypins.order = false;
    }
    if (run_place && !legroute_places && (place_options.boundarypins.enabled || place_options.boundarypins.align)) {
      throw Lhd_error{
          "usage",
          "detail-final region placement (busorder, boundarypins) needs single-row cells; the domino profile places "
          "two-row cells",
          "leave synth.place.region.busorder and synth.place.region.boundarypins off, or place with synth.place.legroute=true"};
    }
  }
  if ((truthy(synth_set(opts, "place.preroute", "false")) || truthy(synth_set(opts, "place.satlegalize.route", "false")))
      && (!run_place || !run_sta || !place_options.full || macro_plan_only || !place_options.handoff_taps
          || place_options.pg == "none" || place_options.pg == "off")) {
    throw Lhd_error{"usage", "pre-routing requires final full placement, timing, handoff taps and native power grid", ""};
  }
  if (synth_set(opts, "place.pg.channel.patches", "").find_first_not_of(" \t\n\r\f\v") != std::string::npos
      && (!run_place || !place_options.full || macro_plan_only || !place_options.handoff_taps || place_options.pg == "none"
          || place_options.pg == "off")) {
    throw Lhd_error{"usage", "PG channels require final full placement, handoff taps and native power grid", ""};
  }
  const int timing_rounds = std::stoi(synth_set(opts, "place.timing_rounds", "3"));
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
  // Native stage timing of the final placement (2d-domplace E): opt-in and
  // report-only, with or without the domino profile, never cached.
  const bool run_stage_timing         = truthy(synth_set(opts, "place.stage_timing", "false"));
  const bool stage_timing_complete    = truthy(synth_set(opts, "place.stage_timing.require_complete", "false"));
  double     stage_timing_uncertainty = 0;
  {
    const auto text          = synth_set(opts, "place.stage_timing.clock_uncertainty_ps", "0");
    char*      end           = nullptr;
    stage_timing_uncertainty = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || end != text.c_str() + text.size() || !std::isfinite(stage_timing_uncertainty)
        || stage_timing_uncertainty < 0) {
      throw Lhd_error{"usage", "synth.place.stage_timing.clock_uncertainty_ps expects a non-negative number of ps", ""};
    }
  }
  const auto stage_timing_port = synth_set(opts, "place.stage_timing.clock_port", "");
  if (!run_stage_timing && (stage_timing_complete || stage_timing_uncertainty != 0 || !stage_timing_port.empty())) {
    throw Lhd_error{"usage",
                    "synth.place.stage_timing.require_complete, clock_uncertainty_ps and clock_port need "
                    "synth.place.stage_timing=true",
                    ""};
  }
  std::optional<stage::Clock_spec>    stage_clock;
  std::unique_ptr<stage::Stage_timer> stage_timer;  // built once the placement LEFs are known
  if (run_stage_timing) {
    if (!run_place || !place_options.full || macro_plan_only) {
      throw Lhd_error{"usage", "synth.place.stage_timing needs a final placement", "set synth.place=true (full placement)"};
    }
    stage_clock = stage_timing_clock(stage_timing_port, usyn_sets, sdc, usyn_liberty);
  }
  // Post-legal domino sizing (2d-domplace G): opt-in, against stage timing.
  const bool                               run_domino_sizing = truthy(synth_set(opts, "place.domino_sizing", "false"));
  bool                                     domino_sizing_ran = false;  // this run wrote place/domino_sizing.json
  livehd::placement::Custom_sizing_options domino_sizing;
  domino_sizing.buffers = truthy(synth_set(opts, "place.domino_sizing.buffers", "true"));
  {
    const auto text         = synth_set(opts, "place.domino_sizing.trials", "64");
    unsigned   value        = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value < 1 || value > 4096) {
      throw Lhd_error{"usage", "synth.place.domino_sizing.trials expects an integer in [1,4096]", ""};
    }
    domino_sizing.max_trials = value;
  }
  if (!run_domino_sizing && (!domino_sizing.buffers || domino_sizing.max_trials != 64)) {
    throw Lhd_error{"usage", "synth.place.domino_sizing.buffers and trials need synth.place.domino_sizing=true", ""};
  }
  if (run_domino_sizing && (!domino || !run_stage_timing)) {
    throw Lhd_error{"usage",
                    "synth.place.domino_sizing needs pass.usyn.physical=domino_provisional and synth.place.stage_timing=true",
                    "domino sizing accepts its trials on native stage timing"};
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
    if (!usyn_liberty.empty()) {
      res.inputs.push_back(usyn_liberty);
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
  if (gui_session && !sdc.empty()) {  // the page shows the achieved frequency against this period
    if (const auto clock_period = sdc_min_clock_period_ps(sdc, liberty)) {
      gui_session->set_clock_period_ps(*clock_period);
    }
  }
  std::shared_ptr<livehd::synth::Io_store> io_store;
  if (!macro_plan_only && io_resynth_percent > 0 && user_workdir && run_place && place_options.full && run_sta
      && mapper_method == "pass.abc") {
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
  emit_deprecated_sets(opts);  // after compile_command, whose setup_diag clears the sink

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
  // An interactive (or replayed) session must run the placement: no whole-flow physical replay, no placement cache.
  if (user_workdir && opts.incremental && run_place && place_options.full && !gui_session) {
    // Hash the learned-routing pool once: replay identity and placement share it.
    place_options.uroute_pool = livehd::placement::load_uroute_pool(place_options);
    auto        dependencies  = place_options.lefs;
    std::string libraries     = liberty;
    std::replace(libraries.begin(), libraries.end(), ',', ' ');
    std::istringstream paths(libraries);
    for (std::string path; paths >> path;) {
      dependencies.push_back(path);
    }
    for (const auto& path : {sdc, place_options.tech_rc_file, place_options.tracks_file, place_options.floorplan, place_options.seed_file}) {
      if (!path.empty()) {
        dependencies.push_back(path);
      }
    }
    physical_cache = std::make_unique<Physical_cache>(fs::path(opts.workdir) / "physical_cache",
                                                      lg_dir,
                                                      physical_descriptor(opts, place_options),
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
  // Kept for the join trials below: they color again with the same options.
  Eprp_var::Eprp_dict color_labels;
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
    // Coarse regions for ABC are register-to-register cones under a SOFT size bound: a color grows past max_gate
    // only where no cut is free (pass/color/README.md). One color per overlapping cone set (flop_to_flop, what the
    // usyn/domino mapper needs) left a CPU as one region of most of the design; the soft bound splits it and is the
    // better result overall -- bp_uno -128 -> +438 ps, partition_c -230 -> -47, partition_p -161 -> -66, with
    // vortex, coralnpu, partition_o and sha3 40-70 ps behind one color. `pass.color.synth.flop_to_flop=true` asks
    // for the one-color form again.
    if (mapper_method == "pass.abc" && partition == "coarse") {
      labels["flop_to_flop"] = "false";
      labels["convex"]       = "true";
    }
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
    color_labels = labels;
  }

  // The synthesis groups as colored: written now, and again if a join trial
  // changes the coloring.
  const auto write_groups = [&] {
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
    if (user_workdir && std::find(res.outputs.begin(), res.outputs.end(), path) == res.outputs.end()) {
      res.outputs.push_back(path);
    }
  };
  write_groups();

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
    labels["flat_sweep"]      = synth_set(opts, "flat_sweep", "false");
    // Clock gating needs the Liberty's integrated clock-gate cell: without one the gated registers' clock would be computed
    // logic and the registers would stay native flops (no mapped DFF, no timing). The library decides, the user can still say
    // false.
    labels["clock_gate"]
        = synth_set(opts, "clock_gate", "true") == "true" && !livehd::liberty::resolve_dff_cells(liberty, "").icg_ladder.empty()
              ? "true"
              : "false";
    labels["clock_gate_min_bits"] = synth_set(opts, "clock_gate_min_bits", "16");
    labels["clock_gate_depth"]    = synth_set(opts, "clock_gate_depth", "1");
    // Each mapper receives its own labels. Native USYN does not inherit
    // ABC optimization settings; standalone dispatch uses the same rule.
    // Several coarse regions meet at ports the mapper buffered blind: with the soft bound the outputs read the
    // root of their buffer chain (pass.abc.port_bypass; vortex -738 -> -177 ps). An explicit setting wins.
    if (mapper_method == "pass.abc" && synth_set(opts, "partition", "coarse") == "coarse") {
      labels["port_bypass"] = "true";
    }
    merge_mapper_sets(opts, mapper_method, labels);
    labels["library"] = usyn_liberty;  // synth.liberty is the one spelling (pass.abc.library is refused)
    if (domino) {
      // The abstracts use the placement LEFs (synth.lef, or the sky130 default
      // placement resolves); a synthesis-only run takes synth.lef as given.
      std::string lefs = run_place ? std::string{} : synth_set(opts, "lef", "");
      for (const auto& lef : place_options.lefs) {
        lefs += (lefs.empty() ? "" : ",") + lef;
      }
      domino_profile_labels(opts, lefs, labels);
    }
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

    // ---- join trials ------------------------------------------------------
    // The coloring lists the few cuts its depth rule could not decide
    // (pass.color.synth.join_trials). Each is mapped both ways -- the design
    // as colored, and with every listed pair folded -- and timed; a pair stays
    // folded only where the worst arrival through it improves by join_gain
    // percent. The region cache makes the second mapping cost the folded
    // regions only, and the final mapping below is then all cache hits.
    struct Join_trial {
      uint32_t a = 0, b = 0;
      int      color_a = 0, color_b = 0, color_joined = 0;
      unsigned depth = 0;
      double   split_ps = 0, joined_ps = 0;
      bool     kept = false;
    };
    const auto top_coloring = [&]() -> std::string {
      for (const auto& g : var.graphs) {
        if (g && g->get_name() == top) {
          auto info = g->get_input_node().attr(livehd::attrs::coloring_info);
          return info.has() ? std::string(info.get()) : std::string{};
        }
      }
      return {};
    };
    std::vector<Join_trial> join_trials;
    {
      rapidjson::Document doc;
      doc.Parse(top_coloring().c_str());
      if (!doc.HasParseError() && doc.IsObject() && doc.HasMember("join_candidates") && doc["join_candidates"].IsArray()) {
        for (const auto& c : doc["join_candidates"].GetArray()) {
          if (c.IsObject() && c.HasMember("a") && c.HasMember("b") && c.HasMember("color_a") && c.HasMember("color_b")) {
            Join_trial t;
            t.a       = c["a"].GetUint();
            t.b       = c["b"].GetUint();
            t.color_a = c["color_a"].GetInt();
            t.color_b = c["color_b"].GetInt();
            t.depth   = c.HasMember("depth") ? c["depth"].GetUint() : 0;
            join_trials.push_back(t);
          }
        }
      }
    }
    if (!join_trials.empty() && (mapper_method != "pass.abc" || !labels.contains("cache_dir") || !mapped_output)) {
      livehd::diag::warn("synth", "join-trials-skipped", "unsupported")
          .msg("{} undecided cut(s) are not tried: a join trial maps the design twice and needs the ABC mapper with its "
               "region cache",
               join_trials.size())
          .hint("give --workdir and keep lhd.incremental on, or drop pass.color.synth.join_trials")
          .emit();
      join_trials.clear();
    }
    if (!join_trials.empty()) {
      const auto trial_root = root + "/join_trial";
      // The worst arrival in each top-level color of one mapping, by color id.
      const auto map_and_time = [&](const std::string& name) {
        const auto dir = trial_root + "/" + name;
        std::error_code trial_ec;
        fs::remove_all(dir, trial_ec);
        ensure_dir(dir);
        auto trial_labels   = labels;
        trial_labels["out"] = dir + "/net";
        trial_labels["qor"] = dir + "/qor.json";
        ensure_dir(trial_labels["out"]);
        run_step(mapper_method, var, trial_labels, opts, res);
        Eprp_var trial_net;
        load_lg_into_var(trial_labels["out"], trial_net);
        Eprp_var::Eprp_dict sta{
            {"files", liberty + (sdc.empty() ? "" : "," + sdc)}
        };
        sta["top"]   = top;
        sta["qor"]   = dir + "/timing.json";
        sta["stats"] = "true";
        merge_sets(opts, "pass.opentimer", sta);
        if (const auto macros = synth_set(opts, "macro_liberty", ""); !macros.empty()) {
          sta["extra_libs"] = macros;
        }
        run_step("pass.opentimer", trial_net, sta, opts, res);
        std::map<int, double> arrival;
        rapidjson::Document   doc;
        doc.Parse(slurp_json(sta["qor"]).c_str());
        if (!doc.HasParseError() && doc.IsObject() && doc.HasMember("designs") && doc["designs"].IsArray()) {
          for (const auto& design : doc["designs"].GetArray()) {
            if (!design.IsObject() || !design.HasMember("colors") || !design["colors"].IsArray()) {
              continue;
            }
            for (const auto& row : design["colors"].GetArray()) {
              if (!row.IsObject() || !row.HasMember("module") || !row.HasMember("color") || !row.HasMember("max_arrival")) {
                continue;
              }
              // A color of the top's (flat) coloring names its region `<top>__...`.
              if (std::string_view{row["module"].GetString()}.starts_with(top + "__")) {
                auto& worst = arrival[row["color"].GetInt()];
                worst       = std::max(worst, row["max_arrival"].GetDouble());
              }
            }
          }
        }
        return arrival;
      };
      const auto recolor = [&](const std::vector<Join_trial>& folded, bool only_kept) {
        auto        again = color_labels;
        std::string pairs;
        for (const auto& t : folded) {
          if (!only_kept || t.kept) {
            pairs += std::format("{}{}:{}", pairs.empty() ? "" : ",", t.a, t.b);
          }
        }
        again["join"]        = pairs;
        again["join_trials"] = "0";  // the trial is the answer: no second round
        run_step("pass.color", var, again, opts, res);
      };

      const auto split = map_and_time("split");
      recolor(join_trials, false);
      {
        rapidjson::Document doc;
        doc.Parse(top_coloring().c_str());
        if (!doc.HasParseError() && doc.IsObject() && doc.HasMember("joined") && doc["joined"].IsArray()) {
          for (const auto& j : doc["joined"].GetArray()) {
            for (auto& t : join_trials) {
              if (j.IsObject() && j.HasMember("a") && j.HasMember("b") && j.HasMember("color") && j["a"].GetUint() == t.a
                  && j["b"].GetUint() == t.b) {
                t.color_joined = j["color"].GetInt();
              }
            }
          }
        }
      }
      const auto joined = map_and_time("joined");
      double     gain   = 5;
      if (auto it = color_labels.find("join_gain"); it != color_labels.end() && !it->second.empty()) {
        gain = std::stod(it->second);
      }
      std::string report;
      for (auto& t : join_trials) {
        const auto sa = split.find(t.color_a);
        const auto sb = split.find(t.color_b);
        const auto jj = joined.find(t.color_joined);
        if (sa != split.end() && sb != split.end() && jj != joined.end() && t.color_joined != 0) {
          t.split_ps  = std::max(sa->second, sb->second);
          t.joined_ps = jj->second;
          t.kept      = t.split_ps > 0 && (t.split_ps - t.joined_ps) * 100.0 >= gain * t.split_ps;
        }
        report += std::format("{}{{\"a\":{},\"b\":{},\"depth\":{},\"color_a\":{},\"color_b\":{},\"color_joined\":{},\"split_ps\":{:.1f},"
                              "\"joined_ps\":{:.1f},\"kept\":{}}}",
                              report.empty() ? "" : ",",
                              t.a,
                              t.b,
                              t.depth,
                              t.color_a,
                              t.color_b,
                              t.color_joined,
                              t.split_ps,
                              t.joined_ps,
                              t.kept);
      }
      const auto kept = std::count_if(join_trials.begin(), join_trials.end(), [](const auto& t) { return t.kept; });
      {
        std::ofstream out(root + "/join_trials.json");
        out << "{\"schema_version\":1,\"gain_percent\":" << gain << ",\"trials\":[" << report << "]}\n";
      }
      if (user_workdir) {
        res.outputs.push_back(root + "/join_trials.json");
      }
      if (opts.stats) {
        std::print("synth[stats]: join trials: {} cut(s) mapped both ways, {} kept joined (gain >= {}%): [{}]\n",
                   join_trials.size(),
                   kept,
                   gain,
                   report);
      }
      res.recipe_steps.emplace_back(std::format("synth.join_trials.tried={}.kept={}", join_trials.size(), kept));
      recolor(join_trials, true);  // the coloring the design keeps
      if (kept != 0) {
        write_groups();
      }
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
  // The domino model bundle pass.usyn published beside its report.
  const std::string bundle_path     = domino ? qor_path + ".domino" : std::string{};
  if (user_workdir && !mapper_qor.empty()) {
    res.outputs.push_back(report_path);
    res.outputs.push_back(provenance_path);
    if (!artifact_path.empty()) {
      res.outputs.push_back(artifact_path);
    }
    if (!bundle_path.empty()) {
      res.outputs.push_back(bundle_path);
    }
  }
  // The domino cells' Liberty for OpenTimer (empty without the profile or STA).
  std::string       domino_liberty_path;
  // hard macros (blackbox cells with a LEF MACRO): their Liberty files, read by the timers after synth.liberty
  const std::string macro_liberty = synth_set(opts, "macro_liberty", "");
  std::string       sram_liberty_path;  // the generated SRAM macros' Liberty (synth.sram)
  const auto        extra_libs    = [&] {
    std::string list = domino_liberty_path;
    if (!sram_liberty_path.empty()) {
      list += (list.empty() ? "" : ",") + sram_liberty_path;
    }
    if (!macro_liberty.empty()) {
      list += (list.empty() ? "" : ",") + macro_liberty;
    }
    return list;
  };
  if (domino) {
    std::string why;
    const auto  sidecar = livehd::domplace::read_domino_sidecar(bundle_path, &why);
    if (!sidecar) {
      throw Lhd_error{"internal", "pass.usyn published no usable domino bundle", why};
    }
    if (run_place) {
      // Every LEF reader (placement, routing identity, DEF export) sees the
      // generated abstracts as one more cell LEF, and so does the cache.
      place_options.lefs.push_back(sidecar->lef.string());
      place_options.row_access                = sidecar->row_access;
      // Resize, buffer repair and cone resynthesis re-place their edits around the two-row cells.
      place_options.optimization_multirow_eco = livehd::placement::Multirow_eco_bounds{};
      for (const auto& [macro, halves] : sidecar->templates) {
        // Each master's rail twin: the template of the other start-row parity.
        const auto twin                         = sidecar->rail_twins.find(macro);
        place_options.multirow_templates[macro] = {halves, twin == sidecar->rail_twins.end() ? std::string{} : twin->second};
      }
      // U22: storage reset pins are reset pins of the one reset tree (synth.place.reset_fanout).
      for (const auto& entry : sidecar->catalog) {
        if (!entry.reset_pin.empty()) {
          place_options.reset_pins[entry.name] = {entry.reset_pin};
        }
        place_options.domino_rail_pins[entry.name] = entry.rails;
      }
      if (run_domino_sizing) {
        // The drive variants domino sizing may swap, from the sidecar catalog.
        std::vector<livehd::placement::Custom_variant> variants;
        for (const auto& entry : sidecar->catalog) {
          livehd::placement::Custom_variant v;
          v.name          = entry.name;
          v.family        = entry.family;
          v.register_cell = entry.register_cell;
          v.rank          = entry.rank;
          v.rails         = entry.rails;
          v.rails_ff      = entry.rails_ff;
          if (!entry.reset_pin.empty()) {
            v.rails.push_back(entry.reset_pin);  // the storage reset: one more input of the same interface
            v.rails_ff.push_back(entry.reset_ff);
          }
          v.clock        = stage::kDominoPins.clock;
          v.output       = stage::kDominoPins.output;
          v.area_um2     = entry.a_footprint_um2;
          v.estimate_um2 = entry.a_estimate_um2;
          v.width_dbu    = entry.width_dbu;
          v.clock_ff     = entry.clock_ff;
          v.drive        = entry.drive;
          variants.push_back(std::move(v));
        }
        try {
          place_options.custom_cells = livehd::placement::custom_catalog(std::move(variants));
        } catch (const std::exception& error) {
          throw Lhd_error{"internal", std::string("the domino bundle has no usable variant catalog: ") + error.what(), ""};
        }
      }
      if (physical_cache) {
        physical_cache->add_dependency(sidecar->lef.string());
        physical_cache->add_dependency((fs::path(bundle_path) / livehd::domplace::kBundleSidecar).string());
      }
    }
    if (run_sta) {
      const auto clock = stage_timing_clock(synth_set(opts, "place.stage_timing.clock_port", ""), usyn_sets, sdc, usyn_liberty);
      if (!(clock.period_ps > 0)) {
        throw Lhd_error{"usage",
                        "OpenTimer on a domino netlist needs a resolvable SDC clock period",
                        "set synth.sdc with one create_clock -period on the domino clock"};
      }
      domino_liberty_path = write_domino_liberty(usyn_liberty, bundle_path, clock.period_ps, root + "/domino_timing.lib");
      if (physical_cache && !domino_liberty_path.empty()) {
        physical_cache->add_dependency(domino_liberty_path);
      }
    }
  }

  // The selected logical or mapped netlist in the output library (in memory).
  Eprp_var net;
  load_lg_into_var(net_dir, net);
  if (net.graphs.empty()) {
    throw Lhd_error{"internal", mapper_method + " produced an empty netlist library", ""};
  }
  if (mapped_output && truthy(synth_set(opts, "sram", "false"))) {
    livehd::synth::Sram_tech tech;
    tech.um2_per_bit = std::stod(synth_set(opts, "sram.um2_per_bit", "0.49"));
    tech.clk_to_q_ns = std::stod(synth_set(opts, "sram.access_ns", "0.218"));
    tech.min_bits    = static_cast<uint64_t>(std::stoull(synth_set(opts, "sram.min_bits", "2048")));
    const auto sram  = livehd::synth::macro_memories(net.graphs, tech);
    for (const auto& [module, why] : sram.kept) {
      std::fprintf(stderr, "lhd synth: synth.sram: '%s' stays a native memory instance: %s\n", module.c_str(), why.c_str());
    }
    if (!sram.macros.empty()) {
      ensure_dir(root);
      const auto lib_dir = root + "/sram", lef_path = root + "/sram.lef";
      ensure_dir(lib_dir);
      std::string lib_path;  // comma-separated: one file per macro
      {
        std::ofstream table(root + "/sram.txt");
        table << "macro\tbits\tentries\treads\twrites\twidth_um\theight_um\n";
        for (const auto& m : sram.macros) {
          table << m.name << '\t' << m.bits << '\t' << m.size << '\t' << m.read_ports << '\t' << m.write_ports << '\t' << m.width_um << '\t'
                << m.height_um << '\n';
        }
      }
      for (const auto& [macro, text] : sram.liberties) {
        const auto file = lib_dir + "/" + macro + ".lib";
        std::ofstream(file) << text;
        lib_path += (lib_path.empty() ? "" : ",") + file;
      }
      std::ofstream(lef_path) << "VERSION 5.7 ;\nBUSBITCHARS \"[]\" ;\n" << sram.lef << "END LIBRARY\n";
      sram_liberty_path = lib_path;
      if (run_place) {
        place_options.lefs.push_back(lef_path);
      }
      if (physical_cache) {
        for (const auto& [macro, text] : sram.liberties) {
          physical_cache->add_dependency(lib_dir + "/" + macro + ".lib");
        }
        physical_cache->add_dependency(lef_path);
      }
      double area = 0;
      uint64_t bits = 0;
      for (const auto& m : sram.macros) {
        area += m.width_um * m.height_um;
        bits += m.bits * m.size;
      }
      std::fprintf(stderr, "lhd synth: synth.sram: %zu memories became macros (%.0f Kib, %.0f um2): %s/*.lib, %s\n", sram.macros.size(),
                   static_cast<double>(bits) / 1024.0, area, lib_dir.c_str(), lef_path.c_str());
    }
  }

  // ---- placement: one persistent flat graph for physical outputs and STA ----
  hhds::GraphLibrary                  placed_library;
  std::unique_ptr<hhds::GraphLibrary> restored_library;
  std::unique_ptr<hhds::GraphLibrary> retry_library;
  std::shared_ptr<hhds::GraphLibrary> preroute_library;
  livehd::placement::Repair_result    repaired;
  livehd::placement::Repair_result    design_repaired;
  std::deque<livehd::placement::Repair_result> repair_loop_keep;  // the graph libraries of every repair-loop iteration, accepted or not
  std::string                         repair_loop_report;           // JSON array, one object per iteration
  livehd::placement::Repair_result    timed_placement;
  livehd::placement::Repair_result    region_refined;  // keeps its graph library alive for the placement it returned
  livehd::placement::Repair_result    timing_kick;     // timing-driven placement reacting to the region refinement
  livehd::placement::Repair_result    sat_aligned;     // the SAT-legalization step (after resize/repair)
  bool                                sat_step_ran = false;
  livehd::placement::Repair_result    resynthesized;
  livehd::placement::Repair_result    finalized;
  livehd::placement::Repair_result    domino_sized;
  std::string                         place_qor;
  const std::string                   place_qor_path = root + "/place/qor.json";
  if (run_place) {
    bool restored = false;
    if (physical_cache) {
      Phase_timer phase(res, "pass.place.restore");
      const auto  restored_net = root + "/repaired_net";
      restored                 = physical_cache->restore(restored_net, root + "/place");
      if (restored) {
        if (place_options.metadata) {
          for (const auto* suffix : {"/place/macro/metadata.json", "/place/metadata.json"}) {
            const auto               path = root + suffix;
            livehd::metadata::Bundle merged(place_options.metadata->json());
            if (fs::is_regular_file(path)) {
              auto cached = livehd::metadata::Bundle::read(path);
              for (const auto* klass : {"livehd.placement.state", "livehd.placement.hint_results"}) {
                for (const auto* record : cached.select(klass, top)) {
                  merged.replace(*record);
                }
              }
              // Replay advisory diagnostics once, from the final bundle only.
              if (std::string_view(suffix) == "/place/metadata.json") {
                for (const auto* record : cached.select("livehd.placement.hint_results", top)) {
                  if (!record->HasMember("hints") || !(*record)["hints"].IsArray()) {
                    continue;
                  }
                  for (const auto& h : (*record)["hints"].GetArray()) {
                    if (!h.IsObject() || !h.HasMember("status") || !h["status"].IsString()) {
                      continue;
                    }
                    const std::string_view status = h["status"].GetString();
                    if ((status == "discarded" || status == "relaxed") && h.HasMember("id") && h["id"].IsString()
                        && h.HasMember("reason") && h["reason"].IsString()) {
                      livehd::diag::warn("pass.place", "placement-hint", "unsupported")
                          .msg("placement hint '{}': {} (cached {})", h["id"].GetString(), h["reason"].GetString(), status)
                          .emit();
                    }
                  }
                }
              }
            }
            merged.write(path);
          }
        }
        restored_library = std::make_unique<hhds::GraphLibrary>();
        restored_library->load(restored_net);
        const auto io = restored_library->find_io(top);
        if (!io || !io->get_graph()) {
          throw Lhd_error{"internal", "completed placement has no repaired top", restored_net};
        }
        net.graphs = {io->get_graph()};
        rapidjson::Document q;
        q.Parse(slurp_json(place_qor_path).c_str());
        if (q.HasParseError() || !q.IsObject()) {
          throw Lhd_error{"internal", "completed placement has invalid QoR", place_qor_path};
        }
        if (place_options.preroute.enabled && q.HasMember("preroute") && q["preroute"].IsObject()
            && q["preroute"].HasMember("accepted_nets") && q["preroute"]["accepted_nets"].IsUint64()
            && q["preroute"]["accepted_nets"].GetUint64() != 0) {
          auto       frame    = livehd::physical::read_def(root + "/place/out.def");
          const auto fixed    = std::count_if(frame.nets.begin(), frame.nets.end(), [](const auto& n) {
            return std::any_of(n.wires.begin(), n.wires.end(), [](const auto& w) { return w.type == "FIXED"; });
          });
          const auto replayed = livehd::physical::replay_routes(*io->get_graph(),
                                                                frame,
                                                                livehd::physical::route_technology_identity(place_options.lefs));
          if (replayed != static_cast<size_t>(fixed)) {
            throw Lhd_error{"internal", "completed placement graph/DEF routes disagree", restored_net};
          }
        }
        restored_library->save(net_dir);
        auto& a = q.GetAllocator();
        if (q.HasMember("incremental") && q["incremental"].IsObject()) {
          auto& incremental = q["incremental"];
          if (incremental.HasMember("hit")) {
            incremental["hit"].SetBool(true);
          }
        }
        if (io_store) {
          const auto io_path  = root + "/place/io_resynth.json";
          const auto feedback = restored_io_report(slurp_json(io_path));
          std::ofstream(io_path) << feedback << '\n';
          rapidjson::Document io_report;
          io_report.Parse(feedback.c_str());
          q.RemoveMember("io_resynth");
          q.AddMember("io_resynth", rapidjson::Value(io_report, a), a);
          res.outputs.push_back(io_path);
        }
        q.AddMember("reused_final_state", true, a);
        rapidjson::StringBuffer                    buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        q.Accept(writer);
        place_qor = buffer.GetString();
        res.recipe_steps.emplace_back("pass.place.restore");
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
      if (place_options.full) {
        livehd::placement::initialize_growth_budget(**found, place_options);
        place_options.growth_budget->fraction     = place_options.growth_fraction;
        place_options.growth_budget->sizing_share = place_options.growth_sizing_share;
      }
      if (physical_cache) {
        const auto hint = physical_cache->placement_hint();
        if (!hint.empty()) {
          try {
            auto frame = livehd::physical::read_def(hint.string());
            if (frame.design == top) {
              // Surviving instance names retain useful positions across color
              // edits. restart_seed never inherits fixed status, old nets or RC;
              // new cells and all timing/legality are solved for this design.
              place_options.restart_seed = std::make_shared<livehd::physical::Floorplan>(std::move(frame));
              res.recipe_steps.emplace_back("pass.place.seed");
            }
          } catch (const std::exception&) {
            // A missing/unreadable optional hint must not prevent a cold solve.
          }
        }
      }
      // These optional final-stage reports belong to this invocation, not a
      // prior routing policy in the reused workdir. Cache hits restore their
      // own reports above; recomputation must only publish newly produced ones.
      fs::remove(fs::path(place_options.output_dir) / "preroute.json");
      fs::remove(fs::path(place_options.output_dir) / "power_grid.json");
      if (user_workdir && opts.incremental && !gui_session) {
        place_options.cache_dir = opts.workdir + "/place_cache";
      }
      Eprp_var timing_options;
      timing_options.dict = {
          {"files", liberty + (sdc.empty() ? "" : "," + sdc)},
          {  "top",                                      top},
          { "hier",                                  "false"}
      };
      merge_sets(opts, "pass.opentimer", timing_options.dict);
      if (const auto extra = extra_libs(); !extra.empty()) {
        timing_options.dict["extra_libs"] = extra;
      }
      timing_options.dict["cache_dir"] = "";
      timing_options.dict["qor"]       = "";
      timing_options.set_stage_labels(timing_options.dict);
      net.physical_timing_session = std::make_shared<livehd::opentimer::Physical_session>(timing_options, place_options.threads);
      auto& timing_session        = *net.physical_timing_session;
      place_options.whatif        = &timing_session.whatif();
      std::string phase_step;
      std::string phase_log;
      // The session runs in-process (no run_step), so keep Liberty/timer prints off the protocol stdout here.
      // Nested redirects (calls from inside place()/repair, already logged) restore correctly.
      const auto  timing_log     = next_log_path(opts, "pass.opentimer.incremental");
      const auto& timing_tech    = place_options.technology().lef;
      const bool  timing_profile = truthy(synth_set(opts, "place.timing_profile", "false"));
      const auto  estimate
          = [&](const livehd::placement::Ranking_view& state, livehd::opentimer::Physical_session::Purpose purpose) {
              Stdout_to_log redirect(timing_log);
              const auto&   controls = state.controls;
              const auto    before   = timing_session.statistics();
              auto          feedback = timing_session.evaluate(state.graph, state.parasitics, controls.timing_cuts, purpose);
              if (timing_profile) {
                const auto& after = timing_session.statistics();
                std::print(
                    "physical STA {}: {:.6f}s validation {:.6f}s reconcile {:.6f}s exclusions {:.6f}s propagation {:.6f}s feedback "
                    "{:.6f}s updated_nets {}\n",
                    after.evaluations,
                    after.seconds - before.seconds,
                    after.validation_seconds - before.validation_seconds,
                    after.reconcile_seconds - before.reconcile_seconds,
                    after.exclusions_seconds - before.exclusions_seconds,
                    after.propagation_seconds - before.propagation_seconds,
                    after.feedback_seconds - before.feedback_seconds,
                    after.updated_nets - before.updated_nets);
              }
              set_crash_context(phase_step, phase_log);
              return livehd::placement::prects_data_timing(std::move(feedback), controls);
            };
      // The ranking engine (2q-fasttimer E): OpenTimer's Elmore ranking, the native engine, or both with a comparison.
      const auto timing_engine = synth_set(opts, "timing.engine", "opentimer");
      if (timing_engine != "opentimer" && timing_engine != "fast" && timing_engine != "compare") {
        throw Lhd_error{"usage", "synth.timing.engine expects opentimer, fast or compare, got '" + timing_engine + "'", ""};
      }
      std::unique_ptr<livehd::timing::fast::Physical_ranker> fast_ranker;
      const auto ensure_fast_ranker = [&] {
        if (!fast_ranker) {
          const auto read_text = [](std::string_view path) {
            auto text = livehd::file_utils::read_file(std::string(path));
            if (!text) {
              throw Lhd_error{"io", "synth.timing.engine=fast cannot read " + std::string(path), ""};
            }
            return std::move(*text);
          };
          livehd::timing::fast::Physical_ranker::Options ranker_options;
          bool                                           first_file = true;
          for (const auto path : absl::StrSplit(liberty, absl::ByAnyChar(", \t\n"), absl::SkipEmpty())) {
            auto text = read_text(path);
            if (first_file) {
              // Macro libraries (SRAM, hard macros) join the setup corner.
              for (const auto extra : absl::StrSplit(extra_libs(), absl::ByAnyChar(", \t\n"), absl::SkipEmpty())) {
                text += "\n" + read_text(extra);
              }
              first_file = false;
            }
            ranker_options.liberty_texts.push_back(std::move(text));
            break;  // one corner for ranking; the min corner joins with hold (step H)
          }
          if (!sdc.empty()) {
            ranker_options.sdc_text = read_text(sdc);
          }
          fast_ranker = std::make_unique<livehd::timing::fast::Physical_ranker>(std::move(ranker_options));
        }
      };
      if (timing_engine == "fast") {
        // Ranking, detailed evaluation and the what-if edits of repair all run on the native engine; OpenTimer is
        // only used for the final report.
        ensure_fast_ranker();
        place_options.whatif = &fast_ranker->whatif();
      }
      const auto fast_rank = [&](const livehd::placement::Ranking_view& state) {
        Stdout_to_log redirect(timing_log);
        ensure_fast_ranker();

        auto feedback = fast_ranker->rank(state.graph, state.parasitics, state.controls.timing_cuts);
        if (timing_profile) {
          const auto& r = fast_ranker->engine()->report();
          std::print("fast engine: wns {:.1f} tns {:.1f} endpoints {} unclocked_endpoints {} unclocked_flops {} loops {}\n",
                     r.wns_ps, r.tns_ps, r.endpoints, r.unclocked_endpoints, r.unclocked_flops,
                     fast_ranker->bound()->netlist->loop_pins().size());
          for (const auto& u : fast_ranker->unsupported()) {
            std::print("fast unsupported: {}\n", u);
          }
          const auto& s = fast_ranker->statistics();
          std::print("fast rank {}: binds {} remaps {} wire {:.6f}s engine {:.6f}s feedback {:.6f}s bad_trees {} unresolved {}\n",
                     s.ranks,
                     s.binds,
                     s.remaps,
                     s.wire_seconds,
                     s.engine_seconds,
                     s.feedback_seconds,
                     s.bad_trees,
                     s.unresolved_labels);
        }
        return livehd::placement::prects_data_timing(std::move(feedback), state.controls);
      };
      const auto compare_rankings = [&](const livehd::timing::Feedback& reference, const livehd::timing::Feedback& fast) {
        Stdout_to_log redirect(timing_log);
        if (!reference.ranking || !fast.ranking) {
          return;
        }
        const auto&         a = *reference.ranking;
        const auto&         b = *fast.ranking;
        std::vector<double> diffs;
        std::vector<std::tuple<double, std::string, double, double>> worst_labels;
        size_t              shown_only = 0;
        std::vector<std::pair<double, double>> pairs;  // (reference, fast) slack of the labels both time
        size_t              only_reference = 0, only_fast = 0, clock_mismatch = 0;
        for (size_t i = 0; i < a.layout->labels.size(); ++i) {
          const auto  j  = b.layout->find(a.layout->labels[i]);
          const bool  ra = std::isfinite(a.pins[i].slack);
          if (j >= b.pins.size()) {
            continue;
          }
          const bool fb = std::isfinite(b.pins[j].slack);
          clock_mismatch += a.pins[i].clock != b.pins[j].clock;
          if (ra && fb) {
            const double sa = a.pins[i].slack * reference.time_unit_ps, sb = b.pins[j].slack * fast.time_unit_ps;
            pairs.emplace_back(sa, sb);  // both in ps
            diffs.push_back(std::abs(sa - sb));
            worst_labels.emplace_back(std::abs(sa - sb), a.layout->labels[i], sa, sb);
          } else {
            only_reference += ra;
            only_fast += fb;
            if (ra && !fb && shown_only++ < 4) {
              std::print("compare only_reference: {} {:.1f} ps\n", a.layout->labels[i], a.pins[i].slack * reference.time_unit_ps);
            }
          }
        }
        std::ranges::sort(diffs);
        std::ranges::sort(worst_labels, std::greater<>());
        for (size_t i = 0; i < std::min<size_t>(4, worst_labels.size()); ++i) {
          const auto& [d, label, x, y] = worst_labels[i];
          std::print("compare outlier: {} reference {:.1f} fast {:.1f}\n", label, x, y);
        }
        const auto at = [&](double q) { return diffs.empty() ? 0. : diffs[std::min(diffs.size() - 1, size_t(q * double(diffs.size())))]; };
        double     worst_a = 0, worst_b = 0;
        for (const auto& [x, y] : pairs) {
          worst_a = std::min(worst_a, x);
          worst_b = std::min(worst_b, y);
        }
        // The most critical 1% of the reference: how many of them the engine puts in its own most critical 2%.
        std::ranges::sort(pairs);
        std::vector<double> fast_sorted;
        for (const auto& pair : pairs) {
          fast_sorted.push_back(pair.second);
        }
        std::ranges::sort(fast_sorted);
        const size_t top_count = std::max<size_t>(1, pairs.size() / 100);
        size_t       kept = 0;
        if (!fast_sorted.empty()) {
          const double cut = fast_sorted[std::min(fast_sorted.size() - 1, 2 * top_count)];
          for (size_t i = 0; i < std::min(top_count, pairs.size()); ++i) {
            kept += pairs[i].second <= cut;
          }
        }
        std::print("compare rank: pins {} only_reference {} only_fast {} clock_mismatch {} |dslack| p50 {:.2f} p90 {:.2f} p99 {:.2f} max "
                   "{:.2f} ps; worst reference {:.1f} fast {:.1f}; top1% kept {}/{}\n",
                   pairs.size(),
                   only_reference,
                   only_fast,
                   clock_mismatch,
                   at(.5),
                   at(.9),
                   at(.99),
                   diffs.empty() ? 0. : diffs.back(),
                   worst_a,
                   worst_b,
                   kept,
                   std::min(top_count, pairs.size()));
      };
      // 2i-interact: the panel shows the numbers every STA evaluation of the flow already computes (read-only).
      const auto show_timing = [&](const livehd::timing::Feedback& fb) {
        if (gui_session && fb.wns) {
          gui_session->set_timing(*fb.wns * fb.time_unit_ps, fb.tns ? *fb.tns * fb.time_unit_ps : 0., -1);
        }
      };
      const auto compare_detailed = [&](const livehd::timing::Feedback& reference, const livehd::timing::Feedback& fast) {
        Stdout_to_log redirect(timing_log);
        const auto    ps = [](const std::optional<double>& v, double unit) { return v ? *v * unit : std::nan(""); };
        std::print("compare detailed: wns {:.1f}/{:.1f} tns {:.0f}/{:.0f} hold_wns {:.1f}/{:.1f} delay {:.1f}/{:.1f} area {:.1f}/{:.1f} "
                   "violations {}/{} excess {:.2f}/{:.2f} (reference/fast)\n",
                   ps(reference.wns, reference.time_unit_ps), ps(fast.wns, fast.time_unit_ps),
                   ps(reference.tns, reference.time_unit_ps), ps(fast.tns, fast.time_unit_ps),
                   ps(reference.hold_wns, reference.time_unit_ps), ps(fast.hold_wns, fast.time_unit_ps),
                   reference.delay * reference.time_unit_ps, fast.delay * fast.time_unit_ps, reference.area, fast.area,
                   reference.violations, fast.violations, reference.electrical_excess, fast.electrical_excess);
        if (!reference.dense || !fast.dense) {
          return;
        }
        std::vector<double> setup, hold, arrival, slew;
        size_t              missing = 0;
        reference.for_each_pin([&](const std::string& label, const livehd::timing::Feedback::Pin& a) {
          const auto* b = fast.find_label(label);
          if (!b) {
            ++missing;
            return;
          }
          const auto add = [](std::vector<double>& out, const std::optional<double>& x, double ux, const std::optional<double>& y,
                              double uy) {
            if (x && y) {
              out.push_back(std::abs(*x * ux - *y * uy));
            }
          };
          add(setup, a.slack, reference.time_unit_ps, b->slack, fast.time_unit_ps);
          add(hold, a.hold_slack, reference.time_unit_ps, b->hold_slack, fast.time_unit_ps);
          add(arrival, a.arrival, reference.time_unit_ps, b->arrival, fast.time_unit_ps);
          add(slew, a.slew, reference.time_unit_ps, b->slew, fast.time_unit_ps);
        });
        const auto quant = [](std::vector<double>& v, double q) {
          if (v.empty()) {
            return 0.;
          }
          std::ranges::sort(v);
          return v[std::min(v.size() - 1, size_t(q * double(v.size())))];
        };
        const auto line = [&](const char* name, std::vector<double>& v) {
          std::ranges::sort(v);
          std::print("compare detailed {}: pins {} |diff| p50 {:.2f} p90 {:.2f} p99 {:.2f} max {:.2f} ps\n", name, v.size(),
                     quant(v, .5), quant(v, .9), quant(v, .99), v.empty() ? 0. : v.back());
        };
        line("slack", setup);
        line("hold_slack", hold);
        line("arrival", arrival);
        line("slew", slew);
        std::print("compare detailed: labels only in reference {}\n", missing);
      };
      const auto evaluate = [&](const livehd::placement::Result& state) {
        Phase_timer phase(res, "pass.opentimer.incremental");
        const auto  controls = livehd::placement::prects_networks(timing_tech, state.floorplan, state.prects);
        if (timing_engine == "fast") {
          ensure_fast_ranker();
          Stdout_to_log redirect(timing_log);
          auto          fb = livehd::placement::prects_data_timing(
              fast_ranker->evaluate(state.graph, state.parasitics, controls.timing_cuts), controls);
          show_timing(fb);
          return fb;
        }
        auto        fb       = estimate({state.graph, state.parasitics, controls}, livehd::opentimer::Physical_session::Purpose::detailed);
        if (timing_engine == "compare") {
          ensure_fast_ranker();
          Stdout_to_log redirect(timing_log);
          auto          fast = livehd::placement::prects_data_timing(
              fast_ranker->evaluate(state.graph, state.parasitics, controls.timing_cuts), controls);
          compare_detailed(fb, fast);
        }
        show_timing(fb);
        return fb;
      };
      const auto rank = [&](const livehd::placement::Ranking_view& state) {
        if (timing_engine == "fast") {
          Phase_timer phase(res, "pass.timing.fast_ranking");
          return fast_rank(state);
        }
        Phase_timer phase(res, "pass.opentimer.ranking");
        auto        reference = estimate(state, livehd::opentimer::Physical_session::Purpose::ranking);
        if (timing_engine == "compare") {
          compare_rankings(reference, fast_rank(state));
        }
        return reference;
      };
      // One invocation-wide budget for optional detailed trials, rather than a
      // fresh budget at each repair phase. Reserve two of eight evaluations for
      // final legalization and the standalone timing report. Ranking is free.
      const size_t eval_budget = place_options.eval_budget;
      // The detailed evaluations so far, by whichever engine runs them.
      const auto detailed_count = [&]() -> size_t {
        if (timing_engine == "fast") {
          return fast_ranker ? fast_ranker->statistics().evaluations : 0;
        }
        const auto& stats = timing_session.statistics();
        return stats.evaluations - stats.rankings;
      };
      const livehd::placement::Trial_admission admit_trial = [&] { return detailed_count() < eval_budget; };
      // Sizing and buffering are what fix electrical violations (a 271-load net with 44 ns of wire delay), so they are
      // never starved by the optional work that runs first: IO resynthesis and the region refinement evaluate
      // unconditionally and can spend the whole shared allowance (eyeriss: the Steiner buffering ran 0 attempts).
      // Each may always use a few evaluations of its own, counted from the first one it asks for. Sizing and
      // buffering do not share that allowance either: sizing runs first and keeps trying while timing fails, so on
      // a shared count it left buffering none (liteeth with several colors: 4 sizing attempts, 0 buffering attempts,
      // and a 159 fF segment of a synchronous reset at -1.2 ns that only a buffer fixes).
      constexpr size_t repair_allowance = 4;
      struct Own_allowance {
        size_t base    = 0;
        bool   started = false;
      };
      Own_allowance sizing_allowance, buffering_allowance;
      const auto    own_allowance = [&](Own_allowance& state) {
        const size_t used = detailed_count();
        if (!state.started) {
          state.started = true;
          state.base    = used;
        }
        return used < eval_budget || used - state.base < repair_allowance;
      };
      const livehd::placement::Trial_admission admit_sizing    = [&] { return own_allowance(sizing_allowance); };
      const livehd::placement::Trial_admission admit_buffering = [&] { return own_allowance(buffering_allowance); };
      const livehd::placement::Trial_admission admit_refinement = [&] {
        // Leave room for sizing and buffer repair. Rejected placement trials
        // must not consume the entire shared allowance on overloaded designs.
        return admit_trial() && (!run_resize || detailed_count() < eval_budget - 2);
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
      place_options.optimization_pending = !macro_plan_only && (run_resize || run_timing_place || run_resynth);
      place_options.persist_parasitics   = !place_options.optimization_pending;
      // Preserve synthesis buffers by default. Explicit auto cleanup delegates
      // buffering to synth.place.resize; timing placement and resynthesis
      // alone do not rebuild it.
      const auto buffer_cleanup          = synth_set(opts, "place.buffer_cleanup", "false");
      if (buffer_cleanup != "auto" && buffer_cleanup != "true" && buffer_cleanup != "false") {
        throw Lhd_error{"usage", "invalid synth.place.buffer_cleanup: " + buffer_cleanup, "expected auto, true or false"};
      }
      const bool run_buffer_cleanup = buffer_cleanup == "auto" ? run_resize && !macro_plan_only : buffer_cleanup == "true";
      std::unique_ptr<hhds::GraphLibrary>              seed_library;
      std::vector<std::unique_ptr<hhds::GraphLibrary>> prepared_libraries;
      bool                                             prepared_before = false;
      const auto                                       prepare_input   = [&]() {
        if (!place_options.full || !run_buffer_cleanup) {
          return *found;
        }
        Phase_timer phase(res, "pass.place.buffer_cleanup");
        auto        library = std::make_unique<hhds::GraphLibrary>();
        const auto  cleaned = livehd::placement::remove_synthesis_buffers(**found, *library, place_options);
        fs::create_directories(place_options.output_dir);
        const auto mode = prepared_before ? std::ios::app : std::ios::trunc;
        std::ofstream(place_options.output_dir + "/buffer_cleanup.jsonl", mode)
            << std::format("{{\"removed\":{},\"boundary\":{},\"protected\":{},\"unsupported\":{}}}\n",
                           cleaned.removed,
                           cleaned.boundary,
                           cleaned.protected_cells,
                           cleaned.unsupported);
        prepared_libraries.push_back(std::move(library));
        prepared_before = true;
        return cleaned.graph;
      };
      if (place_options.metadata) {
        // Both buffer cleanup and the timing seed flatten this hierarchy.
        place_options.metadata_scopes = std::make_shared<const std::vector<livehd::placement::Hint_scope>>(
            livehd::placement::placement_hint_scopes(**found, place_options.technology().lef));
      }
      auto placement_input = prepare_input();
      if (run_timing_place && (place_options.fp_timing_interval > 0 || place_options.macro_timing_interval > 0)) {
        place_options.fp_evaluate = evaluate;
        place_options.gp_rank     = rank;
        // GP consumes criticality only. Physical resizing runs after legal GP.
        if (timing_engine == "fast") {
          // GP checkpoints rank from numeric wire trees: no RC strings are built for them.
          place_options.gp_rank_trees = [&](const livehd::placement::Trees_view& state) {
            Phase_timer   phase(res, "pass.timing.fast_ranking");
            Stdout_to_log redirect(timing_log);
            ensure_fast_ranker();
            return livehd::placement::prects_data_timing(
                fast_ranker->rank_trees(state.graph, state.trees, state.skeleton, state.controls.timing_cuts), state.controls);
          };
        }
        place_options.fp_resize   = false;
      }
      if (gui_session) {
        place_options.gui_rank = rank;  // read-only timing refresh for the panel (never the flow's own gp_rank)
      }
      if (io_flow && !place_options.macro_legalize_cells && !place_options.fp_evaluate) {
        // Resynthesis still needs one selected GP measurement when periodic
        // placement timing is disabled. It replaces the post-macro STA call.
        place_options.fp_evaluate           = evaluate;
        place_options.gp_rank               = rank;
        place_options.macro_timing_interval = 0;
      }
      if (run_timing_place && !macro_plan_only) {
        seed_library                      = std::make_unique<hhds::GraphLibrary>();
        auto seed_options                 = place_options;
        seed_options.timing_seed          = true;
        seed_options.macro_magnet.enabled = false;
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
        for (unsigned too_dense = 0;; ++too_dense) {
          try {
            Stdout_to_log redirect(seed_log);
            seed = livehd::placement::place(*placement_input, *seed_library, seed_options);
            break;
          } catch (const std::exception& e) {
            if (!density_failure(e, seed_options) || too_dense >= seed_options.area_restarts) {
              throw Lhd_error{"unsupported", std::string("timing seed placement failed: ") + e.what(), "see " + seed_log};
            }
            if (seed_options.area_search) {  // the driver's search takes the failed point
              throw livehd::placement::Placement_area_restart(seed_options.utilization * seed_options.area_step,
                                                              seed_options.utilization * seed_options.area_step + .05);
            }
            // too dense for the core: the seed and the placement after it run at a lower utilization
            seed_options.utilization  *= seed_options.area_step;
            seed_options.density       = std::min(seed_options.density, seed_options.utilization + .05);
            place_options.utilization  = seed_options.utilization;
            place_options.density      = seed_options.density;
            seed_library               = std::make_unique<hhds::GraphLibrary>();
            std::fprintf(stderr,
                         "lhd synth: the timing seed placement cannot legalize; utilization %.3f density %.3f\n",
                         seed_options.utilization,
                         seed_options.density);
            res.recipe_steps.emplace_back(std::format("pass.place.area_restart.utilization={:.3f}", seed_options.utilization));
          }
        }
        phase_step.clear();
        phase_log.clear();
        set_crash_context({}, {});
        note_unknown_tied(seed);
        // The seed owns a flattened copy, including its cell declarations and
        // source locations. Retire the cleanup generation before constructing
        // the full-design timer; no later stage reads that scratch graph.
        placement_input = seed.graph;
        prepared_libraries.clear();
        auto timing = [&] {
          const auto controls = livehd::placement::prects_networks(timing_tech, seed.floorplan, seed.prects);
          return rank({seed.graph, seed.parasitics, controls});
        }();
        const auto controls
            = livehd::placement::prects_networks(place_options.technology().lef, seed.floorplan, seed.prects, timing);
        const auto data_timing    = livehd::placement::prects_data_timing(std::move(timing), controls);
        place_options.net_weights = livehd::placement::timing_weights(*seed.parasitics,
                                                                      data_timing,
                                                                      place_options.timing_min_weight, place_options.timing_max_weight);
        if (place_options.net_weights.empty()) {
          throw Lhd_error{"unsupported",
                          "post-synthesis timing has no constrained data nets for placement weighting",
                          "check synth.sdc"};
        }
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
        auto*         generation    = &placed_library;
        unsigned      area_restarts = 0, resynthesis_restarts = 0;
        const auto    mesh_max_cells = place_options.region.max_cells;  // the mesh as asked; the variants scale it
        for (;;) {
          try {
            placed = livehd::placement::place(*placement_input, *generation, place_options, [&] {
              // The last stage has copied all declarations and provenance. Do
              // not carry the seed/cleanup graph through GP and its RC peaks.
              placement_input.reset();
              seed_library.reset();
              prepared_libraries.clear();
            });
            break;
          } catch (const livehd::placement::Placement_area_restart& restart) {
            // routability needs a larger core (legroute.probe, or a region without a routable answer): place again
            // at the lower utilization; in the driver's search the failed point goes back to it
            if (place_options.area_search) {
              throw;
            }
            if (++area_restarts > place_options.area_restarts) {
              throw std::runtime_error(
                  std::format("legroute asked for a larger core more than {} times", place_options.area_restarts));
            }
            placement_input                     = prepare_input();
            retry_library                       = std::make_unique<hhds::GraphLibrary>();
            generation                          = retry_library.get();
            place_options.utilization           = restart.utilization;
            place_options.density               = std::min(place_options.density, restart.density);
            place_options.placement_area_resume = true;
            // the region mesh variant: regions a quarter larger (1) or a fifth smaller (2) than asked, the given mesh (0)
            place_options.mesh_variant          = restart.mesh_variant;
            place_options.region.max_cells      = unsigned(std::lround(double(mesh_max_cells)
                                                                       * (restart.mesh_variant == 1   ? 1.25
                                                                          : restart.mesh_variant == 2 ? .8
                                                                                                      : 1.0)));
            place_options.cache_dir.clear();
            res.recipe_steps.emplace_back(
                std::format("pass.place.area_restart.utilization={:.3f}.mesh={}", restart.utilization, restart.mesh_variant));
          } catch (const livehd::placement::Placement_restart& restart) {
            // Enlarging the core does not consume the separate IO-resynthesis
            // retry. Macro legalization can fail before any color is rebuilt.
            if (resynthesis_restarts++ != 0) {
              throw std::runtime_error("whole-color resynthesis exceeded one automatic restart");
            }
            placement_input            = prepare_input();
            retry_library              = std::make_unique<hhds::GraphLibrary>();
            generation                 = retry_library.get();
            place_options.restart_seed = std::make_shared<livehd::physical::Floorplan>(restart.seed);
            place_options.cache_dir.clear();
            res.recipe_steps.emplace_back("pass.place.io_resynth.restart");
          } catch (const std::runtime_error& e) {  // the legalizer cannot close it: too dense for the core
            if (!density_failure(e, place_options) || ++area_restarts > place_options.area_restarts) {
              throw;
            }
            const double utilization = place_options.utilization * place_options.area_step;
            if (place_options.area_search) {
              throw livehd::placement::Placement_area_restart(utilization, utilization + .05);
            }
            std::fprintf(stderr, "lhd synth: %s; placement runs again at utilization %.3f\n", e.what(), utilization);
            placement_input                     = prepare_input();
            retry_library                       = std::make_unique<hhds::GraphLibrary>();
            generation                          = retry_library.get();
            place_options.utilization           = utilization;
            place_options.density               = std::min(place_options.density, utilization + .05);
            place_options.placement_area_resume = true;
            place_options.cache_dir.clear();
            res.recipe_steps.emplace_back(std::format("pass.place.area_restart.utilization={:.3f}", utilization));
          }
        }
      } catch (const livehd::placement::Placement_area_restart&) {
        throw;  // the whole command runs again at another utilization (synth_command)
      } catch (const std::exception& e) {
        throw Lhd_error{"unsupported", std::string("placement failed: ") + e.what(), "see " + log};
      }
      phase_step.clear();
      phase_log.clear();
      set_crash_context({}, {});
      // Placement has copied its input into the accepted generation. Keep only
      // that generation and the original synthesis netlist needed by resynthesis.
      placement_input.reset();
      seed_library.reset();
      prepared_libraries.clear();
      if (!run_timing_place) {
        note_unknown_tied(placed);  // the seed (above) already reported it when it ran
      }
      res.recipe_steps.emplace_back("pass.place");
      // Optimization trials can be rejected; only their accepted final state
      // should replace the completed-stage feedback.
      place_options.after_stage = {};
      place_options.restart_seed.reset();
      // Authored macro hints have already been consumed by macro placement.
      // Cell-only timing/repair trials must retain that decision, rather than
      // replaying the hints against now-fixed macros and reporting them invalid.
      place_options.metadata_finalized = true;
      if (io_flow) {
        timed_placement.timing = io_flow->take_final_timing();
      }
      if (!macro_plan_only && (run_resize || run_timing_place || run_resynth)) {
        // Every round/trial below re-runs placement (and resynth a fresh ABC frame
        // per cone): keep their output off the protocol stdout, as for pass.place.
        phase_step = "pass.place.optimize";
        phase_log  = next_log_path(opts, phase_step);
        set_crash_context(phase_step, phase_log);
        try {
          Stdout_to_log redirect(phase_log);
          if (run_timing_place) {
            Phase_timer phase(res, "pass.place.timing_driven");
            timed_placement = livehd::placement::timing_place(std::move(placed),
                                                              place_options,
                                                              evaluate,
                                                              timing_rounds,
                                                              std::move(timed_placement.timing),
                                                              admit_refinement);
            placed          = std::move(timed_placement.placed);
            res.recipe_steps.emplace_back("pass.place.timing_driven");
          }
          if (place_options.boundarypins.enabled) {
            // The region refinements (bus order, boundary pins, pin alignment) are pre-legalization steps: once the
            // timing-driven placement has converged, run them exactly once, and let timing react if they moved cells.
            {
              Phase_timer phase(res, "pass.place.regions");
              region_refined = livehd::placement::refine_regions(std::move(placed), place_options, evaluate);
              placed         = std::move(region_refined.placed);
              res.recipe_steps.emplace_back("pass.place.regions");
            }
            timed_placement.timing = std::move(region_refined.timing);
            if (run_timing_place && region_refined.moved_cells > 0) {
              Phase_timer phase(res, "pass.place.timing_driven.regions");
              auto        kick_options  = place_options;
              kick_options.output_dir  += "/regions";  // its rounds next to the refinement, not over the first rounds
              timing_kick               = livehd::placement::timing_place(std::move(placed),
                                                                          kick_options,
                                                                          evaluate,
                                                                          timing_rounds,
                                                                          std::move(timed_placement.timing),
                                                                          admit_refinement);
              placed                    = std::move(timing_kick.placed);
              timed_placement.timing    = std::move(timing_kick.timing);
              res.recipe_steps.emplace_back("pass.place.timing_driven.regions");
            }
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
                                                         std::move(timed_placement.timing),
                                                         admit_sizing);
            placed   = std::move(repaired.placed);
            res.recipe_steps.emplace_back("pass.place.resize");
          }
          if (run_resize && place_options.resize_engine != "new") {
            Phase_timer phase(res, "pass.place.repair");
            design_repaired = livehd::placement::repair_buffers(std::move(placed),
                                                                place_options,
                                                                evaluate,
                                                                livehd::placement::Repair_scope::design_after_sizing,
                                                                std::move(repaired.timing),
                                                                admit_buffering);
            placed          = std::move(design_repaired.placed);
            res.recipe_steps.emplace_back("pass.place.repair");
          }
          // Repair loop: buffers and sizes change the timing picture and, once they exist, the placement they were
          // computed for is stale. Re-place with the weights of the repaired result (a new buffer is a timed net, so
          // its slack rank pulls it to its loads), repair again, and keep the iteration only while it improves timing.
          // Each iteration may use a larger multiple of the growth allowance: the loop stops when more does not help.
          if (run_resize && place_options.resize_engine != "new" && place_options.repair_iterations > 0 && design_repaired.timing
              && !design_repaired.timing->ranking_only) {
            Phase_timer phase(res, "pass.place.repair_loop");
            auto        best_timing = *design_repaired.timing;
            repair_loop_report      = "[";
            // Detailed evaluations one iteration may use: half for the placement kick and sizing, half for
            // buffering, which would otherwise find the count spent when its turn comes.
            constexpr size_t loop_allowance = 10;
            for (unsigned it = 0; it < place_options.repair_iterations; ++it) {
              if (best_timing.wns && *best_timing.wns >= 0) {
                break;
              }
              auto iter_options        = place_options;
              iter_options.output_dir += "/repair_loop_" + std::to_string(it);
              if (place_options.growth_budget) {
                iter_options.growth_budget           = std::make_shared<livehd::placement::Growth_budget>(*place_options.growth_budget);
                iter_options.growth_budget->fraction = place_options.growth_budget->fraction * (it + 2);
              }
              const auto base = detailed_count();
              const livehd::placement::Trial_admission admit_iteration = [&] { return detailed_count() - base < loop_allowance / 2; };
              Own_allowance                            loop_buffering;
              const livehd::placement::Trial_admission admit_loop_buffering = [&] {
                const size_t used = detailed_count();
                if (!loop_buffering.started) {
                  loop_buffering.started = true;
                  loop_buffering.base    = used;
                }
                return used - loop_buffering.base < loop_allowance / 2;
              };
              const auto  before_wns = best_timing.wns.value_or(0), before_tns = best_timing.tns.value_or(0);
              // a rejected iteration takes its reset-tree depth decisions back with its placement
              const auto  pcrs_before = place_options.pcrs_depth ? std::optional{*place_options.pcrs_depth} : std::nullopt;
              auto        kick  = livehd::placement::timing_place(placed, iter_options, evaluate, 1, best_timing, admit_iteration);
              auto        sized = livehd::placement::repair_buffers(std::move(kick.placed),
                                                                    iter_options,
                                                                    evaluate,
                                                                    livehd::placement::Repair_scope::sizing,
                                                                    std::move(kick.timing),
                                                                    admit_iteration);
              auto        again = livehd::placement::repair_buffers(std::move(sized.placed),
                                                                    iter_options,
                                                                    evaluate,
                                                                    livehd::placement::Repair_scope::design_after_sizing,
                                                                    std::move(sized.timing),
                                                                    admit_loop_buffering);
              repair_loop_keep.push_back(std::move(kick));
              repair_loop_keep.push_back(std::move(sized));
              const bool better = again.timing && livehd::placement::timing_improves(best_timing, *again.timing);
              repair_loop_report += std::format("{}{{\"iteration\":{},\"growth_fraction\":{},\"wns_before\":{},\"tns_before\":{},"
                                                "\"wns_after\":{},\"tns_after\":{},\"accepted\":{}}}",
                                                it ? "," : "",
                                                it,
                                                iter_options.growth_budget ? iter_options.growth_budget->fraction : 0.0,
                                                before_wns,
                                                before_tns,
                                                again.timing ? again.timing->wns.value_or(0) : 0.0,
                                                again.timing ? again.timing->tns.value_or(0) : 0.0,
                                                better);
              if (!better) {
                if (pcrs_before) {
                  *place_options.pcrs_depth = *pcrs_before;
                }
                repair_loop_keep.push_back(std::move(again));
                break;
              }
              placed      = std::move(again.placed);
              best_timing = *again.timing;
              repair_loop_keep.push_back(std::move(again));
            }
            repair_loop_report += "]";
            design_repaired.timing = std::move(best_timing);  // the prior of the final legalization
            res.recipe_steps.emplace_back("pass.place.repair_loop");
          }
          if (run_resynth) {
            Phase_timer phase(res, "pass.place.resynth");
            auto        prior = run_resize ? (place_options.resize_engine == "new" ? std::move(repaired.timing)
                                                                                   : std::move(design_repaired.timing))
                                           : std::move(timed_placement.timing);
            resynthesized     = livehd::placement::resynthesize_cones(
                std::move(placed),
                place_options,
                evaluate,
                [](const auto& logic, const auto& boundary, const auto& library) {
                  return livehd::synth::map_cone("abc", logic, boundary, library);
                },
                std::move(prior),
                admit_trial);
            placed = std::move(resynthesized.placed);
            res.recipe_steps.emplace_back("pass.place.resynth");
          }
          if (place_options.satlegalize) {
            // The last pre-legalization step: nothing resizes after it, and it does not re-run timing-driven placement.
            Phase_timer phase(res, "pass.place.satlegalize");
            sat_aligned  = livehd::placement::sat_align_placed(std::move(placed), place_options, evaluate);
            placed       = std::move(sat_aligned.placed);
            sat_step_ran = true;
            res.recipe_steps.emplace_back("pass.place.satlegalize");
          }
        } catch (const livehd::placement::Placement_area_restart&) {
          throw;  // the whole command runs again with a larger core (synth_command)
        } catch (const std::exception& e) {
          if (density_failure(e, place_options)) {
            // Timing and boundary refinements can exhaust row space after the
            // initial placement. Give them the same automatic growth policy.
            const double utilization = place_options.utilization * place_options.area_step;
            std::fprintf(stderr, "lhd synth: %s; placement optimization requests utilization %.3f\n", e.what(), utilization);
            throw livehd::placement::Placement_area_restart(utilization, std::min(place_options.density, utilization + .05));
          }
          throw Lhd_error{"unsupported", std::string("placement optimization failed: ") + e.what(), "see " + phase_log};
        }
        phase_step.clear();
        phase_log.clear();
        set_crash_context({}, {});
      }
      if (place_options.optimization_pending) {
        Phase_timer   phase(res, "pass.place.final_legalize");
        const auto    final_log = next_log_path(opts, "pass.place.final_legalize");
        Stdout_to_log redirect(final_log);
        // Intermediate timing and repair borrowed Result::parasitics. Persist
        // the accepted final geometry before saving or invoking standalone STA.
        place_options.persist_parasitics = true;
        auto prior = sat_step_ran  ? std::move(sat_aligned.timing)
                     : run_resynth ? std::move(resynthesized.timing)
                     : run_resize
                         ? (place_options.resize_engine == "new" ? std::move(repaired.timing) : std::move(design_repaired.timing))
                         : std::move(timed_placement.timing);
        finalized  = livehd::placement::finalize_placement(placed, place_options, evaluate, std::move(prior));
        placed     = std::move(finalized.placed);
        res.recipe_steps.emplace_back("pass.place.final_legalize");
      }
      if (run_domino_sizing) {
        // Post-legal domino sizing on native stage timing: every trial is
        // re-placed through the two-row ECO and re-timed with this timer.
        if (!stage_timer) {
          stage_timer = make_stage_timer(usyn_liberty,
                                         sdc,
                                         top,
                                         bundle_path,
                                         place_options.lefs,
                                         root + "/place",
                                         *stage_clock,
                                         stage_timing_uncertainty);
        }
        const livehd::placement::Evaluate stage_evaluate = [&](const livehd::placement::Result& state) {
          const auto timing = stage_timer->evaluate(*state.graph, state.parasitics.get(), &state.floorplan, {.classes = false});
          if (!timing.period_ps) {
            std::string why = "stage timing is " + timing.status;
            for (const auto& reason : timing.reasons) {
              why += "; " + reason;
            }
            throw std::runtime_error("domino sizing needs a measured stage period: " + why);
          }
          return timing.feedback;
        };
        phase_step = "pass.place.domino_sizing";
        phase_log  = next_log_path(opts, phase_step);
        set_crash_context(phase_step, phase_log);
        try {
          Phase_timer   phase(res, "pass.place.domino_sizing");
          Stdout_to_log redirect(phase_log);
          domino_sized = livehd::placement::size_custom_placed(std::move(placed), place_options, stage_evaluate, domino_sizing);
          placed       = std::move(domino_sized.placed);
        } catch (const std::exception& e) {
          throw Lhd_error{"unsupported", std::string("domino sizing failed: ") + e.what(), "see " + phase_log};
        }
        phase_step.clear();
        phase_log.clear();
        set_crash_context({}, {});
        res.recipe_steps.emplace_back("pass.place.domino_sizing");
        domino_sizing_ran = true;
        if (user_workdir) {  // a scratch workdir's file does not survive the run
          res.outputs.push_back(root + "/place/domino_sizing.json");
        }
      }
      net.graphs = {placed.graph};
      if (!repair_loop_report.empty()) {
        rapidjson::Document q, loop;
        q.Parse(placed.qor.c_str());
        loop.Parse(repair_loop_report.c_str());
        if (!q.HasParseError() && q.IsObject() && !loop.HasParseError()) {
          q.AddMember("repair_loop", rapidjson::Value(loop, q.GetAllocator()), q.GetAllocator());
          rapidjson::StringBuffer                    buffer;
          rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
          q.Accept(writer);
          placed.qor = buffer.GetString();
        }
      }
      if (io_flow) {
        const auto    io_log = next_log_path(opts, "pass.place.io_resynth.final");
        Stdout_to_log redirect(io_log);
        (void)io_flow->after_stage(livehd::placement::Placement_stage::final, placed, std::move(finalized.timing));
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
      if (place_options.preroute.enabled) {
        const auto  previous_owner = placed.fp_library;
        Phase_timer phase(res, "pass.place.preroute");
        const auto  preroute_log = next_log_path(opts, "pass.place.preroute");
        try {
          Stdout_to_log redirect(preroute_log);
          livehd::placement::preroute_placed(placed, place_options, evaluate);
          preroute_library = placed.fp_library;
          net.graphs       = {placed.graph};
        } catch (const std::exception& error) {
          throw Lhd_error{"unsupported", std::string("pre-routing failed: ") + error.what(), "see " + preroute_log};
        }
        res.recipe_steps.emplace_back("pass.place.preroute");
        res.outputs.push_back(root + "/place/preroute.json");
      }
      net.physical_rc          = placed.parasitics;
      net.physical_timing_cuts = placed.parasitics->timing_cuts;
      {
        rapidjson::Document report, statistics;
        report.Parse(placed.qor.c_str());
        statistics.Parse(timing_session.statistics_json().c_str());
        report.AddMember("physical_timing", rapidjson::Value(statistics, report.GetAllocator()), report.GetAllocator());
        rapidjson::StringBuffer                    buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        report.Accept(writer);
        placed.qor = buffer.GetString();
      }
      place_qor = placed.qor;
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
    if (!place_options.seed_file.empty()) {
      res.inputs.push_back(place_options.seed_file);
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
  if (run_sta && !macro_plan_only) {
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
    if (const auto extra = extra_libs(); !extra.empty()) {
      labels["extra_libs"] = extra;
    }
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

  // Stage timing reruns on fresh and restored placement alike; a restored
  // QoR or report from an earlier run never stands in for it.
  const std::string                  stage_timing_path = root + "/place/stage_timing.json";
  std::optional<stage::Stage_timing> stage_timing;
  if (run_place) {
    if (run_stage_timing) {
      Phase_timer phase(res, "pass.place.stage_timing");
      if (!stage_timer) {
        stage_timer = make_stage_timer(usyn_liberty,
                                       sdc,
                                       top,
                                       bundle_path,
                                       place_options.lefs,
                                       root + "/place",
                                       *stage_clock,
                                       stage_timing_uncertainty);
      }
      const auto                                          graph = net.graphs.front();
      std::shared_ptr<const livehd::physical::Parasitics> rc    = net.physical_rc;
      if (!rc) {
        if (auto stored = graph->get_input_node().attr(livehd::attrs::placement_rc); stored.has()) {
          rc = std::make_shared<const livehd::physical::Parasitics>(livehd::physical::deserialize_parasitics_exact(stored.get()));
        }
      }
      stage_timing = stage_timing_report(*stage_timer, *graph, rc.get(), root + "/place/out.def", stage_timing_path);
      res.recipe_steps.emplace_back("pass.place.stage_timing");
      if (user_workdir) {
        res.outputs.push_back(stage_timing_path);
      }
    } else {
      std::error_code ec;
      fs::remove(stage_timing_path, ec);
    }
  }
  if (run_place) {
    rapidjson::Document placement, timing;
    placement.Parse(place_qor.c_str());
    timing.Parse(sta_qor.c_str());
    auto& allocator = placement.GetAllocator();
    if (placement.IsObject()) {
      placement.RemoveMember("stage_timing");
    }
    if (stage_timing && placement.IsObject()) {
      rapidjson::Value summary(rapidjson::kObjectType);
      const auto       optional_number = [&](std::optional<double> value) {
        rapidjson::Value v;
        if (value) {
          v.SetDouble(*value);
        }
        return v;
      };
      summary.AddMember("status", rapidjson::Value(stage_timing->status.c_str(), allocator), allocator);
      summary.AddMember("period_ps", optional_number(stage_timing->period_ps), allocator);
      summary.AddMember("limiting", rapidjson::Value(stage_timing->limiting.c_str(), allocator), allocator);
      summary.AddMember("excluded", static_cast<uint64_t>(stage_timing->excluded), allocator);
      rapidjson::Value unmeasured(rapidjson::kArrayType);
      for (const auto& name : stage_timing->unmeasured) {
        unmeasured.PushBack(rapidjson::Value(name.c_str(), allocator), allocator);
      }
      summary.AddMember("unmeasured", unmeasured, allocator);
      summary.AddMember("c_clk_ff", optional_number(stage_timing->c_clk_ff), allocator);
      summary.AddMember("report", "place/stage_timing.json", allocator);
      placement.AddMember("stage_timing", summary, allocator);
    }
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
      if (stage_timing) {
        const auto report = fs::path(report_emit->path) / "stage_timing.json";
        if (canon(report.string()) != canon(stage_timing_path)) {
          copy_export_file(stage_timing_path, report);
        }
        res.outputs.push_back(report.string());
      }
      if (domino_sizing_ran) {
        const auto sized  = root + "/place/domino_sizing.json";
        const auto report = fs::path(report_emit->path) / "domino_sizing.json";
        if (canon(report.string()) != canon(sized)) {
          copy_export_file(sized, report);
        }
        res.outputs.push_back(report.string());
      }
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
    for (const auto& src : {qor_path, run_sta && !macro_plan_only ? timing_path : std::string{}, report_path}) {
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
    for (const auto& archive : {provenance_path, artifact_path, bundle_path}) {
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
  // Coverage is checked last: the placement and its reports are kept, so a
  // rerun restores them instead of placing again.
  if (stage_timing && stage_timing_complete) {
    try {
      stage::require_complete(*stage_timing);
    } catch (const std::exception& error) {
      throw Lhd_error{"unsupported", error.what(), "see " + stage_timing_path};
    }
  }
}
}  // namespace

void gui_report_failure(const Result& res) {
  const auto session = livehd::placement::gui::active_session();
  if (!session) {
    return;
  }
  std::string detail = res.error_message;
  if (!res.error_hint.empty()) {
    detail += "\nhint: " + res.error_hint;
  }
  size_t shown = 0;
  for (const auto& rec : livehd::diag::sink().records()) {
    if (rec.severity == livehd::diag::Severity::error && rec.message != res.error_message && shown++ < 10) {
      detail += "\n" + rec.message;
    }
  }
  std::fprintf(stderr, "lhd synth --gui: the run failed; the page shows the error. Exit it in the page (or restart with other flags).\n");
  session->fail("The run failed (" + res.error_class + ")", detail);
}
}  // namespace lhd
