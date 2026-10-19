// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// pass/sweep state sweep: constant and unobserved flop bits of one flat graph.
//
// A flop bit is CONSTANT when its next state can only be its own value or its
// reset value (a hold, a load of the reset value, a never-written register):
// it then reads that value for every cycle after the first reset, and a flop
// with no reset and no initial value may be said to have started there. That is
// exactly Yosys opt_dff's constant-D / D=Q rule, applied per bit. A flop bit is
// UNOBSERVED when no output, instance, memory or other observed state can see
// it. Both kinds are cut out of the flop: the flop keeps only the contiguous
// runs of live bits and the reader of the old Q sees the constants (or 0 for an
// unobserved bit) spliced back in.
//
// The analysis is deliberately local and structural -- it reads the shapes cprop
// leaves behind (Mux, Hotmux, Get_mask, Concat, And/Or with constants) -- and
// every unknown shape is "anything", so it can only miss a fold, never invent
// one. The compile driver iterates it with cprop + bitwidth until it stops
// finding work.
#include <algorithm>
#include <cstdint>
#include <deque>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "affine_amount.hpp"
#include "attr_carry.hpp"
#include "bus_name.hpp"
#include "node_util.hpp"
#include "sweep.hpp"

namespace livehd::sweep {
namespace gu = livehd::graph_util;

namespace {

// ---------------------------------------------------------------- bit values
// The set of values a bit can take once the hypothesised constant state bits
// hold their constants: any of {0, 1}, or `unk` (anything).
struct Val {
  bool can0 = false;
  bool can1 = false;
  bool unk  = false;

  static Val zero() { return Val{.can0 = true}; }
  static Val one() { return Val{.can1 = true}; }
  static Val anything() { return Val{.unk = true}; }
  [[nodiscard]] bool known() const { return !unk && (can0 != can1); }
  [[nodiscard]] bool is_zero() const { return known() && can0; }
  [[nodiscard]] bool is_one() const { return known() && can1; }
};

Val join(const Val& a, const Val& b) { return Val{.can0 = a.can0 || b.can0, .can1 = a.can1 || b.can1, .unk = a.unk || b.unk}; }

Val tri_not(const Val& a) { return a.known() ? (a.is_zero() ? Val::one() : Val::zero()) : Val::anything(); }
Val tri_and(const Val& a, const Val& b) {
  if (a.is_zero() || b.is_zero()) {
    return Val::zero();
  }
  return a.known() && b.known() ? Val::one() : Val::anything();
}
Val tri_or(const Val& a, const Val& b) {
  if (a.is_one() || b.is_one()) {
    return Val::one();
  }
  return a.known() && b.known() ? Val::zero() : Val::anything();
}
Val tri_xor(const Val& a, const Val& b) {
  return a.known() && b.known() ? (a.is_one() != b.is_one() ? Val::one() : Val::zero()) : Val::anything();
}

using Vec = std::vector<Val>;

// One flop under analysis.
struct Flop_info {
  hhds::Node_class node;
  int32_t          width    = 0;
  bool             is_signed = false;
  bool             has_reset = false;
  int32_t          group     = 0;  // flops sharing a reset net are evaluated together (0: no reset)
  std::vector<int8_t> reset;  // per bit: 0, 1, or -1 unknown / not constant
  std::vector<int8_t> klass;  // per bit: 0/1 = hypothesised (then proven) constant, -1 live
};

class Evaluator {
public:
  Evaluator(const absl::flat_hash_map<hhds::Class_index, int32_t>& flop_of, const std::vector<Flop_info>& flops, size_t node_budget,
            const absl::flat_hash_map<hhds::Class_index, Val>& assumed = {})
      : flop_of_(flop_of), flops_(flops), budget_(node_budget), assumed_(assumed) {}

  // Value of bit `i` of the signal `pin` drives (extension past its width).
  Val bit(const hhds::Pin_class& pin, int i, int depth = 0) {
    if (pin.is_invalid()) {
      return Val::anything();
    }
    if (!assumed_.empty() && i == 0) {
      // a net assumed to hold a value (the inactive level of the reset these flops share)
      if (const auto it = assumed_.find(pin.get_class_index()); it != assumed_.end()) {
        return it->second;
      }
    }
    if (pin.is_const()) {
      const auto& c = gu::const_of(pin);
      if (c.has_unknowns() && c.unknown_bit_test(i)) {
        return Val::anything();
      }
      if (!c.is_integer() && !c.is_bool()) {
        return Val::anything();
      }
      return c.bit_test(i) ? Val::one() : Val::zero();
    }
    // A graph input, or a pin whose width nobody stamped, can hold anything.
    if (gu::is_graph_input_pin(pin) || gu::bits_of(pin) <= 0) {
      return Val::anything();
    }
    const auto& v = vec(pin, depth);
    const int   w = static_cast<int>(v.size());
    if (i < w) {
      return v[static_cast<size_t>(i)];
    }
    // past the declared width: zero for unsigned, the sign bit for signed
    if (gu::is_unsign(pin)) {
      return Val::zero();
    }
    return v.back();
  }

private:
  const Vec& vec(const hhds::Pin_class& pin, int depth) {
    const auto key = pin.get_class_index();
    if (auto it = memo_.find(key); it != memo_.end()) {
      return it->second;
    }
    // Insert a placeholder first: a combinational loop (there should be none
    // after pass.legalize) reads "anything" instead of recursing forever.
    const int w = std::max(gu::bits_of(pin), 0);
    auto&     slot = memo_[key];
    slot.assign(static_cast<size_t>(w), Val::anything());
    if (depth > 160 || w == 0 || budget_ == 0) {
      return slot;
    }
    --budget_;
    Vec out = compute(pin, w, depth + 1);
    auto& again = memo_[key];
    again = std::move(out);
    return again;
  }

  Vec all_unknown(int w) const { return Vec(static_cast<size_t>(w), Val::anything()); }

