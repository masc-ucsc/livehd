//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lhd_sim_tune.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <format>
#include <map>
#include <set>

#include "lhd_tune.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace lhd::sim_tune {

namespace {

namespace rj = rapidjson;
using Writer = rj::Writer<rj::StringBuffer>;

using livehd::sim::kTuneDefaultFenceRatio;
using livehd::sim::kTuneNoFences;

void key(Writer& w, std::string_view k) { w.Key(k.data(), static_cast<rj::SizeType>(k.size())); }
void str(Writer& w, std::string_view v) { w.String(v.data(), static_cast<rj::SizeType>(v.size())); }
// Fixed decimals: rapidjson's shortest round-trip would print float noise into
// a record a human diffs. A non-finite value (0/0 on an empty window) is 0.
void fixed(Writer& w, double v, int digits = 4) {
  const auto t = std::format("{:.{}f}", std::isfinite(v) ? v : 0.0, digits);
  w.RawValue(t.data(), t.size(), rj::kNumberType);
}

std::string get_str(const rj::Value& o, const char* k) {
  const auto it = o.FindMember(k);
  if (it == o.MemberEnd()) {
    return {};
  }
  if (it->value.IsString()) {
    return {it->value.GetString(), it->value.GetStringLength()};
  }
  if (it->value.IsUint64()) {
    return std::to_string(it->value.GetUint64());
  }
  if (it->value.IsInt64()) {
    return std::to_string(it->value.GetInt64());
  }
  return {};
}

uint64_t get_u64(const rj::Value& o, const char* k) {
  const auto it = o.FindMember(k);
  if (it == o.MemberEnd()) {
    return 0;
  }
  const auto& v = it->value;
  if (v.IsUint64()) {
    return v.GetUint64();
  }
  if (v.IsInt64()) {
    return v.GetInt64() < 0 ? 0 : static_cast<uint64_t>(v.GetInt64());
  }
  if (v.IsDouble()) {
    return v.GetDouble() <= 0 ? 0 : static_cast<uint64_t>(v.GetDouble());
  }
  if (v.IsString()) {
    return std::strtoull(v.GetString(), nullptr, 10);
  }
  return 0;
}

double get_dbl(const rj::Value& o, const char* k, double dflt = 0.0) {
  const auto it = o.FindMember(k);
  if (it == o.MemberEnd() || !it->value.IsNumber()) {
    return dflt;
  }
  return it->value.GetDouble();
}

bool get_bool(const rj::Value& o, const char* k, bool dflt = false) {
  const auto it = o.FindMember(k);
  if (it == o.MemberEnd()) {
    return dflt;
  }
  if (it->value.IsBool()) {
    return it->value.GetBool();
  }
  if (it->value.IsString()) {
    const std::string_view s{it->value.GetString(), it->value.GetStringLength()};
    return s == "true" || s == "1" || s == "on";
  }
  return dflt;
}

const rj::Value* get_obj(const rj::Value& o, const char* k) {
  const auto it = o.FindMember(k);
  return it != o.MemberEnd() && it->value.IsObject() ? &it->value : nullptr;
}

const rj::Value* get_arr(const rj::Value& o, const char* k) {
  const auto it = o.FindMember(k);
  return it != o.MemberEnd() && it->value.IsArray() ? &it->value : nullptr;
}

double clamp01(double v) { return std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : 0.0; }

// The JSON text of any scalar, for canonicalizing an args object.
std::string scalar_text(const rj::Value& v) {
  if (v.IsString()) {
    return {v.GetString(), v.GetStringLength()};
  }
  if (v.IsBool()) {
    return v.GetBool() ? "true" : "false";
  }
  if (v.IsNull()) {
    return "null";
  }
  rj::StringBuffer sb;
  Writer           w(sb);
  v.Accept(w);
  return sb.GetString();
}

Tune_vector default_vector() { return livehd::sim::resolve_tune_defaults(std::nullopt, std::nullopt, std::nullopt, std::nullopt); }

// The class-weight variants of I_s, in report order.
struct Weight_slot {
  std::string_view      name;
  std::optional<double> Stats::* field;
};
constexpr std::array<Weight_slot, 4> kWeights{
    {
     {"ge", &Stats::I_ge},
     {"sites", &Stats::I_sites},
     {"cost", &Stats::I_cost},
     {"cost_flat", &Stats::I_cost_flat},
     }
};

void write_opt(Writer& w, const std::optional<double>& v) {
  if (v) {
    fixed(w, *v);
  } else {
    w.Null();
  }
}

std::optional<double> get_opt_dbl(const rj::Value& o, const char* k) {
  const auto it = o.FindMember(k);
  if (it == o.MemberEnd() || !it->value.IsNumber()) {
    return std::nullopt;
  }
  return it->value.GetDouble();
}

void write_stats(Writer& w, const Stats& s) {
  w.StartObject();
  key(w, "I_s");
  fixed(w, s.I_s);
  key(w, "weight");
  str(w, s.weight);
  for (const auto& ws : kWeights) {
    key(w, std::format("I_{}", ws.name));
    write_opt(w, s.*ws.field);
  }
  key(w, "I");
  fixed(w, s.I);
  key(w, "q");
  fixed(w, s.q);
  key(w, "pairs");
  w.Uint64(s.pairs);
  key(w, "cycles");
  w.Uint64(s.cycles);
  key(w, "postwarm");
  w.Uint64(s.postwarm);
  key(w, "cpu_ms");
  fixed(w, s.cpu_ms, 3);
  key(w, "support");
  w.Bool(s.support);
  key(w, "exact");
  w.Bool(s.exact);
  key(w, "qualifies");
  w.Bool(s.qualifies);
  key(w, "judgeable");
  w.Bool(s.judgeable);
  w.EndObject();
}

// The trial gate: enough post-warm-up cycles and CPU to time the run (no pair
// or 0.2 s floor: a big win makes the trial run short).
bool judgeable(const Stats& s) { return s.postwarm >= cm1::kMinPostWarmCycles && s.cpu_ms >= cm1::kTrialMinCpuMs; }

Stats read_stats(const rj::Value& o) {
  Stats s;
  s.valid    = true;
  s.I_s      = get_dbl(o, "I_s");
  s.weight   = get_str(o, "weight");
  s.I        = get_dbl(o, "I");
  s.q        = get_dbl(o, "q");
  s.pairs    = get_u64(o, "pairs");
  s.cycles   = get_u64(o, "cycles");
  s.postwarm = get_u64(o, "postwarm");
  s.cpu_ms   = get_dbl(o, "cpu_ms");
  s.support  = get_bool(o, "support");
  s.exact    = get_bool(o, "exact");
  for (const auto& ws : kWeights) {
    s.*ws.field = get_opt_dbl(o, std::format("I_{}", ws.name).c_str());
  }
  s.qualifies = get_bool(o, "qualifies");
  // A record written before the trial gate existed: recompute it.
  s.judgeable = o.HasMember("judgeable") ? get_bool(o, "judgeable") : judgeable(s);
  return s;
}

bool qualifies(const Stats& s) {
  return s.pairs >= cm1::kMinPairs && s.postwarm >= cm1::kMinPostWarmCycles && s.cpu_ms >= cm1::kMinCpuMs;
}

// Cycle-weighted mean accumulator of one optional statistic.
struct Mean {
  double w   = 0;
  double sum = 0;
  void   add(double weight, double v) {
    w   += weight;
    sum += weight * v;
  }
  [[nodiscard]] std::optional<double> get() const { return w > 0 ? std::optional<double>{sum / w} : std::nullopt; }
};

// The codegen table lines a tune vector sets (sim_tune_vector.hpp keys): left
// out of the comparability digest, since trial and baseline differ in them BY
// DESIGN.
bool is_vector_line(std::string_view line) {
  return line.starts_with("sim.tune.dirty=") || line.starts_with("sim.tune.fence=") || line.starts_with("sim.tune.live_words=")
         || line.starts_with("sim.tune.backend=");
}

Test_row read_test(const rj::Value& t) {
  Test_row r;
  r.test         = get_str(t, "test");
  r.status       = get_str(t, "status");
  r.sim_cycles   = get_u64(t, "sim_cycles");
  r.cpu_ns       = get_u64(t, "cpu_ns");
  r.cpu_cycles   = get_u64(t, "cpu_cycles");
  r.instructions = get_u64(t, "instructions");
  r.pcore_frac   = get_dbl(t, "pcore_frac", 1.0);
  r.end_digest   = get_str(t, "end_digest");
  r.out_digest   = get_str(t, "out_digest");
  return r;
}

void write_test(Writer& w, const Test_row& r) {
  w.StartObject();
  key(w, "test");
  str(w, r.test);
  key(w, "status");
  str(w, r.status);
  key(w, "sim_cycles");
  w.Uint64(r.sim_cycles);
  key(w, "cpu_ns");
  w.Uint64(r.cpu_ns);
  key(w, "cpu_cycles");
  w.Uint64(r.cpu_cycles);
  key(w, "instructions");
  w.Uint64(r.instructions);
  key(w, "pcore_frac");
  fixed(w, r.pcore_frac);
  key(w, "end_digest");
  str(w, r.end_digest);
  key(w, "out_digest");
  str(w, r.out_digest);
  w.EndObject();
}

std::optional<Run> run_from_record(const rj::Value& d) {
  Run r;
  r.id             = get_str(d, "id");
  r.t              = static_cast<int64_t>(get_u64(d, "t"));
  r.src            = get_str(d, "src");
  r.mode           = get_str(d, "mode");
  r.vector         = get_str(d, "vector");
  r.structure      = get_str(d, "structure");
  r.codegen        = get_str(d, "codegen");
  r.tb             = get_str(d, "tb");
  r.host           = get_str(d, "host");
  r.init_zero      = get_bool(d, "init_zero");
  r.seed           = get_str(d, "seed");
  r.fill           = get_str(d, "fill");
  r.unknown_draws  = get_u64(d, "unknown_draws");
  r.runtime_random = get_bool(d, "runtime_random");
  r.args           = get_str(d, "args");
  r.counters       = get_str(d, "counters");
  r.setup_ms       = get_dbl(d, "setup_ms");
  r.regen          = get_bool(d, "regen");
  if (const auto* sel = get_arr(d, "selected")) {
    for (const auto& s : sel->GetArray()) {
      if (s.IsString()) {
        r.selected.emplace_back(s.GetString(), s.GetStringLength());
      }
    }
  }
  if (const auto* tests = get_arr(d, "tests")) {
    for (const auto& t : tests->GetArray()) {
      if (t.IsObject()) {
        r.tests.push_back(read_test(t));
      }
    }
  }
  if (const auto* s = get_obj(d, "stats")) {
    r.stats = read_stats(*s);
  }
  return r;
}

}  // namespace

