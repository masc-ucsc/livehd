//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// The tree aegraph on hand-built staging trees (top -> stmts): the rule
// allowlist, the prescan, tolg's scope model, the in-place write-back and its
// refusals, an output validity checker, idempotence and determinism, linear
// scale, and the step injected into uPass_runner ahead of its DCE.

#include "upass_aeg.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <functional>
#include <initializer_list>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "aeg_rules.hpp"
#include "fold_engine.hpp"
#include "gtest/gtest.h"
#include "lnast.hpp"
#include "lnast_manager.hpp"
#include "runtime/match.hpp"
#include "upass_runner.hpp"

namespace {

using N     = Lnast_ntype;
using Ltype = Lnast_ntype::Lnast_ntype_int;
using upass::aeg::Kind;

constexpr std::string_view kAllGroups = "group:arith+group:div+group:mask+group:mux+group:shift";

// Every rule group off for the scope (the fixtures' reference compile).
struct Rules_off {
  Rules_off() { livehd::set_fold_rules_disable(std::string(kAllGroups)); }
  ~Rules_off() { livehd::set_fold_rules_disable(""); }
};

bool is_const_text(std::string_view s) {
  return !s.empty() && (std::isdigit(static_cast<unsigned char>(s[0])) != 0 || s[0] == '-' || s == "true" || s == "false");
}

struct Tree {
  std::shared_ptr<Lnast> ln;
  Lnast_nid              body;

  explicit Tree(std::string_view name = "aeg") {
    ln        = std::make_shared<Lnast>(name);
    auto root = ln->set_root(N::create_top());
    body      = ln->add_child(root, N::create_stmts());
  }

  void in(std::string_view n, Io_kind k = Io_kind::integer, Io_sig sig = Io_sig::none, bool has_default = false) {
    Lnast_io_entry e;
    e.name        = std::string(n);
    e.bits        = 8;
    e.kind        = k;
    e.sig         = sig;
    e.has_default = has_default;
    ln->io_meta().inputs.push_back(e);
  }
  void out(std::string_view n, Io_kind k = Io_kind::integer, int32_t bits = 8, int32_t stages = 0) {
    Lnast_io_entry e;
    e.name       = std::string(n);
    e.bits       = bits;
    e.kind       = k;
    e.stages_min = stages;
    e.stages_max = stages;
    ln->io_meta().outputs.push_back(e);
  }
  // declare(name, int, mode[, stages(n, n)]): `stage[n]` when n >= 0.
  void declare(std::string_view name, std::string_view mode, int stages = -1) {
    auto d = ln->add_child(body, N::create_declare());
    ln->add_child(d, Lnast_node::create_ref(name));
    ln->add_child(d, N::create_prim_type_int());
    ln->add_child(d, Lnast_node::create_const(mode));
    if (stages >= 0) {
      auto st = ln->add_child(d, N::create_stages());
      ln->add_child(st, Lnast_node::create_const(std::to_string(stages)));
      ln->add_child(st, Lnast_node::create_const(std::to_string(stages)));
    }
  }

  Lnast_nid leaf(const Lnast_nid& parent, std::string_view s) {
    return is_const_text(s) ? ln->add_child(parent, Lnast_node::create_const(s)) : ln->add_child(parent, Lnast_node::create_ref(s));
  }
  Lnast_nid op(Ltype t, std::string_view dst, std::initializer_list<std::string_view> ops, const Lnast_nid& parent = {}) {
    auto s = ln->add_child(parent.is_invalid() ? body : parent, t);
    ln->add_child(s, Lnast_node::create_ref(dst));
    for (const auto o : ops) {
      leaf(s, o);
    }
    return s;
  }
  Lnast_nid store(std::string_view dst, std::string_view rhs, const Lnast_nid& parent = {}) {
    return op(N::Lnast_ntype_store, dst, {rhs}, parent);
  }
  // if-like(cond, then[, else]): the node and its arm bodies.
  std::tuple<Lnast_nid, Lnast_nid, Lnast_nid> branch(std::string_view cond, bool with_else, const Lnast_nid& parent = {},
                                                     Ltype t = N::Lnast_ntype_if) {
    auto node  = ln->add_child(parent.is_invalid() ? body : parent, t);
    leaf(node, cond);
    auto then_ = ln->add_child(node, N::create_stmts());
    auto else_ = with_else ? ln->add_child(node, N::create_stmts()) : Lnast_nid{};
    return {node, then_, else_};
  }

  [[nodiscard]] std::string dump() const {
    std::ostringstream o;
    ln->dump(o);
    return o.str();
  }
  upass::Staging_rewrite_stats run() { return upass::aeg::rewrite_staging({*ln, ln->io_meta(), "aeg"}); }
  // A mod (or pipe) whose cycle checks run on the step's output.
  upass::Staging_rewrite_stats run_timed(bool pipe = false) {
    return upass::aeg::rewrite_staging({*ln, ln->io_meta(), "aeg", true, pipe});
  }
};

// How often `rule` applied in a run.
uint64_t applied(const upass::Staging_rewrite_stats& st, std::string_view rule) {
  const auto id = livehd::aegraph::rt::Registry::instance().find(rule);
  return id && *id < st.rules.size() ? st.rules[*id][2] : 0;
}

// The text of every child of `nid` (refs and consts).
std::vector<std::string> kids(const Lnast& ln, const Lnast_nid& nid) {
  std::vector<std::string> v;
  for (auto c = nid.first_child(); !c.is_invalid(); c = c.next_sibling()) {
    v.emplace_back(ln.get_name(c));
  }
  return v;
}
std::string rhs(const Lnast& ln, const Lnast_nid& store) { return std::string(ln.get_name(store.first_child().next_sibling())); }

size_t count_text(const std::string& hay, std::string_view needle) {
  size_t n = 0;
  for (auto p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + 1)) {
    ++n;
  }
  return n;
}

const upass::aeg::Prescan_report::Stmt* stmt_of(const upass::aeg::Prescan_report& r, std::string_view dst, std::string_view op) {
  for (const auto& s : r.stmts) {
    if (s.dst == dst && s.op == op) {
      return &s;
    }
  }
  return nullptr;
}

// tolg's scope model over the output: every ref a statement reads names a value
// defined earlier in scope (an arm's writes end with the arm), a port, or a
// name nothing in the tree defines (external). O(N) with an undo log.
bool every_ref_bound(const Lnast& ln, std::string* bad = nullptr) {
  absl::flat_hash_set<std::string> defined_anywhere;
  std::function<void(const Lnast_nid&)> collect = [&](const Lnast_nid& n) {
    for (auto c = n.first_child(); !c.is_invalid(); c = c.next_sibling()) {
      const auto t = ln.get_type(c);
      if (N::is_ref(t) && c.is_first_child() && !N::is_if(ln.get_type(n)) && !N::is_unique_if(ln.get_type(n))) {
        defined_anywhere.insert(std::string(ln.get_name(c)));
      }
      collect(c);
    }
  };
  collect(ln.get_root());
  absl::flat_hash_set<std::string> live;
  std::vector<std::string>         undo;
  for (const auto& e : ln.io_meta().inputs) {
    live.insert(e.name);
  }
  bool ok = true;
  auto check = [&](const Lnast_nid& ref) {
    const std::string nm(ln.get_name(ref));
    if (!live.contains(nm) && defined_anywhere.contains(nm)) {
      ok = false;
      if (bad != nullptr) {
        *bad = nm;
      }
    }
  };
  std::function<void(const Lnast_nid&)> block = [&](const Lnast_nid& stmts) {
    for (auto s = stmts.first_child(); !s.is_invalid(); s = s.next_sibling()) {
      const auto t = ln.get_type(s);
      if (N::is_stmts(t)) {
        block(s);
      } else if (N::is_if(t)) {
        std::vector<std::string> wrote;
        for (auto c = s.first_child(); !c.is_invalid(); c = c.next_sibling()) {
          if (!N::is_stmts(ln.get_type(c))) {
            if (N::is_ref(ln.get_type(c))) {
              check(c);
            }
            continue;
          }
          const auto mark = undo.size();
          block(c);
          while (undo.size() > mark) {
            wrote.push_back(undo.back());
            live.erase(undo.back());
            undo.pop_back();
          }
        }
        for (auto& w : wrote) {
          if (live.insert(w).second) {
            undo.push_back(w);
          }
        }
      } else {
        auto c = s.first_child();
        for (auto k = c.is_invalid() ? c : c.next_sibling(); !k.is_invalid(); k = k.next_sibling()) {
          if (N::is_ref(ln.get_type(k))) {
            check(k);
          }
        }
        if (!c.is_invalid() && N::is_ref(ln.get_type(c))) {
          if (live.insert(std::string(ln.get_name(c))).second) {
            undo.push_back(std::string(ln.get_name(c)));
          }
        }
      }
    }
  };
  for (auto c = ln.get_root().first_child(); !c.is_invalid(); c = c.next_sibling()) {
    if (N::is_stmts(ln.get_type(c))) {
      block(c);
    }
  }
  return ok;
}

}  // namespace

// ── a. rule policy ───────────────────────────────────────────────────────────

