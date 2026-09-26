//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "bitwidth.hpp"
#include "bitwidth_rewrite.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "bitwidth_range.hpp"
#include "diag.hpp"
#include "hhds/attrs/srcid.hpp"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "mask_eval.hpp"
#include "node_util.hpp"
#include "pass_bitwidth.hpp"
#include "perf_tracing.hpp"
#include "split_selfref.hpp"

// "sbits not constrained/known" sentinel. Bits are a plain int32 attr now
// (livehd::attrs::bits); the old packed-format width cap is gone.
static constexpr int32_t Bits_unknown = std::numeric_limits<int32_t>::max();

using livehd::graph_util::bits_of;
using livehd::graph_util::create_const;
using livehd::graph_util::create_typed_node;
using livehd::graph_util::debug_name;
using livehd::graph_util::find_sink_pin;
using livehd::graph_util::get_driver_of_sink_name;
using livehd::graph_util::is_graph_input_pin;
using livehd::graph_util::is_graph_output_pin;
using livehd::graph_util::is_sink_connected;
using livehd::graph_util::is_type_flop;
using livehd::graph_util::set_bits;
using livehd::graph_util::set_sbits;
using livehd::graph_util::set_sign;
using livehd::graph_util::set_type_op;
using livehd::graph_util::set_ubits;
using livehd::graph_util::set_unsign;
using livehd::graph_util::setup_sink_by_name;
using livehd::graph_util::type_op_of;
using livehd::graph_util::wire_name;

namespace {

using livehd::graph_util::const_of;

bool is_finite_low_mask(const Dlop& value) {
  return value.is_integer() && !value.has_unknowns() && !value.is_negative() && !value.is_known_zero()
         && value.eq_op(*Dlop::get_mask_value(value.get_last_bit_set() + 1))->is_known_true();
}

// These cells have a complete value-range rule below.  Their pin `bits`
// attribute is an output cache/materialization hint, not a finite-width
// operation contract: LNAST and LGraph arithmetic is signed and unbounded.
// Frontends may conservatively pre-stamp it (Slang commonly uses the width of
// an enclosing expression), but feeding that stamp back as an input constraint
// prevents bitwidth from discovering the actual range and leaves generated
// Slop operations wider than their consumers.
//
// Cells absent from this list intentionally retain their pre-stamped width.
// In particular LUT has a finite truth-table width, Rem may explicitly
// reduce precision, and Clock_cell is timing metadata rather than arithmetic.
constexpr bool infer_internal_range(Ntype_op op) {
  switch (op) {
    case Ntype_op::Sum     :
    case Ntype_op::Mult    :
    case Ntype_op::Div     :
    case Ntype_op::And     :
    case Ntype_op::Or      :
    case Ntype_op::Xor     :
    case Ntype_op::Rxor    :
    case Ntype_op::Popcount:
    case Ntype_op::Ror     :
    case Ntype_op::Not     :
    case Ntype_op::Get_mask:
    case Ntype_op::Set_mask:
    case Ntype_op::Concat  :
    case Ntype_op::Sext    :
    case Ntype_op::LT      :
    case Ntype_op::GT      :
    case Ntype_op::EQ      :
    case Ntype_op::SHL     :
    case Ntype_op::SRA     :
    case Ntype_op::Mux     :
    case Ntype_op::Hotmux  :
    case Ntype_op::Memory  :
    case Ntype_op::Sub     :
    case Ntype_op::AttrSet : return true;
    default                : return false;
  }
}

// Drop the driver feeding a sink pin. One driver per sink pin (graph/cell.hpp),
// so this is a single disconnect, not the edge sweep it used to be.
void clear_sink(const hhds::Pin_class& spin) {
  if (spin.is_invalid()) {
    return;
  }
  spin.del_sink();
}

// Every consumer SINK of `node`, materialized.
//
// Two reasons this exists rather than being inlined at each call site:
//
//  1. SAFETY. The callers below reconnect the fan-out and then delete the node,
//     i.e. they mutate the very edge storage they are walking. A node's driver
//     fan-out is a LAZY view over that storage, so reading it while rewiring it
//     is a use-after-free waiting to happen -- the loops used to do exactly
//     that, under a comment claiming the live walk was deliberate. Taking the
//     snapshot first is what makes "live-reconnect" honest.
//
//  2. It is the pin-centric spelling: the node's DRIVER PINS, then each pin's
//     own fan-out. That yields the sinks in the same order the old node-level
//     out_edges() walk did (driver pins ascending, each pin's edges in storage
//     order), because both walk the same two loops.
//
// A driver's fan-out is genuinely a SET -- clock and reset reach 100K+ sinks --
// so there is no single-sink reader to use here, and materializing is the point.
std::vector<hhds::Pin_class> consumer_sinks(const hhds::Node_class& node) {
  std::vector<hhds::Pin_class> sinks;
  for (const auto& out_pin : node.out_sorted_pins()) {
    for (const auto& e : out_pin.out_edges()) {
      sinks.push_back(e.sink);
    }
  }
  return sinks;
}

}  // namespace

Bitwidth::Bitwidth(int _max_iterations, bool _constrain_outputs)
    : max_iterations(_max_iterations), constrain_outputs(_constrain_outputs) {}

void Bitwidth::do_trans(const std::shared_ptr<hhds::Graph>& g) {
  if (!g) {
    return;
  }

  auto gio  = g->get_io();
  auto name = gio ? std::string{gio->get_name()} : std::string{};

  TRACE_EVENT("pass", nullptr, [&name](perfetto::EventContext ctx) {
    std::string converted_str{(char)('A' + (trace_module_cnt++ % 25))};
    ctx.event()->set_name(absl::StrCat(converted_str, name));
  });

#ifndef NDEBUG
  // Internal hints may be stale after cprop, or absent after bitfuzz. They
  // are outputs of this pass, not preconditions for entering inference.
  // Literal metadata remains independently checkable against its value.
  for (const auto& pin : g->get_constant_node().out_pins()) {
    livehd::graph_util::debug_check_const_pin(pin);
  }
#endif

  // A sized to-positive boundary captures a finite bus, not the future
  // significant width of its producer. Make that window explicit before
  // inference can narrow a signed producer (e.g. a 4-bit mux of 0 and -1).
  for (auto node : g->body().nodes(hhds::Node_order::forward)) {
    if (type_op_of(node) != Ntype_op::Get_mask) {
      continue;
    }
    const auto output = node.get_driver_pin(0);
    const auto mask   = get_driver_of_sink_name(node, "mask");
    if (bits_of(output) > 0 && mask.is_const() && const_of(mask).is_just_i64() && const_of(mask).to_just_i64() == -1) {
      clear_sink(find_sink_pin(node, "mask"));
      setup_sink_by_name(node, "mask").connect_driver(create_const(*g, *Dlop::get_mask_value(bits_of(output))));
    }
  }
  bw_pass(g.get());
}

// The range of a boolean-producing cell (a comparator, a reduce-OR).
//
// It is the ZERO-EXTENDED boolean {0, 1} -- NOT the 1-bit signed {-1, 0}. That
// is the same convention process_get_mask spells out for a single-bit slice
// ("a set bit is the unsigned 1; only `#sext` may be negative"), and it is what
// every front end emits: tolg/cgen declare a comparator result `reg [1:0]`,
// i.e. 2 signed bits marked unsigned.
//
// Modelling it as [-1, 0] made the value NEGATIVE, and a boolean is routinely
// SHIFTED into position: `(a==1) << 1` is 2 under the real convention and -2
// under the signed one, whose sign extension sets every bit above it. A one-hot
// selector assembled that way (`e0 | e1<<1 | e2<<2 | ...`, the shape `match`
// lowers to) came out with several bits set, and pass.formal correctly REFUTED
// its own design's one-hotness -- five corpus designs failed to compile the
// moment pass.bitfuzz made this path run.
void Bitwidth::set_bw_1bit(hhds::Pin_class dpin) {
  if (dpin.is_invalid()) {
    return;
  }
  auto [it, inserted] = bwmap.insert_or_assign(dpin.get_class_index(), Bitwidth_range(0, 1));
  set_bits_sign(dpin, it->second);
}

void Bitwidth::set_bits_sign(const hhds::Pin_class& dpin, const Bitwidth_range& bw) {
  if (dpin.is_invalid()) {
    return;
  }
  // A DECLARED graph-IO port is the source RTL interface: leave it exactly as
  // declared, width AND sign.
  //
  // The old guard tested `bits_of(dpin)` -- the PIN attr -- but a port's
  // declaration lives on the GraphIO decl, not the pin (node_util.hpp
  // bits_of(pin, gio, name)), and IO pins normally carry no pin attrs at all.
  // So it was inert on the FIRST write: inference stamped its own width and
  // sign onto the port, the guard then dutifully preserved that, and bw_pass
  // re-seeds from the pin attrs at the top of the next iteration -- feeding an
  // inferred range back in as if it were the declaration. A port narrowed that
  // way silently truncates everything downstream (a 4-bit signed `[-8..7]`
  // input re-read as `[0..3]` collapsed the Sext that carried its sign
  // extension, since process_sext bypasses a Sext whose source already fits).
  if (is_graph_input_pin(dpin) || is_graph_output_pin(dpin)) {
    if (const auto* gio = current_graph ? current_graph->get_io().get() : nullptr; gio != nullptr) {
      const auto nm = livehd::graph_util::pin_name_of(dpin);
      if (!nm.empty() && gio->get_bits(nm) != 0) {
        return;
      }
    }
    // No declared width (the front end left the port unsized): inference is
    // allowed to size it, and the stamped value is all any reader has.
  }
  const bool positive = bw.is_always_positive();
  auto       b        = positive ? bw.get_ubits() : bw.get_sbits();
  // CELL CONTRACT ceiling. A finite Get_mask CLEARS every unselected bit, so
  // its result never needs more than popcount(mask) bits -- no matter what a
  // CONSUMER's back-propagation suggests (process_flop stamps the flop's merged
  // Q range onto every din driver, and a memory does the same for its dout
  // drivers). Stamping past it mints a pin node_util's debug_check_pin_hint
  // rejects on the next load ("selects 64 bits but is hinted at 75"), which
  // aborts a dbg `lhd sim` on the saved lg:. Get_mask always returns unsigned;
  // a consumer's signed range must not reinterpret the selected top bit.
  if (auto master = dpin.get_master_node(); !master.is_invalid() && type_op_of(master) == Ntype_op::Get_mask) {
    auto mask = get_driver_of_sink_name(master, "mask");
    if (mask.is_const()) {
      const auto& mv = const_of(mask);
      if (!mv.is_negative() && !mv.has_unknowns()) {
        const auto capacity = static_cast<int32_t>(mv.popcount());
        if (capacity > 0 && b > capacity) {
          b = capacity;
        }
      }
    }
  }
  // A computed value used only by declared module ports is observed modulo
  // their widest bus. Keep its full mathematical range in bwmap, but do not
  // materialize unobservable high bits (e.g. a 1024-bit SRA driving out:u64).
  // An internal consumer or an unsized output still needs the inferred range.
  // Concat's width describes its lane layout; unlike an arithmetic result it
  // cannot be narrowed without rewriting the lanes themselves. State and
  // multi-output cells likewise retain their own realization contracts.
  // Guarded like the Get_mask ceiling above: a driver pin's master can be the
  // IO / CONST singleton (or invalid), whose stored type is not a cell op.
  const auto master = dpin.get_master_node();
  const auto op     = master.is_invalid() ? Ntype_op::Invalid : type_op_of(master);
  if (constrain_outputs && current_graph && infer_internal_range(op) && op != Ntype_op::Concat
      && !Ntype::has_multiple_driver_pins(op)) {
    const auto gio         = current_graph->get_io();
    int32_t    output_bits = 0;
    for (const auto& e : dpin.out_edges()) {
      int32_t declared = 0;
      if (gio && is_graph_output_pin(e.sink)) {
        declared = gio->get_bits(livehd::graph_util::pin_name_of(e.sink));
      } else if (const auto sink = e.sink.get_master_node(); type_op_of(sink) == Ntype_op::Sub && !sink.is_loop_subnode()) {
        // An instance input is the same finite assignment boundary, including
        // Liberty's one-bit inputs fed by ABC's compact shift selectors.
        if (const auto child_io = sink.get_subnode_io()) {
          declared = child_io->get_bits(e.sink.get_pin_name());
        }
      }
      if (declared == 0) {
        output_bits = 0;
        break;
      }
      output_bits = std::max(output_bits, static_cast<int32_t>(declared));
    }
    if (output_bits > 0) {
      b = std::min(b, output_bits);
    }
  }
  if (positive || op == Ntype_op::Get_mask) {
    set_ubits(dpin, b);
  } else {
    set_sbits(dpin, b);
  }
}

