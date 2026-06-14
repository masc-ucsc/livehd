//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Constant-propagation-style evaluation of `.[attr]` reads.
//
// Rules implemented:
//   * presence-only attr_set stores boolean true; explicit values
//     (including `false`) round-trip as the explicit Dlop.
//   * .[bits]/.[ubits]/.[sbits]/.[max]/.[min] derive from the base value or
//     declared type when no explicit attr is on the side-map.
//   * .[size] returns the field count (1 for scalars; aggregate cases live
//     in upass_attributes_tuple.cpp).
//   * .[comptime] resolves true iff the base value (or declaration) is
//     comptime-known.
//   * unset ordinary reads return *Dlop::nil().
//
// The evaluator writes every successful fold to `tmp_fold` so the override
// of `fold_ref` substitutes the value at every downstream consumer.

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/strings/str_cat.h"
#include "hlop/dlop.hpp"
#include "range_bits.hpp"
#include "decl_facts.hpp"
#include "lnast.hpp"
#include "lnast_ntype.hpp"
#include "upass_attributes.hpp"
#include "upass_attributes_sticky.hpp"

namespace {

bool is_uppercase_ident(std::string_view name) {
  if (name.empty()) {
    return false;
  }
  return std::isupper(static_cast<unsigned char>(name.front())) != 0;
}

}  // namespace

std::optional<Dlop> uPass_attributes::lookup_attr_value(std::string_view var, std::string_view attr) const {
  // Explicit attr values live as residual attrs ON the binding
  // (field-scoped for dotted names). A dotted miss falls back to the root's
  // whole-bundle attr: aggregate-level attrs are inherited by every field
  // (`foo::[potato=4]` answers `foo.b.[potato]`) — replaces the legacy
  // migrate_aggregate_attrs_to_fields pre-copies.
  if (runner_st == nullptr || var.empty()) {
    return std::nullopt;
  }
  const auto root  = Bundle::get_first_level(var);
  const auto field = Bundle::get_all_but_first_level(var);
  const auto b     = runner_st->get_bundle(root);
  if (!b) {
    return std::nullopt;
  }
  if (!field.empty()) {
    if (const auto& v = b->get_attr(field, attr); !v.is_invalid()) {
      return v;
    }
    // Aggregate→field inheritance is cat-D only: builtin attrs (bits/max/
    // min/typename/…) describe ONE level and never leak from the container
    // onto a field (legacy Cat-D gate).
    if (is_builtin_attr(attr)) {
      return std::nullopt;
    }
  }
  if (const auto& v = b->get_attr(attr); !v.is_invalid()) {
    return v;
  }
  return std::nullopt;
}

const uPass_attributes::Type_info* uPass_attributes::lookup_type_info_bundle(std::string_view var) const {
  // Delegates to the shared derivation (upass/core/decl_facts.hpp);
  // the runner and constprop consume the same helper directly, replacing the
  // provide_* pull seams. Returns a pointer into a per-call scratch.
  if (runner_st == nullptr) {
    return nullptr;
  }
  const auto f = upass::decl_facts::lookup(*runner_st, lm ? lm->get_lnast().get() : nullptr, var);
  if (!f) {
    return nullptr;
  }
  Type_info ti;
  switch (f->kind) {
    case upass::decl_facts::Num::unsigned_int: ti.kind = Numeric_kind::unsigned_int; break;
    case upass::decl_facts::Num::signed_int  : ti.kind = Numeric_kind::signed_int; break;
    case upass::decl_facts::Num::boolean     : ti.kind = Numeric_kind::boolean; break;
    case upass::decl_facts::Num::string      : ti.kind = Numeric_kind::string; break;
    case upass::decl_facts::Num::none        : ti.kind = Numeric_kind::none; break;
  }
  switch (f->mode) {
    case upass::Mode::mut_kind  : ti.decl = Decl_kind::mut_kind; break;
    case upass::Mode::const_kind: ti.decl = Decl_kind::const_kind; break;
    case upass::Mode::reg_kind  : ti.decl = Decl_kind::reg_kind; break;
    case upass::Mode::await_kind: ti.decl = Decl_kind::await_kind; break;
    case upass::Mode::type_kind : ti.decl = Decl_kind::type_kind; break;
    default                     : ti.decl = Decl_kind::unknown; break;
  }
  ti.bits          = f->bits;
  ti.is_comptime   = f->is_comptime;
  ti.has_type_spec = f->has_type_spec;
  ti.range_max     = f->range_max;
  ti.range_min     = f->range_min;
  ti_scratch_      = std::move(ti);
  return &ti_scratch_;
}

