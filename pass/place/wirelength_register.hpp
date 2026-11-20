// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "hhds/graph.hpp"
#include "pass/liberty/liberty_dff.hpp"

namespace livehd::placement {
// Return a private proof view only for a flat physical netlist containing
// register replicas. Fold a replica after checking the characterized state
// element, the initial/reset condition and every data and control binding.
// Source graph and physical checkpoint retain their independent instances.
bool                         wire_register_has_reset(hhds::Node_class node, const liberty::Dff_cell& cell);
bool                         has_wire_register_replicas(hhds::Graph& graph);
std::shared_ptr<hhds::Graph> wire_register_proof_view(hhds::Graph& graph, hhds::GraphLibrary& owner,
                                                      const liberty::Cell_library& technology, bool after_reset);
}  // namespace livehd::placement
