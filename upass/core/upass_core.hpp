//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stack>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/inlined_vector.h"
#include "const.hpp"
#include "lnast.hpp"
#include "lnast_manager.hpp"
#include "lnast_ntype.hpp"
#include "lnast_writer.hpp"
#include "upass_bundle.hpp"
#include "upass_utils.hpp"

namespace upass {

// Free-form, per-pass configuration threaded from `pass.upass` labels down
// through the runner to individual passes. Pass_upass collects the labels
// it cares about and stuffs them in here; each pass pulls the keys it
// recognizes in its `set_options` override. Unknown keys are silently
// ignored so new labels can be added without a whole-stack change.
using Options_map = std::map<std::string, std::string>;

enum class Emit_kind { emit, drop_subtree };

struct Emit_decision {
  Emit_kind kind{Emit_kind::emit};

  static constexpr Emit_decision emit_node() { return Emit_decision{Emit_kind::emit}; }
  static constexpr Emit_decision drop() { return Emit_decision{Emit_kind::drop_subtree}; }
};

// Runner-resolved operand list for the current source op node. Built once
// per drop-candidate / verbatim op by the runner before dispatching to
// passes; passes can consume it via runner_op_summary_fn instead of
// walking children themselves. See docs/upass_redesign.md §6 (the future
// shape uses {Runtime_trivial, Const, shared_ptr<Bundle>}; this is the
// pre-Bundle landing zone using raw {text, ntype} pairs so existing passes
// can adopt it without depending on the full Bundle migration).
struct Op_operand {
  std::string_view             text;
  Lnast_ntype::Lnast_ntype_int kind;
};

struct Op_summary {
  std::string_view                       lhs;
  absl::InlinedVector<Op_operand, 4>     operands;     // every child after the LHS
  std::size_t                            ref_count{0}; // ref-typed entries in `operands`
  bool                                   is_assign{false};
  bool                                   is_alias{false}; // assign with exactly one ref operand
};

struct uPass {
public:
  uPass(std::shared_ptr<Lnast_manager>& _lm) : lm(_lm) {}
  virtual ~uPass() = default;

  virtual void begin_iteration() { changed = false; }
  bool         has_changed() const { return changed; }

  // Called by the runner on every drop-candidate op-node (assign, plus, eq, …)
  // after its per-node process_* has populated any ST state. Returning `drop`
  // tells the runner to omit the node (and its subtree) from the staging tree.
  // Default: always emit.
  virtual Emit_decision classify_statement() { return Emit_decision::emit_node(); }
  // Self-declared capability: only the passes that actually return drop on
  // some inputs need to be polled. The default-no-op overhead matters on
  // bulk-arithmetic workloads where any_pass_drops() runs per emitted op.
  virtual bool overrides_classify_statement() const { return false; }

  // Called by the runner for every `ref <name>` operand it is about to emit
  // (RHS of an op, condition of an `if`, cassert operand). If any pass
  // returns a concrete Const, the runner writes `const <value>` in place of
  // the ref. First non-nullopt wins.
  virtual std::optional<Const> fold_ref(std::string_view /*name*/) { return std::nullopt; }

  // Self-declared capability: derived passes that override `fold_ref` return
  // true so the runner can precompute the dispatch list and skip the no-op
  // default for the others. On bulk-arithmetic workloads (1M+ refs), this
  // eliminates ~5 virtual calls per fold attempt — measurable since
  // `try_fold_ref` runs on every ref operand of every emitted op.
  virtual bool overrides_fold_ref() const { return false; }

  // Runner-supplied helper that delegates to `try_fold_ref` across every
  // registered pass. Passes can use this to resolve a ref against the
  // aggregate symbol-table state (e.g. verifier consulting constprop's ST
  // to decide whether a cassert operand is comptime-known).
  //
  // The runner wires this up after every pass is constructed (so the
  // callback sees the full pass list). Before that point it is empty —
  // defensive callers should check before invoking.
  using Fold_fn = std::function<std::optional<Const>(std::string_view)>;
  void set_runner_fold_fn(Fold_fn fn) { runner_fold_fn = std::move(fn); }

