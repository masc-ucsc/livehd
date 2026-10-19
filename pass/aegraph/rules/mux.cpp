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
