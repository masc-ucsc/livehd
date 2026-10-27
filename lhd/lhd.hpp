//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// lhd — the stateless, hermetic LiveHD CLI kernel.
// Contract: the LiveHD docs ("Stateless build-system mode").
//
// One invocation = one step: (declared inputs, config) -> (declared outputs,
// exit code). No @tag, no ~/.cache, no lock, no `latest` symlink. lhd drives
// the registered EPRP methods programmatically (Eprp::run_method_now) plus
// the direct C++ entry points (Lnast::dump, uPass_tolg::run,
// livehd::Hhds_graph_library). lhd is the only driver; `lhd pyrope lsp`
// serves the LSP.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lhd {

inline constexpr std::string_view kVersion = "0.1.0";

// One typed I/O slot: --in KIND:PATH / --emit KIND:PATH / --emit-dir KIND:DIR/
struct Typed_path {
  std::string kind;
  std::string path;
};

// Rendering of the stdout result envelope and the stderr diagnostic stream:
// machine JSONL or human text. `--diag-fmt auto` (the default) resolves at
// Options construction via default_diag_fmt(); a presentation choice only, so
// it is a top-level flag (never --set: those hash into the run_id).
enum class Diag_fmt { jsonl, pretty };

// pretty when stdout is a terminal, jsonl when piped/captured (agents, CI,
// pipelines) — clang/git-style isatty detection, overridable with --diag-fmt.
Diag_fmt default_diag_fmt();

struct Options {
  std::vector<std::string> invocation_argv;  // exact CLI tokens, before normalization/config expansion
  std::string              command;          // compile|lec|scan|pyrope|tool|pass|list|describe|version|help
  std::string              language;         // verilog|pyrope ("" for the IR/meta commands)

  std::vector<std::string> files;  // positional: source files / list pattern / describe name

  std::vector<Typed_path> emits;
  std::vector<Typed_path> emit_dirs;
  std::vector<Typed_path> ins;
  std::vector<Typed_path> in_dirs;
  // --lib KIND:DIR (repeatable): extra graph libraries used only to resolve
  // references, never elaborated as inputs. lec uses them to flatten Sub
  // instances (e.g. the gensim cell models behind an ABC standard-cell netlist).
  std::vector<Typed_path> libs;

  std::string top;
  // Shared across every pass that wants determinism (e.g. pass.color mincut):
  // the `lhd.seed` kernel field, set via `--set lhd.seed=N` or `--seed N`
  // (default 0). One seed for the whole run rather than a per-pass
  // `pass.X.seed` option. `lhd sim` forwards it to each test driver as `--seed`
  // (the driver seeds hlop's PRNG) only when `seed_explicit` — otherwise the
  // driver keeps its own default seed.
  unsigned    jobs          = 0;  // shared cross-domain ceiling; 0 = affinity-aware CPU count
  std::string seed          = "0";
  bool        seed_explicit = false;  // user passed `--seed`/`--set lhd.seed=`
  // Verilog front-end. Default `slang` — the direct inou.slang SV -> LNAST
  // front-end, so verilog joins the pyrope flow (ln:/lg: emits, in-process
  // lec). `--reader yosys-slang|yosys-verilog` overrides to the yosys path
  // (SV/Verilog -> LGraphs).
  std::string reader        = "slang";
  std::string depfile;
  // --unused-inputs PATH (compile): write the declared source-file positionals
  // whose contents did NOT reach the compiled closure (absent from every final
  // unit's Source_locator table, e.g. a .sv outside the --top hierarchy) — one
  // cwd(exec-root)-relative path per line, empty when everything was read; the
  // format Bazel's unused_inputs_list consumes for input pruning.
  std::string unused_inputs;
  std::string config;  // --config lhd.toml: pass-flag defaults (CLI --set wins)

  std::vector<std::pair<std::string, std::string>> sets;  // --set pass[.idx].flag=value

  // --dump WHAT (repeat or comma-separated): debug observables printed to
  // stderr at the named pipeline stage. parse = post-frontend LNAST,
  // lnast = post-upass LNAST, lg = textual LGraph node/edge dump. A dump
  // forces the stage that produces it (the screen twin of --emit-dir
  // lnast-dump:/lg:).
  std::vector<std::string> dumps;

  // `lhd tool` inspector: target = node|pin|edge|all ("" => all);
  // attr = explicit display column CSV ("" => per-target defaults); max = row
  // cap (0 = unlimited); hier = -1 unset (flat for cat/grep/diff, full for
  // tree), INT_MAX = bare --hier (all levels), else an explicit depth; hops =
  // focus radius around filter matches (reserved).
  std::string              tool_target;
  // `tool tree --target kind:<X>` (repeatable): node kinds to list inside each
  // module of the hierarchy — registers/memories that ride the same instance
  // tree. Empty => the bare instance tree (default). `kind:register` aliases
  // flop/fflop/latch, `kind:memory` aliases memory; any Ntype name (flop, mux,
  // sub, …) also matches exactly.
  std::vector<std::string> tool_kinds;
  std::string              tool_attr;
  int                      tool_max        = 200;
  int                      tool_hier       = -1;
  int                      tool_context    = 2;      // `tool diff -C n` text-line context
  bool                     tool_invert     = false;  // `tool grep -v`: keep records that do NOT match
  bool                     tool_match      = false;  // `tool diff --match`: visualize via the semdiff `match` attribute
  bool                     tool_structural = false;  // `tool diff --structural`: strict compile-cache H5 comparison

  // `--stats` (canonical `--set lhd.stats=true`): ask whichever pass runs for its
  // aggregate report. Meaning is per consumer: `pass semdiff` prints the
  // node/register/memory match report, `pass color` the partition-size report,
  // `pass abc` / `pass opentimer` one row per mapped color (including resynth), and
  // `lhd lec` / `lhd formal verify` (canonical knob `lhd.stats`) a cvc5
  // solve-insight report (problem size, conflicts = learned clauses, decisions,
  // propagations, restarts, theory lemmas, resource units, timings). The formal
  // consumer also registers a cvc5 plugin that makes the solve ~8x SLOWER, so it is a
  // diagnosis tool — never leave it on, and never time a run with it.
  bool stats = false;

  // Internal design-load policy: formal verify discharges hierarchy assumptions
  // with its own engine instead of the compile-time preflight.
  bool compile_formal_preflight = true;

  // `--set lhd.incremental=true|false` (default true): the ONE switch for every
  // persistent reuse tier -- the Pyrope compile cache, pass.abc's per-region
  // cache, and the formal/lec verdict cache. Reuse also needs a user-named
  // --workdir (the caches live under it; a scratch dir would start cold every
  // run), so `incremental` means "reuse when there is somewhere to keep it".
  // false forces an honest cold run with byte-identical outputs (reuse is a
  // speedup, never an oracle of record) while the telemetry keeps reporting
  // enabled=false, so a benchmark row can tell a disabled tier from an old
  // binary. There is deliberately no per-tier switch.
  bool incremental = true;

  std::string              impl_kind, impl_path, impl_top;  // lec --impl
  std::string              ref_kind, ref_path, ref_top;     // lec --ref
  std::string              formal_filter;                   // formal verify / lec: formal-block name glob
  // lec --collapse <def> (repeatable): module-def names the driver has already
  // proven equivalent, forced to the sound black-box path even when --lib could
  // flatten them (proven-module collapse — the parent stops re-solving them).
  std::vector<std::string> collapse;
  // lec --trust <def> (repeatable): module-def names ASSUMED equivalent without a
  // proof — the escape hatch for a cell the encoder cannot yet model (a Latch).
  // The driver skips proving them and force-blackboxes their instances, keeping
  // them boxed even through a refute's flat-confirm; a top proven under a
  // non-empty trust list is disclosed, never a silent unconditional pass.
  std::vector<std::string> trust;

  // `lhd pyrope fmt` formatter knobs (clang-format-like). Consumed only by the
  // pyrope command; harmless defaults elsewhere.
  bool        fmt_inplace = false;  // -i / --inplace: rewrite each input file
  std::string fmt_output;           // -o / --output FILE: write to FILE (one input)
  bool        fmt_human  = false;   // --mode human|ai: width-limited, aligned layout (ai, the default, has neither)
  int         fmt_indent = 2;       // --indent N: spaces per level
  int         fmt_width  = 0;       // --width N: wrap column, --mode human only (0 = unset: prpfmt's 132)
  bool        fmt_verify = false;   // --verify: re-parse the formatted output

  // Source-only repetition analysis (`pyrope style`).
  size_t style_min_repeats          = 7;
  size_t style_max_block_statements = 128;
  size_t style_max_findings         = 20;

  std::string result_json;
  std::string workdir;
  // Set by workdir() when it MINTED an ephemeral scratch dir because the user
  // named none. Every persistent-reuse gate (compile cache, abc_cache,
  // formal verdict cache) keys on "user-named workdir", so a command that
  // needs a scratch path BEFORE it runs a sub-flow (synth mints <scratch>/synth
  // and then compiles into it) must not turn reuse on by accident.
  bool        workdir_scratch = false;

  std::vector<std::string> raw_args;  // after `--` (elaborate verilog: raw slang args)
  // (message, hint) of each deprecated synth.* spelling canonicalize_synth_sets
  // rewrote; emitted after the command's setup_diag. Not an input: never hashed.
  std::vector<std::pair<std::string, std::string>> deprecated_sets;

  bool quiet   = false;
  bool verbose = false;

  // `--list-tests`: enumerate the source's TEST UNITS as JSON (or a human list in
  // pretty mode) and exit without building/proving anything. Shared by the two
  // test-bearing commands so they read the same: `sim` lists its `test` blocks,
  // `formal verify` lists its `formal` blocks (each is an independent test).
  bool list_tests = false;

  // `sim` command modifiers
  bool                     sim_setup_only = false;  // generate the C++ sim, do NOT build/run
  bool                     sim_run_only   = false;  // build/run an already-generated sim (needs --workdir), no regen
  // Ordered application arguments; signature defaults stay local to each test.
  std::vector<std::string> sim_plusargs;  // ordered +key[=value], including normalized --arg aliases
  // `sim` debug-replay flags (sim_checkpoint_debug_plan). The driver loads the
  // nearest checkpoint <= the target and resumes from there. -1 = not requested.
  long                     sim_restart_cycle   = -1;     // --restart-cycle N: jump to cycle N
  long                     sim_vcd_from        = -1;     // --vcd-from Y: trace VCD starting at cycle Y
  long                     sim_vcd_to          = -1;     // --vcd-to Z: trace VCD up to cycle Z (with --vcd-from)
  bool                     sim_vcd_on_fail     = false;  // --vcd-on-fail: re-run a failed test with a VCD of the failure region
  long                     sim_vcd_fail_window = 20;     // --vcd-fail-window N: cycles before the failure to trace
  // `sim` observability: query signal values without re-instrumenting (the driver
  // snapshots scalar signals by hierarchical name). Results land in the result
  // envelope's "debug" member (and `--result-json`).
  bool                     sim_list_signals    = false;  // --list-signals: enumerate observable signals, then exit
  std::string              sim_probe;                    // --probe SIG,...: per-cycle JSON trajectory of these signals
  long                     sim_probe_from = -1;          // --probe-from A
  long                     sim_probe_to   = -1;          // --probe-to B
  std::string              sim_break_when;               // --break-when 'SIG OP VALUE|SIG': first cycle the condition holds
  // `sim --query FILE|-|{inline}` (2f-sim): a BATCHED JSON request
  // ({schema_version:1, kind:"sim_query", queries:[...]}). Batching is what lets
  // the planner union every question's time range and answer them all from ONE
  // replay; the legacy flags above stay the low-ceremony spelling of the same
  // engine. Answers land in the envelope's "query" member.
  std::string              sim_query;
  bool                     sim_observe = false;  // setup-time hierarchical instrumentation needed by VCD/probe/query
  bool sim_runtime_support = true;  // generated checkpoint/probe/query methods; false only for a lean checkpoint-off setup
  // The RESOLVED `sim.tune.*` vector (explicit --set > sim.tune.file > the
  // workdir's tuned decision > default), set by `lhd sim` before it compiles
  // and handed to inou.cgen.sim as concrete labels. Deliberately NOT mirrored
  // into `sets`: those hash into the run_id and the compile-cache context, and
  // a store-derived value must move neither. `lhd compile --emit-dir sim:`
  // leaves it unresolved and resolves explicit + sim.tune.file itself.
  struct Sim_tune {
    bool     resolved   = false;
    bool     dirty      = true;  // the built-in L1 vector (sim_tune_vector.hpp kTuneDefault*)
    int64_t  fence      = 16;    // std::numeric_limits<int64_t>::max() = no fences ("none")
    uint64_t live_words = 256;
    bool     llvm       = true;  // an unset sim.tune.backend is llvm (kTuneDefaultLlvm)
  } sim_tune;

  Diag_fmt diag_fmt = default_diag_fmt();
};

// Process exit code for an error class. 0 is success and EVERY failure stays
// non-zero, so `cmd || handle` keeps working; the distinct values let a caller
// tell WHY without parsing stdout — notably `assert` (the tool worked, the
// DESIGN failed) apart from `usage`/`missing_file` (the invocation was wrong).
// Unknown and `internal` stay 1, the historical value.
int exit_code_for(std::string_view error_class);

