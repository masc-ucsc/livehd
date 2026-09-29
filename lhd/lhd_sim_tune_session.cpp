//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lhd_sim_tune_session.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <map>
#include <set>

#include "diag.hpp"
#include "lhd_compile_cache.hpp"
#include "lhd_kernel_internal.hpp"
#include "lhd_sim_tune.hpp"
#include "lhd_tune.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace lhd {

namespace {

namespace rj = rapidjson;
using livehd::sim::Tune_vector;

bool is_bool_text(std::string_view v) { return v == "true" || v == "false" || v == "1" || v == "0" || v == "on" || v == "off"; }

// Record a file the generated code depends on (the --depfile / envelope inputs).
void add_input(Result& res, const std::string& path) {
  if (std::ranges::find(res.inputs, path) == res.inputs.end()) {
    res.inputs.push_back(path);
  }
}

// `sim.tune.file`: missing -> missing_file, unreadable format -> config. A
// file that was read is an input of the generated code.
std::optional<sim_tune::Tune_file> load_tune_file(const Options& opts, Result& res) {
  const auto path = sim_tune::last_set(opts.sets, "sim.tune.file");
  if (!path || path->empty()) {
    return std::nullopt;
  }
  const auto text = tune::read_file(*path);
  if (!text) {
    throw Lhd_error{"missing_file",
                    std::format("--set sim.tune.file={}: no such file", *path),
                    "write one with `lhd sim ... --workdir W --set sim.tune.export=FILE`"};
  }
  add_input(res, *path);
  std::string err;
  auto        f = sim_tune::parse_tune_file(*text, err);
  if (!f) {
    throw Lhd_error{"config", std::format("--set sim.tune.file={}: {}", *path, err), ""};
  }
  return f;
}

sim_tune::Pins explicit_pins(const Options& opts) {
  std::string err;
  auto        p = sim_tune::pins_from_sets(opts.sets, err);
  if (!err.empty()) {
    throw Lhd_error{"usage", std::format("--set {}", err), ""};
  }
  return p;
}

// The leading decimal of a raw run file name `<unix_ns>-<pid>.json`.
uint64_t name_ns(std::string_view name) {
  uint64_t   v = 0;
  const auto r = std::from_chars(name.data(), name.data() + name.size(), v);
  return r.ec == std::errc{} ? v : 0;
}

uint64_t unix_ns_now() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

void write_str(rj::Writer<rj::StringBuffer>& w, std::string_view k, std::string_view v) {
  w.Key(k.data(), static_cast<rj::SizeType>(k.size()));
  w.String(v.data(), static_cast<rj::SizeType>(v.size()));
}

std::string read_str(const rj::Value& o, const char* k) {
  const auto it = o.FindMember(k);
  return it != o.MemberEnd() && it->value.IsString() ? std::string{it->value.GetString(), it->value.GetStringLength()}
                                                     : std::string{};
}

bool read_bool(const rj::Value& o, const char* k) {
  const auto it = o.FindMember(k);
  return it != o.MemberEnd() && it->value.IsBool() && it->value.GetBool();
}

// ---- tree identity ------------------------------------------------------------------
//
// What design a generated sim tree simulates, as its drv.bin would report it:
// the structure keys baked into the tune-id TUs of the DUT classes the driver
// declares (the same '+'-joined string a raw run's `structure` is), plus the
// testbench digest (drv.cpp). Read from the tree's own files, so it never
// trusts a label that another setup could have left behind.
struct Tree_id {
  std::map<std::string, std::string> structures;  // DUT class -> baked structure key
  std::string                        structure;
  std::string                        tb;
  [[nodiscard]] bool                 known() const { return !structure.empty(); }
};

constexpr std::string_view kStructureSym = "__lhd_tune_structure_";

bool ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

// Every `__lhd_tune_structure_<cls>(` in `text`: (cls, offset past the '(').
std::vector<std::pair<std::string, size_t>> structure_syms(const std::string& text) {
  std::vector<std::pair<std::string, size_t>> out;
  for (size_t pos = text.find(kStructureSym); pos != std::string::npos; pos = text.find(kStructureSym, pos + 1)) {
    size_t e = pos + kStructureSym.size();
    while (e < text.size() && ident_char(text[e])) {
      ++e;
    }
    if (e < text.size() && text[e] == '(' && e > pos + kStructureSym.size()) {
      out.emplace_back(text.substr(pos + kStructureSym.size(), e - pos - kStructureSym.size()), e + 1);
    }
  }
  return out;
}

Tree_id read_tree_id(const std::string& dir) {
  Tree_id               id;
  std::set<std::string> classes;  // the driver's weak identity declarations name its DUT classes
  if (const auto drv = tune::read_file(dir + "/drv.cpp")) {
    id.tb = tune::hex16(tune::fnv1a64(*drv));
    for (const auto& [cls, at] : structure_syms(*drv)) {
      const size_t bol  = drv->rfind('\n', at);
      const size_t from = bol == std::string::npos ? 0 : bol + 1;
      if (std::string_view{*drv}.substr(from, at - from).find("[[gnu::weak]]") != std::string_view::npos) {
        classes.insert(cls);
      }
    }
  }
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->path().filename().string().ends_with(".tune-id.cpp")) {
      continue;
    }
    const auto text = tune::read_file(it->path().string());
    if (!text) {
      continue;
    }
    // `extern "C" const char* __lhd_tune_structure_<cls>() { return "<key>"; }`
    for (const auto& [cls, at] : structure_syms(*text)) {
      const size_t q = text->find("return \"", at);
      const size_t b = q == std::string::npos ? q : q + 8;
      const size_t e = b == std::string::npos ? b : text->find('"', b);
      if (e != std::string::npos && (classes.empty() || classes.contains(cls))) {
        id.structures[cls] = text->substr(b, e - b);
      }
    }
  }
  std::set<std::string> distinct;
  for (const auto& [cls, s] : id.structures) {
    if (!s.empty()) {
      distinct.insert(s);
    }
  }
  for (const auto& s : distinct) {
    id.structure += id.structure.empty() ? s : "+" + s;
  }
  return id;
}

void write_structures(rj::Writer<rj::StringBuffer>& w, const std::map<std::string, std::string>& structures) {
  w.Key("structures");
  w.StartObject();
  for (const auto& [cls, s] : structures) {
    write_str(w, cls, s);
  }
  w.EndObject();
}

std::map<std::string, std::string> read_structures(const rj::Value& o) {
  std::map<std::string, std::string> out;
  const auto                         it = o.FindMember("structures");
  if (it != o.MemberEnd() && it->value.IsObject()) {
    for (const auto& m : it->value.GetObject()) {
      if (m.value.IsString()) {
        out.emplace(std::string{m.name.GetString(), m.name.GetStringLength()},
                    std::string{m.value.GetString(), m.value.GetStringLength()});
      }
    }
  }
  return out;
}

// Does a raw run belong to a tree? Every root that reports a structure must
// match the tree's key for that class, and at least one must report one.
bool run_belongs(const sim_tune::Run& r, const std::map<std::string, std::string>& structures) {
  bool any = false;
  for (const auto& [module, s] : r.roots) {
    if (s.empty()) {
      continue;
    }
    const auto it = structures.find(module);
    if (it == structures.end() || it->second != s) {
      return false;
    }
    any = true;
  }
  return any;
}

