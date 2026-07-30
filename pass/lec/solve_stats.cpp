// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "solve_stats.hpp"

#include <cvc5/cvc5.h>

#include <algorithm>
#include <cstdlib>
#include <print>
#include <string>

namespace livehd::lec {

namespace {

// Every cvc5 TIMER statistic is a STRING ("2209ms"), never an int or a double --
// isInt() is false for all of them and a naive getInt() throws. Parse the leading
// integer and drop the unit.
int64_t ms_of(const cvc5::Stat& s) {
  if (s.isInt()) {  // defensive: a future cvc5 could switch a timer to int
    return s.getInt();
  }
  if (!s.isString()) {
    return 0;
  }
  const std::string& t = s.getString();
  return std::strtoll(t.c_str(), nullptr, 10);
}

int64_t int_of(const cvc5::Stat& s) {
  if (s.isInt()) {
    return s.getInt();
  }
  if (s.isDouble()) {
    return static_cast<int64_t>(s.getDouble());
  }
  return 0;
}

void merge_hist(std::map<std::string, uint64_t>& dst, const cvc5::Stat& s) {
  if (!s.isHistogram()) {
    return;
  }
  for (const auto& [k, v] : s.getHistogram()) {
    dst[k] += v;
  }
}

bool ends_with(std::string_view s, std::string_view suffix) {
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Thousands separator so a 16-million-propagation line stays readable.
std::string commas(int64_t v) {
  std::string s = std::to_string(v < 0 ? -v : v);
  for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) {
    s.insert(static_cast<size_t>(i), ",");
  }
  return v < 0 ? "-" + s : s;
}

// "a 12, b 3" for the top-N entries of a histogram, biggest first.
std::string top_hist(const std::map<std::string, uint64_t>& h, size_t n) {
  std::vector<std::pair<std::string, uint64_t>> v(h.begin(), h.end());
  std::ranges::sort(v, [](const auto& a, const auto& b) { return a.second > b.second; });
  std::string out;
  for (size_t i = 0; i < v.size() && i < n; ++i) {
    if (!out.empty()) {
      out += ", ";
    }
    out += std::format("{} {}", v[i].first, commas(static_cast<int64_t>(v[i].second)));
  }
  return out;
}

// cvc5 stat keys and Kind names are bare identifiers today, but they are cvc5's
// strings and not ours -- one quote or backslash would silently produce a
// formal_report.json that no longer parses, which is worse than a wrong number
// because it breaks every consumer of the file.
std::string json_escape(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (const char c : s) {
    switch (c) {
      case '"' : out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          out += std::format("\\u{:04x}", static_cast<unsigned char>(c));
        } else {
          out += c;
        }
    }
  }
  return out;
}

std::string json_hist(const std::map<std::string, uint64_t>& h) {
  std::string out   = "{";
  bool        first = true;
  for (const auto& [k, v] : h) {
    out   += std::format("{}\"{}\": {}", first ? "" : ", ", json_escape(k), v);
    first  = false;
  }
  return out + "}";
}

}  // namespace

Cvc5_stats& Cvc5_stats::operator+=(const Cvc5_stats& o) {
  solvers          += o.solvers;
  checks           += o.checks;
  atoms            += o.atoms;
  input_atoms      += o.input_atoms;
  clause_literals  += o.clause_literals;
  conflicts        += o.conflicts;
  decisions        += o.decisions;
  propagations     += o.propagations;
  restarts         += o.restarts;
  learnt_literals  += o.learnt_literals;
  tot_literals     += o.tot_literals;
  max_literals     += o.max_literals;
  theory_lemmas    += o.theory_lemmas;
  theory_conflicts += o.theory_conflicts;
  resource_units   += o.resource_units;
  total_ms         += o.total_ms;
  cnf_ms           += o.cnf_ms;
  theory_check_ms  += o.theory_check_ms;
  for (const auto& [k, v] : o.lemma_kinds) {
    lemma_kinds[k] += v;
  }
  for (const auto& [k, v] : o.resource_steps) {
    resource_steps[k] += v;
  }
  deep_active   = deep_active || o.deep_active;
  deep_clauses += o.deep_clauses;
  for (const auto& [k, v] : o.clause_widths) {
    clause_widths[k] += v;
  }
  for (const auto& [k, v] : o.deep_lemma_kinds) {
    deep_lemma_kinds[k] += v;
  }
  return *this;
}

