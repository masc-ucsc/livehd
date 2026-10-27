// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gated_fold.hpp"

#include <algorithm>
#include <format>
#include <optional>

#include "absl/container/flat_hash_set.h"
#include "attr_carry.hpp"
#include "cell.hpp"
#include "clock_gates.hpp"
#include "latch_contract.hpp"
#include "node_util.hpp"

namespace livehd::synth {
namespace {
namespace gu = graph_util;

// One level of the instance chain a trace descends: `graph` is the body being
// read and `inst` the Sub that instantiates it in the previous level's graph
// (invalid at level 0, the flat design top).
struct Scope {
  hhds::Graph*     graph = nullptr;
  hhds::Node_class inst;
};
using Chain = std::vector<Scope>;

// A pin read at some level of a chain.
struct Ref {
  hhds::Pin_class pin;
  size_t          level = 0;
};

struct Trace {
  std::string        refusal;   // a reason code; empty on success
  std::string        kind;      // the gate kind, empty for an ungated clock
  Ref                root;      // a graph input of its level (ungated) or the gate's reference clock
  std::optional<Ref> enable;    // the gate's cycle-level enable (absent: ungated)
  Chain              chain;     // the scopes the refs point into
  hhds::Node_class   boundary;  // the level-0 gate instance or enable latch (droppable once unread)
};

bool has_body(const std::shared_ptr<hhds::Graph>& def) {
  if (!def) {
    return false;
  }
  for ([[maybe_unused]] auto n : def->body().nodes()) {
    return true;
  }
  return false;
}

hhds::Pin_class peel(hhds::Pin_class p) { return clock_root(p, {}); }

bool const_value(const hhds::Pin_class& p, bool value) {
  if (!p.is_const()) {
    return false;
  }
  const auto c = gu::const_of(p);
  return value ? (c.is_just_i64() && (c.to_just_i64() & 1) != 0) : c.is_known_false();
}

// The parent-level driver of instance input port `name`.
hhds::Pin_class instance_driver(const hhds::Node_class& inst, std::string_view name) {
  const auto io = inst.get_subnode_io();
  if (!io || !io->has_input(name)) {
    return {};
  }
  const auto pid = io->get_input_port_id(name);
  for (const auto& sink : inst.inp_sorted_pins()) {
    if (sink.get_port_id() == pid) {
      return sink.get_driver_pin();
    }
  }
  return {};
}

std::string_view instance_cell_name(const hhds::Node_class& inst) {
  const auto io = inst.get_subnode_io();
  return io ? std::string_view{io->get_name()} : std::string_view{};
}

class Tracer {
public:
  Tracer(hhds::Graph* top, const Gated_fold_options& options) : top_(top), options_(options) {}

