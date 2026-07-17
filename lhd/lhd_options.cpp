//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "lhd.hpp"

namespace lhd {

Diag_fmt default_diag_fmt() { return ::isatty(STDOUT_FILENO) != 0 ? Diag_fmt::pretty : Diag_fmt::jsonl; }

namespace {

// Canonical kind vocabulary. `ln:` is an hhds::Forest::save directory (the
// LNAST units of a design); `lg:` is an hhds::GraphLibrary::save directory
// (the LGraphs of a design). The long spellings stay accepted as aliases.
const std::vector<std::string_view> kInputKinds{"ln", "lg", "verilog", "pyrope"};
const std::vector<std::string_view> kOutputKinds{"ln",
                                                 "lg",
                                                 "verilog",
                                                 "pyrope",
                                                 "lnast-dump",
                                                 "isabelle",
                                                 "lean",
                                                 "sim",
                                                 "graphviz",
                                                 "metadata",
                                                 "results",
                                                 "diagnostics"};

std::string canonical_kind(std::string_view kind) {
  if (kind == "lnast") {
    return "ln";
  }
  if (kind == "design" || kind == "lgraph") {
    return "lg";
  }
  return std::string{kind};
}

bool is_known_kind(std::string_view kind, bool output) {
  const auto& kinds = output ? kOutputKinds : kInputKinds;
  return std::find(kinds.begin(), kinds.end(), kind) != kinds.end();
}

// "KIND:PATH" -> Typed_path. Throws usage on a missing ':' or unknown kind.
Typed_path parse_typed(std::string_view flag, std::string_view arg, bool output) {
  auto pos = arg.find(':');
  if (pos == std::string_view::npos || pos == 0 || pos + 1 >= arg.size()) {
    throw Lhd_error{"usage",
                    std::format("{} expects KIND:PATH, got '{}'", flag, arg),
                    "see `lhd list emit-kinds` for the KIND vocabulary"};
  }
  Typed_path tp{canonical_kind(arg.substr(0, pos)), std::string(arg.substr(pos + 1))};
  if (!is_known_kind(tp.kind, output)) {
    throw Lhd_error{"usage",
                    std::format("unknown {} kind '{}' in '{}'", output ? "output" : "input", tp.kind, arg),
                    "see `lhd list emit-kinds` for the KIND vocabulary"};
  }
  return tp;
}

// A positional argument may be a typed IR input ("ln:DIR" / "lg:DIR"); a
// plain path is a source file. Anything else with a known-kind prefix is
// routed as a typed input too (e.g. "verilog:foo.v").
bool route_positional(Options& opts, std::string_view a) {
  auto pos = a.find(':');
  if (pos == std::string_view::npos || pos == 0) {
    return false;
  }
  auto kind = canonical_kind(a.substr(0, pos));
  if (!is_known_kind(kind, /*output=*/false)) {
    return false;  // not a kind prefix (e.g. a path with a colon) -> source file
  }
  Typed_path tp{kind, std::string(a.substr(pos + 1))};
  if (tp.kind == "ln") {
    opts.in_dirs.push_back(tp);
  } else if (tp.kind == "lg") {
    opts.ins.push_back(tp);
  } else {
    // verilog:/pyrope: prefixed sources are just source files.
    opts.files.push_back(tp.path);
  }
  return true;
}

std::string_view need_value(std::string_view flag, int& i, int argc, char** argv) {
  if (i + 1 >= argc) {
    throw Lhd_error{"usage", std::format("{} requires a value", flag), ""};
  }
  ++i;
  return argv[i];
}

// A non-negative integer flag value (cycle counts: --restart-at/--vcd-from/-to).
long parse_nonneg(std::string_view flag, std::string_view val) {
  size_t consumed = 0;
  long   n        = 0;
  try {
    n = std::stol(std::string{val}, &consumed, 0);  // base 0: decimal or 0x..
  } catch (const std::exception&) {
    consumed = 0;
  }
  if (val.empty() || consumed != val.size() || n < 0) {
    throw Lhd_error{"usage", std::format("{} expects a non-negative integer, got '{}'", flag, val), ""};
  }
  return n;
}

bool ends_with(std::string_view s, std::string_view suffix) {
  return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

// --impl/--ref sides: KIND:PATH, or a bare source path whose kind is
// inferred from the extension (.prp -> pyrope, .v/.sv -> verilog).
Typed_path parse_check_side(std::string_view flag, std::string_view arg) {
  auto pos = arg.find(':');
  if (pos != std::string_view::npos && pos != 0 && is_known_kind(canonical_kind(arg.substr(0, pos)), /*output=*/false)) {
    return parse_typed(flag, arg, /*output=*/false);
  }
  if (ends_with(arg, ".prp")) {
    return {"pyrope", std::string{arg}};
  }
  if (ends_with(arg, ".v") || ends_with(arg, ".sv")) {
    return {"verilog", std::string{arg}};
  }
  throw Lhd_error{"usage",
                  std::format("{} expects KIND:PATH or a .prp/.v/.sv file, got '{}'", flag, arg),
                  "input kinds: verilog, pyrope, ln, lg"};
}

// --emit PATH: KIND:PATH, or a bare output path whose kind is inferred from the
// extension (.v/.sv -> verilog, .prp -> pyrope). Inference is `--emit` only (a
// single file); `--emit-dir` directory containers keep their explicit KIND:DIR.
Typed_path parse_emit_arg(std::string_view flag, std::string_view arg) {
  auto pos = arg.find(':');
  if (pos != std::string_view::npos && pos != 0 && is_known_kind(canonical_kind(arg.substr(0, pos)), /*output=*/true)) {
    return parse_typed(flag, arg, /*output=*/true);
  }
  if (ends_with(arg, ".prp")) {
    return {"pyrope", std::string{arg}};
  }
  if (ends_with(arg, ".v") || ends_with(arg, ".sv")) {
    return {"verilog", std::string{arg}};
  }
  // No extension match: defer to the typed parser for its precise KIND:PATH
  // usage error (a missing ':' or an unknown kind).
  return parse_typed(flag, arg, /*output=*/true);
}

// ---- --config lhd.toml --------------------------------------------------------
// A strict TOML *subset*: `#` comments, `[pass]` tables, and `key = value`
// where value is a quoted string, a boolean, or an integer. Each table entry
// becomes a `--set pass.flag=value` default (prepended, so an explicit CLI
// --set always wins); the only top-level key is `recipe` (CLI --recipe wins).
// Anything outside the subset is a config error — reject rather than misread.

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
    s.remove_prefix(1);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
    s.remove_suffix(1);
  }
  return s;
}

bool is_bare_key(std::string_view s) {
  if (s.empty()) {
    return false;
  }
  for (char c : s) {
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) {
      return false;
    }
  }
  return true;
}

