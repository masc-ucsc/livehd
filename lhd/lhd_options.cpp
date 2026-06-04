//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "lhd.hpp"

namespace lhd {

namespace {

// Canonical kind vocabulary. `ln:` is an hhds::Forest::save directory (the
// LNAST units of a design); `lg:` is an hhds::GraphLibrary::save directory
// (the LGraphs of a design). The long spellings stay accepted as aliases.
const std::vector<std::string_view> kInputKinds{"ln", "lg", "verilog", "pyrope"};
const std::vector<std::string_view> kOutputKinds{"ln",
                                                 "lg",
                                                 "verilog",
                                                 "pyrope",
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

bool ends_with(std::string_view s, std::string_view suffix) {
  return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

}  // namespace

Options parse_args(int argc, char** argv) {
  Options opts;

  int i = 1;

  // --version / -h before any command word
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

  if (i >= argc) {
    opts.command = "help";
    return opts;
  }

  std::string_view cmd{argv[i]};
  if (cmd == "elaborate" || cmd == "compile") {
    opts.command = cmd;
    ++i;
    // The language word is optional: it is inferred from the source-file
    // extensions (.prp -> pyrope, .v/.sv -> verilog) when omitted.
    if (i < argc) {
      std::string_view lang{argv[i]};
      if (lang == "verilog" || lang == "pyrope") {
        opts.language = lang;
        ++i;
      }
    }
  } else if (cmd == "synth" || cmd == "check" || cmd == "scan" || cmd == "list" || cmd == "describe" || cmd == "version"
             || cmd == "help") {
    opts.command = cmd;
    ++i;
  } else {
    throw Lhd_error{"usage", std::format("unknown command '{}'", cmd), "run `lhd help` for the command list"};
  }

  bool raw_mode = false;
  for (; i < argc; ++i) {
    std::string_view a{argv[i]};

    if (raw_mode) {
      opts.raw_args.emplace_back(a);
      continue;
    }
    if (a == "--") {
      raw_mode = true;
      continue;
    }

    if (a == "--emit") {
      opts.emits.emplace_back(parse_typed(a, need_value(a, i, argc, argv), true));
    } else if (a == "--emit-dir") {
      opts.emit_dirs.emplace_back(parse_typed(a, need_value(a, i, argc, argv), true));
    } else if (a == "--in") {
      opts.ins.emplace_back(parse_typed(a, need_value(a, i, argc, argv), false));
    } else if (a == "--in-dir") {
      opts.in_dirs.emplace_back(parse_typed(a, need_value(a, i, argc, argv), false));
    } else if (a == "--top") {
      opts.top = need_value(a, i, argc, argv);
    } else if (a == "--reader") {
      opts.reader = need_value(a, i, argc, argv);
      if (opts.reader != "slang" && opts.reader != "yosys") {
        throw Lhd_error{"usage", std::format("--reader must be slang or yosys, got '{}'", opts.reader), ""};
      }
    } else if (a == "--depfile") {
      opts.depfile = need_value(a, i, argc, argv);
    } else if (a == "--recipe") {
      opts.recipe = need_value(a, i, argc, argv);
    } else if (a == "--recipe-file") {
      opts.recipe_file = need_value(a, i, argc, argv);
    } else if (a == "--set") {
      auto kv  = std::string{need_value(a, i, argc, argv)};
      auto pos = kv.find('=');
      if (pos == std::string::npos || pos == 0) {
        throw Lhd_error{"usage", std::format("--set expects pass[.idx].flag=value, got '{}'", kv), ""};
      }
      opts.sets.emplace_back(kv.substr(0, pos), kv.substr(pos + 1));
    } else if (a == "--impl") {
      auto tp        = parse_typed(a, need_value(a, i, argc, argv), false);
      opts.impl_kind = tp.kind;
      opts.impl_path = tp.path;
    } else if (a == "--ref") {
      auto tp       = parse_typed(a, need_value(a, i, argc, argv), false);
      opts.ref_kind = tp.kind;
      opts.ref_path = tp.path;
    } else if (a == "--impl-top") {
      opts.impl_top = need_value(a, i, argc, argv);
    } else if (a == "--ref-top") {
      opts.ref_top = need_value(a, i, argc, argv);
    } else if (a == "--result-json") {
      opts.result_json = need_value(a, i, argc, argv);
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
    } else if (a == "-h" || a == "--help") {
      opts.files.insert(opts.files.begin(), opts.command);
      opts.command = "help";
    } else if (!a.empty() && a[0] == '-') {
      throw Lhd_error{"usage", std::format("unknown option '{}'", a), std::format("run `lhd help {}`", opts.command)};
    } else if (opts.command == "elaborate" || opts.command == "compile" || opts.command == "synth") {
      if (!route_positional(opts, a)) {
        opts.files.emplace_back(a);
      }
    } else {
      opts.files.emplace_back(a);
    }
  }

  if (!opts.recipe_file.empty()) {
    throw Lhd_error{"unsupported",
                    "--recipe-file is not implemented yet (built-in recipes ship first)",
                    "use --recipe O0|O1|O2 and --set pass.flag=value"};
  }

  // Infer the source language from the file extensions when not given.
  if ((opts.command == "elaborate" || opts.command == "compile") && opts.language.empty() && !opts.files.empty()) {
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
                      "use `lhd elaborate pyrope|verilog ...` or .prp/.v/.sv extensions"};
    }
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
