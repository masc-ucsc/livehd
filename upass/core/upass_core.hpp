//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stack>
#include <string>
#include <string_view>
#include <vector>

#include "bundle.hpp"
#include "const.hpp"
#include "lnast.hpp"
#include "lnast_manager.hpp"
#include "lnast_ntype.hpp"
#include "lnast_writer.hpp"
#include "symbol_table.hpp"
#include "upass_utils.hpp"

namespace upass {

// Free-form, per-pass configuration threaded from `pass.upass` labels down
// through the runner to individual passes. Pass_upass collects the labels
// it cares about and stuffs them in here; each pass pulls the keys it
// recognizes in its `set_options` override. Unknown keys are silently
// ignored so new labels can be added without a whole-stack change.
using Options_map = std::map<std::string, std::string>;

// Push-based dispatch vocabulary. The runner resolves every operand of
// a value-producing node to a Bundle and PUSHES them into each pass:
//
//   Vote process_<op>(std::string_view dst_name, Bundle& dst, Src_span src)
//
// `dst` is the live symbol-table bundle for the dst name (created by the
// runner on first write); `src` is the child-ordered operand list — a `const`
// literal is a throwaway trivial Bundle (make_const) owned by the runner for
// the duration of the call, a `ref` is the name's live bundle. Names ride
// along for diagnostics only. Passes mutate their slice of `dst` and vote
// keep/drop; the runner derives the emitted node.
enum class Vote : uint8_t { keep, drop };

struct Operand {
  std::string_view              name;    // symbol-table key for refs; "" for const literals
  std::shared_ptr<const Bundle> bundle;  // never null (empty throwaway when unbound)
  // 0sb/0ub BIT-PATTERN literal: carries bits, not a value — storing a
  // signed-negative pattern into an unsigned envelope is the documented
  // force/reinterpret, never an overflow (bitwidth skips the fit range).
  bool pattern{false};
};
using Src_span = std::span<const Operand>;

enum class Emit_kind { emit, drop_subtree };

struct Emit_decision {
  Emit_kind kind{Emit_kind::emit};

  static constexpr Emit_decision emit_node() { return Emit_decision{Emit_kind::emit}; }
  static constexpr Emit_decision drop() { return Emit_decision{Emit_kind::drop_subtree}; }
};

// Step K — pending_import poison marker.
//
// Presence-only Bundle attribute (Bundle::has_attr / Bundle::set_attr) used
// to mark every name that depends on an unresolved import. Origination,
// propagation, gating, and clearing rules live in:
//   - runner (origination on `import` op, gating skip for non-constprop)
//   - finisher (defer verifier/assert dest walk while any Bundle is marked)
// All sites read/write this name; the central definition keeps the
// string consistent.
inline constexpr std::string_view k_pending_import_attr = "pending_import";

struct uPass {
public:
  uPass(std::shared_ptr<Lnast_manager>& _lm) : lm(_lm) {}
  virtual ~uPass() = default;

  // Per-run setup hook. The runner does a single walk per invocation (no
  // fixed-point loop); passes override this to seed/reset per-run state.
  virtual void begin_iteration() {}

  // Called by the runner on every drop-candidate op-node (assign, plus, eq, …)
  // after its per-node process_* has populated any ST state. Returning `drop`
  // tells the runner to omit the node (and its subtree) from the staging tree.
  // Default: always emit.
  virtual Emit_decision classify_statement() { return Emit_decision::emit_node(); }

  // Called by the runner for every `ref <name>` operand it is about to emit
  // (RHS of an op, condition of an `if`, cassert operand). If any pass
  // returns a concrete Const, the runner writes `const <value>` in place of
  // the ref. First non-nullopt wins.

  // Perf hint to the runner. Overriding passes return true so the runner
  // can skip them in the hot try_fold_ref / any_pass_drops loops. Default
  // false matches the default no-op implementations above; passes that
  // override fold_ref / classify_statement must also override these.
  virtual bool overrides_classify_statement() const { return false; }

  // ── Declared-type query vocabulary ───────────────────────────────────────
  // Answered by the shared derivation (upass/core/decl_facts.hpp) through
  // the runner's try_* helpers — facts flow through the table, nothing pulls
  // from a pass.

  // Declared scalar (integer) type of a variable, as its authoritative
  // prim_type_int `(max,min)` range. The comb-call inliner propagates an
  // *untyped* parameter's type from the actual argument at the call site:
  // `comb reverse(x) { … x.[bits] … }` invoked as `reverse(x0:u6)` must see
  // `x:u6` inside the inlined body, because `.[bits]`/`.[max]`/`.[min]` are
  // declared-type-driven (not value-driven) — an untyped param bound only to
  // a value reads nil for all three. Either bound may be unset (unbounded).
  // See bitreverse.prp.
  struct Decl_scalar_type {
    std::optional<Const> range_max;
    std::optional<Const> range_min;
  };