// A --config table name = the command-path namespace (2h-set_path): one or
// more bare segments joined by dots, e.g. [cgen], [upass], [pass.abc].
bool is_table_name(std::string_view s) {
  size_t start = 0;
  for (size_t p = 0; p <= s.size(); ++p) {
    if (p == s.size() || s[p] == '.') {
      if (!is_bare_key(s.substr(start, p - start))) {
        return false;
      }
      start = p + 1;
    }
  }
  return true;
}

// "value" -> value; true/false; integer. Throws on anything else.
std::string toml_value(const std::string& file, int lineno, std::string_view raw) {
  if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
    auto body = raw.substr(1, raw.size() - 2);
    if (body.find('"') != std::string_view::npos || body.find('\\') != std::string_view::npos) {
      throw Lhd_error{"config", std::format("{}:{}: escapes are not supported in config strings", file, lineno), ""};
    }
    return std::string{body};
  }
  if (raw == "true" || raw == "false") {
    return std::string{raw};
  }
  auto digits = raw;
  if (!digits.empty() && digits.front() == '-') {
    digits.remove_prefix(1);
  }
  if (!digits.empty() && std::all_of(digits.begin(), digits.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c));
      })) {
    return std::string{raw};
  }
  throw Lhd_error{"config",
                  std::format("{}:{}: unsupported value '{}'", file, lineno, raw),
                  "config values are \"quoted strings\", true/false, or integers"};
}

