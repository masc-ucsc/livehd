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

  void               run(std::size_t max_iters = 1);
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

  absl::flat_hash_map<std::string, std::shared_ptr<Lnast>> &runner_function_registry() { return function_registry; }

  // Monotonic per-run counter giving each inline call site a unique rename
  // tag/salt. Cross-pass idempotence is a documented 1i follow-up.
  uint32_t                      inline_seq_{0};
  std::shared_ptr<hhds::Forest> scratch_forest_;
  // Callee bodies currently being spliced (innermost last). Re-entering one
  // means recursion — bailed to the evaluator until Phase D adds fuel.
  std::vector<const Lnast*> active_inline_callees_;
  // Registry keys (qualified names) whose bodies can reach themselves
  // (direct/mutual recursion). Bailed to the evaluator; computed in
  // set_function_registry.
  absl::flat_hash_set<std::string> recursive_callees_;
  // Registry keys the runner can fully splice today (single output written by
  // name, no placeholders; recursion allowed under fuel). All other callees
  // route to the evaluator. Computed in set_function_registry.
  absl::flat_hash_set<std::string> inlinable_callees_;
  // Inlinable callees whose body uses positional placeholders (_0/_1/_); the
  // prologue binds those aliases for these only.
  absl::flat_hash_set<std::string> placeholder_callees_;
  // Phase D recursion fuel. Per-callee depth is capped at kInlineMaxDepth
  // (active frames of the same callee); inline_budget_ is a per-run total
  // splice cap so a non-terminating / exponential unroll bails instead of
  // running away. Both generous — fib/fact/tree_sum stay well under.
  // Per-callee recursion depth cap — a backstop for pathological const-arg
  // recursion (e.g. f(n)=f(n+1)). Kept well below the C++ stack-overflow
  // depth since each runner inline level consumes several KB of stack. Legit
  // comptime recursion (fib/fact) is far shallower. inline_budget_ is a
  // per-run total-splice cap for exponential fan-out.
  static constexpr std::size_t kInlineMaxDepth = 256;
  std::size_t                  inline_budget_{200000};

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

  // Step G — reduce a sequence of Votes by priority drop > toconst > update
  // > keep. Used by the new-surface dispatch path once passes migrate to
  // the Vote-returning hooks. Today this is a free helper called by
  // nothing in the legacy dispatch.
  static upass::Vote reduce_votes(const std::vector<upass::Vote>& votes);

  // Verbatim path (category C): dispatch so passes see the node, then copy
  // the subtree without folding.
  void process_verbatim(Pass_method fn);
};