void Bitwidth::adjust_bw(hhds::Pin_class dpin, const Bitwidth_range& bw) {
  if (dpin.is_invalid()) {
    return;
  }
  // LiveHD's "single driver" optimisation: if a node has a single driver and
  // a fully resolved (min==max) range, replace the entire input subgraph with
  // a constant. We approximate "single driver" by checking the node has no
  // multi-driver type.
  auto master = dpin.get_master_node();
  auto op     = type_op_of(master);
  // NOTE: this may DELETE dpin's master (the fold below); a caller that keeps
  // using the node afterwards must re-check node.is_invalid().
  if (!not_finished && !bw.is_overflow() && !Ntype::has_multiple_driver_pins(op) && !dpin.is_const()
      && !livehd::graph_util::is_builtin_node(master)) {
    if (bw.get_min().same_repr(bw.get_max())) {
      // Range-proven constant: replace the whole cell by the CONST_NODE pin for
      // that value (the only constant representation): reconnect every consumer
      // to it and bulk-delete the node. The interned const pin is shared across
      // the graph, so it gets no private bits/sign stamp -- its range is seeded
      // into bwmap here (and again by the per-visit constant seeding) and
      // consumers read the exact value. Callers that passed an INPUT driver
      // (process_mux/hotmux selectors) keep a handle that is now invalid; every
      // one of them already tolerates that.
      auto cdpin = create_const(*current_graph, bw.get_min());
      bwmap.insert_or_assign(cdpin.get_class_index(), bw);
      bwmap.erase(dpin.get_class_index());
      // Combining duplicate constant operands can grow the constant pool;
      // preserve sink handles before reconnecting and deleting the old cell.
      for (const auto& sink : consumer_sinks(master)) {
        livehd::graph_util::connect_folded_const(*current_graph, cdpin, sink);
      }
      master.del_node();
      return;
    }
  }

  auto [it, inserted] = bwmap.insert({dpin.get_class_index(), bw});
  if (inserted) {
    set_bits_sign(dpin, bw);
    return;
  }
  it->second.set_wider_range(bw);
  set_bits_sign(dpin, it->second);
}

void Bitwidth::process_flop(hhds::Node_class& node) {
  I(is_sink_connected(node, "din"));
  std::vector<hhds::Pin_class> flop_cpins;

  if (is_sink_connected(node, "din")) {
    flop_cpins.emplace_back(get_driver_of_sink_name(node, "din"));
  }
  if (is_sink_connected(node, "initial")) {
    flop_cpins.emplace_back(get_driver_of_sink_name(node, "initial"));
  }

  Dlop max_val;
  Dlop min_val;

  for (auto& cpin : flop_cpins) {
    auto           it = bwmap.find(cpin.get_class_index());
    Bitwidth_range bw;
    if (it == bwmap.end()) {
      auto bits = bits_of(cpin);
      if (bits) {
        if (livehd::graph_util::is_unsign(cpin)) {
          bw.set_ubits_range(bits);
        } else {
          bw.set_sbits_range(bits);
        }
      } else {
        debug_unconstrained_msg(node, cpin);
        not_finished = true;
        return;
      }
    } else {
      bw = it->second;
    }

    auto a = bw.get_max();
    if (max_val.lt_op(a)->is_known_true()) {
      if (!max_val.is_known_zero()) {
        not_finished = true;
      }
      max_val = a;
    }
    auto b = bw.get_min();
    if (min_val.gt_op(b)->is_known_true()) {
      if (!min_val.is_known_zero()) {
        not_finished = true;
      }
      min_val = b;
    }
  }

  auto           dpin = node.create_driver_pin(0);
  Bitwidth_range bw(min_val, max_val);
  for (auto& cpin : flop_cpins) {
    adjust_bw(cpin, bw);
  }
  adjust_bw(dpin, bw);
}

void Bitwidth::process_ror(hhds::Node_class& node, Inp_pins& inp_edges) {
  I(inp_edges.size());
  set_bw_1bit(node.create_driver_pin(0));
}

void Bitwidth::process_not(hhds::Node_class& node, Inp_pins& inp_edges) {
  I(inp_edges.size());

  // ~x == -x-1 is monotonically DECREASING, so the operand's max maps to the
  // result's MIN and its min to the result's MAX.
  //
  // Seed from the first operand rather than from a default-constructed Dlop.
  // The defaults sit at 0 and were only ever widened outward, so for a
  // non-negative operand (~x always negative) max_val never came down off 0:
  // the range came out as [~pmax, 0] instead of [~pmax, ~pmin], which
  // is_always_positive()/set_bits_sign() then read as UNSIGNED. That single
  // wrong attribute made three consumers disagree about sign extension --
  // the LEC treated the result as non-negative, cgen emitted a correct
  // signed net, and abc zero-filled the widening where it had to sign-extend
  // (`(~ec)#[0..=9]` mapped to 511 instead of 1023).
  Dlop max_val;
  Dlop min_val;
  bool seeded = false;
  for (auto e : inp_edges) {
    auto it = bwmap.find(e.get_driver_pin().get_class_index());
    if (it == bwmap.end()) {
      debug_unconstrained_msg(node, e.get_driver_pin());
      not_finished = true;
      return;
    }
    auto lo = it->second.get_max().not_op();  // ~max is the SMALLEST result
    auto hi = it->second.get_min().not_op();  // ~min is the LARGEST result
    if (!seeded) {
      min_val = lo;
      max_val = hi;
      seeded  = true;
      continue;
    }
    if (lo->lt_op(min_val)->is_known_true()) {
      min_val = lo;
    }
    if (hi->gt_op(max_val)->is_known_true()) {
      max_val = hi;
    }
  }

  adjust_bw(node.create_driver_pin(0), Bitwidth_range(min_val, max_val));
}

void Bitwidth::process_mux(hhds::Node_class& node, Inp_pins& inp_edges) {
  I(inp_edges.size());
  Bitwidth_range bw;

  for (auto e : inp_edges) {
    if (e.get_port_id() == 0) {
      // The selector is an INDEX over the data arms, so its envelope is
      // [0 .. n_data-1] -- non-negative, exactly like process_hotmux's.
      //
      // It used to be [-(n/2) .. n/2-1], i.e. [-1..0] for the two-arm case:
      // the signed 1-bit boolean model that set_bw_1bit also used and that the
      // rest of the stack contradicts. adjust_bw only ever WIDENS, so that
      // negative half got unioned into the comparator driving the selector,
      // its range became [-1..1], is_always_positive() went false, and
      // set_bits_sign stamped `pin_signed` on the comparator's OUTPUT pin.
      // cgen keys the emitted comparison's signedness off exactly that pin
      // (cgen_verilog.cpp `signed_compare = !is_unsign(dpin)`), so an unsigned
      // compare silently became a signed one.
      //
      // With fewer than two CONNECTED data arms (the others undriven: yosys2lg
      // imports an undriven wire as an input-less placeholder Or, which folds
      // away) the envelope degenerates to [0..0]. That says nothing about the
      // selector's VALUE, yet adjust_bw folds any min==max range into a
      // constant for EVERY consumer of the driver: the select of a bmuxmap
      // SRAM read tree (addr[0], shared by every pair mux) was replaced by 0
      // in all of them because the one pair mux over the missing entries of a
      // non-power-of-two array had no data arms left. Only a real envelope
      // is a hint worth recording.
      const auto n_data = inp_edges.size() - 1;
      if (n_data >= 2) {
        Bitwidth_range bw2(0, static_cast<int64_t>(n_data) - 1);
        adjust_bw(e.get_driver_pin(), bw2);
      }
      continue;
    }
    auto it = bwmap.find(e.get_driver_pin().get_class_index());
    if (it == bwmap.end()) {
      debug_unconstrained_msg(node, e.get_driver_pin());
      not_finished = true;
      return;
    }
    bw.set_wider_range(it->second);
  }
  adjust_bw(node.create_driver_pin(0), bw);
}

// Hotmux controls have independent one-bit widths; union only value arms.
void Bitwidth::process_hotmux(hhds::Node_class& node, Inp_pins& inp_edges) {
  I(inp_edges.size());
  Bitwidth_range bw;

  // ONE spelling of the control/value split (the shared helper), never a local
  // `edges/2*2`: a re-derivation classifies pins differently on a gapped cell.
  const auto control_end = livehd::graph_util::hotmux_control_end(node);
  for (auto e : inp_edges) {
    if (livehd::graph_util::is_hotmux_control(e.get_port_id(), control_end)) {
      continue;
    }
    auto it = bwmap.find(e.get_driver_pin().get_class_index());
    if (it == bwmap.end()) {
      debug_unconstrained_msg(node, e.get_driver_pin());
      not_finished = true;
      return;
    }
    bw.set_wider_range(it->second);
  }
  adjust_bw(node.create_driver_pin(0), bw);
}

void Bitwidth::process_shl(hhds::Node_class& node, Inp_pins& inp_edges) {
  I(inp_edges.size() == 2);

  auto a_dpin = get_driver_of_sink_name(node, "a");
  auto a_it   = bwmap.find(a_dpin.get_class_index());

  Bitwidth_range a_bw{};
  if (a_it == bwmap.end()) {
    debug_unconstrained_msg(node, a_dpin);
    not_finished = true;
    return;
  }
  a_bw = a_it->second;

  // SHL b is single-driver (the one-hot multi-shift form was removed).
  auto n_dpin = get_driver_of_sink_name(node, "b");
  auto n_it   = bwmap.find(n_dpin.get_class_index());
  if (unlikely(n_it == bwmap.end())) {
    debug_unconstrained_msg(node, n_dpin);
    not_finished = true;
    return;
  }
  // A signed count carrier can include negative values even when the caller
  // constrains the actual count (notably a counted-loop body). Infer the
  // envelope of valid counts instead of depending on this output's old hint.
  // Include zero for the out-of-range hardware shift case as well.
  const auto zero            = *Dlop::create_integer(0);
  const auto n_min           = n_it->second.get_min();
  const auto n_max           = n_it->second.get_max();
  const bool may_be_negative = n_min.is_negative();
  const auto n_lo            = may_be_negative ? zero : n_min;
  const auto n_hi            = n_max.is_negative() ? zero : n_max;

  auto       max        = a_bw.get_max();
  auto       min        = a_bw.get_min();
  // a<<n = a*2^n is monotonic in n per fixed operand (UP for a>0, MORE NEGATIVE
  // for a<0). The range envelope is the min/max over the four corner shifts —
  // NOT max<<nmax / min<<nmin, which leaves a negative `min` at min<<nmin and so
  // under-estimates the (more negative) true lower bound for signed inputs.
  const Dlop corners[4] = {*max.shl_op(n_hi), *max.shl_op(n_lo), *min.shl_op(n_hi), *min.shl_op(n_lo)};
  Dlop       lo         = may_be_negative ? zero : corners[0];
  Dlop       hi         = lo;
  for (const auto& c : corners) {
    if (c.lt_op(lo)->is_known_true()) {
      lo = c;
    }
    if (c.gt_op(hi)->is_known_true()) {
      hi = c;
    }
  }

  Bitwidth_range bw(lo, hi);
  adjust_bw(node.create_driver_pin(0), bw);
}

