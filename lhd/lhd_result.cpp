//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <unistd.h>

#include <algorithm>
#include <ctime>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <print>
#include <sstream>
#include <string>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "graph_library_singleton.hpp"
#include "hhds/graph.hpp"
#include "lhd.hpp"
#include "node_util.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "woothash.hpp"

namespace lhd {

namespace fs = std::filesystem;

namespace {

void append_file_content(std::string& buf, const std::string& path) {
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs.is_open()) {
    return;  // the run itself reports missing_file with a proper diagnostic
  }
  std::ostringstream oss;
  oss << ifs.rdbuf();
  buf += oss.str();
}

// Hash a directory input (a design blob) deterministically: sorted relative
// paths + file bytes.
void append_dir_content(std::string& buf, const std::string& dir) {
  std::vector<std::string> rels;
  std::error_code          ec;
  for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); ++it) {
    if (it->is_regular_file()) {
      rels.emplace_back(it->path().lexically_relative(dir).string());
    }
  }
  std::sort(rels.begin(), rels.end());
  for (const auto& r : rels) {
    buf += r;
    buf += '\0';
    append_file_content(buf, (fs::path(dir) / r).string());
  }
}

// Hash only the `top` slice of an hhds graph-library directory: the resolved
// top graph(s) plus every graph reachable through Sub instances — bodies AND
// IO declarations (port name/id/bits/sign live ONLY in library.txt, and lec
// pairs pins by name, so they are proof inputs; bodyless black-box stubs
// contribute their decls). A whole-design library holds every module of the
// design; a proof scoped to one subtree must not change run_id when an
// unrelated module changes. Known approximation: the hierarchical driver
// today also entity-pairs defs that are not Sub-reachable from the top (stale
// whole-design leftovers) — those are outside the slice until load_side_graphs
// itself narrows to the top cone. Returns false when the slice cannot be
// resolved (no top, not a graph library, top absent or bodyless everywhere,
// or any library error) — the caller falls back to whole-directory hashing.
bool append_lg_slice_content(std::string& buf, const std::string& dir, const std::string& top) {
  std::error_code ec;
  if (top.empty() || !fs::exists(fs::path(dir) / "library.txt", ec) || ec) {
    return false;
  }
  try {
    auto& lib = livehd::Hhds_graph_library::instance(dir);

    // Resolve `top` like lec's pick(): exact full name, else the entity
    // (post-'.') name. Ambiguous entity matches hash the UNION of their
    // slices — whether ambiguity is an error stays a lec-side decision.
    std::vector<hhds::Gid> roots;
    if (auto gio = lib.find_io(top); gio) {
      roots.push_back(gio->get_gid());
    } else {
      auto entity = [](std::string_view n) {
        auto d = n.rfind('.');
        return d == std::string_view::npos ? n : n.substr(d + 1);
      };
      for (const hhds::Gid id : lib.all_io_gids()) {
        if (auto g = lib.find_io(id); g && entity(g->get_name()) == entity(top)) {
          roots.push_back(id);
        }
      }
    }
    // A top that only resolves to bodyless stubs is not this library's design
    // top (lec's pick() would reject it): whole-dir fallback, and never
    // get_graph() a bodyless gid (it asserts on dbg builds).
    const bool any_body = std::any_of(roots.begin(), roots.end(), [&lib](hhds::Gid id) { return lib.has_graph(id); });
    if (roots.empty() || !any_body) {
      return false;
    }

    // DFS over Sub targets. Materializes bodies for the slice only; fast_class
    // + subnode is a pure structure walk, so edge overflow stays deferred.
    // Bodyless decl-only gids stay in the cone (their interface hashes below).
    absl::flat_hash_set<hhds::Gid> seen(roots.begin(), roots.end());
    std::vector<hhds::Gid>         stack = roots;
    std::vector<hhds::Gid>         cone;
    while (!stack.empty()) {
      const hhds::Gid id = stack.back();
      stack.pop_back();
      cone.push_back(id);
      if (!lib.has_graph(id)) {
        continue;  // declared interface without a body: nothing to walk
      }
      auto g = lib.get_graph(id);
      if (!g) {
        continue;
      }
      namespace gu = livehd::graph_util;
      for (auto node : g->fast_class()) {
        if (gu::type_op_of(node) != Ntype_op::Sub) {
          continue;
        }
        auto sio = node.get_subnode_io();
        if (!sio) {
          continue;  // target has no decl in this library (resolved elsewhere)
        }
        const hhds::Gid child = sio->get_gid();
        if (seen.insert(child).second) {
          stack.push_back(child);
        }
      }
    }

    // Build the slice apart from `buf`: a throw mid-way must not leave partial
    // slice bytes in front of the whole-directory fallback.
    std::sort(cone.begin(), cone.end());
    std::string slice;
    for (const hhds::Gid id : cone) {
      slice += std::format("g{}", id);  // gid is the name hash: renames change the slice
      slice += '\0';
      if (auto gio = lib.find_io(id); gio) {
        auto decls = [&slice](std::string_view tag, const auto& pins) {
          for (const auto& d : pins) {
            slice += std::format("{} {} {} {} {} {}", tag, d.name, d.port_id, d.bits, d.unsign ? 1 : 0, d.loop_break ? 1 : 0);
            slice += '\0';
          }
        };
        decls("i", gio->get_input_pin_decls());
        decls("o", gio->get_output_pin_decls());
      }
      append_dir_content(slice, (fs::path(dir) / std::format("graph_{}", id)).string());
    }
    buf += slice;
    return true;
  } catch (...) {
    return false;  // any library/load hiccup: the whole-dir hash still works
  }
}