// ---- vocabulary -----------------------------------------------------------------

std::optional<Mode> parse_mode(std::string_view v) {
  if (v.empty() || v == "auto") {
    return Mode::auto_;
  }
  if (v == "on") {
    return Mode::on;
  }
  if (v == "off") {
    return Mode::off;
  }
  return std::nullopt;
}

std::string_view mode_name(Mode m) {
  switch (m) {
    case Mode::on   : return "on";
    case Mode::off  : return "off";
    case Mode::auto_: break;
  }
  return "auto";
}

std::string_view source_name(Source s) {
  switch (s) {
    case Source::store    : return "store";
    case Source::file     : return "file";
    case Source::explicit_: return "explicit";
    case Source::dflt     : break;
  }
  return "default";
}

Resolved resolve(const Pins& ex, const Pins& file, const std::optional<Tune_vector>& store) {
  Resolved r;
  if (ex.dirty) {
    r.v.dirty = *ex.dirty;
    r.dirty   = Source::explicit_;
  } else if (file.dirty) {
    r.v.dirty = *file.dirty;
    r.dirty   = Source::file;
  } else if (store) {
    r.v.dirty = store->dirty;
    r.dirty   = Source::store;
  } else {
    r.v.dirty = livehd::sim::kTuneDefaultDirty;
    r.dirty   = Source::dflt;
  }

  if (ex.fence) {
    r.v.fence = *ex.fence;
    r.fence   = Source::explicit_;
  } else if (file.fence) {
    r.v.fence = *file.fence;
    r.fence   = Source::file;
  } else if (store && r.dirty == Source::store) {
    r.v.fence = store->fence;
    r.fence   = Source::store;
  } else {
    r.v.fence = r.v.dirty ? kTuneDefaultFenceRatio : kTuneNoFences;
    r.fence   = Source::dflt;
  }

  if (ex.live_words) {
    r.v.live_words = *ex.live_words;
    r.live_words   = Source::explicit_;
  } else if (file.live_words) {
    r.v.live_words = *file.live_words;
    r.live_words   = Source::file;
  } else if (store) {
    r.v.live_words = store->live_words;
    r.live_words   = Source::store;
  } else {
    r.v.live_words = livehd::sim::kTuneDefaultLiveWords;
    r.live_words   = Source::dflt;
  }

  if (ex.llvm) {
    r.v.llvm  = *ex.llvm;
    r.backend = Source::explicit_;
  } else if (file.llvm) {
    r.v.llvm  = *file.llvm;
    r.backend = Source::file;
  } else if (store) {
    r.v.llvm  = store->llvm;
    r.backend = Source::store;
  } else {
    r.v.llvm  = false;
    r.backend = Source::dflt;
  }
  return r;
}

std::optional<std::string> last_set(const std::vector<std::pair<std::string, std::string>>& sets, std::string_view k) {
  std::optional<std::string> out;
  for (const auto& [sk, sv] : sets) {
    if (sk == k) {
      out = sv;
    }
  }
  return out;
}

Pins pins_from_sets(const std::vector<std::pair<std::string, std::string>>& sets, std::string& err) {
  Pins p;
  for (const auto& [k, v] : sets) {
    if (k == "sim.tune.dirty") {
      const auto kn = livehd::sim::parse_tune_dirty(v);
      if (!kn.ok) {
        err = std::format("sim.tune.dirty {}", kn.err);
        return {};
      }
      p.dirty = kn.value;
    } else if (k == "sim.tune.fence") {
      const auto kn = livehd::sim::parse_tune_fence(v);
      if (!kn.ok) {
        err = std::format("sim.tune.fence {}", kn.err);
        return {};
      }
      p.fence = kn.value;
    } else if (k == "sim.tune.live_words") {
      const auto kn = livehd::sim::parse_tune_live_words(v);
      if (!kn.ok) {
        err = std::format("sim.tune.live_words {}", kn.err);
        return {};
      }
      p.live_words = kn.value;
    } else if (k == "sim.tune.backend") {
      const auto kn = livehd::sim::parse_tune_backend(v);
      if (!kn.ok) {
        err = std::format("sim.tune.backend {}", kn.err);
        return {};
      }
      p.llvm = kn.value;
    }
  }
  return p;
}

std::optional<Rename_hint> renamed_sim_flag(std::string_view flag, std::string_view value) {
  const std::string v{value};
  if (flag == "vcdfakedelay") {
    return Rename_hint{"vcd_fake_delay", v};
  }
  if (flag == "color_dirty") {
    // A copy-pasteable translation into the tri grammar.
    if (v == "true" || v == "1" || v == "on") {
      return Rename_hint{"tune.dirty", "on"};
    }
    if (v == "false" || v == "0" || v == "off") {
      return Rename_hint{"tune.dirty", "off"};
    }
    return Rename_hint{"tune.dirty", v.empty() ? std::string{"auto"} : v};
  }
  if (flag == "fence_ratio") {
    if (v.empty()) {
      return Rename_hint{"tune.fence", "auto"};
    }
    // The old knob took any non-negative number; a ratio past the cap never
    // fenced anything, which the new grammar spells `none`.
    const auto kn = livehd::sim::parse_tune_fence(v);
    if (!kn.ok) {
      char*        end = nullptr;
      const double d   = std::strtod(v.c_str(), &end);
      if (end != v.c_str() && *end == '\0' && d > static_cast<double>(livehd::sim::kTuneMaxNumber)) {
        return Rename_hint{"tune.fence", "none"};
      }
    }
    return Rename_hint{"tune.fence", v};
  }
  if (flag == "live_words") {
    return Rename_hint{"tune.live_words", (v.empty() || v == "0") ? std::string{"auto"} : v};
  }
  if (flag == "backend") {
    return Rename_hint{"tune.backend", v.empty() ? std::string{"auto"} : v};
  }
  return std::nullopt;
}

// ---- runs ---------------------------------------------------------------------

