//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <print>
#include <stack>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "decl_facts.hpp"
#include "hlop/dlop.hpp"
#include "lnast.hpp"
#include "lnast_manager.hpp"
#include "lnast_ntype.hpp"
#include "upass_core.hpp"

// ── Shared comb-call-inliner registry ───────────────────────────────────────
// Built ONCE per Pass_upass::work and shared (by const pointer) with every
// per-unit uPass_runner instance, instead of being recomputed from scratch
// inside each runner. The old per-runner rebuild walked EVERY lnast's whole
// tree on EVERY unit, making the upass walk O(units^2 * tree) — minutes-long
// "hangs" on large designs (XSCore). ensure() is incremental: it walks each
// lnast body exactly once across the whole pass (caching the per-body facts),
// even as runner-spawned template specializations grow var.lnasts. The derived
// global sets (recursion closure + sub-convertible filter) are tree-walk-free
// and recomputed cheaply whenever the lnast count grows.
struct uPass_function_registry {
  // Module/function name -> its already-extracted body lnast.
  absl::flat_hash_map<std::string, std::shared_ptr<Lnast>> function_registry;
  // Keys whose bodies can reach themselves (direct/mutual recursion).
  absl::flat_hash_set<std::string>                         recursive_callees;
  // Keys the runner can fully splice (single output written by name, etc.).
  absl::flat_hash_set<std::string>                         inlinable_callees;
  // Inlinable pure-dataflow combs convertible to a Sub module instance.
  absl::flat_hash_set<std::string>                         sub_convertible_combs;

  // Per-lnast facts, all PURELY LOCAL to one body (so they can be cached and a
  // body never re-walked). The recursion-dependent part is recomputed globally.
  struct Lnast_facts {
    std::vector<std::string> callee_names;           // raw func_call callee refs
    bool                     inlinable     = false;  // all declared outputs written
    bool                     sub_candidate = false;  // sub-convertible modulo recursion
  };
  absl::flat_hash_map<std::string, Lnast_facts> facts;            // keyed by registry name
  std::size_t                                   built_count = 0;  // #lnasts already folded in

  // Idempotent. Folds any lnasts past built_count into the registry (one tree
  // walk each), then recomputes the global recursion closure + derived sets.
  // No-op when lnasts.size() == built_count.
  void ensure(const std::vector<std::shared_ptr<Lnast>>& lnasts);

  std::shared_ptr<Lnast> lookup_callee(std::string_view name) const;
};

// Tuple layouts learned by the streaming front end while it walks file-level
// type declarations.  Extracted function bodies are separate LNASTs/runners,
// but they follow their owning file-level unit in Pass_upass's queue; sharing
// this small semantic registry preserves the source symbol-table visibility
// without rescanning or copying any tree.
struct uPass_detuple_registry {
  struct Scalar_type {
    upass::Kind kind{upass::Kind::unknown};
    Dlop        max;
    Dlop        min;

    [[nodiscard]] bool valid() const { return kind == upass::Kind::integer || kind == upass::Kind::boolean; }
  };
  struct Field {
    std::string name;
    Scalar_type type;
  };
  using Layout = std::vector<Field>;

  absl::flat_hash_map<std::string, Layout> named_types;
};

struct uPass_runner : public upass::uPass_struct {
public:
  uPass_runner(std::shared_ptr<upass::Lnast_manager>& _lm, const std::vector<std::string>& upass_names,
               upass::Options_map options = {});

  void                   run();
  // Elaborate a hardware entry point without call-site actuals. The caller
  // keeps the template in the call registry for explicit instantiations.
  std::shared_ptr<Lnast> specialize_top_defaults();
  bool                   has_configuration_error() const { return configuration_error; }
  const std::string&     get_configuration_error() const { return configuration_error_msg; }

  // Mark this runner as processing a function-body LNAST spawned by
  // func_extract. The dead-code-elimination pass uses this to skip
  // function bodies (their outputs are consumed by external callers
  // and would look unreferenced from inside-the-body alone). pass_upass
  // sets this flag for every LNAST past the original entry-point count.
  void set_is_function_body(bool v) { is_function_body_ = v; }

  // Point this runner at the shared comb-body registry (built once by
  // pass_upass and reused by every per-unit runner; see uPass_function_registry
  // above). Only names that actually appear as a func_call callee are inlined.
  // The pointer must outlive the runner; nullptr means "no inlining" (the
  // bitwidth-only runner never calls this — reg() then returns an empty set).
  void set_function_registry(const uPass_function_registry& reg) { registry_ = &reg; }

  // Shared semantic type layouts populated by file-level runners and consumed
  // by the subsequently extracted function runners. The registry contains no
  // tree nodes and therefore needs no source-tree lifetime management.
  void set_detuple_registry(uPass_detuple_registry& reg) { detuple_registry_ = &reg; }

  // Hands the freshly-built staging LNAST to the caller; after this the
  // runner no longer owns it. Call once per run(), after run() returns.
  std::shared_ptr<Lnast>              take_staging() { return std::move(staging); }
  std::vector<std::shared_ptr<Lnast>> take_new_lnasts() override { return std::move(new_lnasts); }

  // What the walk left on one of this unit's output ports / default-value
  // locals as its body ended (snapshot_unit_body): the value kind typecheck
  // stamped on its last write, and the compile-time value when it folded.
  // pass.upass reads them after run() to export a mod's untyped outputs
  // (ruling 28) and its folded input defaults (ruling 34).
  upass::Kind value_kind(std::string_view name) const {
    const auto it = unit_body_values_.find(name);
    return it == unit_body_values_.end() ? upass::Kind::unknown : it->second.kind;
  }
  std::optional<Dlop> comptime_value(std::string_view name) const {
    const auto it = unit_body_values_.find(name);
    return it == unit_body_values_.end() ? std::nullopt : it->second.value;
  }

  // toln:0 && tolg:0 (diagnostics-only runs, e.g. the LSP): skip building the
  // staging tree entirely. The walk still dispatches every pass (all
  // diagnostics, symbol-table state, io/bw side-channels are unchanged) but
  // the emit_* family becomes a no-op and the post-walk DCE is skipped —
  // nothing downstream consumes the rewritten LNAST. Default on. Must stay on
  // for the func_extract pre-loop and whenever take_staging() is consumed.
  void set_materialize(bool m) { materialize_ = m; }

  // Named-constant provenance (the Pyrope counterpart of inou.slang's
  // `preserve_param_provenance`): materialize a folded `pkg.PARAM` read as the
  // SYMBOLIC ref `pkg.PARAM` instead of its value, so `--emit-dir pyrope:`
  // re-emits `cmd == vpu_defs_pkg.VPU_TRANS_SIN_P2` rather than `cmd == 0x78`.
  // Comptime evaluation is unaffected (it reads the symbol table, never the
  // materialized tree), but lnast.tolg cannot wire a symbolic ref — so the
  // kernel only turns this on for a pyrope-emitting, no-graphs compile.
  void set_preserve_param_provenance(bool v) {
    preserve_param_provenance_ = v;
    if (v && root_lnast_->is_verilog_origin()) {
      for (const auto& name : root_lnast_->get_generics()) {
        preserved_param_names_.insert(name);
      }
    }
  }

protected:
  struct Pass_entry {
    std::string                   name;
    std::shared_ptr<upass::uPass> pass;
    // Per-pass dispatch wall-clock + call count, accumulated only when the
    // LIVEHD_UPASS_STATS env var is set (dispatch_to_passes / dispatch_push);
    // run() prints the per-unit breakdown to stderr at walk end.
    uint64_t                      stat_ns{0};
    uint64_t                      stat_calls{0};
  };

  std::vector<Pass_entry> upasses;

  // LIVEHD_UPASS_STATS: per-pass dispatch timing (see Pass_entry). Checked
  // once in the constructor; the disabled path adds one branch per dispatch.
  bool dispatch_stats_{false};

  // dce:mark (pass.upass option, set by the kernel for lg-only flows): the
  // post-walk DCE records dead statement nids on the staging Lnast for tolg
  // to skip instead of rebuilding the tree — the LNAST is dropped after
  // lowering, so a clean copy is wasted work there.
  bool dce_mark_only_{false};

  // THE symbol table: one runner-owned, scope-aware table holding one
  // shared_ptr<Bundle> per live name, shared by every pass (wired via
  // uPass::set_runner_symbol_table). The runner owns all scope transitions:
  // function_scope at run() start, block_scope/leave_scope around every
  // `stmts` (process_stmts / walk_loop_iteration), and the uncertain-if-arm
  // marking (next_block_uncertain_, set where notify_uncertain_arm_begin is
  // dispatched). Fresh per runner == fresh per tree (pass_upass constructs a
  // runner per tree).
  Symbol_table symbol_table_;
  bool         next_block_uncertain_{false};

  // Cache of parsed const-operand literals (resolve_node_operands). Dlop::from_pyrope
  // is a pure function of the literal text but is shln-heavy for wide values, and
  // the SAME literals ("0", "1", wide masks, …) are re-parsed on every dispatched
  // node. Key = literal text → its parsed value/kind/pattern. Cleared per run().
  struct Parsed_const {
    Dlop        value;
    upass::Kind kind{upass::Kind::unknown};
    bool        pattern{false};
  };
  absl::flat_hash_map<std::string, Parsed_const> const_parse_cache_;
  // has_in_place_type_folds per inlined callee tree. Cleared per run().
  absl::flat_hash_map<const Lnast*, bool>        in_place_fold_cache_;

  // Perf: pre-cached subsets of `upasses`.
  //   fold_capable_passes — passes that override fold_ref(). try_fold_ref
  //     iterates only these instead of every pass.
  //   classify_capable_passes — passes that override classify_statement().
  //     any_pass_drops iterates only these instead of every pass.
  // Populated in the constructor right after `upasses` is fully built. With
  // 6 passes total and only 2–4 overriders, this cuts the per-node virtual
  // dispatch count by 30–50% on the bulk arithmetic hot loop.
  std::vector<upass::uPass*> classify_capable_passes;
  // Passes that expose shared-ST reads (provide_bundle_fields /
  // provide_typename), consulted by try_bundle_fields / try_typename.

  // Shared comb-call-inliner registry, built once and owned by pass_upass (see
  // uPass_function_registry above). nullptr for runners that never inline
  // (e.g. the bitwidth-only runner); reg() returns an empty registry then.
  const uPass_function_registry* registry_         = nullptr;
  uPass_detuple_registry*        detuple_registry_ = nullptr;
  const uPass_function_registry& reg() const;

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
  bool                                materialize_{true};                 // see set_materialize()
  bool                                preserve_param_provenance_{false};  // see set_preserve_param_provenance()
  absl::flat_hash_set<std::string>    preserved_param_names_;
  bool                                track_param_provenance();

  // The `pkg.PARAM` a folded ref came from, or "" when the value has no
  // imported-package origin. Reads uPass_constprop's tget_origin plus the
  // `pub_unit` marker call_resolver stamps on an import namespace bundle.
  std::string pkg_origin_of(std::string_view name) const;

  // The input Lnast being rebuilt (the lm tree at frame depth 0). The staging
  // body becomes ITS body via replace_body, so SourceIds carried into staging
  // must resolve in THIS Lnast's locator — carry_srcid imports ids read from
  // a callee tree (inline frame) into it.
  std::shared_ptr<Lnast> root_lnast_;

  // General carry: stamp the freshly created staging node with the
  // current source node's SourceId (cross-tree ids re-minted into
  // root_lnast_'s locator). Pass-synthesized nodes inherit the enclosing
  // statement's id (the read cursor's node).
  void carry_srcid(const Lnast_nid& staged);

  // Rewrite carry: a scratch statement (inl-bind/inl-spec/…) replaces
  // the statement the cursor sits on — stamp the scratch ROOT with the
  // nearest enclosing SourceId so carry_srcid's ancestor walk hands it to
  // every node emitted from the scratch walk (else the rewrite, and every
  // cell tolg mints from it, loses provenance).
  void stamp_scratch_srcid(const std::shared_ptr<Lnast>& scratch, const Lnast_nid& root);

  // Call-site SourceIds of the active inline frames (parallel to the
  // lm source-frame stack, pushed only around the body splice). carry_srcid
  // mints combine(callee_def, call_site) for spliced nodes — the callee def
  // stays the primary anchor, the call site becomes a Diagnostic::note.
  std::vector<hhds::SourceId> inline_call_sites_;

