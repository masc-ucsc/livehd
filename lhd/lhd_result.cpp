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
// top graph(s) plus every graph reachable through Sub instances. A whole-design
// library holds every module of the design; a proof scoped to one subtree must
// not change run_id when an unrelated module changes. Returns false when the
// slice cannot be resolved (no top, not a graph library, top absent, or any
// library error) — the caller then falls back to whole-directory hashing.
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
    if (roots.empty()) {
      return false;
    }

    // DFS over Sub targets. Materializes bodies for the slice only; fast_class
    // + subnode is a pure structure walk, so edge overflow stays deferred.
    absl::flat_hash_set<hhds::Gid> seen(roots.begin(), roots.end());
    std::vector<hhds::Gid>         stack = roots;
    std::vector<hhds::Gid>         cone;
    while (!stack.empty()) {
      const hhds::Gid id = stack.back();
      stack.pop_back();
      cone.push_back(id);
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
          continue;  // external/black-box target: no bytes of it in this library
        }
        const hhds::Gid child = sio->get_gid();
        if (lib.has_graph(child) && seen.insert(child).second) {
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

  // Each input carries the top that scopes it: a lec --impl/--ref side is read
  // only from its (per-side) top down, so its hash covers just that slice; all
  // other inputs are read whole and hash whole (empty top = no slice).
  struct Run_input {
    std::string path;
    std::string top;
  };
  std::vector<Run_input> inputs;
  for (const auto& f : opts.files) {
    inputs.push_back({f, ""});
  }
  for (const auto& in : opts.ins) {
    inputs.push_back({in.path, ""});
  }
  for (const auto& in : opts.in_dirs) {
    inputs.push_back({in.path, ""});
  }
  if (!opts.impl_path.empty()) {
    inputs.push_back({opts.impl_path, opts.impl_top.empty() ? opts.top : opts.impl_top});
  }
  if (!opts.ref_path.empty()) {
    inputs.push_back({opts.ref_path, opts.ref_top.empty() ? opts.top : opts.ref_top});
  }
  std::sort(inputs.begin(), inputs.end(),
            [](const Run_input& a, const Run_input& b) { return std::tie(a.path, a.top) < std::tie(b.path, b.top); });

  // Hash input BYTES only (ordinal-separated), never the path strings: the
  // same sources under a different sandbox/exec-root path must produce the
  // same run_id (future_cli.md: no absolute path leaks into an artifact).
  for (size_t idx = 0; idx < inputs.size(); ++idx) {
    const auto& [f, eff_top] = inputs[idx];
    buf += '|';
    buf += std::format("{}", idx);
    buf += '\0';
    if (fs::is_directory(f)) {
      // Mode headers keep the slice and whole-dir byte streams from aliasing;
      // the slice header also folds the per-side top (not otherwise hashed)
      // into the run_id.
      std::string slice;
      if (append_lg_slice_content(slice, f, eff_top)) {
        buf += std::format("lgslice={}", eff_top);
        buf += '\0';
        buf += slice;
      } else {
        buf += "dir";
        buf += '\0';
        append_dir_content(buf, f);
      }
    } else {
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
