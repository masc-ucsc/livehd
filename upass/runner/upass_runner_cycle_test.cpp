//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <memory>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "lnast.hpp"
#include "lnast_manager.hpp"
#include "upass_core.hpp"
#include "upass_runner.hpp"

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
  auto ln = std::make_shared<Lnast>("upass_runner_cycle_test");
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

TEST(UpassRunnerResolve, SharedNoopResolvesAndRuns) {
  auto           lm = make_lm();
  Exposed_runner runner(lm, {"noop_shared"});

  auto ordered = runner.expose_resolve({"noop_shared"});
  ASSERT_EQ(ordered.size(), 1U);
  EXPECT_EQ(ordered[0], "noop_shared");
  EXPECT_FALSE(runner.has_configuration_error());
}

TEST(UpassRunnerResolve, SharedScanResolves) {
  auto           lm = make_lm();
  Exposed_runner runner(lm, {"scan_shared"});

  auto ordered = runner.expose_resolve({"scan_shared"});
  ASSERT_EQ(ordered.size(), 1U);
  EXPECT_EQ(ordered[0], "scan_shared");
  EXPECT_FALSE(runner.has_configuration_error());
}

TEST(UpassRunnerResolve, SharedDecideResolves) {
  auto           lm = make_lm();
  Exposed_runner runner(lm, {"decide_shared"});

  auto ordered = runner.expose_resolve({"decide_shared"});
  ASSERT_EQ(ordered.size(), 1U);
  EXPECT_EQ(ordered[0], "decide_shared");
  EXPECT_FALSE(runner.has_configuration_error());
}

// ── Slice 7: if-branch pruning ────────────────────────────────────────────────
//
// Helper: count the number of nodes of a given ntype in the staging LNAST.
static size_t count_ntype(const Lnast& ln, Lnast_ntype::Lnast_ntype_int target) {
  size_t n = 0;
  for (const auto& nid : ln.depth_preorder(Lnast_nid::root())) {
    if (ln.get_type(nid).get_raw_ntype() == target) {
      ++n;
    }
  }
  return n;
}

// Build a minimal LNAST:
//   top → stmts → if(cond, then_stmts[assign(%out, val)], [else_stmts[assign(%alt, else_val)]])
//
// Then- and else-branches write to *distinct* observable output ports (%out vs
// %alt) so constprop cannot silently drop them (ports are never DCE'd as tmps).
// This makes it possible to verify WHICH branch was spliced into the staging
// tree after dead-branch elimination.
//
// `cond_text` is the condition literal ("true", "false", or a ref like "___c").
// `else_val`  is "" when there is no else branch.
static std::shared_ptr<Lnast> make_if_lnast(std::string_view cond_text,
                                             std::string_view then_val  = "4",
                                             std::string_view else_val  = "") {
  auto ln = std::make_shared<Lnast>("if_test");
  ln->set_root(Lnast_node::create_top());
  auto stmts = ln->add_child(Lnast_nid::root(), Lnast_node::create_stmts());
  auto if_nid = ln->add_child(stmts, Lnast_node::create_if());

  // Condition — emit as const ("true"/"false") or ref (unknown variable).
  bool is_literal = (cond_text == "true" || cond_text == "false");
  if (is_literal) {
    ln->add_child(if_nid, Lnast_node::create_const(cond_text));
  } else {
    ln->add_child(if_nid, Lnast_node::create_ref(cond_text));
  }

  // Then-stmts: assigns to %out (observable output port, never DCE'd).
  auto then_s = ln->add_child(if_nid, Lnast_node::create_stmts());
  auto asgn1  = ln->add_child(then_s, Lnast_node::create_assign());
  ln->add_child(asgn1, Lnast_node::create_ref("%out"));
  ln->add_child(asgn1, Lnast_node::create_const(then_val));

  // Optional else-stmts: assigns to %alt (distinct port, never DCE'd).
  if (!else_val.empty()) {
    auto else_s = ln->add_child(if_nid, Lnast_node::create_stmts());
    auto asgn2  = ln->add_child(else_s, Lnast_node::create_assign());
    ln->add_child(asgn2, Lnast_node::create_ref("%alt"));
    ln->add_child(asgn2, Lnast_node::create_const(else_val));
  }

  return ln;
}

