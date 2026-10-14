// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace livehd::lec {

// Shared by the encoder and single-design prover without linking the
// relational query engines or their optional ABC cone implementation.
enum class Verdict { Proven, Refuted, Undecided };

// INTERNAL cause of an undecided BLOCK-level result (hierarchical progress
// records, premise lists, the escalation driver). Top-level results use
// Outcome / undecided_outcome() below instead. "unknown" names nothing, so an
// undecided block is reported by its CAUSE. `word` is the printed verdict, `json` the
// machine-readable `verdict` value. The cause is read from the run's detail text
// (and the encoder-refusal flag), so every producer of a detail stays free-form.
//   TIMEOUT      a resource limit: formal.timeout reached (no proof, no counterexample, nothing claimed),
//                including cvc5's own TIMEOUT / RESOURCEOUT / MEMOUT / INTERRUPTED
//   UNSUPPORTED  nothing can check it: the encoder cannot model the design (a cell, a word-level
//                cycle, an encode failure) or every engine reported it cannot check the functionality
//   UNCHECKED    options forced an engine (e.g. induction only) that cannot handle this logic; never
//                happens under engine=auto
//   UNDISCHARGED a child premise the top relied on was never proven
//   UNCONFIRMED  a counterexample candidate exists but could not be confirmed
//   UNPAIRED     state correspondence between the two sides is incomplete
//   VACUOUS      the assume set is contradictory (a vacuous proof is rejected)
//   UNEXPLAINED  no recognized cause, or cvc5 gave no reason: a BUG, to be understood and named
struct Undecided_cause {
  std::string_view word;
  std::string_view json;
};

[[nodiscard]] inline Undecided_cause undecided_cause(std::string_view detail, bool unsupported = false) {
  const auto has = [&](std::string_view needle) { return detail.find(needle) != std::string_view::npos; };
  // A design-level refusal first: every engine fails on it, so more time cannot help.
  if (unsupported || has("encode failed") || has("encoder does not model") || has("WORD-LEVEL CYCLE") || has("REFUSED")
      || has("refused") || has("no encodable driver")) {
    return {"UNSUPPORTED", "unsupported"};
  }
  // Any resource limit is a timeout -- even when another engine also cannot check it.
  // Out of memory is a resource limit too (TIMEOUT-memory at the top level).
  if (has("formal.timeout") || has("wall deadline") || has("wall-clock limit") || has("timed out") || has("a resource limit")
      || has("MEMOUT") || has("out of memory") || has("memory backstop") || has("RLIMIT_AS") || has("bad_alloc")) {
    return {"TIMEOUT", "timeout"};
  }
  if (has("NO reason")) {
    return {"UNEXPLAINED", "unexplained"};
  }
  if (has("deeper than formal.bound") || has("escalation disabled")) {
    return {"UNCHECKED", "unchecked"};  // an option (the bound, a disabled escalation) forbade the check
  }
  if (has("cannot check this functionality")) {
    // Under engine=auto every engine was tried, so nothing can check it. A FORCED engine
    // (e.g. engine=ind) that cannot model the logic only means "not checked": the options asked for it.
    if (!has("auto:")) {
      return {"UNCHECKED", "unchecked"};
    }
    return {"UNSUPPORTED", "unsupported"};
  }
  if (has("CONTRADICTORY") || has("contradictory")) {
    return {"VACUOUS", "vacuous"};
  }
  if (has("never discharged")) {
    return {"UNDISCHARGED", "undischarged"};
  }
  if (has("refuted only at") || has("flat confirmation only BOUNDED") || has("unconfirmed")) {
    return {"UNCONFIRMED", "unconfirmed"};
  }
  if (has("correspondence") || has("unpaired") || has("ref-only cut point") || has("impl-only cut point")) {
    return {"UNPAIRED", "unpaired"};
  }
  return {"UNEXPLAINED", "unexplained"};
}