  Vec compute(const hhds::Pin_class& pin, int w, int depth) {
    if (gu::is_graph_input_pin(pin)) {
      return all_unknown(w);
    }
    const auto node = pin.get_master_node();
    const auto op   = gu::type_op_of(node);
    if (op == Ntype_op::Flop) {
      // a candidate flop bit still hypothesised constant reads its constant
      auto it = flop_of_.find(node.get_class_index());
      Vec  v  = all_unknown(w);
      if (it != flop_of_.end() && pin.get_port_id() == 0) {
        const auto& k = flops_[static_cast<size_t>(it->second)].klass;
        for (int i = 0; i < w && i < static_cast<int>(k.size()); ++i) {
          if (k[static_cast<size_t>(i)] >= 0) {
            v[static_cast<size_t>(i)] = k[static_cast<size_t>(i)] == 1 ? Val::one() : Val::zero();
          }
        }
      }
      return v;
    }
    switch (op) {
      case Ntype_op::Mux: return mux(node, w, depth);
      case Ntype_op::Hotmux: return hotmux(node, w, depth);
      case Ntype_op::Get_mask: return get_mask(node, w, depth);
      case Ntype_op::Concat: return concat(node, w, depth);
      case Ntype_op::And:
      case Ntype_op::Or: return andor(node, w, depth, op == Ntype_op::And);
      case Ntype_op::Sum: return sum_op(node, w, depth);
      case Ntype_op::SHL: return shift_op(node, w, depth, true);
      case Ntype_op::SRA: return shift_op(node, w, depth, false);
      case Ntype_op::Not: return not_op(node, w, depth);
      case Ntype_op::Xor: return xor_op(node, w, depth);
      case Ntype_op::Ror: return ror_op(node, w, depth);
      case Ntype_op::EQ: return eq_op(node, w, depth);
      default: return all_unknown(w);
    }
  }

  static std::vector<hhds::Pin_class> drivers_of(const hhds::Pin_class& sink) {
    std::vector<hhds::Pin_class> out;
    for (const auto& d : sink.get_driver_pins()) {
      out.push_back(d);
    }
    return out;
  }

  Vec mux(const hhds::Node_class& node, int w, int depth) {
    std::vector<hhds::Pin_class> arms;
    hhds::Pin_class              sel;
    for (const auto& sink : node.inp_sorted_pins()) {
      auto d = drivers_of(sink);
      if (d.size() != 1) {
        return all_unknown(w);
      }
      if (sink.get_port_id() == 0) {
        sel = d.front();
      } else {
        arms.push_back(d.front());
      }
    }
    if (arms.empty()) {
      return all_unknown(w);
    }
    // A one-bit select over two arms that is known (a constant, or a function of
    // hypothesised-constant state): the live arm only.
    const bool sel_const_bool = !sel.is_invalid() && sel.is_const() && !gu::const_of(sel).has_unknowns() && gu::const_of(sel).is_integer()
                                && (gu::const_of(sel).is_known_zero() || (gu::const_of(sel).is_just_i64() && gu::const_of(sel).to_just_i64() == 1));
    if (arms.size() == 2 && !sel.is_invalid() && (sel_const_bool || (!sel.is_const() && gu::bits_of(sel) == 1))) {
      const auto s0 = bit(sel, 0, depth);
      if (s0.known()) {
        const auto& arm = s0.is_zero() ? arms[0] : arms[1];
        Vec         v(static_cast<size_t>(w));
        for (int i = 0; i < w; ++i) {
          v[static_cast<size_t>(i)] = bit(arm, i, depth);
        }
        return v;
      }
    }
    Vec v(static_cast<size_t>(w));
    for (int i = 0; i < w; ++i) {
      Val acc;
      bool first = true;
      for (const auto& arm : arms) {
        const auto b = bit(arm, i, depth);
        acc          = first ? b : join(acc, b);
        first        = false;
      }
      v[static_cast<size_t>(i)] = acc;
    }
    return v;
  }

  Vec hotmux(const hhds::Node_class& node, int w, int depth) {
    // Interleaved (control, value) pairs and an optional trailing default.
    std::vector<hhds::Pin_class> pins;
    for (const auto& sink : node.inp_sorted_pins()) {
      auto d = drivers_of(sink);
      if (d.size() != 1) {
        return all_unknown(w);
      }
      pins.push_back(d.front());
    }
    const bool has_default = pins.size() % 2 == 1;
    // Controls are mutually exclusive: a control known 0 drops its arm, one known 1
    // selects it (nothing else can be active).
    std::vector<size_t> live;  // indices of value pins that can still be selected
    bool                forced       = false;
    size_t              forced_value = 0;
    for (size_t k = 0; k + 1 < pins.size(); k += 2) {
      const auto c = bit(pins[k], 0, depth);
      if (c.is_zero()) {
        continue;
      }
      if (c.is_one()) {
        forced       = true;
        forced_value = k + 1;
        break;
      }
      live.push_back(k + 1);
    }
    Vec v(static_cast<size_t>(w));
    for (int i = 0; i < w; ++i) {
      if (forced) {
        v[static_cast<size_t>(i)] = bit(pins[forced_value], i, depth);
        continue;
      }
      Val acc = has_default ? bit(pins.back(), i, depth) : Val::zero();  // no control active: default / 0
      for (const size_t k : live) {
        acc = join(acc, bit(pins[k], i, depth));
      }
      v[static_cast<size_t>(i)] = acc;
    }
    return v;
  }

  Vec get_mask(const hhds::Node_class& node, int w, int depth) {
    const auto a  = gu::get_driver_of_sink_name(node, "a");
    const auto lo = gu::get_driver_of_sink_name(node, "lo");
    const auto hi = gu::get_driver_of_sink_name(node, "hi");
    if (a.is_invalid() || lo.is_invalid() || hi.is_invalid() || !lo.is_const() || !hi.is_const() || !gu::const_of(lo).is_just_i64()
        || !gu::const_of(hi).is_just_i64()) {
      return all_unknown(w);
    }
    const auto base  = static_cast<int>(gu::const_of(lo).to_just_i64());
    const auto limit = static_cast<int>(gu::const_of(hi).to_just_i64()) - base;  // the window width
    Vec        v(static_cast<size_t>(w));
    for (int i = 0; i < w; ++i) {
      v[static_cast<size_t>(i)] = i < limit ? bit(a, base + i, depth) : Val::zero();  // a Get_mask is non-negative
    }
    return v;
  }

  Vec concat(const hhds::Node_class& node, int w, int depth) {
    // (value, width) pairs, most significant lane first.
    std::vector<std::pair<hhds::Pin_class, int>> lanes;
    hhds::Pin_class                              pending;
    for (const auto& sink : node.inp_sorted_pins()) {
      auto d = drivers_of(sink);
      if (d.size() != 1) {
        return all_unknown(w);
      }
      if (sink.get_port_id() % 2 == 0) {
        pending = d.front();
      } else {
        if (!d.front().is_const() || !gu::const_of(d.front()).is_just_i64()) {
          return all_unknown(w);
        }
        lanes.emplace_back(pending, static_cast<int>(gu::const_of(d.front()).to_just_i64()));
      }
    }
    Vec v = all_unknown(w);
    int off = 0;
    for (auto it = lanes.rbegin(); it != lanes.rend(); ++it) {
      for (int i = 0; i < it->second && off + i < w; ++i) {
        v[static_cast<size_t>(off + i)] = bit(it->first, i, depth);
      }
      off += it->second;
    }
    for (int i = off; i < w; ++i) {
      v[static_cast<size_t>(i)] = Val::zero();  // a Concat is non-negative
    }
    return v;
  }