const uPass_attributes::Type_info* uPass_attributes::lookup_type_info(std::string_view var) const {
  // Bundle-backed (see lookup_type_info_bundle). The legacy
  // type_info_map and its tuple_get_alias/direct_alias chase are gone.
  return lookup_type_info_bundle(var);
}

std::optional<std::pair<Dlop, Dlop>> uPass_attributes::lookup_range(std::string_view tmp) const {
  auto it = range_bounds.find(std::string{tmp});
  if (it == range_bounds.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<Dlop> uPass_attributes::resolve_value(std::string_view var) const {
  // Prefer the runner's aggregate fold (constprop's symbol table is the
  // primary source). Fall back to our own tmp_fold for refs that only this
  // pass knows about (e.g. chained attr_get→attr_get).
  if (runner_st != nullptr) {
    auto v = runner_st->known_const_scalar(var);
    if (v && !v->is_invalid()) {
      return *v;
    }
  }
  auto it = tmp_fold.find(std::string{var});
  if (it != tmp_fold.end() && !it->second.is_invalid()) {
    return it->second;
  }
  return std::nullopt;
}

// 1v locked policy: declared-type-only. `.[max]`/`.[min]`/`.[bits]` derive
// from explicit type annotations (uN, sN, bool, `int(max=K, min=L)`).
// Un-annotated values return nil. Tuple aggregates return nil for `.[bits]`.
// The `.[ubits]`/`.[sbits]` queries no longer exist; the dispatcher forwards
// `.[bits]` through here. (`:[range=…]` sugar was removed — semacheck rejects
// it like any other `:[max=]`/`:[min=]` attribute write.)

std::optional<Dlop> uPass_attributes::derive_bw(std::string_view base, bool want_max) const {
  // `.[bw_max]`/`.[bw_min]` — the bitwidth pass's running value range at this
  // statement point (debug-only; prp2lnast rejects non-assert uses). The
  // stamps ride the binding's "0" entry (upass/bitwidth write_bw, replace-on-
  // stamp, SSA-versioned names verbatim) and are scalar-only: a dotted base
  // has no per-field range and reads nil.
  if (runner_st != nullptr && base.find('.') == std::string_view::npos) {
    if (const auto b = runner_st->get_bundle(base); b) {
      const auto& e = b->get_entry("0");
      const auto& v = want_max ? e.bw_max : e.bw_min;
      if (!v.is_invalid() && !v.is_nil()) {
        return v;
      }
    }
  }
  // Cross-invocation persistence: a previous pass.upass run left ranges in
  // bw_meta (this walk's bundles start empty).
  if (lm != nullptr && lm->get_lnast() != nullptr) {
    const auto& meta = lm->get_lnast()->bw_meta();
    if (auto it = meta.ranges.find(std::string(base)); it != meta.ranges.end() && !it->second.unbounded) {
      return *Dlop::create_integer(want_max ? it->second.max : it->second.min);
    }
  }
  return std::nullopt;
}

std::optional<Dlop> uPass_attributes::derive_max(std::string_view base) const {
  // Explicit attr wins (e.g. `:int:[max=N]` partial pinning).
  if (auto v = lookup_attr_value(base, "max"); v) {
    return v;
  }
  // Typesystem redesign (entry 1v): declaration-driven. A concrete uN/sN
  // gives a value; bool has its own envelope; everything else (`:int`,
  // `:string`, untyped) reads nil — no value-based fallback.
  if (auto* ti = lookup_type_info(base); ti && ti->has_type_spec) {
    // prim_type_int range is the single source of truth.
    if (ti->range_max) {
      return *ti->range_max;
    }
    // No range_max pinned ⇒ max is unbounded (nil). range_max is the single
    // source of truth: `:u8` lowers to `int(0,255)` so a width type already
    // carries it; reconstructing a max FROM `bits` here would be the inverted
    // dependency the review (cat 1) calls out (bits is derived from max/min,
    // not the other way round). bool keeps its own {-1,0} envelope.
    if (ti->kind == Numeric_kind::boolean) {
      return *Dlop::create_integer(0);
    }
  }
  return std::nullopt;
}

std::optional<Dlop> uPass_attributes::derive_min(std::string_view base) const {
  if (auto v = lookup_attr_value(base, "min"); v) {
    return v;
  }
  if (auto* ti = lookup_type_info(base); ti && ti->has_type_spec) {
    // prim_type_int range is the single source of truth.
    if (ti->range_min) {
      return *ti->range_min;
    }
    // No range_min pinned ⇒ min is unbounded (nil). range_min is the single
    // source of truth (`:u8` lowers to `int(0,255)`); reconstructing a min FROM
    // `bits` is the inverted dependency the review (cat 1) calls out. bool keeps
    // its own {-1,0} envelope.
    if (ti->kind == Numeric_kind::boolean) {
      return *Dlop::create_integer(-1);
    }
  }
  return std::nullopt;
}

std::optional<Dlop> uPass_attributes::derive_bits(std::string_view base, std::string_view variant) const {
  // Explicit attr_set override (e.g. `:[bits=N]`).
  if (auto v = lookup_attr_value(base, variant); v) {
    return v;
  }
  // Typesystem redesign — declared-type authoritative; untyped → nil.
  if (auto* ti = lookup_type_info(base); ti && ti->has_type_spec) {
    if ((ti->kind == Numeric_kind::unsigned_int || ti->kind == Numeric_kind::signed_int) && ti->bits != 0) {
      return *Dlop::create_integer(static_cast<int64_t>(ti->bits));
    }
    if (ti->kind == Numeric_kind::boolean) {
      return *Dlop::create_integer(1);
    }
  }
  // Derive bits from explicit max/min. Only fires when both bounds are
  // pinned; otherwise the envelope is incomplete and bits stays nil per the
  // design.
  std::optional<Dlop> max_v = lookup_attr_value(base, "max");
  std::optional<Dlop> min_v = lookup_attr_value(base, "min");
  if (max_v && min_v && max_v->is_integer() && min_v->is_integer()) {
    // Derive bits from the bound Consts directly (handles >64-bit, no to_i).
    // get_bits() is the SIGNED width; for an unsigned range (min ≥ 0) drop the
    // sign bit, for a signed range take the widest signed bound.
    int64_t bits;
    if (!min_v->is_negative()) {
      bits = max_v->is_known_zero() ? 0 : static_cast<int64_t>(max_v->get_bits() - 1);
    } else {
      bits = std::max<int64_t>(max_v->get_bits(), min_v->get_bits());
    }
    return *Dlop::create_integer(bits);
  }
  return std::nullopt;
}

std::optional<Dlop> uPass_attributes::derive_comptime(std::string_view base, std::string_view base_text) const {
  // Explicit attr wins (attr_set "comptime" true/false from prp2lnast or
  // user code).
  if (auto v = lookup_attr_value(base, "comptime"); v) {
    return v->is_known_zero() ? *Dlop::create_integer(0) : *Dlop::create_integer(1);
  }
  if (auto* ti = lookup_type_info(base); ti && ti->is_comptime) {
    return *Dlop::create_integer(1);
  }
  // Uppercase identifier → implicit comptime (per spec).
  if (is_uppercase_ident(base_text) || is_uppercase_ident(base)) {
    if (resolve_value(base).has_value()) {
      return *Dlop::create_integer(1);
    }
  }
  // const declarations are comptime when their value is known.
  if (auto* ti = lookup_type_info(base); ti && ti->decl == Decl_kind::const_kind) {
    if (resolve_value(base).has_value()) {
      return *Dlop::create_integer(1);
    }
  }
  // Phase 8: comptime is non-sticky on copy. A `mut` declaration is NOT
  // comptime even if its value happens to be known — only explicit
  // `comptime` markers or `const` (resolved) qualify. Mut variables with
  // a known type decl read as not-comptime (returns 0); for a variable
  // with no decl_kind at all, leave unresolved.
  if (auto* ti = lookup_type_info(base); ti && ti->decl == Decl_kind::mut_kind) {
    return *Dlop::create_integer(0);
  }
  // Fallback: any value that constprop has fully resolved is comptime
  // (used for ref aliases / unnamed tmps that never got a decl_kind).
  if (resolve_value(base).has_value()) {
    return *Dlop::create_integer(1);
  }
  // Aggregate (tuple): comptime iff every field is comptime. This lets
  // `cassert(t.[comptime])` resolve when `t` itself has no scalar, but each
  // field does. The field set comes from the live BUNDLE (the
  // legacy tuple_shapes/shape_source side-maps are gone).
  if (runner_st != nullptr && base.find('.') == std::string_view::npos) {
    if (const auto b = runner_st->get_bundle(base); b && (b->has_named_top() || b->unnamed_top_count() > 0)) {
      bool any          = false;
      bool all_comptime = true;
      for (const auto& tl : b->top_levels()) {
        any = true;
        const std::string seg = tl.name.empty() ? std::to_string(tl.pos) : std::string(tl.name);
        std::string field_path;
        field_path.reserve(base.size() + 1 + seg.size());
        field_path.assign(base);
        field_path.push_back('.');
        field_path.append(seg);
        auto sub = derive_comptime(field_path, field_path);
        if (!sub || sub->is_known_zero()) {
          all_comptime = false;
          break;
        }
      }
      if (any && all_comptime) {
        return *Dlop::create_integer(1);
      }
    }
  }
  return *Dlop::create_integer(0);
}

void uPass_attributes::evaluate_attr_get(std::string_view dst, std::string_view base_text, std::string_view base,
                                         std::string_view attr) {
  std::optional<Dlop> result;

  // Sticky bucket presence — `_*` and `debug` reads return *Dlop::create_integer(1) when
  // the variable has acquired the bucket. When unmarked, fall through to
  // the explicit-set lookup so an `x::[debug=false]` read returns the
  // false (not nil); only when nothing explicit nor sticky is recorded do
  // we return nil.
  const bool sticky_pattern = upass::attributes::Sticky_handler::is_sticky_name(attr);
  if (sticky_pattern) {
    auto* h = reg.lookup(attr);
    if (auto* sh = dynamic_cast<upass::attributes::Sticky_handler*>(h); sh) {
      const auto bucket = upass::attributes::Sticky_handler::canonical_bucket(attr);
      if (sh->has_sticky(base, bucket)) {
        // Explicit value (including `false`) overrides the sticky boolean
        // when one was set on this variable directly.
        if (auto explicit_val = lookup_attr_value(base, attr); explicit_val) {
          result = *explicit_val;
        } else {
          result = *Dlop::create_integer(1);
        }
      }
    }
  }

  // Built-in derived attrs / explicit attr_set values (when no sticky
  // result was produced above).
  if (!result) {
    if (attr == "max") {
      result = derive_max(base);
    } else if (attr == "min") {
      result = derive_min(base);
    } else if (attr == "bw_max") {
      result = derive_bw(base, /*want_max=*/true);
    } else if (attr == "bw_min") {
      result = derive_bw(base, /*want_max=*/false);
    } else if (attr == "bits") {
      // Phase 3 — aggregate `tup.[bits]` is the sum of the per-field bits.
      // Falls back to scalar derivation when no tuple shape is known.
      result = derive_aggregate_bits(base);
      if (!result) {
        result = derive_bits(base, attr);
      }
    } else if (attr == "size") {
      // Phase 3 — aggregate size first; scalar fallback gives 1 when the
      // value is known. Phase 8: strings report their character count.
      result = derive_aggregate_size(base);
      if (!result) {
        if (auto v = resolve_value(base); v) {
          if (v->is_string()) {
            // to_pyrope wraps strings in single quotes; strip both.
            auto    repr = v->to_pyrope();
            int64_t n    = 0;
            if (repr.size() >= 2 && repr.front() == '\'' && repr.back() == '\'') {
              n = static_cast<int64_t>(repr.size() - 2);
            } else {
              n = static_cast<int64_t>(repr.size());
            }
            result = *Dlop::create_integer(n);
          } else {
            result = *Dlop::create_integer(1);
          }
        }
      }
    } else if (attr == "sign") {
      // Derived read (typesystem redesign): signed ⇔ the declared min can be
      // negative. Computed from the type's min (never a stored attr); nil when
      // the type pins no min (unbounded / untyped), matching bits/max/min.
      if (auto m = derive_min(base); m && !m->is_invalid() && !m->is_nil()) {
        result = *Dlop::create_integer(m->is_negative() ? 1 : 0);
      } else {
        result = *Dlop::nil();
      }
    } else if (attr == "comptime") {
      result = derive_comptime(base, base_text);
    } else if (attr == "typename") {
      result = derive_aggregate_typename(base, base_text);
    } else if (attr == "key") {
      // `.[key]` on a tuple_get tmp returns the source field's name; on a
      // bare aggregate it returns the aggregate's own name. The
      // extraction origin comes from Symbol_table::tget_origin.
      std::string field_seg;
      if (runner_st != nullptr) {
        if (auto oit = runner_st->tget_origin.find(std::string(base)); oit != runner_st->tget_origin.end()) {
          const auto f = Bundle::get_last_level(oit->second);
          // Named segments only (a positional index is not a key).
          if (!f.empty() && f.find_first_not_of("0123456789") != std::string_view::npos) {
            field_seg = std::string(f);
          }
        }
      }
      if (!field_seg.empty()) {
        result = *Dlop::from_pyrope(std::string{"\'"} + field_seg + "\'");
      } else {
        result = derive_aggregate_key(base, base_text);
      }
    } else if (auto v_inh = lookup_attr_with_inheritance(base, attr); v_inh) {
      // Phase 3 — cat-D aggregate→field inheritance: a tuple_get tmp's
      // attribute look-up chains through the alias to the parent aggregate.
      result = *v_inh;
    } else if (auto v = lookup_attr_value(base, attr); v) {
      result = *v;
    }
  }

  // Unset / unresolved attrs: per spec (§Phase 2), an unset ordinary
  // attribute reads as `nil`. Phase 8 typesystem redesign extends this to
  // the size-trio (`bits`/`max`/`min`) so an annotated-but-unbounded
  // declaration (`a:int = 3`) folds to nil instead of staying unresolved
  // — the new derive_* are declaration-driven and never depend on a
  // second walk over the tree. `typename` joins them: an anonymous
  // aggregate has no typename, which reads as nil (typesystem.prp §2) —
  // previously this leaned on constprop's undeclared-reads-as-nil fold,
  // now removed (undeclared reads are a prp2lnast compile error).
  if (!result) {
    if (sticky_pattern || !is_builtin_attr(attr) || attr == "bits" || attr == "max" || attr == "min" || attr == "typename"
        || attr == "bw_max" || attr == "bw_min") {
      result = *Dlop::nil();
    } else {
      return;
    }
  }

  auto [it, inserted] = tmp_fold.emplace(std::string{dst}, *result);
  if (!inserted && !it->second.same_repr(*result)) {
    it->second = *result;
  }
  // The derived value is the attr_get dst's VALUE: write it to the
  // binding too, so push-form operand resolution (table-only) sees it
  // directly off the table (tmp_fold stays attributes-internal).
  if (runner_st != nullptr && dst.find('.') == std::string_view::npos) {
    (void)runner_st->set(std::string(dst), *result);
  }
}

// fold_ref deleted — cross-pass folds land on the table (known_const_scalar).

void uPass_attributes::process_type_spec() {
  // Layout (prp2lnast::emit_type_spec):
  //   type_spec
  //     ref(target)
  //     <prim_type_int | prim_type_uint | prim_type_sint | prim_type_boolean |
  //      … | comp_type_array | unknown_type>
  //       [ const(width) ]                // prim_type_uint/sint (legacy)
  //       [ const(max) const(min) ]       // prim_type_int; "nil" = unbounded
  //
  // We extract the kind and (when present) the explicit width or range, then
  // merge it into type_info_map for the target.
  if (!move_to_child()) {
    return;
  }
  auto target = normalize_name(current_text());
  if (target.empty()) {
    move_to_parent();
    return;
  }
  Numeric_kind         kind = Numeric_kind::none;
  uint32_t             bits = 0;
  std::optional<Dlop> range_max;
  std::optional<Dlop> range_min;
  bool                 is_real_type = false;
  if (move_to_sibling()) {
    read_scalar_type_at_cursor(kind, bits, range_max, range_min, is_real_type);
  }
  move_to_parent();

  // The runner's declare/type_spec bake writes these facts onto the
  // binding (Entry kind + decl ranges; pending stash for dotted dsts);
  // lookup_type_info reads them back bundle-backed. Nothing to record here.
  (void)target;
  (void)kind;
  (void)bits;
  (void)range_max;
  (void)range_min;
  (void)is_real_type;
}

void uPass_attributes::read_scalar_type_at_cursor(Numeric_kind& kind, uint32_t& bits, std::optional<Dlop>& range_max,
                                                  std::optional<Dlop>& range_min, bool& is_real_type) {
  auto t = get_raw_ntype();
  if (Lnast_ntype::is_prim_type_int(t)) {
    // The canonical integer type carries its `(max,min)` range as
    // up to two const children; a "nil" (non-integer) child means that bound
    // is unbounded. (uN/sN now lower to this too — the legacy
    // prim_type_uint/sint width form is gone.)
    is_real_type = true;
    if (move_to_child()) {
      if (Lnast_ntype::is_const(get_raw_ntype())) {
        auto v = Dlop::from_pyrope(current_text());
        if (v->is_integer()) {
          range_max = *v;
        }
      }
      if (move_to_sibling() && Lnast_ntype::is_const(get_raw_ntype())) {
        auto v = Dlop::from_pyrope(current_text());
        if (v->is_integer()) {
          range_min = *v;
        }
      }
      move_to_parent();
    }
    // Recover the legacy kind/bits view from the range so wrap/sat narrowing
    // (which reads `kind`+`bits`) keeps working. Signedness from min<0; bits
    // derived from the bound Consts via get_bits() (signed width; drop the sign
    // bit for unsigned) — no to_i, handles >64-bit bounds.
    if (range_min) {
      kind = range_min->is_negative() ? Numeric_kind::signed_int : Numeric_kind::unsigned_int;
    }
    if (range_max && range_min && range_max->is_integer() && range_min->is_integer()) {
      if (!range_min->is_negative()) {
        bits = range_max->is_known_zero() ? 0 : static_cast<uint32_t>(range_max->get_bits() - 1);
      } else {
        bits = static_cast<uint32_t>(std::max<int64_t>(range_max->get_bits(), range_min->get_bits()));
      }
    }
  } else if (Lnast_ntype::is_prim_type_bool(t)) {
    kind         = Numeric_kind::boolean;
    is_real_type = true;
  } else if (Lnast_ntype::is_prim_type_string(t)) {
    kind         = Numeric_kind::string;
    is_real_type = true;
  }
}

void uPass_attributes::process_declare() {
  // Layout: declare( ref(var), TYPE, const(mode) [, value] )
  //   TYPE  : prim_type_int/uint/sint/boolean/string / comp_type_* /
  //           none_type (no `:type` annotation).
  //   mode  : space-joined tokens, storage first: "mut" | "const" | "reg" |
  //           "await" | "" + optional " comptime".  Mirrors the old
  //           attr_set(type,KIND) + attr_set(comptime,true) pair.
  //   value : optional init — IGNORED here (it stays a separate `store`; the
  //           value/range are recorded by constprop/bitwidth on that store).
  if (!move_to_child()) {
    return;
  }
  auto target = normalize_name(current_text());
  if (target.empty()) {
    move_to_parent();
    return;
  }
  Numeric_kind         kind = Numeric_kind::none;
  uint32_t             bits = 0;
  std::optional<Dlop> range_max;
  std::optional<Dlop> range_min;
  bool                 is_real_type = false;
  Decl_kind            decl         = Decl_kind::unknown;
  bool                 comptime     = false;
  if (move_to_sibling()) {  // TYPE
    read_scalar_type_at_cursor(kind, bits, range_max, range_min, is_real_type);
    if (move_to_sibling() && Lnast_ntype::is_const(get_raw_ntype())) {  // mode
      auto   mode  = current_text();
      // Split on spaces; storage token first, optional "comptime".
      size_t start = 0;
      while (start <= mode.size()) {
        size_t sp  = mode.find(' ', start);
        auto   tok = mode.substr(start, sp == std::string_view::npos ? std::string_view::npos : sp - start);
        if (tok == "mut") {
          decl = Decl_kind::mut_kind;
        } else if (tok == "const") {
          decl = Decl_kind::const_kind;
        } else if (tok == "reg") {
          decl = Decl_kind::reg_kind;
        } else if (tok == "await") {
          decl = Decl_kind::await_kind;
        } else if (tok == "type") {
          // A `type X = …` binding; the inliner rejects it as a
          // `ref` actual (ref-self methods need a mut value receiver).
          decl = Decl_kind::type_kind;
        } else if (tok == "comptime") {
          comptime = true;
        }
        if (sp == std::string_view::npos) {
          break;
        }
        start = sp + 1;
      }
    }
  }
  move_to_parent();

  // The runner's declare bake writes kind/decl ranges/mode/comptime
  // onto the binding; the const single-bind tally lives there too ("vbound"
  // attr — fresh clone scopes need no reset). Only the explicit-comptime
  // attr value remains map-side (E3e migrates attr_set_values).
  (void)kind;
  (void)bits;
  (void)range_max;
  (void)range_min;
  (void)is_real_type;
  (void)decl;
  if (comptime) {
    set_binding_attr(target, "comptime", *Dlop::create_integer(1));
  }
}

void uPass_attributes::process_range() {
  // Layout (prp2lnast::range_to_node):
  //   range
  //     ref(dst)
  //     (const|ref)(start)
  //     (const|ref)(end)
  if (!move_to_child()) {
    return;
  }
  auto dst = normalize_name(current_text());
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  // Resolve start/end opportunistically. Refs (e.g. comptime symbol) need
  // to be looked up via runner_fold_fn since this pass doesn't track its
  // own scalar table.
  auto read_value = [this]() -> Dlop {
    if (Lnast_ntype::is_const(get_raw_ntype())) {
      return *Dlop::from_pyrope(current_text());
    }
    if (Lnast_ntype::is_ref(get_raw_ntype()) && runner_st != nullptr) {
      auto v = runner_st->known_const_scalar(current_text());
      if (v) {
        return *v;
      }
    }
    return *Dlop::invalid();
  };
  Dlop start = read_value();
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  Dlop end = read_value();
  move_to_parent();

  if (start.is_invalid() || end.is_invalid()) {
    return;
  }
  auto [it, inserted] = range_bounds.emplace(dst, std::make_pair(start, end));
  if (!inserted && (!it->second.first.same_repr(start) || !it->second.second.same_repr(end))) {
    it->second = {start, end};
  }
}