// ---------------------------------------------------------------------------
// TOP-LEVEL outcome of `lhd lec` and `lhd formal verify` (owner rulings
// 2026-10-14). The two commands print the SAME words and exit with the SAME
// codes. Every verdict carries a modifier; "unknown" is never printed.
//
//   PROVEN-full          unbounded (inductive) proof                                   exit 0
//   PROVEN-bounded(n)    no difference for n design cycles from reset; still a proof   exit 0
//   TESTED-sampled(N)    sampled effort presets: N input vectors agreed                exit 0
//   TIMEOUT-time         any clock limit: formal.timeout, wall deadline, cvc5
//                        resource/step budget                                          exit 0 + warning
//   TIMEOUT-memory       out of memory, caught instead of a crash (cvc5 MEMOUT, a
//                        child killed at the RLIMIT_AS backstop, std::bad_alloc)       exit 0 + warning
//   REFUTED-cex          a real counterexample trace (a trace that needs only a free
//                        blackbox/trusted-box output counts: the boxed model is the
//                        contract)                                                     exit 10
//   REFUTED-interface    a top port NAME exists on one side only (a width mismatch is
//                        fine; a one-sided input with no fanout is fine)               exit 7
//   REFUTED-unsupported  engine=auto and no engine can model/check it                  exit 7
//   REFUTED-unchecked    options forced an engine that cannot handle the logic, or
//                        forbade the escalation that would decide it                   exit 7
//   REFUTED-vacuous      the check proved nothing, a sign of a problem: a CONTRADICTORY
//                        assume set (it can prune away a real counterexample), nothing
//                        to check (no outputs/state: wrong top or dropped ports; verify
//                        with no assert), or a verify assert under an `if`/`match`
//                        guard that can never be true (dead branch)                    exit 7
//   REFUTED-unexplained  an engine gave up with no reason, or no recognized cause:
//                        a LiveHD BUG to understand and name                           exit 7
//
// There is NO "PROVEN-vacuous" (final owner ruling, 2026-10-14): every vacuous
// case is REFUTED-vacuous, because a check that proved nothing signals a problem.
//
// UNCONFIRMED / UNDISCHARGED / UNPAIRED (Undecided_cause above) are INTERNAL
// iteration triggers of the hierarchical driver (flat retry, inline a child,
// another engine), never a top-level outcome. One that still reaches the top is
// a bug and is reported as REFUTED-unexplained.
// ---------------------------------------------------------------------------
enum class Outcome {
  Proven_full,
  Proven_bounded,
  Tested_sampled,
  Timeout_time,
  Timeout_memory,
  Refuted_cex,
  Refuted_interface,
  Refuted_unsupported,
  Refuted_unchecked,
  Refuted_vacuous,
  Refuted_unexplained,
};

// Process exit codes of the two commands (lhd_kernel_common's error classes map onto these).
inline constexpr int kExitPass     = 0;
inline constexpr int kExitCex      = 10;  // REFUTED-cex only: a design difference/violation
inline constexpr int kExitNoAnswer = 7;   // every other REFUTED-<why>: tool, options, assumptions or a bug

struct Outcome_info {
  std::string_view word;  // printed verdict, WITHOUT the (n) suffix of the bounded/sampled kinds
  std::string_view json;  // machine-readable `verdict` value
  int              exit_code;
  bool             warn;  // exit 0 but worth a warning diagnostic
};

[[nodiscard]] constexpr Outcome_info outcome_info(Outcome o) {
  switch (o) {
    case Outcome::Proven_full: return {"PROVEN-full", "proven-full", kExitPass, false};
    case Outcome::Proven_bounded: return {"PROVEN-bounded", "proven-bounded", kExitPass, false};
    case Outcome::Refuted_vacuous: return {"REFUTED-vacuous", "refuted-vacuous", kExitNoAnswer, false};
    case Outcome::Tested_sampled: return {"TESTED-sampled", "tested-sampled", kExitPass, false};
    case Outcome::Timeout_time: return {"TIMEOUT-time", "timeout-time", kExitPass, true};
    case Outcome::Timeout_memory: return {"TIMEOUT-memory", "timeout-memory", kExitPass, true};
    case Outcome::Refuted_cex: return {"REFUTED-cex", "refuted-cex", kExitCex, false};
    case Outcome::Refuted_interface: return {"REFUTED-interface", "refuted-interface", kExitNoAnswer, false};
    case Outcome::Refuted_unsupported: return {"REFUTED-unsupported", "refuted-unsupported", kExitNoAnswer, false};
    case Outcome::Refuted_unchecked: return {"REFUTED-unchecked", "refuted-unchecked", kExitNoAnswer, false};
    case Outcome::Refuted_unexplained: return {"REFUTED-unexplained", "refuted-unexplained", kExitNoAnswer, false};
  }
  return {"REFUTED-unexplained", "refuted-unexplained", kExitNoAnswer, false};
}

[[nodiscard]] constexpr bool is_proven(Outcome o) {
  return o == Outcome::Proven_full || o == Outcome::Proven_bounded;
}
[[nodiscard]] constexpr bool is_timeout(Outcome o) { return o == Outcome::Timeout_time || o == Outcome::Timeout_memory; }
[[nodiscard]] constexpr bool is_refuted(Outcome o) { return outcome_info(o).exit_code != kExitPass; }

// The printed word, with the depth for the bounded/sampled kinds:
// "PROVEN-bounded(8)", "TESTED-sampled(2048)". `n` < 0 omits the suffix.
[[nodiscard]] inline std::string outcome_word(Outcome o, int64_t n = -1) {
  std::string w{outcome_info(o).word};
  if ((o == Outcome::Proven_bounded || o == Outcome::Tested_sampled) && n >= 0) {
    w += "(" + std::to_string(n) + ")";
  }
  return w;
}

