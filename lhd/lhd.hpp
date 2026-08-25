//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// lhd — the stateless, hermetic LiveHD CLI kernel.
// Contract: the LiveHD docs ("Stateless build-system mode").
//
// One invocation = one step: (declared inputs, config) -> (declared outputs,
// exit code). No @tag, no ~/.cache, no lock, no `latest` symlink. lhd drives
// the registered EPRP methods programmatically (Eprp::run_method_now) plus
// the direct C++ entry points (Lnast::dump, uPass_tolg::run,
// livehd::Hhds_graph_library). The legacy lgshell REPL was removed
// 2026-06-04 (lhd is the only driver; `lhd pyrope lsp` serves the LSP).

#include <cstddef>
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
  std::string command;   // compile|lec|scan|pyrope|tool|pass|list|describe|version|help
  std::string language;  // verilog|pyrope ("" for the IR/meta commands)

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
  std::string recipe;  // resolved per-command default in the kernel
  std::string config;  // --config lhd.toml: pass-flag defaults (CLI --set/--recipe win)

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
  // `lhd lec` / `lhd formal verify` (canonical knob `formal.stats`) a cvc5
  // solve-insight report (problem size, conflicts = learned clauses, decisions,
  // propagations, restarts, theory lemmas, resource units, timings). The formal
  // consumer also registers a cvc5 plugin that makes the solve ~8x SLOWER, so it is a
  // diagnosis tool — never leave it on, and never time a run with it.
  bool stats = false;

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
  int         fmt_indent = 0;       // --indent N: spaces per level (0 => prpfmt default 4)
  int         fmt_width  = 0;       // --width N: wrap column (0 => prpfmt default 80)
  bool        fmt_verify = false;   // --verify: re-parse the formatted output

  std::string result_json;
  std::string workdir;
  // Set by workdir() when it MINTED an ephemeral scratch dir because the user
  // named none. Every persistent-reuse gate (compile cache, abc_cache,
  // formal verdict cache) keys on "user-named workdir", so a command that
  // needs a scratch path BEFORE it runs a sub-flow (synth mints <scratch>/synth
  // and then compiles into it) must not turn reuse on by accident.
  bool        workdir_scratch = false;

  std::vector<std::string> raw_args;  // after `--` (elaborate verilog: raw slang args)

  bool quiet   = false;
  bool verbose = false;

  // `--list-tests`: enumerate the source's TEST UNITS as JSON (or a human list in
  // pretty mode) and exit without building/proving anything. Shared by the two
  // test-bearing commands so they read the same: `sim` lists its `test` blocks,
  // `formal verify` lists its `formal` blocks (each is an independent test).
  bool list_tests = false;

  // `sim` command modifiers
  bool sim_setup_only = false;  // generate the C++ sim, do NOT build/run
  bool sim_run_only   = false;  // build/run an already-generated sim (needs --workdir), no regen
  // `sim --arg key=value` (repeatable): bind a `test name(params)` parameter; an
  // override wins over the parameter's default. A param with neither is an error.
  std::vector<std::pair<std::string, std::string>> sim_args;
  // `sim` debug-replay flags (sim_checkpoint_debug_plan). The driver loads the
  // nearest checkpoint <= the target and resumes from there. -1 = not requested.
  long                                             sim_restart_cycle = -1;  // --restart-cycle N: jump to cycle N
  long                                             sim_vcd_from      = -1;  // --vcd-from Y: trace VCD starting at cycle Y
  long                                             sim_vcd_to        = -1;  // --vcd-to Z: trace VCD up to cycle Z (with --vcd-from)
  bool        sim_vcd_on_fail     = false;  // --vcd-on-fail: re-run a failed test with a VCD of the failure region
  long        sim_vcd_fail_window = 20;     // --vcd-fail-window N: cycles before the failure to trace
  // `sim` observability: query signal values without re-instrumenting (the driver
  // snapshots scalar signals by hierarchical name). Results land in the result
  // envelope's "debug" member (and `--result-json`).
  bool        sim_list_signals    = false;  // --list-signals: enumerate observable signals, then exit
  std::string sim_probe;                    // --probe SIG,...: per-cycle JSON trajectory of these signals
  long        sim_probe_from = -1;          // --probe-from A
  long        sim_probe_to   = -1;          // --probe-to B
  std::string sim_break_when;               // --break-when 'SIG OP VALUE|SIG': first cycle the condition holds
  // `sim --query FILE|-|{inline}` (2f-sim): a BATCHED JSON request
  // ({schema_version:1, kind:"sim_query", queries:[...]}). Batching is what lets
  // the planner union every question's time range and answer them all from ONE
  // replay; the legacy flags above stay the low-ceremony spelling of the same
  // engine. Answers land in the envelope's "query" member.
  std::string sim_query;
  bool        sim_observe         = false;  // setup-time hierarchical instrumentation needed by VCD/probe/query
  bool        sim_runtime_support = true;   // generated checkpoint/probe/query methods; false only for a lean checkpoint-off setup

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

  // Internal hand-off from Tier A (source/LNAST sync) to Tier B (final LGraph
  // restore/store). Not serialized; the public machine contract is the stats
  // object above. A graph inventory independently records this closure key, so
  // a compile that fails after updating the parse cache cannot authorize a
  // stale pre-failure graph generation on the next run.
  std::string                                      compile_cache_scope;
  std::string                                      compile_cache_context;
  std::string                                      compile_cache_closure_key;
  std::vector<std::pair<std::string, std::string>> compile_cache_unit_keys;
  std::vector<std::string>                         compile_cache_clean_units;
  std::vector<std::string>                         compile_cache_restored_graphs;
  // Clean final graph bodies to overlay after a diagnostic-carrying partial
  // restore is refused and the complete pipeline runs live.
  std::vector<std::string>                         compile_cache_overlay_graphs;
  // Unit names of this scope's PRIOR generation (empty when none/incompatible).
  // Ghost pruning may delete artifacts of a unit that left the closure only
  // when that unit provably belonged to this same scope's previous compile.
  std::vector<std::string>                         compile_cache_prior_units;
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

  // `lhd pass abc` QoR payload (2opt-freq A): the qor.json sidecar content
  // (per-region + total mapped gates/area/critical delay, source-attributed),
  // embedded verbatim as the result's "qor" member.
  std::string qor_json;

  std::string error_class;  // empty when status == pass (future_cli.md taxonomy)
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
  enum class Kind { boolean, non_neg_num, bool_or_file, backend };  // value grammar enforced on --set
  std::string_view name;                                            // flag under sim.*, e.g. "checkpoint_min_secs"
  std::string_view default_value;                                   // shown by `lhd list options`
  Kind             kind;
  std::string_view help;  // full help (also `lhd describe sim.flag`)
};

