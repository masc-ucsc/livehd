//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lnast_to_lgraph.hpp"

#include <memory>
#include <string>

#include "cell.hpp"
#include "graph_library.hpp"
#include "gtest/gtest.h"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "lnast.hpp"
#include "upass_ssa.hpp"

namespace {

// ── Helpers ──────────────────────────────────────────────────────────────────

// Create a fresh LGraph with a unique name so tests don't collide.
static Lgraph* make_lg(const std::string& name) {
  auto* lib = Graph_library::instance("lgdb_lnast_to_lgraph_test");
  return lib->create_lgraph(name, "lgdb_lnast_to_lgraph_test");
}

// Count non-IO nodes of a given op type in the graph using the public
// fast iterator.
static int count_op(Lgraph* lg, Ntype_op op) {
  int n = 0;
  for (auto node : lg->fast()) {
    if (node.get_type_op() == op) {
      n++;
    }
  }
  return n;
}

// Count graph inputs by name.
static bool has_input(Lgraph* lg, const std::string& name) {
  bool found = false;
  lg->each_graph_input([&](const Node_pin& pin) {
    if (pin.has_name() && pin.get_name() == name) {
      found = true;
    }
  });
  return found;
}

// Count graph outputs by name.
static bool has_output(Lgraph* lg, const std::string& name) {
  bool found = false;
  lg->each_graph_output([&](const Node_pin& pin) {
    if (pin.has_name() && pin.get_name() == name) {
      found = true;
    }
  });
  return found;
}

// Build:  top → stmts → assign(lhs, rhs_ref)
//   assign %out = $a
static std::shared_ptr<Lnast> make_pass_through() {
  auto ln = std::make_shared<Lnast>("pass_through");
  ln->set_root(Lnast_ntype::create_top());
  auto stmts = ln->add_child(ln->get_root(), Lnast_ntype::create_stmts());
  auto asgn  = ln->add_child(stmts, Lnast_ntype::create_assign());
  ln->add_child(asgn, Lnast_node::create_ref("%out"));
  ln->add_child(asgn, Lnast_node::create_ref("$a"));
  return ln;
}

// Build:  top → stmts → assign(%out, const 42)
static std::shared_ptr<Lnast> make_const_assign() {
  auto ln = std::make_shared<Lnast>("const_assign");
  ln->set_root(Lnast_ntype::create_top());
  auto stmts = ln->add_child(ln->get_root(), Lnast_ntype::create_stmts());
  auto asgn  = ln->add_child(stmts, Lnast_ntype::create_assign());
  ln->add_child(asgn, Lnast_node::create_ref("%out"));
  ln->add_child(asgn, Lnast_node::create_const("42"));
  return ln;
}

// Build:  top → stmts → plus(%out, $a, $b)
static std::shared_ptr<Lnast> make_add() {
  auto ln = std::make_shared<Lnast>("add");
  ln->set_root(Lnast_ntype::create_top());
  auto stmts = ln->add_child(ln->get_root(), Lnast_ntype::create_stmts());
  auto plus  = ln->add_child(stmts, Lnast_ntype::create_plus());
  ln->add_child(plus, Lnast_node::create_ref("%out"));
  ln->add_child(plus, Lnast_node::create_ref("$a"));
  ln->add_child(plus, Lnast_node::create_ref("$b"));
  return ln;
}

// Build:  top → stmts → lt(%out, $a, $b)
static std::shared_ptr<Lnast> make_lt() {
  auto ln = std::make_shared<Lnast>("lt_cmp");
  ln->set_root(Lnast_ntype::create_top());
  auto stmts = ln->add_child(ln->get_root(), Lnast_ntype::create_stmts());
  auto lt    = ln->add_child(stmts, Lnast_ntype::create_lt());
  ln->add_child(lt, Lnast_node::create_ref("%out"));
  ln->add_child(lt, Lnast_node::create_ref("$a"));
  ln->add_child(lt, Lnast_node::create_ref("$b"));
  return ln;
}

// Build:  top → stmts → bit_and(%out, $a, $b)
static std::shared_ptr<Lnast> make_and() {
  auto ln = std::make_shared<Lnast>("bit_and");
  ln->set_root(Lnast_ntype::create_top());
  auto stmts = ln->add_child(ln->get_root(), Lnast_ntype::create_stmts());
  auto band  = ln->add_child(stmts, Lnast_ntype::create_bit_and());
  ln->add_child(band, Lnast_node::create_ref("%out"));
  ln->add_child(band, Lnast_node::create_ref("$a"));
  ln->add_child(band, Lnast_node::create_ref("$b"));
  return ln;
}

// Build:  top → stmts → bit_not(%out, $a)
static std::shared_ptr<Lnast> make_not() {
  auto ln = std::make_shared<Lnast>("bit_not");
  ln->set_root(Lnast_ntype::create_top());
  auto stmts = ln->add_child(ln->get_root(), Lnast_ntype::create_stmts());
  auto bnot  = ln->add_child(stmts, Lnast_ntype::create_bit_not());
  ln->add_child(bnot, Lnast_node::create_ref("%out"));
  ln->add_child(bnot, Lnast_node::create_ref("$a"));
  return ln;
}

// Build:  top → stmts → mult(%out, $a, $b)
static std::shared_ptr<Lnast> make_mult() {
  auto ln = std::make_shared<Lnast>("mult");
  ln->set_root(Lnast_ntype::create_top());
  auto stmts = ln->add_child(ln->get_root(), Lnast_ntype::create_stmts());
  auto mul   = ln->add_child(stmts, Lnast_ntype::create_mult());
  ln->add_child(mul, Lnast_node::create_ref("%out"));
  ln->add_child(mul, Lnast_node::create_ref("$a"));
  ln->add_child(mul, Lnast_node::create_ref("$b"));
  return ln;
}

}  // namespace

