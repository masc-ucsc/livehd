//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_runner.hpp"

#include <algorithm>
#include <format>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/container/flat_hash_set.h"

#include "upass_runner_common.hpp"
#include "upass_shared.hpp"

namespace {

struct Lnast_pass_noop_shared final : public upass::uPass {
  using upass::uPass::uPass;

  void process_top() override { upass::run_noop_shared(*lm, "uPass"); }
};

struct Lnast_pass_scan_shared final : public upass::uPass {
  using upass::uPass::uPass;

  void process_assign() override {
    if (emitted) {
      return;
    }
    const auto rep = upass::run_scan_shared(*lm, "uPass");
    std::print("uPass - shared scan summary nodes:{} const:{} arith:{}\n", rep.node_count, rep.const_count, rep.arithmetic_count);
    emitted = true;
  }

private:
  bool emitted{false};
};

struct Lnast_pass_decide_shared final : public upass::uPass {
  using upass::uPass::uPass;

  void process_assign() override {
    if (emitted) {
      return;
    }
    const auto rep = upass::run_decide_shared(*lm, "uPass");
    std::print("uPass - shared decide summary fold_candidates:{} has_fold_candidates:{}\n",
               rep.fold_candidate_count,
               rep.has_fold_candidates ? "true" : "false");
    emitted = true;
  }

private:
  bool emitted{false};
};

static upass::uPass_plugin plugin_noop_shared("noop_shared", upass::uPass_wrapper<Lnast_pass_noop_shared>::get_upass);
static upass::uPass_plugin plugin_scan_shared("scan_shared", upass::uPass_wrapper<Lnast_pass_scan_shared>::get_upass);
static upass::uPass_plugin plugin_decide_shared("decide_shared", upass::uPass_wrapper<Lnast_pass_decide_shared>::get_upass);

}  // namespace

uPass_runner::uPass_runner(std::shared_ptr<upass::Lnast_manager>& _lm, const std::vector<std::string>& upass_names,
                           upass::Options_map options)
    : uPass_struct(_lm) {
  auto        upass_registry = upass::uPass_plugin::get_registry();
  std::string order_error;
  auto        resolved = resolve_order(upass_names, &order_error);
  if (!order_error.empty()) {
    configuration_error     = true;
    configuration_error_msg = order_error;
  }
  if (!resolved.empty()) {
    std::print("uPass - resolved order:");
    for (const auto& name : resolved) {
      std::print(" {}", name);
    }
    std::print("\n");
  }

  for (const auto& name : resolved) {
    const auto it = upass_registry.find(name);
    if (it == upass_registry.end()) {
      std::print("{} is not defined.\n", name);
      continue;
    }

    std::print("uPass - add {}\n", name);
    upasses.emplace_back(Pass_entry{.name = name, .pass = it->second.setup_fn(_lm)});
  }

  // Wire runner-backed fold callback + options into every pass. Done once,
  // after all passes are constructed, so the callback sees the full pass
  // list and each pass can pick the options it recognizes.
  auto fold_fn    = [this](std::string_view name) { return try_fold_ref(name); };
  auto emit_at_fn = [this](const Lnast_nid& src) { emit_op_with_fold_at(src); };
  for (auto& entry : upasses) {
    entry.pass->set_runner_fold_fn(fold_fn);
    entry.pass->set_runner_emit_at_fn(emit_at_fn);
    entry.pass->set_runner_symbol_table(&runner_symbol_table);
    entry.pass->set_options(options);
  }

  // Pre-cache pass subsets so the hot try_fold_ref / any_pass_drops loops
  // only visit passes that actually override the relevant virtual. Order is
  // preserved (try_fold_ref takes the first non-nullopt; attributes' wrap/sat
  // narrowing must beat constprop's raw value, etc.).
  fold_capable_passes.reserve(upasses.size());
  classify_capable_passes.reserve(upasses.size());
  for (auto& entry : upasses) {
    if (entry.pass->overrides_fold_ref()) {
      fold_capable_passes.push_back(entry.pass.get());
    }
    if (entry.pass->overrides_classify_statement()) {
      classify_capable_passes.push_back(entry.pass.get());
    }
  }
}

std::vector<std::string> uPass_runner::resolve_order(const std::vector<std::string>& requested_names,
                                                     std::string*                    error_msg) const {
  const auto& upass_registry = upass::uPass_plugin::get_registry();

  enum class Mark { kUnseen, kVisiting, kDone };
  std::unordered_map<std::string, Mark> marks;
  std::vector<std::string>              ordered;

  std::function<bool(const std::string&)> dfs = [&](const std::string& name) {
    const auto it = upass_registry.find(name);
    if (it == upass_registry.end()) {
      std::print("{} is not defined.\n", name);
      if (error_msg && error_msg->empty()) {
        *error_msg = std::format("unknown pass '{}'", name);
      }
      return false;
    }

    const auto mit = marks.find(name);
    if (mit != marks.end()) {
      if (mit->second == Mark::kVisiting) {
        std::print(stderr, "uPass dependency cycle detected at {}\n", name);
        if (error_msg && error_msg->empty()) {
          *error_msg = std::format("dependency cycle detected at '{}'", name);
        }
        return false;
      }
      return mit->second == Mark::kDone;
    }

    marks.emplace(name, Mark::kVisiting);
    for (const auto& dep : it->second.depends_on) {
      if (!dfs(dep)) {
        std::print(stderr, "uPass dependency chain for {} is invalid\n", name);
        if (error_msg && error_msg->empty()) {
          *error_msg = std::format("dependency chain for '{}' is invalid", name);
        }
        marks[name] = Mark::kDone;
        return false;
      }
    }
    marks[name] = Mark::kDone;
    ordered.emplace_back(name);
    return true;
  };

  for (const auto& name : requested_names) {
    dfs(name);
  }

  return ordered;
}