// SOURCE_DATE_EPOCH (reproducible-builds.org): the only timestamp the kernel
// ever embeds. Returns empty when unset/invalid -> timestamps are omitted.
std::string source_date_epoch_iso() {
  const char* env = std::getenv("SOURCE_DATE_EPOCH");
  if (env == nullptr || *env == '\0') {
    return "";
  }
  char*     end   = nullptr;
  long long epoch = std::strtoll(env, &end, 10);
  if (end == nullptr || *end != '\0' || epoch < 0) {
    return "";
  }
  std::time_t t = static_cast<std::time_t>(epoch);
  std::tm     tm{};
  if (gmtime_r(&t, &tm) == nullptr) {
    return "";
  }
  char buf[32];
  if (std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm) == 0) {
    return "";
  }
  return buf;
}

// ---- pretty qor rendering ---------------------------------------------------
// A pass's structured qor payload (embed_qor_sidecar) rendered as a short
// human report per `kind` instead of the raw JSON blob. jsonl mode embeds the
// payload untouched; an unknown/unparseable kind keeps the raw dump.

std::string json_str(const rapidjson::Value& v, const char* key) {
  auto it = v.FindMember(key);
  if (it == v.MemberEnd() || !it->value.IsString()) {
    return {};
  }
  return {it->value.GetString(), it->value.GetStringLength()};
}

bool json_num(const rapidjson::Value& v, const char* key, double& out) {
  auto it = v.FindMember(key);
  if (it == v.MemberEnd() || !it->value.IsNumber()) {
    return false;
  }
  out = it->value.GetDouble();
  return true;
}

// kind:"sta" (pass.opentimer timing.json): an OpenSTA-style report per design
// — max-delay summary, the critical-path Delay/Time/Description table, and
// the worst-endpoint arrivals, each source-attributed when a src is present.
bool write_pretty_sta(const rapidjson::Value& d) {
  auto dit = d.FindMember("designs");
  if (dit == d.MemberEnd() || !dit->value.IsArray()) {
    return false;
  }
  const std::string unit = json_str(d, "time_unit");
  for (const auto& des : dit->value.GetArray()) {
    if (!des.IsObject()) {
      continue;
    }
    std::string head = std::format("  sta: {}", json_str(des, "module"));
    if (double md = 0; json_num(des, "max_delay", md)) {
      head += std::format("  max delay {:.3f}{}{}", md, unit.empty() ? "" : " ", unit);
    }
    std::print("{}\n", head);
    if (auto cp = json_str(des, "critical_pin"); !cp.empty()) {
      auto src = json_str(des, "critical_src");
      std::print("    critical pin: {}{}\n", cp, src.empty() ? std::string{} : std::format("  ({})", src));
    }
    if (auto pit = des.FindMember("path"); pit != des.MemberEnd() && pit->value.IsArray() && !pit->value.Empty()) {
      const auto path = pit->value.GetArray();
      std::print("\n       Delay      Time   Description\n");
      std::print("    ------------------------------------------------------------\n");
      double last_at = 0;
      for (rapidjson::SizeType i = 0; i < path.Size(); ++i) {
        const auto& p = path[i];
        if (!p.IsObject()) {
          continue;
        }
        double at    = 0;
        double delay = 0;
        json_num(p, "at", at);
        json_num(p, "delay", delay);
        last_at   = at;
        auto dir  = json_str(p, "dir");
        char mark = dir == "rise" ? '^' : (dir == "fall" ? 'v' : ' ');
        auto cell = json_str(p, "cell");
        auto src  = json_str(p, "src");

        std::string desc = json_str(p, "pin");
        if (!cell.empty()) {
          desc += std::format(" ({})", cell);
        } else if (i == 0) {
          desc += " (in)";  // module input or a flop/memory-boundary arrival
        } else if (i + 1 == path.Size()) {
          desc += " (out)";
        }
        if (!src.empty()) {
          desc += std::format("  {}", src);
        }
        std::print("    {:>8.3f}  {:>8.3f} {} {}\n", delay, at, mark, desc);
      }
      std::print("              {:>8.3f}   data arrival time\n", last_at);
    }
    if (auto eit = des.FindMember("endpoints"); eit != des.MemberEnd() && eit->value.IsArray() && !eit->value.Empty()) {
      std::print("\n    worst endpoints:\n");
      for (const auto& e : eit->value.GetArray()) {
        if (!e.IsObject()) {
          continue;
        }
        double delay = 0;
        json_num(e, "delay", delay);
        auto src = json_str(e, "src");
        std::print("    {:>8.3f}  {}{}\n", delay, json_str(e, "pin"), src.empty() ? std::string{} : std::format("  ({})", src));
      }
    }
  }
  return true;
}