// ── Test 1: pass-through ($a → %out) creates one input, one output, no ops ──
TEST(LnastToLgraph, PassThrough) {
  auto* lg = make_lg("pt1");
  ASSERT_NE(lg, nullptr);
  Lnast_to_lgraph lowerer(lg, make_pass_through());
  lowerer.lower();

  EXPECT_TRUE(has_input(lg, "a")) << "expected graph input 'a'";
  EXPECT_TRUE(has_output(lg, "out")) << "expected graph output 'out'";
  // No arithmetic nodes — just the IO wires.
  EXPECT_EQ(count_op(lg, Ntype_op::Sum), 0);
}

// ── Test 2: constant assign creates a Const node and an output ───────────────
TEST(LnastToLgraph, ConstAssign) {
  auto* lg = make_lg("ca2");
  ASSERT_NE(lg, nullptr);
  Lnast_to_lgraph lowerer(lg, make_const_assign());
  lowerer.lower();

  EXPECT_TRUE(has_output(lg, "out")) << "expected graph output 'out'";
  EXPECT_EQ(count_op(lg, Ntype_op::Nconst), 1) << "expected exactly one Const node";
}

// ── Test 3: addition lowered to Sum node ─────────────────────────────────────
TEST(LnastToLgraph, Addition) {
  auto* lg = make_lg("add3");
  ASSERT_NE(lg, nullptr);
  Lnast_to_lgraph lowerer(lg, make_add());
  lowerer.lower();

  EXPECT_EQ(count_op(lg, Ntype_op::Sum), 1) << "expected exactly one Sum node";
  EXPECT_TRUE(has_input(lg, "a")) << "expected graph input 'a'";
  EXPECT_TRUE(has_input(lg, "b")) << "expected graph input 'b'";
  EXPECT_TRUE(has_output(lg, "out")) << "expected graph output 'out'";
}

