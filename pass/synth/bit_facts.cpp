// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "bit_facts.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "node_util.hpp"

namespace livehd::synth::bit_facts {
namespace gu = livehd::graph_util;

namespace {
void shape(const hhds::Pin_class& from, const hhds::Pin_class& to) {
  gu::set_bits(to, std::max(gu::bits_of(from), 1));
  gu::is_unsign(from) ? gu::set_unsign(to) : gu::set_sign(to);
}

std::vector<bool> known_zero_bits_core(const hhds::Pin_class& pin, int width, int depth);
}  // namespace

std::vector<bool> known_zero_bits(const hhds::Pin_class& pin, int width, int depth) {
  auto zero = known_zero_bits_core(pin, width, depth);
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

namespace {
std::vector<bool> known_zero_bits_core(const hhds::Pin_class& pin, int width, int depth) {
  std::vector<bool> zero(static_cast<size_t>(std::max(width, 0)), false);
  if (pin.is_invalid() || width <= 0 || depth > 8) {
    return zero;
  }
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
  const auto node = pin.get_master_node();
  const auto op   = gu::type_op_of(node);
  const auto all_of_inputs = [&](bool and_mode, const std::vector<hhds::Pin_class>& inputs) {
    // and_mode: zero if ANY input is zero at the bit; else zero if ALL are.
    std::vector<bool> acc(zero.size(), and_mode ? false : true);
    for (const auto& in : inputs) {
      const auto z = known_zero_bits(in, width, depth + 1);
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
        const auto z = known_zero_bits(d, width, depth + 1);
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
    std::vector<hhds::Pin_class> vals;
    const auto                   sinks = node.inp_sorted_pins();
    size_t                       n     = 0;
    for (const auto& sink : sinks) {
      (void)sink;
      ++n;
    }
    size_t idx = 0;
    for (const auto& sink : sinks) {
      if ((idx % 2 == 1) || (n % 2 == 1 && idx == n - 1)) {
        vals.push_back(sink.get_driver_pin());
      }
      ++idx;
    }
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
        if (!w.is_const() || !gu::const_of(w).is_just_i64()) {
          return zero;
        }
        lanes.emplace_back(pending, static_cast<int>(gu::const_of(w).to_just_i64()));
      }
    }
    int offset = 0;
    for (auto it = lanes.rbegin(); it != lanes.rend(); ++it) {
      const auto z = known_zero_bits(it->first, it->second, depth + 1);
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
  if (op == Ntype_op::SHL) {
    // a constant left shift: zero below the shift amount, the operand's own zeros above it
    const auto a = gu::get_driver_of_sink_name(node, "a");
    const auto b = gu::get_driver_of_sink_name(node, "b");
    if (!a.is_invalid() && !b.is_invalid() && b.is_const() && gu::const_of(b).is_just_i64() && gu::const_of(b).to_just_i64() >= 0
        && gu::const_of(b).to_just_i64() < (1 << 20)) {
      const int  sh = static_cast<int>(gu::const_of(b).to_just_i64());
      const auto za = known_zero_bits(a, std::max(width - sh, 0), depth + 1);
      for (int i = 0; i < width; ++i) {
        zero[static_cast<size_t>(i)] = i < sh || za[static_cast<size_t>(i - sh)];
      }
    }
    return zero;
  }
  if (op == Ntype_op::Get_mask) {
    const auto a  = gu::get_driver_of_sink_name(node, "a");
    const auto lo = gu::get_driver_of_sink_name(node, "lo");
    const auto hi = gu::get_driver_of_sink_name(node, "hi");
    if (!a.is_invalid() && lo.is_const() && hi.is_const() && gu::const_of(lo).is_just_i64() && gu::const_of(hi).is_just_i64()) {
      const int l  = static_cast<int>(gu::const_of(lo).to_just_i64());
      const int h  = static_cast<int>(gu::const_of(hi).to_just_i64());
      const auto za = known_zero_bits(a, h, depth + 1);
      for (int i = 0; i < width; ++i) {
        zero[static_cast<size_t>(i)] = l + i >= h || (l + i >= 0 && za[static_cast<size_t>(l + i)]);  // a Get_mask is non-negative
      }
    }
    return zero;
  }
  return zero;
}
}  // namespace

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
