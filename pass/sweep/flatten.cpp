// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <deque>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "inline_sub.hpp"
#include "node_util.hpp"
#include "sweep.hpp"

namespace livehd::sweep {
namespace gu = livehd::graph_util;

namespace {
// A clock-gate leaf: a body with no instances whose 1-bit output is `AND(clock input, Q)` where Q is a 1-bit
// state element (flop or latch) clocked by that same input -- the LiteX/NVDLA `CKLNQD12` shapes (a negedge
// flop or an enable latch followed by an AND). Spliced into its parent, the gate is no longer a module output
// and synthesis would see a clock cone it cannot recognize (the flop-based form is not `<clock> & <latch>`), so the
// registers it clocks stay native flops the placer rejects. Left as an instance, the gate's output is a module
// output that the register read-back already rebuilds as a derived clock.
bool is_clock_gate_leaf(const hhds::Graph& g) {
  const auto real_bits = [](const hhds::Pin_class& p) { return gu::real_width(p); };
  size_t ands = 0;
  for (auto n : g.body().nodes()) {
    const auto op = gu::type_op_of(n);
    if (op == Ntype_op::Sub) {
      return false;
    }
    if (op == Ntype_op::And) {
      ++ands;
    }
  }
  if (ands != 1) {
    return false;
  }
  for (auto n : g.body().nodes()) {
    if (gu::type_op_of(n) != Ntype_op::And) {
      continue;
    }
    const auto out = n.get_driver_pin(0);
    if (out.is_invalid() || real_bits(out) != 1) {
      return false;
    }
    std::vector<hhds::Pin_class> live;
    for (const auto& in_pin : n.inp_sorted_pins()) {
      for (const auto& drv : in_pin.get_driver_pins()) {
        if (drv.is_const()) {
          if (!gu::const_of(drv).bit_test(0)) {
            return false;
          }
          continue;
        }
        live.push_back(drv);
      }
    }
    if (live.size() != 2) {
      return false;
    }
    for (int k = 0; k < 2; ++k) {
      const auto clk = live[k];
      const auto q   = live[1 - k];
      if (!gu::is_graph_input_pin(clk) || real_bits(clk) > 1 || q.is_invalid() || q.get_port_id() != 0) {
        continue;
      }
      const auto st = q.get_master_node();
      const auto op = gu::type_op_of(st);
      if ((op != Ntype_op::Flop && op != Ntype_op::Latch) || real_bits(q) != 1) {
        continue;
      }
      auto ctl = gu::get_driver_of_sink_name(st, op == Ntype_op::Flop ? "clock_pin" : "enable");
      // The state element may see the clock through an inverter (a falling-edge sample).
      while (!ctl.is_invalid() && !ctl.is_const() && !gu::is_graph_input_pin(ctl) && gu::type_op_of(ctl.get_master_node()) == Ntype_op::Not) {
        const auto inv = ctl.get_master_node();
        ctl            = hhds::Pin_class{};
        for (const auto& in_pin : inv.inp_sorted_pins()) {
          for (const auto& drv : in_pin.get_driver_pins()) {
            ctl = drv;
          }
        }
      }
      if (!ctl.is_invalid() && ctl == clk) {
        return true;
      }
    }
    return false;
  }
  return false;
}
}  // namespace

Flatten_stats flatten(const std::vector<std::shared_ptr<hhds::Graph>>& roots, Flat_origin* origin) {
  Flatten_stats stats;
  if (origin != nullptr && roots.size() != 1) {
    origin = nullptr;  // provenance is per root
  }
  for (const auto& root : roots) {
    if (!root) {
      continue;
    }
    gu::Host_inliner                                inliner(root.get(), "pass.sweep");
    std::deque<hhds::Node_class>                    work;
    // Provenance: the context of every Sub still in the root (by class index).
    absl::flat_hash_map<hhds::Class_index, int32_t> context_of;
    if (origin != nullptr) {
      origin->contexts.clear();
      origin->flops.clear();
      origin->memories.clear();
      origin->contexts.push_back({.parent = -1, .sub = 0, .def = root->get_gid()});
    }
    for (auto node : root->body().nodes()) {
      const auto op = gu::type_op_of(node);
      if (op == Ntype_op::Sub) {
        work.push_back(node);
        if (origin != nullptr) {
          const auto callee = node.get_subnode_graph();
          context_of.emplace(node.get_class_index(), static_cast<int32_t>(origin->contexts.size()));
          origin->contexts.push_back({.parent = 0, .sub = node.get_debug_nid(), .def = callee ? callee->get_gid() : hhds::Gid{}});
        }
      } else if (op == Ntype_op::Flop && origin != nullptr) {
        origin->flops.emplace(node.get_class_index(), std::pair{int32_t{0}, node.get_debug_nid()});
      } else if (op == Ntype_op::Memory && origin != nullptr) {
        origin->memories.emplace(node.get_class_index(), std::pair{int32_t{0}, node.get_debug_nid()});
      }
    }
    while (!work.empty()) {
      auto inst = work.front();
      work.pop_front();
      if (inst.is_invalid() || gu::type_op_of(inst) != Ntype_op::Sub) {
        continue;
      }
      // A black box (a Liberty cell or macro, external IP) has no body to
      // splice, and a rolled loop keeps its compact form.
      if (!inst.get_subnode_graph() || inst.subnode_loop()) {
        ++stats.kept;
        continue;
      }
      if (is_clock_gate_leaf(*inst.get_subnode_graph())) {
        ++stats.kept;
        continue;
      }
      std::vector<hhds::Node_class>                              children;
      std::vector<std::pair<hhds::Node_class, hhds::Node_class>> clones;
      int32_t                                                    context = -1;
      if (origin != nullptr) {
        if (auto it = context_of.find(inst.get_class_index()); it != context_of.end()) {
          context = it->second;
        }
      }
      if (!inliner.splice(inst, &children, context >= 0 ? &clones : nullptr)) {
        ++stats.kept;
        continue;
      }
      ++stats.inlined;
      if (context >= 0) {
        for (const auto& [body_node, clone] : clones) {
          const auto op = gu::type_op_of(body_node);
          if (op == Ntype_op::Flop) {
            origin->flops.emplace(clone.get_class_index(), std::pair{context, body_node.get_debug_nid()});
          } else if (op == Ntype_op::Memory) {
            origin->memories.emplace(clone.get_class_index(), std::pair{context, body_node.get_debug_nid()});
          } else if (op == Ntype_op::Sub) {
            const auto callee = body_node.get_subnode_graph();
            context_of.emplace(clone.get_class_index(), static_cast<int32_t>(origin->contexts.size()));
            origin->contexts.push_back(
                {.parent = context, .sub = body_node.get_debug_nid(), .def = callee ? callee->get_gid() : hhds::Gid{}});
          }
        }
      }
      for (const auto& child : children) {
        work.push_back(child);
      }
    }
    inliner.flush();
  }
  return stats;
}

std::vector<std::shared_ptr<hhds::Graph>> reachable_closure(const std::vector<std::shared_ptr<hhds::Graph>>& roots) {
  std::vector<std::shared_ptr<hhds::Graph>> out;
  absl::flat_hash_set<const hhds::Graph*>   seen;
  std::deque<std::shared_ptr<hhds::Graph>>  pending(roots.begin(), roots.end());
  while (!pending.empty()) {
    auto graph = pending.front();
    pending.pop_front();
    if (!graph || !seen.insert(graph.get()).second) {
      continue;
    }
    out.push_back(graph);
    for (auto node : graph->body().nodes()) {
      if (gu::type_op_of(node) == Ntype_op::Sub) {
        if (auto callee = node.get_subnode_graph()) {
          pending.push_back(std::move(callee));
        }
      }
    }
  }
  return out;
}

}  // namespace livehd::sweep
