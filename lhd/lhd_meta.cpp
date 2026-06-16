//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <cstdio>
#include <format>
#include <print>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "lhd.hpp"

namespace lhd {

namespace {

constexpr std::string_view kSteps =
    R"json(["compile verilog","compile pyrope","check","lec","scan","ln.cat","ln.diff","pass","lsp"])json";
constexpr std::string_view kRecipes = R"json(["O0","O1","O2"])json";
constexpr std::string_view kEmitKinds =
    R"json(["ln","lg","verilog","pyrope","lnast-dump","graphviz","metadata","results","diagnostics"])json";
constexpr std::string_view kErrorClasses =
    R"json(["usage","syntax","internal","equiv_fail","signal","timeout","missing_file","config","dependency","unsupported"])json";

void print_json_line(std::string_view s) {
  std::fwrite(s.data(), 1, s.size(), stdout);
  std::fputc('\n', stdout);
}

std::string json_escape(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          out += std::format("\\u{:04x}", static_cast<unsigned char>(c));
        } else {
          out += c;
        }
    }
  }
  return out;
}

// The one-line list view shows the first sentence of the registered help,
// capped so one option stays one line; `lhd describe pass.flag` has the
// full text.
std::string brief_help(std::string_view help) {
  auto        cut = help.find(". ");
  std::string out{cut == std::string_view::npos ? help : help.substr(0, cut)};
  constexpr size_t kMax = 108;
  if (out.size() > kMax) {
    out.resize(kMax);
    while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80) {
      out.pop_back();  // never cut a UTF-8 sequence mid-byte
    }
    out += "…";
  }
  return out;
}

// Word-wrap `text` to `width` columns, each line prefixed with `indent`.
void print_wrapped(std::string_view text, size_t width, std::string_view indent) {
  std::string line;
  size_t      pos = 0;
  while (pos < text.size()) {
    auto next = text.find(' ', pos);
    auto word = text.substr(pos, next == std::string_view::npos ? std::string_view::npos : next - pos);
    if (!line.empty() && line.size() + 1 + word.size() > width) {
      std::print("{}{}\n", indent, line);
      line.clear();
    }
    if (!line.empty()) {
      line += ' ';
    }
    line += word;
    if (next == std::string_view::npos) {
      break;
    }
    pos = next + 1;
  }
  if (!line.empty()) {
    std::print("{}{}\n", indent, line);
  }
}

// `lhd list options [REGEX]` — the --set/--config vocabulary, from the live
// EPRP label registry. Honors --diag-fmt: pretty (one `pass.flag=default  #
// help` line each) on a terminal, the usual JSON line when piped/captured.
int list_options(const Options& opts) {
  std::string filter = opts.files.size() > 1 ? opts.files[1] : "";
  std::regex  re;
  if (!filter.empty()) {
    try {
      re = std::regex{filter};
    } catch (const std::regex_error& e) {
      std::print(stderr, "lhd list options: bad regex '{}': {}\n", filter, e.what());
      return 1;
    }
  }

  const auto              all = list_set_options();
  std::vector<const Set_option*> sel;
  for (const auto& o : all) {
    if (filter.empty() || std::regex_search(o.name, re)) {
      sel.push_back(&o);
    }
  }

  if (opts.diag_fmt == Diag_fmt::jsonl) {
    std::string items = "[";
    for (const auto* o : sel) {
      if (items.size() > 1) {
        items += ',';
      }
      items += std::format(R"json({{"name":"{}","method":"{}","default":"{}","help":"{}"}})json",
                           json_escape(o->name),
                           json_escape(o->method),
                           json_escape(o->default_value),
                           json_escape(o->help));
    }
    items += "]";
    print_json_line(std::format(R"json({{"schema_version":1,"pattern":"options","items":{}}})json", items));
    return 0;
  }

  size_t w = 0;
  for (const auto* o : sel) {
    w = std::max(w, o->name.size() + 1 + o->default_value.size());
  }
  for (const auto* o : sel) {
    std::print("{:<{}}  # {}\n", std::format("{}={}", o->name, o->default_value), w, brief_help(o->help));
  }
  return 0;
}