TEST(TreeAegRules, AllowAndExcludePartitionTheRegistry) {
  namespace rt = livehd::aegraph::rt;
  auto&                 reg = rt::Registry::instance();
  std::set<std::string> listed;
  for (const auto name : upass::aeg::allowed_rules()) {
    const auto id = reg.find(name);
    ASSERT_TRUE(id.has_value()) << "allowed rule '" << name << "' is not registered";
    EXPECT_EQ(reg.rule(*id).tag, rt::Tag::simplify) << name;
    EXPECT_TRUE(listed.insert(std::string(name)).second) << "listed twice: " << name;
  }
  for (const auto& ex : upass::aeg::excluded_rules()) {
    EXPECT_TRUE(reg.find(ex.name).has_value()) << "excluded rule '" << ex.name << "' is not registered";
    EXPECT_FALSE(ex.reason.empty()) << ex.name;
    EXPECT_TRUE(listed.insert(std::string(ex.name)).second) << "listed twice: " << ex.name;
  }
  for (rt::Rule_id id = 1; id <= reg.size(); ++id) {
    EXPECT_TRUE(listed.contains(reg.rule(id).name)) << "rule '" << reg.rule(id).name << "' is neither allowed nor excluded";
  }
  EXPECT_EQ(listed.size(), reg.size());
  EXPECT_EQ(upass::aeg::allowed_rules().size(), 55U);
  EXPECT_EQ(upass::aeg::excluded_rules().size(), 72U);
}

// ── b. prescan ───────────────────────────────────────────────────────────────

TEST(TreeAegPrescan, IfExpressionTempIsDefinedInBothArms) {
  Tree t;
  t.in("x");
  t.in("y");
  t.in("c", Io_kind::boolean);
  auto [node, th, el] = t.branch("c", true);
  t.store("%r", "x", th);
  t.store("%r", "y", el);
  t.store("o", "%r");
  const auto r = upass::aeg::prescan_report(*t.ln, t.ln->io_meta());
  EXPECT_EQ(r.names.at("%r").defs, 2U);
  EXPECT_EQ(r.names.at("%r").reads, 1U);
  EXPECT_EQ(r.names.at("c").reads, 1U);
  EXPECT_EQ(r.names.at("%r").kind, Kind::integer);
}

TEST(TreeAegPrescan, StreamSsaVersionHasItsBaseKind) {
  Tree t;
  auto d = t.ln->add_child(t.body, N::create_declare());
  t.ln->add_child(d, Lnast_node::create_ref("x"));
  t.ln->add_child(d, N::create_prim_type_int());
  t.ln->add_child(d, Lnast_node::create_const("mut"));
  t.op(N::Lnast_ntype_plus, "%a", {"x___ssa_1", "1"});
  const auto r = upass::aeg::prescan_report(*t.ln, t.ln->io_meta());
  EXPECT_TRUE(r.names.at("x").state);
  EXPECT_EQ(r.names.at("x___ssa_1").kind, Kind::integer);
  EXPECT_FALSE(r.names.at("x___ssa_1").state);
}

TEST(TreeAegPrescan, RolledForCarryAndConstTempTextAreReads) {
  Tree t;
  t.in("x");
  t.op(N::Lnast_ntype_plus, "%t", {"x", "1"});
  t.op(N::Lnast_ntype_plus, "%u", {"x", "2"});
  auto loop = t.ln->add_child(t.body, N::create_rolled_for());
  t.ln->add_child(loop, Lnast_node::create_ref("i"));
  for (const auto* k : {"0", "1", "4", "", ""}) {
    t.ln->add_child(loop, Lnast_node::create_const(k));
  }
  auto carries = t.ln->add_child(loop, N::create_tuple_add());
  auto carry   = t.ln->add_child(carries, N::create_store());
  t.ln->add_child(carry, Lnast_node::create_ref("%t"));
  t.ln->add_child(carry, Lnast_node::create_const("%u"));
  t.ln->add_child(loop, N::create_stmts());
  t.ln->add_child(loop, N::create_stmts());
  const auto r = upass::aeg::prescan_report(*t.ln, t.ln->io_meta());
  EXPECT_GE(r.names.at("%t").reads, 1U);
  EXPECT_GE(r.names.at("%u").reads, 1U);
}

TEST(TreeAegPrescan, WidthSeedPinsItsWholeCone) {
  Tree t;
  t.in("x");
  t.op(N::Lnast_ntype_plus, "%t", {"x", "1"});
  t.store("y", "%t");
  t.op(N::Lnast_ntype_red_and, "%r", {"y"});
  t.op(N::Lnast_ntype_plus, "%u", {"x", "2"});
  t.store("o", "%r");
  t.store("p", "%u");
  const auto r = upass::aeg::prescan_report(*t.ln, t.ln->io_meta());
  ASSERT_NE(stmt_of(r, "%r", "red_and"), nullptr);
  EXPECT_TRUE(stmt_of(r, "%r", "red_and")->w_seed);
  EXPECT_TRUE(stmt_of(r, "%r", "red_and")->pinned);
  EXPECT_TRUE(stmt_of(r, "y", "store")->pinned);
  EXPECT_TRUE(stmt_of(r, "%t", "plus")->pinned);
  EXPECT_FALSE(stmt_of(r, "%u", "plus")->pinned);
  EXPECT_FALSE(stmt_of(r, "p", "store")->pinned);
}

// An output with no declared width (`o:Signed`, `o:Unsigned`, untyped) takes
// its driver's tolg width as its port: every write of it, SSA versions and arm
// writes included, pins its cone. A declared-width output does not.
TEST(TreeAegPrescan, UnboundedOutputPinsItsWholeCone) {
  Tree t;
  t.in("a");
  t.in("b");
  t.in("c", Io_kind::boolean);
  t.out("o", Io_kind::integer, 0);
  t.out("q", Io_kind::none, 0);
  t.out("w");
  t.op(N::Lnast_ntype_plus, "%s", {"a", "b"});
  t.op(N::Lnast_ntype_plus, "%t", {"%s", "1"});
  t.store("o", "%t");
  auto [node, th, el] = t.branch("c", false);
  t.op(N::Lnast_ntype_plus, "%u", {"a", "2"}, th);
  t.store("o___ssa_1", "%u", th);
  t.op(N::Lnast_ntype_minus, "%q", {"b", "a"});
  t.store("q", "%q");
  t.op(N::Lnast_ntype_plus, "%v", {"a", "b"});
  t.store("w", "%v");
  const auto r = upass::aeg::prescan_report(*t.ln, t.ln->io_meta());
  EXPECT_TRUE(stmt_of(r, "o", "store")->w_seed);
  EXPECT_TRUE(stmt_of(r, "%t", "plus")->pinned);
  EXPECT_TRUE(stmt_of(r, "%s", "plus")->pinned);
  EXPECT_TRUE(stmt_of(r, "o___ssa_1", "store")->w_seed);
  EXPECT_TRUE(stmt_of(r, "%u", "plus")->pinned);
  EXPECT_TRUE(stmt_of(r, "%q", "minus")->pinned);
  EXPECT_FALSE(stmt_of(r, "%v", "plus")->pinned);
  EXPECT_FALSE(stmt_of(r, "w", "store")->pinned);
}

// The runner's DCE sweeps an unread temp and, transitively, what only it read:
// the step ignores those definitions. A call keeps its definition (it writes
// more than child 0), and so does a user name.
TEST(TreeAegPrescan, UnreadTempsAreDeadTransitively) {
  Tree t;
  t.in("x");
  t.in("y");
  t.op(N::Lnast_ntype_plus, "%a", {"x", "y"});
  t.op(N::Lnast_ntype_mult, "%b", {"%a", "2"});
  t.op(N::Lnast_ntype_red_and, "%r", {"%b"});
  t.op(N::Lnast_ntype_plus, "%c", {"x", "y"});
  t.store("o", "%c");
  t.op(N::Lnast_ntype_plus, "u", {"x", "1"});
  t.op(N::Lnast_ntype_func_call, "%f", {"callee", "x"});
  const auto r = upass::aeg::prescan_report(*t.ln, t.ln->io_meta());
  EXPECT_TRUE(stmt_of(r, "%r", "red_and")->dead);
  EXPECT_FALSE(stmt_of(r, "%r", "red_and")->pinned);  // a swept W-seed pins nothing
  EXPECT_TRUE(stmt_of(r, "%b", "mult")->dead);
  EXPECT_TRUE(stmt_of(r, "%a", "plus")->dead);
  EXPECT_FALSE(stmt_of(r, "%c", "plus")->dead);
  EXPECT_FALSE(stmt_of(r, "u", "plus")->dead);
  EXPECT_FALSE(stmt_of(r, "%f", "fcall")->dead);
}

TEST(TreeAegPrescan, SpecialPropagatesFromATupleGetThroughGetMask) {
  Tree t;
  t.in("i");
  t.op(N::Lnast_ntype_tuple_get, "%m", {"mem", "i"});
  t.op(N::Lnast_ntype_get_mask, "%g", {"%m", "0", "4"});
  t.op(N::Lnast_ntype_plus, "%p", {"%g", "1"});
  const auto r = upass::aeg::prescan_report(*t.ln, t.ln->io_meta());
  EXPECT_TRUE(r.names.at("%m").special);
  EXPECT_TRUE(r.names.at("%g").special);
  EXPECT_FALSE(r.names.at("%p").special);
}