// kind:"abc-map" (pass.abc qor.json): the compact one-line summary (the same
// shape as the pass's own step-log line, which fd redirection keeps off the
// terminal).
bool write_pretty_abc_map(const rapidjson::Value& d) {
  auto tit = d.FindMember("total");
  if (tit == d.MemberEnd() || !tit->value.IsObject()) {
    return false;
  }
  const auto& t       = tit->value;
  double      regions = 0;
  double      gates   = 0;
  double      area    = 0;
  json_num(t, "regions", regions);
  json_num(t, "gates", gates);
  json_num(t, "area", area);
  std::string line
      = std::format("  qor: abc-map '{}': {:.0f} region(s), {:.0f} gates, area {:.2f}", json_str(d, "top"), regions, gates, area);
  if (double md = 0; json_num(t, "max_delay", md)) {
    line += std::format(", max delay {:.2f}", md);
    std::string crit;
    if (auto r = json_str(t, "critical_region"); !r.empty()) {
      crit += std::format("region '{}'", r);
    }
    if (auto o = json_str(t, "critical_output"); !o.empty()) {
      crit += std::format("{}output '{}'", crit.empty() ? "" : " ", o);
    }
    if (auto s = json_str(t, "critical_src"); !s.empty()) {
      crit += std::format(" @ {}", s);
    }
    if (!crit.empty()) {
      line += std::format(" ({})", crit);
    }
  }
  if (double bb = 0; json_num(t, "div_blackbox", bb) && bb > 0) {
    line += std::format("  [PARTIAL: {:.0f} blackboxed div/mod cone(s) unscored]", bb);
  }
  std::print("{}\n", line);
  return true;
}

void write_pretty_qor(const std::string& qor_json) {
  rapidjson::Document d;
  d.Parse(qor_json.data(), qor_json.size());
  bool rendered = false;
  if (!d.HasParseError() && d.IsObject()) {
    const auto kind = json_str(d, "kind");
    if (kind == "sta") {
      rendered = write_pretty_sta(d);
    } else if (kind == "abc-map") {
      rendered = write_pretty_abc_map(d);
    }
  }
  if (!rendered) {
    std::print("  qor: {}\n", qor_json);
  }
}

