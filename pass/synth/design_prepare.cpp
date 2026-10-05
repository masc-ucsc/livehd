// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "design_prepare.hpp"

#include <optional>
#include <unordered_set>

#include "diag.hpp"
#include "node_util.hpp"
#include "pass_partition.hpp"

namespace livehd::synth {

namespace {

namespace gu = livehd::graph_util;

// A plain `Clock_cell` (div 1, no invert) is a clock gate whose enable is
// sampled at the reference clock's active edge -- exactly what an integrated
// clock-gate cell does with its transparent-low enable latch. The mappers
// only recognize (and map onto a Liberty ICG) the structural spelling
// `clk & latch(en, transparent while clk low)`, so a Clock_cell -- what a
// Pyrope `Clock(clock_pin=, enable=)` lowers to -- reached the netlist as an
// unmapped native node and the STA refused it. Spell it structurally, on the
// private copies only. Divided/inverted flavours stay native (no ICG maps them).
std::optional<uint64_t> expand_plain_clock_cells(hhds::Graph* g, Preparation_budget* budget) {
  std::vector<hhds::Node_class> cells;
  for (auto n : g->body().nodes()) {
    if (!admit_preparation(budget, "clock-cell-scan")) {
      return {};
    }
    if (gu::type_op_of(n) == Ntype_op::Clock_cell) {
      cells.push_back(n);
    }
  }
  uint64_t done = 0;
  for (auto n : cells) {
    if (!admit_preparation(budget, "clock-cell-expand", 16)) {
      return {};
    }
    const auto clk = gu::get_driver_of_sink_name(n, "clk_ref");
    const auto en  = gu::get_driver_of_sink_name(n, "en");
    const auto div = gu::get_driver_of_sink_name(n, "div");
    const auto inv = gu::get_driver_of_sink_name(n, "invert");
    if (clk.is_invalid() || clk.is_const()) {
      continue;
    }
    if (!div.is_invalid() && !(div.is_const() && gu::const_of(div).is_just_i64() && gu::const_of(div).to_just_i64() == 1)) {
      continue;
    }
    if (!inv.is_invalid() && !inv.is_known_false()) {
      continue;
    }
    const auto out = n.get_driver_pin(0);
    if (out.is_invalid()) {
      continue;
    }
    hhds::Pin_class gclk = clk;  // an unset enable is always-on
    if (!en.is_invalid()) {
      auto latch = gu::create_typed_node(*g, Ntype_op::Latch);
      clk.connect_sink(gu::setup_sink_by_name(latch, "enable"));
      en.connect_sink(gu::setup_sink_by_name(latch, "din"));
      gu::create_const(*g, *Dlop::create_integer(0)).connect_sink(gu::setup_sink_by_name(latch, "posclk"));
      auto q = latch.create_driver_pin(0);
      gu::set_bits(q, 1);
      gu::set_unsign(q);
      auto gate = gu::create_typed_node(*g, Ntype_op::And);
      clk.connect_sink(gate.create_sink_pin(0));
      q.connect_sink(gate.create_sink_pin(1));
      gclk = gate.create_driver_pin(0);
      gu::set_bits(gclk, 1);
      gu::set_unsign(gclk);
    }
    // SNAPSHOT the fan-out: connect_sink mutates the storage out_edges walks.
    std::vector<hhds::Pin_class> readers;
    for (auto e : out.out_edges()) {
      if (!admit_preparation(budget, "clock-cell-reader")) {
        return {};
      }
      readers.push_back(e.sink);
    }
    for (const auto& reader : readers) {
      if (!admit_preparation(budget, "clock-cell-rewire")) {
        return {};
      }
      gclk.connect_sink(reader);
    }
    n.del_node();
    ++done;
  }
  return done;
}

}  // namespace

std::unique_ptr<Prepared_design> prepare_design(std::span<const std::shared_ptr<hhds::Graph>> sources, bool unroll_carry,
                                                std::string_view from_pass, Preparation_budget* budget) {
  if (!admit_preparation(budget, "begin", 0)) {
    return {};
  }
  auto result = std::make_unique<Prepared_design>();
  for (const auto& source : sources) {
    if (!source) {
      continue;
    }
    auto  io      = source->get_io();
    auto* library = io ? io->get_library() : nullptr;
    if (!library) {
      diag::err(from_pass, "scratch-copy", "internal")
          .msg("could not copy '{}' into the private synthesis library", source->get_name())
          .emit();
      return {};
    }
    if (!admit_preparation(budget, "definitions", 0)) {
      return {};
    }
    // copy_from is definition-local; a copied instance resolves only through
    // the destination library. Include callees absent from the input list.
    for (const auto& graph : source->definitions().graphs()) {
      if (result->library.find_io(graph->get_name())) {
        continue;
      }
      if (budget) {
        // Admit the complete source shape before copying its body. Count ports,
        // pins and edges as work too: a small node count need not mean a small
        // graph, especially for opaque interfaces and wide multi-input nodes.
        const auto graph_io = graph->get_io();
        for (const auto& port : graph_io->get_input_pin_decls()) {
          (void)port;
          if (!budget->check("source-port")) {
            return {};
          }
        }
        for (const auto& port : graph_io->get_output_pin_decls()) {
          (void)port;
          if (!budget->check("source-port")) {
            return {};
          }
        }
        const auto admit_pins = [&](const hhds::Node_class& node) {
          for (const auto& pin : node.inp_sorted_pins()) {
            if (!budget->check("source-pin")) {
              return false;
            }
            for (const auto& driver : pin.get_driver_pins()) {
              (void)driver;
              if (!budget->check("source-edge")) {
                return false;
              }
            }
          }
          for (const auto& pin : node.out_sorted_pins()) {
            (void)pin;
            if (!budget->check("source-pin")) {
              return false;
            }
          }
          return true;
        };
        // body().nodes() excludes the primary IO nodes. Include their pins
        // explicitly so output drivers and direct feedthrough edges are charged.
        if (!admit_pins(graph->get_input_node()) || !admit_pins(graph->get_output_node())) {
          return {};
        }
        for (const auto node : graph->body().nodes()) {
          if (!budget->source_node() || !admit_pins(node)) {
            return {};
          }
        }
      }
      if (!admit_preparation(budget, "copy", 0)) {
        return {};
      }
      if (!result->library.copy_from(*library, graph->get_name())) {
        diag::err(from_pass, "scratch-copy", "internal")
            .msg("could not copy '{}' into the private synthesis library", graph->get_name())
            .emit();
        return {};
      }
      if (!admit_preparation(budget, "copied", 0)) {
        return {};
      }
      for (const auto node : graph->body().nodes()) {
        if (!admit_preparation(budget, "opaque-scan")) {
          return {};
        }
        if (graph_util::type_op_of(node) == Ntype_op::Sub && node.get_subnode_io() && !node.get_subnode_graph()
            && !result->library.find_io(node.get_subnode_io()->get_name())) {
          if (budget) {
            for (const auto& port : node.get_subnode_io()->get_input_pin_decls()) {
              (void)port;
              if (!budget->check("opaque-port")) {
                return {};
              }
            }
            for (const auto& port : node.get_subnode_io()->get_output_pin_decls()) {
              (void)port;
              if (!budget->check("opaque-port")) {
                return {};
              }
            }
          }
          if (!admit_preparation(budget, "opaque-copy", 0)) {
            return {};
          }
          partition::resolve_or_clone_subdef(&result->library, node);
          if (!admit_preparation(budget, "opaque-copied", 0)) {
            return {};
          }
        }
      }
    }
  }
  for (const auto& source : sources) {
    if (!admit_preparation(budget, "roots")) {
      return {};
    }
    auto io = source ? result->library.find_io(source->get_name()) : std::shared_ptr<hhds::GraphIO>{};
    result->roots.push_back(io ? io->get_graph() : std::shared_ptr<hhds::Graph>{});
  }
  for (const auto gid : result->library.all_gids()) {
    if (!admit_preparation(budget, "copied-definitions")) {
      return {};
    }
    if (auto graph = result->library.get_graph(gid)) {
      result->definitions.push_back(std::move(graph));
    }
  }
  if (!prepare_loop_bodies(result->definitions, unroll_carry, result->loops, from_pass, budget)) {
    return {};
  }
  result->definitions.insert(result->definitions.end(), result->loops.shared_bodies.begin(), result->loops.shared_bodies.end());
  uint64_t clock_cells = 0;
  for (const auto& graph : result->definitions) {
    const auto expanded = expand_plain_clock_cells(graph.get(), budget);
    if (!expanded) {
      return {};
    }
    clock_cells += *expanded;
  }
  if (clock_cells > 0) {
    livehd::diag::info(from_pass, "clock-cell-expanded", "progress")
        .msg("{}: spelled {} clock gate(s) as `clk & latch` for ICG mapping", from_pass, clock_cells)
        .emit();
  }
  result->resolve_graphs = result->roots;
  std::unordered_set<hhds::Gid> listed;
  for (const auto& graph : result->roots) {
    if (graph) {
      listed.insert(graph->get_gid());
    }
  }
  for (const auto& graph : result->definitions) {
    if (!admit_preparation(budget, "resolve")) {
      return {};
    }
    if (listed.insert(graph->get_gid()).second) {
      result->resolve_graphs.push_back(graph);
    }
  }
  if (!admit_preparation(budget, "complete", 0)) {
    return {};
  }
  return result;
}

}  // namespace livehd::synth
