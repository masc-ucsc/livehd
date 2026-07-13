//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Common kernel plumbing: diagnostics, pass execution, typed I/O, and emits.

#include "lhd_kernel_internal.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>

#include "color_common.hpp"
#include "diag.hpp"
#include "file_utils.hpp"
#include "graph_library_singleton.hpp"
#include "hhds/graph.hpp"
#include "hhds/tree.hpp"
#include "lnast.hpp"
#include "log.hpp"
#include "node_util.hpp"
#include "pass.hpp"
#include "perf_tracing.hpp"
#include "rapidjson/document.h"
#include "str_tools.hpp"
#include "woothash.hpp"

namespace lhd {

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

// An lg: input is a GraphLibrary directory whose manifest is library.txt.
// GraphLibrary::load_merge() asserts ifs.good() when that file is missing, so a
// non-directory or an empty/incomplete dir would abort the process. Validate up
// front so the linker import paths (compile prp/ln + lg:) reject it cleanly,
// mirroring the lg-only path's is_directory guard.
// hhds writes a 12-byte header (magic, version, endian) at the front of every
// graph_*/tree_* body.bin. The format identity magic is stable ("HHGB"/"HHTB").
// Validate the on-disk body.bin magic before handing a dir to the hhds loader.
// hhds::Graph/Tree::load_body() check the magic only with assert(), compiled
// out under NDEBUG — where the loader then reads garbage counts and corrupts
// the heap (SIGABRT). A truncated/garbage body.bin is reachable via the
// documented lg:/ln: inputs (e.g. an interrupted write), so reject a bad magic
// with a clean diagnostic. Only the stable identity magic is checked here (not
// the version), so a normal hhds version bump still flows through to hhds.
void check_ir_body_magic(std::string_view dir, std::string_view subdir_prefix, uint32_t magic, std::string_view kind) {
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    if (!entry.is_directory()) {
      continue;
    }
    const auto name = entry.path().filename().string();
    if (name.rfind(subdir_prefix, 0) != 0) {  // e.g. "graph_" / "tree_"
      continue;
    }
    const auto body = entry.path() / "body.bin";
    if (!fs::is_regular_file(body)) {
      continue;
    }
    std::ifstream ifs(body, std::ios::binary);
    uint32_t      m = 0;
    ifs.read(reinterpret_cast<char*>(&m), sizeof(m));
    if (!ifs.good() || m != magic) {
      throw Lhd_error{"config",
                      std::format("{} input {} is corrupt ({}/body.bin has a bad or truncated header)", kind, dir, name),
                      "the directory was not produced by a matching lhd version, or a write was truncated"};
    }
  }
}

