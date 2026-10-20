//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Hand-written rules, milestone D: two-arm Mux canonicalization (the fixed-shape
// ones, mux_sel_arm and mux_sel_not, live in mux.rules). A Mux is
// `Mux(s, f, t) = s ? t : f` with banks 0 (selector), 1 (f), 2 (t). A Mux with
// any other port layout is an n-ary selection and no rule here touches it.

#include "runtime/match.hpp"

using namespace livehd::aegraph;
using namespace livehd::aegraph::rt;

AEG_RULE_GROUP(mux);

namespace {

[[nodiscard]] bool two_arm(const Root& root) {
  return root.ops.size() == 3 && root.ops[0].bank == 0 && root.ops[1].bank == 1 && root.ops[2].bank == 2;
}

}  // namespace

// Mux(s, 1, 0) = EQ(s, 0), and Mux(s, 0, 1) = s for a 0/1 selector (a wider
// selector must keep the Mux: it reads any nonzero value as true).
AEG_NATIVE_RULE(mux_bool_const_arms, simplify, 30, Ntype_op::Mux) {
  if (!two_arm(root)) {
    return;
  }
  const Class_id s = root.ops[0].cls, f = root.ops[1].cls, t = root.ops[2].cls;
  if (ctx.is_int(f, 1) && ctx.is_int(t, 0) && !ctx.known(s)) {
    out.subsume(ctx.make(Ntype_op::EQ, {Operand{0, s}, Operand{0, ctx.konst(0)}}));
  } else if (ctx.is_int(f, 0) && ctx.is_int(t, 1) && !ctx.known(s) && ctx.bool01(s)) {
    out.subsume(s);
  }
}

// One constant 0/1 arm over a 0/1 value is plain logic:
//   s ? 1 : f = s | f      s ? 0 : f = !s & f      s ? t : 0 = s & t      s ? t : 1 = !s | t
// Only over a computed (e-node) value: a latch Q is a leaf here, and `s | q` on a
// latch's hold arm would be a Q -> D path through logic.
AEG_NATIVE_RULE(mux_const_arm_logic, simplify, 30, Ntype_op::Mux) {
  if (!two_arm(root)) {
    return;
  }
  const Class_id s = root.ops[0].cls, f = root.ops[1].cls, t = root.ops[2].cls;
  const auto     fk = ctx.small_int(f), tk = ctx.small_int(t);
  if (fk.has_value() == tk.has_value() || ctx.known(s)) {
    return;
  }
  const int64_t  k     = tk ? *tk : *fk;
  const Class_id other = tk ? f : t;
  if ((k != 0 && k != 1) || ctx.known(other) || ctx.eg().cls(other).kind != Egraph::Kind::node || !ctx.bool01(s)
      || !ctx.bool01(other)) {
    return;
  }
  const bool negated = tk ? k == 0 : k == 1;
  Class_id   literal = s;
  if (negated) {
    const auto y = ctx.eq_against(s, 0);  // !(y == 0) is y for y in {0,1}
    literal      = (y && !ctx.known(*y) && ctx.bool01(*y)) ? *y : ctx.make(Ntype_op::EQ, {Operand{0, s}, Operand{0, ctx.konst(0)}});
  }
  out.subsume(ctx.make(k == 1 ? Ntype_op::Or : Ntype_op::And, {Operand{0, literal}, Operand{0, other}}));
}