void Bitwidth::process_sra(hhds::Node_class& node, Inp_pins& inp_edges) {
  I(inp_edges.size() == 2);

  auto a_dpin = get_driver_of_sink_name(node, "a");
  auto n_dpin = get_driver_of_sink_name(node, "b");

  auto a_it = bwmap.find(a_dpin.get_class_index());
  auto n_it = bwmap.find(n_dpin.get_class_index());

  if (a_it == bwmap.end()) {
    debug_unconstrained_msg(node, a_dpin);
    not_finished = true;
    return;
  }
  auto a_bw = a_it->second;
  if (n_it == bwmap.end()) {
    debug_unconstrained_msg(node, n_dpin);
    not_finished = true;
    return;
  }
  auto n_bw = n_it->second;

  if (n_bw.get_min().is_negative()) {
    // A possibly negative count is not an identity. Frontends can use this
    // for out-of-range part selects; preserve the carrier, never propagate a
    // constant operand's singleton range through the unresolved shift.
    auto       output = node.create_driver_pin(0);
    const auto bits   = bits_of(output);
    if (bits <= 0) {
      // No carrier to preserve (an unstamped cell, or one pass.bitfuzz just
      // stripped). `bits_of` is the attribute THIS pass writes, so waiting for
      // it here never converges -- fall back to the operand's own range, the
      // bound an unresolved shift cannot exceed in magnitude.
      adjust_bw(output, a_bw);
      return;
    }
    Bitwidth_range fallback;
    if (livehd::graph_util::is_unsign(output)) {
      fallback.set_ubits_range(bits);
    } else {
      fallback.set_sbits_range(bits);
    }
    adjust_bw(output, fallback);
    return;
  }
  if (n_bw.get_min().is_positive() && n_bw.get_min().is_just_i64() && n_bw.get_max().is_just_i64()) {
    // Take the FOUR-CORNER envelope, the way process_shl does.
    //
    // Arithmetic >> (rounds toward -inf), NOT division (rounds toward zero):
    // the two differ for negative operands (`-3 sra 1` == -2 but `-3 / 2` ==
    // -1), so division gives an unsoundly tight lower bound.
    //
    // Shifting BOTH bounds by the smallest amount is unsound for the same
    // reason in the other direction: `a >> n` shrinks toward 0 (or -1) as n
    // grows, so for a NON-NEGATIVE `a` the minimum is `a_min >> n_max`, not
    // `a_min >> n_min`. With a variable shift the old bound collapsed the range
    // to a single point -- and adjust_bw then const-folded the whole SRA away.
    // `packed_assign`'s array select (`19'sh24810 >>> ((sel&3)*4+4)`, shift
    // amount [4..16]) came out as the constant 9345, and the design degenerated
    // to `z = 1`.
    const auto a_max = a_bw.get_max();
    const auto a_min = a_bw.get_min();
    const auto n_lo  = n_bw.get_min();
    const auto n_hi  = n_bw.get_max();

    const Dlop corners[4] = {*a_max.sra_op(n_lo), *a_max.sra_op(n_hi), *a_min.sra_op(n_lo), *a_min.sra_op(n_hi)};
    Dlop       max_val    = corners[0];
    Dlop       min_val    = corners[0];
    for (int i = 1; i < 4; ++i) {
      if (corners[i].gt_op(max_val)->is_known_true()) {
        max_val = corners[i];
      }
      if (corners[i].lt_op(min_val)->is_known_true()) {
        min_val = corners[i];
      }
    }

    Bitwidth_range bw(min_val, max_val);
    adjust_bw(node.create_driver_pin(0), bw);
  } else {
    adjust_bw(node.create_driver_pin(0), a_bw);
  }
}

void Bitwidth::process_sum(hhds::Node_class& node, Inp_pins& inp_edges) {
  I(inp_edges.size());

  // Seed the accumulators EXPLICITLY at zero. A default-constructed Dlop is
  // Type::Invalid, and arithmetic guards on is_numeric() and returns NIL for a
  // non-numeric operand (dlop.cpp add_op/sub_op) -- so `max_val.add_op(x)`
  // yields nil, not x, and every later step stays nil. Comparison hides the
  // difference (three_way_cmp reads Invalid's zero words, so it behaves like 0,
  // which is why process_flop's default-seeded accumulators work), but here the
  // nil pair reaches Bitwidth_range(min,max), where is_just_i64() accepts it
  // and to_just_i64() reads 0 -- a non-overflow range [0..0]. adjust_bw sees
  // min==max and CONST-FOLDS the whole adder to zero. Every front end stamps a
  // width on its Sum nodes, which takes the "pin already has bits" early-exit
  // in bw_pass, so nothing on a normal path ever reached this; pass.bitfuzz
  // strips the width and does.
  Dlop max_val = *Dlop::create_integer(0);
  Dlop min_val = *Dlop::create_integer(0);

  for (auto e : inp_edges) {
    auto it = bwmap.find(e.get_driver_pin().get_class_index());
    if (it == bwmap.end()) {
      debug_unconstrained_msg(node, e.get_driver_pin());
      not_finished = true;
      return;
    }
    // Sum: EVEN sink pid adds ("as" bank), ODD subtracts ("bs") -- one pid per
    // operand, see graph/cell.hpp's ONE DRIVER PER SINK PIN block.
    auto pid = Ntype::sink_bank(Ntype_op::Sum, e.get_port_id());
    if (pid == 0) {
      max_val = max_val.add_op(it->second.get_max());
      min_val = min_val.add_op(it->second.get_min());
    } else {
      max_val = max_val.sub_op(it->second.get_min());
      min_val = min_val.sub_op(it->second.get_max());
    }
  }

  adjust_bw(node.create_driver_pin(0), Bitwidth_range(min_val, max_val));
}

void Bitwidth::process_memory(hhds::Node_class& node) {
  int64_t                      mem_size              = 0;
  int32_t                      mem_bits              = 0;
  int32_t                      mem_din_bits          = 0;
  bool                         mem_din_bits_missing  = false;
  int32_t                      mem_addr_bits         = 0;
  bool                         mem_addr_bits_missing = false;
  std::vector<hhds::Pin_class> din_drivers;
  std::vector<hhds::Pin_class> addr_drivers;

  // Lazy: this walk only reads the Memory's operands, records drivers in local
  // vectors and stamps bitwidth ATTRIBUTES, and an attribute write is not a
  // structural mutation (it does not bump the body epoch), so nothing here can
  // invalidate the view.
  for (const auto& in_pin : node.inp_sorted_pins()) {
    const auto in_drv  = in_pin.get_driver_pin();
    auto       raw_pid = static_cast<int>(in_pin.get_port_id());
    auto       n       = Ntype::get_sink_name(Ntype_op::Memory, raw_pid % Ntype::Memory_port_stride);
    if (str_tools::ends_with(n, "clock")) {
      auto it = bwmap.find(in_drv.get_class_index());
      if (it == bwmap.end()) {
        set_bw_1bit(in_drv);
        discovered_some_backward_nodes_try_again = true;
      }
    } else if (n == "bits" || n == "size") {
      if (!in_drv.is_const()) {
        livehd::diag::err("pass.bitwidth", "mem-malformed", "internal")
            .msg("Memory node:{} has {} connected to a non-constant pin", debug_name(node), n)
            .fatal();
        return;
      }
      const auto& val = const_of(in_drv);
      if (!val.is_just_i64()) {
        livehd::diag::err("pass.bitwidth", "mem-malformed", "internal")
            .msg("Memory node:{} has {} connected to a non-integer value", debug_name(node), n)
            .fatal();
        return;
      }
      auto v = val.to_just_i64();
      if (n == "bits") {
        mem_bits = v;
      } else {
        mem_size = v;
      }
    } else {
      auto n_din  = str_tools::ends_with(n, "din");
      auto n_addr = str_tools::ends_with(n, "addr");
      if (n_din || n_addr) {
        auto    it    = bwmap.find(in_drv.get_class_index());
        int32_t dbits = 0;
        if (it != bwmap.end()) {
          dbits = it->second.get_sbits();
          // get_sbits() is the SIGNED width (magnitude + sign bit). A memory's
          // width (mem_bits) and depth (mem_size) are unsigned, so for an
          // always-positive addr OR din the comparison must use the magnitude,
          // which is one bit narrower. (The din arm used to skip this, so a
          // 3-bit unsigned din reported sbits=4 and tripped the width check
          // below against a legitimate 3-bit memory.)
          if ((n_addr || n_din) && it->second.is_always_positive()) {
            --dbits;
          }
        } else {
          if (n_din) {
            mem_din_bits_missing = true;
          } else {
            mem_addr_bits_missing = true;
          }
        }
        if (n_din) {
          mem_din_bits = std::max(dbits, mem_din_bits);
          din_drivers.emplace_back(in_drv);
        } else {
          mem_addr_bits = std::max(dbits, mem_addr_bits);
          addr_drivers.emplace_back(in_drv);
        }
      }
    }
  }

  // Declared geometry is part of the state/layout contract (including init,
  // update and read_all). Write-data ranges alone cannot shrink an element.
  if (mem_bits == 0 && mem_din_bits && !mem_din_bits_missing) {
    mem_bits   = mem_din_bits;
    auto cdpin = create_const(*current_graph, *Dlop::create_integer(mem_bits));
    setup_sink_by_name(node, "bits").connect_driver(cdpin);
  }

  // Declared depth is part of that same contract: only an UNSIZED memory has
  // its depth derived from the address range.
  if (mem_size == 0) {
    int64_t new_mem_size = 0;
    if (!mem_addr_bits_missing) {
      for (const auto& dpin : addr_drivers) {
        auto it = bwmap.find(dpin.get_class_index());
        if (it == bwmap.end()) {
          continue;
        }
        if (!it->second.get_max().is_just_i64()) {
          // A dynamic or very wide address prevents deriving a size from its
          // value range, and there is no declared bound to fall back on here
          // (an explicitly sized cell never enters this block).
          livehd::diag::err("pass.bitwidth", "mem-size-limit", "bitwidth")
              .msg("memory {} size exceeds limit", debug_name(node))
              .fatal();
          return;
        }
        auto sz      = it->second.get_max().to_just_i64() + 1;
        new_mem_size = std::max(sz, new_mem_size);
      }
    }
    if (new_mem_size == 0) {
      not_finished = true;
      Pass::info("memory {} could not infer memory size (trying again)", debug_name(node));
    } else {
      Pass::info("memory {} inferring size of {}", debug_name(node), new_mem_size);
      mem_size   = new_mem_size;
      auto cdpin = create_const(*current_graph, *Dlop::create_integer(mem_size));
      setup_sink_by_name(node, "size").connect_driver(cdpin);
    }
  }

  // Backward seeding of an address expression this pass has not reached yet.
  // It applies to a DECLARED depth exactly as much as to an inferred one --
  // without it, an explicitly sized memory whose address driver is still
  // unranged leaves that driver unsized forever (no retry is even scheduled).
  if (mem_size && mem_addr_bits_missing) {
    // Memory indices are non-negative. A signed backward seed contaminates
    // a not-yet-visited Concat address with a negative range, despite its
    // unsigned lane contract, and a later cprop pass correctly rejects it.
    Bitwidth_range addr_bw(0, mem_size - 1);
    for (auto& dpin : addr_drivers) {
      auto it = bwmap.find(dpin.get_class_index());
      if (it == bwmap.end()) {
        bwmap.insert_or_assign(dpin.get_class_index(), addr_bw);
        discovered_some_backward_nodes_try_again = true;
      }
      // else: the address driver already has a derived range. It may
      // legitimately be WIDER than the freshly inferred address width
      // — a normal unsigned address `raddr:uN` carries an extra sign bit
      // (get_sbits() == N+1), one more than the ceil(log2(entries)) the
      // memory needs. Keep the existing range. (The removed
      // `I(get_sbits() <= addr_sbits)` here wrongly aborted on that common
      // unsigned-address case; e.g. a 2-entry mem with a u1 address, and
      // several ware/rtl barrel-shifter / BTB designs.)
    }
  }

  if (mem_bits == 0) {
    Pass::info("memory {} could not infer the entry size in bits (trying again)", debug_name(node));
    not_finished = true;
    return;
  }

  // The element's SIGN follows what the front end stamped on the memory's value
  // pins, rather than being forced signed. tolg stamps each dout with the
  // declared element sign (set_ubits for a `[N]uW` array); blanket-signing them
  // here made is_unsign(dout) false for EVERY memory in the design, which is
  // exactly what cgen_sim's infer_memory_unsign reads -- so the unsigned-carrier
  // arm of sim.slop_u could never engage under any recipe that runs
  // pass.bitwidth, no matter what the source declared.
  //
  // Only an EXPLICITLY SIZED pin gets a vote: is_unsign() is attribute-ABSENCE,
  // so an unstamped pin reads "unsigned" and would flip a whole memory on no
  // evidence at all. With no sized value pin anywhere, keep the signed default.
  // One signed value pin is enough to keep the memory signed (same conservative
  // AND that infer_memory_unsign applies downstream).
  bool       saw_sized_value = false;
  bool       all_unsigned    = true;
  const auto vote_sign       = [&](const hhds::Pin_class& p) {
    if (p.is_invalid() || bits_of(p) <= 0) {
      return;
    }
    saw_sized_value = true;
    all_unsigned    = all_unsigned && livehd::graph_util::is_unsign(p);
  };
  // OUTPUT pins decide, and only they. A din DRIVER is an arbitrary write-data
  // expression whose sign this very pass infers from a range -- evidence about
  // one written value, never about the declared element type -- so letting it
  // vote lets a negative-capable expression re-sign a `[N]uW` array, which
  // adjust_bw then stamps onto every dout. Consult din only when there is no
  // sized output at all (a write-only memory with no observable read).
  // One vote per dout PIN. vote_sign is idempotent, so the old per-CONSUMER
  // walk voted the same pin once per reader for the same answer.
  for (const auto& out_pin : node.out_sorted_pins()) {
    if (out_pin.get_port_id() != Ntype::Memory_readall_pid) {
      vote_sign(out_pin);
    }
  }
  if (!saw_sized_value) {
    for (const auto& dpin : din_drivers) {
      vote_sign(dpin);
    }
  }
  const bool elem_unsigned = saw_sized_value && all_unsigned;

  Bitwidth_range data_bw;
  if (elem_unsigned) {
    data_bw.set_ubits_range(mem_bits);
  } else {
    data_bw.set_sbits_range(mem_bits);
  }
  for (auto& dpin : din_drivers) {
    auto it = bwmap.find(dpin.get_class_index());
    // A memory input is an assignment boundary, not a range constraint on
    // its producer. Internal expressions derive their range from operands.
    // Seeding an 8-bit Get_mask with a 32-bit element range leaves that wide
    // range in the monotone map even though its pin is correctly capped at 8;
    // another consumer (a mux feeding a concat lane) then inherits 32 bits.
    if (it == bwmap.end() && !infer_internal_range(type_op_of(dpin.get_master_node()))) {
      bwmap.insert_or_assign(dpin.get_class_index(), data_bw);
    }
  }

  Bitwidth_range bw_din = data_bw;

  // Out-connected pins: walk out_edges, collect unique driver pins.
  absl::flat_hash_set<hhds::Class_index> seen;
  // read-only: adjust_bw sets a bitwidth attr. out_sorted_pins() yields each
  // dout pin exactly once, which is what the `seen` set was reconstructing from
  // the per-consumer edge walk; it is kept only for the `dout_pins` bookkeeping
  // further down.
  for (const auto& out_pin : node.out_sorted_pins()) {
    if (!seen.insert(out_pin.get_class_index()).second) {
      continue;
    }
    if (out_pin.get_port_id() != Ntype::Memory_readall_pid) {
      adjust_bw(out_pin, bw_din);
      continue;
    }
    // The packed read-all port is mem_bits * mem_size wide -- but ONLY once the
    // depth is resolved. With mem_size still 0, `mem_bits * 0` would stamp it
    // at ZERO bits (an unsized cell every emit rejects); the depth is being
    // retried (not_finished is already set), so leave the port alone.
    if (mem_size > 0) {
      Bitwidth_range packed_bw;
      packed_bw.set_ubits_range(static_cast<int32_t>(mem_bits * mem_size));
      adjust_bw(out_pin, packed_bw);
    }
  }
}

