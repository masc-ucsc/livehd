// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "domino_cell.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>

#include "cell.hpp"
#include "dlop.hpp"
#include "hash_util.hpp"
#include "liberty_dff.hpp"
#include "node_util.hpp"
#include "provisional_cell.hpp"

namespace livehd::domplace {
namespace {
namespace gu = graph_util;
using Kind   = usyn::Gate_formula::Kind;

// The pin of a literal: its (variable, polarity) rail.
size_t rail_of(const Domino_master& master, const usyn::Gate_formula::Node& node) {
  const auto it = std::find(master.rails.begin(), master.rails.end(), usyn::Gate_rail{node.variable, node.inverted});
  if (it == master.rails.end()) {
    throw std::invalid_argument("domino formula literal has no rail: " + master.base);
  }
  return static_cast<size_t>(it - master.rails.begin());
}

void check_interface(const Domino_master& master, const usyn::Physical_cell_interface& cell) {
  if (!cell.name.starts_with(master.base) || cell.rails.size() != master.rails.size()) {
    throw std::invalid_argument("domino cell interface does not match its master: " + cell.name);
  }
}
}  // namespace

std::optional<Domino_master> make_domino_master(const usyn::Gate_formula& formula, uint32_t inputs, usyn::Budget& work) {
  const auto metrics = formula.metrics();
  if (!metrics || formula.output_inverted || inputs == 0 || inputs > usyn::max_logical_inputs
      || metrics->support != (1U << inputs) - 1) {
    throw std::invalid_argument("a domino master needs a valid positive-output formula that uses every input");
  }
  auto function = formula.evaluate_table(inputs, work);
  if (!function) {
    return std::nullopt;  // a valid formula fails only for lack of work
  }
  Domino_master master;
  master.function = std::move(*function);
  master.formula  = formula;
  master.rails    = usyn::gate_rails(*metrics, inputs);
  // Canonical text: only the fields each node kind defines.
  auto key        = std::format("domino-model-v{}|inputs={}|formula", kDominoModelVersion, inputs);
  for (const auto& node : formula.nodes) {
    switch (node.kind) {
      case Kind::constant: key += std::format(":c{}", node.inverted ? 1 : 0); break;
      case Kind::literal : key += std::format(":l{}{}", node.variable, node.inverted ? '-' : '+'); break;
      case Kind::series  : key += std::format(":s{},{}", node.left, node.right); break;
      case Kind::parallel: key += std::format(":p{},{}", node.left, node.right); break;
    }
  }
  key += "|rails";
  for (const auto& rail : master.rails) {
    key += std::format(":{}{}", rail.input, rail.negative ? '-' : '+');
  }
  key += "|table";
  for (const auto word : master.function.words) {
    key += std::format(":{:016x}", word);
  }
  master.identity = hash_util::fnv1a64(key);
  master.base     = std::format("{}{:016x}", kProvisionalDominoPrefix, master.identity);
  return master;
}

usyn::Physical_cell_interface domino_interface(const Domino_master& master, size_t variant, uint32_t geometry_template) {
  usyn::Physical_cell_interface cell;
  cell.name = std::format("{}_v{}_t{}", master.base, variant, geometry_template);
  for (size_t k = 0; k < master.rails.size(); ++k) {
    cell.rails.push_back(std::format("R{}", k));
  }
  return cell;
}

std::shared_ptr<hhds::GraphIO> declare_domino_cell(hhds::GraphLibrary& library, const usyn::Physical_cell_interface& cell) {
  // The named-cell declaration path: sequential port ids, 1-bit unsigned pins,
  // the clocked output as a loop break, and the bodied-collision refusal.
  liberty::Memory_cell decl;
  decl.name = cell.name;
  for (const auto& rail : cell.rails) {
    decl.pins.push_back({.name = rail, .direction = "input"});
  }
  decl.pins.push_back({.name = cell.clock, .direction = "input", .clock_input = true});
  decl.pins.push_back({.name = cell.output, .direction = "output", .clocked = true});
  liberty::declare_memory_cells(library, {decl});
  return library.find_io(cell.name);
}

void emit_domino_model(hhds::GraphLibrary& models, const Domino_master& master, const usyn::Physical_cell_interface& cell) {
  check_interface(master, cell);
  if (const auto existing = models.find_io(cell.name); existing && existing->has_graph()) {
    return;
  }
  auto       io      = declare_domino_cell(models, cell);
  auto       body    = io->create_graph();
  const auto one_bit = [](hhds::Pin_class pin) {
    gu::set_ubits(pin, 1);
    return pin;
  };
  std::vector<hhds::Pin_class> values;
  for (const auto& node : master.formula.nodes) {
    if (node.kind == Kind::constant) {
      values.push_back(gu::create_const(*body, *Dlop::create_integer(node.inverted ? 1 : 0)));
    } else if (node.kind == Kind::literal) {
      values.push_back(body->get_input_pin(cell.rails[rail_of(master, node)]));
    } else {
      auto gate = gu::create_typed_node(*body, node.kind == Kind::series ? Ntype_op::And : Ntype_op::Or);
      values[node.left].connect_sink(gu::setup_sink_pid(gate, 0));
      values[node.right].connect_sink(gu::setup_sink_pid(gate, 0));
      values.push_back(one_bit(gate.create_driver_pin(0)));
    }
  }
  auto flop = gu::create_typed_node(*body, Ntype_op::Flop);
  values.back().connect_sink(gu::setup_sink_by_name(flop, "din"));
  body->get_input_pin(cell.clock).connect_sink(gu::setup_sink_by_name(flop, "clock_pin"));
  one_bit(flop.create_driver_pin(0)).connect_sink(body->get_output_pin(cell.output));
  body->commit();
}

std::string domino_expression(const Domino_master& master, const usyn::Physical_cell_interface& cell) {
  check_interface(master, cell);
  std::vector<std::string> values;
  for (const auto& node : master.formula.nodes) {
    switch (node.kind) {
      case Kind::constant: values.push_back(node.inverted ? "1'b1" : "1'b0"); break;
      case Kind::literal : values.push_back(cell.rails[rail_of(master, node)]); break;
      case Kind::series  : values.push_back(std::format("({} & {})", values[node.left], values[node.right])); break;
      case Kind::parallel: values.push_back(std::format("({} | {})", values[node.left], values[node.right])); break;
    }
  }
  return values.back();
}

std::string domino_verilog_model(const Domino_master& master, const usyn::Physical_cell_interface& cell) {
  const auto  next = domino_expression(master, cell);
  std::string ports, inputs;
  for (const auto& rail : cell.rails) {
    ports  += rail + ", ";
    inputs += std::format("  input {};\n", rail);
  }
  return std::format(
      "// Provisional domino master {} (model v{}): cycle-level next state only.\n"
      "module {}({}{}, {});\n{}  input {};\n  output reg {};\n  always @(posedge {}) {} <= {};\nendmodule\n",
      master.base,
      kDominoModelVersion,
      cell.name,
      ports,
      cell.clock,
      cell.output,
      inputs,
      cell.clock,
      cell.output,
      cell.clock,
      cell.output,
      next);
}

}  // namespace livehd::domplace