inline constexpr Sim_set_option kSimSetOptions[] = {
    {                "backend",
     "slop",      Sim_set_option::Kind::backend,
     "slop|llvm — simulator color-kernel backend. llvm is experimental and emits native object files directly; "
     "unsupported colors fall back to the reference Slop C++ lowering"                                     },
    {                    "vcd",
     "false", Sim_set_option::Kind::bool_or_file,
     "false|true|FILE — VCD tracing, the ONE vcd knob for every flow. `lhd sim`: any non-false value dumps one VCD "
     "per test to <workdir>/<test.name>.vcd. Compiled sim binaries (--emit-dir sim:): true bakes <top>.vcd, "
     "FILE bakes that explicit path, false bakes none"                                                     },
    {         "vcd_fake_delay",
     "true",      Sim_set_option::Kind::boolean,
     "VCD data settles a few ticks after each clock edge, with X during the settle window (edge->data causality); "
     "false = plain edge-aligned updates (no X, no delay; smaller/faster trace)"                           },
    {               "hlop_dir",
     "", Sim_set_option::Kind::bool_or_file,
     "DIR — hlop checkout to build the sim driver against (resolves slop.hpp/blop.hpp/vcd_writer.hpp). Empty = "
     "auto: the bazel runfiles, else the sibling ../hlop of a source checkout. Set it to build the driver against "
     "a WIP hlop — testing new slop/vcd_writer code without reinstalling it is the reason this knob exists"},
    {            "iassert_dir",
     "", Sim_set_option::Kind::bool_or_file,
     "DIR — iassert checkout to build the sim driver against (resolves iassert.hpp, which slop.hpp pulls in). "
     "Empty = auto: the bazel runfiles, else the sibling ../iassert/src. Same purpose as sim.hlop_dir"     },
    {                "flatten",
     "0",  Sim_set_option::Kind::non_neg_num,
     "N — structurally inline a sub-instance into its parent before occurrence-wide color planning when the "
     "callee body has <= N nodes. 0 keeps hierarchy intact. Inlining may reduce storage-path depth but duplicates "
     "the body per instantiation, so use it only for measured experiments"                                 },
    {                  "ninja",
     "", Sim_set_option::Kind::bool_or_file,
     "false|true|PATH — build the sim driver with ninja instead of the built-in parallel compile. Empty (the "
     "default) uses ninja when it is on PATH and the built-in build otherwise; true REQUIRES it; PATH names the "
     "binary. Ninja is what makes the host build incremental (depfile-accurate, so a header edit rebuilds exactly "
     "its dependents); the built-in path always rebuilds every translation unit. A `build.ninja` reproducing the "
     "exact build is written into the sim dir either way — `ninja -C <workdir>/sim`"                       },
    {                   "jobs",
     "0",  Sim_set_option::Kind::non_neg_num,
     "host C++ compiles to run concurrently when building the sim driver (0 = one per hardware thread). Each "
     "generated module body is its own translation unit sharing only headers, so the build parallelizes flat; "
     "pin this to reproduce a build-time measurement, or to leave the machine usable on a big design"      },
    {                 "slop_u",
     "true",      Sim_set_option::Kind::boolean,
     "materialize LGraph-proven unsigned combinational values as the CANONICAL-unsigned Slop_u<n> instead of a "
     "lazily-masked Slop<n+1>. Slop makes no promise about storage above bit n-1, so every READ of a stored value "
     "re-masks; Slop_u pays ONE mask at the write and none at the reads. Reset-free state and other unknown-capable "
     "boundaries remain Slop. Set false only for lowering comparisons"                                     },
    {            "color_dirty",
     "true",      Sim_set_option::Kind::boolean,
     "cross-cycle color activation cache. false executes every color once in its existing static phase order and "
     "emits direct boundary assignments instead of change comparisons and dirty propagation; intended for measured "
     "scheduler-overhead comparisons"                                                                      },
    {                  "debug",
     "false",      Sim_set_option::Kind::boolean,
     "retain runtime validation landings for bitwidth-proven unsigned Slop_u values. The default trusts the proof "
     "and emits only compile-time width checks, avoiding masks in production generated code"               },
    {              "init_zero",
     "false",      Sim_set_option::Kind::boolean,
     "use zero as the power-on value only for flops and memories that have neither an initializer nor a reset. "
     "Explicit initial values and runtime reset values are unchanged"                                      },
    {           "unknown_zero",
     "false",      Sim_set_option::Kind::boolean,
     "fill every unknown (`?`) literal bit with 0 instead of a random 0/1. Slop carries no runtime X, so a `?` must "
     "become some concrete bit; the default DRAWS it from the run's seeded PRNG (--seed / lhd.seed, reported as "
     "run.seed + rng_draws) so an unspecified bit cannot be silently relied on, and the draw is once per literal "
     "per run — the value is stable across cycles. true restores the deterministic-zero fill, which also lets the "
     "literal fold at C++ compile time. Orthogonal to sim.init_zero, which covers the power-on value of state "
     "having neither an initializer nor a reset"                                                           },
    {             "checkpoint",
     "true",      Sim_set_option::Kind::boolean,
     "periodic editable state checkpoints of the DUT + testbench (default on; --restart-cycle needs them)" },
    {    "checkpoint_min_secs",
     "10",  Sim_set_option::Kind::non_neg_num,
     "wall-clock floor in seconds between checkpoints (a short run writes none)"                           },
    {         "checkpoint_max",
     "10",  Sim_set_option::Kind::non_neg_num,
     "max checkpoints kept per test, evenly spaced (older ones are pruned)"                                },
    {"checkpoint_max_overhead",
     "0.10",  Sim_set_option::Kind::non_neg_num,
     "target checkpoint cost as a fraction of run time (caps how often they are taken)"                    },
    {       "checkpoint_every",
     "0",  Sim_set_option::Kind::non_neg_num,
     "deterministic cadence: checkpoint every N cycles (0 = time-based, the default)"                      },
};