// Severity order used to fold several results (properties of one verify run,
// arms of a portfolio) into one: a real counterexample wins, then the other
// refutes, then a resource limit, then the weaker proofs.
[[nodiscard]] constexpr int outcome_rank(Outcome o) {
  switch (o) {
    case Outcome::Refuted_cex: return 100;
    case Outcome::Refuted_interface: return 90;
    case Outcome::Refuted_unsupported: return 80;
    case Outcome::Refuted_unchecked: return 75;
    case Outcome::Refuted_unexplained: return 70;
    case Outcome::Timeout_memory: return 50;
    case Outcome::Timeout_time: return 45;
    case Outcome::Tested_sampled: return 30;
    case Outcome::Refuted_vacuous: return 85;
    case Outcome::Proven_bounded: return 15;
    case Outcome::Proven_full: return 10;
  }
  return 70;
}
[[nodiscard]] constexpr Outcome worse(Outcome a, Outcome b) { return outcome_rank(a) >= outcome_rank(b) ? a : b; }

// The outcome of an UNDECIDED top-level result (Verdict::Undecided), from its
// detail text. `unsupported` is the encoder-refusal flag; `engine_forced` is
// true when the options pinned an engine/profile (anything but engine=auto).
// A memory limit is checked before a time limit (a child killed by the
// RLIMIT_AS backstop also hits the wall deadline in the same run).
[[nodiscard]] inline Outcome undecided_outcome(std::string_view detail, bool unsupported, bool engine_forced) {
  const auto has = [&](std::string_view needle) { return detail.find(needle) != std::string_view::npos; };
  if (unsupported || has("encode failed") || has("encoder does not model") || has("WORD-LEVEL CYCLE") || has("REFUSED")
      || has("refused") || has("no encodable driver")) {
    return Outcome::Refuted_unsupported;  // a design-level refusal: every engine fails on it
  }
  if (has("MEMOUT") || has("out of memory") || has("memory backstop") || has("RLIMIT_AS") || has("bad_alloc")) {
    return Outcome::Timeout_memory;
  }
  if (has("formal.timeout") || has("wall deadline") || has("wall-clock limit") || has("timed out") || has("a resource limit")) {
    return Outcome::Timeout_time;
  }
  if (has("NO reason")) {
    return Outcome::Refuted_unexplained;
  }
  // An option forbade the check: an obligation whose window/past() history is
  // deeper than formal.bound is checked at no cycle, and a disabled escalation
  // (decompose=true, hier_refute=fail, a non-after_reset phase) never ran the
  // step that would decide it.
  if (has("deeper than formal.bound") || has("escalation disabled")) {
    return Outcome::Refuted_unchecked;
  }
  if (has("cannot check this functionality")) {
    return engine_forced ? Outcome::Refuted_unchecked : Outcome::Refuted_unsupported;
  }
  if (has("CONTRADICTORY") || has("contradictory")) {
    return Outcome::Refuted_vacuous;  // a contradictory assume set can prune away a real counterexample
  }
  return Outcome::Refuted_unexplained;  // incl. an internal cause that leaked to the top: a bug
}

// The outcome of ONE equivalence query whose engine the CALLER pinned, with no
// escalation behind it: the synthesis tuning validation (pass.abc, pass.usyn)
// runs engine=ind and nothing else. A Proven names its strength. A Refuted is a
// real counterexample (a pinned ind engine refutes only a stateless miter). An
// empty miter is vacuous when neither side exposes anything to compare, and an
// interface mismatch when outputs exist but none pairs up. An undecided query
// is classified as a top-level one with the engine forced; an induction
// give-up that classifier does not name (a single-step CEX from an arbitrary
// start state, an incomplete state correspondence) is the pinned engine not
// handling the logic, so REFUTED-unchecked. `n` for outcome_word() is the
// bounded depth (Query_result::checked_steps).
[[nodiscard]] inline Outcome pinned_engine_outcome(Verdict verdict, bool bounded, bool nothing_compared, bool unsupported,
                                                   std::string_view detail) {
  const auto has = [&](std::string_view needle) { return detail.find(needle) != std::string_view::npos; };
  if (nothing_compared) {
    if (unsupported) {
      return Outcome::Refuted_unsupported;
    }
    return has("correspondence incomplete") ? Outcome::Refuted_interface : Outcome::Refuted_vacuous;
  }
  if (verdict == Verdict::Proven) {
    return bounded ? Outcome::Proven_bounded : Outcome::Proven_full;
  }
  if (verdict == Verdict::Refuted) {
    return Outcome::Refuted_cex;
  }
  const Outcome o = undecided_outcome(detail, unsupported, /*engine_forced=*/true);
  if (o == Outcome::Refuted_unexplained && !has("NO reason")
      && (has("single-step CEX") || has("unmatched") || has("correspondence") || has("cut point"))) {
    return Outcome::Refuted_unchecked;
  }
  return o;
}

[[nodiscard]] inline bool is_assume_kind(std::string_view kind) { return kind == "assume" || kind == "assume_nocheck"; }

[[nodiscard]] inline bool is_unchecked_assume_class(std::string_view aclass) {
  return aclass == "unchecked" || aclass == "check_disabled";
}

}  // namespace livehd::lec
