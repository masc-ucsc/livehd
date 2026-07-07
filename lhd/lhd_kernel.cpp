//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/container/flat_hash_map.h"
#include "color_common.hpp"  // livehd::color::is_partitionable / NO_COLOR (lhd tool)
#include "diag.hpp"
#include "str_tools.hpp"
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
#include "perf_tracing.hpp"  // TRACE_EVENT — no-op unless built with --define profiling=1
#include "prp2lnast.hpp"
#include "prp_sim.hpp"  // `lhd sim`: generate C++ test drivers from `test` blocks
#include "query.hpp"  // pass/lec L1 (lec::prove_equal) for the cross-check path
#include "rapidjson/document.h"
#include "semdiff.hpp"  // pass/semdiff (structural_match) for the `lhd pass semdiff` command
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

// An lg: input is a GraphLibrary directory whose manifest is library.txt.
// GraphLibrary::load_merge() asserts ifs.good() when that file is missing, so a
// non-directory or an empty/incomplete dir would abort the process. Validate up
// front so the linker import paths (compile prp/ln + lg:) reject it cleanly,
// mirroring the lg-only path's is_directory guard.
// hhds writes a 12-byte header (magic, version, endian) at the front of every
// graph_*/tree_* body.bin. The format identity magic is stable ("HHGB"/"HHTB").
constexpr uint32_t kHhdsGraphBodyMagic = 0x48484742;  // "HHGB"
constexpr uint32_t kHhdsTreeBodyMagic  = 0x48485442;  // "HHTB"

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
constexpr std::pair<std::string_view, std::string_view> kSetPasses[] = {
    {        "compile.upass",        "pass.upass"},
    {        "compile.cprop",        "pass.cprop"},
    {     "compile.bitwidth",     "pass.bitwidth"},
    {       "compile.formal",       "pass.formal"},
    {          "pass.formal",       "pass.formal"},  // standalone `lhd pass formal --set` mirrors compile.formal.*
    {         "compile.cgen", "inou.cgen.verilog"},
    {          "compile.sim",     "inou.cgen.sim"},  // compile.sim.vcd=FILE -> VCD trace
    {        "compile.yosys",    "inou.yosys.tolg"},
    {     "compile.isabelle",      "pass.isabelle"},
    {          "compile.lean",          "pass.lean"},
    {   "compile.prp_writer",   "pass.prp_writer"},
    {           "pass.color",        "pass.color"},
    {       "pass.partition",    "pass.partition"},
    {             "pass.abc",          "pass.abc"},
    {         "pass.liberty",      "pass.liberty"},
    {                  "lec",          "pass.lec"},
    {         "pass.semdiff",      "pass.semdiff"},
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
        throw Lhd_error{"usage",
                        std::format("--set/--config compile.lnast_fmt expects true|false, got '{}'", value),
                        ""};
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
        throw Lhd_error{"usage", std::format("--set/--config references unknown sim flag 'sim.{}'", flag),
                        std::format("the sim.* namespace takes: {}", known)};
      }
      if (opt->kind == Sim_set_option::Kind::boolean && value != "true" && value != "false" && value != "1"
          && value != "0" && value != "on" && value != "off") {
        throw Lhd_error{"usage", std::format("--set/--config sim.{} expects true|false, got '{}'", flag, value), ""};
      }
      // The numeric checkpoint knobs must be non-negative numbers, else a typo would
      // silently reach the driver as 0 (checkpoint every cycle / divide-by-zero cadence).
      if (opt->kind == Sim_set_option::Kind::non_neg_num) {
        errno         = 0;
        char*  endp   = nullptr;
        double parsed = std::strtod(value.c_str(), &endp);
        if (value.empty() || endp == value.c_str() || *endp != '\0' || parsed < 0.0 || errno == ERANGE) {
          throw Lhd_error{"usage", std::format("--set/--config sim.{} expects a non-negative number, got '{}'", flag, value),
                          ""};
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
                                   bool default_srcmap = false) {
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
  std::vector<fs::path> roots;
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
    for (fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
         it != end && result.empty(); it.increment(ec)) {
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
      throw Lhd_error{"config",
                      "no LGraphs to emit as Isabelle",
                      "the input produced no synthesizable modules"};
    }
    ensure_dir(e.path);
    Eprp_var::Eprp_dict labels{{"path", e.path}};
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
      throw Lhd_error{"config",
                      "no LGraphs to emit as Lean",
                      "the input produced no synthesizable modules"};
    }
    ensure_dir(e.path);
    Eprp_var::Eprp_dict labels{{"path", e.path}};
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

// `lhd pass semdiff …` — the structural diff/match pass, now a `pass`
// subcommand rather than a top-level command. The command-level validations
// that used to key off the former `semdiff` command word (no --emit/--dump
// observables) now key off this.
bool is_pass_semdiff(const Options& opts) {
  return opts.command == "pass" && !opts.files.empty() && opts.files.front() == "semdiff";
}

void validate_emits(const Options& opts) {
  // Kinds with no implementation yet anywhere:
  reject_emit_kind(opts, "graphviz", {"unsupported", "--emit graphviz: is not implemented yet", ""});
  reject_emit_kind(opts, "metadata", {"unsupported", "--emit metadata: is not implemented yet (needs [3c] hashes)", ""});

  // ln:/lg:/lnast-dump:/isabelle:/lean: outputs are directory containers (a Forest dir, a
  // GraphLibrary dir, one file per unit) — directory form only, never a single
  // file. (pyrope: is allowed as a single file for a one-unit design; the
  // multi-unit check lives in emit_pyrope_single_file.)
  for (const auto& e : opts.emits) {
    if (e.kind == "ln" || e.kind == "lg" || e.kind == "lnast-dump" || e.kind == "isabelle" || e.kind == "lean") {
      throw Lhd_error{"usage",
                      std::format("--emit {0}:PATH is a directory container; use --emit-dir {0}:DIR/", e.kind),
                      "ln: is a Forest save dir, lg: a GraphLibrary save dir, lnast-dump:/isabelle:/lean: one file per unit"};
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
  if (opts.command == "lec" || is_pass_semdiff(opts)) {
    const std::string_view what = opts.command == "lec" ? "lec" : "pass semdiff";
    for (const char* k : {"lg", "verilog", "ln", "pyrope", "lnast-dump", "isabelle", "lean"}) {
      reject_emit_kind(opts, k, {"usage", std::format("{} has no outputs beyond the result", what), ""});
    }
  }
  if (opts.command == "tool") {
    for (const char* k : {"lg", "verilog", "ln", "pyrope", "lnast-dump", "isabelle", "lean"}) {
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
  if (opts.command == "lec" || is_pass_semdiff(opts) || opts.command == "scan" || opts.command == "tool") {
    const std::string_view what = is_pass_semdiff(opts) ? "pass semdiff" : std::string_view{opts.command};
    throw Lhd_error{"usage", std::format("{} has no --dump observables", what), "--dump applies to compile"};
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

// 2i-import S1 — on-disk import discovery. For each source unit's unresolved
// import (a bare logical name, not `lg:`/`ln:`), search the *importing file's
// own directory* for `<name>.prp`, parse it (named by its logical import path),
// and repeat to a fixpoint. So `lhd compile foo/bar.prp` pulls in its siblings
// without the caller listing every dependency, and the LSP (which opens a
// single file) resolves the same way. Resolution is importer-directory relative
// only — never the cwd, never an ancestor crawl. A logical name that resolves
// to two distinct files is an error, never a silent first-hit.
std::vector<std::string> collect_imports(const std::shared_ptr<Lnast>& ln);  // defined below

void discover_imports(Options& opts, Eprp_var& var, size_t n_imports) {
  TRACE_EVENT("pyrope", "discover_imports");
  auto dir_of = [](std::string_view p) -> std::string {
    auto s = p.rfind('/');
    return s == std::string_view::npos ? std::string(".") : std::string(p.substr(0, s));
  };
  // The unit name inou.prp gives a file: basename, cut at the first '.' (mirror
  // inou_prp.cpp so our loaded-set keys line up with get_top_module_name()).
  auto unit_name_of = [](std::string_view p) -> std::string {
    auto s    = p.rfind('/');
    auto base = s == std::string_view::npos ? p : p.substr(s + 1);
    auto d    = base.find('.');
    return std::string(d == std::string_view::npos ? base : base.substr(0, d));
  };
  auto abspath_of = [](std::string_view p) -> std::string {
    std::error_code ec;
    auto            a = fs::absolute(fs::path(p), ec);
    return ec ? std::string(p) : a.lexically_normal().string();
  };
  // Resolve `<stem>.prp` in `dir` case-SENSITIVELY (names are case-sensitive).
  // Scans the directory and returns the on-disk path only when a filename
  // matches `<stem>.prp` exactly — FS-independent (a case-insensitive host FS
  // does not let `import("stem")` resolve a file named `Stem.prp`). Empty when
  // absent. Listings are cached per directory: a fresh directory_iterator per
  // import is O(import-edges × dirents) stat calls (an xs_core_prp-scale sweep
  // is 1631 sibling files, each importing several others).
  absl::flat_hash_map<std::string, absl::flat_hash_map<std::string, std::string>> dir_listing;  // dir -> (fname -> path)
  auto find_prp = [&dir_listing](const std::string& dir, const std::string& stem) -> std::string {
    // A stem may be path-qualified (`subdir/mod`): the directory portion rides
    // verbatim onto `dir` and only the final component is matched against the
    // on-disk filenames.
    std::string scan_dir = dir;
    std::string leaf     = stem;
    if (const auto s = stem.rfind('/'); s != std::string::npos) {
      scan_dir = dir + "/" + stem.substr(0, s);
      leaf     = stem.substr(s + 1);
    }
    auto [lit, first_visit] = dir_listing.try_emplace(scan_dir);
    if (first_visit) {
      std::error_code it_ec;
      for (fs::directory_iterator it(scan_dir, it_ec), end; !it_ec && it != end; it.increment(it_ec)) {
        if (!it->is_regular_file()) {
          continue;
        }
        auto fn = it->path().filename().string();
        if (str_tools::ends_with(fn, ".prp")) {
          lit->second.emplace(std::move(fn), it->path().string());
        }
      }
    }
    const auto fit = lit->second.find(leaf + ".prp");
    return fit == lit->second.end() ? std::string{} : fit->second;
  };

  absl::flat_hash_map<std::string, std::string>          unit_dir;      // unit -> source dir (case-sensitive)
  absl::flat_hash_set<std::string> parsed_paths;  // abs paths already parsed
  for (const auto& f : opts.files) {
    unit_dir[unit_name_of(f)] = dir_of(f);
    parsed_paths.insert(abspath_of(f));
  }

  // Each unit's imports are examined exactly ONCE, in the round after the unit
  // was parsed (`next_scan` worklist). Re-scanning every loaded unit per round
  // is O(units × rounds) full-LNAST walks — and resolution is deterministic on
  // a static tree, so a re-scan can never produce a different outcome: an
  // import that resolved was parsed on the spot, one that didn't never will
  // (same dir, same stem), and one satisfied by a later round's load needs no
  // action anyway.
  absl::flat_hash_set<std::string> loaded;
  for (const auto& ln : var.lnasts) {
    loaded.insert(std::string(ln->get_top_module_name()));
  }
  size_t next_scan = n_imports;
  while (true) {
    std::map<std::string, std::string>           found;       // logical name -> file to parse
    std::map<std::string, std::set<std::string>> seen_paths;  // logical name -> resolved files

    const size_t scan_end = var.lnasts.size();
    for (size_t i = next_scan; i < scan_end; ++i) {
      const auto& ln  = var.lnasts[i];
      auto        dit = unit_dir.find(std::string(ln->get_top_module_name()));
      if (dit == unit_dir.end()) {
        continue;  // a unit with no known on-disk origin (e.g. a derived tree)
      }
      const std::string dir = dit->second;
      for (const auto& raw : collect_imports(ln)) {
        if (raw.starts_with("lg:") || raw.starts_with("ln:")) {
          continue;  // artifact imports resolve elsewhere, not on-disk source
        }
        // A trailing `.entry` after the last '/' selects a pub member, so the
        // file is the stem; otherwise the whole string is the file path.
        std::vector<std::string> names;
        auto                     slash = raw.rfind('/');
        auto                     dot   = raw.rfind('.');
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
          names.emplace_back(raw.substr(0, dot));
        }
        names.emplace_back(raw);
        bool already = false;
        for (const auto& c : names) {
          if (loaded.contains(c)) {
            already = true;
            break;
          }
        }
        if (already) {
          continue;
        }
        for (const auto& c : names) {
          std::string path = find_prp(dir, c);
          if (!path.empty()) {
            seen_paths[c].insert(abspath_of(path));
            found.try_emplace(c, path);
            break;
          }
        }
      }
    }

    bool ambiguous = false;
    for (const auto& [name, paths] : seen_paths) {
      if (paths.size() <= 1) {
        continue;
      }
      ambiguous = true;
      std::string list;
      for (const auto& p : paths) {
        if (!list.empty()) {
          list += ", ";
        }
        list += p;
      }
      livehd::diag::sink().emit(livehd::diag::Diagnostic{
          .severity = livehd::diag::Severity::error,
          .code     = "import-ambiguous",
          .category = "name",
          .pass     = "lhd.compile",
          .message  = std::format("ambiguous import \"{}\": resolves to more than one file", name),
          .hint     = std::format("candidates: {}; rename the file or import explicitly", list)});
    }
    if (ambiguous) {
      throw classify_engine_failure("ambiguous import resolution");
    }

    bool progress = false;
    for (const auto& [name, path] : found) {
      if (!parsed_paths.insert(abspath_of(path)).second) {
        continue;  // already parsed under some name — don't double-load the file
      }
      Prp2lnast converter(path, name);
      var.add(converter.get_lnast());
      loaded.insert(name);
      unit_dir[name] = dir_of(path);
      progress       = true;
    }
    next_scan = scan_end;
    if (!progress) {
      break;
    }
  }
  // A discovered file that fails to parse propagates its own diagnostic out of
  // Prp2lnast (caught by the top-level compile handler), so no extra check here.
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
      // A loaded import whose post-upass io_meta was restored is reused as-is:
      // pass.upass skips re-elaborating it (it already holds its final body),
      // resolving callers through the restored interface. Restricted to STATEFUL
      // `mod`/`pipe` entities: those are always Sub instances — never inlined,
      // never comptime-evaluated — so the importer needs only their interface. A
      // `comb` (even when emitted as a Sub) can still be inlined or called at
      // comptime (`cassert(f(3)==4)`), which needs its body, so leave it to
      // re-elaborate. The file/__pub wrappers carry no io_meta. Older ln: dirs
      // without persisted io_meta also fall back to re-elaboration.
      const auto lk = ln->get_lambda_kind();
      if (!ln->io_meta().empty() && (lk == "mod" || lk == "pipe")) {
        ln->set_pre_elaborated(true);
      }
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
  // 2i-import S1 — transitively pull in imported sibling sources from each
  // importing file's own directory, so a single-file compile needs no
  // dependency list (and the LSP resolves the same way).
  discover_imports(opts, var, n_imports);
  if (lnastfmt_enabled(opts)) {
    run_step("pass.lnastfmt", var, {}, opts, res);
  }
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
  merge_sets(opts, "compile.yosys", labels);

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
  if (lnastfmt_enabled(opts)) {
    run_step("pass.lnastfmt", var, {}, opts, res);
  }
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
         || find_slot(opts.emit_dirs, "verilog") != nullptr || find_slot(opts.emit_dirs, "isabelle") != nullptr
         || find_slot(opts.emit_dirs, "lean") != nullptr || find_slot(opts.emit_dirs, "sim") != nullptr || wants_dump(opts, "lg");
}

// True when any requested observable consumes the post-upass LNAST (gates the
// toln materialization — the dual of emits_need_graphs for pass.upass's
// toln:0|1). --dump lnast prints that tree, so it counts; ln.cat/ln.diff
// print/compare it, so the commands count too.
bool emits_need_lnast(const Options& opts) {
  return find_slot(opts.emit_dirs, "ln") != nullptr || find_slot(opts.emit_dirs, "pyrope") != nullptr
         || find_slot(opts.emit_dirs, "lnast-dump") != nullptr || wants_dump(opts, "lnast") || opts.command == "tool";
}

// A *bare* `lhd compile FILE` (no --emit/--emit-dir at all) is the
// maximum-diagnostics action: it should lower all the way to LGraphs (tolg +
// recipe) so the LNAST->LGraph lowering and the graph passes surface every
// warning/error, even though nothing is saved (graph_pipeline_and_emits is a
// no-op without an emit slot: the lg-save guard needs a non-null lg slot and
// every emit_* helper early-returns).
//
// Gated narrowly so it never changes a flow the user explicitly asked for:
//   - When ANY emit/emit-dir is requested, the existing emits_need_graphs()
//     decides — so `--emit-dir pyrope:`/`ln:`/`lnast-dump:` keep their no-tolg
//     (pre-upass / toln:0) re-emit semantics.
//   - The explicit front-end-only tier `--set upass.order=noop` guts the runner
//     so the tree is never rewritten into a tolg-ready shape; lowering it would
//     only produce spurious failures (prplib.py's `:type: parsing`/`lnast`
//     tiers rely on this). Mirrors the user_toln_off scan in lower_lnasts.
bool force_diag_graphs(const Options& opts) {
  if (!opts.emits.empty() || !opts.emit_dirs.empty()) {
    return false;  // a requested emit defines the flow — don't override it
  }
  for (const auto& [key, value] : opts.sets) {
    if (key == "compile.upass.order" && value == "noop") {
      return false;
    }
    // Explicit opt-out: `--set upass.tolg=false` means "front-end + upass only,
    // don't lower to graphs" — the stage-scoped check the prplib comptime/upass
    // test tiers want (a pure comptime program is not synthesizable hardware).
    if (key == "compile.upass.tolg" && (value == "0" || value == "false")) {
      return false;
    }
  }
  return true;
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
    // A directory (e.g. a path with a trailing '/') passes check_inputs_exist
    // but has an empty stem; create_io asserts on an empty unit name. Reject any
    // non-regular-file / empty-stem input with a clean usage error.
    if (!fs::is_regular_file(f)) {
      throw Lhd_error{"usage", std::format("scan expects .prp source files, got '{}'", f), "scan derives a module name from each file's stem"};
    }
    auto base = fs::path(f).stem().string();
    if (base.empty()) {
      throw Lhd_error{"usage", std::format("cannot derive a module name from '{}'", f), ""};
    }

    // Dependency discovery only needs the import strings, so run the prpparse
    // LEXER (Prp2lnast::scan_imports) — NOT a full parse/elaborate. This is the
    // whole point of `lhd scan`: it stays linear/fast (ms even on multi-MB files),
    // where building the LNAST per file took minutes. A malformed file (lex error,
    // e.g. an unterminated string) is reported and skipped with empty imports so
    // one bad file in a multi-file scan does not abort the rest.
    // Lexer-only: a file that LEXES but would not PARSE still yields its imports
    // (scan deliberately does no parse/elaborate). Only a genuine LEX failure
    // (unterminated string/comment) is an error — reported per-file (category
    // syntax) so it exits non-zero with error.class=syntax, while the loop still
    // finishes and res.scan_json is written for every other file (lhd_main turns
    // the error diag into the fail status; write_result emits scan_json regardless).
    std::vector<std::string> imports;
    try {
      imports = Prp2lnast::scan_imports(f);
    } catch (const std::exception& e) {
      livehd::diag::sink().emit(livehd::diag::Diagnostic{.severity = livehd::diag::Severity::error,
                                                         .code     = "scan-lex-error",
                                                         .category = "syntax",
                                                         .pass     = "scan",
                                                         .message  = std::format("scan: {} did not lex: {}", f, e.what())});
    }

    if (!first) {
      payload += ',';
    }
    first        = false;
    payload     += std::format("{{\"file\":\"{}\",\"imports\":[", json_escape_min(f));
    bool ifirst  = true;
    for (const auto& imp : imports) {
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
  if (lnastfmt_enabled(opts)) {
    run_step("pass.lnastfmt", var, {}, opts, res);
  }

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
  // comb inlining is OFF by default (a directly-named, fully-defined comb is
  // emitted as a Sub module instance, preserving the comb boundary for
  // debug/optimization); only the O2 recipe flattens by inlining. Bare `compile`
  // and O0/O1 keep the boundary. A user `--set compile.upass.inline=…` overrides
  // (merge_sets below runs after this).
  up["inline"] = (opts.recipe == "O2") ? "true" : "false";
  // Derived toln gate (the dual of the emit-derived tolg gate): when neither
  // the lnast.tolg stage below (need_graphs) nor any post-upass LNAST emit
  // (ln:/pyrope:/lnast-dump:) consumes the rewritten tree, skip materializing
  // it — pass.upass toln:0 drops the whole staging build, the post-walk DCE,
  // and the coalescer; every pass still dispatches, so diagnostics are
  // unchanged. An explicit `--set upass.toln=…` (merged below) overrides.
  if (!need_graphs && !emits_need_lnast(opts)) {
    up["toln"] = "0";
  }
  // lg-only flows: the rewritten LNAST is consumed ONLY by lnast.tolg and then
  // dropped, so the post-walk DCE marks dead statements for tolg to skip
  // instead of rebuilding a cleaned staging tree (dce:mark — the rebuild is
  // the dominant DCE cost on generated-code-scale units). Any LNAST-keeping
  // flow (pyrope:/ln:/lnast dumps/tool) keeps the rebuild.
  if (need_graphs && !emits_need_lnast(opts)) {
    up["dce"] = "mark";
  }
  merge_sets(opts, "compile.upass", up);

  // A user `--set upass.toln=0` keeps each tree's original (post-lnastfmt,
  // pre-upass-rewrite) body in var.lnasts instead of the rewritten one, so
  // pass.prp_writer re-emits Pyrope straight from the inou.slang / inou.prp
  // LNAST. That pyrope emit is the one supported reason to force toln:0 from
  // the CLI; with no pyrope emit nothing downstream consumes the un-rewritten
  // tree, so it is a diagnostics/debug or unexpected flow — flag it.
  bool user_toln_off = false;
  for (const auto& [key, value] : opts.sets) {
    if (key == "compile.upass.toln" && (value == "0" || value == "false")) {
      user_toln_off = true;
    }
  }
  if (user_toln_off && find_slot(opts.emits, "pyrope") == nullptr && find_slot(opts.emit_dirs, "pyrope") == nullptr) {
    livehd::diag::warn("lhd.elaborate", "toln-debug-flow", "io")
        .msg("--set upass.toln=0 keeps the original pre-upass LNAST, but no pyrope emit (pass.prp_writer) consumes it")
        .hint("toln:0 is meant for `--emit-dir pyrope:DIR/` (re-emit source from the inou.slang/inou.prp LNAST); "
              "without a pyrope emit this is a debugging or unexpected flow")
        .emit();
  }

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

  // pass.formal — single-design property checks (assert / assume / Hotmux
  // one-hotness) on the cvc5 prover. A dedicated none|fast|normal mode step,
  // independent of the O-level recipe above: default `fast` (small cvc5 budget),
  // `none` under -O0/--recipe O0, override with --set compile.formal.mode=...
  // It stamps proven / runtime-check attributes, so it precedes the lg save and
  // cgen below.
  if (!var.graphs.empty()) {
    Eprp_var::Eprp_dict labels;
    merge_sets(opts, "compile.formal", labels);
    const std::string recipe = opts.recipe.empty() ? "O1" : opts.recipe;
    const std::string mode   = labels.count("mode") ? labels["mode"] : (recipe == "O0" ? "none" : "fast");
    if (mode != "none" && mode != "fast" && mode != "normal") {
      throw Lhd_error{"usage", std::format("--set compile.formal.mode must be none|fast|normal, got '{}'", mode), ""};
    }
    if (mode != "none") {
      labels["mode"] = mode;
      // The committed design boundary: a refutation at --top fails the build,
      // refutations in instantiated submodules ("not enough top") only warn.
      if (!opts.top.empty() && opts.top != "-auto-top" && !labels.count("top")) {
        labels["top"] = opts.top;
      }
      run_step("pass.formal", var, labels, opts, res);
    }
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
    TRACE_EVENT("pass", "lg.save");
    livehd::Hhds_graph_library::save(lib_path);
    res.outputs.push_back(lg_out->path);
  }

  emit_isabelle_outputs(opts, res, var);
  emit_lean_outputs(opts, res, var);
  emit_verilog_outputs(opts, res, var);
  emit_sim_outputs(opts, res, var);  // --emit-dir sim:DIR/ (inou.cgen.sim, TODO 3d)
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
    // Force the tolg lowering for diagnostics even with no lg: emit (bare
    // `lhd compile ln:DIR`) — see force_diag_graphs.
    lower_lnasts(opts, res, var, lib_path, emits_need_graphs(opts) || force_diag_graphs(opts));
    if (ln_out != nullptr) {  // re-publish the post-upass forest (the loaded units are the design)
      emit_ln_outputs(var.lnasts, opts, res);
    }
  } else {
    setup_diag(opts, "compile.lg");
    const auto& lg_in = ir.lg_dirs.front();
    if (!fs::is_directory(lg_in)) {
      throw Lhd_error{"missing_file", std::format("lg: input not found: {}", lg_in), "an lg: input is a GraphLibrary directory"};
    }
    check_ir_body_magic(lg_in, "graph_", kHhdsGraphBodyMagic, "lg:");
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
    check_lg_input_dir(d);
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
        check_lg_input_dir(d);
        res.inputs.push_back(d);
        lib.load_merge(d);
      }
    }
    // Bare `lhd compile FILE.prp` (no emit) still lowers to LGraphs for max
    // diagnostics; the graphs are built and discarded (force_diag_graphs).
    lower_lnasts(opts, res, var, lib_path, emits_need_graphs(opts) || force_diag_graphs(opts) || !ir.lg_dirs.empty());
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
      // Bare `lhd compile FILE.sv` (no emit) still lowers for max diagnostics.
      lower_lnasts(opts, res, var, lib_path, emits_need_graphs(opts) || force_diag_graphs(opts));
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

// ---- sim --------------------------------------------------------------------

std::string shell_quote(const std::string& s);  // defined below (check section)

// `lhd sim <file.prp> [test.name] [--arg k=v ...]` — lower the design's DUT
// modules to Slop<N> structs (inou.cgen.sim) and, for each `test` block,
// generate a C++ driver that runs the `tick` loop and turns `assert`s into
// runtime checks, then bazel-build + run them and report pass/fail.
void sim_command(Options& opts, Result& res) {
  res.command = "sim pyrope";
  if (opts.files.empty()) {
    throw Lhd_error{"usage", "sim requires a .prp file", "e.g. `lhd sim foo.prp` or `lhd sim foo.prp test.name`"};
  }
  // Positional args: every `.prp` is a SOURCE — `lhd sim a.prp b.prp top.prp`
  // loads all three so the top's `import("a")`/`import("b")` resolve to the
  // co-loaded units (no relative path or `-I` needed). A lone non-`.prp` token is
  // the test selector. The LAST source is the primary, test-containing file;
  // any earlier sources are the units it imports.
  std::vector<std::string> sources;
  std::string              test_sel;
  for (const auto& f : opts.files) {
    if (str_tools::ends_with(f, ".prp")) {
      sources.push_back(f);
    } else {
      test_sel = f;
    }
  }
  if (sources.empty()) {
    throw Lhd_error{"usage", "sim requires a .prp file", "e.g. `lhd sim foo.prp` or `lhd sim foo.prp test.name`"};
  }
  const std::string file       = sources.back();
  const bool        setup_only = opts.sim_setup_only;
  const bool        run_only   = opts.sim_run_only;
  const bool        list_only  = opts.sim_list_tests;
  const bool        pretty     = opts.diag_fmt == Diag_fmt::pretty;
  if (setup_only && run_only) {
    throw Lhd_error{"usage", "--setup-only and --run-only are mutually exclusive", ""};
  }

  // ---- --list-tests: a pure parse of the source's `test` blocks -> the dotted
  // names + parameters. No DUT lowering / build, so tooling can enumerate the
  // tests cheaply (and even when the design does not compile). Output honors
  // --diag-fmt like every other command: JSON (machine-readable, the SAME shape
  // the built binary's `--list-tests` prints) by default when piped, or a
  // human-readable listing in pretty mode; `--diag-fmt` overrides the auto-pick.
  if (list_only) {
    std::vector<prp_sim::Test_info> tests;
    std::string                     err;
    if (prp_sim::list_tests(file, test_sel, tests, err) != 0) {
      res.status        = "fail";
      res.error_class   = "usage";
      res.error_message = err;
      res.exit_code     = 1;
      return;
    }
    if (pretty) {
      std::print("{} test(s) in {}:\n", tests.size(), file);
      for (const auto& t : tests) {
        std::print("  {}", t.name);
        for (size_t i = 0; i < t.params.size(); ++i) {
          const auto& p = t.params[i];
          std::print("{}{}{}", i == 0 ? "(" : ", ", p.name,
                     p.required ? std::string(": required") : std::format(" = {}", p.default_text));
        }
        if (!t.params.empty()) {
          std::print(")");
        }
        std::print("\n");
      }
    } else {
      std::print("{}\n", prp_sim::tests_to_json(file, tests));
    }
    std::fflush(stdout);
    return;  // status stays pass (a pure query — no output artifact)
  }

  // The sim build dir: --workdir if given (REUSED in place — generated files are
  // overwritten, nothing is deleted), else a fresh OS-temp dir. The temp dir is
  // OUTSIDE any workspace, so a later `bazel build //...` in the user's repo
  // never sweeps the nested BUILD package or follows its convenience symlinks.
  std::string simroot;
  if (!opts.workdir.empty()) {
    simroot = opts.workdir;
  } else {
    if (run_only) {
      throw Lhd_error{"usage", "--run-only needs --workdir pointing at a prior --setup-only build", ""};
    }
    auto              tmpl = (fs::temp_directory_path() / "lhd_sim_XXXXXX").string();
    std::vector<char> buf(tmpl.c_str(), tmpl.c_str() + tmpl.size() + 1);
    if (::mkdtemp(buf.data()) == nullptr) {
      throw Lhd_error{"config", "could not create a temp dir for the sim build", "set $TMPDIR or pass --workdir"};
    }
    simroot = buf.data();
  }
  const std::string simdir = simroot + "/sim";

  // --set sim.vcd=true: dump one VCD per test, `<workdir>/<test.name>.vcd`. The
  // path is absolute (the driver binary is run from the caller's cwd), and when
  // on, the fast build also links hlop's VCD writer (vcd_writer.cpp).
  bool vcd_on = false;
  for (const auto& [k, v] : opts.sets) {
    if (k == "sim.vcd") {
      vcd_on = (v == "true" || v == "1" || v == "on");
    }
  }
  // A `--vcd-from` window or `--vcd-on-fail` implies VCD: the driver needs the
  // trace machinery emitted (the first run still produces none until a window opens).
  if (opts.sim_vcd_from >= 0 || opts.sim_vcd_on_fail) {
    vcd_on = true;
  }
  const std::string vcd_dir = vcd_on ? fs::absolute(simroot).string() : std::string{};

  // --set sim.checkpoint* : periodic editable checkpoints of DUT + testbench state
  // under <simroot>/ckpt/<test>/ckp<cycle>/ (regs.json + *.hex + tb.json +
  // meta.json). Default ON; a short run (< the min-secs floor) writes none. The
  // settings are forwarded to the driver, which owns the fork cadence + prune.
  bool        ckpt_on = true;
  std::string ckpt_min_secs, ckpt_max, ckpt_max_overhead, ckpt_every;
  for (const auto& [k, v] : opts.sets) {
    if (k == "sim.checkpoint") {
      ckpt_on = (v == "true" || v == "1" || v == "on");
    } else if (k == "sim.checkpoint_min_secs") {
      ckpt_min_secs = v;
    } else if (k == "sim.checkpoint_max") {
      ckpt_max = v;
    } else if (k == "sim.checkpoint_max_overhead") {
      ckpt_max_overhead = v;
    } else if (k == "sim.checkpoint_every") {
      ckpt_every = v;
    }
  }
  const std::string ckpt_dir = ckpt_on ? (fs::absolute(simroot).string() + "/ckpt") : std::string{};

  // Debug-flag sanity (sim_checkpoint_debug_plan): catch contradictory combinations
  // up front instead of silently producing a degenerate run.
  if (opts.sim_vcd_to >= 0 && opts.sim_vcd_from < 0) {
    throw Lhd_error{"usage", "--vcd-to needs --vcd-from (the window start)", "e.g. --vcd-from 100 --vcd-to 140"};
  }
  if (opts.sim_vcd_from >= 0 && opts.sim_vcd_to >= 0 && opts.sim_vcd_from > opts.sim_vcd_to) {
    throw Lhd_error{"usage", std::format("--vcd-from {} is after --vcd-to {}", opts.sim_vcd_from, opts.sim_vcd_to), ""};
  }
  if (ckpt_dir.empty() && opts.sim_restart_at >= 0) {
    throw Lhd_error{"usage", "--restart-at needs checkpoints — do not combine it with --set sim.checkpoint=false",
                    "run once with checkpointing on to create them, then --restart-at"};
  }

  std::vector<prp_sim::Test_info> tests;

  // ---- setup: lower DUT -> Slop, generate the single driver (drv.cpp)
  if (!run_only) {
    ensure_dir(simdir);
    opts.language = "pyrope";
    opts.files    = sources;  // compile ALL positional sources (imports resolve across them)
    if (vcd_on) {
      opts.sets.emplace_back("compile.sim.vcd", "1");  // make cgen_sim emit the VCD machinery
    }
    // `sim` is DYNAMIC verification: each `test` block's asserts are checked by
    // running the generated driver, not formally. Skip pass.formal — a `test`
    // lowers to a never-instantiated comb whose runtime parameters become free
    // inputs, so a concrete-valued assert (`assert(acc == 22)`) would otherwise
    // be "refuted" over those free inputs even though the bound run satisfies it.
    opts.sets.emplace_back("compile.formal.mode", "none");
    opts.emit_dirs.push_back(Typed_path{"sim", simdir});
    auto ir = gather_ir_inputs(opts, "sim");
    compile_sources(opts, res, ir);
    if (res.status != "pass") {
      return;
    }
    std::string err;
    if (prp_sim::generate(file, simdir, test_sel, vcd_dir, tests, err) != 0) {
      res.status        = "fail";
      res.error_class   = "unsupported";
      res.error_message = err;
      res.exit_code     = 1;
      return;
    }
    // Also append a single `drv` cc_binary so the generated dir stays
    // bazel-buildable (`cd <simdir> && bazel run //:drv -- --test ...`); the
    // default `lhd sim` flow runs it via the fast host-compile below, no bazel.
    std::ofstream bf(std::format("{}/BUILD", simdir), std::ios::app);
    bf << "\nload(\"@rules_cc//cc:defs.bzl\", \"cc_binary\")\n";
    bf << std::format(
        "cc_binary(\n    name = \"{0}\",\n    srcs = [\"{0}.cpp\"],\n    copts = [\"-std=c++23\"],\n"
        "    deps = [\":sim\", \"@hlop//hlop\"],\n)\n",
        prp_sim::kDriverBasename);
    bf.close();
    res.recipe_steps.push_back(std::format("sim setup: {} test(s) in {}", tests.size(), simdir));
  }

  if (setup_only) {
    res.outputs.push_back(simdir);
    if (pretty) {
      std::print("  sim setup complete: {} test(s) generated in {}\n", tests.size(), simdir);
      std::print("  run with: lhd sim {} --run-only --workdir {}\n", file, simroot);
      std::fflush(stdout);
    }
    return;  // status stays pass
  }

  auto capture = [](const std::string& c, int& rc) {
    std::string out;
    char        buf[4096];
    FILE*       p = ::popen(c.c_str(), "r");
    if (p == nullptr) {
      rc = -1;
      return out;
    }
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, p)) > 0) {
      out.append(buf, n);
    }
    int st = ::pclose(p);
    rc     = (WIFEXITED(st) ? WEXITSTATUS(st) : -1);
    return out;
  };

  // ---- the single fast run path: host-compile drv.cpp + the DUT bodies (+ the
  // VCD writer when sim.vcd) with the host C++ compiler, then run the one binary.
  // The Slop runtime is header-only and, with -DNDEBUG, has no link deps — so
  // there is no nested bazel, no abseil, no network. (For --run-only the driver +
  // bodies are reused from a prior --setup-only; only the compile + run happen.)
  const std::string drv_cpp = std::format("{}/{}.cpp", simdir, prp_sim::kDriverBasename);
  if (::access(drv_cpp.c_str(), R_OK) != 0) {
    res.status        = "fail";
    res.error_class   = "usage";
    res.error_message = std::format("no generated sim driver in {} (run --setup-only --workdir {} first)", simdir, simroot);
    res.exit_code     = 1;
    return;
  }
  // A VCD request against a prior --setup-only that was generated WITHOUT VCD (the
  // driver lacks the trace machinery) would silently produce no waveform — reject
  // it so the user regenerates instead. The `vcd::global_timestamp` line is emitted
  // iff VCD codegen was on (prp_sim).
  if (run_only && vcd_on) {
    std::ifstream     dfs(drv_cpp);
    std::stringstream dss;
    dss << dfs.rdbuf();
    if (dss.str().find("vcd::global_timestamp") == std::string::npos) {
      res.status        = "fail";
      res.error_class   = "usage";
      res.error_message = "this --run-only sim was generated without VCD; re-run without --run-only (or "
                          "--setup-only --set sim.vcd=true) so the driver gets the trace machinery";
      res.exit_code     = 1;
      return;
    }
  }
  const std::string hlop_inc    = sim_hlop_include_dir(opts);
  const std::string iassert_inc = sim_iassert_include_dir(opts);
  if (hlop_inc.empty() || iassert_inc.empty()) {
    res.status        = "fail";
    res.error_class   = "dependency";
    res.error_message = std::format("could not locate the sim runtime headers (slop.hpp: {}, iassert.hpp: {})",
                                    hlop_inc.empty() ? "<not found>" : hlop_inc, iassert_inc.empty() ? "<not found>" : iassert_inc);
    res.exit_code     = 1;
    if (pretty) {
      std::print("  hint: --set compile.cgen.sim_hlop=/path/to/hlop  --set compile.cgen.sim_iassert=/path/to/iassert/src\n");
      std::fflush(stdout);
    }
    return;
  }
  const std::string cxx = sim_host_cxx();

  // The DUT bodies: every non-driver *.cpp in simdir. inou.cgen.sim does NOT emit
  // the `%`-named `test` units, so these are exactly the real module bodies.
  std::vector<std::string> bodies;
  {
    std::error_code ec;
    for (auto& de : fs::directory_iterator(simdir, ec)) {
      if (!de.is_regular_file()) {
        continue;
      }
      auto fn = de.path().filename().string();
      if (fn.size() < 5 || fn.substr(fn.size() - 4) != ".cpp") {
        continue;
      }
      if (fn == std::string(prp_sim::kDriverBasename) + ".cpp") {
        continue;  // the driver itself (exact name — a prefix skip would also drop a `drv*.prp` design's DUT bodies)
      }
      bodies.push_back(de.path().string());
    }
    std::sort(bodies.begin(), bodies.end());
  }

  const std::string exe = std::format("{}/{}.bin", simdir, prp_sim::kDriverBasename);
  std::string       cc  = std::format("{} -std=c++23 -DNDEBUG -O1 -I{} -I{} -I{}", shell_quote(cxx), shell_quote(simdir),
                                       shell_quote(hlop_inc), shell_quote(iassert_inc));
  for (const auto& b : bodies) {
    cc += " " + shell_quote(b);
  }
  cc += " " + shell_quote(drv_cpp);
  if (vcd_on) {
    cc += " " + shell_quote(hlop_inc + "/vcd_writer.cpp");  // link the VCD writer
  }
  cc += " -o " + shell_quote(exe) + " 2>&1";  // merge compiler diagnostics into the capture
  int  build_rc  = 0;
  auto build_out = capture(cc, build_rc);
  if (build_rc != 0) {
    res.status        = "fail";
    res.error_class   = "compile";
    res.error_message = "the sim driver failed to compile (see the compiler output)";
    res.exit_code     = 1;
    if (pretty) {
      std::istringstream iss(build_out);
      std::string        ln;
      while (std::getline(iss, ln)) {
        std::print("    {}\n", ln);
      }
      std::fflush(stdout);
    }
    return;
  }

  // Run the one binary. A test selector (`lhd sim foo.prp my.test`) becomes
  // `--test`; an explicit `--seed` and every `lhd sim --arg key=value` are
  // forwarded (the binary itself filters per-test params + warns on a `--arg`
  // that no run test consumes).
  std::string run_args;
  if (!test_sel.empty()) {
    run_args += " --test " + shell_quote(test_sel);
  }
  if (opts.seed_explicit) {
    run_args += " --seed " + shell_quote(opts.seed);
  }
  // Always ask the driver for its per-test result array (a sidecar JSON file);
  // it is read back below and embedded verbatim as the envelope's "tests" member
  // (so `lhd sim --result-json r.json` carries {test,status,cycle,failing_assert,
  // prp_file,line,msg} per test). Remove any stale sidecar from a reused workdir.
  const std::string sim_tests_path = std::format("{}/sim_tests.json", simdir);
  {
    std::error_code ec_rm;
    fs::remove(sim_tests_path, ec_rm);
  }
  run_args += " --result-json " + shell_quote(sim_tests_path);
  // Checkpoint creation (sim.checkpoint*): enabled by default; the driver owns the
  // fork cadence / prune and only writes once the min-secs floor elapses.
  if (!ckpt_dir.empty()) {
    run_args += " --ckpt-dir " + shell_quote(ckpt_dir);
    if (!ckpt_min_secs.empty()) {
      run_args += " --checkpoint-min-secs " + shell_quote(ckpt_min_secs);
    }
    if (!ckpt_max.empty()) {
      run_args += " --checkpoint-max " + shell_quote(ckpt_max);
    }
    if (!ckpt_max_overhead.empty()) {
      run_args += " --checkpoint-max-overhead " + shell_quote(ckpt_max_overhead);
    }
    if (!ckpt_every.empty()) {
      run_args += " --checkpoint-every " + shell_quote(ckpt_every);
    }
  } else {
    run_args += " --no-checkpoint";
  }
  // Debug replay: jump to the failure region (loads the nearest checkpoint <= N).
  if (opts.sim_restart_at >= 0) {
    run_args += " --restart-at " + shell_quote(std::to_string(opts.sim_restart_at));
  }
  // Windowed VCD: restart near Y, run silent to Y, trace [Y, Z].
  if (opts.sim_vcd_from >= 0) {
    run_args += " --vcd-from " + shell_quote(std::to_string(opts.sim_vcd_from));
    if (opts.sim_vcd_to >= 0) {
      run_args += " --vcd-to " + shell_quote(std::to_string(opts.sim_vcd_to));
    }
  }
  // On an assert fire, auto-dump a VCD of the failure region (re-run from the
  // nearest checkpoint with a window around the failing cycle).
  if (opts.sim_vcd_on_fail) {
    run_args += " --vcd-on-fail --vcd-fail-window " + shell_quote(std::to_string(opts.sim_vcd_fail_window));
  }
  // Observability: --list-signals / --probe / --break-when. Results go to the
  // debug sidecar, read back below and embedded as the envelope's "debug" member.
  const std::string sim_debug_path = std::format("{}/sim_debug.json", simdir);
  {
    std::error_code ec_rm2;
    fs::remove(sim_debug_path, ec_rm2);
  }
  const bool debug_requested = opts.sim_list_signals || !opts.sim_probe.empty() || !opts.sim_break_when.empty();
  if (debug_requested) {
    run_args += " --debug-json " + shell_quote(sim_debug_path);
    if (opts.sim_list_signals) {
      run_args += " --list-signals";
    }
    if (!opts.sim_probe.empty()) {
      run_args += " --probe " + shell_quote(opts.sim_probe);
      if (opts.sim_probe_from >= 0) {
        run_args += " --probe-from " + shell_quote(std::to_string(opts.sim_probe_from));
      }
      if (opts.sim_probe_to >= 0) {
        run_args += " --probe-to " + shell_quote(std::to_string(opts.sim_probe_to));
      }
    }
    if (!opts.sim_break_when.empty()) {
      run_args += " --break-when " + shell_quote(opts.sim_break_when);
    }
  }
  // Forward each `--arg key=value` as `--key value`, but ONLY when `key` is a
  // parameter of a SELECTED test (`selected_params`). Two reasons:
  //  * a key that is a driver control flag (`--arg help=1` -> `--help`, `--arg
  //    test=x` -> `--test x`, `--arg seed=N`) would otherwise be intercepted by
  //    the binary and silently skip / restrict the run — a false green;
  //  * a key that is a real parameter of some test but not a selected one is
  //    irrelevant to this run, so it is dropped silently (not forwarded).
  // A key that is a parameter of NO test in the file (`all_params`) is a genuine
  // typo and is warned about unconditionally (visible in JSON mode too). This
  // restores the pre-single-driver two-layer guard. `tests` lists the SELECTED
  // tests' parameters (generate / list_tests already filtered by `test_sel`); for
  // --run-only re-derive them from the source.
  if (run_only && tests.empty()) {
    std::vector<prp_sim::Test_info> lt;
    std::string                     lerr;
    if (prp_sim::list_tests(file, test_sel, lt, lerr) == 0) {
      tests = std::move(lt);
    }
  }
  std::set<std::string> selected_params;
  for (const auto& t : tests) {
    for (const auto& p : t.params) {
      selected_params.insert(p.name);
    }
  }
  std::set<std::string> all_params = selected_params;  // == selected when no test_sel
  if (!test_sel.empty()) {
    std::vector<prp_sim::Test_info> allt;
    std::string                     aerr;
    if (prp_sim::list_tests(file, "", allt, aerr) == 0) {
      for (const auto& t : allt) {
        for (const auto& p : t.params) {
          all_params.insert(p.name);
        }
      }
    }
  }
  for (const auto& [k, v] : opts.sim_args) {
    if (selected_params.count(k) != 0) {
      run_args += " " + shell_quote("--" + k) + " " + shell_quote(v);
    } else if (all_params.count(k) == 0) {
      std::print(stderr, "lhd sim: warning: --arg {}={} matches no test parameter (ignored)\n", k, v);
    }
    // else: a real parameter of an unselected test — valid but not for this run.
  }
  // Capture the binary's STDOUT (its `puts` output + the per-test PASS/FAIL
  // verdict lines) for parsing + the pretty relay, but let its STDERR pass
  // through to the user's stderr UNCHANGED — that is where the driver prints its
  // warnings (e.g. a `--arg` that matches no test parameter) and usage errors, so
  // they stay visible in JSON mode too (not only in the pretty relay below).
  int  rc  = 0;
  auto out = capture(std::format("{}{}", shell_quote(exe), run_args), rc);

  // Read back the per-test result array (present whenever the driver ran, even on
  // assert failure); embedded as the envelope's "tests" member. Absent if the
  // driver crashed before writing it.
  if (std::error_code ec_st; fs::exists(sim_tests_path, ec_st)) {
    std::ifstream     ifs(sim_tests_path);
    std::stringstream ss;
    ss << ifs.rdbuf();
    std::string tj = ss.str();
    while (!tj.empty() && (tj.back() == '\n' || tj.back() == '\r' || tj.back() == ' ' || tj.back() == '\t')) {
      tj.pop_back();
    }
    if (tj.size() >= 2 && tj.front() == '[') {  // a well-formed array, never a partial write
      res.sim_tests_json = tj;
    }
  }

  // Read back the observability sidecar ({signals?, probe?, break?}); embedded as
  // the envelope's "debug" member.
  if (std::error_code ec_dbg; debug_requested && fs::exists(sim_debug_path, ec_dbg)) {
    std::ifstream     ifs(sim_debug_path);
    std::stringstream ss;
    ss << ifs.rdbuf();
    std::string dj = ss.str();
    while (!dj.empty() && (dj.back() == '\n' || dj.back() == '\r' || dj.back() == ' ' || dj.back() == '\t')) {
      dj.pop_back();
    }
    if (dj.size() >= 2 && dj.front() == '{') {
      res.sim_debug_json = dj;
    }
  }

  // The binary's EXIT CODE is the authoritative verdict (0 = all selected tests
  // passed, 1 = a test failed, 2 = a usage error, <0 = the driver crashed). The
  // per-test `PASS <name>` / `FAIL <name> (...)` lines are parsed only for the
  // structured recipe detail (best-effort: a test that itself `puts` a line
  // starting with "PASS "/"FAIL " adds a cosmetic recipe entry but never changes
  // the verdict, which is rc-driven).
  int    n_fail = 0;
  size_t n_run  = 0;
  {
    std::istringstream iss(out);
    std::string        ln;
    while (std::getline(iss, ln)) {
      const bool pass = ln.rfind("PASS ", 0) == 0;
      const bool fail = ln.rfind("FAIL ", 0) == 0;
      if (!pass && !fail) {
        continue;
      }
      ++n_run;
      std::string name = ln.substr(5);
      if (auto sp = name.find(" ("); sp != std::string::npos) {
        name = name.substr(0, sp);
      }
      res.recipe_steps.push_back(std::format("sim {} ({})", name, pass ? "pass" : "fail"));
      if (fail) {
        ++n_fail;
      }
    }
  }
  if (pretty) {
    std::istringstream iss(out);
    std::string        ln;
    while (std::getline(iss, ln)) {
      std::print("    {}\n", ln);
    }
    std::fflush(stdout);
  }
  res.outputs.push_back(simdir);
  if (rc == 0) {
    // every selected test passed — status stays pass.
  } else if (rc == 2) {
    // a usage error inside the binary (unknown flag / unknown `--test` name /
    // missing value / bad `--arg` value); the binary printed the specific reason
    // (relayed above in pretty mode).
    res.status        = "fail";
    res.error_class   = "usage";
    res.error_message = (!test_sel.empty() && n_run == 0) ? std::format("no test matched '{}' in {}", test_sel, file)
                                                          : std::format("the sim driver rejected an argument in {}", file);
    res.exit_code     = 1;
  } else {
    // rc == 1 (a test's assert fired) or rc < 0 (the driver crashed / signaled).
    res.status        = "fail";
    res.error_class   = "assert";
    res.error_message = (n_fail > 0 && n_run > 0) ? std::format("{} of {} test(s) failed", n_fail, n_run)
                                                  : std::format("the sim driver exited with code {}", rc);
    res.exit_code     = 1;
  }
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

// The yosys-slang plugin (slang.so) for lgcheck's `--gold_reader slang`: lets
// yosys read SystemVerilog packed-struct sources (CIRCT output) that
// read_verilog cannot parse. Same candidates inou_yosys_api probes.
std::string locate_yosys_slang_plugin() {
  auto exe_path = file_utils::get_exe_path();
  for (const auto& cand : {absl::StrCat(exe_path, "/../external/+_repo_rules+yosys_slang/slang.so"),
                           absl::StrCat(exe_path, "/../external/+http_archive+yosys_slang/slang.so"),
                           absl::StrCat(exe_path, "/lhd.runfiles/+http_archive+yosys_slang/slang.so")}) {
    if (::access(cand.c_str(), R_OK) == 0) {
      return cand;
    }
  }
  return "";
}

// The lgyosys backend (`--set lec.solver=lgyosys`): materialize both sides to
// Verilog and discharge with inou/yosys/lgcheck (the former `lhd check`).
// Verilog sides pass straight through; pyrope:/ln:/lg: are compiled first.
void lec_lgyosys(Options& opts, Result& res) {
  auto impl_v  = fs::absolute(materialize_verilog(opts, res, opts.impl_kind, opts.impl_path, "impl")).string();
  auto ref_v   = fs::absolute(materialize_verilog(opts, res, opts.ref_kind, opts.ref_path, "ref")).string();
  auto lgcheck = locate_lgcheck();
  auto yosys   = locate_lgcheck_yosys();

  // --set lec.gold_reader=slang: read the REFERENCE side through yosys-slang
  // (SystemVerilog packed structs / '{...} patterns exceed read_verilog).
  std::string gold_reader = "verilog";
  for (const auto& [k, v] : opts.sets) {
    if (k == "lec.gold_reader" && !v.empty()) {
      gold_reader = v;
    }
  }
  if (gold_reader != "verilog" && gold_reader != "slang") {
    throw Lhd_error{"usage", std::format("--set lec.gold_reader expects verilog|slang, got '{}'", gold_reader), ""};
  }
  std::string slang_plugin;
  if (gold_reader == "slang") {
    slang_plugin = locate_yosys_slang_plugin();
    if (slang_plugin.empty()) {
      throw Lhd_error{"dependency", "lec.gold_reader=slang: yosys-slang plugin (slang.so) not found",
                      "build //inou/yosys (the @yosys_slang external) or use the default gold_reader"};
    }
  }

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
  if (gold_reader == "slang") {
    cmd += std::format(" --gold_reader slang --slang_plugin {}", shell_quote(slang_plugin));
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
  int rc   = std::system(cmd.c_str());
  int code = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
  if (opts.verbose) {
    mirror_log_to_stderr(log);
  }
  std::string name = !opts.impl_top.empty() ? opts.impl_top : opts.impl_path;
  // lgcheck exit codes: 0 = proven equivalent, 2 = INCONCLUSIVE (could not prove
  // AND found no counterexample — yosys' equiv flow often can't prove a
  // cgen-restructured netlist equal to its source even when it is), anything else
  // = a real refutation. Only a real refute is a hard failure.
  if (code == 2) {
    std::print("lec: '{}' INCONCLUSIVE (solver=lgyosys; could not prove, no counterexample)\n", name);
    return;
  }
  std::print("lec: '{}' {} (solver=lgyosys)\n", name, code == 0 ? "PROVEN equivalent" : "REFUTED (not equivalent)");
  if (code != 0) {
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
        // A pyrope: input can be a single .prp OR an emit DIRECTORY holding one
        // .prp per module (the slang->pyrope multi-module emission). inou.prp
        // splits `files` on comma and loads each as its own LNAST; the runner
        // then resolves the top's import() of its sibling modules. Enumerate the
        // dir's *.prp so a multi-file library recompiles (a lone top file would
        // fail import-no-progress with its callees absent).
        std::string files = path;
        if (fs::is_directory(path)) {
          std::vector<std::string> prps;
          for (const auto& de : fs::directory_iterator(path)) {
            if (de.is_regular_file() && de.path().extension() == ".prp") {
              prps.push_back(de.path().string());
            }
          }
          if (prps.empty()) {
            throw Lhd_error{"missing_file", std::format("pyrope: directory has no .prp files: {}", path), ""};
          }
          std::sort(prps.begin(), prps.end());
          files.clear();
          for (const auto& p : prps) {
            files += (files.empty() ? "" : ",") + p;
          }
        } else {
          check_inputs_exist({path});
        }
        run_step("inou.prp", var, {{"files", files}}, opts, res);
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

// Emit the machine-parseable per-block progress line (info severity: never an
// error or exit-code change) the moment a block resolves, so an agent driving a
// long bottom-up run stream-parses pass/fail/inconclusive instead of waiting for
// the end. Reuses the diag jsonl/pretty rendering; the record carries the block
// name, the verdict, the engine that reached it (the portfolio winner r.engine
// when the auto engine set one, else the requested engine), and the elapsed ms.
static void emit_lec_block_progress(std::string_view block, const livehd::lec::Query_result& r,
                                    const livehd::lec::Lec_options& o, long long elapsed_ms) {
  const char* code;
  const char* verdict;
  switch (r.verdict) {
    case livehd::lec::Verdict::Proven : code = "lec-block-proven";       verdict = "pass";         break;
    case livehd::lec::Verdict::Refuted: code = "lec-block-refuted";      verdict = "fail";         break;
    default                           : code = "lec-block-inconclusive"; verdict = "inconclusive"; break;
  }
  const std::string eng = r.engine.empty() ? o.engine : r.engine;
  const long long   ms  = r.elapsed_ms >= 0 ? r.elapsed_ms : elapsed_ms;
  auto              b   = livehd::diag::info("pass.lec", code, "progress")
               .msg("lec block '{}' {}", block, verdict)
               .verdict(verdict)
               .engine(eng)
               .duration_ms(ms);
  if (!r.detail.empty()) {
    b.attr("detail", r.detail);
  }
  if (!r.witness.empty()) {
    b.attr("witness", r.witness);
  }
  if (o.engine == "bmc" || o.engine == "auto") {
    b.attr("bound", std::to_string(o.bound));
  }
  b.emit();
}

// Bottom-up hierarchical LEC driver (lec.hierarchical=true). Build the module-def
// dependency DAG over the defs present in both libraries (paired by ENTITY — see
// below), topo-order it leaves-first, and LEC each def under the `auto` portfolio. Record the proven
// set; for each parent, force-black-box its PROVEN child instances (--collapse) so
// the parent proof stops re-solving them, while a child NOT provable in isolation
// stays FLATTENED into the parent (descended) — the M5 CEGAR / un-black-box
// fallback, now in v1. Correspondence is name-based (no semdiff needed when the
// call structures match). Each def emits a per-block progress line the instant it
// resolves, so an agent stream-parses the long run. Returns the TOP def's result.
static livehd::lec::Query_result lec_hierarchical(Result& res, Eprp_var& ref_var, Eprp_var& impl_var,
                                                  const std::string&        top_name,
                                                  hhds::Graph* ref_top_g, hhds::Graph* impl_top_g,
                                                  const livehd::lec::Lec_options& base,
                                                  const absl::flat_hash_map<hhds::Gid, hhds::Graph*>* sub_lib) {
  using livehd::lec::Verdict;
  namespace gu = livehd::graph_util;

  // key -> def graph (case-sensitive, LiveHD/Pyrope name policy). A def's FULL
  // graph name embeds its front-end namespace (Pyrope "file.entity" vs slang's
  // flat "entity"), so the same module never shares a full name across a
  // cross-front-end pair. Defs therefore pair by ENTITY (the post-'.' tail)
  // when that entity names exactly ONE graph on its side; an ambiguous entity
  // keeps the full name (such defs simply stay flattened into their parents).
  // pass/lec's box-correspondence builder canonicalizes the same way, so the
  // entity keys pushed into o.collapse resolve on both sides.
  auto entity_of = [](std::string_view n) -> std::string {
    auto d = n.rfind('.');
    return std::string(d == std::string_view::npos ? n : n.substr(d + 1));
  };
  absl::flat_hash_map<std::string, int> ref_ent_cnt, impl_ent_cnt;
  for (auto& g : ref_var.graphs) {
    if (g) {
      ref_ent_cnt[entity_of(g->get_name())]++;
    }
  }
  for (auto& g : impl_var.graphs) {
    if (g) {
      impl_ent_cnt[entity_of(g->get_name())]++;
    }
  }
  auto canon_ref = [&](std::string_view full) -> std::string {
    auto e  = entity_of(full);
    auto it = ref_ent_cnt.find(e);
    return it != ref_ent_cnt.end() && it->second == 1 ? e : std::string(full);
  };
  auto canon_impl = [&](std::string_view full) -> std::string {
    auto e  = entity_of(full);
    auto it = impl_ent_cnt.find(e);
    return it != impl_ent_cnt.end() && it->second == 1 ? e : std::string(full);
  };
  absl::flat_hash_map<std::string, hhds::Graph*> ref_by_name, impl_by_name;
  for (auto& g : ref_var.graphs) {
    if (g) {
      ref_by_name[canon_ref(g->get_name())] = g.get();
    }
  }
  for (auto& g : impl_var.graphs) {
    if (g) {
      impl_by_name[canon_impl(g->get_name())] = g.get();
    }
  }

  // Per-side tops may DIFFER (--ref-top vs --impl-top; e.g. v2prp LECs the
  // emitted .v module name against the original Pyrope lambda). The by-name
  // pairing alone would then never LEC the top pair at all, and an UNKNOWN
  // top exits 0 under the inconclusive-is-a-warning policy — a silent
  // vacuous pass. Force-pair the two explicitly-picked TOP graphs under the
  // ref-top key so the driver always proves/refutes the top itself.
  const std::string top_key = canon_ref(top_name);
  ref_by_name[top_key]      = ref_top_g;
  impl_by_name[top_key]     = impl_top_g;

  // The LEC-able defs are those present on BOTH sides; children[def] = the child
  // def keys it instantiates (taken from the ref-side Subs, canonicalized).
  absl::flat_hash_map<std::string, std::vector<std::string>> children;
  std::vector<std::string>             defs;
  for (auto& [name, g] : ref_by_name) {
    if (impl_by_name.find(name) == impl_by_name.end()) {
      continue;
    }
    defs.push_back(name);
    absl::flat_hash_set<std::string> seen;
    for (auto node : g->forward_class()) {
      if (gu::type_op_of(node) != Ntype_op::Sub) {
        continue;
      }
      auto        sio = node.get_subnode_io();
      std::string cn  = canon_ref(sio->get_name());
      if (ref_by_name.find(cn) != ref_by_name.end() && impl_by_name.find(cn) != impl_by_name.end() && !seen.count(cn)) {
        children[name].push_back(cn);
        seen.insert(cn);
      }
    }
  }

  // Topo-order leaves-first (DFS post-order; the in-progress mark guards cycles).
  std::vector<std::string> order;
  absl::flat_hash_map<std::string, int>          mark;  // 0 unvisited, 1 in-progress, 2 done
  std::function<void(const std::string&)> dfs = [&](const std::string& n) {
    int& m = mark[n];
    if (m != 0) {
      return;  // done, or a cycle back-edge (modules form a DAG)
    }
    m = 1;
    if (auto it = children.find(n); it != children.end()) {
      for (const auto& c : it->second) {
        dfs(c);
      }
    }
    m = 2;
    order.push_back(n);
  };
  for (const auto& name : defs) {
    dfs(name);
  }

  // LEC each def leaves-first; collapse its already-proven children.
  absl::flat_hash_set<std::string>                proven;
  livehd::lec::Query_result top_result;
  bool                      have_top      = false;
  int                       semdiff_count = 0;  // defs dropped structurally (no solver)
  std::vector<std::string>  proven_list, collapsed_note;
  for (const auto& name : order) {
    livehd::lec::Lec_options o = base;
    // Each def is LEC'd under the requested engine (lec.engine, default `auto` =
    // the ind+bmc portfolio). Honor an explicit engine so `--set lec.engine=bmc`
    // (e.g. a reset-phase proof) is not silently overridden by the hierarchical driver.

    // M3 structural def-diff reduction: a def whose ref/impl are structurally
    // IDENTICAL (no unmatched node on either side) and whose children are ALL
    // proven is equivalent with NO solver call. A parent's own-structure match
    // does NOT cover a child's internals (the child Sub matches by name regardless),
    // so require the children proven first — leaves-first guarantees they are settled.
    if (o.semdiff != "none") {
      bool kids_proven = true;
      if (auto it = children.find(name); it != children.end()) {
        for (const auto& c : it->second) {
          if (!proven.count(c)) {
            kids_proven = false;
            break;
          }
        }
      }
      if (kids_proven) {
        auto                            t0 = std::chrono::steady_clock::now();
        livehd::semdiff::Semdiff_options so;
        so.alg            = o.semdiff;
        so.matching_names = true;  // anchor flops/mems by hier name (lec's correspondence basis)
        auto m            = livehd::semdiff::structural_match(ref_by_name[name], impl_by_name[name], so);
        const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        if (m.a_unmatched == 0 && m.b_unmatched == 0) {
          livehd::lec::Query_result sr;
          sr.verdict    = Verdict::Proven;
          sr.engine     = "semdiff";
          sr.elapsed_ms = ms;
          sr.detail     = std::format("structurally identical ({}: {} matched node(s), no solver call)", so.alg, m.a_matched);
          emit_lec_block_progress(name, sr, o, ms);
          proven.insert(name);
          proven_list.push_back(name);
          ++semdiff_count;
          std::print("lec[hier]: '{}' MATCHED (semdiff {}, no solver)\n", name, so.alg);
          if ((name == top_key)) {
            top_result = sr;
            have_top   = true;
          }
          continue;  // skip the solver for this def
        }
      }
    }

    o.collapse.clear();
    std::vector<std::string> coll;
    if (auto it = children.find(name); it != children.end()) {
      for (const auto& c : it->second) {
        if (proven.count(c)) {
          o.collapse.push_back(c);  // proven child -> sound black-box collapse
          coll.push_back(c);
        }
        // a non-proven child is left OUT of the collapse set -> flattened (descended)
      }
    }
    auto t0 = std::chrono::steady_clock::now();
    auto r  = livehd::lec::prove_equal(ref_by_name[name], impl_by_name[name], o, sub_lib);
    if (r.verdict == Verdict::Refuted && !coll.empty()) {
      // A REFUTE under proven-child collapse is an ABSTRACTION verdict: the box
      // over-approximates the child (free/UF values the real leaf can never
      // emit, and — for unnamed interchangeable instances — an occurrence-paired
      // correspondence that may associate different physical instances), so the
      // counterexample can be spurious. Confirm FLAT (collapse cleared, children
      // descended) before reporting a fail: flat-Proven is adopted, flat-Unknown
      // stays inconclusive. A FAIL is then only ever reported from a
      // counterexample free of proven-child collapse boxes (true blackboxes for
      // UNRESOLVED defs may remain in the flat run — those correspond
      // explicitly and gate to inconclusive when one-sided).
      std::print("lec[hier]: '{}' REFUTED under collapse ({} box def(s)) -> flat confirmation\n", name, coll.size());
      livehd::lec::Lec_options oflat = o;
      oflat.collapse.clear();
      auto rf   = livehd::lec::prove_equal(ref_by_name[name], impl_by_name[name], oflat, sub_lib);
      rf.detail = "flat-confirm after collapsed-box REFUTE" + std::string(rf.detail.empty() ? "" : "; ") + rf.detail
                + (r.detail.empty() ? "" : " (collapsed run: " + r.detail + ")");
      rf.elapsed_ms = -1;  // the progress record carries the combined wall-clock below
      r = std::move(rf);
    }
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    emit_lec_block_progress(name, r, o, ms);
    if (r.verdict == Verdict::Proven) {
      proven.insert(name);
      proven_list.push_back(name);
    }
    if (!coll.empty()) {
      collapsed_note.push_back(name + "<-{" + [&] {
        std::string s;
        for (const auto& c : coll) {
          s += (s.empty() ? "" : ",") + c;
        }
        return s;
      }() + "}");
    }
    std::print("lec[hier]: '{}' {} ({} child collapse{})\n",
               name,
               r.verdict == Verdict::Proven ? "PROVEN" : (r.verdict == Verdict::Refuted ? "REFUTED" : "UNKNOWN"),
               coll.size(),
               coll.size() == 1 ? "" : "s");
    if ((name == top_key)) {
      top_result = r;
      have_top   = true;
    }
  }

  std::print("lec[hier]: {}/{} def(s) proven leaves-first ({} via semdiff, {} via solver)\n",
             proven_list.size(),
             order.size(),
             semdiff_count,
             static_cast<int>(proven_list.size()) - semdiff_count);
  res.recipe_steps.emplace_back(std::format("pass.lec hierarchical defs:{} proven:{} semdiff:{}",
                                            order.size(),
                                            proven_list.size(),
                                            semdiff_count));

  if (!have_top) {
    top_result.verdict = Verdict::Unknown;
    top_result.detail  = std::format("hierarchical: top '{}' not found in both libraries", top_name);
  }
  return top_result;
}

// ===== lecfail witness reproduction (`lhd lec` + --workdir) ==================
// On a REFUTED verdict with a reproducible BMC trace, write a self-contained
// Pyrope testbench (lec.prpfail, default lecfail.prp) that instantiates BOTH
// designs inside one wrapper module, drives the counterexample input sequence,
// and (with lec.prpfailrun) runs `lhd sim` to dump ONE VCD (lecfail.vcd) so the
// impl-vs-ref divergence is visualized / re-runnable. Every step is best-effort:
// a side that cannot re-emit as Pyrope (lg:/yosys netlists have no LNAST), a
// name clash, or a sim build error is a WARNING, never a hard failure — the LEC
// verdict already stands on its own.

// One re-emitted Pyrope module: name + parsed header IO + full source text (the
// emitted `::[lg="..", hdl]` attribute is kept verbatim; a fresh sim compile
// ignores the stale `lg=` reference — validated).
struct Lecfail_mod {
  std::string                                      name;
  std::string                                      text;     // full module source
  std::vector<std::pair<std::string, std::string>> inputs;   // {name, ":type" suffix or ""}
  std::vector<std::pair<std::string, std::string>> outputs;  // {name, ":type@[..]" suffix or ""}
};

// Simple (unqualified) module name: the tail after the last '.' (a graph named
// "impl.dut" re-emits as `mod dut`).
std::string lecfail_simple_name(std::string_view n) {
  auto dot = n.rfind('.');
  return std::string(dot == std::string_view::npos ? n : n.substr(dot + 1));
}

// Split a comma-separated IO list ("en, din:u8") into {name, ":type" suffix}.
void lecfail_parse_io(std::string_view list, std::vector<std::pair<std::string, std::string>>& out) {
  size_t i = 0;
  while (i <= list.size()) {
    size_t           c    = list.find(',', i);
    std::string_view item = list.substr(i, (c == std::string_view::npos ? list.size() : c) - i);
    size_t           b    = item.find_first_not_of(" \t\r\n");
    size_t           e    = item.find_last_not_of(" \t\r\n");
    if (b != std::string_view::npos) {
      item             = item.substr(b, e - b + 1);
      size_t colon     = item.find(':');
      if (colon == std::string_view::npos) {
        out.emplace_back(std::string(item), std::string{});
      } else {
        out.emplace_back(std::string(item.substr(0, colon)), std::string(item.substr(colon)));
      }
    }
    if (c == std::string_view::npos) {
      break;
    }
    i = c + 1;
  }
}

// Parse `... mod NAME[::[..]](in..) -> (out..) {` from a module's source. The
// attribute block and types carry no parens, so the first '(' after the name is
// the input list. Returns false if no `mod` header is present (e.g. an empty
// file-level unit).
bool lecfail_parse_header(std::string_view text, Lecfail_mod& m) {
  size_t mp = text.find("mod ");
  if (mp == std::string_view::npos) {
    return false;
  }
  size_t p  = mp + 4;
  size_t ns = p;
  while (p < text.size() && text[p] != ':' && text[p] != '(' && text[p] != ' ' && text[p] != '\t' && text[p] != '\n') {
    ++p;
  }
  m.name.assign(text.substr(ns, p - ns));
  if (m.name.empty()) {
    return false;
  }
  size_t io = text.find('(', p);
  if (io == std::string_view::npos) {
    return false;
  }
  size_t ic = text.find(')', io);
  if (ic == std::string_view::npos) {
    return false;
  }
  size_t body  = text.find('{', ic);
  size_t arrow = text.find("->", ic);
  if (arrow != std::string_view::npos && (body == std::string_view::npos || arrow < body)) {
    size_t oo = text.find('(', arrow);
    size_t oc = oo == std::string_view::npos ? std::string_view::npos : text.find(')', oo);
    if (oo != std::string_view::npos && oc != std::string_view::npos && (body == std::string_view::npos || oc < body)) {
      lecfail_parse_io(text.substr(oo + 1, oc - oo - 1), m.outputs);
    }
  }
  lecfail_parse_io(text.substr(io + 1, ic - io - 1), m.inputs);
  return true;
}

// Read every *.prp in `dir` (one module per file) and parse its header.
std::vector<Lecfail_mod> lecfail_parse_dir(const std::string& dir) {
  std::vector<Lecfail_mod> mods;
  std::error_code          ec;
  for (auto& de : fs::directory_iterator(dir, ec)) {
    if (!de.is_regular_file() || de.path().extension() != ".prp") {
      continue;
    }
    std::ifstream     ifs(de.path());
    std::stringstream ss;
    ss << ifs.rdbuf();
    Lecfail_mod m;
    if (!lecfail_parse_header(ss.str(), m)) {
      continue;  // empty file-level unit: no `mod`
    }
    m.text = ss.str();
    mods.push_back(std::move(m));
  }
  std::sort(mods.begin(), mods.end(), [](const Lecfail_mod& a, const Lecfail_mod& b) { return a.name < b.name; });
  return mods;
}

// Rename module `from` -> `to` ONLY at a definition (`mod from`) or an
// instantiation (`from(`) site — never inside a string/type/signal (so a stale
// `lg="side.from"` reference and any signal named like a module are untouched).
std::string lecfail_rename(const std::string& s, const std::string& from, const std::string& to) {
  auto is_ident = [](char c) { return (std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '_'; };
  std::string out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    if (s.compare(i, from.size(), from) == 0) {
      bool   lb = i == 0 || !is_ident(s[i - 1]);
      size_t j  = i + from.size();
      bool   rb = j >= s.size() || !is_ident(s[j]);
      if (lb && rb) {
        bool def  = i >= 4 && s.compare(i - 4, 4, "mod ") == 0;
        bool inst = j < s.size() && s[j] == '(';
        if (def || inst) {
          out += to;
          i = j;
          continue;
        }
      }
    }
    out += s[i];
    ++i;
  }
  return out;
}

// Give each UNTYPED module parameter an explicit `:u<width>` when its name is a
// known primary input (from the witness trace). prp_writer re-emits inputs
// untyped (`mod adder(en)`), but Pyrope needs an explicit width at an internal
// `mod` INSTANTIATION boundary — so a hierarchical DUT (a top whose sub-module
// takes a threaded top input) otherwise fails to re-compile. The top's own
// inputs are typed by the wrapper regardless; this fixes the internal boundaries.
std::string lecfail_type_params(const std::string& text, const absl::flat_hash_map<std::string, int>& width_of) {
  size_t mp = text.find("mod ");
  if (mp == std::string::npos) {
    return text;
  }
  size_t io = text.find('(', mp);
  size_t ic = io == std::string::npos ? std::string::npos : text.find(')', io);
  if (io == std::string::npos || ic == std::string::npos || ic <= io + 1) {
    return text;  // no header params
  }
  const std::string params = text.substr(io + 1, ic - io - 1);
  std::string       rebuilt;
  bool              changed = false;
  size_t            i       = 0;
  while (i <= params.size()) {
    size_t      c   = params.find(',', i);
    std::string raw = params.substr(i, (c == std::string::npos ? params.size() : c) - i);
    size_t      b = raw.find_first_not_of(" \t");
    size_t      e = raw.find_last_not_of(" \t");
    if (b != std::string::npos) {
      std::string name = raw.substr(b, e - b + 1);
      if (name.find(':') == std::string::npos) {  // untyped
        if (auto it = width_of.find(name); it != width_of.end()) {
          raw     = name + std::format(":u{}", it->second);
          changed = true;
        }
      }
    }
    rebuilt += (rebuilt.empty() ? "" : ", ") + raw;
    if (c == std::string::npos) {
      break;
    }
    i = c + 1;
  }
  if (!changed) {
    return text;
  }
  return text.substr(0, io + 1) + rebuilt + text.substr(ic);
}

// Re-emit one --impl/--ref side as Pyrope (LNAST -> .prp) by shelling to a fresh
// `lhd compile ... --emit-dir pyrope:` (clean process isolation; reuses the
// tested front-end + prp_writer flow). Returns false when the side has no LNAST
// path (lg:/yosys-verilog) or the compile fails.
bool lecfail_emit_side(const std::string& lhd_bin, const Options& opts, const std::string& kind, const std::string& path,
                       const std::string& outdir, const std::string& scratch, const std::string& log) {
  ensure_dir(outdir);
  ensure_dir(scratch);
  std::string sidearg = kind == "lg" ? "lg:" + path : (kind == "ln" ? "ln:" + path : path);
  std::string cmd     = shell_quote(lhd_bin) + " compile " + shell_quote(sidearg) + " --emit-dir "
                    + shell_quote("pyrope:" + outdir) + " --workdir " + shell_quote(scratch);
  if (kind == "verilog") {
    cmd += " --reader " + shell_quote(opts.reader);
  }
  cmd += " >> " + shell_quote(log) + " 2>&1";
  int st = std::system(cmd.c_str());
  return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

// True when the .prp at `path` declares `top` as a `pub` lambda — the precondition
// for `import("<stem>.<top>")` to resolve it. (A lec top compiled from a bare
// `mod dut` is not importable; the generator then falls back to inlining.)
// Text scan: a `pub <kind> <top>` where <top> ends the token (`(`, `:`, or space).
bool lecfail_prp_top_is_pub(const std::string& path, const std::string& top) {
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    return false;
  }
  std::stringstream ss;
  ss << ifs.rdbuf();
  const std::string text = ss.str();
  for (const char* kw : {"mod", "comb", "pipe", "fluid"}) {
    const std::string needle = std::string("pub ") + kw + " " + top;
    for (size_t p = text.find(needle); p != std::string::npos; p = text.find(needle, p + 1)) {
      const size_t e     = p + needle.size();
      const char   after = e < text.size() ? text[e] : ' ';
      if (after == '(' || after == ':' || after == ' ' || after == '\t' || after == '\n' || after == '\r') {
        return true;
      }
    }
  }
  return false;
}

// The generator proper. `impl_top`/`ref_top` are the two designs' TOP graph names
// (unqualified names are matched against the re-emitted modules).
void emit_lecfail_witness(Options& opts, Result& res, const livehd::lec::Query_result& r, const std::string& impl_top_full,
                          const std::string& ref_top_full, const std::string& prpfail, bool run_sim) {
  auto skip = [&](std::string_view why) {
    livehd::diag::info("pass.lec", "lecfail-skip", "io")
        .msg("lec.prpfail witness testbench not generated: {}", why)
        .emit();
  };
  if (r.trace.empty()) {
    skip("the verdict carries no reproducible input trace (inductive single-step CEX, or witnesses disabled)");
    return;
  }

  const std::string prpfail_path = prpfail.find('/') != std::string::npos ? prpfail : opts.workdir + "/" + prpfail;
  // Test name = the .prp basename stem, sanitized to a Pyrope identifier; it is
  // also the sole sim instance's VCD stem (`<workdir>/<stem>.vcd`).
  std::string stem = fs::path(prpfail_path).stem().string();
  std::string test_name;
  for (char c : stem) {
    test_name += (std::isalnum(static_cast<unsigned char>(c)) != 0) ? c : '_';
  }
  if (test_name.empty() || (std::isdigit(static_cast<unsigned char>(test_name[0])) != 0)) {
    test_name = "lecfail_" + test_name;
  }

  // Phase 2/3 of the lec-on-failure flow: re-emitting both sides as Pyrope shells
  // out to `lhd` twice, so announce the target up front (the write itself is quick;
  // the side re-emit is the slow part).
  livehd::diag::info("pass.lec", "lecfail-creating-prp", "progress")
      .msg("lec: creating counterexample testbench {}", prpfail_path)
      .emit();

  const std::string lhd_bin  = file_utils::get_exe_path() + "/lhd";
  const std::string impl_dir = opts.workdir + "/lecfail_impl_prp";
  const std::string ref_dir  = opts.workdir + "/lecfail_ref_prp";
  const std::string log      = next_log_path(opts, "lec.prpfail");
  if (!lecfail_emit_side(lhd_bin, opts, opts.impl_kind, opts.impl_path, impl_dir, opts.workdir + "/lecfail_impl_w", log)
      || !lecfail_emit_side(lhd_bin, opts, opts.ref_kind, opts.ref_path, ref_dir, opts.workdir + "/lecfail_ref_w", log)) {
    skip(std::format("a side could not be re-emitted as Pyrope (lg:/yosys-verilog sides have no LNAST); see {}", log));
    return;
  }

  std::vector<Lecfail_mod> impl_mods = lecfail_parse_dir(impl_dir);
  std::vector<Lecfail_mod> ref_mods  = lecfail_parse_dir(ref_dir);
  if (impl_mods.empty() || ref_mods.empty()) {
    skip("no Pyrope modules were re-emitted for a side");
    return;
  }

  std::string      impl_top = lecfail_simple_name(impl_top_full);
  std::string      ref_top  = lecfail_simple_name(ref_top_full);
  const std::string wrapper = "__lecfail_dut_pair";

  // Prefer IMPORTING the original sources: the testbench then references the two
  // designs by `import("<file>.<top>")` instead of inlining renamed copies, so
  // fixing a bug in the original .prp and re-running the SAME lecfail.prp picks
  // up the fix. Requires both sides to be Pyrope files (an lg:/verilog side has
  // no editable .prp to iterate on) with DISTINCT file stems — the stem is the
  // import unit name, and two same-named units would collide. Otherwise fall
  // back to the self-contained inline form (renamed copies) built below.
  const std::string impl_stem = fs::path(opts.impl_path).stem().string();
  const std::string ref_stem  = fs::path(opts.ref_path).stem().string();
  const bool        prp_pair  = opts.impl_kind == "pyrope" && opts.ref_kind == "pyrope" && !impl_stem.empty()
                        && !ref_stem.empty() && impl_stem != ref_stem;
  const bool impl_pub  = prp_pair && lecfail_prp_top_is_pub(opts.impl_path, impl_top);
  const bool ref_pub   = prp_pair && lecfail_prp_top_is_pub(opts.ref_path, ref_top);
  const bool can_import = prp_pair && impl_pub && ref_pub;

  // A Pyrope pair that qualifies EXCEPT for a non-`pub` top gets the inline copy
  // (which cannot iterate on the original). Nudge the user to opt into the import
  // form — `import("<file>.<top>")` needs the top to be `pub`.
  if (prp_pair && !can_import) {
    std::string which;
    if (!impl_pub) {
      which = std::format("the impl top `{}` in {}", impl_top, opts.impl_path);
    }
    if (!ref_pub) {
      which += (which.empty() ? "" : " and ") + std::format("the ref top `{}` in {}", ref_top, opts.ref_path);
    }
    livehd::diag::warn("pass.lec", "lecfail-top-not-pub", "io")
        .msg("lecfail.prp inlines a COPY of each design because a LEC top is not `pub` ({}) — mark the LEC top `pub` "
             "and the testbench will `import` the original instead, so a fix to the .prp flows into a re-run",
             which)
        .hint(std::format("e.g. `pub mod {}(...)` / `pub comb {}(...)`", impl_top, ref_top))
        .emit();
  }

  // Rename any ref-side module whose name clashes with an impl-side module (or
  // the wrapper) so both hierarchies coexist in one Pyrope namespace. Only the
  // inline (non-import) form shares a namespace; an import keeps each side's
  // modules under its own unit, so the two `<file>.<top>` graphs never collide.
  absl::flat_hash_map<std::string, std::string> ref_rename;
  if (!can_import) {
    absl::flat_hash_set<std::string> impl_names;
    for (const auto& m : impl_mods) {
      impl_names.insert(m.name);
    }
    for (const auto& m : ref_mods) {
      if (impl_names.count(m.name) != 0 || m.name == wrapper) {
        ref_rename[m.name] = "lecref_" + m.name;
      }
    }
    for (auto& m : ref_mods) {
      for (const auto& [from, to] : ref_rename) {
        m.text = lecfail_rename(m.text, from, to);
      }
    }
    for (auto& m : ref_mods) {
      if (auto it = ref_rename.find(m.name); it != ref_rename.end()) {
        m.name = it->second;
      }
    }
    if (auto it = ref_rename.find(ref_top); it != ref_rename.end()) {
      ref_top = it->second;
    }
  }

  auto find_mod = [](const std::vector<Lecfail_mod>& mods, const std::string& name) -> const Lecfail_mod* {
    for (const auto& m : mods) {
      if (m.name == name) {
        return &m;
      }
    }
    return nullptr;
  };
  const Lecfail_mod* impl_m = find_mod(impl_mods, impl_top);
  const Lecfail_mod* ref_m  = find_mod(ref_mods, ref_top);
  if (impl_m == nullptr || ref_m == nullptr || impl_m->outputs.empty() || ref_m->outputs.empty()) {
    skip("could not locate both TOP modules (or a side exposes no outputs) in the re-emitted Pyrope");
    return;
  }

  // Per-input bit width (any cycle carrying the input), for unsigned wrapper-input
  // typing — driving the unsigned magnitude then reproduces the exact bit pattern.
  absl::flat_hash_map<std::string, int> width_of;
  for (const auto& cyc : r.trace.cycles) {
    for (const auto& in : cyc.inputs) {
      width_of[in.name] = in.width < 1 ? 1 : in.width;
    }
  }
  auto wtype = [&](const std::string& n) {
    auto it = width_of.find(n);
    return std::format(":u{}", it == width_of.end() ? 1 : it->second);
  };

  // Union of the two tops' declared inputs (order: impl first, then ref extras).
  std::vector<std::string>         win;
  absl::flat_hash_set<std::string> seen;
  for (const auto& [n, t] : impl_m->inputs) {
    if (seen.insert(n).second) {
      win.push_back(n);
    }
  }
  for (const auto& [n, t] : ref_m->inputs) {
    if (seen.insert(n).second) {
      win.push_back(n);
    }
  }

  // ---- build the wrapper module -------------------------------------------
  std::string sig_in;
  for (const auto& n : win) {
    sig_in += (sig_in.empty() ? "" : ", ") + n + wtype(n);
  }
  std::string sig_out;
  for (const auto& [n, suf] : impl_m->outputs) {
    sig_out += (sig_out.empty() ? "" : ", ") + std::format("impl_{}{}", n, suf);
  }
  for (const auto& [n, suf] : ref_m->outputs) {
    sig_out += (sig_out.empty() ? "" : ", ") + std::format("ref_{}{}", n, suf);
  }
  auto call_args = [](const Lecfail_mod* m) {
    std::string a;
    for (const auto& [n, t] : m->inputs) {
      a += (a.empty() ? "" : ", ") + std::format("{} = {}", n, n);
    }
    return a;
  };
  auto side_body = [&](const std::string& top, const Lecfail_mod* m, const std::string& prefix, const std::string& tmp) {
    std::string b;
    if (m->outputs.size() == 1) {
      b += std::format("  {}{} = {}({})\n", prefix, m->outputs[0].first, top, call_args(m));
    } else {
      // A stateful multi-output instance is bound to a fresh local (needs `const`,
      // like prp_writer's own emission), then each output read as `inst.port`.
      b += std::format("  const {} = {}({})\n", tmp, top, call_args(m));
      for (const auto& [n, suf] : m->outputs) {
        b += std::format("  {}{} = {}.{}\n", prefix, n, tmp, n);
      }
    }
    return b;
  };
  // The wrapper calls each side by the imported const name (`implmod`/`refmod`)
  // when importing, else by the (possibly renamed) inlined module name.
  const std::string impl_callee = can_import ? std::string{"implmod"} : impl_top;
  const std::string ref_callee  = can_import ? std::string{"refmod"} : ref_top;
  std::string wrap_text = std::format("mod {}({}) -> ({}) {{\n", wrapper, sig_in, sig_out);
  wrap_text += side_body(impl_callee, impl_m, "impl_", "_lec_impl");
  wrap_text += side_body(ref_callee, ref_m, "ref_", "_lec_ref");
  wrap_text += "}\n";

  // ---- build the test: per-cycle stimulus arrays indexed by `clock` -------
  const int   ncyc = static_cast<int>(r.trace.cycles.size());
  auto        val_at = [&](const std::string& name, int c) -> std::string {
    for (const auto& in : r.trace.cycles[static_cast<size_t>(c)].inputs) {
      if (in.name == name) {
        return in.value;
      }
    }
    return "0";
  };
  // The implicit reset: a trace input named `reset` that is NOT a declared port
  // (Pyrope-origin designs drive their registers off it). An explicit reset PORT
  // is instead driven by name like any other input.
  const bool reset_is_port = std::find(win.begin(), win.end(), "reset") != win.end();
  const bool implicit_reset = width_of.count("reset") != 0 && !reset_is_port;

  std::string test_text = std::format("test {} {{\n  mut _lec_dut = {}\n", test_name, wrapper);
  for (const auto& n : win) {
    std::string arr;
    for (int c = 0; c < ncyc; ++c) {
      arr += (arr.empty() ? "" : ", ") + val_at(n, c);
    }
    test_text += std::format("  const _drv_{} = [{}]\n", n, arr);
  }
  if (implicit_reset) {
    std::string arr;
    for (int c = 0; c < ncyc; ++c) {
      arr += (arr.empty() ? "" : ", ") + val_at("reset", c);
    }
    test_text += std::format("  const _drv_reset = [{}]\n", arr);
  }
  test_text += std::format("  tick {} {{\n", ncyc);
  // Reset drive: an explicit `reset` PORT is driven by the input loop below (and
  // the sim unifies it with the implicit reset). Otherwise drive the implicit
  // reset — from the trace's `reset` values, or the reset-hold prologue length.
  if (!reset_is_port) {
    if (implicit_reset) {
      test_text += "    _lec_dut.reset = _drv_reset[clock]\n";
    } else if (r.trace.reset_cycles > 0) {
      test_text += std::format("    _lec_dut.reset = clock < {}\n", r.trace.reset_cycles);
    } else {
      test_text += "    _lec_dut.reset = false\n";
    }
  }
  for (const auto& n : win) {
    test_text += std::format("    _lec_dut.{} = _drv_{}[clock]\n", n, n);
  }
  test_text += "    step\n  }\n}\n";

  // ---- assemble lecfail.prp -----------------------------------------------
  std::string divtxt;
  for (const auto& d : r.trace.diverge_outputs) {
    divtxt += (divtxt.empty() ? "" : ", ") + d;
  }
  // The reproduce/iterate command. The import form passes BOTH original sources
  // positionally so their `import("<stem>.<top>")` resolve to the co-loaded units
  // (edit either .prp, re-run, the fix flows through); the inline form is a single
  // self-contained file.
  const std::string rerun =
      can_import ? std::format("lhd sim {} {} {} --set sim.vcd=true --workdir <dir>", opts.impl_path, opts.ref_path,
                               prpfail_path)
                 : std::format("lhd sim {} --set sim.vcd=true --workdir <dir>", prpfail_path);
  std::string out = std::format(
      "/*\n:name: {}\n:type: simulation\n*/\n"
      "// AUTO-GENERATED by `lhd lec` from a REFUTED counterexample.\n"
      "// impl='{}'  ref='{}'\n"
      "// Drives BOTH designs with the failing input sequence ({} cycle(s), {} reset-hold).\n"
      "// Divergence at cycle {}: {}\n"
      "// Re-run:  {}   (dumps {}.vcd)\n\n",
      test_name, opts.impl_path, opts.ref_path, ncyc, r.trace.reset_cycles, r.trace.diverge_cycle,
      divtxt.empty() ? "(see verdict)" : divtxt, rerun, test_name);
  if (can_import) {
    // Reference the ORIGINAL sources by their `<file-stem>.<top>` import key.
    out += std::format("const implmod = import(\"{}.{}\")\n", impl_stem, impl_top);
    out += std::format("const refmod  = import(\"{}.{}\")\n\n", ref_stem, ref_top);
  } else {
    // Inline both re-emitted hierarchies (ref-side clashes already renamed).
    for (const auto& m : impl_mods) {
      out += lecfail_type_params(m.text, width_of);
      if (!out.empty() && out.back() != '\n') {
        out += '\n';
      }
      out += '\n';
    }
    for (const auto& m : ref_mods) {
      out += lecfail_type_params(m.text, width_of);
      if (!out.empty() && out.back() != '\n') {
        out += '\n';
      }
      out += '\n';
    }
  }
  out += wrap_text + "\n" + test_text;

  std::ofstream ofs(prpfail_path);
  if (!ofs.is_open()) {
    skip(std::format("could not write {}", prpfail_path));
    return;
  }
  ofs << out;
  ofs.close();
  res.outputs.push_back(prpfail_path);
  res.recipe_steps.push_back(std::format("lec.prpfail witness testbench -> {}", prpfail_path));
  std::print("lec: wrote counterexample testbench {}\n", prpfail_path);

  if (!run_sim) {
    return;
  }
  // Phase 3/3 of the lec-on-failure flow: `lhd sim` on the testbench dumps the
  // waveform; announce the target up front (the sim run is the slow part).
  livehd::diag::info("pass.lec", "lecfail-creating-vcd", "progress")
      .msg("lec: creating counterexample waveform {}/{}.vcd", opts.workdir, test_name)
      .emit();
  // Run it: one instance -> one VCD at <workdir>/<test_name>.vcd.
  const std::string sim_log = next_log_path(opts, "lec.prpfailrun");
  std::string       cmd     = shell_quote(lhd_bin) + " sim ";
  // Import form: pass both original sources positionally so the testbench's
  // `import("<stem>.<top>")` resolve to the co-loaded units.
  if (can_import) {
    cmd += shell_quote(opts.impl_path) + " " + shell_quote(opts.ref_path) + " ";
  }
  cmd += shell_quote(prpfail_path) + " --set sim.vcd=true --workdir " + shell_quote(opts.workdir);
  // Forward any explicit sim-runtime header locations (compile.cgen.sim_hlop /
  // sim_iassert) to the child sim host-compile — needed when `../hlop` isn't
  // beside the cwd (e.g. under `bazel test`, where the caller passes them).
  for (const auto& [k, v] : opts.sets) {
    if ((k == "compile.cgen.sim_hlop" || k == "compile.cgen.sim_iassert") && !v.empty()) {
      cmd += " --set " + shell_quote(k + "=" + v);
    }
  }
  cmd += " >> " + shell_quote(sim_log) + " 2>&1";
  int         st  = std::system(cmd.c_str());
  std::string vcd = std::format("{}/{}.vcd", opts.workdir, test_name);
  if (WIFEXITED(st) && WEXITSTATUS(st) == 0 && fs::exists(vcd)) {
    res.outputs.push_back(vcd);
    res.recipe_steps.push_back(std::format("lec.prpfailrun VCD -> {}", vcd));
    std::print("lec: wrote counterexample waveform {}\n", vcd);
  } else {
    livehd::diag::warn("pass.lec", "lecfail-sim", "io")
        .msg("lec.prpfailrun: `lhd sim {}` did not produce {} (see {})", prpfail_path, vcd, sim_log)
        .emit();
  }
}

void lec_command(Options& opts, Result& res) {
  // Whether the USER passed --workdir (captured before load_side_graphs' first
  // workdir() call fabricates a scratch temp dir): the lecfail witness testbench
  // + VCD are on-by-default only for a persistent, user-named --workdir.
  const bool workdir_set = !opts.workdir.empty();
  setup_diag(opts, "lec");
#ifndef NDEBUG
  // NDEBUG is only defined under `-c opt`; a dbg/fastbuild binary runs the SMT
  // discharge far slower, so nudge the user toward an optimized build first.
  livehd::diag::info("pass.lec", "lec-debug-build-slow", "progress")
      .msg("lec is slow and you compile without optimizations. Maybe `bazel build -c opt //...`")
      .emit();
#endif
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
      // Fall back to the SIMPLE (post-'.') module name. Internal graph names are
      // the hierarchical `file.entity`, but Verilog/flat-name tooling (and the
      // v2prp harness `:pyrope_top:`) names the module by its flat entity
      // (`counter`, not `mod_counter.counter`). Accept `t` as either the full name
      // or the entity, matching on the entity of both — only when unambiguous.
      auto entity = [](std::string_view n) {
        auto d = n.rfind('.');
        return d == std::string_view::npos ? n : n.substr(d + 1);
      };
      const std::string_view           want_entity = entity(t);
      std::shared_ptr<hhds::Graph>      simple_hit;
      int                              n_simple = 0;
      for (auto& g : v.graphs) {
        if (g && entity(g->get_name()) == want_entity) {
          simple_hit = g;
          ++n_simple;
        }
      }
      if (n_simple == 1) {
        return simple_hit;
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
  o.engine  = label("engine", "auto");
  o.solver  = solver;  // cvc5 | bitwuzla
  o.gold_x  = label("gold_x", "ignore");
  o.bound   = std::atoi(label("bound", "6").c_str());
  o.timeout = std::atoi(label("timeout", "120").c_str());  // bound the CLI: hard miters degrade to UNKNOWN, never freeze (0 = unbounded)
  o.witness = label("witness", "true") != "false" && label("witness", "true") != "0";
  o.decompose    = label("decompose", "auto");
  o.strict       = label("strict", "false") != "false" && label("strict", "false") != "0";
  o.semdiff      = livehd::lec::lec_canon_semdiff(label("semdiff", "structural"));
  o.phase        = label("phase", "after_reset");
  o.reset_cycles = std::atoi(label("reset_cycles", "2").c_str());
  o.reset        = label("reset", "");

  // lec.match: explicit register correspondence, inline or @FILE.
  if (std::string match_spec = label("match", ""); !match_spec.empty()) {
    std::string text = match_spec;
    if (match_spec.front() == '@') {
      std::string path = match_spec.substr(1);
      if (!fs::is_regular_file(path)) {
        throw Lhd_error{"missing_file", std::format("lec.match file not found: {}", path), ""};
      }
      std::ifstream     f(path);
      std::stringstream ss;
      ss << f.rdbuf();
      text = ss.str();
    }
    o.match = livehd::lec::parse_match_pairs(text);
  }

  // lec.collapse: proven module defs to force-blackbox. Union of the --collapse
  // flags and a comma-separated `--set lec.collapse=a,b,c`.
  o.collapse = opts.collapse;
  if (std::string cs = label("collapse", ""); !cs.empty()) {
    size_t pos = 0;
    while (pos < cs.size()) {
      size_t c   = cs.find(',', pos);
      size_t end = c == std::string::npos ? cs.size() : c;
      if (end > pos) {
        o.collapse.emplace_back(cs.substr(pos, end - pos));
      }
      pos = end + 1;
    }
  }

  if (auto e = livehd::lec::lec_options_range_error(o); !e.empty()) {
    throw Lhd_error{"usage", e, "the BMC engine unrolls one SMT copy of the design per cycle"};
  }

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

  // Phase 1/3 of the lec-on-failure flow (detect -> testbench -> waveform):
  // announce the (possibly long, quiet) SMT detection up front so a slow solve is
  // legible instead of looking like a hang.
  livehd::diag::info("pass.lec", "lec-detecting", "progress")
      .msg("lec: detecting equivalence of '{}' vs '{}' (engine={}, solver={}, {})", impl_g->get_name(),
           ref_g->get_name(), o.engine, o.solver,
           o.timeout > 0 ? std::format("timeout={}s", o.timeout) : std::string{"no timeout"})
      .emit();

  livehd::lec::Query_result r;
  if (label("hierarchical", "true") != "false" && label("hierarchical", "true") != "0") {
    // Bottom-up: LEC every def leaves-first under `auto`, collapsing proven
    // children. The driver emits a per-def progress line itself; the TOP def's
    // verdict drives the exit policy below (like the single-design path).
    r = lec_hierarchical(res, ref_var, impl_var, std::string(ref_g->get_name()), ref_g.get(), impl_g.get(), o, sub_lib_ptr);
  } else {
    res.recipe_steps.emplace_back(std::format("pass.lec engine:{} solver:{} phase:{}", o.engine, o.solver, o.phase));
    auto t0 = std::chrono::steady_clock::now();
    r       = livehd::lec::prove_equal(ref_g.get(), impl_g.get(), o, sub_lib_ptr);
    if (r.verdict == livehd::lec::Verdict::Refuted && !o.collapse.empty()) {
      // Same abstraction rule as the hierarchical driver: a REFUTE under a
      // manual --collapse can be an artifact of the box over-approximation, so
      // confirm FLAT before letting the exit policy report a fail.
      std::print("lec: '{}' REFUTED under collapse ({} box def(s)) -> flat confirmation\n", impl_g->get_name(),
                 o.collapse.size());
      livehd::lec::Lec_options oflat = o;
      oflat.collapse.clear();
      auto rf   = livehd::lec::prove_equal(ref_g.get(), impl_g.get(), oflat, sub_lib_ptr);
      rf.detail = "flat-confirm after collapsed-box REFUTE" + std::string(rf.detail.empty() ? "" : "; ") + rf.detail
                + (r.detail.empty() ? "" : " (collapsed run: " + r.detail + ")");
      rf.elapsed_ms = -1;  // the progress record carries the combined wall-clock below
      r = std::move(rf);
    }
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    // Per-block progress (info severity): stream the verdict the moment it resolves.
    emit_lec_block_progress(impl_g->get_name(), r, o, ms);
  }
  bool lec_equiv = r.verdict == livehd::lec::Verdict::Proven;
  bool lec_known = r.verdict != livehd::lec::Verdict::Unknown;

  const char* verdict = lec_known ? (lec_equiv ? "PROVEN equivalent" : "REFUTED (not equivalent)") : "UNKNOWN";
  std::print("lec: '{}' {} ({})\n", impl_g->get_name(), verdict, r.detail);
  // The witness names the diverging COMMON outputs; print it on Refuted AND on the
  // Unknown-because-incomplete-correspondence case (where a matched-portion diff is
  // still the actionable iteration signal), not only on a clean Refuted.
  if (!r.witness.empty()) {
    std::print("  counterexample: {}\n", r.witness);
  }

  // lecfail witness testbench + VCD (`lhd lec` + --workdir). On a REFUTED verdict,
  // write a self-contained Pyrope reproduction (lec.prpfail) and optionally run it
  // to dump a VCD (lec.prpfailrun). Resolved with workdir-aware defaults: without
  // a user --workdir both are off; with one they default on. Gated by lec.witness.
  if (r.verdict == livehd::lec::Verdict::Refuted) {
    auto get_set = [&](std::string_view k, std::string& v) {
      auto it = labels.find(std::string{k});
      if (it == labels.end()) {
        return false;
      }
      v = it->second;
      return true;
    };
    std::string prpfail;  // "" = off; else the .prp basename under --workdir
    std::string pv;
    if (o.witness && workdir_set) {
      if (get_set("prpfail", pv)) {
        prpfail = (pv.empty() || pv == "false" || pv == "0") ? std::string{}
                  : (pv == "true" || pv == "1")              ? std::string{"lecfail.prp"}
                                                             : pv;
      } else {
        prpfail = "lecfail.prp";  // default when --workdir is set
      }
    } else if (get_set("prpfail", pv) && !workdir_set && !pv.empty() && pv != "false" && pv != "0") {
      livehd::diag::info("pass.lec", "lecfail-needs-workdir", "io")
          .msg("lec.prpfail needs --workdir (a persistent output dir); skipping the witness testbench")
          .emit();
    }
    bool prpfailrun = workdir_set;  // default: run iff --workdir
    if (std::string rv; get_set("prpfailrun", rv)) {
      prpfailrun = !(rv == "false" || rv == "0" || rv.empty());
    }
    if (!prpfail.empty()) {
      emit_lecfail_witness(opts, res, r, std::string(impl_g->get_name()), std::string(ref_g->get_name()), prpfail,
                           prpfailrun);
    }
  }

  if (!cross) {
    if (r.verdict == livehd::lec::Verdict::Refuted) {
      throw Lhd_error{"equiv_fail",
                      std::format("'{}' is not equivalent ({} vs {})", impl_g->get_name(), opts.impl_path, opts.ref_path),
                      r.witness.empty() ? "" : std::format("counterexample: {}", r.witness)};
    }
    if (r.verdict == livehd::lec::Verdict::Unknown) {
      // REFUTED above disproves equivalence (a real counterexample → hard fail).
      // UNKNOWN is the solver giving up: it found NO counterexample but could not
      // complete the proof. Per the deferred-warning policy (disproved ⇒ error;
      // could-not-prove ⇒ warning) this is NOT a hard failure — UNLESS `lec.strict`
      // is set, or the partial (incomplete-correspondence) miter actually surfaced a
      // diff (a non-empty witness, which is a potential discrepancy). Otherwise emit a
      // loud inconclusive warning and exit cleanly: an UNKNOWN proves nothing, but it
      // also disproves nothing, so it must not be conflated with REFUTED.
      if (o.strict || !r.witness.empty()) {
        throw Lhd_error{"unsupported", std::format("lec could not decide equivalence of '{}'", impl_g->get_name()),
                        r.witness.empty() ? r.detail : std::format("{}; witness: {}", r.detail, r.witness)};
      }
      livehd::diag::warn("pass.lec", "inconclusive", "io")
          .msg("lec INCONCLUSIVE: '{}' — the solver could not complete the proof and found NO counterexample ({}). "
               "This is NOT a proof of equivalence; pass --set lec.strict=true to treat it as a failure.",
               impl_g->get_name(),
               r.detail)
          .emit();
      return;  // clean exit: inconclusive (warning), not a hard error
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

// ---- semdiff (structural diff/match: stamp the `match` attribute) -----------

// `lhd pass semdiff --ref lg:A --impl lg:B [--top m]`: structural LEC. Mirrors
// lec_command (two sides, per-side top pick) but instead of an SMT proof it
// calls semdiff::structural_match to stamp corresponding nodes/pins of both
// libraries with a shared `match` id (0 = no counterpart), then saves both lg:
// back in place — so the diff is greppable (`lhd tool grep match=0 lg:B`) and
// visualizable (`lhd tool diff lg:A lg:B --match`). v1 marks lg: libraries in
// place, so both sides must be lg: (compile sources to lg: first); the
// structural_match API itself is kind-agnostic (pass.semdiff / a future lec).
// Reached only through pass_command (`lhd pass semdiff`), not a top-level word.
void semdiff_command(Options& opts, Result& res) {
  setup_diag(opts, "semdiff");
  if (opts.impl_path.empty() || opts.ref_path.empty()) {
    throw Lhd_error{"usage", "pass semdiff requires --ref lg:DIR and --impl lg:DIR", "e.g. `lhd pass semdiff --ref lg:gold --impl lg:opt --top adder`"};
  }
  if (opts.ref_kind != "lg" || opts.impl_kind != "lg") {
    throw Lhd_error{"usage",
                    "v1 semdiff marks lg: libraries in place, so both --ref and --impl must be lg:DIR",
                    "compile sources to lg: first (e.g. `lhd compile a.v --emit-dir lg:gold`)"};
  }
  if (fs::weakly_canonical(opts.ref_path) == fs::weakly_canonical(opts.impl_path)) {
    throw Lhd_error{"usage", "pass semdiff --ref and --impl must be different lg: libraries", ""};
  }

  // Each side loads from its OWN library instance, so the two graphs keep
  // independent gids and attr stores (the cross-library trap) — hold the two
  // Graph* directly.
  Eprp_var ref_var;
  Eprp_var impl_var;
  load_side_graphs(opts, res, opts.ref_kind, opts.ref_path, "ref", ref_var);
  load_side_graphs(opts, res, opts.impl_kind, opts.impl_path, "impl", impl_var);

  // Pick the top module on each side (same rule as lec): explicit
  // --{ref,impl}-top, else --top, else the sole module.
  auto pick = [&](Eprp_var& v, const std::string& want, std::string_view side) -> std::shared_ptr<hhds::Graph> {
    const std::string& t = !want.empty() ? want : opts.top;
    if (!t.empty()) {
      for (auto& g : v.graphs) {
        if (g && g->get_name() == t) {
          return g;
        }
      }
      throw Lhd_error{"config", std::format("pass semdiff: {} top '{}' not found", side, t), ""};
    }
    if (v.graphs.size() == 1) {
      return v.graphs.front();
    }
    throw Lhd_error{"usage", std::format("pass semdiff: {} has {} modules; pass --{}-top or --top", side, v.graphs.size(), side), ""};
  };
  auto ref_g  = pick(ref_var, opts.ref_top, "ref");
  auto impl_g = pick(impl_var, opts.impl_top, "impl");

  Eprp_var::Eprp_dict labels;
  merge_sets(opts, "pass.semdiff", labels);
  auto label = [&](std::string_view k, std::string_view def) -> std::string {
    auto it = labels.find(std::string{k});
    return it == labels.end() ? std::string{def} : it->second;
  };
  livehd::semdiff::Semdiff_options o;
  o.alg            = label("alg", "structural");
  o.matching_names = label("matching_names", "false") != "false" && label("matching_names", "false") != "0";
  o.id_granularity = label("id_granularity", "pair");
  o.verbose        = false;  // the command prints its own summary below
  if (o.id_granularity != "pair" && o.id_granularity != "region") {
    throw Lhd_error{"usage", std::format("--set pass.semdiff.id_granularity expects pair|region, got '{}'", o.id_granularity), ""};
  }

  res.recipe_steps.emplace_back(std::format("pass.semdiff alg:{} matching_names:{} id_granularity:{}", o.alg, o.matching_names, o.id_granularity));
  auto r = livehd::semdiff::structural_match(ref_g.get(), impl_g.get(), o);

  if (!opts.quiet) {
    std::print("semdiff: ref '{}' {}/{} matched, impl '{}' {}/{} matched, {} regions, similarity {:.3f}\n",
               ref_g->get_name(),
               r.a_matched,
               r.a_matched + r.a_unmatched,
               impl_g->get_name(),
               r.b_matched,
               r.b_matched + r.b_unmatched,
               r.regions,
               r.similarity);
    std::print("  inspect: `lhd tool diff lg:{} lg:{} --match`  |  `lhd tool grep match=0 lg:{}`\n",
               opts.ref_path,
               opts.impl_path,
               opts.impl_path);
  }

  // v1 persistence: mark-in-place. Save both libraries back so the `match`
  // attribute survives for the greppable / visualizable workflow.
  livehd::Hhds_graph_library::save(opts.ref_path);
  livehd::Hhds_graph_library::save(opts.impl_path);
  res.outputs.push_back(opts.ref_path);
  res.outputs.push_back(opts.impl_path);
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
                    "pass requires a subcommand: color <alg> | partition | abc | liberty gensim | semdiff",
                    "e.g. `lhd pass color acyclic --top m lg:dir` or `lhd pass abc --top m lg:dir --emit-dir lg:net`"};
  }
  const std::string sub = opts.files[0];

  // `pass semdiff --ref lg:A --impl lg:B`: the structural diff/match pass takes
  // two lg: libraries via --ref/--impl (not a positional lg: input) and marks
  // the `match` attribute on both in place — so handle it before the single
  // lg:-input requirement below. semdiff_command holds the side-loading logic
  // (it mirrors lec_command), just like `pass liberty` is handled specially.
  if (sub == "semdiff") {
    semdiff_command(opts, res);
    return;
  }

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
  check_ir_body_magic(lg_in, "graph_", kHhdsGraphBodyMagic, "lg:");
  res.inputs.push_back(lg_in);

  if (sub == "color") {
    std::string alg = opts.files.size() > 1 ? opts.files[1] : std::string{"acyclic"};
    Eprp_var    var;
    load_lg_into_var(lg_in, var);
    if (var.graphs.empty()) {
      throw Lhd_error{"config", std::format("lg: input {} holds no graphs", lg_in), ""};
    }
    Eprp_var::Eprp_dict labels;
    labels["alg"]  = alg;
    labels["seed"] = opts.seed;  // the shared `lhd.seed` (mincut RNG); no per-pass seed option
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
  } else if (sub == "formal") {
    Eprp_var var;
    load_lg_into_var(lg_in, var);
    if (var.graphs.empty()) {
      throw Lhd_error{"config", std::format("lg: input {} holds no graphs", lg_in), ""};
    }
    Eprp_var::Eprp_dict labels;
    if (!opts.top.empty()) {
      labels["top"] = opts.top;
    }
    merge_sets(opts, "pass.formal", labels);
    run_step("pass.formal", var, labels, opts, res);  // marks proven/runtime_check in place; errors on a real violation
  } else {
    throw Lhd_error{"usage", std::format("unknown pass subcommand '{}'", sub),
                    "use: color <alg> | partition | abc | formal | liberty gensim | semdiff"};
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
  return f == "id" || f == "nid" || f == "color" || f == "bits" || f == "delay" || f == "hier_color" || f == "match";
}

// The columns a `<field><sep><value>` filter may target. A token whose head is
// not one of these is treated as a bare match-everything term instead, so a
// value that merely contains ':'/'=' (e.g. a src path `x.prp:5`) is not
// mis-split into a bogus field.
bool tool_is_known_field(std::string_view f) {
  return f == "nid" || f == "id" || f == "kind" || f == "name" || f == "color" || f == "src"
         || f == "partitionable" || f == "bits" || f == "signed" || f == "from" || f == "to" || f == "delay"
         || f == "hier_color" || f == "match";
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

// The pass/semdiff structural-correspondence id. "nil" until semdiff has run
// (the attribute is absent); 0 is a real value meaning "no counterpart".
std::string tool_match_str(const hhds::Node_class& node) {
  namespace gu = livehd::graph_util;
  return gu::has_match(node) ? std::to_string(gu::match_of(node)) : std::string{"nil"};
}
std::string tool_match_str(const hhds::Pin_class& pin) {
  namespace gu = livehd::graph_util;
  return gu::has_match(pin) ? std::to_string(gu::match_of(pin)) : std::string{"nil"};
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
  r.cols.emplace_back("match", tool_match_str(node));
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
    r.cols.emplace_back("match", tool_match_str(pin));
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
    case Tool_target::node: return {"color", "match", "src"};
    case Tool_target::pin: return {"bits", "signed", "match"};
    case Tool_target::edge: return {"bits"};
    default: return {"color", "match", "src", "bits", "signed"};  // target=all flat (grep)
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
      // A sink pin has no width of its own — its width is the DRIVER's (`bits` is a
      // driver-pin property; see graph/node_util.hpp set_bits). Read the driver.
      int32_t b = gu::bits_of(e.driver);
      if (!take(std::format("    .{}  bits={}{}  <- {}",
                            tool_pin_label(e.sink),
                            b != 0 ? std::to_string(b) : std::string{"nil"},
                            gu::is_unsign(e.driver) ? "" : " signed",
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
        if (tool_match_all(r, filters) == opts.tool_invert) {  // -v keeps the non-matching records
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

// One node as seen by the match-aware diff: its correspondence id + a label.
struct Match_node {
  uint32_t    id;
  std::string line;  // "<kind>  <ident>  src=…"
};

std::vector<Match_node> tool_match_nodes(hhds::Graph* g) {
  namespace gu = livehd::graph_util;
  std::vector<Match_node> v;
  for (auto node : g->forward_class()) {
    std::string src  = tool_node_src(g, node);
    std::string line = std::format("{:<10}  {}", Ntype::get_name(gu::type_op_of(node)), gu::debug_name(node));
    if (src != "nil") {
      line += std::format("  src={}", src);
    }
    v.push_back({gu::match_of(node), std::move(line)});
  }
  return v;
}

// `tool diff lg:A lg:B --match` — semantic diff driven by the semdiff `match`
// attribute: matched regions (shared id) are the common part (summarized);
// unmatched nodes (match=0) are the actual differences, printed `-` for ref-only
// and `+` for impl-only. Falls back with a hint when neither side was marked.
void tool_diff_match_lg(Options& opts, const std::vector<std::string>& lg_dirs) {
  auto   ga    = tool_select_graphs(lg_dirs[0], opts);
  auto   gb    = tool_select_graphs(lg_dirs[1], opts);
  size_t pairs = std::min(ga.size(), gb.size());

  bool saw_match = false;
  for (size_t i = 0; i < pairs; ++i) {
    if (ga[i]->has_attr(livehd::attrs::match)) {  // a side was marked by semdiff
      saw_match = true;
    }
  }
  if (!saw_match) {
    std::string hint =
        "-- no `match` attribute found; run `lhd pass semdiff --ref lg:… --impl lg:…` first to mark correspondences, "
        "then `lhd tool diff … --match`\n";
    std::fwrite(hint.data(), 1, hint.size(), stdout);
    std::fflush(stdout);
    return;
  }

  std::string out;
  for (size_t i = 0; i < pairs; ++i) {
    hhds::Graph* a = ga[i].get();
    hhds::Graph* b = gb[i].get();
    auto         ra = tool_match_nodes(a);
    auto         rb = tool_match_nodes(b);

    // Matched ids on each side (id != 0); a node is part of the common region.
    std::set<uint32_t> ids_a;
    std::set<uint32_t> ids_b;
    uint32_t           ma = 0;
    uint32_t           mb = 0;
    for (const auto& r : ra) {
      if (r.id != 0) {
        ids_a.insert(r.id);
        ++ma;
      }
    }
    for (const auto& r : rb) {
      if (r.id != 0) {
        ids_b.insert(r.id);
        ++mb;
      }
    }
    size_t shared = 0;
    for (uint32_t id : ids_a) {
      if (ids_b.contains(id)) {
        ++shared;
      }
    }

    out += std::format("//---- tool diff (match) {} vs {}\n", a->get_name(), b->get_name());
    out += std::format("  matched: {} regions ({} ref nodes, {} impl nodes)\n", shared, ma, mb);
    for (const auto& r : ra) {
      if (r.id == 0) {
        out += std::format("  - {}\n", r.line);
      }
    }
    for (const auto& r : rb) {
      if (r.id == 0) {
        out += std::format("  + {}\n", r.line);
      }
    }
    uint32_t ta = static_cast<uint32_t>(ra.size());
    uint32_t tb = static_cast<uint32_t>(rb.size());
    uint32_t tot = ta + tb;
    double   sim = tot == 0 ? 1.0 : static_cast<double>(ma + mb) / static_cast<double>(tot);
    out += std::format("  {}/{} ref matched, {}/{} impl matched, similarity {:.3f}\n", ma, ta, mb, tb, sim);
  }
  std::fwrite(out.data(), 1, out.size(), stdout);
  std::fflush(stdout);
}

void tool_diff_lg(Options& opts, const std::vector<std::string>& lg_dirs, const std::vector<Tool_filter>& filters) {
  if (lg_dirs.size() != 2) {
    throw Lhd_error{"usage", "tool diff takes exactly two lg: inputs", "e.g. `lhd tool diff lg:before lg:after --attr color`"};
  }
  if (opts.tool_match) {  // semdiff `match`-attribute visualization
    tool_diff_match_lg(opts, lg_dirs);
    return;
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

// ── `tool tree ln:` — the LNAST counterpart of the lg instance tree ─────────
// `tool cat ln:` already dumps every node; `tool tree ln:` is the SUMMARY: only
// the scope / control-flow / call / def nodes that give the tree its shape, each
// annotated with the total LNAST node count of its subtree (the "[N nodes]" stat,
// like the lg tree). Everything else (declares, stores, operators, refs, consts,
// types) is collapsed. `--target kind:<verbal>` adds extra node kinds; `--hier N`
// limits depth; `--max N` caps rows. Rendered with box-drawing connectors.
struct Ln_tree_row {
  std::string              label;
  std::vector<Ln_tree_row> children;
};

bool tool_tree_ln_skeleton(Lnast_ntype::Lnast_ntype_int t) {
  using L = Lnast_ntype;
  return L::is_top(t) || L::is_stmts(t) || L::is_if(t) || L::is_unique_if(t) || L::is_for(t) || L::is_while(t)
         || L::is_func_def(t) || L::is_func_call(t) || L::is_io(t);
}

// `--target kind:<X>` for the ln tree: X names an Lnast verbal (store, declare,
// if, …), matched exactly. Additive — the skeleton set is always shown too.
bool tool_tree_ln_kind_match(Lnast_ntype::Lnast_ntype_int t, const std::vector<std::string>& kinds) {
  if (kinds.empty()) {
    return false;
  }
  std::string_view name = Lnast_ntype::to_sv(t);
  for (const auto& k : kinds) {
    if (k == name) {
      return true;
    }
  }
  return false;
}

// fcall/fdef carry their identity in the leading ref/const children (fcall:
// instance, callee, arg-stores…; fdef: name, …). Surface them the way the lg
// tree shows `instance : Module`. Other nodes append their own name if any.
std::string tool_tree_ln_label(const Lnast& ln, const Lnast_nid& nid, Lnast_ntype::Lnast_ntype_int type, size_t total) {
  std::string head{Lnast_ntype::to_sv(type)};
  if (Lnast_ntype::is_func_call(type) || Lnast_ntype::is_func_def(type)) {
    std::vector<std::string_view> names;
    for (auto c = ln.get_first_child(nid); !c.is_invalid() && names.size() < 2; c = ln.get_sibling_next(c)) {
      auto ct = ln.get_type(c);
      if (!Lnast_ntype::is_ref(ct) && !Lnast_ntype::is_const(ct)) {
        break;  // leading name run ended
      }
      names.push_back(ln.get_name(c));
    }
    if (names.size() >= 2) {
      head += std::format(" {} : {}", names[0], names[1]);
    } else if (names.size() == 1) {
      head += std::format(" {}", names[0]);
    }
  } else if (auto n = ln.get_name(nid); !n.empty()) {
    head += std::format(" {}", n);
  }
  return std::format("{}  [{} nodes]", head, total);
}

// Build the skeleton for the subtree at `nid` into `sink` (each shown node
// attaches to its nearest shown ancestor). Returns the total LNAST node count of
// the subtree — ALL nodes, shown or collapsed — so a scope's count reflects
// everything it holds. `sink == nullptr` ⇒ count only (over depth / suppressed).
size_t tool_tree_ln_collect(const Lnast& ln, const Lnast_nid& nid, int depth, int maxdepth,
                            const std::vector<std::string>& kinds, std::vector<Ln_tree_row>* sink) {
  auto type  = ln.get_type(nid);
  bool shown = tool_tree_ln_skeleton(type) || tool_tree_ln_kind_match(type, kinds);

  std::vector<Ln_tree_row>  kids;
  std::vector<Ln_tree_row>* child_sink = sink;  // non-shown nodes are transparent (pass through)
  bool                      emit       = false;
  if (shown) {
    if (sink != nullptr && depth < maxdepth) {
      emit       = true;
      child_sink = &kids;
    } else {
      child_sink = nullptr;  // over depth (or already suppressed): count only
    }
  }
  int child_depth = shown ? depth + 1 : depth;  // only shown nodes consume a level

  size_t total = 1;
  for (auto c = ln.get_first_child(nid); !c.is_invalid(); c = ln.get_sibling_next(c)) {
    total += tool_tree_ln_collect(ln, c, child_depth, maxdepth, kinds, child_sink);
  }
  if (emit) {
    sink->push_back(Ln_tree_row{tool_tree_ln_label(ln, nid, type, total), std::move(kids)});
  }
  return total;
}

void tool_tree_ln_print(const Ln_tree_row& row, const std::string& prefix, bool last, std::string& out, size_t& budget,
                        bool& truncated) {
  if (budget == 0) {
    truncated = true;
    return;
  }
  out += prefix;
  out += last ? "└── " : "├── ";
  out += row.label;
  out += '\n';
  --budget;
  std::string child_prefix = prefix + (last ? "    " : "│   ");
  for (size_t i = 0; i < row.children.size(); ++i) {
    tool_tree_ln_print(row.children[i], child_prefix, i + 1 == row.children.size(), out, budget, truncated);
    if (truncated) {
      return;
    }
  }
}

void tool_tree_ln(Options& opts, Result& res, const std::vector<std::string>& ln_tokens) {
  auto in = classify_ln_inputs(ln_tokens, "tool tree");
  for (const auto& d : opts.in_dirs) {  // --in-dir ln:DIR spelling
    if (d.kind == "ln") {
      in.ln_dirs.push_back(d.path);
    }
  }
  auto units = sorted_by_name(filter_top(ln_tool_units(opts, res, in), opts.top));
  if (units.empty()) {
    throw Lhd_error{"config", "ln: input holds no matching units", "check --top"};
  }

  int         maxdepth  = opts.tool_hier < 0 ? std::numeric_limits<int>::max() : opts.tool_hier;  // tree: full by default
  size_t      budget    = tool_budget(opts);
  bool        truncated = false;
  std::string out;
  for (const auto& ln : units) {
    // The unit's root (top/stmts/func_def) is the header; its children are the
    // box-tree body — so the count beside the header is the whole-unit total.
    std::vector<Ln_tree_row> body;
    size_t                   total = 1;  // the root node itself
    for (auto c = ln->get_first_child(ln->get_root()); !c.is_invalid(); c = ln->get_sibling_next(c)) {
      total += tool_tree_ln_collect(*ln, c, 0, maxdepth, opts.tool_kinds, &body);
    }
    std::string suffix{ln->get_lambda_kind()};
    if (ln->is_verilog_origin()) {
      suffix += suffix.empty() ? "verilog" : ", verilog";
    }
    out += std::format("{}  [{} nodes]{}\n",
                       ln->get_top_module_name(),
                       total,
                       suffix.empty() ? std::string{} : std::format("  ({})", suffix));
    for (size_t i = 0; i < body.size(); ++i) {
      tool_tree_ln_print(body[i], "", i + 1 == body.size(), out, budget, truncated);
      if (truncated) {
        break;
      }
    }
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
  for (const auto& d : lg_dirs) {  // reject a corrupt/non-GraphLibrary dir cleanly before the hhds loader asserts
    check_lg_input_dir(d);
  }

  if (verb == "tree") {
    if (have_ln) {
      tool_tree_ln(opts, res, ln_tokens);  // LNAST structural skeleton
      return;
    }
    if (!have_lg) {
      throw Lhd_error{"usage",
                      "tool tree needs an lg: or ln:/source input",
                      "e.g. `lhd tool tree lg:dir --top m` or `lhd tool tree ln:dir`"};
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
  // `<channel>.log=<level>` is the developer-logging namespace (livehd::log),
  // orthogonal to the pass-flag registry — a channel name (e.g. `upass`,
  // `cprop`) is NOT a pass name, so it must never collect a command-path
  // prefix. Leave any `.log` key verbatim for apply_log_settings.
  if (key.size() > 4 && key.substr(key.size() - 4) == ".log") {
    return std::string{key};
  }
  // The `sim.*` command namespace (sim_command, kSimSetOptions) is its own
  // vocabulary, distinct from the `compile.sim.*` cgen labels. Like `.log`, it
  // must never collect a command-path prefix — else `sim.vcd` would be rewritten
  // to the unrelated `compile.sim.vcd` under a `compile`/describe context.
  if (key.size() > 4 && key.substr(0, 4) == "sim.") {
    auto flag = key.substr(4);
    for (const auto& s : kSimSetOptions) {
      if (s.name == flag) {
        return std::string{key};
      }
    }
  }
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
  // The `lhd.*` kernel namespace: shared, cross-pass settings the kernel folds
  // into Options (apply_lhd_settings), not labels of any single EPRP method.
  // Keep in sync with check_known_set_passes / apply_lhd_settings.
  out.push_back(Set_option{"lhd.seed", "lhd", "0",
                           "shared RNG seed for every pass that wants determinism (e.g. pass.color mincut); one seed per run"});
  out.push_back(Set_option{"lhd.top", "lhd", "",
                           "top module shared across passes; the canonical form of the --top flag (the flag wins if both are given)"});
  // The `sim.*` command namespace (consumed by sim_command, not an EPRP method):
  // keep `lhd list options` / `lhd describe` complete. Single source of truth =
  // kSimSetOptions, which also drives check_known_set_passes / the sim --help block.
  for (const auto& s : kSimSetOptions) {
    out.push_back(Set_option{std::format("sim.{}", s.name), "sim", std::string{s.default_value}, std::string{s.help}});
  }
  std::sort(out.begin(), out.end(), [](const Set_option& a, const Set_option& b) { return a.name < b.name; });
  return out;
}

void run_engine_command(Options& opts, Result& res) {
  validate_emits(opts);
  validate_dumps(opts);
  check_known_set_passes(opts);  // --set AND --config table names: a typo'd pass must error, not no-op
  apply_log_settings(opts);      // --set <channel>.log=<level> -> livehd::log (developer logging)
  apply_lhd_settings(opts);      // --set lhd.seed / lhd.top -> the shared kernel Options fields

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
  } else if (opts.command == "sim") {
    sim_command(opts, res);
  } else {
    throw Lhd_error{"usage", std::format("unknown command '{}'", opts.command), ""};
  }
}

}  // namespace lhd
