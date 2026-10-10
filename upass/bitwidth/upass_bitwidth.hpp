//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "diag.hpp"
#include "hlop/dlop.hpp"
#include "lnast_range.hpp"
#include "symbol_table.hpp"
#include "upass_core.hpp"

// uPass_bitwidth — LNAST bitwidth / value-range analysis.
//
// Registers as a "bitwidth" uPass_plugin, but it is a standalone
// READ-ONLY FINALIZATION analysis: pass.upass runs it on its own
// (`uPass_runner(lm, {"bitwidth"})`) *after* the main opt runner + SSA, never
// interleaved with constprop, and discards the staging tree.
//
// MIGRATED to push-based dispatch. The per-variable facts live on the
// runner symbol table's bundles:
//   * value range  → Entry.bw_max / bw_min on the dst bundle (was range_map_),
//     written through to lnast->bw_meta() so lnast_to_lgraph and the LSP read
//     them (and so ranges persist across pass.upass invocations — reads fall
//     back to bw_meta for names not bound in this walk).
//   * declared envelope → Entry.decl_max / decl_min on the BASE name's
//     binding (the runner's declare pre-step bakes them; this pass also bakes
//     standalone type_spec targets in its own runner — was decl_envelope_).
// The does-not-fit check happens AT THE STORE/OP NODE: the diagnostic
// carries the node's own source span; an error-severity diag already fails
// the compile, so there is no end_run throw, no write_site_, and no
// pending_overflow_msg_.
//
// Key properties:
//   * It does NOT participate in const-folding (no fold_ref) — constprop owns
//     value folding; this removes the stale-narrow-range bug class.
//   * Boolean / comparison results always get the signed 1-bit range [-1, 0].
//   * Compiler temps (___*) participate normally.
//   * Arithmetic on unbounded ranges propagates unbounded conservatively.
struct uPass_bitwidth : public upass::uPass {
public:
  explicit uPass_bitwidth(std::shared_ptr<upass::Lnast_manager>& lm);
  ~uPass_bitwidth() override = default;

  using Vote = upass::Vote;