Cvc5_stats::Widths Cvc5_stats::widths() const {
  Widths   w;
  uint64_t total = 0;
  for (const auto& [width, count] : clause_widths) {
    total += count;
    w.max  = std::max(w.max, width);
  }
  if (total == 0) {
    return w;
  }
  // clause_widths is a std::map, so iteration is ascending by width -- walk the
  // cumulative distribution once and take the first width crossing each quantile.
  uint64_t seen     = 0;
  bool     have_p50 = false, have_p90 = false;
  for (const auto& [width, count] : clause_widths) {
    seen += count;
    if (!have_p50 && seen * 100 >= total * 50) {
      w.p50    = width;
      have_p50 = true;
    }
    if (!have_p90 && seen * 100 >= total * 90) {
      w.p90    = width;
      have_p90 = true;
      break;
    }
  }
  return w;
}

// ---------------------------------------------------------------------------
// The plugin tier.
// ---------------------------------------------------------------------------

struct Solve_probe::Impl : cvc5::Plugin {
  Impl(cvc5::TermManager& tm, Cvc5_stats* acc) : cvc5::Plugin(tm), acc_(acc) {}

  std::string getName() override { return "lhd-stats"; }

  void notifySatClause(const cvc5::Term& clause) override {
    ++clauses_;
    // A learned clause arrives as an OR term; a unit clause is the literal itself.
    ++widths_[clause.getNumChildren() == 0 ? 1 : clause.getNumChildren()];
  }

  void notifyTheoryLemma(const cvc5::Term& lemma) override { ++kinds_[std::to_string(lemma.getKind())]; }

  Cvc5_stats*                     acc_;
  int64_t                         clauses_ = 0;
  std::map<uint64_t, uint64_t>    widths_;
  std::map<std::string, uint64_t> kinds_;
};

Solve_probe::Solve_probe(cvc5::TermManager& tm, Cvc5_stats* acc)
    : impl_(acc != nullptr ? std::make_unique<Impl>(tm, acc) : nullptr) {}

Solve_probe::~Solve_probe() = default;

void Solve_probe::attach(cvc5::Solver& solver) {
  if (impl_) {
    // Throws if the solver has already been given a formula -- see the header.
    solver.addPlugin(*impl_);
  }
}

void Solve_probe::drain() {
  if (!impl_ || impl_->acc_ == nullptr) {
    return;
  }
  Cvc5_stats& acc   = *impl_->acc_;
  acc.deep_active   = true;
  acc.deep_clauses += impl_->clauses_;
  for (const auto& [w, c] : impl_->widths_) {
    acc.clause_widths[w] += c;
  }
  for (const auto& [k, c] : impl_->kinds_) {
    acc.deep_lemma_kinds[k] += c;
  }
  impl_->clauses_ = 0;
  impl_->widths_.clear();
  impl_->kinds_.clear();
}

Stats_guard::~Stats_guard() {
  if (acc_ == nullptr) {
    return;
  }
  // A destructor is implicitly noexcept, and cvc5 throws CVC5ApiRecoverableException
  // from its API (getStatistics included). Letting one escape here calls
  // std::terminate -- and this guard sits on the unwind path of a solver that may be
  // failing for an unrelated reason, so an escaping exception would turn a clean
  // error into an abort. Reporting is strictly best-effort: losing a stats line is
  // always preferable to killing the run.
  try {
    capture_cvc5_stats(solver_, acc_);
    probe_.drain();
  } catch (...) {  // NOLINT(bugprone-empty-catch) -- see above: never fail a run for stats
  }
  // Count this Solver only if it was actually ASKED something. prove_equal_impl
  // has ~21 returns past the Solver construction and many of them (an encoder
  // refusal, a box-correspondence bail) never reach a checkSat -- bumping
  // unconditionally made `Cvc5_stats::empty()` (solvers == 0) stop detecting "no
  // cvc5 query ran", and report_cvc5_stats then printed a wall of zeros ending
  // in "every query closed by rewriting/preprocessing", which is simply false.
  // The check counter is LiveHD's own (cvc5 exposes none), bumped at each
  // checkSat site into this same acc, so a delta over the guard's lifetime is
  // exactly "this solver ran at least one query".
  if (acc_->checks > entry_checks_) {
    ++acc_->solvers;
  }
}