// <simdir>/tune_applied.json: what the generated tree holds. It rides the tree
// (clone, swap), so it always describes the sim/ it sits in. `closed` marks a
// trial tree whose attempt ended without a swap back to the incumbent (no
// retained clone, or one of another design): a --run-only refuses to run it.
struct Applied_file {
  std::string vector;
  std::string source[4];
  bool        trial = false;
  std::string closed;  // the verdict that closed the trial this tree holds ("" = open or no trial)
  std::string closed_why;
  std::string scope;
  std::string structure;
  double      cgen_ms          = 0;
  bool        regen            = false;
  bool        unknown_zero_set = false;
  bool        unknown_zero     = false;
};

std::optional<Applied_file> read_applied(const std::string& simdir) {
  const auto text = tune::read_file(simdir + "/tune_applied.json");
  if (!text) {
    return std::nullopt;
  }
  rj::Document d;
  d.Parse(text->data(), text->size());
  if (d.HasParseError() || !d.IsObject() || read_str(d, "schema") != sim_tune::kAppliedSchema) {
    return std::nullopt;
  }
  Applied_file a;
  a.vector = read_str(d, "vector");
  if (const auto it = d.FindMember("source"); it != d.MemberEnd() && it->value.IsObject()) {
    int i = 0;
    for (const char* k : {"dirty", "fence", "live_words", "backend"}) {
      a.source[i++] = read_str(it->value, k);
    }
  }
  a.trial      = read_bool(d, "trial");
  a.closed     = read_str(d, "closed");
  a.closed_why = read_str(d, "closed_why");
  a.scope      = read_str(d, "scope");
  a.structure  = read_str(d, "structure");
  if (const auto it = d.FindMember("cgen_ms"); it != d.MemberEnd() && it->value.IsNumber()) {
    a.cgen_ms = it->value.GetDouble();
  }
  a.regen            = read_bool(d, "regen");
  a.unknown_zero_set = read_bool(d, "unknown_zero_set");
  a.unknown_zero     = read_bool(d, "unknown_zero");
  return a;
}

// Rewrite <simdir>/tune_applied.json in place when `pick` accepts it (every
// field the edit does not touch rides along). False when there is no valid
// label, `pick` declined it, or the write failed.
template <typename Pick, typename Edit>
bool edit_applied(const std::string& simdir, Pick&& pick, Edit&& edit) {
  const auto path = simdir + "/tune_applied.json";
  const auto text = tune::read_file(path);
  if (!text) {
    return false;
  }
  rj::Document d;
  d.Parse(text->data(), text->size());
  if (d.HasParseError() || !d.IsObject() || read_str(d, "schema") != sim_tune::kAppliedSchema || !pick(d)) {
    return false;
  }
  edit(d);
  rj::StringBuffer             sb;
  rj::Writer<rj::StringBuffer> w(sb);
  d.Accept(w);
  std::string why;
  return tune::write_file_atomic(path, std::string{sb.GetString()} + "\n", why);
}

void set_member(rj::Document& d, const char* k, rj::Value v) {
  if (const auto it = d.FindMember(k); it != d.MemberEnd()) {
    it->value = std::move(v);
  } else {
    d.AddMember(rj::Value(k, d.GetAllocator()), std::move(v), d.GetAllocator());
  }
}

// <workdir>/sim_tune/retained.json: the identity of the retained incumbent clone.
struct Retained {
  std::string                        tv;     // the vector the clone holds
  std::string                        scope;  // the scope the clone's own label names
  std::string                        structure;
  std::map<std::string, std::string> structures;
  std::string                        tb;
  std::string                        gen;  // digest of the clone's gen_digests.json (informational)
};

std::optional<Retained> read_retained_file(const std::string& path) {
  const auto text = tune::read_file(path);
  if (!text) {
    return std::nullopt;
  }
  rj::Document d;
  d.Parse(text->data(), text->size());
  if (d.HasParseError() || !d.IsObject() || read_str(d, "schema") != sim_tune::kRetainedSchema) {
    return std::nullopt;
  }
  Retained r;
  r.tv         = read_str(d, "tv");
  r.scope      = read_str(d, "scope");
  r.structure  = read_str(d, "structure");
  r.structures = read_structures(d);
  r.tb         = read_str(d, "tb");
  r.gen        = read_str(d, "gen");
  return r;
}

sim_tune::Source source_from(std::string_view s) {
  if (s == "store") {
    return sim_tune::Source::store;
  }
  if (s == "file") {
    return sim_tune::Source::file;
  }
  if (s == "explicit") {
    return sim_tune::Source::explicit_;
  }
  return sim_tune::Source::dflt;
}

struct Queued_diag {
  bool        error = false;
  std::string code;
  std::string category;
  std::string msg;
  std::string hint;
};

}  // namespace

// ---- option grammar ---------------------------------------------------------------

std::pair<std::string, std::string> sim_set_value_error(const Sim_set_option& opt, std::string_view value) {
  using K        = Sim_set_option::Kind;
  const auto bad = [&](std::string_view expects, std::string hint = {}) {
    return std::pair{std::format("--set/--config sim.{} expects {}, got '{}'", opt.name, expects, value), std::move(hint)};
  };
  switch (opt.kind) {
    case K::boolean    : return is_bool_text(value) ? std::pair<std::string, std::string>{} : bad("true|false");
    case K::non_neg_num: {
      // The numeric checkpoint knobs must be non-negative numbers, else a typo
      // would silently reach the driver as 0 (checkpoint every cycle).
      const std::string v{value};
      errno               = 0;
      char*        endp   = nullptr;
      const double parsed = std::strtod(v.c_str(), &endp);
      if (v.empty() || endp == v.c_str() || *endp != '\0' || parsed < 0.0 || errno == ERANGE) {
        return bad("a non-negative number");
      }
      return {};
    }
    case K::bool_or_file:
    case K::path        : return {};
    case K::backend:
      return livehd::sim::parse_tune_backend(value).ok ? std::pair<std::string, std::string>{} : bad("auto|slop|llvm");
    case K::tri: return livehd::sim::parse_tune_dirty(value).ok ? std::pair<std::string, std::string>{} : bad("auto|on|off");
    case K::tune_mode:
      return value == "auto" || value == "on" || value == "off" ? std::pair<std::string, std::string>{}
                                                                : bad("auto|on|off (a mode, not a boolean)");
    case K::fence:
      return livehd::sim::parse_tune_fence(value).ok ? std::pair<std::string, std::string>{}
                                                     : bad("auto|none|N (N a whole number in [0, 1048576])",
                                                           "`none` fences nothing; `0` fences every single-use module");
    case K::num_or_auto:
      return livehd::sim::parse_tune_live_words(value).ok
                 ? std::pair<std::string, std::string>{}
                 : bad("auto|N (N a whole number in [1, 1048576])", value == "0" ? "the default budget is spelled `auto`" : "");
    case K::count: {
      // The same range drv.bin accepts, checked here so a bad value fails at
      // parse time rather than after a full build (or silently, when the run
      // does not profile).
      uint64_t n = 0;
      if (!livehd::sim::tune_detail::whole(value, 0, livehd::sim::kTuneMaxNumber, n)) {
        return bad("a whole number in [0, 1048576]");
      }
      return {};
    }
  }
  return {};
}

std::optional<std::pair<std::string, std::string>> renamed_sim_set(std::string_view flag, std::string_view value) {
  if (const auto r = sim_tune::renamed_sim_flag(flag, value)) {
    return std::pair{"sim." + r->new_flag, r->value};
  }
  return std::nullopt;
}

