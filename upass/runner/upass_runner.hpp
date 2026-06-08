//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <print>
#include <stack>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "const.hpp"
#include "lnast.hpp"
#include "lnast_manager.hpp"
#include "lnast_ntype.hpp"
#include "upass_core.hpp"

struct uPass_runner : public upass::uPass_struct {
public:
  uPass_runner(std::shared_ptr<upass::Lnast_manager>& _lm, const std::vector<std::string>& upass_names,
               upass::Options_map options = {});

  void               run();
  bool               has_configuration_error() const { return configuration_error; }
  const std::string& get_configuration_error() const { return configuration_error_msg; }

  // Mark this runner as processing a function-body LNAST spawned by
  // func_extract. The dead-code-elimination pass uses this to skip
  // function bodies (their outputs are consumed by external callers
  // and would look unreferenced from inside-the-body alone). pass_upass
  // sets this flag for every LNAST past the original entry-point count.
  void set_is_function_body(bool v) { is_function_body_ = v; }

  // 1i — populate the comb-body registry the runner inlines from. Keyed by
  // the body's top-module name (e.g. "pp3.count_bits"). pass_upass passes
  // the same var.lnasts it hands constprop's set_function_registry; the
  // runner picks the function bodies (everything past the entry points) and
  // the top modules alike — only names that actually appear as a func_call
  // callee are ever inlined.
  void set_function_registry(const std::vector<std::shared_ptr<Lnast>>& lnasts);

  // Hands the freshly-built staging LNAST to the caller; after this the
  // runner no longer owns it. Call once per run(), after run() returns.
  std::shared_ptr<Lnast>              take_staging() { return std::move(staging); }
  std::vector<std::shared_ptr<Lnast>> take_new_lnasts() override { return std::move(new_lnasts); }

  // toln:0 && tolg:0 (diagnostics-only runs, e.g. the LSP): skip building the
  // staging tree entirely. The walk still dispatches every pass (all
  // diagnostics, symbol-table state, io/bw side-channels are unchanged) but
  // the emit_* family becomes a no-op and the post-walk DCE is skipped —
  // nothing downstream consumes the rewritten LNAST. Default on. Must stay on
  // for the func_extract pre-loop and whenever take_staging() is consumed.
  void set_materialize(bool m) { materialize_ = m; }

protected:
  struct Pass_entry {
    std::string                   name;
    std::shared_ptr<upass::uPass> pass;
  };

  std::vector<Pass_entry> upasses;

  // Perf: pre-cached subsets of `upasses`.
  //   fold_capable_passes — passes that override fold_ref(). try_fold_ref
  //     iterates only these instead of every pass.
  //   classify_capable_passes — passes that override classify_statement().
  //     any_pass_drops iterates only these instead of every pass.
  // Populated in the constructor right after `upasses` is fully built. With
  // 6 passes total and only 2–4 overriders, this cuts the per-node virtual
  // dispatch count by 30–50% on the bulk arithmetic hot loop.
  std::vector<upass::uPass*> fold_capable_passes;
  std::vector<upass::uPass*> classify_capable_passes;
  // 1i Phase E — passes that expose shared-ST reads (provide_bundle_fields /
  // provide_typename), consulted by try_bundle_fields / try_typename.
  std::vector<upass::uPass*> shared_st_passes_;

  // Step C of upass redesign — single shared symbol table owned by the
  // runner. Each pass receives a pointer to this via
  // set_runner_symbol_table(). Initially used as a wiring slot only;
  // existing passes keep their private state until they migrate.
  Symbol_table runner_symbol_table;

  // Step I of upass redesign — runner-owned SSA-version counters keyed
  // by emitted name. Bumped on every mut reassignment when the runner
  // takes over SSA at emit time (today still owned by uPass_ssa).
  // Reserved here so the emit path can populate it without an API change.
  absl::flat_hash_map<std::string, int> ssa_counters;

  // Step L of upass redesign — function-body registry the runner owns
  // once func_extract collapses into the main walk. Maps function name
  // to its already-extracted dest Lnast (allocated in dest_forest_)
  // so an fcall can switch the cursor into the callee instead of
  // running the dedicated func_extract pre-pass. Empty today; the
  // dedicated uPass_func_extract pass still populates Eprp_var::lnasts.
  absl::flat_hash_map<std::string, std::shared_ptr<Lnast>> function_registry;

