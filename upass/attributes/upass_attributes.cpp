//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_attributes.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "lnast_ntype.hpp"
#include "upass_attributes_sticky.hpp"
#include "upass_attributes_wrap_sat.hpp"

// Plugin registration. Name "attributes" is what pass_upass appends to the
// default upass order.
static upass::uPass_plugin plugin_attributes("attributes", upass::uPass_wrapper<uPass_attributes>::get_upass);

// Defined in upass_attributes_wiring.cpp.
void uPass_attributes_register_wiring(uPass_attributes& self);

uPass_attributes::uPass_attributes(std::shared_ptr<upass::Lnast_manager>& _lm) : upass::uPass(_lm) {
  // Sticky `_*` / `debug` attribute pattern.
  reg.register_sticky_pattern(std::make_shared<upass::attributes::Sticky_handler>());

  // Category-A consumption: const single-bind enforcement (type=const).
  // wrap/sat are no longer attributes — Task 1t lowers them to a
  // `wrap|sat(v=…, type=…)` call handled in process_func_call.
  reg.register_exact("type", std::make_shared<upass::attributes::Const_handler>());

  // Category-B LGraph-wiring handlers (clock_pin, reset_pin, posclk, ...).
  // Lives in its own translation unit so the table of pin/mode names doesn't
  // clutter constructor logic.
  uPass_attributes_register_wiring(*this);

  // Default pass-through handler. Unknown attribute names land here. The
  // handler is intentionally a no-op: process_attr_set already records every
  // value into attr_set_values regardless of name, so cat-D attrs round-trip
  // via the side-map for downstream LGraph generation.
  reg.register_default(std::make_shared<upass::attributes::Attribute_handler>());
}

void uPass_attributes::begin_iteration() {
  pending_arms.clear();
  active_arm_stack.clear();
  // Typed io PORT facts are read straight from io_meta()
  // by lookup_type_info_bundle (no per-walk recording; ports are never
  // table-backed values, so their declared kind/bits stay metadata-only).
  reg.for_each_handler([this](upass::attributes::Attribute_handler& h) { h.begin_iteration(*this); });
}

void uPass_attributes::end_run() {
  reg.for_each_handler([this](upass::attributes::Attribute_handler& h) { h.end_run(*this); });
}

std::string uPass_attributes::normalize_name(std::string_view name) { return std::string{name}; }

uPass_attributes::Op_view uPass_attributes::scan_op() {
  Op_view view;

  // Task 1t — a 0-level `store` reaching here (routed through process_assign)
  // is the scalar-write/alias shape, exactly like `assign`.
  const bool is_assign = is_type(Lnast_ntype::Lnast_ntype_store) || is_type(Lnast_ntype::Lnast_ntype_store);

  if (!move_to_child()) {
    return view;
  }
  view.lhs = current_text();

  std::size_t ref_count = 0;
  while (move_to_sibling()) {
    if (Lnast_ntype::is_ref(get_raw_ntype())) {
      view.rhs_refs.emplace_back(current_text());
      ++ref_count;
    }
  }
  move_to_parent();

  if (is_assign && view.rhs_refs.size() == 1 && ref_count == 1) {
    view.is_alias = true;
  }
  return view;
}

void uPass_attributes::dispatch_attr_set(std::string_view attr_name, std::string_view lhs, std::string_view value_text) {
  auto* h = reg.lookup(attr_name);
  if (!h) {
    return;
  }
  dispatch_bucket = upass::attributes::Sticky_handler::canonical_bucket(attr_name);
  h->on_attr_set(*this, lhs, value_text);
  dispatch_bucket.clear();
}

void uPass_attributes::dispatch_attr_get(std::string_view attr_name, std::string_view dst, std::string_view base) {
  auto* h = reg.lookup(attr_name);
  if (!h) {
    return;
  }
  dispatch_bucket = upass::attributes::Sticky_handler::canonical_bucket(attr_name);
  h->on_attr_get(*this, dst, base);
  dispatch_bucket.clear();
}