std::optional<Run> run_from_raw(std::string_view raw, std::string& err) {
  rj::Document d;
  d.Parse(raw.data(), raw.size());
  if (d.HasParseError() || !d.IsObject()) {
    err = "not a JSON object";
    return std::nullopt;
  }
  if (get_str(d, "schema") != kRawSchema) {
    err = std::format("schema is not {}", kRawSchema);
    return std::nullopt;
  }
  Run                      r;
  // One tune vector per workdir: every DUT root of one binary bakes the same
  // one. A binary predating the tune-id TU reports none ("").
  std::vector<std::string> vectors;
  std::vector<std::string> structures;
  std::vector<std::string> codegens;  // "<module>:<table minus the tune-vector lines>"
  if (const auto* roots = get_arr(d, "roots")) {
    for (const auto& root : roots->GetArray()) {
      if (!root.IsObject()) {
        continue;
      }
      if (auto v = get_str(root, "vector"); !v.empty()) {
        vectors.push_back(std::move(v));
      }
      const auto module = get_str(root, "module");
      auto       s      = get_str(root, "structure");
      r.roots.emplace_back(module, s);
      if (!s.empty()) {
        structures.push_back(std::move(s));
      }
      const auto table = get_str(root, "codegen");
      if (table.find("plan.runtime_random=true") != std::string::npos) {
        r.runtime_random = true;
      }
      if (!table.empty()) {
        std::string kept;
        for (size_t pos = 0; pos < table.size();) {
          size_t eol = table.find('\n', pos);
          eol        = eol == std::string::npos ? table.size() : eol;
          const std::string_view line{table.data() + pos, eol - pos};
          if (!line.empty() && !is_vector_line(line)) {
            kept += line;
            kept += '\n';
          }
          pos = eol + 1;
        }
        codegens.push_back(std::format("{}:{}", module, kept));
      }
    }
  }
  std::ranges::sort(vectors);
  vectors.erase(std::unique(vectors.begin(), vectors.end()), vectors.end());
  r.vector = vectors.size() == 1 ? vectors.front() : std::string{};
  std::ranges::sort(structures);
  structures.erase(std::unique(structures.begin(), structures.end()), structures.end());
  for (const auto& s : structures) {
    r.structure += r.structure.empty() ? s : "+" + s;
  }
  std::ranges::sort(codegens);
  codegens.erase(std::unique(codegens.begin(), codegens.end()), codegens.end());
  if (!codegens.empty()) {
    std::string all;
    for (const auto& c : codegens) {
      all += c;
      all += '\x1f';
    }
    r.codegen = tune::hex16(tune::fnv1a64(all));
  }
  r.seed          = get_str(d, "seed");
  r.init_zero     = get_bool(d, "init_zero");
  r.fill          = get_str(d, "fill");
  r.unknown_draws = get_u64(d, "unknown_draws");
  r.counters      = get_str(d, "counters");
  if (const auto* sel = get_arr(d, "selected")) {
    for (const auto& s : sel->GetArray()) {
      if (s.IsString()) {
        r.selected.emplace_back(s.GetString(), s.GetStringLength());
      }
    }
  }
  if (const auto* args = get_obj(d, "args")) {
    std::vector<std::string> kv;
    for (const auto& m : args->GetObject()) {
      kv.push_back(std::format("{}={}", std::string_view{m.name.GetString(), m.name.GetStringLength()}, scalar_text(m.value)));
    }
    std::ranges::sort(kv);
    for (const auto& s : kv) {
      r.args += r.args.empty() ? s : "\n" + s;
    }
  }
  if (const auto* host = get_obj(d, "host")) {
    r.host = std::format("{}|{}", get_str(*host, "name"), get_str(*host, "cpu"));
  }

  // Cycle-weighted activity statistics over the profiled tests.
  Stats               s;
  double              w_sum = 0, is_sum = 0, i_sum = 0, q_sum = 0, cpu_ns = 0;
  bool                all_exact = true;
  std::array<Mean, 4> variants;  // kWeights order
  std::string         weight_used;
  if (const auto* tests = get_arr(d, "tests")) {
    for (const auto& t : tests->GetArray()) {
      if (!t.IsObject()) {
        continue;
      }
      r.tests.push_back(read_test(t));
      cpu_ns           += static_cast<double>(r.tests.back().cpu_ns);
      const auto* prof  = get_obj(t, "profile");
      if (prof == nullptr) {
        continue;
      }
      const auto cycles     = r.tests.back().sim_cycles;
      const auto warm       = get_u64(*prof, "warm_cycles");
      // Every profiled test times the run, sampled pairs or not (the trial gate).
      s.postwarm           += cycles > warm ? cycles - warm : 0;
      const uint64_t pairs  = get_u64(*prof, "pairs");
      if (pairs == 0) {
        continue;
      }
      const double                         pd = static_cast<double>(pairs);
      // idle / (pairs * total) under every class weight the driver reports.
      std::array<std::optional<double>, 4> v_t;
      if (const auto* wts = get_obj(*prof, "weights")) {
        for (size_t k = 0; k < kWeights.size(); ++k) {
          const auto* one = get_obj(*wts, std::string{kWeights[k].name}.c_str());
          if (one != nullptr && get_dbl(*one, "total") > 0) {
            v_t[k] = clamp01(get_dbl(*one, "idle") / (pd * get_dbl(*one, "total")));
          }
        }
      }
      double      is_t = -1.0;
      std::string used;
      const auto* sup = get_obj(*prof, "support");
      if (sup != nullptr && get_dbl(*sup, "total_ge") > 0 && !v_t[0]) {
        v_t[0] = clamp01(get_dbl(*sup, "idle_ge") / (pd * get_dbl(*sup, "total_ge")));  // a driver predating `weights`
      }
      for (size_t k = 0; k < kWeights.size(); ++k) {
        if (v_t[k] && kWeights[k].name == cm1::kSupportWeight) {
          is_t = *v_t[k];
          used = kWeights[k].name;
        }
      }
      if (is_t < 0 && v_t[0]) {
        is_t = *v_t[0];
        used = kWeights[0].name;
      }
      if (is_t >= 0) {
        // I_s came from the plan's support tables -- also when every class is
        // pure wiring (total_ge 0) and only the word-cost weights are non-zero.
        s.support = true;
        all_exact = all_exact && sup != nullptr && get_bool(*sup, "exact");
      }
      if (is_t < 0) {
        if (const auto* wk = get_obj(*prof, "walker"); wk != nullptr && get_u64(*wk, "words") > 0) {
          is_t      = clamp01(get_dbl(*wk, "idle_words") / (pd * static_cast<double>(get_u64(*wk, "words"))));
          used      = "walker";
          all_exact = false;
        }
      }
      if (is_t < 0) {
        continue;  // a profile with no activity measure: nothing to weigh
      }
      // One weight names the run's I_s; tests that fell back differently make it "mixed".
      weight_used   = weight_used.empty() || weight_used == used ? used : std::string{"mixed"};
      double ge_sum = 0, ge_idle = 0;
      if (const auto* occ = get_arr(*prof, "occ")) {
        for (const auto& o : occ->GetArray()) {
          if (!o.IsObject()) {
            continue;
          }
          const double ge = get_dbl(o, "ge");
          if (ge <= 0) {
            continue;
          }
          ge_sum  += ge;
          ge_idle += ge * (1.0 - clamp01(static_cast<double>(get_u64(o, "active_pairs")) / pd));
        }
      }
      const double i_t  = ge_sum > 0 ? ge_idle / ge_sum : is_t;
      const double w    = static_cast<double>(std::max<uint64_t>(cycles, 1));
      w_sum            += w;
      is_sum           += w * is_t;
      i_sum            += w * i_t;
      q_sum            += w * clamp01(static_cast<double>(get_u64(*prof, "quiescent_pairs")) / pd);
      for (size_t k = 0; k < kWeights.size(); ++k) {
        if (v_t[k]) {
          variants[k].add(w, *v_t[k]);
        }
      }
      s.pairs  += pairs;
      s.cycles += cycles;
    }
  }
  s.cpu_ms    = cpu_ns / 1e6;
  s.judgeable = judgeable(s);
  if (w_sum > 0) {
    s.valid  = true;
    s.I_s    = is_sum / w_sum;
    s.weight = weight_used;
    for (size_t k = 0; k < kWeights.size(); ++k) {
      s.*kWeights[k].field = variants[k].get();
    }
    s.I         = i_sum / w_sum;
    s.q         = q_sum / w_sum;
    s.exact     = s.support && all_exact;
    s.qualifies = qualifies(s);
  }
  r.stats = s;
  return r;
}

std::string run_record(const Run& r) {
  rj::StringBuffer sb;
  Writer           w(sb);
  w.StartObject();
  key(w, "schema");
  str(w, kStoreSchema);
  key(w, "kind");
  str(w, "run");
  key(w, "id");
  str(w, r.id);
  key(w, "t");
  w.Int64(r.t);
  key(w, "src");
  str(w, r.src);
  key(w, "mode");
  str(w, r.mode);
  key(w, "vector");
  str(w, r.vector);
  key(w, "structure");
  str(w, r.structure);
  key(w, "codegen");
  str(w, r.codegen);
  key(w, "tb");
  str(w, r.tb);
  key(w, "host");
  str(w, r.host);
  key(w, "init_zero");
  w.Bool(r.init_zero);
  key(w, "seed");
  str(w, r.seed);
  key(w, "fill");
  str(w, r.fill);
  key(w, "unknown_draws");
  w.Uint64(r.unknown_draws);
  key(w, "runtime_random");
  w.Bool(r.runtime_random);
  key(w, "selected");
  w.StartArray();
  for (const auto& s : r.selected) {
    str(w, s);
  }
  w.EndArray();
  key(w, "args");
  str(w, r.args);
  key(w, "counters");
  str(w, r.counters);
  key(w, "setup_ms");
  fixed(w, r.setup_ms, 3);
  key(w, "regen");
  w.Bool(r.regen);
  key(w, "tests");
  w.StartArray();
  for (const auto& t : r.tests) {
    write_test(w, t);
  }
  w.EndArray();
  key(w, "stats");
  if (r.stats.valid) {
    write_stats(w, r.stats);
  } else {
    w.Null();
  }
  w.EndObject();
  return sb.GetString();
}

namespace {
// The kSupportWeight slot of kWeights.
const std::optional<double>& support_variant(const Stats& s) {
  for (const auto& ws : kWeights) {
    if (ws.name == cm1::kSupportWeight) {
      return s.*ws.field;
    }
  }
  static const std::optional<double> none;
  return none;
}
}  // namespace

std::optional<double> ladder_I(const Stats& s) {
  if (!s.valid) {
    return std::nullopt;
  }
  if (const auto& v = support_variant(s)) {
    return *v;
  }
  // No stored variant: I_s itself, when it was computed under this model's
  // weight (a record with no `weight` predates the variants and used GE) or
  // by the walker (a plan without support tables has no weights at all).
  const std::string_view w = s.weight.empty() ? std::string_view{"ge"} : std::string_view{s.weight};
  if (w == cm1::kSupportWeight || w == "walker") {
    return s.I_s;
  }
  return std::nullopt;
}

