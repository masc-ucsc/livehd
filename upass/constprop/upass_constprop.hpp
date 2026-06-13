//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <format>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "call_resolver.hpp"
#include "decl_facts.hpp"
#include "hlop/dlop.hpp"
#include "lnast_ntype.hpp"
#include "symbol_table.hpp"
#include "upass_core.hpp"

struct uPass_constprop : public upass::uPass {
public:
  uPass_constprop(std::shared_ptr<upass::Lnast_manager>&);
  uPass_constprop() = delete;
  virtual ~uPass_constprop() {}

  // Store routes both arities; the cursor-walking assign/tuple_set
  // bodies stay as private helpers (subtree payloads don't ride the span).
  upass::Vote process_store(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  void        process_assign() override;
  upass::Vote process_plus(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_minus(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_mult(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_div(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_mod(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_shl(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_sra(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_bit_and(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_bit_or(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_bit_not(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_bit_xor(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_log_and(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_log_or(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_log_not(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_ne(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_eq(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_lt(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_le(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_gt(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_ge(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_is(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  void        process_if() override;

  // Bitwidth Insensitive Reduce
  upass::Vote process_red_or(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_red_and(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_red_xor(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_popcount(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;

  // Bit Manipulation
  upass::Vote process_sext(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_get_mask(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_set_mask(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;

  void        process_stmts_post() override;
  // At file-scope completion, fold every `pub` value export into
  // the Lnast's pub-values side channel (errors when not comptime-foldable).
  void        harvest_pub_values();
  void        process_declare() override;
  void        process_tuple_set() override;
  void        process_tuple_get() override;
  upass::Vote process_tuple_add(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_tuple_concat(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  void        process_attr_set() override;
  void        process_func_call() override;
  upass::Vote process_func_does(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_func_equals(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_func_in(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_func_has(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_func_case(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  void        process_range() override;
  void        end_run() override;

  // Bundle-access check (optable.md): a comptime-known tuple access whose key
  // is out of bounds (positional) or names a non-existent field is a compile
  // error. Only fires when `base`'s bundle shape is resolved (non-empty,
  // non-scalar) — a runtime index or an unresolved/partial base is skipped.
  void check_tuple_access(const std::string& base, const std::string& seg, bool is_index);

  // Inner-dimension bounds for all-index dotted paths (`b[2][10]` on
  // b:[4][8]u8); named segments stay legal nil-probes, scalar-leaf zero
  // chains (`(1,2)[0][0]`) stay legal sugar.
  void check_nested_tuple_access(const std::string& base, const std::string& key);

  // The emit decision rides the push VOTE: every value-op/store hook
  // returns classify_vote() (cursor-based evaluation, now constprop-private —
  // the base classify_statement virtual stays for the verifier/func_extract
  // REGION verdicts only).
  // Distinct name: a same-signature redeclaration would implicitly override
  // the base virtual (overrides_classify_statement() stays false, so the
  // runner must never see this through the base interface).
  upass::Emit_decision classify_statement_impl();
  upass::Vote          classify_vote() {
    return classify_statement_impl().kind == upass::Emit_kind::drop_subtree ? upass::Vote::drop : upass::Vote::keep;
  }

  static void set_function_registry(const std::vector<std::shared_ptr<Lnast>>& lnasts);

  // Unresolved live imports recorded during the walk: (unit that
  // hit the import, import string as written). pass.upass either surfaces
  // them as errors or hands them to the kernel's iterate loop (import_defer).
  struct Pending_import {
    std::string unit;
    std::string text;
  };
  static void                               reset_pending_imports() { pending_imports_.clear(); }
  static const std::vector<Pending_import>& pending_imports() { return pending_imports_; }

  // Unit/tree names that appear in MORE THAN ONE loaded input
  // (registry is name-keyed, so a plain lookup would silently pick one).
  // An `import` that resolves to one of these is an ambiguity error (§2);
  // a non-imported collision is tolerated (never consulted). Set by
  // pass.upass from the duplicate top_module_names in var.lnasts.
  static void set_ambiguous_units(std::unordered_set<std::string> s) { ambiguous_units_ = std::move(s); }

protected:
  static inline std::vector<Pending_import>     pending_imports_;
  static inline std::unordered_set<std::string> ambiguous_units_;

  // Resolve a live `import` call (the LiveHD docs):
  // cursor sits on the const "import" callee; binds `dst` (tuple form → pub
  // namespace bundle; `ln:` url → lambda tree-name string) or records a
  // pending import and leaves the call unfolded.
  void process_import_call(const std::string& dst);

  // The symbol table is the RUNNER's: one shared scope-aware table,
  // with every push/pop (function/block scopes, uncertain-arm marking) owned
  // by the runner. These accessors keep the many `st().` call sites short.
  Symbol_table&       st() { return *runner_st; }
  const Symbol_table& st() const { return *runner_st; }

  // Range bookkeeping outside the symbol table: a `range` LNAST node binds
  // its destination ref to a (start, end) pair. `end` may be the literal
  // pyrope `nil` to mean "open-ended" (slice runs to the source's last
  // index). process_tuple_get consults this map when the field operand is
  // a ref so it can fold string slicing like `x[1..]` and `x[1..=2]`.

  // Loop-migration (Step 1): per tuple-valued name, the ref each slot holds
  // when that slot is a RUNTIME scalar (no comptime value / sub-bundle, so the
  // bundle itself can't remember it). Keyed by tuple name → slot key
  // ("0","1",… positional, or the field name) → the source ref. Populated in
  // process_tuple_add, propagated on a ref-alias assign, and exposed via
  // provide_tuple_slot_ref so the runner can rewrite `t[i]` / `for x in t`
  // into a direct copy `dst = ref` (a tuple_get with a comptime index is a
  // comptime structural pick even when the picked VALUE is a runtime signal).

  // Typename of variables that were declared with an explicit `:Type`
  // annotation (`attr_set var typename 'Type'` in the lnast). Used by the
  // method-dispatch fallback so `p.method(args)` can resolve `method`
  // through `Type`'s bundle when the instance bundle alone doesn't carry
  // the method field.

  // Declared UNSIGNED bit-width per var, recorded by process_declare
  // from `declare(var, prim_type_uint(N) | prim_type_int(max,min≥0), …)`. Used
  // to coerce a known-negative comptime literal to its unsigned N-bit pattern
  // at the var's FIRST scalar write (`v:u8 = 0sb1001_0111` ⇒ 151), so
  // constprop's OWN folding of later reads (`v | 0xff`) sees the unsigned
  // value — not the raw signed literal. (Mirrors the attributes-side coercion
  // in tmp_fold, but here it lands in constprop's symbol table where its own
  // op-folding reads operands.) Signed/none-typed decls are not recorded.
  // Stores the declared MAX as a Dlop (for uN this is the N-bit all-ones
  // mask); the first-write coercion is `v & max`. No width/to_i.
  std::optional<std::string> pending_unsigned_overflow_msg_;

  // Named type per var, recorded by process_declare when the declare's
  // type slot is a `ref(NAMED)` (a named type, e.g. `mut c:v_type = …`). At the
  // var's init bundle write, process_assign materializes NAMED's resolved bundle
  // (its default field values + per-field types) and overlays the init fields,
  // so `const v_type=(x:u3=nil, b:string="foo"); mut c:v_type=(x=3)` yields
  // c={x:3, b:"foo"}. NAMED may be declared via `type T=(…)` or `const T=(…)`;
  // both leave T's bundle in the symbol table.

  // Names declared with mode `reg` (incl. stage-synthesized regs).
  // Their reads are RUNTIME q reads (never fold through the symbol table)
  // and their stores are next-state din writes (never bound, never dropped):
  // Verilog `<=` semantics — a read after a write still sees the flop's q.

  // Names declared `mut`. classify_statement keeps a comptime init
  // store (`acc = <const>`) for these (not const/temps): if the mut is later
  // reassigned with a RUNTIME value inside a comptime-eliminated block (an
  // `if true {…}` arm or an unrolled loop iteration), the body is copied
  // verbatim (not SSA-versioned), so the in-block read emits a bare `acc` whose
  // only driver is this init — dropping it leaves it undriven. A mut that stays
  // comptime keeps a dead init that cprop/DCE then removes, so this is safe.

  auto current_bundle() { return st().get_bundle(current_text()); }

  // Bundle iff the cursor is on a ref. Returns nullptr otherwise — used by
  // marker-fold helpers that walk const/ref siblings.
  std::shared_ptr<Bundle const> current_ref_bundle() const {
    if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      if (auto b = st().get_bundle(current_text()); b) {
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
        return st().get_bundle(raw);
      }
    }
    return nullptr;
  }

  Dlop current_pyrope_value() { return *Dlop::from_pyrope(current_text()); }

  // Declared facts read from the BINDING (the runner bake writes
  // mode/type_name/decl ranges at the declare node, before any store):
  upass::Mode decl_mode_of(std::string_view var) {
    const auto b = st().get_bundle(var);
    return b ? b->get_mode() : upass::Mode::unknown;
  }
  std::string decl_type_name_of(std::string_view var) {
    const auto b = st().get_bundle(var);
    return b ? std::string(b->get_type_name()) : std::string{};
  }
  // The declared MAX for an UNSIGNED int decl (min known ≥ 0); invalid Dlop
  // when unsigned-ness doesn't hold or nothing was declared.
  Dlop decl_unsigned_max_of(std::string_view var) {
    const auto b = st().get_bundle(var);
    if (!b) {
      return Dlop();
    }
    const Bundle::Entry& e = b->get_entry("0");
    if (e.decl_max.is_invalid() || e.decl_min.is_invalid() || e.decl_min.is_negative() || e.decl_max.is_negative()) {
      return Dlop();
    }
    return e.decl_max;
  }

  void check_unsigned_positive_overflow(std::string_view lhs, const Dlop& value);

  // Field paths read via tuple_get this walk (unused-unset warning).
  absl::flat_hash_set<std::string> field_reads_;

  // Local replacement for the deleted runner_type_query_fn seam:
  // inferred scalar KIND off the binding + declared integer ENVELOPE from
  // the shared decl-facts derivation.
  upass::uPass::Scalar_type_query scalar_type_query_of(std::string_view name) {
    upass::uPass::Scalar_type_query q;
    if (const auto b = st().get_bundle(name); b && !(b->has_named_top() || b->unnamed_top_count() > 1)) {
      upass::Kind k = b->get_value_kind();
      if (k == upass::Kind::unknown) {
        k = b->get_entry("0").kind;
      }
      switch (k) {
        case upass::Kind::integer: q.kind = Io_kind::integer; break;
        case upass::Kind::boolean: q.kind = Io_kind::boolean; break;
        case upass::Kind::string : q.kind = Io_kind::string; break;
        default                  : break;
      }
    }
    if (const auto f = upass::decl_facts::lookup(st(), lm ? lm->get_lnast().get() : nullptr, name); f && f->has_type_spec) {
      if (q.kind == Io_kind::none) {
        switch (f->kind) {
          case upass::decl_facts::Num::unsigned_int:
          case upass::decl_facts::Num::signed_int  : q.kind = Io_kind::integer; break;
          case upass::decl_facts::Num::boolean     : q.kind = Io_kind::boolean; break;
          case upass::decl_facts::Num::string      : q.kind = Io_kind::string; break;
          case upass::decl_facts::Num::none        : break;
        }
        if (q.kind == Io_kind::none && (f->range_max || f->range_min)) {
          q.kind = Io_kind::integer;
        }
      }
      q.range_max = f->range_max;
      q.range_min = f->range_min;
      q.annotated = f->range_max.has_value() || f->range_min.has_value();
    }
    return q;
  }

  // True when `name` (root, dotted paths collapse to their first level) is a
  // declared OUTPUT of the unit under fold (io_meta is populated by the SSA
  // upass before constprop runs). Outputs are the unit's boundary contract:
  // their stores must always materialize (see classify_statement_impl).
  bool is_io_output(std::string_view name) const {
    const auto& ln = lm ? lm->get_lnast() : nullptr;
    if (!ln) {
      return false;
    }
    const auto root = Bundle::get_first_level(name);
    for (const auto& oe : ln->io_meta().outputs) {
      if (oe.name == root) {
        return true;
      }
    }
    return false;
  }

  // Poison-nil propagation for the cassert-discharge ops (eq/ne, log_*).
  // When any ref operand is an uncertainty-pinned var (or a temp this rule
  // already marked), the result is the same indeterminate nil: store it AND
  // mark the dst, so
  //   (a) the verifier still discharges casserts over the chain (the nil
  //       reads through known_const_scalar), and
  //   (b) value consumers keep the producer (classify's marked-dst gate),
  //       never substitute the nil (emit_ref_or_folded refuses nils), and
  //       the runner treats a nil if-cond as unknown — so tolg receives the
  //       real wires instead of a dangling ref.
  bool propagate_uncertain_nil(std::string_view dst, upass::Src_span src) {
    if (dst.empty()) {
      return false;
    }
    for (const auto& o : src) {
      if (!o.name.empty() && st().is_uncertain_nil(o.name)) {
        store_trivial(dst, *Dlop::nil());
        st().mark_uncertain_nil(dst);  // after the store — set() clears marks
        return true;
      }
    }
    return false;
  }

  // Push-form operand value. Mirrors current_prim_value: the
  // cross-pass fold override wins (wrap/sat narrowed values, attr-derived
  // tmps — all of which land on the table),
  // then a scalar bundle flattens, then the stored trivial. Dlop operands
  // carry their parsed value in the resolver's make_const bundle.
  // An uncertainty-pinned var reads as INVALID here: its nil is a poison
  // marker for a runtime-divergent value (mux of if-arm writes), so value
  // folds must not consume it — the ref stays and tolg wires the real
  // producer. The discharge ops (eq/ne, log_*) handle marked operands BEFORE
  // calling the push_* templates via propagate_uncertain_nil above.
  Dlop operand_value(const upass::Operand& o) {
    if (!o.name.empty()) {
      if (st().is_uncertain_nil(o.name)) {
        return Dlop();
      }
      // Cross-pass folds land on the table — read it directly.
      if (auto b = st().get_bundle(o.name); b && b->is_scalar()) {
        if (auto bv = b->lone_trivial(); !bv.is_invalid()) {
          return bv;
        }
      }
      return st().get_trivial(o.name);
    }
    return o.bundle ? o.bundle->lone_trivial() : Dlop();
  }

  // Push-form fold templates (the cursor-walking originals below die with
  // the cursor-walking originals).
  template <typename F>
  upass::Vote push_nary(std::string_view dst, upass::Src_span src, F op) {
    if (dst.empty() || src.empty()) {
      return upass::Vote::keep;
    }
    Dlop r = operand_value(src[0]);
    if (!is_numeric(r)) {
      return upass::Vote::keep;
    }
    for (size_t i = 1; i < src.size(); ++i) {
      auto operand = operand_value(src[i]);
      if (!is_numeric(operand)) {
        return upass::Vote::keep;
      }
      op(r, operand);
    }
    if (!r.is_invalid()) {
      store_trivial(dst, r);
    }
    return classify_vote();
  }
  template <typename F>
  upass::Vote push_binary_passthrough(std::string_view dst, upass::Src_span src, F op) {
    if (dst.empty() || src.size() < 2) {
      return upass::Vote::keep;
    }
    Dlop n1 = operand_value(src[0]);
    Dlop n2 = operand_value(src[1]);
    if (!is_numeric(n1) || !is_numeric(n2)) {
      return upass::Vote::keep;
    }
    Dlop r = op(n1, n2);
    if (!r.is_invalid()) {
      store_trivial(dst, r);
    }
    return classify_vote();
  }
  template <typename F>
  upass::Vote push_unary(std::string_view dst, upass::Src_span src, F op) {
    if (dst.empty() || src.empty()) {
      return upass::Vote::keep;
    }
    Dlop r = operand_value(src[0]);
    if (!is_numeric(r)) {
      return upass::Vote::keep;
    }
    op(r);
    if (!r.is_invalid()) {
      store_trivial(dst, r);
    }
    return classify_vote();
  }
  template <typename F>
  upass::Vote push_reduction(std::string_view dst, upass::Src_span src, F op) {
    if (dst.empty() || src.empty()) {
      return upass::Vote::keep;
    }
    Dlop v = operand_value(src[0]);
    if (!is_numeric(v)) {
      return upass::Vote::keep;
    }
    Dlop r = op(v);
    if (!r.is_invalid()) {
      store_trivial(dst, r);
    }
    return classify_vote();
  }

  auto current_prim_value() const {
    if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      auto name = current_text();
      // Uncertainty-pinned poison nil never folds as a value (see
      // operand_value above) — return invalid so the caller's foldable()
      // gate keeps the ref alive.
      if (st().is_uncertain_nil(name)) {
        return Dlop();
      }
      // cross-pass folds (wrap/sat narrowing on call-dst tmps,
      // attr_get results, `is`) land on the table now — no seam read.
      // Single-entry bundle: a parenthesized scalar `(expr)` lowers to a
      // 1-element tuple_add (e.g. `!(p is yy)`); flatten to its lone value so
      // unary/nary ops fold over it (mirrors process_eq_ne_impl::resolve).
      if (auto b = st().get_bundle(name); b && b->is_scalar()) {
        if (auto bv = b->lone_trivial(); !bv.is_invalid()) {
          return bv;
        }
      }
      const auto v = st().get_trivial(name);
      return v;  // may be invalid — caller's foldable() check short-circuits.
    }
    I(is_type(Lnast_ntype::Lnast_ntype_const));
    return *Dlop::from_pyrope(current_text());
  }

  // Predicates for the standard "is this value foldable?" guard. `is_numeric`
  // allows X-bit unknowns through (n-ary bitwise ops propagate `?` bits);
  // `foldable` is the strict version used everywhere else.
  static bool is_numeric(const Dlop& v) { return !v.is_invalid() && !v.is_string(); }
  static bool foldable(const Dlop& v) { return is_numeric(v) && !v.has_unknowns(); }

  // True while folding a deferred-template body (Lnast::is_template — an
  // extracted lambda with unbound params/var-args, stamped by func_extract).
  // There, unbound params fold nil-derived placeholder values, so value-level
  // diagnostics (descending range, concat overlap, …) would be optimization
  // artifacts, not user mistakes — the genuine error resurfaces when the body
  // is realized (inlined/specialized) at a real call site. Fully-typed
  // extracted bodies are NOT exempt: whatever folds there is a real
  // compile-time fact.
  bool in_template_body() const {
    const auto& ln = lm->get_lnast();
    return ln && ln->is_template();
  }

  // Type-agnostic structural identity: same base+extra+size, but type may
  // differ. Used by process_eq_ne to fold `(v != 0) == 0sb?` (Boolean unknown
  // vs Integer unknown with identical bit patterns) to known-true. Dlop's
  // same_repr requires matching `type`, so it can't catch this case.
  static bool same_bits_ignore_type(const Dlop& a, const Dlop& b) {
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
  // Non-static: emits a negative-index / descending-range compile error
  // (via lm's span + in_template_body gate) before the degenerate fold.
  Dlop apply_range_mask(const Dlop& value, const Dlop& start, const Dlop& end);

  // Single-shot "store result only when the value actually changed".
  // The has_trivial/get_trivial!=/set dance was repeated at every fold site;
  // centralising it kills a bug-prone pattern.
  void store_trivial(std::string_view name, const Dlop& v) {
    if (!st().has_trivial(name) || !st().get_trivial(name).same_repr(v)) {
      st().set(name, v);
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
  upass::Vote process_eq_ne_impl(std::string_view dst_name, upass::Src_span srcs);

  void fold_does(const std::string& dst);
  void fold_in(const std::string& dst);
  void fold_has(const std::string& dst);
  void fold_case(const std::string& dst);

  // Type-aware `does`/`equals`. A resolved operand carries its scalar
  // KIND plus, for integers, a (max,min) ENVELOPE (with explicit unbounded
  // flags) and — when it has a concrete value — the literal/folded Dlop used
  // to coerce a scalar into a single-positional bundle for the structural
  // (tuple) path. `bundle` is set only for real tuples.
  struct Does_operand {
    enum class Kind : uint8_t { integer, boolean, string, tuple, nil, unknown };
    Kind                          kind    = Kind::unknown;
    bool                          max_inf = false;  // envelope max is +∞
    bool                          min_inf = false;  // envelope min is −∞
    Dlop                          max;
    Dlop                          min;
    bool                          has_value = false;
    Dlop                          value;   // valid when has_value
    std::shared_ptr<Bundle const> bundle;  // set when kind==tuple
    // The TABLE name the bundle was resolved under (Bundle::name is
    // gone); resolve_field_operand's dotted declared-type query keys on it.
    std::string                   name;
  };
  // Resolve the `does`/`equals`/`case` operand at the current cursor (a ref or
  // const). nullopt when undecidable this walk (defer the fold).
  std::optional<Does_operand>        resolve_does_operand();
  // Build a scalar Does_operand from a declared type query plus an optional
  // folded value (shared by the ref-operand and per-field paths).
  static Does_operand                build_scalar_operand(const upass::uPass::Scalar_type_query& q, const Dlop& folded);
  // Resolve one named/positional field of a bundle to a Does_operand for the
  // per-field type check: declared type via the dotted query
  // (`bundle.field`), value via the bundle entry, sub-bundle for nested tuples.
  // `declared_only` (used by the per-field type check) returns nullopt for a
  // scalar field with NO explicit declared type — an untyped field imposes no
  // type constraint (`m1 does (a:u32)` passes; `s case (a=0)` stays a value
  // pattern), so the check runs only when BOTH sides are declared-typed.
  std::optional<Does_operand>        resolve_field_operand(std::string_view base, const Bundle& b, std::string_view field,
                                                           bool declared_only);
  // Decode a primitive type token (`u32`/`s8`/`int`/`bool`/`string`/…) in
  // operand position to its kind+envelope. Returns nullopt if `name` is not a
  // type token.
  static std::optional<Does_operand> decode_prim_type_token(std::string_view name);
  // Build a one-entry positional bundle {0: value} so a scalar operand can take
  // the structural path against a real tuple (`(100,30) does 30`).
  static std::shared_ptr<Bundle>     single_positional_bundle(const Dlop& v);
  // Tri-state kind+envelope `a does b` for two NON-tuple operands. nullopt =
  // undecidable.
  static std::optional<bool>         scalar_does(const Does_operand& a, const Does_operand& b);
  // Tri-state `a does b` for any two resolved operands: structural (tuple)
  // path when a side is a real tuple (plus per-field type checks), else
  // scalar_does. nullopt = undecidable. `equals` is this both ways.
  std::optional<bool>                compute_does(const Does_operand& a, const Does_operand& b);

  // The actual-collection moved to the runner-owned resolver
  // (upass/core/call_resolver.hpp); the alias keeps every consumer
  // (inliner binding, cell folds, import handling) source-compatible.
  using Call_actual = upass::call_resolver::Call_actual;

  static inline std::unordered_map<std::string, std::shared_ptr<Lnast>> function_registry;

  std::optional<Dlop>                     resolve_current_scalar() const;
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