// `lhd describe pass.flag` — one --set/--config option with its full help.
// Returns -1 when `name` is not in the option vocabulary (caller falls
// through to the unknown-name error).
int describe_option(const Options& opts, const std::string& name) {
  const auto all = list_set_options();
  for (const auto& o : all) {
    if (o.name != name) {
      continue;
    }
    if (opts.diag_fmt == Diag_fmt::jsonl) {
      print_json_line(
          std::format(R"json({{"schema_version":1,"name":"{}","kind":"option","method":"{}","default":"{}","help":"{}"}})json",
                      json_escape(o.name),
                      json_escape(o.method),
                      json_escape(o.default_value),
                      json_escape(o.help)));
    } else if (o.default_value.empty()) {
      std::print("{}   (no default; a --set/--config flag of {})\n\n", o.name, o.method);
      print_wrapped(o.help, 92, "  ");
    } else {
      std::print("{} = {}   (default; a --set/--config flag of {})\n\n", o.name, o.default_value, o.method);
      print_wrapped(o.help, 92, "  ");
    }
    return 0;
  }
  // A known pass with an unknown flag gets a targeted hint. The pass token is
  // everything up to the LAST dot (the flag), so dotted command-path
  // namespaces like `pass.abc` are reported whole (2h-set_path).
  auto prefix = name.substr(0, name.rfind('.'));
  for (const auto& o : all) {
    if (o.name.size() > prefix.size() && o.name.compare(0, prefix.size(), prefix) == 0 && o.name[prefix.size()] == '.') {
      std::print(stderr, "lhd describe: unknown option '{}' (`lhd list options {}\\..*` shows what {} accepts)\n", name, prefix,
                 prefix);
      return 1;
    }
  }
  return -1;
}

int list_command(const Options& opts) {
  std::string pattern = opts.files.empty() ? "" : opts.files.front();

  if (pattern.empty()) {
    print_json_line(
        R"json({"schema_version":1,"patterns":[{"name":"steps","scope":"global"},{"name":"recipes","scope":"global"},{"name":"emit-kinds","scope":"global"},{"name":"error-classes","scope":"global"},{"name":"options","scope":"global"}]})json");
    return 0;
  }
  if (pattern == "options") {
    return list_options(opts);
  }
  if (pattern == "steps") {
    print_json_line(std::format(R"json({{"schema_version":1,"pattern":"steps","items":{}}})json", kSteps));
    return 0;
  }
  if (pattern == "recipes") {
    print_json_line(std::format(R"json({{"schema_version":1,"pattern":"recipes","items":{}}})json", kRecipes));
    return 0;
  }
  if (pattern == "emit-kinds") {
    print_json_line(std::format(R"json({{"schema_version":1,"pattern":"emit-kinds","items":{}}})json", kEmitKinds));
    return 0;
  }
  if (pattern == "error-classes") {
    print_json_line(std::format(R"json({{"schema_version":1,"pattern":"error-classes","items":{}}})json", kErrorClasses));
    return 0;
  }
  std::print(stderr, "lhd list: unknown pattern '{}' (try: steps, recipes, emit-kinds, error-classes, options [REGEX])\n", pattern);
  return 1;
}