Stats window_stats(const std::vector<const Run*>& runs) {
  Stats               s;
  double              w_sum = 0, i_sum = 0, q_sum = 0;
  Mean                is_mean;
  bool                any_table = false, any_walker = false;
  bool                all_exact = true;
  std::array<Mean, 4> variants;
  for (const auto* r : runs) {
    if (r == nullptr || !r->stats.valid) {
      continue;
    }
    const double w  = static_cast<double>(std::max<uint64_t>(r->stats.cycles, 1));
    w_sum          += w;
    // Only idleness under this model's class weight is averaged: a run
    // recorded under another weight adds nothing to I_s.
    if (const auto li = ladder_I(r->stats)) {
      is_mean.add(w, *li);
      (!support_variant(r->stats) && r->stats.weight == "walker" ? any_walker : any_table) = true;
    }
    i_sum += w * r->stats.I;
    q_sum += w * r->stats.q;
    for (size_t k = 0; k < kWeights.size(); ++k) {
      if (const auto& v = r->stats.*kWeights[k].field) {
        variants[k].add(w, *v);
      }
    }
    s.pairs    += r->stats.pairs;
    s.cycles   += r->stats.cycles;
    s.postwarm += r->stats.postwarm;
    s.cpu_ms   += r->stats.cpu_ms;
    s.support   = s.support || r->stats.support;
    all_exact   = all_exact && r->stats.exact;
  }
  s.judgeable = judgeable(s);
  if (w_sum > 0) {
    s.valid  = true;
    s.I_s    = is_mean.get().value_or(0.0);
    s.weight = any_table ? std::string{cm1::kSupportWeight} : any_walker ? std::string{"walker"} : std::string{};
    for (size_t k = 0; k < kWeights.size(); ++k) {
      s.*kWeights[k].field = variants[k].get();
    }
    s.I         = i_sum / w_sum;
    s.q         = q_sum / w_sum;
    s.exact     = s.support && all_exact;
    s.qualifies = qualifies(s);
  }
  return s;
}

// ---- the store ------------------------------------------------------------------

bool State::is_rejected(std::string_view tv) const { return std::ranges::find(rejected, tv) != rejected.end(); }

int State::attempts(std::string_view to, std::string_view structure) const {
  return static_cast<int>(
      std::ranges::count_if(closed, [&](const Closed& c) { return c.charged && c.to == to && c.structure == structure; }));
}

int State::failures(std::string_view to, std::string_view structure) const {
  return static_cast<int>(std::ranges::count_if(closed, [&](const Closed& c) {
    return c.to == to && c.structure == structure && (c.result == "build-failed" || c.result == "run-failed");
  }));
}

int State::attempts_on(std::string_view to, std::string_view structure) const {
  return static_cast<int>(std::ranges::count_if(closed, [&](const Closed& c) {
    return c.charged && c.mode == "on" && c.to == to && c.structure == structure;
  }));
}

namespace {
// A plan-random verdict is final for the structure: another attempt would draw
// the same memory contents.
bool random_ineligible(const State& st, std::string_view to, std::string_view structure) {
  return std::ranges::any_of(st.closed, [&](const State::Closed& c) {
    return c.to == to && c.structure == structure && c.result == "random-ineligible";
  });
}
}  // namespace

bool State::exhausted(std::string_view to, std::string_view structure) const {
  return random_ineligible(*this, to, structure) || attempts(to, structure) >= cm1::kMaxTrialAttempts;
}

bool State::exhausted_on(std::string_view to, std::string_view structure) const {
  return random_ineligible(*this, to, structure) || attempts_on(to, structure) >= cm1::kMaxTrialAttempts;
}

std::optional<size_t> State::last_closed_seq(std::string_view to) const {
  std::optional<size_t> s;
  for (const auto& c : closed) {
    if (c.to == to) {
      s = std::max(s.value_or(0), c.seq);
    }
  }
  return s;
}

void apply_record(State& st, std::string_view line) {
  const size_t seq = st.seq++;
  rj::Document d;
  d.Parse(line.data(), line.size());
  if (d.HasParseError() || !d.IsObject()) {
    return;
  }
  const auto kind = get_str(d, "kind");
  if (kind == "run") {
    if (auto r = run_from_record(d)) {
      st.runs.push_back(std::move(*r));
      st.run_seq.push_back(seq);
    }
  } else if (kind == "trial") {
    Trial t;
    t.from          = get_str(d, "from");
    t.to            = get_str(d, "to");
    t.step          = get_str(d, "step");
    t.gate          = get_str(d, "gate");
    t.t             = static_cast<int64_t>(get_u64(d, "t"));
    t.seq           = seq;
    st.trial        = std::move(t);
    st.attempt_open = false;
    st.attempt_mode.clear();
    st.converged = false;
  } else if (kind == "attempt") {
    // A setup applied the pending trial (an attempt of an older trial, or of
    // none, is ignored).
    if (st.trial && st.trial->to == get_str(d, "to") && st.trial->from == get_str(d, "from")) {
      st.attempt_open = true;
      st.attempt_seq  = seq;
      st.attempt_mode = get_str(d, "mode");
    }
  } else if (kind == "verdict") {
    Verdict v;
    v.from      = get_str(d, "from");
    v.to        = get_str(d, "to");
    v.result    = get_str(d, "result");
    v.oracle    = get_str(d, "oracle");
    v.reason    = get_str(d, "reason");
    v.structure = get_str(d, "structure");
    v.mode      = get_str(d, "mode");
    if (v.mode.empty() && st.attempt_open && st.trial && st.trial->to == v.to) {
      v.mode = st.attempt_mode;  // a record written before verdicts carried the mode
    }
    v.rho          = get_dbl(d, "rho");
    v.rho_c        = get_dbl(d, "rho_c");
    v.rho_i        = get_dbl(d, "rho_i");
    v.decided_on_i = get_bool(d, "decided_on_i");
    // Before `charged` was recorded, every non-stale closure was one attempt.
    v.charged      = d.HasMember("charged") ? get_bool(d, "charged") : !v.reason.starts_with("stale");
    if (const auto* tests = get_arr(d, "tests")) {
      for (const auto& t : tests->GetArray()) {
        if (t.IsString()) {
          v.tests.emplace_back(t.GetString(), t.GetStringLength());
        }
      }
    }
    st.trial.reset();  // at most one trial is ever pending; a verdict closes it
    st.attempt_open = false;
    st.attempt_mode.clear();
    st.closed.push_back(State::Closed{v.to, v.structure, v.result, v.mode, v.charged, seq});
    // A failure bans only once it repeats on one structure (environmental
    // failures -- a bad $CXX, a full disk -- must not ban on the first hit).
    const bool ban = v.bans() || (v.failure() && st.failures(v.to, v.structure) >= cm1::kMaxTrialAttempts);
    if (ban && !st.is_rejected(v.to)) {
      st.rejected.push_back(v.to);
    }
    if (v.result == "accepted") {
      st.accepted.push_back(v.to);
      ++st.flips;
    }
    st.last_verdict = std::move(v);
  } else if (kind == "decision") {
    st.incumbent        = Tune_vector::parse(get_str(d, "vector"));
    st.converged        = get_bool(d, "converged");
    st.converged_reason = get_str(d, "reason");
    if (st.converged) {
      st.flips = 0;  // a profile generation ends at convergence
    }
  }
}

State replay(const std::vector<std::string>& lines) {
  State st;
  for (const auto& l : lines) {
    apply_record(st, l);
  }
  return st;
}

std::string trial_record(const Trial& tr) {
  rj::StringBuffer sb;
  Writer           w(sb);
  w.StartObject();
  key(w, "schema");
  str(w, kStoreSchema);
  key(w, "kind");
  str(w, "trial");
  key(w, "t");
  w.Int64(tr.t);
  key(w, "model");
  str(w, cm1::kName);
  key(w, "from");
  str(w, tr.from);
  key(w, "to");
  str(w, tr.to);
  key(w, "step");
  str(w, tr.step);
  key(w, "gate");
  str(w, tr.gate);
  w.EndObject();
  return sb.GetString();
}

std::string attempt_record(const Trial& tr, int64_t t, Mode m) {
  rj::StringBuffer sb;
  Writer           w(sb);
  w.StartObject();
  key(w, "schema");
  str(w, kStoreSchema);
  key(w, "kind");
  str(w, "attempt");
  key(w, "t");
  w.Int64(t);
  key(w, "model");
  str(w, cm1::kName);
  key(w, "from");
  str(w, tr.from);
  key(w, "to");
  str(w, tr.to);
  key(w, "step");
  str(w, tr.step);
  key(w, "mode");
  str(w, mode_name(m));
  w.EndObject();
  return sb.GetString();
}

