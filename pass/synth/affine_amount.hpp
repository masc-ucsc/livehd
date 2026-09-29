// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// A dynamic word select `v[index]` lowers to a shift whose amount is
// `index*scale + bias`. Front ends spell it `(index << 5) + 32`; cprop's
// low-lane narrowing (pass/cprop/cprop_lowlane.cpp) respells the same value
// `Shl(Sum(index, 1), 5)` or `Or(Shl(Sum(index, c), 5), low)`. Every spelling
// is one CHAIN: each link has exactly one variable operand and constants
// elsewhere, from the narrow index out to the amount.
#include <algorithm>
#include <bit>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "node_util.hpp"

namespace livehd::synth {

// One link of the chain as its cell computes it (see Affine_chain::eval).
struct Affine_link {
  Ntype_op op;
  int64_t  k;       // Sum: added constant; SHL: amount; Mult: factor; Or: constant;
                    // And: trailing-zero count of the mask; Get_mask: kept low bits (-1: all)
  int      bits;    // the link's output width
  bool     unsign;  // the link's output sign
};

struct Affine_chain {
  hhds::Pin_class                                        index;  // the one variable leaf
  std::vector<std::pair<hhds::Node_class, hhds::Pin_class>> links;  // index side first; links.back() drives the amount
  int64_t                                                scale = 1;
  int64_t                                                bias  = 0;
  std::vector<Affine_link>                               ops;  // parallel to `links`