// The structured result envelope (future_cli.md "Result schema"). Written as
// one JSON object to --result-json (else stdout).
struct Result {
  std::string command;          // e.g. "compile verilog"
  std::string status = "pass";  // pass|fail
  std::string run_id;           // content hash (deterministic, never wall clock)
  int         exit_code = 0;

  std::vector<std::string> inputs;
  std::vector<std::string> outputs;
  std::vector<std::string> recipe_steps;  // the expanded steps that actually ran

  // Per-phase wall clock (steady_clock), milliseconds, in COMPLETION order —
  // the result's "phases" member. A row is appended when its phase ENDS, so
  // today (no Phase_timer nests inside another) the array reads as execution
  // order; a future nested timer would land AFTER the phases it contains, and
  // would also double-count under the "the consumer sums" rule below, so keep
  // the timed regions disjoint. Deliberately SEPARATE from recipe_steps:
  // recipe_steps is the human-readable expanded recipe, carrying the
  // label-decorated step string ("pass.abc cache_dir:… recipe:…") plus purely
  // informational lines that never ran as a timed phase. phase_ms holds only
  // genuinely timed work, keyed by the BARE step name ("pass.abc"), so a
  // performance consumer can key on it. A name may repeat when a step runs
  // more than once in a pipeline (the array is ordered; the consumer sums).
  // Never part of run_id — a wall-clock value in a content hash breaks caching.
  std::vector<std::pair<std::string, double>> phase_ms;

  // Synthesis CLI envelope observation, including handled failures. The wall
  // interval starts at main entry and ends just before final result emission;
  // it encloses all command phases and publication. RSS is this process only,
  // never a claim about simultaneous memory across its child processes.
  struct Synthesis_invocation {
    bool     present               = false;
    double   wall_ms               = 0;
    uint64_t parent_peak_rss_bytes = 0;  // zero means unavailable
  } synthesis_invocation;

  // `lhd compile` incremental front-end accounting (docs/opt_loop_incr.md L8).
  // Present for a Pyrope source compile with a user-named --workdir, including
  // lhd.incremental=false (enabled=false + zero counters), so benchmark rows can
  // distinguish an honestly disabled cache from an old binary that reports no
  // cache telemetry. `redone_ms` is work on cache misses only; sync/lookup/store
  // have disjoint Phase_timer rows and never ride this counter.
  struct Compile_cache_stats {
    bool     present{false};
    bool     enabled{false};
    uint64_t hits{0};
    uint64_t misses{0};
    double   redone_ms{0.0};
    uint64_t store_failed{0};
    uint64_t refused{0};
  } compile_cache;

  // pass.abc's incremental region reuse (`lhd synth` / `lhd pass abc`),
  // harvested from the embedded qor report (harvest_abc_incremental) so the
  // envelope's `incremental` member carries EVERY reuse tier in one place --
  // what a stats report builder (../lhdsuite) reads, instead of digging the
  // counters out of the pass's own qor object. `regions` is the mapped region
  // count; `store_failed` the regions the cache could not snapshot (each one
  // re-runs ABC forever -- a bug, not a property of the design).
  struct Abc_incr_stats {
    bool     present{false};
    bool     enabled{false};  // the region cache ran (user --workdir + lhd.incremental); false = honest cold map
    uint64_t hits{0};
    uint64_t misses{0};
    double   hit_ms{0.0};
    double   miss_ms{0.0};
    uint64_t regions{0};
    uint64_t store_failed{0};
  } abc_incr;

  // pass.opentimer's incremental STA reuse (`lhd synth` / `lhd pass opentimer`),
  // harvested from the embedded sta report exactly like abc_incr. ONE analysis
  // per run, so `hits`/`misses` are 0/1 or 1/0; `digestable` is false when the
  // netlist could not be given a reproducible identity (an anonymous state
  // cell), which is the one way the tier is enabled and still never hits.
  struct Sta_incr_stats {
    bool     present{false};
    bool     enabled{false};
    uint64_t hits{0};
    uint64_t misses{0};
    bool     digestable{true};
    double   lookup_ms{0.0};
  } sta_incr;

  // `lhd lec`'s VERDICT, as a machine-readable value.
  //
  // The envelope's `status` is the process outcome, not the proof: a
  // TIMEOUT-time run, a TESTED-sampled run and a real proof ALL exit 0 with
  // `"status":"pass"` (by design -- running out of budget must not fail a
  // pair), so a consumer that reads only `status` records a non-proof AS a
  // proof. That is the exact conflation lec's verdict discipline exists to
  // prevent, and a tracker aggregating it silently promotes an unverified
  // design into a headline comparison (lhdtrack flows/lib/lec.py). Emitted for
  // every solver, alongside the human verdict line.
  //
  // The outcome words of the owner rulings 2026-10-14 (the table is
  // pass/lec/property_types.hpp; the same words as `lhd formal verify`):
  //   proven-full          an unbounded (inductive) proof                    exit 0
  //   proven-bounded       no difference for `bound` design cycles from reset exit 0
  //   tested-sampled       a sampled effort preset: `vectors` agreed         exit 0
  //   timeout-time         a clock limit stopped it; nothing claimed         exit 0 + warning
  //   timeout-memory       out of memory, caught; nothing claimed            exit 0 + warning
  //   refuted-cex          a real counterexample                             exit 10
  //   refuted-interface | refuted-unsupported | refuted-unchecked |
  //   refuted-vacuous | refuted-unexplained   no answer; the word names why  exit 7
  //                        (refuted-vacuous: nothing to compare, or a
  //                        contradictory assume set -- it proved nothing)
  struct Lec_verdict {
    bool        present{false};
    std::string verdict{};         // one of the outcome words above
    std::string effort{};          // requested formal.effort preset ("" = legacy)
    bool        sampled{false};    // input-vector sampling ran
    std::string effort_reached{};  // highest preset the sampled vectors satisfy ("" = none)
    // sample_capped: sampling queries that hit their per-query cap (no answer).
    uint64_t    sample_vectors{0}, sample_traces{0}, sample_partial{0}, sample_queries{0}, sample_capped{0};
    uint64_t    sample_solve_us{0};
    uint64_t    vectors{0};      // tested-sampled: the N of TESTED-sampled(N)
    std::string solver{};        // cvc5 | bitwuzla | lgyosys | …
    bool        bounded{false};  // proven-bounded: proven only to `bound` design cycles from reset
    int64_t     bound{0};        // the n of PROVEN-bounded(n), in design cycles
    // Independent oracle result; a native proof cannot stand in for this.
    // empty until invoked; proven-full | proven-bounded | refuted-cex |
    // incomplete (lgcheck could not decide) | failed (lgcheck did not run)
    std::string crosscheck_verdict{};
    int         crosscheck_exit_code{-1};
    bool        crosscheck_bounded{false};  // proven only by lgcheck's complete bounded miter
    int64_t     crosscheck_bound{0};        // that window, in native design cycles (formal.bound)
    // State the proof could NOT correspond, reported only when a def ended
    // weaker than PROVEN-full (bounded / undecided): the usual reason a
    // run stops at PROVEN-bounded(n). `reason` is semdiff's (no full match / ambiguous /
    // kind/init mismatch) or "uncertain pair dropped". `suggested_match` is a
    // HEURISTIC `formal.lec.match` string (the dropped pairs, then same width +
    // name similarity) — a starting point to try, never assumed by the prover.
    struct Unpaired_state {
      std::string def;
      std::string side;  // ref | impl
      std::string name;
      std::string reason;
      int64_t     bits{0};
    };
    std::vector<Unpaired_state> unpaired_state{};
    std::string                 suggested_match{};
    // A weak verdict (PROVEN-bounded / undecided) whose induction step failed from
    // an ARBITRARY state: the step counterexample, inputs then `| state: k=v`.
    // That state may be unreachable; a proven `assume` over it makes the step
    // inductive (suggestions6 1.6). Empty when not applicable.
    std::string                 induction_cex{};
  } lec;

  // `lhd formal verify`'s run VERDICT (its obligation rows folded by severity),
  // as a machine-readable value: the same lowercase outcome words as
  // `lec.verdict` above, for the same reason (a TIMEOUT-time or TESTED-sampled
  // run exits 0 too). formal_report.json carries the per-obligation rows.
  struct Formal_verdict {
    bool        present{false};
    std::string verdict{};  // one of the outcome words of Lec_verdict
    bool        bounded{false};
    int64_t     bound{0};  // the n of PROVEN-bounded(n), in design cycles
  } formal;

  // Internal hand-off from Tier A (source/LNAST sync) to Tier B (final LGraph  // Internal hand-off from Tier A (source/LNAST sync)
  // to Tier B (final LGraph restore/store). Not serialized; the public machine contract is the stats object above. A graph
  // inventory independently records this closure key, so a compile that fails after updating the parse cache cannot authorize a
  // stale pre-failure graph generation on the next run.
  std::string                                      compile_cache_scope;
  std::string                                      compile_cache_context;
  std::string                                      compile_cache_closure_key;
  std::vector<std::pair<std::string, std::string>> compile_cache_unit_keys;
  std::vector<std::pair<std::string, uint64_t>>    compile_cache_unit_interfaces;  // source interface hash per unit
  std::vector<std::string>                         compile_cache_clean_units;
  std::vector<std::string>                         compile_cache_restored_graphs;
  // Clean final graph bodies to overlay after a diagnostic-carrying partial
  // restore is refused and the complete pipeline runs live.
  std::vector<std::string>                         compile_cache_overlay_graphs;
  // Unit names of this scope's PRIOR generation (empty when none/incompatible),
  // plus the modules the library's ownership record lists (lg_owners_load). Ghost pruning may delete artifacts of a unit that left
  // the closure only when that unit provably belonged to this design's previous compile.
  std::vector<std::string>                         compile_cache_prior_units;
  // The graph library's ownership record (lg_owners_load): this design's key
  // (empty: the compile records no ownership) and the roots OTHER designs
  // recorded in the library, which the prune never deletes.
  std::string                                      lg_owner_design;
  std::vector<std::string>                         lg_foreign_claims;
  // [mark, end) is the half-open range of diag::sink().records() produced by the
  // GRAPH PIPELINE — upass, tolg, cprop, pass.formal — which is exactly the set
  // of stages a warm restore SKIPS, so it is what the generation must carry and
  // replay to stay diagnostic-equal. Both ends matter: before the mark is the
  // front end and the deferred-source materialization, and after the end are
  // the emits; all of those run on a warm restore too, so a record from either
  // side would be printed twice (or, since the cache key ignores the `--emit`
  // slots, replayed onto a run that never requested that emit).
  size_t                                           compile_cache_diag_mark = 0;
  // Set by graph_pipeline_and_emits once the pipeline stages are done. SIZE_MAX
  // ("not yet closed") keeps a path that stores without running the pipeline on
  // the old carry-everything behavior rather than silently carrying nothing.
  size_t                                           compile_cache_diag_end  = std::numeric_limits<size_t>::max();

  // `lhd scan` payload: a pre-serialized JSON array of per-file import lists,
  // embedded verbatim as the result's "scan" member.
  std::string scan_json;

  // `lhd sim` payload: a pre-serialized JSON array of per-test results
  // [{test,status,cycle,failing_assert,prp_file,line,msg}, ...] read back from
  // the driver's sidecar, embedded verbatim as the result's "tests" member.
  std::string sim_tests_json;

  // `lhd sim` observability payload (--list-signals / --probe / --break-when): a
  // pre-serialized JSON object {signals?, probe?, break?} read back from the
  // driver's debug sidecar, embedded verbatim as the result's "debug" member.
  std::string sim_debug_json;

  // `lhd sim --query` payload (2f-sim): the {schema_version, kind:"sim_query_result",
  // run, results} object embedded verbatim as the result's "query" member. Built
  // from the driver's <simdir>/sim_query.json sidecar merged with the queries the
  // kernel answered from the static catalog alone (`signals`), in REQUEST order.
  std::string sim_query_json;

  // `lhd sim` profile-guided tuning (sim.tune.*): the envelope's "sim_tune"
  // member, a pre-serialized JSON object built with a rapidjson Writer (applied
  // vector + per-knob source, this run's activity stats, trial / verdict,
  // pending step, convergence, the reproducing --set list). Present for every
  // `lhd sim` that reaches setup or run, enabled or not. `sim_tune_note` is its
  // one-line human summary for the pretty output.
  std::string sim_tune_json;
  std::string sim_tune_note;

  // `lhd pass abc` QoR payload (2opt-freq A): the qor.json sidecar content
  // (per-region + total mapped gates/area/critical delay, source-attributed),
  // embedded verbatim as the result's "qor" member.
  std::string qor_json;

  // pass.satopt's stage report (satopt::Report::json: per-stage state, cost,
  // candidates, proofs, rewrites and budget skips), embedded verbatim as the
  // result's "satopt" member when compile or `lhd pass satopt` ran it (one
  // merged report when a command runs it more than once, e.g. lec's sides).
  std::string satopt_json;

  std::string error_class;  // empty when status == pass (future_cli.md taxonomy)
  // The pinned diagnostic category of the error that failed the run
  // (core/diag.hpp: syntax|name|type|bitwidth|time|...), when one did: the
  // exit-code class folds every source-level category onto `syntax`, so an
  // overflow would otherwise read as a syntax error. Empty for kernel failures.
  std::string error_category;
  std::string error_message;
  std::string error_hint;

