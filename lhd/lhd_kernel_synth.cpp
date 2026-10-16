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
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "attrs.hpp"
#include "diag.hpp"
#include "graph_library_singleton.hpp"
#include "lhd_kernel_internal.hpp"
#include "node_util.hpp"
#include "pass.hpp"
#include "pass/place/place.hpp"
#include "pass/place/repair.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "synth_policy.hpp"

namespace lhd {

namespace {

bool truthy(std::string_view v) { return !v.empty() && v != "false" && v != "0" && v != "off"; }

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
    const char* tech = std::getenv("HAGENT_TECH_DIR");
    if (tech == nullptr || *tech == '\0') {
      throw Lhd_error{"missing_file", "placement needs synth.lef or $HAGENT_TECH_DIR", "set synth.lef=tech.lef,cells.lef"};
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
  out.full      = mode == "full";
  out.snapshots = truthy(synth_set(opts, "place.snapshots", "false"));
  out.cohesion  = number("place.cohesion", "0");
  if (out.cohesion < 0) {
    throw Lhd_error{"usage", "synth.place.cohesion must be nonnegative", ""};
  }
  out.utilization          = number("place.utilization", "0.5");
  out.aspect               = number("place.aspect", "1");
  out.density              = number("place.density", "0.8");
  out.iterations           = integer("place.iterations", "500");
  out.bins                 = integer("place.bins", "32");
  out.scale_macro_density  = truthy(synth_set(opts, "place.scale_macro_density", "false"));
  out.automatic_bins       = truthy(synth_set(opts, "place.auto_bins", "false"));
  out.exact_macro_density  = truthy(synth_set(opts, "place.exact_macro_density", "false"));
  out.adaptive_density     = truthy(synth_set(opts, "place.adaptive_density", "false"));
  out.routability          = truthy(synth_set(opts, "place.routability", "false"));
  out.macro_overlap_weight = number("place.macro_overlap", "0");
  out.macro_jitter         = number("place.macro_jitter", "0");
  out.io_iterations        = integer("place.io_iterations", "0");
  out.schedule_iterations  = integer("place.schedule_iterations", "0");
  if (out.macro_overlap_weight < 0 || out.macro_jitter < 0 || out.macro_jitter > .1 || out.schedule_iterations < 0
      || out.io_iterations < 0) {
    throw Lhd_error{"usage", "invalid synth.place repulsion controls", ""};
  }
  out.threads         = integer("place.threads", "1");
  const auto halo     = synth_set(opts, "place.halo", "0");
  char*      halo_end = nullptr;
  out.halo_x          = std::strtod(halo.c_str(), &halo_end);
  if (halo_end == halo.c_str()) {
    throw Lhd_error{"usage", "invalid synth.place.halo", "expected nonnegative microns: H or X,Y"};
  }
  out.halo_y = out.halo_x;
  if (*halo_end == ',') {
    const auto* y = halo_end + 1;
    out.halo_y    = std::strtod(y, &halo_end);
    if (halo_end == y) {
      throw Lhd_error{"usage", "invalid synth.place.halo", "expected nonnegative microns: H or X,Y"};
    }
  }
  if (*halo_end != '\0' || !std::isfinite(out.halo_x) || !std::isfinite(out.halo_y) || out.halo_x < 0 || out.halo_y < 0) {
    throw Lhd_error{"usage", "invalid synth.place.halo", "expected nonnegative microns: H or X,Y"};
  }
  out.macro_mode = synth_set(opts, "place.macro", "none");
  if (out.macro_mode != "none" && out.macro_mode != "h2d") {
    throw Lhd_error{"usage", "synth.place.macro must be none or h2d", ""};
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
// $HAGENT_TECH_DIR. `synth.liberty` is the ONE spelling every Liberty reader
// shares -- `lhd synth` (pass.abc + pass.opentimer) and the standalone
// `lhd pass abc` / `lhd pass opentimer` all resolve through here, so no two
// of them can ever fall back to a different file. A missing library is a
// directed missing_file error.
std::string resolve_liberty(const Options& opts) {
  std::string lib = synth_set(opts, "liberty", "");
  if (lib.empty()) {
    if (const char* tech = std::getenv("HAGENT_TECH_DIR"); tech != nullptr && *tech != '\0') {
      lib = (fs::path(tech) / std::string{kSynthDefaultLiberty}).string();
    }
  }
  if (lib.empty()) {
    throw Lhd_error{"missing_file",
                    "no Liberty cell library: neither --set synth.liberty nor $HAGENT_TECH_DIR is set",
                    "pass --set synth.liberty=cells.lib, or point HAGENT_TECH_DIR at a sky130 PDK (install one with `ciel`)"};
  }
  if (!fs::is_regular_file(lib)) {
    throw Lhd_error{"missing_file",
                    std::format("Liberty cell library not found: {}", lib),
                    "pass --set synth.liberty=cells.lib, or point HAGENT_TECH_DIR at a sky130 PDK (install one with `ciel`)"};
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
  const auto*       mapper           = find_mapper(synth_set(opts, "mapper", "abc"));  // validated by check_known_set_passes
  const auto        mapper_method    = std::string{mapper->method};
  const bool        mapped_output    = mapper_maps_cells(opts, mapper_method);
  const std::string liberty          = mapped_output ? resolve_liberty(opts) : std::string{};
  const bool        run_sta          = mapped_output && truthy(synth_set(opts, "opentimer", "true"));
  const bool        run_place        = truthy(synth_set(opts, "place", "false"));
  auto              place_options    = run_place ? placement_options(opts) : livehd::placement::Options{};
  const bool        run_resynth      = truthy(synth_set(opts, "place.resynth", "false"));
  const bool        run_resize       = truthy(synth_set(opts, "place.resize", "false"));
  const bool        run_timing_place = truthy(synth_set(opts, "place.timing_driven", "false"));
  const int         timing_rounds    = std::stoi(synth_set(opts, "place.timing_rounds", "3"));
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
  const bool        user_workdir = !opts.workdir.empty();
  const std::string root         = workdir(opts) + "/synth";
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
    const auto partition = synth_set(opts, "partition", "auto");
    labels["mapper"]     = partition == "coarse" ? std::string{"usyn"}
                           : partition == "fine" ? std::string{"abc"}
                                                 : std::string{mapper->color_profile};
    merge_color_sets(opts, labels);
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
  {
    std::error_code ec;
    fs::remove_all(net_dir, ec);  // a stale netlist must never shadow a region shell the mapper fills
    ensure_dir(net_dir);
    Eprp_var::Eprp_dict labels;
    labels["top"]        = top;
    labels["out"]        = net_dir;
    labels["qor"]        = qor_path;
    // ABC keeps its independent worker setting; USYN uses shared admission.
    labels["threads"]    = "0";
    labels["specialize"] = synth_set(opts, "specialize", "true");
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
  hhds::GraphLibrary               placed_library;
  livehd::placement::Repair_result repaired;
  livehd::placement::Repair_result design_repaired;
  livehd::placement::Repair_result timed_placement;
  livehd::placement::Repair_result resynthesized;
  std::string                      place_qor;
  const std::string                place_qor_path = root + "/place/qor.json";
  if (run_place) {
    const auto found = std::find_if(net.graphs.begin(), net.graphs.end(), [&](const auto& g) { return g && g->get_name() == top; });
    if (found == net.graphs.end()) {
      throw Lhd_error{"internal", "mapped top missing before placement", ""};
    }
    place_options.liberty    = liberty;
    place_options.output_dir = root + "/place";
    if (user_workdir && opts.incremental) {
      place_options.cache_dir = opts.workdir + "/place_cache";
    }
    unsigned    trial = 0;
    // The placement phase that drives evaluate(): its run_step clears the crash
    // context, which then names that phase again for the next placement call.
    std::string phase_step;
    std::string phase_log;
    const auto  evaluate = [&](const livehd::placement::Result& state) {
      Eprp_var analysis;
      analysis.graphs            = {state.graph};
      analysis.physical_rc       = state.parasitics;
      const auto          report = root + "/place/resize_timing_" + std::to_string(trial++) + ".json";
      Eprp_var::Eprp_dict labels{
          {"files", liberty + (sdc.empty() ? "" : "," + sdc)},
          {  "top",                                      top},
          {  "qor",                                   report}
      };
      merge_sets(opts, "pass.opentimer", labels);
      labels["cache_dir"] = "";
      run_step("pass.opentimer", analysis, labels, opts, res);
      set_crash_context(phase_step, phase_log);
      return livehd::placement::timing_feedback(slurp_json(report));
    };
    hhds::GraphLibrary seed_library;
    auto               placement_input = *found;
    if (run_timing_place) {
      auto seed_options        = place_options;
      seed_options.timing_seed = true;
      seed_options.cache_dir.clear();
      seed_options.output_dir += "/post_synth";
      Phase_timer phase(res, "pass.place.post_synth_timing");
      // Like pass.place below: DREAMPlace prints to stdout, which is reserved
      // for the result protocol, and a refusal is a placement failure.
      const auto  seed_log = next_log_path(opts, "pass.place.post_synth_timing");
      set_crash_context("pass.place.post_synth_timing", seed_log);
      livehd::placement::Result seed;
      try {
        Stdout_to_log redirect(seed_log);
        seed = livehd::placement::place(**found, seed_library, seed_options);
      } catch (const std::exception& e) {
        throw Lhd_error{"unsupported", std::string("timing seed placement failed: ") + e.what(), "see " + seed_log};
      }
      set_crash_context({}, {});
      const auto timing         = evaluate(seed);
      place_options.net_weights = livehd::placement::timing_weights(*seed.parasitics, timing);
      if (place_options.net_weights.empty()) {
        throw Lhd_error{"unsupported",
                        "post-synthesis timing has no constrained data nets for placement weighting",
                        "check synth.sdc"};
      }
      placement_input = seed.graph;
      res.recipe_steps.emplace_back("pass.place.post_synth_timing");
    }
    const auto log = next_log_path(opts, "pass.place");
    set_crash_context("pass.place", log);
    livehd::placement::Result placed;
    try {
      Phase_timer   phase(res, "pass.place");
      Stdout_to_log redirect(log);
      placed = livehd::placement::place(*placement_input, placed_library, place_options);
    } catch (const std::exception& e) {
      throw Lhd_error{"unsupported", std::string("placement failed: ") + e.what(), "see " + log};
    }
    set_crash_context({}, {});
    res.recipe_steps.emplace_back("pass.place");
    if (run_resize || run_timing_place || run_resynth) {
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
        if (run_resize) {
          Phase_timer phase(res, "pass.place.resize");
          repaired = livehd::placement::repair_buffers(std::move(placed),
                                                       place_options,
                                                       evaluate,
                                                       livehd::placement::Repair_scope::sizing);
          placed   = std::move(repaired.placed);
          res.recipe_steps.emplace_back("pass.place.resize");
        }
        if (run_resize) {
          Phase_timer phase(res, "pass.place.repair");
          design_repaired = livehd::placement::repair_buffers(std::move(placed),
                                                              place_options,
                                                              evaluate,
                                                              livehd::placement::Repair_scope::design);
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
    net.graphs      = {placed.graph};
    net.physical_rc = placed.parasitics;
    place_qor       = placed.qor;
    placed.graph->get_io()->get_library()->save(net_dir);
    for (const auto& path : place_options.lefs) {
      res.inputs.push_back(path);
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
        fs::copy_file(root + "/place/out.def", destination, fs::copy_options::overwrite_existing);
      }
      res.outputs.push_back(destination.string());
      const auto proxy_lef = fs::path(root) / "place/memory_proxies.lef";
      if (fs::is_regular_file(proxy_lef)) {
        const auto proxy_destination = fs::path(def_out->path) / "memory_proxies.lef";
        if (canon(proxy_destination.string()) != canon(proxy_lef.string())) {
          fs::copy_file(proxy_lef, proxy_destination, fs::copy_options::overwrite_existing);
        }
        res.outputs.push_back(proxy_destination.string());
      }
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
        fs::copy_file(place_qor_path, destination, fs::copy_options::overwrite_existing);
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
      fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
      if (ec) {
        throw Lhd_error{"config", std::format("could not write {}: {}", dst, ec.message()), "check --emit-dir report: permissions"};
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
}

}  // namespace lhd