// ── Staging emit helpers ──────────────────────────────────────────────────────

void uPass_runner::emit_push(Lnast_ntype::Lnast_ntype_int type) {
  auto nid = staging->add_child(staging_parent, type);
  staging_parent_stack.push(staging_parent);
  staging_parent = nid;
}

void uPass_runner::emit_pop() {
  staging_parent = staging_parent_stack.top();
  staging_parent_stack.pop();
}

void uPass_runner::emit_leaf(Lnast_ntype::Lnast_ntype_int type) { staging->add_child(staging_parent, type); }

void uPass_runner::emit_leaf(const Lnast_node& node) { staging->add_child(staging_parent, node); }

void uPass_runner::emit_subtree_verbatim() {
  auto type = lm->current_type();
  if (lm->has_child()) {
    emit_push(type);
    lm->move_to_child();
    do {
      emit_subtree_verbatim();
    } while (lm->move_to_sibling());
    lm->move_to_parent();
    emit_pop();
  } else {
    if (Lnast_ntype::is_ref(type) || Lnast_ntype::is_const(type)) {
      emit_leaf(lm->current_node());
    } else {
      emit_leaf(type);
    }
  }
}

std::optional<Const> uPass_runner::try_fold_ref(std::string_view name) {
  for (auto* p : fold_capable_passes) {
    auto folded = p->fold_ref(name);
    if (folded) {
      return folded;
    }
  }
  return std::nullopt;
}

void uPass_runner::emit_op_with_fold_at(const Lnast_nid& src) {
  // Save and re-position the read cursor so the in-progress traversal isn't
  // disturbed. emit_op_with_fold balances its own move_to_child / sibling /
  // parent operations, so the nid_stack returns to baseline; restoring here
  // just covers move_to_nid (which doesn't push onto the stack).
  auto saved = lm->save_cursor();
  lm->move_to_nid(src);
  emit_op_with_fold(/*fold_all=*/false);
  lm->restore_cursor(saved);
}

void uPass_runner::emit_ref_or_folded(std::string_view name) {
  auto folded = try_fold_ref(name);
  if (folded && !folded->is_invalid()) {
    emit_leaf(Lnast_node::create_const(folded->to_pyrope()));
  } else {
    emit_leaf(lm->current_node());
  }
}

void uPass_runner::emit_op_with_fold(bool fold_all) {
  emit_push(lm->current_type());

  if (lm->has_child()) {
    lm->move_to_child();
    int idx = 0;
    do {
      const bool is_lhs = (idx == 0) && !fold_all;
      if (!is_lhs && lm->get_raw_ntype() == Lnast_ntype::Lnast_ntype_ref) {
        emit_ref_or_folded(lm->current_text());
      } else {
        // Either the LHS (don't fold — it's a dst) or a non-ref child
        // (const, or a nested subtree like tuple_add's assign(key, val)).
        emit_subtree_verbatim();
      }
      ++idx;
    } while (lm->move_to_sibling());
    lm->move_to_parent();
  }

  emit_pop();
}

// ── Pass dispatch ─────────────────────────────────────────────────────────────

void uPass_runner::dispatch_to_passes(Pass_method fn) {
  for (auto& entry : upasses) {
    // Snapshot the read cursor before dispatch. If the pass throws (e.g.
    // constprop's sub_op refuses a string-typed invalid Const) or is
    // otherwise unbalanced, restoring here keeps the runner-level emit
    // logic operating on the op-node the switch case selected.
    const auto saved = lm->save_cursor();
    try {
      (entry.pass.get()->*fn)();
    } catch (const std::runtime_error& ex) {
      std::print(stderr, "uPass [{}] error: {}\n", entry.name, ex.what());
    }
    lm->restore_cursor(saved);
  }
}

upass::Vote uPass_runner::reduce_votes(const std::vector<upass::Vote>& votes) {
  // Priority per docs/upass_redesign.md §G:
  //   drop  > toconst > update > keep
  // toconst values must agree across voters (sanity-checked).
  upass::Vote out;
  const upass::Vote* toconst_seen = nullptr;
  const upass::Vote* update_seen  = nullptr;
  for (const auto& v : votes) {
    if (v.kind == upass::Vote_kind::drop) {
      return upass::Vote::drop();
    }
    if (v.kind == upass::Vote_kind::toconst) {
      if (toconst_seen && !toconst_seen->toconst_value.same_repr(v.toconst_value)) {
        upass::error("uPass_runner: conflicting toconst votes for the same node\n");
      }
      toconst_seen = &v;
    } else if (v.kind == upass::Vote_kind::update) {
      update_seen = &v;
    }
  }
  if (toconst_seen) {
    return *toconst_seen;
  }
  if (update_seen) {
    return *update_seen;
  }
  return upass::Vote::keep();
}