void Bitwidth::process_mult(hhds::Node_class& node, Inp_pins& inp_edges) {
  I(inp_edges.size());

  Dlop max_val;
  max_val = Dlop::create_integer(1);
  Dlop min_val;
  min_val = Dlop::create_integer(1);
  for (auto e : inp_edges) {
    auto it = bwmap.find(e.get_driver_pin().get_class_index());
    if (it != bwmap.end()) {
      // All four interval corners matter when either operand crosses zero.
      const Dlop products[] = {*min_val.mult_op(it->second.get_min()),
                               *min_val.mult_op(it->second.get_max()),
                               *max_val.mult_op(it->second.get_min()),
                               *max_val.mult_op(it->second.get_max())};
      min_val               = products[0];
      max_val               = products[0];
      for (const auto& product : products) {
        if (product.lt_op(min_val)->is_known_true()) {
          min_val = product;
        }
        if (product.gt_op(max_val)->is_known_true()) {
          max_val = product;
        }
      }
    } else {
      debug_unconstrained_msg(node, e.get_driver_pin());
      not_finished = true;
      return;
    }
  }

  adjust_bw(node.create_driver_pin(0), Bitwidth_range(min_val, max_val));
}

void Bitwidth::process_div(hhds::Node_class& node) {
  const auto a  = get_driver_of_sink_name(node, "a");
  const auto b  = get_driver_of_sink_name(node, "b");
  const auto ai = bwmap.find(a.get_class_index());
  const auto bi = bwmap.find(b.get_class_index());
  if (ai == bwmap.end() || bi == bwmap.end()) {
    not_finished = true;
    return;
  }
  const auto        alo = ai->second.get_min();
  const auto        ahi = ai->second.get_max();
  const auto        blo = bi->second.get_min();
  const auto        bhi = bi->second.get_max();
  std::vector<Dlop> divisors{blo, bhi};
  const Dlop        one       = *Dlop::create_integer(1);
  const Dlop        minus_one = *Dlop::create_integer(-1);
  const auto        contains  = [&](const Dlop& v) { return !v.lt_op(blo)->is_known_true() && !v.gt_op(bhi)->is_known_true(); };
  if (contains(one)) {
    divisors.push_back(one);
  }
  if (contains(minus_one)) {
    divisors.push_back(minus_one);
  }
  std::optional<Dlop> lo, hi;
  const auto          include = [&](const Dlop& value) {
    if (!lo || value.lt_op(*lo)->is_known_true()) {
      lo = value;
    }
    if (!hi || value.gt_op(*hi)->is_known_true()) {
      hi = value;
    }
  };
  for (const auto& divisor : divisors) {
    if (!divisor.is_known_zero()) {
      include(*alo.div_op(divisor));
      include(*ahi.div_op(divisor));
    }
  }
  if (contains(*Dlop::create_integer(0))) {
    // Keep the total bit-vector behavior used by the mapper/LEC in range;
    // in particular a possible zero divisor must not fold 0/b into a constant.
    if (livehd::graph_util::is_unsign(a) && livehd::graph_util::is_unsign(b)) {
      // The dividend's width, NOT max(a,b): both the LEC (encode.cpp's Div arm)
      // and the mapper compute the quotient at a scratch width and then FIT it
      // back to the Div pin's own W, so `2^W-1` is what x/0 can actually reach.
      // Anchoring W to bits_of(a) is self-consistent and tight; folding in a
      // wide divisor instead self-widens the pin (`a:u4 / b:u32` stamped 32
      // bits) and hands ABC an O(W^2) divider eight times too big.
      const int width = std::max(bits_of(a), 1);
      include(*Dlop::get_mask_value(width));
    } else {
      if (alo.is_negative()) {
        include(one);
      }
      if (!ahi.is_negative()) {
        include(minus_one);
      }
    }
  }
  if (lo && hi) {
    adjust_bw(node.create_driver_pin(0), Bitwidth_range(*lo, *hi));
  }
}

void Bitwidth::process_set_mask(hhds::Node_class& node) {
  auto a_dpin = get_driver_of_sink_name(node, "a");
  if (a_dpin.is_invalid()) {
    livehd::diag::err("pass.bitwidth", "setmask-undefined", "bitwidth").msg("set_mask can not have an undefined input").fatal();
  }

  auto it = bwmap.find(a_dpin.get_class_index());
  if (it == bwmap.end()) {
    debug_unconstrained_msg(node, a_dpin);
    not_finished = true;
    return;
  }
  Bitwidth_range bw{it->second};

  auto mask_dpin = get_driver_of_sink_name(node, "mask");
  if (mask_dpin.is_invalid()) {
    livehd::diag::err("pass.bitwidth", "setmask-undefined", "bitwidth").msg("set_mask can not have an undefined mask").fatal();
  }

  if (!mask_dpin.is_const()) {
    not_finished = true;
    return;
  }
  const auto& mask = const_of(mask_dpin);

  auto value_dpin = get_driver_of_sink_name(node, "value");
  if (value_dpin.is_invalid()) {
    livehd::diag::err("pass.bitwidth", "setmask-undefined", "bitwidth").msg("set_mask can not have an undefined value").fatal();
  }

  auto           it2 = bwmap.find(value_dpin.get_class_index());
  Bitwidth_range value_bw;
  if (it2 == bwmap.end()) {
    if (value_dpin.is_const()) {
      value_bw.set_range(Dlop::create_integer(0), const_of(value_dpin));
    } else if (mask.is_negative()) {
      debug_unconstrained_msg(node, value_dpin);
      not_finished = true;
      return;
    } else {
      bw.set_wider_range(Dlop::create_integer(0), mask);
      adjust_bw(node.create_driver_pin(0), bw);
      return;
    }
  } else {
    value_bw = it2->second;
  }

  if (mask.is_just_i64() && mask.to_just_i64() == -1) {
    adjust_bw(node.create_driver_pin(0), value_bw);
    return;
  }
  if (mask.is_known_zero()) {
    adjust_bw(node.create_driver_pin(0), bw);
    return;
  }

  if (!mask.is_negative()) {
    // A finite insertion scatters the value's low bits into the mask window;
    // a negative value sign-fills THAT window, not the result above it. Bounds
    // on the shifted signed value would mis-sign and undersize the assembly.
    Bitwidth_range result;
    if (bw.is_always_positive()) {
      result.set_ubits_range(std::max(bw.get_ubits(), static_cast<int32_t>(mask.get_payload_bits())));
    } else {
      result.set_sbits_range(std::max(bw.get_sbits(), static_cast<int32_t>(mask.get_signed_bits())));
    }
    adjust_bw(node.create_driver_pin(0), result);
    return;
  }

  // A negative mask overwrites the infinite high tail, so the result's sign
  // follows the inserted value. The finite prefix can still contain base bits;
  // even inserting constant zero does not make the whole result constant zero.
  // `get_payload_bits()` drops the sign slot only for a NON-NEGATIVE value, and
  // this branch is the NEGATIVE-mask one, so it would return the full signed
  // width and inflate the result by a bit. The prefix is the mask's finite part.
  const int32_t  prefix = mask.get_signed_bits() - 1;
  Bitwidth_range result;
  if (value_bw.is_always_positive()) {
    result.set_ubits_range(prefix + value_bw.get_ubits());
  } else {
    result.set_sbits_range(prefix + value_bw.get_sbits());
  }
  adjust_bw(node.create_driver_pin(0), result);
}

void Bitwidth::process_get_mask(hhds::Node_class& node) {
  auto a_dpin = get_driver_of_sink_name(node, "a");
  if (a_dpin.is_invalid()) {
    if (!not_finished) {
      node.del_node();
    }
    return;
  }

  auto mask_dpin = get_driver_of_sink_name(node, "mask");

  auto it = bwmap.find(a_dpin.get_class_index());
  if (it == bwmap.end()) {
    debug_unconstrained_msg(node, a_dpin);
    not_finished = true;
    return;
  }

  auto it2 = bwmap.find(mask_dpin.get_class_index());
  Dlop mask_val;
  if (mask_dpin.is_const()) {
    mask_val = const_of(mask_dpin);
  } else if (it2 == bwmap.end()) {
    debug_unconstrained_msg(node, mask_dpin);
    not_finished = true;
    return;
  } else {
    mask_val = it2->second.get_max().or_op(it2->second.get_min());
  }

  // Analyses may inspect hand-built or incomplete graphs. Do not infer a
  // range or an identity for a selection outside the construction contract.
  if (!mask_dpin.is_const() || !livehd::graph_util::is_legal_mask(mask_val)) {
    return;
  }

  Dlop a_max = it->second.get_max();
  Dlop a_min = it->second.get_min();

  if (mask_dpin.is_const() && is_finite_low_mask(mask_val) && !a_min.is_negative() && a_max.le_op(mask_val)->is_known_true()) {
    // Preserve the lower bound as well as the width. Turning [1,8] into
    // [0,15] here makes a following `(1 << count) - 1` look possibly negative
    // and leaves unnecessary coercion masks behind even after this one dies.
    const auto unchanged = it->second;
    adjust_bw(node.create_driver_pin(0), unchanged);
    return;
  }

  // get_mask is the zext (force) bit-select: the packed result is never
  // negative, a lone selected set bit included (only `#sext` may be negative).
  auto gm = [](const Dlop& v, const Dlop& m) -> Dlop { return livehd::eval_get_mask(v, m); };

  Dlop res_max;
  if (a_max.same_repr(a_min)) {
    res_max = gm(a_max, mask_val);
  } else {
    res_max = gm(*a_max.get_mask_value(), mask_val);
  }

  // Bit extraction is not monotone: [1,2] masked by 1 contains both 0 and 1,
  // even though the lower endpoint and the upper bit-envelope both select 1.
  Dlop res_min = a_max.same_repr(a_min) ? res_max : *Dlop::create_integer(0);

  if (a_min.is_negative()) {
    // Probe the worst case: the operand with EVERY bit set.
    //
    // The literal -1 expresses that only for a NON-negative mask. A negative
    // (carve-out) mask makes Dlop::get_mask_op copy bits [positive_mask_bits,
    // src_bits) of the SOURCE, and src_bits is the source's OWN
    // get_signed_bits() -- and -1 is one bit wide, so exactly one bit gets
    // selected and the pack is the 1-bit value, not an all-ones pattern. The
    // probe then never raised res_max and the bound fell back to a_min.neg_op(),
    // i.e. 2^(N-1) instead of 2^N-1: an 8-bit port zero-extended by `x & 8'hff`
    // was modelled as [0..128] instead of [0..255]. bits_of hides it
    // (get_bits(128) == get_bits(255) == 9), so the Get_mask pin looks identical
    // and only a consumer reveals it -- `+1` needs 9 bits for 128 but 10 for
    // 255, and the one-bit-narrow sum truncated: `r(ref=256 impl=0) @ x=255`.
    //
    // For a negative mask, probe at the OPERAND's width instead. The positive
    // branch keeps -1: for a sparse mask like 0xf00 that is the bound that
    // yields the right envelope.
    Dlop tmp;
    if (mask_val.is_negative()) {
      const auto a_bits = std::max(a_max.get_signed_bits(), a_min.get_signed_bits());
      tmp               = gm(*Dlop::get_mask_value(a_bits), mask_val);
    } else {
      tmp = gm(*Dlop::create_integer(-1), mask_val);
    }
    if (tmp.gt_op(res_max)->is_known_true()) {
      res_max = tmp;
    }
    res_min = *Dlop::create_integer(0);
    a_min   = a_min.neg_op();
  }

  Dlop val2;
  val2 = gm(a_min, mask_val);
  if (val2.gt_op(res_max)->is_known_true()) {
    res_max = val2;
  }
  if (val2.lt_op(res_min)->is_known_true()) {
    res_min = val2;
  }

  if (res_min.is_known_zero() && res_max.is_known_zero() && !not_finished) {
    auto zero_dpin = create_const(*current_graph, *Dlop::create_integer(0));
    // Reconnect every consumer to const-0, then del_node (below) drops all of
    // node's edges in one bulk op (no per-edge del_edge lookup). Iterating the
    // live out_edges while connecting is safe: connect_sink only grows
    // zero_dpin/sink storage, never node's out-edge set.
    for (const auto& sink : consumer_sinks(node)) {  // snapshot: the loop rewires what it walks
      zero_dpin.connect_sink(sink);
    }
    node.del_node();
    return;
  }

  adjust_bw(node.create_driver_pin(0), Bitwidth_range(res_min, res_max));
}

