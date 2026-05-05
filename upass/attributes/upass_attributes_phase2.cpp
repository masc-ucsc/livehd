//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Phase 2 — constant-propagation-style evaluation of `.[attr]` reads.
//
// Implements the rules from attribute_todo.md §Phase 2:
//   * presence-only attr_set stores boolean true; explicit values
//     (including `false`) round-trip as the explicit Lconst.
//   * .[bits]/.[ubits]/.[sbits]/.[max]/.[min] derive from the base value or
//     declared type when no explicit attr is on the side-map.
//   * .[size] returns the field count (1 for scalars; aggregate cases are
//     Phase 3's job).
//   * .[comptime] resolves true iff the base value (or declaration) is
//     comptime-known.
//   * unset ordinary reads return Lconst::nil().
//
// The evaluator writes every successful fold to `tmp_fold` so the override
// of `fold_ref` substitutes the value at every downstream consumer.

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "boost/multiprecision/cpp_int.hpp"
#include "lconst.hpp"
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
// (so `100 → 7`, not `8`). Special-case 0 and -1 per Lconst's own contract.
uint32_t bits_natural(const Lconst& v) {
  if (v.is_nil() || v.is_invalid() || v.is_string() || v.has_unknowns()) {
    return 0;
  }
  if (v == 0) {
    return 0;
  }
  if (v == -1) {
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

uint32_t bits_unsigned(const Lconst& v) {
  if (v.is_nil() || v.is_invalid() || v.is_string() || v.has_unknowns() || v.is_negative()) {
    return 0;
  }
  if (v == 0) {
    return 0;
  }
  const auto sb = v.get_bits();
  return sb >= 1 ? sb - 1 : 0;
}

uint32_t bits_signed(const Lconst& v) {
  if (v.is_nil() || v.is_invalid() || v.is_string() || v.has_unknowns()) {
    return 0;
  }
  return v.get_bits();
}

// max/min for an unsigned type with `n` bits. n==0 means "unbounded" — the
// caller should treat that as "no derivation possible".
Lconst max_unsigned(uint32_t n) {
  using Number = boost::multiprecision::cpp_int;
  if (n == 0) {
    return Lconst(0);
  }
  return Lconst((Number(1) << n) - 1);
}

Lconst max_signed(uint32_t n) {
  using Number = boost::multiprecision::cpp_int;
  if (n == 0) {
    return Lconst(0);
  }
  return Lconst((Number(1) << (n - 1)) - 1);
}

Lconst min_signed(uint32_t n) {
  using Number = boost::multiprecision::cpp_int;
  if (n == 0) {
    return Lconst(0);
  }
  return Lconst(-(Number(1) << (n - 1)));
}

}  // namespace

std::optional<Lconst> uPass_attributes::lookup_attr_value(std::string_view var, std::string_view attr) const {
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
  auto it = type_info_map.find(std::string{var});
  if (it == type_info_map.end()) {
    return nullptr;
  }
  return &it->second;
}

std::optional<std::pair<Lconst, Lconst>> uPass_attributes::lookup_range(std::string_view tmp) const {
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

std::optional<Lconst> uPass_attributes::resolve_value(std::string_view var) const {
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

std::optional<Lconst> uPass_attributes::derive_max(std::string_view base) const {
  // Explicit attr wins.
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
  // Type-derived.
  if (auto* ti = lookup_type_info(base); ti && ti->bits != 0) {
    if (ti->kind == Numeric_kind::unsigned_int) {
      return max_unsigned(ti->bits);
    }
    if (ti->kind == Numeric_kind::signed_int) {
      return max_signed(ti->bits);
    }
  }
  // Untyped int / no width: fall back to the current value's natural bound
  // when available so `.[max] != nil` answers correctly.
  if (auto v = resolve_value(base); v) {
    if (v->is_negative()) {
      return max_signed(bits_signed(*v));
    }
    return max_unsigned(bits_unsigned(*v));
  }
  return std::nullopt;
}

std::optional<Lconst> uPass_attributes::derive_min(std::string_view base) const {
  if (auto v = lookup_attr_value(base, "min"); v) {
    return v;
  }
  if (auto tmp = lookup_attr_ref(base, "range"); tmp) {
    if (auto rb = lookup_range(*tmp); rb && rb->first.is_i()) {
      return rb->first;
    }
  }
  if (auto* ti = lookup_type_info(base); ti && ti->bits != 0) {
    if (ti->kind == Numeric_kind::unsigned_int) {
      return Lconst(0);
    }
    if (ti->kind == Numeric_kind::signed_int) {
      return min_signed(ti->bits);
    }
  }
  if (auto v = resolve_value(base); v) {
    if (v->is_negative()) {
      return min_signed(bits_signed(*v));
    }
    return Lconst(0);
  }
  return std::nullopt;
}

std::optional<Lconst> uPass_attributes::derive_bits(std::string_view base, std::string_view variant) const {
  auto v = resolve_value(base);
  if (!v) {
    return std::nullopt;
  }
  if (variant == "ubits") {
    return Lconst(static_cast<int64_t>(bits_unsigned(*v)));
  }
  if (variant == "sbits") {
    return Lconst(static_cast<int64_t>(bits_signed(*v)));
  }
  // "bits": the natural width — unsigned for non-negative values, signed
  // (including the sign bit) for negative values, 0 for zero, 1 for -1.
  return Lconst(static_cast<int64_t>(bits_natural(*v)));
}

std::optional<Lconst> uPass_attributes::derive_comptime(std::string_view base, std::string_view base_text) const {
  // Explicit attr wins (attr_set "comptime" true/false from prp2lnast or
  // user code).
  if (auto v = lookup_attr_value(base, "comptime"); v) {
    return *v == 0 ? Lconst(0) : Lconst(1);
  }
  if (auto* ti = lookup_type_info(base); ti && ti->is_comptime) {
    return Lconst(1);
  }
  // Uppercase identifier → implicit comptime (per spec).
  if (is_uppercase_ident(base_text) || is_uppercase_ident(base)) {
    if (resolve_value(base).has_value()) {
      return Lconst(1);
    }
  }
  // const declarations are comptime when their value is known.
  if (auto* ti = lookup_type_info(base); ti && ti->decl == Decl_kind::const_kind) {
    if (resolve_value(base).has_value()) {
      return Lconst(1);
    }
  }
  // Fallback: any value that constprop has fully resolved is comptime.
  if (resolve_value(base).has_value()) {
    return Lconst(1);
  }
  return Lconst(0);
}

void uPass_attributes::evaluate_attr_get(std::string_view dst, std::string_view base_text, std::string_view base,
                                         std::string_view attr) {
  std::optional<Lconst> result;

  // Sticky bucket presence — `_*` and `debug` reads return Lconst(1) when
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
          result = Lconst(1);
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
      // value is known.
      result = derive_aggregate_size(base);
      if (!result && resolve_value(base).has_value()) {
        result = Lconst(1);
      }
    } else if (attr == "comptime") {
      result = derive_comptime(base, base_text);
    } else if (attr == "typename") {
      result = derive_aggregate_typename(base, base_text);
    } else if (attr == "key") {
      // `.[key]` on a tuple_get tmp returns the source field's name; on a
      // bare aggregate it returns the aggregate's own name.
      if (auto* a = lookup_get_alias(base); a && !a->field_name.empty()) {
        result = Lconst::from_pyrope(std::string{"\'"} + a->field_name + "\'");
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

  // Unset / unresolved attrs: do NOT publish a fold. Leaving the dst tmp
  // unresolved keeps the cassert as "unknown" (not fail) for cases this
  // phase doesn't yet understand (e.g. aggregate `.[size]` in Phase 3),
  // and leaves the existing constprop "undeclared_ref + const(nil)" trick
  // free to handle `cassert ... == nil` against unset scalar attrs.
  if (!result) {
    return;
  }

  auto [it, inserted] = tmp_fold.emplace(std::string{dst}, *result);
  if (!inserted && it->second != *result) {
    it->second = *result;
    mark_changed();
  } else if (inserted) {
    mark_changed();
  }
}

std::optional<Lconst> uPass_attributes::fold_ref(std::string_view name) {
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
          auto v = Lconst::from_pyrope(current_text());
          if (v.is_i()) {
            bits = static_cast<uint32_t>(v.to_i());
          }
        }
        move_to_parent();
      }
    }
  }
  move_to_parent();

  auto& ti = type_info_map[target];
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
  auto read_value = [this]() -> Lconst {
    if (Lnast_ntype::is_const(get_raw_ntype())) {
      return Lconst::from_pyrope(current_text());
    }
    if (Lnast_ntype::is_ref(get_raw_ntype()) && runner_fold_fn) {
      auto v = runner_fold_fn(current_text());
      if (v) {
        return *v;
      }
    }
    return Lconst::invalid();
  };
  Lconst start = read_value();
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  Lconst end = read_value();
  move_to_parent();

  if (start.is_invalid() || end.is_invalid()) {
    return;
  }
  auto [it, inserted] = range_bounds.emplace(dst, std::make_pair(start, end));
  if (!inserted && (it->second.first != start || it->second.second != end)) {
    it->second = {start, end};
    mark_changed();
  } else if (inserted) {
    mark_changed();
  }
}