std::vector<std::pair<std::string, std::string>> sim_tune_codegen_labels(const Options& opts, Result& res) {
  Tune_vector v;
  if (opts.sim_tune.resolved) {
    v = Tune_vector{opts.sim_tune.dirty, opts.sim_tune.fence, opts.sim_tune.live_words, opts.sim_tune.llvm};
    // The session already read the file; the compile phase reset the inputs.
    if (const auto path = sim_tune::last_set(opts.sets, "sim.tune.file"); path && !path->empty()) {
      add_input(res, *path);
    }
  } else {
    // `lhd compile --emit-dir sim:`: no workdir tune store; explicit > file > default.
    const auto f = load_tune_file(opts, res);
    v            = sim_tune::resolve(explicit_pins(opts), f ? f->pins : sim_tune::Pins{}, std::nullopt).v;
  }
  return {
      {"color_dirty", v.dirty ? "true" : "false"},
      {"fence_ratio",              v.fence_str()},
      { "live_words",         v.live_words_str()},
      {    "backend",            v.backend_str()},
  };
}

// ---- the session ------------------------------------------------------------------

struct Sim_tune_session::Impl {
  Options&       opts;
  Result&        res;
  Sim_tune_shape sh;

  int    uncaught_at_start = 0;
  size_t phase_rows_start  = 0;

  sim_tune::Mode mode          = sim_tune::Mode::auto_;
  bool           mode_explicit = false;
  bool           enabled       = false;
  std::string    reason;

  sim_tune::Pins ex_pins;
  sim_tune::Pins file_pins;
  std::string    file_structure;
  std::string    export_path;
  std::string    stride;

  // workdir layout
  std::string wd;  // absolute workdir
  std::string scope;
  std::string store_path;
  std::string inbox;
  std::string tune_dir;  // <wd>/sim_tune
  std::string drv_cpp;

  tune::File_lock          lock;
  std::vector<std::string> lines;
  sim_tune::State          state;

  sim_tune::Resolved               applied;
  bool                             applied_known = true;
  std::optional<Applied_file>      applied_file;  // run-only: the tree's record
  std::optional<sim_tune::Trial>   trial_in_play;
  std::optional<sim_tune::Verdict> verdict;
  std::optional<sim_tune::Stats>   run_stats;
  std::string                      run_fill;
  std::vector<std::string>         mine_vectors;  // the vectors of this run's own raw records
  std::string                      gate;
  std::vector<std::string>         notes;
  std::vector<Queued_diag>         diags;

  bool                   profile          = false;
  bool                   set_parser       = true;  // until inspect_driver reads the driver: a setup generates one with it
  bool                   driver_inspected = false;
  bool                   regen            = false;
  bool                   regenerating     = false;  // this setup dropped the tree's label and rewrites the tree
  bool                   compiled         = false;
  bool                   built            = false;
  bool                   diverged         = false;
  bool                   refused          = false;  // a --run-only this session refused (res carries the usage error)
  bool                   foreign_tree     = false;  // a --run-only of a tree another scope generated
  uint64_t               run_start        = 0;
  std::string            tb_digest;
  std::optional<Tree_id> generated_id;  // what this setup generated (after_generate)

  Impl(Options& o, Result& r, Sim_tune_shape s) : opts(o), res(r), sh(std::move(s)) {}

  [[nodiscard]] bool uz_set() const { return sh.unknown_zero_set || (applied_file && applied_file->unknown_zero_set); }

  // The binary folds every `?` to 0 at generation: its setup set
  // sim.unknown_zero=true explicitly (this invocation's, or the --run-only
  // tree's setup).
  [[nodiscard]] bool uz_baked() const {
    if (sh.run_only) {
      return applied_file && applied_file->unknown_zero_set && applied_file->unknown_zero;
    }
    return sh.unknown_zero_set && sh.unknown_zero;
  }

  std::string retained_dir(std::string_view tv) const { return std::format("{}/v-{}", tune_dir, tune::hex16(tune::fnv1a64(tv))); }
  std::string retained_json() const { return tune_dir + "/retained.json"; }

  // The identity of the tree in <simdir> right now. A setup that started
  // rewriting it knows it only once generation finished (a half-generated tree
  // has no trustworthy identity).
  [[nodiscard]] std::optional<Tree_id> current_tree_id() const {
    if (regenerating) {
      return generated_id;
    }
    return read_tree_id(sh.simdir);
  }

  // The tree holds the OPEN attempt's trial vector, built for this scope.
  [[nodiscard]] bool tree_holds_open_trial() const {
    if (!state.trial || !state.attempt_open) {
      return false;
    }
    const auto ap = read_applied(sh.simdir);
    return ap && ap->trial && ap->closed.empty() && ap->vector == state.trial->to && ap->scope == scope;
  }

  double phase_sum(std::string_view name) const {
    double ms = 0;
    for (size_t i = phase_rows_start; i < res.phase_ms.size(); ++i) {
      if (res.phase_ms[i].first == name) {
        ms += res.phase_ms[i].second;
      }
    }
    return ms;
  }

  [[nodiscard]] bool has_phase(std::string_view name) const {
    for (size_t i = phase_rows_start; i < res.phase_ms.size(); ++i) {
      if (res.phase_ms[i].first == name) {
        return true;
      }
    }
    return false;
  }

  void warn(std::string code, std::string msg, std::string hint = {}) {
    diags.push_back(Queued_diag{false, std::move(code), "io", std::move(msg), std::move(hint)});
  }

  bool persist(const std::vector<std::string>& add) {
    if (add.empty()) {
      return true;
    }
    std::string why;
    if (!tune::append_jsonl(store_path, add, why)) {
      notes.push_back(std::format("could not update the tune store: {}", why));
      return false;
    }
    return true;
  }

  void compact_if_needed() {
    if (lines.size() < sim_tune::cm1::kCompactAt) {
      return;
    }
    auto        kept = sim_tune::compact(lines);
    std::string why;
    if (tune::rewrite_jsonl(store_path, kept, why)) {
      lines = std::move(kept);
      state = sim_tune::replay(lines);
    }
  }

  void load_store() {
    auto l = tune::load_jsonl(store_path, sim_tune::kStoreSchema);
    if (l.status == tune::Jsonl_load::Status::bad) {
      warn("sim-tune-store-untrusted",
           std::format("the sim tune store {} was untrusted ({}) and set aside{}",
                       store_path,
                       l.why,
                       l.bad_path.empty() ? std::string{} : " as " + l.bad_path),
           "tuning starts over from lhd's defaults");
    }
    lines = std::move(l.lines);
    state = sim_tune::replay(lines);
  }

  std::string driver_digest() {
    if (tb_digest.empty()) {
      if (const auto text = tune::read_file(drv_cpp)) {
        tb_digest = tune::hex16(tune::fnv1a64(*text));
      }
    }
    return tb_digest;
  }

  [[nodiscard]] sim_tune::Context context() const {
    sim_tune::Context cx;
    cx.mode          = mode;
    cx.explicit_pins = ex_pins;
    cx.file_pins     = file_pins;
    cx.may_propose   = !sh.observation && !sh.compile_only && (mode == sim_tune::Mode::on || !state.converged);
    cx.now           = tune::unix_seconds_now();
    return cx;
  }