  // Runner-supplied helper that re-emits the op-node at `src` (with operand
  // folding through try_fold_ref) at the runner's current staging cursor.
  // Used by the coalescer to flush a deferred (parked) statement: the source
  // LNAST nid is captured at park time and replayed here when a downstream
  // read or scope exit triggers the flush. The runner saves/restores its
  // own read cursor around the replay so passes can call this from inside
  // their process_* without disturbing the in-progress traversal.
  using Emit_at_fn = std::function<void(const Lnast_nid&)>;
  void set_runner_emit_at_fn(Emit_at_fn fn) { runner_emit_at_fn = std::move(fn); }

  // Runner-supplied pointer to the operand summary of the *current* source
  // op node (LHS string + every child operand with its LNAST type). The
  // runner refills this before each drop-candidate / verbatim dispatch, so
  // a pass can read the operand layout in O(1) instead of walking children
  // itself. Direct pointer (not std::function) so the hot-path consumers
  // pay no indirection cost — the underlying object stays valid for the
  // pass lifetime. See §6.
  void set_runner_op_summary_ptr(const Op_summary* p) { runner_op_summary = p; }

  // Consume per-pass options (see Options_map). Default: no-op. Passes
  // override to pull the keys they care about. Called once, before run().
  virtual void set_options(const Options_map& /*opts*/) {}

  // Called once after the last iteration of the run completes. Passes can
  // use it to emit summaries or assert invariants that only make sense
  // once the full traversal is done (e.g. verifier comparing its cassert
  // tallies against expected counts).
  virtual void end_run() {}

  // Side-output LNASTs produced by structural lowering passes. The runner
  // collects these after end_run() and hands them to pass.upass so they can be
  // appended to Eprp_var::lnasts.
  virtual std::vector<std::shared_ptr<Lnast>> take_new_lnasts() { return {}; }

  // Called by the runner immediately before / after descending into an if-arm
  // whose condition didn't fold to a comptime-known true or false. Constprop
  // overrides to mark the next pushed block scope so any var assigned inside
  // gets invalidated when the arm exits. Default: no-op.
  virtual void notify_uncertain_arm_begin() {}
  virtual void notify_uncertain_arm_end() {}

#define PROCESS_NODE(NAME) \
  virtual void process_##NAME() {}

  // Assignment
  PROCESS_NODE(assign)
  PROCESS_NODE(delay_assign)

  // Bitwidth
  PROCESS_NODE(bit_and)
  PROCESS_NODE(bit_or)
  PROCESS_NODE(bit_not)
  PROCESS_NODE(bit_xor)

  // Bitwidth Insensitive Reduce
  PROCESS_NODE(red_or)
  PROCESS_NODE(red_and)
  PROCESS_NODE(red_xor)

  // Logical
  PROCESS_NODE(log_and)
  PROCESS_NODE(log_or)
  PROCESS_NODE(log_not)

  // Arithmetic
  PROCESS_NODE(plus)
  PROCESS_NODE(minus)
  PROCESS_NODE(mult)
  PROCESS_NODE(div)
  PROCESS_NODE(mod)

  // Shift
  PROCESS_NODE(shl)
  PROCESS_NODE(sra)

  // Bit Manipulation
  PROCESS_NODE(sext)
  PROCESS_NODE(set_mask)
  PROCESS_NODE(get_mask)

  // Comparison
  PROCESS_NODE(ne)
  PROCESS_NODE(eq)
  PROCESS_NODE(lt)
  PROCESS_NODE(le)
  PROCESS_NODE(gt)
  PROCESS_NODE(ge)

  // Function Call
  PROCESS_NODE(func_call)
  PROCESS_NODE(func_def)
  PROCESS_NODE(io)