  std::vector<std::string> resolve_order(const std::vector<std::string>& requested_names, std::string* error_msg = nullptr) const;

  // ── Push-based dispatch ────────────────────────────────────────────────
  // Per-node operand resolution: dst = the live symbol-table bundle for the
  // first-child ref (created on first write — see lazy install in
  // dispatch_push), src = the remaining children in order (const →
  // make_const throwaway; ref → live bundle, empty throwaway when unbound).
  struct Resolved_node {
    std::string                 dst_name;
    std::shared_ptr<Bundle>     dst;
    bool                        dst_was_bound{false};
    std::vector<upass::Operand> src;
  };

  // Build dst/src for the op node under the cursor. Returns false when the
  // node has no leading dst ref (the push hook is then called with an empty
  // dst_name and a throwaway dst).
  bool resolve_node_operands(Resolved_node& out);

  // Dispatch a push-form hook to every pass (cursor saved/restored around
  // each call, runtime_error swallowed like dispatch_to_passes), then lazily
  // install a previously-unbound dst that a pass populated. Returns true
  // when any pass voted drop.
  bool dispatch_push(upass::Push_method fn, Resolved_node& rn);

  // Fold the typed fact fields from a resolved dst onto
  // the live slot bundle (constprop binds values mid-dispatch; see dispatch_push).
  static void merge_fact_fields(Bundle& bound, const Bundle& from);

  // Drop-candidate path for value-producing ops: resolve operands, push
  // dispatch, combine the votes with the legacy classify_statement, emit.
  void process_drop_candidate_push(upass::Push_method fn, bool fold_all);

  // Record one LSP semantic-index entry for the variable
  // DEFINED at the current op node (its source-level name, resolved type/range,
  // and statement span), for textDocument/hover. Only the caller's
  // livehd::lsp_index::index().enabled() gate (set by the LSP, never the CLI)
  // reaches here, so the normal pipeline pays one bool check per op and records
  // nothing. dst_name is the resolved (post-SSA) destination name.
  void record_lsp_def(std::string_view dst_name);

  // Declaration pre-step: read the TYPE subtree of a `declare`
  // (is_declare=true) or standalone `type_spec` ONCE and bake the persistent
  // facts into the destination's symbol-table bundle as TYPED fields (Kind +
  // declared max/min + comptime on the "0" Entry; mode/type_name on the
  // Bundle). End state: no pass walks a type subtree; until then the
  // passes' own walks coexist.
  void bake_decl_pre_step(bool is_declare);

  // Drain the dotted-bake stash (Symbol_table::pending_decl_facts):
  // apply declared facts to fields that just received their first value.
  void apply_pending_field_facts(std::string_view root);

  // Push the block scope for the stmts node under the cursor and mark
  // it uncertain when entering an unresolved if-arm. Pop side: callers run
  // symbol_table_.leave_scope() AFTER dispatching process_stmts_post.
  void enter_block_scope() {
    symbol_table_.block_scope(lm->current_scope_uid());
    if (next_block_uncertain_) {
      symbol_table_.mark_current_uncertain();
      next_block_uncertain_ = false;
    }
  }

  // Staging emit helpers.
  void emit_push(Lnast_ntype::Lnast_ntype_int type);
  void emit_pop();
  void emit_leaf(Lnast_ntype::Lnast_ntype_int type);
  void emit_leaf(const Lnast_node& node);
  void emit_current_leaf();
  void emit_subtree_verbatim();

  // Returns the first non-nullopt result from any pass's fold_ref(name).
  std::optional<Dlop> try_fold_ref(std::string_view name);

  // A comptime name's value, also through the active inline frame's binding
  // of it (an array dimension's `N` / `LO` inside an inlined body).
  [[nodiscard]] std::optional<Dlop> fold_frame_ref(std::string_view name) const;

  // Shared-ST reads: the comb-call inliner uses these to introspect
  // state it can't see through scalar fold_ref. try_bundle_fields returns the
  // flat comptime-const fields behind a bundle ref (for tuple actuals);
  // try_typename returns a var's declared typename (for method dispatch /
  // setter-init). First pass that provides wins.
  std::optional<std::vector<std::pair<std::string, Dlop>>> try_bundle_fields(std::string_view name);
  std::string                                              try_typename(std::string_view name);
  // Declared integer (max,min) range of a variable (for re-typing an untyped
  // inlined param from the actual at the call site). First pass that provides
  // wins. See uPass::provide_decl_type.
  std::optional<upass::uPass::Decl_scalar_type>            try_decl_type(std::string_view name);
  // Folded (start, end_inclusive, step) bounds of a `range` tmp (comptime
  // for-loop iterable). The step is per-range (defaults to 1; `a..=b step n`
  // sets it), so the unroll always does `v += step`. See uPass::provide_range.
  std::optional<std::tuple<Dlop, Dlop, Dlop>>              try_range(std::string_view name);
  // Declared kind + range of a dotted field path (`t1.a`). First pass that
  // provides wins. See uPass::provide_field_type.
  std::optional<upass::uPass::Field_decl_type>             try_field_type(std::string_view name);
  // Inferred scalar kind of a variable (bool vs int even when
  // un-annotated). First pass returning a non-none kind wins (typecheck).
  // See uPass::provide_scalar_kind.
  Io_kind                                                  try_scalar_kind(std::string_view name);
  // Comptime scalar kind of a call actual (const literal or ref): bool/string/
  // integer, falling back to integer for a range-carrying typed var; `none`
  // otherwise. The single source of truth shared by try_inline_func_call's real
  // bind and signature_matches's overload probe — they MUST agree on kind or a
  // probed-accepted candidate gets rejected at commit.
  Io_kind                                                  actual_node_kind(const Lnast_node& node);
  // Declared storage class (mut/const/reg/type) of a variable. First pass
  // that provides a non-unknown answer wins. See uPass::provide_decl_storage
  // (ref-actual mutability).
  upass::uPass::Decl_storage                               try_decl_storage(std::string_view name);

  // Typed-self `does`-check: the receiver bound to `self:T` must
  // structurally satisfy T (per declared field: same-name receiver field
  // present, scalar kinds match, integer range receiver ⊆ declared). Extra
  // receiver fields are fine. Emits a fatal `fcall-self-does` diagnostic on
  // the first failing field.
  void check_self_does(const livehd::diag::Span& span, std::string_view callee_name, std::string_view decl_tn,
                       const Lnast_node& receiver);

  // Emits either the folded value of `name` (when any pass returns a valid
  // Dlop) or the original ref node otherwise. Used by both emit_op_with_fold
  // and the statement-scope ref leaf case.
  void                                       emit_ref_or_folded(std::string_view name);
  // ── concat width binding ────────────────────────────────────────────────
  // A `concat`'s width operands arrive as the `nil` sentinel from a frontend
  // with no types. They are bound HERE, at emission, because the same emission
  // loop folds a comptime lane ref to a literal -- and a literal's magnitude is
  // not its window, so a width derived after that fold would be wrong.
  [[nodiscard]] static std::string_view      concat_logical_name(std::string_view name);
  void                                       check_concat_lanes();
  void                                       check_bitsel_named_bundle();
  // Shared by both spellings of the packing rule (a concat lane and `x#[..]`):
  // true when `name` resolves to a NAMED bundle with more than one field, which
  // has field identity but deliberately no field ORDER.
  [[nodiscard]] bool                         named_bundle_without_bit_order(std::string_view name) const;
  void                                       check_concat_dest(std::string_view dest_name, std::string_view value_name);
  [[nodiscard]] uint32_t                     concat_lane_declared_bits(std::string_view lane_name) const;
  [[nodiscard]] std::vector<std::string>     resolve_concat_widths(std::string& dst_name);
  // `x:u48 = 0sb?` -> the `0ub` + 48 `?` literal the wildcard stands for at this
  // destination, or "" to leave the store as written. See the definition.
  [[nodiscard]] std::string                  resolve_x_fill();
  // Result temp -> its lane sum, for the lanes of a NESTING concat: an inner
  // concat's temp is never declared by the user, but its width is the sum by
  // construction, which is what makes `concat(concat(a,b), c)` legal.
  absl::flat_hash_map<std::string, uint32_t> concat_result_bits_;
  absl::flat_hash_set<Lnast_nid>             concat_checked_;  // report each concat once, not per runner iteration
  absl::flat_hash_set<Lnast_nid>             bitsel_checked_;  // report each `#[..]` once, not per runner iteration
  absl::flat_hash_set<Lnast_nid>             concat_dest_checked_;

  // A declare/type_spec type slot that is a `ref` to a SCALAR named-type alias
  // (`x:PType` where `type PType = u10`; local OR imported `pkg.PType`) is
  // concretized into `prim_type_int(max,min)` / `prim_type_bool` /
  // `prim_type_string` in staging, so the width applies to the net (a bare ref
  // slot carries no range for the bitwidth/wire-net lowering; the symbol-table
  // decl_max only drives overflow checks). The `typename` provenance rides a
  // separate attr_set, so the prp_writer can still re-emit `:PType`. Returns
  // false for a TUPLE/struct or unresolved named type (emit it verbatim).
  // `port_name` is the io port this type slot belongs to (empty for a declare's
  // slot); with provenance on it records the alias into Lnast::io_type_names.
  std::pair<std::shared_ptr<Lnast>, Lnast_nid> lookup_file_type(std::string_view type_name);
  bool emit_scalar_named_type_slot(std::string_view type_name, std::string_view port_name = {});
  // Emit a declare/type_spec slot while concretizing scalar named aliases at
  // the element leaf of an array type. Array dimensions are copied verbatim:
  // they are values, not type names, and have their own comptime fold path.
  bool emit_concrete_type_slot();
  // Resolve an IMPORTED scalar alias `pkg.PType` off the exporting unit's pub
  // list ("type" kind) + its "MAX|MIN" pub_values face. True when the range
  // was recovered — used by the declare borrow AND the type-slot concretizer
  // (a lambda unit carries no import statement, so its symbol table never has
  // the namespace bundle).
  bool imported_alias_range(std::string_view type_name, Dlop& max_out, Dlop& min_out) const;
  // io staging: like emit_op_with_fold's verbatim copy of the io subtree, but a
  // port store's REF type slot (an imported alias) concretizes to prim_type_int.
  void emit_io_with_type_slots();

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
  // `tick` — emitted, never unrolled. Its body is walked as an UNCERTAIN scope
  // because the iteration count is assumed unknown (a runtime `--arg` can set it
  // to 0), so every variable written inside is invalidated on scope exit.
  void tick_uncertain_body();
  // Register every name a tick body stores to as an uncertain write, without
  // folding the body. Cursor-neutral.
  void register_tick_body_writes();
  // Strict bool (user ruling 2026-09-28 (20)) over a tick body, which is
  // emitted verbatim: a kind walk in statement order that reports what
  // typecheck and the call-site argument check would -- a poke of the wrong
  // kind into an instance input (`d.en = 1`), a bool operand where an operator
  // needs an integer or the reverse (`d.f + 1`), a non-bool condition.
  // Cursor-neutral.
  void check_tick_body_kinds();
  // A test block's `mut d = <unit>` (Symbol_table::instance_handles): record
  // the handle and its ports' declared facts. `dst` is the handle's name,
  // `rhs` the bound name.
  void bind_instance_handle(std::string_view dst, std::string_view rhs);
  // A poke `store(d, field..., value)` into an instance handle: kind-checked
  // against the input like a call-site argument, then emitted verbatim (a
  // handle has no fields to write). False when the store is not one.
  bool try_instance_handle_store();

  // With the cursor on a poke's handle child: the poked input (the fields up
  // to the value, dot-joined; "" when one is computed) and `value_kind()` of
  // the value, which the cursor is left on.
  std::pair<std::string, upass::Kind> instance_poke_operands(const std::function<upass::Kind()>& value_kind);
  // The call-argument rules for a poke of `value` (kind `got`) into input
  // `port` of `unit`: the kind rule, and the overflow rule when `value` (an
  // invalid node skips it) has a derived range.
  void check_instance_poke(const std::shared_ptr<Lnast>& unit, std::string_view port, upass::Kind got, const Lnast_node& value,
                           const livehd::diag::Span& span) const;
  // The Clock/Reset rules of a poke (docs 05b "Running cycles", qa.md section 6):
  // a `Clock` input (declared, or the minted `clock`) takes only a real Clock --
  // never a constant (`value` is a literal) and never a Bool expression; a
  // `Bool` input never takes a Clock (`value_is_clock`); the minted `reset` is
  // Bool-like, so an integer needs a Bool. Fatal on a violation.
  void check_instance_clock_poke(const std::shared_ptr<Lnast>& unit, std::string_view port, upass::Kind got,
                                 const Lnast_node& value, bool value_is_clock, const livehd::diag::Span& span) const;

