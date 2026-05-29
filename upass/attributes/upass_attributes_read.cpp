//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Constant-propagation-style evaluation of `.[attr]` reads.
//
// Rules implemented:
//   * presence-only attr_set stores boolean true; explicit values
//     (including `false`) round-trip as the explicit Const.
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

#include "const.hpp"
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

// Lconst::get_bits stores SIGNED width — fine for negative values, but
// the spec wants `.[bits]` on a positive literal to be the unsigned width
// (so `100 → 7`, not `8`). Special-case 0 and -1 per Const's own contract.
uint32_t bits_natural(const Const& v) {
  if (v.is_nil() || v.is_invalid() || v.is_string() || v.has_unknowns()) {
    return 0;
  }
  if (v.is_known_zero()) {
    return 0;
  }
  if (v.is_i() && v.to_i() == -1) {
    return 1;
  }
  if (v.is_negative()) {
    return v.get_bits();
  }
  // Non-negative: minimal unsigned width. get_bits is signed-style (msb+2),
  // so subtract one to get the unsigned width.
  const auto sb = v.get_bits();
  return sb >= 1 ? sb - 1 : 0;
}

uint32_t bits_unsigned(const Const& v) {
  if (v.is_nil() || v.is_invalid() || v.is_string() || v.has_unknowns() || v.is_negative()) {
    return 0;
  }
  if (v.is_known_zero()) {
    return 0;
  }
  const auto sb = v.get_bits();
  return sb >= 1 ? sb - 1 : 0;
}

uint32_t bits_signed(const Const& v) {
  if (v.is_nil() || v.is_invalid() || v.is_string() || v.has_unknowns()) {
    return 0;
  }
  return v.get_bits();
}

// max/min for an unsigned type with `n` bits. n==0 means "unbounded" — the
// caller should treat that as "no derivation possible".
Const max_unsigned(uint32_t n) {
  if (n == 0) {
    return *Dlop::create_integer(0);
  }
  return *Dlop::get_mask_value(n);
}

Const max_signed(uint32_t n) {
  if (n == 0) {
    return *Dlop::create_integer(0);
  }
  return *Dlop::get_mask_value(n - 1);
}

Const min_signed(uint32_t n) {
  if (n == 0) {
    return *Dlop::create_integer(0);
  }
  return *Dlop::get_neg_mask_value(n - 1);
}

}  // namespace

std::optional<Const> uPass_attributes::lookup_attr_value(std::string_view var, std::string_view attr) const {
  auto it = attr_set_values.find(std::string{var});
  if (it == attr_set_values.end()) {
    return std::nullopt;
  }
  auto ait = it->second.find(std::string{attr});
  if (ait == it->second.end()) {
    return std::nullopt;
  }
  return ait->second;
}

const uPass_attributes::Type_info* uPass_attributes::lookup_type_info(std::string_view var) const {
  // Heterogeneous find on flat_hash_map<std::string, T>: no temporary
  // std::string allocation. This sits on the per-op record_assign() path
  // so the saving is multiplied across 1M+ ops on bulk-arithmetic inputs.
  if (auto it = type_info_map.find(var); it != type_info_map.end()) {
    return &it->second;
  }
  // Phase 8 typesystem: when prp2lnast lowers a tuple literal with typed
  // fields (`t = (a:u4=3, …)`), it emits `attr_set tg_ref "ubits" 4`
  // against a `tuple_get` tmp. The type_info therefore lives on the
  // tuple_get tmp, not on the dotted path. Two chains can land here:
  //   1. The attr_get's base is itself a tuple_get tmp (`tuple_get ___9
  //      t "a"; attr_get ___10 ___9 "bits"`). Follow the alias to
  //      `t.a`, then a sibling tuple_get tmp's type_info, if any.
  //   2. The base is already a dotted path like "t.a". Same final step.
  std::string base;
  std::string field;
  if (auto a_it = tuple_get_alias.find(std::string{var}); a_it != tuple_get_alias.end()) {
    base  = a_it->second.base;
    field = a_it->second.field_name.empty() ? a_it->second.field_key : a_it->second.field_name;
  } else {
    auto dot = var.find('.');
    if (dot == std::string_view::npos) {
      return nullptr;
    }
    base  = std::string{var.substr(0, dot)};
    field = std::string{var.substr(dot + 1)};
  }
  if (field.empty()) {
    return nullptr;
  }
  // Resolve `base` through direct_alias (e.g. `t` → `___1`) so the
  // search hits the canonical tuple tmp the type-emission targeted.
  std::string canonical_base = base;
  if (auto da = direct_alias.find(canonical_base); da != direct_alias.end()) {
    canonical_base = da->second;
  }
  // Field-aware migrated entry: if a prior migrate_alias copied the
  // type_info onto `base.field`, return that.
  {
    std::string path;
    path.reserve(base.size() + 1 + field.size());
    path  = base;
    path += '.';
    path += field;
    if (auto it = type_info_map.find(path); it != type_info_map.end()) {
      return &it->second;
    }
  }
  if (canonical_base != base) {
    std::string path;
    path.reserve(canonical_base.size() + 1 + field.size());
    path  = canonical_base;
    path += '.';
    path += field;
    if (auto it = type_info_map.find(path); it != type_info_map.end()) {
      return &it->second;
    }
  }
  // Fallback: scan tuple_get_alias for the sibling tmp whose base
  // matches the canonical base and field matches.
  for (const auto& [tmp, alias] : tuple_get_alias) {
    if (alias.base != canonical_base && alias.base != base) {
      continue;
    }
    if (alias.field_key == field || alias.field_name == field) {
      if (auto it = type_info_map.find(tmp); it != type_info_map.end()) {
        return &it->second;
      }
    }
  }
  return nullptr;
}