void uPass_attributes::on_assign_like(bool is_assign_node) {
  // Hot-path fast exit for the bulk-arithmetic case (process_plus / minus /
  // … on a tmp LHS with no narrowed/sticky/wrap/sat/type-info state in
  // scope). All downstream branches in this function short-circuit when
  // those state maps are empty, so the scan_op walk is wasted. Skipping it
  // shaves ~10–15% off the xx.prp 1M-op run.
  //
  // Conditions for non-assign ops (process_plus / minus / eq / …):
  //  - lhs is always a fresh tmp (e.g. `plus ___N a b`), so record_assign
  //    short-circuits on the ti==nullptr fast-path and the alias branch
  //    is unreachable (is_alias requires is_assign).
  //  - sticky inert: on_expr_assign returns immediately when no var has
  //    acquired a bucket and no control taint is active.
  //  - tmp_fold empty: nothing for the alias-branch tmp_fold lookup to find
  //    (the alias-branch is unreachable anyway since is_alias is false).
  // Keep is_assign_node on the slow path — `assign a ___N` updates
  // assigned_once for vars with type_info plus drives the alias-branch
  // shape inheritance, neither of which is safe to elide.
  if (!is_assign_node) {
    auto* sticky = static_cast<upass::attributes::Sticky_handler*>(reg.sticky_handler());
    if (sticky && sticky->is_inert() && tmp_fold.empty()) {
      return;
    }
  }
  auto view = scan_op();
  if (view.lhs.empty()) {
    return;
  }
  // Phase 5 — bookkeeping for any assignment-shaped op. The `nil` rvalue
  // is an explicit invalidation per spec and is not counted as a binding.
  // Detect by checking the single rhs ref or const text.
  bool                 rhs_is_nil = false;
  std::optional<Dlop> rhs_value;  // direct LNAST-source value, when scalar

  // rhs_value is only consumed by the wrap/sat narrowing block below, which
  // requires a policy on view.lhs. If no wrap/sat policy is in scope at all,
  // resolving rhs_value through runner_fold_fn is wasted work — skip the
  // Dlop parse / fold lookup on the bulk-arithmetic path.
  // Task 1t — also materialize the RHS value when the LHS has an unsigned
  // declared type, so a known-negative comptime literal can be coerced to its
  // unsigned bit pattern (e.g. `v:u8 = 0sb1001_0111` ⇒ 151). Checked via the
  // already-resolved view.lhs; null/typed-tmp LHS keeps the bulk fast path.
  const auto* lhs_ti_for_coerce = is_assign_node ? lookup_type_info(view.lhs) : nullptr;
  const bool  lhs_unsigned      = lhs_ti_for_coerce != nullptr && lhs_ti_for_coerce->has_type_spec
                                  && lhs_ti_for_coerce->kind == Numeric_kind::unsigned_int && lhs_ti_for_coerce->bits != 0;
  // Task 1b — also materialize the RHS for a SIGNED-int / bounded LHS so the
  // first-write range-fit check (below) can run on it. An unbounded `int`/
  // `uint` (no concrete width or range) has no envelope, so it stays on the
  // fast path (and a present-but-nil range Dlop must not be treated as a
  // bound — that crashes Dlop arithmetic).
  const bool  lhs_signed_bounded
      = lhs_ti_for_coerce != nullptr && lhs_ti_for_coerce->has_type_spec && lhs_ti_for_coerce->kind == Numeric_kind::signed_int
        && (lhs_ti_for_coerce->bits != 0 || (lhs_ti_for_coerce->range_max && lhs_ti_for_coerce->range_max->is_integer())
            || (lhs_ti_for_coerce->range_min && lhs_ti_for_coerce->range_min->is_integer()));
  const bool need_rhs_value = is_assign_node && (lhs_unsigned || lhs_signed_bounded);
  if (is_assign_node) {
    move_to_child();
    if (move_to_sibling()) {
      auto txt = current_text();
      if (txt == "nil") {
        rhs_is_nil = true;
      }
      if (need_rhs_value && !rhs_is_nil) {
        if (Lnast_ntype::is_const(get_raw_ntype())) {
          auto v = Dlop::from_pyrope(txt);
          if (!v->is_invalid()) {
            rhs_value = *v;
          }
        } else if (Lnast_ntype::is_ref(get_raw_ntype()) && runner_st != nullptr) {
          auto folded = runner_st->known_const_scalar(txt);
          if (folded && !folded->is_invalid()) {
            rhs_value = *folded;
          }
        }
      }
    }
    move_to_parent();
  }
  // Phase 5 — apply wrap/sat narrowing eagerly when we have a fresh RHS
  // value in hand. record_assign also tries this via runner_fold_fn, but
  // that lookup falls back to constprop's ST which has not yet seen this
  // statement's RHS at the moment our process_assign runs
  // (constprop's process_assign fires AFTER ours in pass order). Reading
  // the RHS directly from the LNAST gets us the right input.
  // tmp_fold is empty on bulk-arithmetic workloads (no narrowing has fired
  // yet). Cheap pre-check: skip the std::string{} alloc for erase() when
  // the map is empty.
  if (!view.lhs.empty() && is_assign_node && !rhs_is_nil && !tmp_fold.empty()) {
    // Drop any stale narrowed value from a previous assign so a per-
    // statement wrap/sat applied to the next attr_set picks up the fresh
    // constprop-stored RHS via runner_fold_fn instead of a stale narrowed
    // result from an earlier assign.
    tmp_fold.erase(std::string{view.lhs});
  }
  if (lhs_unsigned && !was_assigned(view.lhs) && rhs_value && rhs_value->bit_test(static_cast<int>(lhs_ti_for_coerce->bits))
      && !rhs_value->unknown_bit_test(static_cast<int>(lhs_ti_for_coerce->bits))) {
    // `!was_assigned` restricts this to the FIRST write (the declaration's
    // initializer). Per-statement `wrap x = …` / `sat x = …` are always
    // reassignments (x was declared+initialized first) and emit their
    // wrap/sat attr_set AFTER this store — so coercing here would pre-mask the
    // value out from under sat (e.g. `sat z = <neg>` must clamp to 0, not mask
    // to its low bits). Skipping reassignments leaves wrap/sat in control.
    // Task 1t — implicit type coercion: a comptime value that sign-extends a
    // KNOWN 1 past the declared width (i.e. a known-negative bit pattern, even
    // with interior unknowns) stored to an unsigned-typed var reads as its
    // unsigned N-bit pattern. `v:u8 = 0sb1001_0111` ⇒ 151, and
    // `v:u8 = 0sb1?01_?000` ⇒ 0ub1?01?000 (so `v | 0xff == 0xff`). This is the
    // implicit (sign-dropping) "force"; task 1b prefers the explicit bit-select
    // spelling `v:u8 = e#[0..=7]`.
    //   * `bit_test(bits)` true + `unknown_bit_test(bits)` false = the
    //     sign-extension past width N is a known 1. An unknown sign bit
    //     (`0sb?` → `v1:u32`) has `unknown_bit_test` true and is SKIPPED, so a
    //     deliberate 1-bit unknown keeps its natural width (see valid_simple).
    //   * Uses and_op (NOT wrap_to_unsigned, which bails on any unknown) so the
    //     interior-unknown case coerces while the known sign bits clear.
    // Wrap/sat policy takes precedence (handled above).
    if (rhs_value->is_negative()) {
      // Known-negative bit pattern → reinterpret as the unsigned N-bit value.
      Dlop out = *rhs_value->and_op(*Dlop::get_mask_value(static_cast<int>(lhs_ti_for_coerce->bits)));
      if (!out.is_invalid() && !out.same_repr(*rhs_value)) {
        auto [iter, inserted] = tmp_fold.emplace(view.lhs, out);
        if (!inserted && !iter->second.same_repr(out)) {
          iter->second = out;
        }
      }
    }
    // (Goal 1n) Overflow capture moved to upass/bitwidth (centralized): bitwidth
    // checks the value range against the declared envelope at end_run, for both
    // signed and unsigned types. The two former checks here — a positive
    // comptime value exceeding an unsigned width, and a signed value outside its
    // declared (max,min) range — are gone. The negative-bit-pattern → unsigned
    // reinterpret above STAYS (it rewrites the value to its unsigned form, which
    // then fits the envelope and is not an overflow).
  }
  record_assign(view.lhs, rhs_is_nil);

  if (view.is_alias) {
    // Phase 3 — shape / typename / range inheritance through direct aliases.
    // When `assign foo bar` and bar carries a tuple shape (or range), foo
    // takes on the same shape so aggregate reads `foo.[size]` etc. resolve.
    const auto&       rhs = view.rhs_refs.front();
    // Fast path: when the rhs has no tracked state anywhere AND sticky has
    // nothing to propagate AND there's no chained narrowing value, every
    // arm of the alias body is a no-op. Skip — saves the migrate_alias
    // direct_alias.emplace + chained lookups on bulk `assign user_var
    // ___N` workloads where ___N is a pure plus tmp.
    const std::string rhs_s{rhs};
    // Shape/attr/alias state rides the shared binding slot; the
    // only alias bookkeeping left is the range-source chain (h.[size] on a
    // var assigned from a `range` tmp) and the handler notification.
    const bool rhs_has_state = range_bounds.contains(rhs_s) || (!tmp_fold.empty() && tmp_fold.contains(rhs_s));
    if (!rhs_has_state) {
      reg.for_each_handler([&](upass::attributes::Attribute_handler& h) { h.on_alias_assign(*this, view.lhs, rhs); });
      return;
    }
    if (lookup_range(rhs)) {
      // Range tmp aliased into a named var — remember the source so
      // `h.[size]` chains back to range_bounds[___16].
      auto [it, inserted] = range_source.emplace(view.lhs, rhs_s);
      if (!inserted && it->second != rhs_s) {
        it->second = rhs_s;
      }
    }
    // Phase 5 — propagate any narrowed value from rhs to lhs so a downstream
    // `lhs == k` (e.g. after `sat z = 3000` then `const m = z`) sees the
    // narrowed value via runner_fold_fn instead of the raw bundle in
    // constprop's symbol table.
    // tmp_fold is std::map<std::string, …> without transparent comparator —
    // materialize one std::string here for the lookup. (The alias path is
    // already off the hot 1M-ops loop; the n-ary path skips this entirely.)
    if (auto it = tmp_fold.find(std::string{rhs}); it != tmp_fold.end() && !it->second.is_invalid()) {
      auto [m_it, inserted] = tmp_fold.emplace(view.lhs, it->second);
      if (!inserted && !m_it->second.same_repr(it->second)) {
        m_it->second = it->second;
      }
    }
    reg.for_each_handler(
        [&](upass::attributes::Attribute_handler& h) { h.on_alias_assign(*this, view.lhs, view.rhs_refs.front()); });
    return;
  }
  // view.rhs_refs is already a contiguous span of string_views — pass
  // straight through; no intermediate vector materialization.
  reg.for_each_handler([&](upass::attributes::Attribute_handler& h) { h.on_expr_assign(*this, view.lhs, view.rhs_refs); });
}