  // Added operands (even pids) minus subtracted ones (odd pids), modulo 2^w, with
  // ternary carries: a bit is known as soon as the operand bits and carry that
  // decide it are.
  Vec sum_op(const hhds::Node_class& node, int w, int depth) {
    if (w > 4096) {
      return all_unknown(w);
    }
    Vec acc(static_cast<size_t>(w), Val::zero());
    for (const auto& sink : node.inp_sorted_pins()) {
      const bool subtract = (sink.get_port_id() % 2) == 1;
      for (const auto& d : sink.get_driver_pins()) {
        // acc = acc + (subtract ? ~d + 1 : d)
        Val carry = subtract ? Val::one() : Val::zero();
        for (int i = 0; i < w; ++i) {
          Val x = bit(d, i, depth);
          if (subtract) {
            x = tri_not(x);
          }
          const Val a = acc[static_cast<size_t>(i)];
          const Val axb = tri_xor(a, x);
          acc[static_cast<size_t>(i)] = tri_xor(axb, carry);
          carry                       = tri_or(tri_and(a, x), tri_and(carry, axb));
        }
      }
    }
    return acc;
  }

  // a << k and a >> k (arithmetic). The amount is a constant or an affine function
  // of a narrow index (a word select `v[idx]`); every index value consistent with
  // the bits already known gives one candidate amount, and the result bit joins them.
  Vec shift_op(const hhds::Node_class& node, int w, int depth, bool left) {
    const auto a = gu::get_driver_of_sink_name(node, "a");
    const auto b = gu::get_driver_of_sink_name(node, "b");
    Vec        v = all_unknown(w);
    if (a.is_invalid() || b.is_invalid()) {
      return v;
    }
    std::vector<int64_t> amounts;
    if (b.is_const()) {
      const auto& c = gu::const_of(b);
      if (c.has_unknowns() || !c.is_just_i64() || c.to_just_i64() < 0 || c.to_just_i64() > (1 << 24)) {
        return v;
      }
      amounts.push_back(c.to_just_i64());
    } else if (gu::bits_of(b) > 0 && gu::bits_of(b) <= 12 && gu::is_unsign(b)) {
      // a narrow amount: every value consistent with the bits already known
      const int        bw = gu::bits_of(b);
      std::vector<Val> bits(static_cast<size_t>(bw));
      for (int t = 0; t < bw; ++t) {
        bits[static_cast<size_t>(t)] = bit(b, t, depth);
      }
      for (uint64_t cand = 0; cand < (uint64_t{1} << bw); ++cand) {
        bool ok = true;
        for (int t = 0; t < bw && ok; ++t) {
          const auto& x = bits[static_cast<size_t>(t)];
          if (x.known() && x.is_one() != (((cand >> t) & 1U) != 0)) {
            ok = false;
          }
        }
        if (ok) {
          amounts.push_back(static_cast<int64_t>(cand));
        }
      }
      if (amounts.empty()) {
        return v;
      }
    } else {
      const auto chain = livehd::synth::affine_chain(b);
      if (!chain) {
        return v;
      }
      const int iw = gu::bits_of(chain->index);
      if (iw <= 0 || iw > 12) {
        return v;
      }
      std::vector<Val> idx(static_cast<size_t>(iw));
      for (int t = 0; t < iw; ++t) {
        idx[static_cast<size_t>(t)] = bit(chain->index, t, depth);
      }
      for (uint64_t cand = 0; cand < (uint64_t{1} << iw); ++cand) {
        bool ok = true;
        for (int t = 0; t < iw && ok; ++t) {
          const auto& x = idx[static_cast<size_t>(t)];
          if (x.known() && x.is_one() != (((cand >> t) & 1U) != 0)) {
            ok = false;
          }
        }
        if (!ok) {
          continue;
        }
        const auto amount = chain->eval(cand);
        if (!amount || *amount > (1U << 24)) {
          return v;
        }
        amounts.push_back(static_cast<int64_t>(*amount));
      }
      if (amounts.empty()) {
        return v;
      }
    }
    for (int j = 0; j < w; ++j) {
      Val acc;
      bool first = true;
      for (const auto k : amounts) {
        Val x;
        if (left) {
          x = j < k ? Val::zero() : bit(a, static_cast<int>(j - k), depth);
        } else {
          x = bit(a, static_cast<int>(j + k), depth);
        }
        acc   = first ? x : join(acc, x);
        first = false;
      }
      v[static_cast<size_t>(j)] = acc;
    }
    return v;
  }

  static std::vector<hhds::Pin_class> all_drivers(const hhds::Node_class& node) {
    std::vector<hhds::Pin_class> ops;
    for (const auto& sink : node.inp_sorted_pins()) {
      for (const auto& d : sink.get_driver_pins()) {
        ops.push_back(d);
      }
    }
    return ops;
  }

  Vec not_op(const hhds::Node_class& node, int w, int depth) {
    const auto ops = all_drivers(node);
    Vec        v   = all_unknown(w);
    if (ops.size() != 1) {
      return v;
    }
    for (int i = 0; i < w; ++i) {
      const auto b = bit(ops.front(), i, depth);
      if (b.known()) {
        v[static_cast<size_t>(i)] = b.is_zero() ? Val::one() : Val::zero();
      }
    }
    return v;
  }

  Vec xor_op(const hhds::Node_class& node, int w, int depth) {
    const auto ops = all_drivers(node);
    Vec        v   = all_unknown(w);
    for (int i = 0; i < w; ++i) {
      bool parity = false;
      bool known  = true;
      for (const auto& op : ops) {
        const auto b = bit(op, i, depth);
        if (!b.known()) {
          known = false;
          break;
        }
        parity ^= b.is_one();
      }
      if (known) {
        v[static_cast<size_t>(i)] = parity ? Val::one() : Val::zero();
      }
    }
    return v;
  }

  // Reduce-or of the operands' bits: 1 when a bit is known set, 0 when every bit is known clear.
  Vec ror_op(const hhds::Node_class& node, int w, int depth) {
    const auto ops = all_drivers(node);
    Vec        v   = all_unknown(w);
    if (ops.size() != 1 || w < 1) {
      return v;
    }
    const int iw = gu::bits_of(ops.front());
    if (iw <= 0) {
      return v;
    }
    bool all_zero = true;
    for (int i = 0; i < iw; ++i) {
      const auto b = bit(ops.front(), i, depth);
      if (b.is_one()) {
        v[0] = Val::one();
        for (int k = 1; k < w; ++k) {
          v[static_cast<size_t>(k)] = Val::zero();
        }
        return v;
      }
      all_zero = all_zero && b.is_zero();
    }
    if (all_zero) {
      v[0] = Val::zero();
      for (int k = 1; k < w; ++k) {
        v[static_cast<size_t>(k)] = Val::zero();
      }
    }
    return v;
  }

