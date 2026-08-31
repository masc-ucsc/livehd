//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_runner.hpp"

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "lnast.hpp"
#include "lnast_manager.hpp"
#include "upass_core.hpp"

namespace {

struct Test_cycle_a : public upass::uPass {
  using uPass::uPass;
};

struct Test_cycle_b : public upass::uPass {
  using uPass::uPass;
};

struct Test_missing_dep : public upass::uPass {
  using uPass::uPass;
};

static upass::uPass_plugin plugin_cycle_a("__upass_cycle_a", upass::uPass_wrapper<Test_cycle_a>::get_upass, {"__upass_cycle_b"});

static upass::uPass_plugin plugin_cycle_b("__upass_cycle_b", upass::uPass_wrapper<Test_cycle_b>::get_upass, {"__upass_cycle_a"});

static upass::uPass_plugin plugin_missing_dep("__upass_missing_dep", upass::uPass_wrapper<Test_missing_dep>::get_upass,
                                              {"__upass_dep_not_defined"});

class Exposed_runner : public uPass_runner {
public:
  Exposed_runner(std::shared_ptr<upass::Lnast_manager>& _lm, const std::vector<std::string>& names) : uPass_runner(_lm, names) {}

  std::vector<std::string> expose_resolve(const std::vector<std::string>& names) const { return resolve_order(names); }
};

std::shared_ptr<upass::Lnast_manager> make_lm() {
  auto ln = std::make_shared<Lnast>("upass_runner_test");
  return std::make_shared<upass::Lnast_manager>(ln);
}

}  // namespace

TEST(UpassRunnerResolve, DetectCycle) {
  auto           lm = make_lm();
  Exposed_runner runner(lm, {});

  auto ordered = runner.expose_resolve({"__upass_cycle_a"});
  EXPECT_TRUE(ordered.empty());
}

TEST(UpassRunnerResolve, DetectMissingDependency) {
  auto           lm = make_lm();
  Exposed_runner runner(lm, {});

  auto ordered = runner.expose_resolve({"__upass_missing_dep"});
  EXPECT_TRUE(ordered.empty());
}

TEST(UpassRunnerResolve, NoopResolvesAndRuns) {
  auto           lm = make_lm();
  Exposed_runner runner(lm, {"noop"});

  auto ordered = runner.expose_resolve({"noop"});
  ASSERT_EQ(ordered.size(), 1U);
  EXPECT_EQ(ordered[0], "noop");
  EXPECT_FALSE(runner.has_configuration_error());
}

// ── Slice 7: if-branch pruning ────────────────────────────────────────────────
//
// Helper: count the number of nodes of a given ntype in the staging LNAST.
static size_t count_ntype(const Lnast& ln, Lnast_ntype::Lnast_ntype_int target) {
  size_t n = 0;
  for (const auto& nid : ln.depth_preorder(ln.get_root())) {
    if (ln.get_type(nid) == target) {
      ++n;
    }
  }
  return n;
}

// Helper: return true if any ref node in `ln` carries exactly `tok` as its text.
static bool has_ref_tok(const Lnast& ln, std::string_view tok) {
  for (const auto& nid : ln.depth_preorder(ln.get_root())) {
    if (ln.get_type(nid) == Lnast_ntype::Lnast_ntype_ref && ln.get_name(nid) == tok) {
      return true;
    }
  }
  return false;
}