void uPass_attributes::process_assign() { on_assign_like(/*is_assign_node=*/true); }

// Store routes both arities to the cursor-walking legacy bodies
// (scan_op reads the node; subtree payloads don't ride the operand span).
upass::Vote uPass_attributes::process_store(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  (void)dst_name;
  (void)dst;
  if (src.size() <= 1) {
    process_assign();
  } else {
    process_tuple_set();
  }
  return upass::Vote::keep;
}

#define EXPR_PROCESS(NAME)                                                                              \
  upass::Vote uPass_attributes::process_##NAME(std::string_view dst_name, Bundle& dst, upass::Src_span src) { \
    (void)dst_name;                                                                                     \
    (void)dst;                                                                                          \
    (void)src;                                                                                          \
    on_assign_like(/*is_assign_node=*/false);                                                           \
    return upass::Vote::keep;                                                                           \
  }

EXPR_PROCESS(plus)
EXPR_PROCESS(minus)
EXPR_PROCESS(mult)
EXPR_PROCESS(div)
EXPR_PROCESS(mod)
EXPR_PROCESS(shl)
EXPR_PROCESS(sra)
EXPR_PROCESS(bit_and)
EXPR_PROCESS(bit_or)
EXPR_PROCESS(bit_not)
EXPR_PROCESS(bit_xor)
EXPR_PROCESS(log_and)
EXPR_PROCESS(log_or)
EXPR_PROCESS(log_not)
EXPR_PROCESS(red_or)
EXPR_PROCESS(red_and)
EXPR_PROCESS(red_xor)
EXPR_PROCESS(popcount)
EXPR_PROCESS(ne)
EXPR_PROCESS(eq)
EXPR_PROCESS(lt)
EXPR_PROCESS(le)
EXPR_PROCESS(gt)
EXPR_PROCESS(ge)
EXPR_PROCESS(sext)
EXPR_PROCESS(get_mask)
EXPR_PROCESS(set_mask)