int describe_command(const Options& opts) {
  if (opts.files.empty()) {
    std::print(stderr, "lhd describe: requires a name (a command, recipe:NAME, or an emit kind)\n");
    return 1;
  }
  const std::string& name = opts.files.front();

  if (name == "pyrope") {
    print_json_line(
        R"json({"schema_version":1,"name":"pyrope","description":"Pyrope source; as --emit-dir a per-unit .prp re-emission via pass.prp_writer (needs ln:/pyrope inputs)","direction":"in/out"})json");
    return 0;
  }
  if (name == "scan") {
    print_json_line(
        R"json({"schema_version":1,"name":"scan","description":"Pyrope import/dependency discovery: parse each .prp and report its import strings (raw, as written; path resolution lands with the import resolver). For depfile writers and BUILD generators (gazelle-style)","args":{"required":[{"name":"files","type":"path[]","positional":true}],"optional":[{"name":"result-json","type":"path"}]},"inputs":["pyrope"],"outputs":["result.scan"],"examples":["lhd scan f1.prp f2.prp"]})json");
    return 0;
  }
  if (name == "check") {
    print_json_line(
        R"json({"schema_version":1,"name":"check","description":"Logic equivalence check (LEC) via inou/yosys/lgcheck; non-verilog sides are compiled to verilog first (bare .prp/.v/.sv paths infer their kind)","args":{"required":[{"name":"impl","type":"verilog:PATH|pyrope:PATH|ln:DIR|lg:DIR"},{"name":"ref","type":"verilog:PATH|pyrope:PATH|ln:DIR|lg:DIR"}],"optional":[{"name":"impl-top","type":"string"},{"name":"ref-top","type":"string"}]},"inputs":["verilog","pyrope","ln","lg"],"outputs":[],"examples":["lhd check --impl verilog:net.v --ref verilog:gold.v --impl-top foo --ref-top foo","lhd check --impl x.prp --ref verilog:gold.v"]})json");
    return 0;
  }
  if (name == "lec") {
    print_json_line(
        R"json({"schema_version":1,"name":"lec","description":"Relational equivalence check (LEC) via the in-process pass/lec engine (cvc5, no yosys): prove_equal(ref, impl). lg:/pyrope:/ln: sides are loaded/elaborated to LGraphs (verilog LEC still goes through `lhd check`). Engine knobs are --set lec.* (`lhd lec --help`)","args":{"required":[{"name":"impl","type":"lg:DIR|pyrope:PATH|ln:DIR"},{"name":"ref","type":"lg:DIR|pyrope:PATH|ln:DIR"}],"optional":[{"name":"impl-top","type":"string"},{"name":"ref-top","type":"string"},{"name":"top","type":"string"},{"name":"set","type":"lec.flag=value","repeatable":true}]},"inputs":["lg","pyrope","ln"],"outputs":[],"examples":["lhd lec --impl impl.prp --ref ref.prp","lhd lec --impl lg:impl/ --ref lg:ref/ --top foo --set lec.engine=ind"]})json");
    return 0;
  }
  if (name == "compile" || name == "compile verilog" || name == "compile pyrope") {
    print_json_line(
        R"json({"schema_version":1,"name":"compile","description":"The single source->IR->netlist action (front-end + elaborate + synth fused: one action, one exit code). Takes Pyrope/(System)Verilog sources (language word optional: inferred from .prp/.v/.sv) and/or ln:/lg: IR inputs; positional ln:DIR supplies pre-elaborated imports, lg:DIR pre-compiled libraries; ln:/lg:-only inputs aggregate, optimize, or link. Verilog readers: yosys-verilog/yosys-slang go through yosys into lg:, slang is the direct SV -> LNAST front-end (ln:/lg: emits, the pyrope flow)","args":{"required":[{"name":"files","type":"path[] and/or ln:DIR|lg:DIR","positional":true}],"optional":[{"name":"top","type":"string"},{"name":"reader","type":"enum","values":["yosys-verilog","yosys-slang","slang"],"default":"yosys-slang"},{"name":"recipe","type":"enum","values":["O0","O1","O2"],"default":"O1"},{"name":"set","type":"pass.flag=value","repeatable":true},{"name":"depfile","type":"path"},{"name":"emit","type":"verilog:PATH|pyrope:PATH (or a bare .v/.sv/.prp; kind inferred)"},{"name":"emit-dir","type":"lg:DIR/|ln:DIR/|verilog:DIR/|pyrope:DIR/|lnast-dump:DIR/"},{"name":"workdir","type":"path"},{"name":"result-json","type":"path"}]},"inputs":["pyrope","verilog","ln","lg"],"outputs":["lg","verilog","ln","pyrope","lnast-dump"],"examples":["lhd compile foo.v --top foo --recipe O2 --emit verilog:net.v","lhd compile x.prp --emit net.v --emit-dir lg:x_lgs/","lhd compile x.prp --emit-dir ln:x_lns/","lhd compile ln:x_lns/ --recipe O1 --emit verilog:net.v","lhd compile lg:top_lgs/ --emit-dir lg:top_opt_lgs/"]})json");
    return 0;
  }
  if (name == "recipe:O0" || name == "O0") {
    print_json_line(
        R"json({"schema_version":1,"name":"recipe:O0","steps":[],"description":"No graph optimization; frontend lowering only (ln: inputs still run pass.upass + tolg)"})json");
    return 0;
  }
  if (name == "recipe:O1" || name == "O1") {
    print_json_line(R"json({"schema_version":1,"name":"recipe:O1","steps":["pass.cprop"],"description":"Constant/copy propagation"})json");
    return 0;
  }
  if (name == "recipe:O2" || name == "O2") {
    print_json_line(
        R"json({"schema_version":1,"name":"recipe:O2","steps":["pass.cprop","pass.bitwidth"],"description":"cprop + bitwidth inference"})json");
    return 0;
  }
  if (name == "lg" || name == "design" || name == "lgraph") {
    print_json_line(
        R"json({"schema_version":1,"name":"lg","description":"The design's LGraphs: an hhds::GraphLibrary save directory (library.txt + one binary body per module graph). 'design'/'lgraph' are accepted aliases","direction":"in/out"})json");
    return 0;
  }
  if (name == "ln" || name == "lnast") {
    print_json_line(
        R"json({"schema_version":1,"name":"ln","description":"The design's LNAST units: an hhds::Forest save directory (forest.txt + binary tree bodies, attrs included) plus a manifest.json unit index. 'lnast' is an accepted alias","direction":"in/out"})json");
    return 0;
  }
  if (name == "verilog") {
    print_json_line(
        R"json({"schema_version":1,"name":"verilog","description":"Verilog source; as --emit a deterministic name-sorted concatenation of per-module cgen output","direction":"in/out"})json");
    return 0;
  }
  if (name == "lsp") {
    print_json_line(
        R"json({"schema_version":1,"name":"lsp","description":"Pyrope LSP server (task 1n): JSON-RPC over stdio, Content-Length framed. Drives prp2lnast + pass.upass + core/diag per buffer; .prp only, ephemeral, no lgdb. stdio belongs to the protocol, so no result JSON is written","args":{},"examples":["lhd lsp"]})json");
    return 0;
  }
  if (name == "pass") {
    print_json_line(
        R"json({"schema_version":1,"name":"pass","description":"Run a single graph pass over lg: inputs. Subcommands: color <alg> (acyclic|synth|path|mincut node coloring), partition (region->module Sub split), abc (combinational ABC tech-map), liberty gensim <file.lib> (Liberty -> sim models)","args":{"required":[{"name":"subcommand","type":"enum","values":["color","partition","abc","liberty"]},{"name":"inputs","type":"lg:DIR","positional":true,"repeatable":true}],"optional":[{"name":"top","type":"string"},{"name":"emit-dir","type":"lg:DIR/"}]},"inputs":["lg"],"outputs":["lg"],"examples":["lhd pass color acyclic --top m lg:dir","lhd pass abc --top m lg:dir --emit-dir lg:net","lhd pass liberty gensim sky130.lib --emit-dir lg:models"]})json");
    return 0;
  }
  if (name == "lnast-dump") {
    print_json_line(
        R"json({"schema_version":1,"name":"lnast-dump","description":"Round-trippable textual LNAST dump (the Lnast::dump text form), one <unit>.lnast per unit. A debug/test observable; the binary interchange form is ln:. The dumped tree is post-upass","direction":"out"})json");
    return 0;
  }
  if (name == "ln.cat") {
    print_json_line(
        R"json({"schema_version":1,"name":"ln.cat","description":"Print LNAST trees to stdout (the Lnast::dump text form, pipe-friendly; no result envelope unless --result-json). Sources (.prp, or .v/.sv via the direct inou.slang front-end) are elaborated through pass.upass first (the --dump lnast tree); ln:DIR forests print as stored. --top filters units","args":{"required":[{"name":"inputs","type":".prp|.v|.sv|ln:DIR","positional":true,"repeatable":true}],"optional":[{"name":"top","type":"string"}]},"inputs":["pyrope","verilog","ln"],"outputs":["stdout"],"examples":["lhd ln.cat x.prp","lhd ln.cat ln:x_lns/ --top x"]})json");
    return 0;
  }
  if (name == "ln.diff") {
    print_json_line(
        R"json({"schema_version":1,"name":"ln.diff","description":"Diff two LNAST trees on stdout: a line diff of the dump texts plus the hhds tree edit distance (Zhang-Shasha via hhds/tree_edit_distance.hpp; nodes match on lnast type + name, loc/fname attrs are ignored). Each side is one .prp/.v/.sv source (elaborated through pass.upass) or one ln:DIR forest; multi-unit sides pair sorted-by-name (use --top to select one unit)","args":{"required":[{"name":"a","type":".prp|.v|.sv|ln:DIR","positional":true},{"name":"b","type":".prp|.v|.sv|ln:DIR","positional":true}],"optional":[{"name":"top","type":"string"}]},"inputs":["pyrope","verilog","ln"],"outputs":["stdout"],"examples":["lhd ln.diff old.prp new.prp","lhd ln.diff ln:before/ x.prp --top x"]})json");
    return 0;
  }
  if (name == "dump") {
    print_json_line(
        R"json({"schema_version":1,"name":"dump","description":"--dump parse|lnast|lg (repeatable, comma-separable): print a debug observable to stderr. parse = the LNAST right after the front-end parse (inou.prp/inou.slang + lnastfmt; needs sources), lnast = the LNAST right after pass.upass, lg = a textual node/edge dump of the LGraphs (post-recipe). A dump forces the pipeline stage that produces it (e.g. `--dump lnast` runs pass.upass). The screen twin of --emit-dir lnast-dump:DIR/; stdout stays protocol-clean","examples":["lhd compile x.prp --dump parse,lnast","lhd compile x.prp --recipe O0 --dump lg"]})json");
    return 0;
  }
  if (name == "config") {
    print_json_line(
        R"json({"schema_version":1,"name":"config","description":"--config lhd.toml: pass-flag defaults as a declared input file. Strict TOML subset: # comments, [pass] tables (upass|cprop|bitwidth|cgen, see `lhd list options`), key = value with quoted strings / true|false / integers; top level takes only `recipe`. Explicit --set/--recipe always win","example":"recipe = \"O2\"\n[upass]\nconstprop = true\nverifier = false"})json");
    return 0;
  }

  // `lhd describe pass.flag` — a --set/--config option (after the named
  // dotted commands above: ln.cat/ln.diff are not options).
  if (name.find('.') != std::string::npos) {
    int rc = describe_option(opts, name);
    if (rc >= 0) {
      return rc;
    }
  }

  std::print(stderr, "lhd describe: unknown name '{}'\n", name);
  return 1;
}

// List the --set pass.flag options whose name starts with `prefix` (e.g.
// "lec.") as one `name=default  # brief` line each — the discoverability
// section under a command/subcommand --help. Reads the live EPRP registry, so
// it can never drift from what --set actually accepts.
void print_options(std::string_view prefix) {
  const auto                     all = list_set_options();
  std::vector<const Set_option*> sel;
  size_t                         w = 0;
  for (const auto& o : all) {
    if (o.name.starts_with(prefix)) {
      sel.push_back(&o);
      w = std::max(w, o.name.size() + 1 + o.default_value.size());
    }
  }
  for (const auto* o : sel) {
    std::print("  {:<{}}  # {}\n", std::format("{}={}", o->name, o->default_value), w, brief_help(o->help));
  }
}

// The "options (--set …)" block under a command's --help: the inline option
// listing for every pass that command can run.
void print_options_section(std::initializer_list<std::string_view> prefixes) {
  std::print("\noptions (--set pass.flag=value; `lhd describe pass.flag` for the full text):\n");
  for (auto p : prefixes) {
    print_options(p);
  }
}

void print_general_help() {
  std::print(
      "lhd — LiveHD stateless CLI kernel (the LiveHD docs)\n"
      "\n"
      "usage: lhd [flags] <command> [args]   (shared flags may come before or after the command)\n"
      "  the language word is optional (inferred from .prp/.v/.sv); ln:/lg: IR inputs are positional\n"
      "\n"
      "commands:\n"
      "  compile    sources and/or ln:/lg: IR -> ln:/lg:/verilog/pyrope (front-end + elaborate + synth)\n"
      "               lhd compile x.prp --emit verilog:net.v\n"
      "               lhd compile foo.v --top foo --recipe O2 --emit net.v\n"
      "               lhd compile x.prp --emit-dir ln:x_lns/      # pre-elaborate for importers\n"
      "               lhd compile ln:x_lns/ --emit verilog:net.v  # synth from IR\n"
      "               lhd compile lg:foo_lgs/ --emit-dir lg:foo_opt_lgs/\n"
      "  check      logic equivalence (LEC) via inou/yosys/lgcheck; pyrope:/ln:/lg: sides compile first\n"
      "               lhd check --impl verilog:net.v --ref verilog:gold.v --top foo\n"
      "               lhd check --impl x.prp --ref verilog:gold.v\n"
      "  lec        relational equivalence (LEC) via the in-process cvc5 engine (no yosys; lg:/pyrope:/ln:)\n"
      "               lhd lec --impl impl.prp --ref ref.prp\n"
      "               lhd lec --impl lg:impl/ --ref lg:ref/ --top foo --set lec.engine=ind\n"
      "  scan       report each .prp file's import strings (the result's \"scan\" member)\n"
      "               lhd scan x.prp y.prp\n"
      "  ln.cat     print LNAST to stdout: sources elaborate through upass, ln: dirs print as stored\n"
      "               lhd ln.cat x.prp\n"
      "               lhd ln.cat ln:x_lns/ --top x\n"
      "  ln.diff    diff two LNAST trees: line diff + hhds tree edit distance, on stdout\n"
      "               lhd ln.diff old.prp new.prp\n"
      "               lhd ln.diff ln:before/ x.prp\n"
      "  lsp        Pyrope LSP server over stdio (JSON-RPC; .prp only)\n"
      "  pass       run one graph pass over lg: inputs: color <alg> | partition | abc | liberty gensim\n"
      "               lhd pass abc --top m lg:dir --emit-dir lg:net\n"
      "  list       steps | recipes | emit-kinds | error-classes | options [REGEX]\n"
      "               lhd list options 'cgen\\..*'   # the --set/--config pass.flag vocabulary\n"
      "  describe   <command | recipe:NAME | emit-kind | pass.flag | dump | config>  (the JSON form)\n"
      "               lhd describe cgen.srcmap      # one option, full help text\n"
      "  version | help [command]\n"
      "\n"
      "per-command help:  lhd <command> --help   (== `lhd help <command>`; lists that command's\n"
      "  --set options too) — e.g. `lhd lec --help`, `lhd pass --help`, `lhd pass partition --help`\n"
      "\n"
      "typed I/O (KIND:PATH):  ln: = Forest dir (LNAST units)   lg: = GraphLibrary dir (LGraphs)\n"
      "  ln:/lg:/lnast-dump: are directory containers (--emit-dir only);\n"
      "  verilog: / pyrope: are --emit (one file; pyrope needs a one-unit design) or --emit-dir\n"
      "  (one file per module). --emit also infers the kind from a bare .v/.sv/.prp path\n"
      "\n"
      "debug dumps (printed to stderr; a dump forces the stage that produces it):\n"
      "  --dump parse|lnast|lg   post-parse LNAST | post-upass LNAST | textual LGraph\n"
      "               lhd compile x.prp --dump parse,lnast\n"
      "               lhd compile x.prp --recipe O0 --dump lg\n"
      "\n"
      "shared flags:\n"
      "  --top T   --reader yosys-verilog|yosys-slang|slang   --recipe O0|O1|O2\n"
      "  --set pass.flag=value   --config lhd.toml   (`lhd list options` for the vocabulary)\n"
      "  --workdir DIR   --result-json PATH\n"
      "  --diag-fmt auto|jsonl|pretty   result + diagnostic rendering (auto: pretty on a\n"
      "                                 terminal, jsonl when piped/captured)\n"
      "  -q (quiet stderr)   --verbose (mirror step logs)   (`lhd describe config` for lhd.toml)\n"
      "\n"
      "Deterministic (content-hash run_id) and hermetic (undeclared input => missing_file)\n"
      "by contract.\n");
}

// `lhd pass [SUB] --help` — the graph-pass subcommands, each with its own
// --set options. `sub` is the subcommand word ("color"/"partition"/...), empty
// for the `pass` overview.
int help_pass(const std::string& sub) {
  if (sub == "color") {
    std::print(
        "lhd pass color <alg> — node coloring over an lg: library (in place)\n"
        "\n"
        "usage: lhd pass color [acyclic|synth|path|mincut] --top M lg:DIR\n"
        "  alg defaults to acyclic. The coloring is written back into the input lg:.\n"
        "\n"
        "example:\n"
        "  lhd pass color acyclic --top m lg:dir\n");
    print_options_section({"color."});
    return 0;
  }
  if (sub == "partition") {
    std::print(
        "lhd pass partition — split a design into region -> module Subs (LEC-equivalent)\n"
        "\n"
        "usage: lhd pass partition --top M lg:DIR --emit-dir lg:OUT/\n"
        "  --emit-dir lg: (must differ from the input) receives the partitioned library.\n"
        "\n"
        "example:\n"
        "  lhd pass partition --top m lg:dir --emit-dir lg:parts\n");
    print_options_section({"partition."});
    return 0;
  }
  if (sub == "abc") {
    std::print(
        "lhd pass abc — combinational ABC tech-map (bit-blast -> AIG -> sky130 blackboxes)\n"
        "\n"
        "usage: lhd pass abc --top M lg:DIR --emit-dir lg:OUT/\n"
        "  --emit-dir lg: (must differ from the input) receives the mapped netlist.\n"
        "\n"
        "example:\n"
        "  lhd pass abc --top m lg:dir --emit-dir lg:net\n");
    print_options_section({"abc."});
    return 0;
  }
  if (sub == "liberty") {
    std::print(
        "lhd pass liberty gensim <file.lib> — Liberty cells -> LGraph simulation models\n"
        "\n"
        "usage: lhd pass liberty gensim <file.lib> --emit-dir lg:OUT/\n"
        "  Takes a Liberty FILE (not an lg: input); --emit-dir lg: receives the model library.\n"
        "\n"
        "example:\n"
        "  lhd pass liberty gensim sky130.lib --emit-dir lg:models\n");
    print_options_section({"liberty."});
    return 0;
  }
  if (!sub.empty()) {
    std::print(stderr, "lhd help: unknown pass subcommand '{}' (color | partition | abc | liberty)\n", sub);
    return 1;
  }
  std::print(
      "lhd pass — run one graph pass over an lg: library input\n"
      "\n"
      "usage: lhd pass <subcommand> [args] [--top M] lg:DIR [--emit-dir lg:OUT/]\n"
      "\n"
      "subcommands (run `lhd pass <subcommand> --help` for each one's --set options):\n"
      "  color <alg>          acyclic|synth|path|mincut node coloring (in place)\n"
      "  partition            region -> module Sub split (-> new lg:)\n"
      "  abc                  combinational ABC tech-map (-> new lg:)\n"
      "  liberty gensim FILE  Liberty -> simulation models (-> new lg:)\n"
      "\n"
      "examples:\n"
      "  lhd pass color acyclic --top m lg:dir\n"
      "  lhd pass partition --top m lg:dir --emit-dir lg:parts\n"
      "  lhd pass abc --top m lg:dir --emit-dir lg:net\n"
      "  lhd pass liberty gensim sky130.lib --emit-dir lg:models\n");
  return 0;
}

int help_command(const Options& opts) {
  const std::string topic = opts.files.empty() ? "" : opts.files.front();
  const std::string sub   = opts.files.size() > 1 ? opts.files[1] : "";

  if (topic.empty() || topic == "help") {
    print_general_help();
    return 0;
  }

  if (topic == "compile") {
    std::print(
        "lhd compile — the single source->IR->netlist action (front-end + elaborate + synth)\n"
        "\n"
        "usage: lhd compile [pyrope|verilog] <files…|ln:DIR|lg:DIR> [flags]\n"
        "  The language word is optional (inferred from .prp/.v/.sv). Sources lower through\n"
        "  the front-end + pass.upass; positional ln:DIR supplies pre-elaborated imports and\n"
        "  lg:DIR pre-compiled libraries. With no sources, ln:/lg: inputs aggregate, optimize,\n"
        "  or link (ln: + lg:). Verilog goes through a --reader (yosys-* -> lg:; slang -> the\n"
        "  direct SV->LNAST front-end). The graph recipe (default O1) and codegen then run.\n"
        "\n"
        "flags:\n"
        "  --top T              --reader R   yosys-verilog | yosys-slang | slang (default yosys-slang)\n"
        "  --recipe O0|O1|O2    (default O1; `lhd list recipes`)\n"
        "  --emit verilog:PATH | pyrope:PATH   (or a bare .v/.sv/.prp — kind inferred)\n"
        "  --emit-dir K:DIR/    lg: | ln: | verilog: | pyrope: | lnast-dump:\n"
        "  --set pass.flag=value   --config lhd.toml   --depfile PATH   --workdir DIR\n"
        "\n"
        "examples:\n"
        "  lhd compile foo.v --top foo --recipe O2 --emit verilog:net.v\n"
        "  lhd compile x.prp --emit net.v --emit-dir lg:x_lgs/\n"
        "  lhd compile x.prp --emit-dir ln:x_lns/        # pre-elaborate for importers\n"
        "  lhd compile ln:x_lns/ --emit verilog:net.v    # synth from IR\n"
        "  lhd compile lg:foo_lgs/ --emit-dir lg:foo_opt_lgs/\n");
    print_options_section({"upass.", "cprop.", "bitwidth.", "cgen."});
    return 0;
  }
  if (topic == "check") {
    std::print(
        "lhd check — logic equivalence (LEC) via inou/yosys/lgcheck\n"
        "\n"
        "usage: lhd check --impl KIND:PATH --ref KIND:PATH [flags]\n"
        "  Sides may be verilog:/pyrope:/ln:/lg: or a bare .v/.sv/.prp path (kind inferred);\n"
        "  non-verilog sides are compiled to Verilog first. For the in-process cvc5 engine\n"
        "  (no yosys, lg:/pyrope:/ln: only) use `lhd lec` instead.\n"
        "\n"
        "flags:\n"
        "  --impl KIND:PATH   --ref KIND:PATH\n"
        "  --top T            --impl-top T   --ref-top T\n"
        "\n"
        "examples:\n"
        "  lhd check --impl verilog:net.v --ref verilog:gold.v --top foo\n"
        "  lhd check --impl x.prp --ref verilog:gold.v\n");
    return 0;
  }
  if (topic == "lec") {
    std::print(
        "lhd lec — relational equivalence (LEC) via the in-process pass/lec engine (cvc5)\n"
        "\n"
        "usage: lhd lec --impl KIND:PATH --ref KIND:PATH [flags]\n"
        "  Sides are lg:DIR, pyrope:PATH (or a bare .prp), or ln:DIR — loaded/elaborated to\n"
        "  LGraphs and proven equal in process (no yosys). verilog: LEC goes through\n"
        "  `lhd check`. Engine knobs are the --set lec.* options below.\n"
        "\n"
        "flags:\n"
        "  --impl KIND:PATH   --ref KIND:PATH\n"
        "  --top T            --impl-top T   --ref-top T\n"
        "  --set lec.flag=value\n"
        "\n"
        "examples:\n"
        "  lhd lec --impl impl.prp --ref ref.prp\n"
        "  lhd lec --impl lg:impl/ --ref lg:ref/ --top foo --set lec.engine=ind\n");
    print_options_section({"lec."});
    return 0;
  }
  if (topic == "scan") {
    std::print(
        "lhd scan — Pyrope import/dependency discovery (for depfile/BUILD generators)\n"
        "\n"
        "usage: lhd scan <files.prp>… [--result-json PATH]\n"
        "  Parses each .prp and reports its import strings (raw, as written).\n"
        "\n"
        "example:\n"
        "  lhd scan f1.prp f2.prp\n");
    return 0;
  }
  if (topic == "ln.cat") {
    std::print(
        "lhd ln.cat — print LNAST trees to stdout (the Lnast::dump text form)\n"
        "\n"
        "usage: lhd ln.cat <.prp|.v|.sv|ln:DIR>… [--top M]\n"
        "  Sources are elaborated through pass.upass first; ln:DIR forests print as stored.\n"
        "  --top filters units.\n"
        "\n"
        "examples:\n"
        "  lhd ln.cat x.prp\n"
        "  lhd ln.cat ln:x_lns/ --top x\n");
    return 0;
  }
  if (topic == "ln.diff") {
    std::print(
        "lhd ln.diff — diff two LNAST trees on stdout (line diff + hhds tree edit distance)\n"
        "\n"
        "usage: lhd ln.diff <A> <B> [--top M]\n"
        "  Each side is one .prp/.v/.sv source (elaborated through pass.upass) or one ln:DIR\n"
        "  forest; multi-unit sides pair sorted-by-name (use --top to select one unit).\n"
        "\n"
        "examples:\n"
        "  lhd ln.diff old.prp new.prp\n"
        "  lhd ln.diff ln:before/ x.prp --top x\n");
    return 0;
  }
  if (topic == "lsp") {
    std::print(
        "lhd lsp — Pyrope LSP server over stdio (JSON-RPC, Content-Length framed; .prp only)\n"
        "\n"
        "usage: lhd lsp\n"
        "  Drives prp2lnast + pass.upass + core/diag per buffer; ephemeral, no lgdb. stdio\n"
        "  belongs to the protocol, so no result JSON is written.\n");
    return 0;
  }
  if (topic == "pass") {
    return help_pass(sub);
  }
  if (topic == "list") {
    std::print(
        "lhd list — enumerate the CLI vocabulary (one JSON line; --diag-fmt pretty for options)\n"
        "\n"
        "usage: lhd list <pattern>\n"
        "  steps | recipes | emit-kinds | error-classes | options [REGEX]\n"
        "  `options` lists every --set/--config pass.flag (filter with a REGEX over the names).\n"
        "\n"
        "examples:\n"
        "  lhd list options 'cgen\\..*'\n"
        "  lhd list recipes\n");
    return 0;
  }
  if (topic == "describe") {
    std::print(
        "lhd describe — one item's full record as JSON (pretty prose for pass.flag options)\n"
        "\n"
        "usage: lhd describe <command | recipe:NAME | emit-kind | pass.flag | dump | config>\n"
        "  For readable per-command help use `lhd help <command>` / `lhd <command> --help`.\n"
        "\n"
        "examples:\n"
        "  lhd describe cgen.srcmap\n"
        "  lhd describe lec\n");
    return 0;
  }
  if (topic == "version") {
    std::print("lhd version — print the tool version (also `lhd --version`)\n");
    return 0;
  }

  // Non-command topics (recipe:NAME, emit-kind, pass.flag, dump, config) stay
  // on the describe path (JSON, or pretty prose for an option name).
  return describe_command(opts);
}

}  // namespace

bool is_meta_command(const Options& opts) {
  return opts.command == "list" || opts.command == "describe" || opts.command == "version" || opts.command == "help";
}

int run_meta_command(const Options& opts) {
  if (opts.command == "version") {
    std::print("lhd {}\n", kVersion);
    return 0;
  }
  if (opts.command == "list") {
    return list_command(opts);
  }
  if (opts.command == "describe") {
    return describe_command(opts);
  }
  return help_command(opts);
}

}  // namespace lhd