  // a == b over the wider operand: 0 as soon as one bit pair is known to differ, 1 when every pair is known equal.
  Vec eq_op(const hhds::Node_class& node, int w, int depth) {
    const auto ops = all_drivers(node);
    Vec        v   = all_unknown(w);
    if (ops.size() != 2 || w < 1) {
      return v;
    }
    const int bw0 = gu::bits_of(ops[0]), bw1 = gu::bits_of(ops[1]);
    if (bw0 <= 0 || bw1 <= 0) {
      return v;
    }
    // One bit past the wider operand: a signed operand extends by its sign bit and an
    // unsigned one by 0, so mixed signs (255 vs -1) still differ there.
    const int iw = std::max(bw0, bw1) + 1;
    if (iw > 4097) {
      return v;
    }
    bool all_equal = true;
    for (int i = 0; i < iw; ++i) {
      const auto x = bit(ops[0], i, depth);
      const auto y = bit(ops[1], i, depth);
      if (x.known() && y.known()) {
        if (x.is_one() != y.is_one()) {
          v[0] = Val::zero();
          for (int k = 1; k < w; ++k) {
            v[static_cast<size_t>(k)] = Val::zero();
          }
          return v;
        }
      } else {
        all_equal = false;
      }
    }
    if (all_equal) {
      v[0] = Val::one();
      for (int k = 1; k < w; ++k) {
        v[static_cast<size_t>(k)] = Val::zero();
      }
    }
    return v;
  }

  Vec andor(const hhds::Node_class& node, int w, int depth, bool is_and) {
    std::vector<hhds::Pin_class> ops;
    for (const auto& sink : node.inp_sorted_pins()) {
      for (const auto& d : sink.get_driver_pins()) {
        ops.push_back(d);
      }
    }
    Vec v(static_cast<size_t>(w));
    for (int i = 0; i < w; ++i) {
      // And: a zero operand wins, ones are identities; Or the other way round.
      std::optional<Val> rest;
      bool               forced = false;
      bool               multi  = false;
      for (const auto& op : ops) {
        const auto b = bit(op, i, depth);
        if (is_and ? b.is_zero() : b.is_one()) {
          forced = true;
          break;
        }
        if (is_and ? b.is_one() : b.is_zero()) {
          continue;  // identity
        }
        if (rest) {
          multi = true;
        }
        rest = b;
      }
      if (forced) {
        v[static_cast<size_t>(i)] = is_and ? Val::zero() : Val::one();
      } else if (!rest) {
        v[static_cast<size_t>(i)] = is_and ? Val::one() : Val::zero();
      } else if (multi) {
        v[static_cast<size_t>(i)] = Val::anything();
      } else {
        v[static_cast<size_t>(i)] = *rest;
      }
    }
    return v;
  }

  const absl::flat_hash_map<hhds::Class_index, int32_t>& flop_of_;
  const std::vector<Flop_info>&                          flops_;
  size_t                                                 budget_;
  const absl::flat_hash_map<hhds::Class_index, Val>&     assumed_;
  absl::flat_hash_map<hhds::Class_index, Vec>            memo_;
};

// ---------------------------------------------------------------- flop access
bool single_depth(const hhds::Node_class& flop) {
  for (auto name : {"pipe_min", "pipe_max"}) {
    auto p = gu::get_driver_of_sink_name(flop, name);
    if (!p.is_invalid() && !(p.is_const() && gu::const_of(p).is_just_i64() && gu::const_of(p).to_just_i64() == 1)) {
      return false;
    }
  }
  return true;
}

std::vector<hhds::Edge_class> readers_of(const hhds::Pin_class& pin) {
  std::vector<hhds::Edge_class> out;
  for (const auto& e : pin.out_edges()) {
    out.push_back(e);
  }
  return out;
}

// The constant whose bits lo..hi-1 are `bits` (1 = set, anything else clear).
// `is_signed`: the top bit is a sign bit, as in a signed register's value.
hhds::Pin_class const_bits(hhds::Graph& g, const std::vector<int8_t>& bits, int lo, int hi, bool is_signed = false) {
  std::string text(static_cast<size_t>(hi - lo), '0');  // MSB first
  for (int i = lo; i < hi; ++i) {
    text[static_cast<size_t>(hi - 1 - i)] = bits[static_cast<size_t>(i)] == 1 ? '1' : '0';
  }
  return gu::create_const(g, *Dlop::from_binary(text, !is_signed));
}

struct Lane {
  hhds::Pin_class value;
  int             width = 0;
};

// Concat of lanes given LSB first; the result is unsigned and exactly the sum of the widths.
hhds::Pin_class make_concat(hhds::Graph& g, const std::vector<Lane>& lanes_lsb_first) {
  auto       cat = gu::create_typed_node(g, Ntype_op::Concat);
  const auto n   = static_cast<int>(lanes_lsb_first.size());
  int        total = 0;
  for (int i = n - 1; i >= 0; --i) {
    const auto& lane = lanes_lsb_first[static_cast<size_t>(n - 1 - i)];
    const auto  pid  = static_cast<hhds::Port_id>(2 * i);
    gu::create_const(g, *Dlop::create_integer(lane.width)).connect_sink(cat.create_sink_pin(pid + 1));
    lane.value.connect_sink(cat.create_sink_pin(pid));
    total += lane.width;
  }
  auto out = cat.create_driver_pin(0);
  gu::set_bits(out, total);
  gu::set_unsign(out);
  return out;
}

}  // namespace

struct State_sweeper {
  explicit State_sweeper(hhds::Graph& graph, const State_options& options) : g(graph), opt(options) {}

  hhds::Graph&         g;
  State_options        opt;
  State_stats          stats;
  std::vector<Flop_info> flops;
  absl::flat_hash_map<hhds::Class_index, int32_t> flop_of;
  // The nets that reset flops. While a group's next states are evaluated its reset is
  // inactive: when it is active the flops hold their reset value, which is the claim.
  struct Reset_group {
    hhds::Class_index reset_net;
    Val               inactive;
  };
  std::vector<Reset_group>                        groups;
  absl::flat_hash_map<hhds::Class_index, int32_t> group_of_reset;

