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
#include <optional>
#include <string>

#include "absl/container/flat_hash_map.h"

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

class Verdict_cache {
public:
  // Loads <workdir>/formal_cache.json when present. A salt mismatch drops the
  // verdict records (stale prover) but keeps the hints.
  Verdict_cache(std::string workdir, uint64_t salt);

  std::optional<Cached_verdict> lookup(const std::string& key);
  void                          insert(const std::string& key, Cached_verdict v);

  std::optional<Strategy_hint> hint(const std::string& entity) const;
  void                         set_hint(const std::string& entity, Strategy_hint h);

  int  hits() const { return hits_; }
  int  stores() const { return stores_; }

  // Atomic persist (tmp + rename). No-op when nothing changed.
  void save() const;

private:
  std::string workdir_;
  uint64_t    salt_;
  bool        dirty_  = false;
  int         hits_   = 0;
  int         stores_ = 0;
  absl::flat_hash_map<std::string, Cached_verdict> verdicts_;
  absl::flat_hash_map<std::string, Strategy_hint>  hints_;
};

}  // namespace livehd::formal