  // After a tuple_get or a Sub call: a read of an opaque Sub output
  // (Symbol_table::opaque_sub_outputs) -- `c.o`, or the call result itself
  // when the callee has one output -- joins wide_values, so a typed
  // destination it reaches is the ruling-21 may-not-fit error.
  void note_opaque_output_read();
  // Re-walk ONE loop iteration. Precondition: the read cursor is on the loop's
  // body `stmts` node. Opens a fresh iteration scope (fresh salt + staging/pass
  // block scope), invokes `emit_binds` to bind the iteration variable(s) into
  // it (a Dlop for a range, a `tuple_get` pick for a tuple), re-walks the
  // body's statements, then closes the scope and restores the cursor to the
  // body `stmts` node. `emit_post` (optional) runs after the body, still inside
  // the iteration scope — used by `for i in ref d` to write the (possibly
  // mutated) value back into the slot. Returns false if the fuel/depth guard
  // tripped (caller stops iterating).
  bool walk_loop_iteration(const std::function<void()>& emit_binds, const std::function<void()>& emit_post = {});
  // RAII bracket around one comptime unroll: bumps loop_depth_ and opens an
  // iteration-ordinal level. `next_iteration()` advances the ordinal. Restores
  // both on scope exit, which the `while` unroller relies on — several of its
  // exits go through loop_fail (a throw).
  class Unroll_scope {
  public:
    explicit Unroll_scope(uPass_runner& r) : r_(r), depth_(r_.loop_iter_ordinals_.size()) {
      ++r_.loop_depth_;
      if (r_.next_loop_ordinal_bases_.size() <= depth_ + 1) {
        r_.next_loop_ordinal_bases_.resize(depth_ + 2, 0);
      }
      base_                                   = r_.next_loop_ordinal_bases_[depth_];
      r_.next_loop_ordinal_bases_[depth_ + 1] = 0;
      r_.loop_iter_ordinals_.push_back(base_);
    }
    Unroll_scope(const Unroll_scope&)            = delete;
    Unroll_scope& operator=(const Unroll_scope&) = delete;
    ~Unroll_scope() {
      r_.next_loop_ordinal_bases_[depth_]
          = iterations_ > std::numeric_limits<uint64_t>::max() - base_ ? std::numeric_limits<uint64_t>::max() : base_ + iterations_;
      --r_.loop_depth_;
      r_.loop_iter_ordinals_.pop_back();
    }
    void complete_iteration() { ++iterations_; }
    void next_iteration() {
      ++r_.loop_iter_ordinals_.back();
      r_.next_loop_ordinal_bases_[depth_ + 1] = 0;
    }

  private:
    uPass_runner& r_;
    uint64_t      base_       = 0;
    uint64_t      iterations_ = 0;
    size_t        depth_      = 0;
  };
  // `__li<ordinal>` per enclosing unroll, "" outside one. Stamped on the calls
  // an unrolled body emits so their instances stay distinguishable.
  [[nodiscard]] std::string loop_inst_suffix() const;
  // Append the reserved `__inst_suffix` actual to the func_call just emitted
  // into staging (cursor-free: it works on `staging_parent`). No-op unless the
  // callee is one tolg lowers to a Sub instance.
  void                      stamp_loop_inst_suffix(std::string_view callee);
  // Emit a per-iteration tuple pick `dst = src[index_text]` as a scratch
  // tuple_get run through the walk (so try_resolve_tuple_get / constprop
  // resolve it). index_text is the pyrope field literal ("0","1",… or "'name'").
  void                      emit_inline_tuple_pick(const std::string& dst, const std::string& src, const std::string& index_text);
  // Emit a per-iteration tuple write-back `dst[index_text] = value` (3-child
  // store) as a scratch tree run through the walk — the `for i in ref d` form.
  void emit_inline_tuple_store(const std::string& dst, const std::string& index_text, const std::string& value);
  // Emit a typed declare `mut name : int(max,min)` as a scratch tree. Used to
  // give a for-loop iteration variable a declared type when its tuple element is
  // a typed runtime ref (var-arg ports), so a nested specialization can type it.
  void emit_inline_declare_typed(const std::string& name, const std::optional<Dlop>& max, const std::optional<Dlop>& min);
  // `declare(name, [N]elem, mut)` for an inlined comb's ARRAY port (`v:[4]u8`,
  // `-> (r:[N]unsigned(bits=N))`), so the tagged copy keeps the declared array
  // shape instead of its packed width.
  struct Array_port_shape;
  void emit_inline_declare_array(const std::string& name, const Array_port_shape& shape);

  // ── comb-call inliner ────────────────────────────────────────────────
  // Called from process_lnast's func_call case. If the callee resolves to a
  // comb body in function_registry and the call shape is supported, performs
  // a virtual splice — emit prologue param-binding assigns, push_source into
  // the callee body and walk it (so every pass folds/observes and the
  // renamed body is emitted into staging), then emit the epilogue (output +
  // ref-param writeback) — and returns true. Returns false to fall back to
  // the normal func_call emit/dispatch path (typecasts, cell-ops, markers,
  // or call shapes this path does not yet handle). The read cursor is left on the
  // func_call node on every path.
  bool try_inline_func_call();

  // Lower a RUNTIME `wrap`/`sat` narrowing call to primitive nodes. The call
  // shape is func_call(dst, ref("wrap"|"sat"), store(ref("v"),
  // value), store(ref("type"), ref(lhs))). When the value is a comptime
  // constant the attributes pass already folds it (and the drop path retires
  // the call), so this declines (returns false) for those. For a runtime
  // value it reads the lhs's declared envelope (decl_facts::lookup) and the
  // value's bitwidth-derived range, then emits:
  //   * nothing-to-narrow (value range already fits the type) → `dst = value`.
  //   * wrap → get_mask(dst, value, 0, N)  [+ sext(dst, .., N-1) when signed]
  //     (C/C++ truncation: low N bits, sign-reinterpreted per the type).
  //   * sat  → seed `dst = value`, then bw-gated clamps `if (value > max) dst =
  //     max` / `if (value < min) dst = min` (signed targets re-sign the clamp
  //     result through a final sext). Clamps to the DECLARED max/min (handles
  //     non-pow2 `int(min,max)`).
  // Runs the per-pass process_func_call hooks first (mirrors
  // process_drop_candidate step 1) so bitwidth's wrap_sat_exempt_ handshake
  // still suppresses the does-not-fit error on the trailing store(lhs,dst).
  // Returns true (suppressing the func_call emit) iff it handled the call. The
  // read cursor is left on the func_call node on every path.
  bool try_lower_wrap_sat();

  // Runtime scalar typecast lowering: `int(x)` / `uint(x)` / `uN(x)` / `sN(x)`
  // on a RUNTIME operand (a comptime-constant operand is folded by constprop —
  // this declines for those so the fold path runs). Mirrors try_lower_wrap_sat:
  //   * int(bool)/sN(bool)  → sext(dst, x, 0)        — true = -1 (1-bit signed)
  //   * uint(bool)/uN(bool) → get_mask(dst, x, 0)  — true = 1  (unsigned bit)
  //   * int(intval)         → `dst = x` (signed is unbounded; value-preserving)
  //   * uint/uN/sN(intval)  → the operand range must provably FIT the target
  //     (a sized cast is CHECKED, not truncating); if it can overflow this is a
  //     compile error (use `wrap`/`sat`). When it fits → `dst = x`.
  //   * string()/bool() of a runtime value → compile error (no hardware form).
  // Runs the per-pass process_func_call hooks first (like try_lower_wrap_sat)
  // before emitting. Returns true iff it consumed the call (handled or errored);
  // declines (false, cursor restored) for non-casts and comptime operands.
  bool try_lower_typecast();

  // Lower a `func_in` (`a in b`) node into hardware. The right operand `b` must
  // be an UNNAMED tuple/array whose size is known at compile time (it always is
  // in upass — tuple shapes are comptime), and every element must have the same
  // type as `a` (int width/signedness may differ; bool/int/enum/tuple kinds may
  // not). The node expands, in source order, to
  //   dst = (a == b[0]) or (a == b[1]) or … or (a == b[N-1])
  // emitted through the inline (scratch-tree) path so constprop folds the
  // constant comparisons (`1 in (runtime,2)` ⇒ `1==runtime`). An empty `b` folds
  // to `false`. A named/unresolved `b`, or a kind mismatch, is a COMPILE ERROR
  // (never a silent nil) — the expansion is finished here, so tolg never sees a
  // func_in. The read cursor is left on the func_in node. Returns false only on
  // a structurally malformed node (so the caller can fall back).
  bool lower_in();

  // Var-arg access resolution. A `comb foo(...args)` call gathers its
  // leftover actuals here, keyed by the FRAME-TAGGED var-arg name (e.g.
  // "inl1_args"): positional leftovers under decimal keys "0","1",…, named
  // leftovers under their names. The body's `args[i]` / `args.NAME` reads are a
  // COMPTIME-structural pick (the index/identity is known at the call site even
  // when the value is a runtime signal), so try_resolve_vararg_get rewrites
  // each such `tuple_get` into a direct `dst = <actual>` copy during the body
  // walk — avoiding a runtime tuple_add/tuple_get that tolg cannot lower.
  // Entries are registered in the prologue and erased after the body walk.
  absl::flat_hash_map<std::string, std::vector<std::pair<std::string, Lnast_node>>> vararg_bindings_;
  // Flattened tuple-port ABI used by the streaming tuple rewrite. uPass_ssa
  // harvests a composite source signature into dotted scalar io_meta leaves;
  // the runner then consumes field reads/writes directly while it builds the
  // sole output tree, instead of requiring SSA to first materialize a second
  // whole body just to rename `p['f']` to `p.f`.
  absl::flat_hash_set<std::string>                                                  stream_port_in_leaf_;
  absl::flat_hash_set<std::string>                                                  stream_port_out_leaf_;
  absl::flat_hash_set<std::string>                                                  io_output_names_;
  // The TOP unit's Lnast_io_entry::default_value_name() locals (one per
  // defaulted input): hardware-less, but kept like an output so a later
  // inliner of the materialized body still finds each default.
  absl::flat_hash_set<std::string>                                                  default_value_names_;
  // The calls (func_call class indices of the source tree) in a TOP `comb`
  // unit's default-only prologue (`b:u8 = g(a=a)`): kept as calls in its own
  // walk (user ruling 2026-09-28 (39)), see try_inline_func_call.
  absl::flat_hash_set<int64_t>                                                      default_prologue_calls_;
  // 1i-inline — the `inl<N>_<port>` locals an inlined `comb` frame mints for
  // the CALLEE's output ports. io_output_names_ holds only the TOP unit's own
  // ports (initialize_stream_port_abi), and an inlined output is declared with
  // a bare `type_spec` so it carries no storage mode either — which left it
  // matching neither of process_drop_candidate_push's "this store is a real
  // hardware driver" guards. See the comment there. Names are salted by the
  // monotonic inline_seq_, so entries never collide and the set is never
  // popped: a stale hit could only KEEP a store (a missed fold), never drop
  // one, which is the safe direction.
  absl::flat_hash_set<std::string>                                                  inline_output_names_;
  // THE predicate both drop paths (process_drop_candidate and
  // process_drop_candidate_push) ask, so they can never disagree.
  bool                                          is_inline_output_driver(std::string_view name) const;
  absl::flat_hash_set<std::string>              stream_port_prefix_;
  absl::flat_hash_map<std::string, std::string> stream_port_alias_;

  // value_kind / comptime_value: filled as the unit's own body block closes.
  struct Body_value {
    upass::Kind         kind{upass::Kind::unknown};
    std::optional<Dlop> value;
  };
  absl::flat_hash_map<std::string, Body_value> unit_body_values_;
  void                                         snapshot_unit_body();