bool uPass_runner::any_pass_drops() const {
  for (auto* p : classify_capable_passes) {
    if (p->classify_statement().kind == upass::Emit_kind::drop_subtree) {
      return true;
    }
  }
  return false;
}

void uPass_runner::process_drop_candidate(Pass_method fn, bool fold_all) {
  // 1. Run per-node process_* so symbol tables see the current statement.
  dispatch_to_passes(fn);
  // 2. Ask every pass whether to keep this statement; first drop wins.
  // 3. Emit (with operand folding) unless dropped.
  if (!any_pass_drops()) {
    emit_op_with_fold(fold_all);
  }
}

void uPass_runner::process_drop_candidate_verbatim(Pass_method fn) {
  dispatch_to_passes(fn);
  if (!any_pass_drops()) {
    emit_subtree_verbatim();
  }
}

void uPass_runner::process_verbatim(Pass_method fn) {
  dispatch_to_passes(fn);
  // Emit the op-node with operand folding (skipping LHS at child 0). This
  // matches A_OP behavior except we never drop the statement — verbatim
  // ops (tuple_*, attr_*, range, io, delay_assign) carry side-effects or
  // shape information that downstream passes still need to see, but their
  // ref operands should fold so the staged tree doesn't carry references
  // to tmps whose producing op was dropped (lnastfmt's read-without-write
  // check would correctly flag the dangling refs otherwise).
  emit_op_with_fold(/*fold_all=*/false);
}

// ── Top-level run loop ────────────────────────────────────────────────────────

void uPass_runner::run(std::size_t max_iters) {
  if (configuration_error) {
    std::print("uPass - invalid configuration: {}\n", configuration_error_msg);
    return;
  }

  if (upasses.empty()) {
    std::print("uPass - no passes configured\n");
    return;
  }

  // Step H — allocate the dest (staging) body in a runner-owned Forest
  // (conceptually the "lgdb/optimized" forest the plan describes; today
  // it's in-memory only, no on-disk lgdb path resolution yet). pass_upass
  // still picks the tree out via take_staging() and replace_body()s the
  // input Lnast so any holder of the input picks up the new body.
  //
  // With single-walk dispatch (Step M) the lambda only runs once.
  if (!dest_forest_) {
    dest_forest_ = hhds::Forest::create();
  }
  auto fresh_staging = [&]() {
    auto body            = dest_forest_->create_tree_temp(std::format("optimized-{}", lm->get_top_module_name()));
    staging              = std::make_shared<Lnast>(body, lm->get_top_module_name());
    staging_parent       = staging->set_root(Lnast_ntype::create_top());
    staging_parent_stack = {};
  };
  fresh_staging();

  // Step M — single walk per invocation. Runner_fixed_point is no longer
  // invoked; the `max_iters` label on `pass.upass` still parses but is
  // ignored (kept for backwards-compat with test drivers that pass
  // max_iters:1). mark_changed() calls on passes still run for
  // diagnostics but no second iteration follows.
  (void)max_iters;
  for (auto& entry : upasses) {
    entry.pass->begin_iteration();
  }
  process_lnast();
  // Single-walk now converges trivially. Print the diagnostic so the
  // dependency / convergence tests (upass_noop_first_iter_test.sh,
  // upass_lnast_shared_scan_test.sh, upass_lnast_shared_decide_test.sh)
  // still see the marker they grep for.
  std::print("uPass - converged at iteration 1\n");

  // Post-walk DCE on staging — removes definition statements (assign,
  // tuple_add, attr_set, etc.) whose dst has no surviving downstream
  // reader. Constprop is conservative about multi-entry tuple bundles
  // (their dst can't fold via fold_ref's single-value return), so it
  // emits orphan tuple_add+assign+attr_set chains for fully-constant
  // tuples even when every consumer was already folded away. The DCE
  // cleans those up.
  dead_code_eliminate_staging();

  // Step J — dest-walk finisher dispatch. Passes that want to inspect
  // the freshly-built staging tree (verifier/assert cassert counts in
  // the redesign) override walk_dest. Default no-op; today no pass
  // uses this, but the hook is available for migration.
  if (staging) {
    for (auto& entry : upasses) {
      entry.pass->walk_dest(staging);
    }
  }

  // Per-pass finalization. Runs after all iterations finish — passes use
  // this to emit summaries or enforce end-of-run invariants (see
  // uPass_verifier::end_run, which compares cassert tallies against
  // expected counts).
  for (auto& entry : upasses) {
    entry.pass->end_run();
    auto produced = entry.pass->take_new_lnasts();
    new_lnasts.insert(new_lnasts.end(), produced.begin(), produced.end());
  }
}

// ── Post-walk DCE ────────────────────────────────────────────────────────────

