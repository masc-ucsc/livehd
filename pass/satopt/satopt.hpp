// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// SAT simplification on the private synthesis copy, before partitioning. The
// selector proofs (cvc5) and the memory proofs live here; the per-bit mux
// facts (satopt_mux.hpp, proven by a backend such as ABC `&fraig`,
// pass/abc/abc_satopt.hpp) share the fact types, the rejection seeds and the
// rewrite helpers below.

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "dlop.hpp"
#include "hhds/graph.hpp"
#include "satopt_stages.hpp"

namespace livehd::satopt {
// A theorem about an arm bit under that arm's selection predicate. Arm numbers
// are zero-based; Hotmux's fallback follows its explicit arms.
struct Mux_fact {
  enum class Kind { zero, one, equal, complement };
  uint64_t node                               = 0;
  int      arm                                = 0;
  int      bit                                = 0;
  Kind     kind                               = Kind::zero;
  int      other                              = -1;
  auto     operator<=>(const Mux_fact&) const = default;
};
struct Satopt_result {
  std::map<uint64_t, std::vector<Mux_fact>> mux;
  uint64_t                                  candidates = 0, survivors = 0, proven = 0;
  uint64_t                                  queries = 0, budget_skips = 0;
  bool                                      reused   = false;
  bool                                      complete = true;  // false: a cap or the budget cut the search short
};
// A selector with the same truth value for every input and state: the two-arm
// Mux select and the Flop enable are control 0; a Hotmux control is its arm.
struct Select_fact {
  uint64_t node                                  = 0;
  int      control                               = 0;
  bool     value                                 = false;
  auto     operator<=>(const Select_fact&) const = default;
};
struct Select_satopt {
  uint64_t candidates = 0, survivors = 0, proven = 0, reused = 0, muxes = 0, hotmux_arms = 0, enables = 0;
};
struct Mux_satopt {
  uint64_t candidates = 0, survivors = 0, proven = 0, muxes = 0, arms = 0, bits = 0;
  uint64_t queries = 0, budget_skips = 0, nodes_removed = 0;
  bool     reused   = false;
  bool     complete = true;
};
// The word-level simulation satopt filters candidates with (satopt_sim.hpp):
// one Dlop per pattern, nullopt when the value is unavailable. Patterns depend
// only on the leaf, so the same pin samples the same before and after an
// equivalent rewrite.
class Satopt_seeds {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  explicit Satopt_seeds(uint32_t samples = Budget{}.samples);
  ~Satopt_seeds();
  std::optional<std::vector<Dlop>> sample(const hhds::Pin_class& pin);
};
std::string                             satopt_constant_key(const Dlop& value);
std::string                             satopt_source_key(hhds::Graph* graph);
// Proves selectors constant (e.g. `x == x + 1`) under the same combinational
// model and ties each one to its constant on the caller's synthesis working
// copy. A never-selected arm is zeroed, and logic left without a consumer is
// deleted, so a dead cone disappears before partitioning. cvc5 decides each
// selector (pass/formal's Prover); an Unknown leaves it alone.
//
// `profile` decides what a proven Hotmux control may change: synthesis ties
// every later arm off after an always-true control (priority semantics), the
// shared profile keeps every control the obligation reads (satopt_stages.hpp).
//
// `meter` bounds the search (satopt_stages.hpp; null = unlimited): out of
// budget, the remaining candidates are skipped and the proof row is partial.
Select_satopt                           optimize_selects(const std::vector<std::shared_ptr<hhds::Graph>>& graphs,
                                                         std::string_view cache_dir = {}, Profile profile = Profile::synthesis,
                                                         Stage_report* report = nullptr, Meter* meter = nullptr);
// Deletes combinational logic no output, state or instance observes, on a
// PRIVATE synthesis copy. Compile keeps a dead Hotmux on purpose -- its
// `unique if` exclusivity is an obligation sim/formal still check -- but
// synthesis may ignore obligations, and each such cone would otherwise become
// its own tiny region and a pile of select proofs. Returns the nodes deleted.
uint64_t                                drop_dead_logic(hhds::Graph* graph);
}  // namespace livehd::satopt