  struct Stream_ssa_def {
    std::string source;
    std::string output;
    std::string previous;
  };
  bool                                          stream_ssa_enabled_{false};
  absl::flat_hash_set<std::string>              stream_ssa_state_names_;
  absl::flat_hash_map<std::string, std::string> stream_ssa_current_;
  absl::flat_hash_map<std::string, int>         stream_ssa_count_;
  absl::flat_hash_map<uint64_t, Stream_ssa_def> stream_ssa_defs_;
  std::optional<Stream_ssa_def>                 stream_ssa_active_def_;

  // Streaming detuple state. The detupler is the first consumer of the
  // runner-owned Symbol_table: tuple shape/type nodes update this semantic
  // state and are consumed before the remaining passes see scalar operations.
  // Nothing here owns or materializes an LNAST.
  struct Detuple_tuple_value {
    bool                                            named{false};
    std::vector<std::pair<std::string, Lnast_node>> fields;
    std::vector<Lnast_node>                         positional;
    // Parallel to `positional`: the SOURCE spelling of a bare-variable entry
    // (`(b, a)` -> "b", "a"), empty for a constant. A typed-tuple construction
    // binds such an entry to the field it spells (call naming exception 2).
    std::vector<std::string>                        positional_src;
  };
  struct Detuple_pending_decl {
    Lnast_nid                      nid;
    std::string                    name;
    std::string                    mode;
    std::optional<std::string>     init_ref;
    std::optional<std::string>     shape_tmp;
    uPass_detuple_registry::Layout fields;
  };
  struct Detuple_split {
    uPass_detuple_registry::Layout fields;
    std::string                    mode;
    bool                           memory{false};
    Lnast_node                     dimension{Lnast_node::create_invalid()};
    // The first whole-tuple store is the CONSTRUCTION (its unnamed values bind
    // like call arguments); later whole-tuple stores are plain assignments.
    bool                           whole_bound{false};
  };
  struct Detuple_index_alias {
    std::string memory;
    Lnast_node  index{Lnast_node::create_invalid()};
  };

  std::optional<Detuple_pending_decl>                              detuple_pending_decl_;
  absl::flat_hash_map<std::string, Detuple_tuple_value>            detuple_tuple_values_;
  absl::flat_hash_map<std::string, uPass_detuple_registry::Layout> detuple_shape_fields_;
  // Slang emits aggregate field type_specs before the bare aggregate declare
  // (`type_spec(io.a,T)...; declare(io,none,wire)`). Cache that already-seen
  // shape so the declaration can split immediately without looking ahead.
  absl::flat_hash_map<std::string, uPass_detuple_registry::Layout> detuple_predecl_fields_;
  // 2f-defaulted_tuple — the NAMED-child twin of detuple_shape_fields_.
  //
  // prp2lnast lowers a tuple type's field two different ways. Without a
  // default (`type D = (a:bool, b:u5)`) the field is a `typed_field` and
  // arrives as a POSITIONAL `ref` child whose name already carries the scalar
  // type, which detuple_shape_fields_ collects. WITH a default
  // (`type D = (mut a:bool = nil, …)`, 03-bundle.md "Tuple named fields can
  // have a default type and or contents") the field is an `assignment`, so it
  // arrives as a NAMED `store` child and the type rides a LATER
  // `tuple_get` + `type_spec` pair against an anonymous projection temp.
  // all_typed_refs went false, no layout was published, and `reg instr:D`
  // stayed a whole-tuple aggregate whose field writes hard-errored in tolg
  // ("tuple/field store has no hardware lowering").
  //
  // So: record the field ORDER here as the named children are seen, then let
  // detuple_field_alias_ carry each projection temp back to its (tuple, field)
  // so the trailing type_spec can fill the type in.
  absl::flat_hash_map<std::string, uPass_detuple_registry::Layout> detuple_named_layout_;
  struct Detuple_field_alias {
    std::string tuple_tmp;
    std::string field;
  };
  absl::flat_hash_map<std::string, Detuple_field_alias> detuple_field_alias_;
  absl::flat_hash_map<std::string, Detuple_split>       detuple_splits_;
  absl::flat_hash_map<std::string, Detuple_index_alias> detuple_index_aliases_;
  bool                                                  detuple_replay_{false};
  bool                                                  detuple_synthetic_{false};

  bool        try_detuple_declare();
  bool        try_detuple_store();
  bool        try_detuple_tuple_add();
  bool        try_detuple_tuple_get();
  bool        try_detuple_typespec();
  std::string detuple_text(const Lnast_nid& nid) const;
  std::string detuple_split_name(std::string_view var) const;
  std::string detuple_registry_key(std::string_view type_name) const;
  void        detuple_commit_pending_split(const Detuple_pending_decl& pending);
  bool        detuple_finalize_pending_decl();
  void        detuple_flush_pending_decl();
  void        detuple_flush_pending_before_current();
  void        detuple_publish_named_type(std::string_view name, std::string_view rhs);
  // 2f-nested_type — flatten a (possibly NESTED) tuple VALUE into dotted leaf
  // paths, so a whole-tuple assignment can bind it against a split destination
  // whose field names are themselves dotted leaf paths. Flat input yields the
  // same list it went in as.
  void        detuple_flatten_tuple_value(const std::string& rhs, const std::string& prefix,
                                          std::vector<std::pair<std::string, Lnast_node>>& out, int depth = 0);
  // A declaration's tuple INITIALIZER projected onto its split leaves: out[i]
  // is the value of fields[i] (nullopt: none). A NESTED field takes its leaves
  // from a nested tuple literal, named (`n=(x=5, y=2)`) or positional, from a
  // split tuple variable of the same shape, or from dotted keys (`n.x=5`); a
  // flat positional initializer gives one entry per leaf. Returns false after
  // reporting a shape mismatch (an unknown field, a wrong arity, a nested
  // field given a scalar). `type_name` names the declared type in the arity
  // error, when there is one.
  bool detuple_project_init(std::string_view var, const Detuple_tuple_value& init, const uPass_detuple_registry::Layout& fields,
                            std::vector<std::optional<Lnast_node>>& out, std::string_view type_name = {});
  // A flat layout (no nested field): the one the unnamed-value binding handles.
  static bool detuple_layout_is_flat(const uPass_detuple_registry::Layout& fields);
  // Binds the UNNAMED values of a typed tuple construction like the arguments of
  // a call; `out` holds the fields already bound by name. False when reported.
  bool detuple_bind_unnamed_values(std::string_view var, std::string_view type_name, const Detuple_tuple_value& value,
                                   const uPass_detuple_registry::Layout& fields, std::vector<std::optional<Lnast_node>>& out);
  std::optional<uPass_detuple_registry::Scalar_type> detuple_scalar_type(std::string_view name) const;
  void detuple_emit_declare(std::string_view name, const uPass_detuple_registry::Scalar_type& type, std::string_view mode,
                            const Lnast_node* init = nullptr, const Lnast_node* dimension = nullptr);
  void detuple_emit_store(std::string_view name, const std::vector<Lnast_node>& operands);
  bool detuple_validate_scalar_store(std::string_view name, const uPass_detuple_registry::Scalar_type& type,
                                     const Lnast_node& value);
  void detuple_error(std::string code, std::string message, std::string hint = {});

  void                                    initialize_stream_port_abi();
  std::optional<std::string>              resolve_stream_port_path(std::string_view name) const;
  // Cursor on store(tmp, tuple-port-prefix). The parser emits this carrier
  // before a dotted access chain. Record only the structural alias and delay
  // materialization until a scalar leaf is selected.
  bool                                    try_stream_tuple_port_alias_store();
  // Cursor on a just-dispatched 2-child store. A runtime tuple VALUE bound to
  // a variable that can be rewritten under control flow gets one scalar leaf
  // per runtime field, so its field reads merge per leaf (see definition).
  std::optional<std::vector<std::string>> runtime_tuple_prior_fields() const;
  void                                    split_runtime_tuple_store(const std::optional<std::vector<std::string>>& prior);
  // Cursor on a >=3-child store. Rewrites a static tuple-output field write
  // into a scalar dotted-leaf binding and dispatches that synthesized binding
  // through every enabled pass. False means the ordinary tuple-set path owns
  // the statement (non-port, dynamic path, input write, or malformed node).
  bool                                    try_stream_tuple_port_store();
  bool                                    try_split_leaf_field_store();
  // Tuple values (by SSA base name) split_runtime_tuple_store gave scalar
  // leaves, and the fields that have one.
  absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>> split_tuple_leaves_;
  // Assign the current value-producing statement's output version before
  // dispatch. Passes continue to see source names; only the emitted LNAST uses
  // these scalar SSA names.
  void                                                               note_stream_ssa_definition();
  std::string                                                        stream_ssa_ref_name(std::string_view name) const;
  // Called from process_lnast's tuple_get case. Returns true (and emits a copy
  // `dst = <picked ref>`) iff the cursor's tuple_get is a single-segment pick
  // with a comptime-known index/name resolving to a known runtime ref — from a
  // gathered var-arg (vararg_bindings_) OR constprop's slot→ref map
  // (try_tuple_slot_ref). False leaves the node to the normal fold/emit path
  // (nested access, dynamic index, comptime slot, or unknown ref).
  bool                                                               try_resolve_tuple_get();
  // `dst = src[idx]` with a RUNTIME index into a comptime fixed-size tuple of
  // scalar wires (`const choices=[a,b,c,d]`) lowers to a balanced Hotmux —
  // `match idx { ==0 {dst=e0} … else {dst=e_{n-1}} }` — instead of erroring in
  // tolg (only memories/multi-out/comptime indices accept a dynamic index).
  // Gated on >=1 slot being a genuine runtime-wire ref (tuple_slot_ref), which
  // a `mut`/`reg` memory array never populates, so the memory path is untouched.
  bool try_lower_dynamic_tuple_index(const std::string& dst, const std::string& src, const std::string& idx_ref);
  // Source ref held at `slot` of tuple `name` (runtime-scalar slot). First pass
  // that provides wins. See uPass::provide_tuple_slot_ref.
  std::optional<std::string>                               try_tuple_slot_ref(std::string_view name, std::string_view slot);
  // Ordered (slot-key, is_positional) shape of tuple `name` for the runner's
  // tuple-for unroll. First pass that provides wins. See provide_tuple_shape.
  std::optional<std::vector<std::pair<std::string, bool>>> try_tuple_shape(std::string_view name);
  // Post-step of process_lnast's tuple_add case (cursor on the tuple_add,
  // AFTER the pass dispatch rebuilt dst's bundle + slot→ref map): backfill
  // tuple_slot_ref entries for REF-valued fields whose value is a RUNTIME
  // SCALAR carrying an ST bundle. Constprop only records a carrier when the
  // ref has NO bundle at all (an input port); a local/temp (`const vh =
  // fi#[4..=7]`, or an inline expression's tmp) holds a trivial-scalar bundle
  // with no comptime value, so its field stores null and the runtime carrier
  // name is lost — a later tuple-ACTUAL expansion (expand_tuple_actual) or
  // dotted read then cannot resolve the leaf. Comptime scalars are skipped
  // (the bundle trivial stays the authority) and genuine tuples are skipped
  // (their runtime leaves were re-homed by constprop's propagate_sub_slot_refs).
  void                                                     record_runtime_tuple_slot_refs();

