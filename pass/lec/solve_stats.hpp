// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// cvc5 solve-insight accounting for `lhd lec` / `lhd formal verify` (formal.stats,
// a.k.a. the `--stats` CLI sugar). OFF by default and strictly zero-cost when off:
// nothing here is constructed, no cvc5 option is set, and no plugin is registered.
//
// TWO SOURCES, very different costs:
//
//   1. cvc5::Solver::getStatistics() -- FREE. cvc5 accumulates its counters
//      unconditionally (verified: the registry is byte-identical with and without
//      cvc5's own `stats`/`stats-internal`/`stats-all` options -- those are printing
//      options of the cvc5 *binary*, not collection switches). This is the backbone
//      and supplies every scalar below.
//
//   2. cvc5::Plugin (notifySatClause / notifyTheoryLemma) -- EXPENSIVE, ~8x. It is
//      the only way to see the *content* of learned clauses (width distribution)
//      and the Kind of each theory lemma, which no statistic reports. Measured on a
//      16-bit semiprime miter under LiveHD's own bv-solver=bitblast-internal:
//      no plugin 2197ms, no-op plugin 16906ms, counting plugin 16887ms. The no-op
//      and counting numbers being equal is the point -- the tax is cvc5's internal
//      Node->Term export on each notification (~50k notifications here, one per
//      conflict), NOT the callback body, so no callback can be written fast enough.
//      cvc5's `plugin-notify-sat-clause-in-solve` does not mitigate it (16704ms).
//      Enabled together with the statistics tier by formal.stats; the report labels
//      the affected timings as instrumented so they are not read as solve time.
//
// Statistics accumulate over a cvc5::Solver's LIFETIME, so exactly one snapshot per
// Solver is that solver's total -- never snapshot per checkSat (a 500-entry map copy
// per check, and prove_properties runs thousands).

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Only solve_stats.cpp includes cvc5/cvc5.h; query.hpp pulls this header in, and
// cvc5.h is expensive. References to incomplete types are all we need.
namespace cvc5 {
class Solver;
class TermManager;
}  // namespace cvc5

namespace livehd::lec {

// Aggregated cvc5 effort. Pure value type: summable, serializable, no cvc5 types.
// Empty (solvers == 0) whenever stats are off OR no cvc5 query ever ran -- the
// latter is common and NOT a bug (semdiff, the verdict cache and the abc cone
// decomposition all settle defs without calling cvc5 at all), so the report has a
// dedicated "no cvc5 query ran" line rather than printing a wall of zeros.
struct Cvc5_stats {
  // ---- provenance ---------------------------------------------------------
  int32_t solvers = 0;  // cvc5::Solver instances that ran (>1 under hier / portfolio)
  int64_t checks  = 0;  // checkSat/checkSatAssuming calls; LiveHD counts these itself
                        // (cvc5 exposes no such counter under bitblast-internal)

  // ---- problem size -------------------------------------------------------
  int64_t atoms           = 0;  // prop::CnfStream::numAtoms
  int64_t input_atoms     = 0;  // prop::PropEngine::numInputAtoms
  int64_t clause_literals = 0;  // sat::clauses_literals  (LITERALS, not clauses --
                                // there is no clause count under bitblast-internal)

  // ---- search effort ------------------------------------------------------
  int64_t conflicts    = 0;  // sat::conflicts  == number of learned clauses
  int64_t decisions    = 0;  // sat::decisions
  int64_t propagations = 0;  // sat::propagations
  int64_t restarts     = 0;  // sat::starts

  // ---- learned-clause bulk ------------------------------------------------
  int64_t learnt_literals = 0;  // sat::learnts_literals (retained after reduction)
  int64_t tot_literals    = 0;  // sat::tot_literals  (kept after minimization)
  int64_t max_literals    = 0;  // sat::max_literals  (before minimization)

  // ---- theory -------------------------------------------------------------
  int64_t theory_lemmas    = 0;  // sum of theory::*::lemmas
  int64_t theory_conflicts = 0;  // sum of theory::*::conflicts

  // ---- cost ---------------------------------------------------------------
  int64_t resource_units  = 0;  // resource::resourceUnitsUsed (machine-independent)
  int64_t total_ms        = 0;  // global::totalTime           (cvc5 reports "2209ms")
  int64_t cnf_ms          = 0;  // prop::CnfStream::cnfConversionTime
  int64_t theory_check_ms = 0;  // sum of theory::*::checkTime

  // ---- histograms ---------------------------------------------------------
  std::map<std::string, uint64_t> lemma_kinds;     // theory::*::inferencesLemma folded
  std::map<std::string, uint64_t> resource_steps;  // resource::steps::resource

