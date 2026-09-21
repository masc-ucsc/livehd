// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "dlop.hpp"
#include "hhds/graph.hpp"
#include "pass_partition.hpp"

namespace livehd::abc {
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
  bool                                      reused = false;
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
class Satopt_seeds {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  Satopt_seeds();
  ~Satopt_seeds();
  std::optional<std::array<Dlop, 8>> sample(const hhds::Pin_class& pin);
};
std::string                             satopt_constant_key(const Dlop& value);
std::string                             satopt_source_key(hhds::Graph* graph);
// Combinational proofs: primary inputs and every state/opaque output are free
// symbols. An unsupported cone or an exhausted budget yields no facts.
std::shared_ptr<const Satopt_result>    satopt(hhds::Graph* graph, std::string_view cache_dir = {}, bool all_regions = false);
// Proves selectors constant (e.g. `x == x + 1`) under the same combinational
// model and ties each one to its constant on the caller's synthesis working
// copy. A never-selected arm is zeroed, and logic left without a consumer is
// deleted, so a dead cone disappears before partitioning. cvc5 decides each
// selector (pass/formal's Prover); an Unknown leaves it alone.
Select_satopt                           optimize_selects(const std::vector<std::shared_ptr<hhds::Graph>>& graphs,
                                                         std::string_view                                 cache_dir = {});
bool                                    satopt_crosses(const hhds::Node_class& node);
std::optional<std::vector<std::string>> satopt_region_facts(const Satopt_result&                  facts,
                                                            const livehd::partition::Region_body& region);
}  // namespace livehd::abc
