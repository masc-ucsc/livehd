//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <cstdio>
#include <format>
#include <print>
#include <string>
#include <string_view>
#include <vector>

#include "lhd.hpp"

namespace lhd {

namespace {

constexpr std::string_view kSteps = R"json(["elaborate verilog","elaborate pyrope","synth","check","scan","compile verilog","compile pyrope"])json";
constexpr std::string_view kRecipes = R"json(["O0","O1","O2"])json";
constexpr std::string_view kEmitKinds =
    R"json(["ln","lg","verilog","pyrope","lnast-dump","graphviz","metadata","results","diagnostics"])json";
constexpr std::string_view kErrorClasses =
    R"json(["usage","syntax","internal","equiv_fail","signal","timeout","missing_file","config","dependency","unsupported"])json";

void print_json_line(std::string_view s) {
  std::fwrite(s.data(), 1, s.size(), stdout);
  std::fputc('\n', stdout);
}

int list_command(const Options& opts) {
  std::string pattern = opts.files.empty() ? "" : opts.files.front();

  if (pattern.empty()) {
    print_json_line(
        R"json({"schema_version":1,"patterns":[{"name":"steps","scope":"global"},{"name":"recipes","scope":"global"},{"name":"emit-kinds","scope":"global"},{"name":"error-classes","scope":"global"}]})json");
    return 0;
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
  std::print(stderr, "lhd list: unknown pattern '{}' (try: steps, recipes, emit-kinds, error-classes)\n", pattern);
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
        R"json({"schema_version":1,"name":"check","description":"Logic equivalence check (LEC) via inou/yosys/lgcheck","args":{"required":[{"name":"impl","type":"verilog:PATH|lg:DIR"},{"name":"ref","type":"verilog:PATH|lg:DIR"}],"optional":[{"name":"impl-top","type":"string"},{"name":"ref-top","type":"string"}]},"inputs":["verilog","lg"],"outputs":[],"examples":["lhd check --impl verilog:net.v --ref verilog:gold.v --impl-top foo --ref-top foo"]})json");
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
  if (name == "lnast-dump") {
    print_json_line(
        R"json({"schema_version":1,"name":"lnast-dump","description":"Round-trippable textual LNAST dump (the lgshell lnast.dump printer), one <unit>.lnast per unit. A debug/test observable; the binary interchange form is ln:. From elaborate: post-parse; from synth/compile: post-upass","direction":"out"})json");
    return 0;
  }
  if (name == "config") {
    print_json_line(
        R"json({"schema_version":1,"name":"config","description":"--config lhd.toml: pass-flag defaults as a declared input file. Strict TOML subset: # comments, [pass] tables (upass|cprop|bitwidth), key = value with quoted strings / true|false / integers; top level takes only `recipe`. Explicit --set/--recipe always win","example":"recipe = \"O2\"\n[upass]\nconstprop = true\nverifier = false"})json");
    return 0;
  }

  std::print(stderr, "lhd describe: unknown name '{}'\n", name);
  return 1;
}

int help_command(const Options& opts) {
  std::string topic = opts.files.empty() ? "" : opts.files.front();
  if (topic.empty() || topic == "help") {
    std::print(
        "lhd — LiveHD stateless CLI kernel (task 1y-bazel; docs/contracts/future_cli.md)\n"
        "\n"
        "usage: lhd <command> [args]\n"
        "\n"
        "commands (language word optional - inferred from .prp/.v/.sv):\n"
        "  elaborate SRCS... [ln:DIR...]      [--top T] --emit-dir ln:DIR/ --emit-dir lg:DIR/\n"
        "            (pyrope; positional ln: dirs supply pre-elaborated imports)\n"
        "  elaborate SRCS.v --top T [--reader yosys-verilog|yosys-slang|slang] [--depfile D]\n"
        "            --emit-dir lg:DIR/   (--reader slang: SV->LNAST, ln:/pyrope: emits)\n"
        "  elaborate ln:DIR... | lg:DIR       aggregate into one ln:/lg: container\n"
        "  synth     ln:DIR|lg:DIR [--recipe O0|O1|O2] [--set pass.flag=value]\n"
        "            --emit-dir lg:DIR/|ln:DIR/|pyrope:DIR/ | --emit verilog:PATH\n"
        "  check     --impl verilog:PATH|lg:DIR --ref verilog:PATH|lg:DIR [--impl-top T] [--ref-top T]\n"
        "  scan      FILES.prp...   report each file's import strings (result \"scan\" member)\n"
        "  lsp       Pyrope LSP server over stdio (JSON-RPC; .prp only)\n"
        "  compile   SRCS...   (fused elaborate + synth)\n"
        "  list      steps|recipes|emit-kinds|error-classes\n"
        "  describe  <command|recipe:NAME|emit-kind>\n"
        "  version | help [command]\n"
        "\n"
        "kinds: ln: = hhds::Forest save dir (LNAST units)  lg: = hhds::GraphLibrary save dir (LGraphs)\n"
        "       ln:/lg:/pyrope: are directory containers (--emit-dir only)\n"
        "shared: --emit KIND:PATH --emit-dir KIND:DIR/ --config lhd.toml --result-json PATH\n"
        "        --workdir DIR -q --verbose   (`lhd describe config` for the lhd.toml schema)\n"
        "\n"
        "The kernel is always deterministic (content-hash run_id, SOURCE_DATE_EPOCH)\n"
        "and always hermetic (undeclared input => missing_file). The lgshell REPL is\n"
        "unchanged and remains available for interactive use.\n");
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