TEST(TreeAegPrescan, ChainInnerNeedsOneReaderOfTheSameOpAndRegion) {
  Tree t;
  t.in("x");
  t.in("y");
  t.in("z");
  t.op(N::Lnast_ntype_bit_and, "%a", {"x", "y"});
  t.op(N::Lnast_ntype_bit_and, "%b", {"%a", "z"});
  t.op(N::Lnast_ntype_plus, "%c", {"%b", "1"});
  t.op(N::Lnast_ntype_bit_xor, "%e", {"x", "y"});
  auto marker = t.ln->add_child(t.body, N::create_attr_set());
  t.ln->add_child(marker, Lnast_node::create_ref("%__region_1"));
  t.ln->add_child(marker, Lnast_node::create_const("__region"));
  t.ln->add_child(marker, Lnast_node::create_const("true"));
  t.op(N::Lnast_ntype_bit_xor, "%f", {"%e", "z"});
  t.op(N::Lnast_ntype_bit_or, "%g", {"x", "z"});
  t.op(N::Lnast_ntype_bit_or, "%h", {"%g", "%g"});
  t.store("o1", "%c");
  t.store("o2", "%f");
  t.store("o3", "%h");
  const auto r = upass::aeg::prescan_report(*t.ln, t.ln->io_meta());
  EXPECT_TRUE(stmt_of(r, "%a", "bit_and")->chain_inner);
  EXPECT_FALSE(stmt_of(r, "%b", "bit_and")->chain_inner);  // its reader is a plus
  EXPECT_FALSE(stmt_of(r, "%e", "bit_xor")->chain_inner);  // its reader is in another region
  EXPECT_FALSE(stmt_of(r, "%g", "bit_or")->chain_inner);   // read twice
}

// A pinned chain (it feeds a width reader) still leaves the absorbing to its
// top link: each link flattening on its own rebuilt the chain so far.
TEST(TreeAegPrescan, ChainInnerHoldsUnderAPinnedReader) {
  Tree t;
  t.in("x");
  t.in("y");
  t.in("z");
  t.op(N::Lnast_ntype_bit_or, "%a", {"x", "y"});
  t.op(N::Lnast_ntype_bit_or, "%b", {"%a", "z"});
  t.op(N::Lnast_ntype_red_and, "%r", {"%b"});
  t.store("o", "%r");
  const auto r = upass::aeg::prescan_report(*t.ln, t.ln->io_meta());
  EXPECT_TRUE(stmt_of(r, "%b", "bit_or")->pinned);
  EXPECT_TRUE(stmt_of(r, "%a", "bit_or")->chain_inner);
}

// ── c. scoping ───────────────────────────────────────────────────────────────

TEST(TreeAegScope, ArmLocalTempIsNotLookedThroughAfterTheIf) {
  Rules_off off;
  Tree      t;
  t.in("x");
  t.in("y");
  t.in("c", Io_kind::boolean);
  auto [node, th, el] = t.branch("c", false);
  t.op(N::Lnast_ntype_plus, "%a", {"x", "y"}, th);
  t.op(N::Lnast_ntype_plus, "%b", {"x", "y"});
  auto o = t.store("o", "%b");
  t.run();
  EXPECT_EQ(rhs(*t.ln, o), "%b");
}

TEST(TreeAegScope, UserNameWrittenInOneArmGetsAFreshValueAfterTheIf) {
  Rules_off off;
  Tree      t;
  t.in("x");
  t.in("c", Io_kind::boolean);
  t.op(N::Lnast_ntype_plus, "%a", {"u", "1"});
  auto [node, th, el] = t.branch("c", false);
  t.store("u", "x", th);
  t.op(N::Lnast_ntype_plus, "%b", {"u", "1"});
  auto o1 = t.store("o1", "%a");
  auto o2 = t.store("o2", "%b");
  t.run();
  EXPECT_EQ(rhs(*t.ln, o1), "%a");
  EXPECT_EQ(rhs(*t.ln, o2), "%b");
}

// %t is written in an arm and never mentioned past the if, so the walk retires
// it instead of killing it. The arm rollback restores %t's binding from before
// the if, but past the if the LNAST reads %t as the merged value: a later
// `x + y` must not start reading %t.
TEST(TreeAegScope, RetiredArmNameIsNeverReadAgain) {
  Rules_off off;
  Tree      t;
  t.in("x");
  t.in("y");
  t.in("z");
  t.in("c", Io_kind::boolean);
  t.op(N::Lnast_ntype_plus, "%t", {"x", "y"});
  auto p              = t.store("p", "%t");
  auto [node, th, el] = t.branch("c", false);
  t.store("%t", "z", th);
  t.op(N::Lnast_ntype_plus, "%u", {"x", "y"});
  auto o = t.store("o", "%u");
  t.run();
  EXPECT_EQ(rhs(*t.ln, p), "%t");
  EXPECT_EQ(rhs(*t.ln, o), "%u");
}

TEST(TreeAegScope, OuterExpressionIsSharedInsideAnArm) {
  Rules_off off;
  Tree      t;
  t.in("x");
  t.in("y");
  t.in("c", Io_kind::boolean);
  t.op(N::Lnast_ntype_plus, "%a", {"x", "y"});
  auto [node, th, el] = t.branch("c", false);
  t.op(N::Lnast_ntype_plus, "%b", {"x", "y"}, th);
  auto o = t.store("o", "%b", th);
  auto p = t.store("p", "%a");
  const auto st = t.run();
  EXPECT_EQ(rhs(*t.ln, o), "%a");
  EXPECT_EQ(rhs(*t.ln, p), "%a");
  EXPECT_EQ(st.forwarded, 1U);
}

TEST(TreeAegScope, NoHolderSurvivesABarrier) {
  for (const auto kind : {N::Lnast_ntype_unique_if, N::Lnast_ntype_rolled_for, N::Lnast_ntype_tick}) {
    Rules_off off;
    Tree      t;
    t.in("x");
    t.in("y");
    t.in("c", Io_kind::boolean);
    t.op(N::Lnast_ntype_plus, "%a", {"x", "y"});
    if (kind == N::Lnast_ntype_unique_if) {
      auto [node, th, el] = t.branch("c", false, {}, kind);
      t.store("q", "x", th);
    } else {
      auto b = t.ln->add_child(t.body, kind);
      t.ln->add_child(b, Lnast_node::create_const("4"));
      auto inner = t.ln->add_child(b, N::create_stmts());
      t.store("q", "x", inner);
    }
    t.op(N::Lnast_ntype_plus, "%b", {"x", "y"});
    auto o = t.store("o", "%b");
    t.run();
    EXPECT_EQ(rhs(*t.ln, o), "%b") << N::to_sv(kind);
  }
}

TEST(TreeAegScope, BacktickedSpellingIsTheSameName) {
  Rules_off off;
  Tree      t;
  t.in("x");
  t.op(N::Lnast_ntype_plus, "%a", {"`u`", "1"});
  t.store("u", "x");  // tolg's canon_io_name: one binding under both spellings
  t.op(N::Lnast_ntype_plus, "%b", {"`u`", "1"});
  auto o = t.store("o", "%b");
  t.run();
  EXPECT_EQ(rhs(*t.ln, o), "%b");
}

TEST(TreeAegScope, RenameKeepsTheHoldersSpelling) {
  // The runner's DCE keys reads by spelling: a reader renamed to `` `x` `` must
  // spell it as the read that bound it, or the definitions of that spelling die.
  Rules_off off;
  Tree      t;
  t.in("x");
  t.store("%c", "`x`");
  auto o = t.store("o", "%c");
  t.run();
  EXPECT_EQ(rhs(*t.ln, o), "`x`");
}

TEST(TreeAegScope, RegionMarkerStopsSharing) {
  Rules_off off;
  Tree      t;
  t.in("x");
  t.in("y");
  t.op(N::Lnast_ntype_plus, "%a", {"x", "y"});
  auto marker = t.ln->add_child(t.body, N::create_attr_set());
  t.ln->add_child(marker, Lnast_node::create_ref("%__region_1"));
  t.ln->add_child(marker, Lnast_node::create_const("__region"));
  t.ln->add_child(marker, Lnast_node::create_const("true"));
  t.op(N::Lnast_ntype_plus, "%b", {"x", "y"});
  auto o = t.store("o", "%b");
  t.run();
  EXPECT_EQ(rhs(*t.ln, o), "%b");
}

TEST(TreeAegScope, SynthAttributeKeepsItsDriverApart) {
  // `const n1::[synth.adder="prefix"] = x + y` after an unannotated `x + y`:
  // the attr_set follows the value statement, and tolg anchors it on n1's
  // driver. Sharing the earlier cell would put the prefix adder on n0's logic.
  Rules_off off;
  Tree      t;
  t.in("x");
  t.in("y");
  t.op(N::Lnast_ntype_plus, "%a", {"x", "y"});
  t.store("n0", "%a");
  t.op(N::Lnast_ntype_plus, "%b", {"x", "y"});
  auto attr = t.ln->add_child(t.body, N::create_attr_set());
  t.ln->add_child(attr, Lnast_node::create_ref("n1"));
  t.ln->add_child(attr, Lnast_node::create_const("synth.adder"));
  t.ln->add_child(attr, Lnast_node::create_const("'prefix'"));
  auto s1 = t.store("n1", "%b");
  t.store("o0", "n0");
  t.store("o1", "n1");
  t.run();
  EXPECT_EQ(rhs(*t.ln, s1), "%b");
}

TEST(TreeAegScope, SynthAttributeColorsItsWholeCone) {
  // tolg spreads a declaration attribute over n's whole fan-in cone: the
  // inner `x + y` of `n = (x + y) + z` must not be shared with m's `x + y`.
  Rules_off off;
  Tree      t;
  t.in("x");
  t.in("y");
  t.in("z");
  t.op(N::Lnast_ntype_plus, "%a", {"x", "y"});
  t.store("m", "%a");
  t.op(N::Lnast_ntype_plus, "%b", {"x", "y"});
  auto c    = t.op(N::Lnast_ntype_plus, "%c", {"%b", "z"});
  auto attr = t.ln->add_child(t.body, N::create_attr_set());
  t.ln->add_child(attr, Lnast_node::create_ref("n"));
  t.ln->add_child(attr, Lnast_node::create_const("synth.adder"));
  t.ln->add_child(attr, Lnast_node::create_const("'prefix'"));
  t.store("n", "%c");
  t.store("o0", "m");
  t.store("o1", "n");
  t.run();
  EXPECT_EQ(kids(*t.ln, c)[1], "%b");
}