namespace {

// True iff `t` is a definition-producing op whose first child is a ref
// to the value being defined. These are the ops eligible for DCE when
// their dst is unused. attr_set is included because it "defines" the
// attribute side-channel on its first-child target; an attr_set on a
// dead name is itself dead.
bool dce_is_def_producing(Lnast_ntype::Lnast_ntype_int t) {
  using N = Lnast_ntype;
  switch (t) {
    case N::Lnast_ntype_assign:
    case N::Lnast_ntype_dp_assign:
    case N::Lnast_ntype_tuple_add:
    case N::Lnast_ntype_tuple_concat:
    case N::Lnast_ntype_tuple_get:
    case N::Lnast_ntype_plus:
    case N::Lnast_ntype_minus:
    case N::Lnast_ntype_mult:
    case N::Lnast_ntype_div:
    case N::Lnast_ntype_mod:
    case N::Lnast_ntype_shl:
    case N::Lnast_ntype_sra:
    case N::Lnast_ntype_sext:
    case N::Lnast_ntype_set_mask:
    case N::Lnast_ntype_get_mask:
    case N::Lnast_ntype_bit_and:
    case N::Lnast_ntype_bit_or:
    case N::Lnast_ntype_bit_xor:
    case N::Lnast_ntype_bit_not:
    case N::Lnast_ntype_red_and:
    case N::Lnast_ntype_red_or:
    case N::Lnast_ntype_red_xor:
    case N::Lnast_ntype_log_and:
    case N::Lnast_ntype_log_or:
    case N::Lnast_ntype_log_not:
    case N::Lnast_ntype_eq:
    case N::Lnast_ntype_ne:
    case N::Lnast_ntype_lt:
    case N::Lnast_ntype_le:
    case N::Lnast_ntype_gt:
    case N::Lnast_ntype_ge:
    case N::Lnast_ntype_is:
    case N::Lnast_ntype_func_call:
    case N::Lnast_ntype_func_does:
    case N::Lnast_ntype_func_equals:
    case N::Lnast_ntype_func_in:
    case N::Lnast_ntype_func_has:
    case N::Lnast_ntype_func_case:
    case N::Lnast_ntype_attr_set:
    case N::Lnast_ntype_attr_get:
      return true;
    default:
      return false;
  }
}

// `attr_set TARGET 'type' 'mut'|'reg'` records a user-visible storage
// class declaration; even when the name has no surviving readers, we
// keep the declaration so downstream consumers (lnastfmt, bitwidth)
// still see it.
bool dce_is_keepalive_attr_set(const Lnast& staging, const Lnast_nid& node) {
  if (staging.get_type(node) != Lnast_ntype::Lnast_ntype_attr_set) {
    return false;
  }
  auto tgt = staging.get_first_child(node);
  if (!tgt.is_valid()) {
    return false;
  }
  auto key = staging.get_sibling_next(tgt);
  if (!key.is_valid() || staging.get_type(key) != Lnast_ntype::Lnast_ntype_const) {
    return false;
  }
  if (staging.get_name(key) != "type") {
    return false;
  }
  auto val = staging.get_sibling_next(key);
  if (!val.is_valid() || staging.get_type(val) != Lnast_ntype::Lnast_ntype_const) {
    return false;
  }
  auto v = staging.get_name(val);
  return v == "mut" || v == "reg";
}

}  // namespace