  bool        configuration_error{false};
  std::string configuration_error_msg;
  bool        is_function_body_{false};

  // Step H — runner-owned dest forest. Allocated lazily on first run();
  // the staging tree below is created inside this forest. Conceptually
  // the lgdb/optimized forest in the plan; not yet backed by an on-disk
  // path.
  std::shared_ptr<hhds::Forest> dest_forest_;

  // Staging tree being built during traversal. See upass.md §2.1.
  std::shared_ptr<Lnast>              staging;
  std::stack<Lnast_nid>               staging_parent_stack;
  Lnast_nid                           staging_parent;
  std::vector<std::shared_ptr<Lnast>> new_lnasts;
  bool                                materialize_{true};  // see set_materialize()

  std::vector<std::string> resolve_order(const std::vector<std::string>& requested_names, std::string* error_msg = nullptr) const;

  // Staging emit helpers.
  void emit_push(Lnast_ntype::Lnast_ntype_int type);
  void emit_pop();
  void emit_leaf(Lnast_ntype::Lnast_ntype_int type);
  void emit_leaf(const Lnast_node& node);
  void emit_subtree_verbatim();

  // Returns the first non-nullopt result from any pass's fold_ref(name).
  std::optional<Const> try_fold_ref(std::string_view name);

  // 1i Phase E shared-ST reads: the comb-call inliner uses these to introspect
  // state it can't see through scalar fold_ref. try_bundle_fields returns the
  // flat comptime-const fields behind a bundle ref (for tuple actuals);
  // try_typename returns a var's declared typename (for method dispatch /
  // setter-init). First pass that provides wins.
  std::optional<std::vector<std::pair<std::string, Const>>> try_bundle_fields(std::string_view name);
  std::string                                               try_typename(std::string_view name);
  // Declared integer (max,min) range of a variable (for re-typing an untyped
  // inlined param from the actual at the call site). First pass that provides
  // wins. See uPass::provide_decl_type.
  std::optional<upass::uPass::Decl_scalar_type>             try_decl_type(std::string_view name);
  // Folded (start, end_inclusive) bounds of a `range` tmp (comptime for-loop
  // iterable). First pass that provides wins. See uPass::provide_range.
  std::optional<std::pair<Const, Const>>                    try_range(std::string_view name);
  // Declared kind + range of a dotted field path (`t1.a`). First pass that
  // provides wins. See uPass::provide_field_type (task 1k).
  std::optional<upass::uPass::Field_decl_type>              try_field_type(std::string_view name);
  // Task 1g — inferred scalar kind of a variable (bool vs int even when
  // un-annotated). First pass returning a non-none kind wins (typecheck).
  // See uPass::provide_scalar_kind.
  Io_kind                                                   try_scalar_kind(std::string_view name);
  // Declared storage class (mut/const/reg/type) of a variable. First pass
  // that provides a non-unknown answer wins. See uPass::provide_decl_storage
  // (task 1k ref-actual mutability).
  upass::uPass::Decl_storage                                try_decl_storage(std::string_view name);

  // Task 1k — typed-self `does`-check: the receiver bound to `self:T` must
  // structurally satisfy T (per declared field: same-name receiver field
  // present, scalar kinds match, integer range receiver ⊆ declared). Extra
  // receiver fields are fine. Emits a fatal `fcall-self-does` diagnostic on
  // the first failing field.
  void check_self_does(const livehd::diag::Span& span, std::string_view callee_name, std::string_view decl_tn,
                       const Lnast_node& receiver);

  // Emits either the folded value of `name` (when any pass returns a valid
  // Const) or the original ref node otherwise. Used by both emit_op_with_fold
  // and the statement-scope ref leaf case.
  void emit_ref_or_folded(std::string_view name);

  // Emits the current op-node and its children into staging. When fold_all is
  // false, the first child (LHS/dst) is copied verbatim and subsequent ref
  // children are fed through fold_ref. When true, every ref child is folded.
  void emit_op_with_fold(bool fold_all);