// Read `opts.config` and fold it into opts (sets/recipe). Strict subset; see
// the comment block above. Runs before run_id hashing, so a config file and
// the equivalent explicit flags produce the same run_id.
void load_config(Options& opts) {
  if (opts.config.empty()) {
    return;
  }
  std::ifstream ifs(opts.config);
  if (!ifs.is_open()) {
    throw Lhd_error{"missing_file", std::format("config file not found: {}", opts.config), ""};
  }

  std::vector<std::pair<std::string, std::string>> file_sets;
  std::string                                      file_recipe;
  std::string                                      table;  // current [pass] table ("" = top level)
  std::string                                      line;
  int                                              lineno = 0;
  while (std::getline(ifs, line)) {
    ++lineno;
    // Strip comments. The subset has no '#' inside strings to worry about
    // beyond quoted values, so scan outside quotes only.
    bool in_str = false;
    for (size_t p = 0; p < line.size(); ++p) {
      if (line[p] == '"') {
        in_str = !in_str;
      } else if (line[p] == '#' && !in_str) {
        line.resize(p);
        break;
      }
    }
    auto t = trim(line);
    if (t.empty()) {
      continue;
    }
    if (t.front() == '[') {
      if (t.back() != ']') {
        throw Lhd_error{"config", std::format("{}:{}: malformed table header '{}'", opts.config, lineno, t), ""};
      }
      auto name = trim(t.substr(1, t.size() - 2));
      if (!is_table_name(name)) {
        throw Lhd_error{"config",
                        std::format("{}:{}: unsupported table '[{}]'", opts.config, lineno, name),
                        "config tables are command-path pass names: [upass], [cgen], [pass.abc] (`lhd list options`)"};
      }
      table = name;
      continue;
    }
    auto eq = t.find('=');
    if (eq == std::string_view::npos) {
      throw Lhd_error{"config", std::format("{}:{}: expected key = value, got '{}'", opts.config, lineno, t), ""};
    }
    auto key = trim(t.substr(0, eq));
    if (!is_bare_key(key)) {
      throw Lhd_error{"config", std::format("{}:{}: malformed key '{}'", opts.config, lineno, key), ""};
    }
    auto value = toml_value(opts.config, lineno, trim(t.substr(eq + 1)));
    if (table.empty()) {
      if (key != "recipe") {
        throw Lhd_error{"config",
                        std::format("{}:{}: unknown top-level key '{}'", opts.config, lineno, key),
                        "top level takes only `recipe`; pass flags go under [upass]/[cprop]/[bitwidth]/[cgen]"};
      }
      file_recipe = value;
      continue;
    }
    // Canonicalize against the `compile` context: a config table is portable
    // across commands, so a bare compile-pass table ([cprop]/[upass]/[cgen]…)
    // resolves to its `compile.*` namespace just as `lhd compile --set cprop.x`
    // does, while a fully-qualified table ([compile.cprop], [pass.abc], [lec])
    // passes through unchanged. An unknown table is left for
    // check_known_set_passes to reject.
    file_sets.emplace_back(canonical_set_key(std::format("{}.{}", table, key), "compile"), value);
  }

  // File entries are defaults: prepend so later (CLI) --set entries overwrite
  // them in merge_sets; --recipe wins over the file's recipe. And unlike an
  // explicit --recipe, a default that the command has no slot for is simply
  // ignored (one lhd.toml can serve every step of a flow), so only the
  // recipe-consuming commands pick it up.
  opts.sets.insert(opts.sets.begin(), file_sets.begin(), file_sets.end());
  if (opts.recipe.empty() && opts.command == "compile") {
    opts.recipe = file_recipe;
  }
}

}  // namespace