std::optional<std::pair<Const, Const>> uPass_attributes::lookup_range(std::string_view tmp) const {
  auto it = range_bounds.find(std::string{tmp});
  if (it == range_bounds.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<std::string> uPass_attributes::lookup_attr_ref(std::string_view var, std::string_view attr) const {
  auto it = attr_set_refs.find(std::string{var});
  if (it == attr_set_refs.end()) {
    return std::nullopt;
  }
  auto ait = it->second.find(std::string{attr});
  if (ait == it->second.end()) {
    return std::nullopt;
  }
  return ait->second;
}

std::optional<Const> uPass_attributes::resolve_value(std::string_view var) const {
  // Prefer the runner's aggregate fold (constprop's symbol table is the
  // primary source). Fall back to our own tmp_fold for refs that only this
  // pass knows about (e.g. chained attr_get→attr_get).
  if (runner_fold_fn) {
    auto v = runner_fold_fn(var);
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
// from explicit type annotations (uN, sN, bool, uint:[max=K],
// int:[max=K, min=L], or `:[range=...]`). Un-annotated values return nil.
// Tuple aggregates return nil for `.[bits]`. The `.[ubits]`/`.[sbits]`
// queries no longer exist; the dispatcher forwards `.[bits]` through here.

namespace {
// ceil_log2(x) for x >= 1. Returns 0 for x==0.
uint32_t ceil_log2_u64(uint64_t x) {
  if (x <= 1) {
    return 0;
  }
  return static_cast<uint32_t>(64 - std::countl_zero(static_cast<uint64_t>(x - 1)));
}
}  // namespace

std::optional<Const> uPass_attributes::derive_max(std::string_view base) const {
  // Explicit attr wins (e.g. `:int:[max=N]` partial pinning).
  if (auto v = lookup_attr_value(base, "max"); v) {
    return v;
  }
  // `range` lowering: an attr_set(var, "range", tmp) recorded the tmp ref
  // text; pair with range_bounds to materialize max.
  if (auto tmp = lookup_attr_ref(base, "range"); tmp) {
    if (auto rb = lookup_range(*tmp); rb && rb->second.is_i()) {
      return rb->second;
    }
  }
  // Typesystem redesign (entry 1v): declaration-driven. A concrete uN/sN
  // gives a value; bool has its own envelope; everything else (`:int`,
  // `:string`, untyped) reads nil — no value-based fallback.
  if (auto* ti = lookup_type_info(base); ti && ti->has_type_spec) {
    if (ti->kind == Numeric_kind::unsigned_int && ti->bits != 0) {
      return max_unsigned(ti->bits);
    }
    if (ti->kind == Numeric_kind::signed_int && ti->bits != 0) {
      return max_signed(ti->bits);
    }
    if (ti->kind == Numeric_kind::boolean) {
      return *Dlop::create_integer(0);
    }
  }
  return std::nullopt;
}

std::optional<Const> uPass_attributes::derive_min(std::string_view base) const {
  if (auto v = lookup_attr_value(base, "min"); v) {
    return v;
  }
  if (auto tmp = lookup_attr_ref(base, "range"); tmp) {
    if (auto rb = lookup_range(*tmp); rb && rb->first.is_i()) {
      return rb->first;
    }
  }
  if (auto* ti = lookup_type_info(base); ti && ti->has_type_spec) {
    if (ti->kind == Numeric_kind::unsigned_int && ti->bits != 0) {
      return *Dlop::create_integer(0);
    }
    if (ti->kind == Numeric_kind::signed_int && ti->bits != 0) {
      return min_signed(ti->bits);
    }
    if (ti->kind == Numeric_kind::boolean) {
      return *Dlop::create_integer(-1);
    }
  }
  return std::nullopt;
}

std::optional<Const> uPass_attributes::derive_bits(std::string_view base, std::string_view variant) const {
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
  // Derive bits from explicit max/min (e.g. `:int:[range=0..=15]` →
  // bits=4). Only fires when both bounds are pinned; otherwise the
  // envelope is incomplete and bits stays nil per the design.
  std::optional<Const> max_v = lookup_attr_value(base, "max");
  std::optional<Const> min_v = lookup_attr_value(base, "min");
  if (!max_v || !min_v) {
    if (auto tmp = lookup_attr_ref(base, "range"); tmp) {
      if (auto rb = lookup_range(*tmp); rb) {
        if (!min_v && rb->first.is_i()) {
          min_v = rb->first;
        }
        if (!max_v && rb->second.is_i()) {
          max_v = rb->second;
        }
      }
    }
  }
  if (max_v && min_v && max_v->is_i() && min_v->is_i()) {
    const int64_t mx = max_v->to_i();
    const int64_t mn = min_v->to_i();
    int64_t       bits;
    if (mn >= 0) {
      bits = mx > 0 ? static_cast<int64_t>(std::bit_width(static_cast<uint64_t>(mx))) : 0;
    } else {
      const int64_t b_pos = mx >= 0 ? static_cast<int64_t>(std::bit_width(static_cast<uint64_t>(mx))) + 1 : 1;
      const int64_t b_neg = static_cast<int64_t>(std::bit_width(static_cast<uint64_t>(-mn - 1))) + 1;
      bits                = std::max(b_pos, b_neg);
    }
    return *Dlop::create_integer(bits);
  }
  return std::nullopt;
}

std::optional<Const> uPass_attributes::derive_comptime(std::string_view base, std::string_view base_text) const {
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
  // field does.
  const Tuple_shape* sh = lookup_tuple_shape(base);
  std::string        sh_base{base};
  if (!sh) {
    if (auto it = shape_source.find(sh_base); it != shape_source.end()) {
      sh      = lookup_tuple_shape(it->second);
      sh_base = it->second;
    }
  }
  if (sh && !sh->fields.empty()) {
    bool all_comptime = true;
    for (const auto& f : sh->fields) {
      std::string field_path;
      field_path.reserve(sh_base.size() + 1 + std::max(f.name.size(), f.positional.size()));
      field_path.assign(sh_base);
      field_path.push_back('.');
      field_path.append(f.name.empty() ? f.positional : f.name);
      auto sub = derive_comptime(field_path, field_path);
      if (!sub || sub->is_known_zero()) {
        all_comptime = false;
        break;
      }
    }
    if (all_comptime) {
      return *Dlop::create_integer(1);
    }
  }
  return *Dlop::create_integer(0);
}

void uPass_attributes::evaluate_attr_get(std::string_view dst, std::string_view base_text, std::string_view base,
                                         std::string_view attr) {
  std::optional<Const> result;

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
    } else if (attr == "bits") {
      // Phase 3 — aggregate `tup.[bits]` is the sum of the per-field bits.
      // Falls back to scalar derivation when no tuple shape is known.
      result = derive_aggregate_bits(base);
      if (!result) {
        result = derive_bits(base, attr);
      }
    } else if (attr == "ubits" || attr == "sbits") {
      result = derive_bits(base, attr);
    } else if (attr == "size") {
      // Phase 3 — aggregate size first; scalar fallback gives 1 when the
      // value is known. Phase 8: strings report their character count.
      result = derive_aggregate_size(base);
      if (!result) {
        if (auto v = resolve_value(base); v) {
          if (v->is_string()) {
            // to_pyrope wraps strings in single quotes; strip both.
            auto repr = v->to_pyrope();
            int64_t n = 0;
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
    } else if (attr == "comptime") {
      result = derive_comptime(base, base_text);
    } else if (attr == "wrap") {
      // Built-in overflow-policy attribute: presence-only on declaration
      // (`x::[wrap]`) → true; an explicit `[wrap=false]` returns false; an
      // active policy without an explicit attr_set value returns true.
      // Otherwise the attr was never present on this var (statement-level
      // `wrap x = ...` erases any pre-recorded value), and per spec the
      // read is nil.
      if (auto v = lookup_attr_value(base, attr); v) {
        result = *v;
      } else if (has_wrap_policy(base)) {
        result = *Dlop::create_integer(1);
      } else {
        result = *Dlop::nil();
      }
    } else if (attr == "saturate" || attr == "sat") {
      // `sat` is shorthand for `saturate`; both names alias to the same
      // policy slot. Same nil-when-unset semantics as `wrap` above.
      auto v = lookup_attr_value(base, "saturate");
      if (!v) {
        v = lookup_attr_value(base, "sat");
      }
      if (v) {
        result = *v;
      } else if (has_sat_policy(base)) {
        result = *Dlop::create_integer(1);
      } else {
        result = *Dlop::nil();
      }
    } else if (attr == "typename") {
      result = derive_aggregate_typename(base, base_text);
    } else if (attr == "key") {
      // `.[key]` on a tuple_get tmp returns the source field's name; on a
      // bare aggregate it returns the aggregate's own name.
      if (auto* a = lookup_get_alias(base); a && !a->field_name.empty()) {
        result = *Dlop::from_pyrope(std::string{"\'"} + a->field_name + "\'");
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
  // later iteration of the runner.
  if (!result) {
    if (sticky_pattern || !is_builtin_attr(attr) || attr == "bits" || attr == "ubits" || attr == "sbits"
        || attr == "max" || attr == "min") {
      result = *Dlop::nil();
    } else {
      return;
    }
  }

  auto [it, inserted] = tmp_fold.emplace(std::string{dst}, *result);
  if (!inserted && !it->second.same_repr(*result)) {
    it->second = *result;
    mark_changed();
  } else if (inserted) {
    mark_changed();
  }
}

std::optional<Const> uPass_attributes::fold_ref(std::string_view name) {
  // Hot path: tmp_fold is empty on bulk-arithmetic workloads — skip the
  // std::string allocation that tmp_fold.find(std::string{name}) would
  // otherwise force on every per-op runner_fold_fn call (xx.prp: 6M+ calls).
  if (tmp_fold.empty()) {
    return std::nullopt;
  }
  auto it = tmp_fold.find(std::string{name});
  if (it == tmp_fold.end()) {
    return std::nullopt;
  }
  return it->second;
}

void uPass_attributes::process_type_spec() {
  // Layout (prp2lnast::emit_type_spec):
  //   type_spec
  //     ref(target)
  //     <prim_type_uint | prim_type_sint | prim_type_boolean | … |
  //      comp_type_array | unknown_type>
  //       [ const(width) ]            // primitive only
  //
  // We extract the kind and (when present) the explicit width, then merge
  // it into type_info_map for the target.
  if (!move_to_child()) {
    return;
  }
  auto target = normalize_name(current_text());
  if (target.empty()) {
    move_to_parent();
    return;
  }
  Numeric_kind kind = Numeric_kind::none;
  uint32_t     bits = 0;
  if (move_to_sibling()) {
    auto t = get_raw_ntype();
    if (Lnast_ntype::is_prim_type_uint(t)) {
      kind = Numeric_kind::unsigned_int;
    } else if (Lnast_ntype::is_prim_type_sint(t)) {
      kind = Numeric_kind::signed_int;
    } else if (Lnast_ntype::is_prim_type_boolean(t)) {
      kind = Numeric_kind::boolean;
    } else if (Lnast_ntype::is_prim_type_string(t)) {
      kind = Numeric_kind::string;
    }
    if (kind == Numeric_kind::unsigned_int || kind == Numeric_kind::signed_int) {
      // The width const is a child of the prim_type_* node.
      if (move_to_child()) {
        if (Lnast_ntype::is_const(get_raw_ntype())) {
          auto v = Dlop::from_pyrope(current_text());
          if (v->is_i()) {
            bits = static_cast<uint32_t>(v->to_i());
          }
        }
        move_to_parent();
      }
    }
  }
  move_to_parent();

  auto& ti         = type_info_map[target];
  ti.has_type_spec = true;
  if (kind != Numeric_kind::none) {
    ti.kind = kind;
  }
  if (bits != 0) {
    ti.bits = bits;
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
  auto read_value = [this]() -> Const {
    if (Lnast_ntype::is_const(get_raw_ntype())) {
      return *Dlop::from_pyrope(current_text());
    }
    if (Lnast_ntype::is_ref(get_raw_ntype()) && runner_fold_fn) {
      auto v = runner_fold_fn(current_text());
      if (v) {
        return *v;
      }
    }
    return *Dlop::invalid();
  };
  Const start = read_value();
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  Const end = read_value();
  move_to_parent();

  if (start.is_invalid() || end.is_invalid()) {
    return;
  }
  auto [it, inserted] = range_bounds.emplace(dst, std::make_pair(start, end));
  if (!inserted && (!it->second.first.same_repr(start) || !it->second.second.same_repr(end))) {
    it->second = {start, end};
    mark_changed();
  } else if (inserted) {
    mark_changed();
  }
}