  // ---- plugin tier (cvc5::Plugin; only source for clause CONTENT) ---------
  // deep_active distinguishes "plugin registered but never notified" (which really
  // happens: under cvc5's default LAZY bitblast, used whenever the design has a
  // Memory or UF boxes, notifySatClause fires ZERO times) from "plugin off".
  bool                            deep_active  = false;
  int64_t                         deep_clauses = 0;  // notifySatClause callbacks
  std::map<uint64_t, uint64_t>    clause_widths;     // clause arity -> frequency
  std::map<std::string, uint64_t> deep_lemma_kinds;  // notifyTheoryLemma Term kind

  [[nodiscard]] bool empty() const { return solvers == 0; }

  // Sums scalars and merges every histogram BY KEY. Always merge, never assign:
  // several call sites already carry a forked child's deserialized stats, and an
  // assignment there silently erases them.
  Cvc5_stats& operator+=(const Cvc5_stats& o);

  // p50/p90/max over clause_widths (0 when the plugin saw nothing).
  struct Widths {
    uint64_t p50 = 0, p90 = 0, max = 0;
  };
  [[nodiscard]] Widths widths() const;
};

// Owns the optional cvc5::Plugin.
//
// MUST be declared BEFORE the cvc5::Solver it instruments: the plugin has to
// OUTLIVE the solver. Destroying the plugin first and then solving SIGSEGVs --
// undocumented in cvc5.h, verified empirically. Declaration order is therefore
// mandatory and load-bearing:
//
//     cvc5::TermManager tm;
//     livehd::lec::Solve_probe probe(tm, acc);   // <-- before the Solver
//     cvc5::Solver solver(tm);
//     probe.attach(solver);                      // <-- before any assertFormula/push
//     livehd::lec::Stats_guard guard(solver, probe, acc);  // <-- after the Solver
//
// A null `acc` makes every method a no-op, so callers need no `if (stats)`.
class Solve_probe {
public:
  Solve_probe(cvc5::TermManager& tm, Cvc5_stats* acc);
  ~Solve_probe();
  Solve_probe(const Solve_probe&)            = delete;
  Solve_probe& operator=(const Solve_probe&) = delete;

  // Registers the plugin. MUST run before the solver's first assertFormula() or
  // push(): cvc5 throws "Cannot add plugin after the solver has been fully
  // initialized" otherwise (also undocumented, also verified).
  void attach(cvc5::Solver& solver);

  // Folds the plugin-observed histograms into `acc`. Called by Stats_guard.
  void drain();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;  // null when acc == nullptr
};

// Snapshots Solver::getStatistics() at scope exit and folds it into `acc`.
//
// MUST be declared AFTER the cvc5::Solver so its destructor runs while the solver
// is still alive. A guard (rather than a capture before each `return`) because
// prove_equal has ~21 return points past its Solver construction -- a tail capture
// silently misses most of them, which is this file's recurring bug class.
class Stats_guard {
public:
  Stats_guard(const cvc5::Solver& solver, Solve_probe& probe, Cvc5_stats* acc)
      : solver_(solver), probe_(probe), acc_(acc), entry_checks_(acc != nullptr ? acc->checks : 0) {}
  ~Stats_guard();
  Stats_guard(const Stats_guard&)            = delete;
  Stats_guard& operator=(const Stats_guard&) = delete;

private:
  const cvc5::Solver& solver_;
  Solve_probe&        probe_;
  Cvc5_stats*         acc_;
  // `acc->checks` when this guard was armed. The destructor counts the Solver in
  // `acc->solvers` only if the counter GREW -- a solver that returned before any
  // checkSat must keep `empty()` (solvers == 0) true, or the report claims cvc5
  // settled every query by preprocessing when in fact it was never asked.
  int64_t entry_checks_;
};

// Reads every live counter out of `solver` and adds it to `acc`. Does NOT touch
// `acc->solvers` -- Stats_guard bumps that, and only for a solver that ran a query.
// No-op when acc is null. Iterates begin(internal=true, defaulted=false) and MATCHES
// keys -- never Statistics::get(name), which THROWS on a missing key, and the key set
// depends on the bv solver (the whole theory::bv::BVSolverBitblast::cadical::* family,
// including the only real clause count, is absent under bitblast-internal).
void capture_cvc5_stats(const cvc5::Solver& solver, Cvc5_stats* acc);

// Human report on stdout, pass.color/pass.abc house style: "<tag>[stats]: " prefix,
// a fixed sub-label column, and a closing "=> " takeaway line. `hot` is an optional
// per-def (name, conflicts) ranking, already sorted and truncated by the caller.
void report_cvc5_stats(std::string_view tag, const Cvc5_stats& st, const std::vector<std::pair<std::string, int64_t>>& hot = {});

// The same numbers as a JSON object (no trailing comma, no newline) for the
// formal_report.json "cvc5" member.
[[nodiscard]] std::string cvc5_stats_json(const Cvc5_stats& st);

}  // namespace livehd::lec