  size_t n_errors   = 0;
  size_t n_warnings = 0;
};

// Failure the kernel detects itself (usage/missing_file/config/unsupported/
// dependency/equiv_fail). Pass failures (std::exception out of Eprp/upass)
// are classified via the diag sink category instead.
struct Lhd_error {
  std::string cls;
  std::string msg;
  std::string hint;
};

// argv -> Options. Throws Lhd_error{usage,...} on malformed input.
Options parse_args(int argc, char** argv);

// Resolve an abbreviated --set/--config key to its canonical
// "<passtoken>.<flag>" form (2h-set_path), given the command-path context
// (the dotted command words to the LEFT of the flag, e.g. "pass.abc"; "" when
// the flag precedes any command word). Returns the key unchanged when it does
// not resolve. Uses only the constexpr pass-name table — no init_engine().
std::string canonical_set_key(std::string_view key, std::string_view ctx);

// Meta commands (list/describe/version/help). Most need no engine init;
// `list options` / `describe pass.flag` initialize the pass registry
// themselves (the option vocabulary lives on the registered EPRP labels).
bool is_meta_command(const Options& opts);
int  run_meta_command(const Options& opts);

// The `sim.*` command-namespace options. NOT pass labels — the `lhd sim`
// command (sim_command) consumes them directly — but they ride the same
// --set/--config syntax, so they must appear in `lhd list options` /
// `lhd describe` and validate like any pass flag. THIS array is their single
// source of truth: check_known_set_passes (validation), list_set_options (the
// `lhd list options` vocabulary), and the `lhd sim --help` options block all
// derive from it, so the three can never drift. `inline constexpr` so it is one
// shared definition across translation units.
struct Sim_set_option {
  // The value grammar enforced on --set:
  //   boolean      true|false|1|0|on|off
  //   non_neg_num  a non-negative number
  //   bool_or_file false|true|FILE (text, not checked)
  //   backend      auto|slop|llvm
  //   tri          auto|on|off; true|false|1|0 are accepted aliases (TOML `dirty = true`)
  //   tune_mode    auto|on|off exactly: a mode, not a boolean
  //   fence        auto|none|N, N a whole number in [0, 2^20]
  //   num_or_auto  auto|N, N a whole number in [1, 2^20] (0 is spelled `auto`)
  //   path         FILE/DIR text; empty = unset; the consumer checks it
  //   count        a whole number
  enum class Kind { boolean, non_neg_num, bool_or_file, backend, tri, tune_mode, fence, num_or_auto, path, count };
  // Where the value takes effect: lhd itself, the generated C++ (baked into
  // drv.bin, so a --run-only value must match what setup baked -- drv.bin's
  // `--set` parser checks it), the run (a drv.bin argument), or both.
  enum class Stage { lhd, codegen, run, both };
  // The sim_profile.md §3 trial-cost class of a TUNABLE knob (none = not one):
  // R = a drv.bin argument, H = a host rebuild, G = regenerated C++.
  enum class Tune { none, perf_r, perf_h, perf_g };
  std::string_view name;           // flag under sim.*, e.g. "checkpoint_min_secs" or "tune.dirty"
  std::string_view default_value;  // shown by `lhd list options`
  Kind             kind;
  std::string_view help;  // full help (also `lhd describe sim.flag`)
  Stage            stage  = Stage::lhd;
  Tune             tune   = Tune::none;
  std::string_view tv_key = {};  // the knob's key in the canonical `tv1:` tune vector (d, f, lw, be)
};

inline constexpr Sim_set_option kSimSetOptions[] = {
    {"compile_only",
     "false", Sim_set_option::Kind::boolean,
     "compile and link the generated simulator, then stop before executing any testbench. With --run-only, this "
     "builds an existing <workdir>/sim incrementally without regenerating its sources"},
    {"vcd",
     "false", Sim_set_option::Kind::bool_or_file,
     "false|true|FILE — VCD tracing, the ONE vcd knob for every flow. `lhd sim`: any non-false value dumps one VCD "
     "per test to <workdir>/<test.name>.vcd. Compiled sim binaries (--emit-dir sim:): true bakes <top>.vcd, "
     "FILE bakes that explicit path, false bakes none", Sim_set_option::Stage::codegen},
    {"vcd_fake_delay",
     "true", Sim_set_option::Kind::boolean,
     "VCD data settles a few ticks after each clock edge, with X during the settle window (edge->data causality); "
     "false = plain edge-aligned updates (no X, no delay; smaller/faster trace)", Sim_set_option::Stage::codegen},
    {"hlop_dir",
     "", Sim_set_option::Kind::bool_or_file,
     "DIR — hlop checkout to build the sim driver against (resolves slop.hpp/blop.hpp/vcd_writer.hpp). Empty = "
     "auto: the bazel runfiles, else the sibling ../hlop of a source checkout. Set it to build the driver against "
     "a WIP hlop — testing new slop/vcd_writer code without reinstalling it is the reason this knob exists"},
    {"iassert_dir",
     "", Sim_set_option::Kind::bool_or_file,
     "DIR — iassert checkout to build the sim driver against (resolves iassert.hpp, which slop.hpp pulls in). "
     "Empty = auto: the bazel runfiles, else the sibling ../iassert/src. Same purpose as sim.hlop_dir"},
    {"ninja",
     "", Sim_set_option::Kind::bool_or_file,
     "false|true|PATH — build the sim driver with ninja instead of the built-in parallel compile. Empty (the "
     "default) uses ninja when it is on PATH and the built-in build otherwise; true REQUIRES it; PATH names the "
     "binary. Ninja is what makes the host build incremental (depfile-accurate, so a header edit rebuilds exactly "
     "its dependents); the built-in path reuses whatever its own depfiles and command stamps show unchanged. A "
     "`build.ninja` reproducing the exact build is written into the sim dir either way — `ninja -C <workdir>/sim`"},
    {"slop_u",
     "true", Sim_set_option::Kind::boolean,
     "materialize LGraph-proven unsigned combinational values as the CANONICAL-unsigned Slop_u<n> instead of a "
     "lazily-masked Slop<n+1>. Slop makes no promise about storage above bit n-1, so every READ of a stored value "
     "re-masks; Slop_u pays ONE mask at the write and none at the reads. Reset-free state and other unknown-capable "
     "boundaries remain Slop. Set false only for lowering comparisons", Sim_set_option::Stage::codegen},
    {"debug",
     "false", Sim_set_option::Kind::boolean,
     "retain runtime validation landings for bitwidth-proven unsigned Slop_u values. The default trusts the proof "
     "and emits only compile-time width checks, avoiding masks in production generated code", Sim_set_option::Stage::codegen},
    {"unroll_sites",
     "0", Sim_set_option::Kind::count,
     "DEBUG/EXPERIMENT: N — expand a compact (rolled) loop into straight-line code when its trip count times its "
     "body's node count, nested loops multiplied out, is at most N. Default 0 keeps every loop rolled: the compact "
     "form's O(1)-in-trip-count code footprint is what the large benchmarks depend on, so it is preserved by "
     "ruling. Set it to measure what a rolled loop costs (lhdtrack br_mux_onehot ran 2.5x, br_arb_lru 5.6x faster "
     "flat). Speed only: the simulated values never change", Sim_set_option::Stage::codegen},
    {"init_zero",
     "false", Sim_set_option::Kind::boolean,
     "use zero as the power-on value only for flops and memories that have neither an initializer nor a reset. "
     "Explicit initial values and runtime reset values are unchanged", Sim_set_option::Stage::run},
    {"unknown_zero",
     "false", Sim_set_option::Kind::boolean,
     "fill every unknown (`?`) literal bit with 0 instead of a random 0/1. Slop carries no runtime X, so a `?` must "
     "become some concrete bit; the default DRAWS it from the run's seeded PRNG (--seed / lhd.seed, reported as "
     "run.seed + rng_draws) so an unspecified bit cannot be silently relied on, and the draw is once per literal "
     "per run — the value is stable across cycles. true restores the deterministic-zero fill, which also lets the "
     "literal fold at C++ compile time. Orthogonal to sim.init_zero, which covers the power-on value of state "
     "having neither an initializer nor a reset", Sim_set_option::Stage::both},
    {"warn_undefined",
     "true", Sim_set_option::Kind::boolean,
     "print a warning on stderr the FIRST time each site computes a value the source leaves undefined -- a division "
     "or remainder by zero, a memory write or read outside its array -- naming the source location and cycle. One "
     "line per site (a soft limit, not a count); the simulated value is unchanged (X for a division, a dropped write, "
     "a 0 read). Usually a design bug, hence on by default", Sim_set_option::Stage::run},
    {"checkpoint",
     "true", Sim_set_option::Kind::boolean,
     "periodic editable state checkpoints of the DUT + testbench (default on; --restart-cycle needs them)", Sim_set_option::Stage::run},
    {"checkpoint_min_secs",
     "10", Sim_set_option::Kind::non_neg_num,
     "wall-clock floor in seconds between checkpoints (a short run writes none)", Sim_set_option::Stage::run},
    {"checkpoint_max",
     "10", Sim_set_option::Kind::non_neg_num,
     "max checkpoints kept per test, evenly spaced (older ones are pruned)", Sim_set_option::Stage::run},
    {"checkpoint_max_overhead",
     "0.10", Sim_set_option::Kind::non_neg_num,
     "target checkpoint cost as a fraction of run time (caps how often they are taken)", Sim_set_option::Stage::run},
    {"checkpoint_every",
     "0", Sim_set_option::Kind::non_neg_num,
     "deterministic cadence: checkpoint every N cycles (0 = time-based, the default)", Sim_set_option::Stage::run},
    // ---- sim.tune.*: the profile-guided tuner (sim_profile.md). Only knobs that
    // change SPEED, never a simulated value, may live here (the registry check
    // below enforces it), plus the tuner's own controls.
    {"tune.profile",
     "auto", Sim_set_option::Kind::tune_mode,
     "auto|on|off — profile-guided tuning of the sim.tune.* speed knobs; needs a user --workdir and lhd.incremental. "
     "auto: while the workdir holds no CONVERGED tune data, runs profile (drv.bin samples state activity; `?` "
     "literals zero-fill unless sim.unknown_zero is set; checkpoints are skipped) and the next setup may trial ONE "
     "better vector, kept only if >= 7% cheaper per simulated cycle with byte-identical results; converged data is "
     "reused with no sampling. on: profile and learn even when converged. off: ignore the workdir's tune data. An "
     "explicit sim.tune.X or sim.tune.file always wins. The envelope's `sim_tune` member reports what was applied", Sim_set_option::Stage::both},
    {"tune.dirty",
     "auto", Sim_set_option::Kind::tri,
     "auto|on|off (was sim.color_dirty) — the cross-cycle color activation cache, for workloads with long "
     "stable-input periods. off executes every color once per cycle in static phase order with direct boundary "
     "assignments; on change-compares boundary writes, runs a color only when an input changed and ends a fully "
     "quiescent period early. auto = the workdir's tuned decision, else on", Sim_set_option::Stage::codegen,
     Sim_set_option::Tune::perf_g,
     "d"},
    {"tune.fence",
     "auto", Sim_set_option::Kind::fence,
     "auto|none|N (was sim.fence_ratio) — fence a module used ONCE into its own simulator colors only when it has "
     "at least N sites per interface word. A fence lets dirty gating skip a mostly idle module (xs_alu's "
     "AluDataModule: 2.4x), but every value crossing it is a stored, change-tested slot, which only costs when the "
     "module toggles every cycle (an LFSR-driven DUT: up to 10x). 0 fences every such module, none fences nothing; "
     "reused modules always keep their fence. auto = the tuned decision, else none with dirty off and 16 with it on", Sim_set_option::Stage::codegen,
     Sim_set_option::Tune::perf_g,
     "f"},
    {"tune.live_words",
     "auto", Sim_set_option::Kind::num_or_auto,
     "auto|N (was sim.live_words) — machine words (64 bits) of live values one simulator color may keep across its "
     "members. A color boundary costs a stored slot, a compare and a dirty mark per value, so a larger budget means "
     "fewer, bigger colors (minion 20->256 words: 1.75x cycles/s); a smaller one keeps idle logic finer-grained. "
     "auto = the tuned decision, else 256", Sim_set_option::Stage::codegen,
     Sim_set_option::Tune::perf_g,
     "lw"},
    {"tune.backend",
     "llvm", Sim_set_option::Kind::backend,
     "llvm|slop|auto (was sim.backend) — simulator color-kernel backend. llvm (the default) emits native object "
     "files directly; a color its lowering rejects is a setup error, never a silent fallback. slop emits the "
     "reference C++ kernels. auto = the tuned decision, else slop (the tuner never trials the backend, so auto is "
     "slop unless sim.tune.file pins one)", Sim_set_option::Stage::codegen,
     Sim_set_option::Tune::perf_g,
     "be"},
    {"tune.file",
     "", Sim_set_option::Kind::path,
     "FILE — pin the tune vector from a `sim.tune.export` file (how hermetic benchmarks use tuning: a fresh workdir "
     "never converges in place). Its knobs apply below an explicit --set sim.tune.X and above the workdir's tuned "
     "decision, in every mode, with or without a --workdir; a file recorded for another design is only a warning"},
    {"tune.export",
     "", Sim_set_option::Kind::path,
     "FILE — write the current tune decision (explicit > sim.tune.file > tuned > default) as a sim.tune.file, with "
     "its provenance (design structure, converged, activity stats)"},
    {"tune.profile_dir",
     "", Sim_set_option::Kind::path,
     "DIR — where a profiling drv.bin writes its raw run file (default <drv.bin dir>/tune_runs, which the next "
     "`lhd sim --workdir` ingests). `lhd sim` points its own runs at <workdir>/sim_tune/inbox", Sim_set_option::Stage::run},
    {"tune.profile_stride",
     "0", Sim_set_option::Kind::count,
     "N in [0, 1048576] — TEST ONLY: a fixed profiling sample stride in cycles with no jitter (0 = the "
     "self-calibrated <= 1% overhead stride)", Sim_set_option::Stage::run},
};