namespace {

// A member of `cls` that is Set_mask(base, lo, hi, value) with a constant window.
struct Lane_write {
  Class_id base  = kNoClass;
  Class_id value = kNoClass;
  int64_t  lo    = 0;
  int64_t  hi    = 0;
};

[[nodiscard]] std::optional<Lane_write> lane_write_of(Ctx& ctx, Class_id cls) {
  std::optional<Lane_write> found;
  ctx.each_member(cls, [&](Class_id member) {
    const Enode* n = ctx.node_of(member);
    if (n == nullptr || n->op != Ntype_op::Set_mask) {
      return false;
    }
    const auto ops = ctx.operands_of(*n);
    if (ops.size() != 4 || ops[0].bank != 0 || ops[1].bank != 2 || ops[2].bank != 3 || ops[3].bank != 4) {
      return false;
    }
    const auto l = ctx.small_int(ops[1].cls), h = ctx.small_int(ops[2].cls);
    if (!l || !h || *l < 0 || *h <= *l) {
      return false;
    }
    found = Lane_write{ops[0].cls, ops[3].cls, *l, *h};
    return true;
  });
  return found;
}

Class_id make_set_mask(Ctx& ctx, const Lane_write& w, Class_id value) {
  return ctx.make(Ntype_op::Set_mask,
                  {Operand{0, w.base}, Operand{2, ctx.konst(w.lo)}, Operand{3, ctx.konst(w.hi)}, Operand{4, value}});
}

}  // namespace

// Both arms rewrite the same lane of the same word: select only the lane value,
//   Mux(s, Set_mask(b, w, v0), Set_mask(b, w, v1)) = Set_mask(b, w, Mux(s, v0, v1))
// (classic scalar_mux factor_set_mask_pair). It removes the word-wide Mux of a nested
// `if (reset) bit = R; else if (enable) bit = D;` update. An alt since milestone I: the lane Mux is narrower (hardware
// pays per bit, so `synth` takes it whenever the window is narrower than the word) but a simulator word is a word, and
// the factored form adds a Get_mask, so `sim` keeps the word Mux of a register of at most 64 bits.
AEG_NATIVE_RULE(mux_setmask_pair, alt, 35, Ntype_op::Mux) {
  if (!two_arm(root)) {
    return;
  }
  const Class_id s = root.ops[0].cls, f = root.ops[1].cls, t = root.ops[2].cls;
  const auto     w0 = lane_write_of(ctx, f), w1 = lane_write_of(ctx, t);
  if (!w0 || !w1 || w0->base != w1->base || w0->lo != w1->lo || w0->hi != w1->hi) {
    return;
  }
  const Class_id lane = ctx.make(Ntype_op::Mux, {Operand{0, s}, Operand{1, w0->value}, Operand{2, w1->value}});
  out.add(make_set_mask(ctx, *w0, lane));
}

// A conditional write of one lane keeps the Mux narrow:
//   Mux(s, b, Set_mask(b, w, v)) = Set_mask(b, w, Mux(s, Get_mask(b, w), v))      (and the swapped arms)
// Slang emits this for every conditional assignment to one bit of a packed register; leaving the Mux outside
// makes the mapper select the ENTIRE packed word once per written bit (classic scalar_mux
// factor_set_mask_arm). An alt since milestone I, for the reason mux_setmask_pair gives.
AEG_NATIVE_RULE(mux_setmask_arm, alt, 34, Ntype_op::Mux) {
  if (!two_arm(root)) {
    return;
  }
  const Class_id s = root.ops[0].cls, f = root.ops[1].cls, t = root.ops[2].cls;
  for (const bool updated_is_true : {true, false}) {
    const Class_id updated = updated_is_true ? t : f;
    const Class_id base    = updated_is_true ? f : t;
    const auto     w       = lane_write_of(ctx, updated);
    if (!w || w->base != base) {
      continue;
    }
    const Class_id old_lane
        = ctx.make(Ntype_op::Get_mask, {Operand{0, base}, Operand{2, ctx.konst(w->lo)}, Operand{3, ctx.konst(w->hi)}});
    const Class_id lane = updated_is_true ? ctx.make(Ntype_op::Mux, {Operand{0, s}, Operand{1, old_lane}, Operand{2, w->value}})
                                          : ctx.make(Ntype_op::Mux, {Operand{0, s}, Operand{1, w->value}, Operand{2, old_lane}});
    out.add(make_set_mask(ctx, *w, lane));
    return;
  }
}