// Build a minimal LNAST:
//   top → stmts → if(cond, then_stmts[assign(out, then_ref)], [else_stmts[assign(alt, else_ref)]])
//
// Then- and else-branches write to distinct variables (`out` vs `alt`) so we
// can verify WHICH branch was spliced into the staging tree after dead-branch
// elimination. The RHS is an undeclared ref so constprop cannot fold the
// assign to a known constant and DCE it.
//
// `cond_text` is the condition literal ("true", "false", or a ref like "___c").
// `else_ref`  is "" when there is no else branch.
static std::shared_ptr<Lnast> make_if_lnast(std::string_view cond_text, std::string_view then_ref = "then_v",
                                            std::string_view else_ref = "") {
  auto ln = std::make_shared<Lnast>("if_test");
  ln->set_root(Lnast_ntype::create_top());
  auto stmts  = ln->add_child(ln->get_root(), Lnast_ntype::create_stmts());
  auto if_nid = ln->add_child(stmts, Lnast_ntype::create_if());

  // Condition — emit as const ("true"/"false") or ref (unknown variable).
  bool is_literal = (cond_text == "true" || cond_text == "false");
  if (is_literal) {
    ln->add_child(if_nid, Lnast_node::create_const(cond_text));
  } else {
    ln->add_child(if_nid, Lnast_node::create_ref(cond_text));
  }

  // Then-stmts: assigns `out = <then_ref>` where `then_ref` is undeclared.
  auto then_s = ln->add_child(if_nid, Lnast_ntype::create_stmts());
  auto asgn1  = ln->add_child(then_s, Lnast_ntype::create_store());
  ln->add_child(asgn1, Lnast_node::create_ref("out"));
  ln->add_child(asgn1, Lnast_node::create_ref(then_ref));

  // Optional else-stmts: assigns `alt = <else_ref>` (distinct variable).
  if (!else_ref.empty()) {
    auto else_s = ln->add_child(if_nid, Lnast_ntype::create_stmts());
    auto asgn2  = ln->add_child(else_s, Lnast_ntype::create_store());
    ln->add_child(asgn2, Lnast_node::create_ref("alt"));
    ln->add_child(asgn2, Lnast_node::create_ref(else_ref));
  }

  return ln;
}

// ── Test 1: condition is const "true" → if is pruned, then-branch spliced ───
TEST(UpassRunnerIfPrune, TrueConditionPrunesIfNode) {
  auto         ln = make_if_lnast("true");
  auto         lm = std::make_shared<upass::Lnast_manager>(ln);
  uPass_runner runner(lm, {"constprop"});
  runner.run();
  auto staging = runner.take_staging();
  ASSERT_NE(staging, nullptr);
  // No if-node should remain — the then-branch (out = then_v) was spliced into parent.
  EXPECT_EQ(count_ntype(*staging, Lnast_ntype::Lnast_ntype_if), 0U);
  // The then-branch lhs (out) must be present; the else lhs (alt) must not.
  EXPECT_TRUE(has_ref_tok(*staging, "out")) << "then-branch out missing from staging";
  EXPECT_FALSE(has_ref_tok(*staging, "alt")) << "dead else-branch alt leaked into staging";
}

// ── Test 2: condition is const "false" → if is pruned, no output emitted ────
TEST(UpassRunnerIfPrune, FalseConditionPrunesIfNodeNoElse) {
  auto         ln = make_if_lnast("false");
  auto         lm = std::make_shared<upass::Lnast_manager>(ln);
  uPass_runner runner(lm, {"constprop"});
  runner.run();
  auto staging = runner.take_staging();
  ASSERT_NE(staging, nullptr);
  // No if-node and no store — the entire if was dropped (no else branch).
  // (The in-branch writes are `store` now; `assign` was deleted.)
  EXPECT_EQ(count_ntype(*staging, Lnast_ntype::Lnast_ntype_if), 0U);
  EXPECT_EQ(count_ntype(*staging, Lnast_ntype::Lnast_ntype_store), 0U);
}

// ── Test 3: condition false + else branch → else-stmts spliced into parent ──
TEST(UpassRunnerIfPrune, FalseConditionSplicesElseBranch) {
  auto         ln = make_if_lnast("false", "then_v", "else_v");
  auto         lm = std::make_shared<upass::Lnast_manager>(ln);
  uPass_runner runner(lm, {"constprop"});
  runner.run();
  auto staging = runner.take_staging();
  ASSERT_NE(staging, nullptr);
  // No if-node — else-branch (alt = else_v) was spliced into the parent stmts.
  EXPECT_EQ(count_ntype(*staging, Lnast_ntype::Lnast_ntype_if), 0U);
  // The else-branch lhs (alt) must be present; the dead then lhs (out) must not.
  EXPECT_TRUE(has_ref_tok(*staging, "alt")) << "else-branch alt missing from staging";
  EXPECT_FALSE(has_ref_tok(*staging, "out")) << "dead then-branch out leaked into staging";
}

// ── Test 4: unknown condition → full if node is preserved ───────────────────
TEST(UpassRunnerIfPrune, UnknownConditionKeepsIfNode) {
  // ___c is never defined → constprop ST has no value for it → condition unknown.
  auto         ln = make_if_lnast("___c");
  auto         lm = std::make_shared<upass::Lnast_manager>(ln);
  uPass_runner runner(lm, {"constprop"});
  runner.run();
  auto staging = runner.take_staging();
  ASSERT_NE(staging, nullptr);
  // The if-node must be preserved when the condition is unknown.
  EXPECT_GE(count_ntype(*staging, Lnast_ntype::Lnast_ntype_if), 1U);
}