// --diag-fmt pretty: a short human status block on stdout instead of the JSON
// envelope. --result-json files always carry JSON; stderr already renders the
// per-diagnostic clang-style text, so this only summarizes the step result.
void write_pretty(const Options& opts, const Result& res) {
  bool        color = ::isatty(STDOUT_FILENO) != 0;
  const char* good  = color ? "\033[1;32m" : "";
  const char* bad   = color ? "\033[1;31m" : "";
  const char* off   = color ? "\033[0m" : "";

  auto plural = [](size_t n) { return n == 1 ? "" : "s"; };

  std::string head = std::format("lhd {}: {}{}{} — {} error{}, {} warning{}",
                                 res.command,
                                 res.status == "pass" ? good : bad,
                                 res.status,
                                 off,
                                 res.n_errors,
                                 plural(res.n_errors),
                                 res.n_warnings,
                                 plural(res.n_warnings));
  if (!res.run_id.empty()) {
    head += std::format("  (run {})", res.run_id);
  }
  std::print("{}\n", head);

  if (opts.verbose) {
    for (const auto& step : res.recipe_steps) {
      std::print("  step: {}\n", step);
    }
  }
  for (const auto& out : res.outputs) {
    std::print("  out: {}\n", out);
  }
  if (!res.scan_json.empty()) {
    std::print("  scan: {}\n", res.scan_json);
  }
  if (opts.verbose && !res.sim_tests_json.empty()) {
    std::print("  tests: {}\n", res.sim_tests_json);
  }
  if (!res.sim_debug_json.empty()) {  // --list-signals/--probe/--break-when: explicitly requested, always shown
    std::print("  debug: {}\n", res.sim_debug_json);
  }
  if (!res.qor_json.empty()) {
    write_pretty_qor(res.qor_json);
  }
  if (res.status != "pass") {
    std::print("  {}error[{}]{}: {}\n", bad, res.error_class, off, res.error_message);
    if (!res.error_hint.empty()) {
      std::print("  help: {}\n", res.error_hint);
    }
  }
  std::fflush(stdout);
}

}  // namespace

std::string compute_run_id(const Options& opts) {
  std::string buf;
  buf += "lhd-";
  buf += kVersion;
  buf += '|';
  buf += opts.command;
  buf += ' ';
  buf += opts.language;
  // Hash the RESOLVED config: the implicit default recipe must hash the same
  // as the equivalent explicit --recipe.
  std::string recipe = opts.recipe;
  if (recipe.empty() && opts.command == "compile") {
    recipe = "O1";
  }
  buf += std::format("|top={}|reader={}|recipe={}", opts.top, opts.reader, recipe);

  auto sets = opts.sets;
  std::sort(sets.begin(), sets.end());
  for (const auto& [k, v] : sets) {
    buf += std::format("|set:{}={}", k, v);
  }

  // Raw `--` args (verilog reader flags, e.g. `-F filelist.f`) change what gets
  // read, so they belong in the run_id. Order-significant (slang argv order).
  for (const auto& a : opts.raw_args) {
    buf += std::format("|raw:{}", a);
  }

  // Each input carries the kind and top that scope it: a lec --impl/--ref
  // lg: side is read only from its (per-side) top down, so its hash covers
  // just that slice; every other input — including --lib model libraries,
  // which the proof flattens through — is read whole and hashes whole. The
  // per-side top is part of every impl/ref row (a file-typed side proves a
  // different obligation under a different --impl-top).
  struct Run_input {
    std::string path;
    std::string kind;  // impl/ref side kind: "lg" enables slice hashing
    std::string top;
  };
  std::vector<Run_input> inputs;
  for (const auto& f : opts.files) {
    inputs.push_back({f, "", ""});
  }
  for (const auto& in : opts.ins) {
    inputs.push_back({in.path, "", ""});
  }
  for (const auto& in : opts.in_dirs) {
    inputs.push_back({in.path, "", ""});
  }
  for (const auto& l : opts.libs) {
    inputs.push_back({l.path, "", ""});
  }
  if (!opts.impl_path.empty()) {
    inputs.push_back({opts.impl_path, opts.impl_kind, opts.impl_top.empty() ? opts.top : opts.impl_top});
  }
  if (!opts.ref_path.empty()) {
    inputs.push_back({opts.ref_path, opts.ref_kind, opts.ref_top.empty() ? opts.top : opts.ref_top});
  }
  std::sort(inputs.begin(), inputs.end(), [](const Run_input& a, const Run_input& b) {
    return std::tie(a.path, a.kind, a.top) < std::tie(b.path, b.kind, b.top);
  });

  // Hash input BYTES only (ordinal-separated), never the path strings: the
  // same sources under a different sandbox/exec-root path must produce the
  // same run_id (future_cli.md: no absolute path leaks into an artifact).
  // Each row is ordinal + per-side top + a mode tag (lgslice/dir/file), so
  // slice, whole-dir and file byte streams can never alias each other.
  for (size_t idx = 0; idx < inputs.size(); ++idx) {
    const auto& [f, kind, eff_top] = inputs[idx];
    buf += '|';
    buf += std::format("{}", idx);
    buf += '\0';
    buf += std::format("top={}", eff_top);
    buf += '\0';
    if (fs::is_directory(f)) {
      // Slice only a true lg: side: an ln:/pyrope: DIRECTORY side may share
      // its db dir with a (stale) graph library, but lec reads the sources.
      std::string slice;
      if (kind == "lg" && append_lg_slice_content(slice, f, eff_top)) {
        buf += "lgslice";
        buf += '\0';
        buf += slice;
      } else {
        buf += "dir";
        buf += '\0';
        append_dir_content(buf, f);
      }
    } else {
      buf += "file";
      buf += '\0';
      append_file_content(buf, f);
    }
  }

  return std::format("{:016x}", lh::woothash64(buf.data(), buf.size(), 1021));
}

