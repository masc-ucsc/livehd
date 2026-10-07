// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "acyclic.hpp"

#include <algorithm>
#include <functional>
#include <set>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "diag.hpp"
#include "inline_sub.hpp"
#include "node_util.hpp"
#include "port_reach.hpp"
#include "split_selfref.hpp"

namespace livehd::legalize {

namespace {

namespace gu = livehd::graph_util;

// A compact loop's carry-in also holds the loop's OWN carry-out: the previous
// ordinal's value, a sequencing edge, never a combinational one.
bool carry_self_edge(const hhds::Node_class& m, const hhds::Pin_class& sink, const hhds::Pin_class& drv) {
  if (!m.is_loop_subnode() || drv.get_master_node() != m) {
    return false;
  }
  for (const auto& c : m.subnode_group().carries()) {
    if (c.output_port() == drv.get_port_id() && c.input_port() == sink.get_port_id()) {
      return true;
    }
  }
  return false;
}

// The combinational fan-in of driver pins of ONE definition. `refined` false
// is the ATOMIC view (an instance output depends on every input); true is the
// ARC view (the callee's port_reach summary).
class Comb_deps {
public:
  explicit Comb_deps(port_reach::Cache& reach) : reach_(reach) {}

  // Calls `out` with every driver `d` depends on in the same cycle.
  void deps(const hhds::Pin_class& d, bool refined, const std::function<void(const hhds::Pin_class&)>& out) {
    if (d.is_invalid() || d.is_const() || gu::is_graph_input_pin(d)) {
      return;
    }
    const auto m = d.get_master_node();
    if (m.is_invalid()) {
      return;
    }
    const auto sink_drivers = [&](const hhds::Pin_class& sink) {
      for (auto drv : sink.get_driver_pins()) {
        if (!carry_self_edge(m, sink, drv)) {
          out(drv);
        }
      }
    };
    const auto all_inputs = [&] {
      for (auto sink : m.inp_sorted_pins()) {
        sink_drivers(sink);
      }
    };
    const auto op = gu::type_op_of(m);
    // Before the register cut: is_type_register counts a Memory, but an async
    // read port is combinational in its own address/enable. Write cones
    // (forwarding, an unclocked array's update) are sequenced after the read
    // by every consumer, so they are no loop; an undecoded port stays a cut,
    // as a Memory always was here.
    if (op == Ntype_op::Memory) {
      auto [it, fresh] = memories_.try_emplace(m, m);
      (void)it->second.deps(static_cast<hhds::Port_id>(d.get_port_id()), out, /*writes=*/false);
      return;
    }
    if (gu::is_type_register(m)) {
      return;  // last period's value
    }
    if (op != Ntype_op::Sub) {
      all_inputs();
      return;
    }
    auto callee = m.get_subnode_graph();
    if (!callee) {
      return;  // a body-less blackbox is opaque to every consumer: its outputs are sources
    }
    if (!refined) {
      all_inputs();
      return;
    }
    const auto& summary = reach_.callee_of(callee);
    const auto  row     = summary.out2ins.find(static_cast<uint32_t>(d.get_port_id()));
    if (row == summary.out2ins.end()) {
      return;  // a pure function of the callee's state
    }
    absl::flat_hash_set<uint32_t> ins(row->second.begin(), row->second.end());
    if (m.is_loop_subnode()) {
      // The summary is ONE ordinal. From the second on, a carry-in holds the
      // previous carry-out, so the final output also depends on whatever that
      // carry-out reads: close the row over the carries.
      for (bool grew = true; grew;) {
        grew = false;
        for (const auto& c : m.subnode_group().carries()) {
          if (!ins.contains(static_cast<uint32_t>(c.input_port()))) {
            continue;
          }
          if (auto it = summary.out2ins.find(static_cast<uint32_t>(c.output_port())); it != summary.out2ins.end()) {
            for (const auto in : it->second) {
              grew |= ins.insert(in).second;
            }
          }
        }
      }
    }
    for (auto sink : m.inp_sorted_pins()) {
      if (ins.contains(static_cast<uint32_t>(sink.get_port_id()))) {
        sink_drivers(sink);
      }
    }
  }

private:
  port_reach::Cache&                                             reach_;
  absl::node_hash_map<hhds::Node_class, port_reach::Memory_deps> memories_;
};

// Iterative Tarjan over driver pins. `roots` seeds the walk; `inside` (when
// non-null) restricts it to a vertex subset. Calls `component` with every
// strongly connected component that is a real cycle (more than one pin, or
// one pin that depends on itself).
void comb_sccs(const std::vector<hhds::Pin_class>& roots, const absl::flat_hash_set<hhds::Class_index>* inside, bool refined,
               Comb_deps& comb, const std::function<void(const std::vector<hhds::Pin_class>&)>& component) {
  struct Info {
    uint32_t index;
    uint32_t low;
    bool     on_stack;
  };
  absl::flat_hash_map<hhds::Class_index, Info> info;
  std::vector<hhds::Pin_class>                 stack;
  struct Frame {
    hhds::Pin_class              pin;
    std::vector<hhds::Pin_class> succ;
    size_t                       next = 0;
    bool                         self = false;
  };
  std::vector<Frame> frames;
  uint32_t           counter = 0;

  const auto admit = [&](const hhds::Pin_class& p) {
    return !p.is_invalid() && !p.is_const() && (inside == nullptr || inside->contains(p.get_class_index()));
  };
  const auto push = [&](const hhds::Pin_class& p) {
    info[p.get_class_index()] = Info{counter, counter, true};
    ++counter;
    stack.push_back(p);
    Frame f;
    f.pin = p;
    comb.deps(p, refined, [&](const hhds::Pin_class& q) {
      if (admit(q)) {
        f.self |= q.get_class_index() == p.get_class_index();
        f.succ.push_back(q);
      }
    });
    frames.push_back(std::move(f));
  };

  for (const auto& root : roots) {
    if (!admit(root) || info.contains(root.get_class_index())) {
      continue;
    }
    push(root);
    while (!frames.empty()) {
      auto& f = frames.back();
      if (f.next < f.succ.size()) {
        const auto q  = f.succ[f.next++];
        auto       it = info.find(q.get_class_index());
        if (it == info.end()) {
          push(q);  // invalidates `f`
        } else if (it->second.on_stack) {
          auto& mine = info[f.pin.get_class_index()];
          mine.low   = std::min(mine.low, it->second.index);
        }
        continue;
      }
      const auto pin  = f.pin;
      const bool self = f.self;
      frames.pop_back();
      const auto me = info[pin.get_class_index()];
      if (!frames.empty()) {
        auto& parent = info[frames.back().pin.get_class_index()];
        parent.low   = std::min(parent.low, me.low);
      }
      if (me.low != me.index) {
        continue;
      }
      std::vector<hhds::Pin_class> members;
      for (;;) {
        const auto top = stack.back();
        stack.pop_back();
        info[top.get_class_index()].on_stack = false;
        members.push_back(top);
        if (top.get_class_index() == pin.get_class_index()) {
          break;
        }
      }
      if (members.size() > 1 || self) {
        component(members);
      }
    }
  }
}

std::string pin_name(const hhds::Pin_class& p) {
  const auto m = p.get_master_node();
  return std::string{gu::has_name(m) ? gu::node_name_of(m) : std::string_view{Ntype::get_name(gu::type_op_of(m))}};
}

// Children before parents, so an instance is inlined from an already-acyclic body.
std::vector<std::shared_ptr<hhds::Graph>> callee_first(const std::vector<std::shared_ptr<hhds::Graph>>& graphs) {
  absl::flat_hash_map<hhds::Gid, std::shared_ptr<hhds::Graph>> by_gid;
  for (const auto& g : graphs) {
    if (g != nullptr && g->get_io() != nullptr) {
      by_gid.emplace(g->get_gid(), g);
    }
  }
  std::vector<std::shared_ptr<hhds::Graph>> order;
  absl::flat_hash_set<hhds::Gid>            done;
  const auto visit = [&](auto& self, const std::shared_ptr<hhds::Graph>& g) -> void {
    if (!done.insert(g->get_gid()).second) {
      return;
    }
    for (auto n : g->body().nodes()) {
      if (!n.is_invalid() && gu::type_op_of(n) == Ntype_op::Sub) {
        if (auto it = by_gid.find(n.get_subnode_gid()); it != by_gid.end()) {
          self(self, it->second);
        }
      }
    }
    order.push_back(g);
  };
  for (const auto& g : graphs) {
    if (g != nullptr && g->get_io() != nullptr) {
      visit(visit, g);
    }
  }
  return order;
}

// A non-loop instance whose whole closure holds no state (no
// Flop/Fflop/Latch/Memory, no rolled loop, no blackbox): inlining it changes
// no state identity. Shared across the design: callee-first order means a
// callee's closure is final before any parent asks.
class State_free {
public:
  bool operator()(const hhds::Node_class& sub) {
    if (sub.is_invalid() || sub.is_loop_subnode() || !sub.get_subnode_graph()) {
      return false;
    }
    return def(sub.get_subnode_graph().get());
  }

private:
  bool def(hhds::Graph* g) {
    if (auto it = cache_.find(g->get_gid()); it != cache_.end()) {
      return it->second;
    }
    cache_.emplace(g->get_gid(), false);  // recursion guard
    bool ok = true;
    for (auto n : g->body().nodes()) {
      if (gu::is_type_register(n) || (gu::type_op_of(n) == Ntype_op::Sub && !(*this)(n))) {
        ok = false;
        break;
      }
    }
    cache_[g->get_gid()] = ok;
    return ok;
  }
  absl::flat_hash_map<hhds::Gid, bool> cache_;
};

// Shared by every scan of one make_acyclic call. A callee's summary is final
// once that callee is processed (callee first); a split half is a NEW def; and
// a def deleted in between stays alive in Split_state::removed, so no memo
// entry outlives its graph.
struct Scan_ctx {
  port_reach::Cache reach{port_reach::Callee_reach{}, /*slices=*/false};
  State_free        state_free;
};

struct Loop_ring {
  hhds::Node_class                   loop;
  absl::flat_hash_set<hhds::Port_id> ring_inputs;  // the inputs the cycle drives
};

struct Scc_scan {
  // ARC-level cycles: an instance's output depends on the inputs its
  // port_reach summary lists (a Moore output on none), the arcs every consumer
  // orders instances by. A valid/ready handshake is no such cycle; a real path
  // that leaves and re-enters a port is.
  std::vector<std::vector<hhds::Pin_class>> cycles;
  // On a cycle only with instances ATOMIC -- no cycle at arc level, but a
  // consumer would have to evaluate the instance in pieces (sim's
  // per-output-group callee partitions):
  //   state-free instances, inlined (no state moves);
  //   rolled loops (combinational too), split by the ring, never unrolled.
  // A rolled loop on an ARC-level cycle is not here: closing a real path
  // through it, no split can break it.
  std::vector<hhds::Node_class> state_free;
  std::vector<Loop_ring>        loops;
};

Scc_scan scan(hhds::Graph* g, Scan_ctx& ctx) {
  Comb_deps                    comb(ctx.reach);
  std::vector<hhds::Pin_class> roots;
  for (auto node : g->body().nodes()) {
    for (auto drv : node.out_sorted_pins()) {
      roots.push_back(drv);
    }
  }
  Scc_scan                              out;
  absl::flat_hash_set<hhds::Node_class> seen;
  std::vector<Loop_ring>                loops;
  absl::flat_hash_set<hhds::Node_class> loop_on_arc;
  comb_sccs(roots, nullptr, /*refined=*/false, comb, [&](const std::vector<hhds::Pin_class>& members) {
    absl::flat_hash_set<hhds::Class_index> inside;
    for (const auto& p : members) {
      inside.insert(p.get_class_index());
    }
    bool through_sub = false;
    for (const auto& p : members) {
      const auto m = p.get_master_node();
      if (gu::type_op_of(m) != Ntype_op::Sub) {
        continue;
      }
      through_sub = true;
      if (!seen.insert(m).second) {
        continue;
      }
      if (m.is_loop_subnode() && m.get_subnode_graph()) {
        Loop_ring ring{.loop = m, .ring_inputs = {}};
        for (auto sink : m.inp_sorted_pins()) {
          for (auto drv : sink.get_driver_pins()) {
            if (!carry_self_edge(m, sink, drv) && inside.contains(drv.get_class_index())) {
              ring.ring_inputs.insert(sink.get_port_id());
            }
          }
        }
        loops.push_back(std::move(ring));
      } else if (ctx.state_free(m)) {
        out.state_free.push_back(m);
      }
    }
    if (!through_sub) {
      out.cycles.push_back(members);
      return;
    }
    comb_sccs(members, &inside, /*refined=*/true, comb, [&](const std::vector<hhds::Pin_class>& arc) {
      for (const auto& p : arc) {
        if (const auto m = p.get_master_node(); gu::type_op_of(m) == Ntype_op::Sub && m.is_loop_subnode()) {
          loop_on_arc.insert(m);
        }
      }
      out.cycles.push_back(arc);
    });
  });
  for (auto& ring : loops) {
    if (!loop_on_arc.contains(ring.loop)) {
      out.loops.push_back(std::move(ring));
    }
  }
  return out;
}

std::string instance_label(const hhds::Node_class& m) {
  if (gu::has_name(m)) {
    return std::string{gu::node_name_of(m)};
  }
  const auto callee = m.get_subnode_graph();
  return callee ? std::string{callee->get_name()} : std::string{"-"};
}

}  // namespace

Acyclic_result make_acyclic(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, Split_state& state) {
  Acyclic_result result;
  Scan_ctx       ctx;
  for (const auto& gp : callee_first(graphs)) {
    auto*                 g    = gp.get();
    const auto            name = std::string{g->get_name()};
    auto&                 done = result.inlined[g];
    std::set<std::string> through_module;  // instances inlined off an arc-level cycle
    auto                  found = scan(g, ctx);
    for (int round = 0; round < 64; ++round) {
      // Every non-loop instance with a body on an arc-level cycle is inlined:
      // the path really leaves and re-enters its ports, so its boundary goes.
      std::vector<hhds::Node_class>         subs = found.state_free;
      absl::flat_hash_set<hhds::Node_class> seen(subs.begin(), subs.end());
      for (const auto& members : found.cycles) {
        for (const auto& p : members) {
          const auto m = p.get_master_node();
          if (gu::type_op_of(m) == Ntype_op::Sub && m.get_subnode_graph() && !m.is_loop_subnode() && seen.insert(m).second) {
            subs.push_back(m);
            through_module.insert(instance_label(m));
          }
        }
      }
      if (subs.empty() && found.loops.empty()) {
        break;
      }
      std::ranges::sort(subs, [](const auto& a, const auto& b) { return a.get_debug_nid() < b.get_debug_nid(); });
      std::ranges::sort(found.loops, [](const auto& a, const auto& b) { return a.loop.get_debug_nid() < b.loop.get_debug_nid(); });
      bool progress = false;
      for (auto& ring : found.loops) {
        auto* lib = g->get_io()->get_library();
        if (lib != nullptr && split_loop_by_ring(g, *lib, ring.loop, ring.ring_inputs, &state)) {
          ++result.loops_split;
          progress = true;
        }
      }
      for (const auto& s : subs) {
        if (s.is_invalid()) {
          continue;
        }
        const auto callee = std::string{s.get_subnode_graph()->get_name()};
        if (gu::inline_sub_instance(g, s, kAcyclicPass)) {
          done.push_back(callee);
          ++result.instances_inlined;
          progress = true;
        }
      }
      if (!progress) {
        break;
      }
      found = scan(g, ctx);
    }
    if (!through_module.empty()) {
      std::string list;
      for (const auto& s : through_module) {
        list += (list.empty() ? "" : ", ") + s;
      }
      diag::warn(kAcyclicPass, "comb-loop-through-module", "time")
          .msg("combinational path loops through module port(s) in '{}'; inlined {} -- ideally split the bus/module in the RTL",
               name,
               list)
          .emit();
    }
    // What is left has no inlinable instance on it: packed slices inside this
    // body (false), a real path through a rolled loop, or a true loop.
    if (!found.cycles.empty()) {
      result.slices_rewired += gu::split_packed_cycle_slices(g);
      found = scan(g, ctx);
    }
    for (const auto& members : found.cycles) {
      std::string hops;
      bool        loop_sub = false;
      for (size_t i = 0; i < members.size(); ++i) {
        const auto m  = members[i].get_master_node();
        loop_sub     |= gu::type_op_of(m) == Ntype_op::Sub && m.is_loop_subnode();
        if (i < 16) {
          hops += (i == 0 ? "" : " -> ") + pin_name(members[i]);
        }
      }
      ++result.loops;
      if (loop_sub) {
        diag::err(kAcyclicPass, "comb-loop-through-loop", "time")
            .msg("combinational loop through a rolled loop instance in '{}': {}{}", name, hops, members.size() > 16 ? " ..." : "")
            .emit();
      } else {
        diag::err(kAcyclicPass, "comb-loop", "time")
            .msg("combinational loop in '{}': {}{}", name, hops, members.size() > 16 ? " ..." : "")
            .emit();
      }
    }
  }
  return result;
}

}  // namespace livehd::legalize