  // ------------------------------------------------------------ collection
  void collect() {
    for (auto node : g.body().nodes()) {
      if (gu::type_op_of(node) != Ntype_op::Flop || !single_depth(node)) {
        continue;
      }
      const auto q = node.get_driver_pin(0);
      if (q.is_invalid()) {
        continue;
      }
      Flop_info info;
      info.node      = node;
      info.width     = gu::bits_of(q);
      info.is_signed = !gu::is_unsign(q);
      if (info.width <= 0) {
        continue;
      }
      if (gu::get_driver_of_sink_name(node, "din").is_invalid() || gu::get_driver_of_sink_name(node, "clock_pin").is_invalid()) {
        continue;
      }
      const auto rst     = gu::get_driver_of_sink_name(node, "reset_pin");
      const auto initial = gu::get_driver_of_sink_name(node, "initial");
      info.has_reset     = !rst.is_invalid();
      if (info.has_reset) {
        auto [it, inserted] = group_of_reset.emplace(rst.get_class_index(), static_cast<int32_t>(groups.size() + 1));
        if (inserted) {
          const auto neg     = gu::get_driver_of_sink_name(node, "negreset");
          const bool negated = !neg.is_invalid() && neg.is_const() && gu::const_of(neg).is_known_true();
          groups.push_back({.reset_net = rst.get_class_index(), .inactive = negated ? Val::one() : Val::zero()});
        }
        info.group = it->second;
      }
      info.reset.assign(static_cast<size_t>(info.width), -1);
      if (info.has_reset) {
        // a reset needs a known value to compare the next state against
        if (initial.is_invalid() || !initial.is_const()) {
          continue;
        }
        const auto& c = gu::const_of(initial);
        for (int i = 0; i < info.width; ++i) {
          if (c.has_unknowns() && c.unknown_bit_test(i)) {
            continue;
          }
          info.reset[static_cast<size_t>(i)] = c.bit_test(i) ? 1 : 0;
        }
      } else if (!initial.is_invalid()) {
        continue;  // a power-on value we must respect
      }
      info.klass.assign(static_cast<size_t>(info.width), -1);
      flop_of.emplace(node.get_class_index(), static_cast<int32_t>(flops.size()));
      flops.push_back(std::move(info));
    }
  }

  // ------------------------------------------------------------- constants
  // Which bits of every candidate flop are constant, by induction.
  //
  // Hypothesise that every candidate bit already holds its constant -- the reset
  // value for a flop with a known reset, and a free choice (0 first, then 1) for a
  // flop with neither reset nor initial value -- then evaluate each bit's next
  // state under that hypothesis, the other bits' constants included, so a ring of
  // FIFOs whose valid bits keep each other at 0 is proven at once. A bit whose
  // next state is not provably its constant falls out; repeat until nothing falls
  // out. What remains is an inductive invariant: the state starts there (after
  // reset, or by the free choice) and the next-state function maps it to itself.
  //
  // Returns true when any bit is constant.
  bool classify_constants() {
    for (auto& info : flops) {
      for (int i = 0; i < info.width; ++i) {
        info.klass[static_cast<size_t>(i)] = info.has_reset ? info.reset[static_cast<size_t>(i)] : static_cast<int8_t>(0);
      }
    }
    // The next-state value of bit i of a flop under the current hypothesis.
    const auto next_of = [&](Evaluator& ev, const Flop_info& info, int i) {
      const auto din    = gu::get_driver_of_sink_name(info.node, "din");
      const auto enable = gu::get_driver_of_sink_name(info.node, "enable");
      const Val  hold   = info.klass[static_cast<size_t>(i)] == 1 ? Val::one() : Val::zero();
      Val        load   = ev.bit(din, i);
      if (enable.is_invalid()) {
        return load;
      }
      const auto en = ev.bit(enable, 0);
      if (en.is_one()) {
        return load;
      }
      if (en.is_zero()) {
        return hold;  // never loads
      }
      return join(load, hold);
    };
    const auto fixpoint = [&]() {
      int rounds = 0;
      for (int round = 0; round < 100'000; ++round) {
        bool dropped = false;
        ++rounds;
        for (size_t gi = 0; gi <= groups.size(); ++gi) {
          absl::flat_hash_map<hhds::Class_index, Val> assumed;
          if (gi > 0) {
            assumed.emplace(groups[gi - 1].reset_net, groups[gi - 1].inactive);
          }
          Evaluator ev(flop_of, flops, 400'000'000, assumed);
          for (auto& info : flops) {
            if (info.group != static_cast<int32_t>(gi)) {
              continue;
            }
            for (int i = 0; i < info.width; ++i) {
              const int8_t k = info.klass[static_cast<size_t>(i)];
              if (k < 0) {
                continue;
              }
              const auto next = next_of(ev, info, i);
              if (!(next.known() && next.is_one() == (k == 1))) {
                info.klass[static_cast<size_t>(i)] = -1;
                dropped                            = true;
              }
            }
          }
        }
        if (!dropped) {
          return rounds;
        }
      }
      return rounds;
    };
    stats.fix_rounds += static_cast<size_t>(fixpoint());
    // A free bit that fell out of the "starts at 0" hypothesis may be a constant 1.
    std::vector<std::pair<size_t, int>> retry;
    for (size_t f = 0; f < flops.size(); ++f) {
      auto& info = flops[f];
      if (info.has_reset) {
        continue;
      }
      for (int i = 0; i < info.width; ++i) {
        if (info.klass[static_cast<size_t>(i)] < 0) {
          retry.emplace_back(f, i);
          info.klass[static_cast<size_t>(i)] = 1;
        }
      }
    }
    if (!retry.empty()) {
      stats.fix_rounds += static_cast<size_t>(fixpoint());
    }
    for (const auto& info : flops) {
      for (int i = 0; i < info.width; ++i) {
        if (info.klass[static_cast<size_t>(i)] >= 0) {
          return true;
        }
      }
    }
    return false;
  }

