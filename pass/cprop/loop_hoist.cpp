// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "loop_hoist.hpp"

#include <cstdlib>
#include <print>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "attr_carry.hpp"
#include "cell.hpp"
#include "debug_env.hpp"
#include "node_util.hpp"

namespace livehd::cprop {
namespace {
namespace gu = livehd::graph_util;

struct Instance {
  hhds::Graph*     parent;
  hhds::Node_class node;
};

struct Body {
  std::shared_ptr<hhds::Graph> graph;
  std::vector<Instance>        instances;
  bool                         plain_call = false;  // also an ordinary Sub somewhere: every input may vary there
};

// A computation the body may lose: a single-output combinational cell with no
// synthesis color (a colored node is a placement decision this does not move).
// Clock_cell sits in the combinational band but gates a clock; it stays.
bool hoistable_op(const hhds::Node_class& n) {
  const auto op = gu::type_op_of(n);
  return Ntype::is_comb(op) && !Ntype::has_multiple_driver_pins(op) && op != Ntype_op::Clock_cell && !gu::has_color(n);
}

// Pure wiring: a slice, a widening, a pack or a shift mints no gate in
// synthesis and costs the simulator no more than reading the port that would
// replace it. A cone made only of these gains nothing from moving, while the
// new port it would need costs every instance a bound, change-tested value.
bool wiring_op(Ntype_op op) {
  return op == Ntype_op::Get_mask || op == Ntype_op::Set_mask || op == Ntype_op::Sext || op == Ntype_op::Concat
         || op == Ntype_op::SHL || op == Ntype_op::SRA;
}

class Hoister {
public:
  Hoister(Body& body, Loop_hoist_stats& stats, bool debug) : body_(body), stats_(stats), debug_(debug) {}

  bool run();

private:
  // The body inputs that change between iterations, over every instance: the
  // ordinal, the activation and each carry-in, plus any port the front end
  // flagged as a loop break. Every other input is bound once per evaluation.
  void collect_variant_ports();
  bool variant_port(hhds::Port_id pid) const { return variant_ports_.contains(static_cast<uint32_t>(pid)); }

  // Memoized: does `n` compute the same value in every iteration?
  bool invariant(const hhds::Node_class& n);

  // The invariant body inputs a cone reads, and whether every instance drives
  // them (an unconnected invariant input has no parent-side value to clone).
  bool cone_inputs_bound(const hhds::Node_class& root, absl::flat_hash_set<uint32_t>& ports);

  // Does the invariant cone under `root` hold at least one computing cell?
  bool cone_computes(const hhds::Node_class& root);
  // `root`'s value reaches, through the body's combinational cells, a memory address sink.
  bool reaches_memory_address(const hhds::Node_class& root);
  absl::flat_hash_set<hhds::Class_index> addr_cone_;  // reaches_memory_address: built once per body
  bool                                   addr_cone_built_ = false;

  // The cone of `pin` rebuilt in an instance's parent, fed from what the parent
  // drives into the instance's invariant inputs. `memo` is per instance and
  // shared by every hoisted value of this body, so a node two cones share is
  // cloned once.
  hhds::Pin_class clone(const hhds::Pin_class& pin, const Instance& inst,
                        absl::flat_hash_map<hhds::Node_class, hhds::Node_class>& memo);

  Body&             body_;
  Loop_hoist_stats& stats_;
  bool              debug_;