  // Raw run files -> `run` records (dedup by basename). The inbox holds
  // lhd-driven runs; <simdir>/tune_runs direct drv.bin runs. A file is recorded
  // into THIS scope's store only when it was produced by this scope's current
  // tree or its retained incumbent (per-root structure match); anything else
  // (another design sharing the workdir, a tree from before an edit) is
  // discarded with a note. Records are appended first; the files are unlinked
  // only once the append succeeded (a failed append keeps them for the next
  // ingest, which dedups by basename).
  void ingest(bool this_run) {
    Phase_timer           ph(res, "sim.tune.ingest");
    std::vector<fs::path> files;
    const std::string     direct_dir = sh.simdir + "/tune_runs";
    for (const auto& dir : {inbox, direct_dir}) {
      std::error_code ec;
      for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const auto name = it->path().filename().string();
        if (!it->is_regular_file() || name.starts_with(".") || !name.ends_with(".json") || name.find(".tmp") != std::string::npos) {
          continue;
        }
        files.push_back(it->path());
      }
    }
    if (files.empty()) {
      return;
    }
    std::ranges::sort(files, [](const fs::path& a, const fs::path& b) { return a.filename() < b.filename(); });

    // Where this scope's runs can come from.
    std::vector<std::map<std::string, std::string>> homes;
    if (const auto ap = read_applied(sh.simdir); ap && ap->scope == scope) {
      homes.push_back(read_tree_id(sh.simdir).structures);
    }
    if (const auto rt = read_retained_file(retained_json()); rt && rt->scope == scope) {
      homes.push_back(rt->structures);
    }

