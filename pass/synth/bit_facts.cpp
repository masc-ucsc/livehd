// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "bit_facts.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "affine_amount.hpp"
#include "node_util.hpp"

namespace livehd::synth::bit_facts {
namespace gu = livehd::graph_util;

namespace {
void shape(const hhds::Pin_class& from, const hhds::Pin_class& to) {
  gu::set_bits(to, std::max(gu::bits_of(from), 1));
  gu::is_unsign(from) ? gu::set_unsign(to) : gu::set_sign(to);
}

// Pins one query may inspect. The walk is a tree over a DAG (no memo), and a wide Hotmux or a
// stack of Mux levels would otherwise visit a shared cone once per path.
constexpr int kVisitBudget = 4096;

// The instances the walk is inside of, outermost first: an instance output is read through its definition, and an
// input of that definition is then whatever THIS instance drives it with (the walk is context sensitive, like a flat
// one). Empty: the walk is in the graph it started in, whose own inputs are unknown.
using Ctx = std::vector<hhds::Node_class>;
std::vector<bool> facts(const hhds::Pin_class& pin, int width, int depth, int& budget, Ctx& ctx);
std::vector<bool> facts_core(const hhds::Pin_class& pin, int width, int depth, int& budget, Ctx& ctx);
std::vector<bool> ones(const hhds::Pin_class& pin, int width, int depth, int& budget, Ctx& ctx);

// Every value a shift amount can take, when they are few: a constant, the `index*scale + bias` of a word select over a
// narrow index (affine_amount.hpp), or any value of a narrow unsigned amount. A bit of `a >> amount` is known when the
// bit it would read is known the same for every one of them: every lane of a packed vector carries 0 there.
constexpr int                   kAmountIndexBits = 10;
constexpr int                   kMaxAmount       = 1 << 24;
std::optional<std::vector<int>> amounts_of(const hhds::Pin_class& b, int depth = 0) {
  if (b.is_invalid() || depth > 4) {
    return std::nullopt;
  }
  if (b.is_const()) {
    const auto& c = gu::const_of(b);
    if (c.has_unknowns() || !c.is_just_i64() || c.to_just_i64() < 0 || c.to_just_i64() > kMaxAmount) {
      return std::nullopt;
    }
    return std::vector<int>{static_cast<int>(c.to_just_i64())};
  }
  std::vector<int> out;
  if (const auto chain = affine_chain(b)) {
    const int wi = gu::bits_of(chain->index);
    if (wi <= 0 || wi > kAmountIndexBits || !gu::is_unsign(chain->index)) {
      return std::nullopt;
    }
    for (uint64_t v = 0; v < (uint64_t{1} << wi); ++v) {
      const auto amount = chain->eval(v);
      if (!amount || *amount > static_cast<uint64_t>(kMaxAmount)) {
        return std::nullopt;
      }
      out.push_back(static_cast<int>(*amount));
    }
    return out;
  }
  if (gu::type_op_of(b.get_master_node()) == Ntype_op::Mux && !gu::is_graph_input_pin(b)) {
    for (const auto& sink : b.get_master_node().inp_sorted_pins()) {
      if (sink.get_port_id() == 0) {
        continue;
      }
      const auto arm = amounts_of(sink.get_driver_pin(), depth + 1);
      if (!arm) {
        return std::nullopt;
      }
      out.insert(out.end(), arm->begin(), arm->end());
    }
    std::ranges::sort(out);
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out.size() > 4096 ? std::nullopt : std::optional{out};
  }
  const int wb = gu::bits_of(b);
  if (wb <= 0 || wb > kAmountIndexBits || !gu::is_unsign(b)) {
    return std::nullopt;
  }
  for (int v = 0; v < (1 << wb); ++v) {
    out.push_back(v);
  }
  return out;
}

// `pin` is an input of the definition the walk entered through `ctx.back()`: the pin that instance drives it with,
// for an unsigned port. Invalid when there is none.
hhds::Pin_class instance_driver(const hhds::Pin_class& pin, const Ctx& ctx) {
  if (ctx.empty() || !gu::is_unsign(pin) || gu::bits_of(pin) <= 0) {
    return {};
  }
  const auto sink = ctx.back().get_sink_pin(pin.get_port_id());
  return sink.is_invalid() ? hhds::Pin_class{} : sink.get_driver_pin();
}

// The driver, inside its definition, of the instance output `pin` (an unsigned port), and the port's width.
std::pair<hhds::Pin_class, int> callee_output(const hhds::Pin_class& pin) {
  const auto node = pin.get_master_node();
  if (node.is_loop_subnode()) {
    return {};
  }
  const auto callee = node.get_subnode_graph();
  const auto io     = node.get_subnode_io();
  if (!callee || !io) {
    return {};
  }
  for (const auto& decl : io->get_output_pin_decls()) {
    if (decl.port_id == pin.get_port_id()) {
      if (decl.bits <= 0 || !decl.unsign) {
        return {};
      }
      const auto out = callee->get_output_pin(decl.name);
      return {out.is_invalid() ? hhds::Pin_class{} : out.get_driver_pin(), static_cast<int>(decl.bits)};
    }
  }
  return {};
}
}  // namespace

std::vector<bool> known_zero_bits(const hhds::Pin_class& pin, int width) {
  int budget = kVisitBudget;
  Ctx ctx;
  return facts(pin, width, 0, budget, ctx);
}

Known known_bits(const hhds::Pin_class& pin, int width) {
  Known k;
  int   budget = kVisitBudget;
  Ctx   ctx;
  k.zero = facts(pin, width, 0, budget, ctx);
  budget = kVisitBudget;
  ctx.clear();
  k.one = ones(pin, width, 0, budget, ctx);
  for (size_t i = 0; i < k.one.size(); ++i) {
    if (k.zero[i]) {
      k.one[i] = false;  // never both: an unreachable value would be the only way
    }
  }
  return k;
}

namespace {
std::vector<bool> facts(const hhds::Pin_class& pin, int width, int depth, int& budget, Ctx& ctx) {
  auto zero = facts_core(pin, width, depth, budget, ctx);
  // An unsigned value of declared width b is zero above bit b.
  if (!pin.is_invalid() && !pin.is_const()) {
    const int b = gu::bits_of(pin);
    if (b > 0 && gu::is_unsign(pin)) {
      for (int i = b; i < width; ++i) {
        zero[static_cast<size_t>(i)] = true;
      }
    }
  }
  return zero;
}

std::vector<bool> facts_core(const hhds::Pin_class& pin, int width, int depth, int& budget, Ctx& ctx) {
  std::vector<bool> zero(static_cast<size_t>(std::max(width, 0)), false);
  if (pin.is_invalid() || width <= 0 || depth > 8 || budget <= 0) {
    return zero;
  }
  --budget;
  if (pin.is_const()) {
    const auto& c = gu::const_of(pin);
    if (c.has_unknowns() || !c.is_integer()) {
      return zero;
    }
    for (int i = 0; i < width; ++i) {
      zero[static_cast<size_t>(i)] = !c.bit_test(i);
    }
    return zero;
  }
  if (gu::is_graph_input_pin(pin)) {
    // Inside an instance: what that instance drives the port with, read in the parent (the wrapper adds the zeros
    // above the port's own width).
    const auto drv = instance_driver(pin, ctx);
    if (drv.is_invalid()) {
      return zero;
    }
    const auto inst = ctx.back();
    ctx.pop_back();
    const auto zp = facts(drv, std::min(width, gu::bits_of(pin)), depth + 1, budget, ctx);
    ctx.push_back(inst);
    for (size_t i = 0; i < zp.size(); ++i) {
      zero[i] = zp[i];
    }
    return zero;
  }
  const auto node          = pin.get_master_node();
  const auto op            = gu::type_op_of(node);
  const auto all_of_inputs = [&](bool and_mode, const std::vector<hhds::Pin_class>& inputs) {
    // and_mode: zero if ANY input is zero at the bit; else zero if ALL are.
    std::vector<bool> acc(zero.size(), and_mode ? false : true);
    for (const auto& in : inputs) {
      const auto z = facts(in, width, depth + 1, budget, ctx);
      for (size_t i = 0; i < acc.size(); ++i) {
        acc[i] = and_mode ? (acc[i] || z[i]) : (acc[i] && z[i]);
      }
    }
    if (inputs.empty()) {
      std::fill(acc.begin(), acc.end(), false);
    }
    return acc;
  };
  if (op == Ntype_op::And || op == Ntype_op::Or) {
    std::vector<hhds::Pin_class> in;
    for (const auto& sink : node.inp_sorted_pins()) {
      for (const auto& d : sink.get_driver_pins()) {
        in.push_back(d);
      }
    }
    return all_of_inputs(op == Ntype_op::And, in);
  }
  if (op == Ntype_op::Mult) {
    // cprop spells `cond ? K : 0` as `cond * K`: a one-bit unsigned operand
    // times a non-negative constant is zero wherever the constant is.
    std::vector<hhds::Pin_class> in;
    for (const auto& sink : node.inp_sorted_pins()) {
      for (const auto& d : sink.get_driver_pins()) {
        in.push_back(d);
      }
    }
    if (in.size() == 2) {
      for (int k = 0; k < 2; ++k) {
        const auto& c = in[static_cast<size_t>(k)];
        const auto& v = in[static_cast<size_t>(1 - k)];
        if (c.is_const() && !gu::const_of(c).has_unknowns() && gu::const_of(c).is_integer() && !gu::const_of(c).is_negative()
            && !v.is_const() && gu::bits_of(v) == 1 && gu::is_unsign(v)) {
          for (int i = 0; i < width; ++i) {
            zero[static_cast<size_t>(i)] = !gu::const_of(c).bit_test(i);
          }
          return zero;
        }
      }
    }
    // A product is a multiple of every constant factor, so its low bits below the
    // factor's trailing zeros are 0 (a word select `idx * 32` has five).
    int low_zero = 0;
    for (const auto& d : in) {
      if (d.is_const() && !gu::const_of(d).has_unknowns() && gu::const_of(d).is_integer() && !gu::const_of(d).is_negative()
          && !gu::const_of(d).is_known_zero()) {
        low_zero += gu::const_of(d).get_trailing_zeroes();
      }
    }
    for (int i = 0; i < std::min(low_zero, width); ++i) {
      zero[static_cast<size_t>(i)] = true;
    }
    return zero;
  }
  if (op == Ntype_op::Sum) {
    // A sum has the low zero bits every operand has (a carry only comes from below): the
    // `idx * 32 + 32` of a word select keeps its five low zeros.
    int low_zero = width;
    for (const auto& sink : node.inp_sorted_pins()) {
      for (const auto& d : sink.get_driver_pins()) {
        const auto z = facts(d, width, depth + 1, budget, ctx);
        int        n = 0;
        while (n < width && z[static_cast<size_t>(n)]) {
          ++n;
        }
        low_zero = std::min(low_zero, n);
      }
    }
    for (int i = 0; i < low_zero; ++i) {
      zero[static_cast<size_t>(i)] = true;
    }
    return zero;
  }
  if (op == Ntype_op::Mux) {  // pid 0 is the select; the arms are the rest
    std::vector<hhds::Pin_class> arms;
    for (const auto& sink : node.inp_sorted_pins()) {
      if (sink.get_port_id() != 0) {
        arms.push_back(sink.get_driver_pin());
      }
    }
    return all_of_inputs(false, arms);
  }
  if (op == Ntype_op::Hotmux) {  // interleaved (control, value) pairs, optional trailing default
    std::vector<hhds::Pin_class> pins;
    for (const auto& sink : node.inp_sorted_pins()) {
      pins.push_back(sink.get_driver_pin());
    }
    std::vector<hhds::Pin_class> vals;
    for (size_t idx = 0; idx < pins.size(); ++idx) {
      if ((idx % 2 == 1) || (pins.size() % 2 == 1 && idx + 1 == pins.size())) {
        vals.push_back(pins[idx]);
      }
    }
    // no control active and no default reads 0, so a missing default is one more zero arm
    return all_of_inputs(false, vals);
  }
  if (op == Ntype_op::Concat) {
    // sinks: (value, width) pairs, MSB lane first; lane i is p(2i), p(2i+1)
    std::vector<std::pair<hhds::Pin_class, int>> lanes;  // MSB first
    hhds::Pin_class                              pending;
    for (const auto& sink : node.inp_sorted_pins()) {
      if (sink.get_port_id() % 2 == 0) {
        pending = sink.get_driver_pin();
      } else {
        const auto w = sink.get_driver_pin();
        if (!w.is_const() || !gu::const_of(w).is_just_i64() || gu::const_of(w).to_just_i64() < 0
            || gu::const_of(w).to_just_i64() > (int64_t{1} << 24)) {
          return zero;
        }
        lanes.emplace_back(pending, static_cast<int>(gu::const_of(w).to_just_i64()));
      }
    }
    int offset = 0;
    for (auto it = lanes.rbegin(); it != lanes.rend(); ++it) {
      const auto z = facts(it->first, it->second, depth + 1, budget, ctx);
      for (int i = 0; i < it->second && offset + i < width; ++i) {
        zero[static_cast<size_t>(offset + i)] = z[static_cast<size_t>(i)];
      }
      offset += it->second;
    }
    for (int i = offset; i < width; ++i) {
      zero[static_cast<size_t>(i)] = true;  // a Concat is non-negative: zero above its lanes
    }
    return zero;
  }
  if (op == Ntype_op::SHL || op == Ntype_op::SRA) {
    // A shift by one of a few amounts: a result bit is zero when the bit it reads is zero for every amount (a left
    // shift reads below itself, and nothing below bit 0; a right shift of an unsigned value reads above itself).
    const auto a = gu::get_driver_of_sink_name(node, "a");
    const auto b = gu::get_driver_of_sink_name(node, "b");
    if (a.is_invalid() || (op == Ntype_op::SRA && !a.is_const() && !gu::is_unsign(a))) {
      return zero;
    }
    const auto amounts = amounts_of(b);
    if (!amounts || amounts->empty() || static_cast<int64_t>(amounts->size()) * width > (int64_t{1} << 22)) {
      return zero;
    }
    const int  most = *std::ranges::max_element(*amounts);
    const int  span = op == Ntype_op::SHL ? width : width + most;
    const auto za   = facts(a, span, depth + 1, budget, ctx);
    for (int i = 0; i < width; ++i) {
      bool z = true;
      for (const int sh : *amounts) {
        const int from = op == Ntype_op::SHL ? i - sh : i + sh;
        if (from >= 0 && !za[static_cast<size_t>(from)]) {
          z = false;
          break;
        }
      }
      zero[static_cast<size_t>(i)] = z;
    }
    return zero;
  }
  if (op == Ntype_op::Sub) {
    // An instance output is whatever its definition drives it with; the definition's own inputs are unknown there.
    const auto [drv, bits] = callee_output(pin);
    if (!drv.is_invalid() && ctx.size() < 8) {
      ctx.push_back(node);
      const auto zc = facts(drv, std::min(width, bits), depth + 1, budget, ctx);
      ctx.pop_back();
      for (size_t i = 0; i < zc.size(); ++i) {
        zero[i] = zc[i];
      }
    }
    return zero;
  }
  if (op == Ntype_op::Set_mask) {
    // a with the window [lo, hi) replaced by the low hi-lo bits of value: the window takes the value's facts, every other bit
    // the base's (the e-graph engine's spelling of a partial write; the classic engine's is a Concat of Get_mask lanes)
    const auto a     = gu::get_driver_of_sink_name(node, "a");
    const auto value = gu::get_driver_of_sink_name(node, "value");
    const auto range = gu::bit_range(node);
    if (!a.is_invalid() && !value.is_invalid() && range && range->second <= (1 << 24)) {
      const auto [l, h] = *range;
      const auto za     = facts(a, width, depth + 1, budget, ctx);
      const auto zv     = facts(value, h - l, depth + 1, budget, ctx);
      for (int i = 0; i < width; ++i) {
        zero[static_cast<size_t>(i)] = (i >= l && i < h) ? zv[static_cast<size_t>(i - l)] : za[static_cast<size_t>(i)];
      }
    }
    return zero;
  }
  if (op == Ntype_op::Get_mask) {
    // the half-open window [lo, hi) of a, as a non-negative value
    const auto a     = gu::get_driver_of_sink_name(node, "a");
    const auto range = gu::bit_range(node);
    if (!a.is_invalid() && range && range->second <= (1 << 24)) {
      const auto [l, h] = *range;
      const auto za     = facts(a, h, depth + 1, budget, ctx);
      for (int i = 0; i < width; ++i) {
        zero[static_cast<size_t>(i)] = l + i >= h || za[static_cast<size_t>(l + i)];
      }
    }
    return zero;
  }
  return zero;
}
}  // namespace

namespace {
// The known-one twin of facts(): which bits of `pin` are provably 1. Same shapes, the dual rules (an Or has the ones
// of any operand, an And only the ones every operand has); arithmetic says nothing.
std::vector<bool> ones(const hhds::Pin_class& pin, int width, int depth, int& budget, Ctx& ctx) {
  std::vector<bool> one(static_cast<size_t>(std::max(width, 0)), false);
  if (pin.is_invalid() || width <= 0 || depth > 8 || budget <= 0) {
    return one;
  }
  --budget;
  if (pin.is_const()) {
    const auto& c = gu::const_of(pin);
    if (c.has_unknowns() || !c.is_integer() || c.is_negative()) {
      return one;
    }
    for (int i = 0; i < width; ++i) {
      one[static_cast<size_t>(i)] = c.bit_test(i);
    }
    return one;
  }
  // Only bits inside the declared width of an unsigned value: above it the value reads 0.
  const int declared = gu::bits_of(pin);
  if (declared <= 0 || !gu::is_unsign(pin)) {
    return one;
  }
  const int w = std::min(width, declared);
  if (gu::is_graph_input_pin(pin)) {
    const auto drv = instance_driver(pin, ctx);
    if (!drv.is_invalid()) {
      const auto inst = ctx.back();
      ctx.pop_back();
      const auto op = ones(drv, w, depth + 1, budget, ctx);
      ctx.push_back(inst);
      for (int i = 0; i < w; ++i) {
        one[static_cast<size_t>(i)] = op[static_cast<size_t>(i)];
      }
    }
    return one;
  }
  const auto node = pin.get_master_node();
  const auto op   = gu::type_op_of(node);
  const auto fill = [&](const std::vector<bool>& inner) {
    for (int i = 0; i < w && static_cast<size_t>(i) < inner.size(); ++i) {
      one[static_cast<size_t>(i)] = inner[static_cast<size_t>(i)];
    }
  };
  const auto join = [&](bool any, const std::vector<hhds::Pin_class>& inputs) {
    std::vector<bool> acc(static_cast<size_t>(w), !any);
    for (const auto& in : inputs) {
      const auto o = ones(in, w, depth + 1, budget, ctx);
      for (size_t i = 0; i < acc.size(); ++i) {
        acc[i] = any ? (acc[i] || o[i]) : (acc[i] && o[i]);
      }
    }
    if (inputs.empty()) {
      std::fill(acc.begin(), acc.end(), false);
    }
    fill(acc);
  };
  if (op == Ntype_op::And || op == Ntype_op::Or) {
    std::vector<hhds::Pin_class> in;
    for (const auto& sink : node.inp_sorted_pins()) {
      for (const auto& d : sink.get_driver_pins()) {
        in.push_back(d);
      }
    }
    join(op == Ntype_op::Or, in);
    return one;
  }
  if (op == Ntype_op::Mux) {
    std::vector<hhds::Pin_class> arms;
    for (const auto& sink : node.inp_sorted_pins()) {
      if (sink.get_port_id() != 0) {
        arms.push_back(sink.get_driver_pin());
      }
    }
    join(false, arms);
    return one;
  }
  if (op == Ntype_op::Concat) {
    std::vector<std::pair<hhds::Pin_class, int>> lanes;  // MSB first
    hhds::Pin_class                              pending;
    for (const auto& sink : node.inp_sorted_pins()) {
      if (sink.get_port_id() % 2 == 0) {
        pending = sink.get_driver_pin();
      } else {
        const auto lw = sink.get_driver_pin();
        if (!lw.is_const() || !gu::const_of(lw).is_just_i64() || gu::const_of(lw).to_just_i64() < 0
            || gu::const_of(lw).to_just_i64() > (int64_t{1} << 24)) {
          return one;
        }
        lanes.emplace_back(pending, static_cast<int>(gu::const_of(lw).to_just_i64()));
      }
    }
    int offset = 0;
    for (auto it = lanes.rbegin(); it != lanes.rend() && offset < w; ++it) {
      const auto o = ones(it->first, it->second, depth + 1, budget, ctx);
      for (int i = 0; i < it->second && offset + i < w; ++i) {
        one[static_cast<size_t>(offset + i)] = o[static_cast<size_t>(i)];
      }
      offset += it->second;
    }
    return one;
  }
  if (op == Ntype_op::SHL || op == Ntype_op::SRA) {
    const auto a = gu::get_driver_of_sink_name(node, "a");
    const auto b = gu::get_driver_of_sink_name(node, "b");
    if (a.is_invalid() || (op == Ntype_op::SRA && !a.is_const() && !gu::is_unsign(a))) {
      return one;
    }
    const auto amounts = amounts_of(b);
    if (!amounts || amounts->empty() || static_cast<int64_t>(amounts->size()) * w > (int64_t{1} << 22)) {
      return one;
    }
    const int  most = *std::ranges::max_element(*amounts);
    const int  span = op == Ntype_op::SHL ? w : w + most;
    const auto oa   = ones(a, span, depth + 1, budget, ctx);
    for (int i = 0; i < w; ++i) {
      bool o = true;
      for (const int sh : *amounts) {
        const int from = op == Ntype_op::SHL ? i - sh : i + sh;
        if (from < 0 || !oa[static_cast<size_t>(from)]) {
          o = false;
          break;
        }
      }
      one[static_cast<size_t>(i)] = o;
    }
    return one;
  }
  if (op == Ntype_op::Sub) {
    const auto [drv, bits] = callee_output(pin);
    if (!drv.is_invalid() && ctx.size() < 8) {
      ctx.push_back(node);
      fill(ones(drv, std::min(w, bits), depth + 1, budget, ctx));
      ctx.pop_back();
    }
    return one;
  }
  if (op == Ntype_op::Hotmux) {  // interleaved (control, value) pairs; only with a trailing default is some arm always read
    std::vector<hhds::Pin_class> pins;
    for (const auto& sink : node.inp_sorted_pins()) {
      pins.push_back(sink.get_driver_pin());
    }
    if (pins.size() % 2 == 1) {
      std::vector<hhds::Pin_class> vals;
      for (size_t idx = 0; idx < pins.size(); ++idx) {
        if ((idx % 2 == 1) || idx + 1 == pins.size()) {
          vals.push_back(pins[idx]);
        }
      }
      join(false, vals);
    }
    return one;
  }
  if (op == Ntype_op::Set_mask) {
    const auto a     = gu::get_driver_of_sink_name(node, "a");
    const auto value = gu::get_driver_of_sink_name(node, "value");
    const auto range = gu::bit_range(node);
    if (!a.is_invalid() && !value.is_invalid() && range && range->second <= (1 << 24)) {
      const auto [l, h] = *range;
      const auto oa     = ones(a, w, depth + 1, budget, ctx);
      const auto ov     = ones(value, h - l, depth + 1, budget, ctx);
      for (int i = 0; i < w; ++i) {
        one[static_cast<size_t>(i)] = (i >= l && i < h) ? ov[static_cast<size_t>(i - l)] : oa[static_cast<size_t>(i)];
      }
    }
    return one;
  }
  if (op == Ntype_op::Get_mask) {
    const auto a     = gu::get_driver_of_sink_name(node, "a");
    const auto range = gu::bit_range(node);
    if (!a.is_invalid() && range && range->second <= (1 << 24)) {
      const auto [l, h] = *range;
      const auto oa     = ones(a, h, depth + 1, budget, ctx);
      for (int i = 0; i < w && l + i < h; ++i) {
        one[static_cast<size_t>(i)] = oa[static_cast<size_t>(l + i)];
      }
    }
    return one;
  }
  return one;
}
}  // namespace

hhds::Pin_class known_pin(hhds::Graph& g, const hhds::Pin_class& driver, const Known& known, int color) {
  const auto width = known.zero.size();
  bool       all = true, any_one = false;
  for (size_t i = 0; i < width; ++i) {
    all     = all && (known.zero[i] || known.one[i]);
    any_one = any_one || known.one[i];
  }
  std::string one_bits(width, '0');  // MSB first
  for (size_t i = 0; i < width; ++i) {
    if (known.one[i]) {
      one_bits[width - 1 - i] = '1';
    }
  }
  if (all) {
    return gu::create_const(g, *Dlop::from_binary(one_bits, true));
  }
  std::vector<bool> keep(width);
  for (size_t i = 0; i < width; ++i) {
    keep[i] = !known.zero[i] && !known.one[i];
  }
  auto value = mask_pin(g, driver, keep, color);
  if (!any_one) {
    return value;
  }
  auto node = gu::create_typed_node(g, Ntype_op::Or);
  if (color >= 0) {
    gu::set_color(node, color);
  }
  value.connect_sink(gu::setup_sink_by_name(node, "as"));
  gu::create_const(g, *Dlop::from_binary(one_bits, true)).connect_sink(gu::setup_sink_by_name(node, "as"));
  auto joined = node.create_driver_pin(0);
  shape(driver, joined);
  return joined;
}

hhds::Pin_class mask_pin(hhds::Graph& g, const hhds::Pin_class& driver, const std::vector<bool>& keep, int color) {
  std::string bits(keep.size(), '0');  // MSB first
  for (size_t i = 0; i < keep.size(); ++i) {
    if (keep[i]) {
      bits[keep.size() - 1 - i] = '1';
    }
  }
  auto node = gu::create_typed_node(g, Ntype_op::And);
  if (color >= 0) {
    gu::set_color(node, color);
  }
  driver.connect_sink(gu::setup_sink_by_name(node, "as"));
  gu::create_const(g, *Dlop::from_binary(bits, true)).connect_sink(gu::setup_sink_by_name(node, "as"));
  auto masked = node.create_driver_pin(0);
  shape(driver, masked);
  return masked;
}

std::string mask_text(const std::vector<bool>& keep) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string           out;
  for (size_t base = 0; base < keep.size(); base += 4) {
    int v = 0;
    for (size_t j = 0; j < 4 && base + j < keep.size(); ++j) {
      v |= (keep[base + j] ? 1 : 0) << j;
    }
    out.push_back(digits[v]);
  }
  return out;
}

}  // namespace livehd::synth::bit_facts
