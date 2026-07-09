// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "prove.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "cell.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"

namespace livehd::formal {

using cvc5::Kind;
using cvc5::Sort;
using cvc5::Term;
using livehd::lec::Val;
namespace gu = livehd::graph_util;

Prover::Prover(hhds::Graph* g, const Prove_options& opts) : g_(g), opts_(opts) {
  // Pre-seed primary inputs from the IO decls (O(#ports), not a whole-graph walk):
  // each gets one fresh free symbol, shared by every later query. This is the only
  // eager step; flops/memory are still cut lazily inside each property cone.
  auto gio = g_->get_io();
  for (const auto& d : gio->get_input_pin_decls()) {
    auto dpin = g_->get_input_pin(d.name);
    if (dpin.is_invalid()) {
      continue;
    }
    int w = lec::real_width_io(dpin, *gio, d.name);
    if (w == 0) {
      w = 1;
    }
    bool sgn = !gio->is_unsign(d.name);
    Term t   = tm_.mkConst(tm_.mkBitVectorSort(w < 1 ? 1 : w), std::string(d.name));
    memo_[dpin.get_class_index()] = Val{t, w, sgn};
    inputs_.emplace_back(std::string(d.name), t);
  }
}

Term Prover::bv_const(int width, uint64_t val) {
  int w = width < 1 ? 1 : width;
  if (w < 64) {
    val &= (uint64_t{1} << w) - 1;
  }
  return tm_.mkBitVector(static_cast<uint32_t>(w), val);
}

Term Prover::bv_extract(const Term& t, int hi, int lo) {
  auto op = tm_.mkOp(Kind::BITVECTOR_EXTRACT, {static_cast<uint32_t>(hi), static_cast<uint32_t>(lo)});
  return tm_.mkTerm(op, {t});
}

Term Prover::pred_to_bv(const Term& b) { return tm_.mkTerm(Kind::ITE, {b, bv_const(1, 1), bv_const(1, 0)}); }

// ---- Demand-driven cone walk (no term building): count + classify the cone.
void Prover::cone_walk(const hhds::Pin_class& pin, absl::flat_hash_set<hhds::Class_index>& seen, int& n, bool& stateful,
                       bool& unsupported) {
  if (pin.is_invalid()) {
    unsupported = true;
    return;
  }
  if (gu::is_const_pin(pin)) {
    return;  // constants are leaves, resolved on demand
  }
  auto ci = pin.get_class_index();
  if (!seen.insert(ci).second) {
    return;  // already counted (sharing collapses here)
  }
  ++n;
  if (gu::is_graph_input_pin(pin)) {
    return;  // primary input leaf
  }
  auto node = pin.get_master_node();
  auto op   = gu::type_op_of(node);
  if (op == Ntype_op::Flop) {
    stateful = true;  // cut: free current-state symbol (leaf)
    return;
  }
  if (op == Ntype_op::Memory) {
    stateful    = true;
    unsupported = true;  // memory reads not modeled yet -> Unknown
    return;
  }
  if (op == Ntype_op::Sub || op == Ntype_op::Fflop || op == Ntype_op::Latch || op == Ntype_op::Nconst) {
    unsupported = true;
    return;
  }
  for (const auto& e : node.inp_edges()) {
    cone_walk(e.driver, seen, n, stateful, unsupported);
  }
}

int Prover::cone_info(const hhds::Pin_class& pin, bool& stateful, bool& unsupported) {
  absl::flat_hash_set<hhds::Class_index> seen;
  int                                    n = 0;
  cone_walk(pin, seen, n, stateful, unsupported);
  return n;
}

// ---- Demand-driven cone encode: dpin -> Val, memoized; nullopt if unsupported.
std::optional<Val> Prover::val_of(const hhds::Pin_class& dpin) {
  if (dpin.is_invalid()) {
    enc_unsupported_ = true;
    return std::nullopt;
  }
  if (gu::is_const_pin(dpin)) {
    Dlop c     = gu::hydrate_const(dpin);
    int  width = std::max(1, c.get_bits());
    bool sgn   = c.is_negative();
    Term t;
    if (c.is_just_i64()) {
      t = bv_const(width, static_cast<uint64_t>(c.to_just_i64()));
    } else {
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
  }

  auto ci = dpin.get_class_index();
  if (auto it = memo_.find(ci); it != memo_.end()) {
    return it->second;
  }

  if (gu::is_graph_input_pin(dpin)) {
    // Fallback: a primary input the ctor did not pre-seed (e.g. unnamed). Use the
    // pin's own bits (real_width, not the by-name IO lookup, to avoid asserting on
    // a non-declared name).
    int w = lec::real_width(dpin);
    if (w == 0) {
      w = 1;
    }
    bool        sgn = !gu::is_unsign(dpin);
    std::string sym = "\x01in:" + std::to_string(static_cast<uint64_t>(dpin.get_debug_pid()));
    Term        t   = tm_.mkConst(tm_.mkBitVectorSort(w < 1 ? 1 : w), sym);
    Val         v{t, w, sgn};
    memo_[ci] = v;
    inputs_.emplace_back(sym, t);
    return v;
  }

  auto node = dpin.get_master_node();
  auto op   = gu::type_op_of(node);

  if (op == Ntype_op::Flop) {
    int w = lec::real_width(dpin);
    if (w == 0) {
      w = 1;
    }
    bool        sgn = !gu::is_unsign(dpin);
    enc_stateful_   = true;
    std::string nm  = lec::flop_state_key(*g_, node);
    Term        t   = tm_.mkConst(tm_.mkBitVectorSort(w < 1 ? 1 : w), nm);
    Val         v{t, w, sgn};
    memo_[ci] = v;
    return v;
  }
  if (op == Ntype_op::Memory) {
    enc_stateful_    = true;
    enc_unsupported_ = true;
    return std::nullopt;
  }
  if (op == Ntype_op::Sub || op == Ntype_op::Fflop || op == Ntype_op::Latch || op == Ntype_op::Nconst) {
    enc_unsupported_ = true;
    return std::nullopt;
  }

  if (on_stack_.contains(ci)) {  // combinational cycle (should not occur post-tolg)
    enc_unsupported_ = true;
    return std::nullopt;
  }
  on_stack_.insert(ci);
  auto r = encode_comb(node, dpin);
  on_stack_.erase(ci);
  if (r) {
    memo_[ci] = *r;
  }
  return r;
}

std::optional<Val> Prover::encode_comb(const hhds::Node_class& node, const hhds::Pin_class& dpin) {
  auto op = gu::type_op_of(node);
  int  W  = lec::real_width(dpin);
  if (W == 0) {
    W = 1;  // comparisons / reductions are 1-bit
  }
  bool out_signed = !gu::is_unsign(dpin);

  absl::flat_hash_map<hhds::Port_id, std::vector<Val>> by_pid;
  std::vector<Val>                                     all;
  for (const auto& e : node.inp_edges()) {
    auto v = val_of(e.driver);
    if (!v) {
      return std::nullopt;
    }
    by_pid[e.sink.get_port_id()].push_back(*v);
    all.push_back(*v);
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
      // Verilog sign rule: a bitwise op is signed only if EVERY operand is signed;
      // one unsigned operand makes the whole op unsigned (zero-extend all). Critical
      // for one-hot selector packing — a lone signed 1-bit condition must NOT
      // sign-extend to all-ones, which would spuriously look like >=2 bits set.
      bool eff_signed = true;
      for (const auto& v : all) {
        eff_signed = eff_signed && v.is_signed;
      }
      for (const auto& v : all) {
        Term t = lec::fit_to(tm_, Val{v.term, v.width, eff_signed}, W);
        result = result.isNull() ? t : tm_.mkTerm(k, {result, t});
      }
      if (result.isNull()) {
        result = (op == Ntype_op::And) ? tm_.mkTerm(Kind::BITVECTOR_NOT, {bv_const(W, 0)}) : bv_const(W, 0);
      }
      break;
    }
    case Ntype_op::Mult: {
      for (const auto& v : all) {
        Term t = lec::fit_to(tm_, v, W);
        result = result.isNull() ? t : tm_.mkTerm(Kind::BITVECTOR_MULT, {result, t});
      }
      if (result.isNull()) {
        result = bv_const(W, 1);
      }
      break;
    }
    case Ntype_op::Sum: {
      Term add_acc;
      for (const auto& v : pid(0)) {
        Term t  = lec::fit_to(tm_, v, W);
        add_acc = add_acc.isNull() ? t : tm_.mkTerm(Kind::BITVECTOR_ADD, {add_acc, t});
      }
      if (add_acc.isNull()) {
        add_acc = bv_const(W, 0);
      }
      for (const auto& v : pid(1)) {
        add_acc = tm_.mkTerm(Kind::BITVECTOR_SUB, {add_acc, lec::fit_to(tm_, v, W)});
      }
      result = add_acc;
      break;
    }
    case Ntype_op::Div: {
      if (pid(0).size() != 1 || pid(1).size() != 1) {
        enc_unsupported_ = true;
        return std::nullopt;
      }
      // Division is NOT modular like add/mult: fitting the operands to the
      // (often tiny) output width W corrupts it. Bitwidth analysis stamps the
      // QUOTIENT width on the Div pin — e.g. `c/100` with c:u3 always yields 0,
      // so W is ~1 bit and `fit(100, 1)` truncates the divisor to 0 =>
      // UDIV-by-zero => all-ones garbage. Compute at a width holding both
      // operands, then narrow to W (matches pass/lec/encode.cpp).
      int  dw = std::max({pid(0)[0].width, pid(1)[0].width, W});
      Term a  = lec::fit_to(tm_, pid(0)[0], dw);
      Term b  = lec::fit_to(tm_, pid(1)[0], dw);
      Term q  = tm_.mkTerm(out_signed ? Kind::BITVECTOR_SDIV : Kind::BITVECTOR_UDIV, {a, b});
      result  = lec::fit_to(tm_, Val{q, dw, out_signed}, W);
      break;
    }
    case Ntype_op::Not: {
      if (all.empty()) {
        enc_unsupported_ = true;
        return std::nullopt;
      }
      result = tm_.mkTerm(Kind::BITVECTOR_NOT, {lec::fit_to(tm_, all[0], W)});
      break;
    }
    case Ntype_op::Ror: {
      Term concat;
      int  cw = 0;
      for (const auto& v : all) {
        concat = concat.isNull() ? v.term : tm_.mkTerm(Kind::BITVECTOR_CONCAT, {concat, v.term});
        cw += v.width;
      }
      if (concat.isNull()) {
        enc_unsupported_ = true;
        return std::nullopt;
      }
      result = pred_to_bv(tm_.mkTerm(Kind::DISTINCT, {concat, bv_const(cw, 0)}));
      break;
    }
    case Ntype_op::EQ: {
      if (all.size() < 2) {
        enc_unsupported_ = true;
        return std::nullopt;
      }
      int  cw         = 0;
      bool eff_signed = true;
      for (const auto& v : all) {
        cw         = std::max(cw, v.width);
        eff_signed = eff_signed && v.is_signed;
      }
      auto ext = [&](const Val& v) { return lec::fit_to(tm_, Val{v.term, v.width, eff_signed}, cw); };
      Term acc;
      for (size_t i = 1; i < all.size(); ++i) {
        Term eq = tm_.mkTerm(Kind::EQUAL, {ext(all[0]), ext(all[i])});
        acc     = acc.isNull() ? eq : tm_.mkTerm(Kind::AND, {acc, eq});
      }
      result = pred_to_bv(acc);
      break;
    }
    case Ntype_op::LT:
    case Ntype_op::GT: {
      auto& as = pid(0);
      auto& bs = pid(1);
      if (as.empty() || bs.empty()) {
        enc_unsupported_ = true;
        return std::nullopt;
      }
      Term acc;
      for (const auto& a : as) {
        for (const auto& b : bs) {
          // Pyrope compares by VALUE, so the compare is signed whenever EITHER
          // operand is signed (not only when BOTH are): a signed value vs a
          // non-negative (front-end-unsigned) constant must still order by
          // signed value. Extend each operand per ITS OWN sign; for a MIXED
          // pair add one headroom bit so the unsigned operand's top bit is not
          // misread as a sign bit (matches pass/lec/encode.cpp).
          bool use_signed = a.is_signed || b.is_signed;
          int  cw         = std::max(a.width, b.width);
          if (a.is_signed != b.is_signed) {
            cw += 1;  // mixed sign: keep the unsigned operand non-negative
          }
          Term la  = lec::fit_to(tm_, Val{a.term, a.width, a.is_signed}, cw);
          Term lb  = lec::fit_to(tm_, Val{b.term, b.width, b.is_signed}, cw);
          Kind cmp = (op == Ntype_op::LT) ? (use_signed ? Kind::BITVECTOR_SLT : Kind::BITVECTOR_ULT)
                                          : (use_signed ? Kind::BITVECTOR_SGT : Kind::BITVECTOR_UGT);
          Term one         = tm_.mkTerm(cmp, {la, lb});
          acc              = acc.isNull() ? one : tm_.mkTerm(Kind::AND, {acc, one});
        }
      }
      result = pred_to_bv(acc);
      break;
    }
    case Ntype_op::SHL: {
      if (pid(0).empty()) {
        enc_unsupported_ = true;
        return std::nullopt;
      }
      Term a = lec::fit_to(tm_, pid(0)[0], W);
      Term acc;
      for (const auto& b : pid(1)) {
        Term shamt = lec::fit_to(tm_, Val{b.term, b.width, false}, W);
        Term sh    = tm_.mkTerm(Kind::BITVECTOR_SHL, {a, shamt});
        acc        = acc.isNull() ? sh : tm_.mkTerm(Kind::BITVECTOR_OR, {acc, sh});
      }
      result = acc.isNull() ? a : acc;
      break;
    }
    case Ntype_op::SRA: {
      if (pid(0).empty() || pid(1).empty()) {
        enc_unsupported_ = true;
        return std::nullopt;
      }
      const Val& a   = pid(0)[0];
      int        cw  = std::max(a.width, std::max(W, 1));
      Term       af  = lec::fit_to(tm_, a, cw);
      Term       shf = lec::fit_to(tm_, Val{pid(1)[0].term, pid(1)[0].width, false}, cw);
      Kind       k   = a.is_signed ? Kind::BITVECTOR_ASHR : Kind::BITVECTOR_LSHR;
      result         = lec::fit_to(tm_, Val{tm_.mkTerm(k, {af, shf}), cw, a.is_signed}, W);
      break;
    }
    case Ntype_op::Sext: {
      if (pid(0).empty() || pid(1).empty()) {
        enc_unsupported_ = true;
        return std::nullopt;
      }
      const Val&      a = pid(0)[0];
      hhds::Pin_class pos_pin;
      for (const auto& e : node.inp_edges()) {
        if (e.sink.get_port_id() == 1) {
          pos_pin = e.driver;
          break;
        }
      }
      if (pos_pin.is_invalid() || !gu::is_const_pin(pos_pin)) {
        enc_unsupported_ = true;
        return std::nullopt;
      }
      Dlop posc = gu::hydrate_const(pos_pin);
      if (!posc.is_just_i64()) {
        enc_unsupported_ = true;
        return std::nullopt;
      }
      int pos = static_cast<int>(posc.to_just_i64());
      if (pos < 1) {
        enc_unsupported_ = true;
        return std::nullopt;
      }
      Term aw  = (pos > a.width) ? lec::fit_to(tm_, a, pos) : a.term;
      Term low = (pos == a.width) ? a.term : bv_extract(aw, pos - 1, 0);
      if (W <= pos) {
        result = (W == pos) ? low : bv_extract(low, W - 1, 0);
      } else {
        auto op2 = tm_.mkOp(Kind::BITVECTOR_SIGN_EXTEND, {static_cast<uint32_t>(W - pos)});
        result   = tm_.mkTerm(op2, {low});
      }
      break;
    }
    case Ntype_op::Get_mask: {
      if (pid(0).empty()) {
        enc_unsupported_ = true;
        return std::nullopt;
      }
      const Val&      a = pid(0)[0];
      // mask is a single const driver; read it directly (no inp_edges scan).
      hhds::Pin_class mask_pin = gu::get_driver_of_sink_name(node, "mask");
      if (mask_pin.is_invalid() || !gu::is_const_pin(mask_pin)) {
        enc_unsupported_ = true;
        return std::nullopt;
      }
      Dlop mask = gu::hydrate_const(mask_pin);
      if (mask.is_just_i64() && mask.to_just_i64() == -1) {
        result = lec::fit_to(tm_, Val{a.term, a.width, false}, W);
        break;
      }
      auto range = mask.get_mask_range();
      int  rb = range.first, re = range.second;
      if (rb < 0 || re <= rb) {
        enc_unsupported_ = true;
        return std::nullopt;
      }
      Term aw    = re > a.width ? lec::fit_to(tm_, a, re) : a.term;
      Term slice = bv_extract(aw, re - 1, rb);
      result     = lec::fit_to(tm_, Val{slice, re - rb, false}, W);
      break;
    }
    case Ntype_op::Set_mask: {
      // mask is a single const driver; read it directly (no inp_edges scan).
      hhds::Pin_class mask_pin = gu::get_driver_of_sink_name(node, "mask");
      if (mask_pin.is_invalid() || !gu::is_const_pin(mask_pin) || pid(0).empty()) {
        enc_unsupported_ = true;
        return std::nullopt;
      }
      const Val& a    = pid(0)[0];
      Dlop       mask = gu::hydrate_const(mask_pin);
      if (mask.is_known_zero()) {
        result = lec::fit_to(tm_, a, W);
        break;
      }
      int  Wm    = std::max(1, W);
      auto range = mask.get_mask_range();
      int  rb = range.first, re = range.second;
      if (rb < 0 || re <= rb) {
        auto runs = mask.get_mask_range_pairs();
        if (runs.empty()) {
          result = lec::fit_to(tm_, a, Wm);
          break;
        }
        auto& vvec = pid(Ntype::get_sink_pid(op, "value"));
        if (vvec.empty()) {
          enc_unsupported_ = true;
          return std::nullopt;
        }
        int total = 0;
        for (auto& pr : runs) {
          total += pr.second - pr.first;
        }
        Term aw  = lec::fit_to(tm_, a, Wm);
        Term val = lec::fit_to(tm_, vvec[0], std::max(1, total));
        int  vi  = 0;
        for (auto& pr : runs) {
          int b = pr.first, e = std::min(pr.second, Wm);
          int w = pr.second - pr.first;
          if (b >= Wm) {
            vi += w;
            continue;
          }
          std::vector<Term> parts;
          if (e < Wm) {
            parts.push_back(bv_extract(aw, Wm - 1, e));
          }
          parts.push_back(bv_extract(val, vi + (e - b) - 1, vi));
          if (b > 0) {
            parts.push_back(bv_extract(aw, b - 1, 0));
          }
          Term r = parts.front();
          for (size_t k = 1; k < parts.size(); ++k) {
            r = tm_.mkTerm(Kind::BITVECTOR_CONCAT, {r, parts[k]});
          }
          aw = r;
          vi += w;
        }
        result = lec::fit_to(tm_, Val{aw, Wm, false}, W);
        break;
      }
      if (rb >= Wm) {
        result = lec::fit_to(tm_, a, Wm);
        break;
      }
      auto& vvec = pid(Ntype::get_sink_pid(op, "value"));
      if (vvec.empty()) {
        enc_unsupported_ = true;
        return std::nullopt;
      }
      int               re_c = std::min(re, Wm);
      Term              aw   = lec::fit_to(tm_, a, Wm);
      std::vector<Term> parts;
      if (re_c < Wm) {
        parts.push_back(bv_extract(aw, Wm - 1, re_c));
      }
      parts.push_back(lec::fit_to(tm_, vvec[0], re_c - rb));
      if (rb > 0) {
        parts.push_back(bv_extract(aw, rb - 1, 0));
      }
      Term r = parts.front();
      for (size_t k = 1; k < parts.size(); ++k) {
        r = tm_.mkTerm(Kind::BITVECTOR_CONCAT, {r, parts[k]});
      }
      result = lec::fit_to(tm_, Val{r, Wm, false}, W);
      break;
    }
    case Ntype_op::AttrSet: {
      if (pid(0).empty()) {
        enc_unsupported_ = true;
        return std::nullopt;
      }
      result = lec::fit_to(tm_, pid(0)[0], W);
      break;
    }
    case Ntype_op::Mux:
    case Ntype_op::Hotmux: {
      if (pid(0).empty()) {
        enc_unsupported_ = true;
        return std::nullopt;
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
        enc_unsupported_ = true;
        return std::nullopt;
      }
      result = lec::fit_to(tm_, arms.back(), W);
      for (int k = static_cast<int>(arms.size()) - 2; k >= 0; --k) {
        int64_t key  = (op == Ntype_op::Mux) ? k : (int64_t{1} << k);
        Term    cond = tm_.mkTerm(Kind::EQUAL, {sel.term, bv_const(sel.width, static_cast<uint64_t>(key))});
        result       = tm_.mkTerm(Kind::ITE, {cond, lec::fit_to(tm_, arms[k], W), result});
      }
      break;
    }
    default: enc_unsupported_ = true; return std::nullopt;
  }

  if (result.isNull()) {
    enc_unsupported_ = true;
    return std::nullopt;
  }
  return Val{result, W, out_signed};
}

// ---- Solve a refutation formula under the deterministic cone-scaled budget.
Query_out Prover::solve(const Term& refute, int cone_nodes, bool stateful) {
  cvc5::Solver solver(tm_);
  solver.setLogic("QF_BV");
  solver.setOption("bv-solver", "bitblast-internal");  // eager: avoids spurious SAT on arithmetic
  if (opts_.budget_k > 0) {
    long long rl = static_cast<long long>(opts_.budget_k) * std::max(1, cone_nodes);
    solver.setOption("rlimit", std::to_string(rl));  // deterministic per-query resource cap
  }
  solver.setOption("produce-models", "true");
  for (const auto& a : assumes_) {
    solver.assertFormula(a);
  }
  for (const auto& [l, r] : side_eqs_) {
    solver.assertFormula(tm_.mkTerm(Kind::EQUAL, {l, r}));
  }
  solver.assertFormula(refute);

  Query_out out;
  out.stateful = stateful;
  cvc5::Result r;
  try {
    r = solver.checkSat();
  } catch (...) {
    out.verdict = Verdict::Unknown;  // solver gave up (resource-out throws in some builds)
    return out;
  }
  if (r.isUnsat()) {
    out.verdict = Verdict::Proven;
  } else if (r.isSat()) {
    out.verdict = Verdict::Refuted;
    std::string w;
    int         shown = 0;
    for (const auto& [name, term] : inputs_) {
      if (shown >= 8) {
        break;
      }
      try {
        auto val = solver.getValue(term);
        if (!w.empty()) {
          w += ", ";
        }
        w += name + "=" + val.getBitVectorValue(10);
        ++shown;
      } catch (...) {
      }
    }
    out.witness = w;
  } else {
    out.verdict = Verdict::Unknown;
  }
  return out;
}

// ---- Public queries -------------------------------------------------------

Query_out Prover::is_true(const hhds::Pin_class& cond) {
  bool st = false, unsup = false;
  int  n = cone_info(cond, st, unsup);
  if (unsup || (opts_.cone_max > 0 && n > opts_.cone_max)) {
    return {Verdict::Unknown, st, ""};
  }
  enc_unsupported_ = false;
  enc_stateful_    = false;
  auto v           = val_of(cond);
  if (!v || enc_unsupported_) {
    return {Verdict::Unknown, st || enc_stateful_, ""};
  }
  int  w      = static_cast<int>(v->term.getSort().getBitVectorSize());
  Term refute = tm_.mkTerm(Kind::EQUAL, {v->term, bv_const(w, 0)});  // cond == 0 falsifies "always true"
  return solve(refute, n, st || enc_stateful_);
}

Query_out Prover::is_false(const hhds::Pin_class& cond) {
  bool st = false, unsup = false;
  int  n = cone_info(cond, st, unsup);
  if (unsup || (opts_.cone_max > 0 && n > opts_.cone_max)) {
    return {Verdict::Unknown, st, ""};
  }
  enc_unsupported_ = false;
  enc_stateful_    = false;
  auto v           = val_of(cond);
  if (!v || enc_unsupported_) {
    return {Verdict::Unknown, st || enc_stateful_, ""};
  }
  int  w      = static_cast<int>(v->term.getSort().getBitVectorSize());
  Term refute = tm_.mkTerm(Kind::DISTINCT, {v->term, bv_const(w, 0)});  // cond != 0 falsifies "always false"
  return solve(refute, n, st || enc_stateful_);
}

Query_out Prover::equal(const hhds::Pin_class& a, const hhds::Pin_class& b) {
  absl::flat_hash_set<hhds::Class_index> seen;
  int                                    n = 0;
  bool                                   st = false, unsup = false;
  cone_walk(a, seen, n, st, unsup);
  cone_walk(b, seen, n, st, unsup);
  if (unsup || (opts_.cone_max > 0 && n > opts_.cone_max)) {
    return {Verdict::Unknown, st, ""};
  }
  enc_unsupported_ = false;
  enc_stateful_    = false;
  auto va          = val_of(a);
  auto vb          = val_of(b);
  if (!va || !vb || enc_unsupported_) {
    return {Verdict::Unknown, st || enc_stateful_, ""};
  }
  int  wa = static_cast<int>(va->term.getSort().getBitVectorSize());
  int  wb = static_cast<int>(vb->term.getSort().getBitVectorSize());
  int  w  = std::max(wa, wb);
  Term ta = lec::fit_to(tm_, lec::Val{va->term, wa, va->is_signed}, w);
  Term tb = lec::fit_to(tm_, lec::Val{vb->term, wb, vb->is_signed}, w);
  Term refute = tm_.mkTerm(Kind::DISTINCT, {ta, tb});  // a != b falsifies "always equal"
  return solve(refute, n, st || enc_stateful_);
}

Query_out Prover::is_onehot0(const hhds::Pin_class& sel) {
  bool st = false, unsup = false;
  int  n = cone_info(sel, st, unsup);
  if (unsup || (opts_.cone_max > 0 && n > opts_.cone_max)) {
    return {Verdict::Unknown, st, ""};
  }
  enc_unsupported_ = false;
  enc_stateful_    = false;
  auto v           = val_of(sel);
  if (!v || enc_unsupported_) {
    return {Verdict::Unknown, st || enc_stateful_, ""};
  }
  Term s    = v->term;
  int  w    = static_cast<int>(s.getSort().getBitVectorSize());
  Term sm1  = tm_.mkTerm(Kind::BITVECTOR_SUB, {s, bv_const(w, 1)});
  Term andv = tm_.mkTerm(Kind::BITVECTOR_AND, {s, sm1});
  Term refute = tm_.mkTerm(Kind::DISTINCT, {andv, bv_const(w, 0)});  // (sel & (sel-1)) != 0 -> >=2 bits set
  return solve(refute, n, st || enc_stateful_);
}

Query_out Prover::is_onehot(const hhds::Pin_class& sel) {
  bool st = false, unsup = false;
  int  n = cone_info(sel, st, unsup);
  if (unsup || (opts_.cone_max > 0 && n > opts_.cone_max)) {
    return {Verdict::Unknown, st, ""};
  }
  enc_unsupported_ = false;
  enc_stateful_    = false;
  auto v           = val_of(sel);
  if (!v || enc_unsupported_) {
    return {Verdict::Unknown, st || enc_stateful_, ""};
  }
  Term s       = v->term;
  int  w       = static_cast<int>(s.getSort().getBitVectorSize());
  Term sm1     = tm_.mkTerm(Kind::BITVECTOR_SUB, {s, bv_const(w, 1)});
  Term andv    = tm_.mkTerm(Kind::BITVECTOR_AND, {s, sm1});
  Term not_oh0 = tm_.mkTerm(Kind::DISTINCT, {andv, bv_const(w, 0)});
  Term is_zero = tm_.mkTerm(Kind::EQUAL, {s, bv_const(w, 0)});
  Term refute  = tm_.mkTerm(Kind::OR, {not_oh0, is_zero});  // >=2 bits OR 0 bits falsifies exactly-one
  return solve(refute, n, st || enc_stateful_);
}

bool Prover::assume(const hhds::Pin_class& cond) {
  enc_unsupported_ = false;
  auto v           = val_of(cond);
  if (!v || enc_unsupported_) {
    return false;
  }
  assumes_.push_back(tm_.mkTerm(Kind::DISTINCT, {v->term, bv_const(v->width, 0)}));
  return true;
}

}  // namespace livehd::formal