// ── Test 1: condition is const "true" → if is pruned, then-branch spliced ───
TEST(UpassRunnerIfPrune, TrueConditionPrunesIfNode) {
  auto ln     = make_if_lnast("true");
  auto lm     = std::make_shared<upass::Lnast_manager>(ln);
  uPass_runner runner(lm, {"constprop"});
  runner.run();
  auto staging = runner.take_staging();
  ASSERT_NE(staging, nullptr);
  // No if-node should remain — the then-branch (%out=4) was spliced into parent.
  EXPECT_EQ(count_ntype(*staging, Lnast_ntype::Lnast_ntype_if), 0U);
  // The then-branch assign (%out=4) must appear in the staging tree.
  EXPECT_GE(count_ntype(*staging, Lnast_ntype::Lnast_ntype_assign), 1U);
}

// ── Test 2: condition is const "false" → if is pruned, no output emitted ────
TEST(UpassRunnerIfPrune, FalseConditionPrunesIfNodeNoElse) {
  auto ln     = make_if_lnast("false");
  auto lm     = std::make_shared<upass::Lnast_manager>(ln);
  uPass_runner runner(lm, {"constprop"});
  runner.run();
  auto staging = runner.take_staging();
  ASSERT_NE(staging, nullptr);
  // No if-node and no assign — the entire if was dropped (no else branch).
  EXPECT_EQ(count_ntype(*staging, Lnast_ntype::Lnast_ntype_if), 0U);
  EXPECT_EQ(count_ntype(*staging, Lnast_ntype::Lnast_ntype_assign), 0U);
}

// ── Test 3: condition false + else branch → else-stmts spliced into parent ──
TEST(UpassRunnerIfPrune, FalseConditionSplicesElseBranch) {
  auto ln     = make_if_lnast("false", "4", "5");
  auto lm     = std::make_shared<upass::Lnast_manager>(ln);
  uPass_runner runner(lm, {"constprop"});
  runner.run();
  auto staging = runner.take_staging();
  ASSERT_NE(staging, nullptr);
  // No if-node — else-branch (%alt=5) was spliced into the parent stmts.
  EXPECT_EQ(count_ntype(*staging, Lnast_ntype::Lnast_ntype_if), 0U);
  // %alt is an output port, so it is not DCE'd; the assign must remain.
  EXPECT_GE(count_ntype(*staging, Lnast_ntype::Lnast_ntype_assign), 1U);
}

// ── Test 4: unknown condition → full if node is preserved ───────────────────
TEST(UpassRunnerIfPrune, UnknownConditionKeepsIfNode) {
  // ___c is never defined → constprop ST has no value for it → condition unknown.
  auto ln     = make_if_lnast("___c");
  auto lm     = std::make_shared<upass::Lnast_manager>(ln);
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
  ln->set_root(Lnast_node::create_top());
  auto stmts = ln->add_child(Lnast_nid::root(), Lnast_node::create_stmts());

  // assign ___x = 3
  auto asgn_x = ln->add_child(stmts, Lnast_node::create_assign());
  ln->add_child(asgn_x, Lnast_node::create_ref("___x"));
  ln->add_child(asgn_x, Lnast_node::create_const("3"));

  // eq ___cond = ___x == 3  →  constprop folds to true (1)
  auto eq_nid = ln->add_child(stmts, Lnast_node::create_eq());
  ln->add_child(eq_nid, Lnast_node::create_ref("___cond"));
  ln->add_child(eq_nid, Lnast_node::create_ref("___x"));
  ln->add_child(eq_nid, Lnast_node::create_const("3"));

  // if ___cond { assign ___a = 4 }
  auto if_nid   = ln->add_child(stmts, Lnast_node::create_if());
  ln->add_child(if_nid, Lnast_node::create_ref("___cond"));
  auto then_s   = ln->add_child(if_nid, Lnast_node::create_stmts());
  auto asgn_a   = ln->add_child(then_s, Lnast_node::create_assign());
  ln->add_child(asgn_a, Lnast_node::create_ref("___a"));
  ln->add_child(asgn_a, Lnast_node::create_const("4"));

  auto lm     = std::make_shared<upass::Lnast_manager>(ln);
  uPass_runner runner(lm, {"constprop"});
  runner.run();
  auto staging = runner.take_staging();
  ASSERT_NE(staging, nullptr);
  // ___cond was folded to true by constprop → runner must prune the if node.
  EXPECT_EQ(count_ntype(*staging, Lnast_ntype::Lnast_ntype_if), 0U);
}
