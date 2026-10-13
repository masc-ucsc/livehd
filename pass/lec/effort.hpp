// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "cvc5/cvc5.h"

namespace livehd::lec {

// Effort presets for designs a full proof cannot finish. Each preset is a
// WORK count, not a wall-clock budget: how many input vectors were checked
// against the equivalence miter without a mismatch. formal.timeout is only
// the total backstop. `verify` is the uncapped proof.
enum class Effort { Legacy, Quick, Test, Long, Verify };
Effort           parse_effort(std::string_view name);
std::string_view effort_name(Effort effort);
bool             is_sampled(Effort effort);  // quick-check | test | long-check
// Input vectors (one per cycle of a sampled trace) a preset must check.
uint64_t         effort_vectors(Effort effort);
// The highest sampled preset whose vector count `vectors` satisfies (Legacy
// when not even quick-check's).
Effort           effort_reached(uint64_t vectors);
// Per-solver cvc5 resource limit of the proof attempt that precedes sampling
// (formal.effort_proof overrides; 0 = sample only). Deterministic, unlike
// seconds: the same design spends the same proof effort on any machine.
uint64_t         effort_proof_rlimit(Effort effort);
uint64_t         parse_effort_proof(std::string_view value, Effort effort);

// Kept separate from Verdict: a sampled test must never discharge a proof
// obligation, enter the theorem cache, or become a hierarchical premise.
struct Sample_report {
  Effort   requested = Effort::Legacy;
  Effort   reached   = Effort::Legacy;
  uint64_t vectors   = 0;  // per-cycle input vectors checked with no mismatch
  uint64_t traces    = 0;  // sampled traces with no mismatch
  uint64_t partial   = 0;  // of those, traces where the solver searched free inputs
  uint64_t queries   = 0;
  uint64_t unknown   = 0;  // queries that hit the per-query cap (not counted)
  uint64_t watched   = 0;  // name-paired register x cycle points watched for leads
  uint64_t leads     = 0;  // paired-register divergences followed up toward an output
  uint64_t solve_us  = 0;
  bool     ran() const { return requested != Effort::Legacy; }
  bool     passed() const { return reached != Effort::Legacy; }
};

// Random-restricted BMC. Each query pins a pseudo-random subset of the free
// primary-input symbols (every cycle of the unrolling) to random constants
// and asserts the miter's `bad`:
//   UNSAT   the trace(s) admitted by those pins never mismatch -> counted;
//   SAT     a mismatch: the push is left live so the caller extracts the
//           ordinary witness from the same model;
//   unknown the per-query cap hit -> not counted.
// Fully pinned queries are pure propagation (milliseconds, no model read);
// partially pinned ones let the solver search the free inputs, which is what
// finds a bug a random vector essentially never hits (one opcode among 2^32).
class Input_sampler {
public:
  explicit Input_sampler(cvc5::TermManager& tm) : tm_(tm) {}
  // A free primary-input term of `cycle`; its free BV symbols are collected
  // (a minted input may be a free core under a zero/sign extension).
  void add_input(const cvc5::Term& term, int cycle = 0);
  // DISTINCT(ref, impl) of a name-paired register's next state at `cycle`.
  // A reachable divergence is a LEAD, never a verdict: the sampler pins that
  // trace's inputs up to the divergence and asks for an OUTPUT mismatch in the
  // remaining cycles; only that is reported. It finds bugs whose effect needs
  // several cycles to reach an output (dino's ADDW-only ALU mutation) far
  // sooner than an output-only search.
  void add_state_diff(const std::string& key, int cycle, const cvc5::Term& diff);
  void set_cycles(uint64_t cycles) { cycles_ = cycles; }
  size_t input_count() const { return inputs_.size(); }

  struct Outcome {
    Sample_report report;
    std::string   reason;
    cvc5::Result  result;  // SAT only for a mismatch; its model stays live
    bool          mismatch = false;
  };
  // check runs one whole checkSat with an extra per-query wall cap in ms
  // (0 = none) under the caller's accounting; expired reports the caller's
  // TOTAL formal.timeout as spent.
  using Check = std::function<cvc5::Result(long long cap_ms)>;
  Outcome run(cvc5::Solver& solver, const cvc5::Term& bad, Effort effort, const Check& check = {},
              const std::function<bool()>& expired = {});

private:
  struct State_diff {
    std::string key;
    int         cycle;
    cvc5::Term  diff;
  };
  cvc5::TermManager&       tm_;
  std::vector<cvc5::Term>  inputs_;
  std::vector<int>         input_cycles_;
  std::vector<State_diff>  state_diffs_;
  uint64_t                 cycles_ = 1;
};

}  // namespace livehd::lec
