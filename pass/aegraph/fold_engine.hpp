//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The process-wide choice of constant-folding engine for every place that folds a graph body outside the
// compile schedule's two pass.cprop slots (pass/aegraph/fold_body.hpp dispatches on it): the specialize
// refolds (and with them the cgen sim post-splice fold and the synth state sweep), the synthesis loop
// cleanup, the memory-module lowering, the satopt cleanup and the LEC/semdiff preflight.
//
// The lhd kernel sets it ONCE per command from `compile.cprop.engine` (apply_lhd_settings); the default is
// classic, so a process that never sets it behaves exactly as before. This header has no dependency on the
// engine or on cprop, so cache code can key on it without linking either.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace livehd {

enum class Fold_engine : uint8_t { classic, aegraph, aegraph_classic };
enum class Fold_cost : uint8_t { synth, sim };

// "classic" | "aegraph" | "aegraph+classic" (nullopt for anything else).
[[nodiscard]] std::optional<Fold_engine> parse_fold_engine(std::string_view text);
[[nodiscard]] const char*                fold_engine_name(Fold_engine engine);
[[nodiscard]] const char*                fold_cost_name(Fold_cost cost);

// Process-wide setting; set once by the kernel before any pass runs, read by every worker.
void                      set_fold_engine(Fold_engine engine);
[[nodiscard]] Fold_engine fold_engine();
// The cost profile of the sites that do not pin one (the kernel's resolved compile.aegraph.cost).
void                      set_fold_default_cost(Fold_cost cost);
[[nodiscard]] Fold_cost   fold_default_cost();

// Identity of the folding configuration for persistent caches: empty for classic (so classic cache keys
// are unchanged), else "engine=<name>;cost=<name>".
[[nodiscard]] std::string fold_descriptor();
// `salt` unchanged for classic, else mixed with fold_descriptor(): a cache keyed by the result never mixes
// the engines. Every synthesis/formal salt that a fold site can influence goes through this.
[[nodiscard]] uint64_t    fold_salt_mix(uint64_t salt);

// Observability: every fold_body call counts under (site, engine); with LHD_FOLD_BODY_LOG set in the
// environment each call also prints `fold_body engine=<engine> site=<site>` to stderr.
void                      fold_note(std::string_view site, Fold_engine engine);
[[nodiscard]] uint64_t    fold_count(std::string_view site, Fold_engine engine);
[[nodiscard]] uint64_t    fold_count(std::string_view site);
void                      fold_counters_reset();

}  // namespace livehd