  // Declared type (scalar kind + optional integer range) of a DOTTED field
  // path such as `t1.a`. The inliner's typed-self `does`-check uses this to
  // compare the declared self type's fields against the receiver's.
  struct Field_decl_type {
    Io_kind              kind{Io_kind::none};
    std::optional<Const> range_max;
    std::optional<Const> range_min;
  };

  // Declared storage class of a variable. The inliner uses this to reject a
  // `const` or `type` binding as a `ref` actual (incl. the UFCS receiver of
  // a `ref self` method): a ref param writes back, so the actual must be a
  // mut value. `unknown` = no declaration seen (temps, expressions).
  enum class Decl_storage : uint8_t { unknown, mut_storage, const_storage, reg_storage, await_storage, type_storage };

  // The runner-owned scope-aware symbol table: ONE shared_ptr<Bundle>
  // per live name, shared by every pass. The runner owns every scope
  // push/pop (function_scope at run() start, block_scope per `stmts`,
  // mark-uncertain for if-arms); passes only read/write bundles through it.
  // Wired right after construction, like the emit callback below.
  void set_runner_symbol_table(Symbol_table* st) { runner_st = st; }

  // Runner-supplied helper that delegates to `try_fold_ref` across every
  // registered pass. Passes can use this to resolve a ref against the
  // aggregate symbol-table state (e.g. verifier consulting constprop's ST
  // to decide whether a cassert operand is comptime-known).
  //
  // The runner wires this up after every pass is constructed (so the
  // callback sees the full pass list). Before that point it is empty —
  // defensive callers should check before invoking.
  using Fold_fn = std::function<std::optional<Const>(std::string_view)>;

  // Runner-supplied helper that re-emits the op-node at `src` (with operand
  // folding through try_fold_ref) at the runner's current staging cursor.
  // Used by the coalescer to flush a deferred (parked) statement: the source
  // LNAST nid is captured at park time and replayed here when a downstream
  // read or scope exit triggers the flush. The runner saves/restores its
  // own read cursor around the replay so passes can call this from inside
  // their process_* without disturbing the in-progress traversal.
  using Emit_at_fn = std::function<void(const Lnast_nid&)>;
  void set_runner_emit_at_fn(Emit_at_fn fn) { runner_emit_at_fn = std::move(fn); }

  // Task 1g — runner-supplied combined scalar-type query for a variable name,
  // aggregating the two shared-ST seams so a single pass (constprop's
  // type-aware `does`/`equals` fold) sees both halves without reaching across
  // passes itself: KIND from uPass_typecheck (`provide_scalar_kind` — infers
  // bool vs int even for un-annotated vars) and the declared integer ENVELOPE
  // from uPass_attributes (`provide_field_type` — `range_max`/`range_min`,
  // present only for explicitly-typed vars). `annotated` is true when an
  // explicit `:type` pinned a finite bound. An un-annotated integer var thus
  // reads kind=integer with annotated=false → the fold treats it as an
  // unbounded envelope (superset of any int), matching the 1g ruling.
  struct Scalar_type_query {
    Io_kind              kind{Io_kind::none};
    std::optional<Const> range_max;
    std::optional<Const> range_min;
    bool                 annotated{false};
  };
  using Type_query_fn = std::function<Scalar_type_query(std::string_view)>;

  // Consume per-pass options (see Options_map). Default: no-op. Passes
  // override to pull the keys they care about. Called once, before run().
  virtual void set_options(const Options_map& /*opts*/) {}

  // Called once after the single walk completes. Passes use it to emit
  // summaries or assert invariants that only make sense once the full
  // traversal is done (e.g. verifier comparing its cassert tallies against
  // expected counts).
  virtual void end_run() {}

  // 1i — flush any deferred/parked emissions whose source nid is relative to
  // the *currently active* read tree. The runner calls this immediately
  // before every inline source-swap (push_source / pop_source) so a pass
  // (e.g. the coalescer, which replays parked writes via emit_at_fn) never
  // tries to re-read a nid against a tree that is no longer the active
  // source. Default no-op for passes that don't defer.
  virtual void flush_deferred() {}

  // Step J — dest-walk finisher hook. Invoked by the runner after the
  // source walk finishes but before end_run(), with a pointer to the
  // freshly-built staging (dest) LNAST. Passes that want to act on the
  // already-emitted output (verifier/assert cassert counts, etc.) read
  // it here. Default no-op so passes that still use per-node dispatch
  // are unaffected.
  virtual void walk_dest(const std::shared_ptr<Lnast>& /*dest*/) {}

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

  // Init-construction window. The runner synthesizes the `init(ref x, …)`
  // constructor call (plus the type-defaults bind) for `mut x:T = v` /
  // `x = T(v)` / `mut x:T = some_mod_init`. While the window is open the
  // attributes pass must not count the synthesized stores toward the const
  // single-bind tally — construction of a `const x:T = v` legitimately
  // emits the defaults bind followed by the ref-self write-back. Default:
  // no-op.
  virtual void notify_init_construction_begin() {}
  virtual void notify_init_construction_end() {}

#define PROCESS_NODE(NAME) \
  virtual void process_##NAME() {}