  // The clock pin's gate, resolved at level 0.
  Trace trace(const hhds::Pin_class& clock) {
    Chain chain{
        {top_, {}}
    };
    return trace_at(clock, chain, 0);
  }

private:
  // `p` is read at chain level `level` (chain.size() == level + 1).
  Trace trace_at(hhds::Pin_class p, Chain chain, int depth) {
    Trace t;
    t.chain = chain;
    if (depth > 16) {
      t.refusal = "gated-chain";
      return t;
    }
    const size_t level = chain.size() - 1;
    p                  = peel(p);
    if (p.is_invalid() || p.is_const()) {
      t.refusal = "constant-clock";
      return t;
    }
    // Identities and inversions (Not, x == 0, Mux(s, 0, 1), width masks).
    if (const auto cr = latch_contract::control_root(p, /*stop_at_clock_cell=*/true); !cr.net.is_invalid()) {
      if (cr.inverted) {
        t.refusal = "inverted-clock";
        return t;
      }
      if (!cr.net.is_const()) {
        p = cr.net;
      }
    }
    if (gu::is_graph_input_pin(p)) {
      t.root = {p, level};
      return t;  // ungated at this level
    }
    auto       node = p.get_master_node();
    const auto op   = gu::type_op_of(node);
    if (op == Ntype_op::Sub) {
      const auto def = node.get_subnode_graph();
      const auto io  = node.get_subnode_io();
      if (!io) {
        t.refusal = "derived-clock";
        return t;
      }
      if (!has_body(def)) {
        return liberty_icg(node, chain, depth);
      }
      hhds::Pin_class inner;
      for (const auto& decl : io->get_output_pin_decls()) {
        if (decl.port_id == p.get_port_id()) {
          inner = def->get_output_pin(decl.name).get_driver_pin();
        }
      }
      if (inner.is_invalid()) {
        t.refusal = "derived-clock";
        return t;
      }
      auto deeper = chain;
      deeper.push_back({def.get(), node});
      auto inside = trace_at(inner, deeper, depth + 1);
      if (!inside.refusal.empty()) {
        return inside;
      }
      // The root must leave the definition through one of its inputs.
      if (inside.root.level != level + 1 || !gu::is_graph_input_pin(inside.root.pin)) {
        inside.refusal = "derived-clock";
        return inside;
      }
      const auto outside = instance_driver(node, gu::pin_name_of(inside.root.pin));
      if (outside.is_invalid()) {
        inside.refusal = "derived-clock";
        return inside;
      }
      if (!inside.enable) {
        return trace_at(outside, chain, depth + 1);  // a feed-through clock
      }
      auto ref = trace_at(outside, chain, depth + 1);
      if (!ref.refusal.empty()) {
        return ref;
      }
      if (ref.enable) {
        ref.refusal = "gated-chain";
        return ref;
      }
      inside.root = ref.root;
      if (level == 0) {
        inside.boundary = node;
      }
      return inside;
    }
    if (op == Ntype_op::And || op == Ntype_op::Or) {
      return gate(node, op == Ntype_op::Or, chain, depth);
    }
    if (op == Ntype_op::Clock_cell) {
      t.refusal = "divided-clock";  // plain cells were expanded to latch + AND before partitioning; any left divides or inverts
      return t;
    }
    if (op == Ntype_op::Flop || op == Ntype_op::Latch) {
      t.refusal = "divided-clock";  // a register output used as a clock (a divider or a generated clock)
      return t;
    }
    t.refusal = "derived-clock";
    return t;
  }