  // ── pipe/mod/fluid template specialization ────────────────────
  // Called from try_inline_func_call at the pipe/mod decline point when the
  // resolved callee is a TEMPLATE (untyped boundary). Reads each untyped fixed
  // port's concrete type from the actual's DECLARED type (decision 4: an
  // untyped actual into such a port is a fatal call-site error), mints (or
  // reuses) a concrete specialized module named `<callee>__<sig>`, appends it
  // to new_lnasts (pass_upass re-SSAs + lowers it), and emits the call to the
  // mangled name so tolg instantiates the Sub. Returns true (call emitted);
  // a method (`ref self`) or var-arg boundary is left to the caller.
  struct Spec_port {
    bool                inject     = false;  // false = keep the template's own (already-typed) port
    std::optional<Dlop> max        = {};
    std::optional<Dlop> min        = {};
    std::string         type_name  = {};  // named type (takes precedence over max/min)
    // Var-arg expansion: a synthesized concrete port replacing one
    // leftover of a `...args` boundary. `port_name` is the new io port name;
    // `is_named`/`field` drive the in-body reconstruction tuple. (max/min/
    // type_name carry the actual's type, same as a fixed port.)
    std::string         port_name  = {};
    bool                is_named   = false;
    std::string         field      = {};
    // Scalar bool needs its native LNAST type node. Encoding it as the integer
    // range [0,1] loses Pyrope's bool-vs-int distinction and, for a rolled
    // carry, used to let `true` widen inconsistently across the lifted boundary.
    Io_kind             kind       = Io_kind::none;
    // An ARRAY carry (`mut v:[N]T`): the boundary port is declared `[N]T` too
    // (comp_type_array over the element range in max/min); lnast.tolg lowers
    // such a port as the packed bus plus the lane view. Also supplies an
    // inferred [] input extent when specializing a hardware call. 0 = scalar.
    int64_t             array_size = 0;
  };
  // An injected scalar `bool` port; the [0,1] envelope stays for range readers.
  static Spec_port bool_spec_port() {
    Spec_port sp{true, *Dlop::create_integer(1), *Dlop::create_integer(0), {}};
    sp.kind = Io_kind::boolean;
    return sp;
  }
  // Declared OUTPUT-port facts for a call that lowers to a Sub instance rather
  // than an inline splice (see the definition). `out_inject` (indexed like the
  // callee's outputs) carries a specialized template's generic-width outputs
  // folded under this call's binds; the template's own io_meta has no bound.
  void stash_sub_instance_port_facts(std::string_view handle, const std::shared_ptr<Lnast>& callee,
                                     const std::vector<Spec_port>* out_inject = nullptr);

  // ── generic `<T,…>` per-call-site binding (2f-generics) ──────────────────
  // One resolved binding per generic name: pure type-macro expansion — the
  // bound type substitutes into `a:T` params, `-> (r:T)` outputs and body
  // `:T` slots; normal typing rules apply afterwards (no special coercion).
  struct Generic_bind {
    uPass_detuple_registry::Layout tuple_fields;
    Io_kind                        kind         = Io_kind::none;  // integer/boolean/string; none = named type
    std::optional<Dlop>            max          = {};             // integer envelope when known
    std::optional<Dlop>            min          = {};
    std::string                    type_name    = {};  // named type (kind == none)
    std::string                    from         = {};  // binding source, for the mismatch diagnostic
    // A CONSTANT-valued generic (`f<3>`): the literal substituted for body reads
    // of the generic name (`r = a + N` → `a + 3`). Non-empty ⇒ constant bind;
    // `kind`/`max`/`min` still carry its envelope (so a constant bound into a
    // type slot has a width — todo 3g D). Never inferred (explicit/default only).
    std::string                    const_text   = {};
    // A constant bound FROM a typed comptime constant (`f<N=W>` with `comptime
    // const W:u8 = 13`): W's declared envelope. An attribute query on a generic
    // reads the substituted ENTITY (docs 06-functions), so `N.[bits]` is 8 as
    // `W.[bits]` is, not the value width of the literal 13 (user ruling 6).
    bool                           decl_typed   = false;
    std::optional<Dlop>            decl_max     = {};
    std::optional<Dlop>            decl_min     = {};
    // A LAMBDA-valued generic (`f<inc>`): the bound callee name, registered in
    // func_param_bindings_ so a body call `F(v)` dispatches to it (todo 3g A).
    std::string                    func_name    = {};
    // This bind came from the DECLARATION DEFAULT (`<N=8>`), not from an
    // explicit `<…>` argument and not from inference. A call that defaults
    // EVERY declared generic and injects no port type is an IDENTITY
    // specialization (see maybe_specialize_template_call).
    bool                           from_default = false;
  };
  // One explicit `<…>` argument at a call site. `value` is the bound entity's
  // text (a type ref / tmp, a constant, or a lambda name); `name` is set for a
  // NAMED bind (`f<T=u8>`, todo 3g C) and empty for a positional one.
  // `src_name`/`src_base` are a bare identifier's SOURCE spelling (see
  // Actual::src_name).
  struct Generic_actual {
    std::string value    = {};
    std::string name     = {};
    std::string src_name = {};
    std::string src_base = {};
  };
  // Explicit `<…>` bindings (positional and/or named) win; otherwise each
  // generic is inferred from the DECLARED types of the actuals at its `:T`
  // positions (literals contribute their kind only), then falls to its
  // declaration default. Conflicts and arity mismatches are fatal call-site
  // errors. A generic that nothing types stays absent (`triadd(a=1,b=2,c=3)`).
  [[nodiscard]] static std::string               generic_cast_token(const Generic_bind& gb);
  absl::flat_hash_map<std::string, Generic_bind> resolve_generic_binds(
      const std::shared_ptr<Lnast>& callee, const Lnast_tree_io& io, const std::vector<Lnast_node>& param_val,
      const std::vector<bool>& param_set, std::size_t nbind, const std::vector<Generic_actual>& explicit_generics,
      const std::string& callee_name, const livehd::diag::Span& call_span);

  // User ruling 2026-09-27 (lhdtrack suggestions 1.1-d): an argument never
  // narrows implicitly. Every provided actual must fit its parameter's
  // DECLARED type (a plain, generic-width or type-generic `:T` integer range)
  // — the overflow rule of a typed assignment — for every callee kind, so it
  // runs once, before the call splits into its inline / Sub / specialize
  // lowering. A wider integer bound to a `bool` input is the same error, and so
  // is an actual too wide for any derived range (`x << n`). An untyped
  // parameter takes the actual's type (no check). Fatal `fcall-arg-overflow`.
  void check_call_args_fit(const std::shared_ptr<Lnast>& callee, const Lnast_tree_io& io, const std::vector<Lnast_node>& param_val,
                           const std::vector<bool>& param_set, std::size_t nbind,
                           const absl::flat_hash_map<std::string, Generic_bind>& gbinds, std::string_view callee_name,
                           const livehd::diag::Span& call_span);
  // An omitted `mod`/`pipe` input's declared default drives the port from the
  // call site, so it must fit the input like an actual would. Fatal
  // `fcall-arg-overflow` (or `fcall-arg-kind` for an integer into a `bool`).
  static void check_omitted_default_fit(const std::shared_ptr<Lnast>& callee, const Lnast_io_entry& e,
                                        const absl::flat_hash_map<std::string, Generic_bind>& gbinds, std::string_view bare,
                                        const livehd::diag::Span& call_span);
  // The declared [min, max] of input `e` once `gbinds` are bound: a plain
  // integer range, a folded generic-width bound, a `:T` bound to an integer
  // type, or [0, 1] for a bool. nullopt = untyped (it takes the actual's type)
  // or a bound that does not fold here.
  [[nodiscard]] static std::optional<std::pair<Dlop, Dlop>> declared_param_range(
      const std::shared_ptr<Lnast>& callee, const Lnast_io_entry& e, const absl::flat_hash_map<std::string, Generic_bind>& gbinds);
  // The declared shape of a one-dimensional ARRAY port once `gbinds` are
  // bound: its lane count and element envelope, from a `[4]u8` io entry or
  // from the template's declared `[N]unsigned(bits=N)` type folded under the
  // binds. An open outer dimension retains its element envelope and takes
  // its lane count from `actual` when supplied. nullopt for a scalar port or
  // a shape whose declared element bounds do not fold here.
  struct Array_port_shape {
    int64_t              lanes{0};
    bool                 infer_lanes{false};  // [] input: each call supplies the extent
    Dlop                 elem_min;            // the LEAF element's range (`[2][4]u8`: a u8)
    Dlop                 elem_max;
    bool                 elem_bool{false};
    std::vector<int64_t> inner_dims;  // below `lanes`, outermost first (Lnast_io_entry::inner_dims)
  };
  [[nodiscard]] std::optional<Array_port_shape> array_port_shape(const std::shared_ptr<Lnast>& callee, const Lnast_io_entry& e,
                                                                 bool                                                  output,
                                                                 const absl::flat_hash_map<std::string, Generic_bind>& gbinds,
                                                                 const Lnast_node* actual = nullptr);
  // What the overflow checks know about a value: whether it is an integer and,
  // if so, the may-hold range {min, max} (an integer literal exactly, else its
  // bitwidth-derived range, else a Sub output port's range, else its declared
  // type). An integer too wide for a derived range (`x << n`) is unbounded; a
  // bool, string, tuple, a bit-pattern literal (force semantics) or a value
  // nothing derived a range for is not judged here (the kind checks own it).
  struct Value_range {
    bool                integer{false};
    std::optional<Dlop> min;
    std::optional<Dlop> max;
    [[nodiscard]] bool  bounded() const { return min.has_value() && max.has_value(); }
    // True when some value of this integer may fall outside [dmin, dmax]. A
    // value too wide for a derived range may not fit any range an i64 holds;
    // against a wider one it cannot be judged.
    [[nodiscard]] bool  may_exceed(const Dlop& dmin, const Dlop& dmax) const {
      if (!integer) {
        return false;
      }
      if (!bounded()) {
        return dmax.get_signed_bits() <= 62 && dmin.get_signed_bits() <= 62;
      }
      return max->gt_op(dmax)->is_known_true() || min->lt_op(dmin)->is_known_true();
    }
  };
  Value_range value_range_of(const Lnast_node& v) const;
  // The kind of a call actual: a literal's, the value's own, or the declared
  // kind of an IO port / Sub instance output. `unknown` when nothing says.
  upass::Kind value_kind_of(const Lnast_node& v) const;
  // User ruling 2026-09-27 (2): booleans never mix with integers at a call
  // boundary. A bit, a bit-select or a literal bound to a `bool` input needs
  // `boolean(x)`; a bool bound to an integer input needs `u1(x)`. Fatal
  // `fcall-arg-kind`; runs for every callee kind (inline, Sub, specialize).
  void        check_call_arg_kind(const Lnast_io_entry& e, const absl::flat_hash_map<std::string, Generic_bind>& gbinds,
                                  const Lnast_node& actual, std::string_view callee, const livehd::diag::Span& call_span) const;
  // The kind rule itself: `got` bound to an input declared `want`.
  static void check_bind_kind(const Lnast_io_entry& e, Io_kind want, upass::Kind got, std::string_view callee,
                              const livehd::diag::Span& call_span);
  // Whether a Sub-instance call may omit input `e`: lnast.tolg drives it from
  // its declared default or auto-wires the caller's clock/reset (ruling 12).
  bool        sub_input_may_be_omitted(const std::shared_ptr<Lnast>& callee, const Lnast_io_entry& e) const;
  // `mut m:[N]T = v` whose extent the front end could not fold (a generic or
  // named `[N]`): it pre-filled nothing, so the store of `v` would leave a bare
  // scalar where N lanes belong. At that store (the cursor), the lanes to
  // pre-fill with `v` once the store is processed; nullopt otherwise.
  struct Array_prefill {
    std::string          var;
    std::string          value;
    std::vector<int64_t> dims;  // outermost first
  };
  [[nodiscard]] std::optional<Array_prefill> array_init_prefill() const;
  void                                       emit_array_prefill(const Array_prefill& fill);

  // An array dimension that is not a plain extent (08-memories.md "Array
  // index"): an index range (`[100..<132]` is 32 entries whose first index is
  // 100, `[-8..<7]` takes signed indices) or an enum type (`[X]`, one entry per
  // entry of `X`, indexed only by those entries). The array itself is a plain
  // zero-based `[lanes]` array: the declaration is lowered to that extent
  // (Index_dims_scope) and every index is checked and rebased at its access
  // (try_lower_array_index), so nothing downstream sees the index map.
  struct Array_index_dim {
    int64_t     lanes = 0;
    int64_t     lo    = 0;  // index (enum value) of the first entry
    std::string enum_type;  // non-empty: indexed only by the entries of this enum
  };
  // The index map of dimension text `dim_txt` (`[…]`), nullopt for a plain
  // extent or a dim that does not resolve. An enum whose entries are not
  // numbered consecutively (a default one-hot enum) sets `*why` instead.
  [[nodiscard]] std::optional<Array_index_dim> array_index_dim(std::string_view dim_txt, std::string* why) const;
  // The index map of a dimension written as a `range` value `name` (a range
  // whose bounds prp2lnast could not fold), nullopt when it is not one.
  [[nodiscard]] std::optional<Array_index_dim> range_value_dim(std::string_view name) const;
  // Cursor at a declare: while the declare is processed, each index-range /
  // enum dimension reads as its plain `[lanes]` extent (bake, staging, tolg);
  // on exit the dim text is restored for a re-walk and the declared array
  // records its index map (`__array_lo<k>`, `__array_n<k>`, `__array_enum<k>`)
  // on its binding, so a copy or a comb argument keeps it.
  class Index_dims_scope {
  public:
    explicit Index_dims_scope(uPass_runner& r);
    ~Index_dims_scope();
    Index_dims_scope(const Index_dims_scope&)            = delete;
    Index_dims_scope& operator=(const Index_dims_scope&) = delete;

