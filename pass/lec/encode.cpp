// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "encode.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "cell.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"

namespace livehd::lec {

using namespace smt;
namespace gu = livehd::graph_util;

// Real bus width of a pin: bits attribute is the signed magnitude+1 count;
// an unsigned pin drops the spare sign bit (see lec.md "Bit-width trap").
int real_width(const hhds::Pin_class& pin) {
  int b = gu::bits_of(pin);
  if (b == 0) {
    return 0;
  }
  return gu::is_unsign(pin) ? b - 1 : b;
}

int real_width_io(const hhds::Pin_class& pin, const hhds::GraphIO& gio, std::string_view name) {
  int b = gu::bits_of(pin, gio, name);
  if (b == 0) {
    return 0;
  }
  bool uns = pin.is_invalid() ? gio.is_unsign(name) : gu::is_unsign(pin);
  return uns ? b - 1 : b;
}

// Extend / truncate a value to exactly `width` bits.
smt::Term fit_to(const smt::SmtSolver& solver, const Val& v, int width) {
  if (v.width == width) {
    return v.term;
  }
  if (width < v.width) {
    return solver->make_term(Op(Extract, width - 1, 0), v.term);  // truncate low bits
  }
  int d = width - v.width;
  return solver->make_term(Op(v.is_signed ? Sign_Extend : Zero_Extend, d), v.term);
}

Sort Encoder::bv(int width) { return solver_->make_sort(BV, width < 1 ? 1 : width); }

smt::Term Encoder::fit(const Val& v, int width) { return fit_to(solver_, v, width); }

Encoded Encoder::encode(hhds::Graph* g, const absl::flat_hash_map<std::string, Val>* shared_inputs, std::string_view prefix) {
  Encoded out;
  auto    gio = g->get_io();

  // driver-pin Class_index -> Val (SSA value table)
  absl::flat_hash_map<hhds::Class_index, Val> pin2val;

  auto fail = [&](const std::string& msg) -> Encoded& {
    if (out.ok) {
      out.ok    = false;
      out.error = msg;
    }
    return out;
  };

  // bit-vector literal of `width` bits from a constant pin's Dlop.
  auto const_val = [&](const hhds::Pin_class& dpin) -> Val {
    Dlop c     = gu::hydrate_const(dpin);
    int  width = std::max(1, c.get_bits());
    bool sgn   = c.is_negative();
    Term t;
    if (c.is_just_i64()) {
      t = solver_->make_term(c.to_just_i64(), bv(width));
    } else {
      // Wide constant: feed the decimal string (handles arbitrary precision).
      t = solver_->make_term(c.to_decimal_string(), bv(width), 10);
    }
    return Val{t, width, sgn};
  };

  // Resolve a driver pin to its Val (constant literal or a computed SSA value).
  auto driver_val = [&](const hhds::Pin_class& dpin, bool& ok) -> Val {
    ok = true;
    if (gu::is_const_pin(dpin)) {
      return const_val(dpin);
    }
    auto it = pin2val.find(dpin.get_class_index());
    if (it == pin2val.end()) {
      ok = false;
      return {};
    }
    return it->second;
  };

  // 1-bit BV from a Bool predicate.
  auto pred_to_bv = [&](const Term& b) -> Term {
    return solver_->make_term(Ite, b, solver_->make_term(1, bv(1)), solver_->make_term(0, bv(1)));
  };

  // ---- Inputs: shared symbol or a fresh one, mapped onto the input driver pin.
  for (const auto& d : gio->get_input_pin_decls()) {
    auto dpin = g->get_input_pin(d.name);
    int  w    = real_width_io(dpin, *gio, d.name);
    if (w == 0) {
      return fail("input '" + d.name + "' has unknown bit width");
    }
    bool sgn = dpin.is_invalid() ? !gio->is_unsign(d.name) : !gu::is_unsign(dpin);
    Val  v;
    if (shared_inputs != nullptr) {
      auto it = shared_inputs->find(d.name);
      if (it != shared_inputs->end()) {
        v = it->second;
        if (v.width != w) {
          v.term  = fit(v, w);  // reconcile declared width across the two designs
          v.width = w;
        }
      }
    }
    if (v.term == nullptr) {
      v = Val{solver_->make_symbol(std::string(prefix) + d.name, bv(w)), w, sgn};
    }
    out.inputs[d.name] = v;
    if (!dpin.is_invalid()) {
      pin2val[dpin.get_class_index()] = v;
    }
  }

  // ---- Combinational nodes in topological order.
  for (auto node : g->forward_class()) {
    auto op = gu::type_op_of(node);

    // Skip nodes with no consumers (cgen does the same).
    if (!node.has_out_edges()) {
      continue;
    }
    // Constants are resolved on demand at use sites.
    if (op == Ntype_op::Nconst) {
      continue;
    }
    // Sequential / structural elements: out of scope for the M1 comb slice.
    if (op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch || op == Ntype_op::Memory || op == Ntype_op::Sub) {
      return fail("sequential/structural op '" + std::string(Ntype::get_name(op)) + "' not supported (M1 is combinational-only)");
    }

    auto dpin = node.get_driver_pin(0);
    int  W    = real_width(dpin);
    if (W == 0) {
      // Comparisons/reductions are 1-bit; default unknown width to that.
      W = 1;
    }
    bool out_signed = !gu::is_unsign(dpin);

    // Bucket input edges by sink port id, resolving every driver to a Val.
    absl::flat_hash_map<hhds::Port_id, std::vector<Val>> by_pid;
    std::vector<Val>                                     all;  // in edge order
    bool                                                 ok = true;
    for (const auto& e : node.inp_edges()) {
      Val v = driver_val(e.driver, ok);
      if (!ok) {
        return fail("operand of '" + gu::debug_name(node) + "' has no encodable driver");
      }
      by_pid[e.sink.get_port_id()].push_back(v);
      all.push_back(v);
    }

    auto pid = [&](hhds::Port_id p) -> std::vector<Val>& {
      static std::vector<Val> empty;
      auto                    it = by_pid.find(p);
      return it == by_pid.end() ? empty : it->second;
    };

    Term result;

    switch (op) {
      case Ntype_op::And:
      case Ntype_op::Or:
      case Ntype_op::Xor: {
        PrimOp pop = (op == Ntype_op::And) ? BVAnd : (op == Ntype_op::Or) ? BVOr : BVXor;
        for (const auto& v : all) {
          Term t = fit(v, W);
          result = result == nullptr ? t : solver_->make_term(pop, result, t);
        }
        break;
      }
      case Ntype_op::Mult: {
        for (const auto& v : all) {
          Term t = fit(v, W);
          result = result == nullptr ? t : solver_->make_term(BVMul, result, t);
        }
        break;
      }
      case Ntype_op::Sum: {
        Term add_acc;
        for (const auto& v : pid(0)) {  // "a" pins: added
          Term t  = fit(v, W);
          add_acc = add_acc == nullptr ? t : solver_->make_term(BVAdd, add_acc, t);
        }
        if (add_acc == nullptr) {
          add_acc = solver_->make_term(0, bv(W));
        }
        for (const auto& v : pid(1)) {  // "b" pins: subtracted
          add_acc = solver_->make_term(BVSub, add_acc, fit(v, W));
        }
        result = add_acc;
        break;
      }
      case Ntype_op::Div: {
        if (pid(0).size() != 1 || pid(1).size() != 1) {
          return fail("Div expects single a/b drivers");
        }
        Term a = fit(pid(0)[0], W);
        Term b = fit(pid(1)[0], W);
        result = solver_->make_term(out_signed ? BVSdiv : BVUdiv, a, b);
        break;
      }
      case Ntype_op::Not: {
        if (all.empty()) {
          return fail("Not has no operand");
        }
        result = solver_->make_term(BVNot, fit(all[0], W));
        break;
      }
      case Ntype_op::Ror: {
        // reduction-OR: 1 iff any input bit set.
        Term concat;
        for (const auto& v : all) {
          concat = concat == nullptr ? v.term : solver_->make_term(Concat, concat, v.term);
        }
        if (concat == nullptr) {
          return fail("Ror has no operand");
        }
        int cw = 0;
        for (const auto& v : all) {
          cw += v.width;
        }
        result = pred_to_bv(solver_->make_term(Distinct, concat, solver_->make_term(0, bv(cw))));
        break;
      }
      case Ntype_op::EQ: {
        if (all.size() < 2) {
          return fail("EQ expects >= 2 operands");
        }
        int cw = 0;
        for (const auto& v : all) {
          cw = std::max(cw, v.width);
        }
        Term acc;
        for (size_t i = 1; i < all.size(); ++i) {
          Term lhs = fit(all[0], cw);
          Term rhs = fit(all[i], cw);
          Term eq  = solver_->make_term(Equal, lhs, rhs);
          acc      = acc == nullptr ? eq : solver_->make_term(And, acc, eq);
        }
        result = pred_to_bv(acc);
        break;
      }
      case Ntype_op::LT:
      case Ntype_op::GT: {
        auto& as = pid(0);
        auto& bs = pid(1);
        if (as.empty() || bs.empty()) {
          return fail("LT/GT missing a/b operand");
        }
        Term acc;
        for (const auto& a : as) {
          for (const auto& b : bs) {
            bool   both_signed = a.is_signed && b.is_signed;
            int    cw          = std::max(a.width, b.width);
            Term   la = fit(a, cw), lb = fit(b, cw);
            PrimOp cmp = (op == Ntype_op::LT) ? (both_signed ? BVSlt : BVUlt) : (both_signed ? BVSgt : BVUgt);
            Term   one = solver_->make_term(cmp, la, lb);
            acc        = acc == nullptr ? one : solver_->make_term(And, acc, one);
          }
        }
        result = pred_to_bv(acc);
        break;
      }
      case Ntype_op::SHL: {
        if (pid(0).empty()) {
          return fail("SHL missing a");
        }
        Term a = fit(pid(0)[0], W);
        Term acc;
        for (const auto& b : pid(1)) {  // one-hot amounts, ORed
          Term sh = solver_->make_term(BVShl, a, fit(b, W));
          acc     = acc == nullptr ? sh : solver_->make_term(BVOr, acc, sh);
        }
        result = acc == nullptr ? a : acc;
        break;
      }
      case Ntype_op::SRA: {
        if (pid(0).empty() || pid(1).empty()) {
          return fail("SRA missing a/b");
        }
        result = solver_->make_term(BVAshr, fit(pid(0)[0], W), fit(pid(1)[0], W));
        break;
      }
      case Ntype_op::Sext: {
        if (pid(0).empty() || pid(1).empty()) {
          return fail("Sext missing a/pos");
        }
        const Val&      a = pid(0)[0];
        // pos must be a constant we can read.
        // (driver_val already turned a const driver into a literal Val; recover its int)
        // Re-fetch the pos driver as a Dlop for the position.
        // Find the pid1 edge driver pin:
        hhds::Pin_class pos_pin;
        for (const auto& e : node.inp_edges()) {
          if (e.sink.get_port_id() == 1) {
            pos_pin = e.driver;
            break;
          }
        }
        if (pos_pin.is_invalid() || !gu::is_const_pin(pos_pin)) {
          return fail("Sext with non-constant position not supported (M1)");
        }
        Dlop posc = gu::hydrate_const(pos_pin);
        if (!posc.is_just_i64()) {
          return fail("Sext position too wide (M1)");
        }
        int pos = static_cast<int>(posc.to_just_i64());
        if (pos < 1 || pos > a.width) {
          return fail("Sext position out of range (M1)");
        }
        Term low = (pos == a.width) ? a.term : solver_->make_term(Op(Extract, pos - 1, 0), a.term);
        result   = (W <= pos) ? (W == pos ? low : solver_->make_term(Op(Extract, W - 1, 0), low))
                              : solver_->make_term(Op(Sign_Extend, W - pos), low);
        break;
      }
      case Ntype_op::Get_mask: {
        if (pid(0).empty()) {
          return fail("Get_mask missing a");
        }
        const Val&      a = pid(0)[0];
        hhds::Pin_class mask_pin;
        for (const auto& e : node.inp_edges()) {
          if (e.sink.get_port_id() == Ntype::get_sink_pid(op, "mask")) {
            mask_pin = e.driver;
            break;
          }
        }
        if (mask_pin.is_invalid() || !gu::is_const_pin(mask_pin)) {
          return fail("Get_mask with non-constant mask not supported (M1)");
        }
        Dlop mask = gu::hydrate_const(mask_pin);
        if (mask.is_just_i64() && mask.to_just_i64() == -1) {
          // zero-extend (sign -> unsigned cast)
          Val zext{a.term, a.width, false};
          result = fit(zext, W);
          break;
        }
        auto range = mask.get_mask_range();  // [begin, end)
        int  rb = range.first, re = range.second;
        if (rb < 0 || re <= rb) {
          return fail("Get_mask non-contiguous mask not supported (M1)");
        }
        int hi = std::min(re, a.width) - 1;
        if (hi < rb) {
          return fail("Get_mask range outside operand (M1)");
        }
        Term slice = solver_->make_term(Op(Extract, hi, rb), a.term);
        Val  sv{slice, hi - rb + 1, false};
        result = fit(sv, W);
        break;
      }
      case Ntype_op::Set_mask: {
        // Support only the trivial mask==0 passthrough in M1.
        hhds::Pin_class mask_pin;
        for (const auto& e : node.inp_edges()) {
          if (e.sink.get_port_id() == Ntype::get_sink_pid(op, "mask")) {
            mask_pin = e.driver;
            break;
          }
        }
        if (!mask_pin.is_invalid() && gu::is_const_pin(mask_pin)) {
          Dlop mask = gu::hydrate_const(mask_pin);
          if (mask.is_known_zero() && !pid(0).empty()) {
            result = fit(pid(0)[0], W);
            break;
          }
        }
        return fail("Set_mask (non-trivial) not supported (M1)");
      }
      case Ntype_op::AttrSet: {
        // pass-through of the parent driver (pid0).
        if (pid(0).empty()) {
          return fail("AttrSet without parent driver");
        }
        result = fit(pid(0)[0], W);
        break;
      }
      case Ntype_op::Mux:
      case Ntype_op::Hotmux: {
        // pid0 = selector; values on pid 1..N.
        if (pid(0).empty()) {
          return fail("Mux/Hotmux missing selector");
        }
        const Val&       sel = pid(0)[0];
        std::vector<Val> arms;
        for (hhds::Port_id p = 1;; ++p) {
          auto it = by_pid.find(p);
          if (it == by_pid.end()) {
            break;
          }
          arms.push_back(it->second.front());
        }
        if (arms.empty()) {
          return fail("Mux/Hotmux has no arms");
        }
        // default else = last arm (covers in-range exactly + out-of-range det.)
        result = fit(arms.back(), W);
        for (int k = static_cast<int>(arms.size()) - 2; k >= 0; --k) {
          int64_t key  = (op == Ntype_op::Mux) ? k : (int64_t{1} << k);
          Term    cond = solver_->make_term(Equal, sel.term, solver_->make_term(key, bv(sel.width)));
          result       = solver_->make_term(Ite, cond, fit(arms[k], W), result);
        }
        break;
      }
      default: return fail("unsupported op '" + std::string(Ntype::get_name(op)) + "' (M1)");
    }

    if (result == nullptr) {
      return fail("op '" + std::string(Ntype::get_name(op)) + "' produced no term");
    }
    pin2val[dpin.get_class_index()] = Val{result, W, out_signed};
  }

  // ---- Outputs: value driving each output sink, fit to the declared width.
  for (const auto& d : gio->get_output_pin_decls()) {
    auto spin = g->get_output_pin(d.name);
    if (spin.is_invalid()) {
      continue;
    }
    auto edges = spin.inp_edges();
    if (edges.empty()) {
      return fail("output '" + d.name + "' is undriven");
    }
    bool ok = true;
    Val  v  = driver_val(edges.front().driver, ok);
    if (!ok) {
      return fail("output '" + d.name + "' driver not encodable");
    }
    int ow = real_width_io(spin, *gio, d.name);
    if (ow == 0) {
      ow = v.width;
    }
    out.outputs[d.name] = Val{fit(v, ow), ow, !gio->is_unsign(d.name)};
  }

  return out;
}

}  // namespace livehd::lec