TEST(TreeAegScope, SynthAttributeCoversStreamSsaVersions) {
  // A later write of an attributed name is the SSA version n___ssa_1, an
  // anchor root to tolg too: an unannotated twin after it must not read it.
  Rules_off off;
  Tree      t;
  t.in("x");
  t.in("y");
  auto attr = t.ln->add_child(t.body, N::create_attr_set());
  t.ln->add_child(attr, Lnast_node::create_ref("n"));
  t.ln->add_child(attr, Lnast_node::create_const("synth.adder"));
  t.ln->add_child(attr, Lnast_node::create_const("'prefix'"));
  t.store("n", "0");
  t.op(N::Lnast_ntype_plus, "%a", {"x", "y"});
  t.store("n___ssa_1", "%a");
  t.op(N::Lnast_ntype_plus, "%b", {"x", "y"});
  auto sm = t.store("m", "%b");
  t.store("o0", "m");
  t.store("o1", "n___ssa_1");
  t.run();
  EXPECT_EQ(rhs(*t.ln, sm), "%b");
}

TEST(TreeAegScope, LaterTwinBlocksAnEarlierFlatten) {
  // t = a | b read by x = t | c, and a later twin y = a | b: t has two
  // readers in total, so x keeps or(t, c) and the shared a | b stays one cell
  // (picorv32 mem_done: flattening first left two ORs of rinst, rdata).
  Tree t;
  t.in("a");
  t.in("b");
  t.in("c");
  t.op(N::Lnast_ntype_bit_or, "%t", {"a", "b"});
  auto x = t.op(N::Lnast_ntype_bit_or, "%x", {"%t", "c"});
  t.store("o1", "%x");
  t.op(N::Lnast_ntype_bit_or, "%y", {"a", "b"});
  t.store("o2", "%y");
  const auto st = t.run();
  EXPECT_EQ(applied(st, "flatten"), 0u);
  EXPECT_EQ(kids(*t.ln, x).size(), 3u);
  EXPECT_EQ(kids(*t.ln, x)[1], "%t");
}

TEST(TreeAegScope, RespellOrdersAFlattenLikeTheLGraphEngine) {
  // `c | (a | b)` flattens to Or(a, b, c). The tree writes the operands in the
  // order the LGraph engine's rewire() gives the same flatten (drivers newest
  // first for And/Or; inputs by port), so the pin order -- which tolg, the
  // simulator kernel order and the ABC AIG walk follow -- is what the LGraph
  // flatten would have built, never e-graph class order.
  Tree t;
  t.in("a");
  t.in("b");
  t.in("c");
  t.op(N::Lnast_ntype_bit_and, "%p", {"a", "b"});
  t.op(N::Lnast_ntype_bit_and, "%q", {"%p", "c"});
  t.store("o0", "%q");
  t.op(N::Lnast_ntype_bit_or, "%t", {"a", "b"});
  auto x = t.op(N::Lnast_ntype_bit_or, "%x", {"c", "%t"});
  t.store("o1", "%x");
  const auto st = t.run();
  ASSERT_GT(applied(st, "flatten"), 0u);
  const auto k = kids(*t.ln, x);
  ASSERT_EQ(k.size(), 4u);
  EXPECT_EQ(k[1], "c");
  EXPECT_EQ(k[2], "b");
  EXPECT_EQ(k[3], "a");
}

TEST(TreeAegScope, RefusedRespellNeverHoldsTheValue) {
  // `w + x + y + z - w` is Sum(x,y,z), three operands: the respell is refused
  // and `%h4` still reads `w`. Were it the holder of x+y+z, `w = x + y + z`
  // would become `w = %h4`, a structural read of w by its own driver.
  Tree t;
  t.in("x");
  t.in("y");
  t.in("z");
  t.declare("w", "wire");
  t.op(N::Lnast_ntype_plus, "%h1", {"w", "x"});
  t.op(N::Lnast_ntype_plus, "%h2", {"%h1", "y"});
  t.op(N::Lnast_ntype_plus, "%h3", {"%h2", "z"});
  t.op(N::Lnast_ntype_minus, "%h4", {"%h3", "w"});
  t.store("h", "%h4");
  t.op(N::Lnast_ntype_plus, "%s1", {"x", "y"});
  t.op(N::Lnast_ntype_plus, "%s2", {"%s1", "z"});
  auto sw = t.store("w", "%s2");
  t.store("o1", "w");
  t.store("o2", "h");
  const auto st = t.run();
  EXPECT_GT(st.refused_sum, 0u) << "the fixture must exercise the refused respell";
  EXPECT_NE(rhs(*t.ln, sw), "%h4");
}

TEST(TreeAegScope, NoReaderAfterTheDefaultPrologueReadsAPrologueTemp) {
  Rules_off off;
  Tree      t;
  t.in("x");
  t.in("b", Io_kind::integer, Io_sig::none, /*has_default=*/true);
  t.op(N::Lnast_ntype_plus, "%p", {"x", "1"});
  t.store("__default_b", "%p");
  t.op(N::Lnast_ntype_plus, "%q", {"x", "1"});
  auto o = t.store("o", "%q");
  const auto r = upass::aeg::prescan_report(*t.ln, t.ln->io_meta());
  EXPECT_EQ(r.prologue_last, 1);
  t.run();
  EXPECT_EQ(rhs(*t.ln, o), "%q");
}

// ── d. rules off: CSE, copies, constants ────────────────────────────────────

TEST(TreeAegRulesOff, SharedExpressionReadersReadTheFirstName) {
  Rules_off off;
  Tree      t;
  t.in("x");
  t.in("y");
  t.op(N::Lnast_ntype_plus, "%a", {"x", "y"});
  t.op(N::Lnast_ntype_plus, "%b", {"y", "x"});
  auto o1 = t.store("o1", "%a");
  auto o2 = t.store("o2", "%b");
  auto u  = t.op(N::Lnast_ntype_mult, "%m", {"%b", "3"});
  t.store("o3", "%m");
  const auto st = t.run();
  EXPECT_EQ(rhs(*t.ln, o1), "%a");
  EXPECT_EQ(rhs(*t.ln, o2), "%a");
  EXPECT_EQ(kids(*t.ln, u)[1], "%a");
  EXPECT_EQ(st.forwarded, 1U);
  EXPECT_EQ(st.respelled, 0U);
}

// An unread definition is swept by the DCE right after the step, so a later
// copy of its value keeps its own name: the body a warm compile re-walks (a
// restored comb with the leftovers its DCE only marked) and the one a cold
// compile walks must simplify alike.
TEST(TreeAegRulesOff, DeadDefinitionNeverHoldsAValue) {
  Rules_off off;
  Tree      t;
  t.in("x");
  t.in("y");
  t.op(N::Lnast_ntype_plus, "%d", {"x", "y"});
  t.op(N::Lnast_ntype_plus, "%l", {"x", "y"});
  auto       o  = t.store("o", "%l");
  const auto st = t.run();
  EXPECT_EQ(rhs(*t.ln, o), "%l");
  EXPECT_EQ(st.forwarded, 0U);
  EXPECT_EQ(st.imported, 1U);
}

TEST(TreeAegRulesOff, StoreCopyOfAnInputIsLookedThrough) {
  Rules_off off;
  Tree      t;
  t.in("x");
  t.store("%c", "x");
  auto o = t.store("o", "%c");
  t.run();
  EXPECT_EQ(rhs(*t.ln, o), "x");
}

TEST(TreeAegRulesOff, StoreNeverReadsAnAggregateOrOpaqueName) {
  Rules_off off;
  Tree      t;
  t.in("x");
  t.op(N::Lnast_ntype_func_call, "%f", {"callee", "x"});
  t.store("%c", "%f");
  auto o = t.store("o", "%c");
  t.store("%d", "tup");
  auto p = t.store("p", "%d");
  t.run();
  EXPECT_EQ(rhs(*t.ln, o), "%c");
  EXPECT_EQ(rhs(*t.ln, p), "%d");
}

TEST(TreeAegRulesOff, UnknownLiteralIsNeverCopied) {
  Rules_off off;
  Tree      t;
  t.in("x");
  t.op(N::Lnast_ntype_plus, "%l", {"x", "0ub1?"});
  t.op(N::Lnast_ntype_plus, "%k", {"x", "0ub1?"});
  t.store("%c", "0ub1?");
  auto o = t.store("o", "%k");
  auto p = t.store("p", "%c");
  const auto before = count_text(t.dump(), "0ub1?");
  t.run();
  EXPECT_EQ(count_text(t.dump(), "0ub1?"), before);
  EXPECT_EQ(rhs(*t.ln, o), "%k");
  EXPECT_EQ(rhs(*t.ln, p), "%c");
}

TEST(TreeAegRulesOff, ConstantOfUnknownKindIsNotSubstituted) {
  Rules_off off;
  Tree      t;
  t.op(N::Lnast_ntype_bit_and, "%t", {"u", "0"});
  auto o = t.store("o", "%t");
  t.run();
  EXPECT_EQ(rhs(*t.ln, o), "%t");
}

TEST(TreeAegRulesOff, BoolConstantIsSpelledAsBool) {
  Rules_off off;
  Tree      t;
  t.op(N::Lnast_ntype_log_not, "%e", {"true"});
  auto o = t.store("o", "%e");
  const auto st = t.run();
  EXPECT_EQ(rhs(*t.ln, o), "false");
  EXPECT_EQ(st.consts, 1U);
}

// ── e. rules on: rewrites and refusals ──────────────────────────────────────

