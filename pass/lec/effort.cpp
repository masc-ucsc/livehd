// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "effort.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <memory>
#include <random>
#include <stdexcept>
#include <type_traits>

#include "absl/container/flat_hash_set.h"
#include "debug_env.hpp"
#include "fresh_solver.hpp"
#include "solver_profile.hpp"
#include "term_sim.hpp"

namespace livehd::lec {

Effort parse_effort(std::string_view name) {
  if (name.empty()) {
    return Effort::Legacy;
  }
  if (name == "quick-check") {
    return Effort::Quick;
  }
  if (name == "test") {
    return Effort::Test;
  }
  if (name == "long-check") {
    return Effort::Long;
  }
  if (name == "verify") {
    return Effort::Verify;
  }
  if (name == "proof") {
    return Effort::Legacy;  // the pre-preset proof-only defaults (timeout=120)
  }
  throw std::invalid_argument("formal.effort expects quick-check|test|long-check|verify|proof");
}

std::string_view effort_name(Effort effort) {
  switch (effort) {
    case Effort::Quick : return "quick-check";
    case Effort::Test  : return "test";
    case Effort::Long  : return "long-check";
    case Effort::Verify: return "verify";
    case Effort::Legacy: return "";
  }
  return "";
}

bool is_sampled(Effort effort) { return effort == Effort::Quick || effort == Effort::Test || effort == Effort::Long; }

uint64_t effort_vectors(Effort effort) {
  switch (effort) {
    case Effort::Quick: return 256;
    case Effort::Test : return 2048;
    case Effort::Long : return 16384;
    default           : return 0;
  }
}

Effort effort_reached(uint64_t vectors) {
  for (auto effort : {Effort::Long, Effort::Test, Effort::Quick}) {
    if (vectors >= effort_vectors(effort)) {
      return effort;
    }
  }
  return Effort::Legacy;
}

void Input_sampler::add_input(const cvc5::Term& term, int cycle, std::string_view port) {
  int port_id = -1;
  if (!port.empty()) {
    port_id = port_ids_.try_emplace(std::string(port), static_cast<int>(port_ids_.size())).first->second;
  }
  std::vector<cvc5::Term> stack{term};
  while (!stack.empty()) {
    auto t = stack.back();
    stack.pop_back();
    if (t.isNull()) {
      continue;
    }
    if (t.getKind() == cvc5::Kind::CONSTANT) {
      if (t.getSort().isBitVector() && input_ids_.insert(t.getId()).second) {
        inputs_.push_back(t);
        input_cycles_.push_back(cycle);
        input_ports_.push_back(port_id);
      }
      continue;
    }
    for (size_t i = 0; i < t.getNumChildren(); ++i) {
      stack.push_back(t[i]);
    }
  }
}

void Input_sampler::add_state_diff(const std::string& key, int cycle, const cvc5::Term& diff) {
  if (!diff.isNull()) {
    state_diffs_.push_back({key, cycle, diff});
  }
}

namespace {
using Clock = std::chrono::steady_clock;
uint64_t micros(Clock::time_point start) {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count());
}
}  // namespace