// Q1: an out-of-range or disconnected selection of an n-ary Mux is a '?' owned by that Mux, so any value is a legal
// resolution of it: a constant selector that names no connected arm may be read as 0 (classic folds exactly this
// to 0). Written as an alt, not a simplify: the engine never picks a refinement on its own, the extraction does,
// and a '?' that is one value to the Mux class stays one value at every reader (the literal is the Mux's own).
AEG_NATIVE_RULE(mux_oob_sel, alt, 20, Ntype_op::Mux) {
  if (root.ops.empty() || root.ops[0].bank != 0 || two_arm(root)) {
    return;
  }
  const auto s = ctx.small_int(root.ops[0].cls);
  if (!s) {
    return;
  }
  if (*s >= 0) {
    for (const auto& arm : root.ops) {
      if (arm.bank == static_cast<uint32_t>(*s) + 1) {
        return;  // a connected arm
      }
    }
  }
  out.add(ctx.konst(0));
}

// Guarded accumulation (classic scalar_mux guard_accumulation), a ROLLED-LOOP rule: `if s { acc OP= x }` is
//   Mux(s, a, a OP x) = a OP Mux(s, id, x)      (id = 0 for Or/Xor, -1 for And; and the swapped arms)
// bit for bit the same logic, but the Mux no longer sits on the accumulator, so an unrolled run of guarded updates
// becomes one chain of ONE associative operator that flattens into a single n-ary cell instead of a serial mux
// chain. It runs only in a lifted loop body (Ctx::guard_scope), where bitwidth follows and sizes the new Mux; the
// update cell must be private to this Mux and plain (no color, no policy); a held STATE output stays a Mux (the
// latch contract exempts Q only as a direct Mux arm, and enableopt turns a Q hold into an enable). A Sum is not
// guarded: a + Mux(s, 0, x) puts the select ahead of the carry chain.
AEG_NATIVE_RULE(mux_guard_accumulation, simplify, 25, Ntype_op::Mux) {
  if (!two_arm(root) || !ctx.guard_scope() || root.attrs.has_color || ctx.known(root.ops[0].cls) != nullptr) {
    return;
  }
  const Class_id s = root.ops[0].cls;
  for (const bool updated_is_true : {true, false}) {
    const Class_id updated = updated_is_true ? root.ops[2].cls : root.ops[1].cls;
    const Class_id hold    = updated_is_true ? root.ops[1].cls : root.ops[2].cls;
    const Enode*   cell    = ctx.eg().enode_of(updated);
    if (cell == nullptr || (cell->op != Ntype_op::Or && cell->op != Ntype_op::Xor && cell->op != Ntype_op::And) || updated == hold
        || !ctx.single_use(updated)) {
      continue;
    }
    const auto& attrs = ctx.eg().attrs_of(*cell);
    if (attrs.has_color || attrs.policy != 0 || attrs.has_proven) {
      continue;
    }
    const auto& hold_class = ctx.eg().cls(hold);
    if (ctx.eg().is_state(hold) || hold_class.kind == Egraph::Kind::literal) {
      continue;
    }
    std::vector<Operand> rest;
    size_t               held = 0;
    for (const auto& o : ctx.operands_of(*cell)) {
      if (o.bank != 0) {
        held = 2;  // not a plain operand bank
        break;
      }
      if (o.cls == hold) {
        ++held;
      } else {
        rest.push_back(Operand{0, o.cls});
      }
    }
    if (held != 1 || rest.empty()) {
      continue;  // the held value must appear exactly once (a Xor parity of it stays)
    }
    // Building a class may reallocate the e-node storage behind cell.
    const auto     op       = cell->op;
    const Class_id term     = ctx.make_bank(op, std::move(rest));
    const Class_id identity = ctx.konst(op == Ntype_op::And ? -1 : 0);
    const Class_id guard    = updated_is_true ? ctx.make(Ntype_op::Mux,
                                                         {
                                                             Operand{0,        s},
                                                             Operand{1, identity},
                                                             Operand{2,     term}
    })
                                              : ctx.make(Ntype_op::Mux, {Operand{0, s}, Operand{1, term}, Operand{2, identity}});
    out.subsume(ctx.make_bank(op,
                              std::vector<Operand>{
                                  Operand{0,  hold},
                                  Operand{0, guard}
    }));
    return;
  }
}
