// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "clock_gate.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "node_util.hpp"

namespace livehd::clock_gate {

namespace {

namespace gu = livehd::graph_util;
using Node   = hhds::Node_class;
using Pin    = hhds::Pin_class;

// A pin by what it is: the node that drives it and its port (the same net reached through two Pin_class handles gets one id).
uint64_t pin_id(const Pin& p) {
  return (static_cast<uint64_t>(p.get_master_node().get_class_index().value) << 16) ^ static_cast<uint64_t>(p.get_port_id());
}

// A 1-bit condition (the sign of a 1-bit value does not change whether it is zero). A graph input keeps its width on the interface.
bool one_bit(const Pin& p) {
  if (p.is_invalid() || p.is_const()) {
    return false;
  }
  if (gu::bits_of(p) != 0) {
    return gu::bits_of(p) == 1;
  }
  if (gu::is_graph_input_pin(p)) {
    const auto io = p.get_master_node().get_graph()->get_io();
    return io && io->get_bits(std::string{p.get_pin_name()}) == 1;
  }
  return false;
}

bool const_known_true(const Pin& p) {
  return p.is_const() && !gu::const_of(p).has_unknowns() && gu::const_of(p).is_numeric() && !gu::const_of(p).is_known_zero();
}

// The conjuncts of an enable: the 1-bit operands of an And, else the driver itself. An unset or constant-true enable has none
// (always enabled). nullopt when the enable cannot be taken apart safely: a constant false, or a driver wider than one bit (it
// reads as "non-zero", which is not the AND of its bits).
std::optional<std::vector<Pin>> conjuncts_of(const Pin& driver) {
  if (driver.is_invalid() || const_known_true(driver)) {
    return std::vector<Pin>{};
  }
  if (driver.is_const() || !one_bit(driver)) {
    return std::nullopt;
  }
  std::vector<Pin> out;
  const auto       node = driver.get_master_node();
  if (gu::type_op_of(node) == Ntype_op::And) {
    std::vector<Pin> operands;
    bool             ok = true;
    for (auto sink : node.inp_sorted_pins()) {
      for (const auto& d : sink.get_driver_pins()) {
        if (const_known_true(d)) {
          continue;
        }
        if (!one_bit(d)) {
          ok = false;
        }
        operands.push_back(d);
      }
    }
    if (ok && operands.size() >= 2) {
      out = std::move(operands);
    }
  }
  if (out.empty()) {
    out.push_back(driver);
  }
  std::ranges::sort(out, {}, pin_id);
  out.erase(std::ranges::unique(out, {}, pin_id).begin(), out.end());
  return out;
}

bool admit_(const Admit& admit, std::string_view stage, uint64_t work = 1) { return !admit || admit(stage, work); }

bool plain_cell(const Node& n) {
  if (gu::type_op_of(n) != Ntype_op::Clock_cell) {
    return false;
  }
  const auto clk = gu::get_driver_of_sink_name(n, "clk_ref");
  const auto div = gu::get_driver_of_sink_name(n, "div");
  const auto inv = gu::get_driver_of_sink_name(n, "invert");
  if (clk.is_invalid() || clk.is_const()) {
    return false;
  }
  if (!div.is_invalid() && !(div.is_const() && gu::const_of(div).is_just_i64() && gu::const_of(div).to_just_i64() == 1)) {
    return false;
  }
  return inv.is_invalid() || inv.is_known_false();
}

// What the plain gates above `clk` already check: their conjuncts (outermost first, deduplicated) and the clock they all hang from.
struct Gate_context {
  std::vector<Pin>   conj;
  std::set<uint64_t> ids;
  Pin                root;  // `clk` itself when nothing gates it
  bool               gated = false;
};

Gate_context context_of(Pin clk) {
  Gate_context     ctx;
  std::vector<Pin> inner_first;
  for (int guard = 0; guard < 64 && !clk.is_invalid() && !clk.is_const(); ++guard) {
    const auto node = clk.get_master_node();
    if (!plain_cell(node)) {
      break;
    }
    const auto conj = conjuncts_of(gu::get_driver_of_sink_name(node, "en"));
    // An enable we cannot safely factor is a domain boundary. In particular,
    // crossing a constant-false gate would turn a stopped clock back on.
    if (!conj) {
      break;
    }
    ctx.gated = true;
    for (const auto& c : *conj) {
      inner_first.push_back(c);
    }
    clk = gu::get_driver_of_sink_name(node, "clk_ref");
  }
  ctx.root = clk;
  for (auto it = inner_first.rbegin(); it != inner_first.rend(); ++it) {
    if (ctx.ids.insert(pin_id(*it)).second) {
      ctx.conj.push_back(*it);
    }
  }
  return ctx;
}

void inherit_color(const Node& node, const Pin& near) {
  if (near.is_invalid() || near.is_const() || gu::is_graph_input_pin(near)) {
    return;
  }
  const auto master = near.get_master_node();
  if (gu::has_color(master)) {
    gu::set_color(node, gu::color_of(master));
  }
}

Pin stamped(const Pin& p) {
  gu::set_bits(p, 1);
  gu::set_unsign(p);
  return p;
}

// And over `pins` (one pin is itself), cached by its operand identity so equal enables share one node.
class Builder {
public:
  explicit Builder(hhds::Graph& graph) : graph_(graph) {}

