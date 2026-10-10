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
  const auto*       mapper        = find_mapper(synth_set(opts, "mapper", "abc"));  // validated by check_known_set_passes
  const auto        mapper_method = std::string{mapper->method};
  const bool        mapped_output = mapper_maps_cells(opts, mapper_method);
  const std::string liberty       = mapped_output ? resolve_liberty(opts) : std::string{};
  const bool        run_sta       = mapped_output && truthy(synth_set(opts, "opentimer", "true"));
  if (!mapped_output && truthy(synth_set(opts, "opentimer", ""))) {
    throw Lhd_error{"usage", "logical-only USYN cannot run OpenTimer", "use tmap=abc or synth.opentimer=false"};
  }
  const bool        run_reduce    = truthy(synth_set(opts, "reduce", "false"));
  const std::string sdc           = synth_set(opts, "sdc", "");
  const std::string spef          = synth_set(opts, "spef", "");
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
    labels["alg"]    = "synth";
    labels["seed"]   = opts.seed;
    labels["top"]    = top;
    // The coloring profile follows the mapper: ABC wants the stop_* cuts (small
    // regions), the unate/domino mapper wants register-to-register colors.
    // Merged BEFORE the user's sets so an explicit pass.color.synth.* wins.
    labels["mapper"] = std::string{mapper->color_profile};
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

  // ---- 3. ABC tech-map --------------------------------------------------------
  const std::string qor_path = root + "/qor.json";
  {
    std::error_code ec;
    fs::remove_all(net_dir, ec);  // a stale netlist must never shadow a region shell the mapper fills
    ensure_dir(net_dir);
    Eprp_var::Eprp_dict labels;
    labels["top"]     = top;
    labels["out"]     = net_dir;
    labels["qor"]     = qor_path;
    // synth.threads is the shared ABC worker limit for every command.
    labels["threads"] = synth_set(opts, "threads", "0");
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
  // The envelope's "qor" member: {kind:"synth", abc:<abc-map>, sta:<sta>} (plus
  // the mapper's own report under its name, e.g. usyn:<decision report>) —
  // each sub-report byte-identical to what its pass alone embeds, so a
  // consumer checks each report kind before reading its fields. USYN mapping
  // uses technology-map; logical-only output has abc:null and no STA report.
  res.qor_json = std::format(R"({{"schema_version":1,"kind":"synth","top":"{}","abc":{}{}{}}})",
                             json_escape_min(top),
                             abc_qor.empty() ? std::string{"null"} : abc_qor,
                             sta_qor.empty() ? std::string{} : std::format(R"(,"sta":{})", sta_qor),
                             mapper_qor.empty() ? std::string{} : std::format(R"(,"{}":{})", mapper->report, mapper_qor));
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