  Trace liberty_icg(const hhds::Node_class& inst, const Chain& chain, int depth) {
    Trace      t;
    const auto name = instance_cell_name(inst);
    const auto cell
        = std::find_if(options_.icg_cells.begin(), options_.icg_cells.end(), [&](const auto& c) { return c.name == name; });
    if (cell == options_.icg_cells.end()) {
      t.chain   = chain;
      t.refusal = "derived-clock";
      return t;
    }
    t = trace_at(instance_driver(inst, cell->clk), chain, depth + 1);
    if (!t.refusal.empty()) {
      return t;
    }
    if (t.enable) {
      t.refusal = "gated-chain";
      return t;
    }
    const auto en = instance_driver(inst, cell->en);
    if (en.is_invalid()) {
      t.refusal = "derived-clock";
      return t;
    }
    t.kind   = "icg-cell";
    t.enable = Ref{en, chain.size() - 1};
    if (chain.size() == 1) {
      t.boundary = inst;
    }
    icg_test_ = cell->test.empty() ? hhds::Pin_class{} : instance_driver(inst, cell->test);
    return t;
  }

public:
  // The test-enable driver of the last Liberty ICG trace (OR-ed into the enable).
  hhds::Pin_class icg_test_;

private:
  // `clk & S` (S a latch transparent while clk is low, or a negedge flop on
  // clk), or `clk | ~L` (L transparent while clk is high).
  Trace gate(const hhds::Node_class& node, bool is_or, const Chain& chain, int depth) {
    Trace t;
    t.chain = chain;
    std::vector<hhds::Pin_class> live;
    for (const auto& sink : node.inp_sorted_pins()) {
      for (const auto& drv : sink.get_driver_pins()) {
        if (drv.is_const()) {
          if (!const_value(drv, !is_or)) {
            t.refusal = "constant-clock";
            return t;
          }
          continue;
        }
        live.push_back(drv);
      }
    }
    if (live.size() != 2 || std::max(1, gu::real_width(node.get_driver_pin(0))) != 1) {
      t.refusal = "gated-unsafe";
      return t;
    }
    for (int s = 0; s < 2; ++s) {
      // The enable storage through identities and inversions (Not, x == 0):
      // AND reads it true, OR reads it complemented.
      const auto sr          = latch_contract::control_root(live[s], /*stop_at_clock_cell=*/true);
      const auto storage_pin = sr.net;
      if (storage_pin.is_invalid() || storage_pin.is_const() || gu::is_graph_input_pin(storage_pin)
          || storage_pin.get_port_id() != 0 || sr.inverted != is_or) {
        continue;
      }
      const auto storage = storage_pin.get_master_node();
      const auto sop     = gu::type_op_of(storage);
      if (sop != Ntype_op::Latch && sop != Ntype_op::Flop) {
        continue;
      }
      const auto other   = live[1 - s];
      // The storage samples on the opposite phase of the gate's own clock.
      const auto control = gu::get_driver_of_sink_name(storage, sop == Ntype_op::Latch ? "enable" : "clock_pin");
      const auto cr      = latch_contract::control_root(control, /*stop_at_clock_cell=*/true);
      const auto gr      = latch_contract::control_root(other, /*stop_at_clock_cell=*/true);
      if (cr.net.is_invalid() || gr.net.is_invalid() || !(cr.net == gr.net)) {
        t.refusal = "gated-mixed";  // the enable storage is not sampled by the gate's own clock
        return t;
      }
      if (std::max(1, gu::real_width(gr.net)) != 1) {
        t.refusal = "multibit-clock";  // the bits of a clock bus the storage and the gate read are not tracked
        return t;
      }
      if (gr.inverted) {
        t.refusal = "inverted-clock";  // the gate passes the inverted clock (the falling edge)
        return t;
      }
      const auto posclk = gu::get_driver_of_sink_name(storage, "posclk");
      // Transparent while the clock is low (a latch) or a negative edge (a flop),
      // with the control's own inversion folded in.
      const bool low    = (posclk.is_const() && gu::const_of(posclk).is_known_false()) != cr.inverted;
      for (std::string_view pin : {"reset_pin", "initial"}) {
        if (!gu::get_driver_of_sink_name(storage, pin).is_invalid()) {
          t.refusal = "gated-unsafe";  // an enable latch with a reset or power-on value is not a plain gate
          return t;
        }
      }
      hhds::Pin_class en;
      if (sop == Ntype_op::Latch) {
        if (is_or == low) {
          t.refusal = "gated-unsafe";  // AND needs a latch transparent while the clock is low, OR one while it is high
          return t;
        }
        en = latch_contract::latch_transparent_arm(storage);
        if (en.is_invalid()) {
          en = gu::get_driver_of_sink_name(storage, "din");
        }
        t.kind = is_or ? "latch-or" : "latch-and";
      } else {
        const auto flop_en = gu::get_driver_of_sink_name(storage, "enable");
        if (is_or || !low || (!flop_en.is_invalid() && !const_value(flop_en, true))) {
          t.refusal = "gated-unsafe";  // only a negative-edge flop ANDed with its clock is glitch-safe
          return t;
        }
        en     = gu::get_driver_of_sink_name(storage, "din");
        t.kind = "negedge-and";
      }
      if (en.is_invalid()) {
        t.refusal = "gated-unsafe";
        return t;
      }
      auto ref = trace_at(other, chain, depth + 1);
      if (!ref.refusal.empty()) {
        return ref;
      }
      if (ref.enable) {
        ref.refusal = "gated-chain";
        return ref;
      }
      t.root   = ref.root;
      t.enable = Ref{en, chain.size() - 1};
      if (chain.size() == 1 && sop == Ntype_op::Latch) {
        t.boundary = storage;
      }
      return t;
    }
    t.refusal = "gated-unsafe";  // an AND/OR clock without a phase-safe enable storage
    return t;
  }