void uPass_runner::dead_code_eliminate_staging() {
  if (!staging) {
    return;
  }
  // Only run DCE when constprop is active. DCE relies on its fold to
  // settle if-bodies and tuple_get expansions; without it the runner's
  // branch-elimination hasn't pruned dead arms and the use-count would
  // be wildly off. Skip on func_extract-spawned function bodies: their
  // outputs are consumed externally and would look unreferenced from
  // inside-the-body alone.
  bool has_constprop = false;
  for (auto& e : upasses) {
    if (e.name == "constprop") {
      has_constprop = true;
      break;
    }
  }
  if (!has_constprop || is_function_body_) {
    return;
  }
  using N = Lnast_ntype;

  // Compute the live-statement set via iterative liveness on the
  // staging tree, then rebuild a fresh tree containing only the live
  // statements. Avoiding in-place delete_subtree dodges HHDS's
  // pre-order iterator transiently yielding default-constructed
  // Node_class instances for deleted slots — downstream lnastfmt walks
  // would crash on the unchecked `get_type` that follows.

  // Set of statement nids in the staging tree that should be dropped.
  absl::flat_hash_set<int64_t> dead_stmts;

  bool changed = true;
  while (changed) {
    changed = false;

    // Pass 1: count uses (refs in non-LHS positions) for each name. Refs
    // that live inside an already-dead statement don't count.
    absl::flat_hash_map<std::string, int> use_count;
    for (auto node : staging->depth_preorder(staging->get_root())) {
      if (node.is_invalid()) {
        continue;
      }
      if (staging->get_type(node) != N::Lnast_ntype_ref) {
        continue;
      }
      auto parent = staging->get_parent(node);
      if (!parent.is_valid()) {
        continue;
      }
      bool inside_dead = false;
      for (auto a = parent; a.is_valid(); a = staging->get_parent(a)) {
        if (dead_stmts.contains(a.get_class_index().value)) {
          inside_dead = true;
          break;
        }
      }
      if (inside_dead) {
        continue;
      }
      const bool is_lhs = dce_is_def_producing(staging->get_type(parent)) && node.is_first_child();
      if (!is_lhs) {
        ++use_count[std::string(staging->get_name(node))];
      }
    }

    // Pass 2: extend the dead set with statement-level defs whose dst
    // is unused. A node is statement-level only when its direct parent
    // is a `stmts` block — nested `assign` nodes living inside a
    // tuple_add (field-label payload, not a real statement) must be
    // skipped. attr_set with `type`=mut|reg is a keepalive marker
    // (storage class declarations survive even when the name has no
    // surviving readers). Empty stmts subtrees are also dropped
    // (constprop's dead-branch elimination collapses if/else with
    // known conds to an empty stmts node so block scope survives;
    // once nothing in there had effects, the wrapper itself is noise).
    for (auto node : staging->depth_preorder(staging->get_root())) {
      if (node.is_invalid()) {
        continue;
      }
      const auto key = node.get_class_index().value;
      if (dead_stmts.contains(key)) {
        continue;
      }
      auto parent = staging->get_parent(node);
      if (!parent.is_valid()) {
        continue;
      }
      const bool parent_is_stmts = staging->get_type(parent) == N::Lnast_ntype_stmts;
      auto t = staging->get_type(node);

      // Empty stmts subtree (other than the body shell directly under top).
      if (t == N::Lnast_ntype_stmts) {
        bool has_live_child = false;
        for (auto c = node.first_child(); c.is_valid(); c = c.next_sibling()) {
          if (!dead_stmts.contains(c.get_class_index().value)) {
            has_live_child = true;
            break;
          }
        }
        if (!has_live_child && staging->get_type(parent) != N::Lnast_ntype_top) {
          dead_stmts.insert(key);
          changed = true;
        }
        continue;
      }

      if (!parent_is_stmts) {
        continue;  // payload node inside an op (e.g. nested assign in tuple_add)
      }
      if (!dce_is_def_producing(t)) {
        continue;
      }
      if (dce_is_keepalive_attr_set(*staging, node)) {
        continue;
      }
      auto fc = staging->get_first_child(node);
      if (!fc.is_valid() || staging->get_type(fc) != N::Lnast_ntype_ref) {
        continue;
      }
      // Only DCE temporary defs (`___N`). User-named variables (out, tmp,
      // alt …) may be observable outputs or have meaning to downstream
      // consumers that DCE can't see (IO ports lose their `%`/`$` prefix
      // by the time they hit staging). Dropping a user-named def with no
      // in-tree readers would silently delete writes the user intended.
      const auto fc_name = staging->get_name(fc);
      if (!Lnast::is_tmp(fc_name)) {
        continue;
      }
      auto it = use_count.find(std::string(fc_name));
      if (it == use_count.end() || it->second == 0) {
        dead_stmts.insert(key);
        changed = true;
      }
    }
  }

  if (dead_stmts.empty()) {
    return;
  }

  // Rebuild staging into a fresh forest body, copying every live node.
  // Once we descend into a non-structural op (anything that isn't
  // top/stmts/if/while/for), every descendant is statement payload and
  // is copied unconditionally — only structural-level statements get
  // dead-checked.
  auto fresh_body  = dest_forest_->create_tree_temp(std::format("optimized-{}", lm->get_top_module_name()));
  auto new_staging = std::make_shared<Lnast>(fresh_body, lm->get_top_module_name());

  auto src_root = staging->get_root();
  auto dst_root = new_staging->set_root(staging->get_type(src_root));

  auto is_structural = [](Lnast_ntype::Lnast_ntype_int t) {
    return t == N::Lnast_ntype_top || t == N::Lnast_ntype_stmts || t == N::Lnast_ntype_if
           || t == N::Lnast_ntype_while || t == N::Lnast_ntype_for;
  };

  std::function<void(const Lnast_nid&, const Lnast_nid&, bool)> copy_subtree;
  copy_subtree = [&](const Lnast_nid& src, const Lnast_nid& dst, bool inside_payload) {
    auto fc = src.first_child();
    while (fc.is_valid()) {
      const bool is_dead_stmt = !inside_payload && dead_stmts.contains(fc.get_class_index().value);
      if (!is_dead_stmt) {
        auto t = staging->get_type(fc);
        Lnast_nid new_child;
        if (t == N::Lnast_ntype_ref) {
          new_child = new_staging->add_child(dst, Lnast_node::create_ref(staging->get_name(fc)));
        } else if (t == N::Lnast_ntype_const) {
          new_child = new_staging->add_child(dst, Lnast_node::create_const(staging->get_name(fc)));
        } else {
          new_child = new_staging->add_child(dst, t);
          copy_subtree(fc, new_child, inside_payload || !is_structural(t));
        }
      }
      fc = fc.next_sibling();
    }
  };
  copy_subtree(src_root, dst_root, false);

  staging = new_staging;
}

// ── Node dispatch ─────────────────────────────────────────────────────────────

