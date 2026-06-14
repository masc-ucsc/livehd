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
    R"json(["elaborate verilog","elaborate pyrope","synth","check","scan","compile verilog","compile pyrope","ln.cat","ln.diff","pass","lsp"])json";
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
  // A known pass with an unknown flag gets a targeted hint.
  auto prefix = name.substr(0, name.find('.'));
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

  if (name == "elaborate" || name == "elaborate verilog") {
    print_json_line(
        R"json({"schema_version":1,"name":"elaborate verilog","description":"Elaborate (System)Verilog (language word optional: inferred from .v/.sv). Readers: yosys-verilog/yosys-slang go through yosys into an lg: graph library; slang is the direct inou.slang SV -> LNAST front-end (ln:/lg: emits, the pyrope flow)","args":{"required":[{"name":"files","type":"path[]","positional":true}],"optional":[{"name":"top","type":"string","default":"-auto-top"},{"name":"reader","type":"enum","values":["yosys-verilog","yosys-slang","slang"],"default":"yosys-slang"},{"name":"depfile","type":"path"},{"name":"emit-dir","type":"lg:DIR/ (+ln:DIR/ with --reader slang)"},{"name":"workdir","type":"path"},{"name":"result-json","type":"path"}]},"inputs":["verilog"],"outputs":["lg","ln"],"examples":["lhd elaborate foo.v --top foo --emit-dir lg:foo_lgs/","lhd elaborate foo.sv --reader slang --emit-dir ln:foo_lns/"]})json");
    return 0;
  }
  if (name == "elaborate pyrope") {
    print_json_line(
        R"json({"schema_version":1,"name":"elaborate pyrope","description":"Elaborate Pyrope into ln: (Forest) and/or lg: (GraphLibrary) directories; positional ln:DIR inputs supply pre-elaborated imports; ln:/lg:-only inputs aggregate into one container","args":{"required":[{"name":"files","type":"path[] and/or ln:DIR|lg:DIR","positional":true}],"optional":[{"name":"top","type":"string"},{"name":"emit-dir","type":"ln:DIR/|lg:DIR/"},{"name":"workdir","type":"path"},{"name":"result-json","type":"path"}]},"inputs":["pyrope","ln","lg"],"outputs":["ln","lg"],"examples":["lhd elaborate x.prp --emit-dir ln:x_lns/ --emit-dir lg:x_lgs/","lhd elaborate y.prp ln:x_lns/ --emit-dir ln:y_lns/","lhd elaborate ln:x_lns/ ln:y_lns/ --top foo --emit-dir lg:top_lgs/"]})json");
    return 0;
  }
  if (name == "synth") {
    print_json_line(
        R"json({"schema_version":1,"name":"synth","description":"Transform/optimize/codegen over IR inputs (language-agnostic): ln: dirs are lowered (upass+tolg), lg: dirs are loaded","args":{"required":[{"name":"inputs","type":"ln:DIR|lg:DIR","positional":true,"repeatable":true}],"optional":[{"name":"top","type":"string"},{"name":"recipe","type":"enum","values":["O0","O1","O2"],"default":"O1"},{"name":"set","type":"pass.flag=value","repeatable":true},{"name":"emit","type":"verilog:PATH"},{"name":"emit-dir","type":"lg:DIR/|ln:DIR/|verilog:DIR/|pyrope:DIR/"}]},"inputs":["ln","lg"],"outputs":["lg","verilog","ln","pyrope"],"examples":["lhd synth lg:top_lgs/ --recipe O1 --emit-dir lg:top_opt_lgs/","lhd synth ln:x_lns/ --emit verilog:net.v"]})json");
    return 0;
  }
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
  if (name == "compile" || name == "compile verilog" || name == "compile pyrope") {
    print_json_line(
        R"json({"schema_version":1,"name":"compile","description":"Fused elaborate+synth (one action, one exit code)","args":{"note":"= elaborate <language> args + synth args"},"examples":["lhd compile foo.v --top foo --recipe O2 --emit verilog:net.v","lhd compile x.prp --emit verilog:net.v --emit-dir lg:x_lgs/"]})json");
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
        R"json({"schema_version":1,"name":"lnast-dump","description":"Round-trippable textual LNAST dump (the Lnast::dump text form), one <unit>.lnast per unit. A debug/test observable; the binary interchange form is ln:. From elaborate: post-parse; from synth/compile: post-upass","direction":"out"})json");
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
        R"json({"schema_version":1,"name":"dump","description":"--dump parse|lnast|lg (repeatable, comma-separable): print a debug observable to stderr. parse = the LNAST right after the front-end parse (inou.prp/inou.slang + lnastfmt), lnast = the LNAST right after pass.upass, lg = a textual node/edge dump of the LGraphs (post-tolg from elaborate, post-recipe from synth/compile). A dump forces the pipeline stage that produces it (e.g. `elaborate --dump lnast` runs pass.upass). The screen twin of --emit-dir lnast-dump:DIR/; stdout stays protocol-clean","examples":["lhd elaborate x.prp --dump parse,lnast","lhd compile x.prp --recipe O0 --dump lg"]})json");
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

int help_command(const Options& opts) {
  std::string topic = opts.files.empty() ? "" : opts.files.front();
  if (topic.empty() || topic == "help") {
    std::print(
        "lhd — LiveHD stateless CLI kernel (the LiveHD docs)\n"
        "\n"
        "usage: lhd [flags] <command> [args]   (shared flags may come before or after the command)\n"
        "  the language word is optional (inferred from .prp/.v/.sv); ln:/lg: IR inputs are positional\n"
        "\n"
        "commands:\n"
        "  elaborate  sources (+ positional ln: imports) -> ln:/lg: IR; ln:/lg:-only inputs aggregate\n"
        "               lhd elaborate x.prp --emit-dir ln:x_lns/\n"
        "               lhd elaborate foo.v --top foo --emit-dir lg:foo_lgs/\n"
        "  synth      transform / optimize / codegen over IR inputs (takes no sources)\n"
        "               lhd synth ln:x_lns/ --recipe O1 --emit verilog:net.v\n"
        "               lhd synth lg:foo_lgs/ --emit-dir lg:foo_opt_lgs/\n"
        "  compile    fused elaborate + synth\n"
        "               lhd compile x.prp --emit verilog:net.v\n"
        "               lhd compile foo.v --top foo --recipe O2 --emit verilog:net.v\n"
        "  check      logic equivalence (LEC) via inou/yosys/lgcheck; pyrope:/ln:/lg: sides compile first\n"
        "               lhd check --impl verilog:net.v --ref verilog:gold.v --top foo\n"
        "               lhd check --impl x.prp --ref verilog:gold.v\n"
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
        "  describe   <command | recipe:NAME | emit-kind | pass.flag | dump | config>\n"
        "               lhd describe cgen.srcmap      # one option, full help text\n"
        "  version | help [command]\n"
        "\n"
        "typed I/O (KIND:PATH):  ln: = Forest dir (LNAST units)   lg: = GraphLibrary dir (LGraphs)\n"
        "  ln:/lg:/pyrope:/lnast-dump: are directory containers (--emit-dir only);\n"
        "  verilog: is --emit (one netlist file) or --emit-dir (one .v per module)\n"
        "\n"
        "debug dumps (printed to stderr; a dump forces the stage that produces it):\n"
        "  --dump parse|lnast|lg   post-parse LNAST | post-upass LNAST | textual LGraph\n"
        "               lhd elaborate x.prp --dump parse,lnast\n"
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
    return 0;
  }
  Options d;
  d.files.push_back(topic == "elaborate" ? "elaborate verilog" : topic);
  return describe_command(d);
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
