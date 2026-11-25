//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Simulation and source-level checking commands.

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <thread>

#include "option_text.hpp"
#include "absl/strings/str_join.h"
#include "cgen_verilog.hpp"
#include "diag.hpp"
#include "file_name.hpp"  // livehd::unit_file_stem — the shared long-name policy
#include "file_utils.hpp"
#include "graph_library_singleton.hpp"
#include "job_manager.hpp"
#include "lhd_kernel_internal.hpp"
#include "compile_salt.hpp"
#include "pass.hpp"
#include "prp_sim.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "sim_compile_jobs.hpp"
#include "tb_llvm.hpp"
#include "worker_pool.hpp"  // livehd::run_workers (big-stack workers)

namespace lhd {

// ---- sim --------------------------------------------------------------------

std::string shell_quote(const std::string& s);  // defined below (check section)

namespace {
// The signal that ended a popen()ed command, or 0. A shell that exec()s its
// command leaves the signal in the wait status; one that forks reports it as
// exit status 128+N (the generated driver itself only exits 0, 1 or 2).
int terminating_signal(int status) {
  if (WIFSIGNALED(status)) {
    return WTERMSIG(status);
  }
  if (WIFEXITED(status) && WEXITSTATUS(status) > 128 && WEXITSTATUS(status) < 128 + NSIG) {
    return WEXITSTATUS(status) - 128;
  }
  return 0;
}

std::string signal_name(int signal) {
  switch (signal) {
    case SIGILL : return "SIGILL";
    case SIGSEGV: return "SIGSEGV";
    case SIGFPE : return "SIGFPE";
    case SIGBUS : return "SIGBUS";
    case SIGABRT: return "SIGABRT";
    case SIGTRAP: return "SIGTRAP";
    case SIGKILL: return "SIGKILL";
    case SIGTERM: return "SIGTERM";
    case SIGINT : return "SIGINT";
    default     : return std::format("signal {}", signal);
  }
}

std::string signal_description(int signal) {
  const char* text = ::strsignal(signal);
  return text != nullptr ? std::string{text} : std::string{"unknown signal"};
}
}  // namespace

// The standalone runtime of the LLVM testbench flow: `lhd_simrt`, built beside `lhd` (a data dependency of it).
// A separate small executable on purpose: starting the compiler binary costs over a second on its own.
std::string simrt_path() {
  if (const char* env = std::getenv("LHD_SIMRT"); env != nullptr && ::access(env, X_OK) == 0) {
    return fs::absolute(env).string();
  }
  const std::string exe_dir = livehd::file_utils::get_exe_path();  // the directory holding lhd, symlinks resolved
  for (const auto& cand : {exe_dir + "/lhd_simrt", exe_dir + "/lhd.runfiles/_main/lhd/lhd_simrt", exe_dir + "/../lhd_simrt"}) {
    if (::access(cand.c_str(), X_OK) == 0) {
      return fs::absolute(cand).string();
    }
  }
  throw Lhd_error{"dependency", "lhd_simrt (the LLVM testbench runtime) not found next to lhd", "build //lhd:lhd_simrt or set LHD_SIMRT=/path/to/lhd_simrt"};
}

// `lhd sim` through the LLVM testbench flow: nothing is host-compiled. The DUT kernels and the testbench
// object (already written by the setup) are loaded and run by `lhd_simrt SIMDIR` (lhd_sim_llvm_rt.cpp),
// which prints what the old driver printed and exits with its verdict codes.
struct Llvm_tb_cfg {
  std::string ckpt_dir, ckpt_min_secs, ckpt_max, ckpt_max_overhead, ckpt_every;
  bool        vcd_on = false;
  std::string vcd_dir;
  bool        unknown_zero = false;  // an explicit compile.unknown_zero=true: every `?` bit is 0 (DUT and testbench)
};

namespace {

std::string read_sim_sidecar(const std::string& path) {
  std::ifstream in(path);
  std::string   json{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  if (const auto last = json.find_last_not_of(" \t\r\n"); last != std::string::npos) {
    json.resize(last + 1);
  } else {
    json.clear();
  }
  return json;
}

bool wants_observation(const Options& opts) {
  return opts.sim_list_signals || !opts.sim_probe.empty() || !opts.sim_break_when.empty() || !opts.sim_query.empty();
}

std::string prepare_query_file(const std::string& spec, const std::string& simdir) {
  if (spec.empty()) {
    return {};
  }
  const auto first     = spec.find_first_not_of(" \t\r\n");
  const bool is_inline = first != std::string::npos && spec[first] == '{';
  if (!is_inline && spec != "-") {
    if (!fs::is_regular_file(spec)) {
      throw Lhd_error{"missing_file", std::format("--query file '{}' does not exist", spec), ""};
    }
    return fs::absolute(spec).string();
  }
  const auto    path = simdir + "/sim_query_request.json";
  std::ofstream out(path);
  if (is_inline) {
    out << spec;
  } else {
    out << std::cin.rdbuf();
  }
  out.close();
  if (!out) {
    throw Lhd_error{"usage", "could not read the query request or write " + path, ""};
  }
  return path;
}

std::pair<int, std::string> run_sim_replay(const std::string& command) {
  std::string out;
  FILE*       stream = ::popen((command + " 2>&1").c_str(), "r");
  if (stream == nullptr) {
    return {-1, "could not launch the replay runtime"};
  }
  char buf[4096];
  while (const auto count = std::fread(buf, 1, sizeof buf, stream)) {
    out.append(buf, count);
  }
  const int status = ::pclose(stream);
  if (const auto last = out.find_last_not_of("\r\n"); last != std::string::npos) {
    out.resize(last + 1);
  }
  return {WIFEXITED(status) ? WEXITSTATUS(status) : -1, std::move(out)};
}

}  // namespace

void run_llvm_testbench(Options& opts, Result& res, const std::string& file, const std::string& simdir, const std::string& simroot,
                        const std::string& test_sel, bool init_zero, const std::string& warn_undefined, bool pretty,
                        const Llvm_tb_cfg& cfg) {
  if (!fs::exists(std::format("{}/{}", simdir, prp_tb::kManifest))) {
    res.status        = "fail";
    res.error_class   = "usage";
    res.error_message = std::format("no generated sim testbench in {} (run --setup-only --workdir {} first)", simdir, simroot);
    res.exit_code     = exit_code_for(res.error_class);
    return;
  }
  const std::string runtime = shell_quote(simrt_path()) + " " + shell_quote(simdir);
  std::string       run_config;
  if (opts.seed_explicit) {
    run_config += " --seed " + shell_quote(opts.seed);
  }
  if (init_zero) {
    run_config += " --init-zero";
  }
  if (cfg.unknown_zero) {
    run_config += " --set sim.unknown_zero=true";
  }
  if (!warn_undefined.empty()) {
    run_config += " --set sim.warn_undefined=" + warn_undefined;
  }
  if (!cfg.ckpt_dir.empty()) {
    run_config += " --ckpt-dir " + shell_quote(cfg.ckpt_dir);
  } else {
    run_config += " --no-checkpoint";
  }
  for (const auto& arg : opts.sim_plusargs) {
    run_config += " " + shell_quote(arg);
  }
  const auto  selection      = test_sel.empty() ? std::string{} : " --test " + shell_quote(test_sel);
  const auto  sim_tests_path = simdir + "/sim_tests.json";
  const auto  sim_debug_path = simdir + "/sim_debug.json";
  const auto  sim_query_path = simdir + "/sim_query.json";
  const bool  observe        = wants_observation(opts);
  std::string observation_args;
  if (observe) {
    std::error_code ec;
    fs::remove(sim_debug_path, ec);
    fs::remove(sim_query_path, ec);
    if (opts.sim_list_signals) {
      observation_args += " --list-signals";
    }
    if (!opts.sim_probe.empty()) {
      observation_args += " --probe " + shell_quote(opts.sim_probe) + " --probe-from " + std::to_string(opts.sim_probe_from)
                          + " --probe-to " + std::to_string(opts.sim_probe_to);
    }
    if (!opts.sim_break_when.empty()) {
      observation_args += " --break-when " + shell_quote(opts.sim_break_when);
    }
    if (!opts.sim_query.empty()) {
      observation_args += " --query-file " + shell_quote(prepare_query_file(opts.sim_query, simdir));
    }
    const auto listed_only
        = opts.sim_list_signals && !cfg.vcd_on && opts.sim_probe.empty() && opts.sim_break_when.empty() && opts.sim_query.empty();
    const std::string mode   = listed_only ? " --debug-json " + shell_quote(sim_debug_path) : " --check-observation";
    const auto [status, msg] = run_sim_replay(runtime + selection + observation_args + mode);
    if (status != 0) {
      throw Lhd_error{"usage", msg.empty() ? "the simulator could not prepare observation" : msg, ""};
    }
    if (listed_only) {
      res.sim_debug_json = read_sim_sidecar(sim_debug_path);
      res.outputs.push_back(simdir);
      return;
    }
    observation_args += " --debug-json " + shell_quote(sim_debug_path) + " --query-json " + shell_quote(sim_query_path);
  }

  {
    std::error_code ec;
    fs::remove(sim_tests_path, ec);
  }
  std::string run_args = runtime + selection + run_config + " --result-json " + shell_quote(sim_tests_path);
  if (!cfg.ckpt_dir.empty()) {
    if (!cfg.ckpt_min_secs.empty()) {
      run_args += " --checkpoint-min-secs " + shell_quote(cfg.ckpt_min_secs);
    }
    if (!cfg.ckpt_max.empty()) {
      run_args += " --checkpoint-max " + shell_quote(cfg.ckpt_max);
    }
    if (!cfg.ckpt_max_overhead.empty()) {
      run_args += " --checkpoint-max-overhead " + shell_quote(cfg.ckpt_max_overhead);
    }
    if (!cfg.ckpt_every.empty()) {
      run_args += " --checkpoint-every " + shell_quote(cfg.ckpt_every);
    }
  }
  if (opts.sim_restart_cycle >= 0) {
    run_args += " --restart-cycle " + std::to_string(opts.sim_restart_cycle);
  }
  int         rc        = 0;
  int         killed_by = 0;
  std::string out;
  {
    Phase_timer run_phase(res, "sim.run");
    FILE*       p = ::popen(run_args.c_str(), "r");
    if (p == nullptr) {
      rc = -1;
    } else {
      char   buf[4096];
      size_t n = 0;
      while ((n = std::fread(buf, 1, sizeof buf, p)) > 0) {
        out.append(buf, n);
      }
      const int st = ::pclose(p);
      rc           = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
      killed_by    = terminating_signal(st);
    }
  }
  res.sim_tests_json = read_sim_sidecar(sim_tests_path);
  if ((observe || cfg.vcd_on) && (rc == 0 || rc == 1)) {
    Phase_timer replay_phase(res, "sim.replay");
    const auto  jobs          = livehd::Job_manager::global().ceiling();
    const auto  replay_config = run_config + " --jobs " + std::to_string(jobs) + " --replay-results " + shell_quote(sim_tests_path);
    rapidjson::Document recorded;
    recorded.Parse(res.sim_tests_json.c_str());
    if (!recorded.IsArray()) {
      throw Lhd_error{"internal", "the simulation did not write results for replay", ""};
    }
    for (const auto& test : recorded.GetArray()) {
      const std::string      name   = test["test"].GetString();
      const std::string_view status = test["status"].GetString();
      if (status == "error") {
        continue;
      }
      std::string cmd         = runtime + " --test " + shell_quote(name) + replay_config + observation_args;
      const bool  failure_vcd = cfg.vcd_on && opts.sim_vcd_on_fail && test.HasMember("cycle") && test["cycle"].GetInt64() >= 0;
      const bool  vcd         = cfg.vcd_on && (!opts.sim_vcd_on_fail || failure_vcd);
      long        from = std::max(0L, opts.sim_vcd_from), to = opts.sim_vcd_to;
      if (failure_vcd) {
        to   = test["cycle"].GetInt64();
        from = std::max(0L, to - opts.sim_vcd_fail_window);
      }
      if (vcd) {
        cmd += " --vcd-out " + shell_quote(cfg.vcd_dir + "/" + name + ".vcd") + " --vcd-from " + std::to_string(from) + " --vcd-to "
               + std::to_string(to);
      } else if (!observe) {
        continue;
      }
      const auto [replay_status, message] = run_sim_replay(cmd);
      if (replay_status != 0) {
        const auto error = std::format("replay of {} failed{}", name, message.empty() ? "" : ": " + message);
        if (rc == 0) {
          throw Lhd_error{replay_status == 2 ? "usage" : "internal", error, ""};
        }
        std::fprintf(stderr, "lhd sim: %s\n", error.c_str());
        continue;
      }
      if (pretty && !message.empty()) {
        out += message + "\n";
      }
      if (failure_vcd) {
        std::fprintf(stderr,
                     "lhd sim: %s failed at clock %ld -> wrote failure VCD (cycles %ld..%ld)\n",
                     name.c_str(),
                     to,
                     from,
                     to);
      }
    }
    if (observe) {
      res.sim_debug_json = read_sim_sidecar(sim_debug_path);
      res.sim_query_json = read_sim_sidecar(sim_query_path);
    }
  }
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
    return;
  }
  res.status = "fail";
  if (rc == 2) {
    res.error_class   = "usage";
    res.error_message = (!test_sel.empty() && n_run == 0) ? std::format("no test matched '{}' in {}", test_sel, file)
                                                          : std::format("the sim runtime rejected an argument in {}", file);
  } else if (killed_by != 0) {
    res.error_class   = "assert";
    res.error_message = std::format("the sim runtime was killed by signal {} ({}) after {} test verdict(s)", signal_name(killed_by),
                                    signal_description(killed_by), n_run);
  } else {
    res.error_class   = "assert";
    res.error_message = (n_fail > 0 && n_run > 0) ? std::format("{} of {} test(s) failed", n_fail, n_run)
                                                  : std::format("the sim runtime exited with code {}", rc);
  }
  res.exit_code = exit_code_for(res.error_class);
}

void sim_command(Options& opts, Result& res) {
  res.command             = "sim pyrope";
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
      test_sel = str_tools::canonical_escaped_path(f);  // `cnt`.`basic` selects cnt.basic
    }
  }
  if (sources.empty()) {
    throw Lhd_error{"usage", "sim requires a .prp file", "e.g. `lhd sim foo.prp` or `lhd sim foo.prp test.name`"};
  }
  // The LAST source is the primary, test-containing file by convention — but
  // accept either order: when the last source holds no `test` block and an
  // earlier positional does, that one is the testbench
  // (`lhd sim tb.prp dut.prp` behaves the same as `lhd sim dut.prp tb.prp`).
  std::string file = sources.back();
  if (sources.size() > 1) {
    std::vector<prp_sim::Test_info> probe;
    std::string                     perr;
    // A non-zero return means the probe FAILED — but list_tests conflates two
    // failures under rc=1: a real read/parse error (the user's problem, must
    // surface) and "parsed fine, holds no `test` block" (the REORDER SIGNAL —
    // the last positional is the DUT, `lhd sim tb.prp dut.prp`). Telling them
    // apart by the message keeps genuine errors loud while making both
    // argument orders work; treating both as fatal made tb-first invocations
    // die with "no test blocks found in <dut>.prp" before the reorder scan
    // ever ran.
    const int                       rc            = prp_sim::list_tests(file, "", probe, perr);
    const bool                      last_testless = rc != 0 && perr.rfind("no test blocks found", 0) == 0;
    if (rc != 0 && !last_testless) {
      throw Lhd_error{"usage", perr.empty() ? std::format("cannot read `{}`", file) : perr, ""};
    }
    if (probe.empty()) {
      for (auto it = std::next(sources.rbegin()); it != sources.rend(); ++it) {
        probe.clear();
        perr.clear();
        if (prp_sim::list_tests(*it, "", probe, perr) == 0 && !probe.empty()) {
          file = *it;
          break;
        }
      }
    }
  }
  const bool setup_only = opts.sim_setup_only;
  const bool run_only   = opts.sim_run_only;
  const bool list_only  = opts.list_tests;
  const bool pretty     = opts.diag_fmt == Diag_fmt::pretty;
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
    // Absolute, so the compile commands (and their up-to-date stamps) are the
    // same whether the build is started by the original command or by a replay
    // from inside the sim dir.
    std::error_code abs_ec;
    simroot = fs::absolute(opts.workdir, abs_ec).string();
    if (abs_ec) {
      simroot = opts.workdir;
    }
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

  // --set sim.vcd=true / --vcd-from / --vcd-on-fail: the VCD is not the simulator's business. The setup also writes a
  // trace object (`drv.tb.trace.o`) and, after the run, lhd replays it from the nearest checkpoint (`lhd_simrt
  // --vcd-out`), one `<workdir>/<test>.vcd` per test.
  bool eval_counts  = false;
  bool compile_only = false;
  bool vcd_on       = false;
  for (const auto& [k, v] : opts.sets) {
    if (k == "sim.compile_only") {
      compile_only = str_tools::parse_bool(v).value_or(false);
    } else if (k == "sim.eval_counts") {
      eval_counts = str_tools::parse_bool(v).value_or(false);
    } else if (k == "sim.dump_bitcode") {
      opts.sim_dump_bitcode = str_tools::parse_bool(v).value_or(false);
    } else if (k == "sim.vcd") {
      vcd_on = !(v == "false" || v == "0" || v == "off" || v.empty());
    }
  }
  if (run_only && opts.sim_dump_bitcode) {
    throw Lhd_error{"usage", "sim.dump_bitcode requires simulator setup", "use --setup-only to write LLVM bitcode"};
  }
  if (run_only && eval_counts) {
    const auto manifest = livehd::file_utils::read_file(simdir + "/" + std::string(prp_tb::kManifest));
    if (!manifest || manifest->find("\"eval_word\"") == std::string::npos) {
      throw Lhd_error{"usage",
                      "sim.eval_counts requires simulator setup",
                      "rerun --setup-only with --set sim.eval_counts=true"};
    }
  }
  if (opts.sim_vcd_from >= 0 || opts.sim_vcd_on_fail) {
    vcd_on = true;
  }
  opts.sim_observe          = vcd_on || !opts.sim_probe.empty() || !opts.sim_break_when.empty() || !opts.sim_query.empty();
  const std::string vcd_dir = vcd_on ? fs::absolute(simroot).string() : std::string{};

  // --set sim.checkpoint* : periodic native checkpoints of the DUT state and the testbench frame under
  // <simroot>/ckpt/<test>/c<cycle>/ (state.bin, frame.bin, meta.json). Default ON; a short run (< the min-secs floor)
  // writes none. The runtime owns the fork cadence and the prune.
  bool        ckpt_on   = true;
  bool        init_zero = false;
  std::string warn_undefined;
  bool        unknown_zero = false;
  std::string ckpt_min_secs, ckpt_max, ckpt_max_overhead, ckpt_every;
  for (const auto& [k, v] : opts.sets) {
    if (k == "sim.checkpoint") {
      ckpt_on = str_tools::parse_bool(v).value_or(false);
    } else if (k == "sim.checkpoint_min_secs") {
      ckpt_min_secs = v;
    } else if (k == "sim.checkpoint_max") {
      ckpt_max = v;
    } else if (k == "sim.checkpoint_max_overhead") {
      ckpt_max_overhead = v;
    } else if (k == "sim.checkpoint_every") {
      ckpt_every = v;
    } else if (k == "sim.init_zero") {
      init_zero = str_tools::parse_bool(v).value_or(false);
    } else if (k == "sim.warn_undefined") {
      warn_undefined = str_tools::parse_bool(v).value_or(false) ? "true" : "false";
    } else if (k == "compile.unknown_zero") {
      unknown_zero
          = str_tools::parse_bool(v).value_or(false);  // the testbench's `?` literals too (the DUT side gets the same flag)
    }
  }
  const std::string ckpt_dir = ckpt_on ? (fs::absolute(simroot).string() + "/ckpt") : std::string{};
  opts.sim_runtime_support   = ckpt_on || opts.sim_observe || opts.sim_list_signals;

  // Debug-flag sanity: catch contradictory combinations up front instead of silently producing a degenerate run.
  if (opts.sim_vcd_to >= 0 && opts.sim_vcd_from < 0) {
    throw Lhd_error{"usage", "--vcd-to needs --vcd-from (the window start)", "e.g. --vcd-from 100 --vcd-to 140"};
  }
  if (opts.sim_vcd_from >= 0 && opts.sim_vcd_to >= 0 && opts.sim_vcd_from > opts.sim_vcd_to) {
    throw Lhd_error{"usage", std::format("--vcd-from {} is after --vcd-to {}", opts.sim_vcd_from, opts.sim_vcd_to), ""};
  }
  if ((opts.sim_probe_from >= 0 || opts.sim_probe_to >= 0) && opts.sim_probe.empty()) {
    throw Lhd_error{"usage", "--probe-from and --probe-to require --probe", ""};
  }
  if (opts.sim_probe_to >= 0 && opts.sim_probe_from > opts.sim_probe_to) {
    throw Lhd_error{"usage", "--probe-from is after --probe-to", ""};
  }
  if (ckpt_dir.empty() && opts.sim_restart_cycle >= 0) {
    throw Lhd_error{"usage",
                    "--restart-cycle needs checkpoints — do not combine it with --set sim.checkpoint=false",
                    "run once with checkpointing on to create them, then --restart-cycle"};
  }

  std::vector<prp_sim::Test_info> tests;

  // A failed setup must not leave a runnable simulator behind: in a reused --workdir, --run-only would otherwise run
  // the PREVIOUS setup's testbench object and report PASS for sources that no longer compile. A failing setup deletes
  // the old object and records its error in kSetupFailed, so --run-only can report the real setup error.
  static constexpr std::string_view kSetupFailed = "setup_failed.txt";
  const std::string                 setup_failed = std::format("{}/{}", simdir, kSetupFailed);
  struct Setup_outcome {
    const std::string& marker;
    const Result&      res;
    const int          uncaught = std::uncaught_exceptions();
    std::string        thrown_cls;  // an exception the setup threw (it never reaches res here)
    std::string        thrown_msg;
    ~Setup_outcome() {
      std::error_code ec;
      if (res.status == "pass" && std::uncaught_exceptions() == uncaught) {
        fs::remove(marker, ec);
        return;
      }
      const auto dir = fs::path(marker).parent_path();
      fs::remove(dir / prp_tb::kObject, ec);
      fs::remove(dir / prp_tb::kTraceObject, ec);
      fs::remove(dir / prp_tb::kManifest, ec);
      std::ofstream out(marker, std::ios::trunc);
      const bool    from_res = res.status != "pass";
      const auto    cls      = from_res ? res.error_class : thrown_cls;
      const auto    msg      = from_res ? res.error_message : thrown_msg;
      out << (cls.empty() ? "internal" : cls) << '\n'
          << (msg.empty() ? std::string{"the setup did not complete (see its diagnostics)"} : msg) << '\n';
    }
  };

  // ---- setup: the DUT as native kernels (pass.lnast_sim), the testbench as one object (prp_tb)
  std::optional<Setup_outcome> setup_outcome;
  if (!run_only) {
    ensure_dir(simdir);
    setup_outcome.emplace(setup_failed, res);
    opts.language = "pyrope";
    opts.files    = sources;  // compile ALL positional sources (imports resolve across them)
    // `sim` is DYNAMIC verification: each `test` block's asserts are checked by running it, not formally. Skip
    // pass.formal — a `test` lowers to a never-instantiated comb whose runtime parameters become free inputs, so a
    // concrete-valued assert (`assert(acc == 22)`) would otherwise be "refuted" over those free inputs even though the
    // bound run satisfies it.
    opts.sets.emplace_back("compile.formal.mode", "none");
    opts.emit_dirs.push_back(Typed_path{"sim", simdir});
    try {
      auto ir = gather_ir_inputs(opts, "sim");
      compile_sources(opts, res, ir);
    } catch (const Lhd_error& e) {
      setup_outcome->thrown_cls = e.cls;
      setup_outcome->thrown_msg = e.msg;
      throw;
    } catch (const std::exception& e) {
      const auto classified     = classify_engine_failure(e.what());  // what lhd_main reports for it
      setup_outcome->thrown_cls = classified.cls;
      setup_outcome->thrown_msg = classified.msg;
      throw;
    }
    if (res.status != "pass") {
      return;
    }
    std::string err;
    int         gen_rc = 0;
    {
      Phase_timer tbgen(res, "sim.tbgen");  // the testbench object (inou/prp/tb_llvm)
      gen_rc = prp_tb::generate(file,
                                simdir,
                                test_sel,
                                tests,
                                err,
                                opts.sim_observe,
                                opts.sim_dump_bitcode,
                                eval_counts,
                                opts.incremental ? std::optional{livehd::kCompileSrcSalt} : std::nullopt);
    }
    if (gen_rc != 0) {
      res.status        = "fail";
      res.error_class   = "unsupported";
      res.error_message = err;
      res.exit_code     = exit_code_for(res.error_class);
      return;
    }
    record_invocation(opts, "sim", simroot, simdir);
    res.recipe_steps.push_back(std::format("sim setup: {} test(s) in {}", tests.size(), simdir));
    setup_outcome.reset();  // the setup passed: clear the marker now, before any test outcome
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
  if (run_only && fs::exists(setup_failed)) {
    std::ifstream fin(setup_failed);
    std::string   klass;
    std::getline(fin, klass);
    std::string msg{std::istreambuf_iterator<char>(fin), std::istreambuf_iterator<char>()};
    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
      msg.pop_back();
    }
    res.status        = "fail";
    res.error_class   = klass.empty() ? "internal" : klass;
    res.error_message = std::format("the last --setup-only in {} failed: {}", simroot, msg);
    res.error_hint    = "fix that error and rerun --setup-only; --run-only never runs a simulator older than the sources";
    res.exit_code     = exit_code_for(res.error_class);
    return;
  }
  if (compile_only) {
    return;  // nothing is built any more: the setup is the whole compile
  }

  Llvm_tb_cfg cfg;
  cfg.ckpt_dir          = ckpt_dir;
  cfg.ckpt_min_secs     = ckpt_min_secs;
  cfg.ckpt_max          = ckpt_max;
  cfg.ckpt_max_overhead = ckpt_max_overhead;
  cfg.ckpt_every        = ckpt_every;
  cfg.vcd_on            = vcd_on;
  cfg.vcd_dir           = vcd_dir;
  cfg.unknown_zero      = unknown_zero;
  run_llvm_testbench(opts, res, file, simdir, simroot, test_sel, init_zero, warn_undefined, pretty, cfg);
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
  if (const auto dir = find_header_in_runfiles("inou/yosys/lgcheck"); !dir.empty()) {
    const auto cand = dir + "/lgcheck";
    if (::access(cand.c_str(), X_OK) == 0) {
      return fs::absolute(cand).string();
    }
  }
  auto exe_dir = livehd::file_utils::get_exe_path();
  for (const auto& cand : {std::string{"./inou/yosys/lgcheck"},
                           std::string{"inou/yosys/lgcheck"},
                           exe_dir + "/../inou/yosys/lgcheck",
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
  if (const auto dir = find_header_in_runfiles("inou/yosys/yosys2"); !dir.empty()) {
    const auto cand = dir + "/yosys2";
    if (::access(cand.c_str(), X_OK) == 0) {
      return fs::absolute(cand).string();
    }
  }
  auto exe_dir = livehd::file_utils::get_exe_path();
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
  // Concatenating with `ofs << ifs.rdbuf()` is wrong twice: inserting an EMPTY
  // streambuf sets failbit and silently drops every later module written to the
  // same stream, and a source without a trailing newline glues its `endmodule`
  // onto the next `module`.  Read, then append with a guaranteed separator.
  auto append_file = [](std::ofstream& ofs, const std::string& p) {
    std::ifstream      ifs(p);
    std::ostringstream oss;
    oss << ifs.rdbuf();
    const auto text = oss.str();
    if (text.empty()) {
      return;
    }
    ofs << text;
    if (text.back() != '\n') {
      ofs << '\n';
    }
  };

  auto out = std::format("{}/check_{}.v", workdir(opts), side);
  if (kind == "verilog") {
    res.inputs.push_back(path);
    check_inputs_exist({path});
    if (opts.libs.empty()) {
      return path;
    }
    std::ofstream ofs(out);
    append_file(ofs, path);
  } else {
    Eprp_var var;
    load_side_graphs(opts, res, kind, path, side, var);  // lg/pyrope/ln -> graphs (throws if empty)
    auto          scratch  = std::format("{}/check_{}", workdir(opts), side);
    const auto&   side_top = side == "ref" ? opts.ref_top : opts.impl_top;
    auto          names    = cgen_into(opts, res, var, scratch, false, side_top.empty() ? opts.top : side_top);
    std::ofstream ofs(out);
    for (const auto& n : names) {
      append_file(ofs, std::format("{}/{}.v", scratch, livehd::unit_file_stem(n)));
    }
  }

  // The in-process solvers resolve mapped cells directly from --lib LGraphs.
  // lgcheck consumes Verilog, so materialize those same model definitions into
  // both sides.  Without this, every standard cell is an undefined module and
  // the Yosys backend reports SETUP FAILED before it can compare behavior.
  // (Both sides produce the SAME model text, so this repeats one load+cgen per
  // run; caching it must NOT go through the reusable --workdir, where a stale
  // file from an earlier run with a different --lib would silently win.)
  auto lib_opts = library_model_opts(opts);  // the model library is not a proof side
  for (size_t i = 0; i < opts.libs.size(); ++i) {
    const auto& lp = opts.libs[i];
    if (lp.kind != "lg") {
      throw Lhd_error{"usage", std::format("lec --lib expects lg:DIR, got '{}:'", lp.kind), ""};
    }
    Eprp_var lib_var;
    auto     lib_side = std::format("{}_lib{}", side, i);
    load_side_graphs(lib_opts, res, lp.kind, lp.path, lib_side, lib_var);
    auto          scratch = std::format("{}/check_{}", workdir(opts), lib_side);
    auto          names   = cgen_into(lib_opts, res, lib_var, scratch);
    std::ofstream ofs(out, std::ios::app);
    for (const auto& n : names) {
      append_file(ofs, std::format("{}/{}.v", scratch, livehd::unit_file_stem(n)));
    }
  }
  return out;
}

}  // namespace lhd