std::string verdict_record(const Verdict& v, int64_t t) {
  rj::StringBuffer sb;
  Writer           w(sb);
  w.StartObject();
  key(w, "schema");
  str(w, kStoreSchema);
  key(w, "kind");
  str(w, "verdict");
  key(w, "t");
  w.Int64(t);
  key(w, "model");
  str(w, cm1::kName);
  key(w, "from");
  str(w, v.from);
  key(w, "to");
  str(w, v.to);
  key(w, "result");
  str(w, v.result);
  key(w, "oracle");
  str(w, v.oracle);
  key(w, "reason");
  str(w, v.reason);
  key(w, "structure");
  str(w, v.structure);
  key(w, "mode");
  str(w, v.mode);
  key(w, "charged");
  w.Bool(v.charged);
  key(w, "rho");
  fixed(w, v.rho);
  key(w, "rho_c");
  fixed(w, v.rho_c);
  key(w, "rho_i");
  fixed(w, v.rho_i);
  key(w, "decided_on_i");
  w.Bool(v.decided_on_i);
  key(w, "tests");
  w.StartArray();
  for (const auto& s : v.tests) {
    str(w, s);
  }
  w.EndArray();
  w.EndObject();
  return sb.GetString();
}

std::string decision_record(const Tune_vector& v, bool converged, std::string_view reason, int64_t t) {
  rj::StringBuffer sb;
  Writer           w(sb);
  w.StartObject();
  key(w, "schema");
  str(w, kStoreSchema);
  key(w, "kind");
  str(w, "decision");
  key(w, "t");
  w.Int64(t);
  key(w, "model");
  str(w, cm1::kName);
  key(w, "vector");
  str(w, v.tv1());
  key(w, "converged");
  w.Bool(converged);
  key(w, "reason");
  str(w, reason);
  w.EndObject();
  return sb.GetString();
}

std::vector<std::string> compact(const std::vector<std::string>& lines) {
  struct Rec {
    std::string kind;  // "" for a line that does not parse (kept verbatim)
    std::string to;
    std::string result;
    std::string structure;
    bool        converged = false;
  };
  const size_t     n = lines.size();
  std::vector<Rec> rec(n);
  for (size_t i = 0; i < n; ++i) {
    rj::Document d;
    d.Parse(lines[i].data(), lines[i].size());
    if (d.HasParseError() || !d.IsObject()) {
      continue;
    }
    auto& r = rec[i];
    r.kind  = get_str(d, "kind");
    if (r.kind == "run") {
      r.structure = get_str(d, "structure");
    } else if (r.kind == "verdict") {
      r.to        = get_str(d, "to");
      r.result    = get_str(d, "result");
      r.structure = get_str(d, "structure");
    } else if (r.kind == "decision") {
      r.converged = get_bool(d, "converged");
    }
  }

  std::vector<bool>     keep(n, false);
  // The newest runs; their structures are the ones whose budgets still matter.
  std::set<std::string> live;
  size_t                runs = 0;
  for (size_t i = n; i-- > 0;) {
    if (rec[i].kind == "run" && runs < cm1::kKeepRuns) {
      keep[i] = true;
      ++runs;
      live.insert(rec[i].structure);
    }
  }
  std::optional<size_t>                              last_decision, last_converged, last_trial, last_verdict;
  std::map<std::string, size_t>                      newest_verdict, newest_accepted;
  std::map<std::pair<std::string, std::string>, int> failures;
  const auto is_failure = [](const Rec& r) { return r.result == "build-failed" || r.result == "run-failed"; };
  for (size_t i = 0; i < n; ++i) {
    const auto& r = rec[i];
    if (r.kind == "decision") {
      last_decision = i;
      if (r.converged) {
        last_converged = i;
      }
    } else if (r.kind == "trial") {
      last_trial = i;
    } else if (r.kind == "verdict") {
      last_verdict         = i;
      newest_verdict[r.to] = i;
      if (r.result == "accepted") {
        newest_accepted[r.to] = i;
      }
      if (is_failure(r)) {
        ++failures[{r.to, r.structure}];
      }
    }
  }
  // The incumbent and convergence (the newest decision) and where the flip
  // count restarts (the newest converged one).
  for (const auto& d : {last_decision, last_converged}) {
    if (d) {
      keep[*d] = true;
    }
  }
  // The pending trial and its attempts; a closed trial's records replay to nothing.
  if (last_trial && (!last_verdict || *last_verdict < *last_trial)) {
    for (size_t i = *last_trial; i < n; ++i) {
      keep[i] = keep[i] || rec[i].kind == "trial" || rec[i].kind == "attempt";
    }
  }
  for (size_t i = 0; i < n; ++i) {
    const auto& r = rec[i];
    if (r.kind == "verdict") {
      keep[i] = r.result == "rejected" || r.result == "divergence"  // bans
                || newest_verdict[r.to] == i                        // the fresh-baseline rule
                || live.contains(r.structure)                       // attempt budgets that still apply
                || (r.result == "accepted" && (newest_accepted[r.to] == i || !last_converged || i > *last_converged))
                || (is_failure(r) && failures[{r.to, r.structure}] >= cm1::kMaxTrialAttempts);  // a failure ban
    } else if (r.kind != "run" && r.kind != "decision" && r.kind != "trial" && r.kind != "attempt") {
      keep[i] = true;  // a record kind this lhd does not know: a newer lhd's, left alone
    }
  }
  std::vector<std::string> out;
  for (size_t i = 0; i < n; ++i) {
    if (keep[i]) {
      out.push_back(lines[i]);
    }
  }
  return out;
}

// ---- verdict ----------------------------------------------------------------------

bool comparable(const Run& a, const Run& b) {
  if (a.structure != b.structure || a.codegen != b.codegen || a.tb != b.tb || a.host != b.host || a.init_zero != b.init_zero
      || a.seed != b.seed || a.fill != b.fill || a.selected != b.selected || a.args != b.args) {
    return false;
  }
  for (const auto& ta : a.tests) {
    for (const auto& tb : b.tests) {
      if (ta.test == tb.test && ta.sim_cycles > 0 && tb.sim_cycles > 0) {
        return true;
      }
    }
  }
  return false;
}

const Run* find_baseline(const State& st, std::string_view from, const Run& trial_run) {
  size_t limit = st.runs.size();
  for (size_t i = 0; i < st.runs.size(); ++i) {
    if (&st.runs[i] == &trial_run) {
      limit = i;
      break;
    }
  }
  for (size_t i = limit; i-- > 0;) {
    const Run& r = st.runs[i];
    if (r.vector == from && r.stats.qualifies && comparable(r, trial_run)) {
      return &r;
    }
  }
  return nullptr;
}

namespace {
struct Test_pair {
  const Test_row* t;
  const Test_row* b;
};
// The tests both runs executed, in the trial's order.
std::vector<Test_pair> common_tests(const Run& tr, const Run& base) {
  std::vector<Test_pair> common;
  for (const auto& t : tr.tests) {
    for (const auto& b : base.tests) {
      if (t.test == b.test) {
        common.push_back({&t, &b});
        break;
      }
    }
  }
  return common;
}
}  // namespace

Verdict judge_oracle(const Run& tr, const Run& base) {
  Verdict v;
  v.from                 = base.vector;
  v.to                   = tr.vector;
  // Random fill with a drawn `?` makes a digest difference legal
  // nondeterminism (ruling 10): skip the check, decide on speed alone.
  const bool random_draw = (tr.fill == "random" && tr.unknown_draws > 0) || (base.fill == "random" && base.unknown_draws > 0);
  if (random_draw) {
    v.oracle = "skipped(random-fill)";
    return v;
  }
  const auto common = common_tests(tr, base);
  v.oracle          = "equal";
  for (const auto& p : common) {
    if (p.t->end_digest != p.b->end_digest || p.t->out_digest != p.b->out_digest || p.t->status != p.b->status) {
      v.tests.push_back(p.t->test);
    }
  }
  if (!v.tests.empty()) {
    v.oracle = "mismatch";
    v.result = "divergence";
    v.reason = std::format("{} of {} common test(s) differ from the baseline", v.tests.size(), common.size());
  }
  return v;
}

Verdict judge_speed(const Run& tr, const Run& base, Verdict v) {
  // CPU-share-weighted per-cycle cost ratios on cycles and on instructions.
  // Weights are the baseline's CPU shares.
  const auto common   = common_tests(tr, base);
  double     base_cpu = 0;
  for (const auto& p : common) {
    if (p.t->sim_cycles > 0 && p.b->sim_cycles > 0) {
      base_cpu += static_cast<double>(p.b->cpu_ns);
    }
  }
  double rc = 0, ri = 0, wc = 0, wi = 0, pc_t = 0, pc_b = 0, wp = 0;
  for (const auto& p : common) {
    if (p.t->sim_cycles == 0 || p.b->sim_cycles == 0) {
      continue;
    }
    const double w  = base_cpu > 0 ? static_cast<double>(p.b->cpu_ns) / base_cpu : 1.0;
    const double ct = static_cast<double>(p.t->sim_cycles);
    const double cb = static_cast<double>(p.b->sim_cycles);
    // cycles per simulated cycle; CPU ns when the counters give no cycles
    const bool   cy = p.t->cpu_cycles > 0 && p.b->cpu_cycles > 0;
    const double nt = cy ? static_cast<double>(p.t->cpu_cycles) : static_cast<double>(p.t->cpu_ns);
    const double nb = cy ? static_cast<double>(p.b->cpu_cycles) : static_cast<double>(p.b->cpu_ns);
    if (nb > 0) {
      rc += w * ((nt / ct) / (nb / cb));
      wc += w;
    }
    if (p.t->instructions > 0 && p.b->instructions > 0) {
      ri += w * ((static_cast<double>(p.t->instructions) / ct) / (static_cast<double>(p.b->instructions) / cb));
      wi += w;
    }
    pc_t += w * p.t->pcore_frac;
    pc_b += w * p.b->pcore_frac;
    wp   += w;
  }
  v.rho_c = wc > 0 ? rc / wc : 0.0;
  v.rho_i = wi > 0 ? ri / wi : 0.0;
  if (wc <= 0 && wi <= 0) {
    v.result = "abandoned";
    v.reason = "no common test with a CPU measurement";
    return v;
  }
  const bool noisy_cores = wp > 0 && (pc_t / wp < cm1::kMinPcore || pc_b / wp < cm1::kMinPcore);
  v.decided_on_i         = wi > 0 && (wc <= 0 || noisy_cores || std::abs(v.rho_c - v.rho_i) > cm1::kMaxRhoDisagree);
  v.rho                  = v.decided_on_i ? v.rho_i : v.rho_c;
  v.result               = v.rho <= cm1::kAcceptRho ? "accepted" : "rejected";
  v.reason               = std::format("rho={:.3f} on {} ({} ratio {} {:.2f})",
                                       v.rho,
                                       v.decided_on_i ? "instructions" : "cycles",
                                       v.result == "accepted" ? "accept" : "reject",
                                       v.result == "accepted" ? "<=" : ">",
                                       cm1::kAcceptRho);
  return v;
}

