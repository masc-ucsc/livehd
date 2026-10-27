// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "design_prepare.hpp"

#include <optional>
#include <string_view>
#include <unordered_set>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "clock_gate.hpp"  // //graph
#include "diag.hpp"
#include "node_util.hpp"
#include "pass_partition.hpp"
#include "reassoc.hpp"
#include "specialize.hpp"
#include "sweep.hpp"  // //pass/sweep
#include "synth_policy.hpp"

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
                                                std::string_view from_pass, Preparation_budget* budget, bool specialize,
                                                Loop_reassoc reassociate_loops, const State_options& state) {
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
  // pass.specialize: constants an instance is tied to (and constants a callee
  // returns) cross the instance boundary in the PRIVATE copy, after pass.color
  // decided the regions -- the same specialization the simulator runs on its
  // private library. Only STATE-FREE callees: a stateful definition keeps its
  // identity (register names, DFF picks, clock-gate mapping, LEC pairing).
  // The nodes it creates (only those) inherit a neighbor's color, so no stray
  // color-0 region appears; nothing else is refolded or recolored, so a region
  // no specialization touched maps exactly as before.
  if (const int specialized
      = specialize ? specialize::specialize_constants(result->definitions, {.inherit_colors = true, .state_free_only = true}) : 0;
      specialized > 0) {
    livehd::diag::info(from_pass, "constant-specialized", "progress")
        .msg("{}: {} constant-specialized instance definition(s)", from_pass, specialized)
        .emit();
  }
  // synth.flat_sweep: the constant registers whose proof needs the whole flat design (router rings: each is constant only
  // if its neighbors are). Flattens the PRIVATE copy only, so the compile cache and its incremental reuse are untouched.
  if (state.flat_sweep) {
    std::vector<std::shared_ptr<hhds::Graph>> roots;
    for (const auto& graph : result->roots) {
      if (graph && (state.top.empty() || graph->get_name() == state.top)) {
        roots.push_back(graph);
      }
    }
    if (!roots.empty()) {
      const auto flat = sweep::flatten(roots);
      for (const auto& root : roots) {
        sweep::refine_unknown(*root);
        specialize::refold_private_body(root, specialize::Refold_cost::synth, "synth.flat_sweep");
        for (int round = 0; round < 16; ++round) {
          const auto stats = sweep::sweep_state(*root, {.observe_markers = false});
          if (!stats.changed()) {
            break;
          }
          specialize::refold_private_body(root, specialize::Refold_cost::synth, "synth.flat_sweep");
        }
      }
      livehd::diag::info(from_pass, "flat-swept", "progress")
          .msg("{}: flat sweep inlined {} instance(s) into {} root(s)", from_pass, flat.inlined, roots.size())
          .emit();
    }
  }
  // synth.state_sweep: registers (and the cells behind them) that no output,
  // instance of a body-less cell or memory can ever observe, and the unread
  // upper bits of the rest. Before the loops are prepared: a compact loop and its
  // body are left alone, so the sweep never sees an expansion half done.
  if (state.merge_registers) {
    const auto merged = merge_equal_registers(result->definitions, budget);
    if (!merged) {
      return {};
    }
    result->merged = *merged;
    if (merged->registers > 0) {
      livehd::diag::info(from_pass, "registers-merged", "progress")
          .msg("{}: merged {} duplicate register(s) ({} bit(s)) in {} round(s)", from_pass, merged->registers, merged->bits,
               merged->rounds)
          .emit();
    }
  }
  if (state.state_sweep) {
    const auto swept = sweep_unobserved_state(result->definitions, state.top, budget);
    if (!swept) {
      return {};
    }
    result->sweep = *swept;
    if (swept->registers_constant + swept->registers_removed + swept->registers_trimmed + swept->registers_split + swept->cells_removed > 0) {
      livehd::diag::info(from_pass, "state-swept", "progress")
          .msg("{}: state sweep folded {} constant register(s) ({} bit(s)), removed {} unobserved register(s) ({} bit(s)) and {} "
               "cell(s), narrowed {} and split {} register(s), dropping {} unobserved register bit(s)",
               from_pass,
               swept->registers_constant,
               swept->constant_bits,
               swept->registers_removed,
               swept->register_bits,
               swept->cells_removed,
               swept->registers_trimmed,
               swept->registers_split,
               swept->trimmed_bits)
          .emit();
    }
  }
  // The cells each definition holds before its carried loops are expanded:
  // reassociation works only on what the expansion mints (reassoc.hpp). Only
  // a definition with a compact loop can gain any.
  absl::flat_hash_map<hhds::Gid, absl::flat_hash_set<hhds::Class_index>> before_loops;
  if (unroll_carry && reassociate_loops != Loop_reassoc::none) {
    for (const auto& graph : result->definitions) {
      absl::flat_hash_set<hhds::Class_index> cells;
      bool                                   loops = false;
      for (const auto node : graph->body().nodes()) {
        if (!admit_preparation(budget, "loop-snapshot")) {
          return {};
        }
        loops = loops || node.is_loop_subnode();
        cells.insert(node.get_class_index());
      }
      if (loops) {
        before_loops.emplace(graph->get_gid(), std::move(cells));
      }
    }
  }
  if (!prepare_loop_bodies(result->definitions, unroll_carry, result->loops, from_pass, budget)) {
    return {};
  }
  result->definitions.insert(result->definitions.end(), result->loops.shared_bodies.begin(), result->loops.shared_bodies.end());
  for (const auto& graph : result->definitions) {
    auto a = graph->get_input_node().attr(livehd::attrs::synth_policy);
    if (a.has() && livehd::synth_attr::read(a.get()).contains("_reuse_definition")) {
      result->loops.preserved_defs.insert(graph->get_gid());
    }
  }
  // Expanded carried loops leave one link per iteration (`acc |= term` is a
  // left-deep Or chain) in colored cells, which cprop's flattening skips.
  // Merge them into n-ary cells per region so the blaster lowers a balanced
  // tree: only the cells the expansion minted (in an expanded definition, or
  // in a shared body it extracted). Before the clock-cell spelling below: an
  // ICG's `clk & latch` keeps its two-input AND (reassoc.hpp).
  if (reassociate_loops != Loop_reassoc::none && result->loops.expanded > 0) {
    const auto trees = reassociate_loops == Loop_reassoc::guarded ? Reassoc_trees::guarded : Reassoc_trees::all;
    absl::flat_hash_set<hhds::Gid> extracted;
    for (const auto& body : result->loops.shared_bodies) {
      extracted.insert(body->get_gid());
    }
    for (const auto& graph : result->definitions) {
      const auto it = before_loops.find(graph->get_gid());
      if (it == before_loops.end() && !extracted.contains(graph->get_gid())) {
        continue;  // no expansion touched this definition
      }
      const auto* existed = it == before_loops.end() ? nullptr : &it->second;
      if (!reassociate_chains(
              graph.get(),
              result->reassoc,
              budget,
              [existed](const hhds::Node_class& node) { return existed == nullptr || !existed->contains(node.get_class_index()); },
              trees)) {
        return {};
      }
    }
  }
  uint64_t clock_cells = 0;
  // synth.clock_gate: tidy the existing gates and factor the enables registers share into new ones, before the Clock_cells are
  // spelled structurally below. After the carried loops are prepared: registers a loop expansion mints share enables too.
  if (state.clock_gate) {
    const auto gated = clock_gate::infer_clock_gates(
        result->definitions,
        {.min_bits = state.clock_gate_min_bits, .depth = state.clock_gate_depth},
        [budget](std::string_view stage, uint64_t work) { return admit_preparation(budget, stage, work); });
    if (!gated) {
      return {};
    }
    result->gating = *gated;
    if (gated->gates + gated->collapsed + gated->duplicates + gated->redundant > 0) {
      livehd::diag::info(from_pass, "clock-gated", "progress")
          .msg("{}: clock gating inferred {} gate(s) over {} register(s) ({} bit(s); {} enable(s) absorbed), collapsed {} gate(s), merged "
               "{} duplicate gate(s), dropped {} implied enable conjunct(s)",
               from_pass,
               gated->gates,
               gated->registers,
               gated->bits,
               gated->enables,
               gated->collapsed,
               gated->duplicates,
               gated->redundant)
          .emit();
    }
  }
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
