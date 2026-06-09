//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <format>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "const.hpp"
#include "lnast_ntype.hpp"
#include "symbol_table.hpp"
#include "upass_core.hpp"

struct uPass_constprop : public upass::uPass {
public:
  uPass_constprop(std::shared_ptr<upass::Lnast_manager>&);
  uPass_constprop() = delete;
  virtual ~uPass_constprop() {}

  void process_assign() override;
  void process_plus() override;
  void process_minus() override;
  void process_mult() override;
  void process_div() override;
  void process_mod() override;
  void process_shl() override;
  void process_sra() override;
  void process_bit_and() override;
  void process_bit_or() override;
  void process_bit_not() override;
  void process_bit_xor() override;
  void process_log_and() override;
  void process_log_or() override;
  void process_log_not() override;
  void process_ne() override;
  void process_eq() override;
  void process_lt() override;
  void process_le() override;
  void process_gt() override;
  void process_ge() override;
  void process_is() override;
  void process_if() override;

  // Bitwidth Insensitive Reduce
  void process_red_or() override;
  void process_red_and() override;
  void process_red_xor() override;
  void process_popcount() override;

  // Bit Manipulation
  void process_sext() override;
  void process_get_mask() override;
  void process_set_mask() override;

  void process_stmts() override;
  void process_stmts_post() override;
  // Task 1m — at file-scope completion, fold every `pub` value export into
  // the Lnast's pub-values side channel (errors when not comptime-foldable).
  void harvest_pub_values();
  void notify_uncertain_arm_begin() override { next_block_uncertain = true; }
  void notify_uncertain_arm_end() override { next_block_uncertain = false; }
  void process_declare() override;
  void process_tuple_set() override;
  void process_tuple_get() override;
  void process_tuple_add() override;
  void process_tuple_concat() override;
  void process_attr_set() override;
  void process_func_call() override;
  void process_func_does() override;
  void process_func_equals() override;
  void process_func_in() override;
  void process_func_has() override;
  void process_func_case() override;
  void process_range() override;
  void end_run() override;

  // Bundle-access check (optable.md): a comptime-known tuple access whose key
  // is out of bounds (positional) or names a non-existent field is a compile
  // error. Only fires when `base`'s bundle shape is resolved (non-empty,
  // non-scalar) — a runtime index or an unresolved/partial base is skipped.
  void check_tuple_access(const std::string& base, const std::string& seg, bool is_index);

  upass::Emit_decision classify_statement() override;
  std::optional<Const> fold_ref(std::string_view name) override;
  bool                 overrides_fold_ref() const override { return true; }
  bool                 overrides_classify_statement() const override { return true; }

  // Shared-ST read access for the runner inliner (1i Phase E). Expose the
  // bundle fields / typename the runner can't see through scalar fold_ref.
  std::optional<std::vector<std::pair<std::string, Const>>> provide_bundle_fields(std::string_view name) override;
  std::string                                               provide_typename(std::string_view name) override;
  // Comptime for-loop unroll (runner): expose the folded (start, end_inclusive)
  // bounds of a `range` tmp so the runner can iterate. nullopt when `name` is
  // not a recorded range.
  std::optional<std::pair<Const, Const>>                    provide_range(std::string_view name) override;
  // Loop-migration (Step 1): the source ref held at `slot` of tuple `name`
  // when that slot is a runtime scalar (so the runner can rewrite a comptime
  // tuple pick into a copy). nullopt when not tracked.
  std::optional<std::string>                                provide_tuple_slot_ref(std::string_view name,
                                                                                   std::string_view slot) override;
  // Loop-migration (Step 2): ordered (slot-key, is_positional) shape of a
  // tuple, from its bundle's top-levels merged with any runtime-only slots
  // (tuple_slot_ref_). Lets the runner unroll `for x in t` over the entries.
  std::optional<std::vector<std::pair<std::string, bool>>>  provide_tuple_shape(std::string_view name) override;
  bool                                                      overrides_shared_st() const override { return true; }

  static void set_function_registry(const std::vector<std::shared_ptr<Lnast>>& lnasts);