Options parse_args(int argc, char** argv) {
  Options opts;

  // --version anywhere before `--`.
  for (int j = 1; j < argc; ++j) {
    std::string_view a{argv[j]};
    if (a == "--version") {
      opts.command = "version";
      return opts;
    }
    if (a == "--") {
      break;
    }
  }

  // One pass over argv: the first bare word is the command, so shared flags
  // may come before or after it (`lhd --diag-fmt jsonl list options` ==
  // `lhd list options --diag-fmt jsonl`). Value-taking flags consume their
  // value wherever they sit (`lhd --top foo synth ...` keeps foo with --top).
  bool raw_mode  = false;
  bool want_help = false;
  // The command-path established by the command words seen so far, dotted
  // (2h-set_path): "" before the command word, the command after it, and
  // "pass.<sub>" once a `pass` sub-command is read. Each --set key to its
  // right is canonicalized against this so abbreviated keys resolve.
  std::string cmd_path;
  // Count the logical tokens the user typed (one per loop turn; a value-flag's
  // value is consumed via ++i, so it is not counted separately). A command word
  // is one token, so `n_user_tokens == 1` means the command was typed bare.
  int n_user_tokens = 0;
  for (int i = 1; i < argc; ++i) {
    std::string_view a{argv[i]};
    ++n_user_tokens;

    if (raw_mode) {
      opts.raw_args.emplace_back(a);
      continue;
    }
    if (a == "--") {
      raw_mode = true;
      continue;
    }

    if (a == "--emit") {
      opts.emits.emplace_back(parse_emit_arg(a, need_value(a, i, argc, argv)));
    } else if (a == "--emit-dir") {
      opts.emit_dirs.emplace_back(parse_typed(a, need_value(a, i, argc, argv), true));
    } else if (a == "--in") {
      opts.ins.emplace_back(parse_typed(a, need_value(a, i, argc, argv), false));
    } else if (a == "--in-dir") {
      opts.in_dirs.emplace_back(parse_typed(a, need_value(a, i, argc, argv), false));
    } else if (a == "--lib") {
      opts.libs.emplace_back(parse_typed(a, need_value(a, i, argc, argv), false));
    } else if (a == "--collapse") {  // lec: proven def(s) to force-blackbox (repeatable / comma-sep)
      std::string v{need_value(a, i, argc, argv)};
      size_t      pos = 0;
      while (pos < v.size()) {
        size_t c   = v.find(',', pos);
        size_t end = c == std::string::npos ? v.size() : c;
        if (end > pos) {
          opts.collapse.emplace_back(v.substr(pos, end - pos));
        }
        pos = end + 1;
      }
    } else if (a == "--top") {
      opts.top = need_value(a, i, argc, argv);
    } else if (a == "--target") {  // `lhd tool`: node|pin|edge|all, or tree's kind:<X>
      auto v = std::string{need_value(a, i, argc, argv)};
      // `tool tree --target kind:register --target kind:memory` (repeatable):
      // a kind:<X> / kind=<X> value selects node kinds to surface in the tree;
      // anything else is the cat/grep/diff node|pin|edge|all target.
      if (v.starts_with("kind:") || v.starts_with("kind=")) {
        opts.tool_kinds.push_back(v.substr(5));
      } else {
        opts.tool_target = std::move(v);
      }
    } else if (a == "--attr") {  // explicit display column CSV
      opts.tool_attr = need_value(a, i, argc, argv);
    } else if (a == "-v" || a == "--invert-match") {  // `tool grep -v`: keep non-matching records
      opts.tool_invert = true;
    } else if (a == "--match") {  // `tool diff --match`: visualize via the semdiff `match` attribute
      opts.tool_match = true;
    } else if (a == "--max" || a == "--hops" || a == "-C" || a == "--context") {  // row cap / focus radius / diff context
      auto   v        = std::string{need_value(a, i, argc, argv)};
      size_t consumed = 0;
      long   n        = 0;
      try {
        n = std::stol(v, &consumed);
      } catch (const std::exception&) {
        consumed = 0;
      }
      if (v.empty() || consumed != v.size() || n < 0) {
        throw Lhd_error{"usage", std::format("{} expects a non-negative integer, got '{}'", a, v), ""};
      }
      if (a == "--max") {
        opts.tool_max = static_cast<int>(n);
      } else if (a == "--hops") {
        opts.tool_hops = static_cast<int>(n);
      } else {
        opts.tool_context = static_cast<int>(n);
      }
    } else if (a == "--hier") {  // optional integer depth; bare = all levels
      if (i + 1 < argc) {
        std::string_view nx{argv[i + 1]};
        if (!nx.empty() && std::all_of(nx.begin(), nx.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
          opts.tool_hier = static_cast<int>(std::stol(std::string{nx}));
          ++i;
        } else {
          opts.tool_hier = std::numeric_limits<int>::max();
        }
      } else {
        opts.tool_hier = std::numeric_limits<int>::max();
      }
    } else if (a == "--reader") {
      opts.reader = need_value(a, i, argc, argv);
      if (opts.reader != "yosys-verilog" && opts.reader != "yosys-slang" && opts.reader != "slang") {
        throw Lhd_error{"usage",
                        std::format("--reader must be yosys-verilog, yosys-slang, or slang, got '{}'", opts.reader),
                        "yosys-* elaborate to LGraphs via yosys; slang is the direct SV -> LNAST front-end"};
      }
    } else if (a == "--config") {
      opts.config = need_value(a, i, argc, argv);
    } else if (a == "--depfile") {
      opts.depfile = need_value(a, i, argc, argv);
    } else if (a == "--unused-inputs") {  // Bazel unused_inputs_list (compile)
      opts.unused_inputs = need_value(a, i, argc, argv);
    } else if (a == "--stats") {  // pass semdiff: the aggregate match report
      opts.stats = true;
    } else if (a == "--recipe") {
      opts.recipe = need_value(a, i, argc, argv);
    } else if (a == "--recipe-file") {
      opts.recipe_file = need_value(a, i, argc, argv);
    } else if (a == "--set") {
      auto kv  = std::string{need_value(a, i, argc, argv)};
      auto pos = kv.find('=');
      if (pos == std::string::npos || pos == 0) {
        throw Lhd_error{"usage", std::format("--set expects pass.flag=value, got '{}'", kv), ""};
      }
      // Canonicalize the key against the command path seen so far so an
      // abbreviated key (`adder` after `pass abc`) resolves to `pass.abc.adder`
      // (2h-set_path). Fully-qualified keys pass through unchanged.
      opts.sets.emplace_back(canonical_set_key(kv.substr(0, pos), cmd_path), kv.substr(pos + 1));
    } else if (a == "--dump") {
      // Repeatable, comma-separable: --dump parse --dump lg == --dump parse,lg
      std::string v{need_value(a, i, argc, argv)};
      size_t      start = 0;
      while (start <= v.size()) {
        auto        end  = v.find(',', start);
        std::string what = v.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (what == "lgraph") {  // mirror the lg: kind alias
          what = "lg";
        }
        if (what != "parse" && what != "lnast" && what != "lg") {
          throw Lhd_error{"usage",
                          std::format("--dump expects parse|lnast|lg, got '{}'", what),
                          "parse = post-frontend LNAST, lnast = post-upass LNAST, lg = textual LGraph dump (all to stderr)"};
        }
        opts.dumps.push_back(what);
        if (end == std::string::npos) {
          break;
        }
        start = end + 1;
      }
    } else if (a == "--impl") {
      auto tp        = parse_check_side(a, need_value(a, i, argc, argv));
      opts.impl_kind = tp.kind;
      opts.impl_path = tp.path;
    } else if (a == "--ref") {
      auto tp       = parse_check_side(a, need_value(a, i, argc, argv));
      opts.ref_kind = tp.kind;
      opts.ref_path = tp.path;
    } else if (a == "--formal") {  // formal verify / lec: fnmatch glob over formal-block dotted names
      opts.formal_filter = need_value(a, i, argc, argv);
    } else if (a == "--impl-top") {
      opts.impl_top = need_value(a, i, argc, argv);
    } else if (a == "--ref-top") {
      opts.ref_top = need_value(a, i, argc, argv);
    } else if (a == "--result-json") {
      opts.result_json = need_value(a, i, argc, argv);
    } else if (a == "--diag-fmt") {
      auto v = need_value(a, i, argc, argv);
      if (v == "jsonl") {
        opts.diag_fmt = Diag_fmt::jsonl;
      } else if (v == "pretty") {
        opts.diag_fmt = Diag_fmt::pretty;
      } else if (v != "auto") {  // auto = keep the isatty-resolved default
        throw Lhd_error{"usage",
                        std::format("--diag-fmt expects auto|jsonl|pretty, got '{}'", v),
                        "auto = pretty on a terminal, jsonl when piped/captured"};
      }
    } else if (a == "--workdir") {
      opts.workdir = need_value(a, i, argc, argv);
    } else if (a == "-j" || a == "--jobs") {
      auto   v        = std::string{need_value(a, i, argc, argv)};
      size_t consumed = 0;
      long   n        = 0;
      try {
        n = std::stol(v, &consumed);
      } catch (const std::exception&) {
        consumed = 0;
      }
      if (v.empty() || consumed != v.size() || n < 0) {
        throw Lhd_error{"usage", std::format("{} expects a non-negative integer, got '{}'", a, v), ""};
      }
      opts.jobs = static_cast<int>(n);
    } else if (a == "-q" || a == "--quiet") {
      opts.quiet = true;
    } else if (a == "--verbose") {
      opts.verbose = true;
    } else if (a == "--setup-only") {  // `sim`: generate the C++ sim, do not build/run
      opts.sim_setup_only = true;
    } else if (a == "--run-only") {  // `sim`: build/run an existing sim (needs --workdir)
      opts.sim_run_only = true;
    } else if (a == "--list-tests") {  // `sim`: print the tests + parameters as JSON, then exit (no build)
      opts.sim_list_tests = true;
    } else if (a == "--arg") {  // `sim`: bind a test parameter, repeatable: --arg key=value
      auto v  = std::string{need_value(a, i, argc, argv)};
      auto eq = v.find('=');
      if (eq == std::string::npos || eq == 0) {
        throw Lhd_error{"usage", std::format("--arg expects key=value, got '{}'", v),
                        "e.g. `lhd sim foo.prp foo.bar --arg max_cycles=30`"};
      }
      opts.sim_args.emplace_back(v.substr(0, eq), v.substr(eq + 1));
    } else if (a == "--restart-at" || a == "--restart-cycle") {  // `sim`: resume from the nearest checkpoint <= N
      opts.sim_restart_at = parse_nonneg(a, need_value(a, i, argc, argv));
    } else if (a == "--vcd-from") {  // `sim`: trace VCD starting at cycle N (restart to N first)
      opts.sim_vcd_from = parse_nonneg(a, need_value(a, i, argc, argv));
    } else if (a == "--vcd-to") {  // `sim`: trace VCD up to cycle N (with --vcd-from)
      opts.sim_vcd_to = parse_nonneg(a, need_value(a, i, argc, argv));
    } else if (a == "--vcd-on-fail") {  // `sim`: re-run a failed test with a VCD of the failure region
      opts.sim_vcd_on_fail = true;
    } else if (a == "--vcd-fail-window") {  // `sim`: cycles before the failure to trace (with --vcd-on-fail)
      opts.sim_vcd_fail_window = parse_nonneg(a, need_value(a, i, argc, argv));
    } else if (a == "--list-signals") {  // `sim`: enumerate observable signals, then exit
      opts.sim_list_signals = true;
    } else if (a == "--probe") {  // `sim`: per-cycle JSON trajectory of SIG,... (no re-instrumenting)
      opts.sim_probe = need_value(a, i, argc, argv);
    } else if (a == "--probe-from") {  // `sim`: probe trajectory start cycle
      opts.sim_probe_from = parse_nonneg(a, need_value(a, i, argc, argv));
    } else if (a == "--probe-to") {  // `sim`: probe trajectory end cycle
      opts.sim_probe_to = parse_nonneg(a, need_value(a, i, argc, argv));
    } else if (a == "--break-when") {  // `sim`: first cycle a `SIG OP VALUE|SIG` condition holds
      opts.sim_break_when = need_value(a, i, argc, argv);
    } else if (a == "--seed") {  // shared RNG seed (alias for `--set lhd.seed=N`); `sim` forwards it to drivers
      auto   v        = std::string{need_value(a, i, argc, argv)};
      size_t consumed = 0;
      try {
        (void)std::stoull(v, &consumed, 0);  // base 0: decimal or 0x.. ; validate the whole token
      } catch (const std::exception&) {
        consumed = 0;
      }
      // stoull silently wraps a leading `-` to a huge value — reject it so the
      // "non-negative" contract holds (and matches the driver's _to_u64).
      if (v.empty() || consumed != v.size() || v.front() == '-') {
        throw Lhd_error{"usage", std::format("--seed expects a non-negative integer, got '{}'", v), ""};
      }
      opts.seed          = v;
      opts.seed_explicit = true;
    } else if (a == "-i" || a == "--inplace") {  // `pyrope fmt`: rewrite the input file(s) in place
      opts.fmt_inplace = true;
    } else if (a == "-o" || a == "--output") {  // `pyrope fmt`: write to a file instead of stdout
      opts.fmt_output = need_value(a, i, argc, argv);
    } else if (a == "--indent" || a == "--width") {  // `pyrope fmt`: indent size / wrap column
      auto   v        = std::string{need_value(a, i, argc, argv)};
      size_t consumed = 0;
      long   n        = 0;
      try {
        n = std::stol(v, &consumed);
      } catch (const std::exception&) {
        consumed = 0;
      }
      if (v.empty() || consumed != v.size() || n <= 0) {
        throw Lhd_error{"usage", std::format("{} expects a positive integer, got '{}'", a, v), ""};
      }
      if (a == "--indent") {
        opts.fmt_indent = static_cast<int>(n);
      } else {
        opts.fmt_width = static_cast<int>(n);
      }
    } else if (a == "--verify") {  // `pyrope fmt`: re-parse the formatted output
      opts.fmt_verify = true;
    } else if (a == "-h" || a == "--help") {
      want_help = true;  // resolved after the loop, once the command word is known
    } else if (!a.empty() && a[0] == '-') {
      throw Lhd_error{"usage",
                      std::format("unknown option '{}'", a),
                      opts.command.empty() ? "run `lhd help`" : std::format("run `lhd help {}`", opts.command)};
    } else if (opts.command.empty()) {
      if (a == "check") {
        // `check` merged into `lec` (the lgcheck/yosys backend is now a solver).
        throw Lhd_error{"usage",
                        "the `check` command merged into `lec` — use `lhd lec ... --set lec.solver=lgyosys`",
                        "lec defaults to the in-process cvc5 solver; lgyosys is the yosys/lgcheck backend"};
      }
      if (a == "semdiff") {
        // `semdiff` is now a `pass` subcommand (it runs a single graph pass over
        // two lg: libraries), not a top-level command.
        throw Lhd_error{"usage",
                        "`semdiff` is now a `pass` subcommand — use `lhd pass semdiff --ref lg:… --impl lg:…`",
                        "e.g. `lhd pass semdiff --ref lg:gold --impl lg:opt --top adder`"};
      }
      if (a == "lsp") {
        // The LSP server is now a `pyrope` subcommand (next to `pyrope fmt`).
        throw Lhd_error{"usage",
                        "the LSP server is now `lhd pyrope lsp` (not `lhd lsp`)",
                        "run `lhd pyrope lsp` (or point your editor's launcher at it)"};
      }
      if (a == "compile" || a == "lec" || a == "scan" || a == "pyrope" || a == "list" || a == "describe"
          || a == "version" || a == "help" || a == "tool" || a == "tools" || a == "pass" || a == "sim"
          || a == "formal") {
        // tool keeps its positionals raw and ORDERED in opts.files: the verb
        // (cat/grep/diff/tree), the filter terms (name:/color:/from:…), and the
        // ln:/lg: inputs all keep their place — tool_command classifies them.
        // `tools` is silently accepted as an alias for `tool` (common typo).
        opts.command = (a == "tools") ? std::string{"tool"} : std::string{a};
        cmd_path     = opts.command;  // command-path root for --set abbreviation (2h-set_path)
      } else {
        throw Lhd_error{"usage", std::format("unknown command '{}'", a), "run `lhd help` for the command list"};
      }
    } else if (opts.command == "compile" && opts.language.empty() && opts.files.empty() && opts.ins.empty()
               && opts.in_dirs.empty() && (a == "verilog" || a == "pyrope")) {
      // The optional language word: the first positional after compile (flags
      // may intervene); inferred from the source-file extensions (.prp ->
      // pyrope, .v/.sv -> verilog) when omitted.
      opts.language = a;
    } else if (opts.command == "compile" || opts.command == "pass") {
      // `pass` positionals: the subcommand word(s) (color/partition/clear/<alg>)
      // land in opts.files; an lg:DIR is routed to opts.ins by route_positional.
      if (!route_positional(opts, a)) {
        // The first `pass` positional is the sub-command (abc/color/partition/
        // liberty): extend the command path to pass.<sub> so a --set to its
        // right may drop the `pass.<sub>.` prefix (2h-set_path).
        if (opts.command == "pass" && opts.files.empty()) {
          cmd_path = "pass." + std::string{a};
        }
        opts.files.emplace_back(a);
      }
    } else if (opts.command == "formal") {
      // `formal <sub>`: the first positional picks the engine face (2f-verify).
      // `formal lec` IS the lec command (behavior-preserving rename — rewrite the
      // command word so everything downstream, --impl/--ref included, is
      // untouched); `formal verify` keeps command=formal with the subcommand in
      // files[0] (the `pass` convention). Later positionals are the design /
      // formal-block sources (lg:DIR routed to opts.ins by route_positional).
      if (opts.files.empty() && a == "lec") {
        opts.command = "lec";
        cmd_path     = "lec";
      } else if (opts.files.empty() && a == "verify") {
        cmd_path = "formal.verify";
        opts.files.emplace_back(a);
      } else if (!route_positional(opts, a)) {
        opts.files.emplace_back(a);
      }
    } else {
      opts.files.emplace_back(a);
    }
  }

  // A command that needs an argument to do anything, typed bare (`lhd compile`
  // with nothing else), is treated as a request for that command's help — show
  // `lhd <cmd> --help` rather than erroring on the missing input. A wrong or
  // extra argument/option does NOT trigger this: an unknown option already threw
  // above, and a stray positional leaves n_user_tokens > 1, so the command runs
  // and reports its own real error.
  if (!want_help && n_user_tokens == 1
      && (opts.command == "compile" || opts.command == "lec" || opts.command == "scan" || opts.command == "tool"
          || opts.command == "pass" || opts.command == "sim" || opts.command == "formal")) {
    want_help = true;
  }

  if (want_help) {
    // `lhd -h [command]` / `lhd <command> -h`: the command word (possibly
    // empty -> the general page) becomes the help topic.
    opts.files.insert(opts.files.begin(), opts.command);
    opts.command = "help";
  }
  if (opts.command.empty()) {
    opts.command = "help";
    return opts;
  }

  if (!opts.recipe_file.empty()) {
    throw Lhd_error{"unsupported",
                    "--recipe-file is not implemented yet (built-in recipes ship first)",
                    "use --recipe O0|O1|O2 and --set pass.flag=value (or --config lhd.toml)"};
  }

  // Fold --config file defaults into sets/recipe BEFORE anything hashes or
  // consumes the resolved config (a config file and the equivalent explicit
  // flags must be indistinguishable downstream).
  load_config(opts);

  // Infer the source language from the file extensions when not given.
  if ((opts.command == "elaborate" || opts.command == "compile" || opts.command == "sim") && opts.language.empty()
      && !opts.files.empty()) {
    bool any_prp = false;
    bool any_v   = false;
    for (const auto& f : opts.files) {
      any_prp |= ends_with(f, ".prp");
      any_v |= ends_with(f, ".v") || ends_with(f, ".sv");
    }
    if (any_prp && any_v) {
      throw Lhd_error{"usage", "cannot mix pyrope and verilog sources in one invocation", "split into two elaborates"};
    }
    if (any_prp) {
      opts.language = "pyrope";
    } else if (any_v) {
      opts.language = "verilog";
    } else {
      throw Lhd_error{"usage",
                      std::format("cannot infer the source language of '{}'", opts.files.front()),
                      "use `lhd compile pyrope|verilog ...` or .prp/.v/.sv extensions"};
    }
  }

  // The verilog readers (`slang`, `yosys-slang`, `yosys-verilog`) can take their
  // sources via the raw `--` args (e.g. `-- -F filelist.f`) instead of a
  // positional .v file, in which case there is no extension to infer from, so
  // pin the language to verilog.
  if ((opts.command == "elaborate" || opts.command == "compile") && opts.language.empty() && !opts.raw_args.empty()
      && (opts.reader == "slang" || opts.reader == "yosys-slang" || opts.reader == "yosys-verilog")) {
    opts.language = "verilog";
  }

  // `--emit results:PATH` is the typed-slot spelling of --result-json.
  for (auto it = opts.emits.begin(); it != opts.emits.end();) {
    if (it->kind == "results") {
      if (opts.result_json.empty()) {
        opts.result_json = it->path;
      }
      it = opts.emits.erase(it);
    } else {
      ++it;
    }
  }

  return opts;
}

}  // namespace lhd