  // Replays emit_op_with_fold for the op-node at `src` at the current staging
  // position. Saves/restores the read cursor so passes can call this from
  // inside process_* without disturbing the in-progress traversal. Exposed to
  // passes via the Emit_at_fn callback wired in the constructor.
  void emit_op_with_fold_at(const Lnast_nid& src);

  void process_top() override;
  void process_stmts() override;
  void process_if() override;
  void process_lnast();

  // ── Comptime loop unroll (range `for` + `while`/`loop`) ─────────────────────
  // Pyrope loops are comptime-only; like recursion, the runner evaluates them
  // by re-walking the body until the bound/condition is exhausted, bounded by
  // the recursion fuel. unroll_for handles `for i in lo..hi` (the iterable is a
  // `range` tmp folded via try_range); tuple-iteration for-loops are still
  // unrolled by prp2lnast and never reach here. unroll_while handles
  // `while cond`/`loop` (cond folded each iteration; non-comptime → verbatim so
  // typecheck still flags a non-bool condition). Each iteration re-walks the
  // body under a fresh salt + block scope (push_iteration) so tmps/locals don't
  // collide. Cursor is left on the loop node on every path.
  void unroll_for();
  void unroll_while();
  // Re-walk ONE loop iteration. Precondition: the read cursor is on the loop's
  // body `stmts` node. Opens a fresh iteration scope (fresh salt + staging/pass
  // block scope), invokes `emit_binds` to bind the iteration variable(s) into
  // it (a Const for a range, a `tuple_get` pick for a tuple), re-walks the
  // body's statements, then closes the scope and restores the cursor to the
  // body `stmts` node. Returns false if the fuel/depth guard tripped (caller
  // stops iterating).
  bool walk_loop_iteration(const std::function<void()>& emit_binds);
  // Emit a per-iteration tuple pick `dst = src[index_text]` as a scratch
  // tuple_get run through the walk (so try_resolve_tuple_get / constprop
  // resolve it). index_text is the pyrope field literal ("0","1",… or "'name'").
  void emit_inline_tuple_pick(const std::string& dst, const std::string& src, const std::string& index_text);

  // ── 1i comb-call inliner ────────────────────────────────────────────────
  // Called from process_lnast's func_call case. If the callee resolves to a
  // comb body in function_registry and the call shape is supported, performs
  // a virtual splice — emit prologue param-binding assigns, push_source into
  // the callee body and walk it (so every pass folds/observes and the
  // renamed body is emitted into staging), then emit the epilogue (output +
  // ref-param writeback) — and returns true. Returns false to fall back to
  // the normal func_call emit/dispatch path (typecasts, cell-ops, markers,
  // or call shapes 1i does not yet handle). The read cursor is left on the
  // func_call node on every path. See 1i in TODO_livehd.md.
  bool try_inline_func_call();

  // Task 1p — var-arg access resolution. A `comb foo(...args)` call gathers its
  // leftover actuals here, keyed by the FRAME-TAGGED var-arg name (e.g.
  // "inl1_args"): positional leftovers under decimal keys "0","1",…, named
  // leftovers under their names. The body's `args[i]` / `args.NAME` reads are a
  // COMPTIME-structural pick (the index/identity is known at the call site even
  // when the value is a runtime signal), so try_resolve_vararg_get rewrites
  // each such `tuple_get` into a direct `dst = <actual>` copy during the body
  // walk — avoiding a runtime tuple_add/tuple_get that tolg cannot lower.
  // Entries are registered in the prologue and erased after the body walk.
  absl::flat_hash_map<std::string, std::vector<std::pair<std::string, Lnast_node>>> vararg_bindings_;
  // Called from process_lnast's tuple_get case. Returns true (and emits a copy
  // `dst = <picked ref>`) iff the cursor's tuple_get is a single-segment pick
  // with a comptime-known index/name resolving to a known runtime ref — from a
  // gathered var-arg (vararg_bindings_) OR constprop's slot→ref map
  // (try_tuple_slot_ref). False leaves the node to the normal fold/emit path
  // (nested access, dynamic index, comptime slot, or unknown ref).
  bool try_resolve_tuple_get();
  // Source ref held at `slot` of tuple `name` (runtime-scalar slot). First pass
  // that provides wins. See uPass::provide_tuple_slot_ref.
  std::optional<std::string> try_tuple_slot_ref(std::string_view name, std::string_view slot);
  // Ordered (slot-key, is_positional) shape of tuple `name` for the runner's
  // tuple-for unroll. First pass that provides wins. See provide_tuple_shape.
  std::optional<std::vector<std::pair<std::string, bool>>> try_tuple_shape(std::string_view name);

