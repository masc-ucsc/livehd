//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "color_common.hpp"  // livehd::color::is_partitionable / NO_COLOR (lhd tool)
#include "diag.hpp"
#include "eprp.hpp"
#include "file_utils.hpp"
#include "graph_library_singleton.hpp"
#include "hhds/graph.hpp"
#include "hhds/tree.hpp"
#include "hhds/tree_edit_distance.hpp"
#include "lhd.hpp"
#include "lnast.hpp"
#include "lnast_ntype.hpp"
#include "log.hpp"  // livehd::log — `--set <channel>.log=<level>` developer logging
#include "node_util.hpp"
#include "pass.hpp"
#include "prp2lnast.hpp"
#include "query.hpp"  // pass/lec L1 (lec::prove_equal) for the cross-check path
#include "rapidjson/document.h"
#include "thread_pool.hpp"
#include "upass_tolg.hpp"
#include "woothash.hpp"

void setup_inou_yosys();  // inou/yosys/inou_yosys_api.cpp (not Pass_plugin-registered)

namespace lhd {

namespace fs = std::filesystem;

namespace {

int step_counter = 0;  // per-process step sequence for log naming

std::string join_csv(const std::vector<std::string>& v) {
  std::string out;
  for (const auto& e : v) {
    if (!out.empty()) {
      out += ',';
    }
    out += e;
  }
  return out;
}

void ensure_dir(const std::string& path) {
  std::error_code ec;
  fs::create_directories(path, ec);
  if (ec) {
    throw Lhd_error{"config", std::format("could not create directory {}: {}", path, ec.message()), ""};
  }
}

void check_inputs_exist(const std::vector<std::string>& files) {
  for (const auto& f : files) {
    if (::access(f.c_str(), R_OK) != 0) {
      throw Lhd_error{"missing_file",
                      std::format("file not found: {}", f),
                      "a source file the frontend cannot find within its declared inputs is an error (hermetic kernel)"};
    }
  }
}

const Typed_path* find_slot(const std::vector<Typed_path>& slots, std::string_view kind) {
  for (const auto& s : slots) {
    if (s.kind == kind) {
      return &s;
    }
  }
  return nullptr;
}

// ---- --dump (screen debug observables) ---------------------------------------
// Dumps print to stderr: stdout stays protocol-clean (the result envelope),
// and run_step captures pass stdout into the step logs regardless.

bool wants_dump(const Options& opts, std::string_view what) {
  return std::find(opts.dumps.begin(), opts.dumps.end(), what) != opts.dumps.end();
}

void screen_dump_lnasts(const std::vector<std::shared_ptr<Lnast>>& units, std::string_view stage) {
  auto sorted = units;
  std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
    return a->get_top_module_name() < b->get_top_module_name();
  });
  for (const auto& ln : sorted) {
    std::ostringstream oss;
    oss << std::format("//---- lnast {} ({})\n", ln->get_top_module_name(), stage);
    ln->dump(oss);
    auto s = oss.str();
    std::fwrite(s.data(), 1, s.size(), stderr);
  }
}

// One LGraph as a plain-text node/edge listing — the lgshell-era lgraph.dump
// debug observable, reborn for --dump lg. Traversal mirrors
// Graphviz::populate_lg_data (fast_class + IO decls; const edges are only
// visible from the sink side because CONST_NODE is a builtin singleton).
void dump_graph_text(std::ostream& os, hhds::Graph* g) {
  namespace gu = livehd::graph_util;

  auto end_name = [](const hhds::Pin_class& pin) -> std::string {
    if (gu::is_graph_input_pin(pin) || gu::is_graph_output_pin(pin)) {
      return std::format("${}", pin.get_pin_name());
    }
    auto node = pin.get_master_node();
    auto pn   = gu::pin_name_of(pin);
    if (pn.empty()) {
      return std::format("{}.p{}", gu::debug_name(node), pin.get_port_id());
    }
    return std::format("{}.{}", gu::debug_name(node), pn);
  };
  auto edge_line = [&](const auto& e) {
    os << std::format("    {} -> {}  ({}b)\n", end_name(e.driver), end_name(e.sink), gu::bits_of(e.driver));
  };
  auto const_edge_line = [&](const auto& inp) {
    os << std::format("    const {} -> {}  ({}b)\n",
                      gu::hydrate_const(inp.driver).to_pyrope(),
                      end_name(inp.sink),
                      gu::bits_of(inp.driver));
  };

  os << std::format("module {}\n", g->get_name());
  auto gio = g->get_io();
  if (gio) {
    for (const auto& decl : gio->get_input_pin_decls()) {
      auto pin = g->get_input_pin(decl.name);
      os << std::format("  input  ${}  ({}b)\n", decl.name, gu::bits_of(pin, *gio, decl.name));
      for (const auto& out : pin.out_edges()) {
        edge_line(out);
      }
    }
    for (const auto& decl : gio->get_output_pin_decls()) {
      auto pin = g->get_output_pin(decl.name);
      os << std::format("  output ${}  ({}b)\n", decl.name, gu::bits_of(pin, *gio, decl.name));
      for (const auto& out : pin.out_edges()) {  // outputs reread as drivers
        edge_line(out);
      }
      for (const auto& inp : pin.inp_edges()) {
        if (gu::is_const_pin(inp.driver)) {
          const_edge_line(inp);
        }
      }
    }
  }
  for (auto node : g->fast_class()) {
    if (node.inp_edges().empty() && node.out_edges().empty()) {
      continue;
    }
    if (gu::type_op_of(node) == Ntype_op::Nconst) {
      os << std::format("  {} = {}\n", gu::debug_name(node), gu::hydrate_const(node).to_pyrope());
    } else {
      os << std::format("  {}\n", gu::debug_name(node));
    }
    for (const auto& out : node.out_edges()) {
      edge_line(out);
    }
    for (const auto& inp : node.inp_edges()) {
      if (gu::is_const_pin(inp.driver)) {
        const_edge_line(inp);
      }
    }
  }
}

void screen_dump_graphs(const Eprp_var& var, std::string_view stage) {
  if (var.graphs.empty()) {
    std::fputs("//---- lg dump: no LGraphs (e.g. a pure-comptime program has no module IO)\n", stderr);
    return;
  }
  auto sorted = var.graphs;
  std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a->get_name() < b->get_name(); });
  for (const auto& g : sorted) {
    std::ostringstream oss;
    oss << std::format("//---- lg {} ({})\n", g->get_name(), stage);
    dump_graph_text(oss, g.get());
    auto s = oss.str();
    std::fwrite(s.data(), 1, s.size(), stderr);
  }
}

// RAII: route fd 1 (stdout) into a log file while a pass runs so raw pass
// output never lands on the protocol stream (future_cli.md "Clean stdout").
class Stdout_to_log {
public:
  explicit Stdout_to_log(const std::string& log_path) {
    std::fflush(stdout);
    std::cout.flush();
    saved_fd_ = ::dup(1);
    int fd    = ::open(log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
      ::dup2(fd, 1);
      ::close(fd);
    }
  }
  Stdout_to_log(const Stdout_to_log&)            = delete;
  Stdout_to_log& operator=(const Stdout_to_log&) = delete;
  ~Stdout_to_log() {
    if (saved_fd_ >= 0) {
      std::fflush(stdout);
      std::cout.flush();
      ::dup2(saved_fd_, 1);
      ::close(saved_fd_);
    }
  }

private:
  int saved_fd_ = -1;
};

std::string& workdir(Options& opts) {
  if (opts.workdir.empty()) {
    // Ephemeral scratch. The path never leaks into a declared artifact, so
    // the kernel's determinism invariant holds over output bytes.
    auto        tmpl = (fs::temp_directory_path() / "lhd_work_XXXXXX").string();
    std::string buf{tmpl};
    if (::mkdtemp(buf.data()) == nullptr) {
      throw Lhd_error{"config", std::format("could not create scratch workdir under {}", tmpl), "pass --workdir DIR"};
    }
    opts.workdir = buf;
  }
  ensure_dir(opts.workdir + "/logs");
  return opts.workdir;
}

std::string next_log_path(Options& opts, std::string_view method) {
  std::string name{method};
  std::replace(name.begin(), name.end(), '.', '_');
  return std::format("{}/logs/{:03d}_lhd_{}.log", workdir(opts), ++step_counter, name);
}

void mirror_log_to_stderr(const std::string& log_path) {
  std::ifstream ifs(log_path);
  if (!ifs.is_open()) {
    return;
  }
  std::string line;
  while (std::getline(ifs, line)) {
    std::fputs(line.c_str(), stderr);
    std::fputc('\n', stderr);
  }
}

std::string map_diag_category(std::string_view cat) {
  if (cat == "syntax" || cat == "name" || cat == "type" || cat == "bitwidth") {
    return "syntax";
  }
  if (cat == "missing_file" || cat == "usage" || cat == "config" || cat == "unsupported") {
    return std::string{cat};
  }
  return "internal";
}

std::string step_desc(std::string_view method, const Eprp_var::Eprp_dict& labels) {
  std::vector<std::string> kv;
  kv.reserve(labels.size());
  for (const auto& [k, v] : labels) {
    kv.emplace_back(std::format("{}:{}", k, v));
  }
  std::sort(kv.begin(), kv.end());  // Eprp_dict iteration order is unstable
  std::string out{method};
  for (const auto& e : kv) {
    out += ' ';
    out += e;
  }
  return out;
}

void setup_diag(const Options& opts, std::string_view step) {
  auto& sink = livehd::diag::sink();
  sink.clear();
  sink.set_human_stderr(!opts.quiet);
  sink.set_stderr_jsonl(opts.diag_fmt == Diag_fmt::jsonl);
  const auto* d = find_slot(opts.emits, "diagnostics");
  sink.set_jsonl_path(d ? std::string_view{d->path} : std::string_view{"off"});
  sink.set_step(step);
}

// Run one registered EPRP method synchronously, stdout captured to a log.
void run_step(std::string_view method, Eprp_var& var, const Eprp_var::Eprp_dict& labels, Options& opts, Result& res) {
  auto log = next_log_path(opts, method);
  {
    Stdout_to_log redirect(log);
    Pass::eprp.run_method_now(method, var, labels);
    thread_pool.wait_all();
  }
  res.recipe_steps.emplace_back(step_desc(method, labels));
  if (opts.verbose) {
    mirror_log_to_stderr(log);
  }
  if (livehd::diag::sink().has_errors()) {
    throw classify_engine_failure(std::format("{} reported errors", method));
  }
}

// The --set/--config pass-name vocabulary -> the EPRP method that consumes
// it. THE central mapping: the merge_sets call sites, --set/--config
// validation, and `lhd list options` / `lhd describe pass.flag` all derive
// from this table (the flags themselves are each method's registered EPRP
// labels — add_label_optional/required is the single registration point).
// The set-name is the command-path namespace the option is reached under
// (2h-set_path): standalone `lhd pass <sub>` commands take `pass.<sub>.*`,
// `lhd lec` is a top-level command so it keeps `lec.*`, and the passes that
// only run inside a recipe (upass/cprop/bitwidth/cgen — no bare command word
// to anchor them) keep their short method-derived names. canonical_set_key()
// lets a user drop any leading segment the command words to the left already
// supply, so `--set pass.abc.adder`, `--set abc.adder` (after `pass`) and
// `--set adder` (after `pass abc`) all resolve here to `pass.abc`.
constexpr std::pair<std::string_view, std::string_view> kSetPasses[] = {
    {         "upass",        "pass.upass"},
    {         "cprop",        "pass.cprop"},
    {      "bitwidth",     "pass.bitwidth"},
    {          "cgen", "inou.cgen.verilog"},
    {    "pass.color",        "pass.color"},
    {"pass.partition",    "pass.partition"},
    {      "pass.abc",          "pass.abc"},
    {  "pass.liberty",      "pass.liberty"},
    {           "lec",          "pass.lec"},
};

std::string_view set_pass_method(std::string_view set_name) {
  for (const auto& [name, method] : kSetPasses) {
    if (name == set_name) {
      return method;
    }
  }
  return {};
}

// Pass-base plumbing labels (Pass::register_inou stamps them onto every inou
// method). In lhd these belong to the kernel — odir comes from the typed
// --emit/--emit-dir slot, inputs are positional — so they are not listed as
// options and --set/--config rejects them.
bool is_kernel_label(std::string_view flag) { return flag == "files" || flag == "path" || flag == "odir"; }

// canonical `<pass>.flag=value` set entries for `pass_name` -> EPRP labels.
// Keys arrive canonicalized (canonical_set_key, at parse time), so the pass
// token may itself contain dots (e.g. `pass.abc`); split on the LAST dot so
// the final component is always the flag.
void merge_sets(const Options& opts, std::string_view pass_name, Eprp_var::Eprp_dict& labels) {
  for (const auto& [key, value] : opts.sets) {
    auto pos = key.rfind('.');
    if (pos == std::string::npos) {
      throw Lhd_error{"usage", std::format("--set expects pass.flag=value, got '{}={}'", key, value), ""};
    }
    auto pass = key.substr(0, pos);
    auto flag = key.substr(pos + 1);
    if (flag == "log") {
      continue;  // `<channel>.log` is a livehd::log channel (apply_log_settings), not a pass label
    }
    if (pass != pass_name) {
      continue;
    }
    labels[flag] = value;
  }
}

// Validate every --set/--config entry against the live registry: a typo'd
// pass OR flag must error, never silently no-op (merge_sets copies labels
// blind). Requires init_engine().
void check_known_set_passes(const Options& opts) {
  for (const auto& [key, value] : opts.sets) {
    auto pos = key.rfind('.');
    if (pos == std::string::npos) {
      throw Lhd_error{"usage", std::format("--set expects pass.flag=value, got '{}={}'", key, value), ""};
    }
    auto pass = key.substr(0, pos);
    auto flag = key.substr(pos + 1);
    if (flag == "log") {
      // `<channel>.log=<level>` enables developer logging (livehd::log), a
      // namespace orthogonal to the pass-flag registry: validate the channel
      // and the level here, then apply_log_settings() records it.
      if (!livehd::log::is_channel(pass)) {
        throw Lhd_error{"usage",
                        std::format("--set/--config references unknown log channel '{}'", pass),
                        "`lhd list log-channels` lists the channels you can enable"};
      }
      bool ok = false;
      (void)livehd::log::parse_level(value, ok);
      if (!ok) {
        throw Lhd_error{"usage",
                        std::format("--set/--config log level '{}={}' must be off|error|warn|info|debug|trace", key, value),
                        ""};
      }
      continue;
    }
    auto method = set_pass_method(pass);
    if (method.empty()) {
      std::string known;
      for (const auto& [name, m] : kSetPasses) {
        (void)m;
        known += known.empty() ? "" : ", ";
        known += name;
      }
      throw Lhd_error{"usage",
                      std::format("--set/--config references unknown pass '{}'", pass),
                      std::format("known passes: {} (`lhd list options`)", known)};
    }
    if (is_kernel_label(flag)) {
      throw Lhd_error{"usage",
                      std::format("--set/--config flag '{}' is kernel-managed, not a pass option", key),
                      "outputs ride typed slots (--emit/--emit-dir) and inputs are positional; see `lhd help`"};
    }
    const auto* m = Pass::eprp.get_method(method);
    if (m == nullptr || !m->has_label(flag)) {
      throw Lhd_error{"usage",
                      std::format("--set/--config references unknown flag '{}' of pass '{}'", flag, pass),
                      std::format("`lhd list options {}\\..*` shows what {} accepts", pass, pass)};
    }
  }
}

// Apply every `--set <channel>.log=<level>` to the livehd::log registry (the
// channel and level were already validated by check_known_set_passes). In a
// release build LHD_LOG is compiled out, so this just records levels nothing
// reads — `--set ...log=...` stays accepted across build modes either way.
void apply_log_settings(const Options& opts) {
  for (const auto& [key, value] : opts.sets) {
    auto pos = key.rfind('.');
    if (pos == std::string::npos || key.substr(pos + 1) != "log") {
      continue;
    }
    bool ok  = false;
    auto lvl = livehd::log::parse_level(value, ok);
    if (ok) {
      livehd::log::configure(key.substr(0, pos), lvl);
    }
  }
}

// Recipe name -> ordered (set-name, EPRP method) graph passes.
std::vector<std::pair<std::string, std::string>> recipe_graph_passes(const Options& opts, std::string_view def) {
  std::string r = opts.recipe.empty() ? std::string{def} : opts.recipe;
  if (r == "O0") {
    return {};
  }
  if (r == "O1") {
    return {
        {"cprop", "pass.cprop"}
    };
  }
  if (r == "O2") {
    return {
        {   "cprop",    "pass.cprop"},
        {"bitwidth", "pass.bitwidth"}
    };
  }
  throw Lhd_error{"usage", std::format("unknown recipe '{}'", r), "built-in recipes: O0, O1, O2 (`lhd list recipes`)"};
}

uint64_t hash_bytes(const std::string& bytes) { return lh::woothash64(bytes.data(), bytes.size(), 1021); }

// ---- typed-emit helpers -----------------------------------------------------

std::string json_escape_min(std::string_view s);  // defined with the scan command below

// One `pub` export in a unit's manifest entry. `url` only for
// lambda exports (`ln:<unit>.<name>`); values live in the `<unit>.__pub`
// wrapper tree.
struct Manifest_pub {
  std::string name;
  std::string kind;  // value|comb|mod|pipe|fluid
  std::string url;
};

// One manifest unit entry. `unit_kind` distinguishes file-level units
// ("file"), extracted lambdas (their durable lambda kind — restored onto the
// Lnast on load, the in-memory member doesn't ride the forest save), and the
// synthesized pub wrapper ("pub"). Empty = omit (non-ln emit kinds).
struct Manifest_unit {
  std::string               name;
  uint64_t                  hash{0};
  std::string               unit_kind;
  std::vector<Manifest_pub> pubs;  // file units only (the pub index)
};