// Only speed knobs live under sim.tune.*: every entry there is either a tunable
// knob (a trial-cost class and a tv1 key) or one of the tuner's controls, and
// every tunable knob lives there. A semantic, observability or plumbing knob
// placed under `tune` fails the BUILD, not a test.
consteval bool sim_tune_registry_ok() {
  for (const auto& s : kSimSetOptions) {
    const bool in_tune = s.name.starts_with("tune.");
    const bool control = s.name == "tune.profile" || s.name == "tune.file" || s.name == "tune.export"
                         || s.name == "tune.profile_dir" || s.name == "tune.profile_stride";
    const bool knob    = s.tune != Sim_set_option::Tune::none;
    if (in_tune != (control || knob) || (control && knob) || knob == s.tv_key.empty()) {
      return false;
    }
    if (knob && s.stage != Sim_set_option::Stage::codegen && s.tune == Sim_set_option::Tune::perf_g) {
      return false;  // a G-class knob regenerates C++: it is a codegen knob by definition
    }
  }
  return true;
}
static_assert(sim_tune_registry_ok(), "sim.tune.* holds only speed knobs (with a tv1 key) plus the tuner controls");

// The `synth.*` command-namespace options (consumed by synth_command -- the
// one-shot compile -> pass.color reduce -> pass.color synth -> pass.abc ->
// pass.opentimer flow --
// not pass labels). Same contract as kSimSetOptions: this array is the single
// source of truth for --set validation, `lhd list options`, and the
// `lhd synth --help` options block. Pass-level tuning still rides the pass
// namespaces (`--set abc.adder=cla`, `--set color.hier=false`, ...).
struct Synth_set_option {
  // `source_only`: a source attribute with no run-wide value; --set is refused.
  enum class Kind { boolean, file, integer, mapper, source_only };
  std::string_view name;
  std::string_view default_value;
  Kind             kind;
  std::string_view help;
};

// The Liberty file `synth.liberty` resolves to under $LHD_TECH_DIR when the
// knob is empty. ONE default, reached through resolve_liberty by every Liberty
// reader (`lhd synth`, `lhd pass abc`, `lhd pass opentimer`).
inline constexpr std::string_view kSynthDefaultLiberty = "sky130_fd_sc_hd__tt_025C_1v80.lib";

// The technology mappers (abc_cleanup.md section 6): `synth.mapper=<name>` in
// `lhd synth`, `lhd pass <name>` standalone. Every mapper-specific decision of
// the kernel reads this table.
struct Mapper {
  std::string_view name;           // the synth.mapper value and the `lhd pass` subcommand
  std::string_view method;         // its EPRP pass
  std::string_view cache_dir;      // <workdir>/<cache_dir>: its incremental region cache
  std::string_view color_profile;  // pass.color.synth.mapper: the default coloring it wants
  bool             inherits_abc;   // also reads the pass.abc.* vocabulary (its own namespace wins)
  bool             timing_files;   // receives the synth.liberty/sdc/spef timing environment
  // Its own report: `<qor>.<report>.json` (with a `<qor>.provenance` archive)
  // and the fused envelope's `qor.<report>` member. Empty: none.
  std::string_view report;
};
inline constexpr Mapper kMappers[] = {
    { "abc",  "pass.abc",  "abc_cache",  "abc", false, false,     ""},
    {"usyn", "pass.usyn", "usyn_cache", "usyn", false,  true, "usyn"},
};
[[nodiscard]] constexpr const Mapper* find_mapper(std::string_view name) {
  for (const auto& m : kMappers) {
    if (m.name == name) {
      return &m;
    }
  }
  return nullptr;
}
[[nodiscard]] constexpr const Mapper* mapper_of_method(std::string_view method) {
  for (const auto& m : kMappers) {
    if (m.method == method) {
      return &m;
    }
  }
  return nullptr;
}