  absl::flat_hash_set<uint32_t>                  variant_ports_;
  absl::flat_hash_map<hhds::Node_class, int8_t> invariant_memo_;  // -1 in progress (a cycle is not invariant)
  absl::flat_hash_set<hhds::Node_class>         wiring_only_;     // cone_computes: cones already found pure wiring
};

void Hoister::collect_variant_ports() {
  for (const auto& inst : body_.instances) {
    if (const auto desc = inst.node.subnode_loop()) {
      if (desc->index_input) {
        variant_ports_.insert(static_cast<uint32_t>(*desc->index_input));
      }
      if (desc->activation_input) {
        variant_ports_.insert(static_cast<uint32_t>(*desc->activation_input));
      }
    }
    for (const auto& carry : inst.node.subnode_group().carries()) {
      variant_ports_.insert(static_cast<uint32_t>(carry.input_port()));
    }
  }
  if (auto io = body_.graph->get_io()) {
    for (const auto& d : io->get_input_pin_decls()) {
      if (d.loop_break) {
        variant_ports_.insert(static_cast<uint32_t>(d.port_id));
      }
    }
  }
}

bool Hoister::invariant(const hhds::Node_class& n) {
  if (auto it = invariant_memo_.find(n); it != invariant_memo_.end()) {
    return it->second > 0;
  }
  invariant_memo_[n] = -1;
  bool result = hoistable_op(n);
  bool reads  = false;  // a cell with no input at all is a constant cprop owns, not a hoist
  if (result) {
    for (auto spin : n.inp_sorted_pins()) {
      for (auto drv : spin.get_driver_pins()) {
        reads = true;
        if (drv.is_const()) {
          continue;
        }
        if (gu::is_graph_input_pin(drv)) {
          if (variant_port(drv.get_port_id())) {
            result = false;
          }
          continue;
        }
        if (!invariant(drv.get_master_node())) {
          result = false;
        }
      }
      if (!result) {
        break;
      }
    }
  }
  result            = result && reads;
  invariant_memo_[n] = result ? 1 : 0;
  return result;
}

bool Hoister::cone_inputs_bound(const hhds::Node_class& root, absl::flat_hash_set<uint32_t>& ports) {
  absl::flat_hash_set<hhds::Node_class> seen;
  std::vector<hhds::Node_class>         work{root};
  while (!work.empty()) {
    auto n = work.back();
    work.pop_back();
    if (!seen.insert(n).second) {
      continue;
    }
    for (auto spin : n.inp_sorted_pins()) {
      for (auto drv : spin.get_driver_pins()) {
        if (drv.is_const()) {
          continue;
        }
        if (gu::is_graph_input_pin(drv)) {
          ports.insert(static_cast<uint32_t>(drv.get_port_id()));
        } else {
          work.push_back(drv.get_master_node());
        }
      }
    }
  }
  for (const auto& inst : body_.instances) {
    for (const auto pid : ports) {
      auto sp = inst.node.try_get_sink_pin(static_cast<hhds::Port_id>(pid));
      if (sp.is_invalid()) {
        return false;
      }
      bool driven = false;
      for ([[maybe_unused]] auto drv : sp.get_driver_pins()) {
        driven = true;
        break;
      }
      if (!driven) {
        return false;
      }
    }
  }
  return true;
}

bool Hoister::cone_computes(const hhds::Node_class& root) {
  absl::flat_hash_set<hhds::Node_class> seen;
  std::vector<hhds::Node_class>         work{root};
  while (!work.empty()) {
    auto n = work.back();
    work.pop_back();
    if (wiring_only_.contains(n) || !seen.insert(n).second) {
      continue;
    }
    if (!wiring_op(gu::type_op_of(n))) {
      return true;
    }
    for (auto spin : n.inp_sorted_pins()) {
      for (auto drv : spin.get_driver_pins()) {
        if (!drv.is_const() && !gu::is_graph_input_pin(drv)) {
          work.push_back(drv.get_master_node());
        }
      }
    }
  }
  // The whole cone is wiring: later frontier values that share it stop here.
  wiring_only_.insert(seen.begin(), seen.end());
  return false;
}

hhds::Pin_class Hoister::clone(const hhds::Pin_class& pin, const Instance& inst,
                               absl::flat_hash_map<hhds::Node_class, hhds::Node_class>& memo) {
  if (pin.is_const()) {
    return gu::create_const(*inst.parent, gu::const_of(pin));
  }
  if (gu::is_graph_input_pin(pin)) {
    // Bound by cone_inputs_bound: the parent's own driver of this invariant.
    return inst.node.get_sink_pin(pin.get_port_id()).get_driver_pin();
  }
  auto n = pin.get_master_node();
  if (auto it = memo.find(n); it != memo.end()) {
    return it->second.create_driver_pin(0);
  }
  auto neo = gu::create_typed_node(*inst.parent, gu::type_op_of(n));
  memo.emplace(n, neo);
  // Width and sign ride the driver pin; the node stays unnamed -- the port it
  // feeds is the traceable name, and a name would collide across instances.
  gu::carry_pin_attrs(n.create_driver_pin(0), neo.create_driver_pin(0));
  // A LUT's function is a node attribute: without it the clone computes nothing.
  gu::carry_attr<livehd::attrs::lut_t>(n, neo);
  for (auto spin : n.inp_pins_snapshot()) {
    for (auto drv : spin.get_driver_pins()) {
      neo.create_sink_pin(spin.get_port_id()).connect_driver(clone(drv, inst, memo));
    }
  }
  return neo.create_driver_pin(0);
}

bool Hoister::reaches_memory_address(const hhds::Node_class& root) {
  if (!addr_cone_built_) {
    // ONE backward pass over the body from every memory address sink: the cells whose value reaches an address.
    // Each candidate then asks a set (a forward walk per candidate was O(candidates x body)).
    addr_cone_built_ = true;
    std::vector<hhds::Node_class> work;
    const auto                    enqueue = [&](const hhds::Pin_class& driver) {
      if (driver.is_invalid() || driver.is_const() || gu::is_graph_input_pin(driver)) {
        return;
      }
      const auto m = driver.get_master_node();
      if (!m.is_invalid() && !gu::is_builtin_node(m) && Ntype::is_comb(gu::type_op_of(m)) && gu::type_op_of(m) != Ntype_op::Sub
          && addr_cone_.insert(m.get_class_index()).second) {
        work.push_back(m);
      }
    };
    for (const auto n : body_.graph->body().nodes()) {
      if (gu::type_op_of(n) != Ntype_op::Memory) {
        continue;
      }
      for (const auto& sink : n.inp_sorted_pins()) {
        const auto field = Ntype::get_sink_name(Ntype_op::Memory, static_cast<int>(sink.get_port_id()) % Ntype::Memory_port_stride);
        if (field.ends_with("addr")) {
          for (const auto& d : sink.get_driver_pins()) {
            enqueue(d);
          }
        }
      }
    }
    while (!work.empty()) {
      const auto n = work.back();
      work.pop_back();
      for (const auto& sink : n.inp_sorted_pins()) {
        for (const auto& d : sink.get_driver_pins()) {
          enqueue(d);
        }
      }
    }
  }
  return addr_cone_.contains(root.get_class_index());
}

bool Hoister::run() {
  auto io = body_.graph->get_io();
  if (!io || body_.instances.empty() || body_.plain_call) {
    return false;
  }
  collect_variant_ports();

  // Frontier: an invariant node some NON-invariant reader (a varying cell, an
  // inner loop, a body output) consumes. Everything below it either feeds
  // another frontier value or becomes dead once the frontier moves. Two more
  // conditions: the value's width must be KNOWN -- it becomes a declared port,
  // and a 0-width declaration takes the body off the simulator's inline pure
  // evaluator for good (so in the compile pipeline the move happens in the
  // cprop round after bitwidth, never before) -- and the cone must compute
  // something, not merely re-wire an invariant (see wiring_op).
  std::vector<hhds::Node_class> frontier;
  for (auto n : body_.graph->body().nodes()) {
    if (!invariant(n) || gu::bits_of(n.create_driver_pin(0)) <= 0) {
      continue;
    }
    bool wanted = false;
    for (const auto& e : n.create_driver_pin(0).out_edges()) {
      if (gu::is_graph_output_pin(e.sink) || !invariant(e.sink.get_master_node())) {
        wanted = true;
        break;
      }
    }
    if (wanted && cone_computes(n)) {
      frontier.push_back(n);
    }
  }
  if (frontier.empty()) {
    return false;
  }

  absl::flat_hash_set<std::string> names;
  hhds::Port_id                    next_pid = 0;
  for (const auto& d : io->get_input_pin_decls()) {
    names.insert(d.name);
    next_pid = std::max<hhds::Port_id>(next_pid, d.port_id + 1);
  }
  for (const auto& d : io->get_output_pin_decls()) {
    names.insert(d.name);
    next_pid = std::max<hhds::Port_id>(next_pid, d.port_id + 1);
  }
  std::vector<absl::flat_hash_map<hhds::Node_class, hhds::Node_class>> memos(body_.instances.size());

  size_t hoisted = 0;
  for (auto f : frontier) {
    absl::flat_hash_set<uint32_t> ports;
    if (!cone_inputs_bound(f, ports)) {
      continue;
    }
    // The new body input is an interface port, and pass.bitwidth reads a callee only through its declared
    // interface: the body sees the hoisted value as [0, 2^bits) (or signed), not its own range. An unsized cone
    // would be an undeclared input; and a value that reaches a memory address (the one place bitwidth rejects a
    // widened range) must keep its exact range: loop_carry_wide_ref's `v[r * 3 + c]` with the invariant `r * 3`
    // ([0, 3], stamped 3 bits) moved out read as [0..9] for a 6-entry memory. Such a cone stays in the body.
    if (gu::bits_of(f.create_driver_pin(0)) <= 0 || reaches_memory_address(f)) {
      continue;
    }
    std::string name;
    for (size_t k = hoisted;; ++k) {
      name = "__hoist" + std::to_string(k);
      if (!names.contains(name)) {
        break;
      }
    }
    names.insert(name);
    const auto pid   = next_pid++;
    auto       fdrv  = f.create_driver_pin(0);
    const auto bits  = gu::bits_of(fdrv);
    const bool unsign = gu::is_unsign(fdrv);
    io->add_input(name, pid);
    if (bits > 0) {
      io->set_bits(name, static_cast<uint32_t>(bits));
    }
    io->set_unsign(name, unsign);
    auto new_in = body_.graph->get_input_pin(name);

    for (size_t i = 0; i < body_.instances.size(); ++i) {
      const auto& inst  = body_.instances[i];
      auto        value = clone(fdrv, inst, memos[i]);
      inst.node.create_sink_pin(pid).connect_driver(value);
    }
    // Snapshot: the fan-out view is live and every step below rewires through it.
    // Delete only THIS edge: a reader's sink may hold other drivers too (an
    // inner compact loop's carry-in sink holds its self edge beside the
    // external initial value), and those must survive the move.
    std::vector<hhds::Edge_class> edges;
    for (const auto& e : fdrv.out_edges()) {
      edges.push_back(e);
    }
    for (auto& edge : edges) {
      edge.del_edge();
      new_in.connect_sink(edge.sink);
    }
    ++hoisted;
    if (debug_) {
      std::println(stderr, "loop_hoist: {}: {} <- {} ({} input(s), {} instance(s))", body_.graph->get_name(), name,
                   Ntype::get_name(gu::type_op_of(f)), ports.size(), body_.instances.size());
    }
  }
  if (hoisted == 0) {
    return false;
  }

  // The moved cones are dead in the body now: the frontier lost its readers,
  // then whatever fed only the frontier. Only invariant nodes can have died.
  size_t removed = 0;
  for (bool again = true; again;) {
    again = false;
    std::vector<hhds::Node_class> dead;
    for (const auto& [n, inv] : invariant_memo_) {
      if (inv > 0 && !n.is_invalid() && !n.has_out_edges()) {
        dead.push_back(n);
      }
    }
    for (auto n : dead) {
      invariant_memo_.erase(n);
      n.del_node();
      ++removed;
      again = true;
    }
  }
  ++stats_.bodies;
  stats_.hoisted_values += hoisted;
  stats_.removed_nodes += removed;
  stats_.instances += body_.instances.size();
  return true;
}

}  // namespace

Loop_hoist_stats hoist_loop_invariants(const std::vector<std::shared_ptr<hhds::Graph>>& graphs) {
  Loop_hoist_stats stats;
  const bool       debug = livehd::debug::env<"LHD_LOOP_HOIST_DEBUG">() != nullptr;
  // A body's new port must be driven by EVERY parent, and the body itself
  // must be in scope (a graph outside it -- a compile-cache restored FINAL
  // body, an unloaded unit -- is never rewritten). With the whole library in
  // scope that holds for every body. A warm compile runs this on the graphs
  // the cache did not restore, so `graphs` is a strict subset there: rather
  // than skipping every body (a warm netlist that differs from the cold one),
  // only a body some out-of-scope graph instantiates is left alone. Loop
  // bodies are derived trees of their owner unit (restored or re-extracted
  // together), so on a warm compile that blocks none. The out-of-scope scan
  // runs only when an in-scope graph holds a loop instance, and visits each
  // such graph once: linear in the library.
  absl::flat_hash_set<const hhds::Graph*> in_scope;
  absl::flat_hash_set<const hhds::Graph*> blocked;
  {
    absl::flat_hash_map<hhds::GraphLibrary*, size_t> seen;
    bool                                             any_loop = false;
    for (const auto& g : graphs) {
      if (!g) {
        continue;
      }
      auto io = g->get_io();
      if (!io || io->get_library() == nullptr) {
        return stats;
      }
      if (in_scope.insert(g.get()).second) {
        ++seen[io->get_library()];
      }
      any_loop = any_loop || g->has_loop_subnodes();
    }
    if (!any_loop) {
      return stats;
    }
    for (const auto& [lib, count] : seen) {
      const auto gids = lib->all_gids();
      if (gids.size() == count) {
        continue;
      }
      size_t outside = 0;
      for (const auto gid : gids) {
        auto og = lib->get_graph(gid);
        if (!og || in_scope.contains(og.get())) {
          continue;
        }
        ++outside;
        for (auto n : og->body().nodes()) {
          if (gu::type_op_of(n) != Ntype_op::Sub) {
            continue;
          }
          if (auto child = n.get_subnode_graph()) {
            blocked.insert(child.get());
          }
        }
      }
      if (debug) {
        std::println(stderr, "loop_hoist: {} of {} library graphs out of scope; {} callee(s) blocked", outside, gids.size(), blocked.size());
      }
    }
  }
  // A hoisted cone lands in the parent; when that parent is itself a loop body
  // the cone may be invariant there too, so sweep until nothing moves. Each
  // sweep sees the library as the previous one left it.
  for (int round = 0; round < 4; ++round) {
    // Discovery order, not hash order: which body moves first decides whether
    // a parent body sees its child's clones this round or the next, and so the
    // `__hoist<k>` numbering -- it must not depend on pointer values (ASLR).
    std::vector<Body>                         bodies;
    absl::flat_hash_map<hhds::Graph*, size_t> body_ix;
    for (const auto& g : graphs) {
      if (!g) {
        continue;
      }
      for (auto n : g->body().nodes()) {
        if (gu::type_op_of(n) != Ntype_op::Sub) {
          continue;
        }
        auto child = n.get_subnode_graph();
        if (!child) {
          continue;
        }
        auto [it, fresh] = body_ix.try_emplace(child.get(), bodies.size());
        if (fresh) {
          bodies.emplace_back();
        }
        auto& body = bodies[it->second];
        body.graph = child;
        if (n.subnode_loop()) {
          body.instances.push_back({g.get(), n});
        } else {
          body.plain_call = true;
        }
      }
    }
    bool changed = false;
    for (auto& body : bodies) {
      if (body.instances.empty() || !in_scope.contains(body.graph.get()) || blocked.contains(body.graph.get())) {
        continue;
      }
      Hoister hoister(body, stats, debug);
      changed |= hoister.run();
    }
    if (!changed) {
      break;
    }
  }
  return stats;
}

}  // namespace livehd::cprop