  Pin all_of(const std::vector<Pin>& pins) {
    if (pins.size() == 1) {
      return pins.front();
    }
    std::string key;
    for (const auto& p : pins) {
      key += std::to_string(pin_id(p)) + ',';
    }
    if (const auto it = ands_.find(key); it != ands_.end()) {
      return it->second;
    }
    auto n = gu::create_typed_node(graph_, Ntype_op::And);
    for (const auto& p : pins) {
      gu::setup_sink_pid(n, 0).connect_driver(p);
    }
    inherit_color(n, pins.front());
    return ands_.emplace(key, stamped(n.create_driver_pin(0))).first->second;
  }

  Pin either(const Pin& a, const Pin& b) {
    const auto key = std::to_string(pin_id(a)) + '|' + std::to_string(pin_id(b));
    if (const auto it = ors_.find(key); it != ors_.end()) {
      return it->second;
    }
    auto n = gu::create_typed_node(graph_, Ntype_op::Or);
    gu::setup_sink_pid(n, 0).connect_driver(a);
    gu::setup_sink_pid(n, 0).connect_driver(b);
    inherit_color(n, a);
    return ors_.emplace(key, stamped(n.create_driver_pin(0))).first->second;
  }

  // reset == 0 as a 1-bit condition (the active level of a negative reset).
  Pin negated(const Pin& p) {
    const auto id = pin_id(p);
    if (const auto it = nots_.find(id); it != nots_.end()) {
      return it->second;
    }
    auto n = gu::create_typed_node(graph_, Ntype_op::EQ);
    gu::setup_sink_pid(n, 0).connect_driver(p);
    gu::setup_sink_pid(n, 0).connect_driver(gu::create_const(graph_, *Dlop::create_integer(0)));
    inherit_color(n, p);
    return nots_.emplace(id, stamped(n.create_driver_pin(0))).first->second;
  }