void uPass_runner::process_lnast() {
  using Ntype = Lnast_ntype;

  // clang-format off
  // Category A: drop-candidate op-nodes. First child is the LHS/dst (not
  // folded); subsequent ref children are fed to fold_ref.
#define A_OP(NAME)                                                                              \
  case Ntype::Lnast_ntype_##NAME:                                                               \
    process_drop_candidate(&upass::uPass::process_##NAME, /*fold_all=*/false);                  \
    break;

  // Category C: emit verbatim; still dispatch so passes can observe.
#define C_OP(NAME)                                                                              \
  case Ntype::Lnast_ntype_##NAME: process_verbatim(&upass::uPass::process_##NAME); break;

  switch (get_raw_ntype()) {
    // Structural — push the node into staging and recurse.
    case Ntype::Lnast_ntype_top:   process_top(); break;
    case Ntype::Lnast_ntype_stmts: process_stmts(); break;
    case Ntype::Lnast_ntype_if:    process_if(); break;

    // Statement-scope leaves (e.g. an if condition's ref/const). Fold refs
    // through the symbol table so dropping the producing assign doesn't
    // leave a dangling name.
    case Ntype::Lnast_ntype_ref: emit_ref_or_folded(lm->current_text()); break;
    case Ntype::Lnast_ntype_const:
      emit_leaf(lm->current_node());
      break;

    // Assignment
    A_OP(assign)

    // Bitwidth
    A_OP(bit_and)
    A_OP(bit_or)
    A_OP(bit_not)
    A_OP(bit_xor)

    // Bitwidth Insensitive Reduce
    A_OP(red_or)
    A_OP(red_and)
    A_OP(red_xor)

    // Logical
    A_OP(log_and)
    A_OP(log_or)
    A_OP(log_not)

    // Arithmetic
    A_OP(plus)
    A_OP(minus)
    A_OP(mult)
    A_OP(div)
    A_OP(mod)

    // Shift
    A_OP(shl)
    A_OP(sra)

    // Bit Manipulation
    A_OP(sext)
    A_OP(set_mask)
    A_OP(get_mask)

    // Comparison
    A_OP(ne)
    A_OP(eq)
    A_OP(lt)
    A_OP(le)
    A_OP(gt)
    A_OP(ge)
    A_OP(is)

    // Function Call — treated like arithmetic so constprop can fold the
    // built-in typecast calls (int/uint/string/uNN/sNN); anything constprop
    // declines to handle stays un-folded and the statement is emitted.
    A_OP(func_call)
    // does/in/has/case fold to a known boolean (or nil) → drop-candidate.
    A_OP(func_does)
    A_OP(func_equals)
    A_OP(func_in)
    A_OP(func_has)
    A_OP(func_case)
    // break/continue/return have no comptime fold yet — emit verbatim.
    C_OP(func_break)
    C_OP(func_continue)
    C_OP(func_return)
    case Ntype::Lnast_ntype_func_def:
      process_drop_candidate_verbatim(&upass::uPass::process_func_def);
      break;
    C_OP(io)

    // Tuple Operations — Slice 1 pass-through (Slice 6 flattens).
    //
    // tuple_add / tuple_get produce a fresh tmp bundle (or a folded scalar
    // extracted from a bundle); their dst is a tmp that's purely scaffolding.
    // Once the destination is resolved (is_known_const true on its first
    // entry), dropping the stmt is safe — consumers either fold the value
    // via fold_ref or read the bundle directly from the symbol table.
    // Without this, for-loop unrolls leave behind orphan
    // `tuple_add ___N = (i,)` stmts whose tuple_concat / assign-to-c
    // consumers got dropped, producing dead code with dangling tmp refs.
    A_OP(tuple_get)
    A_OP(tuple_add)
    // tuple_set mutates its LHS bundle in place — never drop; the side
    // effect on the bundle is the whole point.
    C_OP(tuple_set)
    // tuple_concat folds when every operand is a known scalar (string/int
    // concat via Lconst::concat_op); treat like arithmetic so classify can
    // drop the statement once the destination is resolved.
    A_OP(tuple_concat)
    // Range nodes carry start/end for slicing (`x[a..=b]` / `x[a..]`). They
    // must dispatch so constprop can stash bounds before the consuming
    // tuple_get folds. C_OP keeps the node visible for downstream passes.
    C_OP(range)

    // Attribute Statements — Slice 1 pass-through (Slice 5 lifts to side-map).
    C_OP(attr_set)
    C_OP(attr_get)

    // Type metadata — emit verbatim, but dispatch so the attribute pass
    // observes type_spec / type_def for max/min/bits derivation.
    C_OP(type_spec)
    C_OP(type_def)

    // Cassert — emit with all operand refs folded (Slice 2 gives this to
    // verifier so known-true cassert gets dropped).
    case Ntype::Lnast_ntype_cassert:
      process_drop_candidate(&upass::uPass::process_cassert, /*fold_all=*/true);
      break;

    // Delay-assign carries timing; emit verbatim (see upass.md invariant 6).
    C_OP(delay_assign)

    default:
      // Unknown / not-yet-handled node type: copy its subtree verbatim so
      // nothing silently disappears from the output tree. Add an explicit
      // A_OP/C_OP entry above when folding behavior is needed.
      emit_subtree_verbatim();
      break;
  }

#undef A_OP
#undef C_OP
  // clang-format on
}

// ── Structural handlers ───────────────────────────────────────────────────────

void uPass_runner::process_top() {
  // staging_parent is the already-materialized root slot; overwrite its
  // data with the input top node (preserves the correct text/token).
  staging->set_data(staging_parent, lm->current_type());

  if (lm->has_child()) {
    lm->move_to_child();
    do {
      process_lnast();
    } while (lm->move_to_sibling());
    lm->move_to_parent();
  }
}

void uPass_runner::process_stmts() {
  // Pre-dispatch lets passes push a block scope before children are
  // walked; post-dispatch (after emit_pop) lets them pop it. The cursor
  // is restored by dispatch_to_passes around each pass call, so passes
  // can move freely without disturbing the runner's traversal.
  //
  // Step D — the runner-owned symbol-table push/pop will land here once
  // Step F's bundle pre-pass populates it. For now the runner-owned
  // table sits idle (a pass that needs it can push/pop via the pointer
  // returned by get_runner_symbol_table()).
  dispatch_to_passes(&upass::uPass::process_stmts);
  emit_push(lm->current_type());
  if (lm->has_child()) {
    lm->move_to_child();
    do {
      process_lnast();
    } while (lm->move_to_sibling());
    lm->move_to_parent();
  }
  // Pre-pop hook fires while the staging cursor is still inside the stmts
  // block, so a pass (e.g. coalescer) can flush deferred writes into the
  // closing block rather than the parent scope. process_stmts_post fires
  // after the pop and is for tear-down (e.g. constprop's scope leave).
  dispatch_to_passes(&upass::uPass::process_stmts_pre_pop);
  emit_pop();
  dispatch_to_passes(&upass::uPass::process_stmts_post);
}

void uPass_runner::process_if() {
  // Dispatch first so passes can update their symbol tables from the condition.
  dispatch_to_passes(&upass::uPass::process_if);

  // Slice 7 — dead-branch elimination.
  // When the first child is a comptime-known condition (ref or const), emit
  // only the taken branch's stmts spliced into the parent (no if node).
  // Cursor invariant: must be at the if-node on all exit paths.
  // See upass.md §Slice 7 and §5 (if cursor discipline).
  //
  // Two if shapes are recognized:
  //   * Scoped form: (cond, stmts, [cond, stmts]…, [stmts]) — normal
  //     if/elif/else. The body's `stmts` wrapper is a real scope.
  //   * Flat form (when/unless): (cond, stmt, [stmt]…) — gated stmts with
  //     no `stmts` wrapper. The body is executed in the parent scope when
  //     the cond is true. Emitted by prp2lnast for `s when c` / `s unless c`.
  //     when/unless conditions are required to be comptime-known; if the
  //     fold here fails, downstream verifier flags it as a build error.
  if (lm->has_child()) {
    lm->move_to_child();
    using Ntype    = Lnast_ntype;
    const auto raw = lm->get_raw_ntype();

    if (raw != Ntype::Lnast_ntype_stmts) {
      // First child is a condition (ref or const). Try to fold it.
      std::optional<Const> cval;
      if (raw == Ntype::Lnast_ntype_const) {
        cval = *Dlop::from_pyrope(lm->current_text());
      } else {
        cval = try_fold_ref(lm->current_text());
      }

      // Peek at the body shape: if the second child is not `stmts`, this
      // is a flat (when/unless) if. Flat ifs have no else/elif chain.
      bool is_flat = false;
      if (lm->move_to_sibling()) {
        is_flat = lm->get_raw_ntype() != Ntype::Lnast_ntype_stmts;
      }
      // The peek above moved cursor onto the second child (or invalid
      // if there isn't one). The original move_to_child pushed the
      // if-node onto the cursor stack, so a single move_to_parent
      // restores cursor to the if-node and unwinds that push. After
      // that we re-enter via move_to_child to leave the cursor at the
      // condition again — exactly mirroring the pre-peek state.
      lm->move_to_parent();
      lm->move_to_child();

      if (is_flat) {
        // Flat form: cond known-true → emit each body stmt in parent scope;
        // cond known-false → drop entirely; cond unknown → emit verbatim
        // (when/unless conditions must be comptime-known; the verifier
        // reports the build error downstream).
        if (cval && !cval->is_invalid() && !cval->has_unknowns() && !cval->is_nil()) {
          const bool taken = !cval->is_known_false();
          if (taken) {
            while (lm->move_to_sibling()) {
              process_lnast();
            }
          }
          lm->move_to_parent();
          return;  // pruned — no if node emitted
        }
        // Unknown cond: emit the if and its children verbatim, no
        // dispatch into the body (we don't want the body's effects to
        // mutate the symbol table when the gate didn't fire).
        lm->move_to_parent();  // matches the move_to_child above
        emit_subtree_verbatim();
        return;
      }

      if (cval && !cval->is_invalid() && !cval->has_unknowns() && !cval->is_nil()) {
        const bool taken = !cval->is_known_false();

        // Advance past the condition to the then-stmts.
        if (lm->move_to_sibling()) {
          if (taken) {
            // Emit the then-stmts block (preserving its scope) — the if
            // node itself is dropped, but the stmts wrapper stays so that
            // `mut x = ...` inside the body remains scoped to that block
            // instead of leaking into the parent.
            process_lnast();
            lm->move_to_parent();  // back to if-node
            return;                // pruned — no if node emitted
          }

          // Condition is false: skip the then-stmts, look for an else-stmts.
          if (lm->move_to_sibling()) {
            if (lm->get_raw_ntype() == Ntype::Lnast_ntype_stmts) {
              // Bare else-stmts: emit it (preserving its scope), drop the if.
              process_lnast();
              lm->move_to_parent();  // back to if-node
              return;                // pruned
            }
            // elif chain: fall through to full-if emit with per-arm dead
            // branch tracking (see loop below).
          } else {
            // No else-stmts: false condition → emit nothing.
            lm->move_to_parent();  // back to if-node
            return;                // pruned
          }
        }
      }
    }

    lm->move_to_parent();  // condition unknown or elif chain — fall through
  }

  // Unknown condition (or elif chain): emit the full if node unchanged.
  emit_push(lm->current_type());
  if (lm->has_child()) {
    lm->move_to_child();
    // Branch elimination for known conditions. The if's children alternate
    // (cond, stmts) pairs, with an optional trailing stmts (else). For
    // every cond/stmts pair we peek at the cond's folded value:
    //   - known-false: emit the stmts subtree verbatim (no constprop
    //     dispatch into the body) so dead-branch assigns can't update
    //     the symbol table. Without this, `if false { c = 2 }` would
    //     overwrite c=1 because constprop's process_assign runs
    //     unconditionally during traversal.
    //   - known-true (and we haven't seen a true arm yet): process the
    //     body normally; mark a "matched" flag so any later arms / else
    //     are emit-verbatim'd (only the first matching arm runs).
    //   - matched-already (previous arm was known-true): emit verbatim.
    //   - unknown / partially-known: process normally (conservative —
    //     the symbol table merges values across both branches today).
    bool last_cond_false     = false;
    bool last_cond_true      = false;
    bool last_was_cond       = false;
    bool already_matched     = false;  // a *previous* arm already fired
    bool any_prior_uncertain = false;  // some earlier cond folded to neither true nor false

    auto cond_value = [this]() -> std::optional<Const> {
      if (lm->get_raw_ntype() == Lnast_ntype::Lnast_ntype_const) {
        try {
          return *Dlop::from_pyrope(lm->current_text());
        } catch (...) {
          return std::nullopt;
        }
      }
      if (lm->get_raw_ntype() == Lnast_ntype::Lnast_ntype_ref) {
        return try_fold_ref(lm->current_text());
      }
      return std::nullopt;
    };

    do {
      auto t = lm->get_raw_ntype();
      if (t == Lnast_ntype::Lnast_ntype_stmts) {
        // Body for the prior cond, or the trailing else (no prior cond
        // this round). Dead iff a prior arm has already fired, or the
        // immediate cond just folded to false.
        const bool dead = already_matched || (last_was_cond && last_cond_false);
        // `just_matched` only fires when no earlier arm was uncertain — if
        // a prior cond was undecided at comptime (nil/unknown) the runtime
        // ordering may still pick *that* arm, so a later concrete-true
        // cond doesn't deterministically take over. Treat the body as
        // uncertain instead so its writes get invalidated on exit.
        // Without this gate, a `match` chain where `case (a=33,…)` folds
        // to nil and `case (a=2,…)` folds to true would still concretely
        // apply case-2's body, leaving the var "definitely 1052" even
        // though case-1 might actually fire at runtime.
        const bool just_matched = last_was_cond && last_cond_true && !any_prior_uncertain;
        // Uncertain := body executes but isn't *guaranteed* to. After a
        // cond: !just_matched (dead is handled separately, so the cond
        // wasn't known-false). Trailing else with no preceding cond: only
        // uncertain when some prior arm's cond didn't fold either way; if
        // every prior cond folded to known-false the else *is* guaranteed.
        const bool uncertain = last_was_cond ? !just_matched : any_prior_uncertain;
        last_was_cond        = false;
        if (dead) {
          emit_subtree_verbatim();
          continue;
        }
        if (uncertain) {
          dispatch_to_passes(&upass::uPass::notify_uncertain_arm_begin);
        }
        process_lnast();
        if (uncertain) {
          dispatch_to_passes(&upass::uPass::notify_uncertain_arm_end);
        }
        // Process the body first, THEN flip the matched flag — so the
        // current arm's body actually dispatches into constprop. Only
        // *subsequent* arms / else become dead.
        if (just_matched) {
          already_matched = true;
        }
        continue;
      }

      // Non-stmts child — must be a cond (ref/const).
      auto val = cond_value();
      // nil cond models "comptime can't decide" (e.g. a `case` whose values
      // didn't match but whose runtime predicate might still fire). Treat
      // it the same as has_unknowns(): not known-true and not known-false,
      // so the arm body is visited as uncertain rather than dead-pruned.
      const bool val_is_nil = val.has_value() && !val->is_invalid() && val->is_nil();
      last_cond_true        = val.has_value() && !val->is_invalid() && !val_is_nil && val->is_known_true();
      last_cond_false       = val.has_value() && !val->is_invalid() && !val_is_nil && val->is_known_false();
      last_was_cond         = true;
      if (!last_cond_true && !last_cond_false) {
        any_prior_uncertain = true;
      }
      process_lnast();
    } while (lm->move_to_sibling());
    lm->move_to_parent();
  }
  emit_pop();
}