  private:
    struct Level {
      Lnast_nid       dim;
      std::string     text;  // the dim as written
      Array_index_dim map;
      bool            is_ref = false;  // a `range` value ref, not const text
    };
    uPass_runner&          r_;
    std::shared_ptr<Lnast> ln_;  // the tree holding the declare (the renamed dims)
    std::string            var_;
    std::vector<Level>     levels_;  // outermost first; one per comp_type_array level
    bool                   mapped_ = false;
  };
  // Cursor at a store (`store(a, i…, v)`) or tuple_get (`tuple_get(d, a, i…)`)
  // of an array with an index map: check every index (an enum dimension takes
  // only entries of its enum, a known index must be inside the range) and
  // re-issue the access with zero-based indices. False when there is nothing
  // to rebase (the caller processes the node as usual).
  bool try_lower_array_index();
  // Some array of this unit has an index map (a cheap filter before the
  // bundle read of every element access).
  bool any_index_mapped_{false};
  // Index_dims_scope already reported the declaration's dimension.
  bool index_dim_reported_{false};
  // Set while an access is re-issued with zero-based indices (and while the
  // physical per-lane pre-fill or a compiler-picked position runs): those
  // indices are never rebased again.
  bool physical_indices_{false};

  // `mut a:[] = 0` (every dimension inferred from its uses, 08-memories.md):
  // the array's name -> its initializer's fill value. A constant read of an
  // entry no write reached is that fill (the extent grows with the indices
  // that reach it), until a runtime-index write: the runner then no longer
  // knows which entries a write reached.
  absl::flat_hash_map<std::string, std::string> open_fill_arrays_;
  // Cursor at a whole store `store(a, v)`: drop `a`'s fill, and record it anew
  // when the store is the initializer of such an array.
  void                                          track_open_fill_array();
  // Cursor at a store (`store(a, i…, v)`) or tuple_get (`tuple_get(d, a, i…)`)
  // of such an array: a runtime-index write ends the fill; a constant read of
  // an entry no write reached becomes `d = fill`. True when it emitted that.
  bool                                          try_open_fill_access();

  // Does `tmpl` hold a type bound or an array dimension that
  // bake_decl_pre_step folds IN PLACE (`unsigned(bits=x.[bits])`, `[N]T`)? An
  // inline of such a callee must walk a private copy (try_inline_func_call).
  bool has_in_place_type_folds(const Lnast& tmpl);
  // Every dimension of an array port of template `callee` must fold under
  // `gbinds` (`v:[N+1]u4`), or the specialization would harvest a width-less
  // port. Fatal `array-port-dim-not-comptime` at the call.
  void check_array_port_dims(const std::shared_ptr<Lnast>& callee, const Lnast_tree_io& io,
                             const absl::flat_hash_map<std::string, Generic_bind>& gbinds, std::string_view callee_name,
                             const livehd::diag::Span& call_span) const;
  // Ruling 1 for an ARRAY input (`v:[4]u4`, `v:[N]unsigned(bits=N)`): the
  // actual must be an array of the declared length whose element type fits the
  // declared element (`fcall-arg-shape` / `fcall-arg-overflow`). An actual of
  // unknown fixed shape is not judged. Open inputs validate each captured
  // element. Probe mode returns false instead of emitting a diagnostic.
  bool check_call_array_arg(const std::shared_ptr<Lnast>& callee, const Lnast_io_entry& e,
                            const absl::flat_hash_map<std::string, Generic_bind>& gbinds, const Lnast_node& actual,
                            std::string_view callee_name, const livehd::diag::Span& call_span, bool probe = false);
  // The declared facts of `name`, or, for a tuple_get temp (`child.o`, `t.f`,
  // which has no type of its own), of the field it was read from.
  std::optional<upass::decl_facts::Facts> operand_decl_facts(std::string_view name) const;

  bool maybe_specialize_template_call(const std::shared_ptr<Lnast>& callee, const Lnast_tree_io& io,
                                      const std::vector<Lnast_node>& param_val, const std::vector<bool>& param_set,
                                      std::size_t nbind, bool has_vararg, const std::vector<Lnast_node>& vararg_pos,
                                      const std::vector<std::pair<std::string, Lnast_node>>& vararg_named,
                                      const std::string& dst_name, const std::string& callee_name,
                                      const livehd::diag::Span&                             call_span,
                                      const absl::flat_hash_map<std::string, Generic_bind>& gbinds);
  // Deep-copy `tmpl` verbatim into a fresh (TreeIO-backed) Lnast named
  // `mangled`, then inject a concrete prim_type_int / named-type child into
  // each untyped fixed input port per `inject`. Clears the template flag and
  // copies the lambda kind. pass_upass re-SSAs the result. When `vname` is
  // non-empty the template has a `...vname` var-arg boundary: its io port is
  // replaced by the `vports` concrete ports and the body is prefixed with a
  // `vname = (port…)` reconstruction so the existing tuple/for machinery lowers.
  // `out_inject` mirrors `inject` for the OUTPUT ports (`-> (r:T)` with T
  // bound). Both are indexed like `tmpl->io_meta()` (inline tuple ports
  // flattened to dotted leaves); a tree port is matched to its slot by name.
  // `type_subst` rewrites body `ref <generic>` type slots to the
  // bound concrete type during the copy (declare/type_spec `:T` uses — SSA
  // strips io type refs, so only body slots need it).
  std::shared_ptr<Lnast> clone_template_specialized(const std::shared_ptr<Lnast>& tmpl, const std::string& mangled,
                                                    const std::vector<Spec_port>& inject, const std::vector<Spec_port>& vports,
                                                    const std::string& vname, const std::vector<Spec_port>& out_inject,
                                                    const absl::flat_hash_map<std::string, Generic_bind>& type_subst);
  // 2f-generic_port_width — fold a deferred port bound of `tmpl` (a `%tmp`
  // defined by its straight-line body prologue, a generic name, or a literal)
  // under `binds`. nullopt = not a compile-time integer; `unbound` (when set)
  // receives the first generic of `tmpl` the bound needs but `binds` lacks.
  [[nodiscard]] static std::optional<Dlop> fold_template_bound(const std::shared_ptr<Lnast>& tmpl, std::string_view text,
                                                               const absl::flat_hash_map<std::string, Generic_bind>& binds,
                                                               int depth = 0, std::string* unbound = nullptr);
  // Concrete port type of an io entry with has_deferred_bound(); a side that
  // does not fold is a fatal `type-bound-not-comptime`.
  [[nodiscard]] Spec_port                  deferred_port_type(const std::shared_ptr<Lnast>& tmpl, const Lnast_io_entry& e,
                                                              const absl::flat_hash_map<std::string, Generic_bind>& binds,
                                                              const std::string& callee_name, const livehd::diag::Span& span);
  void copy_subtree_into(const std::shared_ptr<Lnast>& src, const Lnast_nid& src_nid, const std::shared_ptr<Lnast>& dst,
                         const Lnast_nid& dst_parent, const absl::flat_hash_map<std::string, Generic_bind>* type_subst = nullptr);
  // Emit a `func_call(dst, callee, [name=], port=val…)` with NAMED actuals into a
  // scratch tree and re-walk it, so tolg wires the Sub instance by port name.
  // Used by template specialization and by the concrete pipe/mod decline to
  // canonicalize any unnamed (type/exception-resolved) actual to named form.
  void emit_named_instance_call(const std::string& dst, const std::string& callee_ref, const std::string& inst_name,
                                const std::vector<std::pair<std::string, Lnast_node>>& actuals);
  // Specialized-module names this runner already minted this run (avoid
  // re-cloning the same signature within one tree; cross-tree dedup is by name
  // in pass_upass's queue drain).
  absl::flat_hash_set<std::string> specialized_emitted_;
  // Set only while emit_named_instance_call RE-WALKS the call it just emitted
  // for an IDENTITY specialization. That call names the template's own module
  // name (the clone kept it), so without this the re-walk would resolve the
  // callee back to the template, specialize again, emit again — unbounded
  // recursion (a stack overflow, not a diagnostic). For every OTHER
  // specialization the mangled name is what stops the re-walk: it resolves to
  // nothing until the queue folds the clone in. The re-walked tree is a single
  // func_call with named-actual stores, so suppressing the whole subtree is
  // exactly the one call.
  bool                             in_identity_respecialize_ = false;

  // ── init constructor hook ───────────────────────────────────────────────
  // One named argument of a synthesized constructor call (positional when
  // `key` is empty).
  struct Ctor_arg {
    std::string key;
    Lnast_node  node{Lnast_node::create_invalid()};
    // An explicit `T(x)` call's positional bare variable: its source spelling
    // (Actual::src_name/src_base), so the init call binds it by name.
    std::string src_name = {};
    std::string src_base = {};
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
  bool                     try_init_construction();
  // Same recognition for the explicit call form `x = T(args…)`: called from
  // the func_call case when try_inline_func_call declined. The callee name
  // is not a registry function but a type bundle with `init`.
  bool                     try_construct_call();
  // Shared splice: emit `receiver = <defaults of tn>` (when tn non-empty),
  // then synthesize and walk `init_fn(__ufcs_arg=receiver, args…)` inside an
  // init-construction window (attributes tally paused, ref-self-on-const
  // admitted, construction args bind positionally in tuple order).
  void                     splice_init_call(const std::string& receiver, const std::string& tn, const std::string& init_fn,
                                            const std::vector<Ctor_arg>& args);
  // Selects the first init overload (tuple order) whose non-self formals
  // match the args (by count; named keys must all exist). Empty when none.
  std::string              select_init_overload(const std::vector<std::string>& candidates, const std::vector<Ctor_arg>& args,
                                                const livehd::diag::Span* span = nullptr, std::string_view label = {});
  // Collects the init candidates recorded on type bundle `tn`: the single
  // `init` function-name string or the `init.N` overload list, tuple order.
  std::vector<std::string> init_candidates_of(std::string_view tn);