void write_manifest(const std::string& dir, std::string_view kind, const std::vector<Manifest_unit>& units) {
  std::ofstream ofs(dir + "/manifest.json");
  if (!ofs.is_open()) {
    throw Lhd_error{"config", std::format("could not write {}/manifest.json", dir), ""};
  }
  // `ln:` units live inside the hhds::Forest save (no per-unit file); the
  // per-unit file kinds carry a "file" entry.
  std::string_view ext = kind == "pyrope" ? ".prp" : (kind == "verilog" ? ".v" : (kind == "lnast-dump" ? ".lnast" : ""));
  ofs << "{\"schema_version\":1,\"kind\":\"" << kind << "\",\"units\":[";
  bool first = true;
  for (const auto& u : units) {
    if (!first) {
      ofs << ',';
    }
    first = false;
    ofs << "{\"name\":\"" << json_escape_min(u.name) << "\"";
    if (!ext.empty()) {
      ofs << ",\"file\":\"" << json_escape_min(u.name) << ext << "\"";
    }
    ofs << ",\"content_hash\":\"" << std::format("{:016x}", u.hash) << "\"";
    if (!u.unit_kind.empty()) {
      ofs << ",\"unit_kind\":\"" << json_escape_min(u.unit_kind) << "\"";
    }
    if (!u.pubs.empty()) {
      ofs << ",\"pub\":[";
      bool pfirst = true;
      for (const auto& p : u.pubs) {
        if (!pfirst) {
          ofs << ',';
        }
        pfirst = false;
        ofs << "{\"name\":\"" << json_escape_min(p.name) << "\",\"kind\":\"" << json_escape_min(p.kind) << "\"";
        if (!p.url.empty()) {
          ofs << ",\"url\":\"" << json_escape_min(p.url) << "\"";
        }
        ofs << "}";
      }
      ofs << "]";
    }
    ofs << "}";
  }
  ofs << "]}\n";
}

// Pair-form adapter for the per-unit-file emit kinds (pyrope/verilog/
// lnast-dump), which carry no unit_kind/pub metadata.
void write_manifest(const std::string& dir, std::string_view kind, const std::vector<std::pair<std::string, uint64_t>>& units) {
  std::vector<Manifest_unit> rich;
  rich.reserve(units.size());
  for (const auto& [name, hash] : units) {
    rich.push_back({name, hash, "", {}});
  }
  write_manifest(dir, kind, rich);
}

// ---- ln: directories (hhds::Forest::save + manifest.json) -------------------

// Persist `units` as ONE hhds::Forest::save directory (the `ln:` interchange
// form): forest.txt + binary tree bodies (attrs included) + a manifest.json
// enumerating the unit names with content hashes.
void save_ln_dir(Options& opts, Result& res, const std::vector<std::shared_ptr<Lnast>>& units, const std::string& dir) {
  (void)opts;
  if (units.empty()) {
    throw Lhd_error{"config", "no LNAST units to emit as ln:", ""};
  }
  ensure_dir(dir);

  auto sorted = units;
  std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
    return a->get_top_module_name() < b->get_top_module_name();
  });

  auto                       forest = hhds::Forest::create();
  std::vector<Manifest_unit> manifest;
  for (const auto& ln : sorted) {
    std::string name{ln->get_top_module_name()};
    if (!manifest.empty() && manifest.back().name == name) {
      throw Lhd_error{"config", std::format("duplicate LNAST unit name '{}'", name), ""};
    }
    ln->export_into(*forest);
    std::ostringstream oss;
    ln->dump(oss);  // hash the canonical text form (deterministic)
    Manifest_unit u;
    u.name = name;
    u.hash = hash_bytes(oss.str());
    // Durable unit metadata: the lambda kind (an in-memory Lnast
    // member, lost across the forest save) and the file unit's pub index.
    if (name.size() > 6 && name.ends_with(".__pub")) {
      u.unit_kind = "pub";
    } else if (!ln->get_lambda_kind().empty()) {
      u.unit_kind = std::string(ln->get_lambda_kind());
    } else {
      u.unit_kind = "file";
    }
    for (const auto& p : ln->get_pub_list()) {
      Manifest_pub mp;
      mp.name = p.name;
      mp.kind = p.kind;
      if (p.kind != "value") {
        mp.url = std::format("ln:{}.{}", name, p.name);
      }
      u.pubs.push_back(std::move(mp));
    }
    manifest.push_back(std::move(u));
  }
  forest->save(dir);
  write_manifest(dir, "ln", manifest);
  res.outputs.push_back(dir);
}

// Load every unit of an `ln:` directory. The units share the loaded forest.
std::vector<std::shared_ptr<Lnast>> load_ln_dir(const std::string& dir) {
  if (::access((dir + "/forest.txt").c_str(), R_OK) != 0) {
    throw Lhd_error{"missing_file",
                    std::format("ln: input is not a forest directory: {}", dir),
                    "expected a directory produced by --emit-dir ln:DIR/ (forest.txt + manifest.json)"};
  }
  std::ifstream mifs(dir + "/manifest.json");
  if (!mifs.is_open()) {
    throw Lhd_error{"missing_file", std::format("missing {}/manifest.json", dir), ""};
  }
  std::ostringstream moss;
  moss << mifs.rdbuf();
  rapidjson::Document doc;
  doc.Parse(moss.str().c_str());
  if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("units") || !doc["units"].IsArray()) {
    throw Lhd_error{"config", std::format("malformed manifest.json in {}", dir), ""};
  }

  auto forest = hhds::Forest::create();
  forest->load(dir);

  std::vector<std::shared_ptr<Lnast>> out;
  for (const auto& u : doc["units"].GetArray()) {
    if (!u.IsObject() || !u.HasMember("name") || !u["name"].IsString()) {
      throw Lhd_error{"config", std::format("malformed unit entry in {}/manifest.json", dir), ""};
    }
    auto ln = Lnast::adopt(forest, u["name"].GetString());
    if (!ln) {
      throw Lhd_error{"config", std::format("unit '{}' listed in manifest but missing from the forest", u["name"].GetString()), ""};
    }
    // Restore the durable unit metadata the forest save doesn't
    // carry: the lambda kind (inliner/tolg gate on it) and the pub index.
    if (u.HasMember("unit_kind") && u["unit_kind"].IsString()) {
      std::string_view uk = u["unit_kind"].GetString();
      if (uk != "file" && uk != "pub") {
        ln->set_lambda_kind(uk);
      }
    }
    if (u.HasMember("pub") && u["pub"].IsArray()) {
      for (const auto& p : u["pub"].GetArray()) {
        if (p.IsObject() && p.HasMember("name") && p["name"].IsString() && p.HasMember("kind") && p["kind"].IsString()) {
          ln->add_pub(p["name"].GetString(), p["kind"].GetString());
        }
      }
    }
    out.push_back(std::move(ln));
  }
  if (out.empty()) {
    throw Lhd_error{"config", std::format("ln: directory {} holds no units", dir), ""};
  }
  return out;
}

void emit_ln_outputs(const std::vector<std::shared_ptr<Lnast>>& units, Options& opts, Result& res) {
  for (const auto& e : opts.emit_dirs) {
    if (e.kind != "ln") {
      continue;
    }
    save_ln_dir(opts, res, units, e.path);
  }
}

// `--emit-dir lnast-dump:DIR/` — the round-trippable textual LNAST dump
// (Lnast::dump text form), one <unit>.lnast per unit. A debug/
// test observable: the binary interchange form stays `ln:`.
void emit_lnast_dump_outputs(const std::vector<std::shared_ptr<Lnast>>& units, Options& opts, Result& res) {
  for (const auto& e : opts.emit_dirs) {
    if (e.kind != "lnast-dump") {
      continue;
    }
    if (units.empty()) {
      throw Lhd_error{"config", "no LNAST units to emit as lnast-dump:", ""};
    }
    ensure_dir(e.path);

    auto sorted = units;
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
      return a->get_top_module_name() < b->get_top_module_name();
    });
    std::vector<std::pair<std::string, uint64_t>> manifest;
    for (const auto& ln : sorted) {
      std::string name{ln->get_top_module_name()};
      if (!manifest.empty() && manifest.back().first == name) {
        throw Lhd_error{"config", std::format("duplicate LNAST unit name '{}'", name), ""};
      }
      std::ostringstream oss;
      ln->dump(oss);
      std::ofstream ofs(std::format("{}/{}.lnast", e.path, name));
      if (!ofs.is_open()) {
        throw Lhd_error{"config", std::format("could not write {}/{}.lnast", e.path, name), ""};
      }
      ofs << oss.str();
      manifest.emplace_back(name, hash_bytes(oss.str()));
    }
    write_manifest(e.path, "lnast-dump", manifest);
    res.outputs.push_back(e.path);
  }
}

// Generate per-module Verilog with inou.cgen.verilog into `odir`, returning
// the sorted module names.
std::vector<std::string> cgen_into(Options& opts, Result& res, Eprp_var& var, const std::string& odir) {
  ensure_dir(odir);
  Eprp_var::Eprp_dict labels{
      {"odir", odir}
  };
  merge_sets(opts, "cgen", labels);  // e.g. --set cgen.srcmap=1
  run_step("inou.cgen.verilog", var, labels, opts, res);

  std::vector<std::string> names;
  names.reserve(var.graphs.size());
  for (const auto& g : var.graphs) {
    names.emplace_back(g->get_name());
  }
  std::sort(names.begin(), names.end());
  for (const auto& n : names) {
    auto f = std::format("{}/{}.v", odir, n);
    if (::access(f.c_str(), R_OK) != 0) {
      throw Lhd_error{"internal", std::format("inou.cgen.verilog did not produce {}", f), "check the step log in --workdir"};
    }
  }
  return names;
}

void emit_verilog_outputs(Options& opts, Result& res, Eprp_var& var) {
  bool wants_verilog = find_slot(opts.emits, "verilog") != nullptr || find_slot(opts.emit_dirs, "verilog") != nullptr;
  if (!wants_verilog) {
    return;
  }
  if (var.graphs.empty()) {
    throw Lhd_error{"config",
                    "no synthesizable modules to emit as verilog",
                    "the design produced no LGraphs (e.g. a pure-comptime pyrope program has no module IO)"};
  }

  for (const auto& e : opts.emit_dirs) {
    if (e.kind != "verilog") {
      continue;
    }
    auto                                          names = cgen_into(opts, res, var, e.path);
    std::vector<std::pair<std::string, uint64_t>> manifest;
    for (const auto& n : names) {
      std::ifstream      ifs(std::format("{}/{}.v", e.path, n));
      std::ostringstream oss;
      oss << ifs.rdbuf();
      manifest.emplace_back(n, hash_bytes(oss.str()));
    }
    write_manifest(e.path, "verilog", manifest);
    res.outputs.push_back(e.path);
  }

  for (const auto& e : opts.emits) {
    if (e.kind != "verilog") {
      continue;
    }
    // One declared file: per-module cgen into scratch, then a deterministic
    // (name-sorted) concatenation.
    auto          scratch = std::format("{}/cgen_{:03d}", workdir(opts), ++step_counter);
    auto          names   = cgen_into(opts, res, var, scratch);
    std::ofstream ofs(e.path);
    if (!ofs.is_open()) {
      throw Lhd_error{"config", std::format("could not write {}", e.path), ""};
    }
    for (const auto& n : names) {
      std::ifstream ifs(std::format("{}/{}.v", scratch, n));
      ofs << ifs.rdbuf();
    }
    res.outputs.push_back(e.path);
  }
}

// Pyrope source emission (LNAST -> .prp via pass.prp_writer). Variable
// cardinality is inherent (one .prp per unit), so only --emit-dir pyrope:DIR/
// is supported; validate_emits rejects the single-file form.
void emit_pyrope_outputs(Options& opts, Result& res, Eprp_var& var) {
  for (const auto& e : opts.emit_dirs) {
    if (e.kind != "pyrope") {
      continue;
    }
    if (var.lnasts.empty()) {
      throw Lhd_error{"unsupported",
                      "no LNAST units to emit as pyrope",
                      "pyrope output needs lnast/pyrope inputs (there is no LGraph -> LNAST decompiler)"};
    }
    ensure_dir(e.path);
    run_step("pass.prp_writer",
             var,
             {
                 {"odir", e.path}
    },
             opts,
             res);

    std::vector<std::string> names;
    names.reserve(var.lnasts.size());
    for (const auto& ln : var.lnasts) {
      names.emplace_back(ln->get_top_module_name());
    }
    std::sort(names.begin(), names.end());
    std::vector<std::pair<std::string, uint64_t>> manifest;
    for (const auto& n : names) {
      auto          f = std::format("{}/{}.prp", e.path, n);
      std::ifstream ifs(f);
      if (!ifs.is_open()) {
        throw Lhd_error{"internal", std::format("pass.prp_writer did not produce {}", f), "check the step log in --workdir"};
      }
      std::ostringstream oss;
      oss << ifs.rdbuf();
      manifest.emplace_back(n, hash_bytes(oss.str()));
    }
    write_manifest(e.path, "pyrope", manifest);
    res.outputs.push_back(e.path);
  }
}

// `--emit foo.prp` (single-file pyrope, inferred from the .prp extension).
// Pyrope re-emission is inherently one .prp per unit, so the single-file form
// is only valid for a one-unit design: emit that unit's .prp to the file; a
// multi-unit design must use --emit-dir pyrope:DIR/.
void emit_pyrope_single_file(Options& opts, Result& res, Eprp_var& var) {
  const auto* e = find_slot(opts.emits, "pyrope");
  if (e == nullptr) {
    return;
  }
  if (var.lnasts.empty()) {
    throw Lhd_error{"unsupported",
                    "no LNAST units to emit as pyrope",
                    "pyrope output needs source/ln: inputs (there is no LGraph -> LNAST decompiler)"};
  }
  std::vector<std::string> names;
  names.reserve(var.lnasts.size());
  for (const auto& ln : var.lnasts) {
    names.emplace_back(ln->get_top_module_name());
  }
  std::sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());
  if (names.size() != 1) {
    throw Lhd_error{"usage",
                    std::format("--emit pyrope:{} is a single file, but the design has {} units", e->path, names.size()),
                    "use --emit-dir pyrope:DIR/ for a multi-unit design (one .prp per unit)"};
  }
  auto scratch = std::format("{}/prp_{:03d}", workdir(opts), ++step_counter);
  ensure_dir(scratch);
  run_step("pass.prp_writer",
           var,
           {
               {"odir", scratch}
  },
           opts,
           res);
  auto          src = std::format("{}/{}.prp", scratch, names.front());
  std::ifstream ifs(src);
  if (!ifs.is_open()) {
    throw Lhd_error{"internal", std::format("pass.prp_writer did not produce {}", src), "check the step log in --workdir"};
  }
  std::ofstream ofs(e->path);
  if (!ofs.is_open()) {
    throw Lhd_error{"config", std::format("could not write {}", e->path), ""};
  }
  ofs << ifs.rdbuf();
  res.outputs.push_back(e->path);
}

// Depfile closure: a compiled unit's Source_locator file table is the
// list of source files its provenance actually references (its own file plus
// anything imports pulled in) — fold them into the depfile prerequisites.
void harvest_source_files(Result& res, const std::vector<std::shared_ptr<Lnast>>& units) {
  for (const auto& ln : units) {
    if (!ln) {
      continue;
    }
    const auto& sl = ln->source_locator();
    for (uint32_t fid = 0; fid < sl.file_count(); ++fid) {
      res.inputs.emplace_back(sl.file_path(fid));
    }
  }
  std::sort(res.inputs.begin(), res.inputs.end());
  res.inputs.erase(std::unique(res.inputs.begin(), res.inputs.end()), res.inputs.end());
}

void write_depfile(const Options& opts, Result& res) {
  if (opts.depfile.empty()) {
    return;
  }
  std::ofstream ofs(opts.depfile);
  if (!ofs.is_open()) {
    throw Lhd_error{"config", std::format("could not write depfile {}", opts.depfile), ""};
  }
  // Make syntax: every primary output depends on every declared input.
  // (`include discovery through the frontend is deferred to the source-map
  // work [1f]; today the prerequisites are the declared inputs.) Paths are
  // emitted cwd(exec-root)-relative per the contract — never absolute.
  auto relativize = [](const std::string& p) {
    std::error_code ec;
    auto            r = fs::proximate(p, ec);
    return (ec || r.empty()) ? p : r.string();
  };
  auto targets = res.outputs;
  if (targets.empty()) {
    targets.emplace_back("lhd.out");
  }
  for (auto& t : targets) {
    t = relativize(t);
  }
  auto prereqs = res.inputs;
  for (auto& p : prereqs) {
    p = relativize(p);
  }
  std::sort(prereqs.begin(), prereqs.end());
  bool first = true;
  for (const auto& t : targets) {
    if (!first) {
      ofs << ' ';
    }
    first = false;
    ofs << t;
  }
  ofs << ':';
  for (const auto& p : prereqs) {
    ofs << ' ' << p;
  }
  ofs << '\n';
  res.outputs.push_back(opts.depfile);
}

// ---- emit-kind validation ---------------------------------------------------

void reject_emit_kind(const Options& opts, std::string_view kind, const Lhd_error& err) {
  if (find_slot(opts.emits, kind) != nullptr || find_slot(opts.emit_dirs, kind) != nullptr) {
    throw err;
  }
}

