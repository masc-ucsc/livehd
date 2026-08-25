//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// `lhd synth`: the one-shot synthesis flow.
//
//   compile -> pass.color reduce -> pass.color synth -> pass.abc -> pass.opentimer
//
// over ONE in-memory design. The same five steps run by hand are
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

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "diag.hpp"
#include "graph_library_singleton.hpp"
#include "lhd_kernel_internal.hpp"
#include "pass.hpp"

namespace lhd {

namespace {

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

bool truthy(std::string_view v) { return !v.empty() && v != "false" && v != "0" && v != "off"; }

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

// Resolve the one Liberty for the flow: synth.liberty, else the sky130 default
// under $HAGENT_TECH_DIR. A missing library is a directed missing_file error —
// the two passes must never each fall back to a different file.
std::string resolve_liberty(const Options& opts) {
  std::string lib = synth_set(opts, "liberty", "");
  if (lib.empty()) {
    if (const char* tech = std::getenv("HAGENT_TECH_DIR"); tech != nullptr && *tech != '\0') {
      lib = (fs::path(tech) / std::string{kSynthDefaultLiberty}).string();
    }
  }
  if (lib.empty()) {
    throw Lhd_error{"missing_file",
                    "synth needs a Liberty cell library and neither --set synth.liberty nor $HAGENT_TECH_DIR is set",
                    "pass --set synth.liberty=cells.lib, or point HAGENT_TECH_DIR at a sky130 PDK (install one with `ciel`)"};
  }
  if (!fs::is_regular_file(lib)) {
    throw Lhd_error{"missing_file",
                    std::format("Liberty cell library not found: {}", lib),
                    "pass --set synth.liberty=cells.lib, or point HAGENT_TECH_DIR at a sky130 PDK (install one with `ciel`)"};
  }
  return lib;
}

}  // namespace

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
  for (const auto& [k, v] : opts.sets) {
    if (k == "pass.abc.library") {
      throw Lhd_error{"usage",
                      "synth takes ONE Liberty for pass.abc and pass.opentimer: --set synth.liberty=PATH",
                      std::format("replace `--set pass.abc.library={0}` with `--set synth.liberty={0}`", v)};
    }
  }
  const std::string liberty    = resolve_liberty(opts);
  const bool        run_sta    = truthy(synth_set(opts, "opentimer", "true"));
  const bool        run_reduce = truthy(synth_set(opts, "reduce", "true"));
  const std::string sdc        = synth_set(opts, "sdc", "");
  const std::string spef       = synth_set(opts, "spef", "");
  {
    std::vector<std::string> extra;
    if (!sdc.empty()) {
      extra.push_back(sdc);
    }
    if (!spef.empty()) {
      extra.push_back(spef);
    }
    check_inputs_exist(extra);
    res.inputs.push_back(liberty);
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
  const std::vector<Typed_path> user_emits     = opts.emits;
  const std::vector<Typed_path> user_emit_dirs = opts.emit_dirs;
  opts.emits.clear();
  opts.emit_dirs = {
      Typed_path{"lg", lg_dir}
  };
  compile_command(opts, res);
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
  if (user_workdir) {
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
  // region CONTENT, and on a warm compile <root>/lg is hardlinked from the
  // compile cache's generation, so an in-place save would write through into
  // the cache.
  if (run_reduce) {
    Eprp_var::Eprp_dict labels;
    labels["seed"]      = opts.seed;
    labels["top"]       = top;
    labels["min_nodes"] = "1";
    labels["max_nodes"] = "2";
    labels["min_count"] = "3";
    labels["min_win"]   = "1";
    merge_sets(opts, "pass.color", labels);
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
    labels["alg"]  = "synth";
    labels["seed"] = opts.seed;
    labels["top"]  = top;
    merge_sets(opts, "pass.color", labels);
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

  // ---- 3. ABC tech-map --------------------------------------------------------
  const std::string qor_path = root + "/qor.json";
  {
    std::error_code ec;
    fs::remove_all(net_dir, ec);  // pass abc's rule: the out library is rebuilt every run
    ensure_dir(net_dir);
    Eprp_var::Eprp_dict labels;
    labels["top"] = top;
    labels["out"] = net_dir;
    labels["qor"] = qor_path;
    merge_sets(opts, "pass.abc", labels);
    labels["library"] = liberty;  // synth.liberty is the one spelling (pass.abc.library refused above)
    if (opts.stats) {
      labels["stats"] = "true";
    }
    // Incremental region reuse: the same <workdir>/abc_cache the standalone
    // `lhd pass abc` uses, under the same gate (a user workdir + lhd.incremental).
    if (user_workdir && opts.incremental) {
      labels["cache_dir"] = (fs::path(opts.workdir) / "abc_cache").string();
    }
    run_step("pass.abc", var, labels, opts, res);
    if (user_workdir || lg_emit != nullptr) {
      Phase_timer phase(res, "lg.save");
      livehd::Hhds_graph_library::save(net_dir);
      res.outputs.push_back(net_dir);
    }
    if (user_workdir && fs::exists(qor_path)) {
      res.outputs.push_back(qor_path);
    }
  }
  const std::string abc_qor = slurp_json(qor_path);

  // The mapped netlist, as pass.abc left it in the out library (in memory).
  Eprp_var net;
  load_lg_into_var(net_dir, net);
  if (net.graphs.empty()) {
    throw Lhd_error{"internal", "pass.abc produced an empty netlist library", ""};
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
    sta_qor = slurp_json(timing_path);
    if (user_workdir && !sta_qor.empty()) {
      res.outputs.push_back(timing_path);
    }
  }

  // ---- reports ----------------------------------------------------------------
  // The envelope's "qor" member: {kind:"synth", abc:<abc-map>, sta:<sta>} —
  // each sub-report byte-identical to what its pass alone embeds, so a
  // consumer keyed on `qor.abc.total` / `qor.sta.designs` reads the one-shot
  // and the manual steps alike.
  res.qor_json = std::format(R"({{"schema_version":1,"kind":"synth","top":"{}","abc":{}{}}})",
                             json_escape_min(top),
                             abc_qor.empty() ? std::string{"null"} : abc_qor,
                             sta_qor.empty() ? std::string{} : std::format(R"(,"sta":{})", sta_qor));
  harvest_abc_incremental(res);           // the envelope's `incremental.abc` (one place for every reuse tier)
  harvest_sta_incremental(res, sta_qor);  // ... and `incremental.sta`
  if (report_emit != nullptr) {
    // --emit-dir report:DIR — the sidecars as files, for a run with no
    // --workdir to keep them in (or a build system that declares outputs).
    ensure_dir(report_emit->path);
    std::error_code ec;
    for (const auto& src : {qor_path, timing_path}) {
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
  }

  // The mapped netlist as Verilog: --emit verilog:FILE / --emit-dir verilog:DIR
  // (cgen over the cell-instantiating netlist, as `lhd compile lg:net` would).
  emit_verilog_outputs(opts, res, net);
}

}  // namespace lhd