  // Assignment-shaped ops (LHS = op(rhs…)) — push form.
  Vote process_store(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  Vote process_plus(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_minus(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_mult(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_div(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_mod(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_shl(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_sra(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_bit_and(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_bit_or(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_bit_not(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_bit_xor(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_log_and(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_log_or(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_log_not(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_red_or(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_red_and(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_red_xor(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_popcount(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_ne(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_eq(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_lt(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_le(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_gt(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_ge(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_sext(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_get_mask(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_set_mask(std::string_view, Bundle&, upass::Src_span) override;
  // concat(msb, …, lsb) — the packed lanes: always [0, 2^sum(w_i) − 1], where
  // each w_i is the lane's DECLARED width (never its inferred range).
  Vote process_concat(std::string_view, Bundle&, upass::Src_span) override;

  // Nullary hooks (these nodes are not push-dispatched):
  // func_call — clear ranges through opaque `ref` actuals; note a wrap/sat
  // narrowing call's target (exempts its next write from the fit check).
  void process_func_call() override;
  // declare / type_spec — in this pass's standalone runner the declare bake
  // already wrote the envelope (Entry.decl_max/min); a bare type_spec target
  // may be unbound here (no constprop), so this pass ensures the binding. Both
  // record which names carry a DECLARED type (typed_names_).
  void process_declare() override;
  void process_type_spec() override;
  // tuple_get(dst, src, field) — a read whose value no derivation reaches:
  // a Sub instance output (its port's range) or an array element picked by a
  // runtime index (the element type).
  void process_tuple_get() override;
  // range(dst, lo, hi) — a runtime-position window whose ends share a base
  // (`x#[i..+4]`) still has a constant width (runtime_lane_bits_).
  void process_range() override;
  // Writes each inferred array's extent and element type into its staged
  // declaration (see inferred_arrays_).
  void walk_dest(const std::shared_ptr<Lnast>& dest) override;

  // ── If-arm value-range merge ──────────────────────────────────────────────
  // A var assigned inside an uncertain if-arm has its range REPLACED by each
  // arm (process_store/stamp use replace=true), and Symbol_table::leave_scope
  // invalidates only the comptime trivial, not the range. So a mux like
  // `mut b = if c { 1 } else { 0 }` would keep just the last arm's range
  // ([0,0]) — a value that is NOT constant reads as bw_min==bw_max, which is a
  // miscompile hazard (downstream treats it as a foldable constant). These
  // hooks range-union what each arm assigns and write the union back after the
  // if, so the range CONTAINS every reachable value.
  void notify_if_merge_begin() override;
  void notify_if_merge_end(bool all_paths_covered) override;
  void notify_uncertain_arm_begin() override;
  void notify_uncertain_arm_end() override;
  void notify_arm_guard(std::string_view cond, bool negated) override;

private:
  // Variables whose next write carries a wrap/sat policy. A wrap/sat call is
  // emitted immediately before the store to its `type` argument; consume the
  // exemption at that store so later ordinary writes are checked normally.
  // (Deliberately transient walk state, like the coalescer's pending set: a
  // one-shot per-write policy handshake, not a per-name fact.)
  absl::flat_hash_set<std::string> wrap_sat_exempt_;
  absl::flat_hash_set<std::string> index_range_reported_;  // check_index_in_size: one error per (array, location)

  // Names declared WITH a type in this walk (a typed declare, or a named
  // type_spec target such as an inlined comb's param/output). Only these (and
  // the unit's own typed ports) are fit-checked: an untyped name takes the
  // range of its value, even when an envelope rode in on that value.
  absl::flat_hash_set<std::string>              typed_names_;
  // The declared envelope of each typed tuple FIELD declared in this walk
  // (`p.x` of `mut p:(x:u4, …)`): see process_declare.
  absl::flat_hash_map<std::string, Lnast_range> declared_field_envs_;

  // Names holding a bit-pattern FORCE (`const K = 0ub10100`, and its copies):
  // their comptime value is not their range (see why_unbounded).
  absl::flat_hash_set<std::string> force_names_;

  // A bit write at a RUNTIME position still has a constant lane width when its
  // mask is one-hot (`1 << i`, a single bit) or a range whose ends differ by a
  // constant (`i..+4` lowers to `range(%r, i, (i + 4) - 1)`). affine_temps_
  // records `%t = x ± c` over compiler temps (one definition each) so the
  // range's ends reduce to one base; runtime_lane_bits_ maps the mask temp to
  // its width for the bit-range-overflow check in process_set_mask.
  struct Affine {
    std::string base;
    int64_t     offset{0};
  };
  absl::flat_hash_map<std::string, Affine>  affine_temps_;
  absl::flat_hash_map<std::string, int64_t> runtime_lane_bits_;
  void                                      note_affine(std::string_view dst_name, upass::Src_span src, int64_t sign);
  [[nodiscard]] Affine                      affine_of(std::string_view name) const;

  // ── If-arm merge state (see the notify_if_* hooks) ────────────────────────
  // One frame per active if node (nesting → a stack). arm_union[v] is the
  // range-union of what v was assigned across the arms seen so far;
  // arm_writes[v] counts how many uncertain arms wrote v; uncertain_arms is
  // the arm count. A var written in ALL arms of a fully-covered if drops its
  // pre-if value (the union is exactly the arms); otherwise the merge widens
  // to the declared/unbounded envelope (never a stale narrow range).
  struct If_merge_frame {
    absl::flat_hash_map<std::string, Lnast_range> arm_union;
    absl::flat_hash_map<std::string, int>         arm_writes;
    // The range each arm-written var had BEFORE this if (captured at its first
    // write inside it): the value a path that skips every write keeps, and the
    // value every later arm starts from. pre_if_wide: those that were too wide.
    absl::flat_hash_map<std::string, Lnast_range> pre_if;
    absl::flat_hash_set<std::string>              pre_if_wide;
    // The vars some arm left too wide / unknown-wide (its last write was): the
    // merge keeps them wide, since a later bounded arm only covers its path.
    absl::flat_hash_set<std::string>              arm_wide;
    // Compiler temps first written INSIDE this if (no value of their own
    // before it): an if/match EXPRESSION's result. A path that skips every arm
    // leaves such a temp with no value at all -- a `match` is exhaustive, so
    // that path does not exist -- hence the arms' union alone is its range.
    absl::flat_hash_set<std::string>              fresh_tmps;
    int                                           uncertain_arms = 0;
  };
  // Guarded subtraction (suggestions6 1.8): `if a > b { a - b }` is never
  // negative. cmp_facts_ maps a comparison result (or its log_not) to the
  // ordering it asserts when true, as `big > small` (strict) or `big >= small`.
  // Each uncertain arm pushes the orderings its guards establish
  // (notify_arm_guard collects them for the next arm); a write to either name
  // inside the arm retires the fact.
  struct Order_fact {
    std::string big;
    std::string small;
    bool        strict{false};
  };
  absl::flat_hash_map<std::string, Order_fact> cmp_facts_;
  std::vector<Order_fact>                      pending_guards_;
  std::vector<std::vector<Order_fact>>         guard_stack_;
  void note_order_fact(std::string_view dst, upass::Src_span src, bool swap, bool strict);
  [[nodiscard]] std::optional<bool> guard_orders(std::string_view big, std::string_view small) const;  // nullopt / strict?
  void retire_guards(std::string_view name);
  // Compiler temps whose range was dropped (clear_range): an absent bw_meta
  // entry then means "unknown", not "never written".
  absl::flat_hash_set<std::string> cleared_tmps_;
  std::vector<If_merge_frame> if_merge_stack_;
  // The current uncertain arm's writes (latest range per var wins). A stack so
  // a nested if's arms don't disturb the enclosing arm's write set.
  std::vector<absl::flat_hash_map<std::string, Lnast_range>> arm_write_stack_;

  // Record `r` as the latest value `name` takes in the current uncertain arm
  // (no-op outside one). Called from write_bw on every committed range.
  void record_arm_write(std::string_view name, const Lnast_range& r);
  // Before a write inside an uncertain arm: remember, for every enclosing if
  // that has not seen `name` written yet, the range it had before that if.
  void note_pre_if_range(std::string_view name);
  // Write the merged range back to `name`'s scalar entry (bundle Entry.bw_* +
  // bw_meta), bypassing write_bw's has-trivial guard so the arm-invalidated
  // entry is refreshed, and propagate it into any enclosing uncertain arm.
  // `wide`: some path leaves `name` too wide for a derived range.
  void commit_merged(std::string_view name, const Lnast_range& r, bool wide);
  // The bundle/bw_meta half of commit_merged, with no arm bookkeeping.
  void set_range(std::string_view name, const Lnast_range& r, bool wide);

  // ── Lnast_range ↔ bundle-Entry conversion ─────────────────────────────────
  static std::optional<int64_t> const_to_i64(const Dlop& v);
  static Lnast_range            range_from_entry(const Dlop& maxc, const Dlop& minc);
  // Range of a pushed operand: comptime point value first (const literals /
  // folded scalars), then the "0" Entry's bw facts, then bw_meta (cross-
  // invocation persistence), else unbounded.
  Lnast_range range_of_operand(const upass::Operand& o) const;
  // Range of a NAME through the table + bw_meta (nullary func_call path).
  Lnast_range read_range(std::string_view name) const;
  // Why a value has no derived range: it is too WIDE for an i64 one (a
  // >62-bit literal or declared type, `x << n` -- an overflow risk), it is a
  // bit-pattern FORCE (its bits are the value), or nothing is known (a tuple,
  // a concat awaiting its lane widths). Only meaningful for an unbounded range.
  enum class Unbounded { unknown, wide, force };
  Unbounded                       why_unbounded(const upass::Operand& o) const;
  // The same question for a name: a published unbounded range (only a wide
  // value gets one, see publish_range) or a >62-bit declared integer.
  bool                            name_is_wide(std::string_view name) const;
  // Bounded, or wide: an operand whose value this pass does know about.
  bool                            is_known_operand(const upass::Operand& o) const;
  // The output port range of a single-output Sub instance `handle`, read as a
  // scalar; nullptr when it is not one.
  const Symbol_table::Port_range* single_output_port(std::string_view handle) const;
  // The declared bit width of an integer operand (an io port or a typed
  // name); 0 when it has none.
  int64_t                         declared_bits_of(const upass::Operand& o) const;
  // bw_meta write-through of `name`'s range. An unbounded range is published
  // only for a wide value; anything else drops the entry.
  void                            publish_range(std::string_view name, const Lnast_range& r, bool wide);

  // Write `r` as dst's value range (Entry.bw_max/min + bw_meta write-through).
  // `replace=false` narrows monotonically (iterative refinement of one
  // producer's tmp); `replace=true` overwrites (mut reassignment must not
  // keep a stale narrow range). Also runs the declared-envelope fit check;
  // `why` says what an unbounded `r` means (see why_unbounded).
  void write_bw(std::string_view name, Bundle& dst, Lnast_range r, bool replace, Unbounded why);

  // Forget a value range after an opaque mutation point (ref actual).
  void clear_range(std::string_view name);

  // Declared envelope for `name` (SSA-base resolved): the unit's own io_meta
  // port type, else the base binding's Entry.decl_max/min (which may have
  // ridden in on a value). nullopt = no (complete) envelope.
  std::optional<Lnast_range> decl_envelope_of(std::string_view name) const;
  // The io_meta integer type of port `base` in `io`.
  std::optional<Lnast_range> port_envelope_of(std::string_view base, const Lnast_tree_io& io) const;
  // decl_envelope_of restricted to names with a DECLARED type (typed_names_
  // or a port): the envelope a write must fit, and a sound range for a read.
  std::optional<Lnast_range> declared_type_of(std::string_view name) const;

  // Check this write against the declared envelope, if any. Emits the
  // does-not-fit diagnostic AT the current node and returns the envelope the
  // destination is left holding; nullopt when the write fits (or is unchecked).
  // An unbounded `r` is an error only when too wide; an unknown one makes the
  // name join Symbol_table::unchecked_typed (its type no longer bounds reads).
  std::optional<Lnast_range> check_declared_fit(std::string_view name, const Lnast_range& r, Unbounded why);
  // A ONE-SIDED declared lower bound (`Unsigned`, `Signed(min=-5)`): the value
  // range must not reach below it. nullopt when `name` has no such floor.
  std::optional<int64_t>     declared_floor_of(std::string_view base) const;

  // declared_type_of for a typed tuple FIELD (`t.x`, a tuple port leaf `o.a`).
  std::optional<Lnast_range> declared_field_type_of(std::string_view name) const;

  // The ELEMENT envelope of array `name`: a declared body array's (the root
  // bundle's internal __elem_max/__elem_min attrs, baked by the runner's
  // declare pre-step) or an array port's element type. nullopt otherwise.
  std::optional<Lnast_range> array_elem_envelope_of(std::string_view name) const;
  // Is `name` an array (a declared body array or an array port)?
  bool                       is_array_name(std::string_view name) const;
  // Check a whole-array store of known entries against the element type.
  void                       check_array_whole_fit(std::string_view name, const upass::Operand& value);
  // Check a `reg`'s declared initial value (every entry of a `reg` array's)
  // against its declared type; the cursor is on the declare.
  void                       check_reg_init_fit(std::string_view var);
  // Check an element store against the array's ELEMENT envelope. No-op for
  // non-array bindings.
  void                       check_array_elem_fit(std::string_view name, const Lnast_range& r, Unbounded why);

  // Emit the common diagnostic at the current node's span (`element`: a
  // store into an entry of array `name`).
  void             record_overflow(std::string_view name, const Lnast_range& value, const Lnast_range& env, bool element = false);
  // The user's spelling of `name`: no inline frame tag, and an input for its
  // default-value local.
  std::string_view display_name(std::string_view name) const;

  // Declared (or ridden) envelope of an operand by name (unbounded for
  // literals/temps): the shift-amount / divisor sign fallback when no value
  // range was derived.
  Lnast_range envelope_of_operand(const upass::Operand& o) const;
  Lnast_range count_pattern(const Lnast_range& amt, const upass::Operand& o) const;

  // Shift-amount sanity for shl/sra (negative-shift): a hardware shift count
  // must be >= 0, judged on the amount's derived range — error when the range
  // is entirely negative (max < 0), warning when it merely allows negatives
  // (min < 0 <= max). Replaces the old constprop comptime-only diagnostic:
  // the range view also covers folded constants and runtime amounts.
  void check_shift_amount(const Lnast_range& amt);

  // An array index must be >= 0; a bw_min < 0 index is a compile error. The
  // runner has rebased an index-range array's index (`idx_name` is its temp).
  void check_index_nonneg(const Lnast_range& idx, std::string_view idx_name);
  void check_index_in_size(std::string_view array_name, const Lnast_range& idx);

  // ── Inferred array shape (08-memories.md) ─────────────────────────────────
  // `reg mem:[] = 0` / `mut a:[] = nil` take their extent from the indices
  // that reach them (a U2 index -> 4 entries) or from a tuple initializer, and
  // `mut m:[13] = 0` / `reg mem:[] = …` take their element type from every
  // value an entry holds. The facts are gathered over the walk; walk_dest
  // writes them into the staged declaration, so the array lowers exactly like
  // a declared `[4]U8`. An array whose shape does not resolve is left as
  // written (lnast.tolg reports it).
  struct Inferred_array {
    std::vector<bool>          open_dim;           // per dimension, outermost first: an open `[]`
    std::vector<int64_t>       extent;             // per dimension: 1 + the largest index seen
    std::vector<int64_t>       init_extent;        // per dimension: entries a tuple initializer lists (0: none)
    bool                       open_elem = false;  // no element type
    bool                       unsized   = false;  // some index has no bounded range
    bool                       conflict  = false;  // declarations of the name leave different shapes open
    bool                       reported  = false;  // the size-limit error was emitted
    std::optional<Lnast_range> elem;               // union of every value an entry takes
    bool                       untyped = false;    // some value has no bounded range
    livehd::diag::Span         span;               // the first declaration
    std::string                big_index;          // an index past the size limit (the error names it)
  };
  // By declared name, merged over every declaration of it (sibling scopes may
  // reuse a name; a larger extent or a wider element is always sound).
  absl::flat_hash_map<std::string, Inferred_array> inferred_arrays_;
  // Names whose latest declaration leaves its shape open: only their accesses
  // count (a sized `[8]U8` of the same name in another scope sizes nothing).
  absl::flat_hash_set<std::string>                 inferred_live_;

  // Record the shape an array declaration leaves open (cursor on its type).
  void            note_inferred_declare(const std::string& var);
  // The facts of `name` when its latest declaration leaves its shape open.
  Inferred_array* live_inferred(std::string_view name);
  // One element access of `name` at indices `idx` (a write also passes the value).
  void            note_inferred_access(std::string_view name, upass::Src_span idx, const upass::Operand* value);
  // The whole-array value of `name` (its initializer): a scalar fills every
  // entry, a positional tuple also sizes the outer dimension.
  void            note_inferred_whole(std::string_view name, const upass::Operand& value);

  // Common body for the value-op hooks.
  Vote stamp(std::string_view dst_name, Bundle& dst, Lnast_range r) {
    // REPLACE semantics: each stamp is a fresh derivation
    // of the value this node just produced. The old narrow-only gate
    // (sound only for a flat accumulating map) refused non-narrowing
    // recomputes, leaving REASSIGNED/loop-rebound bindings with ranges that
    // no longer CONTAIN their value (`mut hit = 0; hit = 30` kept [0,0]).
    write_bw(dst_name, dst, r, /*replace=*/true, Unbounded::unknown);
    return Vote::keep;
  }
  // An arithmetic op that can outgrow an i64 range (+, -, *, <<): an unbounded
  // result over operands that were all bounded or wide is WIDE itself (`x << n`,
  // a u40 product), so it stays an overflow risk downstream.
  Vote stamp_arith(std::string_view dst_name, Bundle& dst, Lnast_range r, upass::Src_span src) {
    const bool wide = r.is_unbounded() && !src.empty()
                      && std::all_of(src.begin(), src.end(), [this](const upass::Operand& o) { return is_known_operand(o); });
    write_bw(dst_name, dst, r, /*replace=*/true, wide ? Unbounded::wide : Unbounded::unknown);
    return Vote::keep;
  }
  // An op that never grows its operands (>>, /, %, bitwise, a bit insert): its
  // unbounded result is only as wide as a wide operand makes it; otherwise the
  // analysis just lost the range (`s8 | s8`).
  Vote stamp_carry(std::string_view dst_name, Bundle& dst, Lnast_range r, upass::Src_span src) {
    const bool wide = r.is_unbounded() && std::any_of(src.begin(), src.end(), [this](const upass::Operand& o) {
                        return range_of_operand(o).is_unbounded() && why_unbounded(o) == Unbounded::wide;
                      });
    write_bw(dst_name, dst, r, /*replace=*/true, wide ? Unbounded::wide : Unbounded::unknown);
    return Vote::keep;
  }
};
