//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lnast_to_lgraph.hpp"

#include <memory>
#include <string>

#include "cell.hpp"
#include "graph_library.hpp"
#include "gtest/gtest.h"
#include "lgraph.hpp"
#include "lnast.hpp"

namespace {

// ── Helpers ──────────────────────────────────────────────────────────────────

// Create a fresh LGraph with a unique name so tests don't collide.
static Lgraph* make_lg(const std::string& name) {
  auto* lib = Graph_library::instance("lgdb_lnast_to_lgraph_test");
  return lib->create_lgraph(name, "lgdb_lnast_to_lgraph_test");
}

// Count non-IO nodes of a given op type in the graph.
static int count_op(Lgraph* lg, Ntype_op op) {
  int   n   = 0;
  auto  nid = lg->fast_first();
  while (nid) {
    auto node = Node(lg, nid);
    if (node.get_type_op() == op) {
      n++;
    }
    nid = lg->fast_next(nid);
  }
  return n;
}

// Count graph inputs by name.
static bool has_input(Lgraph* lg, const std::string& name) {
  bool found = false;
  lg->each_graph_input([&](const Node_pin& pin) {
    if (pin.has_name() && pin.get_name() == name) found = true;
  });
  return found;
}

// Count graph outputs by name.
static bool has_output(Lgraph* lg, const std::string& name) {
  bool found = false;
  lg->each_graph_output([&](const Node_pin& pin) {
    if (pin.has_name() && pin.get_name() == name) found = true;
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

  EXPECT_TRUE(has_input(lg, "a"))   << "expected graph input 'a'";
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
  EXPECT_EQ(count_op(lg, Ntype_op::Const), 1) << "expected exactly one Const node";
}

// ── Test 3: addition lowered to Sum node ─────────────────────────────────────
TEST(LnastToLgraph, Addition) {
  auto* lg = make_lg("add3");
  ASSERT_NE(lg, nullptr);
  Lnast_to_lgraph lowerer(lg, make_add());
  lowerer.lower();

  EXPECT_EQ(count_op(lg, Ntype_op::Sum), 1)  << "expected exactly one Sum node";
  EXPECT_TRUE(has_input(lg, "a"))              << "expected graph input 'a'";
  EXPECT_TRUE(has_input(lg, "b"))              << "expected graph input 'b'";
  EXPECT_TRUE(has_output(lg, "out"))           << "expected graph output 'out'";
}

// ── Test 4: comparison lowered to LT node ───────────────────────────────────
TEST(LnastToLgraph, LessThan) {
  auto* lg = make_lg("lt4");
  ASSERT_NE(lg, nullptr);
  Lnast_to_lgraph lowerer(lg, make_lt());
  lowerer.lower();

  EXPECT_EQ(count_op(lg, Ntype_op::LT), 1) << "expected exactly one LT node";
  EXPECT_TRUE(has_output(lg, "out"))         << "expected graph output 'out'";
}

// ── Test 5: bitwise AND lowered to And node ──────────────────────────────────
TEST(LnastToLgraph, BitAnd) {
  auto* lg = make_lg("and5");
  ASSERT_NE(lg, nullptr);
  Lnast_to_lgraph lowerer(lg, make_and());
  lowerer.lower();

  EXPECT_EQ(count_op(lg, Ntype_op::And), 1) << "expected exactly one And node";
  EXPECT_TRUE(has_output(lg, "out"))          << "expected graph output 'out'";
}

// ── Test 6: bitwise NOT lowered to Not node ──────────────────────────────────
TEST(LnastToLgraph, BitNot) {
  auto* lg = make_lg("not6");
  ASSERT_NE(lg, nullptr);
  Lnast_to_lgraph lowerer(lg, make_not());
  lowerer.lower();

  EXPECT_EQ(count_op(lg, Ntype_op::Not), 1) << "expected exactly one Not node";
  EXPECT_TRUE(has_input(lg, "a"))             << "expected graph input 'a'";
  EXPECT_TRUE(has_output(lg, "out"))          << "expected graph output 'out'";
}

// ── Test 7: multiplication lowered to Mult node ──────────────────────────────
TEST(LnastToLgraph, Multiply) {
  auto* lg = make_lg("mul7");
  ASSERT_NE(lg, nullptr);
  Lnast_to_lgraph lowerer(lg, make_mult());
  lowerer.lower();

  EXPECT_EQ(count_op(lg, Ntype_op::Mult), 1) << "expected exactly one Mult node";
  EXPECT_TRUE(has_output(lg, "out"))           << "expected graph output 'out'";
}
