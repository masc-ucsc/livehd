//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The `lhd sim` profile-guided tuner's MODEL (sim_profile.md §5-§8): knob
// precedence, the per-run activity statistics, the tune-store replay, the
// dirty/fence ladder with its gates, the trial verdict (oracle first, then
// speed), the sim.tune.file export/import format and the envelope member.
//
// Everything here is PURE over plain structs -- no clock, no filesystem, no
// Options/Result -- so lhd_sim_tune_test pins the policy without an engine.
// lhd_sim_tune_session.cpp is the glue that reads the workdir and drives it.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "sim_tune_vector.hpp"

namespace lhd::sim_tune {

using livehd::sim::Tune_vector;

inline constexpr std::string_view kStoreSchema    = "lhd-sim-tune-1";          // tune.jsonl records
inline constexpr std::string_view kRawSchema      = "lhd-sim-tune-raw-1";      // drv.bin's per-run raw file
inline constexpr std::string_view kFileSchema     = "lhd-sim-tune-file-1";     // sim.tune.export / sim.tune.file
inline constexpr std::string_view kAppliedSchema  = "lhd-sim-tune-applied-1";  // <simdir>/tune_applied.json
inline constexpr std::string_view kRetainedSchema = "lhd-sim-retained-1";      // <workdir>/sim_tune/retained.json

// cm-1: the versioned policy constants. Calibrated once, never fitted per
// design (sim_profile.md §8.1); a change here is a new model name.
namespace cm1 {
// cm-2 (2026-09-18): calibrated on 25 lhdsuite + lhdtrack design/source pairs
// against measured L0/L1/L2 retired instructions per cycle. With the
// cost_flat weight every pair where dirty gating loses has I_s <= 0.39 (dino,
// matched_filter, the LFSR-driven lhdtrack DUTs) and every pair where it wins
// has I_s >= 0.54 (xs_alu 4x, xs_renametable 6x, minion 2-3x, xs_rob 158x);
// GE weights misclassified xs_renametable pyrope2 and xs_rob. q was 0 on all.
inline constexpr std::string_view kName              = "cm-2";
inline constexpr uint64_t         kMinPairs          = 64;      // quality gate: sampled consecutive-cycle pairs
inline constexpr uint64_t         kMinPostWarmCycles = 10'000;  // quality gate: cycles after the sampler warm-up
inline constexpr double           kMinCpuMs          = 200.0;   // quality gate: CPU over every test of the run
// A TRIAL run is judged on a looser gate: a big win makes it short, so the CPU
// floor above (the baseline's noise floor) would filter out exactly the wins.
// Pairs are a statistics requirement, not a verdict one.
inline constexpr double           kTrialMinCpuMs     = 20.0;
// The class weight whose idleness is the ladder's I_s: "ge" | "sites" | "cost"
// | "cost_flat" (the driver reports all four; a raw file with none of them
// falls back to the support table's GE idleness, then to the walker's).
inline constexpr std::string_view kSupportWeight     = "cost_flat";
// The L1 threshold sits in the measured 0.39..0.54 gap, biased low: a missed
// win costs up to 158x, a wasted trial one rebuild (reverted without another).
inline constexpr double           kL1Idle            = 0.45;  // dirty gating (L1) pays when support idleness reaches this ...
inline constexpr double           kL1Quiescent       = 0.05;  // ... or this many pairs change nothing at all; below both, L0
inline constexpr double           kL2Idle            = 0.7;   // L1 -> L2 (fence every single-use module)
inline constexpr double           kBusyIdle          = 0.3;   // below both: the always-toggling (LFSR) regime, whose
inline constexpr double           kBusyQuiescent     = 0.01;  // L1 -> L0 trial passes the payoff gate outright
inline constexpr double           kGainPerIdle       = 1.0;   // predicted_gain = I_s (measured gains were never below it)
inline constexpr double           kExpectedRuns      = 20.0;  // remaining runs the payoff gate amortizes over
inline constexpr double           kGateIdle          = 0.7;   // I_s above this passes the payoff gate outright
inline constexpr double           kAcceptRho         = 0.93;  // a trial must be >= 7% cheaper per cycle
inline constexpr double           kMinPcore          = 0.9;   // below: cycles are core-type noise; decide on instructions
inline constexpr double           kMaxRhoDisagree    = 0.2;   // |rho_cycles - rho_instr| above: decide on instructions
inline constexpr int              kSmokeRuns         = 3;     // consecutive non-qualifying profiles -> converged
// Setups that applied one vector on one structure without a final verdict
// (abandoned, build- or run-failed) before `auto` stops proposing it there
// (EXHAUSTED; a structure change re-opens it). An explicit `on` re-opens an
// exhausted vector ONCE: its own attempts get this same budget, after which
// `on` converges `exhausted` too. Build and run failures ban the vector only
// once they reach this count on a structure.
inline constexpr int              kMaxTrialAttempts  = 2;
inline constexpr int              kMaxFlips          = 3;     // accepted trials per profile generation
inline constexpr size_t           kWindow            = 4;     // qualifying runs the ladder averages
inline constexpr size_t           kCompactAt         = 2000;  // records before the store is compacted
inline constexpr size_t           kKeepRuns          = 32;    // run records a compaction keeps
}  // namespace cm1

enum class Mode : uint8_t { auto_, on, off };
[[nodiscard]] std::optional<Mode> parse_mode(std::string_view v);  // strict auto|on|off
[[nodiscard]] std::string_view    mode_name(Mode m);

// Where a resolved knob came from: explicit --set > sim.tune.file > the
// workdir's decision > lhd's built-in default.
enum class Source : uint8_t { dflt, store, file, explicit_ };
[[nodiscard]] std::string_view source_name(Source s);

// Knobs one source pins; nullopt = that source says `auto`.
struct Pins {
  std::optional<bool>     dirty;
  std::optional<int64_t>  fence;
  std::optional<uint64_t> live_words;
  std::optional<bool>     llvm;
  [[nodiscard]] bool      any() const { return dirty || fence || live_words || llvm; }
  bool                    operator==(const Pins&) const = default;
};

struct Resolved {
  Tune_vector v;
  Source      dirty                             = Source::dflt;
  Source      fence                             = Source::dflt;
  Source      live_words                        = Source::dflt;
  Source      backend                           = Source::dflt;
  bool        operator==(const Resolved&) const = default;
};

// Pure precedence per knob. Projection: when dirty comes from an explicit set
// or the file and fence does not, fence follows the DEFAULT rule for the
// resolved dirty -- never the store's fence, which was chosen for the store's
// dirty (a fence without dirty gating is the measured-bad case).
[[nodiscard]] Resolved resolve(const Pins& explicit_pins, const Pins& file_pins, const std::optional<Tune_vector>& store);

// The explicit knob pins in `--set` order: the LAST occurrence wins and `auto`
// unpins (config entries come first, CLI entries after). `err` names a bad value.
[[nodiscard]] Pins pins_from_sets(const std::vector<std::pair<std::string, std::string>>& sets, std::string& err);

// The effective value of `sim.<flag>` in `sets` (last occurrence), if any.
[[nodiscard]] std::optional<std::string> last_set(const std::vector<std::pair<std::string, std::string>>& sets,
                                                  std::string_view                                        key);

// ---- old spellings (ruling 6: an immediate directed rename error) ----------
struct Rename_hint {
  std::string new_flag;  // under sim.*, e.g. "tune.dirty"
  std::string value;     // the value translated into the new grammar
};
[[nodiscard]] std::optional<Rename_hint> renamed_sim_flag(std::string_view flag, std::string_view value);

// ---- one run's measurements -------------------------------------------------
struct Test_row {
  std::string test;
  std::string status;  // pass | fail | error
  uint64_t    sim_cycles   = 0;
  uint64_t    cpu_ns       = 0;
  uint64_t    cpu_cycles   = 0;
  uint64_t    instructions = 0;
  double      pcore_frac   = 1.0;
  std::string end_digest;
  std::string out_digest;
};

// Cycle-weighted activity statistics of one run (or a window of runs).
struct Stats {
  bool                  valid   = false;  // at least one test carried a profile with pairs
  double                I_s     = 0.0;    // support idleness under kSupportWeight (see `weight`); the walker's E without tables
  double                I       = 0.0;    // occurrence idleness (GE-weighted own-state stillness)
  double                q       = 0.0;    // fraction of pairs where nothing changed at all
  bool                  support = false;  // I_s came from the plan's support tables
  bool                  exact   = false;  // ... and those were exact (no bucketing)
  std::string           weight;           // the class weight I_s used: ge | sites | cost | cost_flat | walker
  // The support idleness under each class weight the driver reports (nullopt:
  // the raw file did not carry it -- older drivers report only GE).
  std::optional<double> I_ge;
  std::optional<double> I_sites;
  std::optional<double> I_cost;
  std::optional<double> I_cost_flat;
  uint64_t              pairs     = 0;
  uint64_t              cycles    = 0;      // DUT cycles of the tests with a profile
  uint64_t              postwarm  = 0;      // cycles after the sampler warm-up
  double                cpu_ms    = 0;      // CPU over every test of the run
  bool                  qualifies = false;  // the full gate: counts toward decisions, may be a baseline
  bool                  judgeable = false;  // the trial gate: a trial vector's run may be judged
};

struct Run {
  std::string                                      id;  // raw-file basename (dedup key)
  int64_t                                          t = 0;
  std::string                                      src;        // lhd | direct
  std::string                                      mode;       // the lhd mode that ran it (auto|on) or "direct"
  std::string                                      vector;     // the binary's baked tv1 ("" when it predates the tune-id TU)
  std::string                                      structure;  // the binary's baked structure key (vector-independent)
  // Digest of the roots' baked codegen tables WITHOUT the four tune-vector
  // lines: two runs whose sim.debug / sim.slop_u / runtime_support differ never
  // judge each other ("" when no root reports a table).
  std::string                                      codegen;
  // (module, structure) of every DUT root that ran; raw-file only (ingest uses
  // it to route the file to its design), never written into a record.
  std::vector<std::pair<std::string, std::string>> roots;
  std::string                                      tb;  // testbench digest (the driver source)
  std::string                                      host;
  bool                                             init_zero = false;
  std::string                                      seed;
  std::string                                      fill;  // zero | random
  uint64_t                                         unknown_draws  = 0;
  bool                                             runtime_random = false;  // the plan has a runtime_random memory collision
  std::vector<std::string>                         selected;
  std::string                                      args;  // canonical text of the driver's args object
  std::string                                      counters;
  double                                           setup_ms = 0;      // inou.cgen.sim + sim.hostbuild of the lhd call that built it
  bool                                             regen    = false;  // that setup regenerated the root (cold, or a new vector)
  std::vector<Test_row>                            tests;
  Stats                                            stats;
};

// A drv.bin raw run file (DESIGN §6.8) -> one run, with its statistics.
[[nodiscard]] std::optional<Run> run_from_raw(std::string_view raw_json, std::string& err);
[[nodiscard]] std::string        run_record(const Run& r);

// The idleness the LADDER reads from one run's stats: the kSupportWeight
// variant when the run recorded it, else I_s when it was computed under that
// weight (a record with no `weight` predates the variants: GE) or by the
// walker (a plan without support tables); nullopt for a run measured under
// another class weight -- it must not be averaged with this model's numbers.
[[nodiscard]] std::optional<double> ladder_I(const Stats& s);

// Cycle-weighted mean over several runs (the ladder's window). I_s averages
// ladder_I() only (a run without one adds no I_s); `weight` is kSupportWeight
// (or "walker" when every contributing run fell back to the walker).
[[nodiscard]] Stats window_stats(const std::vector<const Run*>& runs);

// ---- the store --------------------------------------------------------------
struct Trial {
  std::string from;  // tv1 of the incumbent it is measured against
  std::string to;    // tv1 under trial
  std::string step;  // L1 | L2 | L0 (reverse)
  std::string gate;
  size_t      seq = 0;  // record index (runs after it count toward the verdict)
  int64_t     t   = 0;
};

// How a pending trial ended. A setup that APPLIES it starts an attempt; every
// attempt ends in exactly one verdict:
//  - final: accepted | rejected | divergence | random-ineligible;
//  - not final (the vector may be proposed again, up to kMaxTrialAttempts per
//    structure): abandoned (no comparable qualifying run, or the attempt was
//    superseded), build-failed (cgen or the host build failed), run-failed
//    (drv.bin crashed, or exited without its raw run record);
//  - a STALE trial (a knob it moves got pinned, or its `from` is no longer the
//    resolved vector) closes as `abandoned` with charged=false.
struct Verdict {
  std::string              from;
  std::string              to;
  std::string              result;
  std::string              oracle;  // equal | mismatch | skipped(random-fill) | none
  std::string              reason;
  std::string              structure;             // the structure the attempt ran on (the exhaustion key)
  std::string              mode;                  // the mode of the setup that applied the attempt (auto | on | "")
  bool                     charged      = false;  // counts as an attempt of `to` on `structure`
  double                   rho          = 0.0;
  double                   rho_c        = 0.0;
  double                   rho_i        = 0.0;
  bool                     decided_on_i = false;
  std::vector<std::string> tests;  // the diverging tests (divergence only)
  // Speed and oracle losses ban at once; a failure bans only when repeated
  // (State::failures), since a bad $CXX or a full disk is not the vector's fault.
  [[nodiscard]] bool       bans() const { return result == "rejected" || result == "divergence"; }
  [[nodiscard]] bool       failure() const { return result == "build-failed" || result == "run-failed"; }
  [[nodiscard]] bool       final_() const {
    return result == "accepted" || result == "rejected" || result == "divergence" || result == "random-ineligible";
  }
};

struct State {
  std::vector<Run>           runs;
  std::vector<size_t>        run_seq;  // record index of each run
  std::optional<Tune_vector> incumbent;
  bool                       converged = false;
  std::string                converged_reason;
  std::optional<Trial>       trial;                 // the ONE pending trial
  bool                       attempt_open = false;  // a setup applied `trial` and no verdict closed it yet
  size_t                     attempt_seq  = 0;      // record index of that `attempt` record
  std::string                attempt_mode;          // the mode of the setup that applied it
  std::vector<std::string>   rejected;              // banned vectors
  std::vector<std::string>   accepted;              // vectors a verdict accepted
  int                        flips = 0;             // accepted verdicts in this profile generation
  size_t                     seq   = 0;             // records applied
  std::optional<Verdict>     last_verdict;
  struct Closed {
    std::string to;
    std::string structure;
    std::string result;
    std::string mode;  // of the attempt it closed
    bool        charged = false;
    size_t      seq     = 0;
  };
  std::vector<Closed>                 closed;  // every verdict, in order
  [[nodiscard]] bool                  is_rejected(std::string_view tv) const;
  // Charged attempts of `to` on `structure`, the failures among them, and the
  // ones a setup under `on` applied.
  [[nodiscard]] int                   attempts(std::string_view to, std::string_view structure) const;
  [[nodiscard]] int                   failures(std::string_view to, std::string_view structure) const;
  [[nodiscard]] int                   attempts_on(std::string_view to, std::string_view structure) const;
  // `auto` proposes `to` on `structure` no more (every charged attempt counts).
  [[nodiscard]] bool                  exhausted(std::string_view to, std::string_view structure) const;
  // `on` proposes it no more: its OWN attempts used the budget (so `on`
  // re-opens what `auto` exhausted exactly once, and never loops).
  [[nodiscard]] bool                  exhausted_on(std::string_view to, std::string_view structure) const;
  // Record index of the newest verdict on `to` (nullopt: never judged).
  [[nodiscard]] std::optional<size_t> last_closed_seq(std::string_view to) const;
};

// Apply one record, in file order (unknown kinds are ignored, so a newer lhd's
// records never make an older replay fail).
void                apply_record(State& st, std::string_view line);
[[nodiscard]] State replay(const std::vector<std::string>& lines);

[[nodiscard]] std::string trial_record(const Trial& tr);
// A setup applied the pending trial: one attempt starts (`m`: that setup's mode).
[[nodiscard]] std::string attempt_record(const Trial& tr, int64_t t, Mode m = Mode::auto_);
[[nodiscard]] std::string verdict_record(const Verdict& v, int64_t t);
[[nodiscard]] std::string decision_record(const Tune_vector& v, bool converged, std::string_view reason, int64_t t);

// Bound the store: the newest kKeepRuns runs, the newest decision (and the
// newest converged one, where the flip count restarts), the pending trial and
// its attempts, and the verdicts replay still needs -- every ban, the accepted
// ones of this profile generation plus the newest per vector, every verdict on
// a structure a kept run has (attempt budgets), the failures behind a failure
// ban, and the newest verdict per vector (the fresh-baseline rule). Replaying
// the result gives the same incumbent, bans, budgets and pending trial.
[[nodiscard]] std::vector<std::string> compact(const std::vector<std::string>& lines);

// ---- verdict ----------------------------------------------------------------
// Two runs measure the same thing: equal structure, testbench, host, init mode,
// seed, fill, ordered test selection and args, and at least one common test.
[[nodiscard]] bool       comparable(const Run& a, const Run& b);
[[nodiscard]] const Run* find_baseline(const State& st, std::string_view from, const Run& trial_run);
// Oracle first (digests + status of every common test, unless random fill drew
// a `?`), then rho = CPU-share-weighted per-cycle cost ratio trial/baseline.
[[nodiscard]] Verdict    judge(const Run& trial_run, const Run& base);
// The two halves. judge_oracle() needs no timing: result is `divergence` on a
// mismatch, else "" with `oracle` set. judge_speed() completes an oracle
// verdict that did not diverge with the speed decision.
[[nodiscard]] Verdict    judge_oracle(const Run& trial_run, const Run& base);
[[nodiscard]] Verdict    judge_speed(const Run& trial_run, const Run& base, Verdict oracle);

// The ladder's window on `structure`: its newest (up to kWindow) qualifying
// runs that carry a ladder_I().
[[nodiscard]] std::vector<const Run*> ladder_window(const State& st, std::string_view structure);

// ---- policy -----------------------------------------------------------------
struct Context {
  Mode                   mode = Mode::auto_;
  Pins                   explicit_pins;         // --set sim.tune.X (frozen: never moved by a trial)
  Pins                   file_pins;             // sim.tune.file (frozen too)
  bool                   may_propose  = false;  // tuner enabled and this run may start a trial
  int64_t                now          = 0;
  // The session's word on the OPEN attempt: its run is over (this invocation
  // ran it, or a new setup / a vanished trial tree supersedes it). decide()
  // then closes it -- with a verdict when a judgeable run of it has a
  // baseline, else as `attempt_failed` (build-failed | run-failed) or
  // `abandoned`, `attempt_why` being the reason.
  bool                   attempt_over = false;
  std::string            attempt_failed;
  std::string            attempt_why;
  // Whether that closure charges an attempt (false: a stale-like close -- the
  // setup that would rebuild the trial's tree will not run it, or a pin
  // made it stale). A verdict judged from a run is always charged.
  bool                   attempt_charged = true;
  // explicit > file > the State's incumbent > default (never the pending trial)
  [[nodiscard]] Resolved current(const State& st) const { return resolve(explicit_pins, file_pins, st.incumbent); }
};

struct Proposal {
  std::optional<Tune_vector> to;
  std::string                step;
  std::string                gate;             // the payoff-gate decision (recorded in the envelope)
  std::string                converge_reason;  // set when nothing can be proposed for the current data
};
[[nodiscard]] Proposal propose(const State& st, const Context& cx);

// The measured rebuild cost the payoff gate weighs a trial against.
[[nodiscard]] double econ_rebuild_ms(const State& st);

struct Outcome {
  std::vector<std::string> append;  // records to persist, in order (already applied to the State)
  std::optional<Verdict>   verdict;
  std::optional<Trial>     judged;    // the trial that verdict closed
  std::optional<Trial>     proposed;  // a new pending trial
  std::string              gate;
  std::string              converge_reason;
};
// After new runs were applied: judge the open attempt (or close it when the
// context says it is over), then propose the next ladder step or declare
// convergence. The ORACLE runs on every run of the trial vector that has a
// comparable baseline, however short (a mismatch is authoritative); only the
// speed half needs a judgeable run. A decide() that closed an attempt without
// a final verdict proposes nothing: the next proposal needs a fresh qualifying
// run of the incumbent (a baseline for the current conditions), and both
// `auto` and `on` stop after kMaxTrialAttempts attempts of one vector on one
// structure, so they always converge.
[[nodiscard]] Outcome decide(State& st, const Context& cx);

// Close the pending trial with `v` (from/to/structure filled in): the verdict
// record and the decision it implies, already applied to `st`.
[[nodiscard]] std::vector<std::string> close_trial(State& st, Verdict v, int64_t now);

// Why the pending trial no longer describes this workdir (nullopt: it does).
// At a SETUP: a knob the step moves is pinned (explicit or file), or its
// `from` is not the resolved vector. At a --run-only that would run the
// already-built trial tree (`built_trial`): a pin contradicts that tree.
[[nodiscard]] std::optional<std::string> stale_reason(const State& st, const Context& cx, bool built_trial = false);

// ---- sim.tune.file (export / import) ------------------------------------------
struct Tune_file {
  Pins        pins;
  std::string vector;
  std::string structure;
  bool        converged = false;
};
[[nodiscard]] std::optional<Tune_file> parse_tune_file(std::string_view text, std::string& err);

struct Provenance {
  std::string          structure;
  bool                 converged = false;
  std::optional<Stats> stats;
  std::string          created;  // ISO-8601 UTC
};
[[nodiscard]] std::string tune_file_json(const Tune_vector& v, const Provenance& p);

// ---- the envelope member ------------------------------------------------------
struct Envelope {
  Mode                       mode    = Mode::auto_;
  bool                       enabled = false;
  std::string                reason;  // why disabled ("" when enabled)
  bool                       profiling = false;
  std::string                fill      = "random";
  Resolved                   applied;
  // false: a --run-only tree the tuner cannot vouch for (no tune_applied.json,
  // another design's tree, a refused trial tree): sources print as "built"
  bool                       applied_known = true;
  std::optional<Stats>       stats;
  std::optional<Trial>       trial;  // the trial this invocation built/ran
  std::optional<Verdict>     verdict;
  std::string                gate;
  std::vector<std::string>   rejected;
  std::optional<std::string> pending;
  bool                       converged = false;
  std::vector<std::string>   notes;
};
[[nodiscard]] std::string short_vector(const Tune_vector& v);  // "d=off f=none lw=256 be=slop"
[[nodiscard]] std::string reproduce(const Tune_vector& v, std::string_view fill);
[[nodiscard]] std::string envelope_note(const Envelope& e);
[[nodiscard]] std::string envelope_json(const Envelope& e);

}  // namespace lhd::sim_tune