  // Rebuild `info.node` keeping only the bits for which `keep[i]` is true. Bits
  // that are not kept read `replace[i]` (a constant, 0/1) -- or 0 when unobserved.
  // Returns true when the graph changed.
  bool rebuild(const Flop_info& info, const std::vector<bool>& keep, const std::vector<int8_t>& replace) {
    const int  w = info.width;
    int        kept = 0;
    for (int i = 0; i < w; ++i) {
      kept += keep[static_cast<size_t>(i)] ? 1 : 0;
    }
    if (kept == w) {
      return false;
    }
    if (info.is_signed && kept != 0) {
      return false;  // a partial split would change a signed Q's meaning
    }
    const auto old_q = info.node.get_driver_pin(0);
    const auto name  = std::string(gu::node_name_of(info.node));

    hhds::Pin_class result;
    if (kept == 0) {
      result = const_bits(g, replace, 0, w, info.is_signed);
    } else {
      // Lanes LSB first: runs of kept bits become narrower flops, the rest constants.
      std::vector<Lane> lanes;
      int               i = 0;
      while (i < w) {
        int j = i;
        if (keep[static_cast<size_t>(i)]) {
          while (j < w && keep[static_cast<size_t>(j)]) {
            ++j;
          }
          lanes.push_back({make_flop_run(info, i, j, name, w), j - i});
        } else {
          while (j < w && !keep[static_cast<size_t>(j)]) {
            ++j;
          }
          lanes.push_back({const_bits(g, replace, i, j), j - i});
        }
        i = j;
      }
      result = lanes.size() == 1 ? lanes.front().value : make_concat(g, lanes);
    }
    // Every reader of the old Q -- including the new flops' own feedback, which
    // was wired to the old Q while they were built -- now reads the new value.
    for (const auto& e : readers_of(old_q)) {
      if (e.sink.get_master_node() == info.node) {
        continue;  // the old flop's own pins go with it
      }
      e.sink.del_sink(old_q);
      result.connect_sink(e.sink);
    }
    info.node.del_node();
    return true;
  }

  hhds::Pin_class make_flop_run(const Flop_info& info, int lo, int hi, const std::string& name, int w) {
    auto src = info.node;
    auto dst = gu::create_typed_node(g, Ntype_op::Flop);
    gu::carry_node_attrs(src, dst);
    for (const auto& sink : src.inp_sorted_pins()) {
      const auto pid = sink.get_port_id();
      const auto sname = Ntype::get_sink_name(Ntype_op::Flop, pid);
      for (const auto& drv : sink.get_driver_pins()) {
        hhds::Pin_class driver = drv;
        if (lo != 0 || hi != w) {
          if (sname == "din") {
            auto get = gu::create_get_mask(g, drv, lo, hi);
            driver   = get.create_driver_pin(0);
            gu::set_ubits(driver, hi - lo);
          } else if (sname == "initial" && drv.is_const()) {
            driver = gu::create_const(g, *gu::const_of(drv).get_mask_op_opt(lo, hi));
          }
        }
        driver.connect_sink(dst.create_sink_pin(pid));
      }
    }
    if (!name.empty() && (lo != 0 || hi != w)) {
      const auto piece = hi - lo == 1 ? livehd::bus_name::bit(name, lo) : std::format("{}[{}:{}]", name, hi - 1, lo);
      dst.attr(hhds::attrs::name).set(piece);
    }
    auto out = dst.create_driver_pin(0);
    gu::set_bits(out, hi - lo);
    gu::set_unsign(out);
    return out;
  }

  // ----------------------------------------------------------- unobserved
  // Backward demanded-bits analysis. `demand[pin]` holds the bits of the value
  // `pin` drives that something observable can read. Roots (everything that is
  // not a combinational op or a plain flop) read all their inputs; the graph
  // outputs are observed. State that is only read by itself or by other
  // unobserved state is never demanded.
  absl::flat_hash_map<hhds::Class_index, std::vector<bool>> demand;
  absl::flat_hash_set<hhds::Class_index>                    full;  // width unknown: every bit
  std::deque<hhds::Node_class>                              queue;
  absl::flat_hash_set<hhds::Class_index>                    queued;

  void schedule(const hhds::Node_class& n) {
    if (queued.insert(n.get_class_index()).second) {
      queue.push_back(n);
    }
  }

  // Bits [lo, hi) of what `drv` drives are observed.
  void need(const hhds::Pin_class& drv, int lo, int hi) {
    if (drv.is_invalid() || drv.is_const() || gu::is_graph_input_pin(drv) || hi <= lo) {
      return;
    }
    const auto node = drv.get_master_node();
    const int  w    = gu::bits_of(drv);
    if (w <= 0) {
      if (full.insert(drv.get_class_index()).second) {
        schedule(node);
      }
      return;
    }
    auto& d = demand[drv.get_class_index()];
    if (d.empty()) {
      d.assign(static_cast<size_t>(w), false);
    }
    bool grew = false;
    for (int i = std::max(lo, 0); i < std::min(hi, w); ++i) {
      if (!d[static_cast<size_t>(i)]) {
        d[static_cast<size_t>(i)] = true;
        grew                      = true;
      }
    }
    // a signed value read past its width reads its sign bit
    if (hi > w && !gu::is_unsign(drv) && !d[static_cast<size_t>(w - 1)]) {
      d[static_cast<size_t>(w - 1)] = true;
      grew                          = true;
    }
    if (grew) {
      schedule(node);
    }
  }
  void need_all(const hhds::Pin_class& drv) {
    if (!drv.is_invalid() && !drv.is_const()) {
      const int w = gu::bits_of(drv);
      need(drv, 0, w > 0 ? w : 1);
      if (w <= 0) {
        full.insert(drv.get_class_index());
        schedule(drv.get_master_node());
      }
    }
  }
  void need_all_inputs(const hhds::Node_class& n) {
    for (const auto& sink : n.inp_sorted_pins()) {
      for (const auto& d : sink.get_driver_pins()) {
        need_all(d);
      }
    }
  }

  [[nodiscard]] bool is_demanded(const hhds::Pin_class& out, int i) const {
    if (full.contains(out.get_class_index())) {
      return true;
    }
    auto it = demand.find(out.get_class_index());
    return it != demand.end() && i >= 0 && i < static_cast<int>(it->second.size()) && it->second[static_cast<size_t>(i)];
  }
  [[nodiscard]] bool any_demand(const hhds::Pin_class& out) const {
    if (full.contains(out.get_class_index())) {
      return true;
    }
    auto it = demand.find(out.get_class_index());
    return it != demand.end() && std::ranges::any_of(it->second, [](bool b) { return b; });
  }
  [[nodiscard]] int top_demand(const hhds::Pin_class& out) const {
    if (full.contains(out.get_class_index())) {
      return std::max(gu::bits_of(out), 1);
    }
    auto it = demand.find(out.get_class_index());
    if (it == demand.end()) {
      return 0;
    }
    for (int i = static_cast<int>(it->second.size()) - 1; i >= 0; --i) {
      if (it->second[static_cast<size_t>(i)]) {
        return i + 1;
      }
    }
    return 0;
  }

  // Positional pass-through: input bit j is needed when output bit j is.
  void need_same(const hhds::Pin_class& in, const hhds::Pin_class& out) {
    const int w = std::max(gu::bits_of(out), 1);
    for (int j = 0; j < w; ++j) {
      if (is_demanded(out, j)) {
        need(in, j, j + 1);
      }
    }
  }