inline constexpr Synth_set_option kSynthSetOptions[] = {
    {"place", "false", Synth_set_option::Kind::boolean, "place mapped cells with native DREAMPlace before final STA"},
    {"lef",
     "",
     Synth_set_option::Kind::file,
     "comma-separated tech LEF then cell/macro LEFs; empty uses sky130HD beside $LHD_TECH_DIR"},
    {"place.mode",
     "full",
     Synth_set_option::Kind::file,
     "full: legalize/detail and emit DEF; gp: approximate global placement, no DEF"},
    {"place.utilization", "0.30", Synth_set_option::Kind::file, "cell area / generated core area, in (0,1)"},
    {"place.aspect", "1", Synth_set_option::Kind::file, "generated core width / height"},
    {"place.die",
     "",
     Synth_set_option::Kind::file,
     "fixed generated die as xl,yl,xh,yh microns (ORFS DIE_AREA); empty derives it from utilization/aspect; "
     "without place.core the core equals the die"},
    {"place.core",
     "",
     Synth_set_option::Kind::file,
     "fixed generated core as xl,yl,xh,yh microns (ORFS CORE_AREA), snapped inward to the site/row grid; "
     "utilization and aspect then do not apply"},
    {"place.density", "0.5", Synth_set_option::Kind::file, "global-placement target density, at least utilization"},
    {"place.snapshots", "false", Synth_set_option::Kind::boolean, "save physical GP coordinates before legalization"},
    {"place.cohesion",
     "0",
     Synth_set_option::Kind::file,
     "soft synthesis-color cohesion coefficient in inverse microns; zero disables"},
    {"place.halo",
     "auto",
     Synth_set_option::Kind::file,
     "non-CORE macro clearance per side: auto = 10 placement-row heights, or H / X,Y microns (site/row rounded up)"},
    {"place.cell_halo",
     "",
     Synth_set_option::Kind::file,
     "optional final-cell clearance around fixed macros: H or X,Y microns; empty inherits place.halo; "
     "each axis must not exceed place.halo; macro placement and macro-to-macro spacing retain place.halo"},
    {"place.fp", "false", Synth_set_option::Kind::boolean, "repelling-centroid floorplan GP, centroid snap and macro refinement"},
    {"place.final",
     "true",
     Synth_set_option::Kind::boolean,
     "after floorplan/macro placement, fix macros and place cells without centroid anchors"},
    {"place.final.iterations", "0", Synth_set_option::Kind::integer, "final cell GP cap; zero uses place.iterations"},
    {"place.fp.snap",
     "true",
     Synth_set_option::Kind::boolean,
     "snap floorplan centroid targets; false freezes the original GP means"},
    {"place.fp.iterations", "1500", Synth_set_option::Kind::integer, "floorplan GP cap; zero uses place.iterations"},
    {"place.fp.timing_interval",
     "50",
     Synth_set_option::Kind::integer,
     "FP-only RC/STA and net-weight checkpoint interval; 0 disables checkpoints including FP resize"},
    {"place.fp.timing_movement",
     "0",
     Synth_set_option::Kind::file,
     "FP timing RMS movement threshold in bins; 0 uses fixed intervals, positive uses the timing_max_updates budget"},
    {"place.fp.timing_max_updates",
     "10",
     Synth_set_option::Kind::integer,
     "maximum movement-triggered FP timing updates including the final cap sample (at least 2); fixed cadence is unaffected"},
    {"place.fp.timing_after_density",
     "false",
     Synth_set_option::Kind::boolean,
     "defer FP RC/STA and repair until GP overflow meets its stop threshold; always evaluate the iteration cap"},
    {"place.fp.minimum", "3000", Synth_set_option::Kind::file, "candidate minimum equivalent physical cell area"},
    {"place.control_ports",
     "",
     Synth_set_option::Kind::file,
     "optional comma-separated clock/reset root-port override; absent uses synthesis and Liberty/LEF metadata"},
    {"place.repair.reset",
     "false",
     Synth_set_option::Kind::boolean,
     "retired control; must be false because pre-CTS clock/reset networks are not sized or buffered"},
    {"place.reset_fanout",
     "0",
     Synth_set_option::Kind::integer,
     "retired control; must be 0 because pre-CTS reset buffering is disabled"},
    {"place.fp.warpspace", "false", Synth_set_option::Kind::boolean, "enable density warp for this placement stage"},
    {"place.fp.warpspace.weight",
     "1",
     Synth_set_option::Kind::file,
     "warp target gain (FP) or relative virtual pull strength (macro), 0..1"},
    {"place.fp.warpspace.bins",
     "16",
     Synth_set_option::Kind::integer,
     "warp density grid per axis, 2..64; smoothing remains 32 bins"},
    {"place.fp.warpspace.iterations", "2", Synth_set_option::Kind::integer, "alternating warp solve rounds, 1..6"},
    {"place.fp.warpspace.seconds", "3", Synth_set_option::Kind::file, "time limit per warp axis solve, (0,30] seconds"},
    {"place.macro.warpspace", "false", Synth_set_option::Kind::boolean, "enable density warp for this placement stage"},
    {"place.macro.warpspace.weight",
     "0.7",
     Synth_set_option::Kind::file,
     "warp target gain (FP) or relative virtual pull strength (macro), 0..1"},
    {"place.macro.warpspace.bins",
     "16",
     Synth_set_option::Kind::integer,
     "warp density grid per axis, 2..64; smoothing remains 32 bins"},
    {"place.macro.warpspace.iterations", "2", Synth_set_option::Kind::integer, "alternating warp solve rounds, 1..6"},
    {"place.macro.warpspace.seconds", "3", Synth_set_option::Kind::file, "time limit per warp axis solve, (0,30] seconds"},
    {"place.macro.warpspace.updates",
     "10",
     Synth_set_option::Kind::integer,
     "legacy maximum macro attempts when refresh_interval=0, 1..30; FP samples once"},
    {"place.macro.warpspace.once_after_density",
     "false",
     Synth_set_option::Kind::boolean,
     "freeze macro anchors after GP reaches stop_overflow"},
    {"place.macro.warpspace.warmup", "0.1", Synth_set_option::Kind::file, "macro warmup fraction of iteration budget, [0,1)"},
    {"place.macro.warpspace.start_stability",
     "0.20",
     Synth_set_option::Kind::file,
     "one-shot start gate: mean macro motion relative to first-50 peak; 0 uses warmup, [0,1]"},
    {"place.macro.warpspace.stability_window",
     "10",
     Synth_set_option::Kind::integer,
     "consecutive GP steps averaged for the start gate and recorded diagnostics, 1..100"},
    {"place.macro.warpspace.refresh_interval",
     "25",
     Synth_set_option::Kind::integer,
     "GP iterations between constant-strength target updates after activation; 0 uses legacy updates budget"},
    {"place.fp.colors",
     "false",
     Synth_set_option::Kind::boolean,
     "add eligible color leaves with a movable macro to floorplan candidates"},
    {"place.fp.min_copies",
     "4",
     Synth_set_option::Kind::integer,
     "minimum matching candidate copies per immediate hierarchy parent (at least 2)"},
    {"place.fp.min_macros",
     "2",
     Synth_set_option::Kind::integer,
     "a hierarchy candidate needs more than this many movable macros (0 = at least one)"},
    {"place.fp.color_source",
     "synthesis",
     Synth_set_option::Kind::file,
     "synthesis|placement|memory_first: membership for FP color centroids"},
    {"place.fp.color_maximum",
     "30000",
     Synth_set_option::Kind::file,
     "memory-first color growth cap in equivalent standard-cell logic area; SRAM area only affects centroid size"},
    {"place.fp.color_minimum",
     "-1",
     Synth_set_option::Kind::file,
     "color centroid minimum physical equivalent area; -1 inherits fp.minimum"},
    {"place.fp.pin_adjust", "true", Synth_set_option::Kind::boolean, "adjust ordered IO buses along the perimeter during FP"},
    {"place.fp.pin_interval", "50", Synth_set_option::Kind::integer, "FP IO adjustment interval, independently of STA"},
    {"place.fp.pin_step",
     "0.01",
     Synth_set_option::Kind::file,
     "maximum per-pin perimeter travel per update as a perimeter fraction"},
    {"place.fp.pin_improvement",
     "0.001",
     Synth_set_option::Kind::file,
     "minimum relative wirelength improvement for IO moves/reversal"},
    {"place.macro.pin_adjust", "true", Synth_set_option::Kind::boolean, "adjust ordered IO buses during movable-macro GP"},
    {"place.macro.pin_interval", "50", Synth_set_option::Kind::integer, "macro GP IO adjustment interval, independently of STA"},
    {"place.macro.pin_step", "0.01", Synth_set_option::Kind::file, "maximum per-pin macro GP travel as a perimeter fraction"},
    {"place.macro.pin_improvement",
     "0.001",
     Synth_set_option::Kind::file,
     "minimum relative weighted-HPWL gain for macro GP IO moves"},
    {"place.fp.attraction", "0.25", Synth_set_option::Kind::file, "floorplan member attraction relative to GP forces"},
    {"place.fp.distance_attraction",
     "true",
     Synth_set_option::Kind::boolean,
     "calibrate centroid attraction once; restoring force grows with distance"},
    {"place.fp.attraction_outside",
     "4",
     Synth_set_option::Kind::file,
     "additional attraction stiffness outside the centroid circle (4 gives 5x slope)"},
    {"place.fp.repulsion", "5", Synth_set_option::Kind::file, "initial adaptive same-type circle repulsion multiplier"},
    {"place.fp.align_weight",
     "0",
     Synth_set_option::Kind::file,
     "moving hierarchy-centroid alignment relative to candidate net force; zero disables"},
    {"place.fp.align_sigma",
     "0.25",
     Synth_set_option::Kind::file,
     "centroid alignment well width as a fraction of circle diameter"},
    {"place.fp.align_start", "100", Synth_set_option::Kind::integer, "first FP iteration of centroid alignment ramp"},
    {"place.fp.align_full",
     "500",
     Synth_set_option::Kind::integer,
     "FP iteration at which centroid alignment reaches full strength"},
    {"place.fp.parent_weight",
     "0",
     Synth_set_option::Kind::file,
     "FP-only parent-child centroid spring relative to calibrated member attraction; zero disables"},
    {"place.fp.parent_outside",
     "4",
     Synth_set_option::Kind::file,
     "quartic excess-distance energy outside the parent circle; cubic restoring force"},
    {"place.fp.anchor_macros_only",
     "false",
     Synth_set_option::Kind::boolean,
     "pull only macros toward frozen FP targets during macro GP; cells remain free"},
    {"place.fp.anchor", "0.25", Synth_set_option::Kind::file, "member attraction to frozen snapped targets in the next GP"},
    {"place.fp.alignment", "1", Synth_set_option::Kind::file, "snap center-offset threshold in smaller-circle radii"},
    {"place.fp.spacing_tolerance", "0.25", Synth_set_option::Kind::file, "maximum normalized range of a shared gap class"},
    {"place.fp.spacing_penalty", "0.02", Synth_set_option::Kind::file, "penalty per distinct neighbor spacing"},
    {"place.fp.symmetry", "1", Synth_set_option::Kind::file, "shared midpoint threshold in smaller-circle radii"},
    {"place.fp.max_displacement", "1", Synth_set_option::Kind::file, "local snap movement limit per axis in own-circle radii"},
    {"place.macro", "none", Synth_set_option::Kind::file, "none|h2d: opt-in native hierarchical macro placement"},
    {"place.macro.magnet",
     "false",
     Synth_set_option::Kind::boolean,
     "globally merge equal-shape macros into rigid placement blocks"},
    {"place.macro.magnet_start_displacement",
     "0.10",
     Synth_set_option::Kind::file,
     "one-shot global macro motion limit before local stability checks"},
    {"place.macro.magnet_before_snap",
     "false",
     Synth_set_option::Kind::boolean,
     "merge only after ordinary macro GP, immediately before snap; no GP restart"},
    {"place.macro.magnet_settle_window", "10", Synth_set_option::Kind::integer, "macro movement observation window"},
    {"place.macro.magnet_settle_displacement",
     "0.05",
     Synth_set_option::Kind::file,
     "maximum movement of each candidate macro normalized by its own smaller extent"},
    {"place.macro.magnet_max_gap", "1", Synth_set_option::Kind::file, "maximum candidate gap in macro extents"},
    {"place.macro.magnet_density_fraction",
     "0.25",
     Synth_set_option::Kind::file,
     "maximum gap cell density relative to global cell density"},
    {"place.macro.magnet_max_per_round",
     "0",
     Synth_set_option::Kind::integer,
     "maximum independent merges per round; zero is unbounded"},
    {"place.macro.magnet_max_rounds", "0", Synth_set_option::Kind::integer, "maximum magnet rounds; zero is unbounded"},
    {"place.macro.align_weight",
     "25",
     Synth_set_option::Kind::file,
     "macro GP alignment relative to mean macro force; zero disables"},
    {"place.macro.align_sigma",
     "0.25",
     Synth_set_option::Kind::file,
     "bounded alignment-well width as a fraction of macro extent; zero is quadratic"},
    {"place.macro.align_start", "100", Synth_set_option::Kind::integer, "first global iteration of the macro alignment ramp"},
    {"place.macro.align_full",
     "500",
     Synth_set_option::Kind::integer,
     "global iteration at which macro alignment reaches full strength"},
    {"place.macro.align_overlap_boost",
     "0",
     Synth_set_option::Kind::file,
     "additional macro-overlap weight scaled by alignment ramp"},
    {"place.macro.align_nearest", "false", Synth_set_option::Kind::boolean, "align only nearest same-class partner per side"},
    {"place.macro.align_global",
     "true",
     Synth_set_option::Kind::boolean,
     "macro GP: align same-master projections across colors using strip-grid pairs refreshed each iteration; needs align_weight > "
     "0"},
    {"place.macro.snap",
     "true",
     Synth_set_option::Kind::boolean,
     "soft snap of equal-size macros within hierarchy scopes during macro legalization"},
    {"place.macro.snap_mode",
     "alignment",
     Synth_set_option::Kind::file,
     "alignment: existing line snap; symmetry: same-scope/size line, gap and symmetry snap"},
    {"place.macro.snap_alignment", "1", Synth_set_option::Kind::file, "hierarchy snap line threshold in equal-area macro radii"},
    {"place.macro.snap_spacing_tolerance",
     "0.25",
     Synth_set_option::Kind::file,
     "hierarchy snap maximum normalized range per gap class"},
    {"place.macro.snap_spacing_penalty",
     "0.02",
     Synth_set_option::Kind::file,
     "hierarchy snap penalty per distinct neighbor spacing"},
    {"place.macro.snap_symmetry", "1", Synth_set_option::Kind::file, "hierarchy snap shared midpoint threshold in macro radii"},
    {"place.macro.snap_displacement", "1", Synth_set_option::Kind::file, "hierarchy snap per-axis movement limit in macro radii"},
    {"place.macro.snap_tolerance",
     "0.25",
     Synth_set_option::Kind::file,
     "maximum snap-group center offset as a fraction of extent"},
    {"place.macro.snap_weight",
     "100",
     Synth_set_option::Kind::integer,
     "soft equality weight relative to unit macro displacement anchors"},
#define H2D_INT(n, d, h)    {"place.macro." #n, #d, Synth_set_option::Kind::integer, h},
#define H2D_REAL(n, d, h)   {"place.macro." #n, #d, Synth_set_option::Kind::file, h},
#define H2D_BOOL(n, d, h)   {"place.macro." #n, #d, Synth_set_option::Kind::boolean, h},
#define H2D_STRING(n, d, h) {"place.macro." #n, d, Synth_set_option::Kind::file, h},
#include "dreamplace/native/macro/params.def"
#undef H2D_INT
#undef H2D_REAL
#undef H2D_BOOL
#undef H2D_STRING
    {"place.floorplan", "", Synth_set_option::Kind::file, "optional DEF with contiguous N/FS rows and FIXED pins/macros"},
    {"place.wire_rc", "", Synth_set_option::Kind::file, "wire RC override: ohm/um,fF/um; empty derives it from LEF"},
    {"place.rc_pessimism",
     "1",
     Synth_set_option::Kind::file,
     "wire RC product multiplier >=1; scales wire R and C by its square root"},
    {"place.tech_rc", "", Synth_set_option::Kind::file, "characterized technology RC file: literal set_wire_rc/set_layer_rc data"},
    {"place.tracks",
     "",
     Synth_set_option::Kind::file,
     "routing grid file: literal make_tracks data; overrides DEF/LEF track grids"},
    {"place.iterations",
     "1500",
     Synth_set_option::Kind::integer,
     "GP iteration budget per call (0 runs legalization only in full mode)"},
    {"place.grid.density", "64", Synth_set_option::Kind::integer, "GP density-grid bins per axis (the uniform force grid)"},
    {"place.grid.density_mode",
     "fixed",
     Synth_set_option::Kind::file,
     "fixed uses grid.density; dynamic sizes the grid from movable area up to grid.density_max (OpenROAD style); macro picks "
     "32..64 bins from the physical macro size"},
    {"place.grid.density_max",
     "512",
     Synth_set_option::Kind::integer,
     "dynamic density-grid cap per axis (power of two, 32..1024)"},
    {"place.grid.route",
     "inherit",
     Synth_set_option::Kind::file,
     "routing grid per axis for RUDY/RSMT/URoute, macro channel pressure, native legalization/detailed placement, terminal "
     "and SAT route-grid bins: inherit follows the density grid (the configured grid.density in dynamic mode), or 2..1024"},
    {"place.bins", "64", Synth_set_option::Kind::integer, "DEPRECATED: use synth.place.grid.density"},
    {"place.dynamic_bins", "false", Synth_set_option::Kind::boolean, "DEPRECATED: use synth.place.grid.density_mode=dynamic"},
    {"place.bin_max", "512", Synth_set_option::Kind::integer, "DEPRECATED: use synth.place.grid.density_max"},
    {"place.macro.fill_cell_space",
     "false",
     Synth_set_option::Kind::boolean,
     "macro-plan filler budget: (1-cell density) times core space excluding physical macro bodies; scaled by "
     "virtual_filler_fraction"},
    {"place.macro.filler_fraction",
     "0",
     Synth_set_option::Kind::file,
     "macro-plan virtual filler axes as a fraction of the smallest memory's physical axes, [0,1]; 0 uses cell sizing"},
    {"place.virtual_fillers",
     "true",
     Synth_set_option::Kind::boolean,
     "internal density-only particles; no netlist cells or legal sites"},
    {"place.virtual_filler_max_count",
     "0",
     Synth_set_option::Kind::integer,
     "optional particle count guard; zero derives count from density and particle size"},
    {"place.virtual_filler_cell_scale",
     "1",
     Synth_set_option::Kind::file,
     "particle dimensions relative to central-90%-mean standard cells"},
    {"place.virtual_filler_bin_fraction",
     "0.25",
     Synth_set_option::Kind::file,
     "maximum particle dimension as a fraction of each density-bin axis"},
    {"place.virtual_filler_fraction",
     "0.25",
     Synth_set_option::Kind::file,
     "multiplier of target-density whitespace charge (0..2, positive; above 1 overfills)"},
    {"place.local_density_weight",
     "0",
     Synth_set_option::Kind::file,
     "screened local electrostatic force relative to ordinary GP force; zero disables"},
    {"place.grid.local", "512", Synth_set_option::Kind::integer, "local spreading grid per axis (power of two, 8..1024)"},
    {"place.local_density_bins", "512", Synth_set_option::Kind::integer, "DEPRECATED: use synth.place.grid.local"},
    {"place.local_density_target",
     "-1",
     Synth_set_option::Kind::file,
     "extra cell-spreading density threshold in [0,1]; -1 inherits global density without changing its target"},
    {"place.macro.cell_density_weight",
     "0",
     Synth_set_option::Kind::file,
     "long-range standard-cell pressure on movable macros in macro GP only; zero disables"},
    {"place.macro.cell_density_reach",
     "2",
     Synth_set_option::Kind::file,
     "cell-to-memory screening length in largest physical SRAM side lengths; independent of cell spreading"},
    {"place.macro.density_pull_weight",
     "0",
     Synth_set_option::Kind::file,
     "weak local density-slope force on FP/macro GP macros, relative to ordinary force and centroid reference (0..1)"},
    {"place.macro.density_pull_range",
     "4",
     Synth_set_option::Kind::file,
     "density-pull side-strip reach in local-density bin pitches (0..32, positive)"},
    {"place.local_density_range", "4", Synth_set_option::Kind::file, "electrostatic screening length in local-bin pitches"},
    {"place.local_density_jitter",
     "0.01",
     Synth_set_option::Kind::file,
     "deterministic cell jitter fraction when local spreading is enabled (0..0.1)"},
    {"place.stop_overflow",
     "0.1",
     Synth_set_option::Kind::file,
     "native GP overflow stopping threshold in [0,1], also used by FP timing density gating"},
    {"place.initial_density_weight",
     "0.01",
     Synth_set_option::Kind::file,
     "positive initial density-force multiplier before native gradient normalization"},
    {"place.min_iterations", "40", Synth_set_option::Kind::integer, "minimum GP iterations before convergence checks"},
    {"place.region.max_cells",
     "10000",
     Synth_set_option::Kind::integer,
     "movable-cell cap per detail-final region rectangle (shared by region.busorder, region.boundarypins, region.pinalign)"},
    {"place.region.merge_um",
     "5",
     Synth_set_option::Kind::file,
     "merge region cut lines from macro edges and IO bus spans within this distance in microns"},
    {"place.region.boundarypins",
     "false",
     Synth_set_option::Kind::boolean,
     "experimental fixed RSMT boundary pins on region edges before final legalization"},
    {"place.region.boundarypins.split",
     "true",
     Synth_set_option::Kind::boolean,
     "split placement nets at boundary pins; false runs the extra-GP control"},
    {"place.region.pinalign",
     "false",
     Synth_set_option::Kind::boolean,
     "align opposite-edge virtual pins across zero or one cell; preserve bus order"},
    {"place.region.boundarypins.order",
     "false",
     Synth_set_option::Kind::boolean,
     "optimize virtual pins along their original rectangle edge with monotone bus order"},
    {"place.region.boundarypins.iterations",
     "50",
     Synth_set_option::Kind::integer,
     "extra final GP iterations with frozen boundary pins"},
    {"place.region.boundarypins.max_displacement_um",
     "20",
     Synth_set_option::Kind::file,
     "maximum extra GP movement per axis, reduced by timing criticality"},
    {"place.region.busorder",
     "false",
     Synth_set_option::Kind::boolean,
     "experimental bus-order refinement across region edges before final-cell legalization"},
    {"place.region.busorder.iterations", "100", Synth_set_option::Kind::integer, "maximum final bus-order settling iterations"},
    {"place.region.busorder.refresh",
     "20",
     Synth_set_option::Kind::integer,
     "GP iterations between bus crossing association updates"},
    {"place.region.busorder.weight",
     "4",
     Synth_set_option::Kind::file,
     "soft bus-order strength; zero provides a settling-only control"},
    {"place.region.busorder.max_displacement_um",
     "20",
     Synth_set_option::Kind::file,
     "maximum refinement displacement per axis, reduced by timing criticality"},
    {"place.boundarypins", "false", Synth_set_option::Kind::boolean, "DEPRECATED: use synth.place.region.boundarypins"},
    {"place.boundarypins.split", "true", Synth_set_option::Kind::boolean, "DEPRECATED: use synth.place.region.boundarypins.split"},
    {"place.pinalign", "false", Synth_set_option::Kind::boolean, "DEPRECATED: use synth.place.region.pinalign"},
    {"place.boundarypins.order", "false", Synth_set_option::Kind::boolean, "DEPRECATED: use synth.place.region.boundarypins.order"},
    {"place.boundarypins.iterations",
     "50",
     Synth_set_option::Kind::integer,
     "DEPRECATED: use synth.place.region.boundarypins.iterations"},
    {"place.boundarypins.max_displacement_um",
     "20",
     Synth_set_option::Kind::file,
     "DEPRECATED: use synth.place.region.boundarypins.max_displacement_um"},
    {"place.busorder", "false", Synth_set_option::Kind::boolean, "DEPRECATED: use synth.place.region.busorder"},
    {"place.busorder.iterations", "100", Synth_set_option::Kind::integer, "DEPRECATED: use synth.place.region.busorder.iterations"},
    {"place.busorder.refresh", "20", Synth_set_option::Kind::integer, "DEPRECATED: use synth.place.region.busorder.refresh"},
    {"place.busorder.max_cells", "10000", Synth_set_option::Kind::integer, "DEPRECATED: use synth.place.region.max_cells"},
    {"place.busorder.weight", "4", Synth_set_option::Kind::file, "DEPRECATED: use synth.place.region.busorder.weight"},
    {"place.busorder.merge_um", "5", Synth_set_option::Kind::file, "DEPRECATED: use synth.place.region.merge_um"},
    {"place.busorder.max_displacement_um",
     "20",
     Synth_set_option::Kind::file,
     "DEPRECATED: use synth.place.region.busorder.max_displacement_um"},
    {"place.post_density_factor",
     "4",
     Synth_set_option::Kind::file,
     "maximum additional GP iterations as a multiple of the first geometry/density-converged iteration; 0 restores density-only "
     "stopping"},
    {"place.convergence_tolerance",
     "0.01",
     Synth_set_option::Kind::file,
     "relative weighted-HPWL/inflation tolerance for two successive GP samples"},
    {"place.satlegalize",
     "false",
     Synth_set_option::Kind::boolean,
     "incremental local pin-access SAT alignment before final legalization (experimental, opt-in)"},
    {"place.satlegalize.backend",
     "cadical",
     Synth_set_option::Kind::file,
     "incremental model backend: cadical, cms-cnf or cms-xor"},
    {"place.satlegalize.encoding",
     "candidate",
     Synth_set_option::Kind::file,
     "local position encoding: candidate or bounded binary DBU coordinates"},
    {"place.satlegalize.max_cells", "24", Synth_set_option::Kind::integer, "maximum movable cells in a SAT region (1..65536)"},
    {"place.satlegalize.max_candidates",
     "12",
     Synth_set_option::Kind::integer,
     "candidate row/site positions per movable cell (1..128)"},
    {"place.satlegalize.max_windows", "8", Synth_set_option::Kind::integer, "maximum local SAT windows (1..1024)"},
    {"place.satlegalize.max_pairs", "32", Synth_set_option::Kind::integer, "maximum incremental alignment proposals (1..1024)"},
    {"place.satlegalize.max_pairs_per_window",
     "0",
     Synth_set_option::Kind::integer,
     "alignment trial cap per region (0 uses only the global budget, 1..1024 caps each region)"},
    {"place.satlegalize.check_packing",
     "false",
     Synth_set_option::Kind::boolean,
     "probe candidate-menu packing before alignment; skip a region on unsat or exhausted probe budget"},
    {"place.satlegalize.max_moved_cells",
     "0",
     Synth_set_option::Kind::integer,
     "maximum moved members per SAT region, including endpoints and spills (0 unbounded, 1..128); post-repair experiment"},
    {"place.satlegalize.move_sites", "4", Synth_set_option::Kind::integer, "horizontal movement radius in sites (0..128)"},
    {"place.satlegalize.move_rows", "1", Synth_set_option::Kind::integer, "vertical movement radius in rows (0..16)"},
    {"place.satlegalize.route",
     "false",
     Synth_set_option::Kind::boolean,
     "route retained two-pin SAT alignments as checked FIXED metal; requires satlegalize, final placement, taps and native PG"},
    {"place.satlegalize.route.vias",
     "false",
     Synth_set_option::Kind::boolean,
     "Permit SAT escape-layer corridors with DRC-checked pin-access vias from the technology profile (experimental)."},
    {"place.satlegalize.route.critical_fraction",
     "0.10",
     Synth_set_option::Kind::file,
     "Fraction (0..1) of eligible aligned nets ranked by worst setup slack to compare straight upper-metal trunks; 0 disables."},
    {"place.satlegalize.route.pg_after",
     "false",
     Synth_set_option::Kind::boolean,
     "Rebuild native PG around accepted SAT signal routes (experimental)."},
    {"place.satlegalize.bin.grid",
     "none",
     Synth_set_option::Kind::file,
     "route uses the exact RSMT routing grid for SAT bins; none keeps bin.size_um or pair-centered windows"},
    {"place.satlegalize.route_grid",
     "false",
     Synth_set_option::Kind::boolean,
     "DEPRECATED: use synth.place.satlegalize.bin.grid=route"},
    {"place.satlegalize.route_guidance",
     "true",
     Synth_set_option::Kind::boolean,
     "prioritize congested bins and lower-congestion spill destinations with bin.grid=route"},
    {"place.satlegalize.route_peak_floor",
     "0",
     Synth_set_option::Kind::file,
     "peak-utilization comparison floor (0..1); 0 is strict, 1 allows changes below estimated routing capacity"},
    {"place.satlegalize.bin.size_um",
     "",
     Synth_set_option::Kind::file,
     "fixed SAT bin width,height in microns, anchored at the die origin; empty preserves pair-centered windows"},
    {"place.satlegalize.region_size", "", Synth_set_option::Kind::file, "DEPRECATED: use synth.place.satlegalize.bin.size_um"},
    {"place.satlegalize.spill_neighbors",
     "false",
     Synth_set_option::Kind::boolean,
     "allow noncritical cells to move into less-dense adjacent fixed SAT bins (bin.size_um or bin.grid=route)"},
    {"place.satlegalize.spill_slack_margin_ps",
     "0",
     Synth_set_option::Kind::file,
     "minimum positive data-pin setup slack for noncritical spill in ps (finite, nonnegative)"},
    {"place.satlegalize.conflicts", "10000", Synth_set_option::Kind::integer, "conflict budget per incremental solve"},
    {"place.satlegalize.milliseconds", "50", Synth_set_option::Kind::integer, "time budget per incremental solve in milliseconds"},
    {"place.satlegalize.skip_conflicts",
     "true",
     Synth_set_option::Kind::boolean,
     "continue the window after a conflicting or undecided pair"},
    {"place.satlegalize.boundary_growth",
     "0.05",
     Synth_set_option::Kind::file,
     "maximum relative incident-net HPWL growth per accepted alignment (0..1)"},
    {"place.detailed_iterations", "2", Synth_set_option::Kind::integer, "iteration cap per detailed-placement kernel (1..100)"},
    {"place.density_upper_bound", "false", Synth_set_option::Kind::boolean, "experimental underfull-bin-neutral density penalty"},
    {"place.fixed_macro_overlap",
     "0",
     Synth_set_option::Kind::file,
     "bounded exact-footprint cell/fixed-macro repulsion; zero disables"},
    {"place.scale_macro_density",
     "true",
     Synth_set_option::Kind::boolean,
     "scale movable macro occupancy by target density and compensate fillers"},
    {"place.auto_bins", "false", Synth_set_option::Kind::boolean, "DEPRECATED: use synth.place.grid.density_mode=macro"},
    {"place.exact_macro_density",
     "true",
     Synth_set_option::Kind::boolean,
     "keep macro occupancy footprints without density smoothing"},
    {"place.macro_overlap", "50", Synth_set_option::Kind::file, "normalized squared macro-overlap penalty weight; zero disables"},
    {"place.macro_jitter",
     "0.01",
     Synth_set_option::Kind::file,
     "deterministic initial macro jitter as fraction of dimensions (0..0.1)"},
    {"place.adaptive_density", "true", Synth_set_option::Kind::boolean, "overflow-feedback density weight with HPWL growth brake"},
    {"place.routability",
     "true",
     Synth_set_option::Kind::boolean,
     "late budgeted RUDY inflation using LEF capacity, blockages and macro pins"},
    {"place.route.model",
     "rsmt",
     Synth_set_option::Kind::file,
     "route estimator: off, rudy, rsmt, uroute, auto (one-way rudy to rsmt to uroute)"},
    {"place.uroute.dir",
     "@pretrained",
     Synth_set_option::Kind::file,
     "URoute project directory; @pretrained selects bundled data, empty disables"},
    {"place.uroute.export",
     "true",
     Synth_set_option::Kind::boolean,
     "export final physical feature maps for offline router ingestion"},
    {"place.uroute.shots", "5", Synth_set_option::Kind::integer, "maximum deterministic support samples (1..16)"},
    {"place.uroute.min_shots",
     "1",
     Synth_set_option::Kind::integer,
     "minimum same-design support before nearest fallback (1..shots)"},
    {"place.uroute.fallback", "nearest", Synth_set_option::Kind::file, "support fallback: nearest or skip"},
    {"place.uroute.drc_level", "0", Synth_set_option::Kind::file, "DRC overflow threshold; zero keeps DRC report-only"},
    {"place.final.route.uroute_switch",
     "0.01",
     Synth_set_option::Kind::file,
     "RSMT overflow at or below which auto can latch URoute; <= final.route.switch"},
    {"place.final.route.uroute_hold",
     "2",
     Synth_set_option::Kind::integer,
     "consecutive qualifying RSMT samples before the one-way URoute latch (1..1000)"},
    {"place.route.degree",
     "40",
     Synth_set_option::Kind::integer,
     "maximum FLUTE net degree (2..1000); larger nets use weighted RUDY"},
    {"place.route.accuracy", "3", Synth_set_option::Kind::integer, "FLUTE accuracy (1..10)"},
    {"place.fp.route.model",
     "inherit",
     Synth_set_option::Kind::file,
     "route estimator override: inherit, off, rudy, rsmt, uroute, auto"},
    {"place.fp.route.switch", "0.05", Synth_set_option::Kind::file, "RUDY mean route overflow at or below which auto latches RSMT"},
    {"place.fp.route.target",
     "0.000000001",
     Synth_set_option::Kind::file,
     "active mean route overflow success level (nonnegative, <= switch)"},
    {"place.fp.route.hold",
     "1",
     Synth_set_option::Kind::integer,
     "consecutive qualifying route samples before auto switches (1..1000)"},
    {"place.fp.route.probe",
     "false",
     Synth_set_option::Kind::file,
     "convergence probe: false, true/rsmt, uroute; bounded feedback on failure"},
    {"place.fp.route.probe_iterations",
     "100",
     Synth_set_option::Kind::integer,
     "maximum additional GP iterations after a failed RSMT probe (1..10000)"},
    {"place.macro.route.model",
     "inherit",
     Synth_set_option::Kind::file,
     "route estimator override: inherit, off, rudy, rsmt, uroute, auto"},
    {"place.macro.route.switch",
     "0.05",
     Synth_set_option::Kind::file,
     "RUDY mean route overflow at or below which auto latches RSMT"},
    {"place.macro.route.target",
     "0.000000001",
     Synth_set_option::Kind::file,
     "active mean route overflow success level (nonnegative, <= switch)"},
    {"place.macro.route.hold",
     "1",
     Synth_set_option::Kind::integer,
     "consecutive qualifying route samples before auto switches (1..1000)"},
    {"place.macro.route.probe",
     "false",
     Synth_set_option::Kind::file,
     "convergence probe: false, true/rsmt, uroute; bounded feedback on failure"},
    {"place.macro.route.probe_iterations",
     "100",
     Synth_set_option::Kind::integer,
     "maximum additional GP iterations after a failed RSMT probe (1..10000)"},
    {"place.final.route.model",
     "inherit",
     Synth_set_option::Kind::file,
     "route estimator override: inherit, off, rudy, rsmt, uroute, auto"},
    {"place.final.route.switch",
     "0.05",
     Synth_set_option::Kind::file,
     "RUDY mean route overflow at or below which auto latches RSMT"},
    {"place.final.route.target",
     "0.000000001",
     Synth_set_option::Kind::file,
     "active mean route overflow success level (nonnegative, <= switch)"},
    {"place.final.route.hold",
     "1",
     Synth_set_option::Kind::integer,
     "consecutive qualifying route samples before auto switches (1..1000)"},
    {"place.final.route.probe",
     "false",
     Synth_set_option::Kind::file,
     "convergence probe: false, true/rsmt, uroute; bounded feedback on failure"},
    {"place.final.route.probe_iterations",
     "100",
     Synth_set_option::Kind::integer,
     "maximum additional GP iterations after a failed RSMT probe (1..10000)"},
    {"place.routability_charge_compensation",
     "true",
     Synth_set_option::Kind::boolean,
     "rebalance charges with RUDY inflation; false reproduces the earlier R4 charge model"},
    {"place.routability_macro_weight",
     "1",
     Synth_set_option::Kind::file,
     "direct RUDY channel pressure on movable macros; quadratic below capacity too; zero disables"},
    {"place.routability_macro_range", "2", Synth_set_option::Kind::file, "macro channel pressure reach in density bins (0,16]"},
    {"place.preserve_imported_ties",
     "false",
     Synth_set_option::Kind::boolean,
     "preserve characterized imported shared tie masters and nets for fixed-netlist physical replay"},
    {"place.unknown_const",
     "error",
     Synth_set_option::Kind::file,
     "error refuses a mapped net tied to an unknown (X) constant; zero ties those bits low, a legal refinement of a don't-care"},
    {"place.io_iterations",
     "500",
     Synth_set_option::Kind::integer,
     "automatic IO prototype iteration cap; zero uses place.iterations"},
    {"place.schedule_iterations", "500", Synth_set_option::Kind::integer, "GP annealing horizon; zero uses iteration budget"},
    {"place.threads",
     "1",
     Synth_set_option::Kind::integer,
     "CPU worker count; threading needs --define=dreamplace_openmp=true at build time"},
    {"place.timing_driven",
     "true",
     Synth_set_option::Kind::boolean,
     "post-synth timing weights and placed-RC refinement for full placement; false disables for debugging; requires OpenTimer"},
    {"place.timing_rounds", "3", Synth_set_option::Kind::integer, "timing-driven placement checkpoints (1..8)"},
    {"place.legalize.eco_connectivity_weight",
     "0",
     Synth_set_option::Kind::file,
     "incident-net wirelength cost during edited-cell legalization, 0..100; zero keeps nearest-seed placement"},
    {"place.legalize.bin.max_density", "0.8", Synth_set_option::Kind::file, "hard per-bin density cap for terminal legalization"},
    {"place.legalize.bin.grid",
     "route",
     Synth_set_option::Kind::file,
     "terminal legalization bins per axis: route = the routing grid (bounded for tiny cores), or an explicit 1..1024"},
    {"place.legalize.max_density", "0.8", Synth_set_option::Kind::file, "DEPRECATED: use synth.place.legalize.bin.max_density"},
    {"place.legalize.critical_fraction",
     "0.1",
     Synth_set_option::Kind::file,
     "fraction of timed data nets whose cells are fixed first"},
    {"place.legalize.local_pair_fraction",
     "0.5",
     Synth_set_option::Kind::file,
     "local SAT occupancy budget as a fraction of target density"},
    {"place.legalize.padding_sites",
     "1",
     Synth_set_option::Kind::integer,
     "reserved sites on each side of terminally legalized cells"},
    {"place.legalize.warpspace",
     "false",
     Synth_set_option::Kind::boolean,
     "warp unprotected cell targets before terminal legalization"},
    {"place.legalize.warpspace.weight", "1", Synth_set_option::Kind::file, "unprotected cell warp displacement gain, 0..1"},
    {"place.legalize.warpspace.seconds",
     "3",
     Synth_set_option::Kind::file,
     "time limit per terminal warp axis solve, (0,30] seconds"},
    {"place.legalize.warpspace.bins", "16", Synth_set_option::Kind::integer, "terminal warp grid bins per axis, 2..64"},
    {"place.legalize.remainder_wire_weight",
     "4",
     Synth_set_option::Kind::file,
     "wirelength weight when relocating unprotected cells; 0 uses displacement only"},
    {"place.legalize.padding_max_extra_rows",
     "0",
     Synth_set_option::Kind::integer,
     "relax noncritical padding if it adds more than this many rows of movement; 0 disables"},
    {"place.legalize.critical_padding_sites",
     "0",
     Synth_set_option::Kind::integer,
     "reserved sites on each side of protected critical cells"},
    {"place.legalize.critical_fanin_depth",
     "0",
     Synth_set_option::Kind::integer,
     "optional untimed side-input support depth for critical cells, 0..8"},
    {"place.legalize.joint_minimize_total",
     "true",
     Synth_set_option::Kind::boolean,
     "minimize total critical SAT displacement after maximum displacement"},
    {"place.legalize.candidates", "12", Synth_set_option::Kind::integer, "retained legal positions per SAT endpoint and anchor"},
    {"place.legalize.pair_move_sites",
     "4",
     Synth_set_option::Kind::integer,
     "maximum SAT displacement in sites from original GP coordinates"},
    {"place.legalize.pair_move_rows",
     "1",
     Synth_set_option::Kind::integer,
     "maximum SAT displacement in rows from original GP coordinates"},
    {"place.legalize.critical_pair_limit",
     "256",
     Synth_set_option::Kind::integer,
     "global critical SAT proposal budget; zero disables alignment"},
    {"place.legalize.local_pair_limit",
     "0",
     Synth_set_option::Kind::integer,
     "local SAT proposal budget; zero disables local pairs"},
    {"place.legalize.timing_corrections",
     "0",
     Synth_set_option::Kind::integer,
     "optional bounded local timing correction attempts after detail, 0..2"},
    {"place.legalize.timing_strength",
     "0",
     Synth_set_option::Kind::file,
     "optional timing displacement scaling, 0..99; zero keeps legacy legalization"},
    {"place.legalize.timing_threshold",
     "0",
     Synth_set_option::Kind::file,
     "criticality protection threshold, 0..1; zero gives continuous priorities"},
    {"place.timing_max_weight", "5", Synth_set_option::Kind::file, "worst selected net target weight, finite, 1..100"},
    {"place.timing_exponent",
     "1",
     Synth_set_option::Kind::file,
     "criticality power, finite >=1; 1 is linear, 3 concentrates extra pull on the most critical selected nets"},
    {"place.timing_spread_weight",
     "0",
     Synth_set_option::Kind::file,
     "extra routing/local spreading protection from incident timing weights, [0,1]; 0 off, 1 inverse-weight response"},
    {"place.macro.detailed",
     "true",
     Synth_set_option::Kind::boolean,
     "run detailed cell placement in the movable-macro stage when cell legalization is enabled"},
    {"place.macro.legalize_cells",
     "true",
     Synth_set_option::Kind::boolean,
     "false legalizes only macros, skips macro cell detail, and reuses pre-snap GP timing for resynthesis; final still legalizes "
     "cells"},
    {"place.final.timing_start",
     "200",
     Synth_set_option::Kind::integer,
     "minimum final GP iterations before timing checkpoints or adaptive timing trials; final reports remain enabled"},
    {"place.timing_profile",
     "false",
     Synth_set_option::Kind::boolean,
     "log disjoint STA cost buckets for every physical timing evaluation"},
    {"place.final.timing_schedule",
     "fixed",
     Synth_set_option::Kind::file,
     "fixed|late: late uses 4x, 2x, then 1x macro.timing_interval over the first half, next quarter and last quarter after "
     "warm-up"},
    {"place.handoff.taps",
     "false",
     Synth_set_option::Kind::boolean,
     "technology-profile fixed tap/endcap generation and cut rows; requires fresh final terminal legalization"},
    {"place.pg",
     "none",
     Synth_set_option::Kind::file,
     "none|off|auto|asap7|pg_asap7: native power grid after final legalization; requires handoff.taps"},
    {"place.pg.channel.patches",
     "",
     Synth_set_option::Kind::file,
     "experimental PG channels: semicolon-separated style,layer,xl,yl,xh,yh in microns; style skip or gap; empty disables"},
    {"place.pg.channel.max_ir_drop",
     "",
     Synth_set_option::Kind::file,
     "maximum additional channel IR drop in volts for independent validation; empty leaves IR acceptance unconfigured"},
    {"place.preroute", "false", Synth_set_option::Kind::boolean, "signal pre-routing after final placement and repair"},
    {"place.preroute.capacity",
     "0.30",
     Synth_set_option::Kind::file,
     "maximum fraction of usable tracks reserved per layer and gcell, 0..1"},
    {"place.preroute.setup_margin", "20", Synth_set_option::Kind::file, "minimum pre-route setup slack in ps"},
    {"place.preroute.hold_margin", "-1", Synth_set_option::Kind::file, "minimum hold slack in ps; -1 uses two HOLD_BUF delays"},
    {"place.preroute.hold_buffer", "auto", Synth_set_option::Kind::file, "Liberty hold buffer; auto uses the profile default"},
    {"place.preroute.nominal_slew", "20", Synth_set_option::Kind::file, "HOLD_BUF nominal input slew in ps, at fanout-one load"},
    {"place.preroute.budget_ms", "10", Synth_set_option::Kind::integer, "per-net pre-route search budget in milliseconds"},
    {"place.preroute.stage_budget_ms", "10000", Synth_set_option::Kind::integer, "whole pre-route search budget in milliseconds"},
    {"place.preroute.bins", "64", Synth_set_option::Kind::integer, "pre-route capacity/RSMT bins per axis, 1..512"},
    {"place.preroute.max_rows", "0", Synth_set_option::Kind::integer, "maximum original-net pin Y span in rows; 0 unlimited"},
    {"place.preroute.max_sites", "0", Synth_set_option::Kind::integer, "maximum original-net pin X span in sites; 0 unlimited"},
    {"place.final.independent_timing",
     "false",
     Synth_set_option::Kind::boolean,
     "start final cells with synthesis weights, then refresh timing after density settles instead of inheriting macro weights"},
    {"place.macro.timing_interval",
     "50",
     Synth_set_option::Kind::integer,
     "macro GP timing checkpoint cadence; 0 disables; inherits FP movement/density gate and update budget; no resizing"},
    {"place.timing_smoothing",
     "0.5",
     Synth_set_option::Kind::file,
     "new target share in timing weight updates [0,1]; 0 holds prior weights, 1 applies targets directly"},
    {"place.timing_fraction",
     "0.2",
     Synth_set_option::Kind::file,
     "fraction of timed non-clock nets ranked most critical by the approximate GP timer, in (0,1]; 0.1 selects 10%"},
    {"place.resynth",
     "false",
     Synth_set_option::Kind::boolean,
     "opt in to proved critical-cone resynthesis with local placement and timing acceptance"},
    {"place.io_resynth",
     "5",
     Synth_set_option::Kind::file,
     "whole-color placement-aware resynthesis: critical margin and tightening threshold as percent of the clock period; "
     "0 disables; ABC full placement with OpenTimer and --workdir; at most one automatic round, then rerun on the same workdir"},
    {"place.resize",
     "true",
     Synth_set_option::Kind::boolean,
     "timing-checked gate sizing, buffer removal and Steiner repair with single-cell legalization"},
    {"place.resize.gp",
     "true",
     Synth_set_option::Kind::boolean,
     "legacy GP repair switch retained for compatibility; buffering runs after placement"},
    {"place.resize.gp_margin",
     "0.01",
     Synth_set_option::Kind::file,
     "legacy GP buffering slack dead band retained for compatibility; range [0,1]"},
    {"place.resize.gp_trials",
     "4",
     Synth_set_option::Kind::integer,
     "legacy GP buffer trial budget retained for compatibility; range [1,16]"},
    {"place.resize.engine",
     "legacy",
     Synth_set_option::Kind::file,
     "new|legacy: new = path-based sizing/buffering evaluated on the incremental what-if timer (FP checkpoints and after "
     "legal placement); legacy = the batch sizing/forest sweeps"},
    {"place.resize.vt_swap",
     "false",
     Synth_set_option::Kind::boolean,
     "engine=new: allow same-footprint, same-function swaps across Liberty files (multi-VT); default keeps each cell's file"},
    {"place.resize.dont_use",
     "",
     Synth_set_option::Kind::file,
     "comma-separated cell-name globs never chosen as a new master or repeater by either resize engine"},
    {"adder", "auto", Synth_set_option::Kind::file, "shared default: auto|rca|cska|cla|prefix|brent; source pins override it"},
    {"multiplier", "auto", Synth_set_option::Kind::file, "shared default: auto|array|tree|csa|tdm"},
    {"barrel", "auto", Synth_set_option::Kind::file, "shared default: auto|log|reverse"},
    {"block_size", "0", Synth_set_option::Kind::integer, "shared CSKA/CLA block width; 0 derives it from width"},
    {"memory",
     "auto",
     Synth_set_option::Kind::file,
     "auto|flops|macro: bit-blast memories to flops + decode (flops), keep the native macro (macro), or let "
     "pass.abc.memory_max_bits and "
     "the port count decide (auto); an explicit pass.abc.memory wins; source pins override it per memory"},
    {"delay",
     "",
     Synth_set_option::Kind::integer,
     "timing budget in ps for every region (= pass.abc.delay); source pins override it"},
    {"delay_frac",
     "0.4",
     Synth_set_option::Kind::file,
     "timing budget as a fraction of the smallest create_clock period of synth.sdc (e.g. 0.4); used only when no "
     "synth.delay or pass.abc.delay is set, so one value can be shared by designs with different clocks"},
    {"stop_mux",
     "",
     Synth_set_option::Kind::boolean,
     "cut colors at muxes (= pass.color.synth.stop_mux); empty follows the mapper profile; source pins override it"},
    {"stop_arith",
     "",
     Synth_set_option::Kind::boolean,
     "cut colors at wide adders/multipliers/dividers (= pass.color.synth.stop_arith); empty follows the mapper profile"},
    {"stop_cmp",
     "",
     Synth_set_option::Kind::boolean,
     "cut colors at wide comparisons (= pass.color.synth.stop_cmp); empty follows the mapper profile"},
    {"stop_shift",
     "",
     Synth_set_option::Kind::boolean,
     "cut colors at runtime shifters (= pass.color.synth.stop_shift); empty follows the mapper profile"},
    {"abc_pre",
     "",
     Synth_set_option::Kind::file,
     "`;`-separated ABC optimization steps run before the abc mapper of every region, e.g. `dc2; &dch -f` "
     "(= pass.abc.pre; ignored by usyn); a source pin overrides it per region, and one color is one run, so "
     "cells of a color that disagree are an error"},
    {"color", "", Synth_set_option::Kind::source_only, "no run-wide value; names one explicit synthesis group (a name or integer)"},
    {"preroute_exclude",
     "",
     Synth_set_option::Kind::source_only,
     "no run-wide value; true excludes the annotated scope's mapped nets from LiveHD pre-routing; not a synthesis optimization "
     "lock"},
    {"grow",
     "",
     Synth_set_option::Kind::source_only,
     "no run-wide value; false keeps an explicit group from absorbing neighbouring cones"},
    {"ware",
     "",
     Synth_set_option::Kind::source_only,
     "no run-wide value; true|false whether the region's arithmetic/shift modules are explored; run-wide control is "
     "synth.tune.profile and pass.color.synth.ware_*"},
    {"abc",
     "",
     Synth_set_option::Kind::source_only,
     "no run-wide value; the ABC flow script for the region (run-wide: pass.abc.flow)"},
    {"partition",
     "coarse",
     Synth_set_option::Kind::file,
     "auto|fine|coarse: region granularity, independent of the mapper. fine cuts at wide arithmetic/shifters/compares/muxes "
     "and extracts ware modules (small ABC regions); coarse keeps register-to-register cones and extracts nothing. "
     "auto = the mapper's own profile (abc: fine, usyn: coarse). Explicit color.* settings still win"},
    {"tune.profile",
     "auto",
     Synth_set_option::Kind::file,
     "auto replays compatible decisions; on explicitly explores; off ignores stored decisions"},
    {"tune.validate",
     "structural",
     Synth_set_option::Kind::file,
     "structural|region|design: validation required before storing a tuned choice"},
    {"tune.file", "", Synth_set_option::Kind::file, "portable synthesis decision JSON to replay"},
    {"tune.export", "", Synth_set_option::Kind::file, "export compatible synthesis decisions as versioned JSON"},
    {"tune.attempts", "24", Synth_set_option::Kind::integer, "maximum arithmetic candidates per region during explicit profiling"},
    {"tune.time_ms", "60000", Synth_set_option::Kind::integer, "soft profiling time budget per region"},
    {"tune.patience",
     "3",
     Synth_set_option::Kind::integer,
     "stop exploring a region after this many consecutive non-improving candidates (0 = never)"},
    {"tune.min_share",
     "0.02",
     Synth_set_option::Kind::file,
     "skip regions below this fraction of the mapped area (area objective); regions are explored most-critical / largest first"},
    {"mapper",
     "abc",
     Synth_set_option::Kind::mapper,
     "abc|usyn: ABC synthesis (pass.abc), or unate synthesis (pass.usyn: a domino-gate LUT cover of every region, "
     "technology-mapped by ABC, with the ABC flow as the fallback)"},
    {"liberty",
     "",
     Synth_set_option::Kind::file,
     "PATH -- THE Liberty .lib, for every command that reads one: `lhd synth`, `lhd pass abc` (maps to its cells) "
     "and `lhd pass opentimer` (times with it, when no .lib positional is given). Empty = "
     "$LHD_TECH_DIR/sky130_fd_sc_hd__tt_025C_1v80.lib (install a PDK with `ciel`). It is the ONE spelling -- a "
     "`pass.abc.library` --set is refused -- so no two readers in a flow can land on different cells"},
    {"opentimer",
     "true",
     Synth_set_option::Kind::boolean,
     "run OpenTimer STA on the mapped netlist (timing.json under --workdir/synth, the critical path in the "
     "report). false stops after the ABC map"},
    {"reduce",
     "false",
     Synth_set_option::Kind::boolean,
     "experimental: can reduce synthesis time but degrade QoR (area and depth). Extract repeated one- and two-node "
     "combinational cones into shared definitions before coloring; disabled by default"},
    {"specialize",
     "true",
     Synth_set_option::Kind::boolean,
     "run pass.specialize on the mapper's private copy after pass.color: a constant instance input is folded into a "
     "content-named copy of its state-free callee, and a constant a callee returns reaches its parent. false keeps "
     "every instance boundary as compiled"},
    {"state_sweep",
     "true",
     Synth_set_option::Kind::boolean,
     "opt in to the observability sweep of the mapper's private copy: delete the registers and cells that no output, memory or "
     "body-less cell can observe and narrow each register to its highest observed bit, tracking the demand per bit across the "
     "instance hierarchy (what Yosys gets from flattening and opt_clean). Works on the private copy only and also sweeps the "
     "lowered memories; complements compile.sweep, which edits the flat compile graphs. Enabled by default"},
    {"merge_registers",
     "true",
     Synth_set_option::Kind::boolean,
     "opt in to merging registers that are the same machine (same clock, enable, reset, reset value, width and D driver) on the "
     "mapper's private copy, repeating until nothing merges: Yosys's opt_merge on flops. Enabled by default"},
    {"clock_gate",
     "true",
     Synth_set_option::Kind::boolean,
     "clock gating on the mapper's private copy: registers on one clock whose enables share a conjunct move behind one Clock_cell "
     "(an ICG: a latch and an AND, or the library's integrated cell) carrying the shared part, keeping only the rest as their own "
     "enable; existing Clock_cells are collapsed (a gate in a row whose enable the gates above already check is removed, equal "
     "gates merge). The common part is the safe one to gate: pushing it back into per-register enables later is the easy "
     "direction"},
    {"clock_gate_min_bits",
     "16",
     Synth_set_option::Kind::integer,
     "synth.clock_gate: the fewest register bits a new gate must cover (a gate replaces one enable mux per bit; lhdtrack on "
     "sky130: "
     "16 keeps the -5% area and loses almost nothing, 8 adds a few small designs that get larger)"},
    {"clock_gate_depth",
     "1",
     Synth_set_option::Kind::integer,
     "synth.clock_gate: how many gates may be chained clock -> gate -> gate -> register, each factoring the next shared conjunct "
     "(default 1: pass.lec "
     "cannot prove a mapped cascade of inferred gates yet)"},
    {"sdc", "", Synth_set_option::Kind::file, "PATH -- optional .sdc timing constraints handed to pass.opentimer"},
    {"spef", "", Synth_set_option::Kind::file, "PATH -- optional .spef parasitics handed to pass.opentimer"},
};

