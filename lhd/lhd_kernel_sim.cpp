//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Simulation and source-level checking commands.

#include "lhd_kernel_internal.hpp"

#include <sys/wait.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <format>
#include <fstream>
#include <regex>
#include <sstream>
#include <thread>

#include "diag.hpp"
#include "file_utils.hpp"
#include "graph_library_singleton.hpp"
#include "pass.hpp"
#include "prp_sim.hpp"

namespace lhd {

// ---- sim --------------------------------------------------------------------

std::string shell_quote(const std::string& s);  // defined below (check section)

// `lhd sim <file.prp> [test.name] [--arg k=v ...]` — lower the design's DUT
// modules to Slop<N> structs (inou.cgen.sim) and, for each `test` block,
// generate a C++ driver that runs the `tick` loop and turns `assert`s into
// runtime checks, then host-compile + run it and report pass/fail.
//
// NOT bazel — this comment used to say "bazel-build", and it was wrong. Setup
// does write a standalone bazel module next to the sources (BUILD +
// MODULE.bazel, see sim_into in lhd_kernel_common.cpp) so the dir stays
// hand-buildable with `cd <simdir> && bazel run //:drv`, but `lhd sim` never
// invokes it: it shells out to the host compiler directly (the "fast run path"
// below). A nested bazel would want its own server and lock, and that generated
// MODULE.bazel archive_overrides iassert from GitHub — so it needs the network
// on a cold build, and it pins hlop at `<cwd>/../hlop`, which only resolves in
// the dev layout. `lhd sim` has to work inside SOMEONE ELSE'S bazel test
// sandbox (that is how lhdsuite drives it), where none of that holds.
//
// The trade used to cost what bazel would have handed us for free — per-TU
// parallelism and incremental rebuilds. Both are back, without the sandbox
// problems: parallelism from the built-in fan-out (`sim.jobs`), and
// incrementality from a generated `build.ninja` used when ninja is present
// (`sim.ninja`). The build file is written either way, so `ninja -C <simdir>`
// always reproduces exactly what lhd did — unlike the bazel project, which
// nothing executes.
void sim_command(Options& opts, Result& res) {
  res.command = "sim pyrope";
  if (opts.files.empty()) {
    throw Lhd_error{"usage", "sim requires a .prp file", "e.g. `lhd sim foo.prp` or `lhd sim foo.prp test.name`"};
  }
  // Positional args: every `.prp` is a SOURCE — `lhd sim a.prp b.prp top.prp`
  // loads all three so the top's `import("a")`/`import("b")` resolve to the
  // co-loaded units (no relative path or `-I` needed). A lone non-`.prp` token is
  // the test selector. The LAST source is the primary, test-containing file;
  // any earlier sources are the units it imports.
  //
  // `ln:DIR` / `lg:DIR` positionals never reach here: the option parser routes
  // them to opts.in_dirs / opts.ins exactly as it does for `lhd compile`, and
  // gather_ir_inputs below hands them to the same compile_sources. So the DUT
  // may equally be a pre-elaborated ln: forest (`import("ln:Cpu")`) or a
  // pre-compiled lg: library (`import("lg:Cpu")`) instead of a .prp beside the
  // testbench — the `test` blocks still need a .prp, which is why one is
  // required regardless.
  std::vector<std::string> sources;
  std::string              test_sel;
  for (const auto& f : opts.files) {
    if (str_tools::ends_with(f, ".prp")) {
      sources.push_back(f);
    } else {
      test_sel = f;
    }
  }
  if (sources.empty()) {
    throw Lhd_error{"usage", "sim requires a .prp file", "e.g. `lhd sim foo.prp` or `lhd sim foo.prp test.name`"};
  }
  const std::string file       = sources.back();
  const bool        setup_only = opts.sim_setup_only;
  const bool        run_only   = opts.sim_run_only;
  const bool        list_only  = opts.list_tests;
  const bool        pretty     = opts.diag_fmt == Diag_fmt::pretty;
  if (setup_only && run_only) {
    throw Lhd_error{"usage", "--setup-only and --run-only are mutually exclusive", ""};
  }

  // ---- --list-tests: a pure parse of the source's `test` blocks -> the dotted
  // names + parameters. No DUT lowering / build, so tooling can enumerate the
  // tests cheaply (and even when the design does not compile). Output honors
  // --diag-fmt like every other command: JSON (machine-readable, the SAME shape
  // the built binary's `--list-tests` prints) by default when piped, or a
  // human-readable listing in pretty mode; `--diag-fmt` overrides the auto-pick.
  if (list_only) {
    std::vector<prp_sim::Test_info> tests;
    std::string                     err;
    if (prp_sim::list_tests(file, test_sel, tests, err) != 0) {
      res.status        = "fail";
      res.error_class   = "usage";
      res.error_message = err;
      res.exit_code     = exit_code_for(res.error_class);
      return;
    }
    if (pretty) {
      std::print("{} test(s) in {}:\n", tests.size(), file);
      for (const auto& t : tests) {
        std::print("  {}", t.name);
        for (size_t i = 0; i < t.params.size(); ++i) {
          const auto& p = t.params[i];
          std::print("{}{}{}",
                     i == 0 ? "(" : ", ",
                     p.name,
                     p.required ? std::string(": required") : std::format(" = {}", p.default_text));
        }
        if (!t.params.empty()) {
          std::print(")");
        }
        std::print("\n");
      }
    } else {
      std::print("{}\n", prp_sim::tests_to_json(file, tests));
    }
    std::fflush(stdout);
    return;  // status stays pass (a pure query — no output artifact)
  }

  // The sim build dir: --workdir if given (REUSED in place — generated files are
  // overwritten, nothing is deleted), else a fresh OS-temp dir. The temp dir is
  // OUTSIDE any workspace, so a later `bazel build //...` in the user's repo
  // never sweeps the nested BUILD package or follows its convenience symlinks.
  std::string simroot;
  if (!opts.workdir.empty()) {
    simroot = opts.workdir;
  } else {
    if (run_only) {
      throw Lhd_error{"usage", "--run-only needs --workdir pointing at a prior --setup-only build", ""};
    }
    auto              tmpl = (fs::temp_directory_path() / "lhd_sim_XXXXXX").string();
    std::vector<char> buf(tmpl.c_str(), tmpl.c_str() + tmpl.size() + 1);
    if (::mkdtemp(buf.data()) == nullptr) {
      throw Lhd_error{"config", "could not create a temp dir for the sim build", "set $TMPDIR or pass --workdir"};
    }
    simroot = buf.data();
  }
  const std::string simdir = simroot + "/sim";

  // --run-only against a workdir that holds no prior --setup-only build has
  // nothing to run. Say so instead of proceeding into a confusing downstream
  // failure (a missing driver source reads as a codegen bug, not a stale or
  // mistyped --workdir).
  if (run_only && !fs::exists(simdir)) {
    throw Lhd_error{"missing_file",
                    std::format("--run-only found no sim build in '{}'", simroot),
                    "run `lhd sim <tb.prp> --setup-only --workdir <dir>` first, or check --workdir points at that dir"};
  }

  // --set sim.vcd=true: dump one VCD per test, `<workdir>/<test.name>.vcd`. The
  // path is absolute (the driver binary is run from the caller's cwd), and when
  // on, the fast build also links hlop's VCD writer (vcd_writer.cpp).
  bool vcd_on            = false;
  bool vcd_fakedelay     = true;  // sim.vcdfakedelay: X + settle delay after each edge (default); false = edge-aligned
  bool vcd_fakedelay_set = false;
  for (const auto& [k, v] : opts.sets) {
    if (k == "sim.vcd") {
      // bool-or-FILE: any non-false value turns tracing on (an explicit FILE
      // only matters for compiled/baked binaries; `lhd sim` derives per-test paths)
      vcd_on = !(v == "false" || v == "0" || v == "off" || v.empty());
    } else if (k == "sim.vcd_fake_delay") {
      vcd_fakedelay     = (v == "true" || v == "1" || v == "on");
      vcd_fakedelay_set = true;
    }
  }
  // A `--vcd-from` window or `--vcd-on-fail` implies VCD: the driver needs the
  // trace machinery emitted (the first run still produces none until a window opens).
  if (opts.sim_vcd_from >= 0 || opts.sim_vcd_on_fail) {
    vcd_on = true;
  }
  const std::string vcd_dir = vcd_on ? fs::absolute(simroot).string() : std::string{};

  // --set sim.checkpoint* : periodic editable checkpoints of DUT + testbench state
  // under <simroot>/ckpt/<test>/ckp<cycle>/ (regs.json + *.hex + tb.json +
  // meta.json). Default ON; a short run (< the min-secs floor) writes none. The
  // settings are forwarded to the driver, which owns the fork cadence + prune.
  bool        ckpt_on = true;
  std::string ckpt_min_secs, ckpt_max, ckpt_max_overhead, ckpt_every;
  for (const auto& [k, v] : opts.sets) {
    if (k == "sim.checkpoint") {
      ckpt_on = (v == "true" || v == "1" || v == "on");
    } else if (k == "sim.checkpoint_min_secs") {
      ckpt_min_secs = v;
    } else if (k == "sim.checkpoint_max") {
      ckpt_max = v;
    } else if (k == "sim.checkpoint_max_overhead") {
      ckpt_max_overhead = v;
    } else if (k == "sim.checkpoint_every") {
      ckpt_every = v;
    }
  }
  const std::string ckpt_dir = ckpt_on ? (fs::absolute(simroot).string() + "/ckpt") : std::string{};

  // Debug-flag sanity (sim_checkpoint_debug_plan): catch contradictory combinations
  // up front instead of silently producing a degenerate run.
  if (opts.sim_vcd_to >= 0 && opts.sim_vcd_from < 0) {
    throw Lhd_error{"usage", "--vcd-to needs --vcd-from (the window start)", "e.g. --vcd-from 100 --vcd-to 140"};
  }
  if (opts.sim_vcd_from >= 0 && opts.sim_vcd_to >= 0 && opts.sim_vcd_from > opts.sim_vcd_to) {
    throw Lhd_error{"usage", std::format("--vcd-from {} is after --vcd-to {}", opts.sim_vcd_from, opts.sim_vcd_to), ""};
  }
  if (ckpt_dir.empty() && opts.sim_restart_at >= 0) {
    throw Lhd_error{"usage",
                    "--restart-at needs checkpoints — do not combine it with --set sim.checkpoint=false",
                    "run once with checkpointing on to create them, then --restart-at"};
  }

  std::vector<prp_sim::Test_info> tests;

  // ---- setup: lower DUT -> Slop, generate the single driver (drv.cpp)
  if (!run_only) {
    ensure_dir(simdir);
    opts.language = "pyrope";
    opts.files    = sources;  // compile ALL positional sources (imports resolve across them)
    if (vcd_on) {
      opts.sets.emplace_back("sim.vcd", "1");  // make cgen_sim emit the VCD machinery
      if (!vcd_fakedelay) {
        opts.sets.emplace_back("sim.vcd_fake_delay", "false");  // edge-aligned trace (cgen default is true)
      }
    }
    // `sim` is DYNAMIC verification: each `test` block's asserts are checked by
    // running the generated driver, not formally. Skip pass.formal — a `test`
    // lowers to a never-instantiated comb whose runtime parameters become free
    // inputs, so a concrete-valued assert (`assert(acc == 22)`) would otherwise
    // be "refuted" over those free inputs even though the bound run satisfies it.
    opts.sets.emplace_back("compile.formal.mode", "none");
    opts.emit_dirs.push_back(Typed_path{"sim", simdir});
    auto ir = gather_ir_inputs(opts, "sim");
    compile_sources(opts, res, ir);
    if (res.status != "pass") {
      return;
    }
    std::string err;
    if (prp_sim::generate(file, simdir, test_sel, vcd_dir, tests, err) != 0) {
      res.status        = "fail";
      res.error_class   = "unsupported";
      res.error_message = err;
      res.exit_code     = exit_code_for(res.error_class);
      return;
    }
    // Also append a single `drv` cc_binary so the generated dir stays
    // bazel-buildable (`cd <simdir> && bazel run //:drv -- --test ...`); the
    // default `lhd sim` flow runs it via the fast host-compile below, no bazel.
    std::ofstream bf(std::format("{}/BUILD", simdir), std::ios::app);
    bf << "\nload(\"@rules_cc//cc:defs.bzl\", \"cc_binary\")\n";
    bf << std::format(
        "cc_binary(\n    name = \"{0}\",\n    srcs = [\"{0}.cpp\"],\n    copts = [\"-std=c++23\"],\n"
        "    deps = [\":sim\", \"@hlop//hlop\"],\n)\n",
        prp_sim::kDriverBasename);
    bf.close();
    res.recipe_steps.push_back(std::format("sim setup: {} test(s) in {}", tests.size(), simdir));
  }

  if (setup_only) {
    res.outputs.push_back(simdir);
    if (pretty) {
      std::print("  sim setup complete: {} test(s) generated in {}\n", tests.size(), simdir);
      std::print("  run with: lhd sim {} --run-only --workdir {}\n", file, simroot);
      std::fflush(stdout);
    }
    return;  // status stays pass
  }

  auto capture = [](const std::string& c, int& rc) {
    std::string out;
    char        buf[4096];
    FILE*       p = ::popen(c.c_str(), "r");
    if (p == nullptr) {
      rc = -1;
      return out;
    }
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, p)) > 0) {
      out.append(buf, n);
    }
    int st = ::pclose(p);
    rc     = (WIFEXITED(st) ? WEXITSTATUS(st) : -1);
    return out;
  };

  // ---- the single fast run path: host-compile drv.cpp + the DUT bodies (+ the
  // VCD writer when sim.vcd) with the host C++ compiler, then run the one binary.
  // The Slop runtime is header-only and, with -DNDEBUG, has no link deps — so
  // there is no nested bazel, no abseil, no network. (For --run-only the driver +
  // bodies are reused from a prior --setup-only; only the compile + run happen.)
  const std::string drv_cpp = std::format("{}/{}.cpp", simdir, prp_sim::kDriverBasename);
  if (::access(drv_cpp.c_str(), R_OK) != 0) {
    res.status        = "fail";
    res.error_class   = "usage";
    res.error_message = std::format("no generated sim driver in {} (run --setup-only --workdir {} first)", simdir, simroot);
    res.exit_code     = exit_code_for(res.error_class);
    return;
  }
  // A VCD request against a prior --setup-only that was generated WITHOUT VCD (the
  // driver lacks the trace machinery) would silently produce no waveform — reject
  // it so the user regenerates instead. The `vcd::global_timestamp` line is emitted
  // iff VCD codegen was on (prp_sim).
  if (run_only) {
    std::ifstream     dfs(drv_cpp);
    std::stringstream dss;
    dss << dfs.rdbuf();
    const bool baked_vcd = dss.str().find("vcd::global_timestamp") != std::string::npos;
    if (vcd_on && !baked_vcd) {
      res.status        = "fail";
      res.error_class   = "usage";
      res.error_message
          = "this --run-only sim was generated without VCD; re-run without --run-only (or "
                          "--setup-only --set sim.vcd=true) so the driver gets the trace machinery";
      res.exit_code     = exit_code_for(res.error_class);
      return;
    }
    // PERSIST the setup-time decision. sim.vcd is a DRIVER-AFFECTING setting:
    // setup bakes the trace machinery in, and the run phase has to link hlop's
    // vcd_writer.cpp to satisfy it. Requiring the flag again on the run
    // invocation made the honest two-phase sequence die at link time with an
    // undefined `__vcd_init()`, so every caller had to repeat the flag. The
    // driver source already records what was baked — use it.
    if (baked_vcd && !vcd_on) {
      vcd_on = true;
    }
  }
  // Same staleness trap for the VCD style: sim.vcdfakedelay is BAKED at setup
  // (the X/settle phases are codegen), so an explicit request that disagrees
  // with the prior --setup-only would be silently ignored — reject instead.
  // The `__vcd_dump_x` method is emitted iff fake-delay codegen was on.
  if (run_only && vcd_on && vcd_fakedelay_set) {
    bool baked_fakedelay = false;
    for (const auto& de : fs::directory_iterator(simdir)) {
      if (de.path().extension() != ".hpp") {
        continue;
      }
      std::ifstream     hfs(de.path());
      std::stringstream hss;
      hss << hfs.rdbuf();
      if (hss.str().find("__vcd_dump_x") != std::string::npos) {
        baked_fakedelay = true;
        break;
      }
    }
    if (baked_fakedelay != vcd_fakedelay) {
      res.status        = "fail";
      res.error_class   = "usage";
      res.error_message = std::format(
          "this --run-only sim was generated with sim.vcd_fake_delay={}; the style is baked "
                                      "at codegen — re-run --setup-only with the desired --set sim.vcd_fake_delay",
                                      baked_fakedelay ? "true" : "false");
      res.exit_code     = exit_code_for(res.error_class);
      return;
    }
  }
  const std::string hlop_inc    = sim_hlop_include_dir(opts);
  const std::string iassert_inc = sim_iassert_include_dir(opts);
  if (hlop_inc.empty() || iassert_inc.empty()) {
    res.status        = "fail";
    res.error_class   = "dependency";
    res.error_message = std::format("could not locate the sim runtime headers (slop.hpp: {}, iassert.hpp: {})",
                                    hlop_inc.empty() ? "<not found>" : hlop_inc,
                                    iassert_inc.empty() ? "<not found>" : iassert_inc);
    res.exit_code     = exit_code_for(res.error_class);
    if (pretty) {
      std::print(
          "  hint: run `lhd` from bazel (its runfiles carry slop.hpp/iassert.hpp), or export "
          "RUNFILES_DIR=<...>/lhd.runfiles to run it by hand; a source checkout resolves them from the sibling "
          "../hlop and ../iassert, and --set sim.hlop_dir=DIR / "
          "--set sim.iassert_dir=DIR point at an explicit checkout\n");
      std::fflush(stdout);
    }
    return;
  }
  const std::string cxx = sim_host_cxx();

  // The DUT bodies: every non-driver *.cpp in simdir. inou.cgen.sim does NOT emit
  // the `%`-named `test` units, so these are exactly the real module bodies.
  std::vector<std::string> bodies;
  {
    std::error_code ec;
    for (auto& de : fs::directory_iterator(simdir, ec)) {
      if (!de.is_regular_file()) {
        continue;
      }
      auto fn = de.path().filename().string();
      if (fn.size() < 5 || fn.substr(fn.size() - 4) != ".cpp") {
        continue;
      }
      if (fn == std::string(prp_sim::kDriverBasename) + ".cpp") {
        continue;  // the driver itself (exact name — a prefix skip would also drop a `drv*.prp` design's DUT bodies)
      }
      bodies.push_back(de.path().string());
    }
    std::sort(bodies.begin(), bodies.end());
  }

  const std::string exe = std::format("{}/{}.bin", simdir, prp_sim::kDriverBasename);

  // The translation units, in a FIXED order — the sorted DUT bodies, then the
  // driver, then hlop's VCD writer when tracing was baked in. The order is what
  // makes the object names and build.log reproducible; the link does not care.
  std::vector<std::string> tus = bodies;
  tus.push_back(drv_cpp);
  if (vcd_on) {
    tus.push_back(hlop_inc + "/vcd_writer.cpp");
  }

  // -O2, not -O1 (todo/livehd/2f-latch M7 efficiency item b). The optimization
  // level is NOT the lever here; the job count is. Measured 2026-07-30 on
  // dino's whole-CPU driver (18 TUs, 5372 generated lines, 18-core arm64):
  //
  //   one serial clang++ over all TUs   -O2 36.5s  -O1 31.6s  -Os 30.9s  -O0 17.3s
  //   per-TU -c in parallel, then link  -O2  6.9s
  //   the simulation itself (20k cycles)  -O2 3.8s   -Os 6.5s  -Oz 11.0s
  //
  // So parallelism buys 5.3x where the level buys ~15%, and -Os pays for its
  // 16% of build with 68% of the RUN. The note that used to sit here — "compile
  // time is IDENTICAL at -O1/-O2/-O3" — was measured on a 1-2 TU driver and
  // does not survive at 18; it reached the right conclusion (keep -O2) for a
  // reason that no longer holds. -O3 still measures no better and inflates code
  // size on generated straight-line arithmetic.
  // ABSOLUTE -I: `--workdir` is routinely relative, and the ninja build runs
  // with its cwd set to the sim dir (so its .ninja_deps/.ninja_log land there),
  // where a relative `-Iw/sim` would resolve to nothing.
  const std::string simdir_abs = fs::absolute(simdir).string();
  const std::string cflags     = std::format("-std=c++23 -DNDEBUG -O2 -I{} -I{} -I{}",
                                             shell_quote(simdir_abs),
                                             shell_quote(hlop_inc),
                                             shell_quote(iassert_inc));

  // --set sim.jobs=N bounds the fan-out (0/unset = one per hardware thread).
  // Pin it to make a build-time measurement reproducible, or to leave the
  // machine usable while a big design builds. Also becomes `ninja -j`.
  int jobs = 0;
  for (const auto& [k, v] : opts.sets) {
    if (k == "sim.jobs") {
      jobs = std::atoi(v.c_str());
    }
  }
  if (jobs <= 0) {
    jobs = static_cast<int>(std::thread::hardware_concurrency());
  }
  jobs = std::clamp(jobs, 1, static_cast<int>(tus.size()));

  // One object per TU. The objects are NOT wiped between runs and their names
  // are STABLE (derived from the source basename, never from a position in the
  // list) — a positional name would rename every object the moment a module is
  // added, turning an incremental build into a full one and orphaning the old
  // files. Design bodies all live in simdir, so their basenames are unique by
  // construction; only hlop's vcd_writer.cpp comes from outside, and it gets a
  // reserved name so a design module called `vcd_writer` cannot collide with it.
  const std::string objdir = simdir + "/obj";
  ensure_dir(objdir);
  std::vector<std::string> objs(tus.size());
  for (size_t i = 0; i < tus.size(); ++i) {
    const bool external = (fs::path(tus[i]).parent_path() != fs::path(simdir));
    objs[i]             = std::format("{}/{}{}.o", objdir, external ? "_rt_" : "", fs::path(tus[i]).stem().string());
  }
  {
    // Two TUs mapping to one object would make ninja hard-fail ("multiple rules
    // generate ...") but would silently make the built-in path link whichever
    // compile finished last — a wrong binary from a green run. Catch it here so
    // both paths report the same, nameable problem.
    auto sorted = objs;
    std::sort(sorted.begin(), sorted.end());
    auto dup = std::adjacent_find(sorted.begin(), sorted.end());
    if (dup != sorted.end()) {
      res.status        = "fail";
      res.error_class   = "unsupported";
      res.error_message = std::format("two sim translation units map to the same object file ({}) — rename one module", *dup);
      res.exit_code     = exit_code_for(res.error_class);
      return;
    }
  }

  // build.ninja, ALWAYS written (even when the build below does not use ninja).
  // It is the escape hatch that works: `ninja -C <simdir>` reproduces exactly
  // what lhd did. That matters because the OTHER generated build file, the
  // bazel one, is never executed by lhd and has silently rotted — its hlop
  // local_path_override resolves only in a dev layout. A build file lhd itself
  // runs cannot drift from the build lhd performs.
  //
  // Ninja is worth generating for ONE reason: incrementality. Parallelism is
  // already covered by the fan-out below. What ninja adds is depfile-accurate
  // staleness (`deps = gcc` + `-MD`), including the runtime headers
  // (slop.hpp/memory.hpp) that no hand-rolled rule would think to track. Its
  // mtime keying is sound here only because emission upstream is
  // WRITE-IF-DIFFERENT (File_output::same_on_disk): a comment-only Pyrope edit
  // rewrites nothing, and a body-only edit rewrites just that module's .cpp,
  // leaving its .hpp — and therefore every parent's object — untouched. If that
  // invariant ever breaks, this degrades to a full rebuild every run.
  //
  // Two limits of the mtime model, recorded because neither is enforced here.
  // (a) Ninja treats an output as fresh when it is NOT OLDER than its inputs,
  // so a source rewritten inside the same filesystem timestamp tick as its
  // object is missed. Sub-second stamps (APFS, ext4, btrfs, xfs) make the
  // window vanishingly small; a whole-second filesystem would make an
  // edit-and-rerun within one second able to link a stale object. (b) The link
  // line is `$in`, which is argument-list bound — around 8k objects on a 1 MiB
  // ARG_MAX. Both are far outside anything these designs reach; a build that
  // does would want `rspfile`, and a `restat`/digest rule respectively.
  const std::string ninja_file = simdir + "/build.ninja";
  {
    // Ninja path escaping (build statements): space, `:` and `$` are the only
    // characters with meaning. Commands additionally go through /bin/sh, so
    // single-input paths are double-quoted at the point of use. Paths are
    // written ABSOLUTE: ninja runs with -C into the sim dir, and the file is
    // also meant to be run by hand from anywhere.
    auto nesc = [](const std::string& p) {
      std::error_code ec_abs;
      std::string     s = fs::absolute(p, ec_abs).string();
      if (ec_abs) {
        s = p;
      }
      std::string o;
      o.reserve(s.size());
      for (char c : s) {
        if (c == ' ' || c == ':' || c == '$') {
          o += '$';
        }
        o += c;
      }
      return o;
    };
    // `cflags` already holds shell-quoted -I paths; `$` would be eaten by ninja
    // variable expansion before /bin/sh ever sees it, so double it.
    std::string nflags;
    for (char c : cflags) {
      nflags += (c == '$') ? "$$" : std::string(1, c);
    }
    std::ofstream nf(ninja_file);
    nf << "# Generated by `lhd sim`. Reproduces the exact host build lhd runs:\n"
       << "#   ninja -C " << simdir_abs << "\n"
       << "# Regenerated on every build, so edits here are lost.\n"
       << "ninja_required_version = 1.3\n\n"
       << "cxx = " << cxx << "\n"
       << "cflags = " << nflags << "\n\n"
       // $in/$out are NOT shell-quoted here: ninja already shell-escapes each
       // path as it expands them into `command`, so wrapping them would hand
       // the compiler an argument containing literal quote characters. The
       // `depfile` binding expands RAW instead, which is exactly why the
       // `-MF $out.d` in the command still names the file ninja goes on to
       // read. Rule bodies must be SPACE-indented and every rule must be
       // declared before the first build statement that uses it.
       << "rule cc\n"
       << "  command = $cxx $cflags -MD -MF $out.d -c $in -o $out\n"
       << "  description = CC $out\n"
       << "  depfile = $out.d\n"
       << "  deps = gcc\n\n"
       << "rule link\n"
       << "  command = $cxx $in -o $out\n"
       << "  description = LINK $out\n\n";
    for (size_t i = 0; i < tus.size(); ++i) {
      nf << "build " << nesc(objs[i]) << ": cc " << nesc(tus[i]) << "\n";
    }
    nf << "\nbuild " << nesc(exe) << ": link";
    for (const auto& o : objs) {
      nf << " " << nesc(o);
    }
    nf << "\n\ndefault " << nesc(exe) << "\n";
  }

  // --set sim.ninja: "" (default) = use ninja when it is on PATH, else the
  // built-in fan-out; false = never; true = require it (fail if absent);
  // PATH = use that binary. The DEFAULT must stay "use it only if present":
  // `lhd sim` runs inside other people's build sandboxes, and needing nothing
  // but a host C++ compiler is exactly why the fast path exists at all. A hard
  // ninja dependency would repeat the mistake the generated bazel project made.
  std::string ninja_set;
  for (const auto& [k, v] : opts.sets) {
    if (k == "sim.ninja") {
      ninja_set = v;
    }
  }
  std::string ninja_bin;
  if (ninja_set != "false" && ninja_set != "0" && ninja_set != "off") {
    if (!ninja_set.empty() && ninja_set != "true" && ninja_set != "1" && ninja_set != "on") {
      ninja_bin = ninja_set;  // an explicit path
    } else {
      int  probe_rc = 0;
      auto found    = capture("command -v ninja 2>/dev/null", probe_rc);
      if (probe_rc == 0) {
        while (!found.empty() && (found.back() == '\n' || found.back() == '\r')) {
          found.pop_back();
        }
        // Absolute only. A bare or `./`-relative hit would mean we picked up a
        // `ninja` sitting in the caller's cwd — and bazel runs tests with `.`
        // FIRST on PATH, so that is a live way to have the build hijacked by a
        // file that merely shares a name with the tool.
        if (!found.empty() && found.front() == '/') {
          ninja_bin = found;
        }
      }
    }
    if (ninja_bin.empty() && (ninja_set == "true" || ninja_set == "1" || ninja_set == "on")) {
      res.status        = "fail";
      res.error_class   = "dependency";
      res.error_message = "--set sim.ninja=true but no `ninja` on PATH (drop the flag to use the built-in parallel build)";
      res.exit_code     = exit_code_for(res.error_class);
      return;
    }
  }

  // fail_build LOG_BODY SHOWN — the one compile-failure exit, shared by both
  // build paths. Persists the full compiler output (in JSONL mode it was
  // previously swallowed entirely — "see the compiler output" pointed nowhere)
  // and surfaces the first error line in the machine-readable message.
  auto fail_build = [&](const std::string& log_body, const std::string& shown) {
    const std::string build_log = simdir + "/build.log";
    {
      std::ofstream bl(build_log);
      if (bl.is_open()) {
        bl << log_body;
      }
    }
    std::string        first_err;
    std::string        ln;
    std::istringstream iss(shown);
    while (std::getline(iss, ln)) {
      if (ln.find("error") != std::string::npos) {
        first_err = ln;
        break;
      }
    }
    res.status        = "fail";
    res.error_class   = "compile";
    res.error_message = std::format("the sim driver failed to compile ({}): {}",
                                    build_log,
                                    first_err.empty() ? std::string{"see the log"} : first_err);
    res.exit_code     = exit_code_for(res.error_class);
    if (pretty) {
      std::istringstream sis(shown);
      while (std::getline(sis, ln)) {
        std::print("    {}\n", ln);
      }
      std::fflush(stdout);
    }
  };

  if (!ninja_bin.empty()) {
    // ninja owns compile AND link, and skips whatever is already up to date.
    // Its own output is already per-edge buffered and ordered, so it is both
    // the log body and what gets shown.
    const std::string nc = std::format("{} -C {} -j {} 2>&1", shell_quote(ninja_bin), shell_quote(simdir), jobs);
    int               nrc = 0;
    auto              nout = capture(nc, nrc);
    if (nrc != 0) {
      fail_build(nc + "\n\n" + nout, nout);
      return;
    }
  } else {
    // Built-in fallback: compile every TU, `jobs` at a time, then link. No
    // staleness check — this path always rebuilds, because without depfiles it
    // cannot know which headers a TU read, and a wrong answer there is a
    // silently stale binary reporting wrong simulation values.
    std::vector<std::string> cmds(tus.size()), outs(tus.size());
    std::vector<int>         rcs(tus.size(), 0);
    {
      std::atomic<size_t> cursor{0};
      auto                worker = [&] {
        for (size_t i = cursor.fetch_add(1); i < tus.size(); i = cursor.fetch_add(1)) {
          cmds[i] = std::format("{} {} -c {} -o {} 2>&1",
                                shell_quote(cxx),
                                cflags,
                                shell_quote(tus[i]),
                                shell_quote(objs[i]));
          outs[i] = capture(cmds[i], rcs[i]);
        }
      };
      std::vector<std::thread> pool;
      pool.reserve(static_cast<size_t>(jobs));
      for (int t = 0; t < jobs; ++t) {
        pool.emplace_back(worker);
      }
      for (auto& t : pool) {
        t.join();
      }
    }

    // Every TU's command + output reaches build.log in TU order, so the log is
    // the same whatever order the workers finished in. What is PRINTED is only
    // the FIRST failing TU: one bad generated header fails all 18 identically,
    // and eighteen copies of the one diagnostic bury it.
    std::string log_body, shown;
    size_t      n_failed = 0;
    for (size_t i = 0; i < tus.size(); ++i) {
      log_body += cmds[i] + "\n\n" + outs[i] + "\n";
      if (rcs[i] != 0 && n_failed++ == 0) {
        shown = outs[i];
      }
    }
    if (n_failed != 0) {
      if (n_failed > 1) {
        shown += std::format("({} more translation unit(s) also failed; see build.log)\n", n_failed - 1);
      }
      fail_build(log_body, shown);
      return;
    }

    std::string link = shell_quote(cxx);
    for (const auto& o : objs) {
      link += " " + shell_quote(o);
    }
    link += " -o " + shell_quote(exe) + " 2>&1";  // merge linker diagnostics into the capture
    int  link_rc  = 0;
    auto link_out = capture(link, link_rc);
    if (link_rc != 0) {
      fail_build(link + "\n\n" + link_out, link_out);
      return;
    }
  }

  // Run the one binary. A test selector (`lhd sim foo.prp my.test`) becomes
  // `--test`; an explicit `--seed` and every `lhd sim --arg key=value` are
  // forwarded (the binary itself filters per-test params + warns on a `--arg`
  // that no run test consumes).
  std::string run_args;
  if (!test_sel.empty()) {
    run_args += " --test " + shell_quote(test_sel);
  }
  if (opts.seed_explicit) {
    run_args += " --seed " + shell_quote(opts.seed);
  }
  // Always ask the driver for its per-test result array (a sidecar JSON file);
  // it is read back below and embedded verbatim as the envelope's "tests" member
  // (so `lhd sim --result-json r.json` carries {test,status,cycle,failing_assert,
  // prp_file,line,msg} per test). Remove any stale sidecar from a reused workdir.
  const std::string sim_tests_path = std::format("{}/sim_tests.json", simdir);
  {
    std::error_code ec_rm;
    fs::remove(sim_tests_path, ec_rm);
  }
  run_args += " --result-json " + shell_quote(sim_tests_path);
  // Checkpoint creation (sim.checkpoint*): enabled by default; the driver owns the
  // fork cadence / prune and only writes once the min-secs floor elapses.
  if (!ckpt_dir.empty()) {
    run_args += " --ckpt-dir " + shell_quote(ckpt_dir);
    if (!ckpt_min_secs.empty()) {
      run_args += " --checkpoint-min-secs " + shell_quote(ckpt_min_secs);
    }
    if (!ckpt_max.empty()) {
      run_args += " --checkpoint-max " + shell_quote(ckpt_max);
    }
    if (!ckpt_max_overhead.empty()) {
      run_args += " --checkpoint-max-overhead " + shell_quote(ckpt_max_overhead);
    }
    if (!ckpt_every.empty()) {
      run_args += " --checkpoint-every " + shell_quote(ckpt_every);
    }
  } else {
    run_args += " --no-checkpoint";
  }
  // Debug replay: jump to the failure region (loads the nearest checkpoint <= N).
  if (opts.sim_restart_at >= 0) {
    run_args += " --restart-at " + shell_quote(std::to_string(opts.sim_restart_at));
  }
  // Windowed VCD: restart near Y, run silent to Y, trace [Y, Z].
  if (opts.sim_vcd_from >= 0) {
    run_args += " --vcd-from " + shell_quote(std::to_string(opts.sim_vcd_from));
    if (opts.sim_vcd_to >= 0) {
      run_args += " --vcd-to " + shell_quote(std::to_string(opts.sim_vcd_to));
    }
  }
  // On an assert fire, auto-dump a VCD of the failure region (re-run from the
  // nearest checkpoint with a window around the failing cycle).
  if (opts.sim_vcd_on_fail) {
    run_args += " --vcd-on-fail --vcd-fail-window " + shell_quote(std::to_string(opts.sim_vcd_fail_window));
  }
  // Observability: --list-signals / --probe / --break-when. Results go to the
  // debug sidecar, read back below and embedded as the envelope's "debug" member.
  const std::string sim_debug_path = std::format("{}/sim_debug.json", simdir);
  {
    std::error_code ec_rm2;
    fs::remove(sim_debug_path, ec_rm2);
  }
  const bool debug_requested = opts.sim_list_signals || !opts.sim_probe.empty() || !opts.sim_break_when.empty();
  if (debug_requested) {
    run_args += " --debug-json " + shell_quote(sim_debug_path);
    if (opts.sim_list_signals) {
      run_args += " --list-signals";
    }
    if (!opts.sim_probe.empty()) {
      run_args += " --probe " + shell_quote(opts.sim_probe);
      if (opts.sim_probe_from >= 0) {
        run_args += " --probe-from " + shell_quote(std::to_string(opts.sim_probe_from));
      }
      if (opts.sim_probe_to >= 0) {
        run_args += " --probe-to " + shell_quote(std::to_string(opts.sim_probe_to));
      }
    }
    if (!opts.sim_break_when.empty()) {
      run_args += " --break-when " + shell_quote(opts.sim_break_when);
    }
  }
  // Forward each `--arg key=value` as `--key value`, but ONLY when `key` is a
  // parameter of a SELECTED test (`selected_params`). Two reasons:
  //  * a key that is a driver control flag (`--arg help=1` -> `--help`, `--arg
  //    test=x` -> `--test x`, `--arg seed=N`) would otherwise be intercepted by
  //    the binary and silently skip / restrict the run — a false green;
  //  * a key that is a real parameter of some test but not a selected one is
  //    irrelevant to this run, so it is dropped silently (not forwarded).
  // A key that is a parameter of NO test in the file (`all_params`) is a genuine
  // typo and is warned about unconditionally (visible in JSON mode too). This
  // restores the pre-single-driver two-layer guard. `tests` lists the SELECTED
  // tests' parameters (generate / list_tests already filtered by `test_sel`); for
  // --run-only re-derive them from the source.
  if (run_only && tests.empty()) {
    std::vector<prp_sim::Test_info> lt;
    std::string                     lerr;
    if (prp_sim::list_tests(file, test_sel, lt, lerr) == 0) {
      tests = std::move(lt);
    }
  }
  std::set<std::string> selected_params;
  for (const auto& t : tests) {
    for (const auto& p : t.params) {
      selected_params.insert(p.name);
    }
  }
  std::set<std::string> all_params = selected_params;  // == selected when no test_sel
  if (!test_sel.empty()) {
    std::vector<prp_sim::Test_info> allt;
    std::string                     aerr;
    if (prp_sim::list_tests(file, "", allt, aerr) == 0) {
      for (const auto& t : allt) {
        for (const auto& p : t.params) {
          all_params.insert(p.name);
        }
      }
    }
  }
  for (const auto& [k, v] : opts.sim_args) {
    if (selected_params.count(k) != 0) {
      run_args += " " + shell_quote("--" + k) + " " + shell_quote(v);
    } else if (all_params.count(k) == 0) {
      std::print(stderr, "lhd sim: warning: --arg {}={} matches no test parameter (ignored)\n", k, v);
    }
    // else: a real parameter of an unselected test — valid but not for this run.
  }
  // Capture the binary's STDOUT (its `puts` output + the per-test PASS/FAIL
  // verdict lines) for parsing + the pretty relay, but let its STDERR pass
  // through to the user's stderr UNCHANGED — that is where the driver prints its
  // warnings (e.g. a `--arg` that matches no test parameter) and usage errors, so
  // they stay visible in JSON mode too (not only in the pretty relay below).
  int  rc  = 0;
  auto out = capture(std::format("{}{}", shell_quote(exe), run_args), rc);

  // Read back the per-test result array (present whenever the driver ran, even on
  // assert failure); embedded as the envelope's "tests" member. Absent if the
  // driver crashed before writing it.
  if (std::error_code ec_st; fs::exists(sim_tests_path, ec_st)) {
    std::ifstream     ifs(sim_tests_path);
    std::stringstream ss;
    ss << ifs.rdbuf();
    std::string tj = ss.str();
    while (!tj.empty() && (tj.back() == '\n' || tj.back() == '\r' || tj.back() == ' ' || tj.back() == '\t')) {
      tj.pop_back();
    }
    if (tj.size() >= 2 && tj.front() == '[') {  // a well-formed array, never a partial write
      res.sim_tests_json = tj;
    }
  }

  // Read back the observability sidecar ({signals?, probe?, break?}); embedded as
  // the envelope's "debug" member.
  if (std::error_code ec_dbg; debug_requested && fs::exists(sim_debug_path, ec_dbg)) {
    std::ifstream     ifs(sim_debug_path);
    std::stringstream ss;
    ss << ifs.rdbuf();
    std::string dj = ss.str();
    while (!dj.empty() && (dj.back() == '\n' || dj.back() == '\r' || dj.back() == ' ' || dj.back() == '\t')) {
      dj.pop_back();
    }
    if (dj.size() >= 2 && dj.front() == '{') {
      res.sim_debug_json = dj;
    }
  }

  // The binary's EXIT CODE is the authoritative verdict (0 = all selected tests
  // passed, 1 = a test failed, 2 = a usage error, <0 = the driver crashed). The
  // per-test `PASS <name>` / `FAIL <name> (...)` lines are parsed only for the
  // structured recipe detail (best-effort: a test that itself `puts` a line
  // starting with "PASS "/"FAIL " adds a cosmetic recipe entry but never changes
  // the verdict, which is rc-driven).
  int    n_fail = 0;
  size_t n_run  = 0;
  {
    std::istringstream iss(out);
    std::string        ln;
    while (std::getline(iss, ln)) {
      const bool pass = ln.rfind("PASS ", 0) == 0;
      const bool fail = ln.rfind("FAIL ", 0) == 0;
      if (!pass && !fail) {
        continue;
      }
      ++n_run;
      std::string name = ln.substr(5);
      if (auto sp = name.find(" ("); sp != std::string::npos) {
        name = name.substr(0, sp);
      }
      res.recipe_steps.push_back(std::format("sim {} ({})", name, pass ? "pass" : "fail"));
      if (fail) {
        ++n_fail;
      }
    }
  }
  if (pretty) {
    std::istringstream iss(out);
    std::string        ln;
    while (std::getline(iss, ln)) {
      std::print("    {}\n", ln);
    }
    std::fflush(stdout);
  }
  res.outputs.push_back(simdir);
  if (rc == 0) {
    // every selected test passed — status stays pass.
  } else if (rc == 2) {
    // a usage error inside the binary (unknown flag / unknown `--test` name /
    // missing value / bad `--arg` value); the binary printed the specific reason
    // (relayed above in pretty mode).
    res.status        = "fail";
    res.error_class   = "usage";
    res.error_message = (!test_sel.empty() && n_run == 0) ? std::format("no test matched '{}' in {}", test_sel, file)
                                                          : std::format("the sim driver rejected an argument in {}", file);
    res.exit_code     = exit_code_for(res.error_class);
  } else {
    // rc == 1 (a test's assert fired) or rc < 0 (the driver crashed / signaled).
    res.status        = "fail";
    res.error_class   = "assert";
    res.error_message = (n_fail > 0 && n_run > 0) ? std::format("{} of {} test(s) failed", n_fail, n_run)
                                                  : std::format("the sim driver exited with code {}", rc);
    res.exit_code     = exit_code_for(res.error_class);
  }
}