// ── Test 4: comparison lowered to LT node ───────────────────────────────────
TEST(LnastToLgraph, LessThan) {
  auto* lg = make_lg("lt4");
  ASSERT_NE(lg, nullptr);
  Lnast_to_lgraph lowerer(lg, make_lt());
  lowerer.lower();

  EXPECT_EQ(count_op(lg, Ntype_op::LT), 1) << "expected exactly one LT node";
  EXPECT_TRUE(has_output(lg, "out")) << "expected graph output 'out'";
}

// ── Test 5: bitwise AND lowered to And node ──────────────────────────────────
TEST(LnastToLgraph, BitAnd) {
  auto* lg = make_lg("and5");
  ASSERT_NE(lg, nullptr);
  Lnast_to_lgraph lowerer(lg, make_and());
  lowerer.lower();

  EXPECT_EQ(count_op(lg, Ntype_op::And), 1) << "expected exactly one And node";
  EXPECT_TRUE(has_output(lg, "out")) << "expected graph output 'out'";
}

// ── Test 6: bitwise NOT lowered to Not node ──────────────────────────────────
TEST(LnastToLgraph, BitNot) {
  auto* lg = make_lg("not6");
  ASSERT_NE(lg, nullptr);
  Lnast_to_lgraph lowerer(lg, make_not());
  lowerer.lower();

  EXPECT_EQ(count_op(lg, Ntype_op::Not), 1) << "expected exactly one Not node";
  EXPECT_TRUE(has_input(lg, "a")) << "expected graph input 'a'";
  EXPECT_TRUE(has_output(lg, "out")) << "expected graph output 'out'";
}

// ── Test 7: multiplication lowered to Mult node ──────────────────────────────
TEST(LnastToLgraph, Multiply) {
  auto* lg = make_lg("mul7");
  ASSERT_NE(lg, nullptr);
  Lnast_to_lgraph lowerer(lg, make_mult());
  lowerer.lower();

  EXPECT_EQ(count_op(lg, Ntype_op::Mult), 1) << "expected exactly one Mult node";
  EXPECT_TRUE(has_output(lg, "out")) << "expected graph output 'out'";
}

// ── Test 8: if/else → two Mux nodes (one per branch variable) ────────────────
// LNAST:
//   if $cond { %out = $a } else { %out = $b }
// Expected:
//   one Mux node: pin0=$cond, pin1=$b (else), pin2=$a (then)
TEST(LnastToLgraph, IfElseToMux) {
  auto ln = std::make_shared<Lnast>("if_else");
  ln->set_root(Lnast_ntype::create_top());
  auto stmts = ln->add_child(ln->get_root(), Lnast_ntype::create_stmts());

  auto if_node = ln->add_child(stmts, Lnast_ntype::create_if());
  // condition
  ln->add_child(if_node, Lnast_node::create_ref("$cond"));
  // then-stmts: %out = $a
  auto then_s = ln->add_child(if_node, Lnast_ntype::create_stmts());
  auto asgn_t = ln->add_child(then_s, Lnast_ntype::create_assign());
  ln->add_child(asgn_t, Lnast_node::create_ref("%out"));
  ln->add_child(asgn_t, Lnast_node::create_ref("$a"));
  // else-stmts: %out = $b
  auto else_s = ln->add_child(if_node, Lnast_ntype::create_stmts());
  auto asgn_e = ln->add_child(else_s, Lnast_ntype::create_assign());
  ln->add_child(asgn_e, Lnast_node::create_ref("%out"));
  ln->add_child(asgn_e, Lnast_node::create_ref("$b"));

  auto* lg = make_lg("if8");
  ASSERT_NE(lg, nullptr);
  Lnast_to_lgraph lowerer(lg, ln);
  lowerer.lower();

  EXPECT_EQ(count_op(lg, Ntype_op::Mux), 1) << "expected exactly one Mux node";
  EXPECT_TRUE(has_output(lg, "out")) << "expected graph output 'out'";
  EXPECT_TRUE(has_input(lg, "cond")) << "expected graph input 'cond'";
  EXPECT_TRUE(has_input(lg, "a")) << "expected graph input 'a'";
  EXPECT_TRUE(has_input(lg, "b")) << "expected graph input 'b'";
}