Verdict judge(const Run& tr, const Run& base) {
  auto v = judge_oracle(tr, base);  // oracle first
  if (v.result == "divergence") {
    return v;
  }
  return judge_speed(tr, base, std::move(v));  // then speed
}

// ---- policy -----------------------------------------------------------------------

double econ_rebuild_ms(const State& st) {
  for (auto it = st.runs.rbegin(); it != st.runs.rend(); ++it) {
    if (it->regen && it->setup_ms > 0) {
      return it->setup_ms;
    }
  }
  for (const auto& r : st.runs) {
    if (r.setup_ms > 0) {
      return r.setup_ms;
    }
  }
  return 0.0;
}

std::vector<const Run*> ladder_window(const State& st, std::string_view structure) {
  std::vector<const Run*> win;
  for (auto it = st.runs.rbegin(); it != st.runs.rend() && win.size() < cm1::kWindow; ++it) {
    if (it->stats.qualifies && it->structure == structure && ladder_I(it->stats)) {
      win.push_back(&*it);
    }
  }
  return win;
}

Proposal propose(const State& st, const Context& cx) {
  Proposal p;
  if (st.runs.empty()) {
    return p;  // nothing measured yet: keep profiling
  }
  const Run& latest = st.runs.back();

  // The window: the newest qualifying runs of the latest structure measured
  // under this model's class weight. Stats are vector-invariant (state
  // hashes), so runs of every vector merge.
  const std::vector<const Run*> win = ladder_window(st, latest.structure);
  if (win.empty()) {
    int smoke = 0;
    for (auto it = st.runs.rbegin(); it != st.runs.rend() && !it->stats.qualifies; ++it) {
      ++smoke;
    }
    if (smoke >= cm1::kSmokeRuns) {
      p.converge_reason = "smoke-only";  // short runs never decide anything; stop sampling them
    }
    return p;
  }
  if (latest.runtime_random) {
    p.converge_reason = "runtime-random";
    return p;
  }
  if (st.flips >= cm1::kMaxFlips) {
    p.converge_reason = "flip-limit";
    return p;
  }

  const Stats       w            = window_stats(win);
  const Resolved    cur          = cx.current(st);
  const Tune_vector v            = cur.v;
  // A knob pinned by an explicit --set or by sim.tune.file is frozen.
  const bool        dirty_pinned = cur.dirty == Source::explicit_ || cur.dirty == Source::file;
  const bool        fence_pinned = cur.fence == Source::explicit_ || cur.fence == Source::file;
  const bool        busy         = w.I_s < cm1::kBusyIdle && w.q < cm1::kBusyQuiescent;

  // The ladder, one lever per step (ruling I7), around the built-in L1
  // (sim_profile.md ruling 11, 2026-09-19):
  //   L0 d=off f=none  <-  L1 d=on f=16 (default)  ->  L2 d=on f=0
  // Both directions are ordinary trials, in `auto` and in `on`, decided by the
  // same calibrated threshold: dirty gating pays when I_s >= kL1Idle (or q >=
  // kL1Quiescent), so a design below both leaves any dirty-gated vector for
  // L0, one at or above it leaves L0 for L1, and a very idle one on L1 goes on
  // to L2. The two directions are complements for the same window, so they
  // cannot oscillate; a rejected step stays rejected and kMaxFlips bounds the
  // rest (a workload that really changes).
  std::optional<Tune_vector> to;
  bool                       lever_dirty = true;  // the knob the step exists to move
  bool                       step_down   = false;
  const bool                 l1_pays     = w.I_s >= cm1::kL1Idle || w.q >= cm1::kL1Quiescent;
  if (!v.dirty) {
    if (l1_pays) {
      to     = Tune_vector{true, fence_pinned ? v.fence : kTuneDefaultFenceRatio, v.live_words, v.llvm};
      p.step = "L1";
    }
  } else if (!l1_pays) {
    to        = Tune_vector{false, fence_pinned ? v.fence : kTuneNoFences, v.live_words, v.llvm};
    p.step    = "L0";
    step_down = true;
  } else if (v.fence == kTuneDefaultFenceRatio && w.I_s >= cm1::kL2Idle) {
    to          = Tune_vector{true, 0, v.live_words, v.llvm};
    p.step      = "L2";
    lever_dirty = false;
  }
  if (!to) {
    p.converge_reason = busy ? "busy" : "no-step";
    return p;
  }
  // A step whose lever is pinned only moves pinned knobs: it does not exist.
  if ((lever_dirty && dirty_pinned) || (!lever_dirty && fence_pinned) || *to == v) {
    p.to.reset();
    p.step.clear();
    p.converge_reason = "pinned";
    return p;
  }
  if (st.is_rejected(to->tv1())) {
    p.step.clear();
    p.converge_reason = "rejected";
    return p;
  }
  // kMaxTrialAttempts unfinished attempts of this step on this structure:
  // `auto` stops proposing it (a new structure re-opens it). An explicit `on`
  // re-opens it once -- its own attempts get the same budget -- so neither
  // mode ever re-applies an incomparable trial forever.
  if (cx.mode == Mode::on ? st.exhausted_on(to->tv1(), latest.structure) : st.exhausted(to->tv1(), latest.structure)) {
    p.step.clear();
    p.converge_reason = "exhausted";
    return p;
  }
  // A trial is judged against the incumbent under the SAME conditions, so it
  // needs a qualifying run of the incumbent vector on this structure -- newer
  // than the last verdict on this step: after an abandoned attempt the next
  // setup builds the incumbent again, and that run becomes the baseline for
  // whatever changed (an edit, other args, a new testbench).
  {
    const auto from_tv = v.tv1();
    const auto since   = st.last_closed_seq(to->tv1());
    bool       base    = false;
    int        waited  = 0;  // incumbent runs since then that did not qualify
    for (size_t i = 0; i < st.runs.size() && !base; ++i) {
      const Run& r = st.runs[i];
      if ((since && st.run_seq[i] <= *since) || r.vector != from_tv || r.structure != latest.structure) {
        continue;
      }
      base    = r.stats.qualifies;
      waited += base ? 0 : 1;
    }
    if (!base) {
      p.step.clear();
      if (waited >= cm1::kSmokeRuns) {
        p.converge_reason = "smoke-only";  // the incumbent's runs are too short to be a baseline
      } else {
        p.gate = "wait(no qualifying incumbent run on this structure yet)";
      }
      return p;
    }
  }

  // Payoff gate (auto only; an explicit `on` always trials): the predicted
  // saving over the expected remaining runs must pay for the root rebuild.
  if (cx.mode == Mode::auto_) {
    std::vector<double> cpu;
    for (const auto* r : win) {
      cpu.push_back(r->stats.cpu_ms);
    }
    std::ranges::sort(cpu);
    const double median = cpu.empty() ? 0.0 : cpu[cpu.size() / 2];
    const double econ   = econ_rebuild_ms(st);
    // Up, the idle share is what dirty gating can skip. Down, the busy share
    // is what its compares and marks cost for nothing -- an optimistic
    // estimate (busy designs measured 8-72% cheaper at L0, calibration
    // table), which only decides whether to spend a trial: the verdict still
    // needs the measured kAcceptRho. The always-toggling regime passes
    // outright, as a very idle design does going up.
    const double gain   = cm1::kGainPerIdle * (step_down ? 1.0 - w.I_s : w.I_s);
    const double payoff = gain * median * cm1::kExpectedRuns;
    const bool   pass   = (step_down ? busy : w.I_s > cm1::kGateIdle) || payoff >= econ;
    p.gate              = std::format("{}(I_s={:.2f} gain={:.2f} x cpu={:.0f}ms x {:.0f} = {:.0f}ms {} rebuild={:.0f}ms)",
                                      pass ? "pass" : "fail",
                                      w.I_s,
                                      gain,
                                      median,
                                      cm1::kExpectedRuns,
                                      payoff,
                                      payoff >= econ ? ">=" : "<",
                                      econ);
    if (!pass) {
      p.step.clear();
      p.converge_reason = "gate";
      return p;
    }
  } else {
    p.gate = "on(always)";
  }
  p.to = to;
  return p;
}

