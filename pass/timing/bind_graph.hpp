// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "hhds/graph.hpp"
#include "netlist.hpp"

// Binding of a flat, technology-mapped graph to the numeric netlist of the fast timing engine (todo
// 2q-fasttimer, step B). The graph is the authority on connectivity; the netlist is built from it once per
// topology and keeps, per cell index, the graph node it came from, so that results can be written back.
//
// What is bound: Sub instances of body-less Liberty cells (the cell is found by the name of its IO
// declaration); the bits of the graph's input and output ports (a port of width w is w port cells named
// `name[bit]`, a one-bit port keeps its name). The wiring between them, which a synthesized graph keeps as
// get_mask / set_mask / concat nodes and constant shifts, is resolved bit by bit down to the driving cell
// pin or port bit, so a sink pin is connected to its real driver.
//
// What is not, is counted and listed: cells with no Liberty model, instances that are not flat, logic that
// was not mapped (the sink behind it is left undriven), sinks driven by a constant, memories.
namespace livehd::timing::fast {

struct Bound_graph {
  std::unique_ptr<Netlist>      netlist;
  std::vector<hhds::Node_class> cell_node;  // per netlist cell; an invalid node for a port cell
  std::vector<std::string>      unsupported;
  size_t                        mapped_cells = 0, opaque_cells = 0, unmapped_nodes = 0;
  size_t                        constant_sinks = 0, undriven_sinks = 0, multiple_driver_sinks = 0, unresolved_sinks = 0;
};

Bound_graph bind_graph(const hhds::Graph& flat, const Cell_types& types);

}  // namespace livehd::timing::fast