#undef EXPR_PROCESS

upass::Vote uPass_attributes::process_is(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  // `p is xx` — nominal type identity. Compare `lookup_attr_value(p,
  // "typename")` against the rhs ref's textual name. Folds to 1/0;
  // when LHS has no recorded typename, folds to 0 (only NAMED-type
  // values match). (Push wrapper: body walks the cursor.)
  (void)dst_name;
  (void)dst;
  (void)src;
  on_assign_like(/*is_assign_node=*/false);

  if (!move_to_child()) {
    return upass::Vote::keep;
  }
  auto dvar = normalize_name(current_text());
  if (!move_to_sibling()) {
    move_to_parent();
    return upass::Vote::keep;
  }
  auto lhs = normalize_name(current_text());
  if (!move_to_sibling()) {
    move_to_parent();
    return upass::Vote::keep;
  }
  auto rhs_text = std::string{current_text()};
  move_to_parent();

  if (dvar.empty() || lhs.empty() || rhs_text.empty()) {
    return upass::Vote::keep;
  }

  std::optional<Dlop> tn    = lookup_attr_value(lhs, "typename");
  bool                 match = false;
  if (tn) {
    auto        repr = tn->to_pyrope();
    std::string stored
        = (repr.size() >= 2 && repr.front() == '\'' && repr.back() == '\'') ? std::string{repr.substr(1, repr.size() - 2)} : repr;
    match = (stored == rhs_text);
  }
  Dlop folded        = match ? *Dlop::create_integer(1) : *Dlop::create_integer(0);
  auto [it, inserted] = tmp_fold.emplace(std::string{dvar}, folded);
  if (!inserted && !it->second.same_repr(folded)) {
    it->second = folded;
  }
  // The fold IS the `is` dst's value; write the binding too (dst is
  // a single-writer tmp constprop keeps alive but never folds), so table-only
  // operand resolution sees it without the runner_fold_fn pull seam.
  if (runner_st != nullptr) {
    (void)runner_st->set(dvar, folded);
  }
  return upass::Vote::keep;
}

