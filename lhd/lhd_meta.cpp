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
#include "log.hpp"  // livehd::log::channels() for `lhd list log-channels`

namespace lhd {

namespace {

constexpr std::string_view kSteps =
    R"json(["compile verilog","compile pyrope","lec","formal verify","formal lec","scan","tool","pass","pyrope fmt","pyrope lsp"])json";
constexpr std::string_view kRecipes = R"json(["O0","O1","O2"])json";
constexpr std::string_view kEmitKinds =
    R"json(["ln","lg","verilog","pyrope","lnast-dump","isabelle","lean","sim","graphviz","metadata","results","diagnostics"])json";
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

// Word-wrap `text` to `width` columns, each line prefixed with `indent`. An
// explicit '\n' in `text` is a hard break (kept as-is, then each segment is
// independently word-wrapped) so a multi-line help string — e.g. the
// pass.abc.flow command cheat-sheet — keeps its layout under `lhd describe`.
void print_wrapped(std::string_view text, size_t width, std::string_view indent) {
  size_t seg_start = 0;
  while (true) {
    auto nl  = text.find('\n', seg_start);
    auto seg = text.substr(seg_start, nl == std::string_view::npos ? std::string_view::npos : nl - seg_start);

    if (seg.empty()) {
      std::print("\n");  // a blank line in the source stays a blank line
    } else {
      std::string line;
      size_t      pos = 0;
      while (pos < seg.size()) {
        auto next = seg.find(' ', pos);
        auto word = seg.substr(pos, next == std::string_view::npos ? std::string_view::npos : next - pos);
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

    if (nl == std::string_view::npos) {
      break;
    }
    seg_start = nl + 1;
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

// `lhd list log-channels` — the developer-logging channel vocabulary
// (livehd::log). `--set <channel>.log=<level>` (off|error|warn|info|debug|trace)
// enables tracing for a channel and its dotted subtree; compiled out in `-c opt`.
int list_log_channels(const Options& opts) {
  const auto& chans = livehd::log::channels();
  if (opts.diag_fmt == Diag_fmt::jsonl) {
    std::string items = "[";
    for (auto c : chans) {
      if (items.size() > 1) {
        items += ',';
      }
      items += std::format("\"{}\"", json_escape(c));
    }
    items += "]";
    print_json_line(std::format(
        R"json({{"schema_version":1,"pattern":"log-channels","levels":["off","error","warn","info","debug","trace"],"items":{}}})json",
        items));
    return 0;
  }
  std::print("log channels ( --set <channel>.log=off|error|warn|info|debug|trace; off by default ):\n");
  for (auto c : chans) {
    std::print("  {}\n", c);
  }
  return 0;
}

int list_command(const Options& opts) {
  std::string pattern = opts.files.empty() ? "" : opts.files.front();

  if (pattern.empty()) {
    print_json_line(
        R"json({"schema_version":1,"patterns":[{"name":"steps","scope":"global"},{"name":"recipes","scope":"global"},{"name":"emit-kinds","scope":"global"},{"name":"error-classes","scope":"global"},{"name":"options","scope":"global"},{"name":"log-channels","scope":"global"}]})json");
    return 0;
  }
  if (pattern == "options") {
    return list_options(opts);
  }
  if (pattern == "log-channels") {
    return list_log_channels(opts);
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
  std::print(stderr,
             "lhd list: unknown pattern '{}' (try: steps, recipes, emit-kinds, error-classes, options [REGEX], log-channels)\n",
             pattern);
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
  if (name == "lec") {
    print_json_line(
        R"json({"schema_version":1,"name":"lec","description":"Logic equivalence check (LEC): prove_equal(ref, impl). Sides are verilog:/pyrope:/ln:/lg: (or a bare .v/.sv/.prp; kind inferred), loaded/elaborated to LGraphs (verilog via --reader, default slang). The --set lec.solver knob picks the backend: cvc5 (default, in-process SMT), bitwuzla (in-process SMT), or lgyosys (inou/yosys/lgcheck, the former `lhd check`). Other engine knobs are --set lec.* (`lhd lec --help`)","args":{"required":[{"name":"impl","type":"verilog:PATH|pyrope:PATH|ln:DIR|lg:DIR"},{"name":"ref","type":"verilog:PATH|pyrope:PATH|ln:DIR|lg:DIR"}],"optional":[{"name":"impl-top","type":"string"},{"name":"ref-top","type":"string"},{"name":"top","type":"string"},{"name":"reader","type":"enum","values":["slang","yosys-slang","yosys-verilog"],"default":"slang"},{"name":"set","type":"lec.flag=value","repeatable":true}]},"inputs":["verilog","pyrope","ln","lg"],"outputs":[],"examples":["lhd lec --impl impl.prp --ref ref.v","lhd lec --impl lg:impl/ --ref lg:ref/ --top foo --set lec.engine=ind","lhd lec --impl net.v --ref gold.v --set lec.solver=lgyosys --top foo"]})json");
    return 0;
  }
  if (name == "formal" || name == "formal verify" || name == "formal lec") {
    print_json_line(
        R"json({"schema_version":1,"name":"formal","description":"Formal command family (2f-verify). `formal verify` proves ONE design's assert/assert_always/assume obligations by BMC from reset on the pass/lec engine: per-obligation solve with frontier assumes, a per-assert/per-cycle verdict table (PROVEN-to-cycle-k is BOUNDED), per-obligation timeout isolation; only a reachable violation fails the run. `formal lec` is the equivalence check (alias of `lhd lec`). Knobs: --set formal.* (bound/timeout/phase/reset/...); legacy lec.* spellings stay accepted","args":{"required":[{"name":"subcommand","type":"verify|lec","positional":true},{"name":"design","type":"path or verilog:PATH|pyrope:PATH|lg:DIR (verify; lec takes --impl/--ref)","positional":true}],"optional":[{"name":"top","type":"string"},{"name":"set","type":"formal.flag=value","repeatable":true}]},"inputs":["verilog","pyrope","lg"],"outputs":[],"examples":["lhd formal verify foo.prp --top foo --set formal.bound=12","lhd formal verify design.v --set formal.timeout=60 --set formal.strict=true","lhd formal lec --impl impl.prp --ref ref.v"]})json");
    return 0;
  }
  if (name == "semdiff" || name == "pass semdiff") {
    print_json_line(
        R"json({"schema_version":1,"name":"pass semdiff","description":"Structural diff/match (a structural LEC), a `pass` subcommand: structural_match(ref, impl) marks corresponding nodes/driver-pins of both lg: libraries with a shared `match` attribute (0 = no counterpart) and saves both back in place. v1 marks lg: libraries, so both sides must be lg:DIR (compile sources to lg: first). Inspect the diff with `lhd tool grep match=0 lg:impl` or visualize it with `lhd tool diff lg:ref lg:impl --match`. Knobs are --set pass.semdiff.* (matching_names | id_granularity=pair|region)","args":{"required":[{"name":"impl","type":"lg:DIR"},{"name":"ref","type":"lg:DIR"}],"optional":[{"name":"impl-top","type":"string"},{"name":"ref-top","type":"string"},{"name":"top","type":"string"},{"name":"set","type":"pass.semdiff.flag=value","repeatable":true}]},"inputs":["lg"],"outputs":["lg"],"examples":["lhd pass semdiff --ref lg:gold --impl lg:opt --top adder","lhd pass semdiff --ref lg:gold --impl lg:opt --set pass.semdiff.matching_names=true","lhd tool diff lg:gold lg:opt --match"]})json");
    return 0;
  }
  if (name == "compile" || name == "compile verilog" || name == "compile pyrope") {
    print_json_line(
        R"json({"schema_version":1,"name":"compile","description":"The single source->IR->netlist action (front-end + elaborate + synth fused: one action, one exit code). Takes Pyrope/(System)Verilog sources (language word optional: inferred from .prp/.v/.sv) and/or ln:/lg: IR inputs; positional ln:DIR supplies pre-elaborated imports, lg:DIR pre-compiled libraries; ln:/lg:-only inputs aggregate, optimize, or link. Verilog readers: yosys-verilog/yosys-slang go through yosys into lg:, slang is the direct SV -> LNAST front-end (ln:/lg: emits, the pyrope flow)","args":{"required":[{"name":"files","type":"path[] and/or ln:DIR|lg:DIR","positional":true}],"optional":[{"name":"top","type":"string"},{"name":"reader","type":"enum","values":["slang","yosys-slang","yosys-verilog"],"default":"slang"},{"name":"recipe","type":"enum","values":["O0","O1","O2"],"default":"O1"},{"name":"set","type":"pass.flag=value","repeatable":true},{"name":"depfile","type":"path"},{"name":"unused-inputs","type":"path (declared source files absent from the compiled closure, e.g. dropped by --top; one cwd-relative path per line — Bazel unused_inputs_list)"},{"name":"emit","type":"verilog:PATH|pyrope:PATH (or a bare .v/.sv/.prp; kind inferred)"},{"name":"emit-dir","type":"lg:DIR/|ln:DIR/|verilog:DIR/|pyrope:DIR/|lnast-dump:DIR/|isabelle:DIR/|lean:DIR/|sim:DIR/"},{"name":"workdir","type":"path"},{"name":"result-json","type":"path"}]},"inputs":["pyrope","verilog","ln","lg"],"outputs":["lg","verilog","ln","pyrope","lnast-dump","isabelle","lean","sim"],"examples":["lhd compile foo.v --top foo --recipe O2 --emit verilog:net.v","lhd compile x.prp --emit net.v --emit-dir lg:x_lgs/","lhd compile x.prp --emit-dir ln:x_lns/","lhd compile ln:x_lns/ --recipe O1 --emit verilog:net.v","lhd compile lg:top_lgs/ --emit-dir lg:top_opt_lgs/","lhd compile lg:top_lgs/ --emit-dir isabelle:top_thy/ --emit-dir lean:top_lean/","lhd compile x.prp --emit-dir sim:x_sim/"]})json");
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
  if (name == "sim") {
    print_json_line(
        R"json({"schema_version":1,"name":"sim","description":"Executable C++ simulation (inou.cgen.sim): a standalone Bazel module of per-module Slop<N> structs (functional Out cycle(In)) over the ../hlop library. Each module is a small <name>.hpp interface plus a <name>.cpp body, so a body edit recompiles only that .o. --emit-dir only; `cd DIR && bazel build //:sim`","direction":"out"})json");
    return 0;
  }
  if (name == "pyrope fmt") {
    print_json_line(
        R"json({"schema_version":1,"name":"pyrope fmt","description":"Format Pyrope source (a clang-format for Pyrope): the prpfmt formatter walks the tree-sitter-pyrope grammar and re-emits standardized Pyrope (indentation, spacing, alignment, smart wrapping). Prints to stdout by default; -i/--inplace rewrites each file; -o/--output writes one file. No result envelope (the output is the formatted source). Exit 0 ok; 1 if any file failed to parse, failed --verify, or could not be read/written","args":{"required":[{"name":"files","type":"path[]","positional":true}],"optional":[{"name":"inplace","type":"flag","aliases":["-i"]},{"name":"output","type":"path","aliases":["-o"]},{"name":"indent","type":"int","default":4},{"name":"width","type":"int","default":80},{"name":"verify","type":"flag"}]},"inputs":["pyrope"],"outputs":["stdout","pyrope"],"examples":["lhd pyrope fmt foo.prp","lhd pyrope fmt -i foo.prp bar.prp","lhd pyrope fmt foo.prp --indent 2 -o foo.fmt.prp"]})json");
    return 0;
  }
  if (name == "pyrope lsp" || name == "lsp") {
    print_json_line(
        R"json({"schema_version":1,"name":"pyrope lsp","description":"Pyrope LSP server (task 1n): JSON-RPC over stdio, Content-Length framed. Drives prp2lnast + pass.upass + core/diag per buffer; .prp only, ephemeral, no lgdb. stdio belongs to the protocol, so no result JSON is written","args":{},"examples":["lhd pyrope lsp"]})json");
    return 0;
  }
  if (name == "pass") {
    print_json_line(
        R"json({"schema_version":1,"name":"pass","description":"Run a single graph pass over lg: inputs. Subcommands: color <alg> (acyclic|synth|path|mincut node coloring), partition (region->module Sub split), abc (combinational ABC tech-map), liberty gensim <file.lib> (Liberty -> sim models), semdiff (structural diff/match of two lg: libraries via --ref/--impl; `lhd describe \"pass semdiff\"`)","args":{"required":[{"name":"subcommand","type":"enum","values":["color","partition","abc","liberty","semdiff"]},{"name":"inputs","type":"lg:DIR","positional":true,"repeatable":true}],"optional":[{"name":"top","type":"string"},{"name":"emit-dir","type":"lg:DIR/"},{"name":"ref","type":"lg:DIR (semdiff)"},{"name":"impl","type":"lg:DIR (semdiff)"}]},"inputs":["lg"],"outputs":["lg"],"examples":["lhd pass color acyclic --top m lg:dir","lhd pass abc --top m lg:dir --emit-dir lg:net","lhd pass liberty gensim sky130.lib --emit-dir lg:models","lhd pass semdiff --ref lg:gold --impl lg:opt --top adder"]})json");
    return 0;
  }
  if (name == "lnast-dump") {
    print_json_line(
        R"json({"schema_version":1,"name":"lnast-dump","description":"Round-trippable textual LNAST dump (the Lnast::dump text form), one <unit>.lnast per unit. A debug/test observable; the binary interchange form is ln:. The dumped tree is post-upass","direction":"out"})json");
    return 0;
  }
  if (name == "tool") {
    print_json_line(
        R"json({"schema_version":1,"name":"tool","description":"Unified ln/lg inspector: `lhd tool <verb> [options] <inputs>` where a verb is cat|grep|diff|tree and inputs are ln:DIR / lg:DIR / verilog:FILE / pyrope:FILE (a bare .prp/.v/.sv source is the verilog:/pyrope: shortcut; the ln path takes sources). cat = structured dump; grep = filtered search (>=1 filter, may span libraries; -v inverts the match); diff = unified-diff of two inputs (--match: visualize the semdiff `match` attribute — matched regions summarized, unmatched nodes shown -/+ ); tree = for lg: the instance hierarchy (add --target kind:register / --target kind:memory, repeatable, to also list those nodes inside each module — registers/memories on the same hierarchy; any Ntype name also matches); for ln:/source the LNAST structural skeleton — the scope / control / call / def nodes (stmts, if, for, while, fcall, fdef, io) with per-subtree node counts, drawn with box connectors (cat dumps every node, tree is the summary; --target kind:<lnast-verbal> additively spotlights a collapsed kind like store/declare). Both honor --hier (depth) and --max (rows). lg options: --target node|pin|edge|all (default all) or tree's kind:<X>, --attr CSV, --max N, --hier [N], --top M; filters are field=value terms (name=/kind=/id=/color=/match=/bits=/from=/to=; ':' also accepted but Pyrope reads it as a type; strings substring|==exact|~regex; numeric >,<,>=,<=,a..b; =nil; a bare term like 'get_mask' has no field and matches any column plus the node/pin identity). Prints to stdout (--diag-fmt jsonl for machine form). Replaces the former ln.cat/ln.diff","args":{"required":[{"name":"verb","type":"cat|grep|diff|tree","positional":true},{"name":"inputs","type":"ln:DIR|lg:DIR|verilog:FILE|pyrope:FILE|.prp|.v|.sv|field=value|term","positional":true,"repeatable":true}],"optional":[{"name":"top","type":"string"},{"name":"target","type":"node|pin|edge|all|kind:<X>"},{"name":"attr","type":"csv"},{"name":"max","type":"int"},{"name":"hier","type":"int?"},{"name":"v","type":"flag (grep: invert match)"},{"name":"match","type":"flag (diff: visualize the semdiff match attribute)"}]},"inputs":["ln","lg","pyrope","verilog"],"outputs":["stdout"],"examples":["lhd tool cat lg:dir --top m","lhd tool grep get_mask lg:dir","lhd tool grep color=nil lg:dir","lhd tool grep -v match=0 lg:dir","lhd tool diff lg:before lg:after --attr color","lhd tool diff lg:gold lg:opt --match","lhd tool tree lg:dir --top m","lhd tool tree ln:dir","lhd tool cat x.prp"]})json");
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
  // commands above are not --set/--config options). Canonicalize against the
  // `compile` context first, so an abbreviated key (`cgen.srcmap`,
  // `upass.verifier`) resolves to its `compile.*` namespace just like
  // `lhd compile --set` does (2h-set_path).
  if (name.find('.') != std::string::npos) {
    int rc = describe_option(opts, canonical_set_key(name, "compile"));
    if (rc >= 0) {
      return rc;
    }
  }

  std::print(stderr, "lhd describe: unknown name '{}'\n", name);
  return 1;
}

// The "options (--set …)" block under a command/subcommand --help: lists the
// --set/--config flags that command accepts, read live from the EPRP registry so
// it never drifts from what --set actually takes. The header names the actual
// flag namespace (e.g. `pass.abc`) and points at `lhd list options <ns>` /
// `lhd describe <ns>.flag`. At most kShown flags are listed inline; the rest are
// summarized as a "… (+N more)" pointer so --help stays short. Every command
// that has this block registers at least one flag, so a prefix that matches
// NOTHING is a stale/typo'd prefix, not a real empty set — that is reported as an
// error (returns non-zero) instead of silently rendering an empty list.
int print_options_section(std::initializer_list<std::string_view> prefixes) {
  constexpr size_t kShown = 10;
  const auto       all    = list_set_options();

  std::vector<const Set_option*> sel;
  for (const auto& o : all) {
    for (auto p : prefixes) {
      if (o.name.starts_with(p)) {
        sel.push_back(&o);
        break;
      }
    }
  }

  // Namespace label(s) without the trailing '.'. `lhd list options` takes a
  // regex, so several namespaces are OR-joined into one pattern.
  std::string pattern;
  for (auto p : prefixes) {
    std::string_view ns = p;
    if (ns.ends_with(".")) {
      ns.remove_suffix(1);
    }
    if (!pattern.empty()) {
      pattern += '|';
    }
    pattern += ns;
  }

  if (sel.empty()) {
    std::print(stderr, "lhd help: no --set options registered under '{}' (a stale or mistyped flag prefix)\n", pattern);
    return 1;
  }

  // `lhd list options <arg>`: a bare namespace for one prefix, a quoted regex
  // for several (so the shell keeps the '|' as one argument).
  std::string list_arg = prefixes.size() == 1 ? pattern : std::format("'{}'", pattern);
  if (prefixes.size() == 1) {
    std::print(
        "\noptions (--set {0}.flag=value; `lhd describe {0}.flag` for each listed flag option in `lhd list options {0}`):\n",
        pattern);
  } else {
    std::print("\noptions (--set <flag>=value; `lhd describe <flag>` for each listed flag option in `lhd list options {}`):\n",
               list_arg);
  }

  size_t shown = std::min(sel.size(), kShown);
  size_t w     = 0;
  for (size_t i = 0; i < shown; ++i) {
    w = std::max(w, sel[i]->name.size() + 1 + sel[i]->default_value.size());
  }
  for (size_t i = 0; i < shown; ++i) {
    std::print("  {:<{}}  # {}\n", std::format("{}={}", sel[i]->name, sel[i]->default_value), w, brief_help(sel[i]->help));
  }
  if (sel.size() > kShown) {
    std::print("  … (+{} more; `lhd list options {}`)\n", sel.size() - kShown, list_arg);
  }
  return 0;
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
      "  sim        build + run a C++ simulation of a Pyrope design's `test` blocks (dynamic verify)\n"
      "               lhd sim foo.prp                  # build + run every test block\n"
      "               lhd sim foo.prp my_test --arg n=4\n"
      "  lec        logic equivalence (LEC): verilog:/pyrope:/ln:/lg: sides, --set lec.solver picks the\n"
      "               backend — cvc5 (default, in-process) | bitwuzla | lgyosys (yosys/lgcheck)\n"
      "               lhd lec --impl impl.prp --ref ref.v\n"
      "               lhd lec --impl net.v --ref gold.v --set lec.solver=lgyosys --top foo\n"
      "  formal     formal verification family: verify (assert/assume BMC from reset) | lec (= lhd lec)\n"
      "               lhd formal verify foo.prp --top foo --set formal.bound=12\n"
      "  scan       report each .prp file's import strings (the result's \"scan\" member)\n"
      "               lhd scan x.prp y.prp\n"
      "  tool       inspect ln:/lg: artifacts: cat | grep | diff | tree (stdout; --diag-fmt jsonl)\n"
      "               lhd tool cat lg:dir --top m       # structured dump + attributes\n"
      "               lhd tool grep get_mask lg:dir     # filtered search (bare term -> any field)\n"
      "               lhd tool diff lg:before lg:after --attr color\n"
      "               lhd tool cat x.prp                # LNAST cat (was ln.cat)\n"
      "  pyrope     Pyrope developer tools: fmt (clang-format-like formatter) | lsp (the LSP server)\n"
      "               lhd pyrope fmt -i foo.prp         # reformat in place\n"
      "               lhd pyrope fmt foo.prp            # print formatted source to stdout\n"
      "               lhd pyrope lsp                    # Pyrope LSP server over stdio (JSON-RPC; .prp only)\n"
      "  pass       run one graph pass over lg: inputs: color <alg> | partition | abc | liberty gensim | semdiff\n"
      "               lhd pass abc --top m lg:dir --emit-dir lg:net\n"
      "               lhd pass semdiff --ref lg:gold --impl lg:opt --top adder   # structural diff/match\n"
      "  list       steps | recipes | emit-kinds | error-classes | options [REGEX]\n"
      "               lhd list options 'compile\\..*'   # the --set/--config pass.flag vocabulary\n"
      "  describe   <command | recipe:NAME | emit-kind | pass.flag | dump | config>  (the JSON form)\n"
      "               lhd describe compile.cgen.srcmap   # one option, full help text\n"
      "  version | help [command]\n"
      "\n"
      "per-command help:  lhd <command> --help   (== `lhd help <command>`; lists that command's\n"
      "  --set options too) — e.g. `lhd lec --help`, `lhd pass --help`, `lhd pass partition --help`\n"
      "  (`--diag-fmt jsonl` renders any help page as a machine record; pretty is the tty default)\n"
      "\n"
      "typed I/O (KIND:PATH):  ln: = Forest dir (LNAST units)   lg: = GraphLibrary dir (LGraphs)\n"
      "  ln:/lg:/lnast-dump:/isabelle:/lean:/sim: are directory containers (--emit-dir only;\n"
      "    sim: is an executable C++ simulation, inou.cgen.sim);\n"
      "  verilog: / pyrope: are --emit (one file; pyrope needs a one-unit design) or --emit-dir\n"
      "  (one file per module). --emit also infers the kind from a bare .v/.sv/.prp path\n"
      "\n"
      "shared flags:\n"
      "  --top T   --reader slang|yosys-slang|yosys-verilog   --recipe O0|O1|O2\n"
      "  --set pass.flag=value   --config lhd.toml   (`lhd list options` for the vocabulary)\n"
      "  --workdir DIR   --result-json PATH\n"
      "  --diag-fmt auto|jsonl|pretty   result + diagnostic rendering (auto: pretty on a\n"
      "                                 terminal, jsonl when piped/captured)\n"
      "  -q (quiet stderr)   --verbose (mirror step logs)   (`lhd describe config` for lhd.toml)\n"
      "\n"
      "Deterministic (content-hash run_id) and hermetic (undeclared input => missing_file)\n"
      "by contract.\n");
}

// `lhd pyrope [SUB] --help` — the Pyrope developer tools. `sub` is the
// subcommand word ("fmt"/"lsp"), empty for the `pyrope` overview.
int help_pyrope(const std::string& sub) {
  if (sub == "fmt") {
    std::print(
        "lhd pyrope fmt — format Pyrope source (a clang-format for Pyrope, via prpfmt)\n"
        "\n"
        "usage: lhd pyrope fmt FILE… [flags]\n"
        "  Re-emits standardized Pyrope (indentation, spacing, alignment, smart wrapping)\n"
        "  by walking the tree-sitter-pyrope grammar. Prints to stdout by default.\n"
        "\n"
        "flags:\n"
        "  -i, --inplace     rewrite each input file in place (unchanged files are left alone)\n"
        "  -o, --output FILE write to FILE instead of stdout (a single input file)\n"
        "      --indent N    spaces per indent level (default 4)\n"
        "      --width N     wrap column / max line width (default 80)\n"
        "      --verify      re-parse the formatted output and warn (exit 1) if it no longer parses\n"
        "\n"
        "exit: 0 ok; 1 if any file failed to parse, failed --verify, or could not be read/written\n"
        "\n"
        "examples:\n"
        "  lhd pyrope fmt foo.prp                 # print formatted foo.prp to stdout\n"
        "  lhd pyrope fmt -i foo.prp bar.prp      # reformat both files in place\n"
        "  lhd pyrope fmt foo.prp --indent 2 -o foo.fmt.prp\n");
    return 0;
  }
  if (sub == "lsp") {
    std::print(
        "lhd pyrope lsp — the Pyrope LSP server\n"
        "\n"
        "usage: lhd pyrope lsp\n"
        "  JSON-RPC over stdio (Content-Length framed; .prp only). Drives prp2lnast +\n"
        "  pass.upass + core/diag per buffer; ephemeral, no lgdb.\n");
    return 0;
  }
  if (!sub.empty()) {
    std::print(stderr, "lhd help: unknown pyrope subcommand '{}' (fmt | lsp)\n", sub);
    return 1;
  }
  std::print(
      "lhd pyrope — Pyrope developer tools (language-adjacent, not the compile/synth flow)\n"
      "\n"
      "usage: lhd pyrope <subcommand> [args]\n"
      "\n"
      "subcommands (run `lhd pyrope <subcommand> --help` for details):\n"
      "  fmt FILE…   format Pyrope source (clang-format-like): -i in place, else stdout\n"
      "  lsp         the Pyrope LSP server over stdio (JSON-RPC; .prp only)\n"
      "\n"
      "examples:\n"
      "  lhd pyrope fmt -i foo.prp\n"
      "  lhd pyrope fmt foo.prp --indent 2 --width 100\n"
      "  lhd pyrope lsp\n");
  return 0;
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
    return print_options_section({"pass.color."});
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
    return print_options_section({"pass.partition."});
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
    return print_options_section({"pass.abc."});
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
    return print_options_section({"pass.liberty."});
  }
  if (sub == "semdiff") {
    std::print(
        "lhd pass semdiff — structural diff/match (a structural LEC)\n"
        "\n"
        "usage: lhd pass semdiff --ref lg:DIR --impl lg:DIR [flags]\n"
        "  Establishes a structural correspondence between the two designs: corresponding\n"
        "  nodes (and their driver pins) get a shared `match` id, a node with no counterpart\n"
        "  gets 0. Anchored frontier propagation, meet-in-the-middle: forward from inputs\n"
        "  (commutative-aware), then backward from outputs for whatever is still unmatched.\n"
        "  Both lg: libraries are marked in place and saved, so the diff is greppable and\n"
        "  visualizable. v1 marks lg: libraries, so both sides must be lg:DIR (compile first).\n"
        "\n"
        "flags:\n"
        "  --ref lg:DIR   --impl lg:DIR\n"
        "  --top T        --ref-top T   --impl-top T\n"
        "  --set pass.semdiff.matching_names=true   anchor internal flops/mems by hierarchical name\n"
        "  --set pass.semdiff.id_granularity=region one id per connected matched region (else pair)\n"
        "\n"
        "inspect the result:\n"
        "  lhd tool grep match=0 lg:impl       # what in impl has no counterpart (the diff)\n"
        "  lhd tool grep -v match=0 lg:ref     # what in ref matched\n"
        "  lhd tool diff lg:ref lg:impl --match  # visualize: matched regions + -/+ differences\n"
        "\n"
        "examples:\n"
        "  lhd pass semdiff --ref lg:gold --impl lg:opt --top adder\n"
        "  lhd pass semdiff --ref lg:gold --impl lg:opt --set pass.semdiff.matching_names=true\n");
    return print_options_section({"pass.semdiff."});
  }
  if (!sub.empty()) {
    std::print(stderr, "lhd help: unknown pass subcommand '{}' (color | partition | abc | liberty | semdiff)\n", sub);
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
      "  semdiff              structural diff/match of two lg: (--ref/--impl; marked in place)\n"
      "\n"
      "examples:\n"
      "  lhd pass color acyclic --top m lg:dir\n"
      "  lhd pass partition --top m lg:dir --emit-dir lg:parts\n"
      "  lhd pass abc --top m lg:dir --emit-dir lg:net\n"
      "  lhd pass liberty gensim sky130.lib --emit-dir lg:models\n"
      "  lhd pass semdiff --ref lg:gold --impl lg:opt --top adder\n");
  return 0;
}

// ---- `--diag-fmt jsonl` help: the machine record for a help page ------------
// `lhd help X` / `lhd X --help` honor --diag-fmt just like `list`/`describe`:
// pretty prints the human page (the functions above), jsonl prints a JSON
// record. For a topic that `lhd describe` already covers (compile/lec/formal/
// scan/tool/pass/pass semdiff/pyrope fmt|lsp, and the recipes/pass.flags/
// emit-kinds describe knows) the record IS the describe record — one source of
// truth, no drift. The topics describe has no entry for get their own records
// here: the general overview, the pyrope/pass sub-command pages, the `sim`
// COMMAND (distinct from the `sim` emit-kind describe returns), and list/
// describe/version. An emit-kind describe lacks a record for (e.g. graphviz)
// falls through to the same "unknown name" error in either format — a pre-
// existing describe gap, consistent across pretty/jsonl, not a help defect.

std::string json_general() {
  return std::format(
      R"json({{"schema_version":1,"name":"lhd","version":"{}","description":"LiveHD stateless CLI kernel: one hermetic invocation per flow (declared inputs + config -> declared outputs + exit code); drives the registered pass/inou (EPRP) methods via argv","commands":[{{"name":"compile","summary":"sources and/or ln:/lg: IR -> ln:/lg:/verilog/pyrope (front-end + elaborate + synth)"}},{{"name":"sim","summary":"build + run a C++ simulation of a Pyrope design's test blocks (dynamic verify)"}},{{"name":"lec","summary":"logic equivalence check: prove_equal(ref, impl); --set lec.solver = cvc5|bitwuzla|lgyosys"}},{{"name":"formal","summary":"formal verification family: verify (assert/assume BMC) | lec (= lhd lec)"}},{{"name":"scan","summary":"report each .prp file's import strings"}},{{"name":"tool","summary":"inspect ln:/lg: artifacts: cat | grep | diff | tree"}},{{"name":"pyrope","summary":"Pyrope developer tools: fmt | lsp"}},{{"name":"pass","summary":"run one graph pass over lg: inputs: color | partition | abc | liberty | semdiff"}},{{"name":"list","summary":"enumerate the CLI vocabulary: steps|recipes|emit-kinds|error-classes|options|log-channels"}},{{"name":"describe","summary":"one item's full record as JSON"}},{{"name":"version","summary":"print the tool version"}},{{"name":"help","summary":"per-command help: lhd help <command> (== lhd <command> --help)"}}],"examples":["lhd compile x.prp --emit verilog:net.v","lhd lec --impl impl.prp --ref ref.v","lhd help compile"]}})json",
      kVersion);
}

std::string json_version() {
  return std::format(
      R"json({{"schema_version":1,"name":"version","version":"{}","description":"Print the tool version (also lhd --version)","examples":["lhd version"]}})json",
      kVersion);
}

constexpr std::string_view kJsonPyropeOverview =
    R"json({"schema_version":1,"name":"pyrope","description":"Pyrope developer tools (language-adjacent, not the compile/synth flow)","subcommands":[{"name":"fmt","summary":"format Pyrope source (clang-format-like): -i in place, else stdout"},{"name":"lsp","summary":"the Pyrope LSP server over stdio (JSON-RPC; .prp only)"}],"examples":["lhd pyrope fmt -i foo.prp","lhd pyrope lsp"]})json";

constexpr std::string_view kJsonPassColor =
    R"json({"schema_version":1,"name":"pass color","description":"Node coloring over an lg: library, in place: acyclic|synth|path|mincut (alg defaults to acyclic). The coloring is written back into the input lg:","args":{"required":[{"name":"alg","type":"enum","values":["acyclic","synth","path","mincut"],"default":"acyclic","positional":true},{"name":"inputs","type":"lg:DIR","positional":true}],"optional":[{"name":"top","type":"string"},{"name":"set","type":"pass.color.flag=value","repeatable":true}]},"inputs":["lg"],"outputs":["lg"],"examples":["lhd pass color acyclic --top m lg:dir"]})json";

constexpr std::string_view kJsonPassPartition =
    R"json({"schema_version":1,"name":"pass partition","description":"Split a design into region -> module Subs (LEC-equivalent). --emit-dir lg: (must differ from the input) receives the partitioned library","args":{"required":[{"name":"inputs","type":"lg:DIR","positional":true}],"optional":[{"name":"top","type":"string"},{"name":"emit-dir","type":"lg:DIR/"},{"name":"set","type":"pass.partition.flag=value","repeatable":true}]},"inputs":["lg"],"outputs":["lg"],"examples":["lhd pass partition --top m lg:dir --emit-dir lg:parts"]})json";

constexpr std::string_view kJsonPassAbc =
    R"json({"schema_version":1,"name":"pass abc","description":"Combinational ABC tech-map: bit-blast -> AIG -> sky130 blackboxes. --emit-dir lg: (must differ from the input) receives the mapped netlist","args":{"required":[{"name":"inputs","type":"lg:DIR","positional":true}],"optional":[{"name":"top","type":"string"},{"name":"emit-dir","type":"lg:DIR/"},{"name":"set","type":"pass.abc.flag=value","repeatable":true}]},"inputs":["lg"],"outputs":["lg"],"examples":["lhd pass abc --top m lg:dir --emit-dir lg:net"]})json";

constexpr std::string_view kJsonPassLiberty =
    R"json({"schema_version":1,"name":"pass liberty","description":"Liberty cells -> LGraph simulation models (gensim). Takes a Liberty FILE (not an lg: input); --emit-dir lg: receives the model library","args":{"required":[{"name":"subcommand","type":"enum","values":["gensim"],"positional":true},{"name":"file","type":"path (.lib)","positional":true}],"optional":[{"name":"emit-dir","type":"lg:DIR/"},{"name":"set","type":"pass.liberty.flag=value","repeatable":true}]},"inputs":[],"outputs":["lg"],"examples":["lhd pass liberty gensim sky130.lib --emit-dir lg:models"]})json";

constexpr std::string_view kJsonSimCommand =
    R"json({"schema_version":1,"name":"sim","description":"Build and run a C++ simulation of a Pyrope design's `test` blocks (dynamic verify): the DUT lowers to a Slop<N> struct (inou.cgen.sim, over ../hlop) and ONE C++ driver holding every test block is host-compiled and run — each test's asserts are checked by running, not formally. An optional second positional selects a single test; each `test name(params)` parameter becomes a --<name> flag on the generated binary","args":{"required":[{"name":"file","type":"path (.prp)","positional":true}],"optional":[{"name":"test","type":"string","positional":true},{"name":"arg","type":"key=value","repeatable":true},{"name":"seed","type":"int"},{"name":"list-tests","type":"flag"},{"name":"setup-only","type":"flag"},{"name":"run-only","type":"flag"},{"name":"workdir","type":"path"},{"name":"result-json","type":"path"},{"name":"restart-at","type":"int"},{"name":"vcd-from","type":"int"},{"name":"vcd-to","type":"int"},{"name":"vcd-on-fail","type":"flag"},{"name":"vcd-fail-window","type":"int"},{"name":"list-signals","type":"flag"},{"name":"probe","type":"SIG,..."},{"name":"probe-from","type":"int"},{"name":"probe-to","type":"int"},{"name":"break-when","type":"SIG OP VALUE"},{"name":"set","type":"sim.flag=value","repeatable":true}]},"inputs":["pyrope"],"outputs":["sim"],"examples":["lhd sim foo.prp","lhd sim foo.prp --list-tests","lhd sim foo.prp my_test --arg n=4","lhd sim foo.prp --set sim.vcd=true"]})json";

constexpr std::string_view kJsonList =
    R"json({"schema_version":1,"name":"list","description":"Enumerate the CLI vocabulary as one JSON line (options also honors --diag-fmt pretty). Patterns: steps | recipes | emit-kinds | error-classes | options [REGEX] | log-channels","args":{"required":[{"name":"pattern","type":"enum","values":["steps","recipes","emit-kinds","error-classes","options","log-channels"],"positional":true}],"optional":[{"name":"regex","type":"string (options name filter)","positional":true}]},"examples":["lhd list options 'cgen\\..*'","lhd list recipes","lhd list log-channels"]})json";

constexpr std::string_view kJsonDescribe =
    R"json({"schema_version":1,"name":"describe","description":"One item's full record as JSON (the machine face of help; pretty prose for a pass.flag option). Accepts a command, recipe:NAME, emit-kind, pass.flag, dump, or config","args":{"required":[{"name":"name","type":"command | recipe:NAME | emit-kind | pass.flag | dump | config","positional":true}]},"examples":["lhd describe compile.cgen.srcmap","lhd describe lec"]})json";

// Route a `--diag-fmt jsonl` help page to its JSON record. `topic`/`sub` are the
// normalized help words (formal lec already folded to lec by the caller).
int help_json_dispatch(const std::string& topic, const std::string& sub, const Options& opts) {
  // Reuse the describe record for a topic describe already knows (no drift):
  // render it by asking describe_command for that exact name.
  auto describe_as = [&](std::string name) {
    Options t = opts;
    t.files   = {std::move(name)};
    return describe_command(t);
  };

  if (topic.empty() || topic == "help") {
    print_json_line(json_general());
    return 0;
  }
  if (topic == "version") {
    print_json_line(json_version());
    return 0;
  }
  if (topic == "sim") {  // the `sim` COMMAND, not the `sim` emit-kind describe returns
    print_json_line(kJsonSimCommand);
    return 0;
  }
  if (topic == "list") {
    print_json_line(kJsonList);
    return 0;
  }
  if (topic == "describe") {
    print_json_line(kJsonDescribe);
    return 0;
  }
  if (topic == "lsp") {  // convenience alias for `pyrope lsp`
    return describe_as("pyrope lsp");
  }
  if (topic == "semdiff") {  // convenience alias for `pass semdiff`
    return describe_as("pass semdiff");
  }
  if (topic == "pyrope") {
    if (sub.empty()) {
      print_json_line(kJsonPyropeOverview);
      return 0;
    }
    if (sub == "fmt" || sub == "lsp") {
      return describe_as("pyrope " + sub);
    }
    std::print(stderr, "lhd help: unknown pyrope subcommand '{}' (fmt | lsp)\n", sub);
    return 1;
  }
  if (topic == "pass") {
    if (sub.empty()) {
      return describe_as("pass");
    }
    if (sub == "semdiff") {
      return describe_as("pass semdiff");
    }
    if (sub == "color") {
      print_json_line(kJsonPassColor);
      return 0;
    }
    if (sub == "partition") {
      print_json_line(kJsonPassPartition);
      return 0;
    }
    if (sub == "abc") {
      print_json_line(kJsonPassAbc);
      return 0;
    }
    if (sub == "liberty") {
      print_json_line(kJsonPassLiberty);
      return 0;
    }
    std::print(stderr, "lhd help: unknown pass subcommand '{}' (color | partition | abc | liberty | semdiff)\n", sub);
    return 1;
  }
  // compile / lec / formal / scan / tool, plus every non-command describe topic
  // (recipe:NAME, emit-kind, pass.flag, dump, config): describe renders the JSON.
  return describe_as(topic);
}

int help_command(const Options& opts) {
  std::string topic = opts.files.empty() ? "" : opts.files.front();
  std::string sub   = opts.files.size() > 1 ? opts.files[1] : "";

  // `formal lec` is a behavior-preserving alias of `lec` (parse_args already
  // rewrites `lhd formal lec --help` to the `lec` topic); fold the
  // `lhd help formal lec` spelling to match so the two render identically.
  if (topic == "formal" && sub == "lec") {
    topic = "lec";
    sub.clear();
  }
  // `tools` is the accepted alias for the `tool` command (parse_args maps the
  // command word); apply it on the help topic too so `lhd help tools` matches
  // `lhd tools --help`.
  if (topic == "tools") {
    topic = "tool";
  }

  // --diag-fmt jsonl -> the machine record (mirrors `list`/`describe`); pretty
  // falls through to the human pages below.
  if (opts.diag_fmt == Diag_fmt::jsonl) {
    return help_json_dispatch(topic, sub, opts);
  }

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
        "  --top T              --reader R   slang | yosys-slang | yosys-verilog (default slang)\n"
        "  --recipe O0|O1|O2    (default O1; `lhd list recipes`)\n"
        "  --emit verilog:PATH | pyrope:PATH   (or a bare .v/.sv/.prp — kind inferred)\n"
        "  --emit-dir K:DIR/    lg: | ln: | verilog: | pyrope: | lnast-dump: | isabelle: | lean: | sim:\n"
        "                       (sim: = executable C++ simulation; `cd DIR && bazel build //:sim`)\n"
        "  --set pass.flag=value   --config lhd.toml   --depfile PATH   --workdir DIR\n"
        "  --unused-inputs PATH  declared source files whose contents did not reach the\n"
        "                        compiled closure (e.g. modules outside the --top hierarchy);\n"
        "                        one cwd-relative path per line, empty when everything was\n"
        "                        read — the Bazel unused_inputs_list format (input pruning)\n"
        "\n"
        "debug dumps (printed to stderr; a dump forces the stage that produces it):\n"
        "  --dump parse|lnast|lg   post-parse LNAST | post-upass LNAST | textual LGraph\n"
        "               lhd compile x.prp --dump parse,lnast\n"
        "               lhd compile x.prp --recipe O0 --dump lg\n"
        "\n"
        "examples:\n"
        "  lhd compile foo.v --top foo --recipe O2 --emit verilog:net.v\n"
        "  lhd compile x.prp --emit net.v --emit-dir lg:x_lgs/\n"
        "  lhd compile x.prp --emit-dir ln:x_lns/        # pre-elaborate for importers\n"
        "  lhd compile ln:x_lns/ --emit verilog:net.v    # synth from IR\n"
        "  lhd compile lg:foo_lgs/ --emit-dir lg:foo_opt_lgs/\n"
        "  lhd compile x.prp --emit-dir sim:x_sim/        # C++ sim (cd x_sim && bazel build //:sim)\n");
    return print_options_section({"compile."});
  }
  if (topic == "lec") {
    std::print(
        "lhd lec — logic equivalence (LEC): prove_equal(ref, impl)\n"
        "\n"
        "usage: lhd lec --impl KIND:PATH --ref KIND:PATH [flags]\n"
        "  Sides may be verilog:/pyrope:/ln:/lg: or a bare .v/.sv/.prp path (kind inferred).\n"
        "  Each side is loaded/elaborated to LGraphs; verilog elaborates through --reader\n"
        "  (default slang, the direct SV->LNAST front-end; --reader yosys-slang|yosys-verilog\n"
        "  overrides). The --set lec.solver knob selects the backend:\n"
        "    cvc5     in-process SMT (default)\n"
        "    bitwuzla in-process SMT\n"
        "    lgyosys  inou/yosys/lgcheck (the former `lhd check`; reads Verilog directly,\n"
        "             the path for gate-level / yosys-origin netlists)\n"
        "\n"
        "flags:\n"
        "  --impl KIND:PATH   --ref KIND:PATH\n"
        "  --top T            --impl-top T   --ref-top T\n"
        "  --reader R         --set lec.flag=value\n"
        "\n"
        "examples:\n"
        "  lhd lec --impl impl.prp --ref ref.v\n"
        "  lhd lec --impl lg:impl/ --ref lg:ref/ --top foo --set lec.engine=ind\n"
        "  lhd lec --impl net.v --ref gold.v --set lec.solver=lgyosys --top foo\n");
    return print_options_section({"lec."});
  }
  if (topic == "formal" || topic == "formal verify") {
    std::print(
        "lhd formal — formal verification family (2f-verify)\n"
        "\n"
        "usage: lhd formal verify <design> [--top T] [flags]\n"
        "       lhd formal lec    --impl KIND:PATH --ref KIND:PATH [flags]   (= lhd lec)\n"
        "\n"
        "verify proves ONE design's assert / assert_always / assume obligations by BMC\n"
        "from reset (the pass/lec engine): each obligation is checked per cycle as its\n"
        "own solver query, every proven fact immediately prunes the search for the rest\n"
        "(frontier assumes), and a timeout costs one obligation at one cycle — the run\n"
        "reports a per-assert verdict table, not a single verdict:\n"
        "  PROVEN to cycle k   BOUNDED (no violation within the unrolled window)\n"
        "  REFUTED at cycle k  a REACHABLE violation + the per-cycle input trace (fails)\n"
        "  UNKNOWN             solver gave up / blackbox artifact / contradictory assumes\n"
        "                      (a loud warning; --set formal.strict=true makes it fail)\n"
        "`assume` is an environment constraint, in force at every checked cycle and\n"
        "disclosed in the table (verdicts are conditional on it).\n"
        "\n"
        "the design: a bare .prp/.v/.sv path, --impl KIND:PATH, or lg:DIR.\n"
        "\n"
        "knobs (--set): formal.* (bound, timeout, phase, reset, reset_cycles,\n"
        "  witness, strict, solver, ...); the legacy lec.* spellings stay accepted.\n"
        "\n"
        "examples:\n"
        "  lhd formal verify foo.prp --top foo --set formal.bound=12\n"
        "  lhd formal verify net.v --set formal.timeout=60 --set formal.strict=true\n"
        "  lhd formal verify design.prp --set formal.phase=full   # reset window too\n");
    return print_options_section({"formal."});
  }
  if (topic == "semdiff") {
    // `semdiff` moved under `pass`; keep `lhd help semdiff` as a convenience
    // alias for `lhd help pass semdiff`.
    return help_pass("semdiff");
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
  if (topic == "tool") {
    std::print(
        "lhd tool — unified ln/lg inspector: cat | grep | diff | tree\n"
        "\n"
        "usage: lhd tool <verb> [options] <inputs…>\n"
        "  inputs are ln:DIR / lg:DIR / verilog:FILE / pyrope:FILE (a bare .prp/.v/.sv\n"
        "  source is the verilog:/pyrope: shortcut; the ln path takes sources).\n"
        "  cat   structured dump of one input (attributes shown by default)\n"
        "  grep  filtered search (>=1 filter; may span multiple lg: libraries; -v inverts)\n"
        "  diff  unified-diff of two inputs (-C n text-line context; --match: semdiff view)\n"
        "  tree  instance hierarchy rooted at --top\n"
        "\n"
        "lg options:\n"
        "  --target node|pin|edge|all   what to show (default all; node=color/src,\n"
        "                               pin=bits/signed, edge=wiring)\n"
        "  --top M    pick a module     --attr CSV   choose columns\n"
        "  --hier [N] descend instances --max N      row cap (0 = unlimited)\n"
        "\n"
        "tree --target kind:<X> (repeatable): also list the nodes of kind X inside each\n"
        "  module, so registers/memories show where they sit in the hierarchy. kind:register\n"
        "  = flop/fflop/latch, kind:memory = memory; any Ntype name (flop, mux, sub, …) also\n"
        "  matches exactly. Default (no kind) = the bare instance tree.\n"
        "\n"
        "filters (AND-combined): a bare term matches any column + identity (grep get_mask),\n"
        "  or field=value: name=Mult  kind=mux  id=12  color=nil  match=0  bits>8  bits=8..16  from=A\n"
        "  ('=' preferred — Pyrope reads ':' as a type; ':' still works; strings ==exact, ~regex).\n"
        "  grep -v inverts (keep records that do NOT match — `grep -v match=0` = the matched part).\n"
        "\n"
        "examples:\n"
        "  lhd tool cat lg:dir --top m\n"
        "  lhd tool grep color=nil lg:dir            # nodes pass.color left uncolored\n"
        "  lhd tool grep match=0 lg:dir              # nodes semdiff found no counterpart for\n"
        "  lhd tool diff lg:before lg:after --attr color\n"
        "  lhd tool diff lg:gold lg:opt --match      # visualize the semdiff structural diff\n"
        "  lhd tool tree lg:dir --top m --target kind:register --target kind:memory\n"
        "  lhd tool cat x.prp          lhd tool diff old.prp new.prp   # the former ln.cat/ln.diff\n");
    return 0;
  }
  if (topic == "lsp") {
    // The LSP server now lives under `pyrope`; keep `lhd help lsp` as a
    // convenience pointer to the canonical `lhd pyrope lsp` help.
    return help_pyrope("lsp");
  }
  if (topic == "pyrope") {
    return help_pyrope(sub);
  }
  if (topic == "pass") {
    return help_pass(sub);
  }
  if (topic == "sim") {
    std::print(
        "lhd sim — build and run a C++ simulation of a Pyrope design's `test` blocks\n"
        "\n"
        "usage: lhd sim <file.prp> [test.name] [flags]\n"
        "  Lowers the design's DUT to a Slop<N> struct (inou.cgen.sim, over ../hlop) and\n"
        "  generates ONE C++ driver (drv.cpp) holding every `test` block. The driver is\n"
        "  built with the host C++ compiler (header-only Slop runtime, no bazel) and run —\n"
        "  each `test`'s asserts are checked dynamically (by running), not formally. An\n"
        "  optional second positional selects a single test by name.\n"
        "\n"
        "  Each `test name(params)` parameter becomes a `--<name>` flag on the generated\n"
        "  binary (defaulting to its signature default; a parameter with no default is\n"
        "  required). The binary also accepts `--list-tests`, `--test NAME`, `--seed N`\n"
        "  (hlop PRNG seed), and `--help`. `--arg key=value` / `--seed N` here are forwarded\n"
        "  to it, and the built binary can be re-run directly with those flags.\n"
        "\n"
        "flags:\n"
        "  --list-tests         list the design's tests + parameters, then exit (no build; JSON or\n"
        "                       a human listing per --diag-fmt)\n"
        "  --arg key=value      bind a test runtime parameter, forwarded as `--key value` (repeatable)\n"
        "  --seed N             PRNG seed forwarded to the driver (else it keeps its default)\n"
        "  --result-json PATH   (global) the result envelope gains a per-test `tests` array (status,\n"
        "                       cycle, located failing assert) for tooling\n"
        "  --restart-at N       resume from the nearest checkpoint <= cycle N (debug a long run)\n"
        "  --vcd-from Y [--vcd-to Z]  trace a VCD over just cycles [Y, Z] (restarts near Y; implies VCD)\n"
        "  --vcd-on-fail [--vcd-fail-window N]  on an assert fire, auto-dump a VCD of the last N cycles\n"
        "  --list-signals       list the observable scalar signals (hierarchical names) as JSON, then exit\n"
        "  --probe SIG,... [--probe-from A --probe-to B]  per-cycle JSON trajectory of SIG (no re-instrumenting)\n"
        "  --break-when 'SIG OP V'  report the first cycle a `SIG >|<|>=|<=|==|!= VALUE|SIG` condition holds\n"
        "  --setup-only         generate the C++ sim driver, do not build/run\n"
        "  --run-only           host-compile + run an existing sim (needs --workdir from --setup-only)\n"
        "  --workdir DIR        reuse DIR as the build dir (else a fresh temp dir)\n"
        "  --set sim.flag=value VCD + checkpoint knobs (the options block below; `lhd describe sim.flag`)\n"
        "\n"
        "examples:\n"
        "  lhd sim foo.prp                                # build + run every test in foo.prp\n"
        "  lhd sim foo.prp --list-tests                   # enumerate the tests + params as JSON\n"
        "  lhd sim foo.prp my_test                        # run just the `my_test` block\n"
        "  lhd sim foo.prp my_test --arg n=4              # bind the test parameter n=4\n"
        "  lhd sim foo.prp my_test --seed 42             # reproducible randomized run\n"
        "  lhd sim foo.prp --result-json r.json           # envelope + per-test located-failure array\n"
        "  lhd sim foo.prp --set sim.vcd=true             # also dump a VCD per test\n"
        "  lhd sim foo.prp --setup-only --workdir build/  # generate, then build it yourself\n"
        "  ./build/sim/drv.bin --test my_test --seed 7    # run the built driver directly\n"
        "  ./build/sim/drv.bin --list-tests               # list tests from the built binary\n"
        "  lhd sim foo.prp --run-only --workdir build/    # rebuild/run a prior --setup-only\n");
    return print_options_section({"sim."});
  }
  if (topic == "list") {
    std::print(
        "lhd list — enumerate the CLI vocabulary (one JSON line; --diag-fmt pretty for options)\n"
        "\n"
        "usage: lhd list <pattern>\n"
        "  steps | recipes | emit-kinds | error-classes | options [REGEX] | log-channels\n"
        "  `options` lists every --set/--config pass.flag (filter with a REGEX over the names).\n"
        "  `log-channels` lists the developer-logging channels (`--set <channel>.log=<level>`).\n"
        "\n"
        "examples:\n"
        "  lhd list options 'cgen\\..*'\n"
        "  lhd list log-channels\n"
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
        "  lhd describe compile.cgen.srcmap\n"
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
