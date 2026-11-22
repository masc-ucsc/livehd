// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <print>
#include <string_view>

#include "diag.hpp"
#include "property_types.hpp"
#include "query.hpp"
#include "tune_store.hpp"

namespace livehd::synth {

inline bool validate_tune_equivalence(hhds::Graph* ref, hhds::Graph* impl,
                                      const absl::flat_hash_map<hhds::Gid, hhds::Graph*>& definitions, Tune_store& store,
                                      std::string_view pass, std::string_view validation, std::string_view region) {
  lec::Lec_options policy;
  policy.engine            = "ind";
  policy.timeout           = 20;
  policy.min_timeout       = 1;
  policy.hard_timeout_mult = 2;
  const auto result        = lec::prove_equal_isolated(ref, impl, policy, &definitions);
  const auto outcome
      = lec::pinned_engine_outcome(result.verdict, result.bounded, result.nothing_compared, result.unsupported, result.detail);
  const auto word = lec::outcome_word(outcome, result.checked_steps > 0 ? result.checked_steps : policy.bound);
  std::print("[synth.tune] validation={} region={} result={} detail={} mapper={}\n",
             validation,
             region,
             word,
             result.detail,
             pass.substr(pass.find('.') + 1));
  if (result.verdict == lec::Verdict::Refuted) {
    store.authorize_all(false);
    store.save();
    diag::err(pass, "synth-tune-refuted-cex", "unsupported")
        .msg("{}: synthesis tuning changed the function of {} (counterexample: {})",
             word,
             region,
             result.witness.empty() ? std::string{"none recorded"} : result.witness)
        .fatal();
  }
  // Only an unbounded proof authorizes the tuned mapping.
  return outcome == lec::Outcome::Proven_full;
}

}  // namespace livehd::synth