  Pin gate(const Pin& clk, const Pin& enable) {
    auto cell = gu::create_typed_node(graph_, Ntype_op::Clock_cell);
    clk.connect_sink(gu::setup_sink_by_name(cell, "clk_ref"));
    enable.connect_sink(gu::setup_sink_by_name(cell, "en"));
    return stamped(cell.create_driver_pin(0));
  }

private:
  hhds::Graph&               graph_;
  std::map<std::string, Pin> ands_, ors_;
  std::map<uint64_t, Pin>    nots_;
};

// Replace the whole enable of `node` by the AND of `conj` (none: always enabled).
void set_enable(Builder& build, const Node& node, const std::vector<Pin>& conj) {
  auto sink = gu::setup_sink_by_name(node, "enable");
  sink.del_sink();
  if (!conj.empty()) {
    sink.connect_driver(build.all_of(conj));
  }
}

void redirect_readers(const Pin& from, const Pin& to) {
  std::vector<Pin> readers;
  for (auto edge : from.out_edges()) {
    readers.push_back(edge.sink);
  }
  for (const auto& sink : readers) {
    sink.del_sink(from);
    to.connect_sink(sink);
  }
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Step 1: collapse
// ---------------------------------------------------------------------------------------------------------------------------------

struct Reg {
  Node             node;
  Pin              clk;    // the clock net it sits on now
  Pin              reset;  // synchronous reset driver (invalid: none)
  bool             negreset = false;
  std::vector<Pin> conj;  // what is left of its enable
  uint32_t         bits  = 0;
  bool             moved = false;
};

// A register this pass may touch, or nullopt.
std::optional<Reg> eligible(const Node& node) {
  if (gu::type_op_of(node) != Ntype_op::Flop) {
    return std::nullopt;
  }
  const auto q = node.get_driver_pin(0);
  if (q.is_invalid() || gu::bits_of(q) <= 0) {
    return std::nullopt;
  }
  const auto clk = gu::get_driver_of_sink_name(node, "clock_pin");
  if (clk.is_invalid() || clk.is_const()) {
    return std::nullopt;
  }
  const auto posclk = gu::get_driver_of_sink_name(node, "posclk");
  if (!posclk.is_invalid() && !const_known_true(posclk)) {
    return std::nullopt;  // a negative edge (or a dynamic polarity): no plain gate fits
  }
  for (const char* name : {"pipe_min", "pipe_max"}) {
    const auto p = gu::get_driver_of_sink_name(node, name);
    if (!p.is_invalid() && !(p.is_const() && gu::const_of(p).is_just_i64() && gu::const_of(p).to_just_i64() == 1)) {
      return std::nullopt;
    }
  }
  Reg r;
  r.node              = node;
  r.clk               = clk;
  r.bits              = static_cast<uint32_t>(gu::bits_of(q));
  const auto async    = gu::get_driver_of_sink_name(node, "async");
  const auto reset    = gu::get_driver_of_sink_name(node, "reset_pin");
  bool       is_async = false;
  if (!async.is_invalid()) {
    if (!async.is_const()) {
      return std::nullopt;  // a dynamic async flag
    }
    is_async = const_known_true(async);
  }
  // An asynchronous reset does not depend on the clock. A synchronous one wins over the enable, so the gate must stay open for it.
  if (!reset.is_invalid() && !is_async) {
    if (reset.is_const() || !one_bit(reset)) {
      return std::nullopt;
    }
    r.reset        = reset;
    const auto neg = gu::get_driver_of_sink_name(node, "negreset");
    if (!neg.is_invalid()) {
      if (!neg.is_const()) {
        return std::nullopt;
      }
      r.negreset = const_known_true(neg);
    }
  }
  auto conj = conjuncts_of(gu::get_driver_of_sink_name(node, "enable"));
  if (!conj) {
    return std::nullopt;
  }
  r.conj = std::move(*conj);
  return r;
}

struct Collapse_counts {
  uint64_t collapsed = 0, duplicates = 0, redundant = 0;
};

// One sweep over the plain Clock_cells of `graph`. True when it changed something.
bool collapse_cells(hhds::Graph& graph, Builder& build, Collapse_counts& counts, const Admit& admit, bool& refused) {
  std::vector<Node> cells;
  for (auto n : graph.body().nodes()) {
    if (!admit_(admit, "clock-gate-collapse-scan")) {
      refused = true;
      return false;
    }
    if (plain_cell(n)) {
      cells.push_back(n);
    }
  }
  bool                       changed = false;
  // Same clock, same enable set: one gate.
  std::map<std::string, Pin> seen;
  for (auto n : cells) {
    if (n.is_invalid()) {
      continue;
    }
    const auto out  = n.get_driver_pin(0);
    const auto clk  = gu::get_driver_of_sink_name(n, "clk_ref");
    const auto en   = gu::get_driver_of_sink_name(n, "en");
    const auto conj = conjuncts_of(en);
    if (out.is_invalid() || !conj) {
      continue;
    }
    // An always-on gate is its clock.
    if (conj->empty()) {
      redirect_readers(out, clk);
      gu::drop_drivers(n);
      n.del_node();
      ++counts.collapsed;
      changed = true;
      continue;
    }
    const auto       ctx = context_of(clk);
    std::vector<Pin> rest;
    for (const auto& c : *conj) {
      if (!ctx.ids.contains(pin_id(c))) {
        rest.push_back(c);
      }
    }
    if (rest.empty()) {
      redirect_readers(out, clk);  // the gates above already check everything this one does: it is the gate that drives it
      gu::drop_drivers(n);
      n.del_node();
      ++counts.collapsed;
      changed = true;
      continue;
    }
    if (ctx.gated) {
      // A gate in a row with something new to check: one level from the root clock with every check in a row ANDed. The same
      // function, one clock gate instead of a cascade (a cascade adds clock latency and the gate above may then lose its reader).
      std::vector<Pin> all = ctx.conj;
      all.insert(all.end(), rest.begin(), rest.end());
      auto clk_sink = gu::setup_sink_by_name(n, "clk_ref");
      clk_sink.del_sink();
      ctx.root.connect_sink(clk_sink);
      auto sink = gu::setup_sink_by_name(n, "en");
      sink.del_sink();
      sink.connect_driver(build.all_of(all));
      ++counts.collapsed;
      changed = true;
    } else if (rest.size() < conj->size()) {
      auto sink = gu::setup_sink_by_name(n, "en");
      sink.del_sink();
      sink.connect_driver(build.all_of(rest));
      ++counts.collapsed;
      changed = true;
    }
    const auto  final_clk  = gu::get_driver_of_sink_name(n, "clk_ref");
    const auto  final_conj = conjuncts_of(gu::get_driver_of_sink_name(n, "en"));
    std::string key        = std::to_string(pin_id(final_clk)) + ':';
    for (const auto& c : final_conj.value_or(rest)) {
      key += std::to_string(pin_id(c)) + ',';
    }
    if (const auto it = seen.find(key); it != seen.end()) {
      redirect_readers(out, it->second);
      gu::drop_drivers(n);
      n.del_node();
      ++counts.duplicates;
      changed = true;
    } else {
      seen.emplace(std::move(key), out);
    }
  }
  return changed;
}

// A register on a gated clock does not re-check what the gates above it check.
void drop_implied_enables(hhds::Graph& graph, Builder& build, Collapse_counts& counts, const Admit& admit, bool& refused) {
  for (auto node : graph.body().nodes()) {
    if (!admit_(admit, "clock-gate-implied-scan")) {
      refused = true;
      return;
    }
    if (gu::type_op_of(node) != Ntype_op::Flop) {
      continue;
    }
    const auto clk = gu::get_driver_of_sink_name(node, "clock_pin");
    if (clk.is_invalid() || clk.is_const() || !plain_cell(clk.get_master_node())) {
      continue;
    }
    const auto conj = conjuncts_of(gu::get_driver_of_sink_name(node, "enable"));
    if (!conj || conj->empty()) {
      continue;
    }
    const auto       ctx = context_of(clk);
    std::vector<Pin> rest;
    for (const auto& c : *conj) {
      if (!ctx.ids.contains(pin_id(c))) {
        rest.push_back(c);
      }
    }
    if (rest.size() < conj->size()) {
      // Exact with or without a reset: the clock only ticks when every gate above is open, and then the dropped conjuncts hold.
      counts.redundant += conj->size() - rest.size();
      set_enable(build, node, rest);
    }
  }
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Step 2: infer
// ---------------------------------------------------------------------------------------------------------------------------------

struct Context {
  Builder&                  build;
  const Clock_gate_options& options;
  Clock_gate_stats&         stats;
  const Admit&              admit;
  bool                      refused = false;
};

// Nodes that only these registers' data inputs consume, walking back to the next register, memory, instance, port or constant.
// The walk stops past a few thousand nodes: a cone that large is worth gating whatever shares it.
uint32_t exclusive_cone(const std::vector<Reg*>& group) {
  constexpr uint32_t       limit = 4096;
  std::set<uint64_t>       regs;
  std::map<uint64_t, Node> cone;
  std::vector<Node>        stack;
  const auto               leaf = [](const Node& n) {
    if (gu::is_graph_input_pin(n.get_driver_pin(0)) || gu::is_type_const(n)) {
      return true;
    }
    switch (gu::type_op_of(n)) {
      case Ntype_op::Flop      :
      case Ntype_op::Latch     :
      case Ntype_op::Memory    :
      case Ntype_op::Sub       :
      case Ntype_op::Clock_cell: return true;
      default                  : return false;
    }
  };
  for (const auto* r : group) {
    regs.insert(static_cast<uint64_t>(r->node.get_debug_nid()));
    const auto din = gu::get_driver_of_sink_name(r->node, "din");
    if (!din.is_invalid() && !din.is_const()) {
      stack.push_back(din.get_master_node());
    }
  }
  while (!stack.empty() && cone.size() < limit) {
    const auto n = stack.back();
    stack.pop_back();
    const auto id = static_cast<uint64_t>(n.get_debug_nid());
    if (leaf(n) || cone.contains(id)) {
      continue;
    }
    cone.emplace(id, n);
    for (auto sink : n.inp_sorted_pins()) {
      for (const auto& d : sink.get_driver_pins()) {
        if (!d.is_invalid() && !d.is_const()) {
          stack.push_back(d.get_master_node());
        }
      }
    }
  }
  // Drop what something outside the cone also reads, and with it everything only that node justified.
  for (bool changed = true; changed;) {
    changed = false;
    for (auto it = cone.begin(); it != cone.end();) {
      bool shared = false;
      for (auto drv : it->second.out_sorted_pins()) {
        for (auto edge : drv.out_edges()) {
          const auto reader = static_cast<uint64_t>(edge.sink.get_master_node().get_debug_nid());
          if (!cone.contains(reader) && !regs.contains(reader)) {
            shared = true;
          }
        }
      }
      if (shared) {
        it      = cone.erase(it);
        changed = true;
      } else {
        ++it;
      }
    }
  }
  return static_cast<uint32_t>(cone.size());
}

// `clk` and `reset_active` are COPIES: callers hand in a register's own `clk`, which this function rewrites as it moves registers.
void gate_level(Context& c, std::vector<Reg*> regs, Pin clk, Pin reset_active, uint32_t level) {
  while (!regs.empty() && !c.refused) {
    // The conjunct covering the most bits (lowest pin id on a tie).
    std::map<uint64_t, std::pair<Pin, uint64_t>> weight;
    for (const auto* r : regs) {
      for (const auto& p : r->conj) {
        auto& slot   = weight[pin_id(p)];
        slot.first   = p;
        slot.second += r->bits;
      }
    }
    uint64_t best_id = 0, best_bits = 0;
    for (const auto& [id, slot] : weight) {
      if (slot.second > best_bits) {
        best_id   = id;
        best_bits = slot.second;
      }
    }
    if (best_bits < c.options.min_bits || best_bits == 0) {
      return;
    }
    std::vector<Reg*> group, rest;
    for (auto* r : regs) {
      const bool has = std::ranges::any_of(r->conj, [&](const Pin& p) { return pin_id(p) == best_id; });
      (has ? group : rest).push_back(r);
    }
    // Everything the whole group shares goes into the gate, not just the conjunct that picked it.
    std::vector<Pin> common = group.front()->conj;
    for (const auto* r : group) {
      std::erase_if(common, [&](const Pin& p) {
        return !std::ranges::any_of(r->conj, [&](const Pin& q) { return pin_id(q) == pin_id(p); });
      });
    }
    if (c.options.min_cone > 0 && exclusive_cone(group) < c.options.min_cone) {
      regs = std::move(rest);  // nothing worth skipping: leave this group on its plain enables
      continue;
    }
    if (!admit_(c.admit, "clock-gate-create", 8)) {
      c.refused = true;
      return;
    }
    Pin enable = c.build.all_of(common);
    if (!reset_active.is_invalid()) {
      enable = c.build.either(enable, reset_active);
    }
    const auto gclk = c.build.gate(clk, enable);
    ++c.stats.gates;
    std::set<uint64_t> shared;
    for (const auto& p : common) {
      shared.insert(pin_id(p));
    }
    for (auto* r : group) {
      std::erase_if(r->conj, [&](const Pin& p) { return shared.contains(pin_id(p)); });
      r->clk   = gclk;
      r->moved = true;
    }
    if (level + 1 < c.options.depth) {
      gate_level(c, group, gclk, reset_active, level + 1);
    }
    regs = std::move(rest);
  }
}

}  // namespace

std::optional<Clock_gate_stats> collapse_clock_gates(std::span<const std::shared_ptr<hhds::Graph>> definitions,
                                                     const Admit&                                  admit) {
  Clock_gate_stats stats;
  for (const auto& graph : definitions) {
    if (!graph) {
      continue;
    }
    Builder         build(*graph);
    Collapse_counts counts;
    bool            refused = false;
    for (int round = 0; round < 4 && collapse_cells(*graph, build, counts, admit, refused) && !refused; ++round) {
    }
    if (!refused) {
      drop_implied_enables(*graph, build, counts, admit, refused);
    }
    if (refused) {
      return std::nullopt;
    }
    stats.collapsed  += counts.collapsed;
    stats.duplicates += counts.duplicates;
    stats.redundant  += counts.redundant;
  }
  return stats;
}

std::optional<Clock_gate_stats> infer_clock_gates(std::span<const std::shared_ptr<hhds::Graph>> definitions,
                                                  const Clock_gate_options& options, const Admit& admit) {
  auto stats = collapse_clock_gates(definitions, admit);
  if (!stats) {
    return std::nullopt;
  }
  if (options.min_bits == 0) {
    return stats;
  }
  for (const auto& graph : definitions) {
    if (!graph) {
      continue;
    }
    std::vector<Reg> regs;
    for (auto node : graph->body().nodes()) {
      if (!admit_(admit, "clock-gate-scan")) {
        return std::nullopt;
      }
      if (auto r = eligible(node); r && !r->conj.empty()) {
        regs.push_back(std::move(*r));
      }
    }
    // One domain: the same clock net and the same synchronous reset behavior.
    std::map<std::string, std::vector<Reg*>> domains;
    for (auto& r : regs) {
      const auto key = std::to_string(pin_id(r.clk)) + '/'
                       + (r.reset.is_invalid() ? std::string{"-"} : std::to_string(pin_id(r.reset)) + (r.negreset ? "n" : "p"));
      domains[key].push_back(&r);
    }
    Builder build(*graph);
    Context ctx{build, options, *stats, admit};
    for (auto& [key, group] : domains) {
      Pin reset_active;
      if (!group.front()->reset.is_invalid()) {
        reset_active = group.front()->negreset ? build.negated(group.front()->reset) : group.front()->reset;
      }
      gate_level(ctx, group, group.front()->clk, reset_active, 0);
      if (ctx.refused) {
        return std::nullopt;
      }
    }
    for (auto& r : regs) {
      if (!r.moved) {
        continue;
      }
      auto sink = gu::setup_sink_by_name(r.node, "clock_pin");
      sink.del_sink();
      r.clk.connect_sink(sink);
      if (r.conj.empty()) {
        ++stats->enables;
      }
      set_enable(build, r.node, r.conj);
      ++stats->registers;
      stats->bits += r.bits;
    }
  }
  return stats;
}

}  // namespace livehd::clock_gate