// Concat's range is a pure function of the DECLARED lane widths, and of
// nothing else -- not of the lane values' own ranges.
//
// Every lane occupies its own window, so a lane's sign never escapes: a driver
// NARROWER than its window sign-extends into it (-1 at 3 bits is 0b111), and a
// driver WIDER than its window is an internal compile error, not a truncation
// (graph_util::concat_lane_violation -- truncating would shift every lane above
// it). The result therefore saturates the full magnitude (any lane can be
// all-ones) and is never negative: exactly [0, 2^sum(w_i)-1].
// set_ubits_range spells that, and set_bits_sign then stamps the exact literal
// bits = sum(w_i) width and `unsign`.
//
// Two traps live here, which is why the lane table only ever comes from
// graph_util::concat_lanes:
//   - The odd sink pids hold the WIDTH constants, not data. The generic
//     operand seeding above puts them in bwmap like any other const, so a
//     process_* that folded all its operands' ranges together would widen the
//     result by the width NUMBER (an 8-lane concat carries an operand holding
//     8). Nothing but the lane table is read below.
//   - A lane's declared window is NOT recoverable from its driver. bits_of on
//     the value is an upper bound that this very pass is free to narrow, and
//     the significant bits are narrower still; re-deriving a width from it
//     shifts every lane ABOVE the one that got it wrong.
void Bitwidth::process_concat(hhds::Node_class& node) {
  const auto lanes = livehd::graph_util::concat_lanes(node);
  const auto total = livehd::graph_util::concat_total_width(lanes);
  if (total <= 0) {
    // Malformed cell (a missing lane, or a width operand that is not yet a
    // positive constant). Fail closed: do NOT fall back to the pre-stamped
    // bits, since a total that disagrees with the lane table misplaces every
    // lane. Retrying is worth it -- adjust_bw can still const-fold the width
    // operand's driver in a later iteration -- and if it never resolves,
    // report_unbounded names the pin.
    not_finished = true;
    return;
  }

  // Check lane carriers after inference finishes: intermediate ranges can
  // still narrow. The final check is a diagnostic in every build mode.

  Bitwidth_range bw;
  bw.set_ubits_range(total);
  adjust_bw(node.create_driver_pin(0), bw);
}

void Bitwidth::process_sext(hhds::Node_class& node, Inp_pins& inp_edges) {
  // inp_edges may not be in pid order — sort by sink port_id so [0]==a, [1]==b.
  I(inp_edges.size() >= 2);
  auto wire_dpin = inp_edges[0].get_driver_pin();
  auto pos_dpin  = inp_edges[1].get_driver_pin();

  bool no_wire = !bwmap.contains(wire_dpin.get_class_index());

  auto sign_max = Bits_unknown;
  if (pos_dpin.is_const()) {
    const auto& pos_const = const_of(pos_dpin);
    if (pos_const.is_just_i64()) {  // a wider position stays Bits_unknown
      sign_max = pos_const.to_just_i64();
    }
  }

  if (sign_max == Bits_unknown && no_wire) {
    debug_unconstrained_msg(node, pos_dpin);
    not_finished = true;
    return;
  }

  Bitwidth_range bw;
  bw.set_sbits_range(sign_max);
  adjust_bw(node.create_driver_pin(0), bw);
  if (node.is_invalid()) {
    return;  // adjust_bw folded the whole cell to a constant
  }

  if (not_finished || no_wire) {
    return;
  }

  auto wire_it = bwmap.find(wire_dpin.get_class_index());
  {
    auto b = wire_it->second.get_sbits();
    if (b <= sign_max) {
      sign_max = b;
      // live-reconnect each consumer to the Sext source; del_node bulk-drops
      // node's edges (no per-edge find). connect only grows the source/sink.
      for (const auto& sink : consumer_sinks(node)) {  // snapshot: the loop rewires what it walks
        sink.connect_driver(inp_edges[0].get_driver_pin());
      }
      node.del_node();
    }
  }

  if (node.is_invalid()) {
    return;
  }

  auto wire_node = wire_dpin.get_master_node();
  auto wire_op   = type_op_of(wire_node);
  if (wire_op == Ntype_op::Sext || wire_op == Ntype_op::And) {
    if (wire_it->second.get_sbits() <= sign_max) {
      // live-reconnect each consumer to wire_dpin; del_node bulk-drops node's
      // edges (no per-edge find).
      for (const auto& sink : consumer_sinks(node)) {  // snapshot: the loop rewires what it walks
        sink.connect_driver(wire_dpin);
      }
      node.del_node();
    } else {
      hhds::Pin_class grandpa_dpin;
      if (wire_op == Ntype_op::Sext) {
        grandpa_dpin = get_driver_of_sink_name(wire_node, "a");
      } else {
        // SNAPSHOT: `inp_edges[0].del_sink()` below rewires this node's cone.
        auto mask_e = wire_node.inp_pins_snapshot();
        if (mask_e.size() != 2) {
          return;
        }
        // Sext(And(x, mask)) with a contiguous low mask at least sign_max
        // wide (guaranteed by the range test above): the And cannot change
        // any bit the Sext reads, so sign-extend x directly. Constants are
        // CONST_NODE PINS, so probe the driver pins, not their master node.
        const auto m0 = mask_e[0].get_driver_pin();
        const auto m1 = mask_e[1].get_driver_pin();
        if (m0.is_const() && const_of(m0).is_mask()) {
          grandpa_dpin = m1;
        } else if (m1.is_const() && const_of(m1).is_mask()) {
          grandpa_dpin = m0;
        } else {
          return;
        }
      }
      inp_edges[0].del_sink();  // one driver per sink pin: dropping it IS the old del_edge
      setup_sink_by_name(node, "a").connect_driver(grandpa_dpin);
    }
  }
}

void Bitwidth::process_comparator(hhds::Node_class& node) { set_bw_1bit(node.create_driver_pin(0)); }

void Bitwidth::process_assignment_or(hhds::Node_class& node, Inp_pins& inp_edges) {
  I(inp_edges.size() == 1);

  auto it = bwmap.find(inp_edges[0].get_driver_pin().get_class_index());
  if (it == bwmap.end()) {
    debug_unconstrained_msg(node, inp_edges[0].get_driver_pin());
    not_finished = true;
    return;
  }
  // Reconnect each consumer to the single input, then let del_node bulk-drop
  // node's edges (no per-edge find). The consumer list is SNAPSHOTTED: the
  // reconnect mutates the same storage a live fan-out walk would be reading.
  for (const auto& sink : consumer_sinks(node)) {
    inp_edges[0].get_driver_pin().connect_sink(sink);
  }
  node.del_node();
}

void Bitwidth::process_bit_or(hhds::Node_class& node, Inp_pins& inp_edges) {
  I(inp_edges.size() > 1);
  int32_t max_bits     = 0;
  bool    any_negative = false;
  for (auto e : inp_edges) {
    auto it = bwmap.find(e.get_driver_pin().get_class_index());
    if (it == bwmap.end()) {
      debug_unconstrained_msg(node, e.get_driver_pin());
      not_finished = true;
      return;
    }
    any_negative = any_negative || !it->second.is_always_positive();
    auto bits    = it->second.is_always_positive() ? it->second.get_ubits() : it->second.get_sbits();
    if (bits > max_bits) {
      max_bits = bits;
    }
  }

  if (max_bits == 0) {
    auto zero_dpin = create_const(*current_graph, *Dlop::create_integer(0));
    // Reconnect every consumer to const-0, then del_node (below) drops all of
    // node's edges in one bulk op (no per-edge del_edge lookup). Iterating the
    // live out_edges while connecting is safe: connect_sink only grows
    // zero_dpin/sink storage, never node's out-edge set.
    for (const auto& sink : consumer_sinks(node)) {  // snapshot: the loop rewires what it walks
      zero_dpin.connect_sink(sink);
    }
    node.del_node();
    return;
  }

  Bitwidth_range bw;
  if (any_negative) {
    // The full signed range of max_bits, NOT [-2^(max_bits-1) .. 0].
    //
    // The old bound was UNSOUND: an operand that *can* be negative only sets
    // the result's sign bit when it actually is, so `a | b` is perfectly
    // capable of being positive and pinning max at 0 said otherwise.
    bw.set_sbits_range(max_bits);
  } else {
    bw.set_range(*Dlop::create_integer(0), *Dlop::get_mask_value(max_bits));
  }

  adjust_bw(node.create_driver_pin(0), bw);
}

void Bitwidth::process_bit_xor(hhds::Node_class& node, Inp_pins& inp_edges) {
  // Constant folding can combine equal operands into a single constant row.
  // Unary XOR is the identity and its range is valid in the loop below.
  I(!inp_edges.empty());
  // TWO maxima, because the unsigned and signed answers are not the same
  // number: a literal uW operand needs W+1 bits once the RESULT has to be
  // signed. Taking the unsigned width into a signed range makes `s4(-8) ^
  // u8(255)` (= -249) stamp s8, and every literal-width consumer truncates it
  // to +7.
  int32_t max_ubits    = 0;
  int32_t max_sbits    = 0;
  bool    any_negative = false;

  for (auto e : inp_edges) {
    auto it = bwmap.find(e.get_driver_pin().get_class_index());
    if (it == bwmap.end()) {
      debug_unconstrained_msg(node, e.get_driver_pin());
      not_finished = true;
      return;
    }
    if (it->second.is_always_positive()) {
      const int32_t ubits = it->second.get_ubits();
      max_ubits           = std::max(max_ubits, ubits);
      max_sbits           = std::max(max_sbits, ubits + 1);
    } else {
      max_sbits    = std::max(max_sbits, it->second.get_sbits());
      any_negative = true;
    }
  }

  Bitwidth_range bw;
  if (any_negative) {
    bw.set_sbits_range(max_sbits);
  } else {
    bw.set_ubits_range(max_ubits);
  }
  adjust_bw(node.create_driver_pin(0), bw);
}