// ── Test 9: if without else → Mux with incoming value as false-path ──────────
// LNAST:
//   %out = $default
//   if $cond { %out = $a }
// Expected:
//   one Mux: pin0=$cond, pin1=$default (pre-if value), pin2=$a
TEST(LnastToLgraph, IfNoElse) {
  auto ln = std::make_shared<Lnast>("if_no_else");
  ln->set_root(Lnast_ntype::create_top());
  auto stmts = ln->add_child(ln->get_root(), Lnast_ntype::create_stmts());

  // %out = $default   (establishes pre-if value)
  auto asgn0 = ln->add_child(stmts, Lnast_ntype::create_assign());
  ln->add_child(asgn0, Lnast_node::create_ref("%out"));
  ln->add_child(asgn0, Lnast_node::create_ref("$default"));

  auto if_node = ln->add_child(stmts, Lnast_ntype::create_if());
  ln->add_child(if_node, Lnast_node::create_ref("$cond"));
  auto then_s = ln->add_child(if_node, Lnast_ntype::create_stmts());
  auto asgn_t = ln->add_child(then_s, Lnast_ntype::create_assign());
  ln->add_child(asgn_t, Lnast_node::create_ref("%out"));
  ln->add_child(asgn_t, Lnast_node::create_ref("$a"));

  auto* lg = make_lg("ifne9");
  ASSERT_NE(lg, nullptr);
  Lnast_to_lgraph lowerer(lg, ln);
  lowerer.lower();

  EXPECT_EQ(count_op(lg, Ntype_op::Mux), 1) << "expected exactly one Mux node";
  EXPECT_TRUE(has_output(lg, "out")) << "expected graph output 'out'";
}

// ── Test 11: func_def lowers trivial XOR function ────────────────────────────
// Mimics the LNAST produced by prp2lnast for:
//   comb trivial(a:u1, b:u1)->(c:u1) { c = a ^ b }
// Structure:
//   top → stmts → func_def(ref"trivial", const"comb",
//                           tuple_add/*generics*/, tuple_add/*captures*/,
//                           tuple_add(assign(ref"a",const"0"), assign(ref"b",const"0")),
//                           tuple_add(assign(ref"c",const"0")),
//                           stmts(bit_xor(ref"___t1",ref"a",ref"b"),
//                                 assign(ref"c",ref"___t1")))
// Expected: one Xor node, graph inputs a+b, graph output c.
static std::shared_ptr<Lnast> make_trivial_func_def() {
  auto ln = std::make_shared<Lnast>("trivial");
  ln->set_root(Lnast_ntype::create_top());
  auto stmts = ln->add_child(ln->get_root(), Lnast_ntype::create_stmts());

  auto fd = ln->add_child(stmts, Lnast_ntype::create_func_def());
  // child0: name
  ln->add_child(fd, Lnast_node::create_ref("trivial"));
  // child1: kind
  ln->add_child(fd, Lnast_node::create_const("comb"));
  // child2: generics (empty tuple_add)
  ln->add_child(fd, Lnast_ntype::create_tuple_add());
  // child3: captures (empty tuple_add)
  ln->add_child(fd, Lnast_ntype::create_tuple_add());
  // child4: inputs tuple_add
  auto in_ta = ln->add_child(fd, Lnast_ntype::create_tuple_add());
  auto ai_a  = ln->add_child(in_ta, Lnast_ntype::create_assign());
  ln->add_child(ai_a, Lnast_node::create_ref("a"));
  ln->add_child(ai_a, Lnast_node::create_const("0"));
  auto ai_b = ln->add_child(in_ta, Lnast_ntype::create_assign());
  ln->add_child(ai_b, Lnast_node::create_ref("b"));
  ln->add_child(ai_b, Lnast_node::create_const("0"));
  // child5: outputs tuple_add
  auto out_ta = ln->add_child(fd, Lnast_ntype::create_tuple_add());
  auto ao_c   = ln->add_child(out_ta, Lnast_ntype::create_assign());
  ln->add_child(ao_c, Lnast_node::create_ref("c"));
  ln->add_child(ao_c, Lnast_node::create_const("0"));
  // child6: body stmts — c = a ^ b via temp ref ___t1
  auto body = ln->add_child(fd, Lnast_ntype::create_stmts());
  auto xorN = ln->add_child(body, Lnast_ntype::create_bit_xor());
  ln->add_child(xorN, Lnast_node::create_ref("___t1"));
  ln->add_child(xorN, Lnast_node::create_ref("a"));
  ln->add_child(xorN, Lnast_node::create_ref("b"));
  auto asgn = ln->add_child(body, Lnast_ntype::create_assign());
  ln->add_child(asgn, Lnast_node::create_ref("c"));
  ln->add_child(asgn, Lnast_node::create_ref("___t1"));
  return ln;
}