// ---- check ------------------------------------------------------------------

std::string shell_quote(const std::string& s) {
  std::string out{"'"};
  for (char c : s) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out += c;
    }
  }
  out += '\'';
  return out;
}

std::string locate_lgcheck() {
  if (const char* env = std::getenv("LHD_LGCHECK"); env != nullptr && ::access(env, X_OK) == 0) {
    return fs::absolute(env).string();
  }
  auto exe_dir = file_utils::get_exe_path();
  for (const auto& cand : {std::string{"./inou/yosys/lgcheck"},
                           std::string{"inou/yosys/lgcheck"},
                           exe_dir + "/lhd.runfiles/_main/inou/yosys/lgcheck",
                           exe_dir + "/lhd.runfiles/livehd/inou/yosys/lgcheck"}) {
    if (::access(cand.c_str(), X_OK) == 0) {
      return fs::absolute(cand).string();
    }
  }
  throw Lhd_error{"dependency",
                  "lgcheck (inou/yosys/lgcheck) not found",
                  "run from the LiveHD repo root or set LHD_LGCHECK=/path/to/lgcheck"};
}

// The yosys binary lgcheck shells out to. lgcheck's own fallbacks are
// cwd-relative, so pass an absolute path explicitly (lhd runs lgcheck from
// the scratch workdir to keep its trace*/log droppings out of the caller's
// cwd). Empty result -> let lgcheck try `which yosys`.
std::string locate_lgcheck_yosys() {
  if (const char* env = std::getenv("LHD_YOSYS"); env != nullptr && ::access(env, X_OK) == 0) {
    return fs::absolute(env).string();
  }
  auto exe_dir = file_utils::get_exe_path();
  for (const auto& cand : {std::string{"bazel-bin/inou/yosys/yosys2"},
                           exe_dir + "/../inou/yosys/yosys2",
                           exe_dir + "/lhd.runfiles/_main/inou/yosys/yosys2",
                           exe_dir + "/lhd.runfiles/livehd/inou/yosys/yosys2"}) {
    if (::access(cand.c_str(), X_OK) == 0) {
      return fs::absolute(cand).string();
    }
  }
  return "";
}

