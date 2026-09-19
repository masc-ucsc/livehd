//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// The sim tuner's model (lhd_sim_tune) and store plumbing (lhd_tune), without
// an engine: precedence and projection, the old-spelling renames, the per-run
// statistics (support-weight variants and the support flag, the codegen
// comparability digest, the baseline vs trial gates), the ladder and its gates
// (a window of this model's class weight only), the verdict math and the
// oracle skip rules, the trial lifecycle (attempts, abandonment without
// livelock under `auto` and `on`, the oracle on runs too short to time,
// uncharged closures, failures that ban only when repeated, stale trials),
// store replay (partial line, foreign schema, read-only load), bounded
// compaction, the export/import round trip (incl. JSON-typed knobs) and the
// envelope member.

#include "lhd_sim_tune.hpp"

#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "lhd_tune.hpp"
#include "rapidjson/document.h"

namespace {

namespace fs = std::filesystem;
using namespace lhd::sim_tune;
// testing::Test has a member Run(): spell the tuner's run record apart inside TEST bodies.
using Sim_run = lhd::sim_tune::Run;
using livehd::sim::kTuneDefaultFenceRatio;
using livehd::sim::kTuneNoFences;

const Tune_vector kL0{false, kTuneNoFences, 256, false};
const Tune_vector kL1{true, kTuneDefaultFenceRatio, 256, false};
const Tune_vector kL2{true, 0, 256, false};

// ---- a synthetic drv.bin raw run file (DESIGN §6.8) ------------------------------------

struct Raw_test {
  std::string name       = "t.a";
  std::string status     = "pass";
  uint64_t    sim_cycles = 1'000'000;
  uint64_t    cpu_ns     = 400'000'000;  // 0.4 s: passes the quality gate on its own
  uint64_t    cpu_cycles = 1'600'000'000;
  uint64_t    instr      = 4'000'000'000;
  double      pcore      = 1.0;
  std::string end_digest = "00000000000000aa";
  std::string out_digest = "00000000000000bb";
  // profile (pairs == 0: no profile object)
  uint64_t    pairs      = 500;
  uint64_t    quiescent  = 0;
  uint64_t    warm       = 2048;
  double      total_ge   = 1000;
  double      idle_ge    = 0;     // Σ over pairs; I_s = idle_ge / (pairs * total_ge)
  bool        exact      = true;  // the support table's `exact`
  bool        walker     = false;
  uint64_t    words      = 10;
  uint64_t    idle_words = 0;
  // The driver's `profile.weights` object. "" = every weight equal to the GE
  // idleness above (what today's driver reports for uniform classes), unless
  // `legacy`: a driver predating `weights` (only the support object).
  std::string weights;
  bool        legacy = false;
};

// The per-root fields of a synthetic raw file.
struct Raw_root {
  std::string structure = "S1";
  std::string codegen   = "sim.tune.dirty=off\nsim.slop_u=true\nsim.debug=false\nplan.runtime_random=false\n";
  std::string args      = "1000";  // the `cycles` argument
};

std::string json_escape_nl(std::string s) {
  for (size_t p = s.find('\n'); p != std::string::npos; p = s.find('\n', p + 2)) {
    s.replace(p, 1, "\\n");
  }
  return s;
}

std::string raw_json(const std::string& vector, const std::vector<Raw_test>& tests, std::string_view fill = "zero",
                     uint64_t draws = 0, std::string_view codegen = "plan.runtime_random=false\n", std::string_view seed = "1",
                     const Raw_root* root = nullptr) {
  Raw_root rr;
  if (root != nullptr) {
    rr = *root;
  } else {
    rr.codegen = std::string{codegen};
  }
  std::string t;
  for (const auto& r : tests) {
    std::string prof = "null";
    if (r.pairs > 0) {
      const std::string support
          = r.walker
                ? "null"
                : std::format(R"({{"total_ge":{},"idle_ge":{},"exact":{}}})", r.total_ge, r.idle_ge, r.exact ? "true" : "false");
      const std::string walker  = r.walker ? std::format(R"({{"words":{},"idle_words":{}}})", r.words, r.idle_words) : "null";
      std::string       weights = r.weights;
      if (weights.empty() && !r.legacy && !r.walker) {
        const auto one = std::format(R"({{"total":{},"idle":{}}})", r.total_ge, r.idle_ge);
        weights        = std::format(R"({{"ge":{0},"sites":{0},"cost":{0},"cost_flat":{0}}})", one);
      }
      prof = std::format(
          R"({{"pairs":{},"quiescent_pairs":{},"stride":1000,"sample_ns":5,"warm_cycles":{},"support":{},{}"walker":{},"occ":[]}})",
          r.pairs,
          r.quiescent,
          r.warm,
          support,
          weights.empty() ? std::string{} : std::format(R"("weights":{},)", weights),
          walker);
    }
    t += std::format(
        R"({}{{"test":"{}","status":"{}","sim_cycles":{},"init_ns":1,"sim_ns":2,"cpu_ns":{},"cpu_cycles":{},"instructions":{},"pcore_frac":{},"counters":"rusage_v6","rng_draws":0,"ckpt_taken":0,"end_digest":"{}","out_digest":"{}","profile":{}}})",
        t.empty() ? "" : ",",
        r.name,
        r.status,
        r.sim_cycles,
        r.cpu_ns,
        r.cpu_cycles,
        r.instr,
        r.pcore,
        r.end_digest,
        r.out_digest,
        prof);
  }
  return std::format(
      R"({{"schema":"lhd-sim-tune-raw-1","roots":[{{"var":"dut","module":"top","vector":"{}","structure":"{}","codegen":"{}"}}],"seed":"{}","init_zero":false,"fill":"{}","baked_unknown_zero":false,"unknown_draws":{},"tb_unknown_literals":0,"selected":["t.a"],"args":{{"cycles":"{}"}},"counters":"rusage_v6","host":{{"name":"h","cpu":"c"}},"tests":[{}]}})",
      vector,
      rr.structure,
      json_escape_nl(rr.codegen),
      seed,
      fill,
      draws,
      rr.args,
      t);
}

Sim_run make_run(const std::string& vector, const std::vector<Raw_test>& tests, std::string_view fill = "zero", uint64_t draws = 0,
                 std::string_view codegen = "plan.runtime_random=false\n") {
  std::string err;
  auto        r = run_from_raw(raw_json(vector, tests, fill, draws, codegen), err);
  EXPECT_TRUE(r.has_value()) << err;
  r->id   = std::format("{}-{}.json", vector.size(), r->tests.size());
  r->tb   = "TB";
  r->mode = "auto";
  return *r;
}

// A run on an explicit structure / argument set (the livelock and edit cases).
Sim_run make_run_at(const std::string& vector, const std::vector<Raw_test>& tests, const Raw_root& root) {
  std::string err;
  auto        r = run_from_raw(raw_json(vector, tests, "zero", 0, "", "1", &root), err);
  EXPECT_TRUE(r.has_value()) << err;
  r->tb   = "TB";
  r->mode = "auto";
  return *r;
}

Raw_test idle_test(double I_s = 0.8) {
  Raw_test t;
  t.idle_ge = I_s * static_cast<double>(t.pairs) * t.total_ge;
  return t;
}

Raw_test busy_test() {
  Raw_test t;
  t.idle_ge = 0.02 * static_cast<double>(t.pairs) * t.total_ge;
  return t;
}

// Feed runs into a State the way the session does (one record per run).
void add_run(State& st, Sim_run r, std::vector<std::string>* lines = nullptr) {
  static int n = 0;
  r.id         = std::format("run-{}", ++n);
  auto line    = run_record(r);
  apply_record(st, line);
  if (lines != nullptr) {
    lines->push_back(line);
  }
}

Context ctx(Mode m = Mode::auto_, Pins ex = {}, Pins file = {}) {
  Context cx;
  cx.mode          = m;
  cx.explicit_pins = ex;
  cx.file_pins     = file;
  cx.may_propose   = true;
  cx.now           = 1000;
  return cx;
}

// The context after the attempt's own run (or its failure): the attempt is over.
Context over(std::string failed = {}, Mode m = Mode::auto_) {
  Context cx        = ctx(m);
  cx.attempt_over   = true;
  cx.attempt_failed = std::move(failed);
  cx.attempt_why    = cx.attempt_failed.empty() ? std::string{} : std::string{"drv.bin died"};
  return cx;
}

// A setup applies the pending trial: one attempt starts (what the session records).
void apply_attempt(State& st, std::vector<std::string>* lines = nullptr) {
  ASSERT_TRUE(st.trial.has_value());
  auto line = attempt_record(*st.trial, 1000);
  apply_record(st, line);
  ASSERT_TRUE(st.attempt_open);
  if (lines != nullptr) {
    lines->push_back(line);
  }
}

void append(std::vector<std::string>& lines, const Outcome& o) { lines.insert(lines.end(), o.append.begin(), o.append.end()); }

// A store whose incumbent is L0 (unconverged). The built-in default is L1
// (2026-09-19); the trial-lifecycle tests below climb L0 -> L1, which exercises
// exactly the attempt / verdict / ban mechanics a descent from the default does.
void seed_l0(State& st, std::vector<std::string>* lines = nullptr) {
  auto line = decision_record(kL0, false, "", 0);
  apply_record(st, line);
  if (lines != nullptr) {
    lines->push_back(line);
  }
}

std::string tmp_dir(std::string_view tag) {
  auto d = fs::path(testing::TempDir()) / std::format("lhd_sim_tune_{}_{}", tag, ::getpid());
  fs::remove_all(d);
  fs::create_directories(d);
  return d.string();
}

// ---- precedence and projection ---------------------------------------------------------

TEST(SimTuneResolve, DefaultsAreTheCanonicalL1) {
  const auto r = resolve({}, {}, std::nullopt);
  EXPECT_EQ(r.v, kL1);
  EXPECT_EQ(r.v.tv1(), "tv1:d=on;f=16;lw=256;be=slop");
  EXPECT_EQ(r.v, Tune_vector{});  // default-constructed IS the default vector
  EXPECT_EQ(r.dirty, Source::dflt);
  EXPECT_EQ(r.fence, Source::dflt);
  EXPECT_EQ(r.live_words, Source::dflt);
  EXPECT_EQ(r.backend, Source::dflt);
}

TEST(SimTuneResolve, StoreMovesOnlyTheDefaults) {
  const Tune_vector store{true, 0, 64, true};
  const auto        r = resolve({}, {}, store);
  EXPECT_EQ(r.v, store);
  EXPECT_EQ(r.dirty, Source::store);
  EXPECT_EQ(r.fence, Source::store);
  // explicit > file > store, knob by knob
  Pins ex;
  ex.live_words = 20;
  Pins file;
  file.live_words = 99;
  file.llvm       = false;
  const auto r2   = resolve(ex, file, store);
  EXPECT_EQ(r2.v.live_words, 20u);
  EXPECT_EQ(r2.live_words, Source::explicit_);
  EXPECT_FALSE(r2.v.llvm);
  EXPECT_EQ(r2.backend, Source::file);
  EXPECT_TRUE(r2.v.dirty);
  EXPECT_EQ(r2.dirty, Source::store);
}

TEST(SimTuneResolve, PinnedDirtyProjectsTheDefaultFence) {
  // The store's f=0 was chosen for ITS dirty=on; pinning dirty=off must not
  // inherit it (a fence without dirty gating is the measured-bad case).
  Pins ex;
  ex.dirty     = false;
  const auto r = resolve(ex, {}, kL2);
  EXPECT_FALSE(r.v.dirty);
  EXPECT_EQ(r.v.fence, kTuneNoFences);
  EXPECT_EQ(r.fence, Source::dflt);
  // a file pinning dirty=on over a d=off store: the fence follows dirty -> 16
  Pins file;
  file.dirty    = true;
  const auto r2 = resolve({}, file, kL0);
  EXPECT_EQ(r2.v, kL1);
  EXPECT_EQ(r2.dirty, Source::file);
  EXPECT_EQ(r2.fence, Source::dflt);
  // an explicit fence stays whatever dirty does
  Pins ex2;
  ex2.fence     = kTuneNoFences;
  const auto r3 = resolve(ex2, {}, kL1);
  EXPECT_TRUE(r3.v.dirty);
  EXPECT_EQ(r3.v.fence, kTuneNoFences);
  EXPECT_EQ(r3.fence, Source::explicit_);
}

TEST(SimTuneResolve, PinsFromSetsLastWinsAndAutoUnpins) {
  std::string err;
  auto        p = pins_from_sets(
      {
          {     "sim.tune.dirty",   "on"},
          {            "sim.vcd",    "1"},
          {     "sim.tune.dirty", "auto"},
          {     "sim.tune.fence", "none"},
          {"sim.tune.live_words",   "20"},
          {   "sim.tune.backend", "llvm"},
          {   "sim.tune.backend", "slop"}
  },
      err);
  EXPECT_TRUE(err.empty()) << err;
  EXPECT_FALSE(p.dirty.has_value());  // config `on`, then CLI `auto`: not pinned
  ASSERT_TRUE(p.fence.has_value());
  EXPECT_EQ(*p.fence, kTuneNoFences);
  EXPECT_EQ(p.live_words.value_or(0), 20u);
  EXPECT_EQ(p.llvm, std::optional<bool>{false});
  (void)pins_from_sets(
      {
          {"sim.tune.live_words", "0"}
  },
      err);
  EXPECT_FALSE(err.empty());  // 0 is spelled auto
  EXPECT_EQ(last_set(
                {
                    {"a", "1"},
                    {"b", "2"},
                    {"a", "3"}
  },
                "a"),
            std::optional<std::string>{"3"});
}

// ---- ruling 6: old spellings are directed rename errors -----------------------------------

TEST(SimTuneRename, CopyPasteableTranslations) {
  auto hint = [](std::string_view f, std::string_view v) {
    const auto r = renamed_sim_flag(f, v);
    return r ? std::format("sim.{}={}", r->new_flag, r->value) : std::string{"-"};
  };
  EXPECT_EQ(hint("color_dirty", "true"), "sim.tune.dirty=on");
  EXPECT_EQ(hint("color_dirty", "1"), "sim.tune.dirty=on");
  EXPECT_EQ(hint("color_dirty", "false"), "sim.tune.dirty=off");
  EXPECT_EQ(hint("fence_ratio", ""), "sim.tune.fence=auto");
  EXPECT_EQ(hint("fence_ratio", "0"), "sim.tune.fence=0");
  EXPECT_EQ(hint("fence_ratio", "1000000000"), "sim.tune.fence=none");
  EXPECT_EQ(hint("live_words", "0"), "sim.tune.live_words=auto");
  EXPECT_EQ(hint("live_words", "20"), "sim.tune.live_words=20");
  EXPECT_EQ(hint("backend", "llvm"), "sim.tune.backend=llvm");
  EXPECT_EQ(hint("vcdfakedelay", "false"), "sim.vcd_fake_delay=false");
  EXPECT_EQ(hint("checkpoint", "true"), "-");
}

// ---- per-run statistics ---------------------------------------------------------------------

TEST(SimTuneStats, SupportIdlenessAndQualityGate) {
  Raw_test a      = idle_test(0.8);
  a.quiescent     = 50;
  Raw_test b      = idle_test(0.4);
  b.name          = "t.b";
  b.sim_cycles    = 3'000'000;
  const Sim_run r = make_run(kL0.tv1(), {a, b});
  EXPECT_EQ(r.vector, kL0.tv1());
  EXPECT_EQ(r.structure, "S1");
  EXPECT_EQ(r.host, "h|c");
  EXPECT_EQ(r.args, "cycles=1000");
  ASSERT_TRUE(r.stats.valid);
  EXPECT_TRUE(r.stats.support);
  EXPECT_TRUE(r.stats.exact);
  EXPECT_NEAR(r.stats.I_s, (0.8 * 1 + 0.4 * 3) / 4, 1e-9);  // cycle-weighted
  EXPECT_NEAR(r.stats.q, (0.1 * 1) / 4, 1e-9);
  EXPECT_EQ(r.stats.pairs, 1000u);
  EXPECT_EQ(r.stats.postwarm, 4'000'000u - 2 * 2048u);
  EXPECT_TRUE(r.stats.qualifies);

  Raw_test small = idle_test();
  small.pairs    = 10;  // < 64 pairs
  small.idle_ge  = 0.8 * 10 * small.total_ge;
  EXPECT_FALSE(make_run(kL0.tv1(), {small}).stats.qualifies);
  Raw_test quick = idle_test();
  quick.cpu_ns   = 50'000'000;  // 50 ms < 0.2 s
  EXPECT_FALSE(make_run(kL0.tv1(), {quick}).stats.qualifies);
}

TEST(SimTuneStats, WalkerFallbackAndErrors) {
  Raw_test w;
  w.walker        = true;
  w.words         = 10;
  w.idle_words    = 500 * 10 * 6 / 10;  // 60% of the words unchanged
  const Sim_run r = make_run(kL0.tv1(), {w});
  EXPECT_FALSE(r.stats.support);
  EXPECT_NEAR(r.stats.I_s, 0.6, 1e-9);

  std::string err;
  EXPECT_FALSE(run_from_raw(R"({"schema":"something-else"})", err).has_value());
  EXPECT_FALSE(run_from_raw("not json", err).has_value());
  // runtime_random comes from the baked codegen table
  EXPECT_TRUE(make_run(kL0.tv1(), {idle_test()}, "zero", 0, "plan.runtime_random=true\n").runtime_random);
}

TEST(SimTuneStats, RecordRoundTrip) {
  const Sim_run r = make_run(kL1.tv1(), {idle_test(0.9)});
  State         st;
  apply_record(st, run_record(r));
  ASSERT_EQ(st.runs.size(), 1u);
  const auto& b = st.runs.front();
  EXPECT_EQ(b.vector, r.vector);
  EXPECT_EQ(b.tests.size(), 1u);
  EXPECT_EQ(b.tests.front().end_digest, "00000000000000aa");
  EXPECT_NEAR(b.stats.I_s, 0.9, 1e-4);
  EXPECT_TRUE(b.stats.qualifies);
  EXPECT_TRUE(comparable(r, b));
}

// FX2's `profile.weights`: I_s is the kSupportWeight variant; every variant is
// recorded (run record, window, envelope stats), cycle-weighted over tests.
TEST(SimTuneStats, SupportWeightVariants) {
  const auto weights = [](double ge, double sites, double cost, double cost_flat) {
    // pairs = 500 and every total = 100: idle = I * 500 * 100
    return std::format(
        R"({{"ge":{{"total":100,"idle":{}}},"sites":{{"total":100,"idle":{}}},"cost":{{"total":100,"idle":{}}},"cost_flat":{{"total":100,"idle":{}}}}})",
        ge * 50'000,
        sites * 50'000,
        cost * 50'000,
        cost_flat * 50'000);
  };
  Raw_test a   = idle_test(0.2);  // the legacy support object says 0.2 ...
  a.weights    = weights(0.2, 0.4, 0.6, 0.8);
  Raw_test b   = idle_test(0.2);
  b.name       = "t.b";
  b.sim_cycles = 3'000'000;
  b.weights    = weights(0.2, 0.0, 0.2, 0.4);
  const auto r = make_run(kL0.tv1(), {a, b});
  ASSERT_TRUE(r.stats.valid);
  EXPECT_EQ(cm1::kSupportWeight, "cost_flat");
  EXPECT_EQ(r.stats.weight, "cost_flat");
  EXPECT_NEAR(r.stats.I_s, (0.8 * 1 + 0.4 * 3) / 4, 1e-9);  // ... but I_s follows cost_flat
  ASSERT_TRUE(r.stats.I_ge && r.stats.I_sites && r.stats.I_cost && r.stats.I_cost_flat);
  EXPECT_NEAR(*r.stats.I_ge, 0.2, 1e-9);
  EXPECT_NEAR(*r.stats.I_sites, (0.4 * 1 + 0.0 * 3) / 4, 1e-9);
  EXPECT_NEAR(*r.stats.I_cost, (0.6 * 1 + 0.2 * 3) / 4, 1e-9);
  EXPECT_NEAR(*r.stats.I_cost_flat, r.stats.I_s, 1e-9);
  // the record keeps all four
  State st;
  apply_record(st, run_record(r));
  ASSERT_TRUE(st.runs.front().stats.I_cost.has_value());
  EXPECT_NEAR(*st.runs.front().stats.I_cost, *r.stats.I_cost, 1e-4);
  EXPECT_EQ(st.runs.front().stats.weight, "cost_flat");
  const auto w = window_stats({&st.runs.front()});
  ASSERT_TRUE(w.I_sites.has_value());
  EXPECT_NEAR(*w.I_sites, *r.stats.I_sites, 1e-4);
  // the envelope's stats carry them too
  Envelope e;
  e.applied = resolve({}, {}, std::nullopt);
  e.stats   = r.stats;
  rapidjson::Document d;
  const auto          j = envelope_json(e);
  d.Parse(j.c_str());
  ASSERT_FALSE(d.HasParseError()) << j;
  EXPECT_NEAR(d["stats"]["I_cost_flat"].GetDouble(), r.stats.I_s, 1e-4);
  EXPECT_NEAR(d["stats"]["I_ge"].GetDouble(), 0.2, 1e-4);
  EXPECT_STREQ(d["stats"]["weight"].GetString(), "cost_flat");

  // a driver predating `weights`: I_s is the support table's GE idleness
  Raw_test legacy = idle_test(0.7);
  legacy.legacy   = true;
  const auto old  = make_run(kL0.tv1(), {legacy});
  EXPECT_EQ(old.stats.weight, "ge");
  EXPECT_NEAR(old.stats.I_s, 0.7, 1e-9);
  ASSERT_TRUE(old.stats.I_ge.has_value());
  EXPECT_FALSE(old.stats.I_cost_flat.has_value());
}

// FX2-R2-4: a root whose classes are all pure wiring (total_ge 0) still
// measures I_s from its support tables through the word-cost weights: the run
// must say support=true and carry the table's `exact`.
TEST(SimTuneStats, SupportFlagFollowsTheTablesActuallyUsed) {
  for (const bool exact : {true, false}) {
    Raw_test t = idle_test(0.0);
    t.total_ge = 0;  // every class is wiring: no GE at all
    t.idle_ge  = 0;
    t.exact    = exact;
    t.weights
        = R"({"ge":{"total":0,"idle":0},"sites":{"total":2,"idle":800},"cost":{"total":2,"idle":800},"cost_flat":{"total":2,"idle":800}})";
    const auto r = make_run(kL0.tv1(), {t});
    ASSERT_TRUE(r.stats.valid);
    EXPECT_EQ(r.stats.weight, "cost_flat");
    EXPECT_NEAR(r.stats.I_s, 800.0 / (500 * 2), 1e-9);
    EXPECT_TRUE(r.stats.support) << "I_s came from the support tables";
    EXPECT_EQ(r.stats.exact, exact);
    EXPECT_FALSE(r.stats.I_ge.has_value());  // a zero GE total measures nothing
  }
  // the walker is still not support
  Raw_test w;
  w.walker      = true;
  w.idle_words  = 500 * 10 / 2;
  const auto rw = make_run(kL0.tv1(), {w});
  EXPECT_FALSE(rw.stats.support);
  EXPECT_FALSE(rw.stats.exact);
  EXPECT_EQ(rw.stats.weight, "walker");
}

// caching-retention:F6 / soundness:S3: two runs whose codegen tables differ
// outside the tune vector never judge each other; the vector lines are ignored.
TEST(SimTuneStats, CodegenDigestGatesComparability) {
  Raw_root base;
  base.codegen   = "sim.tune.dirty=off\nsim.tune.fence=none\nsim.slop_u=true\nsim.debug=false\nplan.runtime_random=false\n";
  Raw_root trial = base;
  trial.codegen  = "sim.tune.dirty=on\nsim.tune.fence=16\nsim.slop_u=true\nsim.debug=false\nplan.runtime_random=false\n";
  Raw_root debug = base;
  debug.codegen  = "sim.tune.dirty=on\nsim.tune.fence=16\nsim.slop_u=true\nsim.debug=true\nplan.runtime_random=false\n";
  const auto b   = make_run_at(kL0.tv1(), {idle_test(0.8)}, base);
  const auto t   = make_run_at(kL1.tv1(), {idle_test(0.8)}, trial);
  const auto x   = make_run_at(kL1.tv1(), {idle_test(0.8)}, debug);
  EXPECT_FALSE(b.codegen.empty());
  EXPECT_EQ(b.codegen, t.codegen);
  EXPECT_TRUE(comparable(b, t));
  EXPECT_NE(b.codegen, x.codegen);
  EXPECT_FALSE(comparable(b, x));
  State st;
  apply_record(st, run_record(b));
  EXPECT_EQ(st.runs.front().codegen, b.codegen);  // it rides the record
  EXPECT_EQ(find_baseline(st, kL0.tv1(), x), nullptr);
}

TEST(SimTuneStats, TrialGateIsLooserThanTheBaselineGate) {
  Raw_test fast = idle_test();
  fast.cpu_ns   = 30'000'000;  // 30 ms
  fast.pairs    = 10;          // few pairs: a stats concern, not a verdict one
  fast.idle_ge  = 0.8 * 10 * fast.total_ge;
  const auto r  = make_run(kL1.tv1(), {fast});
  EXPECT_FALSE(r.stats.qualifies);
  EXPECT_TRUE(r.stats.judgeable);
  Raw_test short_run   = idle_test();
  short_run.sim_cycles = 8'000;  // < 10^4 post-warm-up cycles
  EXPECT_FALSE(make_run(kL1.tv1(), {short_run}).stats.judgeable);
  EXPECT_EQ(r.stats.postwarm, 1'000'000u - 2048u);
}

// ---- the ladder and its gates ------------------------------------------------------------

TEST(SimTuneLadder, IdleClimbsToL1AndBusyDescendsToL0) {
  // From L0, an idle design trials L1.
  State idle;
  seed_l0(idle);
  add_run(idle, make_run(kL0.tv1(), {idle_test(0.8)}));
  const auto p = propose(idle, ctx());
  ASSERT_TRUE(p.to.has_value());
  EXPECT_EQ(*p.to, kL1);
  EXPECT_EQ(p.step, "L1");
  EXPECT_TRUE(p.gate.starts_with("pass")) << p.gate;

  // From the default L1, an always-toggling design trials L0, and the gate
  // passes outright (the LFSR regime).
  State busy;
  add_run(busy, make_run(kL1.tv1(), {busy_test()}));
  const auto q = propose(busy, ctx());
  ASSERT_TRUE(q.to.has_value());
  EXPECT_EQ(*q.to, kL0);
  EXPECT_EQ(q.step, "L0");
  EXPECT_TRUE(q.gate.starts_with("pass")) << q.gate;

  // At L0 a busy design has nowhere to go: zero trials.
  State settled;
  seed_l0(settled);
  add_run(settled, make_run(kL0.tv1(), {busy_test()}));
  const auto r = propose(settled, ctx());
  EXPECT_FALSE(r.to.has_value());
  EXPECT_EQ(r.converge_reason, "busy");

  // At the default L1 a moderately idle design stays: dirty gating pays, and
  // it is below the L2 bar.
  State mid;
  add_run(mid, make_run(kL1.tv1(), {idle_test(0.5)}));
  const auto m = propose(mid, ctx());
  EXPECT_FALSE(m.to.has_value());
  EXPECT_EQ(m.converge_reason, "no-step");
}

TEST(SimTuneLadder, L2FromL1WhenVeryIdle) {
  // L1 is the default: no accepted L1 verdict is needed before L2.
  State st;
  add_run(st, make_run(kL1.tv1(), {idle_test(0.9)}));
  const auto p = propose(st, ctx());
  ASSERT_TRUE(p.to.has_value());
  EXPECT_EQ(*p.to, kL2);
  EXPECT_EQ(p.step, "L2");
  // an L1 incumbent from the store (imported, or accepted) climbs the same way
  State imported;
  imported.incumbent = kL1;
  add_run(imported, make_run(kL1.tv1(), {idle_test(0.9)}));
  ASSERT_TRUE(propose(imported, ctx()).to.has_value());
  // I_s below the L2 bar: converged at L1
  State low;
  add_run(low, make_run(kL1.tv1(), {idle_test(0.6)}));
  EXPECT_FALSE(propose(low, ctx()).to.has_value());
  // L0 never jumps straight to L2: one lever per step
  State l0;
  seed_l0(l0);
  add_run(l0, make_run(kL0.tv1(), {idle_test(0.9)}));
  const auto q = propose(l0, ctx());
  ASSERT_TRUE(q.to.has_value());
  EXPECT_EQ(*q.to, kL1);
}

TEST(SimTuneLadder, PinnedLeversNeverMove) {
  // the default L1, busy: the descent moves dirty, which an explicit `on` pins
  State def;
  add_run(def, make_run(kL1.tv1(), {busy_test()}));
  Pins on;
  on.dirty      = true;
  const auto p0 = propose(def, ctx(Mode::auto_, on));
  EXPECT_FALSE(p0.to.has_value());
  EXPECT_EQ(p0.converge_reason, "pinned");

  State st;
  seed_l0(st);
  add_run(st, make_run(kL0.tv1(), {idle_test(0.8)}));
  Pins ex;
  ex.dirty     = false;
  const auto p = propose(st, ctx(Mode::auto_, ex));
  EXPECT_FALSE(p.to.has_value());
  EXPECT_EQ(p.converge_reason, "pinned");
  // a pinned co-moving knob keeps its value: L1 moves dirty only
  Pins ex2;
  ex2.fence     = kTuneNoFences;
  const auto p2 = propose(st, ctx(Mode::auto_, ex2));
  ASSERT_TRUE(p2.to.has_value());
  EXPECT_TRUE(p2.to->dirty);
  EXPECT_EQ(p2.to->fence, kTuneNoFences);
  // live_words / backend are never moved by the ladder (and the baseline must
  // be a run of the vector the pins resolve to)
  Pins file;
  file.live_words = 20;
  file.llvm       = true;
  EXPECT_FALSE(propose(st, ctx(Mode::auto_, {}, file)).to.has_value());  // no run of lw=20 be=llvm yet
  add_run(st, make_run(Tune_vector{false, kTuneNoFences, 20, true}.tv1(), {idle_test(0.8)}));
  const auto p3 = propose(st, ctx(Mode::auto_, {}, file));
  ASSERT_TRUE(p3.to.has_value());
  EXPECT_EQ(p3.to->live_words, 20u);
  EXPECT_TRUE(p3.to->llvm);
}

TEST(SimTuneLadder, RejectedRuntimeRandomAndFlipLimit) {
  State st;
  seed_l0(st);
  add_run(st, make_run(kL0.tv1(), {idle_test(0.8)}));
  st.rejected.push_back(kL1.tv1());
  EXPECT_EQ(propose(st, ctx()).converge_reason, "rejected");
  // a rejected descent keeps the default
  State down;
  add_run(down, make_run(kL1.tv1(), {busy_test()}));
  down.rejected.push_back(kL0.tv1());
  EXPECT_EQ(propose(down, ctx()).converge_reason, "rejected");

  State rr;
  seed_l0(rr);
  add_run(rr, make_run(kL0.tv1(), {idle_test(0.8)}, "zero", 0, "plan.runtime_random=true\n"));
  EXPECT_EQ(propose(rr, ctx()).converge_reason, "runtime-random");

  State fl;
  seed_l0(fl);
  add_run(fl, make_run(kL0.tv1(), {idle_test(0.8)}));
  fl.flips = cm1::kMaxFlips;
  EXPECT_EQ(propose(fl, ctx()).converge_reason, "flip-limit");
}

TEST(SimTuneLadder, PayoffGateAppliesToAutoOnly) {
  State st;
  seed_l0(st);
  Raw_test t = idle_test(0.55);  // below the outright-pass idleness
  t.cpu_ns   = 250'000'000;      // 250 ms
  Sim_run r  = make_run(kL0.tv1(), {t});
  r.setup_ms = 50'000;  // a 50 s root rebuild
  r.regen    = true;
  add_run(st, r);
  EXPECT_NEAR(econ_rebuild_ms(st), 50'000, 1e-9);
  const auto p = propose(st, ctx(Mode::auto_));
  EXPECT_FALSE(p.to.has_value());
  EXPECT_EQ(p.converge_reason, "gate");
  EXPECT_TRUE(p.gate.starts_with("fail")) << p.gate;
  // `on` always trials
  const auto q = propose(st, ctx(Mode::on));
  ASSERT_TRUE(q.to.has_value());
  EXPECT_EQ(q.gate, "on(always)");
  // a cheap rebuild passes: 0.55 * 250 ms * 20 = 2750 ms >= 1000 ms
  State cheap;
  seed_l0(cheap);
  r.setup_ms = 1000;
  add_run(cheap, r);
  EXPECT_TRUE(propose(cheap, ctx(Mode::auto_)).to.has_value());

  // The descent from the default predicts 1 - I_s: busy but not the LFSR
  // regime (I_s = 0.4) fails against a 50 s rebuild (0.6 * 250 ms * 20 =
  // 3000 ms), passes against a 1 s one, and `on` always trials.
  Raw_test b  = idle_test(0.4);
  b.cpu_ns    = 250'000'000;
  Sim_run  rd = make_run(kL1.tv1(), {b});
  rd.setup_ms = 50'000;
  rd.regen    = true;
  State down;
  add_run(down, rd);
  const auto d = propose(down, ctx(Mode::auto_));
  EXPECT_FALSE(d.to.has_value());
  EXPECT_EQ(d.converge_reason, "gate");
  EXPECT_TRUE(d.gate.starts_with("fail")) << d.gate;
  EXPECT_TRUE(propose(down, ctx(Mode::on)).to.has_value());
  State down_cheap;
  rd.setup_ms = 1000;
  add_run(down_cheap, rd);
  EXPECT_TRUE(propose(down_cheap, ctx(Mode::auto_)).to.has_value());
  // the LFSR regime passes outright, whatever the rebuild costs
  Raw_test lfsr = busy_test();
  lfsr.cpu_ns   = 250'000'000;
  Sim_run rl    = make_run(kL1.tv1(), {lfsr});
  rl.setup_ms   = 50'000;
  rl.regen      = true;
  State lfsr_st;
  add_run(lfsr_st, rl);
  const auto l = propose(lfsr_st, ctx(Mode::auto_));
  ASSERT_TRUE(l.to.has_value());
  EXPECT_EQ(*l.to, kL0);
  EXPECT_TRUE(l.gate.starts_with("pass")) << l.gate;
}

TEST(SimTuneLadder, SmokeOnlyConvergesAfterThreeRuns) {
  Raw_test small = idle_test();
  small.cpu_ns   = 1'000'000;
  State st;
  seed_l0(st);
  add_run(st, make_run(kL0.tv1(), {small}));
  add_run(st, make_run(kL0.tv1(), {small}));
  EXPECT_TRUE(propose(st, ctx()).converge_reason.empty());  // keep profiling
  add_run(st, make_run(kL0.tv1(), {small}));
  EXPECT_EQ(propose(st, ctx()).converge_reason, "smoke-only");
}

// FX2-R2-3: the ladder never averages idleness measured under another class
// weight (records from before a kSupportWeight change, or before the weight
// variants existed) with this model's; the stored I_<weight> variant is used
// whenever the record has it.
std::string cost_flat_weights(double I) {
  // pairs = 500, every total = 100
  return std::format(
      R"({{"ge":{{"total":100,"idle":{}}},"sites":{{"total":100,"idle":{}}},"cost":{{"total":100,"idle":{}}},"cost_flat":{{"total":100,"idle":{}}}}})",
      0.99 * 50'000,
      0.99 * 50'000,
      0.99 * 50'000,
      I * 50'000);
}

TEST(SimTuneLadder, WindowAveragesOnlyThisModelsWeight) {
  // Three qualifying runs recorded under GE (a driver predating `weights`):
  // I_s = 0.99, and no cost_flat variant.
  State st;
  seed_l0(st);
  for (int i = 0; i < 3; ++i) {
    Raw_test lt       = idle_test(0.99);
    lt.legacy         = true;
    const auto legacy = make_run(kL0.tv1(), {lt});
    ASSERT_EQ(legacy.stats.weight, "ge");
    ASSERT_FALSE(legacy.stats.I_cost_flat.has_value());
    EXPECT_FALSE(ladder_I(legacy.stats).has_value());
    add_run(st, legacy);
  }
  EXPECT_TRUE(ladder_window(st, "S1").empty());
  EXPECT_FALSE(propose(st, ctx()).to.has_value());  // nothing measured under cost_flat yet: keep profiling
  EXPECT_TRUE(propose(st, ctx()).converge_reason.empty());
  // One run under cost_flat, busy (0.30; GE says 0.99): only it counts.
  Raw_test t = idle_test(0.99);
  t.weights  = cost_flat_weights(0.30);
  add_run(st, make_run(kL0.tv1(), {t}));
  const auto win = ladder_window(st, "S1");
  ASSERT_EQ(win.size(), 1u);
  const auto w = window_stats(win);
  EXPECT_NEAR(w.I_s, 0.30, 1e-4);
  EXPECT_EQ(w.weight, "cost_flat");
  // window_stats itself skips a foreign-weight run's I_s (never "mixed")
  std::vector<const Sim_run*> all;
  for (const auto& r : st.runs) {
    all.push_back(&r);
  }
  const auto wa = window_stats(all);
  EXPECT_NEAR(wa.I_s, 0.30, 1e-4);
  EXPECT_EQ(wa.weight, "cost_flat");
  const auto p = propose(st, ctx());
  EXPECT_FALSE(p.to.has_value()) << "L1 proposed from GE numbers mixed into a cost_flat window";
  EXPECT_EQ(p.converge_reason, "no-step");
  // A record that names the model's weight but predates the variant fields
  // (and the walker's) still counts; one with no `weight` at all is GE.
  Stats s;
  s.valid  = true;
  s.I_s    = 0.6;
  s.weight = "cost_flat";
  EXPECT_EQ(ladder_I(s), std::optional<double>{0.6});
  s.weight = "walker";
  EXPECT_EQ(ladder_I(s), std::optional<double>{0.6});
  s.weight.clear();
  EXPECT_FALSE(ladder_I(s).has_value());
  s.weight      = "ge";
  s.I_cost_flat = 0.4;  // the stored variant wins over I_s
  EXPECT_EQ(ladder_I(s), std::optional<double>{0.4});
}

TEST(SimTuneLadder, DescentReadsOnlyThisModelsWeight) {
  // At the default L1: busy runs recorded under GE (a driver predating
  // `weights`) never start the descent; a cost_flat run says what counts --
  // here busy, although its GE idleness reads 0.99.
  const auto run_of = [](bool legacy) {
    Raw_test t = legacy ? busy_test() : idle_test(0.99);  // the GE idleness: 0.02 or 0.99
    t.legacy   = legacy;
    if (!legacy) {
      t.weights = cost_flat_weights(0.02);
    }
    return make_run(kL1.tv1(), {t});
  };
  State st;
  for (int i = 0; i < 3; ++i) {
    add_run(st, run_of(true));
  }
  EXPECT_FALSE(propose(st, ctx()).to.has_value());
  EXPECT_TRUE(propose(st, ctx()).converge_reason.empty());  // keep profiling
  add_run(st, run_of(false));
  const auto p = propose(st, ctx());
  ASSERT_TRUE(p.to.has_value());
  EXPECT_EQ(*p.to, kL0);
  EXPECT_EQ(p.step, "L0");
}

TEST(SimTuneLadder, DescentToL0UnderAutoAndOn) {
  // One busy window is enough in either mode (the old rule waited for three
  // busy `on` windows, when L0 was the default and the step undid a trial).
  State st;
  add_run(st, make_run(kL1.tv1(), {busy_test()}));
  for (const auto m : {Mode::auto_, Mode::on}) {
    const auto p = propose(st, ctx(m));
    ASSERT_TRUE(p.to.has_value());
    EXPECT_EQ(*p.to, kL0);
    EXPECT_EQ(p.step, "L0");
  }
  // from L2 as well (a workload that turned busy)
  State l2;
  l2.incumbent = kL2;
  add_run(l2, make_run(kL2.tv1(), {busy_test()}));
  const auto q = propose(l2, ctx());
  ASSERT_TRUE(q.to.has_value());
  EXPECT_EQ(*q.to, kL0);
}

// ---- verdict ------------------------------------------------------------------------------

Sim_run trial_run(double cost_ratio, double instr_ratio, std::string_view end = "00000000000000aa", double pcore = 1.0) {
  Raw_test t   = idle_test(0.8);
  t.cpu_cycles = static_cast<uint64_t>(1'600'000'000 * cost_ratio);
  t.cpu_ns     = static_cast<uint64_t>(400'000'000 * cost_ratio);
  t.instr      = static_cast<uint64_t>(4'000'000'000 * instr_ratio);
  t.end_digest = std::string{end};
  t.pcore      = pcore;
  return make_run(kL1.tv1(), {t});
}

TEST(SimTuneVerdict, OracleThenSpeed) {
  const Sim_run base = make_run(kL0.tv1(), {idle_test(0.8)});
  auto          v    = judge(trial_run(0.6, 0.6), base);
  EXPECT_EQ(v.result, "accepted");
  EXPECT_EQ(v.oracle, "equal");
  EXPECT_NEAR(v.rho, 0.6, 1e-6);
  EXPECT_FALSE(v.decided_on_i);

  v = judge(trial_run(0.95, 0.95), base);
  EXPECT_EQ(v.result, "rejected");
  EXPECT_NEAR(v.rho, 0.95, 1e-6);

  v = judge(trial_run(0.5, 0.5, "00000000000000ff"), base);
  EXPECT_EQ(v.result, "divergence");
  EXPECT_EQ(v.oracle, "mismatch");
  ASSERT_EQ(v.tests.size(), 1u);
  EXPECT_EQ(v.tests.front(), "t.a");
}

TEST(SimTuneVerdict, RandomFillSkipsTheOracleOnlyWithDraws) {
  Raw_test      b     = idle_test(0.8);
  const Sim_run base  = make_run(kL0.tv1(), {b}, "random", 3);
  Raw_test      t     = idle_test(0.8);
  t.end_digest        = "0000000000000fff";  // a different `?` draw: legal nondeterminism
  t.cpu_cycles        = 800'000'000;
  t.cpu_ns            = 200'000'000;
  t.instr             = 2'000'000'000;
  const Sim_run trial = make_run(kL1.tv1(), {t}, "random", 3);
  const auto    v     = judge(trial, base);
  EXPECT_EQ(v.oracle, "skipped(random-fill)");
  EXPECT_EQ(v.result, "accepted");
  // random fill that drew nothing keeps the exact check
  const Sim_run base0  = make_run(kL0.tv1(), {b}, "random", 0);
  const Sim_run trial0 = make_run(kL1.tv1(), {t}, "random", 0);
  EXPECT_EQ(judge(trial0, base0).result, "divergence");
}

TEST(SimTuneVerdict, InstructionsDecideOnCoreNoiseOrDisagreement) {
  const Sim_run base = make_run(kL0.tv1(), {idle_test(0.8)});
  // cycles say 0.5 but the run sat on E-cores: instructions (0.97) decide
  auto          v    = judge(trial_run(0.5, 0.97, "00000000000000aa", 0.5), base);
  EXPECT_TRUE(v.decided_on_i);
  EXPECT_EQ(v.result, "rejected");
  // cycles and instructions disagree by > 0.2: instructions decide
  v = judge(trial_run(0.6, 0.95), base);
  EXPECT_TRUE(v.decided_on_i);
  EXPECT_NEAR(v.rho, 0.95, 1e-6);
  EXPECT_EQ(v.result, "rejected");
  // agreeing counters on P-cores: cycles decide
  v = judge(trial_run(0.9, 0.95), base);
  EXPECT_FALSE(v.decided_on_i);
  EXPECT_EQ(v.result, "accepted");
}

TEST(SimTuneVerdict, BaselineMustBeComparable) {
  State st;
  add_run(st, make_run(kL0.tv1(), {idle_test(0.8)}));
  Sim_run other_seed = make_run(kL0.tv1(), {idle_test(0.8)});
  other_seed.seed    = "99";
  add_run(st, other_seed);
  add_run(st, trial_run(0.6, 0.6));
  const Sim_run* base = find_baseline(st, kL0.tv1(), st.runs.back());
  ASSERT_NE(base, nullptr);
  EXPECT_EQ(base->seed, "1");  // the most recent COMPARABLE one, not the newest
  Sim_run tb = st.runs.back();
  tb.tb      = "another testbench";
  EXPECT_EQ(find_baseline(st, kL0.tv1(), tb), nullptr);
}

// ---- decide(): the whole loop over a replayed store -----------------------------------------

TEST(SimTuneDecide, ConvergesIdleToDirtyOnWithinThreeRuns) {
  State                    st;
  std::vector<std::string> lines;
  seed_l0(st, &lines);
  // One `lhd sim`: its setup applies a pending trial (an attempt), then the run.
  const auto               step = [&](Sim_run r, Mode m = Mode::auto_) {
    const bool attempt = st.trial.has_value();
    if (attempt) {
      apply_attempt(st, &lines);
    }
    add_run(st, std::move(r), &lines);
    auto out = decide(st, attempt ? over({}, m) : ctx(m));
    append(lines, out);
    return out;
  };
  // run 1: an L0 incumbent, idle design -> trial L1 pending
  auto o1 = step(make_run(kL0.tv1(), {idle_test(0.9)}));
  EXPECT_FALSE(o1.verdict.has_value());
  ASSERT_TRUE(o1.proposed.has_value());
  EXPECT_EQ(o1.proposed->to, kL1.tv1());
  EXPECT_FALSE(st.converged);
  // run 2: the trial vector, 40% cheaper, same digests -> accepted, then L2 proposed
  auto o2 = step(trial_run(0.6, 0.6));
  ASSERT_TRUE(o2.verdict.has_value());
  EXPECT_EQ(o2.verdict->result, "accepted");
  EXPECT_EQ(st.incumbent, std::optional<Tune_vector>{kL1});
  ASSERT_TRUE(o2.proposed.has_value());
  EXPECT_EQ(o2.proposed->to, kL2.tv1());
  // run 3: L2 is no better -> rejected (banned), incumbent stays L1, converged
  Raw_test t3   = idle_test(0.9);
  t3.cpu_cycles = static_cast<uint64_t>(1'600'000'000 * 0.59);
  t3.cpu_ns     = static_cast<uint64_t>(400'000'000 * 0.59);
  t3.instr      = static_cast<uint64_t>(4'000'000'000 * 0.59);
  auto o3       = step(make_run(kL2.tv1(), {t3}));
  ASSERT_TRUE(o3.verdict.has_value());
  EXPECT_EQ(o3.verdict->result, "rejected");
  EXPECT_EQ(st.incumbent, std::optional<Tune_vector>{kL1});
  EXPECT_TRUE(st.is_rejected(kL2.tv1()));
  EXPECT_TRUE(st.converged);
  EXPECT_FALSE(st.trial.has_value());

  // the whole history replays to the same state
  const State again = replay(lines);
  EXPECT_EQ(again.incumbent, st.incumbent);
  EXPECT_EQ(again.converged, st.converged);
  EXPECT_EQ(again.rejected, st.rejected);
  EXPECT_EQ(again.runs.size(), 3u);
}

TEST(SimTuneDecide, LfsrDescendsFromTheDefaultWithOneTrial) {
  State                    st;
  std::vector<std::string> lines;
  // run 1: the default L1 on an always-toggling design -> trial L0 pending
  add_run(st, make_run(kL1.tv1(), {busy_test()}), &lines);
  auto o1 = decide(st, ctx());
  append(lines, o1);
  ASSERT_TRUE(o1.proposed.has_value());
  EXPECT_EQ(o1.proposed->to, kL0.tv1());
  // run 2: L0 is 40% cheaper with the same digests -> accepted, converged busy
  apply_attempt(st, &lines);
  Raw_test t   = busy_test();
  t.cpu_cycles = static_cast<uint64_t>(1'600'000'000 * 0.6);
  t.cpu_ns     = static_cast<uint64_t>(400'000'000 * 0.6);
  t.instr      = static_cast<uint64_t>(4'000'000'000 * 0.6);
  add_run(st, make_run(kL0.tv1(), {t}), &lines);
  const auto o2 = decide(st, over());
  append(lines, o2);
  ASSERT_TRUE(o2.verdict.has_value());
  EXPECT_EQ(o2.verdict->result, "accepted");
  EXPECT_FALSE(o2.proposed.has_value());
  EXPECT_EQ(st.incumbent, std::optional<Tune_vector>{kL0});
  EXPECT_TRUE(st.converged);
  EXPECT_EQ(st.converged_reason, "busy");
  EXPECT_EQ(replay(lines).incumbent, st.incumbent);
}

TEST(SimTuneDecide, LfsrAtL0ConvergesWithZeroTrials) {
  State st;
  seed_l0(st);
  add_run(st, make_run(kL0.tv1(), {busy_test()}));
  const auto out = decide(st, ctx());
  EXPECT_FALSE(out.proposed.has_value());
  EXPECT_EQ(out.converge_reason, "busy");
  EXPECT_TRUE(st.converged);
  EXPECT_EQ(st.incumbent, std::optional<Tune_vector>{kL0});
}

TEST(SimTuneDecide, APendingTrialWaitsForItsAttempt) {
  State st;
  seed_l0(st);
  add_run(st, make_run(kL0.tv1(), {idle_test(0.9)}));
  (void)decide(st, ctx());
  ASSERT_TRUE(st.trial.has_value());
  // Runs of the incumbent (setups that did not apply it: e.g. not profiled)
  // neither judge nor abandon a trial nobody built.
  for (int i = 0; i < 4; ++i) {
    add_run(st, make_run(kL0.tv1(), {idle_test(0.9)}));
    EXPECT_FALSE(decide(st, ctx()).verdict.has_value());
  }
  EXPECT_TRUE(st.trial.has_value());
  EXPECT_FALSE(st.attempt_open);
  // A trial run before any attempt record is not judged either.
  add_run(st, trial_run(0.5, 0.5));
  EXPECT_FALSE(decide(st, ctx()).verdict.has_value());
  apply_attempt(st);
  add_run(st, trial_run(0.5, 0.5));
  const auto out = decide(st, over());
  ASSERT_TRUE(out.verdict.has_value());
  EXPECT_EQ(out.verdict->result, "accepted");
  EXPECT_EQ(out.verdict->structure, "S1");
  EXPECT_TRUE(out.verdict->charged);
}

// policy:F1 / caching-retention:F1: an attempt whose run has no comparable
// baseline (the args change every run) is abandoned, never re-proposed in the
// same decide(), re-proposed only after a fresh incumbent run, and `auto`
// converges after kMaxTrialAttempts attempts.
TEST(SimTuneDecide, IncomparableTrialsConvergeInsteadOfLooping) {
  State                    st;
  std::vector<std::string> lines;
  seed_l0(st, &lines);
  int                      arg  = 1000;
  const auto               next = [&](const Tune_vector& v) {
    Raw_root root;
    root.args = std::to_string(arg++);  // a different --arg every run
    Sim_run r = make_run_at(v.tv1(), {idle_test(0.9)}, root);
    add_run(st, r, &lines);
  };
  next(kL0);
  append(lines, decide(st, ctx()));
  ASSERT_TRUE(st.trial.has_value());
  int trials = 1;
  for (int round = 0; round < 6 && !st.converged; ++round) {
    if (st.trial) {
      apply_attempt(st, &lines);
      next(kL1);
      const auto out = decide(st, over());
      append(lines, out);
      ASSERT_TRUE(out.verdict.has_value());
      EXPECT_EQ(out.verdict->result, "abandoned");
      EXPECT_FALSE(out.proposed.has_value()) << "re-proposed in the decide() that abandoned it";
      EXPECT_FALSE(st.is_rejected(kL1.tv1()));  // abandoned is not banned
      // no incumbent run since: nothing to propose; converged only once exhausted
      const auto again = decide(st, ctx());
      append(lines, again);
      EXPECT_FALSE(again.proposed.has_value());
      EXPECT_EQ(st.converged, st.attempts(kL1.tv1(), "S1") >= cm1::kMaxTrialAttempts);
    } else {
      next(kL0);  // the incumbent runs: a fresh baseline for the new conditions
      const auto out = decide(st, ctx());
      append(lines, out);
      trials += out.proposed ? 1 : 0;
    }
  }
  EXPECT_TRUE(st.converged);
  EXPECT_EQ(st.converged_reason, "exhausted");
  EXPECT_EQ(trials, cm1::kMaxTrialAttempts);
  EXPECT_EQ(st.attempts(kL1.tv1(), "S1"), cm1::kMaxTrialAttempts);
  EXPECT_TRUE(st.exhausted(kL1.tv1(), "S1"));
  // a new structure (an edit) re-opens the step ...
  Raw_root edited;
  edited.structure = "S2";
  add_run(st, make_run_at(kL0.tv1(), {idle_test(0.9)}, edited));
  const auto re = propose(st, ctx());
  EXPECT_TRUE(re.to.has_value()) << re.converge_reason;
  // ... and so does an explicit `on` on the exhausted structure
  add_run(st, make_run(kL0.tv1(), {idle_test(0.9)}));
  EXPECT_EQ(propose(st, ctx()).converge_reason, "exhausted");
  EXPECT_TRUE(propose(st, ctx(Mode::on)).to.has_value());
  // the whole history replays to the same attempts
  const State again = replay(lines);
  EXPECT_EQ(again.attempts(kL1.tv1(), "S1"), cm1::kMaxTrialAttempts);
}

// caching-retention:F2 / soundness:S1: a trial binary that crashes is closed
// as run-failed (not banned on the first hit), and a second failure on the
// same structure bans it; auto converges.
TEST(SimTuneDecide, CrashingTrialIsBannedAfterRepeatedFailures) {
  State                    st;
  std::vector<std::string> lines;
  seed_l0(st, &lines);
  add_run(st, make_run(kL0.tv1(), {idle_test(0.9)}), &lines);
  append(lines, decide(st, ctx()));
  for (int k = 1; k <= cm1::kMaxTrialAttempts; ++k) {
    ASSERT_TRUE(st.trial.has_value()) << "attempt " << k;
    apply_attempt(st, &lines);
    const auto out = decide(st, over("run-failed"));  // no raw record: drv.bin died
    append(lines, out);
    ASSERT_TRUE(out.verdict.has_value());
    EXPECT_EQ(out.verdict->result, "run-failed");
    EXPECT_TRUE(out.verdict->failure());
    EXPECT_EQ(st.is_rejected(kL1.tv1()), k == cm1::kMaxTrialAttempts);
    EXPECT_FALSE(out.proposed.has_value());
    add_run(st, make_run(kL0.tv1(), {idle_test(0.9)}), &lines);  // the incumbent runs again
    append(lines, decide(st, ctx()));
  }
  EXPECT_TRUE(st.converged);
  EXPECT_EQ(st.converged_reason, "rejected");
  EXPECT_EQ(replay(lines).rejected, st.rejected);
}

TEST(SimTuneDecide, BuildFailureIsNotBannedOnTheFirstHit) {
  State st;
  seed_l0(st);
  add_run(st, make_run(kL0.tv1(), {idle_test(0.9)}));
  (void)decide(st, ctx());
  apply_attempt(st);
  // what the session does when inou.cgen.sim or the host build failed
  (void)close_trial(st, Verdict{.result = "build-failed", .oracle = "none", .reason = "cgen", .charged = true}, 1000);
  EXPECT_FALSE(st.is_rejected(kL1.tv1()));
  EXPECT_EQ(st.failures(kL1.tv1(), "S1"), 1);
  // a stale closure charges nothing
  add_run(st, make_run(kL0.tv1(), {idle_test(0.9)}));
  (void)decide(st, ctx());
  ASSERT_TRUE(st.trial.has_value());
  (void)close_trial(st, Verdict{.result = "abandoned", .oracle = "none", .reason = "stale: pinned", .charged = false}, 1000);
  EXPECT_EQ(st.attempts(kL1.tv1(), "S1"), 1);
}

TEST(SimTuneDecide, RandomIneligibleExhaustsWithoutABan) {
  State st;
  seed_l0(st);
  add_run(st, make_run(kL0.tv1(), {idle_test(0.9)}));
  (void)decide(st, ctx());
  apply_attempt(st);
  add_run(st, make_run(kL1.tv1(), {idle_test(0.9)}, "zero", 0, "plan.runtime_random=true\n"));
  const auto out = decide(st, over());
  ASSERT_TRUE(out.verdict.has_value());
  EXPECT_EQ(out.verdict->result, "random-ineligible");
  EXPECT_FALSE(st.is_rejected(kL1.tv1()));
  EXPECT_TRUE(st.exhausted(kL1.tv1(), "S1"));
}

// policy:F2: the trial run is judged on the trial gate, so a big win (a short
// trial run) is accepted instead of being filtered out as smoke.
TEST(SimTuneDecide, FastTrialIsJudged) {
  State st;
  seed_l0(st);
  add_run(st, make_run(kL0.tv1(), {idle_test(0.9)}));  // 400 ms: the baseline qualifies
  (void)decide(st, ctx());
  apply_attempt(st);
  const Sim_run fast = trial_run(0.1, 0.1);  // 40 ms of CPU
  EXPECT_FALSE(fast.stats.qualifies);
  EXPECT_TRUE(fast.stats.judgeable);
  add_run(st, fast);
  const auto out = decide(st, over());
  ASSERT_TRUE(out.verdict.has_value());
  EXPECT_EQ(out.verdict->result, "accepted");
  EXPECT_NEAR(out.verdict->rho, 0.1, 1e-6);
  // too short to time at all (10 ms): not judgeable, so the attempt is abandoned
  State st2;
  seed_l0(st2);
  add_run(st2, make_run(kL0.tv1(), {idle_test(0.9)}));
  (void)decide(st2, ctx());
  apply_attempt(st2);
  const Sim_run tiny = trial_run(0.025, 0.025);
  EXPECT_FALSE(tiny.stats.judgeable);
  add_run(st2, tiny);
  EXPECT_EQ(decide(st2, over()).verdict->result, "abandoned");
}

// lifecycle:R2-2: the ORACLE is not gated by the trial's CPU/cycle gate. A
// diverging trial run too short to time (a miscompile that skips work, or a
// testbench loop that ends early on a DUT signal) is a divergence -- banned,
// reported -- never a silent `abandoned`.
TEST(SimTuneDecide, ShortDivergingTrialIsADivergence) {
  const auto setup = [](State& st) {
    seed_l0(st);
    add_run(st, make_run(kL0.tv1(), {idle_test(0.9)}));  // a qualifying baseline
    (void)decide(st, ctx());
    apply_attempt(st);
  };
  // wrong end state, 10 ms of CPU: not judgeable, yet a mismatch
  State st;
  setup(st);
  const Sim_run tiny = trial_run(0.025, 0.025, "0000000000000bad");
  ASSERT_FALSE(tiny.stats.judgeable);
  add_run(st, tiny);
  const auto out = decide(st, over());
  ASSERT_TRUE(out.verdict.has_value());
  EXPECT_EQ(out.verdict->result, "divergence");
  EXPECT_EQ(out.verdict->oracle, "mismatch");
  EXPECT_TRUE(out.verdict->charged);
  EXPECT_TRUE(st.is_rejected(kL1.tv1()));
  EXPECT_EQ(st.incumbent, std::optional<Tune_vector>{kL0});
  // the oracle does not even wait for the session to end the attempt
  State early;
  setup(early);
  add_run(early, tiny);
  const auto e = decide(early, ctx());
  ASSERT_TRUE(e.verdict.has_value());
  EXPECT_EQ(e.verdict->result, "divergence");
  // a failing test status diverges as well (digests aside)
  State fail;
  setup(fail);
  Raw_test ft   = idle_test(0.8);
  ft.status     = "fail";
  ft.sim_cycles = 20'000;  // the test stopped early
  ft.cpu_ns     = 5'000'000;
  const auto fr = make_run(kL1.tv1(), {ft});
  ASSERT_FALSE(fr.stats.judgeable);
  add_run(fail, fr);
  EXPECT_EQ(decide(fail, over()).verdict->result, "divergence");
  // equal results that are too short to time are still abandoned (speed
  // needs a judgeable run) ...
  State eq;
  setup(eq);
  add_run(eq, trial_run(0.025, 0.025));
  EXPECT_EQ(decide(eq, over()).verdict->result, "abandoned");
  // ... and a run that did not finish normally judges nothing on a short record
  State crashed;
  setup(crashed);
  add_run(crashed, tiny);
  EXPECT_EQ(decide(crashed, over("run-failed")).verdict->result, "run-failed");
  EXPECT_FALSE(crashed.is_rejected(kL1.tv1()));
}

// lifecycle:R2-4 (model half): a closure the session marks as not an attempt
// (a stale-like close of a setup that will not run the trial) is uncharged; a
// verdict judged from a run is charged whatever the session says.
TEST(SimTuneDecide, UnchargedClosureKeepsTheBudget) {
  State st;
  seed_l0(st);
  add_run(st, make_run(kL0.tv1(), {idle_test(0.9)}));
  (void)decide(st, ctx());
  apply_attempt(st);
  Context cx         = over();
  cx.attempt_charged = false;
  cx.attempt_why     = "stale: this setup will not profile the trial";
  const auto out     = decide(st, cx);
  ASSERT_TRUE(out.verdict.has_value());
  EXPECT_EQ(out.verdict->result, "abandoned");
  EXPECT_FALSE(out.verdict->charged);
  EXPECT_EQ(st.attempts(kL1.tv1(), "S1"), 0);
  // a run-judged verdict is charged even under attempt_charged=false
  add_run(st, make_run(kL0.tv1(), {idle_test(0.9)}));
  (void)decide(st, ctx());
  ASSERT_TRUE(st.trial.has_value());
  apply_attempt(st);
  add_run(st, trial_run(0.5, 0.5));
  const auto j = decide(st, cx);
  ASSERT_TRUE(j.verdict.has_value());
  EXPECT_EQ(j.verdict->result, "accepted");
  EXPECT_TRUE(j.verdict->charged);
}

// lifecycle:R2-3: an explicit `on` re-opens a vector `auto` exhausted ONCE --
// its own attempts get the same budget -- and then converges `exhausted`
// itself instead of re-applying an incomparable trial every other run forever.
TEST(SimTuneDecide, OnReopensAnExhaustedVectorOnceThenConverges) {
  State                    st;
  std::vector<std::string> lines;
  seed_l0(st, &lines);
  int                      arg    = 1000;
  const auto               run_of = [&](const Tune_vector& v) {
    Raw_root root;
    root.args = std::to_string(arg++);  // a different --arg every run: never comparable
    add_run(st, make_run_at(v.tv1(), {idle_test(0.9)}, root), &lines);
  };
  // one `lhd sim` under mode m: its setup applies a pending trial (the run is
  // then the trial's), else the incumbent runs
  const auto invocation = [&](Mode m) {
    if (st.trial) {
      auto line = attempt_record(*st.trial, 1000, m);
      apply_record(st, line);
      lines.push_back(line);
      run_of(kL1);
      append(lines, decide(st, over({}, m)));
    } else {
      run_of(kL0);
      append(lines, decide(st, ctx(m)));
    }
  };
  for (int i = 0; i < 12 && !st.converged; ++i) {
    invocation(Mode::auto_);
  }
  ASSERT_TRUE(st.converged);
  EXPECT_EQ(st.converged_reason, "exhausted");
  EXPECT_EQ(st.attempts(kL1.tv1(), "S1"), cm1::kMaxTrialAttempts);
  EXPECT_EQ(st.attempts_on(kL1.tv1(), "S1"), 0);
  // `on` re-opens it ...
  int on_runs = 0;
  invocation(Mode::on);
  ++on_runs;
  ASSERT_TRUE(st.trial.has_value()) << "an explicit `on` must re-open an exhausted vector";
  // ... for kMaxTrialAttempts attempts of its own, then converges.
  for (int i = 0; i < 40 && (st.trial || !st.converged); ++i) {
    invocation(Mode::on);
    ++on_runs;
  }
  EXPECT_TRUE(st.converged) << "`on` never converged";
  EXPECT_EQ(st.converged_reason, "exhausted");
  EXPECT_EQ(st.attempts_on(kL1.tv1(), "S1"), cm1::kMaxTrialAttempts);
  EXPECT_EQ(st.attempts(kL1.tv1(), "S1"), 2 * cm1::kMaxTrialAttempts);
  EXPECT_TRUE(st.exhausted_on(kL1.tv1(), "S1"));
  EXPECT_LE(on_runs, 2 * cm1::kMaxTrialAttempts + 1);
  // further `on` runs keep profiling but never apply it again
  const size_t records = lines.size();
  for (int i = 0; i < 5; ++i) {
    invocation(Mode::on);
    EXPECT_FALSE(st.trial.has_value());
  }
  EXPECT_EQ(lines.size(), records + 5) << "a converged `on` run appends only its run record";
  // the whole history replays to the same budgets
  const State again = replay(lines);
  EXPECT_EQ(again.attempts_on(kL1.tv1(), "S1"), cm1::kMaxTrialAttempts);
  EXPECT_EQ(again.attempts(kL1.tv1(), "S1"), 2 * cm1::kMaxTrialAttempts);
  EXPECT_EQ(again.converged_reason, "exhausted");
  // a new structure re-opens it under `on` as well
  Raw_root edited;
  edited.structure = "S2";
  add_run(st, make_run_at(kL0.tv1(), {idle_test(0.9)}, edited));
  EXPECT_TRUE(propose(st, ctx(Mode::on)).to.has_value());
}

// `on` from a fresh store with never-comparable trials converges too.
TEST(SimTuneDecide, OnAloneConvergesOnIncomparableTrials) {
  State st;
  seed_l0(st);
  int   arg = 5000;
  for (int i = 0; i < 20 && !(st.converged && !st.trial); ++i) {
    Raw_root root;
    root.args = std::to_string(arg++);
    if (st.trial) {
      apply_record(st, attempt_record(*st.trial, 1000, Mode::on));
      add_run(st, make_run_at(kL1.tv1(), {idle_test(0.9)}, root));
      (void)decide(st, over({}, Mode::on));
    } else {
      add_run(st, make_run_at(kL0.tv1(), {idle_test(0.9)}, root));
      (void)decide(st, ctx(Mode::on));
    }
  }
  EXPECT_TRUE(st.converged);
  EXPECT_EQ(st.converged_reason, "exhausted");
  EXPECT_EQ(st.attempts_on(kL1.tv1(), "S1"), cm1::kMaxTrialAttempts);
}

// soundness:S1 (pins) / item 1: a pending trial goes STALE when a knob it moves
// is pinned or its `from` is not the resolved vector; a --run-only of the built
// trial tree is stale only when a pin contradicts that tree.
TEST(SimTuneDecide, PinsMakeATrialStale) {
  State st;
  seed_l0(st);
  add_run(st, make_run(kL0.tv1(), {idle_test(0.9)}));
  (void)decide(st, ctx());
  ASSERT_TRUE(st.trial.has_value());
  EXPECT_FALSE(stale_reason(st, ctx()).has_value());
  Pins off;
  off.dirty = false;  // resolves to exactly `from`, yet pins the knob L1 moves
  ASSERT_TRUE(stale_reason(st, ctx(Mode::auto_, off)).has_value());
  EXPECT_TRUE(stale_reason(st, ctx(Mode::auto_, off))->starts_with("stale"));
  Pins file_fence;
  file_fence.fence = 16;
  EXPECT_TRUE(stale_reason(st, ctx(Mode::auto_, {}, file_fence)).has_value());
  Pins lw;
  lw.live_words = 64;  // not moved by L1, but `from` is no longer the resolved vector
  EXPECT_TRUE(stale_reason(st, ctx(Mode::auto_, lw)).has_value());
  Pins same_be;
  same_be.llvm = false;  // pins a knob L1 does not move to the value it has: still valid
  EXPECT_FALSE(stale_reason(st, ctx(Mode::auto_, same_be)).has_value());
  // run-only of the built trial tree: an agreeing pin is fine, a contradicting one is stale
  Pins on;
  on.dirty = true;
  EXPECT_FALSE(stale_reason(st, ctx(Mode::auto_, on), true).has_value());
  EXPECT_TRUE(stale_reason(st, ctx(Mode::auto_, off), true).has_value());
}

TEST(SimTuneDecide, DivergenceBansTheVector) {
  State st;
  seed_l0(st);
  add_run(st, make_run(kL0.tv1(), {idle_test(0.9)}));
  (void)decide(st, ctx());
  apply_attempt(st);
  add_run(st, trial_run(0.5, 0.5, "0000000000000bad"));
  const auto out = decide(st, ctx());
  ASSERT_TRUE(out.verdict.has_value());
  EXPECT_EQ(out.verdict->result, "divergence");
  EXPECT_TRUE(st.is_rejected(kL1.tv1()));
  EXPECT_EQ(st.incumbent, std::optional<Tune_vector>{kL0});
}

// ---- the store ------------------------------------------------------------------------------

TEST(SimTuneStore, PartialTrailingLineIsDroppedAndTruncated) {
  const auto dir  = tmp_dir("partial");
  const auto path = dir + "/tune.jsonl";
  const auto good = decision_record(kL1, true, "busy", 1);
  {
    std::ofstream f(path);
    f << good << "\n" << R"({"schema":"lhd-sim-tune-1","kind":"run","id":"x)";
  }
  auto l = lhd::tune::load_jsonl(path, kStoreSchema);
  EXPECT_EQ(l.status, lhd::tune::Jsonl_load::Status::ok);
  EXPECT_TRUE(l.dropped_partial);
  ASSERT_EQ(l.lines.size(), 1u);
  EXPECT_EQ(replay(l.lines).incumbent, std::optional<Tune_vector>{kL1});
  std::string why;
  ASSERT_TRUE(lhd::tune::append_jsonl(path, {decision_record(kL2, true, "", 2)}, why)) << why;
  l = lhd::tune::load_jsonl(path, kStoreSchema);
  ASSERT_EQ(l.lines.size(), 2u);  // the append started on a clean line
  EXPECT_FALSE(l.dropped_partial);
  EXPECT_EQ(replay(l.lines).incumbent, std::optional<Tune_vector>{kL2});
}

TEST(SimTuneStore, ForeignSchemaIsSetAside) {
  const auto dir  = tmp_dir("foreign");
  const auto path = dir + "/tune.jsonl";
  {
    std::ofstream f(path);
    f << decision_record(kL1, true, "", 1) << "\n" << R"({"schema":"lhd-synth-tune-1","kind":"decision"})" << "\n";
  }
  const auto l = lhd::tune::load_jsonl(path, kStoreSchema);
  EXPECT_EQ(l.status, lhd::tune::Jsonl_load::Status::bad);
  EXPECT_TRUE(l.lines.empty());
  EXPECT_FALSE(fs::exists(path));
  EXPECT_TRUE(fs::exists(path + ".bad"));
  EXPECT_EQ(lhd::tune::load_jsonl(dir + "/missing.jsonl", kStoreSchema).status, lhd::tune::Jsonl_load::Status::missing);
}

// An observation run reads the decision without the lock: it must change nothing.
TEST(SimTuneStore, ReadOnlyLoadChangesNothing) {
  const auto dir  = tmp_dir("readonly");
  const auto path = dir + "/tune.jsonl";
  const auto body = decision_record(kL1, true, "busy", 1) + "\n" + R"({"schema":"lhd-sim-tune-1","kind":"run","id":"x)";
  {
    std::ofstream f(path);
    f << body;
  }
  auto l = lhd::tune::load_jsonl(path, kStoreSchema, /*read_only=*/true);
  EXPECT_EQ(l.status, lhd::tune::Jsonl_load::Status::ok);
  ASSERT_EQ(l.lines.size(), 1u);
  EXPECT_EQ(lhd::tune::read_file(path), std::optional<std::string>{body});  // the partial tail is still there
  {
    std::ofstream f(path);
    f << "garbage\n";
  }
  l = lhd::tune::load_jsonl(path, kStoreSchema, /*read_only=*/true);
  EXPECT_EQ(l.status, lhd::tune::Jsonl_load::Status::bad);
  EXPECT_TRUE(fs::exists(path));  // not renamed .bad
  EXPECT_FALSE(fs::exists(path + ".bad"));
}

TEST(SimTuneStore, CompactionKeepsTheNewestRunsAndDecision) {
  std::vector<std::string> lines;
  for (size_t i = 0; i < cm1::kKeepRuns + 10; ++i) {
    Sim_run r = make_run(kL0.tv1(), {busy_test()});
    r.id      = std::format("r{}", i);
    lines.push_back(run_record(r));
    if (i % 7 == 0) {
      lines.push_back(decision_record(kL0, true, i < 20 ? "smoke-only" : "busy", static_cast<int64_t>(i)));
    }
  }
  lines.push_back(R"({"schema":"lhd-sim-tune-1","kind":"from-a-newer-lhd"})");
  const auto kept = compact(lines);
  const auto st   = replay(kept);
  EXPECT_EQ(st.runs.size(), cm1::kKeepRuns);
  EXPECT_EQ(st.runs.back().id, std::format("r{}", cm1::kKeepRuns + 9));  // the NEWEST runs survive
  EXPECT_EQ(st.incumbent, std::optional<Tune_vector>{kL0});
  EXPECT_TRUE(st.converged);
  EXPECT_EQ(st.converged_reason, "busy");
  // runs + the newest decision + the unknown record
  EXPECT_EQ(kept.size(), cm1::kKeepRuns + 2);
  EXPECT_EQ(kept.back(), lines.back());
}

// lifecycle:R2-3: the non-run history is bounded too. A long-lived workdir
// (many edits, `on` attempts on every structure, flapping convergence
// reasons) compacts to the kept runs plus a bounded set of records, and the
// replay keeps the incumbent, the bans, the flip count, the budgets that still
// apply and the pending trial.
TEST(SimTuneStore, CompactionBoundsNonRunHistory) {
  State                    st;
  std::vector<std::string> lines;
  const auto               push = [&](std::string line) {
    apply_record(st, line);
    lines.push_back(std::move(line));
  };
  const auto run = [&](const Tune_vector& v, const std::string& structure, int arg) {
    Raw_root root;
    root.structure = structure;
    root.args      = std::to_string(arg);
    Sim_run r      = make_run_at(v.tv1(), {idle_test(0.9)}, root);
    r.id           = std::format("r{}", lines.size());
    push(run_record(r));
  };
  const auto trial_of = [&](const Tune_vector& to) {
    Trial t;
    t.from = st.incumbent.value_or(kL0).tv1();
    t.to   = to.tv1();
    t.step = "L1";
    push(trial_record(t));
  };
  const auto verdict_of = [&](std::string result, const std::string& structure, Mode m, bool charged = true) {
    push(attempt_record(*st.trial, 1, m));
    for (auto& line :
         close_trial(st, Verdict{.result = std::move(result), .oracle = "none", .structure = structure, .charged = charged}, 1)) {
      lines.push_back(std::move(line));
    }
  };
  const Tune_vector kLw{true, kTuneDefaultFenceRatio, 64, false};  // a vector banned by repeated failures
  // an early divergence ban and a failure ban on structures long gone
  run(kL0, "S0", 1);
  trial_of(kL2);
  verdict_of("divergence", "S0", Mode::auto_);
  for (int k = 0; k < cm1::kMaxTrialAttempts; ++k) {
    trial_of(kLw);
    verdict_of("run-failed", "S0", Mode::auto_);
  }
  ASSERT_TRUE(st.is_rejected(kL2.tv1()));
  ASSERT_TRUE(st.is_rejected(kLw.tv1()));
  // an accepted L1, then a long `on` life over 300 edits
  trial_of(kL1);
  run(kL1, "S0", 1);
  verdict_of("accepted", "S0", Mode::auto_);
  push(decision_record(kL1, true, "busy", 2));
  for (int e = 1; e <= 300; ++e) {
    const auto s = std::format("S{}", e);
    run(kL1, s, e);
    push(decision_record(kL1, true, e % 2 ? "no-step" : "busy", 3));  // a flapping reason
    trial_of(kL0);                                                    // a reverse step, never comparable
    verdict_of("abandoned", s, Mode::on);
    verdict_of("abandoned", s, Mode::on, /*charged=*/false);
  }
  // an open attempt at the end
  trial_of(kL0);
  push(attempt_record(*st.trial, 9, Mode::on));
  ASSERT_GT(lines.size(), 2000u);

  const auto kept    = compact(lines);
  const auto re      = replay(kept);
  size_t     non_run = 0;
  for (const auto& l : kept) {
    non_run += l.find(R"("kind":"run")") == std::string::npos ? 1 : 0;
  }
  EXPECT_EQ(re.runs.size(), cm1::kKeepRuns);
  EXPECT_LE(non_run, 8 * cm1::kKeepRuns) << "the non-run history is not bounded";
  EXPECT_LT(kept.size(), cm1::kCompactAt / 4);
  // the same decision, bans, accepted set, flips and pending trial
  EXPECT_EQ(re.incumbent, st.incumbent);
  EXPECT_EQ(re.converged, st.converged);
  EXPECT_EQ(re.converged_reason, st.converged_reason);
  EXPECT_TRUE(re.is_rejected(kL2.tv1()));
  EXPECT_TRUE(re.is_rejected(kLw.tv1())) << "a failure ban on a dead structure was lost";
  EXPECT_EQ(re.rejected.size(), st.rejected.size());
  EXPECT_NE(std::ranges::find(re.accepted, kL1.tv1()), re.accepted.end());
  EXPECT_EQ(re.flips, st.flips);
  ASSERT_TRUE(re.trial.has_value());
  EXPECT_EQ(re.trial->to, kL0.tv1());
  EXPECT_TRUE(re.attempt_open);
  EXPECT_EQ(re.attempt_mode, "on");
  // the budgets on the structures still in the window
  for (const auto& r : re.runs) {
    EXPECT_EQ(re.attempts(kL0.tv1(), r.structure), st.attempts(kL0.tv1(), r.structure)) << r.structure;
    EXPECT_EQ(re.attempts_on(kL0.tv1(), r.structure), st.attempts_on(kL0.tv1(), r.structure)) << r.structure;
  }
  EXPECT_EQ(re.last_closed_seq(kL0.tv1()).has_value(), true);
  // compaction is stable
  EXPECT_EQ(compact(kept), kept);
}

TEST(SimTuneStore, LockCloneAndSwap) {
  const auto           dir = tmp_dir("tree");
  lhd::tune::File_lock lk;
  std::string          why;
  ASSERT_TRUE(lk.open(dir + "/.lhd_sim.lock", why)) << why;
  EXPECT_TRUE(lk.exclusive(""));
  EXPECT_TRUE(lk.shared());
  EXPECT_TRUE(lk.exclusive(""));
  lk.release();

  fs::create_directories(dir + "/a/obj");
  {
    std::ofstream(dir + "/a/x.cpp") << "incumbent";
    std::ofstream(dir + "/a/obj/x.o") << "obj";
  }
  if (!lhd::tune::clone_tree(dir + "/a", dir + "/keep", why)) {
    GTEST_SKIP() << "no copy-on-write clone here: " << why;
  }
  std::ofstream(dir + "/a/x.cpp") << "trial";                                                     // the trial rewrites the tree ...
  EXPECT_EQ(lhd::tune::read_file(dir + "/keep/x.cpp"), std::optional<std::string>{"incumbent"});  // ... not the clone
  ASSERT_TRUE(lhd::tune::swap_dirs(dir + "/a", dir + "/keep", why)) << why;
  EXPECT_EQ(lhd::tune::read_file(dir + "/a/x.cpp"), std::optional<std::string>{"incumbent"});
  EXPECT_EQ(lhd::tune::read_file(dir + "/keep/x.cpp"), std::optional<std::string>{"trial"});
  lhd::tune::discard_dir(dir + "/keep");
  EXPECT_FALSE(fs::exists(dir + "/keep"));
}

// ---- export / import ------------------------------------------------------------------------

TEST(SimTuneFile, ExportImportRoundTrip) {
  Provenance p;
  p.structure = "S1";
  p.converged = true;
  p.created   = lhd::tune::iso8601_utc(0);
  Stats s;
  s.valid         = true;
  s.I_s           = 0.71;
  s.pairs         = 812;
  p.stats         = s;
  const auto text = tune_file_json(kL2, p);
  EXPECT_EQ(p.created, "1970-01-01T00:00:00Z");
  std::string err;
  const auto  f = parse_tune_file(text, err);
  ASSERT_TRUE(f.has_value()) << err;
  EXPECT_EQ(f->pins, (Pins{true, 0, 256, false}));
  EXPECT_EQ(f->structure, "S1");
  EXPECT_TRUE(f->converged);
  // file-sourced knobs resolve as "file"
  const auto r = resolve({}, f->pins, kL0);
  EXPECT_EQ(r.v, kL2);
  EXPECT_EQ(r.dirty, Source::file);

  // `auto` in the file pins nothing; a foreign schema and a bad knob are errors
  const auto partial = parse_tune_file(
      R"({"schema":"lhd-sim-tune-file-1","vector":"","knobs":{"dirty":"on","fence":"auto","live_words":"auto","backend":"auto"}})",
      err);
  ASSERT_TRUE(partial.has_value()) << err;
  EXPECT_EQ(partial->pins, (Pins{true, std::nullopt, std::nullopt, std::nullopt}));
  EXPECT_FALSE(parse_tune_file(R"({"schema":"x"})", err).has_value());
  EXPECT_FALSE(parse_tune_file(R"({"schema":"lhd-sim-tune-file-1","knobs":{"dirty":"maybe"}})", err).has_value());
  EXPECT_TRUE(err.find("dirty") != std::string::npos) << err;
}

// options-ux:F5: a hand-written file's JSON bool / integer knobs.
TEST(SimTuneFile, JsonTypedKnobs) {
  std::string err;
  const auto  f = parse_tune_file(R"({"schema":"lhd-sim-tune-file-1","knobs":{"dirty":true,"fence":0,"live_words":64}})", err);
  ASSERT_TRUE(f.has_value()) << err;
  EXPECT_EQ(f->pins, (Pins{true, 0, 64, std::nullopt}));
  const auto g = parse_tune_file(R"({"schema":"lhd-sim-tune-file-1","knobs":{"dirty":false}})", err);
  ASSERT_TRUE(g.has_value()) << err;
  EXPECT_EQ(g->pins.dirty, std::optional<bool>{false});
  // anything else is an error naming the knob, never a silent `auto`
  for (const auto* bad : {R"({"schema":"lhd-sim-tune-file-1","knobs":{"fence":1.5}})",
                          R"({"schema":"lhd-sim-tune-file-1","knobs":{"dirty":1}})",
                          R"({"schema":"lhd-sim-tune-file-1","knobs":{"backend":true}})",
                          R"({"schema":"lhd-sim-tune-file-1","knobs":{"live_words":null}})",
                          R"({"schema":"lhd-sim-tune-file-1","knobs":{"fence":-3}})"}) {
    err.clear();
    EXPECT_FALSE(parse_tune_file(bad, err).has_value()) << bad;
    EXPECT_TRUE(err.starts_with("bad knob: ")) << err;
  }
  err.clear();
  (void)parse_tune_file(R"({"schema":"lhd-sim-tune-file-1","knobs":{"fence":1.5}})", err);
  EXPECT_TRUE(err.find("fence") != std::string::npos) << err;
  err.clear();
  (void)parse_tune_file(R"({"schema":"lhd-sim-tune-file-1","knobs":{"backend":true}})", err);
  EXPECT_TRUE(err.find("backend") != std::string::npos) << err;
}

// ---- the envelope member --------------------------------------------------------------------

TEST(SimTuneEnvelope, JsonAndNote) {
  Envelope e;
  e.mode      = Mode::auto_;
  e.enabled   = true;
  e.profiling = true;
  e.fill      = "zero";
  e.applied   = resolve({}, {}, std::nullopt);
  Stats s;
  s.valid         = true;
  s.I_s           = 0.12;
  s.pairs         = 812;
  s.qualifies     = true;
  e.stats         = s;
  e.pending       = kL0.tv1();
  const auto note = envelope_note(e);
  EXPECT_EQ(note, "applied d=on f=16 lw=256 be=slop (default) | I_s=0.12 q=0.00 pairs=812 | next setup: TRIAL d=off f=none");

  rapidjson::Document d;
  const auto          j = envelope_json(e);
  d.Parse(j.c_str());
  ASSERT_FALSE(d.HasParseError()) << j;
  EXPECT_EQ(d["schema_version"].GetInt(), 1);
  EXPECT_STREQ(d["mode"].GetString(), "auto");
  EXPECT_STREQ(d["applied"]["vector"].GetString(), "tv1:d=on;f=16;lw=256;be=slop");
  EXPECT_EQ(d["applied"]["live_words"].GetUint64(), 256u);
  EXPECT_STREQ(d["source"]["fence"].GetString(), "default");
  EXPECT_TRUE(d["trial"].IsNull());
  EXPECT_TRUE(d["verdict"].IsNull());
  EXPECT_STREQ(d["pending"].GetString(), "tv1:d=off;f=none;lw=256;be=slop");
  EXPECT_STREQ(d["reproduce"].GetString(),
               "--set sim.tune.dirty=on --set sim.tune.fence=16 --set sim.tune.live_words=256 --set sim.tune.backend=slop "
               "--set sim.unknown_zero=true");
  EXPECT_STREQ(d["note"].GetString(), note.c_str());

  // a trial a --setup-only applied is still pending: it awaits its run, not a setup
  Envelope awaiting = e;
  awaiting.trial    = Trial{.from = kL1.tv1(), .to = kL0.tv1(), .step = "L0"};
  EXPECT_TRUE(envelope_note(awaiting).ends_with("| the trial awaits its run")) << envelope_note(awaiting);

  // disabled: the reason rides the note
  Envelope off;
  off.enabled = false;
  off.reason  = "no-workdir";
  off.applied = resolve({}, {}, std::nullopt);
  EXPECT_EQ(envelope_note(off), "applied d=on f=16 lw=256 be=slop (default) | tuner off (no-workdir)");
}

}  // namespace