std::vector<std::string> close_trial(State& st, Verdict v, int64_t now) {
  if (!st.trial) {
    return {};
  }
  const Trial tr = *st.trial;
  v.from         = tr.from;
  v.to           = tr.to;
  if (v.mode.empty() && st.attempt_open) {
    v.mode = st.attempt_mode;
  }
  if (v.structure.empty()) {
    // The structure the attempt ran on: its own run's, else the newest run's
    // (a build that failed never reported one).
    for (size_t i = st.runs.size(); st.attempt_open && i-- > 0;) {
      if (st.run_seq[i] > st.attempt_seq && st.runs[i].vector == tr.to) {
        v.structure = st.runs[i].structure;
        break;
      }
    }
    if (v.structure.empty() && !st.runs.empty()) {
      v.structure = st.runs.back().structure;
    }
  }
  std::vector<std::string> out;
  auto                     push = [&](std::string line) {
    apply_record(st, line);
    out.push_back(std::move(line));
  };
  push(verdict_record(v, now));
  // The store's incumbent moves only the knobs a trial moves (dirty and
  // fence); live_words and backend stay what the store had.
  Tune_vector inc = st.incumbent.value_or(default_vector());
  if (v.result == "accepted") {
    if (const auto a = Tune_vector::parse(tr.to)) {
      inc.dirty = a->dirty;
      inc.fence = a->fence;
    }
  }
  push(decision_record(inc, false, "", now));
  return out;
}

std::optional<std::string> stale_reason(const State& st, const Context& cx, bool built_trial) {
  if (!st.trial) {
    return std::nullopt;
  }
  const auto from = Tune_vector::parse(st.trial->from);
  const auto to   = Tune_vector::parse(st.trial->to);
  if (!from || !to) {
    return "stale: the trial names an unreadable vector";
  }
  const Resolved cur    = cx.current(st);
  const auto     pinned = [](Source s) { return s == Source::explicit_ || s == Source::file; };
  if (built_trial) {
    // The binary is already built: only a pin that contradicts it matters (an
    // agreeing one just restates what the trial tree baked). These are the
    // values drv.bin itself checks: explicit over file, knob by knob.
    const auto& ex = cx.explicit_pins;
    const auto& fp = cx.file_pins;
    const auto  d  = ex.dirty ? ex.dirty : fp.dirty;
    const auto  f  = ex.fence ? ex.fence : fp.fence;
    const auto  lw = ex.live_words ? ex.live_words : fp.live_words;
    const auto  be = ex.llvm ? ex.llvm : fp.llvm;
    if ((d && *d != to->dirty) || (f && *f != to->fence) || (lw && *lw != to->live_words) || (be && *be != to->llvm)) {
      return std::format("stale: a pinned knob contradicts the built trial vector {}", to->tv1());
    }
    return std::nullopt;
  }
  if ((from->dirty != to->dirty && pinned(cur.dirty)) || (from->fence != to->fence && pinned(cur.fence))
      || (from->live_words != to->live_words && pinned(cur.live_words)) || (from->llvm != to->llvm && pinned(cur.backend))) {
    return "stale: a knob the trial moves is now pinned";
  }
  if (cur.v != *from) {
    return std::format("stale: the resolved vector is now {}", cur.v.tv1());
  }
  return std::nullopt;
}

Outcome decide(State& st, const Context& cx) {
  Outcome out;
  auto    push = [&](std::string line) {
    apply_record(st, line);
    out.append.push_back(std::move(line));
  };
  bool closed_unfinished = false;

  // 1. Judge the OPEN attempt on the runs of its vector that have a comparable
  //    baseline: the ORACLE on any of them (a digest or status mismatch is
  //    authoritative however short the run -- a miscompile that ends or skips
  //    work early makes exactly such a run), the speed verdict only on a
  //    judgeable one. When the session says the attempt is over and no run
  //    decided it, close it as failed or abandoned.
  if (st.trial && st.attempt_open) {
    const Trial            tr = *st.trial;
    std::optional<Verdict> v;
    for (size_t i = 0; i < st.runs.size(); ++i) {
      if (st.run_seq[i] <= st.attempt_seq) {
        continue;
      }
      const Run& r = st.runs[i];
      if (r.vector != tr.to) {
        continue;
      }
      if (r.runtime_random) {  // a property of the plan, not of the run's length
        v = Verdict{.result = "random-ineligible", .oracle = "none", .reason = "the plan draws runtime_random memory contents"};
        v->structure = r.structure;
        break;
      }
      if (!r.stats.judgeable && !cx.attempt_failed.empty()) {
        continue;  // this invocation's run did not finish normally: judge nothing on a short record
      }
      const Run* base = find_baseline(st, tr.from, r);
      if (base == nullptr) {
        continue;
      }
      auto o = judge_oracle(r, *base);
      if (o.result == "divergence") {
        v            = std::move(o);
        v->structure = r.structure;
        break;
      }
      if (!r.stats.judgeable) {
        continue;  // equal (or random-fill) results, but too short to time
      }
      v            = judge_speed(r, *base, std::move(o));
      v->structure = r.structure;
      break;
    }
    const bool from_run = v.has_value();
    if (!v && cx.attempt_over) {
      v = Verdict{
          .result = cx.attempt_failed.empty() ? std::string{"abandoned"} : cx.attempt_failed,
          .oracle = "none",
          .reason = cx.attempt_why.empty() ? std::string{"no comparable qualifying run of the trial vector"} : cx.attempt_why};
    }
    if (v) {
      v->charged        = from_run || cx.attempt_charged;
      closed_unfinished = !v->final_();
      for (auto& line : close_trial(st, *v, cx.now)) {
        out.append.push_back(std::move(line));
      }
      out.verdict = st.last_verdict;
      out.judged  = tr;
    }
  }

  // 2. Propose the next step, or declare convergence -- but never in the call
  //    that just gave up on an attempt: the incumbent runs first.
  if (!st.trial && cx.may_propose && !closed_unfinished) {
    const Proposal p = propose(st, cx);
    out.gate         = p.gate;
    if (p.to) {
      Trial t;
      t.from = cx.current(st).v.tv1();
      t.to   = p.to->tv1();
      t.step = p.step;
      t.gate = p.gate;
      t.t    = cx.now;
      push(trial_record(t));
      out.proposed = st.trial;
    } else if (!p.converge_reason.empty()) {
      out.converge_reason = p.converge_reason;
      if (!st.converged || st.converged_reason != p.converge_reason) {
        push(decision_record(st.incumbent.value_or(default_vector()), true, p.converge_reason, cx.now));
      }
    }
  }
  return out;
}

// ---- sim.tune.file ------------------------------------------------------------------

std::optional<Tune_file> parse_tune_file(std::string_view text, std::string& err) {
  rj::Document d;
  d.Parse(text.data(), text.size());
  if (d.HasParseError() || !d.IsObject()) {
    err = "not a JSON object";
    return std::nullopt;
  }
  if (get_str(d, "schema") != kFileSchema) {
    err = std::format("not a sim.tune file (schema is not {})", kFileSchema);
    return std::nullopt;
  }
  Tune_file f;
  f.vector = get_str(d, "vector");
  if (const auto* knobs = get_obj(d, "knobs")) {
    // A knob's value is text in the knob's grammar. A hand-written file may also
    // spell dirty as a JSON bool and fence / live_words as a JSON integer; any
    // other JSON type is an error naming the knob, never a silent `auto`.
    std::string bad;
    const auto  knob = [&](const char* k, bool allow_bool, bool allow_int, std::string_view expects) -> std::string {
      const auto it = knobs->FindMember(k);
      if (it == knobs->MemberEnd()) {
        return {};
      }
      const auto& v = it->value;
      if (v.IsString()) {
        return {v.GetString(), v.GetStringLength()};
      }
      if (allow_bool && v.IsBool()) {
        return v.GetBool() ? "on" : "off";
      }
      if (allow_int && v.IsUint64()) {
        return std::to_string(v.GetUint64());
      }
      if (allow_int && v.IsInt64()) {
        return std::to_string(v.GetInt64());  // negative: the grammar names the range
      }
      if (bad.empty()) {
        bad = std::format("bad knob: {} expects {}, got a JSON {}",
                          k,
                          expects,
                          v.IsBool()     ? "boolean"
                          : v.IsNumber() ? "number"
                          : v.IsNull()   ? "null"
                          : v.IsArray()  ? "array"
                                         : "object");
      }
      return {};
    };
    const auto dk = livehd::sim::parse_tune_dirty(knob("dirty", true, false, "\"auto\"|\"on\"|\"off\" or true|false"));
    const auto fk = livehd::sim::parse_tune_fence(knob("fence", false, true, "\"auto\"|\"none\"|N"));
    const auto lk = livehd::sim::parse_tune_live_words(knob("live_words", false, true, "\"auto\"|N"));
    const auto bk = livehd::sim::parse_tune_backend(knob("backend", false, false, "\"auto\"|\"slop\"|\"llvm\""));
    if (!bad.empty()) {
      err = bad;
      return std::nullopt;
    }
    const std::pair<std::string_view, const std::string*> errs[] = {
        {     "dirty", &dk.err},
        {     "fence", &fk.err},
        {"live_words", &lk.err},
        {   "backend", &bk.err}
    };
    for (const auto& [name, e] : errs) {
      if (!e->empty()) {
        err = std::format("bad knob: {} {}", name, *e);
        return std::nullopt;
      }
    }
    f.pins = Pins{dk.value, fk.value, lk.value, bk.value};
  } else if (const auto v = Tune_vector::parse(f.vector)) {
    f.pins = Pins{v->dirty, v->fence, v->live_words, v->llvm};
  } else {
    err = "neither `knobs` nor a valid `vector`";
    return std::nullopt;
  }
  if (const auto* prov = get_obj(d, "provenance")) {
    f.structure = get_str(*prov, "structure");
    f.converged = get_bool(*prov, "converged");
  }
  return f;
}