void validate_emits(const Options& opts) {
  // Kinds with no implementation yet anywhere:
  reject_emit_kind(opts, "graphviz", {"unsupported", "--emit graphviz: is not implemented yet", ""});
  reject_emit_kind(opts, "metadata", {"unsupported", "--emit metadata: is not implemented yet (needs [3c] hashes)", ""});

  // ln:/lg:/lnast-dump: outputs are directory containers (a Forest dir, a
  // GraphLibrary dir, one file per unit) — directory form only, never a single
  // file. (pyrope: is allowed as a single file for a one-unit design; the
  // multi-unit check lives in emit_pyrope_single_file.)
  for (const auto& e : opts.emits) {
    if (e.kind == "ln" || e.kind == "lg" || e.kind == "lnast-dump") {
      throw Lhd_error{"usage",
                      std::format("--emit {0}:PATH is a directory container; use --emit-dir {0}:DIR/", e.kind),
                      "ln: is a Forest save dir, lg: a GraphLibrary save dir, lnast-dump: one file per unit"};
    }
  }

  bool has_ln_inputs = false;
  for (const auto& in : opts.in_dirs) {
    has_ln_inputs |= in.kind == "ln";
  }
  // Sources may arrive as positional files or, for the verilog readers, via
  // raw `-- -F filelist.f` args (no positional file then).
  const bool has_sources = !opts.files.empty() || !opts.raw_args.empty();

  // --reader slang emits the current func-style LNAST conventions (io node,
  // declare/store, lambda_kind), so lg:/verilog: emits lower through the same
  // upass+tolg pipeline as pyrope sources (todo/ 2s).
  if (opts.command == "compile" && opts.language == "verilog" && opts.reader != "slang") {
    for (const char* k : {"ln", "lnast-dump"}) {
      reject_emit_kind(opts,
                       k,
                       {"unsupported",
                        "the yosys-* readers elaborate to LGraphs (lg:), not LNAST",
                        "use --emit-dir lg:DIR/, or --reader slang (the direct SV -> LNAST front-end)"});
    }
    reject_emit_kind(opts,
                     "pyrope",
                     {"unsupported",
                      "there is no LGraph -> LNAST decompiler, so the yosys-* readers cannot re-emit pyrope",
                      "--reader slang elaborates SV to LNAST, which can"});
  }
  if (opts.command == "compile" && !has_ln_inputs && !has_sources) {
    // An lg:-only compile (no sources, no ln: inputs) has no LNAST on the pipe;
    // ln/pyrope/lnast-dump outputs need LNAST units, and there is no decompiler.
    for (const char* k : {"ln", "lnast-dump"}) {
      reject_emit_kind(opts, k, {"unsupported", "there is no LGraph -> LNAST decompiler", "ln: outputs need source/ln: inputs"});
    }
    reject_emit_kind(opts,
                     "pyrope",
                     {"unsupported", "there is no LGraph -> LNAST decompiler", "pyrope outputs need source/ln: inputs"});
  }
  if (opts.command == "lec") {
    for (const char* k : {"lg", "verilog", "ln", "pyrope", "lnast-dump"}) {
      reject_emit_kind(opts, k, {"usage", std::format("{} has no outputs beyond the result", opts.command), ""});
    }
  }
  if (opts.command == "tool") {
    for (const char* k : {"lg", "verilog", "ln", "pyrope", "lnast-dump"}) {
      reject_emit_kind(opts,
                       k,
                       {"usage",
                        "tool prints to stdout (redirect with `>`); it has no --emit outputs",
                        "use compile for declared artifacts"});
    }
  }
}

// --dump stage availability mirrors the emit-kind rules above: a dump is an
// observable, so a stage the command/reader cannot produce is an error, not
// a silent no-op.
void validate_dumps(const Options& opts) {
  if (opts.dumps.empty()) {
    return;
  }
  if (opts.command == "lec" || opts.command == "scan" || opts.command == "tool") {
    throw Lhd_error{"usage", std::format("{} has no --dump observables", opts.command), "--dump applies to compile"};
  }
  if (opts.language == "verilog" && opts.reader != "slang" && (wants_dump(opts, "parse") || wants_dump(opts, "lnast"))) {
    throw Lhd_error{"unsupported",
                    "the yosys-* readers elaborate to LGraphs, so there is no LNAST to dump",
                    "use --dump lg, or --reader slang (the direct SV -> LNAST front-end)"};
  }
  // compile from IR inputs has no front-end parse, and only ln: inputs carry
  // LNAST: --dump parse needs sources, --dump lnast needs sources or ln:.
  const bool has_sources = !opts.files.empty() || !opts.raw_args.empty();
  bool       has_ln      = false;
  for (const auto& in : opts.in_dirs) {
    has_ln |= in.kind == "ln";
  }
  if (!has_sources && wants_dump(opts, "parse")) {
    throw Lhd_error{"usage", "--dump parse needs source files (an ln:/lg: input is already parsed)", ""};
  }
  if (!has_sources && !has_ln && wants_dump(opts, "lnast")) {
    throw Lhd_error{"usage", "--dump lnast needs source files or ln: inputs (an lg: input carries no LNAST)", ""};
  }
}

// ---- elaborate --------------------------------------------------------------

std::vector<std::shared_ptr<Lnast>> filter_top(const std::vector<std::shared_ptr<Lnast>>& units, const std::string& top) {
  if (top.empty()) {
    return units;
  }
  std::vector<std::shared_ptr<Lnast>> out;
  std::vector<std::string>            names;
  for (const auto& ln : units) {
    names.emplace_back(ln->get_top_module_name());
    if (ln->get_top_module_name() == top) {
      out.push_back(ln);
    }
  }
  if (out.empty()) {
    throw Lhd_error{"config",
                    std::format("--top {} not found among elaborated units", top),
                    std::format("available units: {}", join_csv(names))};
  }
  return out;
}

// Synthesize the `<unit>.__pub` wrapper tree: the durable,
// atomically-published pub list of a file unit. Self-describing body shape
// (walkable without evaluation; valid plain LNAST):
//   value leaves: store(ref '<name>[.field]', const <pyrope-text>)
//                 — the folded comptime leaves stamped by uPass_constprop
//   lambdas:      attr_set(ref '<name>', const '__pub', const '<kind>')
//                 store(ref '<name>', const 'ln:<unit>.<name>')
// Returns nullptr when the unit exports nothing.
std::shared_ptr<Lnast> synthesize_pub_wrapper(const std::shared_ptr<Lnast>& ln) {
  const auto& pubs = ln->get_pub_list();
  if (pubs.empty()) {
    return nullptr;
  }
  const std::string unit{ln->get_top_module_name()};
  auto              w            = std::make_shared<Lnast>(unit + ".__pub");
  auto              root         = w->set_root(Lnast_ntype::create_top());
  auto              stmts        = w->add_child(root, Lnast_ntype::create_stmts());
  // Wrapper nodes anchor at their pub declaration — re-mint the pub
  // decl's SourceId (recorded by prp2lnast) into the wrapper's own locator.
  auto              pub_srcid_of = [&](std::string_view name) -> hhds::SourceId {
    for (const auto& p : pubs) {
      if (p.name == name && p.srcid != hhds::SourceId_invalid) {
        return w->source_locator().import_from(ln->source_locator(), p.srcid);
      }
    }
    return hhds::SourceId_invalid;
  };
  for (const auto& [path, text] : ln->get_pub_values()) {
    auto s = w->add_child(stmts, Lnast_ntype::create_store());
    w->set_srcid(s, pub_srcid_of(std::string_view(path).substr(0, path.find('.'))));
    w->add_child(s, Lnast_node::create_ref(path));
    w->add_child(s, Lnast_node::create_const(text));
  }
  for (const auto& p : pubs) {
    if (p.kind == "value") {
      continue;
    }
    const auto wid = pub_srcid_of(p.name);
    auto       a   = w->add_child(stmts, Lnast_ntype::create_attr_set());
    w->set_srcid(a, wid);
    w->add_child(a, Lnast_node::create_ref(p.name));
    w->add_child(a, Lnast_node::create_const("__pub"));
    w->add_child(a, Lnast_node::create_const(std::format("'{}'", p.kind)));
    auto s = w->add_child(stmts, Lnast_ntype::create_store());
    w->set_srcid(s, wid);
    w->add_child(s, Lnast_node::create_ref(p.name));
    w->add_child(s, Lnast_node::create_const(std::format("'ln:{}.{}'", unit, p.name)));
  }
  return w;
}

// The publishable unit set of the given source units: each source
// plus every derived tree named "<src>.<entity>" (func_extract naming —
// extracted lambdas, and the `.__pub` wrapper once synthesized).
std::vector<std::shared_ptr<Lnast>> collect_source_derived(const std::vector<std::shared_ptr<Lnast>>& all,
                                                           const std::vector<std::shared_ptr<Lnast>>& sources) {
  absl::flat_hash_set<std::string> roots;
  for (const auto& ln : sources) {
    roots.emplace(ln->get_top_module_name());
  }
  std::vector<std::shared_ptr<Lnast>> out;
  for (const auto& ln : all) {
    std::string name{ln->get_top_module_name()};
    if (roots.contains(name)) {
      out.push_back(ln);
      continue;
    }
    // "<src>.<entity>" — match against every root (a unit name may itself
    // contain dots, so a single find('.') split would mis-bucket).
    for (const auto& r : roots) {
      if (name.size() > r.size() + 1 && name.compare(0, r.size(), r) == 0 && name[r.size()] == '.') {
        out.push_back(ln);
        break;
      }
    }
  }
  return out;
}

// Partition the typed IR inputs of elaborate/synth: ln: dirs and lg: dirs.
struct Ir_inputs {
  std::vector<std::string> ln_dirs;
  std::vector<std::string> lg_dirs;
};

Ir_inputs gather_ir_inputs(const Options& opts, std::string_view cmd) {
  Ir_inputs ir;
  for (const auto& in : opts.in_dirs) {
    if (in.kind == "ln") {
      ir.ln_dirs.push_back(in.path);
    } else {
      throw Lhd_error{"usage", std::format("{} does not accept {}: dir inputs", cmd, in.kind), ""};
    }
  }
  for (const auto& in : opts.ins) {
    if (in.kind == "lg") {
      ir.lg_dirs.push_back(in.path);
    } else {
      throw Lhd_error{"usage", std::format("{} does not accept {}: inputs", cmd, in.kind), "IR inputs are ln:DIR or lg:DIR"};
    }
  }
  return ir;
}

// Pyrope parse phase: load ln: import units (visible to upass/inliner), then
// parse+validate the source files. Returns the number of imported units (the
// source units are var.lnasts[n_imports..]).
size_t pyrope_parse(Options& opts, Result& res, Eprp_var& var, const std::vector<std::string>& ln_import_dirs) {
  check_inputs_exist(opts.files);
  res.inputs = opts.files;

  size_t n_imports = 0;
  for (const auto& d : ln_import_dirs) {
    res.inputs.push_back(d);
    for (auto& ln : load_ln_dir(d)) {
      var.add(ln);
      ++n_imports;
    }
  }

  run_step("inou.prp",
           var,
           {
               {"files", join_csv(opts.files)}
  },
           opts,
           res);
  run_step("pass.lnastfmt", var, {}, opts, res);
  if (wants_dump(opts, "parse")) {  // this invocation's source units only (imports are pre-elaborated)
    screen_dump_lnasts(std::vector<std::shared_ptr<Lnast>>(var.lnasts.begin() + static_cast<long>(n_imports), var.lnasts.end()),
                       "post-parse");
  }
  return n_imports;
}

// Frontend half of verilog: source files -> LGraphs in the library at
// `lib_path` (and on var.graphs). Returns the library path used.
std::string verilog_frontend(Options& opts, Result& res, Eprp_var& var) {
  check_inputs_exist(opts.files);
  res.inputs = opts.files;

  const auto* d_out    = find_slot(opts.emit_dirs, "lg");
  std::string lib_path = d_out ? d_out->path : workdir(opts) + "/lgdb";

  Eprp_var::Eprp_dict labels{
      {    "path",                                                                       lib_path},
      {     "top",                         opts.top.empty() ? std::string{"-auto-top"} : opts.top},
      {"frontend", opts.reader == "yosys-verilog" ? std::string{"verilog"} : std::string{"slang"}}
  };
  // An empty `files` label is path-validated (and rejected) by Eprp_var::add, so
  // only set it when there are positional sources; with `-- -F filelist.f` the
  // sources ride in slang_flags instead.
  if (!opts.files.empty()) {
    labels["files"] = join_csv(opts.files);
  }
  if (!opts.raw_args.empty()) {
    // Join with '\x1f' (ASCII unit separator) — a comma is lossy for shell
    // argv tokens like +incdir+a,b; inou_yosys_api splits on '\x1f' when seen.
    std::string joined;
    for (const auto& arg : opts.raw_args) {
      if (!joined.empty()) {
        joined += '\x1f';
      }
      joined += arg;
    }
    labels["slang_flags"] = joined;
  }

  run_step("inou.yosys.tolg", var, labels, opts, res);

  if (var.graphs.empty()) {
    throw Lhd_error{"internal", "verilog elaboration produced no graphs", "check the step log in --workdir"};
  }
  return lib_path;
}

// --reader slang: the direct inou.slang SV -> LNAST front-end. From here the
// design is LNAST units, so the rest of the flow is the pyrope one (lnastfmt,
// upass, emit-gated tolg). The yosys-* readers above elaborate to LGraphs.
void slang_parse(Options& opts, Result& res, Eprp_var& var) {
  check_inputs_exist(opts.files);
  res.inputs = opts.files;

  Eprp_var::Eprp_dict labels;
  // An empty `files` label is rejected by Eprp_var::add (it validates each
  // comma split path), so only set it when there are positional sources.
  if (!opts.files.empty()) {
    labels["files"] = join_csv(opts.files);
  }
  // Forward --top so inou.slang elaborates only that module's hierarchy (it
  // otherwise auto-tops every uninstantiated module, e.g. a sim/difftest top).
  if (!opts.top.empty() && opts.top != "-auto-top") {
    labels["top"] = opts.top;
  }
  // Raw `--` args ride straight to the slang driver (e.g. `-F filelist.f` to
  // read a file list). Join with '\x1f' (ASCII unit separator) so shell argv
  // tokens like `+incdir+a,b` survive; inou.slang splits on '\x1f'. With a
  // `-F`/`-f` file list the explicit `files` may be empty.
  if (!opts.raw_args.empty()) {
    std::string joined;
    for (const auto& arg : opts.raw_args) {
      if (!joined.empty()) {
        joined += '\x1f';
      }
      joined += arg;
    }
    labels["slang_flags"] = joined;
  }

  run_step("inou.slang", var, labels, opts, res);
  run_step("pass.lnastfmt", var, {}, opts, res);
  if (wants_dump(opts, "parse")) {
    screen_dump_lnasts(var.lnasts, "post-parse");
  }
}

void lower_lnasts(Options& opts, Result& res, Eprp_var& var, const std::string& lib_path, bool need_graphs);  // fwd

// True when any requested observable needs LGraphs (gates the tolg lowering —
// the CLI-level equivalent of pass.upass's tolg:0|1 toggle). A --dump lg is
// an observable like an emit, so it pulls the stage in too.
bool emits_need_graphs(const Options& opts) {
  return find_slot(opts.emit_dirs, "lg") != nullptr || find_slot(opts.emits, "verilog") != nullptr
         || find_slot(opts.emit_dirs, "verilog") != nullptr || wants_dump(opts, "lg");
}

// True when any requested observable consumes the post-upass LNAST (gates the
// toln materialization — the dual of emits_need_graphs for pass.upass's
// toln:0|1). --dump lnast prints that tree, so it counts; ln.cat/ln.diff
// print/compare it, so the commands count too.
bool emits_need_lnast(const Options& opts) {
  return find_slot(opts.emit_dirs, "ln") != nullptr || find_slot(opts.emit_dirs, "pyrope") != nullptr
         || find_slot(opts.emit_dirs, "lnast-dump") != nullptr || wants_dump(opts, "lnast") || opts.command == "tool";
}

// ---- scan (pyrope import/dependency discovery) -------------------------------

