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

constexpr std::string_view kSteps
    = R"json(["compile verilog","compile pyrope","synth","sim","lec","formal verify","formal lec","scan","tool","pass","pyrope fmt","pyrope lsp"])json";
constexpr std::string_view kRecipes = R"json(["O0","O1","O2"])json";
constexpr std::string_view kEmitKinds
    = R"json(["ln","lg","verilog","pyrope","lnast-dump","isabelle","lean","sim","graphviz","metadata","results","report","diagnostics"])json";
constexpr std::string_view kErrorClasses
    = R"json(["usage","syntax","internal","equiv_fail","signal","timeout","missing_file","config","dependency","unsupported","assert","compile"])json";

constexpr std::string_view kJsonSynthCommand
    = R"json({"schema_version":1,"name":"synth","description":"One-shot synthesis flow over ONE in-memory design: compile (Pyrope/(System)Verilog sources and/or ln:/lg: IR, as `lhd compile`) -> pass.color reduce (synth.reduce=true; shares repeated one/two-node combinational cones) -> pass.color synth (always; per-(def,color) regions keep a big design inside ABC's memory budget and are what incremental reuse is keyed on — other colorings are the manual `lhd pass color <alg>` + `lhd pass abc` steps) -> pass.abc tech-map -> pass.opentimer STA (synth.opentimer=true). --top is resolved once (a bare entity is enough). ONE Liberty (synth.liberty, default $HAGENT_TECH_DIR/sky130_fd_sc_hd__tt_025C_1v80.lib) feeds both abc and opentimer. --workdir is optional: with one, <workdir>/synth/ keeps lg/ (compiled design), net/ (mapped netlist), qor.json and timing.json, and the compile + abc_cache incremental tiers are live (lhd.incremental, default true; false = honest cold run, same outputs); without one the flow runs in a scratch dir and only the emits and the printed report survive. An lg: input is never rewritten. The result envelope's `qor` member is {kind:synth, abc:<abc-map>, sta:<sta>}; --stats adds the per-color rows of both","args":{"required":[{"name":"files","type":"path[] and/or ln:DIR|lg:DIR","positional":true}],"optional":[{"name":"top","type":"string"},{"name":"workdir","type":"path"},{"name":"emit-dir","type":"lg:DIR/ (mapped netlist; relocates <workdir>/synth/net) | verilog:DIR/ | report:DIR/ (qor.json + timing.json)"},{"name":"emit","type":"verilog:PATH (mapped netlist)"},{"name":"stats","type":"flag"},{"name":"reader","type":"enum","values":["slang","yosys-slang","yosys-verilog"],"default":"slang"},{"name":"recipe","type":"enum","values":["O0","O1","O2"],"default":"O1"},{"name":"set","type":"synth.flag=value | abc.flag=value | color.flag=value | opentimer.flag=value | compile.<pass>.flag=value","repeatable":true},{"name":"result-json","type":"path"}]},"inputs":["pyrope","verilog","ln","lg"],"outputs":["lg","verilog","report"],"examples":["lhd synth cpu.prp --top Cpu --workdir W","lhd synth cpu.prp --top Cpu --workdir W --stats --result-json r.json","lhd synth lg:cpu_lg --top Cpu --emit-dir lg:net --emit-dir report:rep","lhd synth cpu.prp --top Cpu --set synth.liberty=cells.lib --set synth.opentimer=false","lhd synth cpu.prp --top Cpu --workdir W --set lhd.incremental=false","lhd synth cpu.sv --top cpu --set abc.adder=cla --emit verilog:net.v"]})json";

void print_json_line(std::string_view s) {
  std::fwrite(s.data(), 1, s.size(), stdout);
  std::fputc('\n', stdout);
}

std::string json_escape(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '"' : out += "\\\""; break;
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
  auto             cut = help.find(". ");
  std::string      out{cut == std::string_view::npos ? help : help.substr(0, cut)};
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

  const auto                     all = list_set_options();
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
      std::print(stderr,
                 "lhd describe: unknown option '{}' (`lhd list options {}\\..*` shows what {} accepts)\n",
                 name,
                 prefix,
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

struct Help_arg {
  std::string              name;
  std::string              syntax;
  std::string              type;
  std::string              help;
  bool                     required   = false;
  bool                     positional = false;
  bool                     repeatable = false;
  std::vector<std::string> aliases;
};

struct Help_doc {
  std::string                                      name;
  std::string                                      summary;
  std::string                                      usage;
  std::vector<std::string>                         details;
  std::vector<Help_arg>                            args;
  std::vector<std::pair<std::string, std::string>> subcommands;
  std::vector<std::string>                         examples;
  std::vector<std::string>                         inputs;
  std::vector<std::string>                         outputs;
  bool                                             leaf = true;
};

Help_arg help_positional(std::string name, std::string type, std::string help, bool required = true, bool repeatable = false) {
  return Help_arg{std::move(name), "", std::move(type), std::move(help), required, true, repeatable, {}};
}

Help_arg help_flag(std::string name, std::string syntax, std::string type, std::string help,
                   std::vector<std::string> aliases = {}) {
  return Help_arg{std::move(name), std::move(syntax), std::move(type), std::move(help), false, false, false, std::move(aliases)};
}

Help_arg help_repeatable_flag(std::string name, std::string syntax, std::string type, std::string help) {
  auto arg       = help_flag(std::move(name), std::move(syntax), std::move(type), std::move(help));
  arg.repeatable = true;
  return arg;
}

std::string json_string_array(const std::vector<std::string>& values) {
  std::string out = "[";
  for (const auto& value : values) {
    if (out.size() > 1) {
      out += ',';
    }
    out += std::format("\"{}\"", json_escape(value));
  }
  out += ']';
  return out;
}

std::string render_help_json(const Help_doc& doc) {
  std::string description = doc.summary;
  for (const auto& detail : doc.details) {
    description += description.empty() ? detail : ". " + detail;
  }
  std::string out = std::format(R"json({{"schema_version":1,"name":"{}","description":"{}","usage":"lhd {}")json",
                                json_escape(doc.name),
                                json_escape(description),
                                json_escape(doc.usage));
  if (!doc.subcommands.empty()) {
    out += R"json(,"subcommands":[)json";
    for (size_t i = 0; i < doc.subcommands.size(); ++i) {
      if (i != 0) {
        out += ',';
      }
      out += std::format(R"json({{"name":"{}","summary":"{}"}})json",
                         json_escape(doc.subcommands[i].first),
                         json_escape(doc.subcommands[i].second));
    }
    out += ']';
  }
  if (!doc.args.empty()) {
    out             += R"json(,"args":{"required":[)json";
    bool first       = true;
    auto append_arg  = [&](const Help_arg& arg) {
      if (!first) {
        out += ',';
      }
      first  = false;
      out   += std::format(R"json({{"name":"{}","type":"{}","help":"{}")json",
                           json_escape(arg.name),
                           json_escape(arg.type),
                           json_escape(arg.help));
      if (arg.positional) {
        out += R"json(,"positional":true)json";
      }
      if (arg.repeatable) {
        out += R"json(,"repeatable":true)json";
      }
      if (!arg.aliases.empty()) {
        out += std::format(R"json(,"aliases":{})json", json_string_array(arg.aliases));
      }
      out += '}';
    };
    for (const auto& arg : doc.args) {
      if (arg.required) {
        append_arg(arg);
      }
    }
    out   += R"json(],"optional":[)json";
    first  = true;
    for (const auto& arg : doc.args) {
      if (!arg.required) {
        append_arg(arg);
      }
    }
    out += "]}";
  }
  if (!doc.inputs.empty()) {
    out += std::format(R"json(,"inputs":{})json", json_string_array(doc.inputs));
  }
  if (!doc.outputs.empty()) {
    out += std::format(R"json(,"outputs":{})json", json_string_array(doc.outputs));
  }
  out += std::format(R"json(,"examples":{})json", json_string_array(doc.examples));
  out += '}';
  return out;
}

void render_help_pretty(const Help_doc& doc) {
  std::print("lhd {} — {}\n\nusage: lhd {}\n", doc.name, doc.summary, doc.usage);
  for (const auto& detail : doc.details) {
    std::print("\n");
    print_wrapped(detail, 92, "  ");
  }
  if (!doc.subcommands.empty()) {
    std::print("\nsubcommands:\n");
    size_t width = 0;
    for (const auto& [name, summary] : doc.subcommands) {
      (void)summary;
      width = std::max(width, name.size());
    }
    for (const auto& [name, summary] : doc.subcommands) {
      std::print("  {:<{}}  {}\n", name, width, summary);
    }
  }
  if (doc.leaf) {
    std::print("\nflags:\n");
    size_t width = 0;
    for (const auto& arg : doc.args) {
      if (!arg.positional) {
        width = std::max(width, arg.syntax.size());
      }
    }
    if (width == 0) {
      std::print("  none\n");
    } else {
      for (const auto& arg : doc.args) {
        if (!arg.positional) {
          std::print("  {:<{}}  {}\n", arg.syntax, width, arg.help);
        }
      }
    }
  }
  std::print("\nexamples:\n");
  for (const auto& example : doc.examples) {
    std::print("  {}\n", example);
  }
}

const Help_doc* tool_help_doc(std::string_view name) {
  static const Help_doc parent{
      "tool",
      "inspect ln:/lg: artifacts",
      "tool <subcommand> [args] [flags]",
      {},
      {},
      {{"cat", "structured dump of one ln:/lg:/source input"},
        {"grep", "filtered search over one or more lg: libraries"},
        {"diff", "unified or semdiff-aware comparison of two inputs"},
        {"tree", "lg: instance hierarchy or LNAST structural skeleton"}},
      {"lhd tool cat lg:dir --top m", "lhd tool grep color=nil lg:dir", "lhd tool diff lg:before lg:after", "lhd tool tree ln:dir"},
      {},
      {},
      false
  };
  static const Help_doc cat{
      "tool cat",
      "structured dump of one ln:/lg:/source input",
      "tool cat [filters…] <input> [flags]",
      {"<input> is one lg:DIR, ln:DIR, verilog:FILE, pyrope:FILE, or bare .prp/.v/.sv source. lg: accepts optional "
       "AND-combined filters; ln:/source dumps whole LNAST units and does not accept filters."},
      {help_positional("input", "lg:DIR|ln:DIR|verilog:FILE|pyrope:FILE|.prp|.v|.sv", "artifact to dump"),
        help_positional("filters", "field=value|term", "AND-combined lg: filters", false, true),
        help_flag("top", "--top M", "string", "select one module/unit"),
        help_flag("target", "--target node|pin|edge|all", "enum", "lg: records to show (default all)"),
        help_flag("attr", "--attr CSV", "csv", "lg: display columns"),
        help_flag("max", "--max N", "int", "output row cap (0 = unlimited)"),
        help_flag("diag-fmt", "--diag-fmt auto|json|pretty", "enum", "lg: human or machine records")},
      {},
      {"lhd tool cat lg:dir --top m", "lhd tool cat color=nil lg:dir", "lhd tool cat x.prp"},
      {"ln", "lg", "pyrope", "verilog"},
      {"stdout"}
  };
  static const Help_doc grep{
      "tool grep",
      "filtered search over one or more lg: libraries",
      "tool grep <filter…> <lg:DIR…> [flags]",
      {"At least one filter is required. Filters are AND-combined: a bare term matches any column plus identity, or use "
       "field=value (name, kind, id, color, match, bits, from, to). Strings support ==exact and ~regex; numbers support "
       ">, <, >=, <=, and a..b; =nil matches an absent value."},
      {help_positional("filters", "field=value|term", "AND-combined record filters", true, true),
        help_positional("inputs", "lg:DIR", "libraries to search", true, true),
        help_flag("top", "--top M", "string", "select one module"),
        help_flag("target", "--target node|pin|edge|all", "enum", "records to search (default all)"),
        help_flag("attr", "--attr CSV", "csv", "display columns"),
        help_flag("max", "--max N", "int", "output row cap (0 = unlimited)"),
        help_flag("invert-match", "-v, --invert-match", "flag", "keep records that do not match", {"-v"}),
        help_flag("diag-fmt", "--diag-fmt auto|json|pretty", "enum", "human or machine records")},
      {},
      {"lhd tool grep get_mask lg:dir", "lhd tool grep color=nil lg:dir", "lhd tool grep -v match=0 lg:dir"},
      {"lg"},
      {"stdout"}
  };
  static const Help_doc diff{
      "tool diff",
      "compare two ln:/lg:/source inputs",
      "tool diff [filters…] <input-a> <input-b> [flags]",
      {"Inputs must be exactly two lg: libraries or two ln:/source inputs. lg: accepts optional AND-combined filters; "
       "ln:/source compares whole LNAST units."},
      {help_positional("inputs", "two lg:DIR values or two ln:DIR/source values", "artifacts to compare"),
        help_positional("filters", "field=value|term", "AND-combined lg: filters", false, true),
        help_flag("top", "--top M", "string", "select one module/unit"),
        help_flag("target", "--target node|pin|edge|all", "enum", "lg: records to compare (default all)"),
        help_flag("attr", "--attr CSV", "csv", "lg: display columns"),
        help_flag("context", "-C N, --context N", "int", "unified-diff context lines", {"-C"}),
        help_flag("match", "--match", "flag", "visualize pass.semdiff match attributes"),
        help_flag("structural", "--structural", "flag", "strict H5: semdiff identity plus IO/semantic-attribute equality")},
      {},
      {"lhd tool diff lg:before lg:after --attr color",
        "lhd tool diff lg:cold lg:warm --structural", "lhd tool diff lg:gold lg:opt --match",
        "lhd tool diff old.prp new.prp"},
      {"ln", "lg", "pyrope", "verilog"},
      {"stdout"}
  };
  static const Help_doc tree{
      "tool tree",
      "lg: instance hierarchy or LNAST structural skeleton",
      "tool tree <input> [flags]",
      {"<input> is one lg:DIR, ln:DIR, verilog:FILE, pyrope:FILE, or bare .prp/.v/.sv source. lg: shows the instance "
       "hierarchy; ln:/source shows scope/control/call/definition nodes with per-subtree node counts."},
      {help_positional("input", "lg:DIR|ln:DIR|verilog:FILE|pyrope:FILE|.prp|.v|.sv", "artifact to summarize"),
        help_flag("top", "--top M", "string", "select the root module/unit"),
        help_repeatable_flag("target", "--target kind:<X>", "kind:<X>", "also show nodes of kind X"),
        help_flag("hier", "--hier [N]", "int?", "descend all levels, or at most N levels"),
        help_flag("max", "--max N", "int", "output row cap (0 = unlimited)")},
      {},
      {"lhd tool tree lg:dir --top m",
        "lhd tool tree lg:dir --target kind:register --target kind:memory", "lhd tool tree ln:dir --hier 3"},
      {"ln", "lg", "pyrope", "verilog"},
      {"stdout"}
  };

  if (name == "tool") {
    return &parent;
  }
  if (name == "tool cat") {
    return &cat;
  }
  if (name == "tool grep") {
    return &grep;
  }
  if (name == "tool diff") {
    return &diff;
  }
  if (name == "tool tree") {
    return &tree;
  }
  return nullptr;
}

int describe_tool(std::string_view name) {
  const auto* doc = tool_help_doc(name);
  if (doc == nullptr) {
    return -1;
  }
  print_json_line(render_help_json(*doc));
  return 0;
}

int describe_command(const Options& opts) {
  if (opts.files.empty()) {
    std::print(stderr, "lhd describe: requires a name (a command, recipe:NAME, or an emit kind)\n");
    return 1;
  }
  const std::string& name = opts.files.front();

  if (int rc = describe_tool(name); rc >= 0) {
    return rc;
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
  if (name == "lec" || name == "formal lec") {
    print_json_line(
        R"json({"schema_version":1,"name":"lec","description":"Logic equivalence check (LEC): prove_equal(ref, impl). Sides are verilog:/pyrope:/ln:/lg: (or a bare .v/.sv/.prp; kind inferred), loaded/elaborated to LGraphs (verilog via --reader, default slang). The --set formal.solver knob picks the backend: cvc5 (default, in-process SMT), bitwuzla (in-process SMT), or lgyosys (inou/yosys/lgcheck, the former `lhd check`). Other engine knobs are --set lec.* (`lhd lec --help`)","args":{"required":[{"name":"impl","type":"verilog:PATH|pyrope:PATH|ln:DIR|lg:DIR"},{"name":"ref","type":"verilog:PATH|pyrope:PATH|ln:DIR|lg:DIR"}],"optional":[{"name":"impl-top","type":"string"},{"name":"ref-top","type":"string"},{"name":"top","type":"string"},{"name":"reader","type":"enum","values":["slang","yosys-slang","yosys-verilog"],"default":"slang"},{"name":"set","type":"lec.flag=value","repeatable":true}]},"inputs":["verilog","pyrope","ln","lg"],"outputs":[],"examples":["lhd lec --impl impl.prp --ref ref.v","lhd lec --impl lg:impl/ --ref lg:ref/ --top foo --set formal.engine=ind","lhd lec --impl net.v --ref gold.v --set formal.solver=lgyosys --top foo"]})json");
    return 0;
  }
  // `formal` names the FAMILY (a dispatcher); the runnable thing is the
  // subcommand, so each gets its own record. `formal lec` IS `lec`.
  if (name == "formal") {
    print_json_line(
        R"json({"schema_version":1,"name":"formal","description":"Formal verification command family (2f-verify): a dispatcher, not a flow. `formal verify <design> [sidecar.prp ...] [BLOCK]` proves ONE design's assert/assert_always/assume obligations by BMC from reset — use it to answer \"does this design satisfy the properties I wrote?\". `formal lec --impl X --ref Y` is the equivalence check (an alias of `lhd lec`) — use it to answer \"are these two designs the same function?\". Both share the --set formal.* knob namespace (bound/timeout/solver/strict/...; legacy lec.* spellings stay accepted). Describe a subcommand for its own record: `lhd describe 'formal verify'` / `lhd describe lec`","args":{"required":[{"name":"subcommand","type":"verify|lec","positional":true}],"optional":[]},"subcommands":[{"name":"verify","summary":"prove one design's assert/assume obligations by BMC from reset"},{"name":"lec","summary":"logic equivalence check: prove_equal(ref, impl) (= lhd lec)"}],"inputs":["verilog","pyrope","lg"],"outputs":[],"examples":["lhd formal verify foo.prp --top foo","lhd formal verify ALU.prp ALU.verify.prp --list-tests","lhd formal lec --impl impl.prp --ref ref.v"]})json");
    return 0;
  }
  if (name == "formal verify") {
    print_json_line(
        R"json({"schema_version":1,"name":"formal verify","description":"Proves ONE design's assert/assert_always/assume obligations by BMC from reset on the pass/lec engine: per-obligation solve with frontier assumes, a per-assert/per-cycle verdict table (PROVEN-to-cycle-k is BOUNDED), per-obligation timeout isolation; only a reachable violation fails the run. Extra .prp positionals are formal-block SIDECARS; each `formal name.dotted { ... }` block is an INDEPENDENT test, enumerated and selected exactly like a sim `test`: `--list-tests` prints them as JSON (a pure parse, no design load) and a lone non-path positional (or --formal GLOB) selects one; a selector that matches nothing fails rather than silently proving only the design's own obligations. EVERY run writes formal_report.json into --workdir (per-obligation verdicts/cycles/solve_ms), and a REFUTED run adds one simfail_<formal-test>.prp/.json per refuted test when formal.simfail=true. Knobs: --set formal.* (bound/timeout/phase/reset/simfail/...); legacy lec.* spellings stay accepted","args":{"required":[{"name":"design","type":"path or verilog:PATH|pyrope:PATH|lg:DIR","positional":true}],"optional":[{"name":"sidecars","type":"path (.prp formal blocks)","positional":true,"repeatable":true},{"name":"test","type":"string","positional":true},{"name":"list-tests","type":"flag"},{"name":"formal","type":"GLOB"},{"name":"top","type":"string"},{"name":"workdir","type":"path"},{"name":"set","type":"formal.flag=value","repeatable":true}]},"inputs":["verilog","pyrope","lg"],"outputs":[],"examples":["lhd formal verify foo.prp --top foo --set formal.bound=12","lhd formal verify dut.prp dut.verify.prp --list-tests","lhd formal verify dut.prp dut.verify.prp alu.addw --top ALU","lhd formal verify dut.prp dut.verify.prp --formal 'alu.*' --top ALU","lhd formal verify design.v --set formal.timeout=60 --set formal.strict=true","lhd formal verify foo.prp --workdir w/ --set formal.simfail_run=false"]})json");
    return 0;
  }
  if (name == "semdiff" || name == "pass semdiff") {
    print_json_line(
        R"json({"schema_version":1,"name":"pass semdiff","description":"Structural diff/match (a structural LEC), a `pass` subcommand: structural_match(ref, impl) marks corresponding nodes/driver-pins of both lg: libraries with a shared `match` attribute (0 = no counterpart) and saves both back in place. v1 marks lg: libraries, so both sides must be lg:DIR (compile sources to lg: first). Inspect the diff with `lhd tool grep match=0 lg:impl` or visualize it with `lhd tool diff lg:ref lg:impl --match`. --stats prints the aggregate node/register/memory match report (a design health check: it implies matching_names + state_pairing; an explicit --set of any of those wins). hier defaults true (sweep every def in --top's subtree; --set pass.semdiff.hier=0 compares one top pair). Knobs are --set pass.semdiff.* (matching_names | state_pairing | hier | dump_state | id_granularity=pair|region)","args":{"required":[{"name":"impl","type":"lg:DIR"},{"name":"ref","type":"lg:DIR"}],"optional":[{"name":"impl-top","type":"string"},{"name":"ref-top","type":"string"},{"name":"top","type":"string"},{"name":"stats","type":"flag (aggregate node/register/memory match report)"},{"name":"set","type":"pass.semdiff.flag=value","repeatable":true}]},"inputs":["lg"],"outputs":["lg"],"examples":["lhd pass semdiff --ref lg:gold --impl lg:opt --top adder","lhd pass semdiff --ref lg:gold --impl lg:opt --top adder --stats","lhd pass semdiff --ref lg:gold --impl lg:opt --set pass.semdiff.matching_names=true","lhd tool diff lg:gold lg:opt --match"]})json");
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
    print_json_line(
        R"json({"schema_version":1,"name":"recipe:O1","steps":["pass.cprop"],"description":"Constant/copy propagation"})json");
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
  if (name == "synth") {
    print_json_line(kJsonSynthCommand);
    return 0;
  }
  if (name == "report") {
    print_json_line(
        R"json({"schema_version":1,"name":"report","description":"The `lhd synth` QoR sidecars as files: qor.json (pass.abc, kind abc-map: per-region + total gates/area/critical delay) and timing.json (pass.opentimer, kind sta: max_delay, critical path, endpoints). With a --workdir they also land in <workdir>/synth/; the same two objects ride the result envelope's `qor` member either way. --emit-dir only","direction":"out"})json");
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
        R"json({"schema_version":1,"name":"pass","description":"Run a single graph pass over lg: inputs. Subcommands: color <alg> (acyclic|synth|path|mincut|flat|reduce node coloring/rewrite), partition (region->module Sub split), single_edge (edge normalization: latches/negedge -> posedge flops, verification only), abc (combinational ABC tech-map), opentimer (OpenTimer STA on a tech-mapped module -> timing.json), liberty gensim <file.lib> (Liberty -> sim models), semdiff (structural diff/match of two lg: libraries via --ref/--impl; `lhd describe \"pass semdiff\"`), analyze (read-only structural diagnosis: comb loops, clock endpoints, coloring validity)","args":{"required":[{"name":"subcommand","type":"enum","values":["color","partition","single_edge","abc","opentimer","liberty","semdiff","analyze"]},{"name":"inputs","type":"lg:DIR","positional":true,"repeatable":true}],"optional":[{"name":"top","type":"string"},{"name":"emit-dir","type":"lg:DIR/"},{"name":"ref","type":"lg:DIR (semdiff)"},{"name":"impl","type":"lg:DIR (semdiff)"}]},"inputs":["lg"],"outputs":["lg"],"examples":["lhd pass color acyclic --top m lg:dir","lhd pass abc --top m lg:dir --emit-dir lg:net","lhd pass liberty gensim sky130.lib --emit-dir lg:models","lhd pass semdiff --ref lg:gold --impl lg:opt --top adder"]})json");
    return 0;
  }
  if (name == "lnast-dump") {
    print_json_line(
        R"json({"schema_version":1,"name":"lnast-dump","description":"Round-trippable textual LNAST dump (the Lnast::dump text form), one <unit>.lnast per unit. A debug/test observable; the binary interchange form is ln:. The dumped tree is post-upass","direction":"out"})json");
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
  constexpr size_t kShown = 5;
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
    std::print("\noptions (--set {0}.flag=value; `lhd describe {0}.flag` for each listed flag option in `lhd list options {0}`):\n",
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
      "  synth      one-shot synthesis: compile -> reduce -> color synth -> abc tech-map -> opentimer STA (QoR + timing)\n"
      "               lhd synth cpu.prp --top Cpu --workdir W          # reports in W/synth/, incremental on re-run\n"
      "               lhd synth lg:cpu_lg --top Cpu --emit-dir lg:net --stats\n"
      "  sim        build + run a C++ simulation of a Pyrope design's `test` blocks (dynamic verify)\n"
      "               lhd sim foo.prp                  # build + run every test block\n"
      "               lhd sim foo.prp my_test --arg n=4\n"
      "  lec        logic equivalence (LEC): verilog:/pyrope:/ln:/lg: sides, --set formal.solver picks the\n"
      "               backend — cvc5 (default, in-process) | bitwuzla | lgyosys (yosys/lgcheck)\n"
      "               lhd lec --impl impl.prp --ref ref.v\n"
      "               lhd lec --impl net.v --ref gold.v --set formal.solver=lgyosys --top foo\n"
      "  formal     formal verification family: verify (assert/assume BMC from reset) | lec (= lhd lec)\n"
      "               lhd formal verify foo.prp --top foo --set formal.bound=12\n"
      "  scan       report each .prp file's import strings (the result's \"scan\" member)\n"
      "               lhd scan x.prp y.prp\n"
      "  tool       inspect ln:/lg: artifacts: cat | grep | diff | tree (stdout; --diag-fmt json)\n"
      "               lhd tool cat lg:dir --top m       # structured dump + attributes\n"
      "               lhd tool grep get_mask lg:dir     # filtered search (bare term -> any field)\n"
      "               lhd tool diff lg:before lg:after --attr color\n"
      "               lhd tool cat x.prp                # LNAST cat (was ln.cat)\n"
      "  pyrope     Pyrope developer tools: fmt (clang-format-like formatter) | lsp (the LSP server)\n"
      "               lhd pyrope fmt -i foo.prp         # reformat in place\n"
      "               lhd pyrope fmt foo.prp            # print formatted source to stdout\n"
      "               lhd pyrope lsp                    # Pyrope LSP server over stdio (JSON-RPC; .prp only)\n"
      "  pass       run one graph pass over lg: inputs: color <alg> | partition | abc | opentimer | liberty gensim | semdiff\n"
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
      "  (`--diag-fmt json` renders any help page as a machine record; pretty is the tty default)\n"
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
      "  --diag-fmt auto|json|pretty    result + diagnostic rendering (auto: pretty on a\n"
      "                                 terminal, json when piped/captured)\n"
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
        "  pass.upass + core/diag per buffer; ephemeral, no lgdb.\n"
        "\n"
        "flags:\n"
        "  none (stdin/stdout belong to the LSP protocol)\n"
        "\n"
        "examples:\n"
        "  lhd pyrope lsp\n");
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

// Both renderings consume the same structured record: adding or renaming a
// tool flag updates pretty and jsonl help together.
int help_tool(const std::string& sub) {
  const std::string name = sub.empty() ? "tool" : "tool " + sub;
  const auto*       doc  = tool_help_doc(name);
  if (doc == nullptr) {
    std::print(stderr, "lhd help: unknown tool subcommand '{}' (cat | grep | diff | tree)\n", sub);
    return 1;
  }
  render_help_pretty(*doc);
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
        "usage: lhd pass color [acyclic|cgen|synth|path|mincut|flat|reduce|clear] --top M lg:DIR\n"
        "  alg defaults to acyclic. The coloring is written back into the input lg:. With\n"
        "  --top the whole instance hierarchy is colored (each unique def once); set\n"
        "  pass.color.hier=false to limit it to the top def.\n"
        "\n"
        "flags:\n"
        "  --top M                    select the root module\n"
        "  --stats                    print the coloring report\n"
        "  --set pass.color.flag=value  pass options (listed below)\n"
        "\n"
        "algorithms:\n"
        "  acyclic  DAG cone partitions: every primary-output driver, fan-out>1, and dead\n"
        "           node seeds a region grown backward over its input cone (--set cutoff,\n"
        "           merge tune small-region merging)\n"
        "  cgen     one color per cone-sink signature — each primary output, plus one\n"
        "           shared flop/mem next-state bucket; logic feeding several sinks gets its\n"
        "           own id (the granularity inou.cgen.sim uses to break false comb loops)\n"
        "  synth    combinational clusters bounded by CUT nodes. A cut owns its own region\n"
        "           and is a barrier in both directions: it never inherits a neighbour's id\n"
        "           and never propagates its own, so a register cannot weld its din cone to\n"
        "           its enable/stall cone (nor its fan-out cones to each other). State\n"
        "           (flop/mem/latch/stateful sub) always cuts; synth mode also cuts mult/div\n"
        "           and >8-bit adders (--set synth_alg=pipe|synth; pipe = state only)\n"
        "  path     register-to-register regions: seed every flop/reg/mem and color its\n"
        "           backward+forward cone up to real (non-clk/rst) wire names; --set\n"
        "           instance=a,b instead seeds named nodes forward-only, bounded by the\n"
        "           instance-name prefix\n"
        "  mincut   two-way split of the def along a VieCut global minimum edge cut\n"
        "           (--set iters, mincut_alg, and the kernel --seed)\n"
        "  flat     one single color for the entire selected hierarchy — the coloring\n"
        "           equivalent of flattening the design (ignores pass.color.continuous)\n"
        "  reduce   NOT a labeling: finds repeated combinational cones (>= --set min_count\n"
        "           occurrences of >= --set min_nodes nodes, alpha-blind structural match;\n"
        "           constants may differ per site — a divergent literal becomes a const\n"
        "           input port, the unrolled-loop shape) and REWRITES the library in place:\n"
        "           each pattern becomes one shared `pat_<digest>` def instantiated at\n"
        "           every site. --set min_win requires an estimated per-site Verilog line\n"
        "           win before a cone is extracted (0 = extract on node count alone)\n"
        "  clear    remove any existing coloring\n"
        "\n"
        "--stats is the shared kernel flag — every pass that has a report reads it (also\n"
        "  pass.semdiff, and lec / formal verify via formal.stats). HERE (or --set\n"
        "  pass.color.stats=true) it reports what the coloring produced, on\n"
        "  stderr: partition count, max/min/avg/median size, singletons, how many land in\n"
        "  the 1k-5k band pass.abc likes, and uncolored nodes. A partition is one\n"
        "  (def, color) -- the unit pass.partition emits as `<def>__c<id>`. For `flat` it\n"
        "  also reports the size of the SINGLE region pass.abc will actually map, which is\n"
        "  every def times its instance count, not the per-def sum. Add pass.color.verbose\n"
        "  for the per-def table.\n"
        "\n"
        "examples:\n"
        "  lhd pass color acyclic --top m lg:dir\n"
        "  lhd pass color flat --top m lg:dir      # whole hierarchy -> one color\n"
        "  lhd pass color synth --top m lg:dir --set pass.color.synth_alg=pipe --stats\n");
    return print_options_section({"pass.color."});
  }
  if (sub == "partition") {
    std::print(
        "lhd pass partition — split a design into region -> module Subs (LEC-equivalent)\n"
        "\n"
        "usage: lhd pass partition --top M lg:DIR --emit-dir lg:OUT/\n"
        "  --emit-dir lg: (must differ from the input) receives the partitioned library.\n"
        "\n"
        "flags:\n"
        "  --top M                        select the root module\n"
        "  --emit-dir lg:OUT/             output library (must differ from the input)\n"
        "  --set pass.partition.flag=value  pass options (listed below)\n"
        "\n"
        "examples:\n"
        "  lhd pass partition --top m lg:dir --emit-dir lg:parts\n");
    return print_options_section({"pass.partition."});
  }
  if (sub == "single_edge") {
    std::print(
        "lhd pass single_edge — edge normalization: latches + negedge state -> posedge flops\n"
        "\n"
        "usage: lhd pass single_edge --top M lg:DIR --emit-dir lg:OUT/\n"
        "  --emit-dir lg: (must differ from the input) receives the normalized library.\n"
        "\n"
        "CONDITIONAL. The pass is SKIPPED ENTIRELY (not run as a no-op) unless the design\n"
        "  holds a Latch, a negedge flop, or more than one clock net. A plain posedge\n"
        "  single-clock design is never re-emitted and cannot change a verdict.\n"
        "\n"
        "SCOPE. Verification and simulation only — never on the synthesis path: slot\n"
        "  enables and a phase divider cost QoR, and the netlist handed to ABC has to\n"
        "  contain a real always_latch. `lhd formal verify` and `lhd lec` run it\n"
        "  automatically, MITER-WIDE (if either side needs it, both sides get it —\n"
        "  otherwise the two are compared in different time bases).\n"
        "\n"
        "FAIL CLOSED. A stateful instance, a second clock domain with no known ratio, a\n"
        "  yosys raw-D/EN latch or a coincident-edge latch/flop pair DECLINES the whole\n"
        "  design with a named diagnostic. A partial lowering is a silent full-cycle\n"
        "  error, so half-transforming is never an option.\n"
        "\n"
        "flags:\n"
        "  --top M                         select the root module\n"
        "  --emit-dir lg:OUT/              output library (must differ from the input)\n"
        "  --set pass.single_edge.flag=value  pass options (listed below)\n"
        "\n"
        "examples:\n"
        "  lhd pass single_edge --top m lg:dir --emit-dir lg:norm\n");
    return print_options_section({"pass.single_edge."});
  }
  if (sub == "abc") {
    std::print(
        "lhd pass abc — combinational ABC tech-map (bit-blast -> AIG -> sky130 blackboxes)\n"
        "\n"
        "usage: lhd pass abc --top M lg:DIR --emit-dir lg:OUT/\n"
        "  --emit-dir lg: (must differ from the input) receives the mapped netlist.\n"
        "\n"
        "memory admission:\n"
        "  A region is bit-blasted into ABC, so a whole-design region costs millions of\n"
        "  gates and several network forms at once — a flat XSCore run reached 221 GB on a\n"
        "  64 GiB host and was killed by the OS. pass.abc samples its own RSS while it\n"
        "  translates and refuses a region that will not fit, before running any synthesis\n"
        "  command and without emitting a partial result (exit nonzero). The budget is\n"
        "  PHYSICAL RAM minus max(2 GiB, 20%) — never swap, because swapping an oversize\n"
        "  run does not make it succeed, it makes it take the machine down slowly.\n"
        "  --set pass.abc.memory_budget_mb=N pins the ceiling (reproducible hosts, CI).\n"
        "  --set pass.abc.allow_oversize=true disables the guard.\n"
        "  Refused? `lhd pass color <alg> --top M lg:DIR --stats` sizes the regions first.\n"
        "\n"
        "progress:\n"
        "  Each completed color immediately flushes one `PROGRESS pass.abc` line with a\n"
        "  monotonic completed count, region/color identity, resynth/cache state, QoR,\n"
        "  elapsed milliseconds, and memory. Verbose per-stage ABC chatter remains off.\n"
        "\n"
        "flags:\n"
        "  --top M                  select the module to map\n"
        "  --emit-dir lg:OUT/       output library (must differ from the input)\n"
        "  --stats                  add one QoR row per (definition, color); resynth=1|0\n"
        "  --set pass.abc.flag=value  pass options (listed below)\n"
        "\n"
        "examples:\n"
        "  lhd pass abc --top m lg:dir --emit-dir lg:net\n");
    return print_options_section({"pass.abc."});
  }
  if (sub == "opentimer") {
    std::print(
        "lhd pass opentimer — OpenTimer STA on a pass.abc tech-mapped module\n"
        "\n"
        "usage: lhd pass opentimer --top M lg:DIR <cells.lib> [file.sdc file.spef]\n"
        "  Reports the critical path of ONE tech-mapped module machine-readably (the\n"
        "  accurate frequency oracle of the 2opt-freq loop). Timing files are POSITIONAL\n"
        "  (like `pass liberty gensim`): 1-2 Liberty files (.lib; a 2nd = min corner) plus\n"
        "  optional .sdc / .spef — not a --set option.\n"
        "\n"
        "  One OpenTimer design per run: --top picks the def out of the netlist library.\n"
        "  Time a region module (<mod>__c<N>), a flat map, or a hierarchical top: by default\n"
        "  (hier=true) the instance hierarchy is structurally flattened and timed as one\n"
        "  module, so the critical path can span module boundaries. Flops and memories are\n"
        "  path boundaries (kept native by pass.abc, zeroed as virtual inputs), not Liberty\n"
        "  cells.\n"
        "\n"
        "flags:\n"
        "  --top M                        select the tech-mapped module\n"
        "  --workdir DIR                  report/build workspace\n"
        "  --stats                        add one timing row per mapped color\n"
        "  --set pass.opentimer.flag=value  pass options (listed below)\n"
        "\n"
        "the report (timing.json / result envelope \"qor\" member):\n"
        "  max_delay (worst MAX-corner gate arrival, library time units), the critical pin,\n"
        "  the 10 worst endpoints, each `src`-attributed back to the pre-synth RTL line.\n"
        "  With --stats, each color row has module/color, cells, max_arrival, and\n"
        "  resynth=1|0 inherited from the pass.abc invocation that emitted the netlist.\n"
        "\n"
        "examples:\n"
        "  lhd pass abc --top m lg:g --emit-dir lg:net\n"
        "  lhd pass opentimer --top m__c0 lg:net cells.lib --workdir W\n");
    return print_options_section({"pass.opentimer."});
  }
  if (sub == "liberty") {
    std::print(
        "lhd pass liberty gensim <file.lib> — Liberty cells -> LGraph simulation models\n"
        "\n"
        "usage: lhd pass liberty gensim <file.lib> --emit-dir lg:OUT/\n"
        "  Takes a Liberty FILE (not an lg: input); --emit-dir lg: receives the model library.\n"
        "\n"
        "flags:\n"
        "  --emit-dir lg:OUT/              output model library\n"
        "  --set pass.liberty.flag=value  pass options (listed below)\n"
        "\n"
        "examples:\n"
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
        "  --top T        --ref-top T   --impl-top T   (T = full `file.entity` name, or the\n"
        "                 bare entity when unique — resolves with a top-entity-fallback warning)\n"
        "  --stats        aggregate node/register/memory match report\n"
        "  --set pass.semdiff.flag=value  pass options (listed below)\n"
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
    std::print(stderr,
               "lhd help: unknown pass subcommand '{}' (color | partition | single_edge | abc | opentimer | liberty | semdiff)\n",
               sub);
    return 1;
  }
  std::print(
      "lhd pass — run one graph pass over an lg: library input\n"
      "\n"
      "usage: lhd pass <subcommand> [args] [--top M] lg:DIR [--emit-dir lg:OUT/]\n"
      "\n"
      "subcommands (run `lhd pass <subcommand> --help` for each one's --set options):\n"
      "  color <alg>          acyclic|synth|path|mincut|flat coloring; reduce = repeated-\n"
      "                       cone extraction into shared pat_* defs (all in place)\n"
      "  partition            region -> module Sub split (-> new lg:)\n"
      "  abc                  combinational ABC tech-map (-> new lg:)\n"
      "  opentimer            OpenTimer STA on a tech-mapped module (-> timing.json)\n"
      "  liberty gensim FILE  Liberty -> simulation models (-> new lg:)\n"
      "  semdiff              structural diff/match of two lg: (--ref/--impl; marked in place)\n"
      "\n"
      "examples:\n"
      "  lhd pass color acyclic --top m lg:dir\n"
      "  lhd pass partition --top m lg:dir --emit-dir lg:parts\n"
      "  lhd pass abc --top m lg:dir --emit-dir lg:net\n"
      "  lhd pass opentimer --top m__c0 lg:net sky130.lib --workdir W\n"
      "  lhd pass liberty gensim sky130.lib --emit-dir lg:models\n"
      "  lhd pass semdiff --ref lg:gold --impl lg:opt --top adder\n");
  return 0;
}

// ---- `--diag-fmt json` help: the machine record for a help page -------------
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
      R"json({{"schema_version":1,"name":"lhd","version":"{}","description":"LiveHD stateless CLI kernel: one hermetic invocation per flow (declared inputs + config -> declared outputs + exit code); drives the registered pass/inou (EPRP) methods via argv","commands":[{{"name":"compile","summary":"sources and/or ln:/lg: IR -> ln:/lg:/verilog/pyrope (front-end + elaborate + synth)"}},{{"name":"synth","summary":"one-shot synthesis: compile -> color synth -> abc tech-map -> opentimer STA; QoR + timing report"}},{{"name":"sim","summary":"build + run a C++ simulation of a Pyrope design's test blocks (dynamic verify)"}},{{"name":"lec","summary":"logic equivalence check: prove_equal(ref, impl); --set formal.solver = cvc5|bitwuzla|lgyosys"}},{{"name":"formal","summary":"formal verification family: verify (assert/assume BMC) | lec (= lhd lec)"}},{{"name":"scan","summary":"report each .prp file's import strings"}},{{"name":"tool","summary":"inspect ln:/lg: artifacts: cat | grep | diff | tree"}},{{"name":"pyrope","summary":"Pyrope developer tools: fmt | lsp"}},{{"name":"pass","summary":"run one graph pass over lg: inputs: color | partition | abc | opentimer | liberty | semdiff"}},{{"name":"list","summary":"enumerate the CLI vocabulary: steps|recipes|emit-kinds|error-classes|options|log-channels"}},{{"name":"describe","summary":"one item's full record as JSON"}},{{"name":"version","summary":"print the tool version"}},{{"name":"help","summary":"per-command help: lhd help <command> (== lhd <command> --help)"}}],"examples":["lhd compile x.prp --emit verilog:net.v","lhd lec --impl impl.prp --ref ref.v","lhd help compile"]}})json",
      kVersion);
}

std::string json_version() {
  return std::format(
      R"json({{"schema_version":1,"name":"version","version":"{}","description":"Print the tool version (also lhd --version)","examples":["lhd version"]}})json",
      kVersion);
}

constexpr std::string_view kJsonPyropeOverview
    = R"json({"schema_version":1,"name":"pyrope","description":"Pyrope developer tools (language-adjacent, not the compile/synth flow)","subcommands":[{"name":"fmt","summary":"format Pyrope source (clang-format-like): -i in place, else stdout"},{"name":"lsp","summary":"the Pyrope LSP server over stdio (JSON-RPC; .prp only)"}],"examples":["lhd pyrope fmt -i foo.prp","lhd pyrope lsp"]})json";

constexpr std::string_view kJsonPassColor
    = R"json({"schema_version":1,"name":"pass color","description":"Node coloring over an lg: library, in place: acyclic|cgen|synth|path|mincut|flat|reduce|clear (alg defaults to acyclic). flat gives the whole --top hierarchy one color (the flatten equivalent). The coloring is written back into the input lg:","args":{"required":[{"name":"alg","type":"enum","values":["acyclic","cgen","synth","path","mincut","flat","reduce","clear"],"default":"acyclic","positional":true},{"name":"inputs","type":"lg:DIR","positional":true}],"optional":[{"name":"top","type":"string"},{"name":"set","type":"pass.color.flag=value","repeatable":true}]},"inputs":["lg"],"outputs":["lg"],"examples":["lhd pass color acyclic --top m lg:dir","lhd pass color flat --top m lg:dir"]})json";

constexpr std::string_view kJsonPassPartition
    = R"json({"schema_version":1,"name":"pass partition","description":"Split a design into region -> module Subs (LEC-equivalent). --emit-dir lg: (must differ from the input) receives the partitioned library","args":{"required":[{"name":"inputs","type":"lg:DIR","positional":true}],"optional":[{"name":"top","type":"string"},{"name":"emit-dir","type":"lg:DIR/"},{"name":"set","type":"pass.partition.flag=value","repeatable":true}]},"inputs":["lg"],"outputs":["lg"],"examples":["lhd pass partition --top m lg:dir --emit-dir lg:parts"]})json";

constexpr std::string_view kJsonPassSingleEdge
    = R"json({"schema_version":1,"name":"pass single_edge","description":"Edge normalization (2f-latch M8): rewrite latches and negedge state into plain posedge flops, carrying the original timing with a synthesized phase divider plus per-flop slot enables. CONDITIONAL - a design with no latch, no negedge flop and one clock net is skipped entirely, not run as a no-op. Verification and simulation ONLY: never on the synthesis path, since slot enables cost QoR and the netlist handed to ABC must still contain a real always_latch. --emit-dir lg: (must differ from the input) receives the normalized library","args":{"required":[{"name":"inputs","type":"lg:DIR","positional":true}],"optional":[{"name":"top","type":"string"},{"name":"emit-dir","type":"lg:DIR/"},{"name":"set","type":"pass.single_edge.flag=value","repeatable":true}]},"inputs":["lg"],"outputs":["lg"],"examples":["lhd pass single_edge --top m lg:dir --emit-dir lg:norm"]})json";

constexpr std::string_view kJsonPassAbc
    = R"json({"schema_version":1,"name":"pass abc","description":"Combinational ABC tech-map: bit-blast -> AIG -> sky130 blackboxes. --emit-dir lg: (must differ from the input) receives the mapped netlist. --stats adds one QoR row per mapped color with resynth=1|0","args":{"required":[{"name":"inputs","type":"lg:DIR","positional":true}],"optional":[{"name":"top","type":"string"},{"name":"emit-dir","type":"lg:DIR/"},{"name":"stats","type":"flag"},{"name":"set","type":"pass.abc.flag=value","repeatable":true}]},"inputs":["lg"],"outputs":["lg"],"examples":["lhd pass abc --top m lg:dir --emit-dir lg:net --stats"]})json";

constexpr std::string_view kJsonPassOpentimer
    = R"json({"schema_version":1,"name":"pass opentimer","description":"OpenTimer static timing analysis on ONE pass.abc tech-mapped module: reports the critical path (max_delay, critical pin, worst endpoints, source-attributed) as timing.json and the result envelope 'qor' member. --stats adds one timing row per mapped color with resynth=1|0. Timing files are POSITIONAL (1-2 Liberty .lib, a 2nd = min corner, plus optional .sdc/.spef). --top picks the def (time a <mod>__c<N> region or a flat map); flops/memories are zeroed path boundaries. hier defaults true: a --top that instantiates sub-modules is structurally flattened and timed as ONE design (--set pass.opentimer.hier=false rejects non-Liberty Subs instead: one flat module per run)","args":{"required":[{"name":"files","type":"path (.lib[,.sdc,.spef])","positional":true,"repeatable":true}],"optional":[{"name":"top","type":"string"},{"name":"workdir","type":"path"},{"name":"stats","type":"flag"},{"name":"set","type":"pass.opentimer.flag=value","repeatable":true}]},"inputs":["lg"],"outputs":["json"],"examples":["lhd pass abc --top m lg:g --emit-dir lg:net","lhd pass opentimer --top m lg:net cells.lib --workdir W --stats"]})json";

constexpr std::string_view kJsonPassLiberty
    = R"json({"schema_version":1,"name":"pass liberty","description":"Liberty cells -> LGraph simulation models (gensim). Takes a Liberty FILE (not an lg: input); --emit-dir lg: receives the model library","args":{"required":[{"name":"subcommand","type":"enum","values":["gensim"],"positional":true},{"name":"file","type":"path (.lib)","positional":true}],"optional":[{"name":"emit-dir","type":"lg:DIR/"},{"name":"set","type":"pass.liberty.flag=value","repeatable":true}]},"inputs":[],"outputs":["lg"],"examples":["lhd pass liberty gensim sky130.lib --emit-dir lg:models"]})json";

constexpr std::string_view kJsonSimCommand
    = R"json({"schema_version":1,"name":"sim","description":"Build and run a C++ simulation of a Pyrope design's `test` blocks (dynamic verify): the DUT lowers to a Slop<N> struct (inou.cgen.sim, over ../hlop) and ONE C++ driver holding every test block is host-compiled and run — each test's asserts are checked by running, not formally. Positionals are the .prp source(s) — the LAST holds the `test` blocks — plus, as in `lhd compile`, any ln:DIR (pre-elaborated units) or lg:DIR (pre-compiled libraries) the testbench imports, so a design compiled once simulates without re-reading its sources. A lone non-path positional selects a single test; each `test name(params)` parameter becomes a --<name> flag on the generated binary","args":{"required":[{"name":"file","type":"path (.prp)","positional":true}],"optional":[{"name":"ir-inputs","type":"ln:DIR|lg:DIR","positional":true,"repeatable":true},{"name":"test","type":"string","positional":true},{"name":"arg","type":"key=value","repeatable":true},{"name":"seed","type":"int"},{"name":"list-tests","type":"flag"},{"name":"setup-only","type":"flag"},{"name":"run-only","type":"flag"},{"name":"workdir","type":"path"},{"name":"result-json","type":"path"},{"name":"restart-cycle","type":"int"},{"name":"vcd-from","type":"int"},{"name":"vcd-to","type":"int"},{"name":"vcd-on-fail","type":"flag"},{"name":"vcd-fail-window","type":"int"},{"name":"list-signals","type":"flag"},{"name":"probe","type":"SIG,..."},{"name":"probe-from","type":"int"},{"name":"probe-to","type":"int"},{"name":"break-when","type":"SIG OP VALUE"},{"name":"query","type":"path|-|json"},{"name":"set","type":"sim.flag=value","repeatable":true}]},"inputs":["pyrope","ln","lg"],"outputs":["sim"],"examples":["lhd sim foo.prp","lhd sim foo.prp --list-tests","lhd sim foo.prp my_test --arg n=4","lhd sim dut.prp tb.prp","lhd sim ln:dut_lns/ tb.prp","lhd sim lg:dut_lgs/ tb.prp","lhd sim foo.prp --set sim.vcd=true","lhd sim foo.prp my_test --query q.json --result-json r.json"]})json";

constexpr std::string_view kJsonList
    = R"json({"schema_version":1,"name":"list","description":"Enumerate the CLI vocabulary as one JSON line (options also honors --diag-fmt pretty). Patterns: steps | recipes | emit-kinds | error-classes | options [REGEX] | log-channels","args":{"required":[{"name":"pattern","type":"enum","values":["steps","recipes","emit-kinds","error-classes","options","log-channels"],"positional":true}],"optional":[{"name":"regex","type":"string (options name filter)","positional":true}]},"examples":["lhd list options 'cgen\\..*'","lhd list recipes","lhd list log-channels"]})json";

constexpr std::string_view kJsonDescribe
    = R"json({"schema_version":1,"name":"describe","description":"One item's full record as JSON (the machine face of help; pretty prose for a pass.flag option). Accepts a command, recipe:NAME, emit-kind, pass.flag, dump, or config","args":{"required":[{"name":"name","type":"command | recipe:NAME | emit-kind | pass.flag | dump | config","positional":true}]},"examples":["lhd describe compile.cgen.srcmap","lhd describe lec"]})json";

// Route a `--diag-fmt json` help page to its JSON record. `topic`/`sub` are the
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
  if (topic == "synth") {
    print_json_line(kJsonSynthCommand);
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
  if (topic == "tool") {
    if (sub.empty()) {
      return describe_as("tool");
    }
    if (sub == "cat" || sub == "grep" || sub == "diff" || sub == "tree") {
      return describe_as("tool " + sub);
    }
    std::print(stderr, "lhd help: unknown tool subcommand '{}' (cat | grep | diff | tree)\n", sub);
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
    if (sub == "single_edge") {
      print_json_line(kJsonPassSingleEdge);
      return 0;
    }
    if (sub == "abc") {
      print_json_line(kJsonPassAbc);
      return 0;
    }
    if (sub == "opentimer") {
      print_json_line(kJsonPassOpentimer);
      return 0;
    }
    if (sub == "liberty") {
      print_json_line(kJsonPassLiberty);
      return 0;
    }
    std::print(stderr,
               "lhd help: unknown pass subcommand '{}' (color | partition | single_edge | abc | opentimer | liberty | semdiff)\n",
               sub);
    return 1;
  }
  // `formal` is a family: the record follows the SUBCOMMAND, so
  // `lhd formal verify --help --diag-fmt json` describes verify, not the family
  // (`formal lec` was already folded to the `lec` topic above).
  if (topic == "formal") {
    if (sub.empty()) {
      return describe_as("formal");
    }
    if (sub == "verify") {
      return describe_as("formal verify");
    }
    std::print(stderr, "lhd help: unknown formal subcommand '{}' (verify | lec)\n", sub);
    return 1;
  }
  // compile / lec / scan, plus every non-command describe topic
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

  // --diag-fmt json -> the machine record (mirrors `list`/`describe`); pretty
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
        "lhd lec — logic equivalence (LEC): prove_equal(ref, impl)   [= lhd formal lec]\n"
        "\n"
        "usage: lhd lec --impl KIND:PATH --ref KIND:PATH [formal-block.prp ...] [flags]\n"
        "  Sides may be verilog:/pyrope:/ln:/lg: or a bare .v/.sv/.prp path (kind inferred).\n"
        "  Each side is loaded/elaborated to LGraphs; verilog elaborates through --reader\n"
        "  (default slang, the direct SV->LNAST front-end; --reader yosys-slang|yosys-verilog\n"
        "  overrides). The --set formal.solver knob selects the backend:\n"
        "    cvc5     in-process SMT (default)\n"
        "    bitwuzla in-process SMT\n"
        "    lgyosys  inou/yosys/lgcheck (the former `lhd check`; reads Verilog directly,\n"
        "             the path for gate-level / yosys-origin netlists)\n"
        "\n"
        "  lec asks \"are these two designs the same function?\". To prove properties OF one\n"
        "  design (assert/assume, formal blocks), that is `lhd formal verify` — lec takes no\n"
        "  formal-block sidecar, because a block is an independent test while lec has the\n"
        "  single obligation impl == ref.\n"
        "\n"
        "flags:\n"
        "  --impl KIND:PATH   --ref KIND:PATH\n"
        "  --top T            --impl-top T   --ref-top T   (T = full `file.entity` name, or\n"
        "                     the bare entity when unique — a top-entity-fallback warning notes it)\n"
        "  --reader R         slang | yosys-slang | yosys-verilog (default slang)\n"
        "  --lib lg:DIR       cell-model libraries for instantiated cells (repeatable)\n"
        "  --collapse DEF     treat DEF as already-proven: force the sound black-box path (repeatable)\n"
        "  --trust DEF        ASSUME DEF equivalent without proving it — disclosed, never silent\n"
        "                     (the escape hatch for a cell the encoder cannot model) (repeatable)\n"
        "  --stats            (= --set formal.stats=true) cvc5 solve insight, off by default\n"
        "  --set formal.flag=value   engine knobs (the options block below; legacy lec.* accepted)\n"
        "\n"
        "  Extra .prp files supply impl-side formal helpers. Internal/output facts are\n"
        "  proven unbounded before use; input-only assumes are environment constraints;\n"
        "  assume_nocheck/assume_nocheck_formal is disclosed (the _formal spelling also\n"
        "  warns); assume_nocheck_synth is ignored.\n"
        "\n"
        "  --stats reports what the cvc5 solve actually did: problem size (atoms, clause\n"
        "  literals), conflicts (= learned clauses), decisions, propagations, restarts,\n"
        "  theory lemmas, resource units and timings, plus the hottest defs by conflict\n"
        "  count. It also registers a cvc5 plugin to see learned-clause CONTENT, which makes\n"
        "  the solve ~8x SLOWER — so it is a DIAGNOSIS tool for a solve that is too slow or\n"
        "  too big, never something to leave on, and never a way to time a run. That slowdown\n"
        "  can CHANGE THE VERDICT: a proof that fits formal.timeout without it may time out\n"
        "  with it and return UNKNOWN, which under the default formal.strict=true exits\n"
        "  non-zero. Raise formal.timeout when diagnosing a run that has to keep passing.\n"
        "  `no cvc5 query ran` is a normal outcome (semdiff, the verdict cache and abc cone\n"
        "  decomposition settle defs without calling cvc5). `lhd formal verify` also writes\n"
        "  the same numbers into formal_report.json; `lhd lec` prints them only.\n"
        "\n"
        "examples:\n"
        "  lhd lec --impl impl.prp --ref ref.v            # the common case\n"
        "  lhd lec --impl net.v --ref gold.v --top foo    # name the top explicitly\n"
        "  lhd lec --impl lg:impl/ --ref lg:ref/ --top foo --set formal.engine=ind\n"
        "  lhd lec --impl net.v --ref gold.v --set formal.solver=lgyosys --top foo\n"
        "  lhd lec --impl impl.prp --ref ref.v --stats    # why is this solve slow? (~8x)\n");
    return print_options_section({"formal."});
  }
  // `lhd formal --help` is the FAMILY page: what the two subcommands are and
  // which one to reach for. The detail lives on the subcommand pages
  // (`formal verify --help` / `formal lec --help`) — one page per thing you can
  // actually run, so neither dumps the other's flags.
  if (topic == "formal" && sub.empty()) {
    std::print(
        "lhd formal — formal verification family (2f-verify)\n"
        "\n"
        "usage: lhd formal <subcommand> [args] [flags]\n"
        "\n"
        "subcommands:\n"
        "  verify   prove ONE design's assert / assert_always / assume obligations by\n"
        "           BMC from reset, per obligation and per cycle. Extra .prp positionals\n"
        "           are formal-block sidecars, each block an independent test\n"
        "             lhd formal verify <design> [sidecar.prp ...] [BLOCK] [flags]\n"
        "  lec      logic equivalence: prove_equal(ref, impl). An alias of `lhd lec`\n"
        "             lhd formal lec --impl KIND:PATH --ref KIND:PATH [flags]\n"
        "\n"
        "which one: `verify` answers \"does this design satisfy the properties I wrote?\"\n"
        "(one design + its asserts/assumes); `lec` answers \"are these two designs the\n"
        "same function?\" (two designs, no properties). Both share the --set formal.*\n"
        "knob namespace (bound, timeout, solver, strict, ...; legacy lec.* accepted).\n"
        "\n"
        "examples:\n"
        "  lhd formal verify foo.prp --top foo            # prove foo's own obligations\n"
        "  lhd formal verify ALU.prp ALU.verify.prp       # ...plus a sidecar's blocks\n"
        "  lhd formal lec --impl impl.prp --ref ref.v     # equivalence instead\n"
        "\n"
        "full help:\n"
        "  lhd formal verify --help\n"
        "  lhd formal lec --help    (= lhd lec --help)\n");
    return 0;
  }
  if (topic == "formal" && sub != "verify") {
    std::print(stderr, "lhd help: unknown formal subcommand '{}' (verify | lec)\n", sub);
    return 1;
  }
  if (topic == "formal") {  // sub == "verify"
    std::print(
        "lhd formal verify — prove one design's assert/assume obligations by BMC from reset\n"
        "\n"
        "usage: lhd formal verify <design> [sidecar.prp ...] [BLOCK] [flags]\n"
        "  <design> is a bare .prp/.v/.sv path, --impl KIND:PATH, or lg:DIR. Every EXTRA\n"
        "  .prp positional is a formal-block sidecar (never compiled as design), and a\n"
        "  lone NON-path positional selects one block — the same shape `lhd sim` uses.\n"
        "\n"
        "  Each obligation is checked per cycle as its own solver query, every proven\n"
        "  fact immediately prunes the search for the rest (frontier assumes), and a\n"
        "  timeout costs one obligation at one cycle — so the run reports a per-assert\n"
        "  verdict table, not a single verdict:\n"
        "    PROVEN to cycle k   BOUNDED (no violation within the unrolled window)\n"
        "    REFUTED at cycle k  a REACHABLE violation + the per-cycle input trace (fails)\n"
        "    UNKNOWN             solver gave up / blackbox artifact / contradictory assumes\n"
        "                        (FAILS the run: an undecided check proved nothing, so it\n"
        "                        must not exit 0. --set formal.strict=false = warning)\n"
        "\n"
        "  every `assume` is a PROOF OBLIGATION (prove-then-use): CHECKED as an assert\n"
        "  first, and only a proven cycle's fact constrains later obligations. A refuted\n"
        "  assume fails the run (over free primary inputs a constraint like\n"
        "  `assume(op == 7)` can never be proven — that refute is the honest verdict);\n"
        "  an unproven one is NOT used. With --workdir a proven assume-check is cached\n"
        "  and skipped on warm re-runs. `assume_nocheck` (formal blocks) is the explicit\n"
        "  spelling for a free environment constraint: assumed WITHOUT check, in force\n"
        "  at every cycle, disclosed — verdicts are conditional on it.\n"
        "  assume_nocheck_synth is invisible to verify (a synthesis-only don't-care).\n"
        "\n"
        "  formal BLOCKS: a sidecar's `formal name.dotted {{ ... }}` blocks are the design's\n"
        "  test units — the Pyrope design file is a block source too, so one file may hold\n"
        "  both. Each block is INDEPENDENT: its assumes scope to itself, so two blocks may\n"
        "  carry mutually-exclusive assumes and both still prove.\n"
        "\n"
        "flags:\n"
        "  --top T              top module (full `file.entity`, or the bare entity when\n"
        "                       unique — a top-entity-fallback warning notes it)\n"
        "  --list-tests         list the formal blocks, then exit — a pure parse: no design\n"
        "                       load, no solver (JSON, or a human listing per --diag-fmt).\n"
        "                       Same envelope as `lhd sim --list-tests`; `params` is always\n"
        "                       [] because a formal block takes no runtime arguments\n"
        "  <BLOCK>              (positional) run ONE block by its dotted name; fnmatch, so a\n"
        "                       glob selects a family. A selector that matches nothing FAILS\n"
        "                       rather than silently proving only the design's obligations\n"
        "  --formal GLOB        the same filter spelled as a flag (passing both is an error)\n"
        "  --impl KIND:PATH     the design, when not given as a positional (--impl-top T)\n"
        "  --lib lg:DIR         cell-model libraries for instantiated cells (repeatable)\n"
        "  --workdir DIR        keep formal_report.json + the refutation artifacts here\n"
        "  --stats              (= --set formal.stats=true) cvc5 solve insight, off by default\n"
        "  --set formal.flag=value   engine knobs (the options block below; legacy lec.* accepted)\n"
        "\n"
        "  machine-readable feedback (agents): EVERY run writes formal_report.json into the\n"
        "  workdir (per-obligation verdicts/cycles/solve_ms, assume classes, the structured\n"
        "  spec_mining_timeout core, witness artifact paths) — pass --workdir to keep it; a\n"
        "  REFUTED run adds simfail_<formal-test>.prp/.json (+ VCD replay). Knobs\n"
        "  formal.simfail, formal.simfail_run, and formal.report. With\n"
        "  formal.spec_mining_timeout set, a stuck run also MINES invariants (base-proven +\n"
        "  induction-surviving) into a paste-ready formal_mined.prp + the report's mined[];\n"
        "  formal.mine=speculative adds step-dropped bounded candidates.\n"
        "\n"
        "  --stats / formal.stats reports what the solve actually did — problem size,\n"
        "  conflicts (= learned clauses), decisions, propagations, restarts, theory lemmas,\n"
        "  resource units, timings — at a ~8x SLOWER solve, so use it to diagnose a slow\n"
        "  proof, never to time one. That slowdown can CHANGE THE VERDICT: a proof that fits\n"
        "  formal.timeout without it may time out with it and return UNKNOWN, which under\n"
        "  the default formal.strict=true exits non-zero. Raise formal.timeout when\n"
        "  diagnosing a run that has to keep passing.\n"
        "\n"
        "examples:\n"
        "  lhd formal verify foo.prp --top foo                      # prove foo's obligations\n"
        "  lhd formal verify ALU.prp ALU.verify.prp --list-tests     # enumerate the blocks\n"
        "  lhd formal verify ALU.prp ALU.verify.prp alu.addw --top ALU   # prove ONE block\n"
        "  lhd formal verify ALU.prp ALU.verify.prp --formal 'alu.*'     # ...or a family\n"
        "  lhd formal verify foo.prp --top foo --set formal.bound=12     # unroll deeper\n"
        "  lhd formal verify net.v --set formal.timeout=60               # per-query budget\n"
        "  lhd formal verify design.prp --set formal.phase=full          # reset window too\n"
        "  lhd formal verify foo.prp --workdir w/    # keep report + simfail_<test> artifacts\n");
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
        "flags:\n"
        "  --result-json PATH  write the result envelope to PATH\n"
        "\n"
        "examples:\n"
        "  lhd scan f1.prp f2.prp\n");
    return 0;
  }
  if (topic == "tool") {
    return help_tool(sub);
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
    std::print("{}",
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
               "  --restart-cycle N    resume from the nearest checkpoint <= cycle N (debug a long run)\n"
               "  --vcd-from Y [--vcd-to Z]  trace a VCD over just cycles [Y, Z] (restarts near Y; implies VCD)\n"
               "  --vcd-on-fail [--vcd-fail-window N]  on an assert fire, auto-dump a VCD of the last N cycles\n"
               "  --list-signals       list the observable scalar signals (hierarchical names) as JSON, then exit\n"
               "  --probe SIG,... [--probe-from A --probe-to B]  per-cycle JSON trajectory of SIG (no re-instrumenting)\n"
               "  --break-when 'SIG OP V'  report the first cycle a `SIG >|<|>=|<=|==|!= VALUE|SIG` condition holds\n"
               "  --query FILE|-|{...} batched JSON questions about the run, answered from ONE replay; the\n"
               "                       answers become the result envelope's `query` member. The request is\n"
               "                       {\"schema_version\":1,\"kind\":\"sim_query\",\"queries\":[{\"id\":..,\"op\":..}]}\n"
               "                       with ops signals|value|values|changes|next_change|find|snapshot|diff;\n"
               "                       a query names a signal (or a {scope|glob|regex|kind} selector) and a\n"
               "                       time {\"cycle\":N}. Unknown ops/fields/phases and bad ranges are usage\n"
               "                       errors; a bad SIGNAL is an in-band per-query error, so one typo never\n"
               "                       erases the other answers. Not combinable with --restart-cycle/--vcd-*\n"
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
               "  lhd sim foo.prp my_test --result-json r.json \\\n"
               "      --query '{\"schema_version\":1,\"kind\":\"sim_query\",\"queries\":[\n"
               "        {\"id\":\"pc\",\"op\":\"value\",\"signal\":\"cpu.fetch.pc\",\"at\":{\"cycle\":42}},\n"
               "        {\"id\":\"regs\",\"op\":\"signals\",\"scope\":\"cpu.rf\"}]}'\n"
               "                                                # one replay answers both; `regs` needs none\n"
               "  lhd sim foo.prp --setup-only --workdir build/  # generate, then build it yourself\n"
               "  ./build/sim/drv.bin --test my_test --seed 7    # run the built driver directly\n"
               "  ./build/sim/drv.bin --list-tests               # list tests from the built binary\n"
               "  lhd sim foo.prp --run-only --workdir build/    # rebuild/run a prior --setup-only\n");
    return print_options_section({"sim."});
  }
  if (topic == "synth") {
    std::print("{}",
               "lhd synth — one-shot synthesis: compile -> reduce -> color synth -> abc tech-map -> opentimer STA\n"
               "\n"
               "usage: lhd synth [--top M] [--workdir W] <file.prp|file.sv|lg:DIR|ln:DIR ...> [--emit-dir lg:NET] [--stats]\n"
               "  The five manual steps over ONE in-memory design:\n"
               "    lhd compile X --top M --emit-dir lg:L        (sources, ln:, lg:, mixed — as `lhd compile`)\n"
               "    lhd pass color reduce --top M lg:L             (synth.reduce=true; repeated small cones)\n"
               "    lhd pass color synth --top M lg:L              (always `synth`: per-(def,color) regions)\n"
               "    lhd pass abc --top M lg:L --emit-dir lg:NET    (ABC tech-map to the Liberty cells)\n"
               "    lhd pass opentimer --top M lg:NET cells.lib    (STA; synth.opentimer=false skips it)\n"
               "  --top is resolved once (a bare entity name is enough; a sole module needs none), the\n"
               "  coloring never touches an lg: input (it happens in memory), and ONE Liberty feeds both\n"
               "  abc and opentimer. Other colorings are the manual steps, not a synth knob.\n"
               "\n"
               "  --workdir is optional. With one, <workdir>/synth/ keeps:\n"
               "    lg/          the compiled design   (`lhd lec --ref lg:W/synth/lg ...` pairs against it)\n"
               "    net/         the mapped netlist    (--emit-dir lg: relocates it instead)\n"
               "    qor.json     pass.abc QoR          timing.json  pass.opentimer critical path\n"
               "  and the incremental tiers are live: the compile cache and <workdir>/abc_cache reuse\n"
               "  everything unchanged since the last run (lhd.incremental, default true; =false is an\n"
               "  honest cold run with byte-identical outputs). Without --workdir the flow runs in a\n"
               "  scratch dir: only --emit-dir lg:/verilog:/report: and the printed report survive.\n"
               "\n"
               "report:\n"
               "  default   one abc-map line (regions, gates, area, max delay) + the STA critical path\n"
               "  --stats   plus one row per (definition, color) from abc and from opentimer (resynth=1|0)\n"
               "  --result-json: the envelope's `qor` member is {kind:\"synth\", abc:{...}, sta:{...}}, plus\n"
               "  `phases` (per-step ms) and `incremental.{compile,abc}` (hits/misses/ms per reuse tier);\n"
               "  --stats also prints those as `incremental[stats]:` + `phases[stats]:` rows\n"
               "\n"
               "flags:\n"
               "  --top M                    top module (bare entity or file.entity)\n"
               "  --workdir W                keep intermediates + reports under W/synth/, enable incremental reuse\n"
               "  --emit-dir lg:DIR/         the mapped netlist library (instead of W/synth/net)\n"
               "  --emit-dir report:DIR/     copy qor.json + timing.json into DIR (handy without --workdir)\n"
               "  --emit verilog:FILE        the mapped netlist as Verilog (also --emit-dir verilog:DIR/)\n"
               "  --stats                    the per-color rows (see report:)\n"
               "  --reader / --recipe        the `lhd compile` front-end knobs (slang by default; O1)\n"
               "  --set synth.flag=value     the flow knobs (below); pass tuning rides the pass namespaces:\n"
               "                             --set abc.adder=cla  --set color.absorb=false  --set opentimer.hier=false\n"
               "  --set lhd.incremental=false  cold run (no compile-cache / abc_cache reuse)\n"
               "\n"
               "examples:\n"
               "  lhd synth cpu.prp --top Cpu --workdir W                  # reports in W/synth/; re-run is incremental\n"
               "  lhd synth cpu.prp --top Cpu --workdir W --stats --result-json r.json\n"
               "  lhd synth lg:cpu_lg --top Cpu --emit-dir lg:net --emit-dir report:rep\n"
               "  lhd synth cpu.sv --top cpu --set synth.liberty=cells.lib --set synth.opentimer=false\n"
               "  lhd synth cpu.prp --top Cpu --set abc.adder=cla --emit verilog:net.v\n");
    return print_options_section({"synth."});
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
        "flags:\n"
        "  --diag-fmt auto|json|pretty   output rendering (pretty affects options/log-channels)\n"
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
        "flags:\n"
        "  --diag-fmt auto|json|pretty   machine record or readable option prose\n"
        "\n"
        "examples:\n"
        "  lhd describe compile.cgen.srcmap\n"
        "  lhd describe lec\n");
    return 0;
  }
  if (topic == "version") {
    std::print(
        "lhd version — print the tool version\n"
        "\n"
        "usage: lhd version\n"
        "\n"
        "flags:\n"
        "  none (`lhd --version` is the top-level alias)\n"
        "\n"
        "examples:\n"
        "  lhd version\n"
        "  lhd --version\n");
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

namespace lhd {

// Distinct process exit codes per error class (see lhd.hpp). Every failure is
// non-zero so `cmd || handle` is unchanged; the values let a caller tell an
// invocation mistake (usage/missing_file/config) from a tool limitation
// (unsupported/timeout) from a DESIGN result (assert/equiv_fail — the tool ran
// correctly and the design is what failed). `internal` and anything unknown
// keep 1, the historical catch-all, so a new class is never silently mapped.
int exit_code_for(std::string_view error_class) {
  if (error_class == "usage") {
    return 2;
  }
  if (error_class == "missing_file") {
    return 3;
  }
  if (error_class == "config") {
    return 4;
  }
  if (error_class == "dependency") {
    return 5;
  }
  if (error_class == "syntax") {
    return 6;
  }
  if (error_class == "unsupported") {
    return 7;
  }
  if (error_class == "timeout") {
    return 8;
  }
  if (error_class == "signal") {
    return 9;
  }
  if (error_class == "equiv_fail") {
    return 10;
  }
  if (error_class == "assert") {
    return 11;  // a testbench assert fired: the DESIGN failed, not the tool
  }
  if (error_class == "compile") {
    return 12;  // the GENERATED sim driver failed to host-compile — our codegen or the
                // host toolchain, never the design's semantics (that is `assert`).
                // `lhd sim` has raised this class since the fast build path landed;
                // it was unlisted here and fell through to the exit-1 catch-all.
  }
  return 1;  // internal, and any class not listed above
}

}  // namespace lhd