template <class Solver>
Input_sampler::Outcome Input_sampler::run_solver(Solver& solver, const cvc5::Term& bad, Effort effort, const Check& check,
                                                 const std::function<bool()>& expired) {
  Outcome out;
  auto&   report                     = out.report;
  report.requested                   = effort;
  const uint64_t      target         = effort_vectors(effort);
  // Pin-probability schedule (percent of input symbols fixed), cycled per
  // query. 100 = simulation of random traces; lower = the solver searches the
  // free symbols for ANY mismatching assignment, i.e. many traces at once.
  constexpr int       kSchedule[]    = {100, 90, 100, 75, 100, 50};
  // A fully pinned query costs about one propagation pass (~55 ms on dino
  // PipelinedDualIssueCPU, mostly per-checkSat overhead). Do NOT batch traces
  // as a disjunction of cubes: cvc5 then case-splits instead of propagating
  // (8 cubes: >4 s per query on dino).
  // Partial queries are bimodal: either propagation-cheap or a real search.
  // Give up on the expensive ones quickly; a stuck one says nothing.
  constexpr long long kFullCapMs     = 4000;
  constexpr long long kPartialCapMs  = 250;
  // Consecutive capped queries before giving up (the design does not admit
  // cheap traces at this depth; whatever was counted still stands).
  constexpr uint64_t  kUnknownStreak = 32;

  std::mt19937_64 rng(0x6c68645f74657374ULL);  // fixed seed: reproducible samples
  // One random cube over the inputs: pin_pct percent of the symbols pinned.
  auto            cube = [&](int pin_pct, uint64_t& free_symbols) {
    std::vector<cvc5::Term> pins;
    pins.reserve(inputs_.size());
    for (const auto& input : inputs_) {
      if (pin_pct < 100 && static_cast<int>(rng() % 100) >= pin_pct) {
        ++free_symbols;
        continue;
      }
      // Biased draws: all-zero / all-one words exercise enables and wide
      // compares that uniform bits almost never hit.
      const auto  width = input.getSort().getBitVectorSize();
      const auto  mode  = rng() % 8;
      std::string bits(width, '0');
      for (uint32_t i = 0; i < width; ++i) {
        bits[i] = mode == 0 ? '0' : mode == 1 ? '1' : ((rng() & 1) ? '1' : '0');
      }
      pins.push_back(tm_.mkTerm(cvc5::Kind::EQUAL, {input, tm_.mkBitVector(width, bits, 2)}));
    }
    return pins.empty() ? tm_.mkTrue() : pins.size() == 1 ? pins.front() : tm_.mkTerm(cvc5::Kind::AND, pins);
  };
  // Paired-register leads: OR over the registers not yet excluded.
  std::set<std::pair<std::string, int>> excluded;  // diverged there without reaching an output
  auto                                  state_any = [&]() {
    std::vector<cvc5::Term> terms;
    for (const auto& d : state_diffs_) {
      if (!excluded.contains({d.key, d.cycle})) {
        terms.push_back(d.diff);
      }
    }
    return terms.empty() ? cvc5::Term{} : terms.size() == 1 ? terms.front() : tm_.mkTerm(cvc5::Kind::OR, terms);
  };
  auto watch                        = state_any();
  report.watched                    = state_diffs_.size();
  constexpr long long kConfirmCapMs = 2000;

  uint64_t streak = 0;
  for (uint64_t q = 0; report.vectors < target; ++q) {
    if (expired && expired()) {
      out.reason = "hit formal.timeout";
      break;
    }
    const int  pin_pct      = kSchedule[q % std::size(kSchedule)];
    uint64_t   free_symbols = 0;
    const auto miter        = watch.isNull() ? bad : tm_.mkTerm(cvc5::Kind::OR, {bad, watch});
    const auto goal         = tm_.mkTerm(cvc5::Kind::AND, {miter, cube(pin_pct, free_symbols)});
    const auto start        = Clock::now();
    solver.push();
    solver.assertFormula(goal);
    // The first query pays for bit-blasting: only the total timeout caps it.
    const auto result = check ? check(q == 0 ? 0 : free_symbols ? kPartialCapMs : kFullCapMs) : solver.checkSat();
    ++report.queries;
    if (result.isSat() && solver.getValue(bad).getBooleanValue()) {
      report.solve_us += micros(start);
      out.mismatch     = true;
      out.result       = result;  // push and model stay live for the witness
      return out;
    }
    if (result.isSat()) {
      // A paired register diverged on a reachable trace whose outputs still
      // agree. Find the first diverging cycle and its registers, then pin
      // that trace's inputs up to that cycle and search the remaining cycles
      // for an OUTPUT mismatch.
      ++report.leads;
      int                      first = INT32_MAX;
      std::vector<std::string> keys;
      for (const auto& d : state_diffs_) {
        if (!excluded.contains({d.key, d.cycle}) && d.cycle <= first && solver.getValue(d.diff).getBooleanValue()) {
          if (d.cycle < first) {
            keys.clear();
          }
          first = d.cycle;
          keys.push_back(d.key);
        }
      }
      std::vector<cvc5::Term> prefix;
      for (size_t i = 0; i < inputs_.size(); ++i) {
        if (input_cycles_[i] <= first) {
          prefix.push_back(tm_.mkTerm(cvc5::Kind::EQUAL, {inputs_[i], solver.getValue(inputs_[i])}));
        }
      }
      solver.pop();
      if (expired && expired()) {
        report.solve_us += micros(start);
        out.reason       = "hit formal.timeout";
        break;
      }
      prefix.push_back(bad);
      solver.push();
      solver.assertFormula(prefix.size() == 1 ? prefix.front() : tm_.mkTerm(cvc5::Kind::AND, prefix));
      const auto confirm = check ? check(kConfirmCapMs) : solver.checkSat();
      ++report.queries;
      if (confirm.isSat()) {
        report.solve_us += micros(start);
        out.mismatch     = true;
        out.result       = confirm;
        return out;
      }
      solver.pop();
      report.solve_us += micros(start);
      // Not observable at an output within the bound from that trace (or the
      // confirm capped out): stop chasing these registers.
      for (const auto& k : keys) {
        excluded.insert({k, first});
      }
      watch = state_any();
      continue;
    }
    solver.pop();
    report.solve_us += micros(start);
    if (result.isUnsat()) {
      streak = 0;
      ++report.traces;
      // A partially pinned UNSAT covers every assignment of its free symbols;
      // count it as one trace per free symbol plus the pinned one (a floor,
      // not 2^bits).
      report.partial += free_symbols ? 1 : 0;
      report.vectors += cycles_ * (1 + free_symbols);
    } else {
      ++report.unknown;
      if (++streak >= kUnknownStreak) {
        out.reason = "sampling queries kept hitting their per-query cap";
        break;
      }
    }
  }
  report.reached = effort_reached(report.vectors);
  if (out.reason.empty() && !report.passed()) {
    out.reason = "too few input vectors checked";
  }
  return out;
}