  // Pseudo-function markers (typed replacements for the const(name)-first
  // func_call form). does/in/has fold; the rest are emitted verbatim.
  PROCESS_NODE(func_does)
  PROCESS_NODE(func_equals)
  PROCESS_NODE(func_in)
  PROCESS_NODE(func_has)
  PROCESS_NODE(func_case)
  PROCESS_NODE(func_break)
  PROCESS_NODE(func_continue)
  PROCESS_NODE(func_return)

  // Tuple Operations
  PROCESS_NODE(tuple_get)
  PROCESS_NODE(tuple_set)
  PROCESS_NODE(tuple_add)
  PROCESS_NODE(tuple_concat)

  // Attributes
  PROCESS_NODE(attr_set)
  PROCESS_NODE(attr_get)

  // Control flow
  PROCESS_NODE(for)
  PROCESS_NODE(while)
  PROCESS_NODE(uif)
  PROCESS_NODE(range)
  PROCESS_NODE(cassert)

  // Types
  PROCESS_NODE(type_def)
  PROCESS_NODE(type_spec)

  // Structure
  PROCESS_NODE(top)
  PROCESS_NODE(stmts)
  // Called by the runner after children of a stmts node have been
  // processed but BEFORE the staging cursor pops out of the stmts block.
  // Used by the coalescer to flush any deferred writes still parked at
  // end-of-block: emissions land inside the closing stmts so order is
  // preserved. process_stmts_post fires after the pop and is the wrong
  // hook for emission — it would land in the parent scope.
  PROCESS_NODE(stmts_pre_pop)
  // Called by the runner after children of a stmts node have been
  // processed. Lets a pass tear down per-block state (e.g. constprop
  // popping a block scope it pushed in process_stmts).
  PROCESS_NODE(stmts_post)
  PROCESS_NODE(if)

#undef PROCESS_NODE

protected:
  std::shared_ptr<Lnast_manager>& lm;
  bool                            changed{false};
  Fold_fn                         runner_fold_fn;
  Emit_at_fn                      runner_emit_at_fn;
  const Op_summary*               runner_op_summary{nullptr};

  void move_to_nid(const Lnast_nid& nid) { lm->move_to_nid(nid); }
  auto current_text() const { return lm->current_text(); }
  auto move_to_child() { return lm->move_to_child(); }
  auto move_to_sibling() { return lm->move_to_sibling(); }
  void move_to_parent() { lm->move_to_parent(); }
  auto get_ntype() const { return lm->get_ntype(); }
  auto get_raw_ntype() const { return lm->get_raw_ntype(); }
  bool is_invalid() const { return lm->is_invalid(); }
  bool is_last_child() const { return lm->is_last_child(); }
  void write_node() { lm->write_node(); }
  void mark_changed() { changed = true; }

  template <class... Lnast_ntype_int>
  bool is_type(Lnast_ntype_int... ty) const {
    auto n = get_raw_ntype();
    return (((n == ty) || ...) || false);
  }
};

struct uPass_node : public uPass {
public:
  using uPass::uPass;
};

struct uPass_struct : uPass {
public:
  using uPass::uPass;
};

template <class T>
struct uPass_wrapper {
public:
  static std::shared_ptr<uPass> get_upass(std::shared_ptr<Lnast_manager>& lm) { return std::make_unique<T>(lm); }
};

class uPass_plugin {
public:
  using Setup_fn = std::function<std::shared_ptr<uPass>(std::shared_ptr<Lnast_manager>&)>;
  struct Setup_entry {
    Setup_fn                 setup_fn;
    std::vector<std::string> depends_on;
  };
  using Map_setup = std::map<std::string, Setup_entry>;

protected:
  static inline Map_setup registry;

public:
  uPass_plugin(const std::string& name, Setup_fn setup_fn, std::vector<std::string> depends_on = {}) {
    if (registry.find(name) != registry.end()) {
      upass::error("uPass_plugin: {} is already registered\n", name);
      return;
    }
    registry[name] = Setup_entry{.setup_fn = std::move(setup_fn), .depends_on = std::move(depends_on)};
  }

  static const Map_setup& get_registry() { return registry; }
};

}  // namespace upass
