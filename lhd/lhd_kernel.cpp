//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Public engine initialization, option discovery, and command dispatch.

#include <algorithm>
#include <format>
#include <vector>

#include "diag.hpp"
#include "lhd_kernel_internal.hpp"
#include "pass.hpp"
#include "synth_policy.hpp"

namespace lhd {

// ---- public entry points ----------------------------------------------------

// Resolve an abbreviated --set/--config key to its canonical
// "<passtoken>.<flag>" form (2h-set_path). `ctx` is the command-path
// established by the command words to the LEFT of the flag, dotted
// (e.g. "pass.abc" after `lhd pass abc`, "" before any command word). Tries
// the key as-is, then prepends successively shorter prefixes of `ctx`
// (longest first); the first candidate whose leading segment(s) name a known
// pass (kSetPasses) wins. Returns the key unchanged when nothing resolves, so
// check_known_set_passes still emits the standard unknown-pass error. Uses
// only the constexpr kSetPasses table, so it is safe before init_engine().
std::string canonical_set_key(std::string_view key, std::string_view ctx) {
  if (key == "pass.satopt") {
    return std::string{key};
  }
  // `<channel>.log=<level>` is the developer-logging namespace (livehd::log),
  // orthogonal to the pass-flag registry — a channel name (e.g. `upass`,
  // `cprop`) is NOT a pass name, so it must never collect a command-path
  // prefix. Leave any `.log` key verbatim for apply_log_settings.
  if (key.size() > 4 && key.substr(key.size() - 4) == ".log") {
    return std::string{key};
  }
  // The `sim.*` command namespace (sim_command + the cgen.sim codegen labels,
  // kSimSetOptions) is its own vocabulary. Like `.log`, it must never collect
  // a command-path prefix under a `compile`/describe context -- and EVERY sim
  // key stays verbatim, registered or not, so a renamed spelling
  // (sim.color_dirty) reaches its directed rename error and a typo its
  // unknown-sim-flag error instead of resolving somewhere else.
  if (key.size() > 4 && key.substr(0, 4) == "sim.") {
    return std::string{key};
  }
  // Same for the `synth.*` command namespace (synth_command, kSynthSetOptions).
  if (key.size() > 6 && key.substr(0, 6) == "synth.") {
    // An unknown synth.* command option must not be reinterpreted as a flag
    // of some pass: pass flags take their explicit pass prefix.
    return std::string{key};
  }
  // A REMOVED namespace (kRenamedSetPasses: `lec.*`, `compile.sim.*`, ...) is
  // kept verbatim so check_known_set_passes raises its directed "was removed"
  // error. Without this, `lec.solver` under a `formal.lec` command path would
  // collect the prefix and resolve to `formal.lec.solver` -- the deliberate
  // removal diagnostic would silently disappear.
  if (auto pos = key.rfind('.'); pos != std::string_view::npos) {
    auto ns = key.substr(0, pos);
    for (const auto& rn : kRenamedSetPasses) {
      if (rn.old_ns == ns) {
        return std::string{key};
      }
    }
  }
  auto names_a_pass = [](std::string_view candidate) {
    auto pos = candidate.rfind('.');
    return pos != std::string_view::npos && !set_pass_method(candidate.substr(0, pos)).empty();
  };
  if (names_a_pass(key)) {
    return std::string{key};
  }
  std::string prefix{ctx};
  while (!prefix.empty()) {
    std::string candidate = prefix + "." + std::string{key};
    if (names_a_pass(candidate)) {
      return candidate;
    }
    auto pos = prefix.rfind('.');
    if (pos == std::string::npos) {
      break;
    }
    prefix.resize(pos);
  }
  return std::string{key};
}

Lhd_error classify_engine_failure(std::string_view fallback_msg) {
  const auto& recs = livehd::diag::sink().records();
  for (auto it = recs.rbegin(); it != recs.rend(); ++it) {
    if (it->severity == livehd::diag::Severity::error) {
      // A REFUTED-cex (a real design counterexample) exits 10 from every
      // command (owner ruling 2026-10-14), including a pass that reports it
      // through a diag: `pass.lec` (lec-refuted-cex) and the pass.abc /
      // pass.usyn tuning validation (synth-tune-refuted-cex). The code, not the
      // category, names it, so every other diag keeps its category mapping.
      if (std::string_view{it->code}.ends_with("-refuted-cex")) {
        return Lhd_error{"equiv_fail", it->message, it->hint};
      }
      return Lhd_error{map_diag_category(it->category), it->message, it->hint};
    }
  }
  return Lhd_error{"internal", std::string{fallback_msg}, ""};
}

void init_engine() {
  static bool done = false;  // engine commands and `lhd list options`/`describe pass.flag` may both reach here
  if (done) {
    return;
  }
  done = true;
  for (const auto& it : Pass_plugin::get_registry()) {
    it.second();
  }
#if LIVEHD_WITH_ABC
  // The optional Yosys integration links ABC through its own kernel.
  setup_inou_yosys();
#endif
}

std::vector<Set_option> list_set_options() {
  init_engine();
  std::vector<Set_option> out;
  out.push_back(Set_option{"pass.satopt",
                           "pass.satopt",
                           "false",
                           "Enable proof-backed logic simplification in the compile step. Default false for every command, "
                           "including synthesis and LEC. Explicit true/false overrides the default. "
                           "pass.satopt.stages narrows the searches when enabled (default: all)."});
  for (const auto& sp : kSetPasses) {
    if (sp.list == Set_pass::List::none) {
      continue;  // legacy alias spelling: accepted by --set, never listed
    }
    const auto* m = Pass::eprp.get_method(sp.method);
    if (m == nullptr) {
      continue;  // defensive: every kSetPasses method registers in init_engine
    }
    for (const auto& [flag, attr] : m->labels) {
      if (sp.method == "pass.lec" && flag == "jobs") {
        continue;
      }
      if (is_kernel_label(flag) || !retired_set_hint(sp.method, flag).empty()) {
        continue;
      }
      // `top` on any pass is the shared --top / lhd.top (set_top_label plumbs
      // it); a per-pass listing would advertise N spellings of one knob.
      if (flag == "top") {
        continue;
      }
      // Deprecated alias labels and kernel-plumbed internals stay functional
      // but leave the vocabulary listing.
      if (attr.help.starts_with("DEPRECATED") || attr.help.starts_with("INTERNAL")) {
        continue;
      }
      // The common/specific split keys on (method, flag), so a pass.formal
      // label that merely SHARES a name with a pass.lec one (timeout, reset)
      // is not hidden by collision.
      const bool is_common = set_flag_is_common(sp.method, flag);
      if (sp.list == Set_pass::List::common && !is_common) {
        continue;
      }
      if (sp.list == Set_pass::List::specific && is_common) {
        continue;
      }
      out.push_back(Set_option{std::format("{}.{}", sp.set_name, flag), std::string{sp.method}, attr.default_value, attr.help});
    }
  }
  // The `lhd.*` kernel namespace: shared, cross-pass settings the kernel folds
  // into Options (apply_lhd_settings), not labels of any single EPRP method.
  // Keep in sync with check_known_set_passes / apply_lhd_settings.
  out.push_back(Set_option{"lhd.jobs",
                           "lhd",
                           "0",
                           "maximum concurrent jobs across formal, LLVM compilation and USYN; 0 selects affinity-aware CPUs; load "
                           "and measured memory can lower admission"});
  out.push_back(Set_option{"lhd.seed",
                           "lhd",
                           "0",
                           "shared RNG seed for every pass that wants determinism (e.g. pass.color mincut); one seed per run"});
  out.push_back(
      Set_option{"lhd.top",
                 "lhd",
                 "",
                 "top module shared across passes; the canonical form of the --top flag (the flag wins if both are given). "
                 "Full internal `file.entity` name, or the bare entity when unique (resolves with a top-entity-fallback warning)"});
  out.push_back(Set_option{"lhd.stats",
                           "lhd",
                           "false",
                           "print the pass's aggregate statistics report; the canonical form of the --stats flag (either "
                           "spelling works). Consumed by the passes that have one (pass.color, pass.abc, pass.opentimer, "
                           "pass.semdiff, pass.lec). ABC/OpenTimer add one structured row per mapped color, with "
                           "resynth=1|0 carried through incremental ABC reuse. "
                           "For `lhd lec` / `lhd formal verify` (canonical knob lhd.stats) it prints a cvc5 "
                           "solve-insight report (problem size, conflicts, decisions, propagations, restarts, theory "
                           "lemmas, resource units, timings) and registers a cvc5 plugin that makes the solve ~8x "
                           "SLOWER -- a diagnosis tool, not something to leave on or to time a run with"});
  out.push_back(
      Set_option{"lhd.incremental",
                 "lhd",
                 "true",
                 "the ONE switch for every persistent reuse tier: the Pyrope compile cache (unchanged parse and lowered graph "
                 "units), pass.abc's per-region cache (abc_cache/), pass.opentimer's STA result cache (sta_cache/), and the "
                 "formal/lec verdict cache (formal_cache.json). Reuse "
                 "needs a user-named --workdir to live under, so this only matters with one. false forces an honest cold run "
                 "with byte-identical outputs (reuse is a speedup, never an oracle) while the --result-json telemetry keeps "
                 "reporting enabled=false. There is no per-tier switch"});
  out.push_back(Set_option{
      "compile.unroll",
      "compile",
      "false",
      "true|false: request per-iteration source expansion for benchmarking. Default "
      "false: an eligible loop is KEPT as one replicated instance (its body lifted to a generated definition), which is "
      "O(1) in the trip count for the front end and for hierarchical synthesis/LEC. Scalar and array carries use "
      "the same policy, with no trip-count threshold. A loop the lifter declines (a "
      "register written in the body, a carry without a declared type, a nested loop whose domain reads the index, …) "
      "unrolls either way, with the reason in the pass.upass log. Backends that cannot consume the compact form expand it "
      "themselves, so results are identical; only representation and speed differ"});
  out.push_back(Set_option{"compile.specialize_modules",
                           "compile",
                           "false",
                           "specialize constant instance inputs before cycle checking (up to 64 contexts per definition), including stateful modules; "
                           "retain the original roots and only their reachable specialized definitions. Useful for configurable "
                           "router modules whose generic feedback disappears under the top's constant configuration"});
  out.push_back(Set_option{"compile.flatten",
                           "compile",
                           "false",
                           "true|false: after the first cprop+bitwidth, splice every module instance that has a body into the "
                           "selected top (or each uninstantiated root) so constant propagation, dead-logic removal and "
                           "compile.sweep see across module ports, like a Yosys `flatten`. The original definitions are dropped "
                           "once nothing instantiates them. Black boxes (macros, Liberty cells) and rolled loops stay instances. "
                           "Hierarchy-based reuse (the per-module ABC cache) is lost, so use it on designs whose cost is the "
                           "hierarchy itself, not on well-factored ones"});
  out.push_back(Set_option{"compile.unknown_zero",
                           "compile",
                           "false",
                           "true|false: read every constant bit that is unknown (x: an undriven output, an unconnected input, "
                           "an explicit 'x) as 0 right after the first cprop+bitwidth, like Yosys `setundef -zero` run before "
                           "optimization instead of after it, so constants fold across what were x-driven ports. A legal "
                           "refinement of a don't-care"});
  out.push_back(Set_option{"compile.sweep",
                           "compile",
                           "false",
                           "true|false: after the last cprop+bitwidth round, iterate a state sweep (pass/sweep): flop bits "
                           "whose next state can only be their own value or their reset value are replaced by that constant, and "
                           "state bits no output can observe are removed, each round followed by cprop+bitwidth until nothing "
                           "changes. Treats a flop with no reset and no initial value as free to start at its constant. Best "
                           "with compile.flatten, since module ports otherwise hide both constants and unobserved bits"});
  out.push_back(Set_option{
      "compile.lnast_fmt",
      "compile",
#ifndef NDEBUG
      "true",
#else
      "false",
#endif
      "run the pass.lnastfmt compiler self-check after parsing; defaults on in debug builds and off in optimized builds"});
  out.push_back(
      Set_option{"compile.verify_frozen",
                 "compile",
#ifndef NDEBUG
                 "true",
#else
                 "false",
#endif
                 "freeze every legalized graph's structure and re-check it after the emits (pass.legalize); a full digest per "
                 "graph, twice, so it defaults on in debug builds and off in optimized builds"});
  // The `sim.*` command namespace (consumed by sim_command, not an EPRP method):
  // keep `lhd list options` / `lhd describe` complete. Single source of truth =
  // kSimSetOptions, which also drives check_known_set_passes / the sim --help block.
  for (const auto& s : kSimSetOptions) {
    if (std::string_view{s.help}.starts_with("DEPRECATED")) {
      continue;  // accepted spelling, canonical name listed instead
    }
    out.push_back(Set_option{std::format("sim.{}", s.name), "sim", std::string{s.default_value}, std::string{s.help}});
  }
  // The `synth.*` command namespace (synth_command), same single-source rule.
  for (const auto& s : kSynthSetOptions) {
    auto help = std::string{s.help};
    // Options that are also source attributes say so, so `lhd describe` is the
    // one place that documents both spellings (the registry is the same list
    // synth_attr::validate accepts).
    for (const auto& a : livehd::synth_attr::kAttributes) {
      if (a.key == s.name) {
        help = std::format("[source attribute ::[synth.{}=...] on: {}] ", a.key, a.scope) + help;
      }
    }
    out.push_back(Set_option{std::format("synth.{}", s.name), "synth", std::string{s.default_value}, std::move(help)});
  }
  std::sort(out.begin(), out.end(), [](const Set_option& a, const Set_option& b) { return a.name < b.name; });
  return out;
}

namespace {

// A run that names nowhere to keep its result is legitimate -- a syntax check, a
// `--stats` report -- but it is far more often a mistake, and the symptom (the
// command "did nothing") looks identical either way. Say it ONCE, up front,
// rather than after a long synthesis: the scratch workdir the kernel mints is
// never named, so nothing the run builds is reachable afterwards.
//
// `compile` and `synth` only: they exist to BUILD something. Every other
// command's output is its report (lec's verdict, sim's test results, tool's
// stdout, a pass's --stats), which a run with no workdir still delivers in
// full. `diagnostics:`/`results:` describe the run, not the design, so they do
// not count as somewhere to keep it.
void warn_no_artifacts(const Options& opts) {
  if (opts.command != "compile" && opts.command != "synth") {
    return;
  }
  // `workdir_scratch` because workdir() MUTATES opts.workdir: by the time this
  // runs, a workdir-less command has already been handed an unnamed mkdtemp
  // path, so a plain `workdir.empty()` test would never fire.
  if ((!opts.workdir.empty() && !opts.workdir_scratch) || !opts.dumps.empty()) {
    return;
  }
  const auto keeps_design = [](const std::vector<Typed_path>& v) {
    return std::any_of(v.begin(), v.end(), [](const Typed_path& t) { return t.kind != "diagnostics" && t.kind != "results"; });
  };
  if (keeps_design(opts.emits) || keeps_design(opts.emit_dirs)) {
    return;
  }
  livehd::diag::warn("lhd", "no-artifacts", "io")
      .msg("`lhd {}` was given neither --workdir nor an --emit/--emit-dir output: it runs, but nothing it builds is kept",
           opts.command)
      .hint(
          "--emit-dir lg:DIR (or verilog:DIR) keeps the result; --workdir DIR additionally keeps the intermediates and the "
          "per-pass logs")
      .emit();
}

}  // namespace

void run_engine_command(Options& opts, Result& res) {
  validate_emits(opts);
  validate_dumps(opts);
  check_known_set_passes(opts);  // --set AND --config table names: a typo'd pass must error, not no-op
  apply_log_settings(opts);      // --set <channel>.log=<level> -> livehd::log (developer logging)
  apply_lhd_settings(opts);      // --set lhd.seed / lhd.top -> the shared kernel Options fields

  // --depfile is supported on both frontends: the Verilog flow lists its
  // declared inputs, and the Pyrope flow additionally folds in every file the
  // Source_locator tables saw (the locator's file table IS the
  // actually-read-files list, covering import discovery).

  if (opts.command == "compile") {
    compile_command(opts, res);
  } else if (opts.command == "lec") {
    lec_command(opts, res);
  } else if (opts.command == "formal") {
    formal_command(opts, res);
  } else if (opts.command == "scan") {
    scan_command(opts, res);
  } else if (opts.command == "tool") {
    tool_command(opts, res);
  } else if (opts.command == "pass") {
    pass_command(opts, res);
  } else if (opts.command == "sim") {
    sim_command(opts, res);
  } else if (opts.command == "synth") {
    synth_command(opts, res);
  } else {
    throw Lhd_error{"usage", std::format("unknown command '{}'", opts.command), ""};
  }
  // AFTER the dispatch: each command opens with its own setup_diag, which
  // clears the sink -- a record emitted before that would print but never be
  // counted (the envelope would say "0 warnings" under its own warning line).
  warn_no_artifacts(opts);
}

}  // namespace lhd