// Imports are comptime string literals (see the LiveHD docs),
// so the strings are statically extractable from the parse: every LNAST
// func_call of the form (target, const "import", const "<module>"). The list
// is a conservative over-approximation — an import under `if cond` may be
// constprop-dead, which only elaboration can tell.
std::vector<std::string> collect_imports(const std::shared_ptr<Lnast>& ln) {
  std::vector<std::string> out;
  for (const auto& nid : ln->tree().pre_order()) {
    if (!Lnast_ntype::is_func_call(ln->get_type(nid))) {
      continue;
    }
    auto target = ln->get_first_child(nid);
    if (target.is_invalid()) {
      continue;
    }
    auto fname = ln->get_sibling_next(target);
    if (fname.is_invalid() || ln->get_name(fname) != "import") {
      continue;
    }
    auto mod = ln->get_sibling_next(fname);
    if (mod.is_invalid()) {
      continue;
    }
    std::string text{ln->get_name(mod)};
    // The module argument is the source text of a string literal.
    if (text.size() >= 2 && (text.front() == '"' || text.front() == '\'') && text.back() == text.front()) {
      text = text.substr(1, text.size() - 2);
    }
    if (!text.empty()) {
      out.push_back(text);
    }
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

std::string json_escape_min(std::string_view s) {
  // Full JSON string escaping: a raw newline/tab/control byte (e.g. in a unit
  // name or import string) would otherwise produce invalid JSON. Mirrors
  // core/diag.cpp json_escape.
  std::string out;
  for (char c : s) {
    switch (c) {
      case '"' : out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

// `lhd scan FILES...` — emit each pyrope file's import strings (raw, as
// written; resolution lands with the task_1m_plan.md resolver). The payload
// rides the result envelope as the "scan" member, so BUILD generators
// (gazelle-style) and depfile writers can consume one JSON object.
void scan_command(Options& opts, Result& res) {
  setup_diag(opts, "scan");
  if (opts.files.empty()) {
    throw Lhd_error{"usage", "scan requires at least one .prp file", ""};
  }
  if (!opts.emits.empty() || !opts.emit_dirs.empty() || !opts.ins.empty() || !opts.in_dirs.empty()) {
    throw Lhd_error{"usage", "scan takes no --emit/--in slots; its output is the result's \"scan\" member", ""};
  }
  check_inputs_exist(opts.files);
  res.inputs = opts.files;

  std::string payload = "[";
  bool        first   = true;
  for (const auto& f : opts.files) {
    auto base = fs::path(f).stem().string();

    std::shared_ptr<Lnast> ln;
    {
      Stdout_to_log redirect(next_log_path(opts, "scan.prp"));
      Prp2lnast     conv(f, base);
      ln = conv.get_lnast();
    }
    if (livehd::diag::sink().has_errors()) {
      throw classify_engine_failure(std::format("scan: {} does not parse", f));
    }

    if (!first) {
      payload += ',';
    }
    first        = false;
    payload     += std::format("{{\"file\":\"{}\",\"imports\":[", json_escape_min(f));
    bool ifirst  = true;
    for (const auto& imp : collect_imports(ln)) {
      if (!ifirst) {
        payload += ',';
      }
      ifirst   = false;
      payload += std::format("\"{}\"", json_escape_min(imp));
    }
    payload += "]}";
  }
  payload       += "]";
  res.scan_json  = payload;
}

// ---- ln.cat / ln.diff (LNAST debug tools; payload on stdout) -----------------

// One ln.cat/ln.diff input set: .prp or .v/.sv sources and/or ln: Forest dirs.
struct Ln_inputs {
  std::vector<std::string> prp_files;
  std::vector<std::string> sv_files;
  std::vector<std::string> ln_dirs;
};

Ln_inputs classify_ln_inputs(const std::vector<std::string>& tokens, std::string_view cmd) {
  Ln_inputs in;
  for (const auto& t : tokens) {
    if (auto pos = t.find(':'); pos != std::string::npos && pos != 0) {
      auto kind = t.substr(0, pos);
      if (kind == "ln" || kind == "lnast") {
        in.ln_dirs.push_back(t.substr(pos + 1));
        continue;
      }
      if (kind == "lg" || kind == "design" || kind == "lgraph") {
        throw Lhd_error{"usage",
                        std::format("{} reads LNAST, not lg: graph libraries", cmd),
                        "inputs are .prp/.v/.sv sources or ln:DIR forests"};
      }
      // Explicit source schemes (URL-like, like the .prp/.v/.sv shortcuts below):
      // the scheme is authoritative, so strip it and route by language.
      if (kind == "pyrope") {
        in.prp_files.push_back(t.substr(pos + 1));
        continue;
      }
      if (kind == "verilog") {
        in.sv_files.push_back(t.substr(pos + 1));
        continue;
      }
      // any other prefix: treat as a plain path (mirror route_positional)
    }
    std::string_view sv{t};
    if (sv.ends_with(".prp")) {
      in.prp_files.push_back(t);  // pyrope: shortcut
    } else if (sv.ends_with(".v") || sv.ends_with(".sv")) {
      in.sv_files.push_back(t);  // verilog: shortcut
    } else {
      throw Lhd_error{"usage",
                      std::format("{}: cannot classify input '{}'", cmd, t),
                      "inputs are .prp sources, .v/.sv sources (inou.slang), or ln:DIR forests"};
    }
  }
  return in;
}

// Elaborate one input set to LNAST units. Sources run the front-end + upass
// (the post-upass tree, like --dump lnast); ln: dirs load as stored when they
// are the whole set, and join sources as pre-elaborated imports otherwise
// (the elaborate convention — imports are not returned).
std::vector<std::shared_ptr<Lnast>> ln_tool_units(Options& opts, Result& res, const Ln_inputs& in) {
  if (!in.prp_files.empty() && !in.sv_files.empty()) {
    throw Lhd_error{"usage", "cannot mix pyrope and verilog sources in one input set", "split into two ln: elaborations"};
  }

  Eprp_var var;
  size_t   n_imports = 0;
  for (const auto& d : in.ln_dirs) {
    res.inputs.push_back(d);
    for (auto& ln : load_ln_dir(d)) {
      var.add(ln);
      ++n_imports;
    }
  }

  if (in.prp_files.empty() && in.sv_files.empty()) {
    if (in.ln_dirs.empty()) {
      throw Lhd_error{"usage", "no inputs", "pass .prp/.v/.sv sources or ln:DIR forests"};
    }
    return var.lnasts;  // pure ln: load — show/compare what was stored
  }

  const auto& files = in.prp_files.empty() ? in.sv_files : in.prp_files;
  check_inputs_exist(files);
  for (const auto& f : files) {
    res.inputs.push_back(f);
  }
  run_step(in.prp_files.empty() ? "inou.slang" : "inou.prp",
           var,
           {
               {"files", join_csv(files)}
  },
           opts,
           res);
  lower_lnasts(opts, res, var, workdir(opts) + "/lgdb", /*need_graphs=*/false);  // lnastfmt + upass; no tolg

  return std::vector<std::shared_ptr<Lnast>>(var.lnasts.begin() + static_cast<long>(n_imports), var.lnasts.end());
}

std::vector<std::shared_ptr<Lnast>> sorted_by_name(std::vector<std::shared_ptr<Lnast>> units) {
  std::sort(units.begin(), units.end(), [](const auto& a, const auto& b) {
    return a->get_top_module_name() < b->get_top_module_name();
  });
  return units;
}

// Dump text as lines with the trailing " @(...)" attribute suffix stripped:
// loc/fname differ across front-ends and revisions, and the tree-edit cost
// below ignores them too, so the line diff must not flag them. The leading
// module-name line is dropped too (the pair header already names both units,
// and unit names embed the file stem).
std::vector<std::string> dump_lines_no_attrs(const std::shared_ptr<Lnast>& ln) {
  std::ostringstream oss;
  ln->dump(oss);
  std::vector<std::string> lines;
  std::istringstream       iss(oss.str());
  std::string              line;
  bool                     first = true;
  while (std::getline(iss, line)) {
    if (first) {
      first = false;
      continue;
    }
    if (auto pos = line.find(" @("); pos != std::string::npos) {
      line.resize(pos);
    }
    lines.push_back(std::move(line));
  }
  return lines;
}

// Minimal LCS line diff: -/+ lines with 2 lines of kept context per hunk.
void print_line_diff(std::string& out, const std::vector<std::string>& a, const std::vector<std::string>& b, size_t ctx = 2) {
  const size_t n = a.size();
  const size_t m = b.size();
  if (n * m > size_t{16} * 1024 * 1024) {
    out += "  (trees too large for a line diff; see tree-edit-distance below)\n";
    return;
  }
  // lcs[i][j] = LCS length of a[i..] / b[j..]
  std::vector<uint32_t> lcs((n + 1) * (m + 1), 0);
  auto                  at = [&](size_t i, size_t j) -> uint32_t& { return lcs[i * (m + 1) + j]; };
  for (size_t i = n; i-- > 0;) {
    for (size_t j = m; j-- > 0;) {
      at(i, j) = (a[i] == b[j]) ? at(i + 1, j + 1) + 1 : std::max(at(i + 1, j), at(i, j + 1));
    }
  }
  // ops: ' ' keep, '-' only-in-a, '+' only-in-b
  std::vector<std::pair<char, const std::string*>> ops;
  for (size_t i = 0, j = 0; i < n || j < m;) {
    if (i < n && j < m && a[i] == b[j]) {
      ops.emplace_back(' ', &a[i]);
      ++i, ++j;
    } else if (j < m && (i == n || at(i, j + 1) >= at(i + 1, j))) {
      ops.emplace_back('+', &b[j]);
      ++j;
    } else {
      ops.emplace_back('-', &a[i]);
      ++i;
    }
  }
  const size_t      kCtx = ctx;
  std::vector<bool> show(ops.size(), false);
  for (size_t k = 0; k < ops.size(); ++k) {
    if (ops[k].first == ' ') {
      continue;
    }
    size_t lo = k >= kCtx ? k - kCtx : 0;
    size_t hi = std::min(ops.size(), k + kCtx + 1);
    for (size_t x = lo; x < hi; ++x) {
      show[x] = true;
    }
  }
  bool in_gap = false;
  for (size_t k = 0; k < ops.size(); ++k) {
    if (!show[k]) {
      if (!in_gap) {
        out    += "  ...\n";
        in_gap  = true;
      }
      continue;
    }
    in_gap  = false;
    out    += ops[k].first;
    out    += ' ';
    out    += *ops[k].second;
    out    += '\n';
  }
}

// `lhd tool cat ln:…` — the former ln.cat: bare Lnast::dump concatenation of
// every selected unit. `tokens` are the input tokens (the verb stripped).
void tool_cat_ln(Options& opts, Result& res, const std::vector<std::string>& tokens) {
  auto in = classify_ln_inputs(tokens, "tool cat");
  for (const auto& d : opts.in_dirs) {  // --in-dir ln:DIR spelling
    if (d.kind == "ln") {
      in.ln_dirs.push_back(d.path);
    }
  }
  auto units = sorted_by_name(filter_top(ln_tool_units(opts, res, in), opts.top));
  for (const auto& ln : units) {  // bare Lnast::dump concatenation (true cat)
    std::ostringstream oss;
    ln->dump(oss);
    auto s = oss.str();
    std::fwrite(s.data(), 1, s.size(), stdout);
  }
  std::fflush(stdout);
}

// `lhd tool diff ln:a ln:b` — the former ln.diff: Zhang-Shasha tree-edit
// distance + line diff over the two LNAST forests. `tokens` are the two inputs.
void tool_diff_ln(Options& opts, Result& res, const std::vector<std::string>& tokens) {
  if (tokens.size() != 2) {
    throw Lhd_error{"usage",
                    "tool diff (ln) takes exactly two inputs (each a .prp/.v/.sv source or an ln:DIR forest)",
                    "e.g. `lhd tool diff old.prp new.prp` or `lhd tool diff ln:before/ x.prp`"};
  }
  auto a_units = sorted_by_name(filter_top(ln_tool_units(opts, res, classify_ln_inputs({tokens[0]}, "tool diff")), opts.top));
  auto b_units = sorted_by_name(filter_top(ln_tool_units(opts, res, classify_ln_inputs({tokens[1]}, "tool diff")), opts.top));

  auto names_of = [](const std::vector<std::shared_ptr<Lnast>>& units) {
    std::vector<std::string> names;
    names.reserve(units.size());
    for (const auto& ln : units) {
      names.emplace_back(ln->get_top_module_name());
    }
    return names;
  };
  if (a_units.size() != b_units.size()) {
    throw Lhd_error{"config",
                    std::format("unit count mismatch: {} has [{}], {} has [{}]",
                                tokens[0],
                                join_csv(names_of(a_units)),
                                tokens[1],
                                join_csv(names_of(b_units))),
                    "use --top NAME to select one unit per side"};
  }

  // Pair sorted-by-name positionally (unit names embed the file stem, so
  // exact name matching would never pair two differently-named files of the
  // same design). The header shows which units got compared.
  std::string out;
  double      total = 0.0;
  for (size_t k = 0; k < a_units.size(); ++k) {
    const auto& la  = a_units[k];
    const auto& lb  = b_units[k];
    out            += std::format("//---- tool diff {} vs {}\n", la->get_top_module_name(), lb->get_top_module_name());

    // The hhds tree diff (Zhang-Shasha edit distance) on the live trees:
    // nodes match on (lnast type, name) — loc/fname attrs are ignored.
    auto cost_fn = [&la, &lb](const hhds::Tree::Node_class& x, const hhds::Tree::Node_class& y) -> double {
      if (la->get_type(x) != lb->get_type(y)) {
        return 1.0;
      }
      return la->get_name(x) == lb->get_name(y) ? 0.0 : 1.0;
    };
    auto ted  = hhds::TreeEditDistance::compute(la->tree_ptr(), lb->tree_ptr(), hhds::EditCosts{}, cost_fn);
    total    += ted.distance;

    if (ted.distance == 0.0) {
      out += "identical\n";
    } else {
      print_line_diff(out, dump_lines_no_attrs(la), dump_lines_no_attrs(lb), static_cast<size_t>(opts.tool_context));
    }
    out += std::format("tree-edit-distance: {}\n", ted.distance);
  }
  if (a_units.size() > 1) {
    out += std::format("//---- total tree-edit-distance over {} unit pairs: {}\n", a_units.size(), total);
  }
  std::fwrite(out.data(), 1, out.size(), stdout);
  std::fflush(stdout);
  res.recipe_steps.emplace_back("hhds.tree_edit_distance");
}

// elaborate — build the design database from sources and/or IR inputs:
//   sources (.prp [+ ln: imports] | .v)  -> --emit-dir ln:DIR and/or lg:DIR
//   ln: dirs only (aggregate)            -> one combined ln:/lg: container
//   one lg: dir (pass-through copy)      -> lg:DIR
// ---- synth ------------------------------------------------------------------

// Lower LNAST units: lnastfmt + upass, then (only when `need_graphs`) the
// terminal LNAST->LGraph sub-pass into the library at lib_path — the
// CLI-level tolg:0|1 gate, derived from the requested emits.
void lower_lnasts(Options& opts, Result& res, Eprp_var& var, const std::string& lib_path, bool need_graphs) {
  run_step("pass.lnastfmt", var, {}, opts, res);

  // The verifier runs by default: it prints `cputs` and, crucially, turns a
  // comptime-false `cassert` into a compile error during elaborate/synth. No
  // verifier_pass / verifier_fail expectation is passed here, so it runs in
  // quiet mode (no count tally) — every cassert that resolves must hold, an
  // unknown / deferred cassert is left for later. A user can still silence it
  // with `--set upass.verifier=false`.
  Eprp_var::Eprp_dict up{
      {"constprop",    "1"},
      { "verifier", "true"}
  };
  // Derived toln gate (the dual of the emit-derived tolg gate): when neither
  // the lnast.tolg stage below (need_graphs) nor any post-upass LNAST emit
  // (ln:/pyrope:/lnast-dump:) consumes the rewritten tree, skip materializing
  // it — pass.upass toln:0 drops the whole staging build, the post-walk DCE,
  // and the coalescer; every pass still dispatches, so diagnostics are
  // unchanged. An explicit `--set upass.toln=…` (merged below) overrides.
  if (!need_graphs && !emits_need_lnast(opts)) {
    up["toln"] = "0";
  }
  merge_sets(opts, "upass", up);

  // Iterate until converged. Units may import each other in any
  // order (no topological pre-ordering; liveness needs constprop), so:
  // each round runs pass.upass over everything; a file whose walk hit an
  // unresolved LIVE import retries WHOLESALE next round from its pristine
  // post-lnastfmt body — by then the exporter has published (its folded pub
  // values ride its Lnast). A round with no progress fails, listing every
  // blocked file with its unresolved import strings (covers both true
  // cycles and missing units). Import-free invocations take the single-pass
  // fast path (no clones, no defer mode).
  bool imports_present = false;
  for (const auto& ln : var.lnasts) {
    if (!collect_imports(ln).empty()) {
      imports_present = true;
      break;
    }
  }
  if (!imports_present) {
    run_step("pass.upass", var, up, opts, res);
  } else {
    up["import_defer"] = "1";
    // Pristine (post-lnastfmt, pre-upass) bodies for the whole-file retry.
    absl::flat_hash_map<std::string, std::shared_ptr<hhds::Tree>> pristine;
    for (const auto& ln : var.lnasts) {
      pristine.emplace(std::string(ln->get_top_module_name()), ln->tree_ptr()->clone());
    }
    std::map<std::string, std::set<std::string>> prev_blocked;
    while (true) {
      run_step("pass.upass", var, up, opts, res);
      if (var.unresolved_imports.empty()) {
        break;
      }
      // Map each blocked unit (possibly an extracted body `file.fn`) to its
      // retryable file-level unit — the longest pristine name prefixing it.
      std::map<std::string, std::set<std::string>> blocked;
      for (const auto& [unit, text] : var.unresolved_imports) {
        std::string file;
        for (const auto& [pname, _] : pristine) {
          const bool prefix
              = unit == pname
                || (unit.size() > pname.size() + 1 && unit.compare(0, pname.size(), pname) == 0 && unit[pname.size()] == '.');
          if (prefix && pname.size() > file.size()) {
            file = pname;
          }
        }
        blocked[file.empty() ? unit : file].insert(text);
      }
      if (blocked == prev_blocked) {
        std::string units_avail;
        for (const auto& [pname, _] : pristine) {
          if (!units_avail.empty()) {
            units_avail += ", ";
          }
          units_avail += pname;
        }
        for (const auto& [file, texts] : blocked) {
          std::string list;
          for (const auto& t : texts) {
            if (!list.empty()) {
              list += ", ";
            }
            list += '"' + t + '"';
          }
          livehd::diag::sink().emit(
              livehd::diag::Diagnostic{.severity = livehd::diag::Severity::error,
                                       .code     = "import-no-progress",
                                       .category = "name",
                                       .pass     = "lhd.elaborate",
                                       .message  = std::format("unit `{}` is blocked on unresolved import(s): {}", file, list),
                                       .hint = std::format("an import cycle or a missing unit; available units: {}", units_avail)});
        }
        throw classify_engine_failure("import resolution made no progress");
      }
      prev_blocked = std::move(blocked);
      // Whole-file retry: restore each blocked file's pristine body and drop
      // its round-derived trees so re-extraction doesn't duplicate units.
      for (const auto& [file, _] : prev_blocked) {
        auto pit = pristine.find(file);
        if (pit == pristine.end()) {
          continue;
        }
        for (const auto& ln : var.lnasts) {
          if (ln->get_top_module_name() == file) {
            ln->replace_body(pit->second->clone());
            ln->set_pub_values({});
            break;
          }
        }
        std::erase_if(var.lnasts, [&](const std::shared_ptr<Lnast>& ln) {
          std::string name{ln->get_top_module_name()};
          return name.size() > file.size() + 1 && name.compare(0, file.size(), file) == 0 && name[file.size()] == '.'
                 && !pristine.contains(name);  // derived this invocation, not a loaded unit
        });
      }
    }
  }

  if (wants_dump(opts, "lnast")) {
    screen_dump_lnasts(var.lnasts, "post-upass");
  }

  if (!need_graphs) {
    return;  // no lg/verilog emit requested -> skip the tolg lowering
  }
  {
    Stdout_to_log redirect(next_log_path(opts, "lnast.tolg"));
    // 2f-lg: reject two units pinned to the same artifact name (lg="…")
    // before any GraphIO is created.
    uPass_tolg::detect_lg_collisions(var.lnasts);
    // Two-phase: register every module's GraphIO first so call
    // sites can bind callee GraphIOs (Sub instances) regardless of order.
    for (const auto& ln : var.lnasts) {
      uPass_tolg::register_io(ln, lib_path, var.lnasts);
    }
    // The reset_style elaboration flag rides the upass set
    // (`--set upass.reset_style=async`); tolg is its only consumer.
    std::string reset_style = "sync";
    if (auto it = up.find("reset_style"); it != up.end() && !it->second.empty()) {
      reset_style = it->second;
    }
    for (const auto& ln : var.lnasts) {
      auto g = uPass_tolg::run(ln, lib_path, var.lnasts, reset_style);
      if (g) {
        var.add(g);
      }
    }
    thread_pool.wait_all();
  }
  res.recipe_steps.emplace_back("lnast.tolg");
  if (livehd::diag::sink().has_errors()) {
    throw classify_engine_failure("lnast.tolg reported errors");
  }
}

// Graph half shared by synth and compile: recipe passes + typed emits.
// `lib_path` is the library the graphs in `var` live in ("" when there are
// no graphs, e.g. a pure-LNAST run).
void graph_pipeline_and_emits(Options& opts, Result& res, Eprp_var& var, const std::string& lib_path) {
  check_known_set_passes(opts);
  for (const auto& [set_name, method] : recipe_graph_passes(opts, "O1")) {
    if (var.graphs.empty()) {
      break;  // nothing to optimize (validated below if an emit needs graphs)
    }
    Eprp_var::Eprp_dict labels;
    merge_sets(opts, set_name, labels);
    run_step(method, var, labels, opts, res);
  }

  if (wants_dump(opts, "lg")) {
    screen_dump_graphs(var, "post-recipe");
  }

  if (const auto* lg_out = find_slot(opts.emit_dirs, "lg"); lg_out != nullptr) {
    if (lib_path.empty() || var.graphs.empty()) {
      throw Lhd_error{"config", "no graphs to save into --emit-dir lg:", "the input produced no synthesizable modules"};
    }
    // By construction lib_path == the lg output dir whenever one was
    // declared (tolg/copy targeted it), so saving the library is the emit.
    livehd::Hhds_graph_library::save(lib_path);
    res.outputs.push_back(lg_out->path);
  }

  emit_verilog_outputs(opts, res, var);
  // ln: emit is handled per-path by the caller (source publish vs plain forest),
  // so it is NOT done here.
  emit_pyrope_outputs(opts, res, var);             // --emit-dir pyrope:DIR/ (one .prp per unit)
  emit_pyrope_single_file(opts, res, var);         // --emit foo.prp (single-unit design)
  emit_lnast_dump_outputs(var.lnasts, opts, res);  // post-upass textual dump (debug/test observable)
}

// First-elaboration `ln:` publish from pyrope sources: the source-derived
// units (each source plus its extracted lambdas — the `ln:<unit>.<entity>` url
// targets) plus a synthesized `<unit>.__pub` wrapper per pub-exporting file.
// Imports are pre-elaborated and keep their own `ln:` dir, so they are
// excluded (var.lnasts[n_imports..] is this invocation's source set).
void publish_source_ln(Options& opts, Result& res, Eprp_var& var, size_t n_imports, const std::string& dir) {
  std::vector<std::shared_ptr<Lnast>> source_units(var.lnasts.begin() + static_cast<long>(n_imports), var.lnasts.end());
  auto                                publish = collect_source_derived(var.lnasts, filter_top(source_units, opts.top));
  // Publication happens at completion (never a partial pub list): the upass
  // above either finished every unit or threw.
  std::vector<std::shared_ptr<Lnast>> wrappers;
  for (const auto& ln : publish) {
    if (ln->get_lambda_kind().empty() && !ln->get_top_module_name().ends_with(".__pub")) {
      if (auto w = synthesize_pub_wrapper(ln)) {
        wrappers.push_back(std::move(w));
      }
    }
  }
  publish.insert(publish.end(), wrappers.begin(), wrappers.end());
  save_ln_dir(opts, res, publish, dir);
}

// IR-only compile: ln:DIR forests and/or lg:DIR libraries, no sources. ln:
// dirs are re-lowered (lnastfmt + upass + tolg); a single lg: dir is loaded.
// Then the shared graph pipeline (recipe + emits) runs. (The ln: + lg: linker
// has its own path — see compile_link_ir.)
void compile_ir(Options& opts, Result& res, const Ir_inputs& ir) {
  if (ir.lg_dirs.size() > 1) {
    throw Lhd_error{"unsupported",
                    "multiple lg: inputs are not supported yet (gids are library-scoped)",
                    "aggregate upstream from ln: units, or merge into one library"};
  }

  const auto* lg_out = find_slot(opts.emit_dirs, "lg");
  const auto* ln_out = find_slot(opts.emit_dirs, "ln");
  Eprp_var    var;
  std::string lib_path;

  if (!ir.ln_dirs.empty()) {
    setup_diag(opts, "compile.ln");
    for (const auto& d : ir.ln_dirs) {
      res.inputs.push_back(d);
      for (auto& ln : load_ln_dir(d)) {
        var.add(ln);
      }
    }
    lib_path = lg_out ? lg_out->path : workdir(opts) + "/lgdb";
    lower_lnasts(opts, res, var, lib_path, emits_need_graphs(opts));
    if (ln_out != nullptr) {  // re-publish the post-upass forest (the loaded units are the design)
      emit_ln_outputs(var.lnasts, opts, res);
    }
  } else {
    setup_diag(opts, "compile.lg");
    const auto& lg_in = ir.lg_dirs.front();
    if (!fs::is_directory(lg_in)) {
      throw Lhd_error{"missing_file", std::format("lg: input not found: {}", lg_in), "an lg: input is a GraphLibrary directory"};
    }
    res.inputs.push_back(lg_in);
    lib_path = lg_in;
    if (lg_out != nullptr && fs::weakly_canonical(lg_out->path) != fs::weakly_canonical(lg_in)) {
      // Saving only rewrites dirty graphs, so emit-to-a-new-path starts from
      // a filesystem copy of the input library and mutates that copy. Purge
      // the destination first: fs::copy never removes destination-only
      // entries, so a reused output dir would keep stale graph bodies.
      std::error_code ec;
      fs::remove_all(lg_out->path, ec);
      ensure_dir(lg_out->path);
      fs::copy(lg_in, lg_out->path, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
      lib_path = lg_out->path;
    }
    auto& lib = livehd::Hhds_graph_library::instance(lib_path);
    for (const hhds::Gid id : lib.all_gids()) {  // gids are sparse name-hashes now
      auto g = lib.get_graph(id);
      if (g) {
        var.add(g);
      }
    }
    if (var.graphs.empty()) {
      throw Lhd_error{"config", std::format("lg: input {} holds no graphs", lg_in), ""};
    }
  }

  graph_pipeline_and_emits(opts, res, var, lib_path);
}

// Linker: merge lg: libraries + lower the ln: source units that reference them
// (`import("lg:foo")` -> a black-box Sub) into one new lg: library, then run
// the shared graph pipeline (recipe + emits).
void compile_link_ir(Options& opts, Result& res, const Ir_inputs& ir) {
  setup_diag(opts, "compile.link");
  const auto* lg_out = find_slot(opts.emit_dirs, "lg");
  if (lg_out == nullptr) {
    throw Lhd_error{"usage", "linking ln: + lg: inputs needs --emit-dir lg:DIR/", ""};
  }
  const std::string lib_path = lg_out->path;
  // Absorb each lg: library into the output library. Name-hash gids make a
  // shared graph name keep the same gid across libraries, so load_merge is
  // conflict-free for matching names (and dedups them).
  auto&             lib      = livehd::Hhds_graph_library::instance(lib_path);
  for (const auto& d : ir.lg_dirs) {
    res.inputs.push_back(d);
    lib.load_merge(d);
  }
  // Lower the ln: source units into the SAME library; their `import("lg:…")`
  // calls resolve to the absorbed graphs by name at tolg.
  Eprp_var var;
  for (const auto& d : ir.ln_dirs) {
    res.inputs.push_back(d);
    for (auto& ln : load_ln_dir(d)) {
      var.add(ln);
    }
  }
  lower_lnasts(opts, res, var, lib_path, /*need_graphs=*/true);
  graph_pipeline_and_emits(opts, res, var, lib_path);
}

// ---- compile (the single source->IR->netlist action; front-end + elaborate +
// synth fused into one) ------------------------------------------------------

// Source-driven compile (pyrope or verilog): front-end parse -> pass.upass ->
// (recipe + emits). The pyrope path also publishes a first-elaboration `ln:`
// dir (pub wrappers) when one is requested; the slang path emits the plain
// post-upass forest.
void compile_sources(Options& opts, Result& res, const Ir_inputs& ir) {
  Eprp_var var;
  if (opts.language == "pyrope") {
    setup_diag(opts, "compile.pyrope");
    auto        n_imports = pyrope_parse(opts, res, var, ir.ln_dirs);
    const auto* lg_out    = find_slot(opts.emit_dirs, "lg");
    const auto* ln_out    = find_slot(opts.emit_dirs, "ln");
    std::string lib_path  = lg_out ? lg_out->path : workdir(opts) + "/lgdb";
    // 2f-lgimport — absorb any lg: input libraries into the working library
    // BEFORE lowering, so a source unit's `import("lg:name")` resolves to the
    // pre-compiled graph by name at tolg (the same linker mechanism the
    // ln:+lg: path uses). Name-hash gids make matching names dedup cleanly.
    if (!ir.lg_dirs.empty()) {
      auto& lib = livehd::Hhds_graph_library::instance(lib_path);
      for (const auto& d : ir.lg_dirs) {
        res.inputs.push_back(d);
        lib.load_merge(d);
      }
    }
    lower_lnasts(opts, res, var, lib_path, emits_need_graphs(opts) || !ir.lg_dirs.empty());
    if (ln_out != nullptr) {
      publish_source_ln(opts, res, var, n_imports, ln_out->path);
    }
    graph_pipeline_and_emits(opts, res, var, lib_path);
  } else {
    setup_diag(opts, "compile.verilog");
    if (!ir.ln_dirs.empty() || !ir.lg_dirs.empty()) {
      throw Lhd_error{"usage", "verilog sources take no ln:/lg: inputs", ""};
    }
    if (opts.reader == "slang") {  // direct SV -> LNAST: the pyrope flow from here
      slang_parse(opts, res, var);
      const auto* lg_out   = find_slot(opts.emit_dirs, "lg");
      const auto* ln_out   = find_slot(opts.emit_dirs, "ln");
      std::string lib_path = lg_out ? lg_out->path : workdir(opts) + "/lgdb";
      lower_lnasts(opts, res, var, lib_path, emits_need_graphs(opts));
      if (ln_out != nullptr) {  // the slang units are the design (no imports) -> plain forest
        save_ln_dir(opts, res, filter_top(var.lnasts, opts.top), ln_out->path);
      }
      graph_pipeline_and_emits(opts, res, var, lib_path);
    } else {
      auto lib_path = verilog_frontend(opts, res, var);
      graph_pipeline_and_emits(opts, res, var, lib_path);
    }
  }
  harvest_source_files(res, var.lnasts);
  write_depfile(opts, res);
}

void compile_command(Options& opts, Result& res) {
  auto ir = gather_ir_inputs(opts, "compile");

  // `--reader <slang|yosys-slang|yosys-verilog> -- -F filelist.f` supplies the
  // verilog sources through the raw slang args, so a source compile is valid
  // with no positional file.
  const bool has_sources = !opts.files.empty() || (opts.language == "verilog" && !opts.raw_args.empty());

  if (has_sources) {
    compile_sources(opts, res, ir);
    return;
  }

  // No sources: IR-only inputs (aggregate / link / optimize).
  if (ir.ln_dirs.empty() && ir.lg_dirs.empty()) {
    throw Lhd_error{"usage",
                    "compile requires source files (.prp/.v/.sv) or ln:/lg: inputs",
                    "e.g. `lhd compile foo.prp`, `lhd compile lg:dir --emit verilog:net.v`"};
  }
  if (!ir.ln_dirs.empty() && !ir.lg_dirs.empty()) {
    compile_link_ir(opts, res, ir);  // ln: + lg: linker
    return;
  }
  compile_ir(opts, res, ir);  // ln:-only or lg:-only
}

// ---- check ------------------------------------------------------------------

std::string shell_quote(const std::string& s) {
  std::string out{"'"};
  for (char c : s) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out += c;
    }
  }
  out += '\'';
  return out;
}

std::string locate_lgcheck() {
  if (const char* env = std::getenv("LHD_LGCHECK"); env != nullptr && ::access(env, X_OK) == 0) {
    return fs::absolute(env).string();
  }
  auto exe_dir = file_utils::get_exe_path();
  for (const auto& cand : {std::string{"./inou/yosys/lgcheck"},
                           std::string{"inou/yosys/lgcheck"},
                           exe_dir + "/lhd.runfiles/_main/inou/yosys/lgcheck",
                           exe_dir + "/lhd.runfiles/livehd/inou/yosys/lgcheck"}) {
    if (::access(cand.c_str(), X_OK) == 0) {
      return fs::absolute(cand).string();
    }
  }
  throw Lhd_error{"dependency",
                  "lgcheck (inou/yosys/lgcheck) not found",
                  "run from the LiveHD repo root or set LHD_LGCHECK=/path/to/lgcheck"};
}

// The yosys binary lgcheck shells out to. lgcheck's own fallbacks are
// cwd-relative, so pass an absolute path explicitly (lhd runs lgcheck from
// the scratch workdir to keep its trace*/log droppings out of the caller's
// cwd). Empty result -> let lgcheck try `which yosys`.
std::string locate_lgcheck_yosys() {
  if (const char* env = std::getenv("LHD_YOSYS"); env != nullptr && ::access(env, X_OK) == 0) {
    return fs::absolute(env).string();
  }
  auto exe_dir = file_utils::get_exe_path();
  for (const auto& cand : {std::string{"bazel-bin/inou/yosys/yosys2"},
                           exe_dir + "/../inou/yosys/yosys2",
                           exe_dir + "/lhd.runfiles/_main/inou/yosys/yosys2",
                           exe_dir + "/lhd.runfiles/livehd/inou/yosys/yosys2"}) {
    if (::access(cand.c_str(), X_OK) == 0) {
      return fs::absolute(cand).string();
    }
  }
  return "";
}

// Load one --impl/--ref side into `var.graphs` (no cgen). Defined below; both
// lec backends share it.
void load_side_graphs(Options& opts, Result& res, const std::string& kind, const std::string& path, std::string_view side,
                      Eprp_var& var);

// Return a verilog file for an --impl/--ref side (the lgyosys/lgcheck backend):
// a verilog side passes straight through (lgcheck reads Verilog directly);
// every other kind is loaded to graphs (load_side_graphs) and re-emitted with
// cgen into the scratch workdir.
std::string materialize_verilog(Options& opts, Result& res, const std::string& kind, const std::string& path,
                                std::string_view side) {
  if (kind == "verilog") {
    res.inputs.push_back(path);
    check_inputs_exist({path});
    return path;
  }
  Eprp_var var;
  load_side_graphs(opts, res, kind, path, side, var);  // lg/pyrope/ln -> graphs (throws if empty)
  auto          scratch = std::format("{}/check_{}", workdir(opts), side);
  auto          names   = cgen_into(opts, res, var, scratch);
  auto          out     = std::format("{}/check_{}.v", workdir(opts), side);
  std::ofstream ofs(out);
  for (const auto& n : names) {
    std::ifstream ifs(std::format("{}/{}.v", scratch, n));
    ofs << ifs.rdbuf();
  }
  return out;
}

// The lgyosys backend (`--set lec.solver=lgyosys`): materialize both sides to
// Verilog and discharge with inou/yosys/lgcheck (the former `lhd check`).
// Verilog sides pass straight through; pyrope:/ln:/lg: are compiled first.
void lec_lgyosys(Options& opts, Result& res) {
  auto impl_v  = fs::absolute(materialize_verilog(opts, res, opts.impl_kind, opts.impl_path, "impl")).string();
  auto ref_v   = fs::absolute(materialize_verilog(opts, res, opts.ref_kind, opts.ref_path, "ref")).string();
  auto lgcheck = locate_lgcheck();
  auto yosys   = locate_lgcheck_yosys();

  // Run lgcheck FROM the scratch workdir so its cwd droppings (trace*.v,
  // lgcheck*.log) never land in the caller's directory (hermetic kernel).
  auto rundir = fs::absolute(workdir(opts)).string();
  auto cmd    = std::format("cd {} && {} --implementation {} --reference {}",
                            shell_quote(rundir),
                            shell_quote(lgcheck),
                            shell_quote(impl_v),
                            shell_quote(ref_v));
  if (!yosys.empty()) {
    cmd += std::format(" --yosys {}", shell_quote(yosys));
  }
  if (!opts.impl_top.empty()) {
    cmd += std::format(" --implementation_top {}", shell_quote(opts.impl_top));
  }
  if (!opts.ref_top.empty()) {
    cmd += std::format(" --reference_top {}", shell_quote(opts.ref_top));
  }
  if (opts.impl_top.empty() && opts.ref_top.empty() && !opts.top.empty()) {
    cmd += std::format(" --top {}", shell_quote(opts.top));
  }
  auto log  = next_log_path(opts, "lec.lgcheck");
  cmd      += std::format(" >> {} 2>&1", shell_quote(fs::absolute(log).string()));

  res.recipe_steps.emplace_back("pass.lec solver:lgyosys (lgcheck)");
  int rc = std::system(cmd.c_str());
  if (opts.verbose) {
    mirror_log_to_stderr(log);
  }
  std::string name = !opts.impl_top.empty() ? opts.impl_top : opts.impl_path;
  std::print("lec: '{}' {} (solver=lgyosys)\n", name, rc == 0 ? "PROVEN equivalent" : "REFUTED (not equivalent)");
  if (rc != 0) {
    throw Lhd_error{"equiv_fail",
                    std::format("equivalence check failed ({} vs {})", opts.impl_path, opts.ref_path),
                    std::format("see {}", log)};
  }
}

// ---- lec (in-process relational equivalence via pass.lec / Pono) ------------

// Load one --impl/--ref side into `var.graphs` WITHOUT cgen. lg: libraries load
// directly; pyrope:/ln: parse/load then lower (upass + tolg + recipe) to
// graphs; verilog: elaborates through --reader — slang (the default: direct
// SV -> LNAST, the pyrope flow) or yosys-slang/yosys-verilog (yosys ->
// LGraphs). The in-process lec engine consumes the graphs directly; the
// lgyosys backend re-emits them through cgen (materialize_verilog).
void load_side_graphs(Options& opts, Result& res, const std::string& kind, const std::string& path, std::string_view side,
                      Eprp_var& var) {
  res.inputs.push_back(path);
  if (kind == "lg") {
    if (!fs::is_directory(path)) {
      throw Lhd_error{"missing_file", std::format("lg: input not found: {}", path), ""};
    }
    auto& lib = livehd::Hhds_graph_library::instance(path);
    for (const hhds::Gid id : lib.all_gids()) {
      auto g = lib.get_graph(id);
      if (g) {
        var.add(g);
      }
    }
  } else if (kind == "pyrope" || kind == "ln" || kind == "verilog") {
    // Verilog through a yosys reader elaborates straight to LGraphs; every
    // other path (pyrope, ln:, verilog via slang) yields LNAST that lowers
    // through upass + tolg + the recipe.
    const bool yosys_reader = kind == "verilog" && opts.reader != "slang";
    auto       lib_path     = std::format("{}/lec_{}_lgdb", workdir(opts), side);
    if (yosys_reader) {
      check_inputs_exist({path});
      Eprp_var::Eprp_dict labels{
          {    "path",                                                                      lib_path},
          {   "files",                                                                          path},
          {     "top",                          opts.top.empty() ? std::string{"-auto-top"} : opts.top},
          {"frontend", opts.reader == "yosys-verilog" ? std::string{"verilog"} : std::string{"slang"}},
      };
      run_step("inou.yosys.tolg", var, labels, opts, res);
    } else {
      if (kind == "pyrope") {
        check_inputs_exist({path});
        run_step("inou.prp", var, {{"files", path}}, opts, res);
      } else if (kind == "verilog") {  // slang: the direct SV -> LNAST front-end
        check_inputs_exist({path});
        run_step("inou.slang", var, {{"files", path}}, opts, res);
      } else {  // ln:
        if (!fs::is_directory(path)) {
          throw Lhd_error{"missing_file", std::format("ln: input not found: {}", path), "an ln: input is a Forest save directory"};
        }
        for (auto& ln : load_ln_dir(path)) {
          var.add(ln);
        }
      }
      lower_lnasts(opts, res, var, lib_path, /*need_graphs=*/true);
      graph_pipeline_and_emits(opts, res, var, lib_path);
    }
  } else {
    throw Lhd_error{"usage",
                    std::format("lec accepts verilog:, lg:, pyrope:, or ln: inputs, got {}:", kind),
                    "a bare .v/.sv/.prp path infers its kind"};
  }
  if (var.graphs.empty()) {
    throw Lhd_error{"config", std::format("lec {} input {} holds no graphs", side, path), ""};
  }
}

void lec_command(Options& opts, Result& res) {
  setup_diag(opts, "lec");
  if (opts.impl_path.empty() || opts.ref_path.empty()) {
    throw Lhd_error{"usage",
                    "lec requires --impl KIND:PATH and --ref KIND:PATH",
                    "sides: verilog:/pyrope:/ln:/lg: or a bare .v/.sv/.prp path"};
  }

  // The solver selects the backend: cvc5 (default) / bitwuzla discharge
  // in-process (pass/lec, no yosys); lgyosys shells out to inou/yosys/lgcheck
  // (the former `lhd check`) — the only backend that reads Verilog without a
  // front-end reader and the path for gate-level / yosys-origin netlists.
  Eprp_var::Eprp_dict labels;
  merge_sets(opts, "lec", labels);
  auto label = [&](std::string_view k, std::string_view def) -> std::string {
    auto it = labels.find(std::string{k});
    return it == labels.end() ? std::string{def} : it->second;
  };
  const std::string solver = label("solver", "cvc5");
  if (solver != "cvc5" && solver != "bitwuzla" && solver != "lgyosys") {
    throw Lhd_error{"usage",
                    std::format("--set lec.solver expects cvc5|bitwuzla|lgyosys, got '{}'", solver),
                    "cvc5 (default, in-process SMT) | bitwuzla (in-process SMT) | lgyosys (yosys/lgcheck)"};
  }
  if (solver == "lgyosys") {
    lec_lgyosys(opts, res);
    return;
  }

  Eprp_var ref_var;
  Eprp_var impl_var;
  load_side_graphs(opts, res, opts.ref_kind, opts.ref_path, "ref", ref_var);
  load_side_graphs(opts, res, opts.impl_kind, opts.impl_path, "impl", impl_var);

  // Pick the top module on each side: explicit --{ref,impl}-top, else --top,
  // else the sole module.
  auto pick = [&](Eprp_var& v, const std::string& want, std::string_view side) -> std::shared_ptr<hhds::Graph> {
    const std::string& t = !want.empty() ? want : opts.top;
    if (!t.empty()) {
      for (auto& g : v.graphs) {
        if (g && g->get_name() == t) {
          return g;
        }
      }
      throw Lhd_error{"config", std::format("lec: {} top '{}' not found", side, t), ""};
    }
    if (v.graphs.size() == 1) {
      return v.graphs.front();
    }
    throw Lhd_error{"usage", std::format("lec: {} has {} modules; pass --{}-top or --top", side, v.graphs.size(), side), ""};
  };
  auto ref_g  = pick(ref_var, opts.ref_top, "ref");
  auto impl_g = pick(impl_var, opts.impl_top, "impl");

  bool cross = label("cross", "false") != "false" && label("cross", "false") != "0";

  // Discharge in-process via pass/lec (L1). The engine is the authority on the
  // non-cross path; in cross mode we additionally run lgcheck and assert
  // agreement (the strongest encoder check).
  livehd::lec::Lec_options o;
  o.engine  = label("engine", "ind");
  o.solver  = solver;  // cvc5 | bitwuzla
  o.bound   = std::atoi(label("bound", "20").c_str());
  o.timeout = std::atoi(label("timeout", "0").c_str());
  o.witness = label("witness", "true") != "false" && label("witness", "true") != "0";
  o.phase        = label("phase", "free");
  o.reset_cycles = std::atoi(label("reset_cycles", "2").c_str());
  o.reset        = label("reset", "");

  // --lib lg:DIR libraries resolve Sub instances during encoding (e.g. the
  // gensim cell models behind an ABC standard-cell netlist), so lec can flatten
  // a hierarchical/mapped impl. Gids are name-hash stable, so an instance's
  // subnode gid matches its def's gid across libraries.
  absl::flat_hash_map<hhds::Gid, hhds::Graph*> sub_lib;
  std::vector<std::shared_ptr<hhds::Graph>>    sub_lib_keep;
  for (const auto& lp : opts.libs) {
    if (lp.kind != "lg") {
      throw Lhd_error{"usage", std::format("lec --lib expects lg:DIR, got '{}:'", lp.kind), "the cell-model library, e.g. --lib lg:models"};
    }
    if (!fs::is_directory(lp.path)) {
      throw Lhd_error{"missing_file", std::format("lec --lib not found: {}", lp.path), ""};
    }
    auto& lib = livehd::Hhds_graph_library::instance(lp.path);
    for (const hhds::Gid id : lib.all_gids()) {
      auto g = lib.get_graph(id);
      if (!g) {
        continue;
      }
      sub_lib_keep.push_back(g);
      sub_lib[id] = g.get();  // later --lib wins on a gid clash
    }
  }
  const auto* sub_lib_ptr = sub_lib.empty() ? nullptr : &sub_lib;

  res.recipe_steps.emplace_back(std::format("pass.lec engine:{} solver:{} phase:{}", o.engine, o.solver, o.phase));
  auto r         = livehd::lec::prove_equal(ref_g.get(), impl_g.get(), o, sub_lib_ptr);
  bool lec_equiv = r.verdict == livehd::lec::Verdict::Proven;
  bool lec_known = r.verdict != livehd::lec::Verdict::Unknown;

  const char* verdict = lec_known ? (lec_equiv ? "PROVEN equivalent" : "REFUTED (not equivalent)") : "UNKNOWN";
  std::print("lec: '{}' {} ({})\n", impl_g->get_name(), verdict, r.detail);
  if (r.verdict == livehd::lec::Verdict::Refuted && !r.witness.empty()) {
    std::print("  counterexample: {}\n", r.witness);
  }

  if (!cross) {
    if (r.verdict == livehd::lec::Verdict::Refuted) {
      throw Lhd_error{"equiv_fail",
                      std::format("'{}' is not equivalent ({} vs {})", impl_g->get_name(), opts.impl_path, opts.ref_path),
                      r.witness.empty() ? "" : std::format("counterexample: {}", r.witness)};
    }
    if (r.verdict == livehd::lec::Verdict::Unknown) {
      throw Lhd_error{"unsupported", std::format("lec could not decide equivalence of '{}'", impl_g->get_name()), r.detail};
    }
    return;  // Proven
  }

  auto impl_v  = fs::absolute(materialize_verilog(opts, res, opts.impl_kind, opts.impl_path, "impl")).string();
  auto ref_v   = fs::absolute(materialize_verilog(opts, res, opts.ref_kind, opts.ref_path, "ref")).string();
  // cross mode re-materializes both sides through materialize_verilog, which
  // re-records their input paths (load_side_graphs already did above) — collapse
  // res.inputs back to one entry per side (stable, first occurrence wins).
  {
    std::vector<std::string> dedup;
    for (const auto& p : res.inputs) {
      if (std::find(dedup.begin(), dedup.end(), p) == dedup.end()) {
        dedup.push_back(p);
      }
    }
    res.inputs = std::move(dedup);
  }
  auto lgcheck = locate_lgcheck();
  auto yosys   = locate_lgcheck_yosys();
  auto rundir  = fs::absolute(workdir(opts)).string();
  auto cmd     = std::format("cd {} && {} --implementation {} --reference {}",
                         shell_quote(rundir),
                         shell_quote(lgcheck),
                         shell_quote(impl_v),
                         shell_quote(ref_v));
  if (!yosys.empty()) {
    cmd += std::format(" --yosys {}", shell_quote(yosys));
  }
  if (!opts.top.empty()) {
    cmd += std::format(" --top {}", shell_quote(opts.top));
  }
  auto log  = next_log_path(opts, "lec.lgcheck");
  cmd      += std::format(" >> {} 2>&1", shell_quote(fs::absolute(log).string()));
  int  rc        = std::system(cmd.c_str());
  bool lg_equiv  = rc == 0;

  std::print("lec cross-check: engine={} -> {}; lgcheck -> {}\n",
             o.engine,
             lec_known ? (lec_equiv ? "equivalent" : "different") : "unknown",
             lg_equiv ? "equivalent" : "different");

  if (lec_known && lec_equiv != lg_equiv) {
    throw Lhd_error{"internal",
                    std::format("lec engine and lgcheck DISAGREE (engine={}, lgcheck={})",
                                lec_equiv ? "equivalent" : "different",
                                lg_equiv ? "equivalent" : "different"),
                    std::format("see {}", log)};
  }
  if (!lg_equiv) {
    throw Lhd_error{"equiv_fail", std::format("equivalence check failed ({} vs {})", opts.impl_path, opts.ref_path), ""};
  }
}

// ---- pass (graph-pass plumbing: color / partition) --------------------------

// Load every graph of an lg: library into `var` (mirrors synth's lg branch).
void load_lg_into_var(const std::string& lib_path, Eprp_var& var) {
  auto& lib = livehd::Hhds_graph_library::instance(lib_path);
  for (const hhds::Gid id : lib.all_gids()) {  // gids are sparse name-hashes
    auto g = lib.get_graph(id);
    if (g) {
      var.add(g);
    }
  }
}

void pass_command(Options& opts, Result& res) {
  setup_diag(opts, "pass");
  if (opts.files.empty()) {
    throw Lhd_error{"usage",
                    "pass requires a subcommand: color <alg> | partition | abc | liberty gensim",
                    "e.g. `lhd pass color acyclic --top m lg:dir` or `lhd pass abc --top m lg:dir --emit-dir lg:net`"};
  }
  const std::string sub = opts.files[0];

  // `pass liberty gensim <file.lib> --emit-dir lg:DIR` takes a Liberty FILE, not
  // an lg: input — handle it before the lg: requirement below.
  if (sub == "liberty") {
    std::string subsub = opts.files.size() > 1 ? opts.files[1] : std::string{};
    if (subsub != "gensim") {
      throw Lhd_error{"usage", "pass liberty supports: gensim <file.lib> --emit-dir lg:DIR", ""};
    }
    if (opts.files.size() < 3) {
      throw Lhd_error{"usage", "pass liberty gensim needs a Liberty .lib file argument", ""};
    }
    const std::string lib_file = opts.files[2];
    check_inputs_exist({lib_file});
    const auto* lg_out = find_slot(opts.emit_dirs, "lg");
    if (lg_out == nullptr) {
      throw Lhd_error{"usage", "pass liberty gensim needs --emit-dir lg:DIR for the model library", ""};
    }
    std::error_code ec;
    fs::remove_all(lg_out->path, ec);
    ensure_dir(lg_out->path);
    res.inputs.push_back(lib_file);
    Eprp_var            var;
    Eprp_var::Eprp_dict labels{
        {"files", lib_file},
        {  "out", lg_out->path}
    };
    merge_sets(opts, "pass.liberty", labels);
    run_step("pass.liberty", var, labels, opts, res);
    livehd::Hhds_graph_library::save(lg_out->path);
    res.outputs.push_back(lg_out->path);
    return;
  }

  auto ir = gather_ir_inputs(opts, "pass");
  if (ir.lg_dirs.empty()) {
    throw Lhd_error{"usage", "pass requires an lg:DIR input", "e.g. `lhd pass color acyclic --top m lg:dir`"};
  }
  if (ir.lg_dirs.size() > 1) {
    throw Lhd_error{"unsupported", "multiple lg: inputs are not supported (gids are library-scoped)", ""};
  }
  const auto& lg_in = ir.lg_dirs.front();
  if (!fs::is_directory(lg_in)) {
    throw Lhd_error{"missing_file", std::format("lg: input not found: {}", lg_in), "an lg: input is a GraphLibrary directory"};
  }
  res.inputs.push_back(lg_in);

  if (sub == "color") {
    std::string alg = opts.files.size() > 1 ? opts.files[1] : std::string{"acyclic"};
    Eprp_var    var;
    load_lg_into_var(lg_in, var);
    if (var.graphs.empty()) {
      throw Lhd_error{"config", std::format("lg: input {} holds no graphs", lg_in), ""};
    }
    Eprp_var::Eprp_dict labels;
    labels["alg"] = alg;
    if (!opts.top.empty()) {
      labels["top"] = opts.top;
    }
    merge_sets(opts, "pass.color", labels);
    run_step("pass.color", var, labels, opts, res);
    livehd::Hhds_graph_library::save(lg_in);  // in-place coloring
    res.outputs.push_back(lg_in);
  } else if (sub == "partition") {
    Eprp_var var;
    load_lg_into_var(lg_in, var);
    if (var.graphs.empty()) {
      throw Lhd_error{"config", std::format("lg: input {} holds no graphs", lg_in), ""};
    }
    const auto*         lg_out = find_slot(opts.emit_dirs, "lg");
    Eprp_var::Eprp_dict labels;
    if (!opts.top.empty()) {
      labels["top"] = opts.top;
    }
    if (lg_out != nullptr) {
      if (fs::weakly_canonical(lg_out->path) == fs::weakly_canonical(lg_in)) {
        throw Lhd_error{"usage", "partition --emit-dir lg: must differ from the input lg:", ""};
      }
      std::error_code ec;
      fs::remove_all(lg_out->path, ec);
      ensure_dir(lg_out->path);
      labels["out"] = lg_out->path;
    }
    merge_sets(opts, "pass.partition", labels);
    run_step("pass.partition", var, labels, opts, res);
    if (lg_out != nullptr) {
      livehd::Hhds_graph_library::save(lg_out->path);
      res.outputs.push_back(lg_out->path);
    }
  } else if (sub == "abc") {
    Eprp_var var;
    load_lg_into_var(lg_in, var);
    if (var.graphs.empty()) {
      throw Lhd_error{"config", std::format("lg: input {} holds no graphs", lg_in), ""};
    }
    const auto*         lg_out = find_slot(opts.emit_dirs, "lg");
    Eprp_var::Eprp_dict labels;
    if (!opts.top.empty()) {
      labels["top"] = opts.top;
    }
    if (lg_out != nullptr) {
      if (fs::weakly_canonical(lg_out->path) == fs::weakly_canonical(lg_in)) {
        throw Lhd_error{"usage", "abc --emit-dir lg: must differ from the input lg:", ""};
      }
      std::error_code ec;
      fs::remove_all(lg_out->path, ec);
      ensure_dir(lg_out->path);
      labels["out"] = lg_out->path;
    }
    merge_sets(opts, "pass.abc", labels);
    run_step("pass.abc", var, labels, opts, res);
    if (lg_out != nullptr) {
      livehd::Hhds_graph_library::save(lg_out->path);
      res.outputs.push_back(lg_out->path);
    }
  } else {
    throw Lhd_error{"usage", std::format("unknown pass subcommand '{}'", sub), "use: color <alg> | partition | abc | liberty gensim"};
  }
}

// ===========================================================================
// `lhd tool`: unified ln/lg inspector — cat / grep / diff / tree.
// Replaces the former ln.cat / ln.diff. Verbs are polymorphic over ln:/lg:.
// ===========================================================================

enum class Tool_target { node, pin, edge, all };

Tool_target parse_tool_target(const std::string& s) {
  if (s.empty() || s == "all") {
    return Tool_target::all;
  }
  if (s == "node") {
    return Tool_target::node;
  }
  if (s == "pin") {
    return Tool_target::pin;
  }
  if (s == "edge") {
    return Tool_target::edge;
  }
  throw Lhd_error{"usage", std::format("--target expects node|pin|edge|all, got '{}'", s), ""};
}

// One AND-combined filter term, `field:value` (or a bare token: numeric=>id,
// text=>name). Numeric fields support comparisons/ranges + `nil`; string
// fields default to substring, `~`=regex, `=`=exact.
struct Tool_filter {
  enum class Kind { sub, re, eq, num_eq, num_gt, num_lt, num_ge, num_le, num_range, is_nil, any_sub };
  std::string field;
  Kind        kind = Kind::sub;
  std::string sval;
  std::regex  re;
  long        n1 = 0;
  long        n2 = 0;
};

bool tool_is_numeric_field(std::string_view f) {
  return f == "id" || f == "nid" || f == "color" || f == "bits" || f == "delay" || f == "hier_color";
}

// The columns a `<field><sep><value>` filter may target. A token whose head is
// not one of these is treated as a bare match-everything term instead, so a
// value that merely contains ':'/'=' (e.g. a src path `x.prp:5`) is not
// mis-split into a bogus field.
bool tool_is_known_field(std::string_view f) {
  return f == "nid" || f == "id" || f == "kind" || f == "name" || f == "color" || f == "src"
         || f == "partitionable" || f == "bits" || f == "signed" || f == "from" || f == "to" || f == "delay"
         || f == "hier_color";
}

long tool_parse_long(std::string_view v, std::string_view ctx) {
  size_t consumed = 0;
  long   n        = 0;
  try {
    n = std::stol(std::string{v}, &consumed);
  } catch (const std::exception&) {
    consumed = 0;
  }
  if (v.empty() || consumed != v.size()) {
    throw Lhd_error{"usage", std::format("filter '{}': expected an integer, got '{}'", ctx, v), ""};
  }
  return n;
}

Tool_filter parse_tool_filter(const std::string& tok) {
  Tool_filter f;
  // A field filter is `<field><op><value>`, where <op> starts at the first of
  // ':' '=' '>' '<' '~' and <field> (the text before it) is a known column.
  // Pyrope reads `a:b` as "a has type b", so '=' is the preferred separator
  // (`name=get_mask`, `kind=get_mask`, `color=nil`); '>'/'<' may lead directly
  // (`bits>8`) and ':' stays accepted out of habit. Anything else — a token
  // with no operator, or whose head is not a known field (e.g. the src path
  // `x.prp:5`) — is a bare match-everything term: a substring tested against
  // every column and the node/pin identity, so `lhd tool grep get_mask lg:dir`
  // lights up the get_mask cells exactly as `cat` shows them.
  auto sep = tok.find_first_of(":=<>~");
  if (sep == std::string::npos || !tool_is_known_field(std::string_view{tok}.substr(0, sep))) {
    f.kind = Tool_filter::Kind::any_sub;
    f.sval = tok;
    return f;
  }
  std::string field = tok.substr(0, sep);
  std::string val   = tok.substr(sep);  // operator(s) + value; e.g. ">8", "=nil", ":Mult"
  f.field           = field;
  if (!val.empty() && (val.front() == ':' || val.front() == '=')) {
    val.erase(0, 1);  // strip one equality separator; a relational op may remain (`=>8`, `:>8`)
  }
  if (val == "nil") {
    f.kind = Tool_filter::Kind::is_nil;
    return f;
  }
  if (tool_is_numeric_field(field)) {
    if (val.starts_with(">=")) {
      f.kind = Tool_filter::Kind::num_ge;
      f.n1   = tool_parse_long(val.substr(2), tok);
    } else if (val.starts_with("<=")) {
      f.kind = Tool_filter::Kind::num_le;
      f.n1   = tool_parse_long(val.substr(2), tok);
    } else if (val.starts_with(">")) {
      f.kind = Tool_filter::Kind::num_gt;
      f.n1   = tool_parse_long(val.substr(1), tok);
    } else if (val.starts_with("<")) {
      f.kind = Tool_filter::Kind::num_lt;
      f.n1   = tool_parse_long(val.substr(1), tok);
    } else if (auto rp = val.find(".."); rp != std::string::npos) {
      f.kind = Tool_filter::Kind::num_range;
      f.n1   = tool_parse_long(val.substr(0, rp), tok);
      f.n2   = tool_parse_long(val.substr(rp + 2), tok);
    } else {
      f.kind = Tool_filter::Kind::num_eq;
      f.n1   = tool_parse_long(val, tok);
    }
  } else if (val.starts_with("~")) {
    f.kind = Tool_filter::Kind::re;
    try {
      f.re = std::regex(val.substr(1));
    } catch (const std::regex_error& e) {
      throw Lhd_error{"usage", std::format("filter '{}': bad regex: {}", tok, e.what()), ""};
    }
  } else if (val.starts_with("=")) {
    f.kind = Tool_filter::Kind::eq;
    f.sval = val.substr(1);
  } else {
    f.kind = Tool_filter::Kind::sub;
    f.sval = val;
  }
  return f;
}

// A flattened inspector record. `cols` carries every filterable+displayable
// field (value "nil" = unset); `ident` is the always-shown identity.
struct Tool_record {
  char                                             type = 'n';  // n|p|e
  std::string                                      ident;
  std::vector<std::pair<std::string, std::string>> cols;
};

const std::string* tool_col(const Tool_record& r, std::string_view key) {
  for (const auto& [k, v] : r.cols) {
    if (k == key) {
      return &v;
    }
  }
  return nullptr;
}

bool tool_match(const Tool_record& r, const Tool_filter& f) {
  if (f.kind == Tool_filter::Kind::any_sub) {  // bare term: substring vs identity + every set column
    if (r.ident.find(f.sval) != std::string::npos) {
      return true;
    }
    for (const auto& [k, val] : r.cols) {
      if (val != "nil" && val.find(f.sval) != std::string::npos) {
        return true;
      }
    }
    return false;
  }
  const std::string* v = tool_col(r, f.field);
  if (v == nullptr && f.field == "id") {
    v = tool_col(r, "nid");  // 'id' aliases 'nid'
  }
  if (v == nullptr) {
    return false;  // field not applicable to this entity
  }
  if (f.kind == Tool_filter::Kind::is_nil) {
    return *v == "nil";
  }
  if (*v == "nil") {
    return false;
  }
  switch (f.kind) {
    case Tool_filter::Kind::sub: return v->find(f.sval) != std::string::npos;
    case Tool_filter::Kind::re: return std::regex_search(*v, f.re);
    case Tool_filter::Kind::eq: return *v == f.sval;
    default: break;
  }
  long n = tool_parse_long(*v, f.field);
  switch (f.kind) {
    case Tool_filter::Kind::num_eq: return n == f.n1;
    case Tool_filter::Kind::num_gt: return n > f.n1;
    case Tool_filter::Kind::num_lt: return n < f.n1;
    case Tool_filter::Kind::num_ge: return n >= f.n1;
    case Tool_filter::Kind::num_le: return n <= f.n1;
    case Tool_filter::Kind::num_range: return n >= f.n1 && n <= f.n2;
    default: return false;
  }
}

bool tool_match_all(const Tool_record& r, const std::vector<Tool_filter>& filters) {
  for (const auto& f : filters) {
    if (!tool_match(r, f)) {
      return false;
    }
  }
  return true;
}

std::string tool_endpoint_name(const hhds::Pin_class& pin) {
  namespace gu = livehd::graph_util;
  if (gu::is_graph_input_pin(pin) || gu::is_graph_output_pin(pin)) {
    return std::format("${}", pin.get_pin_name());
  }
  auto node = pin.get_master_node();
  auto pn   = gu::pin_name_of(pin);
  if (pn.empty()) {
    return std::format("{}.p{}", gu::debug_name(node), pin.get_port_id());
  }
  return std::format("{}.{}", gu::debug_name(node), pn);
}

std::string tool_node_src(hhds::Graph* g, const hhds::Node_class& node) {
  auto ref = node.attr(hhds::attrs::srcid);
  if (!ref.has()) {
    return "nil";
  }
  auto sp = g->source_locator().resolve_span(ref.get());
  if (sp.start_line) {
    return std::format("{}:{}", sp.file.empty() ? std::string{"?"} : sp.file, *sp.start_line);
  }
  return sp.file.empty() ? std::string{"nil"} : sp.file;
}

std::string tool_color_str(const hhds::Node_class& node) {
  namespace gu = livehd::graph_util;
  return gu::has_node_color(node) ? std::to_string(gu::node_color_of(node)) : std::string{"nil"};
}

std::string tool_pin_label(const hhds::Pin_class& pin) {
  auto pn = livehd::graph_util::pin_name_of(pin);
  return pn.empty() ? std::format("p{}", pin.get_port_id()) : std::string{pn};
}

Tool_record tool_node_record(hhds::Graph* g, const hhds::Node_class& node) {
  namespace gu = livehd::graph_util;
  Tool_record r;
  r.type  = 'n';
  r.ident = gu::debug_name(node);
  r.cols.emplace_back("nid", std::to_string(static_cast<uint64_t>(node.get_debug_nid())));
  r.cols.emplace_back("kind", std::string{Ntype::get_name(gu::type_op_of(node))});
  auto nm = gu::node_name_of(node);
  r.cols.emplace_back("name", nm.empty() ? std::string{"nil"} : std::string{nm});
  r.cols.emplace_back("color", tool_color_str(node));
  r.cols.emplace_back("src", tool_node_src(g, node));
  r.cols.emplace_back("partitionable", livehd::color::is_partitionable(node) ? "1" : "0");
  return r;
}

// Output (driver) pins of `node`, deduped — the "what is this signal's width"
// view. An input pin is some other node's output, so it is not lost.
void tool_pin_records(const hhds::Node_class& node, std::vector<Tool_record>& out) {
  namespace gu = livehd::graph_util;
  std::set<std::pair<int, std::string>> seen;
  for (const auto& e : node.out_edges()) {
    const auto& pin = e.driver;
    auto        pn  = gu::pin_name_of(pin);
    if (!seen.insert({pin.get_port_id(), std::string{pn}}).second) {
      continue;
    }
    Tool_record r;
    r.type  = 'p';
    r.ident = tool_endpoint_name(pin);
    r.cols.emplace_back("nid", std::to_string(static_cast<uint64_t>(node.get_debug_nid())));
    r.cols.emplace_back("name", pn.empty() ? std::string{"nil"} : std::string{pn});
    int32_t b = gu::bits_of(pin);
    r.cols.emplace_back("bits", b != 0 ? std::to_string(b) : std::string{"nil"});
    r.cols.emplace_back("signed", gu::is_unsign(pin) ? "0" : "1");
    out.push_back(std::move(r));
  }
}

void tool_edge_records(const hhds::Node_class& node, std::vector<Tool_record>& out) {
  namespace gu = livehd::graph_util;
  for (const auto& e : node.out_edges()) {
    std::string from = tool_endpoint_name(e.driver);
    std::string to   = tool_endpoint_name(e.sink);
    int32_t     b    = gu::bits_of(e.driver);
    Tool_record r;
    r.type  = 'e';
    r.ident = std::format("{} -> {}  ({}b)", from, to, b);
    r.cols.emplace_back("from", from);
    r.cols.emplace_back("to", to);
    r.cols.emplace_back("bits", b != 0 ? std::to_string(b) : std::string{"nil"});
    out.push_back(std::move(r));
  }
}

void tool_flat_records(hhds::Graph* g, Tool_target tgt, std::vector<Tool_record>& recs) {
  for (auto node : g->forward_class()) {
    if (tgt == Tool_target::node || tgt == Tool_target::all) {
      recs.push_back(tool_node_record(g, node));
    }
    if (tgt == Tool_target::pin || tgt == Tool_target::all) {
      tool_pin_records(node, recs);
    }
    if (tgt == Tool_target::edge || tgt == Tool_target::all) {
      tool_edge_records(node, recs);
    }
  }
}

std::vector<std::string> tool_split_csv(const std::string& s) {
  std::vector<std::string> out;
  size_t                   start = 0;
  while (start <= s.size()) {
    auto end = s.find(',', start);
    out.push_back(s.substr(start, end == std::string::npos ? std::string::npos : end - start));
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return out;
}

std::vector<std::string> tool_display_cols(const Options& opts, Tool_target tgt) {
  if (!opts.tool_attr.empty()) {
    return tool_split_csv(opts.tool_attr);
  }
  switch (tgt) {
    case Tool_target::node: return {"color", "src"};
    case Tool_target::pin: return {"bits", "signed"};
    case Tool_target::edge: return {"bits"};
    default: return {"color", "src", "bits", "signed"};  // target=all flat (grep)
  }
}

std::string tool_render_pretty(const Tool_record& r, const std::vector<std::string>& cols) {
  std::string line = r.ident;
  for (const auto& key : cols) {
    const std::string* v = tool_col(r, key);
    if (v == nullptr) {
      continue;
    }
    if (key == "signed") {  // cosmetic: bare `signed` when set, omit otherwise
      if (*v == "1") {
        line += "  signed";
      }
      continue;
    }
    if (*v == "nil") {  // omit unset/absent attributes to cut verbosity
      continue;
    }
    line += std::format("  {}={}", key, *v);
  }
  return line;
}

std::string tool_render_jsonl(const Tool_record& r, std::string_view mod) {
  std::string out = "{";
  out += std::format("\"t\":\"{}\"", r.type == 'n' ? "node" : (r.type == 'p' ? "pin" : "edge"));
  out += std::format(",\"mod\":\"{}\"", json_escape_min(mod));
  for (const auto& [k, v] : r.cols) {
    if (v == "nil") {
      out += std::format(",\"{}\":null", k);
    } else if (k == "signed" || k == "partitionable") {
      out += std::format(",\"{}\":{}", k, v == "1" ? "true" : "false");
    } else if (tool_is_numeric_field(k) || k == "bits") {
      out += std::format(",\"{}\":{}", k, v);
    } else {
      out += std::format(",\"{}\":\"{}\"", k, json_escape_min(v));
    }
  }
  out += "}";
  return out;
}

// Load one lg: library and return the graphs selected by --top (all when
// --top is empty), sorted by name. The graphs outlive `var` (owned by the
// Hhds_graph_library singleton keyed on `dir`).
std::vector<std::shared_ptr<hhds::Graph>> tool_select_graphs(const std::string& dir, const Options& opts) {
  if (!fs::is_directory(dir)) {
    throw Lhd_error{"missing_file", std::format("lg: input not found: {}", dir), "an lg: input is a GraphLibrary directory"};
  }
  Eprp_var var;
  load_lg_into_var(dir, var);
  std::vector<std::shared_ptr<hhds::Graph>> sel;
  for (auto& g : var.graphs) {
    if (!g) {
      continue;
    }
    if (!opts.top.empty() && g->get_name() != opts.top) {
      continue;
    }
    sel.push_back(g);
  }
  std::sort(sel.begin(), sel.end(), [](const auto& a, const auto& b) { return a->get_name() < b->get_name(); });
  return sel;
}

// Nested per-node block for `cat --target all` (pretty): node header + its
// input pins (with driver) + output pins (with sink).
void tool_cat_all_pretty(hhds::Graph* g, const std::vector<Tool_filter>& filters, std::string& out, size_t& budget,
                         bool& truncated) {
  namespace gu = livehd::graph_util;
  auto take    = [&](const std::string& line) {
    if (budget == 0) {
      truncated = true;
      return false;
    }
    out += line;
    out += '\n';
    --budget;
    return true;
  };
  for (auto node : g->forward_class()) {
    auto nr = tool_node_record(g, node);
    if (!filters.empty() && !tool_match_all(nr, filters)) {
      continue;
    }
    if (!take(std::format("  {}", tool_render_pretty(nr, {"color", "src"})))) {
      return;
    }
    for (const auto& e : node.inp_edges()) {
      int32_t b = gu::bits_of(e.sink);
      if (!take(std::format("    .{}  bits={}{}  <- {}",
                            tool_pin_label(e.sink),
                            b != 0 ? std::to_string(b) : std::string{"nil"},
                            gu::is_unsign(e.sink) ? "" : " signed",
                            tool_endpoint_name(e.driver)))) {
        return;
      }
    }
    for (const auto& e : node.out_edges()) {
      int32_t b = gu::bits_of(e.driver);
      if (!take(std::format("    .{}  bits={}{}  -> {}",
                            tool_pin_label(e.driver),
                            b != 0 ? std::to_string(b) : std::string{"nil"},
                            gu::is_unsign(e.driver) ? "" : " signed",
                            tool_endpoint_name(e.sink)))) {
        return;
      }
    }
  }
}

size_t tool_budget(const Options& opts) {
  return opts.tool_max <= 0 ? std::numeric_limits<size_t>::max() : static_cast<size_t>(opts.tool_max);
}

// `tool cat lg:…` — single-library structured dump (module headers preserved).
void tool_cat_lg(Options& opts, const std::vector<std::string>& lg_dirs, const std::vector<Tool_filter>& filters) {
  if (lg_dirs.size() > 1) {
    throw Lhd_error{"usage", "tool cat takes one lg: input (use tool grep for multi-library search)", ""};
  }
  Tool_target tgt   = parse_tool_target(opts.tool_target);
  auto        dcols = tool_display_cols(opts, tgt);
  bool        jsonl = opts.diag_fmt == Diag_fmt::jsonl;
  auto        graphs = tool_select_graphs(lg_dirs.front(), opts);
  if (graphs.empty()) {
    throw Lhd_error{"config", std::format("lg: input {} holds no matching graphs", lg_dirs.front()), "check --top"};
  }
  std::string out;
  size_t      budget    = tool_budget(opts);
  bool        truncated = false;
  for (const auto& gp : graphs) {
    hhds::Graph* g = gp.get();
    if (!jsonl) {
      out += std::format("module {}\n", g->get_name());
    }
    if (!jsonl && tgt == Tool_target::all) {
      tool_cat_all_pretty(g, filters, out, budget, truncated);
    } else {
      std::vector<Tool_record> recs;
      tool_flat_records(g, tgt, recs);
      for (const auto& r : recs) {
        if (!filters.empty() && !tool_match_all(r, filters)) {
          continue;
        }
        if (budget == 0) {
          truncated = true;
          break;
        }
        out += jsonl ? tool_render_jsonl(r, g->get_name()) : std::format("  {}", tool_render_pretty(r, dcols));
        out += '\n';
        --budget;
      }
    }
    if (truncated) {
      break;
    }
  }
  if (truncated) {
    out += std::format("-- output truncated at --max {} rows; narrow with --top/filters or raise --max (0 = unlimited)\n",
                       opts.tool_max);
  }
  std::fwrite(out.data(), 1, out.size(), stdout);
  std::fflush(stdout);
}

// `tool grep lg:… [lg:…]` — flat matches across one or more libraries, each
// line prefixed `lib/`. A filter is required.
void tool_grep_lg(Options& opts, const std::vector<std::string>& lg_dirs, const std::vector<Tool_filter>& filters) {
  if (filters.empty()) {
    throw Lhd_error{"usage", "tool grep requires at least one filter (e.g. color:nil, name:Mult, bits:>8)", ""};
  }
  Tool_target tgt    = parse_tool_target(opts.tool_target);
  auto        dcols  = tool_display_cols(opts, tgt);
  bool        jsonl  = opts.diag_fmt == Diag_fmt::jsonl;
  std::string out;
  size_t      budget    = tool_budget(opts);
  bool        truncated = false;
  for (const auto& dir : lg_dirs) {
    std::string lib = fs::path(dir).filename().string();
    if (lib.empty()) {
      lib = dir;
    }
    for (const auto& gp : tool_select_graphs(dir, opts)) {
      hhds::Graph*             g = gp.get();
      std::vector<Tool_record> recs;
      tool_flat_records(g, tgt, recs);
      for (const auto& r : recs) {
        if (!tool_match_all(r, filters)) {
          continue;
        }
        if (budget == 0) {
          truncated = true;
          break;
        }
        if (jsonl) {
          out += tool_render_jsonl(r, std::format("{}/{}", lib, g->get_name()));
        } else {
          out += std::format("{}/{}", lib, g->get_name());
          out += "  ";
          out += tool_render_pretty(r, dcols);
        }
        out += '\n';
        --budget;
      }
      if (truncated) {
        break;
      }
    }
    if (truncated) {
      break;
    }
  }
  if (truncated) {
    out += std::format("-- output truncated at --max {} rows; tighten filters or raise --max (0 = unlimited)\n", opts.tool_max);
  }
  std::fwrite(out.data(), 1, out.size(), stdout);
  std::fflush(stdout);
}

// Deterministic per-line listing of one library for `tool diff` (module-prefixed
// so the unified diff stays aligned across modules).
std::vector<std::string> tool_diff_lines(const std::string& dir, Options& opts, Tool_target tgt,
                                         const std::vector<Tool_filter>& filters, const std::vector<std::string>& dcols) {
  std::vector<std::string> lines;
  for (const auto& gp : tool_select_graphs(dir, opts)) {
    hhds::Graph*             g = gp.get();
    std::vector<Tool_record> recs;
    tool_flat_records(g, tgt, recs);
    for (const auto& r : recs) {
      if (!filters.empty() && !tool_match_all(r, filters)) {
        continue;
      }
      lines.push_back(std::format("{}: {}", g->get_name(), tool_render_pretty(r, dcols)));
    }
  }
  return lines;
}

void tool_diff_lg(Options& opts, const std::vector<std::string>& lg_dirs, const std::vector<Tool_filter>& filters) {
  if (lg_dirs.size() != 2) {
    throw Lhd_error{"usage", "tool diff takes exactly two lg: inputs", "e.g. `lhd tool diff lg:before lg:after --attr color`"};
  }
  Tool_target tgt   = parse_tool_target(opts.tool_target);
  auto        dcols = tool_display_cols(opts, tgt);
  auto        a     = tool_diff_lines(lg_dirs[0], opts, tgt, filters, dcols);
  auto        b     = tool_diff_lines(lg_dirs[1], opts, tgt, filters, dcols);
  std::string out;
  if (a == b) {
    out += "identical\n";
  } else {
    print_line_diff(out, a, b, static_cast<size_t>(opts.tool_context));
  }
  std::fwrite(out.data(), 1, out.size(), stdout);
  std::fflush(stdout);
}

size_t tool_node_count(hhds::Graph* g) {
  size_t n = 0;
  for ([[maybe_unused]] auto node : g->forward_class()) {
    ++n;
  }
  return n;
}

// Does a node's cell kind match one of the `tool tree --target kind:<X>`
// selectors? `register`/`reg` aliases the sequential state cells (flop, fflop,
// latch) but NOT memory; `memory`/`mem` aliases memory; any other token matches
// an Ntype name exactly (flop, mux, sub, …), so the tree can spotlight any kind.
bool tool_tree_kind_match(Ntype_op op, const std::vector<std::string>& kinds) {
  std::string_view name = Ntype::get_name(op);
  for (const auto& k : kinds) {
    if (k == "register" || k == "reg" || k == "registers") {
      if (op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch) {
        return true;
      }
    } else if (k == "memory" || k == "mem" || k == "memories") {
      if (op == Ntype_op::Memory) {
        return true;
      }
    } else if (k == name) {
      return true;
    }
  }
  return false;
}

// Width of a node's first non-zero output pin (the "register/memory holds N
// bits" view). 0 = unknown (no sized driver pin, e.g. a dead flop).
int32_t tool_tree_node_bits(const hhds::Node_class& node) {
  namespace gu = livehd::graph_util;
  for (const auto& e : node.out_edges()) {
    if (auto b = gu::bits_of(e.driver); b != 0) {
      return b;
    }
  }
  return 0;
}

// List the nodes of `g` whose kind matches `kinds`, indented to sit beside the
// module's sub-instances (a register/memory is content of the module, not a
// child in the call tree). No-op when `kinds` is empty (the default tree).
void tool_tree_kind_nodes(hhds::Graph* g, const std::vector<std::string>& kinds, int indent, std::string& out,
                          size_t& budget, bool& truncated) {
  namespace gu = livehd::graph_util;
  if (kinds.empty()) {
    return;
  }
  for (auto node : g->forward_class()) {  // topological => deterministic order
    auto op = gu::type_op_of(node);
    if (op == Ntype_op::Sub || !tool_tree_kind_match(op, kinds)) {
      continue;  // Sub instances are the call tree itself (printed elsewhere)
    }
    if (budget == 0) {
      truncated = true;
      return;
    }
    auto bits = tool_tree_node_bits(node);
    out += std::format("{}{}  : {}{}\n",
                       std::string(static_cast<size_t>(indent), ' '),
                       gu::default_instance_name(node),
                       Ntype::get_name(op),
                       bits != 0 ? std::format("  ({}b)", bits) : std::string{});
    --budget;
  }
}

void tool_tree_children(hhds::Graph* g, const std::vector<std::string>& kinds, int maxdepth, int depth,
                        std::set<hhds::Gid>& on_path, std::string& out, size_t& budget, bool& truncated) {
  namespace gu = livehd::graph_util;
  if (depth >= maxdepth) {
    return;
  }
  for (auto node : g->fast_class()) {
    if (node.get_subnode_gid() == hhds::Gid_invalid) {
      continue;
    }
    auto sub = node.get_subnode_graph();
    if (!sub) {
      continue;
    }
    if (budget == 0) {
      truncated = true;
      return;
    }
    out += std::format("{}{}  : {}  [{} nodes]\n",
                       std::string(static_cast<size_t>(depth + 1) * 2, ' '),
                       gu::default_instance_name(node),
                       sub->get_name(),
                       tool_node_count(sub.get()));
    --budget;
    // The instance's registers/memories sit one level in, alongside its own
    // sub-instances (which the recursion below prints at the same indent).
    tool_tree_kind_nodes(sub.get(), kinds, (depth + 2) * 2, out, budget, truncated);
    if (truncated) {
      return;
    }
    hhds::Gid gid = node.get_subnode_gid();
    if (on_path.insert(gid).second) {  // cycle guard
      tool_tree_children(sub.get(), kinds, maxdepth, depth + 1, on_path, out, budget, truncated);
      on_path.erase(gid);
    }
    if (truncated) {
      return;
    }
  }
}

void tool_tree_lg(Options& opts, const std::vector<std::string>& lg_dirs) {
  if (lg_dirs.size() != 1) {
    throw Lhd_error{"usage", "tool tree takes one lg: input", ""};
  }
  int maxdepth = opts.tool_hier < 0 ? std::numeric_limits<int>::max() : opts.tool_hier;  // tree: full by default
  auto graphs   = tool_select_graphs(lg_dirs.front(), opts);
  if (graphs.empty()) {
    throw Lhd_error{"config", std::format("lg: input {} holds no matching graphs", lg_dirs.front()), "check --top"};
  }
  std::string out;
  size_t      budget    = tool_budget(opts);
  bool        truncated = false;
  for (const auto& gp : graphs) {
    hhds::Graph* g = gp.get();
    out += std::format("{}  [{} nodes]\n", g->get_name(), tool_node_count(g));
    tool_tree_kind_nodes(g, opts.tool_kinds, 2, out, budget, truncated);  // top module's own regs/mems
    if (truncated) {
      break;
    }
    std::set<hhds::Gid> on_path;
    tool_tree_children(g, opts.tool_kinds, maxdepth, 0, on_path, out, budget, truncated);
    if (truncated) {
      break;
    }
  }
  if (truncated) {
    out += std::format("-- output truncated at --max {} rows; raise --max (0 = unlimited)\n", opts.tool_max);
  }
  std::fwrite(out.data(), 1, out.size(), stdout);
  std::fflush(stdout);
}

// A positional token's input kind: "lg" / "ln" / "src" (.prp/.v/.sv) / "" (a
// filter term). lg: prefix strips to a directory; ln:/source keep the raw token
// for classify_ln_inputs.
std::string tool_input_kind(const std::string& t) {
  if (auto pos = t.find(':'); pos != std::string::npos && pos != 0) {
    auto k = t.substr(0, pos);
    if (k == "lg" || k == "lgraph" || k == "design") {
      return "lg";
    }
    if (k == "ln" || k == "lnast") {
      return "ln";
    }
    if (k == "verilog" || k == "pyrope") {
      return "src";  // explicit source scheme (URL-like); classify_ln_inputs strips the prefix
    }
  }
  std::string_view sv{t};
  if (sv.ends_with(".prp") || sv.ends_with(".v") || sv.ends_with(".sv")) {
    return "src";  // bare-extension shortcut for pyrope:/verilog:
  }
  return "";
}

void tool_command(Options& opts, Result& res) {
  setup_diag(opts, "tool");
  if (opts.files.empty()) {
    throw Lhd_error{"usage", "tool requires a verb: cat | grep | diff | tree", "e.g. `lhd tool cat lg:dir` or `lhd tool grep color:nil lg:dir`"};
  }
  const std::string verb = opts.files[0];
  if (verb != "cat" && verb != "grep" && verb != "diff" && verb != "tree") {
    throw Lhd_error{"usage", std::format("unknown tool verb '{}'", verb), "use: cat | grep | diff | tree"};
  }

  std::vector<std::string> lg_dirs;
  std::vector<std::string> ln_tokens;  // ln:/source tokens, raw for classify_ln_inputs
  std::vector<Tool_filter> filters;
  for (size_t k = 1; k < opts.files.size(); ++k) {
    const std::string& t    = opts.files[k];
    auto               kind = tool_input_kind(t);
    if (kind == "lg") {
      lg_dirs.push_back(t.substr(t.find(':') + 1));
    } else if (kind == "ln" || kind == "src") {
      ln_tokens.push_back(t);
    } else {
      filters.push_back(parse_tool_filter(t));
    }
  }
  for (const auto& d : opts.in_dirs) {  // --in-dir ln:DIR spelling
    if (d.kind == "ln") {
      ln_tokens.push_back("ln:" + d.path);
    }
  }
  for (const auto& d : opts.ins) {  // --in lg:DIR spelling
    if (d.kind == "lg") {
      lg_dirs.push_back(d.path);
    }
  }

  const bool have_lg = !lg_dirs.empty();
  const bool have_ln = !ln_tokens.empty();
  if (have_lg && have_ln) {
    throw Lhd_error{"usage", "tool takes either lg: or ln:/source inputs, not both", ""};
  }

  if (verb == "tree") {
    if (!have_lg) {
      throw Lhd_error{"usage", "tool tree needs an lg: input", "e.g. `lhd tool tree lg:dir --top m`"};
    }
    tool_tree_lg(opts, lg_dirs);
    return;
  }
  if (verb == "grep") {
    if (!have_lg) {
      throw Lhd_error{"usage", "tool grep needs lg: input(s)", "ln: grep is not yet supported"};
    }
    tool_grep_lg(opts, lg_dirs, filters);
    return;
  }
  if (verb == "cat") {
    if (have_ln) {
      if (!filters.empty()) {
        throw Lhd_error{"usage", "tool cat ln: does not take filters (LNAST cat is whole-unit)", ""};
      }
      tool_cat_ln(opts, res, ln_tokens);
    } else if (have_lg) {
      tool_cat_lg(opts, lg_dirs, filters);
    } else {
      throw Lhd_error{"usage", "tool cat needs an lg:DIR, ln:DIR, or .prp/.v/.sv input", ""};
    }
    return;
  }
  // verb == "diff"
  if (have_ln) {
    if (!filters.empty()) {
      throw Lhd_error{"usage", "tool diff ln: does not take filters", ""};
    }
    tool_diff_ln(opts, res, ln_tokens);
  } else if (have_lg) {
    tool_diff_lg(opts, lg_dirs, filters);
  } else {
    throw Lhd_error{"usage", "tool diff needs two lg: or two ln:/source inputs", ""};
  }
}

}  // namespace

// ---- public entry points ----------------------------------------------------

// Resolve an abbreviated --set/--config key to its canonical
// "<passtoken>.<flag>" form (2h-set_path). `ctx` is the command-path
// established by the command words to the LEFT of the flag, dotted
// (e.g. "pass.abc" after `lhd pass abc`, "" before any command word). Tries
// the key as-is, then prepends successively shorter prefixes of `ctx`
// (longest first); the first candidate whose leading segment(s) name a known
// pass (kSetPasses) wins. Returns the key unchanged when nothing resolves, so
// check_known_set_passes still emits the standard unknown-pass error. Uses
// only the constexpr kSetPasses table, so it is safe before init_engine().
std::string canonical_set_key(std::string_view key, std::string_view ctx) {
  auto names_a_pass = [](std::string_view candidate) {
    auto pos = candidate.rfind('.');
    return pos != std::string_view::npos && !set_pass_method(candidate.substr(0, pos)).empty();
  };
  if (names_a_pass(key)) {
    return std::string{key};
  }
  std::string prefix{ctx};
  while (!prefix.empty()) {
    std::string candidate = prefix + "." + std::string{key};
    if (names_a_pass(candidate)) {
      return candidate;
    }
    auto pos = prefix.rfind('.');
    if (pos == std::string::npos) {
      break;
    }
    prefix.resize(pos);
  }
  return std::string{key};
}

Lhd_error classify_engine_failure(std::string_view fallback_msg) {
  const auto& recs = livehd::diag::sink().records();
  for (auto it = recs.rbegin(); it != recs.rend(); ++it) {
    if (it->severity == livehd::diag::Severity::error) {
      return Lhd_error{map_diag_category(it->category), it->message, it->hint};
    }
  }
  return Lhd_error{"internal", std::string{fallback_msg}, ""};
}

void init_engine() {
  static bool done = false;  // engine commands and `lhd list options`/`describe pass.flag` may both reach here
  if (done) {
    return;
  }
  done = true;
  for (const auto& it : Pass_plugin::get_registry()) {
    it.second();
  }
  setup_inou_yosys();
}

std::vector<Set_option> list_set_options() {
  init_engine();
  std::vector<Set_option> out;
  for (const auto& [set_name, method] : kSetPasses) {
    const auto* m = Pass::eprp.get_method(method);
    if (m == nullptr) {
      continue;  // defensive: every kSetPasses method registers in init_engine
    }
    for (const auto& [flag, attr] : m->labels) {
      if (is_kernel_label(flag)) {
        continue;
      }
      out.push_back(Set_option{std::format("{}.{}", set_name, flag), std::string{method}, attr.default_value, attr.help});
    }
  }
  std::sort(out.begin(), out.end(), [](const Set_option& a, const Set_option& b) { return a.name < b.name; });
  return out;
}

void run_engine_command(Options& opts, Result& res) {
  validate_emits(opts);
  validate_dumps(opts);
  check_known_set_passes(opts);  // --set AND --config table names: a typo'd pass must error, not no-op
  apply_log_settings(opts);      // --set <channel>.log=<level> -> livehd::log (developer logging)

  // --depfile is supported on both frontends: the Verilog flow lists its
  // declared inputs, and the Pyrope flow additionally folds in every file the
  // Source_locator tables saw (the locator's file table IS the
  // actually-read-files list, covering import discovery).

  if (opts.command == "compile") {
    compile_command(opts, res);
  } else if (opts.command == "lec") {
    lec_command(opts, res);
  } else if (opts.command == "scan") {
    scan_command(opts, res);
  } else if (opts.command == "tool") {
    tool_command(opts, res);
  } else if (opts.command == "pass") {
    pass_command(opts, res);
  } else {
    throw Lhd_error{"usage", std::format("unknown command '{}'", opts.command), ""};
  }
}

}  // namespace lhd
