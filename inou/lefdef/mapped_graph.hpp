// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <memory>

#include "hhds/graph.hpp"
#include "physical.hpp"

namespace livehd::physical {
// Exact signal-cell and net import. No HDL reader, synthesis, cprop or removal
// of redundant/dangling cells. Flat Sub instances retain DEF instance names and
// LEF signal-pin declarations. Undriven nets with sinks and multiple signal
// drivers require a physical netlist owner. When supplied, unresolved receives
// their complete DEF hyperedges. Its caller must retain that physical model
// and exclude unsupported directed timing or logic operations on it.
std::shared_ptr<hhds::Graph> mapped_graph(hhds::GraphLibrary& library, const Floorplan& floorplan, const Technology& technology,
                                          std::vector<Net>* unresolved = nullptr);
}  // namespace livehd::physical