TEST(TreeAegRulesOn, ComplementaryBooleanTermsForwardTheInput) {
  Tree t;
  t.in("x", Io_kind::boolean);
  t.in("s", Io_kind::boolean);
  t.op(N::Lnast_ntype_log_not, "%neg", {"s"});
  t.op(N::Lnast_ntype_log_and, "%on", {"x", "s"});
  t.op(N::Lnast_ntype_log_and, "%off", {"%neg", "x"});
  t.op(N::Lnast_ntype_log_or, "%sum", {"%on", "%off"});
  auto o = t.store("o", "%sum");
  EXPECT_GT(applied(t.run(), "or_combine_complements"), 0U);
  EXPECT_EQ(rhs(*t.ln, o), "x");
}

TEST(TreeAegRulesOn, ProductCancellationRequiresANonzeroDivisor) {
  Tree t;
  t.in("x");
  t.in("d");
  t.op(N::Lnast_ntype_mult, "%p", {"x", "-3"});
  t.op(N::Lnast_ntype_div, "%q", {"%p", "-3"});
  t.op(N::Lnast_ntype_mult, "%u", {"x", "d"});
  t.op(N::Lnast_ntype_div, "%v", {"%u", "d"});
  auto known   = t.store("known", "%q");
  auto unknown = t.store("unknown", "%v");
  EXPECT_GT(applied(t.run(), "div_cancel_product"), 0U);
  EXPECT_EQ(rhs(*t.ln, known), "x");
  EXPECT_EQ(rhs(*t.ln, unknown), "%v");
}

TEST(TreeAegRulesOn, SignedDoubleNotIsTheOperand) {
  Tree t;
  t.in("x");
  t.op(N::Lnast_ntype_bit_not, "%a", {"x"});
  auto b = t.op(N::Lnast_ntype_bit_not, "%b", {"%a"});
  auto o = t.store("o", "%b");
  t.run();
  EXPECT_EQ(rhs(*t.ln, o), "x");
  EXPECT_EQ(t.ln->get_type(b), N::Lnast_ntype_bit_not);  // left in place for the DCE
}

TEST(TreeAegRulesOn, TypedDoubleNotIsAMaskAppendedInPlace) {
  Tree t;
  t.in("x");
  t.op(N::Lnast_ntype_bit_not, "%a", {"x", "8"});
  auto b = t.op(N::Lnast_ntype_bit_not, "%b", {"%a", "8"});
  t.store("o", "%b");
  const auto st = t.run();
  EXPECT_EQ(t.ln->get_type(b), N::Lnast_ntype_get_mask);
  EXPECT_EQ(kids(*t.ln, b), (std::vector<std::string>{"%b", "x", "0", "8"}));
  EXPECT_EQ(st.respelled, 1U);
}

TEST(TreeAegRulesOn, MultByPowerOfTwoIsAShiftInPlace) {
  Tree t;
  t.in("x");
  auto m = t.op(N::Lnast_ntype_mult, "%m", {"x", "8"});
  t.ln->set_srcid(m, 4242);
  const auto cls = m.get_class_index().value;
  t.store("o", "%m");
  t.run();
  EXPECT_EQ(t.ln->get_type(m), N::Lnast_ntype_shl);
  EXPECT_EQ(kids(*t.ln, m), (std::vector<std::string>{"%m", "x", "3"}));
  EXPECT_EQ(t.ln->get_srcid(m), 4242U);
  EXPECT_EQ(m.get_class_index().value, cls);
}

TEST(TreeAegRulesOn, LowMaskAndIsAGetMask) {
  Tree t;
  t.in("x");
  auto a = t.op(N::Lnast_ntype_bit_and, "%a", {"x", "255"});
  t.store("o", "%a");
  t.run();
  EXPECT_EQ(t.ln->get_type(a), N::Lnast_ntype_get_mask);
  EXPECT_EQ(kids(*t.ln, a), (std::vector<std::string>{"%a", "x", "0", "8"}));
}

TEST(TreeAegRulesOn, DifferenceOfItselfIsZero) {
  Tree t;
  t.in("x");
  t.op(N::Lnast_ntype_minus, "%d", {"x", "x"});
  auto o = t.store("o", "%d");
  t.run();
  EXPECT_EQ(rhs(*t.ln, o), "0");
}

TEST(TreeAegRulesOn, AndWithItsComplementIsZero) {
  Tree t;
  t.in("x");
  t.op(N::Lnast_ntype_bit_not, "%n", {"x"});
  t.op(N::Lnast_ntype_bit_and, "%z", {"x", "%n"});
  auto o = t.store("o", "%z");
  t.run();
  EXPECT_EQ(rhs(*t.ln, o), "0");
}

TEST(TreeAegRulesOn, NegatedComparesRoundTrip) {
  Tree t;
  t.in("x");
  t.in("y");
  auto ne = t.op(N::Lnast_ntype_ne, "%ne", {"x", "y"});
  auto le = t.op(N::Lnast_ntype_le, "%le", {"x", "y"});
  auto ge = t.op(N::Lnast_ntype_ge, "%ge", {"x", "y"});
  t.store("o1", "%ne");
  t.store("o2", "%le");
  t.store("o3", "%ge");
  const auto before = t.dump();
  const auto st     = t.run();
  EXPECT_EQ(t.dump(), before);
  EXPECT_EQ(st.respelled, 0U);
  EXPECT_EQ(t.ln->get_type(ne), N::Lnast_ntype_ne);
  EXPECT_EQ(t.ln->get_type(le), N::Lnast_ntype_le);
  EXPECT_EQ(t.ln->get_type(ge), N::Lnast_ntype_ge);
}

TEST(TreeAegRulesOn, AndChainBecomesOneStatement) {
  Tree t;
  t.in("x");
  t.in("y");
  t.in("z");
  t.in("w");
  t.op(N::Lnast_ntype_bit_and, "%a", {"x", "y"});
  t.op(N::Lnast_ntype_bit_and, "%b", {"%a", "z"});
  auto c = t.op(N::Lnast_ntype_bit_and, "%c", {"%b", "w"});
  t.store("o", "%c");
  t.run();
  auto k = kids(*t.ln, c);
  std::sort(k.begin() + 1, k.end());
  EXPECT_EQ(k, (std::vector<std::string>{"%c", "w", "x", "y", "z"}));
  EXPECT_FALSE(t.dump().empty());
}

// (x + x + y) - x - z + z = x + y: the flattened chain cancels as a multiset,
// one added copy per subtracted operand.
TEST(TreeAegRulesOn, PlusMinusChainCancelsAsAMultiset) {
  Tree t;
  t.in("x");
  t.in("y");
  t.in("z");
  t.op(N::Lnast_ntype_plus, "%a", {"x", "x"});
  t.op(N::Lnast_ntype_plus, "%b", {"%a", "y"});
  t.op(N::Lnast_ntype_minus, "%c", {"%b", "x"});
  t.op(N::Lnast_ntype_minus, "%d", {"%c", "z"});
  auto e = t.op(N::Lnast_ntype_plus, "%e", {"%d", "z"});
  t.store("o", "%e");
  const auto st = t.run();
  EXPECT_EQ(applied(st, "sum_cancel"), 1U);
  EXPECT_EQ(t.ln->get_type(e), N::Lnast_ntype_plus);
  auto k = kids(*t.ln, e);
  std::sort(k.begin() + 1, k.end());
  EXPECT_EQ(k, (std::vector<std::string>{"%e", "x", "y"}));
}

TEST(TreeAegRulesOn, WideSumIsRefused) {
  Tree t;
  t.in("x");
  t.in("y");
  t.in("z");
  t.op(N::Lnast_ntype_plus, "%a", {"x", "y"});
  auto b = t.op(N::Lnast_ntype_plus, "%b", {"%a", "z"});
  t.store("o", "%b");
  const auto st = t.run();
  EXPECT_EQ(st.refused_sum, 1U);
  EXPECT_EQ(kids(*t.ln, b), (std::vector<std::string>{"%b", "%a", "z"}));
}

TEST(TreeAegRulesOn, ShrinkingRewriteIsRefused) {
  Tree t;
  t.in("x");
  t.in("y");
  auto a = t.op(N::Lnast_ntype_bit_and, "%a", {"x", "y", "x"});
  t.store("o", "%a");
  const auto st = t.run();
  EXPECT_EQ(st.refused_arity, 1U);
  EXPECT_EQ(kids(*t.ln, a), (std::vector<std::string>{"%a", "x", "y", "x"}));
}

TEST(TreeAegRulesOn, UntypedNotKeepsItsSpellingAndOperand) {
  Tree t;
  t.in("x");
  t.in("y");
  t.op(N::Lnast_ntype_plus, "%a", {"x", "y"});
  t.op(N::Lnast_ntype_plus, "%b", {"x", "y"});
  auto n = t.op(N::Lnast_ntype_bit_not, "%n", {"%b"});
  t.store("o", "%n");
  t.run();
  EXPECT_EQ(t.ln->get_type(n), N::Lnast_ntype_bit_not);
  EXPECT_EQ(kids(*t.ln, n), (std::vector<std::string>{"%n", "%b"}));
}

TEST(TreeAegRulesOn, PartialWriteWindowOpStaysInItsClass) {
  Tree t;
  t.in("x");
  t.in("y");
  t.op(N::Lnast_ntype_eq, "%e", {"x", "y"});
  auto v = t.op(N::Lnast_ntype_bit_xor, "%v", {"%e", "true"});
  t.store("o", "%v", {});
  const auto st = t.run();
  EXPECT_EQ(t.ln->get_type(v), N::Lnast_ntype_bit_xor);
  EXPECT_EQ(st.refused_class, 1U);
}