    std::vector<std::string> add;
    std::vector<fs::path>    recorded;
    const auto               now = tune::unix_seconds_now();
    for (const auto& f : files) {
      const auto      id       = f.filename().string();
      const bool      in_inbox = f.parent_path() == fs::path(inbox);
      std::error_code ec;
      if (std::ranges::any_of(state.runs, [&](const sim_tune::Run& r) { return r.id == id; })) {
        fs::remove(f, ec);  // already recorded
        continue;
      }
      const auto  text = tune::read_file(f.string());
      std::string err;
      auto        run = text ? sim_tune::run_from_raw(*text, err) : std::nullopt;
      if (!run) {
        notes.push_back(std::format("ignored raw run file {}: {}", id, text ? err : "unreadable"));
        fs::remove(f, ec);
        continue;
      }
      if (!std::ranges::any_of(homes, [&](const auto& h) { return run_belongs(*run, h); })) {
        notes.push_back(std::format("discarded raw run file {}: not a run of this design's tree ({})",
                                    id,
                                    run->structure.empty() ? std::string{"no structure"} : run->structure));
        fs::remove(f, ec);
        continue;
      }
      const bool mine = this_run && in_inbox && name_ns(id) >= run_start;
      run->id         = id;
      run->t          = now;
      run->src        = in_inbox ? "lhd" : "direct";
      run->mode       = mine ? std::string{sim_tune::mode_name(mode)} : (in_inbox ? std::string{} : std::string{"direct"});
      run->tb         = driver_digest();
      if (mine) {
        run->setup_ms = (applied_file ? applied_file->cgen_ms : phase_sum("inou.cgen.sim")) + phase_sum("sim.hostbuild");
        run->regen    = sh.run_only ? (applied_file && applied_file->regen) : regen;
        if (run->vector.empty()) {
          run->vector = applied.v.tv1();  // a binary built before the tune-id TU existed
        }
        run_stats = run->stats;
        run_fill  = run->fill;
        mine_vectors.push_back(run->vector);
      }
      add.push_back(sim_tune::run_record(*run));
      recorded.push_back(f);
    }
    if (!persist(add)) {
      return;  // keep the files: the next ingest retries them
    }
    for (auto& line : add) {
      sim_tune::apply_record(state, line);
      lines.push_back(std::move(line));
    }
    for (const auto& f : recorded) {
      std::error_code ec;
      fs::remove(f, ec);
    }
  }

  void drop_retained() noexcept {
    try {
      std::error_code ec;
      for (fs::directory_iterator it(tune_dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_directory() && it->path().filename().string().starts_with("v-")) {
          tune::discard_dir(it->path().string());
        }
      }
      fs::remove(retained_json(), ec);
    } catch (...) {  // NOLINT(bugprone-empty-catch) -- best effort: a stale retained tree only costs disk
    }
  }

  // The tree still holds the trial `tr` after `v` closed it without a swap
  // back to the incumbent (no retained clone, a clone of another design, a
  // failed swap): mark its label CLOSED, so a later --run-only refuses to run
  // (and report as the incumbent) a vector the store has rejected or never
  // judged. A diverging binary is also removed, so a direct re-exec cannot
  // reuse it. A setup that died mid-generation left no label: when cgen had
  // started, a minimal closed one is written (the tree is part trial).
  void close_tree_label(const sim_tune::Trial& tr, const sim_tune::Verdict& v) {
    const bool marked = edit_applied(
        sh.simdir,
        [&](const rj::Document& d) {
          return read_bool(d, "trial") && read_str(d, "vector") == tr.to && read_str(d, "scope") == scope;
        },
        [&](rj::Document& d) {
          set_member(d, "closed", rj::Value(v.result.c_str(), d.GetAllocator()));
          set_member(d, "closed_why", rj::Value(v.reason.c_str(), d.GetAllocator()));
        });
    std::error_code ec;
    if (!marked && regenerating && !fs::exists(sh.simdir + "/tune_applied.json", ec) && has_phase("inou.cgen.sim")) {
      rj::StringBuffer             sb;
      rj::Writer<rj::StringBuffer> w(sb);
      w.StartObject();
      write_str(w, "schema", sim_tune::kAppliedSchema);
      write_str(w, "vector", tr.to);
      w.Key("trial");
      w.Bool(true);
      write_str(w, "closed", v.result);
      write_str(w, "closed_why", v.reason);
      write_str(w, "scope", scope);
      w.EndObject();
      std::string why;
      (void)tune::write_file_atomic(sh.simdir + "/tune_applied.json", std::string{sb.GetString()} + "\n", why);
    }
    if (v.result == "divergence") {
      fs::remove(sh.simdir + "/drv.bin", ec);
    }
    notes.push_back(
        std::format("the sim tree still holds the {} trial vector: a --run-only refuses it until a setup rebuilds "
                    "the incumbent",
                    v.result));
  }

  // Put the incumbent back after a trial lost: swap the retained clone into
  // place when the tree holds the trial -- its label says it was built as that
  // trial, or THIS invocation's setup started regenerating it (a failed cgen
  // leaves a mixed, unlabeled tree). The clone replaces the tree ONLY when it
  // simulates the same design (scope, structure and testbench equal to the
  // tree's): a clone taken before an edit, or of another design sharing the
  // workdir, is discarded instead, and the next setup rebuilds the incumbent
  // from the current sources (a --run-only never runs a stale design). A trial
  // tree left in place is marked closed. Returns whether the swap happened.
  bool revert(const sim_tune::Trial& tr, const sim_tune::Verdict& v) {
    const auto      ap            = read_applied(sh.simdir);
    const bool      tree_is_trial = (ap && ap->trial && ap->vector == tr.to && ap->scope == scope)
                                    || (trial_in_play && !sh.run_only && trial_in_play->to == tr.to);
    const auto      rt            = read_retained_file(retained_json());
    const auto      ret           = retained_dir(tr.from);
    std::error_code ec;
    const bool      have    = rt && rt->tv == tr.from && fs::is_directory(ret, ec);
    bool            swapped = false;
    if (tree_is_trial && have) {
      const auto cur  = current_tree_id();
      const bool same = cur && cur->known() && rt->scope == scope && rt->structure == cur->structure && rt->tb == cur->tb;
      if (!cur || !cur->known()) {
        notes.push_back(
            "the trial tree was not fully generated, so the retained incumbent cannot be matched to it: "
            "discarded; the next setup rebuilds the incumbent");
      } else if (!same) {
        notes.push_back(
            "the retained incumbent tree was built from other sources (design, testbench or scope): discarded; the next "
            "setup rebuilds the incumbent");
      } else if (std::string why; tune::swap_dirs(sh.simdir, ret, why)) {
        tune::discard_dir(ret);  // it now holds the losing trial tree
        swapped = true;
        notes.push_back("reverted to the retained incumbent tree (no rebuild)");
      } else {
        notes.push_back(std::format("could not swap the incumbent tree back ({}); the next setup rebuilds it", why));
      }
    } else if (tree_is_trial) {
      notes.push_back("no retained incumbent tree; the next setup rebuilds it");
    }
    if (tree_is_trial && !swapped) {
      close_tree_label(tr, v);
    }
    drop_retained();
    return swapped;
  }

  void handle_verdict(const sim_tune::Verdict& v, const sim_tune::Trial& tr) {
    verdict       = v;
    bool restored = false;
    if (v.result == "accepted") {
      drop_retained();
      // The tree's vector is the incumbent now, not a trial.
      (void)edit_applied(
          sh.simdir,
          [&](const rj::Document& d) {
            return read_bool(d, "trial") && read_str(d, "vector") == tr.to && read_str(d, "scope") == scope;
          },
          [&](rj::Document& d) { set_member(d, "trial", rj::Value(false)); });
    } else {
      restored = revert(tr, v);
    }
    if (v.result == "divergence") {
      diverged = true;
      std::string tests;
      for (const auto& t : v.tests) {
        tests += tests.empty() ? t : ", " + t;
      }
      diags.push_back(Queued_diag{true,
                                  "sim-tune-divergence",
                                  "internal",
                                  std::format("sim.tune trial {} changed simulated results against {} (tests: {}); the vector "
                                              "is banned and {}",
                                              v.to,
                                              v.from,
                                              tests,
                                              restored ? "the incumbent restored"
                                                       : "its tree set aside (a --run-only refuses it); the next setup "
                                                         "rebuilds the incumbent"),
                                  "a tune knob must never change a value: this is a livehd simulator bug, reproduce it "
                                  "with the two vectors' `--set` lists"});
    }
  }

  // Close the pending trial now (a stale trial, a failed attempt): record it,
  // then revert whatever the tree holds.
  void close_now(sim_tune::Verdict v) {
    if (!state.trial) {
      return;
    }
    const auto tr   = *state.trial;
    auto       recs = sim_tune::close_trial(state, std::move(v), tune::unix_seconds_now());
    lines.insert(lines.end(), recs.begin(), recs.end());
    persist(recs);
    if (state.last_verdict) {
      handle_verdict(*state.last_verdict, tr);
    }
  }

  // decide(): judge the open attempt (closing it when `attempt_over`, charged
  // unless `charged` is false), then propose or converge.
  void run_decide(bool attempt_over = false, std::string failed = {}, std::string why = {}, bool charged = true) {
    auto cx            = context();
    cx.attempt_over    = attempt_over;
    cx.attempt_failed  = std::move(failed);
    cx.attempt_why     = std::move(why);
    cx.attempt_charged = charged;
    auto out           = sim_tune::decide(state, cx);
    lines.insert(lines.end(), out.append.begin(), out.append.end());
    persist(out.append);
    if (!out.gate.empty()) {
      gate = out.gate;
    }
    if (out.verdict && out.judged) {
      handle_verdict(*out.verdict, *out.judged);
    }
  }

  // This setup builds the pending trial's vector (the knobs the trial moves
  // come from the store).
  bool build_trial(const sim_tune::Resolved& base) {
    const auto tr = *state.trial;
    const auto to = Tune_vector::parse(tr.to);
    if (!to) {
      return false;
    }
    applied         = base;
    applied.v.dirty = to->dirty;
    applied.v.fence = to->fence;
    applied.dirty   = sim_tune::Source::store;
    applied.fence   = sim_tune::Source::store;
    trial_in_play   = tr;
    return true;
  }

  // Apply the pending trial at this setup: ONE attempt starts (persisted now,
  // so an lhd killed mid-run still charges it), and the incumbent tree is
  // retained first so a losing trial reverts with a pointer swap.
  void apply_trial(const sim_tune::Resolved& base) {
    if (!build_trial(base)) {
      return;
    }
    const auto tr = *state.trial;
    {
      auto line = sim_tune::attempt_record(tr, tune::unix_seconds_now(), mode);
      sim_tune::apply_record(state, line);
      lines.push_back(line);
      persist({line});
    }

    // Retain the tree only when it is THIS scope's incumbent (its own label
    // says so): a tree another design left in a shared workdir is no incumbent.
    const auto      ap = read_applied(sh.simdir);
    std::error_code ec;
    if (!ap || ap->vector != tr.from || ap->scope != scope || !fs::exists(sh.simdir + "/gen_digests.json", ec)) {
      return;
    }
    const auto id = read_tree_id(sh.simdir);
    if (!id.known()) {
      notes.push_back("the incumbent tree carries no identity: not retained (a losing trial costs a rebuild)");
      return;
    }
    drop_retained();  // the budget is ONE retained vector
    const auto  ret = retained_dir(tr.from);
    std::string why;
    if (!tune::clone_tree(sh.simdir, ret, why)) {
      notes.push_back(std::format("no incumbent retention ({}): a losing trial costs a rebuild", why));
      return;
    }
    fs::remove_all(ret + "/tune_runs", ec);
    rj::StringBuffer             sb;
    rj::Writer<rj::StringBuffer> w(sb);
    w.StartObject();
    write_str(w, "schema", sim_tune::kRetainedSchema);
    write_str(w, "tv", tr.from);
    write_str(w, "scope", ap->scope);
    write_str(w, "structure", id.structure);
    write_structures(w, id.structures);
    write_str(w, "tb", id.tb);
    write_str(w, "gen", tune::hex16(tune::fnv1a64(tune::read_file(sh.simdir + "/gen_digests.json").value_or(""))));
    w.EndObject();
    if (!tune::write_file_atomic(retained_json(), sb.GetString(), why)) {
      tune::discard_dir(ret);
    }
  }

  void begin() {
    uncaught_at_start = std::uncaught_exceptions();
    phase_rows_start  = res.phase_ms.size();
    drv_cpp           = sh.simdir + "/drv.cpp";

    if (const auto m = sim_tune::last_set(opts.sets, "sim.tune.profile")) {
      mode          = sim_tune::parse_mode(*m).value_or(sim_tune::Mode::auto_);
      mode_explicit = true;
    }
    ex_pins = explicit_pins(opts);
    if (const auto f = load_tune_file(opts, res)) {
      file_pins      = f->pins;
      file_structure = f->structure;
    }
    export_path = sim_tune::last_set(opts.sets, "sim.tune.export").value_or("");
    stride      = sim_tune::last_set(opts.sets, "sim.tune.profile_stride").value_or("");
    if (stride == "0") {
      stride.clear();
    }

    if (!sh.user_workdir) {
      reason = "no-workdir";
    } else if (!opts.incremental) {
      reason = "incremental-off";
    } else if (mode == sim_tune::Mode::off) {
      reason = "mode-off";
    } else if (sh.observation) {
      reason = "observation";
    }
    enabled = reason.empty();
    if (mode_explicit && mode == sim_tune::Mode::on && !enabled && reason != "observation") {
      warn("tune-disabled",
           std::format("--set sim.tune.profile=on has no effect: the tuner needs {}",
                       reason == "no-workdir" ? "a --workdir to keep its data in" : "lhd.incremental=true"),
           "pass --workdir DIR (and keep lhd.incremental on)");
    }
    if (!export_path.empty() && !sh.user_workdir) {
      warn("tune-disabled",
           "--set sim.tune.export without a --workdir exports the explicit/file/default vector: there is no tuned decision",
           "tune in a persistent --workdir first");
    }

    if (sh.user_workdir) {
      std::error_code ec;
      wd = fs::absolute(sh.simroot, ec).lexically_normal().string();
      while (wd.size() > 1 && wd.back() == '/') {
        wd.pop_back();
      }
      scope      = compile_cache_scope_name(opts, sh.sources);
      tune_dir   = wd + "/sim_tune";
      inbox      = tune_dir + "/inbox";
      store_path = std::format("{}/incr/scopes/sim/{}/tune.jsonl", wd, scope);
    }

    applied = sim_tune::resolve(ex_pins, file_pins, std::nullopt);
    if (!enabled && reason == "observation") {
      // An observation run (VCD, --list-signals, --restart-cycle, ...) never
      // learns, but it builds and replays the TUNED tree: the workdir's decision
      // is read without the lock and without writing anything, so the run
      // reuses the tree (and the binary) the plain runs use.
      const auto l = tune::load_jsonl(store_path, sim_tune::kStoreSchema, /*read_only=*/true);
      if (l.status == tune::Jsonl_load::Status::ok) {
        if (const auto st = sim_tune::replay(l.lines); st.incumbent) {
          applied = sim_tune::resolve(ex_pins, file_pins, st.incumbent);
        }
      }
    }
    if (enabled) {
      std::string why;
      if (!lock.open(wd + "/.lhd_sim.lock", why)
          || !lock.exclusive(std::format("lhd sim: waiting for another `lhd sim` in {}", wd))) {
        enabled = false;
        reason  = "lock-failed";
        notes.push_back(why);
      }
    }
    const bool ckpt_blocks = mode == sim_tune::Mode::auto_ && sh.checkpoint_explicit;  // auto then does not profile
    if (enabled) {
      load_store();
      ingest(false);  // direct drv.bin runs (and a crashed lhd's leftovers), BEFORE any clone or swap
      Phase_timer ph(res, "sim.tune.decide");

      // An attempt an earlier setup left open ends here -- judged on whatever
      // runs of it were ingested, else abandoned -- unless this invocation
      // runs (or only builds) the trial tree it left behind: the --run-only
      // of that tree, or a setup that rebuilds the very same tree (a repeated
      // --setup-only, or a full run after one), which keeps the attempt.
      std::string over;
      bool        over_charged = true;
      if (state.trial && state.attempt_open) {
        const bool tree_trial = tree_holds_open_trial();
        if (!sh.run_only) {
          if (!tree_trial) {
            over = "a new setup replaced the trial tree before its run was judged";  // another scope, an off run
          } else if (auto why = sim_tune::stale_reason(state, context(), false)) {
            over         = *why;  // nothing was tried: not an attempt
            over_charged = false;
          } else if (sh.compile_only || ckpt_blocks) {
            over         = "stale: this setup will not profile the trial (compile_only or explicit checkpoint settings)";
            over_charged = false;
          }
        } else if (!tree_trial) {
          over = "the trial tree was replaced before its run";
        } else if (ckpt_blocks && !sh.compile_only) {
          over = "the run that followed its setup does not profile (explicit checkpoint settings)";
        }
      }
      run_decide(!over.empty(), {}, over, over_charged);

      const auto base = sim_tune::resolve(ex_pins, file_pins, state.incumbent);
      applied         = base;
      if (!file_structure.empty() && !state.runs.empty() && !state.runs.back().structure.empty()
          && file_structure != state.runs.back().structure) {
        warn("sim-tune-file-structure",
             std::format("sim.tune.file was recorded for design structure {}, this workdir last ran {}",
                         file_structure,
                         state.runs.back().structure),
             "the pinned vector still applies (a tune knob never changes values); refresh the file when convenient");
      }

      // A pending trial that no longer describes this workdir closes without
      // charging an attempt: at a setup, a knob it moves got pinned or its
      // `from` is not the resolved vector; at the --run-only of its tree, a pin
      // contradicts what that tree baked.
      if (state.trial && !sh.compile_only) {
        const bool trial_built = sh.run_only && tree_holds_open_trial();
        if (!sh.run_only || trial_built) {
          if (auto why = sim_tune::stale_reason(state, context(), trial_built)) {
            close_now(sim_tune::Verdict{.result = "abandoned", .oracle = "none", .reason = *why, .charged = false});
          }
        }
      }

      // Apply a pending trial only at a setup whose run will profile it (for
      // --setup-only: the --run-only that follows, as far as this invocation
      // can tell); anything else would run an unverified vector unjudged.
      const bool would_profile
          = !sh.compile_only && !ckpt_blocks && (mode == sim_tune::Mode::on || !state.converged || state.trial.has_value());
      if (!sh.run_only && state.trial && state.attempt_open) {
        // The tree holds the open attempt's trial and this setup rebuilds it
        // (a gen-key no-op): the SAME attempt continues -- no new record, the
        // retained incumbent stays valid -- and this invocation's run (or the
        // --run-only after a --setup-only) judges it.
        (void)build_trial(base);
      } else if (!sh.run_only && state.trial) {
        if (would_profile) {
          apply_trial(base);
        } else {
          notes.push_back(std::format("the pending trial {} waits for a profiled run", state.trial->to));
        }
      }
      if (sh.run_only && !sh.compile_only && tree_holds_open_trial()) {
        trial_in_play = state.trial;  // this run is the open attempt's run
      }
    }

    if (sh.run_only) {
      // Nothing regenerates: the tree holds whatever its setup baked.
      applied_file = read_applied(sh.simdir);
      if (applied_file) {
        if (const auto v = Tune_vector::parse(applied_file->vector)) {
          applied.v          = *v;
          applied.dirty      = source_from(applied_file->source[0]);
          applied.fence      = source_from(applied_file->source[1]);
          applied.live_words = source_from(applied_file->source[2]);
          applied.backend    = source_from(applied_file->source[3]);
        } else {
          applied_known = false;
        }
      } else {
        applied_known = false;
      }
      check_run_only_tree();
    } else {
      std::error_code ec;
      const auto      ap = read_applied(sh.simdir);
      regen              = !fs::exists(sh.simdir + "/gen_digests.json", ec) || !ap || ap->vector != applied.v.tv1();
      // This setup is about to regenerate the tree: drop its label first, so a
      // setup that dies half way never leaves a tree claiming the old vector
      // (after_generate writes the new label).
      if (sh.user_workdir) {
        fs::remove(sh.simdir + "/tune_applied.json", ec);
      }
      regenerating = true;
    }

    opts.sim_tune = Options::Sim_tune{true, applied.v.dirty, applied.v.fence, applied.v.live_words, applied.v.llvm};

    // Does this run sample? Converged `auto` reuses the decision for free.
    if (enabled && !sh.setup_only && !sh.compile_only && !refused && !foreign_tree) {
      if (mode == sim_tune::Mode::on || !state.converged || state.trial) {
        profile = true;
        if (ckpt_blocks) {
          profile = false;
          notes.push_back("explicit checkpoint settings: not profiled (sim.tune.profile=on profiles without checkpoints)");
        }
      }
    }
    write_envelope();
  }

  // A --run-only runs whatever its tree holds, so the tree must be one the
  // tuner can stand behind:
  //  - a trial tree whose attempt was CLOSED without a swap back (rejected,
  //    diverged, abandoned, failed) is refused in every mode: its vector is
  //    not the store's incumbent, and a diverged one computes wrong values;
  //  - a tree another scope generated (a shared workdir) runs as built, with a
  //    warning, unprofiled (its raw run would be discarded) and never claimed
  //    as this scope's decision;
  //  - an unclosed trial tree that is neither the open attempt nor an accepted
  //    vector of this store is refused; with the tuner off it runs as built,
  //    with a warning (nothing here can tell whether it is still open).
  void check_run_only_tree() {
    if (!applied_file) {
      return;
    }
    const auto& ap     = *applied_file;
    const auto  refuse = [&](std::string msg) {
      refused           = true;
      applied_known     = false;
      res.status        = "fail";
      res.error_class   = "usage";
      res.error_message = std::move(msg);
      res.error_hint
          = std::format("re-run `lhd sim ... --setup-only --workdir {}` (or a full `lhd sim`), which rebuilds the incumbent",
                        sh.simroot);
      res.exit_code = exit_code_for(res.error_class);
      notes.push_back(std::format("run refused: the tree holds trial vector {}", ap.vector));
    };
    if (ap.closed == "divergence") {
      refuse(
          std::format("the sim tree in {} holds trial vector {}, which diverged from the incumbent "
                      "(sim-tune-divergence): its results are wrong",
                      sh.simroot,
                      ap.vector));
      return;
    }
    if (!ap.scope.empty() && !scope.empty() && ap.scope != scope) {
      foreign_tree  = true;
      applied_known = false;
      notes.push_back(std::format("the tree was generated for {}: run as built, unprofiled", ap.scope));
      warn("sim-tune-foreign-tree",
           std::format("the sim tree in {} was generated for {}, not {}: this --run-only runs it as built, unprofiled",
                       sh.simroot,
                       ap.scope,
                       scope),
           std::format("re-run --setup-only for {}", scope));
      return;
    }
    if (!ap.trial) {
      return;
    }
    if (!ap.closed.empty()) {
      refuse(
          std::format("the sim tree in {} holds trial vector {}, which was {} ({}); the incumbent could not be swapped "
                      "back, so the tree is not this workdir's decision",
                      sh.simroot,
                      ap.vector,
                      ap.closed,
                      ap.closed_why.empty() ? std::string{"no reason recorded"} : ap.closed_why));
      return;
    }
    if (!enabled) {
      applied_known = false;
      warn("sim-tune-trial-tree",
           std::format("the sim tree in {} holds the trial vector {}, which this run neither judges nor records (the tuner "
                       "is off here: {})",
                       sh.simroot,
                       ap.vector,
                       reason),
           "run it with the tuner on to judge the trial, or re-run --setup-only to rebuild the incumbent");
      return;
    }
    const bool open     = state.trial && state.attempt_open && state.trial->to == ap.vector;
    const bool accepted = std::ranges::find(state.accepted, ap.vector) != state.accepted.end();
    if (!open && !accepted) {
      refuse(
          std::format("the sim tree in {} holds trial vector {}, which is neither an open trial nor an accepted vector of "
                      "this workdir's tune store (nothing would judge its run)",
                      sh.simroot,
                      ap.vector));
    }
  }

  // The `?` fill the binary actually got: this run's raw file says so when it
  // profiled; otherwise it is baked-true, else the explicit value lhd forwards,
  // else the implicit profiling zero fill, else random.
  [[nodiscard]] std::string fill() const {
    if (!run_fill.empty()) {
      return run_fill;
    }
    if (uz_baked()) {
      return "zero";
    }
    if (sh.unknown_zero_set) {
      return sh.unknown_zero && set_parser ? "zero" : "random";
    }
    if (uz_set()) {
      return "random";  // the --run-only tree's setup chose random fill explicitly
    }
    return profile && set_parser ? "zero" : "random";
  }

  void write_envelope() {
    sim_tune::Envelope e;
    e.mode          = mode;
    e.enabled       = enabled;
    e.reason        = reason;
    e.profiling     = profile;
    e.applied       = applied;
    e.applied_known = applied_known;
    e.fill          = fill();
    e.stats         = run_stats;
    e.trial         = trial_in_play;
    e.verdict       = verdict;
    e.gate          = gate;
    if (enabled) {
      e.rejected = state.rejected;
      if (state.trial) {
        e.pending = state.trial->to;
      }
      e.converged = state.converged && !state.trial;
    }
    e.notes           = notes;
    res.sim_tune_json = sim_tune::envelope_json(e);
    res.sim_tune_note = sim_tune::envelope_note(e);
  }

  void write_export() {
    if (export_path.empty()) {
      return;
    }
    const auto           cur = sim_tune::resolve(ex_pins, file_pins, enabled ? state.incumbent : std::nullopt);
    sim_tune::Provenance p;
    p.converged = enabled && state.converged && !state.trial;
    p.created   = tune::iso8601_utc(tune::unix_seconds_now());
    p.structure = file_structure;
    if (enabled && !state.runs.empty()) {
      p.structure    = state.runs.back().structure;
      const auto win = sim_tune::ladder_window(state, p.structure);  // the same runs the ladder reads
      if (!win.empty()) {
        p.stats = sim_tune::window_stats(win);
      }
    }
    std::string why;
    if (tune::write_file_atomic(export_path, sim_tune::tune_file_json(cur.v, p), why)) {
      res.outputs.push_back(export_path);
      if (!p.converged) {
        notes.push_back("exported a decision that has not converged yet");
      }
    } else {
      warn("sim-tune-export", std::format("could not write sim.tune.export {}: {}", export_path, why));
    }
  }

  void finish() noexcept {
    try {
      const bool failed        = res.status != "pass" || std::uncaught_exceptions() > uncaught_at_start;
      // cgen or the host build started and did not complete.
      const bool build_reached = (!compiled && has_phase("inou.cgen.sim")) || (!built && has_phase("sim.hostbuild"));
      // This invocation's attempt is still open: its run never reached
      // after_run (or this was the --setup-only that applied it).
      if (enabled && trial_in_play && state.trial && state.attempt_open && state.trial->to == trial_in_play->to) {
        if (lock.is_open()) {
          (void)lock.exclusive(std::format("lhd sim: waiting for another `lhd sim` in {}", wd));
        }
        if (failed && build_reached) {
          close_now(sim_tune::Verdict{.result  = "build-failed",
                                      .oracle  = "none",
                                      .reason  = "the trial vector failed to generate or build",
                                      .charged = true});
        } else if (failed) {
          // A failure before the trial vector was generated or ran (a parse
          // error, missing runtime headers) is nobody's attempt.
          close_now(sim_tune::Verdict{.result  = "abandoned",
                                      .oracle  = "none",
                                      .reason  = std::format("the invocation failed before its trial ran: {}", res.error_message),
                                      .charged = false});
        } else if (!sh.setup_only && !sh.compile_only) {
          close_now(sim_tune::Verdict{.result  = "abandoned",
                                      .oracle  = "none",
                                      .reason  = "the trial run did not complete",
                                      .charged = true});
        }
      }
      write_export();
      write_envelope();
      for (const auto& d : diags) {
        auto b = d.error ? livehd::diag::err("lhd.sim", d.code, d.category) : livehd::diag::warn("lhd.sim", d.code, d.category);
        b.msg("{}", d.msg);
        if (!d.hint.empty()) {
          b.hint(d.hint);
        }
        b.emit();
      }
      if (diverged && res.status == "pass") {
        res.status        = "fail";
        res.error_class   = "internal";
        res.error_message = "sim.tune: a trial vector changed simulated results (sim-tune-divergence)";
        res.exit_code     = exit_code_for(res.error_class);
      }
    } catch (...) {  // NOLINT(bugprone-empty-catch) -- the tuner must never turn a finished run into a crash
    }
    lock.release();
  }
};

