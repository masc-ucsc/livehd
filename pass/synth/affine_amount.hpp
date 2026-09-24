// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// A dynamic word select `v[index]` lowers to a shift whose amount is
// `index*scale + bias`. Front ends spell it `(index << 5) + 32`; cprop's
// low-lane narrowing (pass/cprop/cprop_lowlane.cpp) respells the same value
// `Shl(Sum(index, 1), 5)` or `Or(Shl(Sum(index, c), 5), low)`. Every spelling
// is one CHAIN: each link has exactly one variable operand and constants
// elsewhere, from the narrow index out to the amount.
#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "node_util.hpp"

namespace livehd::synth {

struct Affine_chain {
  hhds::Pin_class                                        index;  // the one variable leaf
  std::vector<std::pair<hhds::Node_class, hhds::Pin_class>> links;  // index side first; links.back() drives the amount
  int64_t                                                scale = 1;
  int64_t                                                bias  = 0;
  // (scale, bias) of each link's own output, parallel to `links`.
  std::vector<std::pair<int64_t, int64_t>>               link_affine;
};

// amount = index*scale + bias through Sum (added variable, constant terms),
// SHL (variable `a`, constant `b`), Mult (constant factors), Or (a constant
// that fits the variable's zero low bits) and And(x, -2^k) (satopt's proven
// low-zero mask, an identity there) links, at least one of them a
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
    if (op != Ntype_op::Sum && op != Ntype_op::SHL && op != Ntype_op::Mult && op != Ntype_op::Or && op != Ntype_op::And) {
      break;
    }
    hhds::Pin_class var;
    int64_t         k     = op == Ntype_op::Mult ? 1 : 0;
    bool            valid = true;
    for (const auto& in : node.inp_sorted_pins()) {
      const auto drv = in.get_driver_pin();
      const auto pid = in.get_port_id();
      if (op == Ntype_op::SHL) {
        if (pid == 0) {
          var = drv;
        } else if (const auto c = small_const(drv); c && *c >= 0 && *c < 40) {
          k = *c;
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
    if (!valid || var.is_invalid() || var.is_const()) {
      break;
    }
    steps.push_back({op, k});
    chain.links.emplace_back(node, cur);
    stride = stride || op == Ntype_op::SHL || op == Ntype_op::Mult;
    cur    = var;
  }
  if (steps.empty() || !stride || cur.is_invalid() || cur.is_const()) {
    return std::nullopt;
  }
  chain.index = cur;
  std::reverse(chain.links.begin(), chain.links.end());
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
    chain.link_affine.emplace_back(chain.scale, chain.bias);
  }
  return chain;
}

}  // namespace livehd::synth
