// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "encode.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "cell.hpp"
#include "hhds/attrs/srcid.hpp"
#include "hhds/source_locator.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"

namespace livehd::lec {

using cvc5::Kind;
using cvc5::Sort;
using cvc5::Term;
namespace gu = livehd::graph_util;

// Stable 1:1 cut-point key for a state cell (Flop), used to put corresponding
// registers of the two designs in correspondence. The REGISTER NAME is the
// primary key: both front-ends preserve the RTL name (yosys-slang on the pin,
// native slang now stamps it in tolg), whereas they anchor the srcid to
// different source constructs (native -> the declaration, yosys -> the
// always_ff assignment) so the source span does NOT match across readers. The
// span is the fallback for an unnamed flop (a pass-inserted pipeline stage),
// then the node id (an unmatchable per-design fallback -> a sound Unknown).
//
// The name is normalized: a leading "$...$" yosys decoration (e.g.
// "$driver$cnt_q") and a trailing "___ssa_N" SSA suffix are stripped so the two
// readers' spellings of the same register collapse to one key.
static std::string normalize_reg_name(std::string_view raw) {
  std::string_view s = raw;
  if (!s.empty() && s.front() == '$') {
    if (auto e = s.find('$', 1); e != std::string_view::npos) {
      s.remove_prefix(e + 1);
    }
  }
  if (auto p = s.find("___ssa_"); p != std::string_view::npos) {
    s = s.substr(0, p);
  }
  return std::string{s};
}

std::string flop_state_key(const hhds::Graph& g, const hhds::Node_class& node) {
  auto pn = gu::pin_name_of(node.get_driver_pin(0));
  if (pn.empty()) {
    pn = gu::node_name_of(node);
  }
  if (auto nm = normalize_reg_name(pn); !nm.empty()) {
    return std::string("\x01n:") + nm;
  }
  auto ref = node.attr(hhds::attrs::srcid);
  if (ref.has()) {
    auto span = g.source_locator().resolve_span(ref.get());
    if (!span.file.empty()) {
      return std::format("\x01s:{}-{}@{}", span.start_byte.value_or(0), span.end_byte.value_or(0), span.file);
    }
  }
  return std::string("\x01f:") + std::to_string(static_cast<uint64_t>(node.get_debug_nid()));
}

namespace {

// Bit-vector constant that always fits the requested width: cvc5 requires the
// value to be < 2^width, so mask the low `width` bits (this also yields the
// correct two's-complement encoding for negative i64 inputs).
Term bv_const(cvc5::TermManager& tm, int width, uint64_t val) {
  if (width < 64) {
    val &= (uint64_t{1} << width) - 1;
  }
  return tm.mkBitVector(static_cast<uint32_t>(width), val);
}

// Indexed bit-vector slice a[hi:lo] (inclusive), via mkOp(BITVECTOR_EXTRACT).
Term bv_extract(cvc5::TermManager& tm, const Term& t, int hi, int lo) {
  auto op = tm.mkOp(Kind::BITVECTOR_EXTRACT, {static_cast<uint32_t>(hi), static_cast<uint32_t>(lo)});
  return tm.mkTerm(op, {t});
}

}  // namespace

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
Term fit_to(cvc5::TermManager& tm, const Val& v, int width) {
  if (v.width == width) {
    return v.term;
  }
  if (width < v.width) {
    return bv_extract(tm, v.term, width - 1, 0);  // truncate low bits
  }
  uint32_t d  = static_cast<uint32_t>(width - v.width);
  auto     op = tm.mkOp(v.is_signed ? Kind::BITVECTOR_SIGN_EXTEND : Kind::BITVECTOR_ZERO_EXTEND, {d});
  return tm.mkTerm(op, {v.term});
}

Sort Encoder::bv(int width) { return tm_.mkBitVectorSort(width < 1 ? 1 : width); }

Term Encoder::fit(const Val& v, int width) { return fit_to(tm_, v, width); }

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
      t = bv_const(tm_, width, static_cast<uint64_t>(c.to_just_i64()));
    } else {
      // Wide / partially-unknown constant: build from the MSB-first binary string
      // (get_bits() chars, no prefix). Unknown (X / don't-care) bits are masked
      // to 0 — consistent across two designs reading the same source constant.
      // (Refine to a shared free symbol if X-sensitive equivalence is needed.)
      auto bin = c.to_binary();
      for (auto& ch : bin) {
        if (ch != '0' && ch != '1') {
          ch = '0';
        }
      }
      if (bin.empty()) {
        bin = "0";
      }
      width = static_cast<int>(bin.size());
      t     = tm_.mkBitVector(static_cast<uint32_t>(width), bin, 2);
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
    return tm_.mkTerm(Kind::ITE, {b, bv_const(tm_, 1, 1), bv_const(tm_, 1, 0)});
  };

  // ---- Inputs: shared symbol or a fresh one, mapped onto the input driver pin.
  for (const auto& d : gio->get_input_pin_decls()) {
    auto dpin = g->get_input_pin(d.name);
    int  w    = real_width_io(dpin, *gio, d.name);
    if (w == 0) {
      // A width-less input is a scalar control signal — a clock (abstracted out
      // of the relational encoding via the flop-state cut) or a 1-bit reset /
      // enable. Both designs read the same RTL, so a missing bits attr is
      // consistent across sides; default to 1 rather than refusing to encode.
      // (A genuinely multi-bit input always carries a bits attr from tolg.)
      w = 1;
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
    if (v.term.isNull()) {
      v = Val{tm_.mkConst(bv(w), std::string(prefix) + d.name), w, sgn};
    }
    out.inputs[d.name] = v;
    if (!dpin.is_invalid()) {
      pin2val[dpin.get_class_index()] = v;
    }
  }

  // ---- M2 flop cut-points (register-correspondence SEC). Each Flop's Q (driver
  // pin 0) is a CURRENT-STATE symbol, shared across the two designs by its
  // preserved name (so a 1:1 latch map falls out of name equality). Seeded here,
  // before the combinational loop, so downstream comb reads resolve it like an
  // input; the matching NEXT-STATE value is emitted as a synthetic output after
  // the loop, and the miter then compares next-states alongside primary outputs.
  std::vector<hhds::Node_class> flops;
  for (auto node : g->forward_class()) {
    auto op = gu::type_op_of(node);
    if (op != Ntype_op::Flop) {
      continue;
    }
    auto qpin = node.get_driver_pin(0);
    if (qpin.is_invalid()) {
      continue;
    }
    int w = real_width(qpin);
    if (w == 0) {
      w = 1;
    }
    bool        sgn = !gu::is_unsign(qpin);
    std::string nm  = flop_state_key(*g, node);
    Val         v;
    if (shared_inputs != nullptr) {
      if (auto it = shared_inputs->find(nm); it != shared_inputs->end()) {
        v = it->second;
        if (v.width != w) {
          v.term  = fit(v, w);
          v.width = w;
        }
      }
    }
    if (v.term.isNull()) {
      v = Val{tm_.mkConst(bv(w), std::string(prefix) + nm), w, sgn};
    }
    pin2val[qpin.get_class_index()] = v;
    out.inputs[nm]                  = v;
    flops.push_back(node);
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
    // Flops were cut above (Q seeded; next-state emitted below) — skip the cell.
    if (op == Ntype_op::Flop) {
      continue;
    }
    // Still out of scope: Fflop (no reset model yet), latches, memories (M4 =
    // SMT arrays), and sub-instances (M5 = hierarchical congruence).
    if (op == Ntype_op::Fflop || op == Ntype_op::Latch || op == Ntype_op::Memory || op == Ntype_op::Sub) {
      return fail("sequential/structural op '" + std::string(Ntype::get_name(op)) + "' not supported yet (M2 = Flop only)");
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
        Kind k = (op == Ntype_op::And) ? Kind::BITVECTOR_AND : (op == Ntype_op::Or) ? Kind::BITVECTOR_OR : Kind::BITVECTOR_XOR;
        for (const auto& v : all) {
          Term t = fit(v, W);
          result = result.isNull() ? t : tm_.mkTerm(k, {result, t});
        }
        break;
      }
      case Ntype_op::Mult: {
        for (const auto& v : all) {
          Term t = fit(v, W);
          result = result.isNull() ? t : tm_.mkTerm(Kind::BITVECTOR_MULT, {result, t});
        }
        break;
      }
      case Ntype_op::Sum: {
        Term add_acc;
        for (const auto& v : pid(0)) {  // "a" pins: added
          Term t  = fit(v, W);
          add_acc = add_acc.isNull() ? t : tm_.mkTerm(Kind::BITVECTOR_ADD, {add_acc, t});
        }
        if (add_acc.isNull()) {
          add_acc = bv_const(tm_, W, 0);
        }
        for (const auto& v : pid(1)) {  // "b" pins: subtracted
          add_acc = tm_.mkTerm(Kind::BITVECTOR_SUB, {add_acc, fit(v, W)});
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
        result = tm_.mkTerm(out_signed ? Kind::BITVECTOR_SDIV : Kind::BITVECTOR_UDIV, {a, b});
        break;
      }
      case Ntype_op::Not: {
        if (all.empty()) {
          return fail("Not has no operand");
        }
        result = tm_.mkTerm(Kind::BITVECTOR_NOT, {fit(all[0], W)});
        break;
      }
      case Ntype_op::Ror: {
        // reduction-OR: 1 iff any input bit set.
        Term concat;
        for (const auto& v : all) {
          concat = concat.isNull() ? v.term : tm_.mkTerm(Kind::BITVECTOR_CONCAT, {concat, v.term});
        }
        if (concat.isNull()) {
          return fail("Ror has no operand");
        }
        int cw = 0;
        for (const auto& v : all) {
          cw += v.width;
        }
        result = pred_to_bv(tm_.mkTerm(Kind::DISTINCT, {concat, bv_const(tm_, cw, 0)}));
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
          Term eq  = tm_.mkTerm(Kind::EQUAL, {lhs, rhs});
          acc      = acc.isNull() ? eq : tm_.mkTerm(Kind::AND, {acc, eq});
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
            bool both_signed = a.is_signed && b.is_signed;
            int  cw          = std::max(a.width, b.width);
            Term la = fit(a, cw), lb = fit(b, cw);
            Kind cmp = (op == Ntype_op::LT) ? (both_signed ? Kind::BITVECTOR_SLT : Kind::BITVECTOR_ULT)
                                            : (both_signed ? Kind::BITVECTOR_SGT : Kind::BITVECTOR_UGT);
            Term one = tm_.mkTerm(cmp, {la, lb});
            acc      = acc.isNull() ? one : tm_.mkTerm(Kind::AND, {acc, one});
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
          Term sh = tm_.mkTerm(Kind::BITVECTOR_SHL, {a, fit(b, W)});
          acc     = acc.isNull() ? sh : tm_.mkTerm(Kind::BITVECTOR_OR, {acc, sh});
        }
        result = acc.isNull() ? a : acc;
        break;
      }
      case Ntype_op::SRA: {
        if (pid(0).empty() || pid(1).empty()) {
          return fail("SRA missing a/b");
        }
        result = tm_.mkTerm(Kind::BITVECTOR_ASHR, {fit(pid(0)[0], W), fit(pid(1)[0], W)});
        break;
      }
      case Ntype_op::Sext: {
        if (pid(0).empty() || pid(1).empty()) {
          return fail("Sext missing a/pos");
        }
        const Val& a = pid(0)[0];
        // pos must be a constant we can read.
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
        Term low = (pos == a.width) ? a.term : bv_extract(tm_, a.term, pos - 1, 0);
        if (W <= pos) {
          result = (W == pos) ? low : bv_extract(tm_, low, W - 1, 0);
        } else {
          auto op2 = tm_.mkOp(Kind::BITVECTOR_SIGN_EXTEND, {static_cast<uint32_t>(W - pos)});
          result   = tm_.mkTerm(op2, {low});
        }
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
        Term slice = bv_extract(tm_, a.term, hi, rb);
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
          Term    cond = tm_.mkTerm(Kind::EQUAL, {sel.term, bv_const(tm_, sel.width, static_cast<uint64_t>(key))});
          result       = tm_.mkTerm(Kind::ITE, {cond, fit(arms[k], W), result});
        }
        break;
      }
      default: return fail("unsupported op '" + std::string(Ntype::get_name(op)) + "' (M1)");
    }

    if (result.isNull()) {
      return fail("op '" + std::string(Ntype::get_name(op)) + "' produced no term");
    }
    pin2val[dpin.get_class_index()] = Val{result, W, out_signed};
  }

  // ---- M2 next-state functions. For each cut flop emit the value it latches
  // next cycle as a synthetic output keyed "<nxt><statekey>", so the miter
  // compares the two designs' next-state for every corresponding register (the
  // inductive step: equal current state + equal inputs => equal next state &
  // outputs). N = reset_active ? initial : (enable ? din : Q). reset_pin is a
  // shared primary input, so the same query covers the reset/base case.
  for (const auto& node : flops) {
    auto qpin = node.get_driver_pin(0);
    int  w    = real_width(qpin);
    if (w == 0) {
      w = 1;
    }
    bool       sgn = !gu::is_unsign(qpin);
    const Val& qv  = pin2val[qpin.get_class_index()];  // current-state symbol (seeded above)
    bool       ok  = true;

    Term nval;
    if (auto din_d = gu::get_driver_of_sink_name(node, "din"); !din_d.is_invalid()) {
      Val dv = driver_val(din_d, ok);
      if (!ok) {
        return fail("flop '" + gu::debug_name(node) + "' din not encodable");
      }
      nval = fit(dv, w);
    } else {
      nval = qv.term;  // no din -> the register holds
    }

    // enable: a low write-enable holds Q (no din=q feedback mux in the graph).
    if (auto en_d = gu::get_driver_of_sink_name(node, "enable"); !en_d.is_invalid()) {
      if (gu::is_const_pin(en_d)) {
        if (gu::hydrate_const(en_d).is_known_false()) {
          nval = qv.term;  // never writes
        }
      } else {
        Val ev = driver_val(en_d, ok);
        if (!ok) {
          return fail("flop '" + gu::debug_name(node) + "' enable not encodable");
        }
        Term en_hot = tm_.mkTerm(Kind::DISTINCT, {ev.term, bv_const(tm_, ev.width, 0)});
        nval        = tm_.mkTerm(Kind::ITE, {en_hot, nval, qv.term});
      }
    }

    // reset: a non-const reset overrides with the initial value when asserted.
    if (auto rst_d = gu::get_driver_of_sink_name(node, "reset_pin"); !rst_d.is_invalid() && !gu::is_const_pin(rst_d)) {
      Val rv = driver_val(rst_d, ok);
      if (!ok) {
        return fail("flop '" + gu::debug_name(node) + "' reset not encodable");
      }
      Term rbit     = tm_.mkTerm(Kind::DISTINCT, {rv.term, bv_const(tm_, rv.width, 0)});
      bool negreset = false;
      if (auto neg_d = gu::get_driver_of_sink_name(node, "negreset"); !neg_d.is_invalid() && gu::is_const_pin(neg_d)) {
        negreset = !gu::hydrate_const(neg_d).is_known_false();
      }
      Term rst_hot = negreset ? tm_.mkTerm(Kind::NOT, {rbit}) : rbit;
      Term initv   = bv_const(tm_, w, 0);
      if (auto init_d = gu::get_driver_of_sink_name(node, "initial"); !init_d.is_invalid()) {
        Val iv = driver_val(init_d, ok);
        if (ok) {
          initv = fit(iv, w);
        }
      }
      nval = tm_.mkTerm(Kind::ITE, {rst_hot, initv, nval});
    }

    out.outputs[std::string("\x01nxt:") + flop_state_key(*g, node)] = Val{nval, w, sgn};
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