Sim_tune_session::Sim_tune_session(Options& opts, Result& res, Sim_tune_shape shape)
    : p_(std::make_unique<Impl>(opts, res, std::move(shape))) {
  p_->begin();
}

Sim_tune_session::~Sim_tune_session() { p_->finish(); }

void Sim_tune_session::after_compile() { p_->compiled = true; }

void Sim_tune_session::after_generate() {
  auto& p = *p_;
  if (!p.sh.user_workdir) {
    return;
  }
  p.generated_id = read_tree_id(p.sh.simdir);
  // What the tree now holds, so a later --run-only (and the retention and
  // ingest logic) knows the vector and the design without trusting the store.
  rj::StringBuffer             sb;
  rj::Writer<rj::StringBuffer> w(sb);
  w.StartObject();
  write_str(w, "schema", sim_tune::kAppliedSchema);
  write_str(w, "vector", p.applied.v.tv1());
  w.Key("source");
  w.StartObject();
  const std::pair<const char*, sim_tune::Source> src[] = {
      {     "dirty",      p.applied.dirty},
      {     "fence",      p.applied.fence},
      {"live_words", p.applied.live_words},
      {   "backend",    p.applied.backend}
  };
  for (const auto& [k, s] : src) {
    write_str(w, k, sim_tune::source_name(s));
  }
  w.EndObject();
  w.Key("trial");
  w.Bool(p.trial_in_play.has_value());
  write_str(w, "scope", p.scope);
  write_str(w, "structure", p.generated_id->structure);
  write_structures(w, p.generated_id->structures);
  write_str(w, "tb", p.generated_id->tb);
  w.Key("cgen_ms");
  w.Double(p.phase_sum("inou.cgen.sim"));
  w.Key("regen");
  w.Bool(p.regen);
  w.Key("unknown_zero_set");
  w.Bool(p.sh.unknown_zero_set);
  w.Key("unknown_zero");
  w.Bool(p.sh.unknown_zero);
  w.EndObject();
  std::string why;
  (void)tune::write_file_atomic(p.sh.simdir + "/tune_applied.json", std::string{sb.GetString()} + "\n", why);
}