TEST(TreeAegRulesOn, LogNotKeepsABooleanOperand) {
  // !(x != 0) is EQ(x, 0): over an integer x it is spelled `x == 0`, never `not x`.
  Tree t;
  t.in("x");
  t.op(N::Lnast_ntype_ne, "%n", {"x", "0"});
  auto l = t.op(N::Lnast_ntype_log_not, "%l", {"%n"});
  t.store("o", "%l");
  t.run();
  EXPECT_EQ(t.ln->get_type(l), N::Lnast_ntype_eq);
  EXPECT_EQ(kids(*t.ln, l), (std::vector<std::string>{"%l", "x", "0"}));
}

TEST(TreeAegRulesOn, NoRenameCrossesKinds) {
  Tree t;
  t.in("x");
  t.op(N::Lnast_ntype_eq, "%e", {"x", "1"});
  t.op(N::Lnast_ntype_minus, "%g", {"%e", "0"});  // ident_drop: the class of %e, but an integer
  auto k = t.op(N::Lnast_ntype_plus, "%k", {"%g", "1"});
  t.run();
  EXPECT_EQ(kids(*t.ln, k)[1], "%g");
}

TEST(TreeAegRulesOn, ClockOperandStaysOpaque) {
  Tree t;
  t.in("clk", Io_kind::boolean, Io_sig::clock);
  t.in("en", Io_kind::boolean);
  auto g1 = t.op(N::Lnast_ntype_bit_and, "%g1", {"clk", "en"});
  t.op(N::Lnast_ntype_bit_and, "%g2", {"clk", "en"});
  auto o = t.store("o", "%g2");
  const auto before = kids(*t.ln, g1);
  const auto st     = t.run();
  EXPECT_EQ(kids(*t.ln, g1), before);
  EXPECT_EQ(rhs(*t.ln, o), "%g2");
  EXPECT_EQ(st.imported, 0U);
}

TEST(TreeAegRulesOn, VerilogClockGateKeepsItsShape) {
  // Verilog has no Clock type: a latch enable or a clock_pin names the clock,
  // and logic derived from it (an active-low ICG) stays as written.
  Tree t;
  t.in("clk");
  t.in("en");
  t.in("d");
  auto d = t.ln->add_child(t.body, N::create_declare());
  t.ln->add_child(d, Lnast_node::create_ref("held_en"));
  t.ln->add_child(d, N::create_prim_type_int());
  t.ln->add_child(d, Lnast_node::create_const("latch"));
  auto a = t.ln->add_child(t.body, N::create_attr_set());
  t.ln->add_child(a, Lnast_node::create_ref("held_en"));
  t.ln->add_child(a, Lnast_node::create_const("enable"));
  t.ln->add_child(a, Lnast_node::create_ref("clk"));
  t.store("held_en", "en");
  t.op(N::Lnast_ntype_bit_not, "%1", {"held_en", "1"});
  t.op(N::Lnast_ntype_bit_or, "%2", {"clk", "%1"});
  auto gate = t.op(N::Lnast_ntype_bit_and, "%3", {"%2", "1"});
  t.store("gclk_n", "%3");
  // A flop clocked by a computed net: the cone feeding clock_pin is pinned.
  auto g = t.op(N::Lnast_ntype_bit_and, "%g", {"d", "255"});
  auto r = t.ln->add_child(t.body, N::create_attr_set());
  t.ln->add_child(r, Lnast_node::create_ref("q"));
  t.ln->add_child(r, Lnast_node::create_const("clock_pin"));
  t.ln->add_child(r, Lnast_node::create_ref("%g"));
  // The flop clocked by clk is data everywhere else: its clock attribute does not taint it.
  auto sd = t.ln->add_child(t.body, N::create_declare());
  t.ln->add_child(sd, Lnast_node::create_ref("s"));
  t.ln->add_child(sd, N::create_prim_type_int());
  t.ln->add_child(sd, Lnast_node::create_const("reg"));
  auto rr = t.ln->add_child(t.body, N::create_attr_set());
  t.ln->add_child(rr, Lnast_node::create_ref("s"));
  t.ln->add_child(rr, Lnast_node::create_const("clock_pin"));
  t.ln->add_child(rr, Lnast_node::create_ref("clk"));
  auto data = t.op(N::Lnast_ntype_bit_and, "%s", {"s", "255"});
  t.store("sd", "%s");
  t.run();
  EXPECT_EQ(t.ln->get_type(gate), N::Lnast_ntype_bit_and);
  EXPECT_EQ(t.ln->get_type(g), N::Lnast_ntype_bit_and);
  EXPECT_EQ(t.ln->get_type(data), N::Lnast_ntype_get_mask);
}

TEST(TreeAegRulesOn, WidthReadingOpsStayOpaque) {
  Tree t;
  t.in("x");
  t.op(N::Lnast_ntype_mod, "%m1", {"x", "4"});
  t.op(N::Lnast_ntype_mod, "%m2", {"x", "4"});
  t.op(N::Lnast_ntype_red_and, "%r1", {"x"});
  t.op(N::Lnast_ntype_red_and, "%r2", {"x"});
  t.op(N::Lnast_ntype_concat, "%c1", {"x", "8"});
  t.op(N::Lnast_ntype_concat, "%c2", {"x", "8"});
  auto o1 = t.store("o1", "%m2");
  auto o2 = t.store("o2", "%r2");
  auto o3 = t.store("o3", "%c2");
  auto mm = t.op(N::Lnast_ntype_mult, "%q", {"%m1", "8"});  // reads a width-dependent value: still fine to rewrite
  t.store("o4", "%q");
  t.store("o5", "%r1");
  t.store("o6", "%c1");
  t.run();
  EXPECT_EQ(rhs(*t.ln, o1), "%m2");
  EXPECT_EQ(rhs(*t.ln, o2), "%r2");
  EXPECT_EQ(rhs(*t.ln, o3), "%c2");
  EXPECT_EQ(t.ln->get_type(mm), N::Lnast_ntype_shl);
}

TEST(TreeAegRulesOn, PinnedConeKeepsItsSpelling) {
  Tree t;
  t.in("x");
  auto m = t.op(N::Lnast_ntype_mult, "%m", {"x", "8"});
  t.op(N::Lnast_ntype_red_and, "%r", {"%m"});
  t.store("o", "%r");
  t.run();
  EXPECT_EQ(t.ln->get_type(m), N::Lnast_ntype_mult);
}

// `o = (a + b + 1) - 1` with o of no declared width: folding the cone to the
// `a + b` already computed for w would give o's port that sum's tolg width.
// The cone stays as written; a declared-width reader may still share the pinned
// statements' values.
TEST(TreeAegRulesOn, UnboundedOutputConeKeepsItsSpelling) {
  Tree t;
  t.in("a");
  t.in("b");
  t.out("o", Io_kind::integer, 0);
  t.out("w");
  t.op(N::Lnast_ntype_plus, "%s", {"a", "b"});
  t.op(N::Lnast_ntype_plus, "%t", {"%s", "1"});
  auto m = t.op(N::Lnast_ntype_minus, "%u", {"%t", "1"});
  auto o = t.store("o", "%u");
  t.op(N::Lnast_ntype_plus, "%v", {"a", "b"});
  auto w        = t.store("w", "%v");
  const auto st = t.run();
  EXPECT_EQ(rhs(*t.ln, o), "%u");
  EXPECT_EQ(t.ln->get_type(m), N::Lnast_ntype_minus);
  EXPECT_EQ(kids(*t.ln, m), (std::vector<std::string>{"%u", "%t", "1"}));
  EXPECT_EQ(rhs(*t.ln, w), "%s");
  EXPECT_EQ(st.forwarded, 1U);
}

TEST(TreeAegRulesOn, SweptWidthReaderPinsNothing) {
  Tree t;
  t.in("x");
  auto m = t.op(N::Lnast_ntype_mult, "%m", {"x", "8"});
  t.op(N::Lnast_ntype_red_and, "%r", {"%m"});  // unread: the DCE sweeps it
  t.store("o", "%m");
  t.run();
  EXPECT_EQ(t.ln->get_type(m), N::Lnast_ntype_shl);
}

TEST(TreeAegRulesOn, MemoryReadChainIsFrozen) {
  Tree t;
  t.in("i");
  t.op(N::Lnast_ntype_tuple_get, "%m", {"mem", "i"});
  t.op(N::Lnast_ntype_plus, "%p", {"%m", "1"});
  t.op(N::Lnast_ntype_plus, "%q", {"%m", "1"});
  auto o = t.store("o", "%q");
  t.run();
  EXPECT_EQ(rhs(*t.ln, o), "%q");
}

// ── cycle checks: upass/timecheck and tolg judge the step's output ──────────

// `stage[3] r = a; o = (r - r) + b`: r - r is 0, but the check reads it as r's
// value at cycle 3 mixed with b at cycle 0, a compile error. Folded, the check
// would see b alone and accept the program.
TEST(TreeAegTiming, StageValueIsNeverFoldedAway) {
  for (const bool timed : {false, true}) {
    Tree t;
    t.in("a");
    t.in("b");
    t.out("o");
    t.declare("r", "reg", 3);
    t.store("r", "a");
    auto       d  = t.op(N::Lnast_ntype_minus, "%d", {"r", "r"});
    auto       s  = t.op(N::Lnast_ntype_plus, "%s", {"%d", "b"});
    auto       o  = t.store("o", "%s");
    const auto st = timed ? t.run_timed() : t.run();
    if (!timed) {
      EXPECT_EQ(rhs(*t.ln, o), "b");  // the fold a timed unit must not make
      continue;
    }
    EXPECT_EQ(kids(*t.ln, d), (std::vector<std::string>{"%d", "r", "r"}));
    EXPECT_EQ(kids(*t.ln, s), (std::vector<std::string>{"%s", "%d", "b"}));
    EXPECT_EQ(rhs(*t.ln, o), "%s");
    EXPECT_EQ(st.imported, 0U);
  }
}

