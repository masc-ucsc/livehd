// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// 2f-fcore verdict cache (todo/livehd/2f-fcore.html §3): --workdir-persistent
// definitive-verdict records + strategy hints for the formal engines.
//
// Soundness (rule F): only DEFINITIVE verdicts are cached — v1 stores PROVEN
// only (a REFUTE is rare and fast to reconfirm; caching it would need witness
// revalidation). Unknown is NEVER cached. The whole verdict section is keyed
// under the engine-identity salt (kFormalSrcSalt: a build-time content hash of
// pass/lec + pass/semdiff + the cvc5 pin — //lhd:formal_salt), so any prover
// change drops every cached verdict on load, with no human in the loop.
//
// Strategy hints are the cache's third record kind (user note 2026-07-10):
// which `auto` choice WON per def — keyed by canonical entity NAME, not digest,
// so a hint survives the design edit that misses the verdict cache and the
// next run tries the known-good strategy first. Hints are heuristic-only (they
// order the portfolio, never decide a verdict), so they deliberately survive
// salt changes too.

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

namespace livehd::formal {

struct Cached_verdict {
  std::string engine;          // winning engine ("ind", "bmc", "casesplit", "semdiff")
  std::string detail;          // original detail line (replayed inside the hit's detail)
  long long   elapsed_ms = 0;  // original solve time
};

struct Strategy_hint {
  std::string engine;          // winning engine last time
  std::string split;           // winning case-split selector ("" = none)
  long long   elapsed_ms = 0;
};

// Fourth record kind (ruling 2026-07-10): an Unknown ATTEMPT ledger — not a
// verdict (the def still reports inconclusive), just "this exact digest+options
// already came back Unknown at this budget", so an unchanged re-run skips the
// re-grind instead of burning the full solver budget again. Salt-gated with the
// verdicts (a prover change justifies a retry) and budget-aware (a larger
// timeout re-attempts). lec.retry=all forces a full re-grind.
struct Unknown_attempt {
  int       timeout    = 0;  // seconds the attempt was given (0 = unbounded)
  long long elapsed_ms = 0;
};

// Fifth record kind (2f-lec tier-2): state-pair hints — the uncertain flop
// pairs that participated in a PASS, keyed by canonical entity NAME like
// strategy hints (and surviving salt like them), so a warm run injects the
// known-good pairing without re-running semdiff's signature pass. UNLIKE
// strategy hints they alter the obligation set: replayed pairs re-enter the
// verdict-cache key (um=[...]), keep the full uncertain discipline
// (drop-and-retry, no bounded-Proven), and are re-validated at injection
// (lec::validate_uncertain_pairs — flops must still exist and the init-equality
// precondition must still hold, since entity-keyed hints survive edits that
// can change either).
struct Pair_hint {
  std::vector<std::pair<std::string, std::string>> pairs;  // {ref, impl} raw names
};

// Sixth record kind (lec.cones): PROVEN cone obligations, keyed by the cone's
// canonical structural digest (lec::cone_digest). Unlike every other record
// here the key needs NOTHING else — a cone obligation is self-contained ("is
// this term UNSAT with every free symbol unconstrained"), so the digest IS the
// claim; there are no options, no entity name and no def-pair to qualify it.
// That is what makes it incremental at a much finer grain than the def-pair
// verdicts: editing one pipeline stage only changes the digests of the cones
// whose logic actually moved, and every other cone still hits. Salt-gated with
// the verdicts (rule F) — a prover change drops them all.

class Verdict_cache {
public:
  // Loads <workdir>/formal_cache.json when present. A salt mismatch drops the
  // verdict records (stale prover) but keeps the hints.
  Verdict_cache(std::string workdir, uint64_t salt);

  std::optional<Cached_verdict> lookup(const std::string& key);
  void                          insert(const std::string& key, Cached_verdict v);

  std::optional<Strategy_hint> hint(const std::string& entity) const;
  void                         set_hint(const std::string& entity, Strategy_hint h);

  std::optional<Pair_hint> pair_hint(const std::string& entity) const;
  void                     set_pair_hint(const std::string& entity, Pair_hint h);
  // Self-heal: a replayed hint whose solve did NOT end Proven is stale (e.g.
  // the design changed under the same entity name) — drop it so the next run
  // re-derives the pairing fresh instead of re-suppressing the signature pass.
  void                     clear_pair_hint(const std::string& entity);

  // Unknown-attempt ledger: skip_unknown() is true when a re-attempt at
  // `timeout_s` cannot outspend the recorded attempt (records with timeout 0 =
  // unbounded dominate everything).
  bool skip_unknown(const std::string& key, int timeout_s) const;
  void note_unknown(const std::string& key, Unknown_attempt a);

  // Cone verdicts (lec.cones): the whole PROVEN digest set is handed to the
  // engine by value (Lec_options::_cone_cache) because the engine may run in a
  // forked worker that must not touch this file; it reports back what it newly
  // proved (Query_result::cone_proven) and the driver notes it here.
  const absl::flat_hash_set<std::string>& cone_digests() const { return cones_; }
  void                                    note_cone_proven(const std::string& digest);

  int  cone_hits() const { return cone_hits_; }
  int  hits() const { return hits_; }
  int  stores() const { return stores_; }
  int  skips() const { return skips_; }

  // Atomic persist (tmp + rename). No-op when nothing changed.
  void save() const;

private:
  std::string workdir_;
  uint64_t    salt_;
  bool        dirty_  = false;
  int         hits_   = 0;
  int         stores_ = 0;
  mutable int skips_  = 0;
  mutable std::mutex                                mutex_;
  absl::flat_hash_map<std::string, Cached_verdict>  verdicts_;
  absl::flat_hash_map<std::string, Strategy_hint>   hints_;
  absl::flat_hash_map<std::string, Pair_hint>       pair_hints_;
  absl::flat_hash_map<std::string, Unknown_attempt> unknowns_;
  absl::flat_hash_set<std::string>                 cones_;
  int                                              cone_hits_ = 0;  // digests loaded from disk
};

}  // namespace livehd::formal
