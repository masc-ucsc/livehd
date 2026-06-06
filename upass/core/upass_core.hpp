//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stack>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "absl/container/inlined_vector.h"
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

enum class Emit_kind { emit, drop_subtree };

struct Emit_decision {
  Emit_kind kind{Emit_kind::emit};

  static constexpr Emit_decision emit_node() { return Emit_decision{Emit_kind::emit}; }
  static constexpr Emit_decision drop() { return Emit_decision{Emit_kind::drop_subtree}; }
};

// ── Step E: Operand vector + Vote dispatch (docs/upass_redesign.md §E) ──
//
// Runtime_trivial marks an operand that has no usable comptime value at
// this site (a ref whose Bundle is empty or whose value bits are unknown).
// Const carries a parsed pyrope literal or a folded value. shared_ptr<Bundle>
// hands the pass a pointer to the per-name Bundle the runner already
// resolved — passes read its `fields["0"]` for scalars, or its tuple
// structure / attrs for richer queries.
struct Runtime_trivial {};

using Operand     = std::variant<Runtime_trivial, Const, std::shared_ptr<Bundle>>;
using Operand_vec = absl::InlinedVector<Operand, 4>;

enum class Vote_kind : uint8_t {
  keep,     // emit source op verbatim (with operand folding)
  drop,     // emit nothing
  toconst,  // emit nothing; LHS Bundle's scalar holds the folded value
  update,   // emit a rewritten shape (carried in update_payload)
};

// Step K — pending_import poison marker.
//
// Presence-only Bundle attribute (Bundle::has_attr / Bundle::set_attr) used
// to mark every name that depends on an unresolved import. Origination,
// propagation, gating, and clearing rules live in:
//   - runner (origination on `import` op, gating skip for non-constprop)
//   - bundle_pre (propagation through operand bundles)
//   - finisher (defer verifier/assert dest walk while any Bundle is marked)
// All four sites read/write this name; the central definition keeps the
// string consistent.
inline constexpr std::string_view k_pending_import_attr = "pending_import";

struct Vote {
  Vote_kind            kind{Vote_kind::keep};
  // Folded value for kind == toconst (committed to dst->fields["0"] by
  // the runner during vote resolution). Ignored otherwise.
  Const                toconst_value;
  // Placeholder for the update shape (Step E doesn't yet ship a concrete
  // representation; populated by passes that need rewrites — e.g. fcall
  // inlining in Step L).
  std::vector<uint8_t> update_payload;