// ── Test 10: if/elif/else → two Mux nodes ────────────────────────────────────
// LNAST:
//   if $c1 { %out = $a } elif $c2 { %out = $b } else { %out = $d }
// Expected: two Mux nodes chained
//   tmp = mux($c2, $d, $b)
//   out = mux($c1, tmp, $a)
TEST(LnastToLgraph, IfElifElseChain) {
  auto ln = std::make_shared<Lnast>("if_elif_else");
  ln->set_root(Lnast_ntype::create_top());
  auto stmts = ln->add_child(ln->get_root(), Lnast_ntype::create_stmts());

  auto if_node = ln->add_child(stmts, Lnast_ntype::create_if());
  // if $c1
  ln->add_child(if_node, Lnast_node::create_ref("$c1"));
  // then: %out = $a
  auto then_s = ln->add_child(if_node, Lnast_ntype::create_stmts());
  auto asgn_t = ln->add_child(then_s, Lnast_ntype::create_assign());
  ln->add_child(asgn_t, Lnast_node::create_ref("%out"));
  ln->add_child(asgn_t, Lnast_node::create_ref("$a"));
  // elif $c2
  ln->add_child(if_node, Lnast_node::create_ref("$c2"));
  // elif body: %out = $b
  auto elif_s = ln->add_child(if_node, Lnast_ntype::create_stmts());
  auto asgn_e = ln->add_child(elif_s, Lnast_ntype::create_assign());
  ln->add_child(asgn_e, Lnast_node::create_ref("%out"));
  ln->add_child(asgn_e, Lnast_node::create_ref("$b"));
  // else: %out = $d
  auto else_s  = ln->add_child(if_node, Lnast_ntype::create_stmts());
  auto asgn_el = ln->add_child(else_s, Lnast_ntype::create_assign());
  ln->add_child(asgn_el, Lnast_node::create_ref("%out"));
  ln->add_child(asgn_el, Lnast_node::create_ref("$d"));

  auto* lg = make_lg("ifee10");
  ASSERT_NE(lg, nullptr);
  Lnast_to_lgraph lowerer(lg, ln);
  lowerer.lower();

  EXPECT_EQ(count_op(lg, Ntype_op::Mux), 2) << "expected two chained Mux nodes";
  EXPECT_TRUE(has_output(lg, "out")) << "expected graph output 'out'";
}