void Bitwidth::process_bit_and(hhds::Node_class& node, Inp_pins& inp_edges) {
  I(!inp_edges.empty());

  int32_t pos_min_sbits = Bits_unknown;
  int32_t unk_max_sbits = 0;

  for (auto i = 0u; i < inp_edges.size(); ++i) {
    const auto& e  = inp_edges[i];
    auto        it = bwmap.find(e.get_driver_pin().get_class_index());
    if (it == bwmap.end()) {
      unk_max_sbits = Bits_unknown;
      continue;
    }
    int32_t bw_sbits = it->second.is_always_positive() ? it->second.get_ubits() : it->second.get_sbits();
    if (bw_sbits == 0) {
      auto zero_dpin = create_const(*current_graph, *Dlop::create_integer(0));
      // live-reconnect each consumer to const-0; del_node bulk-drops node's
      // edges (no per-edge find).
      for (const auto& sink : consumer_sinks(node)) {  // snapshot: the loop rewires what it walks
        zero_dpin.connect_sink(sink);
      }
      node.del_node();
      return;
    }

    if (it->second.is_always_positive()) {
      pos_min_sbits = std::min(pos_min_sbits, bw_sbits);
    } else {
      if (bw_sbits > unk_max_sbits) {
        unk_max_sbits = bw_sbits;
      }
    }
  }

  if (unk_max_sbits == Bits_unknown && pos_min_sbits == Bits_unknown) {
    Pass::info("could not find constrains for AND node:{}\n", debug_name(node));
    return;
  }

  Dlop max_val;
  Dlop min_val;

  if (pos_min_sbits != Bits_unknown) {
    max_val = Dlop::get_mask_value(pos_min_sbits);
    min_val = Dlop::create_integer(0);
  } else {
    max_val = Dlop::get_mask_value(unk_max_sbits - 1);
    min_val = Dlop::create_integer(-1)->sub_op(max_val);
  }

  Bitwidth_range bw(min_val, max_val);

  adjust_bw(node.create_driver_pin(0), bw);
  if (node.is_invalid()) {
    return;  // adjust_bw folded the whole cell to a constant
  }

  for (auto e : inp_edges) {
    auto bw_bits = bits_of(e.get_driver_pin());
    if (bw_bits) {
      continue;
    }
    auto drv_node = e.get_driver_pin().get_master_node();
    if (is_graph_input_pin(e.get_driver_pin()) || Ntype::is_loop_last(type_op_of(drv_node))) {
      set_bits_sign(e.get_driver_pin(), bw);
    }
  }
}

Bitwidth::Attr Bitwidth::get_key_attr(std::string_view key) {
  if (str_tools::ends_with(key, "__max")) {
    return Attr::Set_max;
  }
  if (str_tools::ends_with(key, "__min")) {
    return Attr::Set_min;
  }
  if (str_tools::ends_with(key, "__ubits")) {
    return Attr::Set_ubits;
  }
  if (str_tools::ends_with(key, "__bits") || str_tools::ends_with(key, "__sbits")) {
    return Attr::Set_sbits;
  }
  if (str_tools::ends_with(key, "__dp_assign")) {
    return Attr::Set_dp_assign;
  }
  return Attr::Set_other;
}

void Bitwidth::process_attr_set_dp_assign(hhds::Node_class& node_dp) {
  I(is_sink_connected(node_dp, "value"));
  I(is_sink_connected(node_dp, "parent"));

  auto dpin_lhs = get_driver_of_sink_name(node_dp, "parent");
  auto dpin_rhs = get_driver_of_sink_name(node_dp, "value");

  auto it = bwmap.find(dpin_lhs.get_class_index());
  if (it == bwmap.end()) {
    if (!not_finished) {
      Pass::info("node:{} dp_assign lhs is not ready", debug_name(node_dp));
    }
    not_finished = true;
    return;
  }

  const Bitwidth_range                   lhs_bw = it->second;
  // Walk out_connected_pins (unique drivers from out_edges).
  absl::flat_hash_set<hhds::Class_index> seen_out;
  // read-only: records the driver bitwidth in a side map, once per DRIVER PIN
  // (which is what seen_out re-derived from the per-consumer edge walk).
  for (const auto& out_pin : node_dp.out_sorted_pins()) {
    if (seen_out.insert(out_pin.get_class_index()).second) {
      bwmap.insert_or_assign(out_pin.get_class_index(), lhs_bw);
    }
  }

  auto it2 = bwmap.find(dpin_rhs.get_class_index());
  if (it2 == bwmap.end()) {
    if (!not_finished) {
      Pass::info("node:{} dp_assign rhs is not ready", debug_name(node_dp));
    }
    not_finished = true;
    return;
  }
}

void Bitwidth::process_attr_set_bw(hhds::Node_class& node_attr, Bitwidth::Attr attr) {
  I(is_sink_connected(node_attr, "value"));

  auto dpin_val  = get_driver_of_sink_name(node_attr, "value");
  auto attr_dpin = node_attr.create_driver_pin("Y");

  Bitwidth_range bw;
  bool           parent_pending = true;

  hhds::Pin_class parent_dpin;
  if (is_sink_connected(node_attr, "parent")) {
    parent_dpin = get_driver_of_sink_name(node_attr, "parent");
    auto it     = bwmap.find(parent_dpin.get_class_index());
    if (it != bwmap.end()) {
      bw             = it->second;
      parent_pending = false;
    }
  }

  I(dpin_val.is_const());
  const auto& val = const_of(dpin_val);
  if ((attr == Attr::Set_ubits || attr == Attr::Set_sbits) && !val.is_just_i64()) {
    livehd::diag::err("pass.bitwidth", "bits-limit", "bitwidth")
        .msg("Attr bits value of node:{} exceeds the supported limit", debug_name(node_attr))
        .fatal();
    return;
  }

  if (attr == Attr::Set_ubits) {
    auto bits = static_cast<int32_t>(val.to_just_i64());

    if (!parent_pending) {
      Bitwidth_range set_bw;
      set_bw.set_ubits_range(bits);
    } else {
      bw.set_ubits_range(bits);
    }

    if (bw.get_sbits() <= bits) {
      if (parent_dpin.is_invalid()) {
        parent_dpin    = create_const(*current_graph, *Dlop::create_integer(0));
        parent_pending = false;
      }
      // live-reconnect each consumer to parent_dpin; del_node bulk-drops
      // node_attr's edges (no per-edge find).
      for (const auto& sink : consumer_sinks(node_attr)) {  // snapshot: the loop rewires what it walks
        parent_dpin.connect_sink(sink);
      }
      node_attr.del_node();
    } else {
      insert_tposs_nodes(node_attr, bits);
    }
  } else if (attr == Attr::Set_sbits) {
    auto bits = static_cast<int32_t>(val.to_just_i64());

    if (!parent_pending) {
      Bitwidth_range set_bw;
      set_bw.set_sbits_range(bits);
      if (bw.get_min().lt_op(set_bw.get_min())->is_known_true()) {
        livehd::diag::err("pass.bitwidth", "bitwidth-mismatch", "bitwidth")
            .msg("bitwidth mismatch at node {}: min {} exceeds {}sbits", debug_name(node_attr), bw.get_min(), bits)
            .fatal();
      }
    } else {
      bw.set_sbits_range(bits);
    }
  } else {
    I(false);
  }

  if (!node_attr.is_invalid()) {
    set_bits_sign(attr_dpin, bw);
    bwmap.insert_or_assign(attr_dpin.get_class_index(), bw);
    // read-only over the lazy view: annotates flop consumers' Q bitwidth (the
    // flop's port-0 driver already exists, so create_driver_pin(0) is a lookup).
    for (const auto& e : attr_dpin.out_edges()) {
      auto sink_node = e.sink.get_master_node();
      // Latch included (2f-latch M2): like a flop, its q is driver pin 0 and
      // takes the attr's width. (is_type_register is deliberately NOT used —
      // it also admits Memory, whose pin 0 is not a state output.)
      if (!is_type_flop(sink_node) && type_op_of(sink_node) != Ntype_op::Latch) {
        continue;
      }
      auto reg_qpin = sink_node.create_driver_pin(0);
      if (bits_of(reg_qpin) == 0) {
        continue;
      }
      set_bits_sign(reg_qpin, bw);
      bwmap.insert_or_assign(reg_qpin.get_class_index(), bw);
    }
  }

  if (parent_pending && !parent_dpin.is_invalid()) {
    set_bits_sign(parent_dpin, bw);
    bwmap.insert_or_assign(parent_dpin.get_class_index(), bw);
  }
}

void Bitwidth::insert_tposs_nodes(hhds::Node_class& node_attr, int32_t ubits) {
  I(absl::StrContains(const_of(get_driver_of_sink_name(node_attr, "field")).to_field(), "__ubits"));

  auto name_dpin = get_driver_of_sink_name(node_attr, "parent");
  if (name_dpin.is_invalid()) {
    return;
  }

  auto mask = Dlop::get_mask_value(ubits);

  hhds::Node_class ntposs;

  // Restart-scan the lazy out_edges view after each rewrite (no fan-out copy):
  // splice a tposs Get_mask in front of the next consumer that needs one, drop
  // the edge, restart. Consumers that already mask (Or-with-single-use / matching
  // Get_mask) are left in place.
  bool progressed = true;
  while (progressed) {
    progressed = false;
    for (const auto& e : node_attr.out_edges()) {
      I(e.driver.get_port_id() == 0);
      auto sink_node = e.sink.get_master_node();
      auto sink_type = type_op_of(sink_node);

      if (sink_type == Ntype_op::Or) {
        // skip a single-use Or consumer. Cap the walk at 2 instead of size()-ing
        // the lazy view (only the "exactly one out edge" distinction matters).
        size_t fanout = 0;
        for (const auto& se : sink_node.out_edges()) {
          (void)se;
          if (++fanout >= 2) {
            break;
          }
        }
        if (fanout == 1) {
          continue;
        }
      }
      if (sink_type == Ntype_op::Get_mask) {
        // The sibling's mask may be a live wire; only a constant equal to ours
        // makes it the same selection. (Used to read a non-const mask as 0.)
        const auto* m = get_driver_of_sink_name(sink_node, "mask").const_value();
        if (m != nullptr && m->same_repr(*mask)) {
          continue;
        }
      }

      if (ntposs.is_invalid()) {
        ntposs = livehd::graph_util::create_get_mask(*current_graph, name_dpin, create_const(*current_graph, *mask));
      }

      ntposs.create_driver_pin(0).connect_sink(e.sink);
      e.del_edge();
      progressed = true;
      break;  // iterator invalidated by the rewrite; restart the scan
    }
  }

  if (!ntposs.is_invalid()) {
    process_get_mask(ntposs);
    pending_added_nodes.push_back(ntposs);
  }
}

void Bitwidth::process_attr_set(hhds::Node_class& node_attr) {
  I(is_sink_connected(node_attr, "field"));

  auto dpin_key = get_driver_of_sink_name(node_attr, "field");
  if (!dpin_key.is_const()) {
    not_finished = true;
    return;
  }
  auto key  = const_of(dpin_key).to_field();
  auto attr = get_key_attr(key);

  if (attr == Attr::Set_other) {
    not_finished = true;
    return;
  }
  if (attr == Attr::Set_dp_assign) {
    process_attr_set_dp_assign(node_attr);
  } else {
    process_attr_set_bw(node_attr, attr);
  }
}

void Bitwidth::debug_unconstrained_msg(hhds::Node_class& node, const hhds::Pin_class& dpin) {
  (void)node;
  (void)dpin;
}

