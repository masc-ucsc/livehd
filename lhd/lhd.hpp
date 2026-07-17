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
  std::string reader = "slang";
  std::string depfile;
  // --unused-inputs PATH (compile): write the declared source-file positionals
  // whose contents did NOT reach the compiled closure (absent from every final
  // unit's Source_locator table, e.g. a .sv outside the --top hierarchy) — one
  // cwd(exec-root)-relative path per line, empty when everything was read; the
  // format Bazel's unused_inputs_list consumes for input pruning.
  std::string unused_inputs;
  std::string recipe;       // resolved per-command default in the kernel
  std::string recipe_file;  // deferred (unsupported)
  std::string config;       // --config lhd.toml: pass-flag defaults (CLI --set/--recipe win)

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
  std::string tool_target;
  // `tool tree --target kind:<X>` (repeatable): node kinds to list inside each
  // module of the hierarchy — registers/memories that ride the same instance
  // tree. Empty => the bare instance tree (default). `kind:register` aliases
  // flop/fflop/latch, `kind:memory` aliases memory; any Ntype name (flop, mux,
  // sub, …) also matches exactly.
  std::vector<std::string> tool_kinds;
  std::string tool_attr;
  int         tool_max     = 200;
  int         tool_hier    = -1;
  int         tool_hops    = 0;
  int         tool_context = 2;     // `tool diff -C n` text-line context
  bool        tool_invert  = false; // `tool grep -v`: keep records that do NOT match
  bool        tool_match   = false; // `tool diff --match`: visualize via the semdiff `match` attribute

  bool        stats = false;  // `pass semdiff --stats`: aggregate node/register/memory match report

  std::string impl_kind, impl_path, impl_top;  // lec --impl
  std::string ref_kind, ref_path, ref_top;     // lec --ref
  std::string formal_filter;                   // formal verify / lec: formal-block name glob
  // lec --collapse <def> (repeatable): module-def names the driver has already
  // proven equivalent, forced to the sound black-box path even when --lib could
  // flatten them (proven-module collapse — the parent stops re-solving them).
  std::vector<std::string> collapse;

  // `lhd pyrope fmt` formatter knobs (clang-format-like). Consumed only by the
  // pyrope command; harmless defaults elsewhere.
  bool        fmt_inplace = false;  // -i / --inplace: rewrite each input file
  std::string fmt_output;           // -o / --output FILE: write to FILE (one input)
  int         fmt_indent  = 0;      // --indent N: spaces per level (0 => prpfmt default 4)
  int         fmt_width   = 0;      // --width N: wrap column (0 => prpfmt default 80)
  bool        fmt_verify  = false;  // --verify: re-parse the formatted output

  std::string result_json;
  std::string workdir;

  std::vector<std::string> raw_args;  // after `--` (elaborate verilog: raw slang args)

  int  jobs    = 0;
  bool quiet   = false;
  bool verbose = false;

  // `sim` command modifiers
  bool sim_setup_only = false;  // generate the C++ sim, do NOT build/run
  bool sim_run_only   = false;  // build/run an already-generated sim (needs --workdir), no regen
  bool sim_list_tests = false;  // print the design's tests + parameters as JSON, then exit (no build)
  // `sim --arg key=value` (repeatable): bind a `test name(params)` parameter; an
  // override wins over the parameter's default. A param with neither is an error.
  std::vector<std::pair<std::string, std::string>> sim_args;
  // `sim` debug-replay flags (sim_checkpoint_debug_plan). The driver loads the
  // nearest checkpoint <= the target and resumes from there. -1 = not requested.
  long sim_restart_at = -1;  // --restart-at/--restart-cycle N: jump to cycle N
  long sim_vcd_from   = -1;  // --vcd-from Y: trace VCD starting at cycle Y
  long sim_vcd_to     = -1;  // --vcd-to Z: trace VCD up to cycle Z (with --vcd-from)
  bool sim_vcd_on_fail     = false;  // --vcd-on-fail: re-run a failed test with a VCD of the failure region
  long sim_vcd_fail_window = 20;     // --vcd-fail-window N: cycles before the failure to trace
  // `sim` observability: query signal values without re-instrumenting (the driver
  // snapshots scalar signals by hierarchical name). Results land in the result
  // envelope's "debug" member (and `--result-json`).
  bool        sim_list_signals = false;  // --list-signals: enumerate observable signals, then exit
  std::string sim_probe;                 // --probe SIG,...: per-cycle JSON trajectory of these signals
  long        sim_probe_from = -1;       // --probe-from A
  long        sim_probe_to   = -1;       // --probe-to B
  std::string sim_break_when;            // --break-when 'SIG OP VALUE|SIG': first cycle the condition holds

  Diag_fmt diag_fmt = default_diag_fmt();
};

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
  enum class Kind { boolean, non_neg_num, bool_or_file };  // value grammar enforced on --set
  std::string_view name;                     // flag under sim.*, e.g. "checkpoint_min_secs"
  std::string_view default_value;            // shown by `lhd list options`
  Kind             kind;
  std::string_view help;  // full help (also `lhd describe sim.flag`)
};

inline constexpr Sim_set_option kSimSetOptions[] = {
    {"vcd", "false", Sim_set_option::Kind::bool_or_file,
     "false|true|FILE — VCD tracing, the ONE vcd knob for every flow. `lhd sim`: any non-false value dumps one VCD "
     "per test to <workdir>/<test.name>.vcd. Compiled sim binaries (--emit-dir sim:): true bakes <top>.vcd, "
     "FILE bakes that explicit path, false bakes none"},
    {"vcd_fake_delay", "true", Sim_set_option::Kind::boolean,
     "VCD data settles a few ticks after each clock edge, with X during the settle window (edge->data causality); "
     "false = plain edge-aligned updates (no X, no delay; smaller/faster trace)"},
    {"vcdfakedelay", "true", Sim_set_option::Kind::boolean, "DEPRECATED alias for sim.vcd_fake_delay"},
    {"cgen_color", "true", Sim_set_option::Kind::boolean,
     "run pass.color (cgen per-output cones) before inou.cgen.sim so sim codegen can schedule a Sub by output "
     "cone (breaks a false combinational loop through an instance); coloring is metadata only, NO_COLOR is just "
     "another partition, so inou.cgen.verilog and an un-split sim are unaffected (default on)"},
    {"checkpoint", "true", Sim_set_option::Kind::boolean,
     "periodic editable state checkpoints of the DUT + testbench (default on; --restart-at needs them)"},
    {"checkpoint_min_secs", "10", Sim_set_option::Kind::non_neg_num,
     "wall-clock floor in seconds between checkpoints (a short run writes none)"},
    {"checkpoint_max", "10", Sim_set_option::Kind::non_neg_num,
     "max checkpoints kept per test, evenly spaced (older ones are pruned)"},
    {"checkpoint_max_overhead", "0.10", Sim_set_option::Kind::non_neg_num,
     "target checkpoint cost as a fraction of run time (caps how often they are taken)"},
    {"checkpoint_every", "0", Sim_set_option::Kind::non_neg_num,
     "deterministic cadence: checkpoint every N cycles (0 = time-based, the default)"},
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
