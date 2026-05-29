//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <format>
#include <map>
#include <memory>
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
  void notify_uncertain_arm_begin() override { next_block_uncertain = true; }
  void notify_uncertain_arm_end() override { next_block_uncertain = false; }
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

  upass::Emit_decision classify_statement() override;
  std::optional<Const> fold_ref(std::string_view name) override;
  bool                 overrides_fold_ref() const override { return true; }
  bool                 overrides_classify_statement() const override { return true; }

  // Shared-ST read access for the runner inliner (1i Phase E). Expose the
  // bundle fields / typename the runner can't see through scalar fold_ref.
  std::optional<std::vector<std::pair<std::string, Const>>> provide_bundle_fields(std::string_view name) override;
  std::string                                               provide_typename(std::string_view name) override;
  bool                                                      overrides_shared_st() const override { return true; }

  static void set_function_registry(const std::vector<std::shared_ptr<Lnast>>& lnasts);

protected:
  Symbol_table st;

  // Set by notify_uncertain_arm_begin (the runner calls this just before we
  // descend into an if-arm whose cond didn't fold to known-true/known-false).
  // Consumed in process_stmts to mark the freshly-pushed block scope as
  // uncertain so leave_scope invalidates anything assigned inside.
  bool next_block_uncertain{false};

  // Names whose bundle should be treated as a tuple (rather than scalar
  // wrapper) by process_tuple_concat — populated by every successful
  // tuple_add / tuple_concat write, and propagated through assign(ref).
  // Lets `c ++ (i,)` stay in bundle mode after the accumulator collapses
  // to a single-entry shape, so for-loop unrolls advance correctly.
  std::unordered_set<std::string> tuple_typed_names;

  // Range bookkeeping outside the symbol table: a `range` LNAST node binds
  // its destination ref to a (start, end) pair. `end` may be the literal
  // pyrope `nil` to mean "open-ended" (slice runs to the source's last
  // index). process_tuple_get consults this map when the field operand is
  // a ref so it can fold string slicing like `x[1..]` and `x[1..=2]`.
  std::map<std::string, std::pair<Const, Const>> range_map;

  // Typename of variables that were declared with an explicit `:Type`
  // annotation (`attr_set var typename 'Type'` in the lnast). Used by the
  // method-dispatch fallback so `p.method(args)` can resolve `method`
  // through `Type`'s bundle when the instance bundle alone doesn't carry
  // the method field.
  std::unordered_map<std::string, std::string> typename_of_var;

  // Variables whose decorator-init setter has already fired. Entry 1k:
  // `mut x:Tup = (v="")` triggers Tup.setter once at construction time;
  // a later `x = ...` is a plain reassign and must NOT refire the setter.
  std::unordered_set<std::string> setter_fired;

  auto current_bundle() { return st.get_bundle(current_text()); }

  // Bundle iff the cursor is on a ref. Returns nullptr otherwise — used by
  // marker-fold helpers that walk const/ref siblings.
  std::shared_ptr<Bundle const> current_ref_bundle() const {
    if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      return st.get_bundle(current_text());
    }
    return nullptr;
  }

  Const current_pyrope_value() { return *Dlop::from_pyrope(current_text()); }

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
  // (lowered via rsh_op); a concrete `hi >= lo` lowers via get_mask_op with
  // the closed mask. Returns invalid when bounds are not folded integers.
  static Const apply_range_mask(const Const& value, const Const& start, const Const& end);

  // Single-shot "store result, mark_changed if value actually changed".
  // The has_trivial/get_trivial!=/set+mark_changed dance was repeated at
  // every fold site; centralising it kills a bug-prone pattern.
  void store_trivial(std::string_view name, const Const& v) {
    if ((!st.has_trivial(name) || !st.get_trivial(name).same_repr(v)) && st.set(name, v)) {
      mark_changed();
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

  struct Call_actual {
    bool        is_named = false;
    std::string name;
    Const       value;
    // When the actual is a bare ref to a caller variable, remember the name
    // so a `ref` param can write back into the caller's scope after the
    // body is folded. Empty when the actual is a const literal or named with
    // a non-ref value (write-back only happens when the param is `ref`).
    std::string var_name;
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
    bool is_unresolved = false;
  };

  static inline std::unordered_map<std::string, std::shared_ptr<Lnast>> function_registry;

  std::optional<Const>                    resolve_current_scalar() const;
  std::optional<std::vector<Call_actual>> collect_call_actuals();
  bool try_eval_comb_call(std::string_view dst, std::string_view fname, const std::vector<Call_actual>& actuals);

  // Entry 1k: fire the decorator-init setter for `var` if its declared
  // typename carries a `setter` field and the setter hasn't fired before.
  // Returns true if a setter was fired (writebacks already applied).
  bool maybe_fire_setter_init(std::string_view var);
  // Direct-cell call dispatch: `__sum(a, b)`, `__hotmux(sel, a, b, …)`, etc.
  // Maps cell names (without the `__` prefix) to Ntype_op kernels and folds
  // when all positional actuals are foldable constants. Returns true when
  // dst was assigned a result; false when the fname is not a recognized
  // cell or arguments aren't comptime.
  bool try_eval_cell_call(std::string_view dst, std::string_view fname, const std::vector<Call_actual>& actuals);

  // Evaluation result for a recursive inline call: outputs indexed by name
  // plus any ref-param mutations to apply at the caller side. Used both by
  // the top-level call site and by recursive calls inside other comb bodies.
  struct Inline_result {
    std::unordered_map<std::string, Const>           outputs;
    std::unordered_map<std::string, Const>           ref_writebacks;
    // For ref-param actuals that were caller-side bundle entries we still
    // expose the qualified-flat writeback names (e.g. "self.x" -> Const).
    std::unordered_map<std::string, Const>           ref_writeback_fields;
  };

  // Recursion-aware inline of `callee_name` with already-bound `actuals`.
  // Returns nullopt when the callee can't be statically evaluated yet
  // (unknown args, missing registry entry, depth exhausted, body shape we
  // can't fold, etc.). The caller decides how to surface the outputs (write
  // to the symbol table for outer calls; merge into local_values for nested
  // calls).
  std::optional<Inline_result> evaluate_callee_inline(std::string_view callee_name,
                                                       const std::vector<Call_actual>& actuals,
                                                       int depth);

  // Per-elaboration recursive-inline depth cap. Comb recursion fully unrolls
  // at constprop time, so a stuck/infinite recursion would never terminate.
  // 10000 matches TODO_prp entry 1y's default; the offending call site is
  // surfaced via Pass::warn before bailing.
  static constexpr int kInlineMaxDepth = 10000;
};

// Plugin registration lives in upass_constprop.cpp to avoid duplicate
// construction when multiple TUs include this header.