// An `@[N]` check compares the cycle its name's cone lands at: `z = a - a` is
// 0, yet it lands at a's cycle.
TEST(TreeAegTiming, CycleCheckConeKeepsItsSpelling) {
  Tree t;
  t.in("a");
  t.out("o");
  t.op(N::Lnast_ntype_minus, "%d", {"a", "a"});
  auto z  = t.store("z", "%d");
  auto tc = t.ln->add_child(t.body, N::create_timecheck());
  t.ln->add_child(tc, Lnast_node::create_ref("z"));
  t.ln->add_child(tc, Lnast_node::create_const("2"));
  t.ln->add_child(tc, Lnast_node::create_const("2"));
  t.store("o", "z");
  t.run_timed();
  EXPECT_EQ(rhs(*t.ln, z), "%d");
}

// `o:U8@[3] = a - a` lands at cycle 0, an interface error; a cone folded to a
// constant would have no cycle to check.
TEST(TreeAegTiming, OutputDeclaredAtACycleKeepsItsCone) {
  Tree t;
  t.in("a");
  t.out("o", Io_kind::integer, 8, 3);
  t.op(N::Lnast_ntype_minus, "%d", {"a", "a"});
  auto o = t.store("o", "%d");
  t.run_timed();
  EXPECT_EQ(rhs(*t.ln, o), "%d");
}

// tolg reads a plain reg as a feedforward stage when an output reaches it
// through copies and slices only. `o = r & 15` reaches r through an And: it
// stays an And (as a slice it would make r a stage and move o a cycle), while
// the same mask of an input is still respelled to a slice.
TEST(TreeAegTiming, RegMaskOnAnOutputChainIsNotRespelledToASlice) {
  Tree t;
  t.in("x");
  t.out("o");
  t.out("p");
  t.declare("r", "reg");
  t.store("r", "x");
  auto m = t.op(N::Lnast_ntype_bit_and, "%m", {"r", "15"});
  t.store("o", "%m");
  auto q = t.op(N::Lnast_ntype_bit_and, "%q", {"x", "15"});
  t.store("p", "%q");
  const auto st = t.run_timed();
  EXPECT_EQ(t.ln->get_type(m), N::Lnast_ntype_bit_and);
  EXPECT_EQ(t.ln->get_type(q), N::Lnast_ntype_get_mask);
  EXPECT_EQ(st.refused_class, 1U);
}

// An output that slices a plain reg makes it a stage: what reads the reg is a
// cycle-checked value and stays as written. In a pipe every plain reg is one.
TEST(TreeAegTiming, RegThatIsAStageKeepsItsReaders) {
  for (const int shape : {0, 1, 2}) {  // 0: mod, 1: mod with a sliced output, 2: pipe
    Tree t;
    t.in("x");
    t.in("y");
    t.out("o");
    t.out("p");
    t.declare("r", "reg");
    t.store("r", "x");
    if (shape == 1) {
      t.op(N::Lnast_ntype_get_mask, "%g", {"r", "0", "4"});
      t.store("o", "%g");
    }
    t.op(N::Lnast_ntype_minus, "%d", {"r", "r"});
    auto s = t.op(N::Lnast_ntype_plus, "%s", {"%d", "y"});
    t.store("p", "%s");
    t.run_timed(shape == 2);
    // A mod's plain reg is cycle-0 state: the fold changes no cycle. (p's own
    // store keeps reading %s: p's value reads a reg, so its chain is kept.)
    EXPECT_EQ(kids(*t.ln, s), (std::vector<std::string>{"%s", shape == 0 ? "0" : "%d", "y"})) << shape;
  }
}

// A call result may land at the callee's declared latency: every value
// computed from it is checked as written, past the readers frozen on its name.
TEST(TreeAegTiming, CallResultConeIsTimed) {
  for (const bool timed : {false, true}) {
    Tree t;
    t.in("x");
    t.in("y");
    t.out("p");
    t.op(N::Lnast_ntype_func_call, "%f", {"callee", "x"});
    t.op(N::Lnast_ntype_tuple_get, "%v", {"%f", "0"});
    t.op(N::Lnast_ntype_plus, "%e", {"%v", "0"});
    t.op(N::Lnast_ntype_minus, "%d", {"%e", "%e"});
    t.op(N::Lnast_ntype_plus, "%s", {"%d", "y"});
    auto p = t.store("p", "%s");
    timed ? t.run_timed() : t.run();
    EXPECT_EQ(rhs(*t.ln, p), timed ? "%s" : "y") << timed;
  }
}

// A timed condition times the merge of every name its arms write.
TEST(TreeAegTiming, TimedConditionTimesTheArmWrites) {
  Tree t;
  t.in("a");
  t.in("x");
  t.in("y");
  t.out("p");
  t.declare("r", "reg", 2);
  t.store("r", "a");
  t.op(N::Lnast_ntype_eq, "%c", {"r", "0"});
  auto [node, th, el] = t.branch("%c", false);
  t.op(N::Lnast_ntype_plus, "%w", {"x", "1"}, th);
  t.store("w", "%w", th);
  t.op(N::Lnast_ntype_minus, "%d", {"w", "w"});
  t.op(N::Lnast_ntype_plus, "%s", {"%d", "y"});
  auto p = t.store("p", "%s");
  t.run_timed();
  EXPECT_EQ(rhs(*t.ln, p), "%s");
}

// ── f. validity, idempotence, determinism ───────────────────────────────────

namespace {
std::shared_ptr<Tree> mixed_tree() {
  auto t = std::make_shared<Tree>("mixed");
  t->in("x");
  t->in("y");
  t->in("c", Io_kind::boolean);
  t->op(N::Lnast_ntype_plus, "%a", {"x", "y"});
  t->op(N::Lnast_ntype_mult, "%m", {"%a", "4"});
  auto [node, th, el] = t->branch("c", true);
  t->op(N::Lnast_ntype_plus, "%b", {"x", "y"}, th);
  t->op(N::Lnast_ntype_bit_and, "%k", {"%b", "255"}, th);
  t->store("%r", "%k", th);
  t->op(N::Lnast_ntype_bit_not, "%n", {"x", "8"}, el);
  t->op(N::Lnast_ntype_bit_not, "%nn", {"%n", "8"}, el);
  t->store("%r", "%nn", el);
  t->op(N::Lnast_ntype_plus, "%s", {"%r", "%m"});
  t->op(N::Lnast_ntype_minus, "%z", {"%s", "%s"});
  t->store("o", "%s");
  t->store("p", "%z");
  return t;
}
}  // namespace

TEST(TreeAegValidity, EveryRefStaysBoundIdempotentAndDeterministic) {
  auto       a = mixed_tree();
  auto       b = mixed_tree();
  const auto s = a->run();
  b->run();
  EXPECT_GT(s.forwarded + s.respelled + s.consts, 0U);
  std::string bad;
  EXPECT_TRUE(every_ref_bound(*a->ln, &bad)) << bad << "\n" << a->dump();
  EXPECT_EQ(a->dump(), b->dump());  // byte-identical across runs
  const auto once = a->dump();
  a->run();
  EXPECT_EQ(a->dump(), once);  // a second run changes nothing
}

// ── g. scale ────────────────────────────────────────────────────────────────

namespace {
double time_chain(int n, upass::Staging_rewrite_stats& st) {
  Tree t("chain");
  t.op(N::Lnast_ntype_bit_and, "%c0", {"v0", "v1"});
  for (int i = 1; i < n; ++i) {
    t.op(N::Lnast_ntype_bit_and, std::format("%c{}", i), {std::format("%c{}", i - 1), std::format("v{}", i + 1)});
  }
  t.store("o", std::format("%c{}", n - 1));
  for (int i = 0; i < n; ++i) {
    t.op(N::Lnast_ntype_plus, std::format("%p{}", i), {std::format("a{}", i), std::format("b{}", i)});
    t.store(std::format("q{}", i), std::format("%p{}", i));
  }
  const auto t0 = std::chrono::steady_clock::now();
  st            = t.run();
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}
}  // namespace

// Every link of a pinned chain (it feeds an output of no declared width) used
// to flatten on its own: O(k^2) operands that drained the unit's rule budget
// and starved the statements after it. The top absorbs the chain once.
TEST(TreeAegScale, PinnedChainFlattensOnceWithinTheBudget) {
  Tree t("pinned");
  t.in("x");
  t.out("o", Io_kind::integer, 0);
  t.out("q");
  t.op(N::Lnast_ntype_bit_or, "%c0", {"v0", "v1"});
  for (int i = 1; i < 10000; ++i) {
    t.op(N::Lnast_ntype_bit_or, std::format("%c{}", i), {std::format("%c{}", i - 1), std::format("v{}", i + 1)});
  }
  t.store("o", "%c9999");
  auto m = t.op(N::Lnast_ntype_mult, "%m", {"x", "8"});
  t.store("q", "%m");
  const auto st = t.run();
  EXPECT_EQ(st.budget_hit, 0U);
  EXPECT_EQ(applied(st, "flatten"), 1U);
  EXPECT_EQ(t.ln->get_type(m), N::Lnast_ntype_shl);  // a later rewrite still has budget
}