  // ── overload-gathering call dispatch (2f-overload) ───────────────────────
  // One gathered/parsed actual argument of a call. Hoisted out of
  // try_inline_func_call so the overload pre-resolution and the normal bind
  // path gather identically (gather_actuals).
  struct Actual {
    bool        is_named{false};
    bool        is_ref_pass{false};  // `f(ref x)` — positional, exempt from naming rules
    std::string key;
    Lnast_node  node{Lnast_node::create_invalid()};
    std::string func_name;  // non-empty: this actual is a function value (closure)
    // A positional bare-variable actual's SOURCE name (never the inline frame's
    // `inl<N>_x`), and the variable it versions when that name is a
    // compiler-minted SSA version (else empty): naming exception 2 binds it to
    // the parameter it spells, the exact name first, even when `node` carries
    // the renamed form (a param of the inlined caller).
    std::string src_name;
    std::string src_base;
  };
  // The source spelling of the bare-variable actual at the cursor, as
  // Actual::{src_name, src_base}; both empty for a const or a tmp.
  std::pair<std::string, std::string> source_var_at_cursor() const;
  // Walk the func_call actuals (cursor MUST be on the callee ref — the call's
  // 2nd child). Fills `actuals` (positional / named / ref-pass) and the
  // explicit `<…>` generic refs, honoring the UFCS-receiver drop. Saves and
  // restores the cursor. Returns false on a malformed call shape (the caller
  // then declines the inline).
  bool gather_actuals(bool drop_ufcs_receiver, std::vector<Actual>& actuals, std::vector<Generic_actual>& explicit_generics);
  // Shared actual→param binding ladder (06-functions.md §"Argument naming"),
  // used by BOTH the real inline bind (try_inline_func_call, commit=true) and the
  // overload probe (signature_matches, commit=false) so the two can never drift.
  // Fills param_val/param_set/param_func (all sized to io.inputs) and the var-arg
  // leftovers. commit=true fails fatally via fcall_arg_fail and lets a whole tuple
  // actual fall through to the single-param exception (whole-tuple→scalar);
  // commit=false returns false on the first rejection and forbids a tuple actual
  // from binding a lone scalar param (the overload-discrimination rule). Returns
  // true on a full successful bind.
  // out_tuple_expanded (optional): set true when a tuple ACTUAL was expanded
  // field-by-field into flattened `<prefix>.<leaf>` params (named `p=t` or
  // positional `f(t)`). A Sub-bound callee (mod/pipe, or a comb kept as an
  // instance under inline=false) must then re-emit the call with the dotted
  // NAMED binding — the source spelling names no leaf port tolg could wire.
  bool bind_call_actuals(const Lnast_tree_io& io, const std::vector<Actual>& actuals, bool commit, std::string_view callee_name,
                         const livehd::diag::Span& call_span, std::vector<Lnast_node>& param_val, std::vector<bool>& param_set,
                         std::vector<std::string>& param_func, std::vector<Lnast_node>& vararg_pos,
                         std::vector<std::pair<std::string, Lnast_node>>& vararg_named, bool* out_tuple_expanded = nullptr);
  // docs 04b: a `mod`/`pipe` child with no declared `Clock` (`Reset`) input
  // has one MINTED (`clock`/`reset`), which a caller may bind by that name
  // (a two-clock caller must). bind_call_actuals accepts such a named actual
  // only while bind_minted_ok_ is set (the callee is a mod/pipe), collecting
  // it into bind_minted_actuals_ for the Sub call tolg wires.
  bool bind_minted_ok_ = false;
  std::vector<std::pair<std::string, Lnast_node>> bind_minted_actuals_;
  // A gathered lambda set `const add = [f1, f2]` folds (constprop) to a bundle
  // of qualified function-name strings under numeric keys "0","1",… — the same
  // shape `init` overloads use. Returns those names in tuple order (only the
  // entries that resolve to a registry lambda); empty when `name` is not such a
  // set. try_inline_func_call rewrites the callee to the FIRST candidate whose
  // signature accepts the call (signature_matches), then proceeds as a normal
  // single-callee inline; no candidate → a fatal `fcall-no-overload` diag.
  std::vector<std::string>                        overload_candidates_of(std::string_view name);
  // True iff callee signature `io` accepts `actuals` — naming + arity + scalar
  // kind/range fit. The synchronous overload-dispatch predicate: a non-fatal
  // mirror of try_inline_func_call's bind loop plus a per-arg kind/range
  // pre-filter (a typed-param MISMATCH is otherwise only caught downstream, so
  // it must be re-derived here to choose among candidates). Used ONLY to pick
  // among gathered candidates; the winner still runs the full bind path, which
  // remains the authority for diagnostics.
  bool                                            signature_matches(const Lnast_tree_io& io, const std::vector<Actual>& actuals,
                                                                    const std::shared_ptr<Lnast>& callee = nullptr);
  // The RETURN half of the overload-dispatch callability test (so "can handle"
  // means the WHOLE `c = f(b)` would be valid, not just the call side): true iff
  // candidate `io`'s OUTPUTS can bind to how the call's result is consumed at the
  // call site, mirroring 06-functions.md §"Binding return values". `req_fields`
  // is the set of field names a destructure picks off the result (`(p1,p2)=f()`
  // → {p1,p2}); `whole_used` is set when the result is bound/used as one value
  // (`c=f()`). Without this, an input-compatible but return-incompatible
  // candidate is silently selected — e.g. a single-output lambda destructured by
  // name leaves `tuple_get(scalar,'p1')` to fold to garbage. Permissive (returns
  // true) when the result is dropped or the output shape is one it cannot model,
  // so a too-strict skip surfaces as a clean no-overload, never a wrong dispatch.
  bool return_matches(const Lnast_tree_io& io, const absl::flat_hash_set<std::string>& req_fields, bool whole_used,
                      bool scalar_destination);
  // Scan the func_call's following statements (cursor restored to `fcall_cursor`)
  // to learn how its result `dst_name` is consumed: each `tuple_get(dst_name,
  // 'field')` adds to `req_fields`; any OTHER reference to `dst_name` sets
  // `whole_used`. Cursor-neutral (saves/restores). Feeds return_matches.
  bool collect_return_consumption(const upass::Lnast_manager::Cursor_state& fcall_cursor, std::string_view dst_name,
                                  absl::flat_hash_set<std::string>& req_fields, bool& whole_used,
                                  bool* scalar_destination = nullptr, std::string_view stop_field = {});
  // >0 while a synthesized constructor call is being spliced.
  int  init_construction_depth_ = 0;
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
  bool                             ctor_call_pending_ = false;
  // Armed with ctor_call_pending_: the constructor args' source spellings, in
  // order. The synthesized call's refs carry the caller's renamed text, so the
  // ctor bind takes Ctor_arg::src_name/src_base from here instead.
  std::vector<Ctor_arg>            ctor_call_args_;

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

  // Emits `attr_set(target, key, value)` straight into staging (literal names,
  // no frame rename). Used by the inliner to stamp a `__hier` instance-path
  // prefix onto each reg/mem declared in an inlined comb body, so tolg can name
  // the resulting flop hierarchically (`pipeB_ex_mem.reg_x`) — matching what a
  // non-inlined Sub instance would report via get_hier_name().
  void emit_inline_attr(const std::string& target, std::string_view key, const std::string& value);

  // Same, but from an explicit prim_type_int `(max,min)` range — used to
  // re-type an untyped param from the actual's declared range (preserves
  // non-pow2 / partial ranges exactly, where bits alone would round up the
  // envelope). An unset bound emits the `nil` (unbounded) child. No-op when
  // both bounds are unset.
  void emit_inline_typespec_range(const std::string& name, const std::optional<Dlop>& range_max,
                                  const std::optional<Dlop>& range_min);

  // A `bool`-bound generic param/output: emit a childless prim_type_bool
  // typespec. A bool bind also carries max=1/min=0, so callers must test the
  // bool kind BEFORE the (max||min) range branch.
  void emit_inline_typespec_bool(const std::string& name);

  // Emits `dst = sext(src, sign_bit)` through the walk so constprop folds it.
  // Mirrors the deleted evaluator's adjust_for_type: a signed output's raw
  // value (e.g. a get_mask bit-slice 0b1110 = 14) must be reinterpreted as its
  // declared width (s4 → -2). sign_bit is the top bit index (bits-1).
  void emit_inline_sext(const std::string& dst, const std::string& src, int sign_bit);

  // Pack aggregate operands and resolve selection bounds before dispatch.
  void process_bit_selection();
  void process_bit_update();

  // `~x` (user ruling 26): an UNSIGNED-typed operand of known width N flips
  // its N bits, so the node is re-issued in its typed LNAST form
  // `bit_not(dst, x, N)`; a signed or untyped one stays `-x - 1`. The one
  // decision point every consumer reads. See the definition.
  void dispatch_bit_not();
  // A bitwise and/or/xor, recording whether its result is unsigned-typed (every
  // operand is: the widest width), so nand/nor/xnor `~(a & b)` follow the rule.
  void dispatch_bitwise(upass::Push_method fn);
  // The declared integer type of a value (user rulings 26, 38, 44): its sign,
  // its width and, when the declaration pins them, its exact bounds.
  struct Int_type {
    bool     is_signed{false};
    uint32_t bits{0};
    Dlop     max;  // invalid: only the width is known
    Dlop     min;
  };
  // The integer type `name` has by its OWN declaration or producer, or nullopt
  // for an untyped value (see the definition).
  [[nodiscard]] std::optional<Int_type>                         typed_int_of(std::string_view name) const;
  // {dst, type} when the func_call under the cursor is a typecast whose result
  // has a declared integer type (`uN(x)`/`sN(x)`, `unsigned(x)`/`signed(x)` of a
  // typed x), else nullopt.
  [[nodiscard]] std::optional<std::pair<std::string, Int_type>> typed_cast_result() const;
  // User ruling 2026-09-28 (38): an untyped `const` alias of a typed value
  // (`const x = a`) takes the value's declared type.
  void                                                          inherit_alias_type(const std::string& dst, const std::string& src);
  // {dst, W} for the tuple_get under the cursor: W is the element width when
  // it reads ONE element of a declared one-dimensional unsigned array
  // (`arr[i]` of `[N]uW`, a reg/memory array, an array port), else 0.
  [[nodiscard]] std::pair<std::string, uint32_t>                array_elem_read_bits() const;
  // Result temp -> its integer type: a `~` of a typed operand, a bitwise
  // and/or/xor over typed operands (unsigned when all are), a sized or
  // sign cast (none of them has a declared type of its own when it folds at
  // comptime), a one-bit select at a runtime or named position, an element
  // read of an unsigned array.
  absl::flat_hash_map<std::string, Int_type>                    typed_expr_types_;
  // Names typed by their OWN declaration or type_spec (bake_decl_pre_step, a
  // bit-select's envelope stamp), or an untyped `const` alias of a typed value
  // (ruling 38, inherit_alias_type) -- not other names whose envelope rode in
  // on a value (`mut t = x#[..]` leaves `t` untyped). An untyped declaration of
  // the name drops it again: a sibling scope may reuse a name (no shadowing, so
  // two live bindings never share one).
  absl::flat_hash_set<std::string>                              declared_typed_;

  bool try_lower_tuple_spread();
  void emit_inline_get_mask(const std::string& dst, const Lnast_node& value, int lo, int hi);

  // Compile-time difference of the explicit half-open endpoints, even when
  // their shared base is a runtime value.
  std::optional<int64_t> runtime_range_width(const Lnast_nid& stmt);

  // Runtime `bool(x)` == `(x != 0)`: emit `ne(dst, value, 0)` so the passes run
  // (typecheck stamps the boolean result kind).
  void emit_inline_to_bool(const std::string& dst, const Lnast_node& value);

  // Emits `dst = op(operands…)` through the walk (scratch tree → process_lnast)
  // so constprop observes and folds it — unlike emit_staging_op which writes the
  // node straight to staging unfolded. `op` may be n-ary (eq, log_or, store).
  // Used by lower_in to build the `a==b[i]` comparisons and their `or`.
  void emit_inline_op(Lnast_ntype::Lnast_ntype_int op, const std::string& dst, const std::vector<Lnast_node>& operands);

  // Append `op(ref(dst), operands...)` DIRECTLY into the staging tree (no
  // process_lnast / pass dispatch). Used by the runtime `sat` lowering for the
  // seed store / gt / lt / sext: emitting CONTROL FLOW (the clamp `if`) through
  // process_lnast would re-enter the if-arm scope machinery from inside the
  // func_call walk and corrupt the symbol-table scope chain (an infinite loop
  // in find_decl_scope_read). Building the nodes straight into staging — like
  // emit_subtree_verbatim — sidesteps that; tolg lowers them by structure.
  void emit_staging_op(Lnast_ntype::Lnast_ntype_int op, const std::string& dst, const std::vector<Lnast_node>& operands);

  // Append `if (cond) { dst = value }` (a single-arm if) directly into staging.
  // With a prior staging `dst = <seed>` this is exactly one Mux(sel=cond,
  // false=seed, true=value) in tolg — the only way to get a mux (no select node).
  void emit_staging_guarded_store(const std::string& cond, const std::string& dst, const Lnast_node& value);

  // 2f-mem_comptime_init — when a reg-array declare's initializer is a ref to a
  // fully-comptime bundle (built by a loop, not a tuple literal), tolg can't
  // resolve it (it only sees literal tuple_adds). Cursor is at the declare
  // node. If this is such a case, materialize the bundle's flattened comptime
  // values into NESTED positional tuple_add literals (so tolg's tuple_recs_
  // resolves them) and re-emit the declare with its init pointed at the
  // materialized outer temp (and, for the inferred form, a synthesized
  // comp_type_array type). Returns true if it handled+emitted the declare (the
  // caller skips the verbatim emit); false to fall through unchanged.
  bool try_materialize_array_init();

  // Emits `dst = (c0, c1, …)` as a POSITIONAL tuple_add (no field-key store
  // wrappers — tolg's memory-init path requires `named` empty). Children are
  // const leaves or refs to nested tuples.
  void emit_inline_positional_tuple(const std::string& dst, const std::vector<Lnast_node>& children);