  // Every shift amount `b` can take: a constant, a narrow unsigned value (all of
  // them), or an affine word-select index*stride + bias (one per index value).
  // False when the set is not small and explicit.
  bool possible_shift_amounts(const hhds::Pin_class& b, std::vector<int64_t>& out) const {
    if (b.is_const()) {
      const auto& c = gu::const_of(b);
      if (c.has_unknowns() || !c.is_just_i64() || c.to_just_i64() < 0 || c.to_just_i64() > (1 << 24)) {
        return false;
      }
      out.push_back(c.to_just_i64());
      return true;
    }
    const int bw = gu::bits_of(b);
    if (bw > 0 && bw <= 8 && gu::is_unsign(b)) {
      for (int64_t k = 0; k < (int64_t{1} << bw); ++k) {
        out.push_back(k);
      }
      return true;
    }
    const auto chain = livehd::synth::affine_chain(b);
    if (!chain) {
      return false;
    }
    const int iw = gu::bits_of(chain->index);
    if (iw <= 0 || iw > 10) {
      return false;
    }
    for (uint64_t v = 0; v < (uint64_t{1} << iw); ++v) {
      const auto amount = chain->eval(v);
      if (!amount || *amount > (1U << 24)) {
        return false;
      }
      out.push_back(static_cast<int64_t>(*amount));
    }
    return true;
  }

  void process(const hhds::Node_class& n) {
    const auto op  = gu::type_op_of(n);
    if (op == Ntype_op::Memory) {
      // any read port observed => every input (address, data, enable, clock) is
      for (const auto& port : n.out_sorted_pins()) {
        if (any_demand(port)) {
          need_all_inputs(n);
          return;
        }
      }
      return;
    }
    const auto out = n.get_driver_pin(0);
    if (out.is_invalid() || !any_demand(out)) {
      return;
    }
    const int w = std::max(gu::bits_of(out), 1);
    switch (op) {
      case Ntype_op::And:
      case Ntype_op::Or:
      case Ntype_op::Xor:
      case Ntype_op::Not:
        for (const auto& sink : n.inp_sorted_pins()) {
          for (const auto& d : sink.get_driver_pins()) {
            need_same(d, out);
          }
        }
        return;
      case Ntype_op::Mux:
        for (const auto& sink : n.inp_sorted_pins()) {
          for (const auto& d : sink.get_driver_pins()) {
            if (sink.get_port_id() == 0) {
              need_all(d);
            } else {
              need_same(d, out);
            }
          }
        }
        return;
      case Ntype_op::Hotmux: {
        size_t total = 0;
        for (const auto& sink : n.inp_sorted_pins()) {
          (void)sink;
          ++total;
        }
        size_t idx = 0;
        for (const auto& sink : n.inp_sorted_pins()) {
          const bool is_value = (idx % 2 == 1) || (total % 2 == 1 && idx == total - 1);
          for (const auto& d : sink.get_driver_pins()) {
            if (is_value) {
              need_same(d, out);
            } else {
              need_all(d);
            }
          }
          ++idx;
        }
        return;
      }
      case Ntype_op::Get_mask: {
        const auto a  = gu::get_driver_of_sink_name(n, "a");
        const auto lo = gu::get_driver_of_sink_name(n, "lo");
        const auto hi = gu::get_driver_of_sink_name(n, "hi");
        if (!lo.is_const() || !gu::const_of(lo).is_just_i64()) {
          need_all_inputs(n);
          return;
        }
        const int base = static_cast<int>(gu::const_of(lo).to_just_i64());
        for (int j = 0; j < w; ++j) {
          if (is_demanded(out, j)) {
            need(a, base + j, base + j + 1);
          }
        }
        (void)hi;
        return;
      }
      case Ntype_op::Set_mask: {
        const auto a     = gu::get_driver_of_sink_name(n, "a");
        const auto value = gu::get_driver_of_sink_name(n, "value");
        const auto lo    = gu::get_driver_of_sink_name(n, "lo");
        const auto hi    = gu::get_driver_of_sink_name(n, "hi");
        if (!lo.is_const() || !hi.is_const() || !gu::const_of(lo).is_just_i64() || !gu::const_of(hi).is_just_i64()) {
          need_all_inputs(n);
          return;
        }
        const int l = static_cast<int>(gu::const_of(lo).to_just_i64());
        const int h = static_cast<int>(gu::const_of(hi).to_just_i64());
        for (int j = 0; j < w; ++j) {
          if (!is_demanded(out, j)) {
            continue;
          }
          if (j >= l && j < h) {
            need(value, j - l, j - l + 1);
          } else {
            need(a, j, j + 1);
          }
        }
        return;
      }
      case Ntype_op::Concat: {
        std::vector<std::pair<hhds::Pin_class, int>> lanes;  // MSB first
        hhds::Pin_class                              pending;
        bool                                         ok = true;
        for (const auto& sink : n.inp_sorted_pins()) {
          const auto d = sink.get_driver_pin();
          if (sink.get_port_id() % 2 == 0) {
            pending = d;
          } else if (d.is_const() && gu::const_of(d).is_just_i64()) {
            lanes.emplace_back(pending, static_cast<int>(gu::const_of(d).to_just_i64()));
          } else {
            ok = false;
          }
        }
        if (!ok) {
          need_all_inputs(n);
          return;
        }
        int off = 0;
        for (auto it = lanes.rbegin(); it != lanes.rend(); ++it) {
          for (int i = 0; i < it->second; ++i) {
            if (is_demanded(out, off + i)) {
              need(it->first, i, i + 1);
            }
          }
          off += it->second;
        }
        return;
      }
      case Ntype_op::Sum: {
        const int top = top_demand(out);
        for (const auto& sink : n.inp_sorted_pins()) {
          for (const auto& d : sink.get_driver_pins()) {
            need(d, 0, top);
          }
        }
        return;
      }
      case Ntype_op::SRA:
      case Ntype_op::SHL: {
        const bool left = op == Ntype_op::SHL;
        const auto a    = gu::get_driver_of_sink_name(n, "a");
        const auto b    = gu::get_driver_of_sink_name(n, "b");
        std::vector<int64_t> amounts;
        if (a.is_invalid() || b.is_invalid() || !possible_shift_amounts(b, amounts)) {
          need_all_inputs(n);
          return;
        }
        need_all(b);
        for (int j = 0; j < w; ++j) {
          if (!is_demanded(out, j)) {
            continue;
          }
          for (const auto k : amounts) {
            const int64_t src = left ? j - k : j + k;
            if (src >= 0 && src < (1 << 24)) {
              need(a, static_cast<int>(src), static_cast<int>(src) + 1);
            } else if (!left) {
              need(a, 0, 1);  // nothing sensible: keep the bottom bit alive
            }
          }
        }
        return;
      }
      case Ntype_op::Flop: {
        for (const auto& sink : n.inp_sorted_pins()) {
          const auto sname = Ntype::get_sink_name(Ntype_op::Flop, sink.get_port_id());
          for (const auto& d : sink.get_driver_pins()) {
            if (sname == "din") {
              need_same(d, out);
            } else {
              need_all(d);
            }
          }
        }
        return;
      }
      default: need_all_inputs(n); return;
    }
  }

