// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "design_prepare.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <unordered_set>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "diag.hpp"
#include "node_util.hpp"
#include "pass.hpp"
#include "pass_partition.hpp"
#include "rapidjson/document.h"
#include "reassoc.hpp"
#include "specialize.hpp"
#include "synth_policy.hpp"

namespace livehd::synth {

namespace {

namespace gu = livehd::graph_util;

// EXPERIMENT, opt-in (LIVEHD_SYNTH_RECOLOR=1; no cache key covers it, so use
// a fresh workdir). pass.color decided the regions on the SOURCE, where a
// compact loop is one opaque instance: every iteration of an expanded carried
// loop takes that instance's single color (loop_cleanup.cpp). The loop-free
// spelling of the same logic is split by that coloring into data and control
// cones (the abc profile's stop_mux/ctrl_cones), so the rolled source maps as
// one region (lhdtrack br_mux_bin_array 264 vs 214 ps,
// br_apb_demux_select_onehot 170 vs 127 with ABC). Once the loops are
// expanded the private copy IS the design written out, so this re-runs the
// coloring the top's coloring_info records on it. Only a plain `synth`
// coloring is reproduced; source-seeded block regions and per-region options
// keep their colors, and an uncolored source stays uncolored. It closes those
// gaps but loses as often elsewhere (br_arb_lru 273 -> 368 ps), as
// `compile.unroll=true` does, which is why it is not the default.
void recolor_expanded_loops(Prepared_design& design, std::string_view from_pass) {
  const char* env = std::getenv("LIVEHD_SYNTH_RECOLOR");
  if (env == nullptr || std::string_view{env} != "1" || design.loops.expanded == 0 || !Pass::eprp.get_method("pass.color")) {
    return;
  }
  // The recorded params that are pass.color labels (params_json writes them);
  // anything else in the record (hier_flat, packed) describes the result.
  static constexpr std::array<std::string_view, 24> kLabels{
      "hier",          "continuous", "keep_colored", "ware_arith", "ware_cmp",        "ware_shift",
      "mode",          "min_ge",     "max_ge",       "name_weight", "mapper",         "stop_arith",
      "stop_cmp",      "stop_shift", "ctrl_cones",   "ctrl_max_gate", "ctrl_min_gate", "max_gate",
      "flop_to_flop",  "forward",    "min_color_nodes", "min_gate", "boundary_wiring", "stop_mux"};
  for (const auto& root : design.roots) {
    if (!root) {
      continue;
    }
    const auto info = root->get_input_node().attr(livehd::attrs::coloring_info);
    if (!info.has()) {
      continue;
    }
    const std::string   json{info.get()};
    rapidjson::Document doc;
    doc.Parse(json.data(), json.size());
    if (doc.HasParseError() || !doc.IsObject()) {
      continue;
    }
    const auto alg = doc.FindMember("algorithm");
    const auto par = doc.FindMember("params");
    if (alg == doc.MemberEnd() || !alg->value.IsString() || std::string_view{alg->value.GetString()} != "synth"
        || par == doc.MemberEnd() || !par->value.IsObject() || doc.HasMember("region_opts")
        || (doc.HasMember("seeded") && doc["seeded"].IsBool() && doc["seeded"].GetBool())) {
      continue;
    }
    Eprp_var::Eprp_dict labels{
        {"alg",                    "synth"},
        {"top", std::string{root->get_name()}},
    };
    for (const auto& member : par->value.GetObject()) {
      const std::string_view key{member.name.GetString(), member.name.GetStringLength()};
      if (std::ranges::find(kLabels, key) == kLabels.end()) {
        continue;
      }
      const auto& v = member.value;
      if (v.IsBool()) {
        labels[std::string{key}] = v.GetBool() ? "true" : "false";
      } else if (v.IsString()) {
        labels[std::string{key}] = std::string{v.GetString(), v.GetStringLength()};
      } else if (v.IsUint64()) {
        labels[std::string{key}] = std::to_string(v.GetUint64());
      } else if (v.IsInt64()) {
        labels[std::string{key}] = std::to_string(v.GetInt64());
      }
    }
    // Only the definitions this top reaches, as the source run saw them: the
    // copy also holds definitions specialization left unreachable (a callee
    // whose every instance now points at its constant-specialized clone), and
    // pass.color counts every Sub it sees -- a child of both the original and
    // its clone would look like a shared implementation and keep its module
    // boundary as a region boundary.
    absl::flat_hash_set<hhds::Gid> reached{root->get_gid()};
    std::vector<hhds::Graph*>      todo{root.get()};
    Eprp_var                       var;
    var.add(root);
    while (!todo.empty()) {
      auto* graph = todo.back();
      todo.pop_back();
      for (const auto node : graph->body().nodes()) {
        if (gu::type_op_of(node) != Ntype_op::Sub) {
          continue;
        }
        if (auto child = node.get_subnode_graph(); child && reached.insert(child->get_gid()).second) {
          todo.push_back(child.get());
          var.add(child);
        }
      }
    }
    Pass::eprp.run_method_now("pass.color", var, labels);
    livehd::diag::info(from_pass, "loop-recolored", "progress")
        .msg("{}: recolored '{}' after expanding {} carried loop(s), as pass.color colors the written-out loop", from_pass,
             root->get_name(), design.loops.expanded)
        .emit();
  }
}

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
                                                bool reassociate_loops) {
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
  // The cells each definition holds before its carried loops are expanded:
  // reassociation works only on what the expansion mints (reassoc.hpp). Only
  // a definition with a compact loop can gain any.
  absl::flat_hash_map<hhds::Gid, absl::flat_hash_set<hhds::Class_index>> before_loops;
  if (unroll_carry && reassociate_loops) {
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
  recolor_expanded_loops(*result, from_pass);
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
  if (const char* env = std::getenv("LIVEHD_SYNTH_REASSOC");
      reassociate_loops && result->loops.expanded > 0 && (env == nullptr || std::string_view{env} != "0")) {
    Reassoc_stats reassoc;
    for (const auto& graph : result->definitions) {
      const auto it     = before_loops.find(graph->get_gid());
      const bool minted = std::ranges::any_of(result->loops.shared_bodies, [&](const auto& b) { return b == graph; });
      if (it == before_loops.end() && !minted) {
        continue;  // no expansion touched this definition
      }
      const auto* existed = it == before_loops.end() ? nullptr : &it->second;
      if (!reassociate_chains(graph.get(), reassoc, budget, [existed](const hhds::Node_class& node) {
            return existed == nullptr || !existed->contains(node.get_class_index());
          })) {
        return {};
      }
    }
    if (std::getenv("LIVEHD_SYNTH_REASSOC_DEBUG") != nullptr) {
      std::print("{} reassoc: {} guarded link(s); {} associative tree(s) absorbed {} link(s)\n", from_pass, reassoc.guarded,
                 reassoc.roots, reassoc.absorbed);
    }
  }
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