  // Recursively emit the nested tuple_add literals for a row-major flat value
  // vector shaped by `dims`. Returns the name of the outermost tuple temp.
  std::string materialize_array_literal(const std::vector<int64_t>& dims, size_t level, const std::vector<Dlop>& flat,
                                        size_t start);

  // 2f-nil_diag — an if/match/while condition that folds to a nil (Type::Nil)
  // value is an illegal use of nil in a conditional: emit a compile error.
  // `which` names the construct ("if"/"match"/"while") for the message. The
  // cursor must be on the condition node (its span is used). Template bodies
  // fold nil placeholders for unbound params, so they are exempt.
  void report_cond_nil(std::string_view which);

  // Monotonic per-run counter giving each inline call site a unique rename
  // salt. Cross-pass idempotence is a documented follow-up. Also used per
  // comptime loop iteration (unroll_for/unroll_while) so each iteration's
  // re-walk gets a fresh tmp-rename namespace + block-scope id.
  uint32_t                      inline_seq_{0};
  // Numbers the `inl<N>_` rename TAG of each inline call site, apart from the
  // salts: the tag names hardware (an inlined array local becomes memory
  // `inl<N>_m`), so it must not depend on how many loop iterations an earlier
  // callee's body took to unroll -- a callee still unconverged (a cold compile)
  // unrolls its loops inside the splice, a converged one (restored warm) no
  // longer has any.
  uint32_t                      inline_tag_seq_{0};
  // `<N>`s of the `inl<N>_` prefixes the walked unit's own source already
  // spells (a rolled loop retained in an inlined caller's namespace lifts
  // ports like `inl1_a`; re-read writer output names them too). A splice
  // tagged `inl1_` would mint `inl1_a` for its callee's `a` and collide, so
  // such tags are skipped. Scanned lazily once per unit (inline_tags_unit_).
  absl::flat_hash_set<uint32_t> inline_tags_taken_;
  const Lnast*                  inline_tags_unit_{nullptr};
  // Comptime loop unroll state. loop_break_hit_ is set by a `func_break`
  // reached on a comptime-taken path during a loop body re-walk; the unroller
  // checks it after each iteration and stops. loop_depth_ counts active
  // (nested) unrolls so the fuel/depth guard bounds non-terminating loops the
  // same way recursion is bounded.
  bool                          loop_break_hit_{false};
  // Set by a `func_continue` reached on a comptime-taken path: like break it
  // stops the rest of the CURRENT iteration's body walk, but (unlike break) the
  // unroller does NOT stop — it proceeds to the next iteration. Reset at the
  // start of each iteration (walk_loop_iteration).
  bool                          loop_continue_hit_{false};
  int                           loop_depth_{0};
  // A deferred type bound the bake folded IN PLACE on its source node while a
  // loop body is walked, with the ref it held. The unroller re-walks the SAME
  // body nodes every iteration, so walk_loop_iteration restores its own
  // iteration's entries once that iteration is emitted: otherwise every later
  // iteration reads the first one's value (`const t:unsigned(bits=i)`, the lane
  // of `wrap a#[0..+i] = ..`).
  struct Loop_baked_ref {
    std::shared_ptr<Lnast> ln;
    Lnast_nid              nid;
    std::string            ref;
  };
  std::vector<Loop_baked_ref>                   loop_baked_refs_;
  // 0-based iteration ordinal of every unroll currently in flight (outermost
  // first), maintained alongside loop_depth_. `loop_inst_suffix()` renders it as
  // the `__li<ordinal>` tag stamped on the instances a body copy creates: one
  // source `mod` call inside `for i in 0..<8` is EIGHT physical instances, and
  // without the tag all eight carry the same source-derived name. cgen would
  // then de-collide them itself as `x`, `x_cgen1`, … — a spelling that neither
  // starts at 0 nor says which iteration an instance came from, and that
  // renumbers as soon as an unrelated instance is added.
  std::vector<uint64_t>                         loop_iter_ordinals_;
  // Per-parent-depth bases for successive source loops. This mirrors HHDS
  // format_occurrence_path(): a second loop in one parent continues after the
  // first site's count, while a newly entered nested parent restarts at zero.
  std::vector<uint64_t>                         next_loop_ordinal_bases_{0};
  std::shared_ptr<hhds::Forest>                 scratch_forest_;
  // Callee bodies currently being spliced (innermost last). Re-entering one
  // means recursion — bailed to the evaluator until Phase D adds fuel.
  std::vector<const Lnast*>                     active_inline_callees_;
  // Hierarchical instance-name prefix stack (innermost last), pushed per
  // inlined comb. The level is the call-site `name=` (if any) else the dst
  // variable name — mirroring tolg's Sub-instance naming so a reg's
  // hierarchical name is the same whether its comb is inlined or kept as a Sub.
  // join('.') of this stack is stamped as the `__hier` attr on inlined regs/mems.
  std::vector<std::string>                      hier_prefix_stack_;
  // Set by gather_actuals when a call carries the reserved `__inst_name` actual
  // (`alu::[name=X](…)`); consumed by try_inline as the hier-prefix level.
  std::string                                   gathered_inst_name_;
  // compile.upass.inline (default true). When false, a DIRECT by-name call to a
  // fully-typed pure-dataflow `comb` is NOT spliced — it is left as a func_call
  // so tolg lowers it to a Sub module instance (preserving the comb boundary for
  // debug/optimization). Overload/method/closure dispatch (no Sub form) always
  // inlines. Read from options["inline"] in the constructor.
  // (The recursion / inlinable / placeholder / sub-convertible sets these used
  // to sit beside now live in the shared uPass_function_registry — see reg().)
  bool                                          inlining_enabled_ = true;
  // Higher-order / closure support: maps a function-valued param's RAW name
  // (as read in the callee body, e.g. `f` in `r = f(x)`) to the registry
  // function it is bound to at this call site (e.g. `step_up`). Saved/restored
  // around each body walk so nested frames don't clobber each other. Consulted
  // by try_inline_func_call when a callee name isn't itself a registry entry.
  absl::flat_hash_map<std::string, std::string> func_param_bindings_;
  // Type-valued generic used as a constructor/cast in the body (`T(a)` with T
  // bound to `u8`): maps the generic's RAW name (as read in the body) to the
  // concrete cast token (`u8`/`s4`/`bool`) so try_lower_typecast reclassifies
  // `T(a)` as that scalar cast (todo 3g A). Saved/restored around each body
  // walk, like func_param_bindings_.
  absl::flat_hash_map<std::string, std::string> generic_cast_binds_;
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

  // The sole loop representation switch. False preserves compact loops;
  // true requests per-iteration source expansion for benchmarking.
  bool     unroll_requested_{false};
  // Per-unit counter making each lifted definition's name unique (the dedup in
  // specialized_emitted_ / pass_upass is BY NAME and silently DROPS a second
  // definition that collides).
  uint64_t roll_seq_{0};

  // Uncertain-scope count at the current loop iteration's entry. A `break`
  // reached with MORE uncertain scopes active than this is guarded by a runtime
  // condition the unroller cannot resolve; the same count means its guard is
  // comptime and unrolling handles it (a comptime break nested inside a runtime
  // `if` around the whole loop must NOT be rejected).
  std::size_t loop_uncertain_base_{0};

  // ── loop rolling (todo_loop_cond_sub.md M4) ─────────────────────────
  //
  // What lifting a loop body needs to know. Free variables of the body split
  // into three classes; anything declared inside the body is local and gets no
  // boundary port:
  //   index      — the iteration variable, one input port
  //   invariant  — read, never written: one input port
  //   carry      — may read an earlier value or may not write on every path: an
  //                input/output pair plus a literal descriptor self-edge
  //   final      — must-write, whole-value result with no incoming value: one
  //                output read from the last occurrence
  struct Loop_roll_plan {
    int64_t     first = 0;
    int64_t     step  = 1;
    uint64_t    count = 0;
    std::string ivar;                      // index port name == the iteration variable
    std::string mangled;                   // lifted definition name
    std::string inst;                      // instance name for the replicated Sub
    bool        has_loop_control = false;  // body owns break/continue and needs activation roles

    absl::flat_hash_map<std::string, std::string>                 actual_names;  // body-local name -> enclosing binding
    // Boundary names the lifted definition spells differently (filled by
    // lift_loop_body; see emit_rolled_loop_call's port_of): a body-local
    // private SSA version.
    absl::flat_hash_map<std::string, std::string>                 port_names;
    std::vector<std::pair<std::string, Dlop>>                     constants;  // copied values, never boundary ports
    std::vector<std::string>                                      invariants;
    std::vector<std::string>                                      carries;
    absl::flat_hash_set<std::string>                              registers;  // separate invariant Q and carried D
    std::vector<std::string>                                      finals;     // must-written, no incoming ordinal-0 value
    absl::flat_hash_map<std::string, Spec_port>                   types;      // boundary name -> declared type
    // Carry -> {min, max} of the values that enter an iteration (seeds the
    // lifted body's carry-in range; see plan_loop_roll).
    absl::flat_hash_map<std::string, std::pair<int64_t, int64_t>> carry_in_ranges;
    // Invariant -> {min, max} of the value it holds where the loop starts (an
    // enclosing rolled loop's index is only a signed storage window otherwise).
    absl::flat_hash_map<std::string, std::pair<int64_t, int64_t>> invariant_ranges;
  };

  // Suffixes for the two compiler-owned ports a carry needs. The body is copied
  // VERBATIM, so a carry keeps its own name inside the body as an ordinary
  // `mut` local seeded from the input port and written back to the output port.
  static constexpr std::string_view kCarryInSuffix   = "__carry_in";
  static constexpr std::string_view kCarryOutSuffix  = "__carry_out";
  static constexpr std::string_view kCarryNextSuffix = "__carry_next";
  static constexpr std::string_view kLoopValid       = "__valid";
  static constexpr std::string_view kLoopExec        = "__loop_exec";
  static constexpr std::string_view kLoopNextActive  = "__next_active";

  // Analysis only: decides whether this range loop can roll and fills `out`.
  // Returns false (with a debug-log reason) to fall back to unrolling.
  bool plan_loop_roll(const Lnast_nid& body_stmts, const std::string& ivar, int64_t lo, int64_t hi, int64_t step,
                      Loop_roll_plan& out);
  // A call under `nid` writes a variable back through `ref`: a `ref x` actual,
  // or the receiver of a `ref self` method (or of a callee that does not
  // resolve here). The roll plan's carry analysis sees only stores.
  bool subtree_writes_through_ref(const Lnast& ln, const Lnast_nid& nid) const;

  // Builds the lifted definition: io ports from the plan, the body copied
  // verbatim between a carry-seeding prologue and a carry-writeback epilogue.
  // Records in plan.port_names every boundary it had to rename.
  std::shared_ptr<Lnast> lift_loop_body(const Lnast_nid& body_stmts, Loop_roll_plan& plan);

  // Emits one explicit rolled_for node containing the surviving source body
  // and the hidden Sub-call/result transport consumed by tolg.
  void emit_rolled_loop_call(const Loop_roll_plan& plan, const Lnast_nid& source_body);

  // What plan_loop_roll consults about ONE definition (a lambda, or the root)
  // for every loop in it, built once per definition instead of once per loop.
  // A name declared in several sibling scopes lands in every class it is
  // declared with, which keeps the planner conservative.
  using Name_deps = absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>>;
  struct Loop_scope_facts {
    absl::flat_hash_set<std::string>          wires;      // declared `wire`
    absl::flat_hash_set<std::string>          regs;       // declared `reg`
    absl::flat_hash_set<std::string>          arrays;     // declared with an array type (also via a `type` alias)
    absl::flat_hash_set<std::string>          comptimes;  // declared `comptime` (never a carry of a rolled body)
    // Straight-line def-use (name -> every name its writes read), per scope:
    // the definition's own statements, and the body of each enclosing loop.
    absl::flat_hash_map<Lnast_nid, Name_deps> deps;
  };
  Loop_scope_facts& loop_scope_facts(const Lnast_nid& definition);

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

  // loop_scope_facts' cache: source tree -> definition -> facts. `tree` pins
  // the tree so its address cannot be reused by another while cached.
  struct Loop_tree_facts {
    std::shared_ptr<Lnast>                           tree;
    absl::flat_hash_map<Lnast_nid, Loop_scope_facts> definitions;
  };
  absl::flat_hash_map<const Lnast*, Loop_tree_facts> loop_tree_facts_;

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