void uPass_attributes::set_binding_attr(std::string_view target, std::string_view attr, const Dlop& v) {
  if (runner_st == nullptr || target.empty()) {
    return;
  }
  const auto root  = Bundle::get_first_level(target);
  const auto fpath = Bundle::get_all_but_first_level(target);
  auto       wb    = runner_st->get_bundle_for_write(root);
  if (!wb) {
    (void)runner_st->set(std::string(root), std::make_shared<Bundle>(std::string(root)));
    wb = runner_st->get_bundle_for_write(root);
    if (!wb) {
      return;
    }
  }
  // Attrs are ADD-ONLY once present: re-setting the SAME value is
  // fine (sticky re-marks, loop re-walks), a DIFFERENT value is a
  // diagnostic — silent overwrite is forbidden.
  const auto& existing = fpath.empty() ? wb->get_attr(attr) : wb->get_attr(fpath, attr);
  if (!existing.is_invalid() && !existing.same_repr(v)) {
    livehd::diag::sink().emit(livehd::diag::Diagnostic{
        .severity = livehd::diag::Severity::error,
        .code     = "attr-conflict",
        .category = "attributes",
        .pass     = "upass.attributes",
        .message  = std::format("attribute `{}` of `{}` is already set to a different value (attributes are add-only)",
                                attr, target),
        .hint     = "attributes bind once; remove the second assignment or use a different attribute",
    });
    return;
  }
  if (fpath.empty()) {
    wb->set_attr(attr, v);
  } else {
    wb->set_attr(fpath, attr, v);
  }
  // Per-field decl-site attr on an extraction tmp (`b::[poison=99]=2` inside
  // a literal lowers to `tuple_get tmp ___1 b ; attr_set tmp poison 99`):
  // echo onto the source field so aggregate reads (`t.b.[poison]`) resolve.
  if (fpath.empty()) {
    if (auto oit = runner_st->tget_origin.find(std::string(root)); oit != runner_st->tget_origin.end()) {
      const auto src_root  = Bundle::get_first_level(oit->second);
      const auto src_fpath = Bundle::get_all_but_first_level(oit->second);
      if (!src_fpath.empty()) {
        if (auto sb = runner_st->get_bundle_for_write(src_root); sb) {
          sb->set_attr(src_fpath, attr, v);
        }
      }
    }
  }
}