void write_result(const Options& opts, const Result& res) {
  // tool (cat/grep/diff/tree) owns stdout (its payload IS the protocol, like
  // lsp): on success the envelope is written only to an explicit --result-json.
  // On failure it is the error report and prints as usual.
  if (opts.command == "tool" && opts.result_json.empty() && res.status == "pass") {
    return;
  }

  rapidjson::StringBuffer                    sb;
  rapidjson::Writer<rapidjson::StringBuffer> w(sb);

  w.StartObject();
  w.Key("schema_version");
  w.Uint(1);
  w.Key("tool");
  w.String("lhd");
  w.Key("command");
  w.String(res.command.c_str());
  w.Key("status");
  w.String(res.status.c_str());
  w.Key("run_id");
  w.String(res.run_id.c_str());
  w.Key("exit_code");
  w.Int(res.exit_code);

  if (auto iso = source_date_epoch_iso(); !iso.empty()) {
    w.Key("started_at");
    w.String(iso.c_str());
    w.Key("ended_at");
    w.String(iso.c_str());
  }

  w.Key("recipe");
  w.StartArray();
  for (const auto& s : res.recipe_steps) {
    w.String(s.c_str());
  }
  w.EndArray();

  w.Key("inputs");
  w.StartArray();
  for (const auto& s : res.inputs) {
    w.String(s.c_str());
  }
  w.EndArray();

  w.Key("outputs");
  w.StartArray();
  for (const auto& s : res.outputs) {
    w.String(s.c_str());
  }
  w.EndArray();

  w.Key("diagnostics_count");
  w.StartObject();
  w.Key("errors");
  w.Uint64(res.n_errors);
  w.Key("warnings");
  w.Uint64(res.n_warnings);
  w.EndObject();

  if (!res.scan_json.empty()) {
    w.Key("scan");
    w.RawValue(res.scan_json.data(), res.scan_json.size(), rapidjson::kArrayType);
  }

  if (!res.sim_tests_json.empty()) {
    w.Key("tests");
    w.RawValue(res.sim_tests_json.data(), res.sim_tests_json.size(), rapidjson::kArrayType);
  }

  if (!res.sim_debug_json.empty()) {
    w.Key("debug");
    w.RawValue(res.sim_debug_json.data(), res.sim_debug_json.size(), rapidjson::kObjectType);
  }

  if (!res.qor_json.empty()) {
    w.Key("qor");
    w.RawValue(res.qor_json.data(), res.qor_json.size(), rapidjson::kObjectType);
  }

  if (res.status != "pass") {
    w.Key("error");
    w.StartObject();
    w.Key("class");
    w.String(res.error_class.c_str());
    w.Key("message");
    w.String(res.error_message.c_str());
    if (!res.error_hint.empty()) {
      w.Key("hint");
      w.String(res.error_hint.c_str());
    }
    w.EndObject();
  }

  w.EndObject();

  // A declared diagnostics output must exist even when no diagnostic fired
  // (the sink creates the file lazily on first emit).
  for (const auto& e : opts.emits) {
    if (e.kind == "diagnostics" && !fs::exists(e.path)) {
      std::ofstream touch(e.path);
    }
  }

  if (opts.result_json.empty()) {
    if (opts.diag_fmt == Diag_fmt::pretty) {
      write_pretty(opts, res);
      return;
    }
    std::fputs(sb.GetString(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
    return;
  }
  // A --result-json file always carries the JSON envelope, whatever the
  // display mode (it is the machine record of the run).
  std::ofstream ofs(opts.result_json);
  if (ofs.is_open()) {
    ofs << sb.GetString() << '\n';
  } else {
    std::fputs(sb.GetString(), stdout);
    std::fputc('\n', stdout);
  }
}

}  // namespace lhd