// Load one --impl/--ref side into `var.graphs` (no cgen). Defined below; both
// lec backends share it.
void load_side_graphs(Options& opts, Result& res, const std::string& kind, const std::string& path, std::string_view side,
                      Eprp_var& var);

// Return a verilog file for an --impl/--ref side (the lgyosys/lgcheck backend):
// a verilog side passes straight through (lgcheck reads Verilog directly);
// every other kind is loaded to graphs (load_side_graphs) and re-emitted with
// cgen into the scratch workdir.
std::string materialize_verilog(Options& opts, Result& res, const std::string& kind, const std::string& path,
                                std::string_view side) {
  if (kind == "verilog") {
    res.inputs.push_back(path);
    check_inputs_exist({path});
    return path;
  }
  Eprp_var var;
  load_side_graphs(opts, res, kind, path, side, var);  // lg/pyrope/ln -> graphs (throws if empty)
  auto          scratch = std::format("{}/check_{}", workdir(opts), side);
  auto          names   = cgen_into(opts, res, var, scratch);
  auto          out     = std::format("{}/check_{}.v", workdir(opts), side);
  std::ofstream ofs(out);
  for (const auto& n : names) {
    std::ifstream ifs(std::format("{}/{}.v", scratch, n));
    ofs << ifs.rdbuf();
  }
  return out;
}