void uPass_attributes::process_attr_set() {
  // Layout (see prp2lnast::emit_attribute_list):
  //   attr_set
  //     ref(target)
  //     const(attr_name)
  //     <value: const | ref>
  if (!move_to_child()) {
    return;
  }
  auto target = normalize_name(current_text());
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  std::string attr_name{current_text()};
  std::string value_text;
  bool        value_is_ref = false;
  if (move_to_sibling()) {
    value_text   = std::string{current_text()};
    value_is_ref = Lnast_ntype::is_ref(get_raw_ntype());
  }
  move_to_parent();

  // Phase 2 side-state writes — keep these BEFORE dispatch so handlers can
  // see the updated maps.
  if (!target.empty() && !attr_name.empty()) {
    if (attr_name == "type") {
      Decl_kind kind = Decl_kind::unknown;
      if (value_text == "mut") {
        kind = Decl_kind::mut_kind;
      } else if (value_text == "const") {
        kind = Decl_kind::const_kind;
      } else if (value_text == "reg") {
        kind = Decl_kind::reg_kind;
      } else if (value_text == "await") {
        kind = Decl_kind::await_kind;
      } else if (value_text == "type") {
        kind = Decl_kind::type_kind;  // task 1k — see Decl_kind::type_kind
      }
      if (kind != Decl_kind::unknown) {
        // Per-field storage class onto the binding (the
        // field Entry's mode for dotted targets; bundle mode for bare names).
        // Existing entries only — minting a fact-only entry would read as a
        // value claim downstream.
        if (runner_st != nullptr) {
          upass::Mode m = upass::Mode::unknown;
          switch (kind) {
            case Decl_kind::mut_kind  : m = upass::Mode::mut_kind; break;
            case Decl_kind::const_kind: m = upass::Mode::const_kind; break;
            case Decl_kind::reg_kind  : m = upass::Mode::reg_kind; break;
            case Decl_kind::await_kind: m = upass::Mode::await_kind; break;
            case Decl_kind::type_kind : m = upass::Mode::type_kind; break;
            default: break;
          }
          const auto root  = Bundle::get_first_level(target);
          const auto fpath = Bundle::get_all_but_first_level(target);
          if (auto wb = runner_st->get_bundle_for_write(root); wb && m != upass::Mode::unknown) {
            if (fpath.empty()) {
              if (wb->get_mode() == upass::Mode::unknown) {
                wb->set_mode(m);
              }
            } else if (wb->has_trivial(fpath)) {
              Bundle::Entry fe = wb->get_entry(fpath);
              fe.immutable     = false;
              if (fe.mode == upass::Mode::unknown) {
                fe.mode = m;
              }
              wb->set(fpath, std::move(fe));
            }
          }
        }
      }
    } else if (attr_name == "comptime") {
      if (value_text != "false" && value_text != "0") {
        // Comptime onto the binding's entry (existing
        // scalar entries only — same no-fact-only-entry rule as above).
        if (runner_st != nullptr) {
          const auto root  = Bundle::get_first_level(target);
          const auto fpath = Bundle::get_all_but_first_level(target);
          if (auto wb = runner_st->get_bundle_for_write(root); wb) {
            const auto           key   = fpath.empty() ? std::string_view{"0"} : fpath;
            const bool           multi = fpath.empty() && (wb->has_named_top() || wb->unnamed_top_count() > 1);
            if (!multi && wb->has_trivial(key)) {
              Bundle::Entry fe = wb->get_entry(key);
              fe.immutable     = false;
              fe.comptime      = true;
              wb->set(key, std::move(fe));
            }
          }
        }
      }
      // Still record the explicit value (true/false) so a `.[comptime]`
      // read returns the explicit answer.
      set_binding_attr(target, attr_name,
                       (value_text == "false" || value_text == "0") ? *Dlop::create_integer(0) : *Dlop::create_integer(1));
    } else if (attr_name == "bits" && !value_is_ref) {
      // `[bits=N]` — explicit width, no signedness. Recorded as an attr
      // value; lookup_type_info_bundle folds it into the answer (`uN`/`sN`
      // sugar lowers to `prim_type_int(max,min)` directly since Task 1t).
      auto v = Dlop::from_pyrope(value_text);
      if (v->is_just_i64()) {
        set_binding_attr(target, attr_name, *v);
      }
    } else if (value_is_ref) {
      // Refs (e.g. a runtime wire ref for clock_pin): the LGraph wiring pass
      // resolves the TEXT by name — stored as a string Dlop residual attr.
      set_binding_attr(target, absl::StrCat(attr_name, "_refname"), *Dlop::from_string(normalize_name(value_text)));
    } else {
      Dlop stored;
      if (value_text.empty() || value_text == "true") {
        stored = Dlop::create_integer(1);
      } else {
        stored = Dlop::from_pyrope(value_text);
      }
      if (!stored.is_invalid()) {
        // Onto the binding; the extraction-tmp echo inside
        // set_binding_attr covers per-field decl-site attrs, and the
        // field→root fallback in lookup_attr_value covers aggregate→field
        // inheritance (no pre-copies).
        set_binding_attr(target, attr_name, stored);
        // Typename-driven attribute inheritance — `const x:a = 100` lowers
        // to `attr_set x typename 'a'`, and per spec x takes on a's
        // attribute set. When the typename string matches a known variable
        // with tracked attrs, treat it as a direct alias for attribute
        // lookup: copy attrs through migrate_alias and notify handlers so
        // sticky buckets propagate too.
        if (attr_name == "typename" && value_text.size() >= 2 && value_text.front() == '\'' && value_text.back() == '\'') {
          std::string src{value_text.substr(1, value_text.size() - 2)};
          const auto src_b = (runner_st != nullptr && !src.empty()) ? runner_st->get_bundle(src) : nullptr;
          if (!src.empty() && src != target
              && ((src_b && !src_b->get_attrs().empty()) || lookup_type_info(src) != nullptr)) {
            // Attr values ride the binding: copy the type var's attrs onto
            // the target's binding (fill-if-absent), then notify handlers.
            if (src_b) {
              if (auto tb = runner_st->get_bundle_for_write(target); tb) {
                for (const auto& [k, e] : src_b->get_attrs()) {
                  // Bind-tracking is a NAME fact of the SOURCE, never
                  // inherited (a used type var's "vbound" would make the
                  // target's first store read as a const rebind).
                  const auto dot  = k.rfind('.');
                  const auto leaf = dot == std::string::npos ? std::string_view{k} : std::string_view{k}.substr(dot + 1);
                  if (leaf == "vbound") {
                    continue;
                  }
                  if (tb->get_attrs().find(k) == tb->get_attrs().end()) {
                    tb->set_attr(k, e.trivial);
                  }
                }
              }
            }
            reg.for_each_handler([&](upass::attributes::Attribute_handler& h) { h.on_alias_assign(*this, target, src); });
          }
        }
      }
    }
  }

  dispatch_attr_set(attr_name, target, value_text);
}