// Placement partitions are named by role (todo 3g-partitions): `grid` for the
// uniform GP force fields (synth.place.grid.*), `region` for the detail-final
// rectangles (synth.place.region.*), `bin` for legalization capacity cells
// (synth.place.legalize.bin.*, synth.place.satlegalize.bin.*). An old spelling
// stays accepted, as a DEPRECATED registry entry, for one release:
// canonicalize_synth_sets warns and rewrites it to the new name before
// anything reads it, so every consumer and cache key sees one spelling. A
// boolean that became an enum value maps through true_value/false_value.
// dynamic_bins and auto_bins both map to grid.density_mode; they merge into
// one mode unless both are true or grid.density_mode is also given, and then
// placement_options reads them (rejecting a mix with the mode).
struct Synth_set_rename {
  std::string_view old_name, new_name;
  std::string_view true_value = {}, false_value = {};
};
inline constexpr Synth_set_rename kSynthSetRenames[] = {
    {                                "place.bins",                         "place.grid.density"},
    {                             "place.bin_max",                     "place.grid.density_max"},
    {                  "place.local_density_bins",                           "place.grid.local"},
    {                        "place.dynamic_bins",                   "place.grid.density_mode", "dynamic", "fixed"},
    {                           "place.auto_bins",                   "place.grid.density_mode",   "macro", "fixed"},
    {                  "place.busorder.max_cells",                     "place.region.max_cells"},
    {                   "place.busorder.merge_um",                      "place.region.merge_um"},
    {                            "place.busorder",                      "place.region.busorder"},
    {                 "place.busorder.iterations",           "place.region.busorder.iterations"},
    {                    "place.busorder.refresh",              "place.region.busorder.refresh"},
    {                     "place.busorder.weight",               "place.region.busorder.weight"},
    {        "place.busorder.max_displacement_um",  "place.region.busorder.max_displacement_um"},
    {                        "place.boundarypins",                  "place.region.boundarypins"},
    {                  "place.boundarypins.split",            "place.region.boundarypins.split"},
    {                  "place.boundarypins.order",            "place.region.boundarypins.order"},
    {             "place.boundarypins.iterations",       "place.region.boundarypins.iterations"},
    {    "place.boundarypins.max_displacement_um", "place.region.boundarypins.max_displacement_um"},
    {                            "place.pinalign",                      "place.region.pinalign"},
    {                "place.legalize.max_density",             "place.legalize.bin.max_density"},
    {             "place.satlegalize.region_size",                "place.satlegalize.bin.size_um"},
    {              "place.satlegalize.route_grid",                   "place.satlegalize.bin.grid",   "route",  "none"},
};