// ── Test 12: post-upass shape — trivial XOR function ─────────────────────────
// Mimics what upass/func_extract.cpp produces for trivial.prp:
//   top
//     io
//       ref __input_tuple_ref
//       ref __output_tuple_ref
//     stmts
//       tuple_add(__input_tuple_ref) { assign(a, "nil"), assign(b, "nil") }
//       tuple_add(__output_tuple_ref) { assign(c, "nil") }
//       bit_xor(___t1, a, b)
//       assign(c, ___t1)
// Expected: one Xor, inputs a+b (1-based positions per §5), output c.
static std::shared_ptr<Lnast> make_trivial_post_upass() {
  auto ln   = std::make_shared<Lnast>("trivial");
  auto root = ln->set_root(Lnast_ntype::create_top());

  auto io = ln->add_child(root, Lnast_ntype::create_io());
  ln->add_child(io, Lnast_node::create_ref("__input_tuple_ref"));
  ln->add_child(io, Lnast_node::create_ref("__output_tuple_ref"));

  auto stmts = ln->add_child(root, Lnast_ntype::create_stmts());

  // Inputs tuple_add
  auto in_ta = ln->add_child(stmts, Lnast_ntype::create_tuple_add());
  ln->add_child(in_ta, Lnast_node::create_ref("__input_tuple_ref"));
  auto ai_a = ln->add_child(in_ta, Lnast_ntype::create_assign());
  ln->add_child(ai_a, Lnast_node::create_ref("a"));
  ln->add_child(ai_a, Lnast_node::create_const("nil"));
  auto ai_b = ln->add_child(in_ta, Lnast_ntype::create_assign());
  ln->add_child(ai_b, Lnast_node::create_ref("b"));
  ln->add_child(ai_b, Lnast_node::create_const("nil"));

  // Outputs tuple_add
  auto out_ta = ln->add_child(stmts, Lnast_ntype::create_tuple_add());
  ln->add_child(out_ta, Lnast_node::create_ref("__output_tuple_ref"));
  auto ao_c = ln->add_child(out_ta, Lnast_ntype::create_assign());
  ln->add_child(ao_c, Lnast_node::create_ref("c"));
  ln->add_child(ao_c, Lnast_node::create_const("nil"));

  // Body: bit_xor(___t1, a, b); assign(c, ___t1)
  auto xorN = ln->add_child(stmts, Lnast_ntype::create_bit_xor());
  ln->add_child(xorN, Lnast_node::create_ref("___t1"));
  ln->add_child(xorN, Lnast_node::create_ref("a"));
  ln->add_child(xorN, Lnast_node::create_ref("b"));
  auto asgn = ln->add_child(stmts, Lnast_ntype::create_assign());
  ln->add_child(asgn, Lnast_node::create_ref("c"));
  ln->add_child(asgn, Lnast_node::create_ref("___t1"));

  return ln;
}

TEST(LnastToLgraph, PostUpassTrivialXor) {
  auto* lg = make_lg("trivial_post12");
  ASSERT_NE(lg, nullptr);
  Lnast_to_lgraph lowerer(lg, make_trivial_post_upass());
  lowerer.lower();

  EXPECT_EQ(count_op(lg, Ntype_op::Xor), 1) << "expected exactly one Xor node";
  EXPECT_TRUE(has_input(lg, "a")) << "expected graph input 'a'";
  EXPECT_TRUE(has_input(lg, "b")) << "expected graph input 'b'";
  EXPECT_TRUE(has_output(lg, "c")) << "expected graph output 'c'";
  // No body tuple_add residue; no extra Sum/Mux/Or nodes.
  EXPECT_EQ(count_op(lg, Ntype_op::Sum), 0);
  EXPECT_EQ(count_op(lg, Ntype_op::Mux), 0);
}