namespace {
std::vector<cvc5::Term> base_assertions(cvc5::Solver& solver) { return solver.getAssertions(); }
std::vector<cvc5::Term> base_assertions(Fresh_solver& solver) { return solver.assertions(); }
}  // namespace

// The simulated sampler (see effort.hpp). Same counting and lead policy as
// run_solver, but every step first SIMULATES one fully pinned trace (one
// Term_sim evaluation), and the search steps solve only a reduced cone on a
// private solver:
//   * with per-cycle output compares (add_output_diff), a search frees the
//     inputs of a short WINDOW of cycles and asks for an output or paired-
//     register divergence within a HORIZON after it -- a few cycles of logic,
//     not the whole unrolling, so its cost stays near the per-query cap;
//   * without them, it frees a random subset of all inputs (run_solver's
//     partial pinning) over the constant-folded miter.
// The caller's solver is used for exactly one thing: turning a mismatch --
// every free symbol pinned -- into the live model its witness path reads.
// That keeps the cost of sampling independent of how expensive a whole-
// unrolling solve is (a fresh non-incremental solver per query re-bit-blasted
// all 26 cycles of dino every time: ~19 s per step).
template <class Solver>
Input_sampler::Outcome Input_sampler::run(Solver& solver, const cvc5::Term& bad, Effort effort, const Check& check,
                                          const std::function<bool()>& expired) {
  const auto              build_start = Clock::now();
  Term_sim                sim(tm_);
  std::vector<cvc5::Term> roots{bad};
  roots.reserve(1 + state_diffs_.size() + out_diffs_.size());
  for (const auto& d : state_diffs_) {
    roots.push_back(d.diff);
  }
  for (const auto& d : out_diffs_) {
    roots.push_back(d.diff);
  }
  std::string why;
  if (!sim.build(base_assertions(solver), roots, why)) {
    auto out = run_solver(solver, bad, effort, check, expired);
    out.note = "solver-only sampling: " + why;
    return out;
  }
  const uint64_t build_us   = micros(build_start);
  const size_t   state_root = 1;                        // roots [1, 1+S): paired-register diffs
  const size_t   out_root   = 1 + state_diffs_.size();  // roots [1+S, 1+S+O): per-cycle output diffs
  const bool     windowed   = !out_diffs_.empty();

  Outcome out;
  auto&   report                      = out.report;
  report.requested                    = effort;
  report.watched                      = state_diffs_.size();
  const uint64_t      target          = effort_vectors(effort);
  // Partial pin rates of the unwindowed search, cycled per search.
  constexpr int       kSchedule[]     = {90, 75, 50};
  constexpr long long kSearchCapMs    = 250;
  constexpr long long kConfirmCapMs   = 2000;
  constexpr uint64_t  kUnknownStreak  = 32;        // capped searches in a row: stop searching, keep simulating
  constexpr uint64_t  kRejectStreak   = 256;       // steps in a row no assumption admits: stop
  constexpr size_t    kMaxSearchTerms = 25000;     // a reduced cone this large is not a cheap search: narrow or skip it
  std::mt19937_64     rng(0x6c68645f74657374ULL);  // fixed seed: reproducible samples
  const bool          trace_steps = livehd::debug::env<"LHD_LEC_SAMPLE_TRACE">() != nullptr;  // debug: one stderr line per search

  const auto&      leaves = sim.leaves();
  std::vector<int> leaf_cycle(leaves.size(), -1);  // an input leaf's cycle; -1 for every other free symbol
  std::vector<int> input_leaf(inputs_.size(), -1);
  int              last_cycle = 0;
  for (size_t k = 0; k < inputs_.size(); ++k) {
    input_leaf[k] = sim.leaf_index(inputs_[k]);
    if (input_leaf[k] >= 0) {
      leaf_cycle[static_cast<size_t>(input_leaf[k])] = input_cycles_[k];
    }
    last_cycle = std::max(last_cycle, input_cycles_[k]);
  }
  int first_checked = INT32_MAX;
  for (const auto& d : out_diffs_) {
    first_checked = std::min(first_checked, d.cycle);
    last_cycle    = std::max(last_cycle, d.cycle);
  }
  // Decoded input fields (Term_sim::fields): an opcode the design compares
  // against is drawn from those constants instead of uniformly, so a random
  // instruction word is a real instruction (dino's ADDIW: 1/1024 per slot
  // uniformly, ~1/80 from the dictionary).
  std::vector<int> leaf_port(leaves.size(), -1);
  for (size_t k = 0; k < inputs_.size(); ++k) {
    if (input_leaf[k] >= 0) {
      leaf_port[static_cast<size_t>(input_leaf[k])] = input_ports_[k] >= 0 ? input_ports_[k] : (1 << 20) + static_cast<int>(k);
    }
  }
  const auto                                         fields = sim.fields(leaf_port);
  std::map<int, std::vector<const Term_sim::Field*>> port_fields;
  for (const auto& f : fields) {
    port_fields[f.port].push_back(&f);
    if (trace_steps) {
      std::string vs;
      for (const auto v : f.values) {
        vs += std::format(" {:x}", v);
      }
      std::fprintf(stderr, "[LEC_SAMPLE] field port %d bits [%u,+%u):%s\n", f.port, f.lo, f.width, vs.c_str());
    }
  }
  // Every free symbol gets a value: inputs (biased draws, as run_solver's
  // cube, then decoded fields), power-on state, X values, and a content seed
  // per free memory. Three traces in four draw the decoded fields (each with
  // probability 7/8): measured on dino's ADDIW mutation, one in ~25 such
  // traces exposes it, against one in ~250 at half that rate and none in
  // 1024 uniform traces; the fourth stays uniform for whatever the
  // dictionary misses.
  auto randomize = [&]() {
    const bool decoded_trace = !port_fields.empty() && rng() % 4 != 0;
    for (size_t i = 0; i < leaves.size(); ++i) {
      if (leaves[i].is_array) {
        sim.set_array_seed(i, rng());
        continue;
      }
      const auto mode = rng() % 8;
      uint64_t*  w    = sim.leaf_words(i);
      for (uint32_t k = 0; k < sim.leaf_nwords(i); ++k) {
        w[k] = mode == 0 ? 0 : mode == 1 ? ~uint64_t{0} : rng();
      }
      const auto pf = port_fields.find(leaf_port[i]);
      if (pf == port_fields.end() || !decoded_trace) {
        continue;
      }
      for (const auto* f : pf->second) {
        if (rng() % 8 == 0) {
          continue;
        }
        const uint64_t v = f->values[rng() % f->values.size()];
        for (uint32_t b = 0; b < f->width; ++b) {
          const uint32_t pos = f->lo + b;
          if (pos >= leaves[i].width) {
            break;
          }
          const uint64_t m = uint64_t{1} << (pos & 63);
          w[pos >> 6]      = ((v >> b) & 1) != 0 ? (w[pos >> 6] | m) : (w[pos >> 6] & ~m);
        }
      }
    }
  };
  auto left_ms = [&]() -> long long {
    if (deadline_ == Clock::time_point{}) {
      return LLONG_MAX;
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(deadline_ - Clock::now()).count();
  };
  auto timed_out = [&]() { return (expired && expired()) || left_ms() <= 0; };
  // One search on a private solver over the reduced formula only.
  auto solve     = [&](const Term_sim::Reduced&       red,
                   const cvc5::Term&              goal,
                   long long                      cap_ms,
                   std::unique_ptr<cvc5::Solver>& s) -> cvc5::Result {
    const auto t0 = Clock::now();
    s             = std::make_unique<cvc5::Solver>(tm_);
    s->setLogic(red.has_arrays ? "QF_ABV" : "QF_BV");
    Solver_query_ctx ctx;
    ctx.has_mem    = red.has_arrays;
    ctx.witness    = true;
    ctx.timeout_ms = std::max<long long>(1, std::min(cap_ms, left_ms()));
    configure_solver(*s, solver_profile("internal"), ctx);
    for (const auto& a : red.assertions) {
      s->assertFormula(a);
    }
    if (!goal.isNull()) {
      s->assertFormula(goal);
    }
    const auto r = s->checkSat();
    ++report.queries;
    report.solve_us += micros(t0);
    if (trace_steps) {
      std::fprintf(stderr,
                   "[LEC_SAMPLE] search: %zu rebuilt term(s), %zu assertion(s), cap %lldms -> %s in %llums\n",
                   red.terms,
                   red.assertions.size(),
                   cap_ms,
                   r.isSat()     ? "sat"
                       : r.isUnsat() ? "unsat"
                                     : "unknown",
                   static_cast<unsigned long long>(micros(t0) / 1000));
    }
    return r;
  };
  // The caller's solver re-derives a mismatch with every free symbol pinned;
  // on SAT the push stays live for the witness. On the caller's fresh
  // (non-incremental) solver a full re-check re-preprocesses the whole
  // unrolling (dino: 13.6 s, 9M rewrite steps, no search); instead cvc5
  // CONFIRMS the simulated trace -- every base assertion and `bad`, with the
  // trace substituted, must rewrite to true -- and the witness model comes
  // from the pins alone (Fresh_solver::check_scope).
  const auto base     = base_assertions(solver);
  auto       confirms = [&]() {
    if constexpr (!std::is_same_v<Solver, Fresh_solver>) {
      return false;
    } else {
      const auto              sub = sim.substitution();
      std::vector<cvc5::Term> all(base);
      all.push_back(bad);
      cvc5::Solver simp(tm_);
      auto         t = simp.simplify(tm_.mkTerm(cvc5::Kind::AND, all).substitute(sub.from, sub.to));
      if (!t.isBooleanValue() && !sub.read_from.empty()) {
        t = simp.simplify(t.substitute(sub.read_from, sub.read_to));
      }
      return t.isBooleanValue() && t.getBooleanValue();
    }
  };
  uint64_t replay_failed = 0;
  auto     replay        = [&](std::vector<cvc5::Term> pins) {
    pins.push_back(bad);
    const auto t0         = Clock::now();
    bool       scope_only = false;
    if constexpr (std::is_same_v<Solver, Fresh_solver>) {
      if (confirms()) {
        const auto                    sub = sim.substitution();
        absl::flat_hash_set<uint64_t> pinned;
        for (size_t i = 0; i < sub.from.size(); ++i) {
          pinned.insert(sub.from[i].getId());
          if (sub.from[i].getSort().isArray()) {
            pins.push_back(tm_.mkTerm(cvc5::Kind::EQUAL, {sub.from[i], sub.to[i]}));  // defined memories: inlined
          }
        }
        // Definitions outside the cone of `bad` (state no output reads) keep
        // the witness's other values consistent with the design.
        for (const auto& a : base) {
          if (a.getKind() == cvc5::Kind::EQUAL && a[0].getKind() == cvc5::Kind::CONSTANT && !pinned.contains(a[0].getId())) {
            pins.push_back(a);
          }
        }
        scope_only = true;
      }
    }
    solver.push();
    solver.assertFormula(pins.size() == 1 ? pins.front() : tm_.mkTerm(cvc5::Kind::AND, pins));
    cvc5::Result r;
    if constexpr (std::is_same_v<Solver, Fresh_solver>) {
      r = scope_only ? solver.check_scope() : (check ? check(0) : solver.checkSat());
    } else {
      r = check ? check(0) : solver.checkSat();
    }
    ++report.queries;
    report.solve_us += micros(t0);
    if (trace_steps) {
      std::fprintf(stderr,
                   "[LEC_SAMPLE] replay of %zu pin(s)%s: %s in %llums\n",
                   pins.size(),
                   scope_only ? " (confirmed by substitution)" : "",
                   r.isSat()     ? "sat"
                              : r.isUnsat() ? "unsat"
                                            : "unknown",
                   static_cast<unsigned long long>(micros(t0) / 1000));
    }
    if (r.isSat()) {
      out.mismatch = true;
      out.result   = r;
      return true;
    }
    solver.pop();
    ++replay_failed;
    return false;
  };
  // A search's model as pins: its symbolic leaves take the model value (also
  // written back into the simulator). Memories keep their simulated content
  // when that reproduces the mismatch; otherwise (a symbolic read chose an
  // entry the simulation never fixed) they take the model's content.
  auto model_pins = [&](cvc5::Solver& s, const std::vector<bool>& symbolic) {
    for (size_t i = 0; i < leaves.size(); ++i) {
      if (symbolic[i] && !leaves[i].is_array) {
        sim.set_leaf_value(i, s.getValue(leaves[i].term));
      }
    }
    sim.eval(true);
    if (sim.root(0) || !sim.has_arrays()) {
      return sim.pins();
    }
    auto pins = sim.pins(false);
    for (const auto& l : leaves) {
      if (l.is_array) {
        pins.push_back(tm_.mkTerm(cvc5::Kind::EQUAL, {l.term, s.getValue(l.term)}));
      }
    }
    return pins;
  };

  std::set<std::pair<std::string, int>> excluded;  // diverged there without reaching an output
  auto live_diff    = [&](size_t k) { return !excluded.contains({state_diffs_[k].key, state_diffs_[k].cycle}); };
  // Roots a search asks for: every output compare and live paired-register
  // diff in cycles [lo, hi] (the whole `bad` when unwindowed).
  auto search_roots = [&](int lo, int hi) {
    std::vector<size_t> ids;
    if (!windowed) {
      ids.push_back(0);
    }
    for (size_t k = 0; k < out_diffs_.size(); ++k) {
      if (out_diffs_[k].cycle >= lo && out_diffs_[k].cycle <= hi) {
        ids.push_back(out_root + k);
      }
    }
    for (size_t k = 0; k < state_diffs_.size(); ++k) {
      if (live_diff(k) && state_diffs_[k].cycle >= lo && state_diffs_[k].cycle <= hi) {
        ids.push_back(state_root + k);
      }
    }
    return ids;
  };
  int       horizon      = std::max(1, horizon_);  // halves while a window's cone is too large for a cheap search
  const int lead_horizon = std::max(2, horizon_);  // a lead's confirmation looks this far (it has a larger cap)
  // A paired register diverged on an admitted trace whose outputs agree: pin
  // that trace up to the divergence and search the following cycles for an
  // OUTPUT mismatch. 1 = mismatch (out is set), 2 = timeout, 0 = not confirmed.
  auto      chase_lead   = [&](int first, const std::vector<std::string>& keys) {
    ++report.leads;
    if (trace_steps) {
      std::fprintf(stderr, "[LEC_SAMPLE] lead at cycle %d on %s (+%zu)\n", first, keys.front().c_str(), keys.size() - 1);
    }
    if (timed_out()) {
      return 2;
    }
    std::vector<bool> symbolic(leaves.size(), false);
    for (size_t i = 0; i < leaves.size(); ++i) {
      symbolic[i] = leaf_cycle[i] > first && (!windowed || leaf_cycle[i] <= first + lead_horizon);
    }
    std::vector<size_t> ids;
    if (windowed) {
      for (size_t k = 0; k < out_diffs_.size(); ++k) {
        if (out_diffs_[k].cycle > first && out_diffs_[k].cycle <= first + 2 * lead_horizon) {
          ids.push_back(out_root + k);
        }
      }
    } else {
      ids.push_back(0);
    }
    sim.eval(true);
    if (!ids.empty()) {
      const auto              red = sim.reduce(symbolic, ids);
      std::vector<cvc5::Term> goals;
      for (const auto& t : red.roots) {
        if (!(t.isBooleanValue() && !t.getBooleanValue())) {
          goals.push_back(t);
        }
      }
      if (!red.vacuous && !goals.empty() && red.terms <= kMaxSearchTerms) {
        std::unique_ptr<cvc5::Solver> s;
        if (solve(red, goals.size() == 1 ? goals.front() : tm_.mkTerm(cvc5::Kind::OR, goals), kConfirmCapMs, s).isSat()) {
          auto pins = model_pins(*s, symbolic);
          s.reset();
          if (replay(std::move(pins))) {
            return 1;
          }
        }
      }
    }
    for (const auto& k : keys) {
      excluded.insert({k, first});
    }
    return 0;
  };
  // Earliest live paired-register divergence under the current evaluation
  // (`truth(root id)` reads either the simulator or a search model).
  auto first_lead = [&](const std::vector<size_t>& ids, const std::function<bool(size_t)>& truth, std::vector<std::string>& keys) {
    int first = INT32_MAX;
    keys.clear();
    for (const auto id : ids) {
      if (id < state_root || id >= out_root) {
        continue;
      }
      const auto& d = state_diffs_[id - state_root];
      if (d.cycle <= first && live_diff(id - state_root) && truth(id)) {
        if (d.cycle < first) {
          keys.clear();
        }
        first = d.cycle;
        keys.push_back(d.key);
      }
    }
    return first;
  };
  std::vector<size_t> all_state_ids;
  for (size_t k = 0; k < state_diffs_.size(); ++k) {
    all_state_ids.push_back(state_root + k);
  }

  uint64_t           sims = 0, rejected = 0, searches = 0, skipped = 0, unknown_streak = 0, reject_streak = 0;
  bool               searching      = true;
  // Searches share the run with simulation: they may spend as much time as
  // building and simulating did (plus a small floor), so a design whose
  // search queries are expensive still finishes its vector count at
  // simulation speed instead of at one capped solve per step.
  constexpr uint64_t kSearchFloorUs = 500000;
  uint64_t           search_us      = 0;
  const auto         run_start      = build_start;
  for (uint64_t q = 0; report.vectors < target; ++q) {
    if (timed_out()) {
      out.reason = "hit formal.timeout";
      break;
    }
    // 1) Simulate one fully pinned trace.
    randomize();
    ++sims;
    if (!sim.eval()) {
      ++rejected;
      if (++reject_streak >= kRejectStreak) {
        out.reason = "no sampled input vector satisfies the design's assumptions";
        break;
      }
      continue;
    }
    reject_streak = 0;
    if (sim.root(0)) {
      sim.eval(true);  // record the free-memory reads the witness pins need
      if (replay(sim.pins())) {
        break;
      }
      continue;
    }
    std::vector<std::string> keys;
    const int                first = first_lead(all_state_ids, [&](size_t id) { return sim.root(id); }, keys);
    if (first != INT32_MAX) {
      const int rc = chase_lead(first, keys);
      if (rc == 1) {
        break;
      }
      if (rc == 2) {
        out.reason = "hit formal.timeout";
        break;
      }
      continue;  // a trace with an unconfirmed lead is not counted
    }
    ++report.traces;
    report.vectors += cycles_;

    // 2) Every other step, a search around the same trace.
    if (!searching || q % 2 == 0 || report.vectors >= target
        || search_us > std::max(kSearchFloorUs, micros(run_start) - search_us)) {
      continue;
    }
    const auto search_start = Clock::now();
    struct Search_clock {
      uint64_t&         acc;
      Clock::time_point t0;
      ~Search_clock() { acc += micros(t0); }
    } search_clock{search_us, search_start};
    if (timed_out()) {
      out.reason = "hit formal.timeout";
      break;
    }
    ++searches;
    std::vector<bool> symbolic(leaves.size(), false);
    uint64_t          free_symbols = 0;
    uint64_t          span         = cycles_;  // cycles a search UNSAT covers per free symbol
    int               lo = 0, hi = last_cycle;
    if (windowed) {
      // A window of one or two cycles inside the checked range, and the
      // horizon after it.
      const int width = 1 + static_cast<int>(rng() % 2);
      const int start
          = first_checked + static_cast<int>(rng() % static_cast<uint64_t>(std::max(1, last_cycle - first_checked + 1)));
      lo   = start;
      hi   = std::min(last_cycle, start + width - 1 + horizon);
      span = static_cast<uint64_t>(hi - lo + 1);
      for (size_t i = 0; i < leaves.size(); ++i) {
        if (leaf_cycle[i] >= start && leaf_cycle[i] < start + width) {
          symbolic[i] = true;
          ++free_symbols;
        }
      }
    } else {
      const int pin_pct = kSchedule[searches % std::size(kSchedule)];
      for (size_t k = 0; k < inputs_.size(); ++k) {
        if (static_cast<int>(rng() % 100) >= pin_pct) {
          ++free_symbols;
          if (input_leaf[k] >= 0) {
            symbolic[static_cast<size_t>(input_leaf[k])] = true;
          }
        }
      }
    }
    const auto ids    = search_roots(lo, hi);
    const auto red_t0 = Clock::now();
    sim.eval(true);  // the concrete memory reads reduce() pins
    const auto red = sim.reduce(symbolic, ids);
    if (trace_steps) {
      std::fprintf(stderr,
                   "[LEC_SAMPLE] search %llu: cycles [%d,%d], %llu free symbol(s), %zu root(s), reduce %llums\n",
                   static_cast<unsigned long long>(searches),
                   lo,
                   hi,
                   static_cast<unsigned long long>(free_symbols),
                   ids.size(),
                   static_cast<unsigned long long>(micros(red_t0) / 1000));
    }
    if (red.vacuous || free_symbols == 0) {
      continue;
    }
    std::vector<cvc5::Term> goals;
    for (const auto& t : red.roots) {
      if (!(t.isBooleanValue() && !t.getBooleanValue())) {
        goals.push_back(t);
      }
    }
    if (red.terms > kMaxSearchTerms) {
      ++skipped;
      if (windowed && horizon > 1) {
        horizon = std::max(1, horizon / 2);
      } else if (skipped >= 4) {
        searching = false;  // even the narrowest window is too large here
      }
      continue;
    }
    std::unique_ptr<cvc5::Solver> s;
    cvc5::Result                  r;
    const bool                    folded = goals.empty();  // no admitted assignment can mismatch
    if (!folded) {
      r = solve(red, goals.size() == 1 ? goals.front() : tm_.mkTerm(cvc5::Kind::OR, goals), kSearchCapMs, s);
    }
    if (folded || r.isUnsat()) {
      if (red.constrained) {
        // Never count a trace set the assumptions exclude: they must admit one.
        std::unique_ptr<cvc5::Solver> c;
        if (!solve(red, cvc5::Term{}, kSearchCapMs, c).isSat()) {
          continue;
        }
      }
      unknown_streak = 0;
      ++report.partial;
      report.vectors += span * free_symbols;
      continue;
    }
    if (r.isSat()) {
      unknown_streak = 0;
      std::vector<size_t> hold;  // the roots the model makes true
      for (size_t j = 0; j < ids.size(); ++j) {
        if (s->getValue(red.roots[j]).getBooleanValue()) {
          hold.push_back(ids[j]);
        }
      }
      const bool output_mismatch = std::any_of(hold.begin(), hold.end(), [&](size_t id) { return id == 0 || id >= out_root; });
      if (output_mismatch) {
        auto pins = model_pins(*s, symbolic);
        s.reset();
        if (replay(std::move(pins))) {
          break;
        }
        continue;
      }
      for (size_t i = 0; i < leaves.size(); ++i) {
        if (symbolic[i] && !leaves[i].is_array) {
          sim.set_leaf_value(i, s->getValue(leaves[i].term));
        }
      }
      s.reset();
      const int lead = first_lead(hold, [](size_t) { return true; }, keys);
      if (lead == INT32_MAX) {
        continue;
      }
      const int rc = chase_lead(lead, keys);
      if (rc == 1) {
        break;
      }
      if (rc == 2) {
        out.reason = "hit formal.timeout";
        break;
      }
      continue;
    }
    ++report.unknown;
    if (++unknown_streak >= kUnknownStreak) {
      searching = false;  // this design admits no cheap search; keep simulating
    }
  }
  report.reached = effort_reached(report.vectors);
  if (!out.mismatch && out.reason.empty() && !report.passed()) {
    out.reason = "too few input vectors checked";
  }
  out.note = std::format(
      "{} trace(s) simulated over {} term node(s) ({} free symbol(s), {} decoded input field(s), built in {}ms){}, {} "
      "{}search(es){}{}{}",
      sims,
      sim.nodes(),
      leaves.size(),
      fields.size(),
      build_us / 1000,
      rejected ? std::format(", {} trace(s) the assumptions rejected", rejected) : std::string{},
      searches,
      windowed ? std::format("{}-cycle-horizon ", horizon) : std::string{},
      skipped ? std::format(" ({} skipped as too large)", skipped) : std::string{},
      searching ? "" : " (searching stopped: per-query cap)",
      replay_failed ? std::format(", {} mismatch replay(s) the solver did not confirm", replay_failed) : std::string{});
  return out;
}

template Input_sampler::Outcome Input_sampler::run(cvc5::Solver&, const cvc5::Term&, Effort, const Check&,
                                                   const std::function<bool()>&);
template Input_sampler::Outcome Input_sampler::run(Fresh_solver&, const cvc5::Term&, Effort, const Check&,
                                                   const std::function<bool()>&);

}  // namespace livehd::lec