// The `synth.*` command-namespace options (consumed by synth_command -- the
// one-shot compile -> pass.color reduce -> pass.color synth -> pass.abc ->
// pass.opentimer flow --
// not pass labels). Same contract as kSimSetOptions: this array is the single
// source of truth for --set validation, `lhd list options`, and the
// `lhd synth --help` options block. Pass-level tuning still rides the pass
// namespaces (`--set abc.adder=cla`, `--set color.absorb=false`, ...).
struct Synth_set_option {
  enum class Kind { boolean, file };
  std::string_view name;
  std::string_view default_value;
  Kind             kind;
  std::string_view help;
};

// The Liberty file `synth.liberty` resolves to under $HAGENT_TECH_DIR when the
// knob is empty (the same default pass.abc uses on its own).
inline constexpr std::string_view kSynthDefaultLiberty = "sky130_fd_sc_hd__tt_025C_1v80.lib";

inline constexpr Synth_set_option kSynthSetOptions[] = {
    {  "liberty",
     "",    Synth_set_option::Kind::file,
     "PATH -- the ONE Liberty .lib for the whole flow: pass.abc maps to its cells and pass.opentimer times with "
     "it. Empty = $HAGENT_TECH_DIR/sky130_fd_sc_hd__tt_025C_1v80.lib (install a PDK with `ciel`). A "
     "`pass.abc.library` --set is refused under synth so the two stages can never disagree"                                },
    {"opentimer",
     "true", Synth_set_option::Kind::boolean,
     "run OpenTimer STA on the mapped netlist (timing.json under --workdir/synth, the critical path in the "
     "report). false stops after the ABC map"                                                                              },
    {   "reduce",
     "true", Synth_set_option::Kind::boolean,
     "extract repeated one- and two-node combinational cones into shared definitions before synthesis coloring. "
     "This bounds duplicated wide operations in large generated designs; false keeps the compiled graph shape"             },
    {      "sdc", "",    Synth_set_option::Kind::file,  "PATH -- optional .sdc timing constraints handed to pass.opentimer"},
    {     "spef", "",    Synth_set_option::Kind::file,         "PATH -- optional .spef parasitics handed to pass.opentimer"},
};

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

// Deterministic content-hash run_id over (tool version + command + resolved
// config + input bytes). A lec --impl/--ref side of kind lg: hashes only its
// per-side --top slice — the top graph(s) plus transitive Sub dependencies,
// bodies AND library.txt IO declarations — because the proof reads nothing
// else, so nothing else may move the run_id. Per-side tops hash into every
// impl/ref row (file or directory); --lib model libraries and every other
// directory input hash whole.
std::string compute_run_id(const Options& opts);

// Serialize the result envelope (single JSON line) to --result-json or stdout.
void write_result(const Options& opts, const Result& res);

}  // namespace lhd
