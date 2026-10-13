// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "effort.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <climits>
#include <random>
#include <stdexcept>

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
  throw std::invalid_argument("formal.effort expects quick-check|test|long-check|verify");
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

uint64_t effort_proof_rlimit(Effort effort) {
  switch (effort) {
    case Effort::Quick: return 5'000'000;
    case Effort::Test : return 50'000'000;
    case Effort::Long : return 500'000'000;
    default           : return 0;
  }
}

uint64_t parse_effort_proof(std::string_view value, Effort effort) {
  if (value.empty()) {
    return effort_proof_rlimit(effort);
  }
  uint64_t limit = 0;
  const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), limit);
  if (error != std::errc{} || end != value.data() + value.size()) {
    throw std::invalid_argument("formal.effort_proof expects an unsigned cvc5 resource-unit count, got '" + std::string(value) + "'");
  }
  return limit;
}

void Input_sampler::add_input(const cvc5::Term& term, int cycle) {
  std::vector<cvc5::Term> stack{term};
  while (!stack.empty()) {
    auto t = stack.back();
    stack.pop_back();
    if (t.isNull()) {
      continue;
    }
    if (t.getKind() == cvc5::Kind::CONSTANT) {
      if (t.getSort().isBitVector() && std::find(inputs_.begin(), inputs_.end(), t) == inputs_.end()) {
        inputs_.push_back(t);
        input_cycles_.push_back(cycle);
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

Input_sampler::Outcome Input_sampler::run(cvc5::Solver& solver, const cvc5::Term& bad, Effort effort, const Check& check,
                                          const std::function<bool()>& expired) {
  Outcome out;
  auto&   report = out.report;
  report.requested = effort;
  const uint64_t target = effort_vectors(effort);
  // Pin-probability schedule (percent of input symbols fixed), cycled per
  // query. 100 = simulation of random traces; lower = the solver searches the
  // free symbols for ANY mismatching assignment, i.e. many traces at once.
  constexpr int kSchedule[] = {100, 90, 100, 75, 100, 50};
  // A fully pinned query costs about one propagation pass (~55 ms on dino
  // PipelinedDualIssueCPU, mostly per-checkSat overhead). Do NOT batch traces
  // as a disjunction of cubes: cvc5 then case-splits instead of propagating
  // (8 cubes: >4 s per query on dino).
  // Partial queries are bimodal: either propagation-cheap or a real search.
  // Give up on the expensive ones quickly; a stuck one says nothing.
  constexpr long long kFullCapMs    = 4000;
  constexpr long long kPartialCapMs = 250;
  // Consecutive capped queries before giving up (the design does not admit
  // cheap traces at this depth; whatever was counted still stands).
  constexpr uint64_t kUnknownStreak = 32;

  std::mt19937_64 rng(0x6c68645f74657374ULL);  // fixed seed: reproducible samples
  // One random cube over the inputs: pin_pct percent of the symbols pinned.
  auto cube = [&](int pin_pct, uint64_t& free_symbols) {
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
  auto                  state_any = [&]() {
    std::vector<cvc5::Term> terms;
    for (const auto& d : state_diffs_) {
      if (!excluded.contains({d.key, d.cycle})) {
        terms.push_back(d.diff);
      }
    }
    return terms.empty() ? cvc5::Term{} : terms.size() == 1 ? terms.front() : tm_.mkTerm(cvc5::Kind::OR, terms);
  };
  auto watch     = state_any();
  report.watched = state_diffs_.size();
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
    // The first query also pays for bit-blasting the whole unrolling: uncapped.
    const auto result = check ? check(q == 0 ? 0 : free_symbols ? kPartialCapMs : kFullCapMs) : solver.checkSat();
    ++report.queries;
    if (result.isSat() && solver.getValue(bad).getBooleanValue()) {
      report.solve_us += micros(start);
      out.mismatch = true;
      out.result   = result;  // push and model stay live for the witness
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
      prefix.push_back(bad);
      solver.push();
      solver.assertFormula(prefix.size() == 1 ? prefix.front() : tm_.mkTerm(cvc5::Kind::AND, prefix));
      const auto confirm = check ? check(kConfirmCapMs) : solver.checkSat();
      ++report.queries;
      if (confirm.isSat()) {
        report.solve_us += micros(start);
        out.mismatch = true;
        out.result   = confirm;
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

}  // namespace livehd::lec