  // Task 1m — unresolved live imports recorded during the walk: (unit that
  // hit the import, import string as written). pass.upass either surfaces
  // them as errors or hands them to the kernel's iterate loop (import_defer).
  struct Pending_import {
    std::string unit;
    std::string text;
  };
  static void                               reset_pending_imports() { pending_imports_.clear(); }
  static const std::vector<Pending_import>& pending_imports() { return pending_imports_; }

  // Task 1m — unit/tree names that appear in MORE THAN ONE loaded input
  // (registry is name-keyed, so a plain lookup would silently pick one).
  // An `import` that resolves to one of these is an ambiguity error (§2);
  // a non-imported collision is tolerated (never consulted). Set by
  // pass.upass from the duplicate top_module_names in var.lnasts.
  static void set_ambiguous_units(std::unordered_set<std::string> s) { ambiguous_units_ = std::move(s); }

protected:
  static inline std::vector<Pending_import>      pending_imports_;
  static inline std::unordered_set<std::string>  ambiguous_units_;

  // Task 1m — resolve a live `import` call (the LiveHD docs):
  // cursor sits on the const "import" callee; binds `dst` (tuple form → pub
  // namespace bundle; `ln:` url → lambda tree-name string) or records a
  // pending import and leaves the call unfolded.
  void process_import_call(const std::string& dst);

  Symbol_table st;

  // Set by notify_uncertain_arm_begin (the runner calls this just before we
  // descend into an if-arm whose cond didn't fold to known-true/known-false).
  // Consumed in process_stmts to mark the freshly-pushed block scope as
  // uncertain so leave_scope invalidates anything assigned inside.
  bool next_block_uncertain{false};

  // Range bookkeeping outside the symbol table: a `range` LNAST node binds
  // its destination ref to a (start, end) pair. `end` may be the literal
  // pyrope `nil` to mean "open-ended" (slice runs to the source's last
  // index). process_tuple_get consults this map when the field operand is
  // a ref so it can fold string slicing like `x[1..]` and `x[1..=2]`.
  std::map<std::string, std::pair<Const, Const>> range_map;

  // Loop-migration (Step 1): per tuple-valued name, the ref each slot holds
  // when that slot is a RUNTIME scalar (no comptime value / sub-bundle, so the
  // bundle itself can't remember it). Keyed by tuple name → slot key
  // ("0","1",… positional, or the field name) → the source ref. Populated in
  // process_tuple_add, propagated on a ref-alias assign, and exposed via
  // provide_tuple_slot_ref so the runner can rewrite `t[i]` / `for x in t`
  // into a direct copy `dst = ref` (a tuple_get with a comptime index is a
  // comptime structural pick even when the picked VALUE is a runtime signal).
  std::unordered_map<std::string, std::map<std::string, std::string>> tuple_slot_ref_;

  // Typename of variables that were declared with an explicit `:Type`
  // annotation (`attr_set var typename 'Type'` in the lnast). Used by the
  // method-dispatch fallback so `p.method(args)` can resolve `method`
  // through `Type`'s bundle when the instance bundle alone doesn't carry
  // the method field.
  std::unordered_map<std::string, std::string> typename_of_var;

  // Task 1t — declared UNSIGNED bit-width per var, recorded by process_declare
  // from `declare(var, prim_type_uint(N) | prim_type_int(max,min≥0), …)`. Used
  // to coerce a known-negative comptime literal to its unsigned N-bit pattern
  // at the var's FIRST scalar write (`v:u8 = 0sb1001_0111` ⇒ 151), so
  // constprop's OWN folding of later reads (`v | 0xff`) sees the unsigned
  // value — not the raw signed literal. (Mirrors the attributes-side coercion
  // in tmp_fold, but here it lands in constprop's symbol table where its own
  // op-folding reads operands.) Signed/none-typed decls are not recorded.
  // Stores the declared MAX as a Const (for uN this is the N-bit all-ones
  // mask); the first-write coercion is `v & max`. No width/to_i — task 1g.
  std::unordered_map<std::string, Const> decl_unsigned_max_;
  std::optional<std::string>             pending_unsigned_overflow_msg_;