std::string tune_file_json(const Tune_vector& v, const Provenance& p) {
  rj::StringBuffer sb;
  Writer           w(sb);
  w.StartObject();
  key(w, "schema");
  str(w, kFileSchema);
  key(w, "vector");
  str(w, v.tv1());
  key(w, "knobs");
  w.StartObject();
  key(w, "dirty");
  str(w, v.dirty_str());
  key(w, "fence");
  str(w, v.fence_str());
  key(w, "live_words");
  str(w, v.live_words_str());
  key(w, "backend");
  str(w, v.backend_str());
  w.EndObject();
  key(w, "provenance");
  w.StartObject();
  key(w, "structure");
  str(w, p.structure);
  key(w, "converged");
  w.Bool(p.converged);
  key(w, "model");
  str(w, cm1::kName);
  key(w, "stats");
  if (p.stats && p.stats->valid) {
    write_stats(w, *p.stats);
  } else {
    w.Null();
  }
  key(w, "created");
  str(w, p.created);
  w.EndObject();
  w.EndObject();
  return std::string{sb.GetString()} + "\n";
}

// ---- the envelope member ------------------------------------------------------------

std::string short_vector(const Tune_vector& v) {
  return std::format("d={} f={} lw={} be={}", v.dirty_str(), v.fence_str(), v.live_words_str(), v.backend_str());
}

std::string reproduce(const Tune_vector& v, std::string_view fill) {
  auto s = v.reproduce();
  if (fill == "zero") {
    s += " --set sim.unknown_zero=true";
  }
  return s;
}

namespace {

// Only the knobs a step moves: "d=on f=16".
std::string moved_knobs(const Tune_vector& from, const Tune_vector& to) {
  std::string s;
  auto        add = [&](std::string_view k, const std::string& val) { s += std::format("{}{}={}", s.empty() ? "" : " ", k, val); };
  if (from.dirty != to.dirty) {
    add("d", to.dirty_str());
  }
  if (from.fence != to.fence) {
    add("f", to.fence_str());
  }
  if (from.live_words != to.live_words) {
    add("lw", to.live_words_str());
  }
  if (from.llvm != to.llvm) {
    add("be", to.backend_str());
  }
  return s.empty() ? short_vector(to) : s;
}

std::string source_summary(const Resolved& r) {
  const std::pair<std::string_view, Source> ks[] = {
      { "d",      r.dirty},
      { "f",      r.fence},
      {"lw", r.live_words},
      {"be",    r.backend}
  };
  if (r.dirty == r.fence && r.fence == r.live_words && r.live_words == r.backend) {
    return std::string{source_name(r.dirty)};
  }
  std::string s;
  for (const auto src : {Source::explicit_, Source::file, Source::store, Source::dflt}) {
    std::string names;
    for (const auto& [k, ksrc] : ks) {
      if (ksrc == src) {
        names += names.empty() ? std::string{k} : " " + std::string{k};
      }
    }
    if (!names.empty()) {
      s += std::format("{}{}: {}", s.empty() ? "" : "; ", source_name(src), names);
    }
  }
  return s;
}

}  // namespace

std::string envelope_note(const Envelope& e) {
  std::string s;
  if (e.trial) {
    const auto from = Tune_vector::parse(e.trial->from);
    const auto to   = Tune_vector::parse(e.trial->to);
    s               = std::format("TRIAL {} (from {})",
                                  to && from ? moved_knobs(*from, *to) : e.trial->to,
                                  from ? short_vector(*from) : e.trial->from);
  } else {
    s = std::format("applied {} ({})", short_vector(e.applied.v), e.applied_known ? source_summary(e.applied) : "as built");
  }
  if (e.verdict) {
    const auto& v = *e.verdict;
    if (v.result == "divergence") {
      s += std::format(" -> DIVERGENCE in {} test(s): vector banned", v.tests.size());
    } else if (v.result == "accepted" || v.result == "rejected") {
      s += std::format(" -> {} {:.2f}x cpu/cycle ({})", v.result, v.rho, v.oracle == "equal" ? "digests equal" : v.oracle);
    } else {
      s += std::format(" -> {} ({})", v.result, v.reason);
    }
  }
  if (e.stats && e.stats->valid) {
    s += std::format(" | I_s={:.2f} q={:.2f} pairs={}{}",
                     e.stats->I_s,
                     e.stats->q,
                     e.stats->pairs,
                     e.stats->qualifies ? "" : " (smoke)");
  }
  if (!e.enabled) {
    s += std::format(" | tuner off ({})", e.reason);
  } else if (e.pending && ((e.trial && e.trial->to == *e.pending) || e.applied.v.tv1() == *e.pending)) {
    s += " | the trial awaits its run";  // built by a --setup-only (or this --run-only only built it)
  } else if (e.pending) {
    const auto to  = Tune_vector::parse(*e.pending);
    s             += std::format(" | next setup: TRIAL {}", to ? moved_knobs(e.applied.v, *to) : *e.pending);
  } else if (e.converged) {
    s += " | converged";
  } else if (e.profiling) {
    s += " | profiling";
  }
  for (const auto& n : e.notes) {
    s += " | " + n;
  }
  return s;
}

std::string envelope_json(const Envelope& e) {
  rj::StringBuffer sb;
  Writer           w(sb);
  w.StartObject();
  key(w, "schema_version");
  w.Int(1);
  key(w, "model");
  str(w, cm1::kName);
  key(w, "mode");
  str(w, mode_name(e.mode));
  key(w, "enabled");
  w.Bool(e.enabled);
  key(w, "reason");
  str(w, e.reason);
  key(w, "profiling");
  w.Bool(e.profiling);
  key(w, "fill");
  str(w, e.fill);
  key(w, "applied");
  w.StartObject();
  key(w, "vector");
  str(w, e.applied.v.tv1());
  key(w, "dirty");
  str(w, e.applied.v.dirty_str());
  key(w, "fence");
  str(w, e.applied.v.fence_str());
  key(w, "live_words");
  w.Uint64(e.applied.v.live_words);
  key(w, "backend");
  str(w, e.applied.v.backend_str());
  w.EndObject();
  key(w, "source");
  w.StartObject();
  const bool known = e.applied_known;
  key(w, "dirty");
  str(w, known ? source_name(e.applied.dirty) : "built");
  key(w, "fence");
  str(w, known ? source_name(e.applied.fence) : "built");
  key(w, "live_words");
  str(w, known ? source_name(e.applied.live_words) : "built");
  key(w, "backend");
  str(w, known ? source_name(e.applied.backend) : "built");
  w.EndObject();
  key(w, "stats");
  if (e.stats && e.stats->valid) {
    write_stats(w, *e.stats);
  } else {
    w.Null();
  }
  key(w, "trial");
  if (e.trial) {
    w.StartObject();
    key(w, "vector");
    str(w, e.trial->to);
    key(w, "from");
    str(w, e.trial->from);
    key(w, "step");
    str(w, e.trial->step);
    w.EndObject();
  } else {
    w.Null();
  }
  key(w, "verdict");
  if (e.verdict) {
    w.StartObject();
    key(w, "result");
    str(w, e.verdict->result);
    key(w, "from");
    str(w, e.verdict->from);
    key(w, "to");
    str(w, e.verdict->to);
    key(w, "rho");
    fixed(w, e.verdict->rho);
    key(w, "rho_c");
    fixed(w, e.verdict->rho_c);
    key(w, "rho_i");
    fixed(w, e.verdict->rho_i);
    key(w, "oracle");
    str(w, e.verdict->oracle);
    key(w, "reason");
    str(w, e.verdict->reason);
    key(w, "tests");
    w.StartArray();
    for (const auto& t : e.verdict->tests) {
      str(w, t);
    }
    w.EndArray();
    w.EndObject();
  } else {
    w.Null();
  }
  key(w, "gate");
  str(w, e.gate);
  key(w, "rejected");
  w.StartArray();
  for (const auto& r : e.rejected) {
    str(w, r);
  }
  w.EndArray();
  key(w, "pending");
  if (e.pending) {
    str(w, *e.pending);
  } else {
    w.Null();
  }
  key(w, "converged");
  w.Bool(e.converged);
  key(w, "reproduce");
  str(w, reproduce(e.applied.v, e.fill));
  key(w, "note");
  str(w, envelope_note(e));
  w.EndObject();
  return sb.GetString();
}

}  // namespace lhd::sim_tune