// ── Test 13: post-upass with __empty_tuple placeholder for outputs ────────────
// Verifies the empty-tuple placeholder is skipped without creating an output.
TEST(LnastToLgraph, PostUpassEmptyOutputTuple) {
  auto ln   = std::make_shared<Lnast>("only_inputs");
  auto root = ln->set_root(Lnast_ntype::create_top());

  auto io = ln->add_child(root, Lnast_ntype::create_io());
  ln->add_child(io, Lnast_node::create_ref("__input_tuple_ref"));
  ln->add_child(io, Lnast_node::create_ref("__empty_tuple"));

  auto stmts = ln->add_child(root, Lnast_ntype::create_stmts());

  auto in_ta = ln->add_child(stmts, Lnast_ntype::create_tuple_add());
  ln->add_child(in_ta, Lnast_node::create_ref("__input_tuple_ref"));
  auto ai_x = ln->add_child(in_ta, Lnast_ntype::create_assign());
  ln->add_child(ai_x, Lnast_node::create_ref("x"));
  ln->add_child(ai_x, Lnast_node::create_const("nil"));

  // Empty placeholder tuple_add
  auto empty = ln->add_child(stmts, Lnast_ntype::create_tuple_add());
  ln->add_child(empty, Lnast_node::create_ref("__empty_tuple"));

  auto* lg = make_lg("only_inputs13");
  ASSERT_NE(lg, nullptr);
  Lnast_to_lgraph lowerer(lg, ln);
  lowerer.lower();

  EXPECT_TRUE(has_input(lg, "x"));
  // No outputs registered.
  bool any_output = false;
  lg->each_graph_output([&](const Node_pin&) { any_output = true; });
  EXPECT_FALSE(any_output) << "expected no graph outputs";
}

// ── Test 11: func_def — trivial XOR function ──────────────────────────────────
TEST(LnastToLgraph, FuncDefTrivialXor) {
  auto* lg = make_lg("trivial11");
  ASSERT_NE(lg, nullptr);
  Lnast_to_lgraph lowerer(lg, make_trivial_func_def());
  lowerer.lower();

  EXPECT_EQ(count_op(lg, Ntype_op::Xor), 1) << "expected exactly one Xor node";
  EXPECT_TRUE(has_input(lg, "a")) << "expected graph input 'a'";
  EXPECT_TRUE(has_input(lg, "b")) << "expected graph input 'b'";
  EXPECT_TRUE(has_output(lg, "c")) << "expected graph output 'c'";
  // No spurious Sum/Mux nodes from func_def scaffolding.
  EXPECT_EQ(count_op(lg, Ntype_op::Sum), 0);
  EXPECT_EQ(count_op(lg, Ntype_op::Mux), 0);
}