// The `lhd pass` subcommand vocabulary (pass_command dispatches exactly these).
// ONE spelling for every surface that lists it -- the bare-`pass` usage error,
// the unknown-subcommand hints, the general help and the machine records --
// so the lists can never disagree again.
inline constexpr std::string_view kPassSubcommands
    = "color <alg> | partition | single_edge | satopt | abc | usyn | opentimer | formal | liberty gensim | semdiff | analyze";

// One --set/--config option in the `pass.flag` vocabulary: an EPRP label of
// the method that consumes it. Enumerated from the live registry, so
// `lhd list options`, --set validation, and the lhd.toml tables can never
// drift apart.
struct Set_option {
  std::string name;           // "cgen.srcmap"
  std::string method;         // "inou.cgen.verilog" (the consuming EPRP method)
  std::string default_value;  // "" = no default
  std::string help;           // the registered help text, in full
};

// Every option --set/--config accepts, sorted by name. Runs init_engine()
// itself (idempotent).
std::vector<Set_option> list_set_options();

// Engine commands (compile/lec/scan/pass/tool/lsp). Requires the pass registry
// to be initialized. Throws Lhd_error or std::exception on failure.
void run_engine_command(Options& opts, Result& res);

// Map an engine failure to the error.class taxonomy via the diag sink (the
// most recent error-severity record); falls back to `internal`.
Lhd_error classify_engine_failure(std::string_view fallback_msg);

