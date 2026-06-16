//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "absl/container/flat_hash_set.h"
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
    auto pass   = key.substr(0, pos);
    auto flag   = key.substr(pos + 1);
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
  if (opts.command == "check" || opts.command == "lec") {
    for (const char* k : {"lg", "verilog", "ln", "pyrope", "lnast-dump"}) {
      reject_emit_kind(opts, k, {"usage", std::format("{} has no outputs beyond the result", opts.command), ""});
    }
  }
  if (opts.command == "ln.cat" || opts.command == "ln.diff") {
    for (const char* k : {"lg", "verilog", "ln", "pyrope", "lnast-dump"}) {
      reject_emit_kind(opts,
                       k,
                       {"usage",
                        std::format("{} prints to stdout and has no --emit outputs", opts.command),
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
  if (opts.command == "check" || opts.command == "lec" || opts.command == "scan" || opts.command == "ln.cat"
      || opts.command == "ln.diff") {
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
         || find_slot(opts.emit_dirs, "lnast-dump") != nullptr || wants_dump(opts, "lnast") || opts.command == "ln.cat"
         || opts.command == "ln.diff";
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
      // any other prefix: treat as a plain path (mirror route_positional)
    }
    std::string_view sv{t};
    if (sv.ends_with(".prp")) {
      in.prp_files.push_back(t);
    } else if (sv.ends_with(".v") || sv.ends_with(".sv")) {
      in.sv_files.push_back(t);
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
void print_line_diff(std::string& out, const std::vector<std::string>& a, const std::vector<std::string>& b) {
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
  constexpr size_t  kCtx = 2;
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

void ln_cat_command(Options& opts, Result& res) {
  setup_diag(opts, "ln.cat");
  auto in = classify_ln_inputs(opts.files, "ln.cat");
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

void ln_diff_command(Options& opts, Result& res) {
  setup_diag(opts, "ln.diff");
  if (opts.files.size() != 2) {
    throw Lhd_error{"usage",
                    "ln.diff takes exactly two inputs (each a .prp/.v/.sv source or an ln:DIR forest)",
                    "e.g. `lhd ln.diff old.prp new.prp` or `lhd ln.diff ln:before/ x.prp`"};
  }
  auto a_units = sorted_by_name(filter_top(ln_tool_units(opts, res, classify_ln_inputs({opts.files[0]}, "ln.diff")), opts.top));
  auto b_units = sorted_by_name(filter_top(ln_tool_units(opts, res, classify_ln_inputs({opts.files[1]}, "ln.diff")), opts.top));

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
                                opts.files[0],
                                join_csv(names_of(a_units)),
                                opts.files[1],
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
    out            += std::format("//---- ln.diff {} vs {}\n", la->get_top_module_name(), lb->get_top_module_name());

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
      print_line_diff(out, dump_lines_no_attrs(la), dump_lines_no_attrs(lb));
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

// Return a verilog file for an --impl/--ref side, materializing non-verilog
// kinds into the scratch workdir: lg: libraries go straight through cgen;
// pyrope:/ln: inputs first run the compile pipeline (parse/load -> upass ->
// tolg -> recipe graph passes, default O1) to graphs.
std::string materialize_verilog(Options& opts, Result& res, const std::string& kind, const std::string& path,
                                std::string_view side) {
  res.inputs.push_back(path);
  if (kind == "verilog") {
    check_inputs_exist({path});
    return path;
  }
  Eprp_var var;
  if (kind == "lg") {
    if (!fs::is_directory(path)) {
      throw Lhd_error{"missing_file", std::format("lg: input not found: {}", path), ""};
    }
    auto& lib = livehd::Hhds_graph_library::instance(path);
    for (const hhds::Gid id : lib.all_gids()) {  // gids are sparse name-hashes now
      auto g = lib.get_graph(id);
      if (g) {
        var.add(g);
      }
    }
    if (var.graphs.empty()) {
      throw Lhd_error{"config", std::format("lg: input {} holds no graphs", path), ""};
    }
  } else if (kind == "pyrope" || kind == "ln") {
    if (kind == "pyrope") {
      check_inputs_exist({path});
      run_step("inou.prp",
               var,
               {
                   {"files", path}
      },
               opts,
               res);
    } else {
      if (!fs::is_directory(path)) {
        throw Lhd_error{"missing_file", std::format("ln: input not found: {}", path), "an ln: input is a Forest save directory"};
      }
      for (auto& ln : load_ln_dir(path)) {
        var.add(ln);
      }
    }
    auto lib_path = std::format("{}/check_{}_lgdb", workdir(opts), side);
    lower_lnasts(opts, res, var, lib_path, /*need_graphs=*/true);
    graph_pipeline_and_emits(opts, res, var, lib_path);  // recipe passes only — check declares no emits
    if (var.graphs.empty()) {
      throw Lhd_error{"config",
                      std::format("{}: input {} produced no synthesizable modules", kind, path),
                      "a pure-comptime design has no module IO to check"};
    }
  } else {
    throw Lhd_error{"usage", std::format("check accepts verilog:, pyrope:, ln:, or lg: inputs, got {}:", kind), ""};
  }
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

void check_command(Options& opts, Result& res) {
  setup_diag(opts, "check");
  if (opts.impl_path.empty() || opts.ref_path.empty()) {
    throw Lhd_error{"usage", "check requires --impl KIND:PATH and --ref KIND:PATH", ""};
  }

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
  auto log  = next_log_path(opts, "check.lgcheck");
  cmd      += std::format(" >> {} 2>&1", shell_quote(fs::absolute(log).string()));

  res.recipe_steps.emplace_back("check.lgcheck");
  int rc = std::system(cmd.c_str());
  if (opts.verbose) {
    mirror_log_to_stderr(log);
  }
  if (rc != 0) {
    throw Lhd_error{"equiv_fail",
                    std::format("equivalence check failed ({} vs {})", opts.impl_path, opts.ref_path),
                    std::format("see {}", log)};
  }
}

// ---- lec (in-process relational equivalence via pass.lec / Pono) ------------

// Load one --impl/--ref side into `var.graphs` WITHOUT cgen (the front half of
// materialize_verilog). lec consumes the graph directly. verilog: is rejected
// here -- there is no in-process Verilog reader on this path; use `lhd check`
// (lgcheck) for verilog: LEC, or feed lg:/pyrope:/ln:.
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
  } else if (kind == "pyrope" || kind == "ln") {
    if (kind == "pyrope") {
      check_inputs_exist({path});
      run_step("inou.prp", var, {{"files", path}}, opts, res);
    } else {
      if (!fs::is_directory(path)) {
        throw Lhd_error{"missing_file", std::format("ln: input not found: {}", path), "an ln: input is a Forest save directory"};
      }
      for (auto& ln : load_ln_dir(path)) {
        var.add(ln);
      }
    }
    auto lib_path = std::format("{}/lec_{}_lgdb", workdir(opts), side);
    lower_lnasts(opts, res, var, lib_path, /*need_graphs=*/true);
    graph_pipeline_and_emits(opts, res, var, lib_path);
  } else {
    throw Lhd_error{"usage",
                    std::format("lec accepts lg:, pyrope:, or ln: inputs, got {}:", kind),
                    "verilog: LEC goes through `lhd check` (lgcheck)"};
  }
  if (var.graphs.empty()) {
    throw Lhd_error{"config", std::format("lec {} input {} holds no graphs", side, path), ""};
  }
}

void lec_command(Options& opts, Result& res) {
  setup_diag(opts, "lec");
  if (opts.impl_path.empty() || opts.ref_path.empty()) {
    throw Lhd_error{"usage", "lec requires --impl KIND:PATH and --ref KIND:PATH", "graph inputs: lg:/pyrope:/ln:"};
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

  Eprp_var::Eprp_dict labels;
  merge_sets(opts, "lec", labels);

  auto label = [&](std::string_view k, std::string_view def) -> std::string {
    auto it = labels.find(std::string{k});
    return it == labels.end() ? std::string{def} : it->second;
  };
  bool cross = label("cross", "false") != "false" && label("cross", "false") != "0";

  // Discharge in-process via pass/lec (L1). The engine is the authority on the
  // non-cross path; in cross mode we additionally run lgcheck and assert
  // agreement (the strongest encoder check).
  livehd::lec::Lec_options o;
  o.engine  = label("engine", "ind");
  o.solver  = label("solver", "cvc5");
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

  // --depfile is supported on both frontends: the Verilog flow lists its
  // declared inputs, and the Pyrope flow additionally folds in every file the
  // Source_locator tables saw (the locator's file table IS the
  // actually-read-files list, covering import discovery).

  if (opts.command == "compile") {
    compile_command(opts, res);
  } else if (opts.command == "check") {
    check_command(opts, res);
  } else if (opts.command == "lec") {
    lec_command(opts, res);
  } else if (opts.command == "scan") {
    scan_command(opts, res);
  } else if (opts.command == "ln.cat") {
    ln_cat_command(opts, res);
  } else if (opts.command == "ln.diff") {
    ln_diff_command(opts, res);
  } else if (opts.command == "pass") {
    pass_command(opts, res);
  } else {
    throw Lhd_error{"usage", std::format("unknown command '{}'", opts.command), ""};
  }
}

}  // namespace lhd