// ---------------------------------------------------------------------------
// The statistics tier.
// ---------------------------------------------------------------------------

void capture_cvc5_stats(const cvc5::Solver& solver, Cvc5_stats* acc) {
  if (acc == nullptr) {
    return;
  }
  cvc5::Statistics st = solver.getStatistics();

  // internal=true (the counters that matter are ALL internal; only global::totalTime
  // is public), defaulted=false (skip the ~450 untouched entries). NOTE cvc5.h's doc
  // comment on begin() is WRONG about the defaults -- always pass both explicitly.
  for (auto it = st.begin(/*internal=*/true, /*defaulted=*/false); it != st.end(); ++it) {
    const std::string& k = it->first;
    const cvc5::Stat&  v = it->second;

    if (k == "prop::CnfStream::numAtoms") {
      acc->atoms += int_of(v);
    } else if (k == "prop::PropEngine::numInputAtoms") {
      acc->input_atoms += int_of(v);
    } else if (k == "sat::clauses_literals") {
      acc->clause_literals += int_of(v);
    } else if (k == "sat::conflicts") {
      acc->conflicts += int_of(v);
    } else if (k == "sat::decisions") {
      acc->decisions += int_of(v);
    } else if (k == "sat::propagations") {
      acc->propagations += int_of(v);
    } else if (k == "sat::starts") {
      acc->restarts += int_of(v);
    } else if (k == "sat::learnts_literals") {
      acc->learnt_literals += int_of(v);
    } else if (k == "sat::tot_literals") {
      acc->tot_literals += int_of(v);
    } else if (k == "sat::max_literals") {
      acc->max_literals += int_of(v);
    } else if (k == "resource::resourceUnitsUsed") {
      acc->resource_units += int_of(v);
    } else if (k == "resource::steps::resource") {
      merge_hist(acc->resource_steps, v);
    } else if (k == "global::totalTime") {
      acc->total_ms += ms_of(v);
    } else if (k == "prop::CnfStream::cnfConversionTime") {
      acc->cnf_ms += ms_of(v);
    } else if (k.starts_with("Plugin::")) {
      // cvc5 auto-registers Plugin::<getName()>::{conflicts,lemmas,...} for a
      // registered plugin. Those describe what OUR plugin fed back to cvc5 (always
      // zero -- we only observe), so folding them into the theory totals would be
      // double counting. Skip.
      continue;
    } else if (k.starts_with("theory::")) {
      // Match by SHAPE, not by a hardcoded theory name: QF_ABV brings in arrays and
      // UF alongside bv, and the live key set depends on the bv solver.
      if (ends_with(k, "::checkTime")) {
        acc->theory_check_ms += ms_of(v);
      } else if (ends_with(k, "::lemmas")) {
        acc->theory_lemmas += int_of(v);
      } else if (ends_with(k, "::conflicts")) {
        acc->theory_conflicts += int_of(v);
      } else if (ends_with(k, "::inferencesLemma")) {
        merge_hist(acc->lemma_kinds, v);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Reporting.
// ---------------------------------------------------------------------------

void report_cvc5_stats(std::string_view tag, const Cvc5_stats& st, const std::vector<std::pair<std::string, int64_t>>& hot) {
  // `lhd lec` frequently settles every def WITHOUT calling cvc5 (semdiff, the
  // verdict cache, and the abc cone decomposition all do). Printing a wall of zeros
  // there reads as a bug, so say what actually happened.
  if (st.empty()) {
    std::print("{}[stats]: cvc5      0 solver(s), 0 check(s)\n", tag);
    std::print("{}[stats]: => no cvc5 query ran (every obligation settled by semdiff / cache / abc cones)\n", tag);
    return;
  }

  std::print("{}[stats]: cvc5      {} solver(s), {} check(s)\n", tag, st.solvers, commas(st.checks));
  // clause_literals/learnt_literals are the SAT solver's CURRENT clause-database
  // sizes, so a finished solver usually reports 0 for them -- printing "0 clause
  // literal(s)" next to a million-step solve reads as a bug. Show them only when
  // the snapshot actually caught a populated database.
  if (st.clause_literals > 0) {
    std::print("{}[stats]: problem   {} SAT atom(s), {} input atom(s), {} clause literal(s) in the SAT db\n",
               tag,
               commas(st.atoms),
               commas(st.input_atoms),
               commas(st.clause_literals));
  } else {
    std::print("{}[stats]: problem   {} SAT atom(s), {} input atom(s)\n", tag, commas(st.atoms), commas(st.input_atoms));
  }
  std::print("{}[stats]: search    {} conflict(s) = learned clause(s), {} decision(s), {} propagation(s), {} restart(s)\n",
             tag,
             commas(st.conflicts),
             commas(st.decisions),
             commas(st.propagations),
             commas(st.restarts));
  // max_literals/tot_literals are CUMULATIVE over the run (unlike the two above),
  // so this line is about how much clause minimization bought.
  if (st.max_literals > 0) {
    const double removed = 100.0 * static_cast<double>(st.max_literals - st.tot_literals) / static_cast<double>(st.max_literals);
    std::print("{}[stats]: learned   {} of {} literal(s) kept after minimization ({:.1f}% removed){}\n",
               tag,
               commas(st.tot_literals),
               commas(st.max_literals),
               removed,
               st.learnt_literals > 0 ? std::format("; {} retained in the SAT db", commas(st.learnt_literals)) : "");
  }
  {
    const std::string kinds = top_hist(st.lemma_kinds, 3);
    std::print("{}[stats]: theory    {} lemma(s), {} conflict(s){}{}\n",
               tag,
               commas(st.theory_lemmas),
               commas(st.theory_conflicts),
               kinds.empty() ? "" : " -- ",
               kinds);
  }
  {
    const std::string steps = top_hist(st.resource_steps, 6);
    std::print("{}[stats]: resource  {} unit(s){}{}\n", tag, commas(st.resource_units), steps.empty() ? "" : " -- ", steps);
  }

  // Two independent reasons this is NOT wall clock, both of which have to be said or
  // the number is read as "the run took this long":
  //   - it is SUMMED over solvers that the portfolio/hier paths run CONCURRENTLY in
  //     parallel forks, so N racers each taking 10s report 20s+;
  //   - with the plugin on, cvc5's own clock is inflated ~8x.
  std::print("{}[stats]: time      {} ms summed over {} solver(s), {} ms theory-check, {} ms cnf{}\n",
             tag,
             commas(st.total_ms),
             st.solvers,
             commas(st.theory_check_ms),
             commas(st.cnf_ms),
             st.deep_active ? " -- INSTRUMENTED (see cost), and concurrent solvers overlap: NOT wall clock"
                            : " -- concurrent solvers overlap: NOT wall clock");

  if (st.deep_active) {
    if (st.deep_clauses == 0) {
      // Real and expected: under cvc5's default LAZY bitblast (any design with a
      // Memory or UF boxes) notifySatClause never fires.
      int64_t seen = 0;
      for (const auto& [kind, n] : st.deep_lemma_kinds) {
        seen += static_cast<int64_t>(n);
      }
      const std::string kinds = top_hist(st.deep_lemma_kinds, 3);
      std::print(
          "{}[stats]: deep      plugin saw no learned clause (the lazy bv solver does not notify); "
          "{} theory lemma(s){}{}\n",
          tag,
          commas(seen),
          kinds.empty() ? "" : " -- ",
          kinds);
    } else {
      const auto        w     = st.widths();
      const std::string kinds = top_hist(st.deep_lemma_kinds, 3);
      std::print("{}[stats]: deep      {} learned clause(s): width p50 {}, p90 {}, max {}{}{}\n",
                 tag,
                 commas(st.deep_clauses),
                 w.p50,
                 w.p90,
                 w.max,
                 kinds.empty() ? "" : "; lemma kinds ",
                 kinds);
    }
    std::print("{}[stats]: cost      the cvc5 plugin makes the solve ~8x slower; drop --stats to time the real run\n", tag);
  }

  // Only defs that actually cost something: a "hot" list whose entries are all 0 is
  // noise, and on a run settled by abc cones every entry is 0.
  {
    std::string line;
    for (const auto& [name, conflicts] : hot) {
      if (conflicts <= 0) {
        continue;
      }
      if (!line.empty()) {
        line += ", ";
      }
      line += std::format("{} {}", name, commas(conflicts));
    }
    if (!line.empty()) {
      std::print("{}[stats]: hot       {} (conflict(s) per def)\n", tag, line);
    }
  }

  // The takeaway: name where the effort ACTUALLY went, because the fix differs.
  // Rewriting/preprocessing dominating means the miter is huge before search even
  // starts (shrink the problem); conflicts-per-atom dominating means the search
  // itself is deep (give it more budget, or split it).
  std::string dominant;
  uint64_t    dominant_units = 0;
  for (const auto& [step, units] : st.resource_steps) {
    if (units > dominant_units) {
      dominant_units = units;
      dominant       = step;
    }
  }
  const double dominant_pct
      = st.resource_units > 0 ? 100.0 * static_cast<double>(dominant_units) / static_cast<double>(st.resource_units) : 0.0;
  const double per_atom = st.atoms > 0 ? static_cast<double>(st.conflicts) / static_cast<double>(st.atoms) : 0.0;

  if (st.conflicts == 0) {
    std::print(
        "{}[stats]: => {} check(s), no conflict -- every query closed by rewriting/preprocessing, "
        "the SAT search never had to work\n",
        tag,
        commas(st.checks));
  } else if (dominant == "RewriteStep" && dominant_pct >= 50.0) {
    std::print(
        "{}[stats]: => rewrite-bound: {:.0f}% of {} resource unit(s) went to rewriting, not search "
        "({} conflict(s) over {} atom(s)) -- shrink the miter rather than raising formal.timeout\n",
        tag,
        dominant_pct,
        commas(st.resource_units),
        commas(st.conflicts),
        commas(st.atoms));
  } else if (per_atom >= 10.0) {
    std::print(
        "{}[stats]: => search-bound: {:.1f} conflict(s) per SAT atom over {} check(s) -- raise "
        "formal.timeout/formal.rlimit, or split the miter\n",
        tag,
        per_atom,
        commas(st.checks));
  } else {
    std::print(
        "{}[stats]: => encode-bound: {} atom(s) but only {:.2f} conflict(s) per atom over {} check(s) -- "
        "the instance is large, not deep ({} dominates at {:.0f}%)\n",
        tag,
        commas(st.atoms),
        per_atom,
        commas(st.checks),
        dominant.empty() ? "no step" : dominant,
        dominant_pct);
  }
}

std::string cvc5_stats_json(const Cvc5_stats& st) {
  std::string j  = "{";
  j             += std::format("\"solvers\": {}, \"checks\": {}, ", st.solvers, st.checks);
  j += std::format("\"atoms\": {}, \"input_atoms\": {}, \"clause_literals\": {}, ", st.atoms, st.input_atoms, st.clause_literals);
  j += std::format("\"conflicts\": {}, \"decisions\": {}, \"propagations\": {}, \"restarts\": {}, ",
                   st.conflicts,
                   st.decisions,
                   st.propagations,
                   st.restarts);
  j += std::format("\"learnt_literals\": {}, \"tot_literals\": {}, \"max_literals\": {}, ",
                   st.learnt_literals,
                   st.tot_literals,
                   st.max_literals);
  j += std::format("\"theory_lemmas\": {}, \"theory_conflicts\": {}, ", st.theory_lemmas, st.theory_conflicts);
  j += std::format("\"resource_units\": {}, \"total_ms\": {}, \"cnf_ms\": {}, \"theory_check_ms\": {}, ",
                   st.resource_units,
                   st.total_ms,
                   st.cnf_ms,
                   st.theory_check_ms);
  j += std::format("\"lemma_kinds\": {}, \"resource_steps\": {}", json_hist(st.lemma_kinds), json_hist(st.resource_steps));
  if (st.deep_active) {
    const auto w  = st.widths();
    j            += std::format(
        ", \"deep\": {{\"clauses\": {}, \"width_p50\": {}, \"width_p90\": {}, \"width_max\": {}, "
        "\"lemma_kinds\": {}}}",
        st.deep_clauses,
        w.p50,
        w.p90,
        w.max,
        json_hist(st.deep_lemma_kinds));
  }
  return j + "}";
}

}  // namespace livehd::lec