void Sim_tune_session::inspect_driver(bool observation_baked) {
  auto&      p    = *p_;
  const auto text = tune::read_file(p.drv_cpp);
  p.set_parser    = false;
  if (text) {
    p.tb_digest  = tune::hex16(tune::fnv1a64(*text));
    p.set_parser = text->find("driver-set-parser: 1") != std::string::npos;
  }
  p.driver_inspected = true;
  if (observation_baked && p.profile) {
    p.profile = false;
    p.notes.push_back("the built driver carries VCD/observation code: not profiled");
  }
  if (p.profile && !p.set_parser) {
    p.profile = false;
    p.notes.push_back("this driver predates drv.bin's --set parser: re-run --setup-only to profile");
  }
  if (p.sh.run_only && !p.set_parser && (p.ex_pins.any() || p.file_pins.any())) {
    p.notes.push_back("this driver cannot check the sim.tune.* codegen knobs; re-run --setup-only");
  }
  if (p.sh.unknown_zero_set && !p.set_parser && !p.uz_baked()) {
    p.notes.push_back("this driver predates drv.bin's --set parser: sim.unknown_zero cannot reach it; re-run --setup-only");
  }
  p.write_envelope();
}

void Sim_tune_session::after_build() { p_->built = true; }

bool Sim_tune_session::profiling() const { return p_->profile; }