  // Assignment
  PROCESS_NODE(assign)
  // Task 1t — declare(var, type, mode[, value]): replaces the
  // attr_set(type)+attr_set(comptime)+type_spec declaration cluster. Only the
  // attributes pass overrides it (type_info + decl_kind + comptime); the value
  // (when present) stays a separate `store`.
  PROCESS_NODE(declare)
  PROCESS_NODE(delay_assign)

  // Bitwidth

  // Bitwidth Insensitive Reduce

  // Logical

  // Arithmetic

  // Shift

  // Bit Manipulation

  // Comparison
  // Nominal type identity (`p is xx`). Compares `p.[typename]` to the
  // string-name of the rhs ref.

  // Function Call
  PROCESS_NODE(func_call)
  PROCESS_NODE(func_def)
  PROCESS_NODE(io)

  // Pseudo-function markers (typed replacements for the const(name)-first
  // func_call form). does/in/has fold; the rest are emitted verbatim.
  PROCESS_NODE(func_break)
  PROCESS_NODE(func_continue)
  PROCESS_NODE(func_return)

  // Tuple Operations
  PROCESS_NODE(tuple_get)
  PROCESS_NODE(tuple_set)

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

  // ── Push-based hooks for the value-producing ops ──────────────────────
  // The runner resolves the operands (dst = the live table bundle, src =
  // one Operand per child) and dispatches these to every pass; the return
  // vote is the emit decision (any drop wins). The cursor is still ON the
  // node — a hook whose payload includes subtrees (named-field stores,
  // does-operands) may walk it; dispatch_push saves/restores around each
  // call. Default: no-op, keep.
#define PROCESS_NODE_PUSH(NAME)                                                                   \
  virtual Vote process_##NAME(std::string_view /*dst_name*/, Bundle& /*dst*/, Src_span /*src*/) { \
    return Vote::keep;                                                                            \
  }

  PROCESS_NODE_PUSH(bit_and)
  PROCESS_NODE_PUSH(bit_or)
  PROCESS_NODE_PUSH(bit_not)
  PROCESS_NODE_PUSH(bit_xor)
  PROCESS_NODE_PUSH(red_or)
  PROCESS_NODE_PUSH(red_and)
  PROCESS_NODE_PUSH(red_xor)
  PROCESS_NODE_PUSH(popcount)
  PROCESS_NODE_PUSH(log_and)
  PROCESS_NODE_PUSH(log_or)
  PROCESS_NODE_PUSH(log_not)
  PROCESS_NODE_PUSH(plus)
  PROCESS_NODE_PUSH(minus)
  PROCESS_NODE_PUSH(mult)
  PROCESS_NODE_PUSH(div)
  PROCESS_NODE_PUSH(mod)
  PROCESS_NODE_PUSH(shl)
  PROCESS_NODE_PUSH(sra)
  PROCESS_NODE_PUSH(sext)
  PROCESS_NODE_PUSH(set_mask)
  PROCESS_NODE_PUSH(get_mask)
  PROCESS_NODE_PUSH(ne)
  PROCESS_NODE_PUSH(eq)
  PROCESS_NODE_PUSH(lt)
  PROCESS_NODE_PUSH(le)
  PROCESS_NODE_PUSH(gt)
  PROCESS_NODE_PUSH(ge)
  PROCESS_NODE_PUSH(is)
  PROCESS_NODE_PUSH(func_does)
  PROCESS_NODE_PUSH(func_equals)
  PROCESS_NODE_PUSH(func_in)
  PROCESS_NODE_PUSH(func_has)
  PROCESS_NODE_PUSH(func_case)
  PROCESS_NODE_PUSH(tuple_add)
  PROCESS_NODE_PUSH(tuple_concat)

#undef PROCESS_NODE_PUSH

  // `store` is dispatched to the passes in push form for BOTH shapes:
  // a scalar store has src = {value}; a field-path store has the resolved
  // selector bundles then the value, in LNAST child order, with `dst` the
  // WHOLE destination bundle. Migration default routes to the legacy
  // arity-split hooks (process_assign for ≤2-child, process_tuple_set for
  // field paths).
  virtual Vote process_store(std::string_view /*dst_name*/, Bundle& /*dst*/, Src_span /*src*/) { return Vote::keep; }

protected:
  std::shared_ptr<Lnast_manager>& lm;
  Emit_at_fn                      runner_emit_at_fn;
  Symbol_table*                   runner_st{nullptr};  // See set_runner_symbol_table

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

  template <class... Lnast_ntype_int>
  bool is_type(Lnast_ntype_int... ty) const {
    auto n = get_raw_ntype();
    return (((n == ty) || ...) || false);
  }
};

// Pointer-to-member type for the push-form hooks. The push forms
// overload the nullary names, so dispatch sites disambiguate with a
// static_cast to this type (see the runner's PUSH_FN macro).
using Push_method = Vote (uPass::*)(std::string_view, Bundle&, Src_span);

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
