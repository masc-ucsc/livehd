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
#include <sstream>
#include <string>
#include <vector>

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

// --set pass[.idx].flag=value entries for `pass_name` -> EPRP labels.
void merge_sets(const Options& opts, std::string_view pass_name, Eprp_var::Eprp_dict& labels) {
  for (const auto& [key, value] : opts.sets) {
    auto pos = key.find('.');
    if (pos == std::string::npos) {
      throw Lhd_error{"usage", std::format("--set expects pass.flag=value, got '{}={}'", key, value), ""};
    }
    auto pass = key.substr(0, pos);
    auto flag = key.substr(pos + 1);
    if (pass != pass_name) {
      continue;
    }
    if (flag.find('.') != std::string::npos) {
      throw Lhd_error{"unsupported",
                      std::format("--set repeated-pass index addressing ('{}') is not implemented yet", key),
                      "built-in recipes run each pass once; use pass.flag=value"};
    }
    labels[flag] = value;
  }
}

void check_known_set_passes(const Options& opts) {
  for (const auto& [key, value] : opts.sets) {
    (void)value;
    auto pass = key.substr(0, key.find('.'));
    if (pass != "upass" && pass != "cprop" && pass != "bitwidth") {
      throw Lhd_error{"usage",
                      std::format("--set/--config references unknown pass '{}'", pass),
                      "known passes: upass, cprop, bitwidth (see `lhd describe recipe:O2`)"};
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
    return {{"cprop", "pass.cprop"}};
  }
  if (r == "O2") {
    return {{"cprop", "pass.cprop"}, {"bitwidth", "pass.bitwidth"}};
  }
  throw Lhd_error{"usage", std::format("unknown recipe '{}'", r), "built-in recipes: O0, O1, O2 (`lhd list recipes`)"};
}

uint64_t hash_bytes(const std::string& bytes) { return lh::woothash64(bytes.data(), bytes.size(), 1021); }

// ---- typed-emit helpers -----------------------------------------------------

void write_manifest(const std::string& dir, std::string_view kind, const std::vector<std::pair<std::string, uint64_t>>& units) {
  std::ofstream ofs(dir + "/manifest.json");
  if (!ofs.is_open()) {
    throw Lhd_error{"config", std::format("could not write {}/manifest.json", dir), ""};
  }
  // `ln:` units live inside the hhds::Forest save (no per-unit file); the
  // per-unit file kinds carry a "file" entry.
  std::string_view ext = kind == "pyrope" ? ".prp" : (kind == "verilog" ? ".v" : (kind == "lnast-dump" ? ".lnast" : ""));
  ofs << "{\"schema_version\":1,\"kind\":\"" << kind << "\",\"units\":[";
  bool first = true;
  for (const auto& [name, hash] : units) {
    if (!first) {
      ofs << ',';
    }
    first = false;
    ofs << "{\"name\":\"" << name << "\"";
    if (!ext.empty()) {
      ofs << ",\"file\":\"" << name << ext << "\"";
    }
    ofs << ",\"content_hash\":\"" << std::format("{:016x}", hash) << "\"}";
  }
  ofs << "]}\n";
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

  auto forest = hhds::Forest::create();
  std::vector<std::pair<std::string, uint64_t>> manifest;
  for (const auto& ln : sorted) {
    std::string name{ln->get_top_module_name()};
    if (!manifest.empty() && manifest.back().first == name) {
      throw Lhd_error{"config", std::format("duplicate LNAST unit name '{}'", name), ""};
    }
    ln->export_into(*forest);
    std::ostringstream oss;
    ln->dump(oss);  // hash the canonical text form (deterministic)
    manifest.emplace_back(name, hash_bytes(oss.str()));
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
  run_step("inou.cgen.verilog", var, {{"odir", odir}}, opts, res);

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
    auto names = cgen_into(opts, res, var, e.path);
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
    auto scratch = std::format("{}/cgen_{:03d}", workdir(opts), ++step_counter);
    auto names   = cgen_into(opts, res, var, scratch);
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
    run_step("pass.prp_writer", var, {{"odir", e.path}}, opts, res);

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

  // ln:/lg:/pyrope:/lnast-dump: outputs are containers (a Forest dir, a
  // GraphLibrary dir, one file per unit) — directory form only, never a
  // single file.
  for (const auto& e : opts.emits) {
    if (e.kind == "ln" || e.kind == "lg" || e.kind == "pyrope" || e.kind == "lnast-dump") {
      throw Lhd_error{"usage",
                      std::format("--emit {0}:PATH is a directory container; use --emit-dir {0}:DIR/", e.kind),
                      "ln: is a Forest save dir, lg: a GraphLibrary save dir, pyrope:/lnast-dump: one file per unit"};
    }
  }

  bool has_ln_inputs = false;
  for (const auto& in : opts.in_dirs) {
    has_ln_inputs |= in.kind == "ln";
  }
  const bool has_sources = !opts.files.empty();

  if (opts.command == "elaborate") {
    reject_emit_kind(opts, "verilog", {"usage", "elaborate does not emit verilog", "use `lhd synth` or `lhd compile`"});
    reject_emit_kind(opts,
                     "pyrope",
                     {"unsupported",
                      "pyrope re-emission needs the post-upass LNAST",
                      "use `lhd compile --emit-dir pyrope:DIR/` (or synth from ln: inputs)"});
    if (opts.language == "verilog" && opts.reader != "slang") {
      for (const char* k : {"ln", "lnast-dump"}) {
        reject_emit_kind(opts,
                         k,
                         {"unsupported",
                          "the yosys-* readers elaborate to LGraphs (lg:), not LNAST",
                          "use --emit-dir lg:DIR/, or --reader slang (the direct SV -> LNAST front-end)"});
      }
    }
  }
  if (opts.language == "verilog" && opts.reader == "slang") {
    // inou.slang still emits the pre-typesystem LNAST conventions (module-level
    // `__ubits` stores, no comb lambda io_meta), which tolg cannot lower yet.
    for (const char* k : {"lg", "verilog"}) {
      reject_emit_kind(opts,
                       k,
                       {"unsupported",
                        "--reader slang stops at LNAST today (inou.slang predates the current io/typesystem conventions)",
                        "emit ln:/pyrope: from --reader slang, or use --reader yosys-slang for lg:/verilog:"});
    }
  }
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
  if (opts.command == "synth" && !has_ln_inputs && !has_sources) {
    // ln/pyrope/lnast-dump outputs need LNAST units on the pipe; an lg
    // (LGraph) input has none and there is no decompiler.
    for (const char* k : {"ln", "lnast-dump"}) {
      reject_emit_kind(opts, k, {"unsupported", "there is no LGraph -> LNAST decompiler", "ln: outputs need ln: inputs"});
    }
    reject_emit_kind(opts,
                     "pyrope",
                     {"unsupported", "there is no LGraph -> LNAST decompiler", "pyrope outputs need ln:/pyrope inputs"});
  }
  if (opts.command == "check") {
    for (const char* k : {"lg", "verilog", "ln", "pyrope", "lnast-dump"}) {
      reject_emit_kind(opts, k, {"usage", "check has no outputs beyond the result", ""});
    }
  }
  if (opts.command == "ln.cat" || opts.command == "ln.diff") {
    for (const char* k : {"lg", "verilog", "ln", "pyrope", "lnast-dump"}) {
      reject_emit_kind(opts,
                       k,
                       {"usage",
                        std::format("{} prints to stdout and has no --emit outputs", opts.command),
                        "use elaborate/synth/compile for declared artifacts"});
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
  if (opts.command == "check" || opts.command == "scan" || opts.command == "ln.cat" || opts.command == "ln.diff") {
    throw Lhd_error{"usage", std::format("{} has no --dump observables", opts.command), "--dump applies to elaborate/synth/compile"};
  }
  if (opts.language == "verilog" && opts.reader != "slang" && (wants_dump(opts, "parse") || wants_dump(opts, "lnast"))) {
    throw Lhd_error{"unsupported",
                    "the yosys-* readers elaborate to LGraphs, so there is no LNAST to dump",
                    "use --dump lg, or --reader slang (the direct SV -> LNAST front-end)"};
  }
  if (opts.language == "verilog" && opts.reader == "slang" && wants_dump(opts, "lg")) {
    throw Lhd_error{"unsupported",
                    "--reader slang stops at LNAST today (inou.slang predates the current io/typesystem conventions)",
                    "use --dump parse|lnast, or --reader yosys-slang for the LGraph view"};
  }
  if (opts.command == "synth") {
    if (wants_dump(opts, "parse")) {
      throw Lhd_error{"usage", "synth consumes pre-parsed IR; --dump parse is an elaborate/compile observable", ""};
    }
    if (wants_dump(opts, "lnast")) {
      bool has_ln = false;
      for (const auto& in : opts.in_dirs) {
        has_ln |= in.kind == "ln";
      }
      if (!has_ln) {
        throw Lhd_error{"usage", "--dump lnast needs ln: inputs (an lg: input carries no LNAST)", ""};
      }
    }
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

  run_step("inou.prp", var, {{"files", join_csv(opts.files)}}, opts, res);
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

  Eprp_var::Eprp_dict labels{{"files", join_csv(opts.files)},
                             {"path", lib_path},
                             {"top", opts.top.empty() ? std::string{"-auto-top"} : opts.top},
                             {"frontend", opts.reader == "yosys-verilog" ? std::string{"verilog"} : std::string{"slang"}}};
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
  if (!opts.raw_args.empty()) {
    throw Lhd_error{"unsupported",
                    "raw `--` args are yosys-reader only (inou.slang takes no flag string)",
                    "use --reader yosys-slang, or file an inou.slang labels request"};
  }
  check_inputs_exist(opts.files);
  res.inputs = opts.files;

  run_step("inou.slang", var, {{"files", join_csv(opts.files)}}, opts, res);
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

// Imports are comptime string literals (see import.md), so the dependency
// list is statically extractable from the parse: every LNAST func_call of
// the form (target, const "import", const "<module>").
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
  std::string out;
  for (char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
    }
    out += c;
  }
  return out;
}

// `lhd scan FILES...` — emit each pyrope file's import strings (raw, as
// written; path resolution lands with the import.md resolver). The payload
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
    first = false;
    payload += std::format("{{\"file\":\"{}\",\"imports\":[", json_escape_min(f));
    bool ifirst = true;
    for (const auto& imp : collect_imports(ln)) {
      if (!ifirst) {
        payload += ',';
      }
      ifirst = false;
      payload += std::format("\"{}\"", json_escape_min(imp));
    }
    payload += "]}";
  }
  payload += "]";
  res.scan_json = payload;
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
  run_step(in.prp_files.empty() ? "inou.slang" : "inou.prp", var, {{"files", join_csv(files)}}, opts, res);
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
        out += "  ...\n";
        in_gap = true;
      }
      continue;
    }
    in_gap = false;
    out += ops[k].first;
    out += ' ';
    out += *ops[k].second;
    out += '\n';
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
    const auto& la = a_units[k];
    const auto& lb = b_units[k];
    out += std::format("//---- ln.diff {} vs {}\n", la->get_top_module_name(), lb->get_top_module_name());

    // The hhds tree diff (Zhang-Shasha edit distance) on the live trees:
    // nodes match on (lnast type, name) — loc/fname attrs are ignored.
    auto cost_fn = [&la, &lb](const hhds::Tree::Node_class& x, const hhds::Tree::Node_class& y) -> double {
      if (la->get_type(x) != lb->get_type(y)) {
        return 1.0;
      }
      return la->get_name(x) == lb->get_name(y) ? 0.0 : 1.0;
    };
    auto ted = hhds::TreeEditDistance::compute(la->tree_ptr(), lb->tree_ptr(), hhds::EditCosts{}, cost_fn);
    total += ted.distance;

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
void elaborate_command(Options& opts, Result& res) {
  auto        ir     = gather_ir_inputs(opts, "elaborate");
  const auto* ln_out = find_slot(opts.emit_dirs, "ln");
  const auto* lg_out = find_slot(opts.emit_dirs, "lg");

  if (!opts.files.empty() && opts.language == "verilog") {
    if (!ir.ln_dirs.empty() || !ir.lg_dirs.empty()) {
      throw Lhd_error{"usage", "verilog elaboration takes no ln:/lg: inputs", ""};
    }
    setup_diag(opts, "elaborate.verilog");
    Eprp_var var;
    if (opts.reader == "slang") {  // direct SV -> LNAST: the pyrope flow from here
      slang_parse(opts, res, var);
      if (ln_out != nullptr) {
        save_ln_dir(opts, res, filter_top(var.lnasts, opts.top), ln_out->path);
      }
      emit_lnast_dump_outputs(filter_top(var.lnasts, opts.top), opts, res);  // post-parse dump
      if (lg_out != nullptr || wants_dump(opts, "lnast")) {
        lower_lnasts(opts, res, var, lg_out ? lg_out->path : workdir(opts) + "/lgdb", /*need_graphs=*/lg_out != nullptr);
        if (lg_out != nullptr) {
          livehd::Hhds_graph_library::save(lg_out->path);
          res.outputs.push_back(lg_out->path);
        }
      }
      write_depfile(opts, res);
      return;
    }
    auto lib_path = verilog_frontend(opts, res, var);
    if (wants_dump(opts, "lg")) {
      screen_dump_graphs(var, "post-tolg");
    }
    if (lg_out != nullptr) {
      livehd::Hhds_graph_library::save(lib_path);
      res.outputs.push_back(lg_out->path);
    }
    write_depfile(opts, res);
    return;
  }

  if (!opts.files.empty()) {  // pyrope sources (+ optional ln: imports)
    if (!ir.lg_dirs.empty()) {
      throw Lhd_error{"usage", "lg: inputs cannot join a source elaboration", "aggregate lg: libraries in a separate step"};
    }
    setup_diag(opts, "elaborate.pyrope");
    Eprp_var var;
    auto     n_imports = pyrope_parse(opts, res, var, ir.ln_dirs);

    // Emit only THIS invocation's source units (imports have their own
    // elaboration step and ln: directory).
    std::vector<std::shared_ptr<Lnast>> source_units(var.lnasts.begin() + static_cast<long>(n_imports), var.lnasts.end());
    if (ln_out != nullptr) {
      save_ln_dir(opts, res, filter_top(source_units, opts.top), ln_out->path);
    }
    emit_lnast_dump_outputs(filter_top(source_units, opts.top), opts, res);  // post-parse dump
    if (lg_out != nullptr || wants_dump(opts, "lnast") || wants_dump(opts, "lg")) {
      // Lower onto LGraphs (imports stay on the pipe for call resolution).
      lower_lnasts(opts, res, var, lg_out ? lg_out->path : workdir(opts) + "/lgdb",
                   /*need_graphs=*/lg_out != nullptr || wants_dump(opts, "lg"));
      if (wants_dump(opts, "lg")) {
        screen_dump_graphs(var, "post-tolg");
      }
      if (lg_out != nullptr) {
        livehd::Hhds_graph_library::save(lg_out->path);
        res.outputs.push_back(lg_out->path);
      }
    }
    return;
  }

  // Aggregation: IR inputs only.
  if (!ir.ln_dirs.empty() && !ir.lg_dirs.empty()) {
    throw Lhd_error{"unsupported", "mixing ln: and lg: inputs in one aggregation is not supported yet", ""};
  }
  if (!ir.ln_dirs.empty()) {
    setup_diag(opts, "elaborate.aggregate");
    Eprp_var var;
    for (const auto& d : ir.ln_dirs) {
      res.inputs.push_back(d);
      for (auto& ln : load_ln_dir(d)) {
        var.add(ln);
      }
    }
    run_step("pass.lnastfmt", var, {}, opts, res);
    if (wants_dump(opts, "parse")) {
      screen_dump_lnasts(var.lnasts, "post-parse");
    }
    if (ln_out != nullptr) {
      save_ln_dir(opts, res, var.lnasts, ln_out->path);
    }
    emit_lnast_dump_outputs(var.lnasts, opts, res);
    if (lg_out != nullptr || wants_dump(opts, "lnast") || wants_dump(opts, "lg")) {
      // Re-lowering all units into ONE fresh library is the v0 aggregation:
      // gids stay consistent by construction.
      lower_lnasts(opts, res, var, lg_out ? lg_out->path : workdir(opts) + "/lgdb",
                   /*need_graphs=*/lg_out != nullptr || wants_dump(opts, "lg"));
      if (wants_dump(opts, "lg")) {
        screen_dump_graphs(var, "post-tolg");
      }
      if (lg_out != nullptr) {
        livehd::Hhds_graph_library::save(lg_out->path);
        res.outputs.push_back(lg_out->path);
      }
    }
    if (ln_out == nullptr && lg_out == nullptr && find_slot(opts.emit_dirs, "lnast-dump") == nullptr && opts.dumps.empty()) {
      throw Lhd_error{"usage", "aggregation needs --emit-dir ln:DIR/ and/or --emit-dir lg:DIR/ (or a --dump)", ""};
    }
    return;
  }
  if (!ir.lg_dirs.empty()) {
    setup_diag(opts, "elaborate.aggregate");
    if (ir.lg_dirs.size() > 1) {
      throw Lhd_error{"unsupported",
                      "linking multiple lg: libraries needs gid remapping (not implemented yet)",
                      "aggregate from the ln: directories instead (re-lowers into one library)"};
    }
    if (lg_out == nullptr) {
      throw Lhd_error{"usage", "an lg: pass-through needs --emit-dir lg:DIR/", ""};
    }
    const auto& src = ir.lg_dirs.front();
    if (!fs::is_directory(src)) {
      throw Lhd_error{"missing_file", std::format("lg: input not found: {}", src), ""};
    }
    res.inputs.push_back(src);
    if (fs::weakly_canonical(lg_out->path) != fs::weakly_canonical(src)) {
      std::error_code ec;
      fs::remove_all(lg_out->path, ec);
      ensure_dir(lg_out->path);
      fs::copy(src, lg_out->path, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    }
    res.outputs.push_back(lg_out->path);
    return;
  }
  throw Lhd_error{"usage", "elaborate requires source files or ln:/lg: inputs", ""};
}

// ---- synth ------------------------------------------------------------------

// Lower LNAST units: lnastfmt + upass, then (only when `need_graphs`) the
// terminal LNAST->LGraph sub-pass into the library at lib_path — the
// CLI-level tolg:0|1 gate, derived from the requested emits.
void lower_lnasts(Options& opts, Result& res, Eprp_var& var, const std::string& lib_path, bool need_graphs) {
  run_step("pass.lnastfmt", var, {}, opts, res);

  Eprp_var::Eprp_dict up{{"constprop", "1"}, {"verifier", "false"}};
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
  run_step("pass.upass", var, up, opts, res);

  if (wants_dump(opts, "lnast")) {
    screen_dump_lnasts(var.lnasts, "post-upass");
  }

  if (!need_graphs) {
    return;  // no lg/verilog emit requested -> skip the tolg lowering
  }
  {
    Stdout_to_log redirect(next_log_path(opts, "lnast.tolg"));
    for (const auto& ln : var.lnasts) {
      auto g = uPass_tolg::run(ln, lib_path);
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
      throw Lhd_error{"config",
                      "no graphs to save into --emit-dir lg:",
                      "the input produced no synthesizable modules"};
    }
    // By construction lib_path == the lg output dir whenever one was
    // declared (tolg/copy targeted it), so saving the library is the emit.
    livehd::Hhds_graph_library::save(lib_path);
    res.outputs.push_back(lg_out->path);
  }

  emit_verilog_outputs(opts, res, var);
  emit_ln_outputs(var.lnasts, opts, res);          // post-upass forest (synth from ln: inputs)
  emit_pyrope_outputs(opts, res, var);             // post-upass .prp re-emission (pass.prp_writer)
  emit_lnast_dump_outputs(var.lnasts, opts, res);  // post-upass textual dump (debug/test observable)
}

void synth_command(Options& opts, Result& res) {
  setup_diag(opts, "synth");

  if (!opts.files.empty()) {
    throw Lhd_error{"usage",
                    std::format("synth does not take source files ('{}')", opts.files.front()),
                    "synth consumes IR: ln:DIR (forest) or lg:DIR (graph library)"};
  }
  auto ir = gather_ir_inputs(opts, "synth");

  if (ir.ln_dirs.empty() && ir.lg_dirs.empty()) {
    throw Lhd_error{"usage", "synth requires ln:DIR and/or lg:DIR inputs", ""};
  }
  if (!ir.ln_dirs.empty() && !ir.lg_dirs.empty()) {
    throw Lhd_error{"unsupported", "mixing ln: and lg: inputs in one synth is not supported yet", ""};
  }
  if (ir.lg_dirs.size() > 1) {
    throw Lhd_error{"unsupported",
                    "multiple lg: inputs are not supported yet (gids are library-scoped)",
                    "aggregate upstream from ln: units, or merge into one library"};
  }

  const auto* lg_out = find_slot(opts.emit_dirs, "lg");
  Eprp_var    var;
  std::string lib_path;

  if (!ir.ln_dirs.empty()) {
    for (const auto& d : ir.ln_dirs) {
      res.inputs.push_back(d);
      for (auto& ln : load_ln_dir(d)) {
        var.add(ln);
      }
    }
    lib_path = lg_out ? lg_out->path : workdir(opts) + "/lgdb";
    lower_lnasts(opts, res, var, lib_path, emits_need_graphs(opts));
  } else {
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
    auto&     lib = livehd::Hhds_graph_library::instance(lib_path);
    hhds::Gid cap = static_cast<hhds::Gid>(lib.capacity());
    for (hhds::Gid id = 1; id < cap; ++id) {
      if (!lib.has_graph(id)) {
        continue;
      }
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

// ---- compile (fused elaborate + synth) --------------------------------------

void compile_command(Options& opts, Result& res) {
  auto     ir = gather_ir_inputs(opts, "compile");
  Eprp_var var;
  if (opts.language == "pyrope") {
    setup_diag(opts, "compile.pyrope");
    if (opts.files.empty()) {
      throw Lhd_error{"usage", "compile pyrope requires at least one .prp file", ""};
    }
    pyrope_parse(opts, res, var, ir.ln_dirs);
    const auto* lg_out   = find_slot(opts.emit_dirs, "lg");
    std::string lib_path = lg_out ? lg_out->path : workdir(opts) + "/lgdb";
    lower_lnasts(opts, res, var, lib_path, emits_need_graphs(opts));
    graph_pipeline_and_emits(opts, res, var, lib_path);
  } else {
    setup_diag(opts, "compile.verilog");
    if (opts.files.empty()) {
      throw Lhd_error{"usage", "compile verilog requires at least one .v file", ""};
    }
    if (!ir.ln_dirs.empty() || !ir.lg_dirs.empty()) {
      throw Lhd_error{"usage", "compile verilog takes no ln:/lg: inputs", ""};
    }
    if (opts.reader == "slang") {  // direct SV -> LNAST: the pyrope flow from here
      slang_parse(opts, res, var);
      const auto* lg_out   = find_slot(opts.emit_dirs, "lg");
      std::string lib_path = lg_out ? lg_out->path : workdir(opts) + "/lgdb";
      lower_lnasts(opts, res, var, lib_path, emits_need_graphs(opts));
      graph_pipeline_and_emits(opts, res, var, lib_path);
    } else {
      auto lib_path = verilog_frontend(opts, res, var);
      graph_pipeline_and_emits(opts, res, var, lib_path);
    }
  }
  write_depfile(opts, res);
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

// Return a verilog file for an --impl/--ref side, materializing lg:
// libraries through cgen into the scratch workdir.
std::string materialize_verilog(Options& opts, Result& res, const std::string& kind, const std::string& path,
                                std::string_view side) {
  res.inputs.push_back(path);
  if (kind == "verilog") {
    check_inputs_exist({path});
    return path;
  }
  if (kind != "lg") {
    throw Lhd_error{"usage", std::format("check accepts verilog: or lg: inputs, got {}:", kind), ""};
  }
  if (!fs::is_directory(path)) {
    throw Lhd_error{"missing_file", std::format("lg: input not found: {}", path), ""};
  }
  Eprp_var  var;
  auto&     lib = livehd::Hhds_graph_library::instance(path);
  hhds::Gid cap = static_cast<hhds::Gid>(lib.capacity());
  for (hhds::Gid id = 1; id < cap; ++id) {
    if (lib.has_graph(id)) {
      auto g = lib.get_graph(id);
      if (g) {
        var.add(g);
      }
    }
  }
  if (var.graphs.empty()) {
    throw Lhd_error{"config", std::format("lg: input {} holds no graphs", path), ""};
  }
  auto scratch = std::format("{}/check_{}", workdir(opts), side);
  auto names   = cgen_into(opts, res, var, scratch);
  auto out     = std::format("{}/check_{}.v", workdir(opts), side);
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
  auto log = next_log_path(opts, "check.lgcheck");
  cmd += std::format(" >> {} 2>&1", shell_quote(fs::absolute(log).string()));

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

}  // namespace

// ---- public entry points ----------------------------------------------------

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
  for (const auto& it : Pass_plugin::get_registry()) {
    it.second();
  }
  setup_inou_yosys();
}

void run_engine_command(Options& opts, Result& res) {
  validate_emits(opts);
  validate_dumps(opts);
  check_known_set_passes(opts);  // --set AND --config table names: a typo'd pass must error, not no-op

  if (!opts.depfile.empty() && !(opts.language == "verilog")) {
    throw Lhd_error{"usage",
                    "--depfile is Verilog-frontend only",
                    "pyrope `import` resolves within the explicit filelist, so there are no hidden inputs"};
  }

  if (opts.command == "elaborate") {
    if (!opts.recipe.empty()) {
      throw Lhd_error{"usage", "--recipe applies to synth/compile; elaborate has a fixed frontend", ""};
    }
    elaborate_command(opts, res);
  } else if (opts.command == "synth") {
    synth_command(opts, res);
  } else if (opts.command == "compile") {
    compile_command(opts, res);
  } else if (opts.command == "check") {
    check_command(opts, res);
  } else if (opts.command == "scan") {
    scan_command(opts, res);
  } else if (opts.command == "ln.cat") {
    ln_cat_command(opts, res);
  } else if (opts.command == "ln.diff") {
    ln_diff_command(opts, res);
  } else {
    throw Lhd_error{"usage", std::format("unknown command '{}'", opts.command), ""};
  }
}

}  // namespace lhd
