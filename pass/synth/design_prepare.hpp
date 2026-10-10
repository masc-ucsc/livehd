// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "hhds/graph.hpp"
#include "loop_cleanup.hpp"
#include "preparation_budget.hpp"

namespace livehd::synth {

// Owns the copied definition closure through mapping/read-back. Roots retain
// the caller's order (including null entries); resolve_graphs starts with those
// roots so implicit top selection never depends on library hash order.
struct Prepared_design {
  hhds::GraphLibrary                        library;
  std::vector<std::shared_ptr<hhds::Graph>> roots;
  std::vector<std::shared_ptr<hhds::Graph>> definitions;
  std::vector<std::shared_ptr<hhds::Graph>> resolve_graphs;
  Loop_preparation                          loops;
};

// ABC-independent private-copy and compact-loop preparation. Source graphs
// and their libraries remain untouched. Opaque callee IOs are copied as well
// as bodies, so nested blackboxes retain their declared ports. Optional budget
// refusal returns no partial design and sets budget->refused; other failures
// retain their existing diagnostics. A refused budget stays refused.
// `specialize` runs pass.specialize (state-free callees only) on the private
// copy, after pass.color decided the regions (synth.specialize, default true).
std::unique_ptr<Prepared_design> prepare_design(std::span<const std::shared_ptr<hhds::Graph>> sources, bool unroll_carry,
                                                std::string_view from_pass, Preparation_budget* budget = nullptr,
                                                bool specialize = true);

}  // namespace livehd::synth