  static bool is_pure(Ntype_op op) {
    switch (op) {
      case Ntype_op::Sum:
      case Ntype_op::Mult:
      case Ntype_op::Div:
      case Ntype_op::Rem:
      case Ntype_op::And:
      case Ntype_op::Or:
      case Ntype_op::Xor:
      case Ntype_op::Ror:
      case Ntype_op::Not:
      case Ntype_op::Get_mask:
      case Ntype_op::Set_mask:
      case Ntype_op::Sext:
      case Ntype_op::Concat:
      case Ntype_op::LT:
      case Ntype_op::GT:
      case Ntype_op::EQ:
      case Ntype_op::SHL:
      case Ntype_op::SRA:
      case Ntype_op::LUT:
      case Ntype_op::Mux:
      case Ntype_op::Hotmux:
      case Ntype_op::Rxor:
      case Ntype_op::Popcount: return true;
      default: return false;
    }
  }

  void analyze_demand() {
    demand.clear();
    full.clear();
    queue.clear();
    queued.clear();
    for (auto node : g.body().nodes()) {
      const auto op = gu::type_op_of(node);
      if (op == Ntype_op::Flop && single_depth(node)) {
        continue;  // observed only through its Q
      }
      if (is_pure(op)) {
        continue;
      }
      if (op == Ntype_op::Sub && !opt.observe_markers && gu::is_property_marker(node)) {
        continue;  // no hardware in a synthesis flow
      }
      if (op == Ntype_op::Memory && !gu::memory_image_of(node)) {
        continue;  // observed only through its read data (a loader-supplied image is observable)
      }
      need_all_inputs(node);  // instances, memories, latches, markers, ... observe everything they read
    }
    for (const auto& sink : g.get_output_node().inp_sorted_pins()) {
      for (const auto& d : sink.get_driver_pins()) {
        need_all(d);
      }
    }
    while (!queue.empty()) {
      auto n = queue.front();
      queue.pop_front();
      queued.erase(n.get_class_index());
      if (!n.is_invalid()) {
        process(n);
      }
    }
  }

  // Remove the flop bits nothing observes. Returns true when the graph changed.
  bool apply_dead() {
    analyze_demand();
    bool changed = false;
    std::vector<hhds::Node_class> all;
    for (auto node : g.body().nodes()) {
      if (gu::type_op_of(node) == Ntype_op::Flop && single_depth(node)) {
        all.push_back(node);
      }
    }
    for (const auto& node : all) {
      const auto q = node.get_driver_pin(0);
      if (q.is_invalid()) {
        continue;
      }
      const int w = gu::bits_of(q);
      if (w <= 0 || full.contains(q.get_class_index())) {
        continue;
      }
      const auto initial = gu::get_driver_of_sink_name(node, "initial");
      if (!initial.is_invalid() && !initial.is_const()) {
        continue;
      }
      if (gu::get_driver_of_sink_name(node, "din").is_invalid()) {
        continue;
      }
      std::vector<bool>   keep(static_cast<size_t>(w), false);
      size_t              dead = 0;
      for (int i = 0; i < w; ++i) {
        keep[static_cast<size_t>(i)] = is_demanded(q, i);
        dead += keep[static_cast<size_t>(i)] ? 0 : 1;
      }
      if (dead == 0) {
        continue;
      }
      Flop_info info;
      info.node      = node;
      info.width     = w;
      info.is_signed = !gu::is_unsign(q);
      if (info.is_signed && dead != static_cast<size_t>(w)) {
        continue;
      }
      std::vector<int8_t> zeros(static_cast<size_t>(w), 0);
      if (rebuild(info, keep, zeros)) {
        stats.dead_bits += dead;
        stats.flops_rebuilt += 1;
        changed = true;
      }
    }
    // A memory none of whose read data is observed is storage nobody can see.
    std::vector<hhds::Node_class> memories;
    for (auto node : g.body().nodes()) {
      if (gu::type_op_of(node) == Ntype_op::Memory && !gu::memory_image_of(node)) {
        memories.push_back(node);
      }
    }
    for (const auto& mem : memories) {
      bool observed = false;
      for (const auto& port : mem.out_sorted_pins()) {
        observed = observed || any_demand(port);
      }
      if (observed) {
        continue;
      }
      const auto zero = gu::create_const(g, *Dlop::create_integer(0));
      for (const auto& port : mem.out_sorted_pins()) {
        for (const auto& e : readers_of(port)) {
          if (e.sink.get_master_node() == mem) {
            continue;
          }
          e.sink.del_sink(port);
          zero.connect_sink(e.sink);
        }
      }
      mem.del_node();
      stats.dead_memories += 1;
      changed = true;
    }
    return changed;
  }

  bool apply_constants() {
    bool changed = false;
    for (auto& info : flops) {
      std::vector<bool>   keep(static_cast<size_t>(info.width), true);
      std::vector<int8_t> replace(static_cast<size_t>(info.width), 0);
      size_t              n = 0;
      for (int i = 0; i < info.width; ++i) {
        if (info.klass[static_cast<size_t>(i)] >= 0) {
          keep[static_cast<size_t>(i)]    = false;
          replace[static_cast<size_t>(i)] = info.klass[static_cast<size_t>(i)];
          ++n;
        }
      }
      if (n == 0) {
        continue;
      }
      if (rebuild(info, keep, replace)) {
        changed = true;
        stats.const_bits += n;
        stats.flops_rebuilt += 1;
      }
    }
    return changed;
  }
};

State_stats sweep_state(hhds::Graph& g, const State_options& options) {
  State_sweeper s(g, options);
  s.collect();
  if (!s.flops.empty() && s.classify_constants()) {
    s.apply_constants();
  }
  if (options.unobserved) {
    // the constants above changed the graph under the demand maps: a fresh sweeper
    State_sweeper d(g, options);
    d.apply_dead();
    s.stats.dead_bits += d.stats.dead_bits;
    s.stats.dead_memories += d.stats.dead_memories;
    s.stats.flops_rebuilt += d.stats.flops_rebuilt;
  }
  return s.stats;
}

}  // namespace livehd::sweep
