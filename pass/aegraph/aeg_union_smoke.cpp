//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// The rule runtime (runtime/match.hpp): registry, counters, knobs, union nodes
// and the check= modes. It registers a few TEST-ONLY rules (group `uniontest`),
// so every test scopes the engine with rules_only to the rules it is about: the
// production rules never fire here unless a test asks for them.

#include <functional>
#include <string>
#include <vector>

#include "aegraph.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "runtime/match.hpp"

using namespace livehd::aegraph;
using namespace livehd::aegraph::rt;
namespace gu = livehd::graph_util;

AEG_RULE_GROUP(uniontest);

// ---- test-only rules ------------------------------------------------------------

namespace {
// The other operand of a two-operand Mult/Sum when one of them is the constant `k`.
std::optional<Class_id> other_than_const(Ctx& ctx, const Root& root, int64_t k) {
  if (root.ops.size() != 2) {
    return std::nullopt;
  }
  for (size_t i = 0; i < 2; ++i) {
    if (ctx.is_int(root.ops[i].cls, k) && !ctx.known(root.ops[i ^ 1].cls)) {
      return root.ops[i ^ 1].cls;
    }
  }
  return std::nullopt;
}
}  // namespace

// Mult(x, 3) alt Sum(SHL(x, 1), x): two e-nodes against one, so extraction keeps the Mult.
AEG_RULE(ut_mult3_alt, alt, 0, Ntype_op::Mult) {
  if (const auto x = other_than_const(ctx, root, 3)) {
    const auto shl = ctx.make(Ntype_op::SHL, {Operand{0, *x}, Operand{1, ctx.konst(1)}});
    out.add(ctx.make(Ntype_op::Sum, {Operand{0, shl}, Operand{0, *x}}));
  }
}

// A second alt on the same root: Mult(x, 3) alt Sum(Mult(x, 2), x).
AEG_RULE(ut_mult3_alt2, alt, -1, Ntype_op::Mult) {
  if (const auto x = other_than_const(ctx, root, 3)) {
    const auto twice = ctx.make(Ntype_op::Mult, {Operand{0, *x}, Operand{0, ctx.konst(2)}});
    out.add(ctx.make(Ntype_op::Sum, {Operand{0, twice}, Operand{0, *x}}));
  }
}

// Sum(SHL(x, 1), x) alt Mult(x, 3): one e-node against two, so extraction takes the Mult.
AEG_RULE(ut_sum_shl_alt, alt, 0, Ntype_op::Sum) {
  if (root.ops.size() != 2 || root.ops[0].bank != 0 || root.ops[1].bank != 0) {
    return;
  }
  for (size_t i = 0; i < 2; ++i) {
    const Class_id x = root.ops[i ^ 1].cls;
    if (ctx.each_member(root.ops[i].cls, [&](Class_id m) {
          const Enode* n = ctx.node_of(m);
          if (n == nullptr || n->op != Ntype_op::SHL) {
            return false;
          }
          const auto ops = ctx.operands_of(*n);
          return ops.size() == 2 && ops[0].cls == x && ctx.is_int(ops[1].cls, 1);
        })) {
      out.add(ctx.make(Ntype_op::Mult, {Operand{0, x}, Operand{0, ctx.konst(3)}}));
      return;
    }
  }
}

// A WRONG simplify: Rem(x, y) = 7. check=unions must catch it, naming the rule.
AEG_RULE(ut_wrong_rem, simplify, 0, Ntype_op::Rem) { out.subsume(ctx.konst(7)); }

// A right simplify with a lower priority than the wrong one, to see priority order.
AEG_RULE(ut_xor_self, simplify, 10, Ntype_op::Xor) {
  if (root.ops.size() == 2 && root.ops[0].cls == root.ops[1].cls) {
    out.subsume(ctx.konst(0));
  }
}