void Bitwidth::bw_pass(hhds::Graph* g) {
  current_graph = g;
  // bwmap is keyed by hhds::Class_index, the per-body node id, which is
  // explicitly allowed to collide across graph bodies (hhds/index.hpp). A
  // single Bitwidth object is reused for every graph in var.graphs, so without
  // this clear a pin in graph B can read a STALE Bitwidth_range left by a
  // same-index pin in graph A — corrupting inferred widths and perturbing
  // control flow (not_finished, const-folding, AttrSet deletion). The set of
  // collisions depends on cross-graph allocation/processing order, so the same
  // module compiled alone vs. alongside others would otherwise differ.
  bwmap.clear();
  discovered_some_backward_nodes_try_again = true;
  not_finished                             = false;

  int  n_iterations = 0;
  auto gio          = g->get_io();

  while (discovered_some_backward_nodes_try_again || not_finished) {
    discovered_some_backward_nodes_try_again = false;
    not_finished                             = false;

    // Seed bw from input pin declarations.
    if (gio) {
      for (const auto& d : gio->get_input_pin_decls()) {
        auto pin = g->get_input_pin(d.name);
        if (pin.is_invalid()) {
          continue;
        }
        // A DECLARED port is seeded from its DECL -- width and sign together --
        // and only an undeclared one falls back to the pin attrs.
        //
        // The pin's own `pin_signed` says nothing about the source: every front
        // end stamps it on every graph-IO pin by convention (LGraph values are
        // signed), so `is_unsign(pin)` is false for `input [2:0] a` and for
        // `input signed [3:0] b` alike. Reading it made a 3-bit UNSIGNED port
        // seed as [-4..3] instead of [0..7].
        //
        // And a PORT's `bits` is the LITERAL declared bus width, not the
        // magnitude+sign count used on internal nets -- which is why the signed
        // arm below is `set_sbits_range(b)` (decl 4 -> [-8..7], exactly a 4-bit
        // signed port). The unsigned arm must match: `set_ubits_range(b)`, so
        // decl 3 -> [0..7]. The old `b - 1` gave [0..3] and every value derived
        // from the port came out a bit too narrow, so cgen emitted a truncating
        // net -- `lhd lec` caught it as res(ref=8 impl=0) @ ar.x=7.
        int32_t b      = static_cast<int32_t>(d.bits);
        bool    unsign = d.unsign;
        if (b == 0) {
          b      = bits_of(pin);
          unsign = livehd::graph_util::is_unsign(pin);
        }
        if (b) {
          Bitwidth_range bw;
          if (unsign) {
            bw.set_ubits_range(b);
          } else {
            bw.set_sbits_range(b);
          }
          bwmap.insert_or_assign(pin.get_class_index(), bw);
        }
      }
    }

    // Forward-iterate nodes, collect work.
    pending_added_nodes.clear();
    std::vector<hhds::Node_class> to_visit;
    if (n_iterations == 0) {
      for (auto node : g->body().nodes(hhds::Node_order::forward)) {
        to_visit.push_back(node);
      }
    } else {
      // A re-run means the first walk met an operand before its driver. On an
      // acyclic body hhds's forward order never does that, so the usual cause
      // is a FALSE word-level loop through a pure-comb `Sub` (a parent wiring
      // one instance's output back into another's input, e.g. an arbiter's
      // request/can_grant handshake). hhds does not treat such a Sub as a cut,
      // so its whole SCC -- plus everything downstream of it -- lands in the
      // raw-storage-index tail with no dependency check. A reader that builds
      // a chain outermost-first (slang's per-element output reassembly) then
      // presents it consumer-before-producer, and every whole-graph pass only
      // resolved one link of it: a 6-lane chain outran the 3-iteration budget
      // and left the or/shl reassembly unbounded, which pass.abc then refused
      // to materialize. For bitwidth a Sub's outputs come from its DECLARED
      // interface, never from its inputs, so breaking the cycle at a Sub is
      // exact. comb_emit_order does precisely that (and is the same stable
      // storage-index Kahn as hhds on an acyclic body); state/Memory are
      // sources, visited first as with Cut_placement::first.
      std::vector<hhds::Node_class> comb_order;
      livehd::graph_util::comb_emit_order(g, comb_order);
      absl::flat_hash_set<hhds::Node_class> placed(comb_order.begin(), comb_order.end());
      for (auto node : g->body().nodes()) {
        if (!placed.contains(node)) {
          to_visit.push_back(node);
        }
      }
      to_visit.insert(to_visit.end(), comb_order.begin(), comb_order.end());
    }
    // Also include any deferred-added nodes from prior iterations.
    for (size_t i = 0; i < to_visit.size(); ++i) {
      auto node = to_visit[i];
      if (node.is_invalid()) {
        continue;
      }
      auto inp_edges = node.inp_pins_snapshot();  // SNAPSHOT: every process_* below rewires while walking
      auto op        = type_op_of(node);

      if (inp_edges.empty() && op != Ntype_op::Sub && op != Ntype_op::LUT) {
        node.del_node();
        continue;
      }

      // Seed the range of every CONSTANT operand. A constant is a driver pin
      // on the CONST_NODE singleton, which no class/flat/hier traversal ever
      // emits (hhds graph.hpp), so this operand walk is the only place it is
      // seen. Without the seed every processor that looks its operands up --
      // process_sum, process_mult, process_mux, ... -- bailed with
      // not_finished, and a cell as ordinary as `x + 1` was uninferable no
      // matter how well bounded `x` was.
      for (const auto& e : inp_edges) {
        // Compact-loop carry inputs also have a previous-ordinal driver.
        for (auto driver : e.get_driver_pins()) {
          if (!driver.is_const()) {
            continue;
          }
          const auto ci = driver.get_class_index();
          if (bwmap.contains(ci)) {
            continue;
          }
          const auto& val = const_of(driver);
          if (val.is_numeric()) {
            bwmap.insert_or_assign(ci, Bitwidth_range(val));
          }
        }
      }

      if (!infer_internal_range(op) && !Ntype::has_multiple_driver_pins(op)) {
        // Unsupported/finite-width cells have no value-range rule in this
        // pass, so their existing annotation is the only available contract.
        //
        // A pre-sized Flop/Fflop/Latch deliberately lands here.  State is a
        // finite-width truncation boundary: once a front end (or a previous
        // bitwidth pass) materializes Q as uN/sN, a later optimization pass
        // must not widen Q from the range of D.  Doing so changed a Pyrope
        // `reg p:u3` into a four-bit register after inference; the spare bit then
        // leaked across an adjacent three-bit field in an aggregate pack.
        // An UNSIZED register has bits==0, falls through, and is still inferred
        // by process_flop below.
        auto dpin = node.create_driver_pin(0);
        auto bits = bits_of(dpin);
        if (bits) {
          Bitwidth_range bw;
          if (livehd::graph_util::is_unsign(dpin)) {
            bw.set_ubits_range(bits);
          } else {
            bw.set_sbits_range(bits);
          }
          bwmap.insert_or_assign(dpin.get_class_index(), bw);
          continue;
        }
      }

      if (op == Ntype_op::Or) {
        if (inp_edges.size() == 1) {
          process_assignment_or(node, inp_edges);
        } else {
          process_bit_or(node, inp_edges);
        }
      } else if (op == Ntype_op::Xor) {
        process_bit_xor(node, inp_edges);
      } else if (op == Ntype_op::Rxor || op == Ntype_op::Popcount) {
        const int maximum = op == Ntype_op::Rxor ? 1 : livehd::graph_util::reduction_count(node);
        adjust_bw(node.create_driver_pin(0), Bitwidth_range(*Dlop::create_integer(0), *Dlop::create_integer(maximum)));
      } else if (op == Ntype_op::Ror) {
        process_ror(node, inp_edges);
      } else if (op == Ntype_op::And) {
        process_bit_and(node, inp_edges);
      } else if (op == Ntype_op::AttrSet) {
        process_attr_set(node);
      } else if (op == Ntype_op::Memory) {
        process_memory(node);
      } else if (op == Ntype_op::Sum) {
        process_sum(node, inp_edges);
      } else if (op == Ntype_op::Mult) {
        process_mult(node, inp_edges);
      } else if (op == Ntype_op::Div) {
        process_div(node);
      } else if (op == Ntype_op::SRA) {
        process_sra(node, inp_edges);
      } else if (op == Ntype_op::SHL) {
        process_shl(node, inp_edges);
      } else if (op == Ntype_op::Not) {
        process_not(node, inp_edges);
      } else if (op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch) {
        // A Latch takes the same path (2f-latch M2): process_flop reads only
        // `din` and `initial`, and both now exist on the Latch cell with the
        // same pin ids. Before this a latch's q simply never got a width.
        process_flop(node);
      } else if (op == Ntype_op::Mux) {
        process_mux(node, inp_edges);
      } else if (op == Ntype_op::Hotmux) {
        process_hotmux(node, inp_edges);
      } else if (op == Ntype_op::GT || op == Ntype_op::LT || op == Ntype_op::EQ) {
        process_comparator(node);
      } else if (op == Ntype_op::Get_mask) {
        process_get_mask(node);
      } else if (op == Ntype_op::Set_mask) {
        process_set_mask(node);
      } else if (op == Ntype_op::Concat) {
        process_concat(node);
      } else if (op == Ntype_op::Sext) {
        process_sext(node, inp_edges);
      } else if (op == Ntype_op::Sub) {
        set_subgraph_boundary_bw(node);
      }
      if (node.is_invalid()) {
        continue;
      }
    }

    // Drain pending added nodes (Get_mask inserted by insert_tposs_nodes).
    while (!pending_added_nodes.empty()) {
      auto add_batch = std::move(pending_added_nodes);
      pending_added_nodes.clear();
      for (auto add : add_batch) {
        if (!add.is_invalid()) {
          process_get_mask(add);
        }
      }
    }

    // Top-level IO widths are the source RTL interface contract. Keep them
    // unchanged here; inferred internal ranges may be wider than the external
    // port expression width and must not be propagated back to GraphIO pins.

    if (discovered_some_backward_nodes_try_again && n_iterations < max_iterations) {
      Pass::info("BW-> some nodes need to back propagate width\n");
      discovered_some_backward_nodes_try_again = false;
    }
    ++n_iterations;
    if (n_iterations > max_iterations) {
      Pass::info("BW aborting after {} iterations (may require a cprop pass)", max_iterations);
      break;
    }
  }

  if (!not_finished && gio) {
    // Delete leftover AttrSet nodes.
    std::vector<hhds::Node_class> attrs;
    for (auto node : g->body().nodes()) {
      if (type_op_of(node) == Ntype_op::AttrSet) {
        attrs.push_back(node);
      }
    }
    for (auto node : attrs) {
      if (!node.is_invalid()) {
        try_delete_attr_node(node);
      }
    }
  }

  // Only on a CONVERGED run, and only AFTER remove_mask_identities: an aborted
  // fixed point (`BW aborting after N iterations`) leaves lane drivers on
  // TolG's conservative over-wide carrier, and this is a `.fatal()` in every
  // build mode (it used to be a debug-only `I()` retried each iteration), so an
  // un-converged design would hard-fail the compile in release. And the mask
  // removal below rewires lane sinks, so checking before it would miss exactly
  // the violations it can introduce.
  if (!not_finished) {
    remove_mask_identities(g);
    Bitwidth_rewrite{}.run(
        *g,
        [&](const hhds::Pin_class& pin) {
          const auto it = bwmap.find(pin.get_class_index());
          return it != bwmap.end() && it->second.is_always_positive() ? it->second.get_ubits() : -1;
        },
        [&](const hhds::Pin_class& pin) { bwmap.erase(pin.get_class_index()); },
        [&](const hhds::Pin_class& pin, int width) {
          if (bwmap.contains(pin.get_class_index())) {
            return;
          }
          Bitwidth_range range;
          range.set_ubits_range(std::max(1, width));
          bwmap.emplace(pin.get_class_index(), range);
          set_bits_sign(pin, range);
        });
    for (auto node : g->body().nodes()) {
      if (type_op_of(node) != Ntype_op::Concat) {
        continue;
      }
      const auto lanes = livehd::graph_util::concat_lanes(node);
      const auto bad   = livehd::graph_util::concat_lane_violation(lanes);
      if (bad.empty()) {
        continue;
      }
      livehd::diag::Span span;
      const auto         source = node.attr(hhds::attrs::srcid);
      if (source.has() && source.get() != 0) {
        span = g->source_locator().resolve_span(source.get());
      }
      // Name the DEFINITION and the node: this fires on a whole-design read
      // where the srcid often resolves to nothing (a generated .v carries no
      // usable span), and "some Concat somewhere" is not actionable across a
      // thousand modules -- localizing it was otherwise a bisect over the
      // module set.
      livehd::diag::err("pass.bitwidth", "concat-lane-width", "internal")
          .at(span)
          .msg("{}", bad)
          .note(std::format("in definition '{}', node {}", g->get_name(), livehd::graph_util::debug_name(node)))
          .note(std::format("lane table (MSB-first): {}",
                            [&] {
                              std::string t;
                              for (size_t li = 0; li < lanes.size(); ++li) {
                                const auto& l  = lanes[li];
                                t             += std::format("{}[{}] drv={} bits={} window={}",
                                                             t.empty() ? "" : ", ",
                                                             li,
                                                             livehd::graph_util::wire_name(l.value),
                                                             livehd::graph_util::lane_value_bits(l.value),
                                                             l.width);
                              }
                              return t;
                            }()))
          // The offending lane driver's OWN operands and their inferred ranges.
          // A too-wide lane driver is always inherited from an operand, so the
          // lane table alone stops one link short of the cause.
          .note(std::format("offending driver's operands: {}",
                            [&] {
                              std::string t;
                              for (const auto& l : lanes) {
                                if (livehd::graph_util::concat_lane_fits(l)) {
                                  continue;
                                }
                                auto drv = l.value.get_master_node();
                                if (drv.is_invalid()) {
                                  return std::string{"<invalid master>"};
                                }
                                t += std::format("{} <= ", livehd::graph_util::debug_name(drv));
                                for (const auto& in_pin : drv.inp_sorted_pins()) {
                                  const auto  in_drv = in_pin.get_driver_pin();
                                  const auto  it     = bwmap.find(in_drv.get_class_index());
                                  std::string rng    = "unmapped";
                                  if (it != bwmap.end()) {
                                    rng = std::format(
                                        "[{}..{}] ubits={} sbits={} pos={}",
                                        it->second.get_min().to_pyrope(),
                                        it->second.get_max().to_pyrope(),
                                        it->second.is_always_positive() ? std::to_string(it->second.get_ubits()) : "n/a",
                                        it->second.get_sbits(),
                                        it->second.is_always_positive());
                                  }
                                  t += std::format("{{pid={} drv={} bits={} unsign={} {}}} ",
                                                   static_cast<int>(in_pin.get_port_id()),
                                                   livehd::graph_util::wire_name(in_drv),
                                                   livehd::graph_util::bits_of(in_drv),
                                                   livehd::graph_util::is_unsign(in_drv),
                                                   rng);
                                }
                                break;
                              }
                              return t;
                            }()))
          .fatal();
    }
  }
  report_unbounded(g);
}

