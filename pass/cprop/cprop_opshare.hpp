// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "node_util.hpp"
namespace livehd {
// Commit a same-shape arithmetic pair AFTER the caller proves the complete
// activations exclusive and rejects dependencies on either replaced output.
// All uses must be owned single data edges, and both results must have the
// same realization. Returns false without edits when shape/width guards fail.
bool share_exclusive_operators(hhds::Graph& graph, hhds::Node_class first, hhds::Node_class second, hhds::Pin_class selector,
                               bool first_when_true, size_t& budget, bool commit);
}  // namespace livehd