  // Task 1t — named type per var, recorded by process_declare when the declare's
  // type slot is a `ref(NAMED)` (a named type, e.g. `mut c:v_type = …`). At the
  // var's init bundle write, process_assign materializes NAMED's resolved bundle
  // (its default field values + per-field types) and overlays the init fields,
  // so `const v_type=(x:u3=nil, b:string="foo"); mut c:v_type=(x=3)` yields
  // c={x:3, b:"foo"}. NAMED may be declared via `type T=(…)` or `const T=(…)`;
  // both leave T's bundle in the symbol table.
  std::unordered_map<std::string, std::string> decl_named_type_;

  // 2d-reg — names declared with mode `reg` (incl. stage-synthesized regs).
  // Their reads are RUNTIME q reads (never fold through the symbol table)
  // and their stores are next-state din writes (never bound, never dropped):
  // Verilog `<=` semantics — a read after a write still sees the flop's q.
  std::unordered_set<std::string> reg_decl_names_;

  // Task 2u — names declared `mut`. classify_statement keeps a comptime init
  // store (`acc = <const>`) for these (not const/temps): if the mut is later
  // reassigned with a RUNTIME value inside a comptime-eliminated block (an
  // `if true {…}` arm or an unrolled loop iteration), the body is copied
  // verbatim (not SSA-versioned), so the in-block read emits a bare `acc` whose
  // only driver is this init — dropping it leaves it undriven. A mut that stays
  // comptime keeps a dead init that cprop/DCE then removes, so this is safe.
  std::unordered_set<std::string> mut_decl_names_;

  auto current_bundle() { return st.get_bundle(current_text()); }

