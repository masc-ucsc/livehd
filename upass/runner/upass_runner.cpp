//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_runner.hpp"

#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

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
  auto fold_fn = [this](std::string_view name) { return try_fold_ref(name); };
  for (auto& entry : upasses) {
    entry.pass->set_runner_fold_fn(fold_fn);
    entry.pass->set_options(options);
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

std::vector<std::string> uPass_runner::changed_passes() const {
  std::vector<std::string> changed_list;
  for (const auto& entry : upasses) {
    if (entry.pass->has_changed()) {
      changed_list.emplace_back(entry.name);
    }
  }
  return changed_list;
}

// ── Staging emit helpers ──────────────────────────────────────────────────────

void uPass_runner::emit_push(const Lnast_node& node) {
  auto nid = staging->add_child(staging_parent, node);
  staging_parent_stack.push(staging_parent);
  staging_parent = nid;
}

void uPass_runner::emit_pop() {
  staging_parent = staging_parent_stack.top();
  staging_parent_stack.pop();
}

void uPass_runner::emit_leaf(const Lnast_node& node) {
  staging->add_child(staging_parent, node);
}

void uPass_runner::emit_subtree_verbatim() {
  auto node = lm->current_node();
  if (lm->has_child()) {
    emit_push(node);
    lm->move_to_child();
    do {
      emit_subtree_verbatim();
    } while (lm->move_to_sibling());
    lm->move_to_parent();
    emit_pop();
  } else {
    emit_leaf(node);
  }
}

std::optional<Lconst> uPass_runner::try_fold_ref(std::string_view name) {
  for (auto& entry : upasses) {
    auto folded = entry.pass->fold_ref(name);
    if (folded) {
      return folded;
    }
  }
  return std::nullopt;
}

void uPass_runner::emit_op_with_fold(bool fold_all) {
  auto op_node = lm->current_node();
  emit_push(op_node);

  if (lm->has_child()) {
    lm->move_to_child();
    int idx = 0;
    do {
      const bool is_lhs = (idx == 0) && !fold_all;
      if (!is_lhs && lm->get_raw_ntype() == Lnast_ntype::Lnast_ntype_ref) {
        auto folded = try_fold_ref(lm->current_text());
        if (folded && !folded->is_invalid()) {
          emit_leaf(Lnast_node::create_const(folded->to_pyrope()));
        } else {
          emit_leaf(lm->current_node());
        }
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
    // constprop's sub_op refuses a string-typed invalid Lconst) or is
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

void uPass_runner::process_drop_candidate(Pass_method fn, bool fold_all) {
  // 1. Run per-node process_* so symbol tables see the current statement.
  dispatch_to_passes(fn);

  // 2. Ask every pass whether to keep this statement. First drop wins.
  auto decision = upass::Emit_decision::emit_node();
  for (auto& entry : upasses) {
    auto d = entry.pass->classify_statement();
    if (d.kind == upass::Emit_kind::drop_subtree) {
      decision = d;
      break;
    }
  }

  // 3. Emit (with operand folding) unless dropped.
  if (decision.kind == upass::Emit_kind::emit) {
    emit_op_with_fold(fold_all);
  }
}

void uPass_runner::process_verbatim(Pass_method fn) {
  dispatch_to_passes(fn);
  emit_subtree_verbatim();
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

  // Initialize a fresh staging tree with the same top-module name as the
  // input. set_root() materializes the root slot; its data (the `top` node)
  // is overwritten by process_top() when it pushes.
  staging = std::make_shared<Lnast>(lm->get_top_module_name());
  staging->set_root(Lnast_node::create_top());
  staging_parent      = Lnast_nid::root();
  staging_parent_stack = {};

  upass::Runner_fixed_point::run(
      "uPass",
      max_iters,
      [this]() {
        for (auto& entry : upasses) {
          entry.pass->begin_iteration();
        }
      },
      [this]() { process_lnast(); },
      [this]() { return changed_passes(); });

  // Per-pass finalization. Runs after all iterations finish — passes use
  // this to emit summaries or enforce end-of-run invariants (see
  // uPass_verifier::end_run, which compares cassert tallies against
  // expected counts).
  for (auto& entry : upasses) {
    entry.pass->end_run();
  }
}

// ── Node dispatch ─────────────────────────────────────────────────────────────

void uPass_runner::process_lnast() {
  using Ntype = Lnast_ntype;

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
    case Ntype::Lnast_ntype_ref: {
      auto folded = try_fold_ref(lm->current_text());
      if (folded && !folded->is_invalid()) {
        emit_leaf(Lnast_node::create_const(folded->to_pyrope()));
      } else {
        emit_leaf(lm->current_node());
      }
      break;
    }
    case Ntype::Lnast_ntype_const: emit_leaf(lm->current_node()); break;

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
    A_OP(mask_and)
    A_OP(mask_popcount)
    A_OP(mask_xor)

    // Comparison
    A_OP(ne)
    A_OP(eq)
    A_OP(lt)
    A_OP(le)
    A_OP(gt)
    A_OP(ge)

    // Function Call — Slice 1 pass-through (Slice 3 reconstructs tuple args).
    C_OP(func_call)

    // Tuple Operations — Slice 1 pass-through (Slice 6 flattens).
    C_OP(tuple_get)
    C_OP(tuple_set)
    C_OP(tuple_add)

    // Attribute Statements — Slice 1 pass-through (Slice 5 lifts to side-map).
    C_OP(attr_set)
    C_OP(attr_get)

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
}

// ── Structural handlers ───────────────────────────────────────────────────────

void uPass_runner::process_top() {
  // staging_parent is the already-materialized root slot; overwrite its
  // data with the input top node (preserves the correct text/token).
  staging->set_data(staging_parent, lm->current_node());

  if (lm->has_child()) {
    lm->move_to_child();
    do {
      process_lnast();
    } while (lm->move_to_sibling());
    lm->move_to_parent();
  }
}

void uPass_runner::process_stmts() {
  emit_push(lm->current_node());
  if (lm->has_child()) {
    lm->move_to_child();
    do {
      process_lnast();
    } while (lm->move_to_sibling());
    lm->move_to_parent();
  }
  emit_pop();
}

void uPass_runner::process_if() {
  // Dispatch first so passes can inspect the condition before we emit.
  dispatch_to_passes(&upass::uPass::process_if);

  emit_push(lm->current_node());
  if (lm->has_child()) {
    lm->move_to_child();
    do {
      process_lnast();
    } while (lm->move_to_sibling());
    lm->move_to_parent();
  }
  emit_pop();
}