void Bitwidth::remove_mask_identities(hhds::Graph* g) {
  // Only completed ranges justify removing a precision boundary. A node's
  // first visit can precede a later range widening in this same inference run.
  std::vector<hhds::Node_class> masks;
  for (auto node : g->body().nodes(hhds::Node_order::forward)) {
    if (type_op_of(node) == Ntype_op::Get_mask || type_op_of(node) == Ntype_op::And) {
      masks.push_back(node);
    }
  }
  for (auto node : masks) {
    hhds::Pin_class source;
    hhds::Pin_class mask;
    if (type_op_of(node) == Ntype_op::Get_mask) {
      source = get_driver_of_sink_name(node, "a");
      mask   = get_driver_of_sink_name(node, "mask");
    } else {
      // SNAPSHOT: the And is rewired and deleted further down this loop body.
      const auto inputs = node.inp_pins_snapshot();
      if (inputs.size() != 2) {
        continue;
      }
      const auto pos = inputs[0].get_driver_pin().is_const() ? 0 : 1;
      mask           = inputs[pos].get_driver_pin();
      source         = inputs[1 - pos].get_driver_pin();
    }
    if (source.is_invalid() || !mask.is_const()) {
      continue;
    }
    const auto& value = const_of(mask);
    // is_mask alone is not enough: only a finite mask beginning at bit zero
    // preserves positions. Sparse and offset selections pack their bits.
    if (!is_finite_low_mask(value)) {
      continue;
    }
    const auto range = bwmap.find(source.get_class_index());
    if (range == bwmap.end() || !range->second.is_always_positive() || !range->second.get_max().le_op(value)->is_known_true()) {
      continue;
    }
    const auto source_bits = is_graph_input_pin(source)
                                 ? static_cast<int32_t>(g->get_io()->get_bits(livehd::graph_util::pin_name_of(source)))
                                 : bits_of(source);
    bool       safe        = source_bits > 0;
    for (const auto& edge : node.out_edges()) {
      // REPEATED OPERAND refusal, restated for one-driver-per-sink-pin.
      //
      // It used to ask whether `source` was among the DRIVERS OF THIS SINK PIN,
      // because a repeated operand of a commutative cell was spelled as a second
      // driver on one pin -- and rewiring the mask away would have made two
      // identical (driver, sink) pairs, which hhds silently dedups, dropping an
      // operand (Sum(a,a) = 2a collapsing to a).
      //
      // That spelling is gone: a sink pin has exactly one driver, so
      // get_driver_pins() on this pin can only ever return the mask itself and
      // the test was dead. The same repeat is now a SECOND PIN OF THE SAME BANK
      // on the same consumer, so that is what is checked. Deliberately kept
      // rather than deleted: the refusal is conservative (at worst a missed
      // mask removal), and dropping it would change which cells this pass
      // rewrites, which is not what a data-structure migration is for.
      if (const auto consumer_node = edge.sink.get_master_node(); !consumer_node.is_invalid()) {
        const auto consumer_op   = type_op_of(consumer_node);
        const auto consumer_bank = Ntype::sink_bank(consumer_op, edge.sink.get_port_id());
        for (const auto& other : consumer_node.inp_sorted_pins()) {
          if (other == edge.sink || Ntype::sink_bank(consumer_op, other.get_port_id()) != consumer_bank) {
            continue;
          }
          if (other.get_driver_pin() == source) {
            safe = false;
          }
        }
      }
      int  declared    = 0;
      bool lane_window = false;  // a Concat window only forbids a WIDER driver
      if (is_graph_output_pin(edge.sink)) {
        declared = g->get_io()->get_bits(livehd::graph_util::pin_name_of(edge.sink));
      } else if (auto consumer = edge.sink.get_master_node(); type_op_of(consumer) == Ntype_op::Sub) {
        if (auto io = consumer.get_subnode_io()) {
          declared = io->get_bits(edge.sink.get_pin_name());
        }
      } else if (type_op_of(consumer) == Ntype_op::Concat) {
        // The third width contract in this IR: a lane driver may only be
        // NARROWER than its declared window; a wider one shifts every lane
        // above it and is an internal compile error (concat_lane_violation).
        const auto pid = edge.sink.get_port_id();
        if ((pid % 2) != 0) {
          safe = false;  // the width operand itself: never rewire
        } else {
          const auto   lanes = livehd::graph_util::concat_lanes(consumer);
          const size_t idx   = static_cast<size_t>(pid) / 2;
          if (lanes.empty() || idx >= lanes.size()) {
            safe = false;  // malformed/undecodable table: keep the mask
          } else {
            declared    = lanes[idx].width;
            lane_window = true;
          }
        }
      }
      if (declared > 0 && (lane_window ? source_bits > declared : declared != source_bits)) {
        safe = false;
      }
    }
    if (!safe) {
      continue;
    }
    for (const auto& sink : consumer_sinks(node)) {  // snapshot: the loop rewires what it walks
      source.connect_sink(sink);
    }
    bwmap.erase(node.get_driver_pin(0).get_class_index());
    node.del_node();
  }
}

// After the iteration budget is spent, every materialised driver pin should
// carry a bounded width (bits>0) and, with it, a resolved sign. Anything still
// at bits==0 means the fixed point could not bound that pin; surface it as a
// diag warning so the user knows a type/width annotation (or a cprop pass) is
// needed. Get_mask/Sext placeholder pins inserted by this pass with no
// resolved width are likewise skipped (consts and multi-driver Sub/Memory
// carry their width elsewhere).
void Bitwidth::report_unbounded(hhds::Graph* g) {
  std::vector<std::string> unbounded;
  for (auto node : g->body().nodes()) {
    auto op = type_op_of(node);
    if (op == Ntype_op::Invalid || Ntype::has_multiple_driver_pins(op)) {
      continue;
    }

    auto dpin = node.create_driver_pin(0);
    if (dpin.is_invalid() || dpin.is_const()) {
      continue;
    }

    if (bits_of(dpin) == 0) {
      unbounded.emplace_back(debug_name(node));
    }
  }

  if (unbounded.empty()) {
    return;
  }

  constexpr size_t max_listed = 8;
  std::string      list;
  for (size_t i = 0; i < unbounded.size() && i < max_listed; ++i) {
    absl::StrAppend(&list, i ? ", " : "", unbounded[i]);
  }
  if (unbounded.size() > max_listed) {
    absl::StrAppend(&list, ", (+", unbounded.size() - max_listed, " more)");
  }

  auto msg = std::format("bitwidth could not bound bits/sign for {} driver pin(s) after {} iteration(s): {}",
                         unbounded.size(),
                         max_iterations,
                         list);

  livehd::diag::sink().emit(livehd::diag::Diagnostic{
      .severity = livehd::diag::Severity::warning,
      .code     = "bitwidth-unbounded",
      .category = "bitwidth",
      .pass     = "pass.bitwidth",
      .message  = msg,
      .hint     = "annotate the value's width/type, or run a cprop pass before bitwidth to resolve constants",
  });
}

void Bitwidth::dump(hhds::Graph* g) {
  for (auto node : g->body().nodes()) {
    auto dpin = node.create_driver_pin(0);
    auto it   = bwmap.find(dpin.get_class_index());
    if (it == bwmap.end()) {
      std::print("node:{} UNKNOWN\n", debug_name(node));
    } else if (it->second.is_always_positive()) {
      std::print("node:{} pos |", debug_name(node));
      it->second.dump();
    } else if (it->second.is_always_negative()) {
      std::print("node:{} neg |", debug_name(node));
      it->second.dump();
    } else {
      std::print("node:{} both|", debug_name(node));
      it->second.dump();
    }
  }
}

void Bitwidth::try_delete_attr_node(hhds::Node_class& node) {
  Attr attr = Attr::Set_other;
  if (is_sink_connected(node, "field")) {
    auto key_dpin = get_driver_of_sink_name(node, "field");
    if (!key_dpin.is_const()) {
      return;
    }
    attr = get_key_attr(const_of(key_dpin).to_field());
    if (attr == Attr::Set_other) {
      return;
    }
  }

  if (attr == Attr::Set_dp_assign) {
    auto dpin_lhs = get_driver_of_sink_name(node, "parent");
    auto dpin_rhs = get_driver_of_sink_name(node, "value");
    auto it1      = bwmap.find(dpin_lhs.get_class_index());
    auto it2      = bwmap.find(dpin_rhs.get_class_index());

    I(it1 != bwmap.end());
    I(it2 != bwmap.end());

    auto bw_lhs = it1->second;
    auto bw_rhs = it2->second;

    if (bw_rhs.get_sbits() > bw_lhs.get_sbits()) {
      auto bw_lhs_bits = bw_lhs.is_always_positive() ? bw_lhs.get_ubits() : bw_lhs.get_sbits();

      auto mask_node = create_typed_node(*current_graph, Ntype_op::And);
      auto mask_dpin = mask_node.create_driver_pin(0);
      set_bits(mask_dpin, bw_lhs_bits);

      auto mask_const   = Dlop::get_mask_value(bw_lhs_bits);
      auto all_one_dpin = create_const(*current_graph, *mask_const);

      bwmap.insert_or_assign(mask_dpin.get_class_index(), Bitwidth_range(Dlop::create_integer(0), mask_const));
      dpin_rhs.connect_sink(setup_sink_by_name(mask_node, "as"));
      all_one_dpin.connect_sink(setup_sink_by_name(mask_node, "as"));
      // live-reconnect each consumer to the masked value; del_node bulk-drops
      // node's edges (no per-edge find).
      for (const auto& sink : consumer_sinks(node)) {  // snapshot: the loop rewires what it walks
        mask_dpin.connect_sink(sink);
      }
      node.del_node();
      return;
    } else {
      // live-reconnect rhs to each port-0 consumer; del_node bulk-drops all of
      // node's edges, including any non-port-0 (no per-edge find).
      // Snapshot: the loop rewires what it walks. Only port 0's consumers move;
      // del_node below drops whatever else the node still drives.
      for (const auto& out_pin : node.out_pins_snapshot()) {
        if (out_pin.get_port_id() != 0) {
          continue;
        }
        std::vector<hhds::Pin_class> sinks;
        for (const auto& e : out_pin.out_edges()) {
          sinks.push_back(e.sink);
        }
        for (const auto& sink : sinks) {
          sink.connect_driver(dpin_rhs);
        }
      }
      node.del_node();
      return;
    }
  }

  if (is_sink_connected(node, "parent")) {
    auto data_dpin = get_driver_of_sink_name(node, "parent");
    // live-reconnect each consumer to the parent driver; the shared del_node
    // below bulk-drops node's edges (no per-edge find).
    for (const auto& sink : consumer_sinks(node)) {  // snapshot: the loop rewires what it walks
      sink.connect_driver(data_dpin);
    }
  } else {
    auto data_dpin = create_const(*current_graph, *Dlop::create_integer(0));
    // live-reconnect each consumer to const-0; the shared del_node bulk-drops
    // node's edges (no per-edge find).
    for (const auto& sink : consumer_sinks(node)) {  // snapshot: the loop rewires what it walks
      sink.connect_driver(data_dpin);
    }
  }
  node.del_node();
}

void Bitwidth::set_subgraph_boundary_bw(hhds::Node_class& node) {
  // Look up the sub-graph via library.
  auto sub_graph = node.get_subnode_graph();
  if (!sub_graph) {
    Pass::info("Global IO connection pass cannot find existing subgraph in lgdb");
    return;
  }
  auto sub_gio2 = sub_graph->get_io();
  if (!sub_gio2) {
    return;
  }

  for (const auto& d : sub_gio2->get_output_pin_decls()) {
    auto top_dpin = node.create_driver_pin(d.name);
    auto sub_out  = sub_graph->get_output_pin(d.name);

    Bitwidth_range bw;
    auto           bits = static_cast<int32_t>(d.bits);
    if (sub_out.is_invalid() || bits == 0) {
      continue;
    }
    // Same rule as the top-level IO seeding: an instance port's `bits` is the
    // LITERAL declared bus width, so unsigned takes ubits(bits) and signed
    // takes sbits(bits). The old `bits - 1` seeded a 9-bit unsigned child port
    // as [0..255] instead of [0..511]; the `bits == 1` special case just below
    // it was the same bug patched for one width (set_range(0,1) IS
    // set_ubits_range(1)), which is what gave the rule away.
    if (d.unsign) {
      bw.set_ubits_range(bits);
    } else {
      bw.set_sbits_range(bits);
    }
    adjust_bw(top_dpin, bw);
  }
}
