// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <filesystem>

#include "artifact.hpp"

namespace livehd::usyn {
struct Logical_cache_options {
  std::filesystem::path directory{};  // empty disables both reads and writes
  std::string           context{};    // additional invocation resource policy, never ABC/Liberty
  uint64_t              entry_work = 1000000000;
  Artifact_limits       limits;
};
struct Logical_cache_stats {
  bool     enabled = false;
  // credit_misses (a subset of misses): a valid record whose credit floor the
  // region's current credits do not reproduce; the region is re-searched.
  // kept (a subset of credit_misses): the re-search was not stored because the
  // record it missed is the more general one (replaces_stored_record).
  uint64_t reused = 0, misses = 0, invalid = 0, refused = 0, credit_misses = 0, kept = 0, stored = 0, store_failures = 0;
  // Hits replay their recorded search work on the search remainder and their
  // structural work on the structural allowance.
  uint64_t io_work = 0, replayed_search_work = 0, replayed_structural_work = 0;
};
enum class Cache_lookup { hit, miss, invalid, refused, credit_miss };
struct Logical_cache_probe {
  Cache_lookup                    outcome = Cache_lookup::miss;
  std::string                     key, reason;
  std::optional<Selection_record> record;
  // On a credit miss: the stored record's credit floor.
  std::optional<Credit_floor>     stored{};
};

// Each region gets a separate bounded I/O budget carrying the invocation's
// process/time admission callback. Cache validation must not change its search
// credits or those of later regions. The key is serialize_logical_identity,
// which excludes credits: `module` and every name outside `names`/the state
// snapshot is provenance. A stored record is a hit only when its credit floor
// reproduces the search under `credits` (the region's current search credits):
// credits >= floor for an unbound search, exactly the recorded credits for a
// bound one; otherwise the probe reports credit_miss (with the stored floor)
// and the caller re-searches, storing the result as replaces_stored_record
// decides. On a hit the caller replays the recorded search work on its search
// remainder and the recorded structural work on its structural allowance. A
// hit is rebound to the current module, RAW port spellings and source snapshot
// (rebind_selection), so a renamed definition or boundary net reuses its
// decisions while a renamed register, memory or top-level IO misses.
Logical_cache_probe probe_logical_cache(const Logical_cache_options& options, std::string_view module, const synth::Lnet& raw,
                                        const synth::Source_state_table& source, const Identity_names& names,
                                        const Logical_options& logical, uint64_t credits, Budget& io);
// Whether a fresh search's record should replace the one `probe` found. An
// unbound record reproduces at every credit level from its floor up, a bound
// one only at its exact credits, so an unbound record is never replaced by a
// bound one: after a credit miss below its floor the unbound record stays and
// a later run at the original credits hits again. Every other fresh record
// (no record, an invalid one, or a bound one missed at other credits) is
// stored; so bound records under one key overwrite each other, latest wins.
// A credit miss below an unbound floor always yields a bound search (an
// unbound run's floor never exceeds its credits).
bool                replaces_stored_record(const Logical_cache_probe& probe, const Credit_floor& fresh);
// `search` is Budget::credit_floor() of the finished search (never one that
// observed a process/time refusal); `structural` its structural-allowance work.
bool                store_logical_cache(const Logical_cache_options& options, std::string_view key, std::string_view module,
                                        const Stateful_region& selected, const Logical_report& report, const Credit_floor& search,
                                        uint64_t structural, Budget& io);
}  // namespace livehd::usyn