  // Bundle iff the cursor is on a ref. Returns nullptr otherwise — used by
  // marker-fold helpers that walk const/ref siblings.
  std::shared_ptr<Bundle const> current_ref_bundle() const {
    if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      if (auto b = st.get_bundle(current_text()); b) {
        return b;
      }
      // Inline-frame fallback: inside a spliced body every ref is tag-renamed
      // (`Person` → `inlN_Person`), but a global comptime binding — e.g. the
      // type tuple of `x does Person` (enum_types) — lives in the ST under
      // its raw name. Reads a lambda body may not see were already rejected
      // at parse time (semacheck lambda-visibility), so the raw lookup only
      // resolves legal lexical globals.
      const auto raw = lm->current_raw_text();
      if (raw != current_text()) {
        return st.get_bundle(raw);
      }
    }
    return nullptr;
  }

  Const current_pyrope_value() { return *Dlop::from_pyrope(current_text()); }

  void check_unsigned_positive_overflow(std::string_view lhs, const Const& value);

  auto current_prim_value() const {
    if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      auto name = current_text();
      // Cross-pass override-or-fallback. uPass_attributes publishes
      // narrowed values for vars under wrap/sat policy via tmp_fold; those
      // overrides must win over constprop's stored raw value, otherwise
      // `cassert w == 0xF` on a wrap-policy `w = 0xFF` reads back the
      // unwrapped 0xFF. For refs with no override, fold_ref returns
      // nullopt and we fall through to the ST. The runner_fold_fn loop
      // also covers attribute-pass-only tmps (attr_get destinations,
      // aggregate `.[size]` folds) that constprop never assigned.
      if (runner_fold_fn) {
        auto folded = runner_fold_fn(name);
        if (folded && !folded->is_invalid()) {
          return *folded;
        }
      }
      // Single-entry bundle: a parenthesized scalar `(expr)` lowers to a
      // 1-element tuple_add (e.g. `!(p is yy)`); flatten to its lone value so
      // unary/nary ops fold over it (mirrors process_eq_ne_impl::resolve).
      if (auto b = st.get_bundle(name); b && b->is_scalar()) {
        if (auto bv = b->get_trivial(); !bv.is_invalid()) {
          return bv;
        }
      }
      const auto v = st.get_trivial(name);
      return v;  // may be invalid — caller's foldable() check short-circuits.
    }
    I(is_type(Lnast_ntype::Lnast_ntype_const));
    return *Dlop::from_pyrope(current_text());
  }

  // Predicates for the standard "is this value foldable?" guard. `is_numeric`
  // allows X-bit unknowns through (n-ary bitwise ops propagate `?` bits);
  // `foldable` is the strict version used everywhere else.
  static bool is_numeric(const Const& v) { return !v.is_invalid() && !v.is_string(); }
  static bool foldable(const Const& v) { return is_numeric(v) && !v.has_unknowns(); }

  // Type-agnostic structural identity: same base+extra+size, but type may
  // differ. Used by process_eq_ne to fold `(v != 0) == 0sb?` (Boolean unknown
  // vs Integer unknown with identical bit patterns) to known-true. Dlop's
  // same_repr requires matching `type`, so it can't catch this case.
  static bool same_bits_ignore_type(const Const& a, const Const& b) {
    if (a.is_invalid() || b.is_invalid()) {
      return false;
    }
    if (a.size != b.size) {
      return false;
    }
    for (int i = 0; i < a.size; ++i) {
      if (a.base()[i] != b.base()[i]) {
        return false;
      }
      if (a.extra()[i] != b.extra()[i]) {
        return false;
      }
    }
    return true;
  }

  // Strip the single-quote wrapping `Lconst::to_pyrope` adds around string
  // values so the bare contents can be re-parsed or compared.
  static std::string strip_pyrope_quotes(std::string s) {
    if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') {
      s = s.substr(1, s.size() - 2);
    }
    return s;
  }

  // Apply a (start, end) bit-slice range to `value` and return the extracted
  // bits packed LSB-first. `end` may be `nil` for an open-ended `lo..` slice
  // (lowered via sra_op); a concrete `hi >= lo` lowers via get_mask_op with
  // the closed mask. Returns invalid when bounds are not folded integers.
  static Const apply_range_mask(const Const& value, const Const& start, const Const& end);

  // Single-shot "store result only when the value actually changed".
  // The has_trivial/get_trivial!=/set dance was repeated at every fold site;
  // centralising it kills a bug-prone pattern.
  void store_trivial(std::string_view name, const Const& v) {
    if (!st.has_trivial(name) || !st.get_trivial(name).same_repr(v)) {
      st.set(name, v);
    }
  }
  void store_trivial(std::string_view name, const spool_ptr<Dlop>& v) { store_trivial(name, *v); }

  template <typename F>
  inline void process_nary(F op);

  template <typename F>
  inline void process_binary(F op);

  // Variant for ops whose Dlop kernel propagates unknowns itself (lt_op etc).
  template <typename F>
  inline void process_binary_passthrough(F op);

  template <typename F>
  inline void process_unary(F op);

  template <typename F>
  inline void process_reduction(F op);

  template <bool Negate>
  void process_eq_ne_impl();

  void fold_does(const std::string& dst);
  void fold_in(const std::string& dst);
  void fold_has(const std::string& dst);
  void fold_case(const std::string& dst);

  // Task 1g — type-aware `does`/`equals`. A resolved operand carries its scalar
  // KIND plus, for integers, a (max,min) ENVELOPE (with explicit unbounded
  // flags) and — when it has a concrete value — the literal/folded Const used
  // to coerce a scalar into a single-positional bundle for the structural
  // (tuple) path. `bundle` is set only for real tuples.
  struct Does_operand {
    enum class Kind : uint8_t { integer, boolean, string, tuple, nil, unknown };
    Kind                          kind = Kind::unknown;
    bool                          max_inf = false;  // envelope max is +∞
    bool                          min_inf = false;  // envelope min is −∞
    Const                         max;
    Const                         min;
    bool                          has_value = false;
    Const                         value;  // valid when has_value
    std::shared_ptr<Bundle const> bundle;  // set when kind==tuple
  };
  // Resolve the `does`/`equals`/`case` operand at the current cursor (a ref or
  // const). nullopt when undecidable this walk (defer the fold).
  std::optional<Does_operand> resolve_does_operand();
  // Build a scalar Does_operand from a declared type query plus an optional
  // folded value (shared by the ref-operand and per-field paths).
  static Does_operand build_scalar_operand(const upass::uPass::Scalar_type_query& q, const Const& folded);
  // Resolve one named/positional field of a bundle to a Does_operand for the
  // per-field type check (1g-D): declared type via the dotted query
  // (`bundle.field`), value via the bundle entry, sub-bundle for nested tuples.
  // `declared_only` (used by the per-field type check) returns nullopt for a
  // scalar field with NO explicit declared type — an untyped field imposes no
  // type constraint (`m1 does (a:u32)` passes; `s case (a=0)` stays a value
  // pattern), so the check runs only when BOTH sides are declared-typed.
  std::optional<Does_operand> resolve_field_operand(const Bundle& b, std::string_view field, bool declared_only);
  // Decode a primitive type token (`u32`/`s8`/`int`/`bool`/`string`/…) in
  // operand position to its kind+envelope. Returns nullopt if `name` is not a
  // type token.
  static std::optional<Does_operand> decode_prim_type_token(std::string_view name);
  // Build a one-entry positional bundle {0: value} so a scalar operand can take
  // the structural path against a real tuple (`(100,30) does 30`).
  static std::shared_ptr<Bundle> single_positional_bundle(const Const& v);
  // Tri-state kind+envelope `a does b` for two NON-tuple operands. nullopt =
  // undecidable.
  static std::optional<bool> scalar_does(const Does_operand& a, const Does_operand& b);
  // Tri-state `a does b` for any two resolved operands: structural (tuple)
  // path when a side is a real tuple (plus per-field type checks, 1g-D), else
  // scalar_does. nullopt = undecidable. `equals` is this both ways.
  std::optional<bool> compute_does(const Does_operand& a, const Does_operand& b);

  struct Call_actual {
    bool                                   is_named = false;
    std::string                            name;
    Const                                  value;
    // When the actual is a bare ref to a caller variable, remember the name
    // so a `ref` param can write back into the caller's scope after the
    // body is folded. Empty when the actual is a const literal or named with
    // a non-ref value (write-back only happens when the param is `ref`).
    std::string                            var_name;
    // When the actual is a tuple-typed value (passed by ref to a caller
    // bundle), bundle_value carries the bundle's flat-keyed field values
    // (e.g. {"x": 2, "y": 11}). Set together with is_bundle=true; `value`
    // is then unused.
    bool                                   is_bundle = false;
    std::unordered_map<std::string, Const> bundle_value;
    // True when the actual could not be folded to a concrete scalar/bundle
    // (e.g. runtime-only Flop driver). The inliner binds nothing for these
    // params but still folds body stmts that don't depend on them — used by
    // entry 1u so an outer cassert against `(a.[comptime], …)` can resolve
    // even when one arg is runtime.
    bool                                   is_unresolved = false;
  };

  static inline std::unordered_map<std::string, std::shared_ptr<Lnast>> function_registry;

  std::optional<Const>                    resolve_current_scalar() const;
  std::optional<std::vector<Call_actual>> collect_call_actuals();

  // Direct-cell call dispatch: `__sum(a, b)`, `__hotmux(sel, a, b, …)`, etc.
  // Maps cell names (without the `__` prefix) to Ntype_op kernels and folds
  // when all positional actuals are foldable constants. Returns true when
  // dst was assigned a result; false when the fname is not a recognized
  // cell or arguments aren't comptime.
  bool try_eval_cell_call(std::string_view dst, std::string_view fname, const std::vector<Call_actual>& actuals);

  // Unlimited-sink mux / hotmux / lut cells: pins are named (`s`, `p1`, …) or
  // positional, so map each actual onto its sink pid and delegate to the
  // matching Dlop kernel (which folds unknown selector/address bits too).
  bool try_eval_mux_cell_call(std::string_view dst, std::string_view op, const std::vector<Call_actual>& actuals);

  // Sum cell folding: pin a (pid 0) adds, pin b (pid 1) subtracts (no plain
  // positional add, so it can't share the generic per-op flatten path).
  bool try_eval_sum_cell_call(std::string_view dst, const std::vector<Call_actual>& actuals);
};

// Plugin registration lives in upass_constprop.cpp to avoid duplicate
// construction when multiple TUs include this header.
