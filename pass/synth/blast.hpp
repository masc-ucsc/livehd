// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The combinational cell translation shared by region mapping and satopt.
// The caller owns boundaries, scheduling, bit storage and failure policy.
#include <algorithm>
#include <climits>
#include <format>
#include <optional>
#include <print>
#include <vector>

#include "affine_amount.hpp"
#include "arith.hpp"
#include "absl/container/btree_map.h"
#include "absl/container/node_hash_map.h"
#include "cell.hpp"
#include "dlop.hpp"
#include "node_util.hpp"
#include "pass_partition.hpp"

namespace livehd::synth {
namespace blast_gu = livehd::graph_util;

// The blaster's knobs (abc_cleanup.md section 5): the arithmetic
// architectures, and how registers cross a region's translation.
struct Blast_options {
  arith::Adder_kind adder          = arith::Adder_kind::rca;
  int               block_size     = 0;  // CSKA skip-block / CLA group width; 0 = auto from the operating width
  arith::Mult_kind  multiplier     = arith::Mult_kind::array;
  bool              reverse_barrel = false;
  // Flops cross as latches (register mapping); false keeps every flop a native
  // boundary.
  bool              map_register   = true;
  // A crossing latch without a power-on value stores ~next_state: the QN-only
  // DFF encoding, exact only under a flow that preserves the latches.
  bool              qn_encode      = false;
  // Asynchronous-reset register cells for reset value v: -1 none, else that
  // cell's q_inverted (0/1). A register whose async reset the cells can
  // express crosses as a latch (reset left OUT of D, Seq_flop::async_reset);
  // otherwise it stays a native boundary.
  int8_t            areset_cell[2] = {-1, -1};
  // Whether that cell's reset pin asserts at 0 (Dff_cell::reset_low): the
  // level an internally computed reset crosses at (Seq_flop::arst_po).
  bool              areset_low[2]  = {false, false};
  // The flow keeps every latch as crossed (why an async cell may be absent:
  // reported precisely in the reset-native diagnostic).
  bool              areset_flow_ok = true;
  bool              verbose        = false;
};

// Sparse wiring is shared as well as Boolean cells: resolving one demanded
// bit must agree on negative masks, concat windows, and sign extension.
template <class Bit>
class Wiring_blaster {
  struct Run {
    int lo, hi, base;
  };
  struct Alias {
    hhds::Pin_class  base, value;
    std::vector<Run> runs;
    bool             valid = false;
  };
  absl::node_hash_map<hhds::Node_class, Alias>                              aliases_;
  absl::node_hash_map<hhds::Node_class, std::vector<blast_gu::Concat_lane>> lanes_;
  absl::node_hash_map<hhds::Pin_class, absl::flat_hash_set<int>>            active_;

public:
  template <class Read, class Zero, class Fail>
  Bit bit(const hhds::Pin_class& pin, int index, const Read& read, const Zero& zero, const Fail& fail) {
    namespace gu      = livehd::graph_util;
    const auto node   = pin.get_master_node();
    auto&      active = active_[pin];
    if (!active.insert(index).second) {
      fail("combinational wiring cycle");
      return zero();
    }
    const auto finish = [&](Bit value) {
      active.erase(index);
      return value;
    };
    if (gu::type_op_of(node) == Ntype_op::Set_mask) {
      auto [it, fresh] = aliases_.try_emplace(node);
      auto& alias      = it->second;
      if (fresh) {
        alias.base    = gu::get_driver_of_sink_name(node, "a");
        alias.value   = gu::get_driver_of_sink_name(node, "value");
        auto mask_pin = gu::get_driver_of_sink_name(node, "mask");
        if (!mask_pin.is_const() || gu::const_of(mask_pin).has_unknowns()) {
          fail("set_mask needs a known constant mask");
          return finish(zero());
        }
        // graph/cell.hpp: the mask is ONE window [lo, hi), or -1 == "replace
        // everything", i.e. the whole declared width. So there is exactly one
        // run and `value` is read from its bit 0 -- no per-bit scan.
        const auto& mask  = gu::const_of(mask_pin);
        const int   width = gu::bits_of(pin);
        int         lo = 0, hi = 0;
        if (gu::is_whole_value_mask(mask)) {
          hi = width;  // an unwidthed pin leaves the window empty, as before
        } else {
          const auto window = gu::mask_window_of(mask);
          if (!window) {
            fail("set_mask needs a contiguous constant mask");
            return finish(zero());
          }
          lo = window->first;
          hi = width > 0 ? std::min(window->second, width) : window->second;
        }
        if (hi > lo) {
          alias.runs.push_back({lo, hi, 0});
        }
      }
      if (fresh) {
        alias.valid = true;
      }
      if (!alias.valid) {
        fail("set_mask needs a known constant mask");
        return finish(zero());
      }
      for (const auto& run : alias.runs) {
        if (index >= run.lo && index < run.hi) {
          return finish(read(alias.value, run.base + index - run.lo));
        }
      }
      return finish(read(alias.base, index));
    }
    auto [it, fresh] = lanes_.try_emplace(node);
    if (fresh) {
      it->second = gu::concat_lanes(node);
    }
    const auto& lanes = it->second;
    if (fresh && !gu::concat_lane_violation(lanes).empty()) {
      it->second.clear();
    }
    if (lanes.empty()) {
      fail("malformed concat lane windows");
      return finish(zero());
    }
    for (const auto& lane : lanes) {
      if (index < lane.offset || index >= lane.offset + lane.width) {
        continue;
      }
      int b = index - lane.offset, real = std::max(1, gu::real_width(lane.value));
      return finish(lane.value.is_const() || b < real ? read(lane.value, b)
                    : gu::is_unsign(lane.value)       ? zero()
                                                      : read(lane.value, real - 1));
    }
    return finish(zero());
  }
};

template <class Ops, class ReadBit, class Slots, class Refuse, class RefuseShift>
void blast_comb(const hhds::Node_class& n, int out_bits, Slots& slots, Ops& ops, const ReadBit& abc_bit, const Blast_options& opts_,
                const livehd::partition::Region_body& rb, const absl::flat_hash_set<hhds::Node_class>& region, const Refuse& refuse,
                const RefuseShift& refuse_shift_amount) {
  namespace gu             = livehd::graph_util;
  using Bit                = decltype(ops.zero());
  const auto op            = gu::type_op_of(n);
  const auto out_pin       = n.create_driver_pin(0);
  const auto abc_const0    = [&] { return ops.zero(); };
  const auto abc_const1    = [&] { return ops.one(); };
  const auto abc_const_bit = [&](bool v) { return v ? ops.one() : ops.zero(); };
  const auto abc_not       = [&](Bit a) { return ops.inv(a); };
  const auto abc_bin       = [&](Bit a, Bit b, char kind) {
    return kind == '&' ? ops.and_(a, b) : kind == '|' ? ops.or_(a, b) : ops.xor_(a, b);
  };
  const auto abc_mux    = [&](Bit sel, Bit t, Bit f) { return ops.or_(ops.and_(sel, t), ops.and_(ops.inv(sel), f)); };
  const auto real_width = [](const hhds::Pin_class& p) { return std::max(1, gu::real_width(p)); };
  const auto eff_width  = [&](const hhds::Pin_class& p) {
    return p.is_const() ? std::max(1, static_cast<int>(gu::const_of(p).get_signed_bits())) : real_width(p);
  };
  const auto abc_eff_bit = [&](const hhds::Pin_class& p, int i) {
    if (p.is_const() || i < eff_width(p)) {
      return abc_bit(p, i);
    }
    return gu::is_unsign(p) ? ops.zero() : abc_bit(p, eff_width(p) - 1);
  };
  if (op == Ntype_op::Not) {
    hhds::Pin_class a;
    for (const auto& in_pin : n.inp_sorted_pins()) {
      const auto in_drv = in_pin.get_driver_pin();
      a                 = in_drv;
    }
    for (int b = 0; b < out_bits; ++b) {
      slots[b] = abc_not(abc_bit(a, b));
    }
  } else if (op == Ntype_op::Rxor || op == Ntype_op::Popcount) {
    const auto                    input = gu::get_driver_of_sink_name(n, "a");
    const int                     count = gu::reduction_count(n);
    std::vector<std::vector<Bit>> level;
    for (int bit = 0; bit < count; ++bit) {
      level.push_back({abc_eff_bit(input, bit)});
    }
    while (level.size() > 1) {
      std::vector<std::vector<Bit>> next;
      for (size_t i = 0; i < level.size(); i += 2) {
        if (i + 1 == level.size()) {
          next.push_back(std::move(level[i]));
          continue;
        }
        const auto& a = level[i];
        const auto& b = level[i + 1];
        if (op == Ntype_op::Rxor) {
          next.push_back({abc_bin(a[0], b[0], '^')});
          continue;
        }
        const int        width = std::min(out_bits, static_cast<int>(std::max(a.size(), b.size())) + 1);
        std::vector<Bit> sum;
        auto             carry = abc_const_bit(false);
        for (int bit = 0; bit < width; ++bit) {
          const auto x  = bit < static_cast<int>(a.size()) ? a[bit] : abc_const_bit(false);
          const auto y  = bit < static_cast<int>(b.size()) ? b[bit] : abc_const_bit(false);
          const auto xy = abc_bin(x, y, '^');
          sum.push_back(abc_bin(xy, carry, '^'));
          carry = abc_bin(abc_bin(x, y, '&'), abc_bin(xy, carry, '&'), '|');
        }
        next.push_back(std::move(sum));
      }
      level = std::move(next);
    }
    for (int bit = 0; bit < out_bits; ++bit) {
      slots[bit] = !level.empty() && bit < static_cast<int>(level[0].size()) ? level[0][bit] : abc_const_bit(false);
    }
  } else if (op == Ntype_op::Ror) {
    std::vector<Bit> level;
    for (const auto& in_pin : n.inp_sorted_pins()) {
      const auto in_drv = in_pin.get_driver_pin();
      // eff_width, not real_width: a constant operand has no `bits` attr and
      // would contribute only its bit 0 (`|{x, 8'h80}` mapped to `|x`).
      for (int b = 0; b < eff_width(in_drv); ++b) {
        level.push_back(abc_eff_bit(in_drv, b));
      }
    }
    // A balanced tree keeps a wide predicate from acquiring an artificial
    // serial dependency before mapping. The result is an unsigned bit.
    while (level.size() > 1) {
      std::vector<Bit> next;
      next.reserve((level.size() + 1) / 2);
      for (size_t i = 0; i < level.size(); i += 2) {
        next.push_back(i + 1 < level.size() ? abc_bin(level[i], level[i + 1], '|') : level[i]);
      }
      level = std::move(next);
    }
    for (int b = 0; b < out_bits; ++b) {
      slots[b] = b == 0 && !level.empty() ? level.front() : abc_const_bit(false);
    }
  } else if (op == Ntype_op::And || op == Ntype_op::Or || op == Ntype_op::Xor) {
    char                         kind = op == Ntype_op::And ? '&' : (op == Ntype_op::Or ? '|' : '^');
    std::vector<hhds::Pin_class> ins;
    for (const auto& in_pin : n.inp_sorted_pins()) {
      const auto in_drv = in_pin.get_driver_pin();
      ins.push_back(in_drv);
    }
    for (int b = 0; b < out_bits; ++b) {
      std::vector<Bit> level;
      level.reserve(ins.size());
      for (const auto& d : ins) {
        level.push_back(abc_bit(d, b));
      }
      // Preserve the associativity already expressed by the n-ary LGraph
      // cell. A left fold turns N parallel arbiter predicates into an
      // artificial N-deep critical path before ABC sees the network; a
      // pairwise reduction exposes the intended logarithmic structure.
      while (level.size() > 1) {
        std::vector<Bit> next;
        next.reserve((level.size() + 1) / 2);
        for (size_t i = 0; i < level.size(); i += 2) {
          next.push_back(i + 1 < level.size() ? abc_bin(level[i], level[i + 1], kind) : level[i]);
        }
        level = std::move(next);
      }
      slots[b] = level.empty() ? abc_const_bit(false) : level.front();
    }
  } else if (op == Ntype_op::Hotmux) {
    const auto       inputs = gu::hotmux_inputs(n);
    std::vector<Bit> controls;
    controls.reserve(inputs.arms.size());
    for (const auto& [control, value] : inputs.arms) {
      auto predicate = abc_const0();
      for (int bit = 0; bit < eff_width(control); ++bit) {
        predicate = abc_bin(predicate, abc_bit(control, bit), '|');
      }
      controls.push_back(predicate);
    }
    auto none = abc_const1();
    if (!inputs.fallback.is_invalid()) {
      // Build the default predicate only at mapping time, with balanced depth.
      std::vector<Bit> level;
      for (auto control : controls) {
        level.push_back(abc_not(control));
      }
      while (level.size() > 1) {
        std::vector<Bit> next;
        for (size_t i = 0; i < level.size(); i += 2) {
          next.push_back(i + 1 < level.size() ? abc_bin(level[i], level[i + 1], '&') : level[i]);
        }
        level = std::move(next);
      }
      none = level.empty() ? abc_const1() : level.front();
    }
    for (int b = 0; b < out_bits; ++b) {
      std::vector<Bit> products;
      for (size_t i = 0; i < inputs.arms.size(); ++i) {
        if (controls[i] != abc_const0()) {
          products.push_back(abc_bin(controls[i], abc_bit(inputs.arms[i].second, b), '&'));
        }
      }
      if (!inputs.fallback.is_invalid() && none != abc_const0()) {
        products.push_back(abc_bin(none, abc_bit(inputs.fallback, b), '&'));
      }
      while (products.size() > 1) {
        std::vector<Bit> next;
        for (size_t i = 0; i < products.size(); i += 2) {
          next.push_back(i + 1 < products.size() ? abc_bin(products[i], products[i + 1], '|') : products[i]);
        }
        products = std::move(next);
      }
      slots[b] = products.empty() ? abc_const0() : products.front();
    }
  } else if (op == Ntype_op::Mux) {
    hhds::Pin_class                       sel;
    absl::btree_map<int, hhds::Pin_class> data;  // pid-1 (value) -> driver; ordered so the OR-tree fed to ABC is deterministic
    int                                   max_v = -1;
    for (const auto& in_pin : n.inp_sorted_pins()) {
      const auto in_drv = in_pin.get_driver_pin();
      auto       pid    = in_pin.get_port_id();
      if (pid == 0) {
        sel = in_drv;
      } else {
        data[static_cast<int>(pid) - 1] = in_drv;
        max_v                           = std::max(max_v, static_cast<int>(pid) - 1);
      }
    }
    int sel_bits = gu::bits_of(sel);
    if (sel_bits == 0) {
      sel_bits = 1;
    }
    // A two-arm Mux is a condition, as in cgen's `if (sel)`: a wide
    // predicate such as `a & 0x80` selects the true arm when ANY bit is set.
    // Comparing that selector to the integer 1 drops both arms for 0x80.
    std::optional<Bit> condition;
    if (op == Ntype_op::Mux && data.size() == 2 && data.contains(0) && data.contains(1)) {
      if (sel.is_const() && !gu::const_of(sel).has_unknowns()) {
        condition = abc_const_bit(!gu::const_of(sel).is_known_zero());
      } else {
        std::vector<Bit> level;
        for (int sb = 0; sb < sel_bits; ++sb) {
          level.push_back(abc_bit(sel, sb));
        }
        while (level.size() > 1) {
          std::vector<Bit> next;
          for (size_t i = 0; i < level.size(); i += 2) {
            next.push_back(i + 1 < level.size() ? abc_bin(level[i], level[i + 1], '|') : level[i]);
          }
          level = std::move(next);
        }
        condition = level.front();
      }
    }
    for (int b = 0; b < out_bits; ++b) {
      if (condition) {
        // Do not demand an unreachable arm: it may be a syntactic self-hold.
        slots[b] = *condition == abc_const0()   ? abc_bit(data.at(0), b)
                   : *condition == abc_const1() ? abc_bit(data.at(1), b)
                                                : abc_mux(*condition, abc_bit(data.at(1), b), abc_bit(data.at(0), b));
        continue;
      }
      std::vector<Bit> products;
      products.reserve(data.size());
      for (const auto& [v, drv] : data) {
        Bit              hit{};  // selector matches value v
        std::vector<Bit> literals;
        literals.reserve(sel_bits);
        for (int sb = 0; sb < sel_bits; ++sb) {
          Bit sbit = abc_bit(sel, sb);
          Bit lit  = ((v >> sb) & 1) ? sbit : abc_not(sbit);
          literals.push_back(lit);
        }
        while (literals.size() > 1) {
          std::vector<Bit> next;
          next.reserve((literals.size() + 1) / 2);
          for (size_t i = 0; i < literals.size(); i += 2) {
            next.push_back(i + 1 < literals.size() ? abc_bin(literals[i], literals[i + 1], '&') : literals[i]);
          }
          literals = std::move(next);
        }
        hit = literals.empty() ? abc_const_bit(true) : literals.front();
        // A constant-only selector can guard a syntactic self-reference
        // (generated RTL uses this for an invalid/X arm).  Form the guard
        // first: an unreachable arm must not recursively demand its data.
        if (hit == abc_const0()) {
          continue;
        }
        Bit term = abc_bit(drv, b);  // data_v[b]
        Bit prod = abc_bin(term, hit, '&');
        products.push_back(prod);
      }
      // Like n-ary logic above, keep the mux cover logarithmic. A serial
      // product-term OR chain made selection logic scale linearly with the
      // number of arms before ABC could optimize it.
      while (products.size() > 1) {
        std::vector<Bit> next;
        next.reserve((products.size() + 1) / 2);
        for (size_t i = 0; i < products.size(); i += 2) {
          next.push_back(i + 1 < products.size() ? abc_bin(products[i], products[i + 1], '|') : products[i]);
        }
        products = std::move(next);
      }
      slots[b] = products.empty() ? abc_const_bit(false) : products.front();
    }
  } else if (op == Ntype_op::Get_mask) {
    // out[j] = a[positions[j]] where positions = mask-selected source bits.
    auto a_drv = gu::get_driver_of_sink_name(n, "a");
    auto m_drv = gu::get_driver_of_sink_name(n, "mask");
    if (!m_drv.is_const()) {
      refuse(n,
             "unsupported-cell",
             "unsupported",
             "get_mask has a non-constant mask, which cannot be technology-mapped",
             {},
             m_drv,
             "mask driven here");
    } else {
      // graph/cell.hpp: the mask is ONE window [lo, hi), or -1 == the whole
      // source. Either way the selected positions are CONSECUTIVE, so output
      // bit j reads source bit lo + j -- no per-bit scan and no position list.
      const auto& mask   = gu::const_of(m_drv);
      int         a_bits = gu::bits_of(a_drv);
      if (a_bits == 0 && a_drv.is_const()) {
        // A CONSTANT driver carries no `bits` attr, so bits_of is 0 (see
        // eff_width above — create_const stamps only the value, never a width).
        // The zero-extend idiom Get_mask(a, -1) selects EVERY source position,
        // and that window is bounded by a_bits: left at 0 it would write const0
        // into every output bit, silently replacing the literal with 0. Note
        // abc_bit is never reached, so its unmaterialized-driver diagnostic
        // cannot warn. Size the literal from its VALUE, exactly as eff_width does.
        a_bits = std::max(1, static_cast<int>(gu::const_of(a_drv).get_signed_bits()));
      }
      const auto window = gu::is_whole_value_mask(mask) ? std::optional<std::pair<int, int>>{{0, a_bits}}
                                                        : gu::mask_window_of(mask);
      if (!window) {
        // REFUSE rather than abort: this is a mapping pass, and its answer to a
        // cell it cannot express is a diagnostic naming the pin, not a crash.
        refuse(n,
               "unsupported-cell",
               "unsupported",
               "get_mask has a mask that is neither a contiguous window nor -1, which cannot be technology-mapped",
               {},
               m_drv,
               "mask driven here");
      } else {
        const auto [lo, hi] = *window;
        const int span      = hi - lo;
        for (int b = 0; b < out_bits; ++b) {
          slots[b] = b < span ? abc_bit(a_drv, lo + b) : abc_const_bit(false);
        }
      }
    }
  } else if (op == Ntype_op::Set_mask) {
    // Pure wiring, resolved lazily by abc_bit above. Do not materialize every
    // bit of a wide sparse-update bus here.
    auto m_drv = gu::get_driver_of_sink_name(n, "mask");
    if (!m_drv.is_const()) {
      refuse(n,
             "unsupported-cell",
             "unsupported",
             "set_mask has a non-constant mask, which cannot be technology-mapped",
             {},
             m_drv,
             "mask driven here");
    }
  } else if (op == Ntype_op::Sext) {
    // The Sext cell's `b` is the KEPT BIT COUNT, so the sign bit sits at
    // `keep-1` -- the same convention inou/cgen emits (`$signed(a[keep-1:0])`)
    // and pass/lec/encode proves against. out[i] = a[min(i, keep-1)].
    auto a_drv = gu::get_driver_of_sink_name(n, "a");
    auto b_drv = gu::get_driver_of_sink_name(n, "b");
    if (!b_drv.is_const()) {
      refuse(n,
             "unsupported-cell",
             "unsupported",
             "sext has a non-constant bit position, which cannot be technology-mapped",
             {},
             b_drv,
             "bit position driven here");
    } else if (int keep = static_cast<int>(gu::const_of(b_drv).to_just_i64()); keep < 1) {
      refuse(n,
             "unsupported-cell",
             "unsupported",
             "sext keeps a non-positive number of bits, which has no gate-level meaning",
             {},
             b_drv,
             "kept bit count driven here");
    } else {
      const int sign_bit = keep - 1;
      for (int b = 0; b < out_bits; ++b) {
        slots[b] = abc_bit(a_drv, std::min(b, sign_bit));
      }
    }
  } else if (op == Ntype_op::Sum) {
    std::vector<arith::Sum_operand<Bit>> operands;
    for (const auto& in_pin : n.inp_sorted_pins()) {
      const auto              in_drv = in_pin.get_driver_pin();
      // Every operand has its OWN sink pid; the add/subtract bank is the pid's
      // PARITY (Ntype::sink_bank, graph/cell.hpp).
      arith::Sum_operand<Bit> operand;
      operand.is_signed = !gu::is_unsign(in_drv);
      operand.subtract  = Ntype::sink_bank(op, in_pin.get_port_id()) == 1;
      const int width   = in_drv.is_const() ? out_bits : std::min(real_width(in_drv), out_bits);
      operand.bits.reserve(width);
      for (int i = 0; i < width; ++i) {
        operand.bits.push_back(abc_bit(in_drv, i));
      }
      operands.push_back(std::move(operand));
    }
    const int bs  = opts_.block_size > 0 ? opts_.block_size : arith::default_block_size(out_bits);
    auto      acc = arith::build_sum(opts_.adder, bs, ops, operands, out_bits);
    for (int b = 0; b < out_bits; ++b) {
      slots[b] = acc[b];
    }
  } else if (op == Ntype_op::LT || op == Ntype_op::GT) {
    // Every cross-bank pair must satisfy the relation. Comparing just the
    // last driver of each bank makes the result depend on operand order.
    std::vector<hhds::Pin_class> lhs, rhs;
    for (const auto& sink : n.inp_sorted_pins()) {
      (Ntype::sink_bank(op, sink.get_port_id()) == 0 ? lhs : rhs).push_back(sink.get_driver_pin());
    }
    if (lhs.empty() || rhs.empty()) {
      refuse(n, "invalid-compare", "unsupported", "LT/GT requires an operand in each bank", "check the comparison's input banks");
      return;
    }
    const auto unsigned_value
        = [&](const hhds::Pin_class& pin) { return pin.is_const() ? !gu::const_of(pin).is_negative() : gu::is_unsign(pin); };
    Bit result = abc_const1();
    for (auto a : lhs) {
      for (auto b : rhs) {
        const bool       a_unsigned = unsigned_value(a), b_unsigned = unsigned_value(b);
        const bool       uns = a_unsigned && b_unsigned;
        // The signed builder reads the subtraction's sign bit. First make
        // unsigned magnitudes positive signed values, then reserve a guard
        // bit for subtraction: s2(-2) - u2(3) needs four signed bits, not three.
        const int        w   = uns ? std::max(eff_width(a), eff_width(b))
                                   : std::max(eff_width(a) + int(a_unsigned), eff_width(b) + int(b_unsigned)) + 1;
        const int        bs  = opts_.block_size > 0 ? opts_.block_size : arith::default_block_size(w);
        std::vector<Bit> av(w), bv(w);
        for (int i = 0; i < w; ++i) {
          av[i] = abc_eff_bit(a, i);
          bv[i] = abc_eff_bit(b, i);
        }
        auto pair = op == Ntype_op::LT ? arith::build_lt(opts_.adder, bs, ops, av, bv, uns)
                                       : arith::build_lt(opts_.adder, bs, ops, bv, av, uns);
        result    = ops.and_(result, pair);
      }
    }
    slots[0] = result;
    for (int b = 1; b < out_bits; ++b) {
      slots[b] = abc_const0();
    }
  } else if (op == Ntype_op::EQ) {
    // 1-bit result; n-ary all-equal (operands on pid 0). Compare at
    // max(width)+1 so sign-extension differences are caught.
    std::vector<hhds::Pin_class> ds;
    for (const auto& in_pin : n.inp_sorted_pins()) {
      const auto in_drv = in_pin.get_driver_pin();
      ds.push_back(in_drv);
    }
    if (ds.size() <= 1) {
      slots[0] = abc_const_bit(true);
    } else {
      // A constant operand usually carries NO bits attribute (bits_of == 0):
      // size it from its VALUE (eff_width), or an all-constant compare -- the
      // shape mem_lower builds for a constant-address port -- degenerates to
      // a 1-bit (parity) compare and every EQ against a constant wider than
      // its other operand only checks the low bits (x[3:0] == 8'd100 mapped
      // to x == 4). Soundness fix, LEC-verified on the multi-write tile.
      int w = 0;
      for (const auto& d : ds) {
        w = std::max(w, d.is_const() ? eff_width(d) : gu::bits_of(d));
      }
      ++w;
      std::vector<std::vector<Bit>> operands(ds.size());
      for (size_t k = 0; k < ds.size(); ++k) {
        operands[k].resize(w);
        for (int i = 0; i < w; ++i) {
          operands[k][i] = abc_bit(ds[k], i);
        }
      }
      slots[0] = arith::build_eq(ops, operands);
    }
    for (int b = 1; b < out_bits; ++b) {
      slots[b] = abc_const_bit(false);
    }
  } else if (op == Ntype_op::SHL) {
    // Logical left shift, in a single combinational cone. pid 0 = value `a`,
    // pid 1 = shift amount `b` (both single-driver; the old one-hot multi-shift
    // `n<<(b0,b1,…)` form was removed). The cvc5 LEC encodes SHL identically
    // (fit `a` to the result width, shift unsigned). A CONSTANT amount becomes
    // pure bit re-wiring; a RUNTIME amount becomes a barrel/log shifter
    // (arith::build_shl). `a` is sign/zero extended to out_bits by abc_bit,
    // matching the LEC's fit-to-W.
    hhds::Pin_class a_d;
    hhds::Pin_class b_d;
    for (const auto& in_pin : n.inp_sorted_pins()) {
      const auto in_drv = in_pin.get_driver_pin();
      if (in_pin.get_port_id() == 0) {
        a_d = in_drv;
      } else if (in_pin.get_port_id() == 1) {
        b_d = in_drv;
      }
    }
    std::vector<Bit> av(out_bits);
    for (int i = 0; i < out_bits; ++i) {
      av[i] = abc_bit(a_d, i);
    }
    std::vector<Bit> sh;  // empty => no amount (result == a)
    if (!b_d.is_invalid()) {
      if (b_d.is_const()) {
        const auto& amt_c = gu::const_of(b_d);
        if (amt_c.has_unknowns() || amt_c.is_negative()) {
          // Unknown and negative are DIFFERENT failures and get different
          // reports. An unknown (`?`) amount is a value that was never given
          // a definite assignment -- ABC has no X, so the shift cannot be
          // mapped. A NEGATIVE amount is a livehd bug: no hardware shift
          // takes one, and upass.bitwidth rejects it upstream, so one
          // reaching synthesis means an earlier pass folded/lowered it wrong.
          // Neither case touches a RUNTIME amount, which is fully supported
          // (it becomes the barrel shifter built below).
          refuse_shift_amount(n, "shl", amt_c, b_d);
        } else {
          // Clean non-negative integer: out[i] = a[i-amt], 0 below. A value too
          // big for i64 (or simply >= out_bits) shifts everything out -> 0.
          int64_t amt = amt_c.is_just_i64() ? amt_c.to_just_i64() : static_cast<int64_t>(out_bits);
          sh.resize(out_bits);
          for (int i = 0; i < out_bits; ++i) {
            sh[i] = (i - amt >= 0) ? av[static_cast<int>(i - amt)] : abc_const_bit(false);
          }
        }
      } else {
        int bw = gu::bits_of(b_d);
        if (bw <= 0) {
          bw = 1;
        }
        std::vector<Bit> bv(bw);
        for (int i = 0; i < bw; ++i) {
          bv[i] = abc_bit(b_d, i);  // unsigned shift count
        }
        sh = arith::build_shl(ops, av, bv, out_bits, opts_.reverse_barrel);
      }
    }
    for (int b = 0; b < out_bits; ++b) {
      slots[b] = sh.empty() ? av[b] : sh[b];
    }
  } else if (op == Ntype_op::SRA) {
    // Right shift: pid 0 = value `a` (single), pid 1 = shift amount `b`
    // (single). Arithmetic (sign-replicating) when `a` is signed, logical
    // otherwise — mirroring Verilog `>>>` and the cvc5 LEC (BITVECTOR_ASHR vs
    // BITVECTOR_LSHR). A right shift pulls bits DOWN from higher positions, so
    // the value must be at its FULL width before shifting. Keep every amount
    // bit: the count is self-determined, and high bits mean overshift rather
    // than wrapping back to a small count. Unlike the SMT encoder, the bit
    // builder can use different data/count widths, so only `a` is extended to
    // cw = max(operand_width, output_width), and
    // the low out_bits become the result. A CONSTANT amount becomes pure bit
    // re-wiring; a RUNTIME amount a combinational barrel shifter (build_shr).
    hhds::Pin_class a_d;
    hhds::Pin_class b_d;
    for (const auto& in_pin : n.inp_sorted_pins()) {
      const auto in_drv = in_pin.get_driver_pin();
      if (in_pin.get_port_id() == 0) {
        if (a_d.is_invalid()) {
          a_d = in_drv;
        }
      } else if (in_pin.get_port_id() == 1) {
        if (b_d.is_invalid()) {
          b_d = in_drv;  // first amount driver, matching the LEC's pid(1)[0]
        }
      }
    }
    bool a_sign          = !gu::is_unsign(a_d);
    int  a_width         = eff_width(a_d);       // operand width as the LEC reads it (port=bits_of, internal=real_width)
    int  out_w           = real_width(out_pin);  // result magnitude width (LEC W)
    int  cw              = std::max(a_width, std::max(out_w, 1));  // shift at the wider of the two
    int  demand_w        = out_w;
    bool sliced_demand   = false;
    bool boundary_output = false;
    // A constant Get_mask is pure wiring. If every in-region consumer only
    // selects a narrow prefix of this shift, build just the barrel-shifter
    // window that can affect those selected bits. A region output or any
    // other consumer conservatively demands the full result.
    for (const auto& port : rb.outputs) {
      if (port.src_driver == out_pin) {
        boundary_output = true;
        break;
      }
    }
    if (!boundary_output) {
      int  selected_hi = 0;
      bool all_slices  = true;
      bool saw_use     = false;
      for (const auto& e : out_pin.out_edges()) {
        auto sink_node = e.sink.get_master_node();
        if (!region.contains(sink_node) || gu::type_op_of(sink_node) != Ntype_op::Get_mask) {
          all_slices = false;
          break;
        }
        auto mask_drv = gu::get_driver_of_sink_name(sink_node, "mask");
        if (!mask_drv.is_const()) {
          all_slices = false;
          break;
        }
        const auto& mask = gu::const_of(mask_drv);
        if (mask.is_negative()) {
          all_slices = false;
          break;
        }
        const int wanted = std::max(0, real_width(sink_node.create_driver_pin(0)));
        int       found  = 0;
        int       hi     = 0;
        for (int bit = 0; bit < static_cast<int>(mask.get_signed_bits()) && found < wanted; ++bit) {
          if (mask.bit_test(bit)) {
            hi = bit + 1;
            ++found;
          }
        }
        selected_hi = std::max(selected_hi, hi);
        saw_use     = true;
      }
      if (all_slices && saw_use && selected_hi > 0 && selected_hi < out_w) {
        demand_w      = selected_hi;
        sliced_demand = true;
      }
    }
    if (sliced_demand && opts_.verbose) {
      std::print("[pass.abc] region '{}': right-shift demand reduced from {} to {} bits\n", rb.module_name, out_w, demand_w);
    }
    std::vector<Bit> av(cw);
    for (int i = 0; i < cw; ++i) {
      av[i] = abc_eff_bit(a_d, i);  // a, sign/zero-extended (past its effective width) to the shift width cw
    }
    Bit              fill = a_sign ? av[cw - 1] : abc_const_bit(false);  // sign bit (arith) or 0 (logical)
    std::vector<Bit> res;                                                // cw-wide shifted value
    if (b_d.is_const()) {
      const auto& amt_c = gu::const_of(b_d);
      if (amt_c.has_unknowns() || amt_c.is_negative()) {
        refuse_shift_amount(n, "sra", amt_c, b_d);  // see the SHL arm for why the two cases are reported apart
      } else {
        int64_t amt = amt_c.is_just_i64() ? amt_c.to_just_i64() : static_cast<int64_t>(cw);
        res.resize(demand_w);
        for (int i = 0; i < demand_w; ++i) {
          res[i] = (amt < cw && i + amt < cw) ? av[static_cast<int>(i + amt)] : fill;  // amt >= cw => all fill
        }
      }
    } else {
      int              nb = eff_width(b_d);  // all self-determined amount bits participate in overshift detection
      std::vector<Bit> bv(nb);
      for (int i = 0; i < nb; ++i) {
        bv[i] = abc_bit(b_d, i);  // unsigned shift count (i < eff width, so the real bit)
      }
      // Recognize amount = f(index) for an affine index*scale + bias chain.
      // This is the canonical packed dynamic word-select lowering. With a
      // narrow demanded prefix, select directly among source words rather
      // than building a full-width barrel.
      //
      // The affine form needs a NARROW demand, not specifically a sliced one: a
      // shift whose own result is already narrower than its operand (a ware
      // module narrows its output port to the demanded width, so its consumer
      // is a module port, never an in-region Get_mask) is the same word select.
      const bool narrow_demand = sliced_demand || demand_w < cw;
      hhds::Pin_class       index;
      std::vector<uint64_t> amounts;  // the amount net's value for each index value
      if (narrow_demand) {
        // index*scale + bias in any spelling (affine_amount.hpp), every link
        // in this region and the index read as a non-negative value.
        if (const auto chain = affine_chain(b_d); chain && gu::is_unsign(chain->index)) {
          const int iw    = eff_width(chain->index);
          bool      valid = iw > 0 && iw <= 16
                       && (uint64_t{1} << iw) * static_cast<uint64_t>(demand_w)
                              < static_cast<uint64_t>(cw) * static_cast<uint64_t>(std::max(nb, 1));
          for (size_t i = 0; valid && i < chain->links.size(); ++i) {
            valid = region.contains(chain->links[i].first);
          }
          // The two lowerings must agree, because only a COST heuristic picks
          // between them. The generic barrel reads the amount as the nb-bit
          // net it is, so each index value's table entry is the EXACT value
          // of that net: every link computed and truncated to its own width
          // (a wrapping link, like satopt's odc narrowing of the amount, stays
          // a wrap), then the low nb bits the barrel reads.
          for (uint64_t v = 0; valid && v < (uint64_t{1} << iw); ++v) {
            const auto amt = chain->eval(v);
            valid          = amt.has_value();
            if (valid) {
              amounts.push_back(nb < 63 ? (*amt & ((uint64_t{1} << nb) - 1)) : *amt);
            }
          }
          if (valid) {
            index = chain->index;
          } else {
            amounts.clear();
          }
        }
      }
      if (!amounts.empty()) {
        const int        index_w = eff_width(index);
        std::vector<Bit> iv(index_w);
        for (int i = 0; i < index_w; ++i) {
          iv[i] = abc_bit(index, i);
        }
        res = arith::build_table_shr_prefix(ops, av, iv, fill, amounts, demand_w);
        if (opts_.verbose) {
          std::print("[pass.abc] region '{}': affine right shift selected ({} output bits, {}-bit index)\n",
                     rb.module_name,
                     demand_w,
                     index_w);
        }
      } else {
        res = arith::build_shr_prefix(ops, av, bv, fill, demand_w, opts_.reverse_barrel);
      }
    }
    // result = low out_w bits of the cw-wide shift. The bit(s) above the
    // magnitude width follow the RESULT's sign, which Verilog takes from the
    // LEFT operand (the amount never counts): the LEC's SRA arm carries
    // `out_signed |= a.is_signed` and sign-extends the W-bit result into a
    // wider consumer/port, and cgen emits `$signed(a) >>> n`, which
    // sign-fills. tolg's bind_result stamps the pin unsigned even for an
    // arithmetic shift, so the "spare" slot is NOT always 0 -- padding it
    // with const0 zero-extended a negative result (abc_mathops __c5: c = -8,
    // n = 0 read 8 instead of 24 on the 5-bit region boundary). Replicate
    // the top kept bit for a signed operand; a logical shift still pads 0.
    Bit pad = abc_const_bit(false);
    if (a_sign && out_w > 0 && out_w <= static_cast<int>(res.size())) {
      pad = res[out_w - 1];
    }
    for (int b = 0; b < out_bits; ++b) {
      slots[b] = (b < out_w && b < static_cast<int>(res.size())) ? res[b] : pad;
    }
  } else if (op == Ntype_op::Div) {
    const auto       a     = gu::get_driver_of_sink_name(n, "a");
    const auto       b     = gu::get_driver_of_sink_name(n, "b");
    // eff_width, NOT real_width: a constant driver carries no `bits` attr, so
    // real_width clamps it to 1 and the `i < width` loop below would silently
    // drop its high bits (`x / 300` mapped as `x / (300 & 0xF)` = `x / 12`).
    // Same soundness fix the EQ/LT arms already carry.
    const int        width = std::max({eff_width(a), eff_width(b), real_width(out_pin), 1});
    const int        bs    = opts_.block_size > 0 ? opts_.block_size : arith::default_block_size(width);
    std::vector<Bit> av(width), bv(width);
    for (int i = 0; i < width; ++i) {
      av[i] = abc_eff_bit(a, i);
      bv[i] = abc_eff_bit(b, i);
    }
    // A constant pin also carries no `pin_signed` attr, so `is_unsign` always
    // says "unsigned" for it -- while abc_bit reads a NEGATIVE literal in
    // two's complement (sign-extending it). Handing build_div a two's-complement
    // -2 with b_signed=false divides by 2^w-2 and returns 0 for every input.
    // Read a negative literal as signed, the same idiom Cgen_verilog's
    // operand_reads_signed uses.
    const auto operand_signed = [&](const hhds::Pin_class& d) {
      if (d.is_const()) {
        const auto& c = gu::const_of(d);
        return !c.has_unknowns() && c.is_negative();
      }
      return !gu::is_unsign(d);
    };
    const auto quotient = arith::build_div(opts_.adder, bs, ops, av, bv, operand_signed(a), operand_signed(b));
    for (int i = 0; i < out_bits; ++i) {
      slots[i] = i < width ? quotient[i] : (gu::is_unsign(out_pin) ? abc_const_bit(false) : quotient.back());
    }
  } else if (op == Ntype_op::Mult) {
    // n-ary product of every input driver (all on pid 0), at width out_bits
    // (the bitwidth-resolved result width). Each operand is sign/zero-extended
    // to out_bits by abc_bit and the running product is kept mod 2^out_bits, so
    // the low out_bits are correct for signed and unsigned operands alike —
    // matching the LEC (fit each operand to W, then BITVECTOR_MULT). A simple
    // single-cycle array multiplier (build_mul) reuses the selected adder for
    // partial-product accumulation. An empty product is 1 (LEC convention).
    std::vector<hhds::Pin_class> ds;
    for (const auto& in_pin : n.inp_sorted_pins()) {
      const auto in_drv = in_pin.get_driver_pin();
      ds.push_back(in_drv);
    }
    int  out_w  = real_width(out_pin);  // result magnitude width (LEC W); product is mod 2^out_w
    int  bs     = opts_.block_size > 0 ? opts_.block_size : arith::default_block_size(out_w);
    auto extend = [&](const hhds::Pin_class& d) {
      std::vector<Bit> v(out_w);
      for (int i = 0; i < out_w; ++i) {
        v[i] = abc_eff_bit(d, i);  // operand fit to out_w at its effective width (no stray internal spare bit)
      }
      return v;
    };
    std::vector<Bit> acc;
    if (ds.empty()) {
      acc.assign(out_w, abc_const_bit(false));
      if (out_w > 0) {
        acc[0] = abc_const_bit(true);  // empty product == 1
      }
    } else {
      acc = extend(ds[0]);
      for (size_t k = 1; k < ds.size(); ++k) {
        acc = arith::build_mul(opts_.multiplier, opts_.adder, bs, ops, acc, extend(ds[k]), out_w);
      }
    }
    // low out_w bits are the product; the spare bit(s) above the magnitude
    // width are 0 (an unsigned product is non-negative; a signed product has
    // out_w == bits_of so there are no spare bits to fill).
    for (int b = 0; b < out_bits; ++b) {
      slots[b] = (b < out_w && b < static_cast<int>(acc.size())) ? acc[b] : abc_const_bit(false);
    }
  } else {
    refuse(n,
           "unsupported-cell",
           "unsupported",
           std::format("cell '{}' has no combinational bit-blast yet", Ntype::get_name(op)),
           "supported: and/or/xor/ror/not/mux/hotmux/sum/mult/div/lt/gt/eq/get_mask/set_mask/sext/shl/sra/const; concat is pure "
           "wiring, resolved per demanded bit");
  }
}
}  // namespace livehd::synth
