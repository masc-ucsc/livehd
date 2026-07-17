// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "color_synth.hpp"

#include <print>

#include "iassert.hpp"

namespace livehd::color {

using livehd::graph_util::bits_of;
using livehd::graph_util::type_op_of;

Color_synth::Color_synth(Color_opts opts_, std::string_view alg) : opts(opts_) {
  // The driver (Pass_color::color) validates the name, so anything reaching
  // here is one of the two.
  synth = alg != "pipe";
}

void Color_synth::set_id(const hhds::Node_class& node, int id) {
  auto [it, inserted] = flat_node2id.insert({node, id});
  if (inserted || it->second == id) {
    return;
  }
  uf.merge(it->second, id);  // two cones meet at this node => one region
}

void Color_synth::force_id(const hhds::Node_class& node, int id) {
  flat_node2id[node] = id;  // a cut owns its id outright -- never a merge
}

// Driver-pin bits for a node, read off its out-edges. Not out_pins(): that is
// Graph::get_driver_pins, which walks the pin linked list only and so misses a
// node-as-pin (port 0) driver -- the common shape -- reporting 0 for a node
// that plainly has a width.
//
// 0 means "width unknown here": either the node drives nothing (dead logic,
// whose region does not matter) or pass.color ran before pass.bitwidth. An
// unknown width simply is not a boundary, which makes the region larger, never
// wrong.
static int driver_bits(const hhds::Node_class& n) {
  for (const auto& e : n.out_edges()) {
    if (auto b = bits_of(e.driver); b != 0) {
      return b;
    }
  }
  return 0;
}

bool Color_synth::is_cut(const hhds::Node_class& node) const {
  if (node.is_loop_break()) {
    return true;  // flop/mem/latch/stateful sub: the pipeline-stage boundary
  }
  if (!synth) {
    return false;  // "pipe": stages are cut at state only
  }
  auto op = type_op_of(node);
  if (op == Ntype_op::Mult || op == Ntype_op::Div) {
    return true;
  }
  // A wide adder is its own synthesis boundary. driver_bits is 0 when bitwidth
  // has not run, and an unknown width is not a boundary -- the region is merely
  // larger, never wrong.
  return op == Ntype_op::Sum && driver_bits(node) > 8;
}

// The id a cut node joins: the region of the data it registers, if it has a
// single `din` whose driver is already in a (non-cut) region. Anything else --
// a memory with several write ports, a sub-instance, a flop driven straight by
// a primary input or by another flop -- opens a fresh region instead.
// Deliberately ONLY `din`: enable/reset/clock are read but never joined, since
// joining more than one fan-in is exactly the weld this design removes.
int Color_synth::data_cone_id(const hhds::Node_class& node) {
  if (auto op = type_op_of(node); op == Ntype_op::Flop || op == Ntype_op::Latch || op == Ntype_op::Fflop) {
    auto din = graph_util::get_driver_of_sink_name(node, "din");
    if (!din.is_invalid()) {
      auto drv = din.get_master_node();
      if (is_partitionable(drv) && !is_cut(drv)) {
        if (auto it = flat_node2id.find(drv); it != flat_node2id.end()) {
          return it->second;
        }
      }
    }
  }
  return get_free_id();
}

void Color_synth::mark_ids(hhds::Graph* g) {
  // Pass 1 -- the combinational regions. Cuts take no part: they neither
  // receive an id here nor hand one out, which is what keeps a register from
  // welding its din cone to its enable/stall cone, and its fan-out cones to
  // each other.
  for (auto node : g->forward_class()) {
    if (!is_partitionable(node) || is_cut(node)) {
      continue;
    }

    auto      it = flat_node2id.find(node);
    const int id = it == flat_node2id.end() ? get_free_id() : it->second;
    set_id(node, id);  // the node records its OWN id, not just its sinks'

    for (const auto& e : node.out_edges()) {
      auto snode = e.sink.get_master_node();
      if (!is_partitionable(snode) || is_cut(snode)) {
        continue;  // a cut owns its region; it never inherits this one
      }
      set_id(snode, id);
    }
  }

  // Pass 2 -- attach each cut to the region of the data it registers. This MUST
  // come after pass 1: forward_class emits loop breaks first (they are the cut
  // points that make the walk acyclic), so during pass 1 a flop's din cone has
  // no id yet and every cut would fall back to a region of its own.
  for (auto node : g->forward_class()) {
    if (is_partitionable(node) && is_cut(node)) {
      force_id(node, data_cone_id(node));
    }
  }
}

void Color_synth::merge_ids() {
  for (auto& it : flat_node2id) {
    it.second = uf.find(it.second);
  }
}

void Color_synth::label(hhds::Graph* g) {
  last_free_id = 1;
  flat_node2id.clear();
  uf = Int_union_find{};

  mark_ids(g);
  merge_ids();

  int n_colors = apply_coloring(g, flat_node2id, opts, opts.sizes);
  if (opts.verbose) {
    std::print(stderr, "[color.synth] {} -> {} clusters\n", g->get_name(), n_colors);
  }
}

}  // namespace livehd::color