  // ── Task 1p — pipe/mod/fluid template specialization ────────────────────
  // Called from try_inline_func_call at the pipe/mod decline point when the
  // resolved callee is a TEMPLATE (untyped boundary). Reads each untyped fixed
  // port's concrete type from the actual's DECLARED type (decision 4: an
  // untyped actual into such a port is a fatal call-site error), mints (or
  // reuses) a concrete specialized module named `<callee>__<sig>`, appends it
  // to new_lnasts (pass_upass re-SSAs + lowers it), and emits the call to the
  // mangled name so tolg instantiates the Sub. Returns true (call emitted);
  // a method (`ref self`) or var-arg boundary is left to the caller.
  struct Spec_port {
    bool                 inject = false;  // false = keep the template's own (already-typed) port
    std::optional<Const> max;
    std::optional<Const> min;
    std::string          type_name;  // named type (takes precedence over max/min)
  };
  bool maybe_specialize_template_call(const std::shared_ptr<Lnast>& callee, const Lnast_tree_io& io,
                                      const std::vector<Lnast_node>& param_val, const std::vector<bool>& param_set,
                                      std::size_t nbind, bool has_vararg, const std::string& dst_name,
                                      const std::string& callee_name, const livehd::diag::Span& call_span);
  // Deep-copy `tmpl` verbatim into a fresh (TreeIO-backed) Lnast named
  // `mangled`, then inject a concrete prim_type_int / named-type child into
  // each untyped fixed input port per `inject`. Clears the template flag and
  // copies the lambda kind. pass_upass re-SSAs the result.
  std::shared_ptr<Lnast> clone_template_specialized(const std::shared_ptr<Lnast>& tmpl, const std::string& mangled,
                                                    const std::vector<Spec_port>& inject);
  void copy_subtree_into(const std::shared_ptr<Lnast>& src, const Lnast_nid& src_nid, const std::shared_ptr<Lnast>& dst,
                         const Lnast_nid& dst_parent);
  void emit_specialized_call(const std::string& dst, const std::string& mangled, const std::vector<Lnast_node>& actuals);
  // Specialized-module names this runner already minted this run (avoid
  // re-cloning the same signature within one tree; cross-tree dedup is by name
  // in pass_upass's queue drain).
  absl::flat_hash_set<std::string> specialized_emitted_;