namespace {

std::shared_ptr<hhds::Graph> io_graph(hhds::GraphLibrary& lib, std::string_view name, int inputs, int outputs) {
  auto io  = lib.create_io(std::string{name});
  int  pid = 0;
  for (int i = 0; i < inputs; ++i) {
    io->add_input("i" + std::to_string(i), pid++);
    io->set_bits("i" + std::to_string(i), 8);
    io->set_unsign("i" + std::to_string(i), true);
  }
  for (int i = 0; i < outputs; ++i) {
    io->add_output("o" + std::to_string(i), pid++);
    io->set_bits("o" + std::to_string(i), 8);
    io->set_unsign("o" + std::to_string(i), true);
  }
  return io->create_graph();
}

hhds::Pin_class konst(const std::shared_ptr<hhds::Graph>& g, int64_t v) { return gu::create_const(*g, *Dlop::create_integer(v)); }

hhds::Pin_class binop(const std::shared_ptr<hhds::Graph>& g, Ntype_op op, const hhds::Pin_class& a, const hhds::Pin_class& b) {
  auto n = gu::create_typed_node(*g, op);
  a.connect_sink(gu::setup_sink_by_name(n, "as"));
  b.connect_sink(gu::setup_sink_by_name(n, "as"));
  gu::set_ubits(n.create_driver_pin(0), 8);
  return n.create_driver_pin(0);
}

// A positional two-operand cell (Rem, SHL, ...): operands a and b.
hhds::Pin_class posop(const std::shared_ptr<hhds::Graph>& g, Ntype_op op, const hhds::Pin_class& a, const hhds::Pin_class& b) {
  auto n = gu::create_typed_node(*g, op);
  a.connect_sink(gu::setup_sink_by_name(n, "a"));
  b.connect_sink(gu::setup_sink_by_name(n, "b"));
  gu::set_ubits(n.create_driver_pin(0), 8);
  return n.create_driver_pin(0);
}

hhds::Pin_class shift(const std::shared_ptr<hhds::Graph>& g, Ntype_op op, const hhds::Pin_class& a, int64_t amount) {
  auto n = gu::create_typed_node(*g, op);
  a.connect_sink(gu::setup_sink_by_name(n, "a"));
  konst(g, amount).connect_sink(gu::setup_sink_by_name(n, "b"));
  gu::set_ubits(n.create_driver_pin(0), 8);
  return n.create_driver_pin(0);
}

size_t count_op(const std::shared_ptr<hhds::Graph>& g, Ntype_op op) {
  size_t n = 0;
  for (auto node : g->body().nodes()) {
    n += gu::type_op_of(node) == op ? 1 : 0;
  }
  return n;
}

hhds::Pin_class out_driver(const std::shared_ptr<hhds::Graph>& g, const char* name) {
  return g->get_output_pin(name).get_driver_pin();
}

Options scoped(std::string only) {
  Options o;
  o.rules_only = std::move(only);
  o.check      = Options::Check::full;
  return o;
}

Rule_id id_of(std::string_view name) {
  const auto id = Registry::instance().find(name);
  EXPECT_TRUE(id.has_value()) << name;
  return id.value_or(0);
}

// ---- registry --------------------------------------------------------------------

TEST(AegUnion, RegistryIsSortedAndDispatchesByRootOp) {
  auto& reg = Registry::instance();
  ASSERT_GE(reg.size(), 28u) << "the 28 production rules (pattern and native) plus this file's test rules";
  // Priority descending, then group, then name: independent of static-initialization order.
  for (size_t id = 2; id <= reg.size(); ++id) {
    const auto& a = reg.rule(static_cast<Rule_id>(id - 1));
    const auto& b = reg.rule(static_cast<Rule_id>(id));
    if (a.prio == b.prio) {
      EXPECT_LE(std::string{a.group} + "/" + a.name, std::string{b.group} + "/" + b.name);
    } else {
      EXPECT_GT(a.prio, b.prio);
    }
  }
  for (size_t id = 1; id <= reg.size(); ++id) {
    const auto& info = reg.rule(static_cast<Rule_id>(id));
    EXPECT_FALSE(info.roots.empty()) << info.name;
    for (const auto op : info.roots) {
      const auto ids = reg.for_op(op);
      EXPECT_NE(std::ranges::find(ids, static_cast<Rule_id>(id)), ids.end()) << info.name;
    }
  }
  EXPECT_TRUE(reg.has_group("arith"));
  EXPECT_TRUE(reg.has_group("shift"));
  EXPECT_TRUE(reg.has_group("mux"));
  EXPECT_FALSE(reg.has_group("nonesuch"));
  EXPECT_FALSE(reg.find("nonesuch").has_value());
  // Every hand rule of the parity-gap list is there.
  for (const char* name : {"const_fold_bank", "annihilate", "ident_drop", "single_operand", "idempotent", "xor_pair_cancel",
                           "sum_cancel", "flatten", "or_absorb", "and_absorb", "bool_complement", "xor_bool_one", "mult_pow2_shl",
                           "not_not", "not_xor_alt", "cmp_same", "eq_bool_chain", "shift_zero", "shift_compose", "shl_shl",
                           "sra_sra", "sext_sext", "sext_getmask", "and_mask_get_mask", "mux_bool_const_arms", "mux_sel_arm",
                           "mux_sel_not", "mux_const_arm_logic"}) {
    EXPECT_TRUE(reg.find(name).has_value()) << name;
  }
}

TEST(AegUnion, OptionStringsAreValidated) {
  EXPECT_THROW(make_options("nonesuch", "", "", "", "", "", "", "", "", "", "", "", ""), std::runtime_error);
  EXPECT_THROW(make_options("group:nonesuch", "", "", "", "", "", "", "", "", "", "", "", ""), std::runtime_error);
  EXPECT_THROW(make_options("", "", "x", "", "", "", "", "", "", "", "", "", ""), std::runtime_error);
  EXPECT_THROW(make_options("", "", "", "", "", "sometimes", "", "", "", "", "", "", ""), std::runtime_error);
  EXPECT_THROW(make_options("", "", "", "", "", "", "", "", "99", "", "", "", ""), std::runtime_error);
  const auto o = make_options("flatten+group:mux", "", "7", "g", "flatten", "full", "3", "2", "4", "100", "9", "sim", "off");
  EXPECT_EQ(o.rules_disable, "flatten+group:mux");
  EXPECT_EQ(o.max_rewrites, 7u);
  EXPECT_EQ(o.check, Options::Check::full);
  EXPECT_EQ(o.rewrite_depth, 3u);
  EXPECT_EQ(o.matches_limit, 2u);
  EXPECT_EQ(o.eclass_limit, 4u);
  EXPECT_EQ(o.extractor_fuel, 100u);
  EXPECT_EQ(o.edge_budget, 9u);
  EXPECT_EQ(o.cost, Options::Cost::sim);
  EXPECT_EQ(make_options("", "", "", "", "", "", "", "", "", "", "", "auto", "").cost, Options::Cost::synth)
      << "auto is resolved by the kernel (sim under `lhd sim`); a bare pass.aegraph reads it as synth";
  EXPECT_EQ(Options{}.cost, Options::Cost::synth) << "the documented default";
  EXPECT_THROW(make_options("", "", "", "", "", "", "", "", "", "", "", "fast", ""), std::runtime_error);
  EXPECT_FALSE(o.choices);
  // The defaults are the Cranelift limits.
  const Options d;
  EXPECT_EQ(d.rewrite_depth, 5u);
  EXPECT_EQ(d.matches_limit, 5u);
  EXPECT_EQ(d.eclass_limit, 5u);
  EXPECT_EQ(d.extractor_fuel, 500u);
  EXPECT_EQ(d.max_rewrites, 0u);
  EXPECT_FALSE(d.choices);
}

// ---- the e-graph's union nodes ---------------------------------------------------

TEST(AegUnion, UnionsAndAliasesAreAcyclicAndOrdered) {
  Egraph eg;
  const auto a = eg.add_leaf(), b = eg.add_leaf();
  const Operand ops[2] = {{0, a}, {0, b}};
  bool          created = false;
  const auto    x       = eg.intern(Ntype_op::Sum, {}, ops, created);
  EXPECT_TRUE(created);
  const auto y = eg.intern(Ntype_op::Mult, {}, ops, created);
  const auto z = eg.intern(Ntype_op::Xor, {}, ops, created);
  const auto u = eg.add_union(x, y);
  const auto v = eg.add_union(u, z);
  EXPECT_EQ(eg.cls(u).members, 2u);
  EXPECT_EQ(eg.cls(v).members, 3u);
  std::vector<Class_id> seen;
  eg.for_each_member(v, 32, [&](Class_id m) {
    seen.push_back(m);
    return false;
  });
  EXPECT_EQ(seen, (std::vector<Class_id>{x, y, z})) << "the extended side first, then the alternatives in order";
  seen.clear();
  EXPECT_TRUE(eg.for_each_member(v, 32, [&](Class_id m) { return m == y; }));
  eg.for_each_member(v, 2, [&](Class_id m) {
    seen.push_back(m);
    return false;
  });
  EXPECT_EQ(seen.size(), 2u) << "the visit limit bounds the walk";

  // A rewrite leaves an alias: a later hash-cons hit reads the rewritten class.
  eg.set_alias(x, v);
  const auto again = eg.intern(Ntype_op::Sum, {}, ops, created);
  EXPECT_FALSE(created);
  EXPECT_EQ(again, v);
  EXPECT_EQ(eg.resolve(x), v);
  Class_id raw = kNoClass;
  (void)eg.intern(Ntype_op::Sum, {}, ops, created, &raw);
  EXPECT_EQ(raw, x);
}

// ---- alt rules, extraction and the limits -----------------------------------------

// Mult(x, 3) alt Sum(SHL(x,1), x): the alternative is bigger, so the graph keeps the Mult.
TEST(AegUnion, AnAltThatLosesLeavesTheGraphAlone) {
  hhds::GraphLibrary lib;
  auto               g = io_graph(lib, "alt_loses", 1, 1);
  auto               m = binop(g, Ntype_op::Mult, g->get_input_pin("i0"), konst(g, 3));
  m.connect_sink(g->get_output_pin("o0"));
  const auto stats = optimize(g, {}, scoped("ut_mult3_alt"));
  EXPECT_EQ(count_op(g, Ntype_op::Mult), 1u);
  EXPECT_EQ(count_op(g, Ntype_op::Sum), 0u);
  EXPECT_EQ(count_op(g, Ntype_op::SHL), 0u);
  EXPECT_EQ(stats.unions, 1u);
  const auto id = id_of("ut_mult3_alt");
  EXPECT_EQ(stats.rule_counts[id].tried, 1u);
  EXPECT_EQ(stats.rule_counts[id].matched, 1u);
  EXPECT_EQ(stats.rule_counts[id].applied, 1u);
  EXPECT_EQ(stats.rule_counts[id].subsumed, 0u);
  EXPECT_EQ(stats.rule_counts[id].extracted, 0u) << "built, but never extracted";
  EXPECT_EQ(stats.rules_fired, 1u);
}

// Sum(SHL(x,1), x) alt Mult(x, 3). The profiles disagree, which is the point of having two: `sim` prices a multiply by an
// immediate at one instruction (cost/sim.cost), so the alternative is strictly cheaper than the shift plus the add and is
// extracted; `synth` prices both at one adder, a tie, and a tie keeps the original.
namespace {
std::shared_ptr<hhds::Graph> sum_shl_graph(hhds::GraphLibrary& lib, const char* name) {
  auto g   = io_graph(lib, name, 1, 1);
  auto x   = g->get_input_pin("i0");
  auto shl = shift(g, Ntype_op::SHL, x, 1);
  auto sum = binop(g, Ntype_op::Sum, shl, x);
  sum.get_master_node().set_name("keep_my_name");
  sum.connect_sink(g->get_output_pin("o0"));
  return g;
}
}  // namespace

TEST(AegUnion, AnAltThatWinsUnderTheSimProfileIsExtracted) {
  hhds::GraphLibrary lib;
  auto               g = sum_shl_graph(lib, "alt_wins");
  auto               o = scoped("ut_sum_shl_alt");
  o.cost               = Options::Cost::sim;
  const auto stats     = optimize(g, {}, o);
  EXPECT_EQ(count_op(g, Ntype_op::Mult), 1u);
  EXPECT_EQ(count_op(g, Ntype_op::Sum), 0u);
  EXPECT_EQ(count_op(g, Ntype_op::SHL), 0u) << "the dead SHL is swept";
  const auto id = id_of("ut_sum_shl_alt");
  EXPECT_EQ(stats.rule_counts[id].applied, 1u);
  EXPECT_EQ(stats.rule_counts[id].extracted, 1u);
  EXPECT_EQ(stats.unions, 1u);
  // The Sum node was rebuilt in place as the Mult: same cell, same name.
  const auto driver = out_driver(g, "o0");
  EXPECT_EQ(gu::type_op_of(driver.get_master_node()), Ntype_op::Mult);
  EXPECT_EQ(gu::node_name_of(driver.get_master_node()), "keep_my_name");
}

TEST(AegUnion, TheSameAltTiesUnderTheSynthProfileAndTheOriginalStays) {
  hhds::GraphLibrary lib;
  auto               g     = sum_shl_graph(lib, "alt_ties");
  const auto         stats = optimize(g, {}, scoped("ut_sum_shl_alt"));
  EXPECT_EQ(count_op(g, Ntype_op::Mult), 0u);
  EXPECT_EQ(count_op(g, Ntype_op::Sum), 1u);
  EXPECT_EQ(count_op(g, Ntype_op::SHL), 1u);
  EXPECT_EQ(stats.unions, 1u);
  EXPECT_EQ(stats.rule_counts[id_of("ut_sum_shl_alt")].extracted, 0u);
  EXPECT_EQ(gu::node_name_of(out_driver(g, "o0").get_master_node()), "keep_my_name");
}

// The extraction is SHARING-AWARE: an operand another cell also reads is needed whichever member is taken, so it does
// not make a member dearer. Same alt as above under `sim`, but the SHL also feeds a second output: the original
// Sum(SHL, x) costs one add (the SHL is free here), the Mult alternative one multiply, a tie, and the original stays.
TEST(AegUnion, TheExtractionIsSharingAware) {
  hhds::GraphLibrary lib;
  auto               g   = io_graph(lib, "alt_shared", 1, 2);
  auto               x   = g->get_input_pin("i0");
  auto               shl = shift(g, Ntype_op::SHL, x, 1);
  auto               sum = binop(g, Ntype_op::Sum, shl, x);
  sum.connect_sink(g->get_output_pin("o0"));
  shl.connect_sink(g->get_output_pin("o1"));
  auto o = scoped("ut_sum_shl_alt");
  o.cost = Options::Cost::sim;
  const auto stats = optimize(g, {}, o);
  EXPECT_EQ(stats.unions, 1u);
  EXPECT_EQ(stats.rule_counts[id_of("ut_sum_shl_alt")].extracted, 0u) << "the shared SHL makes the Sum as cheap as the Mult";
  EXPECT_EQ(count_op(g, Ntype_op::Mult), 0u);
  EXPECT_EQ(count_op(g, Ntype_op::Sum), 1u);
  EXPECT_EQ(count_op(g, Ntype_op::SHL), 1u);
}

TEST(AegUnion, TheUnionLimitsAreCountedAsRejections) {
  const auto build = [](hhds::GraphLibrary& lib, const char* name) {
    auto g = io_graph(lib, name, 1, 1);
    binop(g, Ntype_op::Mult, g->get_input_pin("i0"), konst(g, 3)).connect_sink(g->get_output_pin("o0"));
    return g;
  };
  const auto a = id_of("ut_mult3_alt"), b = id_of("ut_mult3_alt2");
  {
    hhds::GraphLibrary lib;
    auto               g = build(lib, "limits_default");
    const auto         stats = optimize(g, {}, scoped("ut_mult3_alt+ut_mult3_alt2"));
    EXPECT_EQ(stats.unions, 2u);
    EXPECT_EQ(stats.rule_counts[a].applied + stats.rule_counts[b].applied, 2u);
  }
  {
    hhds::GraphLibrary lib;
    auto               g = build(lib, "limits_matches");
    auto               o = scoped("ut_mult3_alt+ut_mult3_alt2");
    o.matches_limit      = 1;
    const auto stats     = optimize(g, {}, o);
    EXPECT_EQ(stats.unions, 1u);
    EXPECT_EQ(stats.rule_counts[a].applied, 1u);  // priority 0 runs before priority -1
    EXPECT_EQ(stats.rule_counts[b].limit_rejected, 1u);
  }
  {
    hhds::GraphLibrary lib;
    auto               g = build(lib, "limits_eclass");
    auto               o = scoped("ut_mult3_alt+ut_mult3_alt2");
    o.eclass_limit       = 2;  // the class plus ONE alternative
    const auto stats     = optimize(g, {}, o);
    EXPECT_EQ(stats.unions, 1u);
    EXPECT_EQ(stats.rule_counts[b].limit_rejected, 1u);
  }
  {
    hhds::GraphLibrary lib;
    auto               g = build(lib, "limits_depth");
    auto               o = scoped("ut_mult3_alt");
    o.rewrite_depth      = 0;  // no e-node meets a rule
    const auto stats     = optimize(g, {}, o);
    EXPECT_EQ(stats.unions, 0u);
    EXPECT_EQ(stats.rule_counts[a].tried, 0u);
    EXPECT_GE(stats.depth_limited, 1u);
  }
}

// ---- simplify, priority, check ---------------------------------------------------

TEST(AegUnion, CheckUnionsNamesTheWrongRule) {
  hhds::GraphLibrary lib;
  auto               g = io_graph(lib, "wrong", 2, 1);
  posop(g, Ntype_op::Rem, g->get_input_pin("i0"), g->get_input_pin("i1")).connect_sink(g->get_output_pin("o0"));
  auto o  = scoped("ut_wrong_rem");
  o.check = Options::Check::unions;
  try {
    (void)optimize(g, {}, o);
    FAIL() << "a rule that does not preserve the value must abort under check=unions";
  } catch (const std::logic_error& e) {
    EXPECT_NE(std::string{e.what()}.find("ut_wrong_rem"), std::string::npos) << e.what();
  }
}

TEST(AegUnion, CheckOffLetsTheSameRuleThrough) {
  hhds::GraphLibrary lib;
  auto               g = io_graph(lib, "wrong_off", 2, 1);
  posop(g, Ntype_op::Rem, g->get_input_pin("i0"), g->get_input_pin("i1")).connect_sink(g->get_output_pin("o0"));
  auto o  = scoped("ut_wrong_rem");
  o.check = Options::Check::off;
  const auto stats = optimize(g, {}, o);
  EXPECT_TRUE(out_driver(g, "o0").is_const());
  EXPECT_EQ(stats.rule_counts[id_of("ut_wrong_rem")].subsumed, 1u);
}

TEST(AegUnion, SimplifyReplacesTheClassAndCountsSubsumed) {
  hhds::GraphLibrary lib;
  auto               g = io_graph(lib, "xor_self", 1, 1);
  // The two-operand Xor of one class: a hash-consed Xor(x, x) is 0.
  binop(g, Ntype_op::Xor, g->get_input_pin("i0"), g->get_input_pin("i0")).connect_sink(g->get_output_pin("o0"));
  const auto stats = optimize(g, {}, scoped("ut_xor_self"));
  ASSERT_TRUE(out_driver(g, "o0").is_const());
  EXPECT_EQ(gu::const_of(out_driver(g, "o0")).to_just_i64(), 0);
  const auto id = id_of("ut_xor_self");
  EXPECT_EQ(stats.rule_counts[id].tried, 1u);
  EXPECT_EQ(stats.rule_counts[id].subsumed, 1u);
  EXPECT_EQ(stats.rules_fired, 1u);
  EXPECT_EQ(stats.const_folded, 1u);
}

// ---- knobs -----------------------------------------------------------------------

namespace {
// o0 = And(i0, 0); o1 = Sext(Sext(i0, 6), 4)
std::shared_ptr<hhds::Graph> knob_graph(hhds::GraphLibrary& lib, const char* name) {
  auto g = io_graph(lib, name, 1, 2);
  binop(g, Ntype_op::And, g->get_input_pin("i0"), konst(g, 0)).connect_sink(g->get_output_pin("o0"));
  auto inner = gu::create_typed_node(*g, Ntype_op::Sext);
  g->get_input_pin("i0").connect_sink(gu::setup_sink_by_name(inner, "a"));
  konst(g, 6).connect_sink(gu::setup_sink_by_name(inner, "b"));
  auto outer = gu::create_typed_node(*g, Ntype_op::Sext);
  inner.create_driver_pin(0).connect_sink(gu::setup_sink_by_name(outer, "a"));
  konst(g, 4).connect_sink(gu::setup_sink_by_name(outer, "b"));
  outer.create_driver_pin(0).connect_sink(g->get_output_pin("o1"));
  return g;
}
}  // namespace

TEST(AegUnion, RulesDisableAndRulesOnlyUseNamesAndGroups) {
  {
    hhds::GraphLibrary lib;
    auto               g     = knob_graph(lib, "knobs_all");
    Options            o;
    o.check              = Options::Check::full;
    const auto stats     = optimize(g, {}, o);
    EXPECT_TRUE(out_driver(g, "o0").is_const()) << "annihilate";
    EXPECT_EQ(count_op(g, Ntype_op::Sext), 1u) << "sext_sext";
    EXPECT_GE(stats.rules_fired, 2u);
  }
  {
    hhds::GraphLibrary lib;
    auto               g = knob_graph(lib, "knobs_disable_name");
    Options            o;
    o.check          = Options::Check::full;
    o.rules_disable  = "annihilate";
    (void)optimize(g, {}, o);
    EXPECT_FALSE(out_driver(g, "o0").is_const());
    EXPECT_EQ(count_op(g, Ntype_op::Sext), 1u);
  }
  {
    hhds::GraphLibrary lib;
    auto               g = knob_graph(lib, "knobs_disable_group");
    Options            o;
    o.check          = Options::Check::full;
    o.rules_disable  = "group:shift";
    (void)optimize(g, {}, o);
    EXPECT_TRUE(out_driver(g, "o0").is_const());
    EXPECT_EQ(count_op(g, Ntype_op::Sext), 2u);
  }
  {
    hhds::GraphLibrary lib;
    auto               g = knob_graph(lib, "knobs_only");
    (void)optimize(g, {}, scoped("sext_sext"));
    EXPECT_FALSE(out_driver(g, "o0").is_const());
    EXPECT_EQ(count_op(g, Ntype_op::Sext), 1u);
  }
  {
    hhds::GraphLibrary lib;
    auto               g = knob_graph(lib, "knobs_none");
    Options            o;
    o.rules_disable = "group:arith+group:shift+group:mux+group:uniontest";
    const auto stats = optimize(g, {}, o);
    EXPECT_EQ(stats.rules_fired, 0u);
    EXPECT_EQ(count_op(g, Ntype_op::And), 1u);
    EXPECT_EQ(count_op(g, Ntype_op::Sext), 2u);
  }
}

TEST(AegUnion, MaxRewritesStopsAfterNRewritesAndLogsTheLastRule) {
  hhds::GraphLibrary lib;
  auto               g = knob_graph(lib, "bisect");
  Options            o;
  o.check         = Options::Check::full;
  o.max_rewrites  = 1;
  o.bisect_graph  = "bisect";
  testing::internal::CaptureStderr();
  const auto stats = optimize(g, {}, o);
  const auto log   = testing::internal::GetCapturedStderr();
  EXPECT_EQ(stats.rules_fired, 1u);
  EXPECT_NE(log.find("aegraph-bisect graph=bisect rewrite=1 rule="), std::string::npos) << log;
  // Rewrite-by-rewrite the graph is the same graph: N=2 applies both.
  hhds::GraphLibrary lib2;
  auto               g2 = knob_graph(lib2, "bisect");
  o.max_rewrites        = 2;
  EXPECT_EQ(optimize(g2, {}, o).rules_fired, 2u);
}

TEST(AegUnion, ExplainPrintsTheChainOfAnExtractedNode) {
  hhds::GraphLibrary lib;
  auto               g = io_graph(lib, "explain", 3, 1);
  // Mux(s, 1, t) over two 0/1 values is Or(EQ(s, 0), t): the Mux is rebuilt in place as the Or
  // (same cell), the EQ(s, 0) is the one cell the rule creates.
  auto s   = binop(g, Ntype_op::EQ, g->get_input_pin("i0"), g->get_input_pin("i1"));
  auto t   = binop(g, Ntype_op::EQ, g->get_input_pin("i1"), g->get_input_pin("i2"));
  auto mux = gu::create_typed_node(*g, Ntype_op::Mux);
  s.connect_sink(mux.create_sink_pin(0));
  konst(g, 1).connect_sink(mux.create_sink_pin(1));
  t.connect_sink(mux.create_sink_pin(2));
  mux.create_driver_pin(0).connect_sink(g->get_output_pin("o0"));
  Options o;
  o.check   = Options::Check::full;
  o.explain = "mux_const_arm_logic";
  testing::internal::CaptureStderr();
  const auto stats = optimize(g, {}, o);
  const auto log   = testing::internal::GetCapturedStderr();
  EXPECT_EQ(count_op(g, Ntype_op::Mux), 0u);
  EXPECT_EQ(count_op(g, Ntype_op::Or), 1u);
  EXPECT_EQ(count_op(g, Ntype_op::EQ), 3u);
  EXPECT_EQ(gu::type_op_of(out_driver(g, "o0").get_master_node()), Ntype_op::Or);
  EXPECT_NE(log.find("aegraph-explain graph=explain rule=mux_const_arm_logic node=c"), std::string::npos) << log;
  const auto id = id_of("mux_const_arm_logic");
  EXPECT_EQ(stats.rule_counts[id].applied, 1u);
  EXPECT_EQ(stats.rule_counts[id].extracted, 2u) << "the new EQ cell and the Or the Mux became";
}


// ---- one example per hand rule -----------------------------------------------------

namespace {

hhds::Pin_class nary(const std::shared_ptr<hhds::Graph>& g, Ntype_op op, std::initializer_list<hhds::Pin_class> ops) {
  auto n = gu::create_typed_node(*g, op);
  for (const auto& p : ops) {
    p.connect_sink(gu::setup_sink_by_name(n, "as"));
  }
  gu::set_ubits(n.create_driver_pin(0), 8);
  return n.create_driver_pin(0);
}

// The op of the cell driving `pin` (Invalid for a constant).
Ntype_op op_of(const hhds::Pin_class& pin) { return pin.is_const() ? Ntype_op::Invalid : gu::type_op_of(pin.get_master_node()); }

size_t inputs_of(const hhds::Pin_class& pin) {
  size_t n = 0;
  for ([[maybe_unused]] auto sink : pin.get_master_node().inp_sorted_pins()) {
    ++n;
  }
  return n;
}

}  // namespace

TEST(AegUnion, EveryHandRuleHasAnExampleThatFiresIt) {
  struct Example {
    const char* rule;
    std::function<hhds::Pin_class(const std::shared_ptr<hhds::Graph>&)> build;
    std::function<void(const hhds::Pin_class&, const std::shared_ptr<hhds::Graph>&)> expect;
  };
  const auto i = [](const std::shared_ptr<hhds::Graph>& g, int k) { return g->get_input_pin("i" + std::to_string(k)); };
  const auto is_input = [](const hhds::Pin_class& p, const std::shared_ptr<hhds::Graph>& g, int k) {
    return p == g->get_input_pin("i" + std::to_string(k));
  };
  const auto ne = [&](const std::shared_ptr<hhds::Graph>& g, int a, int b) {  // a 0/1 value: EQ(ia, ib)
    return binop(g, Ntype_op::EQ, i(g, a), i(g, b));
  };
  std::vector<Example> examples;
  examples.push_back({"const_fold_bank", [&](auto g) { return nary(g, Ntype_op::Xor, {i(g, 0), konst(g, 5), konst(g, 3)}); },
                      [&](auto p, auto) {
                        EXPECT_EQ(op_of(p), Ntype_op::Xor);
                        EXPECT_EQ(inputs_of(p), 2u) << "x ^ 5 ^ 3 = x ^ 6";
                      }});
  examples.push_back({"annihilate", [&](auto g) { return nary(g, Ntype_op::Mult, {i(g, 0), konst(g, 0), i(g, 1)}); },
                      [&](auto p, auto) { EXPECT_TRUE(p.is_const()); }});
  examples.push_back({"ident_drop", [&](auto g) { return nary(g, Ntype_op::Or, {i(g, 0), konst(g, 0), i(g, 1)}); },
                      [&](auto p, auto) { EXPECT_EQ(inputs_of(p), 2u); }});
  examples.push_back({"single_operand", [&](auto g) { return nary(g, Ntype_op::And, {i(g, 0)}); },
                      [&](auto p, auto g) { EXPECT_TRUE(is_input(p, g, 0)); }});
  examples.push_back({"idempotent", [&](auto g) { return nary(g, Ntype_op::And, {i(g, 0), i(g, 0), i(g, 1)}); },
                      [&](auto p, auto) { EXPECT_EQ(inputs_of(p), 2u); }});
  examples.push_back({"xor_pair_cancel", [&](auto g) { return nary(g, Ntype_op::Xor, {i(g, 0), i(g, 0), i(g, 1)}); },
                      [&](auto p, auto g) { EXPECT_TRUE(is_input(p, g, 1)); }});
  examples.push_back({"sum_cancel",
                      [&](auto g) {
                        auto n = gu::create_typed_node(*g, Ntype_op::Sum);
                        i(g, 0).connect_sink(gu::setup_sink_by_name(n, "as"));
                        i(g, 1).connect_sink(gu::setup_sink_by_name(n, "as"));
                        i(g, 0).connect_sink(gu::setup_sink_by_name(n, "bs"));
                        return n.create_driver_pin(0);
                      },
                      [&](auto p, auto g) { EXPECT_TRUE(is_input(p, g, 1)); }});
  examples.push_back({"flatten",
                      [&](auto g) { return nary(g, Ntype_op::Xor, {nary(g, Ntype_op::Xor, {i(g, 0), i(g, 1)}), i(g, 2)}); },
                      [&](auto p, auto g) {
                        EXPECT_EQ(op_of(p), Ntype_op::Xor);
                        EXPECT_EQ(inputs_of(p), 3u);
                        EXPECT_EQ(count_op(g, Ntype_op::Xor), 1u);
                      }});
  examples.push_back({"or_absorb",
                      [&](auto g) { return nary(g, Ntype_op::Or, {i(g, 0), nary(g, Ntype_op::And, {i(g, 0), i(g, 1)})}); },
                      [&](auto p, auto g) { EXPECT_TRUE(is_input(p, g, 0)); }});
  examples.push_back({"and_absorb",
                      [&](auto g) { return nary(g, Ntype_op::And, {i(g, 0), nary(g, Ntype_op::Or, {i(g, 0), i(g, 1)})}); },
                      [&](auto p, auto g) { EXPECT_TRUE(is_input(p, g, 0)); }});
  examples.push_back({"bool_complement",
                      [&](auto g) { return nary(g, Ntype_op::And, {i(g, 0), binop(g, Ntype_op::EQ, i(g, 0), konst(g, 0))}); },
                      [&](auto p, auto) {
                        ASSERT_TRUE(p.is_const());
                        EXPECT_EQ(gu::const_of(p).to_just_i64(), 0);
                      }});
  examples.push_back({"xor_bool_one", [&](auto g) { return nary(g, Ntype_op::Xor, {ne(g, 0, 1), konst(g, 1)}); },
                      [&](auto p, auto) { EXPECT_EQ(op_of(p), Ntype_op::EQ); }});
  examples.push_back({"mult_pow2_shl", [&](auto g) { return nary(g, Ntype_op::Mult, {i(g, 0), konst(g, 8)}); },
                      [&](auto p, auto) { EXPECT_EQ(op_of(p), Ntype_op::SHL); }});
  examples.push_back({"not_not",
                      [&](auto g) {
                        auto inner = gu::create_typed_node(*g, Ntype_op::Not);
                        i(g, 0).connect_sink(gu::setup_sink_by_name(inner, "a"));
                        auto outer = gu::create_typed_node(*g, Ntype_op::Not);
                        inner.create_driver_pin(0).connect_sink(gu::setup_sink_by_name(outer, "a"));
                        return outer.create_driver_pin(0);
                      },
                      [&](auto p, auto g) { EXPECT_TRUE(is_input(p, g, 0)); }});
  examples.push_back({"cmp_same", [&](auto g) { return nary(g, Ntype_op::EQ, {i(g, 0), i(g, 0)}); },
                      [&](auto p, auto) {
                        ASSERT_TRUE(p.is_const());
                        EXPECT_EQ(gu::const_of(p).to_just_i64(), 1);
                      }});
  examples.push_back({"eq_bool_chain", [&](auto g) { return binop(g, Ntype_op::EQ, ne(g, 0, 1), konst(g, 1)); },
                      [&](auto p, auto) { EXPECT_EQ(op_of(p), Ntype_op::EQ); EXPECT_EQ(inputs_of(p), 2u); }});
  examples.push_back({"and_mask_get_mask", [&](auto g) { return nary(g, Ntype_op::And, {i(g, 0), konst(g, 15)}); },
                      [&](auto p, auto) { EXPECT_EQ(op_of(p), Ntype_op::Get_mask); }});
  examples.push_back({"shift_zero", [&](auto g) { return shift(g, Ntype_op::SHL, i(g, 0), 0); },
                      [&](auto p, auto g) { EXPECT_TRUE(is_input(p, g, 0)); }});
  // The same-direction chains are pattern rules (shift.rules); the mixed ones are native.
  examples.push_back({"sra_sra", [&](auto g) { return shift(g, Ntype_op::SRA, shift(g, Ntype_op::SRA, i(g, 0), 2), 3); },
                      [&](auto p, auto g) {
                        EXPECT_EQ(op_of(p), Ntype_op::SRA);
                        EXPECT_EQ(count_op(g, Ntype_op::SRA), 1u);
                        EXPECT_EQ(gu::const_of(p.get_master_node().get_sink_pin(1).get_driver_pins().front()).to_just_i64(), 5);
                      }});
  examples.push_back({"shl_shl", [&](auto g) { return shift(g, Ntype_op::SHL, shift(g, Ntype_op::SHL, i(g, 0), 2), 3); },
                      [&](auto p, auto g) {
                        EXPECT_EQ(op_of(p), Ntype_op::SHL);
                        EXPECT_EQ(count_op(g, Ntype_op::SHL), 1u);
                        EXPECT_EQ(gu::const_of(p.get_master_node().get_sink_pin(1).get_driver_pins().front()).to_just_i64(), 5);
                      }});
  examples.push_back({"shift_compose", [&](auto g) { return shift(g, Ntype_op::SRA, shift(g, Ntype_op::SHL, i(g, 0), 4), 2); },
                      [&](auto p, auto g) {
                        EXPECT_EQ(op_of(p), Ntype_op::SHL) << "SRA(SHL(x, 4), 2) = SHL(x, 2)";
                        EXPECT_EQ(count_op(g, Ntype_op::SRA), 0u);
                        EXPECT_EQ(gu::const_of(p.get_master_node().get_sink_pin(1).get_driver_pins().front()).to_just_i64(), 2);
                      }});
  examples.push_back({"sext_sext",
                      [&](auto g) {
                        auto inner = gu::create_typed_node(*g, Ntype_op::Sext);
                        i(g, 0).connect_sink(gu::setup_sink_by_name(inner, "a"));
                        konst(g, 3).connect_sink(gu::setup_sink_by_name(inner, "b"));
                        auto outer = gu::create_typed_node(*g, Ntype_op::Sext);
                        inner.create_driver_pin(0).connect_sink(gu::setup_sink_by_name(outer, "a"));
                        konst(g, 6).connect_sink(gu::setup_sink_by_name(outer, "b"));
                        return outer.create_driver_pin(0);
                      },
                      [&](auto p, auto g) {
                        EXPECT_EQ(op_of(p), Ntype_op::Sext);
                        EXPECT_EQ(count_op(g, Ntype_op::Sext), 1u) << "Sext(Sext(x,3),6) = Sext(x,3)";
                      }});
  examples.push_back({"mux_bool_const_arms",
                      [&](auto g) {
                        auto m = gu::create_typed_node(*g, Ntype_op::Mux);
                        i(g, 0).connect_sink(m.create_sink_pin(0));
                        konst(g, 1).connect_sink(m.create_sink_pin(1));
                        konst(g, 0).connect_sink(m.create_sink_pin(2));
                        return m.create_driver_pin(0);
                      },
                      [&](auto p, auto) { EXPECT_EQ(op_of(p), Ntype_op::EQ) << "Mux(s, 1, 0) = EQ(s, 0)"; }});
  examples.push_back({"mux_sel_arm",
                      [&](auto g) {
                        auto m = gu::create_typed_node(*g, Ntype_op::Mux);
                        i(g, 0).connect_sink(m.create_sink_pin(0));
                        konst(g, 0).connect_sink(m.create_sink_pin(1));
                        i(g, 0).connect_sink(m.create_sink_pin(2));
                        return m.create_driver_pin(0);
                      },
                      [&](auto p, auto g) { EXPECT_TRUE(is_input(p, g, 0)); }});

  for (const auto& ex : examples) {
    SCOPED_TRACE(ex.rule);
    hhds::GraphLibrary lib;
    auto               g = io_graph(lib, ex.rule, 3, 1);
    ex.build(g).connect_sink(g->get_output_pin("o0"));
    Options o;
    o.check          = Options::Check::full;
    o.rules_only     = ex.rule;  // the example is for THIS rule: another rule with the same effect must not steal it
    const auto stats = optimize(g, {}, o);
    const auto id    = id_of(ex.rule);
    EXPECT_GE(stats.rule_counts[id].applied, 1u) << ex.rule << " did not fire on its example";
    ex.expect(out_driver(g, "o0"), g);
  }
}

// ---- determinism and the production rules ----------------------------------------

TEST(AegUnion, TheEngineIsDeterministicWithRulesOn) {
  const auto run = [](const char* name) {
    hhds::GraphLibrary lib;
    auto               g = knob_graph(lib, name);
    Options            o;
    o.check = Options::Check::full;
    const auto stats = optimize(g, {}, o);
    std::string sig;
    for (auto node : g->body().nodes()) {
      sig += std::to_string(static_cast<int>(gu::type_op_of(node))) + ";";
    }
    return std::make_pair(sig, stats.rules_fired);
  };
  EXPECT_EQ(run("det"), run("det"));
}

}  // namespace