namespace {
// `a0 + a1 - a2 + a3 - ...` into a declared-width output: one flattened Sum of
// n operands, half of them subtracted.
double time_plus_minus(int n, upass::Staging_rewrite_stats& st) {
  Tree t("pm");
  t.out("o");
  t.op(N::Lnast_ntype_plus, "%s1", {"a0", "a1"});
  for (int i = 2; i < n; ++i) {
    t.op(i % 2 == 0 ? N::Lnast_ntype_minus : N::Lnast_ntype_plus,
         std::format("%s{}", i),
         {std::format("%s{}", i - 1), std::format("a{}", i)});
  }
  t.store("o", std::format("%s{}", n - 1));
  const auto t0 = std::chrono::steady_clock::now();
  st            = t.run();
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}
}  // namespace

// sum_cancel on the flattened Sum pairs the two banks in one merge (it scanned
// the subtracted bank once per added operand).
TEST(TreeAegScale, WidePlusMinusChainIsLinear) {
  upass::Staging_rewrite_stats s1;
  upass::Staging_rewrite_stats s4;
  time_plus_minus(2000, s1);  // warm the pools
  // Match LinearInTheTree's sampling: preemption during a full regression
  // must not turn a single wall-clock sample into an apparent growth penalty.
  double t1 = time_plus_minus(10000, s1), t4 = time_plus_minus(40000, s4);
  for (int rep = 0; rep < 2; ++rep) {
    t1 = std::min(t1, time_plus_minus(10000, s1));
    t4 = std::min(t4, time_plus_minus(40000, s4));
  }
  EXPECT_EQ(applied(s4, "flatten"), 1U);
  EXPECT_EQ(s4.budget_hit, 0U);
  EXPECT_LT(t4, 8.0 * std::max(t1, 1e-3)) << "10k " << t1 << "s, 40k " << t4 << "s";
}

TEST(TreeAegScale, LinearInTheTree) {
  upass::Staging_rewrite_stats s10;
  upass::Staging_rewrite_stats s100;
  time_chain(2000, s10);  // warm the pools
  // Best of a few runs: a test sharing the machine with a full regression is
  // preempted at random, and the ratio of two single samples then measures the
  // load, not the growth.
  double t10 = time_chain(10000, s10), t100 = time_chain(100000, s100);
  for (int rep = 0; rep < 2; ++rep) {
    t10  = std::min(t10, time_chain(10000, s10));
    t100 = std::min(t100, time_chain(100000, s100));
  }
  EXPECT_EQ(s10.budget_hit, 0U);
  EXPECT_EQ(s100.budget_hit, 0U);
  EXPECT_EQ(s100.respelled, 1U);  // the whole chain absorbed by its top link
  EXPECT_LT(t100, 25.0 * std::max(t10, 1e-3)) << "10k " << t10 << "s, 100k " << t100 << "s";
}

namespace {
// An `if s == i { ... } else { if ... }` chain `depth` deep (the shape slang
// gives a Verilog else-if chain): each arm computes arm-local temps, the
// output is written at every level.
double time_nested_ifs(int depth) {
  Tree t("nest");
  t.in("s");
  t.in("x");
  t.in("y");
  t.out("o");
  Lnast_nid at = t.body;
  for (int i = 0; i < depth; ++i) {
    const auto c = std::format("%c{}", i);
    t.op(N::Lnast_ntype_eq, c, {"s", std::to_string(i)}, at);
    auto [node, th, el] = t.branch(c, true, at);
    std::string prev    = "x";
    for (int k = 0; k < 8; ++k) {
      const auto v = std::format("%v{}_{}", i, k);
      t.op(N::Lnast_ntype_plus, v, {prev, k % 2 == 0 ? std::string_view{"y"} : std::string_view{"3"}}, th);
      prev = v;
    }
    t.store("o", prev, th);
    at = el;
  }
  t.store("o", "0", at);
  const auto t0 = std::chrono::steady_clock::now();
  t.run();
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}
}  // namespace

// A name written at depth d used to be killed again at each of the d enclosing
// ifs (O(depth^2) over a chain); now an arm-local is retired once and only the
// output, live past every if, is killed per level.
TEST(TreeAegScale, NestedIfsAreLinearInDepth) {
  time_nested_ifs(500);  // warm the pools
  // Best of three: a loaded machine (a whole-repo `bazel test`) only ever makes a sample slower, and a 5 ms
  // baseline is easily doubled by one scheduling hiccup. Linear is 4x, quadratic 16x.
  double t1 = 1e9, t4 = 1e9;
  for (int i = 0; i < 3; ++i) {
    t1 = std::min(t1, time_nested_ifs(1000));
    t4 = std::min(t4, time_nested_ifs(4000));
  }
  EXPECT_LT(t4, 8.0 * std::max(t1, 1e-3)) << "depth 1000 " << t1 << "s, depth 4000 " << t4 << "s";
}

// ── runner integration: the step ahead of the DCE ───────────────────────────

namespace {
std::shared_ptr<Lnast> cse_unit() {
  auto t = Tree("cse_unit");
  t.in("x");
  t.in("y");
  t.out("z1");
  t.out("z2");
  t.out("z3");
  t.op(N::Lnast_ntype_plus, "%a", {"x", "y"});
  t.op(N::Lnast_ntype_plus, "%b", {"x", "y"});
  t.op(N::Lnast_ntype_mult, "%m", {"x", "8"});
  t.store("z1", "%a");
  t.store("z2", "%b");
  t.store("z3", "%m");
  return t.ln;
}

bool defines(const Lnast& ln, std::string_view name, bool skip_dead) {
  for (const auto& nid : ln.depth_preorder(ln.get_root())) {
    const auto p = nid.parent();
    if (N::is_ref(ln.get_type(nid)) && nid.is_first_child() && ln.get_name(nid) == name && !p.is_invalid()
        && !N::is_store(ln.get_type(p)) && !(skip_dead && ln.is_dce_dead(p))) {
      return true;
    }
  }
  return false;
}
bool has_type(const Lnast& ln, Ltype t) {
  for (const auto& nid : ln.depth_preorder(ln.get_root())) {
    if (ln.get_type(nid) == t) {
      return true;
    }
  }
  return false;
}
}  // namespace

TEST(TreeAegRunner, OrphansAreSweptInRebuildAndMarkMode) {
  for (const bool mark : {false, true}) {
    auto ln = cse_unit();
    ln->set_lambda_kind("mod");  // a concrete mod: tolg is the only reader of its body, so dce:mark only marks
    auto lm = std::make_shared<upass::Lnast_manager>(ln);
    upass::Options_map opts;
    if (mark) {
      opts["dce"] = "mark";
    }
    uPass_runner runner(lm, {"constprop"}, opts);
    runner.set_staging_rewrite(&upass::aeg::rewrite_staging);
    runner.run();
    EXPECT_EQ(runner.staging_rewrite_stats().forwarded, 1U);
    EXPECT_EQ(runner.staging_rewrite_stats().respelled, 1U);
    auto staging = runner.take_staging();
    ASSERT_NE(staging, nullptr);
    EXPECT_TRUE(defines(*staging, "%a", true)) << mark;
    EXPECT_FALSE(defines(*staging, "%b", true)) << "mark=" << mark;
    EXPECT_TRUE(has_type(*staging, N::Lnast_ntype_shl));
    std::string bad;
    EXPECT_TRUE(every_ref_bound(*staging, &bad)) << bad;
  }
}

// The step rewrites in place only a body tolg alone reads (a concrete mod or
// pipe). A comb's post-walk body is spliced by the inliner and restored by the
// compile cache for a warm caller, so it keeps the walk's spelling (simplified
// with the comb's own port facts, a warm caller's re-walk differed from a cold
// one); the step runs on a private lowering copy that only the comb's own tolg
// build reads, so a value-false loop lowers in a comb as in a mod. Other kinds
// are not rewritten at all.
TEST(TreeAegRunner, OnlyABodyTolgAloneReadsIsRewritten) {
  for (const std::string_view kind : {"mod", "pipe", "comb", ""}) {
    auto ln = cse_unit();
    ln->set_lambda_kind(kind);
    auto         lm = std::make_shared<upass::Lnast_manager>(ln);
    uPass_runner runner(lm, {"constprop"});
    runner.set_staging_rewrite(&upass::aeg::rewrite_staging);
    runner.run();
    const bool tolg_only = kind == "mod" || kind == "pipe";
    const bool comb      = kind == "comb";
    EXPECT_EQ(runner.staging_rewrite_stats().units, tolg_only || comb ? 1U : 0U) << kind;
    auto lowering = runner.take_lowering_staging();
    EXPECT_EQ(lowering != nullptr, comb) << kind;
    if (lowering) {
      EXPECT_FALSE(defines(*lowering, "%b", true)) << kind;
      EXPECT_TRUE(has_type(*lowering, N::Lnast_ntype_shl)) << kind;
    }
    auto staging = runner.take_staging();
    ASSERT_NE(staging, nullptr);
    EXPECT_EQ(defines(*staging, "%b", true), !tolg_only) << kind;
    EXPECT_EQ(has_type(*staging, N::Lnast_ntype_shl), tolg_only) << kind;
  }
}

TEST(TreeAegRunner, WithoutTheHookStagingIsUnchanged) {
  auto         ln = cse_unit();
  auto         lm = std::make_shared<upass::Lnast_manager>(ln);
  uPass_runner runner(lm, {"constprop"});
  runner.run();
  auto staging = runner.take_staging();
  ASSERT_NE(staging, nullptr);
  EXPECT_TRUE(defines(*staging, "%b", true));
  EXPECT_TRUE(has_type(*staging, N::Lnast_ntype_mult));
  EXPECT_FALSE(has_type(*staging, N::Lnast_ntype_shl));
}