  // ── init constructor hook ───────────────────────────────────────────────
  // One named argument of a synthesized constructor call (positional when
  // `key` is empty).
  struct Ctor_arg {
    std::string key;
    Lnast_node  node{Lnast_node::create_invalid()};
  };
  // Called from process_lnast's 2-child `store` case BEFORE the normal
  // assign dispatch. Recognizes the construction forms on `store(x, V)`:
  //   * x has a declared typename T whose bundle carries an `init` field
  //     (one function-name string, or an `init.N` overload list) — the
  //     typed-decl (`mut x:T = value`) and nil-decl (`mut x:T = nil`) forms.
  //   * V is itself a registry function whose first input is `ref self` —
  //     the mod-init form (`mut y:Mix_tup = mix_tup_init`).
  // On a match: binds x to T's defaults, splices `init(ref x, args…)`
  // (args exploded from V), consumes the store, and returns true. Returns
  // false (caller proceeds structurally) when there is no init, the value
  // isn't comptime-resolvable, or no overload matches the argument count.
  bool try_init_construction();
  // Same recognition for the explicit call form `x = T(args…)`: called from
  // the func_call case when try_inline_func_call declined. The callee name
  // is not a registry function but a type bundle with `init`.
  bool try_construct_call();
  // Shared splice: emit `receiver = <defaults of tn>` (when tn non-empty),
  // then synthesize and walk `init_fn(__ufcs_arg=receiver, args…)` inside an
  // init-construction window (attributes tally paused, ref-self-on-const
  // admitted, construction args bind positionally in tuple order).
  void splice_init_call(const std::string& receiver, const std::string& tn, const std::string& init_fn,
                        const std::vector<Ctor_arg>& args);
  // Selects the first init overload (tuple order) whose non-self formals
  // match the args (by count; named keys must all exist). Empty when none.
  std::string select_init_overload(const std::vector<std::string>& candidates, const std::vector<Ctor_arg>& args);
  // Collects the init candidates recorded on type bundle `tn`: the single
  // `init` function-name string or the `init.N` overload list, tuple order.
  std::vector<std::string> init_candidates_of(std::string_view tn);
  // >0 while a synthesized constructor call is being spliced.
  int init_construction_depth_ = 0;
  // Vars whose `declare` has been walked but whose declaration store hasn't
  // arrived yet — the only store where construction may run.
  absl::flat_hash_set<std::string> pending_ctor_store_;
  // Receivers currently mid-construction: their synthesized defaults-bind /
  // ref-self write-back stores must not re-enter try_init_construction
  // (nested constructions of OTHER vars inside an init body stay allowed).
  absl::flat_hash_set<std::string> constructing_vars_;
  // One-shot: armed right before walking the synthesized ctor fcall so the
  // FIRST try_inline entry (the ctor itself, not calls nested in its body)
  // relaxes the arg-naming rules (construction args bind in tuple order) and
  // admits a const receiver on `ref self` (the constructor is the one legal
  // writer of a const).
  bool ctor_call_pending_ = false;

  // Resolves a (possibly unqualified) callee name to a registry body. Tries
  // the bare name, then the unique "<module>.<name>" suffix match.
  std::shared_ptr<Lnast> lookup_callee(std::string_view name) const;

  // Dispatches uPass::flush_deferred to every pass — used by the inliner to
  // drain deferred/parked emits (coalescer) before each source-swap so their
  // src nids stay valid against the active read tree.
  void flush_deferred_emits();

  // Emits one binding statement `lhs = rhs` by building a one-assign scratch
  // tree and running it through the normal walk (so constprop records the
  // value and the assign is emitted/folded into the caller's staging).
  void emit_inline_binding(const std::string& lhs, const Lnast_node& rhs);

  // Emits `dst = (k0=v0, …)` as a tuple_add through the walk so constprop
  // builds dst's bundle — used to splat a multi-output callee's returns so
  // the caller's destructuring tuple_gets fold.
  void emit_inline_tuple(const std::string& dst, const std::vector<std::pair<std::string, Lnast_node>>& fields);

  // Emits `lhs : uN/sN` (a type_spec) the same way, so the attributes pass
  // records the inlined param/output's declared width — that's what lets
  // `<tag>param.[bits]` fold during the body walk (the callee's io widths
  // are otherwise lost once the signature is gone). No-op when bits<=0.
  void emit_inline_typespec(const std::string& name, int bits, bool is_signed);

  // Same, but from an explicit prim_type_int `(max,min)` range — used to
  // re-type an untyped param from the actual's declared range (preserves
  // non-pow2 / partial ranges exactly, where bits alone would round up the
  // envelope). An unset bound emits the `nil` (unbounded) child. No-op when
  // both bounds are unset.
  void emit_inline_typespec_range(const std::string& name, const std::optional<Const>& range_max,
                                  const std::optional<Const>& range_min);

  // Emits `dst = sext(src, sign_bit)` through the walk so constprop folds it.
  // Mirrors the deleted evaluator's adjust_for_type: a signed output's raw
  // value (e.g. a get_mask bit-slice 0b1110 = 14) must be reinterpreted as its
  // declared width (s4 → -2). sign_bit is the top bit index (bits-1).
  void emit_inline_sext(const std::string& dst, const std::string& src, int sign_bit);

  absl::flat_hash_map<std::string, std::shared_ptr<Lnast>>& runner_function_registry() { return function_registry; }