// ── Test 5: ref condition resolved by constprop → if is pruned ───────────────
// This is the primary real-world path: the condition is a tmp ref (___cond)
// whose value is folded by constprop via a preceding eq statement.
//
// LNAST built:
//   top → stmts
//     assign:  ___x  = 3          ← known scalar
//     eq:      ___cond = ___x == 3 ← constprop folds to true
//     if(ref:___cond)
//       stmts → assign: ___a = 4
//
// After run(): constprop sets ___cond=true in its ST; runner calls
// try_fold_ref("___cond") → true → splices then-stmts, no if in staging.
TEST(UpassRunnerIfPrune, RefCondResolvedByConstpropPrunesIfNode) {
  auto ln = std::make_shared<Lnast>("if_ref_cond");
  ln->set_root(Lnast_ntype::create_top());
  auto stmts = ln->add_child(ln->get_root(), Lnast_ntype::create_stmts());

  // assign ___x = 3
  auto asgn_x = ln->add_child(stmts, Lnast_ntype::create_store());
  ln->add_child(asgn_x, Lnast_node::create_ref("___x"));
  ln->add_child(asgn_x, Lnast_node::create_const("3"));

  // eq ___cond = ___x == 3  →  constprop folds to true (1)
  auto eq_nid = ln->add_child(stmts, Lnast_ntype::create_eq());
  ln->add_child(eq_nid, Lnast_node::create_ref("___cond"));
  ln->add_child(eq_nid, Lnast_node::create_ref("___x"));
  ln->add_child(eq_nid, Lnast_node::create_const("3"));

  // if ___cond { assign ___a = 4 }
  auto if_nid = ln->add_child(stmts, Lnast_ntype::create_if());
  ln->add_child(if_nid, Lnast_node::create_ref("___cond"));
  auto then_s = ln->add_child(if_nid, Lnast_ntype::create_stmts());
  auto asgn_a = ln->add_child(then_s, Lnast_ntype::create_store());
  ln->add_child(asgn_a, Lnast_node::create_ref("___a"));
  ln->add_child(asgn_a, Lnast_node::create_const("4"));

  auto         lm = std::make_shared<upass::Lnast_manager>(ln);
  uPass_runner runner(lm, {"constprop"});
  runner.run();
  auto staging = runner.take_staging();
  ASSERT_NE(staging, nullptr);
  // ___cond was folded to true by constprop → runner must prune the if node.
  EXPECT_EQ(count_ntype(*staging, Lnast_ntype::Lnast_ntype_if), 0U);
}

TEST(UpassRunnerTuplePortStream, StaticReadBecomesDottedScalarBinding) {
  auto ln    = std::make_shared<Lnast>("tuple_port_read");
  auto root  = ln->set_root(Lnast_ntype::create_top());
  auto stmts = ln->add_child(root, Lnast_ntype::create_stmts());
  ln->io_meta().inputs.push_back(Lnast_io_entry{.name = "ar.x", .bits = 8});
  ln->io_meta().outputs.push_back(Lnast_io_entry{.name = "z", .bits = 8});

  auto alias = ln->add_child(stmts, Lnast_ntype::create_store());
  ln->add_child(alias, Lnast_node::create_ref("%port"));
  ln->add_child(alias, Lnast_node::create_ref("ar"));
  auto get = ln->add_child(stmts, Lnast_ntype::create_tuple_get());
  ln->add_child(get, Lnast_node::create_ref("%field"));
  ln->add_child(get, Lnast_node::create_ref("%port"));
  ln->add_child(get, Lnast_node::create_const("x"));
  auto out = ln->add_child(stmts, Lnast_ntype::create_store());
  ln->add_child(out, Lnast_node::create_ref("z"));
  ln->add_child(out, Lnast_node::create_ref("%field"));

  auto         lm = std::make_shared<upass::Lnast_manager>(ln);
  uPass_runner runner(lm, {"constprop"});
  runner.run();
  auto staging = runner.take_staging();
  ASSERT_NE(staging, nullptr);
  EXPECT_EQ(count_ntype(*staging, Lnast_ntype::Lnast_ntype_tuple_get), 0U);
  EXPECT_TRUE(has_ref_tok(*staging, "ar.x"));
}