  // The exact value the amount NET carries (its low `bits` bits) for an index
  // value `v`, every link computed and truncated to its own output width as
  // the cells do -- so a link may wrap, e.g. satopt's odc narrowing
  // `Get_mask(amount, 0x3ff)` of an amount whose top value (1024) no output
  // observes. nullopt when a value leaves the modelled range or an
  // intermediate signed output would read negative.
  [[nodiscard]] std::optional<uint64_t> eval(uint64_t v) const {
    constexpr uint64_t kLimit = uint64_t{1} << 52;
    for (size_t i = 0; i < ops.size(); ++i) {
      const auto& l = ops[i];
      switch (l.op) {
        case Ntype_op::Sum:
          if (l.k < 0 && v < static_cast<uint64_t>(-l.k)) {
            return std::nullopt;
          }
          v = l.k < 0 ? v - static_cast<uint64_t>(-l.k) : v + static_cast<uint64_t>(l.k);
          break;
        case Ntype_op::SHL: v <<= l.k; break;
        case Ntype_op::Mult: v *= static_cast<uint64_t>(l.k); break;
        case Ntype_op::Or: v |= static_cast<uint64_t>(l.k); break;
        case Ntype_op::And: v &= ~((uint64_t{1} << l.k) - 1); break;
        case Ntype_op::Get_mask:
          if (l.k >= 0) {
            v &= (uint64_t{1} << l.k) - 1;
          }
          break;
        default: return std::nullopt;
      }
      if (v >= kLimit || l.bits <= 0) {
        return std::nullopt;
      }
      if (l.bits < 63) {
        const bool last = i + 1 == ops.size();
        if (!l.unsign && !last && v >= (uint64_t{1} << (l.bits - 1))) {
          return std::nullopt;  // a negative intermediate: not modelled
        }
        v &= (uint64_t{1} << l.bits) - 1;
      }
    }
    return v;
  }
};

// amount = index*scale + bias through Sum (added variable, constant terms),
// SHL (variable `a`, constant `b`), Mult (constant factors), Or (a constant
// that fits the variable's zero low bits), And(x, -2^k) (satopt's proven
// low-zero mask, an identity there) and Get_mask(x, 2^n-1 or -1) (a low
// truncation, e.g. satopt's odc narrowing; scale/bias read it as the
// identity, `eval` applies the wrap) links, at least one of them a
// stride (SHL or Mult): cprop folds `(i << 5) + 32` to `Shl(i + 1, 5)`, a
// one-link chain whose index is `i + 1`. nullopt for any other shape, a negative/overflowing
// scale or bias, or a graph input inside the chain.
inline std::optional<Affine_chain> affine_chain(const hhds::Pin_class& amount) {
  namespace gu = livehd::graph_util;
  const auto small_const = [](const hhds::Pin_class& p) -> std::optional<int64_t> {
    if (!p.is_const()) {
      return std::nullopt;
    }
    const auto& c = gu::const_of(p);
    if (c.has_unknowns() || !c.is_just_i64()) {
      return std::nullopt;
    }
    const auto v = c.to_just_i64();
    return v >= -(int64_t{1} << 40) && v <= (int64_t{1} << 40) ? std::optional<int64_t>{v} : std::nullopt;
  };
  struct Step {
    Ntype_op op;
    int64_t  k;  // Sum: added constant; SHL: amount; Mult: factor; Or: constant
  };
  Affine_chain         chain;
  std::vector<Step>    steps;  // amount side first
  hhds::Pin_class      cur = amount;
  bool                 stride = false;
  while (!cur.is_invalid() && !cur.is_const() && !gu::is_graph_input_pin(cur) && cur.get_port_id() == 0) {
    const auto node = cur.get_master_node();
    const auto op   = gu::type_op_of(node);
    if (op != Ntype_op::Sum && op != Ntype_op::SHL && op != Ntype_op::Mult && op != Ntype_op::Or && op != Ntype_op::And
        && op != Ntype_op::Get_mask) {
      break;
    }
    hhds::Pin_class var;
    int64_t         k     = op == Ntype_op::Mult ? 1 : 0;
    bool            valid = true;
    bool            has_k = false;  // SHL/Get_mask: exactly one constant operand
    for (const auto& in : node.inp_sorted_pins()) {
      const auto drv = in.get_driver_pin();
      const auto pid = in.get_port_id();
      if (op == Ntype_op::SHL || op == Ntype_op::Get_mask) {
        const auto c = pid == 0 || has_k ? std::nullopt : small_const(drv);
        if (pid == 0) {
          var = drv;
        } else if (c && op == Ntype_op::SHL && *c >= 0 && *c < 40) {
          k     = *c;
          has_k = true;
        } else if (c && op == Ntype_op::Get_mask && (*c == -1 || (*c > 0 && ((*c + 1) & *c) == 0))) {
          // A low window [0, n), or -1: the whole value.
          k     = *c == -1 ? -1 : static_cast<int64_t>(std::bit_width(static_cast<uint64_t>(*c)));
          has_k = true;
        } else {
          valid = false;
        }
        continue;
      }
      if (const auto c = small_const(drv)) {
        if (op == Ntype_op::Sum) {
          k += Ntype::sink_bank(op, pid) == 1 ? -*c : *c;
        } else if (op == Ntype_op::Mult) {
          valid = valid && *c > 0 && k <= (int64_t{1} << 40) / *c;
          k    *= *c;
        } else if (op == Ntype_op::And) {
          // Only And(x, -2^k), satopt's proven-low-zero mask (k stored as the
          // mask's trailing zero count).
          const int tz = gu::const_of(drv).get_trailing_zeroes();
          valid        = valid && k == 0 && tz > 0 && tz < 40 && *c == -(int64_t{1} << tz);
          k            = tz;
        } else {
          valid = valid && *c >= 0 && k == 0;
          k     = *c;
        }
      } else if (var.is_invalid() && (op != Ntype_op::Sum || Ntype::sink_bank(op, pid) == 0)) {
        var = drv;
      } else {
        valid = false;
      }
    }
    if (!valid || var.is_invalid() || var.is_const() || ((op == Ntype_op::SHL || op == Ntype_op::Get_mask) && !has_k)) {
      break;
    }
    steps.push_back({op, k});
    chain.links.emplace_back(node, cur);
    chain.ops.push_back({op, k, gu::bits_of(cur), gu::is_unsign(cur)});
    stride = stride || op == Ntype_op::SHL || op == Ntype_op::Mult;
    cur    = var;
  }
  // A truncation on the index side narrows the index itself (`sel#[0..4]`):
  // its output is the index, never a link to the wider value behind it.
  while (!steps.empty() && steps.back().op == Ntype_op::Get_mask) {
    cur = chain.links.back().second;
    steps.pop_back();
    chain.links.pop_back();
    chain.ops.pop_back();
  }
  if (steps.empty() || !stride || cur.is_invalid() || cur.is_const()) {
    return std::nullopt;
  }
  chain.index = cur;
  std::reverse(chain.links.begin(), chain.links.end());
  std::reverse(chain.ops.begin(), chain.ops.end());
  // Index outward: value = index*scale + bias.
  for (auto it = steps.rbegin(); it != steps.rend(); ++it) {
    const auto [op, k] = *it;
    if (op == Ntype_op::Sum) {
      chain.bias += k;
    } else if (op == Ntype_op::SHL) {
      chain.scale <<= k;
      chain.bias  <<= k;
    } else if (op == Ntype_op::Mult) {
      chain.scale *= k;
      chain.bias  *= k;
    } else if (op == Ntype_op::Get_mask) {
      // The identity for scale/bias; a wrap is Affine_chain::eval's.
    } else if (op == Ntype_op::And) {
      // An identity only when the variable part keeps those bits zero.
      const int64_t low = int64_t{1} << k;
      if ((chain.scale & (low - 1)) != 0 || (chain.bias & (low - 1)) != 0) {
        return std::nullopt;
      }
    } else {
      // Or adds only into zero bits: the constant must sit below every bit
      // the variable part (index*scale + bias) can set.
      const int64_t low = chain.scale & -chain.scale;  // largest power of two dividing scale
      if (k >= low || (chain.bias & (low - 1)) != 0 || (chain.bias & k) != 0) {
        return std::nullopt;
      }
      chain.bias += k;
    }
    if (chain.scale <= 0 || chain.bias < 0 || chain.scale > (int64_t{1} << 40) || chain.bias > (int64_t{1} << 50)) {
      return std::nullopt;
    }
  }
  return chain;
}

}  // namespace livehd::synth