bool Sim_tune_session::refused() const { return p_->refused; }

std::string Sim_tune_session::driver_args() const {
  const auto& p = *p_;
  if (!p.set_parser) {
    return {};
  }
  std::string a;
  const auto  add = [&](const std::string& kv) { a += " --set " + shell_quote(kv); };
  // Ruling 3: an explicit sim.unknown_zero is always followed, profiling or
  // not (drv.bin refuses `false` on a binary generated with `true`).
  if (p.sh.unknown_zero_set) {
    add(std::format("sim.unknown_zero={}", p.sh.unknown_zero ? "true" : "false"));
  }
  if (p.profile) {
    add("sim.tune.profile=on");
    add("sim.tune.profile_dir=" + p.inbox);
    if (!p.uz_set()) {
      add("sim.unknown_zero=true");  // ruling 3: a profiling run is zero-filled unless the user chose
    }
    if (!p.stride.empty()) {
      add("sim.tune.profile_stride=" + p.stride);
    }
    a += " --no-checkpoint";  // fork CoW faults and the per-tick cadence clock would pollute the counters
  }
  if (p.sh.run_only) {
    // drv.bin's --set parser is the ONE authority on whether the pinned codegen
    // knobs match what this binary baked (a mismatch is its usage error).
    const auto pick = [](const auto& ex, const auto& file) { return ex ? ex : file; };
    if (const auto d = pick(p.ex_pins.dirty, p.file_pins.dirty)) {
      add(std::format("sim.tune.dirty={}", *d ? "on" : "off"));
    }
    if (const auto f = pick(p.ex_pins.fence, p.file_pins.fence)) {
      add("sim.tune.fence=" + Tune_vector{false, *f, 1, false}.fence_str());
    }
    if (const auto lw = pick(p.ex_pins.live_words, p.file_pins.live_words)) {
      add(std::format("sim.tune.live_words={}", *lw));
    }
    if (const auto be = pick(p.ex_pins.llvm, p.file_pins.llvm)) {
      add(std::format("sim.tune.backend={}", *be ? "llvm" : "slop"));
    }
  }
  return a;
}

void Sim_tune_session::before_run() {
  auto& p     = *p_;
  p.run_start = unix_ns_now();
  if (!p.enabled) {
    return;
  }
  std::error_code ec;
  fs::create_directories(p.inbox, ec);
  (void)p.lock.shared();
}

void Sim_tune_session::after_run(int rc) {
  auto& p = *p_;
  if (!p.enabled) {
    return;
  }
  (void)p.lock.exclusive(std::format("lhd sim: waiting for another `lhd sim` in {}", p.wd));
  p.ingest(true);
  Phase_timer ph(p.res, "sim.tune.decide");
  // This run ends the attempt it ran: judged when its record has a baseline,
  // else abandoned -- or failed, when drv.bin did not finish normally (0/1 are
  // its verdicts, 2 its usage error; anything else, or a signal, is a crash) or
  // a profiled run left no raw record of the trial vector.
  const bool  ours = p.trial_in_play && p.state.trial && p.state.attempt_open && p.state.trial->to == p.trial_in_play->to;
  std::string failed;
  std::string why;
  if (ours) {
    const bool got = std::ranges::find(p.mine_vectors, p.trial_in_play->to) != p.mine_vectors.end();
    if (rc < 0 || rc > 2) {
      failed = "run-failed";
      why    = rc < 0 ? std::string{"drv.bin died (a signal or crash) before it finished"}
                      : std::format("drv.bin exited with code {}", rc);
    } else if (rc == 2) {
      why = "drv.bin rejected its arguments (exit 2)";
    } else if (!p.profile) {
      why = "the trial run was not profiled";
    } else if (!got) {
      failed = "run-failed";
      why    = "drv.bin finished without writing the trial run's raw record";
    }
  }
  p.run_decide(ours, failed, why);
  p.compact_if_needed();
  p.write_envelope();
}

}  // namespace lhd