// Initialize the pass/inou registry: every static Pass_plugin plus
// setup_inou_yosys() (no REPL-style Top/Meta command surface).
void init_engine();

// Name the crash. A pass that dies on SIGSEGV (ABC and the solvers do not
// null-check their allocations) unwinds nothing, so no diagnostic, no result
// envelope and no exit-code class ever reach the user -- and off glibc the
// installed handler prints not even a backtrace, leaving a bare exit 1 with no
// output at all. install_crash_reporter() reports which step died and where its
// log is, from data run_step parks in fixed buffers for the handler to read.
void install_crash_reporter();

// Deterministic content-hash run_id over (tool version + command + resolved
// config + input bytes). The input bytes include the transitive on-disk import
// closure of every Pyrope source (resolved like the compile driver does), so
// an edit of an imported file moves it. A lec --impl/--ref side of kind lg:
// hashes only its per-side --top slice — the top graph(s) plus transitive Sub
// dependencies, bodies AND library.txt IO declarations — because the proof
// reads nothing else, so nothing else may move the run_id. The side role and
// per-side top hash into every impl/ref row (file or directory); --lib model
// libraries and every other directory input hash whole.
std::string compute_run_id(const Options& opts);

// Serialize the result envelope (single JSON line) to --result-json or stdout.
void write_result(const Options& opts, const Result& res);

}  // namespace lhd