TEST(UpassRunnerTuplePortStream, StaticOutputFieldWriteBecomesDottedScalarStore) {
  auto ln    = std::make_shared<Lnast>("tuple_port_write");
  auto root  = ln->set_root(Lnast_ntype::create_top());
  auto stmts = ln->add_child(root, Lnast_ntype::create_stmts());
  ln->io_meta().inputs.push_back(Lnast_io_entry{.name = "a", .bits = 8});
  ln->io_meta().outputs.push_back(Lnast_io_entry{.name = "p.x", .bits = 8});

  auto out = ln->add_child(stmts, Lnast_ntype::create_store());
  ln->add_child(out, Lnast_node::create_ref("p"));
  ln->add_child(out, Lnast_node::create_const("x"));
  ln->add_child(out, Lnast_node::create_ref("a"));

  auto         lm = std::make_shared<upass::Lnast_manager>(ln);
  uPass_runner runner(lm, {"constprop"});
  runner.run();
  auto staging = runner.take_staging();
  ASSERT_NE(staging, nullptr);
  EXPECT_TRUE(has_ref_tok(*staging, "p.x"));
  EXPECT_FALSE(has_ref_tok(*staging, "p"));
}

TEST(UpassRunnerSsaStream, RepeatedScalarDefinitionIsVersionedInOutput) {
  auto ln    = std::make_shared<Lnast>("stream_ssa");
  auto root  = ln->set_root(Lnast_ntype::create_top());
  auto stmts = ln->add_child(root, Lnast_ntype::create_stmts());
  ln->io_meta().inputs.push_back(Lnast_io_entry{.name = "a", .bits = 8});
  ln->io_meta().inputs.push_back(Lnast_io_entry{.name = "b", .bits = 8});
  ln->io_meta().outputs.push_back(Lnast_io_entry{.name = "z", .bits = 8});
  ln->set_stream_ssa(true);
  ln->set_stream_ssa_names({"x"});

  auto first = ln->add_child(stmts, Lnast_ntype::create_store());
  ln->add_child(first, Lnast_node::create_ref("x"));
  ln->add_child(first, Lnast_node::create_ref("a"));
  auto second = ln->add_child(stmts, Lnast_ntype::create_store());
  ln->add_child(second, Lnast_node::create_ref("x"));
  ln->add_child(second, Lnast_node::create_ref("b"));
  auto out = ln->add_child(stmts, Lnast_ntype::create_store());
  ln->add_child(out, Lnast_node::create_ref("z"));
  ln->add_child(out, Lnast_node::create_ref("x"));

  auto         lm = std::make_shared<upass::Lnast_manager>(ln);
  uPass_runner runner(lm, {"constprop"});
  runner.run();
  auto staging = runner.take_staging();
  ASSERT_NE(staging, nullptr);
  EXPECT_TRUE(has_ref_tok(*staging, "x___ssa_1"));
}

TEST(UpassRunnerDce, ManySiblingSubtreesRemainDumpableAfterPrune) {
  auto ln    = std::make_shared<Lnast>("dce_many_siblings");
  auto root  = ln->set_root(Lnast_ntype::create_top());
  auto stmts = ln->add_child(root, Lnast_ntype::create_stmts());
  ln->io_meta().inputs.push_back(Lnast_io_entry{.name = "a", .bits = 8});
  ln->io_meta().outputs.push_back(Lnast_io_entry{.name = "z", .bits = 8});
  for (int i = 0; i < 10000; ++i) {
    auto dead = ln->add_child(stmts, Lnast_ntype::create_store());
    ln->add_child(dead, Lnast_node::create_ref(std::format("%dead{}", i)));
    ln->add_child(dead, Lnast_node::create_ref("a"));
  }
  auto out = ln->add_child(stmts, Lnast_ntype::create_store());
  ln->add_child(out, Lnast_node::create_ref("z"));
  ln->add_child(out, Lnast_node::create_ref("a"));

  auto         lm = std::make_shared<upass::Lnast_manager>(ln);
  uPass_runner runner(lm, {"constprop"});
  runner.run();
  auto staging = runner.take_staging();
  ASSERT_NE(staging, nullptr);
  std::ostringstream dump;
  staging->dump(dump);
  EXPECT_FALSE(dump.str().empty());
  EXPECT_TRUE(has_ref_tok(*staging, "z"));
}
