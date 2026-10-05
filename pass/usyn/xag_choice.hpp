// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "xag.hpp"

namespace livehd::usyn {
// Immutable, total-function evidence on one ordered independent basis. Member
// ownership never contributes to selected-network references or deletion credit.
// The graph may append scratch nodes, but its existing nodes must stay immutable.
class Xag_choice {
public:
  Xsignal                     incumbent() const { return root; }
  const std::vector<Xsignal>& members() const { return alternatives; }
  const std::vector<Id>&      inputs() const { return basis; }

private:
  Xsignal                          root;
  std::vector<Id>                  basis;
  Truth_table                      function;
  std::vector<Xsignal>             alternatives;
  friend std::optional<Xag_choice> make_choice(const Xag&, Xsignal, std::span<const Id>, Budget&);
  friend Status                    retain_choice(const Xag&, Xag_choice&, Xsignal, Budget&, uint32_t);
};
std::optional<Xag_choice> make_choice(const Xag& graph, Xsignal root, std::span<const Id> basis, Budget& work);
// Reject root/transitive-fanout dependencies even when Boolean cancellation
// makes their truth table equal. Caps include the incumbent; phases are explicit.
Status                    retain_choice(const Xag& graph, Xag_choice& choice, Xsignal alternative, Budget& work, uint32_t cap = 3);

struct Choice_extraction {
  Status               status = Status::invalid;
  Xag                  graph;
  std::vector<Xsignal> outputs;
};
// Pick one member per class, then rebuild the complete selected DAG with real
// shared references. Detect overlap-induced cycles; never publish partial output.
// Sources, including unused ones, retain their order. No choices reach tmap.
Choice_extraction extract_choices(const Xag& graph, std::span<const Xag_choice> choices, std::span<const uint32_t> selected,
                                  std::span<const Xsignal> outputs, Budget& work, uint32_t max_nodes = 2000000);
}  // namespace livehd::usyn
