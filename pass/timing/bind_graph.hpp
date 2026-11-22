// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "hhds/graph.hpp"
#include "netlist.hpp"
#include "sdc.hpp"

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
// Memories are clock boundaries with zero internal delay: each consumed read-data bit is a start point (an
// input port cell named `<memory>:out_<pid>[<bit>]`) and each runtime data/address/enable bit it samples is an
// endpoint (an output port cell `<memory>:in_<pid>[<bit>]`). The names are the placement labels of the memory
// macro pins, so placed RC binds to them.
//
// What is not, is counted and listed: cells with no Liberty model, instances that are not flat, logic that
// was not mapped (the sink behind it is left undriven), sinks driven by a constant.
namespace livehd::timing::fast {

struct Bound_graph {
  std::unique_ptr<Netlist>      netlist;
  // Per netlist cell; an invalid node for a port cell. After bind_hier it is the DEFINITION node the occurrence is an
  // instance of (its graph is the module the cell belongs to; class attributes such as srcid read from it).
  std::vector<hhds::Node_class> cell_node;
  // bind_hier only (empty after bind_graph): per cell, the dotted instance path of the occurrence (`a.b.`, empty at the
  // top), the full hierarchical name (prefix + node name, the name a flattened copy would give the cell), and the color of
  // the occurrence (its hier_color, else the definition color).
  std::vector<uint32_t>         cell_prefix;
  std::vector<std::string>      prefixes;
  std::vector<std::string>      cell_name;
  std::vector<int32_t>          cell_color;
  std::vector<std::string>      unsupported;
  size_t                        mapped_cells = 0, opaque_cells = 0, unmapped_nodes = 0, memory_nodes = 0;
  size_t                        constant_sinks = 0, undriven_sinks = 0, multiple_driver_sinks = 0, unresolved_sinks = 0;

  [[nodiscard]] bool             hierarchical() const { return !cell_name.empty(); }
  // The name of a cell: its hierarchical name after bind_hier, the graph node name after bind_graph.
  [[nodiscard]] std::string_view name(uint32_t cell) const {
    return hierarchical() ? std::string_view{cell_name[cell]} : cell_node[cell].get_name();
  }
};

Bound_graph bind_graph(const hhds::Graph& flat, const Cell_types& types);

// The same binding over the instance hierarchy of `top`, without flattening it: the cells are the occurrences of the
// body-less Liberty instances found by the hierarchical traversal (graph->occurrences()), and a sink is resolved to its
// driver across instance boundaries by the occurrence connectivity. Names are the dotted instance paths a flattened copy
// would carry, so constraints and reports see the same design.
Bound_graph bind_hier(hhds::Graph& top, const Cell_types& types);
// Optionally retain the bound values of literal sink pins for structural analysis.
Bound_graph bind_hier(hhds::Graph& top, const Cell_types& types, absl::flat_hash_map<uint32_t, bool>* constants);

// The names of a bound graph for the constraint reader: instances by their name in the graph, nets by the name
// of the pin that drives them (the name the Verilog writer gives the wire: the pin's own name, or the name of
// its instance). The names are views into the graph, which must stay alive and unchanged; the net names are
// gathered the first time a net is asked for, so a constraint file without nets costs nothing.
class Bound_names final : public Design_names {
public:
  explicit Bound_names(const Bound_graph& bound);
  [[nodiscard]] uint32_t cell(std::string_view instance) const override;
  void                   cells(const std::function<void(std::string_view, uint32_t)>& visit) const override;
  [[nodiscard]] uint32_t net_driver(std::string_view net) const override;
  void                   nets(const std::function<void(std::string_view, uint32_t)>& visit) const override;

private:
  void                                                    gather_nets() const;
  [[nodiscard]] std::string_view                          cell_name(uint32_t cell) const { return bound_.name(cell); }
  const Bound_graph&                                      bound_;
  absl::flat_hash_map<std::string_view, uint32_t>         cells_;
  mutable bool                                            nets_ready_ = false;
  mutable absl::flat_hash_map<std::string, uint32_t>      nets_;
};

}  // namespace livehd::timing::fast