  hhds::Graph*              top_;
  const Gated_fold_options& options_;
};

// Clones a combinational cone read at some chain level into the top, through
// definition inputs onto the instances' drivers. admit() walks the cones first
// without creating anything and fails on state, hierarchy or the node limit,
// so a refused gate leaves the top untouched; clone() then cannot fail.
class Cloner {
public:
  Cloner(hhds::Graph* top, const Chain& chain, int32_t color, uint32_t limit)
      : top_(top), chain_(chain), color_(color), limit_(limit) {}

  // Whether `d` (read at `level`) can be cloned, sharing the node limit with
  // every cone admitted before it.
  bool admit(const hhds::Pin_class& d, size_t level, int depth = 0) {
    if (d.is_invalid() || depth > 128) {
      return false;
    }
    if (d.is_const() || level == 0 || !seen_.insert(std::pair{level, d}).second) {
      return true;
    }
    if (gu::is_graph_input_pin(d)) {
      const auto drv = instance_driver(chain_[level].inst, gu::pin_name_of(d));
      return !drv.is_invalid() && admit(drv, level - 1, depth + 1);
    }
    const auto n  = d.get_master_node();
    const auto op = gu::type_op_of(n);
    if (Ntype::is_loop_last(op) || op == Ntype_op::IO || op == Ntype_op::Sub || ++nodes_ > limit_) {
      return false;
    }
    for (const auto& sink : n.inp_sorted_pins()) {
      for (const auto& drv : sink.get_driver_pins()) {
        if (!admit(drv, level, depth + 1)) {
          return false;
        }
      }
    }
    return true;
  }