void uPass_attributes::process_attr_get() {
  // Layout (see prp2lnast::attribute_read_to_node):
  //   attr_get
  //     ref(dst)
  //     ref(base)
  //     const(attr_name)
  if (!move_to_child()) {
    return;
  }
  auto dst = normalize_name(current_text());
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  std::string base_text{current_text()};
  auto        base = normalize_name(base_text);
  std::string attr_name;
  if (move_to_sibling()) {
    attr_name = std::string{current_text()};
  }
  move_to_parent();
  if (attr_name.empty()) {
    return;
  }

  // Phase 2 — compute and publish the folded value before per-name dispatch
  // so the sticky/per-attr handlers see the same observable state every
  // pass other consumer would.
  evaluate_attr_get(dst, base_text, base, attr_name);

  dispatch_attr_get(attr_name, dst, base);
}

void uPass_attributes::process_if() {
  // Pre-scan the if's children to associate each arm-stmts node with its
  // controlling cond. The runner walks the body next; process_stmts hooks
  // pop the recorded arm and notify handlers about the cond's sticky refs.
  //
  // Layout — alternating (cond, stmts) pairs with an optional trailing
  // else-stmts:
  //   if(cond1, stmts1, [cond2, stmts2, ...], [else_stmts])
  //
  // Cond is always a ref or const at the top (compound conditions are
  // lowered to tmps in prp2lnast).
  if (!move_to_child()) {
    return;
  }
  std::vector<std::string> running_cond_refs;
  while (true) {
    const auto t = get_raw_ntype();
    if (Lnast_ntype::is_stmts(t)) {
      // This stmts is an arm body (then/else/elif). Record the most-recent
      // cond's refs against this stmts nid (1i: salt-qualified so callee
      // arm bodies don't alias caller nids during a source-swap).
      const auto  nid_key = lm->current_scope_uid();
      Pending_arm arm;
      arm.cond_refs = running_cond_refs;  // empty for bare-else stmts
      pending_arms.emplace(nid_key, std::move(arm));
      running_cond_refs.clear();
      if (!move_to_sibling()) {
        break;
      }
      continue;
    }
    if (Lnast_ntype::is_ref(t)) {
      running_cond_refs.emplace_back(normalize_name(current_text()));
    }
    // const cond contributes no refs; nothing to record.
    if (!move_to_sibling()) {
      break;
    }
  }
  move_to_parent();
}

void uPass_attributes::process_stmts() {
  const auto nid_key = lm->current_scope_uid();
  auto       it      = pending_arms.find(nid_key);
  if (it == pending_arms.end()) {
    return;
  }
  std::vector<std::string_view> refs;
  refs.reserve(it->second.cond_refs.size());
  for (const auto& r : it->second.cond_refs) {
    refs.emplace_back(r);
  }
  reg.for_each_handler([&](upass::attributes::Attribute_handler& h) { h.on_if_arm_enter(*this, refs); });
  active_arm_stack.push_back(nid_key);
}

void uPass_attributes::process_stmts_post() {
  const auto nid_key = lm->current_scope_uid();
  if (active_arm_stack.empty() || active_arm_stack.back() != nid_key) {
    return;  // not an arm stmts
  }
  reg.for_each_handler([this](upass::attributes::Attribute_handler& h) { h.on_if_arm_exit(*this); });
  active_arm_stack.pop_back();
  pending_arms.erase(nid_key);
}