void check_lg_input_dir(std::string_view d) {
  if (!fs::is_directory(d)) {
    throw Lhd_error{"missing_file", std::format("lg: input not found: {}", d), "an lg: input is a GraphLibrary directory"};
  }
  if (!fs::is_regular_file(fs::path(d) / "library.txt")) {
    throw Lhd_error{"config",
                    std::format("lg: input {} is not a GraphLibrary (no library.txt)", d),
                    "produce one with `lhd compile ... --emit-dir lg:DIR/`"};
  }
  check_ir_body_magic(d, "graph_", kHhdsGraphBodyMagic, "lg:");
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
    if (!node.has_inp_edges() && !node.has_out_edges()) {  // fast: don't materialize the edge vectors
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
Stdout_to_log::Stdout_to_log(const std::string& log_path) {
  std::fflush(stdout);
  std::cout.flush();
  saved_fd_ = ::dup(1);
  int fd    = ::open(log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (fd >= 0) {
    ::dup2(fd, 1);
    ::close(fd);
  }
}

Stdout_to_log::~Stdout_to_log() {
  if (saved_fd_ >= 0) {
    std::fflush(stdout);
    std::cout.flush();
    ::dup2(saved_fd_, 1);
    ::close(saved_fd_);
  }
}

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
  // One perfetto slice per pipeline step (`--define profiling=1` builds; a
  // plain build compiles this away), so the trace timeline reads as the recipe.
  TRACE_EVENT("pass", perfetto::DynamicString{std::string(method)});
  auto log = next_log_path(opts, method);
  {
    Stdout_to_log redirect(log);
    Pass::eprp.run_method_now(method, var, labels);
  }
  res.recipe_steps.emplace_back(step_desc(method, labels));
  if (opts.verbose) {
    mirror_log_to_stderr(log);
  }
  // Halting errors abort the pipeline here; deferred errors (e.g. a refuted
  // formal property) are recorded and fail the build at the end, but let the
  // remaining passes / emits run so the design still compiles with its failing
  // check kept as a runtime check.
  if (livehd::diag::sink().has_halting_errors()) {
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
// only run inside `lhd compile` (upass/cprop/bitwidth/cgen/prp_writer — no
// bare command word of their own) live under the `compile.*` namespace so the
// option's owning command is always its leading segment. canonical_set_key()
// lets a user drop any leading segment the command words to the left already
// supply, so after `lhd compile` `--set compile.cprop.hier`, `--set cprop.hier`
// both resolve to `compile.cprop`, and after `lhd pass abc` `--set pass.abc.adder`,
// `--set abc.adder`, `--set adder` all resolve to `pass.abc`.
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
    if (pass == "lhd") {
      // The `lhd.*` kernel namespace: shared, cross-pass settings folded into
      // Options by apply_lhd_settings (not consumed by any single pass). Keep
      // this list in sync with apply_lhd_settings / list_set_options.
      if (flag != "seed" && flag != "top") {
        throw Lhd_error{"usage",
                        std::format("--set/--config references unknown kernel flag 'lhd.{}'", flag),
                        "the lhd.* namespace takes: seed, top (`lhd list options lhd`)"};
      }
      continue;
    }
    if (pass == "compile" && flag == "lnast_fmt") {
      // Kernel gate (not a pass option): whether the pass.lnastfmt LNAST
      // self-check runs. Default is build-mode (on in dbg, off in opt); this
      // overrides it. Folded into the run decision by lnastfmt_enabled(), and
      // merge_sets never copies it into a pass (its `pass` matches none).
      if (value != "true" && value != "false" && value != "1" && value != "0" && value != "on" && value != "off") {
        throw Lhd_error{"usage", std::format("--set/--config compile.lnast_fmt expects true|false, got '{}'", value), ""};
      }
      continue;
    }
    if (pass == "sim") {
      // `sim.*` is the sim-command namespace (consumed by sim_command, not a pass).
      // Single source of truth = kSimSetOptions (also feeds list_set_options and
      // the `lhd sim --help` options block, so the three never drift).
      const Sim_set_option* opt = nullptr;
      for (const auto& s : kSimSetOptions) {
        if (s.name == flag) {
          opt = &s;
          break;
        }
      }
      if (opt == nullptr) {
        std::string known;
        for (const auto& s : kSimSetOptions) {
          known += known.empty() ? "" : ", ";
          known += s.name;
        }
        throw Lhd_error{"usage",
                        std::format("--set/--config references unknown sim flag 'sim.{}'", flag),
                        std::format("the sim.* namespace takes: {}", known)};
      }
      if (opt->kind == Sim_set_option::Kind::boolean && value != "true" && value != "false" && value != "1" && value != "0"
          && value != "on" && value != "off") {
        throw Lhd_error{"usage", std::format("--set/--config sim.{} expects true|false, got '{}'", flag, value), ""};
      }
      // The numeric checkpoint knobs must be non-negative numbers, else a typo would
      // silently reach the driver as 0 (checkpoint every cycle / divide-by-zero cadence).
      if (opt->kind == Sim_set_option::Kind::non_neg_num) {
        errno         = 0;
        char*  endp   = nullptr;
        double parsed = std::strtod(value.c_str(), &endp);
        if (value.empty() || endp == value.c_str() || *endp != '\0' || parsed < 0.0 || errno == ERANGE) {
          throw Lhd_error{"usage", std::format("--set/--config sim.{} expects a non-negative number, got '{}'", flag, value), ""};
        }
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

// pass.lnastfmt is a compiler self-check: it walks the LNAST verifying the
// producer built a well-formed tree and ONLY ever emits internal
// `lnast-malformed` diagnostics (never anything a user can act on). On a correct
// build it is pure overhead, so default it ON in debug builds (catch producer
// bugs early) and OFF in release. `--set compile.lnast_fmt=true|false` (or the
// bare `lnast_fmt`) overrides either default; validated by check_known_set_passes.
bool lnastfmt_enabled(const Options& opts) {
#ifdef NDEBUG
  bool enabled = false;
#else
  bool enabled = true;
#endif
  for (const auto& [key, value] : opts.sets) {
    std::string_view k{key};
    auto             pos  = k.rfind('.');
    auto             flag = pos == std::string_view::npos ? k : k.substr(pos + 1);
    auto             pass = pos == std::string_view::npos ? std::string_view{} : k.substr(0, pos);
    if (flag == "lnast_fmt" && (pass.empty() || pass == "compile")) {
      enabled = (value == "true" || value == "1" || value == "on");
    }
  }
  return enabled;
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

// Fold the `lhd.*` kernel namespace (shared, cross-pass settings) into Options.
// `lhd.seed` is the one RNG seed shared by every pass that wants determinism;
// `lhd.top` is an alias for the `--top` flag (the dedicated flag wins when both
// are given). Validated by check_known_set_passes; the keys stay in opts.sets
// (so they still hash into run_id) and merge_sets never copies them into a
// pass (their `pass` is "lhd", which matches no pass name).
void apply_lhd_settings(Options& opts) {
  for (const auto& [key, value] : opts.sets) {
    if (key == "lhd.seed") {
      opts.seed          = value;
      opts.seed_explicit = true;
    } else if (key == "lhd.top" && opts.top.empty()) {
      opts.top = value;
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
        {"compile.cprop", "pass.cprop"}
    };
  }
  if (r == "O2") {
    // pass.formal is NOT a recipe pass: it runs as a dedicated none|fast|normal
    // mode step in graph_pipeline_and_emits (default fast, none under O0), so it
    // is independent of the O-level optimization recipe below.
    return {
        {   "compile.cprop",    "pass.cprop"},
        {"compile.bitwidth", "pass.bitwidth"}
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
  bool                      verilog_origin{false};  // durable: a Verilog-read tree (open ports legal, etc.)
  std::vector<Manifest_pub> pubs;                    // file units only (the pub index)
  // For `ln:` units: the live Lnast, so write_manifest can persist the
  // post-upass io_meta/bw_meta side-channels (otherwise empty on adopt, see
  // lnast.hpp). A loaded import restores them and skips re-elaboration.
  const Lnast*              ln{nullptr};
};

// Persist the SSA io_meta side-channel as JSON. Each entry uses short keys to
// keep big manifests compact; defaults are omitted on read.
void write_io_entry(std::ofstream& ofs, const Lnast_io_entry& e) {
  ofs << "{\"n\":\"" << json_escape_min(e.name) << "\",\"b\":" << e.bits << ",\"s\":" << (e.is_signed ? 1 : 0)
      << ",\"r\":" << (e.is_ref ? 1 : 0) << ",\"v\":" << (e.is_varargs ? 1 : 0) << ",\"k\":" << static_cast<int>(e.kind)
      << ",\"smin\":" << e.stages_min << ",\"smax\":" << e.stages_max << ",\"t\":\"" << json_escape_min(e.type_name)
      << "\",\"hr\":" << (e.has_range ? 1 : 0) << ",\"rmin\":" << e.range_min << ",\"rmax\":" << e.range_max << "}";
}

void write_unit_meta(std::ofstream& ofs, const Lnast& ln) {
  const auto& io = ln.io_meta();
  if (!io.empty()) {
    ofs << ",\"io_meta\":{\"in\":[";
    for (size_t i = 0; i < io.inputs.size(); ++i) {
      if (i) {
        ofs << ',';
      }
      write_io_entry(ofs, io.inputs[i]);
    }
    ofs << "],\"out\":[";
    for (size_t i = 0; i < io.outputs.size(); ++i) {
      if (i) {
        ofs << ',';
      }
      write_io_entry(ofs, io.outputs[i]);
    }
    ofs << "]}";
  }
  const auto& bw = ln.bw_meta();
  if (!bw.empty()) {
    ofs << ",\"bw_meta\":[";
    bool first = true;
    for (const auto& [name, e] : bw.ranges) {
      if (!first) {
        ofs << ',';
      }
      first = false;
      ofs << "{\"n\":\"" << json_escape_min(name) << "\",\"mn\":" << e.min << ",\"mx\":" << e.max
          << ",\"u\":" << (e.unbounded ? 1 : 0) << "}";
    }
    ofs << "]";
  }
}

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
    if (u.verilog_origin) {
      ofs << ",\"verilog_origin\":true";
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
    if (u.ln != nullptr) {
      write_unit_meta(ofs, *u.ln);  // persist post-upass io_meta/bw_meta (ln: units)
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
    Manifest_unit mu;  // unit_kind/verilog_origin/pubs keep their defaults
    mu.name = name;
    mu.hash = hash;
    rich.push_back(std::move(mu));
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
    u.name           = name;
    u.hash           = hash_bytes(oss.str());
    u.verilog_origin = ln->is_verilog_origin();
    u.ln             = ln.get();  // persist io_meta/bw_meta for fast import reuse
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

// Restore the post-upass io_meta/bw_meta side-channels a unit entry carries
// (written by write_unit_meta). adopt() loads them empty; restoring them lets a
// pre-elaborated import skip re-elaboration (it already holds its final body).
void restore_unit_meta(const rapidjson::Value& u, Lnast& ln) {
  auto read_entries = [](const rapidjson::Value& arr, std::vector<Lnast_io_entry>& out) {
    for (const auto& e : arr.GetArray()) {
      if (!e.IsObject() || !e.HasMember("n")) {
        continue;
      }
      Lnast_io_entry x;
      x.name       = e["n"].GetString();
      x.bits       = e.HasMember("b") ? e["b"].GetInt() : 0;
      x.is_signed  = !e.HasMember("s") || e["s"].GetInt() != 0;
      x.is_ref     = e.HasMember("r") && e["r"].GetInt() != 0;
      x.is_varargs = e.HasMember("v") && e["v"].GetInt() != 0;
      x.kind       = e.HasMember("k") ? static_cast<Io_kind>(e["k"].GetInt()) : Io_kind::none;
      x.stages_min = e.HasMember("smin") ? e["smin"].GetInt() : 0;
      x.stages_max = e.HasMember("smax") ? e["smax"].GetInt() : 0;
      x.type_name  = e.HasMember("t") ? e["t"].GetString() : "";
      x.has_range  = e.HasMember("hr") && e["hr"].GetInt() != 0;
      x.range_min  = e.HasMember("rmin") ? e["rmin"].GetInt64() : 0;
      x.range_max  = e.HasMember("rmax") ? e["rmax"].GetInt64() : 0;
      out.push_back(std::move(x));
    }
  };
  if (u.HasMember("io_meta") && u["io_meta"].IsObject()) {
    const auto& m = u["io_meta"];
    if (m.HasMember("in") && m["in"].IsArray()) {
      read_entries(m["in"], ln.io_meta().inputs);
    }
    if (m.HasMember("out") && m["out"].IsArray()) {
      read_entries(m["out"], ln.io_meta().outputs);
    }
  }
  if (u.HasMember("bw_meta") && u["bw_meta"].IsArray()) {
    for (const auto& e : u["bw_meta"].GetArray()) {
      if (!e.IsObject() || !e.HasMember("n")) {
        continue;
      }
      BitwidthEntry be;
      be.min       = e.HasMember("mn") ? e["mn"].GetInt64() : 0;
      be.max       = e.HasMember("mx") ? e["mx"].GetInt64() : 0;
      be.unbounded = !e.HasMember("u") || e["u"].GetInt() != 0;
      ln.bw_meta().ranges.emplace(e["n"].GetString(), be);
    }
  }
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

  check_ir_body_magic(dir, "tree_", kHhdsTreeBodyMagic, "ln:");
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
    // A Verilog-read tree keeps Verilog semantics across the ln: round-trip
    // (e.g. open output ports are legal — see the tolg undriven/unresolved
    // origin gate), so a reloaded forest stays lenient.
    if (u.HasMember("verilog_origin") && u["verilog_origin"].IsBool() && u["verilog_origin"].GetBool()) {
      ln->set_verilog_origin(true);
    }
    if (u.HasMember("pub") && u["pub"].IsArray()) {
      for (const auto& p : u["pub"].GetArray()) {
        if (p.IsObject() && p.HasMember("name") && p["name"].IsString() && p.HasMember("kind") && p["kind"].IsString()) {
          ln->add_pub(p["name"].GetString(), p["kind"].GetString());
        }
      }
    }
    restore_unit_meta(u, *ln);  // io_meta/bw_meta (empty on adopt) for import reuse
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
// the sorted module names. `default_srcmap` seeds cgen.srcmap before the user's
// --set is merged, so the directory emit path (where each `.map` lands adjacent
// to its `.v`) ships the source map by default while `--set cgen.srcmap=0` can
// still turn it off.
std::vector<std::string> cgen_into(Options& opts, Result& res, Eprp_var& var, const std::string& odir,
                                   bool default_srcmap) {
  ensure_dir(odir);
  Eprp_var::Eprp_dict labels{
      {"odir", odir}
  };
  if (default_srcmap) {
    labels["srcmap"] = "1";
  }
  merge_sets(opts, "compile.cgen", labels);  // user --set compile.cgen.srcmap=... overrides the seeded default
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
    auto                                          names = cgen_into(opts, res, var, e.path, /*default_srcmap=*/true);
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

// Run inou.cgen.sim (TODO 3d): a <module>.hpp interface + <module>.cpp body per
// graph into `odir`. Mirrors cgen_into — seed odir, run the step, then assert
// each pair exists.
// sim.cgen_color (default true): run the per-output-cone coloring before
// inou.cgen.sim. Honors `sim.cgen_color` (lhd sim) and `compile.sim.cgen_color`
// (the compile --emit-dir sim: path); absent => on.
static bool sim_cgen_color_enabled(const Options& opts) {
  for (const auto& [k, v] : opts.sets) {
    if (k == "sim.cgen_color" || k == "compile.sim.cgen_color") {
      return v != "false" && v != "0" && !v.empty();
    }
  }
  return true;
}

std::vector<std::string> sim_into(Options& opts, Result& res, Eprp_var& var, const std::string& odir) {
  ensure_dir(odir);

  // Color each module by its per-output cones (pass.color cgen) BEFORE emitting,
  // so inou.cgen.sim can schedule a Sub at output-cone granularity and break a
  // false combinational loop through an instance. The coloring is metadata on the
  // live graphs only: inou.cgen.verilog ignores it and an un-split sim is
  // identical, and NO_COLOR is treated as just another partition, so generation
  // is always safe whether or not a node was colored.
  if (sim_cgen_color_enabled(opts)) {
    Eprp_var::Eprp_dict clabels{
        {"alg", "cgen"}
    };
    if (!opts.top.empty() && opts.top != "-auto-top") {
      clabels["top"] = opts.top;
    }
    run_step("pass.color", var, clabels, opts, res);
  }

  Eprp_var::Eprp_dict labels{
      {"odir", odir}
  };
  merge_sets(opts, "compile.cgen", labels);
  merge_sets(opts, "compile.sim", labels);  // compile.sim.vcd=FILE -> labels["vcd"]
  if (!opts.top.empty() && opts.top != "-auto-top") {
    labels["top"] = opts.top;  // only the top module emits the VCD
  }
  run_step("inou.cgen.sim", var, labels, opts, res);

  std::vector<std::string> names;
  names.reserve(var.graphs.size());
  for (const auto& g : var.graphs) {
    std::string gn     = std::string{g->get_name()};
    auto        entity = gn.substr(gn.rfind('.') + 1);
    // Skip compiler-minted (`%`-named) graphs: a `test` block lowers to one, and
    // inou.cgen.sim does not emit it (it is a testbench, not hardware), so it
    // must not appear in the build's source list or the manifest either.
    if (!entity.empty() && entity.front() == '%') {
      continue;
    }
    // Mirror inou.cgen.sim's file-name sanitization: a '/' from a path-qualified
    // import (`../pp.Foo`) is a directory separator that cgen collapses to '_'
    // for the emitted .hpp/.cpp — the existence check and the BUILD srcs list
    // must reference the SAME sanitized basename.
    for (char& c : gn) {
      if (c == '/' || c == '\\') {
        c = '_';
      }
    }
    names.emplace_back(std::move(gn));
  }
  std::sort(names.begin(), names.end());
  for (const auto& n : names) {
    for (const char* ext : {"hpp", "cpp"}) {
      auto f = std::format("{}/{}.{}", odir, n, ext);
      if (::access(f.c_str(), R_OK) != 0) {
        throw Lhd_error{"internal", std::format("inou.cgen.sim did not produce {}", f), "check the step log in --workdir"};
      }
    }
  }
  return names;
}

// Locate a header staged in the bazel RUNFILES of the running binary (e.g. the
// `cc_direct_headers` sim-runtime deps): walk up from the executable dir to a
// `*.runfiles` ancestor and bounded-search it for `header`, returning its
// directory (cached per header). Empty when there is no runfiles layout (a
// plain dev checkout) — the caller then falls back to the sibling ../hlop. This
// makes `lhd sim`'s host-compile self-sufficient in any runfiles context (a
// child sim spawned by `lhd lec`, a test that data-deps //lhd, ...), not only
// from a repo-root cwd where ../hlop resolves.
std::string find_header_in_runfiles(std::string_view header) {
  static absl::flat_hash_map<std::string, std::string> cache;
  std::string                                          key{header};
  if (auto it = cache.find(key); it != cache.end()) {
    return it->second;
  }
  // Candidate runfiles roots. `get_exe_path()` is symlink-RESOLVED (proc_pidpath
  // / readlink) so it usually points OUTSIDE the runfiles tree — the reliable
  // source is bazel's RUNFILES_DIR / TEST_SRCDIR env (set for a `bazel test`/`run`
  // and inherited by a child `lhd sim` spawned by `lhd lec`); the exe-ancestor
  // walk is a backstop for a genuinely in-runfiles executable.
  // The CWD anchor comes FIRST: a bazel test runs with cwd inside the LIVE
  // sandbox's runfiles (…/<target>.runfiles/_main), while RUNFILES_DIR /
  // TEST_SRCDIR can point at a torn-down/reused sandbox instance (observed: a
  // child `lhd sim` spawned by `lhd formal verify` got -I dirs into a dead
  // darwin-sandbox tree and the driver compile failed with "no such file").
  std::vector<fs::path> roots;
  {
    std::error_code cec;
    for (fs::path p = fs::current_path(cec); !cec && !p.empty() && p != p.root_path(); p = p.parent_path()) {
      if (p.filename().string().find(".runfiles") != std::string::npos) {
        roots.push_back(p);
        break;
      }
    }
  }
  for (const char* env : {"RUNFILES_DIR", "TEST_SRCDIR"}) {
    if (const char* v = std::getenv(env); v != nullptr && *v != 0) {
      roots.emplace_back(v);
    }
  }
  for (fs::path p = file_utils::get_exe_path(); !p.empty() && p != p.root_path(); p = p.parent_path()) {
    if (p.filename().string().find(".runfiles") != std::string::npos) {
      roots.push_back(p);
      break;
    }
  }
  // Runfiles stage each external repo as a direct child; the sim headers live at
  // <repo>/hlop/<h> (hlop), <repo>/src/<h> (iassert), or flat. Probe those bounded
  // candidates per child — access() follows any repo symlink, so no recursive walk
  // (the runfiles tree has symlink cycles a naive follow-symlinks recursion hangs on).
  std::string     result;
  std::error_code ec;
  for (const auto& root : roots) {
    for (fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end && result.empty();
         it.increment(ec)) {
      if (ec) {
        ec.clear();
        continue;
      }
      for (const char* sub : {"", "hlop", "src"}) {
        fs::path cand = (*sub != 0) ? it->path() / sub / header : it->path() / header;
        if (::access(cand.c_str(), R_OK) == 0) {
          result = cand.parent_path().string();
          break;
        }
      }
    }
    if (!result.empty()) {
      break;
    }
  }
  cache.emplace(std::move(key), result);
  return result;
}

// hlop checkout for the generated MODULE.bazel: --set compile.cgen.sim_hlop=PATH
// wins; else the sibling ../hlop of the CWD (the dev layout).
std::string sim_hlop_path(const Options& opts) {
  for (const auto& [k, v] : opts.sets) {
    if (k == "compile.cgen.sim_hlop" && !v.empty()) {
      return v;
    }
  }
  std::error_code ec;
  auto            p = std::filesystem::weakly_canonical(std::filesystem::current_path(ec) / ".." / "hlop", ec);
  return p.empty() ? std::string{"../hlop"} : p.string();
}

// The `-I` directory that resolves `#include "slop.hpp"` (and blop.hpp /
// vcd_writer.hpp): the `hlop/` subdir of the hlop module root. Falls back to the
// module root itself in case `--set compile.cgen.sim_hlop=` already points at the
// header dir. Empty result -> slop.hpp not located (the caller reports it).
std::string sim_hlop_include_dir(const Options& opts) {
  const auto root = sim_hlop_path(opts);
  for (const auto& cand : {root + "/hlop", root}) {
    if (::access((cand + "/slop.hpp").c_str(), R_OK) == 0) {
      return cand;
    }
  }
  return find_header_in_runfiles("slop.hpp");  // bazel runfiles fallback
}

// The `-I` directory that resolves `#include "iassert.hpp"` (slop.hpp pulls it
// in). `--set compile.cgen.sim_iassert=DIR` wins; else the sibling `../iassert`
// of the hlop root / CWD, where the header lives under `src/`. Empty -> not found.
std::string sim_iassert_include_dir(const Options& opts) {
  std::vector<std::string> cands;
  for (const auto& [k, v] : opts.sets) {
    if (k == "compile.cgen.sim_iassert" && !v.empty()) {
      cands.push_back(v);
    }
  }
  std::error_code ec;
  const auto      hlop_root = std::filesystem::path(sim_hlop_path(opts));
  for (const auto& base : {hlop_root.parent_path(), std::filesystem::current_path(ec) / ".."}) {
    cands.push_back((base / "iassert" / "src").string());
    cands.push_back((base / "iassert").string());
  }
  for (const auto& cand : cands) {
    if (::access((cand + "/iassert.hpp").c_str(), R_OK) == 0) {
      return cand;
    }
  }
  return find_header_in_runfiles("iassert.hpp");  // bazel runfiles fallback
}

// Host C++ compiler for the fast (header-only + optional VCD) sim build. The Slop
// runtime needs C++23; the repo already requires a C++23 toolchain to build lhd,
// so the host compiler has it. $CXX wins (CI override), then the usual names.
std::string sim_host_cxx() {
  std::vector<std::string> cands;
  if (const char* cxx = std::getenv("CXX"); cxx != nullptr && cxx[0] != '\0') {
    cands.emplace_back(cxx);
  }
  cands.insert(cands.end(), {"clang++", "c++", "g++"});
  const char* path = std::getenv("PATH");
  for (const auto& c : cands) {
    if (c.find('/') != std::string::npos) {  // an explicit path ($CXX)
      if (::access(c.c_str(), X_OK) == 0) {
        return c;
      }
      continue;
    }
    for (std::string_view dirs = (path != nullptr ? path : ""); !dirs.empty();) {
      auto             sep = dirs.find(':');
      std::string_view d   = dirs.substr(0, sep);
      if (!d.empty() && ::access((std::string(d) + "/" + c).c_str(), X_OK) == 0) {
        return c;  // on PATH
      }
      dirs = (sep == std::string_view::npos) ? std::string_view{} : dirs.substr(sep + 1);
    }
  }
  return "c++";  // last resort (POSIX): let exec resolve it
}

// `--emit-dir sim:DIR/` — inou.cgen.sim (TODO 3d). Writes a standalone Bazel
// module of per-module Slop<N> structs over ../hlop: the pass writes a
// <name>.hpp interface + a <name>.cpp body per module, here we add the build
// scaffold (MODULE.bazel / BUILD) + manifest so `cd DIR && bazel build //:sim`
// compiles the library.
void emit_sim_outputs(Options& opts, Result& res, Eprp_var& var) {
  const auto* sim_out = find_slot(opts.emit_dirs, "sim");
  if (sim_out == nullptr) {
    return;
  }
  if (var.graphs.empty()) {
    throw Lhd_error{"config",
                    "no synthesizable modules to emit as sim:",
                    "the design produced no LGraphs (a pure-comptime program has no module IO)"};
  }
  const auto& dir   = sim_out->path;
  auto        names = sim_into(opts, res, var, dir);  // writes <name>.hpp + <name>.cpp + checks
  const auto  hlop  = sim_hlop_path(opts);

  // MODULE.bazel — standalone root. bzlmod honors only ROOT overrides, so we
  // re-declare hlop (local_path_override to the dev checkout) AND iassert
  // (archive_override, from hlop/MODULE.bazel). abseil/rules_cc come from BCR.
  {
    std::ofstream ofs(std::format("{}/MODULE.bazel", dir));
    ofs << "module(name = \"sim\", version = \"0.0.0\")\n\n";
    ofs << "bazel_dep(name = \"rules_cc\", version = \"0.2.19\")\n";
    ofs << "bazel_dep(name = \"hlop\", version = \"0.2.0\")\n";
    ofs << std::format("local_path_override(module_name = \"hlop\", path = \"{}\")\n\n", hlop);
    ofs << "bazel_dep(name = \"iassert\", version = \"0.1.1\")\n";
    ofs << "archive_override(\n"
           "    module_name = \"iassert\",\n"
           "    integrity = \"sha256-sMOp2bzYfLhVMYfFoliGIXKHAi3bZpTVTSD+2oXjMp8=\",\n"
           "    strip_prefix = \"iassert-0460fbd8405a96b3615be425df9066eba835fcf0\",\n"
           "    urls = [\"https://github.com/masc-ucsc/iassert/archive/0460fbd8405a96b3615be425df9066eba835fcf0.zip\"],\n"
           ")\n";
  }

  // BUILD — one cc_library compiling every module's <name>.cpp against
  // @hlop//hlop (C++23). Each .cpp is its own compile action, so editing one
  // module's body recompiles only that .o (the per-module bodies no longer live
  // in headers). srcs is an explicit generated list (not glob) so a stray .cpp
  // dropped in the dir — e.g. a hand-written test driver — is not swept into the
  // library. ThinLTO is gated to `-c opt`: it restores the cross-module inlining
  // the old header-only form got for release builds, while dev/incremental
  // builds (fastbuild/dbg) skip LTO and stay fast.
  {
    std::ofstream ofs(std::format("{}/BUILD", dir));
    ofs << "load(\"@rules_cc//cc:defs.bzl\", \"cc_library\")\n\n";
    ofs << "config_setting(\n    name = \"opt\",\n    values = {\"compilation_mode\": \"opt\"},\n)\n\n";
    ofs << "cc_library(\n    name = \"sim\",\n    srcs = [\n";
    for (const auto& n : names) {
      ofs << std::format("        \"{}.cpp\",\n", n);
    }
    ofs << "    ],\n"
           "    hdrs = glob([\"*.hpp\"]),\n"
           "    copts = [\"-std=c++23\"],\n"
           "    features = select({\n"
           "        \":opt\": [\"thin_lto\"],\n"
           "        \"//conditions:default\": [],\n"
           "    }),\n"
           "    deps = [\"@hlop//hlop\"],\n"
           "    visibility = [\"//visibility:public\"],\n"
           ")\n";
  }

  std::vector<std::pair<std::string, uint64_t>> manifest;
  for (const auto& n : names) {
    std::ostringstream oss;
    for (const char* ext : {"hpp", "cpp"}) {
      std::ifstream ifs(std::format("{}/{}.{}", dir, n, ext));
      oss << ifs.rdbuf();
    }
    manifest.emplace_back(n, hash_bytes(oss.str()));
  }
  write_manifest(dir, "sim", manifest);
  res.outputs.push_back(dir);
}

void emit_isabelle_outputs(Options& opts, Result& res, Eprp_var& var) {
  for (const auto& e : opts.emit_dirs) {
    if (e.kind != "isabelle") {
      continue;
    }
    if (var.graphs.empty()) {
      throw Lhd_error{"config", "no LGraphs to emit as Isabelle", "the input produced no synthesizable modules"};
    }
    ensure_dir(e.path);
    Eprp_var::Eprp_dict labels{
        {"path", e.path}
    };
    if (!opts.top.empty()) {
      labels["top"] = opts.top;
    }
    merge_sets(opts, "compile.isabelle", labels);
    run_step("pass.isabelle", var, labels, opts, res);
    res.outputs.push_back(e.path);
  }
}

void emit_lean_outputs(Options& opts, Result& res, Eprp_var& var) {
  for (const auto& e : opts.emit_dirs) {
    if (e.kind != "lean") {
      continue;
    }
    if (var.graphs.empty()) {
      throw Lhd_error{"config", "no LGraphs to emit as Lean", "the input produced no synthesizable modules"};
    }
    ensure_dir(e.path);
    Eprp_var::Eprp_dict labels{
        {"path", e.path}
    };
    if (!opts.top.empty()) {
      labels["top"] = opts.top;
    }
    merge_sets(opts, "compile.lean", labels);
    run_step("pass.lean", var, labels, opts, res);
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
    Eprp_var::Eprp_dict labels;
    labels["odir"] = e.path;
    merge_sets(opts, "compile.prp_writer", labels);  // e.g. --set compile.prp_writer.debug=true
    run_step("pass.prp_writer", var, labels, opts, res);

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
  Eprp_var::Eprp_dict labels;
  labels["odir"] = scratch;
  merge_sets(opts, "compile.prp_writer", labels);  // e.g. --set compile.prp_writer.debug=true
  run_step("pass.prp_writer", var, labels, opts, res);
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
// Returns the harvested closure by itself (res.inputs also holds the declared
// inputs) so write_unused_inputs can subtract it from the declared files.
std::vector<std::string> harvest_source_files(Result& res, const std::vector<std::shared_ptr<Lnast>>& units) {
  std::vector<std::string> closure;
  for (const auto& ln : units) {
    if (!ln) {
      continue;
    }
    const auto& sl = ln->source_locator();
    for (uint32_t fid = 0; fid < sl.file_count(); ++fid) {
      closure.emplace_back(sl.file_path(fid));
    }
  }
  std::sort(closure.begin(), closure.end());
  closure.erase(std::unique(closure.begin(), closure.end()), closure.end());
  res.inputs.insert(res.inputs.end(), closure.begin(), closure.end());
  std::sort(res.inputs.begin(), res.inputs.end());
  res.inputs.erase(std::unique(res.inputs.begin(), res.inputs.end()), res.inputs.end());
  return closure;
}

void write_depfile(const Options& opts, Result& res) {
  if (opts.depfile.empty()) {
    return;
  }
  std::ofstream ofs(opts.depfile);
  if (!ofs.is_open()) {
    throw Lhd_error{"config", std::format("could not write depfile {}", opts.depfile), ""};
  }
  // Make syntax: every primary output depends on every declared input plus
  // the frontend-harvested source closure (harvest_source_files folded each
  // compiled unit's Source_locator file table — imports included — into
  // res.inputs). Paths are emitted cwd(exec-root)-relative per the contract —
  // never absolute.
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

// --unused-inputs (Bazel `unused_inputs_list`): the declared source-file
// positionals whose contents did NOT reach the compiled closure — absent from
// every final unit's Source_locator table, e.g. a .sv whose modules fall
// outside the --top hierarchy (inou.slang elaborates only the requested top).
// One cwd(exec-root)-relative path per line, in the declared spelling; empty
// when everything was read. The file is ALWAYS created when the flag is given.
// Conservative by construction: every pyrope positional is compiled as a root
// (its module reaches the emits), so it is never listed; a flow that harvests
// no LNAST units (yosys-* readers, IR-only compiles) lists nothing. A listed
// file is still *parsed* today — pruning it trades away re-runs on its future
// parse errors, the standard unused_inputs_list contract — but its contents
// provably do not affect the produced artifacts.
void write_unused_inputs(const Options& opts, Result& res, const std::vector<std::string>& closure) {
  if (opts.unused_inputs.empty()) {
    return;
  }
  std::ofstream ofs(opts.unused_inputs);  // creation is unconditional (empty = all inputs used)
  if (!ofs.is_open()) {
    throw Lhd_error{"config", std::format("could not write unused-inputs list {}", opts.unused_inputs), ""};
  }
  auto canon = [](const std::string& p) {
    std::error_code ec;
    auto            c = fs::weakly_canonical(p, ec);
    return (ec || c.empty()) ? p : c.string();
  };
  // Canonicalize both sides before subtracting: harvest paths are the
  // Source_locator's workspace-relative form (srcloc::workspace_relative —
  // which strips the leading '/' from outside-workspace absolutes), declared
  // inputs are as-typed. Register each closure entry under both readings.
  absl::flat_hash_set<std::string> read;
  for (const auto& p : closure) {
    read.insert(canon(p));
    if (!p.empty() && p.front() != '/') {
      read.insert(canon("/" + p));  // outside-workspace ingress form
    }
  }
  if (!read.empty()) {  // no harvested closure -> nothing is provably unused
    absl::flat_hash_set<std::string> emitted;
    for (const auto& f : opts.files) {
      auto key = canon(f);
      if (read.contains(key) || !emitted.insert(key).second) {
        continue;
      }
      std::error_code ec;
      auto            r = fs::proximate(f, ec);  // same relativization as write_depfile
      ofs << ((ec || r.empty()) ? f : r.string()) << '\n';
    }
  }
  res.outputs.push_back(opts.unused_inputs);
}

}  // namespace lhd