  static Vote keep() { return {}; }
  static Vote drop() { return {Vote_kind::drop, {}, {}}; }
  static Vote toconst(const Const& v) { return {Vote_kind::toconst, v, {}}; }
  static Vote update(std::vector<uint8_t> payload) { return {Vote_kind::update, {}, std::move(payload)}; }
};

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
  virtual std::optional<Const> fold_ref(std::string_view /*name*/) { return std::nullopt; }

  // Perf hint to the runner. Overriding passes return true so the runner
  // can skip them in the hot try_fold_ref / any_pass_drops loops. Default
  // false matches the default no-op implementations above; passes that
  // override fold_ref / classify_statement must also override these.
  virtual bool overrides_fold_ref() const { return false; }
  virtual bool overrides_classify_statement() const { return false; }

  // ── Shared symbol-table read access (1i Phase E campaign) ────────────────
  // The runner's comb-call inliner can introspect a scalar via fold_ref, but
  // some inlinable shapes need more of the symbol-table state than a scalar:
  //   - tuple actuals / tuple params  → the bundle fields behind a ref
  //   - method dispatch / setter-init → the declared typename of a var
  // A pass that owns that state (constprop) overrides these so the runner can
  // read it without the full shared-ST migration. provide_bundle_fields
  // returns the flat (field → comptime Const) entries of a bundle, or nullopt
  // if `name` is not a known bundle. provide_typename returns the var's
  // declared typename, or "" if none.
  virtual std::optional<std::vector<std::pair<std::string, Const>>> provide_bundle_fields(std::string_view /*name*/) {
    return std::nullopt;
  }
  virtual std::string provide_typename(std::string_view /*name*/) { return {}; }

  // Declared scalar (integer) type of a variable, as its authoritative
  // prim_type_int `(max,min)` range. Exposed so the comb-call inliner can
  // propagate an *untyped* parameter's type from the actual argument at the
  // call site: `comb reverse(x) { … x.[bits] … }` invoked as `reverse(x0:u6)`
  // must see `x:u6` inside the inlined body, because `.[bits]`/`.[max]`/
  // `.[min]` are declared-type-driven (not value-driven) — an untyped param
  // bound only to a value reads nil for all three. Either bound may be unset
  // (unbounded). Returns nullopt when `name` has no concrete declared integer
  // type (untyped / `:int` unbounded / non-numeric). See bitreverse.prp.
  struct Decl_scalar_type {
    std::optional<Const> range_max;
    std::optional<Const> range_min;
  };
  virtual std::optional<Decl_scalar_type> provide_decl_type(std::string_view /*name*/) { return std::nullopt; }

  // Task 1k — declared type (scalar kind + optional integer range) of a
  // DOTTED field path such as `t1.a` (per-field types of a type/tuple bundle
  // live on tuple_get tmps in the attributes pass; lookup_type_info chases
  // the alias chain). The inliner's typed-self `does`-check uses this to
  // compare the declared self type's fields against the receiver's. Returns
  // nullopt when no declared type is recorded for the path.
  struct Field_decl_type {
    Io_kind              kind{Io_kind::none};
    std::optional<Const> range_max;
    std::optional<Const> range_min;
  };
  virtual std::optional<Field_decl_type> provide_field_type(std::string_view /*name*/) { return std::nullopt; }

  // Task 1k — declared storage class of a variable. The inliner uses this to
  // reject a `const` or `type` binding as a `ref` actual (incl. the UFCS
  // receiver of a `ref self` method): a ref param writes back, so the actual
  // must be a mut value. `unknown` = no declaration seen (temps, expressions).
  enum class Decl_storage : uint8_t { unknown, mut_storage, const_storage, reg_storage, await_storage, type_storage };
  virtual Decl_storage provide_decl_storage(std::string_view /*name*/) { return Decl_storage::unknown; }

  // Folded `(start, end_inclusive)` bounds of a `range` tmp (the iterable of a
  // comptime `for i in lo..hi` loop), exposed so the runner's loop unroller can
  // iterate. nullopt when `name` is not a known range. A bound that has not
  // folded to a concrete integer is returned as-is (the runner checks).
  virtual std::optional<std::pair<Const, Const>> provide_range(std::string_view /*name*/) { return std::nullopt; }
  virtual bool                                   overrides_shared_st() const { return false; }

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

  // Runner-owned symbol table (step C of upass redesign). Set once at
  // construction. Passes that migrate to the shared symbol table read/write
  // through this pointer; passes that still use private state ignore it.
  void          set_runner_symbol_table(Symbol_table* st) { runner_st = st; }
  Symbol_table* get_runner_symbol_table() const { return runner_st; }

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

  // ── Step E new dispatch surface (per docs/upass_redesign.md §E) ──
  //
  // The redesign replaces the per-op virtual void process_* family with a
  // single arith hook (for the 27 arithmetic/logic/comparison/bit ops)
  // plus shaped hooks for ops whose operand layout is distinctive. Each
  // hook receives a runner-resolved Operand_vec and a Bundle* for the LHS
  // (nullptr for ops with no LHS like `if`/`cassert`). The pass returns
  // a Vote; the runner reduces votes (drop > toconst > update > keep) and
  // emits accordingly.
  //
  // Migration is staged: today the runner still drives the old
  // `process_<op>()` virtuals. As each pass moves to the new surface, the
  // runner will route that op kind through process_arith / shaped hooks
  // and the pass will override these instead. Default implementations
  // return Vote::keep() so a pass that hasn't migrated is a no-op for
  // these hooks.
  virtual Vote process_arith(Lnast_ntype::Lnast_ntype_int /*kind*/, Bundle* /*dst*/, const Operand_vec& /*ops*/) {
    return Vote::keep();
  }
  virtual Vote process_attr_set_v(Bundle* /*dst*/, const Operand_vec& /*ops*/) { return Vote::keep(); }
  virtual Vote process_attr_get_v(Bundle* /*dst*/, const Operand_vec& /*ops*/) { return Vote::keep(); }
  virtual Vote process_tuple_add_v(Bundle* /*dst*/, const Operand_vec& /*ops*/) { return Vote::keep(); }
  virtual Vote process_tuple_concat_v(Bundle* /*dst*/, const Operand_vec& /*ops*/) { return Vote::keep(); }
  virtual Vote process_tuple_set_v(Bundle* /*dst*/, const Operand_vec& /*ops*/) { return Vote::keep(); }
  virtual Vote process_tuple_get_v(Bundle* /*dst*/, const Operand_vec& /*ops*/) { return Vote::keep(); }
  virtual Vote process_range_v(Bundle* /*dst*/, const Operand_vec& /*ops*/) { return Vote::keep(); }
  virtual Vote process_type_spec_v(Bundle* /*dst*/, const Operand_vec& /*ops*/) { return Vote::keep(); }
  virtual Vote process_if_v(const Operand_vec& /*ops*/) { return Vote::keep(); }
  virtual Vote process_cassert_v(const Operand_vec& /*ops*/) { return Vote::keep(); }
  virtual Vote process_func_def_v(Bundle* /*dst*/, const Operand_vec& /*ops*/) { return Vote::keep(); }
  virtual Vote process_func_call_v(Bundle* /*dst*/, const Operand_vec& /*ops*/) { return Vote::keep(); }

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
  PROCESS_NODE(bit_and)
  PROCESS_NODE(bit_or)
  PROCESS_NODE(bit_not)
  PROCESS_NODE(bit_xor)

  // Bitwidth Insensitive Reduce
  PROCESS_NODE(red_or)
  PROCESS_NODE(red_and)
  PROCESS_NODE(red_xor)
  PROCESS_NODE(popcount)

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
  // Nominal type identity (`p is xx`). Compares `p.[typename]` to the
  // string-name of the rhs ref.
  PROCESS_NODE(is)

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
  Fold_fn                         runner_fold_fn;
  Emit_at_fn                      runner_emit_at_fn;
  Symbol_table*                   runner_st{nullptr};

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