  // Monotonic per-run counter giving each inline call site a unique rename
  // tag/salt. Cross-pass idempotence is a documented 1i follow-up. Also used
  // per comptime loop iteration (unroll_for/unroll_while) so each iteration's
  // re-walk gets a fresh tmp-rename namespace + block-scope id.
  uint32_t                                      inline_seq_{0};
  // Comptime loop unroll state. loop_break_hit_ is set by a `func_break`
  // reached on a comptime-taken path during a loop body re-walk; the unroller
  // checks it after each iteration and stops. loop_depth_ counts active
  // (nested) unrolls so the fuel/depth guard bounds non-terminating loops the
  // same way recursion is bounded.
  bool                                          loop_break_hit_{false};
  int                                           loop_depth_{0};
  std::shared_ptr<hhds::Forest>                 scratch_forest_;
  // Callee bodies currently being spliced (innermost last). Re-entering one
  // means recursion — bailed to the evaluator until Phase D adds fuel.
  std::vector<const Lnast*>                     active_inline_callees_;
  // Registry keys (qualified names) whose bodies can reach themselves
  // (direct/mutual recursion). Bailed to the evaluator; computed in
  // set_function_registry.
  absl::flat_hash_set<std::string>              recursive_callees_;
  // Registry keys the runner can fully splice today (single output written by
  // name, no placeholders; recursion allowed under fuel). All other callees
  // route to the evaluator. Computed in set_function_registry.
  absl::flat_hash_set<std::string>              inlinable_callees_;
  // Inlinable callees whose body uses positional placeholders (_0/_1/_); the
  // prologue binds those aliases for these only.
  absl::flat_hash_set<std::string>              placeholder_callees_;
  // Higher-order / closure support: maps a function-valued param's RAW name
  // (as read in the callee body, e.g. `f` in `r = f(x)`) to the registry
  // function it is bound to at this call site (e.g. `step_up`). Saved/restored
  // around each body walk so nested frames don't clobber each other. Consulted
  // by try_inline_func_call when a callee name isn't itself a registry entry.
  absl::flat_hash_map<std::string, std::string> func_param_bindings_;
  // Phase D recursion fuel. Per-callee depth is capped at kInlineMaxDepth
  // (active frames of the same callee); inline_budget_ is a per-run total
  // splice cap so a non-terminating / exponential unroll bails instead of
  // running away. Both generous — fib/fact/tree_sum stay well under.
  // Per-callee recursion depth cap — a backstop for pathological const-arg
  // recursion (e.g. f(n)=f(n+1)). Kept well below the C++ stack-overflow
  // depth since each runner inline level consumes several KB of stack. Legit
  // comptime recursion (fib/fact) is far shallower. inline_budget_ is a
  // per-run total-splice cap for exponential fan-out.
  static constexpr std::size_t                  kInlineMaxDepth = 256;
  std::size_t                                   inline_budget_{200000};

  // Post-walk DCE: scans the freshly-built staging tree, drops definition
  // statements (assign / tuple_add / attr_set / etc.) whose dst name is
  // never read elsewhere, and iterates to fixpoint. Constprop's
  // classify_statement is conservative about multi-entry bundles (a tuple
  // dst can't safely be inlined via fold_ref since fold_ref returns only
  // the position-0 trivial), so it emits tuple_add+assign+attr_set chains
  // for fully-constant tuples even when no downstream consumer survives.
  // DCE cleans up those orphan chains.
  void dead_code_eliminate_staging();

private:
  using Pass_method = void (upass::uPass::*)();

  // Dispatches `fn` across every registered pass so they can update their
  // internal state (symbol tables, etc.) from the current read cursor. The
  // cursor must be at an op-node and each pass is expected to restore it.
  void dispatch_to_passes(Pass_method fn);

  // Drop-candidate path (category A / B in upass.md §3 Slice 1): dispatch,
  // classify via every pass's classify_statement, emit if no pass drops.
  void process_drop_candidate(Pass_method fn, bool fold_all);
  void process_drop_candidate_verbatim(Pass_method fn);
  bool any_pass_drops() const;

  // Verbatim path (category C): dispatch so passes see the node, then copy
  // the subtree without folding.
  void process_verbatim(Pass_method fn);
};
