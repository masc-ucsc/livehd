//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cprop.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"
#include "split_selfref.hpp"
#include "pass_cprop.hpp"
#include "perf_tracing.hpp"

using livehd::graph_util::bits_of;
using livehd::graph_util::const_value_of;
using livehd::graph_util::create_const;
using livehd::graph_util::create_typed_node;
using livehd::graph_util::debug_name;
using livehd::graph_util::find_sink_pin;
using livehd::graph_util::is_const_pin;
using livehd::graph_util::is_graph_input_pin;
using livehd::graph_util::is_graph_output_pin;
using livehd::graph_util::type_op_of;

#define TRACE(x)
// #define TRACE(x) x

namespace {

void sort_inp(livehd::graph_util::Edge_vec& edges) {
  std::sort(edges.begin(), edges.end(), [](const hhds::Edge_class& a, const hhds::Edge_class& b) {
    return a.sink.get_port_id() < b.sink.get_port_id();
  });
}

livehd::graph_util::Edge_vec ordered_inp_edges(const hhds::Node_class& node) {
  auto e = node.inp_edges();
  sort_inp(e);
  return e;
}

using livehd::graph_util::hydrate_const;
using livehd::graph_util::setup_sink_by_name;

// "Is there an edge from `driver` to `sink`?"
[[nodiscard]] bool is_driver_connected_to_sink(const hhds::Pin_class& driver, const hhds::Pin_class& sink) {
  if (driver.is_invalid() || sink.is_invalid()) {
    return false;
  }
  for (const auto& e : driver.out_edges()) {
    if (e.sink == sink) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] std::string sink_pin_name(const hhds::Pin_class& spin) {
  if (spin.is_invalid()) {
    return {};
  }
  auto master = spin.get_master_node();
  auto op     = type_op_of(master);
  return Ntype::get_sink_name(op, spin.get_port_id());
}

// ---- packed-wire slice fold helpers ---------------------------------------
// A firtool/Chisel bundle-as-UInt writes a wide net as an Or of constant-shifted
// disjoint fields and reads fields back as constant slices. At WORD level the net
// is one node, so write-field-A/read-field-B looks like a combinational cycle even
// though the bit ranges are disjoint. Resolving each constant slice to the one
// operand that drives it removes the false edge (and is a plain win regardless of
// any cycle). See graph/split_selfref.cpp for the cycle-gated, node-CREATING
// rules (Mux/EQ/distribute) this deliberately does NOT duplicate.

[[nodiscard]] hhds::Pin_class drv_at(const hhds::Node_class& n, uint32_t pid) {
  for (const auto& e : n.inp_edges()) {
    if (static_cast<uint32_t>(e.sink.get_port_id()) == pid) {
      return e.driver;
    }
  }
  return {};
}

constexpr std::pair<int, int> kFpBail{-1, -1};

// The CONSTANT shift amount of a SHL, or <0 when not a bounded constant.
[[nodiscard]] int const_shl_amount(const hhds::Node_class& m) {
  auto kd = drv_at(m, 1);
  if (kd.is_invalid() || !is_const_pin(kd)) {
    return -1;
  }
  auto kc = hydrate_const(kd);
  if (kc.has_unknowns() || kc.is_negative() || !kc.is_just_i64()) {
    return -1;
  }
  auto k = kc.to_just_i64();
  return (k < 0 || k > (1 << 28)) ? -1 : static_cast<int>(k);
}

// The half-open [begin,end) bit range of a Get_mask's CONSTANT mask, or kFpBail.
// Rejects the `-1` to-unsigned idiom and noncontiguous/negative masks.
[[nodiscard]] std::pair<int, int> const_mask_range(const hhds::Node_class& m) {
  auto md = drv_at(m, 2);
  if (md.is_invalid() || !is_const_pin(md)) {
    return kFpBail;
  }
  auto mc = hydrate_const(md);
  if (mc.has_unknowns() || !mc.is_positive()) {
    return kFpBail;  // includes mask == -1
  }
  auto [b, e] = mc.get_mask_range();  // {-1,-1} also signals noncontiguous
  if (b < 0 || e <= b) {
    return kFpBail;
  }
  return {b, e};
}

// footprint(p): a sound OVER-approximation [lo,hi) of the bit positions where `p`
// can be nonzero; kFpBail = "cannot bound". Over-approximating only makes the
// disjointness test below HARDER to satisfy (it refuses), so it is the safe
// direction; UNDER-approximating would pick the wrong operand and miscompile.
//
// GOTCHA: LiveHD stores bits = magnitude+1, so an unsigned pin's significant
// width is bits-1. Using bits here makes adjacent packed fields overlap by one
// bit, disjointness always fails, and the fold silently never fires.
[[nodiscard]] std::pair<int, int> footprint(const hhds::Pin_class& p, int depth) {
  if (p.is_invalid() || depth > 16) {
    return kFpBail;
  }
  if (is_const_pin(p)) {
    auto c = hydrate_const(p);
    if (c.is_negative()) {
      return kFpBail;
    }
    if (c.has_unknowns()) {
      return {0, c.get_bits()};  // a '?'-const still occupies its declared width
    }
    int fb = c.get_first_bit_set();
    int lb = c.get_last_bit_set();
    if (fb < 0 || lb < 0) {
      return {0, 0};  // known zero contributes no bits
    }
    return {fb, lb + 1};
  }
  if (is_graph_input_pin(p)) {
    return kFpBail;  // a port reads SIGNED whatever its unsign metadata says
  }
  auto m = p.get_master_node();
  if (m.is_invalid()) {
    return kFpBail;
  }
  auto op = type_op_of(m);
  if (op == Ntype_op::SHL) {
    int k = const_shl_amount(m);
    if (k < 0) {
      return kFpBail;
    }
    auto fx = footprint(drv_at(m, 0), depth + 1);
    if (fx.first < 0) {
      return kFpBail;
    }
    if (fx.second <= fx.first) {
      return {0, 0};
    }
    return {fx.first + k, fx.second + k};
  }
  if (op == Ntype_op::Get_mask) {
    auto r = const_mask_range(m);
    if (r.first >= 0) {
      int w = r.second - r.first;  // extracted bits are packed down to [0,w)
      return w <= 1 ? std::pair<int, int>{0, 1} : std::pair<int, int>{0, w};
    }
    // else fall through to the generic bound
  }
  if (!livehd::graph_util::is_unsign(p)) {
    return kFpBail;  // a signed x makes shl(x,k) set every bit above k
  }
  int b = bits_of(p);
  if (b == 0) {
    return kFpBail;  // unsized (Nconst/Sub/Memory/IO stay exempt) -> [k,k) would
                     // read as EMPTY and fold a live value to constant 0
  }
  return {0, std::max(1, b - 1)};
}

}  // namespace

void Cprop::collapse_forward_same_op(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered) {
  auto op = type_op_of(node);

  // out_edges() is a lazy view; rewriting an edge (connect_driver/del_edge)
  // invalidates an in-flight iterator. Rather than snapshot the (possibly huge)
  // fan-out into a vector, re-scan from the front after each rewrite: find the
  // next same-op consumer, splice this node's inputs into it, drop the edge, and
  // restart. Edges to a different op (or mismatched port) are left in place;
  // once none remain the node is fully collapsed and can be deleted.
  bool progressed = true;
  while (progressed) {
    progressed = false;
    for (const auto& out : node.out_edges()) {
      if (type_op_of(out.sink.get_master_node()) != op) {
        continue;
      }
      if (out.driver.get_port_id() != out.sink.get_port_id()) {
        continue;
      }

      for (auto& inp : inp_edges_ordered) {
        if (op == Ntype_op::Xor) {
          if (is_driver_connected_to_sink(inp.driver, out.sink)) {
            out.sink.del_sink(inp.driver);
          } else {
            out.sink.connect_driver(inp.driver);
          }
        } else if (op == Ntype_op::Or || op == Ntype_op::And) {
          out.sink.connect_driver(inp.driver);
        } else {
          I(op != Ntype_op::Sum);
          out.sink.connect_driver(inp.driver);
        }
      }

      out.del_edge();
      progressed = true;
      break;  // iterator invalidated by the rewrite; restart the scan
    }
  }
  if (!node.has_out_edges()) {  // every consumer collapsed -> node is dead
    node.del_node();
  }
}

void Cprop::collapse_forward_sum(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered) {
  if (inp_edges_ordered.size() > 32) {
    return;
  }

  I(type_op_of(node) == Ntype_op::Sum);
  // Restart-scan the lazy out_edges view after each rewrite (no fan-out copy):
  // splice into the next Sum consumer, drop its edge, restart. Non-Sum consumers
  // stay; once all Sum edges are gone the node is fully merged forward.
  bool progressed = true;
  while (progressed) {
    progressed = false;
    for (const auto& out : node.out_edges()) {
      auto next_sum_node = out.sink.get_master_node();
      if (type_op_of(next_sum_node) != Ntype_op::Sum) {
        continue;
      }

      for (auto& inp : inp_edges_ordered) {
        // Sum(A,Sum(B,C))  = Sum(A+C,B)
        // Sum(Sum(A,B),C)) = Sum(A+C,B)
        if (inp.sink.get_port_id() == 0 && out.sink.get_port_id() == 0) {
          out.sink.connect_driver(inp.driver);
        } else if (inp.sink.get_port_id() == 0 && out.sink.get_port_id() == 1) {
          out.sink.connect_driver(inp.driver);
        } else if (inp.sink.get_port_id() == 1 && out.sink.get_port_id() == 0) {
          setup_sink_by_name(next_sum_node, "bs").connect_driver(inp.driver);
        } else {
          setup_sink_by_name(next_sum_node, "as").connect_driver(inp.driver);
        }
      }
      out.del_edge();
      progressed = true;
      break;  // iterator invalidated by the rewrite; restart the scan
    }
  }

  if (!node.has_out_edges()) {  // every Sum consumer merged forward -> node dead
    bwd_del_node(node);
  }
}

void Cprop::collapse_forward_always_pin0(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered) {
  auto op = type_op_of(node);

  for (const auto& out : node.out_edges()) {  // read-only width check
    auto out_bits = bits_of(out.driver);
    for (auto& inp : inp_edges_ordered) {
      auto inp_bits = bits_of(inp.driver);
      if (out_bits > 0 && inp_bits > 0 && out_bits != inp_bits) {
        return;
      }
    }
  }

  // Splice this node's inputs onto every consumer sink, then bwd_del_node
  // deletes the node in one shot (del_node bulk-drops its edges — no per-edge
  // del_edge lookup). Iterating the live out_edges while connecting is safe:
  // connect_driver/del_sink only touch the input/consumer pins, never node's
  // out-edge storage, and no node/pin is created to realloc the tables.
  for (const auto& out : node.out_edges()) {
    for (auto& inp : inp_edges_ordered) {
      if (op == Ntype_op::Xor) {
        if (is_driver_connected_to_sink(inp.driver, out.sink)) {
          out.sink.del_sink(inp.driver);
        } else {
          out.sink.connect_driver(inp.driver);
        }
      } else {
        out.sink.connect_driver(inp.driver);
      }
    }
  }

  bwd_del_node(node);
}

// Redirect every consumer of `node` to `new_dpin` and delete `node`. Returns
// FALSE without touching the graph when a consumer's width disagrees with
// new_dpin's — a blind reconnect would change the value that consumer reads. The
// bool is not cosmetic: a caller that fabricated `new_dpin`'s node before calling
// (rather than passing an existing operand) MUST check it and clean up the orphan
// on false, or it leaks. Existing callers pass an existing pin, so a false there
// is just a missed collapse (the node is left for later folds).
bool Cprop::collapse_forward_for_pin(hhds::Node_class& node, hhds::Pin_class new_dpin) {
  auto new_bits = bits_of(new_dpin);
  for (const auto& out : node.out_edges()) {  // read-only width check
    auto out_bits = bits_of(out.driver);
    if (out_bits > 0 && new_bits > 0 && out_bits != new_bits) {
      return false;
    }
  }

  // Redirect every consumer to new_dpin, then bwd_del_node deletes the node in
  // one shot (del_node bulk-drops its edges — no per-edge find). connect_sink
  // only grows new_dpin/sink storage, so the live walk stays valid.
  for (const auto& out : node.out_edges()) {
    new_dpin.connect_sink(out.sink);
  }

  bwd_del_node(node);
  return true;
}

bool Cprop::try_constant_prop(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered) {
  int n_inputs_constant = 0;
  int n_inputs          = 0;
  for (auto& e : inp_edges_ordered) {
    n_inputs++;
    if (!is_const_pin(e.driver)) {
      continue;
    }
    n_inputs_constant++;
  }

  if (n_inputs == n_inputs_constant && n_inputs) {
    replace_all_inputs_const(node, inp_edges_ordered);
    return true;
  } else if (n_inputs && n_inputs_constant >= 1) {
    replace_part_inputs_const(node, inp_edges_ordered);
    return true;
  }

  return false;
}

void Cprop::try_collapse_forward(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered) {
  auto op = type_op_of(node);

  if (inp_edges_ordered.size() == 1) {
    auto       prev_op      = type_op_of(inp_edges_ordered[0].driver.get_master_node());
    // A single-input Sum whose lone driver is on the SUBTRACT (b) pin is a
    // negation (0 - x = -x). collapse_forward_always_pin0 forwards the driver
    // UNCHANGED, which would drop the sign (turning -x into +x). Leave the Sum
    // node so cgen renders it as -(x).
    const bool sum_subtract = op == Ntype_op::Sum && sink_pin_name(inp_edges_ordered[0].sink) == "bs";
    if ((op == Ntype_op::Sum || op == Ntype_op::Mult || op == Ntype_op::Div || op == Ntype_op::And || op == Ntype_op::Or
         || op == Ntype_op::Xor)
        && !sum_subtract) {
      collapse_forward_always_pin0(node, inp_edges_ordered);
      return;
    }
    if (prev_op == Ntype_op::Get_mask) {
      if (op == Ntype_op::Get_mask) {
        collapse_forward_always_pin0(node, inp_edges_ordered);
        return;
      }
    }
  }

  if (op == Ntype_op::Sum) {
    collapse_forward_sum(node, inp_edges_ordered);
  } else if (op == Ntype_op::Mult || op == Ntype_op::Or || op == Ntype_op::And || op == Ntype_op::Xor) {
    collapse_forward_same_op(node, inp_edges_ordered);
  } else if (op == Ntype_op::Mux || op == Ntype_op::Hotmux) {
    // All data arms identical -> the selector is irrelevant (a Hotmux's
    // zero/multi-hot error case stays a runtime property; cprop assumes the
    // unique-if assume holds, same as folding a Mux assumes an in-range sel).
    if (inp_edges_ordered.size() <= 1) {
      node.del_node();
      return;
    }
    auto& a_pin = inp_edges_ordered[1].driver;
    for (auto i = 2u; i < inp_edges_ordered.size(); ++i) {
      if (a_pin != inp_edges_ordered[i].driver) {
        return;
      }
    }
    collapse_forward_for_pin(node, a_pin);
  }
}

void Cprop::replace_part_inputs_const(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered) {
  auto op = type_op_of(node);
  if (op == Ntype_op::Mux) {
    auto& s_pin = inp_edges_ordered[0].driver;
    if (!is_const_pin(s_pin)) {
      return;
    }
    auto s_const = hydrate_const(s_pin);
    if (s_const.has_unknowns()) {
      return;
    }

    I(s_const.is_just_i64());
    size_t sel = s_const.to_just_i64();

    hhds::Pin_class a_pin;
    for (auto& e : inp_edges_ordered) {
      if (e.sink.get_port_id() == 0) {
        continue;
      }
      if (e.sink.get_port_id() == static_cast<hhds::Port_id>(sel + 1)) {
        a_pin = e.driver;
        break;
      }
    }

    if (a_pin.is_invalid()) {
#ifndef NDEBUG
      Pass::info("WARNING: mux selector:{} for a disconnected pin in mux. Using zero\n", sel);
#endif
      a_pin = create_const(*current_graph, *Dlop::create_integer(0));
    }

    collapse_forward_for_pin(node, a_pin);
  } else if (op == Ntype_op::Hotmux) {
    // Constant one-hot selector: collapse to the selected arm (bit i ->
    // p(i+1)). A zero/multi-hot constant violates the unique-if assume; warn
    // and keep the cell (cgen's case default models the runtime error).
    auto& s_pin = inp_edges_ordered[0].driver;
    if (!is_const_pin(s_pin)) {
      return;
    }
    auto s_const = hydrate_const(s_pin);
    if (s_const.has_unknowns() || !s_const.is_just_i64()) {
      return;
    }
    auto sel = s_const.to_just_i64();
    if (sel <= 0 || (sel & (sel - 1)) != 0) {
      Pass::info("WARNING: hotmux selector:{} is not one-hot (unique-if assume violated); not folding\n", sel);
      return;
    }
    size_t arm = 0;  // bit position of the hot bit
    while ((sel >> arm) != 1) {
      ++arm;
    }

    hhds::Pin_class a_pin;
    for (auto& e : inp_edges_ordered) {
      if (e.sink.get_port_id() == static_cast<hhds::Port_id>(arm + 1)) {
        a_pin = e.driver;
        break;
      }
    }
    if (a_pin.is_invalid()) {
#ifndef NDEBUG
      Pass::info("WARNING: hotmux selector:{} for a disconnected pin in hotmux. Using zero\n", sel);
#endif
      a_pin = create_const(*current_graph, *Dlop::create_integer(0));
    }
    collapse_forward_for_pin(node, a_pin);
  } else if (op == Ntype_op::EQ) {
    // FIXME: 1- eq(X,0) = not(ror(x))
  } else if (op == Ntype_op::Sum || op == Ntype_op::Or || op == Ntype_op::And) {
    hhds::Edge_class first_const_edge;
    int              nconstants = 0;
    int              npending   = 0;

    // Seed the accumulator with the op's identity (0 for Sum/Or, -1 for And).
    // A default Dlop is Invalid, which used to act as the additive identity but
    // now propagates to nil (hlop's non-numeric guard) — poisoning the fold.
    Dlop result;
    result = Dlop::create_integer(op == Ntype_op::And ? -1 : 0);

    livehd::graph_util::Edge_vec edge_it2;
    for (auto& i : inp_edges_ordered) {
      if (!is_const_pin(i.driver)) {
        if (npending == 0) {
          edge_it2.push_back(i);
        }
        npending++;
        continue;
      }

      auto c = hydrate_const(i.driver);

      ++nconstants;

      if (op == Ntype_op::Sum) {
        if (sink_pin_name(i.sink) == "as") {
          result = result.add_op(c);
        } else {
          I(sink_pin_name(i.sink) == "bs");
          result = result.sub_op(c);
        }
      } else if (op == Ntype_op::Or) {
        result = result.or_op(c);
      } else {
        I(op == Ntype_op::And);
        result = result.and_op(c);
      }

      if (nconstants == 1) {
        first_const_edge = i;
      } else {
        i.del_edge();
      }
    }

    if (nconstants > 1) {
      first_const_edge.del_edge();
      if (!result.is_known_zero()) {
        auto dpin = create_const(*current_graph, result);
        if (result.is_positive() || op == Ntype_op::Or) {
          setup_sink_by_name(node, "as").connect_driver(dpin);
        } else {
          setup_sink_by_name(node, "bs").connect_driver(dpin);
        }
      } else if (npending == 1 && !(op == Ntype_op::Sum && sink_pin_name(edge_it2[0].sink) == "bs")) {
        // Same guard as the nconstants==0 case below: a lone pending operand on a
        // Sum's subtract (b) pin is `0 - x` = -x, so forwarding x unchanged would
        // drop the sign. Leave the Sum node (it now holds only the b driver).
        collapse_forward_always_pin0(node, edge_it2);
      }
    } else if (nconstants == 1 && npending >= 1
               && ((op == Ntype_op::And && result.is_just_i64() && result.to_just_i64() == -1)
                   || (op == Ntype_op::Or && result.is_known_zero()))) {
      // Identity element: and(x.., -1) == and(x..), or(x.., 0) == or(x..).
      // Dropping it matters for codegen too: cgen renders -1 as `1'sh1`,
      // which only sign-extends in an all-signed Verilog expression — in a
      // mixed/unsigned context it reads as +1 and masks everything away.
      first_const_edge.del_edge();
      if (npending == 1) {
        collapse_forward_always_pin0(node, edge_it2);
      }
    } else if (npending == 0 && nconstants == 1) {
      collapse_forward_always_pin0(node, inp_edges_ordered);
    } else if (npending == 1 && nconstants == 0) {
      if (!(op == Ntype_op::Sum && sink_pin_name(edge_it2[0].sink) == "bs")) {
        collapse_forward_always_pin0(node, edge_it2);
      }
    }
  } else if (op == Ntype_op::SRA) {
    auto& amt_pin = inp_edges_ordered[1].driver;
    if (is_const_pin(amt_pin) && hydrate_const(amt_pin).is_known_zero()) {
      collapse_forward_for_pin(node, inp_edges_ordered[0].driver);
    }
  } else if (op == Ntype_op::SHL) {
    if (inp_edges_ordered.size() == 2) {
      auto& amt_pin = inp_edges_ordered[1].driver;
      if (is_const_pin(amt_pin) && hydrate_const(amt_pin).is_known_zero()) {
        collapse_forward_for_pin(node, inp_edges_ordered[0].driver);
      }
    }
  }
}

void Cprop::replace_all_inputs_const(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered) {
  auto op = type_op_of(node);
  if (op == Ntype_op::SHL) {
    // SHL b is single-driver (the one-hot multi-shift form was removed).
    auto a_pin = livehd::graph_util::get_driver_of_sink_name(node, "a");
    auto b_pin = livehd::graph_util::get_driver_of_sink_name(node, "b");
    if (a_pin.is_invalid() || b_pin.is_invalid()) {
      return;
    }
    Dlop val = hydrate_const(a_pin);
    Dlop amt = hydrate_const(b_pin);

    Dlop result;
    result = Dlop::create_integer(0);
    result = result.or_op(val.shl_op(amt));  // val << amt
    replace_node(node, result);

  } else if (op == Ntype_op::Ror) {
    Dlop result;
    result = Dlop::create_integer(0);
    for (auto& i : inp_edges_ordered) {
      auto c = hydrate_const(i.driver);
      result = result.ror_op(c);
    }

    replace_node(node, result);
  } else if (op == Ntype_op::Set_mask) {
    auto a_pin     = livehd::graph_util::get_driver_of_sink_name(node, "a");
    auto mask_pin  = livehd::graph_util::get_driver_of_sink_name(node, "mask");
    auto value_pin = livehd::graph_util::get_driver_of_sink_name(node, "value");

    if (a_pin.is_invalid()) {
      return;
    }
    Dlop val = hydrate_const(a_pin);

    if (!mask_pin.is_invalid() && !value_pin.is_invalid()) {
      auto mask  = hydrate_const(mask_pin);
      auto value = hydrate_const(value_pin);
      replace_node(node, val.set_mask_op(mask, value));
    } else {
      replace_node(node, val);
    }
  } else if (op == Ntype_op::Sum) {
    Dlop result;
    result = Dlop::create_integer(0);  // additive identity (Invalid no longer folds as 0)
    for (auto& i : inp_edges_ordered) {
      auto c = hydrate_const(i.driver);
      if (i.sink.get_port_id() == 0) {
        result = result.add_op(c);
      } else {
        result = result.sub_op(c);
      }
    }

    replace_node(node, result);
  } else if (op == Ntype_op::Or) {
    Dlop result;
    result = Dlop::create_integer(0);  // or identity (Invalid no longer folds as 0)
    for (auto& e : inp_edges_ordered) {
      auto c = hydrate_const(e.driver);
      result = result.or_op(c);
    }

    replace_logic_node(node, result);

  } else if (op == Ntype_op::And) {
    Dlop result;
    result = Dlop::create_integer(-1);
    for (auto& i : inp_edges_ordered) {
      auto c = hydrate_const(i.driver);
      result = result.and_op(c);
    }

    replace_node(node, result);

  } else if (op == Ntype_op::EQ) {
    bool eq    = true;
    // EQ is a single-port, multi-edge "all drivers on port a are equal" cell.
    // When both comparator operands resolve to the SAME driver pin (e.g.
    // const==const), HHDS collapses the duplicate driver->sink edges into one,
    // leaving a single input edge. A 1-input "all-equal" is trivially true, and
    // the loop below already yields that (it starts at index 1), so no >1
    // assertion is warranted (try_constant_prop only calls this with n>=1).
    auto first = hydrate_const(inp_edges_ordered[0].driver);
    for (auto i = 1u; i < inp_edges_ordered.size(); ++i) {
      auto c = hydrate_const(inp_edges_ordered[i].driver);
      eq     = eq && !first.eq_op(c)->is_known_false();
    }

    Dlop result;
    result = Dlop::create_integer(eq ? 1 : 0);

    replace_node(node, result);
  } else if (op == Ntype_op::Mux) {
    auto sel_const = hydrate_const(inp_edges_ordered[0].driver);
    if (!sel_const.is_just_i64()) {
      return;  // unknown-bit selector (0sb? poison cond): keep the mux as-is
    }

    size_t sel = sel_const.to_just_i64();

    Dlop result;
    for (auto& e : inp_edges_ordered) {
      if (e.sink.get_port_id() == 0) {
        continue;
      }
      if (e.sink.get_port_id() == static_cast<hhds::Port_id>(sel + 1)) {
        result = hydrate_const(e.driver);
        break;
      }
    }

    if (result.get_bits() == 0) {
      result = Dlop::create_integer(0);
#ifndef NDEBUG
      Pass::info("WARNING: mux:{} selector:{} goes for disconnected pin in mux. Using zero\n", debug_name(node), sel);
#endif
    }

    replace_node(node, result);
  } else if (op == Ntype_op::Hotmux) {
    // All-const Hotmux: the selector must be one-hot (bit i -> p(i+1)).
    // A zero/multi-hot constant violates the unique-if assume; keep the cell
    // so cgen's case default models the runtime error.
    auto sel_const = hydrate_const(inp_edges_ordered[0].driver);
    I(sel_const.is_just_i64());

    auto sel = sel_const.to_just_i64();
    if (sel <= 0 || (sel & (sel - 1)) != 0) {
      Pass::info("WARNING: hotmux:{} selector:{} is not one-hot (unique-if assume violated); not folding\n", debug_name(node), sel);
      return;
    }
    size_t arm = 0;
    while ((sel >> arm) != 1) {
      ++arm;
    }

    Dlop result;
    for (auto& e : inp_edges_ordered) {
      if (e.sink.get_port_id() == static_cast<hhds::Port_id>(arm + 1)) {
        result = hydrate_const(e.driver);
        break;
      }
    }
    if (result.get_bits() == 0) {
      result = Dlop::create_integer(0);
#ifndef NDEBUG
      Pass::info("WARNING: hotmux:{} selector:{} goes for disconnected pin in hotmux. Using zero\n", debug_name(node), sel);
#endif
    }

    replace_node(node, result);
  } else if (op == Ntype_op::Mult) {
    Dlop result;
    result = Dlop::create_integer(1);
    for (auto& i : inp_edges_ordered) {
      auto c = hydrate_const(i.driver);
      result = result.mult_op(c);
    }

    replace_node(node, result);
  } else if (op == Ntype_op::Div) {
    I(inp_edges_ordered.size() == 2);
    Dlop a = hydrate_const(inp_edges_ordered[0].driver);
    Dlop b = hydrate_const(inp_edges_ordered[1].driver);

    auto result = a.div_op(b);

    replace_node(node, result);
  } else {
#ifndef NDEBUG
    Pass::info("FIXME: cprop still does not copy prop node:{}\n", debug_name(node));
#endif
  }
}

void Cprop::replace_node(hhds::Node_class& node, const Dlop& result) {
  auto dpin     = create_const(*current_graph, result);
  auto new_bits = bits_of(dpin);

  // Reconnect every consumer to the const, then bulk-delete the node (del_node
  // drops all of node's edges with no per-edge del_edge lookup). Width-mismatched
  // consumers need a freshly bit-adjusted const, but creating one mid-walk could
  // realloc the node/pin tables and invalidate the live iterator — so defer those
  // (rare) sinks and wire them after the walk. The deferred buffer is bounded by
  // the number of mismatches, never the full fan-out.
  absl::InlinedVector<std::pair<hhds::Pin_class, int32_t>, 2> mismatched;
  for (const auto& out : node.out_edges()) {
    auto out_bits = bits_of(out.driver);
    if (new_bits == out_bits || out_bits == 0) {
      dpin.connect_sink(out.sink);
    } else {
      mismatched.emplace_back(out.sink, out_bits);
    }
  }
  for (const auto& [sink, out_bits] : mismatched) {
    auto result2 = result.adjust_bits(out_bits);
    auto dpin2   = create_const(*current_graph, *result2);
    dpin2.connect_sink(sink);
  }

  node.del_node();
}

void Cprop::replace_logic_node(hhds::Node_class& node, const Dlop& result) {
  // Create the shared const up front (NOT lazily mid-walk: a create_const there
  // could realloc the node/pin tables and invalidate the live iterator), then
  // reconnect every consumer and delete the node in one shot — del_node drops
  // all of node's edges with no per-edge del_edge lookup.
  auto dpin_0 = create_const(*current_graph, result);
  for (const auto& out : node.out_edges()) {
    dpin_0.connect_sink(out.sink);
  }

  node.del_node();
}

bool Cprop::scalar_mux(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered) {
  if (inp_edges_ordered.size() != 3) {
    return false;
  }

  if (inp_edges_ordered[1].driver == inp_edges_ordered[2].driver) {
    return collapse_forward_for_pin(node, inp_edges_ordered[1].driver);
  }

  bool false_path_zero = false;
  if (is_const_pin(inp_edges_ordered[1].driver)) {
    auto v          = hydrate_const(inp_edges_ordered[1].driver);
    false_path_zero = v.is_known_zero() || v.is_string();
  }

  bool true_path_sel = inp_edges_ordered[0].driver == inp_edges_ordered[2].driver;

  // Mux selectors are 0/1 (port = sel+1), so mux(s,0,s) == s. The old
  // -1-as-true folds (mux(s,0,-1)->s, mux(s,s,-1)->s, mux(s,-1,s)->-1,
  // mux(s,s,0)->Not(s)) are only bit-accurate when the consumer reads a
  // single bit; for wider consumers they swap -1 (all ones) for 1 — e.g. a
  // yosys-consolidated 8-bit write-enable mux(reset,0,-1) must yield 0xff,
  // not 1 (caught by lgcheck BMC on mem_reset). Keep only the sound rule.
  if (false_path_zero && true_path_sel) {
    return collapse_forward_for_pin(node, inp_edges_ordered[0].driver);
  }

  return false;
}

void Cprop::scalar_sext(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered) {
  const auto& pos_dpin = inp_edges_ordered[1].driver;
  if (!is_const_pin(pos_dpin)) {
    return;
  }

  int64_t self_pos;
  {
    auto v = hydrate_const(pos_dpin);
    if (!v.is_just_i64()) {
      return;
    }
    self_pos = v.to_just_i64();
  }

  const auto& wire_dpin = inp_edges_ordered[0].driver;

  // Sext(X,1) feeding a Mux can be bypassed (a Mux selector/arm is contractually
  // 1 bit) ONLY when X is already 1 bit. For a wider X (e.g. the offset-0 select
  // of a bmux mux-tree, where X is the full ~addr signal) bypassing connects a
  // multi-bit value to the 1-bit Mux selector, which cgen then emits as a
  // reduction-OR (`if (x)` instead of `if (x[0])`) — selecting the wrong arm.
  if (self_pos == 1 && bits_of(wire_dpin) == 1) {
    // Restart-scan the lazy view: bypass each Mux consumer (rewrite + drop edge)
    // and restart; non-Mux consumers stay. No fan-out copy.
    bool progressed = true;
    while (progressed) {
      progressed = false;
      for (const auto& e : node.out_edges()) {
        if (type_op_of(e.sink.get_master_node()) == Ntype_op::Mux) {
          e.sink.connect_driver(wire_dpin);
          e.del_edge();
          progressed = true;
          break;  // iterator invalidated by the rewrite; restart the scan
        }
      }
    }
  }

  auto wire_master = wire_dpin.get_master_node();
  if (type_op_of(wire_master) != Ntype_op::Sext) {
    return;
  }

  // Sext(Sext(X,a),b) == Sext(X, min(a,b))
  auto parent_pos_dpin = livehd::graph_util::get_driver_of_sink_name(wire_master, "b");
  if (!is_const_pin(parent_pos_dpin)) {
    return;
  }

  auto parent_pos_const = hydrate_const(parent_pos_dpin);
  if (!parent_pos_const.is_just_i64()) {
    return;
  }
  auto parent_pos = parent_pos_const.to_just_i64();

  auto b = std::min(self_pos, parent_pos);
  if (b != self_pos) {
    auto new_const_dpin = create_const(*current_graph, *Dlop::create_integer(b));
    inp_edges_ordered[1].del_edge();
    setup_sink_by_name(node, "b").connect_driver(new_const_dpin);
  }

  auto parent_wire_dpin = livehd::graph_util::get_driver_of_sink_name(wire_master, "a");
  inp_edges_ordered[0].del_edge();
  setup_sink_by_name(node, "a").connect_driver(parent_wire_dpin);
}

hhds::Pin_class Cprop::try_find_single_driver_pin(hhds::Node_class& node, int64_t pos) {
  I(type_op_of(node) == Ntype_op::Set_mask);

  // Follow the Set_mask `a`-driver chain iteratively (the old recursion at the
  // tail was a non-terminating stack-overflow risk: a combinational cycle
  // through the `a` pins is structurally representable and the front-end does
  // not guarantee acyclicity). `pos` is invariant along the chain, so this is a
  // plain loop; the visited set breaks any `a`-pin cycle deterministically.
  absl::flat_hash_set<hhds::Class_index> visited;
  hhds::Node_class                       cur = node;

  while (true) {
    if (!visited.insert(cur.get_class_index()).second) {
      return {};  // cycle in the Set_mask `a`-chain
    }

    auto a_pin    = livehd::graph_util::get_driver_of_sink_name(cur, "a");
    auto mask_pin = livehd::graph_util::get_driver_of_sink_name(cur, "mask");
    if (a_pin.is_invalid() || mask_pin.is_invalid()) {
      return {};
    }
    if (!is_const_pin(mask_pin)) {
      return {};
    }

    auto mask_const               = hydrate_const(mask_pin);
    auto [range_begin, range_end] = mask_const.get_mask_range();
    if (pos >= range_end || pos < range_begin) {
      if (is_const_pin(a_pin)) {
        // get_mask is Pyrope's default-ZEXT bit-select: a non-negative mask packs
        // the selected bits LSB-first as an UNSIGNED value. Dlop::get_mask_op has a
        // single-bit quirk that returns the signed 1-bit -1 for a lone set bit;
        // `#[N]` zero-extends (a set bit is the unsigned 1), so correct it here to
        // match cgen's plain `a[N]` part-select — same fix as upass get_mask_zext
        // and pass/bitwidth's `gm`.
        const auto pos_mask = Dlop::get_mask_value(pos);
        auto       v        = hydrate_const(a_pin).get_mask_op(*pos_mask);
        if (!pos_mask->is_negative() && v->is_integer() && !v->has_unknowns() && v->is_negative()) {
          v = Dlop::create_integer(1);
        }
        return create_const(*current_graph, *v);
      }
      auto a_master = a_pin.get_master_node();
      if (type_op_of(a_master) != Ntype_op::Set_mask) {
        return {};
      }
      cur = a_master;  // walk to the next Set_mask in the chain
      continue;
    }
    if (range_begin == pos && range_end == (pos + 1)) {
      return livehd::graph_util::get_driver_of_sink_name(cur, "value");
    }

    return {};
  }
}

// Resolve a CONSTANT slice read of a packed wire to the operand that actually
// drives it: Get_mask(Or(.. shl(x_j,k_j) ..), mask[lo,hi)) -> Get_mask(x_j, ..).
// The firtool/Chisel bundle-as-UInt idiom writes a wide net as an Or of
// constant-shifted disjoint fields and reads fields back with constant slices;
// at word level that reads as a combinational cycle even though the bit ranges
// never overlap. Rewiring the read to its one real driver deletes the false
// edge. Every rule is node-NON-increasing and locally value-preserving, so this
// is an unconditional win and needs no cycle gate and no creation budget (unlike
// the node-creating Mux/EQ/distribute rules in graph/split_selfref.cpp).
//
// Returns true iff `node` was deleted (folded to a constant). A `false` return
// may still have REWIRED `node`, so callers must re-read its input edges.
bool Cprop::scalar_get_mask_packed(hhds::Node_class& node, const Dlop& mask_const) {
  if (!mask_const.is_positive()) {
    return false;  // -1 (to-unsigned) is consumed by Rule 4 before we get here
  }
  auto [lo, hi] = mask_const.get_mask_range();  // half-open; {-1,-1} = noncontiguous
  if (lo < 0 || hi <= lo) {
    return false;
  }

  auto cur = livehd::graph_util::get_driver_of_sink_name(node, "a");

  // No decreasing measure exists: the Or rule descends with [lo,hi) UNCHANGED, so
  // an Or->operand->Or chain inside a word-level SCC can revisit the same slice
  // and spin forever. A repeat means the slice depends on itself -- a GENUINE
  // bit-level cycle -- so stop and leave the node alone.
  absl::flat_hash_set<std::tuple<hhds::Class_index, int, int>> seen;

  for (;;) {
    if (cur.is_invalid() || is_const_pin(cur)) {
      break;
    }
    if (!seen.insert({cur.get_class_index(), lo, hi}).second) {
      break;
    }
    auto m = cur.get_master_node();
    if (m.is_invalid()) {
      break;
    }
    auto op = type_op_of(m);

    if (op == Ntype_op::Or) {
      // Or is N-ary on ONE sink; a driver on any other port is not this shape.
      hhds::Pin_class overlapper;
      int             n_over  = 0;
      bool            bad_pid = false;
      for (const auto& e : m.inp_edges()) {
        if (static_cast<uint32_t>(e.sink.get_port_id()) != 0) {
          bad_pid = true;
          break;
        }
        auto f = footprint(e.driver, 0);
        // An unbounded (kFpBail) operand counts as an OVERLAPPER: soundness rests
        // ONLY on the others being provably disjoint from [lo,hi).
        if (f.first < 0 || !(hi <= f.first || lo >= f.second)) {
          ++n_over;
          overlapper = e.driver;
        }
      }
      if (bad_pid || n_over > 1) {
        break;
      }
      if (n_over == 0) {  // no operand reaches this slice: it reads as zero
        replace_node(node, *Dlop::create_integer(0));
        return true;
      }
      cur = overlapper;  // unique cover: descend, range unchanged
      continue;
    }

    if (op == Ntype_op::SHL) {
      int k = const_shl_amount(m);
      if (k < 0) {
        break;
      }
      if (hi <= k) {  // entirely below the shifted value: zeros
        replace_node(node, *Dlop::create_integer(0));
        return true;
      }
      if (lo < k) {
        break;  // straddles the shift boundary; splitting would create nodes
      }
      auto x = drv_at(m, 0);
      if (x.is_invalid()) {
        break;
      }
      cur = x;
      lo -= k;
      hi -= k;
      continue;
    }

    if (op == Ntype_op::Get_mask) {
      auto r = const_mask_range(m);
      if (r.first < 0) {
        break;
      }
      int w = r.second - r.first;
      // A 1-bit Get_mask yields the SIGNED -1/0, so bits above it read the sign
      // rather than zero -- re-basing through it would be unsound.
      if (w <= 1 || hi > w) {
        break;
      }
      auto x = drv_at(m, 0);
      if (x.is_invalid()) {
        break;
      }
      cur = x;
      lo += r.first;
      hi += r.first;
      continue;
    }

    break;
  }

  auto a_now = livehd::graph_util::get_driver_of_sink_name(node, "a");
  if (cur.is_invalid() || cur == a_now) {
    return false;  // nothing resolved
  }
  I(hi > lo, "packed-slice fold produced an empty range");

  // Every rule preserves (hi-lo), so the mask popcount is invariant and the
  // pin's existing bits (= magnitude+1) stays correct: do NOT re-stamp bits/sign.
  auto old_master = a_now.get_master_node();
  auto edges      = node.inp_edges();  // snapshot before mutating
  for (auto e : edges) {
    auto pid = static_cast<uint32_t>(e.sink.get_port_id());
    if (pid == 0 || pid == 2) {
      e.del_edge();
    }
  }
  setup_sink_by_name(node, "a").connect_driver(cur);
  setup_sink_by_name(node, "mask").connect_driver(create_const(*current_graph, *Dlop::get_mask_value(hi - 1, lo)));

  // The bypassed pack/wire-buffer chain is usually dead now; bwd_del_node also
  // sweeps the inputs that become dead behind it.
  if (!old_master.is_invalid() && !livehd::graph_util::is_builtin_node(old_master)
      && !Ntype::is_loop_last(type_op_of(old_master)) && !old_master.has_out_edges()) {
    bwd_del_node(old_master);
  }
  return false;  // rewired in place, not deleted
}

bool Cprop::scalar_get_mask(hhds::Node_class& node) {
  auto a_pin    = livehd::graph_util::get_driver_of_sink_name(node, "a");
  auto mask_pin = livehd::graph_util::get_driver_of_sink_name(node, "mask");
  if (a_pin.is_invalid() || mask_pin.is_invalid() || !node.has_out_edges()) {
    node.del_node();
    return true;
  }
  if (!is_const_pin(mask_pin)) {
    return false;
  }

  auto mask_const = hydrate_const(mask_pin);

  // Rule 4: get_mask(a, -1) == a — only when `a` is provably non-negative.
  // get_mask always yields a non-negative value (it zero-extends the selected
  // bits), so it is the to-positive wrapper for signed-read pins (e.g. module
  // ports, which cgen declares `signed`). Bypassing it around a pin that can
  // go negative changes the value: u3 a=0b101 must read 5, not -3 (caught by
  // LEC once the lgcheck BMC stage became sound).
  if (mask_const.is_just_i64() && mask_const.to_just_i64() == -1) {
    bool nonneg = false;
    if (is_const_pin(a_pin)) {
      auto v = hydrate_const(a_pin);
      // is_positive() is exact at any width (an is_just_i64 gate would treat
      // a >62-bit non-negative constant as "maybe negative"); an unknown sign
      // bit reads negative, which stays conservative for Rule 4.
      nonneg = v.is_positive();
    } else if (is_graph_input_pin(a_pin)) {
      // A module port always reads SIGNED in the LGraph/cgen model; its
      // unsign attr (when present) is source-interface metadata, not a
      // value-range guarantee. Never bypass the to-positive wrapper here.
      nonneg = false;
    } else {
      auto a_master = a_pin.get_master_node();
      nonneg = (!a_master.is_invalid() && type_op_of(a_master) == Ntype_op::Get_mask) || livehd::graph_util::is_unsign(a_pin);
    }
    if (!nonneg) {
      return false;
    }
    return collapse_forward_for_pin(node, a_pin);
  }

  auto a_master = a_pin.get_master_node();
  if (type_op_of(a_master) != Ntype_op::Set_mask) {
    // `a` is not a Set_mask writer, so the Set_mask path below cannot fire and
    // the packed-wire fold (Or / SHL / Get_mask operands) is the complement.
    // Running it here keeps `a_pin`/`mask_const` fresh: the fold may rewire this
    // node's `a`+`mask` in place, which would invalidate both.
    return scalar_get_mask_packed(node, mask_const);
  }

  auto [range_begin, range_end] = mask_const.get_mask_range();

  if ((range_begin + 1) != range_end) {
    return false;
  }

  auto dpin = try_find_single_driver_pin(a_master, range_begin);
  if (!dpin.is_invalid()) {
    return collapse_forward_for_pin(node, dpin);
  }

  return false;
}

void Cprop::scalar_pass(hhds::Graph* g) {
  std::vector<hhds::Node_class> snapshot;
  for (auto node : g->forward_class()) {
    snapshot.push_back(node);
  }

  for (auto& node : snapshot) {
    if (node.is_invalid()) {
      continue;
    }
    auto op = type_op_of(node);
    // Everything above Hotmux (38) is IO/state/Sub/const/attr — not
    // copy-propagatable. Hotmux sits between Mux (36) and IO (39) and IS
    // handled (const one-hot selector fold, same-arm collapse).
    if (op > Ntype_op::Hotmux) {
      continue;
    }

    auto inp_edges_ordered = ordered_inp_edges(node);

    if (op == Ntype_op::Sext) {
      if (inp_edges_ordered.size() >= 2) {
        scalar_sext(node, inp_edges_ordered);
      }
    } else if (op == Ntype_op::Mux) {
      bool del = scalar_mux(node, inp_edges_ordered);
      if (del) {
        continue;
      }
    } else if (op == Ntype_op::Get_mask) {
      bool del = scalar_get_mask(node);
      if (del || node.is_invalid()) {
        continue;
      }
      // The packed-slice fold can rewire `a`/`mask` in place and return false,
      // which would leave the vector captured above stale.
      inp_edges_ordered = ordered_inp_edges(node);
    } else if (!node.has_out_edges()) {
      bwd_del_node(node);
      continue;
    }

    auto replaced_some = try_constant_prop(node, inp_edges_ordered);

    if (node.is_invalid()) {
      continue;
    }

    if (replaced_some) {
      inp_edges_ordered = ordered_inp_edges(node);
    }

    try_collapse_forward(node, inp_edges_ordered);
  }
}

void Cprop::do_trans(const std::shared_ptr<hhds::Graph>& g) {
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
  // Invariant (-c dbg): every value-producing cell must carry a resolved width
  // at cprop ENTRY (i.e. as produced by tolg / upass generation). cprop is the
  // only graph pass in the default O1 recipe, so checking here covers BOTH
  // front-ends' tolg output (upass/tolg and inou/yosys/lgyosys_tolg). The lnast
  // tolg additionally self-checks at its own output (covers O0, where no graph
  // pass runs) -- see uPass_tolg::run.
  livehd::graph_util::debug_assert_cells_sized(*g, "tolg/upass (seen at cprop entry)");
#endif

  current_graph = g.get();
  // Dissolve word-level-false comb cycles FIRST (packed-wire concat
  // accumulators read back via const Get_mask slices -- the Chisel arbiter
  // grant-chain shape): each slice-read is redirected to the operand that
  // drives it, so the sweep below AND every downstream consumer (lec encode,
  // cgen, sim scheduling) sees the acyclic bit-level DAG. No-op on acyclic
  // graphs. Lifted from cgen_sim 2026-07-10 (which still calls it for O0
  // graphs that skip cprop); was sim-only, leaving lec unable to encode 442
  // XSCore defs.
  livehd::graph_util::split_packed_selfref_wires(current_graph);
  scalar_pass(current_graph);
  current_graph = nullptr;

#ifndef NDEBUG
  // Debug self-check (Tier 1, -c dbg): every constant left in the graph must be
  // consistent with the bits/sign attributes on its pin. cprop is the only
  // graph pass that runs in the default O1 recipe, so this is the front line for
  // catching front-end translation misses (a const stamped the wrong width/sign).
  for (auto node : g->forward_class()) {
    if (type_op_of(node) != Ntype_op::Nconst) {
      continue;
    }
    livehd::graph_util::debug_check_const_pin(node.create_driver_pin(0));
  }
#endif
}

void Cprop::bwd_del_node(hhds::Node_class& node) {
  // Aggressive del: also remove single-user inputs that become dead.
  // WARNING: only call when all needed downstream edges have been added.

  I(!Ntype::is_loop_last(type_op_of(node)));

  absl::flat_hash_set<hhds::Class_index> potential_set;
  std::deque<hhds::Node_class>           potential;

  for (const auto& e : node.inp_edges()) {
    if (is_graph_input_pin(e.driver) || is_graph_output_pin(e.driver)) {
      continue;
    }
    auto master = e.driver.get_master_node();
    // CONST_NODE (and other singletons) cannot be deleted: const pins are
    // leaves of the form CONST_NODE.pid_N, so dropping the consumer just
    // leaves the pin unreferenced — harmless and dedup-friendly.
    if (livehd::graph_util::is_builtin_node(master)) {
      continue;
    }
    if (potential_set.contains(master.get_class_index())) {
      continue;
    }
    potential.emplace_back(master);
    potential_set.insert(master.get_class_index());
  }

  node.del_node();

  while (!potential.empty()) {
    auto n = potential.front();
    potential.pop_front();

    if (n.is_invalid()) {
      continue;
    }

    if (!Ntype::is_loop_last(type_op_of(n)) && !n.has_out_edges()) {
      for (auto e : n.inp_edges()) {
        if (is_graph_input_pin(e.driver) || is_graph_output_pin(e.driver)) {
          continue;
        }
        auto d_master = e.driver.get_master_node();
        if (livehd::graph_util::is_builtin_node(d_master)) {
          continue;
        }
        if (potential_set.contains(d_master.get_class_index())) {
          continue;
        }
        potential.emplace_back(d_master);
        potential_set.insert(d_master.get_class_index());
      }
      n.del_node();
    }
  }
}