// ── Test 14: SSA renaming — multi-assigned user variable ──────────────────────
// Verifies that uPass_ssa::run() correctly renames a variable assigned twice
// in straight-line code, and that the resulting LNAST lowers cleanly to LGraph.
//
// Source (conceptual Pyrope):
//   comb f(a:u4, b:u4) -> (c:u4) {
//     t = a + b      // first assignment
//     t = t + 1      // second assignment → renamed t___ssa_1 after SSA
//     c = t          // reads t___ssa_1
//   }
//
// Expected LGraph: 2 Sum nodes (one per addition), inputs a & b, output c.
// The SSA rename ensures the second plus uses the result of the first, not
// a fresh graph input.
TEST(LnastToLgraph, SsaRenameMultiAssign) {
  // Build post-func_extract LNAST:
  //   top → io(ref __input_tuple_ref, ref __output_tuple_ref)
  //        → stmts(
  //            tuple_add(__input_tuple_ref){ assign(a,nil), assign(b,nil) }
  //            tuple_add(__output_tuple_ref){ assign(c,nil) }
  //            plus(t, a, b)         // t = a + b
  //            plus(t, t, 1)         // t = t + 1  (multi-assign → gets renamed)
  //            assign(c, t)          // c = t      (reads renamed t)
  //          )
  auto ln   = std::make_shared<Lnast>("ssa_multiassign");
  auto root = ln->set_root(Lnast_ntype::create_top());

  auto io = ln->add_child(root, Lnast_ntype::create_io());
  ln->add_child(io, Lnast_node::create_ref("__input_tuple_ref"));
  ln->add_child(io, Lnast_node::create_ref("__output_tuple_ref"));

  auto stmts = ln->add_child(root, Lnast_ntype::create_stmts());

  // Inputs tuple_add
  auto in_ta = ln->add_child(stmts, Lnast_ntype::create_tuple_add());
  ln->add_child(in_ta, Lnast_node::create_ref("__input_tuple_ref"));
  auto ai_a = ln->add_child(in_ta, Lnast_ntype::create_assign());
  ln->add_child(ai_a, Lnast_node::create_ref("a"));
  ln->add_child(ai_a, Lnast_node::create_const("nil"));
  auto ai_b = ln->add_child(in_ta, Lnast_ntype::create_assign());
  ln->add_child(ai_b, Lnast_node::create_ref("b"));
  ln->add_child(ai_b, Lnast_node::create_const("nil"));

  // Outputs tuple_add
  auto out_ta = ln->add_child(stmts, Lnast_ntype::create_tuple_add());
  ln->add_child(out_ta, Lnast_node::create_ref("__output_tuple_ref"));
  auto ao_c = ln->add_child(out_ta, Lnast_ntype::create_assign());
  ln->add_child(ao_c, Lnast_node::create_ref("c"));
  ln->add_child(ao_c, Lnast_node::create_const("nil"));

  // plus(t, a, b)  →  t = a + b
  auto p1 = ln->add_child(stmts, Lnast_ntype::create_plus());
  ln->add_child(p1, Lnast_node::create_ref("t"));
  ln->add_child(p1, Lnast_node::create_ref("a"));
  ln->add_child(p1, Lnast_node::create_ref("b"));

  // plus(t, t, 1)  →  t = t + 1  (second assignment to t — SSA renames this)
  auto p2 = ln->add_child(stmts, Lnast_ntype::create_plus());
  ln->add_child(p2, Lnast_node::create_ref("t"));
  ln->add_child(p2, Lnast_node::create_ref("t"));
  ln->add_child(p2, Lnast_node::create_const("1"));

  // assign(c, t)   →  c = t  (reads the SSA-renamed version of t)
  auto asgn = ln->add_child(stmts, Lnast_ntype::create_assign());
  ln->add_child(asgn, Lnast_node::create_ref("c"));
  ln->add_child(asgn, Lnast_node::create_ref("t"));

  // Run the SSA pass: should rename the second `t` to `t___ssa_1` and
  // substitute the RHS `t` in the third statement with `t___ssa_1`.
  uPass_ssa::run(ln);

  // Verify io_meta was harvested correctly.
  ASSERT_EQ(ln->io_meta().inputs.size(), 2u);
  EXPECT_EQ(ln->io_meta().inputs[0].name, "a");
  EXPECT_EQ(ln->io_meta().inputs[1].name, "b");
  ASSERT_EQ(ln->io_meta().outputs.size(), 1u);
  EXPECT_EQ(ln->io_meta().outputs[0].name, "c");

  // Lower to LGraph via the io_meta path.
  auto* lg = make_lg("ssa_multiassign14");
  ASSERT_NE(lg, nullptr);
  Lnast_to_lgraph lowerer(lg, ln);
  lowerer.lower();

  // Expect exactly 2 Sum nodes (one for a+b, one for t+1).
  EXPECT_EQ(count_op(lg, Ntype_op::Sum), 2) << "expected two Sum nodes";
  EXPECT_TRUE(has_input(lg, "a")) << "expected graph input 'a'";
  EXPECT_TRUE(has_input(lg, "b")) << "expected graph input 'b'";
  EXPECT_TRUE(has_output(lg, "c")) << "expected graph output 'c'";
  // No Mux or Xor nodes expected.
  EXPECT_EQ(count_op(lg, Ntype_op::Mux), 0);
  EXPECT_EQ(count_op(lg, Ntype_op::Xor), 0);
}