// The yosys-slang plugin (slang.so) for lgcheck's `--gold_reader slang`: lets
// yosys read SystemVerilog packed-struct sources (CIRCT output) that
// read_verilog cannot parse. Same candidates inou_yosys_api probes.
std::string locate_yosys_slang_plugin() {
  auto exe_path = file_utils::get_exe_path();
  for (const auto& cand : {absl::StrCat(exe_path, "/../external/+_repo_rules+yosys_slang/slang.so"),
                           absl::StrCat(exe_path, "/../external/+http_archive+yosys_slang/slang.so"),
                           absl::StrCat(exe_path, "/lhd.runfiles/+http_archive+yosys_slang/slang.so")}) {
    if (::access(cand.c_str(), R_OK) == 0) {
      return cand;
    }
  }
  return "";
}

// The lgyosys backend (`--set formal.solver=lgyosys`): materialize both sides to
// Verilog and discharge with inou/yosys/lgcheck (the former `lhd check`).
// Verilog sides pass straight through; pyrope:/ln:/lg: are compiled first.
void lec_lgyosys(Options& opts, Result& res) {
  auto impl_v  = fs::absolute(materialize_verilog(opts, res, opts.impl_kind, opts.impl_path, "impl")).string();
  auto ref_v   = fs::absolute(materialize_verilog(opts, res, opts.ref_kind, opts.ref_path, "ref")).string();
  auto lgcheck = locate_lgcheck();
  auto yosys   = locate_lgcheck_yosys();

  // --set formal.lec.gold_reader=slang: read the REFERENCE side through yosys-slang
  // (SystemVerilog packed structs / '{...} patterns exceed read_verilog).
  std::string gold_reader = "verilog";
  for (const auto& [k, v] : opts.sets) {
    if (k == "formal.lec.gold_reader" && !v.empty()) {
      gold_reader = v;
    }
  }
  if (gold_reader != "verilog" && gold_reader != "slang") {
    throw Lhd_error{"usage", std::format("--set formal.lec.gold_reader expects verilog|slang, got '{}'", gold_reader), ""};
  }
  std::string slang_plugin;
  if (gold_reader == "slang") {
    slang_plugin = locate_yosys_slang_plugin();
    if (slang_plugin.empty()) {
      throw Lhd_error{"dependency",
                      "formal.lec.gold_reader=slang: yosys-slang plugin (slang.so) not found",
                      "build //inou/yosys (the @yosys_slang external) or use the default gold_reader"};
    }
  }

  // Run lgcheck FROM the scratch workdir so its cwd droppings (trace*.v,
  // lgcheck*.log) never land in the caller's directory (hermetic kernel).
  auto rundir = fs::absolute(workdir(opts)).string();
  auto cmd    = std::format("cd {} && {} --implementation {} --reference {}",
                            shell_quote(rundir),
                            shell_quote(lgcheck),
                            shell_quote(impl_v),
                            shell_quote(ref_v));
  if (!yosys.empty()) {
    cmd += std::format(" --yosys {}", shell_quote(yosys));
  }
  if (gold_reader == "slang") {
    cmd += std::format(" --gold_reader slang --slang_plugin {}", shell_quote(slang_plugin));
  }
  if (!opts.impl_top.empty()) {
    cmd += std::format(" --implementation_top {}", shell_quote(opts.impl_top));
  }
  if (!opts.ref_top.empty()) {
    cmd += std::format(" --reference_top {}", shell_quote(opts.ref_top));
  }
  if (opts.impl_top.empty() && opts.ref_top.empty() && !opts.top.empty()) {
    cmd += std::format(" --top {}", shell_quote(opts.top));
  }
  auto log  = next_log_path(opts, "lec.lgcheck");
  cmd      += std::format(" >> {} 2>&1", shell_quote(fs::absolute(log).string()));

  res.recipe_steps.emplace_back("pass.lec solver:lgyosys (lgcheck)");
  int rc   = std::system(cmd.c_str());
  int code = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
  if (opts.verbose) {
    mirror_log_to_stderr(log);
  }
  std::string name = !opts.impl_top.empty() ? opts.impl_top : opts.impl_path;
  // lgcheck exit codes: 0 = proven equivalent, 2 = INCONCLUSIVE (could not prove
  // AND found no counterexample — yosys' equiv flow often can't prove a
  // cgen-restructured netlist equal to its source even when it is), anything else
  // = a real refutation. Only a real refute is a hard failure.
  if (code == 2) {
    std::print("lec: '{}' INCONCLUSIVE (solver=lgyosys; could not prove, no counterexample)\n", name);
    return;
  }
  std::print("lec: '{}' {} (solver=lgyosys)\n", name, code == 0 ? "PROVEN equivalent" : "REFUTED (not equivalent)");
  if (code != 0) {
    throw Lhd_error{"equiv_fail",
                    std::format("equivalence check failed ({} vs {})", opts.impl_path, opts.ref_path),
                    std::format("see {}", log)};
  }
}

}  // namespace lhd
