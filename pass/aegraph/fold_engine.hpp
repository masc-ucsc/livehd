//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The folding configuration every place that folds a graph body outside the compile schedule shares
// (pass/aegraph/fold_body.hpp): the specialize refolds (and with them the cgen sim post-splice fold and the
// synth state sweep), the synthesis loop cleanup, the memory-module lowering, the satopt cleanup and the
// LEC/semdiff preflight. The e-graph engine is the only engine; what varies is the cost profile, set ONCE
// per command by the lhd kernel from `compile.aegraph.cost` (apply_lhd_settings). This header has no
// dependency on the engine, so cache code can key on it without linking it.

#include <cstdint>
#include <string>
#include <string_view>

namespace livehd {

enum class Fold_cost : uint8_t { synth, sim };

[[nodiscard]] const char* fold_cost_name(Fold_cost cost);

// The cost profile of the sites that do not pin one (the kernel's resolved compile.aegraph.cost).
void                    set_fold_default_cost(Fold_cost cost);
[[nodiscard]] Fold_cost fold_default_cost();
// The rules every fold site turns off (compile.aegraph.rules_disable: names or group:NAME joined with '+'),
// so the knob reaches the fold sites as it reaches the two compile slots. Empty = every rule on.
void                      set_fold_rules_disable(std::string list);
[[nodiscard]] std::string fold_rules_disable();

// Identity of the folding configuration for persistent caches: "engine=aegraph;cost=<name>", plus
// ";rules_disable=<list>" when some rule is off.
[[nodiscard]] std::string fold_descriptor();
// `salt` mixed with fold_descriptor(): a cache keyed by the result never mixes cost profiles. Every
// synthesis/formal salt that a fold site can influence goes through this.
[[nodiscard]] uint64_t fold_salt_mix(uint64_t salt);

// Observability: every fold_body call counts under its site; with LHD_FOLD_BODY_LOG set in the environment
// each call also prints `fold_body engine=aegraph site=<site>` to stderr.
void                   fold_note(std::string_view site);
[[nodiscard]] uint64_t fold_count(std::string_view site);
void                   fold_counters_reset();

}  // namespace livehd
