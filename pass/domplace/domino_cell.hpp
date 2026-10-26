// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "function.hpp"
#include "hhds/graph.hpp"
#include "physical_emission.hpp"

namespace livehd::domplace {

inline constexpr uint32_t kDominoModelVersion = 1;

// One provisional domino function: an exact positive-output formula over its
// inputs, its ordered rails and its Q function. Identity is Boolean and
// structural, never a shape bin: equal metrics with a different function, rail
// order or formula never share a master.
struct Domino_master {
  uint64_t                     identity = 0;
  usyn::Truth_table            function;
  usyn::Gate_formula           formula;
  std::vector<usyn::Gate_rail> rails;  // pin order R0..R{k-1}
  std::string                  base;   // __lhd_domino_<h16>
};

// identity = hash(model version, input count, postorder formula, rails, truth
// table words). The formula must be valid, positive-output and use every input;
// anything else throws std::invalid_argument. nullopt only on work exhaustion.
std::optional<Domino_master> make_domino_master(const usyn::Gate_formula& formula, uint32_t inputs, usyn::Budget& work);

// The netlist-side interface of one drive variant and geometry template:
// `<base>_v<variant>_t<template>` with pins R*, CLK and Q.
usyn::Physical_cell_interface domino_interface(const Domino_master& master, size_t variant, uint32_t geometry_template = 0);

// A behavioral model for simulation and LEC, emitted into a separate model
// library and never into the netlist: the same IO names and ids as the netlist
// declaration (usyn::declare_physical_cell), and one
// Flop(din = formula over the rail pins, clock_pin = CLK) driving Q, so a
// single-flop splice names the state after the instance. No initial value (v1
// converts no initialized state). No-op when the model already exists.
void emit_domino_model(hhds::GraphLibrary& models, const Domino_master& master, const usyn::Physical_cell_interface& cell);

// The next-state function as a Verilog expression over the rail pins, in the
// formula's series (&) / parallel (|) structure.
std::string domino_expression(const Domino_master& master, const usyn::Physical_cell_interface& cell);

// The same model as a Verilog module, for external simulators.
std::string domino_verilog_model(const Domino_master& master, const usyn::Physical_cell_interface& cell);

}  // namespace livehd::domplace