  // Clones an admitted cone. The copies keep their source attributes
  // (signedness, LUT tables, ...) but not the definition's names.
  hhds::Pin_class clone(const hhds::Pin_class& d, size_t level) {
    if (d.is_const()) {
      return gu::create_const(*top_, gu::const_of(d));
    }
    if (level == 0) {
      return d;  // already a top-level net
    }
    const auto key = std::pair{level, d};
    if (auto it = cache_.find(key); it != cache_.end()) {
      return it->second;
    }
    if (gu::is_graph_input_pin(d)) {
      auto res    = clone(instance_driver(chain_[level].inst, gu::pin_name_of(d)), level - 1);
      cache_[key] = res;
      return res;
    }
    const auto n   = d.get_master_node();
    auto       neo = gu::create_typed_node(*top_, gu::type_op_of(n));
    gu::carry_node_attrs(n, neo);
    neo.attr(hhds::attrs::name).del();
    gu::set_color(neo, color_);
    auto res    = neo.create_driver_pin(d.get_port_id());
    cache_[key] = res;
    gu::carry_pin_attrs(d, res);
    res.attr(livehd::attrs::pin_name).del();
    for (auto sink : n.inp_pins_snapshot()) {
      auto sp = neo.create_sink_pin(sink.get_port_id());
      gu::carry_pin_attrs(sink, sp);
      sp.attr(livehd::attrs::pin_name).del();
      for (const auto& drv : sink.get_driver_pins()) {
        clone(drv, level).connect_sink(sp);
      }
    }
    gu::set_bits(res, std::max(1, static_cast<int>(gu::bits_of(d))));
    return res;
  }

private:
  hhds::Graph*                                                             top_;
  Chain                                                                    chain_;
  int32_t                                                                  color_;
  uint32_t                                                                 limit_;
  uint32_t                                                                 nodes_ = 0;
  absl::flat_hash_set<std::pair<size_t, hhds::Pin_class>>                  seen_;
  absl::flat_hash_map<std::pair<size_t, hhds::Pin_class>, hhds::Pin_class> cache_;
};

hhds::Pin_class and2(hhds::Graph& g, const hhds::Pin_class& a, const hhds::Pin_class& b, int32_t color) {
  auto n = gu::create_typed_node(g, Ntype_op::And);
  gu::set_color(n, color);
  a.connect_sink(gu::setup_sink_pid(n, 0));
  b.connect_sink(gu::setup_sink_pid(n, 0));
  auto out = n.create_driver_pin(0);
  gu::set_ubits(out, 1);
  return out;
}

hhds::Pin_class or2(hhds::Graph& g, const hhds::Pin_class& a, const hhds::Pin_class& b, int32_t color) {
  auto n = gu::create_typed_node(g, Ntype_op::Or);
  gu::set_color(n, color);
  a.connect_sink(gu::setup_sink_pid(n, 0));
  b.connect_sink(gu::setup_sink_pid(n, 0));
  auto out = n.create_driver_pin(0);
  gu::set_ubits(out, 1);
  return out;
}

// A 1-bit view of a possibly wider enable: bit 0, as the latch/flop sampled it.
hhds::Pin_class bit0(hhds::Graph& g, const hhds::Pin_class& p, int32_t color) {
  if (p.is_const() || std::max(1, gu::real_width(p)) == 1) {
    return p;
  }
  auto mask = gu::create_get_mask(g, p, 0);
  gu::set_color(mask, color);
  auto out = mask.create_driver_pin(0);
  gu::set_ubits(out, 1);
  return out;
}
}  // namespace

Gated_fold_result fold_gated_clocks(hhds::Graph* flat, const Gated_fold_options& options) {
  Gated_fold_result result;
  if (!flat || options.domain_clock.empty()) {
    return result;
  }
  std::vector<hhds::Node_class> flops;
  int32_t                       fresh = 0;
  for (auto n : flat->body().nodes()) {
    fresh = std::max(fresh, gu::node_color_of(n));
    if (gu::type_op_of(n) == Ntype_op::Flop) {
      flops.push_back(n);
    }
  }
  // One cloned enable per gate (its output pin), in a color of its own: it
  // partitions apart and reaches every consumer region as one input, the
  // stage's single enable rail, computed once for all of them.
  absl::flat_hash_map<hhds::Pin_class, hhds::Pin_class> enables;
  absl::flat_hash_map<hhds::Pin_class, std::string>     failures;  // gates whose enable cannot be cloned
  Tracer                                                tracer(flat, options);
  for (const auto& flop : flops) {
    const auto clock = gu::get_driver_of_sink_name(flop, "clock_pin");
    if (clock.is_invalid() || clock.is_const()) {
      continue;
    }
    const auto direct = peel(clock);
    if (gu::is_graph_input_pin(direct)) {
      continue;  // an ungated design clock: nothing to fold
    }
    auto& hint        = result.registers[flop];
    tracer.icg_test_  = {};
    auto       t      = tracer.trace(clock);
    const auto refuse = [&](std::string code) {
      ++result.refused[code];
      hint.code = std::move(code);
    };
    if (!t.refusal.empty()) {
      refuse(t.refusal);
      continue;
    }
    if (!t.enable) {
      refuse("derived-clock");  // a feed-through that leaves no gate: not a top input either
      continue;
    }
    if (!options.fall_sample && (t.kind == "negedge-and" || t.kind == "latch-or")) {
      refuse("gated-fall-sample");
      continue;
    }
    if (t.root.level != 0 || !gu::is_graph_input_pin(t.root.pin)) {
      refuse("derived-clock");
      continue;
    }
    if (gu::pin_name_of(t.root.pin) != options.domain_clock) {
      refuse("clock-domain");
      continue;
    }
    // The root and the register's clock are one bit wide: a bit of a clock
    // bus is not tracked through the gate.
    if (std::max(1, gu::real_width(t.root.pin)) != 1 || std::max(1, gu::real_width(clock)) != 1) {
      refuse("multibit-clock");
      continue;
    }
    // Only registers the domino profile could convert fold; the rest keep
    // their gate untouched (and their own retention reason).
    const auto posclk = gu::get_driver_of_sink_name(flop, "posclk");
    if (posclk.is_const() && gu::const_of(posclk).is_known_false()) {
      refuse("negative-edge");
      continue;
    }
    const auto reset   = gu::get_driver_of_sink_name(flop, "reset_pin");
    const auto initial = gu::get_driver_of_sink_name(flop, "initial");
    if (!initial.is_invalid() && reset.is_invalid()) {
      refuse("initial-value");
      continue;
    }
    // A synchronous reset outranks the enable (rst ? v : en ? d : Q) but, on
    // the gated clock, acts only on an enabled edge. On the root clock it
    // would reset with the gate closed, and keeping it inside the hold would
    // put the reset network in the stage: the register keeps its gate.
    if (!reset.is_invalid() && !const_value(reset, false)) {
      const auto async = gu::get_driver_of_sink_name(flop, "async");
      if (!async.is_const() || gu::const_of(async).is_known_false()) {
        refuse("gated-sync-reset");
        continue;
      }
    }
    const auto color = gu::node_color_of(flop);
    const auto key   = peel(clock);
    if (const auto failed = failures.find(key); failed != failures.end()) {
      refuse(failed->second);  // the gate's enable was already refused for another register
      continue;
    }
    auto en_it = enables.find(key);
    if (en_it == enables.end()) {
      const auto own  = fresh + 1;
      const bool test = !tracer.icg_test_.is_invalid() && !const_value(tracer.icg_test_, false);
      Cloner     cloner(flat, t.chain, own, options.max_enable_nodes);
      if (!cloner.admit(t.enable->pin, t.enable->level) || (test && !cloner.admit(tracer.icg_test_, t.enable->level))) {
        failures.emplace(key, "gated-enable-state");
        refuse("gated-enable-state");
        continue;
      }
      auto en = cloner.clone(t.enable->pin, t.enable->level);
      if (test) {
        en = or2(*flat, en, cloner.clone(tracer.icg_test_, t.enable->level), own);
      }
      // A design input already reaches every region as one input; any other
      // net gets an identity of its own color, so it partitions apart.
      en = bit0(*flat, en, own);
      if (!gu::is_graph_input_pin(en) && !en.is_const() && gu::node_color_of(en.get_master_node()) != own) {
        en = and2(*flat, en, gu::create_const(*flat, *Dlop::create_integer(1)), own);
      }
      if (!en.is_const() && !gu::is_graph_input_pin(en)) {
        ++fresh;
      }
      en_it = enables.emplace(key, en).first;
    }
    auto       enable = en_it->second;
    const auto old_en = gu::get_driver_of_sink_name(flop, "enable");
    if (!old_en.is_invalid() && !const_value(old_en, true)) {
      enable = and2(*flat, old_en, enable, color);
    }
    auto en_sink = gu::setup_sink_by_name(flop, "enable");
    en_sink.del_sink();
    en_sink.connect_driver(enable);
    // The root stays visible on the clock pin without changing it: an AND
    // gate's output is root & en (so root & gclk == gclk), an OR gate's is
    // root | ~en (so root | gclk == gclk).
    const auto gclk     = t.kind == "latch-or" ? or2(*flat, t.root.pin, clock, color) : and2(*flat, t.root.pin, clock, color);
    auto       clk_sink = gu::setup_sink_by_name(flop, "clock_pin");
    clk_sink.del_sink();
    clk_sink.connect_driver(gclk);
    hint.code   = "fold";
    hint.root   = t.root.pin;
    hint.gclk   = clock;
    hint.enable = en_it->second;
    hint.gate   = t.boundary;
    ++result.gates[t.kind];
  }
  return result;
}

}  // namespace livehd::synth
