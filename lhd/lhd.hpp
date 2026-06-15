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
// 2026-06-04 (lhd is the only driver; `lhd lsp` serves the LSP).

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
  std::string command;   // elaborate|synth|check|compile|list|describe|version|help
  std::string language;  // verilog|pyrope ("" for the IR/meta commands)

  std::vector<std::string> files;  // positional: source files / list pattern / describe name

  std::vector<Typed_path> emits;
  std::vector<Typed_path> emit_dirs;
  std::vector<Typed_path> ins;
  std::vector<Typed_path> in_dirs;

  std::string top;
  // elaborate verilog: yosys-verilog|yosys-slang (yosys -> LGraphs) or
  // slang (the direct inou.slang SV -> LNAST front-end).
  std::string reader = "yosys-slang";
  std::string depfile;
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

  std::string impl_kind, impl_path, impl_top;  // check
  std::string ref_kind, ref_path, ref_top;     // check

  std::string result_json;
  std::string workdir;

  std::vector<std::string> raw_args;  // after `--` (elaborate verilog: raw slang args)

  int  jobs    = 0;
  bool quiet   = false;
  bool verbose = false;

  Diag_fmt diag_fmt = default_diag_fmt();
};

// The structured result envelope (future_cli.md "Result schema"). Written as
// one JSON object to --result-json (else stdout).
struct Result {
  std::string command;          // e.g. "elaborate verilog"
  std::string status = "pass";  // pass|fail
  std::string run_id;           // content hash (deterministic, never wall clock)
  int         exit_code = 0;

  std::vector<std::string> inputs;
  std::vector<std::string> outputs;
  std::vector<std::string> recipe_steps;  // the expanded steps that actually ran

  // `lhd scan` payload: a pre-serialized JSON array of per-file import lists,
  // embedded verbatim as the result's "scan" member.
  std::string scan_json;

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

// Engine commands (elaborate/synth/check/compile). Requires the pass registry
// to be initialized. Throws Lhd_error or std::exception on failure.
void run_engine_command(Options& opts, Result& res);

// Map an engine failure to the error.class taxonomy via the diag sink (the
// most recent error-severity record); falls back to `internal`.
Lhd_error classify_engine_failure(std::string_view fallback_msg);

// Initialize the pass/inou registry: every static Pass_plugin plus
// setup_inou_yosys() (no REPL-style Top/Meta command surface).
void init_engine();

// Deterministic content-hash run_id over (tool version + command + resolved
// config + input bytes).
std::string compute_run_id(const Options& opts);

// Serialize the result envelope (single JSON line) to --result-json or stdout.
void write_result(const Options& opts, const Result& res);

}  // namespace lhd
